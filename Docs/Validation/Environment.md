# 환경·기존 자료 조사

갱신: 2026-10-30. **확인 사실과 미검증 범위**를 기록한다. UE 5.8.4의 구성안은 아래 D01-S 절, 노트북의 전체 환경 관측은 실행 환경 재확인 절을 따른다. 이전 시점의 기록은 보존하며 재확인하지 않은 항목을 현재 사실로 승계하지 않는다.

## 문서 사용 기준

- **열람·수정 시점:** [AGENTS의 문서 책임](../../AGENTS.md#문서-책임과-종료-보고)을 따른다. 환경을 선택·준비하거나 실행을 재현하는 사람이 참조한다.
- **포함·경계:** 실제 관측한 도구·장비·경로·자료 검토 범위, 미검증 사항, 향후 검증된 공통 빌드/실행 명령을 관리한다. 환경 채택 결정은 Roadmap, 개별 게임 시험 결과는 해당 검증 기록에 둔다.
- **최소 양식:** 조사·검토의 기본 순서는 AGENTS를 따르며 관측 날짜·대상·방법·결과·판정 한계를 기존 표나 짧은 기록으로 남긴다. 후속 선택·작업은 Roadmap의 해당 항목을 참조한다. 공통 명령을 추가할 때는 작업 경로·도구/구성·사전 조건·명령·실행 결과를 함께 남기고 후보와 검증된 명령을 구분한다.
- **검증:** 이관 기록과 이번 관측, 파일 존재와 실행 성공, 조사한 범위와 미확인 범위를 구분한다. 변동 가능한 정보는 사용 전에 필요한 항목만 재확인하며 문서 편집일을 재측정일로 쓰지 않는다. 읽기 조사 결과 확인을 이유로 설치·빌드를 실행하지 않는다.

## 로컬 Git 초기화·첫 커밋 — 2026-10-30

사용자가 로컬 초기화·검사·첫 커밋을 허용했다. 작업 경로는 `C:\CoopRPG`이며 최초 커밋은 **`80e0808053920e969a1689157819fe6cd8c11a9f`**다. 원격 저장소 생성·업로드·다른 장비 전달·다운로드·설치·빌드는 수행하지 않았다.

| 항목 | 실행·확인 결과 | 한계 |
|---|---|---|
| 저장소·작성자 | `git init --initial-branch=main` 성공. 루트 `C:/CoopRPG`, 브랜치 `main` 확인. 기존에 선택한 이름·이메일을 `.git/config`에 저장소별로 설정 | 전역 설정은 변경하지 않음. 이메일 원문은 문서에 추가하지 않음. 원격 연결 0개, 활성 hook 파일 없음(기본 sample만 존재) |
| 제외 규칙 | `git check-ignore --no-index -- <경로 목록>`으로 제외 26개·보존 29개를 검사해 오류 0건. 실제 게임·자산 파일 없이 경로 문자열로 확인 | 이전 ripgrep 검사와 달리 Git 자체 결과. 플러그인 원본 프로젝트·필수 DLL·Build 입력 보존 포함 |
| 속성·줄바꿈 | `git check-attr filter diff merge text eol -- <경로 목록>`으로 Markdown/관리 파일 LF, uasset/umap의 LFS filter/diff/merge·text unset, png/wav의 LFS 미지정 확인. 후보 9파일 모두 eol=lf. `git ls-files --eol`에서도 색인·작업 파일 모두 LF | LFS 속성 조회이며 실제 자산 추가·필터 실행·객체 전송 시험은 아님. LFS 설치/갱신은 하지 않음 |
| 커밋 대상 | 명시한 9경로만 `git add -- <9경로>`로 준비. `git diff --cached --name-only`가 목록과 일치, `git diff --cached --check` 오류 0건. 관리 파일 diff와 전체 stat 검토 | 초기 문서 6개·관리 파일 2개·스킬 1개. 게임 코드·자산·엔진·인증 파일 없음 |
| 문서·사본 검토 | 후보 9파일 UTF-8/LF, 상대 링크 49개·사용 앵커 24개 검사 오류 0건. 제한된 키/토큰 패턴 일치 0건, 스킬 사본 해시 불변 | 패턴 검사만으로 비밀정보 부재를 보증하지 않음. 외부 링크 전체 재검증·게임 시험은 하지 않음 |
| 첫 커밋 식별·종료 확인 | `80e0808` 생성 후 `main`·커밋 수 1개·추적 파일 9개·선택한 작성자·작업 트리 clean·원격 0개 확인 | 재현 시 `git log -1 --oneline`, `git ls-tree -r --name-only HEAD`, `git status --short` 사용. 원격 생성/업로드·다른 장비의 같은 커밋 확인은 미실시 |

실패·수정·재확인: 첫 저장소별 설정 쓰기는 접근 제한으로 실패했으나 허용된 로컬 작업의 쓰기 권한으로 재시도해 성공했다. 최초 제외 검사에서는 PowerShell 파이프 입력의 CR이 경로에 붙어 결과 비교가 실패했다. `check-ignore -v`와 출력 확인으로 입력 문제를 확인하고 경로 인수를 직접 전달해 55개 전부 재검사했다. `.gitignore` 규칙을 바꾸거나 기준을 완화하지 않았다. 공통 빌드·게임 실행 명령은 여전히 없다.

**첫 커밋 후속 검토(2026-10-30):** 검토 착수 시 작업 트리는 clean이었다. 커밋과 현재 색인의 파일 목록 모두 의도한 9개와 일치하고, 브랜치 `main`·부모 없는 최초 커밋 1개·선택한 작성자·원격 0개·활성 hook 없음이 확인됐다. 제외 26개·보존 29개와 LF/LFS 속성을 Git 자체로 재확인해 오류가 없었다. 커밋에 든 스킬의 blob도 개인 원본과 일치했다. 규칙·스킬 변경은 필요하지 않아 유지했다. 완료 상태 표현을 명확히 하려고 Roadmap과 이 문서만 보완했으며, 보완 내용은 미커밋·미스테이징 상태로 남겼다. 최초 커밋은 변경하지 않았다. LFS 객체 전송·다른 장비 로딩·게임 실행 검증은 여전히 미실시다.

**문서 분할 커밋 시 환경 확인(2026-10-30):** 현재 `.git/hooks`에는 Git LFS 기본 훅 `post-checkout`, `post-commit`, `post-merge`, `pre-push`가 존재하며 본문을 확인했다. 앞선 ‘활성 hook 없음’은 당시 관측이다. 이번 작업에서는 훅을 생성·수정하거나 LFS를 설치하지 않았으며 생성 시점·경위는 조사하지 않았다. 원격 목록은 비어 있고 커밋 대상 Markdown에는 LFS 필터가 지정되지 않았다.

아래는 초기화·커밋 이전 준비·조사 기록이며 당시의 미실행 상태와 최신 실행 결과를 구분한다.

## 초기 커밋 후보 파일 준비 — 2026-10-30

사용자 요청으로 관리 파일 2개를 작성하고 같은 장비의 개인 스킬을 프로젝트 안에 복사했다. 기존 문서 6개와 합쳐 아래 9파일이 현재 실제 후보 목록이다. 문서·관리 파일·사본 준비만 수행했고 Git 초기화·커밋·원격 생성·업로드·다른 장비 전달·다운로드·설치·빌드는 하지 않았다.

| 구분 | 실제 파일 |
|---|---|
| 기존 문서 6개 | `AGENTS.md`, `README.md`, `Docs/Project.md`, `Docs/Roadmap.md`, `Docs/Validation/Index.md`, `Docs/Validation/Environment.md` |
| 관리 파일 2개 | `.gitignore`, `.gitattributes` |
| 스킬 사본 1개 | `Tools/Skills/recommend-model/SKILL.md` |

| 확인 항목 | 방법·실제 결과 | 한계 |
|---|---|---|
| 제외 규칙·후보 목록 | 루트에서 `rg --files --hidden --no-ignore-parent --no-ignore-vcs --ignore-file .gitignore .`로 열거해 후보 9파일이 모두 표시됨을 확인 | 아직 추적되거나 staged 상태인 파일은 아님. 이후 Git 초기화 허용 시 Git 자체 검사·staged diff 재확인 |
| 제외/보존 경로 | 별도 임시 폴더에 빈 파일 예시를 만들고 `rg --files --hidden --no-require-git --no-ignore-parent .`로 검사. 제외 26개·보존 25개 모두 기대대로 처리, 임시 폴더 제거 확인 | ripgrep의 ignore 처리로 확인했으며 Git 자체의 `check-ignore` 실행 결과는 아님. 프로젝트나 임시 폴더에 Git 저장소를 만들지 않음 |
| 보존·예외 | Game의 Build 입력·Config·Source·Content·Plugins 원본, 플러그인 필수 DLL, Docs의 이미지와 Game의 음원 보존 예시 확인. Game 생성물·플러그인 중간 산출물·Artifacts·Docs/X·알려진 엔진 사본·개인 설정·임시 파일 제외 예시 확인 | 플러그인 Binaries를 일괄 제외하지 않음. 미래 플러그인의 이용 권한·필수 배포 바이너리와 생성물 구분은 D03 조사 후 개별 반영 |
| 파일 속성 | `.gitattributes` 내용을 대조: 기본 `text=auto`, Markdown/관리 파일 LF, `.uasset`·`.umap`에 LFS filter/diff/merge와 `-text` 지정. 이미지·음원 전체를 LFS 대상으로 삼지 않음 | 파일 내용 검토. 아직 Git 속성 적용·LFS 필터 실행·객체 전송은 미검증. 첫 자산 추가 전 LFS 준비 조건 유지 |
| 스킬 사본 | 개인 원본과 프로젝트 사본의 SHA-256이 모두 `8B60FA30B905662F36E41679F242435D38BBB57A172C50DFD7170022990480AA`로 일치 | 개인 원본은 수정하지 않음. 현재는 저장소에 담을 사본만 배치했으며 다른 장비 전달·개인 스킬 설치·로딩 시험은 하지 않음 |
| 최종 문서·파일 검토 | 전체 파일이 후보 9개와 일치. UTF-8/LF 확인, 상대 링크 48개·사용 앵커 23개 검사 오류 0건. 9파일의 제한된 키/토큰 패턴 검사 일치 0건, 작성자 이메일 원문 추가 없음. `.git`·Game·Docs/X 없음 | 패턴 검사만으로 비밀정보 부재를 보증하지 않음. Docs/X는 없어 보존 해시 비교 불가. 실제 공개 직전 포함 목록·내용을 다시 확인 |

**후속 검토(2026-10-30):** 기존 `.gitignore`의 전역 `*.sln`·`*.slnx`·`*.vcxproj`·`*.vcxproj.filters`가 도구·외부 플러그인의 원본 프로젝트 예시 4개를 숨기는 문제를 임시 폴더에서 재현했다. 제외 범위를 `/Game/` 바로 아래로 좁혀 같은 4개가 보존되고 생성 파일은 계속 제외되는지 확인했다. 같은 ripgrep 방식으로 기존 경로와 회귀 사례를 합친 **제외 26개·보존 29개**를 검사해 오류 0건, 임시 폴더 제거를 확인했다. 범위 지정의 근거는 [Git ignore 패턴 안내](https://git-scm.com/docs/gitignore)다. `.gitattributes`의 LF/LFS 정책과 추천 전용 스킬 사본은 모순을 찾지 못해 수정하지 않았다. 스킬 원본·사본 해시는 기존 값과 일치하며 Git 자체 적용·로딩·실행 검증은 여전히 미실시다. 초기 26/25 검사는 당시 사례 범위의 결과로 보존한다.

허용 범위·현재 작업·다음 단계는 [Roadmap](../Roadmap.md)을 따른다. 아래 읽기 조사와 D02-S 스킬 표본은 **관리 파일·사본을 만들기 전 관측**이며, 당시의 미생성 기록을 현재 상태로 해석하지 않는다.

## 초기 Git 공유 전 읽기 조사 — 2026-10-30

노트북의 기존 파일·Git 설정과 GitHub 읽기 API를 조사했다. 시간 표본은 14:56~14:57 KST이며 전체 실제 투입 시간은 별도 계측하지 않았다. 로컬/원격 저장소 초기화·커밋·생성·업로드·파일 전달·설치·빌드는 하지 않았다. 사용자 선택의 원본은 [Roadmap](../Roadmap.md#d02-s-폴더git스킬-전달-정책)에 둔다.

| 대상 | 확인 사실·방법 | 한계·후속 조치 |
|---|---|---|
| 작업 경로·공유 후보 | `Get-Location`은 `C:\CoopRPG`. 재귀 파일 목록은 기존 Markdown 6개, 조사 시작 당시 합계 107,287바이트. `.git`·게임 프로젝트·관리 파일·Tools·Docs/X 없음. `git rev-parse --show-toplevel`도 상위 경로를 포함해 저장소가 아니라고 반환 | 문서 수정 후 바이트 수는 달라짐. 미래 초기 추적 목록은 문서 6개 + `.gitignore` + `.gitattributes` + 스킬 1개, 총 9파일 후보. 관리 파일·스킬 사본은 아직 없음 |
| Git/LFS | Git 2.55.0.windows.2, LFS 3.7.1 실행 확인 | 문서 공유에는 LFS 객체 없음. 첫 자산 처리 전 준비 조건은 아래 D02-S LFS 기록 참조 |
| 작성자 | `git config --show-origin --get user.name/user.email`: 사용자 전역 설정에서 이름 `Razria-Notebook`과 이메일 값 확인 | 사용자가 현재 값 사용을 선택. 이메일 원문은 이 문서에 추가하지 않음. 데스크탑 설정에는 자동 전달되지 않으며 사용 전 같은 작성자 정책을 적용 |
| 초기 브랜치·줄바꿈 | 시스템 Git 설정의 `init.defaultBranch=master`, 사용자 설정의 `core.autocrlf=true`. 스킬의 줄바꿈은 LF, Roadmap은 LF/CRLF 혼재, 나머지 문서는 LF | 조사 당시 관측. 문서 편집 과정에서 Roadmap 줄바꿈을 LF로 정리. 초기화 허용 시 `main`을 명시하고 전역 설정을 바꾸지 않는 정책. Markdown의 LF 고정은 Roadmap 정책에 반영. 현재 Git 설정은 수정하지 않음 |
| 커밋 관련 설정 | 조회한 `commit.gpgsign`·`core.hooksPath`·`init.templateDir`·`user.useConfigOnly`에는 명시 값 없음 | 조회한 설정 키의 결과일 뿐 향후 저장소의 hook·서명·커밋 성공을 검증한 것은 아님 |
| GitHub 계정 | 연결 도구 `get_user_login`과 기존 CLI `gh api user`가 모두 `Razria93` 반환. 제한된 네트워크에서 CLI 연결 실패 후 허용된 읽기 조회 성공 | 인증 토큰을 출력·저장하지 않음. API 조회 성공과 Git 전송 인증·새 저장소 생성/업로드 성공은 별개 |
| 대상 후보 조회 | `Razria93/CoopRPG` 메타데이터 조회는 연결 도구·CLI 모두 404. CLI의 조회 가능한 소유 저장소 목록을 페이지별로 읽어 이름에 coop/rpg가 있는 항목을 필터한 결과 0개 | 조회 범위에서 대상 저장소를 찾지 못함. 404만으로 모든 권한 부재·이름 예약 가능·생성 가능을 확정하지 않음. 새 저장소는 생성하지 않았고 대상 쓰기 권한·원격 연결은 미검증 |
| 공개할 문서 내용 | 현재 6문서를 읽고 개인 키 헤더·GitHub/OpenAI 토큰 형식 일부를 패턴 검사하여 일치 0건. 환경 기록에 로컬 사용자 경로·하드웨어 정보가 있고 외부 링크에는 접근 권한이 필요한 Epic 소스 링크도 있음 | 제한된 패턴 검사이며 비밀정보 부재를 보증하지 않음. 사용자 선택은 공개 저장소. 실제 공개 직전 최종 포함 목록·내용을 다시 확인하며 이번에 과거 환경 사실을 삭제하거나 공개하지 않음 |
| 모델 추천 스킬 | 개인 `recommend-model` 폴더에는 `SKILL.md` 1개(6,511바이트), SHA-256은 기존 D02-S 표본과 일치. 내용상 필수 로컬 스크립트·추가 파일·개인 절대 경로 의존성은 발견하지 못함. 공식 문서 URL 2개는 판단 시 참조 자료 | 단일 파일 스냅샷 전달 가능하다는 문서 조사 결과. 사본 생성·다른 장비 로딩·모델 사용 가능성 시험은 미실시. 추천 전용·현재 설정 유지·자료 확보 우선 판단은 보존 대상 |

**남은 미확인 사실:** 실제 Git 전송 인증·공개 저장소 생성/업로드 결과, 제외 규칙·속성의 실제 적용, 데스크탑 경로·Git/작성자 설정·스킬 로딩. 지금 사용자에게 기술 결과를 추정하도록 묻지 않고 해당 작업을 허용받은 뒤 확인한다. 엔진 준비·GAS 세부 설계는 초기 문서 공유의 선행 조건이 아니다.

## D01-S UE 5.8.4 환경 구성안 — 2026-10-30

작업 범위: 노트북에서 공식 릴리스·소규모 소스 설정 텍스트의 읽기 조회, 설치 정보 대조, 공간 예산 작성. 60분 상한의 조사 단위로 진행했고 주요 조회는 14:09~14:11 KST에 수행했다. 전체 소스/바이너리 다운로드·Git 초기화·설치·Setup·빌드는 하지 않았다. 아래 권고와 공간 예산은 구성안이며 실제 준비·시험 통과와 구분한다.

### 공식 소스 기준 — 확인 사실

| 항목 | 확인 근거·결과 | 한계 |
|---|---|---|
| 배포 | [Epic 5.8.4 발표](https://forums.unrealengine.com/t/5-8-4-hotfix-released/2842775): 2026-10-20 공개. 공식 저장소의 릴리스 API도 Unreal Engine 5.8.4, tag `5.8.4-release` 반환 | 현재 프로젝트의 엔진 채택 확정이나 실행 성공은 아님 |
| 고정 소스 | 노트북의 기존 `gh api` 읽기 조회로 `EpicGames/UnrealEngine` 접근 성공. `commits/5.8.4-release`에서 **f45729c0b6f097038e72b52d366a62baeca3275b** 확인 | 이 커밋으로 읽은 Build.version은 5.8.4·CL 0·Compatible CL 55116800. CL 0만으로 비공식/수정본이라고 판정하지 않음 |
| 연결별 접근 | GitHub 연결 도구 `get_repo`는 404. 기존 GitHub CLI는 네트워크 허용 후 같은 저장소·릴리스·파일 API 읽기 성공 | 연결 도구의 404는 사용자 계정 전체의 권한 부재가 아님. 데스크탑의 로그인·권한은 따로 확인 |
| 준비 방식 | [공식 소스 안내](https://dev.epicgames.com/documentation/unreal-engine/downloading-source-code-in-unreal-engine): Epic/GitHub 연결과 조직 초대 수락 후 소스 접근. Setup은 바이너리 의존성 다운로드·사전 구성 설치를 수행 | 이번에는 계정 연결·초대 수락·Setup을 실행하지 않음. 접근이 이미 되는 노트북에서 재연결을 요구하지 않음 |

**제안:** 엔진 소스는 위 공식 태그와 커밋으로 고정하고, 이동하는 `release`·개발 브랜치의 최신 상태를 그대로 개발 기준으로 삼지 않는다. 기존 OneDrive의 출처 미확인 5.6.1 폴더는 5.8.4 원본으로 사용하지 않는다. 소스 확보·준비는 데스크탑에서 허용받은 뒤 수행한다. 게임/엔진 분리는 [D02-S 정책](../Roadmap.md#d02-s-폴더git스킬-전달-정책)을 따르고 실제 엔진 경로는 데스크탑 실측 후 정한다. 이번에는 폴더를 만들지 않았다.

### 도구 요구 — 고정 커밋과 현재 노트북 대조

원본은 같은 커밋의 [Windows_SDK.json](https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Config/Windows/Windows_SDK.json), [GetDotnetPath.bat](https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Build/BatchFiles/GetDotnetPath.bat), [MSVC 검색 코드](https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Programs/UnrealBuildTool/Platform/Windows/MicrosoftPlatformSDK.cs)다. API 응답을 메모리에서 읽었으며 엔진 파일을 로컬에 저장하지 않았다.

| 항목 | 공식 소스·문서의 조건 | 노트북 대조·구성안 |
|---|---|---|
| IDE | [공식 5.8 VS 안내](https://dev.epicgames.com/documentation/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine?application_version=5.8): VS 2022 17.14 이상 지원, 일반 개발에는 VS 2026 권장 | 기존 VS 2022 17.14.12를 활용하는 안 우선. 새 IDE 설치를 자동 전제하지 않음. 데스크탑에서는 재조사 |
| MSVC | 선호 family 14.44.35207~14.44.99999 및 14.50.35717~14.50.99999. 실제 버전 금지 범위는 14.44.0~35210, 14.50.0~35722 등 | 로컬 family 폴더는 14.44.35207이나 cl.exe **ProductVersion은 14.44.35214.0**, FileVersion은 19.44.35214.0. UBT는 제품 버전으로 금지 범위를 검사하므로 표본상 금지 범위 밖·선호 family 안. 폴더명만 보고 금지 판정하지 않음. 실제 선택·컴파일 성공은 D01-H |
| Windows SDK | 고정 소스 Min 19041·Main 22621·Max 10.9.99999.0. 웹 5.8 안내는 최소 22621·권장 26100 이상으로 표시 | 기존 26100은 양쪽 조건에 해당하고 주요 파일 존재 확인됨. 당장 22621 추가 설치를 필수로 정하지 않음 |
| .NET SDK | **고정 5.8.4 배치 파일은 동봉 `10.0/win-x64`를 사용**하고 시스템 SDK 경로 선택 시 major 10 이상 검사 | 기존 시스템 SDK 9.0.304만으로 불충족. 5.5/5.6의 동봉 8.0.300을 가져오지 않음. 데스크탑 소스 의존성 준비 시 동봉 10.0을 확인하는 안 우선. 정확한 SDK 패치·복원 패키지 완전성은 후속 확인 |
| VS 추가 구성 | 소스가 .NET Framework 4.6.2 TargetingPack·ManagedDesktop·NativeDesktop·NativeGame 등을 제안 | 이번 vswhere 조회로 4.6.2 TargetingPack·ManagedDesktop도 등록 확인. 기존 C++ 구성과 함께 데스크탑에서 재확인 |
| 실행 사전 구성 | 고정 Setup.bat는 GitDependencies, VC++ Redistributable·GameInput Redistributable 설치와 엔진 등록을 수행 | Setup은 읽기 조사 명령이 아님. 후속 설치 허용 전에 실행하지 않음. 서버 안내의 전제인 소스 빌드 엔진·C++ 게임 조건은 D01-H에서 검증 |

**조건부 권고:** UE 5.8.4 고정 소스 + VS 2022 17.14 계열 + 금지 범위 밖의 MSVC 14.44 + SDK 26100 + 해당 소스의 동봉 .NET 10.0으로 시작하는 구성을 우선 검토한다. 이는 기존 노트북 도구를 기준으로 준비 변경을 줄이는 안이며, 데스크탑의 설치 상태와 호환성 검증을 대신하지 않는다. 데스크탑에 VS 2026 환경이 이미 있다면 유효한 MSVC 14.50 조합을 대안으로 비교한다. 현재 노트북에서 SDK/도구 설치를 추가하지 않는다.

준비 시간은 소스/의존성 확보·설치·셰이더 처리·첫 빌드와 실패 수정에 영향을 받는다. 현장 처리 시간은 미측정이므로 공식 예시의 컴파일 시간을 이 장비의 견적으로 사용하지 않고, 데스크탑에서 실행 후 기록한다. 기준일 이후 준비·수정 시간도 4주 계획에 포함한다.

### 공간 예산 — 실측과 계획 배정 구분

**실측:** 2026-10-30 14:09 KST `Get-PSDrive`: C: 여유 **62.93GiB**, 사용 412.85GiB. 노출된 Temp도 같은 볼륨이다. 데스크탑의 디스크는 아직 측정하지 않았다.

**근거의 한계:** 열람한 공식 Windows 소스/빌드 안내에는 이 프로젝트에 적용할 고정 디스크 요구량이 없다. [하드웨어 안내](https://dev.epicgames.com/documentation/en-us/unreal-engine/hardware-and-software-specifications-for-unreal-engine)의 Epic 워크스테이션 디스크 사양을 최소 요구량으로 사용하지 않는다. 의존성 매니페스트는 메타데이터(36,681,163바이트·blob `002e0d5f19cc6cd89f5803030ddba1e37202281b`)만 조회했다. 그 크기는 내려받을 바이너리 크기가 아니며, 파일 필터·압축/해제·중복 캐시·빌드 타깃에 따른 실제 점유량은 미측정이다.

다음 수치는 **AI가 제안하는 용도별 초기 예약 예산**이다. 공식 최소치·실제 용량 예측·상한 보장이 아니다. Windows x64의 개발용 Editor·Server·Client, 작은 MVP 한 개를 위한 계획 배정이며 추가 플랫폼·디버그 전체 심볼·Installed Build 별도 제작·여러 엔진 사본은 포함하지 않는다.

| 배정 대상 | 계획 배정(GiB) | 확인·조정 시점 |
|---|---:|---|
| 엔진 소스·제한된 Git 이력 | 40 | 실제 소스 확보 후 점유 측정. ZIP과 저장소의 중복 보관은 별도 가산 |
| 외부 의존성·다운로드/해제 임시 공간 | 60 | Setup 전후 캐시와 설치 대상의 증가량 측정 |
| 엔진 바이너리·중간 산출물·심볼 | 160 | 첫 Editor/Server/Client 빌드별 증가량 측정 |
| DDC·셰이더·임시 파일 | 40 | 첫 실행·쿠킹 후 측정, 캐시 위치별 중복 확인 |
| 게임 소스·최소 자산·쿠킹·패키지·시험 증거 | 30 | 최소 자산 선정·첫 패키징 후 측정 |
| OS·도구 변경과 예상 밖 증가를 위한 여유 | 30 | 큰 준비 작업 전 잔여 공간 확인 |
| **합계** | **360** | 실제 크기에 따라 재산정 |

**운영 제안:** 데스크탑에서 엔진 준비를 시작하기 전 관련 볼륨 합계 약 **400GiB의 빈 공간**을 확보하는 것을 초기 목표로 삼는다(360GiB 배정 + 추가 여유 40GiB). 필수 장비 구매 조건이 아니며 공간이 적다면 실제 준비 범위·캐시·타깃을 다시 산정한다. 예산보다 실제 증가량이 크면 진행 범위와 저장 위치를 재판단한다. 현재 노트북 62.93GiB는 이 계획 예산에 미달하므로 노트북에서 전체 소스 준비·빌드를 시작하는 안은 권하지 않는다. 파일 정리·삭제·이동이나 저장 장치 구매는 수행하지 않았다.

### 결과와 남은 결정

- **확인 사실:** 공식 릴리스 태그·고정 커밋·SDK/MSVC/.NET 조건, 기존 CLI의 공식 저장소 접근 성공, 노트북 현재 가용 공간을 확보했다. GitHub 연결 도구의 404와 CLI의 성공을 구분한다.
- **선택할 사항:** 엔진 5.8.4 및 위 도구 조합의 최종 채택, 데스크탑 저장 위치와 공간 확보 조치. 노트북 조사 단계에서는 조건부 권고로 남긴다.
- **미확인·미실행:** 데스크탑 사양·로그인·도구·공간, 소스와 전체 의존성의 현장 점유량·.NET 세부 패치·빌드 성공. D01-H는 미실행이다. 기존 5.6.1의 출처 해소를 공식 5.8.4 경로 사용의 선행 조건으로 두지 않는다.
- **후속 상태:** D02-S 정책과 초기 공유 방식·작성자 선택을 마쳤다. 최신 상태와 다음 허용 범위는 Roadmap을 따른다. 실제 Git 초기화·공유는 허용 후 수행하고, 데스크탑으로 넘어가면 이 구성안을 실측과 대조한다.

## D02-S LFS 도구와 스킬 표본 — 2026-10-30

- **확인 사실:** 개인 스킬 `C:\Users\star_\.codex\skills\recommend-model\SKILL.md`를 읽고 SHA-256 `8B60FA30B905662F36E41679F242435D38BBB57A172C50DFD7170022990480AA`를 확인했다. 현재 파일의 표본 식별값이며 프로젝트 스냅샷이나 데스크탑 설치가 이미 있다는 뜻은 아니다. 이번에 스킬을 수정·복사하지 않았다.
- **기존 관측:** 같은 날 실행 환경 재확인에서 Git LFS 3.7.1 실행을 확인했다. [Git LFS 공식 안내](https://git-lfs.com/)는 모든 사용자에게 보안 수정판 3.8.1 업데이트를 권고하고 [3.8.1 릴리스](https://github.com/git-lfs/git-lfs/releases/tag/v3.8.1)에 수정 내역이 있다.
- **필요 조치:** LFS 자산을 다룰 장비의 버전과 공유 경로 지원을 사용 전에 확인하고 공식 수정판 3.8.1 이상을 준비하는 정책이다. 패키지 클라이언트만 실행하는 시험 장비에는 Git/LFS 준비를 요구하지 않는다. 실제 업데이트는 설치 허용 후 수행한다. 문서·텍스트만 있는 초기 커밋을 LFS 바이너리 전송과 혼동하지 않는다.
- **미확인·미실행:** 데스크탑 스킬 경로·동명 스킬·Git/LFS 상태는 미조사다. 저장소별 LFS 구성, 스킬 전달·로딩 확인, Git 초기화·공유 모두 미실시다. 전달 정책의 원본은 [Roadmap](../Roadmap.md#d02-s-폴더git스킬-전달-정책)에 둔다.

## 2026-10-30 실행 환경 재확인

사용자가 디바이스를 변경했다고 알림에 따라 현재 명령 실행 환경을 재조사했다. 관측은 2026-10-30 13:39 KST 전후이며, 결과는 현재 이 채팅에서 접근 가능한 실행 호스트에 한한다. CPU·GPU·OS·도구 버전과 소스 표본이 이전 기록과 일치한다. 후속 사용자 설명(2026-10-30): 다른 기기에서 ‘다른 기기 제어’로 이 채팅에 접근한 것이다. 접속 기기와 명령 실행 호스트를 구분하며, 이번 결과는 기존 작업 실행 환경의 재확인으로 사용한다. 접속에 사용한 별도 기기의 사양을 조사한 결과는 아니다.

| 항목 | 이번 관측·방법 | 이전 기록과의 비교·한계 |
|---|---|---|
| 프로젝트 | `Get-Location`: `C:\CoopRPG`. 파일 목록은 기존 문서 6개, `.git`·게임 프로젝트 없음. `Docs/X` 없음 | 기본 경로 유지. 프로젝트 파일을 새로 생성·이동하지 않음 |
| CPU·GPU | 레지스트리: i5-13500HX, NVIDIA RTX 4060 Laptop GPU. GPU 등록 메모리 8,585,740,288바이트(약 8GiB), Intel UHD도 등록 | 이전 노트북 구성과 일치. CIM은 접근 제한으로 조회 불가. 장치 등록값이며 성능·실제 GPU 동작 검증은 아님 |
| RAM | Win32 `GlobalMemoryStatusEx`: OS 사용 가능 총량 31.70GiB, 당시 가용 14.91GiB | 총량은 이전과 같음. 설치 모듈·정확한 실장 용량은 미확인 |
| 공간 | `Get-PSDrive`: C: 여유 63.35GiB, 사용 412.42GiB. 노출된 Temp도 같은 볼륨 | 이전 64.82GiB보다 여유 약 1.47GiB 감소. 다른 물리 디스크가 없다는 뜻은 아님. 소스 빌드 전체 공간 예산은 미산정 |
| OS | 레지스트리 DisplayVersion 25H2, Build 26200.9711, ProductName Windows 10 Home | 이전 값과 일치. 제품명과 빌드 표기의 상충 가능성 때문에 제품명 단정 보류 |
| 엔진 | Launcher 등록·표준 설치 경로: UE 5.5.4 CL40574608. Editor·UBT·동봉 DotNet·Insights 파일 존재 | 조회 범위에서 5.8 설치는 확인되지 않음. 다른 위치의 미등록 엔진까지 전수 조사한 것은 아님 |
| 소스 후보 | 기존 OneDrive의 5.6.1 폴더 존재. `.git`·Editor·UBT DLL·동봉 DotNet 폴더 없음 | 이전 Build.version/Windows_SDK.json/Setup.bat의 세 SHA-256과 모두 일치. 전체 원본 무결성·출처는 여전히 미확인 |
| VS·MSVC | vswhere: VS 2022 Community 17.14.12, C++ 도구·NativeDesktop·NativeGame 구성 등록. MSVC 폴더 14.44.35207, cl.exe 파일 버전 19.44.35214.0 | 이전과 동일. 엔진별 실제 빌드 미검증 |
| SDK·.NET | SDK Include에 10240·26100 폴더. 26100 Windows.h·x64 kernel32.lib·rc.exe 존재. 시스템 .NET SDK 9.0.304 | 이전 주요 버전과 동일. 10240은 이번 완전성 재검사 없음. 동봉 .NET과 시스템 SDK는 구분 |
| Git·기타 도구 | Git 2.55.0.windows.2, LFS 3.7.1 실행 확인. PATH에서 CMake·MSBuild 미검색. 개인 recommend-model 파일 존재 | PATH 미검색은 미설치가 아님. Python은 WindowsApps 별칭 경로만 확인했고 실행 가능성은 미검증 |

**판단과 다음 조치:** 사용자 설명으로 접속 방식의 변경을 확인했으므로 실행 대상에 관한 확인 대기를 해소한다. 현재 관측값을 기준으로 D01-S의 소스·도구·공간 구성안을 계속 비교할 수 있다. UE 5.8.4는 대화에서 제시한 1순위 후보이며 이번 재확인으로 채택·설치가 확정된 것은 아니다.

2026-10-30 사용자 채택에 따른 장비 운영 계획은 [Roadmap](../Roadmap.md#노트북-작업git-공유데스크탑-이관)을 따른다. 위 값은 노트북 실행 환경의 기록이다. 데스크탑은 아직 조사하지 않았으며 이관 후 별도 관측을 추가한다. 노트북에서 준비한 구성안의 도구·공간 조건은 데스크탑 관측과 대조한 후 실제 준비에 사용한다. 계획 반영 자체를 Git 공유·장비 이관·개발 환경 준비 완료로 표현하지 않는다.

조사 범위는 레지스트리·Win32 메모리 조회·설치 등록·알려진 경로·도구 버전과 프로젝트 파일 목록이다. 엔진 실행·설치·다운로드·게임 구현·빌드는 수행하지 않았다.

## 프로젝트 파일 — 초기 기록

이번 작성 직전 `Get-Location`은 `C:\CoopRPG`였으며 파일 목록은 `Docs/X`의 임시 문서 3개였다. 상위 `C:\AGENTS.md`와 프로젝트 내부 AGENTS는 없었다. 새 문서 작성으로 루트 지침과 초기 문서를 추가했다. `.git`, `.uproject`, 게임 코드·콘텐츠·빌드 산출물은 없으며 Git 초기화·설치·구현·빌드를 수행하지 않았다.

후속 관측(2026-10-29, 문서 사용 기준 정비 시): `Get-Location`으로 같은 루트를 확인하고 `rg --files --hidden` 및 `Get-ChildItem -Force`로 현재 문서 6개를 확인했다. `Test-Path` 결과 `Docs/X`는 존재하지 않는다. 이전 임시 문서의 해시 비교는 수행할 수 없었으며 이동·삭제 경위는 미확인이다. 이번 정비에서는 기존 6문서만 수정했고 파일 이동·삭제는 하지 않았다. 이는 위 초기 작성 당시 기록과 구분한다.

## 설치·하드웨어 초기 조사 — 2026-10-29 이관 기록

| 항목 | 확인 방법·결과 | 판정 한계 |
|---|---|---|
| 엔진 | `C:\Program Files\Epic Games\UE_5.5`의 Build.version: 5.5.4, CL 40574608. Launcher 기록·UnrealEditor.exe·InstalledBuild.txt 존재 | 에디터 실행/서버 빌드 미실시. 루트 Setup.bat·GenerateProjectFiles.bat 없음 |
| 소스 후보 | `C:\Users\star_\OneDrive\Desktop\ETC\UnrealEngine-release`의 Build.version: 5.6.1, Setup.bat·GenerateProjectFiles.bat 존재 | `.git`, UnrealEditor.exe, UnrealBuildTool.dll, 동봉 DotNet 폴더 없음. 출처·수정 여부·완전성 미확인 |
| IDE | vswhere: VS Community 2022 17.14.12, 설치 완료·실행 가능·재부팅 불필요, C++ x64/x86 구성 확인 | 게임 빌드 성공과 별개 |
| 컴파일러 | VS의 `VC/Tools/MSVC/14.44.35207`, cl.exe 19.44.35214.0 | 두 엔진의 Windows_SDK.json 선호 범위는 14.38.33130~14.38.99999. 설치 14.44는 선호 범위 밖이며 명시 금지 범위는 아님; 성공/실패 미검증 |
| 기타 빌드 도구 | MSBuild.exe 17.14.18.37206, VS 동봉 CMake 3.31.6 파일 존재 | 일반 PATH 미등록과 미설치를 구분 |
| SDK | 10.0.26100.0의 Windows.h·kernel32.lib·rc.exe 존재. 10.0.10240.0 Include/Lib 폴더도 있음 | 구형 폴더 완전성 미확인. 엔진 설정 Main 22621, Min 19041; 26100은 설정상 범위 내이나 빌드 미검증 |
| .NET | `dotnet --list-sdks`: 9.0.304. UE 5.5.4 내부 8.0.300 폴더 | 의존성 전체 준비 완료 아님 |
| Git | `git --version`: 2.55.0.windows.2, `git lfs version`: 3.7.1 | CoopRPG 저장소 미초기화 |
| 진단 도구 | UE 5.5.4 UnrealInsights.exe 존재 | 실행·트레이스 수집 미실시 |
| 저장 공간 | Get-PSDrive: C: 여유 약 66.8GiB, 사용 약 409.0GiB. 노출된 Temp도 C: 사용 | 당시 값. 소스·의존성·중간 산출물·패키지 공간 예산 미산정 |
| 하드웨어 | 레지스트리: i5-13500HX, RTX 4060 Laptop GPU·8GiB VRAM | RAM은 CIM 접근 제한으로 미확인 |
| OS | 레지스트리: DisplayVersion 25H2, 빌드 26200.9711, ProductName은 Windows 10 Home | 제품명은 상충 가능성이 있어 단정하지 않음 |

조사는 설치 기록·등록 키·표준 설치 위치·포트폴리오 및 사용자 관련 폴더의 파일명 중심이었다. 시스템 전수 조사가 아니며 다른 엔진이 절대 없다고 주장하지 않는다. 이관된 조사에는 원시 로그 파일이 별도로 보관되어 있지 않다. 위 방법과 경로로 후속 재확인이 가능하다.

**선택할 사항(D01-S):** 채택 엔진·소스·도구·저장 경로·공간 계획. 선행 조사(D01-F)와 실제 빌드·실행 검증(D01-H)은 별개다. 5.4는 과거 검토안이고 5.5.4·5.6.1도 자동 채택하지 않는다. 독립 서버 빌드 절차는 선택 버전의 공식 문서와 로컬 구성으로 다시 확인한다. 현재 실행 가능한 빌드/시험 명령은 확정되지 않았다.

## D01-F 환경 재조사 — 2026-10-29

허용 범위: 읽기 조사와 문서 반영. 첫 조사 단위의 60분 상한 안에서 수행했으며 로컬 수치 관측은 18:05~18:07 KST다. 원시 출력은 이 채팅의 도구 실행 기록에 있고 별도 로그 파일은 생성하지 않았다. 아래 정보는 실제 준비 작업 전에 필요한 항목을 다시 확인한다.

### 확인 사실

| 대상 | 방법·관측 결과 | 해석·한계 |
|---|---|---|
| 작업 위치 | `Get-Location`: `C:\CoopRPG`. `rg --files --hidden`: 기존 문서 6개 | `.uproject`·게임 코드·빌드 산출물 없음. `Docs/X`도 현재 없음 |
| RAM | CIM 두 조회는 접근 제한. Win32 `GlobalMemoryStatusEx`를 도구 프로세스에서 조회: OS 사용 가능 물리 메모리 총 31.70GiB, 당시 가용 13.78GiB | 실장 RAM 용량·모듈 구성과 구분. `GetPhysicallyInstalledSystemMemory`도 오류 5(접근 거부). 사용 가능 총량은 확인되어 RAM을 전부 미확인으로 취급하지 않음 |
| 저장 공간 | `Get-PSDrive -PSProvider FileSystem`: C: 여유 64.82GiB, 사용 410.95GiB. Temp는 같은 C: | 조사에 노출된 파일시스템 드라이브 기준. 엔진 소스·의존성·중간 산출물·프로젝트·패키지의 합계 공간 예산은 미산정. 충분/부족 확정 불가 |
| 설치 엔진 | `C:\Program Files\Epic Games\UE_5.5\Engine\Build\Build.version`: 5.5.4, CL 40574608. InstalledBuild.txt·UnrealEditor.exe·UBT DLL·동봉 .NET 실행 파일 존재. 루트 Setup/GenerateProjectFiles 없음 | 설치형 엔진 확인. 소스 빌드 완료 엔진과 구분. 에디터 실행·서버 빌드 미실시 |
| 소스 후보 | `C:\Users\star_\OneDrive\Desktop\ETC\UnrealEngine-release`의 Build.version: 5.6.1, CL 0, Compatible CL 43139311, BranchName UE5. `.git` 없음. Setup·GenerateProjectFiles·Commit.gitdeps.xml·GitDependencies.exe·UnrealServer.Target.cs 존재. 에디터·UBT DLL·동봉 DotNet 폴더 없음 | 엔진 소스 트리 후보이며 즉시 빌드 가능한 상태는 미확인. CL 0·BranchName·폴더명으로 공식 태그나 수정 여부 확정 불가. 전체 파일 무결성 검사 미실시 |
| 출처 흔적 | 후보 README의 `Zone.Identifier`에 ZoneId=3, ReferrerUrl이 로컬 Desktop의 `UnrealEngine-release.zip` 경로. 해당 ZIP은 Desktop·Desktop/ETC의 두 후보 경로에서 찾지 못함 | 로컬 ZIP에서 풀린 흔적이며 원래 다운로드 URL은 아님. 공식 Epic 원본·태그·로컬 수정 이력의 증거로 사용할 수 없음 |
| OneDrive 위치 | 소스 루트 항목이 OneDrive 아래 있으며 ReparsePoint 속성 표시 | 이 속성만으로 파일의 온라인 전용 여부를 단정하지 않음. 전체 로컬 가용성·동기화/잠금 영향 미검증 |
| Visual Studio | `vswhere`: Community 2022 17.14.12, complete/launchable, 재부팅 불필요. NativeDesktop·NativeGame·VC.Tools.x86.x64 구성 등록 확인 | IDE 버전·등록 상태 확인. 실제 엔진 빌드 성공과 별개 |
| MSVC | 설치 폴더 14.44.35207, cl.exe 파일 버전 19.44.35214.0. 두 엔진의 `Engine/Config/Windows/Windows_SDK.json`: 최소 14.38.33130, 선호 14.38.33130~14.38.99999, 금지 14.39 및 14.40의 명시 범위 | 설치 14.44는 선호 범위 밖·명시 금지 범위 밖. 즉시 실패나 성공을 단정하지 않음. 14.38 병행 설치는 D01-S에서 검토할 후보 |
| Windows SDK | 26100의 Windows.h·x64 kernel32.lib·rc.exe 존재. 두 엔진 설정: Min 19041, Main 22621, Max 10.9.99999.0 | 26100은 로컬 설정상 허용 범위. 10240 폴더에는 검사한 세 파일이 없어 완전한 SDK로 세지 않음. 실제 빌드 미검증 |
| .NET·Git | `dotnet --list-sdks`: 시스템 SDK 9.0.304만 출력. 두 엔진 GetDotnetPath.bat는 동봉 8.0.300을 지정. 설치형에는 해당 dotnet.exe 존재, 소스 후보에는 폴더 없음. Git 2.55.0.windows.2·LFS 3.7.1 재확인 | 시스템 .NET 9가 있다는 이유로 소스 후보의 .NET 8 준비가 완료됐다고 판단하지 않음. 배치 파일은 읽기만 수행 |

재현 경로 보완: Windows SDK·MSVC 선호 범위의 원본 파일은 두 엔진 모두 `Engine/Config/Windows/Windows_SDK.json`이다. 처음 조회한 UBT 하위 경로에는 없었으나 파일명 검색으로 실제 경로를 찾아 읽었다.

### 공식 요구와 대조

2026-10-29에 버전별 공식 페이지를 열어 확인했다. [UE 5.5 VS 설정](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine?application_version=5.5)과 [UE 5.6 VS 설정](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine?application_version=5.6)은 VS 2022 최소 17.8, MSVC 최소·권장 14.38.33130, SDK 최소 19041·권장 22621 이상, .NET 8을 제시한다. VS 권장은 5.5에서 17.10, 5.6에서 17.14이며 IDE 버전과 MSVC 버전을 별도로 판단한다. 이 공식 표는 바이너리 엔진 통합 안내이므로 소스 후보의 전체 빌드 성공 보장은 아니다.

[UE 5.6 데디케이트 서버 안내](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-dedicated-servers-in-unreal-engine?application_version=5.6)는 소스 빌드 엔진과 클라이언트/서버 게임플레이를 지원하는 C++ 프로젝트를 전제로 한다. 이 공식 경로를 기준으로 소스·의존성 준비가 필요하다. 설치형 5.5.4의 존재만으로 해당 조건을 충족했다고 판단하지 않는다. 문서 예제의 Lyra를 CoopRPG의 필수 자산이나 설치 대상으로 채택한 것은 아니다.

### 남은 사항과 다음 조치

- **미확인 사실(D01-F 부분 확인):** 소스의 실제 다운로드 출처·태그/브랜치·수정 이력과 전체 원본 무결성. 로컬 근거만으로 확인할 수 없어 사용자에게 취득 경위를 질문했다. 답변을 받더라도 사용자 진술과 원본 대조 검증을 구분한다. 기억나지 않으면 미확인으로 유지하고 공식 출처를 재확보하는 경로를 D01-S에서 비교할 수 있다.
- **추가 조사 대상:** 소스 트리·의존성·중간 산출물·프로젝트·패키지의 저장 공간 예산, OneDrive 밖 작업 경로의 준비 조건. 전체 소스 크기·실제 디스크 점유량은 이번에 측정하지 않았다. 이는 추가 조사와 구성안 비교로 해결하며 사용자에게 기술 용량 추정을 맡기지 않는다.
- **선택할 사항(D01-S):** 엔진 버전·신뢰할 소스 확보 방식, MSVC 14.38 병행 준비 여부, SDK 유지 여부, .NET 의존성 확보 방법, 저장 위치와 공간 확보 방안. 이번에는 채택하지 않았다.
- **검증할 가설(D01-H):** 선택한 조합의 엔진/서버/클라이언트 빌드·실행 성공과 실제 공간 사용량. 설치·다운로드·Setup·프로젝트 생성·게임 구현·빌드를 실행하지 않았으므로 미실행이다.
- **범위 밖:** 최소 자산(D03-F/S), 프로젝트/Git 구조(D02-S), 두 PC 연결(D11)은 이번 조사에서 해소하지 않았다.

문서 반영 확인: Environment·Roadmap의 결과/상태를 대조하고 상대 링크 9개와 사용 앵커를 검사해 오류 0건을 확인했다. 기존 임시 경로 `Docs/X`는 없어 보존 해시 비교가 불가능했다. 이번 작업에서 기존 파일을 삭제·이동하지 않았다.

후속 동일성 비교용 소스 후보 표본 SHA-256(공식 원본과의 일치 인증 아님):

| 상대 경로 | SHA-256 |
|---|---|
| Engine/Build/Build.version | 6D625F43E1BDE27436C437D527C63DE7CC0B5EDC627DEF2DD6E494B10563D32D |
| Engine/Config/Windows/Windows_SDK.json | BD2AA569569F22530464FE160DCD8940878099FBEAA2E20DC7997804798BA3D9 |
| Setup.bat | 263CBA752BCED87195FFDCC8456BC55E6F79847C07ED28D98B7C6C60B295B96C |

## 기존 프로젝트 검토 — 2026-10-29 기록

| 자료 | 확인 범위 |
|---|---|
| [Project Stellar](https://github.com/Razria93/Portfolio_UE5.4_verGit) | README, S37 실행 전환 문서, CAction.cpp, CActionOrchestratorComponent.cpp, Portfolio.Build.cs, 자산 출처 HTML |
| [Asset Reference Inspector](https://github.com/Razria93/Plugin_Portfolio_UE5.4) | README의 에디터 도구·작업 기여·검증 범위 |
| [Graphics Study Archive](https://github.com/Razria93/Graphics_Study_Archive) | README의 학습·반복 실행·촬영·검증 범위 |
| [통합 포트폴리오 저장소](https://github.com/Razria93/razria93.github.io) | 스텔라 페이지 HTML 소스. 렌더링 페이지 조회 실패 후 소스로 확인 |

스텔라 트리 조회 기준 SHA는 `c8f62ea79df447d2193ce96b60e9a012b49af6d7`이었다. 후속 개별 파일은 당시 기본 브랜치로 읽었으므로 모든 검토 파일을 이 SHA에 고정한 감사로 표현하지 않는다. 기존 게임 빌드·실행·PDF 전체·영상 재생 검증은 하지 않았다.

확인 코드에는 Start/Interrupt/Complete, CleanupRuntimeEffects/ClearRuntime, 몽타주 serial·generation 확인, 요청 검증과 실행 결정 분리가 있었다. 확인한 Build.cs에는 GameplayAbilities/GameplayTags/GameplayTasks 의존성이 없었다. 이 표본으로 전체 저장소의 네트워크 기능 부재를 단정하지 않는다.

**평가:** 실행·취소·수명 개념을 배우는 데 유리한 경험이나 GAS 예측·복제 숙련의 증거는 아니다. 기존 serial은 PredictionKey와 같지 않다. 공개 자료는 사용자 판단과 AI 구현·검증을 구분하므로 결과물 규모를 개인 독립 구현 속도로 환산하지 않는다.

## 자산과 로컬 위치

[자산 출처 목록](https://github.com/Razria93/Portfolio_UE5.4_verGit/blob/main/portfolio-web/site/docs/stellar_resource_catalog_20261021/assets.html)에는 Stellar Girl Celeste, UE Mannequin/Quinn, Basic Sword Animation Pack, One Handed Sword Animset Pro, 부트캠프 제공 자료 등이 있다. 이는 재사용 후보의 출처 확인이며 신규 프로젝트 이용·원본 재배포 권한 확인이 아니다.

`C:\PORTFOLIO\Portfolio_Project\Portfolio_Project.uproject`는 EngineAssociation 5.5, `C:\Plugin_Portfolio_UE5.4\Portfolio_PlugIn.uproject`는 5.4의 별도 에디터 플러그인 프로젝트였다. 로컬 후보를 원격 스텔라의 동일 사본으로 확정하지 않았다. 다른 프로젝트의 지침을 CoopRPG 지침으로 가져오지 않았다.

**다음 확인:** D03-F의 로컬 원본 위치, 최소 캐릭터·검·단타·기본 이동 모션의 실제 이용 조건과 전용 Notify 의존성. 환경의 남은 사항은 위 D01-F 재조사 절을 따른다. 자산 복사·이식은 미실시다.

## 이관 경계

임시 문서에서 경로·설치 조사 사실, 사용자 조건, 기존 자료 검토 범위와 중요한 미결정만 선별했다. 새 목적·AI/MVP 운영 원칙은 이후 사용자 지시를 반영했다. 과거 24일 상세 일정·기능 번호·고정 콘텐츠 수·제출 분량은 채택하지 않았다. 일부 시험 수치는 검증 색인에 미확정 후보로만 보존했다.

새 작업은 현재 문서만으로 이어갈 수 있으며 `Docs/X`를 필수로 읽을 필요가 없다. 임시 문서는 수정·삭제하지 않는다.
