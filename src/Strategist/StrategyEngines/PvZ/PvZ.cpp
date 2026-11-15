#include "StrategyEngines/PvZ.h"

#include "Units.h"
#include "Map.h"
#include "Strategist.h"
#include "Players.h"
#include "Workers.h"
#include "Opponent.h"
#include "BnetConfig.h"
#include "BnetLog.h"
#include "BnetPolicy.h"
#include "BuildingPlacement.h"
#include "Builder.h"
#include "UnitUtil.h"

#include <sstream>

#include "Plays/Macro/SaturateBases.h"
#include "Plays/MainArmy/DefendMyMain.h"
#include "Plays/MainArmy/ForgeFastExpand.h"
#include "Plays/MainArmy/AttackEnemyBase.h"
#include "Plays/Scouting/EarlyGameWorkerScout.h"
#include "Plays/Scouting/EjectEnemyScout.h"
#include "Plays/SpecialTeams/Corsairs.h"
#include "Plays/SpecialTeams/Elevator.h"

namespace
{
    std::map<BWAPI::UnitType, int> emptyUnitCountMap;

    template<class T, class ...Args>
    void setMainPlay(MainArmyPlay *current, Args &&...args)
    {
        if (typeid(*current) == typeid(T)) return;
        current->status.transitionTo = std::make_shared<T>(std::forward<Args>(args)...);
    }

    bool enemyHasOurNatural()
    {
        auto natural = Map::getMyNatural();
        return natural && natural->owner == BWAPI::Broodwar->enemy();
    }

    // Moves probe goals above `keep` below the gas tech. Depot priority also holds buildings, so only probe
    // goals are moved. An unlimited probe goal stays put until we already have `keep` probes.
    void demoteProbeProductionAbove(std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals, int keep, int newPriority)
    {
        int probes = Units::countAll(BWAPI::UnitTypes::Protoss_Probe);
        if (Strategist::getWorkerScoutStatus() != Strategist::WorkerScoutStatus::Unstarted && !Strategist::isWorkerScoutComplete())
        {
            probes--;
        }

        prioritizedProductionGoals[newPriority];
        prioritizedProductionGoals[PRIORITY_WORKERS];
        prioritizedProductionGoals[PRIORITY_DEPOTS];
        auto &newGoals = prioritizedProductionGoals[newPriority];

        auto demote = [&](std::vector<ProductionGoal> &workerGoals)
        {
            for (auto it = workerGoals.begin(); it != workerGoals.end();)
            {
                auto probeGoal = std::get_if<UnitProductionGoal>(&*it);
                if (!probeGoal || !probeGoal->unitType().isWorker())
                {
                    ++it;
                    continue;
                }
                if (probes < keep)
                {
                    int count = probeGoal->countToProduce();
                    if (count < 0)
                    {
                        ++it;
                        continue;
                    }
                    probes += count;
                    if (probes > keep)
                    {
                        int kept = keep - (probes - count);
                        probeGoal->setCountToProduce(kept);
                        newGoals.emplace_back(std::in_place_type<UnitProductionGoal>,
                                              probeGoal->requester,
                                              BWAPI::UnitTypes::Protoss_Probe,
                                              probes - keep,
                                              probeGoal->getProducerLimit(),
                                              probeGoal->getLocation());
                    }
                    ++it;
                    continue;
                }
                newGoals.push_back(std::move(*it));
                it = workerGoals.erase(it);
            }
        };

        demote(prioritizedProductionGoals[PRIORITY_WORKERS]);
        demote(prioritizedProductionGoals[PRIORITY_DEPOTS]);
    }

    void spendSurplusGas(std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals)
    {
        if (!BnetConfig::get().pvzAntiAllinFix) return;

        bool haveCore = Units::countAll(BWAPI::UnitTypes::Protoss_Cybernetics_Core) > 0;
        bool range = BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Singularity_Charge) > 0
                     || Units::isBeingUpgradedOrResearched(BWAPI::UpgradeTypes::Singularity_Charge);
        bool haveStargate = Units::countAll(BWAPI::UnitTypes::Protoss_Stargate) > 0;
        bool haveCitadel = Units::countAll(BWAPI::UnitTypes::Protoss_Citadel_of_Adun) > 0;
        bool citadelDone = Units::countCompleted(BWAPI::UnitTypes::Protoss_Citadel_of_Adun) > 0;
        bool haveArchives = Units::countAll(BWAPI::UnitTypes::Protoss_Templar_Archives) > 0;
        bool archivesDone = Units::countCompleted(BWAPI::UnitTypes::Protoss_Templar_Archives) > 0;
        bool leg = BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Leg_Enhancements) > 0
                   || Units::isBeingUpgradedOrResearched(BWAPI::UpgradeTypes::Leg_Enhancements);
        bool storm = BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Psionic_Storm)
                     || Units::isBeingUpgradedOrResearched(BWAPI::TechTypes::Psionic_Storm);
        bool haveTemplar = Units::countAll(BWAPI::UnitTypes::Protoss_High_Templar) > 0;
        bool hydra = Units::hasEnemyBuilt(BWAPI::UnitTypes::Zerg_Hydralisk_Den)
                     || Units::countEnemy(BWAPI::UnitTypes::Zerg_Hydralisk) > 0;
        bool muta = Units::hasEnemyBuilt(BWAPI::UnitTypes::Zerg_Spire)
                    || Units::countEnemy(BWAPI::UnitTypes::Zerg_Mutalisk) > 0;

        auto tech = BnetPolicy::pvzNextGasTech(true,
                                               BWAPI::Broodwar->self()->gas(),
                                               haveCore,
                                               range,
                                               haveStargate,
                                               haveCitadel,
                                               haveArchives,
                                               BnetPolicy::pvzHydraNotMuta(hydra, muta),
                                               citadelDone,
                                               leg,
                                               archivesDone,
                                               storm,
                                               haveTemplar);
        if (tech == BnetPolicy::PvzGasTech::None) return;

        // Below cannons and the probes we still need. Further probes move under this so the building is paid for.
        const int priority = PRIORITY_BASEDEFENSE;
        demoteProbeProductionAbove(prioritizedProductionGoals, 16, PRIORITY_NORMAL);

        switch (tech)
        {
            case BnetPolicy::PvzGasTech::CyberCore:
                prioritizedProductionGoals[priority].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                  "pvz-gas",
                                                                  BWAPI::UnitTypes::Protoss_Cybernetics_Core,
                                                                  1,
                                                                  1);
                break;
            case BnetPolicy::PvzGasTech::Singularity:
                prioritizedProductionGoals[priority].emplace_back(std::in_place_type<UpgradeProductionGoal>,
                                                                  "pvz-gas",
                                                                  BWAPI::UpgradeTypes::Singularity_Charge);
                break;
            case BnetPolicy::PvzGasTech::Stargate:
                prioritizedProductionGoals[priority].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                  "pvz-gas",
                                                                  BWAPI::UnitTypes::Protoss_Stargate,
                                                                  1,
                                                                  1);
                break;
            case BnetPolicy::PvzGasTech::Citadel:
                prioritizedProductionGoals[priority].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                  "pvz-gas",
                                                                  BWAPI::UnitTypes::Protoss_Citadel_of_Adun,
                                                                  1,
                                                                  1);
                break;
            case BnetPolicy::PvzGasTech::LegSpeed:
                prioritizedProductionGoals[priority].emplace_back(std::in_place_type<UpgradeProductionGoal>,
                                                                  "pvz-gas",
                                                                  BWAPI::UpgradeTypes::Leg_Enhancements);
                break;
            case BnetPolicy::PvzGasTech::Archives:
                prioritizedProductionGoals[priority].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                  "pvz-gas",
                                                                  BWAPI::UnitTypes::Protoss_Templar_Archives,
                                                                  1,
                                                                  1);
                break;
            case BnetPolicy::PvzGasTech::Storm:
                prioritizedProductionGoals[priority].emplace_back(std::in_place_type<UpgradeProductionGoal>,
                                                                  "pvz-gas",
                                                                  BWAPI::TechTypes::Psionic_Storm);
                break;
            case BnetPolicy::PvzGasTech::HighTemplar:
                prioritizedProductionGoals[priority].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                  "pvz-gas",
                                                                  BWAPI::UnitTypes::Protoss_High_Templar,
                                                                  1,
                                                                  1);
                break;
            case BnetPolicy::PvzGasTech::None:
                break;
        }
    }

    bool enemyHasHydra()
    {
        return Units::hasEnemyBuilt(BWAPI::UnitTypes::Zerg_Hydralisk_Den)
               || Units::countEnemy(BWAPI::UnitTypes::Zerg_Hydralisk) > 0;
    }

    bool enemyHasMuta()
    {
        return Units::hasEnemyBuilt(BWAPI::UnitTypes::Zerg_Spire)
               || Units::countEnemy(BWAPI::UnitTypes::Zerg_Mutalisk) > 0;
    }

    bool enemyHydraNotMuta()
    {
        return BnetPolicy::pvzHydraNotMuta(enemyHasHydra(), enemyHasMuta());
    }

    // Main mineral cannons while mutalisks are the plan, one probe at a time.
    // ForgeFastExpand only did this after the enemy strategy became MutaRush, and DefendBase
    // does not run while that play is still defending the wall. A photon cannon's required
    // building is the forge, so the producer adds a forge when this opening does not have one.
    void requestAssumedMutaMainCannons(std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals)
    {
        if (!BnetConfig::get().pvzAntiAllinFix) return;
        if (BWAPI::Broodwar->enemy()->getRace() != BWAPI::Races::Zerg) return;

        int target = BnetPolicy::pvzAssumedMutaMainCannons(enemyHasHydra(), enemyHasMuta());
        if (target <= 0) return;

        auto main = Map::getMyMain();
        if (!main) return;
        auto &locations = BuildingPlacement::baseStaticDefenseLocations(main);
        if (!locations.isValid()) return;

        int existing = 0;
        bool unstarted = false;
        BWAPI::TilePosition next = BWAPI::TilePositions::Invalid;
        for (const auto &tile : locations.workerDefenseCannons)
        {
            auto building = Units::myBuildingAt(tile);
            if (building && building->type == BWAPI::UnitTypes::Protoss_Photon_Cannon)
            {
                existing++;
                continue;
            }
            auto pending = Builder::pendingHere(tile);
            if (pending)
            {
                existing++;
                if (!pending->isConstructionStarted()) unstarted = true;
                continue;
            }
            if (!next.isValid()) next = tile;
        }
        if (unstarted || existing >= target || !next.isValid()) return;

        auto buildAt = [&](BWAPI::TilePosition tile, BWAPI::UnitType type, int framesUntilPowered)
        {
            auto buildLocation = BuildingPlacement::BuildLocation(
                    Block::Location(tile),
                    BuildingPlacement::builderFrames(main->getPosition(), tile, type),
                    framesUntilPowered,
                    0);
            prioritizedProductionGoals[PRIORITY_DEPOTS].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                     "pvz-muta",
                                                                     type,
                                                                     buildLocation,
                                                                     currentFrame);
        };

        int framesUntilPowered = 0;
        auto pylon = Units::myBuildingAt(locations.powerPylon);
        if (!pylon && !Builder::isPendingHere(locations.powerPylon))
        {
            buildAt(locations.powerPylon, BWAPI::UnitTypes::Protoss_Pylon, 0);
        }
        if (!pylon || !pylon->completed)
        {
            framesUntilPowered = Builder::framesUntilCompleted(
                    locations.powerPylon,
                    UnitUtil::BuildTime(BWAPI::UnitTypes::Protoss_Pylon) + 240);
        }
        buildAt(next, BWAPI::UnitTypes::Protoss_Photon_Cannon, framesUntilPowered);
    }
}

void PvZ::initialize(std::vector<std::shared_ptr<Play>> &plays, bool transitioningFromRandom, const std::string &openingOverride)
{
    if (!openingOverride.empty()) plays.clear();

    if (transitioningFromRandom)
    {
        plays.emplace(beforePlayIt<MainArmyPlay>(plays), std::make_shared<EjectEnemyScout>());
        plays.emplace(beforePlayIt<MainArmyPlay>(plays), std::make_shared<Corsairs>());

        auto ffePlay = getPlay<ForgeFastExpand>(plays);
        if (ffePlay)
        {
            ourStrategy = OurStrategy::FFEDragoons;
        }
    }
    else
    {
        plays.emplace_back(std::make_shared<SaturateBases>());
        plays.emplace_back(std::make_shared<EarlyGameWorkerScout>());
        plays.emplace_back(std::make_shared<EjectEnemyScout>());
        plays.emplace_back(std::make_shared<Corsairs>());

        std::string opening = openingOverride;

        // Battle.net: fixed opening instead of the per-opponent UCB1 search (opponent names change every game there)
        auto &bnetConfig = BnetConfig::get();
        if (opening.empty() && !bnetConfig.forcedPvzOpening.empty())
        {
            if (bnetConfig.forcedPvzOpening == OurStrategyNames[OurStrategy::FFEDragoons] ||
                bnetConfig.forcedPvzOpening == OurStrategyNames[OurStrategy::SairSpeedlot] ||
                bnetConfig.forcedPvzOpening == OurStrategyNames[OurStrategy::EarlyGameDefense])
            {
                opening = bnetConfig.forcedPvzOpening;
            }
            else
            {
                BnetLog::event("forced_pvz_opening_unknown value=" + bnetConfig.forcedPvzOpening + " using=UCB1");
            }
        }

        if (opening.empty())
        {
            opening = Opponent::selectOpeningUCB1(
                    {
                            OurStrategyNames[OurStrategy::FFEDragoons],
                            OurStrategyNames[OurStrategy::SairSpeedlot],
                            OurStrategyNames[OurStrategy::EarlyGameDefense]
                    });
        }
        bool forgeFastExpand = (opening == OurStrategyNames[OurStrategy::SairSpeedlot]
                                || opening == OurStrategyNames[OurStrategy::FFEDragoons])
                               && BuildingPlacement::hasForgeGatewayWall();
        // The wall pylon replaces the main supply pylon. If no probe can walk there,
        // that pylon is never ordered and the game stays at 9 supply. Use the main
        // opening instead. A reachable wall keeps forge fast expand.
        if (forgeFastExpand && !BuildingPlacement::workerCanReach(
                BuildingPlacement::getForgeGatewayWall().pylon,
                BWAPI::UnitTypes::Protoss_Pylon))
        {
            Log::Get() << "Wall pylon " << BuildingPlacement::getForgeGatewayWall().pylon
                       << " is not reachable from our probes; using EarlyGameDefense";
            BnetLog::event("pvz_wall_pylon_unreachable fallback=EarlyGameDefense");
            forgeFastExpand = false;
        }
        if (forgeFastExpand)
        {
            plays.emplace_back(std::make_shared<ForgeFastExpand>());
            if (opening == OurStrategyNames[OurStrategy::SairSpeedlot])
            {
                ourStrategy = OurStrategy::SairSpeedlot;
            }
            else
            {
                ourStrategy = OurStrategy::FFEDragoons;
            }
        }
        else
        {
            plays.emplace_back(std::make_shared<DefendMyMain>());
        }
        Log::Get() << "Selected opening " << OurStrategyNames[ourStrategy];
    }

    Opponent::addMyStrategyChange(OurStrategyNames[ourStrategy]);
    Opponent::addEnemyStrategyChange(ZergStrategyNames[enemyStrategy]);
}

void PvZ::updatePlays(std::vector<std::shared_ptr<Play>> &plays)
{
    auto newEnemyStrategy = recognizeEnemyStrategy();
    auto newStrategy = chooseOurStrategy(newEnemyStrategy, plays);

    if (enemyStrategy != newEnemyStrategy)
    {
#if LOGGING_ENABLED
        Log::Get() << "Enemy strategy changed from " << ZergStrategyNames[enemyStrategy] << " to " << ZergStrategyNames[newEnemyStrategy];
#endif
#if CHERRYVIS_ENABLED
        CherryVis::log() << "Enemy strategy changed from " << ZergStrategyNames[enemyStrategy] << " to " << ZergStrategyNames[newEnemyStrategy];
#endif

        enemyStrategy = newEnemyStrategy;
        enemyStrategyChanged = currentFrame;
        Opponent::addEnemyStrategyChange(ZergStrategyNames[enemyStrategy]);
    }

    if (ourStrategy != newStrategy)
    {
#if LOGGING_ENABLED
        Log::Get() << "Our strategy changed from " << OurStrategyNames[ourStrategy] << " to " << OurStrategyNames[newStrategy];
#endif
#if CHERRYVIS_ENABLED
        CherryVis::log() << "Our strategy changed from " << OurStrategyNames[ourStrategy] << " to " << OurStrategyNames[newStrategy];
#endif

        ourStrategy = newStrategy;
        Opponent::addMyStrategyChange(OurStrategyNames[ourStrategy]);
    }

    bool defendOurMain;
    if (hasEnemyStolenOurGas())
    {
        defendOurMain = true;
    }
    else
    {
        auto mainArmyPlay = getPlay<MainArmyPlay>(plays);
        auto canTransitionToAttack = [&](int requiredUnitCount, bool requireDragoon)
        {
            if (!mainArmyPlay)
            {
                CherryVis::setBoardValue("defend-main-reason", "no-main-army-play");
                return false;
            }

            auto vanguardCluster = mainArmyPlay->getSquad()->vanguardCluster();
            if (!vanguardCluster || !vanguardCluster->vanguard)
            {
                CherryVis::setBoardValue("defend-main-reason", "no-vanguard");
                return false;
            }

            int radius = mainArmyPlay->isDefensive() ? 200 : 250;
            bool hasDragoon = false;
            int count = 0;
            for (const auto &unit : vanguardCluster->units)
            {
                if (unit->isStaticGroundDefense()) continue;

                if (unit->getDistance(vanguardCluster->vanguard) < radius)
                {
                    if (unit->type == BWAPI::UnitTypes::Protoss_Dragoon) hasDragoon = true;
                    count++;
                }
            }

            if (count < requiredUnitCount)
            {
                CherryVis::setBoardValue("defend-main-reason", "not-enough-units");
                return false;
            }
            if (requireDragoon && !hasDragoon)
            {
                CherryVis::setBoardValue("defend-main-reason", "no-dragoon");
                return false;
            }

            // If the enemy has done a sneak attack recently, don't leave our base until we have built defensive cannons
            if (isFFE()) return true; // only relevant for non-FFE builds
            if (currentFrame > 10000) return true; // only relevant in early game
            if (enemyHasOurNatural()) return true; // exception if the enemy has our natural
            auto sneakAttack = Opponent::minValueInPreviousGames("sneakAttack", INT_MAX, 20, 0);
            if (sneakAttack <= 10000 && Units::countCompleted(BWAPI::UnitTypes::Protoss_Photon_Cannon) < 2)
            {
                CherryVis::setBoardValue("defend-main-reason", "no-anti-sneak-attack-cannons");
                return false;
            }
            return true;
        };

        switch (ourStrategy)
        {
            case OurStrategy::EarlyGameDefense:
            case OurStrategy::AntiAllIn:
            {
                defendOurMain = true;
                break;
            }
            case OurStrategy::AntiSunkenContain:
            {
                // Attack when we have +1 and speed
                defendOurMain = (BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Protoss_Ground_Weapons) == 0 ||
                                 BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Leg_Enhancements) == 0);

                // Elevator some units out of our main to attack the enemy main
                {
                    auto elevatorPlay = getPlay<Elevator>(plays);
                    if (!elevatorPlay)
                    {
                        plays.emplace(beforePlayIt<MainArmyPlay>(plays),
                                      std::make_shared<Elevator>(true, BWAPI::UnitTypes::Protoss_Zealot));
                    }
                }

                break;
            }
            case OurStrategy::SairSpeedlot:
            {
                // Attack when we have +1 and speed or 6 corsairs and an army
                // This strategy transitions to mid game as soon as it has gone on the attack, so the opposite transition is not needed
                defendOurMain = true;
                if ((BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Protoss_Ground_Weapons) > 0 &&
                     BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Leg_Enhancements) > 0)
                    || (Units::countCompleted(BWAPI::UnitTypes::Protoss_Corsair) > 5 && canTransitionToAttack(5, false)))
                {
                    defendOurMain = false;
                }

                break;
            }
            case OurStrategy::FFEDragoons:
            {
                if (!mainArmyPlay)
                {
                    defendOurMain = true;
                    break;
                }

                // Transition to attack when we have 5 units in our army and range is started
                if (mainArmyPlay->isDefensive())
                {
                    defendOurMain = true;
                    if ((BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Singularity_Charge) > 0
                         || Units::isBeingUpgradedOrResearched(BWAPI::UpgradeTypes::Singularity_Charge))
                        && canTransitionToAttack(5, true))
                    {
                        defendOurMain = false;
                    }

                    break;
                }

                // TODO: Should we ever transition back to defense?
                defendOurMain = false;
                break;
            }
            default:
            {
                if (!mainArmyPlay)
                {
                    defendOurMain = true;
                    break;
                }

                defendOurMain = false;

                // Transition from defense when appropriate
                if (mainArmyPlay->isDefensive())
                {
                    if (ourStrategy == OurStrategy::FastExpansion)
                    {
                        defendOurMain = !canTransitionToAttack(3, false);
                    }
                    else
                    {
                        defendOurMain = !canTransitionToAttack(4, true);
                    }
                }

                break;
            }
        }
    }

    updateAttackPlays(plays, defendOurMain);

    // Set the worker scout to monitor the enemy choke once the pool is finished if we are in a defensive mode
    if (Strategist::getWorkerScoutStatus() == Strategist::WorkerScoutStatus::EnemyBaseScouted &&
        (ourStrategy == OurStrategy::AntiAllIn || ourStrategy == OurStrategy::Defensive))
    {
        auto play = getPlay<EarlyGameWorkerScout>(plays);
        if (play)
        {
            auto pools = Units::allEnemyOfType(BWAPI::UnitTypes::Zerg_Spawning_Pool);
            if (!pools.empty() && (*pools.begin())->completed)
            {
                play->monitorEnemyChoke();
            }
        }
    }

    updateDefendBasePlays(plays);
    updateSpecialTeamsPlays(plays);
    // When doing an FFE, wait until at least frame 10000 to take a third
    if (!isFFE() || currentFrame > 10000)
    {
        defaultExpansions(plays);
    }
    scoutExpos(plays, 10000);
}

void PvZ::updateProduction(std::vector<std::shared_ptr<Play>> &plays,
                           std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals,
                           std::vector<std::pair<int, int>> &mineralReservations)
{
    reserveMineralsForExpansion(mineralReservations);
    handleNaturalExpansion(plays, prioritizedProductionGoals);
    handleDetection(prioritizedProductionGoals);

    if (handleIslandExpansionProduction(plays, prioritizedProductionGoals)) return;

    // +1 ground weapons as soon as the forge is up, ahead of extra gateways and dragoons. Every map.
    if (Units::countCompleted(BWAPI::UnitTypes::Protoss_Forge) > 0)
    {
        upgrade(prioritizedProductionGoals, BWAPI::UpgradeTypes::Protoss_Ground_Weapons, 1, PRIORITY_BASEDEFENSE);
    }

    // Confirmed hydras, and not mutalisks: a cannon at the entrance, not on the mineral line.
    if (enemyHydraNotMuta())
    {
        buildDefensiveCannons(prioritizedProductionGoals, true, 0, 0);
    }

    auto mainArmyPlay = getPlay<MainArmyPlay>(plays);
    auto completedUnits = mainArmyPlay ? mainArmyPlay->getSquad()->getUnitCountByType() : emptyUnitCountMap;
    auto &incompleteUnits = mainArmyPlay ? mainArmyPlay->assignedIncompleteUnits : emptyUnitCountMap;

    int zealotCount = completedUnits[BWAPI::UnitTypes::Protoss_Zealot] + incompleteUnits[BWAPI::UnitTypes::Protoss_Zealot];
    int dragoonCount = completedUnits[BWAPI::UnitTypes::Protoss_Dragoon] + incompleteUnits[BWAPI::UnitTypes::Protoss_Dragoon];
    int dtCount = completedUnits[BWAPI::UnitTypes::Protoss_Dark_Templar] + incompleteUnits[BWAPI::UnitTypes::Protoss_Dark_Templar];
    int corsairCount = Units::countAll(BWAPI::UnitTypes::Protoss_Corsair);

    int inProgressCount = Units::countIncomplete(BWAPI::UnitTypes::Protoss_Zealot)
                          + Units::countIncomplete(BWAPI::UnitTypes::Protoss_Dragoon)
                          + Units::countIncomplete(BWAPI::UnitTypes::Protoss_Dark_Templar)
                          + Units::countIncomplete(BWAPI::UnitTypes::Protoss_Corsair);

    if (!isFFE())
    {
        handleGasStealProduction(prioritizedProductionGoals, zealotCount);
    }

    auto buildAntiSneakAttackCannons = [&]()
    {
        // Not relevant when we are doing an FFE
        if (isFFE())
        {
            CherryVis::setBoardValue("anti-sneak-attack", "ffe");
            return;
        }

        // Only relevant in early game
        if (currentFrame > 10000) return;

        // Check if the enemy has done a sneak attack recently
        auto sneakAttack = Opponent::minValueInPreviousGames("sneakAttack", INT_MAX, 20, 0);
        if (sneakAttack > 10000) return;

        // Build three cannons
        buildDefensiveCannons(prioritizedProductionGoals, false, 0, 3);
    };

    auto desiredCorsairs = [&]()
    {
        // Limit to 7 + 1 for every 2 enemy mutalisks
        int limit = 7 + (Units::countEnemy(BWAPI::UnitTypes::Zerg_Mutalisk) / 2);
        int desired = limit - corsairCount;
        if (desired <= 0) return 0;

        // Always build if we have DTs to force enemy to defend its overlords
        if (dtCount > 0) return desired;

        // Always build if the enemy has air units that can threaten our bases
        if (Units::hasEnemyBuilt(BWAPI::UnitTypes::Zerg_Mutalisk)) return desired;

        // Don't build if the enemy has defended its overlords
        auto &grid = Players::grid(BWAPI::Broodwar->enemy());
        int defendedOverlords = 0;
        int undefendedOverlords = 0;
        for (auto &overlord : Units::allEnemyOfType(BWAPI::UnitTypes::Zerg_Overlord))
        {
            if (!overlord->lastPositionValid) continue;

            long threat = grid.airThreat(overlord->lastPosition);
            ((threat == 0) ? undefendedOverlords : defendedOverlords)++;
        }
        if (defendedOverlords > undefendedOverlords) return 0;

        return desired;
    };

    // Main army production
    switch (ourStrategy)
    {
        case OurStrategy::EarlyGameDefense:
        {
            // We start with two-gate zealots until we have more scouting information
            prioritizedProductionGoals[PRIORITY_MAINARMY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                       "SE",
                                                                       BWAPI::UnitTypes::Protoss_Zealot,
                                                                       -1,
                                                                       2);
            break;
        }
        case OurStrategy::AntiAllIn:
        {
            // Get at least six zealots before dragoons, more if our choke is hard to defend
            int desiredZealots = 6;
            auto mainChoke = Map::getMyMainChoke();
            if (mainChoke && !mainChoke->isNarrowChoke)
            {
                desiredZealots = 10;
            }

            // Also bump up the number of zealots if the enemy has a lot of lings
            desiredZealots = std::max(desiredZealots, 2 + Units::countEnemy(BWAPI::UnitTypes::Zerg_Zergling) / 3);

            int zealotsRequired = desiredZealots - zealotCount;

            handleAntiRushProduction(prioritizedProductionGoals, dragoonCount, zealotCount, zealotsRequired);

            // We can build anti-sneak-attack cannons when we have started goon range
            if (Units::isBeingUpgradedOrResearched(BWAPI::UpgradeTypes::Singularity_Charge))
            {
                CherryVis::setBoardValue("anti-sneak-attack", "started-goon-range");
                buildAntiSneakAttackCannons();
            }
            else
            {
                CherryVis::setBoardValue("anti-sneak-attack", "not-started-goon-range");
            }

            break;
        }

        case OurStrategy::AntiSunkenContain:
        {
            // Cancel goons, goon range
            cancelTrainingUnits(prioritizedProductionGoals, BWAPI::UnitTypes::Protoss_Dragoon);
            if (Units::isBeingUpgradedOrResearched(BWAPI::UpgradeTypes::Singularity_Charge))
            {
                for (const auto &core : Units::allMineCompletedOfType(BWAPI::UnitTypes::Protoss_Cybernetics_Core))
                {
                    if (core->bwapiUnit->isUpgrading())
                    {
                        core->bwapiUnit->cancelUpgrade();
                    }
                }
            }

            // Build pure zealots
            prioritizedProductionGoals[PRIORITY_MAINARMY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                       "SE-anticontain",
                                                                       BWAPI::UnitTypes::Protoss_Zealot,
                                                                       -1,
                                                                       -1);

            // Get +1 weapons, then zealot speed
            upgrade(prioritizedProductionGoals, BWAPI::UpgradeTypes::Protoss_Ground_Weapons);
            if (Units::isBeingUpgradedOrResearched(BWAPI::UpgradeTypes::Protoss_Ground_Weapons) ||
                BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Protoss_Ground_Weapons) > 0)
            {
                upgrade(prioritizedProductionGoals, BWAPI::UpgradeTypes::Leg_Enhancements);
            }

            // Get a shuttle
            if (Units::countAll(BWAPI::UnitTypes::Protoss_Shuttle) < 1)
            {
                prioritizedProductionGoals[PRIORITY_SPECIALTEAMS].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                               "SE",
                                                                               BWAPI::UnitTypes::Protoss_Shuttle,
                                                                               1,
                                                                               1);
            }

            // Take one expansion using a shuttle
            if (Units::countAll(BWAPI::UnitTypes::Protoss_Nexus) < 2) takeExpansionWithShuttle(plays);

            break;
        }

        case OurStrategy::FastExpansion:
        {
            // We've scouted a non-threatening opening, so generally skip zealots and go straight for dragoons
            // Build a couple of zealots though if we have seen zerglings on the way and have nothing to defend with
            if (Units::countEnemy(BWAPI::UnitTypes::Zerg_Zergling) > 0)
            {
                int unitCount = zealotCount + dragoonCount;
                if (unitCount < 2)
                {
                    prioritizedProductionGoals[PRIORITY_BASEDEFENSE].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                                  "SE-fe",
                                                                                  BWAPI::UnitTypes::Protoss_Zealot,
                                                                                  2 - unitCount,
                                                                                  2);
                }
            }

            prioritizedProductionGoals[PRIORITY_MAINARMY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                       "SE-fe",
                                                                       BWAPI::UnitTypes::Protoss_Dragoon,
                                                                       -1,
                                                                       -1);
            prioritizedProductionGoals[PRIORITY_MAINARMY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                       "SE-fe",
                                                                       BWAPI::UnitTypes::Protoss_Zealot,
                                                                       -1,
                                                                       -1);

            // Default upgrades
            handleUpgrades(prioritizedProductionGoals);

            // Build anti-sneak-attack cannons immediately
            buildAntiSneakAttackCannons();

            break;
        }

        case OurStrategy::SairSpeedlot:
        {
            bool isEnemyMutaBuild = (enemyStrategy == ZergStrategy::MutaRush) ||
                                    Units::countEnemy(BWAPI::UnitTypes::Zerg_Mutalisk) > 5;

            int corsairs = std::max(7 - corsairCount, desiredCorsairs());

            // First sair is prioritized highest
            if (corsairCount == 0)
            {
                prioritizedProductionGoals[PRIORITY_DEPOTS + 1].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                             "SE",
                                                                             BWAPI::UnitTypes::Protoss_Corsair,
                                                                             1,
                                                                             1);
                corsairs--;
            }

            // First zealots are prioritized next
            if (zealotCount < 2)
            {
                prioritizedProductionGoals[PRIORITY_DEPOTS + 1].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                             "SE",
                                                                             BWAPI::UnitTypes::Protoss_Zealot,
                                                                             2 - zealotCount,
                                                                             1);
            }

            if (!isEnemyMutaBuild || corsairs > 4)
            {
                upgrade(prioritizedProductionGoals, BWAPI::UpgradeTypes::Protoss_Ground_Weapons, 1, PRIORITY_NORMAL);
                upgrade(prioritizedProductionGoals, BWAPI::UpgradeTypes::Protoss_Air_Weapons, 1, PRIORITY_NORMAL);
            }
            // Zealot speed pulls a citadel. That stays on the confirmed-hydra plan.
            if (enemyHydraNotMuta())
            {
                upgrade(prioritizedProductionGoals, BWAPI::UpgradeTypes::Leg_Enhancements, 1, PRIORITY_NORMAL);
            }

            if (corsairs > 0)
            {
                prioritizedProductionGoals[PRIORITY_MAINARMY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                           "SE",
                                                                           BWAPI::UnitTypes::Protoss_Corsair,
                                                                           corsairs,
                                                                           ((Units::countEnemy(BWAPI::UnitTypes::Zerg_Mutalisk) > 2) ? 2 : 1));
            }

            if (isEnemyMutaBuild)
            {
                prioritizedProductionGoals[PRIORITY_MAINARMY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                           "SE",
                                                                           BWAPI::UnitTypes::Protoss_Dragoon,
                                                                           -1,
                                                                           -1);
            }

            prioritizedProductionGoals[PRIORITY_MAINARMY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                       "SE",
                                                                       BWAPI::UnitTypes::Protoss_Zealot,
                                                                       -1,
                                                                       -1);

            break;
        }

        case OurStrategy::FFEDragoons:
        {
            // Scale our desired zealots based on enemy ling count
            // Start with one before goons
            int unitCount = zealotCount + dragoonCount;
            int cannonCount = Units::countCompleted(BWAPI::UnitTypes::Protoss_Photon_Cannon);
            int desiredZealots = std::max(1 - unitCount, (1 + (Units::countEnemy(BWAPI::UnitTypes::Zerg_Zergling) + 2) / 3) - cannonCount);

            // Battle.net (pvz_antiallin_fix): minimum zealots vs. pool first, more and above probes vs. an all-in.
            // Upstream subtracts completed cannons, so with two wall cannons and few scouted lings it stayed at one zealot.
            auto &bnetConfig = BnetConfig::get();
            bool bnetAllIn = bnetConfig.pvzAntiAllinFix && enemyStrategy == ZergStrategy::ZerglingAllIn;
            bool bnetPoolFirst = bnetConfig.pvzAntiAllinFix &&
                                 (enemyStrategy == ZergStrategy::PoolBeforeHatchery || bnetAllIn);
            int bnetMinZealots = BnetPolicy::pvzMinZealots(bnetConfig.pvzAntiAllinFix,
                                                           bnetPoolFirst,
                                                           bnetAllIn,
                                                           bnetConfig.pvzPoolFirstZealots,
                                                           bnetConfig.pvzAllinZealots,
                                                           Units::countEnemy(BWAPI::UnitTypes::Zerg_Zergling));
            desiredZealots = std::max(desiredZealots, bnetMinZealots);
            if (enemyHydraNotMuta())
            {
                int hydras = Units::countEnemy(BWAPI::UnitTypes::Zerg_Hydralisk);
                if (hydras < 4 && Units::hasEnemyBuilt(BWAPI::UnitTypes::Zerg_Hydralisk_Den)) hydras = 4;
                desiredZealots = std::max(desiredZealots, std::min(8, hydras));
            }
            bnetLogDefenseState(BnetPolicy::pvzWallCannonTarget(bnetConfig.pvzAntiAllinFix, bnetPoolFirst, bnetConfig.pvzPoolFirstCannons),
                                bnetMinZealots,
                                BnetPolicy::pvzProbeCap(bnetConfig.pvzAntiAllinFix, bnetAllIn, bnetConfig.pvzAllinProbeCap));

            if (zealotCount < desiredZealots)
            {
                prioritizedProductionGoals[bnetAllIn ? (PRIORITY_EMERGENCY + 1) : PRIORITY_BASEDEFENSE].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                              "SE",
                                                                              BWAPI::UnitTypes::Protoss_Zealot,
                                                                              desiredZealots - zealotCount,
                                                                              -1);
            }

            prioritizedProductionGoals[PRIORITY_MAINARMY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                       "SE",
                                                                       BWAPI::UnitTypes::Protoss_Dragoon,
                                                                       -1,
                                                                       -1);
            prioritizedProductionGoals[PRIORITY_MAINARMY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                       "SE",
                                                                       BWAPI::UnitTypes::Protoss_Zealot,
                                                                       -1,
                                                                       -1);

            // Upgrade goon range at 1 dragoon
            upgradeAtCount(prioritizedProductionGoals, BWAPI::UpgradeTypes::Singularity_Charge, BWAPI::UnitTypes::Protoss_Dragoon, 1);

            // Upgrade +1 attack at 2 zealots
            upgradeAtCount(prioritizedProductionGoals, BWAPI::UpgradeTypes::Protoss_Ground_Weapons, BWAPI::UnitTypes::Protoss_Zealot, 2);

            // Leg speed at 5 zealots inserts the upgrade into the zealot goal, and the producer builds
            // the citadel as its prerequisite ahead of the stargate. Only when hydras are confirmed.
            if (enemyHydraNotMuta())
            {
                upgradeAtCount(prioritizedProductionGoals, BWAPI::UpgradeTypes::Leg_Enhancements, BWAPI::UnitTypes::Protoss_Zealot, 5);
            }

            break;
        }

        case OurStrategy::Defensive:
        case OurStrategy::Normal:
        {
            // Scale our desired zealots based on enemy ling count
            // Start with three before goons
            int unitCount = zealotCount + dragoonCount;
            int desiredZealots = std::max(3 - unitCount, 1 + (Units::countEnemy(BWAPI::UnitTypes::Zerg_Zergling) + 2) / 3);
            if (zealotCount < desiredZealots)
            {
                prioritizedProductionGoals[PRIORITY_BASEDEFENSE].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                              "SE",
                                                                              BWAPI::UnitTypes::Protoss_Zealot,
                                                                              desiredZealots - zealotCount,
                                                                              2);

                cancelTrainingUnits(prioritizedProductionGoals,
                                    BWAPI::UnitTypes::Protoss_Dragoon,
                                    desiredZealots - zealotCount,
                                    BWAPI::UnitTypes::Protoss_Zealot.buildTime());
            }

            int corsairs = desiredCorsairs();
            if (corsairs > 0)
            {
                prioritizedProductionGoals[PRIORITY_MAINARMY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                           "SE",
                                                                           BWAPI::UnitTypes::Protoss_Corsair,
                                                                           1,
                                                                           1);
            }

            prioritizedProductionGoals[PRIORITY_MAINARMY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                       "SE",
                                                                       BWAPI::UnitTypes::Protoss_Dragoon,
                                                                       -1,
                                                                       -1);
            prioritizedProductionGoals[PRIORITY_MAINARMY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                       "SE",
                                                                       BWAPI::UnitTypes::Protoss_Zealot,
                                                                       -1,
                                                                       -1);

            // Upgrade goon range at 2 dragoons
            upgradeAtCount(prioritizedProductionGoals, BWAPI::UpgradeTypes::Singularity_Charge, BWAPI::UnitTypes::Protoss_Dragoon, 2);

            // Upgrade zealots at varying cutoffs depending on whether we already have a forge
            if (Units::countAll(BWAPI::UnitTypes::Protoss_Forge) > 0)
            {
                upgradeAtCount(prioritizedProductionGoals, BWAPI::UpgradeTypes::Protoss_Ground_Weapons, BWAPI::UnitTypes::Protoss_Zealot, 4);
            }
            else
            {
                upgradeAtCount(prioritizedProductionGoals, BWAPI::UpgradeTypes::Protoss_Ground_Weapons, BWAPI::UnitTypes::Protoss_Zealot, 6);
            }

            // Build anti-sneak-attack cannons on 4 completed units
            // Exception is if the enemy has taken our natural, in which case we skip this
            if (enemyHasOurNatural())
            {
                CherryVis::setBoardValue("anti-sneak-attack", "enemy-has-our-natural");
            }
            else if ((zealotCount + dragoonCount - inProgressCount) >= 4)
            {
                CherryVis::setBoardValue("anti-sneak-attack", "enough-units");
                buildAntiSneakAttackCannons();
            }
            else
            {
                CherryVis::setBoardValue("anti-sneak-attack", "not-enough-units");
            }

            break;
        }
        case OurStrategy::MidGame:
        {
            // TODO: Higher-tech units

            // Baseline production is one combat unit for every 6 workers (approximately 3 units per mining base)
            int higherPriorityCount = (Workers::mineralWorkers() / 6) - inProgressCount;

            // Compute enemy unit mix
            int enemyLings = Units::countEnemy(BWAPI::UnitTypes::Zerg_Zergling);
            int enemyHydras = Units::countEnemy(BWAPI::UnitTypes::Zerg_Hydralisk);
            int enemyMutas = Units::countEnemy(BWAPI::UnitTypes::Zerg_Lurker_Egg)
                    + Units::countEnemy(BWAPI::UnitTypes::Zerg_Lurker)
                    + Units::countEnemy(BWAPI::UnitTypes::Zerg_Mutalisk);

            // Simulate future units if we've seen tech. Hydras not seen: count the assumed mutalisks
            // the same way a seen spire is counted, so the mix does not stay on lings.
            if (enemyHydras == 0 && Units::countEnemy(BWAPI::UnitTypes::Zerg_Hydralisk_Den) > 0) enemyHydras = 3;
            if (enemyMutas == 0 && Units::countEnemy(BWAPI::UnitTypes::Zerg_Spire) > 0) enemyMutas = 3;
            if (enemyMutas == 0 && BnetPolicy::pvzAssumeMutalisks(enemyHasHydra(), enemyHasMuta())) enemyMutas = 3;

            // Compute the enemy ling percentage, weighted to count mutas a bit higher
            double enemyLingPercentage = 1.0;
            if ((enemyLings + enemyHydras + enemyMutas) > 0)
            {
                enemyLingPercentage = (double)enemyLings / (double)(enemyLings + enemyHydras + enemyMutas * 2);
            }

            // Now use this to determine our unit mix
            // We prefer zealots more and more as the enemy ling percentage goes up, but still build some goons
            double desiredZealotRatio = BnetPolicy::pvzHydraZealotRatio(enemyHydraNotMuta(), enemyLingPercentage * 0.8);

            double actualZealotRatio = 0.0;
            if ((zealotCount + dragoonCount) > 0)
            {
                actualZealotRatio = (double)zealotCount / (double)(zealotCount + dragoonCount);
            }

            int corsairs = desiredCorsairs();
            if (corsairs > 0)
            {
                int toBuild = 1;
                int stargates = 1;

                // Boost the count to build at a time if the enemy has mutas
                if (corsairs > 2 && Units::countEnemy(BWAPI::UnitTypes::Zerg_Mutalisk) > 4)
                {
                    toBuild = 2;
                    stargates = 2;
                    if (Units::countCompleted(BWAPI::UnitTypes::Protoss_Nexus) > 1)
                    {
                        toBuild = 3;
                    }
                }

                mainArmyProduction(prioritizedProductionGoals, BWAPI::UnitTypes::Protoss_Corsair, toBuild, higherPriorityCount, stargates);
            }
            if (actualZealotRatio > desiredZealotRatio)
            {
                mainArmyProduction(prioritizedProductionGoals, BWAPI::UnitTypes::Protoss_Dragoon, -1, higherPriorityCount);
            }
            mainArmyProduction(prioritizedProductionGoals, BWAPI::UnitTypes::Protoss_Zealot, -1, higherPriorityCount);

            // Make sure we get weapons upgrade for zealots
            upgradeAtCount(prioritizedProductionGoals, BWAPI::UpgradeTypes::Protoss_Ground_Weapons, BWAPI::UnitTypes::Protoss_Zealot, 4);

            handleUpgrades(prioritizedProductionGoals);
            buildAntiSneakAttackCannons();

            break;
        }
    }

    // Floating gas used to sit on the core, dragoon range, stargate, citadel and archives.
    // Against confirmed hydras the next item is the citadel, then leg speed, archives, storm, and templars.
    // Otherwise the stargate comes before the citadel. Every opening, not only forge fast expand:
    // a wall that cannot be walked still has to cover the assumed mutalisks.
    spendSurplusGas(prioritizedProductionGoals);

    // Corsairs once that stargate exists. One at a time, ahead of the zealot/dragoon line.
    {
        int target = BnetPolicy::pvzAssumedMutaCorsairTarget(enemyHasHydra(), enemyHasMuta(),
                                                             Units::countEnemy(BWAPI::UnitTypes::Zerg_Mutalisk));
        int have = Units::countAll(BWAPI::UnitTypes::Protoss_Corsair);
        if (target > have && Units::countAll(BWAPI::UnitTypes::Protoss_Stargate) > 0)
        {
            prioritizedProductionGoals[PRIORITY_SPECIALTEAMS].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                           "pvz-muta",
                                                                           BWAPI::UnitTypes::Protoss_Corsair,
                                                                           1,
                                                                           1);
        }
    }

    requestAssumedMutaMainCannons(prioritizedProductionGoals);

    if (enemyHydraNotMuta()
        && (BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Psionic_Storm)
            || Units::isBeingUpgradedOrResearched(BWAPI::TechTypes::Psionic_Storm)))
    {
        int have = Units::countAll(BWAPI::UnitTypes::Protoss_High_Templar);
        int hydras = Units::countEnemy(BWAPI::UnitTypes::Zerg_Hydralisk);
        int want = hydras / 4;
        if (want < 2) want = 2;
        if (have < want)
        {
            prioritizedProductionGoals[PRIORITY_MAINARMY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                       "pvz-hydra",
                                                                       BWAPI::UnitTypes::Protoss_High_Templar,
                                                                       want - have,
                                                                       1);
        }
    }
}

void PvZ::handleNaturalExpansion(std::vector<std::shared_ptr<Play>> &plays,
                                 std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals)
{
    // Hop out if the natural has already been taken
    auto natural = Map::getMyNatural();
    if (!natural || natural->ownedSince != -1)
    {
        CherryVis::setBoardValue("natural", "complete");
        return;
    }

    // If we have a backdoor natural, expand when our third goon is being produced or we have lots of money
    if (Map::mapSpecificOverride()->hasBackdoorNatural())
    {
        if (BWAPI::Broodwar->self()->minerals() > 450 ||
            Units::countAll(BWAPI::UnitTypes::Protoss_Dragoon) > 2)
        {
            CherryVis::setBoardValue("natural", "take-backdoor");

            takeNaturalExpansion(plays, prioritizedProductionGoals);
            return;
        }
    }

    switch (ourStrategy)
    {
        case OurStrategy::EarlyGameDefense:
        case OurStrategy::AntiAllIn:
        case OurStrategy::AntiSunkenContain:
        case OurStrategy::Defensive:
            // Don't take our natural if the enemy could be rushing or doing an all-in
            CherryVis::setBoardValue("natural", "wait-defensive");
            break;

        case OurStrategy::SairSpeedlot:
        case OurStrategy::FFEDragoons:
            // Handled by the play
            CherryVis::setBoardValue("natural", "forge-expand");
            return;

        case OurStrategy::FastExpansion:
            CherryVis::setBoardValue("natural", "take-fast-expo");
            takeNaturalExpansion(plays, prioritizedProductionGoals);
            return;

        case OurStrategy::Normal:
        case OurStrategy::MidGame:
        {
            // In this case we want to expand when we consider it safe to do so: we have an attacking or containing army
            // that is close to the enemy base

            auto mainArmyPlay = getPlay<AttackEnemyBase>(plays);
            if (!mainArmyPlay)
            {
                CherryVis::setBoardValue("natural", "no-attack-play");
                break;
            }

            auto corsairCount = Units::countCompleted(BWAPI::UnitTypes::Protoss_Corsair);
            auto squad = mainArmyPlay->getSquad();
            auto squadUnitCount = squad ? squad->getUnits().size() : 0;
            if (squadUnitCount < 5 || (corsairCount == 0 && squadUnitCount < 8))
            {
                CherryVis::setBoardValue("natural", "attack-play-too-small");
                break;
            }

            int dist;
            auto vanguardCluster = squad->vanguardCluster(&dist);
            if (!vanguardCluster)
            {
                CherryVis::setBoardValue("natural", "no-vanguard-cluster");
                break;
            }

            // Cluster should be past our own natural
            int naturalDist = PathFinding::GetGroundDistance(natural->getPosition(), mainArmyPlay->base->getPosition());
            if (naturalDist != -1 && dist > (naturalDist - 320))
            {
                CherryVis::setBoardValue("natural", "vanguard-cluster-too-close");
                break;
            }

            // Cluster should not be moving or fleeing
            // In other words, we want the cluster to be in some kind of stable attack or contain state
            if (vanguardCluster->currentActivity == UnitCluster::Activity::Moving
                || vanguardCluster->isFleeing())
            {
                // We don't cancel a queued expansion in this case
                CherryVis::setBoardValue("natural", "vanguard-cluster-not-attacking");
                return;
            }

            CherryVis::setBoardValue("natural", "take");
            takeNaturalExpansion(plays, prioritizedProductionGoals);
            return;
        }
    }

    cancelNaturalExpansion(plays, prioritizedProductionGoals);
}

void PvZ::handleUpgrades(std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals)
{
    // Basic infantry skill upgrades are queued when we have enough of them and are still building them.
    // Zealot speed is the confirmed-hydra plan. It is what builds the citadel ahead of a stargate.
    if (enemyHydraNotMuta())
    {
        upgradeAtCount(prioritizedProductionGoals, BWAPI::UpgradeTypes::Leg_Enhancements, BWAPI::UnitTypes::Protoss_Zealot, 5);
    }
    upgradeAtCount(prioritizedProductionGoals, BWAPI::UpgradeTypes::Singularity_Charge, BWAPI::UnitTypes::Protoss_Dragoon, 2);

    // Cases where we want the upgrade as soon as we start building one of the units
    upgradeWhenUnitCreated(prioritizedProductionGoals, BWAPI::UpgradeTypes::Gravitic_Drive, BWAPI::UnitTypes::Protoss_Shuttle, false, true);
    upgradeWhenUnitCreated(prioritizedProductionGoals, BWAPI::UpgradeTypes::Carrier_Capacity, BWAPI::UnitTypes::Protoss_Carrier, true);

    // Upgrade observer speed on three gas
    if (Units::countCompleted(BWAPI::UnitTypes::Protoss_Assimilator) >= 3)
    {
        upgradeWhenUnitCreated(prioritizedProductionGoals, BWAPI::UpgradeTypes::Gravitic_Boosters, BWAPI::UnitTypes::Protoss_Observer);
    }

    defaultGroundUpgrades(prioritizedProductionGoals);

    // Air weapon upgrades
    if (!Units::isBeingUpgradedOrResearched(BWAPI::UpgradeTypes::Protoss_Air_Weapons))
    {
        // Keep one level ahead of enemy armor when we have 5 corsairs
        if (BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Protoss_Air_Weapons) <=
            BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Zerg_Flyer_Carapace))
        {
            upgradeAtCount(prioritizedProductionGoals, BWAPI::UpgradeTypes::Protoss_Air_Weapons, BWAPI::UnitTypes::Protoss_Corsair, 5);
        }
    }
}

void PvZ::handleDetection(std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals)
{
    // The main army play will reactively request mobile detection when it sees a cloaked enemy unit
    // The logic here is to look ahead to make sure we already have detection available when we need it

    // Break out if we already have an observer
    if (Units::countCompleted(BWAPI::UnitTypes::Protoss_Observer) > 0 || Units::countIncomplete(BWAPI::UnitTypes::Protoss_Observer) > 0)
    {
        return;
    }

    bool enemyHasLurkerTech = Units::hasEnemyBuilt(BWAPI::UnitTypes::Zerg_Lurker_Egg) ||
                              Units::hasEnemyBuilt(BWAPI::UnitTypes::Zerg_Lurker) ||
                              Players::hasResearched(BWAPI::Broodwar->enemy(), BWAPI::TechTypes::Lurker_Aspect);

    auto buildObserver = [&]()
    {
        prioritizedProductionGoals[PRIORITY_NORMAL].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                 "SE",
                                                                 BWAPI::UnitTypes::Protoss_Observer,
                                                                 1,
                                                                 1);
    };

    // If we are doing an FFE, only build an observer if we have seen lurkers
    if (isFFE())
    {
        if (enemyHasLurkerTech)
        {
            buildObserver();
        }
        return;
    }

    // If the opponent has done a lurker rush recently, build a cannon at the choke to protect our main
    auto lurkerInMain = Opponent::minValueInPreviousGames("firstLurkerAtOurMain", INT_MAX, 20, 0);
    if (lurkerInMain < 12000)
    {
        buildDefensiveCannons(prioritizedProductionGoals, true, lurkerInMain - 500, 2);
    }

    // Build an observer when we are on two gas or the enemy has lurker tech
    if (Units::countCompleted(BWAPI::UnitTypes::Protoss_Assimilator) > 1 ||
        (Units::countCompleted(BWAPI::UnitTypes::Protoss_Nexus) > 1 && currentFrame > 10000) ||
        enemyHasLurkerTech)
    {
        buildObserver();
    }
}

bool PvZ::isFFE()
{
    // Return true if we are executing a FFE strategy
    switch (ourStrategy)
    {
        case OurStrategy::SairSpeedlot:
        case OurStrategy::FFEDragoons:
            return true;
        default:
            break;
    }

    return hasCannonAtWall();
}

void PvZ::bnetLogDefenseState(int wallCannons, int minZealots, int probeCap)
{
    if (!BnetConfig::get().pvzAntiAllinFix) return;

    std::ostringstream os;
    os << "pvz_defense enemy=" << ZergStrategyNames[enemyStrategy]
       << " wall_cannons=" << wallCannons
       << " min_zealots=" << minZealots
       << " probe_cap=" << probeCap;
    auto state = os.str();
    if (state == bnetDefenseState) return;

    bnetDefenseState = state;
    BnetLog::event(state);
}
