#include "test_utils.h"
#include "simulation/simulation.h"
#include "world/world.h"

using namespace ashgrove;

TEST(simulation_time_advance) {
    Simulation sim;
    World world;
    sim.initialize(&world);
    
    sim.advance_time(60); // 1 hour
    CHECK_EQ(sim.get_time().hour, 7);
    CHECK_EQ(sim.get_time().minute, 0);
    CHECK_EQ(sim.get_time().ticks, 60);
}

TEST(simulation_day_rollover) {
    Simulation sim;
    World world;
    sim.initialize(&world);
    
    sim.advance_time(18 * 60); // 18 hours from 6 AM -> midnight
    CHECK_EQ(sim.get_time().hour, 0);
    CHECK_EQ(sim.get_time().day_of_year, 2);
    CHECK_EQ(sim.get_time().ticks, 1080);
}

TEST(simulation_season_change) {
    Simulation sim;
    World world;
    sim.initialize(&world);
    
    // Default year starts at day 1 (Spring). Advance ~3 months (90 days).
    sim.advance_time(90 * 1440);
    CHECK(sim.get_time().season == Season::Summer || sim.get_time().season == Season::Autumn);
}

TEST(simulation_serialization) {
    Simulation sim;
    World world;
    sim.initialize(&world);
    sim.advance_time(100);
    
    auto j = sim.serialize();
    CHECK(j["ticks"] == 100);
    
    Simulation restored;
    restored.deserialize(j);
    CHECK_EQ(restored.get_time().ticks, 100);
}

TEST(simulation_pause) {
    Simulation sim;
    World world;
    sim.initialize(&world);
    sim.pause();
    sim.advance_time(50);
    CHECK_EQ(sim.get_tick(), 0);
    sim.resume();
    sim.advance_time(10);
    CHECK_EQ(sim.get_tick(), 10);
}

TEST(simulation_weather_consistency) {
    Simulation sim;
    World world;
    sim.initialize(&world);
    sim.advance_time(1440 * 30); // 30 days
    // Weather should always be a valid enum
    int w = static_cast<int>(sim.get_time().weather);
    CHECK(w >= 0 && w <= 5);
}