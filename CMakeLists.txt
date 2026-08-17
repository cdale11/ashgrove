cmake_minimum_required(VERSION 3.25)
project(ashgrove LANGUAGES CXX VERSION 0.6.0)

# ---------------------------------------------------------------------------
# C++ Standard & Baseline
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Compile Options
# ---------------------------------------------------------------------------
set(COMMON_WARNING_FLAGS
  "-Wall -Wextra -Wpedantic"
  "-Wconversion -Wsign-conversion"
  "-Wshadow -Wnon-virtual-dtor"
  "-Wold-style-cast -Wcast-align"
  "-Wunused -Woverloaded-virtual"
  "-Wdouble-promotion -Wformat=2"
  "-Wnull-dereference"
)

# Release: maximum optimization, LTO, native arch
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -flto=auto -ffat-lto-objects -DNDEBUG")
# RelWithDebInfo: optimized with debug symbols
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -flto=auto")
# Debug: no optimization, full debug, sanitizers
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -fno-omit-frame-pointer")

# ---------------------------------------------------------------------------
# Parallelism / Concurrency Support
# ---------------------------------------------------------------------------
# Threads (required for pthread, std::thread, std::execution::par)
find_package(Threads REQUIRED)

# Enable C++17 parallel algorithms (requires TBB or libstdc++ parallel mode)
# We'll use std::execution::par with GCC's libstdc++ parallel mode
# For full TBB support, add: find_package(TBB REQUIRED) and link TBB::tbb

# ---------------------------------------------------------------------------
# Sanitizers (Debug builds only)
# ---------------------------------------------------------------------------
option(ENABLE_SANITIZERS "Enable Address/Undefined/Thread/Leak sanitizers" OFF)
if(ENABLE_SANITIZERS)
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address,undefined,leak -fno-omit-frame-pointer")
  set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined,leak")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=thread")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=thread")
    set(SANITIZER_LIST "ASan, UBSan, LSan, TSan")
  else()
    set(SANITIZER_LIST "ASan, UBSan, LSan")
  endif()
  message(STATUS "Sanitizers ENABLED (${SANITIZER_LIST})")
endif()

# ---------------------------------------------------------------------------
# Dependencies via FetchContent
# ---------------------------------------------------------------------------
include(FetchContent)

# cpp-httplib (header-only HTTP server)
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib
  GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

# nlohmann/json (header-only JSON)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

# llama.cpp (static library for local LLM)
FetchContent_Declare(
  llama_cpp
  GIT_REPOSITORY https://github.com/ggerganov/llama.cpp
  GIT_TAG master
)
FetchContent_MakeAvailable(llama_cpp)

# ---------------------------------------------------------------------------
# Target: ashgrove_server
# ---------------------------------------------------------------------------
add_executable(ashgrove_server
  src/main.cpp
  src/server.cpp
  src/world.cpp
  src/event_bus.cpp
  src/plugin.cpp
  src/llama_wrapper.cpp
  src/intent_engine.cpp
  src/command_log.cpp
  src/town_consciousness.cpp
  src/cognitive_core.cpp
  src/cognitive_registry.cpp
  src/social_cognition.cpp
  src/nature_mind.cpp
  src/village_mind.cpp
)

# Include plugin sources
file(GLOB PLUGIN_SRCS "plugins/*.cpp")
if(PLUGIN_SRCS)
  target_sources(ashgrove_server PRIVATE ${PLUGIN_SRCS})
endif()

target_include_directories(ashgrove_server PRIVATE
  include
  ${httplib_SOURCE_DIR}
  ${nlohmann_json_SOURCE_DIR}/include
  ${llama_cpp_SOURCE_DIR}
)

target_link_libraries(ashgrove_server PRIVATE
  Threads::Threads
  llama
)

target_compile_options(ashgrove_server PRIVATE
  -Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion
  -Wshadow -Wnon-virtual-dtor
  -Wold-style-cast -Wcast-align
  -Wunused -Woverloaded-virtual
  -Wdouble-promotion -Wformat=2
  -Wnull-dereference
  -Werror=return-type
  -Werror=non-virtual-dtor
)

# ---------------------------------------------------------------------------
# Link-time Optimization (LTO)
# ---------------------------------------------------------------------------
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.19)
  target_link_options(ashgrove_server PRIVATE
    "$<$<CONFIG:Release>:-flto=auto>"
    "$<$<CONFIG:RelWithDebInfo>:-flto=auto>"
  )
endif()

# ---------------------------------------------------------------------------
# Install (optional)
# ---------------------------------------------------------------------------
install(TARGETS ashgrove_server
  RUNTIME DESTINATION bin
)

# ---------------------------------------------------------------------------
# Testing Support (CTest)
# ---------------------------------------------------------------------------
enable_testing()

# Unit tests target (add tests in tests/ directory)
# add_subdirectory(tests)

# ---------------------------------------------------------------------------
# CPack for packaging
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Ashgrove Valley - Farming Simulation MUD")
set(CPACK_PACKAGE_VENDOR "Ashgrove Valley Team")
set(CPACK_GENERATOR "TGZ;TBZ2")
include(CPack)

# ---------------------------------------------------------------------------
# Developer Helpers
# ---------------------------------------------------------------------------
# Format target (requires clang-format)
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
  add_custom_target(format
    COMMAND ${CLANG_FORMAT} -i
      src/main.cpp src/server.cpp src/world.cpp
      include/world.hpp include/protocol.hpp include/event_bus.hpp include/plugin.hpp include/llama_wrapper.hpp
    COMMENT "Formatting source files with clang-format"
    VERBATIM
  )
endif()

# Compile commands for clangd / LSP
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)