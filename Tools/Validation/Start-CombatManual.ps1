param(
    [string]$PackageRoot,
    [string]$GameMonitor='',[string]$LogMonitor='',[switch]$ListMonitors,
    [ValidateSet('SideBySide','Stacked')][string]$Layout='SideBySide',
    [switch]$AlwaysOnTop,
    [switch]$NoForeground,
    [switch]$NoLogViewer,
    [switch]$EnemyCombat,
    [ValidateSet('A','B')][string]$FocusClient='A'
)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'CombatViewerLayout.psm1') -Force
if($ListMonitors){Get-CombatMonitors | Format-Table Index,DeviceName,Primary,Left,Top,Width,Height -AutoSize;return}
if(!$PackageRoot){throw 'PackageRoot is required unless -ListMonitors is used.'}

# Only the processes launched here are managed. No persistent OS/game settings are changed.
if (!('CoopManualWindow' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class CoopManualWindow {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct MonitorInfo { public int Size; public Rect Monitor, Work; public uint Flags; }
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Auto)] static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfo info);
    [DllImport("user32.dll", SetLastError=true)] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern bool SetWindowText(IntPtr window, string title);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll", EntryPoint="GetWindowLongW")] public static extern int GetWindowLong(IntPtr window, int index);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    public static Rect WorkArea() {
        var info = new MonitorInfo(); info.Size = Marshal.SizeOf(info);
        if (!GetMonitorInfo(MonitorFromWindow(GetForegroundWindow(), 2), ref info)) throw new InvalidOperationException("Cannot read monitor work area");
        return info.Work;
    }
}
'@
}
$root=[IO.Path]::GetFullPath($PackageRoot)
$server=Join-Path $root 'WindowsServer/CoopRPG/Binaries/Win64/CoopRPGServer.exe'
$client=Join-Path $root 'WindowsClient/CoopRPG/Binaries/Win64/CoopRPGClient.exe'
foreach($exe in @($server,$client)){if(!(Test-Path -LiteralPath $exe)){throw "Missing package: $exe"}}
$out=Join-Path (Split-Path $root) ('manual-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
[void][IO.Directory]::CreateDirectory($out)
$socket=[Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback,0))
try{$port=$socket.Client.LocalEndPoint.Port}finally{$socket.Dispose()}
$started=@{}
$windows=@{}
$oldDpi=[CoopManualWindow]::SetThreadDpiAwarenessContext([IntPtr](-4))
function Wait-Log([string]$Role,[string]$Pattern) {
    for($i=0;$i -lt 240;$i++) {
        if($started[$Role].HasExited){throw "$Role exited before readiness"}
        if((Test-Path "$out/$Role.log") -and (Select-String -LiteralPath "$out/$Role.log" -Pattern $Pattern -Quiet)){return}
        Start-Sleep -Milliseconds 250
    }
    throw "$Role startup timeout; inspect $out/$Role.log"
}
try {
    $foregroundWork=[CoopManualWindow]::WorkArea()
    $screens=@(Get-CombatMonitors)
    $foregroundScreen=$screens | Where-Object { $_.Left -eq $foregroundWork.Left -and $_.Top -eq $foregroundWork.Top } | Select-Object -First 1
    $settings=Read-CombatViewerSettings
    $gameChoice=if($GameMonitor){$GameMonitor}elseif($settings.GameMonitor){$settings.GameMonitor}else{''}
    $logChoice=if($LogMonitor){$LogMonitor}elseif($settings.LogMonitor){$settings.LogMonitor}else{''}
    $placement=Resolve-CombatLayout -Monitors $screens -GameMonitor $gameChoice -LogMonitor $logChoice -DefaultGameDevice $foregroundScreen.DeviceName -Layout $Layout -NoLogViewer:$NoLogViewer
    foreach($note in $placement.Notes){Write-Warning $note}
    $work=$placement.Game
    $gap=12;$width=$placement.A.Width;$height=$placement.A.Height
    $common=@('-nosound','-NoSplash','-forcelogflush')
    if(!$EnemyCombat){$common+='-CoopCombatTest'}
    $mapArgs=@(if($EnemyCombat){'/Game/Maps/L_Movement?EnemyCombat'})
    # Server log is written to disk; its console must not cover the two game windows.
    $started.server=Start-Process -FilePath $server -WorkingDirectory (Split-Path $server) -ArgumentList ($mapArgs+@("-port=$port",'-MULTIHOME=127.0.0.1','-log',"-ABSLOG=`"$out/server.log`"",'-ExecCmds="t.MaxFPS 30,Log LogCoopRPGInput Verbose,AbilitySystem.ServerRPCBatching.Log 1"')+$common) -WindowStyle Hidden -PassThru
    Wait-Log 'server' "listening on port $port\b"
    foreach($role in @('a','b')) {
        $bounds=if($role -eq 'a'){$placement.A}else{$placement.B}
        $x=$bounds.Left;$y=$bounds.Top
        $started[$role]=Start-Process -FilePath $client -WorkingDirectory (Split-Path $client) -ArgumentList (@("127.0.0.1:$port",'-CoopManualInput','-windowed',"-ResX=$($width-16)","-ResY=$($height-39)","-WinX=$x","-WinY=$y","-ABSLOG=`"$out/$role.log`"",'-ExecCmds="t.MaxFPS 30,Log LogCoopRPGInput Verbose,AbilitySystem.ServerRPCBatching.Log 1"')+$common) -WindowStyle Normal -PassThru
        Wait-Log $role 'ManualInputEnabled'
        for($i=0;$i -lt 120;$i++) {
            $started[$role].Refresh()
            if($started[$role].HasExited){throw "$role exited before its window appeared"}
            if($started[$role].MainWindowHandle -ne [IntPtr]::Zero){break}
            Start-Sleep -Milliseconds 250
        }
        $handle=$started[$role].MainWindowHandle
        if($handle -eq [IntPtr]::Zero){throw "$role window timeout"}
        $windows[$role]=$handle
        if(![CoopManualWindow]::SetWindowText($handle,"CoopRPG | Client $($role.ToUpperInvariant()) | Manual")){throw "Cannot label client $role"}
        $after=if($AlwaysOnTop){[IntPtr](-1)}else{[IntPtr]::Zero}
        if(![CoopManualWindow]::SetWindowPos($handle,$after,$x,$y,$width,$height,0x50)){throw "Cannot position client $role"}
    }
    # Raise both once; focus belongs to one client, never repeatedly stolen on a timer.
    foreach($role in @('b','a')) {
        $after=if($AlwaysOnTop){[IntPtr](-1)}else{[IntPtr]::Zero}
        if(![CoopManualWindow]::SetWindowPos($windows[$role],$after,0,0,0,0,0x53)){throw "Cannot raise client $role"}
    }
    $foregroundRequested=!$NoForeground
    $foregroundGranted=$false
    $meta=[ordered]@{started=(Get-Date -Format o);port=$port;packageRoot=$root;logs=$out;layout=$Layout;alwaysOnTop=[bool]$AlwaysOnTop;workArea=$work;foregroundRequested=$foregroundRequested;foregroundGranted=$foregroundGranted;focusClient=$FocusClient;processes=@{};windows=@{}}
    foreach($role in $started.Keys){$meta.processes[$role]=$started[$role].Id}
    foreach($role in $windows.Keys) {
        $rect=New-Object CoopManualWindow+Rect
        if(![CoopManualWindow]::GetWindowRect($windows[$role],[ref]$rect)){throw "Cannot read client $role window bounds"}
        $meta.windows[$role]=@{handle=$windows[$role].ToInt64();bounds=$rect;topmost=([CoopManualWindow]::GetWindowLong($windows[$role],-20) -band 8) -ne 0;foreground=[CoopManualWindow]::GetForegroundWindow() -eq $windows[$role]}
    }
    $meta.sources=@(
        foreach($role in @('server','a','b')){
            $label=if($role -eq 'server'){'서버'}elseif($role -eq 'a'){'클라이언트 1'}else{'클라이언트 2'}
            @{id=$role;label=$label;file=($role+'.log');processId=$started[$role].Id;processName=$started[$role].ProcessName;startedUtc=$started[$role].StartTime.ToUniversalTime().ToString('o')}
        }
    )
    $meta.enemyCombat=[bool]$EnemyCombat
    $meta.monitorLayout=@{game=$placement.Game.DeviceName;log=$placement.Log.DeviceName;separate=$placement.Separate}
    # The viewer reads this manifest at startup, before its own PID is known.
    $meta|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$out/processes.json" -Encoding utf8
    if(!$NoLogViewer) {
        $viewer=Join-Path $PSScriptRoot 'Watch-CombatManual.ps1'
        $v=$placement.Viewer
        $viewerArgs=@('-NoProfile','-ExecutionPolicy','Bypass','-STA','-File',"`"$viewer`"",'-LogDirectory',"`"$out`"",'-Monitor',$placement.Log.DeviceName,'-Left',$v.Left,'-Top',$v.Top,'-Width',$v.Width,'-Height',$v.Height)
        if($placement.Separate){$viewerArgs+='-Maximized'}else{$viewerArgs+='-Docked'}
        if($AlwaysOnTop){$viewerArgs+='-AlwaysOnTop'}
        # Hide only the helper console; its WinForms log window remains visible.
        $started.logViewer=Start-Process -FilePath 'powershell.exe' -ArgumentList $viewerArgs -WindowStyle Hidden -RedirectStandardOutput "$out/log-viewer.stdout.log" -RedirectStandardError "$out/log-viewer.stderr.log" -PassThru
        $viewerReady=$false
        for($i=0;$i -lt 60;$i++){
            $started.logViewer.Refresh()
            if($started.logViewer.HasExited){break}
            if($started.logViewer.MainWindowHandle -ne [IntPtr]::Zero){$viewerReady=$true;break}
            Start-Sleep -Milliseconds 100
        }
        if(!$viewerReady){Write-Warning "Log window did not become visible. See $out/log-viewer.stderr.log; the games remain available."}
        $meta.processes.logViewer=$started.logViewer.Id
        $meta.logViewer=@{enabled=$true;windowReady=$viewerReady;left=$v.Left;top=$v.Top;width=$v.Width;height=$v.Height;maximized=$placement.Separate}
    } else {$meta.logViewer=@{enabled=$false}}
    # Request focus once, after the optional viewer has opened.
    if($foregroundRequested){$foregroundGranted=[CoopManualWindow]::SetForegroundWindow($windows[$FocusClient.ToLowerInvariant()])}
    $meta.foregroundGranted=$foregroundGranted
    foreach($role in $windows.Keys){$meta.windows[$role].foreground=[CoopManualWindow]::GetForegroundWindow() -eq $windows[$role]}
    $meta|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$out/processes.json" -Encoding utf8
    Write-Host "Two manual clients ready. No automatic timeout. Logs: $out"
    Write-Host 'Cursor is visible/unlocked. After focus loss/window switching, the first left click is consumed; the next click attacks.'
    Write-Host 'Always-on-top is opt-in (-AlwaysOnTop). Use Alt+Tab or click the other window to switch.'
    if(!$NoLogViewer -and $viewerReady){Write-Host 'Live log viewer: Integrated | Server | Client 1 | Client 2. Separate monitor when available; closing it does not stop the games.'}
    if($foregroundRequested -and !$foregroundGranted){Write-Warning 'Windows refused the one-time foreground request. Click the Client A/B title bar; the launcher will not steal focus repeatedly.'}
    try{Save-CombatViewerSettings @{GameMonitor=$placement.Game.DeviceName;LogMonitor=$placement.Log.DeviceName}}catch{Write-Warning ('Local settings not saved: '+$_.Exception.Message)}
    Write-Host "Finish: close both client windows, then Stop-Process -Id $($started.server.Id) (this run's server only)."
} catch {
    foreach($proc in $started.Values){if(!$proc.HasExited){$proc.Kill()}}
    throw
} finally {
    if($oldDpi -ne [IntPtr]::Zero){[void][CoopManualWindow]::SetThreadDpiAwarenessContext($oldDpi)}
}
