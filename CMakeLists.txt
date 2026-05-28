cmake_minimum_required(VERSION 3.20)
project(spvdb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Detect whether we are the top-level project or included via add_subdirectory.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(SPVDB_IS_TOPLEVEL TRUE)
else()
    set(SPVDB_IS_TOPLEVEL FALSE)
endif()

# ---- Third-party dependencies -----------------------------------------------

include(FetchContent)

# SPIRV-Headers (git submodule; header-only; provides spirv/unified1/spirv.h)
if(NOT TARGET spirv-headers)
    add_library(spirv-headers INTERFACE)
    target_include_directories(spirv-headers INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SPIRV-Headers/include)
endif()

# stb (single-file headers; image loading via stb_image.h; committed in-tree)
# Guard: the parent project (e.g. slang) may already define a 'stb' target.
if(NOT TARGET stb)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
endif()

# nlohmann/json (header-only) — use local copy if present, else fetch.
if(NOT TARGET nlohmann_json)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include/nlohmann/json.hpp)
        add_library(nlohmann_json INTERFACE)
        target_include_directories(nlohmann_json INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
    else()
        FetchContent_Declare(nlohmann_json_src
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG        d96329f8d6dee5da67c5ebd861acab25445d0ddb)
        FetchContent_Populate(nlohmann_json_src)
        add_library(nlohmann_json INTERFACE)
        target_include_directories(nlohmann_json INTERFACE
            ${nlohmann_json_src_SOURCE_DIR}/include)
    endif()
endif()

# Catch2, replxx, CLI, tools, and tests are only needed for the standalone build.
if(SPVDB_IS_TOPLEVEL)
    # Catch2 (for tests) — use local copy if present, else fetch.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Catch2/CMakeLists.txt)
        set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
        set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
        add_subdirectory(third_party/Catch2 EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        69e0473f6e98d47c93518424c08ee69ee632c0f0)
        set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
        set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(Catch2)
    endif()

    # replxx (for CLI) — use local copy if present, else fetch.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/replxx/CMakeLists.txt)
        set(REPLXX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(REPLXX_BUILD_PACKAGE  OFF CACHE BOOL "" FORCE)
        add_subdirectory(third_party/replxx EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(replxx
            GIT_REPOSITORY https://github.com/AmokHuginnsson/replxx.git
            GIT_TAG        1f149bfe20bf6e49c1afd4154eaf0032c8c2fda2)
        set(REPLXX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(REPLXX_BUILD_PACKAGE  OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(replxx)
    endif()
endif()

# ---- libspvdb ---------------------------------------------------------------

add_library(spvdb STATIC
    lib/parser/parser.cpp
    lib/module/module.cpp
    lib/interpreter/interpreter.cpp
    lib/interpreter/glsl_std_450.cpp
    lib/interpreter/image_sampler.cpp
    lib/api/spvdb.cpp
)

target_include_directories(spvdb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

target_link_libraries(spvdb PUBLIC
    spirv-headers
    nlohmann_json
    stb
)

# When embedded in another project, skip the stb_image implementation to avoid
# duplicate symbol errors if the parent already compiles it.
if(NOT SPVDB_IS_TOPLEVEL)
    target_compile_definitions(spvdb PRIVATE SPVDB_SKIP_STB_IMAGE_IMPL=1)
endif()

target_compile_options(spvdb PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W3 /wd4100>
)

# ---- spvdb CLI, tools, and tests (standalone build only) --------------------

if(SPVDB_IS_TOPLEVEL)
    add_subdirectory(cli)
    enable_testing()
    add_subdirectory(tests)
endif()
