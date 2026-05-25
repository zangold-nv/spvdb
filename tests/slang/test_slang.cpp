#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "../../lib/api/spvdb.h"
#include "../../lib/api/spvdb_session.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace spvdb;

static const std::string k_spv = std::string(SLANG_TEST_SPV_DIR) + "/add_buffers.spv";

// Read first N uint32_t values back from a descriptor binding.
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

TEST_CASE("slang add_buffers: correct output") {
    auto mod = load_module_from_file(k_spv);
    REQUIRE(mod);

    auto sess = create_session(*mod, "main");
    REQUIRE(sess);

    // inputA[0]=7, inputB[0]=5, output[0]=0
    REQUIRE(set_descriptor_json(**sess, 0, 0, "[7]"));
    REQUIRE(set_descriptor_json(**sess, 0, 1, "[5]"));
    REQUIRE(set_descriptor_json(**sess, 0, 2, "[0]"));

    auto reason = run(**sess);
    if (reason == StopReason::Panic)
        FAIL("Panic: " + panic_message(**sess));
    REQUIRE(reason == StopReason::EntryFinished);

    auto out = read_u32s(**sess, 0, 2, 1);
    CHECK(out[0] == 12u);
}

TEST_CASE("slang add_buffers: local_variables shows a and b") {
    auto mod = load_module_from_file(k_spv);
    REQUIRE(mod);

    auto sess = create_session(*mod, "main");
    REQUIRE(sess);

    REQUIRE(set_descriptor_json(**sess, 0, 0, "[7]"));
    REQUIRE(set_descriptor_json(**sess, 0, 1, "[5]"));
    REQUIRE(set_descriptor_json(**sess, 0, 2, "[0]"));

    bool saw_a = false, saw_b = false;
    for (int i = 0; i < 2000; ++i) {
        auto stop = step_instruction(**sess);
        auto locals = local_variables(**sess);
        for (auto& lv : locals) {
            if (lv.name == "a" &&
                lv.value.kind == Value::Kind::UInt32 &&
                lv.value.scalar.u32 == 7u)
                saw_a = true;
            if (lv.name == "b" &&
                lv.value.kind == Value::Kind::UInt32 &&
                lv.value.scalar.u32 == 5u)
                saw_b = true;
        }
        if (stop == StopReason::EntryFinished || stop == StopReason::Panic) break;
    }

    CHECK(saw_a);
    CHECK(saw_b);
}

TEST_CASE("slang add_buffers: source locations are valid") {
    auto mod = load_module_from_file(k_spv);
    REQUIRE(mod);

    auto sess = create_session(*mod, "main");
    REQUIRE(sess);

    REQUIRE(set_descriptor_json(**sess, 0, 0, "[1]"));
    REQUIRE(set_descriptor_json(**sess, 0, 1, "[1]"));
    REQUIRE(set_descriptor_json(**sess, 0, 2, "[0]"));

    bool saw_valid_loc = false;
    for (int i = 0; i < 2000; ++i) {
        auto loc = current_location(**sess);
        if (loc.line != 0) saw_valid_loc = true;
        auto stop = step_instruction(**sess);
        if (stop == StopReason::EntryFinished || stop == StopReason::Panic) break;
    }

    CHECK(saw_valid_loc);
}
