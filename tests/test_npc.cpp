#include "test_utils.h"
#include "npc/npc.h"

using namespace ashgrove;

TEST(npc_memory_add_and_recall) {
    NPC npc(1, "Test", NPCTier::Tier1_Major);
    Memory mem;
    mem.event_id = 100;
    mem.description = "The mayor looked nervous when I asked about the miller";
    mem.importance = 0.9f;
    mem.confidence = 1.0f;
    npc.add_memory(mem);
    
    auto results = npc.recall("miller");
    CHECK_EQ(results.size(), 1);
    CHECK_EQ(results[0].description, "The mayor looked nervous when I asked about the miller");
}

TEST(npc_memory_forget) {
    NPC npc(1, "Test", NPCTier::Tier1_Major);
    Memory m;
    m.event_id = 10;
    m.description = "secret";
    npc.add_memory(m);
    npc.forget(10);
    CHECK_EQ(npc.memories.size(), 0);
}

TEST(npc_belief_system) {
    NPC npc(1, "Test", NPCTier::Tier1_Major);
    Belief b{"ghosts_exist", 0.3f, 0, 0, "", false};
    npc.add_belief(b);
    float conf = npc.get_belief_confidence("ghosts_exist");
    CHECK_NEAR(conf, 0.3f, 0.001f);
    
    npc.update_belief("ghosts_exist", 0.2f);
    CHECK_NEAR(npc.get_belief_confidence("ghosts_exist"), 0.5f, 0.001f);
}

TEST(npc_relationships) {
    NPC npc(1, "Test", NPCTier::Tier1_Major);
    npc.modify_relationship(99, 0.5f, 0.3f);
    auto* rel = npc.get_relationship(99);
    CHECK(rel != nullptr);
    CHECK_NEAR(rel->affinity, 0.5f, 0.001f);
    CHECK_NEAR(rel->trust, 0.3f, 0.001f);
    
    npc.modify_relationship(99, -0.2f, -0.3f);
    CHECK_NEAR(rel->affinity, 0.3f, 0.001f);
    CHECK_NEAR(rel->trust, 0.0f, 0.001f);
}

TEST(npc_emotion_set) {
    NPC npc(1, "Test", NPCTier::Tier1_Major);
    npc.set_emotion(EmotionType::Angry, 0.8f);
    CHECK(npc.current_emotion == EmotionType::Angry);
    CHECK_NEAR(npc.emotion_intensity, 0.8f, 0.001f);
    
    npc.decay_emotion(0.3f);
    CHECK_NEAR(npc.emotion_intensity, 0.5f, 0.001f);
    
    npc.decay_emotion(1.0f);
    CHECK(npc.current_emotion == EmotionType::Neutral);
}

TEST(npc_serialization_roundtrip) {
    NPC npc(1, "Test", NPCTier::Tier1_Major);
    npc.surname = "Smith";
    npc.age = 42;
    npc.add_belief({"sky_is_falling", 0.9f, 5, 0, "", false});
    
    auto j = npc.serialize();
    NPC loaded;
    loaded.deserialize(j);
    
    CHECK_EQ(loaded.name, "Test");
    CHECK_EQ(loaded.surname, "Smith");
    CHECK_EQ(loaded.age, 42);
    CHECK_NEAR(loaded.get_belief_confidence("sky_is_falling"), 0.9f, 0.001f);
}

TEST(npc_schedule_lookup) {
    NPC npc(1, "Test", NPCTier::Tier2_Persistent);
    DailyScheduleEntry work;
    work.start_hour = 8;
    work.duration_hours = 8;
    work.activity = "work";
    npc.schedule.push_back(work);
    
    CHECK_EQ(npc.get_scheduled_activity(10), "work");
    CHECK_EQ(npc.get_scheduled_activity(3), "idle");
    CHECK_EQ(npc.get_scheduled_activity(16), "idle");
}