// A save that breaks one of the body's invariants is refused by a load that
// names the rule it broke (task L, step L2a). The save is the game's own, with
// one number or one flag in one robot's body changed.

#include <gtest/gtest.h>

#include <expected>
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

// Step L2b, not this one: the head and the torso are also never above
// `kMaxWear - wear`. A game's own save satisfies that since step L3a, but a
// state written by hand does not, so that rule arrives with the repairs to
// every state the tests script.
