param([Parameter(Mandatory)][string]$RunRoot,[string]$Attempt='lifetime-01')
$ErrorActionPreference='Stop'
$out=Join-Path $RunRoot $Attempt
if(Test-Path -LiteralPath $out){throw 'Use a fresh attempt'}
[void][IO.Directory]::CreateDirectory("$out/Binaries")
$socket=[Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback,0))
try {$port=$socket.Client.LocalEndPoint.Port} finally {$socket.Dispose()}
$processes=@{}
$meta=[ordered]@{started=(Get-Date -Format o);port=$port;processes=[ordered]@{}}
function Start-Game($role,$exe,$arguments){
    $arguments+=@('-unattended','-nosound','-NoSplash','-forcelogflush',"-ABSLOG=$out/$role.log")
    $p=Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $processes[$role]=$p
    $meta.processes[$role]=[ordered]@{pid=$p.Id;args=$arguments;exit=$null;forced=$false}
}
function Wait-Log($role,$pattern){
    $limit=[DateTime]::UtcNow.AddSeconds(60)
    do{
        if($processes[$role].HasExited){throw "$role exited before $pattern"}
        if((Test-Path "$out/$role.log") -and (Select-String -LiteralPath "$out/$role.log" -Pattern $pattern -Quiet)){return}
        Start-Sleep -Milliseconds 250
    }while([DateTime]::UtcNow -lt $limit)
    throw "$role timed out: $pattern"
}
function Start-Client($role,[int]$offset){
    $events=[Collections.Generic.List[object]]::new()
    function At($frame,$command){$events.Add(@([int]($frame+$offset),[string]$command))}
    function Click($frame,$key='LeftMouseButton'){
        At $frame "Input.+key $key"
        At ($frame+3) "Input.-key $key"
    }
    function Observe($frame,$label){At $frame "Coop.Test.Snapshot $role-$label"}
    function FiveAttacks($start){for($i=0;$i -lt 5;$i++){Click ($start+15*$i)}}

    Observe 345 'FarBefore'
    FiveAttacks 360
    Observe 440 'FarAfter'

    At 460 'Input.+key W'
    At 480 'Input.-key W'
    At 510 'Coop.Test.Vitals 0 100'
    Observe 525 'DeadTargetBefore'
    FiveAttacks 540
    Observe 620 'DeadTargetAfter'

    At 640 'Coop.Test.Vitals 100 0'
    Observe 655 'DeadAttackerBefore'
    FiveAttacks 670
    Observe 750 'DeadAttackerAfter'
    At 765 'Coop.Test.Vitals 100 100'

    for($i=1;$i -le 5;$i++){
        $frame=810+110*($i-1)
        Observe ($frame-10) "Cancel$i-Before"
        Click $frame
        Click ($frame+6) 'RightMouseButton'
        Observe ($frame+40) "Cancel$i-After"
    }
    Observe 1370 'RecoveryBefore'
    Click 1380
    Click 1416 'RightMouseButton'
    Observe 1430 'RecoveryAfter'

    for($i=1;$i -le 5;$i++){
        $frame=1510+100*($i-1)
        At ($frame-20) 'Coop.Test.Prepare 100'
        Observe ($frame-6) "Duplicate$i-Before"
        Click $frame
        At ($frame+6) 'Coop.Test.ConfirmTwice'
        Observe ($frame+40) "Duplicate$i-After"
    }

    At 2010 'Coop.Test.Prepare 100'
    Observe 2025 'MissBefore'
    Click 2040
    At 2046 'Coop.Test.Vitals 0 100'
    Observe 2080 'MissAfter'
    At 2100 'Coop.Test.Prepare 100'

    $events.Add(@(4420,'quit'))
    $macro=("$out/Binaries/$role.exec").Replace('\','/')
    $events | Sort-Object {$_[0]} | ForEach-Object {('defer '*$_[0])+$_[1]} | Set-Content -LiteralPath $macro -Encoding ascii
    Start-Game $role "$RunRoot/Stage/WindowsClient/CoopRPG/Binaries/Win64/CoopRPGClient.exe" @(
        "127.0.0.1:$port",'-CoopCombatTest','-RenderOffscreen','-windowed','-ResX=960','-ResY=540',
        ('-ExecCmds="t.MaxFPS 30,Log LogCoopRPGInput Verbose,AbilitySystem.ServerRPCBatching.Log 1,exec '+$macro+'"'))
}
try{
    Start-Game server "$RunRoot/Stage/WindowsServer/CoopRPG/Binaries/Win64/CoopRPGServer.exe" @(
        "-port=$port",'-MULTIHOME=127.0.0.1','-CoopCombatTest',
        ('-ExecCmds="t.MaxFPS 15,Log LogCoopRPGInput Verbose,AbilitySystem.ServerRPCBatching.Log 1,'+('defer '*2580)+'quit"'))
    Wait-Log server "listening on port $port\b"
    Start-Client a 0
    Start-Client b 2200
    Wait-Log a 'Welcomed by server'
    Wait-Log b 'Welcomed by server'
    foreach($role in @('a','b','server')){
        if(!$processes[$role].WaitForExit(205000)){throw "$role timeout"}
        if($processes[$role].ExitCode -ne 0){throw "$role exit $($processes[$role].ExitCode)"}
    }
}finally{
    foreach($role in @('b','a','server')){
        if(!$processes.ContainsKey($role)){continue}
        $p=$processes[$role]
        if(!$p.HasExited){$meta.processes[$role].forced=$true;$p.Kill();[void]$p.WaitForExit(10000)}
        if($p.HasExited){$meta.processes[$role].exit=$p.ExitCode}
    }
    $meta.finished=Get-Date -Format o
    $meta|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$out/processes.json" -Encoding utf8
}
