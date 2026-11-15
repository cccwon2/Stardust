#include "MyUnit.h"
#include "BnetLog.h"
#include "BnetConfig.h"
#include "BnetPolicy.h"

#include "UnitUtil.h"
#include "Units.h"
#include "Geo.h"
#include "Players.h"

#include "DebugFlag_UnitOrders.h"

#define DEBUG_ORDER_PROCESS_INDEX false

namespace
{
    // BWAPI's isVisible is really intended for knowing if an enemy unit is in the fog or not
    // We want to also know if a unit is loaded or harvesting gas, as these remove the unit from the game's visible units list
    bool isVisible(BWAPI::Unit unit)
    {
        if (!unit->getType().isBuilding() && !unit->isCompleted()) return false;
        if (!unit->isVisible()) return false;
        if (unit->isLoaded()) return false;
        if (unit->getOrder() == BWAPI::Orders::HarvestGas && unit->getOrderTimer() > 0) return false;
        return true;
    }
}

MyUnitImpl::MyUnitImpl(BWAPI::Unit unit)
        : UnitImpl(unit)
        , orderProcessIndex(isVisible(unit) ? (10 + currentFrame) : -1)
        , producer(nullptr)
        , energy(unit->getEnergy())
        , lastCastFrame(-1)
        , completionFrame(unit->isCompleted() ? currentFrame : -1)
        , issuedOrderThisFrame(false)
        , moveCommand(nullptr)
        , targetPosition(BWAPI::Positions::Invalid)
        , currentlyMovingTowards(BWAPI::Positions::Invalid)
        , grid(nullptr)
        , gridNode(nullptr)
        , currentlyNavigatingUsingGrid(false)
        , lastMoveFrame(0)
        , unstickUntil(-1)
        , simulatedPositionsUpdated(false)
        , visible(isVisible(unit))
{
    recentCommands.resize(BnetConfig::microLatencyFrames() + 1, unit->getLastCommand());
    simulatedPositions.resize(BnetConfig::microLatencyFrames() + 1, unit->getPosition());
    simulatedHeading.resize(BnetConfig::microLatencyFrames() + 1, 0);
#if DEBUG_ORDER_PROCESS_INDEX
    CherryVis::log(id) << "Order process index: " << orderProcessIndex;
#endif
}

std::ostream &operator<<(std::ostream &os, const MyUnitImpl &unit)
{
    os << unit.type << ":" << unit.id << "@" << BWAPI::WalkPosition(unit.getTilePosition());
    return os;
}

void MyUnitImpl::update(BWAPI::Unit unit)
{
    if (!unit || !unit->exists()) return;

    // Update the order process index if the unit's visibility has changed
    if (isVisible(unit) != visible)
    {
        if (visible)
        {
            visible = false;
            orderProcessIndex = -1;
#if DEBUG_ORDER_PROCESS_INDEX
            CherryVis::log(id) << "Order process index reset as unit has become invisible";
#endif
        }
        else
        {
            visible = true;
            orderProcessIndex = 10 + currentFrame;
            orderProcessTimer = 0;
#if DEBUG_ORDER_PROCESS_INDEX
            CherryVis::log(id) << "Order process index: " << orderProcessIndex;
#endif
        }
    }

    // Update command positions
    // Battle.net: keep the in-flight command window at the effective micro latency (+1); it can grow during the game
    {
        size_t wanted = (size_t)BnetConfig::microLatencyFrames() + 1;
        while (recentCommands.size() < wanted) recentCommands.push_front(recentCommands.empty() ? unit->getLastCommand() : recentCommands.front());
        while (recentCommands.size() > wanted) recentCommands.pop_front();
        if (simulatedPositions.size() != wanted)
        {
            simulatedPositions.resize(wanted, unit->getPosition());
            simulatedHeading.resize(wanted, 0);
        }
    }
    recentCommands.pop_front();
    recentCommands.emplace_back(unit->getLastCommand());
    if (recentCommands.back().target)
    {
        recentCommands.back().assignTarget(recentCommands.back().target->getPosition());
    }
    simulatedPositionsUpdated = false;

    if (bwapiUnit->isCompleted() && completionFrame == -1)
    {
        producer = nullptr;
        completionFrame = currentFrame;
    }

    if (energy - unit->getEnergy() > 45)
    {
        lastCastFrame = currentFrame;
        Log::Get() << "Unit cast spell: " << *this;
    }
    energy = unit->getEnergy();

    // If this unit has just gone on cooldown, add an upcoming attack on its target
    if (bwapiUnit->getLastCommand().getType() == BWAPI::UnitCommandTypes::Attack_Unit ||
        bwapiUnit->getOrder() == BWAPI::Orders::AttackUnit)
    {
        auto cooldown = std::max(unit->getGroundWeaponCooldown(), unit->getAirWeaponCooldown());
        if (cooldown > 0 && cooldown > (cooldownUntil - currentFrame + 1))
        {
            auto target = Units::get(
                    (bwapiUnit->getLastCommand().getType() == BWAPI::UnitCommandTypes::Attack_Unit)
                    ? bwapiUnit->getLastCommand().getTarget()
                    : bwapiUnit->getOrderTarget());
            if (target)
            {
                target->addUpcomingAttack(Units::get(bwapiUnit));
            }
        }
    }

    UnitImpl::update(unit);

    // Battle.net log: count the order issued to this unit on the previous frame (BWAPI's getAPM is 0 on Remastered)
    if (issuedOrderThisFrame)
    {
        BnetLog::countIssuedOrder(type.isWorker()
                                  ? BnetLog::OrderCategory::Worker
                                  : (type == BWAPI::UnitTypes::Protoss_Observer ? BnetLog::OrderCategory::Observer
                                                                                 : BnetLog::OrderCategory::Army));
    }

    issuedOrderThisFrame = false;
    moveCommand = nullptr;

    // Guard against buildings having a deep training queue
    // Disabled in tournament builds as it causes crashes in BWAPI because of https://github.com/bwapi/bwapi/issues/864
#if LOGGING_ENABLED
    if (BWAPI::Broodwar->self()->supplyUsed() < 380 &&
        unit->getLastCommand().getType() != BWAPI::UnitCommandTypes::Cancel_Train &&
        unit->getLastCommand().getType() != BWAPI::UnitCommandTypes::Cancel_Train_Slot &&
        unit->getTrainingQueue().size() > 1 &&
        lastCommandFrame < (currentFrame - BnetConfig::microLatencyFrames()))
    {
        Log::Get() << "WARNING: Training queue for " << unit->getType() << " @ " << unit->getTilePosition()
                   << " is too deep! Cancelling later units.";
        std::ostringstream units;
        for (int i = 0; i < unit->getTrainingQueue().size(); i++)
        {
            if (i > 0) units << ",";
            units << unit->getTrainingQueue()[i];
        }
        Log::Get() << "Queue: " << units.str();
        unit->cancelTrain(1);
    }
#endif

    // Cancel dying incomplete buildings
    if (type.isBuilding() &&
        !completed &&
        bwapiUnit->isUnderAttack() &&
        bwapiUnit->canCancelConstruction() &&
        (lastShields + lastHealth) < 20)
    {
        Log::Get() << "Cancelling dying " << type << " @ " << getTilePosition();
        BnetLog::event("build_cancel_dying type=" + type.getName() + " tile=" + std::to_string(getTilePosition().x) + ","
                       + std::to_string(getTilePosition().y));
        bwapiUnit->cancelConstruction();
    }
}

bool MyUnitImpl::isBeingManufacturedOrCarried() const
{
    if (!exists()) return false;

    // For Protoss, anything incomplete that isn't a building is currently being manufactured
    // Will need to change if we ever support building Zerg units
    if (!bwapiUnit->isCompleted() && !bwapiUnit->getType().isBuilding()) return true;

    // Otherwise look at whether it is loaded
    return bwapiUnit->isLoaded();
}

void MyUnitImpl::attackUnit(const Unit &target,
                            std::vector<std::pair<MyUnit, Unit>> &unitsAndTargets,
                            bool clusterAttacking,
                            int enemyAoeRadius)
{
    // If the enemy is a long way away, move to it
    int dist = getDistance(target);
    if (dist > 320 || !target->bwapiUnit->isVisible())
    {
#if DEBUG_UNIT_ORDERS
        CherryVis::log(id) << "Attack: Moving to target @ " << BWAPI::WalkPosition(target->simPosition);
#endif
        moveTo(target->simPosition);
        return;
    }

    auto myPredictedPosition = predictPosition(BnetConfig::microRemainingLatencyFrames());
    auto targetPredictedPosition = target->predictPosition(BnetConfig::microRemainingLatencyFrames());
    auto predictedDist = Geo::EdgeToEdgeDistance(type, myPredictedPosition, target->type, targetPredictedPosition);
    auto rangeToTarget = range(target);

    // Predicted to be in range or target is stationary: attack
    if (predictedDist <= rangeToTarget || targetPredictedPosition == target->simPosition)
    {
        // Re-send the attack command when we are coming into range
        bool forceAttackCommand = false;
        if (dist > rangeToTarget && predictedDist <= rangeToTarget && cooldownUntil < (currentFrame + BnetConfig::microRemainingLatencyFrames())
            && bwapiUnit->getLastCommand().type == BWAPI::UnitCommandTypes::Attack_Unit)
        {
            // If we recently sent the attack command, check our predicted distance when it will kick in
            // If we still expect to be in range, we don't need to do anything
            int lastCommandTakesEffect = lastCommandFrame + BnetConfig::microLatencyFrames() - currentFrame;
            if (lastCommandTakesEffect >= 0)
            {
                auto distAtAttackFrame =
                        (lastCommandTakesEffect == 0)
                            ? dist
                            : Geo::EdgeToEdgeDistance(type, predictPosition(lastCommandTakesEffect),
                                                      target->type, target->predictPosition(lastCommandTakesEffect));
                if (distAtAttackFrame <= rangeToTarget)
                {
#if DEBUG_UNIT_ORDERS
                    CherryVis::log(id) << "Attack: Attack command effective @ " << lastCommandTakesEffect
                        << "; dist=" << distAtAttackFrame << "; don't touch";
#endif
                    return;
                }

#if DEBUG_UNIT_ORDERS
                CherryVis::log(id) << "Attack: Attack command effective @ " << lastCommandTakesEffect
                                   << "; dist=" << distAtAttackFrame << "; resend attack command";
#endif
            }
            else
            {
#if DEBUG_UNIT_ORDERS
                CherryVis::log(id) << "Attack: Attack command effective @ " << lastCommandTakesEffect
                                   << "; resending attack command";
#endif
            }

            // Otherwise force the attack command
            forceAttackCommand = true;
        }

        attack(target->bwapiUnit, forceAttackCommand);
        return;
    }

    // Plot an intercept course
    auto interceptPosition = intercept(target);
    if (!interceptPosition.isValid()) interceptPosition = target->predictPosition(BnetConfig::microLatencyFrames() + 2);

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Attack: Moving to intercept @ " << BWAPI::WalkPosition(interceptPosition);
#endif
    moveTo(interceptPosition);
}

bool MyUnitImpl::isReady() const
{
    // When we have a large army, only micro units every other frame to avoid having commands dropped
    if (BWAPI::Broodwar->self()->supplyUsed() > 250 &&
        (currentFrame % 2) != (id % 2))
    {
        return false;
    }

    return !immobile;
}

bool MyUnitImpl::unstick()
{
    // Unit is dead
    if (!bwapiUnit) return true;

    // If we recently sent a command meant to unstick the unit, give it a bit of time to kick in
    if (unstickUntil > currentFrame)
    {
#if DEBUG_UNIT_ORDERS
        CherryVis::log(id) << "Unstick pending until " << unstickUntil;
#endif

        return true;
    }

    // If the unit is listed as stuck, send a stop command unless we have done so recently
    if (bwapiUnit->isStuck() && unstickUntil < (currentFrame - 10))
    {
#if DEBUG_UNIT_ORDERS
        CherryVis::log(id) << "Sending stop command to unstick";
#endif

        stop();
        unstickUntil = currentFrame + nextUnstickWait();
        return true;
    }

    return false;
}

int MyUnitImpl::nextUnstickWait()
{
    int base = BnetConfig::microRemainingLatencyFrames();

    // Battle.net only, workers only: a trapped worker used to get a stop + move every ~12 frames for minutes.
    // Army units keep upstream timing, since callers also use unstick() before attacking.
    int maxFrames = BnetConfig::get().unstickBackoffMaxFrames;
    if (!type.isWorker() || maxFrames <= 0 || BnetConfig::commandIntervalFrames() <= 0)
    {
        unstickAttempts = 0;
        return base;
    }

    if (unstickAttempts > 0 && unstickAttemptPosition.isValid() && lastPosition.getApproxDistance(unstickAttemptPosition) <= 32)
    {
        unstickAttempts++;
    }
    else
    {
        unstickAttempts = 1;
        unstickAttemptPosition = lastPosition;
    }

    int wait = BnetPolicy::unstickBackoffFrames(unstickAttempts, base, maxFrames);
    if (wait > base) BnetLog::unstickBackoff();
    return wait;
}

BWAPI::Position MyUnitImpl::simulatePosition(int frames) const
{
    if (!simulatedPositionsUpdated) updateSimulatedPositions();

    if (simulatedPositions.empty()) return lastPosition;
    return simulatedPositions[BnetPolicy::clampPredictionFrames(frames, (int)simulatedPositions.size()) - 1];
}

int MyUnitImpl::simulateHeading(int frames) const
{
    if (!simulatedPositionsUpdated) updateSimulatedPositions();

    if (simulatedHeading.empty()) return BWHeading();
    return simulatedHeading[BnetPolicy::clampPredictionFrames(frames, (int)simulatedHeading.size()) - 1];
}

void MyUnitImpl::updateSimulatedPositions() const
{
    simulatedPositionsUpdated = true;

    int x = lastPosition.x;
    int bwX = x << 8;
    int y = lastPosition.y;
    int bwY = y << 8;

    int heading = BWHeading();

    int speed = BWSpeed();
    double topSpeed = Players::unitTopSpeed(player, type);
    int bwTopSpeed = Players::unitBWTopSpeed(player, type);
    if (speed > bwTopSpeed) speed = bwTopSpeed;
    int acceleration = UnitUtil::Acceleration(type, topSpeed);
    if (!bwapiUnit->isAccelerating()) acceleration *= -1;

    for (int i=0; i<recentCommands.size(); i++)
    {
        // Compute desired heading to move target at this frame
        int desiredHeading;
        if (recentCommands[i].type == BWAPI::UnitCommandTypes::Move && recentCommands[i].getTargetPosition().isValid())
        {
            desiredHeading = Geo::BWDirection({recentCommands[i].getTargetPosition().x - x, recentCommands[i].getTargetPosition().y - y});
        }
        else
        {
            desiredHeading = heading;
        }

        // Simulate movement
        Geo::BWMovement(bwX, bwY, heading, desiredHeading, type.turnRadius(), speed, acceleration, bwTopSpeed);

        // Assign simulated position and heading
        x = bwX >> 8;
        y = bwY >> 8;
        simulatedPositions[i].x = x;
        simulatedPositions[i].y = y;
        simulatedHeading[i] = heading;
    }
}
