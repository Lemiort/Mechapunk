// A save that breaks one of the body's invariants is refused by a load that
// names the rule it broke: the invariants of step L2a and the wear cap of
// step L2b. The save is the game's own, with one number or one flag in one
// robot's body changed.

#include <gtest/gtest.h>

#include <expected>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include <glaze/glaze.hpp>

#include "mechapunk/sim/Save.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {

// The save of a new town, with `edit` applied to the body of robot `robot`
// and nothing else touched, so a refusal can only be about that body.
std::string EditBody(const sim::World& world, std::size_t robot,
                     std::function<void(sim::SaveDocument&)> edit) {
    auto json = sim::SerializeWorld(world.State());
    EXPECT_TRUE(json.has_value());
    sim::SaveDocument document;
    EXPECT_FALSE(glz::read_json(document, json.value_or("")));
    edit(document["world"]["robots"][std::uint64_t{robot}]["body"]);
    return glz::write_json(document).value_or("");
}

// Reads such a save back: the load must fail, and say which rule broke.
void RejectsBody(const std::string& json, std::string_view reason) {
    auto state = sim::DeserializeWorld(json);
    ASSERT_TRUE(state.has_value()) << state.error();
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_FALSE(world);
    EXPECT_NE(world.error().find(reason), std::string::npos)
        << world.error();
}

// Reads such a save back and expects it: a rule must not refuse a state the
// game itself writes, so the boundary of a rule is checked on both sides.
void AcceptsBody(const std::string& json) {
    auto state = sim::DeserializeWorld(json);
    ASSERT_TRUE(state.has_value()) << state.error();
    auto world = sim::World::FromState(std::move(*state));
    if (!world) {
        ADD_FAILURE() << world.error();
    }
}

}  // namespace

// A part's condition runs from destroyed to whole and no further, in either
// direction.
TEST(Validation, RejectsAPartOutOfConditionRange) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 60});
    RejectsBody(EditBody(world, 0, [](sim::SaveDocument& body) {
        body["parts"][4]["condition"] = std::int64_t{1200};
    }), "legs in condition 1200, outside 0 to 1000");
    RejectsBody(EditBody(world, 0, [](sim::SaveDocument& body) {
        body["parts"][2]["condition"] = std::int64_t{-1};
    }), "leftHand in condition -1, outside 0 to 1000");
}

// A part that is gone has no condition left: a save that says both is a
// contradiction.
TEST(Validation, RejectsALostPartThatStillHasCondition) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 60});
    RejectsBody(EditBody(world, 0, [](sim::SaveDocument& body) {
        body["parts"][2]["lost"] = true;
        body["parts"][2]["condition"] = std::int64_t{600};
    }), "lost its leftHand, which still has condition 600");
}

// The head and the torso are never lost, because a robot that lost one is
// scrapped instead of saved; the replaceable parts are checked by the rule
// above, so each of these two says which part it means.
TEST(Validation, RejectsALostHeadOrTorso) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 60});
    RejectsBody(EditBody(world, 0, [](sim::SaveDocument& body) {
        body["parts"][0]["lost"] = true;
        body["parts"][0]["condition"] = std::int64_t{0};
    }), "lost its head, which a robot cannot live without");
    RejectsBody(EditBody(world, 0, [](sim::SaveDocument& body) {
        body["parts"][1]["lost"] = true;
        body["parts"][1]["condition"] = std::int64_t{0};
    }), "lost its torso, which a robot cannot live without");
}

// The hand a robot works with is one of its two hands: a save naming another
// part as its working hand is refused.
TEST(Validation, RejectsAnImpossibleDominantHand) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 60});
    RejectsBody(EditBody(world, 0, [](sim::SaveDocument& body) {
        body["dominantHand"] = "legs";
    }), "works with a hand that is neither its left nor its right");
}

// Step L2b: the head and the torso are never in better condition than the
// robot's lasting damage allows, `max(0, kMaxWear - wear)`, which is what
// `CapBody` pins them to wherever wear grows (Body.hpp, step L3a). Hands and
// legs are not capped, because a clinic or the black market replaces them
// whole (step L12). A robot at no wear would sit at the whole cap, where one
// more condition is already outside 0 to 1000 and refused by the rule of
// step L2a, so the city's first robot with some wear is the one checked.
TEST(Validation, RejectsBasicPartsAboveTheWearCap) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 60});
    const auto state = world.State();
    std::size_t robot = 0;
    while (robot < state.robots.size() && state.robots[robot].wear == 0) {
        ++robot;
    }
    ASSERT_LT(robot, state.robots.size()) << "no robot of the city has wear";
    const auto wear = state.robots[robot].wear;
    const auto cap = sim::kMaxWear - wear;
    RejectsBody(EditBody(world, robot, [=](sim::SaveDocument& body) {
        body["parts"][0]["condition"] = std::int64_t{cap + 1};
    }), std::format("head in condition {}, above the {} its wear {} allows",
                    cap + 1, cap, wear));
    RejectsBody(EditBody(world, robot, [=](sim::SaveDocument& body) {
        body["parts"][1]["condition"] = std::int64_t{cap + 1};
    }), std::format("torso in condition {}, above the {} its wear {} allows",
                    cap + 1, cap, wear));
    // Both basic parts exactly at the cap load, which is where the game's own
    // state sits, and the hands and the legs are left as the new city has them
    // at 1000: whole, so above a cap that is below 1000 because this robot has
    // wear. That is what step L12's replaced parts look like, and the rule
    // must let them through.
    AcceptsBody(EditBody(world, robot, [=](sim::SaveDocument& body) {
        body["parts"][0]["condition"] = std::int64_t{cap};
        body["parts"][1]["condition"] = std::int64_t{cap};
    }));
}
