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
}
