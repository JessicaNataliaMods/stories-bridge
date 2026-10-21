param([string]$WorldDirectory=(Join-Path $PSScriptRoot '..\out\world-stage'))
$ErrorActionPreference='Stop'
$taskWorld=(Resolve-Path -LiteralPath $WorldDirectory).Path
$taskManifest=Get-Content -LiteralPath (Join-Path $taskWorld 'data\lcs\world-manifest.json') -Raw | ConvertFrom-Json
if(!$taskManifest.WorldPrepared){throw 'Map preparation is incomplete'}
$taskBindings=Get-Content -LiteralPath (Join-Path $taskWorld 'data\lcs\model-bindings.json') -Raw | ConvertFrom-Json
$taskIds=@{};$taskNames=@{}
foreach($taskBinding in $taskBindings){if($taskBinding.Available){$taskIds[[int]$taskBinding.SaId]=$taskBinding;$taskNames[$taskBinding.SaName]=$taskBinding}}
$taskImgPath=Join-Path $taskWorld 'models\lcs\world.img'
$taskImg=[IO.File]::OpenRead($taskImgPath);$taskReader=[IO.BinaryReader]::new($taskImg)
try {
    if([Text.Encoding]::ASCII.GetString($taskReader.ReadBytes(4)) -ne 'VER2'){throw 'World archive is not native IMG v2'}
    $taskCount=$taskReader.ReadUInt32();$taskEntries=@{};$taskDirectoryEnd=8+32*[long]$taskCount
    $taskPreviousEnd=[Math]::Ceiling($taskDirectoryEnd/2048)*2048
    for($taskI=0;$taskI -lt $taskCount;$taskI++){
        $taskOffset=[long]$taskReader.ReadUInt32()*2048;$taskSizes=$taskReader.ReadUInt32();$taskSize=[long]($taskSizes -band 65535)*2048
        if($taskSizes -shr 16){throw 'Unexpected compressed world entry'}
        $taskName=[Text.Encoding]::ASCII.GetString($taskReader.ReadBytes(24)).Split([char]0)[0]
        if(!$taskName -or $taskEntries.ContainsKey($taskName) -or $taskOffset -lt $taskPreviousEnd -or $taskOffset+$taskSize -gt $taskImg.Length){throw 'Invalid world IMG directory bounds'}
        $taskEntries[$taskName]=@{Offset=$taskOffset;Size=$taskSize};$taskPreviousEnd=$taskOffset+$taskSize
    }
    $taskDff=0;$taskTxd=0;$taskTextureRefs=0
    foreach($taskEntry in $taskEntries.GetEnumerator()){
        $taskImg.Position=$taskEntry.Value.Offset
        $taskMagic=$taskReader.ReadUInt32();$taskLength=$taskReader.ReadUInt32();$taskVersion=$taskReader.ReadUInt32()
        if($taskLength+12 -gt $taskEntry.Value.Size -or $taskVersion -ne 0x1803FFFF){throw "Invalid SA RenderWare header: $($taskEntry.Key)"}
        if($taskEntry.Key.EndsWith('.dff')){
            if($taskMagic -ne 16){throw 'Map DFF has no clump'}
            if($taskReader.ReadUInt32() -ne 1){throw 'Clump has no struct'}
            $taskStructLength=$taskReader.ReadUInt32();$null=$taskReader.ReadUInt32()
            if($taskReader.ReadUInt32() -ne 1){throw "SA map model must have one atomic: $($taskEntry.Key)"}
            $taskDff++
        }elseif($taskEntry.Key.EndsWith('.txd')){
            if($taskMagic -ne 22){throw 'Map TXD has no dictionary'}
            $taskTxd++
        }else{throw 'Unexpected file type in world IMG'}
    }
    foreach($taskBinding in $taskIds.Values){
        if(!$taskEntries.ContainsKey($taskBinding.SaName+'.dff') -or !$taskEntries.ContainsKey($taskBinding.SaTxd+'.txd')){throw 'Model archive binding is incomplete'}
    }
} finally {$taskReader.Dispose();$taskImg.Dispose()}
$taskPlaced=[Collections.Generic.List[object]]::new();$taskPlacementCounts=[Collections.Generic.List[object]]::new();$taskLodLinks=0
if(!$taskManifest.PlacementFiles){throw 'World IPL must be split before installation'}
foreach($taskPlacementFile in $taskManifest.PlacementFiles){
if($taskPlacementFile.File -notmatch '^world[0-9]+\.ipl$'){throw 'Invalid IPL filename'}
$taskPart=[Collections.Generic.List[object]]::new();$taskSection=''
foreach($taskLine in [IO.File]::ReadLines((Join-Path $taskWorld ('data\lcs\'+$taskPlacementFile.File)))){
    if($taskLine -eq 'inst'){$taskSection='inst';continue};if($taskLine -eq 'end'){$taskSection='';continue};if(!$taskSection){continue}
    $taskP=$taskLine -split ','|ForEach-Object {$_.Trim()}
    if($taskP.Count -ne 11 -or !$taskIds.ContainsKey([int]$taskP[0]) -or $taskIds[[int]$taskP[0]].SaName -ne $taskP[1]){throw 'Invalid native placement binding'}
    $taskPart.Add($taskP);$taskPlaced.Add($taskP)
}
if($taskPart.Count -gt 4000 -or $taskPart.Count -ne $taskPlacementFile.Instances){throw 'IPL capacity or manifest count mismatch'}
for($taskI=0;$taskI -lt $taskPart.Count;$taskI++){
    $taskP=$taskPart[$taskI];$taskLod=[int]$taskP[10]
    if($taskLod -lt -1 -or $taskLod -ge $taskPart.Count -or $taskLod -eq $taskI){throw 'Placement LOD index out of local IPL range'}
    if($taskLod -ge 0){$taskLodLinks++}
    $taskVisited=@{};$taskNext=$taskI
    while($taskNext -ge 0){if($taskVisited.ContainsKey($taskNext)){throw 'Cyclic placement LOD chain'};$taskVisited[$taskNext]=$true;$taskNext=[int]$taskPart[$taskNext][10]}
}
$taskPlacementCounts.Add([pscustomobject]@{File=$taskPlacementFile.File;Instances=$taskPart.Count})
}
if($taskLodLinks -ne $taskManifest.LodLinks){throw 'Partitioned LOD link count mismatch'}
$taskCol=[IO.File]::ReadAllBytes((Join-Path $taskWorld 'models\lcs\world.col'));$taskOffset=0;$taskCols=@{}
while($taskOffset -lt $taskCol.Length){
    if($taskOffset+88 -gt $taskCol.Length -or [Text.Encoding]::ASCII.GetString($taskCol,$taskOffset,4) -ne 'COLL'){throw 'Invalid native collision chunk'}
    $taskSize=8+[BitConverter]::ToUInt32($taskCol,$taskOffset+4);$taskName=[Text.Encoding]::ASCII.GetString($taskCol,$taskOffset+8,24).Split([char]0)[0]
    if($taskSize -lt 88 -or $taskOffset+$taskSize -gt $taskCol.Length -or !$taskNames.ContainsKey($taskName) -or $taskCols.ContainsKey($taskName)){throw 'Invalid collision model binding'}
    $taskCols[$taskName]=$true;$taskOffset+=$taskSize
}
if($taskCols.Count -ne $taskIds.Count -or $taskPlaced.Count -ne $taskManifest.Instances){throw 'World manifest count mismatch'}
$taskConverter=Join-Path $PSScriptRoot '..\build\Release\lcs-convert-rw.exe'
$taskCollisionAudit=& $taskConverter (Join-Path $taskWorld 'models\lcs\world.col') --validate-col
if($LASTEXITCODE -or $taskCollisionAudit -notmatch 'maximum_surface_id=(\d+)'){throw 'Collision structure validation failed'}
if([int]$Matches[1] -ge 179){throw 'Collision material outside native SA surface table'}
if(Test-Path -LiteralPath (Join-Path $taskWorld 'rw-sa')){
    $taskGeometryAudit=& $taskConverter (Join-Path $taskWorld 'rw-sa') --validate
    if($LASTEXITCODE){throw 'Native map geometry/texture validation failed'}
}
# Validate every native bootstrap operand, output variable and loop boundary.
$taskBootstrap=(& (Join-Path $PSScriptRoot 'validate-bootstrap.ps1') -ScmFile (Join-Path $taskWorld 'data\script\main.scm'))|ConvertFrom-Json
[pscustomobject]@{NativeMapModels=$taskDff;TextureDictionaries=$taskTxd;NativePlacements=$taskPlaced.Count;PlacementFiles=$taskPlacementCounts;LodLinks=$taskLodLinks;CollisionBindings=$taskCols.Count;ArchiveBytes=(Get-Item -LiteralPath $taskImgPath).Length;BootstrapBytes=$taskBootstrap.Bytes;BootstrapEntry=$taskBootstrap.Entry;BootstrapCommands=$taskBootstrap.Commands;OfflineValidationPassed=$true;GameTested=$false;CampaignPlayable=$false}|ConvertTo-Json -Depth 4
