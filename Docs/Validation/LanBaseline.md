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

본가 Windows PC의 전용 서버와 클라이언트 A, 노트북의 클라이언트 B를 `c0fed32` 전달 패키지로 실행했다. 두 클라이언트는 `-CoopManualInput`으로 기동했다. 공유 자료에서 시험 전용 옵션이나 명령은 확인되지 않았다. 본가의 서버·클라이언트 실행 파일 2개의 SHA-256은 전달 매니페스트와 일치했다. 노트북 판본은 접속·기동 로그로 대조했다.

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

사용자는 클라이언트를 다시 열었을 때 스테이지 2가 바로 나오는 점을 확인했다. 첫 완주 직후 서버가 생성한 `601F3CE4…` 회차와 두 클라이언트의 재접속 로그가 일치했다. 기동 스크립트는 전용 서버를 독립 프로세스로 실행하므로 클라이언트 종료 뒤에도 해당 맵이 유지됐다. 사용자는 본가 PC에서 실행 중인 서버를 PowerShell로 조회해 종료했고, 다시 조회했을 때 서버 프로세스가 없음을 확인했다.

이 사례에서 드러난 문제는 검증 중 기존 서버와 새 서버를 쉽게 구분하기 어려웠다는 점이다. 사용자는 실행·종료·현재 상태·실패 이유를 한 창에서 보는 도구를 제안했다. [실행기 완료 기준](../Roadmap.md#validation-launcher)
