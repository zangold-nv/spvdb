// spvdb-test: scriptable debugger front-end for use with slang-test.
//
// Usage:
//   spvdb-test <source.slang> <compiled.spv>
//
// The source file is scanned for lines of the form:
//   // SPVDB-CMD: <command> [args...]
//
// Commands are executed in order against the loaded module. Output is printed
// to stdout in a format that FileCheck (with prefix "SPVDB") can match.
//
// Supported commands:
//   entry <name>               Select entry point (default: "main")
//   descriptor <set> <bind> <json>  Bind a buffer/array descriptor
//   break <file_suffix> <line> Set a source breakpoint
//   run                        Run until a stop; prints "stopped: <reason>"
//   step                       Step one source line; prints "stopped: <reason>"
//   stepi                      Step one instruction; prints "stopped: <reason>"
//   continue                   Alias for "run"
//   location                   Print "location: <file>:<line>"
//   locals                     Print "local: <name> = <value>" for each local
//   backtrace                  Print "frame[N]: <func> at <file>:<line>"
//   read <set> <bind>          Print "read[set][bind]: <json>" (first 16 u32s)

#include "../../lib/api/spvdb.h"
#include "../../lib/api/spvdb_session.h"
#include "../../lib/core/value.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace spvdb;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string stop_reason_str(StopReason r) {
    switch (r) {
        case StopReason::Breakpoint:    return "Breakpoint";
        case StopReason::Step:         return "Step";
        case StopReason::EntryReached: return "EntryReached";
        case StopReason::EntryFinished:return "EntryFinished";
        case StopReason::Panic:        return "Panic";
    }
    return "Unknown";
}

static std::string value_str(const Value& v) {
    switch (v.kind) {
        case Value::Kind::Bool:   return v.scalar.b ? "true" : "false";
        case Value::Kind::Int32:  return std::to_string(v.scalar.i32);
        case Value::Kind::UInt32: return std::to_string(v.scalar.u32);
        case Value::Kind::Int64:  return std::to_string(v.scalar.i64);
        case Value::Kind::UInt64: return std::to_string(v.scalar.u64);
        case Value::Kind::Float32:  {
            std::ostringstream os;
            os << v.scalar.f32;
            return os.str();
        }
        case Value::Kind::Float64: {
            std::ostringstream os;
            os << v.scalar.f64;
            return os.str();
        }
        case Value::Kind::Composite: {
            std::string s = "(";
            for (size_t i = 0; i < v.elements.size(); ++i) {
                if (i) s += ", ";
                s += value_str(v.elements[i]);
            }
            return s + ")";
        }
        default: return "<complex>";
    }
}

// Trim leading whitespace from a string.
static std::string ltrim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t");
    return (start == std::string::npos) ? "" : s.substr(start);
}

// Split a line into whitespace-separated tokens (respects quoted strings with "").
static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream ss(line);
    std::string tok;
    while (ss >> tok) {
        tokens.push_back(tok);
    }
    return tokens;
}

// Extract SPVDB-CMD directives from a source file.
static std::vector<std::string> extract_commands(const std::string& path) {
    std::vector<std::string> cmds;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "spvdb-test: cannot open source file: " << path << "\n";
        return cmds;
    }
    std::string line;
    while (std::getline(f, line)) {
        // Look for "// SPVDB-CMD:" prefix (with optional leading whitespace)
        std::string trimmed = ltrim(line);
        const std::string prefix = "// SPVDB-CMD:";
        if (trimmed.size() >= prefix.size() &&
            trimmed.compare(0, prefix.size(), prefix) == 0) {
            std::string cmd = ltrim(trimmed.substr(prefix.size()));
            if (!cmd.empty())
                cmds.push_back(cmd);
        }
    }
    return cmds;
}

// ---------------------------------------------------------------------------
// Command executor
// ---------------------------------------------------------------------------

static int run_commands(const std::string& spv_path,
                        const std::vector<std::string>& commands) {
    // Load module.
    auto mod_result = load_module_from_file(spv_path);
    if (!mod_result) {
        std::cerr << "spvdb-test: failed to load SPIR-V: " << spv_path << "\n";
        return 1;
    }
    auto mod = *mod_result;

    std::string entry_name = "main";
    std::unique_ptr<Session> sess;

    // Helper: ensure session exists and is started.
    auto ensure_session = [&]() -> bool {
        if (!sess) {
            auto r = create_session(mod, entry_name);
            if (!r) {
                std::cerr << "spvdb-test: create_session failed: " << r.error().message << "\n";
                return false;
            }
            sess = std::move(*r);
        }
        return true;
    };

    for (const auto& cmd_line : commands) {
        auto tokens = tokenize(cmd_line);
        if (tokens.empty()) continue;
        const std::string& cmd = tokens[0];

        if (cmd == "entry") {
            if (tokens.size() < 2) {
                std::cerr << "spvdb-test: entry requires <name>\n";
                return 1;
            }
            entry_name = tokens[1];
            // Reset session if already created.
            sess.reset();

        } else if (cmd == "descriptor") {
            if (tokens.size() < 4) {
                std::cerr << "spvdb-test: descriptor requires <set> <binding> <json>\n";
                return 1;
            }
            if (!ensure_session()) return 1;
            uint32_t set     = static_cast<uint32_t>(std::stoul(tokens[1]));
            uint32_t binding = static_cast<uint32_t>(std::stoul(tokens[2]));
            // Re-join remaining tokens as the JSON value (allows spaces in JSON).
            std::string json;
            for (size_t i = 3; i < tokens.size(); ++i) {
                if (i > 3) json += " ";
                json += tokens[i];
            }
            auto r = set_descriptor_json(*sess, set, binding, json);
            if (!r) {
                std::cerr << "spvdb-test: set_descriptor_json failed: " << r.error().message << "\n";
                return 1;
            }

        } else if (cmd == "break") {
            if (tokens.size() < 3) {
                std::cerr << "spvdb-test: break requires <file_suffix> <line>\n";
                return 1;
            }
            if (!ensure_session()) return 1;
            std::string file  = tokens[1];
            uint32_t    line  = static_cast<uint32_t>(std::stoul(tokens[2]));
            auto r = set_breakpoint(*sess, file, line);
            if (!r) {
                std::cerr << "spvdb-test: set_breakpoint failed: " << r.error().message << "\n";
                return 1;
            }

        } else if (cmd == "run" || cmd == "continue") {
            if (!ensure_session()) return 1;
            StopReason reason = run(*sess);
            std::cout << "stopped: " << stop_reason_str(reason);
            if (reason == StopReason::Panic)
                std::cout << ": " << panic_message(*sess);
            std::cout << "\n";

        } else if (cmd == "step") {
            if (!ensure_session()) return 1;
            StopReason reason = step(*sess);
            std::cout << "stopped: " << stop_reason_str(reason);
            if (reason == StopReason::Panic)
                std::cout << ": " << panic_message(*sess);
            std::cout << "\n";

        } else if (cmd == "stepi") {
            if (!ensure_session()) return 1;
            StopReason reason = step_instruction(*sess);
            std::cout << "stopped: " << stop_reason_str(reason);
            if (reason == StopReason::Panic)
                std::cout << ": " << panic_message(*sess);
            std::cout << "\n";

        } else if (cmd == "location") {
            if (!ensure_session()) return 1;
            auto loc = current_location(*sess);
            std::cout << "location: " << loc.file << ":" << loc.line << "\n";

        } else if (cmd == "locals") {
            if (!ensure_session()) return 1;
            auto vars = local_variables(*sess);
            for (const auto& lv : vars) {
                std::cout << "local: " << lv.name << " = " << value_str(lv.value) << "\n";
            }
            if (vars.empty())
                std::cout << "locals: (none)\n";

        } else if (cmd == "backtrace") {
            if (!ensure_session()) return 1;
            auto bt = backtrace(*sess);
            for (size_t i = 0; i < bt.size(); ++i) {
                std::cout << "frame[" << i << "]: " << bt[i].function_name
                          << " at " << bt[i].loc.file << ":" << bt[i].loc.line << "\n";
            }
            if (bt.empty())
                std::cout << "backtrace: (empty)\n";

        } else if (cmd == "read") {
            if (tokens.size() < 3) {
                std::cerr << "spvdb-test: read requires <set> <binding>\n";
                return 1;
            }
            if (!ensure_session()) return 1;
            uint32_t set     = static_cast<uint32_t>(std::stoul(tokens[1]));
            uint32_t binding = static_cast<uint32_t>(std::stoul(tokens[2]));
            auto r = read_descriptor(*sess, set, binding);
            if (!r) {
                std::cerr << "spvdb-test: read_descriptor failed: " << r.error().message << "\n";
                return 1;
            }
            const auto& bytes = *r;
            size_t n_u32 = bytes.size() / sizeof(uint32_t);
            std::cout << "read[" << set << "][" << binding << "]: [";
            for (size_t i = 0; i < n_u32; ++i) {
                if (i) std::cout << ", ";
                uint32_t val;
                std::memcpy(&val, bytes.data() + i * sizeof(uint32_t), sizeof(uint32_t));
                std::cout << val;
            }
            std::cout << "]\n";

        } else {
            std::cerr << "spvdb-test: unknown command: " << cmd << "\n";
            return 1;
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: spvdb-test <source.slang> <compiled.spv>\n";
        return 1;
    }

    std::string source_path = argv[1];
    std::string spv_path    = argv[2];

    auto commands = extract_commands(source_path);
    if (commands.empty()) {
        std::cerr << "spvdb-test: no SPVDB-CMD directives found in: " << source_path << "\n";
        return 1;
    }

    return run_commands(spv_path, commands);
}
