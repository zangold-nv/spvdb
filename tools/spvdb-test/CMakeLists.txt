add_executable(spvdb-test main.cpp)

target_link_libraries(spvdb-test PRIVATE spvdb)

target_compile_options(spvdb-test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W3 /wd4100>
)

install(TARGETS spvdb-test RUNTIME DESTINATION bin)
