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
