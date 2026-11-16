param([Parameter(Mandatory)][string]$RunRoot,[string]$Attempt='boundaries-01',[switch]$DisableServerProbe)
$ErrorActionPreference='Stop'
$out=Join-Path $RunRoot $Attempt
if(Test-Path -LiteralPath $out){throw 'Fresh attempt required'}
[void][IO.Directory]::CreateDirectory("$out/Binaries")
$socket=[Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback,0))
try {$port=$socket.Client.LocalEndPoint.Port} finally {$socket.Dispose()}
$processes=@{}
$meta=[ordered]@{started=(Get-Date -Format o);port=$port;serverProbeEnabled=(!$DisableServerProbe);processes=[ordered]@{}}
function Start-Game($role,$exe,$gameArgs){
    $allArgs=@($gameArgs)+@('-unattended','-nosound','-NoSplash','-forcelogflush',"-ABSLOG=$out/$role.log")
    $proc=Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -ArgumentList $allArgs -WindowStyle Hidden -PassThru
    $processes[$role]=$proc
    $meta.processes[$role]=[ordered]@{pid=$proc.Id;args=$allArgs;exit=$null;forced=$false}
}
function Wait-Log($role,$pattern,$seconds){
    $deadline=[DateTime]::UtcNow.AddSeconds($seconds)
    do {
        if($processes[$role].HasExited){throw "$role exited before $pattern"}
        if((Test-Path "$out/$role.log") -and (Select-String -LiteralPath "$out/$role.log" -Pattern $pattern -Quiet)){return}
        Start-Sleep -Milliseconds 250
    }while([DateTime]::UtcNow -lt $deadline)
    throw "$role timed out: $pattern"
}
function Start-Client($role,$offset){
    $events=[Collections.Generic.List[object]]::new()
    function At($frame,$command){$events.Add(@([int]$frame,[string]$command))}
    function Click($frame){At $frame 'Input.+key LeftMouseButton'; At ($frame+3) 'Input.-key LeftMouseButton'}
    At 270 'Input.+key W'; At 290 'Input.-key W'
    At 330 "Coop.Test.Snapshot $role-Ready"
    if($DisableServerProbe){
        At 360 'Coop.Test.Prepare 9'; At 420 "Coop.Test.Snapshot $role-Disabled"; At 510 'quit'
    }else{
        At ($offset+360) 'Coop.Test.Prepare 9'
        At ($offset+380) "Coop.Test.Snapshot $role-CostBefore"
        Click ($offset+390)
        foreach($frame in @(420,430,440,450,460)){At ($offset+$frame) 'Coop.Test.Request'}
        At ($offset+480) "Coop.Test.Snapshot $role-CostAfter"
        At ($offset+510) 'Coop.Test.Prepare 10'; Click ($offset+540)
        At ($offset+630) "Coop.Test.Snapshot $role-Boundary"
        At ($offset+660) 'Coop.Test.Prepare 100'; Click ($offset+690)
        foreach($frame in @(696,702,708,714,720)){At ($offset+$frame) 'Coop.Test.Request'}
        Click ($offset+726)
        At ($offset+735) "Coop.Test.Snapshot $role-CooldownDuring"
        At ($offset+780) "Coop.Test.Snapshot $role-CooldownExpired"
        Click ($offset+810)
        At ($offset+840) "Coop.Test.Snapshot $role-Reuse"
        At ($offset+900) 'Coop.Test.Prepare 100'
        foreach($frame in @(930,1020,1110)){Click ($offset+$frame)}
        At ($offset+1200) "Coop.Test.Snapshot $role-NormalThree"
        At 3000 'Coop.Test.Prepare 100'
        $stagger=if($role -eq 'a'){0}else{15}
        foreach($frame in @(3300,3420)){
            Click ($frame+$stagger)
            At ($frame+$stagger+30) 'Coop.Test.Request'
        }
        At 3540 "Coop.Test.Snapshot $role-Independent"
        At 3630 'quit'
    }
    $commands=$events | Sort-Object {$_[0]} | ForEach-Object { ('defer ' * $_[0])+$_[1] }
    $macro="$out/Binaries/$role.exec"
    $commands | Set-Content -LiteralPath $macro -Encoding ascii
    Start-Game $role "$RunRoot/Stage/WindowsClient/CoopRPG/Binaries/Win64/CoopRPGClient.exe" @("127.0.0.1:$port",'-CoopCombatTest','-RenderOffscreen','-windowed','-ResX=960','-ResY=540',('-ExecCmds="t.MaxFPS 30,Log LogCoopRPGInput Verbose,AbilitySystem.ServerRPCBatching.Log 1,exec '+$macro+'"'))
}
try{
    $endFrame=if($DisableServerProbe){600}else{2400}
    $serverArgs=@("-port=$port",'-MULTIHOME=127.0.0.1',('-ExecCmds="t.MaxFPS 15,AbilitySystem.ServerRPCBatching.Log 1,'+('defer '*$endFrame)+'quit"'))
    if(!$DisableServerProbe){$serverArgs+='-CoopCombatTest'}
    Start-Game server "$RunRoot/Stage/WindowsServer/CoopRPG/Binaries/Win64/CoopRPGServer.exe" $serverArgs
    Wait-Log server "listening on port $port\b" 60
    Start-Client a 0
    Start-Client b 1500
    Wait-Log a 'Welcomed by server' 60
    Wait-Log b 'Welcomed by server' 60
    foreach($role in @('a','b','server')){
        if(!$processes[$role].WaitForExit(180000)){throw "$role timeout"}
        if($processes[$role].ExitCode -ne 0){throw "$role exit $($processes[$role].ExitCode)"}
    }
}finally{
    foreach($role in @('b','a','server')){
        if(!$processes.ContainsKey($role)){continue}
        $proc=$processes[$role]
        if(!$proc.HasExited){$meta.processes[$role].forced=$true;$proc.Kill();[void]$proc.WaitForExit(10000)}
        if($proc.HasExited){$meta.processes[$role].exit=$proc.ExitCode}
    }
    $meta.finished=Get-Date -Format o
    $meta|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$out/processes.json" -Encoding utf8
}
