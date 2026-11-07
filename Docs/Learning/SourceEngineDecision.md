# CoopRPG의 소스 엔진 채택 이유

CoopRPG · 2026-11-07 · UE 5.8.4 · 환경 선택 기록

CoopRPG는 독립 Server와 Client 실행물을 만들고 패키지로 검증하기 위해 Epic의 UE 5.8.4 소스를 받아 빌드한 엔진을 사용한다. 이 선택은 V01에서 실제로 사용한 경로에 관한 것이다.

**근거** · [전용 서버 가이드](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-dedicated-servers-in-unreal-engine) · [V01 빌드 기록](../Validation/Environment.md#v01-source-build)

## 선택한 경로와 비용

| 작업    | CoopRPG의 실제 경로                                                                                    |
| ----- | ------------------------------------------------------------------------------------------------- |
| 엔진 준비 | 공식 5.8.4 소스를 고정하고, `Setup.bat`로 의존성을 준비한 뒤 `GenerateProjectFiles.bat`와 Editor 빌드로 소스 기반 엔진을 준비했다. |
| 게임 빌드 | `C:/UE/5.8.4`의 빌드 도구로 프로젝트 타깃과 필요한 엔진·게임 모듈을 빌드했다.                                                |
| 실행 검증 | 쿠킹·스테이징한 전용 서버와 클라이언트를 별도 프로세스로 실행했다.                                                             |

엔진 소스는 처음에 확보한다. 새 타깃을 빌드할 때 필요한 엔진 모듈은 해당 타깃의 조건으로 컴파일할 수 있다. 이번 Client 추가는 기존 서버를 그대로 사용하고 Client만 새로 빌드했다. 런타임 검증에는 패키징한 실행 파일과 콘텐츠를 사용했다.

**근거** · [공식 소스 준비 절차](https://dev.epicgames.com/documentation/en-us/unreal-engine/downloading-source-code-in-unreal-engine) · [Client 타깃 실측](../Validation/Environment.md#v01-client-build) · [실행 근거](../Validation/Index.md#v01-two-clients)

Launcher 설치형은 미리 빌드된 엔진을 사용하는 선택지이고, 일상적인 편집과 PIE 같은 개발 흐름을 지원한다. 독립 서버·클라이언트 패키지를 만드는 경우 Epic의 전용 서버 가이드가 제시하는 소스 빌드 경로를 CoopRPG에서 채택했다. 팀 환경에서는 `WithServer`와 `WithClient` 옵션을 사용해 필요한 지원이 포함된 Installed Build를 제작·공유하는 방식도 고려할 수 있다. CoopRPG에서 해당 배포 방식을 시험한 기록은 아직 없다.

**근거** · [Epic 전용 서버 가이드](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-dedicated-servers-in-unreal-engine) · [Epic Installed Build 안내](https://dev.epicgames.com/documentation/en-us/unreal-engine/create-an-installed-build-of-unreal-engine)

소스 엔진의 초기 빌드에는 시간과 저장 공간이 든다. 개발 중에는 호환되는 엔진·SDK·타깃 설정으로 산출물을 재사용하고, 어떤 조건에서 빌드 범위가 커졌는지는 로그로 확인한다. 설정 혼용의 실제 사례는 SDK 기록에 둔다.

**근거** · [환경·공간 기록](../Validation/Environment.md) · [SDK 혼용 사례](../Cases/V01-WindowsSdkSelection.md)

## 근거 및 더 읽기

- **채택·실행 근거** — [V01용 소스 엔진 패키지 빌드](../Validation/Environment.md#v01-source-build)에는 선택한 엔진 경로와 명령, [2클라이언트 검증](../Validation/Index.md#v01-two-clients)에는 실행 범위와 결과가 있다.
- **채택 조건** — [Epic 전용 서버 가이드](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-dedicated-servers-in-unreal-engine)에서 소스 빌드 전제와 Server·Client 빌드 및 Cook 절차를 확인한다.
- **더 읽기** — [공식 소스 다운로드](https://dev.epicgames.com/documentation/en-us/unreal-engine/downloading-source-code-in-unreal-engine)는 엔진 준비, [Installed Build](https://dev.epicgames.com/documentation/en-us/unreal-engine/create-an-installed-build-of-unreal-engine)는 팀이 미리 빌드한 엔진을 공유하는 방법을 설명한다.
