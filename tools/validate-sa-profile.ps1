param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [string]$WorldDirectory=(Join-Path $PSScriptRoot '..\out\world-stage'),
    [string]$ProfileFile,
    [string]$CoreDirectory
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'object-data.ps1')
$taskGame=(Resolve-Path -LiteralPath $GameDirectory).Path
$taskWorld=(Resolve-Path -LiteralPath $WorldDirectory).Path
if(!$ProfileFile){$ProfileFile=Join-Path $taskWorld 'data\gta.dat'}
function Resolve-Resource([string]$Relative){
    if([IO.Path]::IsPathRooted($Relative) -or $Relative -match '(^|[\\/])\.\.([\\/]|$)'){throw 'Invalid resource path'}
    $taskResource=Join-Path $taskWorld $Relative
    if(Test-Path -LiteralPath $taskResource -PathType Leaf){return $taskResource}
    $taskResource=Join-Path $taskGame $Relative
    if(!(Test-Path -LiteralPath $taskResource -PathType Leaf)){throw "Level resource missing: $Relative"}
    return $taskResource
}
$taskIdeFiles=[Collections.Generic.List[string]]::new()
$taskArchives=[Collections.Generic.List[string]]::new()
$taskCollisionFiles=[Collections.Generic.List[string]]::new()
$taskFirstMission=$false
foreach($taskName in @('gta3.img','gta_int.img')){
    if($CoreDirectory){$taskNative=Join-Path $CoreDirectory $taskName}
    elseif(Test-Path -LiteralPath (Join-Path $taskWorld ('models\sa-core-clean\'+$taskName))){$taskNative=Join-Path $taskWorld ('models\sa-core-clean\'+$taskName)}
    else{$taskNative=Join-Path $taskWorld ('models\sa-core\'+$taskName)}
    if(!(Test-Path -LiteralPath $taskNative)){$taskNative=Resolve-Resource ('models\'+$taskName)}
    $taskArchives.Add($taskNative)
}
foreach($taskProfile in @((Join-Path $taskGame 'data\default.dat'),$ProfileFile)){
    foreach($taskLine in [IO.File]::ReadLines($taskProfile)){
        $taskText=($taskLine -split '#')[0].Trim()
        if($taskText -match '^(IDE|IPL|IMG|COLFILE)\s+(.+)$'){
            $taskType=$Matches[1];$taskRelative=$Matches[2]
            if($taskType -eq 'COLFILE'){$taskRelative=$taskRelative -replace '^\d+\s+',''}
            $taskPath=Resolve-Resource $taskRelative
            if($taskType -eq 'IDE'){$taskIdeFiles.Add($taskPath)}
            if($taskType -eq 'IMG'){$taskArchives.Add($taskPath)}
            if($taskType -eq 'COLFILE'){$taskCollisionFiles.Add($taskPath)}
            if($taskType -eq 'IPL' -and $taskRelative -ieq 'DATA\LCS\FIRSTMISSION.IPL'){
                $taskGarageLines=@([IO.File]::ReadLines($taskPath) | ForEach-Object {$_.Trim()} | Where-Object {$_ -and ! $_.StartsWith('#')})
                if(($taskGarageLines -join "`n") -ne "grge`n1160.7 -267.5 16.25 1156 -267.5 1160.7 -273.7 19.25 0 19 LCSVIC1`nend"){throw 'Unexpected first-mission garage profile'}
                if($taskFirstMission){throw 'Duplicate first-mission garage IPL'}
                $taskFirstMission=$true
            }
            elseif($taskType -eq 'IPL' -and $taskRelative -ieq 'DATA\LCS\CAMPAIGN-ANCHORS.IPL'){
                if(!(Test-Path -LiteralPath (Join-Path $taskWorld 'metadata\campaign-anchors.json'))){throw 'Campaign carrier source audit missing'}
                $taskCarrierAudit=Get-Content -LiteralPath (Join-Path $taskWorld 'metadata\campaign-anchors.json') -Raw|ConvertFrom-Json
                if($taskCarrierAudit.gameExecuted -or $taskCarrierAudit.approvedMapMediaChanged){throw 'Invalid campaign carrier audit'}
            }
            elseif($taskType -eq 'IPL' -and $taskRelative -in @('DATA\LCS\INFO.ZON','DATA\LCS\MAP.ZON')){
                # Converted LCS zones (tools/convert-zones.py): one zone section only, navigation type 0 or map type 3.
                $taskZoneType=if($taskRelative -ieq 'DATA\LCS\INFO.ZON'){'0'}else{'3'}
                $taskZoneLines=@([IO.File]::ReadLines($taskPath) | ForEach-Object {$_.Trim()} | Where-Object {$_})
                if($taskZoneLines.Count -lt 3 -or $taskZoneLines[0] -ne 'zone' -or $taskZoneLines[-1] -ne 'end'){throw "Invalid converted zone file: $taskRelative"}
                foreach($taskZone in $taskZoneLines[1..($taskZoneLines.Count-2)]){
                    $taskZ=@($taskZone -split ',' | ForEach-Object {$_.Trim()})
                    if($taskZ.Count -ne 10 -or $taskZ[1] -ne $taskZoneType -or $taskZ[0].Length -gt 7 -or $taskZ[9].Length -gt 7){throw "Invalid converted zone row: $taskZone"}
                }
            }
            elseif($taskType -eq 'IPL' -and $taskRelative -notin @('DATA\MAP.ZON','DATA\INFO.ZON') -and $taskRelative -notmatch '^DATA\\LCS\\WORLD[0-9]*\.IPL$'){throw "Unexpected SA world placement file: $taskRelative"}
        }
    }
}
$taskModels=@{};$taskModelIds=@{};$taskCounts=@{}
foreach($taskIde in $taskIdeFiles){
    $taskSection=''
    foreach($taskLine in [IO.File]::ReadLines($taskIde)){
        $taskText=($taskLine -split '#')[0].Trim();if(!$taskText){continue}
        if($taskText -eq 'end'){$taskSection='';continue}
        if($taskText -match '^[a-z][a-z0-9]+$'){$taskSection=$taskText;continue}
        if($taskSection -notin @('objs','tobj','weap','hier','anim','cars','peds')){continue}
        $taskP=$taskText -split ','|ForEach-Object {$_.Trim()}
        if($taskP.Count -lt 3){throw "Invalid native model definition: $taskText"}
        $taskId=[int]$taskP[0];$taskName=$taskP[1]
        $taskSharedClothes=$taskSection -eq 'hier' -and $taskName -eq 'clothes01' -and $taskId -ge 384 -and $taskId -le 393 -and $taskIde -eq (Join-Path $taskGame 'data\default.ide')
        if($taskId -lt 0 -or $taskId -ge 20000 -or $taskModelIds.ContainsKey($taskId) -or ($taskModels.ContainsKey($taskName) -and !$taskSharedClothes)){throw "Conflicting native model: $taskText"}
        $taskDefinition=[pscustomobject]@{Id=$taskId;Name=$taskName;Texture=$taskP[2];Section=$taskSection;File=$taskIde}
        $taskModelIds[$taskId]=$taskDefinition;$taskModels[$taskName]=$taskDefinition
        $taskCounts[$taskSection]++
    }
}
# These stores have fixed capacities in the validated SA 1.0 executable.
foreach($taskLimit in @{objs=14000;tobj=169;weap=51;cars=212;peds=278}.GetEnumerator()){
    if($taskCounts[$taskLimit.Key] -gt $taskLimit.Value){throw "Native $($taskLimit.Key) model store capacity exceeded"}
}
$taskRequired=@('Cylinder','hoop','diamond_3','male01','colt45','nitestick','missile','Money','health','bodyarmour','info','bribe','pickupsave')
if($taskFirstMission){
    $taskRequired+=@('VMAFF1','VMAFF2','primo','lc1095')
    $taskObjectRows=@(Get-NativeObjectDataRows ([IO.File]::ReadAllText((Resolve-Resource 'data\object.dat'))))
    $taskObjectDoor=@($taskObjectRows | Where-Object {$_.Name -ieq 'lc1095'})
    $taskObjectDonor=@($taskObjectRows | Where-Object {$_.Name -ieq 'xref_garagedoor'})
    if($taskObjectDoor.Count -ne 1 -or $taskObjectDonor.Count -ne 1 -or
       (($taskObjectDoor[0].Fields | Select-Object -Skip 1) -join ' ') -ne
       (($taskObjectDonor[0].Fields | Select-Object -Skip 1) -join ' ')){
        throw 'First-mission garage door physics is missing before the native object.dat terminator'
    }
}
foreach($taskName in $taskRequired){if(!$taskModels.ContainsKey($taskName)){throw "Missing native engine definition: $taskName"}}
# An IDE + DFF alone does not make a usable object: CObject::Init dereferences
# its collision model even for money dropped by a pedestrian. SA world COL
# filtering must leave these dependencies explicitly loaded before any IPL.
$taskCollisionNames=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach($taskCollisionFile in $taskCollisionFiles){
    $taskColStream=[IO.File]::OpenRead($taskCollisionFile);$taskColReader=[IO.BinaryReader]::new($taskColStream)
    try{
        while($taskColStream.Position -lt $taskColStream.Length){
            $taskColStart=$taskColStream.Position
            if($taskColStream.Length-$taskColStart -lt 32){throw 'Truncated collision header'}
            $taskColMagic=[Text.Encoding]::ASCII.GetString($taskColReader.ReadBytes(4))
            $taskColSize=[long]$taskColReader.ReadUInt32()+8
            if($taskColMagic -notin @('COLL','COL2','COL3','COL4') -or $taskColSize -lt 72 -or $taskColStart+$taskColSize -gt $taskColStream.Length){throw 'Invalid collision record bounds/version'}
            $taskColName=[Text.Encoding]::ASCII.GetString($taskColReader.ReadBytes(22)).Split([char]0)[0]
            $null=$taskCollisionNames.Add($taskColName)
            $taskColStream.Position=$taskColStart+$taskColSize
        }
    }finally{$taskColReader.Dispose();$taskColStream.Dispose()}
}
foreach($taskName in @('Money','health','bodyarmour','info','bribe','pickupsave')){
    if(!$taskCollisionNames.Contains($taskName)){throw "Native pickup has no explicitly loaded collision: $taskName"}
}
$taskArchiveFiles=@{};$taskExtraModels=[Collections.Generic.List[string]]::new()
$taskAnimationNames=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$null=$taskAnimationNames.Add('ped')
$taskPathCounts=@{}
if($taskArchives.Count+1 -gt 8){throw 'Native IMG store capacity exceeded including PLAYER.IMG'}
foreach($taskArchive in $taskArchives){
    $taskStream=[IO.File]::OpenRead($taskArchive);$taskReader=[IO.BinaryReader]::new($taskStream)
    try{
        if([Text.Encoding]::ASCII.GetString($taskReader.ReadBytes(4)) -ne 'VER2'){throw "Not a native IMG v2: $taskArchive"}
        $taskCount=$taskReader.ReadUInt32();$taskMinimum=[Math]::Ceiling((8+32*[long]$taskCount)/2048)*2048
        if($taskMinimum -gt $taskStream.Length){throw 'IMG directory extends beyond archive'}
        for($taskI=0;$taskI -lt $taskCount;$taskI++){
            $taskOffset=[long]$taskReader.ReadUInt32()*2048;$taskSizes=$taskReader.ReadUInt32()
            $taskSize=[long]($taskSizes -band 65535)*2048
            if($taskSizes -shr 16){$taskSize=[long]($taskSizes -shr 16)*2048}
            $taskName=[Text.Encoding]::ASCII.GetString($taskReader.ReadBytes(24)).Split([char]0)[0]
            if(!$taskName -or $taskOffset -lt $taskMinimum -or $taskSize -le 0 -or $taskOffset+$taskSize -gt $taskStream.Length){throw "Invalid native archive entry: $taskName"}
            $taskArchiveFiles[$taskName]=$taskArchive
            if($taskName.EndsWith('.ifp',[StringComparison]::OrdinalIgnoreCase)){$null=$taskAnimationNames.Add([IO.Path]::GetFileNameWithoutExtension($taskName))}
            if($taskName -match '^nodes([0-9]+)\.dat$'){
                $taskArea=[int]$Matches[1]
                if($taskArea -ge 64 -or $taskPathCounts.ContainsKey($taskArea)){throw 'Invalid or duplicate streamed navigation area'}
                $taskPathCounts[$taskArea]=$taskArchive
            }
            if($taskName.EndsWith('.dff',[StringComparison]::OrdinalIgnoreCase) -and !$taskModels.ContainsKey([IO.Path]::GetFileNameWithoutExtension($taskName))){$taskExtraModels.Add($taskName)}
        }
    }finally{$taskReader.Dispose();$taskStream.Dispose()}
}
if($taskExtraModels.Count -gt 550){throw "Native extra-object directory overflow: $($taskExtraModels.Count) unregistered DFFs (capacity 550). Filter unused SA map models."}
foreach($taskIfp in Get-ChildItem -LiteralPath $taskWorld -Recurse -File -Filter '*.ifp'){
    $null=$taskAnimationNames.Add($taskIfp.BaseName)
}
$taskPrivateAnimationBlocks=0
$taskAnimationStorageFile=Join-Path $taskWorld 'data\lcs\animations\storage.json'
if(Test-Path -LiteralPath $taskAnimationStorageFile){
    $taskAnimationStorage=Get-Content -LiteralPath $taskAnimationStorageFile -Raw|ConvertFrom-Json
    if($taskAnimationStorage.nativeBlocks -ne 2 -or $taskAnimationStorage.automaticStreamingBlocks -ne 0 -or @(Get-ChildItem -LiteralPath (Join-Path $taskWorld 'data\lcs\animations') -Recurse -File -Filter '*.ifp').Count){throw 'Unsafe mission animation registration'}
    $taskPrivateAnimationBlocks=[int]$taskAnimationStorage.nativeBlocks
}
if($taskAnimationNames.Count+$taskPrivateAnimationBlocks -gt 180){throw "Native animation block table overflow: $($taskAnimationNames.Count) registered plus $taskPrivateAnimationBlocks private blocks (capacity 180)."}
if(!$taskExtraModels.Contains('player.dff')){throw 'Native player.dff is missing from the extra-object directory'}
if($taskPathCounts.Count -ne 64){throw 'Missing native navigation streaming entries: LOAD_SCENE still requests all path areas'}
$taskNavigation=(& (Join-Path $PSScriptRoot 'validate-navigation.ps1') -ArchiveFile (Resolve-Resource 'models\lcs\paths.img'))|ConvertFrom-Json
foreach($taskName in $taskRequired){
    $taskDefinition=$taskModels[$taskName]
    foreach($taskResource in @(($taskDefinition.Name+'.dff'),($taskDefinition.Texture+'.txd'))){
        if(!$taskArchiveFiles.ContainsKey($taskResource) -and !(Test-Path -LiteralPath (Join-Path $taskGame ('models\generic\'+$taskResource)))){throw "Native engine asset unavailable: $taskResource"}
    }
}
[pscustomobject]@{NativeProfileValidated=$true;NativePickupCollisionsValidated=$true;ExplicitCollisionModels=$taskCollisionNames.Count;ModelDefinitions=$taskModels.Count;ModelStores=$taskCounts;NativeRequiredModels=$taskRequired;ArchiveEntries=$taskArchiveFiles.Count;ExtraObjectDirectoryEntries=$taskExtraModels.Count;ExtraObjectDirectoryCapacity=550;NativePlayerPresent=$true;NativeNavigationAreas=$taskNavigation.Areas;ImgSlotsIncludingPlayer=$taskArchives.Count+1;AnimationBlocksRegistered=$taskAnimationNames.Count;AnimationBlocksPrivate=$taskPrivateAnimationBlocks;AnimationBlockCapacity=180;WorldPlacements='LCS only';GameExecuted=$false}|ConvertTo-Json -Depth 4
