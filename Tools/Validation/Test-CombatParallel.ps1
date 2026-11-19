param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a new output directory.'}
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$out=[IO.Path]::GetFullPath($OutputDirectory);$utf8=[Text.UTF8Encoding]::new($true)
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$reference=Join-Path $root 'Artifacts/Validation/v02-cost-cooldown-20261115-165043/final/boundaries-03'
foreach($name in @('server','a','b')){
    $path=Join-Path $reference "$name.log"
    if(!(Test-Path $path)){throw "Missing prior regression fixture: $path"}
    Copy-Item -LiteralPath $path -Destination "$out/$name.log"
}
$script=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Watch-CombatManual.ps1') -Raw
foreach($module in @('CombatLogReader.psm1','CombatViewerLayout.psm1','CombatLogViews.psm1','CombatLogExport.psm1')){
    $modulePath=(Join-Path $PSScriptRoot $module).Replace("'","''")
    $script=$script.Replace("(Join-Path `$PSScriptRoot '$module')","'$modulePath'")
}
$settingsPath=Join-Path $out 'settings.json'
$checks=@'
$script:qaChecks=New-Object Collections.Generic.List[object];$script:qaPhase=0;$script:qaTicks=0;$script:qaFinished=$false;$script:qaStarted=[DateTime]::UtcNow
function Check-UI([string]$Name,[bool]$Success){$script:qaChecks.Add([pscustomobject]@{name=$Name;passed=$Success});if(!$Success){throw "UI check failed: $Name"}}
function Check-SourceHeaderGap {
    $minimumGap = [int][Math]::Ceiling(8 * $form.DeviceDpi / 96.0)
    $gaps = @(foreach ($pane in $script:activePanes) {
        $selectorBottom = $pane.Selector.PointToScreen([Drawing.Point]::Empty).Y + $pane.Selector.Height
        $gridTop = $pane.Grid.PointToScreen([Drawing.Point]::Empty).Y
        $gridTop - $selectorBottom
    })
    $smallestGap = ($gaps | Measure-Object -Minimum).Minimum
    Check-UI "source selector gap at $($fontControl.Value)pt/$($script:activePanes.Count) panes: $($gaps -join ',') px (minimum $minimumGap)" ($smallestGap -ge $minimumGap)
}
function Save-Preview([string]$Name){
    Update-ViewerStatus
    $bitmap=New-Object Drawing.Bitmap($form.Width,$form.Height)
    try{$form.DrawToBitmap($bitmap,(New-Object Drawing.Rectangle(0,0,$form.Width,$form.Height)));$bitmap.Save((Join-Path $logRoot ($Name+'.png')),[Drawing.Imaging.ImageFormat]::Png)}finally{$bitmap.Dispose()}
}
function Finish-UI([bool]$Success,[string]$Message){
    if(!$Success){try{Save-Preview 'failure'}catch{}}
    $result=@{passed=$Success;message=$Message;count=$script:qaChecks.Count;checks=$script:qaChecks.ToArray();basis='Own WinForms controls/events, not desktop mouse input';elapsedSeconds=([DateTime]::UtcNow-$script:qaStarted).TotalSeconds;total=$script:collection;stored=$script:store.Count;evicted=$script:evicted;maxTickMs=$script:maxTickMs;toolbar=@(foreach($g in $script:groups){foreach($c in $g.Tag.Items){@{text=$c.Text;top=$c.Top;height=$c.Height;groupHeight=$g.Height;offset=($c.Top+$c.Height/2.0)-$g.Height/2.0}}})}
    [IO.File]::WriteAllText((Join-Path $logRoot 'ui-result.json'),($result | ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false));$script:qaFinished=$true;$form.Close()
}
function Choose-FirstEvent($Pane,[string]$EventCode){
    $Pane.Grid.ClearSelection()
    for($j=0;$j -lt $Pane.Rows.Count;$j++){if($null -ne $Pane.Rows[$j].Event -and $Pane.Rows[$j].Event.EventCode -eq $EventCode){$Pane.Grid.Rows[$j].Selected=$true;Show-PaneDetail $Pane;return}}
    throw "Missing UI event $EventCode"
}
$timer.Add_Tick({
    if($script:qaFinished){return};$script:qaTicks++
    try{
        if(([DateTime]::UtcNow-$script:qaStarted).TotalSeconds -gt 180){throw 'UI test timeout'}
        switch($script:qaPhase){
            0 {
                if($script:store.Count -lt 300 -or @($sources.Values | Where-Object {$_.Reader.UnreadBytes -gt 0}).Count){return}
                Check-UI 'default time order and single view' ($order.SelectedIndex -eq 0 -and $viewChoice.SelectedIndex -eq 0)
                Check-UI 'all existing result types selectable including observation' ($outcome.Items.Contains('관찰') -and $outcome.Items.Contains('수신') -and $outcome.Items.Contains('제출') -and $outcome.Items.Contains('미제출') -and $outcome.Items.Contains('소비'))
                $centred=$true;foreach($g in $script:groups){foreach($c in $g.Tag.Items){if([Math]::Abs(($c.Top+$c.Height/2.0)-$g.Height/2.0) -gt 1.1){$centred=$false}}}
                Check-UI 'toolbar items vertically centred' $centred
                Check-UI 'toolbar groups carry separators' (@($script:groups | Where-Object {$_.Tag.Line.Width -ne 1 -or $_.Tag.Line.Height -lt 10}).Count -eq 0)
                $outcome.SelectedItem='관찰';Refresh-Events
                Check-UI 'observation result excludes approval and rejection' ($script:common.Count -gt 0 -and @($script:common | Where-Object Outcome -NE '관찰').Count -eq 0)
                $topicButton.PerformClick()
                Check-UI 'category button opens a menu with checked topics and presets' ($topicMenu.Visible -and @(Get-SelectedLogTopics).Count -eq 6 -and $topicPresets.Count -eq 3)
                $topicMenu.Close()
                $topicPresets['공격 흐름'].PerformClick()
                $outcome.SelectedItem='전체'
                Refresh-Events
                Check-UI 'attack preset combines input server verdict and replication' ($script:common.Count -gt 0 -and @($script:common | Where-Object Topic -NotIn @('입력·요청','서버 판정','복제')).Count -eq 0)
                $topicItems['서버 판정'].PerformClick()
                Refresh-Events
                Check-UI 'category click toggles one topic and updates export context' (!$topicItems['서버 판정'].Checked -and @($script:common | Where-Object Topic -EQ '서버 판정').Count -eq 0 -and !( (New-ViewerExportSnapshot).Metadata['카테고리 필터'].Contains('서버 판정')))
                $topicPresets['전체 선택'].PerformClick()
                $outcome.SelectedItem='전체';$tabs.SelectedIndex=2;Refresh-Events
                Check-UI 'single source tabs still filter client 1' ($script:activePanes.Count -eq 1 -and $script:activePanes[0].SourceId -eq 'a' -and @($script:activePanes[0].Rows | Where-Object {$_.Event.SourceId -ne 'a'}).Count -eq 0)
                $fontControl.Value=11;$countChoice.SelectedItem='4';$displayChoice.SelectedIndex=1
                $viewChoice.SelectedIndex=1;Refresh-Events
                Check-UI 'initial switch from single directly to saved four-pane layout' ($script:activePanes.Count -eq 4 -and $script:activePanes[3].Rows.Count -eq $script:common.Count)
                $viewChoice.SelectedIndex=0
                $form.WindowState='Normal';$form.Size=[Drawing.Size]::new(900,480);$fontControl.Value=18
                foreach($count in @('2','3','4')){
                    $countChoice.SelectedItem=$count;$detailToggle.Checked=$false;$viewChoice.SelectedIndex=1
                    $detailToggle.Checked=$true;Refresh-Events
                    $invalid=@($script:activePanes | Where-Object {
                        $_.Split.Panel2Collapsed -or $_.Split.SplitterDistance -lt $_.Split.Panel1MinSize -or
                        $_.Split.SplitterDistance -gt ($_.Split.ClientSize.Height-$_.Split.Panel2MinSize-$_.Split.SplitterWidth)
                    })
                    Check-UI "short window single to $count panes and detail reopen" ($script:activePanes.Count -eq [int]$count -and !$invalid.Count)
                    $viewChoice.SelectedIndex=0
                }
                $probe=New-LogPane 99 'server'
                try{
                    $probe.Container.Size=[Drawing.Size]::new(450,80)
                    $probe.Container.PerformLayout()
                    $requiredHeight=$probe.Split.Panel1MinSize+$probe.Split.Panel2MinSize+$probe.Split.SplitterWidth
                    Check-UI 'split keeps the combined minimum while its container is short' ($probe.Split.ClientSize.Height -ge $requiredHeight)
                }finally{$probe.Container.Dispose()}
                $form.Size=[Drawing.Size]::new(1920,1000);$fontControl.Value=12
                $viewChoice.SelectedIndex=1;$countChoice.SelectedItem='3';$displayChoice.SelectedIndex=1;Refresh-Events
                Check-UI 'three independent list and detail pairs' ($script:activePanes.Count -eq 3 -and @($script:activePanes | Where-Object {$_.Grid -eq $null -or $_.Detail -eq $null}).Count -eq 0)
                Check-UI 'source selectors visible and inside distinct header cells' (@($script:activePanes | Where-Object {!$_.Selector.Visible -or $_.Selector.Right -gt $_.Header.ColumnStyles[0].Width}).Count -eq 0)
                $fontControl.Value=11
                Check-SourceHeaderGap
                Save-Preview 'three-aligned-11pt'
                $fontControl.Value=12
                $rows=$script:common.Count;$correct=$true
                for($j=0;$j -lt $rows;$j++){
                    $events=0;foreach($p in $script:activePanes){if($p.Rows[$j].Basis.EventId -ne $script:common[$j].EventId){$correct=$false};if($null -ne $p.Rows[$j].Event){$events++}}
                    if($events -ne 1){$correct=$false}
                }
                Check-UI 'aligned rows share one event identity and exactly one source' $correct
                $script:activePanes[0].Selector.SelectedIndex=2;Refresh-Events
                Check-UI 'pane source changes without changing its neighbours' ($script:activePanes[0].SourceId -eq 'a' -and $script:activePanes[2].SourceId -eq 'b' -and @($script:activePanes[0].Rows | Where-Object {$null -ne $_.Event -and $_.Event.SourceId -ne 'a'}).Count -eq 0)
                $script:activePanes[0].Selector.SelectedIndex=1
                foreach($number in @('2','4','3')){$countChoice.SelectedItem=$number;Refresh-Events;Check-UI "switch to $number panes" ($script:activePanes.Count -eq [int]$number)}
                $displayChoice.SelectedIndex=0;Refresh-Events
                Check-UI 'compact mode contains no placeholders and keeps every source event' (@($script:activePanes | Where-Object {$_.Rows.Count -ne $_.ShownCount}).Count -eq 0 -and ($script:activePanes | ForEach-Object ShownCount | Measure-Object -Sum).Sum -eq $script:common.Count)
                $follow.Checked=$false;Refresh-Events;$other=$script:activePanes[1].Grid.FirstDisplayedScrollingRowIndex;$script:activePanes[0].Grid.FirstDisplayedScrollingRowIndex=5
                Check-UI 'compact scrolling is independent' ($script:activePanes[1].Grid.FirstDisplayedScrollingRowIndex -eq $other)
                $displayChoice.SelectedIndex=1;Refresh-Events
                $commonTop=$script:activePanes[0].Grid.FirstDisplayedScrollingRowIndex
                Check-UI 'switching from independent scroll restores a common anchor' (@($script:activePanes | Where-Object {$_.Grid.FirstDisplayedScrollingRowIndex -ne $commonTop}).Count -eq 0)
                $script:activePanes[0].Grid.FirstDisplayedScrollingRowIndex=10
                Check-UI 'aligned scrolling moves every pane' (@($script:activePanes | Where-Object {$_.Grid.FirstDisplayedScrollingRowIndex -ne 10}).Count -eq 0)
                $script:activePanes[0].Split.SplitterDistance=300
                Check-UI 'aligned list detail boundary is shared' (@($script:activePanes | Where-Object {$_.Split.SplitterDistance -ne 300}).Count -eq 0)
                Choose-FirstEvent $script:activePanes[0] 'AttackApproved';Choose-FirstEvent $script:activePanes[1] 'AttackInput'
                $script:qaServer=$script:activePanes[0].SelectedId;$script:qaClient=$script:activePanes[1].SelectedId
                Check-UI 'each pane retains its own selected detail' ($script:activePanes[0].Detail.Text.Contains('타격 확정 · 표적 HP') -and $script:activePanes[1].Detail.Text.Contains('공격 입력 감지'))
                $script:qaOldCount=$script:collection
                [IO.File]::AppendAllText("$logRoot/a.log",'[2026.11.14-00.00.00:000][1]LogCoopRPGQa: ParallelLateEvent Value=1'+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
                $script:qaPhase=1
            }
            1 {
                if($script:collection -eq $script:qaOldCount){return}
                Check-UI 'late event sorted into shared first row' ($script:common[0].EventCode -eq 'ParallelLateEvent' -and $script:activePanes[0].Rows[0].Event -eq $null -and $script:activePanes[1].Rows[0].Event.EventCode -eq 'ParallelLateEvent')
                Check-UI 'selected identities survive late insertion' ($script:activePanes[0].SelectedId -eq $script:qaServer -and $script:activePanes[1].SelectedId -eq $script:qaClient)
                $first=$script:activePanes[0].Grid.FirstDisplayedScrollingRowIndex
                Check-UI 'visible anchor remains aligned after late insertion' (@($script:activePanes | Where-Object {$_.Grid.FirstDisplayedScrollingRowIndex -ne $first}).Count -eq 0)
                $pause.Checked=$true;$script:qaFrozen=$script:common.Count;$script:qaOldCount=$script:collection
                Check-UI 'pause locks category filter' (!$topicButton.Enabled)
                Check-UI 'pause locks mode filter and source labels' (!$viewChoice.Enabled -and !$countChoice.Enabled -and !$search.Enabled -and @($script:activePanes | Where-Object {$_.Selector.Enabled}).Count -eq 0)
                [IO.File]::AppendAllText("$logRoot/b.log",'[2026.11.15-10.59.00:000][1]LogCoopRPGQa: ParallelPauseEvent Value=2'+[Environment]::NewLine,[Text.UTF8Encoding]::new($false));$script:qaPhase=2
            }
            2 {
                if($script:collection -eq $script:qaOldCount){return}
                Check-UI 'paused display stable while collection advances' ($script:common.Count -eq $script:qaFrozen -and $script:collection -eq $script:qaOldCount+1)
                $pause.Checked=$false;Refresh-Events
                Check-UI 'resume updates all pane rows together' ($script:common.Count -eq $script:qaFrozen+1 -and @($script:activePanes | Where-Object {$_.Rows.Count -ne $script:common.Count}).Count -eq 0)
                $search.Text='impossible-no-event-qa';Refresh-Events
                Check-UI 'empty filtered view clears stale detail in all panes' (@($script:activePanes | Where-Object {$_.Rows.Count -ne 0 -or $_.SelectedId}).Count -eq 0)
                $search.Text='Attack';$latest.PerformClick();Refresh-Events
                Check-UI 'latest resumes follow for every pane' ($follow.Checked -and !$pause.Checked -and @($script:activePanes | Where-Object {!$_.SelectedId}).Count -eq 0)
                $fontControl.Value=18;Resize-Panes
                Check-UI 'font changes every list detail and row height' (@($script:activePanes | Where-Object {$_.Grid.Font.Size -ne 18 -or $_.Detail.Font.Size -ne 18 -or $_.Grid.Rows[0].Height -ne 43}).Count -eq 0)
                Check-UI 'large font also scales toolbar control widths' ($order.Width -ge 172 -and $displayChoice.Width -ge 195 -and $monitorChoice.Width -ge 375)
                $previousSize=$fontSize;$fontSize=18;$probe=New-LogPane 99 'server'
                Check-UI 'initial pane columns use restored font size' ($probe.Grid.Columns['Stage'].Width -eq 135 -and $probe.Grid.Columns['Time'].Width -eq 159)
                $probe.Container.Dispose();$fontSize=$previousSize
                $form.WindowState='Normal';$form.Size=New-Object Drawing.Size(1000,700);Resize-Panes
                Check-SourceHeaderGap
                Save-Preview 'large-font-narrow'
                $fontControl.Value=12;$countChoice.SelectedItem='2';$displayChoice.SelectedIndex=0;Refresh-Events;Resize-Panes
                Check-UI 'two panes fit a 1000 pixel window at default font' ($table.Width -le $hostPanel.ClientSize.Width -and $script:activePanes.Count -eq 2)
                Check-SourceHeaderGap
                Save-Preview 'two-narrow'
                $form.Size=New-Object Drawing.Size(1920,1000);$countChoice.SelectedItem='3';$displayChoice.SelectedIndex=1;$follow.Checked=$false;Refresh-Events;Resize-Panes
                $script:activePanes[0].Grid.FirstDisplayedScrollingRowIndex=0;Choose-FirstEvent $script:activePanes[0] 'AttackApproved';Choose-FirstEvent $script:activePanes[1] 'AttackInput';Choose-FirstEvent $script:activePanes[2] 'AttackInput';Save-Preview 'three-aligned'
                $countChoice.SelectedItem='4';Refresh-Events;Resize-Panes
                Check-UI 'four panes fit 1920 at default font' ($table.Width -le $hostPanel.ClientSize.Width -and $script:activePanes.Count -eq 4)
                Check-UI 'integrated pane has every event' ($script:activePanes[3].SourceId -eq '' -and $script:activePanes[3].ShownCount -eq $script:common.Count)
                $value=[string]$script:activePanes[3].Grid.Rows[0].Cells['Summary'].Value
                Check-UI 'integrated pane summary identifies source' ($value.StartsWith('['+$script:common[0].SourceLabel+']'))
                Check-SourceHeaderGap
                Save-Preview 'four-aligned';$displayChoice.SelectedIndex=0;Refresh-Events;Save-Preview 'four-compact'
                $viewChoice.SelectedIndex=0;$tabs.SelectedIndex=1;$search.Text='AttackApproved';Refresh-Events
                Check-UI 'return to single view retains original approval count' ($script:activePanes.Count -eq 1 -and $script:activePanes[0].Rows.Count -eq 16)
                $form.Size=New-Object Drawing.Size(1000,700);Resize-Panes;Save-Preview 'single-narrow'
                $viewChoice.SelectedIndex=1;$countChoice.SelectedItem='4';$displayChoice.SelectedIndex=1;$search.Text='';Refresh-Events
                $script:qaVolumeStart=$script:collection
                $lines=[string[]](1..10000 | ForEach-Object {'[2026.11.15-11.59.00:000][1]LogCoopRPGQa: ParallelVolume Index='+$_})
                [IO.File]::AppendAllLines("$logRoot/a.log",$lines,[Text.UTF8Encoding]::new($false));$script:qaPhase=3
            }
            3 {
                if($script:collection -lt $script:qaVolumeStart+10000){return}
                Check-UI 'ten thousand appended events collected without duplicates' ($script:collection -eq $script:qaVolumeStart+10000)
                Check-UI 'capacity is global rather than multiplied by panes' ($script:store.Count -eq 3000 -and $script:evicted -eq $script:collection-3000)
                Check-UI 'evicted selection is cleared rather than reassigned' (!$script:activePanes[0].SelectedId -and !$script:activePanes[1].SelectedId)
                Check-UI 'four aligned projections remain equal after eviction' (@($script:activePanes | Where-Object {$_.Rows.Count -ne 3000}).Count -eq 0 -and $script:activePanes[0].ShownCount -eq 0 -and $script:activePanes[1].ShownCount -eq 3000 -and $script:activePanes[2].ShownCount -eq 0 -and $script:activePanes[3].ShownCount -eq 3000)
                $displayChoice.SelectedIndex=0;Refresh-Events
                Check-UI 'compact mode removes placeholders after eviction' ($script:activePanes[0].Rows.Count -eq 0 -and $script:activePanes[1].Rows.Count -eq 3000 -and $script:activePanes[2].Rows.Count -eq 0 -and $script:activePanes[3].Rows.Count -eq 3000)
                Finish-UI $true 'Parallel UI checks completed'
            }
        }
    }catch{Finish-UI $false ($_.Exception.Message+' @ '+$_.ScriptStackTrace)}
})
'@
$script=$script.Replace('$form.Add_Shown(',$checks+[Environment]::NewLine+'$form.Add_Shown(')
$runner=Join-Path $out 'ui-runner.ps1';[IO.File]::WriteAllText($runner,$script,$utf8)
$arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-STA','-File',('"'+$runner+'"'),'-SettingsPath',('"'+$settingsPath+'"'),'-LogDirectory',('"'+$out+'"'),'-Left','0','-Top','0','-Width','1920','-Height','1000','-ViewMode','Single','-PaneCount','3','-DisplayMode','Compact')
$process=Start-Process powershell.exe -WindowStyle Hidden -PassThru -ArgumentList $arguments -RedirectStandardError "$out/stderr.log" -RedirectStandardOutput "$out/stdout.log"
$process.Id | Set-Content "$out/process.pid"
if(!$process.WaitForExit(210000)){$process.Kill();throw 'Parallel UI test process timed out; own test process stopped.'}
if(!(Test-Path "$out/ui-result.json")){throw "No UI result. Inspect $out/stderr.log"}
$result=Get-Content "$out/ui-result.json" -Raw -Encoding UTF8 | ConvertFrom-Json
if(!$result.passed){throw $result.message}
$settings=Get-Content "$out/settings.json" -Raw -Encoding UTF8 | ConvertFrom-Json
if($settings.ViewMode -ne 'Parallel' -or $settings.PaneCount -ne 4 -or $settings.DisplayMode -ne 'Compact' -or $settings.PaneSources.Count -ne 4){throw 'View preferences were not persisted'}
Write-Output "PASS $($result.count) UI checks. $out/ui-result.json"
