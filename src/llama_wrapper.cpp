#include "llama_wrapper.hpp"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <llama.h>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include <memory>
#include <stdexcept>
#include <vector>
#include <string>
#include <iomanip>
#include <iostream>

struct LlamaWrapper::Impl {
    llama_model* model = nullptr;

    Impl(const std::string& model_path) {
        if (!model_path.empty()) {
            llama_model_params mparams = llama_model_default_params();
            model = llama_model_load_from_file(model_path.c_str(), mparams);
            if (!model) {
                throw std::runtime_error("Failed to load llama model from " + model_path);
            }
        }
    }

    ~Impl() {
        if (model) llama_model_free(model);
    }
};

std::string LlamaWrapper::infer(const std::string& prompt, int max_tokens, float temp) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!pimpl_->model) return "";

    llama_context_params cparams = llama_context_default_params();
    cparams.offload_kqv = false;
    cparams.n_batch = 2048;  // Increase batch size for long prompts
    cparams.n_ctx = 4096;    // Explicit context window
    cparams.n_ubatch = 512;  // Micro-batch size
    llama_context* ctx = llama_init_from_model(pimpl_->model, cparams);
    if (!ctx) return "";

    auto cleanup = [&ctx]() { llama_free(ctx); };

    const llama_vocab* vocab = llama_model_get_vocab(pimpl_->model);

    // Tokenize prompt
    std::vector<llama_token> tokens(prompt.size() + 4);
    int n_tokens = llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
                                  tokens.data(), static_cast<int32_t>(tokens.size()), true, true);
    if (n_tokens < 0) { cleanup(); return ""; }
    tokens.resize(static_cast<size_t>(n_tokens));

    // Evaluate prompt
    llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));
    if (llama_decode(ctx, batch) != 0) { cleanup(); return ""; }

    // Sample with temperature - use low temperature for deterministic JSON
    llama_sampler* sampler = nullptr;
    if (temp <= 0.0f) {
        sampler = llama_sampler_init_greedy();
    } else {
        // Use temperature sampling with proper chain
        sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
        llama_sampler_chain_add(sampler, llama_sampler_init_temp(temp));
        llama_sampler_chain_add(sampler, llama_sampler_init_dist(1234));
    }
    if (!sampler) { cleanup(); return ""; }

    std::string generated;
    llama_token eos = llama_vocab_eos(vocab);
    for (int i = 0; i < max_tokens; ++i) {
        llama_token id = llama_sampler_sample(sampler, ctx, -1);
        if (id == eos) break;
        char buf[32];
        int n = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, false);
        if (n > 0) generated.append(buf, static_cast<size_t>(n));
        // feed token back
        llama_batch one = llama_batch_get_one(&id, 1);
        if (llama_decode(ctx, one) != 0) break;
        // Stop if we've completed a JSON object (found closing brace)
        if (generated.size() > 2 && generated.back() == '}') {
            // Check if the JSON is complete (balanced braces)
            int brace_count = 0;
            for (char c : generated) {
                if (c == '{') brace_count++;
                else if (c == '}') brace_count--;
            }
            if (brace_count == 0 && generated.back() == '}') break;
        }
    }
    llama_sampler_free(sampler);
    cleanup();
    return generated;
}

LlamaWrapper::LlamaWrapper(const std::string& model_path)
    : pimpl_(std::make_unique<Impl>(model_path)) {}

LlamaWrapper::~LlamaWrapper() = default;

std::optional<Intent> LlamaWrapper::parse_command(const std::string& raw_text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!pimpl_->model) {
        // No model loaded – fallback to simple keyword intent
        Intent fallback;
        fallback.action = "unknown";
        fallback.parameters = nlohmann::json::object({{"raw", raw_text}});
        return fallback;
    }

    // A fresh context is created per parse so no state (KV cache, batch allocator)
    // is ever shared across requests. The persistent llama_context's allocator keeps
    // internal bookkeeping that references KV buffers; freeing those buffers between
    // calls (llama_memory_clear) caused use-after-free crashes under load. Creating a
    // context here and freeing it before returning avoids that entirely. The model is
    // loaded once in the constructor and reused (it is read-only during decode).
    llama_context_params cparams = llama_context_default_params();
    // llama.cpp optimization flags (server equivalents: -fa, -ctk/-ctv, -nkvo)
    // NOTE: this gemma-4 model is SWA (sliding-window attention) based. Enabling
    // flash attention or quantizing the KV cache (q8_0) trips GGML asserts/segfaults
    // on this model, so we keep the defaults and only avoid GPU offload.
    cparams.offload_kqv = false; // -nkvo: keep KV cache off GPU
    llama_context* ctx = llama_init_from_model(pimpl_->model, cparams);
    if (!ctx) return std::nullopt;

    auto cleanup = [&ctx]() { llama_free(ctx); };

    // Very small prompt template
    std::string prompt = "### Instruction:\nParse the player's command into a JSON intent with fields \"action\" and \"parameters\".\n"
                         "### Input:\n" + raw_text + "\n### Response:\n";

    const llama_vocab* vocab = llama_model_get_vocab(pimpl_->model);

    // Tokenize prompt
    std::vector<llama_token> tokens(prompt.size() + 4);
    int n_tokens = llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
                                  tokens.data(), static_cast<int32_t>(tokens.size()), true, true);
    if (n_tokens < 0) { cleanup(); return std::nullopt; }
    tokens.resize(static_cast<size_t>(n_tokens));

    // Evaluate prompt
    llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));
    if (llama_decode(ctx, batch) != 0) { cleanup(); return std::nullopt; }

    // Simple greedy sampling for a few tokens
    llama_sampler* sampler = llama_sampler_init_greedy();
    std::string generated;
    llama_token eos = llama_vocab_eos(vocab);
    for (int i = 0; i < 64; ++i) {
        llama_token id = llama_sampler_sample(sampler, ctx, -1);
        if (id == eos) break;
        char buf[32];
        int n = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, false);
        if (n > 0) generated.append(buf, static_cast<size_t>(n));
        // feed token back
        llama_batch one = llama_batch_get_one(&id, 1);
        if (llama_decode(ctx, one) != 0) break;
    }
    llama_sampler_free(sampler);
    cleanup();

    // Try to parse JSON from generated text
    try {
        // The model may continue generating template text after the JSON
        // object. Extract the first balanced JSON object before parsing.
        std::string json_text = generated;
        size_t start = json_text.find('{');
        if (start != std::string::npos) {
            int brace_count = 0;
            for (size_t i = start; i < json_text.size(); ++i) {
                if (json_text[i] == '{') brace_count++;
                else if (json_text[i] == '}') {
                    brace_count--;
                    if (brace_count == 0) {
                        json_text = json_text.substr(start, i - start + 1);
                        break;
                    }
                }
            }
        }
        auto json = nlohmann::json::parse(json_text);
        Intent intent;
        intent.action = json.value("action", "unknown");
        intent.parameters = json.value("parameters", nlohmann::json::object());
        return intent;
    } catch (...) {
        std::cerr << "[parse_command] JSON parse failed for: '" << raw_text << "'"
                  << " generated=" << std::quoted(generated) << std::endl;
        return std::nullopt;
    }
}