# V01 Windows SDK 혼용과 대응

CoopRPG · 2026-11-07 · UE 5.8.4 · 트러블슈팅 기록

첫 패키징 빌드에서 타깃마다 Windows SDK가 다르게 선택됐다. SDK를 명시한 개별 빌드로 설정을 맞추고 Cook·Stage를 분리했다. 이후 Client 추가에서는 기존 서버·도구를 재사용했다. 11월 15일에는 [CoopRPG에만 SDK를 고정하고 IDE 빌드의 재사용 여부를 확인](#coop-ide-sdk-26100)했다.

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
| 프로젝트 로컬 설정 | 2026-11-15에 `Game/Saved/UnrealBuildTool/BuildConfiguration.xml`에 SDK `10.0.26100.0`을 지정했다. SDK 선택과 증분 빌드 판정은 [후속 검증](#coop-ide-sdk-26100)을 참조한다. |

CoopRPG만 IDE에서 SDK 26100으로 맞추기 위해 위 프로젝트 로컬 XML에 아래 항목을 추가했다. UE 5.8.4의 UBT 소스에서 `WindowsPlatform`의 `WindowsSdkVersion` XML 속성과 `-WindowsSDKVersion` 인자를 확인했다. 저장 위치는 로컬 생성물 경로이므로 새 장비에서는 적용 절차를 다시 수행하고 실제 선택값을 확인한다.

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
2. 새 장비에서는 기존 XML을 보존한 뒤 해당 프로젝트에만 병합한다. Visual Studio의 `Development Editor`가 호출하는 UBT 타깃의 로그와 실제 컴파일 옵션이 `26100`을 가리키는지 확인한다. 기존 엔진을 재사용해야 하면 일반 빌드 전에 보호 조건을 붙여 확인한다.
3. 여러 프로젝트가 같은 소스 엔진의 중간 산출물을 공유한다면, SDK를 바꾸어 빌드했을 때 어느 산출물이 다시 만들어지는지 확인한다. 호환성이 확인된 조합의 재사용을 우선하고, 서로 다른 조합이 필요하면 산출물 분리를 실제 비용에 따라 검토한다.
4. 예상 밖의 대규모 빌드가 잡히면 SDK·엔진 경로·타깃·구성과 로그를 먼저 대조한다. Cook·Stage나 접속 시험 시간은 컴파일 시간과 따로 기록한다.

<a id="coop-ide-sdk-26100"></a>
## 2026-11-15 · CoopRPG 전용 SDK 적용과 빌드 재사용 점검

**판정:** Visual Studio의 `Development Editor`가 요청하는 `CoopRPGEditor`·`ShaderCompileWorker` 양쪽에 SDK `10.0.26100.0`이 적용됐다. 같은 타깃과 `-FromMsBuild`로 UBT를 호출하고, 기존 엔진 산출물의 덮어쓰기를 막는 `-NoEngineChanges`를 추가했다. UBT가 CoopRPG의 XML을 읽었고, 양쪽 타깃의 리소스 컴파일러와 헤더 경로가 `26100`을 가리켰다. 검증 명령에는 SDK 인자를 별도로 전달하지 않았다.

| 확인 범위 | 실제 결과 |
|---|---|
| IDE와 같은 두 타깃 | 6.22초 후 종료 코드 5. 갱신이 필요한 기존 엔진 파일 7,130개(오브젝트 1,936개 포함)를 보고했고, 예정된 컴파일과 링크를 실행하기 전에 중단했다. 파일 개수와 빌드 작업 개수는 다르다. |
| CoopRPG 모듈만 지정 | 3.58초 후 종료 코드 5. Core 등 엔진 중간 산출물에 선행 갱신이 필요해 게임 모듈의 새 빌드도 중단했다. |
| 이전에 검증한 실행 파일 | Editor·Engine·UnrealEd·ShaderCompileWorker 및 게임 DLL의 파일 크기와 수정 시간이 전후 동일했다. 게임 DLL의 SHA-256이 마지막 정상 기동 때와 같았고 엔진과 게임 모듈의 BuildId도 일치했다. 이번 점검에서는 에디터를 다시 띄우지 않았다. |

**근거** · 로컬 실행 `v02-ide-sdk-20261115-131133`의 `IDE-guarded-ubt.log`, `module-guarded-ubt.log` 및 각 `*-result.json` · [이전 에디터 기동 확인](../Validation/Index.md#v02-log-context-original-editor)

로그에는 `ShaderCompileWorker`의 일부 중간 라이브러리가 없거나 미리 컴파일한 헤더가 더 새롭고, Editor의 Core 공통 컴파일 옵션이 `22621`에서 `26100`으로 바뀐 것이 남아 있다. 최종 바이너리는 유지됐지만 이 상태에서 IDE의 일반 빌드가 기존 엔진을 모두 재사용할 수 있다고 판단할 근거는 없다. 기존 DLL로 실행이 필요할 때는 이전에 검증한 에디터를 사용한다. 새 C++ 변경을 반영하려면 엔진 쪽 선행 갱신 범위를 따로 결정한다.

SDK 고정은 이 프로젝트의 Git 제외 `Saved` 아래에만 저장했다. Visual Studio 전역·사용자 UBT·엔진의 설정 XML과 다른 프로젝트 설정을 수정하지 않았다. 다만 UBT가 계획을 만들면서 공유 엔진의 `Intermediate` 내 컴파일 옵션 파일을 다시 썼다. 같은 소스 엔진 경로를 공유하는 다른 프로젝트가 별도 SDK로 빌드하면 중간 산출물을 다시 평가할 수 있다.

## 근거 및 더 읽기

- **사건 원본** — [V01 실패와 재검증](../Validation/Index.md#v01-first-client), [타깃별 빌드 기록](../Validation/Environment.md#v01-source-build). 로컬 실행 ID `v01-20261107-100941`의 `build-isolated.log`와 `CoopRPGEditor-ubt.log`로 SDK 선택 및 재컴파일 내역을 확인한다.
- **후속 검증** — [Client 추가 빌드·쿠킹](../Validation/Environment.md#v01-client-build)과 [2클라이언트 시험](../Validation/Index.md#v01-two-clients). 로컬 실행 ID `v01-client-2p-20261107-190037`의 `Client-build.log`와 `cook-stage.log`에서 두 단계의 시간을 확인한다.
- **더 읽기** — [Epic UBT 설정](https://dev.epicgames.com/documentation/en-us/unreal-engine/build-configuration-for-unreal-engine)은 프로젝트별 설정 위치, [Microsoft Windows SDK 안내](https://learn.microsoft.com/en-us/cpp/windows/how-to-use-the-windows-10-sdk-in-a-windows-desktop-application)는 일반 Visual Studio C++ 프로젝트의 SDK 선택 절차를 설명한다.
