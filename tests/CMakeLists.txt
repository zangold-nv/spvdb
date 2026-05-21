# ---- Assemble SPIR-V test binaries ------------------------------------------

find_program(SPIRV_AS spirv-as)

set(SPIRV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/spirv)
set(SPIRV_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/spirv)
file(MAKE_DIRECTORY ${SPIRV_BIN_DIR})

set(SPVASM_SOURCES
    ${SPIRV_DIR}/minimal.spvasm
    ${SPIRV_DIR}/copy_uint.spvasm
    ${SPIRV_DIR}/add_uint.spvasm
    ${SPIRV_DIR}/loop_sum.spvasm
    ${SPIRV_DIR}/loop_sum_inline.spvasm
    ${SPIRV_DIR}/spec_const.spvasm
    ${SPIRV_DIR}/passthrough_frag.spvasm
    ${SPIRV_DIR}/deriv_frag.spvasm
)

set(SPV_BINARIES "")
foreach(SPVASM_FILE ${SPVASM_SOURCES})
    get_filename_component(STEM ${SPVASM_FILE} NAME_WE)
    set(SPV_FILE ${SPIRV_BIN_DIR}/${STEM}.spv)
    list(APPEND SPV_BINARIES ${SPV_FILE})
    add_custom_command(
        OUTPUT  ${SPV_FILE}
        COMMAND ${SPIRV_AS} --target-env vulkan1.1 ${SPVASM_FILE} -o ${SPV_FILE}
        DEPENDS ${SPVASM_FILE}
        COMMENT "Assembling ${STEM}.spvasm"
    )
endforeach()

add_custom_target(spvdb_test_spirv ALL DEPENDS ${SPV_BINARIES})

# ---- Test executable --------------------------------------------------------

add_executable(spvdb_tests
    unit/test_parser.cpp
    unit/test_module.cpp
    unit/test_interpreter.cpp
)

add_dependencies(spvdb_tests spvdb_test_spirv)

target_link_libraries(spvdb_tests PRIVATE spvdb Catch2::Catch2WithMain)

# Pass the directory containing assembled .spv files to the test binary.
target_compile_definitions(spvdb_tests PRIVATE
    SPIRV_TEST_DIR="${SPIRV_BIN_DIR}"
)

include(Catch)
catch_discover_tests(spvdb_tests)
