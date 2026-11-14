# Stardust for StarCraft: Remastered Battle.net (Pluto Terran snipe build)

This fork adds runtime switches so the bot can be tuned on Battle.net without recompiling.

## Config file

The bot reads the first file found among:

1. `bwapi-data/AI/stardust_bnet.json`
2. `bwapi-data/read/stardust_bnet.json`
3. `bwapi-data/write/stardust_bnet.json`

If no file is found, or a key is missing or has the wrong type, the default below is used.
Comments (`//`, `/* */`) are allowed. The template is `config/stardust_bnet.json`.

## Phase 1: Battle.net stability

| Key | Default | Effect |
| --- | --- | --- |
| `safe_bnet_mining` | `true` | Bypasses the MiningOptimizationV2 optimized initial split, start-of-mining and return-of-resource path optimizations (all frame-exact resends). Workers use plain `gather()` / `returnCargo()` only, and are never re-ordered while already moving to, waiting at, or mining their patch. Resource command guards (below) are also active only in this mode. |
| `stacked_mineral_fallback` | `true` | Detects bases with stacked/overlapping mineral patches (same tile or centers < 24px apart) and uses simple gather mode there even when `safe_bnet_mining` is off. Also force-reassigns any gathering worker with no position/cargo change for `stuck_worker_frames` (doubled while in `WaitForMinerals`) to another patch, and switches that worker to simple gather mode for the rest of the game. |
| `stuck_worker_frames` | `48` | Stuck detection threshold (clamped 12-1000). |
| `max_orders_per_frame` | `0` | Cap on worker resource orders (gather / return cargo / resource right-click) per frame. `0` = unlimited. |
| `min_command_interval_frames` | `-1` | Minimum frames between identical commands to the same unit. `-1` = auto: `latencyFrames + 2` when `getLatencyFrames() > 4`, otherwise off. Applies to repeated same-target move orders always, and to same-target gather / return / resource right-click in safe mining mode. |
| `enable_auto_gg` | `false` | Enables the surrender logic (send "gg" and leave). Previously this was always on in `VS_HUMAN` builds. The surrender condition itself is unchanged and strict: no probes, no nexus or < 50 minerals, no mobile combat unit, and an enemy combat unit at one of our bases. |

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
| `early_observer_priority` | `true` | Observer (the producer adds robo + observatory) is requested at `PRIORITY_SPECIALTEAMS` as soon as a vulture, machine shop, spider mine / mine research, or `TwoFactory` / mech strategy is seen, and otherwise at normal priority once dragoon range starts or after frame 6500, instead of upstream's "two nexuses or frame 14000". Not during `AntiMarineRush`. |
| `natural_cannons` | `1` | Once our natural nexus is complete, guarantee this many cannons (0-2) in the natural mineral line, plus 1 in the main mineral line against `TwoFactory` / mech / 3+ vultures. |
| `bio_aggression_scale` | `0.7` | Multiplies attack aggression in `AttackBaseSquad` (start and continue attack) when the enemy is recognized as `MidGameBio`. Lower = more regrouping, fewer trickle attacks (0.1-2.0). |

Recognizer changes (`PvTEnemyStrategyRecognizer.cpp`, only when `pluto_pvt_snipe` is on):

- `TwoFactory` (2FactVult) is now also recognized by: early machine shop (< frame 6000) + 2 vultures (< 6500), 3 vultures (< 7000), or a spider mine (< 7000), and is re-checked from `Normal` / `WallIn` until frame 9000. Upstream only checked "2 factories" while still `Unknown`, which usually resolved to `Normal` at frame 4000 before the second factory was scouted.
- Early bio (fast second barracks with academy or gas, academy + 4 marines by frame 5500, or an early medic) maps to `MarinePressure` and is re-checked from `Normal` / `WallIn` until frame 7500.
- Our strategy: `Normal` falls back to `Defensive` once (before two nexuses, before frame 10000) when `TwoFactory` or `MarinePressure` is recognized late.

Mid-game vs bio: keep at least `max(4, dragoons)` zealots with leg enhancements started at 2 zealots, and allow arbiters vs `MidGameBio` / `MidGameBioMech` under upstream's conditions (3 nexuses, 20+ army). High templar storm is not added because Stardust has no high templar micro.

## Phase 3: Build script and summary log

- `scripts/build-bnet.ps1`: configures with `-G "Visual Studio 17 2022" -A Win32 -DTOURNAMENT_BUILD=ON` (which also skips the `windbg` target), builds only the `Stardust` target in Release, and copies `Stardust.dll` plus `config/stardust_bnet.json` to `dist/bwapi-data/AI/`. An existing `dist/.../stardust_bnet.json` is kept unless `-OverwriteConfig`; the defaults are always written to `stardust_bnet.default.json`. Options: `-Clean`, `-VsHuman`, `-NoBnetLog`, `-BuildDir`, `-DistDir`.
- CMake option `BNET_LOG` (default `ON`) compiles the summary log in, independent of `LOGGING_ENABLED` (which `TOURNAMENT_BUILD` turns off). Runtime switch: `bnet_log`.
- Log file `bwapi-data/write/stardust_bnet.log`, appended, one line per event, flushed immediately:

```
2026-11-15 21:03:11 f=0 t=0:00 [START] map="..." opponent="..." enemy_race=Terran latency_frames=6 battlenet=1 multiplayer=1 config=bwapi-data/AI/stardust_bnet.json safe_bnet_mining=1 ...
2026-11-15 21:03:11 f=0 t=0:00 [OURS] from=- to=EarlyGameDefense
2026-11-15 21:06:02 f=4100 t=2:52 [ENEMY] from=Unknown to=TwoFactory
2026-11-15 21:07:14 f=5760 t=4:01 [APM] apm=142 latency_frames=6 supply=31 stuck_reassign=0
2026-11-15 21:20:40 f=24500 t=17:09 [END] result=WIN avg_apm=160 max_sampled_apm=171 final_enemy_strategy=MidGameMech final_our_strategy=MidGame stuck_reassign=1
```

`[APM]` is written every 2 game minutes using `BWAPI::Game::getAPM(false)`; `[EVENT]` records `opponent_left` and `surrender`.
