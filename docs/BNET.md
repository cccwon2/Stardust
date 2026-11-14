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
| `min_command_interval_frames` | `-1` | Minimum frames between identical commands to the same unit. `-1` = auto: `latencyFrames + 2` when `getLatencyFrames() > 4`, otherwise off. Applies only to workers in simple gather mode: same-target gather / return cargo / resource right-click, and same-target mining moves issued directly by `Workers::issueOrders` (`Workers::miningMoveTo`). Combat units, scouting, fleeing no-go areas, building and other worker moves keep upstream behavior. |
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
| `natural_cannons` | `1` | Conditional cannon placement requests (0-2) in the natural mineral line once our natural nexus is complete (not in `ForgeExpandGoons`, whose play handles the natural). |
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

## Phase 3: Build script and summary log

- `scripts/build-bnet.ps1`: configures with the newest installed Visual Studio 2022 (`-G "Visual Studio 17 2022"`) or 2026 (`-G "Visual Studio 18 2026"`, needs CMake 4.2+; the CMake bundled with Visual Studio is preferred), `-A Win32 -DTOURNAMENT_BUILD=ON` (which also skips the `windbg` target), builds only the `Stardust` target in Release, and copies `Stardust.dll` plus `config/stardust_bnet.json` to `dist/bwapi-data/AI/`. An existing `dist/.../stardust_bnet.json` is kept unless `-OverwriteConfig`; the defaults are always written to `stardust_bnet.default.json`. Options: `-Clean`, `-VsHuman`, `-NoBnetLog`, `-BuildDir`, `-DistDir`, `-Generator` (override). A build dir configured with a different generator is rejected; use `-Clean`.
- CMake option `BNET_LOG` (default `ON`) compiles the summary log in, independent of `LOGGING_ENABLED` (which `TOURNAMENT_BUILD` turns off). Runtime switch: `bnet_log`.
- Log file `bwapi-data/write/stardust_bnet.log`, appended, one line per event, flushed immediately:

```
2026-11-15 21:03:11 f=0 t=0:00 [START] map="..." opponent="..." enemy_race=Terran latency_frames=6 battlenet=1 multiplayer=1 config=bwapi-data/AI/stardust_bnet.json safe_bnet_mining=1 ... observer_min_dragoons=2 natural_cannons=1 main_cannons=1 bio_attack_start_scale=0.7 bio_attack_continue_scale=1 bio_attack_resume_scale=0.7 bio_aggression_scale(legacy)=absent bnet_log=1
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
- `bwapi_apm`: `BWAPI::Game::getAPM(false)` (selections excluded), sampled when the `[APM]` line is written. BWAPI computes it with an exponential decay (APMAlert formula, time constant about 0.95 minutes), so it reflects roughly the last minute of play, not a game average and not our own command count. `bwapi_apm_at_end` is the same value at game end; `bwapi_apm_max_sampled` is the maximum over the 2-minute samples (short spikes between samples are not captured). Older logs used `apm`, `avg_apm` and `max_sampled_apm` for these values.
- `res_*`: cumulative counts (since game start) of resource commands (gather / return cargo / resource right-click) for workers in simple gather mode only, i.e. where the Battle.net guards apply. `res_attempts` = calls reaching the guard; `res_issued` = commands BWAPI accepted; `res_blocked_interval` = same command to the same target within `min_command_interval_frames`; `res_blocked_already` = already moving to / waiting at / mining the target, or already returning cargo; `res_blocked_budget` = `max_orders_per_frame` exhausted. Attempts that BWAPI itself rejects are counted in `res_attempts` only. Optimized-mining workers and recovery commands are not counted here.
- `stuck_*`: cumulative stuck-worker recovery counts: `stuck_attempts` (recoveries started), `stuck_reassign` (successful reassignment to a different patch), `stuck_cmd_ok` / `stuck_cmd_failed` (recovery gather accepted / rejected by BWAPI), `stuck_budget_deferred` (postponed because the per-frame budget was exhausted).
- `bio_to_attack` / `bio_to_regroup`: cumulative attack <-> regroup transitions of attack-base squads, counted only while the MidGameBio scales are active (Terran opponent recognized as `MidGameBio` with `pluto_pvt_snipe` on). Repeated decisions in the same state are not counted.
- `[EVENT] detection_stage`: the first-observer request stage changed; the line lists the inputs that drove the decision. `first_observer_complete` is written once, on the first frame a completed observer exists (PvT only). `cannon_issue` is written once per distinct cannon placement problem. `opponent_left` and `surrender` are unchanged.
- The log compiles with `TOURNAMENT_BUILD=ON` (CMake `BNET_LOG`, default `ON`) and can be turned off at runtime with `bnet_log`. The counters are kept even when the log is disabled, but are not written anywhere.
