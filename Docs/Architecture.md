# 최소 맵의 입력과 캐릭터 이동

CoopRPG · 2026-11-07 · UE 5.8.4 · 첫 이동 구현

## 한 번의 입력이 이동이 되기까지

1. 기본 맵 `L_Movement`가 열리면 게임 모드가 `PlayerStart`에 기본 캐릭터를 생성하고 엔진의 플레이어 컨트롤러가 이를 조작한다. [게임 시작 설정](../Game/Config/DefaultEngine.ini) · [기본 Pawn 선택](../Game/Source/CoopRPG/GameFramework/CoopRPGGameMode.cpp)
2. 각 로컬 플레이어의 Enhanced Input 서브시스템이 `IMC_Player`를 적용한다. W/S는 `IA_Move`의 Y축, D/A는 X축에 각각 ±1을 더한다. [입력 설정](../Game/Config/DefaultInput.ini) · [키 매핑](../Game/Content/Input/IMC_Player.uasset)
3. 빙의한 캐릭터의 `SetupPlayerInputComponent`가 액션을 `Move`에 연결한다. 고정 카메라를 기준으로 화면 위쪽은 월드 +X, 오른쪽은 +Y다. `Move`는 `AddMovementInput`으로 방향과 세기를 보낸다. `CharacterMovement`가 이를 소비해 가속·속도 제한·충돌을 처리한다. [캐릭터 구현](../Game/Source/CoopRPG/Character/CoopRPGCharacter.cpp)

## 멀티플레이를 고려해 분리한 책임

| 데이터 | 책임과 수명 | 이번 선택 |
|---|---|---|
| 어느 키를 눌렀는가 | 키 매핑은 로컬 플레이어, 액션 바인딩은 현재 조작하는 Pawn | 스텔라에서 사용한 입력 축 → 이동 방향 → `AddMovementInput` 흐름을 재사용했다. 이번 고정 카메라에서는 월드 축을 사용한다. |
| 캐릭터가 어디로 움직였는가 | 캐릭터와 이동 컴포넌트 | 멀티플레이 시 소유 클라이언트의 예측, 서버의 이동 처리·보정, 다른 클라이언트의 이동 표시는 엔진 경로에 맡긴다. 키·위치를 중복 전송하는 사용자 RPC는 두지 않았다. |
| 얼마나 빠르게 걸을 수 있는가 | `ACoopRPGCharacter` 생성자가 이동 컴포넌트의 기본 `MaxWalkSpeed`를 설정 | 기준 커밋 `5dd6669`의 값은 **500 cm/s**, 사용자 비교 후 현재 코드 값은 **300 cm/s**다. 같은 빌드의 공통 초기값이다. 이후 달리기·GAS 효과처럼 플레이 중 속도가 달라진다면 서버가 변경 조건을 판정하고 소유 클라이언트 예측과 값을 어떻게 일치시킬지 별도로 설계한다. |

PIE 단일 플레이의 근거는 [이동 검증](Validation/Index.md#l00-movement)에 둔다. 11월 7일 패키지 서버와 첫 클라이언트에서 같은 입력의 이동을 관찰했고 두 프로세스의 XY 최종 좌표가 일치했다. [실행 근거](Validation/Index.md#v01-first-client)

[Server 타깃](../Game/Source/CoopRPGServer.Target.cs)은 같은 게임 모듈을 전용 서버로 빌드하며 기본 서버 맵은 `L_Movement`다. [Client 타깃](../Game/Source/CoopRPGClient.Target.cs)은 같은 모듈을 접속용 실행 파일로 빌드한다. 독립 클라이언트 2개의 자기 Pawn 입력과 상대 이동을 확인했다. [시험 근거](Validation/Index.md#v01-two-clients) 지연·손실 조건에서의 동작은 V08에서 확인한다. [Epic의 캐릭터 이동 동기화 설명](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine)

<a id="server-authority"></a>
## 서버 권한과 신뢰 경계

**현재 확인한 범위는 기본 이동이다.** 두 클라이언트에서 각자 자기 Pawn을 조작했고 상대 이동이 반영됐다. 아래 상자 판정과 GAS 책임은 다음 구현에서 사용할 설계 기준이다. 비정상 요청 차단이나 GAS 승인·거절을 이미 검증한 결과로 해석하지 않는다.

**근거** · [2클라이언트 시험](Validation/Index.md#v01-two-clients) · [게임 규칙](Project.md#game-rules)

### 클라이언트는 행동을 요청하고 서버는 결과를 확정한다

| 책임 | 예시 |
|---|---|
| 소유 클라이언트 | 입력을 받고 상호작용 대상을 고른다. 필요하면 예측과 UI를 표시한다. |
| 서버 | 연결과 객체 소유권을 확인하고 서버가 보유한 현재 라운드·위치·상태로 허용 여부를 판단한다. 승인된 요청만 게임 상태에 반영한다. |
| 다른 클라이언트 | 전달받은 게임 상태를 표시한다. 자신의 표현을 위해 필요한 복제 정보만 받도록 설계한다. |

서버 RPC의 실행 위치와 소유 연결은 요청의 경로를 정한다. 서버는 그 경로를 거친 요청의 대상·거리·수량 등도 별도로 검증해야 한다. `HasAuthority()`는 해당 액터의 권한을 확인하며, 로컬 입력을 받는 주체인지는 `IsLocallyControlled()` 등으로 확인한다. 두 조건은 리슨 호스트에서 함께 성립할 수 있어도 서로 다른 판단이다.

**근거** · [Epic 네트워킹 개요](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-overview-for-unreal-engine) · [Epic RPC와 소유 연결](https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-procedure-calls-in-unreal-engine)

### 상자 상호작용 설계 예시

1. 클라이언트는 자기 소유의 네트워크 객체를 통해 상자 대상을 지정해 상호작용을 요청한다. 라운드 식별값을 받는다면 서버의 현재 라운드와 대조하는 용도로만 쓴다.
2. 서버는 요청자가 현재 라운드에 참여 중인 생존 플레이어인지, 진행 상태가 `Playing`인지, 서버 기준 위치에서 유효한 대상과 거리인지, 상자가 이미 소진됐는지 확인한다.
3. 서버가 상자 소비와 공동 수집량 1 증가를 확정한다. 같은 상자에 두 요청이 들어와도 첫 유효한 처리만 성공하도록 소비 여부와 지급을 한 처리 흐름으로 관리한다.
4. 변경된 상자 상태와 공동 수집량을 필요한 클라이언트에 복제한다. 거절된 요청은 수집량과 소비 상태를 바꾸지 않는다.

   **근거** · [상자와 라운드 규칙](Project.md#game-rules)

구현 후에는 두 클라이언트로 정상 획득, 거리 밖·잘못된 대상 거절, 같은 상자의 동시·반복 요청, 이전 라운드 요청을 확인한다. 서버의 최종 수집량과 두 화면의 표시가 일치하는지 비교한다. 첫 GAS 공격에서도 입력·예측 표현과 서버의 피해·비용 확정을 구분하고, 승인·거절·취소·재실행을 별도로 확인한다.

**근거** · [V02~V03·V04 시험 기준](Validation/Index.md#active-tests)

### 빌드에서 제외하는 코드와 실행 중 권한

소스 저장소 공개 범위, 실행 파일에 포함되는 코드, 네트워크로 보내는 상태는 각각 관리한다. 클라이언트 배포물에는 컴파일된 코드와 콘텐츠가 들어가므로, 실행 파일과 수신 데이터는 분석될 수 있다고 가정한다. 서버 전용 로직의 빌드 제외, 비밀 키를 서버 환경에서만 관리하는 일, 숨겨야 할 상태를 복제 대상에서 제외하는 일은 각각 다른 노출을 줄인다. 게임 결과의 무결성은 서버가 요청을 검증하고 상태 변경을 통제하는 구조에 달려 있다.

**근거** · [Epic 패키징 설명](https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-your-project) · [Epic 복제 설명](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-overview-for-unreal-engine)

CoopRPG는 현재 하나의 게임 모듈을 공유한다. 필요할 때 빌드 조건이나 서버 전용 모듈을 사용해 배포물에 들어가는 코드를 제한할 수 있다. 기본 `WITH_SERVER_CODE` 값은 Game·Server에서 1, Client에서 0이므로 Game 타깃에도 서버 지원 코드가 포함될 수 있다. 조건부 컴파일의 적용 여부는 타깃과 산출물에서 확인하고, 실행 중 권한과 요청 검증은 기능 시험에서 확인한다.

**근거** · [타깃별 네트워크 컴파일 조건](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-overview-for-unreal-engine) · [CoopRPG 빌드 구조](Learning/UnrealBuildPipeline.md)

## 근거 및 더 읽기

- **현재 구현 근거** — [이동 코드](../Game/Source/CoopRPG/Character/CoopRPGCharacter.cpp)와 [2클라이언트 시험](Validation/Index.md#v01-two-clients)은 입력·Pawn 이동과 상대 관찰의 확인 범위를 보여준다.
- **설계 근거** — [CoopRPG 게임 규칙](Project.md#game-rules)은 상자당 1개, 공동 수집, 중복 지급 금지의 원본이다. [시험 기준](Validation/Index.md#active-tests)은 구현 후 판정에 사용한다.
- **더 읽기** — [Epic RPC 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-procedure-calls-in-unreal-engine)는 소유 연결에 따른 호출 경로, [네트워킹 개요](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-overview-for-unreal-engine)는 권한·복제·빌드 조건, [캐릭터 이동 동기화](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine)는 현재 사용 중인 엔진 이동 경로를 설명한다.
