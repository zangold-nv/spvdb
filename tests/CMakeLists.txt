add_executable(spvdb_tests
    unit/test_parser.cpp
    unit/test_module.cpp
    unit/test_interpreter.cpp
)

target_link_libraries(spvdb_tests PRIVATE spvdb Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(spvdb_tests)
