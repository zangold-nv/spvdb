cmake_minimum_required(VERSION 3.20)
project(spvdb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Third-party dependencies -----------------------------------------------

# SPIRV-Headers (header-only; provides spirv/unified1/spirv.h)
add_library(spirv-headers INTERFACE)
target_include_directories(spirv-headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SPIRV-Headers/include)

# nlohmann/json (header-only)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)

# Catch2 (for tests)
option(CATCH_INSTALL_DOCS "" OFF)
option(CATCH_INSTALL_EXTRAS "" OFF)
add_subdirectory(third_party/Catch2 EXCLUDE_FROM_ALL)

# replxx (for CLI)
option(REPLXX_BUILD_EXAMPLES "" OFF)
option(REPLXX_BUILD_PACKAGE "" OFF)
add_subdirectory(third_party/replxx EXCLUDE_FROM_ALL)

# ---- libspvdb ---------------------------------------------------------------

add_library(spvdb STATIC
    lib/parser/parser.cpp
    lib/module/module.cpp
    lib/interpreter/interpreter.cpp
    lib/interpreter/glsl_std_450.cpp
    lib/api/spvdb.cpp
)

target_include_directories(spvdb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

target_link_libraries(spvdb PUBLIC
    spirv-headers
    nlohmann_json
)

target_compile_options(spvdb PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W3 /wd4100>
)

# ---- spvdb CLI --------------------------------------------------------------

add_subdirectory(cli)

# ---- Tests ------------------------------------------------------------------

enable_testing()
add_subdirectory(tests)
