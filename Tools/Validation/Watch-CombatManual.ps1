param(
    [Parameter(Mandatory)][string]$LogDirectory,
    [int]$Left=80,[int]$Top=80,[int]$Width=1500,[int]$Height=800,
    [string]$Monitor='', [switch]$Maximized, [switch]$Docked,
    [switch]$AlwaysOnTop,
    [string]$PreviewPath='',
    [ValidateSet('Single','Parallel')][string]$ViewMode='Single',
    [ValidateRange(2,4)][int]$PaneCount=3,
    [ValidateSet('Compact','Aligned')][string]$DisplayMode='Compact',
    [ValidateRange(100,20000)][int]$Capacity=3000,
    [string]$SettingsPath
)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'CombatLogReader.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'CombatLogViews.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'CombatLogExport.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'CombatViewerLayout.psm1') -Force
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
$logRoot=[IO.Path]::GetFullPath($LogDirectory)
if(!(Test-Path -LiteralPath $logRoot -PathType Container)){throw "Missing log directory: $logRoot"}
if(!$SettingsPath){$SettingsPath=Get-CombatViewerSettingsPath}
$settings=Read-CombatViewerSettings -Path $SettingsPath
$monitors=@(Get-CombatMonitors)
$choice=if($Monitor){$Monitor}elseif($settings.LogMonitor){$settings.LogMonitor}else{''}
$chosen=Find-CombatMonitor $monitors $choice
if(!$chosen){$chosen=if($monitors.Count -gt 1){$monitors[1]}else{$monitors[0]}}
$explicitBounds=$PSBoundParameters.ContainsKey('Left') -or $PSBoundParameters.ContainsKey('Top') -or $PSBoundParameters.ContainsKey('Width') -or $PSBoundParameters.ContainsKey('Height')
if(!$explicitBounds){$Left=$chosen.Left+20;$Top=$chosen.Top+20;$Width=[Math]::Max(800,$chosen.Width-40);$Height=[Math]::Max(360,$chosen.Height-40);$Maximized=$true}
# Clamp a saved/explicit rectangle onto an available monitor; never reopen offscreen.
$rect=New-Object Drawing.Rectangle($Left,$Top,$Width,$Height)
$overlap=@($monitors | Where-Object { $_.Left -lt $rect.Right -and $_.Right -gt $rect.Left -and $_.Top -lt $rect.Bottom -and $_.Bottom -gt $rect.Top })
if(!$overlap.Count){$Left=$chosen.Left+12;$Top=$chosen.Top+12;$Width=[Math]::Min($Width,$chosen.Width-24);$Height=[Math]::Min($Height,$chosen.Height-24)}
$fontSize=12
if($settings.FontSize){$fontSize=[Math]::Max(10,[Math]::Min(18,[double]$settings.FontSize))}
$runId=Split-Path $logRoot -Leaf
$manifestPath=Join-Path $logRoot 'processes.json'
$logPrefix=$logRoot.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
$script:manifest=$null
$script:lastManifestText=$null
$script:sourceSpecs=@()
$sources=@{}
$readers=[ordered]@{}
$sourceLabelById=@{}

function Read-ViewerManifestText {
    if(![IO.File]::Exists($manifestPath)){return}
    # Allow the launcher to replace the manifest while the viewer reads a snapshot.
    $share=[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
    $stream=[IO.File]::Open($manifestPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,$share)
    try{
        $reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8)
        try{$reader.ReadToEnd()}finally{$reader.Dispose()}
    }finally{$stream.Dispose()}
}

function Set-ViewerSources($Active,$Previous,[switch]$FollowFromStart) {
    $entries=@(
        foreach($isCurrent in @($false,$true)){
            $specs=if($isCurrent){$Active}else{$Previous}
            foreach($spec in $specs){
                if(!$spec.id -or !$spec.file){throw 'The process manifest contains an incomplete log source.'}
                $path=[IO.Path]::GetFullPath((Join-Path $logRoot $spec.file))
                if(!$path.StartsWith($logPrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Source log must be inside the selected run directory.'}
                $fileKey=[uri]::EscapeDataString($path.Substring($logPrefix.Length).Replace('\','/').ToUpperInvariant())
                @{Spec=$spec;Path=$path;FileKey=$fileKey;Current=$isCurrent}
            }
        }
    )
    $sources.Clear()
    foreach($entry in $entries){
        $spec=$entry.Spec
        $key='{0}/{1}' -f $spec.id,$entry.FileKey
        if(!$readers.Contains($key)){
            $reader=New-CombatLogReader $entry.Path
            if($FollowFromStart){$reader.Initialized=$true}
            $readers[$key]=@{Spec=$spec;FileKey=$entry.FileKey;Reader=$reader;Ignored=0;LastEvent=$null;ProcessState='확인 대기';Drained=$false}
        }
        $source=$readers[$key];$source.Spec=$spec
        $sourceLabelById[$spec.id]=[string]$spec.label
        if($entry.Current){$sources[$spec.id]=$source}
    }
    $script:sourceSpecs=@($Active)
}

try{
    $script:lastManifestText=Read-ViewerManifestText
    if($script:lastManifestText){$script:manifest=$script:lastManifestText | ConvertFrom-Json}
}catch{Write-Warning 'Process manifest unreadable; log viewing still works.'}
$initialSpecs=if($manifest -and $manifest.sources){$manifest.sources}else{
    foreach($definition in @(@('server','서버','server.log','CoopRPGServer'),@('a','클라이언트 1','a.log','CoopRPGClient'),@('b','클라이언트 2','b.log','CoopRPGClient'))){
        $id=$definition[0];$processId=0
        if($manifest -and $manifest.processes){$value=$manifest.processes.PSObject.Properties[$id];if($value){$processId=[int]$value.Value}}
        [pscustomobject]@{id=$id;label=$definition[1];file=$definition[2];processId=$processId;processName=$definition[3];startedUtc=''}
    }
}
Set-ViewerSources @($initialSpecs) $manifest.previousSources
$script:store=New-Object Collections.Generic.List[object]
$script:clientByPlayerId=@{}
$script:common=@();$script:collection=0L;$script:evicted=0;$script:renderDirty=$true;$script:refreshing=$false;$script:settingsError=''
$script:lastProcessCheck=[DateTime]::MinValue;$script:lastDiagnostic=[DateTime]::MinValue
$script:previewAttempted=$false;$script:renderCount=0;$script:maxTickMs=0.0;$script:tickCount=0;$script:tickMs=0.0;$script:lastShownCollection=0L
$script:scrollSync=$false;$script:splitSync=$false;$script:layoutBusy=$false;$script:tabSync=$false;$script:activePane=$null;$script:splitRatio=0.64
$script:panes=@();$script:activePanes=@();$script:groups=@()
$script:exportDirectory=Join-Path $logRoot 'Exports';$script:exportMessage='내보내기: 창 캡처 또는 Excel 저장';$script:lastExport=$null;$script:lastRenderedUtc=''
$script:sourceIds=@('')+@($sourceSpecs | ForEach-Object id)
$script:sourceLabels=@('통합')+@($sourceSpecs | ForEach-Object label)
$script:sourceChoicesBusy=$false
if(!$PSBoundParameters.ContainsKey('ViewMode') -and $settings.ViewMode -in @('Single','Parallel')){$ViewMode=$settings.ViewMode}
if(!$PSBoundParameters.ContainsKey('PaneCount') -and $settings.PaneCount -in @(2,3,4)){$PaneCount=[int]$settings.PaneCount}
if(!$PSBoundParameters.ContainsKey('DisplayMode') -and $settings.DisplayMode -in @('Compact','Aligned')){$DisplayMode=$settings.DisplayMode}

$form=New-Object Windows.Forms.Form
$form.Text='CoopRPG | 검증 로그';$form.StartPosition='Manual';$form.AutoScaleMode='Dpi'
$form.Location=New-Object Drawing.Point($Left,$Top);$form.Size=New-Object Drawing.Size($Width,$Height)
$form.MinimumSize=New-Object Drawing.Size(900,480);$form.TopMost=[bool]$AlwaysOnTop
$form.BackColor=[Drawing.Color]::FromArgb(24,28,36);$form.ForeColor=[Drawing.Color]::Gainsboro
$form.Font=New-Object Drawing.Font('Malgun Gothic',$fontSize)
if($Maximized){$form.WindowState='Maximized'}
$header=New-Object Windows.Forms.Panel;$header.Dock='Top';$header.Height=57
$heading=New-Object Windows.Forms.Label;$heading.Text='CoopRPG 검증 로그';$heading.AutoSize=$true;$heading.Location=New-Object Drawing.Point(12,5);$heading.Font=New-Object Drawing.Font('Malgun Gothic',15,[Drawing.FontStyle]::Bold)
$subtitle=New-Object Windows.Forms.Label;$subtitle.Text="실행 $runId  ·  읽기 전용  ·  요청 관계 미계측";$subtitle.AutoSize=$true;$subtitle.Location=New-Object Drawing.Point(14,34);$subtitle.Font=New-Object Drawing.Font('Malgun Gothic',9)
$header.Controls.AddRange(@($heading,$subtitle))
function New-Bar {
    $p=New-Object Windows.Forms.FlowLayoutPanel;$p.Dock='Top';$p.AutoSize=$true;$p.AutoSizeMode='GrowAndShrink';$p.Padding=New-Object Windows.Forms.Padding(8,1,8,1);$p.WrapContents=$true;return $p
}
function New-Label([string]$Text){$l=New-Object Windows.Forms.Label;$l.Text=$Text;$l.AutoSize=$true;return $l}
function New-Button([string]$Text){$b=New-Object Windows.Forms.Button;$b.Text=$Text;$b.AutoSize=$false;$b.BackColor=[Drawing.Color]::FromArgb(46,54,68);$b.ForeColor=[Drawing.Color]::White;$b.FlatStyle='Flat';$b.FlatAppearance.BorderColor=[Drawing.Color]::FromArgb(75,84,100);return $b}
function New-Combo($Items,[int]$Width){$c=New-Object Windows.Forms.ComboBox;$c.DropDownStyle='DropDownList';$c.Width=$Width;foreach($item in $Items){[void]$c.Items.Add($item)};if($c.Items.Count){$c.SelectedIndex=0};return $c}
function New-Check([string]$Text,[bool]$Value){$c=New-Object Windows.Forms.CheckBox;$c.Text=$Text;$c.AutoSize=$true;$c.Checked=$Value;return $c}
function Add-Group($Bar,$Controls){
    $group=New-Object Windows.Forms.Panel;$group.Margin=New-Object Windows.Forms.Padding(0,0,10,0)
    $group.Controls.AddRange([Windows.Forms.Control[]]$Controls)
    $line=New-Object Windows.Forms.Panel;$line.Width=1;$line.BackColor=[Drawing.Color]::FromArgb(73,82,97);$group.Controls.Add($line)
    $line.Visible=$false
    if($Bar.Controls.Count){$Bar.Controls[$Bar.Controls.Count-1].Tag.Line.Visible=$true}
    $group.Tag=@{Items=$Controls;Line=$line};$script:groups+=,$group;$Bar.Controls.Add($group)
}
function Align-Toolbars {
    # All groups share one row height. Each control is vertically centred in that row.
    $scale=[float]$fontControl.Value/12.0;$height=[int][Math]::Ceiling(40*$scale)
    foreach($group in $script:groups){
        $x=2
        foreach($c in $group.Tag.Items){
            if($c -is [Windows.Forms.Button]){$c.Height=[int][Math]::Ceiling(30*$scale);$c.Width=[Windows.Forms.TextRenderer]::MeasureText($c.Text,$c.Font).Width+18}
            elseif($c -is [Windows.Forms.Label] -or $c -is [Windows.Forms.CheckBox]){$c.Size=$c.PreferredSize}
            else{if(!$c.Tag){$c.Tag=@{BaseWidth=$c.Width}};$c.Width=[int]([int]$c.Tag.BaseWidth*$scale)}
            $c.Location=New-Object Drawing.Point($x,[int][Math]::Floor(($height-$c.Height)/2));$x+=$c.Width+7
        }
        $group.Size=New-Object Drawing.Size(($x+6),$height)
        $group.Tag.Line.Location=New-Object Drawing.Point(($x+3),7);$group.Tag.Line.Height=$height-14
    }
}
$filters=New-Bar;$options=New-Bar
$search=New-Object Windows.Forms.TextBox;$search.Width=200
$outcome=New-Combo @('전체','관찰','승인','거절','실패','중단','취소','빗나감','호출','수신','제출','미제출','소비','오류·경고','미분류') 115
$order=New-Combo @('시각순','수집순') 115
$order.AccessibleName='정렬'
$orderTooltip=New-Object Windows.Forms.ToolTip
$orderTooltip.SetToolTip($order,'시각과 수집순은 표시 순서이며 인과관계를 뜻하지 않습니다.')
$pause=New-Check '일시정지' $false;$follow=New-Check '최신 따라가기' $true;$latest=New-Button '최신으로'
$detailToggle=New-Check '상세·원문' (!$Docked)
Add-Group $filters @((New-Label '검색'),$search,(New-Label '정렬'),$order,(New-Label '결과'),$outcome)
$topicButton = New-Button '카테고리 ▾'
$topicMenu = New-Object Windows.Forms.ContextMenuStrip
$topicItems = [ordered]@{}
$topicPresets = [ordered]@{}
$topicNames = @('접속·초기화','입력·요청','서버 판정','복제','오류·경고','상태·기타')
$topicTooltip = New-Object Windows.Forms.ToolTip
$script:keepTopicMenuOpen = $false

function Get-SelectedLogTopics {
    foreach ($item in $topicItems.Values) {
        if ($item.Checked) { $item.Text }
    }
}

function Update-TopicFilter {
    $selected = @(Get-SelectedLogTopics)
    $description = if ($selected.Count) { $selected -join ', ' } else { '선택 없음' }
    $topicButton.Text = if ($selected.Count -eq $topicNames.Count) { '카테고리 ▾' } else { "카테고리 $($selected.Count)/$($topicNames.Count) ▾" }
    $topicTooltip.SetToolTip($topicButton,$description)
    # The paused display retains the filter selection shown in its snapshot.
    $script:renderDirty = $true
    Align-Toolbars
}

$presetTopics = [ordered]@{
    '전체 선택' = $topicNames
    '접속 흐름' = @('접속·초기화')
    '공격 흐름' = @('입력·요청','서버 판정','복제')
}
foreach ($preset in $presetTopics.GetEnumerator()) {
    $item = [Windows.Forms.ToolStripMenuItem]::new($preset.Key)
    $item.Tag = $preset.Value
    $item.Add_Click({
        param($sender,$eventArgs)
        foreach ($topic in $topicItems.Values) { $topic.Checked = $topic.Text -in $sender.Tag }
        Update-TopicFilter
    })
    $topicPresets[$preset.Key] = $item
    [void]$topicMenu.Items.Add($item)
}
[void]$topicMenu.Items.Add([Windows.Forms.ToolStripSeparator]::new())
foreach ($topicName in $topicNames) {
    $item = [Windows.Forms.ToolStripMenuItem]::new($topicName)
    $item.Checked = $true
    $item.CheckOnClick = $true
    $item.Add_Click({ Update-TopicFilter })
    $topicItems[$topicName] = $item
    [void]$topicMenu.Items.Add($item)
}
$topicMenu.Add_ItemClicked({
    param($sender,$eventArgs)
    $script:keepTopicMenuOpen = $eventArgs.ClickedItem -is [Windows.Forms.ToolStripMenuItem] -and $eventArgs.ClickedItem.CheckOnClick
})
$topicMenu.Add_Closing({
    param($sender,$eventArgs)
    if ($script:keepTopicMenuOpen -and $eventArgs.CloseReason -eq 'ItemClicked') { $eventArgs.Cancel = $true }
    $script:keepTopicMenuOpen = $false
})
$topicButton.Add_Click({ $topicMenu.Show($topicButton, [Drawing.Point]::new(0, $topicButton.Height)) })
Add-Group $filters @($topicButton)
Add-Group $filters @((New-Label '갱신'),$pause,$follow,$latest)
Add-Group $filters @($detailToggle)
$viewChoice=New-Combo @('단일','병렬') 80;$viewChoice.SelectedIndex=if($ViewMode -eq 'Parallel'){1}else{0}
$countChoice=New-Combo @('2','3','4') 48;$countChoice.SelectedItem=[string]$PaneCount
$displayChoice=New-Combo @('모아 보기','공통 행 비교') 130;$displayChoice.SelectedIndex=if($DisplayMode -eq 'Aligned'){1}else{0}
$monitorChoice=New-Combo @($monitors | ForEach-Object Label) 250
for($i=0;$i -lt $monitors.Count;$i++){if($monitors[$i].DeviceName -eq $chosen.DeviceName){$monitorChoice.SelectedIndex=$i}}
$displayChoice.AccessibleName='비교'
$monitorChoice.AccessibleName='화면'
$move=New-Button '이동';$move.AccessibleName='로그 창 이동'
$pin=New-Check '항상 위' ([bool]$AlwaysOnTop)
$fontControl=New-Object Windows.Forms.NumericUpDown;$fontControl.Minimum=10;$fontControl.Maximum=18;$fontControl.Value=$fontSize;$fontControl.Width=53
$copy=New-Button '선택 복사';$folder=New-Button '경로 복사'
Add-Group $options @((New-Label '보기'),$viewChoice,(New-Label '분할'),$countChoice,(New-Label '비교'),$displayChoice)
Add-Group $options @((New-Label '화면'),$monitorChoice,$move,$pin,(New-Label '글자'),$fontControl)
Add-Group $options @($copy,$folder)
$capture=New-Button '창 캡처';$excel=New-Button 'Excel 저장';$exportFolder=New-Button '저장 폴더'
$exportFolder.Enabled=Test-Path -LiteralPath $script:exportDirectory -PathType Container
$exportTooltip=New-Object Windows.Forms.ToolTip
$exportTooltip.SetToolTip($capture,'현재 창에 보이는 화면을 PNG와 상태 JSON으로 저장')
$exportTooltip.SetToolTip($excel,'현재 필터·선택 출처의 목록 전체를 XLSX로 저장. 스크롤 밖 행 포함, 중복·빈칸 제외')
Add-Group $options @($capture,$excel,$exportFolder)
$tabs=New-Object Windows.Forms.TabControl;$tabs.Dock='Top';$tabs.Height=35
for($i=0;$i -lt $sourceIds.Count;$i++){$t=New-Object Windows.Forms.TabPage;$t.Text=$sourceLabels[$i];$t.Tag=$sourceIds[$i];[void]$tabs.TabPages.Add($t)}
$hostPanel=New-Object Windows.Forms.Panel;$hostPanel.Dock='Fill';$hostPanel.AutoScroll=$true
$table=New-Object Windows.Forms.TableLayoutPanel;$table.Location=New-Object Drawing.Point(0,0);$table.RowCount=1;$table.ColumnCount=1;$table.Margin=New-Object Windows.Forms.Padding(0);[void]$table.RowStyles.Add((New-Object Windows.Forms.RowStyle('Percent',100)));$hostPanel.Controls.Add($table)
$status=New-Object Windows.Forms.Label;$status.Dock='Bottom';$status.Height=125;$status.Padding=New-Object Windows.Forms.Padding(10,5,10,3);$status.Font=New-Object Drawing.Font('Malgun Gothic',9);$status.AutoEllipsis=$true
$form.Controls.Add($hostPanel);$form.Controls.Add($tabs);$form.Controls.Add($options);$form.Controls.Add($filters);$form.Controls.Add($header);$form.Controls.Add($status)

function Test-Aligned {return $viewChoice.SelectedIndex -eq 1 -and $displayChoice.SelectedIndex -eq 1}
function Get-RequesterLabel($Event) {
    $playerId = [string]$Event.Fields['RequesterPlayerId']
    if ($playerId -notmatch '^[1-9][0-9]*$') { return '미계측' }
    $sourceId = $script:clientByPlayerId[$playerId]
    if (!$sourceId) { return "출처 미확인 (PlayerId: $playerId)" }
    return "$($sources[$sourceId].Spec.label) (PlayerId: $playerId)"
}

function Show-PaneDetail($Pane){
    $grid=$Pane.Grid
    if($grid.SelectedRows.Count -eq 0){$Pane.Detail.Text='행을 선택하면 상세와 원문을 확인할 수 있습니다.';$Pane.SelectedId='';return}
    $index=$grid.SelectedRows[0].Index
    if($index -ge $Pane.Rows.Count){return}
    $row=$Pane.Rows[$index];$e=$row.Event
    if($null -eq $e){$Pane.SelectedId='';$Pane.Detail.Text="공통 행 $($row.Ordinal) · $($row.Basis.SourceLabel)의 사건`r`n이 칸의 출처가 아닙니다. 빈칸은 미수신이나 오류를 뜻하지 않습니다.";return}
    $Pane.SelectedId = $e.EventId
    $utc = if ($e.UtcTime) { $e.UtcTime.ToString('o') } else { '미기록' }
    $stage = if ($e.Stage -eq '복제 관찰') { '복제 적용' } else { $e.Stage }
    $showOutcome = $e.Stage -ne '복제 관찰' -or $e.Outcome -ne '관찰'
    $hasRequester = $e.SourceId -eq 'server' -and ($e.Stage -eq '서버 판정' -or $e.Fields.ContainsKey('RequesterPlayerId'))

    $Pane.Detail.Lines = [string[]]@(
        "[$($e.SourceLabel) · $($e.Time) · 행 $($row.Ordinal)]"
        $e.Summary
        if ($e.HealthSubject) { "체력 소유자: $($e.HealthSubject)" }
        if ($e.EventCode -eq 'AttackStarted' -and $e.Fields['Stamina'] -and $e.Fields['Windup']) {
            "피해 확정 대기 $($e.Fields['Windup'])초"
        }
        if ($e.EventCode -eq 'AttackApproved' -and $e.Fields['Stamina']) {
            "이전 형식의 Commit 비용 요약 · 공격자 스태미나 $($e.Fields['Stamina'] -replace '->', ' → ')"
        }
        if ($showOutcome) { "단계: $stage    결과: $($e.Outcome)" } else { "단계: $stage" }
        "관련 플레이어: $(Get-CombatRelatedPlayerLabel $e $script:clientByPlayerId $sourceLabelById)"
        "대상 객체: $(Get-CombatTargetLabel $e)"
        if ($hasRequester -or $e.Actor) {
            ''
            if ($hasRequester) { "요청자: $(Get-RequesterLabel $e)" }
            if ($e.Actor) {
                "행위자: $($e.Actor)"
                $role = if ($e.Fields.ContainsKey('Actor') -or $e.Fields.ContainsKey('Attacker')) { $e.Fields['LocalRole'] }
                if ($role) { "행위자 역할: $role" } else { '행위자 역할: 미계측' }
            }
        }
        ''
        "이벤트: $($e.EventCode)"
        if ($e.Fields['Func']) { "기록 함수: $($e.Fields['Func'])" }
        $hasDistance = $e.Stage -eq '서버 판정' -and $e.Fields['Distance'] -and $e.Fields['Range']
        if ($e.Target -or $hasDistance) {
            ''
            if ($e.Target) { "대상: $($e.Target)" }
            if ($hasDistance) { "거리: $($e.Fields['Distance']) cm / 허용 $($e.Fields['Range']) cm" }
        }
        ''
        if ($e.Category) { "로그: $($e.Category) · $($e.Level)" } else { "로그 수준: $($e.Level)" }
        "분류: $($e.Topic)"
        ''
        '추적 정보'
        "EventId: $($e.EventId)"
        "UTC: $utc"
        "원본 위치: $($e.SourcePath) @ byte $($e.ByteOffset)"
        if ($e.RequestId) { "요청 ID: $($e.RequestId) · $($e.Relationship)" } else { '요청 추적: 미계측' }
        ''
        '원문'
        $e.Raw
    )
}
function Set-PaneSplitterDistance($Split,[int]$Distance) {
    if($Split.Panel2Collapsed){return}
    $minimum=$Split.Panel1MinSize
    $maximum=$Split.ClientSize.Height-$Split.Panel2MinSize-$Split.SplitterWidth
    if($minimum -gt $maximum){return}
    $distanceInBounds=[Math]::Max($minimum,[Math]::Min($Distance,$maximum))
    if($Split.SplitterDistance -ne $distanceInBounds){$Split.SplitterDistance=$distanceInBounds}
}
function New-LogPane([int]$Index,[string]$SourceId){
    $container=New-Object Windows.Forms.Panel;$container.Font=$form.Font;$container.Dock='Fill';$container.Margin=New-Object Windows.Forms.Padding(2,0,4,0)
    $head=New-Object Windows.Forms.TableLayoutPanel;$head.Dock='Top';$head.Height=38;$head.ColumnCount=2;$head.RowCount=3
    [void]$head.RowStyles.Add([Windows.Forms.RowStyle]::new('Absolute', 8))
    [void]$head.RowStyles.Add([Windows.Forms.RowStyle]::new('Percent', 100))
    [void]$head.RowStyles.Add([Windows.Forms.RowStyle]::new('Absolute', 8))
    [void]$head.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Absolute',170)));[void]$head.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Percent',100)))
    $selector=New-Combo $sourceLabels 160;$selector.Anchor='Top, Left';$selector.Margin=New-Object Windows.Forms.Padding(5,0,5,0)
    $label=New-Object Windows.Forms.Label;$label.AutoSize=$false;$label.AutoEllipsis=$true;$label.TextAlign='MiddleLeft';$label.Dock='Fill'
    $head.Controls.Add($selector,0,1);$head.Controls.Add($label,1,1)
    $split=New-Object Windows.Forms.SplitContainer;$split.Size=New-Object Drawing.Size(600,600);$split.Dock='Fill';$split.Orientation='Horizontal';$split.Panel1MinSize=55;$split.Panel2MinSize=60;$split.SplitterWidth=6;$split.BackColor=[Drawing.Color]::FromArgb(61,69,82)
    $split.MinimumSize=[Drawing.Size]::new(0,($split.Panel1MinSize+$split.Panel2MinSize+$split.SplitterWidth))
    $grid=New-Object Windows.Forms.DataGridView;$grid.Dock='Fill';$grid.VirtualMode=$true;$grid.ReadOnly=$true;$grid.AllowUserToAddRows=$false;$grid.AllowUserToDeleteRows=$false;$grid.AllowUserToResizeRows=$false;$grid.AllowUserToOrderColumns=$false;$grid.RowHeadersVisible=$false;$grid.MultiSelect=$false;$grid.SelectionMode='FullRowSelect';$grid.AutoGenerateColumns=$false;$grid.BackgroundColor=$form.BackColor;$grid.BorderStyle='None';$grid.EnableHeadersVisualStyles=$false;$grid.ScrollBars='Both'
    $grid.ColumnHeadersDefaultCellStyle.BackColor=[Drawing.Color]::FromArgb(40,47,60);$grid.ColumnHeadersDefaultCellStyle.ForeColor=[Drawing.Color]::Gainsboro
    $grid.DefaultCellStyle.BackColor=[Drawing.Color]::FromArgb(27,32,42);$grid.DefaultCellStyle.ForeColor=[Drawing.Color]::Gainsboro;$grid.DefaultCellStyle.SelectionBackColor=[Drawing.Color]::FromArgb(49,68,95);$grid.DefaultCellStyle.SelectionForeColor=[Drawing.Color]::White
    $grid.AlternatingRowsDefaultCellStyle.BackColor=[Drawing.Color]::FromArgb(31,37,48);$grid.GridColor=[Drawing.Color]::FromArgb(47,54,66);$grid.CellBorderStyle='SingleHorizontal';$grid.ColumnHeadersHeightSizeMode='DisableResizing';$grid.ColumnHeadersHeight=[int]($fontSize*2.5);$grid.RowTemplate.Height=[int]($fontSize*2.4)
    foreach($def in @(@('Ordinal','행',38),@('Time','시각',106),@('SourceLabel','관측 출처',124),@('RelatedPlayer','관련 플레이어',124),@('TargetObject','대상 객체',94),@('Stage','단계',90),@('Outcome','결과',64),@('Summary','사건 · 관측값',180))){
        $col=New-Object Windows.Forms.DataGridViewTextBoxColumn;$col.Name=$def[0];$col.HeaderText=$def[1];$col.Width=[int]([int]$def[2]*$fontSize/12.0);$col.Tag=[int]$def[2];$col.SortMode='NotSortable'
        if($def[0] -eq 'Summary'){$col.AutoSizeMode='Fill';$col.MinimumWidth=100};[void]$grid.Columns.Add($col)
    }
    $detail=New-Object Windows.Forms.TextBox;$detail.Dock='Fill';$detail.Multiline=$true;$detail.ReadOnly=$true;$detail.ScrollBars='Both';$detail.WordWrap=$false;$detail.BackColor=[Drawing.Color]::FromArgb(20,24,31);$detail.ForeColor=[Drawing.Color]::Gainsboro;$detail.BorderStyle='None';$detail.Font=New-Object Drawing.Font('Consolas',$fontSize)
    $split.Panel1.Controls.Add($grid);$split.Panel2.Controls.Add($detail);$split.Panel2Collapsed=!$detailToggle.Checked
    $container.Controls.Add($split);$container.Controls.Add($head)
    $pane=@{Index=$Index;Container=$container;Header=$head;Selector=$selector;Label=$label;Split=$split;Grid=$grid;Detail=$detail;SourceId=$SourceId;Rows=@();SelectedId='';ShownCount=0}
    $grid.Tag=$pane;$selector.Tag=$pane;$split.Tag=$pane
    $idx=[Array]::IndexOf($sourceIds,$SourceId);$selector.SelectedIndex=[Math]::Max(0,$idx)
    $selector.Add_SelectedIndexChanged({param($sender,$e)
        if($script:sourceChoicesBusy -or $sender.SelectedIndex -lt 0){return}
        $p=$sender.Tag;$p.SourceId=$sourceIds[$sender.SelectedIndex];$p.SelectedId='';$p.Grid.ClearSelection();$script:activePane=$p;$script:renderDirty=$true
        if($p.Index -eq 0 -and !$script:tabSync){$script:tabSync=$true;try{$tabs.SelectedIndex=$sender.SelectedIndex}finally{$script:tabSync=$false}}
    })
    $grid.Add_CellValueNeeded({param($sender,$e)
        $p=$sender.Tag;if($e.RowIndex -ge $p.Rows.Count){return};$row=$p.Rows[$e.RowIndex];$field=$sender.Columns[$e.ColumnIndex].Name
        if($field -eq 'Ordinal'){$e.Value=$row.Ordinal}elseif($field -eq 'Time'){$e.Value=$row.Basis.Time}elseif($null -ne $row.Event){
            $e.Value=switch($field){
                'RelatedPlayer' {Get-CombatRelatedPlayerLabel $row.Event $script:clientByPlayerId $sourceLabelById}
                'TargetObject' {Get-CombatTargetLabel $row.Event}
                'Summary' {
                    if(!$p.SourceId -and $viewChoice.SelectedIndex -eq 1){'['+$row.Event.SourceLabel+'] '+$row.Event.Summary}
                    else{$row.Event.Summary}
                }
                default {$row.Event.$field}
            }
        }else{$e.Value=''}
    })
    $grid.Add_CellFormatting({param($sender,$e)
        $p=$sender.Tag;if($e.RowIndex -ge $p.Rows.Count){return};$event=$p.Rows[$e.RowIndex].Event
        if($null -eq $event){$e.CellStyle.BackColor=[Drawing.Color]::FromArgb(38,42,48);$e.CellStyle.ForeColor=[Drawing.Color]::FromArgb(112,118,128);return}
        if($sender.Columns[$e.ColumnIndex].Name -eq 'Outcome'){
            $palette=@{'승인'='LightGreen';'거절'='Orange';'실패'='Salmon';'중단'='Khaki';'오류'='Salmon';'경고'='Khaki';'미분류'='LightSteelBlue'}
            $value=if($event.Level -in @('Error','Fatal')){'오류'}elseif($event.Level -eq 'Warning'){'경고'}else{$event.Outcome}
            $e.CellStyle.ForeColor=[Drawing.Color]::FromName($(if($palette.ContainsKey($value)){$palette[$value]}else{'Gainsboro'}))
        }
    })
    $grid.Add_SelectionChanged({param($sender,$e) if(!$script:refreshing){$script:activePane=$sender.Tag;Show-PaneDetail $sender.Tag}})
    $grid.Add_MouseDown({param($sender,$e) $script:activePane=$sender.Tag;$follow.Checked=$false})
    $grid.Add_MouseWheel({param($sender,$e) $script:activePane=$sender.Tag;$follow.Checked=$false})
    $grid.Add_KeyDown({param($sender,$e) $script:activePane=$sender.Tag;$follow.Checked=$false})
    $grid.Add_Scroll({param($sender,$e)
        if($script:refreshing -or $script:scrollSync -or !(Test-Aligned) -or $e.ScrollOrientation -ne 'VerticalScroll'){return}
        $first=$sender.FirstDisplayedScrollingRowIndex;if($first -lt 0){return}
        $script:scrollSync=$true;try{foreach($p in $script:activePanes){if($p.Grid -ne $sender -and $p.Grid.RowCount){$p.Grid.FirstDisplayedScrollingRowIndex=[Math]::Min($first,$p.Grid.RowCount-1)}}}finally{$script:scrollSync=$false}
    })
    $split.Add_SplitterMoved({param($sender,$e)
        if($script:layoutBusy -or $script:splitSync -or !(Test-Aligned) -or $sender.Panel2Collapsed){return}
        $script:splitRatio=[double]$sender.SplitterDistance/[Math]::Max(1,$sender.Height);$script:splitSync=$true
        try{foreach($p in $script:activePanes){if($p.Split -ne $sender){Set-PaneSplitterDistance $p.Split $sender.SplitterDistance}}}finally{$script:splitSync=$false}
    })
    return $pane
}
$script:panes+=,(New-LogPane 0 '')
for($i=1;$i -le 4;$i++){
    $source=if($i -le $sourceSpecs.Count){[string]$sourceSpecs[$i-1].id}else{''}
    if($settings.PaneSources -and $settings.PaneSources.Count -ge $i -and $settings.PaneSources[$i-1] -in $sourceIds){$source=[string]$settings.PaneSources[$i-1]}
    $script:panes+=,(New-LogPane $i $source)
}
function Resize-Panes {
    if($script:layoutBusy -or !$script:activePanes.Count){return};$script:layoutBusy=$true
    try{
        $count=$script:activePanes.Count;$scale=[float]$fontControl.Value/12.0
        $width=[Math]::Max($hostPanel.ClientSize.Width-2,[int](440*$scale)*$count)
        $height=[Math]::Max(300,$hostPanel.ClientSize.Height-$(if($width -gt $hostPanel.ClientSize.Width){20}else{0}))
        $table.Size=New-Object Drawing.Size($width,$height)
        for($i=0;$i -lt $count;$i++){$table.ColumnStyles[$i].Width=[float]($width/$count)}
        $sourceInset = [int][Math]::Ceiling(8 * $form.DeviceDpi / 96.0)
        foreach($p in $script:activePanes){
            if ($p.Header.Visible) {
                # Fit the native selector height before reserving space above the log grid.
                $p.Header.RowStyles[0].Height = $sourceInset
                $p.Header.RowStyles[2].Height = $sourceInset
                $selectorHeight = [Math]::Max($p.Selector.Height, $p.Selector.PreferredHeight)
                $p.Header.Height = $selectorHeight + 2 * $sourceInset
                $p.Selector.Width = [int](160 * $scale)
                $p.Header.ColumnStyles[0].Width = [int](170 * $scale)
            }
            Set-PaneSplitterDistance $p.Split ([int]($p.Split.ClientSize.Height*$script:splitRatio))
        }
    }finally{$script:layoutBusy=$false}
}
function Set-PaneLayout {
    $script:layoutBusy=$true
    $table.SuspendLayout()
    try{
        $table.Controls.Clear();$table.ColumnStyles.Clear()
        $parallel=$viewChoice.SelectedIndex -eq 1
        $script:activePanes=@(if($parallel){$script:panes | Select-Object -Skip 1 -First ([int]$countChoice.SelectedItem)}else{$script:panes[0]})
        $table.ColumnCount=$script:activePanes.Count
        for($i=0;$i -lt $script:activePanes.Count;$i++){
            [void]$table.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Absolute',560)))
            $p=$script:activePanes[$i];$p.Header.Visible=$parallel;$table.Controls.Add($p.Container,$i,0)
        }
        $tabs.Visible=!$parallel;$countChoice.Enabled=$parallel -and !$pause.Checked;$displayChoice.Enabled=$parallel -and !$pause.Checked
        $script:activePane=$script:activePanes[0]
    }finally{
        try{$table.ResumeLayout()}finally{$script:layoutBusy=$false}
    }
    Resize-Panes;$script:renderDirty=$true
}
function Refresh-Events {
    if($pause.Checked){return}
    $anchors=@{}
    foreach($p in $script:activePanes){$idx=$p.Grid.FirstDisplayedScrollingRowIndex;$anchors[$p.Index]=if($idx -ge 0 -and $idx -lt $p.Rows.Count){$p.Rows[$idx].Basis.EventId}else{''}}
    $script:refreshing=$true
    try{
        Update-CombatHealthLabels $script:store $script:clientByPlayerId $sourceLabelById
        $script:common=@(Select-CombatEvents -Events $script:store -Outcome ([string]$outcome.SelectedItem) -Search $search.Text -Topics @(Get-SelectedLogTopics) -CollectionOrder:($order.SelectedIndex -eq 1))
        $aligned=Test-Aligned
        $sharedAnchor=if($script:activePane -and $anchors.ContainsKey($script:activePane.Index)){$anchors[$script:activePane.Index]}else{$anchors[$script:activePanes[0].Index]}
        foreach($p in $script:activePanes){
            $selectedId=$p.SelectedId;$grid=$p.Grid
            $p.Rows=@(ConvertTo-CombatPaneRows -CommonEvents $script:common -SourceId $p.SourceId -Aligned:$aligned)
            $p.ShownCount=@($p.Rows | Where-Object {$null -ne $_.Event}).Count
            $p.Label.Text=if($aligned){"$($p.ShownCount)건 / 공통 $($p.Rows.Count)행"}else{"$($p.ShownCount)건"}
            $grid.Columns['SourceLabel'].Visible=!$p.SourceId -and $viewChoice.SelectedIndex -eq 0
            $grid.RowCount=$p.Rows.Count;$grid.ClearSelection()
            if($grid.RowCount){
                $selected=Find-CombatRowIndex $p.Rows $selectedId
                if($follow.Checked){
                    $selected=-1;for($i=$p.Rows.Count-1;$i -ge 0;$i--){if($null -ne $p.Rows[$i].Event){$selected=$i;break}}
                    $grid.FirstDisplayedScrollingRowIndex=$grid.RowCount-1
                }else{
                    $anchor=if($aligned){$sharedAnchor}else{$anchors[$p.Index]}
                    $first=Find-CombatRowIndex $p.Rows $anchor -IncludePlaceholder
                    if($first -ge 0){$grid.FirstDisplayedScrollingRowIndex=$first}
                }
                if($selected -ge 0){$grid.Rows[$selected].Selected=$true}
            }
            $grid.Invalidate();Show-PaneDetail $p
        }
        $script:renderDirty=$false;$script:renderCount++;$script:lastShownCollection=$script:collection;$script:lastRenderedUtc=[DateTime]::UtcNow.ToString('o')
    }finally{$script:refreshing=$false}
    Update-ViewerStatus
}
$search.Add_TextChanged({$script:renderDirty=$true});$outcome.Add_SelectedIndexChanged({$script:renderDirty=$true});$order.Add_SelectedIndexChanged({$script:renderDirty=$true})
$tabs.Add_SelectedIndexChanged({
    if(!$script:sourceChoicesBusy -and !$script:tabSync -and $tabs.SelectedIndex -ge 0){$script:panes[0].Selector.SelectedIndex=$tabs.SelectedIndex}
})
$viewChoice.Add_SelectedIndexChanged({Set-PaneLayout});$countChoice.Add_SelectedIndexChanged({Set-PaneLayout})
$displayChoice.Add_SelectedIndexChanged({Resize-Panes;$script:renderDirty=$true})
$pause.Add_CheckedChanged({
    if ($pause.Checked) { $topicMenu.Close() }
    foreach($control in @($tabs,$search,$outcome,$topicButton,$order,$viewChoice)){$control.Enabled=!$pause.Checked}
    foreach($p in $script:panes){$p.Selector.Enabled=!$pause.Checked}
    $countChoice.Enabled=!$pause.Checked -and $viewChoice.SelectedIndex -eq 1;$displayChoice.Enabled=$countChoice.Enabled
    if(!$pause.Checked){$script:renderDirty=$true}
})
$follow.Add_CheckedChanged({$script:renderDirty=$true})
$latest.Add_Click({$pause.Checked=$false;$follow.Checked=$true;$script:renderDirty=$true})
$pin.Add_CheckedChanged({$form.TopMost=$pin.Checked})
$folder.Add_Click({[Windows.Forms.Clipboard]::SetText($logRoot)})
$copy.Add_Click({if($script:activePane -and $script:activePane.Detail.Text){[Windows.Forms.Clipboard]::SetText($script:activePane.Detail.Text)}})
$detailToggle.Add_CheckedChanged({
    $script:splitSync=$true
    try{foreach($p in $script:panes){$p.Split.Panel2Collapsed=!$detailToggle.Checked}}finally{$script:splitSync=$false}
    Resize-Panes
})
$fontControl.Add_ValueChanged({
    $newFont=New-Object Drawing.Font('Malgun Gothic',[float]$fontControl.Value);$oldFormFont=$form.Font;$form.Font=$newFont
    foreach($p in $script:panes){
        # A pane removed from the table no longer inherits its font from the form.
        $p.Container.Font=$form.Font
        $old=$p.Detail.Font;$p.Detail.Font=New-Object Drawing.Font('Consolas',[float]$fontControl.Value);$old.Dispose()
        $p.Grid.RowTemplate.Height=[int]([float]$fontControl.Value*2.4);$p.Grid.ColumnHeadersHeight=[int]([float]$fontControl.Value*2.5)
        for($i=0;$i -lt $p.Grid.RowCount;$i++){$p.Grid.Rows[$i].Height=$p.Grid.RowTemplate.Height}
        foreach($c in $p.Grid.Columns){if($c.Name -ne 'Summary'){$c.Width=[int]([int]$c.Tag*[float]$fontControl.Value/12.0)}}
    }
    $oldFormFont.Dispose();Align-Toolbars;Resize-Panes
})
$move.Add_Click({
    $fresh=@(Get-CombatMonitors);$selected=$monitors[$monitorChoice.SelectedIndex];$target=Find-CombatMonitor $fresh $selected.DeviceName;if(!$target){$target=$fresh[0]}
    $form.WindowState='Normal';$form.Bounds=New-Object Drawing.Rectangle($target.Left,$target.Top,$target.Width,$target.Height);$form.WindowState='Maximized'
    try{Save-CombatViewerSettings @{LogMonitor=$target.DeviceName} -Path $SettingsPath}catch{$script:settingsError=$_.Exception.Message}
})
$hostPanel.Add_Resize({Resize-Panes})
function New-ViewerExportSnapshot {
    $rows=@(Get-CombatExportRows -CommonEvents $script:common -Panes $script:activePanes)
    $panes=@(foreach($p in $script:activePanes){
        $first=$p.Grid.FirstDisplayedScrollingRowIndex
        [ordered]@{Source=$p.SourceId;Label=[string]$p.Selector.SelectedItem;SelectedEventId=$p.SelectedId;FirstVisibleRow=$first;FirstEventId=$(if($first -ge 0 -and $first -lt $p.Rows.Count){$p.Rows[$first].Basis.EventId}else{''});Rows=$p.Rows.Count;Events=$p.ShownCount}
    })
    $metadata=[ordered]@{
        '실행 ID'=$runId;'저장 UTC'=[DateTime]::UtcNow.ToString('o');'저장 현지 시각'=[DateTimeOffset]::Now.ToString('o');'표시 시간대'=[TimeZoneInfo]::Local.Id
        '원본 로그 폴더'=$logRoot;'내보낸 사건 수'=$rows.Count;'검색어'=$search.Text;'결과 필터'=[string]$outcome.SelectedItem;'정렬'="$($order.SelectedItem) · 인과 아님"
        '카테고리 필터'=(@(Get-SelectedLogTopics) -join ', ')
        '보기'=[string]$viewChoice.SelectedItem;'분할 수'=$script:activePanes.Count;'표시 모드'=$(if($viewChoice.SelectedIndex -eq 1){[string]$displayChoice.SelectedItem}else{'단일 목록'})
        '선택 출처'=($panes.Label -join ', ');'표시 멈춤'=[bool]$pause.Checked;'최신 따라가기'=[bool]$follow.Checked;'글자 크기'=[double]$fontControl.Value
        '화면 데이터 갱신 UTC'=$script:lastRenderedUtc;'화면 갱신 당시 수집 수'=$script:lastShownCollection;'저장 당시 수집 수'=$script:collection
        '현재 보관 수'=$script:store.Count;'전체 보관 한도'=$Capacity;'현재 한도 초과 제거 수'=$script:evicted
        '파일 시작 생략 바이트'=($readers.Values | ForEach-Object {$_.Reader.InitialSkippedBytes} | Measure-Object -Sum).Sum
        '현재 읽기 대기 바이트'=($readers.Values | ForEach-Object {$_.Reader.UnreadBytes} | Measure-Object -Sum).Sum
        'Excel 범위'='현재 필터·선택 출처에 해당하는 목록 전체. 화면 밖 행 포함, 빈칸·중복 사건 제외. 원본 파일 전체와 다름.'
        'PNG 범위'='현재 창에 보이는 목록·상세·배치만 렌더링. 스크롤 밖 영역과 다른 창은 포함하지 않음.'
        '관계 해석'='시각·행 번호는 인과관계나 요청 ID가 아님. 요청 관계 미계측.'
        '긴 셀'='Excel의 32767자 한도를 넘는 셀은 잘라내지 않고 저장 실패로 알림. 원본 로그 유지.'
    }
    [pscustomobject]@{Rows=$rows;Metadata=$metadata;Panes=$panes}
}

function Invoke-ViewerExport([ValidateSet('PNG','XLSX')][string]$Format){
    $wasRunning=$timer.Enabled;$timer.Stop();$createdPath=''
    try{
        if(!$pause.Checked -and $script:renderDirty){Refresh-Events}
        $snapshot=New-ViewerExportSnapshot
        [void][IO.Directory]::CreateDirectory($script:exportDirectory)
        $name='CoopRPG-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
        $path=Join-Path $script:exportDirectory ($name+'.'+$Format.ToLowerInvariant())
        if($Format -eq 'PNG'){
            Export-CombatWindowPng -Form $form -Path $path;$createdPath=$path
            $context=[ordered]@{Metadata=$snapshot.Metadata;Panes=$snapshot.Panes;Window=@{Width=$form.Width;Height=$form.Height;Dpi=$form.DeviceDpi}}
            $json=[Text.UTF8Encoding]::new($false).GetBytes(($context | ConvertTo-Json -Depth 7))
            $sidecar=[IO.File]::Open(($path+'.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
            try{$sidecar.Write($json,0,$json.Length)}finally{$sidecar.Dispose()}
        }else{Export-CombatWorkbook -Path $path -Rows $snapshot.Rows -Metadata $snapshot.Metadata;$createdPath=$path}
        $script:exportMessage="$Format 저장 완료 · $($snapshot.Rows.Count)건 · $path"
        $script:lastExport=[pscustomobject]@{Succeeded=$true;Format=$Format;Path=$path;Count=$snapshot.Rows.Count;Error=''}
    }catch{
        $script:exportMessage='저장 실패 · '+$_.Exception.Message+$(if($createdPath){' · 생성된 파일: '+$createdPath}else{''})
        $script:lastExport=[pscustomobject]@{Succeeded=$false;Format=$Format;Path=$createdPath;Count=0;Error=$_.Exception.Message}
    }finally{
        $exportFolder.Enabled=Test-Path -LiteralPath $script:exportDirectory -PathType Container
        $exportTooltip.SetToolTip($status,$script:exportMessage);Update-ViewerStatus
        if($wasRunning){$timer.Start()}
    }
}
$capture.Add_Click({Invoke-ViewerExport 'PNG'})
$excel.Add_Click({Invoke-ViewerExport 'XLSX'})
$exportFolder.Add_Click({
    try{Start-Process explorer.exe -ArgumentList ('"'+$script:exportDirectory+'"') | Out-Null}
    catch{$script:exportMessage='폴더 열기 실패 · '+$_.Exception.Message;Update-ViewerStatus}
})


Align-Toolbars;Set-PaneLayout

function Update-ViewerSourceChoices {
    $ids=@('')+@($sourceSpecs | ForEach-Object id)
    $labels=@('통합')+@($sourceSpecs | ForEach-Object label)
    if(($ids -join [char]10) -ceq ($sourceIds -join [char]10) -and ($labels -join [char]10) -ceq ($sourceLabels -join [char]10)){return}
    $script:sourceChoicesBusy=$true
    try{
        $selectedTab=[string]$tabs.SelectedTab.Tag
        $script:sourceIds=$ids;$script:sourceLabels=$labels
        foreach($page in @($tabs.TabPages)){$page.Dispose()}
        for($i=0;$i -lt $ids.Count;$i++){
            $page=New-Object Windows.Forms.TabPage
            $page.Text=$labels[$i];$page.Tag=$ids[$i]
            [void]$tabs.TabPages.Add($page)
        }
        $tabs.SelectedIndex=[Math]::Max(0,[Array]::IndexOf($ids,$selectedTab))
        foreach($pane in $script:panes){
            $pane.Selector.Items.Clear()
            $pane.Selector.Items.AddRange([object[]]$labels)
            $index=[Math]::Max(0,[Array]::IndexOf($ids,$pane.SourceId))
            $pane.SourceId=$ids[$index]
            $pane.Selector.SelectedIndex=$index
        }
        $script:renderDirty=$true
    }finally{$script:sourceChoicesBusy=$false}
}

function Update-ViewerLogSources {
    try{
        $text=Read-ViewerManifestText
        if(!$text -or $text -ceq $script:lastManifestText){return}
        $script:lastManifestText=$text
        $next=$text | ConvertFrom-Json
        if(!$next.sources){return}
        Set-ViewerSources @($next.sources) $next.previousSources -FollowFromStart
        $script:manifest=$next
        Update-ViewerSourceChoices
    }catch{Write-Warning "Process manifest update ignored: $($_.Exception.Message)"}
}

function Update-ViewerStatus {
        $script:sourceText=@(foreach($spec in $sourceSpecs){
            $s=$sources[$spec.id];$file=if($s.Reader.State -eq 'Waiting'){'파일 대기'}elseif($s.Reader.State -eq 'Error'){'읽기 실패: '+$s.Reader.Error}else{'파일 읽기 정상'}
            "$($spec.label): $($s.ProcessState) / $file / 마지막 사건 $(if($s.LastEvent){$s.LastEvent}else{'없음'})"
        })
        $ignored=($readers.Values | ForEach-Object Ignored | Measure-Object -Sum).Sum
        $truncated=($readers.Values | ForEach-Object {$_.Reader.InitialSkippedBytes} | Measure-Object -Sum).Sum
        $script:unreadBytes=($readers.Values | ForEach-Object {$_.Reader.UnreadBytes} | Measure-Object -Sum).Sum
        $mode=if($pause.Checked){"표시 멈춤 · 새 사건 $($script:collection-$script:lastShownCollection)건 대기"}else{'표시 갱신 중'}
        $status.Text=($script:sourceText -join [Environment]::NewLine)+[Environment]::NewLine+"$mode · 화면 $($script:common.Count) / 보관 $($script:store.Count) / 한도 $Capacity · 한도 초과 $script:evicted · 엔진 일반 로그 제외 $ignored · 초기 생략 $truncated B · 읽기 대기 $script:unreadBytes B"+[Environment]::NewLine+"시각은 $([TimeZoneInfo]::Local.Id) · 정렬은 인과관계가 아님 · 빈칸=다른 출처의 사건 · 원본 보존$(if($script:settingsError){' · 설정 저장 실패: '+$script:settingsError})"+[Environment]::NewLine+$script:exportMessage
}
function Update-SourceState {
    foreach($source in $sources.Values){
        $spec=$source.Spec
        if(!$spec.processId){$source.ProcessState='오프라인 로그';continue}
        try{
            $process=Get-Process -Id ([int]$spec.processId) -ErrorAction Stop
            $valid=$process.ProcessName -eq $spec.processName
            if($spec.startedUtc){$valid=$valid -and [Math]::Abs(($process.StartTime.ToUniversalTime()-[DateTime]::Parse($spec.startedUtc).ToUniversalTime()).TotalSeconds) -lt 2}
            elseif($manifest -and $manifest.started){$valid=$valid -and $process.StartTime -le [DateTime]::Parse($manifest.started)}
            $source.ProcessState=if($valid){'실행 중'}else{'이전 프로세스 종료'}
        }catch [Microsoft.PowerShell.Commands.ProcessCommandException]{$source.ProcessState='종료'}
        catch{$source.ProcessState='프로세스 확인 불가'}
    }
}
$timer=New-Object Windows.Forms.Timer;$timer.Interval=300
$timer.Add_Tick({
    $watch=[Diagnostics.Stopwatch]::StartNew()
    try{
        if(([DateTime]::UtcNow-$script:lastProcessCheck).TotalSeconds -ge 2){
            Update-ViewerLogSources
            Update-SourceState
            $script:lastProcessCheck=[DateTime]::UtcNow
        }
        foreach($source in $readers.Values){
            $spec=$source.Spec
            $current=[object]::ReferenceEquals($source,$sources[$spec.id])
            if(!$current -and $source.Drained){continue}
            try{
                foreach($record in @(Read-CombatLogRecords $source.Reader -MaxBytes 8192)){
                    $event=ConvertTo-CombatLogEvent $record.Line
                    if($null -eq $event){$source.Ignored++;continue}
                    $script:collection++
                    $event=Add-CombatEventContext $event $runId $spec.id $spec.label $source.Reader.Path $record $script:collection $source.FileKey
                    $playerId = [string]$event.Fields['LocalPlayerId']
                    if ($spec.id -ne 'server' -and $event.Fields['NetMode'] -eq 'Client' -and $playerId -match '^[1-9][0-9]*$') {
                        if (!$script:clientByPlayerId.ContainsKey($playerId)) {
                            $script:clientByPlayerId[$playerId] = $spec.id
                        } elseif ($script:clientByPlayerId[$playerId] -ne $spec.id) {
                            # A conflicting ID must never be attributed to whichever file was read last.
                            $script:clientByPlayerId[$playerId] = $null
                        }
                    }
                    $script:store.Add($event);$source.LastEvent=$event.Time;$script:renderDirty=$true
                }
                if(!$current -and $source.Reader.State -ne 'Error' -and !$source.Reader.UnreadBytes){$source.Drained=$true}
            }catch{$source.Reader.State='Error';$source.Reader.Error=$_.Exception.Message}
        }
        if($script:store.Count -gt $Capacity){$remove=$script:store.Count-$Capacity;$script:store.RemoveRange(0,$remove);$script:evicted+=$remove}
        if($script:renderDirty){Refresh-Events}
        Update-ViewerStatus
        # Render only this tool's own controls for visual QA; not a desktop capture.
        if($PreviewPath -and !$script:previewAttempted -and $script:renderCount -gt 0 -and $script:unreadBytes -eq 0){
            $script:previewAttempted=$true
            try{
                if(Test-Path -LiteralPath $PreviewPath){throw 'Preview already exists; use a new file.'}
                [void][IO.Directory]::CreateDirectory((Split-Path ([IO.Path]::GetFullPath($PreviewPath))))
                $bitmap=New-Object Drawing.Bitmap($form.Width,$form.Height)
                try{$form.DrawToBitmap($bitmap,(New-Object Drawing.Rectangle(0,0,$form.Width,$form.Height)));$bitmap.Save($PreviewPath,[Drawing.Imaging.ImageFormat]::Png)}finally{$bitmap.Dispose()}
            }catch{Write-Warning ('Viewer preview failed: '+$_.Exception.Message)}
        }
        if(([DateTime]::UtcNow-$script:lastDiagnostic).TotalSeconds -ge 5){
            $snapshot=@{pid=$PID;runId=$runId;updatedUtc=[DateTime]::UtcNow.ToString('o');title=$form.Text;bounds=@{left=$form.Left;top=$form.Top;width=$form.Width;height=$form.Height};windowState=[string]$form.WindowState;device=[Windows.Forms.Screen]::FromControl($form).DeviceName;dpi=$form.DeviceDpi;viewMode=[string]$viewChoice.SelectedItem;paneCount=$script:activePanes.Count;displayMode=[string]$displayChoice.SelectedItem;panes=@(foreach($p in $script:activePanes){@{source=$p.SourceId;rows=$p.Rows.Count;events=$p.ShownCount;selected=$p.SelectedId;firstRow=$p.Grid.FirstDisplayedScrollingRowIndex}});visible=$script:common.Count;stored=$script:store.Count;total=$script:collection;evicted=$script:evicted;paused=$pause.Checked;follow=$follow.Checked;renders=$script:renderCount;maxTickMs=$script:maxTickMs;meanTickMs=$(if($script:tickCount){$script:tickMs/$script:tickCount}else{0});sources=@($script:sourceText)}
            [IO.File]::WriteAllText((Join-Path $logRoot "viewer-status-$PID.json"),($snapshot | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false));$script:lastDiagnostic=[DateTime]::UtcNow
        }
    }catch{$status.Text='뷰어 오류 · '+$_.Exception.Message;Write-Warning $_.Exception.Message}
    finally{$watch.Stop();$script:maxTickMs=[Math]::Max($script:maxTickMs,$watch.Elapsed.TotalMilliseconds);$script:tickMs+=$watch.Elapsed.TotalMilliseconds;$script:tickCount++}
})

$form.Add_Shown({$form.Hide();$form.Show();Align-Toolbars;Resize-Panes;$timer.Start()})
$form.Add_FormClosing({
    try{Save-CombatViewerSettings @{FontSize=[double]$fontControl.Value;LogMonitor=[Windows.Forms.Screen]::FromControl($form).DeviceName;ViewMode=$(if($viewChoice.SelectedIndex -eq 1){'Parallel'}else{'Single'});PaneCount=[int]$countChoice.SelectedItem;DisplayMode=$(if($displayChoice.SelectedIndex -eq 1){'Aligned'}else{'Compact'});PaneSources=@($script:panes | Select-Object -Skip 1 | ForEach-Object SourceId)} -Path $SettingsPath}catch{Write-Warning $_.Exception.Message}
})
$form.Add_FormClosed({$timer.Stop();$timer.Dispose();$exportTooltip.Dispose();$topicMenu.Dispose();$topicTooltip.Dispose()})
try{[void]$form.ShowDialog()}finally{foreach($p in $script:panes){$p.Container.Dispose()};$form.Dispose()}
