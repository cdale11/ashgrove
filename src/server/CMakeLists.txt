cmake_minimum_required(VERSION 3.25)

add_executable(ashgrove_server
    main.cpp
    game_server.cpp
    player.cpp
)

target_include_directories(ashgrove_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(ashgrove_server PRIVATE
    ashgrove_common
    ashgrove_simulation
    ashgrove_world
    ashgrove_npc
    ashgrove_investigation
    ashgrove_ai
    ashgrove_network
    nlohmann_json::nlohmann_json
    fmt::fmt
    spdlog::spdlog
    Threads::Threads
)

set_target_properties(ashgrove_server PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)