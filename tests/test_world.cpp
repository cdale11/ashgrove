#include "test_utils.h"
#include "world/world.h"
#include <memory>

using namespace ashgrove;

TEST(world_create_region) {
    World world;
    Region village;
    village.name = "Ashgrove";
    village.type = RegionType::Village;
    EntityID id = world.create_region(village);
    CHECK(id != INVALID_ENTITY_ID);
    CHECK(world.get_region(id) != nullptr);
    CHECK_EQ(world.get_region(id)->name, "Ashgrove");
}

TEST(world_create_building) {
    World world;
    Building tavern;
    tavern.name = "Test Tavern";
    EntityID id = world.create_building(tavern);
    CHECK(id != INVALID_ENTITY_ID);
    CHECK(world.get_building(id) != nullptr);
    CHECK_EQ(world.get_building(id)->name, "Test Tavern");
}

TEST(world_ids_unique) {
    World world;
    Region r;
    Building b;
    EntityID r1 = world.create_region(r);
    EntityID b1 = world.create_building(b);
    EntityID r2 = world.create_region(r);
    CHECK(r1 != r2);
    CHECK(r1 != b1);
}

TEST(world_serialize_roundtrip) {
    World world;
    Region village;
    village.name = "Ashgrove";
    village.type = RegionType::Village;
    EntityID village_id = world.create_region(village);
    
    Building tavern;
    tavern.name = "The Sleeping Fox";
    tavern.position = {0, 0, 0, village_id};
    world.create_building(tavern);
    
    auto j = world.serialize();
    
    World restored;
    restored.deserialize(j);
    
    CHECK(restored.get_region(village_id) != nullptr);
    CHECK(restored.get_region(village_id)->name == "Ashgrove");
    
    auto buildings = restored.get_buildings_in_region(village_id);
    CHECK_EQ(buildings.size(), 1);
    CHECK_EQ(restored.get_building(buildings[0])->name, "The Sleeping Fox");
}

TEST(world_remove_npc) {
    World world;
    auto npc = std::make_shared<NPC>(INVALID_ENTITY_ID, "Test", NPCTier::Tier2_Persistent);
    EntityID id = world.create_npc(npc);
    CHECK(world.get_npc(id) != nullptr);
    world.remove_npc(id);
    CHECK(world.get_npc(id) == nullptr);
}

TEST(world_id_generation) {
    World world;
    CHECK_EQ(world.next_id(), 1);
    CHECK_EQ(world.next_id(), 2);
    CHECK_EQ(world.next_id(), 3);
}

TEST(world_economy_value) {
    World world;
    Region r;
    r.name = "Village";
    EntityID rid = world.create_region(r);
    
    CHECK_EQ(world.get_item_value("food", rid), 5.0f);
    CHECK_EQ(world.get_item_value("weapon", rid), 30.0f);
}