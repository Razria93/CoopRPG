param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'CombatLogReader.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'CombatViewerLayout.psm1') -Force
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a new output directory to preserve prior results.'}
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$checks=New-Object Collections.Generic.List[object]
function Check([string]$Name,[bool]$Passed){$checks.Add([pscustomobject]@{name=$Name;passed=$Passed});if(!$Passed){throw "FAIL: $Name"}}
$completed=$false
$utf8=[Text.UTF8Encoding]::new($false)
try{
    foreach($file in @('CombatLogReader.psm1','CombatViewerLayout.psm1','Watch-CombatManual.ps1','Start-CombatManual.ps1')){
        $errors=$null;$tokens=$null;[void][Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $file),[ref]$tokens,[ref]$errors)
        Check "parse $file" ($errors.Count -eq 0)
    }
    $path=Join-Path $OutputDirectory 'reader.log';$r=New-CombatLogReader $path
    Check 'missing file waits without claiming an event' (@(Read-CombatLogRecords $r).Count -eq 0 -and $r.State -eq 'Waiting')
    [IO.File]::WriteAllBytes($path,[byte[]]@())
    Check 'empty file readable' (@(Read-CombatLogRecords $r).Count -eq 0 -and $r.State -eq 'Ready')
    $bytes=$utf8.GetBytes('한글 첫 줄'+[Environment]::NewLine)
    [IO.File]::WriteAllBytes($path,$bytes[0..1])
    Check 'incomplete UTF8 not emitted' (@(Read-CombatLogRecords $r).Count -eq 0)
    $writer=[IO.File]::Open($path,[IO.FileMode]::Append,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite)
    try{$writer.Write($bytes,2,$bytes.Length-2);$writer.Flush();$record=@(Read-CombatLogRecords $r)}finally{$writer.Dispose()}
    Check 'complete UTF8 with open writer' ($record.Count -eq 1 -and $record[0].Line -eq '한글 첫 줄' -and $record[0].ByteOffset -eq 0)
    Check 'repeated poll does not duplicate' (@(Read-CombatLogRecords $r).Count -eq 0)
    [IO.File]::WriteAllText($path,"new line"+[Environment]::NewLine,$utf8)
    $reset=@(Read-CombatLogRecords $r)
    Check 'replacement/truncation produces distinct file generation' ($reset.Count -eq 1 -and $reset[0].FileGeneration -eq 1)
    $lock=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try{$caught=$false;try{Read-CombatLogRecords $r | Out-Null}catch{$caught=$true};Check 'exclusive lock is a read error' ($caught -and $r.State -eq 'Error')}finally{$lock.Dispose()}
    [void]@(Read-CombatLogRecords $r);Check 'read error recovers' ($r.State -eq 'Ready')
    $large=Join-Path $OutputDirectory 'large.log';[IO.File]::WriteAllText($large,('padding row'+[Environment]::NewLine)*20000,$utf8)
    $tail=New-CombatLogReader $large;$rows=@(Read-CombatLogRecords $tail)
    Check 'initial tail discloses omitted bytes' ($tail.InitialSkippedBytes -gt 0 -and $rows.Count -gt 0 -and $rows[0].Line -eq 'padding row')
    $prefix='[2026.11.15-09.00.00:123][  1]'
    $a=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGMelee: AttackRejected Attacker=PawnA Target=TargetA Reason=OutOfRange Distance=351.75 Range=250.00 HP=100.00 Stamina=9.00')
    Check 'range reason retains observed and required values' ($a.Detail -like '*351.75cm*250.00cm*' -and $a.Actor -eq 'PawnA' -and $a.Target -eq 'TargetA')
    Check 'expected rejection is not runtime error' ($a.Outcome -eq '거절' -and $a.Level -eq 'Log')
    Check 'date preserved as UTC' ($a.UtcTime.ToString('o') -eq '2026-11-15T09:00:00.1230000Z')
    $cost=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGMelee: AttackBlocked Attacker=PawnA Reason=Cost Stamina=9.00 Cooldown=0')
    Check 'local failure is distinct and does not invent required cost' ($cost.Stage -eq '로컬 검사' -and $cost.Detail -like '*Stamina=9.00*' -and $cost.Detail -notlike '*10*')
    $commit=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGMelee: AttackRejected Attacker=PawnA Reason=CommitFailed')
    Check 'unknown commit cause stays unknown' ($commit.Detail -like '*세부 원인 미확인*')
    $call = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGInput: Verbose: CancelRPCCalled Actor=PawnA LocalPlayerId=256')
    $receipt = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGInput: Verbose: CancelRPCReceived Actor=PawnS RequesterPlayerId=256')
    $commit = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGMelee: AttackStarted Attacker=PawnS Target=Dummy Windup=0.65 Stamina=100.00->90.00')
    $hit = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGMelee: AttackApproved Attacker=PawnS Target=Dummy HP=100.00->90.00')
    $legacyHit = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGMelee: AttackApproved Attacker=PawnS Target=Dummy HP=100.00->90.00 Stamina=100.00->90.00')
    Check 'cancel call and receipt appear as request stages with player identity' ($call.Stage -eq 'RPC 호출' -and $receipt.Stage -eq 'RPC 수신' -and $call.Topic -eq '입력·요청' -and $receipt.Topic -eq '입력·요청' -and $receipt.Fields['RequesterPlayerId'] -eq '256' -and $call.Fields['LocalPlayerId'] -eq '256')
    Check 'server commit displays stamina separately' ($commit.Summary -like '비용 확정 · 스태미나 100.00 → 90.00' -and $commit.Summary -notlike '*HP*')
    Check 'hit and legacy replay show HP while preserving legacy cost fields and raw log' ($hit.Summary -eq $legacyHit.Summary -and $hit.Summary -eq '타격 확정 · 표적 HP 100.00 → 90.00' -and $legacyHit.Fields['Stamina'] -eq '100.00->90.00' -and $legacyHit.Raw -like '*Stamina=100.00->90.00')
    $readyCall = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRoundReady: LootReadyRequested Round=R1 LocalPlayerId=256')
    $openCall = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGChestRequest: ChestViewRequested Action=Open Chest=Box Round=R1')
    $closeCall = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGChestRequest: ChestViewRequested Action=Close Chest=Box Round=R1')
    $closed = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGChestRequest: ChestClosed Chest=Box Round=R1 RequesterPlayerId=256')
    $automaticClose = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGChestRequest: ChestClosed Chest=Box Round=R1 Reason=OutOfRange RequesterPlayerId=256')
    $legacyOpen = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGChestRequest: ChestOpened Chest=Box Round=R1 Stock=1')
    $open = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGChestRequest: ChestOpened Chest=Box Round=R1 Stock=1 WeaponStock=2')
    $openView = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGChestRequest: ChestViewReplicated Chest=Box Open=1 Stock=0 WeaponStock=1')
    Check 'out-of-range closure keeps its server reason and requester' ($automaticClose.Summary -eq '상자 자동 닫기 · 거리 이탈' -and $automaticClose.ReasonCode -eq 'OutOfRange' -and $automaticClose.Fields['RequesterPlayerId'] -eq '256')
    Check 'server opening displays both stocks when present and preserves older logs' ($open.Detail -eq '근접무기 2개 · 회복약 1개' -and $legacyOpen.Detail -eq '회복약 1개')
    Check 'reopened client view displays updated weapons and zero recovery items' ($openView.Detail -eq '근접무기 1개 · 회복약 0개')
    $closeIgnored = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGChestRequest: ChestRejected Action=Close Reason=NotOpen Chest=Box Round=R1 Request=0')
    $closeRejected = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGChestRequest: ChestRejected Action=Close Reason=WrongRound Chest=Box Round=OLD Request=0')
    $oldCloseView = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGChestRequest: ChestViewReplicated Chest=None Open=0 Stock=0')
    $closeView = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGChestRequest: ChestViewReplicated Chest=None Open=0')
    Check 'chest lifecycle calls are separate client RPC entries' ($readyCall.Label -eq '준비 RPC 호출' -and $openCall.Label -eq '상자 열기 RPC 호출' -and $closeCall.Label -eq '상자 닫기 RPC 호출' -and @($readyCall,$openCall,$closeCall | Where-Object Topic -eq '입력·요청').Count -eq 3)
    Check 'server close outcome identifies requester and separates ignored and rejected' ($closed.Outcome -eq '승인' -and $closed.Fields['RequesterPlayerId'] -eq '256' -and $closeIgnored.Label -eq '상자 닫기 무시' -and $closeIgnored.Outcome -eq '관찰' -and $closeRejected.Label -eq '상자 닫기 거절' -and $closeRejected.Outcome -eq '거절')
    Check 'both versions show closed view without claiming stock' ($closeView.Summary -eq '내 상자 닫기' -and $oldCloseView.Summary -eq $closeView.Summary -and $oldCloseView.Raw.Contains('Stock=0'))
    $unarmed = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGMelee: AttackRejected Reason=Unarmed Stamina=100.00 Cooldown=0')
    $weapon = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGChestRequest: ChestWeaponGranted Request=2 Stock=2->1 Equipped=1 RequesterPlayerId=256')
    $equipped = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGEquipment: MeleeWeaponReplicated Actor=PawnA Equipped=1 EquippedPlayerId=256 LocalRole=SimulatedProxy')
    $full = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGChestRequest: RecoveryUseRejected Reason=FullHealth Recovery=1')
    $empty = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGChestRequest: RecoveryUseRejected Reason=NoRecovery Recovery=0')
    $healed = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGChestRequest: RecoveryUsed HP=80.00->100.00 Recovery=1->0')
    Check 'weapon prerequisite and grant use server outcomes, actual equipment uses replication' ($unarmed.Detail -like '*무기 미장착*' -and $weapon.Outcome -eq '승인' -and $weapon.Detail -like '*2 → 1*' -and $equipped.Topic -eq '복제' -and $equipped.Detail -like '*256*')
    Check 'healing success and rejection show exact HP, item count, and reason' ($full.Summary -like '*최대 HP*1개' -and $empty.Summary -like '*회복약 없음*0개' -and $healed.Outcome -eq '승인' -and $healed.Detail -eq 'HP 80.00 → 100.00 · 보유량 1 → 0')
    # Taken from the final double-ring package's Visual01 server and client logs.
    $shieldBlockRaw='[2026.11.20-04.44.59:961][539]LogCoopRPGMelee: ShieldMeleeEvaluated Angle=0.0000 Raised=1 Outcome=Blocked NetMode=DedicatedServer PIE=-1 Func=UCoopRPGMeleeAbility::ResolveHit LocalRole=Authority RequesterPlayerId=256 Swing=1'
    $shieldPassRaw='[2026.11.20-04.45.07:195][827]LogCoopRPGMelee: ShieldMeleeEvaluated Angle=102.3520 Raised=1 Outcome=Hit NetMode=DedicatedServer PIE=-1 Func=UCoopRPGMeleeAbility::ResolveHit LocalRole=Authority RequesterPlayerId=257 Swing=1'
    $shieldStartRaw='[2026.11.20-04.45.11:052][980]LogCoopRPGEnemy: ShieldWarning Active=1 Pattern=7 TargetSlot=2 Yaw=101.72 EndsAt=51.613 X=664.87 Y=-45.52'
    $shieldEndRaw='[2026.11.20-04.45.12:553][ 40]LogCoopRPGEnemy: ShieldWarning Active=0 Pattern=7 TargetSlot=0 Yaw=101.72 EndsAt=0.000 X=664.87 Y=-45.52'
    $shieldCopyStartRaw='[2026.11.20-04.45.11:102][859]LogCoopRPGEnemy: ShieldWarningReplicated Active=1 Pattern=7 TargetSlot=2 Yaw=101.72 EndsAt=51.613 Visible=1 RingVisible=1'
    $shieldCopyEndRaw='[2026.11.20-04.45.12:588][236]LogCoopRPGEnemy: ShieldWarningReplicated Active=0 Pattern=7 TargetSlot=0 Yaw=101.72 EndsAt=0.000 Visible=0 RingVisible=1'
    $shieldBlock=ConvertTo-CombatLogEvent $shieldBlockRaw
    $shieldPass=ConvertTo-CombatLogEvent $shieldPassRaw
    $shieldStart=ConvertTo-CombatLogEvent $shieldStartRaw
    $shieldEnd=ConvertTo-CombatLogEvent $shieldEndRaw
    $shieldCopyStart=ConvertTo-CombatLogEvent $shieldCopyStartRaw
    $shieldCopyEnd=ConvertTo-CombatLogEvent $shieldCopyEndRaw
    Check 'shield block is a server combat outcome with the raw requester and swing' (
        $shieldBlock.Topic -eq '서버 판정' -and
        $shieldBlock.Outcome -eq '차단' -and
        $shieldBlock.Level -eq 'Log' -and
        $shieldBlock.Fields['RequesterPlayerId'] -eq '256' -and
        $shieldBlock.Fields['Swing'] -eq '1' -and
        $shieldBlock.Raw -ceq $shieldBlockRaw)
    Check 'shield pass is an observation before damage approval' (
        $shieldPass.Outcome -eq '관찰' -and
        $shieldPass.Label -eq '방패 판정 통과' -and
        $shieldPass.Detail -eq '정면 각도 102.3520° · 방패 활성' -and
        $shieldPass.Raw -ceq $shieldPassRaw)
    Check 'server starts and clears a shield warning without inventing player IDs' (
        $shieldStart.Topic -eq '서버 판정' -and
        $shieldStart.Summary -like '방패 예고 시작*패턴 7*대상 P2*' -and
        !$shieldStart.Fields.ContainsKey('TargetPlayerId') -and
        $shieldEnd.Summary -like '방패 예고 종료*대상 해제*' -and
        $shieldEnd.Raw -ceq $shieldEndRaw)
    Check 'clients observe shield activation and clearing as replication' (
        $shieldCopyStart.Topic -eq '복제' -and
        $shieldCopyStart.Summary -like '방패 예고 시작 복제*대상 P2*예고 표시 중' -and
        $shieldCopyEnd.Summary -like '방패 예고 종료 복제*대상 해제*예고 표시 없음' -and
        $shieldCopyEnd.Raw -ceq $shieldCopyEndRaw)
    $shieldLate=ConvertTo-CombatLogEvent ($shieldCopyStartRaw.Replace('Visible=1 RingVisible=1','Visible=0 RingVisible=1'))
    $shieldIncomplete=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGEnemy: ShieldWarningReplicated Pattern=9')
    Check 'late or incomplete replication does not invent a visible warning or target' (
        $shieldLate.Summary -like '방패 예고 시작 복제*예고 표시 없음' -and
        $shieldIncomplete.Summary -eq '방패 예고 상태 미확인 복제 · 패턴 9')
    Check 'shield blocks can be filtered independently of server rejections and damage' (@(Select-CombatEvents @($shieldBlock,$shieldPass,$a,$hit) -Outcome '차단').Count -eq 1)
    $enemyHit = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGEnemy: EnemyAttackHit Actor=Enemy Target=PawnA TargetPlayerId=256 HP=100.00->90.00')
    $enemyMiss = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGEnemy: EnemyAttackMissed Actor=Enemy Target=PawnA TargetPlayerId=256 Reason=OutOfRange Distance=200.00 Range=135.00')
    $enemyMove = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGEnemy: EnemyMovementObserved Actor=Enemy Distance=45.00')
    Check 'enemy hit identifies victim without implying an RPC requester' ($enemyHit.Outcome -eq '승인' -and $enemyHit.Detail -like '*256*100.00 → 90.00*' -and !$enemyHit.Fields.ContainsKey('RequesterPlayerId'))
    Check 'enemy escape and movement replication retain their distinct stages' ($enemyMiss.Outcome -eq '빗나감' -and $enemyMiss.Detail -like '*사거리 밖*200.00*135.00*' -and $enemyMove.Topic -eq '복제')
    function New-HealthFixture([string]$Text,[string]$Source,[int]$World=1,[int]$Generation=0){
        $evt=ConvertTo-CombatLogEvent ($prefix+$Text)
        Add-CombatEventContext $evt 'health-test' $Source $Source $path ([pscustomobject]@{ByteOffset=11;FileGeneration=$Generation;WorldSequence=$World}) 11
    }
    $replication='LogCoopRPGCombatAttributes: HealthReplicated Actor=CoopRPGCharacter_12 Before=100.00 After=90.00 NetMode=Client LocalRole=SimulatedProxy'
    $equipped='LogCoopRPGEquipment: MeleeWeaponReplicated Actor=CoopRPGCharacter_12 Equipped=1 NetMode=Client EquippedPlayerId='
    $direct=New-HealthFixture ($replication+' SubjectType=Player SubjectPlayerId=257 LocalPlayerId=256') a
    $enemy=New-HealthFixture 'LogCoopRPGCombatAttributes: HealthReplicated Actor=CoopRPGEnemy_1 Before=100.00 After=90.00 NetMode=Client LocalRole=SimulatedProxy' a
    $legacy=New-HealthFixture $replication b
    $otherSource=New-HealthFixture $replication c
    $otherWorld=New-HealthFixture $replication b 2
    $otherFile=New-HealthFixture $replication b 1 1
    $unknownWorld=New-HealthFixture $replication b 0
    $weaponEvidence=New-HealthFixture ($equipped+'256') b
    $rejoined=Add-CombatEventContext (ConvertTo-CombatLogEvent ($prefix+$replication)) 'health-test' 'b' 'b' "$path-rejoined" `
        ([pscustomobject]@{ByteOffset=11;FileGeneration=0;WorldSequence=1}) 12
    $clients=@{'256'='a';'257'='b'}
    $labels=@{a='클라이언트 1';b='클라이언트 2'}
    $events=@($direct,$enemy,$legacy,$otherSource,$otherWorld,$otherFile,$unknownWorld,$weaponEvidence,$rejoined)
    Update-CombatHealthLabels $events $clients $labels
    Check 'explicit subject identifies remote player, and legacy enemy type is recognized' ($direct.HealthSubject -eq '플레이어 2 (257)' -and $enemy.Summary -eq '체력 복제 · 적 · 100.00 → 90.00')
    Check 'legacy actor evidence is scoped by source, world, and file, with no cross-scope inference' ($legacy.HealthSubject -eq '플레이어 1 (256)' -and @($otherSource,$otherWorld,$otherFile,$unknownWorld | Where-Object HealthSubject -eq '플레이어 (ID 미확인)').Count -eq 4)
    Check 'reconnect files isolate actor evidence and event IDs for the same source and offset' ($rejoined.HealthSubject -eq '플레이어 (ID 미확인)' -and $rejoined.EventId -ne $legacy.EventId)
    Check 'search uses the same subject label and preserves the raw line' (@(Select-CombatEvents $events -Search '플레이어 1 (256)').Count -eq 1 -and $legacy.Raw.EndsWith($replication))
    Update-CombatHealthLabels ($events+@(New-HealthFixture ($equipped+'257') b)) $clients $labels
    Check 'conflicting older evidence drops the inferred ID' ($legacy.HealthSubject -eq '플레이어 (ID 미확인)' -and $direct.HealthSubject -eq '플레이어 2 (257)')
    $scopePath=Join-Path $OutputDirectory 'world-scope.log'
    [IO.File]::WriteAllLines($scopePath,@('before world','LogWorld: Bringing World /Game/One up for play',
        'first world','LogWorld: BeginTearingDown for /Game/One','in transit',
        'LogWorld: Bringing World /Game/Two up for play','second world'))
    $scopeRows=@(Read-CombatLogRecords (New-CombatLogReader $scopePath))
    Check 'legacy world scope opens at map play and stops at teardown' (($scopeRows.WorldSequence -join ',') -eq '0,1,1,0,0,2,2')
    $death = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGEnemy: EnemyDeathConfirmed Actor=Enemy HP=10.00->0.00 PendingAttackDiscarded=1')
    $loot = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGEnemy: EnemyLootSpawned Actor=Drop DropId=ABC')
    $seenLoot = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGEnemyLoot: EnemyLootReplicated Actor=Drop DropId=ABC')
    Check 'enemy death shows the lethal transition and discarded attack' ($death.Stage -eq '서버 판정' -and $death.Detail -eq 'HP 10.00 → 0.00 · 대기 공격 취소')
    Check 'loot generation and observed replication keep the same drop ID' ($loot.Outcome -eq '승인' -and $seenLoot.Topic -eq '복제' -and $loot.Detail -eq 'ID ABC' -and $seenLoot.Detail -eq $loot.Detail)
    $lootRequest=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGEnemyLootRequest: EnemyLootTakeRequested Round=DEF DropId=ABC')
    $lootGrant=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGEnemyLootRequest: EnemyLootGranted Round=DEF DropId=ABC TeamLoot=0->1 RequesterPlayerId=256')
    $lootReject=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGEnemyLootRequest: EnemyLootRejected Round=DEF DropId=ABC Reason=AlreadyClaimed TeamLoot=1')
    $lootClaim=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGEnemyLoot: EnemyLootClaimReplicated DropId=ABC Claimed=1')
    $teamLoot=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRound: TeamLootCountReplicated Before=0 After=1')
    Check 'enemy loot request and grant keep the DropId and server requester' ($lootRequest.Stage -eq 'RPC 호출' -and $lootGrant.Fields['RequesterPlayerId'] -eq '256' -and $lootGrant.Detail -eq '팀 수량 0 → 1 · ID ABC')
    Check 'duplicate enemy loot request has its own reason and unchanged quantity' ($lootReject.Outcome -eq '거절' -and $lootReject.ReasonCode -eq 'AlreadyClaimed' -and $lootReject.Detail -match '이미 지급됨 · 팀 수량 1')
    Check 'claim and quantity replication are distinct observations' ($lootClaim.Topic -eq '복제' -and $teamLoot.Topic -eq '복제' -and $lootClaim.Fields['DropId'] -eq 'ABC' -and $teamLoot.Detail -eq '0 → 1')
    $extractionStarted=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRound: ExtractionStarted Round=DEF Time=7.0 Deadline=10.0')
    $extractionReset=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRound: ExtractionReset Round=DEF Time=8.0 Elapsed=1.000')
    $extractionResult=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRound: ExtractionResult Round=DEF Outcome=Success Score=1 Time=12.0 Held=3.000')
    $extractionRep=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRound: ExtractionResultReplicated Round=DEF Outcome=Success Score=1')
    $readySlots=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRound: RoundReadySlotsReplicated Round=DEF P1=Ready P2=Waiting NetMode=Client PIE=-1')
    Check 'replicated ready slots identify both player statuses' ($readySlots.Stage -eq '복제 관찰' -and $readySlots.Summary -eq '팀 준비 상태 복제 · P1 준비 완료 · P2 준비 전')
    $readyMissing=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRound: RoundReadySlotsReplicated Round=DEF P1=Disconnected NetMode=Client PIE=-1')
    Check 'replicated ready slots distinguish missing fields from disconnected' ($readyMissing.Summary -eq '팀 준비 상태 복제 · P1 미접속 · P2 미계측')
    $extractionResetRep=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRound: ExtractionProgressReplicated Round=DEF Phase=Reset Deadline=0.000')
    Check 'extraction start and reset show the server progress outcome' ($extractionStarted.Stage -eq '서버 판정' -and $extractionReset.Detail -eq '경과 1.000초 · 진행 0')
    Check 'one-point extraction result distinguishes authority from replication' ($extractionResult.Outcome -eq '승인' -and $extractionRep.Topic -eq '복제' -and $extractionResult.Detail -eq '팀 점수 1점' -and $extractionRep.Detail -eq $extractionResult.Detail)
    Check 'client reset is a replication observation' ($extractionResetRep.Topic -eq '복제' -and $extractionResetRep.Label -eq '탈출 진행 초기화 복제')
    $joinDenied=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRoundAdmission: RoundJoinRejected Stage=PreLogin Reason=RoundInProgress')
    $joinNotice=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGConnectionNotice: ConnectionNoticeDisplayed Reason=RoundFull')
    $seatEntered=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRoundTravelSeats: RoundTravelSeatJoined Map=L_Movement_Round2 Remaining=1')
    Check 'admission denial and local notice preserve reason and source meaning' ($joinDenied.Outcome -eq '거절' -and $joinDenied.ReasonCode -eq 'RoundInProgress' -and $joinNotice.Detail -eq '정원 초과')
    Check 'travel seat countdown is visible' ($seatEntered.Detail -eq 'L_Movement_Round2 · 남은 예약 1석')
    $resultDisplayed=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRoundResultDisplay: RoundResultDisplayed Round=DEF NetMode=Client PIE=-1')
    $resultAck=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRoundTravel: RoundResultAcknowledged Round=DEF PlayerId=256 Count=2')
    $travel=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRoundTravel: RoundTravelStarted Round=DEF TargetMap=/Game/Maps/L_Movement_Round2')
    $travelFailed=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRoundTravel: Error: RoundTravelFailed Round=DEF TargetMap=/Game/Maps/L_Movement_Round2')
    Check 'round display and receipt distinguish observation from receipt' ($resultDisplayed.Label -eq '라운드 결과 HUD 표시' -and $resultAck.Stage -eq 'RPC 수신' -and $resultAck.Detail -eq 'PlayerId 256 · 2/2')
    Check 'travel start retains target and failure retains error' ($travel.Label -eq '다음 맵 이동 시작' -and $travel.Detail -eq '/Game/Maps/L_Movement_Round2' -and $travelFailed.Outcome -eq '오류')
    $roundClock=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRound: RoundClockStarted Round=DEF Time=8.0 Deadline=308.0 Duration=300')
    $roundFailure=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRound: RoundResult Round=DEF Outcome=Failed Score=0 Reason=Death Frame=25 Disconnect=0 Death=1 Timeout=1 Extraction=0')
    $roundAborted=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGRound: RoundResultReplicated Round=DEF Outcome=Aborted Score=0 Reason=Disconnected')
    Check 'round clock reports the configured duration' ($roundClock.Detail -eq '300초')
    Check 'failure and its arbitration candidates remain visible and filterable' ($roundFailure.Stage -eq '서버 판정' -and $roundFailure.Detail -eq '플레이어 사망 · 팀 점수 0점' -and $roundFailure.Fields['Timeout'] -eq '1' -and @(Select-CombatEvents @($roundFailure,$roundAborted) -Outcome '실패').Count -eq 1)
    Check 'abort is a replicated outcome with its own reason and filter' ($roundAborted.Stage -eq '복제 관찰' -and $roundAborted.Detail -eq '플레이어 접속 종료 · 팀 점수 0점' -and @(Select-CombatEvents @($roundFailure,$roundAborted) -Outcome '중단').Count -eq 1)
    $remaining=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGCombatProbe: CombatSnapshot Label=Prepared Actor=PawnA HP=100 Stamina=9 Cooldown=1 Remaining=1.250')
    Check 'recorded cooldown remaining retained' ($remaining.Detail -like '*1.250s*')
    $unknown=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGInventory: NewLootEvent Item=Potion Amount=1')
    Check 'new project event retained as unclassified' ($unknown.Outcome -eq '미분류' -and $unknown.Fields['Item'] -eq 'Potion')
    $runtimeError=ConvertTo-CombatLogEvent ($prefix+'LogNet: Error: Deliberate fixture error')
    $warning=ConvertTo-CombatLogEvent ($prefix+'LogNet: Warning: Deliberate fixture warning')
    Check 'engine errors and warnings retained' ($runtimeError.Outcome -eq '오류' -and $warning.Outcome -eq '경고')
    $knownError=ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGMelee: Error: AttackRejected Actor=PawnA Reason=Cost Stamina=9')
    $knownError=Add-CombatEventContext $knownError 'run1' 'server' '서버' $path ([pscustomobject]@{ByteOffset=99;FileGeneration=0}) 99
    $severityFiltered=@(Select-CombatEvents @($knownError) -Outcome '오류·경고')
    Check 'known event retains severity in summary and error filter' ($knownError.Outcome -eq '거절' -and $knownError.Summary.StartsWith('[Error]') -and $severityFiltered.Count -eq 1)
    Check 'ordinary engine noise filtered' ($null -eq (ConvertTo-CombatLogEvent ($prefix+'LogInit: Engine initialization')))
    $none=ConvertTo-CombatLogEvent 'LogCoopRPGInput: AttackInput Actor=PawnA'
    Check 'missing timestamp is explicit' ($null -eq $none.UtcTime -and $none.Time -eq '시각 미기록')
    $e1=Add-CombatEventContext $a 'run1' 'a' '클라이언트 1' $path ([pscustomobject]@{ByteOffset=10;FileGeneration=0}) 2
    $e2=Add-CombatEventContext $unknown 'run1' 'b' '클라이언트 2' $path ([pscustomobject]@{ByteOffset=10;FileGeneration=0}) 1
    Check 'source-local actor is not used for event identity' ($e1.EventId -ne $e2.EventId -and $null -eq $e1.RequestId)
    $before=ConvertTo-CombatLogEvent '[2026.11.14-23.59.59:999][1]LogCoopRPGInput: AttackInput Actor=PawnA'
    $e3=Add-CombatEventContext $before 'run1' 'a' '클라이언트 1' $path ([pscustomobject]@{ByteOffset=1;FileGeneration=0}) 3
    $all=@($e1,$e2,$e3)
    $sorted=@(Select-CombatEvents $all)
    Check 'late-arriving prior-day event sorted by full date' ($sorted[0].EventId -eq $e3.EventId)
    Check 'equal timestamps stable by source and offset' ($sorted[1].SourceId -eq 'a' -and $sorted[2].SourceId -eq 'b')
    $arrived=@(Select-CombatEvents $all -CollectionOrder)
    Check 'collection order explicitly separate' ($arrived[0].EventId -eq $e2.EventId -and $arrived[2].EventId -eq $e3.EventId)
    $filtered=@(Select-CombatEvents $all -SourceId 'a' -Outcome '거절' -Search '351.75')
    Check 'tab/result/search operate on same events' ($filtered.Count -eq 1 -and [object]::ReferenceEquals($filtered[0],$e1))
    Check 'network handshake labels and codes describe distinct observations' $(
        $login = ConvertTo-CombatLogEvent ($prefix+'LogNet: Login request: fixture')
        $welcome = ConvertTo-CombatLogEvent ($prefix+'LogNet: Welcomed by server (Level: /Game/Fixture)')
        $join = ConvertTo-CombatLogEvent ($prefix+'LogNet: Join request: /Game/Fixture')
        $joined = ConvertTo-CombatLogEvent ($prefix+'LogNet: Join succeeded: Fixture')
        @($login.EventCode,$welcome.EventCode,$join.EventCode,$joined.EventCode) -join ',' -eq 'LoginRequestReceived,WelcomeReceived,JoinRequestReceived,JoinSucceeded' -and $welcome.Label -ne $joined.Label
    )
    $misleading = ConvertTo-CombatLogEvent ($prefix+'LogOther: Message Join succeeded: fixture')
    $warningJoin = ConvertTo-CombatLogEvent ($prefix+'LogNet: Warning: Join request: fixture')
    Check 'connection labels require LogNet message prefix and preserve warning priority' ($null -eq $misleading -and $warningJoin.Outcome -eq '경고')
    $filtered = @(Select-CombatEvents $all -Topics @('입력·요청','서버 판정') -SourceId 'a' -Outcome '거절' -Search '351.75')
    Check 'topic union combines with source outcome and search' ($filtered.Count -eq 1 -and $filtered[0].EventId -eq $e1.EventId)
    Check 'empty topics returns no rows' (@(Select-CombatEvents $all -Topics @()).Count -eq 0)
    $severitySelection = @(Select-CombatEvents @($e1,$knownError) -Topics @('오류·경고'))
    Check 'severity topic includes warnings and errors on categorized events' ($severitySelection.Count -eq 1 -and $severitySelection[0].EventId -eq $knownError.EventId)
    $gasRaw = $prefix+'LogAbilitySystem: Display: ::CallServerTryActivateAbility 1 0 [0/0]'
    $rpc = ConvertTo-CombatLogEvent $gasRaw
    Check 'engine RPC helper entry is a distinct call with preserved payload' ($rpc.EventCode -eq 'GasServerRpcCall' -and $rpc.Stage -eq 'RPC 호출' -and $rpc.Outcome -eq '호출' -and $rpc.Topic -eq '입력·요청' -and $rpc.Raw -eq $gasRaw -and $rpc.Fields['SpecHandle'] -eq '1' -and $rpc.Fields['InputPressed'] -eq '0' -and $rpc.Fields['PredictionKey'] -eq '[0/0]' -and $null -eq $rpc.RequestId)
    $returnTrue = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGInput: Verbose: AttackDispatch Actor=PawnA Submitted=1')
    $returnFalse = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGInput: Verbose: AttackDispatch Actor=PawnA Submitted=0')
    Check 'activation result keeps true and false distinct from RPC call and server verdict' ($returnTrue.Stage -eq '함수 반환' -and $returnFalse.Stage -eq '함수 반환' -and $returnTrue.Label.EndsWith('true') -and $returnFalse.Label.EndsWith('false') -and $returnTrue.Outcome -eq '제출' -and $returnFalse.Outcome -eq '미제출')
    $noReturn = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGInput: AttackDispatch Actor=PawnA')
    Check 'missing result is unknown' ($noReturn.Outcome -eq '미분류')
    $noBatch = ConvertTo-CombatLogEvent ($prefix+'LogAbilitySystem: Display:     NO BATCH IN SCOPE')
    $otherCategory = ConvertTo-CombatLogEvent ($prefix+'LogOther: Display: ::CallServerTryActivateAbility 1 0 [0/0]')
    $duplicateWarning = ConvertTo-CombatLogEvent ($prefix+'LogAbilitySystem: Warning: ::CallServerTryActivateAbility called multiple times for ability in a single batch')
    Check 'batch chatter ignored and GAS duplicate warning kept as warning' ($null -eq $noBatch -and $null -eq $otherCategory -and $duplicateWarning.Outcome -eq '경고')
    $selectedCalls = @(Select-CombatEvents @($rpc,$returnTrue,$returnFalse) -Topics @('입력·요청') -Outcome '호출')
    Check 'request topic and call filter selects only engine call event' ($selectedCalls.Count -eq 1 -and $selectedCalls[0].EventCode -eq 'GasServerRpcCall')
    $flush = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGManualInput: ManualInputFlushed Pawn=PawnA HeldWBefore=1 W=0')
    $focusClick = ConvertTo-CombatLogEvent ($prefix+'LogCoopRPGManualInput: ManualFocusClickConsumed Pawn=PawnA')
    $attackTopics = @('입력·요청','서버 판정','복제')
    $focusEvents = @($flush,$focusClick,$rpc)
    $attackView = @(Select-CombatEvents $focusEvents -Topics $attackTopics)
    $windowView = @(Select-CombatEvents $focusEvents -Topics @('상태·기타'))
    Check 'attack preset omits key reset but retains focus clicks and GAS calls' ($attackView.Count -eq 2 -and 'ManualInputFlushed' -notin $attackView.EventCode -and 'ManualFocusClickConsumed' -in $attackView.EventCode -and 'GasServerRpcCall' -in $attackView.EventCode)
    Check 'window key reset remains searchable with measured input state' ($windowView.Count -eq 1 -and $windowView[0].EventCode -eq 'ManualInputFlushed' -and $windowView[0].Detail -eq 'W 1 → 0' -and $windowView[0].Raw.Contains('HeldWBefore=1 W=0'))
    $m1=[pscustomobject]@{Index=1;DeviceName='Left';Primary=$true;Left=-1920;Top=0;Right=0;Bottom=1040;Width=1920;Height=1040}
    $m2=[pscustomobject]@{Index=2;DeviceName='Right';Primary=$false;Left=0;Top=0;Right=2560;Bottom=1392;Width=2560;Height=1392}
    $dual=Resolve-CombatLayout @($m1,$m2) -GameMonitor 2 -LogMonitor 1
    Check 'dual monitor explicit selection and maximized viewer' ($dual.Game.DeviceName -eq 'Right' -and $dual.Viewer.Left -eq -1920 -and $dual.Viewer.Maximized)
    Check 'game windows fit and do not overlap' ($dual.A.Left+$dual.A.Width -lt $dual.B.Left -and $dual.B.Left+$dual.B.Width -le $dual.Game.Right)
    $single=Resolve-CombatLayout @($m1) -GameMonitor 'Removed' -LogMonitor 'Removed'
    Check 'missing saved monitor falls back onto available screen' (!$single.Separate -and $single.Notes.Count -eq 2 -and $single.Viewer.Left -ge $m1.Left)
    Check 'single monitor game and log do not overlap' ($single.A.Top+$single.A.Height -lt $single.Viewer.Top -and $single.Viewer.Top+$single.Viewer.Height -le $m1.Bottom)
    $stack=Resolve-CombatLayout @($m1,$m2) -GameMonitor 2 -LogMonitor 1 -Layout Stacked
    Check 'stacked layout remains available' ($stack.A.Top+$stack.A.Height -lt $stack.B.Top)
    $nogui=Resolve-CombatLayout @($m1) -NoLogViewer
    Check 'no-viewer option reserves no log height' ($nogui.Viewer.Height -eq 0)
    $settings=Join-Path $OutputDirectory 'settings.json'
    Save-CombatViewerSettings @{FontSize=14;UnknownSetting='preserved';GameMonitor='Right'} $settings
    Save-CombatViewerSettings @{LogMonitor='Left'} $settings
    $saved=Read-CombatViewerSettings $settings
    Check 'settings merge preserves unrelated preferences' ($saved.FontSize -eq 14 -and $saved.UnknownSetting -eq 'preserved' -and $saved.LogMonitor -eq 'Left')
    $completed=$true
}finally{
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'result.json'),(@{passed=($completed -and @($checks | Where-Object {!$_.passed}).Count -eq 0);count=$checks.Count;checks=$checks.ToArray()} | ConvertTo-Json -Depth 5),$utf8)
}
$savedResult=Get-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Raw | ConvertFrom-Json
if(!$savedResult.passed){throw 'Result did not record successful completion'}
Write-Output "PASS $($checks.Count) checks"
