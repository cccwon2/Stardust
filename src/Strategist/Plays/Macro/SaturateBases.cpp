#include "SaturateBases.h"

#include "Units.h"
#include "Workers.h"
#include "Map.h"
#include "PathFinding.h"
#include "Strategist.h"
#include "BnetConfig.h"
#include "BnetLog.h"

namespace
{
    // probe_throttle_under_vultures (PvT): while a vulture is at one of our bases, replacement probes would just be killed
    // again and block army / cannon / observer production (2026-11-15 KnockOut: only probes were produced for 2.5 minutes
    // while vultures sat in the main), so probes drop below army production. Never below 8 probes.
    bool throttleProbes()
    {
        auto &bnetConfig = BnetConfig::get();
        if (!bnetConfig.plutoPvtSnipe || !bnetConfig.probeThrottleUnderVultures) return false;
        if (!BWAPI::Broodwar->enemy() || BWAPI::Broodwar->enemy()->getRace() != BWAPI::Races::Terran) return false;
        if (Units::countAll(BWAPI::UnitTypes::Protoss_Probe) < 8) return false;

        for (auto &base : Map::getMyBases(BWAPI::Broodwar->self()))
        {
            for (const auto &unit : Units::enemyAtBase(base))
            {
                if (unit->type == BWAPI::UnitTypes::Terran_Vulture) return true;
            }
        }
        return false;
    }

    bool throttleLogged = false;
    int lastThrottleFrame = -1000;
}

void SaturateBases::addPrioritizedProductionGoals(std::map<int, std::vector<ProductionGoal>> &prioritizedProductionGoals)
{
    // Hard cap, defaults to 75 workers
    if (Units::countAll(BWAPI::UnitTypes::Protoss_Probe) >= workerLimit) return;

    // On the first scan, gather all owned bases and how many workers they need
    std::vector<Base *> fullBases;
    std::vector<std::pair<std::vector<Base *>, int>> baseClusters;
    for (auto &base : Map::getMyBases(BWAPI::Broodwar->self()))
    {
        int desiredWorkers = Workers::availableMineralAssignments(base, workersPerPatch) + Workers::availableGasAssignments(base);

        // Reduce by one if this base is already building a worker
        // It's OK if this base goes to a negative number of workers
        if (base->resourceDepot && base->resourceDepot->isTraining()) desiredWorkers--;

        // TODO: Reduce by workers in the area assigned to other duties

        if (desiredWorkers > 0)
        {
            baseClusters.emplace_back(std::vector<Base *>{base}, desiredWorkers);
        }
        else
        {
            fullBases.push_back(base);
        }
    }

    // Now assign full bases to base clusters if they can safely transfer workers
    for (auto &base : fullBases)
    {
        std::vector<Base *> *bestCluster = nullptr;
        int bestFrames = INT_MAX;
        for (auto &cluster : baseClusters)
        {
            int frames = PathFinding::ExpectedTravelTime(base->getPosition(),
                                                         (*cluster.first.begin())->getPosition(),
                                                         BWAPI::UnitTypes::Protoss_Probe,
                                                         PathFinding::PathFindingOptions::Default,
                                                         -1);
            if (frames == -1) continue;
            if (frames < bestFrames)
            {
                bestFrames = frames;
                bestCluster = &cluster.first;
            }
        }

        if (bestCluster && (bestFrames <= 400 || Strategist::isEnemyContained()))
        {
            bestCluster->push_back(base);
        }
    }

    int workerPriority = PRIORITY_WORKERS;
    if (throttleProbes())
    {
        workerPriority = PRIORITY_NORMAL;
        if (!throttleLogged || currentFrame - lastThrottleFrame > 24 * 60)
        {
            BnetLog::event("probe_throttle vultures_at_base=1");
            throttleLogged = true;
        }
        lastThrottleFrame = currentFrame;
    }

    // Now order the production
    for (auto &clusterAndRequiredWorkers : baseClusters)
    {
        // Balance the production amongst the bases
        size_t desiredProductionPerBase = clusterAndRequiredWorkers.second / clusterAndRequiredWorkers.first.size();
        size_t remainder = clusterAndRequiredWorkers.second % clusterAndRequiredWorkers.first.size();
        for (auto &base : clusterAndRequiredWorkers.first)
        {
            size_t count = desiredProductionPerBase;
            if (remainder > 0)
            {
                count++;
                remainder--;
            }

            if (count < 1) continue;
            if (!base->resourceDepot) continue;
            if (!base->resourceDepot->completed) continue;

            prioritizedProductionGoals[workerPriority].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                      label,
                                                                      BWAPI::UnitTypes::Protoss_Probe,
                                                                      count,
                                                                      1,
                                                                      base);
        }
    }

    // Finally make sure we always produce probes at low priority on one base
    // This will allow us to ramp up our natural faster
    if (Map::getMyBases().size() == 1 && Map::getMyMain() && Map::getMyNatural())
    {
        int desiredProbes = (Map::getMyMain()->mineralPatchCount() + Map::getMyNatural()->mineralPatchCount()) * 2 +
            (Map::getMyMain()->geysersOrRefineries().size() + Map::getMyNatural()->geysersOrRefineries().size()) * 3 -
                Units::countAll(BWAPI::UnitTypes::Protoss_Probe);
        if (desiredProbes > 0)
        {
            prioritizedProductionGoals[PRIORITY_LOWEST].emplace_back(std::in_place_type<UnitProductionGoal>,
                                                                      label,
                                                                      BWAPI::UnitTypes::Protoss_Probe,
                                                                      desiredProbes,
                                                                      1,
                                                                      Map::getMyMain());
        }
    }
}
