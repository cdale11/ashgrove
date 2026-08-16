// Smoke test for CognitiveCore + CognitiveRegistry.
// Verifies: construction, tick decay, event recording, social updates,
// drive updates, action selection, subconscious replay, save/load.
//
// Run: g++ -std=c++20 -Iinclude tools/test_cognitive_core.cpp src/cognitive_core.cpp src/cognitive_registry.cpp -o /tmp/test_cog && /tmp/test_cog

#include "cognitive_core.hpp"
#include "cognitive_registry.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace ashgrove;

#define CHECK(cond, msg) do { \
  if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); std::exit(1); } \
  else { std::fprintf(stderr, "PASS: %s\n", msg); } \
} while(0)

int main() {
  // 1. Construction + initial state.
  CognitiveCore npc("npc:demo");
  CHECK(npc.agent_id() == "npc:demo", "agent_id set");
  CHECK(npc.state().drives.drive_satisfaction[DriveState::kHunger] == 0.5f,
        "hunger starts at 0.5");

  // 2. Tick decay: hunger/thirst should fall, others stable-ish.
  float hunger_before = npc.state().drives.drive_satisfaction[DriveState::kHunger];
  for (uint32_t t = 0; t < 100; ++t) {
    npc.tick(t, {"npc:other"});
  }
  float hunger_after = npc.state().drives.drive_satisfaction[DriveState::kHunger];
  CHECK(hunger_after < hunger_before, "hunger decays after 100 ticks");
  CHECK(hunger_after >= CognitiveState::kDriveSatisfactionFloor,
        "hunger doesn't fall below floor");

  // 3. Record a horror event: fear should rise.
  float fear_before = npc.state().current_emotion.fear;
  npc.record_event("horror_event_sighting", "{}", 100, 1, "Spring");
  CHECK(npc.state().current_emotion.fear > fear_before,
        "fear rises after horror event");
  CHECK(npc.state().episodic_memory.size() == 1,
        "event recorded in episodic memory");

  // 4. Record a gift event: joy should rise, trust should rise.
  float joy_before = npc.state().current_emotion.joy;
  float trust_before = npc.state().current_emotion.trust;
  npc.record_event("gift_received", "{\"from\":\"merchant\"}", 101, 1, "Spring");
  CHECK(npc.state().current_emotion.joy > joy_before,
        "joy rises after gift");
  CHECK(npc.state().current_emotion.trust > trust_before,
        "trust rises after gift");
  CHECK(npc.state().episodic_memory.size() == 2,
        "second event recorded");

  // 5. Update drive: eating should raise hunger satisfaction.
  float hunger_pre_eat = npc.state().drives.drive_satisfaction[DriveState::kHunger];
  npc.update_drive(DriveState::kHunger, 5.0f);  // big positive delta
  CHECK(npc.state().drives.drive_satisfaction[DriveState::kHunger] > hunger_pre_eat,
        "eating raises hunger satisfaction");

  // 6. Social update: trust with merchant grows, familiarity grows.
  float trust_before_edge = 0.0f;
  auto it = npc.state().social_graph.find("npc:merchant");
  if (it != npc.state().social_graph.end()) {
    trust_before_edge = it->second.trust;
  }
  npc.update_social("npc:merchant", 0.8f, true);
  auto it2 = npc.state().social_graph.find("npc:merchant");
  CHECK(it2 != npc.state().social_graph.end(), "social edge created");
  CHECK(it2->second.trust > trust_before_edge, "trust rises after positive interaction");
  CHECK(it2->second.imitation_target > 0.0f, "imitation target set after positive observation");

  // 7. Action selection: returns valid index in [0,5].
  for (uint32_t t = 200; t < 220; ++t) {
    npc.tick(t, {"npc:merchant", "loc:tavern"});
  }
  std::size_t act = npc.select_action();
  CHECK(act < 6, "action index in [0,5]");

  // 8. Subconscious replay: confidence of top events should rise.
  float conf_before = npc.state().episodic_memory[0].confidence;
  npc.subconscious_replay(220);
  float conf_after = npc.state().episodic_memory[0].confidence;
  CHECK(conf_after >= conf_before, "subconscious replay reinforces episodic confidence");

  // 9. Save / load roundtrip.
  std::string save_dir = "/tmp/opencode/cog_test";
  std::filesystem::create_directories(save_dir);
  CHECK(npc.save(save_dir), "save succeeds");
  CognitiveCore npc2("npc:demo");
  CHECK(npc2.load(save_dir), "load succeeds");
  CHECK(std::abs(npc2.state().mean_valence - npc.state().mean_valence) < 0.01f,
        "mean_valence roundtrips (within precision)");

  // 10. Registry: get_or_create returns same instance.
  CognitiveCore& c1 = CognitiveRegistry::instance().get_or_create("npc:robin");
  CognitiveCore& c2 = CognitiveRegistry::instance().get_or_create("npc:robin");
  CHECK(&c1 == &c2, "registry returns same instance for same id");

  // Also register the original npc for aggregate test.
  CognitiveCore& c3 = CognitiveRegistry::instance().get_or_create("npc:demo");

  // 11. Registry: tick_all iterates without crashing.
  CognitiveRegistry::instance().tick_all(300, {"npc:demo", "storm_warning"});
  float mv, ma;
  std::size_t n;
  CognitiveRegistry::instance().aggregate_stats(mv, ma, n);
  CHECK(n >= 2, "aggregate sees >=2 agents");

  // 12. Memory caps: working memory never exceeds 7.
  for (uint32_t t = 400; t < 500; ++t) {
    npc.tick(t, {"stim1", "stim2", "stim3", "stim4", "stim5",
                 "stim6", "stim7", "stim8", "stim9", "stim10"});
  }
  CHECK(npc.state().working_memory.size() <= CognitiveState::kWorkingMemoryCap,
        "working memory capped at 7");

  std::fprintf(stderr, "\nALL TESTS PASSED\n");
  return 0;
}
