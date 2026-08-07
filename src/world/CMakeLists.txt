cmake_minimum_required(VERSION 3.25)

add_library(ashgrove_world STATIC
    world.cpp
)

target_include_directories(ashgrove_world PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(ashgrove_world PUBLIC
    ashgrove_common
    ashgrove_npc
    nlohmann_json::nlohmann_json
    fmt::fmt
    spdlog::spdlog
    Threads::Threads
)

set_target_properties(ashgrove_world PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)