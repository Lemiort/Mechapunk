// mechapunk-sim: runs the city simulation headless and prints what happens.
//
//   mechapunk-sim --seed 42 --robots 200 --days 3 --save city.json
//   mechapunk-sim --load city.json --days 1 --follow 5
#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <map>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Save.hpp"
#include "mechapunk/sim/Venues.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {

struct Options {
    std::uint64_t seed{1};
    std::uint32_t robots{100};
    // One day when no duration is given at all.
    std::optional<std::uint64_t> days;
    std::uint64_t hours{};
    std::uint64_t minutes{};
    std::uint32_t follow{3};
    bool all{false};
    bool quiet{false};
    bool help{false};
    std::optional<std::string> load;
    std::optional<std::string> save;
};

void PrintUsage() {
    std::println(
        "Usage: mechapunk-sim [options]\n"
        "  --seed N      world seed for a new city (default 1)\n"
        "  --robots N    population of a new city (default 100)\n"
        "  --load FILE   continue a saved world instead of generating one\n"
        "  --days N      days to simulate (default 1 if no duration given)\n"
        "  --hours N     extra hours to simulate (default 0)\n"
        "  --minutes N   extra minutes to simulate (default 0)\n"
        "  --follow N    print events of the first N robots (default 3)\n"
        "  --all         print events of every robot\n"
        "  --quiet       print only daily summaries\n"
        "  --save FILE   save the world when done");
}

template <typename T>
bool ParseNumber(std::string_view text, T& value) {
    const auto* end = text.data() + text.size();
    const auto [parsed, error] = std::from_chars(text.data(), end, value);
    return error == std::errc{} && parsed == end;
}

std::optional<Options> ParseOptions(std::span<char*> arguments) {
    Options options;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string_view name = arguments[index];
        const auto next = [&]() -> std::optional<std::string_view> {
            if (index + 1 >= arguments.size()) {
                std::println(stderr, "{} needs a value", name);
                return std::nullopt;
            }
            return std::string_view{arguments[++index]};
        };
        const auto number = [&](auto& target) {
            const auto text = next();
            if (!text || !ParseNumber(*text, target)) {
                std::println(stderr, "{} needs a whole number", name);
                return false;
            }
            return true;
        };
        bool ok = true;
        if (name == "--seed") {
            ok = number(options.seed);
        } else if (name == "--robots") {
            ok = number(options.robots);
            if (ok && options.robots == 0) {
                std::println(stderr, "--robots must be at least 1");
                ok = false;
            }
        } else if (name == "--days") {
            ok = number(options.days.emplace());
        } else if (name == "--hours") {
            ok = number(options.hours);
        } else if (name == "--minutes") {
            ok = number(options.minutes);
        } else if (name == "--follow") {
            ok = number(options.follow);
        } else if (name == "--all") {
            options.all = true;
        } else if (name == "--quiet") {
            options.quiet = true;
        } else if (name == "--load" || name == "--save") {
            const auto path = next();
            ok = path.has_value();
            if (ok) {
                (name == "--load" ? options.load : options.save) =
                    std::string{*path};
            }
        } else if (name == "--help" || name == "-h") {
            options.help = true;
        } else {
            std::println(stderr, "Unknown option {}", name);
            ok = false;
        }
        if (!ok) {
            PrintUsage();
            return std::nullopt;
        }
    }
    return options;
}

struct DayStats {
    std::uint64_t walks{};
    std::uint64_t metroRides{};
    std::uint64_t fares{};
    std::uint64_t walkMinutes{};
    std::uint64_t lateArrivals{};
    std::uint64_t wages{};
    std::uint64_t spent{};
    std::uint64_t rent{};
    std::uint64_t behindOnRent{};
    std::uint64_t talks{};
    std::uint64_t scrapped{};
    std::uint64_t arrived{};
    std::uint64_t arguments{};
    std::uint64_t rainMinutes{};
    std::uint32_t heaviestRain{};
    std::map<sim::MemoryKind, std::uint64_t> memories;
    std::map<std::string, std::uint64_t> visitsByKind;

    void Add(const sim::World& world, const sim::SimEvent& event) {
        switch (event.kind) {
            case sim::EventKind::Departed:
                ++walks;
                walkMinutes += event.minutes;
                metroRides += event.metro ? 1 : 0;
                fares += static_cast<std::uint64_t>(event.credits);
                break;
            case sim::EventKind::ClockedIn:
                lateArrivals += event.minutes > 0 ? 1 : 0;
                break;
            case sim::EventKind::ShiftEnded:
                wages += static_cast<std::uint64_t>(event.credits);
                break;
            case sim::EventKind::ArrivedAtVenue: {
                spent += static_cast<std::uint64_t>(event.credits);
                const auto room = world.Rooms().Find(event.room);
                const sim::VenueRules* rules =
                    room ? sim::FindVenueRules(room->room->kind) : nullptr;
                ++visitsByKind[rules != nullptr ? std::string{rules->label}
                                                : "?"];
                break;
            }
            case sim::EventKind::ArrivedHome:
                break;
            case sim::EventKind::RentPaid:
                rent += static_cast<std::uint64_t>(event.credits);
                break;
            case sim::EventKind::RentMissed:
                ++behindOnRent;
                break;
            case sim::EventKind::Talked:
                // Both robots of a talk get an event; count the talk once.
                if (event.robot < event.otherRobot) {
                    ++talks;
                    arguments += event.change < 0 ? 1 : 0;
                }
                break;
            case sim::EventKind::Remembered:
                ++memories[event.memory];
                break;
            case sim::EventKind::Scrapped:
                ++scrapped;
                break;
            case sim::EventKind::Arrived:
                ++arrived;
                break;
            case sim::EventKind::WeatherChanged:
                if (event.change > 0) {
                    rainMinutes += event.minutes;
                    heaviestRain = std::max(
                        heaviestRain, static_cast<std::uint32_t>(event.change));
                }
                break;
        }
    }
};

void PrintSummary(std::uint64_t day, const sim::World& world,
                  const DayStats& stats) {
    std::array<std::uint64_t, 4> activities{};
    std::int64_t charge = 0;
    std::int64_t condition = 0;
    std::int64_t social = 0;
    std::int64_t fun = 0;
    std::int64_t credits = 0;
    for (const sim::Robot& robot : world.State().robots) {
        ++activities.at(static_cast<std::size_t>(robot.activity));
        charge += robot.needs.charge;
        condition += robot.needs.condition;
        social += robot.needs.social;
        fun += robot.needs.fun;
        credits += robot.credits;
    }
    const auto count = std::max<std::int64_t>(
        1, static_cast<std::int64_t>(world.State().robots.size()));
    std::uint64_t withFriend = 0;
    std::uint64_t withCloseFriend = 0;
    std::uint64_t withRival = 0;
    for (const sim::Robot& robot : world.State().robots) {
        const auto has = [&](sim::BondLevel level) {
            return std::ranges::any_of(robot.bonds, [&](const sim::Bond& bond) {
                return bond.level == level;
            });
        };
        withFriend += has(sim::BondLevel::Friend) ||
                              has(sim::BondLevel::CloseFriend)
                          ? 1
                          : 0;
        withCloseFriend += has(sim::BondLevel::CloseFriend) ? 1 : 0;
        withRival += has(sim::BondLevel::Rival) ? 1 : 0;
    }
    const auto memories = [&](sim::MemoryKind kind) {
        const auto found = stats.memories.find(kind);
        return found != stats.memories.end() ? found->second : 0;
    };
    std::string visits;
    for (const auto& [label, number] : stats.visitsByKind) {
        visits += std::format("{}{} {}", visits.empty() ? "" : ", ", number,
                              label);
    }
    std::println(
        "== Day {} done: {} walks ({} min average), {} late, {} cr earned, {} "
        "cr spent, {} cr rent, {} behind on rent\n   visits: {}\n   now {} "
        "resting, {} working, {} visiting, {} walking; average charge {}, "
        "condition {}, social {}, fun {}, credits {}\n   {} talks ({} "
        "arguments); {} new friendships, {} grew close, {} drifted apart, {} "
        "new rivalries, {} made peace; {} robots have a friend, {} a close "
        "friend, {} a rival",
        day, stats.walks,
        stats.walks == 0 ? 0 : stats.walkMinutes / stats.walks,
        stats.lateArrivals, stats.wages, stats.spent, stats.rent,
        stats.behindOnRent,
        visits.empty() ? "none" : visits, activities[0], activities[2],
        activities[3], activities[1], charge / count, condition / count,
        social / count, fun / count, credits / count, stats.talks,
        stats.arguments, memories(sim::MemoryKind::BecameFriends),
        memories(sim::MemoryKind::BecameCloseFriends),
        memories(sim::MemoryKind::DriftedApart),
        memories(sim::MemoryKind::BecameRivals),
        memories(sim::MemoryKind::MadePeace), withFriend, withCloseFriend,
        withRival);
    std::array<std::uint64_t, sim::kMaxJobLevel + 1> levels{};
    std::uint64_t outOfWork = 0;
    for (const sim::Robot& robot : world.State().robots) {
        if (robot.workspace.IsValid()) {
            ++levels.at(robot.jobLevel);
        } else {
            ++outOfWork;
        }
    }
    std::println(
        "   {} promoted, {} lost their job, {} found one, {} moved up, {} "
        "evicted; {} junior, {} regular, {} senior, {} lead, {} out of work",
        memories(sim::MemoryKind::Promoted), memories(sim::MemoryKind::LostJob),
        memories(sim::MemoryKind::FoundJob), memories(sim::MemoryKind::MovedUp),
        memories(sim::MemoryKind::Evicted), levels[0], levels[1], levels[2],
        levels[3], outOfWork);
    std::int64_t age = 0;
    std::int64_t wear = 0;
    for (const sim::Robot& robot : world.State().robots) {
        age += robot.age;
        wear += robot.wear;
    }
    std::println(
        "   {} trips by metro ({} cr fares); {} scrapped, {} arrived, {} "
        "friends lost, {} dodgy parts; {} robots, average age {} days, "
        "average wear {}",
        stats.metroRides, stats.fares, stats.scrapped, stats.arrived, memories(sim::MemoryKind::LostFriend),
        memories(sim::MemoryKind::GotBadPart), world.State().robots.size(),
        age / count, wear / count);
    // Rain spells are counted on the day they start, for their whole length.
    std::println("   weather: {} min of rain started, heaviest {}; now {}",
                 stats.rainMinutes, sim::RainName(stats.heaviestRain),
                 sim::RainName(world.State().weather.rain));
    if (!world.State().history.empty()) {
        const sim::DailyStats& day = world.State().history.back();
        const sim::DayCounters& counted = day.counted;
        std::println(
            "   economy: money {} (robots {}, corporations {}, banks {}); created {} "
            "today, {} in all, destroyed {}; wages {} at {}%, sales {}, rent {}, upkeep {}, "
            "supplies {}, dividends {}; debts: robots {} ({} robots), corporations {}, "
            "banks {}\n   wealth: median {} credits, {} wealth, gini {}, top tenth {}, "
            "bottom half {}; {} homeowners, {} tenants, {} landlords, {} let by "
            "corporations, {} vacant; {} sales; {} broke, {} behind",
            day.moneySupply, day.robotMoney, day.corporationMoney, day.bankMoney,
            counted.created, day.createdTotal, day.destroyedTotal, counted.wages,
            day.averagePayPercent, counted.venueSales, counted.rent, counted.upkeep,
            counted.supplies, counted.dividends, day.robotDebt, day.indebtedRobots,
            day.corporationDebt, day.bankDebt, day.medianCredits, day.medianWealth,
            day.giniWealth, day.topTenthShare, day.bottomHalfShare, day.homeowners,
            day.tenants, day.landlords, day.corporateApartments, day.vacantApartments,
            counted.propertySales, day.broke, day.behindOnBills);
        std::println(
            "   city hall: {} in wage tax, {} property tax, {} profit tax; {} support, {} public "
            "works; {} in the treasury, {} owed; central bank rate {}/10000 a night, {} interest on "
            "savings",
            counted.wageTax, counted.propertyTax, counted.profitTax, counted.benefits,
            counted.publicSpending, day.governmentMoney, day.governmentDebt, day.policyRate,
            counted.savingsInterest);
        std::println(
            "   homes: {} in dorms, {} in studios, {} in flats, {} in two-bedroom flats, {} in "
            "penthouses; {} sharing ({} moved in together); supplies at {}%; rents {}/{}/{}/{}/{}, "
            "venue prices at {}%; {} venues ({} opened, {} closed), {} empty units",
            day.residentsByClass[0], day.residentsByClass[1], day.residentsByClass[2],
            day.residentsByClass[3], day.residentsByClass[4], day.sharing,
            counted.movedInTogether, day.supplyPercent, day.averageRent[0], day.averageRent[1],
            day.averageRent[2], day.averageRent[3], day.averageRent[4], day.venuePricePercent,
            day.openVenues, counted.venuesOpened, counted.venuesClosed, day.emptyUnits);
        std::println(
            "   crime: {} robberies ({} cr stolen), {} arrests, {} gang fights; {} in gangs",
            counted.robberies, counted.robbedCredits, counted.arrests, counted.fights,
            day.gangMembers);
    }
}


int Run(std::span<char*> arguments) {
    const auto options = ParseOptions(arguments);
    if (!options) {
        return EXIT_FAILURE;
    }
    if (options->help) {
        PrintUsage();
        return EXIT_SUCCESS;
    }

    std::optional<sim::World> world;
    if (options->load) {
        auto state = sim::LoadWorldFromFile(*options->load);
        if (!state) {
            std::println(stderr, "Cannot load {}: {}", *options->load,
                         state.error());
            return EXIT_FAILURE;
        }
        auto loaded = sim::World::FromState(std::move(*state));
        if (!loaded) {
            std::println(stderr, "Invalid save {}: {}", *options->load,
                         loaded.error());
            return EXIT_FAILURE;
        }
        world.emplace(std::move(*loaded));
        std::println("Loaded {} at {}", *options->load,
                     sim::FormatTime(world->CurrentTick()));
    } else {
        world.emplace(sim::World::Generate(
            {.seed = options->seed, .robots = options->robots}));
        std::println("Generated seed {}: {} robots, {} buildings",
                     options->seed, world->State().robots.size(),
                     world->State().city.buildings.size());
    }

    // Robots are sorted by id; follow the first few.
    std::vector<sim::EntityId> followed;
    for (const sim::Robot& robot : world->State().robots) {
        if (followed.size() >= options->follow) {
            break;
        }
        followed.push_back(robot.id);
    }

    DayStats stats;
    world->SetEventSink([&](const sim::SimEvent& event) {
        stats.Add(*world, event);
        const bool show =
            !options->quiet &&
            (options->all || event.kind == sim::EventKind::WeatherChanged ||
             std::ranges::find(followed, event.robot) != followed.end());
        if (show) {
            std::println("{}", world->Describe(event));
        }
    });

    const bool anyDuration =
        options->days || options->hours > 0 || options->minutes > 0;
    const std::uint64_t days = options->days.value_or(anyDuration ? 0 : 1);
    const sim::Tick minutes = (days * sim::kMinutesPerDay) +
                              (options->hours * sim::kMinutesPerHour) +
                              options->minutes;
    const auto started = std::chrono::steady_clock::now();
    for (sim::Tick minute = 0; minute < minutes; ++minute) {
        world->Step();
        if (sim::MinuteOfDay(world->CurrentTick()) == 0) {
            PrintSummary(sim::DayNumber(world->CurrentTick()) - 1, *world,
                         stats);
            stats = {};
        }
    }
    const auto elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    std::println("Simulated {} minutes to {} in {:.3f} s", minutes,
                 sim::FormatTime(world->CurrentTick()), elapsed);

    if (options->save) {
        if (auto saved = sim::SaveWorldToFile(world->State(), *options->save);
            !saved) {
            std::println(stderr, "{}", saved.error());
            return EXIT_FAILURE;
        }
        std::println("Saved {}", *options->save);
    }
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        return Run(std::span(argv, static_cast<std::size_t>(argc)));
    } catch (const std::exception& error) {
        std::println(stderr, "mechapunk-sim failed: {}", error.what());
    } catch (...) {
        std::println(stderr, "mechapunk-sim failed with an unknown error");
    }
    return EXIT_FAILURE;
}
