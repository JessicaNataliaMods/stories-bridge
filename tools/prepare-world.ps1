param(
    [string]$ConvertedDirectory=(Join-Path $PSScriptRoot '..\out\lcs-converted'),
    [string]$ExtractedModels=(Join-Path $PSScriptRoot '..\out\img-v1'),
    [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\world-stage')
)
$ErrorActionPreference='Stop'
$taskCulture=[Globalization.CultureInfo]::InvariantCulture
$taskSource=(Resolve-Path -LiteralPath $ConvertedDirectory).Path
$taskModels=(Resolve-Path -LiteralPath $ExtractedModels).Path
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $taskOutput) {throw 'Output directory already exists; use a new staging directory'}
$taskDefinitions=[Collections.Generic.List[object]]::new()
$taskIds=@{};$taskNames=@{};$taskTextures=@{}
$taskIdeFiles=Get-ChildItem -LiteralPath (Join-Path $taskSource 'DATA\Maps') -Filter '*.ide' | Sort-Object Name
foreach($taskFile in $taskIdeFiles) {
    $taskSection=''
    foreach($taskLine in [IO.File]::ReadLines($taskFile.FullName)) {
        $taskText=($taskLine -split '#')[0].Trim()
        if(!$taskText) {continue}
        if($taskText -in @('objs','tobj')) {$taskSection=$taskText;continue}
        if($taskText -eq 'end') {$taskSection='';continue}
        if(!$taskSection) {continue}
        $taskP=$taskText -split ',' | ForEach-Object {$_.Trim()}
        $taskId=[int]$taskP[0];$taskCount=[int]$taskP[3]
        $taskExpected=5+$taskCount+$(if($taskSection -eq 'tobj'){2}else{0})
        if($taskP.Count -ne $taskExpected -or $taskCount -lt 1 -or $taskCount -gt 3) {throw "Invalid LCS object definition: $taskText"}
        if($taskIds.ContainsKey($taskId) -or $taskNames.ContainsKey($taskP[1])) {throw "Conflicting object definition: $taskText"}
        $taskD=[pscustomobject]@{LcsId=$taskId;SaId=0;Name=$taskP[1];SaName=('lc'+$taskId);Txd=$taskP[2];SaTxd='';Section=$taskSection;Draw=[double]::Parse($taskP[4],$taskCulture);LcsFlags=[int]$taskP[4+$taskCount];Flags=0;TimeOn=0;TimeOff=0;Available=$false}
        # Source flags 2/16/32/256 are LCS no-fade/subway/ignore-light/draw-distance.
        # SA uses these bits for different properties. Preserve only shared bits.
        $taskD.Flags=$taskD.LcsFlags -band 0x6CD
        if($taskSection -eq 'tobj') {$taskD.TimeOn=[int]$taskP[5+$taskCount];$taskD.TimeOff=[int]$taskP[6+$taskCount]}
        $taskDefinitions.Add($taskD);$taskIds[$taskId]=$taskD;$taskNames[$taskD.Name]=$taskD
        $taskTextures[$taskD.Txd]=''
    }
}
$taskNext=10000
foreach($taskD in ($taskDefinitions | Sort-Object LcsId)) {$taskD.SaId=$taskNext;$taskNext++}
if($taskNext -gt 20000) {throw 'Native model capacity exceeded'}
$taskNext=0
foreach($taskTxd in @($taskTextures.Keys | Sort-Object)) {$taskTextures[$taskTxd]='lt'+$taskNext;$taskNext++}
$taskInput=Join-Path $taskOutput 'rw-input'
$taskData=Join-Path $taskOutput 'data\lcs'
$taskNativeModels=Join-Path $taskOutput 'models\lcs'
New-Item -ItemType Directory -Path $taskInput,$taskData,$taskNativeModels -Force | Out-Null
$taskMissing=[Collections.Generic.List[string]]::new()
foreach($taskD in $taskDefinitions) {
    $taskD.SaTxd=$taskTextures[$taskD.Txd]
    $taskDff=Join-Path $taskModels ($taskD.Name+'.dff')
    $taskTxd=Join-Path $taskModels ($taskD.Txd+'.txd')
    if(!(Test-Path -LiteralPath $taskTxd)) {$taskTxd=Join-Path $taskSource ('models\'+$taskD.Txd+'.txd')}
    # The shipped fire_chevs IDE references an absent editor dictionary "temp".
    # Its only material uses chevron64HVa, present in the shipped factory TXD.
    if(!(Test-Path -LiteralPath $taskTxd) -and $taskD.Txd -eq 'temp' -and $taskD.Name -eq 'fire_chevs') {$taskTxd=Join-Path $taskModels 'factory.txd'}
    if((Test-Path -LiteralPath $taskDff) -and (Test-Path -LiteralPath $taskTxd)) {
        Copy-Item -LiteralPath $taskDff -Destination (Join-Path $taskInput ($taskD.SaName+'.dff'))
        if(!(Test-Path -LiteralPath (Join-Path $taskInput ($taskD.SaTxd+'.txd')))) {Copy-Item -LiteralPath $taskTxd -Destination (Join-Path $taskInput ($taskD.SaTxd+'.txd'))}
        $taskD.Available=$true
    } else {$taskMissing.Add($taskD.Name+': dff='+[bool](Test-Path -LiteralPath $taskDff)+' txd='+[bool](Test-Path -LiteralPath $taskTxd))}
}
Copy-Item -LiteralPath (Join-Path $taskSource 'models\generic.txd') -Destination (Join-Path $taskInput 'lcgeneric.txd')
$taskInstances=[Collections.Generic.List[object]]::new()
$taskSkippedSections=@{};$taskScaleIgnored=0
# Follow the base world's load list, preserving separately prepared mission
# geometry for future swaps. Loading every alternative at once causes overlap.
$taskBaseFiles=@('commer.ipl','suburb.ipl','overview.ipl','indust.ipl')
foreach($taskFileName in $taskBaseFiles) {
    $taskSection=''
    foreach($taskLine in [IO.File]::ReadLines((Join-Path $taskSource ('DATA\Maps\'+$taskFileName)))) {
        $taskText=($taskLine -split '#')[0].Trim();if(!$taskText){continue}
        if($taskText -eq 'end'){$taskSection='';continue}
        if($taskText -match '^[a-zA-Z][a-zA-Z0-9]+$'){$taskSection=$taskText;continue}
        if($taskSection -ne 'inst') {$taskSkippedSections[$taskSection]++;continue}
        $taskP=$taskText -split ',' | ForEach-Object {$_.Trim()}
        if($taskP.Count -ne 13) {throw "Invalid LCS placement: $taskText"}
        $taskD=$taskIds[[int]$taskP[0]]
        if(!$taskD -or !$taskD.Available) {throw "Placement lacks a usable model/texture: $taskText"}
        if($taskD.Name -ne $taskP[1]) {throw "Placement model/name mismatch: $taskText"}
        $taskXYZ=@($taskP[3..5] | ForEach-Object {[double]::Parse($_,$taskCulture)})
        # reLCS FileLoader::LoadObjectInstance reads scale but never applies it.
        # Match that behavior. These artist values must not be baked a second time.
        if(@($taskP[6..8] | Where-Object {[Math]::Abs([double]::Parse($_,$taskCulture)-1) -gt 0.00001}).Count) {$taskScaleIgnored++}
        $taskInstances.Add([pscustomobject]@{Definition=$taskD;Fields=$taskP;Position=$taskXYZ;Lod=-1})
    }
}
$taskLodGroups=@{}
for($taskI=0;$taskI -lt $taskInstances.Count;$taskI++) {
    $taskInstance=$taskInstances[$taskI];$taskName=$taskInstance.Definition.Name
    if($taskName.Length -gt 3 -and $taskName.StartsWith('lod',[StringComparison]::OrdinalIgnoreCase)) {
        $taskKey=$taskName.Substring(3)
        if(!$taskLodGroups.ContainsKey($taskKey)) {$taskLodGroups[$taskKey]=[Collections.Generic.List[int]]::new()}
        $taskLodGroups[$taskKey].Add($taskI)
    }
}
$taskLodLinks=0
for($taskI=0;$taskI -lt $taskInstances.Count;$taskI++) {
    $taskInstance=$taskInstances[$taskI];$taskName=$taskInstance.Definition.Name
    if($taskName.Length -le 3 -or $taskName.StartsWith('lod',[StringComparison]::OrdinalIgnoreCase)) {continue}
    $taskCandidates=$taskLodGroups[$taskName.Substring(3)]
    if(!$taskCandidates) {continue}
    $taskDistance=0.01;$taskBest=-1
    foreach($taskCandidate in $taskCandidates) {
        $taskOther=$taskInstances[$taskCandidate];$taskDist=0
        for($taskAxis=0;$taskAxis -lt 3;$taskAxis++) {$taskDelta=$taskInstance.Position[$taskAxis]-$taskOther.Position[$taskAxis];$taskDist+=$taskDelta*$taskDelta}
        if($taskDist -lt $taskDistance) {$taskDistance=$taskDist;$taskBest=$taskCandidate}
    }
    if($taskBest -ge 0) {$taskInstance.Lod=$taskBest;$taskLodLinks++}
}
$taskIde=[Collections.Generic.List[string]]::new()
foreach($taskSection in @('objs','tobj')) {
    $taskIde.Add($taskSection)
    foreach($taskD in ($taskDefinitions | Where-Object {$_.Available -and $_.Section -eq $taskSection} | Sort-Object SaId)) {
        $taskLine=('{0}, {1}, {2}, {3}, {4}' -f $taskD.SaId,$taskD.SaName,$taskD.SaTxd,$taskD.Draw.ToString('R',$taskCulture),$taskD.Flags)
        if($taskSection -eq 'tobj') {$taskLine+=(', {0}, {1}' -f $taskD.TimeOn,$taskD.TimeOff)}
        $taskIde.Add($taskLine)
    }
    $taskIde.Add('end')
}
$taskIde.Add('txdp')
foreach($taskTxd in ($taskTextures.Values | Sort-Object)) {if(Test-Path -LiteralPath (Join-Path $taskInput ($taskTxd+'.txd'))){$taskIde.Add($taskTxd+', lcgeneric')}}
$taskIde.Add('end')
[IO.File]::WriteAllLines((Join-Path $taskData 'world.ide'),$taskIde,[Text.Encoding]::ASCII)
$taskIpl=[Collections.Generic.List[string]]::new();$taskIpl.Add('inst')
foreach($taskInstance in $taskInstances) {
    $taskP=$taskInstance.Fields;$taskD=$taskInstance.Definition
    $taskIpl.Add(($taskD.SaId.ToString()+', '+$taskD.SaName+', '+($taskP[2..5] -join ', ')+', '+($taskP[9..12] -join ', ')+', '+$taskInstance.Lod))
}
$taskIpl.Add('end');[IO.File]::WriteAllLines((Join-Path $taskData 'world.ipl'),$taskIpl,[Text.Encoding]::ASCII)
$taskCollision=New-Object IO.MemoryStream;$taskColNames=@{};$taskColCount=0
$taskColFiles=@(Get-ChildItem -LiteralPath $taskModels -Filter '*.col')+@(Get-ChildItem -LiteralPath (Join-Path $taskSource 'models\coll') -Filter '*.col')
foreach($taskColFile in $taskColFiles) {
    if($taskColFile.Name -in @('peds.col','vehicles.col','weapons.col')) {continue}
    $taskBytes=[IO.File]::ReadAllBytes($taskColFile.FullName);$taskOffset=0
    while($taskOffset+32 -le $taskBytes.Length) {
        $taskMagic=[Text.Encoding]::ASCII.GetString($taskBytes,$taskOffset,4)
        if($taskMagic -eq ([string][char]0)*4){break}
        if($taskMagic -ne 'COLL'){throw "Unsupported collision format in $($taskColFile.Name)"}
        $taskSize=8+[BitConverter]::ToUInt32($taskBytes,$taskOffset+4)
        if($taskSize -lt 88 -or $taskOffset+$taskSize -gt $taskBytes.Length){throw 'Invalid collision chunk bounds'}
        $taskName=[Text.Encoding]::ASCII.GetString($taskBytes,$taskOffset+8,24).Split([char]0)[0]
        $taskD=$taskNames[$taskName]
        if($taskD -and $taskD.Available -and !$taskColNames.ContainsKey($taskD.SaName)) {
            $taskChunk=New-Object byte[] $taskSize;[Array]::Copy($taskBytes,$taskOffset,$taskChunk,0,$taskSize)
            [Array]::Clear($taskChunk,8,24);$taskNewName=[Text.Encoding]::ASCII.GetBytes($taskD.SaName);[Array]::Copy($taskNewName,0,$taskChunk,8,$taskNewName.Length)
            $taskCollision.Write($taskChunk,0,$taskChunk.Length);$taskColNames[$taskD.SaName]=$true;$taskColCount++
        }
        $taskOffset+=$taskSize
    }
}
[IO.File]::WriteAllBytes((Join-Path $taskNativeModels 'world.col'),$taskCollision.ToArray());$taskCollision.Dispose()
$taskColConverter=Join-Path $PSScriptRoot '..\build\Release\lcs-convert-rw.exe'
& $taskColConverter (Join-Path $taskNativeModels 'world.col') --map-col (Join-Path $taskNativeModels 'world-mapped.col')
if($LASTEXITCODE){throw 'Native collision surface conversion failed'}
Copy-Item -LiteralPath (Join-Path $taskNativeModels 'world-mapped.col') -Destination (Join-Path $taskNativeModels 'world.col') -Force
[IO.File]::WriteAllLines((Join-Path $taskData 'level.dat'),@('IMG MODELS\LCS\WORLD.IMG','IDE DATA\LCS\WORLD.IDE','COLFILE 0 MODELS\LCS\WORLD.COL','IPL DATA\LCS\WORLD.IPL'),[Text.Encoding]::ASCII)
$taskDefinitions | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $taskData 'model-bindings.json') -Encoding utf8
$taskBindingLines=@($taskDefinitions | Where-Object Available | Sort-Object SaId | ForEach-Object {"$($_.LcsId)`t$($_.SaId)`t$($_.Name)`t$($_.SaName)"})
[IO.File]::WriteAllLines((Join-Path $taskData 'model-bindings.tsv'),$taskBindingLines,[Text.Encoding]::ASCII)
$taskMissing | Set-Content -LiteralPath (Join-Path $taskData 'unused-missing-models.txt') -Encoding utf8
$taskManifest=[pscustomobject]@{WorldPrepared=$false;GameTested=$false;Source='reLCS PS2 converted';Definitions=$taskDefinitions.Count;AvailableModels=@($taskDefinitions|Where-Object Available).Count;Instances=$taskInstances.Count;LodLinks=$taskLodLinks;CollisionModels=$taskColCount;NativeSurfaceMapping=$true;ScaleFieldsIgnoredLikeRelcs=$taskScaleIgnored;SkippedSections=$taskSkippedSections;Pending=@('road graph conversion','mission building swaps','engine test');SourceDirectory=$taskSource}
$taskManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskData 'world-manifest.json') -Encoding utf8
$taskConverter=Join-Path $PSScriptRoot '..\build\Release\lcs-convert-rw.exe'
$taskConverted=Join-Path $taskOutput 'rw-sa'
& $taskConverter $taskInput $taskConverted --atomic | Out-File -LiteralPath (Join-Path $taskData 'conversion.log') -Encoding utf8
if($LASTEXITCODE){throw 'Native map geometry conversion failed'}
& (Join-Path $PSScriptRoot '..\build\Release\lcs-img.exe') pack $taskConverted (Join-Path $taskNativeModels 'world.img')
if($LASTEXITCODE){throw 'Native map IMG packing failed'}
& (Join-Path $PSScriptRoot 'prepare-navigation.ps1') -OutputFile (Join-Path $taskNativeModels 'paths.img')
if(!$?){throw 'Native empty navigation preparation failed'}
$taskManifest.WorldPrepared=$true
$taskManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskData 'world-manifest.json') -Encoding utf8
& (Join-Path $PSScriptRoot 'split-world-ipl.ps1') -WorldDirectory $taskOutput
if(!$?){throw 'Native IPL partition failed'}
$taskManifest | ConvertTo-Json -Depth 4
