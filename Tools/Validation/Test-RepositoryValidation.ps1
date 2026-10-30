#Requires -Version 7.0
# Disposable Git fixtures stay under ignored Artifacts; no engine/project builds.
[CmdletBinding()]
param([string]$RepositoryRoot = (Join-Path $PSScriptRoot '../..'))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($RepositoryRoot)
$runId = 'selftest-' + [DateTimeOffset]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$work = Join-Path $root "Artifacts/Validation/$runId"
if (Test-Path -LiteralPath $work) { throw 'Self-test output already exists.' }
# Ask the entry point's module to validate the exact repository root first.
Import-Module (Join-Path $PSScriptRoot 'RepositoryValidation.psm1') -Force
$preflight = Invoke-RepositoryValidation -RepositoryRoot $root
if ($preflight.status -ne 'passed') { throw 'Fix repository validation before running fixtures.' }
# Reject reparse parents before creating fixture repositories.
$cursor = $work
while ($cursor.Length -ge $root.Length) {
    if ((Test-Path -LiteralPath $cursor) -and
        (([IO.File]::GetAttributes($cursor) -band [IO.FileAttributes]::ReparsePoint) -ne 0)) { throw 'Reparse fixture output is unsupported.' }
    $cursor = Split-Path -Parent $cursor
}
[void][IO.Directory]::CreateDirectory($work)
$results = [Collections.Generic.List[object]]::new()
$utf8 = [Text.UTF8Encoding]::new($false)
$entry = Join-Path $PSScriptRoot 'Validate-Repository.ps1'
$pwsh = (Get-Process -Id $PID).Path

function Write-FixtureFile([string]$Dir, [string]$RelativePath, [string]$Text) {
    $path = Join-Path $Dir $RelativePath
    [void][IO.Directory]::CreateDirectory((Split-Path $path))
    [IO.File]::WriteAllText($path, $Text, $utf8)
}
function Fixture-Git([string]$Dir, [string[]]$GitArguments) {
    $output = @(& git -C $Dir @GitArguments 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "Fixture Git failed: $($GitArguments[0])" }
    $output -join "`n"
}
function New-Fixture([string]$Name) {
    $dir = Join-Path $work $Name
    [void][IO.Directory]::CreateDirectory($dir)
    [void](Fixture-Git $dir @('init', '-q', '-b', 'main'))
    [void](Fixture-Git $dir @('config', 'core.hooksPath', '.git/no-fixture-hooks'))
    [void](Fixture-Git $dir @('config', 'core.autocrlf', 'false'))
    foreach ($policy in @('.gitignore', '.gitattributes')) {
        Write-FixtureFile $dir $policy ([IO.File]::ReadAllText((Join-Path $root $policy)))
    }
    Write-FixtureFile $dir 'README.md' "# Fixture`n`n[Guide](Docs/Guide.md#한국어-제목)`n"
    Write-FixtureFile $dir 'Docs/Guide.md' "# 한국어 제목`n"
    [void](Fixture-Git $dir @('add', '--', '.gitignore', '.gitattributes', 'README.md', 'Docs/Guide.md'))
    [void](Fixture-Git $dir @('-c', 'user.name=ValidationFixture', '-c', 'user.email=fixture@example.invalid',
        '-c', 'commit.gpgsign=false', 'commit', '-q', '-m', 'fixture baseline'))
    return $dir
}
function Run-Validator([string]$Dir, [string]$Output = '') {
    $start = [Diagnostics.ProcessStartInfo]::new($pwsh)
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    foreach ($arg in @('-NoProfile', '-File', $entry, '-RepositoryRoot', $Dir)) { $start.ArgumentList.Add($arg) }
    if ($Output) { $start.ArgumentList.Add('-OutputDirectory'); $start.ArgumentList.Add($Output) }
    $process = [Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync(); $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(30000)) { $process.Kill($true); $process.WaitForExit(); throw 'Fixture validator timed out (30 seconds).' }
    $exitCode = $process.ExitCode
    $text = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
    $process.Dispose()
    $report = $null; $reportPath = ''
    $match = [regex]::Match($text, '(?m)^Result:\s*(.+)\r?$')
    if ($match.Success) {
        $reportPath = $match.Groups[1].Value.Trim()
        $report = [IO.File]::ReadAllText($reportPath) | ConvertFrom-Json
    }
    [pscustomobject]@{ ExitCode = $exitCode; Text = $text; Report = $report; ReportPath = $reportPath }
}
function Assert-Run([object]$Run, [int]$ExitCode, [string]$Check = '') {
    if ($Run.ExitCode -ne $ExitCode) { throw "Expected exit $ExitCode; got $($Run.ExitCode). $($Run.Text)" }
    if ($ExitCode -lt 2) {
        if ($null -eq $Run.Report -or $Run.Report.executedChecks -ne 6 -or $Run.Report.plannedChecks -ne 6) { throw 'Missing/incomplete result.' }
        if ($Run.Report.scope -ne 'repository-only' -or $Run.Report.head -notmatch '^[a-f0-9]{40}$') { throw 'Invalid result identity.' }
        if ($Check -and @($Run.Report.checks | Where-Object { $_.id -eq $Check -and $_.status -eq 'failed' }).Count -ne 1) {
            throw "Expected failed check: $Check"
        }
        if ($ExitCode -eq 0 -and ($Run.Report.status -ne 'passed' -or $Run.Report.failedChecks -ne 0)) { throw 'False success summary.' }
        if ($ExitCode -eq 1 -and ($Run.Report.status -ne 'failed' -or $Run.Report.failedChecks -lt 1)) { throw 'False failure summary.' }
    }
}
$cases = @(
    @{ Name='baseline'; Exit=0; Check=''; Mutate={} },
    @{ Name='duplicate-and-code-heading'; Exit=0; Check=''; Mutate={
        param($d)
        Write-FixtureFile $d 'Docs/Guide.md' ("# 한국어 제목`n# 한국어 제목`n" + '## `Inline` code' + "`n")
        Write-FixtureFile $d 'README.md' "# Fixture`n[a](Docs/Guide.md#한국어-제목-1)`n[b](Docs/Guide.md#inline-code)`n"
    }},
    @{ Name='reference-and-spaces'; Exit=0; Check=''; Mutate={
        param($d)
        Write-FixtureFile $d 'Docs/With Space.md' "# Space`n"
        Write-FixtureFile $d 'README.md' "# Fixture`n[guide][ref]`n[ref]: <Docs/With Space.md#space>`n"
    }},
    @{ Name='code-examples'; Exit=0; Check=''; Mutate={
        param($d)
        Write-FixtureFile $d 'README.md' (@('# Fixture','```text','[example](missing.md)','```','`[inline](also-missing.md)`') -join "`n")
    }},
    @{ Name='external-not-fetched'; Exit=0; Check=''; Mutate={
        param($d); Write-FixtureFile $d 'README.md' "# Fixture`n[external](https://invalid.example.invalid/no-network)`n"
    }},
    @{ Name='broken-file'; Exit=1; Check='documents.links'; Mutate={
        param($d); Write-FixtureFile $d 'README.md' "# Fixture`n[bad](Missing.md)`n"
    }},
    @{ Name='broken-anchor'; Exit=1; Check='documents.links'; Mutate={
        param($d); Write-FixtureFile $d 'README.md' "# Fixture`n[bad](Docs/Guide.md#absent)`n"
    }},
    @{ Name='undefined-reference'; Exit=1; Check='documents.links'; Mutate={
        param($d); Write-FixtureFile $d 'README.md' "# Fixture`n[bad][undefined]`n"
    }},
    @{ Name='case-mismatch'; Exit=1; Check='documents.links'; Mutate={
        param($d); Write-FixtureFile $d 'README.md' "# Fixture`n[bad](docs/Guide.md)`n"
    }},
    @{ Name='outside-link'; Exit=1; Check='documents.links'; Mutate={
        param($d); Write-FixtureFile $d 'README.md' "# Fixture`n[bad](../outside.md)`n"
    }},
    @{ Name='untracked-document'; Exit=1; Check='documents.links'; Mutate={
        param($d); Write-FixtureFile $d 'Docs/New.md' "# New`n[bad](Absent.md)`n"
    }},
    @{ Name='invalid-json'; Exit=1; Check='configuration.syntax'; Mutate={
        param($d); Write-FixtureFile $d 'settings.json' '{ broken }'
    }},
    @{ Name='valid-json-and-script'; Exit=0; Check=''; Mutate={
        param($d); Write-FixtureFile $d 'settings.json' '{"enabled":true}'
        Write-FixtureFile $d 'sample.ps1' 'param(); Write-Output "fixture"'
    }},
    @{ Name='invalid-script'; Exit=1; Check='configuration.syntax'; Mutate={
        param($d); Write-FixtureFile $d 'sample.ps1' 'function Broken {'
    }},
    @{ Name='missing-ignore-rule'; Exit=1; Check='git.ignore-policy'; Mutate={
        param($d); Write-FixtureFile $d '.gitignore' '# intentionally incomplete'
    }},
    @{ Name='overbroad-ignore'; Exit=1; Check='git.ignore-policy'; Mutate={
        param($d); $p=Join-Path $d '.gitignore'; [IO.File]::AppendAllText($p,"`n*.vcxproj`n",$utf8)
    }},
    @{ Name='missing-lfs-rule'; Exit=1; Check='git.lfs-attributes'; Mutate={
        param($d); Write-FixtureFile $d '.gitattributes' '* text=auto'
    }},
    @{ Name='asset-attribute-override'; Exit=1; Check='git.lfs-attributes'; Mutate={
        param($d); Write-FixtureFile $d 'Game/Content/Dummy.uasset' 'fixture, not an Unreal asset'
        [IO.File]::AppendAllText((Join-Path $d '.gitattributes'),"`nGame/Content/Dummy.uasset -filter`n",$utf8)
    }},
    @{ Name='forced-generated-input'; Exit=1; Check='repository.inputs'; Mutate={
        param($d); Write-FixtureFile $d 'Game/Saved/dummy.log' 'fixture only'
        [void](Fixture-Git $d @('add','-f','--','Game/Saved/dummy.log'))
    }},
    @{ Name='forced-root-cache'; Exit=1; Check='repository.inputs'; Mutate={
        param($d); Write-FixtureFile $d 'DerivedDataCache/dummy.bin' 'fixture only'
        [void](Fixture-Git $d @('add','-f','--','DerivedDataCache/dummy.bin'))
    }},
    @{ Name='oversized-input'; Exit=1; Check='repository.inputs'; Mutate={
        param($d); $stream=[IO.File]::Create((Join-Path $d 'large.bin')); try{$stream.SetLength(50MB+1)}finally{$stream.Dispose()}
    }},
    @{ Name='missing-indexed-file'; Exit=1; Check='repository.inputs'; Mutate={
        param($d); $blob=Fixture-Git $d @('rev-parse','HEAD:README.md')
        [void](Fixture-Git $d @('update-index','--add','--cacheinfo',"100644,$blob,Missing.md"))
    }},
    @{ Name='whitespace-diff'; Exit=1; Check='git.diff'; Mutate={
        param($d); Write-FixtureFile $d 'README.md' "# Fixture`ntrailing   `n"
    }},
    @{ Name='existing-output'; Exit=2; Check=''; Mutate={
        param($d); Write-FixtureFile $d 'Artifacts/Validation/occupied/sentinel.txt' 'preserve'
    }},
    @{ Name='forced-nested-ide-cache'; Exit=1; Check='repository.inputs'; Mutate={
        param($d); Write-FixtureFile $d 'Tools/.vs/dummy.bin' 'fixture only'
        [void](Fixture-Git $d @('add','-f','--','Tools/.vs/dummy.bin'))
    }},
    @{ Name='html-data-id-is-not-anchor'; Exit=1; Check='documents.links'; Mutate={
        param($d); Write-FixtureFile $d 'Docs/Guide.md' '<a data-id="fake"></a>'
        Write-FixtureFile $d 'README.md' "# Fixture`n[bad](Docs/Guide.md#fake)`n"
    }},
    @{ Name='quoted-html-anchors'; Exit=0; Check=''; Mutate={
        param($d); Write-FixtureFile $d 'Docs/Guide.md' '<a id="valid"></a><a name=''named''></a>'
        Write-FixtureFile $d 'README.md' "# Fixture`n[a](Docs/Guide.md#valid)`n[b](Docs/Guide.md#named)`n"
    }},
    @{ Name='html-title-is-not-anchor'; Exit=1; Check='documents.links'; Mutate={
        param($d); Write-FixtureFile $d 'Docs/Guide.md' '<a title='' id="fake"''></a>'
        Write-FixtureFile $d 'README.md' "# Fixture`n[bad](Docs/Guide.md#fake)`n"
    }},
    @{ Name='outside-output'; Exit=2; Check=''; Mutate={
        param($d)
    }}
)
foreach ($case in $cases) {
    $started = [DateTimeOffset]::UtcNow; $ok = $false; $message = ''; $reportPath = ''
    try {
        $dir = New-Fixture $case.Name
        & $case.Mutate $dir
        $output = if ($case.Name -eq 'existing-output') { Join-Path $dir 'Artifacts/Validation/occupied' }
            elseif ($case.Name -eq 'outside-output') { Join-Path $dir 'not-allowed-output' } else { '' }
        $run = Run-Validator $dir $output
        Assert-Run $run $case.Exit $case.Check
        $reportPath = $run.ReportPath
        if ($case.Name -eq 'existing-output' -and [IO.File]::ReadAllText((Join-Path $output 'sentinel.txt')) -ne 'preserve') { throw 'Prior evidence modified.' }
        if ($case.Name -eq 'outside-output' -and (Test-Path -LiteralPath $output)) { throw 'Unsafe output was created.' }
        if ($case.Name -eq 'baseline') {
            $again = Run-Validator $dir; Assert-Run $again 0
            if ($run.Report.runId -eq $again.Report.runId) { throw 'Run IDs are not unique.' }
            $sub = Join-Path $dir 'Docs'; Assert-Run (Run-Validator $sub) 2
        }
        $ok = $true
    } catch { $message = $_.Exception.Message }
    $results.Add([pscustomobject]@{ name=$case.Name; passed=$ok; expectedExit=$case.Exit; expectedCheck=$case.Check
        reportPath=$reportPath; durationSeconds=([DateTimeOffset]::UtcNow-$started).TotalSeconds; message=$message })
    Write-Host "[$(if($ok){'passed'}else{'FAILED'})] $($case.Name)"
    if (-not $ok) { Write-Host $message }
}
$failed = @($results | Where-Object { -not $_.passed }).Count
$summary = [pscustomobject]@{ schemaVersion=1; runId=$runId; scope='repository-validator-fixtures'
    targetHead=$preflight.head; workingTreeDirty=$preflight.workingTreeDirty
    testScriptSha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
    validatorSha256=$preflight.validatorSha256; entryPointSha256=(Get-FileHash -LiteralPath $entry -Algorithm SHA256).Hash
    planned=$cases.Count; executed=$results.Count; failed=$failed
    status=$(if($failed -or $results.Count -eq 0){'failed'}else{'passed'}); cases=$results.ToArray() }
$summaryPath = Join-Path $work 'selftest.json'
[IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 12), $utf8)
Write-Host "Self-tests: $($results.Count) executed, $failed failed. Result: $summaryPath"
if ($failed -or $results.Count -eq 0 -or $results.Count -ne $cases.Count) { exit 1 }
exit 0
