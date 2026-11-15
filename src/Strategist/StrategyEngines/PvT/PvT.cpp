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
#include "BnetLog.h"

#include <sstream>

#include "Plays/Macro/SaturateBases.h"
#include "Plays/MainArmy/DefendMyMain.h"
#include "Plays/MainArmy/AttackEnemyBase.h"
#include "Plays/MainArmy/ForgeFastExpand.h"
#include "Plays/Scouting/EarlyGameWorkerScout.h"
#include "Plays/Scouting/EjectEnemyScout.h"
#include "Plays/Scouting/PlutoRescout.h"
#include "General.h"
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

    // pvt_mech_fix: active while pluto_pvt_snipe is on and the enemy is recognized as MidGameMech
    bool isMidGameStrategy(PvT::TerranStrategy strategy)
    {
        return strategy == PvT::TerranStrategy::MidGameMech ||
               strategy == PvT::TerranStrategy::MidGameBio ||
               strategy == PvT::TerranStrategy::MidGameBioMech;
    }

    // Enemy early bio / rush recognized (or we are holding one): no blind main cannon / home guard reasoning needed
    bool earlyBioOrRush(PvT::TerranStrategy enemyStrategy, PvT::OurStrategy ourStrategy)
    {
        return enemyStrategy == PvT::TerranStrategy::MarineRush ||
               enemyStrategy == PvT::TerranStrategy::BunkerContain ||
               enemyStrategy == PvT::TerranStrategy::ProxyRush ||
               enemyStrategy == PvT::TerranStrategy::WorkerRush ||
               enemyStrategy == PvT::TerranStrategy::MarinePressure ||
               ourStrategy == PvT::OurStrategy::AntiMarineRush;
    }

    bool enemyMineSeen()
    {
        return Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Vulture_Spider_Mine) ||
               Players::hasResearched(BWAPI::Broodwar->enemy(), BWAPI::TechTypes::Spider_Mines);
    }

    bool bnetMechFixActive(PvT::TerranStrategy enemyStrategy)
    {
        auto &bnetConfig = BnetConfig::get();
        return bnetConfig.plutoPvtSnipe && bnetConfig.pvtMechFix && enemyStrategy == PvT::TerranStrategy::MidGameMech;
    }

    // Completed main-army combat units (zealots + dragoons), used by the mech army gates
    int bnetArmyUnitCount()
    {
        return Units::countCompleted(BWAPI::UnitTypes::Protoss_Zealot) + Units::countCompleted(BWAPI::UnitTypes::Protoss_Dragoon);
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

void PvT::updatePlutoSignals(std::vector<std::shared_ptr<Play>> &plays)
{
    auto &bnetConfig = BnetConfig::get();
    if (!bnetConfig.plutoPvtSnipe) return;

    // Drop risk: starport / control tower / dropship before ~8:20
    if (!plutoDropRisk && BnetPolicy::dropRisk(bnetConfig.dropRiskDetection,
                                               Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Starport),
                                               Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Control_Tower),
                                               Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Dropship),
                                               currentFrame,
                                               12000))
    {
        plutoDropRisk = true;
        std::ostringstream ev;
        ev << "drop_risk starport=" << Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Starport)
           << " control_tower=" << Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Control_Tower)
           << " dropship=" << Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Dropship);
        Log::Get() << "Pluto " << ev.str();
        BnetLog::event(ev.str());
    }

    // Second worker scout
    if (!plutoRescoutAdded && bnetConfig.plutoRescoutFrame > 0 && currentFrame >= bnetConfig.plutoRescoutFrame)
    {
        plutoRescoutAdded = true;
        if (!earlyBioOrRush(enemyStrategy, ourStrategy) && Map::getEnemyStartingMain())
        {
            plays.emplace(beforePlayIt<MainArmyPlay>(plays), std::make_shared<PlutoRescout>(bnetConfig.plutoRescoutMaxFrames));
        }
        else
        {
            BnetLog::event(std::string("rescout_skipped reason=") + (Map::getEnemyStartingMain() ? "early_bio_or_rush" : "enemy_main_unknown"));
        }
    }
}

void PvT::updatePlays(std::vector<std::shared_ptr<Play>> &plays)
{
    auto newEnemyStrategy = recognizeEnemyStrategy();

    // midgame_strategy_hold_frames: don't flap between MidGameMech / MidGameBio / MidGameBioMech every few seconds
    {
        auto &bnetConfig = BnetConfig::get();
        if (bnetConfig.plutoPvtSnipe && newEnemyStrategy != enemyStrategy &&
            !BnetPolicy::midgameSwitchAllowed(isMidGameStrategy(enemyStrategy),
                                              isMidGameStrategy(newEnemyStrategy),
                                              currentFrame - enemyStrategyChanged,
                                              bnetConfig.midgameStrategyHoldFrames))
        {
            if (plutoMidgameHolds++ == 0)
            {
                BnetLog::event(std::string("midgame_switch_held from=") + TerranStrategyNames[enemyStrategy]
                               + " to=" + TerranStrategyNames[newEnemyStrategy]);
            }
            newEnemyStrategy = enemyStrategy;
        }
    }

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

    // mine_hold_until_observer: after a spider mine is seen, don't walk the army over mines until the first observer is out
    {
        auto &bnetConfig = BnetConfig::get();
        bool hold = BnetPolicy::mineHoldActive(bnetConfig.plutoPvtSnipe,
                                               bnetConfig.mineHoldUntilObserver,
                                               enemyMineSeen(),
                                               Units::countCompleted(BWAPI::UnitTypes::Protoss_Observer),
                                               currentFrame,
                                               bnetConfig.mineHoldMaxFrame);
        if (hold != plutoMineHold)
        {
            plutoMineHold = hold;
            BnetLog::event(std::string("mine_hold ") + (hold ? "start" : "end")
                           + " observers=" + std::to_string(Units::countCompleted(BWAPI::UnitTypes::Protoss_Observer)));
        }
        if (hold) defendOurMain = true;
    }

    // expansion_defense_redirect: upstream abandons a 3rd+ base when more than 5 dragoons' worth of enemies attack it.
    // When the main army is strong enough it defends that base instead (Backrooms 1.1: 3rd and 4th lost to ~30 units
    // while a 23-28 unit army sat in front of the enemy minefield).
    Base *redirectTarget = nullptr;
    {
        auto &bnetConfig = BnetConfig::get();
        auto mainArmyPlay = getPlay<MainArmyPlay>(plays);
        auto enemyValueAt = [](Base *base)
        {
            int value = 0;
            for (const auto &unit : Units::enemyAtBase(base)) value += CombatSim::unitValue(unit);
            return value;
        };
        auto endRedirect = [&](const char *reason)
        {
            if (!plutoDefenseRedirectBase) return;
            std::ostringstream ev;
            ev << "defense_redirect end base=" << plutoDefenseRedirectBase->getTilePosition().x << ","
               << plutoDefenseRedirectBase->getTilePosition().y << " reason=" << reason
               << " frames=" << (currentFrame - plutoDefenseRedirectStart);
            BnetLog::event(ev.str());
            plutoDefenseRedirectBase = nullptr;
        };

        if (bnetConfig.plutoPvtSnipe && bnetConfig.expansionDefenseRedirect && !defendOurMain && mainArmyPlay
            && !mainArmyPlay->isDefensive())
        {
            if (plutoDefenseRedirectBase)
            {
                if (plutoDefenseRedirectBase->owner != BWAPI::Broodwar->self())
                {
                    endRedirect("base_lost");
                }
                else
                {
                    if (enemyValueAt(plutoDefenseRedirectBase) > 0) plutoDefenseRedirectLastEnemy = currentFrame;
                    if (!BnetPolicy::defenseRedirectKeep(currentFrame - plutoDefenseRedirectLastEnemy,
                                                         currentFrame - plutoDefenseRedirectStart,
                                                         480,
                                                         2880))
                    {
                        endRedirect(currentFrame - plutoDefenseRedirectStart >= 2880 ? "timeout" : "clear");
                    }
                }
            }

            if (!plutoDefenseRedirectBase)
            {
                Base *best = nullptr;
                int bestValue = 0;
                for (auto base : Map::getMyBases())
                {
                    if (!base || base == Map::getMyMain() || base == Map::getMyNatural()) continue;
                    if (base->island || base->mineralPatchCount() < 3) continue;
                    int value = enemyValueAt(base);
                    if (value > bestValue)
                    {
                        best = base;
                        bestValue = value;
                    }
                }

                if (best)
                {
                    int armyValue = 0;
                    for (const auto &unit : mainArmyPlay->getSquad()->getUnits()) armyValue += CombatSim::unitValue(unit);
                    int minEnemyValue = bnetConfig.defenseRedirectMinDragoons
                                        * CombatSim::unitValue(BWAPI::UnitTypes::Protoss_Dragoon);
                    bool start = BnetPolicy::defenseRedirectStart(true,
                                                                  bestValue,
                                                                  armyValue,
                                                                  minEnemyValue,
                                                                  bnetConfig.defenseRedirectRatio);
                    std::ostringstream ev;
                    ev << "base=" << best->getTilePosition().x << "," << best->getTilePosition().y
                       << " enemy_value=" << bestValue << " army_value=" << armyValue
                       << " min_enemy_value=" << minEnemyValue << " ratio=" << bnetConfig.defenseRedirectRatio;
                    if (start)
                    {
                        plutoDefenseRedirectBase = best;
                        plutoDefenseRedirectStart = currentFrame;
                        plutoDefenseRedirectLastEnemy = currentFrame;
                        BnetLog::defenseRedirect();
                        BnetLog::event("defense_redirect start " + ev.str());
                    }
                    else if (bestValue >= minEnemyValue && currentFrame - plutoDefenseRedirectSkipLogged >= 480)
                    {
                        plutoDefenseRedirectSkipLogged = currentFrame;
                        BnetLog::defense("redirect_skip " + ev.str());
                    }
                }
            }

            redirectTarget = plutoDefenseRedirectBase;
        }
        else
        {
            endRedirect("inactive");
        }
    }

    updateAttackPlays(plays, defendOurMain, redirectTarget);

    updatePlutoSignals(plays);

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

    // home_guard_dragoons: keep dragoons at the main against drops until the first observer is out
    int homeGuardValue = 0;
    {
        auto &bnetConfig = BnetConfig::get();
        bool guard = BnetPolicy::homeGuardActive(bnetConfig.plutoPvtSnipe,
                                                 bnetConfig.homeGuardDragoons,
                                                 currentFrame,
                                                 bnetConfig.homeGuardUntilFrame,
                                                 Units::countCompleted(BWAPI::UnitTypes::Protoss_Observer))
                     && !earlyBioOrRush(enemyStrategy, ourStrategy);
        if (guard)
        {
            homeGuardValue = BnetPolicy::homeGuardValue(bnetConfig.homeGuardDragoons,
                                                        CombatSim::unitValue(BWAPI::UnitTypes::Protoss_Zealot));
        }
        if (guard != plutoHomeGuard)
        {
            plutoHomeGuard = guard;
            BnetLog::event(std::string("home_guard ") + (guard ? "start" : "end")
                           + " dragoons=" + std::to_string(bnetConfig.homeGuardDragoons));
        }
    }

    // expansion_guard_dragoons: keep dragoons at every 3rd+ base vs mech (the 3rd / 4th had 0-2 defenders on Backrooms 1.1)
    int expansionGuardValue = 0;
    {
        auto &bnetConfig = BnetConfig::get();
        bool mechThreat = enemyStrategy == TerranStrategy::TwoFactory ||
                          enemyStrategy == TerranStrategy::MidGameMech ||
                          enemyStrategy == TerranStrategy::MidGameBioMech ||
                          Units::countEnemy(BWAPI::UnitTypes::Terran_Vulture) > 2 ||
                          Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Vulture_Spider_Mine);
        bool guard = BnetPolicy::expansionGuardActive(bnetConfig.plutoPvtSnipe,
                                                      bnetConfig.expansionGuardDragoons,
                                                      mechThreat,
                                                      earlyBioOrRush(enemyStrategy, ourStrategy));
        if (guard)
        {
            expansionGuardValue = BnetPolicy::homeGuardValue(bnetConfig.expansionGuardDragoons,
                                                             CombatSim::unitValue(BWAPI::UnitTypes::Protoss_Zealot));
        }
        if (guard != plutoExpansionGuard)
        {
            plutoExpansionGuard = guard;
            BnetLog::event(std::string("expansion_guard ") + (guard ? "start" : "end")
                           + " dragoons=" + std::to_string(bnetConfig.expansionGuardDragoons));
        }
    }

    updateDefendBasePlays(plays, homeGuardValue, expansionGuardValue);
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
        // pvt_mech_fix: against mech use mech_workers_per_army_unit (default 4), since the bot kept out-expanding its army
        bool mechFix = bnetMechFixActive(enemyStrategy);
        int workersPerArmyUnit = mechFix ? BnetConfig::get().mechWorkersPerArmyUnit : 6;
        int higherPriorityCount = BnetPolicy::armyBaselineUnits(Workers::mineralWorkers(), workersPerArmyUnit) - inProgressCount;

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

            // pvt_mech_fix: keep the army dragoon-heavy against tanks, vultures and mines (default 0.5 zealot per dragoon)
            if (mechFix)
            {
                desiredZealots = BnetPolicy::mechZealotTarget(dragoonCount, enemyTanks, BnetConfig::get().mechZealotRatio);
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

    // pvt_mech_fix: against mech, defer observer speed / sight and shuttle speed (which also builds a robotics support bay)
    // until the army reaches mech_luxury_upgrade_min_army, so gas goes into dragoons first
    bool luxuryUpgrades = !bnetMechFixActive(enemyStrategy) ||
                          BnetPolicy::mechLuxuryUpgradeAllowed(bnetArmyUnitCount(), BnetConfig::get().mechLuxuryUpgradeMinArmy);

    // Cases where we want the upgrade as soon as we start building one of the units
    if (luxuryUpgrades)
    {
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
    }
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
    if (luxuryUpgrades && Units::countCompleted(BWAPI::UnitTypes::Protoss_Assimilator) >= 3)
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

    // Battle.net summary log: first observer completion frame (any matchup state)
    if (!plutoFirstObserverLogged && Units::countCompleted(BWAPI::UnitTypes::Protoss_Observer) > 0)
    {
        plutoFirstObserverLogged = true;
        BnetLog::event("first_observer_complete");
    }

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
                enemyStrategy == TerranStrategy::MidGameBioMech ||
                plutoDropRisk,
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
            std::ostringstream reason;
            reason << "detection_stage from=" << BnetPolicy::detectionStageName(plutoDetectionStage)
                   << " to=" << BnetPolicy::detectionStageName(stage)
                   << " mine=" << input.mineSeenOrResearched
                   << " twofact_or_mech=" << input.twoFactoryOrMech
                   << " vultures=" << input.enemyVultures
                   << " vulture_or_shop=" << input.vultureOrMachineShopSeen
                   << " anti_marine_rush=" << input.antiMarineRush
                   << " timing=" << input.timingReached
                   << " dragoons=" << input.completedDragoons
                   << " observers=" << input.completedObservers;
            Log::Get() << "Pluto " << reason.str();
            BnetLog::event(reason.str());
            plutoDetectionStage = stage;
        }

        if (stage != BnetPolicy::DetectionStage::Upstream)
        {
            // observer_emergency_priority: once a mine is seen (or a drop is likely and the observatory is done), the observer
            // goes above probes; in the KnockOut game the observatory finished at 5:46 but the observer only started at 8:49
            auto tier = BnetPolicy::observerPriority(stage,
                                                     bnetConfig.observerEmergencyPriority,
                                                     Units::countCompleted(BWAPI::UnitTypes::Protoss_Observatory) > 0,
                                                     Units::countCompleted(BWAPI::UnitTypes::Protoss_Robotics_Facility) > 0);
            int priority = tier == BnetPolicy::ObserverPriority::Emergency
                           ? PRIORITY_EMERGENCY
                           : (tier == BnetPolicy::ObserverPriority::SpecialTeams ? PRIORITY_SPECIALTEAMS : PRIORITY_NORMAL);
            buildObserver(priority);
            return;
        }
    }

    // Build an observer if the enemy has cloaked wraith tech
    if (Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Control_Tower) ||
        Players::hasResearched(BWAPI::Broodwar->enemy(), BWAPI::TechTypes::Cloaking_Field))
    {
        buildObserver(PRIORITY_SPECIALTEAMS);
        return;
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
    // - Natural: natural_cannons once our natural nexus is complete (skipped for ForgeExpandGoons, handled by the FFE play),
    //   raised to mech_natural_cannons under a mech / vulture threat.
    // - Main: main_cannons against TwoFactory / mech / 3+ vultures / spider mines, not while holding a marine rush.
    // - Other bases: expansion_cannons under the same threat, once each nexus is placed (expansion_cannon_at_nexus_start;
    //   upstream / off: once it is complete).
    // Requests are at PRIORITY_NORMAL, below dragoon base production, except the first cannon (and its pylon) of a base
    // under a threat with cannon_threat_priority, which goes to PRIORITY_BASEDEFENSE (the forge follows as a prerequisite).
    // Only the predefined workerDefenseCannons locations are used, so the target count is not guaranteed (no choke cannon
    // helper exists in Stardust).
    auto &bnetConfig = BnetConfig::get();
    if (!bnetConfig.plutoPvtSnipe) return;

    auto logOnce = [&](Base *base, const char *reason)
    {
        std::ostringstream key;
        key << base->getTilePosition() << ":" << reason;
        if (plutoCannonIssuesLogged.insert(key.str()).second)
        {
            Log::Get() << "Pluto cannons @ " << base->getTilePosition() << ": " << reason;
            BnetLog::event(std::string("cannon_issue base=")
                           + (base == Map::getMyMain() ? "main" : (base == Map::getMyNatural() ? "natural" : "expansion"))
                           + " tile=" + std::to_string(base->getTilePosition().x) + "," + std::to_string(base->getTilePosition().y)
                           + " reason=\"" + reason + "\"");
        }
    };

    // Mech / vulture threat: drives the main, raised natural and expansion cannon targets and the urgent first cannon
    bool mechThreat = enemyStrategy == TerranStrategy::TwoFactory ||
                      enemyStrategy == TerranStrategy::MidGameMech ||
                      enemyStrategy == TerranStrategy::MidGameBioMech ||
                      Units::countEnemy(BWAPI::UnitTypes::Terran_Vulture) > 2 ||
                      Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Vulture_Spider_Mine);

    // Main threat: the mech threat, a likely drop (starport / control tower / dropship) or the blind timing
    // (blind_main_cannon_frame) unless we face early bio / a rush
    bool blindMain = BnetPolicy::blindMainCannonDue(currentFrame, bnetConfig.blindMainCannonFrame,
                                                    earlyBioOrRush(enemyStrategy, ourStrategy));
    bool mainThreat = mechThreat || plutoDropRisk || blindMain;
    if (blindMain && !mechThreat && !plutoDropRisk && !plutoBlindMainCannonLogged)
    {
        plutoBlindMainCannonLogged = true;
        BnetLog::event("blind_main_cannon frame=" + std::to_string(bnetConfig.blindMainCannonFrame));
    }

    auto requestCannonsAtBase = [&](Base *base, int target, const char *kind, bool threat)
    {
        if (!base || target <= 0) return;
        if (!BnetPolicy::cannonBaseReady(base->resourceDepot != nullptr,
                                         base->resourceDepot && base->resourceDepot->completed,
                                         std::string(kind) == "expansion",
                                         bnetConfig.expansionCannonAtNexusStart))
        {
            return;
        }

        auto &locations = BuildingPlacement::baseStaticDefenseLocations(base);
        if (!locations.isValid())
        {
            logOnce(base, "no valid static defense locations");
            return;
        }

        auto buildAtTile = [&](BWAPI::TilePosition tile, BWAPI::UnitType type, int framesUntilPowered, bool urgent)
        {
            auto buildLocation = BuildingPlacement::BuildLocation(Block::Location(tile),
                                                                  BuildingPlacement::builderFrames(base->getPosition(), tile, type),
                                                                  framesUntilPowered,
                                                                  0);
            prioritizedProductionGoals[urgent ? PRIORITY_BASEDEFENSE : PRIORITY_NORMAL].emplace_back(std::in_place_type<UnitProductionGoal>,
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
                buildAtTile(locations.powerPylon,
                            BWAPI::UnitTypes::Protoss_Pylon,
                            0,
                            BnetPolicy::cannonRequestUrgent(bnetConfig.cannonThreatPriority, threat, 0, 0));
            }
            return;
        }

        std::vector<BnetPolicy::CannonTile> tiles;
        int backoffTiles = 0;
        for (const auto &location : locations.workerDefenseCannons)
        {
            int tileKey = location.x * 1000 + location.y;
            auto building = Units::myBuildingAt(location);
            if (building)
            {
                if (building->type != BWAPI::UnitTypes::Protoss_Photon_Cannon) tiles.push_back(BnetPolicy::CannonTile::Occupied);
                else if (building->completed) tiles.push_back(BnetPolicy::CannonTile::Cannon);
                else tiles.push_back(BnetPolicy::CannonTile::CannonPending);
                if (building->type == BWAPI::UnitTypes::Protoss_Photon_Cannon) plutoCannonTilesSeen.insert(tileKey);
            }
            else if (Builder::isPendingHere(location))
            {
                tiles.push_back(BnetPolicy::CannonTile::CannonPending);
            }
            else
            {
                // cannon_rebuild_backoff_frames: a cannon (or cannon under construction) at this tile was lost
                if (plutoCannonTilesSeen.erase(tileKey) > 0 && bnetConfig.cannonRebuildBackoffFrames > 0)
                {
                    plutoCannonTileLost[tileKey] = currentFrame;
                    BnetLog::cannonBackoff();
                    BnetLog::event("cannon_backoff tile=" + std::to_string(location.x) + "," + std::to_string(location.y)
                                   + " frames=" + std::to_string(bnetConfig.cannonRebuildBackoffFrames));
                }
                auto lostIt = plutoCannonTileLost.find(tileKey);
                if (lostIt != plutoCannonTileLost.end()
                    && BnetPolicy::cannonTileBackoff(lostIt->second, currentFrame, bnetConfig.cannonRebuildBackoffFrames))
                {
                    tiles.push_back(BnetPolicy::CannonTile::Occupied);
                    backoffTiles++;
                }
                else
                {
                    tiles.push_back(BnetPolicy::CannonTile::Empty);
                }
            }
        }

        auto plan = BnetPolicy::planCannons(tiles.data(), (int)tiles.size(), target);
        if (plan.shortfall > backoffTiles) logOnce(base, "not enough free cannon locations for the requested count");

        int framesUntilPowered = pylon->completed ? 0 : std::max(0, pylon->estimatedCompletionFrame - currentFrame);
        for (int i = 0; i < plan.requestCount; i++)
        {
            bool urgent = BnetPolicy::cannonRequestUrgent(bnetConfig.cannonThreatPriority, threat, plan.existing, i);
            buildAtTile(locations.workerDefenseCannons[plan.requestIndices[i]],
                        BWAPI::UnitTypes::Protoss_Photon_Cannon,
                        framesUntilPowered,
                        urgent);
        }

        // One [EVENT] line per base and target count, so the log shows when each base got its cannons requested
        if (plan.requestCount > 0)
        {
            std::ostringstream key;
            key << base->getTilePosition() << ":plan:" << target;
            if (plutoCannonIssuesLogged.insert(key.str()).second)
            {
                std::ostringstream ev;
                ev << "cannon_plan base=" << kind << " tile=" << base->getTilePosition().x << "," << base->getTilePosition().y
                   << " target=" << target << " existing=" << plan.existing << " requested=" << plan.requestCount
                   << " urgent=" << BnetPolicy::cannonRequestUrgent(bnetConfig.cannonThreatPriority, threat, plan.existing, 0)
                   << " mech_threat=" << mechThreat
                   << " drop_risk=" << plutoDropRisk;
                BnetLog::event(ev.str());
            }
        }
    };

    using Kind = BnetPolicy::CannonBaseKind;
    auto cannonTargetFor = [&](Kind kind, bool threat)
    {
        return BnetPolicy::cannonTarget(kind,
                                        threat,
                                        bnetConfig.naturalCannons,
                                        bnetConfig.mechNaturalCannons,
                                        bnetConfig.mainCannons,
                                        bnetConfig.expansionCannons);
    };

    // Natural mineral line (raised to mech_natural_cannons under a mech / vulture threat)
    auto natural = Map::getMyNatural();
    if (ourStrategy != OurStrategy::ForgeExpandGoons && natural && natural->owner == BWAPI::Broodwar->self())
    {
        requestCannonsAtBase(natural, cannonTargetFor(Kind::Natural, mechThreat), "natural", mechThreat);
    }

    // Main mineral line, independent of the natural
    auto mainBase = Map::getMyMain();
    if (ourStrategy != OurStrategy::AntiMarineRush)
    {
        requestCannonsAtBase(mainBase, cannonTargetFor(Kind::Main, mainThreat), "main", mainThreat);
    }

    // Every other owned base (third and later): expansion_cannons under a mech / vulture threat.
    // In the 2026-11-15 Fighting Spirit replay the third base went 13 minutes without a cannon vs. 108 vultures.
    int expansionTarget = cannonTargetFor(Kind::Expansion, mechThreat);
    if (expansionTarget > 0)
    {
        for (auto base : Map::getMyBases())
        {
            if (!base || base == mainBase || base == natural) continue;
            if (base->owner != BWAPI::Broodwar->self()) continue;
            requestCannonsAtBase(base, expansionTarget, "expansion", mechThreat);
        }
    }
}
