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

    // Call once per frame; writes a periodic [APM] line every 2 game minutes (2880 frames)
    void onFrame();

    // Strategy transitions (hooked from Opponent::addEnemyStrategyChange / addMyStrategyChange)
    void enemyStrategyChanged(const std::string &strategy);
    void myStrategyChanged(const std::string &strategy);

    // Free-form event (e.g. opponent left, surrender)
    void event(const std::string &text);

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
    };
    ResourceOrderStats &resourceOrders();

    // MidGameBio attack-state decisions in AttackBaseSquad (counted only when the bio scales are active)
    void bioDecision(bool wasAttacking, bool nowAttacking);
}
