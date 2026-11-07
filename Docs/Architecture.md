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
