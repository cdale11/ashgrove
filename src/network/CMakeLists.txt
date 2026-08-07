cmake_minimum_required(VERSION 3.25)

add_library(ashgrove_network STATIC
    network.cpp
)

target_include_directories(ashgrove_network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(ashgrove_network PUBLIC
    ashgrove_common
    nlohmann_json::nlohmann_json
    fmt::fmt
    spdlog::spdlog
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

set_target_properties(ashgrove_network PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)