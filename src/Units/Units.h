#pragma once

#include "Common.h"

#include "Resource.h"
#include "MyUnit.h"
#include "Unit.h"
#include "UpgradeOrTechType.h"
#include "Base.h"

#define USE_BUNKER_ATTACKER_LOGIC true

namespace Units
{
    void initialize();

    void update();

    void issueOrders();

    void onUnitDestroy(BWAPI::Unit unit);

    void onBulletCreate(BWAPI::Bullet bullet);

    Unit get(BWAPI::Unit unit);

    MyUnit mine(BWAPI::Unit unit);

    MyUnit myBuildingAt(BWAPI::TilePosition tile);

    std::unordered_set<MyUnit> &allMine();

    std::unordered_set<MyUnit> &allMineCompletedOfType(BWAPI::UnitType type);

    std::unordered_set<MyUnit> &allMineIncompleteOfType(BWAPI::UnitType type);

    std::map<BWAPI::UnitType, std::unordered_set<MyUnit>> &allMineIncompleteByType();

    std::unordered_set<Unit> &allEnemy();

    std::unordered_set<Unit> &allEnemyOfType(BWAPI::UnitType type);

    void mine(std::set<MyUnit> &units,
              const std::function<bool(const MyUnit &)> &predicate = nullptr);

    void enemy(std::set<Unit> &units,
               const std::function<bool(const Unit &)> &predicate = nullptr);

    void enemyInRadius(std::set<Unit> &units,
                       BWAPI::Position position,
                       int radius,
                       const std::function<bool(const Unit &)> &predicate = nullptr);

    void enemyInArea(std::set<Unit> &units,
                     const BWEM::Area *area,
                     const std::function<bool(const Unit &)> &predicate = nullptr);

    std::unordered_set<Unit> &enemyAtBase(Base *base);

    std::unordered_set<Unit> &enemyCombatUnitsNotAtAnEnemyBase();

    int countAll(BWAPI::UnitType type);

    int countCompleted(BWAPI::UnitType type);

    int countIncomplete(BWAPI::UnitType type);

    std::map<BWAPI::UnitType, int> countIncompleteByType();

    int countEnemy(BWAPI::UnitType type);

    std::vector<std::pair<int, int>> &getEnemyUnitTimings(BWAPI::UnitType type);

    bool hasEnemyBuilt(BWAPI::UnitType type);

    // First (not destroyed) resource registered on the build tile
    Resource resourceAt(BWAPI::TilePosition tile);

    // All (not destroyed) resources on the build tile; more than one mineral field only on stacked-mineral maps with
    // stacked_mineral_identity_fix
    std::vector<Resource> resourcesAt(BWAPI::TilePosition tile);

    // Resource for a mineral field / geyser / refinery unit (minerals matched by unit ID with the identity fix)
    Resource resourceFor(BWAPI::Unit bwapiUnit);

    // Whether stacked_mineral_identity_fix was active when this game's resources were registered
    bool stackedMineralIdentityFix();

    struct ResourceRegistryStats
    {
        bool identityFix = true;
        int mineralUnits = 0;        // Mineral field units at game start
        int registeredMinerals = 0;  // Mineral resources registered at game start
        int droppedMinerals = 0;     // Fields dropped because their tile was already taken (upstream behavior)
        int stackedTiles = 0;        // Build tiles with more than one mineral field
        int maxStack = 0;            // Most mineral fields on one tile
        int lateRegistered = 0;      // Resources registered after game start (not found when first seen)
    };
    const ResourceRegistryStats &resourceRegistryStats();

    std::vector<Resource> myCompletedRefineries();

    // TODO: This doesn't really fit here
    bool isBeingUpgradedOrResearched(UpgradeOrTechType type);

#if LOGGING_ENABLED
    void setLogUnitsCreatedAndLost(bool newValue);
#endif
}
