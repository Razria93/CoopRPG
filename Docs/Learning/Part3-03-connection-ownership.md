# Part3 3강 · 커넥션과 오너십

CoopRPG · 2026-11-06 · 상태: 자막 기반 파일럿

강의: [이득우의 언리얼 프로그래밍 Part3 3강](https://www.inflearn.com/courses/lecture?courseId=332060&tab=script&type=LECTURE&unitId=184571) · 예제: [UnrealProgrammingPart3 브랜치 3](https://github.com/ideugu/UnrealProgrammingPart3/tree/3) · 강의 예제 엔진: 1강 설명 기준 UE 5.2

근거 범위: 3강 스크립트의 처음부터 마지막까지 확인. 강의 예제 소스·CoopRPG 코드와 대조하지 않았고 직접 실행하지 않았다. 아래 함수명은 강의 설명을 정리한 것이며 구현 경로는 미확인이다.

## 이 강의가 답하는 질문

원격 클라이언트의 액터는 언제 초기 데이터를 받은 상태가 되는가? 서버와 클라이언트의 연결 객체는 어떻게 구성되며, 어떤 액터가 특정 플레이어의 연결을 소유하는가? 2강의 로그인 흐름을 액터 초기화와 오너십까지 이어서 설명한다.

## 핵심 원리와 적용 조건

`PostInitializeComponents()`는 액터 구성요소 초기화 뒤 호출된다. 네트워크로 복제된 클라이언트 액터의 초기 데이터 수신 완료를 뜻하지는 않는다. `PostNetInit()`은 복제되어 생성되는 원격 액터의 초기 네트워크 설정이 끝난 시점을 확인하는 데 사용한다. `BeginPlay()`는 게임플레이 시작 단계다. 강의에서는 `StartPlay()`의 상위 구현 호출을 막아도 접속·액터 준비는 진행되지만 `BeginPlay()`가 실행되지 않는 모습을 보였다. 따라서 생성, 초기 복제, 게임 시작을 같은 시점으로 취급하지 않는다.

게임 콘텐츠에서는 `PlayerController`가 플레이어와 접속을 대표한다. 강의는 `NetConnection`을 연결 관리, `NetDriver`를 연결 관리와 실제 송수신의 하위 계층으로 설명한다. 서버 NetDriver는 원격 클라이언트 연결들을, 클라이언트 NetDriver는 서버 연결 하나를 관리한다. 리슨 서버가 초기에 Standalone으로 표시되다가 서버로 바뀌는 실습 로그는 `Listen()` 과정에서 NetDriver가 준비되는 흐름과 연결된다.

오너십은 액터의 `Owner` 관계를 따라 특정 플레이어의 `PlayerController`와 `NetConnection`에 도달하는 관계다. 예를 들어 무기 액터 → 캐릭터 → PlayerController → 해당 연결로 이어진다. 이 정보는 이후 RPC 대상과 소유자 관련 복제 조건을 판단할 때 중요하다. 이 강의에서는 해당 기능의 사용 결과까지 구현하지 않고 관계의 형성을 확인한다.

## 예제 코드와 실행 흐름

| 출처·확인 상태 | 함수·대상 | 강의에서 설명한 역할 |
|---|---|---|
| 강의 설명, 소스 미대조 | GameMode `PreLogin()` | 에러 메시지를 넣어 외부 접속을 거부하는 시연 |
| 강의 설명, 소스 미대조 | PlayerController `PostInitializeComponents()`·`PostNetInit()` | 서버와 원격 클라이언트의 초기화 시점을 로그로 비교 |
| 강의 설명, 소스 미대조 | World `Listen()`·`InternalGetNetMode()` | NetDriver 생성과 넷모드 변화의 관계를 엔진 소스로 설명 |
| 강의 설명, 소스 미대조 | GameMode `PostLogin()`·PlayerController `PostNetInit()` | 서버의 클라이언트 연결 수와 클라이언트의 서버 연결을 출력 |
| 강의 설명, 소스 미대조 | PlayerController `OnPossess()`·Pawn `PossessedBy()` | 서버 빙의 중 Owner가 설정되는 시점을 로그로 확인 |
| 강의 설명, 소스 미대조 | Pawn `OnRep_Owner()` | 클라이언트에서 복제된 Owner 변경을 확인 |

서버에서는 `OnPossess()` → `PossessedBy()`의 상위 로직 → Pawn의 Owner 설정으로 이어진다. 클라이언트는 같은 빙의 로직을 실행해 Owner를 설정하는 대신 서버에서 복제된 Owner를 받는다. 강의 로그에서는 `OnRep_Owner()` 호출을 통해 이를 확인한다.

## 관찰과 해석

- 강의 시연: `PreLogin()`에서 에러를 지정하자 외부 클라이언트 접속이 거부되었다. 이것은 강의 시연이며 CoopRPG의 거부 경로 시험 결과가 아니다.
- 강의 시연: 클라이언트를 추가할 때 서버의 클라이언트 연결 수는 늘고, 각 클라이언트는 서버 연결 하나를 출력했다.
- 강의 시연: 서버에서 `PossessedBy()` 상위 로직 뒤 Pawn의 Owner가 PlayerController로 바뀌었고, 클라이언트에서는 Owner 복제에 따른 콜백을 관찰했다.
- 미확인: 예제 브랜치의 정확한 파일 경로·구현 코드, UE 5.8.4의 해당 흐름, CoopRPG의 리슨 서버 실행 결과.

## CoopRPG에 연결할 점

현재 단계에서는 리슨 서버의 호스트와 원격 클라이언트를 구분해 초기화·소유권을 확인할 필요가 있다. GAS를 붙일 때도 Ability를 실행할 액터와 접속 소유 관계를 별도로 추적할 근거가 된다. 이는 적용 제안이며 CoopRPG에 구현하거나 검증한 결과는 아니다. 전용 서버로 분리할 때는 로컬 호스트가 없는 조건에서 같은 흐름을 다시 확인한다.

## 다시 확인할 질문·출처

- UE 5.8.4 및 현재 예제 소스에서 각 로그의 정확한 파일·함수 위치는 어디인가?
- 원격 클라이언트 자신의 Pawn과 다른 플레이어 Pawn에서 Owner 복제·참조 결과가 어떻게 다른가?
- 강의 주요 위치: 액터 초기화 1:03~9:51, 연결 구조 9:51~23:46, 오너십 23:55~33:55, 서버/클라이언트 Owner 설정 33:55~43:53.
- [강의가 제시한 액터와 접속 소유 문서](https://bit.ly/uenetownership) · [프로젝트 학습 기록](../Development.md#learning-operation) · [시험 기록](../Validation/Index.md).
