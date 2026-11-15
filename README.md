# Stardust
AI for StarCraft: Brood War

## Battle.net / Pluto fork

이 포크는 StarCraft: Remastered Battle.net의 지연 환경에서 채취 안정성과 명령량(APM)을 개선하고, Pluto 스타일 테란과 사람 상대의 초반 러시에 대응하기 위한 프로토스 봇입니다. 작업 브랜치는 `bnet-pluto`이며 검토된 변경은 `main`에 병합합니다. 원작은 [bmnielsen/Stardust](https://github.com/bmnielsen/Stardust)입니다.

문서 기준: 2026-11-16 KST, 코드 커밋 [`c9fe8ad`](../../commit/c9fe8add) (PR #8까지). 구현 상세·전체 설정·제한 사항은 [Battle.net 가이드](docs/BNET.md), 기본값은 [JSON 템플릿](config/stardust_bnet.json)을 참고하세요.

### 구현 단계와 최신 변경

| 단계 | 커밋 | 내용 |
| --- | --- | --- |
| Phase 1 | `926fb61` | 안전 채취, 겹친 미네랄 및 정지 일꾼 복구, 지연 기반 명령 가드, JSON 설정, 자동 gg 기본 비활성화 |
| Phase 2 | `bcb06a6` | PvT/PvU 오프닝 고정, 투팩·바이오닉 조기 인식, 옵저버·캐논 요청, MidGameBio 대응 |
| Phase 3 | `a0b7d41` | Win32 Release 빌드·배포 스크립트 및 경량 경기 로그 |

초기 Phase 구현 이후 다음 수정이 적용되었습니다. 아래 설명은 초기 커밋 메시지보다 최신 동작을 우선합니다.

| 후속 커밋 | 변경 사항 |
| --- | --- |
| `6e21907` | 빈손으로 미네랄 이동·대기 중인 일꾼만 정지 감시. 정상 채취·줄서기·자원 운반은 제외. 복구 명령이 성공한 뒤에만 배정 변경하며, 실패·예산 지연을 별도 집계 |
| `0e0af40` | 자원 명령 및 직접 채취 이동 가드를 단순 채취 일꾼으로 한정. 전투·정찰·도주·건설 이동은 원작 동작 유지 |
| `e0ca608` | 첫 옵저버 요청을 urgent / elevated / preventive로 분리. 마인은 즉시 대응하고, 투팩·메카닉·벌처 다수는 완성 드라군 수에 따라 우선순위 조절 |
| `ad9eb12` | MidGameBio 공격 시작·유지·재개 배율을 각각 분리. 기본값 0.7 / 1.0 / 0.7; 기존 bio_aggression_scale은 시작·재개에만 적용 |
| `6d58e8e` | 본진·앞마당 미네랄 라인 캐논 요청 분리. 본진은 앞마당 완성과 무관하게 조건부 요청. 완성·건설 중·예약 캐논을 계산하고 배치 장애 기록 |
| `df69d84` | 자원 명령 가드·정지 복구·바이오닉 교전 전환 카운터, 탐지 단계·첫 옵저버·캐논 배치 이벤트 추가. APM 필드의 의미와 설정 적용 시점 명확화 |
| `659dc1d` (PR #4) | PvT 메카닉 대응: 병력 대비 확장·업그레이드 게이트, 질럿 비율 제한, 공격 배율. 비일꾼 유닛 반복 이동 가드와 자체 명령 카운터(`orders_per_min`) |
| `06c3460` (PR #5) | 빌드 스크립트 Visual Studio 2026(`Visual Studio 18 2026`) 지원, 설치된 최신 VS 자동 선택 |
| `6e6d881` (PR #6) | 게임 시작 시 빌드 ID(`브랜치@커밋`)와 설정 파일 경로 표시(`print_build_info`), `[START]`에 `build=` 기록 |
| `5805b1b` (PR #7) | Move·Stop 과다 감소(경로 웨이포인트 유지, 군대·옵저버 재전송 간격, 스테이시스·언로드 반복 방지, 갇힌 일꾼 대기 증가). 메카닉·벌처 위협 시 앞마당 캐논 2기, 세 번째 이후 확장마다 캐논 1기 |
| `c9fe8ad` (PR #8) | 저그전 오프닝 고정(`forced_pvz_opening`), 저글링 올인 조기 확정, 선풀 시 포지 더블 입구 캐논 3기·최소 질럿, 올인 중 프로브 생산 제한 |

종족별 적용 범위: 채취 안정화와 이동 명령 감소(PR #7)는 상대 종족과 무관하게 지연이 4프레임을 넘는 환경(배틀넷)에서 동작합니다. 캐논 보강과 메카닉 대응은 테란전, 올인 대응은 저그전에만 적용됩니다. 프로토스전은 원작 로직을 사용합니다.

### Windows 빌드와 배포

필요 도구: Visual Studio 2022 또는 2026의 Desktop development with C++ 워크로드(MSVC v143/v145, Windows SDK, Windows용 C++ CMake 도구), Windows PowerShell 5.1. CMake는 VS 2022에서 3.24 이상, VS 2026에서 4.2 이상이 필요하며 Visual Studio에 포함된 CMake를 자동으로 찾습니다.

저장소 루트에서 실행합니다.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-bnet.ps1
```

[빌드 스크립트](scripts/build-bnet.ps1)는 설치된 Visual Studio 중 최신 버전에 맞춰 `-G "Visual Studio 18 2026"` 또는 `-G "Visual Studio 17 2022"`와 `-A Win32 -DTOURNAMENT_BUILD=ON`으로 구성하고(`-Generator`로 직접 지정 가능), `Stardust` 타겟만 Release로 빌드합니다. `windbg` 타겟은 제외됩니다.

- 출력: `dist/bwapi-data/AI/Stardust.dll`
- 운영 설정: `dist/bwapi-data/AI/stardust_bnet.json`
- 기본값 참조: `dist/bwapi-data/AI/stardust_bnet.default.json`
- 기존 운영 설정은 보존하며 `-OverwriteConfig`를 지정했을 때만 덮어씁니다. 기본값 참조 파일은 매번 갱신합니다.
- 추가 옵션: `-Clean`, `-VsHuman`, `-NoBnetLog`, `-BuildDir`, `-DistDir`

빌드 후 `dist/bwapi-data` 내용을 사용 중인 StarCraft의 `bwapi-data`에 배치합니다. 새 설정 키가 추가된 버전으로 갱신할 때는 `-OverwriteConfig`로 운영 설정을 새 기본값으로 바꾸거나 `stardust_bnet.default.json`과 비교해 옮기세요. 기존 파일에 남아 있는 값(예: 예전 `move_resend_tolerance_px: 24`)은 새 기본값보다 우선합니다. 이 스크립트는 DLL·설정 빌드/배포용이며 Remastered용 BWAPI 실행 환경이나 브리지 설치를 대신하지 않습니다.

### 주요 설정

설정 검색 순서: `bwapi-data/AI/stardust_bnet.json` → `bwapi-data/read/stardust_bnet.json` → `bwapi-data/write/stardust_bnet.json`. 경로는 게임 프로세스의 작업 폴더 기준이며, 파일이 없으면 안전한 기본값을 사용합니다.

설정 파일을 어디에 둘지 모르겠다면 `stardust_bnet.log`가 생기는 `bwapi-data/write/` 폴더에 넣으면 됩니다(`AI` 폴더를 새로 만들 필요 없음). `[START]` 줄의 `config=` 값이 `defaults`이면 파일을 찾지 못한 것이고, 경로가 찍히면 그 파일을 읽은 것입니다.

설정은 경기 시작 시 한 번 읽습니다. 재컴파일은 필요 없지만 경기 중 수정은 다음 경기부터 적용됩니다. 핫 리로드는 지원하지 않습니다.

| 설정 | 기본값 | 의미 |
| --- | --- | --- |
| `safe_bnet_mining` | `true` | MiningOptimizationV2의 프레임 단위 최적화·재전송을 우회하고 단순 채취 사용 |
| `stacked_mineral_fallback` | `true` | 겹친 미네랄 대응 및 정지 일꾼 복구 |
| `stuck_worker_frames` | `48` | 정지 감시 임계값 및 일꾼별 복구 시도 간격 |
| `max_orders_per_frame` | `0` | 가드 대상 자원 명령의 프레임당 상한; 0은 무제한이며 전 유닛 APM 제한이 아님 |
| `min_command_interval_frames` | `-1` | 자동: 지연이 4프레임 초과이면 동일 명령 간격을 latencyFrames + 2로 설정 |
| `enable_auto_gg` | `false` | 자동 항복 비활성화 |
| `pluto_pvt_snipe` | `true` | Phase 2 기능 스위치 |
| `forced_pvt_opening` | `EarlyGameDefense` | 1게이트 코어 드라군 기반 PvT 오프닝 |
| `forced_random_opening` | `OneGateCore` | 상대 랜덤일 때 PvU 오프닝 고정 |
| `early_observer_priority` | `true` | 첫 옵저버의 단계별 생산 요청 |
| `observer_min_dragoons` | `2` | 투팩·메카닉·벌처 다수 대응 시 옵저버 우선순위를 올릴 완성 드라군 수; 마인 긴급 대응에는 적용하지 않음 |
| `main_cannons` / `natural_cannons` | `1` / `1` | 본진 / 앞마당 미네랄 라인 조건부 캐논 요청 |
| `mech_natural_cannons` | `2` | 메카닉·벌처·마인 위협 시 앞마당 캐논 목표 |
| `expansion_cannons` | `1` | 위협 시 세 번째 이후 모든 확장 미네랄 라인 캐논 수 (0-2) |
| `cannon_threat_priority` | `true` | 위협 시 기지별 첫 캐논과 파일런을 병력 생산보다 먼저 요청 |
| `combat_move_guard` / `move_resend_tolerance_px` | `true` / `64` | 비일꾼 유닛의 반복 이동 명령 생략과 같은 목표로 볼 거리 |
| `combat_move_interval_frames` / `observer_move_interval_frames` | `12` / `24` | 반복 이동 명령 최소 간격(군대 / 옵저버) |
| `move_guard_min_remaining_px` | `48` | 목표에 이만큼 가까워지면 생략하지 않음(멈칫 방지) |
| `grid_waypoint_hold` / `grid_waypoint_refresh_px` | `true` / `64` | 경로 웨이포인트를 타일마다 바꾸지 않고 가까워졌을 때만 갱신 |
| `spell_resend_interval_frames` | `24` | 같은 스테이시스·언로드 명령 재전송 간격 |
| `unstick_backoff_max_frames` | `192` | 갇힌 일꾼의 Stop·Move 반복 대기 상한 |
| `micro_latency_frames` | `-1` | 유닛 컨트롤에 쓰는 지연 프레임. 자동은 경기 중 본 최대 지연(4 초과일 때), 0은 원작(BWAPI 보고값), 1-12는 고정 |
| `in_flight_move_guard` | `true` | 직전 이동이 아직 반영되기 전(지연 프레임 이내)에는 같은 목표 근처로 이동을 다시 보내지 않음 |
| `pvt_combat_commit` | `true` | 테란전 공격·후퇴 전환 안정화 스위치 |
| `attack_commit_frames` / `commit_max_loss` | `48` / `0.6` | 공격 시작 후 48프레임은 시뮬레이션이 60% 이상 손실을 예측하지 않으면 후퇴하지 않음 |
| `attack_abort_stable_frames` | `24` | 후퇴 판단이 이 프레임 동안 유지돼야 공격 중단(원작 6) |
| `regroup_commit_frames` | `0` | 도망 중이 아닌 재집결을 최소 유지할 프레임; 0은 끔 |
| `flee_guard` / `flee_guard_max_loss` / `flee_guard_speed_ratio` | `true` / `0.85` / `1.25` | 붙어 있는 적 지상군 절반 이상이 우리보다 빠르면(벌처) 도망 대신 교전; 시즈 탱크 사거리 안이거나 85% 이상 손실 예측이면 도망 |
| `invalid_move_guard` | `true` | 잘못된 좌표(맵 우하단 (4095,4095))로의 이동 명령 차단 |
| `worker_flee_vultures` / `worker_flee_margin_px` / `worker_flee_hold_frames` | `true` / `96` / `48` | 미네랄 라인 캐논이 없을 때 일꾼이 벌처와 싸우지 않고 다른 기지(본진↔앞마당)나 반대 방향으로 피신, 위협이 사라진 뒤 48프레임 유지 |
| `probe_throttle_under_vultures` | `true` | 우리 기지에 벌처가 있는 동안 프로브 생산을 병력·캐논·옵저버 뒤로 미룸(프로브 8기 이상일 때) |
| `blind_main_cannon_frame` | `6000` | 이 프레임(약 4:10)부터 위협이 안 보여도 본진 미네랄 라인 캐논 요청(초반 바이오·러시 판별 시 제외); 0은 끔 |
| `home_guard_dragoons` / `home_guard_until_frame` | `1` / `14000` | 첫 옵저버 전까지 본진에 드라군 수비 유지 |
| `observer_emergency_priority` | `true` | 마인 발견(또는 드랍 위험 + 옵저버터리 완성) 시 첫 옵저버를 프로브보다 먼저 생산 |
| `mine_hold_until_observer` / `mine_hold_max_frame` | `true` / `12000` | 마인 발견 후 첫 옵저버 완성 전까지 본진 수비 유지 |
| `pluto_rescout_frame` / `pluto_rescout_max_frames` | `4800` / `1440` | 약 3:20에 프로브로 적 본진·앞마당 재정찰; 0은 끔 |
| `drop_risk_detection` | `true` | 스타포트·컨트롤 타워·드랍십을 보면 드랍 위험으로 보고 본진 캐논·옵저버 우선순위 상향 |
| `midgame_strategy_hold_frames` | `480` | MidGameMech/Bio/BioMech 사이 전환은 직전 전환 후 480프레임(20초)이 지나야 반영 |
| `forced_pvz_opening` | `FFEDragoons` | 저그전 오프닝 고정(`FFEDragoons` / `SairSpeedlot` / `EarlyGameDefense`); 빈 값은 UCB1 |
| `pvz_antiallin_fix` | `true` | 저그전 저글링 올인 대응 스위치 |
| `pvz_allin_base_lings` / `pvz_allin_seen_lings` | `6` / `12` | 본진·앞마당에 보인 저글링 / 살아 있는 저글링 수로 올인 조기 확정 |
| `pvz_pool_first_cannons` | `3` | 선풀 판별 시 포지 더블 입구 캐논 목표 |
| `pvz_pool_first_zealots` / `pvz_allin_zealots` | `2` / `4` | 선풀 / 올인 시 최소 질럿 수(올인 시 프로브보다 먼저) |
| `pvz_allin_probe_cap` | `18` | 올인 중 이 수를 넘는 프로브 생산을 질럿 뒤로 미룸; 0은 끔 |
| `bio_attack_start_scale` | `0.7` | MidGameBio 상대 공격 시작 배율 |
| `bio_attack_continue_scale` | `1.0` | 공격 유지 배율; 기본값은 원작 유지 판단 |
| `bio_attack_resume_scale` | `0.7` | 재집결 후 공격 재개 배율 |
| `print_build_info` | `true` | 게임 시작 시 빌드 ID(브랜치@커밋)와 설정 파일 경로를 내 화면에만 표시 (printf + 처음 30초 화면 그리기). 배틀넷 브리지에서는 화면에 보이지 않고 브리지 로그에 기록될 수 있음 |
| `bnet_log` | `true` | 경량 경기 로그 활성화 |

순정 PvT UCB1 후보는 이미 `EarlyGameDefense` 하나입니다. 오프닝 고정의 주요 차이는 상대 이름별 이력과 무관한 명시적 선택 및 랜덤 상대의 PvU 탐색 우회입니다. 캐논은 배치 가능 위치·전략 조건·생산 우선순위에 따른 요청이며 건설 보장이 아닙니다.

호환성: 기존 `bio_aggression_scale`은 시작·재개에만 적용되고 새 키가 우선합니다. `main_cannons`가 없는 기존 설정은 `natural_cannons > 0`이면 1, 아니면 0으로 해석됩니다.

### 경기 로그와 검증 상태

로그는 `bwapi-data/write/stardust_bnet.log`에 누적됩니다. CMake `BNET_LOG=ON`이면 `TOURNAMENT_BUILD=ON`에서도 동작하며, `-NoBnetLog`는 로그를 빌드에서 제외합니다.

- `[START]`: 빌드 ID, 맵, 상대, 지연 프레임, 설정 파일 경로(`config=`), 실제 적용 설정
- `[ENEMY]` / `[OURS]`: 적·아군 전략 전환
- `[EVENT]`: 탐지 요청 단계 전환, 첫 옵저버 완성, 캐논 계획(`cannon_plan`)·배치 문제(`cannon_issue`), 메카닉 확장 게이트, 저그 올인 확정(`pvz_allin_detect`)과 저그전 수비 목표(`pvz_defense`) 등
- `[APM]`: 2880프레임마다(게임 시간 약 2분) 자체 명령 수(`orders_per_min`, 유닛 종류별 누적), 생략된 이동(`moves_skipped`, `grid_holds`), 반복 명령 생략(`orders_repeat_skipped`), 갇힌 일꾼 대기(`unstick_backoffs`), 지연·명령 간격(`latency_max`, `cmd_interval`)과 기타 누적 카운터
- `[END]`: 승패, 최종 전략, 종료 APM 및 누적 카운터

명령량은 `orders_per_min`으로 확인하세요. `bwapi_apm`은 Remastered에서 0으로 기록되며, 원래 BWAPI의 감쇠된 최근 APM으로 경기 전체 평균이나 자체 명령 횟수가 아닙니다. `bwapi_apm_at_end`는 종료 시 값, `bwapi_apm_max_sampled`는 주기 표본 중 최대값으로 표본 사이 순간 피크는 놓칠 수 있습니다. `res_*`는 단순 채취 일꾼의 가드 대상 자원 명령만 집계하고, 복구 명령은 `stuck_*`로 별도 집계합니다. `bio_to_attack` / `bio_to_regroup`는 MidGameBio 배율이 적용되는 동안의 실제 상태 전환 횟수입니다. `attack_commit_holds`, `flee_guard_fights`, `dragoon_unsticks`(공격 취소로 멈춘 드라군 Stop), `micro_latency`(실제 적용 지연)는 테란전 교전 수정의 동작 확인용입니다. `invalid_moves`(차단된 잘못된 이동), `worker_flees`(벌처를 피해 피신한 일꾼)는 벌처 대응 수정의 확인용이며, `[EVENT] previous_game_unfinished`는 직전 경기가 `[END]` 없이 끝났음(크래시·강제 종료·연결 끊김)을 뜻합니다.

코드·문서·빌드 스크립트가 존재하는 것과 실행 검증은 구분합니다.

- 확인됨: [독립 정책 테스트](tools/bnet-policy-test) 234개 검사와 설정 로더 테스트 통과(Linux g++), 변경 소스의 g++ 문법 검사. 사용자 Windows 환경에서 `bnet-pluto@9fabc694` 빌드가 배틀넷 저그전에서 실행되어 새 기본 설정으로 동작하고(`config=defaults`) 2분 시점 `orders_per_min`이 약 100으로 기록됨.
- 미확인: 벌처 드랍·마인 대응(드랍 위험 인식, 일꾼 피신, 본진 캐논, 홈 가드, 옵저버 긴급 생산, 재정찰)의 Windows 빌드와 실전 효과, PR #8(저그전 올인 대응)의 Windows 빌드와 실전 효과, 중후반 대규모 교전의 명령량, 이동 명령 생략이 드라군 컨트롤에 주는 영향. 승률 향상은 보증하지 않습니다. 기본 전술 수치는 실전 조정의 출발점입니다. 하이템플러 스톰 마이크로는 추가되지 않았고, 경로 추적의 후속 waypoint 이동은 채취 이동 가드 대상이 아닙니다.

## About
Stardust is written in C++ and uses [BWAPI](https://github.com/bwapi/bwapi) to play 1v1 Melee games of StarCraft: Brood War. Stardust plays as the Protoss race.

To see Stardust play against other bots, [BASIL ladder](https://basil.bytekeeper.org/).

Stardust is mostly optimized for AI vs. AI tournaments, but if you want to play against it, check out [SCHNAIL](https://schnail.com/).

## Libraries

Stardust uses [BWEM](http://bwem.sourceforge.net/) for terrain analysis and a modified version of [FAP](https://github.com/N00byEdge/FAP) for combat simulation.

## License

The license is MIT with an added condition that forks may not be submitted to StarCraft AI competitions without written consent of the author. The rationale behind this is to prevent a flood of minimally-altered forks popping up in tournaments, which happened with my previous bot Locutus in 2018.

So if you are interested in forking Stardust and submitting it to a tournament, create an issue here and we'll discuss it. I will likely accept any Zerg or Terran forks, but be much more strict with Protoss forks. 
