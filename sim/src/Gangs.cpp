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

const Gang* FindGangById(const std::vector<Gang>& gangs, EntityId id) noexcept {
    for (const Gang& g : gangs) {
        if (g.id == id) return &g;
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

// Whether this robot remembers `kind` happening within the last hour: the
// window B4 gave the officer (it catches a robbery of the hour, no further
// back). Memories are oldest first, so the scan starts at the newest and stops
// as soon as a memory is older than the hour.
bool RememberedInLastHour(const Robot& robot, MemoryKind kind, Tick now) {
    const Tick cutoff = now > kMinutesPerHour ? now - kMinutesPerHour : 0;
    for (auto memory = robot.memories.rbegin(); memory != robot.memories.rend();
         ++memory) {
        if (memory->tick < cutoff) return false;
        if (memory->kind == kind) return true;
    }
    return false;
}

}  // namespace

// An officer is a robot whose job room is a police station. The three places
// that need to know (who may not join a gang, which rooms are watched, which
// talking pairs cool off) all go through these two.
bool World::WorksInPoliceStation(const Robot& robot) const {
    if (!robot.workspace.value) {
        return false;
    }
    const auto roomRef = rooms_.Find(robot.workspace);
    return roomRef && roomRef->room->kind == RoomKind::PoliceStation;
}

// On shift means standing its post: at its job, or out on a patrol leg.
bool World::OfficerOnShift(const Robot& robot) const {
    return WorksInPoliceStation(robot) &&
           (robot.activity == RobotActivity::Working || robot.patrol.IsValid());
}

// Handle officer+member affinity: both bonds drop by 20 when an officer on
// shift talks with a gang member. Applies to each talking pair (first, second).
void World::HandleOfficerAffinity(Robot& first, Robot& second) {
    // B4's "always rivals": one of the pair is an officer on shift, the other
    // a gang member, and both feelings drop. The officer is not asked whether
    // it belongs to a gang: ReviewGangs forbids that, so the old
    // `first.gang.value &&` in front of the officer test made this rule never
    // fire once. Officers both on shift name no gang member, so they fall out
    // of the same condition.
    const bool firstOfficer = OfficerOnShift(first);
    const bool secondOfficer = OfficerOnShift(second);

    if ((firstOfficer && second.gang.IsValid()) ||
        (secondOfficer && first.gang.IsValid())) {
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
        const std::uint64_t first = distSq(a);
        const std::uint64_t second = distSq(b);
        if (first != second) {
            return first > second;  // farthest first
        }
        // Two habitats the same distance from the middle: the smaller id
        // first. Distance alone leaves their order unspecified to std::sort,
        // which would leave which habitat a gang claims up to the machine
        // (rule 1: the same seed builds the same city everywhere).
        return a < b;
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
    // A world saved before gangs existed (version 17) has none: found them
    // on this first midnight, from the saved seed and kGangStream - the
    // stream a newly generated city uses too - so the same save grows the
    // same gangs however far its clock had run, and they arrive with the
    // turfs and the full set of relations a founded city's gangs have.
    // The code this replaces invented a gang per id the robots happened to
    // name: no turf, colour 0 and no relations, which left StanceOfGang
    // answering Neutral for every pair, `RivalsInTurf` forever empty and the
    // gang page's relations empty; it was also unreachable from a legal save,
    // since validation refuses a robot that names a missing gang.
    if (state_.gangs.empty()) {
        Pcg32 gangRandom = Pcg32::FromSeed(state_.seed, kGangStream);
        FoundGangs(gangRandom);
    }

    for (Robot& robot : state_.robots) {
        if (!robot.gang.value) {
            // Check joining gangs.

            // Never join if the robot is an officer, or a close friend of one
            // (B3: an officer is a robot whose job is a police station).
            if (WorksInPoliceStation(robot)) continue;

            bool hasCloseFriendOfficer = false;
            for (const Bond& bond : robot.bonds) {
                if (bond.level != BondLevel::CloseFriend || !bond.robot.value) continue;
                const Robot* other = FindRobot(bond.robot);
                if (!other) continue;
                hasCloseFriendOfficer |= WorksInPoliceStation(*other);
            }

            if (hasCloseFriendOfficer) continue;

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

    // A robot that works in a police station is an officer, and officers are
    // never members: force it out (B3).
    for (Robot& robot : state_.robots) {
        if (!robot.gang.value) continue;
        if (WorksInPoliceStation(robot)) {
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

// The whole of an arrest, so both paths that end in one cannot drift apart.
// No credit is taken: the fine (B4: credits plus savings, at most 100) is paid
// to City Hall through PayBill, which is why the money still adds up. The
// arrest names its officer (B15): the event puts the officer in `robot` and
// the robber in `otherRobot`, so the chronicle says who did it and whom;
// both sides remember - the robber `Arrested` naming the officer, the officer
// `ArrestedSomeone` naming the robber - which is what the personal cards show.
void World::Arrest(Robot& robber, Robot& officer) {
    const std::int64_t fine =
        std::min(robber.credits + robber.savings, static_cast<std::int64_t>(100));
    PayBill(robber, FindGovernment(state_.economy.organizations).id, fine);
    ++state_.today.arrests;
    ++robber.arrests;
    Emit({.tick = state_.tick,
          .kind = EventKind::Arrested,
          .robot = officer.id,
          .room = robber.location,
          .otherRobot = robber.id});
    Remember(robber, MemoryKind::Arrested, officer.id, robber.location);
    Remember(officer, MemoryKind::ArrestedSomeone, robber.id, robber.location);
}

// B4 asked for two ways an officer takes a robber and the code had one: the
// pair roll decides a robbery in the same ten minutes the watched room is
// built, so the officer has to stand there at that exact moment, and over 40
// days at 10000 robots that came out at 4 arrests to 1473 robberies - an
// officer visits one venue for a quarter of an hour once a shift hour, while
// the city has hundreds of venues, so a crime's room is watched about one
// hour in fifty. The other way B4 gave is the officer on patrol meeting a
// member who robbed in the last hour: the crime may have been in another room
// and its room may never be watched, but the criminal is standing there with
// the officer, so an arrest needs no luck about where the robbery was.
// `Socialize` calls this for every room it found watched: a member standing
// in it that remembers robbing in that hour is arrested, once an hour (the
// arrest's own memory is the guard), in id order and with no random draw, so
// the same room always gives the same arrests (rule 1).
void World::PatrolArrests(const std::vector<std::size_t>& present,
                         std::size_t officerIndex) {
    for (std::size_t index : present) {
        Robot& robot = state_.robots[index];
        // Officers are never members (ReviewGangs forbids it), so a member
        // standing in a room an officer stands in is a rival of that officer.
        if (!robot.gang.IsValid()) continue;
        if (!RememberedInLastHour(robot, MemoryKind::RobbedSomeone, state_.tick)) {
            continue;
        }
        if (RememberedInLastHour(robot, MemoryKind::Arrested, state_.tick)) continue;
        Arrest(robot, state_.robots[officerIndex]);
    }
}

void World::ConsiderCrime(Robot& first, Robot& second,
    const std::map<std::uint64_t, std::size_t>& officerInRoom) {
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

    // The pair roll (B4): only 3 percent of the pairs where a member could
    // take something try it. One roll per pair decides both what follows:
    // an officer on shift standing in this room (officerInRoom is built once
    // per Socialize call, B7's speed fix) turns the attempt into an arrest
    // by that officer instead of a robbery. Drawing the roll once is the
    // whole point: the first version tested it only on the arrest path, so
    // every eligible pair robbed and an officer held crime down rather than
    // changing it.
    const std::uint32_t roll = state_.random.UniformInt(1, 100);
    if (static_cast<std::int64_t>(roll) > state_.settings.crime.robberyPercent) {
        return;
    }

    if (auto watched = officerInRoom.find(first.location.value);
        watched != officerInRoom.end()) {
        // B5: the would-be robber is arrested by the room's officer instead.
        // No credit moves; the fine goes to City Hall through PayBill, and
        // the robber's rap sheet grows, which is what makes it leave its
        // gang later.
        Arrest(*robber, state_.robots[watched->second]);
    } else {
        // No officer: the robbery proceeds.
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
    }
}

// B14b: everything above needs two rival members standing in one room, and
// they almost never were: a member chose its outing out of the city's ~484
// venues as if it were not in a gang, so the members of one particular rival
// gang were one face in four hundred it might ever meet, and 40 days at
// 10000 robots gave 24 fights. A gang's turf is where its members live
// (`ReviewGangs` joins a robot to the gang whose turf is nearest its home)
// and therefore where it hangs around, so an outing now spends its time in
// one neighbourhood instead of anywhere in the city: the venues round one
// turf - the gang's own, or the turf of a gang it rivals, one of them chosen
// by lot. Rivals spending part of their nights where the other lives is what
// rivals do to each other, and it is what puts the two of them in the same
// room, where the fight roll, the robbery, the dropped bond and the officer's
// cooling of a member's feelings all get something to work on.
//
// On such an outing `Decide` only weighs these venues, and keeps every rule it
// always had for the choice within them: the robot must afford the visit and
// the way home, the venue must be open when it arrives, not be where it works,
// and have room for it. A robot that is not a member, a gang with no turf of
// its own or a rival's, and a turf with no venue in reach all give an empty
// answer, which is an ordinary outing over the whole city. The roll comes
// first, so an ordinary outing pays nothing for this, and the street distances
// are cached per origin node, so the city computes a turf's distances once.
// The answer is sorted by room id and never follows a container's order,
// so the same city makes the same choices on every machine (rule 1).
std::vector<EntityId> World::GangOuting(const Robot& robot) {
    if (!robot.gang.IsValid()) return {};
    const Gang* gang = FindGangOf(state_.gangs, robot);
    if (gang == nullptr) return {};
    if (static_cast<std::int64_t>(state_.random.UniformInt(1, 100)) > state_.settings.crime.gangOutingPercent) return {};

    // The turfs this outing could hang round: its gang's own, then those of
    // the gangs it rivals - `FoundGangs` writes a pair's stance both ways,
    // so this gang's own relations name its rivals, the same reading
    // `StanceOfGang` gives `ConsiderCrime`.
    std::vector<EntityId> turfs;
    if (gang->turf.IsValid()) turfs.push_back(gang->turf);
    for (const GangRelation& relation : gang->relations) {
        if (relation.stance != Stance::Rival) continue;
        const Gang* rival = FindGangById(state_.gangs, relation.gang);
        if (rival != nullptr && rival->turf.IsValid()) {
            turfs.push_back(rival->turf);
        }
    }
    if (turfs.empty()) return {};

    // One of them by lot: the member spends this outing in one gang's
    // neighbourhood, its own or a rival's, which is what mixes the two.
    const EntityId host =
        turfs[static_cast<std::size_t>(state_.random.UniformInt(
            0, static_cast<std::uint32_t>(turfs.size()) - 1))];

    // That building: the corner it stands on is the address to measure from.
    const Building* hostBuilding = nullptr;
    for (const Building& building : state_.city.buildings) {
        if (building.id != host) continue;
        hostBuilding = &building;
        break;
    }
    if (hostBuilding == nullptr) return {};

    // Every venue room a walk from that corner reaches.
    std::vector<EntityId> venues;
    for (const RoomKind kind : VenueKinds()) {
        for (const RoomRef& venue : rooms_.RoomsOfKind(kind)) {
            const auto meters =
                routes_.DistanceMeters(hostBuilding->entrance,
                                      venue.building->entrance);
            if (meters && static_cast<std::int64_t>(*meters) <= state_.settings.crime.turfReachMeters) {
                venues.push_back(venue.room->id);
            }
        }
    }
    std::sort(venues.begin(), venues.end());
    return venues;
}

}  // namespace mechapunk::sim
