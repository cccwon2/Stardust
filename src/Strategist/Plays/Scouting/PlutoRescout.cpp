#include "PlutoRescout.h"

#include "Workers.h"
#include "Map.h"
#include "Units.h"
#include "MyWorker.h"
#include "BnetLog.h"

#include <sstream>

namespace
{
    const int arrivalDistance = 6 * 32;     // Close enough to the target base position to have seen the base
    const int abortHitPoints = 20;          // Shields + hit points at which the scout gives up
}

void PlutoRescout::update()
{
    if (stage == Stage::Done) return;

    if (startFrame < 0) startFrame = currentFrame;

    if (scout && !scout->exists())
    {
        scout = nullptr;
        finish("scout_died");
        return;
    }

    auto enemyMain = Map::getEnemyStartingMain();
    if (!enemyMain)
    {
        finish("enemy_main_unknown");
        return;
    }
    auto enemyNatural = Map::getEnemyStartingNatural();

    if (currentFrame - startFrame > maxFrames)
    {
        finish("timeout");
        return;
    }

    if (!scout)
    {
        auto worker = Workers::getClosestReassignableWorker(enemyMain->getPosition(), false);
        if (!worker)
        {
            // Try again next frame, the timeout ends the play if no worker is ever available
            return;
        }
        Workers::reserveWorker(worker);
        scout = worker;
        BnetLog::event("rescout_start");
    }

    if ((scout->health + scout->shields) <= abortHitPoints)
    {
        finish("damaged");
        return;
    }

    if (stage == Stage::Main)
    {
        if (scout->getDistance(enemyMain->getPosition()) <= arrivalDistance)
        {
            stage = Stage::Natural;
        }
        else
        {
            scout->moveTo(enemyMain->getPosition());
            return;
        }
    }

    if (stage == Stage::Natural)
    {
        if (!enemyNatural || scout->getDistance(enemyNatural->getPosition()) <= arrivalDistance)
        {
            finish("done");
            return;
        }
        scout->moveTo(enemyNatural->getPosition());
    }
}

void PlutoRescout::finish(const char *reason)
{
    if (stage == Stage::Done) return;
    stage = Stage::Done;

    if (scout && scout->exists())
    {
        Workers::releaseWorker(std::static_pointer_cast<MyWorkerImpl>(scout));
    }
    scout = nullptr;

    std::ostringstream ev;
    ev << "rescout_end reason=" << reason
       << " factories=" << Units::countEnemy(BWAPI::UnitTypes::Terran_Factory)
       << " barracks=" << Units::countEnemy(BWAPI::UnitTypes::Terran_Barracks)
       << " starport=" << Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Starport)
       << " control_tower=" << Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Control_Tower)
       << " machine_shop=" << Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Machine_Shop)
       << " academy=" << Units::hasEnemyBuilt(BWAPI::UnitTypes::Terran_Academy)
       << " cc=" << Units::countEnemy(BWAPI::UnitTypes::Terran_Command_Center);
    BnetLog::event(ev.str());

    status.complete = true;
}

void PlutoRescout::disband(const std::function<void(const MyUnit)> &,
                           const std::function<void(const MyUnit)> &)
{
    if (scout && scout->exists())
    {
        Workers::releaseWorker(std::static_pointer_cast<MyWorkerImpl>(scout));
    }
    scout = nullptr;
}

void PlutoRescout::removeUnit(const MyUnit &unit)
{
    if (scout == unit) scout = nullptr;
}
