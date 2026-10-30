# Validate child results against the caller's snapshot, not aggregate counts alone.
Set-StrictMode -Version Latest
function Assert-Count([object]$Value,[int]$Minimum=0) {
    if(($Value -isnot [int] -and $Value -isnot [long]) -or $Value -lt $Minimum){throw 'Invalid result count.'}
}
function Assert-CIContext {
    param([bool]$Enabled,[string]$EventName,[string]$EventSha,[string]$TargetHead,[string]$PrHeadSha,[string]$PrBaseSha)
    if(-not $Enabled){return}
    if($EventName -cnotin @('pull_request','push') -or $EventSha -cnotmatch '^[0-9a-f]{40}$' -or $EventSha -cne $TargetHead){
        throw 'GitHub event and tested HEAD do not match.'
    }
    if($EventName -ceq 'pull_request' -and ($PrHeadSha -cnotmatch '^[0-9a-f]{40}$' -or $PrBaseSha -cnotmatch '^[0-9a-f]{40}$')){
        throw 'Pull request identity is incomplete.'
    }
}
function Assert-CIPhaseResult {
    param([ValidateSet('workflow','repository','fixtures')][string]$Name,[object]$Report,[int]$ExitCode,[hashtable]$ExpectedIdentity)
    Assert-Count $Report.schemaVersion 1
    if($ExitCode -ne 0 -or $Report.schemaVersion -ne 1 -or $Report.status -isnot [string] -or $Report.status -cne 'passed'){throw 'Child validator did not pass.'}
    $scopes=@{workflow='workflow-profile-local';repository='repository-only';fixtures='repository-validator-fixtures'}
    if($Report.scope -isnot [string] -or $Report.scope -cne $scopes[$Name]){throw 'Unexpected result scope.'}
    foreach($key in $ExpectedIdentity.Keys){
        $property=$Report.PSObject.Properties[$key]
        if($null -eq $property -or $property.Value -isnot [string] -or $property.Value -cne $ExpectedIdentity[$key]){
            throw "Result identity mismatch: $key"
        }
    }
    if($Name -eq 'repository'){
        Assert-Count $Report.executedChecks 1;Assert-Count $Report.plannedChecks 1;Assert-Count $Report.failedChecks
        if($Report.executedChecks -ne 6 -or $Report.plannedChecks -ne 6 -or $Report.failedChecks -ne 0 -or @($Report.checks).Count -ne 6){
            throw 'Repository result is incomplete.'
        }
        $ids=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach($check in $Report.checks){
            Assert-Count $check.items
            if($check.id -isnot [string] -or $check.status -isnot [string] -or -not $ids.Add($check.id) -or @($check.failures).Count -ne 0 -or $check.status -cnotin @('passed','notApplicable')){
                throw 'Repository check state contradicts its summary.'
            }
            if(($check.status -ceq 'passed' -and $check.items -eq 0) -or
               ($check.status -ceq 'notApplicable' -and ($check.id -cne 'configuration.syntax' -or $check.items -ne 0))){
                throw 'Unexpected check applicability.'
            }
        }
        $expected=@('repository.inputs','git.ignore-policy','git.lfs-attributes','documents.links','configuration.syntax','git.diff')
        foreach($id in $expected){if(-not $ids.Contains($id)){throw 'Required repository check missing.'}}
        return $Report.executedChecks
    }
    Assert-Count $Report.executed 1;Assert-Count $Report.planned 1;Assert-Count $Report.failed
    if($Report.executed -ne $Report.planned -or $Report.failed -ne 0 -or @($Report.cases).Count -ne $Report.executed){
        throw 'Fixture result is incomplete.'
    }
    $names=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach($case in $Report.cases){
        if($case.name -isnot [string] -or [string]::IsNullOrWhiteSpace($case.name) -or -not $names.Add($case.name) -or
           $case.passed -isnot [bool] -or -not $case.passed){throw 'Individual fixture result did not pass.'}
    }
    return $Report.executed
}
Export-ModuleMember -Function Assert-CIPhaseResult,Assert-CIContext
