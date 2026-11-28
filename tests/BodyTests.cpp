#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <utility>

#include "mechapunk/sim/Body.hpp"
#include "mechapunk/sim/Save.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

// A part's condition in this robot's body, by which part it is.
int Condition(const sim::Robot& robot, sim::BodyPart part) {
    return sim::Part(robot.body, part).condition;
}

TEST(Body, CapBodyCapsHeadAndTorsoOnly) {
    sim::Robot robot;
    robot.wear = 400;
    for (sim::PartState& part : robot.body.parts) {
        part.condition = 1000;
    }
    // The head is worse than the cap allows, the torso already better: the
    // cap holds neither of them up.
    sim::Part(robot.body, sim::BodyPart::Head).condition = 800;
    sim::Part(robot.body, sim::BodyPart::Torso).condition = 500;
    sim::Part(robot.body, sim::BodyPart::Legs).lost = true;
    robot.body.dominantHand = sim::BodyPart::LeftHand;

    sim::CapBody(robot);

    EXPECT_EQ(Condition(robot, sim::BodyPart::Head), 600);
    EXPECT_EQ(Condition(robot, sim::BodyPart::Torso), 500);
    // Hands and legs can be lost and replaced, so wear does not cap them.
    EXPECT_EQ(Condition(robot, sim::BodyPart::LeftHand), 1000);
    EXPECT_EQ(Condition(robot, sim::BodyPart::RightHand), 1000);
    EXPECT_EQ(Condition(robot, sim::BodyPart::Legs), 1000);
    // The cap touches nothing else about the body.
    EXPECT_TRUE(sim::Part(robot.body, sim::BodyPart::Legs).lost);
    EXPECT_EQ(robot.body.dominantHand, sim::BodyPart::LeftHand);
}

TEST(Body, WornOutRobotHasDestroyedHeadAndTorso) {
    sim::Robot robot;
    robot.wear = sim::kMaxWear;
    sim::CapBody(robot);
    EXPECT_EQ(Condition(robot, sim::BodyPart::Head), 0);
    EXPECT_EQ(Condition(robot, sim::BodyPart::Torso), 0);
    EXPECT_EQ(Condition(robot, sim::BodyPart::Legs), 1000);
}

// A clinic that takes lasting damage away gives the head and the torso back
// exactly as much condition as wear it took, never above the cap; a part
// damaged beyond what the wear explains is mended only by that much, and a
// hand or a leg is not mended by wear at all (step L12 fits those).
TEST(Body, MendBodyFollowsTheWearItTook) {
    sim::Robot robot;
    for (sim::PartState& part : robot.body.parts) {
        part.condition = 1000;
    }
    robot.wear = 600;
    sim::CapBody(robot);
    EXPECT_EQ(Condition(robot, sim::BodyPart::Head), 400);
    EXPECT_EQ(Condition(robot, sim::BodyPart::Torso), 400);
    // The clinic took 150 of the wear away: the parts follow the cap exactly.
    robot.wear = 450;
    sim::MendBody(robot, 150);
    EXPECT_EQ(Condition(robot, sim::BodyPart::Head), 550);
    EXPECT_EQ(Condition(robot, sim::BodyPart::Torso), 550);
    // A head cut down to 50 by something the clinic has not reached is mended
    // by the 20 it works, and stays below the cap.
    sim::Part(robot.body, sim::BodyPart::Head).condition = 50;
    robot.wear = 430;
    sim::MendBody(robot, 20);
    EXPECT_EQ(Condition(robot, sim::BodyPart::Head), 70);
    EXPECT_EQ(Condition(robot, sim::BodyPart::Torso), 570);
    EXPECT_EQ(Condition(robot, sim::BodyPart::Legs), 1000);
    EXPECT_EQ(Condition(robot, sim::BodyPart::LeftHand), 1000);
}

TEST(Body, MendBodyNeverGoesAboveTheCap) {
    sim::Robot robot;
    robot.wear = 300;
    // More wear than the robot has: the mend stops at the cap.
    sim::MendBody(robot, 900);
    EXPECT_EQ(Condition(robot, sim::BodyPart::Head), sim::kMaxWear - 300);
    EXPECT_EQ(Condition(robot, sim::BodyPart::Torso), sim::kMaxWear - 300);
    // Nothing the robot has left to take away mends nothing at all.
    sim::Part(robot.body, sim::BodyPart::Head).condition = 50;
    sim::MendBody(robot, 0);
    EXPECT_EQ(Condition(robot, sim::BodyPart::Head), 50);
}

TEST(Body, TheFivePartsAreEachTheirOwn) {
    sim::Body body;
    for (const sim::BodyPart part :
         {sim::BodyPart::Head, sim::BodyPart::Torso, sim::BodyPart::LeftHand,
          sim::BodyPart::RightHand, sim::BodyPart::Legs}) {
        sim::Part(body, part).condition =
            1 + static_cast<int>(static_cast<std::uint8_t>(part));
    }
    EXPECT_EQ(sim::Part(body, sim::BodyPart::Head).condition, 1);
    EXPECT_EQ(sim::Part(body, sim::BodyPart::Torso).condition, 2);
    EXPECT_EQ(sim::Part(body, sim::BodyPart::LeftHand).condition, 3);
    EXPECT_EQ(sim::Part(body, sim::BodyPart::RightHand).condition, 4);
    EXPECT_EQ(sim::Part(body, sim::BodyPart::Legs).condition, 5);
}

TEST(Body, NewCityRobotsHaveCappedBodies) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 60});
    std::uint32_t leftHanded = 0;
    for (const sim::Robot& robot : world.State().robots) {
        // New, but no better than its age's wear allows.
        EXPECT_EQ(Condition(robot, sim::BodyPart::Head),
                  sim::kMaxWear - robot.wear)
            << robot.name;
        EXPECT_EQ(Condition(robot, sim::BodyPart::Torso),
                  sim::kMaxWear - robot.wear)
            << robot.name;
        for (const sim::BodyPart part :
             {sim::BodyPart::LeftHand, sim::BodyPart::RightHand,
              sim::BodyPart::Legs}) {
            EXPECT_EQ(Condition(robot, part), 1000) << robot.name;
            EXPECT_FALSE(sim::Part(robot.body, part).lost) << robot.name;
        }
        // The hand it works with follows its id, so no random draw moved.
        const bool left = robot.id.value % 10 == 0;
        EXPECT_EQ(robot.body.dominantHand,
                  left ? sim::BodyPart::LeftHand : sim::BodyPart::RightHand)
            << robot.name;
        leftHanded += left ? 1U : 0U;
    }
    EXPECT_GT(leftHanded, 0U);
}

TEST(Body, BodySurvivesASaveRoundTrip) {
    sim::WorldState state =
        sim::World::Generate({.seed = 5, .robots = 20}).State();
    ASSERT_GT(state.robots.size(), 1U);
    // Wear a robot down in a way a new city never starts with, so the round
    // trip proves the numbers and the flags travel, not just the defaults.
    sim::Robot& worn = state.robots.front();
    // A new city already pins the head and the torso to `kMaxWear - wear`
    // (step L3a), and step L2b6 refuses a save whose basic parts sit above that
    // cap, so every number below stays under it. This robot is 136 days old,
    // so its wear is `WearAtAge(136)` = 302 and the cap 698: the cap is read
    // from the robot, not trusted from that number, and the test says so if a
    // future change to generation makes the robot too worn for these values.
    const std::int32_t cap = sim::kMaxWear - worn.wear;
    ASSERT_GT(cap, 400)
        << "the front robot's wear leaves no room under the cap";
    worn.body.parts[0].condition = 300;  // the head, worn and under the cap
    worn.body.parts[1].condition = 0;    // the torso, destroyed, not scrapped
    worn.body.parts[2].lost = true;      // a hand this robot has lost
    worn.body.parts[2].condition = 0;    // a lost part has no condition
    worn.body.parts[4].condition = 300;  // legs worn; hands and legs uncapped
    worn.body.dominantHand = sim::BodyPart::LeftHand;

    auto encoded = sim::SerializeWorld(state);
    ASSERT_TRUE(encoded.has_value()) << encoded.error();
    const std::string json = *encoded;
    auto loaded = sim::DeserializeWorld(json);
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    ASSERT_EQ(loaded->robots.size(), state.robots.size());
    for (std::size_t index = 0; index < state.robots.size(); ++index) {
        EXPECT_EQ(loaded->robots[index].body, state.robots[index].body)
            << index;
    }
    EXPECT_TRUE(loaded->robots.front().body.parts[2].lost);
    // The part names are spelled out in the save, not numbered.
    EXPECT_NE(json.find("\"leftHand\""), std::string::npos);

    // The new keys do not keep a world from loading or validating.
    auto world = sim::World::FromState(std::move(*loaded));
    ASSERT_TRUE(world.has_value()) << world.error();
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}
