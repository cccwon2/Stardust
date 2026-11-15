#include "BnetLog.h"

#include "BnetConfig.h"
#include "BnetPolicy.h"
#include "Opponent.h"
#include "Workers.h"
#include "Units.h"

#include <BWAPI.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#ifndef STARDUST_BUILD_ID
#define STARDUST_BUILD_ID "unknown"
#endif

extern int currentFrame;

namespace BnetLog
{
    const char *buildId()
    {
        return STARDUST_BUILD_ID;
    }

    namespace
    {
        std::string buildInfoLine1()
        {
            return std::string("Stardust bnet build ") + buildId();
        }

        std::string buildInfoLine2()
        {
            auto &c = BnetConfig::get();
            std::ostringstream os;
            os << "config: " << (BnetConfig::loadedFromFile() ? BnetConfig::loadedPath() : std::string("defaults (stardust_bnet.json not found)"))
               << " | pluto_pvt_snipe=" << c.plutoPvtSnipe
               << " pvt_mech_fix=" << c.pvtMechFix
               << " combat_move_guard=" << c.combatMoveGuard;
            return os.str();
        }

        const int buildInfoDrawFrames = 24 * 30;  // 30 game seconds
    }

    void printBuildInfo()
    {
        if (!BnetConfig::get().printBuildInfo) return;
        auto game = BWAPI::BroodwarPtr;
        if (!game) return;

        game->printf("%s", buildInfoLine1().c_str());
        game->printf("%s", buildInfoLine2().c_str());
    }

    void drawBuildInfo()
    {
        if (currentFrame >= buildInfoDrawFrames) return;
        if (!BnetConfig::get().printBuildInfo) return;
        auto game = BWAPI::BroodwarPtr;
        if (!game) return;

        game->drawTextScreen(8, 30, "%s", buildInfoLine1().c_str());
        game->drawTextScreen(8, 42, "%s", buildInfoLine2().c_str());
    }

    namespace
    {
        ResourceOrderStats resourceOrderStats;
        int bioToAttack = 0;
        int bioToRegroup = 0;
        int mechToAttack = 0;
        int mechToRegroup = 0;

        // Issued orders: cumulative and since the last [APM] line
        int ordersWorker = 0;
        int ordersObserver = 0;
        int ordersArmy = 0;
        int ordersWindow = 0;
        int movesSkipped = 0;
        int gridHolds = 0;
        int unstickBackoffs = 0;
        int repeatedOrdersSkipped = 0;
        int attackCommitHolds = 0;
        int regroupCommitHolds = 0;
        int fleeGuardFights = 0;
        int dragoonUnsticks = 0;
        int mechGateState = -1;     // -1 unknown, 0 open, 1 blocked
        int mechGateBlocks = 0;     // Number of open -> blocked transitions
        int invalidMoves = 0;
        int workerFlees = 0;
        int arrivedHolds = 0;
        int rearUnsticks = 0;
        int mineClearRequests = 0;
        int defenseRedirects = 0;
        int expansionGuards = 0;
        int cannonBackoffs = 0;
    }

    void rearUnstick() { rearUnsticks++; }
    void mineClearRequest() { mineClearRequests++; }
    void defenseRedirect() { defenseRedirects++; }
    void expansionGuard() { expansionGuards++; }
    void cannonBackoff() { cannonBackoffs++; }

    void arrivedMoveHeld()
    {
        arrivedHolds++;
    }

    void mechDecision(bool wasAttacking, bool nowAttacking)
    {
        if (wasAttacking == nowAttacking) return;
        if (nowAttacking) mechToAttack++;
        else mechToRegroup++;
    }

    void countIssuedOrder(OrderCategory category)
    {
        switch (category)
        {
            case OrderCategory::Worker: ordersWorker++; break;
            case OrderCategory::Observer: ordersObserver++; break;
            case OrderCategory::Army: ordersArmy++; break;
        }
        ordersWindow++;
    }

    void moveSkipped()
    {
        movesSkipped++;
    }

    void gridHold()
    {
        gridHolds++;
    }

    void unstickBackoff()
    {
        unstickBackoffs++;
    }

    void repeatedOrderSkipped()
    {
        repeatedOrdersSkipped++;
    }

    void combatCommitHold(bool attack)
    {
        if (attack) attackCommitHolds++;
        else regroupCommitHolds++;
    }

    void fleeGuardFight()
    {
        fleeGuardFights++;
    }

    void dragoonUnstick()
    {
        dragoonUnsticks++;
    }

    void workerFlee()
    {
        workerFlees++;
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
            mechToAttack = 0;
            mechToRegroup = 0;
            ordersWorker = 0;
            ordersObserver = 0;
            ordersArmy = 0;
            ordersWindow = 0;
            movesSkipped = 0;
            gridHolds = 0;
            unstickBackoffs = 0;
            repeatedOrdersSkipped = 0;
            attackCommitHolds = 0;
            regroupCommitHolds = 0;
            fleeGuardFights = 0;
            dragoonUnsticks = 0;
            mechGateState = -1;
            mechGateBlocks = 0;
            invalidMoves = 0;
            workerFlees = 0;
            arrivedHolds = 0;
            rearUnsticks = 0;
            mineClearRequests = 0;
            defenseRedirects = 0;
            expansionGuards = 0;
            cannonBackoffs = 0;
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
               << " res_blocked_backoff=" << r.blockedBackoff
               << " res_failed=" << r.failed
               << " stuck_attempts=" << rec.attempts
               << " stuck_reassign=" << rec.reassigned
               << " stuck_cmd_ok=" << rec.commandOk
               << " stuck_cmd_failed=" << rec.commandFailed
               << " stuck_budget_deferred=" << rec.budgetDeferred
               << " mining_assign_ok=" << Workers::bnetMiningStats().assignOk
               << " mining_assign_fail=" << Workers::bnetMiningStats().assignFail
               << " mining_idle_holds=" << Workers::bnetMiningStats().idleHolds
               << " mining_idle_depot_moves=" << Workers::bnetMiningStats().idleDepotMoves
               << " mining_unassigned_long=" << Workers::bnetMiningStats().unassignedLong
               << " bio_to_attack=" << bioToAttack
               << " bio_to_regroup=" << bioToRegroup
               << " mech_to_attack=" << mechToAttack
               << " mech_to_regroup=" << mechToRegroup
               << " orders_total=" << (ordersWorker + ordersObserver + ordersArmy)
               << " orders_worker=" << ordersWorker
               << " orders_observer=" << ordersObserver
               << " orders_army=" << ordersArmy
               << " moves_skipped=" << movesSkipped
               << " grid_holds=" << gridHolds
               << " unstick_backoffs=" << unstickBackoffs
               << " orders_repeat_skipped=" << repeatedOrdersSkipped
               << " attack_commit_holds=" << attackCommitHolds
               << " regroup_commit_holds=" << regroupCommitHolds
               << " flee_guard_fights=" << fleeGuardFights
               << " dragoon_unsticks=" << dragoonUnsticks
               << " micro_latency=" << BnetConfig::microLatencyFrames()
               << " mech_gate_blocks=" << mechGateBlocks
               << " invalid_moves=" << invalidMoves
               << " worker_flees=" << workerFlees
               << " arrived_holds=" << arrivedHolds
               << " rear_unsticks=" << rearUnsticks
               << " mine_clear_requests=" << mineClearRequests
               << " defense_redirects=" << defenseRedirects
               << " expansion_guards=" << expansionGuards
               << " cannon_backoffs=" << cannonBackoffs
               << " latency_max=" << BnetConfig::maxLatencyFrames()
               << " cmd_interval=" << BnetConfig::commandIntervalFrames();
            return os.str();
        }
    }

#if BNET_LOG_ENABLED
    namespace
    {
        const char *logPath = "bwapi-data/write/stardust_bnet.log";

        // Written at game start and removed after [END]; if it still exists at the next start, the previous game ended
        // without onEnd (crash, forced close or a bridge disconnect) and an [EVENT] previous_game_unfinished line is written.
        const char *markerPath = "bwapi-data/write/stardust_bnet.running";
        const int apmIntervalFrames = 24 * 60 * 2;  // 2 game minutes at fastest speed

        std::ofstream file;
        bool active = false;
        int nextApmFrame = apmIntervalFrames;
        int maxApm = 0;
        std::string lastEnemyStrategy;
        std::string lastMyStrategy;

        // Supply drop detection (supply_drop_event): supply sampled every second over the last 30 game seconds
        const int supplySampleFrames = 24;
        const int supplyWindowSamples = 30;
        int supplySamples[supplyWindowSamples] = {};
        int supplySampleCount = 0;
        int nextSupplySampleFrame = 0;
        int lastSupplyDropFrame = -100000;

        // Enemy tech seen (logged once per type)
        std::set<std::string> enemyTechLogged;
        int nextTechCheckFrame = 0;

        void resetSamplers()
        {
            supplySampleCount = 0;
            nextSupplySampleFrame = 0;
            lastSupplyDropFrame = -100000;
            enemyTechLogged.clear();
            nextTechCheckFrame = 0;
        }

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
        resetSamplers();

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

        // Read the marker of a previous game that never reached onEnd, then write our own
        std::string unfinishedGame;
        try
        {
            std::ifstream marker(markerPath);
            if (marker.good()) std::getline(marker, unfinishedGame);
        }
        catch (...) {}

        auto game = BWAPI::BroodwarPtr;
        try
        {
            std::ofstream marker(markerPath, std::ios::out | std::ios::trunc);
            marker << "build=" << buildId() << " started=" << timestamp() << " map=\"" << game->mapFileName() << "\"\n";
        }
        catch (...) {}

        std::ostringstream os;
        os << "build=" << buildId()
           << " map=\"" << game->mapFileName() << "\""
           << " opponent=\"" << Opponent::getName() << "\""
           << " enemy_race=" << (game->enemy() ? game->enemy()->getRace().getName() : "Unknown")
           << " latency_frames=" << game->getLatencyFrames()
           << " battlenet=" << game->isBattleNet()
           << " multiplayer=" << game->isMultiplayer()
           << " " << BnetConfig::summary();
        writeLine("START", os.str());

        if (!unfinishedGame.empty())
        {
            writeLine("EVENT", "previous_game_unfinished " + unfinishedGame
                               + " (no [END]: crash, forced close or disconnect; check Windows Event Viewer > Application)");
        }
    }

    void invalidMove(const std::string &unitType)
    {
        invalidMoves++;
        if (invalidMoves <= 3 || invalidMoves % 500 == 0)
        {
            writeLine("EVENT", "invalid_move unit=" + unitType + " count=" + std::to_string(invalidMoves));
        }
    }

    void resourceOrderFailed(const std::string &command, const std::string &unitType, const std::string &order,
                             const std::string &error)
    {
        int failed = ++resourceOrderStats.failed;
        if (failed <= 3 || failed % 500 == 0)
        {
            writeLine("EVENT", "res_failed cmd=" + command + " unit=" + unitType + " order=" + order + " error=\"" + error
                               + "\" count=" + std::to_string(failed));
        }
    }

    namespace
    {
        void sampleSupply()
        {
            if (currentFrame < nextSupplySampleFrame) return;
            nextSupplySampleFrame = currentFrame + supplySampleFrames;

            int minDrop = BnetConfig::get().supplyDropEvent;
            if (minDrop <= 0) return;

            int supply = BWAPI::Broodwar->self()->supplyUsed() / 2;
            supplySamples[supplySampleCount % supplyWindowSamples] = supply;
            supplySampleCount++;

            int samples = std::min(supplySampleCount, supplyWindowSamples);
            int peak = 0;
            for (int i = 0; i < samples; i++) peak = std::max(peak, supplySamples[i]);

            if (currentFrame - lastSupplyDropFrame < supplySampleFrames * supplyWindowSamples) return;
            if (!BnetPolicy::supplyDropDetected(peak, supply, minDrop)) return;
            lastSupplyDropFrame = currentFrame;

            std::ostringstream os;
            os << "supply_drop from=" << peak << " to=" << supply << " window_s=" << (supplyWindowSamples * supplySampleFrames / 24)
               << " workers=" << BWAPI::Broodwar->self()->completedUnitCount(BWAPI::Broodwar->self()->getRace().getWorker());
            writeLine("EVENT", os.str());
        }

        void checkEnemyTech()
        {
            if (currentFrame < nextTechCheckFrame) return;
            nextTechCheckFrame = currentFrame + 24;

            static const BWAPI::UnitType techTypes[] = {
                    BWAPI::UnitTypes::Protoss_Robotics_Facility,
                    BWAPI::UnitTypes::Protoss_Robotics_Support_Bay,
                    BWAPI::UnitTypes::Protoss_Observatory,
                    BWAPI::UnitTypes::Protoss_Shuttle,
                    BWAPI::UnitTypes::Protoss_Reaver,
                    BWAPI::UnitTypes::Protoss_Citadel_of_Adun,
                    BWAPI::UnitTypes::Protoss_Templar_Archives,
                    BWAPI::UnitTypes::Protoss_Dark_Templar,
                    BWAPI::UnitTypes::Protoss_High_Templar,
                    BWAPI::UnitTypes::Protoss_Stargate,
                    BWAPI::UnitTypes::Protoss_Arbiter,
                    BWAPI::UnitTypes::Protoss_Carrier,
                    BWAPI::UnitTypes::Terran_Factory,
                    BWAPI::UnitTypes::Terran_Starport,
                    BWAPI::UnitTypes::Terran_Dropship,
                    BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode,
                    BWAPI::UnitTypes::Terran_Science_Vessel,
                    BWAPI::UnitTypes::Zerg_Lair,
                    BWAPI::UnitTypes::Zerg_Spire,
                    BWAPI::UnitTypes::Zerg_Hydralisk_Den,
                    BWAPI::UnitTypes::Zerg_Lurker,
                    BWAPI::UnitTypes::Zerg_Mutalisk};
            for (const auto &type : techTypes)
            {
                if (!Units::hasEnemyBuilt(type)) continue;
                if (!enemyTechLogged.insert(type.getName()).second) continue;
                writeLine("EVENT", "enemy_tech_seen type=" + type.getName());
            }
        }
    }

    void onFrame()
    {
        if (!active) return;
        sampleSupply();
        checkEnemyTech();
        if (currentFrame < nextApmFrame) return;
        nextApmFrame = currentFrame + apmIntervalFrames;

        // BWAPI's own APM (no selections): exponentially decayed, roughly the last minute of play, not a game average
        int apm = BWAPI::Broodwar->getAPM(false);
        if (apm > maxApm) maxApm = apm;

        // Our own count of issued orders over the window (BWAPI's APM is 0 on Remastered)
        int ordersPerMin = (ordersWindow * 60 * 24) / apmIntervalFrames;
        ordersWindow = 0;

        std::ostringstream os;
        os << "orders_per_min=" << ordersPerMin
           << " bwapi_apm=" << apm
           << " latency_frames=" << BWAPI::Broodwar->getLatencyFrames()
           << " supply=" << (BWAPI::Broodwar->self()->supplyUsed() / 2)
           << " minerals=" << BWAPI::Broodwar->self()->minerals()
           << " gas=" << BWAPI::Broodwar->self()->gas()
           << " depots=" << BWAPI::Broodwar->self()->completedUnitCount(BWAPI::Broodwar->self()->getRace().getResourceDepot())
           << " refineries=" << BWAPI::Broodwar->self()->completedUnitCount(BWAPI::Broodwar->self()->getRace().getRefinery())
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

    void mining(const std::string &text)
    {
        if (!BnetConfig::get().miningAssignmentLog) return;
        writeLine("MINING", text);
    }

    void army(const std::string &text)
    {
        if (!BnetConfig::get().armyStateLog) return;
        writeLine("ARMY", text);
    }

    void defense(const std::string &text)
    {
        if (!BnetConfig::get().armyStateLog) return;
        writeLine("DEFENSE", text);
    }

    void mechExpandGate(bool blocked, int bases, int armyUnits, int required)
    {
        int state = blocked ? 1 : 0;
        if (state == mechGateState) return;
        if (blocked) mechGateBlocks++;
        bool first = mechGateState == -1;
        mechGateState = state;
        if (first && !blocked) return;  // Don't log the initial open state

        std::ostringstream os;
        os << "mech_expand_gate=" << (blocked ? "blocked" : "open")
           << " bases=" << bases << " army=" << armyUnits << " required=" << required;
        writeLine("EVENT", os.str());
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

        std::error_code ec;
        std::filesystem::remove(markerPath, ec);
    }
#else
    void invalidMove(const std::string &) { invalidMoves++; }
    void resourceOrderFailed(const std::string &, const std::string &, const std::string &, const std::string &)
    {
        resourceOrderStats.failed++;
    }
    void gameStart() { resetCounters(); }
    void onFrame() {}
    void enemyStrategyChanged(const std::string &) {}
    void myStrategyChanged(const std::string &) {}
    void event(const std::string &) {}
    void mining(const std::string &) {}
    void army(const std::string &) {}
    void defense(const std::string &) {}
    void mechExpandGate(bool blocked, int, int, int)
    {
        int state = blocked ? 1 : 0;
        if (state != mechGateState && blocked) mechGateBlocks++;
        mechGateState = state;
    }
    void gameEnd(bool) {}
#endif
}
