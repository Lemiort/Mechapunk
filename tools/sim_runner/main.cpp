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
    std::uint64_t walkMinutes{};
    std::uint64_t lateArrivals{};
    std::uint64_t wages{};
    std::uint64_t spent{};
    std::map<std::string, std::uint64_t> visitsByKind;

    void Add(const sim::World& world, const sim::SimEvent& event) {
        switch (event.kind) {
            case sim::EventKind::Departed:
                ++walks;
                walkMinutes += event.minutes;
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
    std::string visits;
    for (const auto& [label, number] : stats.visitsByKind) {
        visits += std::format("{}{} {}", visits.empty() ? "" : ", ", number,
                              label);
    }
    std::println(
        "== Day {} done: {} walks ({} min average), {} late, {} cr earned, {} "
        "cr spent\n   visits: {}\n   now {} resting, {} working, {} visiting, "
        "{} walking; average charge {}, condition {}, social {}, fun {}, "
        "credits {}",
        day, stats.walks,
        stats.walks == 0 ? 0 : stats.walkMinutes / stats.walks,
        stats.lateArrivals, stats.wages, stats.spent,
        visits.empty() ? "none" : visits, activities[0], activities[2],
        activities[3], activities[1], charge / count, condition / count,
        social / count, fun / count, credits / count);
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
            (options->all || std::ranges::find(followed, event.robot) !=
                                 followed.end());
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
