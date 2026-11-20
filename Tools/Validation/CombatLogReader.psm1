Set-StrictMode -Version Latest

# FileGeneration describes a log file reset, never a game world/actor generation.
function New-CombatLogReader([string]$Path) {
    @{Path=$Path;Offset=0L;Pending=[byte[]]@();Initialized=$false;SkipFirst=$false;Generation=0;WorldSequence=0;WorldActive=$false;Prefix=[byte[]]@();LastReadUtc=$null;State='Waiting';Error='';InitialSkippedBytes=0L;UnreadBytes=0L}
}

function Read-CombatLogRecords([hashtable]$State, [int]$MaxBytes=262144) {
    if(!(Test-Path -LiteralPath $State.Path -PathType Leaf)){$State.State='Waiting';return}
    $stream=$null
    try {
        $stream=[IO.File]::Open($State.Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        $head=New-Object byte[] ([int][Math]::Min(64L,$stream.Length))
        if($head.Length){[void]$stream.Read($head,0,$head.Length)}
        $prefixChanged=$false
        for($i=0;$i -lt [Math]::Min($State.Prefix.Length,$head.Length);$i++){
            if($State.Prefix[$i] -ne $head[$i]){$prefixChanged=$true;break}
        }
        if($State.Initialized -and ($stream.Length -lt $State.Offset -or $prefixChanged)){
            $State.Offset=0L;$State.Pending=[byte[]]@();$State.SkipFirst=$false;$State.Generation++;$State.WorldSequence=0;$State.WorldActive=$false
        }
        if(!$State.Initialized){
            $State.Offset=[Math]::Max(0L,$stream.Length-131072L)
            $State.InitialSkippedBytes=$State.Offset;$State.SkipFirst=$State.Offset -gt 0;$State.Initialized=$true
        }
        $State.Prefix=$head;$State.State='Ready';$State.Error=''
        $State.UnreadBytes=[Math]::Max(0L,$stream.Length-$State.Offset)
        $count=[int][Math]::Min([Math]::Max(1,$MaxBytes),$State.UnreadBytes)
        if($count -eq 0){return}
        [void]$stream.Seek($State.Offset,[IO.SeekOrigin]::Begin)
        $bytes=New-Object byte[] $count;$read=$stream.Read($bytes,0,$count)
        $base=$State.Offset-$State.Pending.Length
        $data=New-Object byte[] ($State.Pending.Length+$read)
        [Array]::Copy($State.Pending,0,$data,0,$State.Pending.Length)
        [Array]::Copy($bytes,0,$data,$State.Pending.Length,$read)
        $State.Offset+=$read;$State.UnreadBytes=[Math]::Max(0L,$stream.Length-$State.Offset);$State.LastReadUtc=[DateTime]::UtcNow
        $begin=0
        for($i=0;$i -lt $data.Length;$i++){
            if($data[$i] -ne 10){continue}
            $length=$i-$begin
            if($length -gt 0 -and $data[$i-1] -eq 13){$length--}
            if($State.SkipFirst){$State.SkipFirst=$false}
            elseif($length -gt 0){
                $line=[Text.Encoding]::UTF8.GetString($data,$begin,$length)
                if($line.Contains('LogWorld: Bringing World ') -and $line.Contains(' up for play')){
                    $State.WorldSequence++;$State.WorldActive=$true
                }elseif($line -match 'LogLoad: LoadMap:|LogWorld: (?:BeginTearingDown for |.*SeamlessTravel)|LogNet: Welcomed by server'){
                    $State.WorldActive=$false
                }
                $world=if($State.WorldActive){$State.WorldSequence}else{0}
                [pscustomobject]@{Line=$line;ByteOffset=[long]($base+$begin);FileGeneration=$State.Generation;WorldSequence=$world}
            }
            $begin=$i+1
        }
        $State.Pending=New-Object byte[] ($data.Length-$begin)
        [Array]::Copy($data,$begin,$State.Pending,0,$State.Pending.Length)
    } catch {$State.State='Error';$State.Error=$_.Exception.Message;throw}
    finally {if($null -ne $stream){$stream.Dispose()}}
}

function Read-CombatLogLines([hashtable]$State) {
    foreach($record in @(Read-CombatLogRecords $State)){[string]$record.Line}
}

function ConvertTo-CombatLogEvent([string]$Line) {
    $utc=$null;$time='시각 미기록'
    if($Line -match '^\[(?<time>\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]'){
        $parsed=[DateTime]::MinValue
        if([DateTime]::TryParseExact($Matches.time,'yyyy.MM.dd-HH.mm.ss:fff',[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::None,[ref]$parsed)){
            $utc=[DateTime]::SpecifyKind($parsed,[DateTimeKind]::Utc);$time=$utc.ToLocalTime().ToString('HH:mm:ss.fff')
        }
    }
    $category='';$level='Log';$message=$Line
    if($Line -match '(?<category>Log\w+):\s*(?:(?<level>VeryVerbose|Verbose|Display|Warning|Error|Fatal):\s*)?(?<message>.*)$'){
        $category=$Matches.category;$message=$Matches.message
        if($Matches['level']){$level=$Matches['level']}
    }
    $fields=@{}
    foreach($m in [regex]::Matches($message,'(?<key>\w+)=(?<value>[^\s]+)')){$fields[$m.Groups['key'].Value]=$m.Groups['value'].Value}
    $eventCode=($message -split '\s+',2)[0]
    $kind='Info';$label='';$detail='';$stage='상태';$outcome='관찰';$reason='';$known=$true
    switch -Regex ($eventCode) {
        '^ManualFocusClickConsumed$' {$label='포커스 클릭';$detail='공격 요청 없음 · 다음 클릭부터 공격';$kind='Focus';$stage='입력';$outcome='소비';break}
        '^ManualInputFlushed$' {
            $label='창 전환 키 초기화'; $kind='Focus'; $stage='창 전환'
            if ($fields.ContainsKey('HeldWBefore') -and $fields.ContainsKey('W')) {
                $detail="W $($fields['HeldWBefore']) → $($fields['W'])"
            }
            break
        }
        '^ManualInputEnabled$' {$label='수동 검증 입력 설정 완료';$stage='초기화';$detail='커서 표시 · 마우스 잠금 해제';break}
        '^AttackInput$' {$label='공격 입력 감지';$kind='Input';$stage='입력';break}
        '^AttackDispatch$' {
            $stage='함수 반환'; $kind='Input'
            switch ($fields['Submitted']) {
                '1' { $label='GAS 활성화 함수 반환 · true'; $outcome='제출' }
                '0' { $label='GAS 활성화 함수 반환 · false'; $outcome='미제출' }
                default { $label='GAS 활성화 함수 반환 · 값 미확인'; $outcome='미분류' }
            }
            break
        }
        '^(AttackBlocked|AttackRejected)$' {
            $label=if($eventCode -eq 'AttackBlocked'){'클라 차단'}else{'서버 거절'}
            $stage=if($eventCode -eq 'AttackBlocked'){'로컬 검사'}else{'서버 판정'};$outcome='거절';$kind='Reject'
            $reason=[string]$fields['Reason']
            $names=@{Cost='비용 부족';Cooldown='쿨다운 중';OutOfRange='사거리 밖';InvalidTarget='유효하지 않은 표적';AttackerDead='공격자 HP 0';NotPlaying='라운드 종료';Unarmed='무기 미장착';CommitFailed='커밋 실패 · 세부 원인 미확인'}
            $detail=if($names.ContainsKey($reason)){$names[$reason]}elseif($reason){$reason}else{'사유 미기록'}
            if($reason){$detail+=" ($reason)"}
            if($fields.ContainsKey('Distance')){$detail+=" · 거리 $($fields['Distance'])cm"}
            if($fields.ContainsKey('Range')){$detail+=" / 허용 $($fields['Range'])cm"}
            if($fields.ContainsKey('Stamina')){$detail+=" · Stamina=$($fields['Stamina'])"}
            if($fields.ContainsKey('Remaining')){$detail+=" · 남은 시간 $($fields['Remaining'])s"}
            break
        }
        '^CancelRPCCalled$' {$label='취소 RPC 호출';$stage='RPC 호출';$outcome='호출';break}
        '^CancelRPCReceived$' {$label='취소 RPC 수신';$stage='RPC 수신';$outcome='수신';break}
        '^LootRoundCreated$' {$label='루팅 라운드 생성';$detail="상자 재고 $($fields['Stock'])개";break}
        '^LootRoundPhase$' {$label='루팅 라운드 상태';$detail=[string]$fields['Phase'];break}
        '^LootReadyRequested$' {$label='준비 RPC 호출';$stage='RPC 호출';$outcome='호출';break}
        '^LootPlayerReady$' {$label='플레이어 준비 완료';$stage='서버 판정';break}
        '^RoundReadySlotsReplicated$' {
            $label='팀 준비 상태 복제';$stage='복제 관찰'
            $names=@{Disconnected='미접속';Waiting='준비 전';Ready='준비 완료'}
            $p1=$names[[string]$fields['P1']];$p2=$names[[string]$fields['P2']]
            if(!$p1){$p1='미계측'}
            if(!$p2){$p2='미계측'}
            $detail="P1 $p1 · P2 $p2";break
        }
        '^ChestTakeRequested$' {$label='회복약 획득 RPC 호출';$detail="요청 $($fields['Request'])";$stage='RPC 호출';$outcome='호출';break}
        '^ChestWeaponRequested$' {$label='근접무기 획득 RPC 호출';$detail="요청 $($fields['Request'])";$stage='RPC 호출';$outcome='호출';break}
        '^RecoveryUseRequested$' {$label='회복약 사용 RPC 호출';$stage='RPC 호출';$outcome='호출';break}
        '^ChestViewRequested$' {
            $label=if($fields['Action'] -eq 'Close'){'상자 닫기 RPC 호출'}else{'상자 열기 RPC 호출'}
            $stage='RPC 호출';$outcome='호출';break
        }
        '^ChestClosed$' {
            $reason=[string]$fields['Reason']
            $label=if($reason -eq 'OutOfRange'){'상자 자동 닫기'}else{'상자 닫기 확정'}
            if($reason -eq 'OutOfRange'){$detail='거리 이탈'}
            $stage='서버 판정';$outcome='승인';break
        }
        '^ChestGranted$' {
            $label='회복약 지급';$stage='서버 판정';$outcome='승인'
            $detail="요청 $($fields['Request']) · 남은 수량 $($fields['Stock'] -replace '->',' → ')"
            break
        }
        '^ChestWeaponGranted$' {
            $label='근접무기 지급·장착';$stage='서버 판정';$outcome='승인'
            $detail="요청 $($fields['Request']) · 남은 무기 $($fields['Stock'] -replace '->',' → ')"
            break
        }
        '^RecoveryUsed$' {
            $label='회복약 사용 승인';$stage='서버 판정';$outcome='승인'
            $detail="HP $($fields['HP'] -replace '->',' → ') · 보유량 $($fields['Recovery'] -replace '->',' → ')"
            break
        }
        '^RecoveryUseRejected$' {
            $label='회복약 사용 거절';$stage='서버 판정';$outcome='거절';$reason=[string]$fields['Reason']
            $names=@{WrongRound='라운드 불일치';NotPlaying='라운드 대기 중';NotAlive='사망 상태';NoRecovery='회복약 없음';FullHealth='최대 HP';InvalidEffect='회복 효과 생성 실패';NoHealing='회복량 0'}
            $detail=if($names[$reason]){$names[$reason]}else{$reason}
            if($fields.ContainsKey('Recovery')){$detail+=" · 보유량 $($fields['Recovery'])개"}
            break
        }
        '^ChestRejected$' {
            $reason=[string]$fields['Reason'];$stage='서버 판정';$outcome='거절'
            switch($fields['Action']){
                'Open'  {$label='상자 열람 거절'}
                'Close' {
                    if($reason -eq 'NotOpen'){$label='상자 닫기 무시';$outcome='관찰'}
                    else{$label='상자 닫기 거절'}
                }
                'TakeWeapon' {$label='근접무기 획득 거절'}
                default {$label='회복약 획득 거절'}
            }
            $names=@{WrongRound='라운드 불일치';NotPlaying='라운드 대기 중';InvalidChest='대상 불일치';NotAlive='사망 상태';OutOfRange='거리 초과';NotOpen='상자 닫힘';DuplicateRequest='중복 요청';Depleted='회복약 소진';AlreadyEquipped='이미 장착함';InvalidItem='유효하지 않은 아이템'}
            if($fields['Action'] -eq 'TakeWeapon'){$names['Depleted']='근접무기 소진'}
            $detail=if($names[$reason]){$names[$reason]}else{$reason}
            if($fields['Request'] -and $fields['Request'] -ne '0'){$detail+=" · 요청 $($fields['Request'])"}
            break
        }
        '^(ChestOpened|ChestViewReplicated)$' {
            if($eventCode -eq 'ChestOpened'){
                $label='상자 열람 승인';$stage='서버 판정';$outcome='승인'
            }else{
                $label=if($fields['Open'] -eq '1'){'내 상자 열람'}else{'내 상자 닫기'}
                $stage='복제 관찰'
            }
            if($eventCode -eq 'ChestOpened' -or $fields['Open'] -eq '1'){
                $stock=@(
                    if($fields.ContainsKey('WeaponStock')){"근접무기 $($fields['WeaponStock'])개"}
                    if($fields.ContainsKey('Stock')){"회복약 $($fields['Stock'])개"}
                )
                $detail=$stock -join ' · '
            }
            break
        }
        '^ChestStockReplicated$' {$label='공용 상자 재고 복제';$detail="남은 회복약 $($fields['Stock'])개";$stage='복제 관찰';break}
        '^WeaponStockReplicated$' {$label='근접무기 재고 복제';$detail="남은 무기 $($fields['Stock'])개";$stage='복제 관찰';break}
        '^MeleeWeaponReplicated$' {
            $label=if($fields['Equipped'] -eq '1'){'근접무기 장착 복제'}else{'근접무기 해제 복제'}
            $playerId=$fields['EquippedPlayerId']
            if($playerId -match '^\d+$'){$detail="PlayerId: $playerId"}
            $stage='복제 관찰';break
        }
        '^RecoveryCountReplicated$' {$label='내 회복약 보유량 복제';$detail="$($fields['Count'])개";$stage='복제 관찰';break}
        '^ShieldMeleeEvaluated$' {
            $stage='서버 판정'
            $label=switch ($fields['Outcome']) {
                'Blocked' {$outcome='차단';'방패 차단'}
                'Hit' {'방패 판정 통과'}
                default {'방패 판정 결과 미확인'}
            }
            $observations=@(
                if($fields.ContainsKey('Angle')){"정면 각도 $($fields['Angle'])°"}
                switch ($fields['Raised']) {'1' {'방패 활성'} '0' {'방패 해제'}}
            )
            $detail=$observations -join ' · '
            break
        }
        '^ShieldWarning(?:Replicated)?$' {
            $replicated=$eventCode -eq 'ShieldWarningReplicated'
            $stage=if($replicated){'복제 관찰'}else{'서버 판정'}
            $phase=switch ($fields['Active']) {'1' {'시작'} '0' {'종료'} default {'상태 미확인'}}
            $label="방패 예고 $phase"
            if($replicated){$kind='Replicate';$label+=' 복제'}
            $observations=@(
                if($fields.ContainsKey('Pattern')){"패턴 $($fields['Pattern'])"}
                if($fields['TargetSlot'] -eq '0'){'대상 해제'}
                elseif($fields['TargetSlot'] -match '^[1-9]\d*$'){"대상 P$($fields['TargetSlot'])"}
                if($fields.ContainsKey('Yaw')){"방향 $($fields['Yaw'])°"}
                if($replicated){
                    switch ($fields['Visible']) {'1' {'예고 표시 중'} '0' {'예고 표시 없음'}}
                }
            )
            $detail=$observations -join ' · '
            break
        }
        '^(EnemyDetected|EnemyChaseStarted|EnemyAttackStarted|EnemyAttackHit|EnemyAttackMissed)$' {
            $stage='서버 판정'
            $label=switch($eventCode){
                'EnemyDetected' {'적 탐지'}
                'EnemyChaseStarted' {'적 추격'}
                'EnemyAttackStarted' {'적 공격 준비'}
                'EnemyAttackHit' {'적 타격 확정'}
                'EnemyAttackMissed' {'적 공격 빗나감'}
            }
            $detail="대상 PlayerId $($fields['TargetPlayerId'])"
            if($eventCode -eq 'EnemyAttackHit'){
                $outcome='승인'
                $detail+=" · 플레이어 HP $($fields['HP'] -replace '->',' → ')"
            }elseif($eventCode -eq 'EnemyAttackMissed'){
                $outcome='빗나감'
                $reason=[string]$fields['Reason']
                $detail+=if($reason -eq 'OutOfRange'){" · 사거리 밖 · $($fields['Distance'])cm / $($fields['Range'])cm"}else{" · $reason"}
            }else{
                $detail+=" · 거리 $($fields['Distance'])cm"
            }
            break
        }
        '^EnemyDeathConfirmed$' {
            $label='적 사망 확정';$stage='서버 판정';$outcome='승인'
            $detail="HP $($fields['HP'] -replace '->',' → ')"
            if($fields['PendingAttackDiscarded'] -eq '1'){$detail+=' · 대기 공격 취소'}
            break
        }
        '^EnemyDeathReplicated$' {
            $label='적 사망 복제';$stage='복제 관찰';break
        }
        '^EnemyLootSpawned$' {
            $label='적 전리품 생성';$detail="ID $($fields['DropId'])";$stage='서버 판정';$outcome='승인';break
        }
        '^EnemyLootReplicated$' {
            $label='적 전리품 복제';$detail="ID $($fields['DropId'])";$stage='복제 관찰';break
        }
        '^EnemyLootTakeRequested$' {
            $label='적 전리품 획득 RPC 호출';$detail="ID $($fields['DropId'])";$stage='RPC 호출';$outcome='호출';break
        }
        '^EnemyLootGranted$' {
            $label='적 전리품 팀 지급';$stage='서버 판정';$outcome='승인'
            $detail="팀 수량 $($fields['TeamLoot'] -replace '->',' → ') · ID $($fields['DropId'])";break
        }
        '^EnemyLootRejected$' {
            $label='적 전리품 획득 거절';$stage='서버 판정';$outcome='거절';$reason=[string]$fields['Reason']
            $names=@{WrongRound='라운드 불일치';NotPlaying='라운드 대기 중';InvalidDrop='전리품 불일치';NotAlive='사망 상태';OutOfRange='거리 초과';AlreadyClaimed='이미 지급됨'}
            $detail=if($names[$reason]){$names[$reason]}else{$reason}
            $detail+=" · 팀 수량 $($fields['TeamLoot']) · ID $($fields['DropId'])";break
        }
        '^EnemyLootClaimReplicated$' {
            $label='적 전리품 획득 상태 복제';$detail="획득됨 · ID $($fields['DropId'])";$stage='복제 관찰';break
        }
        '^TeamLootCountReplicated$' {
            $label='팀 전리품 수량 복제';$detail="$($fields['Before']) → $($fields['After'])";$stage='복제 관찰';break
        }
        '^RoundJoinRejected$' {
            $label='서버 입장 거절';$stage='서버 판정';$outcome='거절';$reason=[string]$fields['Reason'];$detail="$reason · $($fields['Stage'])";break
        }
        '^RoundPlayerAdmitted$' {$label='서버 입장 승인';$detail="$($fields['Map']) · $($fields['Players'])/2";$stage='서버 판정';$outcome='승인';break}
        '^RoundTravelSeatsReserved$' {$label='다음 맵 자리 예약';$detail="$($fields['Map']) · $($fields['Count'])석";break}
        '^RoundTravelSeatJoined$' {$label='다음 맵 기존 참가자 도착';$detail="$($fields['Map']) · 남은 예약 $($fields['Remaining'])석";break}
        '^ConnectionNotice(Queued|Displayed)$' {
            $label=if($eventCode -eq 'ConnectionNoticeDisplayed'){'클라이언트 연결 안내 표시'}else{'클라이언트 연결 안내 수신'}
            $stage='접속'
            $note=[string]$fields['Reason']
            $names=@{RoundFull='정원 초과';RoundInProgress='라운드 진행 중';RoundTravelReserved='기존 참가자 이동 중';RoundFinished='종료된 라운드';UnsupportedJoin='지원하지 않는 입장 방식';ConnectionEnded='서버 연결 종료';ConnectionFailed='서버 연결 실패'}
            $detail=if($names[$note]){$names[$note]}else{$note};break
        }
        '^RoundResultDisplayed$' {$label='라운드 결과 HUD 표시';$detail="라운드 $($fields['Round'])";break}
        '^RoundResultAcknowledged$' {
            $label='결과 표시 확인 수신';$detail="PlayerId $($fields['PlayerId']) · $($fields['Count'])/2";$stage='RPC 수신';$outcome='수신';break
        }
        '^RoundTravelStarted$' {
            $label='다음 맵 이동 시작';$detail=[string]$fields['TargetMap'];$stage='서버 판정';break
        }
        '^RoundTravelFailed$' {
            $label='다음 맵 이동 요청 실패';$detail=[string]$fields['TargetMap'];$stage='서버 판정';$outcome='오류';break
        }
        '^RoundClockStarted$' {$label='라운드 타이머 시작';$detail="$($fields['Duration'])초";break}
        '^RoundResult(Replicated)?$' {
            $reason=[string]$fields['Reason']
            $names=@{Disconnected='플레이어 접속 종료';Death='플레이어 사망';Timeout='시간초과'}
            $outcome=if($fields['Outcome'] -eq 'Aborted'){'중단'}else{'실패'}
            if($eventCode -eq 'RoundResult'){$label="라운드 $outcome 확정";$stage='서버 판정'}
            else{$label="라운드 $outcome 복제";$stage='복제 관찰'}
            $name=if($names[$reason]){$names[$reason]}else{$reason}
            $detail="$name · 팀 점수 $($fields['Score'])점"
            break
        }
        '^ExtractionStarted$' {
            $label='공동 탈출 진행 시작';$detail='두 플레이어 연속 체류 3초';$stage='서버 판정';break
        }
        '^ExtractionReset$' {
            $label='공동 탈출 진행 초기화';$detail="경과 $($fields['Elapsed'])초 · 진행 0";$stage='서버 판정';break
        }
        '^ExtractionResult$' {
            $label='공동 탈출 성공 확정';$detail="팀 점수 $($fields['Score'])점";$stage='서버 판정';$outcome='승인';break
        }
        '^ExtractionProgressReplicated$' {
            $label=if($fields['Phase'] -eq 'Reset'){'탈출 진행 초기화 복제'}else{'탈출 진행 시작 복제'}
            $stage='복제 관찰';break
        }
        '^ExtractionResultReplicated$' {
            $label='공동 탈출 성공 복제';$detail="팀 점수 $($fields['Score'])점";$stage='복제 관찰';break
        }
        '^TestExtractionPosition$' {
            $label='시험 위치 설정';$detail=if($fields['Inside'] -eq '1'){'탈출 구역 안'}else{'탈출 구역 밖'};$stage='상태';break
        }
        '^TestEnemyLootPosition$' {
            $label='시험 위치 설정';$detail="$($fields['Position']) · 거리 $($fields['Distance'])cm";$stage='상태';break
        }
        '^EnemyLootSpawnFailed$' {
            $label='적 전리품 생성 실패';$stage='서버 판정';$kind='Error';$outcome='오류';break
        }
        '^EnemyMovementObserved$' {
            $label='적 이동 복제';$detail="시작 위치에서 $($fields['Distance'])cm";$stage='복제 관찰';break
        }
        '^AttackStarted$' {
            $label='공격 준비 시작';$stage='서버 판정'
            $detail="피해 확정 대기 $($fields['Windup'])초"
            if($fields['Stamina']){
                $label='비용 확정'
                $detail="스태미나 $($fields['Stamina'] -replace '->',' → ')"
            }
            break
        }
        '^AttackCancelled$' {$label='서버 취소 승인';$detail='피해 0 · 적용한 비용·쿨다운 유지';$stage='서버 판정';$outcome='취소';break}
        '^AttackMissed$' {$label='피해 확정 실패';$reason=[string]$fields['Reason'];$detail="피해 0 · 비용·쿨다운 유지 · $reason";$stage='서버 판정';$outcome='빗나감';break}
        '^CancelIgnored$' {$label='취소 요청 무시';$detail='진행 중인 단타 없음';$stage='서버 판정';break}
        '^AttackApproved$' {
            $label='타격 확정';$kind='Approve';$stage='서버 판정';$outcome='승인'
            if($fields['HP']){$detail="표적 HP $($fields['HP'] -replace '->',' → ')"}
            break
        }
        '^(Health|Stamina)Replicated$' {$label=if($eventCode -eq 'HealthReplicated'){'체력 복제'}else{'스태미나 복제'};$detail="$($fields['Before']) → $($fields['After'])";$kind='Replicate';$stage='복제 관찰';break}
        '^CombatSnapshot$' {$label=if($fields['Label'] -eq 'Prepared'){'초기화 완료'}else{'상태 확인 '+$fields['Label']};$detail="HP=$($fields['HP']) · Stamina=$($fields['Stamina']) · Cooldown=$($fields['Cooldown'])";if($fields.ContainsKey('Remaining')){$detail+=" · 남은 시간 $($fields['Remaining'])s"};$kind='Snapshot';break}
        '^CooldownChanged$' {$label=if($fields['Count'] -eq '0'){'쿨다운 만료'}else{'쿨다운 시작'};$detail='Count='+$fields['Count'];$kind='Cooldown';break}
        '^TestActivationRequest$' {$label='서버 시험 요청';$detail='클라이언트 사전 검사만 생략';$kind='Input';$stage='요청';break}
        '^TestPrepareRejected$' {$label='초기화 거절';$detail='개발용 서버 옵션/입력값 확인';$kind='Reject';$outcome='거절';$stage='서버 판정';break}
        '^MovementSnapshot$' {$label='이동 상태';$detail=$message.Substring($eventCode.Length).Trim();$kind='Snapshot';break}
        default {$known=$false}
    }
    if(!$known){
        if($Line -match 'Cmd: (?<command>Coop\.Test\..*)'){$label='콘솔 명령 입력';$detail=$Matches.command;$eventCode='ConsoleCommand';$kind='Input';$stage='입력'}
        elseif($Line -match 'Command not recognized'){$label='알 수 없는 명령';$detail=$message;$eventCode='UnknownCommand';$kind='Reject';$outcome='거절'}
        elseif($Line -match 'Fatal error:|Ensure condition failed:' -or $level -in @('Error','Fatal')){$label='실행 오류';$detail=$message;$eventCode='RuntimeError';$kind='Error';$outcome='오류'}
        elseif($level -eq 'Warning'){$label='실행 경고';$detail=$message;$eventCode='RuntimeWarning';$kind='Warning';$outcome='경고'}
        elseif ($category -eq 'LogAbilitySystem' -and $message -match '^::CallServerTryActivateAbility\s+(?<handle>-?\d+)\s+(?<pressed>[01])\s+(?<prediction>\S.*)$') {
            # The engine logs this at helper entry, before batching or the RPC invocation.
            $fields['SpecHandle']=$Matches.handle
            $fields['InputPressed']=$Matches.pressed
            $fields['PredictionKey']=$Matches.prediction
            $eventCode='GasServerRpcCall'; $label='GAS 서버 RPC 호출'; $stage='RPC 호출'; $outcome='호출'
        }
        elseif ($category -eq 'LogNet' -and $message -match '^(Login request:|Welcomed by server\b|Join request:|Join succeeded:)') {
            $stage = '접속'
            switch -Regex ($message) {
                '^Login request:' { $label='로그인 요청 수신'; $eventCode='LoginRequestReceived'; break }
                '^Welcomed by server\b' { $label='서버 Welcome 수신'; $eventCode='WelcomeReceived'; break }
                '^Join request:' { $label='입장 요청 수신'; $eventCode='JoinRequestReceived'; break }
                '^Join succeeded:' { $label='서버 입장 처리 완료'; $eventCode='JoinSucceeded'; break }
            }
        }
        elseif($Line -match 'LogExit: Exiting\.|Engine exit requested'){$label='종료 요청';$detail='종료 로그 관찰 · 실제 프로세스 상태는 별도 표시';$eventCode='ExitObserved'}
        elseif($category -like 'LogCoopRPG*'){$label='미분류 · '+$eventCode;$detail=$message;$kind='Unknown';$outcome='미분류'}
        else{return}
    }
    $actor=if($fields.ContainsKey('Attacker')){$fields['Attacker']}elseif($fields.ContainsKey('Actor')){$fields['Actor']}elseif($fields.ContainsKey('Pawn')){$fields['Pawn']}else{''}
    $topic = switch ($stage) {
        '접속' { '접속·초기화' }
        '초기화' { '접속·초기화' }
        '입력' { '입력·요청' }
        '요청' { '입력·요청' }
        'RPC 호출' { '입력·요청' }
        'RPC 수신' { '입력·요청' }
        '함수 반환' { '입력·요청' }
        '로컬 검사' { '입력·요청' }
        '서버 판정' { '서버 판정' }
        '복제 관찰' { '복제' }
        default {
            if ($kind -in @('Error','Warning')) { '오류·경고' } else { '상태·기타' }
        }
    }
    $summary=$label+$(if($detail){' · '+$detail}else{''})
    if($level -in @('Warning','Error','Fatal')){$summary="[$level] $summary"}
    [pscustomobject]@{SchemaVersion=1;UtcTime=$utc;Time=$time;SortTicks=$(if($null -ne $utc){$utc.Ticks}else{[long]::MaxValue});Category=$category;Topic=$topic;Level=$level;EventCode=$eventCode;Stage=$stage;Outcome=$outcome;ReasonCode=$reason;Kind=$kind;HealthSubject='';Label=$label;Detail=$detail;Summary=$summary;Actor=$actor;Target=[string]$fields['Target'];RequestId=$null;Relationship='관계 미계측';Fields=$fields;Raw=$Line;Text="[$time] $summary"}
}

function Add-CombatEventContext($Event,[string]$RunId,[string]$SourceId,[string]$SourceLabel,[string]$Path,$Record,[long]$CollectionOrder,[string]$FileKey){
    $Event.Relationship='관계 미계측'
    $world=if($Record.PSObject.Properties['WorldSequence']){[int]$Record.WorldSequence}else{0}
    if(!$FileKey){$FileKey=[uri]::EscapeDataString($Path.ToUpperInvariant())}
    $properties=@{WorldSequence=$world;RunId=$RunId;SourceId=$SourceId;SourceLabel=$SourceLabel;SourcePath=$Path;SourceFileKey=$FileKey;FileGeneration=$Record.FileGeneration;ByteOffset=$Record.ByteOffset;CollectionOrder=$CollectionOrder;EventId=('{0}/{1}/{2}/file{3}/{4}' -f $RunId,$SourceId,$FileKey,$Record.FileGeneration,$Record.ByteOffset)}
    foreach($key in $properties.Keys){$Event | Add-Member -NotePropertyName $key -NotePropertyValue $properties[$key]}
    return $Event
}

function Get-CombatHealthSubjectType($Event) {
    $type=[string]$Event.Fields['SubjectType']
    if($type){return $type}
    switch -Regex ($Event.Actor) {
        '^CoopRPGCharacter(?:_\d+)?$' {return 'Player'}
        '^CoopRPGEnemy(?:_\d+)?$' {return 'Enemy'}
        '^CoopRPGAttackTarget(?:_\d+)?$' {return 'TrainingTarget'}
        default {return 'Unknown'}
    }
}

function Get-CombatHealthPlayerId($Event) {
    $id=[string]$Event.Fields['SubjectPlayerId']
    if($id -match '^[1-9]\d*$'){return $id}
    if($Event.Fields['LocalRole'] -eq 'AutonomousProxy'){
        $id=[string]$Event.Fields['LocalPlayerId']
        if($id -match '^[1-9]\d*$'){return $id}
    }
    return ''
}

function Get-CombatActorEvidenceKey($Event) {
    if($Event.WorldSequence -gt 0 -and $Event.Actor -and $Event.Fields['NetMode'] -eq 'Client'){
        return '{0}/{1}/{2}/{3}/{4}/{5}' -f $Event.RunId,$Event.SourceId,$Event.SourceFileKey,$Event.FileGeneration,$Event.WorldSequence,$Event.Actor
    }
    return ''
}

function Update-CombatHealthLabels($Events,[hashtable]$ClientByPlayerId,[hashtable]$SourceLabelById) {
    # Read evidence from all retained rows before the display filter is applied.
    $byActor=@{}
    $health=New-Object Collections.Generic.List[object]
    foreach($event in $Events){
        $id=''
        if($event.EventCode -eq 'HealthReplicated'){
            $health.Add($event)
            if((Get-CombatHealthSubjectType $event) -eq 'Player'){$id=Get-CombatHealthPlayerId $event}
        }elseif($event.EventCode -eq 'MeleeWeaponReplicated'){
            $id=[string]$event.Fields['EquippedPlayerId']
        }
        if($id -notmatch '^[1-9]\d*$'){continue}
        $key=Get-CombatActorEvidenceKey $event
        if(!$key){continue}
        if(!$byActor.ContainsKey($key)){$byActor[$key]=$id}
        elseif($byActor[$key] -ne $id){$byActor[$key]=$null}
    }
    foreach($event in $health){
        $playerId=''
        $playerType=Get-CombatHealthSubjectType $event
        $subject=switch($playerType){
            'Player' {
                $playerId=Get-CombatHealthPlayerId $event
                if(!$playerId){
                    $key=Get-CombatActorEvidenceKey $event
                    if($key){$playerId=$byActor[$key]}
                }
                if(!$playerId){'플레이어 (ID 미확인)';break}
                $source=$ClientByPlayerId[$playerId]
                if($source -and $SourceLabelById[$source] -match '^클라이언트 ([1-9]\d*)$'){
                    "플레이어 $($Matches[1]) ($playerId)"
                }else{
                    "플레이어 (ID $playerId)"
                }
            }
            'Enemy' {'적'}
            'TrainingTarget' {'고정 표적'}
            default {'대상 미확인'}
        }
        $event.HealthSubject=$subject
        $event.Detail="$subject · $($event.Fields['Before']) → $($event.Fields['After'])"
        $event.Summary="$($event.Label) · $($event.Detail)"
        if($event.Level -in @('Warning','Error','Fatal')){$event.Summary="[$($event.Level)] $($event.Summary)"}
        $event.Text="[$($event.Time)] $($event.Summary)"
    }
}

function Select-CombatEvents($Events,[string]$SourceId='',[string]$Outcome='전체',[string]$Search='',[switch]$CollectionOrder,[string[]]$Topics){
    $selected=@(foreach($e in $Events){
        if($SourceId -and $e.SourceId -ne $SourceId){continue}
        if ($PSBoundParameters.ContainsKey('Topics') -and $e.Topic -notin $Topics) {
            $includeSeverity = '오류·경고' -in $Topics -and ($e.Outcome -in @('오류','경고') -or $e.Level -in @('Warning','Error','Fatal'))
            if (!$includeSeverity) { continue }
        }
        if($Outcome -eq '오류·경고'){if($e.Outcome -notin @('오류','경고') -and $e.Level -notin @('Warning','Error','Fatal')){continue}}
        elseif($Outcome -ne '전체' -and $e.Outcome -ne $Outcome){continue}
        if($Search -and ($e.Raw+' '+$e.Summary+' '+$e.SourceLabel+' '+$e.EventId).IndexOf($Search,[StringComparison]::OrdinalIgnoreCase) -lt 0){continue}
        $e
    })
    if($CollectionOrder){$selected | Sort-Object CollectionOrder}
    else{$selected | Sort-Object SortTicks,SourceId,FileGeneration,ByteOffset,CollectionOrder}
}

Export-ModuleMember -Function New-CombatLogReader,Read-CombatLogRecords,Read-CombatLogLines,ConvertTo-CombatLogEvent,Add-CombatEventContext,Update-CombatHealthLabels,Select-CombatEvents
