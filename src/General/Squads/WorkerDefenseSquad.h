#pragma once

#include "Squad.h"
#include "MyUnit.h"
#include "MyWorker.h"
#include "Base.h"

class WorkerDefenseSquad
{
public:
    explicit WorkerDefenseSquad(Base *base) : base(base) {}

    // Selects a target for each worker in the base.
    std::vector<std::pair<MyWorker, Unit>> selectTargets(std::set<Unit> &enemyUnits);

    void execute(std::vector<std::pair<MyWorker, Unit>> &workersAndTargets, std::vector<std::pair<MyUnit, Unit>> &combatUnitsAndTargets);

    void disband();

private:
    Base *base;
    std::vector<MyWorker> units; // All of the reserved workers

    // worker_flee_vultures: workers threatened by a vulture with no cannon in the mineral line run away instead of
    // attacking. Keyed by unit id: frame until which the worker keeps fleeing after the last threat.
    std::map<int, int> fleeUntil;
    std::vector<MyWorker> fleeHold;  // Reserved workers still fleeing without a current threat (hold frames)

    [[nodiscard]] bool vultureFleeActive(bool hasCannonInMineralLine) const;
    BWAPI::Position fleeDestination(const MyWorker &worker, const Unit &threat) const;
    void flee(const MyWorker &worker, const Unit &threat);
};
