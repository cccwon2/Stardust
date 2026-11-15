#include "DefendBaseSquad.h"

#include <BWAPI.h>

void DefendBaseSquad::execute()
{
    for (auto it = enemyUnits.begin(); it != enemyUnits.end();)
    {
        if ((*it)->exists())
        {
            it++;
        }
        else
        {
            it = enemyUnits.erase(it);
        }
    }

    // When a vulture is in the base, aim the squad at the mineral line so home-guard dragoons walk onto
    // the harassment instead of idling at the nexus / choke (KnockOut 1.4 observation).
    bool vultureInBase = false;
    for (const auto &enemy : enemyUnits)
    {
        if (enemy && enemy->type == BWAPI::UnitTypes::Terran_Vulture)
        {
            vultureInBase = true;
            break;
        }
    }
    if (vultureInBase && base && base->mineralLineCenter.isValid())
    {
        targetPosition = base->mineralLineCenter;
    }
    else if (base)
    {
        targetPosition = base->getPosition();
    }

    Squad::execute();

    if (clusters.empty())
    {
        auto workersAndTargets = workerDefenseSquad->selectTargets(enemyUnits);
        std::vector<std::pair<MyUnit, Unit>> emptyUnitsAndTargets;
        workerDefenseSquad->execute(workersAndTargets, emptyUnitsAndTargets);
    }
}

void DefendBaseSquad::execute(UnitCluster &cluster)
{
    if (enemyUnits.empty())
    {
        cluster.setActivity(UnitCluster::Activity::Moving);
        cluster.move(targetPosition);
        return;
    }

    updateDetectionNeeds(enemyUnits);

    // Select targets
    auto unitsAndTargets = cluster.selectTargets(enemyUnits, targetPosition);

    // Run combat sim
    // auto simResult = cluster.runCombatSim(unitsAndTargets, enemyUnits, detectors, false);

    // TODO: Interpret result

    cluster.setActivity(UnitCluster::Activity::Attacking);
    cluster.attack(unitsAndTargets, targetPosition);

    if (cluster.isVanguardCluster)
    {
        auto workersAndTargets = workerDefenseSquad->selectTargets(enemyUnits);
        workerDefenseSquad->execute(workersAndTargets, unitsAndTargets);
    }
}
