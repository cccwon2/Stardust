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
        config.moveResendTolerancePx = std::clamp(config.moveResendTolerancePx, 3, 96);
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
}
