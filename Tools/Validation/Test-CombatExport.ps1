param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a new output directory.'}
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$out=[IO.Path]::GetFullPath($OutputDirectory);$utf8=[Text.UTF8Encoding]::new($true)
foreach($source in @('server','a','b')){
    $lines=@(for($i=0;$i -lt 40;$i++){
        $event=if($source -eq 'server'){if($i%2){'AttackRejected Reason=Cost Attacker=Player HP=100 Stamina=9'}else{'AttackApproved Attacker=Player HP=100->90 Stamina=100->90'}}else{'AttackInput Actor=Player Target=Dummy'}
        '[2026.11.15-12.00.'+$i.ToString('00')+':'+$(if($source -eq 'server'){'200'}elseif($source -eq 'a'){'100'}else{'300'})+'][1]LogCoopRPGTest: '+$event
    })
    [IO.File]::WriteAllLines("$out/$source.log",$lines,$utf8)
}
$script=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Watch-CombatManual.ps1') -Raw -Encoding UTF8
foreach($module in @('CombatLogReader.psm1','CombatViewerLayout.psm1','CombatLogViews.psm1','CombatLogExport.psm1')){
    $modulePath=(Join-Path $PSScriptRoot $module).Replace("'","''")
    $script=$script.Replace("(Join-Path `$PSScriptRoot '$module')","'$modulePath'")
}
$settingsPath=Join-Path $out 'settings.json'
$checks=@'
$script:qaChecks=New-Object Collections.Generic.List[object];$script:qaExports=New-Object Collections.Generic.List[object];$script:qaPhase=0;$script:qaStarted=[DateTime]::UtcNow;$script:qaFinished=$false
function Check-Export([string]$Name,[bool]$Success){$script:qaChecks.Add([pscustomobject]@{name=$Name;passed=$Success});if(!$Success){throw "Export check failed: $Name"}}
function Keep-Export([string]$Case){
    Check-Export ($Case+' wrote a nonempty file') ($script:lastExport.Succeeded -and (Get-Item -LiteralPath $script:lastExport.Path).Length -gt 0)
    $snapshot=New-ViewerExportSnapshot
    $script:qaExports.Add([pscustomobject]@{case=$Case;path=$script:lastExport.Path;count=$script:lastExport.Count;metadata=$snapshot.Metadata;events=@($snapshot.Rows | ForEach-Object { @{Ordinal=$_.Ordinal;EventId=$_.Event.EventId;Raw=$_.Event.Raw;Summary=$_.Event.Summary;Source=$_.Event.SourceLabel;Fields=($_.Event.Fields | ConvertTo-Json -Compress -Depth 5)} })})
}
function Finish-Export([bool]$Success,[string]$Message){
    $result=@{passed=$Success;count=$script:qaChecks.Count;checks=$script:qaChecks.ToArray();exports=$script:qaExports.ToArray();message=$Message;seconds=([DateTime]::UtcNow-$script:qaStarted).TotalSeconds;basis='Synthetic fixture, actual own WinForms button handlers; not desktop mouse input'}
    [IO.File]::WriteAllText("$logRoot/export-result.json",($result | ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false));$script:qaFinished=$true;$form.Close()
}
$timer.Add_Tick({
    if($script:qaFinished){return}
    try{
        if(([DateTime]::UtcNow-$script:qaStarted).TotalSeconds -gt 120){throw 'Export UI timeout'}
        switch($script:qaPhase){
            0 {
                if($script:store.Count -lt 120){return}
                Check-Export 'export controls present and enabled' ($capture.Visible -and $excel.Visible -and $capture.Enabled -and $excel.Enabled)
                $follow.Checked=$false;$tabs.SelectedIndex=1;$search.Text='AttackApproved';Refresh-Events
                $excel.PerformClick();Keep-Export 'single-approved'
                Check-Export 'single tab and search exported 20 approvals' ($script:lastExport.Count -eq 20)
                Check-Export 'all filtered rows include rows outside viewport' ($script:lastExport.Count -gt $script:activePanes[0].Grid.DisplayedRowCount($false))
                Check-Export 'success message and folder action available' ($script:exportMessage.Contains('XLSX 저장 완료') -and $exportFolder.Enabled)
                $capture.PerformClick();Keep-Export 'single-png'
                Check-Export 'PNG context companion saved' (Test-Path -LiteralPath ($script:lastExport.Path+'.json'))
                $viewChoice.SelectedIndex=1;$countChoice.SelectedItem='4';$displayChoice.SelectedIndex=1;$search.Text='';Refresh-Events
                $excel.PerformClick();Keep-Export 'parallel-all'
                Check-Export 'four panes with integrated source deduplicated' ($script:lastExport.Count -eq 120 -and $script:activePanes.Count -eq 4)
                $capture.PerformClick();Keep-Export 'parallel-png'
                $script:activePanes[1].Selector.SelectedIndex=1;$script:activePanes[2].Selector.SelectedIndex=2;$script:activePanes[3].Selector.SelectedIndex=1;Refresh-Events
                $excel.PerformClick();Keep-Export 'parallel-subset'
                Check-Export 'repeated pane sources export their union once' ($script:lastExport.Count -eq 80)
                $script:activePanes[1].Selector.SelectedIndex=2;$script:activePanes[2].Selector.SelectedIndex=3;$script:activePanes[3].Selector.SelectedIndex=0;Refresh-Events
                $search.Text='no-such-export-event';Refresh-Events;$excel.PerformClick();Keep-Export 'empty'
                Check-Export 'empty filter creates header and context without inventing rows' ($script:lastExport.Count -eq 0)
                $search.Text='';Refresh-Events
                $event=$script:store[0];$script:originalRaw=$event.Raw;$script:originalSummary=$event.Summary
                $event.Summary='=1+1';$event.Raw="=HYPERLINK(`"https://invalid.example/`",`"text`")`r`n한글 & <XML> _x0041_ "+[char]1+' '+[char]0xD83D+[char]0xDE03
                Refresh-Events;$excel.PerformClick();Keep-Export 'text-edge'
                $before=@(Get-ChildItem -LiteralPath $script:exportDirectory -Filter '*.xlsx').Count
                $event.Raw='z'*32768;$excel.PerformClick()
                Check-Export 'overlong Excel cell reports failure without truncating' (!$script:lastExport.Succeeded -and $script:lastExport.Error.Contains('32767'))
                Check-Export 'failed workbook does not leave partial xlsx' (@(Get-ChildItem -LiteralPath $script:exportDirectory -Filter '*.xlsx').Count -eq $before)
                $event.Raw=$script:originalRaw;$event.Summary=$script:originalSummary;Refresh-Events
                $snapshot=New-ViewerExportSnapshot;$existing=$script:qaExports[0].path;$oldHash=(Get-FileHash -LiteralPath $existing).Hash;$rejected=$false
                try{Export-CombatWorkbook -Path $existing -Rows $snapshot.Rows -Metadata $snapshot.Metadata}catch{$rejected=$true}
                Check-Export 'existing output is never overwritten' ($rejected -and (Get-FileHash -LiteralPath $existing).Hash -eq $oldHash)
                $directory=$script:exportDirectory;$blocked=Join-Path $logRoot 'blocked-output';[IO.File]::WriteAllText($blocked,'sentinel')
                $script:exportDirectory=$blocked;$excel.PerformClick()
                Check-Export 'save error remains visible and reader timer recovers' (!$script:lastExport.Succeeded -and $script:exportMessage.StartsWith('저장 실패') -and $timer.Enabled -and [IO.File]::ReadAllText($blocked) -eq 'sentinel')
                $script:exportDirectory=$directory;$pause.Checked=$true
                [IO.File]::AppendAllText("$logRoot/server.log","[2026.11.15-12.01.00:000][1]LogCoopRPGTest: AttackApproved HP=90->80 Stamina=90->80`n",[Text.UTF8Encoding]::new($false))
                $script:qaPhase=1
            }
            1 {
                if($script:collection -lt 121){return}
                $excel.PerformClick();Keep-Export 'paused'
                Check-Export 'paused export uses visible snapshot while collection advances' ($script:lastExport.Count -eq 120 -and $script:collection -eq 121 -and $script:lastShownCollection -eq 120 -and $pause.Checked)
                $pause.Checked=$false;$excel.PerformClick();Keep-Export 'resumed'
                Check-Export 'resume exports pending display update' ($script:lastExport.Count -eq 121)
                $large=New-Object Collections.Generic.List[object]
                for($i=0;$i -lt 3000;$i++){$large.Add([pscustomobject]@{Ordinal=$i+1;Event=$script:store[$i%120]})}
                $watch=[Diagnostics.Stopwatch]::StartNew();Export-CombatWorkbook -Path "$logRoot/volume-3000.xlsx" -Rows $large -Metadata (New-ViewerExportSnapshot).Metadata;$watch.Stop()
                Check-Export '3000 retained rows exported' ((Get-Item "$logRoot/volume-3000.xlsx").Length -gt 0)
                [IO.File]::WriteAllText("$logRoot/volume.json",(@{rows=3000;milliseconds=$watch.Elapsed.TotalMilliseconds}|ConvertTo-Json))
                Check-Export 'export leaves search order and view intact' ($search.Text -eq '' -and $order.SelectedIndex -eq 0 -and $viewChoice.SelectedIndex -eq 1 -and $countChoice.SelectedItem -eq '4' -and $displayChoice.SelectedIndex -eq 1)
                $bFirstLine=Get-Content -LiteralPath "$logRoot/b.log" -Encoding UTF8 -TotalCount 1
                [IO.File]::WriteAllLines("$logRoot/b-rejoined.log",[string[]]@($bFirstLine),[Text.UTF8Encoding]::new($true))
                [IO.File]::WriteAllLines("$logRoot/probe.log",[string[]]@($bFirstLine),[Text.UTF8Encoding]::new($true))
                $oldB=@{id='b';label='클라이언트 2';file='b.log'}
                $manifest=@{
                    previousSources=@($oldB)
                    sources=@(
                        @{id='server';label='서버';file='server.log'}
                        @{id='a';label='클라이언트 1';file='a.log'}
                        @{id='b';label='클라이언트 2';file='b-rejoined.log'}
                        @{id='probe';label='추가 출처';file='probe.log'}
                    )
                }
                $script:qaSelectedSources=@($script:activePanes | ForEach-Object SourceId) -join ','
                $search.Text='AttackInput'
                [IO.File]::WriteAllText("$logRoot/processes.json",($manifest | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
                $script:qaPhase=2
            }
            2 {
                if($script:collection -lt 123){return}
                Check-Export 'live manifest adds an actual tab and selector option' ($tabs.TabPages[4].Tag -eq 'probe' -and $script:panes[1].Selector.Items[4] -eq '추가 출처')
                Check-Export 'live manifest preserves source selections and search' ((@($script:activePanes | ForEach-Object SourceId) -join ',') -eq $script:qaSelectedSources -and $search.Text -eq 'AttackInput')
                $firstB=@($script:store | Where-Object { $_.SourceId -eq 'b' -and $_.ByteOffset -eq 0 })
                Check-Export 'same role and byte offset remain distinct across files' ($firstB.Count -eq 2 -and $firstB[0].EventId -ne $firstB[1].EventId)
                foreach($pane in $script:activePanes){$pane.Selector.SelectedIndex=[Array]::IndexOf($sourceIds,'b')}
                Refresh-Events;$excel.PerformClick();Keep-Export 'reconnected-b'
                Check-Export 'old and new B logs exported once through overlapping panes' ($script:lastExport.Count -eq 41 -and @((New-ViewerExportSnapshot).Rows.Event.SourcePath | Sort-Object -Unique).Count -eq 2)
                Finish-Export $true 'Export checks completed'
            }
        }
    }catch{Finish-Export $false ($_.Exception.Message+' @ '+$_.ScriptStackTrace)}
})
'@
$script=$script.Replace('$form.Add_Shown(',$checks+[Environment]::NewLine+'$form.Add_Shown(')
$runner=Join-Path $out 'ui-runner.ps1';[IO.File]::WriteAllText($runner,$script,$utf8)
$process=Start-Process powershell.exe -WindowStyle Hidden -PassThru -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-STA','-File',('"'+$runner+'"'),'-SettingsPath',('"'+$settingsPath+'"'),'-LogDirectory',('"'+$out+'"'),'-Left','0','-Top','0','-Width','1920','-Height','1000','-ViewMode','Single') -RedirectStandardError "$out/stderr.log" -RedirectStandardOutput "$out/stdout.log"
$process.Id | Set-Content "$out/process.pid"
if(!$process.WaitForExit(150000)){$process.Kill();throw 'Export test process timed out; own test process stopped.'}
if(!(Test-Path "$out/export-result.json")){throw "No export result; inspect $out/stderr.log"}
$result=Get-Content "$out/export-result.json" -Raw -Encoding UTF8 | ConvertFrom-Json
if(!$result.passed){throw $result.message}
Write-Output "PASS $($result.count) export checks. $out/export-result.json"
