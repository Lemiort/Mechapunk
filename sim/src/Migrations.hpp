#pragma once

#include <expected>
#include <string>

#include "mechapunk/sim/Save.hpp"

namespace mechapunk::sim::migrations {

// Version 1 to 2: buildings gain floors and rooms, robots get an apartment,
// a workspace, needs and credits.
//
// Every migration is frozen once released. It works on the untyped JSON of
// the save and must never call current game code (generators, rules), which
// will keep changing while old saves must keep loading the same way.
[[nodiscard]] std::expected<void, std::string> V1ToV2(SaveDocument& world);

// Version 2 to 3: apartments get a daily rent, robots get two days off a
// week, a planned departure for work, and rent owed.
[[nodiscard]] std::expected<void, std::string> V2ToV3(SaveDocument& world);

// Version 3 to 4: robots get two interests, and start with no bonds and no
// memories.
[[nodiscard]] std::expected<void, std::string> V3ToV4(SaveDocument& world);

// Version 4 to 5: robots start as juniors with no record at work yet.
[[nodiscard]] std::expected<void, std::string> V4ToV5(SaveDocument& world);

// Version 5 to 6: robots get an age and the wear that comes with it, and the
// world a list of scrapped robots, empty so far.
[[nodiscard]] std::expected<void, std::string> V5ToV6(SaveDocument& world);

// Version 6 to 7: the city gets metro stations at every third intersection
// of its square street grid, and robots are not riding yet.
[[nodiscard]] std::expected<void, std::string> V6ToV7(SaveDocument& world);

// Version 7 to 8: the world gets weather, dry for its first six hours.
[[nodiscard]] std::expected<void, std::string> V7ToV8(SaveDocument& world);

// Version 8 to 9: robots riding the metro change lines at their last
// station, that is, they don't.
[[nodiscard]] std::expected<void, std::string> V8ToV9(SaveDocument& world);

// Version 9 to 10: a closed economy. Banks, a central bank and
// corporations are founded with capital; corporations own every workspace
// and venue, robots the apartments they live in, and a housing trust the
// rest. The founding money is logged as created; no statistics yet.
[[nodiscard]] std::expected<void, std::string> V9ToV10(SaveDocument& world);

// Version 10 to 11: robots have spent nothing of today's budget yet, and
// supplies cost venues 60% of their takings. Venues hire staff as robots
// look for work; no format change is needed for that.
[[nodiscard]] std::expected<void, std::string> V10ToV11(SaveDocument& world);

// Version 11 to 12: apartments get a class; every older apartment is a
// studio. The city gets a housing office, with no dorms yet.
[[nodiscard]] std::expected<void, std::string> V11ToV12(SaveDocument& world);

// Version 12 to 13: venues ask their base price and have had no guests
// today; market rents are at their base.
[[nodiscard]] std::expected<void, std::string> V12ToV13(SaveDocument& world);

// Version 13 to 14: no venue or workspace has been quiet yet.
[[nodiscard]] std::expected<void, std::string> V13ToV14(SaveDocument& world);

// Version 14 to 15: the city's housing office becomes City Hall, which owns
// the metro; nobody has paid taxes yet.
[[nodiscard]] std::expected<void, std::string> V14ToV15(SaveDocument& world);

// Version 15 to 16: the central bank's rate starts at the old fixed rate of
// 10/10000 a night.
[[nodiscard]] std::expected<void, std::string> V15ToV16(SaveDocument& world);

// Version 16 to 17: robots gain a venue to patrol; nobody has been assigned
// one yet.
[[nodiscard]] std::expected<void, std::string> V16ToV17(SaveDocument& world);

// Version 17 to 18: gangs and crime statistics; every robot starts with no
// gang and zero arrests, counters are all zero.
[[nodiscard]] std::expected<void, std::string> V17ToV18(SaveDocument& world);

// Version 18 to 19: the world gets a chronicle of notable events, empty at
// first: it fills up as the game runs, so an old save simply starts its
// story from the moment it is loaded.
[[nodiscard]] std::expected<void, std::string> V18ToV19(SaveDocument& world);

// Version 19 to 20: an arrest names the arresting officer. No key is new
// (memories already name the other robot), but an `arrested` chronicle
// entry of a version 19 save stored the criminal in `robot` and knew no
// officer, so the migration turns it around: the criminal goes to
// `otherRobot`, `robot` becomes none, and the new memory kind
// `arrestedSomeone` cannot appear in a version 19 file at all.
[[nodiscard]] std::expected<void, std::string> V19ToV20(SaveDocument& world);

// Version 20 to 21: a chronicle entry names the account its event's money
// moved for - the account an emission created money in or destroyed it
// from, the owner of a unit that opened or closed. A version 20 entry
// stored no account and the migration must not invent one the old game
// never recorded, so every entry of an old save gains none.
[[nodiscard]] std::expected<void, std::string> V20ToV21(SaveDocument& world);

// Version 21 to 22: the city saves its free building plots - out of four
// corners per intersection, those no building of that intersection stands
// on. A version 21 city names its nodes and every building's entrance, so
// the migration computes the plots from them exactly as the view counts a
// node's corners, instead of leaving the land construction builds on empty.
[[nodiscard]] std::expected<void, std::string> V21ToV22(SaveDocument& world);

// Version 22 to 23: the world gets its list of buildings under construction,
// empty at first: a version 22 game never raised a building, and projects
// are started by the nightly review (task D, step D2b), not by this
// migration, so an old save simply starts building as soon as the current
// game runs it and homes or jobs run short.
[[nodiscard]] std::expected<void, std::string> V22ToV23(SaveDocument& world);

// Version 23 to 24: rooms gain a new kind, the site office of a building
// going up (task D, step D3a). No key is new and no stored value changes:
// a version 23 game never wrote a site office, so no room of an old save
// can carry the kind, and the migration changes nothing. The bump is there
// because a version 24 save may name a kind a version 23 game cannot read.
[[nodiscard]] std::expected<void, std::string> V23ToV24(SaveDocument& world);

}  // namespace mechapunk::sim::migrations
