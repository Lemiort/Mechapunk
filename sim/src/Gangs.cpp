// Gangs: founding gangs during city generation (World::FoundGangs).

#include "mechapunk/sim/Gangs.hpp"
#include <algorithm>
#include <cstdint>
#include <set>
#include <string_view>
#include <utility>
#include <vector>
#include "mechapunk/sim/Random.hpp"
#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {
namespace {

constexpr std::size_t kGangNames = 12;

std::string_view kGangName[kGangNames] = {
    "Chrome Jackals",   "Rust Saints",        "Neon Vipers",
    "Silicon Reapers",  "Plasma Wolves",      "Carbon Kings",
    "Ferro Hawks",      "Oxidant Drifters",   "Voltage Syndicate",
    "Bio Wire",         "Synth Punks",        "Data Ghosts",
};

constexpr std::uint32_t kGangColor[kGangNames] = {
    0xFF1493, 0x00CED1, 0xADFF2F, 0xFF69B4, 0x7B68EE, 0xFFD700,
    0x9ACD32, 0xDAA520, 0xBA55DC, 0x00FA9A, 0xDC143C, 0x48D1CC,
};

// --- Gang helpers used by ConsiderCrime ---

const Gang* FindGangOf(const std::vector<Gang>& gangs, const Robot& robot) noexcept {
    for (const Gang& g : gangs) {
        if (g.id == robot.gang) return &g;
    }
    return nullptr;
}

Stance StanceOfGang(const Gang& from, EntityId toGang) noexcept {
    for (const auto& rel : from.relations) {
        if (rel.gang == toGang) return rel.stance;
    }
    return Stance::Neutral;  // Should not happen.
}

bool IsRivalGangs(const Gang& a, const Gang& b) noexcept {
    return (StanceOfGang(a, b.id) == Stance::Rival ||
            StanceOfGang(b, a.id) == Stance::Rival);
}

// Find the Government organization (City Hall) in an economy. Always exists.
const Organization& FindGovernment(const std::vector<Organization>& organizations) noexcept {
    for (const Organization& org : organizations) {
        if (org.kind == OrganizationKind::Government) return org;
    }
    // Should never happen: City Hall always exists in a valid economy.
    return organizations[0];
}

}  // namespace

// Handle officer+member affinity: both bonds drop by 20 when an officer on
// shift talks with a gang member. Applies to each talking pair (first, second).
void World::HandleOfficerAffinity(Robot& first, Robot& second) {
    // Check if either of the pair is an officer on shift in this room.
    bool firstIsOfficer = false;
    auto workRefFirst = rooms_.Find(first.workspace);
    if (first.gang.value && workRefFirst &&
        workRefFirst->room->kind == RoomKind::PoliceStation &&
        (first.activity == RobotActivity::Working || first.patrol.value)) {
        firstIsOfficer = true;
    }

    bool secondIsOfficer = false;
    auto workRefSecond = rooms_.Find(second.workspace);
    if (second.gang.value && workRefSecond &&
        workRefSecond->room->kind == RoomKind::PoliceStation &&
        (second.activity == RobotActivity::Working || second.patrol.value)) {
        secondIsOfficer = true;
    }

    // Both are officers on shift talking: no affinity change.
    if (firstIsOfficer && secondIsOfficer) return;

    // One is an officer, the other should be a gang member (not an officer).
    bool firstIsGangMember = (first.gang.value != 0) && !firstIsOfficer;
    bool secondIsGangMember = (second.gang.value != 0) && !secondIsOfficer;

    if ((firstIsOfficer && secondIsGangMember) ||
        (secondIsOfficer && firstIsGangMember)) {
        // Both bonds drop by 20.
        ChangeBond(first, second.id, -20);
        ChangeBond(second, first.id, -20);
    }
}

void World::FoundGangs(Pcg32& random) {
    std::uint32_t numRobots = static_cast<std::uint32_t>(state_.robots.size());
    std::uint32_t numGangs = std::max(2u, std::min(6u, numRobots / 1500));

    // Collect habitat buildings and find those nearest the city edge.
    std::vector<EntityId> habitatBuildings;
    std::int32_t minXNode = INT32_MAX, maxXNode = INT32_MIN;
    std::int32_t minZNode = INT32_MAX, maxZNode = INT32_MIN;

    auto findBuilding = [&](EntityId id) -> const Building* {
        for (const Building& b : state_.city.buildings) {
            if (b.id == id) return &b;
        }
        return nullptr;
    };

    // Compute bounding box of all nodes.
    for (const CityNode& node : state_.city.nodes) {
        minXNode = std::min(minXNode, node.x);
        maxXNode = std::max(maxXNode, node.x);
        minZNode = std::min(minZNode, node.z);
        maxZNode = std::max(maxZNode, node.z);
    }

    for (const Building& building : state_.city.buildings) {
        if (building.kind != BuildingKind::Habitat) continue;
        habitatBuildings.push_back(building.id);
        const CityNode& node = state_.city.nodes[static_cast<std::size_t>(building.entrance.value)];
        minXNode = std::min(minXNode, node.x);
        maxXNode = std::max(maxXNode, node.x);
        minZNode = std::min(minZNode, node.z);
        maxZNode = std::max(maxZNode, node.z);
    }

    // Compute centroid and sort habitats by distance from center (farthest first).
    const std::int32_t cx = (minXNode + maxXNode) / 2;
    const std::int32_t cz = (minZNode + maxZNode) / 2;

    auto distSq = [&](EntityId buildingId) -> std::uint64_t {
        const Building* b = findBuilding(buildingId);
        if (!b) return 0;
        const CityNode& n = state_.city.nodes[static_cast<std::size_t>(b->entrance.value)];
        return static_cast<std::uint64_t>((n.x - cx) * (n.x - cx) + (n.z - cz) * (n.z - cz));
    };

    std::sort(habitatBuildings.begin(), habitatBuildings.end(), [&](EntityId a, EntityId b) {
        return distSq(a) > distSq(b);  // farthest first
    });

    // Create gangs.
    state_.gangs.reserve(numGangs);
    for (std::uint32_t i = 0; i < numGangs; ++i) {
        Gang gang;
        gang.id = static_cast<EntityId>(state_.ids.Allocate());
        gang.name = kGangName[i % kGangNames];
        gang.color = kGangColor[i % kGangNames];
        if (i < habitatBuildings.size()) {
            gang.turf = habitatBuildings[static_cast<std::size_t>(i)];
        } else {
            gang.turf = EntityId{0};  // no turf if not enough habitats.
        }

        state_.gangs.push_back(std::move(gang));
    }

    // Assign symmetric relations between all pairs, store in a flat map.
    std::vector<Stance> stancePairs;
    for (std::size_t i = 0; i < state_.gangs.size(); ++i) {
        for (std::size_t j = static_cast<std::size_t>(i + 1); j < state_.gangs.size(); ++j) {
            Stance stance;

            // Force at least one rival pair.
            if (i == 0 && j == static_cast<std::size_t>(1)) {
                stance = Stance::Rival;
            } else {
                std::uint32_t roll = static_cast<std::uint32_t>(random.UniformInt(0, 99));
                if (roll < 25) {
                    stance = Stance::Friendly;
                } else if (roll < 70) {
                    stance = Stance::Neutral;
                } else {
                    stance = Stance::Rival;
                }
            }
            stancePairs.push_back(stance);
        }
    }

    // Build relations for each gang, looking up pair stances symmetrically.
    // Index of pair (i,j), i < j, of n gangs: unique across all pairs.
    auto findStanceIdx = [&](std::size_t i, std::size_t j) -> std::size_t {
        if (i > j) std::swap(i, j);
        const std::size_t n = state_.gangs.size();
        return i * (2 * n - i - 1) / 2 + (j - i - 1);
    };

    for (std::size_t i = 0; i < state_.gangs.size(); ++i) {
        Gang& gang = state_.gangs[static_cast<std::size_t>(i)];
        for (std::size_t j = 0; j < state_.gangs.size(); ++j) {
            if (j == i) continue;
            Stance stance = stancePairs[findStanceIdx(i, j)];
            GangRelation rel{EntityId{state_.gangs[static_cast<std::size_t>(j)].id.value}, stance};
            gang.relations.push_back(std::move(rel));
        }

        // Sort relations by gang id.
        std::sort(gang.relations.begin(), gang.relations.end(), [](const GangRelation& a, const GangRelation& b) {
            return a.gang.value < b.gang.value;
        });
    }
}

void World::ReviewGangs() {
    // Old saves (before v18) have no gangs but robots may reference them.
    // Initialise gangs from any existing gang ids in robots.
    if (state_.gangs.empty()) {
        for (const Robot& robot : state_.robots) {
            if (!robot.gang.value) continue;
            bool found = false;
            for (std::size_t gi = 0; gi < state_.gangs.size(); ++gi) {
                if (state_.gangs[gi].id == robot.gang) { found = true; break; }
            }
            if (!found) {
                Gang gang;
                gang.id = robot.gang;
                // Use a default name and colour.
                std::size_t idx = static_cast<std::uint32_t>(robot.gang.value) % kGangNames;
                gang.name = std::string(kGangName[idx]);
                gang.color = 0;
                state_.gangs.push_back(std::move(gang));
            }
        }
        // Sort gangs by id.
        std::sort(state_.gangs.begin(), state_.gangs.end(), [](const Gang& a, const Gang& b) {
            return a.id < b.id;
        });
    }

    for (Robot& robot : state_.robots) {
        if (!robot.gang.value) {
            // Check joining gangs.

            // Never join if the robot is an officer.
            bool isOfficer = false;
            if (robot.workspace.value) {
                auto roomRef = rooms_.Find(robot.workspace);
                if (roomRef && roomRef->room->kind == RoomKind::PoliceStation) {
                    isOfficer = true;
                }
            }

            // Never join if a close friend is an officer.
            bool hasCloseFriendOfficer = false;
            for (const Bond& bond : robot.bonds) {
                if (bond.level != BondLevel::CloseFriend || !bond.robot.value) continue;
                const Robot* other = FindRobot(bond.robot);
                if (!other) continue;
                bool otherIsOfficer = false;
                if (other->workspace.value) {
                    auto otherRef = rooms_.Find(other->workspace);
                    if (otherRef && otherRef->room->kind == RoomKind::PoliceStation) {
                        otherIsOfficer = true;
                    }
                }
                hasCloseFriendOfficer |= otherIsOfficer;
            }

            if (isOfficer || hasCloseFriendOfficer) continue;

            // Check join conditions: out of work, in debt, or broke.
            auto homeRef = rooms_.Find(robot.apartment);
            bool isDorm = (homeRef &&
                homeRef->room->apartmentClass == ApartmentClass::Dorm);

            // Out of work: no workspace assigned.
            bool shouldConsiderJoining = !robot.workspace.value;
            // In debt: has an outstanding loan or owes rent.
            const Loan* loan = LoanOf(robot.id);
            if (loan && loan->owed > 0) shouldConsiderJoining = true;
            if (robot.rentOwed > 0) shouldConsiderJoining = true;
            // Broke: cannot pay rent with credits and savings.
            if (!shouldConsiderJoining &&
                robot.credits + robot.savings < RentOf(robot)) {
                shouldConsiderJoining = true;
            }

            // Younger than 120 days and living in a dorm may only double the
            // chance when already eligible for another reason.
            if (!shouldConsiderJoining) continue;

            // Look for close friends who are gang members.
            EntityId friendGang{};
            for (const Bond& bond : robot.bonds) {
                if (bond.level != BondLevel::CloseFriend || !bond.robot.value) continue;
                const Robot* other = FindRobot(bond.robot);
                if (!other || !other->gang.value) continue;
                friendGang = other->gang;
                break;  // Found one close friend in a gang.
            }

            // Calculate joining chance: 2% base, doubled (4%) for young+in-dorm.
            std::uint32_t chance = shouldConsiderJoining && robot.age < 120 && isDorm ? 4 : 2;
            if (friendGang.value) {
                // Close friend in gang: multiply by 2 again → up to 8% or 16%.
                chance *= 2;
            }

            if (state_.random.UniformInt(1, 100) > chance) continue;

            // Find target gang: friend's or nearest turf.
            EntityId targetGang{};
            if (friendGang.value) {
                targetGang = friendGang;
            } else {
                // Nearest turf gang from the robot's home apartment.
                std::int64_t bestDistSq = INT64_MAX;
                for (std::size_t i = 0; i < state_.gangs.size(); ++i) {
                    const Gang& gang = state_.gangs[static_cast<std::size_t>(i)];
                    if (!gang.turf.value) continue;  // Skip gangs without turf.
                    // gang.turf is a Building id: look up any room in that building.
                    EntityId turfRoom{};
                    for (const Building& b : state_.city.buildings) {
                        if (b.id == gang.turf && !b.rooms.empty()) {
                            turfRoom = b.rooms.front().id;
                            break;
                        }
                    }
                    if (!turfRoom.value) continue;  // No rooms in this building.
                    std::uint32_t minutes = TravelMinutes(robot.apartment, turfRoom);
                    // Use travel minutes as distance proxy.
                    if (minutes < static_cast<std::uint32_t>(bestDistSq)) {
                        bestDistSq = minutes;
                        targetGang = gang.id;
                    }
                }
            }

            if (!targetGang.value) continue;  // No turf gangs available.

            robot.gang = targetGang;
            ++state_.today.joinedGangs;
            Emit({.tick = state_.tick,
                  .kind = EventKind::JoinedGang,
                  .robot = robot.id,
                  .room = robot.apartment});
            Remember(robot, MemoryKind::JoinedGang, {}, robot.apartment);
        } else {
            // Already in a gang — check leaving.

            // Always leave at 3 arrests.
            if (robot.arrests >= 3) {
                robot.gang = EntityId{0};
                ++state_.today.leftGangs;
                Emit({.tick = state_.tick,
                      .kind = EventKind::LeftGang,
                      .robot = robot.id,
                      .room = robot.apartment});
                Remember(robot, MemoryKind::LeftGang, {}, robot.apartment);
                continue;
            }

            // 2% chance when employed and not in debt.
            bool isEmployed = robot.workspace.value;
            const Loan* loan = LoanOf(robot.id);
            bool hasDebt = (loan && loan->owed > 0) || robot.rentOwed > 0;

            if (isEmployed && !hasDebt) {
                if (state_.random.UniformInt(1, 100) <= 2) {
                    robot.gang = EntityId{0};
                    ++state_.today.leftGangs;
                    Emit({.tick = state_.tick,
                          .kind = EventKind::LeftGang,
                          .robot = robot.id,
                          .room = robot.apartment});
                    Remember(robot, MemoryKind::LeftGang, {}, robot.apartment);
                }
            }
        }
    }

    // Officers who lost their job and joined gangs while unemployed — force them out.
    for (Robot& robot : state_.robots) {
        if (!robot.gang.value || !robot.workspace.value) continue;
        auto roomRef = rooms_.Find(robot.workspace);
        if (roomRef && roomRef->room->kind == RoomKind::PoliceStation) {
            // This robot is currently an officer but has a gang — force it out.
            robot.gang = EntityId{0};
            ++state_.today.leftGangs;
            Emit({.tick = state_.tick,
                  .kind = EventKind::LeftGang,
                  .robot = robot.id,
                  .room = robot.apartment});
            Remember(robot, MemoryKind::LeftGang, {}, robot.apartment);
        }
    }
}

void World::ConsiderCrime(Robot& first, Robot& second,
    const std::set<std::uint64_t>& officerRooms) {
    const Gang* gangA = FindGangOf(state_.gangs, first);
    const Gang* gangB = FindGangOf(state_.gangs, second);

    // Members of the same gang: no robbery, no fight.
    if (gangA && gangB && gangA->id == gangB->id) {
        return;
    }

    // --- Gang interactions: rival fights and officer+member affinity ---
    if (gangA && gangB) {
        bool isRival = IsRivalGangs(*gangA, *gangB);

        if (isRival) {
            // Members of rival gangs lose 30 affinity.
            ChangeBond(first, second.id, -30);
            ChangeBond(second, first.id, -30);

            // Fight at 1%: both gain wear 20.
            if (state_.random.UniformInt(1, 100) <= 1) {
                ++state_.today.fights;
                Emit({.tick = state_.tick,
                      .kind = EventKind::Fought,
                      .robot = first.id,
                      .room = first.location,
                      .otherRobot = second.id});

                first.wear += 20;
                second.wear += 20;
                // B4 said a fight also costs condition. B7 thought Robot has
                // no condition field: it lives in Robot.needs.condition, on
                // the 0..1000 needs scale, so a fight cuts it by 100.
                first.needs.condition = std::max(0, first.needs.condition - 100);
                second.needs.condition = std::max(0, second.needs.condition - 100);
            }
        }
    }

    // --- Robbery: night time (20:00–04:00) ---
    const uint32_t minute = MinuteOfDay(state_.tick);
    bool atNight = (minute >= 20 * kMinutesPerHour || minute < 4 * kMinutesPerHour);

    // Handle officer+member affinity for all pairs.
    HandleOfficerAffinity(first, second);

    if (!atNight) {
        return;
    }

    // Robbery roll: determine who would rob whom (3%).
    Robot* robber = nullptr;
    Robot* victim = nullptr;
    bool canRob = false;

    if (gangA && !gangB) {
        // first is member, second is not.
        robber = &first;
        victim = &second;
        canRob = true;
    } else if (!gangA && gangB) {
        // second is member, first is not.
        robber = &second;
        victim = &first;
        canRob = true;
    } else if (gangA && gangB) {
        // Both members — check stance: rob if not friendly.
        Stance stance = StanceOfGang(*gangA, gangB->id);
        if (stance != Stance::Friendly) {
            robber = &first;
            victim = &second;
            canRob = true;
        } else {
            // Check the other direction.
            stance = StanceOfGang(*gangB, gangA->id);
            if (stance != Stance::Friendly) {
                robber = &second;
                victim = &first;
                canRob = true;
            }
        }
    }

    if (!canRob || !robber || !victim) {
        return;
    }

    // Try the robbery roll (3%).
    std::uint32_t roll = state_.random.UniformInt(1, 100);

    // Would an officer on shift standing in this room stop this robbery?
    // `officerRooms` is built once per Socialize call (B7's speed fix), so
    // the lookup is a set contains instead of a walk over the population.
    const bool officerPresent = officerRooms.contains(first.location.value);

    if (!officerPresent) {
        // No officer: robbery proceeds.
        std::int64_t loot = std::min(victim->credits, 20 + victim->credits / 4);

        ++state_.today.robberies;
        state_.today.robbedCredits += loot;

        Pay(victim->id, robber->id, loot);

        Emit({.tick = state_.tick,
              .kind = EventKind::Robbed,
              .robot = victim->id,
              .room = victim->location,
              .credits = loot,
              .otherRobot = robber->id});

        Remember(*victim, MemoryKind::WasRobbed, robber->id, victim->location);
        Remember(*robber, MemoryKind::RobbedSomeone, victim->id, robber->location);
        ChangeBond(*victim, robber->id, -60);
    } else if (roll <= 3) {
        // Would have robbed but officer present: arrest instead.
        std::int64_t fine = std::min(robber->credits + robber->savings, static_cast<std::int64_t>(100));

        PayBill(*robber, FindGovernment(state_.economy.organizations).id, fine);

        ++state_.today.arrests;
        ++robber->arrests;

        Emit({.tick = state_.tick,
              .kind = EventKind::Arrested,
              .robot = robber->id,
              .room = robber->location});

        Remember(*robber, MemoryKind::Arrested, {}, robber->location);
    }
}

}  // namespace mechapunk::sim
