#Requires -Version 7.0
# Same local validators as CI; no GitHub API or upload calls.
[CmdletBinding()]
param([string]$RepositoryRoot = (Join-Path $PSScriptRoot '../..'), [string]$OutputDirectory, [string]$DiffBase = '')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'CIResultValidation.psm1') -Force
$root = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\','/')
$top = (& git --no-optional-locks -C $root rev-parse --show-toplevel 2>$null)
if ($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($top).TrimEnd('\','/') -ne $root) { throw 'Exact Git repository root required.' }
$targetHead=(& git --no-optional-locks -C $root rev-parse HEAD)
if($LASTEXITCODE -ne 0){throw 'Git HEAD unavailable.'}
Assert-CIContext -Enabled ($env:GITHUB_ACTIONS -ceq 'true') -EventName $env:CI_EVENT_NAME -EventSha $env:CI_EVENT_SHA -TargetHead $targetHead -PrHeadSha $env:CI_PR_HEAD_SHA -PrBaseSha $env:CI_PR_BASE_SHA
$baseline = Resolve-CIDiffBaseline -RepositoryRoot $root -TargetHead $targetHead -Enabled ($env:GITHUB_ACTIONS -ceq 'true') -EventName $env:CI_EVENT_NAME -PrHeadSha $env:CI_PR_HEAD_SHA -PrBaseSha $env:CI_PR_BASE_SHA -PushBeforeSha $env:CI_PUSH_BEFORE_SHA -LocalDiffBase $DiffBase
$allowed = [IO.Path]::GetFullPath((Join-Path $root 'Artifacts/Validation'))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $allowed ('ci-' + [guid]::NewGuid().ToString('N')) }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $output.StartsWith($allowed + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe CI output directory.' }
$cursor = $output
while ($cursor.Length -ge $root.Length) {
    if ((Test-Path -LiteralPath $cursor) -and (([IO.File]::GetAttributes($cursor) -band [IO.FileAttributes]::ReparsePoint) -ne 0)) { throw 'Reparse output path.' }
    $cursor = Split-Path -Parent $cursor
}
if (Test-Path -LiteralPath $output) { throw 'CI output already exists.' }
[void][IO.Directory]::CreateDirectory($output)
$utf8 = [Text.UTF8Encoding]::new($false)
$pwsh = (Get-Process -Id $PID).Path
$phases = [Collections.Generic.List[object]]::new()
function Invoke-Phase([string]$Name, [string]$Script, [string]$Pattern) {
    $started = [DateTimeOffset]::UtcNow; $passed = $false; $count = 0; $reason = ''
    try {
        $identity=@{}
        switch($Name){
            'workflow'{
                $identity.workflowSha256=(Get-FileHash -LiteralPath (Join-Path $root '.github/workflows/repository-validation.yml')).Hash
                $identity.testScriptSha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $Script)).Hash
                $identity.resultPolicySha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'CIResultValidation.psm1')).Hash
            }
            'repository'{
                $identity.head=$targetHead;$identity.repositoryRoot=$root
                $identity.diffBaseSha=$baseline.sha;$identity.diffMode='committed-and-working-tree'
                $identity.validatorSha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'RepositoryValidation.psm1')).Hash
                $identity.entryPointSha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $Script)).Hash
            }
            'fixtures'{
                $identity.targetHead=$targetHead
                $identity.validatorSha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'RepositoryValidation.psm1')).Hash
                $identity.entryPointSha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'Validate-Repository.ps1')).Hash
                $identity.testScriptSha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $Script)).Hash
            }
        }
        $start = [Diagnostics.ProcessStartInfo]::new($pwsh)
        $start.UseShellExecute=$false; $start.CreateNoWindow=$true
        $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
        foreach($arg in @('-NoProfile','-File',(Join-Path $PSScriptRoot $Script),'-RepositoryRoot',$root)) { $start.ArgumentList.Add($arg) }
        if ($Name -eq 'repository') { $start.ArgumentList.Add('-DiffBase'); $start.ArgumentList.Add($baseline.sha) }
        $process=[Diagnostics.Process]::Start($start)
        try {
            $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
            if(-not $process.WaitForExit(540000)) { $process.Kill($true);$process.WaitForExit();throw 'Phase timed out.' }
            $code=$process.ExitCode
            $text=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
        } finally { $process.Dispose() }
        [IO.File]::WriteAllText((Join-Path $output "$Name.log"),$text,$utf8)
        $match=[regex]::Match($text,$Pattern)
        if(-not $match.Success) { throw 'Result path missing.' }
        $resultPath=$match.Groups[1].Value.Trim()
        $full=[IO.Path]::GetFullPath($resultPath)
        if(-not $full.StartsWith($allowed+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected result path.' }
        $cursor=$full
        while($cursor.Length -ge $root.Length){
            if((Test-Path -LiteralPath $cursor) -and (([IO.File]::GetAttributes($cursor) -band [IO.FileAttributes]::ReparsePoint) -ne 0)){throw 'Reparse result path.'}
            $cursor=Split-Path -Parent $cursor
        }
        $r=[IO.File]::ReadAllText($full)|ConvertFrom-Json
        $count=Assert-CIPhaseResult -Name $Name -Report $r -ExitCode $code -ExpectedIdentity $identity
        $passed=$true
    } catch { $reason='execution-or-result-check-failed'; Write-Host "[$Name] $($_.Exception.Message)" }
    $phases.Add([pscustomobject]@{name=$Name;status=$(if($passed){'passed'}else{'failed'});executed=$count;reason=$reason;durationSeconds=([DateTimeOffset]::UtcNow-$started).TotalSeconds})
    Write-Host "[$Name] $(if($passed){'passed'}else{'failed'})"
}
Invoke-Phase 'workflow' 'Test-ActionsWorkflow.ps1' '(?m)^Workflow tests result:\s*(.+)\r?$'
Invoke-Phase 'repository' 'Validate-Repository.ps1' '(?m)^Result:\s*(.+)\r?$'
Invoke-Phase 'fixtures' 'Test-RepositoryValidation.ps1' '(?m)^Self-tests:.*Result:\s*(.+)\r?$'
$failed=@($phases|Where-Object status -ne 'passed').Count
$head=$targetHead
$dirty=[bool](& git --no-optional-locks -C $root status --porcelain=v1)
$summary=[pscustomobject]@{
    schemaVersion=1;scope='repository-only';head=$head;workingTreeDirty=$dirty
    event=$env:CI_EVENT_NAME;eventSha=$env:CI_EVENT_SHA;prHeadSha=$env:CI_PR_HEAD_SHA;prBaseSha=$env:CI_PR_BASE_SHA
    diffBaseSha=$baseline.sha;diffBaseSource=$baseline.source
    powerShellVersion=$PSVersionTable.PSVersion.ToString()
    entryPointSha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
    resultPolicySha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'CIResultValidation.psm1') -Algorithm SHA256).Hash
    workflowSha256=$(if(Test-Path -LiteralPath (Join-Path $root '.github/workflows/repository-validation.yml')){(Get-FileHash -LiteralPath (Join-Path $root '.github/workflows/repository-validation.yml') -Algorithm SHA256).Hash}else{$null})
    status=$(if($failed -or $phases.Count -ne 3){'failed'}else{'passed'})
    planned=3;executed=$phases.Count;failed=$failed;phases=$phases.ToArray()
}
[IO.File]::WriteAllText((Join-Path $output 'summary.json'),($summary|ConvertTo-Json -Depth 8),$utf8)
$lines=@('# Repository validation','Scope: repository-only; no UE build or game tests.',"Status: $($summary.status)","Tested commit: $head","Diff base: $($baseline.sha) ($($baseline.source))","Working tree dirty: $dirty",'','| Phase | Status | Executed |','|---|---|---|')
foreach($phase in $phases){$lines+="| $($phase.name) | $($phase.status) | $($phase.executed) |"}
[IO.File]::WriteAllText((Join-Path $output 'summary.md'),($lines -join "`n"),$utf8)
Write-Host "CI summary: $(Join-Path $output 'summary.json')"
if($failed -or $phases.Count -ne 3){exit 1}
exit 0
