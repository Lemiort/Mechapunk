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

}  // namespace mechapunk::sim::migrations
