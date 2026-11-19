Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot 'CombatLogReader.psm1')

function Read-CoopLauncherSession([string]$LogRoot) {
    if (!(Test-Path -LiteralPath $LogRoot -PathType Container)) { return }
    $latest = Get-ChildItem -LiteralPath $LogRoot -Directory -Filter 'launcher-*' |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'processes.json') -PathType Leaf } |
        Sort-Object Name -Descending | Select-Object -First 1
    if (!$latest) { return }
    $manifest = Get-Content -LiteralPath (Join-Path $latest.FullName 'processes.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    [pscustomobject]@{ Directory = $latest.FullName; Manifest = $manifest }
}

function Write-CoopLauncherJson([string]$Path, $Value) {
    $tempPath = Join-Path (Split-Path -Parent $Path) (([guid]::NewGuid().ToString('N')) + '.tmp')
    try {
        $json = $Value | ConvertTo-Json -Depth 5
        [IO.File]::WriteAllText($tempPath, $json, [Text.UTF8Encoding]::new($false))
        if ([IO.File]::Exists($Path)) {
            [IO.File]::Replace($tempPath, $Path, "$tempPath.bak")
        } else {
            [IO.File]::Move($tempPath, $Path)
        }
    } finally {
        if ([IO.File]::Exists($tempPath)) { [IO.File]::Delete($tempPath) }
        if ([IO.File]::Exists("$tempPath.bak")) { [IO.File]::Delete("$tempPath.bak") }
    }
}

function Save-CoopLauncherSession($Session) {
    Write-CoopLauncherJson (Join-Path $Session.Directory 'processes.json') $Session.Manifest
}

function Get-CoopLauncherSettingsPath([string]$LauncherRoot) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $key = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($LauncherRoot.ToUpperInvariant()))).Replace('-', '')
    } finally {
        $sha.Dispose()
    }
    $directory = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'CoopRPG/ValidationLauncher'
    Join-Path $directory "$key.json"
}

function Read-CoopLauncherSettings([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
    }
}

function Save-CoopLauncherSettings([string]$Path, $Settings) {
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    Write-CoopLauncherJson $Path $Settings
}

function ConvertTo-CoopLauncherIPv4([string]$Text) {
    $ip = $null
    if ($Text -match '^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$' -and
        [ipaddress]::TryParse($Text, [ref]$ip) -and !$ip.Equals([ipaddress]::Any)) { $ip }
}

function ConvertFrom-CoopLauncherEndpoint([string]$Text) {
    if ($Text -notmatch '^\s*(?<address>[0-9.]+):(?<port>[0-9]{1,5})\s*$') { return }
    $port = [int]$Matches.port
    $address = ConvertTo-CoopLauncherIPv4 $Matches.address
    if ($address -and $port -ge 1024 -and $port -le 65535) {
        [pscustomobject]@{ address = $address.ToString(); port = $port }
    }
}

function Read-CoopLauncherRemoteEndpoint([string]$Path) {
    $legacy = !(Test-Path -LiteralPath $Path -PathType Leaf)
    $files = if ($legacy) {
        Get-ChildItem -LiteralPath (Split-Path -Parent $Path) -Filter '*.json' -File -ErrorAction SilentlyContinue |
            Where-Object BaseName -match '^[A-Fa-f0-9]{64}$' | Sort-Object LastWriteTime -Descending |
            Select-Object -ExpandProperty FullName
    } else { $Path }
    foreach ($file in $files) {
        try {
            $settings = Read-CoopLauncherSettings $file
            if (!$settings -or !$settings.PSObject.Properties['port']) { continue }
            if ($legacy) {
                if (!$settings.PSObject.Properties['mode'] -or $settings.mode -ne 2 -or
                    !$settings.PSObject.Properties['manualAddress']) { continue }
                $address = $settings.manualAddress
            } else {
                if (!$settings.PSObject.Properties['address']) { continue }
                $address = $settings.address
            }
            $endpoint = ConvertFrom-CoopLauncherEndpoint ('{0}:{1}' -f $address, $settings.port)
            if ($endpoint) { return $endpoint }
        } catch { continue }
    }
}

function Get-CoopLauncherPackageInfo([string]$Stage) {
    $unknown = [pscustomobject]@{ BuildId = ''; Roster = 'Unknown' }
    $directory = if ((Split-Path -Leaf $Stage) -eq 'Stage') { Split-Path -Parent $Stage } else { $Stage }
    $path = Join-Path $directory 'package-manifest.json'
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { return $unknown }

    try {
        $manifest = Read-CoopLauncherSettings $path
        if (!$manifest -or !$manifest.PSObject.Properties['buildId'] -or
            $manifest.buildId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,35}$' -or
            !$manifest.PSObject.Properties['capabilities'] -or !$manifest.capabilities -or
            !$manifest.capabilities.PSObject.Properties['validationParticipants'] -or
            $manifest.capabilities.validationParticipants -isnot [bool]) { return $unknown }

        $verified = $false
        foreach ($role in @('Server', 'Client')) {
            $binary = Join-Path $Stage "Windows$role/CoopRPG/Binaries/Win64/CoopRPG$role.exe"
            if (!(Test-Path -LiteralPath $binary -PathType Leaf)) { continue }
            $property = $manifest.PSObject.Properties["${role}Sha256"]
            if (!$property -or (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash -ne $property.Value) {
                return [pscustomobject]@{ BuildId = ''; Roster = 'Mismatch' }
            }
            $verified = $true
        }
        if ($verified) {
            $roster = if ($manifest.capabilities.validationParticipants) { 'Supported' } else { 'Unsupported' }
            return [pscustomobject]@{ BuildId = [string]$manifest.buildId; Roster = $roster }
        }
    } catch { return $unknown }
    $unknown
}

function Lock-CoopLauncherLogRoot([string]$LogRoot) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $key = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($LogRoot.ToUpperInvariant()))).Replace('-', '')
    } finally {
        $sha.Dispose()
    }
    $mutex = [Threading.Mutex]::new($false, "Local\CoopRPGValidation-$key")
    try {
        if ($mutex.WaitOne(0)) { return $mutex }
    } catch [Threading.AbandonedMutexException] {
        return $mutex
    }
    $mutex.Dispose()
    throw '이 로그 폴더를 관리하는 실행기가 이미 열려 있습니다.'
}

function Get-CoopLauncherLocalAddresses {
    foreach ($adapter in [Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces()) {
        if ($adapter.OperationalStatus -ne [Net.NetworkInformation.OperationalStatus]::Up) { continue }
        $isTailscale = $adapter.Name -match 'Tailscale' -or $adapter.Description -match 'Tailscale'
        foreach ($entry in $adapter.GetIPProperties().UnicastAddresses) {
            $ip = $entry.Address
            if ($ip.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork -or [ipaddress]::IsLoopback($ip) -or $ip.Equals([ipaddress]::Any)) { continue }
            [pscustomobject]@{ Address = $ip; AdapterName = $adapter.Name; Key = "$($adapter.Id)|$ip"; IsTailscale = $isTailscale }
        }
    }
}

function New-CoopLauncherSession([string]$LogRoot, [ipaddress]$Address, [int]$Port, [int]$Mode,
    [string]$PackageRoot, [string]$AddressMode, [string]$AdapterKey) {
    $name = 'launcher-{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), [guid]::NewGuid().ToString('N').Substring(0, 8)
    $directory = Join-Path $LogRoot $name
    [void][IO.Directory]::CreateDirectory($directory)
    [pscustomobject]@{
        Directory = $directory
        Manifest = [pscustomobject]@{
            address = $Address.ToString(); port = $Port; mode = $Mode; sources = @()
            packageRoot = $PackageRoot; addressMode = $AddressMode; adapterKey = $AdapterKey
        }
    }
}

function Get-CoopLauncherSource($Session, [string]$Id) {
    if ($Session) {
        $Session.Manifest.sources | Where-Object { $_.id -eq $Id } | Select-Object -First 1
    }
}

function Get-CoopLauncherProcess($Source, [string]$PackageRoot) {
    if (!$Source) { return }
    $role = if ($Source.id -eq 'server') { 'Server' } else { 'Client' }
    $expectedPath = Join-Path $PackageRoot "Windows$role/CoopRPG/Binaries/Win64/CoopRPG$role.exe"
    $expectedPath = [IO.Path]::GetFullPath($expectedPath)
    $process = Get-Process -Id $Source.processId -ErrorAction SilentlyContinue
    if (!$process) { return }
    $matches = $false
    try {
        # Open a handle before verifying identity, so a later stop uses this process.
        [void]$process.Handle
        $matches = !$process.HasExited -and
            $process.StartTime.ToUniversalTime().Ticks -eq [long]$Source.startTicks -and
            $process.MainModule.FileName -eq $expectedPath
        if ($matches) { $process }
    } catch [InvalidOperationException] {
        if (!$process.HasExited) { throw }
    } finally {
        if (!$matches) { $process.Dispose() }
    }
}

function Test-CoopLauncherPort([ipaddress]$Address, [int]$Port) {
    foreach ($endpoint in [Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveUdpListeners()) {
        if ($endpoint.Port -eq $Port -and ($endpoint.Address.Equals($Address) -or $endpoint.Address.Equals([ipaddress]::Any))) {
            return $true
        }
    }
    return $false
}

function Start-CoopLauncherProcess($Session, [ValidateSet('server', 'a', 'b')][string]$Id, [string]$PackageRoot) {
    $old = Get-CoopLauncherSource $Session $Id
    $running = Get-CoopLauncherProcess $old $PackageRoot
    if ($running) {
        $running.Dispose()
        throw '이미 실행 중입니다.'
    }
    $address = [ipaddress]$Session.Manifest.address
    $port = $Session.Manifest.port
    if ($Id -eq 'server' -and (Test-CoopLauncherPort $address $port)) {
        throw "UDP $port 포트를 다른 프로세스가 사용 중입니다."
    }

    $role = if ($Id -eq 'server') { 'Server' } else { 'Client' }
    $label = switch ($Id) { 'server' { '서버' }; 'a' { '클라이언트 A' }; 'b' { '클라이언트 B' } }
    $options = @{
        Role = $role; Address = $address; Port = $port; PackageRoot = $PackageRoot
        LogRoot = $Session.Directory; AllowLoopback = $true; PassThru = $true
    }
    if ($role -eq 'Client') { $options.ClientLabel = $Id.ToUpperInvariant() }
    $launch = & (Join-Path $PSScriptRoot 'Start-CoopLan.ps1') @options
    $process = $launch.Process
    try {
        $started = $process.StartTime.ToUniversalTime()
        $source = [pscustomobject]@{
            id = $Id
            label = $label
            file = Split-Path -Leaf $launch.LogPath
            processId = $process.Id
            processName = "CoopRPG$role"
            startedUtc = $started.ToString('o')
            startTicks = $started.Ticks.ToString([Globalization.CultureInfo]::InvariantCulture)
            stoppedByLauncher = $false
        }
        $previous = $Session.Manifest.sources
        $Session.Manifest.sources = @($previous | Where-Object { $_.id -ne $Id }) + @($source)
        try {
            Save-CoopLauncherSession $Session
        } catch {
            $Session.Manifest.sources = $previous
            throw
        }
        $source
    } catch {
        if (!$process.HasExited) { $process.Kill() }
        throw
    } finally {
        $process.Dispose()
    }
}

function Stop-CoopLauncherProcess($Session, $Source, [string]$PackageRoot) {
    $process = Get-CoopLauncherProcess $Source $PackageRoot
    if (!$process) { return }
    try {
        $process.Kill()
        $Source.stoppedByLauncher = $true
        Save-CoopLauncherSession $Session
    } finally {
        $process.Dispose()
    }
}

function Write-CoopLauncherClientStop($Session, $Source, [string]$Event, [string]$Method = '') {
    $entry = [ordered]@{
        utc = [DateTime]::UtcNow.ToString('o'); event = $Event
        client = $Source.id; processId = $Source.processId; method = $Method
    }
    $line = ($entry | ConvertTo-Json -Compress) + [Environment]::NewLine
    [IO.File]::AppendAllText((Join-Path $Session.Directory 'launcher-events.jsonl'), $line, [Text.UTF8Encoding]::new($false))
}

function Update-CoopLauncherClientStop($Session, $Stop, [string]$PackageRoot) {
    $source = $Stop.Source
    $process = Get-CoopLauncherProcess $source $PackageRoot
    if (!$process) {
        $source.stoppedByLauncher = $true
        Save-CoopLauncherSession $Session
        $method = if ($Stop.ForceSent) { 'Forced' } elseif ($Stop.CloseSent) { 'WindowClose' } else { 'AlreadyExited' }
        Write-CoopLauncherClientStop $Session $source 'ClientStopCompleted' $method
        return $true
    }
    try {
        if (!$Stop.CloseSent -and !$Stop.ForceSent) {
            $process.Refresh()
            $Stop.CloseSent = $process.CloseMainWindow()
            if ($Stop.CloseSent) { Write-CoopLauncherClientStop $Session $source 'ClientStopRequested' 'WindowClose' }
        }
        if (!$Stop.ForceSent -and $Stop.Elapsed.Elapsed.TotalSeconds -ge 5 -and !$process.HasExited) {
            try { $process.Kill() } catch { if (!$process.HasExited) { throw } }
            $Stop.ForceSent = $true
            Write-CoopLauncherClientStop $Session $source 'ClientStopRequested' 'Forced'
        }
        return $false
    } finally {
        $process.Dispose()
    }
}

function New-CoopLauncherObservation([string]$Path) {
    $reader = New-CombatLogReader $Path
    # A reopened launcher needs the startup events even for a long-lived run.
    $reader.Initialized = $true
    @{
        Reader = $reader; Listening = $false; Welcomed = $false; Joined = $false
        Map = ''; Stage = ''; Failure = ''; Message = ''; Diagnostic = ''; Generation = 0
        Round = ''; Roster = $null; Traveling = $false
    }
}

function Update-CoopLauncherObservation($Observation, [int]$Port) {
    $records = @(Read-CombatLogRecords $Observation.Reader)
    if ($Observation.Generation -ne $Observation.Reader.Generation) {
        $Observation.Generation = $Observation.Reader.Generation
        $Observation.Listening = $false
        $Observation.Welcomed = $false
        $Observation.Joined = $false
        $Observation.Map = ''
        $Observation.Stage = ''
        $Observation.Failure = ''
        $Observation.Message = ''
        $Observation.Diagnostic = ''
        $Observation.Round = ''
        $Observation.Roster = $null
        $Observation.Traveling = $false
    }
    foreach ($record in $records) {
        $line = $record.Line
        switch -Regex ($line) {
            ('LogNet: .*IpNetDriver listening on port {0}(?:\s|$)' -f $Port) {
                $Observation.Listening = $true
                break
            }
            'LogNet: Welcomed by server \(Level: .*?/(?<map>L_[^,]+)' {
                $Observation.Map = $Matches.map
                $Observation.Welcomed = $true
                break
            }
            'LogCoopRPGManualInput: .*ManualInputEnabled .*NetMode=Client' {
                if ($Observation.Welcomed) { $Observation.Joined = $true }
                break
            }
            'LogCoopRPGConnectionNotice: .*ConnectionNoticeQueued Reason=(?<reason>\w+) Failure=(?<failure>\w+)' {
                $reason = $Matches.reason
                $names = @{
                    RoundFull = '정원 2명이 모두 접속했습니다.'
                    RoundInProgress = '이미 진행 중인 라운드입니다.'
                    RoundTravelReserved = '기존 참가자가 다음 맵으로 이동 중입니다.'
                    RoundFinished = '라운드가 종료됐습니다.'
                    UnsupportedJoin = '클라이언트 한 개당 한 명만 접속할 수 있습니다.'
                    ConnectionEnded = '서버 연결이 끊겼습니다.'
                    ConnectionFailed = '서버에 접속할 수 없습니다.'
                }
                $message = if ($Matches.failure -eq 'ConnectionTimeout' -and $reason -eq 'ConnectionFailed') {
                    '접속 응답을 기다리다가 시간이 초과됐습니다.'
                } elseif ($names.ContainsKey($reason)) { $names[$reason] } else { '연결 오류가 발생했습니다.' }
                $Observation.Failure = $reason
                $Observation.Message = "$message ($reason)"
                break
            }
            'LogCoopRPGRound: (?:\w+: )?LootRoundCreated Round=(?<round>[0-9A-Fa-f]{32}) .* Map=(?<map>L_\w+)' {
                $Observation.Map = $Matches.map
                $Observation.Round = $Matches.round
                $Observation.Roster = $null
                $Observation.Traveling = $false
                break
            }
            'LogCoopRPGValidationRoster: ValidationParticipants Round=(?<round>[0-9A-Fa-f]{32}) P1Label=(?<p1Label>A|B|None) P1Id=(?<p1Id>-?\d+) P1State=(?<p1State>Disconnected|Joining|Waiting|Ready) P2Label=(?<p2Label>A|B|None) P2Id=(?<p2Id>-?\d+) P2State=(?<p2State>Disconnected|Joining|Waiting|Ready)$' {
                if ($Observation.Round -eq $Matches.round) {
                    $Observation.Roster = @(
                        @{ Label = $Matches.p1Label; PlayerId = [int]$Matches.p1Id; State = $Matches.p1State }
                        @{ Label = $Matches.p2Label; PlayerId = [int]$Matches.p2Id; State = $Matches.p2State }
                    )
                }
                break
            }
            'LogCoopRPGRoundTravel: (?:Error: )?RoundTravel(?<phase>Started|Failed) Round=(?<round>[0-9A-Fa-f]{32}) ' {
                if ($Observation.Round -eq $Matches.round) {
                    $Observation.Traveling = $Matches.phase -eq 'Started'
                }
                break
            }
            'Log(?:Net|Windows|Init|Load): (?:Error|Fatal): (?<detail>.*)' {
                $Observation.Diagnostic = $Matches.detail
                break
            }
        }
    }
    $Observation.Stage = switch ($Observation.Map) {
        'L_Movement' { '스테이지 1' }
        'L_Movement_Round2' { '스테이지 2' }
        default { $Observation.Map }
    }
}

Export-ModuleMember -Function Read-CoopLauncherSession, New-CoopLauncherSession, Get-CoopLauncherSource,
    Get-CoopLauncherProcess, Test-CoopLauncherPort, Start-CoopLauncherProcess, Stop-CoopLauncherProcess,
    Write-CoopLauncherClientStop, Update-CoopLauncherClientStop, New-CoopLauncherObservation,
    Update-CoopLauncherObservation, Get-CoopLauncherSettingsPath,
    Read-CoopLauncherSettings, Save-CoopLauncherSettings, Lock-CoopLauncherLogRoot, Get-CoopLauncherLocalAddresses,
    ConvertTo-CoopLauncherIPv4, ConvertFrom-CoopLauncherEndpoint, Read-CoopLauncherRemoteEndpoint, Get-CoopLauncherPackageInfo
