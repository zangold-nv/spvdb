#include "../lib/api/spvdb.h"
#include "../lib/api/spvdb_session.h"  // complete Session definition for unique_ptr
#include <replxx.hxx>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace spvdb;
using Replxx = replxx::Replxx;

// ---- state -----------------------------------------------------------------

static std::shared_ptr<Module>       g_module;
static std::unique_ptr<Session>      g_session;
static std::string                   g_entry;

// ---- helpers ---------------------------------------------------------------

static std::vector<std::string> split(const std::string& s) {
    std::istringstream iss(s);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

static std::string value_to_string(const Value& v) {
    switch (v.kind) {
        case Value::Kind::Bool:    return v.scalar.b ? "true" : "false";
        case Value::Kind::Int32:   return std::to_string(v.scalar.i32);
        case Value::Kind::UInt32:  return std::to_string(v.scalar.u32);
        case Value::Kind::Float32: return std::to_string(v.scalar.f32);
        case Value::Kind::Int64:   return std::to_string(v.scalar.i64);
        case Value::Kind::UInt64:  return std::to_string(v.scalar.u64);
        case Value::Kind::Float64: return std::to_string(v.scalar.f64);
        case Value::Kind::Composite: {
            std::string s = "{";
            for (size_t i = 0; i < v.elements.size(); ++i) {
                if (i) s += ", ";
                s += value_to_string(v.elements[i]);
            }
            return s + "}";
        }
        case Value::Kind::Pointer:
            return "ptr(sc=" + std::to_string(v.ptr_storage_class) +
                   ", base=%" + std::to_string(v.ptr_base_var) + ")";
    }
    return "?";
}

static void ensure_session() {
    if (!g_session && g_module && !g_entry.empty()) {
        auto r = create_session(g_module, g_entry);
        if (r) g_session = std::move(*r);
        else std::cerr << "Error creating session: " << r.error().message << "\n";
    }
}

// ---- command handlers ------------------------------------------------------

static void cmd_file(const std::vector<std::string>& args) {
    if (args.size() < 2) { std::cout << "Usage: file <path.spv>\n"; return; }
    auto r = load_module_from_file(args[1]);
    if (!r) { std::cerr << "Error: " << r.error().message << "\n"; return; }
    g_module  = std::move(*r);
    g_session = nullptr;
    g_entry   = {};
    std::cout << "Loaded " << args[1] << "\n";
    // Auto-list entry points.
    auto eps = list_entry_points(*g_module);
    std::cout << eps.size() << " entry point(s):\n";
    for (auto& ep : eps) {
        const char* model = ep.execution_model == ExecutionModel::GLCompute ? "GLCompute" :
                            ep.execution_model == ExecutionModel::Vertex    ? "Vertex"    :
                            ep.execution_model == ExecutionModel::Fragment  ? "Fragment"  : "Other";
        std::cout << "  " << ep.name << " (" << model << ")\n";
    }
}

static void cmd_info_entries() {
    if (!g_module) { std::cout << "No module loaded.\n"; return; }
    auto eps = list_entry_points(*g_module);
    for (auto& ep : eps) {
        const char* model = ep.execution_model == ExecutionModel::GLCompute ? "GLCompute" :
                            ep.execution_model == ExecutionModel::Vertex    ? "Vertex"    :
                            ep.execution_model == ExecutionModel::Fragment  ? "Fragment"  : "Other";
        std::cout << ep.name << " (" << model << ")\n";
    }
}

static void cmd_entry(const std::vector<std::string>& args) {
    if (args.size() < 2) { std::cout << "Usage: entry <name>\n"; return; }
    g_entry   = args[1];
    g_session = nullptr;
    std::cout << "Entry point set to: " << g_entry << "\n";
}

static void cmd_run() {
    if (!g_module) { std::cout << "No module loaded.\n"; return; }
    if (g_entry.empty()) { std::cout << "No entry point selected. Use: entry <name>\n"; return; }
    // (Re)create session and run.
    auto r = create_session(g_module, g_entry);
    if (!r) { std::cerr << "Error: " << r.error().message << "\n"; return; }
    g_session = std::move(*r);
    auto reason = spvdb::run(*g_session);
    switch (reason) {
        case StopReason::EntryFinished: std::cout << "Program finished normally.\n"; break;
        case StopReason::Breakpoint:    std::cout << "Breakpoint hit.\n"; break;
        case StopReason::Panic:         std::cerr << "Panic: " << panic_message(*g_session) << "\n"; break;
        default: break;
    }
}

static void print_stop_reason(StopReason reason) {
    switch (reason) {
        case StopReason::EntryFinished: std::cout << "Program finished normally.\n"; break;
        case StopReason::Panic:         std::cerr << "Panic: " << panic_message(*g_session) << "\n"; break;
        case StopReason::Breakpoint: {
            std::cout << "Breakpoint hit";
            auto loc = current_location(*g_session);
            if (loc.line) std::cout << " at " << loc.file << ":" << loc.line;
            std::cout << "\n";
            break;
        }
        default: {
            // For step/next: print current location if known.
            auto loc = current_location(*g_session);
            if (loc.line) std::cout << loc.file << ":" << loc.line << "\n";
            break;
        }
    }
}

static void cmd_stepi() {
    if (!g_session) { ensure_session(); if (!g_session) return; }
    auto reason = step_instruction(*g_session);
    print_stop_reason(reason);
}

static void cmd_step() {
    if (!g_session) { ensure_session(); if (!g_session) return; }
    auto reason = step(*g_session);
    print_stop_reason(reason);
}

static void cmd_next() {
    if (!g_session) { ensure_session(); if (!g_session) return; }
    auto reason = step_over(*g_session);
    print_stop_reason(reason);
}

static void cmd_continue() {
    if (!g_session) { ensure_session(); if (!g_session) return; }
    auto reason = spvdb::run(*g_session);
    print_stop_reason(reason);
}

static void cmd_finish() {
    if (!g_session) { std::cout << "No active session.\n"; return; }
    auto reason = step_out(*g_session);
    print_stop_reason(reason);
}

static void cmd_break(const std::vector<std::string>& args) {
    if (!g_module) { std::cout << "No module loaded.\n"; return; }
    ensure_session();
    if (!g_session) return;
    if (args.size() < 2) { std::cout << "Usage: break <file>:<line>  or  break %<result-id>\n"; return; }
    const std::string& loc = args[1];
    if (!loc.empty() && loc[0] == '%') {
        uint32_t rid = static_cast<uint32_t>(std::stoul(loc.substr(1)));
        auto r = set_breakpoint_at_id(*g_session, rid);
        if (r) std::cout << "Breakpoint " << r->id << " at %" << rid << "\n";
        return;
    }
    auto colon = loc.rfind(':');
    if (colon == std::string::npos) { std::cout << "Usage: break <file>:<line>\n"; return; }
    std::string file = loc.substr(0, colon);
    uint32_t line = static_cast<uint32_t>(std::stoul(loc.substr(colon + 1)));
    auto r = set_breakpoint(*g_session, file, line);
    if (r) std::cout << "Breakpoint " << r->id << " at " << file << ":" << line << "\n";
}

static void cmd_delete(const std::vector<std::string>& args) {
    if (!g_session) { std::cout << "No active session.\n"; return; }
    if (args.size() < 2) { std::cout << "Usage: delete <bp-id>\n"; return; }
    uint32_t id = static_cast<uint32_t>(std::stoul(args[1]));
    remove_breakpoint(*g_session, BreakpointId{id});
    std::cout << "Deleted breakpoint " << id << "\n";
}

static void cmd_info_breakpoints() {
    std::cout << "(breakpoint list not yet queryable via public API)\n";
}

static void cmd_backtrace() {
    if (!g_session) { std::cout << "No active session.\n"; return; }
    auto frames = backtrace(*g_session);
    if (frames.empty()) { std::cout << "(no stack frames)\n"; return; }
    for (size_t i = 0; i < frames.size(); ++i) {
        std::cout << "#" << i << "  " << frames[i].function_name;
        if (frames[i].loc.line)
            std::cout << " at " << frames[i].loc.file << ":" << frames[i].loc.line;
        std::cout << "\n";
    }
}

static void cmd_where() { cmd_backtrace(); }

static void cmd_info_outputs() {
    if (!g_session) { std::cout << "No active session.\n"; return; }
    auto outs = output_variables(*g_session);
    if (outs.empty()) { std::cout << "(no output variables)\n"; return; }
    for (auto& v : outs)
        std::cout << v.name << " = " << value_to_string(v.value) << "\n";
}

static void cmd_info_locals() {
    if (!g_session) { std::cout << "No active session.\n"; return; }
    auto vars = local_variables(*g_session);
    if (vars.empty()) { std::cout << "(no local variables)\n"; return; }
    for (auto& v : vars)
        std::cout << v.name << " = " << value_to_string(v.value) << "\n";
}

static void cmd_print(const std::vector<std::string>& args) {
    if (args.size() < 2) { std::cout << "Usage: print <var>\n"; return; }
    if (!g_session) { std::cout << "No active session.\n"; return; }
    auto r = evaluate_variable(*g_session, args[1]);
    if (!r) { std::cerr << r.error().message << "\n"; return; }
    std::cout << r->name << " = " << value_to_string(r->value) << "\n";
}

static void cmd_set_input(const std::vector<std::string>& args) {
    // set input <set> <binding> <json_literal_or_filename>
    if (args.size() < 5) { std::cout << "Usage: set input <set> <binding> <json>\n"; return; }
    if (!g_module) { std::cout << "No module loaded.\n"; return; }
    ensure_session();
    if (!g_session) return;

    uint32_t s_idx   = std::stoul(args[2]);
    uint32_t b_idx   = std::stoul(args[3]);
    std::string data = args[4];
    // Concatenate remaining args (in case JSON has spaces).
    for (size_t i = 5; i < args.size(); ++i) data += " " + args[i];

    // Detect inline JSON vs filename.
    if (!data.empty() && (data[0] == '[' || data[0] == '{')) {
        auto r = set_descriptor_json(*g_session, s_idx, b_idx, data);
        if (!r) std::cerr << "Error: " << r.error().message << "\n";
        else    std::cout << "Set descriptor " << s_idx << ":" << b_idx << "\n";
    } else {
        // Treat as filename ending in .json — load and pass.
        std::ifstream f(data);
        if (!f) { std::cerr << "Cannot open file: " << data << "\n"; return; }
        std::string json((std::istreambuf_iterator<char>(f)), {});
        auto r = set_descriptor_json(*g_session, s_idx, b_idx, json);
        if (!r) std::cerr << "Error: " << r.error().message << "\n";
        else    std::cout << "Set descriptor " << s_idx << ":" << b_idx << " from file\n";
    }
}

static void cmd_set_builtin(const std::vector<std::string>& args) {
    // set builtin <name> <val0> [val1] [val2]
    if (args.size() < 4) { std::cout << "Usage: set builtin <name> <val...>\n"; return; }
    ensure_session();
    if (!g_session) return;
    // Map common builtin names to SpvBuiltIn values.
    uint32_t bi = 0;
    if      (args[2] == "GlobalInvocationID")  bi = SpvBuiltInGlobalInvocationId;
    else if (args[2] == "LocalInvocationID")   bi = SpvBuiltInLocalInvocationId;
    else if (args[2] == "WorkgroupID")         bi = SpvBuiltInWorkgroupId;
    else if (args[2] == "LocalInvocationIndex")bi = SpvBuiltInLocalInvocationIndex;
    else { std::cerr << "Unknown builtin: " << args[2] << "\n"; return; }

    // Parse value (x, y, z or single scalar).
    if (args.size() >= 6) {
        Value v = Value::make_composite({
            Value::make_u32(std::stoul(args[3])),
            Value::make_u32(std::stoul(args[4])),
            Value::make_u32(std::stoul(args[5]))
        });
        set_builtin(*g_session, bi, v);
    } else {
        set_builtin(*g_session, bi, Value::make_u32(std::stoul(args[3])));
    }
    std::cout << "Set builtin " << args[2] << "\n";
}

static void cmd_set_specconst(const std::vector<std::string>& args) {
    if (args.size() < 4) { std::cout << "Usage: set specconst <id> <val>\n"; return; }
    ensure_session();
    if (!g_session) return;
    uint32_t spec_id = std::stoul(args[2]);
    // Try int, then float.
    try {
        set_spec_constant(*g_session, spec_id, static_cast<int32_t>(std::stoi(args[3])));
        std::cout << "Set specconst " << spec_id << " = " << args[3] << "\n";
    } catch (...) {
        set_spec_constant(*g_session, spec_id, std::stof(args[3]));
        std::cout << "Set specconst " << spec_id << " = " << args[3] << "\n";
    }
}

// ---- dispatch --------------------------------------------------------------

static bool dispatch(const std::string& line) {
    auto tokens = split(line);
    if (tokens.empty()) return true;

    const std::string& cmd = tokens[0];

    if (cmd == "quit" || cmd == "q" || cmd == "exit") return false;

    if (cmd == "file") { cmd_file(tokens); return true; }
    if (cmd == "entry")   { cmd_entry(tokens);    return true; }
    if (cmd == "run" || cmd == "r")      { cmd_run();            return true; }
    if (cmd == "continue" || cmd == "c") { cmd_continue();        return true; }
    if (cmd == "step"  || cmd == "s")    { cmd_step();            return true; }
    if (cmd == "next"  || cmd == "n")    { cmd_next();            return true; }
    if (cmd == "finish")                 { cmd_finish();          return true; }
    if (cmd == "stepi" || cmd == "si")   { cmd_stepi();           return true; }
    if (cmd == "break" || cmd == "b")    { cmd_break(tokens);     return true; }
    if (cmd == "delete" || cmd == "d")   { cmd_delete(tokens);    return true; }
    if (cmd == "backtrace" || cmd == "bt" || cmd == "where") { cmd_backtrace(); return true; }
    if (cmd == "print" || cmd == "p")    { cmd_print(tokens);     return true; }
    if (cmd == "info") {
        if (tokens.size() > 1 && tokens[1] == "entries")     { cmd_info_entries();     return true; }
        if (tokens.size() > 1 && tokens[1] == "outputs")     { cmd_info_outputs();     return true; }
        if (tokens.size() > 1 && tokens[1] == "locals")      { cmd_info_locals();      return true; }
        if (tokens.size() > 1 && tokens[1] == "breakpoints") { cmd_info_breakpoints(); return true; }
        std::cout << "info subcommands: entries, outputs, locals, breakpoints\n"; return true;
    }
    if (cmd == "set") {
        if (tokens.size() > 1 && tokens[1] == "input")     { cmd_set_input(tokens);     return true; }
        if (tokens.size() > 1 && tokens[1] == "builtin")   { cmd_set_builtin(tokens);   return true; }
        if (tokens.size() > 1 && tokens[1] == "specconst") { cmd_set_specconst(tokens); return true; }
        std::cout << "set subcommands: input, builtin, specconst\n"; return true;
    }
    if (cmd == "help" || cmd == "h" || cmd == "?") {
        std::cout <<
            "Commands:\n"
            "  file <path.spv>          Load SPIR-V module\n"
            "  info entries             List entry points\n"
            "  entry <name>             Select entry point\n"
            "  run / r                  Execute from the start\n"
            "  continue / c             Continue execution to next breakpoint\n"
            "  step / s                 Step one source line (into calls)\n"
            "  next / n                 Step one source line (over calls)\n"
            "  finish                   Run until current function returns\n"
            "  stepi / si               Step one SPIR-V instruction\n"
            "  break <file>:<line>      Set a source breakpoint\n"
            "  break %<id>              Set a result-id breakpoint\n"
            "  delete <bp-id>           Remove a breakpoint\n"
            "  info breakpoints         List breakpoints\n"
            "  backtrace / bt           Print call stack\n"
            "  info locals              Print local variables\n"
            "  info outputs             Print output variables\n"
            "  print <var> / p <var>    Print a variable\n"
            "  set input <s> <b> <json> Bind descriptor from JSON\n"
            "  set builtin <name> <val> Set a built-in value\n"
            "  set specconst <id> <val> Override a spec constant\n"
            "  quit / q                 Exit\n";
        return true;
    }

    std::cerr << "Unknown command: " << cmd << " (try 'help')\n";
    return true;
}

// ---- main ------------------------------------------------------------------

int main(int argc, char** argv) {
    Replxx rx;
    rx.set_word_break_characters(" \t\n");
    rx.history_load(".spvdb_history");

    std::cout << "spvdb — SPIR-V debugger  (type 'help' for commands)\n";

    // If a .spv was passed as argument, auto-load it.
    if (argc >= 2) {
        std::vector<std::string> args = {"file", argv[1]};
        cmd_file(args);
        if (argc >= 3) {
            std::vector<std::string> eargs = {"entry", argv[2]};
            cmd_entry(eargs);
        }
    }

    while (true) {
        const char* line_raw = rx.input("(spvdb) ");
        if (!line_raw) break;  // EOF / Ctrl-D
        std::string line(line_raw);
        if (line.empty()) continue;
        rx.history_add(line);
        if (!dispatch(line)) break;
    }

    rx.history_save(".spvdb_history");
    return 0;
}
