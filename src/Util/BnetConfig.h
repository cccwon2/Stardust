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
        int naturalCannons = 1;                 // Conditional cannon requests in the natural mineral line once it is taken (0-2)
        int mainCannons = 1;                    // Conditional cannon requests in the main mineral line vs. vultures / mech (0-1);
                                                // absent key -> 1 if natural_cannons > 0 else 0
        int mechNaturalCannons = 2;             // Natural target raised to this under a mech / vulture threat (0-2)
        int expansionCannons = 2;               // Cannons in the mineral line of every other owned base under a mech / vulture threat (0-2)
        bool cannonThreatPriority = true;       // First cannon (and its pylon) of each base above army production under a threat
        // Aggression multipliers vs. a recognized MidGameBio Terran (AttackBaseSquad), per decision state.
        // Effective values after the compatibility policy in BnetPolicy::resolveBioScales.
        double bioAttackStartScale = 0.7;       // shouldStartAttack (moving -> attacking)
        double bioAttackContinueScale = 1.0;    // shouldContinueAttack (attacking -> regrouping); 1.0 = upstream
        double bioAttackResumeScale = 0.7;      // shouldStopRegrouping (regrouping -> attacking)
        bool bioLegacyKeyPresent = false;       // bio_aggression_scale was present in the file (applies to start / resume only)
        double bioLegacyScale = 0.7;

        // PvT vs. MidGameMech (only with pluto_pvt_snipe on, enemy Terran recognized as MidGameMech)
        bool pvtMechFix = true;                 // Master switch for the mech changes below
        int mechWorkersPerArmyUnit = 4;         // Baseline army: one unit per N mineral workers (upstream 6)
        double mechZealotRatio = 0.5;           // Max zealots per dragoon vs. tanks (upstream 1.5, or 1.0 with many vultures)
        int mechExpandArmyPerBase = 6;          // 4th+ base needs army >= N * owned bases; also no contained-enemy boost. 0 = off
        int mechLuxuryUpgradeMinArmy = 20;      // Defer observer speed/sight and shuttle speed until army >= N. 0 = off
        double mechAttackStartScale = 0.7;      // AttackBaseSquad aggression multipliers vs. MidGameMech
        double mechAttackContinueScale = 1.0;
        double mechAttackResumeScale = 0.7;

        // Repeated move guard for non-worker units (observers, army)
        bool combatMoveGuard = true;            // Skip re-sending a move to (nearly) the same target within the command interval
        int moveResendTolerancePx = 64;         // Targets within this distance of the last move target count as the same
        int combatMoveIntervalFrames = 12;      // Minimum frames between such moves (at least the latency command interval)
        int observerMoveIntervalFrames = 24;    // Same for observers following the army
        int moveGuardMinRemainingPx = 48;       // Never skip once the unit is this close to its last move target
        bool gridWaypointHold = true;           // Keep the current grid waypoint until close to it (no per-tile re-targeting)
        int gridWaypointRefreshPx = 64;         // Distance to the waypoint at which the next grid waypoint is sent
        int spellResendIntervalFrames = 24;     // Minimum frames between identical stasis / unload orders. 0 = off
        int unstickBackoffMaxFrames = 192;      // Backoff cap for repeatedly stuck workers (stop / move loop). 0 = off

        // Combat fixes (Battle.net latency, attack <-> regroup oscillation, fleeing from faster units)
        int microLatencyFrames = -1;            // Latency used by unit micro: -1 = auto (highest seen when > 4), 0 = reported
                                                // by BWAPI (upstream), >0 = forced (1-12)
        bool inFlightMoveGuard = true;          // Don't resend a near-identical move while the previous one is still in flight
        bool pvtCombatCommit = true;            // vs. Terran: attack / regroup commitment below
        int attackCommitFrames = 48;            // Keep a new attack at least this long unless the sim predicts losing > commit_max_loss
        double commitMaxLoss = 0.6;             // Share of our army the sim may predict losing while an attack is held
        int attackAbortStableFrames = 24;       // Retreat recommendation must be stable this long before aborting (upstream 6)
        int regroupCommitFrames = 0;            // Keep a new (non-flee) regroup at least this long before attacking again. 0 = off
        bool fleeGuard = true;                  // vs. Terran: fight instead of fleeing when the pursuers are faster (vultures)
        double fleeGuardMaxLoss = 0.85;         // ... unless the sim predicts losing at least this share of our army
        double fleeGuardSpeedRatio = 1.25;      // Pursuer counts as faster when its top speed >= ratio * our slowest unit

        // PvZ vs. zergling all-ins (pool first -> mass speedlings)
        std::string forcedPvzOpening = "FFEDragoons";   // PvZ opening used instead of UCB1: FFEDragoons, SairSpeedlot,
                                                        // EarlyGameDefense (empty = use UCB1)
        bool pvzAntiAllinFix = true;            // Master switch for the PvZ changes below
        int pvzAllinBaseLings = 6;              // Zerglings at our main / natural that confirm an all-in (before frame 9000). 0 = off
        int pvzAllinSeenLings = 12;             // Living zerglings seen that confirm an all-in (before frame 8000). 0 = off
        int pvzPoolFirstCannons = 3;            // FFE wall cannons once a pool-first opening is recognized (2-5)
        int pvzPoolFirstZealots = 2;            // FFEDragoons: zealots before dragoons vs. pool first (0-6)
        int pvzAllinZealots = 4;                // FFEDragoons: minimum zealots vs. an all-in, above probe production (0-12)
        int pvzAllinProbeCap = 18;              // FFE: probes above this move below zealots during an all-in. 0 = off

        // PvT vs. vulture drops / runbys and spider mines (2026-11-15 KnockOut analysis)
        bool invalidMoveGuard = true;           // Reject move orders to invalid positions (upstream sent them to (4095,4095))
        bool workerFleeVultures = true;         // Workers flee vultures at their base instead of attacking them (no cannon there)
        int workerFleeMarginPx = 96;            // Vulture within its range + this margin of a worker counts as a threat
        int workerFleeHoldFrames = 48;          // Keep fleeing this long after the last threat (avoids flee / mine yo-yo)
        bool probeThrottleUnderVultures = true; // Probe production below army / defense while vultures are at one of our bases
        int blindMainCannonFrame = 6000;        // From this frame, main cannon even without a seen mech threat. 0 = off
        int homeGuardDragoons = 1;              // Dragoons kept at the main until the first observer is out. 0 = off
        int homeGuardUntilFrame = 14000;        // Home guard ends at this frame at the latest
        bool observerEmergencyPriority = true;  // Mine seen / drop risk with observatory done: observer at emergency priority
        bool mineHoldUntilObserver = true;      // Army defends the main after a mine is seen until the first observer is out
        int mineHoldMaxFrame = 12000;           // ... but not past this frame
        int plutoRescoutFrame = 4800;           // Second probe scout of the enemy main / natural at this frame. 0 = off
        int plutoRescoutMaxFrames = 1440;       // Give up the re-scout after this many frames
        bool dropRiskDetection = true;          // Starport / control tower / dropship seen early -> treat as drop risk
        int midgameStrategyHoldFrames = 480;    // MidGameMech / Bio / BioMech switches need this many frames since the last change

        // PvP vs. robo / shuttle-reaver and gas-blocked expansion (2026-11-15 Radeon replay)
        bool pvpRoboFix = true;                 // Master switch for the PvP changes below
        bool pvpNaturalFix = true;              // Expand when gas blocked / floating minerals; keep a safe incomplete natural
        int pvpReexpandFrame = 12000;           // From this frame, take the natural when minerals >= pvp_reexpand_minerals. 0 = off
        int pvpReexpandMinerals = 400;
        bool pvpRoboObserver = true;            // Enemy robo tech seen -> observer without waiting for a second gas
        int pvpHomeGuardDragoons = 2;           // Dragoons kept at the main once a shuttle / reaver / support bay is seen. 0 = off
        int pvpDropCannons = 1;                 // Main mineral line cannons once a shuttle / reaver / support bay is seen (0-2)

        // Command volume / resource order failures
        int arrivedMoveHoldFrames = 48;         // Non-worker unit at its move target: same move not re-sent for N frames. 0 = off
        int arrivedMoveTolerancePx = 16;        // ... when the new target is within this distance of the last one
        int resourceFailBackoffFrames = 24;     // Worker resource order rejected by BWAPI: no resource order for N frames. 0 = off
        int supplyDropEvent = 20;               // [EVENT] supply_drop when supply falls by >= N within 30 game seconds. 0 = off

        // Stacked minerals (OdysseyRE 2.0: several mineral fields on the same tile right next to the nexus)
        bool stackedMineralIdentityFix = true;  // Track mineral fields per unit ID instead of one per build tile (applied at
                                                // game start; upstream kept only the first field of a tile)
        bool mineralAssignmentFix = true;       // Patch assignment accepts distance 0 (patch touching the depot), ties broken
                                                // by ID; a miner without a patch near its base is not re-sent to the depot
        bool miningAssignmentLog = true;        // [MINING] lines: resource registry, base patches, worker snapshots, failures

        // Army / expansion defense (2026-11-15 Backrooms 1.1: rear army stuck at a home spider mine, 3rd / 4th lost)
        bool armyStateLog = true;               // [ARMY] cluster snapshots and [DEFENSE] base defense decisions
        int armyStateIntervalFrames = 720;      // [ARMY] snapshot interval (frames)
        bool homeMineClear = true;              // Known burrowed spider mines near our bases need detection (observer + kill)
        int homeMineClearRadiusPx = 960;        // ... within this distance of one of our bases
        int rearClusterUnstickFrames = 480;     // Non-vanguard cluster near home regrouping this long with no threat except
                                                // mines -> forced link-up with the vanguard. 0 = off
        int rearClusterHomeRadiusPx = 1280;     // "Near home" = within this distance of our main or natural
        bool expansionSelectFix = true;         // vs Terran: weight distance from our bases more, skip far bases
        int expansionMaxDistancePx = 2500;      // ... bases farther than this (ground) from all our bases go last. 0 = no limit
        int expansionGuardDragoons = 2;         // vs mech: dragoons kept at every 3rd+ base. 0 = off
        bool expansionDefenseRedirect = true;   // Main army defends a 3rd+ base under a real attack when it is strong enough
        double defenseRedirectRatio = 1.2;      // ... main army value >= ratio x enemy value at the base
        int defenseRedirectMinDragoons = 3;     // ... enemy value at the base >= this many dragoons
        bool expansionCannonAtNexusStart = true; // Expansion cannons requested when the nexus starts, not when it completes
        int cannonRebuildBackoffFrames = 480;   // Cannon tile not re-requested for N frames after a cannon there was lost. 0 = off

        // Phase 3: Lightweight log
        bool printBuildInfo = true;             // Local-only in-game message at game start: build id and config source
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

    // Highest latency observed this game (updated by commandIntervalFrames / noteLatency)
    int maxLatencyFrames();
    void noteLatency();

    // Latency used by unit micro instead of BWAPI::Broodwar->getLatencyFrames() / getRemainingLatencyFrames()
    // (micro_latency_frames, see BnetPolicy::effectiveMicroLatency)
    int microLatencyFrames();
    int microRemainingLatencyFrames();

    // Upper bound for per-unit prediction buffers (latency + margin), independent of when the unit was created
    int predictionBufferFrames();
}
