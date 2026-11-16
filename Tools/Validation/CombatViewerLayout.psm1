Set-StrictMode -Version Latest

function Get-CombatMonitors {
    Add-Type -AssemblyName System.Windows.Forms
    $index=0
    foreach($screen in [Windows.Forms.Screen]::AllScreens){
        $index++
        [pscustomobject]@{Index=$index;DeviceName=$screen.DeviceName;Primary=$screen.Primary;Left=$screen.WorkingArea.Left;Top=$screen.WorkingArea.Top;Width=$screen.WorkingArea.Width;Height=$screen.WorkingArea.Height;Right=$screen.WorkingArea.Right;Bottom=$screen.WorkingArea.Bottom;Label=('{0}: {1} · {2}×{3}' -f $index,$screen.DeviceName,$screen.WorkingArea.Width,$screen.WorkingArea.Height)}
    }
}

function Find-CombatMonitor($Monitors,[string]$Choice){
    @($Monitors | Where-Object { $_.DeviceName -eq $Choice -or [string]$_.Index -eq $Choice }) | Select-Object -First 1
}

function Get-CombatViewerSettingsPath {
    [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Artifacts/Validation/viewer-settings.json'))
}
function Read-CombatViewerSettings([string]$Path=(Get-CombatViewerSettingsPath)){
    $values=@{}
    if(Test-Path -LiteralPath $Path){
        try{$data=[IO.File]::ReadAllText($Path) | ConvertFrom-Json;foreach($p in $data.PSObject.Properties){$values[$p.Name]=$p.Value}}
        catch{Write-Warning ('Local viewer settings ignored: '+$_.Exception.Message)}
    }
    return $values
}
function Save-CombatViewerSettings([hashtable]$Values,[string]$Path=(Get-CombatViewerSettingsPath)){
    $merged=Read-CombatViewerSettings -Path $Path
    foreach($key in $Values.Keys){$merged[$key]=$Values[$key]}
    [void][IO.Directory]::CreateDirectory((Split-Path $Path))
    $temp=$Path+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    try{
        [IO.File]::WriteAllText($temp,($merged | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
        if(Test-Path -LiteralPath $Path){[IO.File]::Replace($temp,$Path,($Path+'.previous'))}else{[IO.File]::Move($temp,$Path)}
    }finally{if(Test-Path -LiteralPath $temp){Remove-Item -LiteralPath $temp}}
}

function Resolve-CombatLayout($Monitors,[string]$GameMonitor='', [string]$LogMonitor='', [string]$DefaultGameDevice='', [string]$Layout='SideBySide',[switch]$NoLogViewer){
    if(@($Monitors).Count -eq 0){throw 'No monitors available'}
    $notes=New-Object Collections.Generic.List[string]
    $game=Find-CombatMonitor $Monitors $GameMonitor
    if(!$game){if($GameMonitor){$notes.Add('게임 모니터 선택을 찾지 못해 현재 화면으로 복구했습니다.')};$game=Find-CombatMonitor $Monitors $DefaultGameDevice}
    if(!$game){$game=$Monitors | Where-Object Primary | Select-Object -First 1}
    if(!$game){$game=$Monitors[0]}
    $log=Find-CombatMonitor $Monitors $LogMonitor
    if(!$log){
        if($LogMonitor){$notes.Add('로그 모니터 선택을 찾지 못해 사용 가능한 화면으로 복구했습니다.')}
        $others=@($Monitors | Where-Object DeviceName -NE $game.DeviceName)
        $log=if($others.Count){$others[0]}else{$game}
    }
    $separate=!$NoLogViewer -and $log.DeviceName -ne $game.DeviceName
    $gap=12;$viewerHeight=0
    if(!$NoLogViewer -and !$separate){$viewerHeight=[int][Math]::Min(540,[Math]::Max(480,[Math]::Floor($game.Height*0.50)))}
    $gameAreaHeight=$game.Height-$(if($viewerHeight){$viewerHeight+$gap}else{0})
    if($Layout -eq 'SideBySide'){
        $width=[int][Math]::Min(976,[Math]::Floor(($game.Width-3*$gap)/2))
        $height=[int][Math]::Min($gameAreaHeight-2*$gap,[Math]::Round(($width-16)*9/16)+39)
    }else{
        $height=[int][Math]::Min(579,[Math]::Floor(($gameAreaHeight-3*$gap)/2))
        $width=[int][Math]::Min($game.Width-2*$gap,[Math]::Round(($height-39)*16/9)+16)
    }
    if($width -lt 336 -or $height -lt 239){throw 'Work area too small. Use SideBySide, -NoLogViewer, or a larger monitor.'}
    $a=[pscustomobject]@{Left=$game.Left+$gap;Top=$game.Top+$gap;Width=$width;Height=$height}
    $b=[pscustomobject]@{Left=$a.Left+$(if($Layout -eq 'SideBySide'){$width+$gap}else{0});Top=$a.Top+$(if($Layout -eq 'Stacked'){$height+$gap}else{0});Width=$width;Height=$height}
    $viewer=if($separate){[pscustomobject]@{Left=$log.Left;Top=$log.Top;Width=$log.Width;Height=$log.Height;Maximized=$true}}else{[pscustomobject]@{Left=$game.Left+$gap;Top=$game.Bottom-$viewerHeight-$gap;Width=$game.Width-2*$gap;Height=$viewerHeight;Maximized=$false}}
    [pscustomobject]@{Game=$game;Log=$log;Separate=$separate;A=$a;B=$b;Viewer=$viewer;Notes=@($notes)}
}

Export-ModuleMember -Function Get-CombatMonitors,Find-CombatMonitor,Get-CombatViewerSettingsPath,Read-CombatViewerSettings,Save-CombatViewerSettings,Resolve-CombatLayout
