#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Server', 'Client')][string]$Role,
    [Parameter(Mandatory)][System.Net.IPAddress]$Address,
    [ValidateRange(1024, 65535)][int]$Port = 7777,
    [ValidateSet('A', 'B')][string]$ClientLabel = 'A',
    [string]$PackageRoot,
    [string]$LogRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (!$PackageRoot) { $PackageRoot = Join-Path $PSScriptRoot 'Stage' }
if (!$LogRoot) { $LogRoot = Join-Path $PSScriptRoot 'Logs' }

if ($Address.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork -or
    [System.Net.IPAddress]::IsLoopback($Address) -or
    $Address.Equals([System.Net.IPAddress]::Any)) {
    throw 'Use the server PC LAN IPv4 address.'
}

if ($Role -eq 'Server') {
    $localAddresses = [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces() |
        Where-Object OperationalStatus -eq 'Up' |
        ForEach-Object { $_.GetIPProperties().UnicastAddresses } |
        ForEach-Object { $_.Address }
    if ($Address -notin $localAddresses) {
        throw "The address $Address does not belong to an active interface on this PC."
    }
}

$package = [System.IO.Path]::GetFullPath($PackageRoot)
$exe = Join-Path $package "Windows$Role/CoopRPG/Binaries/Win64/CoopRPG$Role.exe"
if (!(Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Game executable not found: $exe. Extract the full Stage folder."
}

$label = if ($Role -eq 'Server') { 'server' } else { $ClientLabel.ToLowerInvariant() }
$logName = '{0}-{1}-{2}.log' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), $label, ([guid]::NewGuid().ToString('N').Substring(0, 8))
$logDirectory = New-Item -ItemType Directory -Path ([System.IO.Path]::GetFullPath($LogRoot)) -Force
$log = Join-Path $logDirectory.FullName $logName
$common = @('-NoSplash', '-forcelogflush', ('-ABSLOG="{0}"' -f $log))

if ($Role -eq 'Server') {
    $gameArguments = @('/Game/Maps/L_Movement?EnemyCombat', "-port=$Port", "-MULTIHOME=$Address", '-unattended', '-nullrhi', '-nosound', '-log')
    $windowStyle = 'Hidden'
} else {
    $gameArguments = @("${Address}:$Port", '-CoopManualInput', '-windowed', '-ResX=1280', '-ResY=720')
    $windowStyle = 'Normal'
}

$process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) `
    -ArgumentList ($gameArguments + $common) -WindowStyle $windowStyle -PassThru

"Role: $Role ($label)"
"Endpoint: ${Address}:$Port"
"Process ID: $($process.Id)"
"Log: $log"
if ($Role -eq 'Server') {
    'Wait for "listening on port" in the server log before launching the clients.'
}
