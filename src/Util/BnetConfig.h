#pragma once

#include <string>

// Runtime configuration for Battle.net (StarCraft: Remastered) play.
// Loaded once per game from bwapi-data/AI/stardust_bnet.json (or bwapi-data/read/, bwapi-data/write/).
// Missing file or invalid values fall back to the safe defaults below, so the bot never needs recompiling to toggle options.
namespace BnetConfig
{
    struct Config
    {
        // Phase 1: Battle.net stability
        bool safeBnetMining = true;             // Bypass MiningOptimizationV2 path optimization / resend loops; use plain gather()
        bool stackedMineralFallback = true;     // Detect stacked minerals and stuck gathering workers, force reassignment
        int stuckWorkerFrames = 48;             // Frames without position / cargo change before a gathering worker is considered stuck
        int maxOrdersPerFrame = 0;              // 0 = unlimited; otherwise max worker gather/return orders issued per frame
        int minCommandIntervalFrames = -1;      // -1 = auto (latencyFrames + 2 when latency > 4, otherwise disabled)
        bool enableAutoGG = false;              // Allow the surrender ("gg" + leave) logic

        // Phase 2: Pluto Terran snipe
        bool plutoPvtSnipe = true;              // Master switch for the PvT changes below
        std::string forcedPvtOpening = "EarlyGameDefense";  // PvT opening name used instead of UCB1 (empty = use UCB1)
        std::string forcedRandomOpening = "OneGateCore";    // PvU (random enemy) opening used instead of UCB1 (empty = use UCB1)
        bool earlyObserverPriority = true;      // Staged first-observer requests (urgent / elevated / preventive), see BnetPolicy
        int observerMinDragoons = 2;            // Completed dragoons before a two-factory / many-vulture observer gets high priority
        int naturalCannons = 1;                 // Cannons to guarantee at the natural once it is taken (0-2)
        double bioAggressionScale = 0.7;        // Multiplier applied to attack aggression vs. MidGameBio (lower = more conservative)

        // Phase 3: Lightweight log
        bool bnetLog = true;                    // Write one-line summaries to bwapi-data/write/stardust_bnet.log
    };

    // Loads the configuration file; safe to call multiple times (reloads)
    void initialize();

    const Config &get();

    // Whether the config was loaded from a file (false = defaults)
    bool loadedFromFile();

    // Path the config was loaded from, or empty
    const std::string &loadedPath();

    // Compact one-line summary of the active options, for logging
    std::string summary();

    // Effective minimum interval in frames between repeated identical commands to the same unit.
    // Returns 0 when the guard is inactive.
    int commandIntervalFrames();
}
