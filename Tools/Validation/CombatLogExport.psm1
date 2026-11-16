Set-StrictMode -Version Latest

function Initialize-CombatWorkbookWriter {
    if('CoopRPG.Validation.CombatWorkbookWriter' -as [type]){return}
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -Path (Join-Path $PSScriptRoot 'CombatWorkbookWriter.cs') -ReferencedAssemblies @('System.dll','System.Core.dll','System.Xml.dll',[IO.Compression.ZipArchive].Assembly.Location)
}

function Get-CombatExportRows($CommonEvents,$Panes){
    $ids=New-Object 'Collections.Generic.HashSet[string]'
    foreach($pane in $Panes){foreach($row in $pane.Rows){if($null -ne $row.Event){[void]$ids.Add($row.Event.EventId)}}}
    $ordinal=0
    foreach($event in $CommonEvents){$ordinal++;if($ids.Contains($event.EventId)){[pscustomobject]@{Ordinal=$ordinal;Event=$event}}}
}

function Export-CombatWorkbook([string]$Path,$Rows,$Metadata){
    Initialize-CombatWorkbookWriter
    $headers=[string[]]@('행','시각 (현지)','출처','단계','결과','사건·관측값','사유','Actor','Target','UTC','Category','Level','EventCode','EventId','수집순','파일 세대','바이트 위치','원본 파일','원문','요청 ID','관계','원문 필드')
    $matrix=New-Object 'Collections.Generic.List[string[]]'
    foreach($row in $Rows){
        $e=$row.Event;$utc=if($e.UtcTime){$e.UtcTime.ToString('o')}else{''}
        $matrix.Add([string[]]@($row.Ordinal,$e.Time,$e.SourceLabel,$e.Stage,$e.Outcome,$e.Summary,$e.ReasonCode,$e.Actor,$e.Target,$utc,$e.Category,$e.Level,$e.EventCode,$e.EventId,$e.CollectionOrder,$e.FileGeneration,$e.ByteOffset,$e.SourcePath,$e.Raw,$(if($null -eq $e.RequestId){'미계측'}else{$e.RequestId}),$e.Relationship,($e.Fields | ConvertTo-Json -Compress -Depth 5)))
    }
    $info=New-Object 'Collections.Generic.List[string[]]'
    foreach($entry in $Metadata.GetEnumerator()){$info.Add([string[]]@($entry.Key,[string]$entry.Value))}
    [CoopRPG.Validation.CombatWorkbookWriter]::Write([IO.Path]::GetFullPath($Path),$headers,$matrix.ToArray(),$info.ToArray())
}

function Export-CombatWindowPng($Form,[string]$Path){
    $bitmap=New-Object Drawing.Bitmap($Form.Width,$Form.Height)
    $stream=$null;$created=$false
    try{
        $Form.DrawToBitmap($bitmap,(New-Object Drawing.Rectangle(0,0,$Form.Width,$Form.Height)))
        $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);$created=$true
        $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
    }catch{
        if($stream){$stream.Dispose();$stream=$null};if($created){[IO.File]::Delete($Path)};throw
    }finally{if($stream){$stream.Dispose()};$bitmap.Dispose()}
}

Export-ModuleMember -Function Get-CombatExportRows,Export-CombatWorkbook,Export-CombatWindowPng
