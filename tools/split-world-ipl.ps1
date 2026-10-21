param([string]$WorldDirectory=(Join-Path $PSScriptRoot '..\out\world-stage'))
$ErrorActionPreference='Stop'
$taskWorld=(Resolve-Path -LiteralPath $WorldDirectory).Path
$taskData=Join-Path $taskWorld 'data\lcs'
$taskRows=[Collections.Generic.List[object]]::new();$taskSection=''
foreach($taskLine in [IO.File]::ReadLines((Join-Path $taskData 'world.ipl'))){
    $taskText=($taskLine -split '#')[0].Trim();if(!$taskText){continue}
    if($taskText -eq 'inst'){$taskSection='inst';continue};if($taskText -eq 'end'){$taskSection='';continue};if($taskSection -ne 'inst'){continue}
    $taskP=$taskText -split ','|ForEach-Object {$_.Trim()}
    if($taskP.Count -ne 11){throw 'Invalid native IPL row'}
    $taskRows.Add($taskP)
}
# An IPL's LOD indices are local to that IPL, in both SA and Euryopa. Keep every
# connected group in one partition; never duplicate or discard map instances.
$taskParents=New-Object int[] $taskRows.Count
for($taskI=0;$taskI -lt $taskRows.Count;$taskI++){$taskParents[$taskI]=$taskI}
function Find-Root([int]$Index){while($taskParents[$Index] -ne $Index){$taskParents[$Index]=$taskParents[$taskParents[$Index]];$Index=$taskParents[$Index]};return $Index}
for($taskI=0;$taskI -lt $taskRows.Count;$taskI++){
    $taskLod=[int]$taskRows[$taskI][10]
    if($taskLod -lt -1 -or $taskLod -ge $taskRows.Count -or $taskLod -eq $taskI){throw 'Invalid original LOD index'}
    if($taskLod -ge 0){$taskA=Find-Root $taskI;$taskB=Find-Root $taskLod;$taskParents[$taskB]=$taskA}
}
$taskGroups=@{}
for($taskI=0;$taskI -lt $taskRows.Count;$taskI++){
    $taskRoot=Find-Root $taskI
    if(!$taskGroups.ContainsKey($taskRoot)){$taskGroups[$taskRoot]=[Collections.Generic.List[int]]::new()}
    $taskGroups[$taskRoot].Add($taskI)
}
$taskParts=[Collections.Generic.List[object]]::new();$taskCurrent=[Collections.Generic.List[int]]::new()
# Below Euryopa's 8096-entry array, with headroom for native related IPL data.
$taskMaximum=4000
foreach($taskGroup in ($taskGroups.Values|Sort-Object {$_[0]})){
    if($taskGroup.Count -gt $taskMaximum){throw 'A connected LOD group exceeds IPL capacity'}
    if($taskCurrent.Count+$taskGroup.Count -gt $taskMaximum){$taskParts.Add($taskCurrent);$taskCurrent=[Collections.Generic.List[int]]::new()}
    $taskCurrent.AddRange($taskGroup)
}
if($taskCurrent.Count){$taskParts.Add($taskCurrent)}
$taskFiles=[Collections.Generic.List[object]]::new();$taskLinks=0
for($taskPart=0;$taskPart -lt $taskParts.Count;$taskPart++){
    $taskIndices=@($taskParts[$taskPart]|Sort-Object);$taskLocal=@{}
    for($taskI=0;$taskI -lt $taskIndices.Count;$taskI++){$taskLocal[[int]$taskIndices[$taskI]]=$taskI}
    $taskLines=[Collections.Generic.List[string]]::new();$taskLines.Add('inst')
    foreach($taskIndex in $taskIndices){
        $taskP=@($taskRows[$taskIndex]);$taskLod=[int]$taskP[10]
        if($taskLod -ge 0){if(!$taskLocal.ContainsKey($taskLod)){throw 'LOD crosses IPL partition'};$taskP[10]=[string]$taskLocal[$taskLod];$taskLinks++}
        $taskLines.Add(($taskP -join ', '))
    }
    $taskLines.Add('end');$taskFile='world'+$taskPart+'.ipl'
    [IO.File]::WriteAllLines((Join-Path $taskData $taskFile),$taskLines,[Text.Encoding]::ASCII)
    $taskFiles.Add([pscustomobject]@{File=$taskFile;Instances=$taskIndices.Count})
}
$taskManifestFile=Join-Path $taskData 'world-manifest.json'
$taskManifest=Get-Content -LiteralPath $taskManifestFile -Raw|ConvertFrom-Json
if($taskRows.Count -ne $taskManifest.Instances -or $taskLinks -ne $taskManifest.LodLinks){throw 'Partition loses map instances or LOD links'}
$taskManifest|Add-Member -NotePropertyName PlacementFiles -NotePropertyValue @($taskFiles.ToArray()) -Force
$taskManifest|Add-Member -NotePropertyName MaximumInstancesPerFile -NotePropertyValue $taskMaximum -Force
$taskManifest|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $taskManifestFile -Encoding utf8
$taskLevel=@('IMG MODELS\LCS\WORLD.IMG','IDE DATA\LCS\WORLD.IDE','COLFILE 0 MODELS\LCS\WORLD.COL')+@($taskFiles|ForEach-Object {'IPL DATA\LCS\'+$_.File})
[IO.File]::WriteAllLines((Join-Path $taskData 'level.dat'),$taskLevel,[Text.Encoding]::ASCII)
[pscustomobject]@{Instances=$taskRows.Count;LodLinks=$taskLinks;PlacementFiles=$taskFiles;MaximumInstancesPerFile=$taskMaximum;GameExecuted=$false}|ConvertTo-Json -Depth 4
