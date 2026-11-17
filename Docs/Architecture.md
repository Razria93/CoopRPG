# 최소 맵의 입력과 서버 판정

CoopRPG · 2026-11-15 · UE 5.8.4 · 기본 이동과 고정 표적 GAS 단타

## 한 번의 입력이 이동이 되기까지

1. 기본 맵 `L_Movement`가 열리면 게임 모드가 `PlayerStart`에 기본 캐릭터를 생성하고 `APlayerController`를 상속한 `ACoopRPGPlayerController`가 이를 조작한다. [게임 시작 설정](../Game/Config/DefaultEngine.ini) · [기본 Pawn 선택](../Game/Source/CoopRPG/GameFramework/CoopRPGGameMode.cpp)
2. 각 로컬 플레이어의 Enhanced Input 서브시스템이 프로젝트 기본 매핑인 `IMC_Player`를 적용한다. 이동과 공격 키를 이 에셋에서 관리한다. W/S는 `IA_Move`의 Y축, D/A는 X축에 각각 ±1을 더한다. [입력 설정](../Game/Config/DefaultInput.ini) · [키 매핑](../Game/Content/Input/IMC_Player.uasset)
3. 빙의한 캐릭터의 `SetupPlayerInputComponent`가 액션을 `Move`에 연결한다. 고정 카메라를 기준으로 화면 위쪽은 월드 +X, 오른쪽은 +Y다. `Move`는 `AddMovementInput`으로 방향과 세기를 보낸다. `CharacterMovement`가 이를 소비해 가속·속도 제한·충돌을 처리한다. [캐릭터 구현](../Game/Source/CoopRPG/Character/CoopRPGCharacter.cpp)

<a id="manual-validation-input"></a>
### 수동 검증의 창과 입력 책임

런처는 운영체제 창의 위치·전면 요청·선택적인 항상 위를 담당하고, `ACoopRPGPlayerController`는 로컬 입력 모드를 담당한다. `-CoopManualInput`을 준 개발 실행에서만 커서를 보이고 마우스 잠금을 해제한다. 이 플래그는 수동 런처가 클라이언트에만 전달하며 복제하거나 서버의 전투 규칙에 사용하지 않는다.

포커스를 잃을 때 `UGameViewportClient`가 `FlushPressedKeys`를 호출하도록 명시한다. 엔진의 Enhanced Input 초기화 뒤 첫 왼쪽 누름과 대응하는 뗌을 소비해, 창을 선택한 클릭이 GAS 공격으로 연결되지 않게 한다. 다음 클릭은 기존 `IA_Attack → Character::Attack → ASC` 경로로 전달한다. 기본 실행과 Shipping/Test는 이 수동 분기를 사용하지 않는다. 현재 고정 카메라 검증을 위한 정책이며 향후 마우스 시점 조작의 제품 UX를 확정한 것은 아니다.

**근거** · [플레이어 컨트롤러](../Game/Source/CoopRPG/GameFramework/CoopRPGPlayerController.cpp) · [런처](../Tools/Validation/Start-CombatManual.ps1) · [실제 창 전환 시험](Validation/Index.md#v02-manual-input)

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

기본 이동은 독립 클라이언트 2개에서 자기 Pawn 조작과 상대 이동을 확인했다. 고정 표적 단타의 코드 경계는 아래에 정리하고 실행 결과는 검증 색인에서 관리한다.

**근거** · [2클라이언트 이동 시험](Validation/Index.md#v01-two-clients) · [게임 규칙](Project.md#game-rules)

### 클라이언트는 행동을 요청하고 서버는 결과를 확정한다

| 책임 | 예시 |
|---|---|
| 소유 클라이언트 | 입력을 받고 상호작용 대상을 고른다. 필요하면 예측과 UI를 표시한다. |
| 서버 | 연결과 객체 소유권을 확인하고 서버가 보유한 현재 라운드·위치·상태로 허용 여부를 판단한다. 승인된 요청만 게임 상태에 반영한다. |
| 다른 클라이언트 | 전달받은 게임 상태를 표시한다. 자신의 표현을 위해 필요한 복제 정보만 받도록 설계한다. |

서버 RPC의 실행 위치와 소유 연결은 요청의 경로를 정한다. 서버는 그 경로를 거친 요청의 대상·거리·수량 등도 별도로 검증해야 한다. `HasAuthority()`는 해당 액터의 권한을 확인하며, 로컬 입력을 받는 주체인지는 `IsLocallyControlled()` 등으로 확인한다. 두 조건은 리슨 호스트에서 함께 성립할 수 있어도 서로 다른 판단이다.

**근거** · [Epic 네트워킹 개요](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-overview-for-unreal-engine) · [Epic RPC와 소유 연결](https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-procedure-calls-in-unreal-engine)

<a id="gas-basic-attack"></a>

### 고정 표적 단타와 서버 취소

1. 소유 클라이언트에서 `IMC_Player`의 왼쪽 클릭이 Boolean 액션 `IA_Attack`을 시작한다. 캐릭터는 이동과 같은 에셋 참조 방식으로 액션을 읽고 `Started`를 `Attack`에 바인딩한다. 처음 누를 때 `ACoopRPGCharacter::Attack`이 Pawn의 ASC에 단타 실행을 요청한다. 서버가 Pawn 시작 시 `ServerOnly` Ability를 부여한다. Pawn을 기준으로 Owner와 Avatar를 초기화하고 컨트롤러가 바뀌면 ActorInfo를 갱신한다.
2. 서버 게임 모드가 `L_Movement`에 고정 표적 하나를 생성한다. `UCoopRPGMeleeAbility::ActivateAbility`는 서버의 표적 유효성·HP와 공격자에서 표적까지의 수평 거리를 검사한다. 공격자 HP가 0이거나 표적이 유효하지 않거나 거리가 250 cm를 초과하면 Commit 전에 종료한다.
3. GAS가 비용·쿨다운을 활성화 전에 검사하고 `CommitAbility`에서 다시 확인한다. Commit 성공 시 공격자에 2초 쿨다운 GE와 스태미나 10 비용을 적용하고 서버에서 0.65초 동안 피해 확정을 기다린다. 확정 시 공격자·표적의 상태와 사거리를 다시 검사한 뒤 표적에 순간 피해 GE를 한 번 적용한다. 표적의 HP는 두 클라이언트에, 공격자의 스태미나는 소유 클라이언트에 복제된다.

4. 오른쪽 클릭은 소유 Pawn으로 서버 취소 RPC를 보낸다. 서버는 해당 Pawn의 활성 단타를 취소한다. `EndAbility`가 예약 타이머와 표적 참조를 정리한다. 이미 적용한 비용·쿨다운은 유지된다. 피해 확정 뒤 들어온 요청에는 활성 단타가 없다.

0.65초는 취소를 관찰하기 위한 시험값이다. 기존 사거리 250cm·비용 10·피해 10과 사용자 채택 쿨다운 2초를 사용한다. 같은 공격에서 피해 확정을 두 번 요청해도 대기 상태는 한 번만 소비된다. 실제 충돌 기반 타격, 피격 자동 취소, 장착과 라운드 연계는 각 기능의 검증 단계에서 다룬다.

**근거** · [공격 수명과 취소 시험](Validation/Index.md#v03-attack-lifetime)

플레이어 ASC는 Mixed 복제를 사용한다. 소유자는 활성 GE를 받아 자신의 쿨다운을 확인하고, 관찰자는 태그·Cue 정보를 받는다. 고정 표적 ASC는 Minimal을 유지한다. 실제 첫 시험에서 Minimal의 소유자 쿨다운 정보 누락을 확인해 바꾼 선택이며 서버 판정·캐릭터 ASC 배치·속성의 소유자 복제 조건을 변경하지 않는다. 개발용 초기화와 직접 요청은 별도 `CombatProbe` 컴포넌트와 명시 실행 옵션에 제한한다. [실패와 재검증](Validation/Index.md#v02-cost-cooldown) · [Epic 복제 모드](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/GameplayAbilities/EGameplayEffectReplicationMode)

**근거** · [공통 키 매핑](../Game/Content/Input/IMC_Player.uasset) · [공격 액션](../Game/Content/Input/IA_Attack.uasset) · [입력과 ASC 초기화](../Game/Source/CoopRPG/Character/CoopRPGCharacter.cpp) · [서버 승인과 거절](../Game/Source/CoopRPG/AbilitySystem/Abilities/CoopRPGMeleeAbility.cpp) · [속성 복제](../Game/Source/CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.cpp) · [입력 통합 후 두 클라이언트 결과](Validation/Index.md#v02-attack-input-regression) · [Epic AttributeSet 설명](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-attributes-and-attribute-sets-for-the-gameplay-ability-system-in-unreal-engine)

단타의 호출 순서, GA 등록·비용·피해 GE, 속성 매크로와 복제는 [CoopRPG의 GAS 단타 구현 읽기](Learning/GASMeleeAttack.md)에 정리했다.

#### ASC 배치와 전투 상태의 수명

CoopRPG는 사용자 결정에 따라 `ACoopRPGCharacter`에 ASC를 둔다. 현재 `InitAbilityActorInfo(this, this)`는 캐릭터를 `OwnerActor`와 `AvatarActor`로 지정한다. MVP에서는 한 명이라도 사망하면 라운드가 실패하고 라운드 중 부활은 없다. 새 라운드에서 HP·스태미나·쿨다운을 초기화하므로, 전투 상태를 현재 Pawn의 수명에 맞추는 편이 게임 규칙과 일치한다. 새 Pawn을 생성하면 새 ASC에서 시작한다. 기존 Pawn을 재사용하는 방식으로 라운드를 전환한다면 초기화는 전환 시점에 명시적으로 처리해야 한다.

`PlayerState`에 ASC를 두는 방식은 리스폰이나 Pawn 교체 뒤에도 일부 능력·지속 효과·쿨다운을 유지해야 할 때 적합하다. Epic의 Lyra는 `PlayerState`가 ASC를 소유하고 현재 Pawn을 `AvatarActor`로 연결한다. 빙의가 바뀌면 Avatar를 갱신하고, 기존 Pawn에만 부여된 능력과 효과를 정리한다. CoopRPG에서 Pawn 교체 후에도 전투 상태를 유지할 요구가 생기면 이 배치를 다시 검토한다.

**근거** · [CoopRPG 게임 규칙 M1](Project.md#game-rules) · [캐릭터의 ASC 생성과 초기화](../Game/Source/CoopRPG/Character/CoopRPGCharacter.cpp) · [Epic ASC 배치 설명](https://dev.epicgames.com/documentation/unreal-engine/gameplay-ability-system-component-and-gameplay-attributes-in-unreal-engine?lang=en-US) · [Epic Lyra의 PlayerState 구성](https://dev.epicgames.com/documentation/unreal-engine/abilities-in-lyra-in-unreal-engine?lang=en-US#alyraplayerstate)

<a id="상자-상호작용-설계-예시"></a>

### 공용 상자와 적 전리품 처리 설계

두 클라이언트는 같은 상자의 내용물을 동시에 볼 수 있다. 각 클라이언트는 자기 소유의 네트워크 객체를 통해 대상 상자와 아이템을 지정해 요청한다. 서버는 현재 라운드, 요청자의 생존·거리, 대상과 남은 수량을 검사한 뒤, 처음 처리한 유효 요청에 대해 아이템 소비와 개인 지급을 한 처리 흐름으로 확정한다. 거절된 요청은 상자 잔량과 개인 소지량을 바꾸지 않는다. 상자에 남은 아이템은 양쪽에 반영하고, 지급 결과는 수령자의 소지품에 반영한다.

적의 생존→사망 전환은 서버에서 한 번 확정한다. 그 전환에서 생성한 전리품은 공용 상자와 같은 서버 검증·소진 처리 흐름을 사용한다. 획득을 확정하면 팀 전리품 수량을 올려 양쪽 클라이언트에 복제한다. 시험에서는 사망, 전리품 생성, 팀 지급이 각각 한 번인지 확인한다.

**근거** · [상자와 라운드 규칙](Project.md#game-rules) · [V04~V06 시험 기준](Validation/Index.md#active-tests)

### 빌드에서 제외하는 코드와 실행 중 권한

소스 저장소 공개 범위, 실행 파일에 포함되는 코드, 네트워크로 보내는 상태는 각각 관리한다. 클라이언트 배포물에는 컴파일된 코드와 콘텐츠가 들어가므로, 실행 파일과 수신 데이터는 분석될 수 있다고 가정한다. 서버 전용 로직의 빌드 제외, 비밀 키를 서버 환경에서만 관리하는 일, 숨겨야 할 상태를 복제 대상에서 제외하는 일은 각각 다른 노출을 줄인다. 게임 결과의 무결성은 서버가 요청을 검증하고 상태 변경을 통제하는 구조에 달려 있다.

**근거** · [Epic 패키징 설명](https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-your-project) · [Epic 복제 설명](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-overview-for-unreal-engine)

CoopRPG는 현재 하나의 게임 모듈을 공유한다. 필요할 때 빌드 조건이나 서버 전용 모듈을 사용해 배포물에 들어가는 코드를 제한할 수 있다. 기본 `WITH_SERVER_CODE` 값은 Game·Server에서 1, Client에서 0이므로 Game 타깃에도 서버 지원 코드가 포함될 수 있다. 조건부 컴파일의 적용 여부는 타깃과 산출물에서 확인하고, 실행 중 권한과 요청 검증은 기능 시험에서 확인한다.

**근거** · [타깃별 네트워크 컴파일 조건](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-overview-for-unreal-engine) · [CoopRPG 빌드 구조](Learning/UnrealBuildPipeline.md)

## 근거 및 더 읽기

- **현재 구현 근거** — [이동 코드](../Game/Source/CoopRPG/Character/CoopRPGCharacter.cpp)와 [2클라이언트 시험](Validation/Index.md#v01-two-clients)은 입력·Pawn 이동과 상대 관찰의 확인 범위를 보여준다.
- **GAS 구현 근거** — [고정 표적 단타 구현](#gas-basic-attack)은 Pawn ASC의 수명과 입력부터 서버 확정·속성 복제까지의 흐름을 보여준다.
- **ASC 배치 더 읽기** — [Epic Lyra의 PlayerState 구성](https://dev.epicgames.com/documentation/unreal-engine/abilities-in-lyra-in-unreal-engine?lang=en-US#alyraplayerstate)은 Pawn 교체 시 유지하는 상태와 정리하는 상태의 예를 보여준다.
- **설계 근거** — [CoopRPG 게임 규칙](Project.md#game-rules)은 동시 열람, 개인 지급 경합, 적 전리품 팀 지급의 원본이다. [시험 기준](Validation/Index.md#active-tests)은 구현 후 판정에 사용한다.
- **더 읽기** — [Epic RPC 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-procedure-calls-in-unreal-engine)는 소유 연결에 따른 호출 경로, [네트워킹 개요](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-overview-for-unreal-engine)는 권한·복제·빌드 조건, [캐릭터 이동 동기화](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine)는 현재 사용 중인 엔진 이동 경로를 설명한다.
