# 언리얼 빌드 시스템 이해

CoopRPG · 2026-11-07 · UE 5.8.4 · 학습 문서

빌드할 타깃을 선택하면 Unreal Build Tool(UBT)이 해당 타깃에 필요한 게임·엔진 모듈과 빌드 설정을 파악하고, 기존 결과를 재사용하거나 필요한 코드를 컴파일·링크한다. CoopRPG의 네 타깃과 하나의 게임 모듈을 따라 이 흐름을 읽는다.

## 타깃과 모듈을 읽는 순서

### 1. 프로젝트가 사용할 엔진 찾기

`CoopRPG.uproject`의 `EngineAssociation`이 이 장비의 소스 엔진 `C:/UE/5.8.4`를 가리킨다. 다른 장비에서는 GUID 등록을 확인한다. 명령줄에서는 사용할 엔진의 `Build.bat` 경로를 직접 지정할 수 있다.

**근거** · [프로젝트 파일](../../Game/CoopRPG.uproject) · [엔진 연결 기록](../Validation/Environment.md)

### 2. 타깃에서 산출물의 용도 고르기

`*.Target.cs`는 `TargetRules`를 정의한다. 빌드 명령의 `Win64`와 `Development`는 각각 플랫폼과 구성이다. 타깃 파일에서는 다음 설정을 읽는다.

- `Type`은 Editor·Game·Server·Client 중 용도를 선택한다.
- `ExtraModuleNames.Add("CoopRPG")`는 포함할 프로젝트 모듈을 지정한다.
- `DefaultBuildSettings`와 `IncludeOrderVersion`은 빌드 기본값 묶음과 헤더 포함 순서 기준을 선택한다.

**근거** · [Client 타깃 예시](../../Game/Source/CoopRPGClient.Target.cs) · [Epic 타깃 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-build-tool-target-reference)

### 3. 모듈에서 빌드 의존성 확인하기

`CoopRPG.Build.cs`는 `ModuleRules`로 게임 모듈의 설정과 의존성을 정한다. UBT는 그 관계를 따라 필요한 모듈을 빌드에 포함한다. 예를 들어 캐릭터는 `Engine`의 `ACharacter`를 상속하고, 입력 구현에서는 `EnhancedInput` 타입을 사용한다.

**근거** · [모듈 설정](../../Game/Source/CoopRPG/CoopRPG.Build.cs) · [캐릭터 헤더](../../Game/Source/CoopRPG/Character/CoopRPGCharacter.h) · [입력 구현](../../Game/Source/CoopRPG/Character/CoopRPGCharacter.cpp)

| 타깃 | 용도 | CoopRPG에서의 위치 |
|---|---|---|
| Editor | 엔진 에디터에서 프로젝트 모듈을 로드해 편집·PIE | [CoopRPGEditor.Target.cs](../../Game/Source/CoopRPGEditor.Target.cs) |
| Game | 일반 독립 게임 실행 파일. 초기 V01에서는 서버에 접속하는 클라이언트로 사용 | [CoopRPG.Target.cs](../../Game/Source/CoopRPG.Target.cs) · [초기 시험](../Validation/Index.md#v01-first-client) |
| Server | 로컬 플레이어 없이 접속을 받는 전용 서버 실행 파일 | [CoopRPGServer.Target.cs](../../Game/Source/CoopRPGServer.Target.cs) |
| Client | 서버에 접속하는 플레이어용 실행 파일 | [CoopRPGClient.Target.cs](../../Game/Source/CoopRPGClient.Target.cs) · [2클라이언트 시험](../Validation/Index.md#v01-two-clients) |

네 타깃은 같은 게임 모듈을 포함한다. 타깃은 빌드의 용도와 구성을 표현하고, 모듈은 관련 소스와 의존성을 묶는다. 빌드에 포함되는 코드와 실행 중 판정을 맡는 주체는 각각 타깃 설정과 권한 설계에서 확인한다.

**관련 문서** · [서버 권한과 신뢰 경계](../Architecture.md#server-authority)

## Public·Private 의존성과 산출물

현재 `Core`, `CoreUObject`, `Engine`은 Public 의존성이고 `EnhancedInput`은 Private 의존성이다. 공개 헤더를 쓰는 다른 모듈도 해당 의존성이 필요하면 Public을 고려하고, 구현 내부에서만 쓰면 Private을 우선 검토한다. 공개 헤더에서 전방 선언으로 충분한 타입은 구현 파일에서 실제 헤더를 포함할 수 있다. 여기서 Public·Private은 **모듈 간 빌드 의존성**을 설명한다. 엔진 모듈뿐 아니라 프로젝트·플러그인 모듈도 의존 대상이 될 수 있다.

**근거** · [현재 모듈 설정](../../Game/Source/CoopRPG/CoopRPG.Build.cs) · [Epic 모듈 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-modules)

UBT는 필요한 경우 Unreal Header Tool로 리플렉션 코드를 생성하고, 변경되었거나 아직 없는 산출물을 컴파일·링크한다. IDE에서 빌드해도 생성된 프로젝트의 명령은 UBT를 호출한다. 같은 코드라도 타깃·구성·SDK가 바뀌면 다시 만들어야 할 부분이 생긴다. V01의 Client 첫 빌드에는 1,016개 작업과 1,511.61초가 들었다. 이후 콘텐츠를 WindowsClient용으로 변환하는 Cook과 실행 파일·콘텐츠를 모으는 Stage는 62.53초였다. 접속 시험은 완성된 패키지를 실행하는 별도 단계였다.

**근거** · [Client 빌드·쿠킹 실측](../Validation/Environment.md#v01-client-build) · [Epic 패키징 설명](https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-your-project)

일상적인 코드 확인은 Editor 빌드와 PIE, 독립 실행 검증은 변경에 영향받은 Server·Client 빌드와 해당 콘텐츠 패키징을 기준으로 삼는다. 기존 바이너리를 재사용할 때는 검증 대상 소스·설정과 일치하는지 확인한다.

**관련 문서** · [소스 엔진 채택 이유](SourceEngineDecision.md) · [SDK 혼용 사례](../Cases/V01-WindowsSdkSelection.md)

## 근거 및 더 읽기

- **현재 구현** — [Client 타깃](../../Game/Source/CoopRPGClient.Target.cs)과 [게임 모듈 설정](../../Game/Source/CoopRPG/CoopRPG.Build.cs)에서 타깃이 선택한 모듈과 그 모듈의 의존성을 따라갈 수 있다.
- **실행 근거** — [V01 빌드 기록](../Validation/Environment.md#v01-client-build)과 [2클라이언트 시험](../Validation/Index.md#v01-two-clients)에서 컴파일·패키징·실행 결과를 구분해 확인할 수 있다.
- **더 읽기** — [Epic 타깃 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-build-tool-target-reference)는 타깃 속성, [Epic 모듈 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-modules)는 의존성 공개 범위와 모듈 구조, [Epic 패키징 설명](https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-your-project)은 Cook과 Stage의 역할을 설명한다.
