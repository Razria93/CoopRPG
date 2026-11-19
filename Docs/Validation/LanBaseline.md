# CoopRPG 별도 PC 2인 실행

CoopRPG · 2026-11-18 · Win64 Development · 소스 `c0fed32`

같은 사설 네트워크의 두 Windows PC에서 전용 서버 한 개에 접속한다. PC A는 서버와 클라이언트 A, PC B는 클라이언트 B를 실행한다. 두 사람은 각자 자기 PC에서 조작한다.

테스트에 사용한 도구와 자료 전달·보존 과정은 [테스트 환경과 근거 수집](Environment.md#phase2-test-environment)을 참고한다.

## 준비

1. 같은 전달 ZIP을 두 PC의 쓰기 가능한 새 폴더에 푼다. 아래 명령은 `C:\CoopRPG-LAN`에 `Start-CoopLan.ps1`과 `Stage`가 나란히 있는 경우다. 다른 경로에 풀었으면 `Set-Location`의 값만 바꾼다.
2. PC A에서 같은 Wi-Fi나 이더넷을 쓰면 `ipconfig`로 해당 IPv4를, 두 PC를 Tailscale로 연결하면 `tailscale ip -4`로 PC A의 Tailscale IPv4를 확인한다. 두 PC 모두 아래 예시의 `192.168.1.20`을 **PC A의 실제 주소**로 바꾼다.
3. Windows에서 `VCRUNTIME140.dll` 또는 `MSVCP140.dll` 오류가 나면 `Prerequisites\vc_redist.x64.exe`로 Microsoft Visual C++ x64 런타임을 설치하고 다시 실행한다.

## PC A에서 서버와 클라이언트 실행

~~~powershell
Set-Location 'C:\CoopRPG-LAN'
$serverIp = '192.168.1.20'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Start-CoopLan.ps1 -Role Server -Address $serverIp
~~~

출력한 서버 PID와 로그 경로를 기록한다. 다른 PowerShell 창에서 그 경로를 사용해 `listening on port 7777`을 기다린다.

~~~powershell
Get-Content -LiteralPath '<출력된 서버 로그 경로>' -Tail 30 -Wait
~~~

서버가 포트를 열면 PC A의 첫 PowerShell 창에서 클라이언트 A를 실행한다.

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Start-CoopLan.ps1 -Role Client -Address $serverIp -ClientLabel A
~~~

## PC B에서 클라이언트 실행

~~~powershell
Set-Location 'C:\CoopRPG-LAN'
$serverIp = '192.168.1.20'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Start-CoopLan.ps1 -Role Client -Address $serverIp -ClientLabel B
~~~

두 창에 `STAGE 1 | TEAM READY`가 뜨면 양쪽에서 서로의 캐릭터가 보이는지 확인한다. 로그의 A와 B는 실행한 PC를 나타낸다. 게임의 P1·P2는 서버에 접속한 순서에 따라 배정된다.

## 한 판과 다음 맵 확인

1. 두 사람이 각각 `R`을 누르고 `WASD`로 움직여 상대 화면에서 이동이 보이는지 확인한다.
2. 상자 가까이에서 각각 `E`로 열고 `G`로 무기를 장착한다. `F`는 회복약 획득, `Q`는 닫기, `H`는 회복약 사용이다.
3. 왼쪽 클릭으로 적을 공격한다. 오른쪽 클릭은 타격 전 취소다. 창에 포커스를 준 직후 첫 왼쪽 클릭은 포커스에 쓰일 수 있다. 적이 쓰러지면 근처에서 한 명이 `T`를 눌러 팀 전리품을 얻는다.
4. 두 사람이 탈출 발판에 함께 3초간 머문다. 성공과 팀 점수 1을 두 화면에서 확인한다.
5. 자동으로 이동한 다음 맵에서 양쪽에 `STAGE 2 | TEAM READY`가 뜨는지 확인한다. A가 `R`을 누르면 P1만 준비되고, B도 `R`을 누르면 새 라운드가 시작된다. 시작 직후 이동·전투·획득 전에 양쪽 HUD의 HP·SP 100/100, 무기 미장착, 회복약·팀 전리품 0을 확인한다.
6. 두 게임 창을 닫은 뒤 PC A에서 `Stop-Process -Id <서버 기동 시 출력된 PID>`로 이번 서버만 종료한다. 다시 기동하면 새 로그가 생성된다.

## 확인할 로그

| 단계 | PC A 서버 로그 | 양쪽 클라이언트 로그·화면 |
|---|---|---|
| 접속 | `listening on port 7777`, `Join succeeded` 2건 | 각자 `Welcomed by server`, `ManualInputEnabled` |
| 준비·무장 | `LootPlayerReady`, `LootRoundPhase`의 `Phase=Playing`, `ChestWeaponGranted` | `MeleeWeaponReplicated`와 장착 HUD |
| 적 처치·루팅 | `AttackApproved`, `EnemyDeathConfirmed`, `EnemyLootGranted`의 `TeamLoot=0->1` | 양쪽 `TeamLootCountReplicated` 및 팀 전리품 1 |
| 탈출·맵 이동 | `ExtractionStarted`, `ExtractionResult`의 `Score=1`, `RoundTravelStarted` | 양쪽 `ExtractionResultReplicated`, `L_Movement_Round2` 진입과 재준비 |

로그는 각 PC의 `Logs` 폴더에 저장된다. 같은 실행의 **PC A 서버 로그, PC A 클라이언트 로그, PC B 클라이언트 로그**를 모아 전달한다. 단계 확인에는 아래 필터를 쓸 수 있다.

~~~powershell
Select-String -Path .\Logs\*.log -Pattern 'listening on port|Join succeeded|Welcomed by server|ConnectionFailed|ConnectionEnded|ManualInputEnabled|LootPlayerReady|LootRoundPhase|ChestWeaponGranted|MeleeWeaponReplicated|AttackApproved|EnemyDeathConfirmed|EnemyLootGranted|TeamLootCountReplicated|ExtractionStarted|ExtractionReset|ExtractionResult|RoundTravelStarted|L_Movement_Round2'
~~~

## PC B에서 접속이 안 될 때

- 서버 로그의 `listening on port 7777`과 PC B의 대상 IP를 확인한다. PC A는 되는데 B만 실패하면 두 PC가 서로 통신 가능한 같은 사설 LAN인지, PC A 방화벽이 막고 있는지 확인한다. 게스트 Wi-Fi는 같은 네트워크처럼 보여도 기기 간 접속을 막을 수 있다.
- 신뢰하는 **개인 네트워크**에서 PC A의 Windows 방화벽이 막는 경우에만 관리자 PowerShell로 게임 서버 실행 파일의 UDP 포트를 허용한다. `$serverIp`에는 PC A의 IPv4를 사용한다.

  ~~~powershell
  Set-Location 'C:\CoopRPG-LAN'
  $serverIp = '192.168.1.20'
  $serverExe = (Resolve-Path .\Stage\WindowsServer\CoopRPG\Binaries\Win64\CoopRPGServer.exe).Path
  New-NetFirewallRule -Name 'CoopRPG-LAN-7777' -DisplayName 'CoopRPG LAN UDP 7777' -Direction Inbound -Action Allow -Program $serverExe -Protocol UDP -LocalAddress $serverIp -LocalPort 7777 -RemoteAddress LocalSubnet -Profile Private
  ~~~

  이 시험에서 추가한 규칙은 끝난 뒤 같은 관리자 창에서 `Remove-NetFirewallRule -Name 'CoopRPG-LAN-7777'`로 제거할 수 있다. 다른 포트를 쓰면 세 기동 명령과 방화벽 규칙에 같은 포트를 지정한다.
- 최초 연결 실패는 클라이언트 로그의 `ConnectionFailed`, 연결 후 서버 종료는 `ConnectionEnded` 안내를 확인한다. `ServerFull` 또는 `RoundInProgress`가 보이면 서버에 접속한 창을 확인하고 새 세션을 시작한다.

## 2026-11-18 패키지 검증

- `c0fed32`의 `Game/` 트리를 별도로 추출해 UE 5.8.4에서 Win64 Development Server와 Client를 둘 다 빌드했다. 맵·입력 에셋·설정·프로젝트 파일이 동일한 이전 후보의 Cook 컨테이너 10개를 재사용했다. SP 후보와 #16 검증 패키지 사이에서 컨테이너 해시가 모두 일치했다.
- 전달 파일은 `CoopRPG-LAN-c0fed32.zip`이다. 공백이 포함된 새 폴더에 풀어 매니페스트의 130개 파일 해시를 대조했다. 두 빌드의 타깃 영수증을 기준으로 패키지의 NonUFS 의존 파일도 Server 15개, Client 16개를 확인했다.
- 한 Windows PC에서 실제 LAN IPv4로 서버를 열고 클라이언트 두 프로세스를 접속시켰다. 서버를 중단한 뒤 양쪽의 `ConnectionEnded`와 동일 포트에서 재기동·재접속을 확인했다. 원본은 로컬 `Artifacts/Validation/phase2-lan-c0fed32-20261118-082619/exact-lan-ipv4-smoke/`에 있다.
- 같은 ZIP으로 자동 성공 회차의 탈출 1점, 양쪽 다음 맵 진입, HP·SP·인벤토리 초기화와 재준비 등 16개 판정을 통과했다. 시험용 입력·상태 설정을 사용한 자동 결과다. 원본은 같은 실행의 `exact-round-travel/success/`에 있다.
- 패키지 준비 시점에 남았던 별도 PC 직접 실행은 [Tailscale 수동 재현 결과](#manual-tailscale-20261118)를 따른다.

<a id="manual-tailscale-20261118"></a>
## 2026-11-18 · Tailscale 별도 PC 수동 재현

Windows 데스크톱의 전용 서버와 클라이언트 A, 노트북의 클라이언트 B를 `c0fed32` 전달 패키지로 실행했다. 두 클라이언트는 `-CoopManualInput`으로 기동했다. 공유 자료에서 시험 전용 옵션이나 명령은 확인되지 않았다. 데스크톱의 서버·클라이언트 실행 파일 2개의 SHA-256은 전달 매니페스트와 일치했다. 노트북 판본은 접속·기동 로그로 대조했다.

| 단계 | 서버·A·B 로그 확인 |
|---|---|
| 두 PC 접속·준비 | 서버가 서로 다른 Tailscale 주소에서 A·B를 받아 초기 `Join succeeded` 2건을 남겼다. 양쪽에 `P1=Ready P2=Ready`가 복제돼 첫 라운드가 시작됐다. |
| 무장·적 처치·팀 전리품 | 서버에서 두 PlayerId에 무기가 각각 지급돼 무기 재고가 `2→1→0`으로 바뀌었다. 적 사망과 팀 전리품 지급은 각각 1회였고 양쪽에 팀 수량 `0→1`이 복제됐다. |
| 공동 탈출 | 서버가 3.002초 점유 후 `Outcome=Success Score=1`을 한 번 확정했다. A·B는 같은 Round ID의 성공·1점을 복제·표시했다. 서버는 두 결과 확인을 받고 다음 맵 이동을 시작했다. |
| 다음 맵 | `L_Movement_Round2`에서 새 Round ID가 만들어졌다. 예약 좌석이 `1→0`으로 소진됐고 두 클라이언트에 `P1=Waiting P2=Waiting`이 기록된 뒤 양쪽이 종료됐다. |

**판정 범위** · 별도 PC의 접속·첫 판 수동 완주·다음 맵 재접속과 준비 대기 상태까지 확인했다. 다음 맵에서 양쪽 `R` 재입력·라운드 시작 기록은 수집분에 없다. 새 Pawn의 HP·SP와 인벤토리 초기값도 이번 수동 원본에서 판정할 수 없다. 초기값과 재준비는 앞 절의 같은 PC 자동 시험에서 확인했다. [같은 스테이지 2의 후속 수동 검증](#manual-tailscale-20261118-rerun)에서 양쪽 초기값과 재준비를 확인했다.

서버와 노트북 로그에서 대응 이벤트의 시각이 약 4.7초 어긋나므로 Round ID, 이벤트, 파일 내 순서로 대조했다. 기기 간 로그 타임스탬프로 지연을 계산하지 않았다.

**원본** · 로컬 `Artifacts/Validation/phase2-lan-c0fed32-20261118-082619/manual-tailscale-20261118-110313/`에 서버·A·B의 복사본과 파일별 SHA-256을 보존했다.

<a id="manual-tailscale-20261118-rerun"></a>
## 2026-11-18 · 별도 PC 다음 맵 초기화·재준비

첫 실행에서 탈출 후 스테이지 2로 이동한 전용 서버를 그대로 사용했다. 두 PC가 클라이언트를 다시 열었을 때 서버의 현재 맵은 `L_Movement_Round2`였다. 화면과 로그는 서버에서 생성한 Round ID를 기준으로 맞췄다. 아래 시각은 서버 로그를 한국 시각으로 환산했다.

| 회차 | 서버·클라이언트 대조 | 화면 근거 |
|---|---|---|
| 첫 실행에서 도착한 스테이지 2 · `601F3CE4…` | 11:11에 생성된 회차에 A·B가 다시 접속했다. 각자 `R`을 눌렀고 서버가 12:47:14에 `Duration=300`, `Phase=Playing`을 기록했다. 12:49:54에 플레이어 사망으로 실패·0점이 확정됐다. | A-03·B-02에 전투·획득 전 HP·SP 각 `100/100`, 무기 미장착, 회복약·팀 전리품 각 `0`이 보인다. A-04·B-03에 상자 초기 재고 무기 2개·회복약 1개가 보인다. |
| 사망 뒤 스테이지 1 · `6E5309A5…` | 양쪽이 이동해 준비했다. 서버가 12:53:25에 공동 탈출 성공·1점을 확정했고 A·B가 같은 결과를 받았다. | A·B 결과 화면을 보존했다. |
| 공동 탈출 뒤 새 스테이지 2 · `C4670813…` | 같은 클라이언트 세션에서 양쪽이 새 맵에 진입했다. 각자 `R`을 눌렀고 12:54:18에 서버 타이머가 300초로 시작했다. 이후 12:59:18에 시간초과·0점이 양쪽에 표시됐다. | A-11에서 P1만 준비, A-12에서 라운드 시작 및 초기 HUD를 확인했다. B-08은 준비 대기 화면이다. B의 `R` 입력과 양쪽 준비는 로그에 있으며, 라운드 시작 후 B의 전체 초기 HUD는 해당 회차 자료에 없다. |

**판정** · 첫 실행의 성공·1점과 다음 맵 진입, 후속 실행의 동일한 목적지에서 두 PC의 초기 HUD·재준비를 연결해 `c0fed32` 별도 PC 2인 기준선을 마감했다. 이어진 새 회차에서도 중간 재접속 없이 양쪽 재준비와 라운드 시작을 확인했다.

**원본** · 로컬 `Artifacts/Validation/phase2-lan-c0fed32-20261118-082619/manual-tailscale-20261118-124142/`에 서버·A·B 원본 로그 3개와 A 화면 12장·B 화면 8장을 보존했다. `manifest.json`에 전체 Round ID, 화면 번호와 파일별 SHA-256을 기록했다.

<a id="stage2-server-reuse"></a>
### 재검증에서 스테이지 2로 시작한 이유

사용자는 클라이언트를 다시 열었을 때 스테이지 2가 바로 나오는 점을 확인했다. 첫 완주 직후 서버가 생성한 `601F3CE4…` 회차와 두 클라이언트의 재접속 로그가 일치했다. 기동 스크립트는 전용 서버를 독립 프로세스로 실행하므로 클라이언트 종료 뒤에도 해당 맵이 유지됐다. 사용자는 데스크톱에서 실행 중인 서버를 PowerShell로 조회해 종료했고, 다시 조회했을 때 서버 프로세스가 없음을 확인했다.

이 사례에서 드러난 문제는 검증 중 기존 서버와 새 서버를 쉽게 구분하기 어려웠다는 점이다. 사용자는 실행·종료·현재 상태·실패 이유를 한 창에서 보는 도구를 제안했다. [실행기 완료 기준](../Roadmap.md#validation-launcher)

<a id="validation-launcher-first-run"></a>
## 검증 실행기 · 첫 버전

실행기 ZIP을 풀고 `Launcher` 안의 `Open-CoopValidationLauncher.cmd`를 더블클릭한다. 기존 게임 패키지에서 `Launcher`가 `Stage`와 나란히 있으면 자동으로 열린다. 별도 폴더에 풀었으면 **실행·접속 설정 → 찾아보기**에서 게임 패키지 폴더 또는 그 안의 `Stage`를 선택한다. 다음 실행부터 마지막으로 선택한 경로를 사용한다. [현재 실행기 설정](#validation-launcher-alignment) 저장소 실행 시 [실행기 스크립트](../../Tools/Validation/Open-CoopValidationLauncher.ps1)의 `-PackageRoot`로 게임 패키지 또는 `Stage`를 지정할 수 있다.

| 실행 위치 | 조작 |
|---|---|
| 로컬 2인 | `로컬 2인`에서 **실행**을 누른다. 서버가 UDP 포트를 열면 A·B가 같은 주소로 접속한다. |
| 데스크톱 | `Tailscale 호스트`에서 데스크톱의 Tailscale IPv4와 UDP 포트를 입력하고 **실행**을 누른다. 서버와 A가 뜬다. |
| 노트북 | `접속 전용`에서 데스크톱와 같은 주소·포트를 입력하고 **클라이언트 B 접속**을 누른다. |

서버의 **접속 대기**는 서버 로그와 포트 수신으로 확인한다. 클라이언트의 **접속 완료**는 서버 Welcome 이후 플레이어 초기화 로그로 확인한다. **이번 로그 보기**는 같은 PC의 서버·클라 로그를 열고, **기존 로그 열기**는 저장된 실행 폴더를 연다. 원격 B 로그는 B 쪽에 기록한다.

실행기 창을 닫아도 시작한 프로세스는 유지된다. 같은 로그 폴더로 다시 열면 PID·시작 시각·실행 파일 경로를 대조해 실행 상태를 복원한다. **종료**는 선택한 행의 프로세스를 끝낸다. 서버가 종료되면 실행기는 같은 로컬 회차에서 서버 프로세스 종료를 먼저 표시한다. 클라이언트가 네트워크 종료를 감지하면 해당 로그를 반영한다. 새 서버를 시작하려면 기존 로컬 클라이언트도 각 행에서 종료한다.

### 로컬 검증 · 2026-11-18

- Win64 Development `c0fed32`, Windows PowerShell 5.1과 WinForms로 확인했다. 실행 버튼으로 로컬 서버·A·B를 띄운 뒤 실행기를 다시 열어 같은 PID와 양쪽 입장을 확인했다. 서버만 종료했을 때 A·B 프로세스가 열려 있는 상태에서 서버 종료가 표시됐다. 양쪽을 종료한 뒤 재실행하자 새 서버 PID·로그 폴더로 양쪽이 스테이지 1에 입장했다.
- 점유한 UDP 포트에서 **포트 사용 중**과 시작 실패 이유를 확인했다. 두 명이 접속한 서버에 다른 실행기의 접속 전용으로 세 번째 클라이언트를 띄워 **접속 거절 · 정원 2명**과 원본의 `RoundFull`을 대조했다. 예전 시작 시각을 가진 PID는 관리 대상에서 제외하는 판정도 확인했다.
- 검증 화면 및 해당 실행 로그는 로컬 `Artifacts/Validation/validation-launcher-controls-20261118-141233-395/`에 있다. [데스크톱·노트북 실행기 수동 검증](#validation-launcher-two-pc)에서 실제 두 PC의 재기동과 재접속을 확인했다.

<a id="validation-launcher-two-pc"></a>
### 실행기 수동 검증 · 데스크톱과 노트북 · 2026-11-18

`c0fed32` 패키지로 사용자가 데스크톱의 Tailscale 호스트와 A, 노트북의 접속 전용 B를 실행했다.

| 확인 항목 | 관측과 근거 |
|---|---|
| 실행기 다시 열기 | 최초 스테이지 1에 두 클라이언트가 입장했다. 사용자가 데스크톱 실행기를 닫았다 다시 열자 동일한 서버 PID `54688`과 시작 시각이 표시됐다. |
| 기존 서버 종료 | 서버 행의 상태가 `접속 대기 → 종료`로 바뀌었다. 기존 회차의 `processes.json`에서 해당 PID의 실행기 종료 표시가 남았다. |
| 새 서버 기동 | 같은 UDP 포트에서 새 서버 PID `15496`과 새 Round ID `2DE1F33E…`를 확인했다. 서버가 `L_Movement`로 열렸다. |
| 양쪽 재접속 | 노트북 B가 먼저 들어가 P1을 받았고, 데스크톱 A가 뒤이어 P2를 받았다. 새 서버에서 입장 성공이 두 건이다. A 원본과 양쪽 화면에서 `STAGE 1 | TEAM READY` 및 P1·P2 준비 대기가 일치했다. |

**원본** · 로컬 `Artifacts/Validation/launcher-two-pc-20261118-185138/`에 화면 9장, 데스크톱 서버·A 로그 4개와 실행 기록 2개를 SHA-256과 함께 보존했다. 노트북 B는 화면과 데스크톱 서버의 입장 로그로 확인했다. 이 실행의 B 원본 로그는 자료에 없다.

<a id="validation-launcher-server-participants"></a>
### 검증 실행기 · 서버 참가자

호스트의 **서버 참가자** 표는 현재 전용 서버가 기록한 슬롯·클라이언트 표식·접속·준비 상태를 표시한다. **이 PC의 프로세스** 영역에는 기존 시작·종료와 로컬 PID를 표시한다. 접속 전용 모드에서는 호스트 실행기에서 참가자를 확인할 수 있다고 안내한다.

실행기가 게임 접속 옵션으로 검증용 A/B 표식을 전달하면 서버가 실제 배정한 슬롯과 `PlayerId`를 기록한다. 표에 표시하는 상태는 서버 원본의 `ValidationParticipants`에서 읽는다. 같은 IP의 두 클라이언트를 슬롯과 참가자 ID로 구분하고, 표시용 표식이 없거나 유효하지 않으면 `표식 없음`으로 보여준다. 서버는 입장·슬롯 배정을 마친 뒤 선택된 슬롯에 표식을 기록한다.

서버는 입장·준비 변경·연결 종료에서 바뀐 두 슬롯의 상태를 함께 남긴다. 실행기는 현재 서버 로그와 Round ID로 대조하고, 다음 맵 이동 중에는 상태를 비운다. 서버를 종료하면 참가자 표를 비운다. 준비 변경과 연결 종료는 서버가 관측한 시점에 반영한다.

#### 검증

UE 5.8.4에서 Win64 Development 전용 서버와 클라이언트를 새로 빌드했다. 새 Stage에 두 실행 파일을 배치했고, 기존 맵의 Cook 컨테이너 10개가 이전 패키지와 일치하는지 SHA-256으로 대조했다. 새 패키지의 `Launcher/Open-CoopValidationLauncher.cmd`에서 실행한다.

| 조건 | 확인 결과 |
|---|---|
| B 선접속·같은 IP | B가 먼저 P1, A가 뒤이어 P2를 받았다. 두 클라이언트는 같은 PC의 루프백 주소를 사용했고 서버가 서로 다른 `PlayerId`를 기록했다. |
| 준비·종료·재접속 | B가 준비를 완료한 상태에서 A가 나가면 P2가 비워졌다. 표식 없는 클라이언트가 들어오면 해당 슬롯에 `표식 없음`을 표시했다. 이후 A 재접속은 새 `PlayerId`와 준비 전 상태로 표시됐고, A도 준비하면 두 상태가 준비 완료로 바뀌었다. |
| 맵 이동·서버 재기동 | 짧은 타임아웃을 만드는 시험 명령으로 맵을 바꿨다. 새 Round ID에서 B·A 표식과 슬롯을 확인했고 준비 값은 초기화됐다. 실행기에서 서버를 종료하면 표가 비워졌고, 재기동한 서버에는 새 회차의 참가자만 표시됐다. |
| 호스트 화면·로그 열기 | B를 다른 실행기 로그 폴더에서 띄웠다. 호스트의 로컬 B 행은 중지이고 서버 참가자 표에는 B가 접속 중으로 표시됐다. 호스트의 **이번 로그 보기**에서 데스크톱의 서버와 A 원본을 열었다. |
| 화면 배치 | 1160×700과 795×596에서 슬롯·클라이언트·접속·준비 텍스트, 두 줄 프로세스 상태와 버튼이 잘리거나 겹치지 않는지 확인했다. 로그 집계·WinForms 배치 55개, 로그 뷰어 읽기 83개·병렬 53개·내보내기 25개를 통과했다. |

**근거** · 로컬 `Artifacts/Validation/launcher-participants-20261118-142048/`의 `before-two-pc.png`, `before-server.log`, `after-wide.png`, `after-narrow.png`, `PackagedRoster/server.log`, `PackagedRoster/roster-results.json`, `FinalLauncher/after-live.png`, `FinalLauncher/Logs/`와 `Package/package-manifest.json`. 넓고 좁은 창은 고정 입력으로 비교했다. 이번 표식·참가자 표시의 실제 게임·실행기 시험은 같은 Windows PC에서 수행했다. 데스크톱·노트북 두 대의 앞선 수동 시험은 [실행기 기동·종료·재접속 기준](#validation-launcher-two-pc)으로 남겨 둔다.

<a id="validation-launcher-settings"></a>
### 검증 실행기 · 실행·접속 설정 · 2026-11-19

창을 **실행·접속 설정 → 이 PC의 프로세스 → 서버 참가자** 순서로 배치했다. UDP 포트 입력을 모드와 IPv4 입력 높이에 맞췄다. 좁은 창에서 서버 상태의 세 번째 줄이 잘리는 것을 확인하고 최소 창 높이를 650px로 조정했다.

| 모드 | 기본 IPv4와 동작 |
|---|---|
| 로컬 2인 | `127.0.0.1`은 이 PC 안의 주소다. **실행**으로 전용 서버와 A·B를 기동한다. |
| Tailscale 호스트 | **내 PC IPv4**에서 활성 Tailscale 주소가 하나면 그 주소를 선택한다. 주소 후보가 여러 개이면 **어댑터**에서 이름과 IP를 보고 바꿀 수 있다. **실행**으로 서버와 A를 기동한다. |
| 접속 전용 | **직접 입력**에 호스트의 IPv4와 UDP 포트를 넣고 A 또는 B의 **접속**을 누른다. |

IPv4 입력칸은 **직접 입력**일 때 수정할 수 있다. UDP 포트는 기본값 `7777`이며 서버와 A·B에 같은 번호를 사용한다. **찾아보기**는 게임 패키지 폴더와 내부 `Stage`를 모두 받는다. 호스트는 Server·Client 실행 파일을 확인하고 접속 전용은 Client를 확인한다. 마지막으로 선택한 패키지와 모드·주소 선택·직접 입력값·포트는 PC의 사용자 설정에 저장한다. 실행 중인 회차를 다시 열면 실제 회차의 주소와 포트를 표시하며, 해당 회차의 프로세스를 종료하면 패키지를 바꿀 수 있다. 저장된 경로가 사라졌다면 창에서 다른 폴더를 선택할 수 있다.

#### 검증 결과

| 검증 | 관측 |
|---|---|
| 배치·설정 50개 판정 | 1160×700과 795×650에서 설정 박스, IPv4·UDP 포트 정렬, 경로와 버튼, 세 줄 프로세스 상태와 영역 순서를 확인했다. 폴더 변경·취소·다시 열기, 잘못된 주소와 사라진 경로에서 복구, 접속 전용 Client 패키지 선택을 확인했다. |
| 기존 Win64 패키지 13개 판정 | 로컬 2인에서 서버와 A·B가 스테이지 1에 입장했고, 로그 뷰어가 세 원본을 읽었다. 실행기를 다시 열자 저장된 환경설정에 다른 모드와 포트가 있어도 실행 중인 동일 서버 PID·주소·포트가 복원됐다. 기동 중에는 패키지 찾기가 잠겼고 종료 후 다시 활성화됐다. |
| 호스트·접속 전용 | 같은 Windows PC에서 실제 Tailscale 어댑터의 IPv4와 별도 UDP 포트로 서버와 A를 기동했다. 독립 로그 폴더의 접속 전용 B가 입장했다. 호스트에서 로컬 B는 중지이고 서버 참가자에는 B가 표시됐다. |
| 게임·실행기 파일 대조 | 시험에 사용한 두 Stage 복사본의 Server·Client 실행 파일 SHA-256이 기존 UE 5.8.4 Win64 Development 패키지와 일치했다. 실행기 ZIP을 다시 풀어 소스 파일 10개와 대조했다. |

**전후 화면** · 로컬 `Artifacts/Validation/launcher-settings-20261119-214507/`의 `layout-comparison.png`에서 비교한다. 넓은 창은 `before-wide.png`와 `after-wide.png`, 좁은 창은 `before-narrow.png`(795×596)와 `after-narrow.png`(795×650)다. 가상 Wi-Fi IP와 고정 프로세스 상태를 넣어 배치를 비교했고, 실제 접속 화면은 `Live-aa72bba0/local-live.png`와 `Live-aa72bba0/host-live.png`에 남겼다.

**원본** · 같은 폴더의 `settings-layout-checks.txt`, `Live-aa72bba0/checks.txt`, `Live-aa72bba0/{LocalLogs,HostLogs,ClientLogs}/`, `existing-binaries.json`. 이번 실행기 버전의 실제 접속 시험 범위는 Windows PC 한 대다. [데스크톱·노트북의 기존 기동 기준선](#validation-launcher-two-pc) · [후속 시각 검토와 보완](#validation-launcher-visual-review)

<a id="validation-launcher-visual-review"></a>
#### 후속 시각 검토와 보완 · 2026-11-19

사용자가 이전 넓은·좁은 화면에서 주소가 달라 보인다고 지적했다. 이전 배치 시험은 가상 Wi-Fi 주소 `172.19.1.4`를 선택하고 프로세스 행에도 같은 시험값을 넣었다. 실제 호스트 원본과 현재 Tailscale 어댑터의 주소는 `100.117.79.28`로 일치했다. 이전 검사에서 795px 창 안쪽 테이블의 좌표는 통과했지만, 부모 영역에서 오른쪽 `실행`과 `찾아보기`가 잘리는 것을 놓쳤다. 이전 시험 결과는 보존하고 좁은 창의 시각 판정을 이 기록에서 정정한다.

- 설정 조작을 `모드`, `서버 IPv4·주소 선택·어댑터`, `UDP 포트·실행`으로 묶었다. 좁으면 마지막 묶음을 다음 줄로 보낸다. 패키지 행에서는 `찾아보기`의 폭을 확보했다.
- 자동 IPv4 아래에는 실제로 선택한 어댑터 이름을 표시한다. 패키지는 폴더 이름을 먼저 보여주고 전체 경로는 툴팁과 접근성 설명에 둔다.
- 프로세스 정보는 상태, 주소·PID·시작 시각·스테이지, 오류 사유 순서로 배치했다. 호스트 화면에서 B의 로컬 행은 `이 PC에서 미실행`, 서버 참가자 행은 `접속 중`으로 표시된다.

| 검증 | 결과 |
|---|---|
| 배치와 상태 153개 판정 | 1160×700 → 795×650 → 1160×700으로 같은 설정 창을 전환했다. 버튼의 선호 크기와 부모 영역 경계를 함께 확인하고, 실제 렌더링에서 버튼 라벨·테두리와 하단 로그 버튼을 검토했다. 어댑터, 패키지 저장·복원, 잘못된 IPv4 오류도 재검증했다. |
| 기존 패키지 19개 판정 | Windows PC 한 대에서 로컬 2인, UDP 포트 충돌 안내와 해제 후 재기동, Tailscale 호스트와 별도 실행기의 B 입장, 실행기 복원과 로그 뷰어를 확인했다. 호스트 PID `17420`, Round ID `6C778D02…`, UDP `56868`을 유지한 채 전후 두 폭을 캡처했다. 양쪽 스크립트에서 같은 서버 회차와 A·B를 확인했다. |
| 125% 확대 | 두 모니터의 Windows 배율은 100%(96 DPI)였다. 검증 실행기 안에서 컨트롤과 글꼴을 125% 크기로 확대해 좁은 창을 재현했고 버튼 전체와 IPv4 오류 사유를 확인했다. Windows 디스플레이 배율을 바꾸는 시험은 하지 않았다. |

**전후 화면과 원본** · 로컬 `Artifacts/Validation/launcher-visual-review-20261119-071736/`에서 보존했다. `Live-58644f30/comparison.png`는 동일한 실제 회차의 `before-wide.png`, `after-wide.png`, `before-narrow.png`, `after-narrow.png`를 한 장에 비교한다. 같은 폴더의 `live-failure-narrow.png`에는 실제 UDP 충돌 사유가 보인다. `Live-58644f30/same-session.json`과 `HostLogs/`에서 회차와 서버 원본을 확인할 수 있다. 설정 배치의 시험용 주소는 `settings-wide.png`·`settings-narrow.png`에 쓰였고, 배율 모의 화면은 `settings-narrow-125-simulated.png`다. 판정 원본은 `settings-layout-checks.txt`와 `Live-58644f30/checks.txt`다.

<a id="validation-launcher-alignment"></a>
#### 실행기 설정 정렬과 패키지 경로 복원 · 2026-11-19

사용자가 넓고 좁은 창에서 입력란의 중심, 패키지 행과 실행 행의 좌우 정렬, 미실행 행과 하단 여백이 어긋난다고 지적했다. 직전 시험의 잘림 판정은 충족했지만 설정 그룹들이 서로 다른 기준으로 놓여 있었다. 실제 캡처를 다시 비교해 두 행을 같은 열에 놓고 정렬을 검증했다.

- 실행·접속 설정은 **모드 │ 서버 IPv4와 주소 선택 │ UDP 포트와 실행**으로 구분했다. 패키지는 아래에 구분선을 두고 전체 경로를 읽기 전용 입력란에 복원했다. 모드와 경로 입력란의 왼쪽 끝, 실행과 찾아보기의 오른쪽 끝을 일치시켰다.
- **어댑터** 버튼과 어댑터 이름 표시를 제거했다. Tailscale 호스트에서는 활성 Tailscale 주소가 하나면 자동으로 선택한다. 자동으로 하나를 정할 수 없는 경우에는 **직접 입력**을 안내한다.
- 프로세스는 실행 정보가 없을 때 둘째 줄에 `—`를 표시한다. 하단의 빈 안내 행을 제거하고 기본 창을 낮췄다. 같은 폭에서 창을 더 높이면 프로세스 행 높이에 여유가 생긴다.

| 시험 | 결과 |
|---|---|
| WinForms 배치 94개 | 1160×700 → 795×650 → 795×600 → 1160×700에서 입력·레이블 중심 차이가 1px 이내였고 지정한 왼쪽과 오른쪽 끝이 같은 좌표였다. 정상 화면에서 하단 로그 버튼과 창 내부 하단 간격은 14px였다. 오류 사유가 추가되면 세로 스크롤로 나머지 화면에 접근할 수 있다. 125%는 실행기 내부 확대 조건으로 시험했다. |
| 기존 패키지 26개 | 한 Windows PC에서 서버·A·B 로컬 기동, 포트 충돌 안내·재기동, Tailscale 호스트·접속 전용 B, 패키지와 프로세스 복원, 로그 뷰어를 확인했다. 호스트 PID `13956`과 Round ID `625D6AFD…`를 유지하면서 넓고 좁은 창과 600px 높이를 확인했다. |

**화면과 원본** · 로컬 `Artifacts/Validation/launcher-alignment-20261119-073851/`의 `layout-checks.txt`, `preview-error-minimum.png`, `preview-125-simulated.png`. `Live-a5bc2219/`의 `before-{wide,narrow}.png`와 `after-{wide,narrow}.png`는 동일한 실제 호스트 회차를 이전·현재 실행기로 열어 찍었다. `after-wide-compact.png`와 `after-narrow-compact.png`는 높이를 600px로 낮춰 확인한 최종 화면이다. `checks.txt`, `same-session.json`, `HostLogs/`에 판정·회차·서버 원본이 있다.

<a id="validation-launcher-compact-default"></a>
#### 기본 창 크기와 넓은 창의 첫 줄 · 2026-11-19

사용자가 좁은 창을 기본으로 선택했다. 실행기는 100% 배율에서 795×600으로 열린다. 창을 넓혀도 첫 줄의 모드·주소·포트 간격은 좁은 창과 같고, 묶음 전체가 오른쪽에 놓인다. `실행`과 `찾아보기`의 오른쪽 끝을 맞췄다.

기존 패키지를 선택한 WinForms 실행기에서 처음 열린 크기와 1160 → 795 → 1000 → 1160px 전환을 확인했다. 실제 Tailscale 주소를 사용해 91개 배치 판정을 확인했고, 시험 주소를 사용한 오류·125% 모의 확대를 포함한 검사는 103개를 통과했다. 캡처와 판정은 로컬 `Artifacts/Validation/launcher-compact-default-20261119-093913/`의 `Rendered/after-default.png`, `Rendered/after-wide.png`, `Rendered/layout-checks.txt`, `layout-checks.txt`에 남겼다.

<a id="validation-launcher-warning-scroll"></a>
#### 종료된 서버에 경고가 남을 때의 스크롤 · 2026-11-19

사용자는 종료된 서버의 네트워크 경고 한 줄이 표시됐을 때, 기본 창에 여유가 있어 보이는데도 참가자 표 하단과 로그 버튼이 스크롤 아래로 밀리는 점을 보고했다. 보존된 별도 PC 종료 회차의 원본으로 같은 증상을 재현했다.

세 프로세스 행에 같은 비율을 주는 WinForms 배치에서, 경고를 포함한 가장 높은 행의 최소 높이가 A·B 행에도 적용됐다. 각 행이 표시하는 내용의 높이로 비율을 계산하고 위아래 여백을 줄였다. 기본 795×600 창에서 전체 내용 높이는 627px에서 561px로 줄어 표시 영역에 들어왔다. 경고는 서버 행에 남고 참가자 표와 로그 버튼이 함께 보였다.

넓혔다가 기본 크기로 돌아오는 전환을 확인했다. 시험용 긴 오류가 실제 표시 영역을 넘기면 스크롤로 하단 버튼에 접근할 수 있었고, 긴 오류를 지우면 기본 창에서 스크롤이 사라졌다. 기존 배치 검사의 103개 판정도 통과했다.

**근거** · 로컬 `Artifacts/Validation/launcher-scroll-fix-20261119-104912/`의 `before-warning-narrow.png`, `after-warning-narrow.png`, `before-measurements.json`, `after-measurements.json`, `Regression/layout-checks.txt`. 비교에 사용한 보존 회차는 [별도 PC 실행기 수동 검증](#validation-launcher-two-pc)의 이전 서버 로그다.

<a id="validation-launcher-two-pc-bundle"></a>
#### 계측 게임과 실행기를 묶은 두 PC 검증판 · 2026-11-19

사용자의 두 PC 재시험에서 A·B 모두 스테이지 1에 입장했다. 서버 원본의 입장은 두 건이었고 `ValidationParticipants`는 0건이었다. 실행기는 갱신했지만 선택한 게임은 기존 `CoopRPG-LAN-c0fed32`였다. 당시 서버 실행 파일의 SHA-256이 참가자 계측 패키지와 달랐다. 실행기와 계측 게임을 따로 전달할 때 생긴 판본 혼선을 줄이기 위해 둘을 같은 ZIP으로 준비했다.

- **묶음 기동** · 두 PC에 `CoopRPG-2PC-Roster.zip`을 풀고 최상단 `Open-CoopRPG-2PC.cmd`를 연다. 이 파일은 같은 폴더의 `Stage`를 지정한다. 제목의 빌드 `roster-20261118`과 참가자 영역의 `계측 지원`을 확인한다. 실행기는 패키지 설명에 적힌 실행 파일 SHA-256과 실제 파일을 대조한다. 정보가 없는 이전 패키지는 `빌드 미확인`과 계측 패키지 선택 안내를 표시한다.
- **주소 복사·복원** · 데스크톱 `Tailscale 호스트`에서 서버를 연 뒤 **주소 복사**로 IPv4와 UDP 포트를 함께 복사한다. 노트북 `접속 전용`에서는 받은 한 줄을 클립보드에 넣고 **주소 붙여넣기**를 누른다. 마지막으로 사용한 값은 Windows 사용자별로 저장돼 실행기 위치가 바뀌어도 접속 전용에서 복원된다. 이전 폴더별 설정은 접속 전용 기록에서 값을 찾는다.
- **미실행 행** · 두 번째 줄에 자리표시를 두지 않고 `이 PC에서 미실행`을 왼쪽 역할 이름과 같은 높이에 놓는다. 실행 이력이 있으면 주소·PID·시각을 표시한다.

**검증** · ZIP을 풀어 92개 파일을 대조했다. 한 Windows PC에서 Tailscale 호스트·A와 별도 실행기의 B를 실제로 접속시켜 참가자 표의 A/P1·B/P2와 준비 전 상태를 확인했다. 주소 복사·붙여넣기 이벤트, 다른 설치 설정으로 실행했을 때의 주소 복원, 795×600에서 로컬 B의 한 줄 중앙 정렬을 포함해 11개 판정을 통과했다. 이전 설정 이전·잘못된 입력·빌드 정보 불일치 9개 판정도 통과했다.

**후속 사용자 확인** · 데스크톱과 노트북에서 A·B 참가자와 B 먼저 준비, 양쪽 준비 및 라운드 시작, 새 서버의 양쪽 스테이지 1 입장을 원본에서 확인했다. 정상 종료 지연과 연타 후 취소 표시는 [종료·중복 접속 보완 검증](#validation-launcher-client-lifecycle)에 남겼다.

**원본과 전달 위치** · 로컬 `Artifacts/Validation/launcher-two-pc-bundle-20261119-111604/`의 `CoopRPG-2PC-Roster.zip`, `archive-verification.json`, `previous-two-pc-diagnosis.json`, `input-checks.txt`, `Live-75ed06c8/checks.txt`와 `host-joined.png`. ZIP에 `README-2PC.txt`를 동봉했다.

<a id="validation-launcher-client-lifecycle"></a>
#### 클라이언트 정상 종료·중복 접속 보완 · 2026-11-19

사용자가 이전 `CoopRPG-2PC-Roster`로 두 PC에서 참가자와 준비 상태를 확인한 뒤 두 문제를 제보했다. B를 종료해도 P2가 남았고, B의 접속을 두 번 눌러 둘째 확인을 취소하자 초록색이어야 할 성공 상태가 빨갛게 표시됐다. [증상과 원인](../Cases/ValidationLauncher.md#client-lifecycle-troubleshooting)

| 조건 | 확인한 결과 |
|---|---|
| 수정 전 · 별도 두 PC 및 Tailscale | 11:54·12:00 서버 실행에서 B 종료 뒤 `ConnectionTimeout` 기준 60초로 정리됐다. 12:00 회차의 같은 데스크톱 로그에서 서버의 P2 변경과 A의 `P2=Disconnected` 사이 간격은 약 12ms였다. 11:56 노트북의 취소 화면과 그 실행의 서버 P2 등록을 별도로 대조했다. |
| 수정 후 · Windows 한 PC, 전용 서버와 A·B, 루프백 | 종료 클릭 → 창 닫기 요청 → B의 `Win RequestExit` → 서버 제어 채널 종료 → P2 미접속 복제 순서로 확인했다. 첫 종료에서 클릭 시각과 서버 P2 제거 로그 간격은 약 0.28초다. 실행기의 종료와 참가자 표 갱신을 함께 확인한 시점은 약 1.7초다. B는 같은 Round ID에 새 PID와 PlayerId 258로 돌아왔다. |
| 강제 종료 경로 · 최종 ZIP 해제판 | 시험 중 B의 창만 비활성화해 정상 종료 요청을 전달할 수 없게 했다. 클릭 후 5.21초에 강제 종료를 요청하고 B가 종료됐다. 루프백 서버는 `Cleanup`으로 P2를 비웠다. 이전 두 PC에서 관측한 60초는 해당 Tailscale 실행의 서버 타임아웃 기록이다. |
| 진행 중 B 정상 종료 · 최종 ZIP 해제판 | B만 먼저 준비한 뒤 A도 준비해 서버가 `Playing`에 진입했다. B를 정상 종료하자 서버는 `Aborted / Disconnected / Score=0`을 한 번 확정했고, A의 결과 복제와 화면 표시 로그를 확인했다. |
| 시작 확인 취소·실제 시작 오류 | Win32 취소 코드 1223과 시작 실패를 각각 주입했다. 실행 확인을 기다릴 때 재진입을 시도해 중복 요청을 차단했고, 취소는 하단 안내와 재시도 가능 상태, 시작 오류는 사유와 실패 표시를 확인했다. 재시도로 실제로 뜬 B는 한 프로세스와 서버 P2 한 슬롯을 유지했고 행이 초록색이었다. OS 실행 확인의 실제 클릭은 별도 두 PC에서 확인한다. |

**원본** · 로컬 `Artifacts/Validation/launcher-client-lifecycle-20261119-121543-982/`의 `Observed/`에 이전 11:54·12:00 서버와 데스크톱 A·노트북 B 원본, 종료 후 화면과 연타 취소 화면이 있다. `Live-9a3531cf/`의 `checks.txt`, `b-stopping.png`, `b-disconnected.png`, `b-rejoined.png`와 `Logs/`에 로컬 재현을 남겼다. 최종 ZIP을 새 폴더에 풀어 반복한 시험은 `Live-755eb216/checks.txt`, `playing-b-disconnected.png`와 `Logs/`에 있다. 종료 클릭·요청·완료와 정상·강제 구분은 각 회차의 `launcher-events.jsonl`에서 확인한다. 저장소 검사 6개 결과는 `RepositoryChecks/result.json`에 있다.

**묶음과 남은 시험** · 같은 폴더의 `CoopRPG-2PC-Lifecycle.zip`은 `roster-20261118` 게임과 수정한 실행기를 포함한다. ZIP의 92개 파일을 해시로 대조했고 결과는 `archive-verification.json`에 있다. 두 PC에 ZIP을 각각 새로 풀고 `Open-CoopRPG-2PC.cmd`를 연다. 동봉한 `README-2PC.txt`에 접속 연타·취소, 준비 전 종료·재접속, 진행 중 종료 확인 순서와 남길 원본을 정리했다. 아래에 이 묶음으로 실행한 사용자 시험을 연결했다.

<a id="validation-launcher-client-lifecycle-two-pc"></a>
#### 종료·재접속 및 Windows 취소 · 두 PC 사용자 시험

사용자는 데스크톱에서 Tailscale 호스트와 A를 열고, 노트북에서 B를 연결했다. 데스크톱 실행 `launcher-20261119-132558-868-a52106ef`의 서버 PID는 42940이었다. 서버·A·두 번째 B 로그의 Round ID가 일치한다.

| 항목 | 판정과 근거 |
|---|---|
| 준비 전 B 종료 | **통과.** 서버 로그 769~776행에서 B의 정상 연결 종료 신호와 P2 미접속을 확인했다. 신호 수신에서 서버의 슬롯 갱신까지 84ms, 같은 데스크톱의 A가 상태를 받기까지 추가 11ms였다. 참가자 표와 A HUD에 미접속이 표시됐다. |
| 같은 서버 재접속·준비 | **통과.** B의 PID는 18072에서 12304로, 서버의 PlayerId는 257에서 258로 바뀌었다. 같은 Round ID에서 B만 먼저 준비했고, A가 준비하자 라운드가 시작됐다. |
| 진행 중 B 종료 | **통과.** 노트북 실행기는 B의 `WindowClose` 요청과 완료를 기록했다. 클릭부터 완료까지는 노트북 시계로 약 2.55초였다. 서버는 정상 연결 종료로 P2를 비우고 접속 중단·0점을 한 번 기록했다. 데스크톱 A 원본과 `ROUND ABORTED` 화면이 일치했다. 시험 회차의 서버 로그에는 연결 타임아웃이 없었다. |
| 중복 입력 잠금과 성공 상태 | **통과.** 사용자는 노트북에서 첫 클릭 직후 버튼이 잠겨 둘째 클릭을 할 수 없었고 Windows 확인 창이 한 번 떴다고 확인했다. 정상 접속 화면의 초록색 `접속 완료`와 B PID 한 개, 종료 전후 서버의 P2 입장 각 한 건을 대조했다. |
| Windows 확인 취소 | **표시 개선.** B가 뜨기 전에 취소하면 노트북 실행기가 빨간 `시작 실패`와 Windows의 취소 문구를 표시했다. 취소 후 접속 재시도는 성공했다. 로컬 시험은 Win32 예외가 보존되는 경우를 주입했다. Windows PowerShell 5.1의 실제 `Start-Process` 셸 실행 경로는 Win32 예외 메시지만 새 `InvalidOperationException`에 넣고 원본 예외와 코드를 넘기지 않는다. 현재 판별기는 예외 체인의 코드 1223을 검사하므로 취소가 일반 시작 실패로 처리됐다. |

**근거** · 로컬 `Artifacts/Validation/launcher-client-lifecycle-20261119-121543-982/TwoPC-20261119-132558/`의 `Desktop/` 서버·A·실행기 로그, `Laptop/`의 B 재접속 원본과 종료 기록, `Desktop/05-aborted-score-zero.png`, `Laptop/03-cancel-red.png`, `Laptop/04-connected.png`. 종료 시간은 동일 장비의 로그끼리 비교했다.

<a id="validation-launcher-start-canceled"></a>
#### 실행 확인 취소 표시 보완 · 로컬

사용자는 취소 후 재시도가 성공한 결과를 확인했고, 취소 안내의 표현만 보완하기로 했다. 실행기는 Windows 셸을 사용하는 `Process.Start`로 게임을 시작해 원본 Win32 예외를 받는다. 코드 1223이면 해당 행에 중립색 `실행 취소`를 표시하고 접속 버튼을 다시 활성화한다. 실제 시작 오류에는 빨간 `시작 실패`와 Windows가 준 사유를 표시한다.

Windows PowerShell 5.1에서 전용 서버와 A를 실제 실행하고, B의 셸 실행 지점에 Win32 취소 코드 1223을 재현했다. B 프로세스·게임 로그 생성 없이 취소가 표시됐다. 없는 실행 파일을 지정한 실제 셸 호출의 코드 2는 빨간색 시작 실패와 오류 사유를 표시했다. 이어 B를 정상 실행해 서버 P2 한 슬롯, B PID와 B 로그 각 한 개, 초록색 접속 완료를 확인했다. 취소 응답은 로컬에서 재현했으며 이 보완에는 별도 PC 사용자 시험을 추가하지 않았다.

**근거** · 로컬 `Artifacts/Validation/launcher-start-cancel-20261119-141616-082/Live-9901366a/`의 `01-canceled.png`, `02-start-failed.png`, `03-connected.png`, `checks.txt`, `Logs/`.

<a id="validation-launcher-live-source-refresh"></a>
#### 열린 로그 뷰어의 늦은 접속과 재접속 · 2026-11-19

#19 리뷰에서 뷰어가 시작할 때 읽은 출처만 감시하고 이후에 시작한 클라이언트의 파일을 놓치는 문제가 확인됐다. 같은 실행 폴더의 `processes.json`을 주기적으로 읽고, 처음 발견한 로그를 읽기 시작한다. 실행기는 교체된 프로세스의 파일을 회차에 기록한다. 뷰어는 파일을 각각 읽으면서 역할별 탭과 분할 선택을 유지한다. 같은 바이트 위치나 Actor 이름은 물리 파일과 세대까지 확인해 사건과 HP 소유자 근거를 구분한다.

- **실제 패키지 · 로컬 Windows PowerShell 5.1** · `roster-20261118` 전용 서버만 있는 상태에서 뷰어를 열었다. 같은 창에서 A, B가 차례로 접속한 뒤 B를 정상 종료하고 같은 서버에 다시 접속시켰다. B 탭에는 재접속 전후 파일의 파싱 대상 15행이 모두 남았다.
- **Excel 원본 대조** · 통합 보기에서 XLSX 50행을 저장했다. 클라이언트 원본의 파싱 대상 25행(A 10, B 종료 전 9, B 재접속 6)이 원본 파일·바이트 위치·원문과 일치했고, 전체 사건 ID가 중복되지 않았다.
- **회귀** · 파서 84개, 단일·분할 UI 53개, Excel 30개를 통과했다. Excel 시험은 같은 바이트 위치의 두 B 로그가 중복 없이 내보내지는지, 새 출처 추가 중 필터와 패널 선택이 유지되는지도 확인했다. 동일 실행 폴더를 대상으로 확인했으며 새 실행 폴더는 실행기의 `이번 로그 보기`에서 연다.

**원본** · 로컬 `Artifacts/Validation/viewer-source-refresh-20261119-153505-126/`의 `Live-e9cdb548/result.json`, `Live-e9cdb548/`의 화면 3장, 해당 회차의 `Exports/`와 원본 로그. 회귀 결과는 `ReaderFinal/`, `Parallel/`, `ExportFinal/`에 남겼다.
