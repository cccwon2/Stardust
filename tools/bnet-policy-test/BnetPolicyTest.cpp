// Standalone unit tests for src/Util/BnetPolicy.h (no BWAPI / game required).
// Build and run (any C++20 compiler):
//   g++ -std=c++20 -Isrc/Util tools/bnet-policy-test/BnetPolicyTest.cpp -o bnet-policy-test && ./bnet-policy-test
//   cl /std:c++20 /EHsc /Isrc\Util tools\bnet-policy-test\BnetPolicyTest.cpp
// Intentionally outside test/ so it is not picked up by the openbw test target's GLOB.
#include "BnetPolicy.h"

#include <cstdio>
#include <string>

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

using namespace BnetPolicy;

namespace
{
    int patchA = 1;
    int patchB = 2;

    StuckInput in(int frame, GatherPhase phase, bool carrying = false, bool minedByOther = false, int x = 100, int y = 100,
                  const void *patch = &patchA)
    {
        return StuckInput{frame, carrying, phase, minedByOther, x, y, patch};
    }

    // Runs frames [from, to] with the same input shape; returns the first frame reported stuck, or -1
    int runUntilStuck(StuckState &state, int from, int to, GatherPhase phase, int threshold, bool carrying = false,
                      bool minedByOther = false)
    {
        for (int f = from; f <= to; f++)
        {
            if (updateStuck(state, in(f, phase, carrying, minedByOther), threshold)) return f;
        }
        return -1;
    }

    void testStuck()
    {
        // Cargo-less worker standing still while moving to minerals: stuck exactly after the threshold
        {
            StuckState s;
            CHECK(runUntilStuck(s, 100, 400, GatherPhase::MovingToMinerals, 48) == 148);
        }

        // Carrying cargo: never stuck (would otherwise be reassigned to another patch)
        {
            StuckState s;
            CHECK(runUntilStuck(s, 100, 400, GatherPhase::MovingToMinerals, 48, true) == -1);
            StuckState s2;
            CHECK(runUntilStuck(s2, 100, 400, GatherPhase::WaitingForMinerals, 48, true) == -1);
        }

        // Normal mining / returning (phase Other): never stuck
        {
            StuckState s;
            CHECK(runUntilStuck(s, 100, 400, GatherPhase::Other, 48) == -1);
        }

        // Waiting while another worker mines the patch (normal queueing): never stuck
        {
            StuckState s;
            CHECK(runUntilStuck(s, 100, 400, GatherPhase::WaitingForMinerals, 48, false, true) == -1);
        }

        // Waiting at a patch nobody is mining: stuck after the threshold
        {
            StuckState s;
            CHECK(runUntilStuck(s, 100, 400, GatherPhase::WaitingForMinerals, 48) == 148);
        }

        // Picking up cargo resets the timer
        {
            StuckState s;
            CHECK(runUntilStuck(s, 100, 140, GatherPhase::MovingToMinerals, 48) == -1);
            CHECK(!updateStuck(s, in(141, GatherPhase::MovingToMinerals, true), 48));
            CHECK(runUntilStuck(s, 142, 400, GatherPhase::MovingToMinerals, 48) == 190);
        }

        // Observation gap (job change / reserved / not processed) resets: stale state must not fire immediately
        {
            StuckState s;
            CHECK(runUntilStuck(s, 100, 140, GatherPhase::MovingToMinerals, 48) == -1);
            CHECK(!updateStuck(s, in(500, GatherPhase::MovingToMinerals), 48));
            CHECK(runUntilStuck(s, 501, 1000, GatherPhase::MovingToMinerals, 48) == 548);
        }

        // Patch change resets
        {
            StuckState s;
            CHECK(runUntilStuck(s, 100, 140, GatherPhase::MovingToMinerals, 48) == -1);
            CHECK(!updateStuck(s, in(141, GatherPhase::MovingToMinerals, false, false, 100, 100, &patchB), 48));
            bool fired = false;
            for (int f = 142; f < 189; f++) fired |= updateStuck(s, in(f, GatherPhase::MovingToMinerals, false, false, 100, 100, &patchB), 48);
            CHECK(!fired);
            CHECK(updateStuck(s, in(189, GatherPhase::MovingToMinerals, false, false, 100, 100, &patchB), 48));
        }

        // Movement resets; tiny jitter (<= 2px) does not
        {
            StuckState s;
            CHECK(!updateStuck(s, in(100, GatherPhase::MovingToMinerals), 48));
            CHECK(!updateStuck(s, in(101, GatherPhase::MovingToMinerals, false, false, 102, 98), 48));
            CHECK(!updateStuck(s, in(102, GatherPhase::MovingToMinerals, false, false, 110, 100), 48));
            CHECK(s.sinceFrame == 102);
        }
    }

    void testRecoveryCooldown()
    {
        CHECK(recoveryAllowed(-1, 0, 48));
        CHECK(!recoveryAllowed(100, 147, 48));
        CHECK(recoveryAllowed(100, 148, 48));

        // Budget-deferred attempt: Workers stores currentFrame - cooldown + LF + 2, so the retry comes after LF + 2 frames
        int latency = 6;
        int cooldown = 48;
        int deferredAt = 200;
        int stored = deferredAt - cooldown + latency + 2;
        CHECK(!recoveryAllowed(stored, deferredAt + latency + 1, cooldown));
        CHECK(recoveryAllowed(stored, deferredAt + latency + 2, cooldown));
    }

    DetectionInput detection()
    {
        // Baseline: feature on, no observer, nothing seen, not holding a rush, early, 0 dragoons, threshold 2
        return DetectionInput{true, 0, false, false, 0, false, false, false, 0, 2};
    }

    void testDetection()
    {
        // Nothing seen: upstream policy (reachable)
        CHECK(decideDetection(detection()) == DetectionStage::Upstream);

        // Single vulture or machine shop only: preventive (normal priority), not urgent
        auto d = detection();
        d.vultureOrMachineShopSeen = true;
        d.enemyVultures = 1;
        CHECK(decideDetection(d) == DetectionStage::Preventive);
        CHECK(!detectionStageIsHighPriority(decideDetection(d)));

        // Real mine: urgent (high priority), different from the single-vulture case
        d.mineSeenOrResearched = true;
        CHECK(decideDetection(d) == DetectionStage::Urgent);
        CHECK(detectionStageIsHighPriority(decideDetection(d)));

        // Mine while holding a marine rush: still urgent (never forbidden); dragoon minimum does not delay it
        d.antiMarineRush = true;
        d.completedDragoons = 0;
        CHECK(decideDetection(d) == DetectionStage::Urgent);

        // AntiMarineRush with only weak mech signs: upstream (no gas diverted from goons)
        d = detection();
        d.antiMarineRush = true;
        d.vultureOrMachineShopSeen = true;
        d.twoFactoryOrMech = true;
        d.enemyVultures = 4;
        d.timingReached = true;
        CHECK(decideDetection(d) == DetectionStage::Upstream);

        // Two factory: waits for dragoons at normal priority, then high priority
        d = detection();
        d.twoFactoryOrMech = true;
        d.completedDragoons = 1;
        CHECK(decideDetection(d) == DetectionStage::ElevatedWaiting);
        CHECK(!detectionStageIsHighPriority(decideDetection(d)));
        d.completedDragoons = 2;
        CHECK(decideDetection(d) == DetectionStage::Elevated);
        CHECK(detectionStageIsHighPriority(decideDetection(d)));

        // Many vultures without a recognized strategy behave like two-factory
        d = detection();
        d.enemyVultures = 3;
        CHECK(decideDetection(d) == DetectionStage::ElevatedWaiting);
        d.enemyVultures = 2;
        d.vultureOrMachineShopSeen = true;
        CHECK(decideDetection(d) == DetectionStage::Preventive);

        // Timing only: preventive
        d = detection();
        d.timingReached = true;
        CHECK(decideDetection(d) == DetectionStage::Preventive);

        // After the first observer exists, everything returns to upstream (no repeated urgent requests)
        d = detection();
        d.mineSeenOrResearched = true;
        d.completedObservers = 1;
        CHECK(decideDetection(d) == DetectionStage::Upstream);

        // All observers lost again with mines around: urgent again (replacement of first detection)
        d.completedObservers = 0;
        CHECK(decideDetection(d) == DetectionStage::Urgent);

        // Feature off: upstream
        d.enabled = false;
        CHECK(decideDetection(d) == DetectionStage::Upstream);
    }

    bool near(double a, double b) { return a - b < 1e-9 && b - a < 1e-9; }

    void testBioScales()
    {
        // No keys
        BioScaleKeys k;
        auto r = resolveBioScales(k);
        CHECK(near(r.start, 0.7) && near(r.cont, 1.0) && near(r.resume, 0.7));

        // Legacy key only: start/resume take it, continue stays 1.0 (never the legacy 0.7)
        k = BioScaleKeys();
        k.hasLegacy = true;
        k.legacy = 0.7;
        r = resolveBioScales(k);
        CHECK(near(r.start, 0.7) && near(r.cont, 1.0) && near(r.resume, 0.7));
        k.legacy = 0.5;
        r = resolveBioScales(k);
        CHECK(near(r.start, 0.5) && near(r.cont, 1.0) && near(r.resume, 0.5));

        // Legacy + some new keys: new keys win, missing start/resume fall back to legacy, continue to 1.0
        k.hasStart = true;
        k.start = 0.8;
        r = resolveBioScales(k);
        CHECK(near(r.start, 0.8) && near(r.cont, 1.0) && near(r.resume, 0.5));
        k.hasContinue = true;
        k.cont = 0.9;
        k.hasResume = true;
        k.resume = 0.6;
        r = resolveBioScales(k);
        CHECK(near(r.start, 0.8) && near(r.cont, 0.9) && near(r.resume, 0.6));

        // Upstream-equivalent configuration
        k = BioScaleKeys();
        k.hasStart = k.hasContinue = k.hasResume = true;
        k.start = k.cont = k.resume = 1.0;
        r = resolveBioScales(k);
        CHECK(near(r.start, 1.0) && near(r.cont, 1.0) && near(r.resume, 1.0));
    }

    void testCannons()
    {
        using T = CannonTile;
        {
            T tiles[] = {T::Empty, T::Empty};
            auto p = planCannons(tiles, 2, 1);
            CHECK(p.existing == 0 && p.requestCount == 1 && p.requestIndices[0] == 0 && p.shortfall == 0);
        }
        {
            // Completed or pending cannons count; nothing new is requested
            T tiles[] = {T::CannonPending, T::Empty};
            auto p = planCannons(tiles, 2, 1);
            CHECK(p.existing == 1 && p.requestCount == 0 && p.shortfall == 0);
        }
        {
            // Occupied first location: use the next one
            T tiles[] = {T::Occupied, T::Empty};
            auto p = planCannons(tiles, 2, 1);
            CHECK(p.requestCount == 1 && p.requestIndices[0] == 1 && p.shortfall == 0);
        }
        {
            // Not enough locations: shortfall reported, no request on occupied tiles
            T tiles[] = {T::Occupied, T::Cannon};
            auto p = planCannons(tiles, 2, 2);
            CHECK(p.existing == 1 && p.requestCount == 0 && p.shortfall == 1);
        }
        {
            T tiles[] = {T::Empty};
            auto p = planCannons(tiles, 1, 0);
            CHECK(p.requestCount == 0 && p.shortfall == 0);
        }

        CHECK(resolveMainCannons(false, 1, 1) == 1);
        CHECK(resolveMainCannons(false, 1, 0) == 0);   // old natural_cannons=0 still disables the main cannon
        CHECK(resolveMainCannons(true, 1, 0) == 1);    // explicit main_cannons wins
        CHECK(resolveMainCannons(true, 0, 2) == 0);
    }
}

namespace
{
    void testMech()
    {
        // Army baseline: upstream divisor 6 vs. mech default 4
        CHECK(armyBaselineUnits(48, 6) == 8);
        CHECK(armyBaselineUnits(48, 4) == 12);
        CHECK(armyBaselineUnits(0, 4) == 0);
        CHECK(armyBaselineUnits(48, 0) == 0);

        // Zealot target: min(ratio * dragoons, 2 * tanks)
        CHECK(mechZealotTarget(12, 10, 0.5) == 6);
        CHECK(mechZealotTarget(12, 2, 0.5) == 4);
        CHECK(mechZealotTarget(12, 10, 0.0) == 0);
        CHECK(mechZealotTarget(3, 10, 0.5) == 1);
        CHECK(mechZealotTarget(-1, 10, 0.5) == 0);

        // Expansion gate: bases 1-2 never gated, 3+ need perBase * bases
        CHECK(mechExpansionAllowed(2, 0, 6));
        CHECK(!mechExpansionAllowed(3, 17, 6));
        CHECK(mechExpansionAllowed(3, 18, 6));
        CHECK(!mechExpansionAllowed(4, 23, 6));
        CHECK(mechExpansionAllowed(4, 0, 0));    // disabled

        CHECK(!mechLuxuryUpgradeAllowed(19, 20));
        CHECK(mechLuxuryUpgradeAllowed(20, 20));
        CHECK(mechLuxuryUpgradeAllowed(0, 0));
    }

    void testMoveGuard()
    {
        CHECK(autoCommandInterval(3) == 0);
        CHECK(autoCommandInterval(4) == 0);
        CHECK(autoCommandInterval(5) == 7);
        CHECK(autoCommandInterval(6) == 8);

        MoveGuardInput base{true, false, false, true, 4, 24, 1, 7};
        CHECK(skipRepeatedMove(base));                                  // observer creeping 4px/frame: skipped
        auto m = base; m.framesSinceLastCommand = 7; CHECK(!skipRepeatedMove(m));   // interval elapsed: resend
        m = base; m.distToLastTarget = 25; CHECK(!skipRepeatedMove(m)); // new target: send
        m = base; m.force = true; CHECK(!skipRepeatedMove(m));
        m = base; m.stuck = true; CHECK(!skipRepeatedMove(m));
        m = base; m.lastWasMove = false; CHECK(!skipRepeatedMove(m));
        m = base; m.enabled = false; CHECK(!skipRepeatedMove(m));
        m = base; m.interval = 0; CHECK(!skipRepeatedMove(m));          // low latency: upstream behavior
    }
}

int main()
{
    testStuck();
    testRecoveryCooldown();
    testDetection();
    testBioScales();
    testCannons();
    testMech();
    testMoveGuard();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
