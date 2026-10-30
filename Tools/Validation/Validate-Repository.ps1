#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '../..'),
    [string]$OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
try {
    Import-Module (Join-Path $PSScriptRoot 'RepositoryValidation.psm1') -Force
    $report = Invoke-RepositoryValidation -RepositoryRoot $RepositoryRoot
    $allowed = [IO.Path]::GetFullPath((Join-Path $report.repositoryRoot 'Artifacts/Validation'))
    if (-not $OutputDirectory) { $OutputDirectory = Join-Path $allowed $report.runId }
    $output = [IO.Path]::GetFullPath($OutputDirectory)
    if (-not $output.StartsWith($allowed + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'OutputDirectory must be a new subdirectory of repository Artifacts/Validation.'
    }
    $cursor = $output
    while ($cursor.Length -ge $report.repositoryRoot.Length) {
        if ((Test-Path -LiteralPath $cursor) -and
            (([IO.File]::GetAttributes($cursor) -band [IO.FileAttributes]::ReparsePoint) -ne 0)) { throw 'Reparse output paths are unsupported.' }
        $cursor = Split-Path -Parent $cursor
    }
    if (Test-Path -LiteralPath $output) { throw 'OutputDirectory already exists; previous evidence will not be overwritten.' }
    [void][IO.Directory]::CreateDirectory($output)
    $report | Add-Member NoteProperty command ([pscustomobject]@{
        entryPoint = 'Tools/Validation/Validate-Repository.ps1'; repositoryRoot = $report.repositoryRoot; outputDirectory = $output
    })
    $report | Add-Member NoteProperty entryPointSha256 (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
    $reportPath = Join-Path $output 'result.json'
    [IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
    $saved = [IO.File]::ReadAllText($reportPath) | ConvertFrom-Json
    if ($saved.executedChecks -ne 6 -or $saved.runId -ne $report.runId) { throw 'Result persistence verification failed.' }
    Write-Host "Repository validation: $($report.status); $($report.failedChecks)/6 checks failed."
    foreach ($check in $report.checks) {
        Write-Host "[$($check.status)] $($check.id) ($($check.items) items)"
        foreach ($failure in $check.failures) { Write-Host "  $failure" }
    }
    Write-Host "Result: $reportPath"
    if ($report.failedChecks) { exit 1 }
    exit 0
} catch {
    Write-Error -Message ("Repository validation infrastructure failure: " + $_.Exception.Message) -ErrorAction Continue
    exit 2
}
