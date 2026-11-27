#!/usr/bin/env python3
"""Keeps every module of Mechapunk small enough for a small model's context.

A module is what one task needs open at once: its sources, the headers it
leans on, and its tests. A small model has about 57k tokens; a module may use
at most MODULE_BUDGET of them, leaving the rest for instructions, the guide's
section and the work itself. No single file may exceed FILE_BUDGET.

Run it from the repository root (ctest runs it as ContextBudget):
    python scripts/context_budget.py            # check, print the table
    python scripts/context_budget.py --markdown # the table for docs/MODULES.md

When you add a source file, add it to a module below. When a module grows past
its budget, split a file (see docs/MODULES.md, "Splitting a file").
"""

import pathlib
import sys

BYTES_PER_TOKEN = 3.5
FILE_BUDGET = 10_000
MODULE_BUDGET = 25_000

I = "sim/include/mechapunk/sim/"
MODULES = {
    "world": [I + "World.hpp", "sim/src/WorldInternal.hpp", "sim/src/World.cpp",
              "sim/src/Behaviour.cpp", "sim/src/Residents.cpp", "tests/WorldTests.cpp"],
    "generation": [I + "World.hpp", "sim/src/WorldInternal.hpp", "sim/src/Generation.cpp",
                   I + "City.hpp", "sim/src/City.cpp", "sim/src/Names.hpp", "tests/CityTests.cpp"],
    "city-streets": [I + "World.hpp", I + "City.hpp", "tests/CityStreetTests.cpp"],
    "city-styles": [I + "City.hpp", "sim/src/City.cpp", "sim/src/CityStyle.cpp",
                    "tests/CityStyleTests.cpp"],
    "validation": [I + "World.hpp", "sim/src/Validation.cpp", "sim/src/Describe.cpp"],
    "money": [I + "World.hpp", I + "Economy.hpp", "sim/src/EconomyInternal.hpp",
              "sim/src/Economy.cpp", "sim/src/Money.cpp", "sim/src/Settlement.cpp",
              "tests/EconomyTests.cpp"],
    "property": [I + "World.hpp", I + "Economy.hpp", "sim/src/EconomyInternal.hpp",
                 "sim/src/Founding.cpp", "sim/src/Property.cpp", "tests/HousingTests.cpp"],
    "units": [I + "World.hpp", I + "Economy.hpp", "sim/src/EconomyInternal.hpp",
              "sim/src/Units.cpp", "tests/VenueTests.cpp"],
    "statistics": [I + "Economy.hpp", "sim/src/EconomyInternal.hpp",
                   "sim/src/EconomyStatistics.cpp", "app/Statistics.hpp",
                   "app/StatisticsInternal.hpp", "app/Statistics.cpp",
                   "app/StatisticsChronicle.cpp", "app/StatisticsConstruction.cpp"],
    "careers": [I + "World.hpp", I + "Careers.hpp", I + "Construction.hpp", "sim/src/Careers.cpp",
                I + "Venues.hpp", "sim/src/Venues.cpp", "tests/CareersTests.cpp"],
    "social": [I + "World.hpp", I + "Social.hpp", "sim/src/Social.cpp",
               "tests/SocialTests.cpp"],
    "life-cycle": [I + "World.hpp", I + "Construction.hpp", "sim/src/LifeCycle.cpp",
                   "tests/LifeCycleTests.cpp"],
    "metro": [I + "Metro.hpp", "sim/src/Metro.cpp", "sim/src/Transit.cpp",
              "tests/MetroTests.cpp", "tests/TransitTests.cpp"],
    "saves": [I + "Save.hpp", "sim/src/Save.cpp", "sim/src/Migrations.hpp", "sim/src/Migrations.cpp",
              "tests/SaveTests.cpp"],
    "settings": [I + "Settings.hpp", "sim/src/Settings.cpp", "tests/SettingsTests.cpp"],
    "basics": [I + "GameTime.hpp", I + "Ids.hpp", I + "Random.hpp", I + "Weather.hpp",
               "sim/src/GameTime.cpp", "sim/src/Random.cpp", "sim/src/Weather.cpp",
               "tests/RandomTests.cpp", "tests/WeatherTests.cpp"],
    "runner": [I + "World.hpp", "tools/sim_runner/main.cpp"],
    "view-core": ["app/CityView.hpp", "app/CityViewInternal.hpp", "app/CityView.cpp",
                  "app/CityStatic.cpp", "app/CityLights.cpp"],
    "view-massing": ["app/CityView.hpp", "app/CityViewInternal.hpp", "app/Massing.hpp",
                     "app/Massing.cpp", "app/CityMassing.cpp", "tests/MassingTests.cpp"],
    "site-style": [I + "Construction.hpp", "sim/src/Construction.cpp", I + "City.hpp",
                   "tests/SiteStyleTests.cpp"],
    "view-robots": ["app/CityView.hpp", "app/CityViewInternal.hpp", "app/CityRobots.cpp",
                    "app/CityMetro.cpp", "app/CityDrivers.cpp", I + "Metro.hpp"],
    "view-interiors": ["app/CityView.hpp", "app/CityViewInternal.hpp", "app/CityInteriors.cpp",
                       "app/Interior.hpp"],
    "floor-plans": ["app/Interior.hpp", "app/InteriorKit.hpp", "app/Interior.cpp",
                    "app/InteriorHomes.cpp", "app/InteriorStores.cpp",
                    "app/InteriorWorkplaces.cpp", "tests/LayoutTests.cpp"],
    "rendering": ["app/GameRenderer.hpp", "app/GameRenderer.cpp", "app/Props.hpp",
                  "app/Props.cpp", "app/Rain.hpp", "app/Rain.cpp", "app/OrbitCamera.hpp"],
    "ui": ["app/main.cpp", "app/SimulationHost.hpp", "app/SimulationHost.cpp"],
    "organization-lists": ["app/OrganizationLists.hpp", "app/OrganizationLists.cpp",
                           "tests/OrganizationListsTests.cpp"],
    "project-lists": ["app/ProjectLists.hpp", "app/ProjectLists.cpp",
                      "tests/ProjectListsTests.cpp"],
    "inspector": ["app/Inspector.hpp", "app/Inspector.cpp", "app/Links.hpp",
                  "app/Links.cpp", "app/InspectorInternal.hpp",
                  "app/InspectorOrganization.cpp", "app/InspectorGang.cpp"],
    "robot-pages": ["app/InspectorRobot.cpp", "app/InspectorInternal.hpp",
                    "app/Links.hpp", "app/Inspector.hpp"],
    "project-pages": ["app/InspectorProject.cpp", "app/InspectorInternal.hpp",
                      "app/Links.hpp", "app/ProjectLists.hpp", "app/Inspector.hpp"],
    "chronicle-pages": ["app/InspectorHistory.cpp", "app/InspectorChronicle.cpp",
                        "app/ChroniclePagesInternal.hpp", "app/InspectorInternal.hpp",
                        "app/Links.hpp", "app/ChronicleLists.hpp", "app/Inspector.hpp"],
    "gangs": [I + "Gangs.hpp", "sim/src/Gangs.cpp", "tests/GangRosterTests.cpp",
              "tests/GangCrimeTests.cpp"],
    "gang-officer": [I + "Gangs.hpp", "tests/GangOfficerTests.cpp"],
    "gang-lists": ["app/GangLists.hpp", "app/GangLists.cpp", "tests/GangListsTests.cpp"],
    "chronicle-lists": ["app/ChronicleLists.hpp", "app/ChronicleLists.cpp",
                        "tests/ChronicleListsTests.cpp"],
    "chronicle": [I + "Chronicle.hpp", "sim/src/Chronicle.cpp", "tests/ChronicleTests.cpp",
                  I + "World.hpp"],
    "construction": [I + "Construction.hpp", "sim/src/Construction.cpp",
                     I + "World.hpp", "tests/ConstructionTests.cpp",
                     "tests/SiteOfficeTests.cpp"],
    "site-supplies": [I + "Construction.hpp", "sim/src/Construction.cpp",
                      I + "World.hpp", "tests/SiteSuppliesTests.cpp"],
    "construction-counters": [I + "Construction.hpp", "sim/src/Construction.cpp",
                              I + "World.hpp", "tests/ConstructionCountersTests.cpp"],
}
SOURCE_DIRS = ["sim", "app", "tests", "tools"]


def tokens(path: pathlib.Path) -> int:
    return round(path.stat().st_size / BYTES_PER_TOKEN)


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    problems = []
    sources = sorted(
        p.relative_to(root).as_posix()
        for d in SOURCE_DIRS
        for p in (root / d).rglob("*")
        if p.suffix in (".cpp", ".hpp") and "fixtures" not in p.parts)
    listed = {f for files in MODULES.values() for f in files}
    for source in sources:
        if source not in listed:
            problems.append(f"{source} is in no module: add it to MODULES")
        size = tokens(root / source)
        if size > FILE_BUDGET:
            problems.append(f"{source} has about {size} tokens, over {FILE_BUDGET}: split it")
    rows = []
    for name, files in MODULES.items():
        missing = [f for f in files if not (root / f).exists()]
        for f in missing:
            problems.append(f"module {name}: {f} does not exist")
        total = sum(tokens(root / f) for f in files if f not in missing)
        rows.append((name, total, files))
        if total > MODULE_BUDGET:
            problems.append(f"module {name} has about {total} tokens, over {MODULE_BUDGET}")
    if "--markdown" in sys.argv:
        print("| Module | Tokens | Files |")
        print("|---|---:|---|")
        for name, total, files in rows:
            names = ", ".join(f"`{f.split('/')[-1]}`" for f in files)
            print(f"| {name} | {total // 1000}k | {names} |")
    else:
        for name, total, _ in rows:
            print(f"{total:7d}  {name}")
    for problem in problems:
        print("PROBLEM:", problem, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
