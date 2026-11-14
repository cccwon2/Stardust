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
  printf("%s\n", BnetConfig::summary().c_str());
  printf("fails=%d\n",fails); return fails;
}
