#include "llama_wrapper.hpp"
#include <llama.h>
#include <memory>
#include <stdexcept>
#include <vector>
#include <string>

struct LlamaWrapper::Impl {
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;

    Impl(const std::string& model_path) {
        if (!model_path.empty()) {
            llama_model_params mparams = llama_model_default_params();
            model = llama_load_model_from_file(model_path.c_str(), mparams);
            if (!model) {
                throw std::runtime_error("Failed to load llama model from " + model_path);
            }
            llama_context_params cparams = llama_context_default_params();
            ctx = llama_new_context_with_model(model, cparams);
            if (!ctx) {
                llama_free_model(model);
                throw std::runtime_error("Failed to create llama context");
            }
        }
    }

    ~Impl() {
        if (ctx) llama_free(ctx);
        if (model) llama_free_model(model);
    }
};

LlamaWrapper::LlamaWrapper(const std::string& model_path)
    : pimpl_(std::make_unique<Impl>(model_path)) {}

LlamaWrapper::~LlamaWrapper() = default;

std::optional<Intent> LlamaWrapper::parse_command(const std::string& raw_text) const {
    if (!pimpl_->ctx) {
        // No model loaded – fallback to simple keyword intent
        Intent fallback;
        fallback.action = "unknown";
        fallback.parameters = nlohmann::json::object({{"raw", raw_text}});
        return fallback;
    }

    // Very small prompt template
    std::string prompt = "### Instruction:\nParse the player's command into a JSON intent with fields \"action\" and \"parameters\".\n"
                         "### Input:\n" + raw_text + "\n### Response:\n";

    // Tokenize prompt
    std::vector<llama_token> tokens(prompt.size() + 4);
    int n_tokens = llama_tokenize(pimpl_->ctx, prompt.c_str(), prompt.size(), tokens.data(), tokens.size(), true, true);
    if (n_tokens < 0) return std::nullopt;
    tokens.resize(n_tokens);

    // Evaluate prompt
    if (llama_eval(pimpl_->ctx, tokens.data(), n_tokens, 0, 4) != 0) return std::nullopt;

    // Simple greedy sampling for a few tokens
    std::string generated;
    for (int i = 0; i < 64; ++i) {
        llama_token id = llama_sample_token_greedy(pimpl_->ctx, nullptr);
        if (id == llama_token_eos(pimpl_->model)) break;
        char buf[32];
        int n = llama_token_to_piece(pimpl_->ctx, id, buf, sizeof(buf), 0);
        if (n > 0) generated.append(buf, n);
        // feed token back
        llama_eval(pimpl_->ctx, &id, 1, tokens.size() + i, 4);
    }

    // Try to parse JSON from generated text
    try {
        auto json = nlohmann::json::parse(generated);
        Intent intent;
        intent.action = json.value("action", "unknown");
        intent.parameters = json.value("parameters", nlohmann::json::object());
        return intent;
    } catch (...) {
        return std::nullopt;
    }
}