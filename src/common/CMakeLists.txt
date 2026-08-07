cmake_minimum_required(VERSION 3.25)

add_library(ashgrove_common STATIC
    ../../src/common/types.cpp
)

target_include_directories(ashgrove_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${nlohmann_json_INCLUDE_DIRS}
    ${fmt_INCLUDE_DIRS}
    ${spdlog_INCLUDE_DIRS}
)

target_link_libraries(ashgrove_common PUBLIC
    fmt::fmt
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    Threads::Threads
)

set_target_properties(ashgrove_common PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)