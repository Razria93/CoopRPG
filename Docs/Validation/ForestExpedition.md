# MVP02-01 · 2인 원정 스테이지 구축

CoopRPG · 2026-11-19 · 숲 동선, 패키지 실행 및 2인 검증

`L_ForestExpedition`에 준비 지점 → 공용 상자 → 교전 공간 → 공동 탈출구를 배치한다. 숲 맵은 시작 시 지정할 수 있고, 기존 기본 맵과 라운드 이동 경로를 사용한다. 서버는 숲 맵에 배치한 공용 상자·적·발판을 기존 라운드 로직에 연결한다. 교전 공간 양옆에 우회할 여유를 두고, 적의 정면은 충돌이 없는 밝은 도형으로 표시한다.

**근거** · [숲 스테이지 인계 기준](../Roadmap.md#forest-stage-handoff) · [서버의 맵과 액터 연결](../../Game/Source/CoopRPG/GameFramework/CoopRPGGameMode.cpp) · [기존 2인 완주 시험](Index.md#v07-default-playthrough)

## 검증 상태

| 항목 | 상태 |
|---|---|
| 전용 서버 패키지에서 숲 맵 지정 실행 | 17:59 KST에 포트 수신, 숲 Round ID 생성, 상자 `(228, 0, 28)`, 적 `(740, 0, 90.15)`, 발판 `(1320, 139, 6)`을 확인했다. |
| 로컬 패키지 2인 입장·완주 | 통과. 전용 서버와 두 Client가 숲 맵에 입장했다. 실제 키·마우스 입력으로 공동 탈출과 양쪽 1점을 확인했고, 원본 로그 판정 19개를 통과했다. |
| 데스크톱·노트북 사용자 확인 | 대기. 아래 순서로 고정 카메라 시야와 측면 이동을 확인한다. |

기준은 `feat/forest-stage-layout`의 `055613c`에 숲 작업을 더한 미커밋 상태다. Unreal Engine 5.8.4 소스 엔진과 Win64 Development Server·Client 타깃을 사용했다. 로컬 근거는 `Artifacts/Validation/mvp02-01-forest-20261119/`에 있다. `forest-server-entry.log`에 숲 지정 실행의 원본 로그, `LanStarterSmoke/result.json`에 기동 스크립트의 기존 기본 맵과 숲 지정 실행 결과가 있다.

## 패키지 실행 결과

18:14 KST의 숲 회차 `F770500C4B7F2C940B2E13AB77B79F76`를 전용 서버와 두 클라이언트 원본으로 대조했다. 클라이언트에는 Windows 키·마우스 입력을 보냈다. 실행 인수와 세 로그에 `-CoopCombatTest` 또는 `Coop.Test.*` 호출은 없었다.

| 단계 | 확인한 결과 |
|---|---|
| 무장 | 서버가 A와 B에 무기를 하나씩 지급했다. 공용 상자 무기 재고는 2 → 1 → 0이었다. |
| 교전 공간 | A가 정면에 머무는 동안 B가 측면으로 돌아갔다. 양쪽 캡처에 두 플레이어와 적의 밝은 정면 표시가 함께 나왔다. A·B 각 5회 적중으로 적 HP가 100 → 0이 됐고, 서버는 사망·드롭을 각각 한 번 기록했다. |
| 전리품 | 팀 전리품은 0 → 1로 한 번 지급됐다. 두 클라이언트의 복제 값도 1이었다. |
| 단독·공동 탈출 | B 혼자 발판에 3.747초 머물고 A가 밖에 있을 때는 탈출이 시작되지 않았다. 두 사람 생존 상태로 3.011초 함께 머문 뒤 서버 성공·1점이 한 번 확정됐다. 두 클라이언트 로그와 HUD에서 같은 회차 결과가 확인됐다. |
| 기본 경로 | 서버와 두 클라이언트 모두 결과 표시 후 기존 `L_Movement_Round2`로 이동했다. LAN 기동 스크립트의 기본 호출도 `L_Movement`를 열었다. |

첫 완주 시도에서는 창마다 전환 직후 한 번만 클릭해 `ManualFocusClickConsumed`가 반복됐고 공격 입력이 전달되지 않았다. 게임 코드의 포커스 동작을 확인한 뒤, 같은 창에서 포커스가 잡혔을 때 다시 클릭하도록 시험 절차를 조정했다. 같은 빌드의 새 회차에서 통과했고 첫 시도의 원본도 남겼다.

**원본** · 로컬 `Artifacts/Validation/mvp02-01-forest-20261119/`에 `CoopRPG-MVP02-01-Forest.zip`, 파일 해시를 담은 `build-manifest.json`, 쿠킹·빌드·스테이징 로그가 있다. 첫 시도는 `Packaged-01/`, 통과 회차는 `Packaged-02/`다. `Packaged-02/`의 `server.log`, `a.log`, `b.log`, `os-input.jsonl`, `audit.json`과 `flank-a.png`·`flank-b.png`, `solo-zone-a.png`·`solo-zone-b.png`, `success-a.png`·`success-b.png`를 함께 보관했다.

Windows PowerShell 5.1에서 로컬 검증 스크립트의 숲 맵 옵션으로 전용 서버·A·B를 실행해 세 프로세스의 숲 입장을 확인했다. ZIP 안의 기동 스크립트·실행 파일·콘텐츠 컨테이너 13개가 실제 시험한 Stage와 동일한지 해시로 대조했다. 결과는 각각 `local-starter-smoke.json`, `archive-verification.json`에 있다.
## 데스크톱·노트북에서 직접 확인

두 PC에서 같은 `CoopRPG-MVP02-01-Forest.zip` 묶음을 풀고 `Start-CoopLan.ps1`과 `Stage`가 나란히 있는 폴더에서 PowerShell을 연다. 데스크톱은 서버와 A를, 노트북은 B를 실행한다. Tailscale 연결과 UDP 포트 환경은 [별도 PC 기준선](LanBaseline.md#manual-tailscale-20261118)을 사용한다.

**데스크톱 · 서버 및 A**

```powershell
$forestServerIp = (tailscale ip -4).Trim()
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Start-CoopLan.ps1 -Role Server -Map L_ForestExpedition -Address $forestServerIp
```

시작 출력의 `Log` 파일에서 `listening on port`와 `ForestStageReady Map=L_ForestExpedition`을 확인하고 A를 실행한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Start-CoopLan.ps1 -Role Client -ClientLabel A -Address $forestServerIp
```

**노트북 · B**

```powershell
$forestServerIp = Read-Host '데스크톱의 Tailscale IPv4'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Start-CoopLan.ps1 -Role Client -ClientLabel B -Address $forestServerIp
```

1. 두 창에서 숲의 시작 지점과 공용 상자가 보이는지 확인한다. 각자 `R`을 누른 뒤 `WASD`로 상자에 접근하고 `E`로 열어 `G`로 각자 무기를 받는다. `Q`로 상자 창을 닫는다.
2. A는 적의 정면에서 접근하고, B는 교전 공간 바깥쪽으로 돌아 적의 측면에 선다. 각 화면에서 적의 밝은 정면 표시와 두 플레이어가 함께 보이는지 확인한다. 카메라가 놓치는 곳이나 이동이 막히는 곳을 화면과 함께 남긴다.
3. 왼쪽 클릭으로 적을 처치한다. 처음 클릭하거나 다른 창에서 돌아온 뒤 첫 클릭이 포커스에 쓰이면 같은 창에서 다시 클릭한다. 적을 처치한 뒤 한 명이 전리품에 접근해 `T`를 누른다. 양쪽 팀 전리품이 `1`인지 확인한다. 한 명만 발판에 3초 이상 서서 진행이 시작되지 않는지 보고, 두 사람이 생존한 채 함께 올라 3초 동안 머문다.
4. 양쪽에 성공·1점이 보이면 결과 화면을 각각 저장한다. 데스크톱의 서버·A 로그와 노트북의 B 로그를 함께 남긴다. 시야와 우회는 교전 전에 찍은 A·B 화면도 함께 비교한다.

**근거** · [LAN 기동 스크립트](../../Tools/Validation/Start-CoopLan.ps1) · [두 PC의 원본 자료 수집 방식](Environment.md#phase2-test-environment)

<a id="round-01-package"></a>
## L_Round_01 · 카툰풍 숲 패키지 재검증

2026-11-19에 승인된 `L_Round_01` 배치를 Unreal Engine 5.8.4 Win64 Development 전용 서버·클라이언트로 패키징했다. `feat/forest-stage-layout`의 기준 커밋 `055613c` 위 미커밋 상태에서 빌드했다. 사용자 원본 `L_Round_Base`를 복제한 지면·바위·조명 환경과 배치한 2인 시작점·공용 상자·적·탈출 발판을 사용했다. 원본 맵과 에셋 파일 83개의 SHA‑256이 배치 전과 모두 일치했다. 승인된 `L_Round_01`과 이전 도형 맵 `L_ForestExpedition`의 해시도 패키징 전에 기록한 값과 일치했다.

| 확인 항목 | 조건과 결과 |
|---|---|
| 패키지 입장 | 전용 서버와 같은 PC의 A·B가 `L_Round_01`에 입장했다. 서버가 상자 `(300, 0, 28)`, 적 `(850, 0, 90.15)`, 발판 `(1480, 0, 6)`을 초기화했다. ZIP에 포함된 `Start-CoopLan.ps1 -Role Server -Map L_Round_01`로도 UDP 수신과 맵 초기화를 확인했다. |
| 고정 카메라와 측면 우회 | 양쪽 화면에서 노란 P1·파란 P2와 적의 밝은 정면 표시를 함께 확인했다. A가 적 앞 `(594.2, -82.0)`에 머물 때 B는 바깥으로 돌아 `(708.5, 148.2)`에서 측면에 진입했다. 두 위치는 각 클라이언트 이동 로그에서 대조했다. |
| 바위 경계 충돌 | 같은 패키지의 `Packaged-03`에서 바위 `SM_RockL22` 쪽으로 이동할 때 Y `1611.9` 부근에서 정지했다. +Y 입력이 들어왔는데 속도가 0인 표본 33개와 반대 입력으로 물러난 표본 27개를 A 원본 로그에서 확인했다. |
| 무장·전투·루팅 | 새 회차 `Packaged-04`에서 실제 Windows 키·마우스 입력으로 양쪽이 무기를 받고 각각 5회 적중시켰다. 적 HP가 100 → 0으로 줄었고 서버는 적 사망과 전리품 생성을 각각 한 번 기록했다. 팀 전리품은 0 → 1로 한 번 지급됐고 A·B에 각각 1로 표시됐다. |
| 단독·공동 탈출 | B만 3.728초 발판에 있었을 때 진행이 시작되지 않았다. 두 사람이 생존한 채 함께 3.001초 머물자 서버는 성공·1점을 한 번 확정했다. 두 클라이언트 HUD와 로그에서 같은 회차의 성공·팀 점수 1을 확인했다. 두 클라이언트가 기존 후속 맵 `L_Movement_Round2`에 진입한 것도 로그에 남았다. |

로그 및 파일 판정 21개가 통과했다. 완주에는 게임의 일반 입력을 사용했다. 바위 경계 시험 중 창 포커스가 바뀐 구간은 판정에서 제외하고, 입력과 이동 속도가 둘 다 남은 원본 표본으로 확인했다. 새 회차에서는 입력 전에 포커스를 확인했으며, 창 전환 직후 클릭이 포커스에 쓰이면 같은 창에서 다시 눌렀다. 대표 바위 경계와 승인된 교전 동선을 검증했다. 데스크톱·노트북을 이용한 이 패키지의 사용자 직접 확인은 대기 중이다.

### 실행 파일과 원본 근거

로컬 `Artifacts/Validation/mvp02-01-round-01-package-20261119/`에 `CoopRPG-MVP02-01-Round-01.zip`과 서버·클라이언트 빌드, 쿠킹·스테이징 로그를 보관했다. `build-manifest.json`은 ZIP과 실제 시험한 Stage의 기동 스크립트·실행 파일·콘텐츠 컨테이너 13개의 해시 대조를 담는다. `source-integrity.json`은 맵과 에셋 해시 대조 결과를 담고, `LanStarterSmoke/result.json`은 ZIP 기동 스크립트의 서버 시작 확인을 담는다. `LocalStarterSmoke/result.json`에는 저장소의 로컬 기동 스크립트로 서버·A·B가 같은 맵에 입장한 결과가 있다. 저장소 검사 6개는 `RepositoryFinal/result.json`에 있다.

- `Packaged-03/rock-boundary-a.png`와 `Packaged-03/a.log`에 바위 경계 화면과 원본 이동 로그가 있고, `collision-sample.json`에 해당 로그 행과 표본 수가 있다.
- `Packaged-04/flank-a.png`·`flank-b.png`와 `flank-positions.json`에 각 시점의 시야와 우회 위치가 있다. `solo-check.json`과 `solo-zone-a.png`·`solo-zone-b.png`에 B 단독 발판 확인 결과가 있다.
- `Packaged-04/success-a.png`·`success-b.png`, `server.log`·`a.log`·`b.log`, `os-input.jsonl`과 `audit.json`에 성공 화면, 원본 로그, 입력 기록과 판정이 있다.

**사용자 확인** · 두 PC에 `CoopRPG-MVP02-01-Round-01.zip`을 풀고 위 기동·플레이 순서에서 서버 옵션을 `-Map L_Round_01`로 바꾼다. 서버 로그의 `listening on port`와 `RoundStageReady Map=L_Round_01`을 확인한 뒤 A·B를 접속한다. 교전에서 적의 정면 표식과 두 플레이어가 함께 들어오는 화면, 양쪽 성공·1점 화면, 서버·A·B 원본 로그를 남긴다. [별도 PC 검증 기준](LanBaseline.md#manual-tailscale-20261118)

<a id="round-01-launcher"></a>
### 실행기에서 숲 서버 시작·종료

실행기의 `서버 맵`에서 패키지에 등록한 시작 맵을 고를 수 있다. 이번 묶음은 `L_Round_01`을 기본 선택으로 표시하고 `L_Movement`도 열 수 있다. 서버 실행 시 선택을 회차 기록에 저장하고, 실행기를 다시 열면 실행 중인 맵을 복원한다. 같은 패키지에서 마지막으로 고른 맵도 저장한다. 기존 패키지 정보에 맵 목록이 없으면 `L_Movement`를 사용한다. `접속 전용`의 클라이언트는 접속 대상 서버가 연 맵에 들어간다.

| PC | 기동과 확인 |
|---|---|
| 데스크톱 | 두 PC에 같은 `CoopRPG-MVP02-01-Round-01-2PC.zip`을 풀고 `Open-CoopRPG-2PC.cmd`를 연다. `Tailscale 호스트`·데스크톱 Tailscale IPv4·UDP `7777`·`L_Round_01`을 확인하고 `실행`을 누른다. 전용 서버와 A가 열리면 `주소 복사`로 주소와 포트를 노트북에 전달한다. |
| 노트북 | `접속 전용`에서 받은 값을 복사한 뒤 `주소 붙여넣기`를 누르고 B를 접속시킨다. 데스크톱의 서버 참가자 표와 양쪽 숲 화면을 확인한다. |
| 시험 종료 | 양쪽 성공·팀 점수 1 화면과 숲 시야·측면 우회·바위 충돌 장면을 남긴다. 각 실행기의 `로그 폴더`에서 데스크톱 서버·A와 노트북 B의 원본을 보관한다. 양쪽 클라이언트를 닫고 데스크톱 서버 행의 `종료`를 누른다. |

게임 `Stage` 파일 85개는 앞서 2인 완주를 확인한 ZIP과 SHA‑256이 모두 일치한다. 실행기만 묶어 배포하는 과정에서 Windows PowerShell의 CMD 진입점이 `Get-FileHash`를 불러오지 못해 빌드 정보가 표시되지 않았다. 해시 판정을 .NET SHA‑256으로 바꾸고 CMD에서 올바른 빌드와 Stage가 열리는 것을 확인했다. 창 폭 795px·1160px에서 맵 라벨·설정과 로그 버튼을 실제 화면으로 확인했다.

**근거** · 로컬 `Artifacts/Validation/mvp02-01-round-01-launcher-20261119/`의 실행 ZIP, `archive-verification.json`, `entry-final.json`, `Live-bb8702c7/checks.txt`와 서버·A·B 원본, `host-default.png`·`host-wide.png`·`host-two-participants.png`·`host-stopped.png`. 별도 물리 PC 2대에서의 실사용 확인은 대기 중이다.

#### 넓은 창 설정 배치 보완

위쪽은 모드와 IPv4 입력·선택을 왼쪽에 두고 UDP 포트와 `실행`을 오른쪽에 두었다. 가운데 추가 폭은 아래 `서버 맵` 선택란에서 사용한다. 맵 선택란은 모드 입력란부터 UDP 포트의 오른쪽 끝까지 이어진다. `실행`은 두 행의 위·아래 끝을 맞추고 텍스트를 가운데에 둔다. UDP 값은 숫자 컨트롤 주변에 여백을 주어 직접 입력과 화살표 증감 동작을 유지한다.

실제 Windows 실행기를 폭 795px·1160px로 띄워 끝선, 중앙 정렬과 스크롤 유무를 확인했다. 로컬 `Artifacts/Validation/mvp02-01-round-01-launcher-layout-20261119/`의 `default.png`·`wide.png`, `layout-checks.json`, `port-check.txt`에 화면과 측정값이 있다. `CoopRPG-MVP02-01-Round-01-2PC-layout.zip`은 이전 ZIP의 실행기 UI 스크립트만 갱신한 파일이며, 게임 `Stage` 파일 85개는 해시가 일치한다. 대조 결과는 `archive-verification.json`에 있다.
