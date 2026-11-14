#include "StrategyEngines/PvT.h"

#include "Units.h"
#include "Map.h"
#include "UnitUtil.h"
#include "Players.h"
#include "Workers.h"
#include "Builder.h"
#include "Opponent.h"
#include "Strategist.h"
#include "BnetConfig.h"
#include "BnetPolicy.h"

#include <sstream>

#include "Plays/Macro/SaturateBases.h"
#include "Plays/MainArmy/DefendMyMain.h"
#include "Plays/MainArmy/AttackEnemyBase.h"
#include "Plays/MainArmy/ForgeFastExpand.h"
#include "Plays/Scouting/EarlyGameWorkerScout.h"
#include "Plays/Scouting/EjectEnemyScout.h"
#include "Plays/SpecialTeams/Elevator.h"
#include "Plays/SpecialTeams/ElevatorRush.h"

namespace
{
    std::map<BWAPI::UnitType, int> emptyUnitCountMap;

    template<class T, class ...Args>
    void setMainPlay(MainArmyPlay *current, Args &&...args)
    {
        if (typeid(*current) == typeid(T)) return;
        current->status.transitionTo = std::make_shared<T>(std::forward<Args>(args)...);
    }
}

void PvT::initialize(std::vector<std::shared_ptr<Play>> &plays, bool transitioningFromRandom, const std::string &openingOverride)
{
    if (!openingOverride.empty()) plays.clear();

    if (transitioningFromRandom)
    {
        plays.emplace(beforePlayIt<MainArmyPlay>(plays), std::make_shared<EjectEnemyScout>());

        if (getPlay<ForgeFastExpand>(plays))
        {
            ourStrategy = OurStrategy::ForgeExpandGoons;
        }
    }
    else
    {
        plays.emplace_back(std::make_shared<SaturateBases>());
        plays.emplace_back(std::make_shared<EarlyGameWorkerScout>());
        plays.emplace_back(std::make_shared<EjectEnemyScout>());
        plays.emplace_back(std::make_shared<DefendMyMain>());

        std::string opening = openingOverride;

        // Pluto snipe: skip per-opponent-name UCB1 and use a fixed safe opening, since Pluto changes its name every game
        auto &bnetConfig = BnetConfig::get();
        if (opening.empty() && bnetConfig.plutoPvtSnipe && !bnetConfig.forcedPvtOpening.empty())
        {
            if (bnetConfig.forcedPvtOpening == OurStrategyNames[OurStrategy::EarlyGameDefense] ||
                bnetConfig.forcedPvtOpening == OurStrategyNames[OurStrategy::FastExpansion])
            {
                opening = bnetConfig.forcedPvtOpening;
            }
            else
            {
                Log::Get() << "Unknown forced_pvt_opening '" << bnetConfig.forcedPvtOpening << "', using EarlyGameDefense";
                opening = OurStrategyNames[OurStrategy::EarlyGameDefense];
            }
            Log::Get() << "Pluto snipe: forced PvT opening " << opening << " (UCB1 skipped)";
        }

        if (opening.empty())
        {
#if VS_HUMAN
            // TODO: Vs. humans do something like a weighted random selection
            opening = OurStrategyNames[OurStrategy::EarlyGameDefense];
#else
            opening = Opponent::selectOpeningUCB1(
                    {
                            OurStrategyNames[OurStrategy::EarlyGameDefense],
                            // OurStrategyNames[OurStrategy::FastExpansion]
                    });
#endif
        }
        if (opening == OurStrategyNames[OurStrategy::FastExpansion])
        {
            ourStrategy = OurStrategy::FastExpansion;
        }

        Log::Get() << "Selected opening " << OurStrategyNames[ourStrategy];
    }

    Opponent::addMyStrategyChange(OurStrategyNames[ourStrategy]);
    Opponent::addEnemyStrategyChange(TerranStrategyNames[enemyStrategy]);
}

void PvT::updatePlays(std::vector<std::shared_ptr<Play>> &plays)
{
    auto newEnemyStrategy = recognizeEnemyStrategy();
    auto newStrategy = chooseOurStrategy(newEnemyStrategy, plays);

    if (enemyStrategy != newEnemyStrategy)
    {
#if LOGGING_ENABLED
        Log::Get() << "Enemy strategy changed from " << TerranStrategyNames[enemyStrategy] << " to " << TerranStrategyNames[newEnemyStrategy];
#endif
#if CHERRYVIS_ENABLED
        CherryVis::log() << "Enemy strategy changed from " << TerranStrategyNames[enemyStrategy] << " to " << TerranStrategyNames[newEnemyStrategy];
#endif

        enemyStrategy = newEnemyStrategy;
        enemyStrategyChanged = currentFrame;
        Opponent::addEnemyStrategyChange(TerranStrategyNames[enemyStrategy]);
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
    if (ourStrategy == OurStrategy::Defensive) plutoDefensiveUsed = true;

    bool defendOurMain;
    if (hasEnemyStolenOurGas())
    {
        defendOurMain = true;
    }
    else
    {
        switch (ourStrategy)
        {
            case OurStrategy::ForgeExpandGoons:
            {
                // Attack when we have goon range
                defendOurMain = (BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Singularity_Charge) == 0);
                break;
            }

            case OurStrategy::EarlyGameDefense:
            case OurStrategy::AntiMarineRush:
            case OurStrategy::Defensive:
                defendOurMain = true;
                break;

            case OurStrategy::AntiBunkerContain:
            {
                // Switch to attack when we have goon range
                defendOurMain = (BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Singularity_Charge) == 0);

                // Elevator some units out of our main to attack the enemy main
                {
                    auto elevatorPlay = getPlay<Elevator>(plays);
                    if (!elevatorPlay)
                    {
                        plays.emplace(beforePlayIt<MainArmyPlay>(plays),
                                      std::make_shared<Elevator>(true, BWAPI::UnitTypes::Protoss_Dragoon));
                    }
                }
            }

            default:
            {
                auto mainArmyPlay = getPlay<MainArmyPlay>(plays);
                if (!mainArmyPlay)
                {
                    defendOurMain = true;
                    break;
                }

                // Require range off of an FFE
                if (getPlay<ForgeFastExpand>(plays) && BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Singularity_Charge) == 0)
                {
                    defendOurMain = true;
                    break;
                }

                defendOurMain = false;

                // Transition from a defend squad when the vanguard cluster has 2 units
                if (mainArmyPlay->isDefensive())
                {
                    auto vanguard = mainArmyPlay->getSquad()->vanguardCluster();
                    defendOurMain = (!vanguard || vanguard->units.size() < 2);
                }

                break;
            }
        }
    }

    updateAttackPlays(plays, defendOurMain);

    // Set the worker scout to monitor the enemy choke if we've detected a non-proxy marine rush
    if (Strategist::getWorkerScoutStatus() == Strategist::WorkerScoutStatus::EnemyBaseScouted &&
        !Strategist::getStrategyEngine()->isEnemyProxy() &&
        ourStrategy == OurStrategy::AntiMarineRush)
    {
        auto play = getPlay<EarlyGameWorkerScout>(plays);
        if (play)
        {
            play->monitorEnemyChoke();
        }
    }

    updateDefendBasePlays(plays);
    updateSpecialTeamsPlays(plays);
    defaultExpansions(plays);
    scoutExpos(plays, 15000);

    // Add an elevator play if it makes sense
    {
        auto elevatorPlay = getPlay<Elevator>(plays);
        if (!elevatorPlay && Elevator::isElevatorFeasible(BWAPI::UnitTypes::Protoss_Dragoon))
        {
            plays.emplace(beforePlayIt<MainArmyPlay>(plays),
                          std::make_shared<Elevator>(false, BWAPI::UnitTypes::Protoss_Dragoon));
        }
    }

    // AIST S4 vs. Human match - Harass enemy bases with carriers after we have our first two arbiters out
    /*
    if (!defendOurMain && Units::countAll(BWAPI::UnitTypes::Protoss_Arbiter) > 1)
    {
        auto carrierHarass = getPlay<CarrierHarass>(plays);
        if (!carrierHarass)
        {
            auto beforeMainArmyIt = [&plays]()
            {
                auto it = plays.begin();
                for (; it != plays.end(); it++)
                {
                    if (std::dynamic_pointer_cast<MainArmyPlay>(*it) != nullptr)
                    {
                        break;
                    }
                }
                return it;
            };
            plays.emplace(beforeMainArmyIt(), std::make_shared<CarrierHarass>());
        }
    }
    */
}

void PvT::updateProduction(std::vector<std::shared_ptr<Play>> &plays,
                           std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals,
                           std::vector<std::pair<int, int>> &mineralReservations)
{
    reserveMineralsForExpansion(mineralReservations);
    handleNaturalExpansion(plays, prioritizedProductionGoals);
    handleDetection(plays, prioritizedProductionGoals);
    handlePlutoNaturalCannons(prioritizedProductionGoals);

    if (handleIslandExpansionProduction(plays, prioritizedProductionGoals)) return;

    auto mainArmyPlay = getPlay<MainArmyPlay>(plays);
    auto completedUnits = mainArmyPlay ? mainArmyPlay->getSquad()->getUnitCountByType() : emptyUnitCountMap;
    auto &incompleteUnits = mainArmyPlay ? mainArmyPlay->assignedIncompleteUnits : emptyUnitCountMap;

    int zealotCount = completedUnits[BWAPI::UnitTypes::Protoss_Zealot] + incompleteUnits[BWAPI::UnitTypes::Protoss_Zealot];
    int dragoonCount = completedUnits[BWAPI::UnitTypes::Protoss_Dragoon] + incompleteUnits[BWAPI::UnitTypes::Protoss_Dragoon];

    int inProgressCount = Units::countIncomplete(BWAPI::UnitTypes::Protoss_Zealot)
                          + Units::countIncomplete(BWAPI::UnitTypes::Protoss_Dragoon)
                          + Units::countIncomplete(BWAPI::UnitTypes::Protoss_Dark_Templar);

    if (!getPlay<ForgeFastExpand>(plays))
    {
        handleGasStealProduction(prioritizedProductionGoals, zealotCount);
    }

    auto midAndLateGameMainArmyProduction = [&]()
    {
        // Baseline production is one combat unit for every 6 workers (approximately 3 units per mining base)
        int higherPriorityCount = (Workers::mineralWorkers() / 6) - inProgressCount;

        // Counter tanks with speedlots once the enemy has at least four
        int enemyTanks = Units::countEnemy(BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) +
                         Units::countEnemy(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode);
        if (enemyTanks > 2)
        {
            // Keep proportionally more dragoons if the enemy has a lot of vultures
            int desiredZealots = std::min((dragoonCount * 3) / 2, enemyTanks * 3);
            if (Units::countEnemy(BWAPI::UnitTypes::Terran_Vulture) > enemyTanks)
            {
                desiredZealots = std::min(dragoonCount, enemyTanks * 2);
            }

            if (desiredZealots > zealotCount)
            {
                mainArmyProduction(prioritizedProductionGoals,
                                   BWAPI::UnitTypes::Protoss_Zealot,
                                   desiredZealots - zealotCount,
                                   higherPriorityCount);
            }

            upgradeAtCount(prioritizedProductionGoals, BWAPI::UpgradeTypes::Leg_Enhancements, BWAPI::UnitTypes::Protoss_Zealot, 0);
        }

        // Pluto snipe: against bio, keep a steady speedlot share alongside dragoons (roughly 1 zealot per dragoon)
        // and get leg enhancements early, since pure goons trade poorly into marine/medic
        if (BnetConfig::get().plutoPvtSnipe &&
            (enemyStrategy == TerranStrategy::MidGameBio || enemyStrategy == TerranStrategy::MarinePressure))
        {
            int desiredZealots = std::max(4, dragoonCount);
            if (desiredZealots > zealotCount)
            {
                mainArmyProduction(prioritizedProductionGoals,
                                   BWAPI::UnitTypes::Protoss_Zealot,
                                   desiredZealots - zealotCount,
                                   higherPriorityCount);
            }

            upgradeAtCount(prioritizedProductionGoals, BWAPI::UpgradeTypes::Leg_Enhancements, BWAPI::UnitTypes::Protoss_Zealot, 2);
        }

        mainArmyProduction(prioritizedProductionGoals, BWAPI::UnitTypes::Protoss_Dragoon, -1, higherPriorityCount);
        mainArmyProduction(prioritizedProductionGoals, BWAPI::UnitTypes::Protoss_Zealot, -1, higherPriorityCount);
    };

    switch (ourStrategy)
    {
        case OurStrategy::ForgeExpandGoons:
        {
            prioritizedProductionGoals[PRIORITY_MAINARMY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                       "SE",
                                                                       BWAPI::UnitTypes::Protoss_Dragoon,
                                                                       -1,
                                                                       -1);
            upgradeAtCount(prioritizedProductionGoals, BWAPI::UpgradeTypes::Singularity_Charge, BWAPI::UnitTypes::Protoss_Dragoon, 1);
            break;
        }

        case OurStrategy::EarlyGameDefense:
        {
            oneGateCoreOpening(prioritizedProductionGoals, dragoonCount, zealotCount, 1);
            upgradeAtCount(prioritizedProductionGoals, BWAPI::UpgradeTypes::Singularity_Charge, BWAPI::UnitTypes::Protoss_Dragoon, 1);

            break;
        }
        case OurStrategy::AntiMarineRush:
        {
            auto buildCannons = [&prioritizedProductionGoals](int desiredCount)
            {
                auto &baseStaticDefenseLocations = BuildingPlacement::baseStaticDefenseLocations(Map::getMyMain());
                if (!baseStaticDefenseLocations.isValid()) return 0;

                std::vector<BWAPI::TilePosition> locations = baseStaticDefenseLocations.workerDefenseCannons;
                if (baseStaticDefenseLocations.startBlockCannon != BWAPI::TilePositions::Invalid)
                {
                    locations.insert(locations.begin() + 1, baseStaticDefenseLocations.startBlockCannon);
                }

                int completedCannons = 0;
                for (const auto &location : locations)
                {
                    if (!location.isValid()) continue;

                    auto cannon = Units::myBuildingAt(location);
                    if (cannon && cannon->type == BWAPI::UnitTypes::Protoss_Photon_Cannon)
                    {
                        desiredCount--;

                        if (cannon->completed || cannon->estimatedCompletionFrame < (currentFrame + 200))
                        {
                            completedCannons++;
                        }

                        continue;
                    }

                    if (desiredCount > 0)
                    {
                        auto buildAtTile = [&prioritizedProductionGoals](BWAPI::TilePosition tile, BWAPI::UnitType type)
                        {
                            if (Builder::isPendingHere(tile)) return;

                            auto buildLocation = BuildingPlacement::BuildLocation(Block::Location(tile),
                                                                                  BuildingPlacement::builderFrames(Map::getMyMain()->getPosition(),
                                                                                                                   tile,
                                                                                                                   type),
                                                                                  0,
                                                                                  0);
                            prioritizedProductionGoals[PRIORITY_EMERGENCY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                                        "SE-antirush",
                                                                                        type,
                                                                                        buildLocation);
                        };

                        auto pylon = Units::myBuildingAt(baseStaticDefenseLocations.powerPylon);
                        if (pylon)
                        {
                            if (pylon->completed)
                            {
                                buildAtTile(location, BWAPI::UnitTypes::Protoss_Photon_Cannon);
                                desiredCount--;
                            }
                        }
                        else if (!pylon)
                        {
                            buildAtTile(baseStaticDefenseLocations.powerPylon, BWAPI::UnitTypes::Protoss_Pylon);
                            return completedCannons;
                        }
                    }
                }

                return completedCannons;
            };

            auto marinesHaveMovedOut = []()
            {
                // First check if there is any observed marine or barracks closer to our main than the enemy's natural choke
                auto enemyNaturalChoke = Map::getEnemyNaturalChoke();
                if (!enemyNaturalChoke) return true;

                auto navigationGrid = PathFinding::getNavigationGrid(Map::getMyMain()->getPosition(), true);
                if (!navigationGrid) return true;

                auto naturalChokeCost = (*navigationGrid)[enemyNaturalChoke->center].cost;
                for (const auto &enemyUnit : Units::allEnemy())
                {
                    if (enemyUnit->type == BWAPI::UnitTypes::Terran_Marine || enemyUnit->type == BWAPI::UnitTypes::Terran_Barracks)
                    {
                        if ((*navigationGrid)[enemyUnit->lastPosition].cost < naturalChokeCost) return true;
                    }
                }

                return false;
            };

            // Higher danger level if:
            // - Marines are on the map
            // - We have no scout left
            // - The strategy has been stable for 300 frames and our scout isn't monitoring the choke yet
            bool highDanger = marinesHaveMovedOut() || Strategist::isWorkerScoutComplete() ||
                    ((Strategist::getWorkerScoutStatus() != Strategist::WorkerScoutStatus::MonitoringEnemyChoke)
                        && enemyStrategyChanged < (currentFrame - 300));

            int requiredCannons = 0;
            if (highDanger)
            {
                if ((zealotCount + dragoonCount) < 3)
                {
                    requiredCannons = 2;
                }
                else if ((zealotCount + dragoonCount) < 5)
                {
                    requiredCannons = 1;
                }
            }

            int completedCannons = buildCannons(requiredCannons);

            // Determine how many zealots we want
            // Zealots are relatively useless against groups of kiting marines, so we want to transition to dragoons as quickly as possible
            int zealotsRequired = 0;
            if (highDanger)
            {
                zealotsRequired = std::max(0, (dragoonCount < 3 ? 3 : 2) - (completedCannons * 2) - zealotCount);
            }
            handleAntiRushProduction(prioritizedProductionGoals, dragoonCount, zealotCount, zealotsRequired, 1);

            break;
        }

        case OurStrategy::AntiBunkerContain:
        {
            // Build pure goons
            prioritizedProductionGoals[PRIORITY_MAINARMY].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                       "SE-anticontain",
                                                                       BWAPI::UnitTypes::Protoss_Dragoon,
                                                                       -1,
                                                                       -1);

            // Get goon range
            upgrade(prioritizedProductionGoals, BWAPI::UpgradeTypes::Singularity_Charge);

            // Get a shuttle
            if (Units::countAll(BWAPI::UnitTypes::Protoss_Shuttle) < 1)
            {
                prioritizedProductionGoals[PRIORITY_SPECIALTEAMS].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                               "SE",
                                                                               BWAPI::UnitTypes::Protoss_Shuttle,
                                                                               1,
                                                                               1);
            }

            break;
        }

        case OurStrategy::FastExpansion:
        case OurStrategy::Defensive:
        case OurStrategy::NormalOpening:
        {
            // If any zealots are in production, and we don't have an emergency production goal, cancel them
            if (Units::countIncomplete(BWAPI::UnitTypes::Protoss_Zealot) > 0)
            {
                cancelTrainingUnits(prioritizedProductionGoals, BWAPI::UnitTypes::Protoss_Zealot);
            }

            // Try to keep at least two army units in production while taking our natural
            int higherPriorityCount = 2 - inProgressCount;
            mainArmyProduction(prioritizedProductionGoals, BWAPI::UnitTypes::Protoss_Dragoon, -1, higherPriorityCount);

            // Default upgrades
            handleUpgrades(prioritizedProductionGoals);

            break;
        }

        case OurStrategy::MidGame:
        {
            // Have some DTs harrassing until the enemy has vessels
            // TODO: Implement DT tactics and re-enable
            /*
            int dtCount = Units::countAll(BWAPI::UnitTypes::Protoss_Dark_Templar);
            if (dtCount < 4 &&
                !Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Science_Vessel) &&
                !Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Science_Facility))
            {
                prioritizedProductionGoals[PRIORITY_NORMAL].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                         "SE-nodetect",
                                                                         BWAPI::UnitTypes::Protoss_Dark_Templar,
                                                                         1,
                                                                         1);
            }
             */

            // Against mech, get one shuttle unless the enemy has more than one goliath
            // Obs may get higher priority depending on what we have scouted
            if (Units::countAll(BWAPI::UnitTypes::Protoss_Shuttle) < 1 &&
                enemyStrategy == TerranStrategy::MidGameMech &&
                Units::countEnemy(BWAPI::UnitTypes::Terran_Goliath) < 2)
            {
                prioritizedProductionGoals[PRIORITY_SPECIALTEAMS].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                               "SE",
                                                                               BWAPI::UnitTypes::Protoss_Shuttle,
                                                                               1,
                                                                               1);
            }

            // Against mech, build arbiters on three completed nexuses and a significant army size
            // Pluto snipe: also continue into arbiters against bio / bio-mech so the tech transition isn't cut off
            // (upstream only builds arbiters against mech; there is no high templar storm micro in Stardust)
            int arbiterCount = Units::countAll(BWAPI::UnitTypes::Protoss_Arbiter);
            bool arbiterMatchup = enemyStrategy == TerranStrategy::MidGameMech ||
                                  (BnetConfig::get().plutoPvtSnipe &&
                                   (enemyStrategy == TerranStrategy::MidGameBio || enemyStrategy == TerranStrategy::MidGameBioMech));
            if (arbiterMatchup && arbiterCount < 2 && Units::countCompleted(BWAPI::UnitTypes::Protoss_Nexus) > 2
                && (zealotCount + dragoonCount) > 20)
            {
                prioritizedProductionGoals[PRIORITY_NORMAL].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                         "SE",
                                                                         BWAPI::UnitTypes::Protoss_Arbiter,
                                                                         1,
                                                                         1);
            }

            midAndLateGameMainArmyProduction();

            // Default upgrades
            handleUpgrades(prioritizedProductionGoals);

            break;
        }

        case OurStrategy::LateGameCarriers:
        {
            // Produce unlimited carriers off of two stargates, at slightly higher priority than main army
            prioritizedProductionGoals[PRIORITY_NORMAL].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                     "SE",
                                                                     BWAPI::UnitTypes::Protoss_Carrier,
                                                                     -1,
                                                                     2);

            midAndLateGameMainArmyProduction();

            // Besides default upgrades, upgrade air weapons immediately
            int weaponLevel = BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Protoss_Air_Weapons);
            if (weaponLevel < 3)
            {
                prioritizedProductionGoals[PRIORITY_NORMAL].emplace_back(std::in_place_type<UpgradeProductionGoal>,
                                                                         "SE",
                                                                         BWAPI::UpgradeTypes::Protoss_Air_Weapons,
                                                                         weaponLevel + 1,
                                                                         1);
            }
            handleUpgrades(prioritizedProductionGoals);

            break;
        }
    }
}

void PvT::handleNaturalExpansion(std::vector<std::shared_ptr<Play>> &plays,
                                 std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals)
{
    // Hop out if the natural has already been taken
    auto natural = Map::getMyNatural();
    if (!natural || natural->ownedSince != -1)
    {
        CherryVis::setBoardValue("natural", "complete");
        return;
    }

    // Don't expand while we are setting up an elevator rush
    auto elevatorRush = getPlay<ElevatorRush>(plays);
    if (elevatorRush && elevatorRush->getSquad()->empty())
    {
        CherryVis::setBoardValue("natural", "wait-elevator");
        return;
    }

    // If we have a backdoor natural, expand when our second goon is being produced or we have lots of money
    if (Map::mapSpecificOverride()->hasBackdoorNatural())
    {
        if (BWAPI::Broodwar->self()->minerals() > 450 ||
            Units::countAll(BWAPI::UnitTypes::Protoss_Dragoon) > 1)
        {
            CherryVis::setBoardValue("natural", "take-backdoor");

            takeNaturalExpansion(plays, prioritizedProductionGoals);
            return;
        }
    }

    switch (ourStrategy)
    {
        case OurStrategy::ForgeExpandGoons:
            // Handled by the play
            CherryVis::setBoardValue("natural", "forge-expand");
            return;

        case OurStrategy::EarlyGameDefense:
        case OurStrategy::AntiBunkerContain:
        case OurStrategy::AntiMarineRush:
        case OurStrategy::Defensive:
            // Don't take our natural if the enemy could be rushing or doing an all-in
            CherryVis::setBoardValue("natural", "wait-defensive");
            break;

        case OurStrategy::FastExpansion:
            CherryVis::setBoardValue("natural", "take-fast-expo");
            takeNaturalExpansion(plays, prioritizedProductionGoals);
            return;

        default:
        {
            // Expand as soon as our main army transitions to attack
            auto mainArmyPlay = getPlay<AttackEnemyBase>(plays);
            if (!mainArmyPlay)
            {
                CherryVis::setBoardValue("natural", "no-attack-play");
                break;
            }

            CherryVis::setBoardValue("natural", "take");
            takeNaturalExpansion(plays, prioritizedProductionGoals);
            return;
        }
    }

    cancelNaturalExpansion(plays, prioritizedProductionGoals);
}

void PvT::handleUpgrades(std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals)
{
    // For PvT dragoon range is important to get early, so get it first unless the enemy is rushing us
    upgradeAtCount(
            prioritizedProductionGoals,
            BWAPI::UpgradeTypes::Singularity_Charge,
            BWAPI::UnitTypes::Protoss_Dragoon,
            ourStrategy == OurStrategy::AntiMarineRush ? 2 : 0);

    // Basic infantry skill upgrades are queued when we have enough of them and are still building them
    upgradeAtCount(prioritizedProductionGoals, BWAPI::UpgradeTypes::Leg_Enhancements, BWAPI::UnitTypes::Protoss_Zealot, 6);

    // Cases where we want the upgrade as soon as we start building one of the units
    upgradeWhenUnitCreated(prioritizedProductionGoals,
                           BWAPI::UpgradeTypes::Gravitic_Boosters,
                           BWAPI::UnitTypes::Protoss_Observer,
                           false,
                           false,
                           PRIORITY_HIGHPRIORITYUPGRADES);
    upgradeWhenUnitCreated(prioritizedProductionGoals,
                           BWAPI::UpgradeTypes::Gravitic_Drive,
                           BWAPI::UnitTypes::Protoss_Shuttle,
                           false,
                           Units::countCompleted(BWAPI::UnitTypes::Protoss_Assimilator) < 3);
    upgradeWhenUnitCreated(prioritizedProductionGoals,
                           BWAPI::UpgradeTypes::Carrier_Capacity,
                           BWAPI::UnitTypes::Protoss_Carrier,
                           true,
                           false,
                           PRIORITY_HIGHPRIORITYUPGRADES);
    upgradeWhenUnitCreated(prioritizedProductionGoals,
                           BWAPI::UpgradeTypes::Khaydarin_Core,
                           BWAPI::UnitTypes::Protoss_Arbiter,
                           false,
                           false,
                           PRIORITY_HIGHPRIORITYUPGRADES);
    upgradeWhenUnitCreated(prioritizedProductionGoals,
                           BWAPI::TechTypes::Stasis_Field,
                           BWAPI::UnitTypes::Protoss_Arbiter,
                           false,
                           false,
                           PRIORITY_HIGHPRIORITYUPGRADES);

    // Get observer sight range on three gases
    if (Units::countCompleted(BWAPI::UnitTypes::Protoss_Assimilator) >= 3)
    {
        upgradeWhenUnitCreated(prioritizedProductionGoals,
                               BWAPI::UpgradeTypes::Sensor_Array,
                               BWAPI::UnitTypes::Protoss_Observer,
                               false,
                               false,
                               PRIORITY_HIGHPRIORITYUPGRADES);
    }

    defaultGroundUpgrades(prioritizedProductionGoals);

    // TODO: Air upgrades
}

void PvT::handleDetection(std::vector<std::shared_ptr<Play>> &plays, std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals)
{
    // The main army play will reactively request mobile detection when it sees a cloaked enemy unit
    // The logic here is to look ahead to make sure we already have detection available when we need it

    // Break out if we are already building an observer
    if (Units::countIncomplete(BWAPI::UnitTypes::Protoss_Observer) > 0) return;

    // Get the number of observers in our main army
    size_t observersWithMainArmy = 0;
    auto mainArmyPlay = getPlay<MainArmyPlay>(plays);
    if (mainArmyPlay && mainArmyPlay->getSquad())
    {
        observersWithMainArmy = mainArmyPlay->getSquad()->getDetectors().size();
    }

    // In PvT, since the enemy can snipe observers with comsat relatively easily, try to keep 2 observers with the main army
    if (observersWithMainArmy >= 2) return;

    auto buildObserver = [&](int priority = PRIORITY_NORMAL)
    {
        prioritizedProductionGoals[priority].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                          "SE-detection",
                                                          BWAPI::UnitTypes::Protoss_Observer,
                                                          1,
                                                          1);
    };

    // Build an observer if the enemy has cloaked wraith tech
    if (Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Control_Tower) ||
        Players::hasResearched(BWAPI::Broodwar->enemy(), BWAPI::TechTypes::Cloaking_Field))
    {
        buildObserver(PRIORITY_SPECIALTEAMS);
        return;
    }

    // Pluto snipe: first-observer policy, see BnetPolicy::decideDetection.
    // Only governs the first observer; once one exists, or when no early condition applies, the upstream policy below runs.
    // The producer adds the robotics facility and observatory prerequisites at the same priority.
    {
        auto &bnetConfig = BnetConfig::get();
        BnetPolicy::DetectionInput input{
                bnetConfig.plutoPvtSnipe && bnetConfig.earlyObserverPriority,
                Units::countCompleted(BWAPI::UnitTypes::Protoss_Observer),
                Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Vulture_Spider_Mine) ||
                Players::hasResearched(BWAPI::Broodwar->enemy(), BWAPI::TechTypes::Spider_Mines),
                enemyStrategy == TerranStrategy::TwoFactory ||
                enemyStrategy == TerranStrategy::MidGameMech ||
                enemyStrategy == TerranStrategy::MidGameBioMech,
                Units::countEnemy(BWAPI::UnitTypes::Terran_Vulture),
                Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Vulture) ||
                Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Machine_Shop),
                ourStrategy == OurStrategy::AntiMarineRush,
                currentFrame > 6500 ||
                Players::upgradeLevel(BWAPI::Broodwar->self(), BWAPI::UpgradeTypes::Singularity_Charge) > 0 ||
                BWAPI::Broodwar->self()->isUpgrading(BWAPI::UpgradeTypes::Singularity_Charge),
                Units::countCompleted(BWAPI::UnitTypes::Protoss_Dragoon),
                bnetConfig.observerMinDragoons};

        auto stage = BnetPolicy::decideDetection(input);
        if (stage != plutoDetectionStage)
        {
            Log::Get() << "Pluto detection stage: " << BnetPolicy::detectionStageName(plutoDetectionStage)
                       << " -> " << BnetPolicy::detectionStageName(stage);
            plutoDetectionStage = stage;
        }

        if (stage != BnetPolicy::DetectionStage::Upstream)
        {
            buildObserver(BnetPolicy::detectionStageIsHighPriority(stage) ? PRIORITY_SPECIALTEAMS : PRIORITY_NORMAL);
            return;
        }
    }

    // In all other cases, wait until we are on two bases
    if (Units::countCompleted(BWAPI::UnitTypes::Protoss_Nexus) < 2) return;

    // Get obs immediately if we've seen a spider mine
    if (Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Vulture_Spider_Mine))
    {
        buildObserver(PRIORITY_SPECIALTEAMS);
        return;
    }

    // Get obs earlier if we've seen a vulture or tank, indicating mech play and likely spider mines
    if (currentFrame > 10000 && (Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode) ||
                                                     Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) ||
                                                     Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Vulture)))
    {
        buildObserver();
        return;
    }

    // Otherwise start getting obs at frame 14000
    if (currentFrame > 14000)
    {
        buildObserver();
        return;
    }
}

void PvT::handlePlutoNaturalCannons(std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals)
{
    // Pluto snipe: conditional cannon placement requests in mineral lines against vulture / drop harass.
    // - Natural: natural_cannons once our natural nexus is complete (skipped for ForgeExpandGoons, handled by the FFE play).
    // - Main: main_cannons against TwoFactory / mech / 3+ vultures, independent of the natural, not while holding a marine rush.
    // Requests are at PRIORITY_NORMAL, below dragoon base production. Only the predefined workerDefenseCannons locations are
    // used, so the target count is not guaranteed (no choke cannon helper exists in Stardust).
    auto &bnetConfig = BnetConfig::get();
    if (!bnetConfig.plutoPvtSnipe) return;

    auto logOnce = [&](Base *base, const char *reason)
    {
        std::ostringstream key;
        key << base->getTilePosition() << ":" << reason;
        if (plutoCannonIssuesLogged.insert(key.str()).second)
        {
            Log::Get() << "Pluto cannons @ " << base->getTilePosition() << ": " << reason;
        }
    };

    auto requestCannonsAtBase = [&](Base *base, int target)
    {
        if (!base || target <= 0) return;
        if (!base->resourceDepot || !base->resourceDepot->completed) return;

        auto &locations = BuildingPlacement::baseStaticDefenseLocations(base);
        if (!locations.isValid())
        {
            logOnce(base, "no valid static defense locations");
            return;
        }

        auto buildAtTile = [&](BWAPI::TilePosition tile, BWAPI::UnitType type, int framesUntilPowered)
        {
            auto buildLocation = BuildingPlacement::BuildLocation(Block::Location(tile),
                                                                  BuildingPlacement::builderFrames(base->getPosition(), tile, type),
                                                                  framesUntilPowered,
                                                                  0);
            prioritizedProductionGoals[PRIORITY_NORMAL].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                     "SE-pluto-cannon",
                                                                     type,
                                                                     buildLocation);
        };

        // Power pylon first
        auto pylon = Units::myBuildingAt(locations.powerPylon);
        if (pylon && pylon->type != BWAPI::UnitTypes::Protoss_Pylon)
        {
            logOnce(base, "pylon tile occupied by another building");
            return;
        }
        if (!pylon)
        {
            if (!Builder::isPendingHere(locations.powerPylon))
            {
                buildAtTile(locations.powerPylon, BWAPI::UnitTypes::Protoss_Pylon, 0);
            }
            return;
        }

        std::vector<BnetPolicy::CannonTile> tiles;
        for (const auto &location : locations.workerDefenseCannons)
        {
            auto building = Units::myBuildingAt(location);
            if (building)
            {
                if (building->type != BWAPI::UnitTypes::Protoss_Photon_Cannon) tiles.push_back(BnetPolicy::CannonTile::Occupied);
                else if (building->completed) tiles.push_back(BnetPolicy::CannonTile::Cannon);
                else tiles.push_back(BnetPolicy::CannonTile::CannonPending);
            }
            else if (Builder::isPendingHere(location))
            {
                tiles.push_back(BnetPolicy::CannonTile::CannonPending);
            }
            else
            {
                tiles.push_back(BnetPolicy::CannonTile::Empty);
            }
        }

        auto plan = BnetPolicy::planCannons(tiles.data(), (int)tiles.size(), target);
        if (plan.shortfall > 0) logOnce(base, "not enough free cannon locations for the requested count");

        int framesUntilPowered = pylon->completed ? 0 : std::max(0, pylon->estimatedCompletionFrame - currentFrame);
        for (int i = 0; i < plan.requestCount; i++)
        {
            buildAtTile(locations.workerDefenseCannons[plan.requestIndices[i]], BWAPI::UnitTypes::Protoss_Photon_Cannon, framesUntilPowered);
        }
    };

    // Natural mineral line
    auto natural = Map::getMyNatural();
    if (ourStrategy != OurStrategy::ForgeExpandGoons && natural && natural->owner == BWAPI::Broodwar->self())
    {
        requestCannonsAtBase(natural, bnetConfig.naturalCannons);
    }

    // Main mineral line, independent of the natural
    bool mechThreat = enemyStrategy == TerranStrategy::TwoFactory ||
                      enemyStrategy == TerranStrategy::MidGameMech ||
                      enemyStrategy == TerranStrategy::MidGameBioMech ||
                      Units::countEnemy(BWAPI::UnitTypes::Terran_Vulture) > 2;
    if (bnetConfig.mainCannons > 0 && mechThreat && ourStrategy != OurStrategy::AntiMarineRush)
    {
        requestCannonsAtBase(Map::getMyMain(), bnetConfig.mainCannons);
    }
}
