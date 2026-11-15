#include "WorkerDefenseSquad.h"

#include "Workers.h"
#include "Geo.h"
#include "BuildingPlacement.h"
#include "Units.h"
#include "Map.h"
#include "BnetConfig.h"
#include "BnetLog.h"

#include "DebugFlag_UnitOrders.h"

#include <algorithm>
#include <cmath>

std::vector<std::pair<MyWorker, Unit>> WorkerDefenseSquad::selectTargets(std::set<Unit> &enemyUnits)
{
    std::vector<std::pair<MyWorker, Unit>> result;

    bool hasCannonInMineralLine = false;
    auto &defenseLocations = BuildingPlacement::baseStaticDefenseLocations(base);
    if (defenseLocations.isValid())
    {
        auto cannon = Units::myBuildingAt(*defenseLocations.workerDefenseCannons.begin());
        if (cannon && cannon->completed && cannon->bwapiUnit->isPowered())
        {
            hasCannonInMineralLine = true;
        }
    }

    bool vultureFlee = vultureFleeActive(hasCannonInMineralLine);
    int fleeMargin = BnetConfig::get().workerFleeMarginPx;
    fleeHold.clear();

    auto selectTarget = [&enemyUnits, &hasCannonInMineralLine, vultureFlee, fleeMargin](MyWorker &worker)
    {
        Unit closestEnemy = nullptr;
        int closestEnemyDist = INT_MAX;
        for (auto &enemy : enemyUnits)
        {
            if (!worker->canAttack(enemy)) continue;
            int buffer = hasCannonInMineralLine ? 0 : 48;
            if (vultureFlee && enemy->type == BWAPI::UnitTypes::Terran_Vulture) buffer = std::max(buffer, fleeMargin);
            if (!worker->isInEnemyWeaponRange(enemy, buffer)) continue;

            int dist = worker->getDistance(enemy);
            if (dist < closestEnemyDist)
            {
                closestEnemy = enemy;
                closestEnemyDist = dist;
            }
        }

        return closestEnemy;
    };

    // For the list of units we've already reserved, remove the dead ones and release units that no longer have a target
    for (auto it = units.begin(); it != units.end();)
    {
        auto &worker = *it;

        // Dead units
        if (!worker->exists())
        {
            fleeUntil.erase(worker->id);
            it = units.erase(it);
            continue;
        }

        auto target = selectTarget(worker);

        // Fleeing workers keep running for worker_flee_hold_frames after the last threat
        if (!target)
        {
            auto fleeIt = fleeUntil.find(worker->id);
            if (fleeIt != fleeUntil.end() && fleeIt->second > currentFrame)
            {
                fleeHold.push_back(worker);
                it++;
                continue;
            }
            if (fleeIt != fleeUntil.end()) fleeUntil.erase(fleeIt);
        }

        // Units not being threatened
        if (!target)
        {
            CherryVis::log(worker->id) << "Releasing from non-mining duties (not threatened)";
            Workers::releaseWorker(worker);
            it = units.erase(it);
            continue;
        }

        result.emplace_back(worker, target);
        it++;
    }

    // For units not yet reserved, reserve those that are being threatened
    auto baseWorkers = Workers::getBaseWorkers(base);
    for (auto &worker : baseWorkers)
    {
        auto target = selectTarget(worker);
        if (target)
        {
            Workers::reserveWorker(worker);
            units.push_back(worker);
            result.emplace_back(worker, target);
        }
    }

    return result;
}

void WorkerDefenseSquad::execute(std::vector<std::pair<MyWorker, Unit>> &workersAndTargets,
                                 std::vector<std::pair<MyUnit, Unit>> &combatUnitsAndTargets)
{
    auto healthLimit = 10;
    if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Terran)
    {
        healthLimit = 12;
    }
    else if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Protoss)
    {
        healthLimit = 16;
    }

    // Execute the micro for each worker with a target
    // Rules are:
    // - Get a mineral patch we can use for fleeing or kiting
    // - If there is no suitable patch, attack
    // - If we are on cooldown, mine from the patch (currently disabled)
    // - If we are low on health, mine from the patch
    // - If a friendly combat unit is moving to attack the target but is not yet in position, mine from the patch
    // - Otherwise attack
    for (auto &workerAndTarget : workersAndTargets)
    {
        auto &worker = workerAndTarget.first;
        auto &target = workerAndTarget.second;

        if (!worker->exists()) continue;

        // worker_flee_vultures: run instead of fighting a vulture we cannot kill without a cannon
        if (target->type == BWAPI::UnitTypes::Terran_Vulture && vultureFleeActive(false))
        {
            bool hasCannon = false;
            auto &defenseLocations = BuildingPlacement::baseStaticDefenseLocations(base);
            if (defenseLocations.isValid())
            {
                auto cannon = Units::myBuildingAt(*defenseLocations.workerDefenseCannons.begin());
                hasCannon = cannon && cannon->completed && cannon->bwapiUnit->isPowered();
            }
            if (!hasCannon)
            {
                flee(worker, target);
                continue;
            }
        }

        // Select a mineral patch to use
        // We pick the patch that is furthest away, but closer to us than the enemy
        BWAPI::Unit furthestPatch = nullptr;
        int furthestPatchDist = 0;
        for (const auto &mineralPatch : base->mineralPatches())
        {
            auto patch = mineralPatch->getBwapiUnitIfVisible();
            if (!patch) continue;

            int dist = Geo::EdgeToEdgeDistance(worker->type, worker->lastPosition, patch->getType(), patch->getPosition());
            if (dist < 10) continue;
            if (dist < furthestPatchDist) continue;

            int targetDistToPatch = Geo::EdgeToEdgeDistance(target->type, target->simPosition, patch->getType(), patch->getPosition());
            if (targetDistToPatch < dist) continue;

            furthestPatch = patch;
            furthestPatchDist = dist;
        }
        if (!furthestPatch)
        {
#if DEBUG_UNIT_ORDERS
            CherryVis::log(worker->id) << "No flee patch; attacking: " << target->type << " @ "
                                       << BWAPI::WalkPosition(target->simPosition);
#endif
            worker->attack(target->bwapiUnit, true);
            continue;
        }

        // Flee if we just attacked
        if (worker->lastSeenAttacking >= (currentFrame - 1))
        {
#if DEBUG_UNIT_ORDERS
            CherryVis::log(worker->id) << "Just attacked, moving to patch: " << BWAPI::WalkPosition(furthestPatch->getPosition());
#endif
            worker->gather(furthestPatch);
            continue;
        }

        // Flee if we are low on health
        if ((worker->health + worker->shields) <= healthLimit)
        {
#if DEBUG_UNIT_ORDERS
            CherryVis::log(worker->id) << "Low on health, moving to patch: " << BWAPI::WalkPosition(furthestPatch->getPosition());
#endif
            worker->gather(furthestPatch);
            continue;
        }

        // Check if there is a combat unit attacking our target
        // If there is, and it isn't close to being in range yet, flee to give it time to get here
        // Don't do this if we are in the target's attack range
        if (!worker->isInEnemyWeaponRange(target))
        {
            bool combatUnitApproaching = false;
            for (auto &combatUnitAndTarget : combatUnitsAndTargets)
            {
                if (combatUnitAndTarget.second != target) continue;

                if (combatUnitAndTarget.first->isInOurWeaponRange(combatUnitAndTarget.second, 16))
                {
                    // There is a unit in range, so clear and break
                    combatUnitApproaching = false;
                    break;
                }

                combatUnitApproaching = true;
            }
            if (combatUnitApproaching)
            {
#if DEBUG_UNIT_ORDERS
                CherryVis::log(worker->id) << "Wait for combat unit, moving to patch: " << BWAPI::WalkPosition(furthestPatch->getPosition());
#endif
                worker->gather(furthestPatch);
                continue;
            }
        }

        // When on cooldown, kite if we are too close to our target
        if ((worker->cooldownUntil - currentFrame) > (BWAPI::Broodwar->getRemainingLatencyFrames() + 6) &&
            worker->getDistance(target) < (worker->groundRange() - 2))
        {
#if DEBUG_UNIT_ORDERS
            CherryVis::log(worker->id) << "Kiting from target @ " << BWAPI::WalkPosition(target->simPosition)
                                       << "; d=" << worker->getDistance(target)
                                       << "; moving to patch: " << BWAPI::WalkPosition(furthestPatch->getPosition());
#endif
            worker->gather(furthestPatch);
            continue;
        }

#if DEBUG_UNIT_ORDERS
        CherryVis::log(worker->id) << "Attacking: " << target->type << " @ "
                                   << BWAPI::WalkPosition(target->simPosition);
#endif
        worker->attack(target->bwapiUnit, true);
        //worker->attackUnit(target, workersAndTargets, false);
    }

    // Workers still in their flee hold time keep moving to the safe base; without one they keep their last order
    for (auto &worker : fleeHold)
    {
        if (!worker->exists()) continue;
        for (auto other : {Map::getMyMain(), Map::getMyNatural()})
        {
            if (!other || other == base || other->owner != BWAPI::Broodwar->self()) continue;
            if (!other->resourceDepot || !other->resourceDepot->completed || !Units::enemyAtBase(other).empty()) continue;
            worker->moveTo(other->getPosition());
            break;
        }
    }
}

bool WorkerDefenseSquad::vultureFleeActive(bool hasCannonInMineralLine) const
{
    if (hasCannonInMineralLine) return false;
    auto &bnetConfig = BnetConfig::get();
    if (!bnetConfig.plutoPvtSnipe || !bnetConfig.workerFleeVultures) return false;
    return BWAPI::Broodwar->enemy() && BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Terran;
}

namespace
{
    // Clamp a pixel position to the map and, if needed, search nearby walkable walk-tiles.
    // Corner mains (e.g. KnockOut (7,7)) used to produce off-map flee targets that became
    // Positions::Invalid / (4095,4095) after pathing — counted as invalid_moves while probes died.
    BWAPI::Position clampFleePosition(BWAPI::Position pos)
    {
        auto game = BWAPI::BroodwarPtr;
        if (!game) return BWAPI::Positions::Invalid;

        const int maxX = std::max(0, game->mapWidth() * 32 - 1);
        const int maxY = std::max(0, game->mapHeight() * 32 - 1);
        pos.x = std::clamp(pos.x, 0, maxX);
        pos.y = std::clamp(pos.y, 0, maxY);

        BWAPI::WalkPosition wp(pos);
        auto walkableAt = [&](BWAPI::WalkPosition w) -> BWAPI::Position
        {
            if (!w.isValid() || !game->isWalkable(w)) return BWAPI::Positions::Invalid;
            return BWAPI::Position(w) + BWAPI::Position(4, 4);
        };

        if (auto p = walkableAt(wp); p.isValid()) return p;

        for (int r = 1; r <= 24; r++)
        {
            for (int dx = -r; dx <= r; dx++)
            {
                for (int dy = -r; dy <= r; dy++)
                {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    if (auto p = walkableAt(BWAPI::WalkPosition(wp.x + dx, wp.y + dy)); p.isValid()) return p;
                }
            }
        }
        return BWAPI::Positions::Invalid;
    }
}

BWAPI::Position WorkerDefenseSquad::fleeDestination(const MyWorker &worker, const Unit &threat) const
{
    // Prefer the other early base (main <-> natural) if it has a completed nexus and no enemy units
    for (auto other : {Map::getMyMain(), Map::getMyNatural()})
    {
        if (!other || other == base) continue;
        if (other->owner != BWAPI::Broodwar->self()) continue;
        if (!other->resourceDepot || !other->resourceDepot->completed) continue;
        if (!Units::enemyAtBase(other).empty()) continue;
        auto dest = clampFleePosition(other->getPosition());
        if (dest.isValid()) return dest;
    }

    // Otherwise run directly away from the vulture, quantized to a 4-tile grid so the target does not change every frame
    auto delta = worker->lastPosition - threat->lastPosition;
    double length = std::sqrt(double(delta.x) * delta.x + double(delta.y) * delta.y);
    if (length < 1.0) delta = worker->lastPosition - base->getPosition(), length = std::sqrt(double(delta.x) * delta.x + double(delta.y) * delta.y);
    if (length >= 1.0)
    {
        BWAPI::Position away(worker->lastPosition.x + (int)(delta.x * 256.0 / length),
                             worker->lastPosition.y + (int)(delta.y * 256.0 / length));
        away = BWAPI::Position(((away.x / 128) * 128) + 64, ((away.y / 128) * 128) + 64);
        auto dest = clampFleePosition(away);
        if (dest.isValid()) return dest;
    }

    // Corner / blocked flee: pull toward the mineral line center (still on the map) before giving up
    if (base && base->mineralLineCenter.isValid())
    {
        auto dest = clampFleePosition(base->mineralLineCenter);
        if (dest.isValid()) return dest;
    }

    return BWAPI::Positions::Invalid;
}

void WorkerDefenseSquad::flee(const MyWorker &worker, const Unit &threat)
{
    int hold = BnetConfig::get().workerFleeHoldFrames;
    auto it = fleeUntil.find(worker->id);
    if (it == fleeUntil.end() || it->second < currentFrame) BnetLog::workerFlee();
    fleeUntil[worker->id] = currentFrame + std::max(1, hold);

    auto dest = threat ? fleeDestination(worker, threat) : BWAPI::Positions::Invalid;
    if (!dest.isValid())
    {
        // Mineral walk to the patch furthest from the threat (workers pass through each other while gathering)
        BWAPI::Unit furthestPatch = nullptr;
        int furthestDist = -1;
        for (const auto &mineralPatch : base->mineralPatches())
        {
            auto patch = mineralPatch->getBwapiUnitIfVisible();
            if (!patch) continue;
            int dist = threat ? threat->lastPosition.getApproxDistance(patch->getPosition()) : 0;
            if (dist > furthestDist)
            {
                furthestPatch = patch;
                furthestDist = dist;
            }
        }
        if (furthestPatch)
        {
            worker->gather(furthestPatch);
            return;
        }
        dest = base ? clampFleePosition(base->getPosition()) : BWAPI::Positions::Invalid;
        if (!dest.isValid()) return;
    }

#if DEBUG_UNIT_ORDERS
    CherryVis::log(worker->id) << "Fleeing vulture to " << BWAPI::WalkPosition(dest);
#endif
    if (!dest.isValid()) return;
    worker->moveTo(dest);
}

void WorkerDefenseSquad::disband()
{
    for (const auto &unit : units)
    {
        CherryVis::log(unit->id) << "Releasing from non-mining duties (disband worker defense)";
        Workers::releaseWorker(unit);
    }

    units.clear();
    fleeUntil.clear();
    fleeHold.clear();
}
