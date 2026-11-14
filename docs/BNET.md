# Stardust for StarCraft: Remastered Battle.net (Pluto Terran snipe build)

This fork adds runtime switches so the bot can be tuned on Battle.net without recompiling.

## Config file

The bot reads the first file found among:

1. `bwapi-data/AI/stardust_bnet.json`
2. `bwapi-data/read/stardust_bnet.json`
3. `bwapi-data/write/stardust_bnet.json`

If no file is found, or a key is missing or has the wrong type, the default below is used.
Comments (`//`, `/* */`) are allowed. The template is `config/stardust_bnet.json`.

The file is read once at game start (`onStart`). Edits made during a game take effect from the next game; there is no hot reload.
The effective values (after clamping and derived defaults) are written to the `[START]` log line.

## Phase 1: Battle.net stability

| Key | Default | Effect |
| --- | --- | --- |
| `safe_bnet_mining` | `true` | Bypasses the MiningOptimizationV2 optimized initial split, start-of-mining and return-of-resource path optimizations (all frame-exact resends). Workers use plain `gather()` / `returnCargo()` only, and are never re-ordered while already moving to, waiting at, or mining their patch. All mining workers are in simple gather mode, so the resource command guards (below) apply to all of them. |
| `stacked_mineral_fallback` | `true` | Detects bases with stacked/overlapping mineral patches (same tile or centers < 24px apart) and uses simple gather mode there even when `safe_bnet_mining` is off. Also recovers stuck workers (see "Stuck worker recovery" below). |
| `stuck_worker_frames` | `48` | Stuck detection threshold (clamped 12-1000). |
| `max_orders_per_frame` | `0` | Cap on worker resource orders (gather / return cargo / resource right-click) per frame. `0` = unlimited. |
| `min_command_interval_frames` | `-1` | Minimum frames between identical commands to the same unit. `-1` = auto: `maxLatency + 2` when the highest `getLatencyFrames()` seen this game is `> 4`, otherwise off (the maximum is latched because Remastered reported 5 at game start and 3 later, which used to switch the guard off mid-game). Also used as the interval of `combat_move_guard` (see "PvT vs. mech" below). Applies only to workers in simple gather mode: same-target gather / return cargo / resource right-click, and same-target mining moves issued directly by `Workers::issueOrders` (`Workers::miningMoveTo`). Combat units, scouting, fleeing no-go areas, building and other worker moves keep upstream behavior. |
| `enable_auto_gg` | `false` | Enables the surrender logic (send "gg" and leave). Previously this was always on in `VS_HUMAN` builds. The surrender condition itself is unchanged and strict: no probes, no nexus or < 50 minerals, no mobile combat unit, and an enemy combat unit at one of our bases. |

### Stuck worker recovery

Only cargo-less workers in `MoveToMinerals` or `WaitForMinerals` are monitored. The timer resets when the worker moves more
than 2px, picks up or carries cargo (minerals or gas), changes patch, switches to any other order (`MiningMinerals`,
returning cargo, building, ...), or is not observed on the previous frame (job change, reserved, lost). `WaitForMinerals`
while another worker is mining the same patch is normal queueing and never counts. A worker carrying cargo is never
reassigned to another patch.

After `stuck_worker_frames` without progress, a recovery is attempted:

- Target: the least-occupied visible patch at the same base, or the current patch if there is no alternative.
- Command: `MyWorkerImpl::recoveryGather`, which bypasses the duplicate-command guards (same-command interval,
  already doing it) so a plain re-gather to the same patch is never permanently blocked, but still respects
  `max_orders_per_frame` and the one-order-per-frame rule. It is not a forced resend loop.
- Rate limit: one attempt per worker per `stuck_worker_frames` frames. If the per-frame budget is exhausted the attempt is
  retried after `latencyFrames + 2` frames.
- The internal patch assignment changes only after the command is actually issued. A rejected command (or no visible
  target) keeps the assignment and is counted as a failure.
- After an issued recovery the worker stays in simple gather mode for the rest of the game.

Counters (`Workers::bnetRecoveryStats()`): attempts, reassigned (issued and moved to another patch), command ok, command
failed, budget deferred. `stuck_reassign` in the log now means successful reassignments to a different patch; before this
change it counted every reassignment attempt.

### Command guard scope

| Setting combination | Guarded workers | Remaining guards |
| --- | --- | --- |
| `safe_bnet_mining=true` | All Minerals / Gas job workers (simple gather) | Interval + "already doing it" + budget on their resource commands and direct mining moves |
| `safe_bnet_mining=false`, `stacked_mineral_fallback=true` | Only workers at stacked-mineral bases and workers that were recovered | Same as above for those workers only; optimized workers are untouched |
| both `false` | None | None (`max_orders_per_frame` still caps `recoveryGather`, which is unused when the fallback is off) |

Limitation: follow-up waypoint moves issued later by the pathing code (`MyUnitImpl::updateMoveWaypoints`) are not guarded.

## Code map (actual upstream paths)

- Mining backends: `src/Workers/MiningOptimizationV2/` (active, selected by `WORKERGATHEROPTIMIZER` in `src/Workers/WorkerGatherOptimizer.h`), `src/Workers/MineralLockingOptimization/`, `src/Workers/OrderProcessTimerOptimization/` (inactive).
- Worker command flow: `Workers::updateAssignments` -> `Workers::issueOrders` (`src/Workers/Workers.cpp`) -> `MyWorkerImpl::gather` / `returnCargo` and `MyUnitImpl::move` / `rightClick` (`src/Units/MyUnit/MyUnit_Orders.cpp`).
- Config loader: `src/Util/BnetConfig.{h,cpp}`.

## Phase 2: Pluto Terran (PvT) snipe

| Key | Default | Effect |
| --- | --- | --- |
| `pluto_pvt_snipe` | `true` | Master switch for every Phase 2 change. `false` restores upstream PvT behavior. |
| `forced_pvt_opening` | `"EarlyGameDefense"` | PvT opening used instead of UCB1. Valid: `EarlyGameDefense` (1-gate core dragoon, then reacts to scouting), `FastExpansion`. Empty string = UCB1. Note: upstream PvT UCB1 only has `EarlyGameDefense` as a candidate, so this mainly makes the choice explicit and independent of opponent name history. |
| `forced_random_opening` | `"OneGateCore"` | Opening used against a Random enemy instead of UCB1 (PvU picks among `ForgeFastExpand`, `TwoGateZealots`, `OneGateCore`). `OneGateCore` hands off cleanly to PvT `EarlyGameDefense` once Terran is confirmed. Empty string = UCB1. |
| `early_observer_priority` | `true` | Staged request for the first observer (the producer adds robo + observatory at the same priority). See "First observer policy" below. `false` (or `pluto_pvt_snipe=false`) restores upstream detection. |
| `observer_min_dragoons` | `2` | Completed dragoons required before a two-factory / mech / 3+ vulture observer request is raised to high priority (0-12). Below it the request stays at normal priority (below dragoon base production) instead of being dropped. Initial value only, not validated in real games. |
| `natural_cannons` | `1` | Conditional cannon placement requests (0-2) in the natural mineral line once our natural nexus is complete (not in `ForgeExpandGoons`, whose play handles the natural). Raised to `mech_natural_cannons` under a mech / vulture threat; see "Cannons vs. vultures" below. |
| `main_cannons` | `1` | Conditional cannon placement request (0-1) in the main mineral line against `TwoFactory` / `MidGameMech` / `MidGameBioMech` / 3+ vultures, independent of the natural, not during `AntiMarineRush`. If the key is absent: `1` when `natural_cannons > 0`, else `0` (old files with `natural_cannons: 0` keep all cannons off). |
| `bio_attack_start_scale` | `0.7` | Multiplier in `shouldStartAttack` (moving -> attacking) vs. `MidGameBio` (0.1-2.0). |
| `bio_attack_continue_scale` | `1.0` | Multiplier in `shouldContinueAttack` (attacking -> regrouping). `1.0` keeps upstream continue decisions. |
| `bio_attack_resume_scale` | `0.7` | Multiplier in `shouldStopRegrouping` (regrouping -> attacking). |
| `bio_aggression_scale` | (legacy) | Pre-split key. Now applies to start and resume only, never to continue. See compatibility below. |

### First observer policy

Only the first observer is governed (no observer completed; upstream already returns while one is in production and when
two observers are with the main army). Once one exists, or when no row below applies, the upstream policy runs unchanged
(two nexuses, mine -> high priority, vulture/tank after frame 10000, frame 14000).

| Stage | Condition | Priority |
| --- | --- | --- |
| `urgent` | Spider mine seen or Spider Mines researched | `PRIORITY_SPECIALTEAMS`, also during `AntiMarineRush`, regardless of dragoon count |
| `elevated` | `TwoFactory` / `MidGameMech` / `MidGameBioMech` or 3+ vultures, and dragoons >= `observer_min_dragoons` | `PRIORITY_SPECIALTEAMS` |
| `elevated_waiting_dragoons` | Same, fewer dragoons | `PRIORITY_NORMAL` (below `PRIORITY_MAINARMYBASEPRODUCTION`) |
| `preventive` | A vulture or machine shop seen, frame > 6500, or dragoon range started | `PRIORITY_NORMAL` |
| `upstream` | Nothing above, `AntiMarineRush` without mines, or feature off | Upstream policy |

Mines are cloaked, so the main army also requests detection reactively through `updateDetectionNeeds()` (unchanged).
Exactly one observer goal is added per frame; production goals are rebuilt every frame, so nothing accumulates.

### MidGameBio aggression

Only when `pluto_pvt_snipe` is on, the enemy is Terran and the recognized enemy strategy is `MidGameBio`. Each scale is
applied after the reinforcement adjustment and before the "pushed back to our natural" bonus; stability rules (6 frames
before aborting, 12 frames before resuming, recent-result voting, reinforcement link-up wait) are unchanged.

| Decision | Upstream base aggression | Final aggression passed to `shouldAttack` |
| --- | --- | --- |
| Start (`shouldStartAttack`) | `reinforcementFactor` (<= 1.0) | `reinforcementFactor * start_scale * naturalBonus` |
| Continue (`shouldContinueAttack`) | `1.2` (+0.005 per supply above 300 if no reinforcements) | `base * continue_scale * naturalBonus` |
| Resume (`shouldStopRegrouping`) | `0.8` minus choke penalty, `1.2` after `ContainChoke`, `1.0` in `StandGround` | `base * reinforcementFactor * resume_scale * naturalBonus(x2 within 500)` |

Note `shouldAttack` has an extra "kill 50% more value than we lose" branch only when aggression > 0.99. With
start/resume at 0.7 that branch is disabled for start and resume unless the natural bonus applies; continue at 1.0 keeps it
(1.2). Example with defaults: start 1.0 -> 0.7, continue 1.2 -> 1.2, resume 0.8 -> 0.56 (`StandGround` 1.0 -> 0.7).

Compatibility (`bio_aggression_scale` = legacy key):

| File contents | start | continue | resume |
| --- | --- | --- | --- |
| No bio keys | 0.7 | 1.0 | 0.7 |
| Legacy only (`X`) | X | 1.0 | X |
| New keys (any) | new key, else legacy, else 0.7 | new key, else 1.0 | new key, else legacy, else 0.7 |

Upstream behavior: set all three new keys to `1.0` (or `pluto_pvt_snipe=false`). START logs the effective values and
`bio_aggression_scale(legacy)=X(start/resume only)` or `absent`.

These values are starting points; static checks do not show fewer attack/retreat oscillations or a higher win rate.

### Cannon placement requests

Requests are conditional, not guaranteed: only the predefined `workerDefenseCannons` locations of
`BuildingPlacement::baseStaticDefenseLocations` are used (there is no choke cannon helper in Stardust). Requests use
`PRIORITY_NORMAL`, below dragoon base production; the producer adds the forge. Per base: the power pylon is requested
first (unless already queued); completed cannons, cannons under construction and buildings already queued at a location
count towards the target; locations occupied by another building are skipped. Invalid locations, an occupied pylon tile or
too few free locations are logged once per base and reason.

Recognizer changes (`PvTEnemyStrategyRecognizer.cpp`, only when `pluto_pvt_snipe` is on):

- `TwoFactory` (2FactVult) is now also recognized by: early machine shop (< frame 6000) + 2 vultures (< 6500), 3 vultures (< 7000), or a spider mine (< 7000), and is re-checked from `Normal` / `WallIn` until frame 9000. Upstream only checked "2 factories" while still `Unknown`, which usually resolved to `Normal` at frame 4000 before the second factory was scouted.
- Early bio (fast second barracks with academy or gas, academy + 4 marines by frame 5500, or an early medic) maps to `MarinePressure` and is re-checked from `Normal` / `WallIn` until frame 7500.
- Our strategy: `Normal` falls back to `Defensive` once (before two nexuses, before frame 10000) when `TwoFactory` or `MarinePressure` is recognized late.

Mid-game vs bio: keep at least `max(4, dragoons)` zealots with leg enhancements started at 2 zealots, and allow arbiters vs `MidGameBio` / `MidGameBioMech` under upstream's conditions (3 nexuses, 20+ army). High templar storm is not added because Stardust has no high templar micro.

## PvT vs. mech and repeated move guard

Added after a Battle.net loss to a 1-rax FE mech Terran (Backrooms 1.1, 17:54): the enemy was recognized as `MidGameMech`
at 5:36, but the bot took its 5th nexus at 10:08 with roughly 20 zealots/dragoons, built 43 zealots vs. 34 dragoons into
tanks/vultures/mines, spent gas on observer/shuttle upgrades and a robotics support bay, and lost to a single tank push at
12:00. The replay also showed about 20,000 APM (167,609 move orders, 42,031 of them to observers), while the log reported
`bwapi_apm=0` throughout.

All mech keys apply only while `pluto_pvt_snipe` and `pvt_mech_fix` are on and the Terran is recognized as `MidGameMech`.

| Key | Default | Effect |
| --- | --- | --- |
| `pvt_mech_fix` | `true` | Master switch for the mech changes below. `false` = previous behavior. |
| `mech_workers_per_army_unit` | `4` | Baseline main-army production (`PvT` mid/late game): one unit per N mineral workers, ahead of other production. Upstream uses 6 (2-12). |
| `mech_zealot_ratio` | `0.5` | With 3+ enemy tanks, desired zealots = `min(ratio * dragoons, 2 * tanks)` instead of upstream `min(1.5 * dragoons, 3 * tanks)` (0-1.5). Zealots are still used as the gas-shortage filler, as upstream. |
| `mech_expand_army_per_base` | `6` | From the 4th base on, a new expansion is queued only if completed zealots + dragoons >= N x owned bases (3 bases -> 18, 4 -> 24). Existing expansion plays are not cancelled. Also removes the "enemy contained" expansion boost vs. mech, since a turtling tank line looks contained. `0` = off (0-20). |
| `mech_luxury_upgrade_min_army` | `20` | Defers Gravitic Boosters, Sensor Array and Gravitic Drive (which pulls in a robotics support bay) until the army reaches N units. `0` = off (0-60). |
| `mech_attack_start_scale` | `0.7` | AttackBaseSquad aggression multiplier for start (moving -> attacking), same decision points as the bio scales (0.1-2.0). |
| `mech_attack_continue_scale` | `1.0` | Continue (attacking -> regrouping). `1.0` = upstream. |
| `mech_attack_resume_scale` | `0.7` | Resume (regrouping -> attacking). |
| `combat_move_guard` | `true` | For non-worker units (observers, army): a move to within `move_resend_tolerance_px` of the last move target is skipped if the last command was sent less than the command interval ago (auto: latched max latency + 2, i.e. 7 frames at latency 5). Forced moves, stuck units and other command types (attack, stop, ...) are never skipped. Inactive when the interval is 0 (latency <= 4 and `min_command_interval_frames=-1`). |
| `move_resend_tolerance_px` | `64` | Distance (px) under which a new move target counts as the same target (3-192). Upstream resends above 3px. Was 24 until the APM fix below. |

### APM fix (2026-11-15 Fighting Spirit replay)

In a Battle.net PvT replay (opponent human, latency 5) the bot sent 234,356 commands in 19 minutes (12,103 APM by screp; the opponent had 213). 108,208 were Move orders, almost all re-sent to the same unit every 1-8 frames with the target shifting 16-48px, so the 24px `combat_move_guard` passed 99% of them. Causes and fixes (all Battle.net only: inactive while the latency command interval is 0):

| Key | Default | Description |
|---|---|---|
| `grid_waypoint_hold` | `true` | Grid navigation moves towards the node 5 tiles ahead and used to re-target on every tile change (one move per tile, ~every 6-8 frames for a dragoon). The current waypoint is now kept while it is still one of the next nodes on the path and the unit is farther than `grid_waypoint_refresh_px` from it, and not near the grid goal. |
| `grid_waypoint_refresh_px` | `64` | Distance to the waypoint at which the next one is sent (16-160). |
| `combat_move_interval_frames` | `12` | `combat_move_guard` interval for non-worker units, never below the latency command interval (0-48). |
| `observer_move_interval_frames` | `24` | Same for observers following the army (5 observers sent 14,586 orders in the replay) (0-96). |
| `move_guard_min_remaining_px` | `48` | The guard never skips once the unit is closer than this to its last move target, so units do not stop and stutter (0-256). Replay simulation of the interval/tolerance change alone: about half of the Move orders removed. |
| `spell_resend_interval_frames` | `24` | Identical stasis (raw `useTech` every frame upstream, 148 casts) and shuttle unload / unload-all orders are not re-sent within this interval. `0` = off (0-96). |
| `unstick_backoff_max_frames` | `192` | Workers only: a probe trapped on the way to a nexus center got a stop + move every ~12 frames for 3 minutes (323 stops, 619 moves). After two unstick attempts without leaving the spot (32px), the wait doubles 24 -> 48 -> 96 -> cap. Army units keep upstream timing. `0` = off (0-480). |

Also: when the move guard skipped a move to a nearby waypoint, `updateMoveWaypoints` treated the unchanged last command as a new order (3px check) and re-planned the path every frame; the check now uses `move_resend_tolerance_px` while the guard is active.

### Cannons vs. vultures (expansions)

In the same replay the third base had no cannon for 13 minutes against 108 vultures and the natural cannon came 4.5 minutes after the nexus (forge at 8:23).

| Key | Default | Description |
|---|---|---|
| `mech_natural_cannons` | `2` | Natural cannon target raised to this under a mech / vulture threat (`TwoFactory` / `MidGameMech` / `MidGameBioMech` / 3+ vultures / a spider mine seen) (0-2). |
| `expansion_cannons` | `1` | Cannons in the mineral line of every other owned base (third and later) under the same threat, once its nexus is complete. Set `2` for more vulture insurance (0-2). |
| `cannon_threat_priority` | `true` | Under the threat, the first cannon of each base (and its power pylon) is requested at `PRIORITY_BASEDEFENSE` instead of `PRIORITY_NORMAL`, so it is not starved by army production; the forge follows as a prerequisite. Further cannons stay at normal priority. |

Each base writes `[EVENT] cannon_plan base=main|natural|expansion tile=x,y target=.. existing=.. requested=.. urgent=.. mech_threat=..` once per target count.

Not changed: the arbiter condition (3 nexuses, 20+ army), carrier switch (disabled upstream), unit micro.

## PvZ vs. zergling all-ins (2026-11-15 Radeon replay)

Loss in 6:15 to a human 12 pool -> hatchery -> speedling all-in (pool 1:17, speed 3:38, about 24 zergling morphs). The
opening came from UCB1 (`FFEDragoons`, nothing fixed it for Zerg). `PoolBeforeHatchery` was recognized at 2:01, but
`ZerglingAllIn` only at frame 6782 (4:42), after the lings had broken the natural: the upstream rules count zerglings by
estimated creation frame (9 before frame 4000, 13 before 5000, 20 before 7000). `FFEDragoons` has no transition for
`ZerglingAllIn`, and its zealot target `1 + lings/3 - completed cannons` stayed at one zealot behind two wall cannons,
below probe production. Army at the end: 2 zealots, 1 dragoon; 27 of 30 trained units were probes.

| Key | Default | Description |
|---|---|---|
| `forced_pvz_opening` | `FFEDragoons` | PvZ opening instead of the per-opponent UCB1 search: `FFEDragoons`, `SairSpeedlot` or `EarlyGameDefense` (1 gate, safest vs. early pools, slower economy). Empty = UCB1. Independent of `pvz_antiallin_fix`. Without a forge-gateway wall on the map the FFE openings fall back to the upstream main-defense play. |
| `pvz_antiallin_fix` | `true` | Master switch for the keys below. |
| `pvz_allin_base_lings` | `6` | Once the opening is known (pool or hatchery first), this many zerglings at our main / natural before frame 9000 confirm `ZerglingAllIn`. `0` = off (0-30). |
| `pvz_allin_seen_lings` | `12` | Same with this many living zerglings known anywhere before frame 8000. `0` = off (0-40). |
| `pvz_pool_first_cannons` | `3` | FFE wall cannons once `PoolBeforeHatchery` or `ZerglingAllIn` is recognized (upstream 2). The third cannon is timed for frame 4500, further ones +1000 frames each. The upstream all-in schedule (up to 5 by frame 6000) still applies on top (2-5). |
| `pvz_pool_first_zealots` | `2` | `FFEDragoons`: minimum zealots vs. pool first, at base-defense priority (0-6). |
| `pvz_allin_zealots` | `4` | `FFEDragoons` during `ZerglingAllIn`: minimum zealots, or one per three known zerglings (at most 8), requested above probe production (`PRIORITY_EMERGENCY + 1`) (0-12). |
| `pvz_allin_probe_cap` | `18` | FFE play during `ZerglingAllIn`: probe goals above this count move to main-army priority, below the zealots. `0` = off; otherwise 12-40. |

- The extra all-in confirmation latches until frame 10000 (or until lair tech, which upstream already turns into `Lair`), so the measures do not flicker when the lings step out of vision. It adds to the upstream rules and never removes them.
- `FFEDragoons` still does not switch to `AntiAllIn` (that strategy defends the main and would abandon the natural nexus and wall); the measures stay inside the FFE.
- Log: `[EVENT] pvz_allin_detect lings_at_bases=.. lings_alive=.. from=..` when the extra rule fires, and `[EVENT] pvz_defense enemy=.. wall_cannons=.. min_zealots=.. probe_cap=..` whenever the FFEDragoons defense targets change. An unknown `forced_pvz_opening` writes `[EVENT] forced_pvz_opening_unknown`.
- Not changed: zergling recognition before the opening is known, `ZerglingRush` handling (the FFE play already cancels the natural and defends the main), other PvZ strategies, unit micro.

## Phase 3: Build script and summary log

### Build id at game start

- CMake computes `STARDUST_BUILD_ID` at configure time as `<branch>@<8-char hash>`, with `-dirty` when tracked files have uncommitted changes (`unknown` without git). Override with `-DSTARDUST_BUILD_ID=...`. `scripts/build-bnet.ps1` reconfigures on every run, so the id follows the checked-out commit.
- `print_build_info` (default `true`): at `onStart` the bot calls `Broodwar->printf` twice. This is a local screen message, not chat, so other players do not see it:
  - `Stardust bnet build bnet-pluto@73d9b55d`
  - `config: bwapi-data/AI/stardust_bnet.json | pluto_pvt_snipe=1 pvt_mech_fix=1 combat_move_guard=1` (or `config: defaults (stardust_bnet.json not found)`)
- For the first 30 game seconds the same two lines are also drawn with `Broodwar->drawTextScreen` (debug drawing, at screen 8,30). `print_build_info=false` disables both.
- The same id is the first field of the `[START]` log line (`build=...`). The message is independent of `BNET_LOG` / `bnet_log`.
- Battle.net: the original bridge runtime only displays native `printf` text with a single-player text binding; otherwise it writes the text to its log (`Text written to bridge log; native display unavailable` in its strings). So on Battle.net the `printf` lines are expected in the bridge log, not on screen. The `drawTextScreen` lines go through the bridge's debug overlay window instead; whether the Battle.net candidate shows them is not verified.

- `scripts/build-bnet.ps1`: configures with the generator of the newest Visual Studio 2022/2026 install that has the MSVC x86/x64 tools (`Visual Studio 17 2022` or `Visual Studio 18 2026`, override with `-Generator`; the CMake bundled with that Visual Studio is preferred and must list the generator), `-A Win32 -DTOURNAMENT_BUILD=ON` (which also skips the `windbg` target), builds only the `Stardust` target in Release, and copies `Stardust.dll` plus `config/stardust_bnet.json` to `dist/bwapi-data/AI/`. An existing `dist/.../stardust_bnet.json` is kept unless `-OverwriteConfig`; the defaults are always written to `stardust_bnet.default.json`. Options: `-Clean` (required when an existing build directory was configured with another generator), `-VsHuman`, `-NoBnetLog`, `-BuildDir`, `-DistDir`, `-Generator`.
- CMake option `BNET_LOG` (default `ON`) compiles the summary log in, independent of `LOGGING_ENABLED` (which `TOURNAMENT_BUILD` turns off). Runtime switch: `bnet_log`.
- Log file `bwapi-data/write/stardust_bnet.log`, appended, one line per event, flushed immediately:

```
2026-11-15 21:03:11 f=0 t=0:00 [START] build=bnet-pluto@73d9b55d map="..." opponent="..." enemy_race=Terran latency_frames=6 battlenet=1 multiplayer=1 config=bwapi-data/AI/stardust_bnet.json safe_bnet_mining=1 ... observer_min_dragoons=2 natural_cannons=1 main_cannons=1 bio_attack_start_scale=0.7 bio_attack_continue_scale=1 bio_attack_resume_scale=0.7 bio_aggression_scale(legacy)=absent bnet_log=1
2026-11-15 21:03:11 f=0 t=0:00 [OURS] from=- to=EarlyGameDefense
2026-11-15 21:04:40 f=2100 t=1:28 [EVENT] detection_stage from=Upstream to=Preventive mine=0 twofact_or_mech=0 vultures=0 vulture_or_shop=0 anti_marine_rush=0 timing=1 dragoons=2 observers=0
2026-11-15 21:06:02 f=4100 t=2:52 [ENEMY] from=Unknown to=TwoFactory
2026-11-15 21:06:02 f=4101 t=2:52 [EVENT] detection_stage from=Preventive to=Urgent mine=0 twofact_or_mech=1 ...
2026-11-15 21:06:40 f=4700 t=3:16 [EVENT] cannon_issue base=natural reason="..."
2026-11-15 21:07:05 f=5300 t=3:41 [EVENT] first_observer_complete
2026-11-15 21:07:14 f=5760 t=4:01 [APM] bwapi_apm=142 latency_frames=6 supply=31 res_attempts=2210 res_issued=905 res_blocked_interval=310 res_blocked_already=990 res_blocked_budget=5 stuck_attempts=1 stuck_reassign=0 stuck_cmd_ok=1 stuck_cmd_failed=0 stuck_budget_deferred=0 bio_to_attack=0 bio_to_regroup=0
2026-11-15 21:20:40 f=24500 t=17:09 [END] result=WIN bwapi_apm_at_end=160 bwapi_apm_max_sampled=171 final_enemy_strategy=MidGameMech final_our_strategy=MidGame res_attempts=... bio_to_attack=3 bio_to_regroup=2
```

(Numbers above are illustrative only, not measured results.)

### Log line meanings

- Lines are written only on state changes (`[START]`, `[OURS]`, `[ENEMY]`, `[EVENT]`, `[END]`) and every 2 game minutes (`[APM]`, every 2880 frames). Nothing is written per frame or per unit; the counters below are plain in-memory integers.
- `orders_per_min` (`[APM]` line): our own count of orders issued over the last 2 game minutes, converted to per minute. Counted in `MyUnitImpl::update` from the one-order-per-unit-per-frame flag, so selections are not included and orders sent directly through BWAPI outside `MyUnit` are missed. Use this instead of `bwapi_apm`, which was 0 on Remastered.
- `orders_total` / `orders_worker` / `orders_observer` / `orders_army`: cumulative issued orders by unit category. `moves_skipped`: non-worker moves skipped by `combat_move_guard`. `grid_holds`: grid re-targets avoided by `grid_waypoint_hold`. `unstick_backoffs`: worker unstick stops sent with a longer backoff. `orders_repeat_skipped`: stasis / unload orders not re-sent.
- `mech_to_attack` / `mech_to_regroup`: like `bio_*`, while the mech scales are active. `mech_gate_blocks`: times the mech expansion gate switched to blocked; each switch also writes `[EVENT] mech_expand_gate=blocked|open bases=.. army=.. required=..`.
- `latency_max` / `cmd_interval`: highest latency seen so far and the effective command interval.
- `bwapi_apm`: `BWAPI::Game::getAPM(false)` (selections excluded), sampled when the `[APM]` line is written. BWAPI computes it with an exponential decay (APMAlert formula, time constant about 0.95 minutes), so it reflects roughly the last minute of play, not a game average and not our own command count. `bwapi_apm_at_end` is the same value at game end; `bwapi_apm_max_sampled` is the maximum over the 2-minute samples (short spikes between samples are not captured). Older logs used `apm`, `avg_apm` and `max_sampled_apm` for these values.
- `res_*`: cumulative counts (since game start) of resource commands (gather / return cargo / resource right-click) for workers in simple gather mode only, i.e. where the Battle.net guards apply. `res_attempts` = calls reaching the guard; `res_issued` = commands BWAPI accepted; `res_blocked_interval` = same command to the same target within `min_command_interval_frames`; `res_blocked_already` = already moving to / waiting at / mining the target, or already returning cargo; `res_blocked_budget` = `max_orders_per_frame` exhausted. Attempts that BWAPI itself rejects are counted in `res_attempts` only. Optimized-mining workers and recovery commands are not counted here.
- `stuck_*`: cumulative stuck-worker recovery counts: `stuck_attempts` (recoveries started), `stuck_reassign` (successful reassignment to a different patch), `stuck_cmd_ok` / `stuck_cmd_failed` (recovery gather accepted / rejected by BWAPI), `stuck_budget_deferred` (postponed because the per-frame budget was exhausted).
- `bio_to_attack` / `bio_to_regroup`: cumulative attack <-> regroup transitions of attack-base squads, counted only while the MidGameBio scales are active (Terran opponent recognized as `MidGameBio` with `pluto_pvt_snipe` on). Repeated decisions in the same state are not counted.
- `[EVENT] detection_stage`: the first-observer request stage changed; the line lists the inputs that drove the decision. `first_observer_complete` is written once, on the first frame a completed observer exists (PvT only). `cannon_issue` is written once per distinct cannon placement problem. `opponent_left` and `surrender` are unchanged.
- The log compiles with `TOURNAMENT_BUILD=ON` (CMake `BNET_LOG`, default `ON`) and can be turned off at runtime with `bnet_log`. The counters are kept even when the log is disabled, but are not written anywhere.
