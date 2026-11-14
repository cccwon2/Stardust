#include "BnetLog.h"

#include "BnetConfig.h"
#include "Opponent.h"
#include "Workers.h"

#include <BWAPI.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

extern int currentFrame;

namespace BnetLog
{
    namespace
    {
        ResourceOrderStats resourceOrderStats;
        int bioToAttack = 0;
        int bioToRegroup = 0;
    }

    ResourceOrderStats &resourceOrders()
    {
        return resourceOrderStats;
    }

    void bioDecision(bool wasAttacking, bool nowAttacking)
    {
        if (wasAttacking == nowAttacking) return;
        if (nowAttacking) bioToAttack++;
        else bioToRegroup++;
    }

    namespace
    {
        void resetCounters()
        {
            resourceOrderStats = ResourceOrderStats();
            bioToAttack = 0;
            bioToRegroup = 0;
        }

        // Cumulative counters, appended to [APM] and [END]
        [[maybe_unused]] std::string counters()
        {
            auto &r = resourceOrderStats;
            auto &rec = Workers::bnetRecoveryStats();
            std::ostringstream os;
            os << "res_attempts=" << r.attempts
               << " res_issued=" << r.issued
               << " res_blocked_interval=" << r.blockedInterval
               << " res_blocked_already=" << r.blockedAlready
               << " res_blocked_budget=" << r.blockedBudget
               << " stuck_attempts=" << rec.attempts
               << " stuck_reassign=" << rec.reassigned
               << " stuck_cmd_ok=" << rec.commandOk
               << " stuck_cmd_failed=" << rec.commandFailed
               << " stuck_budget_deferred=" << rec.budgetDeferred
               << " bio_to_attack=" << bioToAttack
               << " bio_to_regroup=" << bioToRegroup;
            return os.str();
        }
    }

#if BNET_LOG_ENABLED
    namespace
    {
        const char *logPath = "bwapi-data/write/stardust_bnet.log";
        const int apmIntervalFrames = 24 * 60 * 2;  // 2 game minutes at fastest speed

        std::ofstream file;
        bool active = false;
        int nextApmFrame = apmIntervalFrames;
        int maxApm = 0;
        std::string lastEnemyStrategy;
        std::string lastMyStrategy;

        std::string timestamp()
        {
            std::time_t now = std::time(nullptr);
            char buffer[32];
            std::tm tmStruct{};
#ifdef _WIN32
            localtime_s(&tmStruct, &now);
#else
            localtime_r(&now, &tmStruct);
#endif
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmStruct);
            return buffer;
        }

        void writeLine(const std::string &type, const std::string &text)
        {
            if (!active) return;
            try
            {
                int seconds = (currentFrame * 42) / 1000;  // 42ms per frame at fastest speed
                file << timestamp() << " f=" << currentFrame
                     << " t=" << (seconds / 60) << ":" << (seconds % 60 < 10 ? "0" : "") << (seconds % 60)
                     << " [" << type << "] " << text << "\n";
                file.flush();
            }
            catch (...)
            {
                active = false;
            }
        }
    }

    void gameStart()
    {
        resetCounters();
        if (file.is_open()) file.close();
        active = false;
        nextApmFrame = apmIntervalFrames;
        maxApm = 0;
        lastEnemyStrategy.clear();
        lastMyStrategy.clear();

        if (!BnetConfig::get().bnetLog) return;

        try
        {
            std::error_code ec;
            std::filesystem::create_directories("bwapi-data/write", ec);
            file.open(logPath, std::ios::out | std::ios::app);
            active = file.good();
        }
        catch (...)
        {
            active = false;
        }
        if (!active) return;

        auto game = BWAPI::BroodwarPtr;
        std::ostringstream os;
        os << "map=\"" << game->mapFileName() << "\""
           << " opponent=\"" << Opponent::getName() << "\""
           << " enemy_race=" << (game->enemy() ? game->enemy()->getRace().getName() : "Unknown")
           << " latency_frames=" << game->getLatencyFrames()
           << " battlenet=" << game->isBattleNet()
           << " multiplayer=" << game->isMultiplayer()
           << " " << BnetConfig::summary();
        writeLine("START", os.str());
    }

    void onFrame()
    {
        if (!active) return;
        if (currentFrame < nextApmFrame) return;
        nextApmFrame = currentFrame + apmIntervalFrames;

        // BWAPI's own APM (no selections): exponentially decayed, roughly the last minute of play, not a game average
        int apm = BWAPI::Broodwar->getAPM(false);
        if (apm > maxApm) maxApm = apm;

        std::ostringstream os;
        os << "bwapi_apm=" << apm
           << " latency_frames=" << BWAPI::Broodwar->getLatencyFrames()
           << " supply=" << (BWAPI::Broodwar->self()->supplyUsed() / 2)
           << " " << counters();
        writeLine("APM", os.str());
    }

    void enemyStrategyChanged(const std::string &strategy)
    {
        if (strategy == lastEnemyStrategy) return;
        writeLine("ENEMY", "from=" + (lastEnemyStrategy.empty() ? std::string("-") : lastEnemyStrategy) + " to=" + strategy);
        lastEnemyStrategy = strategy;
    }

    void myStrategyChanged(const std::string &strategy)
    {
        if (strategy == lastMyStrategy) return;
        writeLine("OURS", "from=" + (lastMyStrategy.empty() ? std::string("-") : lastMyStrategy) + " to=" + strategy);
        lastMyStrategy = strategy;
    }

    void event(const std::string &text)
    {
        writeLine("EVENT", text);
    }

    void gameEnd(bool isWinner)
    {
        if (!active) return;

        std::ostringstream os;
        os << "result=" << (isWinner ? "WIN" : "LOSS")
           << " bwapi_apm_at_end=" << BWAPI::Broodwar->getAPM(false)
           << " bwapi_apm_max_sampled=" << maxApm
           << " final_enemy_strategy=" << (lastEnemyStrategy.empty() ? "-" : lastEnemyStrategy)
           << " final_our_strategy=" << (lastMyStrategy.empty() ? "-" : lastMyStrategy)
           << " " << counters();
        writeLine("END", os.str());

        file.close();
        active = false;
    }
#else
    void gameStart() { resetCounters(); }
    void onFrame() {}
    void enemyStrategyChanged(const std::string &) {}
    void myStrategyChanged(const std::string &) {}
    void event(const std::string &) {}
    void gameEnd(bool) {}
#endif
}
