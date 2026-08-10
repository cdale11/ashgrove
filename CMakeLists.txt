cmake_minimum_required(VERSION 3.16)
project(ashgrove LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release optimization
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -flto")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")

# Use cpp-httplib (header-only, no Boost)
include(FetchContent)
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib
  GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

# nlohmann/json for protocol
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(ashgrove_server
  src/main.cpp
  src/server.cpp
  src/world.cpp
)

target_include_directories(ashgrove_server PRIVATE include ${httplib_SOURCE_DIR} ${nlohmann_json_SOURCE_DIR}/include)
target_link_libraries(ashgrove_server PRIVATE pthread)
target_compile_options(ashgrove_server PRIVATE -Wall -Wextra -Wpedantic)