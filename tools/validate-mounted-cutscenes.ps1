param([string]$PackageDirectory=(Join-Path $PSScriptRoot '..\out\campaign-cutscenes-mounted'))
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'cutscene-binding-policy.ps1')
function Get-TaskHash([string]$Path){
    $taskStream=[IO.File]::OpenRead($Path);$taskHasher=[Security.Cryptography.SHA256]::Create()
    try{return [BitConverter]::ToString($taskHasher.ComputeHash($taskStream)).Replace('-','')}
    finally{$taskStream.Dispose();$taskHasher.Dispose()}
}
function Read-TaskImg([string]$Path){
    $taskStream=[IO.File]::OpenRead($Path);$taskReader=[IO.BinaryReader]::new($taskStream)
    try{
        if([Text.Encoding]::ASCII.GetString($taskReader.ReadBytes(4)) -cne 'VER2'){throw 'Mounted scene archive is not native IMG v2'}
        $taskCount=$taskReader.ReadUInt32();$taskStart=[Math]::Ceiling((8+32*[long]$taskCount)/2048)*2048
        if($taskCount -gt 100000 -or $taskStart -gt $taskStream.Length){throw 'Invalid mounted IMG directory'}
        $taskEntries=@{}
        for($taskI=0;$taskI -lt $taskCount;$taskI++){
            $taskOffset=[long]$taskReader.ReadUInt32()*2048;$taskSizes=$taskReader.ReadUInt32()
            $taskSectors=$taskSizes -band 65535;if($taskSizes -shr 16){$taskSectors=$taskSizes -shr 16}
            $taskBytes=$taskReader.ReadBytes(24);$taskName=[Text.Encoding]::ASCII.GetString($taskBytes).Split([char]0)[0]
            $taskSize=[long]$taskSectors*2048
            if(!$taskName -or $taskName -match '[\\/:]' -or $taskName.Length -gt 23 -or $taskEntries.ContainsKey($taskName) -or $taskOffset -lt $taskStart -or !$taskSize -or $taskOffset+$taskSize -gt $taskStream.Length){throw 'Invalid/duplicate mounted IMG entry'}
            $taskEntries[$taskName]=[pscustomobject]@{Name=$taskName;Offset=$taskOffset;Size=$taskSize}
        }
        $taskRanges=@($taskEntries.Values | Sort-Object Offset)
        for($taskI=1;$taskI -lt $taskRanges.Count;$taskI++){if($taskRanges[$taskI-1].Offset+$taskRanges[$taskI-1].Size -gt $taskRanges[$taskI].Offset){throw 'Overlapping mounted IMG entries'}}
        return $taskEntries
    }finally{$taskReader.Dispose();$taskStream.Dispose()}
}
$taskPackage=(Resolve-Path -LiteralPath $PackageDirectory).Path
$taskManifest=Get-Content -LiteralPath (Join-Path $taskPackage 'manifest.json') -Raw | ConvertFrom-Json
if(!$taskManifest.Prepared -or $taskManifest.GameExecuted -or $taskManifest.CutscenesPlayable -or $taskManifest.CampaignPlayable){throw 'Package metadata falsely claims game validation'}
if(@($taskManifest.Scenes | Where-Object Name -ceq 'intro').Count){
    # Recompute every required association, camera and audio property first;
    # the five terminal helpers must be the ONLY unresolved INTRO sequences.
    & (Join-Path $PSScriptRoot 'prepare-campaign-cutscenes.ps1') -ValidateOnly | Out-Null
}
foreach($taskFile in $taskManifest.Files){
    $taskPath=[IO.Path]::GetFullPath((Join-Path $taskPackage $taskFile.Relative))
    if(!$taskPath.StartsWith($taskPackage+'\',[StringComparison]::OrdinalIgnoreCase) -or (Get-TaskHash $taskPath) -cne $taskFile.Sha256){throw 'Mounted artifact hash/path mismatch'}
}
$taskCuts=Read-TaskImg (Join-Path $taskPackage 'anim\cuts.img')
$taskModels=Read-TaskImg (Join-Path $taskPackage 'models\cutscene.img')
if($taskManifest.Scenes.Count -notin @(20,21) -or $taskCuts.Count -ne 3*$taskManifest.Scenes.Count -or $taskModels.Count -ne 716){throw 'Unexpected mounted native inventory'}
$taskRows=[IO.File]::ReadAllLines((Join-Path $taskPackage 'data\lcs\cutscenes\index.tsv'))
$taskHeader=$taskRows[0].Split("`t")
if($taskHeader.Length -ne 3 -or $taskHeader[0] -cne 'LCSCUT1' -or $taskHeader[1] -cne (Get-TaskHash (Join-Path $taskPackage 'anim\cuts.img')) -or $taskHeader[2] -cne (Get-TaskHash (Join-Path $taskPackage 'models\cutscene.img'))){throw 'Native runtime archive index mismatch'}
if($taskRows.Length -ne 1+$taskManifest.Scenes.Count){throw 'Unexpected complete-scene registry count'}
$taskIfp=Join-Path $PSScriptRoot '..\build\Release\lcs-ifp-inspect.exe'
$taskNames=@{};$taskAliases=@{}
foreach($taskScene in $taskManifest.Scenes){
    $taskName=$taskScene.Name;$taskAlias=$taskScene.NativeName
    if(!$taskScene.BindingsComplete){
        $taskPrepared=Join-Path $PSScriptRoot '..\out\campaign-cutscenes-prepared'
        $taskSourceManifest=Get-Content -LiteralPath (Join-Path $taskPrepared 'manifest.json') -Raw | ConvertFrom-Json
        $taskSourceScene=@($taskSourceManifest.Scenes | Where-Object Name -ceq $taskName)
        if($taskSourceScene.Count -ne 1 -or !$taskScene.RequiredBindingsComplete){throw 'Unresolved scene registered without verified policy'}
        $taskPolicy=@(Get-IntroBindingPolicy $taskSourceScene[0] $taskPrepared)
        if($taskScene.IgnoredSequences.Count -ne 5 -or ($taskScene.IgnoredSequences | ConvertTo-Json -Compress) -cne ($taskPolicy | ConvertTo-Json -Compress)){throw 'Unresolved scene policy changed'}
    }elseif($null -ne $taskScene.IgnoredSequences -and @($taskScene.IgnoredSequences).Count){throw 'Fully bound scene has unexpected ignored sequences'}
    if($taskNames.ContainsKey($taskName) -or $taskAliases.ContainsKey($taskAlias) -or $taskAlias -cnotmatch '^L[0-9]{4}$'){throw 'Duplicate scene registered as playable resource'}
    $taskNames[$taskName]=$true;$taskAliases[$taskAlias]=$true
    if($taskRows -cnotcontains ($taskName+"`t"+$taskAlias+"`t"+$taskScene.AudioSha256)){throw 'Scene index identity/audio mismatch'}
    foreach($taskExt in @('cut','dat','ifp')){if(!$taskCuts.ContainsKey($taskAlias+'.'+$taskExt)){throw 'Aliased scene file absent from native IMG'}}
    $taskInfo=@(& $taskIfp (Join-Path $taskPackage ('cuts\'+$taskAlias+'.ifp')))
    if($LASTEXITCODE -ne 0 -or $taskInfo[0] -cnotmatch ('^block='+$taskAlias+' animations=')){throw 'Native association block alias mismatch'}
    $taskInModels=$false
    foreach($taskLine in [IO.File]::ReadLines((Join-Path $taskPackage ('cuts\'+$taskAlias+'.cut')))){
        $taskText=$taskLine.Trim();if($taskText -ceq 'model'){$taskInModels=$true;continue};if($taskText -ceq 'end'){$taskInModels=$false;continue}
        if($taskInModels){$taskParts=$taskText.Split(',');$taskModel=$taskParts[1].Trim();if($taskModel -cnotmatch '^lccs[0-9]{2}$' -or !$taskModels.ContainsKey($taskModel+'.dff') -or !$taskModels.ContainsKey($taskModel+'.txd')){throw 'Original scene model has no aliased native DFF/TXD'}}
    }
}
if(!$taskNames.ContainsKey('homswet') -or ($taskManifest.Scenes.Count -eq 21 -and !$taskNames.ContainsKey('intro')) -or $taskNames.ContainsKey('crazy69') -or $taskNames.ContainsKey('sayonar') -or $taskNames.ContainsKey('cuttest')){throw 'Opening missing or unresolved scene registered'}
[pscustomobject]@{MountedPackageValidated=$true;Scenes=$taskManifest.Scenes.Count;ModelAliases=41;NativeModelArchiveEntries=$taskModels.Count;NativeCutsArchiveEntries=$taskCuts.Count;NativeModelsRetained=634;GameExecuted=$false;CutscenesPlayableConfirmed=$false;CampaignPlayable=$false} | ConvertTo-Json
