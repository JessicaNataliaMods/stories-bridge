param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [string]$BaseArchiveDirectory=$GameDirectory,
    [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\campaign-cutscenes-mounted'),
    [switch]$IncludeArrival
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'cutscene-binding-policy.ps1')
$taskProject=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskGame=(Resolve-Path -LiteralPath $GameDirectory).Path
$taskPrepared=Join-Path $taskProject 'out\campaign-cutscenes-prepared'
$taskMounted=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $taskMounted){throw 'Mounted package already exists; preserve it and choose a fresh output before preparing again'}
& (Join-Path $PSScriptRoot 'prepare-campaign-cutscenes.ps1') -ValidateOnly | Out-Null
$taskManifest=Get-Content -LiteralPath (Join-Path $taskPrepared 'manifest.json') -Raw | ConvertFrom-Json
$taskImg=Join-Path $taskProject 'build\Release\lcs-img.exe'
$taskIfp=Join-Path $taskProject 'build\Release\lcs-ifp-adapt.exe'
$taskAscii=[Text.Encoding]::ASCII
$taskModelFolder=Join-Path $taskMounted 'models'
$taskCutFolder=Join-Path $taskMounted 'cuts'
$taskAudioFolder=Join-Path $taskMounted 'data\lcs\cutscenes\audio'
New-Item -ItemType Directory -Path $taskCutFolder,$taskAudioFolder -Force | Out-Null
$taskBase=(Resolve-Path -LiteralPath $BaseArchiveDirectory).Path
$taskOriginalModelArchive=Join-Path $taskBase 'models\cutscene.img'
$taskOriginalCutsArchive=Join-Path $taskBase 'anim\cuts.img'
$taskOriginalModelHash=(Get-FileHash -LiteralPath $taskOriginalModelArchive -Algorithm SHA256).Hash
$taskOriginalCutsHash=(Get-FileHash -LiteralPath $taskOriginalCutsArchive -Algorithm SHA256).Hash
if($taskOriginalModelHash -cne 'E065F1B47EC81736E19762D1FA50AE207E51F782ACB74778B80EFBA931092643' -or
   $taskOriginalCutsHash -cne '28D9B9002AC5479106C06CE0FC390120F24E0AC2A2395517BADBCBD0ADF502CB'){throw 'Unexpected base native cutscene archives'}
$taskPredecessorModelHash=(Get-FileHash -LiteralPath (Join-Path $taskGame 'models\cutscene.img') -Algorithm SHA256).Hash
$taskPredecessorCutsHash=(Get-FileHash -LiteralPath (Join-Path $taskGame 'anim\cuts.img') -Algorithm SHA256).Hash
& $taskImg unpack-native $taskOriginalModelArchive $taskModelFolder
if($LASTEXITCODE -ne 0){throw 'Native cutscene model archive extraction failed'}
$taskModels=@{}
$taskModelIndex=0
foreach($taskModel in @(Get-ChildItem -LiteralPath (Join-Path $taskPrepared 'models') -Filter '*.dff' | Sort-Object Name)){
    $taskAlias='lccs'+$taskModelIndex.ToString('D2');$taskModelIndex++
    $taskModels[$taskModel.BaseName.ToLowerInvariant()]=$taskAlias
    foreach($taskExt in @('dff','txd')){
        $taskTarget=Join-Path $taskModelFolder ($taskAlias+'.'+$taskExt)
        if(Test-Path -LiteralPath $taskTarget){throw 'LCS alias collides with an existing native resource'}
        Copy-Item -LiteralPath (Join-Path $taskPrepared ('models\'+$taskModel.BaseName+'.'+$taskExt)) -Destination $taskTarget
    }
}
$taskScenes=[Collections.Generic.List[object]]::new();$taskSceneIndex=0
foreach($taskScene in @($taskManifest.Scenes | Sort-Object Name)){
    $taskSceneIndex++;$taskAlias='L'+$taskSceneIndex.ToString('D4')
    $taskIgnored=@()
    if(!$taskScene.BindingsComplete){
        if(!$IncludeArrival -or $taskScene.Name -cne 'intro'){continue}
        $taskIgnored=@(Get-IntroBindingPolicy $taskScene $taskPrepared)
    }
    $taskName=$taskScene.Name.ToLowerInvariant()
    $taskLines=[IO.File]::ReadAllLines((Join-Path $taskPrepared ('cuts\'+$taskName+'.cut')),$taskAscii)
    $taskInModels=$false;$taskRows=0
    for($taskI=0;$taskI -lt $taskLines.Length;$taskI++){
        $taskLine=$taskLines[$taskI].Trim()
        if($taskLine -ceq 'model'){$taskInModels=$true;continue}
        if($taskLine -ceq 'end'){$taskInModels=$false;continue}
        if($taskInModels){
            $taskFields=$taskLine.Split(',')
            if($taskFields.Length -ne 3){throw 'Unexpected original CUT model row'}
            $taskModelName=$taskFields[1].Trim().ToLowerInvariant()
            if(!$taskModels.ContainsKey($taskModelName)){throw 'Missing verified native cutscene model'}
            $taskLines[$taskI]=$taskFields[0].Trim()+', '+$taskModels[$taskModelName]+', '+$taskFields[2].Trim();$taskRows++
        }
    }
    if($taskRows -ne $taskScene.Objects){throw 'CUT model row count changed'}
    [IO.File]::WriteAllLines((Join-Path $taskCutFolder ($taskAlias+'.cut')),$taskLines,$taskAscii)
    Copy-Item -LiteralPath (Join-Path $taskPrepared ('cuts\'+$taskName+'.dat')) -Destination (Join-Path $taskCutFolder ($taskAlias+'.dat'))
    & $taskIfp (Join-Path $taskPrepared ('cuts\'+$taskName+'.ifp')) (Join-Path $taskCutFolder ($taskAlias+'.ifp')) --block-name $taskAlias | Out-Null
    if($LASTEXITCODE -ne 0){throw 'Native animation block aliasing failed'}
    $taskAudio=Join-Path $taskAudioFolder ($taskName+'.wav')
    Copy-Item -LiteralPath (Join-Path $taskPrepared ('audio\'+$taskName+'.wav')) -Destination $taskAudio
    $taskScenes.Add([pscustomobject]@{Name=$taskName;NativeName=$taskAlias;AudioSha256=(Get-FileHash -LiteralPath $taskAudio -Algorithm SHA256).Hash;BindingsComplete=[bool]$taskScene.BindingsComplete;RequiredBindingsComplete=$true;IgnoredSequences=$taskIgnored})
}
$taskCuts=Join-Path $taskMounted 'anim\cuts.img';$taskModelArchive=Join-Path $taskMounted 'models\cutscene.img'
& $taskImg pack $taskCutFolder $taskCuts
if($LASTEXITCODE -ne 0){throw 'Native cutscene directory packing failed'}
& $taskImg pack $taskModelFolder $taskModelArchive
if($LASTEXITCODE -ne 0){throw 'Merged native cutscene model archive packing failed'}
$taskCutsHash=(Get-FileHash -LiteralPath $taskCuts -Algorithm SHA256).Hash
$taskModelsHash=(Get-FileHash -LiteralPath $taskModelArchive -Algorithm SHA256).Hash
$taskIndex=[Collections.Generic.List[string]]::new()
$taskIndex.Add("LCSCUT1`t$taskCutsHash`t$taskModelsHash")
foreach($taskScene in $taskScenes){$taskIndex.Add($taskScene.Name+"`t"+$taskScene.NativeName+"`t"+$taskScene.AudioSha256)}
[IO.File]::WriteAllLines((Join-Path $taskMounted 'data\lcs\cutscenes\index.tsv'),$taskIndex,$taskAscii)
$taskFiles=@(Get-ChildItem -LiteralPath (Join-Path $taskMounted 'data') -File -Recurse | ForEach-Object {[pscustomobject]@{Relative=$_.FullName.Substring($taskMounted.Length+1);Sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})
$taskFiles+=@([pscustomobject]@{Relative='anim\cuts.img';Sha256=$taskCutsHash},[pscustomobject]@{Relative='models\cutscene.img';Sha256=$taskModelsHash})
[pscustomobject]@{Prepared=$true;Installed=$false;GameExecuted=$false;CutscenesPlayable=$false;CampaignPlayable=$false;OriginalModelArchiveSha256=$taskOriginalModelHash;OriginalCutsArchiveSha256=$taskOriginalCutsHash;PredecessorModelArchiveSha256=$taskPredecessorModelHash;PredecessorCutsArchiveSha256=$taskPredecessorCutsHash;ModelAliases=$taskModels;Scenes=$taskScenes;Files=$taskFiles} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskMounted 'manifest.json') -Encoding UTF8
Write-Output "Native cutscene package: $($taskScenes.Count) scenes with verified required bindings, $taskModelIndex model aliases. Existing native archive resources retained. Game untouched."
