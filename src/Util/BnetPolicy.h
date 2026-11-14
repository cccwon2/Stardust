#pragma once

// Pure (BWAPI-free) decision helpers for the Battle.net / Pluto snipe changes.
// Kept header-only and free of game dependencies so the branch logic can be unit-tested standalone
// (see tools/bnet-policy-test/BnetPolicyTest.cpp).
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
}
