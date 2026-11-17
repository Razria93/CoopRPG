Set-StrictMode -Version Latest

# Projection only. Ordinal is a position in this filtered/sorted view, never a request ID.
function ConvertTo-CombatPaneRows($CommonEvents,[string]$SourceId='', [switch]$Aligned) {
    $ordinal=0
    foreach($event in $CommonEvents){
        $ordinal++
        $matchesSource=!$SourceId -or $event.SourceId -eq $SourceId
        if($matchesSource -or $Aligned){
            [pscustomobject]@{Ordinal=$ordinal;Basis=$event;Event=$(if($matchesSource){$event}else{$null})}
        }
    }
}

function Find-CombatRowIndex($Rows,[string]$EventId,[switch]$IncludePlaceholder){
    if(!$EventId){return -1}
    for($i=0;$i -lt $Rows.Count;$i++){
        $candidate=if($IncludePlaceholder){$Rows[$i].Basis}else{$Rows[$i].Event}
        if($null -ne $candidate -and $candidate.EventId -eq $EventId){return $i}
    }
    return -1
}

Export-ModuleMember -Function ConvertTo-CombatPaneRows,Find-CombatRowIndex
