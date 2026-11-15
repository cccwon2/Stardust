#pragma once

#include <string>

// Lightweight one-line-per-event game summary log for Battle.net verification.
// Writes to bwapi-data/write/stardust_bnet.log independently of Stardust's normal logging (which is compiled out in
// TOURNAMENT_BUILD). Enabled at compile time by BNET_LOG_ENABLED and at runtime by bnet_log in stardust_bnet.json.
// Each call writes at most one short line and flushes it, so the log survives crashes and costs no per-frame work
// besides an integer comparison.
namespace BnetLog
{
    // Call at the end of onStart (after BnetConfig and Opponent are initialized)
    void gameStart();

    // Build identifier compiled into the DLL (<branch>@<short hash>[-dirty], from CMake STARDUST_BUILD_ID)
    const char *buildId();

    // Local-only in-game message (Broodwar->printf, not sent to other players) with the build id and config source.
    // Controlled by print_build_info; independent of BNET_LOG_ENABLED / bnet_log. Call after BnetConfig::initialize.
    void printBuildInfo();

    // Same two lines drawn with Broodwar->drawTextScreen (debug overlay) for the first 30 game seconds.
    // On Battle.net the Remastered bridge does not display native printf text (it writes it to its log instead),
    // but it may show debug drawing through its overlay window. Call once per frame.
    void drawBuildInfo();

    // Call once per frame; writes a periodic [APM] line every 2 game minutes (2880 frames)
    void onFrame();

    // Strategy transitions (hooked from Opponent::addEnemyStrategyChange / addMyStrategyChange)
    void enemyStrategyChanged(const std::string &strategy);
    void myStrategyChanged(const std::string &strategy);

    // Free-form event (e.g. opponent left, surrender)
    void event(const std::string &text);

    // Mining diagnostics line ([MINING]: resource registry, base patches, worker snapshots, assignment failures).
    // Written only with mining_assignment_log.
    void mining(const std::string &text);

    // Call from onEnd
    void gameEnd(bool isWinner);

    // ---------------------------------------------------------------------------------------------
    // Cheap in-memory counters (always compiled; only written to the file in periodic [APM] lines and [END]).
    // Reset in gameStart.
    // ---------------------------------------------------------------------------------------------

    // Resource commands (gather / return cargo / resource right-click) of simple-gather (guarded) workers.
    // Recovery commands are counted separately by Workers::bnetRecoveryStats.
    struct ResourceOrderStats
    {
        int attempts = 0;
        int issued = 0;
        int blockedInterval = 0;   // Same command to the same target within min_command_interval_frames
        int blockedAlready = 0;    // Already moving to / waiting at / mining the target, or already returning cargo
        int blockedBudget = 0;     // max_orders_per_frame exhausted
        int blockedBackoff = 0;    // Worker still in resource_fail_backoff_frames after a rejected command
        int failed = 0;            // Command sent to BWAPI but rejected (returned false)
    };
    ResourceOrderStats &resourceOrders();

    // MidGameBio attack-state decisions in AttackBaseSquad (counted only when the bio scales are active)
    void bioDecision(bool wasAttacking, bool nowAttacking);

    // MidGameMech attack-state decisions in AttackBaseSquad (counted only when the mech scales are active)
    void mechDecision(bool wasAttacking, bool nowAttacking);

    // Our own command counter. BWAPI's getAPM() returns 0 on Remastered, so every frame MyUnitImpl::update counts the
    // units that were issued an order on the previous frame (one order per unit per frame, as enforced by MyUnit).
    enum class OrderCategory { Worker, Observer, Army };
    void countIssuedOrder(OrderCategory category);

    // Non-worker moves skipped by the repeated move guard (combat_move_guard)
    void moveSkipped();

    // Grid waypoint re-targets avoided by grid_waypoint_hold
    void gridHold();

    // Unstick stops of repeatedly stuck workers sent with a backoff longer than upstream (unstick_backoff_max_frames)
    void unstickBackoff();

    // Stasis / unload orders not re-sent because the same order is still in flight (spell_resend_interval_frames)
    void repeatedOrderSkipped();

    // PvT combat: attacks kept by attack_commit_frames (attack=true) / regroups kept by regroup_commit_frames
    void combatCommitHold(bool attack);

    // PvT combat: frames where a fleeing cluster fought instead because the pursuers were faster (flee_guard)
    void fleeGuardFight();

    // Dragoon stop orders sent to unstick a dragoon whose attack was cancelled (upstream MyDragoon::unstick)
    void dragoonUnstick();

    // Move orders rejected because the target position was invalid (invalid_move_guard). The first few and then every
    // 500th are also written as [EVENT] invalid_move lines with the unit type.
    void invalidMove(const std::string &unitType);

    // Workers sent away from a vulture instead of attacking it (worker_flee_vultures)
    void workerFlee();

    // Resource command rejected by BWAPI (counted in res_failed). The first few and then every 500th are also written as
    // [EVENT] res_failed lines with the command, unit type, order and BWAPI error.
    void resourceOrderFailed(const std::string &command, const std::string &unitType, const std::string &order,
                             const std::string &error);

    // Non-worker moves skipped by the arrived-unit move guard (arrived_move_hold_frames)
    void arrivedMoveHeld();

    // PvT mech expansion gate state change (logged as an [EVENT] line only when the state changes)
    void mechExpandGate(bool blocked, int bases, int armyUnits, int required);
}
