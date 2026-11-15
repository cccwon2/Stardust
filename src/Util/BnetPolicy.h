#pragma once

// Pure (BWAPI-free) decision helpers for the Battle.net / Pluto snipe changes.
// Kept header-only and free of game dependencies so the branch logic can be unit-tested standalone
// (see tools/bnet-policy-test/BnetPolicyTest.cpp).
#include <vector>

namespace BnetPolicy
{
    // ---------------------------------------------------------------------------------------------
    // Stuck gathering worker detection
    // ---------------------------------------------------------------------------------------------

    enum class GatherPhase
    {
        Other,              // Any order we do not monitor (mining, returning, building, moving elsewhere, ...)
        MovingToMinerals,   // Order MoveToMinerals
        WaitingForMinerals  // Order WaitForMinerals
    };

    struct StuckInput
    {
        int frame;
        bool carrying;                  // Carrying minerals or gas
        GatherPhase phase;
        bool patchBeingMinedByOther;    // The worker's patch is currently being mined by another worker (normal queueing)
        int x;
        int y;
        const void *patch;              // Identity of the assigned mineral patch
    };

    struct StuckState
    {
        bool valid = false;
        int x = 0;
        int y = 0;
        const void *patch = nullptr;
        int lastCheckFrame = -1;
        int sinceFrame = 0;
    };

    // Updates the per-worker detection state for this frame and returns true if the worker should be considered stuck.
    // Only cargo-less workers approaching or waiting at their patch are monitored; normal mining, carrying cargo,
    // queueing behind a worker that is actively mining, a changed patch, movement, or a gap in observation
    // (job change, reserved, etc.) all reset the timer.
    inline bool updateStuck(StuckState &state, const StuckInput &in, int thresholdFrames)
    {
        bool monitored = !in.carrying &&
                         (in.phase == GatherPhase::MovingToMinerals ||
                          (in.phase == GatherPhase::WaitingForMinerals && !in.patchBeingMinedByOther));

        int dx = in.x - state.x;
        int dy = in.y - state.y;
        bool moved = (dx > 2 || dx < -2 || dy > 2 || dy < -2);
        bool gap = state.lastCheckFrame != in.frame - 1;

        bool reset = !monitored || !state.valid || gap || moved || state.patch != in.patch;
        state.lastCheckFrame = in.frame;
        if (reset)
        {
            state.valid = monitored;
            state.x = in.x;
            state.y = in.y;
            state.patch = in.patch;
            state.sinceFrame = in.frame;
            return false;
        }

        return (in.frame - state.sinceFrame) >= thresholdFrames;
    }

    // Whether a stuck-recovery command may be attempted for a worker whose last recovery attempt was at lastRecoveryFrame
    inline bool recoveryAllowed(int lastRecoveryFrame, int frame, int cooldownFrames)
    {
        if (lastRecoveryFrame < 0) return true;
        return (frame - lastRecoveryFrame) >= cooldownFrames;
    }

    // Outcome of issuing a recovery gather command
    enum class RecoveryCommandResult
    {
        Issued,
        BudgetExhausted,    // max_orders_per_frame reached this frame; retry soon
        Rejected            // BWAPI rejected the command, an order was already issued this frame, or the target is gone
    };
    // ---------------------------------------------------------------------------------------------
    // PvT first-observer policy (early_observer_priority)
    // ---------------------------------------------------------------------------------------------

    enum class DetectionStage
    {
        Upstream,           // No early-detection condition (or feature off / first observer already out): upstream policy decides
        Preventive,         // Single vulture / machine shop / timing: request at normal priority
        ElevatedWaiting,    // Two-factory or many vultures, but fewer dragoons than observer_min_dragoons: normal priority
        Elevated,           // Two-factory or many vultures with enough dragoons: high priority
        Urgent              // Spider mine seen or researched: high priority regardless of dragoons or rush defense
    };

    inline const char *detectionStageName(DetectionStage stage)
    {
        switch (stage)
        {
            case DetectionStage::Upstream: return "upstream";
            case DetectionStage::Preventive: return "preventive";
            case DetectionStage::ElevatedWaiting: return "elevated_waiting_dragoons";
            case DetectionStage::Elevated: return "elevated";
            case DetectionStage::Urgent: return "urgent";
        }
        return "unknown";
    }

    struct DetectionInput
    {
        bool enabled;               // pluto_pvt_snipe && early_observer_priority
        int completedObservers;     // Callers already return early while an observer is in production
        bool mineSeenOrResearched;
        bool twoFactoryOrMech;      // Enemy strategy TwoFactory / MidGameMech / MidGameBioMech
        int enemyVultures;
        bool vultureOrMachineShopSeen;
        bool antiMarineRush;        // Our strategy is AntiMarineRush
        bool timingReached;         // Frame > 6500 or dragoon range started
        int completedDragoons;
        int minDragoons;            // observer_min_dragoons
    };

    // Decides how the first observer is requested. Once any observer exists (or no condition applies), returns Upstream so
    // the unchanged upstream policy handles additional observers and replacement after losses at its own priorities.
    inline DetectionStage decideDetection(const DetectionInput &in)
    {
        if (!in.enabled) return DetectionStage::Upstream;
        if (in.completedObservers > 0) return DetectionStage::Upstream;

        // Real mine threat: first detection is urgent even while holding a marine rush (mines kill goons and probes outright)
        if (in.mineSeenOrResearched) return DetectionStage::Urgent;

        // While holding a marine rush, weaker mech signs never divert gas / robo investment from dragoons
        if (in.antiMarineRush) return DetectionStage::Upstream;

        if (in.twoFactoryOrMech || in.enemyVultures >= 3)
        {
            return in.completedDragoons >= in.minDragoons ? DetectionStage::Elevated : DetectionStage::ElevatedWaiting;
        }

        if (in.vultureOrMachineShopSeen || in.timingReached) return DetectionStage::Preventive;

        return DetectionStage::Upstream;
    }

    inline bool detectionStageIsHighPriority(DetectionStage stage)
    {
        return stage == DetectionStage::Urgent || stage == DetectionStage::Elevated;
    }
    // ---------------------------------------------------------------------------------------------
    // MidGameBio attack aggression scales (AttackBaseSquad)
    // ---------------------------------------------------------------------------------------------

    struct BioScaleKeys
    {
        bool hasLegacy = false;     // bio_aggression_scale (pre-split key)
        double legacy = 0.7;
        bool hasStart = false;      // bio_attack_start_scale
        double start = 0.7;
        bool hasContinue = false;   // bio_attack_continue_scale
        double cont = 1.0;
        bool hasResume = false;     // bio_attack_resume_scale
        double resume = 0.7;
    };

    struct BioScales
    {
        double start;
        double cont;
        double resume;
    };

    constexpr double defaultBioStartScale = 0.7;
    constexpr double defaultBioContinueScale = 1.0;
    constexpr double defaultBioResumeScale = 0.7;

    // Compatibility policy:
    // - No keys: start 0.7, continue 1.0, resume 0.7.
    // - Only legacy bio_aggression_scale=X: start X, resume X, continue 1.0 (the legacy key no longer affects continue).
    // - New keys present: each new key wins; missing start/resume fall back to the legacy key, then the default;
    //   missing continue is always 1.0 (never the legacy value).
    inline BioScales resolveBioScales(const BioScaleKeys &keys)
    {
        double legacyOrStart = keys.hasLegacy ? keys.legacy : defaultBioStartScale;
        double legacyOrResume = keys.hasLegacy ? keys.legacy : defaultBioResumeScale;
        return BioScales{
                keys.hasStart ? keys.start : legacyOrStart,
                keys.hasContinue ? keys.cont : defaultBioContinueScale,
                keys.hasResume ? keys.resume : legacyOrResume};
    }
    // ---------------------------------------------------------------------------------------------
    // Mineral line cannon placement requests (PvT::handlePlutoCannons)
    // ---------------------------------------------------------------------------------------------

    enum class CannonTile
    {
        Empty,          // Free: a cannon may be requested here
        Cannon,         // Our completed cannon
        CannonPending,  // Our cannon under construction, or a building already queued in the builder at this tile
        Occupied        // Another of our buildings occupies the tile (cannot be used)
    };

    struct CannonPlan
    {
        int existing = 0;           // Completed + under construction / queued cannons counted towards the target
        int requestIndices[8] = {}; // Indices of Empty tiles to request this frame
        int requestCount = 0;
        int shortfall = 0;          // Target that cannot be met with the available locations
    };

    // Plans conditional cannon requests for up to `target` cannons over the predefined locations.
    // Existing and pending cannons count first; occupied tiles are skipped; the remainder is requested on empty tiles.
    inline CannonPlan planCannons(const CannonTile *tiles, int tileCount, int target)
    {
        CannonPlan plan;
        if (target <= 0) return plan;

        for (int i = 0; i < tileCount; i++)
        {
            if (tiles[i] == CannonTile::Cannon || tiles[i] == CannonTile::CannonPending) plan.existing++;
        }

        int needed = target - plan.existing;
        for (int i = 0; i < tileCount && needed > 0 && plan.requestCount < 8; i++)
        {
            if (tiles[i] != CannonTile::Empty) continue;
            plan.requestIndices[plan.requestCount++] = i;
            needed--;
        }

        plan.shortfall = needed > 0 ? needed : 0;
        return plan;
    }

    // main_cannons compatibility: absent -> 1 if natural_cannons > 0 else 0 (natural_cannons=0 used to disable all cannons)
    inline int resolveMainCannons(bool mainKeyPresent, int mainCannons, int naturalCannons)
    {
        if (mainKeyPresent) return mainCannons;
        return naturalCannons > 0 ? 1 : 0;
    }

    // ---------------------------------------------------------------------------------------------
    // PvT vs. MidGameMech (pvt_mech_fix)
    // ---------------------------------------------------------------------------------------------

    // Baseline main-army units requested above other production: one per `workersPerUnit` mineral workers
    // (upstream uses 6). Non-positive divisor disables the baseline.
    inline int armyBaselineUnits(int mineralWorkers, int workersPerUnit)
    {
        if (workersPerUnit <= 0 || mineralWorkers <= 0) return 0;
        return mineralWorkers / workersPerUnit;
    }

    // Desired zealot count against mech: at most `ratio` zealots per dragoon and at most two per enemy tank.
    // Upstream used min(1.5 * dragoons, 3 * tanks) (min(dragoons, 2 * tanks) with many vultures).
    inline int mechZealotTarget(int dragoons, int enemyTanks, double ratio)
    {
        if (dragoons < 0) dragoons = 0;
        if (enemyTanks < 0) enemyTanks = 0;
        if (ratio < 0.0) ratio = 0.0;
        int byRatio = (int)(dragoons * ratio);
        int byTanks = enemyTanks * 2;
        return byRatio < byTanks ? byRatio : byTanks;
    }

    // Whether a new normal expansion may be queued against mech. The first three bases are never gated; from the
    // fourth on, the army (completed zealots + dragoons) must be at least `armyPerBase` units per base we own.
    // armyPerBase <= 0 disables the gate.
    inline bool mechExpansionAllowed(int ownedBases, int armyUnits, int armyPerBase)
    {
        if (armyPerBase <= 0) return true;
        if (ownedBases < 3) return true;
        return armyUnits >= armyPerBase * ownedBases;
    }

    // Luxury upgrades (observer speed / sight, shuttle speed) are deferred against mech until the army reaches
    // `minArmy` units. minArmy <= 0 disables the deferral.
    inline bool mechLuxuryUpgradeAllowed(int armyUnits, int minArmy)
    {
        if (minArmy <= 0) return true;
        return armyUnits >= minArmy;
    }

    // ---------------------------------------------------------------------------------------------
    // Command interval and repeated move guard
    // ---------------------------------------------------------------------------------------------

    // Auto min_command_interval_frames from the highest latency observed this game. Remastered can report 5 at game
    // start and 3 later; latching the maximum keeps the guard from silently turning off mid-game.
    inline int autoCommandInterval(int maxLatencyFrames)
    {
        return maxLatencyFrames > 4 ? maxLatencyFrames + 2 : 0;
    }

    struct MoveGuardInput
    {
        bool enabled;               // combat_move_guard and interval > 0
        bool force;                 // Caller forced the move
        bool stuck;                 // BWAPI reports the unit stuck
        bool lastWasMove;           // The last command was a Move
        int distToLastTarget;       // Approx distance between the new and the last move target (px)
        int tolerance;              // move_resend_tolerance_px
        int framesSinceLastCommand;
        int interval;               // Effective command interval in frames
        int distUnitToLastTarget = 1 << 20;   // Distance from the unit to the last move target (px)
        int minRemainingPx = 0;               // Only skip while the unit still has at least this far to go
    };

    // Whether a non-worker move to (nearly) the same target as the last move should be skipped because it was sent
    // less than `interval` frames ago. Upstream resends whenever the target moved more than 3px, which with per-frame
    // target updates means one move per unit per frame (~20,000 APM observed on Battle.net).
    inline bool skipRepeatedMove(const MoveGuardInput &in)
    {
        if (!in.enabled || in.force || in.stuck || !in.lastWasMove) return false;
        if (in.interval <= 0) return false;
        if (in.distToLastTarget > in.tolerance) return false;
        // Never let the unit run out of path: once it is close to its last target, accept the new one immediately
        if (in.distUnitToLastTarget < in.minRemainingPx) return false;
        return in.framesSinceLastCommand < in.interval;
    }

    // Effective interval for the non-worker move guard: only active when the latency-based command interval is active
    // (Battle.net), then at least combat_move_interval_frames, or observer_move_interval_frames for observers.
    inline int combatMoveInterval(int commandInterval, int combatInterval, int observerInterval, bool isObserver)
    {
        if (commandInterval <= 0) return 0;
        int result = commandInterval;
        if (combatInterval > result) result = combatInterval;
        if (isObserver && observerInterval > result) result = observerInterval;
        return result;
    }

    // ---------------------------------------------------------------------------------------------
    // Grid waypoint hold
    // ---------------------------------------------------------------------------------------------

    // Upstream grid navigation moves towards the node 5 tiles ahead and re-targets every time the unit enters a new tile,
    // i.e. one move per tile (~every 6-8 frames for a dragoon). Keep the current waypoint while it is still on the path
    // ahead and the unit is not yet close to it, so the move is only refreshed every few tiles.
    struct GridHoldInput
    {
        bool enabled;               // grid_waypoint_hold and Battle.net command interval active
        bool waypointOnPath;        // Current waypoint is one of the next nodes from the unit's current grid node
        bool nearGoal;              // Current grid node is close to the grid goal (switch to the final target soon)
        int distToWaypoint;         // Distance from the unit to the current waypoint (px)
        int refreshPx;              // grid_waypoint_refresh_px
    };

    inline bool holdGridWaypoint(const GridHoldInput &in)
    {
        if (!in.enabled || !in.waypointOnPath || in.nearGoal) return false;
        return in.distToWaypoint > in.refreshPx;
    }

    // ---------------------------------------------------------------------------------------------
    // Repeated spell / unload guard
    // ---------------------------------------------------------------------------------------------

    // Whether the same targeted order (stasis, unload) was sent recently enough that re-sending it now only adds APM.
    // Upstream re-sent stasis every frame (148 casts in one replay) and shuttle unloads every frame.
    inline bool skipRepeatedOrder(bool enabled, bool sameOrder, int distToLastTarget, int tolerance, int framesSinceLastCommand,
                                  int interval)
    {
        if (!enabled || !sameOrder || interval <= 0) return false;
        if (distToLastTarget > tolerance) return false;
        return framesSinceLastCommand < interval;
    }

    // ---------------------------------------------------------------------------------------------
    // Stuck worker backoff
    // ---------------------------------------------------------------------------------------------

    // Frames to wait after an unstick stop before the next normal command, given how many consecutive unstick attempts
    // were made without the unit moving. Upstream retried every ~12 frames forever (a trapped probe sent 323 stops and
    // 619 moves in 3 minutes). The first two attempts keep upstream timing; then 24, 48, 96, capped at maxFrames.
    inline int unstickBackoffFrames(int consecutiveAttempts, int baseFrames, int maxFrames)
    {
        if (consecutiveAttempts <= 2) return baseFrames;
        int wait = 24;
        for (int i = 3; i < consecutiveAttempts && wait < maxFrames; i++) wait *= 2;
        if (wait > maxFrames) wait = maxFrames;
        return wait > baseFrames ? wait : baseFrames;
    }

    // ---------------------------------------------------------------------------------------------
    // Expansion / natural cannons vs. mech
    // ---------------------------------------------------------------------------------------------

    enum class CannonBaseKind { Main, Natural, Expansion };

    // Target cannon count for a base. Natural: natural_cannons, raised to mech_natural_cannons under a mech / vulture threat.
    // Main: main_cannons under a mech threat. Other bases: expansion_cannons under a mech threat.
    inline int cannonTarget(CannonBaseKind kind, bool mechThreat, int naturalCannons, int mechNaturalCannons, int mainCannons,
                            int expansionCannons)
    {
        switch (kind)
        {
            case CannonBaseKind::Natural:
                return (mechThreat && mechNaturalCannons > naturalCannons) ? mechNaturalCannons : naturalCannons;
            case CannonBaseKind::Main:
                return mechThreat ? mainCannons : 0;
            case CannonBaseKind::Expansion:
                return mechThreat ? expansionCannons : 0;
        }
        return 0;
    }

    // Whether the next cannon request at a base should be raised above army production: only the first cannon of a base
    // that has none completed or pending, and only under a vulture / mech threat.
    inline bool cannonRequestUrgent(bool threatPriority, bool mechThreat, int existingOrPending, int requestIndex)
    {
        return threatPriority && mechThreat && existingOrPending + requestIndex == 0;
    }
    // ---------------------------------------------------------------------------------------------
    // PvZ vs. zergling all-ins
    // ---------------------------------------------------------------------------------------------

    // Frames after which the extra all-in rules no longer trigger and an existing latch expires
    constexpr int PvzAllInBaseLingsUntilFrame = 9000;
    constexpr int PvzAllInSeenLingsUntilFrame = 8000;
    constexpr int PvzAllInLatchUntilFrame = 10000;

    // Extra all-in confirmation on top of the upstream timing rules, which only count zerglings by their estimated creation
    // frame and so confirmed the 12-pool speedling all-in of 2026-11-15 only after it had broken the natural (frame 6782).
    // Only once the opening is known (pool or hatchery first): zerglings at our main / natural, or many living zerglings.
    inline bool pvzAllInByLings(bool enabled, bool openingKnown, int frame, int lingsAtOurBases, int lingsAlive,
                                int baseThreshold, int seenThreshold)
    {
        if (!enabled || !openingKnown) return false;
        if (baseThreshold > 0 && frame < PvzAllInBaseLingsUntilFrame && lingsAtOurBases >= baseThreshold) return true;
        if (seenThreshold > 0 && frame < PvzAllInSeenLingsUntilFrame && lingsAlive >= seenThreshold) return true;
        return false;
    }

    // Whether a latched all-in confirmation still holds (latchFrame < 0 = not latched)
    inline bool pvzAllInLatchActive(int latchFrame, int frame)
    {
        return latchFrame >= 0 && frame >= latchFrame && frame < PvzAllInLatchUntilFrame;
    }

    // FFE wall cannon target: upstream 2, raised to pool_first_cannons vs. pool first or an all-in
    // (the all-in schedule itself is upstream: up to 5 by frame 6000)
    inline int pvzWallCannonTarget(bool enabled, bool poolFirstOrAllIn, int poolFirstCannons)
    {
        if (!enabled || !poolFirstOrAllIn) return 2;
        return poolFirstCannons > 2 ? poolFirstCannons : 2;
    }

    // FFEDragoons minimum zealots: pool first -> pool_first_zealots; all-in -> allin_zealots or one per three known lings (at most 8)
    inline int pvzMinZealots(bool enabled, bool poolFirst, bool allIn, int poolFirstZealots, int allInZealots, int enemyLings)
    {
        if (!enabled) return 0;
        if (allIn)
        {
            int byLings = (enemyLings + 2) / 3;
            if (byLings > 8) byLings = 8;   // the rest stays at upstream base-defense priority
            int target = allInZealots > byLings ? allInZealots : byLings;
            return target > poolFirstZealots ? target : poolFirstZealots;
        }
        return poolFirst ? poolFirstZealots : 0;
    }

    // Probe count above which FFE probe production is moved below zealots during an all-in (0 = no cap)
    inline int pvzProbeCap(bool enabled, bool allIn, int cap)
    {
        return (enabled && allIn && cap > 0) ? cap : 0;
    }

    // ---------------------------------------------------------------------------------------------
    // Micro latency (PvT combat fix)
    // ---------------------------------------------------------------------------------------------

    // Remastered reports getLatencyFrames() = 5 at game start and 3 later in the game, while commands on Battle.net
    // actually take 5-6 frames to execute. Upstream micro (dragoon attack windows, cooldown deferral, kiting, position
    // prediction) is tuned on the reported value, so with 3 it issues moves that cancel dragoon attacks before the shot.
    // configured: micro_latency_frames (-1 = auto, 0 = off / reported value, >0 = forced).
    // Auto uses the highest latency seen this game, but only in high-latency (Battle.net) environments.
    inline int effectiveMicroLatency(int configured, int reportedLatency, int maxSeenLatency)
    {
        int result = reportedLatency;
        if (configured > 0)
        {
            result = configured;
        }
        else if (configured < 0 && maxSeenLatency > 4)
        {
            result = maxSeenLatency;
        }
        if (result < reportedLatency) result = reportedLatency;
        if (result < 1) result = 1;
        if (result > 12) result = 12;
        return result;
    }

    // Remaining latency frames for the effective latency: the reported remaining value shifted by the extra latency
    inline int effectiveRemainingLatency(int reportedRemaining, int reportedLatency, int effectiveLatency)
    {
        int extra = effectiveLatency - reportedLatency;
        if (extra < 0) extra = 0;
        int result = reportedRemaining + extra;
        if (result < 0) result = 0;
        if (result > effectiveLatency) result = effectiveLatency;
        return result;
    }

    // Safe index for position prediction buffers sized at unit creation: clamps frames to [1, size]
    inline int clampPredictionFrames(int frames, int size)
    {
        if (size < 1) return 0;
        if (frames < 1) return 1;
        if (frames > size) return size;
        return frames;
    }

    // In-flight move guard: a non-worker move to (nearly) the same target as the previous move is pointless while the
    // previous move has not even reached the game yet (framesSinceLastCommand <= effective latency). Upstream resent
    // such moves exactly in that window ("lastCommandFrame > currentFrame - LF - 3" -> resend), producing up to one move
    // per unit per frame in fights.
    inline bool skipInFlightMove(bool enabled, bool force, bool stuck, bool lastWasMove, int distToLastTarget, int tolerance,
                                 int framesSinceLastCommand, int effectiveLatency)
    {
        if (!enabled || force || stuck || !lastWasMove) return false;
        if (distToLastTarget > tolerance) return false;
        return framesSinceLastCommand <= effectiveLatency;
    }

    // ---------------------------------------------------------------------------------------------
    // PvT combat commitment (attack <-> regroup hysteresis)
    // ---------------------------------------------------------------------------------------------

    // Attacking cluster whose sim now says retreat: keep attacking while the attack is younger than commitFrames and
    // the sim does not predict losing more than maxLoss of our army. Turning around under fire against vultures /
    // tanks loses units for nothing; the log showed ~35 attack<->regroup flips per minute late in a lost game.
    inline bool holdAttackCommit(bool enabled, int framesSinceActivityChange, int commitFrames, double myPercentLost, double maxLoss)
    {
        if (!enabled || commitFrames <= 0) return false;
        if (framesSinceActivityChange >= commitFrames) return false;
        return myPercentLost < maxLoss;
    }

    // Upstream aborts an attack once the retreat recommendation has been stable for 6 frames (one command latency on
    // Battle.net). Effective stable-frame requirement before aborting.
    inline int attackAbortStableFrames(bool enabled, int configured)
    {
        if (!enabled) return 6;
        return configured > 6 ? configured : 6;
    }

    // Regrouping cluster whose sim now says attack: stay regrouping while the regroup is younger than commitFrames,
    // so a cluster that just turned around does not immediately turn back into the same fight.
    inline bool holdRegroupCommit(bool enabled, int framesSinceActivityChange, int commitFrames)
    {
        if (!enabled || commitFrames <= 0) return false;
        return framesSinceActivityChange < commitFrames;
    }

    // ---------------------------------------------------------------------------------------------
    // Flee guard
    // ---------------------------------------------------------------------------------------------

    struct FleeGuardInput
    {
        bool enabled;               // flee_guard and enemy Terran (pluto_pvt_snipe)
        int myUnits;                // Mobile ground units in the cluster
        int engagedPursuers;        // Armed enemy ground units within reach of our units
        int fastPursuers;           // ... of which are faster than our slowest unit by speedRatio (vultures)
        bool siegedTankInRange;     // A sieged tank covers the cluster (fleeing out of tank range is right)
        double myPercentLost;       // Combat sim: share of our army lost if we fight
        double maxLoss;             // flee_guard_max_loss
    };

    // Whether a cluster that wants to flee should fight instead: the units engaging us are mostly faster than us, so
    // fleeing just gives them free shots (dragoons running from vultures), and the fight is not hopeless.
    inline bool fightInsteadOfFlee(const FleeGuardInput &in)
    {
        if (!in.enabled) return false;
        if (in.myUnits < 2 || in.engagedPursuers <= 0) return false;
        if (in.siegedTankInRange) return false;
        if (in.fastPursuers * 2 < in.engagedPursuers) return false;
        return in.myPercentLost < in.maxLoss;
    }

    // ---------------------------------------------------------------------------------------------
    // PvT vs. vulture drops / runbys and spider mines (2026-11-15 KnockOut analysis)
    // ---------------------------------------------------------------------------------------------

    // Observer production priority tier for the first-observer stages (observer_emergency_priority).
    // Emergency = PRIORITY_EMERGENCY (above probes), SpecialTeams = PRIORITY_SPECIALTEAMS, Normal = PRIORITY_NORMAL.
    enum class ObserverPriority { Emergency, SpecialTeams, Normal };

    inline ObserverPriority observerPriority(DetectionStage stage, bool emergencyEnabled, bool observatoryComplete,
                                             bool roboticsComplete)
    {
        if (!emergencyEnabled)
        {
            return detectionStageIsHighPriority(stage) ? ObserverPriority::SpecialTeams : ObserverPriority::Normal;
        }
        switch (stage)
        {
            case DetectionStage::Urgent:
                return ObserverPriority::Emergency;
            case DetectionStage::Elevated:
                return observatoryComplete ? ObserverPriority::Emergency : ObserverPriority::SpecialTeams;
            case DetectionStage::ElevatedWaiting:
            case DetectionStage::Preventive:
                return roboticsComplete ? ObserverPriority::SpecialTeams : ObserverPriority::Normal;
            case DetectionStage::Upstream:
                break;
        }
        return ObserverPriority::Normal;
    }

    // Main mineral line cannon without a seen mech threat (blind_main_cannon_frame): Pluto's 1-1-1 drop is often not
    // scouted, so from the given frame the main gets its cannon anyway, unless we are holding early bio aggression.
    inline bool blindMainCannonDue(int frame, int blindFrame, bool earlyBioOrRush)
    {
        return blindFrame > 0 && frame >= blindFrame && !earlyBioOrRush;
    }

    // Home guard (home_guard_dragoons): keep dragoons at the main until the first observer is out
    inline bool homeGuardActive(bool snipe, int dragoons, int frame, int untilFrame, int completedObservers)
    {
        return snipe && dragoons > 0 && frame < untilFrame && completedObservers == 0;
    }

    // DefendBase::update: upstream releases every ground unit when the squad needs detection and has no
    // observer. That left probes alone against a visible vulture while a remembered mine also flagged
    // detection (KnockOut 1.4, bnet-pluto@908059a0). Keep defenders when a visible fightable threat is
    // present; still release when detection is missing and only undetectable threats remain.
    inline bool defendBaseShouldReleaseForMissingDetection(bool needsDetection, bool hasDetector,
                                                           bool hasVisibleFightableThreat)
    {
        if (!needsDetection || hasDetector) return false;
        return !hasVisibleFightableThreat;
    }

    // DefendBase "enemy value" that makes DefendBase request about `dragoons` units: it requests zealot-value units until
    // ourValue >= enemyValue * 6 / 5, so a value just below dragoons * zealotValue * 5 / 6 asks for that many.
    inline int homeGuardValue(int dragoons, int zealotValue)
    {
        if (dragoons <= 0 || zealotValue <= 0) return 0;
        int value = (dragoons * zealotValue * 5) / 6 - 1;
        return value > 1 ? value : 1;
    }

    // Mine hold (mine_hold_until_observer): army defends the main after a mine is seen until the first observer is out
    inline bool mineHoldActive(bool snipe, bool enabled, bool mineSeen, int completedObservers, int frame, int maxFrame)
    {
        return snipe && enabled && mineSeen && completedObservers == 0 && frame < maxFrame;
    }

    // Mid-game strategy hysteresis (midgame_strategy_hold_frames): a switch between two MidGame* strategies is only
    // accepted once the current one has been held this long. Switches into or out of the mid game are never delayed.
    inline bool midgameSwitchAllowed(bool currentIsMidGame, bool newIsMidGame, int framesSinceLastChange, int holdFrames)
    {
        if (holdFrames <= 0) return true;
        if (!currentIsMidGame || !newIsMidGame) return true;
        return framesSinceLastChange >= holdFrames;
    }

    // Drop risk: starport, control tower or dropship seen before the given frame
    inline bool dropRisk(bool enabled, bool starport, bool controlTower, bool dropship, int frame, int untilFrame)
    {
        if (!enabled || frame >= untilFrame) return false;
        return starport || controlTower || dropship;
    }

    // ---------------------------------------------------------------------------------------------
    // PvP vs. robo / shuttle-reaver and gas-blocked expansion (2026-11-15 Radeon replay)
    // ---------------------------------------------------------------------------------------------

    enum class PvpNaturalReason { None, KeepIncomplete, GasBlocked, MineralFloat };

    inline const char *pvpNaturalReasonName(PvpNaturalReason reason)
    {
        switch (reason)
        {
            case PvpNaturalReason::None: return "none";
            case PvpNaturalReason::KeepIncomplete: return "keep_incomplete";
            case PvpNaturalReason::GasBlocked: return "gas_blocked";
            case PvpNaturalReason::MineralFloat: return "mineral_float";
        }
        return "none";
    }

    struct PvpNaturalInput
    {
        bool enabled;               // pvp_robo_fix && pvp_natural_fix
        bool underConstruction;     // our natural nexus is placed but not completed
        int frame;
        int naturalFrame;           // upstream earliest natural frame (10000, 6500 backdoor, 12000 vs. dragoon all-in)
        int minerals;
        int gas;
        int reexpandFrame;          // pvp_reexpand_frame (0 = off)
        int reexpandMinerals;       // pvp_reexpand_minerals
        int enemyValueAtNatural;    // CombatSim value of enemy combat units at the natural
        int threatValue;            // At or above this, upstream decides (cancel / wait)
    };

    // Overrides the upstream PvP natural decision (Normal / ThreeGateRobo / MidGame) when the expansion is safe:
    // upstream only expanded with an attack squad of 5+ units beyond the natural, checked the gas-blocked case after
    // that, and cancelled an incomplete natural whenever the army went home. Returns None to keep upstream behavior.
    inline PvpNaturalReason pvpNaturalOverride(const PvpNaturalInput &in)
    {
        if (!in.enabled) return PvpNaturalReason::None;
        if (in.threatValue > 0 && in.enemyValueAtNatural >= in.threatValue) return PvpNaturalReason::None;
        if (in.underConstruction) return PvpNaturalReason::KeepIncomplete;
        if (in.frame >= in.naturalFrame && in.minerals > 500 && in.gas < 100) return PvpNaturalReason::GasBlocked;
        if (in.reexpandFrame > 0 && in.frame >= in.reexpandFrame && in.minerals >= in.reexpandMinerals)
        {
            return PvpNaturalReason::MineralFloat;
        }
        return PvpNaturalReason::None;
    }

    // Enemy robotics tech seen (robotics facility, support bay, observatory, shuttle, reaver): get our own observer
    // without waiting for a second gas (upstream required 2 completed assimilators or 2 nexus after frame 10000)
    inline bool pvpRoboTechSeen(bool enabled, bool robo, bool supportBay, bool observatory, bool shuttle, bool reaver)
    {
        return enabled && (robo || supportBay || observatory || shuttle || reaver);
    }

    // Shuttle / reaver drop threat: support bay, shuttle or reaver seen
    inline bool pvpDropThreat(bool enabled, bool supportBay, bool shuttle, bool reaver)
    {
        return enabled && (supportBay || shuttle || reaver);
    }

    // Arrived-unit move guard (arrived_move_hold_frames): a non-worker unit that is already at its move target
    // (within move_guard_min_remaining_px) is not sent the same move again (within arrived_move_tolerance_px) until
    // hold frames have passed. Upstream resent such moves every latency + 3 frames once the unit stopped moving
    // (Radeon replay: 9,617 of 16,128 moves repeated the previous target, most within 4-19 frames).
    inline bool skipArrivedMove(bool enabled, bool force, bool stuck, bool lastWasMove, int distToLastTarget, int tolerance,
                                int framesSinceLastCommand, int holdFrames, int remainingToLastTarget, int arrivedPx)
    {
        if (!enabled || holdFrames <= 0 || force || stuck || !lastWasMove) return false;
        if (distToLastTarget > tolerance) return false;
        if (remainingToLastTarget > arrivedPx) return false;
        return framesSinceLastCommand < holdFrames;
    }

    // Per-worker backoff after BWAPI rejected a resource command (resource_fail_backoff_frames)
    inline bool resourceBackoffActive(int lastFailFrame, int frame, int backoffFrames)
    {
        return backoffFrames > 0 && lastFailFrame >= 0 && (frame - lastFailFrame) < backoffFrames;
    }

    // Supply drop event: our supply fell by at least minDrop from its peak in the trailing window
    inline bool supplyDropDetected(int peakSupply, int currentSupply, int minDrop)
    {
        return minDrop > 0 && (peakSupply - currentSupply) >= minDrop;
    }

    // ---------------------------------------------------------------------------------------------
    // Stacked minerals (OdysseyRE 2.0, 2026-11-15): several mineral fields on one build tile
    // ---------------------------------------------------------------------------------------------

    // Initial resource registration: upstream kept a single resource per build tile, so every further mineral field on
    // the same tile was dropped. With the identity fix every field is registered.
    inline bool registerResourceAtInit(bool identityFix, bool tileAlreadyHasResource)
    {
        return identityFix || !tileAlreadyHasResource;
    }

    struct ResourceKey
    {
        int id;
        bool isMinerals;
        bool destroyed;
    };

    // Picks which registered resource on a tile a visible neutral unit (or refinery) updates. Returns the index into
    // `onTile`, or -1 when none matches (a new resource is then registered).
    // - Minerals match by unit ID; without the identity fix the first mineral resource on the tile is used (upstream).
    // - Geysers / refineries match the (single) non-mineral resource on the tile.
    inline int matchResource(const std::vector<ResourceKey> &onTile, int unitId, bool unitIsMinerals, bool identityFix)
    {
        for (size_t i = 0; i < onTile.size(); i++)
        {
            auto &r = onTile[i];
            if (r.destroyed || r.isMinerals != unitIsMinerals) continue;
            if (!unitIsMinerals || r.id == unitId) return (int)i;
        }
        if (unitIsMinerals && !identityFix)
        {
            for (size_t i = 0; i < onTile.size(); i++)
            {
                if (!onTile[i].destroyed && onTile[i].isMinerals) return (int)i;
            }
        }
        return -1;
    }

    // Mineral patch scoring in Workers::assignMineralPatch. First worker on a patch: lowest score wins; second worker:
    // highest score wins. Upstream started the second-worker search at bestScore = 0 with a strict comparison, so a patch
    // touching the depot (distance score 0) could never get its second worker. With the fix the first candidate is
    // always accepted and equal scores are broken by the lower unit ID (deterministic).
    inline bool mineralScoreBetter(bool fix, bool haveBest, bool lowerIsBetter, int score, int bestScore, int id, int bestId)
    {
        if (fix && !haveBest) return true;
        if (score != bestScore) return lowerIsBetter ? score < bestScore : score > bestScore;
        return fix && id < bestId;
    }

    // Miner in the Minerals / Gas job with nothing to do near its base: upstream cleared its state and re-sent it to the
    // depot every few frames (probes circling the nexus on OdysseyRE). With the fix it only clears its state and waits
    // for the next assignment.
    inline bool sendIdleMinerToDepot(bool fix, bool simpleGather)
    {
        return !(fix && simpleGather);
    }

    // ---------------------------------------------------------------------------------------------
    // Army / expansion defense (2026-11-15 Backrooms 1.1: rear army stuck at a home spider mine, 3rd / 4th lost)
    // ---------------------------------------------------------------------------------------------

    // home_mine_clear: a known burrowed spider mine near one of our bases is added to the main army detection needs
    // (upstream needsDetection() is false for mines, so observers never went to them and the mine stayed "known" forever)
    inline bool homeMineNeedsDetection(bool enabled, bool isSpiderMine, bool burrowed, bool positionValid,
                                       int distToNearestBasePx, int radiusPx)
    {
        return enabled && isSpiderMine && burrowed && positionValid && distToNearestBasePx >= 0
               && distToNearestBasePx <= radiusPx;
    }

    // "Near home": within radius of our main or natural (-1 = unknown distance)
    inline bool clusterNearHome(int distMainPx, int distNaturalPx, int radiusPx)
    {
        if (radiusPx <= 0) return false;
        return (distMainPx >= 0 && distMainPx <= radiusPx) || (distNaturalPx >= 0 && distNaturalPx <= radiusPx);
    }

    // rear_cluster_unstick_frames: a non-vanguard cluster near home that has been regrouping for N frames with no threat
    // except spider mines / undetected units is forced to link up with the vanguard. Once triggered it keeps moving for
    // rearClusterForceFrames so it does not fall back into the regroup state the next frame.
    inline bool rearClusterUnstick(int unstickFrames, bool isVanguard, bool haveOtherVanguard, bool nearHome,
                                   int realThreats, bool regrouping, int regroupFrames, bool forcing)
    {
        if (unstickFrames <= 0 || isVanguard || !haveOtherVanguard || !nearHome || realThreats > 0) return false;
        return forcing || (regrouping && regroupFrames >= unstickFrames);
    }

    inline int rearClusterForceFrames(int unstickFrames)
    {
        int frames = unstickFrames / 2;
        return frames > 120 ? frames : 120;
    }

    // Upstream keeps regrouping while units joined the cluster in the past 72 frames. Near home that never ends while
    // production keeps adding units, so the rule is skipped there (rear_cluster_unstick_frames > 0).
    inline bool skipReinforcementWait(int unstickFrames, bool isVanguard, bool nearHome)
    {
        return unstickFrames > 0 && !isVanguard && nearHome;
    }

    // expansion_select_fix: our own expansion score. Upstream: distEnemy * 3/2 - distUs (+ resources), which on 4-player
    // maps prefers the far corner (Backrooms 1.1: third at the opposite corner, 0-2 defenders). With the fix the distance
    // from our bases weighs twice the distance from the enemy and bases beyond maxDistUs go last.
    inline int expansionScore(bool fix, int distEnemy, int distUs, int minerals, int gas, int maxDistUs)
    {
        int score = fix ? distEnemy - distUs * 2 : (distEnemy * 3) / 2 - distUs;
        score += minerals / 100;
        score += gas / 50;
        if (fix && maxDistUs > 0 && distUs > maxDistUs) score -= 100000;
        return score;
    }

    // expansion_guard_dragoons: dragoons kept at every 3rd+ base vs mech (not during early bio / rush defense)
    inline bool expansionGuardActive(bool snipe, int dragoons, bool mechThreat, bool earlyBioOrRush)
    {
        return snipe && dragoons > 0 && mechThreat && !earlyBioOrRush;
    }

    // expansion_defense_redirect: upstream gives up a 3rd+ base when the enemy value there exceeds 5 dragoons. Redirect
    // the main army instead when the attack is real (>= minEnemyValue) and the army is strong enough (>= ratio x enemy).
    inline bool defenseRedirectStart(bool enabled, int enemyValue, int armyValue, int minEnemyValue, double ratio)
    {
        if (!enabled || enemyValue <= 0 || enemyValue < minEnemyValue) return false;
        return armyValue >= (int)(ratio * enemyValue + 0.5);
    }

    // Keep an active redirect while enemies are at the base or were seen there recently, up to maxFrames in total
    inline bool defenseRedirectKeep(int framesSinceEnemyAtBase, int framesSinceStart, int holdFrames, int maxFrames)
    {
        return framesSinceEnemyAtBase < holdFrames && framesSinceStart < maxFrames;
    }

    // expansion_cannon_at_nexus_start: expansion cannons (and their pylon) are requested once the nexus is placed,
    // not after it completes (Backrooms 1.1: 4th base cannon started 21 seconds before the base was attacked)
    inline bool cannonBaseReady(bool depotExists, bool depotCompleted, bool expansion, bool atNexusStart)
    {
        if (!depotExists) return false;
        if (depotCompleted) return true;
        return expansion && atNexusStart;
    }

    // cannon_rebuild_backoff_frames: a cannon tile is not re-requested for N frames after a cannon there was lost
    // (Backrooms 1.1: six build_cancel_dying at the same tile within one second)
    inline bool cannonTileBackoff(int lostFrame, int frame, int backoffFrames)
    {
        return backoffFrames > 0 && lostFrame >= 0 && frame - lostFrame < backoffFrames;
    }
}
