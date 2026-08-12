#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <unordered_map>
#include <map>
#include <set>
#include <string>
#include <optional>

constexpr uint16_t MAP_W = 128;
constexpr uint16_t MAP_H = 96;
constexpr uint8_t TILE_SIZE = 16;

struct Vec2 {
    int16_t x, y;
    bool operator==(Vec2 const& o) const noexcept { return x == o.x && y == o.y; }
};

// ---- tiles ----
enum class Tile : uint8_t {
    Grass = 0, GrassVar = 1, Water = 2, WaterNorth = 3, WaterSouth = 4,
    WaterEast = 5, WaterWest = 6, Dirt = 7, Sand = 8, Tilled = 9, Bridge = 10,
    Snow = 11, Ice = 12, Cobble = 13,
};

// ---- items ----
enum class Item : uint16_t {
    None = 0,
    Hoe = 1, WateringCan = 2, Axe = 3, Pickaxe = 4, Scythe = 5,
    ParsnipSeeds = 10, PotatoSeeds = 11, CauliflowerSeeds = 12,
    CornSeeds = 13, TomatoSeeds = 14, WheatSeeds = 15, BlueberrySeeds = 16,
    GreenBeanSeeds = 17, HopsSeeds = 18,
    StrawberrySeeds = 19, MelonSeeds = 20, PumpkinSeeds = 21, RedCabbageSeeds = 22,
    RhubarbSeeds = 23, GarlicSeeds = 24, ArtichokeSeeds = 25, BokChoySeeds = 26, KaleSeeds = 27,
    CranberrySeeds = 28, GrapeSeeds = 29,
    Parsnip = 35, Potato = 36, Cauliflower = 37,
    Corn = 38, Tomato = 39, Wheat = 40, Blueberry = 41,
    GreenBean = 42, Hops = 43,
    Strawberry = 44, Melon = 45, Pumpkin = 46, RedCabbage = 47,
    Rhubarb = 48, Garlic = 49, Artichoke = 50, BokChoy = 51, Kale = 52,
    Cranberry = 53, Grape = 54,
    CopperOre = 6, IronOre = 7, GoldOre = 8, IridiumOre = 9,
    CopperBar = 17, IronBar = 18, GoldBar = 19,
    Wood = 60, Stone = 61, Fiber = 62,
    Fish = 63, Forage = 64, Bread = 65,
    FertilizerBasic = 66, FertilizerQuality = 67, FertilizerPremium = 68,
    Scarecrow = 69,
    Composter = 70,
    AppleSapling = 71, CherrySapling = 72, PeachSapling = 73, PomegranateSapling = 74,
    ApricotSapling = 75, OrangeSapling = 76, BananaSapling = 77, MangoSapling = 78,
    PlumSapling = 79, PearSapling = 80, FigSapling = 81, AvocadoSapling = 82,
    LemonSapling = 83, LimeSapling = 84, GrapefruitSapling = 85, PersimmonSapling = 86,
    Apple = 87, Cherry = 88, Peach = 89, Pomegranate = 90,
    Apricot = 91, Orange = 92, Banana = 93, Mango = 94,
    Plum = 95, Pear = 96, Fig = 97, Avocado = 98,
    Lemon = 99, Lime = 100, Grapefruit = 101, Persimmon = 102,
    // Tree products
    Sap = 110, Resin = 111, Rubber = 112, Bark = 113, Hardwood = 114,
    MapleSyrup = 115, OakResin = 116, PineTar = 117,
    // Tree seeds/saplings for forestation
    OakSapling = 120, MapleSapling = 121, BirchSapling = 122, CedarSapling = 123,
    RedwoodSapling = 124, TeakSapling = 125, MahoganySapling = 126, RubberTreeSapling = 127,
    WalnutSapling = 128, HickorySapling = 129, ChestnutSapling = 130,
    // Timber products
    OakLog = 131, MapleLog = 132, BirchLog = 133, CedarLog = 134,
    RedwoodLog = 135, TeakLog = 136, MahoganyLog = 137, RubberLog = 138,
    WalnutLog = 139, HickoryLog = 140, ChestnutLog = 141,
    Lumber = 142, Plank = 143, Plywood = 144,
    // Nuts
    Walnut = 145, HickoryNut = 146, Chestnut = 147, Acorn = 148,
};

struct ItemDef {
    const char* name;
    uint8_t type;        // 0 tool, 1 seed, 2 produce, 3 resource
    int buy;             // shop price, -1 = not sold
    int sell;
    uint8_t energy;      // energy cost to use (tools)
};

inline ItemDef const& item_def(Item it) {
    static const ItemDef defs[] = {
        /* 0  */ {"None",0,0,0,0},
        /* 1  */ {"Hoe",0,-1,0,2},{"Watering Can",0,-1,0,2},{"Axe",0,-1,0,5},
        /* 4  */ {"Pickaxe",0,-1,0,5},{"Scythe",0,-1,0,2},
        /* 6-9  */ {"Copper Ore",3,-1,15,0},{"Iron Ore",3,-1,30,0},
                    {"Gold Ore",3,-1,60,0},{"Iridium Ore",3,-1,150,0},
        /* 10 */ {"Parsnip Seeds",1,20,0,0},{"Potato Seeds",1,50,0,0},{"Cauliflower Seeds",1,80,0,0},
        /* 13 */ {"Corn Seeds",1,150,0,0},{"Tomato Seeds",1,50,0,0},{"Wheat Seeds",1,10,0,0},
                {"Blueberry Seeds",1,80,0,0},
        /* 17 */ {"Green Bean Seeds",1,60,0,0},{"Hops Seeds",1,60,0,0},{"Strawberry Seeds",1,100,0,0},
        /* 20 */ {"Melon Seeds",1,80,0,0},{"Pumpkin Seeds",1,100,0,0},{"Red Cabbage Seeds",1,100,0,0},
        /* 23 */ {"Rhubarb Seeds",1,100,0,0},{"Garlic Seeds",1,40,0,0},{"Artichoke Seeds",1,30,0,0},
                {"Bok Choy Seeds",1,50,0,0},{"Kale Seeds",1,70,0,0},
        /* 28 */ {"Cranberry Seeds",1,240,0,0},{"Grape Seeds",1,60,0,0},
        /* 17-19 */ {"Copper Bar",3,-1,2,0},{"Iron Bar",3,-1,4,0},{"Gold Bar",3,-1,8,0},
        /* 35 */ {"Parsnip",2,-1,35,0},{"Potato",2,-1,80,0},{"Cauliflower",2,-1,175,0},
        /* 38 */ {"Corn",2,-1,110,0},{"Tomato",2,-1,120,0},{"Wheat",2,-1,25,0},
                {"Blueberry",2,-1,100,0},
        /* 42 */ {"Green Bean",2,-1,40,0},{"Hops",2,-1,25,0},
        /* 44 */ {"Strawberry",2,-1,120,0},{"Melon",2,-1,250,0},{"Pumpkin",2,-1,320,0},{"Red Cabbage",2,-1,260,0},
        /* 48 */ {"Rhubarb",2,-1,220,0},{"Garlic",2,-1,60,0},{"Artichoke",2,-1,160,0},
                {"Bok Choy",2,-1,80,0},{"Kale",2,-1,110,0},
        /* 53 */ {"Cranberry",2,-1,75,0},{"Grape",2,-1,80,0},
        /* 60 */ {"Wood",3,-1,2,0},{"Stone",3,-1,2,0},{"Fiber",3,-1,1,0},
        /* 63 */ {"Fish",3,-1,0,0},{"Forage",3,-1,0,0},
        /* 65 */ {"Bread",2,5,0,0},
        /* 66 */ {"Fertilizer Basic",3,100,0,0},{"Fertilizer Quality",3,200,0,0},{"Fertilizer Premium",3,400,0,0},
        /* 69 */ {"Scarecrow",3,500,0,0},
        /* 70 */ {"Composter",3,1000,0,0},
        /* 71 */ {"Apple Sapling",3,4000,0,0},{"Cherry Sapling",3,3400,0,0},{"Peach Sapling",3,6000,0,0},{"Pomegranate Sapling",3,6000,0,0},
        /* 75 */ {"Apricot Sapling",3,2000,0,0},{"Orange Sapling",3,4000,0,0},{"Banana Sapling",3,5000,0,0},{"Mango Sapling",3,5000,0,0},
        /* 79 */ {"Plum Sapling",3,2600,0,0},{"Pear Sapling",3,3000,0,0},{"Fig Sapling",3,3000,0,0},{"Avocado Sapling",3,5000,0,0},
        /* 83 */ {"Lemon Sapling",3,2000,0,0},{"Lime Sapling",3,2000,0,0},{"Grapefruit Sapling",3,2600,0,0},{"Persimmon Sapling",3,3000,0,0},
        /* 87 */ {"Apple",2,-1,100,0},{"Cherry",2,-1,80,0},{"Peach",2,-1,140,0},{"Pomegranate",2,-1,140,0},
        /* 91 */ {"Apricot",2,-1,50,0},{"Orange",2,-1,100,0},{"Banana",2,-1,150,0},{"Mango",2,-1,130,0},
        /* 95 */ {"Plum",2,-1,80,0},{"Pear",2,-1,100,0},{"Fig",2,-1,90,0},{"Avocado",2,-1,150,0},
        /* 99 */ {"Lemon",2,-1,50,0},{"Lime",2,-1,40,0},{"Grapefruit",2,-1,80,0},{"Persimmon",2,-1,120,0},
        /* 110 */ {"Sap",3,-1,2,0},{"Resin",3,-1,5,0},{"Rubber",3,-1,10,0},{"Bark",3,-1,3,0},{"Hardwood",3,-1,15,0},
        /* 115 */ {"Maple Syrup",2,-1,200,0},{"Oak Resin",3,-1,15,0},{"Pine Tar",3,-1,10,0},
        /* 120 */ {"Oak Sapling",3,200,0,0},{"Maple Sapling",3,200,0,0},{"Birch Sapling",3,150,0,0},{"Cedar Sapling",3,300,0,0},
        /* 124 */ {"Redwood Sapling",3,500,0,0},{"Teak Sapling",3,1000,0,0},{"Mahogany Sapling",3,1000,0,0},{"Rubber Tree Sapling",3,500,0,0},
        /* 128 */ {"Walnut Sapling",3,400,0,0},{"Hickory Sapling",3,400,0,0},{"Chestnut Sapling",3,500,0,0},
        /* 131 */ {"Oak Log",3,-1,50,0},{"Maple Log",3,-1,60,0},{"Birch Log",3,-1,40,0},{"Cedar Log",3,-1,80,0},
        /* 135 */ {"Redwood Log",3,-1,200,0},{"Teak Log",3,-1,300,0},{"Mahogany Log",3,-1,300,0},{"Rubber Log",3,-1,100,0},
        /* 139 */ {"Walnut Log",3,-1,100,0},{"Hickory Log",3,-1,120,0},{"Chestnut Log",3,-1,100,0},
        /* 142 */ {"Lumber",3,-1,20,0},{"Plank",3,-1,40,0},{"Plywood",3,-1,80,0},
        /* 145 */ {"Walnut",2,-1,30,0},{"Hickory Nut",2,-1,25,0},{"Chestnut",2,-1,20,0},{"Acorn",3,-1,1,0},
    };
    uint16_t i = static_cast<uint16_t>(it);
    return i < sizeof(defs)/sizeof(defs[0]) ? defs[i] : defs[0];
}

// ---- crops (seed -> produce) ----
struct CropDef {
    const char* name;
    Item seed;
    Item produce;
    int buy;
    int sell;
    uint8_t days;
    uint8_t min_season;   // 0 spring ...
    uint8_t max_season;   // 3 winter (crops unplantable in winter)
};

inline CropDef const* crop_def(const char* name) {
    static const CropDef crops[] = {
        {"parsnip", Item::ParsnipSeeds, Item::Parsnip, 20, 35, 4, 0, 2},
        {"potato", Item::PotatoSeeds, Item::Potato, 50, 80, 6, 0, 2},
        {"cauliflower", Item::CauliflowerSeeds, Item::Cauliflower, 80, 175, 12, 0, 1},
        {"corn", Item::CornSeeds, Item::Corn, 150, 110, 14, 1, 2},
        {"tomato", Item::TomatoSeeds, Item::Tomato, 50, 120, 10, 1, 1},
        {"wheat", Item::WheatSeeds, Item::Wheat, 10, 25, 4, 0, 3},
        {"blueberry", Item::BlueberrySeeds, Item::Blueberry, 80, 100, 13, 0, 2},
        {"green bean", Item::GreenBeanSeeds, Item::GreenBean, 60, 40, 10, 0, 2},
        {"hops", Item::HopsSeeds, Item::Hops, 60, 25, 11, 1, 1},
        {"strawberry", Item::StrawberrySeeds, Item::Strawberry, 100, 120, 8, 0, 2},
        {"melon", Item::MelonSeeds, Item::Melon, 80, 250, 12, 1, 1},
        {"pumpkin", Item::PumpkinSeeds, Item::Pumpkin, 100, 320, 13, 2, 2},
        {"red cabbage", Item::RedCabbageSeeds, Item::RedCabbage, 100, 260, 9, 2, 2},
        {"rhubarb", Item::RhubarbSeeds, Item::Rhubarb, 100, 220, 13, 0, 1},
        {"garlic", Item::GarlicSeeds, Item::Garlic, 40, 60, 8, 2, 2},
        {"artichoke", Item::ArtichokeSeeds, Item::Artichoke, 30, 160, 8, 2, 2},
        {"bok choy", Item::BokChoySeeds, Item::BokChoy, 50, 80, 4, 2, 2},
        {"kale", Item::KaleSeeds, Item::Kale, 70, 110, 6, 2, 2},
        {"cranberry", Item::CranberrySeeds, Item::Cranberry, 240, 75, 7, 2, 2},
        {"grape", Item::GrapeSeeds, Item::Grape, 60, 80, 10, 1, 1},
        {"apple", Item::AppleSapling, Item::Apple, 4000, 100, 28, 0, 0},
        {"cherry", Item::CherrySapling, Item::Cherry, 3400, 80, 28, 0, 0},
        {"peach", Item::PeachSapling, Item::Peach, 6000, 140, 28, 0, 0},
        {"pomegranate", Item::PomegranateSapling, Item::Pomegranate, 6000, 140, 28, 0, 0},
        {"apricot", Item::ApricotSapling, Item::Apricot, 2000, 50, 28, 0, 0},
        {"orange", Item::OrangeSapling, Item::Orange, 4000, 100, 28, 0, 0},
        {"banana", Item::BananaSapling, Item::Banana, 5000, 150, 28, 0, 0},
        {"mango", Item::MangoSapling, Item::Mango, 5000, 130, 28, 0, 0},
        {"plum", Item::PlumSapling, Item::Plum, 2600, 80, 28, 0, 0},
        {"pear", Item::PearSapling, Item::Pear, 3000, 100, 28, 0, 0},
        {"fig", Item::FigSapling, Item::Fig, 3000, 90, 28, 0, 0},
        {"avocado", Item::AvocadoSapling, Item::Avocado, 5000, 150, 28, 0, 0},
        {"lemon", Item::LemonSapling, Item::Lemon, 2000, 50, 28, 0, 0},
        {"lime", Item::LimeSapling, Item::Lime, 2000, 40, 28, 0, 0},
        {"grapefruit", Item::GrapefruitSapling, Item::Grapefruit, 2600, 80, 28, 0, 0},
        {"persimmon", Item::PersimmonSapling, Item::Persimmon, 3000, 120, 28, 0, 0},
    };
    std::string nm(name);
    // normalize: "parsnip seeds" -> "parsnip", "parsnips" -> "parsnip"
    if (nm.size() > 6 && nm.substr(nm.size() - 6) == " seeds") nm = nm.substr(0, nm.size() - 6);
    for (auto& c : crops)
        if (nm == c.name || nm == std::string(c.name) + "s" ||
            nm == std::string(c.name) + " seed" || nm == std::string(c.name) + " seeds")
            return &c;
    return nullptr;
}

inline CropDef const* crop_def(Item produce) {
    static const CropDef crops[] = {
        {"parsnip", Item::ParsnipSeeds, Item::Parsnip, 20, 35, 4, 0, 2},
        {"potato", Item::PotatoSeeds, Item::Potato, 50, 80, 6, 0, 2},
        {"cauliflower", Item::CauliflowerSeeds, Item::Cauliflower, 80, 175, 12, 0, 1},
        {"corn", Item::CornSeeds, Item::Corn, 150, 110, 14, 1, 2},
        {"tomato", Item::TomatoSeeds, Item::Tomato, 50, 120, 10, 1, 1},
        {"wheat", Item::WheatSeeds, Item::Wheat, 10, 25, 4, 0, 3},
        {"blueberry", Item::BlueberrySeeds, Item::Blueberry, 80, 100, 13, 0, 2},
        {"green bean", Item::GreenBeanSeeds, Item::GreenBean, 60, 40, 10, 0, 2},
        {"hops", Item::HopsSeeds, Item::Hops, 60, 25, 11, 1, 1},
        {"strawberry", Item::StrawberrySeeds, Item::Strawberry, 100, 120, 8, 0, 2},
        {"melon", Item::MelonSeeds, Item::Melon, 80, 250, 12, 1, 1},
        {"pumpkin", Item::PumpkinSeeds, Item::Pumpkin, 100, 320, 13, 2, 2},
        {"red cabbage", Item::RedCabbageSeeds, Item::RedCabbage, 100, 260, 9, 2, 2},
        {"rhubarb", Item::RhubarbSeeds, Item::Rhubarb, 100, 220, 13, 0, 1},
        {"garlic", Item::GarlicSeeds, Item::Garlic, 40, 60, 8, 2, 2},
        {"artichoke", Item::ArtichokeSeeds, Item::Artichoke, 30, 160, 8, 2, 2},
        {"bok choy", Item::BokChoySeeds, Item::BokChoy, 50, 80, 4, 2, 2},
        {"kale", Item::KaleSeeds, Item::Kale, 70, 110, 6, 2, 2},
        {"cranberry", Item::CranberrySeeds, Item::Cranberry, 240, 75, 7, 2, 2},
        {"grape", Item::GrapeSeeds, Item::Grape, 60, 80, 10, 1, 1},
        {"apple", Item::AppleSapling, Item::Apple, 4000, 100, 28, 0, 0},
        {"cherry", Item::CherrySapling, Item::Cherry, 3400, 80, 28, 0, 0},
        {"peach", Item::PeachSapling, Item::Peach, 6000, 140, 28, 0, 0},
        {"pomegranate", Item::PomegranateSapling, Item::Pomegranate, 6000, 140, 28, 0, 0},
        {"apricot", Item::ApricotSapling, Item::Apricot, 2000, 50, 28, 0, 0},
        {"orange", Item::OrangeSapling, Item::Orange, 4000, 100, 28, 0, 0},
        {"banana", Item::BananaSapling, Item::Banana, 5000, 150, 28, 0, 0},
        {"mango", Item::MangoSapling, Item::Mango, 5000, 130, 28, 0, 0},
        {"plum", Item::PlumSapling, Item::Plum, 2600, 80, 28, 0, 0},
        {"pear", Item::PearSapling, Item::Pear, 3000, 100, 28, 0, 0},
        {"fig", Item::FigSapling, Item::Fig, 3000, 90, 28, 0, 0},
        {"avocado", Item::AvocadoSapling, Item::Avocado, 5000, 150, 28, 0, 0},
        {"lemon", Item::LemonSapling, Item::Lemon, 2000, 50, 28, 0, 0},
        {"lime", Item::LimeSapling, Item::Lime, 2000, 40, 28, 0, 0},
        {"grapefruit", Item::GrapefruitSapling, Item::Grapefruit, 2600, 80, 28, 0, 0},
        {"persimmon", Item::PersimmonSapling, Item::Persimmon, 3000, 120, 28, 0, 0},
    };
    for (auto& c : crops)
        if (c.produce == produce) return &c;
    return nullptr;
}

inline CropDef const* crop_by_seed(Item seed) {
    static const CropDef crops[] = {
        {"parsnip", Item::ParsnipSeeds, Item::Parsnip, 20, 35, 4, 0, 2},
        {"potato", Item::PotatoSeeds, Item::Potato, 50, 80, 6, 0, 2},
        {"cauliflower", Item::CauliflowerSeeds, Item::Cauliflower, 80, 175, 12, 0, 1},
        {"corn", Item::CornSeeds, Item::Corn, 150, 110, 14, 1, 2},
        {"tomato", Item::TomatoSeeds, Item::Tomato, 50, 120, 10, 1, 1},
        {"wheat", Item::WheatSeeds, Item::Wheat, 10, 25, 4, 0, 3},
        {"blueberry", Item::BlueberrySeeds, Item::Blueberry, 80, 100, 13, 0, 2},
        {"green bean", Item::GreenBeanSeeds, Item::GreenBean, 60, 40, 10, 0, 2},
        {"hops", Item::HopsSeeds, Item::Hops, 60, 25, 11, 1, 1},
        {"strawberry", Item::StrawberrySeeds, Item::Strawberry, 100, 120, 8, 0, 2},
        {"melon", Item::MelonSeeds, Item::Melon, 80, 250, 12, 1, 1},
        {"pumpkin", Item::PumpkinSeeds, Item::Pumpkin, 100, 320, 13, 2, 2},
        {"red cabbage", Item::RedCabbageSeeds, Item::RedCabbage, 100, 260, 9, 2, 2},
        {"rhubarb", Item::RhubarbSeeds, Item::Rhubarb, 100, 220, 13, 0, 1},
        {"garlic", Item::GarlicSeeds, Item::Garlic, 40, 60, 8, 2, 2},
        {"artichoke", Item::ArtichokeSeeds, Item::Artichoke, 30, 160, 8, 2, 2},
        {"bok choy", Item::BokChoySeeds, Item::BokChoy, 50, 80, 4, 2, 2},
        {"kale", Item::KaleSeeds, Item::Kale, 70, 110, 6, 2, 2},
        {"cranberry", Item::CranberrySeeds, Item::Cranberry, 240, 75, 7, 2, 2},
        {"grape", Item::GrapeSeeds, Item::Grape, 60, 80, 10, 1, 1},
        {"apple", Item::AppleSapling, Item::Apple, 4000, 100, 28, 0, 0},
        {"cherry", Item::CherrySapling, Item::Cherry, 3400, 80, 28, 0, 0},
        {"peach", Item::PeachSapling, Item::Peach, 6000, 140, 28, 0, 0},
        {"pomegranate", Item::PomegranateSapling, Item::Pomegranate, 6000, 140, 28, 0, 0},
        {"apricot", Item::ApricotSapling, Item::Apricot, 2000, 50, 28, 0, 0},
        {"orange", Item::OrangeSapling, Item::Orange, 4000, 100, 28, 0, 0},
        {"banana", Item::BananaSapling, Item::Banana, 5000, 150, 28, 0, 0},
        {"mango", Item::MangoSapling, Item::Mango, 5000, 130, 28, 0, 0},
        {"plum", Item::PlumSapling, Item::Plum, 2600, 80, 28, 0, 0},
        {"pear", Item::PearSapling, Item::Pear, 3000, 100, 28, 0, 0},
        {"fig", Item::FigSapling, Item::Fig, 3000, 90, 28, 0, 0},
        {"avocado", Item::AvocadoSapling, Item::Avocado, 5000, 150, 28, 0, 0},
        {"lemon", Item::LemonSapling, Item::Lemon, 2000, 50, 28, 0, 0},
        {"lime", Item::LimeSapling, Item::Lime, 2000, 40, 28, 0, 0},
        {"grapefruit", Item::GrapefruitSapling, Item::Grapefruit, 2600, 80, 28, 0, 0},
        {"persimmon", Item::PersimmonSapling, Item::Persimmon, 3000, 120, 28, 0, 0},
    };
    for (auto& c : crops)
        if (c.seed == seed) return &c;
    return nullptr;
}

// ---- NPCs ----
struct NPC {
    std::string name;
    std::string kind = "villager"; // "villager", "rabbit", etc.
    uint8_t color = 1;         // 1..5 sprite color (index into NPC list; color >palette clamps via %)
    Vec2 pos;
    Vec2 way[2];
    uint8_t way_idx = 0;
    uint64_t next_move_ms = 0;
    uint8_t dir = 0;
    // daily schedule: current time slot + BFS path toward its anchor
    int8_t sched_slot = -1;
    std::vector<Vec2> path;
    size_t path_i = 0;
};

// ---- world objects ----
enum class ObjType : uint8_t {
    None = 0, Tree, Rock, Weed, TallGrass, Flower, Stump,
    FencePost, FenceRail, Pine, Bush, Mushroom, Building,
    Sprinkler = 13, Statue, LeafLitter, Scarecrow, Composter,
    // Forestation trees
    Oak = 18, Maple, Birch, Cedar, Redwood, Teak, Mahogany, RubberTree,
    WalnutTree, HickoryTree, ChestnutTree,
};
static inline bool is_machine(ObjType t) { return t == ObjType::Sprinkler || t == ObjType::Composter; }
static inline bool is_tree(ObjType t) {
    return t == ObjType::Tree || t == ObjType::Pine ||
           t == ObjType::Oak || t == ObjType::Maple || t == ObjType::Birch ||
           t == ObjType::Cedar || t == ObjType::Redwood || t == ObjType::Teak ||
           t == ObjType::Mahogany || t == ObjType::RubberTree ||
           t == ObjType::WalnutTree || t == ObjType::HickoryTree || t == ObjType::ChestnutTree;
}
static inline Item tree_log_item(ObjType t) {
    switch (t) {
        case ObjType::Tree: return Item::Wood;
        case ObjType::Pine: return Item::Wood;
        case ObjType::Oak: return Item::OakLog;
        case ObjType::Maple: return Item::MapleLog;
        case ObjType::Birch: return Item::BirchLog;
        case ObjType::Cedar: return Item::CedarLog;
        case ObjType::Redwood: return Item::RedwoodLog;
        case ObjType::Teak: return Item::TeakLog;
        case ObjType::Mahogany: return Item::MahoganyLog;
        case ObjType::RubberTree: return Item::RubberLog;
        case ObjType::WalnutTree: return Item::WalnutLog;
        case ObjType::HickoryTree: return Item::HickoryLog;
        case ObjType::ChestnutTree: return Item::ChestnutLog;
        default: return Item::Wood;
    }
}
static inline Item tree_sap_item(ObjType t) {
    switch (t) {
        case ObjType::Maple: return Item::MapleSyrup;
        case ObjType::Oak: return Item::OakResin;
        case ObjType::Pine: return Item::PineTar;
        case ObjType::RubberTree: return Item::Rubber;
        case ObjType::Birch: return Item::Sap;
        case ObjType::Cedar: return Item::Resin;
        default: return Item::Sap;
    }
}

static inline const char* obj_type_name(ObjType t) {
    switch (t) {
        case ObjType::Tree: return "tree";
        case ObjType::Pine: return "pine";
        case ObjType::Oak: return "oak";
        case ObjType::Maple: return "maple";
        case ObjType::Birch: return "birch";
        case ObjType::Cedar: return "cedar";
        case ObjType::Redwood: return "redwood";
        case ObjType::Teak: return "teak";
        case ObjType::Mahogany: return "mahogany";
        case ObjType::RubberTree: return "rubber tree";
        case ObjType::WalnutTree: return "walnut";
        case ObjType::HickoryTree: return "hickory";
        case ObjType::ChestnutTree: return "chestnut";
        case ObjType::Stump: return "stump";
        default: return "object";
    }
}

struct Bldg {
    std::string name;
    int16_t x, y, w, h;
};

// Building condition state for weathering/maintenance
struct BuildingState {
    uint8_t condition = 100;     // 0..100, decays over time
    uint8_t roof_leak = 0;        // rain damage accrued
    uint8_t foundation = 100;     // winter frost damage
    uint32_t last_maintained_day = 0;
};

// ---- interiors ----
struct InteriorRoom {
    std::string building;              // matches Bldg::name; "" = none
    std::vector<std::string> rows;     // '#' wall, '.' floor, letters = furniture
    int16_t w = 0, h = 0;
};

struct FarmObj { ObjType type = ObjType::None; uint8_t hp = 0; uint8_t ore = 0; };

struct Crop {
    Item crop = Item::None;   // Parsnip / Potato / Cauliflower
    uint8_t stage = 0;        // 0..3 (sprite index)
    int8_t days_left = 0;     // days until mature
    bool watered = false;
    // Trellis crops (green bean, hops) are impassable when growing
    // Fruit trees (apple, cherry, peach, pomegranate) produce seasonally
    bool is_trellis = false;
    bool is_fruit_tree = false;
    int8_t last_harvest_season = -1; // for fruit trees: season when last harvested
    bool is_crop() const { return crop != Item::None; }
};

// ---- cell ----
struct Cell {
    Tile tile = Tile::Grass;
    FarmObj obj;
    Crop crop;
};

// ---- player ----
struct InvSlot { Item item = Item::None; uint16_t count = 0; };

struct Player {
    uint32_t id = 0;
    Vec2 pos, target;
    uint8_t dir = 0;           // 0 down, 1 left, 2 right, 3 up
    bool moving = false;
    uint32_t move_start_ms = 0;
    std::string name;
    float energy = 270.0f;
    uint16_t max_energy = 270;
    int money = 500;
    uint8_t sel = 0;           // selected inventory slot
    std::array<InvSlot, 12> inv; // tools + items
    std::vector<Vec2> path;    // waypoints (mutex-guarded)
    std::string inside;        // building interior name, or "" for overworld
    int16_t inx = 0, iny = 0;  // position inside the interior
    bool train_used = false;   // rode the Zuzu express today
    Vec2 inside_exit;          // overworld tile to return to on exit
    // friendship: npc name -> hearts (0..10). one gift per NPC per day.
    std::map<std::string, uint8_t> hearts;
    std::set<std::string> gifted_today;
    // Egg Festival (Spring 13): eggs found this festival, searches remaining.
    uint8_t fest_eggs = 0;
    uint8_t fest_tries = 8;
    // Known landmarks for fast-travel / path-walking (building names player has entered)
    std::set<std::string> known_landmarks;
};

// ---- world ----
struct World {
    std::array<Cell, MAP_W * MAP_H> cells;
    std::unordered_map<uint32_t, Player> players;
    std::vector<NPC> npcs;
    std::vector<Bldg> buildings;
    std::unordered_map<std::string, InteriorRoom> interiors;
    std::unordered_map<std::string, BuildingState> building_states;
    uint32_t next_player_id = 1;
    uint32_t day = 1;
    float day_seconds = 0.0f;   // seconds since 6:00 AM
    static constexpr float DAY_LENGTH_S = 800.0f; // 20h @ 40s per game hour

    // Farmhouse upgrade level (1=starter, 2=cottage, 3=house, 4=manor)
    uint8_t farmhouse_level = 1;

    Vec2 house_tl{38, 78};

    Cell&     at(int x, int y) { return cells[static_cast<size_t>(y) * MAP_W + static_cast<size_t>(x)]; }
    Cell const& at(int x, int y) const { return cells[static_cast<size_t>(y) * MAP_W + static_cast<size_t>(x)]; }
    Cell&     at(Vec2 p) { return cells[static_cast<size_t>(p.y) * MAP_W + static_cast<size_t>(p.x)]; }
    Cell const& at(Vec2 p) const { return cells[static_cast<size_t>(p.y) * MAP_W + static_cast<size_t>(p.x)]; }
    bool in_bounds(int x, int y) const { return x >= 0 && x < MAP_W && y >= 0 && y < MAP_H; }
    bool in_bounds(Vec2 p) const { return in_bounds(p.x, p.y); }

    bool walkable(int x, int y) const {
        if (!in_bounds(x, y)) return false;
        Tile t = at(x, y).tile;
        if (t == Tile::Water || t == Tile::WaterNorth || t == Tile::WaterSouth ||
            t == Tile::WaterEast || t == Tile::WaterWest) return false;
        ObjType o = at(x, y).obj.type;
        if (is_tree(o) || o == ObjType::Rock || o == ObjType::Stump ||
            o == ObjType::FencePost || o == ObjType::FenceRail ||
            o == ObjType::Building ||
            o == ObjType::Sprinkler || o == ObjType::Statue) return false;
        // Trellis crops (green bean, hops) are impassable
        const Crop& crop = at(x, y).crop;
        if (crop.is_crop() && crop.is_trellis) return false;
        return true;
    }
    bool walkable(Vec2 p) const { return walkable(p.x, p.y); }

    bool in_house(int x, int y) const {
        return x >= house_tl.x && x < house_tl.x + 3 && y >= house_tl.y && y < house_tl.y + 4;
    }
    Vec2 door() const { return {int16_t(house_tl.x + 1), int16_t(house_tl.y + 3)}; }
};

void generate_world(World& world);
void resolve_water_edges(World& world);
void init_npcs(World& world);
void place_buildings(World& world);
void clear_paths(World& world);   // keep bridges + doorways passable (post-scatter)
void init_interiors(World& world);
int  hour_of_day(const World& w);      // 6..26 (26 == 2:00 AM)
const char* clock_str(const World& w); // "Day 3 · 10:40 AM"
int  season_index(uint32_t day);       // 0 spring .. 3 winter
int  season_day(uint32_t day);         // 1..28 within season
const char* season_name(int season);
int  weather_of_day(uint32_t day);     // 0 sunny, 1 rainy
const char* weather_of_day_name(uint32_t day);
const char* region_at(const World& w, int x, int y);
const char* npc_line(const char* name, int season);
int  npc_at(const World& w, int x, int y);   // index into world.npcs or -1
bool is_festival_day(uint32_t day);          // Egg Festival: Spring 13
// daily schedule slot for hour; writes the anchor to walk to (-1 = free-roam)
int  schedule_slot(const std::string& name, uint32_t day, int hour, Vec2& anchor);
std::string forage_table(int season, int& count);       // fills count
std::string fish_table(int season, int& count);         // fills count
bool bfs_path(World& world, Vec2 from, Vec2 to, std::vector<Vec2>& out, size_t max_len = 64);
std::string serialize_world(const World& w);
bool deserialize_world(World& w, const std::string& json_str);
void add_item(Player& p, Item item, uint16_t count);     // first free slot, else adds to existing
bool consume_item(Player& p, Item item, uint16_t count);