// Standalone loader test for src/Util/BnetConfig.cpp (JSON parsing, comments, legacy keys, clamping).
// Uses BWAPI headers only; the two BWAPI symbols BnetConfig touches are stubbed below (no game needed).
// Build and run from the repository root in a scratch directory (it writes bwapi-data/AI/stardust_bnet.json):
//   g++ -std=c++20 -include memory -Isrc/Util -I3rdparty/BWAPILIB/include -I3rdparty/nlohmann -I3rdparty \
//       tools/bnet-policy-test/BnetConfigTest.cpp src/Util/BnetConfig.cpp -o bnet-config-test
//   mkdir -p /tmp/bnetcfg && cd /tmp/bnetcfg && <repo>/bnet-config-test
#include "BnetConfig.h"
#include <BWAPI.h>
#include <cstdio>
#include <fstream>
#include <filesystem>
namespace BWAPI { Game *BroodwarPtr = nullptr; GameWrapper Broodwar; Game* GameWrapper::operator->() const { return BroodwarPtr; } }
int fails=0;
void w(const char*s){ std::filesystem::create_directories("bwapi-data/AI"); std::ofstream("bwapi-data/AI/stardust_bnet.json")<<s; }
#define C(x) do{ if(!(x)){fails++;printf("FAIL %d %s\n",__LINE__,#x);} }while(0)
int main(){
  std::filesystem::remove_all("bwapi-data");
  BnetConfig::initialize(); auto c=BnetConfig::get();
  C(!BnetConfig::loadedFromFile()); C(c.bioAttackStartScale==0.7&&c.bioAttackContinueScale==1.0&&c.bioAttackResumeScale==0.7);
  w("{ // old file\n \"bio_aggression_scale\": 0.5, \"natural_cannons\": 2 }");
  BnetConfig::initialize(); c=BnetConfig::get();
  C(BnetConfig::loadedFromFile()); C(c.bioAttackStartScale==0.5&&c.bioAttackContinueScale==1.0&&c.bioAttackResumeScale==0.5&&c.bioLegacyKeyPresent);
  w("{\"bio_aggression_scale\": 0.5, \"bio_attack_continue_scale\": 0.9, /* c */ \"bio_attack_start_scale\": \"bad\"}");
  BnetConfig::initialize(); c=BnetConfig::get();
  C(c.bioAttackStartScale==0.5&&c.bioAttackContinueScale==0.9&&c.bioAttackResumeScale==0.5);
  w("{\"bio_attack_resume_scale\": 9, \"observer_min_dragoons\": -3}");
  BnetConfig::initialize(); c=BnetConfig::get();
  C(c.bioAttackResumeScale==2.0 && c.observerMinDragoons==0);
  C(c.mainCannons==1);
  w("{\"natural_cannons\": 0}");
  BnetConfig::initialize(); c=BnetConfig::get();
  C(c.naturalCannons==0 && c.mainCannons==0);
  w("{\"natural_cannons\": 0, \"main_cannons\": 1}");
  BnetConfig::initialize(); c=BnetConfig::get();
  C(c.naturalCannons==0 && c.mainCannons==1);
  // pvt_mech_fix defaults and clamping
  std::filesystem::remove_all("bwapi-data");
  BnetConfig::initialize(); c=BnetConfig::get();
  C(c.pvtMechFix && c.mechWorkersPerArmyUnit==4 && c.mechZealotRatio==0.5 && c.mechExpandArmyPerBase==6);
  C(c.mechLuxuryUpgradeMinArmy==20 && c.mechAttackStartScale==0.7 && c.mechAttackContinueScale==1.0 && c.mechAttackResumeScale==0.7);
  C(c.combatMoveGuard && c.moveResendTolerancePx==64);
  w("{\"pvt_mech_fix\": false, \"mech_workers_per_army_unit\": 1, \"mech_zealot_ratio\": 9, \"mech_expand_army_per_base\": -2, \"move_resend_tolerance_px\": 500, \"combat_move_guard\": false, \"mech_attack_start_scale\": 0.5}");
  BnetConfig::initialize(); c=BnetConfig::get();
  C(!c.pvtMechFix && c.mechWorkersPerArmyUnit==2 && c.mechZealotRatio==1.5 && c.mechExpandArmyPerBase==0);
  C(c.moveResendTolerancePx==192 && !c.combatMoveGuard && c.mechAttackStartScale==0.5);
  C(BnetConfig::commandIntervalFrames()==0 && BnetConfig::maxLatencyFrames()==0);   // no game: guard inactive
  // APM / cannon keys added after the PvT mech fix
  std::filesystem::remove_all("bwapi-data");
  BnetConfig::initialize(); c=BnetConfig::get();
  C(c.combatMoveIntervalFrames==12 && c.observerMoveIntervalFrames==24 && c.moveGuardMinRemainingPx==48);
  C(c.gridWaypointHold && c.gridWaypointRefreshPx==64 && c.spellResendIntervalFrames==24 && c.unstickBackoffMaxFrames==192);
  C(c.mechNaturalCannons==2 && c.expansionCannons==1 && c.cannonThreatPriority);
  w("{\"expansion_cannons\": 5, \"mech_natural_cannons\": -1, \"grid_waypoint_refresh_px\": 1, \"combat_move_interval_frames\": 99, \"observer_move_interval_frames\": 999, \"unstick_backoff_max_frames\": 0, \"grid_waypoint_hold\": false, \"cannon_threat_priority\": false}");
  BnetConfig::initialize(); c=BnetConfig::get();
  C(c.expansionCannons==2 && c.mechNaturalCannons==0 && c.gridWaypointRefreshPx==16 && c.combatMoveIntervalFrames==48);
  C(c.observerMoveIntervalFrames==96 && c.unstickBackoffMaxFrames==0 && !c.gridWaypointHold && !c.cannonThreatPriority);
  // PvZ anti all-in keys
  std::filesystem::remove_all("bwapi-data");
  BnetConfig::initialize(); c=BnetConfig::get();
  C(c.forcedPvzOpening=="FFEDragoons" && c.pvzAntiAllinFix && c.pvzAllinBaseLings==6 && c.pvzAllinSeenLings==12);
  C(c.pvzPoolFirstCannons==3 && c.pvzPoolFirstZealots==2 && c.pvzAllinZealots==4 && c.pvzAllinProbeCap==18);
  w("{\"forced_pvz_opening\": \"\", \"pvz_antiallin_fix\": false, \"pvz_allin_base_lings\": -1, \"pvz_allin_seen_lings\": 99, \"pvz_pool_first_cannons\": 9, \"pvz_pool_first_zealots\": -2, \"pvz_allin_zealots\": 50, \"pvz_allin_probe_cap\": 3}");
  BnetConfig::initialize(); c=BnetConfig::get();
  C(c.forcedPvzOpening.empty() && !c.pvzAntiAllinFix && c.pvzAllinBaseLings==0 && c.pvzAllinSeenLings==40);
  C(c.pvzPoolFirstCannons==5 && c.pvzPoolFirstZealots==0 && c.pvzAllinZealots==12 && c.pvzAllinProbeCap==12);
  w("{\"pvz_allin_probe_cap\": 0, \"pvz_pool_first_cannons\": 1}");
  BnetConfig::initialize(); c=BnetConfig::get();
  C(c.pvzAllinProbeCap==0 && c.pvzPoolFirstCannons==2);
  // Combat fix defaults and clamping
  w("{}"); BnetConfig::initialize(); c=BnetConfig::get();
  C(c.microLatencyFrames==-1 && c.inFlightMoveGuard && c.pvtCombatCommit && c.attackCommitFrames==48 && c.commitMaxLoss==0.6);
  C(c.attackAbortStableFrames==24 && c.regroupCommitFrames==0 && c.fleeGuard && c.fleeGuardMaxLoss==0.85 && c.fleeGuardSpeedRatio==1.25);
  w("{\"micro_latency_frames\": 40, \"attack_commit_frames\": 999, \"commit_max_loss\": 3, \"attack_abort_stable_frames\": 1, \"regroup_commit_frames\": -5, \"flee_guard\": false, \"flee_guard_max_loss\": -1, \"flee_guard_speed_ratio\": 0.5, \"in_flight_move_guard\": false, \"pvt_combat_commit\": false}");
  BnetConfig::initialize(); c=BnetConfig::get();
  C(c.microLatencyFrames==12 && c.attackCommitFrames==240 && c.commitMaxLoss==1.0 && c.attackAbortStableFrames==6 && c.regroupCommitFrames==0);
  C(!c.fleeGuard && c.fleeGuardMaxLoss==0.0 && c.fleeGuardSpeedRatio==1.0 && !c.inFlightMoveGuard && !c.pvtCombatCommit);
  w("{\"micro_latency_frames\": -7}"); BnetConfig::initialize(); c=BnetConfig::get();
  C(c.microLatencyFrames==-1);
  // Anti-vulture keys: defaults and clamping
  w("{}"); BnetConfig::initialize(); c=BnetConfig::get();
  C(c.invalidMoveGuard && c.workerFleeVultures && c.workerFleeMarginPx==96 && c.workerFleeHoldFrames==48 && c.probeThrottleUnderVultures);
  C(c.blindMainCannonFrame==6000 && c.homeGuardDragoons==1 && c.homeGuardUntilFrame==14000 && c.observerEmergencyPriority);
  C(c.mineHoldUntilObserver && c.mineHoldMaxFrame==12000 && c.plutoRescoutFrame==4800 && c.plutoRescoutMaxFrames==1440);
  C(c.dropRiskDetection && c.midgameStrategyHoldFrames==480);
  w("{\"worker_flee_margin_px\": 999, \"worker_flee_hold_frames\": -1, \"blind_main_cannon_frame\": 100, \"home_guard_dragoons\": 9, \"pluto_rescout_frame\": 99999, \"pluto_rescout_max_frames\": 1, \"midgame_strategy_hold_frames\": 99999, \"invalid_move_guard\": false}");
  BnetConfig::initialize(); c=BnetConfig::get();
  C(c.workerFleeMarginPx==256 && c.workerFleeHoldFrames==0 && c.blindMainCannonFrame==3000 && c.homeGuardDragoons==4);
  C(c.plutoRescoutFrame==12000 && c.plutoRescoutMaxFrames==240 && c.midgameStrategyHoldFrames==2400 && !c.invalidMoveGuard);
  w("{\"blind_main_cannon_frame\": 0, \"pluto_rescout_frame\": -1}"); BnetConfig::initialize(); c=BnetConfig::get();
  C(c.blindMainCannonFrame==0 && c.plutoRescoutFrame==0);
  // PvP robo / command volume keys: defaults and clamping
  w("{}"); BnetConfig::initialize(); c=BnetConfig::get();
  C(c.pvpRoboFix && c.pvpNaturalFix && c.pvpReexpandFrame==12000 && c.pvpReexpandMinerals==400 && c.pvpRoboObserver);
  C(c.pvpHomeGuardDragoons==2 && c.pvpDropCannons==1 && c.arrivedMoveHoldFrames==48 && c.arrivedMoveTolerancePx==16);
  C(c.resourceFailBackoffFrames==24 && c.supplyDropEvent==20);
  w("{\"pvp_robo_fix\": false, \"pvp_reexpand_frame\": 100, \"pvp_reexpand_minerals\": 9, \"pvp_home_guard_dragoons\": 9, \"pvp_drop_cannons\": 7, \"arrived_move_hold_frames\": 999, \"arrived_move_tolerance_px\": -4, \"resource_fail_backoff_frames\": 999, \"supply_drop_event\": -1}");
  BnetConfig::initialize(); c=BnetConfig::get();
  C(!c.pvpRoboFix && c.pvpReexpandFrame==6000 && c.pvpReexpandMinerals==150 && c.pvpHomeGuardDragoons==4 && c.pvpDropCannons==2);
  C(c.arrivedMoveHoldFrames==240 && c.arrivedMoveTolerancePx==0 && c.resourceFailBackoffFrames==240 && c.supplyDropEvent==0);
  w("{\"pvp_reexpand_frame\": 0, \"arrived_move_hold_frames\": 0}"); BnetConfig::initialize(); c=BnetConfig::get();
  C(c.pvpReexpandFrame==0 && c.arrivedMoveHoldFrames==0);
  printf("%s\n", BnetConfig::summary().c_str());
  printf("fails=%d\n",fails); return fails;
}
