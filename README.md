# CoopRPG

GAS와 언리얼 데디케이트 서버를 활용하는 협동 액션 RPG 포트폴리오 프로젝트. 프로젝트 스텔라의 자체 전투 구조 경험을 엔진 프레임워크·네트워크 영역으로 확장하고, AI 협업과 MVP 범위 통제 과정을 증거로 남기는 것이 목적이다.

## 현재 상태

**2026-11-15 현재:** V01 접속·이동·서버 종료 감지는 통과했고, V02 GAS 단타의 승인·거리 거절·입력 통합 및 사용자 수동 로그 확인까지 근거가 있다. 후속 [독립 패키지 비용·2초 쿨다운 시험](Docs/Validation/Index.md#v02-cost-cooldown)과 기존 회귀도 통과했다. 사용자 [새 기능 직접 재현](Docs/Validation/Index.md#v02-cost-cooldown-manual)과 Editor 갱신, V02 잔여·V03 취소·한 판 완주는 남아 있다. 아래 날짜별 결과는 당시 이력으로 보존한다.

2026-11-07 · **최소 맵 `L_Movement`에서 기본 캐릭터의 WASD 이동과 정지를 로컬 PIE로 확인했다.** 사용자가 `MaxWalkSpeed`를 500에서 300 cm/s로 바꿔 빌드하고 플레이에서 차이를 확인했다. 소스 UE 5.8.4의 패키지 전용 서버와 전용 Client 2개로 접속·자기 캐릭터 조작·상대 이동·정상 종료를 확인했다. 서버 선종료 감지를 포함한 전체 흐름은 자동 검증 3회를 통과했다. 수동 1회에서도 사용자 이동 복제 관찰과 양쪽 종료 로그를 확인했다. [수동 결과](Docs/Validation/Index.md#v01-manual-result) [V01 반복 근거](Docs/Validation/Index.md#v01-server-shutdown) [현재 카드](Docs/Roadmap.md#next-work) · [이동 구조](Docs/Architecture.md) · [검증 결과](Docs/Validation/Index.md#l00-movement) · [35개 요구](Docs/Development.md#requirement-coverage)

2026-11-14 · `L_Movement`에 **GAS 고정 표적 단타**를 추가했다. 마우스 왼쪽 버튼으로 요청하고 서버가 사거리·비용·피해를 판정한다. [구현 흐름](Docs/Architecture.md#gas-basic-attack) · [두 클라이언트 시험](Docs/Validation/Index.md#v02-basic-attack)

아래 날짜별 내용은 이전 실행 이력이다. 과거4주 목표·미실행 표시는 당시 시점으로 읽고 현재 납기는 Roadmap을 따른다.

2026-10-30: 문서 6개·Git 관리 파일 2개·모델 추천 스킬 사본 1개와 커밋 3개를 [공개 저장소](https://github.com/Razria93/CoopRPG)의 `main`에 처음 공유했다. 이후 데스크탑 저장소 이관·기준 커밋 검증과 UE 5.8.4·기존 VS 2022 도구 조합 채택에 이어 공식 소스·Setup 의존성 준비와 기존 사전 구성 확인을 마쳤으며 당시 게임 프로젝트·게임 구현·게임 빌드·게임 시험은 모두 미착수였다. 4주는 검증 전 목표이며 최신 작업 상태는 [Roadmap](Docs/Roadmap.md)을 따른다.

2026-11-02 엔진 빌드 후속: 엔진용 프로젝트 파일 생성은 성공했으며 UnrealEditor Win64 Development 재시도는 커밋 압박으로 중단해 미완료다. [실행 결과·한계](Docs/Validation/Environment.md#첫-unrealeditor-엔진-빌드--2026-11-02)를 확인하며 에디터 실행·게임 빌드·독립 접속 성공과 구분한다.

2026-11-03 엔진 재개 결과: UnrealEditor Win64 Development 빌드가 종료 코드 0으로 끝났고 실행 파일·receipt를 확인했다. [실제 결과·한계](Docs/Validation/Environment.md#unrealeditor-빌드-재개-결과--2026-11-03)를 따른다. 에디터 기동 후속에서 초기 DLL 2개 복구·ShaderCompileWorker 준비를 거쳐 [로컬 프로젝트 선택 화면·정상 종료를 확인](Docs/Validation/Environment.md#에디터-기동-재검증-완료--2026-11-03)했다. 비치명 오류·경고와 미검증 범위는 해당 기록에 남겼다. 그 기동 확인 당시 게임 프로젝트·게임/서버 빌드·게임 시험은 미착수였다.

2026-11-03 프로젝트 착수 결과: Git LFS 3.8.1과 [최소 C++ 프로젝트](Game/CoopRPG.uproject)를 준비했고 **CoopRPGEditor Win64 Development 첫 프로젝트 빌드가 성공**했다. IDE 파일 생성 10.452초·Editor 빌드 46.733초이며 [실제 도구·공간·검증 근거](Docs/Validation/Environment.md#최소-c-프로젝트와-editor-첫-빌드--2026-11-03)를 기록했다. 그 빌드 시점에 게임 기능·프로젝트 기동·Server/Game 빌드·게임 시험은 하지 않았다.

2026-11-03 프로젝트 기동 결과: 소스 Editor에서 **CoopRPG 모듈 로드·실제 작업 화면·정상 종료를 확인**했다. [실제 기동 결과·잔여 엔진 진단·이전 해석 정정](Docs/Validation/Environment.md#cooprpg-프로젝트-editor-기동-확인--2026-11-03)을 기록했다. 엔진 자동 시험 3개·StylusInput 진단은 미해결이며 게임 기능·Server/Game 빌드·게임 시험은 미실행이다.

**엔진 준비 경로:** 공식 GitHub 소스를 확보하고 Setup으로 의존성을 준비한 뒤, 엔진용 IDE 파일 생성과 C++ 컴파일로 UnrealEditor 실행 파일을 만든다. 여기서 엔진 빌드는 에디터를 실행하거나 게임 프로젝트를 빌드하는 작업과 구분한다. 독립 Server 타깃을 사용할 소스 엔진을 준비하는 단계다.

## 문서 안내

| 문서 | 내용 |
|---|---|
| [작업 지침](AGENTS.md) | AI 작업 절차·범위 통제·검증 원칙 |
| [작업 판단 가이드](Docs/Planning/WorkingGuide.md) | 계획·다음 작업 선택·검토 기준, 학습/역할/완료 조건과 포트폴리오 근거 연결 |
| [프로젝트 기준](Docs/Project.md) | 확정 목적·제안 범위·게임 규칙·포트폴리오 방향 |
| [진행 계획](Docs/Roadmap.md) | MVP 단계·현재 작업·남은 확인/선택/검증 사항·결정 기록 |
| [개발·학습 기록](Docs/Development.md) | 일별 예정/실제·추가/미달·학습·변경·게이트·요구 대응·포트폴리오 연결 |
| [최초 계획 보존본](Docs/Planning/Baseline-20261105.md) | 최초 범위·19일 일정·37강 매핑·완료 기준. 현행 변경과 비교하는 기준 |
| [검증 색인](Docs/Validation/Index.md) | 시험 기준안·결과와 증거 기록 방식 |
| [환경 조사](Docs/Validation/Environment.md) | 확인된 도구·환경과 미검증 항목 |
| [검증 로그 도구](Docs/Validation/LogViewer.md) | 두 모니터 배치·통합/출처 탭·표시 규칙·요청 ID 설계 |
| [빌드 원리](Docs/Learning/UnrealBuildPipeline.md) · [소스 엔진 선택](Docs/Learning/SourceEngineDecision.md) | CoopRPG의 타깃·모듈·의존성 및 전용 실행물 준비 경로 |
| [서버 권한과 신뢰 경계](Docs/Architecture.md#server-authority) | 현재 이동 확인 범위와 상자·GAS 판정 설계 |
| [GAS 단타 구현 읽기](Docs/Learning/GASMeleeAttack.md) | 입력·GA·GE·속성 복제 흐름과 실제 검증 범위 |
| [기능 문서·세션 인계](Docs/Development.md#feature-documents) | 같은 기능 문서의 갱신·분리 기준, 실제 기여·학습·역할 인계 |
| [Windows SDK 혼용 사례](Docs/Cases/V01-WindowsSdkSelection.md) | V01 빌드 로그와 프로젝트별 SDK 관리안 |

작업을 계획하거나 검토할 때는 [cooprpg-work-planning](.agents/skills/cooprpg-work-planning/SKILL.md)을, 실제 기능 설명을 갱신할 때는 [cooprpg-feature-docs](.agents/skills/cooprpg-feature-docs/SKILL.md)를 사용한다. 사람용 설명과 요청 예시는 [가이드 사용 방법](Docs/Planning/WorkingGuide.md#usage)에 있다.

`Docs/Cases/`에는 SDK 사례가 있다. 추가 사례와 `Docs/Evidence/` 자료는 실제 문제·증거가 생길 때만 만든다. 빈 자료나 완료 증거를 미리 만들지 않는다. `Docs/X`는 기존 임시 자료의 보관 경로였으며 현재 기준 문서가 아니다. 이번 문서 정비 시 해당 경로는 존재하지 않았고, 이동·삭제 경위는 확인하지 않았다.

<a id="document-editions"></a>
### 기능 문서와 편집본 찾기

기술 사실은 코드·채택 규칙·검증 기록을 기준으로 판단한다. 같은 기능의 변경은 기존 문서에 누적하고, 편집본은 중간 목표 완료·공유 요청 때 갱신한다. 아직 작성하지 않은 기능 문서를 목록에 미리 만들지 않는다.

| 주제 | 유지할 설명·근거 | 편집본·현재 상태 |
|---|---|---|
| 빌드·전용 서버·이동 | [빌드 원리](Docs/Learning/UnrealBuildPipeline.md) · [소스 엔진](Docs/Learning/SourceEngineDecision.md) · [구조](Docs/Architecture.md) · [V01 결과](Docs/Validation/Index.md#v01-server-shutdown) | 현재 저장소 문서를 사용. 별도 HTML/PDF가 있더라도 아래 식별 정보가 확인되기 전 최신본으로 간주하지 않음 |
| GAS 단타·비용·속성 복제 | [GAS 구현 읽기](Docs/Learning/GASMeleeAttack.md) · [입력 회귀](Docs/Validation/Index.md#v02-attack-input-regression) · [사용자 확인](Docs/Validation/Index.md#v02-manual-attack-result) | Markdown은 비용·쿨다운과 Mixed 복제의 실제 시험을 추가한 미커밋 갱신본. 8쪽 PDF는 그 이전 로컬 검토본으로 새 기능 미반영·공개/최종 제출본 아님 |
| 전용 서버 로그 뷰어 | [기술 안내](Docs/Validation/LogViewer.md) · [사용자 수동 검증](Docs/Validation/Index.md#v02-viewer-player-attribution-manual) · [최종 화면 검증](Docs/Validation/Index.md#v02-viewer-toolbar-order) | 포트폴리오 HTML 로컬 편집본은 `Artifacts/Documentation/CoopRPG-LogViewer-Case/index.html`. `manifest.json`에 뷰어 기준·수동 실행·파일 해시를 기록했다. 1440px·390px에서 검수했다. |

**GAS PDF 판본 · 2026-11-15 식별**

- 저장소 상대 보관 위치: `Artifacts/Documentation/GASMeleeAttack/20261115-review/`. 해당 폴더는 Git 제외 로컬 자료이며 외부 독자가 열 수 있는 공개 링크가 아니다.
- 출력본: `CoopRPG-GAS-Technical-Review.pdf`. 같은 문서 세션에 남아 있는 편집 원본 `render_gas.py`와 파일별 원래 경로·SHA-256을 `manifest.json`에 함께 보존했다. 원본을 이동·덮어쓰지 않았다.
- 이전 Markdown 기준은 `45a1876`이었다. 현재 Markdown은 그 위 미커밋 비용·쿨다운 구현과 [새 패키지 시험](Docs/Validation/Index.md#v02-cost-cooldown)을 반영했다. 코드/패키지 해시는 해당 실행의 `final/package/input-package-hashes.json`에 있다. PDF 생성 당시 코드 커밋은 미확인이고 편집 원본 재실행에 의한 출력 일치는 이번에 검증하지 않았다. 현재 HEAD를 과거 PDF의 생성 기준으로 소급 지정하지 않는다.
- 검수 근거: [11/15 PDF·Markdown 편집 기록](Docs/Development.md#daily). 당시 운영 보완은 판본 식별·복사 보존만 했으며 도식 변경·PDF 재렌더·새 게임 시험은 하지 않았다. 기존 승인 도식은 편집본의 의도로 보존한다.
- 이전 사본 `Artifacts/Documentation/GASMeleeAttack.pdf`도 보존한다. 이름이 비슷해도 최신 검토본과 혼용하지 않으며 해시·쪽수는 manifest에서 구분한다.

새 판본을 낼 때 위 항목에서 실제 코드·시험 기준, 편집 원본/출력본, 검수 범위와 공개 상태를 갱신한다. 외부 공유가 필요하면 열람 가능한 배포 경로를 확인한 뒤 연결한다. 스킬·형식 규칙은 [작업 지침](AGENTS.md#session-workflow)과 [기능 문서 스킬](.agents/skills/cooprpg-feature-docs/SKILL.md)을 따른다.

## 실행 안내

아래는 첫 이동 실습 안내다. 두 클라이언트 전투·로그 확인은 [V02 수동 검증 안내](Docs/Validation/Index.md#v02-manual-attack-log)를 사용한다.

1. UE 5.8.4 소스 엔진으로 `CoopRPGEditor Win64 Development`를 빌드하고 [CoopRPG.uproject](Game/CoopRPG.uproject)를 연다. [엔진과 빌드 환경](Docs/Validation/Environment.md#최소-c-프로젝트와-editor-첫-빌드--2026-11-03)
2. 기본 맵 `L_Movement`에서 플레이어 1명으로 Play를 누르고 뷰포트를 클릭한다. W/S는 앞뒤, A/D는 좌우 이동이다. Esc로 종료한다.
3. 속도 비교는 [캐릭터 생성자](Game/Source/CoopRPG/Character/CoopRPGCharacter.cpp)의 `MaxWalkSpeed` 한 줄을 변경하고 에디터를 닫아 같은 타깃을 다시 빌드한 뒤 진행한다. 시작 지점 옆과 500 cm 전방에 있는 두 기둥을 같은 기준으로 사용한다.

## 문서 사용 기준

- **열람·수정 시점:** [AGENTS의 문서 책임](AGENTS.md#문서-책임과-종료-보고)을 따른다. 신규 독자와 평가자가 프로젝트와 다음 읽을 곳을 찾는 진입점이다.
- **포함·경계:** 목적 요약·주요 상태·문서 링크·실행 안내를 포함한다. 상세 게임 규칙·작업 이력·시험 결과는 원본에 두고 링크한다.
- **최소 양식:** 현재의 소개 → 상태 → 문서 안내 → 실행 안내를 유지한다. 실행물이 없으면 미제공 상태를 명시한다.
- **검증:** 상태 요약이 Roadmap과 맞는지, 링크가 열리는지 확인한다. 실행 안내를 추가할 때는 검증한 빌드·환경·절차를 연결하고 미검증 명령을 실행 가능한 안내로 제시하지 않는다.
