# Stardust
AI for StarCraft: Brood War

## Battle.net / Pluto fork

이 포크는 StarCraft: Remastered Battle.net의 지연 환경에서 채취 안정성을 개선하고 Pluto 스타일 테란에 대응하기 위한 프로토스 봇입니다. 작업 브랜치는 `bnet-pluto`입니다. 원작은 [bmnielsen/Stardust](https://github.com/bmnielsen/Stardust)입니다.

문서 기준: 2026-11-15 KST, 코드 커밋 [`df69d84`](../../commit/df69d8483c7c4de0c2fe6d0eadf0168837e311b8). 구현 상세·전체 설정·제한 사항은 [Battle.net 가이드](docs/BNET.md), 기본값은 [JSON 템플릿](config/stardust_bnet.json)을 참고하세요.

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

빌드 후 `dist/bwapi-data` 내용을 사용 중인 StarCraft의 `bwapi-data`에 배치합니다. 이 스크립트는 DLL·설정 빌드/배포용이며 Remastered용 BWAPI 실행 환경이나 브리지 설치를 대신하지 않습니다.

### 주요 설정

설정 검색 순서: `bwapi-data/AI/stardust_bnet.json` → `bwapi-data/read/stardust_bnet.json` → `bwapi-data/write/stardust_bnet.json`. 파일이 없으면 안전한 기본값을 사용합니다.

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
| `bio_attack_start_scale` | `0.7` | MidGameBio 상대 공격 시작 배율 |
| `bio_attack_continue_scale` | `1.0` | 공격 유지 배율; 기본값은 원작 유지 판단 |
| `bio_attack_resume_scale` | `0.7` | 재집결 후 공격 재개 배율 |
| `bnet_log` | `true` | 경량 경기 로그 활성화 |

순정 PvT UCB1 후보는 이미 `EarlyGameDefense` 하나입니다. 오프닝 고정의 주요 차이는 상대 이름별 이력과 무관한 명시적 선택 및 랜덤 상대의 PvU 탐색 우회입니다. 캐논은 배치 가능 위치·전략 조건·생산 우선순위에 따른 요청이며 건설 보장이 아닙니다.

호환성: 기존 `bio_aggression_scale`은 시작·재개에만 적용되고 새 키가 우선합니다. `main_cannons`가 없는 기존 설정은 `natural_cannons > 0`이면 1, 아니면 0으로 해석됩니다.

### 경기 로그와 검증 상태

로그는 `bwapi-data/write/stardust_bnet.log`에 누적됩니다. CMake `BNET_LOG=ON`이면 `TOURNAMENT_BUILD=ON`에서도 동작하며, `-NoBnetLog`는 로그를 빌드에서 제외합니다.

- `[START]`: 맵, 상대, 지연 프레임, 실제 적용 설정
- `[ENEMY]` / `[OURS]`: 적·아군 전략 전환
- `[EVENT]`: 탐지 요청 단계 전환, 첫 옵저버 완성, 캐논 배치 문제 등
- `[APM]`: 2880프레임마다(게임 시간 약 2분) BWAPI APM과 누적 카운터
- `[END]`: 승패, 최종 전략, 종료 APM 및 누적 카운터

`bwapi_apm`은 BWAPI의 감쇠된 최근 APM이며 경기 전체 평균이나 자체 명령 횟수가 아닙니다. `bwapi_apm_at_end`는 종료 시 값, `bwapi_apm_max_sampled`는 주기 표본 중 최대값으로 표본 사이 순간 피크는 놓칠 수 있습니다. `res_*`는 단순 채취 일꾼의 가드 대상 자원 명령만 집계하고, 복구 명령은 `stuck_*`로 별도 집계합니다. `bio_to_attack` / `bio_to_regroup`는 MidGameBio 배율이 적용되는 동안의 실제 상태 전환 횟수입니다.

코드·문서·빌드 스크립트가 존재하는 것과 실행 검증은 구분합니다. 이 README 갱신 과정에서는 Windows Win32 Release 빌드, 단위 테스트 실행, Remastered Battle.net 실전 대전을 수행하지 않았습니다. 저장소에는 [독립 정책 테스트](tools/bnet-policy-test)가 있으며, 여기서 테스트 통과·실전 안정성·승률 향상을 보증하지 않습니다. 기본 전술 수치는 실전 조정의 출발점입니다. 하이템플러 스톰 마이크로는 추가되지 않았고, 경로 추적의 후속 waypoint 이동은 채취 이동 가드 대상이 아닙니다.

## About
Stardust is written in C++ and uses [BWAPI](https://github.com/bwapi/bwapi) to play 1v1 Melee games of StarCraft: Brood War. Stardust plays as the Protoss race.

To see Stardust play against other bots, [BASIL ladder](https://basil.bytekeeper.org/).

Stardust is mostly optimized for AI vs. AI tournaments, but if you want to play against it, check out [SCHNAIL](https://schnail.com/).

## Libraries

Stardust uses [BWEM](http://bwem.sourceforge.net/) for terrain analysis and a modified version of [FAP](https://github.com/N00byEdge/FAP) for combat simulation.

## License

The license is MIT with an added condition that forks may not be submitted to StarCraft AI competitions without written consent of the author. The rationale behind this is to prevent a flood of minimally-altered forks popping up in tournaments, which happened with my previous bot Locutus in 2018.

So if you are interested in forking Stardust and submitting it to a tournament, create an issue here and we'll discuss it. I will likely accept any Zerg or Terran forks, but be much more strict with Protoss forks. 
