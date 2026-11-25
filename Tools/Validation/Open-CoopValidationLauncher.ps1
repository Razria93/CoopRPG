#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$PackageRoot,
    [string]$LogRoot,
    [string]$Address = '127.0.0.1',
    [ValidateRange(1024, 65535)][int]$Port = 7777,
    [switch]$ServerForceLogFlush
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'CoopValidationSession.psm1')
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()

function Test-LauncherExecutable([string]$Stage, [string]$Role) {
    [IO.File]::Exists((Join-Path $Stage "Windows$Role/CoopRPG/Binaries/Win64/CoopRPG$Role.exe"))
}

function Find-LauncherStage([string]$Directory) {
    if (!$Directory) { return }
    foreach ($candidate in @($Directory, (Join-Path $Directory 'Stage'))) {
        if ((Test-LauncherExecutable $candidate 'Server') -or (Test-LauncherExecutable $candidate 'Client')) {
            return (Get-Item -LiteralPath $candidate).FullName
        }
    }
}

$requestedPackage = $PackageRoot
$logOverride = $LogRoot
$explicitAddress = $PSBoundParameters.ContainsKey('Address')
$explicitPort = $PSBoundParameters.ContainsKey('Port')
$settingsPath = Get-CoopLauncherSettingsPath $PSScriptRoot
$remoteEndpointPath = Join-Path (Split-Path -Parent $settingsPath) 'last-remote-endpoint.json'
$script:packageRoot = $null
$script:packageInfo = $null
$script:logRoot = $null
$script:logMutex = $null
$script:applyingSettings = $false
$script:manualAddress = ''
$script:adapterKey = ''

$script:lastPackageIssue = ''
$addressModes = @('Loopback', 'Local', 'Manual')

$script:session = $null
$script:observed = @{}
$script:messages = @{}
$script:canceledStarts = @{}
$script:starting = $false
$script:clientStops = @{}
$script:closeAfterOperations = $false
$script:pendingMode = -1
$script:currentLive = @{ server = $false; a = $false; b = $false }
$script:serverReady = $false
$ids = @('server', 'a', 'b')
$labels = @{ server = '서버'; a = '클라이언트 A'; b = '클라이언트 B' }

$form = [Windows.Forms.Form]@{
    Text = 'CoopRPG | 검증 실행기'
    StartPosition = 'CenterScreen'
    AutoScaleMode = 'Dpi'
    Size = [Drawing.Size]::new(795, 632)
    MinimumSize = [Drawing.Size]::new(795, 600)
    Font = [Drawing.Font]::new('Malgun Gothic', 10)
}
$viewport = [Windows.Forms.Panel]@{ Dock = 'Fill'; AutoScroll = $true }
$layout = [Windows.Forms.TableLayoutPanel]@{
    Dock = 'Top'; AutoSize = $true; AutoSizeMode = 'GrowAndShrink'
    ColumnCount = 1; RowCount = 4; Padding = [Windows.Forms.Padding]::new(12, 10, 12, 8)
}
[void]$layout.ColumnStyles.Add([Windows.Forms.ColumnStyle]::new('Percent', 100))
$settingsHeight = [Windows.Forms.RowStyle]::new('Absolute', 144)
[void]$layout.RowStyles.Add($settingsHeight)
[void]$layout.RowStyles.Add([Windows.Forms.RowStyle]::new('Percent', 100))
foreach ($section in 1..2) { [void]$layout.RowStyles.Add([Windows.Forms.RowStyle]::new('AutoSize')) }
$viewport.Controls.Add($layout)
$form.Controls.Add($viewport)

function New-LauncherLabel([string]$Text) {
    [Windows.Forms.Label]@{ Text = $Text; AutoSize = $true; Margin = [Windows.Forms.Padding]::new(4, 7, 4, 4) }
}

function New-LauncherButton([string]$Text, [string]$Name) {
    [Windows.Forms.Button]@{ Text = $Text; Name = $Name; AutoSize = $true }
}

$settings = [Windows.Forms.GroupBox]@{
    Text = '실행·접속 설정'; Dock = 'Fill'; Padding = [Windows.Forms.Padding]::new(8, 13, 8, 6)
}
$layout.Controls.Add($settings, 0, 0)

$settingsRows = [Windows.Forms.TableLayoutPanel]@{ Dock = 'Fill'; ColumnCount = 1; RowCount = 3 }
[void]$settingsRows.ColumnStyles.Add([Windows.Forms.ColumnStyle]::new('Percent', 100))
[void]$settingsRows.RowStyles.Add([Windows.Forms.RowStyle]::new('AutoSize'))
[void]$settingsRows.RowStyles.Add([Windows.Forms.RowStyle]::new('Absolute', 9))
[void]$settingsRows.RowStyles.Add([Windows.Forms.RowStyle]::new('AutoSize'))
$settings.Controls.Add($settingsRows)

$settingsGrid = [Windows.Forms.TableLayoutPanel]@{
    Dock = 'Fill'; AutoSize = $true; AutoSizeMode = 'GrowAndShrink'
    ColumnCount = 11; RowCount = 2; Margin = [Windows.Forms.Padding]::new(0)
}
for ($column = 0; $column -lt $settingsGrid.ColumnCount; $column++) {
    $style = switch ($column) {
        0 { [Windows.Forms.ColumnStyle]::new('Absolute', 63) }
        6 { [Windows.Forms.ColumnStyle]::new('Percent', 100) }
        9 { [Windows.Forms.ColumnStyle]::new('Absolute', 5) }
        10 { [Windows.Forms.ColumnStyle]::new('Absolute', 86) }
        default { [Windows.Forms.ColumnStyle]::new('AutoSize') }
    }
    [void]$settingsGrid.ColumnStyles.Add($style)
}
foreach ($row in 1..2) { [void]$settingsGrid.RowStyles.Add([Windows.Forms.RowStyle]::new('AutoSize')) }
$settingsRows.Controls.Add($settingsGrid, 0, 0)
$mode = [Windows.Forms.ComboBox]@{ Name = 'Mode'; DropDownStyle = 'DropDownList'; Width = 113 }
$mode.Items.AddRange(@('로컬 2인', 'Tailscale 호스트', '접속 전용'))
$mode.SelectedIndex = 0
$target = [Windows.Forms.TextBox]@{ Name = 'TargetAddress'; AccessibleName = '서버 IPv4'; Width = 114; Text = $Address }
$addressChoice = [Windows.Forms.ComboBox]@{ Name = 'AddressChoice'; AccessibleName = 'IPv4 주소 선택'; DropDownStyle = 'DropDownList'; Width = 102 }
$addressChoice.Items.AddRange(@('127.0.0.1', '내 PC IPv4', '직접 입력'))
$addressChoice.SelectedIndex = 0

$targetPort = [Windows.Forms.NumericUpDown]@{
    Name = 'TargetPort'; AccessibleName = 'UDP 포트'; BorderStyle = 'None'
}
$targetPort.Maximum = 65535
$targetPort.Minimum = 1024
$targetPort.Value = $Port
$portCell = [Windows.Forms.Panel]@{
    Width = 64; Height = $mode.Height; Anchor = 'Left'
    BackColor = [Drawing.SystemColors]::Window
    Margin = [Windows.Forms.Padding]::new(3)
    Padding = [Windows.Forms.Padding]::new(5, 0, 2, 0)
}
$portCell.Add_Paint({
    param($sender, $eventArgs)
    [Windows.Forms.ControlPaint]::DrawBorder($eventArgs.Graphics, $sender.ClientRectangle,
        [Drawing.SystemColors]::ControlDark, [Windows.Forms.ButtonBorderStyle]::Solid)
})
$portCell.Controls.Add($targetPort)
$startMode = New-LauncherButton '실행' 'StartMode'
$separatorColor = [Drawing.Color]::FromArgb(204, 209, 214)
$modeSeparator = [Windows.Forms.Panel]@{
    Width = 1; Height = 20; Anchor = 'None'; Margin = [Windows.Forms.Padding]::new(8, 3, 8, 3); BackColor = $separatorColor
}
$portSeparator = [Windows.Forms.Panel]@{
    Width = 1; Height = 20; Anchor = 'Right'; Margin = [Windows.Forms.Padding]::new(8, 3, 8, 3); BackColor = $separatorColor
}
$packageSeparator = [Windows.Forms.Panel]@{
    Height = 1; Anchor = 'Left, Right'; Margin = [Windows.Forms.Padding]::new(3, 0, 3, 0); BackColor = $separatorColor
}
function New-LauncherComboCell([Windows.Forms.ComboBox]$ComboBox) {
    $cell = [Windows.Forms.Panel]@{
        Width = $ComboBox.Width; Height = $ComboBox.Height; Anchor = 'Left'
        Margin = [Windows.Forms.Padding]::new(3)
    }
    $ComboBox.Dock = 'Top'
    $cell.Controls.Add($ComboBox)
    $cell
}

$modeCell = New-LauncherComboCell $mode
$addressCell = New-LauncherComboCell $addressChoice
$settingsControls = [ordered]@{
    0 = New-LauncherLabel '모드'; 1 = $modeCell; 2 = $modeSeparator
    3 = New-LauncherLabel '서버 IPv4'; 4 = $target; 5 = $addressCell; 6 = $portSeparator
    7 = New-LauncherLabel 'UDP 포트'; 8 = $portCell; 10 = $startMode
}
foreach ($settingCell in $settingsControls.GetEnumerator()) {
    $control = $settingCell.Value
    if ($control -isnot [Windows.Forms.Panel]) {
        $control.Margin = [Windows.Forms.Padding]::new(3)
        $control.Anchor = 'Left'
    }
    $settingsGrid.Controls.Add($control, $settingCell.Key, 0)
}
$startMode.AutoSize = $false
$startMode.Anchor = 'Top, Bottom, Left, Right'
$startMode.TextAlign = 'MiddleCenter'
$settingsGrid.SetRowSpan($startMode, 2)
$serverMapLabel = New-LauncherLabel '서버 맵'
$serverMapLabel.Anchor = 'Left'
$serverMapLabel.Margin = [Windows.Forms.Padding]::new(3)
$serverMap = [Windows.Forms.ComboBox]@{
    Name = 'ServerMap'; AccessibleName = '서버 맵'; DropDownStyle = 'DropDownList'; Enabled = $false
}
$serverMapCell = New-LauncherComboCell $serverMap
$serverMapCell.Anchor = 'Left, Right'
$settingsGrid.Controls.Add($serverMapLabel, 0, 1)
$settingsGrid.Controls.Add($serverMapCell, 1, 1)
$settingsGrid.SetColumnSpan($serverMapCell, 8)
$settingsRows.Controls.Add($packageSeparator, 0, 1)

$packageRow = [Windows.Forms.TableLayoutPanel]@{
    Dock = 'Fill'; AutoSize = $true; AutoSizeMode = 'GrowAndShrink'
    ColumnCount = 3; RowCount = 1; Margin = [Windows.Forms.Padding]::new(0)
}
[void]$packageRow.ColumnStyles.Add([Windows.Forms.ColumnStyle]::new('Absolute', 63))
[void]$packageRow.ColumnStyles.Add([Windows.Forms.ColumnStyle]::new('Percent', 100))
[void]$packageRow.ColumnStyles.Add([Windows.Forms.ColumnStyle]::new('Absolute', 86))
$settingsRows.Controls.Add($packageRow, 0, 2)
$packageLabel = New-LauncherLabel '패키지'
$packageLabel.Anchor = 'Left'
$packageLabel.Margin = [Windows.Forms.Padding]::new(3)
$packageText = [Windows.Forms.TextBox]@{
    Name = 'PackagePath'; AccessibleName = '게임 패키지 경로'; ReadOnly = $true; Anchor = 'Left, Right'
    Text = '패키지를 선택하세요.'; Margin = [Windows.Forms.Padding]::new(3)
}
$browsePackage = New-LauncherButton '찾아보기' 'BrowsePackage'
$browsePackage.Anchor = 'Left, Right'
$browsePackage.AutoSize = $false
$browsePackage.Height = 28
$packageRow.Controls.Add($packageLabel, 0, 0)
$packageRow.Controls.Add($packageText, 1, 0)
$packageRow.Controls.Add($browsePackage, 2, 0)

function Resize-LauncherSettings {
    $modeCell.Height = $mode.Height
    $addressCell.Height = $addressChoice.Height
    $serverMapCell.Height = $serverMap.Height
    $portCell.Height = $mode.Height
    $portTop = [int][Math]::Floor(($portCell.ClientSize.Height - $targetPort.Height) / 2)
    $targetPort.SetBounds($portCell.Padding.Left, $portTop, ($portCell.ClientSize.Width - $portCell.Padding.Horizontal), $targetPort.Height)
    $height = $settingsRows.PreferredSize.Height + $settings.DisplayRectangle.Top +
        $settings.Padding.Bottom + $settings.Margin.Vertical
    if ($settingsHeight.Height -ne $height) { $settingsHeight.Height = $height }
    $settingsGrid.PerformLayout()
}

$rosterGroup = [Windows.Forms.GroupBox]@{
    Text = '서버 참가자 (정원 2명)'; Dock = 'Fill'; AutoSize = $true; AutoSizeMode = 'GrowAndShrink'
    Padding = [Windows.Forms.Padding]::new(8, 13, 8, 4)
}
$rosterGrid = [Windows.Forms.TableLayoutPanel]@{
    Dock = 'Top'; ColumnCount = 4; RowCount = 4; AutoSize = $true; AutoSizeMode = 'GrowAndShrink'
}
foreach ($width in @(14, 32, 27, 27)) {
    [void]$rosterGrid.ColumnStyles.Add([Windows.Forms.ColumnStyle]::new('Percent', $width))
}
$headers = @('슬롯', '클라이언트', '접속', '준비')
for ($column = 0; $column -lt $headers.Count; $column++) {
    $headerLabel = New-LauncherLabel $headers[$column]
    $headerLabel.Margin = [Windows.Forms.Padding]::new(4, 4, 4, 2)
    $headerLabel.ForeColor = [Drawing.Color]::FromArgb(66, 78, 92)
    $rosterGrid.Controls.Add($headerLabel, $column, 0)
}
$rosterRows = @(
    foreach ($slot in 1..2) {
        $slotLabel = New-LauncherLabel "P$slot"
        $slotLabel.Margin = [Windows.Forms.Padding]::new(4, 4, 4, 2)
        $rosterGrid.Controls.Add($slotLabel, 0, $slot)
        $cells = @(foreach ($column in 1..3) {
            $cell = New-LauncherLabel '—'
            $cell.Margin = [Windows.Forms.Padding]::new(4, 4, 4, 2)
            $rosterGrid.Controls.Add($cell, $column, $slot)
            $cell
        })
        @{ Name = $cells[0]; Connection = $cells[1]; Readiness = $cells[2] }
    }
)
$rosterNote = New-LauncherLabel ''
$rosterNote.ForeColor = [Drawing.Color]::FromArgb(66, 78, 92)
$rosterGrid.Controls.Add($rosterNote, 0, 3)
$rosterGrid.SetColumnSpan($rosterNote, 4)
$rosterGroup.Controls.Add($rosterGrid)
$layout.Controls.Add($rosterGroup, 0, 2)

$processGroup = [Windows.Forms.GroupBox]@{
    Text = '이 PC의 프로세스'; Dock = 'Fill'; AutoSize = $true; AutoSizeMode = 'GrowAndShrink'
    Padding = [Windows.Forms.Padding]::new(8, 13, 8, 5)
}
$rows = [Windows.Forms.TableLayoutPanel]@{
    Dock = 'Fill'; AutoSize = $true; AutoSizeMode = 'GrowAndShrink'; ColumnCount = 4; RowCount = 3
}
[void]$rows.ColumnStyles.Add([Windows.Forms.ColumnStyle]::new('Absolute', 125))
[void]$rows.ColumnStyles.Add([Windows.Forms.ColumnStyle]::new('Percent', 100))
[void]$rows.ColumnStyles.Add([Windows.Forms.ColumnStyle]::new('AutoSize'))
[void]$rows.ColumnStyles.Add([Windows.Forms.ColumnStyle]::new('AutoSize'))
$processGroup.Controls.Add($rows)
$layout.Controls.Add($processGroup, 0, 1)

$processStatusFont = [Drawing.Font]::new($form.Font, [Drawing.FontStyle]::Bold)
$processMetadataColor = [Drawing.Color]::FromArgb(66, 78, 92)
$script:processLayoutWidth = 0
$rowControls = @{}
foreach ($id in $ids) {
    [void]$rows.RowStyles.Add([Windows.Forms.RowStyle]::new('Percent', 100.0 / 3))
    $index = $rows.RowStyles.Count - 1
    $roleLabel = New-LauncherLabel $labels[$id]
    $roleLabel.Margin = [Windows.Forms.Padding]::new(4)
    $details = [Windows.Forms.TableLayoutPanel]@{
        AutoSize = $true; AutoSizeMode = 'GrowAndShrink'; ColumnCount = 1; RowCount = 3
        Anchor = 'Left, Right'; Margin = [Windows.Forms.Padding]::new(4, 2, 4, 2)

    }
    [void]$details.ColumnStyles.Add([Windows.Forms.ColumnStyle]::new('Percent', 100))
    $statusLabel = [Windows.Forms.Label]@{
        Name = "Status_$id"; Text = '이 PC에서 미실행'; AutoSize = $true; Font = $processStatusFont
        Margin = [Windows.Forms.Padding]::new(0)
    }
    $identityLabel = [Windows.Forms.Label]@{
        Name = "Identity_$id"; Text = '—'; AutoSize = $true; ForeColor = $processMetadataColor
        Margin = [Windows.Forms.Padding]::new(0)
    }
    $reasonLabel = [Windows.Forms.Label]@{
        Name = "Reason_$id"; AutoSize = $true; ForeColor = [Drawing.Color]::Firebrick
        Margin = [Windows.Forms.Padding]::new(0, 2, 0, 0)
    }
    $details.Controls.Add($statusLabel, 0, 0)
    $details.Controls.Add($identityLabel, 0, 1)
    $details.Controls.Add($reasonLabel, 0, 2)
    $details.Add_ClientSizeChanged({
        param($sender, $eventArgs)
        $width = [Math]::Max(1, $sender.ClientSize.Width)
        foreach ($line in $sender.Controls) {
            if ($line.MaximumSize.Width -ne $width) { $line.MaximumSize = [Drawing.Size]::new($width, 0) }
        }
    })
    $actionText = if ($id -eq 'server') { '시작' } else { '접속' }
    $start = New-LauncherButton $actionText "Start_$id"
    $stop = New-LauncherButton '종료' "Stop_$id"
    $start.AccessibleName = "$($labels[$id]) $actionText"
    $stop.AccessibleName = "$($labels[$id]) 종료"
    $start.Tag = $id
    $stop.Tag = $id
    $start.Anchor = 'Left'
    $stop.Anchor = 'Left'
    $roleLabel.Anchor = 'Left'
    $rows.Controls.Add($roleLabel, 0, $index)
    $rows.Controls.Add($details, 1, $index)
    $rows.Controls.Add($start, 2, $index)
    $rows.Controls.Add($stop, 3, $index)
    $rowControls[$id] = @{ Details = $details; Status = $statusLabel; Identity = $identityLabel
        Reason = $reasonLabel; Role = $roleLabel; Start = $start; Stop = $stop }
}

function Resize-LauncherProcessRows {
    # WinForms distributes percent rows by relative weight. Using content heights
    # keeps a warning in one row from increasing the minimum height of every row.
    for ($index = 0; $index -lt $ids.Count; $index++) {
        $details = $rowControls[$ids[$index]].Details
        $row = $rowControls[$ids[$index]]
        $twoLineHeight = $row.Status.PreferredSize.Height + $row.Identity.PreferredSize.Height
        $height = [Math]::Max($details.PreferredSize.Height, $twoLineHeight) + $details.Margin.Vertical
        $style = $rows.RowStyles[$index]
        if ($style.Height -ne $height) { $style.Height = $height }
    }
}

$bottom = [Windows.Forms.FlowLayoutPanel]@{ Dock = 'Fill'; AutoSize = $true; WrapContents = $true }
$openLogs = New-LauncherButton '이번 로그 보기' 'OpenLogs'
$openSaved = New-LauncherButton '기존 로그 열기' 'OpenSaved'
$openFolder = New-LauncherButton '로그 폴더' 'OpenFolder'
$notice = New-LauncherLabel ''
$notice.ForeColor = [Drawing.Color]::Firebrick
$notice.Visible = $false
$notice.Add_TextChanged({ $notice.Visible = [bool]$notice.Text })
$notice.MaximumSize = [Drawing.Size]::new(660, 0)
$endpointAction = New-LauncherButton '주소 복사' 'EndpointAction'
$endpointAction.Margin = [Windows.Forms.Padding]::new(9, 3, 3, 3)
$closeHint = New-LauncherLabel '창을 닫아도 서버·클라 실행은 유지됩니다.'
$bottom.Controls.AddRange(@($openLogs, $openSaved, $openFolder, $endpointAction, $closeHint, $notice))
$bottom.SetFlowBreak($closeHint, $true)
$layout.Controls.Add($bottom, 0, 3)
$tooltip = [Windows.Forms.ToolTip]::new()
$tooltip.SetToolTip($mode, '로컬 2인은 서버·A·B, Tailscale 호스트는 서버·A, 접속 전용은 선택한 클라이언트를 실행합니다.')
$tooltip.SetToolTip($addressChoice, '127.0.0.1은 이 PC 안에서 사용합니다. 내 PC IPv4는 자동 선택하며, 접속할 서버 주소는 직접 입력할 수 있습니다.')
$tooltip.SetToolTip($targetPort, '기본 게임 포트는 UDP 7777입니다. 서버와 두 클라이언트는 같은 번호를 사용합니다.')
$tooltip.SetToolTip($serverMap, '서버를 시작할 맵입니다. 접속 전용에서는 서버가 연 맵에 들어갑니다.')

function Get-LauncherPackageIssue([string]$Stage) {
    if (!$Stage) { return '게임 패키지를 선택하세요.' }
    if ($mode.SelectedIndex -ne 2 -and !(Test-LauncherExecutable $Stage 'Server')) {
        return '이 모드에는 패키지의 WindowsServer 실행 파일이 필요합니다.'
    }
    if (!(Test-LauncherExecutable $Stage 'Client')) {
        return '패키지의 WindowsClient 실행 파일을 찾을 수 없습니다.'
    }
}

function Set-LauncherPackageDisplay([string]$Path) {
    $packageText.Text = $Path
    $packageText.Select(0, 0)
    $packageText.AccessibleDescription = $Path
    $tooltip.SetToolTip($packageText, $Path)
}


function Update-LauncherAddressInput {
    $wasApplying = $script:applyingSettings
    $script:applyingSettings = $true
    try {
        $script:adapterKey = ''
        switch ($addressChoice.SelectedIndex) {
            0 { $target.Text = [ipaddress]::Loopback.ToString() }
            1 {
                $addresses = @(Get-CoopLauncherLocalAddresses)
                $selected = $null
                if ($mode.SelectedIndex -eq 1) {
                    $tailscale = @($addresses | Where-Object IsTailscale)
                    if ($tailscale.Count -eq 1) { $selected = $tailscale[0] }
                }
                if (!$selected -and $addresses.Count -eq 1) { $selected = $addresses[0] }
                $target.Text = if ($selected) { $selected.Address.ToString() } else { '' }
                $script:adapterKey = if ($selected) { $selected.Key } else { '' }
            }
            2 { $target.Text = $script:manualAddress }
        }

    } finally {
        $script:applyingSettings = $wasApplying
    }
}

function Restore-LauncherRemoteEndpoint([switch]$KeepAddress, [switch]$KeepPort) {
    $endpoint = Read-CoopLauncherRemoteEndpoint $remoteEndpointPath
    if (!$endpoint) { return }
    if (!$KeepAddress) {
        $script:manualAddress = $endpoint.address
        $addressChoice.SelectedIndex = 2
    }
    if (!$KeepPort) { $targetPort.Value = $endpoint.port }
}

function Apply-LauncherRemoteEndpoint([string]$Text) {
    $endpoint = ConvertFrom-CoopLauncherEndpoint $Text
    if (!$endpoint) { throw '서버 주소를 IPv4:UDP 포트 형식으로 복사해 붙여넣으세요.' }
    $script:applyingSettings = $true
    try {
        $script:manualAddress = $endpoint.address
        $addressChoice.SelectedIndex = 2
        $targetPort.Value = $endpoint.port
        Update-LauncherAddressInput
    } finally { $script:applyingSettings = $false }
    Save-CoopLauncherSettings $remoteEndpointPath $endpoint
}

function Save-LauncherPreferences {
    if (!$script:packageRoot) { return }
    Save-CoopLauncherSettings $settingsPath ([ordered]@{
        packageRoot = $script:packageRoot; mode = $mode.SelectedIndex
        addressMode = $addressModes[$addressChoice.SelectedIndex]
        manualAddress = $script:manualAddress; port = [int]$targetPort.Value
        serverMap = [string]$serverMap.SelectedItem
    })
}

function Get-LiveLauncherSources($Session, [string]$Stage) {
    if (!$Session -or !$Stage) { return }
    foreach ($source in $Session.Manifest.sources) {
        $process = Get-CoopLauncherProcess $source $Stage
        if ($process) {
            $process.Dispose()
            $source.id
        }
    }
}

function Set-LauncherPackage([string]$Directory) {
    $stage = Find-LauncherStage $Directory
    if (!$stage) { throw '게임 실행 파일을 찾을 수 없습니다. 게임 패키지 폴더 또는 Stage를 골라 주세요.' }
    if ($script:packageRoot -and $stage -eq $script:packageRoot) { return }
    if (@(Get-LiveLauncherSources $script:session $script:packageRoot).Count -ne 0) {
        throw '이 회차의 서버·클라이언트를 종료하면 패키지를 바꿀 수 있습니다.'
    }

    $nextLogRoot = if ($logOverride) { $logOverride } else { Join-Path (Split-Path -Parent $stage) 'Logs' }
    $nextLogRoot = [IO.Path]::GetFullPath($nextLogRoot).TrimEnd('\', '/')
    $nextMutex = if ($nextLogRoot -eq $script:logRoot) { $script:logMutex } else { Lock-CoopLauncherLogRoot $nextLogRoot }
    try {
        $nextSession = Read-CoopLauncherSession $nextLogRoot
        if ($nextSession -and $nextSession.Manifest.PSObject.Properties['packageRoot'] -and
            $nextSession.Manifest.packageRoot -and $nextSession.Manifest.packageRoot -ne $stage) {
            if (@(Get-LiveLauncherSources $nextSession $nextSession.Manifest.packageRoot).Count -ne 0) {
                throw '이 로그 폴더에는 다른 패키지의 회차가 실행 중입니다.'
            }
            $nextSession = $null
        }
        $liveSources = @(Get-LiveLauncherSources $nextSession $stage)
    } catch {
        if ($nextMutex -ne $script:logMutex) {
            $nextMutex.ReleaseMutex()
            $nextMutex.Dispose()
        }
        throw
    }

    if ($script:logMutex -and $nextMutex -ne $script:logMutex) {
        $script:logMutex.ReleaseMutex()
        $script:logMutex.Dispose()
    }
    $script:logMutex = $nextMutex
    $script:packageRoot = $stage
    $script:logRoot = $nextLogRoot
    $script:session = $nextSession
    $script:observed = @{}
    $script:messages.Clear()
    $script:canceledStarts.Clear()
    foreach ($id in $ids) { $script:currentLive[$id] = $id -in $liveSources }
    Set-LauncherPackageDisplay $stage
    $script:packageInfo = Get-CoopLauncherPackageInfo $stage
    $serverMap.Items.Clear()
    $serverMap.Items.AddRange([object[]]$script:packageInfo.ServerMaps)
    $serverMap.SelectedItem = $script:packageInfo.DefaultServerMap
    $build = if ($script:packageInfo.BuildId) { $script:packageInfo.BuildId } else { '빌드 미확인' }
    $form.Text = "CoopRPG | 검증 실행기 · $build"

    if ($liveSources.Count -ne 0) {
        $manifest = $nextSession.Manifest
        $script:applyingSettings = $true
        try {
            $mode.SelectedIndex = [int]$manifest.mode
            $targetPort.Value = [int]$manifest.port
            $addressChoice.SelectedIndex = if ($manifest.PSObject.Properties['addressMode'] -and $manifest.addressMode -in $addressModes) {
                [Array]::IndexOf($addressModes, [string]$manifest.addressMode)
            } elseif ($manifest.address -eq '127.0.0.1') { 0 } else { 2 }
            $target.Text = [string]$manifest.address
            if ($addressChoice.SelectedIndex -eq 2) { $script:manualAddress = $target.Text }
            if ($manifest.PSObject.Properties['adapterKey']) { $script:adapterKey = [string]$manifest.adapterKey }
            $sessionMap = if ($manifest.PSObject.Properties['serverMap']) { $manifest.serverMap } else { 'L_Movement' }
            if ($serverMap.Items.Contains($sessionMap)) { $serverMap.SelectedItem = $sessionMap }

        } finally {
            $script:applyingSettings = $false
        }
    }
}

function Initialize-LauncherSettings {
    $saved = $null
    try { $saved = Read-CoopLauncherSettings $settingsPath } catch {
        $notice.Text = '저장된 설정을 읽을 수 없습니다. 실행 설정을 다시 선택하세요.'
    }
    $script:applyingSettings = $true
    try {
        if ($saved) {
            if ($saved.PSObject.Properties['mode'] -and $saved.mode -in 0..2) { $mode.SelectedIndex = [int]$saved.mode }
            if ($saved.PSObject.Properties['addressMode'] -and $saved.addressMode -in $addressModes) {
                $addressChoice.SelectedIndex = [Array]::IndexOf($addressModes, [string]$saved.addressMode)
            } else { $addressChoice.SelectedIndex = $mode.SelectedIndex }
            if ($saved.PSObject.Properties['manualAddress']) { $script:manualAddress = [string]$saved.manualAddress }

            if (!$explicitPort -and $saved.PSObject.Properties['port'] -and $saved.port -ge 1024 -and $saved.port -le 65535) {
                $targetPort.Value = [int]$saved.port
            }
        }
        if ($mode.SelectedIndex -eq 2) {
            Restore-LauncherRemoteEndpoint -KeepAddress:$explicitAddress -KeepPort:$explicitPort
        }
        if ($explicitAddress) {
            if ($Address -eq '127.0.0.1') {
                $addressChoice.SelectedIndex = 0
            } else {
                $script:manualAddress = $Address
                if (!$saved) { $mode.SelectedIndex = 1 }
                $addressChoice.SelectedIndex = 2
            }
        }
        Update-LauncherAddressInput
    } finally {
        $script:applyingSettings = $false
    }

    $directory = $requestedPackage
    if (!$directory -and $saved -and $saved.PSObject.Properties['packageRoot']) {
        $directory = [string]$saved.packageRoot
    }
    if (!$directory) {
        $legacy = [IO.Path]::ChangeExtension($settingsPath, '.txt')
        if (Test-Path -LiteralPath $legacy -PathType Leaf) { $directory = [IO.File]::ReadAllText($legacy).Trim() }
    }
    if (!$directory) { $directory = Find-LauncherStage $PSScriptRoot }
    if (!$directory) { $directory = Find-LauncherStage (Split-Path -Parent $PSScriptRoot) }
    if ($directory) {
        try {
            Set-LauncherPackage $directory
            if ($script:packageRoot -and !$script:currentLive.ContainsValue($true) -and $saved -and
                $saved.PSObject.Properties['packageRoot'] -and $saved.packageRoot -eq $script:packageRoot -and
                $saved.PSObject.Properties['serverMap'] -and $serverMap.Items.Contains($saved.serverMap)) {
                $serverMap.SelectedItem = $saved.serverMap
            }
        } catch {
            Set-LauncherPackageDisplay $directory
            $notice.Text = $_.Exception.Message
        }
    }
}

function Get-LauncherAddress {
    if ($addressChoice.SelectedIndex -eq 1 -and !$target.Text) {
        throw '내 PC IPv4를 자동으로 정할 수 없습니다. 직접 입력에서 이 PC의 IPv4를 입력하세요.'
    }
    $ip = ConvertTo-CoopLauncherIPv4 $target.Text
    if (!$ip) {
        throw '서버 IPv4 주소를 입력하세요.'
    }
    $ip
}

function Get-LauncherStartMessage([string]$Reason) {
    switch -Wildcard ($Reason) {
        'Game executable not found:*' { '게임 실행 파일을 찾지 못했습니다. 패키지의 Stage 폴더를 확인하세요.'; break }
        'The address * does not belong*' { '이 PC의 활성 네트워크에 입력한 서버 주소가 없습니다.'; break }
        default { $reason }
    }
}

function Get-LauncherNativeStartError($ErrorRecord) {
    for ($cause = $ErrorRecord.Exception; $cause; $cause = $cause.InnerException) {
        if ($cause -is [ComponentModel.Win32Exception]) { return $cause }
    }
}

function Invoke-LauncherStart([string]$Id) {
    if ($script:starting -or $script:currentLive[$Id] -or $script:clientStops.ContainsKey($Id)) { return $false }

    $script:starting = $true
    $script:messages.Remove($Id)
    $script:canceledStarts.Remove($Id)
    $notice.Text = ''
    try {
        Invoke-LauncherRefresh
        if (!$script:currentLive.ContainsValue($true) -and $script:pendingMode -eq -1) {
            $issue = Get-LauncherPackageIssue $script:packageRoot
            if ($issue) { throw $issue }
            if ($addressChoice.SelectedIndex -eq 1) { Update-LauncherAddressInput }
            $address = Get-LauncherAddress
            try {
                Save-LauncherPreferences
                if ($mode.SelectedIndex -eq 2) {
                    Save-CoopLauncherSettings $remoteEndpointPath @{ address = $address.ToString(); port = [int]$targetPort.Value }
                }
            } catch { $notice.Text = "설정을 저장하지 못했습니다. $($_.Exception.Message)" }
            $selectedMap = if ($mode.SelectedIndex -ne 2) { [string]$serverMap.SelectedItem } else { '' }
            $script:session = New-CoopLauncherSession $script:logRoot $address ([int]$targetPort.Value) $mode.SelectedIndex `
                $script:packageRoot $addressModes[$addressChoice.SelectedIndex] $script:adapterKey -ServerMap $selectedMap
            $script:observed = @{}
            $script:messages.Clear()
            $script:canceledStarts.Clear()
        }
        $source = Start-CoopLauncherProcess $script:session $Id $script:packageRoot -ServerForceLogFlush:$ServerForceLogFlush
        $script:observed[$Id] = New-CoopLauncherObservation (Join-Path $script:session.Directory $source.file)
        $script:currentLive[$Id] = $true
        return $true
    } catch {
        $nativeError = Get-LauncherNativeStartError $_
        if ($nativeError -and $nativeError.NativeErrorCode -eq 1223) {
            $script:canceledStarts[$Id] = $true
        } else {
            $reason = if ($nativeError) { $nativeError.Message } else { $_.Exception.Message }
            $script:messages[$Id] = Get-LauncherStartMessage $reason
        }
        return $false
    } finally {
        $script:starting = $false
    }
}

function Update-LauncherClientStops {
    foreach ($id in @($script:clientStops.Keys)) {
        try {
            if (Update-CoopLauncherClientStop $script:session $script:clientStops[$id] $script:packageRoot) {
                $script:clientStops.Remove($id)
            }
        } catch {
            $script:clientStops.Remove($id)
            $script:messages[$id] = "종료 실패 · $($_.Exception.Message)"
        }
    }
}

function Update-LauncherRoster {
    $server = $script:observed['server']
    $participants = $null
    $capability = if ($script:packageInfo) { $script:packageInfo.Roster } else { 'Unknown' }
    $rosterGroup.Text = switch ($capability) {
        'Supported' { '서버 참가자 (정원 2명) · 계측 지원' }
        'Unsupported' { '서버 참가자 (정원 2명) · 계측 미지원' }
        default { '서버 참가자 (정원 2명)' }
    }
    $rosterNote.Text = if ($mode.SelectedIndex -eq 2) {
        '서버 참가자는 호스트 실행기에서 확인할 수 있습니다.'
    } elseif ($script:currentLive['server'] -and $server -and $server.Traveling) {
        '서버가 다음 맵으로 이동 중입니다.'
    } elseif ($script:currentLive['server'] -and $server -and $server.Roster) {
        $participants = $server.Roster
        "서버 로그 기준 · $($server.Stage)"
    } elseif (!$script:packageRoot) {
        '게임 패키지를 선택하세요.'
    } elseif ($capability -eq 'Mismatch') {
        '패키지의 빌드 정보와 실행 파일이 다릅니다. 계측 패키지를 확인하세요.'
    } elseif ($capability -eq 'Unsupported') {
        '선택한 게임 빌드는 참가자 로그를 기록하지 않습니다.'
    } elseif ($capability -eq 'Unknown') {
        '참가자 계측을 확인할 수 없습니다. 계측 패키지를 선택하세요.'
    } elseif (!$script:currentLive['server']) {
        '이 PC에서 서버를 시작하면 참가자가 표시됩니다.'
    } else {
        '이 서버의 참가자 로그를 기다리는 중입니다.'
    }

    for ($index = 0; $index -lt $rosterRows.Count; $index++) {
        $row = $rosterRows[$index]
        $participant = if ($participants) { $participants[$index] }
        $connected = $participant -and $participant.State -ne 'Disconnected'
        $row.Name.Text = if ($connected) {
            if ($participant.Label -eq 'None') { '표식 없음' } else { "클라이언트 $($participant.Label)" }
        } else { '—' }
        $tooltip.SetToolTip($row.Name, $(if ($connected -and $participant.PlayerId -ge 0) { "서버 PlayerId $($participant.PlayerId)" } else { "" }))
        $row.Connection.Text = if (!$participant) { '—' } elseif ($connected) { '접속 중' } else { '미접속' }
        $row.Connection.ForeColor = if ($connected) { [Drawing.Color]::DarkGreen } else { [Drawing.Color]::Black }
        $row.Readiness.Text = if ($connected) {
            switch ($participant.State) {
                'Joining' { '초기화 중' }
                'Waiting' { '준비 전' }
                'Ready' { '준비 완료' }
            }
        } else { '—' }
        $row.Readiness.ForeColor = if ($connected -and $participant.State -eq 'Ready') {
            [Drawing.Color]::DarkGreen
        } else { [Drawing.Color]::Black }
    }
}

function Update-LauncherView {
    $packageIssue = Get-LauncherPackageIssue $script:packageRoot
    $serverSource = Get-CoopLauncherSource $script:session 'server'
    foreach ($id in $ids) {
        $source = Get-CoopLauncherSource $script:session $id
        $isLive = $false
        if ($source) {
            $process = Get-CoopLauncherProcess $source $script:packageRoot
            $isLive = $null -ne $process
            if ($process) { $process.Dispose() }
            if (!$script:observed.ContainsKey($id)) {
                $script:observed[$id] = New-CoopLauncherObservation (Join-Path $script:session.Directory $source.file)
            }
            $observation = $script:observed[$id]
            Update-CoopLauncherObservation $observation $script:session.Manifest.port
        }
        $script:currentLive[$id] = $isLive
        $status = '이 PC에서 미실행'
        $identity = ''
        $diagnostic = ''
        $serverReady = $false
        $detail = if ($script:messages.ContainsKey($id)) { $script:messages[$id] } else { '' }
        $stopping = $script:clientStops.ContainsKey($id)
        if (!$isLive -and $script:canceledStarts.ContainsKey($id)) {
            $status = '실행 취소'
        } elseif ($source) {
            $endpoint = '{0}:{1}' -f $script:session.Manifest.address, $script:session.Manifest.port
            $startedLocal = [DateTime]::new([long]$source.startTicks, [DateTimeKind]::Utc).ToLocalTime().ToString('HH:mm:ss')
            $identity = "$endpoint  ·  PID $($source.processId)  ·  시작 $startedLocal"
            if ($observation.Stage) { $identity += "  ·  $($observation.Stage)" }
            if ($stopping) {
                $status = '종료 중'
            } elseif ($isLive -and $id -eq 'server') {
                $serverReady = $observation.Listening -and (Test-CoopLauncherPort ([ipaddress]$script:session.Manifest.address) $script:session.Manifest.port)
                $status = if ($serverReady) { '접속 대기' } elseif ($observation.Listening) { '포트 대기' } else { '시작 중' }
                $startAge = ([DateTime]::UtcNow.Ticks - [long]$source.startTicks) / [TimeSpan]::TicksPerSecond
                if (!$serverReady -and !$observation.Listening -and $startAge -gt 60) {
                    $status = '시작 지연'
                    if (!$detail) { $detail = '60초 안에 포트 수신을 확인하지 못했습니다. 로그를 확인하세요.' }
                }
            } elseif ($observation.Failure -and !$source.stoppedByLauncher) {
                $status = switch ($observation.Failure) {
                    'ConnectionEnded' { '연결 끊김' }
                    'ConnectionFailed' { '접속 실패' }
                    default { '접속 거절' }
                }
            } elseif ($isLive -and $serverSource -and !$script:currentLive['server'] -and $observation.Joined) {
                $status = '서버 종료 확인'
                if (!$detail) { $detail = '같은 회차의 서버 프로세스가 종료됐습니다.' }
            } elseif ($isLive) {
                $status = if ($observation.Joined) { '접속 완료' } else { '접속 중' }
            } elseif ($source.stoppedByLauncher -or $observation.Listening -or $observation.Joined) {
                $status = '종료'
            } else {
                $status = '시작 실패'
                if (!$detail) { $detail = '준비를 확인하기 전에 프로세스가 종료됐습니다.' }
            }
            if (!$detail -and $observation.Message) { $detail = $observation.Message }
            $diagnostic = $observation.Diagnostic
            if (!$detail -and $diagnostic) { $detail = '네트워크 경고가 기록됐습니다. 이번 로그에서 확인하세요.' }
        } elseif ($detail) {
            $status = '시작 실패'
        } elseif ($id -eq 'server' -and $mode.SelectedIndex -ne 2) {
            $selectedAddress = $null
            [void][ipaddress]::TryParse($target.Text, [ref]$selectedAddress)
            if ($selectedAddress -and (Test-CoopLauncherPort $selectedAddress ([int]$targetPort.Value))) {
                $status = '포트 사용 중'
                $detail = '현재 포트는 실행기에서 관리 중인 서버와 연결되지 않았습니다.'
            }
        }
        if ($id -eq 'server') { $script:serverReady = $serverReady }
        $controls = $rowControls[$id]
        $controls.Status.Text = $status
        $controls.Identity.Text = if ($identity) { $identity } else { '—' }
        $controls.Identity.Visible = [bool]$identity
        $controls.Reason.Text = $detail
        $controls.Reason.Visible = [bool]$detail
        $controls.Status.ForeColor = if ($status -in @('시작 실패', '시작 지연', '포트 사용 중', '서버 종료 확인', '접속 실패', '접속 거절', '연결 끊김')) {
            [Drawing.Color]::Firebrick
        } elseif ($status -in @('접속 대기', '접속 완료')) {
            [Drawing.Color]::DarkGreen
        } elseif ($status -in @('이 PC에서 미실행', '실행 취소')) {
            $processMetadataColor
        } else { [Drawing.Color]::Black }
        $tooltip.SetToolTip($controls.Reason, $diagnostic)
        $modeAllows = switch ($mode.SelectedIndex) {
            0 { $id -eq 'server' -or $script:serverReady }
            1 { $id -eq 'server' -or ($id -eq 'a' -and $script:serverReady) }
            2 { $id -ne 'server' }
        }
        if ($id -eq 'server' -and ($script:currentLive['a'] -or $script:currentLive['b'])) { $modeAllows = $false }
        $controls.Start.Enabled = !$script:starting -and !$stopping -and !$isLive -and
            $modeAllows -and $script:pendingMode -eq -1 -and !$packageIssue
        $controls.Stop.Enabled = !$script:starting -and !$stopping -and $isLive
    }
    Update-LauncherRoster
    $running = $script:starting -or $script:clientStops.Count -gt 0 -or $script:currentLive.ContainsValue($true)
    $mode.Enabled = !$running
    $portCell.Enabled = !$running
    $portCell.BackColor = if ($running) { [Drawing.SystemColors]::Control } else { [Drawing.SystemColors]::Window }
    $serverMap.Enabled = !$running -and $mode.SelectedIndex -ne 2 -and $serverMap.Items.Count -gt 1
    $addressChoice.Enabled = !$running
    $target.Enabled = !$running -and $addressChoice.SelectedIndex -eq 2

    $browsePackage.Enabled = !$running -and $script:pendingMode -eq -1
    $endpointAction.Text = if ($mode.SelectedIndex -eq 2) { '주소 붙여넣기' } else { '주소 복사' }
    $endpointAction.Enabled = ($mode.SelectedIndex -eq 1 -and (ConvertTo-CoopLauncherIPv4 $target.Text)) -or
        ($mode.SelectedIndex -eq 2 -and !$running)
    if ($notice.Text -eq $script:lastPackageIssue) { $notice.Text = '' }
    if ($packageIssue -and !$notice.Text) { $notice.Text = $packageIssue }
    $script:lastPackageIssue = $packageIssue
    $startMode.Enabled = !$running -and $mode.SelectedIndex -ne 2 -and !$packageIssue
    $openLogs.Enabled = $null -ne $script:session -and $script:session.Manifest.sources.Count -gt 0
    $openFolder.Enabled = $script:logRoot -and (Test-Path -LiteralPath $script:logRoot -PathType Container)
    Resize-LauncherSettings
    Resize-LauncherProcessRows
}

function Invoke-LauncherRefresh {
    try {
        Update-LauncherClientStops
        Update-LauncherView
    } catch { $notice.Text = "상태 확인 실패 · $($_.Exception.Message)" }
}

function Open-LauncherViewer([string]$Directory) {
    $viewer = Join-Path $PSScriptRoot 'Watch-CombatManual.ps1'
    $viewerSettings = Join-Path $(if ($script:logRoot) { $script:logRoot } else { $Directory }) 'viewer-settings.json'
    $arguments = '-NoProfile -STA -ExecutionPolicy Bypass -File "{0}" -LogDirectory "{1}" -SettingsPath "{2}"' -f $viewer, $Directory.TrimEnd('\','/'), $viewerSettings
    Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList $arguments -WindowStyle Hidden
}

try {
    Initialize-LauncherSettings

    foreach ($id in $ids) {
        $rowControls[$id].Start.Add_Click({
            param($sender, $eventArgs)
            [void](Invoke-LauncherStart $sender.Tag)
            Invoke-LauncherRefresh
        })
        $rowControls[$id].Stop.Add_Click({
            param($sender, $eventArgs)
            $id = $sender.Tag
            if ($script:clientStops.ContainsKey($id)) { return }
            try {
                $source = Get-CoopLauncherSource $script:session $id
                $script:messages.Remove($id)
                if ($id -eq 'server') {
                    Stop-CoopLauncherProcess $script:session $source $script:packageRoot
                    $script:pendingMode = -1
                } elseif ($source) {
                    Write-CoopLauncherClientStop $script:session $source 'ClientStopClicked'
                    $script:clientStops[$id] = @{
                        Source = $source; Elapsed = [Diagnostics.Stopwatch]::StartNew()
                        CloseSent = $false; ForceSent = $false
                    }
                }
            } catch {
                $script:messages[$id] = "종료 실패 · $($_.Exception.Message)"
            }
            Invoke-LauncherRefresh
        })
    }
    $mode.Add_SelectedIndexChanged({
        if ($script:applyingSettings) { return }
        $script:applyingSettings = $true
        try {
            $addressChoice.SelectedIndex = $mode.SelectedIndex
            if ($mode.SelectedIndex -eq 2) { Restore-LauncherRemoteEndpoint }
            Update-LauncherAddressInput
        } finally { $script:applyingSettings = $false }
        Invoke-LauncherRefresh
    })
    $addressChoice.Add_SelectedIndexChanged({
        if ($script:applyingSettings) { return }
        Update-LauncherAddressInput
        Invoke-LauncherRefresh
    })
    $target.Add_TextChanged({
        if (!$script:applyingSettings -and $addressChoice.SelectedIndex -eq 2) { $script:manualAddress = $target.Text }
    })
    $browsePackage.Add_Click({
        $picker = [Windows.Forms.FolderBrowserDialog]::new()
        try {
            $picker.Description = '게임 패키지 폴더 또는 그 안의 Stage를 선택하세요.'
            $picker.SelectedPath = if ($script:packageRoot) { Split-Path -Parent $script:packageRoot }
                else { [Environment]::GetFolderPath('DesktopDirectory') }
            $picker.ShowNewFolderButton = $false
            if ($picker.ShowDialog($form) -ne 'OK') { return }
            $stage = Find-LauncherStage $picker.SelectedPath
            $issue = Get-LauncherPackageIssue $stage
            if ($issue) { throw $issue }
            Set-LauncherPackage $stage
            $notice.Text = ''
            Save-LauncherPreferences
        } catch {
            $notice.Text = $_.Exception.Message
        } finally {
            $picker.Dispose()
            Invoke-LauncherRefresh
        }
    })
    $endpointAction.Add_Click({
        try {
            if ($mode.SelectedIndex -eq 2) {
                Apply-LauncherRemoteEndpoint ([Windows.Forms.Clipboard]::GetText())
                $message = '접속 주소와 포트를 적용했습니다.'
            } else {
                [Windows.Forms.Clipboard]::SetText(('{0}:{1}' -f (Get-LauncherAddress), [int]$targetPort.Value))
                $message = '서버 주소와 포트를 복사했습니다.'
            }
            $notice.Text = ''
            $tooltip.Show($message, $endpointAction, 0, -24, 2200)
        } catch {
            $notice.Text = $_.Exception.Message
        }
        Invoke-LauncherRefresh
    })
    $startMode.Add_Click({
        if (Invoke-LauncherStart 'server') {
            $script:pendingMode = $mode.SelectedIndex
            $script:startupDeadline = [DateTime]::UtcNow.AddSeconds(60)
        }
        Invoke-LauncherRefresh
    })
    $openLogs.Add_Click({
        try { Open-LauncherViewer $script:session.Directory } catch { $notice.Text = $_.Exception.Message }
    })
    $openSaved.Add_Click({
        $picker = [Windows.Forms.FolderBrowserDialog]::new()
        try {
            $picker.Description = '기존 processes.json 또는 server.log·a.log·b.log가 있는 폴더'
            if ($picker.ShowDialog($form) -eq 'OK') { Open-LauncherViewer $picker.SelectedPath }
        } catch { $notice.Text = $_.Exception.Message } finally { $picker.Dispose() }
    })
    $openFolder.Add_Click({
        try { Start-Process -FilePath 'explorer.exe' -ArgumentList ('"{0}"' -f $script:logRoot) }
        catch { $notice.Text = $_.Exception.Message }
    })
    $timer = [Windows.Forms.Timer]@{ Interval = 900 }
    $timer.Add_Tick({
        Invoke-LauncherRefresh
        if (!$script:starting -and $script:pendingMode -ge 0) {
            if (!$script:currentLive['server']) {
                $script:pendingMode = -1
            } elseif ($script:serverReady) {
                [void](Invoke-LauncherStart 'a')
                if ($script:pendingMode -eq 0) { [void](Invoke-LauncherStart 'b') }
                $script:pendingMode = -1
                Invoke-LauncherRefresh
            } elseif ([DateTime]::UtcNow -gt $script:startupDeadline) {
                $script:pendingMode = -1
            }
        }
    })
    $form.Add_FormClosing({
        param($sender, $eventArgs)
        if ($eventArgs.CloseReason -eq 'UserClosing' -and ($script:clientStops.Count -gt 0 -or $script:starting)) {
            $eventArgs.Cancel = $true
            $script:closeAfterOperations = $true
        }
    })
    $timer.Add_Tick({
        if ($script:closeAfterOperations -and !$script:starting -and $script:clientStops.Count -eq 0) {
            $form.Close()
        }
    })
    $viewport.Add_ClientSizeChanged({
        $height = $viewport.ClientSize.Height
        if ($layout.MinimumSize.Height -ne $height) {
            $layout.MinimumSize = [Drawing.Size]::new(0, $height)
            if ($viewport.IsHandleCreated) {
                [void]$viewport.BeginInvoke([Action]{ $viewport.PerformLayout() })
            }
        }
    })
    $settingsRows.Add_ClientSizeChanged({ Resize-LauncherSettings })
    $rows.Add_ClientSizeChanged({
        $width = $rows.ClientSize.Width
        if ($script:processLayoutWidth -ne $width) {
            $script:processLayoutWidth = $width
            if ($rows.IsHandleCreated) {
                [void]$rows.BeginInvoke([Action]{ Resize-LauncherProcessRows })
            }
        }
    })
    $form.Add_Shown({
        $layout.MinimumSize = [Drawing.Size]::new(0, $viewport.ClientSize.Height)
        Invoke-LauncherRefresh
    })
    Invoke-LauncherRefresh
    $timer.Start()
    [void]$form.ShowDialog()
} finally {
    if (Get-Variable timer -ErrorAction SilentlyContinue) { $timer.Dispose() }
    try { Save-LauncherPreferences } catch { Write-Warning "설정을 저장하지 못했습니다. $($_.Exception.Message)" }

    $tooltip.Dispose()
    $form.Dispose()
    $processStatusFont.Dispose()
    if ($script:logMutex) {
        $script:logMutex.ReleaseMutex()
        $script:logMutex.Dispose()
    }
}
