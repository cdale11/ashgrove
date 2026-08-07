#include "ai/llm_client.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>

namespace ashgrove {

namespace {

// CURL write callback for append downloaded bytes to std::string.
size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string trim_copy(std::string s) {
    const auto IsSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    const auto notspace = [&](unsigned char c) { return !IsSpace(c); };
    const auto first = std::find_if(s.begin(), s.end(), notspace);
    const auto last = std::find_if(s.rbegin(), s.rend(), notspace).base();
    if (first >= last) return std::string();
    return std::string(first, last);
}

} // namespace

LLMClient::LLMClient(LLMConfig config)
    : config_(std::move(config)) {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_ALL); });
    ready_ = health();
}

LLMClient::~LLMClient() = default;

bool LLMClient::health() const {
    std::unique_lock<std::mutex> lock(init_mutex_);
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string url = config_.url + "/health";
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) return false;

    try {
        nlohmann::json j = nlohmann::json::parse(response);
        return j.value("status", "") == "ok";
    } catch (...) {
        return false;
    }
}

std::optional<std::string> LLMClient::complete(const std::string& system_prompt,
                                               const std::string& user_prompt) const {
    nlohmann::json request;
    request["model"] = config_.model;
    request["temperature"] = config_.temperature;
    request["max_tokens"] = config_.max_tokens;
    request["messages"] = nlohmann::json::array({
        {{"role", "system"}, {"content", system_prompt}},
        {{"role", "user"}, {"content", user_prompt}},
    });

    if (getenv("LLGROVE_LLM_DEBUG")) {
        spdlog::info("LLM REQUEST: {}", request.dump());
    }

    CURL* curl = curl_easy_init();
    if (!curl) return std::nullopt;

    std::string url = config_.url + "/v1/chat/completions";
    std::string body = request.dump();
    struct curl_slist* headers = nullptr;
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    // Match system curl behavior: disable Expect: 100-continue
    curl_easy_setopt(curl, CURLOPT_EXPECT_100_TIMEOUT_MS, 0L);
    // Disable chunked encoding for the request body
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(config_.request_timeout_s * 1000.0));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    CURLcode rc = curl_easy_perform(curl);
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        spdlog::warn("LLM completion request failed: {}", curl_easy_strerror(rc));
        return std::nullopt;
    }

    if (getenv("LLGROVE_LLM_DEBUG")) {
        spdlog::info("LLM RAW: {}", response);
    }

    try {
        nlohmann::json j = nlohmann::json::parse(response);
        const nlohmann::json& choice = j.at("choices").at(0);
        std::string content = choice.value("message", nlohmann::json{}).value("content", "");
        content = trim_copy(content);
        if (getenv("LLGROVE_LLM_DEBUG")) {
            spdlog::info("LLM DEBUG finish={} tokens={} content=[{}]", choice.value("finish_reason", "?"),
                         j.value("usage", nlohmann::json{}).value("completion_tokens", 0), content);
        }
        if (content.empty()) return std::nullopt;
        return content;
    } catch (const std::exception& e) {
        spdlog::warn("LLM completion response parse failed: {}", e.what());
        return std::nullopt;
    }
}

} // namespace ashgrove