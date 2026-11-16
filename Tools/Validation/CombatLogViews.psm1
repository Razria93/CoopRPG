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

function Get-CombatRelatedPlayerLabel($Event,[hashtable]$ClientByPlayerId,[hashtable]$SourceLabelById) {
    foreach($field in @('RequesterPlayerId','SubjectPlayerId','EquippedPlayerId','TargetPlayerId','LocalPlayerId')){
        if($field -eq 'LocalPlayerId' -and $Event.SourceId -eq 'server'){continue}
        $playerId=[string]$Event.Fields[$field]
        if($playerId -notmatch '^[1-9]\d*$'){continue}
        $sourceId=$ClientByPlayerId[$playerId]
        if($sourceId -and $SourceLabelById[$sourceId]){return ($SourceLabelById[$sourceId] -replace '^클라이언트 ', '클라 ')}
        return "ID $playerId"
    }
    return '—'
}

function Get-CombatTargetLabel($Event) {
    $chest=[string]$Event.Fields['Chest']
    if($Event.EventCode -in @('ChestStockReplicated','WeaponStockReplicated')){$chest=[string]$Event.Actor}
    if($chest -and $chest -ne 'None'){
        if($chest -match '^CoopRPGSharedChest(?:_\d+)?$'){return '공용 상자'}
        return $chest
    }

    if($Event.Fields['DropId'] -match '^[0-9a-f]{32}$' -and $Event.Fields['DropId'] -notmatch '^0+$'){return '적 전리품'}
    $target=[string]$Event.Target
    if($Event.EventCode -in @('HealthReplicated','StaminaReplicated','MeleeWeaponReplicated')){$target=[string]$Event.Actor}
    switch -Regex ($target) {
        '^CoopRPGEnemy(?:_\d+)?$' {return '적'}
        '^CoopRPGAttackTarget(?:_\d+)?$' {return '훈련 표적'}
        '^CoopRPGCharacter(?:_\d+)?$' {return '플레이어 Pawn'}
        '^(None)?$' {return '—'}
        default {return $target}
    }
}

Export-ModuleMember -Function ConvertTo-CombatPaneRows,Find-CombatRowIndex,Get-CombatRelatedPlayerLabel,Get-CombatTargetLabel
