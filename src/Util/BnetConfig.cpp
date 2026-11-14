#include "BnetConfig.h"

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
        fromFile = false;
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
                read(json, "natural_cannons", config.naturalCannons);
                read(json, "bio_aggression_scale", config.bioAggressionScale);
                read(json, "bnet_log", config.bnetLog);

                fromFile = true;
                path = candidate;
                break;
            }
            catch (...)
            {
                // Parse error: fall back to defaults and keep looking
                config = Config();
            }
        }

        // Clamp values to sane ranges
        config.stuckWorkerFrames = std::clamp(config.stuckWorkerFrames, 12, 1000);
        config.maxOrdersPerFrame = std::max(0, config.maxOrdersPerFrame);
        config.minCommandIntervalFrames = std::clamp(config.minCommandIntervalFrames, -1, 48);
        config.naturalCannons = std::clamp(config.naturalCannons, 0, 2);
        config.bioAggressionScale = std::clamp(config.bioAggressionScale, 0.1, 2.0);
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
           << " natural_cannons=" << config.naturalCannons
           << " bio_aggression_scale=" << config.bioAggressionScale
           << " bnet_log=" << config.bnetLog;
        return os.str();
    }

    int commandIntervalFrames()
    {
        if (config.minCommandIntervalFrames >= 0) return config.minCommandIntervalFrames;

        // Auto: only active in high-latency environments (Battle.net is typically 5-6 frames)
        int latency = BWAPI::BroodwarPtr ? BWAPI::Broodwar->getLatencyFrames() : 0;
        if (latency > 4) return latency + 2;
        return 0;
    }
}
