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
    CopperBar = 30, IronBar = 31, GoldBar = 32,
    Wood = 60, Stone = 61, Fiber = 62,
    Fish = 63, Forage = 64, Bread = 65,
    FertilizerBasic = 66, FertilizerQuality = 67, FertilizerPremium = 68,
    Scarecrow = 69,
    Composter = 70,
    AppleSapling = 71, CherrySapling = 72, PeachSapling = 73, PomegranateSapling = 74,
    ApricotSapling = 75, OrangeSapling = 76, BananaSapling = 77, MangoSapling = 78,
    PlumSapling = 79, PearSapling = 80, FigSapling = 81, AvocadoSapling = 82,
    LemonSapling = 83, LimeSapling = 84, GrapefruitSapling = 85, PersimmonSapling = 86,
    DeodarSapling = 87,  // Indian Deodar (Cedar deodara)
    Apple = 90, Cherry = 91, Peach = 92, Pomegranate = 93,
    Apricot = 94, Orange = 95, Banana = 96, Mango = 97,
    Plum = 98, Pear = 99, Fig = 100, Avocado = 101,
    Lemon = 102, Lime = 103, Grapefruit = 104, Persimmon = 105,
    DeodarCone = 106, DeodarResin = 107, DeodarOil = 108,
    // Tree products
    Sap = 110, Resin = 111, Rubber = 112, Bark = 113, Hardwood = 114,
    MapleSyrup = 115, OakResin = 116, PineTar = 117,
    // Tree seeds/saplings for forestation
    OakSapling = 120, MapleSapling = 121, BirchSapling = 122, CedarSapling = 123,
    RedwoodSapling = 124, TeakSapling = 125, MahoganySapling = 126, RubberTreeSapling = 127,
    WalnutSapling = 128, HickorySapling = 129, ChestnutSapling = 130,
    // Timber products
    OakLog = 140, MapleLog = 141, BirchLog = 142, CedarLog = 143,
    RedwoodLog = 144, TeakLog = 145, MahoganyLog = 146, RubberLog = 147,
    WalnutLog = 148, HickoryLog = 149, ChestnutLog = 150, DeodarLog = 151,
    Lumber = 152, Plank = 153, Plywood = 154,
    // Nuts
    Walnut = 155, HickoryNut = 156, Chestnut = 157, Acorn = 158,
};

struct ItemDef {
    const char* name;
    uint8_t type;        // 0 tool, 1 seed, 2 produce, 3 resource
    int buy;             // shop price, -1 = not sold
    int sell;
    uint8_t energy;      // energy cost to use (tools)
};

inline ItemDef const& item_def(Item it) {
    // Table of {id, def} pairs looked up by id. Using a struct + linear scan
    // (instead of array indexing) keeps every item aligned to its enum id even
    // where enum values have gaps, so names/buy/sell/energy are always correct.
    struct Def { Item id; ItemDef d; };
    static const Def defs[] = {
        {Item::None, {"None",0,0,0,0}},
        {Item::Hoe, {"Hoe",0,-1,0,2}}, {Item::WateringCan, {"Watering Can",0,-1,0,2}}, {Item::Axe, {"Axe",0,-1,0,5}},
        {Item::Pickaxe, {"Pickaxe",0,-1,0,5}}, {Item::Scythe, {"Scythe",0,-1,0,2}},
        {Item::CopperOre, {"Copper Ore",3,-1,15,0}}, {Item::IronOre, {"Iron Ore",3,-1,30,0}},
        {Item::GoldOre, {"Gold Ore",3,-1,60,0}}, {Item::IridiumOre, {"Iridium Ore",3,-1,150,0}},
        {Item::ParsnipSeeds, {"Parsnip Seeds",1,20,0,0}}, {Item::PotatoSeeds, {"Potato Seeds",1,50,0,0}}, {Item::CauliflowerSeeds, {"Cauliflower Seeds",1,80,0,0}},
        {Item::CornSeeds, {"Corn Seeds",1,150,0,0}}, {Item::TomatoSeeds, {"Tomato Seeds",1,50,0,0}}, {Item::WheatSeeds, {"Wheat Seeds",1,10,0,0}},
        {Item::BlueberrySeeds, {"Blueberry Seeds",1,80,0,0}},
        {Item::GreenBeanSeeds, {"Green Bean Seeds",1,60,0,0}}, {Item::HopsSeeds, {"Hops Seeds",1,60,0,0}}, {Item::StrawberrySeeds, {"Strawberry Seeds",1,100,0,0}},
        {Item::MelonSeeds, {"Melon Seeds",1,80,0,0}}, {Item::PumpkinSeeds, {"Pumpkin Seeds",1,100,0,0}}, {Item::RedCabbageSeeds, {"Red Cabbage Seeds",1,100,0,0}},
        {Item::RhubarbSeeds, {"Rhubarb Seeds",1,100,0,0}}, {Item::GarlicSeeds, {"Garlic Seeds",1,40,0,0}}, {Item::ArtichokeSeeds, {"Artichoke Seeds",1,30,0,0}},
        {Item::BokChoySeeds, {"Bok Choy Seeds",1,50,0,0}}, {Item::KaleSeeds, {"Kale Seeds",1,70,0,0}},
        {Item::CranberrySeeds, {"Cranberry Seeds",1,240,0,0}}, {Item::GrapeSeeds, {"Grape Seeds",1,60,0,0}},
        {Item::CopperBar, {"Copper Bar",3,-1,2,0}}, {Item::IronBar, {"Iron Bar",3,-1,4,0}}, {Item::GoldBar, {"Gold Bar",3,-1,8,0}},
        {Item::Parsnip, {"Parsnip",2,-1,35,0}}, {Item::Potato, {"Potato",2,-1,80,0}}, {Item::Cauliflower, {"Cauliflower",2,-1,175,0}},
        {Item::Corn, {"Corn",2,-1,110,0}}, {Item::Tomato, {"Tomato",2,-1,120,0}}, {Item::Wheat, {"Wheat",2,-1,25,0}},
        {Item::Blueberry, {"Blueberry",2,-1,100,0}},
        {Item::GreenBean, {"Green Bean",2,-1,40,0}}, {Item::Hops, {"Hops",2,-1,25,0}},
        {Item::Strawberry, {"Strawberry",2,-1,120,0}}, {Item::Melon, {"Melon",2,-1,250,0}}, {Item::Pumpkin, {"Pumpkin",2,-1,320,0}}, {Item::RedCabbage, {"Red Cabbage",2,-1,260,0}},
        {Item::Rhubarb, {"Rhubarb",2,-1,220,0}}, {Item::Garlic, {"Garlic",2,-1,60,0}}, {Item::Artichoke, {"Artichoke",2,-1,160,0}},
        {Item::BokChoy, {"Bok Choy",2,-1,80,0}}, {Item::Kale, {"Kale",2,-1,110,0}},
        {Item::Cranberry, {"Cranberry",2,-1,75,0}}, {Item::Grape, {"Grape",2,-1,80,0}},
        {Item::Wood, {"Wood",3,-1,2,0}}, {Item::Stone, {"Stone",3,-1,2,0}}, {Item::Fiber, {"Fiber",3,-1,1,0}},
        {Item::Fish, {"Fish",3,-1,0,0}}, {Item::Forage, {"Forage",3,-1,0,0}},
        {Item::Bread, {"Bread",2,5,0,0}},
        {Item::FertilizerBasic, {"Fertilizer Basic",3,100,0,0}}, {Item::FertilizerQuality, {"Fertilizer Quality",3,200,0,0}}, {Item::FertilizerPremium, {"Fertilizer Premium",3,400,0,0}},
        {Item::Scarecrow, {"Scarecrow",3,500,0,0}},
        {Item::Composter, {"Composter",3,1000,0,0}},
        {Item::AppleSapling, {"Apple Sapling",3,4000,0,0}}, {Item::CherrySapling, {"Cherry Sapling",3,3400,0,0}}, {Item::PeachSapling, {"Peach Sapling",3,6000,0,0}}, {Item::PomegranateSapling, {"Pomegranate Sapling",3,6000,0,0}},
        {Item::ApricotSapling, {"Apricot Sapling",3,2000,0,0}}, {Item::OrangeSapling, {"Orange Sapling",3,4000,0,0}}, {Item::BananaSapling, {"Banana Sapling",3,5000,0,0}}, {Item::MangoSapling, {"Mango Sapling",3,5000,0,0}},
        {Item::PlumSapling, {"Plum Sapling",3,2600,0,0}}, {Item::PearSapling, {"Pear Sapling",3,3000,0,0}}, {Item::FigSapling, {"Fig Sapling",3,3000,0,0}}, {Item::AvocadoSapling, {"Avocado Sapling",3,5000,0,0}},
        {Item::LemonSapling, {"Lemon Sapling",3,2000,0,0}}, {Item::LimeSapling, {"Lime Sapling",3,2000,0,0}}, {Item::GrapefruitSapling, {"Grapefruit Sapling",3,2600,0,0}}, {Item::PersimmonSapling, {"Persimmon Sapling",3,3000,0,0}},
        {Item::DeodarSapling, {"Deodar Sapling",3,5000,0,0}},
        {Item::Apple, {"Apple",2,-1,100,0}}, {Item::Cherry, {"Cherry",2,-1,80,0}}, {Item::Peach, {"Peach",2,-1,140,0}}, {Item::Pomegranate, {"Pomegranate",2,-1,140,0}},
        {Item::Apricot, {"Apricot",2,-1,50,0}}, {Item::Orange, {"Orange",2,-1,100,0}}, {Item::Banana, {"Banana",2,-1,150,0}}, {Item::Mango, {"Mango",2,-1,130,0}},
        {Item::Plum, {"Plum",2,-1,80,0}}, {Item::Pear, {"Pear",2,-1,100,0}}, {Item::Fig, {"Fig",2,-1,90,0}}, {Item::Avocado, {"Avocado",2,-1,150,0}},
        {Item::Lemon, {"Lemon",2,-1,50,0}}, {Item::Lime, {"Lime",2,-1,40,0}}, {Item::Grapefruit, {"Grapefruit",2,-1,80,0}}, {Item::Persimmon, {"Persimmon",2,-1,120,0}},
        {Item::DeodarCone, {"Deodar Cone",2,-1,30,0}}, {Item::DeodarResin, {"Deodar Resin",3,-1,20,0}}, {Item::DeodarOil, {"Deodar Oil",3,-1,100,0}},
        {Item::Sap, {"Sap",3,-1,2,0}}, {Item::Resin, {"Resin",3,-1,5,0}}, {Item::Rubber, {"Rubber",3,-1,10,0}}, {Item::Bark, {"Bark",3,-1,3,0}}, {Item::Hardwood, {"Hardwood",3,-1,15,0}},
        {Item::MapleSyrup, {"Maple Syrup",2,-1,200,0}}, {Item::OakResin, {"Oak Resin",3,-1,15,0}}, {Item::PineTar, {"Pine Tar",3,-1,10,0}},
        {Item::OakSapling, {"Oak Sapling",3,200,0,0}}, {Item::MapleSapling, {"Maple Sapling",3,200,0,0}}, {Item::BirchSapling, {"Birch Sapling",3,150,0,0}}, {Item::CedarSapling, {"Cedar Sapling",3,300,0,0}},
        {Item::RedwoodSapling, {"Redwood Sapling",3,500,0,0}}, {Item::TeakSapling, {"Teak Sapling",3,1000,0,0}}, {Item::MahoganySapling, {"Mahogany Sapling",3,1000,0,0}}, {Item::RubberTreeSapling, {"Rubber Tree Sapling",3,500,0,0}},
        {Item::WalnutSapling, {"Walnut Sapling",3,400,0,0}}, {Item::HickorySapling, {"Hickory Sapling",3,400,0,0}}, {Item::ChestnutSapling, {"Chestnut Sapling",3,500,0,0}},
        {Item::OakLog, {"Oak Log",3,-1,50,0}}, {Item::MapleLog, {"Maple Log",3,-1,60,0}}, {Item::BirchLog, {"Birch Log",3,-1,40,0}}, {Item::CedarLog, {"Cedar Log",3,-1,80,0}},
        {Item::RedwoodLog, {"Redwood Log",3,-1,200,0}}, {Item::TeakLog, {"Teak Log",3,-1,300,0}}, {Item::MahoganyLog, {"Mahogany Log",3,-1,300,0}}, {Item::RubberLog, {"Rubber Log",3,-1,100,0}},
        {Item::WalnutLog, {"Walnut Log",3,-1,100,0}}, {Item::HickoryLog, {"Hickory Log",3,-1,120,0}}, {Item::ChestnutLog, {"Chestnut Log",3,-1,100,0}}, {Item::DeodarLog, {"Deodar Log",3,-1,150,0}},
        {Item::Lumber, {"Lumber",3,-1,20,0}}, {Item::Plank, {"Plank",3,-1,40,0}}, {Item::Plywood, {"Plywood",3,-1,80,0}},
        {Item::Walnut, {"Walnut",2,-1,30,0}}, {Item::HickoryNut, {"Hickory Nut",2,-1,25,0}}, {Item::Chestnut, {"Chestnut",2,-1,20,0}}, {Item::Acorn, {"Acorn",3,-1,1,0}},
    };
    for (auto const& d : defs)
        if (d.id == it) return d.d;
    return defs[0].d;
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
    WalnutTree, HickoryTree, ChestnutTree, Deodar,
};
static inline bool is_machine(ObjType t) { return t == ObjType::Sprinkler || t == ObjType::Composter; }
static inline bool is_tree(ObjType t) {
    return t == ObjType::Tree || t == ObjType::Pine ||
           t == ObjType::Oak || t == ObjType::Maple || t == ObjType::Birch ||
           t == ObjType::Cedar || t == ObjType::Redwood || t == ObjType::Teak ||
           t == ObjType::Mahogany || t == ObjType::RubberTree ||
           t == ObjType::WalnutTree || t == ObjType::HickoryTree || t == ObjType::ChestnutTree ||
           t == ObjType::Deodar;
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
        case ObjType::Deodar: return Item::DeodarLog;
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
        case ObjType::Deodar: return Item::DeodarResin;
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
        case ObjType::Deodar: return "deodar";
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
    std::string building;                    // matches Bldg::name; "" = none
    std::vector<std::string> rows;           // floor 0 (ground floor) - '#' wall, '.' floor, letters = furniture, ' ' = doorway
    std::vector<std::vector<std::string>> floors; // additional floors (floor 1+)
    int16_t w = 0, h = 0;
    // Helper: get floor by index (0 = ground, 1+ = upper)
    const std::vector<std::string>& get_floor(int floor) const {
        if (floor == 0) return rows;
        if (floor > 0 && floor - 1 < (int)floors.size()) return floors[floor - 1];
        static const std::vector<std::string> empty;
        return empty;
    }
    int num_floors() const { return 1 + (int)floors.size(); }
};

// ---- buyable plots ----
struct Plot {
    std::string name;
    int16_t x, y, w, h;       // plot area on the map
    int price;                // purchase price in gold
    const char* climate;      // climate description
    uint32_t owner_id = 0;    // 0 = unowned; player.id when bought
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
    // Owned plots (indices into world.plots)
    std::set<size_t> owned_plots;
	// Structures placed on plots: (plot_index, structure_type, tile_x, tile_y)
    struct PlacedStruct {
        size_t plot_idx = 0;
        uint8_t type = 0;   // 1=barn, 2=silo, 3=shed, 4=well, 5=scarecrow, 6=windmill
        int16_t x = 0, y = 0;
    };
    std::vector<PlacedStruct> placed_structs;
};

// ---- world ----
struct World {
    std::array<Cell, MAP_W * MAP_H> cells;
    std::unordered_map<uint32_t, Player> players;
    std::vector<NPC> npcs;
    std::vector<Bldg> buildings;
    std::unordered_map<std::string, InteriorRoom> interiors;
    std::unordered_map<std::string, BuildingState> building_states;
    std::vector<Plot> plots;   // buyable plots (R16)
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
void init_plots(World& world);     // R16: buyable plots
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
bool bfs_path(World& world, Vec2 from, Vec2 to, std::vector<Vec2>& out, size_t max_len = 512);
std::string serialize_world(const World& w);
bool deserialize_world(World& w, const std::string& json_str);
void add_item(Player& p, Item item, uint16_t count);     // first free slot, else adds to existing
bool consume_item(Player& p, Item item, uint16_t count);