# V01 Windows SDK 혼용과 대응

CoopRPG · 2026-11-07 · UE 5.8.4 · 트러블슈팅 기록

첫 패키징 빌드에서 타깃마다 Windows SDK가 다르게 선택됐다. SDK를 명시한 개별 빌드로 설정을 맞추고 Cook·Stage를 분리했다. 이후 Client 추가에서는 기존 서버·도구를 재사용했다. IDE용 프로젝트 로컬 설정은 아래의 적용 후보로 구분한다.

**근거** · [당시 실패와 재검증](../Validation/Index.md#v01-first-client) · [Client 추가 실측](../Validation/Environment.md#v01-client-build)

## 증상과 원인

| 관찰 | 확인 근거 |
|---|---|
| UAT의 `-ubtargs`로 SDK `10.0.26100.0`을 지정했으나 Game·Server에만 전달됐다. Editor·ShaderCompileWorker·UnrealPak은 `10.0.22621.0`을 선택했다. | 실행 `v01-20261107-100941`의 `build-isolated.log`에서 타깃별 명령과 `Using Visual Studio ... and Windows ... SDK`를 대조했다. |
| SDK를 통일해 재시도하는 과정에서 Editor의 공통 컴파일 입력이 바뀌고 재컴파일 범위가 커졌다. | 같은 실행의 `CoopRPGEditor-ubt.log`와 Core의 이전·현재 컴파일 옵션을 비교해 SDK 헤더 경로가 `22621`에서 `26100`으로 바뀐 것을 확인했다. |
| 성공한 Editor 빌드는 3,724개 작업, 3,703.87초였다. | [타깃별 실측표](../Validation/Environment.md#v01-source-build) |

약 62분은 성공한 Editor 빌드 한 번의 시간이다. SDK 변경이 추가한 시간만 따로 측정한 비교 시험은 없다. 당시 빌드 경로 변경과 최대 4개 병렬 작업 등 다른 조건도 있었다. 앞서 설치형 에디터가 잡고 있던 DLL 때문에 원본 경로에서의 첫 실행이 중단된 건 별도 사건이다. 이번 자료는 두 SDK 중 하나의 결함을 입증하지 않는다.

**근거** · [초기 실패와 환경 조건](../Validation/Index.md#v01-first-client)

## 실제 대응과 확인

타깃별 `Build.bat` 호출에 `-WindowsSDKVersion=10.0.26100.0`을 직접 적용했다. 빌드 로그에서 사용한 SDK를 확인한 뒤, 완료된 실행 파일을 대상으로 UAT의 Cook·Stage를 실행했다. 이후 Client 추가에서는 같은 SDK로 Client만 빌드하고 UAT에 `-skipbuild -skipbuildeditor`를 사용했다. WindowsClient 패키지 두 개가 기존 서버에 접속해 자기 Pawn 이동과 상대 이동을 확인했다.

**근거** · [검증된 빌드 명령](../Validation/Environment.md#v01-source-build) · [Client 빌드·쿠킹](../Validation/Environment.md#v01-client-build) · [2클라이언트 시험](../Validation/Index.md#v01-two-clients)

## Visual Studio와 병렬 프로젝트의 관리안

설치된 Windows SDK는 컴파일에 사용할 Windows 헤더·라이브러리 등을 제공한다. MSVC 도구 체인, SDK, UE 버전은 별도 항목이다. 일반 MSBuild C++ 프로젝트는 `WindowsTargetPlatformVersion`을 설정할 수 있다. CoopRPG의 생성된 `.vcxproj`에서 실제 솔루션 빌드는 `Build.bat`를 호출하므로 UBT가 선택한 SDK를 빌드 출력으로 확인한다. 생성된 `.vcxproj`나 `.vsconfig`의 값만으로 컴파일에 쓰인 SDK를 판정하지 않는다.

**근거** · [Microsoft의 Windows SDK 설정 안내](https://learn.microsoft.com/en-us/cpp/windows/how-to-use-the-windows-10-sdk-in-a-windows-desktop-application) · [Epic의 UBT 설정 위치](https://dev.epicgames.com/documentation/en-us/unreal-engine/build-configuration-for-unreal-engine)

| 확인 항목 | CoopRPG에서 확인한 값과 상태 |
|---|---|
| 소스 엔진과 CLI | UE 5.8.4, MSVC 14.44.35216, 명시 SDK 10.0.26100.0으로 V01 빌드 성공 |
| 기존 IDE 산출물 | `Game/Intermediate/Build/Win64/x64/UnrealEditor/` 아래 Development와 DebugGame의 `CoopRPG/CoopRPG.Shared.rsp`에 `10.0.22621.0` 헤더 경로가 남아 있음 |
| 프로젝트 로컬 설정 | `Game/Saved/UnrealBuildTool/BuildConfiguration.xml`에 SDK 지정이 없는 상태를 읽기 확인함. 아래 설정 적용 후 새 IDE 빌드는 미검증 |

CoopRPG만 IDE에서 SDK 26100으로 맞출 경우, 위 프로젝트 로컬 XML의 기존 설정에 아래 항목을 병합하는 것을 후보로 둔다. UE 5.8.4의 UBT 소스에서 `WindowsPlatform`의 `WindowsSdkVersion` XML 속성과 `-WindowsSDKVersion` 인자를 확인했다. 저장 위치는 로컬 생성물 경로이므로 새 장비에서는 적용 절차를 다시 수행하고 실제 선택값을 확인한다.

**근거** · [Epic UBT 설정 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/build-configuration-for-unreal-engine)

```xml
<?xml version="1.0" encoding="utf-8"?>
<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">
  <WindowsPlatform>
    <WindowsSdkVersion>10.0.26100.0</WindowsSdkVersion>
  </WindowsPlatform>
</Configuration>
```

1. 프로젝트별로 엔진 버전·경로, 타깃과 구성, MSVC와 SDK, 확인한 로그 위치를 기록한다. 다른 프로젝트에는 해당 엔진이 지원하고 기존 검증에서 사용한 조합을 기준으로 삼는다.
2. IDE 설정을 적용한다면 기존 XML을 보존한 뒤 해당 프로젝트에만 병합한다. Visual Studio에서 `Development Editor | Win64`의 CoopRPG를 빌드하고 출력의 SDK 선택값을 확인한다. 로그와 컴파일 옵션이 `26100`을 가리킬 때 적용을 확인한 것으로 기록한다.
3. 여러 프로젝트가 같은 소스 엔진의 중간 산출물을 공유한다면, SDK를 바꾸어 빌드했을 때 어느 산출물이 다시 만들어지는지 확인한다. 호환성이 확인된 조합의 재사용을 우선하고, 서로 다른 조합이 필요하면 산출물 분리를 실제 비용에 따라 검토한다.
4. 예상 밖의 대규모 빌드가 잡히면 SDK·엔진 경로·타깃·구성과 로그를 먼저 대조한다. Cook·Stage나 접속 시험 시간은 컴파일 시간과 따로 기록한다.

## 근거 및 더 읽기

- **사건 원본** — [V01 실패와 재검증](../Validation/Index.md#v01-first-client), [타깃별 빌드 기록](../Validation/Environment.md#v01-source-build). 로컬 실행 ID `v01-20261107-100941`의 `build-isolated.log`와 `CoopRPGEditor-ubt.log`로 SDK 선택 및 재컴파일 내역을 확인한다.
- **후속 검증** — [Client 추가 빌드·쿠킹](../Validation/Environment.md#v01-client-build)과 [2클라이언트 시험](../Validation/Index.md#v01-two-clients). 로컬 실행 ID `v01-client-2p-20261107-190037`의 `Client-build.log`와 `cook-stage.log`에서 두 단계의 시간을 확인한다.
- **더 읽기** — [Epic UBT 설정](https://dev.epicgames.com/documentation/en-us/unreal-engine/build-configuration-for-unreal-engine)은 프로젝트별 설정 위치, [Microsoft Windows SDK 안내](https://learn.microsoft.com/en-us/cpp/windows/how-to-use-the-windows-10-sdk-in-a-windows-desktop-application)는 일반 Visual Studio C++ 프로젝트의 SDK 선택 절차를 설명한다.
