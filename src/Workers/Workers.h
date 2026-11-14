#pragma once

#include "Common.h"
#include "Base.h"
#include "MyWorker.h"

namespace Workers
{
    void initialize();

    void onUnitDestroy(const Unit &unit);

    void onMineralPatchDestroyed(const Resource &mineralPatch);

    void updateAssignments();

    void issueOrders();

    // Battle.net stuck worker recovery counters for this game
    struct BnetRecoveryStats
    {
        int attempts = 0;        // Recovery attempts (worker detected as stuck and off cooldown)
        int reassigned = 0;      // Issued recoveries that moved the worker to a different patch
        int commandOk = 0;       // Recovery commands actually issued (reassignment or plain re-gather)
        int commandFailed = 0;   // Recovery commands rejected / no visible target (assignment kept)
        int budgetDeferred = 0;  // Attempts deferred because max_orders_per_frame was exhausted
    };

    // Number of stuck gathering workers successfully reassigned to another patch this game
    // (before this change it counted every reassignment attempt)
    int bnetStuckReassignments();

    const BnetRecoveryStats &bnetRecoveryStats();

    // Whether the given worker unit can currently be reassigned to non-gathering duties
    bool isAvailableForReassignment(const MyWorker &unit, bool allowCarryMinerals, bool allowMining);

    MyWorker getClosestReassignableWorker(BWAPI::Position position, bool allowCarryMinerals, int *bestTravelTime = nullptr);

    size_t getBaseWorkerCount(Base *base);

    std::vector<MyWorker> getBaseWorkers(Base *base);

    int baseMineralWorkerCount(Base *base);

    void reserveBaseWorkers(std::vector<MyWorker> &workers, Base *base);

    void reserveWorker(const MyWorker &unit);

    void releaseWorker(const MyWorker &unit);

    // How many mineral patches are currently available for assignment
    int availableMineralAssignments(Base *base = nullptr, int workersPerPatch = 2);

    // How many gas slots are currently available for assignment
    int availableGasAssignments(Base *base = nullptr);

    void setDesiredGasWorkerDelta(int gasWorkerDelta);

    int mineralWorkers();

    std::pair<int, int> gasWorkers();

    int reassignableMineralWorkers();

    int reassignableGasWorkers();

    int idleWorkerCount();

    std::map<Resource, std::set<MyWorker>> &mineralsAndAssignedWorkers();

    // Gets all workers assigned to the resource. Does not check if any have died since their status was last updated.
    std::set<MyWorker> getWorkersAssignedTo(const Resource &resource);

    // Given a patch and a worker, returns the other worker assigned to it, if it exists.
    MyWorker getOtherWorkerMining(const Resource &resource, const MyWorker &worker);

    void setWorkerMineralPatch(const MyWorker &worker, const Resource &resource, Base *base);
}
