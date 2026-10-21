param(
    [string]$AssetsDirectory=(Join-Path $PSScriptRoot '..\out\first-mission-assets'),
    [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\first-cutscene'),
    [switch]$ValidateOnly
)
$ErrorActionPreference='Stop'
function Get-TaskSha256([string]$Path) {
    $taskStream=[IO.File]::OpenRead($Path)
    $taskAlgorithm=[Security.Cryptography.SHA256]::Create()
    try {return [BitConverter]::ToString($taskAlgorithm.ComputeHash($taskStream)).Replace('-','')}
    finally {$taskAlgorithm.Dispose();$taskStream.Dispose()}
}
$taskAssets=(Resolve-Path -LiteralPath $AssetsDirectory).Path
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
if(!$ValidateOnly -and (Test-Path -LiteralPath $taskOutput)){throw 'Cutscene output exists; choose a new directory'}
$taskManifest=Get-Content -LiteralPath (Join-Path $taskAssets 'manifest.json') -Raw | ConvertFrom-Json
if(!$taskManifest.Prepared){throw 'Original first mission models have not been prepared'}
$taskConverter=Join-Path $PSScriptRoot '..\build\Release\lcs-convert-rw.exe'
$taskIfp=Join-Path $PSScriptRoot '..\build\Release\lcs-ifp-inspect.exe'
$taskImg=Join-Path $PSScriptRoot '..\build\Release\lcs-img.exe'
$taskAnimation=& $taskIfp (Join-Path $taskAssets 'original-cuts\homswet.ifp') --bindings
if($LASTEXITCODE){throw 'Original intro animation is invalid'}
$taskModelAnimations=@{}
$taskSection=''
foreach($taskLine in Get-Content -LiteralPath (Join-Path $taskAssets 'original-cuts\homswet.cut')) {
    $taskLine=$taskLine.Trim()
    if($taskLine -eq 'model'){$taskSection='model';continue}
    if($taskLine -eq 'end'){$taskSection='';continue}
    if($taskSection -ne 'model'){continue}
    $taskFields=$taskLine.Split(',') | ForEach-Object { $_.Trim() }
    if($taskFields.Count -ne 3 -or $taskFields[0] -ne '1'){throw 'Intro requires an unverified cutscene model declaration'}
    if($taskFields[1] -notmatch '^[a-zA-Z0-9_]{1,23}$' -or $taskFields[2] -notmatch '^[a-zA-Z0-9_]{1,23}$'){throw 'Invalid scene resource label'}
    if($taskModelAnimations.ContainsKey($taskFields[2])){throw 'Duplicate scene animation declaration'}
    $taskModelAnimations[$taskFields[2]]=$taskFields[1]
}
if($taskModelAnimations.Count -ne 4){throw 'Original intro does not contain the expected four models'}
$taskModelBones=@{};$taskModelFrames=@{};$taskReports=@{}
foreach($taskModel in $taskModelAnimations.Values) {
    foreach($taskExtension in 'dff','txd') {
        $taskFile=$taskManifest.Files | Where-Object { $_.Name -eq ($taskModel+'.'+$taskExtension) }
        $taskPath=Join-Path $taskAssets ('native-models\'+$taskModel+'.'+$taskExtension)
        if(!$taskFile -or (Get-TaskSha256 $taskPath) -ne $taskFile.Sha256){throw "Converted model hash mismatch: $taskModel.$taskExtension"}
    }
    $taskReport=& $taskConverter (Join-Path $taskAssets ('native-models\'+$taskModel+'.dff')) --info
    if($LASTEXITCODE){throw "Cannot read scene model: $taskModel"}
    $taskReports[$taskModel]=$taskReport
    $taskModelBones[$taskModel]=@{};$taskModelFrames[$taskModel]=@{}
    foreach($taskLine in $taskReport) {
        if($taskLine -match '^bone=(\d+) flags='){$taskModelBones[$taskModel][$Matches[1]]=$true}
        if($taskLine -match '^frame=(.+)$'){$taskModelFrames[$taskModel][$Matches[1]]=$true}
    }
}
$taskBindings=0;$taskAnimations=@{}
foreach($taskLine in $taskAnimation) {
    if($taskLine -notmatch '^binding animation=(\S+) name=(.+) bone_tag=(-?\d+) keyframes=(\d+)$'){continue}
    $taskName=$Matches[1];$taskFrame=$Matches[2];$taskTag=[int]$Matches[3]
    if(!$taskModelAnimations.ContainsKey($taskName)){throw "Animation has no declared scene model: $taskName"}
    $taskModel=$taskModelAnimations[$taskName];$taskAnimations[$taskName]=$true
    if($taskTag -ge 0) {
        if(!$taskModelBones[$taskModel].ContainsKey([string]$taskTag)){throw "Animation bone is missing: $taskName / $taskTag"}
    } elseif(!$taskModelFrames[$taskModel].ContainsKey($taskFrame)) {throw "Animation frame name is missing: $taskName / $taskFrame"}
    $taskBindings++
}
if($taskAnimations.Count -ne $taskModelAnimations.Count -or $taskBindings -ne 161){throw 'Intro animation/model binding coverage is incomplete'}
# The original camera contains four arrays: FOV, roll, position and target.
# Match LCS's 4/4/10/10 floats per key, including equal-time camera cuts.
$taskCameraBytes=[IO.File]::ReadAllBytes((Join-Path $taskAssets 'original-cuts\homswet.dat'))
$taskCamera=[Text.Encoding]::ASCII.GetString($taskCameraBytes).TrimEnd([char]0)
$taskTracks=@($taskCamera.Split(';') | Where-Object { $_.Trim() })
if($taskTracks.Count -ne 4){throw 'Original intro camera does not contain four tracks'}
$taskCameraKeys=@()
for($taskTrack=0;$taskTrack -lt 4;$taskTrack++) {
    $taskTokens=@($taskTracks[$taskTrack].Split(',') | ForEach-Object { $_.Trim().TrimEnd('f','F') } | Where-Object { $_ })
    $taskValues=@($taskTokens | ForEach-Object { [double]::Parse($_,[Globalization.CultureInfo]::InvariantCulture) })
    $taskCount=[int]$taskValues[0];$taskStride=if($taskTrack -lt 2){4}else{10}
    if($taskCount -lt 2 -or $taskCount -ne $taskValues[0] -or $taskValues.Count -ne 1+$taskCount*$taskStride -or $taskValues.Count -gt 800){throw 'Invalid original intro camera count/length'}
    $taskPrevious=0.0
    foreach($taskValue in $taskValues){if([double]::IsNaN($taskValue) -or [double]::IsInfinity($taskValue)){throw 'Nonfinite original camera value'}}
    for($taskKey=0;$taskKey -lt $taskCount;$taskKey++) {
        $taskTime=$taskValues[1+$taskKey*$taskStride]
        if($taskTime -lt $taskPrevious){throw 'Original camera times decrease'}
        $taskPrevious=$taskTime
    }
    $taskCameraKeys+=$taskCount
}
if($ValidateOnly) {Write-Output 'PASS: original intro 161 animation/model bindings and four camera tracks; no game execution';return}
New-Item -ItemType Directory -Path (Join-Path $taskOutput 'cuts'),(Join-Path $taskOutput 'models') | Out-Null
foreach($taskExtension in 'ifp','cut','dat') {Copy-Item -LiteralPath (Join-Path $taskAssets ('original-cuts\homswet.'+$taskExtension)) -Destination (Join-Path $taskOutput 'cuts')}
foreach($taskModel in $taskModelAnimations.Values) {
    foreach($taskExtension in 'dff','txd'){Copy-Item -LiteralPath (Join-Path $taskAssets ('native-models\'+$taskModel+'.'+$taskExtension)) -Destination (Join-Path $taskOutput 'models')}
    [IO.File]::WriteAllLines((Join-Path $taskOutput ($taskModel+'-bindings.txt')),[string[]]$taskReports[$taskModel])
}
[IO.File]::WriteAllLines((Join-Path $taskOutput 'animation-validation.txt'),[string[]]$taskAnimation)
& $taskImg pack (Join-Path $taskOutput 'cuts') (Join-Path $taskOutput 'cuts.img')
if($LASTEXITCODE){throw 'Cannot pack original intro as native IMG v2'}
[pscustomobject]@{Prepared=$true;Installed=$false;Scene='homswet';Animations=4;SequenceModelBindings=$taskBindings;CameraKeys=$taskCameraKeys;NativeArchiveFormat='IMGv2';ArchiveSha256=(Get-TaskSha256 (Join-Path $taskOutput 'cuts.img'));NativeModelsMounted=$false;NativeAnimationPlayed=$false;AudioAdapted=$false;CutscenePlayable=$false;MissionPlayable=$false} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskOutput 'manifest.json') -Encoding UTF8
Write-Output "Original intro prepared: 161 animation/model bindings, four camera tracks, native IMG v2; not installed or played"
