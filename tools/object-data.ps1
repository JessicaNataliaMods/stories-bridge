# CObjectData::Initialise (SA 1.0, 0x5B5360) stops at the FIRST line
# beginning with '*', not merely the '* ;end of file' marker.
function Get-NativeObjectDataRows([string]$Text) {
    foreach($taskObjectLine in ($Text -split "\r?\n")) {
        $taskObjectLine=$taskObjectLine.Trim()
        if(!$taskObjectLine -or $taskObjectLine.StartsWith(';')){continue}
        if($taskObjectLine.StartsWith('*')){break}
        $taskObjectFields=@(($taskObjectLine -replace ',',' ') -split '\s+')
        if($taskObjectFields.Count -ge 13) {
            [pscustomobject]@{Name=$taskObjectFields[0];Fields=$taskObjectFields;Line=$taskObjectLine}
        }
    }
}
function Set-FirstMissionDoorPhysics([string]$Text) {
    $taskObjectRows=@(Get-NativeObjectDataRows $Text)
    $taskObjectDonors=@($taskObjectRows | Where-Object {$_.Name -ieq 'xref_garagedoor'})
    if($taskObjectDonors.Count -ne 1){throw 'Readable native garage-door physics template missing'}
    # Remove an old definition even if it was mistakenly installed after the
    # native parser's termination marker, then place it beside the readable donor.
    $Text=[regex]::Replace($Text,'(?im)^[\t ]*lc1095\s[^\r\n]*(?:\r?\n|$)','')
    $taskObjectDonor=[regex]::Matches($Text,'(?im)^[\t ]*xref_garagedoor[^\r\n]*')
    if($taskObjectDonor.Count -ne 1){throw 'Ambiguous native garage-door physics template'}
    $taskObjectDoor=[regex]::Replace($taskObjectDonor[0].Value,'(?i)xref_garagedoor','lc1095')
    $taskObjectInsertion=$taskObjectDonor[0].Index+$taskObjectDonor[0].Length
    $Text=$Text.Insert($taskObjectInsertion,"`r`n"+$taskObjectDoor)
    $taskObjectDoors=@(Get-NativeObjectDataRows $Text | Where-Object {$_.Name -ieq 'lc1095'})
    if($taskObjectDoors.Count -ne 1 -or
       (($taskObjectDoors[0].Fields | Select-Object -Skip 1) -join ' ') -ne
       (($taskObjectDonors[0].Fields | Select-Object -Skip 1) -join ' ')) {
        throw 'Native parser cannot read the expected first-mission door physics'
    }
    return $Text
}
