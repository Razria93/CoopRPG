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

<a id="enemy-combat-first"></a>
### 서버의 적 추격과 피해 처리

서버는 `L_Movement?EnemyCombat`에서 ASC를 가진 적 캐릭터 한 마리를 생성한다. 게임 모드가 선택한 적 또는 고정 표적의 ASC를 기존 공격 GA가 사용한다. V04의 적은 Playing 상태에서 주변 360cm 안의 살아 있는 플레이어 한 명을 탐지한다. 평평한 검증 맵에서는 `CharacterMovement`로 목표 방향을 따라가고 위치는 기존 캐릭터 이동 복제 경로로 전달한다.

서버는 135cm 안에서 0.9초 공격을 준비한 뒤 대상의 생존과 거리를 다시 확인한다. 조건이 맞으면 기존 즉시 피해 GE를 대상 ASC에 적용한다. 라운드가 멈추면 추격과 대기 공격을 정리한다. 적의 ASC 체력이 양수에서 0으로 바뀌면 서버가 즉시 사망 상태를 확정하고 추격·대기 공격·충돌을 정리한다. 먼저 사망을 표시한 뒤 `ACoopRPGEnemyLoot`을 한 번 생성한다. 게임 모드는 사망한 적을 현재 유효 표적에서 제외하므로 이미 시작된 다른 타격도 확정 시점에 유효 대상을 잃는다. 적의 사망 플래그와 전리품의 고유 ID는 각 Actor의 복제로 전달한다. 피격과 회복으로 바뀐 HP는 양쪽에, 회복약 수량은 소유자에게 복제된다.

**근거** · [적 캐릭터](../Game/Source/CoopRPG/Character/CoopRPGEnemy.cpp) · [게임 모드의 표적 선택](../Game/Source/CoopRPG/GameFramework/CoopRPGGameMode.cpp) · [패키지 검증](Validation/Index.md#v04-enemy-first-combat)

<a id="shield-encounter-authority"></a>
### 숲 스테이지 방패 적의 서버 판정

`L_Round_01`의 적은 새 공격 예고를 시작할 때 135cm 이내에서 가장 가까운 생존자를 고른다. 거리가 같으면 낮은 플레이어 슬롯을 고른다. 서버는 대상과 정면 방향을 1.5초 동안 고정한다. 예고가 끝날 때 대상의 생존·거리·정면 범위를 재검사하고, 0.8초 회복 뒤 다음 예고 대상을 다시 고른다. 적이 죽거나 라운드가 끝나면 예고와 대기 중인 공격이 정리된다.

플레이어 기본 공격은 기존 구체 스윕으로 적을 찾는다. 적중 순간 방패가 올라 있고 적의 정면과 공격자 위치의 수평각이 45° 이하면 피해를 막는다. 45°를 넘거나 방패가 내려간 동안에는 기존 피해 처리를 적용한다. 적 공격도 같은 좌우 45° 기준을 사용한다. 예고 대상이 살아 있고 사거리 135cm 안의 정면 범위에 있으면 적중한다.

클라이언트는 방패 적의 발밑에 이중 링을 그린다. 뒤쪽 90°는 비우고 앞에 화살표를 붙인다. 비조명 재질의 기본색은 짙은 차콜이다. 방패를 들면 서버가 복제한 예고 방향과 종료 시각을 사용해 바깥선과 안쪽 선의 앞쪽 90°, 그리고 화살표의 색을 같은 노란색으로 바꾼다. 평상시 링은 적의 방향을 따르며, 적이 사망하면 링을 숨긴다. 링은 바닥의 렌더링 구성요소로만 동작한다. 방패 적 이름표는 적의 화면상 중심에 이름·HP·대상 세 줄을 가운데 정렬한다. 배경과 각 줄 좌표는 예고 상태와 무관하게 고정하며 대상 줄은 예고 중 `Target P1` 또는 `Target P2`, 평상시 `Target —`를 표시한다.

`L_Round_01`의 클라이언트는 비공개 BlackKnight 에셋이 있으면 기존 적 액터에 스켈레탈 메시와 원본 도끼·방패를 표시한다. 대기·추적 이동은 속도에 따라, 방어와 공격은 복제된 예고의 방향과 종료 시각에 따라 재생한다. `Attack01` 클립의 0.400초 포즈를 서버 타격 시각에 맞춘다. 서버는 현재 캡슐과 범위 검사로 피해를 한 번 결정하고 표시 에셋을 로드하지 않는다. 에셋을 제공하지 않은 실행은 기존 도형 표시를 사용한다. [BlackKnight 적용과 확인](Validation/ForestExpedition.md#black-knight-presentation)

[공통 각도와 이름표 검증](Validation/ForestExpedition.md#shield-angle-target-nameplate) · [초기 링 검증](Validation/ForestExpedition.md#shield-ground-ring-verification) · [서버 예고와 대상 선정](../Game/Source/CoopRPG/Character/CoopRPGEnemy.cpp) · [기본 공격 판정](../Game/Source/CoopRPG/AbilitySystem/Abilities/CoopRPGMeleeAbility.cpp)

<a id="상자-상호작용-설계-예시"></a>

<a id="shared-chest-implementation"></a>

### 공용 상자와 적 전리품 구현

두 명이 준비하면 서버 `GameMode`가 `GameState`의 라운드 상태를 Playing으로 바꾼다. 클라이언트는 복제된 현재 상자와 라운드 ID를 사용하고, 자기 소유의 `PlayerController`에서 열람·획득 RPC를 요청한다. 서버는 Playing, 요청 라운드, 지정 상자, 생존, 거리 220 cm와 열람 여부를 확인한다. 각 컨트롤러는 자신의 열람 포인터와 회복약 소지량을 받으므로 같은 상자를 열어도 상대의 열람·소지량을 가져오지 않는다.

`ACoopRPGSharedChest`는 공용 회복약 재고를 두 클라이언트에 복제한다. 서버는 첫 유효 요청에서 재고 1→0과 수령자의 보유량 증가를 같은 RPC 처리 안에서 확정한다. 두 번째 유효 조건의 요청은 소진으로 거절되고, 무효 요청은 재고를 차감하지 않는다. Canvas HUD는 공용 잔량과 자신의 수량을 읽어 표시한다.

**근거** · [상자 재고](../Game/Source/CoopRPG/Loot/CoopRPGSharedChest.cpp) · [입력·서버 판정·소유자 복제](../Game/Source/CoopRPG/GameFramework/CoopRPGPlayerController.cpp) · [2인 패키지 시험](Validation/Index.md#v05-shared-chest-first)

상자에는 같은 근접무기 두 개가 있다. 서버가 첫 유효 획득에서 재고를 한 개 줄이고 요청자 Pawn을 장착 상태로 바꾼다. 두 사람의 HUD와 무기 외형은 복제 상태를 읽는다. 이미 장착한 Pawn의 재요청은 재고를 차감하기 전에 거절한다. 공격 GA는 미장착 요청을 서버에서 비용·쿨다운 커밋 전에 거절한다.

개인 회복약은 소유 `PlayerController`의 서버 RPC로 사용한다. 서버는 재고·라운드·생존·현재 HP를 검사하고, 자기 Pawn의 ASC에 즉시 회복 GE `+30`을 적용한다. HP 상한 100은 `AttributeSet`의 기본값 변경에서 적용하며, HP가 실제로 오른 경우에만 회복약 한 개를 차감한다. HP는 관찰 클라이언트에, 개인 회복약 수량은 컨트롤러 소유자에게 복제한다.

**근거** · [장착 복제](../Game/Source/CoopRPG/Character/CoopRPGCharacter.cpp) · [서버 권한·거절](../Game/Source/CoopRPG/GameFramework/CoopRPGPlayerController.cpp) · [회복 GE](../Game/Source/CoopRPG/AbilitySystem/Effects/CoopRPGRecoveryEffect.cpp) · [2인 패키지 시험](Validation/Index.md#v05-equipment-recovery)

적 전리품은 서버에서 사망 전환당 한 번 생성하며 양쪽 클라이언트가 같은 ID를 받는다. [사망·전리품 생성 검증](Validation/Index.md#v04-death-loot-once)

<a id="enemy-loot-team-design"></a>
#### 적 전리품 획득과 팀 지급

서버 `GameState`는 현재 라운드에 생성된 드롭 한 개를 등록한다. 각 클라이언트의 `T` 입력은 소유 `PlayerController`에서 라운드 ID와 전리품 ID를 보낸다. 서버는 Playing, 두 식별자의 일치, 요청자의 Pawn 생존, 250 cm 거리와 획득 상태를 확인한다. 첫 유효 요청의 처리 안에서 전리품을 획득 상태로 바꾸고 팀 수량을 한 번 올린다. 이후 같은 전리품의 유효 조건 요청은 `AlreadyClaimed`로 거절한다. 획득 상태를 유지한 Actor는 라운드에 남겨 늦게 도착한 요청에도 판정을 내린다.

전리품 Actor의 획득 상태로 양쪽에서 메시를 숨기고, `GameState`의 팀 수량을 두 클라이언트에 복제해 HUD에 표시한다. 두 상태의 도착 순서와 관계없이 서버의 지급 횟수는 한 번이다. 공용 상자의 회복약 소지량은 계속 컨트롤러 소유자에게 복제된다.

**근거** · [요청과 서버 검증](../Game/Source/CoopRPG/GameFramework/CoopRPGPlayerController.cpp) · [라운드 등록·팀 지급](../Game/Source/CoopRPG/GameFramework/CoopRPGGameState.cpp) · [획득 상태 복제](../Game/Source/CoopRPG/Loot/CoopRPGEnemyLoot.cpp) · [V05 실행 검증](Validation/Index.md#v05-enemy-loot-team)

<a id="joint-extraction-design"></a>
### 공동 탈출의 서버 판정과 결과 복제

`GameMode`는 적 전투 모드의 `L_Movement`에 탈출 구역을 스폰한다. 서버는 매 판정 시점에 Playing 여부, 팀 전리품 1개, 두 컨트롤러의 살아 있는 Pawn 중심이 모두 구역 안에 있는지를 검사한다. `GameState`는 조건이 맞기 시작한 시점에 서버 시각으로 3초 기한을 기록하고 조건이 끊기면 초기화한다.

기한까지 조건이 유지되면 `GameState`는 `Result`와 팀 점수 1을 확정하고 진행 기한을 닫는다. 준비 상태 갱신과 이후 판정은 이미 확정된 결과를 다시 열 수 없다. 클라이언트 HUD는 복제된 기한을 서버 시각과 비교해 남은 시간을 표시하고, 복제된 최종 상태에서 성공과 점수를 읽는다.

**근거** · [구역과 서버 검사](../Game/Source/CoopRPG/GameFramework/CoopRPGGameMode.cpp) · [기한·점수 복제](../Game/Source/CoopRPG/GameFramework/CoopRPGGameState.cpp) · [패키지 검증](Validation/Index.md#v06-extraction-success)

<a id="round-outcome-arbitration"></a>
### 라운드 종료 판정과 우선순위

`GameMode`가 Playing에 들어가는 두 컨트롤러를 기록한다. 참가자가 접속을 종료하면 `Logout`에서 이탈 사실을 보존한다. 서버는 이동 및 전투 타이머가 갱신된 뒤 참가자의 생존과 체류를 수집한다. `GameState`가 같은 시점의 서버 시계로 300초 기한과 탈출 기한을 확인해 **접속 종료 → 사망 → 시간초과 → 탈출 성공** 순으로 결과를 고른다. 같은 판정의 후보와 최종 사유는 프레임 번호와 함께 기록한다.

한 회차의 최종 상태는 `Result`, 종료 사유, 점수를 포함한다. 성공은 1점이고 실패·중단은 0점이다. 확정 뒤 새로운 입력이나 참가자 정리로 결과가 바뀌지 않는다. 연결을 유지한 클라이언트는 같은 결과를 복제받아 HUD에 표시한다. 전투와 전리품 획득은 서버의 라운드 상태를 검사한다.

**근거** · [참가자·종료 입력](../Game/Source/CoopRPG/GameFramework/CoopRPGGameMode.cpp) · [우선순위 및 결과 복제](../Game/Source/CoopRPG/GameFramework/CoopRPGGameState.cpp) · [전용 패키지 검증](Validation/Index.md#v06-round-outcomes)

<a id="round-travel-design"></a>
### 서버 주도 맵 이동과 라운드 수명

결과 HUD가 그려지면 각 클라이언트의 `PlayerController`가 현재 `RoundId`를 표시 확인 RPC로 보낸다. `GameMode`는 서버에 기록한 두 참가자와 현재 결과를 대조하고 중복을 제외한다. 이동 목적지가 있는 결과는 두 확인을 받은 뒤 서버 시계로 3초가 지나면 `ServerTravel`을 한 번 요청한다. 참가자가 이탈한 회차는 `Result`에 머문다.

서버는 라운드 종료 사유와 현재 맵으로 이동 목적지를 결정한다. 사망·시간 초과는 현재 맵을 다시 열고, 탈출 성공은 연결된 다음 맵이 있을 때만 진행한다. `L_Round_01`은 후속 원정 맵이 없어 성공 결과에 머문다. 개발 검증용 `L_Movement`와 `L_Movement_Round2`는 성공 시 서로 이동한다. 결과 HUD도 같은 목적지 규칙을 사용한다.

비심리스 이동을 사용해 같은 클라이언트 프로세스가 새 월드에 다시 접속하고 `GameState`, `PlayerController`, Pawn을 새로 만든다. 새 서버 월드는 새 `RoundId`로 상자·적·탈출 구역을 초기화한다. 새 Pawn과 ASC는 초기 체력·스태미나와 쿨다운 상태로 시작한다. 상자 획득은 서버의 현재 `RoundId`를 검사하므로 새 상자를 대상으로 보낸 이전 회차 ID도 거절된다.

**근거** · [서버의 표시 확인과 전환](../Game/Source/CoopRPG/GameFramework/CoopRPGGameMode.cpp) · [표시 확인 RPC와 상자 요청 검사](../Game/Source/CoopRPG/GameFramework/CoopRPGPlayerController.cpp) · [V06 패키지 시험](Validation/Index.md#v06-round-travel) · [숲 재도전 검증](Validation/ForestExpedition.md#forest-round-retry) · [Epic 멀티플레이 맵 이동](https://dev.epicgames.com/documentation/unreal-engine/travelling-in-multiplayer-in-unreal-engine?lang=en-US)

<a id="round-admission-design"></a>
### 전용 서버 입장 판정과 맵 이동 중 두 자리

서버 `GameMode`는 `PreLogin`에서 진행 상태와 2명 정원을 검사하고, `Login`에서 같은 판정을 다시 실행한다. 검사 사이에 다른 클라이언트가 자리를 채우거나 라운드가 시작될 수 있다. 신규 입장은 Playing과 Result에서 거절한다. Waiting에서는 정원과 맵 전환 예약을 검사한다. 결과 확정과 참가자 이탈의 우선순위는 기존 `GameState`가 처리한다.

비심리스 이동 때 `GameInstance`의 서브시스템이 기존 참가자의 연결 ID 두 개를 목적지에 30초간 예약한다. 새 월드는 실제 입장에 성공한 ID의 예약만 해제한다. 이 프로젝트의 `NULL` 연결 ID는 패키지 시험에서 프로세스의 연속성을 비교하는 데 사용했다. 계정 인증과 중단된 라운드 복구는 MVP 범위 밖이다.

클라이언트의 게임 뷰포트는 네트워크 실패 이벤트에서 서버의 입장 거절 사유를 받거나 연결 손실·타임아웃을 감지한다. 안내는 월드 전환 후에도 유지되고 정상 연결이 성립하면 지워진다. 남은 참가자의 `중단·0점`은 별도로 서버가 확정한 라운드 결과다.

**근거** · [서버 입장 판정](../Game/Source/CoopRPG/GameFramework/CoopRPGGameMode.cpp) · [자리 예약 수명](../Game/Source/CoopRPG/GameFramework/CoopRPGRoundTravelSeats.cpp) · [클라이언트 안내](../Game/Source/CoopRPG/GameFramework/CoopRPGGameViewportClient.cpp) · [패키지 검증](Validation/Index.md#v06-round-admission) · [Epic PreLogin](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/AGameModeBase/PreLogin) · [Epic 네트워크 실패 이벤트](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UEngine/FOnNetworkFailure)

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
