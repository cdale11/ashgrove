#include "cognitive_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>

namespace ashgrove {

namespace {

constexpr float kClamp01(float x) {
  return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

constexpr float kClamp11(float x) {
  return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
}

}  // namespace

// JSON escaping helper (in ashgrove namespace for save/load access)
std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

CognitiveCore::CognitiveCore(std::string agent_id)
    : agent_id_(std::move(agent_id)),
      social_cognition_(agent_id_) {
  state_.agent_id = agent_id_;
}

void CognitiveCore::clamp_all() {
  for (auto& s : state_.drives.drive_satisfaction) s = kClamp01(s);
  for (auto& w : state_.drives.drive_weights) w = kClamp01(w);
  for (auto& w : state_.attention_weights) w = kClamp01(w);
  for (auto& b : state_.world_model_bias) b = kClamp11(b);
  for (auto& a : state_.last_action_scores) a = kClamp01(a);
  state_.self_model.self_esteem_estimate =
      kClamp01(state_.self_model.self_esteem_estimate);
  state_.self_model.competence_estimate =
      kClamp01(state_.self_model.competence_estimate);
  state_.self_model.autonomy_estimate =
      kClamp01(state_.self_model.autonomy_estimate);
  state_.mean_valence = kClamp11(state_.mean_valence);
  state_.mean_arousal = kClamp01(state_.mean_arousal);
  state_.current_emotion.joy = kClamp01(state_.current_emotion.joy);
  state_.current_emotion.fear = kClamp01(state_.current_emotion.fear);
  state_.current_emotion.trust = kClamp01(state_.current_emotion.trust);
  state_.current_emotion.anger = kClamp01(state_.current_emotion.anger);
  state_.current_emotion.surprise = kClamp01(state_.current_emotion.surprise);
  state_.current_emotion.anticipation = kClamp01(state_.current_emotion.anticipation);
  state_.current_emotion.disgust = kClamp01(state_.current_emotion.disgust);
}

void CognitiveCore::apply_tick_decay() {
  // Drives: hunger/thirst rise (satisfaction falls); others decay gently.
  for (auto& s : state_.drives.drive_satisfaction) {
    s -= CognitiveState::kDriveDecayRate;
  }
  // Hunger/thirst decay faster than others.
  state_.drives.drive_satisfaction[DriveState::kHunger] -=
      CognitiveState::kDriveDecayRate * 4.0f;
  state_.drives.drive_satisfaction[DriveState::kThirst] -=
      CognitiveState::kDriveDecayRate * 5.0f;

  // Working memory decay.
  for (auto& item : state_.working_memory) {
    item.decay_factor *= CognitiveState::kWorkingMemoryDecay;
  }

  // Episodic confidence decay.
  for (auto& ev : state_.episodic_memory) {
    ev.confidence *= CognitiveState::kEpisodicConfidenceDecay;
  }

  // Semantic confidence decay (very slow).
  for (auto& kv : state_.semantic_memory) {
    kv.second.confidence *= CognitiveState::kSemanticConfidenceDecay;
  }

  // Emotional baseline drift toward neutral (mean reversion).
  EmotionalTag& e = state_.current_emotion;
  e.joy *= 0.999f;
  e.fear *= 0.999f;
  e.anger *= 0.997f;
  e.trust = kClamp01(e.trust + 0.0001f);  // trust slowly accumulates
  e.surprise *= 0.99f;
  e.anticipation *= 0.999f;
  e.disgust *= 0.999f;
}

void CognitiveCore::maintain_caps() {
  // Working memory: drop lowest-decay items if over cap.
  while (state_.working_memory.size() > CognitiveState::kWorkingMemoryCap) {
    auto min_it = std::min_element(
        state_.working_memory.begin(), state_.working_memory.end(),
        [](const WorkingMemoryItem& a, const WorkingMemoryItem& b) {
          return a.decay_factor < b.decay_factor;
        });
    if (min_it != state_.working_memory.end()) {
      state_.working_memory.erase(min_it);
    } else {
      break;
    }
  }
  // Drop fully decayed working memory items.
  state_.working_memory.erase(
      std::remove_if(state_.working_memory.begin(),
                     state_.working_memory.end(),
                     [](const WorkingMemoryItem& i) {
                       return i.decay_factor < 0.05f;
                     }),
      state_.working_memory.end());

  // Episodic memory cap.
  while (state_.episodic_memory.size() > CognitiveState::kEpisodicMemoryCap) {
    state_.episodic_memory.pop_front();
  }

  // Semantic memory cap (drop lowest confidence).
  if (state_.semantic_memory.size() > CognitiveState::kSemanticMemoryCap) {
    auto min_it = std::min_element(
        state_.semantic_memory.begin(), state_.semantic_memory.end(),
        [](const auto& a, const auto& b) {
          return a.second.confidence < b.second.confidence;
        });
    if (min_it != state_.semantic_memory.end()) {
      state_.semantic_memory.erase(min_it);
    }
  }

  // Social graph cap (drop lowest familiarity).
  if (state_.social_graph.size() > CognitiveState::kSocialGraphCap) {
    auto min_it = std::min_element(
        state_.social_graph.begin(), state_.social_graph.end(),
        [](const auto& a, const auto& b) {
          return a.second.familiarity < b.second.familiarity;
        });
    if (min_it != state_.social_graph.end()) {
      state_.social_graph.erase(min_it);
    }
  }

  // Goal stack cap.
  while (state_.goal_stack.size() > CognitiveState::kGoalStackCap) {
    state_.goal_stack.pop_back();
  }
}

EmotionalTag CognitiveCore::tag_for_event(const std::string& event_type) const {
  EmotionalTag t;
  if (event_type.find("horror") != std::string::npos ||
      event_type.find("storm_damage") != std::string::npos) {
    t.fear = 0.6f;
    t.surprise = 0.3f;
  } else if (event_type.find("gift") != std::string::npos ||
             event_type.find("friend") != std::string::npos) {
    t.joy = 0.6f;
    t.trust = 0.3f;
  } else if (event_type.find("attack") != std::string::npos ||
             event_type.find("insult") != std::string::npos) {
    t.anger = 0.5f;
    t.disgust = 0.3f;
  } else if (event_type.find("discover") != std::string::npos) {
    t.surprise = 0.4f;
    t.anticipation = 0.4f;
  } else if (event_type.find("repair") != std::string::npos) {
    t.joy = 0.3f;
    t.trust = 0.2f;
  } else if (event_type.find("rest") != std::string::npos ||
             event_type.find("sleep") != std::string::npos) {
    t.trust = 0.2f;
  }
  return t;
}

float CognitiveCore::compute_attention_score(const std::string& stimulus) const {
  // Simple heuristic; the real attention_mlp is loaded when available.
  // Score = (novelty * attention_weights[0]) + (reward potential * 1) +
  //         (social relevance * 0.8) + (survival * 1).
  float novelty = 0.5f;
  float reward = 0.3f;
  float social = 0.0f;
  float survival = 0.2f;
  if (stimulus.find("npc:") == 0) social = 0.6f;
  if (stimulus.find("storm") != std::string::npos) survival = 0.8f;
  if (stimulus.find("gift") != std::string::npos) reward = 0.6f;
  return (novelty * state_.attention_weights[0]) +
         (reward * state_.attention_weights[1]) +
         (social * state_.attention_weights[2]) +
         (survival * state_.attention_weights[3]);
}

void CognitiveCore::tick(uint32_t current_tick,
                        const std::vector<std::string>& observed_stimuli) {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  state_.last_tick = current_tick;

  // 1. Apply tick decay (drives rise, memory decays, emotion reverts).
  apply_tick_decay();

  // 1b. Social cognition tick (familiarity growth, trust drift, etc.)
  social_cognition_.tick(current_tick);

  // 2. Attention: score stimuli, insert top into working memory.
  for (const auto& stim : observed_stimuli) {
    float score = compute_attention_score(stim);
    if (score < 0.2f) continue;  // gated out
    WorkingMemoryItem item;
    item.tick = current_tick;
    item.stimulus_ref = stim;
    item.decay_factor = 1.0f;
    item.relevance = score;
    state_.working_memory.push_back(item);
  }

  // 3. Subconscious replay (every 256 ticks if no sleep has happened).
  if (current_tick % 256 == 0 && current_tick > 0) {
    subconscious_replay(current_tick);
  }

  // 4. Action evaluation: pick action based on drives + working memory.
  // Drives -> weighted urgency vector -> action score.
  float go_score = 0.0f;
  float interact_score = 0.0f;
  float talk_score = 0.0f;
  float repair_score = 0.0f;
  float harvest_score = 0.0f;
  float rest_score = 0.0f;

  float drive_urgency = 0.0f;
  for (std::size_t i = 0; i < state_.drives.drive_satisfaction.size(); ++i) {
    float urgency = (1.0f - state_.drives.drive_satisfaction[i]) *
                    state_.drives.drive_weights[i];
    drive_urgency += urgency;
    switch (i) {
      case DriveState::kHunger:
        harvest_score += urgency * 0.7f;
        go_score += urgency * 0.3f;
        break;
      case DriveState::kThirst:
        go_score += urgency * 0.8f;
        break;
      case DriveState::kSocial:
        talk_score += urgency;
        break;
      case DriveState::kSafety:
        go_score += urgency * 0.5f;  // flee toward safety
        rest_score += urgency * 0.3f;
        break;
      case DriveState::kCuriosity:
        interact_score += urgency * 0.8f;
        go_score += urgency * 0.4f;
        break;
      case DriveState::kRest:
        rest_score += urgency;
        break;
    }
  }

  // Apply world-model bias to action scores.
  go_score += state_.world_model_bias[0] * 0.1f;
  interact_score += state_.world_model_bias[1] * 0.1f;
  harvest_score += state_.world_model_bias[2] * 0.1f;

  // Apply attention weights to bias toward salient stimuli.
  float social_pressure = 0.0f;
  for (const auto& item : state_.working_memory) {
    if (item.stimulus_ref.find("npc:") == 0) social_pressure += 0.2f;
  }
  talk_score += social_pressure * state_.attention_weights[2];

  // Store normalized scores.
  float max_score = std::max({go_score, interact_score, talk_score,
                              repair_score, harvest_score, rest_score, 0.01f});
  state_.last_action_scores = {
      kClamp01(go_score / max_score),
      kClamp01(interact_score / max_score),
      kClamp01(talk_score / max_score),
      kClamp01(repair_score / max_score),
      kClamp01(harvest_score / max_score),
      kClamp01(rest_score / max_score),
  };

  // 5. Self-model update: estimate competence from action success.
  // (Real success feedback comes from outside; this is the passive drift.)
  state_.self_model.competence_estimate =
      kClamp01(state_.self_model.competence_estimate * 0.9995f + 0.0005f);

  // 6. Mean valence/arousal for LLM summarization (running average).
  float v = state_.current_emotion.joy - state_.current_emotion.fear -
            state_.current_emotion.anger + state_.current_emotion.trust * 0.5f;
  float a = (state_.current_emotion.fear + state_.current_emotion.anger +
             state_.current_emotion.surprise) * 0.5f;
  state_.mean_valence = state_.mean_valence * 0.99f + v * 0.01f;
  state_.mean_arousal = state_.mean_arousal * 0.99f + a * 0.01f;

  // 7. Cap maintenance + clamping.
  maintain_caps();
  clamp_all();
}

void CognitiveCore::update_drive(std::size_t drive_idx, float delta) {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  if (drive_idx >= state_.drives.drive_satisfaction.size()) return;
  state_.drives.drive_satisfaction[drive_idx] +=
      delta * CognitiveState::kDriveUpdateRate;
  state_.drives.drive_satisfaction[drive_idx] = std::max(
      CognitiveState::kDriveSatisfactionFloor,
      std::min(1.0f, state_.drives.drive_satisfaction[drive_idx]));
}

void CognitiveCore::apply_drive_decay() {
  // Called more frequently than per-tick (e.g. per in-game minute).
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  apply_tick_decay();
}

void CognitiveCore::record_event(const std::string& event_type,
                                 const std::string& payload_json,
                                 uint32_t current_tick, uint32_t current_day,
                                 const std::string& season) {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  EpisodicEvent ev;
  ev.tick = current_tick;
  ev.day = current_day;
  ev.season = season;
  ev.event_type = event_type;
  ev.payload_json = payload_json;
  ev.tag = tag_for_event(event_type);
  ev.confidence = 1.0f;
  state_.episodic_memory.push_back(ev);

  // Update current emotion from tag (additive).
  state_.current_emotion.joy += ev.tag.joy * 0.3f;
  state_.current_emotion.fear += ev.tag.fear * 0.4f;
  state_.current_emotion.trust += ev.tag.trust * 0.2f;
  state_.current_emotion.anger += ev.tag.anger * 0.3f;
  state_.current_emotion.surprise += ev.tag.surprise * 0.2f;
  state_.current_emotion.anticipation += ev.tag.anticipation * 0.2f;
  state_.current_emotion.disgust += ev.tag.disgust * 0.3f;

  maintain_caps();
  clamp_all();
}

void CognitiveCore::update_social(const std::string& other_agent_id,
                                  float interaction_valence,
                                  bool observed_positive_action) {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  social_cognition_.update_social(other_agent_id, interaction_valence, observed_positive_action);
  // Sync back to state_ for persistence/serialization
  const SocialEdge& edge = social_cognition_.get_edge(other_agent_id);
  state_.social_graph[other_agent_id] = edge;
  clamp_all();
}

void CognitiveCore::subconscious_replay([[maybe_unused]] uint32_t current_tick) {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  // Pick top-5 events by emotional intensity (sum of tag values).
  std::vector<EpisodicEvent*> ranked;
  for (auto& ev : state_.episodic_memory) {
    ranked.push_back(&ev);
  }
  std::sort(ranked.begin(), ranked.end(),
            [](EpisodicEvent* a, EpisodicEvent* b) {
              float ai = a->tag.joy + a->tag.fear + a->tag.anger +
                         a->tag.surprise + a->tag.trust;
              float bi = b->tag.joy + b->tag.fear + b->tag.anger +
                         b->tag.surprise + b->tag.trust;
              return ai > bi;
            });
  std::size_t n = std::min<std::size_t>(5, ranked.size());
  for (std::size_t i = 0; i < n; ++i) {
    EpisodicEvent* ev = ranked[i];
    ev->confidence = std::min(1.0f, ev->confidence + 0.05f);
    // Slight self-model boost if event was positive.
    if (ev->tag.joy > ev->tag.fear && ev->tag.joy > 0.3f) {
      state_.self_model.self_esteem_estimate =
          kClamp01(state_.self_model.self_esteem_estimate + 0.002f);
    }
  }
}

std::size_t CognitiveCore::select_action() const {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  std::size_t best = 0;
  float best_score = state_.last_action_scores[0];
  for (std::size_t i = 1; i < state_.last_action_scores.size(); ++i) {
    if (state_.last_action_scores[i] > best_score) {
      best_score = state_.last_action_scores[i];
      best = i;
    }
  }
  return best;
}

std::string CognitiveCore::to_json_locked() const {
  std::ostringstream o;
  o << "{\"agent_id\":\"" << json_escape(state_.agent_id) << "\",";
  o << "\"created_tick\":" << state_.created_tick << ",";
  o << "\"last_tick\":" << state_.last_tick << ",";
  o << "\"mean_valence\":" << state_.mean_valence << ",";
  o << "\"mean_arousal\":" << state_.mean_arousal << ",";
  o << "\"drives\":{";
  o << "\"satisfaction\":[";
  for (std::size_t i = 0; i < state_.drives.drive_satisfaction.size(); ++i) {
    if (i) o << ",";
    o << state_.drives.drive_satisfaction[i];
  }
  o << "],\"weights\":[";
  for (std::size_t i = 0; i < state_.drives.drive_weights.size(); ++i) {
    if (i) o << ",";
    o << state_.drives.drive_weights[i];
  }
  o << "]},";
  o << "\"attention_weights\":[";
  for (std::size_t i = 0; i < state_.attention_weights.size(); ++i) {
    if (i) o << ",";
    o << state_.attention_weights[i];
  }
  o << "],";
  o << "\"world_model_bias\":[";
  for (std::size_t i = 0; i < state_.world_model_bias.size(); ++i) {
    if (i) o << ",";
    o << state_.world_model_bias[i];
  }
  o << "],";
  o << "\"self_model\":{";
  o << "\"self_esteem\":" << state_.self_model.self_esteem_estimate << ",";
  o << "\"competence\":" << state_.self_model.competence_estimate << ",";
  o << "\"autonomy\":" << state_.self_model.autonomy_estimate;
  o << "},";
  o << "\"last_action_scores\":[";
  for (std::size_t i = 0; i < state_.last_action_scores.size(); ++i) {
    if (i) o << ",";
    o << state_.last_action_scores[i];
  }
  o << "],";
  o << "\"episodic_count\":" << state_.episodic_memory.size() << ",";
  o << "\"semantic_count\":" << state_.semantic_memory.size() << ",";
  o << "\"social_count\":" << state_.social_graph.size() << ",";
  o << "\"working_count\":" << state_.working_memory.size();
  o << "}";
  return o.str();
}

bool CognitiveCore::from_json_locked(const std::string& json_str) {
  auto find_float = [&](const std::string& key, float& dst) -> bool {
    auto k = json_str.find("\"" + key + "\":");
    if (k == std::string::npos) return false;
    auto v = json_str.find_first_of("-0123456789", k + key.size() + 3);
    if (v == std::string::npos) return false;
    try { dst = std::stof(json_str.substr(v, 32)); return true; }
    catch (...) { return false; }
  };
  find_float("mean_valence", state_.mean_valence);
  find_float("mean_arousal", state_.mean_arousal);
  find_float("self_esteem", state_.self_model.self_esteem_estimate);
  find_float("competence", state_.self_model.competence_estimate);
  find_float("autonomy", state_.self_model.autonomy_estimate);
  clamp_all();
  return true;
}

bool CognitiveCore::save(const std::string& dir) const {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  std::string path = dir + "/" + ashgrove::json_escape(agent_id_) + ".json";
  std::ofstream f(path);
  if (!f) return false;
  f << to_json_locked();
  return f.good();
}

bool CognitiveCore::load(const std::string& dir) {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  std::string path = dir + "/" + ashgrove::json_escape(agent_id_) + ".json";
  std::ifstream f(path);
  if (!f) return false;
  std::stringstream buf;
  buf << f.rdbuf();
  return from_json_locked(buf.str());
}

}  // namespace ashgrove
