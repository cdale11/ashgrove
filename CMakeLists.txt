cmake_minimum_required(VERSION 3.25)
project(Ashgrove VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build types
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")

# Compiler warnings + optimization
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -Wno-missing-field-initializers
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-O0 -g -fsanitize=address,undefined)
        add_link_options(-fsanitize=address,undefined)
    else()
        # Tuned for the dev box: 4-core Haswell (AVX2/FMA/BMI2).
        # -march=native emits code that will not run on older CPUs — keep the
        # build on this machine (or change to -march=haswell for portability).
        add_compile_options(
            -O3
            -march=native
            -funroll-loops
            -fomit-frame-pointer
            -ffunction-sections
            -fdata-sections
            -flto
            -pipe
        )
        add_link_options(-flto -Wl,--gc-sections)
    endif()
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Find dependencies
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# vcpkg or system packages for: nlohmann_json, libuv, openssl, sqlite3, fmt, spdlog
# For now, use FetchContent for header-only libs

include(FetchContent)

# nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

# fmt
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt
    GIT_TAG 10.2.1
)
FetchContent_MakeAvailable(fmt)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog
    GIT_TAG v1.13.0
)
FetchContent_MakeAvailable(spdlog)

# libuv (if not system)
# find_package(libuv REQUIRED)

# Project structure
add_subdirectory(src/common)
add_subdirectory(src/simulation)
add_subdirectory(src/npc)
add_subdirectory(src/world)
add_subdirectory(src/network)
add_subdirectory(src/investigation)
add_subdirectory(src/quest)
add_subdirectory(src/ai)
add_subdirectory(src/server)

# Tests
enable_testing()
add_subdirectory(tests)

# Install
install(TARGETS ashgrove_server RUNTIME DESTINATION bin)