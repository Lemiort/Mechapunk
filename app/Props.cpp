#include "Props.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <utility>

namespace mechapunk {

namespace {
using glm::vec3;

constexpr vec3 kWood{0.45F, 0.3F, 0.18F};
constexpr vec3 kDarkWood{0.24F, 0.15F, 0.09F};
constexpr vec3 kMetal{0.58F, 0.6F, 0.64F};
constexpr vec3 kDarkMetal{0.18F, 0.19F, 0.22F};
constexpr vec3 kWhite{0.86F, 0.87F, 0.88F};
constexpr vec3 kBlack{0.05F, 0.05F, 0.06F};
constexpr vec3 kTile{0.72F, 0.77F, 0.8F};
constexpr vec3 kGlass{0.6F, 0.8F, 0.92F};
constexpr vec3 kScreen{0.1F, 0.6F, 0.95F};
constexpr vec3 kNeonPink{1.0F, 0.2F, 0.7F};
constexpr vec3 kNeonCyan{0.2F, 0.9F, 1.0F};
constexpr vec3 kGlow{1.0F, 0.45F, 0.1F};
constexpr vec3 kGreen{0.2F, 0.9F, 0.4F};
constexpr vec3 kYellow{0.95F, 0.75F, 0.1F};
constexpr vec3 kLeaf{0.15F, 0.48F, 0.2F};
constexpr vec3 kPot{0.5F, 0.3F, 0.25F};
constexpr vec3 kChrome{0.7F, 0.72F, 0.76F};
constexpr vec3 kLampShade{0.85F, 0.78F, 0.6F};
constexpr vec3 kBulb{1.0F, 0.95F, 0.8F};
constexpr std::array<vec3, 6> kGoods{
    vec3{0.85F, 0.2F, 0.2F}, vec3{0.2F, 0.5F, 0.9F}, vec3{0.95F, 0.8F, 0.2F},
    vec3{0.3F, 0.8F, 0.4F},  vec3{0.8F, 0.4F, 0.9F}, vec3{0.9F, 0.55F, 0.2F}};

// A box with its base-center at (x, y, z) and the given width, height, depth.
PropPart Box(float x, float y, float z, float w, float h, float d, vec3 color) {
    return {.pivot = {x, y, z}, .size = {w, h, d}, .color = color};
}
PropPart Tinted(float x, float y, float z, float w, float h, float d) {
    PropPart part = Box(x, y, z, w, h, d, kWhite);
    part.tinted = true;
    return part;
}
PropPart Glass(float x, float y, float z, float w, float h, float d) {
    PropPart part = Box(x, y, z, w, h, d, kGlass);
    part.glass = true;
    return part;
}

// Four legs under a top of the given size and height.
void Legs(PropModel& model, float w, float d, float h, vec3 color,
          float thickness = 0.05F) {
    const float x = (w * 0.5F) - (thickness * 0.5F);
    const float z = (d * 0.5F) - (thickness * 0.5F);
    for (const float sx : {-x, x}) {
        for (const float sz : {-z, z}) {
            model.push_back(Box(sx, 0.0F, sz, thickness, h, thickness, color));
        }
    }
}

PropModel Make(PropKind kind) {
    PropModel m;
    switch (kind) {
        case PropKind::Bed:
            m = {Box(0, 0, 0, 1.6F, 0.35F, 2.1F, kDarkWood),
                 Tinted(0, 0.35F, 0.05F, 1.5F, 0.18F, 1.95F),
                 Box(0, 0.53F, -0.78F, 1.2F, 0.1F, 0.35F, kWhite),
                 Box(0, 0, -1.05F, 1.6F, 1.0F, 0.08F, kDarkWood),
                 Box(0, 0.88F, -1.0F, 1.2F, 0.05F, 0.04F, kNeonCyan)};
            break;
        case PropKind::Nightstand:
            m = {Box(0, 0, 0, 0.45F, 0.5F, 0.4F, kWood),
                 Box(0, 0.5F, 0, 0.15F, 0.25F, 0.15F, kWhite)};
            break;
        case PropKind::Wardrobe:
            m = {Box(0, 0, 0, 1.2F, 2.1F, 0.6F, kWood),
                 Box(0, 0.1F, 0.3F, 0.02F, 1.9F, 0.01F, kDarkWood)};
            break;
        case PropKind::Sofa:
            m = {Tinted(0, 0, 0, 2.0F, 0.42F, 0.9F),
                 Tinted(0, 0.42F, -0.33F, 2.0F, 0.45F, 0.24F),
                 Tinted(-0.92F, 0.42F, 0, 0.16F, 0.22F, 0.9F),
                 Tinted(0.92F, 0.42F, 0, 0.16F, 0.22F, 0.9F)};
            break;
        case PropKind::CoffeeTable:
            m = {Box(0, 0.35F, 0, 1.0F, 0.06F, 0.55F, kWood)};
            Legs(m, 0.9F, 0.45F, 0.35F, kDarkMetal);
            break;
        case PropKind::WallScreen:
            m = {Box(0, 1.0F, 0, 1.4F, 0.8F, 0.06F, kBlack),
                 Box(0, 1.05F, 0.031F, 1.3F, 0.7F, 0.01F, kScreen)};
            break;
        case PropKind::KitchenCounter:
            m = {Box(0, 0, 0, 2.4F, 0.88F, 0.6F, kWhite),
                 Box(0, 0.88F, 0, 2.44F, 0.04F, 0.64F, kDarkMetal),
                 Box(0.6F, 0.92F, 0, 0.6F, 0.02F, 0.5F, kBlack),
                 Box(-0.6F, 0.9F, 0.05F, 0.5F, 0.03F, 0.4F, kMetal),
                 Box(0, 1.5F, -0.15F, 2.4F, 0.7F, 0.3F, kWhite)};
            break;
        case PropKind::Fridge:
            m = {Box(0, 0, 0, 0.7F, 1.9F, 0.65F, kMetal),
                 Box(0.28F, 0.9F, 0.33F, 0.03F, 0.5F, 0.03F, kDarkMetal)};
            break;
        case PropKind::DiningTable:
            m = {Box(0, 0.72F, 0, 1.2F, 0.05F, 0.8F, kWood)};
            Legs(m, 1.1F, 0.7F, 0.72F, kDarkWood);
            break;
        case PropKind::Chair:
            m = {Tinted(0, 0.44F, 0, 0.45F, 0.06F, 0.45F),
                 Tinted(0, 0.5F, -0.2F, 0.45F, 0.45F, 0.05F)};
            Legs(m, 0.42F, 0.42F, 0.44F, kDarkMetal, 0.04F);
            break;
        case PropKind::Toilet:
            m = {Box(0, 0, 0, 0.4F, 0.42F, 0.55F, kWhite),
                 Box(0, 0.42F, -0.22F, 0.4F, 0.35F, 0.15F, kWhite)};
            break;
        case PropKind::Sink:
            m = {Box(0, 0, 0, 0.6F, 0.82F, 0.45F, kWhite),
                 Box(0, 0.82F, 0.02F, 0.5F, 0.06F, 0.38F, kTile),
                 Box(0, 1.1F, -0.21F, 0.6F, 0.7F, 0.02F, kGlass)};
            break;
        case PropKind::Shower:
            m = {Box(0, 0, 0, 0.9F, 0.08F, 0.9F, kTile),
                 Glass(0, 0.08F, 0.44F, 0.9F, 2.0F, 0.02F),
                 Glass(0.44F, 0.08F, 0, 0.02F, 2.0F, 0.9F),
                 Box(0, 1.95F, -0.38F, 0.2F, 0.05F, 0.2F, kMetal)};
            break;
        case PropKind::Rug:
            m = {Tinted(0, 0, 0, 2.0F, 0.015F, 1.4F)};
            break;
        case PropKind::Plant:
            m = {Box(0, 0, 0, 0.35F, 0.35F, 0.35F, kPot),
                 Box(0, 0.35F, 0, 0.5F, 0.7F, 0.5F, kLeaf)};
            break;
        case PropKind::CeilingLamp:
            // Base at the bottom of the shade; the cord runs up to the ceiling.
            m = {Box(0, 0, 0, 0.45F, 0.1F, 0.45F, kLampShade),
                 Box(0, -0.02F, 0, 0.3F, 0.02F, 0.3F, kBulb),
                 Box(0, 0.1F, 0, 0.02F, 0.4F, 0.02F, kBlack)};
            break;
        case PropKind::StreetLamp:
            m = {Box(0, 0, 0, 0.3F, 0.3F, 0.3F, kDarkMetal),
                 Box(0, 0.3F, 0, 0.14F, 5.9F, 0.14F, kDarkMetal),
                 Box(0, 6.0F, 0.75F, 0.1F, 0.1F, 1.6F, kDarkMetal),
                 Box(0, 5.85F, 1.45F, 0.35F, 0.15F, 0.6F, kDarkMetal),
                 Box(0, 5.83F, 1.45F, 0.28F, 0.02F, 0.5F, kBulb)};
            break;
        case PropKind::BarCounter:
            m = {Box(0, 0, 0, 3.0F, 1.05F, 0.6F, kDarkWood),
                 Box(0, 1.05F, 0.05F, 3.1F, 0.05F, 0.75F, kDarkMetal),
                 Box(0, 0.2F, 0.31F, 2.9F, 0.04F, 0.02F, kNeonPink)};
            break;
        case PropKind::BottleShelf:
            m = {Box(0, 0, 0, 3.0F, 2.2F, 0.3F, kDarkWood),
                 Box(0, 1.2F, 0.2F, 3.0F, 0.03F, 0.3F, kMetal),
                 Box(0, 1.6F, 0.2F, 3.0F, 0.03F, 0.3F, kMetal)};
            for (int index = 0; index < 12; ++index) {
                const float x = -1.35F + (static_cast<float>(index % 6) * 0.54F);
                const float y = index < 6 ? 1.23F : 1.63F;
                m.push_back(Box(x, y, 0.22F, 0.1F, 0.3F, 0.1F,
                                kGoods.at(static_cast<std::size_t>(index) %
                                          kGoods.size())));
            }
            break;
        case PropKind::Stool:
            m = {Box(0, 0, 0, 0.35F, 0.03F, 0.35F, kDarkMetal),
                 Box(0, 0, 0, 0.08F, 0.7F, 0.08F, kMetal),
                 Tinted(0, 0.7F, 0, 0.38F, 0.06F, 0.38F)};
            break;
        case PropKind::BarTable:
            m = {Box(0, 0, 0, 0.1F, 1.0F, 0.1F, kMetal),
                 Box(0, 1.0F, 0, 0.7F, 0.04F, 0.7F, kDarkWood)};
            break;
        case PropKind::Booth:
            m = {Tinted(0, 0, -0.75F, 1.4F, 0.45F, 0.5F),
                 Tinted(0, 0.45F, -0.95F, 1.4F, 0.6F, 0.1F),
                 Tinted(0, 0, 0.75F, 1.4F, 0.45F, 0.5F),
                 Tinted(0, 0.45F, 0.95F, 1.4F, 0.6F, 0.1F),
                 Box(0, 0, 0, 0.12F, 0.72F, 0.12F, kMetal),
                 Box(0, 0.72F, 0, 1.2F, 0.05F, 0.7F, kWood)};
            break;
        case PropKind::ChargingPod:
            // An open charging post: the robot stands on the plate, backed
            // up to the column, with a low rail on each side.
            m = {Box(0, 0, 0, 0.9F, 0.1F, 0.9F, kDarkMetal),
                 Box(0, 0, -0.4F, 0.3F, 1.9F, 0.1F, kMetal),
                 Box(0, 1.0F, -0.34F, 0.12F, 0.6F, 0.03F, kGreen),
                 Box(0, 1.9F, -0.4F, 0.36F, 0.06F, 0.16F, kGreen),
                 Box(-0.42F, 0, -0.1F, 0.06F, 0.9F, 0.6F, kMetal),
                 Box(0.42F, 0, -0.1F, 0.06F, 0.9F, 0.6F, kMetal)};
            break;
        case PropKind::RepairCradle:
            m = {Box(0, 0, 0, 0.9F, 0.72F, 2.1F, kWhite),
                 Box(0, 0.72F, 0, 0.8F, 0.06F, 2.0F, kTile),
                 Box(0.75F, 0, -0.8F, 0.16F, 2.3F, 0.16F, kMetal),
                 Box(0.4F, 2.2F, -0.8F, 0.7F, 0.1F, 0.1F, kMetal),
                 Box(0.05F, 1.7F, -0.8F, 0.08F, 0.5F, 0.08F, kMetal),
                 Box(0.05F, 1.6F, -0.8F, 0.25F, 0.12F, 0.25F, kNeonCyan),
                 Box(-0.75F, 0, 0.8F, 0.4F, 1.3F, 0.3F, kDarkMetal),
                 Box(-0.75F, 1.0F, 0.96F, 0.36F, 0.25F, 0.02F, kGreen)};
            break;
        case PropKind::MarketStall:
            m = {Box(0, 0.85F, 0, 2.0F, 0.05F, 0.8F, kDarkWood),
                 Box(-0.4F, 0.9F, 0, 0.5F, 0.25F, 0.5F, kGoods.at(0)),
                 Box(0.2F, 0.9F, 0.1F, 0.4F, 0.15F, 0.4F, kGoods.at(2)),
                 Box(0.7F, 0.9F, -0.1F, 0.3F, 0.35F, 0.3F, kDarkMetal),
                 Box(0, 0.3F, 0.41F, 1.9F, 0.04F, 0.02F, kNeonPink)};
            Legs(m, 2.0F, 0.8F, 0.85F, kDarkWood, 0.06F);
            break;
        case PropKind::Crate:
            m = {Box(0, 0, 0, 0.7F, 0.6F, 0.7F, kWood),
                 Box(0, 0.6F, 0, 0.72F, 0.04F, 0.72F, kDarkWood)};
            break;
        case PropKind::Workbench:
            m = {Box(0, 0.9F, 0, 2.0F, 0.06F, 0.8F, kWood),
                 Box(0.7F, 0.96F, 0.25F, 0.2F, 0.15F, 0.2F, kDarkMetal),
                 Box(0, 0.96F, -0.38F, 2.0F, 1.0F, 0.04F, kDarkMetal),
                 Box(-0.5F, 1.4F, -0.34F, 0.08F, 0.35F, 0.03F, kYellow),
                 Box(-0.2F, 1.5F, -0.34F, 0.3F, 0.06F, 0.03F, kMetal),
                 Box(0.3F, 1.35F, -0.34F, 0.06F, 0.4F, 0.03F, kNeonPink)};
            Legs(m, 1.9F, 0.7F, 0.9F, kDarkMetal, 0.08F);
            break;
        case PropKind::CarLift:
            m = {Box(-0.8F, 0, 0, 0.3F, 0.1F, 2.4F, kDarkMetal),
                 Box(0.8F, 0, 0, 0.3F, 0.1F, 2.4F, kDarkMetal),
                 Box(-1.1F, 0, 0, 0.2F, 2.2F, 0.2F, kYellow),
                 Box(1.1F, 0, 0, 0.2F, 2.2F, 0.2F, kYellow),
                 Box(0, 1.2F, 0, 2.2F, 0.12F, 0.2F, kYellow)};
            break;
        case PropKind::PartsShelf:
        case PropKind::GoodsShelf: {
            const bool goods = kind == PropKind::GoodsShelf;
            const float width = goods ? 2.4F : 2.0F;
            m = {Box(0, 0, -0.2F, width, 1.8F, 0.06F,
                     goods ? kWhite : kDarkMetal)};
            for (int level = 0; level < 4; ++level) {
                const float y = 0.1F + (static_cast<float>(level) * 0.45F);
                m.push_back(Box(0, y, 0, width, 0.03F, 0.45F, kMetal));
                for (int slot = 0; slot < 5; ++slot) {
                    const float x = (-width * 0.4F) +
                                    (static_cast<float>(slot) * width * 0.2F);
                    m.push_back(Box(
                        x, y + 0.03F, 0, 0.25F, 0.22F + (0.04F * slot), 0.3F,
                        kGoods.at(static_cast<std::size_t>(level + slot) %
                                  kGoods.size())));
                }
            }
            break;
        }
        case PropKind::ArcadeCabinet:
            m = {Tinted(0, 0, 0, 0.7F, 1.8F, 0.75F),
                 Box(0, 1.15F, 0.38F, 0.55F, 0.45F, 0.02F, kScreen),
                 Box(0, 0.95F, 0.45F, 0.7F, 0.12F, 0.3F, kBlack),
                 Box(0, 1.65F, 0.39F, 0.7F, 0.15F, 0.04F, kNeonPink)};
            break;
        case PropKind::Checkout:
            m = {Box(0, 0, 0, 1.6F, 1.0F, 0.7F, kWhite),
                 Box(0.4F, 1.0F, 0, 0.35F, 0.25F, 0.3F, kBlack),
                 Box(0.4F, 1.25F, -0.05F, 0.3F, 0.2F, 0.03F, kScreen)};
            break;
        case PropKind::Furnace:
            m = {Box(0, 0, 0, 2.0F, 2.4F, 2.0F, kDarkMetal),
                 Box(0, 0.6F, 1.0F, 1.0F, 0.7F, 0.02F, kGlow),
                 Box(0, 2.4F, -0.5F, 0.6F, 1.2F, 0.6F, kDarkMetal)};
            break;
        case PropKind::Conveyor:
            m = {Box(0, 0.8F, 0, 4.0F, 0.1F, 0.8F, kBlack),
                 Box(0, 0.75F, 0.42F, 4.0F, 0.12F, 0.04F, kYellow),
                 Box(0, 0.75F, -0.42F, 4.0F, 0.12F, 0.04F, kYellow),
                 Box(-1.2F, 0.9F, 0, 0.4F, 0.25F, 0.4F, kMetal),
                 Box(0.6F, 0.9F, 0, 0.4F, 0.25F, 0.4F, kMetal)};
            Legs(m, 3.8F, 0.7F, 0.75F, kDarkMetal, 0.08F);
            break;
        case PropKind::RobotArm:
            m = {Box(0, 0, 0, 0.6F, 0.4F, 0.6F, kYellow),
                 Box(0, 0.4F, 0, 0.2F, 1.0F, 0.2F, kYellow),
                 PropPart{.pivot = {0, 1.4F, 0},
                          .size = {0.15F, 0.9F, 0.15F},
                          .angleX = 55.0F,
                          .color = kYellow},
                 Box(0, 1.4F, 0, 0.3F, 0.3F, 0.3F, kDarkMetal)};
            break;
        case PropKind::Desk:
            m = {Box(0, 0.74F, 0, 1.6F, 0.04F, 0.8F, kWhite),
                 Box(0, 0.78F, -0.25F, 0.6F, 0.38F, 0.04F, kBlack),
                 Box(0, 0.8F, -0.228F, 0.56F, 0.34F, 0.005F, kScreen),
                 Box(0, 0.78F, 0.05F, 0.45F, 0.02F, 0.15F, kDarkMetal)};
            Legs(m, 1.5F, 0.7F, 0.74F, kDarkMetal);
            break;
        case PropKind::ServerRack:
            m = {Box(0, 0, 0, 0.6F, 2.0F, 1.0F, kBlack)};
            for (int row = 0; row < 8; ++row) {
                m.push_back(Box(0, 0.2F + (static_cast<float>(row) * 0.22F),
                                0.501F, 0.5F, 0.03F, 0.005F,
                                row % 3 == 0 ? kNeonCyan : kGreen));
            }
            break;
        case PropKind::PrepTable:
            m = {Box(0, 0.9F, 0, 2.0F, 0.05F, 0.8F, kMetal),
                 Box(0, 0.25F, 0, 1.9F, 0.03F, 0.7F, kMetal)};
            Legs(m, 1.9F, 0.7F, 0.9F, kMetal);
            break;
        case PropKind::Stairs:
            // One straight flight of 16 steps up one floor (3.2 m), 1.2 m
            // wide, climbing towards -z from z = 2.4 to z = -2.4.
            for (int step = 0; step < 16; ++step) {
                const auto index = static_cast<float>(step);
                m.push_back(Box(0, 0, 2.25F - (index * 0.3F), 1.2F,
                                0.2F * (index + 1.0F), 0.3F, kTile));
            }
            m.push_back(Box(0.62F, 0.9F, 0, 0.05F, 3.2F, 4.8F, kDarkMetal));
            break;
        case PropKind::ElevatorDoors:
            m = {Box(0, 0, 0, 1.6F, 2.4F, 0.15F, kDarkMetal),
                 Box(-0.37F, 0.05F, 0.08F, 0.72F, 2.2F, 0.02F, kMetal),
                 Box(0.37F, 0.05F, 0.08F, 0.72F, 2.2F, 0.02F, kMetal),
                 Box(0, 2.45F, 0.08F, 0.3F, 0.1F, 0.02F, kNeonCyan)};
            break;
        case PropKind::Door:
            // An open door leaf, hinged at x = 0 and swung against the wall.
            m = {Tinted(0.45F, 0, 0, 0.9F, 2.1F, 0.05F),
                 Box(0.8F, 1.0F, 0.04F, 0.08F, 0.03F, 0.06F, kMetal)};
            break;
    }
    return m;
}
}  // namespace

const PropModel& Prop(PropKind kind) {
    static std::map<PropKind, PropModel> cache;
    auto found = cache.find(kind);
    if (found == cache.end()) {
        found = cache.emplace(kind, Make(kind)).first;
    }
    return found->second;
}

PropModel RobotFigure(Pose pose, float phase, vec3 accent) {
    const float swing = pose == Pose::Walking ? std::sin(phase) * 28.0F : 0.0F;
    PropModel m;
    const auto limb = [](vec3 pivot, float length, float thickness,
                         float angle, vec3 color) {
        return PropPart{.pivot = pivot,
                        .base = {0.0F, -length, 0.0F},
                        .size = {thickness, length, thickness},
                        .angleX = angle,
                        .color = color};
    };
    if (pose == Pose::Sitting) {
        // Hips on a 0.45 m seat, thighs forward, shins down.
        constexpr float kHip = 0.45F;
        m.push_back(Box(0, kHip, 0, 0.46F, 0.62F, 0.28F, accent));
        for (const float side : {-0.11F, 0.11F}) {
            m.push_back(limb({side, kHip + 0.08F, 0}, 0.45F, 0.17F, -90.0F,
                             kDarkMetal));
            m.push_back(Box(side, 0, 0.4F, 0.15F, kHip, 0.15F, kDarkMetal));
            m.push_back(limb({side * 2.7F, kHip + 0.6F, 0}, 0.6F, 0.11F,
                             -40.0F, kChrome));
        }
        m.push_back(Box(0, kHip + 0.65F, 0, 0.3F, 0.28F, 0.3F, kChrome));
        m.push_back(Box(0, kHip + 0.73F, 0.15F, 0.24F, 0.07F, 0.03F, kNeonCyan));
        return m;
    }
    // Standing, walking, and lying (the standing figure tipped onto its
    // back by whoever places it).
    m.push_back(Box(0, 0.85F, 0, 0.46F, 0.62F, 0.28F, accent));
    for (const float side : {-0.11F, 0.11F}) {
        const float legSwing = side < 0.0F ? swing : -swing;
        m.push_back(limb({side, 0.85F, 0}, 0.85F, 0.17F, legSwing, kDarkMetal));
        m.push_back(
            limb({side * 2.7F, 1.45F, 0}, 0.62F, 0.11F, -legSwing, kChrome));
    }
    m.push_back(Box(0, 1.5F, 0, 0.3F, 0.28F, 0.3F, kChrome));
    m.push_back(Box(0, 1.58F, 0.15F, 0.24F, 0.07F, 0.03F, kNeonCyan));
    return m;
}

}  // namespace mechapunk
