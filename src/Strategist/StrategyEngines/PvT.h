#pragma once

#include "BnetPolicy.h"

#include "StrategyEngine.h"

class PvT : public StrategyEngine
{
public:
    enum class TerranStrategy
    {
        Unknown,
        WorkerRush,         // Detected by seeing more than two workers in our main without other combat units
        BunkerContain,      // Detected by seeing enemy building an early bunker at our natural
        ProxyRush,          // Detected by seeing fewer buildings than expected in main
        MarineRush,         // Detected by seeing early barracks or early marines
        MarinePressure,     // For when we detect a marine rush but then lose it, indicating the enemy might be saving up marines for a larger push
        WallIn,             // Detected by scout blocked by buildings
        BlockScouting,      // The enemy has blocked our scout from getting into their main, suspect shenanigans
        FastExpansion,      // Natural expansion taken early
        TwoFactory,         // The enemy has built two factories early
        NormalOpening,      // Normal opening
        MidGameMech,        // Opponent has transitioned from opening into a mech-heavy composition
        MidGameBio,         // Opponent has transitioned from opening into a bio-heavy composition
        MidGameBioMech,     // Opponent has transitioned from opening into a balanced composition
        // TODO: Learn more Terran openings
        // TODO: Mid- and late game
    };
    static std::map<TerranStrategy, std::string> TerranStrategyNames;

    enum class OurStrategy
    {
        ForgeExpandGoons,       // FFE strategy we use against Random
        EarlyGameDefense,       // We don't have enough scouting data yet
        AntiBunkerContain,      //
        AntiMarineRush,         // For fast rushes, proxy rushes or any serious early pressure, defends main until it can get tech out
        FastExpansion,          // For when the opponent plays a greedy strategy
        Defensive,              // For when the opponent is playing an aggressive strategy that isn't considered a zealot rush
        NormalOpening,          // Normal non-greedy and non-cautious opening
        MidGame,                // When we have reached the mid-game
        LateGameCarriers,       // Late-game carrier strategy
        // TODO: More mid- and late-game strategies
    };
    static std::map<OurStrategy, std::string> OurStrategyNames;

    TerranStrategy enemyStrategy;
    OurStrategy ourStrategy;

    PvT() : enemyStrategy(TerranStrategy::Unknown), ourStrategy(OurStrategy::EarlyGameDefense), enemyStrategyChanged(0) {}

    void initialize(std::vector<std::shared_ptr<Play>> &plays, bool transitioningFromRandom, const std::string &openingOverride) override;

    void updatePlays(std::vector<std::shared_ptr<Play>> &plays) override;

    void updateProduction(std::vector<std::shared_ptr<Play>> &plays,
                          std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals,
                          std::vector<std::pair<int, int>> &mineralReservations) override;

    std::string getEnemyStrategy() override { return TerranStrategyNames[enemyStrategy]; }

    std::string getOurStrategy() override { return OurStrategyNames[ourStrategy]; }

    bool isEnemyRushing() override
    {
        return enemyStrategy == TerranStrategy::WorkerRush ||
               enemyStrategy == TerranStrategy::ProxyRush ||
               enemyStrategy == TerranStrategy::MarineRush;
    }

    bool isEnemyProxy() override
    {
        return enemyStrategy == TerranStrategy::ProxyRush;
    }

private:
    int enemyStrategyChanged;

    // Pluto snipe: whether we have already used the Defensive strategy this game (prevents Normal <-> Defensive oscillation)
    bool plutoDefensiveUsed = false;

    // Pluto snipe: current first-observer request stage (for logging stage changes)
    BnetPolicy::DetectionStage plutoDetectionStage = BnetPolicy::DetectionStage::Upstream;

    // Pluto snipe: cannon placement issues already logged (base tile + reason), to avoid repeating them every frame
    std::set<std::string> plutoCannonIssuesLogged;

    // Battle.net summary log: whether first_observer_complete was written
    bool plutoFirstObserverLogged = false;

    // Pluto snipe: starport / control tower / dropship seen early (drop_risk_detection), latched once seen
    bool plutoDropRisk = false;

    // Pluto snipe: army held at home after a spider mine until the first observer (mine_hold_until_observer)
    bool plutoMineHold = false;

    // Pluto snipe: home guard dragoons at the main until the first observer (home_guard_dragoons), for logging
    bool plutoHomeGuard = false;

    // Pluto snipe: second worker scout (pluto_rescout_frame) already added
    bool plutoRescoutAdded = false;

    // Pluto snipe: main cannon requested by the blind timing (blind_main_cannon_frame), for logging
    bool plutoBlindMainCannonLogged = false;

    // Pluto snipe: mid-game strategy switches suppressed by midgame_strategy_hold_frames (logged once per pair)
    int plutoMidgameHolds = 0;

    void updatePlutoSignals(std::vector<std::shared_ptr<Play>> &plays);

    TerranStrategy recognizeEnemyStrategy();

    OurStrategy chooseOurStrategy(TerranStrategy newEnemyStrategy, std::vector<std::shared_ptr<Play>> &plays);

    void handleNaturalExpansion(std::vector<std::shared_ptr<Play>> &plays, std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals);

    void handleUpgrades(std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals);

    void handleDetection(std::vector<std::shared_ptr<Play>> &plays, std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals);

    void handlePlutoNaturalCannons(std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals);
};
