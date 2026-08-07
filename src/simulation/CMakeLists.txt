cmake_minimum_required(VERSION 3.25)

add_library(ashgrove_simulation STATIC
    simulation.cpp
)

target_include_directories(ashgrove_simulation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(ashgrove_simulation PUBLIC
    ashgrove_common
    nlohmann_json::nlohmann_json
    fmt::fmt
    spdlog::spdlog
    Threads::Threads
)

set_target_properties(ashgrove_simulation PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)