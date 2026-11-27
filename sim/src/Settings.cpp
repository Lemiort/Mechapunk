#include "mechapunk/sim/Settings.hpp"

#include <array>
#include <format>

namespace mechapunk::sim {
namespace {

struct Entry {
    std::string_view name;
    std::int64_t& (*field)(SimSettings&);
    std::int64_t minimum;
    std::int64_t maximum;
};

#define MECHAPUNK_SETTING(group, member, low, high)                        \
    Entry {                                                                \
        #group "." #member, [](SimSettings& settings) -> std::int64_t& {   \
            return settings.group.member;                                  \
        }, low, high                                                       \
    }

const std::array kEntries{
    MECHAPUNK_SETTING(crime, robberyPercent, 0, 100),
    MECHAPUNK_SETTING(crime, gangOutingPercent, 0, 100),
    MECHAPUNK_SETTING(crime, turfReachMeters, 0, 100000),
    MECHAPUNK_SETTING(crime, patrolReachMinutes, 0, 1440),
    MECHAPUNK_SETTING(crime, patrolStayMinutes, 1, 480),
    MECHAPUNK_SETTING(crime, crimeLookbackMinutes, 0, 1440),
    MECHAPUNK_SETTING(construction, streakNights, 1, 365),
    MECHAPUNK_SETTING(construction, habitatFloors, 1, 60),
    MECHAPUNK_SETTING(construction, workplaceFloors, 1, 60),
    MECHAPUNK_SETTING(construction, siteBuilders, 1, 100),
    MECHAPUNK_SETTING(construction, siteSuppliesNight, 0, 1000000),
    MECHAPUNK_SETTING(construction, marketVacancyPercent, 0, 100),
    MECHAPUNK_SETTING(construction, unemploymentPercent, 0, 100),
};

#undef MECHAPUNK_SETTING

const std::array kNames = [] {
    std::array<std::string_view, kEntries.size()> names;
    for (std::size_t index = 0; index != kEntries.size(); ++index) {
        names[index] = kEntries[index].name;
    }
    return names;
}();

}  // namespace

std::expected<void, std::string> ApplySetting(SimSettings& settings, std::string_view name,
                                              std::int64_t value) {
    for (const Entry& entry : kEntries) {
        if (entry.name != name) {
            continue;
        }
        if (value < entry.minimum || value > entry.maximum) {
            return std::unexpected(std::format("{} must be from {} to {}, not {}", name,
                                               entry.minimum, entry.maximum, value));
        }
        entry.field(settings) = value;
        return {};
    }
    return std::unexpected(std::format("unknown setting '{}'", name));
}

std::expected<void, std::string> ValidateSettings(const SimSettings& settings) {
    SimSettings scratch = settings;
    for (const Entry& entry : kEntries) {
        const std::int64_t value = entry.field(scratch);
        if (value < entry.minimum || value > entry.maximum) {
            return std::unexpected(std::format("Setting {} is {}, outside {} to {}", entry.name,
                                               value, entry.minimum, entry.maximum));
        }
    }
    return {};
}

std::span<const std::string_view> SettingNames() noexcept { return kNames; }

}  // namespace mechapunk::sim
