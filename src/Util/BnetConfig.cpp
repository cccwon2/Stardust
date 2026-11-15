#include "BnetConfig.h"
#include "BnetPolicy.h"

#include <BWAPI.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace BnetConfig
{
    namespace
    {
        Config config;
        bool fromFile = false;
        bool mainCannonsPresent = false;
        int maxLatency = 0;
        std::string path;

        const char *candidatePaths[] = {
                "bwapi-data/AI/stardust_bnet.json",
                "bwapi-data/read/stardust_bnet.json",
                "bwapi-data/write/stardust_bnet.json"
        };

        template<typename T>
        void read(const nlohmann::json &json, const char *key, T &target)
        {
            auto it = json.find(key);
            if (it == json.end() || it->is_null()) return;
            try
            {
                target = it->get<T>();
            }
            catch (...)
            {
                // Wrong type: keep the default
            }
        }

        // Like read, but reports whether a valid value was present
        template<typename T>
        bool readPresent(const nlohmann::json &json, const char *key, T &target)
        {
            auto it = json.find(key);
            if (it == json.end() || it->is_null()) return false;
            try
            {
                target = it->get<T>();
                return true;
            }
            catch (...)
            {
                return false;
            }
        }
    }

    // Removes // line comments and /* */ block comments outside of string literals
    std::string stripComments(const std::string &in)
    {
        std::string out;
        out.reserve(in.size());
        bool inString = false;
        for (size_t i = 0; i < in.size(); i++)
        {
            char c = in[i];
            if (inString)
            {
                out += c;
                if (c == '\\' && i + 1 < in.size())
                {
                    out += in[++i];
                }
                else if (c == '"')
                {
                    inString = false;
                }
                continue;
            }
            if (c == '"')
            {
                inString = true;
                out += c;
                continue;
            }
            if (c == '/' && i + 1 < in.size() && in[i + 1] == '/')
            {
                while (i < in.size() && in[i] != '\n') i++;
                out += '\n';
                continue;
            }
            if (c == '/' && i + 1 < in.size() && in[i + 1] == '*')
            {
                i += 2;
                while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/')) i++;
                i++;
                out += ' ';
                continue;
            }
            out += c;
        }
        return out;
    }

    void initialize()
    {
        config = Config();
        maxLatency = 0;
        fromFile = false;
        mainCannonsPresent = false;
        path.clear();

        for (auto candidate : candidatePaths)
        {
            std::error_code ec;
            if (!std::filesystem::exists(candidate, ec)) continue;

            try
            {
                std::ifstream file(candidate);
                if (!file.good()) continue;

                // Bundled nlohmann is 3.1.1 (no comment support), so strip // and /* */ comments ourselves
                std::stringstream buffer;
                buffer << file.rdbuf();
                auto json = nlohmann::json::parse(stripComments(buffer.str()));
                if (!json.is_object()) continue;

                read(json, "safe_bnet_mining", config.safeBnetMining);
                read(json, "stacked_mineral_fallback", config.stackedMineralFallback);
                read(json, "stuck_worker_frames", config.stuckWorkerFrames);
                read(json, "max_orders_per_frame", config.maxOrdersPerFrame);
                read(json, "min_command_interval_frames", config.minCommandIntervalFrames);
                read(json, "enable_auto_gg", config.enableAutoGG);
                read(json, "pluto_pvt_snipe", config.plutoPvtSnipe);
                read(json, "forced_pvt_opening", config.forcedPvtOpening);
                read(json, "forced_random_opening", config.forcedRandomOpening);
                read(json, "early_observer_priority", config.earlyObserverPriority);
                read(json, "observer_min_dragoons", config.observerMinDragoons);
                read(json, "natural_cannons", config.naturalCannons);
                mainCannonsPresent = readPresent(json, "main_cannons", config.mainCannons);
                read(json, "mech_natural_cannons", config.mechNaturalCannons);
                read(json, "expansion_cannons", config.expansionCannons);
                read(json, "cannon_threat_priority", config.cannonThreatPriority);
                BnetPolicy::BioScaleKeys bioKeys;
                bioKeys.hasLegacy = readPresent(json, "bio_aggression_scale", bioKeys.legacy);
                bioKeys.hasStart = readPresent(json, "bio_attack_start_scale", bioKeys.start);
                bioKeys.hasContinue = readPresent(json, "bio_attack_continue_scale", bioKeys.cont);
                bioKeys.hasResume = readPresent(json, "bio_attack_resume_scale", bioKeys.resume);
                auto bioScales = BnetPolicy::resolveBioScales(bioKeys);
                config.bioAttackStartScale = bioScales.start;
                config.bioAttackContinueScale = bioScales.cont;
                config.bioAttackResumeScale = bioScales.resume;
                config.bioLegacyKeyPresent = bioKeys.hasLegacy;
                config.bioLegacyScale = bioKeys.legacy;
                read(json, "pvt_mech_fix", config.pvtMechFix);
                read(json, "mech_workers_per_army_unit", config.mechWorkersPerArmyUnit);
                read(json, "mech_zealot_ratio", config.mechZealotRatio);
                read(json, "mech_expand_army_per_base", config.mechExpandArmyPerBase);
                read(json, "mech_luxury_upgrade_min_army", config.mechLuxuryUpgradeMinArmy);
                read(json, "mech_attack_start_scale", config.mechAttackStartScale);
                read(json, "mech_attack_continue_scale", config.mechAttackContinueScale);
                read(json, "mech_attack_resume_scale", config.mechAttackResumeScale);
                read(json, "combat_move_guard", config.combatMoveGuard);
                read(json, "move_resend_tolerance_px", config.moveResendTolerancePx);
                read(json, "combat_move_interval_frames", config.combatMoveIntervalFrames);
                read(json, "observer_move_interval_frames", config.observerMoveIntervalFrames);
                read(json, "move_guard_min_remaining_px", config.moveGuardMinRemainingPx);
                read(json, "grid_waypoint_hold", config.gridWaypointHold);
                read(json, "grid_waypoint_refresh_px", config.gridWaypointRefreshPx);
                read(json, "spell_resend_interval_frames", config.spellResendIntervalFrames);
                read(json, "unstick_backoff_max_frames", config.unstickBackoffMaxFrames);
                read(json, "micro_latency_frames", config.microLatencyFrames);
                read(json, "in_flight_move_guard", config.inFlightMoveGuard);
                read(json, "pvt_combat_commit", config.pvtCombatCommit);
                read(json, "attack_commit_frames", config.attackCommitFrames);
                read(json, "commit_max_loss", config.commitMaxLoss);
                read(json, "attack_abort_stable_frames", config.attackAbortStableFrames);
                read(json, "regroup_commit_frames", config.regroupCommitFrames);
                read(json, "flee_guard", config.fleeGuard);
                read(json, "flee_guard_max_loss", config.fleeGuardMaxLoss);
                read(json, "flee_guard_speed_ratio", config.fleeGuardSpeedRatio);
                read(json, "forced_pvz_opening", config.forcedPvzOpening);
                read(json, "pvz_antiallin_fix", config.pvzAntiAllinFix);
                read(json, "pvz_allin_base_lings", config.pvzAllinBaseLings);
                read(json, "pvz_allin_seen_lings", config.pvzAllinSeenLings);
                read(json, "pvz_pool_first_cannons", config.pvzPoolFirstCannons);
                read(json, "pvz_pool_first_zealots", config.pvzPoolFirstZealots);
                read(json, "pvz_allin_zealots", config.pvzAllinZealots);
                read(json, "pvz_allin_probe_cap", config.pvzAllinProbeCap);
                read(json, "invalid_move_guard", config.invalidMoveGuard);
                read(json, "worker_flee_vultures", config.workerFleeVultures);
                read(json, "worker_flee_margin_px", config.workerFleeMarginPx);
                read(json, "worker_flee_hold_frames", config.workerFleeHoldFrames);
                read(json, "probe_throttle_under_vultures", config.probeThrottleUnderVultures);
                read(json, "blind_main_cannon_frame", config.blindMainCannonFrame);
                read(json, "home_guard_dragoons", config.homeGuardDragoons);
                read(json, "home_guard_until_frame", config.homeGuardUntilFrame);
                read(json, "observer_emergency_priority", config.observerEmergencyPriority);
                read(json, "mine_hold_until_observer", config.mineHoldUntilObserver);
                read(json, "mine_hold_max_frame", config.mineHoldMaxFrame);
                read(json, "pluto_rescout_frame", config.plutoRescoutFrame);
                read(json, "pluto_rescout_max_frames", config.plutoRescoutMaxFrames);
                read(json, "drop_risk_detection", config.dropRiskDetection);
                read(json, "midgame_strategy_hold_frames", config.midgameStrategyHoldFrames);
                read(json, "pvp_robo_fix", config.pvpRoboFix);
                read(json, "pvp_natural_fix", config.pvpNaturalFix);
                read(json, "pvp_reexpand_frame", config.pvpReexpandFrame);
                read(json, "pvp_reexpand_minerals", config.pvpReexpandMinerals);
                read(json, "pvp_robo_observer", config.pvpRoboObserver);
                read(json, "pvp_home_guard_dragoons", config.pvpHomeGuardDragoons);
                read(json, "pvp_drop_cannons", config.pvpDropCannons);
                read(json, "arrived_move_hold_frames", config.arrivedMoveHoldFrames);
                read(json, "arrived_move_tolerance_px", config.arrivedMoveTolerancePx);
                read(json, "resource_fail_backoff_frames", config.resourceFailBackoffFrames);
                read(json, "supply_drop_event", config.supplyDropEvent);
                read(json, "stacked_mineral_identity_fix", config.stackedMineralIdentityFix);
                read(json, "mineral_assignment_fix", config.mineralAssignmentFix);
                read(json, "mining_assignment_log", config.miningAssignmentLog);
                read(json, "army_state_log", config.armyStateLog);
                read(json, "army_state_interval_frames", config.armyStateIntervalFrames);
                read(json, "home_mine_clear", config.homeMineClear);
                read(json, "home_mine_clear_radius_px", config.homeMineClearRadiusPx);
                read(json, "rear_cluster_unstick_frames", config.rearClusterUnstickFrames);
                read(json, "rear_cluster_home_radius_px", config.rearClusterHomeRadiusPx);
                read(json, "expansion_select_fix", config.expansionSelectFix);
                read(json, "expansion_max_distance_px", config.expansionMaxDistancePx);
                read(json, "expansion_guard_dragoons", config.expansionGuardDragoons);
                read(json, "expansion_defense_redirect", config.expansionDefenseRedirect);
                read(json, "defense_redirect_ratio", config.defenseRedirectRatio);
                read(json, "defense_redirect_min_dragoons", config.defenseRedirectMinDragoons);
                read(json, "expansion_cannon_at_nexus_start", config.expansionCannonAtNexusStart);
                read(json, "cannon_rebuild_backoff_frames", config.cannonRebuildBackoffFrames);
                read(json, "print_build_info", config.printBuildInfo);
                read(json, "bnet_log", config.bnetLog);

                fromFile = true;
                path = candidate;
                break;
            }
            catch (...)
            {
                // Parse error: fall back to defaults and keep looking
                config = Config();
                mainCannonsPresent = false;
            }
        }

        config.mainCannons = BnetPolicy::resolveMainCannons(mainCannonsPresent, config.mainCannons, config.naturalCannons);

        // Clamp values to sane ranges
        config.stuckWorkerFrames = std::clamp(config.stuckWorkerFrames, 12, 1000);
        config.maxOrdersPerFrame = std::max(0, config.maxOrdersPerFrame);
        config.minCommandIntervalFrames = std::clamp(config.minCommandIntervalFrames, -1, 48);
        config.naturalCannons = std::clamp(config.naturalCannons, 0, 2);
        config.mainCannons = std::clamp(config.mainCannons, 0, 1);
        config.observerMinDragoons = std::clamp(config.observerMinDragoons, 0, 12);
        config.bioAttackStartScale = std::clamp(config.bioAttackStartScale, 0.1, 2.0);
        config.bioAttackContinueScale = std::clamp(config.bioAttackContinueScale, 0.1, 2.0);
        config.bioAttackResumeScale = std::clamp(config.bioAttackResumeScale, 0.1, 2.0);
        config.mechWorkersPerArmyUnit = std::clamp(config.mechWorkersPerArmyUnit, 2, 12);
        config.mechZealotRatio = std::clamp(config.mechZealotRatio, 0.0, 1.5);
        config.mechExpandArmyPerBase = std::clamp(config.mechExpandArmyPerBase, 0, 20);
        config.mechLuxuryUpgradeMinArmy = std::clamp(config.mechLuxuryUpgradeMinArmy, 0, 60);
        config.mechAttackStartScale = std::clamp(config.mechAttackStartScale, 0.1, 2.0);
        config.mechAttackContinueScale = std::clamp(config.mechAttackContinueScale, 0.1, 2.0);
        config.mechAttackResumeScale = std::clamp(config.mechAttackResumeScale, 0.1, 2.0);
        config.moveResendTolerancePx = std::clamp(config.moveResendTolerancePx, 3, 192);
        config.mechNaturalCannons = std::clamp(config.mechNaturalCannons, 0, 2);
        config.expansionCannons = std::clamp(config.expansionCannons, 0, 2);
        config.combatMoveIntervalFrames = std::clamp(config.combatMoveIntervalFrames, 0, 48);
        config.observerMoveIntervalFrames = std::clamp(config.observerMoveIntervalFrames, 0, 96);
        config.moveGuardMinRemainingPx = std::clamp(config.moveGuardMinRemainingPx, 0, 256);
        config.gridWaypointRefreshPx = std::clamp(config.gridWaypointRefreshPx, 16, 160);
        config.spellResendIntervalFrames = std::clamp(config.spellResendIntervalFrames, 0, 96);
        config.unstickBackoffMaxFrames = std::clamp(config.unstickBackoffMaxFrames, 0, 480);
        config.microLatencyFrames = std::clamp(config.microLatencyFrames, -1, 12);
        config.attackCommitFrames = std::clamp(config.attackCommitFrames, 0, 240);
        config.commitMaxLoss = std::clamp(config.commitMaxLoss, 0.0, 1.0);
        config.attackAbortStableFrames = std::clamp(config.attackAbortStableFrames, 6, 96);
        config.regroupCommitFrames = std::clamp(config.regroupCommitFrames, 0, 240);
        config.fleeGuardMaxLoss = std::clamp(config.fleeGuardMaxLoss, 0.0, 1.0);
        config.fleeGuardSpeedRatio = std::clamp(config.fleeGuardSpeedRatio, 1.0, 3.0);
        config.pvzAllinBaseLings = std::clamp(config.pvzAllinBaseLings, 0, 30);
        config.pvzAllinSeenLings = std::clamp(config.pvzAllinSeenLings, 0, 40);
        config.pvzPoolFirstCannons = std::clamp(config.pvzPoolFirstCannons, 2, 5);
        config.pvzPoolFirstZealots = std::clamp(config.pvzPoolFirstZealots, 0, 6);
        config.pvzAllinZealots = std::clamp(config.pvzAllinZealots, 0, 12);
        config.pvzAllinProbeCap = config.pvzAllinProbeCap <= 0 ? 0 : std::clamp(config.pvzAllinProbeCap, 12, 40);
        config.workerFleeMarginPx = std::clamp(config.workerFleeMarginPx, 0, 256);
        config.workerFleeHoldFrames = std::clamp(config.workerFleeHoldFrames, 0, 240);
        config.blindMainCannonFrame = config.blindMainCannonFrame <= 0 ? 0 : std::clamp(config.blindMainCannonFrame, 3000, 20000);
        config.homeGuardDragoons = std::clamp(config.homeGuardDragoons, 0, 4);
        config.homeGuardUntilFrame = std::clamp(config.homeGuardUntilFrame, 0, 30000);
        config.mineHoldMaxFrame = std::clamp(config.mineHoldMaxFrame, 0, 30000);
        config.plutoRescoutFrame = config.plutoRescoutFrame <= 0 ? 0 : std::clamp(config.plutoRescoutFrame, 2000, 12000);
        config.plutoRescoutMaxFrames = std::clamp(config.plutoRescoutMaxFrames, 240, 4800);
        config.midgameStrategyHoldFrames = std::clamp(config.midgameStrategyHoldFrames, 0, 2400);
        config.pvpReexpandFrame = config.pvpReexpandFrame <= 0 ? 0 : std::clamp(config.pvpReexpandFrame, 6000, 30000);
        config.pvpReexpandMinerals = std::clamp(config.pvpReexpandMinerals, 150, 2000);
        config.pvpHomeGuardDragoons = std::clamp(config.pvpHomeGuardDragoons, 0, 4);
        config.pvpDropCannons = std::clamp(config.pvpDropCannons, 0, 2);
        config.arrivedMoveHoldFrames = std::clamp(config.arrivedMoveHoldFrames, 0, 240);
        config.arrivedMoveTolerancePx = std::clamp(config.arrivedMoveTolerancePx, 0, 128);
        config.resourceFailBackoffFrames = std::clamp(config.resourceFailBackoffFrames, 0, 240);
        config.supplyDropEvent = std::clamp(config.supplyDropEvent, 0, 200);
        config.armyStateIntervalFrames = std::clamp(config.armyStateIntervalFrames, 120, 14400);
        config.homeMineClearRadiusPx = std::clamp(config.homeMineClearRadiusPx, 0, 4000);
        config.rearClusterUnstickFrames = std::clamp(config.rearClusterUnstickFrames, 0, 4800);
        config.rearClusterHomeRadiusPx = std::clamp(config.rearClusterHomeRadiusPx, 0, 6000);
        config.expansionMaxDistancePx = std::clamp(config.expansionMaxDistancePx, 0, 10000);
        config.expansionGuardDragoons = std::clamp(config.expansionGuardDragoons, 0, 4);
        config.defenseRedirectRatio = std::clamp(config.defenseRedirectRatio, 0.5, 5.0);
        config.defenseRedirectMinDragoons = std::clamp(config.defenseRedirectMinDragoons, 1, 20);
        config.cannonRebuildBackoffFrames = std::clamp(config.cannonRebuildBackoffFrames, 0, 4800);
    }

    const Config &get()
    {
        return config;
    }

    bool loadedFromFile()
    {
        return fromFile;
    }

    const std::string &loadedPath()
    {
        return path;
    }

    std::string summary()
    {
        std::ostringstream os;
        os << "config=" << (fromFile ? path : std::string("defaults"))
           << " safe_bnet_mining=" << config.safeBnetMining
           << " stacked_mineral_fallback=" << config.stackedMineralFallback
           << " stuck_worker_frames=" << config.stuckWorkerFrames
           << " max_orders_per_frame=" << config.maxOrdersPerFrame
           << " min_command_interval_frames=" << config.minCommandIntervalFrames
           << "(eff=" << commandIntervalFrames() << ")"
           << " enable_auto_gg=" << config.enableAutoGG
           << " pluto_pvt_snipe=" << config.plutoPvtSnipe
           << " forced_pvt_opening=" << (config.forcedPvtOpening.empty() ? "UCB1" : config.forcedPvtOpening)
           << " forced_random_opening=" << (config.forcedRandomOpening.empty() ? "UCB1" : config.forcedRandomOpening)
           << " early_observer_priority=" << config.earlyObserverPriority
           << " observer_min_dragoons=" << config.observerMinDragoons
           << " natural_cannons=" << config.naturalCannons
           << " main_cannons=" << config.mainCannons << (mainCannonsPresent ? "" : "(derived)")
           << " mech_natural_cannons=" << config.mechNaturalCannons
           << " expansion_cannons=" << config.expansionCannons
           << " cannon_threat_priority=" << config.cannonThreatPriority
           << " bio_attack_start_scale=" << config.bioAttackStartScale
           << " bio_attack_continue_scale=" << config.bioAttackContinueScale
           << " bio_attack_resume_scale=" << config.bioAttackResumeScale
           << " bio_aggression_scale(legacy)=";
        if (config.bioLegacyKeyPresent) os << config.bioLegacyScale << "(start/resume only)";
        else os << "absent";
        os
           << " pvt_mech_fix=" << config.pvtMechFix
           << " mech_workers_per_army_unit=" << config.mechWorkersPerArmyUnit
           << " mech_zealot_ratio=" << config.mechZealotRatio
           << " mech_expand_army_per_base=" << config.mechExpandArmyPerBase
           << " mech_luxury_upgrade_min_army=" << config.mechLuxuryUpgradeMinArmy
           << " mech_attack_start_scale=" << config.mechAttackStartScale
           << " mech_attack_continue_scale=" << config.mechAttackContinueScale
           << " mech_attack_resume_scale=" << config.mechAttackResumeScale
           << " combat_move_guard=" << config.combatMoveGuard
           << " move_resend_tolerance_px=" << config.moveResendTolerancePx
           << " combat_move_interval_frames=" << config.combatMoveIntervalFrames
           << " observer_move_interval_frames=" << config.observerMoveIntervalFrames
           << " move_guard_min_remaining_px=" << config.moveGuardMinRemainingPx
           << " grid_waypoint_hold=" << config.gridWaypointHold
           << " grid_waypoint_refresh_px=" << config.gridWaypointRefreshPx
           << " spell_resend_interval_frames=" << config.spellResendIntervalFrames
           << " unstick_backoff_max_frames=" << config.unstickBackoffMaxFrames
           << " micro_latency_frames=" << config.microLatencyFrames << "(eff=" << microLatencyFrames() << ")"
           << " in_flight_move_guard=" << config.inFlightMoveGuard
           << " pvt_combat_commit=" << config.pvtCombatCommit
           << " attack_commit_frames=" << config.attackCommitFrames
           << " commit_max_loss=" << config.commitMaxLoss
           << " attack_abort_stable_frames=" << config.attackAbortStableFrames
           << " regroup_commit_frames=" << config.regroupCommitFrames
           << " flee_guard=" << config.fleeGuard
           << " flee_guard_max_loss=" << config.fleeGuardMaxLoss
           << " flee_guard_speed_ratio=" << config.fleeGuardSpeedRatio
           << " forced_pvz_opening=" << (config.forcedPvzOpening.empty() ? "UCB1" : config.forcedPvzOpening)
           << " pvz_antiallin_fix=" << config.pvzAntiAllinFix
           << " pvz_allin_base_lings=" << config.pvzAllinBaseLings
           << " pvz_allin_seen_lings=" << config.pvzAllinSeenLings
           << " pvz_pool_first_cannons=" << config.pvzPoolFirstCannons
           << " pvz_pool_first_zealots=" << config.pvzPoolFirstZealots
           << " pvz_allin_zealots=" << config.pvzAllinZealots
           << " pvz_allin_probe_cap=" << config.pvzAllinProbeCap
           << " invalid_move_guard=" << config.invalidMoveGuard
           << " worker_flee_vultures=" << config.workerFleeVultures
           << " worker_flee_margin_px=" << config.workerFleeMarginPx
           << " worker_flee_hold_frames=" << config.workerFleeHoldFrames
           << " probe_throttle_under_vultures=" << config.probeThrottleUnderVultures
           << " blind_main_cannon_frame=" << config.blindMainCannonFrame
           << " home_guard_dragoons=" << config.homeGuardDragoons
           << " home_guard_until_frame=" << config.homeGuardUntilFrame
           << " observer_emergency_priority=" << config.observerEmergencyPriority
           << " mine_hold_until_observer=" << config.mineHoldUntilObserver
           << " mine_hold_max_frame=" << config.mineHoldMaxFrame
           << " pluto_rescout_frame=" << config.plutoRescoutFrame
           << " pluto_rescout_max_frames=" << config.plutoRescoutMaxFrames
           << " drop_risk_detection=" << config.dropRiskDetection
           << " midgame_strategy_hold_frames=" << config.midgameStrategyHoldFrames
           << " pvp_robo_fix=" << config.pvpRoboFix
           << " pvp_natural_fix=" << config.pvpNaturalFix
           << " pvp_reexpand_frame=" << config.pvpReexpandFrame
           << " pvp_reexpand_minerals=" << config.pvpReexpandMinerals
           << " pvp_robo_observer=" << config.pvpRoboObserver
           << " pvp_home_guard_dragoons=" << config.pvpHomeGuardDragoons
           << " pvp_drop_cannons=" << config.pvpDropCannons
           << " arrived_move_hold_frames=" << config.arrivedMoveHoldFrames
           << " arrived_move_tolerance_px=" << config.arrivedMoveTolerancePx
           << " resource_fail_backoff_frames=" << config.resourceFailBackoffFrames
           << " supply_drop_event=" << config.supplyDropEvent
           << " stacked_mineral_identity_fix=" << config.stackedMineralIdentityFix
           << " mineral_assignment_fix=" << config.mineralAssignmentFix
           << " mining_assignment_log=" << config.miningAssignmentLog
           << " army_state_log=" << config.armyStateLog
           << " army_state_interval_frames=" << config.armyStateIntervalFrames
           << " home_mine_clear=" << config.homeMineClear
           << " home_mine_clear_radius_px=" << config.homeMineClearRadiusPx
           << " rear_cluster_unstick_frames=" << config.rearClusterUnstickFrames
           << " rear_cluster_home_radius_px=" << config.rearClusterHomeRadiusPx
           << " expansion_select_fix=" << config.expansionSelectFix
           << " expansion_max_distance_px=" << config.expansionMaxDistancePx
           << " expansion_guard_dragoons=" << config.expansionGuardDragoons
           << " expansion_defense_redirect=" << config.expansionDefenseRedirect
           << " defense_redirect_ratio=" << config.defenseRedirectRatio
           << " defense_redirect_min_dragoons=" << config.defenseRedirectMinDragoons
           << " expansion_cannon_at_nexus_start=" << config.expansionCannonAtNexusStart
           << " cannon_rebuild_backoff_frames=" << config.cannonRebuildBackoffFrames
           << " print_build_info=" << config.printBuildInfo
           << " bnet_log=" << config.bnetLog;
        return os.str();
    }

    void noteLatency()
    {
        int latency = BWAPI::BroodwarPtr ? BWAPI::Broodwar->getLatencyFrames() : 0;
        if (latency > maxLatency) maxLatency = latency;
    }

    int maxLatencyFrames()
    {
        return maxLatency;
    }

    int commandIntervalFrames()
    {
        if (config.minCommandIntervalFrames >= 0) return config.minCommandIntervalFrames;

        // Auto: only active in high-latency environments (Battle.net is typically 5-6 frames).
        // Uses the highest latency seen this game: Remastered reported 5 at start and 3 later, which used to turn the
        // guard off mid-game.
        noteLatency();
        return BnetPolicy::autoCommandInterval(maxLatency);
    }

    int microLatencyFrames()
    {
        int reported = BWAPI::BroodwarPtr ? BWAPI::Broodwar->getLatencyFrames() : 0;
        noteLatency();
        return BnetPolicy::effectiveMicroLatency(config.microLatencyFrames, reported, maxLatency);
    }

    int microRemainingLatencyFrames()
    {
        if (!BWAPI::BroodwarPtr) return 0;
        int reported = BWAPI::Broodwar->getLatencyFrames();
        return BnetPolicy::effectiveRemainingLatency(BWAPI::Broodwar->getRemainingLatencyFrames(), reported, microLatencyFrames());
    }

    int predictionBufferFrames()
    {
        // Effective latency is capped at 12; callers ask for up to latency + 2
        return 14;
    }
}
