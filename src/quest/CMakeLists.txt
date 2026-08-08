cmake_minimum_required(VERSION 3.25)

add_library(ashgrove_quest STATIC
    quest.cpp
)

target_include_directories(ashgrove_quest PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(ashgrove_quest PUBLIC
    ashgrove_common
    nlohmann_json::nlohmann_json
)

set_target_properties(ashgrove_quest PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
