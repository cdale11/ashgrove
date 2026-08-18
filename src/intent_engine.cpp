#include "intent_engine.hpp"

#include <algorithm>
#include <cctype>

namespace {

std::string lower_trim(std::string s) {
    auto issp = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](char c) { return !issp(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](char c) { return !issp(c); }).base(), s.end());
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> split_words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool one_of(const std::string& s, std::initializer_list<const char*> opts) {
    for (auto* o : opts) if (s == o) return true;
    return false;
}

}  // namespace

IntentEngine::IntentEngine() = default;

void IntentEngine::set_llm_backend(std::function<std::optional<Intent>(const std::string&)> backend) {
    llm_backend_ = std::move(backend);
}

std::optional<Intent> IntentEngine::parse_llm(const std::string& raw) {
    if (!llm_backend_) return std::nullopt;
    return llm_backend_(raw);
}

// Tier 0: map a raw command string to {action, parameters} using the fixed
// command surface. This is intentionally a whitelist of the verbs the game
// already understands in handle_cmd(), so the fast path never diverges from
// game behaviour.
std::optional<Intent> IntentEngine::parse_rule(const std::string& raw) {
    auto words = split_words(lower_trim(raw));
    if (words.empty()) return std::nullopt;
    const std::string& c = words[0];
    std::string arg;
    if (words.size() > 1) {
        arg = words[1];
        for (size_t i = 2; i < words.size(); ++i) arg += " " + words[i];
    }

    Intent intent;
    auto set = [&](std::string action) {
        intent.action = std::move(action);
        intent.parameters = nlohmann::json::object();
    };

    // Movement / world
    if (one_of(c, {"go", "move", "walk", "travel", "g", "w", "n", "s", "e", "north", "south", "east", "west"})) {
        set("move");
        intent.parameters["target"] = arg.empty() ? c : arg;
        return intent;
    }
    if (one_of(c, {"look", "l", "inspect", "examine"})) { set("look"); return intent; }
    if (one_of(c, {"enter", "inside"})) { set("enter"); intent.parameters["building"] = arg; return intent; }
    if (one_of(c, {"exit", "leave", "out"})) { set("exit"); return intent; }
    if (one_of(c, {"interact", "use"})) { set("interact"); intent.parameters["thing"] = arg; return intent; }

    // Inventory / self
    if (one_of(c, {"inventory", "inv", "bag", "items"})) { set("inventory"); return intent; }
    if (one_of(c, {"status", "stats", "energy", "money"})) { set("status"); return intent; }
    if (one_of(c, {"help", "?"})) { set("help"); intent.parameters["topic"] = arg; return intent; }
    if (one_of(c, {"eat", "consume", "drink"})) { set("eat"); intent.parameters["item"] = arg; return intent; }

    if (one_of(c, {"hoe", "till", "plow"})) { set("hoe"); return intent; }
    if (one_of(c, {"soil", "soiltest", "testsoil"})) { set("soil"); return intent; }
    if (one_of(c, {"fertilize", "fertilizer"})) { set("fertilize"); intent.parameters["fertilizer"] = arg; return intent; }
    if (one_of(c, {"plant", "planttree", "sow"})) {
        set(c == "planttree" ? "planttree" : "plant");
        intent.parameters["crop"] = arg;
        return intent;
    }
    if (one_of(c, {"water"})) { set("water"); intent.parameters["tile"] = arg; return intent; }
    if (one_of(c, {"well"})) { set("well"); return intent; }
    if (one_of(c, {"harvest", "pick", "reap"})) { set("harvest"); intent.parameters["crop"] = arg; return intent; }
    if (one_of(c, {"axe", "chop", "cut"})) { set("axe"); intent.parameters["tree"] = arg; return intent; }
    if (one_of(c, {"scythe", "clear", "cutgrass"})) { set("scythe"); return intent; }
    if (one_of(c, {"fish", "cast", "reel"})) { set("fish"); return intent; }
    if (one_of(c, {"tap"})) { set("tap"); intent.parameters["tree"] = arg; return intent; }
    if (one_of(c, {"shake"})) { set("shake"); intent.parameters["tree"] = arg; return intent; }

    // Social
    if (one_of(c, {"talk", "speak", "chat", "converse"})) { set("talk"); intent.parameters["npc"] = arg; return intent; }
    if (one_of(c, {"gift", "give"})) { set("gift"); intent.parameters["item"] = arg; return intent; }
    if (one_of(c, {"hearts", "friends", "friendship"})) { set("hearts"); return intent; }

    // Economy
    if (one_of(c, {"buy", "purchase"})) { set("buy"); intent.parameters["item"] = arg; return intent; }
    if (one_of(c, {"sell"})) { set("sell"); intent.parameters["item"] = arg; return intent; }
    if (one_of(c, {"craft", "cook", "make", "bake"})) { set("craft"); intent.parameters["recipe"] = arg; return intent; }
    if (one_of(c, {"place", "build", "construct"})) { set("place"); intent.parameters["thing"] = arg; return intent; }
    if (one_of(c, {"repair", "fix"})) { set("repair"); intent.parameters["building"] = arg; return intent; }
    if (one_of(c, {"upgrade"})) { set("upgrade"); intent.parameters["building"] = arg; return intent; }

    // Land / world features
    if (one_of(c, {"plots", "deeds", "land"})) { set("plots"); return intent; }
    if (one_of(c, {"train"})) { set("train"); return intent; }
    if (one_of(c, {"bus"})) { set("bus"); return intent; }
    if (one_of(c, {"tv", "watch"})) { set("tv"); return intent; }
    if (one_of(c, {"festival", "fest"})) { set("festival"); return intent; }
    if (one_of(c, {"basement", "cellar"})) { set("basement"); return intent; }
    if (one_of(c, {"horror", "sanity"})) { set("horror"); return intent; }

    // Persistence / lifecycle
    if (one_of(c, {"sleep", "rest", "bed"})) { set("sleep"); return intent; }
    if (one_of(c, {"save"})) { set("save"); intent.parameters["name"] = arg; return intent; }
    if (one_of(c, {"load"})) { set("load"); intent.parameters["name"] = arg; return intent; }
    if (one_of(c, {"newgame", "reset"})) { set("newgame"); return intent; }

    return std::nullopt;
}

std::optional<Intent> IntentEngine::parse(const std::string& raw, std::string* source) {
    if (auto rule = parse_rule(raw)) {
        if (source) *source = "rule";
        return rule;
    }
    if (auto llm = parse_llm(raw)) {
        if (source) *source = "llm";
        return llm;
    }
    if (source) *source = "none";
    return std::nullopt;
}
