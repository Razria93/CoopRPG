[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageRoot,
    [Parameter(Mandatory)][string]$Python,
    [switch]$VerifyButton
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()

$stage = (Resolve-Path -LiteralPath $PackageRoot).Path
$pythonExe = (Resolve-Path -LiteralPath $Python).Path
$artifact = Split-Path -Parent (Split-Path -Parent $stage)
foreach ($role in 'Server','Client') {
    $executable = Join-Path $stage "Windows$role/CoopRPG/Binaries/Win64/CoopRPG$role.exe"
    if (!(Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "CoopRPG $role 패키지가 없습니다. $executable"
    }
}

$form = New-Object Windows.Forms.Form
$form.Text = 'CoopRPG | 적 전리품 경합'
$form.StartPosition = 'CenterScreen'
$form.ClientSize = [Drawing.Size]::new(500, 200)
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.Font = [Drawing.Font]::new('Malgun Gothic', 11)
if ($VerifyButton) {
    $form.Opacity = 0
    $form.ShowInTaskbar = $false
}

$instructions = New-Object Windows.Forms.Label
$instructions.SetBounds(16, 12, 468, 55)
$instructions.Text = "전용 서버와 두 클라이언트에서 같은 전리품을 요청합니다.`r`n로그 뷰어에서 지급 1회와 양쪽 팀 수량 1을 확인하세요."
$start = New-Object Windows.Forms.Button
$start.SetBounds(16, 72, 240, 36)
$start.Text = '두 클라이언트 전리품 요청'
$status = New-Object Windows.Forms.Label
$status.SetBounds(16, 118, 468, 70)
$status.Text = '대기 중'
$form.Controls.AddRange(@($instructions, $start, $status))

$run = @{ Process = $null; Output = ''; ViewerStarted = $false; Passed = $false }
$timer = New-Object Windows.Forms.Timer
$timer.Interval = 400

$start.Add_Click({
    if ($run.Process -and !$run.Process.HasExited) { return }
    try {
        $tag = if ($VerifyButton) { 'button-check' } else { 'button-manual' }
        $run.Output = Join-Path $artifact "$tag-$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
        New-Item -ItemType Directory -Path $run.Output | Out-Null
        [IO.File]::WriteAllText((Join-Path $artifact "$tag-latest.txt"), $run.Output, [Text.UTF8Encoding]::new($false))
        $arguments = '-X utf8 "{0}/Run-EnemyLoot.py" "{1}" "{2}"' -f $PSScriptRoot, $stage, $run.Output
        $run.Process = Start-Process -FilePath $pythonExe -ArgumentList $arguments -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $run.Output 'harness.stdout.log') `
            -RedirectStandardError (Join-Path $run.Output 'harness.stderr.log')
        # Keep the process handle open so ExitCode is available after the timer observes termination.
        $null = $run.Process.Handle
        $run.Passed = $false
        $run.ViewerStarted = $false
        $start.Enabled = $false
        $status.Text = '진행 중 · 서버와 클라이언트 두 개를 준비합니다.'
        $timer.Start()
    } catch {
        $status.Text = $_.Exception.Message
        $start.Enabled = $true
        if ($VerifyButton) { $form.Close() }
    }
})

$timer.Add_Tick({
    try {
        if (!$run.ViewerStarted -and (Test-Path -LiteralPath (Join-Path $run.Output 'b.log'))) {
            if (!$VerifyButton) {
                $arguments = '-NoProfile -ExecutionPolicy Bypass -STA -File "{0}/Watch-CombatManual.ps1" -LogDirectory "{1}" -ViewMode Parallel -PaneCount 3 -DisplayMode Aligned' -f $PSScriptRoot, $run.Output
                Start-Process powershell.exe -WindowStyle Hidden -ArgumentList $arguments `
                    -RedirectStandardOutput (Join-Path $run.Output 'viewer.stdout.log') `
                    -RedirectStandardError (Join-Path $run.Output 'viewer.stderr.log')
            }
            $run.ViewerStarted = $true
            $status.Text = '진행 중 · 사거리 거절, 두 요청의 경합, 재요청을 확인합니다.'
        }
        if (!$run.Process.HasExited) { return }

        $run.Process.WaitForExit()
        $timer.Stop()
        $start.Enabled = $true
        $file = Join-Path $run.Output 'result.json'
        if (!(Test-Path -LiteralPath $file)) { throw "결과 파일이 없습니다. $($run.Output)" }
        $result = Get-Content -LiteralPath $file -Encoding UTF8 -Raw | ConvertFrom-Json
        $run.Passed = $run.Process.ExitCode -eq 0 -and $result.status -eq 'PASS'
        if ($run.Passed) {
            $shortId = $result.details.dropId.Substring(0, 8)
            $status.Text = "통과 · 팀 지급 1회 / 클라이언트 1·2 팀 수량 1`r`n전리품 ID $shortId… · 사거리 밖 및 중복 요청 거절"
        } else {
            $status.Text = '실패 · ' + ($result.failures -join ', ')
        }
        if ($VerifyButton) { $form.Close() }
    } catch {
        $timer.Stop()
        $start.Enabled = $true
        $status.Text = $_.Exception.Message
        if ($VerifyButton) { $form.Close() }
    }
})

if ($VerifyButton) {
    $form.Add_Shown({ $start.PerformClick() })
}
$form.Add_FormClosing({
    param($sender, $eventArgs)
    if ($run.Process -and !$run.Process.HasExited) {
        $eventArgs.Cancel = $true
        $status.Text = '시험 프로세스가 종료되면 이 창을 닫을 수 있습니다.'
    }
})

try { [void]$form.ShowDialog() } finally { $timer.Dispose(); $form.Dispose() }
if ($VerifyButton) {
    Write-Output $status.Text
    if (!$run.Passed) { exit 1 }
}
