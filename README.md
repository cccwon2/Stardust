# Stardust
AI for StarCraft: Brood War

## Battle.net / Pluto fork

이 포크는 StarCraft: Remastered Battle.net의 지연 환경에서 채취 안정성과 명령량(APM)을 개선하고, Pluto 스타일 테란과 사람 상대의 초반 러시에 대응하기 위한 프로토스 봇입니다. 작업 브랜치는 `bnet-pluto`이며 검토된 변경은 `main`에 병합합니다. 원작은 [bmnielsen/Stardust](https://github.com/bmnielsen/Stardust)입니다.

문서 기준: 2026-11-15 KST, 코드 커밋 [`de0eba2e`](../../commit/de0eba2e6448f3c4dda40f4e8e4b437ceed68bca) (PR #18 저그전 초반 캐논·가스). 구현 상세·전체 설정·제한 사항은 [Battle.net 가이드](docs/BNET.md), 기본값은 [JSON 템플릿](config/stardust_bnet.json)을 참고하세요.

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
| `6b592e2` (PR #9, #10) | README 갱신, `bnet-pluto`를 `main`에 동기화(PR #4-#9) |
| `85c3856` (PR #11) | 테란전 교전: 실제 지연(최대 관측값) 기반 마이크로, 공격 시작 후 유지(`attack_commit_frames`), 후퇴 판단 안정화, 빠른 추격자(벌처) 상대 도주 대신 교전(`flee_guard`) |
| `b9bc2d4` (PR #12) | 본진 미네랄이 겹친 맵(OdysseyRE 2.0)에서 `onStart` 크래시 수정(BWEM 시작 기지 대체 매칭) |
| `95cac3e` (PR #13) | 한글 등 비 ASCII 상대 이름에서 `onStart` 예외 수정(상대 파일 키를 ASCII로 변환) |
| `1b31cee` (PR #14) | 테란전 벌처 드랍·마인 대응: 잘못된 좌표 이동 차단, 일꾼 피신, 본진 캐논, 홈 가드, 옵저버 긴급 생산, 3:20 재정찰, 드랍 위험 인식 |
| `a359126` (PR #15) | 프로토스전 리버·셔틀 대응: 가스 부족·자원 누적 시 앞마당 확장, 위협 없는 건설 중 앞마당 유지, 적 로보 확인 시 옵저버, 드랍 위협 시 홈 가드·본진 캐논. 도착한 유닛 반복 이동 억제(`arrived_move_hold_frames`), BWAPI 거부 자원 명령 집계·백오프, 보급 급감·적 테크·건물 취소 로그 |
| `1ba0cc7` (PR #16) | 겹친 미네랄 채취 수정(OdysseyRE 2.0): 미네랄을 타일 대신 유닛 ID별로 등록하고 기지에 중복 없이 연결, 넥서스에 붙은 미네랄(거리 0)에도 두 번째 일꾼 배정, 미네랄을 받지 못한 일꾼을 넥서스로 반복 이동시키지 않음. `[MINING]` 진단 로그와 `mining_*` 카운터 |
| `fffb996` (PR #17) | 테란전 병력·확장 방어(Backrooms 1.1): 기지 근처에 기억된 스파이더 마인에 옵저버를 보내 제거, 본진·앞마당 근처에서 20초 넘게 재집결만 하는 후방 무리를 본대로 합류, 확장 위치를 우리 기지 가까운 곳 우선으로 변경(먼 구석 기지는 마지막), 메카닉 상대 3번째 이후 확장에 드라군 상주, 공격받는 확장에 본대가 충분히 강하면 본대 투입, 확장 캐논을 넥서스 착공 시 요청(기본 2기)하고 잃은 자리 재건축 대기. `[ARMY]`·`[DEFENSE]` 로그와 관련 카운터 |
| `0a3f9a84` | 테란전 벌처 런바이(KnockOut 1.4, `908059a0`): 탐지 미완만으로 홈 가드 드라군을 전부 해제하지 않음(보이는 벌처 등이 있으면 유지), 수비 스쿼드가 벌처 존재 시 미네랄 라인으로 이동, 일꾼 도주 좌표를 맵 안 walkable로 고정해 `(4095,4095)`·`invalid_moves` 폭주 완화 |
| `de0eba2e` (PR #18) | 저그전 초반(Fighting Spirit 1.4): 포지가 캐논을 지을 수 있으면 첫 벽 캐논을 바로 요청, 안 보이거나 완성된 채로 처음 본 풀은 6풀로 간주, 착공 전 벽 캐논은 프로브 1기만 보냄, 받아들여진 건설 명령 뒤에는 건물이 생길 때까지 이동을 보내지 않음, 남는 가스로 사이버네틱스 코어·드라군 사거리·스타게이트·시타델 오브 아둔·템플러 아카이브를 순서대로 요청 |

종족별 적용 범위: 채취 안정화와 이동 명령 감소(PR #7, #15), 겹친 미네랄 채취 수정(PR #16), `[ARMY]` 로그·마인 제거·후방 무리 합류(PR #17)는 상대 종족과 무관하게 지연이 4프레임을 넘는 환경(배틀넷)에서 동작합니다(`arrived_move_hold_frames`, 잘못된 좌표 차단, 자원 명령 백오프, 미네랄 ID 등록·배정 수정은 지연과 무관하게 동작). 받아들여진 건설 명령 뒤 이동을 보내지 않는 동작(PR #18)은 종족·지연과 무관합니다. 캐논 보강·메카닉·교전·벌처 대응과 확장 선택·확장 수비·본대 투입(PR #17)은 테란전, 올인 대응과 초반 벽 캐논·잉여 가스 테크(PR #18)는 저그전, 리버·셔틀 대응과 가스 부족 확장(PR #15)은 프로토스전에만 적용됩니다. 그 밖의 프로토스전 운영은 원작 로직입니다.

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
| `stacked_mineral_identity_fix` | `true` | 같은 타일에 겹친 미네랄을 유닛 ID별 자원으로 등록하고 기지에 한 번씩 연결; `false`는 원작(타일당 자원 1개) |
| `mineral_assignment_fix` | `true` | 넥서스에 붙은 미네랄(거리 0)도 배정 후보로 선택하고 동점은 ID 순; 미네랄을 받지 못한 단순 채취 일꾼을 넥서스로 반복 이동시키지 않음 |
| `mining_assignment_log` | `true` | `[MINING]` 진단 로그(자원 등록, 기지 미네랄, 0:10·0:30·1:00 일꾼 배정 스냅샷, 배정 실패) |
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
| `expansion_cannons` | `2` | 위협 시 세 번째 이후 모든 확장 미네랄 라인 캐논 수 (0-2; PR #17 전 기본값 1) |
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
| `pvz_pool_first_cannons` | `2` | 선풀 판별 시 포지 더블 입구 캐논 목표. 3은 과하고 상한은 2(맵 무관) |
| `pvz_pool_first_zealots` / `pvz_allin_zealots` | `2` / `4` | 선풀 / 올인 시 최소 질럿 수(올인 시 프로브보다 먼저) |
| `pvz_allin_probe_cap` | `18` | 올인 중 이 수를 넘는 프로브 생산을 질럿 뒤로 미룸; 0은 끔 |
| `bio_attack_start_scale` | `0.7` | MidGameBio 상대 공격 시작 배율 |
| `bio_attack_continue_scale` | `1.0` | 공격 유지 배율; 기본값은 원작 유지 판단 |
| `bio_attack_resume_scale` | `0.7` | 재집결 후 공격 재개 배율 |
| `pvp_robo_fix` | `true` | 프로토스전 리버·셔틀 대응 스위치 |
| `pvp_natural_fix` | `true` | 앞마당에 적 병력(드라군 2기 가치 이상)이 없으면 건설 중인 앞마당을 취소하지 않고, 가스 부족(미네랄 500 초과·가스 100 미만) 시 공격 부대 조건 없이 앞마당 확장 |
| `pvp_reexpand_frame` / `pvp_reexpand_minerals` | `12000` / `400` | 이 프레임(약 8:20)부터 미네랄이 쌓이면 앞마당 확장·재확장; 0은 끔 |
| `pvp_robo_observer` | `true` | 적 로보틱스·서포트 베이·옵저버터리·셔틀·리버를 보면 두 번째 가스를 기다리지 않고 옵저버 1기 요청 |
| `pvp_home_guard_dragoons` | `2` | 서포트 베이·셔틀·리버를 보면 본진에 유닛 약 2기 수비 유지; 0은 끔 |
| `pvp_drop_cannons` | `1` | 같은 조건에서 본진 미네랄 라인 캐논 수(완성된 앞마당에는 원작 헬퍼대로 2기); 0은 끔 |
| `arrived_move_hold_frames` / `arrived_move_tolerance_px` | `48` / `16` | 목표에 이미 도착한(48px 이내) 비일꾼 유닛에게 16px 이내의 같은 이동을 48프레임 동안 다시 보내지 않음; 0은 끔 |
| `resource_fail_backoff_frames` | `24` | BWAPI가 자원 명령을 거부한 일꾼에게 이 프레임 동안 자원 명령을 보내지 않음; 0은 끔 |
| `supply_drop_event` | `20` | 30초 안에 보급이 이만큼 줄면 `[EVENT] supply_drop` 기록; 0은 끔 |
| `army_state_log` / `army_state_interval_frames` | `true` / `720` | 30초마다 공격 부대 무리별 `[ARMY]` 스냅샷, 확장 수비 판단 `[DEFENSE]` 기록 |
| `home_mine_clear` / `home_mine_clear_radius_px` | `true` / `960` | 우리 기지 960px 안에 기억된 매설 마인이 있으면 공격 부대가 탐지를 요청해 옵저버 1기가 가고 드라군이 제거 |
| `rear_cluster_unstick_frames` / `rear_cluster_home_radius_px` | `480` / `1280` | 본진·앞마당 1280px 안의 후방 무리가 마인·미탐지 유닛 말고 위협 없이 480프레임(20초) 넘게 재집결 중이면 본대로 합류. 이 범위에서는 72프레임 증원 대기 규칙 생략; 0은 끔 |
| `expansion_select_fix` / `expansion_max_distance_px` | `true` / `2500` | 테란전 우리 확장 위치 점수를 적과의 거리 - 2 × 우리와의 거리로 바꾸고, 모든 우리 기지에서 지상 2500px를 넘는 기지는 마지막 순위; 거리 0은 제한 없음 |
| `expansion_guard_dragoons` | `2` | 메카닉·벌처 위협 시 3번째 이후 확장마다 적이 없어도 유닛 약 2기 수비 유지(원작 수비 조건 적용); 0은 끔 |
| `expansion_defense_redirect` / `defense_redirect_ratio` / `defense_redirect_min_dragoons` | `true` / `1.2` / `3` | 3번째 이후 확장에 드라군 3기 가치 이상의 적이 있고 본대 가치가 그 1.2배 이상이면 본대를 그 확장으로 보냄. 적이 사라지고 480프레임 뒤, 최대 2880프레임, 또는 기지를 잃으면 복귀 |
| `expansion_cannon_at_nexus_start` | `true` | 확장 캐논·파일런을 넥서스 완성이 아니라 착공 시 요청 |
| `cannon_rebuild_backoff_frames` | `480` | 캐논(건설 중 포함)을 잃은 자리는 480프레임 동안 다시 요청하지 않음; 0은 끔 |
| `print_build_info` | `true` | 게임 시작 시 빌드 ID(브랜치@커밋)와 설정 파일 경로를 내 화면에만 표시 (printf + 처음 30초 화면 그리기). 배틀넷 브리지에서는 화면에 보이지 않고 브리지 로그에 기록될 수 있음 |
| `bnet_log` | `true` | 경량 경기 로그 활성화 |

순정 PvT UCB1 후보는 이미 `EarlyGameDefense` 하나입니다. 오프닝 고정의 주요 차이는 상대 이름별 이력과 무관한 명시적 선택 및 랜덤 상대의 PvU 탐색 우회입니다. 캐논은 배치 가능 위치·전략 조건·생산 우선순위에 따른 요청이며 건설 보장이 아닙니다.

저그전 아래 행동은 맵 이름과 무관하게 모든 맵에 적용됩니다. 선풀 입구 캐논은 2기가 상한이고, 풀이 보이면 앞마당 넥서스보다 게이트·질럿을 먼저 요청합니다. 히드라 덴 또는 히드라가 확인되고 스파이어·뮤탈리스크는 아니면, 넥서스 미네랄 캐논은 더 두지 않고 입구(벽 또는 초크)에 캐논을 더 둡니다. 같은 조건에서 가스 건물은 시타델 → 시타델이 끝나는 즉시 질럿 다리 속도 → 템플러 아카이브 → 스톰 → 하이템플러 순서이고, 스타게이트를 그 앞에 두지 않습니다. 질럿 비율은 히드라전에서 질럿+드라군 병력의 절반 이상입니다. 시작 정찰은 프로브 1기이고, 이후 추가 정찰은 질럿입니다. 포지가 끝나면 +1 지상 무기를 추가 게이트보다 먼저 요청합니다. 히드라(럴커 포함)를 앞에 둔 2–3 드라군은 들어가지 않고, 질럿+드라군(스톰이 있으면 하이템플러)이 그 수보다 작으면 싸우지 않습니다.

호환성: 기존 `bio_aggression_scale`은 시작·재개에만 적용되고 새 키가 우선합니다. `main_cannons`가 없는 기존 설정은 `natural_cannons > 0`이면 1, 아니면 0으로 해석됩니다.

### 경기 로그와 검증 상태

로그는 `bwapi-data/write/stardust_bnet.log`에 누적됩니다. CMake `BNET_LOG=ON`이면 `TOURNAMENT_BUILD=ON`에서도 동작하며, `-NoBnetLog`는 로그를 빌드에서 제외합니다.

- `[START]`: 빌드 ID, 맵, 상대, 지연 프레임, 설정 파일 경로(`config=`), 실제 적용 설정
- `[ENEMY]` / `[OURS]`: 적·아군 전략 전환
- `[EVENT]`: 탐지 요청 단계 전환, 첫 옵저버 완성, 캐논 계획(`cannon_plan`)·배치 문제(`cannon_issue`), 메카닉 확장 게이트, 저그 올인 확정(`pvz_allin_detect`)과 저그전 수비 목표(`pvz_defense`), 적 테크 첫 확인(`enemy_tech_seen`), 건물 취소(`build_cancel`), 보급 급감(`supply_drop`), 거부된 자원 명령(`res_failed`), 프로토스전 확장·드랍 대응(`pvp_natural_take`, `pvp_drop_threat`, `pvp_robo_observer`), 기지 근처 마인 제거 요청(`home_mine_clear`)·후방 무리 합류(`rear_unstick`)·본대 투입(`defense_redirect`)·확장 수비(`expansion_guard`)·캐논 재건축 대기(`cannon_backoff`) 등
- `[MINING]`: 경기 시작 시 자원 등록(`registry`: 실제 미네랄 수·등록 수·병합으로 빠진 수 `dropped`·겹친 타일 수)과 본진·앞마당 미네랄 연결(`base`: `patches`·`unique`·패치별 배정 일꾼 `load`), 0:10·0:30·1:00 본진 일꾼 스냅샷(`w<일꾼>=<배정 미네랄 ID>/<실제 명령 대상 ID>/<명령>`), 배정 실패(`assign_fail reason=all_full|no_patches`)와 장시간 미배정 일꾼(`unassigned_miner`)
- `[ARMY]` / `[DEFENSE]`: 무리별 위치·활동 상태·지속 시간·주변 적·실제 위협·마인·미탐지 유닛·옵저버 수(`army_state_interval_frames`마다), 3번째 이후 확장의 수비 판단(`guard`/`defend`/`idle`/`abandon_too_strong`) 변화, 본대 투입을 하지 못한 경우(`redirect_skip`), 다음 확장 후보 순서(`expansion_order`)
- `[APM]`: 2880프레임마다(게임 시간 약 2분) 자체 명령 수(`orders_per_min`, 유닛 종류별 누적), 보유 자원(`minerals`, `gas`)과 완성 기지·가스 건물 수(`depots`, `refineries`), 생략된 이동(`moves_skipped`, `grid_holds`), 반복 명령 생략(`orders_repeat_skipped`), 갇힌 일꾼 대기(`unstick_backoffs`), 지연·명령 간격(`latency_max`, `cmd_interval`)과 기타 누적 카운터
- `[END]`: 승패, 최종 전략, 종료 APM 및 누적 카운터

명령량은 `orders_per_min`으로 확인하세요. `bwapi_apm`은 Remastered에서 0으로 기록되며, 원래 BWAPI의 감쇠된 최근 APM으로 경기 전체 평균이나 자체 명령 횟수가 아닙니다. `bwapi_apm_at_end`는 종료 시 값, `bwapi_apm_max_sampled`는 주기 표본 중 최대값으로 표본 사이 순간 피크는 놓칠 수 있습니다. `res_*`는 단순 채취 일꾼의 가드 대상 자원 명령만 집계하고(`res_failed`는 BWAPI가 거부한 명령, `res_blocked_backoff`는 거부 후 대기로 생략한 명령), 복구 명령은 `stuck_*`로 별도 집계합니다. `bio_to_attack` / `bio_to_regroup`는 MidGameBio 배율이 적용되는 동안의 실제 상태 전환 횟수입니다. `attack_commit_holds`, `flee_guard_fights`, `dragoon_unsticks`(공격 취소로 멈춘 드라군 Stop), `micro_latency`(실제 적용 지연)는 테란전 교전 수정의 동작 확인용입니다. `invalid_moves`(차단된 잘못된 이동), `worker_flees`(벌처를 피해 피신한 일꾼)는 벌처 대응 수정의 확인용이고, `arrived_holds`는 도착 유닛 반복 이동을 생략한 횟수이며, `mining_assign_ok` / `mining_assign_fail`(배정 호출 단위라 기다리는 일꾼은 매 프레임 집계), `mining_idle_holds` / `mining_idle_depot_moves`(미배정 일꾼을 대기시킨 / 넥서스로 보낸 프레임 수), `mining_unassigned_long`은 겹친 미네랄 채취 수정의 확인용이고, `rear_unsticks`, `mine_clear_requests`, `defense_redirects`, `expansion_guards`, `cannon_backoffs`는 병력·확장 방어 수정(PR #17)의 확인용이며, `[EVENT] previous_game_unfinished`는 직전 경기가 `[END]` 없이 끝났음(크래시·강제 종료·연결 끊김)을 뜻합니다.

코드·문서·빌드 스크립트가 존재하는 것과 실행 검증은 구분합니다.

- 확인됨: [독립 정책 테스트](tools/bnet-policy-test) 407개 검사(겹친 미네랄 배정 시뮬레이션 포함: 원작은 거리 0인 병합 자원에 프로브 4기 중 1기만 배정, 수정 후 2기; 거리 0인 미네랄 8개에 12기 중 8기 → 12기; DefendBase 탐지 미완 해제 조건 검사 포함)와 설정 로더 테스트 통과(Linux g++), 변경 소스의 g++(C++23) 문법 검사. 사용자 Windows 환경에서 `bnet-pluto@9fabc694` 빌드가 배틀넷 저그전에서 실행되어 2분 시점 `orders_per_min`이 약 100으로 기록됨. `bnet-pluto@32a56cdb` 빌드로 2경기 실행(Attitude SE 2.1 저그전 승리, Radeon 1.2 프로토스전 16:20 패배); 이 두 경기 리플레이·로그 분석이 PR #15의 근거입니다. `bnet-pluto@0264e7aa` 빌드의 OdysseyRE 2.0 경기 로그(0:39에 `res_issued=2`, `orders_worker=232`, `stuck_attempts=0`)와 코드 분석이 PR #16의 근거입니다. 같은 빌드의 Backrooms 1.1 테란전(13:48 패배) 로그·리플레이 분석이 PR #17의 근거입니다. `bnet-pluto@908059a0` KnockOut 1.4 테란전(10:27 패배, 벌처 런바이·`worker_flees`/`invalid_moves` 폭주) 로그·리플레이 분석이 홈 가드 유지·도주 좌표 고정의 근거입니다. Fighting Spirit 1.4 저그전(2:52 저글링 난입 때 캐논 미착공, 캐논 자리 프로브 4기 대기, 가스 858) 로그·리플레이 분석이 PR #18의 근거입니다. Attitude SE 2.1 저그전(16:06 패배, 선풀 캐논 3기, 히드라 덴 확인 후 드라군 59회 생산·질럿 5회, 시타델·스톰·하이템플러 명령 없음) 로그·리플레이 분석이 히드라 대응의 근거입니다.
- 미확인: PR #18(초반 벽 캐논·잉여 가스 테크)의 Windows 빌드와 실전 효과, 홈 가드 유지·도주 좌표 고정의 Windows 빌드와 KnockOut/유사 맵 실전 효과, PR #17(병력·확장 방어)의 Windows 빌드와 실전 효과, 후방 무리 정체가 기억된 마인 때문이라는 원인(시간·위치가 일치하는 정황 근거; 새 `[ARMY]` 로그로 확인 예정), 공격받는 확장의 일꾼 대피(미구현), PR #16(겹친 미네랄 채취 수정)의 Windows 빌드와 OdysseyRE 실전 효과(새 `[MINING]` 로그로 확인 예정), PR #15(프로토스전 리버·셔틀 대응, 도착 유닛 이동 억제, 자원 명령 백오프)의 Windows 빌드와 실전 효과, PR #14(벌처 드랍·마인 대응)와 PR #8(저그 올인 대응)의 실전 효과, Attitude 경기에서 BWAPI가 자원 명령 약 1,826건을 거부한 원인(새 `res_failed` 이벤트로 확인 예정), 중후반 대규모 교전의 명령량, 이동 명령 생략이 드라군 컨트롤에 주는 영향. 승률 향상은 보증하지 않습니다. 기본 전술 수치는 실전 조정의 출발점입니다. 하이템플러 스톰 마이크로와 리버·스캐럽 회피 마이크로는 추가되지 않았고, 프로토스전 적 전략 인식(`EarlyRobo`)은 바뀌지 않았습니다.

## About
Stardust is written in C++ and uses [BWAPI](https://github.com/bwapi/bwapi) to play 1v1 Melee games of StarCraft: Brood War. Stardust plays as the Protoss race.

To see Stardust play against other bots, [BASIL ladder](https://basil.bytekeeper.org/).

Stardust is mostly optimized for AI vs. AI tournaments, but if you want to play against it, check out [SCHNAIL](https://schnail.com/).

## Libraries

Stardust uses [BWEM](http://bwem.sourceforge.net/) for terrain analysis and a modified version of [FAP](https://github.com/N00byEdge/FAP) for combat simulation.

## License

The license is MIT with an added condition that forks may not be submitted to StarCraft AI competitions without written consent of the author. The rationale behind this is to prevent a flood of minimally-altered forks popping up in tournaments, which happened with my previous bot Locutus in 2018.

So if you are interested in forking Stardust and submitting it to a tournament, create an issue here and we'll discuss it. I will likely accept any Zerg or Terran forks, but be much more strict with Protoss forks. 
