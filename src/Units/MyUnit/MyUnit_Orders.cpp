#include "MyUnit.h"
#include "MyWorker.h"

#include "DebugFlag_UnitOrders.h"
#include "BnetConfig.h"
#include "BnetLog.h"

namespace
{
    // Battle.net APM guard: per-frame counter for worker resource orders (gather / return cargo / resource right-click)
    int resourceOrderFrame = -1;
    int resourceOrdersThisFrame = 0;

    // Returns true if a worker resource order may be issued on this frame (max_orders_per_frame cap)
    bool resourceOrderBudgetAvailable()
    {
        int cap = BnetConfig::get().maxOrdersPerFrame;
        if (cap <= 0) return true;
        if (resourceOrderFrame != currentFrame)
        {
            resourceOrderFrame = currentFrame;
            resourceOrdersThisFrame = 0;
        }
        return resourceOrdersThisFrame < cap;
    }

    void countResourceOrder()
    {
        if (resourceOrderFrame != currentFrame)
        {
            resourceOrderFrame = currentFrame;
            resourceOrdersThisFrame = 0;
        }
        resourceOrdersThisFrame++;
    }

    // Returns true if an identical command to the same unit was issued too recently in a high-latency environment
    // (min_command_interval_frames, defaulting to latencyFrames + 2 when latency > 4)
    bool withinCommandInterval(int lastCommandFrame)
    {
        int interval = BnetConfig::commandIntervalFrames();
        if (interval <= 0) return false;
        return (currentFrame - lastCommandFrame) < interval;
    }

    // Resource command guards only apply to workers in simple gather mode (all mining workers when safe_bnet_mining is on,
    // only stacked-mineral / recovered workers when it is off), since the mining optimizer depends on frame-exact resends
    bool resourceGuardsActive(const MyUnitImpl *unit)
    {
        auto worker = dynamic_cast<const MyWorkerImpl *>(unit);
        return worker && worker->bnetSimpleGather;
    }
}

void MyUnitImpl::move(BWAPI::Position position, bool force)
{
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Move to " << BWAPI::WalkPosition(position) << (force ? " (forced)" : "");
        return;
    }

    auto skipMoveCommand = [&]()
    {
        if (force) return false;
        if (bwapiUnit->isStuck()) return false;
        if (bwapiUnit->getLastCommand().getType() != BWAPI::UnitCommandTypes::Move) return false;
        if (bwapiUnit->getLastCommand().getTargetPosition().getApproxDistance(position) > 3) return false;

        // Battle.net guard (mining moves of simple-gather workers only, see Workers::miningMoveTo): in high-latency
        // environments, don't resend the same move within the minimum command interval. All other units keep upstream behavior.
        if (bnetGuardMoves && withinCommandInterval(lastCommandFrame)) return true;

        // Don't resend orders to similar positions too quickly
        if (lastCommandFrame > (currentFrame - BWAPI::Broodwar->getLatencyFrames() - 3)) return false;

        // Don't resend order if the unit is moving
        return lastCommandFrame < frameLastMoved;
    };

    if (skipMoveCommand()) return;

    issuedOrderThisFrame = bwapiUnit->move(position);
    if (issuedOrderThisFrame)
    {
        lastMoveFrame = currentFrame;
    }

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Move to " << BWAPI::WalkPosition(position) << (force ? " (forced)" : "");
#endif
}

void MyUnitImpl::attack(BWAPI::Unit target, bool force)
{
    if (!target || !target->exists())
    {
        Log::Get() << "ATTACK INVALID TARGET: " << *this;
        return;
    }
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Attack " << target->getType() << " @ " << BWAPI::WalkPosition(target->getPosition());
        return;
    }

    BWAPI::UnitCommand currentCommand(bwapiUnit->getLastCommand());
    if (!force &&
        !bwapiUnit->isStuck() &&
        currentCommand.getType() == BWAPI::UnitCommandTypes::Attack_Unit &&
        currentCommand.getTarget() == target)
    {
        return;
    }

    issuedOrderThisFrame = bwapiUnit->attack(target);

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Attack " << target->getType() << " @ " << BWAPI::WalkPosition(target->getPosition());
#endif
}

void MyUnitImpl::attackMove(BWAPI::Position position)
{
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Attack move to " << BWAPI::WalkPosition(position);
        return;
    }

    BWAPI::UnitCommand currentCommand(bwapiUnit->getLastCommand());
    if (!bwapiUnit->isStuck() &&
        currentCommand.getType() == BWAPI::UnitCommandTypes::Attack_Move &&
        currentCommand.getTargetPosition() == position)
    {
        return;
    }

    issuedOrderThisFrame = bwapiUnit->attack(position);

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Attack move to " << BWAPI::WalkPosition(position);
#endif
}

void MyUnitImpl::rightClick(BWAPI::Unit target)
{
    if (!target || !target->exists())
    {
        Log::Get() << "RIGHT-CLICK INVALID TARGET: " << *this;
        return;
    }
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Right-click " << target->getType() << " @ " << BWAPI::WalkPosition(target->getPosition());
        return;
    }

    // Unless is it a mineral field, don't click on the same target again
    if (!target->getType().isMineralField())
    {
        BWAPI::UnitCommand currentCommand(bwapiUnit->getLastCommand());
        if (currentCommand.getType() == BWAPI::UnitCommandTypes::Right_Click_Unit &&
            currentCommand.getTargetPosition() == target->getPosition())
        {
            return;
        }
    }

    bool isResourceClick = target->getType().isMineralField() || target->getType().isResourceDepot();
    bool guarded = isResourceClick && resourceGuardsActive(this);
    if (guarded)
    {
        auto &stats = BnetLog::resourceOrders();
        stats.attempts++;
        BWAPI::UnitCommand currentCommand(bwapiUnit->getLastCommand());
        if (currentCommand.getType() == BWAPI::UnitCommandTypes::Right_Click_Unit &&
            currentCommand.getTarget() == target &&
            withinCommandInterval(lastCommandFrame))
        {
            stats.blockedInterval++;
            return;
        }
        if (!resourceOrderBudgetAvailable())
        {
            stats.blockedBudget++;
            return;
        }
    }

    auto result = bwapiUnit->rightClick(target);
    if (result && isResourceClick) countResourceOrder();
    if (result && guarded) BnetLog::resourceOrders().issued++;

    if (result)
    {
        issuedOrderThisFrame = true;
        if (target->getType().isMineralField())
        {
            gatherCommandFrames.insert(currentFrame);
        }
        else if (target->getType().isResourceDepot())
        {
            returnCommandFrames.insert(currentFrame);
        }
    }


#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Right-click " << target->getType() << " @ " << BWAPI::WalkPosition(target->getPosition());
#endif
}

bool MyWorkerImpl::gather(BWAPI::Unit target)
{
    if (!target || !target->exists())
    {
        Log::Get() << "GATHER INVALID TARGET: " << *this;
        return false;
    }
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Gather " << target->getType() << " @ " << BWAPI::WalkPosition(target->getPosition());
        return false;
    }

    // Unless it is a mineral field, don't gather on the same target again
    if (!target->getType().isMineralField())
    {
        BWAPI::UnitCommand currentCommand(bwapiUnit->getLastCommand());
        if (currentCommand.getType() == BWAPI::UnitCommandTypes::Gather &&
            currentCommand.getTargetPosition() == target->getPosition())
        {
            return false;
        }
    }

    bool guarded = resourceGuardsActive(this);
    if (guarded)
    {
        // Battle.net guard: don't resend a gather to the same target within the minimum command interval,
        // and never re-gather a target the worker is already moving to / waiting at / mining
        auto &stats = BnetLog::resourceOrders();
        stats.attempts++;
        BWAPI::UnitCommand currentCommand(bwapiUnit->getLastCommand());
        if (currentCommand.getType() == BWAPI::UnitCommandTypes::Gather && currentCommand.getTarget() == target)
        {
            if (withinCommandInterval(lastCommandFrame))
            {
                stats.blockedInterval++;
                return false;
            }

            auto order = bwapiUnit->getOrder();
            if (bwapiUnit->getOrderTarget() == target &&
                (order == BWAPI::Orders::MoveToMinerals || order == BWAPI::Orders::WaitForMinerals ||
                 order == BWAPI::Orders::MiningMinerals || order == BWAPI::Orders::MoveToGas ||
                 order == BWAPI::Orders::WaitForGas || order == BWAPI::Orders::HarvestGas))
            {
                stats.blockedAlready++;
                return false;
            }
        }
        if (!resourceOrderBudgetAvailable())
        {
            stats.blockedBudget++;
            return false;
        }
    }

    auto result = bwapiUnit->gather(target);
    if (result)
    {
        issuedOrderThisFrame = true;
        gatherCommandFrames.insert(currentFrame);
        countResourceOrder();
        if (guarded) BnetLog::resourceOrders().issued++;
    }

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Gather " << target->getType() << " @ " << BWAPI::WalkPosition(target->getPosition());
#endif

    return result;
}

BnetPolicy::RecoveryCommandResult MyWorkerImpl::recoveryGather(BWAPI::Unit target)
{
    if (!target || !target->exists()) return BnetPolicy::RecoveryCommandResult::Rejected;
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Recovery gather " << target->getType();
        return BnetPolicy::RecoveryCommandResult::Rejected;
    }

    if (!resourceOrderBudgetAvailable()) return BnetPolicy::RecoveryCommandResult::BudgetExhausted;

    auto result = bwapiUnit->gather(target);
    if (!result) return BnetPolicy::RecoveryCommandResult::Rejected;

    issuedOrderThisFrame = true;
    gatherCommandFrames.insert(currentFrame);
    countResourceOrder();

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Recovery gather " << BWAPI::WalkPosition(target->getPosition());
#endif

    return BnetPolicy::RecoveryCommandResult::Issued;
}

bool MyWorkerImpl::returnCargo()
{
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Return cargo";
        return false;
    }

    bool guarded = resourceGuardsActive(this);
    if (guarded)
    {
        // Battle.net guard: don't spam return cargo while the worker is already returning or the last return is still in flight
        auto &stats = BnetLog::resourceOrders();
        stats.attempts++;
        auto order = bwapiUnit->getOrder();
        if (order == BWAPI::Orders::ReturnMinerals || order == BWAPI::Orders::ReturnGas)
        {
            stats.blockedAlready++;
            return false;
        }
        if (bwapiUnit->getLastCommand().getType() == BWAPI::UnitCommandTypes::Return_Cargo &&
            withinCommandInterval(lastCommandFrame))
        {
            stats.blockedInterval++;
            return false;
        }
        if (!resourceOrderBudgetAvailable())
        {
            stats.blockedBudget++;
            return false;
        }
    }

    auto result = bwapiUnit->returnCargo();
    if (result)
    {
        issuedOrderThisFrame = true;
        returnCommandFrames.insert(currentFrame);
        countResourceOrder();
        if (guarded) BnetLog::resourceOrders().issued++;
    }

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Return cargo";
#endif

    return result;
}

bool MyUnitImpl::build(BWAPI::UnitType type, BWAPI::TilePosition tile)
{
    if (!tile.isValid())
    {
        Log::Get() << "BUILD INVALID TILE: " << *this;
        return false;
    }
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Build " << type << " @ " << BWAPI::WalkPosition(tile);
        return false;
    }

    issuedOrderThisFrame = bwapiUnit->build(type, tile);

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Build " << type << " @ " << BWAPI::WalkPosition(tile);
#endif

    return issuedOrderThisFrame;
}

bool MyUnitImpl::train(BWAPI::UnitType type)
{
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Train " << type;
        return false;
    }

    issuedOrderThisFrame = bwapiUnit->train(type);

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Train " << type;
#endif

    return issuedOrderThisFrame;
}

bool MyUnitImpl::upgrade(BWAPI::UpgradeType type)
{
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Upgrade " << type;
        return false;
    }

    issuedOrderThisFrame = bwapiUnit->upgrade(type);

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Upgrade " << type;
#endif

    return issuedOrderThisFrame;
}

bool MyUnitImpl::research(BWAPI::TechType type)
{
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Research " << type;
        return false;
    }

    issuedOrderThisFrame = bwapiUnit->research(type);

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Research " << type;
#endif

    return issuedOrderThisFrame;
}

void MyUnitImpl::stop()
{
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Stop";
        return;
    }

    issuedOrderThisFrame = bwapiUnit->stop();

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Stop";
#endif
}

void MyUnitImpl::cancelConstruction()
{
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Cancel Construction";
        return;
    }

    issuedOrderThisFrame = bwapiUnit->cancelConstruction();

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Cancel Construction";
#endif
}

void MyUnitImpl::load(BWAPI::Unit cargo)
{
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Load " << cargo->getType() << " @ " << cargo->getTilePosition();
        return;
    }

    // Don't re-issue the same command
    BWAPI::UnitCommand currentCommand(bwapiUnit->getLastCommand());
    if (currentCommand.getType() == BWAPI::UnitCommandTypes::Load &&
        currentCommand.getTarget() == cargo)
    {
        return;
    }

    issuedOrderThisFrame = bwapiUnit->load(cargo);

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Load " << cargo->getType() << " @ " << cargo->getTilePosition();
#endif
}

void MyUnitImpl::unloadAll(BWAPI::Position pos)
{
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Unload All";
        return;
    }

    issuedOrderThisFrame = bwapiUnit->unloadAll(pos);

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Unload All " << BWAPI::WalkPosition(pos);
#endif
}

void MyUnitImpl::unload(BWAPI::Unit cargo)
{
    if (issuedOrderThisFrame)
    {
        Log::Get() << "DUPLICATE ORDER: " << *this << ": Unload " << cargo->getType() << " @ " << cargo->getTilePosition();
        return;
    }

    issuedOrderThisFrame = bwapiUnit->unload(cargo);

#if DEBUG_UNIT_ORDERS
    CherryVis::log(id) << "Order: Unload " << cargo->getType() << " @ " << cargo->getTilePosition();
#endif
}

void MyUnitImpl::setProducerRallyPosition(BWAPI::Position pos) const
{
    if (!producer) return;

    auto current = producer->getRallyPosition();
    if (current.isValid() && pos.getApproxDistance(current) < 32) return;

    producer->setRallyPoint(pos);

#if DEBUG_UNIT_ORDERS
    CherryVis::log(producer->getID()) << "Order: Set rally position to " << BWAPI::WalkPosition(pos);
#endif
}
