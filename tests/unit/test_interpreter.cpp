#include <catch2/catch_test_macros.hpp>
#include "../../lib/api/spvdb.h"
#include "../../lib/api/spvdb_session.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace spvdb;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string spv(const char* name) {
    return std::string(SPIRV_TEST_DIR) + "/" + name + ".spv";
}

// Build a JSON array string from a vector of uint32 values, e.g. "[1, 2, 3]".
static std::string json_u32(std::initializer_list<uint32_t> vals) {
    std::string s = "[";
    bool first = true;
    for (auto v : vals) {
        if (!first) s += ", ";
        s += std::to_string(v);
        first = false;
    }
    return s + "]";
}

// Read the first N uint32 values back from a descriptor binding.
static std::vector<uint32_t> read_u32s(Session& sess, uint32_t set,
                                        uint32_t binding, size_t count) {
    auto r = read_descriptor(sess, set, binding);
    REQUIRE(r);
    std::vector<uint32_t> out(count);
    size_t bytes = count * sizeof(uint32_t);
    REQUIRE(r->size() >= bytes);
    std::memcpy(out.data(), r->data(), bytes);
    return out;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("interpreter: minimal compute shader runs to completion") {
    auto mod = load_module_from_file(spv("minimal"));
    REQUIRE(mod);

    auto sess = create_session(*mod, "main");
    REQUIRE(sess);

    auto reason = run(**sess);
    CHECK(reason == StopReason::EntryFinished);
}

TEST_CASE("interpreter: entry point enumeration") {
    auto mod = load_module_from_file(spv("minimal"));
    REQUIRE(mod);

    auto eps = list_entry_points(**mod);
    REQUIRE(eps.size() == 1);
    CHECK(eps[0].name == "main");
    CHECK(eps[0].execution_model == ExecutionModel::GLCompute);
}

TEST_CASE("interpreter: copy_uint — output[0] == input[0]") {
    auto mod = load_module_from_file(spv("copy_uint"));
    REQUIRE(mod);

    auto sess = create_session(*mod, "main");
    REQUIRE(sess);

    REQUIRE(set_descriptor_json(**sess, 0, 0, json_u32({42})));
    REQUIRE(set_descriptor_json(**sess, 0, 1, json_u32({0})));

    auto reason = run(**sess);
    if (reason == StopReason::Panic)
        FAIL("Panic: " + panic_message(**sess));
    REQUIRE(reason == StopReason::EntryFinished);

    auto out = read_u32s(**sess, 0, 1, 1);
    CHECK(out[0] == 42u);
}

TEST_CASE("interpreter: add_uint — output[0] == input[0] + input[1]") {
    auto mod = load_module_from_file(spv("add_uint"));
    REQUIRE(mod);

    auto sess = create_session(*mod, "main");
    REQUIRE(sess);

    REQUIRE(set_descriptor_json(**sess, 0, 0, json_u32({7, 13})));
    REQUIRE(set_descriptor_json(**sess, 0, 1, json_u32({0})));

    auto reason = run(**sess);
    if (reason == StopReason::Panic)
        FAIL("Panic: " + panic_message(**sess));
    REQUIRE(reason == StopReason::EntryFinished);

    auto out = read_u32s(**sess, 0, 1, 1);
    CHECK(out[0] == 20u);
}

TEST_CASE("interpreter: add_uint — multiple invocations produce independent results") {
    auto mod = load_module_from_file(spv("add_uint"));
    REQUIRE(mod);

    for (uint32_t a : {0u, 1u, 100u, 0xFFFF0000u}) {
        uint32_t b = 5;
        auto sess = create_session(*mod, "main");
        REQUIRE(sess);
        REQUIRE(set_descriptor_json(**sess, 0, 0, json_u32({a, b})));
        REQUIRE(set_descriptor_json(**sess, 0, 1, json_u32({0})));
        auto reason = run(**sess);
        if (reason == StopReason::Panic)
            FAIL("Panic: " + panic_message(**sess));
        REQUIRE(reason == StopReason::EntryFinished);
        auto out = read_u32s(**sess, 0, 1, 1);
        CHECK(out[0] == (a + b));
    }
}

TEST_CASE("interpreter: loop_sum_inline — sums 4 elements via inline loop") {
    auto mod = load_module_from_file(spv("loop_sum_inline"));
    REQUIRE(mod);

    auto sess = create_session(*mod, "main");
    REQUIRE(sess);

    REQUIRE(set_descriptor_json(**sess, 0, 0, json_u32({10, 20, 30, 40})));
    REQUIRE(set_descriptor_json(**sess, 0, 1, json_u32({0})));

    auto reason = run(**sess);
    if (reason == StopReason::Panic)
        FAIL("Panic: " + panic_message(**sess));
    REQUIRE(reason == StopReason::EntryFinished);

    auto out = read_u32s(**sess, 0, 1, 1);
    CHECK(out[0] == 100u);
}

TEST_CASE("interpreter: loop_sum — sums 4 elements via loop and function call") {
    auto mod = load_module_from_file(spv("loop_sum"));
    REQUIRE(mod);

    auto sess = create_session(*mod, "main");
    REQUIRE(sess);

    REQUIRE(set_descriptor_json(**sess, 0, 0, json_u32({10, 20, 30, 40})));
    REQUIRE(set_descriptor_json(**sess, 0, 1, json_u32({0})));

    auto reason = run(**sess);
    if (reason == StopReason::Panic)
        FAIL("Panic: " + panic_message(**sess));
    REQUIRE(reason == StopReason::EntryFinished);

    auto out = read_u32s(**sess, 0, 1, 1);
    CHECK(out[0] == 100u);
}

TEST_CASE("interpreter: spec_const — default multiplier (3) applied") {
    auto mod = load_module_from_file(spv("spec_const"));
    REQUIRE(mod);

    auto sess = create_session(*mod, "main");
    REQUIRE(sess);

    REQUIRE(set_descriptor_json(**sess, 0, 0, json_u32({7})));
    REQUIRE(set_descriptor_json(**sess, 0, 1, json_u32({0})));

    auto reason = run(**sess);
    if (reason == StopReason::Panic)
        FAIL("Panic: " + panic_message(**sess));
    REQUIRE(reason == StopReason::EntryFinished);
    auto out = read_u32s(**sess, 0, 1, 1);
    CHECK(out[0] == 21u);  // 7 * 3
}

TEST_CASE("interpreter: spec_const — overridden multiplier applied") {
    auto mod = load_module_from_file(spv("spec_const"));
    REQUIRE(mod);

    auto sess = create_session(*mod, "main");
    REQUIRE(sess);

    // Override SpecId 7 to value 10.
    REQUIRE(set_spec_constant(**sess, 7, static_cast<uint32_t>(10)));

    REQUIRE(set_descriptor_json(**sess, 0, 0, json_u32({5})));
    REQUIRE(set_descriptor_json(**sess, 0, 1, json_u32({0})));

    auto reason = run(**sess);
    if (reason == StopReason::Panic)
        FAIL("Panic: " + panic_message(**sess));
    REQUIRE(reason == StopReason::EntryFinished);
    auto out = read_u32s(**sess, 0, 1, 1);
    CHECK(out[0] == 50u);  // 5 * 10
}

TEST_CASE("interpreter: step_instruction advances through copy_uint") {
    auto mod = load_module_from_file(spv("copy_uint"));
    REQUIRE(mod);

    auto sess = create_session(*mod, "main");
    REQUIRE(sess);

    REQUIRE(set_descriptor_json(**sess, 0, 0, json_u32({99})));
    REQUIRE(set_descriptor_json(**sess, 0, 1, json_u32({0})));

    // Step until done; should never panic.
    StopReason reason = StopReason::Step;
    int steps = 0;
    while (reason != StopReason::EntryFinished && reason != StopReason::Panic) {
        reason = step_instruction(**sess);
        REQUIRE(steps++ < 1000);  // Guard against infinite loops.
    }
    if (reason == StopReason::Panic)
        FAIL("Panic: " + panic_message(**sess));
    REQUIRE(reason == StopReason::EntryFinished);

    auto out = read_u32s(**sess, 0, 1, 1);
    CHECK(out[0] == 99u);
}

TEST_CASE("interpreter: breakpoint on result id stops execution") {
    auto mod = load_module_from_file(spv("add_uint"));
    REQUIRE(mod);

    auto sess = create_session(*mod, "main");
    REQUIRE(sess);

    REQUIRE(set_descriptor_json(**sess, 0, 0, json_u32({3, 4})));
    REQUIRE(set_descriptor_json(**sess, 0, 1, json_u32({0})));

    // id 0 won't match any real result id — run should finish normally.
    auto bp = set_breakpoint_at_id(**sess, 0);
    auto reason = run(**sess);
    if (reason == StopReason::Panic)
        FAIL("Panic: " + panic_message(**sess));
    CHECK(reason == StopReason::EntryFinished);

    auto out = read_u32s(**sess, 0, 1, 1);
    CHECK(out[0] == 7u);
}
