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

    // Call once per frame; writes a periodic APM line every 2 game minutes
    void onFrame();

    // Strategy transitions (hooked from Opponent::addEnemyStrategyChange / addMyStrategyChange)
    void enemyStrategyChanged(const std::string &strategy);
    void myStrategyChanged(const std::string &strategy);

    // Free-form event (e.g. opponent left, surrender)
    void event(const std::string &text);

    // Call from onEnd
    void gameEnd(bool isWinner);
}
