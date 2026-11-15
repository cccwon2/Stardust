// Standalone unit tests for src/Util/BnetPolicy.h (no BWAPI / game required).
// Build and run (any C++20 compiler):
//   g++ -std=c++20 -Isrc/Util tools/bnet-policy-test/BnetPolicyTest.cpp -o bnet-policy-test && ./bnet-policy-test
//   cl /std:c++20 /EHsc /Isrc\Util tools\bnet-policy-test\BnetPolicyTest.cpp
// Intentionally outside test/ so it is not picked up by the openbw test target's GLOB.
#include "BnetPolicy.h"

#include <cstdio>
#include <string>
#include <vector>

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

        // Remaining-path rule: close to the last target, always accept the new target
        m = base; m.minRemainingPx = 48; m.distUnitToLastTarget = 100; CHECK(skipRepeatedMove(m));
        m.distUnitToLastTarget = 47; CHECK(!skipRepeatedMove(m));

        // Effective interval
        CHECK(combatMoveInterval(0, 12, 24, false) == 0);   // not Battle.net: upstream
        CHECK(combatMoveInterval(7, 12, 24, false) == 12);
        CHECK(combatMoveInterval(7, 12, 24, true) == 24);
        CHECK(combatMoveInterval(7, 0, 0, true) == 7);
        CHECK(combatMoveInterval(8, 5, 6, false) == 8);
    }

    void testGridHold()
    {
        GridHoldInput in{true, true, false, 120, 64};
        CHECK(holdGridWaypoint(in));
        auto m = in; m.distToWaypoint = 64; CHECK(!holdGridWaypoint(m));   // close enough: refresh
        m = in; m.waypointOnPath = false; CHECK(!holdGridWaypoint(m));     // path changed
        m = in; m.nearGoal = true; CHECK(!holdGridWaypoint(m));            // let upstream switch to the final target
        m = in; m.enabled = false; CHECK(!holdGridWaypoint(m));
    }

    void testRepeatedOrder()
    {
        CHECK(skipRepeatedOrder(true, true, 0, 48, 3, 24));
        CHECK(!skipRepeatedOrder(true, true, 0, 48, 24, 24));
        CHECK(!skipRepeatedOrder(true, true, 49, 48, 3, 24));
        CHECK(!skipRepeatedOrder(true, false, 0, 48, 3, 24));
        CHECK(!skipRepeatedOrder(false, true, 0, 48, 3, 24));
        CHECK(!skipRepeatedOrder(true, true, 0, 48, 3, 0));
    }

    void testUnstickBackoff()
    {
        CHECK(unstickBackoffFrames(0, 3, 192) == 3);
        CHECK(unstickBackoffFrames(2, 3, 192) == 3);
        CHECK(unstickBackoffFrames(3, 3, 192) == 24);
        CHECK(unstickBackoffFrames(4, 3, 192) == 48);
        CHECK(unstickBackoffFrames(5, 3, 192) == 96);
        CHECK(unstickBackoffFrames(6, 3, 192) == 192);
        CHECK(unstickBackoffFrames(20, 3, 192) == 192);
        CHECK(unstickBackoffFrames(10, 3, 0) == 3);         // backoff disabled
    }

    void testCannonTargets()
    {
        using K = CannonBaseKind;
        CHECK(cannonTarget(K::Natural, false, 1, 2, 1, 1) == 1);
        CHECK(cannonTarget(K::Natural, true, 1, 2, 1, 1) == 2);
        CHECK(cannonTarget(K::Natural, true, 2, 1, 1, 1) == 2);
        CHECK(cannonTarget(K::Main, false, 1, 2, 1, 1) == 0);
        CHECK(cannonTarget(K::Main, true, 1, 2, 1, 1) == 1);
        CHECK(cannonTarget(K::Expansion, false, 1, 2, 1, 2) == 0);
        CHECK(cannonTarget(K::Expansion, true, 1, 2, 1, 2) == 2);
        CHECK(cannonTarget(K::Expansion, true, 1, 2, 1, 0) == 0);

        CHECK(cannonRequestUrgent(true, true, 0, 0));
        CHECK(!cannonRequestUrgent(true, true, 0, 1));     // second cannon stays at normal priority
        CHECK(!cannonRequestUrgent(true, true, 1, 0));
        CHECK(!cannonRequestUrgent(true, false, 0, 0));
        CHECK(!cannonRequestUrgent(false, true, 0, 0));
    }

    void testPvzAllIn()
    {
        // 2026-11-15 Radeon replay: 12 pool, speedlings at the natural around frame 5700
        CHECK(pvzAllInByLings(true, true, 5700, 6, 6, 6, 12));
        CHECK(!pvzAllInByLings(true, true, 5700, 5, 6, 6, 12));
        CHECK(pvzAllInByLings(true, true, 5000, 0, 12, 6, 12));
        CHECK(!pvzAllInByLings(true, true, 8000, 0, 12, 6, 12));    // seen-lings rule ends at frame 8000
        CHECK(pvzAllInByLings(true, true, 8999, 6, 0, 6, 12));
        CHECK(!pvzAllInByLings(true, true, 9000, 6, 0, 6, 12));     // base-lings rule ends at frame 9000
        CHECK(!pvzAllInByLings(true, false, 5700, 10, 20, 6, 12));  // opening not known yet
        CHECK(!pvzAllInByLings(false, true, 5700, 10, 20, 6, 12));
        CHECK(!pvzAllInByLings(true, true, 5700, 10, 20, 0, 0));    // both rules off

        CHECK(!pvzAllInLatchActive(-1, 6000));
        CHECK(pvzAllInLatchActive(5700, 6000));
        CHECK(pvzAllInLatchActive(5700, 9999));
        CHECK(!pvzAllInLatchActive(5700, 10000));
        CHECK(!pvzAllInLatchActive(5700, 100));                     // new game started (frame went back)

        CHECK(pvzWallCannonTarget(true, true, 3) == 2);   // early anti-ling cap
        CHECK(pvzWallCannonTarget(true, true, 5) == 2);
        CHECK(pvzWallCannonTarget(true, false, 3) == 2);
        CHECK(pvzWallCannonTarget(false, true, 5) == 2);
        CHECK(pvzWallCannonTarget(true, true, 2) == 2);

        CHECK(pvzGatewayBeforeNexus(true, true));
        CHECK(!pvzGatewayBeforeNexus(true, false));
        CHECK(!pvzGatewayBeforeNexus(false, true));

        CHECK(pvzMinZealots(true, true, false, 2, 4, 30) == 2);
        CHECK(pvzMinZealots(true, false, false, 2, 4, 30) == 0);
        CHECK(pvzMinZealots(true, true, true, 2, 4, 3) == 4);
        CHECK(pvzMinZealots(true, true, true, 2, 4, 16) == 6);
        CHECK(pvzMinZealots(true, true, true, 2, 4, 60) == 8);      // capped
        CHECK(pvzMinZealots(true, true, true, 6, 0, 0) == 6);
        CHECK(pvzMinZealots(false, true, true, 2, 4, 30) == 0);

        CHECK(pvzProbeCap(true, true, 18) == 18);
        CHECK(pvzProbeCap(true, false, 18) == 0);
        CHECK(pvzProbeCap(true, true, 0) == 0);
        CHECK(pvzProbeCap(false, true, 18) == 0);

        // Fighting Spirit: an unseen or already-finished pool is a 6-pool, not a 9-pool.
        CHECK(pvzUnseenPoolStart(true) == 900);
        CHECK(pvzUnseenPoolStart(false) == 1600);
        CHECK(pvzFirstCannonFrame(true, 0, 3400) == 0);
        CHECK(pvzFirstCannonFrame(true, 1, 3400) == 3400);
        CHECK(pvzFirstCannonFrame(false, 0, 3400) == 3400);
        CHECK(pvzQueueAnotherCannon(true, true) == false);
        CHECK(pvzQueueAnotherCannon(true, false) == true);
        CHECK(pvzQueueAnotherCannon(false, true) == true);

        // Gas was 500 at 6:00 with no core, and 858 at 8:00 with no archives.
        CHECK(pvzNextGasTech(true, 32, false, false, false, false, false) == PvzGasTech::None);
        CHECK(pvzNextGasTech(true, 50, false, false, false, false, false) == PvzGasTech::CyberCore);
        CHECK(pvzNextGasTech(true, 500, false, false, false, false, false) == PvzGasTech::CyberCore);
        CHECK(pvzNextGasTech(true, 149, true, false, false, false, false) == PvzGasTech::None);
        CHECK(pvzNextGasTech(true, 150, true, false, false, false, false) == PvzGasTech::Singularity);
        CHECK(pvzNextGasTech(true, 150, true, true, false, false, false) == PvzGasTech::Stargate);
        CHECK(pvzNextGasTech(true, 100, true, true, true, false, false) == PvzGasTech::Citadel);
        CHECK(pvzNextGasTech(true, 200, true, true, true, true, false) == PvzGasTech::Archives);
        CHECK(pvzNextGasTech(true, 858, true, true, true, true, true) == PvzGasTech::None);
        CHECK(pvzNextGasTech(false, 858, false, false, false, false, false) == PvzGasTech::None);

        // Hydras confirmed, mutalisks not: citadel before a stargate, then leg speed, archives, storm, templar.
        CHECK(pvzHydraNotMuta(true, false));
        CHECK(!pvzHydraNotMuta(true, true));
        CHECK(!pvzHydraNotMuta(false, false));
        CHECK(!pvzNexusCannons(true, false));
        CHECK(pvzNexusCannons(true, true));
        CHECK(pvzNexusCannons(false, false));
        CHECK(pvzEntranceCannonTarget(true, false, 2) == 4);
        CHECK(pvzEntranceCannonTarget(true, false, 5) == 5);
        CHECK(pvzEntranceCannonTarget(false, false, 2) == 2);
        CHECK(pvzHydraZealotRatio(false, 0.1) == 0.1);
        CHECK(pvzHydraZealotRatio(true, 0.1) == 0.5);
        CHECK(pvzHydraZealotRatio(true, 0.8) == 0.8);

        CHECK(pvzNextGasTech(true, 150, true, true, false, false, false, true) == PvzGasTech::Citadel);
        CHECK(pvzNextGasTech(true, 100, true, true, false, true, false, true, false) == PvzGasTech::None);
        CHECK(pvzNextGasTech(true, 150, true, true, false, true, false, true, true, false) == PvzGasTech::LegSpeed);
        CHECK(pvzNextGasTech(true, 200, true, true, false, true, false, true, true, true) == PvzGasTech::Archives);
        CHECK(pvzNextGasTech(true, 200, true, true, false, true, true, true, true, true, false) == PvzGasTech::None);
        CHECK(pvzNextGasTech(true, 200, true, true, false, true, true, true, true, true, true, false) == PvzGasTech::Storm);
        CHECK(pvzNextGasTech(true, 150, true, true, false, true, true, true, true, true, true, true, false) == PvzGasTech::HighTemplar);
        CHECK(pvzNextGasTech(true, 500, true, true, false, true, true, true, true, true, true, true, true) == PvzGasTech::None);

        // 2-3 dragoons do not walk into more hydras. A zealot+dragoon mix attacks only when it is large enough.
        CHECK(!pvzTakeHydraFight(0, 2, 0, 8));
        CHECK(!pvzTakeHydraFight(0, 3, 0, 4));
        CHECK(pvzTakeHydraFight(0, 2, 0, 1));
        CHECK(!pvzTakeHydraFight(0, 8, 0, 5));
        CHECK(pvzTakeHydraFight(0, 8, 0, 4));
        CHECK(!pvzTakeHydraFight(2, 2, 0, 11));
        CHECK(pvzTakeHydraFight(4, 4, 0, 10));
        CHECK(pvzTakeHydraFight(0, 2, 1, 6));
        CHECK(pvzTakeHydraFight(0, 0, 0, 0));
    }

    void testCombatFix()
    {
        // Effective micro latency
        CHECK(effectiveMicroLatency(-1, 3, 5) == 5);     // auto on Battle.net: highest seen
        CHECK(effectiveMicroLatency(-1, 3, 3) == 3);     // auto off in low latency
        CHECK(effectiveMicroLatency(-1, 3, 4) == 3);
        CHECK(effectiveMicroLatency(0, 3, 6) == 3);      // off
        CHECK(effectiveMicroLatency(6, 3, 5) == 6);      // forced
        CHECK(effectiveMicroLatency(2, 3, 5) == 3);      // never below reported
        CHECK(effectiveMicroLatency(40, 3, 5) == 12);    // capped
        CHECK(effectiveRemainingLatency(2, 3, 5) == 4);
        CHECK(effectiveRemainingLatency(3, 3, 3) == 3);
        CHECK(effectiveRemainingLatency(3, 3, 6) == 6);
        CHECK(effectiveRemainingLatency(5, 3, 5) == 5);  // clamped to effective
        CHECK(clampPredictionFrames(8, 5) == 5);
        CHECK(clampPredictionFrames(0, 5) == 1);
        CHECK(clampPredictionFrames(3, 5) == 3);

        // In-flight move guard
        CHECK(skipInFlightMove(true, false, false, true, 10, 64, 3, 5));
        CHECK(skipInFlightMove(true, false, false, true, 0, 64, 5, 5));
        CHECK(!skipInFlightMove(true, false, false, true, 0, 64, 6, 5));    // previous move executed
        CHECK(!skipInFlightMove(true, false, false, true, 100, 64, 1, 5));  // different target
        CHECK(!skipInFlightMove(true, true, false, true, 0, 64, 1, 5));     // forced
        CHECK(!skipInFlightMove(true, false, true, true, 0, 64, 1, 5));     // stuck
        CHECK(!skipInFlightMove(true, false, false, false, 0, 64, 1, 5));   // last was not a move
        CHECK(!skipInFlightMove(false, false, false, true, 0, 64, 1, 5));

        // Attack / regroup commitment
        CHECK(holdAttackCommit(true, 10, 48, 0.3, 0.6));
        CHECK(!holdAttackCommit(true, 48, 48, 0.3, 0.6));
        CHECK(!holdAttackCommit(true, 10, 48, 0.7, 0.6));   // hopeless: retreat
        CHECK(!holdAttackCommit(false, 10, 48, 0.3, 0.6));
        CHECK(!holdAttackCommit(true, 10, 0, 0.3, 0.6));
        CHECK(attackAbortStableFrames(true, 24) == 24);
        CHECK(attackAbortStableFrames(true, 2) == 6);
        CHECK(attackAbortStableFrames(false, 24) == 6);
        CHECK(holdRegroupCommit(true, 10, 48));
        CHECK(!holdRegroupCommit(true, 60, 48));
        CHECK(!holdRegroupCommit(false, 10, 48));

        // Flee guard
        FleeGuardInput fg{true, 6, 4, 3, false, 0.5, 0.85};
        CHECK(fightInsteadOfFlee(fg));
        fg.siegedTankInRange = true; CHECK(!fightInsteadOfFlee(fg)); fg.siegedTankInRange = false;
        fg.fastPursuers = 1; CHECK(!fightInsteadOfFlee(fg)); fg.fastPursuers = 2; CHECK(fightInsteadOfFlee(fg));
        fg.myPercentLost = 0.9; CHECK(!fightInsteadOfFlee(fg)); fg.myPercentLost = 0.5;
        fg.myUnits = 1; CHECK(!fightInsteadOfFlee(fg)); fg.myUnits = 6;
        fg.engagedPursuers = 0; CHECK(!fightInsteadOfFlee(fg)); fg.engagedPursuers = 4;
        fg.enabled = false; CHECK(!fightInsteadOfFlee(fg));
    }
}

namespace
{
    void testAntiVulture()
    {
        using S = DetectionStage;
        using P = ObserverPriority;
        // observer_emergency_priority on
        CHECK(observerPriority(S::Urgent, true, false, false) == P::Emergency);
        CHECK(observerPriority(S::Elevated, true, true, true) == P::Emergency);
        CHECK(observerPriority(S::Elevated, true, false, true) == P::SpecialTeams);
        CHECK(observerPriority(S::Preventive, true, false, true) == P::SpecialTeams);
        CHECK(observerPriority(S::Preventive, true, false, false) == P::Normal);
        CHECK(observerPriority(S::ElevatedWaiting, true, true, true) == P::SpecialTeams);
        CHECK(observerPriority(S::Upstream, true, true, true) == P::Normal);
        // off: previous behavior
        CHECK(observerPriority(S::Urgent, false, true, true) == P::SpecialTeams);
        CHECK(observerPriority(S::Elevated, false, true, true) == P::SpecialTeams);
        CHECK(observerPriority(S::Preventive, false, true, true) == P::Normal);

        CHECK(blindMainCannonDue(6000, 6000, false));
        CHECK(!blindMainCannonDue(5999, 6000, false));
        CHECK(!blindMainCannonDue(9000, 6000, true));
        CHECK(!blindMainCannonDue(9000, 0, false));

        CHECK(homeGuardActive(true, 1, 8000, 14000, 0));
        CHECK(!homeGuardActive(true, 1, 8000, 14000, 1));
        CHECK(!homeGuardActive(true, 0, 8000, 14000, 0));
        CHECK(!homeGuardActive(true, 1, 14000, 14000, 0));
        CHECK(!homeGuardActive(false, 1, 8000, 14000, 0));
        // DefendBase requests zealot-value units until ourValue >= enemyValue * 6 / 5
        for (int n = 1; n <= 4; n++)
        {
            int zv = 600;
            int ev = homeGuardValue(n, zv);
            int requested = 0, ours = 0;
            while (ours < (ev * 6) / 5) { requested++; ours += zv; }
            CHECK(requested == n);
        }
        CHECK(homeGuardValue(0, 600) == 0);

        // Keep defenders when a visible fightable threat exists despite missing detection
        CHECK(!defendBaseShouldReleaseForMissingDetection(true, false, true));
        CHECK(defendBaseShouldReleaseForMissingDetection(true, false, false));
        CHECK(!defendBaseShouldReleaseForMissingDetection(true, true, false));
        CHECK(!defendBaseShouldReleaseForMissingDetection(false, false, false));

        CHECK(mineHoldActive(true, true, true, 0, 9000, 12000));
        CHECK(!mineHoldActive(true, true, true, 1, 9000, 12000));
        CHECK(!mineHoldActive(true, true, false, 0, 9000, 12000));
        CHECK(!mineHoldActive(true, true, true, 0, 12000, 12000));
        CHECK(!mineHoldActive(true, false, true, 0, 9000, 12000));

        CHECK(midgameSwitchAllowed(true, true, 480, 480));
        CHECK(!midgameSwitchAllowed(true, true, 100, 480));
        CHECK(midgameSwitchAllowed(false, true, 0, 480));
        CHECK(midgameSwitchAllowed(true, false, 0, 480));
        CHECK(midgameSwitchAllowed(true, true, 0, 0));

        CHECK(dropRisk(true, true, false, false, 5000, 12000));
        CHECK(dropRisk(true, false, false, true, 5000, 12000));
        CHECK(!dropRisk(true, true, true, true, 12000, 12000));
        CHECK(!dropRisk(false, true, true, true, 5000, 12000));
        CHECK(!dropRisk(true, false, false, false, 5000, 12000));
    }

    void testPvpRobo()
    {
        using R = PvpNaturalReason;
        PvpNaturalInput in{true, false, 13000, 10000, 600, 50, 12000, 400, 0, 500};
        CHECK(pvpNaturalOverride(in) == R::GasBlocked);
        in.gas = 200;
        CHECK(pvpNaturalOverride(in) == R::MineralFloat);
        in.minerals = 300;
        CHECK(pvpNaturalOverride(in) == R::None);
        in.frame = 11000; in.minerals = 450;
        CHECK(pvpNaturalOverride(in) == R::None);              // before reexpand frame, not gas blocked
        in.minerals = 600; in.gas = 50; in.frame = 9000;
        CHECK(pvpNaturalOverride(in) == R::None);              // gas blocked but before the natural frame
        in.underConstruction = true;
        CHECK(pvpNaturalOverride(in) == R::KeepIncomplete);
        in.enemyValueAtNatural = 500;
        CHECK(pvpNaturalOverride(in) == R::None);              // threatened: upstream decides
        in.enemyValueAtNatural = 499;
        CHECK(pvpNaturalOverride(in) == R::KeepIncomplete);
        in.enabled = false;
        CHECK(pvpNaturalOverride(in) == R::None);
        in = PvpNaturalInput{true, false, 20000, 10000, 1000, 500, 0, 400, 0, 500};
        CHECK(pvpNaturalOverride(in) == R::None);              // reexpand off, not gas blocked
        CHECK(std::string(pvpNaturalReasonName(R::GasBlocked)) == "gas_blocked");
        CHECK(std::string(pvpNaturalReasonName(R::KeepIncomplete)) == "keep_incomplete");

        CHECK(pvpRoboTechSeen(true, true, false, false, false, false));
        CHECK(pvpRoboTechSeen(true, false, false, false, false, true));
        CHECK(!pvpRoboTechSeen(true, false, false, false, false, false));
        CHECK(!pvpRoboTechSeen(false, true, true, true, true, true));
        CHECK(pvpDropThreat(true, false, true, false));
        CHECK(pvpDropThreat(true, true, false, false));
        CHECK(!pvpDropThreat(true, false, false, false));
        CHECK(!pvpDropThreat(false, true, true, true));

        // Arrived move guard
        CHECK(skipArrivedMove(true, false, false, true, 8, 16, 10, 48, 20, 48));
        CHECK(!skipArrivedMove(true, false, false, true, 8, 16, 48, 48, 20, 48));   // hold elapsed
        CHECK(!skipArrivedMove(true, false, false, true, 32, 16, 10, 48, 20, 48));  // different target
        CHECK(!skipArrivedMove(true, false, false, true, 8, 16, 10, 48, 100, 48));  // not arrived yet
        CHECK(!skipArrivedMove(true, true, false, true, 8, 16, 10, 48, 20, 48));    // forced
        CHECK(!skipArrivedMove(true, false, true, true, 8, 16, 10, 48, 20, 48));    // stuck
        CHECK(!skipArrivedMove(true, false, false, false, 8, 16, 10, 48, 20, 48));  // last was not a move
        CHECK(!skipArrivedMove(true, false, false, true, 8, 16, 10, 0, 20, 48));    // off
        CHECK(!skipArrivedMove(false, false, false, true, 8, 16, 10, 48, 20, 48));

        CHECK(resourceBackoffActive(100, 110, 24));
        CHECK(!resourceBackoffActive(100, 124, 24));
        CHECK(!resourceBackoffActive(-1, 10, 24));
        CHECK(!resourceBackoffActive(100, 101, 0));

        CHECK(supplyDropDetected(57, 37, 20));
        CHECK(!supplyDropDetected(57, 38, 20));
        CHECK(!supplyDropDetected(57, 0, 0));
    }

    // Mirrors Workers::assignMineralPatch on patches given by their distance scores; returns how many of `workers`
    // probes got a patch (2 per patch max)
    int simulateAssignment(const std::vector<int> &scores, int workers, bool fix)
    {
        std::vector<int> load(scores.size(), 0);
        int assigned = 0;
        for (int w = 0; w < workers; w++)
        {
            bool patchWithoutWorkers = false;
            for (int l : load) if (l == 0) patchWithoutWorkers = true;
            int bestScore = patchWithoutWorkers ? 1 << 30 : 0;
            int best = -1;
            for (size_t i = 0; i < scores.size(); i++)
            {
                if (load[i] != (patchWithoutWorkers ? 0 : 1)) continue;
                if (mineralScoreBetter(fix, best >= 0, patchWithoutWorkers, scores[i], bestScore, (int)i, best >= 0 ? best : 1 << 30))
                {
                    bestScore = scores[i];
                    best = (int)i;
                }
            }
            if (best >= 0)
            {
                load[best]++;
                assigned++;
            }
        }
        return assigned;
    }

    void testStackedMinerals()
    {
        // Registration: upstream drops further fields on an occupied tile
        CHECK(registerResourceAtInit(true, true));
        CHECK(registerResourceAtInit(false, false));
        CHECK(!registerResourceAtInit(false, true));

        // Matching: minerals by ID with the fix, first mineral of the tile without it; geysers by type
        std::vector<ResourceKey> tile = {{10, true, false}, {11, true, false}, {12, true, true}};
        CHECK(matchResource(tile, 11, true, true) == 1);
        CHECK(matchResource(tile, 10, true, true) == 0);
        CHECK(matchResource(tile, 12, true, true) == -1);   // destroyed
        CHECK(matchResource(tile, 99, true, true) == -1);   // unknown field -> new resource
        CHECK(matchResource(tile, 99, true, false) == 0);   // upstream: whatever is on the tile
        CHECK(matchResource(tile, 11, true, false) == 1);
        std::vector<ResourceKey> geyser = {{20, false, false}};
        CHECK(matchResource(geyser, 77, false, true) == 0); // refinery on the geyser
        CHECK(matchResource(geyser, 77, true, true) == -1);
        CHECK(matchResource({}, 1, true, true) == -1);

        // Scoring: second worker on a patch touching the depot (score 0)
        CHECK(!mineralScoreBetter(false, false, false, 0, 0, 1, 1 << 30));   // upstream bug
        CHECK(mineralScoreBetter(true, false, false, 0, 0, 1, 1 << 30));
        CHECK(mineralScoreBetter(true, true, false, 5, 3, 9, 1));
        CHECK(!mineralScoreBetter(true, true, false, 3, 5, 1, 9));
        CHECK(mineralScoreBetter(true, true, true, 3, 3, 1, 9));             // tie -> lower ID
        CHECK(!mineralScoreBetter(true, true, true, 3, 3, 9, 1));
        CHECK(!mineralScoreBetter(false, true, true, 3, 3, 1, 9));           // upstream keeps the first
        CHECK(mineralScoreBetter(false, false, true, 3, 1 << 30, 1, 1 << 30));

        // OdysseyRE-like main: upstream registered one resource for the whole stack -> 1 probe mines
        CHECK(simulateAssignment({0}, 4, false) == 1);
        CHECK(simulateAssignment({0}, 4, true) == 2);
        // With the identity fix every field is a patch: 8 fields touching the depot take 12 probes
        CHECK(simulateAssignment(std::vector<int>(8, 0), 12, false) == 8);
        CHECK(simulateAssignment(std::vector<int>(8, 0), 12, true) == 12);
        CHECK(simulateAssignment(std::vector<int>(8, 0), 20, true) == 16);
        // Normal main (no zero scores): fix changes nothing in the count
        CHECK(simulateAssignment({32, 40, 48, 40, 32, 56, 64, 48}, 16, false) == 16);
        CHECK(simulateAssignment({32, 40, 48, 40, 32, 56, 64, 48}, 16, true) == 16);

        CHECK(sendIdleMinerToDepot(false, true));
        CHECK(sendIdleMinerToDepot(true, false));
        CHECK(!sendIdleMinerToDepot(true, true));
    }
    void testArmyDefense()
    {
        CHECK(homeMineNeedsDetection(true, true, true, true, 160, 960));
        CHECK(!homeMineNeedsDetection(false, true, true, true, 160, 960));
        CHECK(!homeMineNeedsDetection(true, false, true, true, 160, 960));
        CHECK(!homeMineNeedsDetection(true, true, false, true, 160, 960));
        CHECK(!homeMineNeedsDetection(true, true, true, false, 160, 960));
        CHECK(!homeMineNeedsDetection(true, true, true, true, 1200, 960));
        CHECK(!homeMineNeedsDetection(true, true, true, true, -1, 960));

        CHECK(clusterNearHome(-1, 300, 1280));
        CHECK(clusterNearHome(1000, -1, 1280));
        CHECK(!clusterNearHome(2000, 1500, 1280));
        CHECK(!clusterNearHome(100, 100, 0));

        CHECK(rearClusterUnstick(480, false, true, true, 0, true, 480, false));
        CHECK(!rearClusterUnstick(480, false, true, true, 0, true, 479, false));
        CHECK(rearClusterUnstick(480, false, true, true, 0, false, 0, true));
        CHECK(!rearClusterUnstick(480, false, true, true, 1, true, 4800, true));
        CHECK(!rearClusterUnstick(480, true, true, true, 0, true, 4800, false));
        CHECK(!rearClusterUnstick(480, false, false, true, 0, true, 4800, false));
        CHECK(!rearClusterUnstick(480, false, true, false, 0, true, 4800, false));
        CHECK(!rearClusterUnstick(0, false, true, true, 0, true, 4800, false));
        CHECK(rearClusterForceFrames(480) == 240);
        CHECK(rearClusterForceFrames(100) == 120);
        CHECK(skipReinforcementWait(480, false, true));
        CHECK(!skipReinforcementWait(0, false, true));
        CHECK(!skipReinforcementWait(480, true, true));
        CHECK(!skipReinforcementWait(480, false, false));

        // Backrooms-like: far corner (distUs 3600, distEnemy 4200) vs near base (distUs 900, distEnemy 2000)
        CHECK(expansionScore(false, 4200, 3600, 0, 0, 2500) > expansionScore(false, 2000, 900, 0, 0, 2500));
        CHECK(expansionScore(true, 4200, 3600, 0, 0, 2500) < expansionScore(true, 2000, 900, 0, 0, 2500));
        CHECK(expansionScore(true, 4200, 3600, 0, 0, 0) == 4200 - 7200);
        CHECK(expansionScore(true, 9000, 2600, 0, 0, 2500) < expansionScore(true, 1000, 2400, 0, 0, 2500));
        CHECK(expansionScore(false, 1000, 500, 1500, 500, 2500) == 1500 - 500 + 15 + 10);

        CHECK(expansionGuardActive(true, 2, true, false));
        CHECK(!expansionGuardActive(true, 0, true, false));
        CHECK(!expansionGuardActive(true, 2, false, false));
        CHECK(!expansionGuardActive(true, 2, true, true));
        CHECK(!expansionGuardActive(false, 2, true, false));

        CHECK(defenseRedirectStart(true, 1000, 1200, 600, 1.2));
        CHECK(!defenseRedirectStart(true, 1000, 1199, 600, 1.2));
        CHECK(!defenseRedirectStart(true, 500, 5000, 600, 1.2));
        CHECK(!defenseRedirectStart(false, 1000, 5000, 600, 1.2));
        CHECK(!defenseRedirectStart(true, 0, 5000, 0, 1.2));
        CHECK(defenseRedirectKeep(100, 1000, 480, 2880));
        CHECK(!defenseRedirectKeep(480, 1000, 480, 2880));
        CHECK(!defenseRedirectKeep(0, 2880, 480, 2880));

        CHECK(cannonBaseReady(true, true, false, false));
        CHECK(!cannonBaseReady(true, false, false, true));
        CHECK(cannonBaseReady(true, false, true, true));
        CHECK(!cannonBaseReady(true, false, true, false));
        CHECK(!cannonBaseReady(false, false, true, true));

        CHECK(cannonTileBackoff(1000, 1200, 480));
        CHECK(!cannonTileBackoff(1000, 1480, 480));
        CHECK(!cannonTileBackoff(1000, 1200, 0));
        CHECK(!cannonTileBackoff(-1, 1200, 480));
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
    testGridHold();
    testRepeatedOrder();
    testUnstickBackoff();
    testCannonTargets();
    testPvzAllIn();
    testCombatFix();
    testAntiVulture();
    testPvpRobo();
    testStackedMinerals();
    testArmyDefense();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
