add_library(ashgrove_ai STATIC llm_client.cpp)
target_include_directories(ashgrove_ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_link_libraries(ashgrove_ai PUBLIC CURL::libcurl nlohmann_json::nlohmann_json spdlog::spdlog)