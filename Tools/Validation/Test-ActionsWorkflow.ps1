#Requires -Version 7.0
# Project workflow profile, mutation tests, and execution of the real gate script.
# This is not GitHub's complete workflow schema or a runner emulator.
[CmdletBinding()]
param([string]$RepositoryRoot = (Join-Path $PSScriptRoot '../..'))
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'CIResultValidation.psm1') -Force
$root=[IO.Path]::GetFullPath($RepositoryRoot)
$path=Join-Path $root '.github/workflows/repository-validation.yml'
$source=[IO.File]::ReadAllText($path)
function Assert-Condition([bool]$Condition,[string]$Message) { if(-not $Condition){throw $Message} }
function Assert-Keys([Collections.IDictionary]$Object,[string[]]$Allowed) {
    foreach($key in $Object.Keys){Assert-Condition ($key -cin $Allowed) "Unexpected workflow key: $key"}
}
function Assert-NoDuplicateKeys([System.Text.Json.JsonElement]$Element) {
    if($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Object){
        $keys=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach($p in $Element.EnumerateObject()){
            Assert-Condition ($keys.Add($p.Name)) "Duplicate workflow key: $($p.Name)"
            Assert-NoDuplicateKeys $p.Value
        }
    } elseif($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Array){
        foreach($item in $Element.EnumerateArray()){Assert-NoDuplicateKeys $item}
    }
}
function Read-Workflow([string]$Text) {
    $json=[System.Text.Json.JsonDocument]::Parse($Text)
    try{Assert-NoDuplicateKeys $json.RootElement}finally{$json.Dispose()}
    ConvertFrom-Json -InputObject $Text -AsHashtable -Depth 100
}
function Assert-Workflow([Collections.IDictionary]$w) {
    Assert-Keys $w @('name','on','permissions','concurrency','defaults','env','jobs')
    Assert-Condition ($w.name -ceq 'Repository validation') 'Unstable workflow name.'
    Assert-Condition ((@($w.on.Keys|Sort-Object)-join ',') -ceq 'pull_request,push') 'Unexpected event/filter.'
    foreach($event in @('pull_request','push')){
        Assert-Keys $w.on[$event] @('branches')
        Assert-Condition ((@($w.on[$event].branches)-join ',') -ceq 'main') 'Expected main target only.'
    }
    Assert-Keys $w.permissions @('contents')
    Assert-Condition ($w.permissions.contents -ceq 'read') 'Read-only contents required.'
    Assert-Keys $w.concurrency @('group','cancel-in-progress')
    Assert-Condition ($w.concurrency.group -ceq 'repository-validation-${{ github.event.pull_request.number || github.ref }}') 'Concurrency grouping changed.'
    Assert-Condition ($w.concurrency['cancel-in-progress'] -ceq $true) 'Cancellation required.'
    Assert-Keys $w.defaults @('run');Assert-Keys $w.defaults.run @('shell')
    Assert-Condition ($w.defaults.run.shell -ceq 'pwsh') 'Expected pwsh.'
    $envExpected=@{GIT_LFS_SKIP_SMUDGE='1';CI_EVENT_NAME='${{ github.event_name }}';CI_EVENT_SHA='${{ github.sha }}'
        CI_PR_HEAD_SHA='${{ github.event.pull_request.head.sha }}';CI_PR_BASE_SHA='${{ github.event.pull_request.base.sha }}';CI_PUSH_BEFORE_SHA='${{ github.event.before }}'}
    Assert-Keys $w.env @($envExpected.Keys)
    foreach($key in $envExpected.Keys){Assert-Condition ($w.env[$key] -ceq $envExpected[$key]) "Unexpected context mapping: $key"}
    Assert-Keys $w.jobs @('validate','repository-validation')
    Assert-Condition ($w.jobs.Count -eq 2) 'Expected two jobs.'
    $job=$w.jobs.validate;$gate=$w.jobs['repository-validation']
    Assert-Keys $job @('name','runs-on','timeout-minutes','steps')
    Assert-Condition ($job.name -ceq 'repository-validation-details' -and $job['runs-on'] -ceq 'windows-2022' -and $job['timeout-minutes'] -eq 15) 'Unexpected validation runner/timeout/name.'
    Assert-Condition ($job.steps.Count -eq 4) 'Unexpected validation steps.'
    foreach($step in $job.steps){
        Assert-Keys $step @('name','uses','with','if','run')
        if($step.Contains('run')){
            Assert-Condition (-not $step.run.Contains('$'+'{{')) 'No expression interpolation in shell scripts.'
            $tokens=$null;$errors=$null
            [void][Management.Automation.Language.Parser]::ParseInput($step.run,[ref]$tokens,[ref]$errors)
            Assert-Condition ($errors.Count -eq 0) 'Invalid inline PowerShell syntax.'
        }
    }
    $checkout=$job.steps[0];$execute=$job.steps[1];$summary=$job.steps[2];$upload=$job.steps[3]
    Assert-Keys $checkout @('name','uses','with')
    Assert-Condition ($checkout.uses -ceq 'actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1') 'Checkout must use reviewed v7.0.1 SHA.'
    Assert-Keys $checkout.with @('persist-credentials','fetch-depth','lfs','submodules')
    Assert-Condition ($checkout.with['persist-credentials'] -ceq $false -and $checkout.with.lfs -ceq $false -and $checkout.with.submodules -ceq $false -and $checkout.with['fetch-depth'] -eq 0) 'Unsafe checkout or incomplete comparison history.'
    Assert-Keys $execute @('name','run')
    Assert-Condition ($execute.run -ceq 'pwsh -NoProfile -File ./Tools/Validation/Invoke-RepositoryCI.ps1 -OutputDirectory ./Artifacts/Validation/ci') 'Unexpected validation entry point.'
    Assert-Condition ($summary['if'] -ceq '${{ always() && !cancelled() }}' -and $upload['if'] -ceq $summary['if']) 'Failure result collection condition changed.'
    Assert-Keys $summary @('name','if','run')
    Assert-Condition ($summary.run -ceq "if (-not (Test-Path -LiteralPath 'Artifacts/Validation/ci/summary.md')) { throw 'CI summary missing.' }; Get-Content -LiteralPath 'Artifacts/Validation/ci/summary.md' -Raw | Add-Content -LiteralPath `$env:GITHUB_STEP_SUMMARY") 'Unexpected summary publishing script.'
    Assert-Keys $upload @('name','if','uses','with')
    Assert-Condition ($upload.uses -ceq 'actions/upload-artifact@cf430e030ddbb5b0abf93d22962f4752f3646cd9') 'Upload must use reviewed v7.0.2 SHA.'
    Assert-Keys $upload.with @('name','path','if-no-files-found','retention-days','include-hidden-files','overwrite')
    Assert-Condition ($upload.with.path -ceq "Artifacts/Validation/ci/summary.json`nArtifacts/Validation/ci/summary.md") 'Only summary files may be uploaded.'
    Assert-Condition ($upload.with.name -ceq 'repository-validation-${{ github.run_id }}-${{ github.run_attempt }}') 'Artifact names must isolate attempts.'
    Assert-Condition ($upload.with['retention-days'] -eq 14 -and $upload.with['if-no-files-found'] -ceq 'error' -and $upload.with['include-hidden-files'] -ceq $false -and $upload.with.overwrite -ceq $false) 'Unexpected artifact policy.'
    Assert-Keys $gate @('name','needs','if','runs-on','timeout-minutes','permissions','env','steps')
    Assert-Condition ($gate.name -ceq 'repository-validation' -and (@($gate.needs)-join ',') -ceq 'validate' -and $gate['if'] -ceq '${{ always() }}') 'Required gate dependency/condition changed.'
    Assert-Condition ($gate['runs-on'] -ceq 'windows-2022' -and $gate['timeout-minutes'] -eq 2 -and $gate.permissions.Count -eq 0) 'Unexpected gate runner/permissions/timeout.'
    Assert-Keys $gate.env @('CHECKS_RESULT')
    Assert-Condition ($gate.env.CHECKS_RESULT -ceq '${{ needs.validate.result }}' -and $gate.steps.Count -eq 1) 'Gate must inspect the actual dependency result.'
    Assert-Keys $gate.steps[0] @('name','run')
    Assert-Condition ($gate.steps[0].run -ceq "if (`$env:CHECKS_RESULT -ne 'success') { throw `"Repository validation dependency did not succeed: `$env:CHECKS_RESULT`" }; Write-Host 'Repository validation gate passed.'") 'Gate script changed; review and update its failure contract.'
}
$results=[Collections.Generic.List[object]]::new()
function Record-Test([string]$Name,[scriptblock]$Body) {
    $ok=$true;$message=''
    try{& $Body}catch{$ok=$false;$message=$_.Exception.Message}
    $results.Add([pscustomobject]@{name=$Name;passed=$ok;message=$message})
    Write-Host "[$(if($ok){'passed'}else{'FAILED'})] $Name"
}
Record-Test 'actual-workflow-profile' {Assert-Workflow (Read-Workflow $source)}
Record-Test 'invalid-json-rejected' {$caught=$false;try{Read-Workflow '{bad}'|Out-Null}catch{$caught=$true};Assert-Condition $caught 'Invalid JSON accepted.'}
Record-Test 'duplicate-keys-rejected' {$caught=$false;try{Read-Workflow '{"on":{},"on":{}}'|Out-Null}catch{$caught=$true};Assert-Condition $caught 'Duplicate keys accepted.'}
$mutations=@(
    @{name='path-filter';change={param($w)$w.on.pull_request.paths=@('Docs/**')}},
    @{name='privileged-event';change={param($w)$w.on.pull_request_target=@{}}},
    @{name='write-permission';change={param($w)$w.permissions.contents='write'}},
    @{name='no-concurrency-cancel';change={param($w)$w.concurrency['cancel-in-progress']=$false}},
    @{name='mutable-action-tag';change={param($w)$w.jobs.validate.steps[0].uses='actions/checkout@v7'}},
    @{name='persisted-credentials';change={param($w)$w.jobs.validate.steps[0].with['persist-credentials']=$true}},
    @{name='missing-history';change={param($w)$w.jobs.validate.steps[0].with['fetch-depth']=1}},
    @{name='lfs-download';change={param($w)$w.jobs.validate.steps[0].with.lfs=$true}},
    @{name='submodules';change={param($w)$w.jobs.validate.steps[0].with.submodules=$true}},
    @{name='self-hosted';change={param($w)$w.jobs.validate['runs-on']='self-hosted'}},
    @{name='continue-on-error';change={param($w)$w.jobs.validate['continue-on-error']=$true}},
    @{name='skipped-job';change={param($w)$w.jobs.validate['if']='false'}},
    @{name='missing-gate-needs';change={param($w)$w.jobs['repository-validation'].needs=@()}},
    @{name='skipped-gate';change={param($w)$w.jobs['repository-validation']['if']='false'}},
    @{name='faked-gate-input';change={param($w)$w.jobs['repository-validation'].env.CHECKS_RESULT='success'}},
    @{name='bypass-gate-script';change={param($w)$w.jobs['repository-validation'].steps[0].run="Write-Host 'ok'"}},
    @{name='broad-artifact';change={param($w)$w.jobs.validate.steps[3].with.path='Artifacts/**'}},
    @{name='hidden-artifact';change={param($w)$w.jobs.validate.steps[3].with['include-hidden-files']=$true}},
    @{name='missing-artifact-accepted';change={param($w)$w.jobs.validate.steps[3].with['if-no-files-found']='ignore'}},
    @{name='failure-summary-skipped';change={param($w)$w.jobs.validate.steps[2]['if']='success()'}}
)
foreach($mutation in $mutations){
    Record-Test "reject-$($mutation.name)" {
        $model=Read-Workflow $source;& $mutation.change $model
        $caught=$false;try{Assert-Workflow $model}catch{$caught=$true}
        Assert-Condition $caught 'Unsafe mutation accepted.'
    }
}
function New-FixtureContract {
    [pscustomobject]@{schemaVersion=1;scope='repository-validator-fixtures';status='passed';executed=1;planned=1;failed=0
        targetHead=('a'*40);testScriptSha256=('b'*64);cases=@([pscustomobject]@{name='one';passed=$true})}
}
function New-RepositoryContract {
    $checks=@(foreach($id in @('repository.inputs','git.ignore-policy','git.lfs-attributes','documents.links','configuration.syntax','git.diff')){
        [pscustomobject]@{id=$id;status='passed';items=1;failures=@()}
    })
    [pscustomobject]@{schemaVersion=1;scope='repository-only';status='passed';executedChecks=6;plannedChecks=6;failedChecks=0;checks=$checks}
}
function Expect-Rejection([scriptblock]$Body) {
    $caught=$false;try{& $Body|Out-Null}catch{$caught=$true}
    Assert-Condition $caught 'Invalid result/context was accepted.'
}
$expectedFixture=@{targetHead=('a'*40);testScriptSha256=('b'*64)}
$contracts=@(
    @{name='fixture-result-valid';body={[void](Assert-CIPhaseResult fixtures (New-FixtureContract) 0 $expectedFixture)}},
    @{name='workflow-result-valid';body={$r=New-FixtureContract;$r.scope='workflow-profile-local';[void](Assert-CIPhaseResult workflow $r 0 @{})}},
    @{name='repository-result-valid';body={[void](Assert-CIPhaseResult repository (New-RepositoryContract) 0 @{})}},
    @{name='result-schema';body={$r=New-FixtureContract;$r.schemaVersion=2;Expect-Rejection {Assert-CIPhaseResult fixtures $r 0 $expectedFixture}}},
    @{name='schema-type';body={$r=New-FixtureContract;$r.schemaVersion=$true;Expect-Rejection {Assert-CIPhaseResult fixtures $r 0 $expectedFixture}}},
    @{name='status-type';body={$r=New-FixtureContract;$r.status=@('passed','passed');Expect-Rejection {Assert-CIPhaseResult fixtures $r 0 $expectedFixture}}},
    @{name='scope-type';body={$r=New-FixtureContract;$r.scope=@('repository-validator-fixtures','repository-validator-fixtures');Expect-Rejection {Assert-CIPhaseResult fixtures $r 0 $expectedFixture}}},
    @{name='result-scope';body={$r=New-FixtureContract;$r.scope='other';Expect-Rejection {Assert-CIPhaseResult fixtures $r 0 $expectedFixture}}},
    @{name='result-head';body={$r=New-FixtureContract;$r.targetHead='0'*40;Expect-Rejection {Assert-CIPhaseResult fixtures $r 0 $expectedFixture}}},
    @{name='result-code-hash';body={$r=New-FixtureContract;$r.testScriptSha256='0'*64;Expect-Rejection {Assert-CIPhaseResult fixtures $r 0 $expectedFixture}}},
    @{name='result-identity-missing';body={$r=New-FixtureContract;$r.PSObject.Properties.Remove('targetHead');Expect-Rejection {Assert-CIPhaseResult fixtures $r 0 $expectedFixture}}},
    @{name='aggregate-contradicts-case';body={$r=New-FixtureContract;$r.cases[0].passed=$false;Expect-Rejection {Assert-CIPhaseResult fixtures $r 0 $expectedFixture}}},
    @{name='case-count-mismatch';body={$r=New-FixtureContract;$r.cases=@();Expect-Rejection {Assert-CIPhaseResult fixtures $r 0 $expectedFixture}}},
    @{name='duplicate-case';body={$r=New-FixtureContract;$r.executed=2;$r.planned=2;$r.cases=@($r.cases[0],$r.cases[0]);Expect-Rejection {Assert-CIPhaseResult fixtures $r 0 $expectedFixture}}},
    @{name='zero-cases';body={$r=New-FixtureContract;$r.executed=0;$r.planned=0;$r.cases=@();Expect-Rejection {Assert-CIPhaseResult fixtures $r 0 $expectedFixture}}},
    @{name='count-type';body={$r=New-FixtureContract;$r.executed='1';Expect-Rejection {Assert-CIPhaseResult fixtures $r 0 $expectedFixture}}},
    @{name='exit-code-overrides-success';body={Expect-Rejection {Assert-CIPhaseResult fixtures (New-FixtureContract) 1 $expectedFixture}}},
    @{name='repository-inner-failure';body={$r=New-RepositoryContract;$r.checks[0].status='failed';Expect-Rejection {Assert-CIPhaseResult repository $r 0 @{}}}},
    @{name='repository-missing-check';body={$r=New-RepositoryContract;$r.checks=@($r.checks|Select-Object -First 5);Expect-Rejection {Assert-CIPhaseResult repository $r 0 @{}}}},
    @{name='repository-duplicate-check';body={$r=New-RepositoryContract;$r.checks[1].id=$r.checks[0].id;Expect-Rejection {Assert-CIPhaseResult repository $r 0 @{}}}},
    @{name='repository-baseline-missing';body={Expect-Rejection {Assert-CIPhaseResult repository (New-RepositoryContract) 0 @{diffBaseSha=('a'*40)}}}},
    @{name='repository-baseline-mismatch';body={$r=New-RepositoryContract;$r|Add-Member NoteProperty diffBaseSha ('b'*40);Expect-Rejection {Assert-CIPhaseResult repository $r 0 @{diffBaseSha=('a'*40)}}}},
    @{name='context-local';body={Assert-CIContext -Enabled $false}},
    @{name='context-push';body={Assert-CIContext -Enabled $true -EventName push -EventSha ('a'*40) -TargetHead ('a'*40)}},
    @{name='context-pr';body={Assert-CIContext -Enabled $true -EventName pull_request -EventSha ('a'*40) -TargetHead ('a'*40) -PrHeadSha ('b'*40) -PrBaseSha ('c'*40)}},
    @{name='context-wrong-head';body={Expect-Rejection {Assert-CIContext -Enabled $true -EventName push -EventSha ('b'*40) -TargetHead ('a'*40)}}},
    @{name='context-pr-incomplete';body={Expect-Rejection {Assert-CIContext -Enabled $true -EventName pull_request -EventSha ('a'*40) -TargetHead ('a'*40)}}},
    @{name='context-unknown-event';body={Expect-Rejection {Assert-CIContext -Enabled $true -EventName workflow_dispatch -EventSha ('a'*40) -TargetHead ('a'*40)}}}
)
foreach($contract in $contracts){Record-Test $contract.name $contract.body}
$model=Read-Workflow $source
$savedResult=$env:CHECKS_RESULT
try{
    foreach($state in @('success','failure','cancelled','skipped','','neutral','timed_out','unexpected')){
        Record-Test "gate-$state" {
            $env:CHECKS_RESULT=$state;$succeeded=$true
            try{& ([scriptblock]::Create($model.jobs['repository-validation'].steps[0].run))|Out-Null}catch{$succeeded=$false}
            Assert-Condition ($succeeded -eq ($state -ceq 'success')) 'Gate status handling mismatch.'
        }
    }
}finally{$env:CHECKS_RESULT=$savedResult}
$failed=@($results|Where-Object {-not $_.passed}).Count
$output=Join-Path $root ('Artifacts/Validation/workflow-'+[guid]::NewGuid().ToString('N'))
$cursor=$output
while($cursor.Length -ge $root.Length){
    if((Test-Path -LiteralPath $cursor)-and (([IO.File]::GetAttributes($cursor)-band [IO.FileAttributes]::ReparsePoint)-ne 0)){throw 'Reparse test output.'}
    $cursor=Split-Path -Parent $cursor
}
[void][IO.Directory]::CreateDirectory($output)
$publishRoot=Join-Path $output 'publish-fixture'
[void][IO.Directory]::CreateDirectory($publishRoot)
$savedSummary=$env:GITHUB_STEP_SUMMARY
try{
    $env:GITHUB_STEP_SUMMARY=Join-Path $output 'step-summary.md'
    Push-Location $publishRoot
    try{
        $publishScript=[scriptblock]::Create($model.jobs.validate.steps[2].run)
        Record-Test 'missing-summary-blocked' {
            $caught=$false;try{& $publishScript}catch{$caught=$true}
            Assert-Condition $caught 'Missing summary did not fail.'
        }
        $summaryPath=Join-Path $publishRoot 'Artifacts/Validation/ci/summary.md'
        [void][IO.Directory]::CreateDirectory((Split-Path $summaryPath))
        [IO.File]::WriteAllText($summaryPath,'# Fixture summary',[Text.UTF8Encoding]::new($false))
        Record-Test 'summary-published' {
            & $publishScript
            Assert-Condition ([IO.File]::ReadAllText($env:GITHUB_STEP_SUMMARY).Trim() -ceq '# Fixture summary') 'Published summary mismatch.'
        }
    }finally{Pop-Location}
}finally{$env:GITHUB_STEP_SUMMARY=$savedSummary}
function Baseline-Git([string[]]$Arguments) {
    $text=@(& git --no-optional-locks -C $baselineRoot @Arguments 2>&1)
    if($LASTEXITCODE -ne 0){throw "Baseline fixture Git failed: $($Arguments[0])"}
    $text -join "`n"
}
$baselineRoot=Join-Path $output 'baseline-fixture'
[void][IO.Directory]::CreateDirectory($baselineRoot)
[void](Baseline-Git @('init','-q','-b','main'))
foreach($pair in @(@('core.hooksPath','.git/no-fixture-hooks'),@('core.autocrlf','false'),
    @('user.name','ValidationFixture'),@('user.email','fixture@example.invalid'),@('commit.gpgsign','false'))){
    [void](Baseline-Git @('config',$pair[0],$pair[1]))
}
function Baseline-Commit([string]$File) {
    [IO.File]::WriteAllText((Join-Path $baselineRoot $File),"fixture`n",[Text.UTF8Encoding]::new($false))
    [void](Baseline-Git @('add','--',$File));[void](Baseline-Git @('commit','-q','-m','baseline fixture'))
    Baseline-Git @('rev-parse','HEAD')
}
$ancestor=Baseline-Commit 'root.txt'
[void](Baseline-Git @('checkout','-q','-b','feature'))
$prHead=Baseline-Commit 'feature.txt'
[void](Baseline-Git @('checkout','-q','main'))
$prBase=Baseline-Commit 'base.txt'
# A parent-looking message line must not be parsed as a commit header.
[void](Baseline-Git @('merge','-q','--no-ff','--no-gpg-sign','-m',("test merge`n`nparent $ancestor"),'feature'))
$mergeHead=Baseline-Git @('rev-parse','HEAD')
[void](Baseline-Git @('checkout','-q','-b','unmerged'))
$future=Baseline-Commit 'future.txt'
[void](Baseline-Git @('checkout','-q','main'))
function Resolve-Fixture([hashtable]$Options) {
    $params=@{RepositoryRoot=$baselineRoot;TargetHead=$mergeHead;Enabled=$false}
    foreach($key in $Options.Keys){$params[$key]=$Options[$key]}
    Resolve-CIDiffBaseline @params
}
$baselineCases=@(
    @{name='baseline-local-default';body={$b=Resolve-Fixture @{};Assert-Condition ($b.sha -ceq $prBase -and $b.source -ceq 'local-first-parent') 'Wrong local first parent.'}},
    @{name='baseline-local-explicit';body={$b=Resolve-Fixture @{LocalDiffBase=$ancestor};Assert-Condition ($b.sha -ceq $ancestor) 'Explicit range lost.'}},
    @{name='baseline-pr-merge';body={$b=Resolve-Fixture @{Enabled=$true;EventName='pull_request';PrBaseSha=$prBase;PrHeadSha=$prHead};Assert-Condition ($b.sha -ceq $prBase -and $b.source -ceq 'pull-request-base') 'Wrong PR baseline.'}},
    @{name='baseline-pr-wrong-base';body={Expect-Rejection {Resolve-Fixture @{Enabled=$true;EventName='pull_request';PrBaseSha=$ancestor;PrHeadSha=$prHead}}}},
    @{name='baseline-pr-wrong-head';body={Expect-Rejection {Resolve-Fixture @{Enabled=$true;EventName='pull_request';PrBaseSha=$prBase;PrHeadSha=$ancestor}}}},
    @{name='baseline-pr-not-merge';body={Expect-Rejection {Resolve-Fixture @{Enabled=$true;EventName='pull_request';TargetHead=$prBase;PrBaseSha=$ancestor;PrHeadSha=$prHead}}}},
    @{name='baseline-push-multiple-commits';body={$b=Resolve-Fixture @{Enabled=$true;EventName='push';PushBeforeSha=$ancestor};Assert-Condition ($b.sha -ceq $ancestor -and $b.source -ceq 'push-before') 'Push range reduced to last commit.'}},
    @{name='baseline-push-missing';body={Expect-Rejection {Resolve-Fixture @{Enabled=$true;EventName='push'}}}},
    @{name='baseline-push-new-branch';body={Expect-Rejection {Resolve-Fixture @{Enabled=$true;EventName='push';PushBeforeSha=('0'*40)}}}},
    @{name='baseline-self';body={Expect-Rejection {Resolve-Fixture @{LocalDiffBase=$mergeHead}}}},
    @{name='baseline-object-missing';body={Expect-Rejection {Resolve-Fixture @{LocalDiffBase=('f'*40)}}}},
    @{name='baseline-invalid-sha';body={Expect-Rejection {Resolve-Fixture @{LocalDiffBase='main'}}}},
    @{name='baseline-ci-override';body={Expect-Rejection {Resolve-Fixture @{Enabled=$true;EventName='push';PushBeforeSha=$ancestor;LocalDiffBase=$prBase}}}},
    @{name='baseline-root-without-base';body={Expect-Rejection {Resolve-Fixture @{TargetHead=$ancestor}}}},
    @{name='baseline-not-ancestor';body={Expect-Rejection {Resolve-Fixture @{LocalDiffBase=$future}}}}
)
foreach($case in $baselineCases){Record-Test $case.name $case.body}
$failed=@($results|Where-Object {-not $_.passed}).Count
$report=[pscustomobject]@{schemaVersion=1;scope='workflow-profile-local';workflowSha256=(Get-FileHash $path).Hash
    resultPolicySha256=(Get-FileHash (Join-Path $PSScriptRoot 'CIResultValidation.psm1')).Hash
    testScriptSha256=(Get-FileHash $PSCommandPath).Hash;planned=(3+$mutations.Count+8+2+$contracts.Count+$baselineCases.Count);executed=$results.Count;failed=$failed
    status=$(if($failed){'failed'}else{'passed'});cases=$results.ToArray()}
$result=Join-Path $output 'result.json'
[IO.File]::WriteAllText($result,($report|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Host "Workflow tests result: $result"
if($failed -or $results.Count -ne $report.planned -or $results.Count -eq 0){exit 1}
exit 0
