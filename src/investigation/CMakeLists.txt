cmake_minimum_required(VERSION 3.25)

add_library(ashgrove_investigation STATIC
    investigation.cpp
)

target_include_directories(ashgrove_investigation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(ashgrove_investigation PUBLIC
    ashgrove_common
    ashgrove_npc
    nlohmann_json::nlohmann_json
    fmt::fmt
    spdlog::spdlog
    Threads::Threads
)

set_target_properties(ashgrove_investigation PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)