param(
    [string]$ExtractedModels=(Join-Path $PSScriptRoot '..\out\img-v1'),
    [string]$ConvertedDirectory=(Join-Path $PSScriptRoot '..\out\lcs-converted'),
    [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\first-mission-models')
)
$ErrorActionPreference='Stop'
$taskModels=(Resolve-Path -LiteralPath $ExtractedModels).Path
$taskConverted=(Resolve-Path -LiteralPath $ConvertedDirectory).Path
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $taskOutput){throw 'Mission model output already exists; choose a new directory'}
$taskConverter=Join-Path $PSScriptRoot '..\build\Release\lcs-convert-rw.exe'
$taskImg=Join-Path $PSScriptRoot '..\build\Release\lcs-img.exe'
$taskNative=Join-Path $taskOutput 'native-models'
New-Item -ItemType Directory -Path $taskNative -Force | Out-Null
$taskFiles=@()
foreach($taskName in 'cssal','csvinc','cstoni_a','cssalchair','vinc_01','gang01','kuruma') {
    foreach($taskExt in 'dff','txd') {
        $taskSource=Join-Path $taskModels ($taskName+'.'+$taskExt)
        $taskDestination=Join-Path $taskNative ($taskName+'.'+$taskExt)
        $taskResult=& $taskConverter $taskSource $taskDestination
        if($LASTEXITCODE){throw "Mission asset conversion failed: $taskName.$taskExt"}
        $taskFiles+=[pscustomobject]@{Name=($taskName+'.'+$taskExt);SourceSha256=(Get-FileHash -LiteralPath $taskSource).Hash;Sha256=(Get-FileHash -LiteralPath $taskDestination).Hash;Conversion=$taskResult}
    }
    $taskInspection=& $taskConverter (Join-Path $taskNative ($taskName+'.dff')) --info
    if($LASTEXITCODE){throw "Mission skeleton inspection failed: $taskName"}
    [IO.File]::WriteAllLines((Join-Path $taskOutput ($taskName+'-hierarchy.txt')),[string[]]$taskInspection)
}
$taskValidation=& $taskConverter $taskNative --validate-actors
if($LASTEXITCODE){throw 'Mission model reread/skin validation failed'}
[IO.File]::WriteAllLines((Join-Path $taskOutput 'native-validation.txt'),[string[]]$taskValidation)
& $taskImg unpack (Join-Path $taskConverted 'anim\cuts.img') (Join-Path $taskConverted 'anim\cuts.dir') (Join-Path $taskOutput 'original-cuts')
if($LASTEXITCODE){throw 'Original cutscene archive validation/extraction failed'}
foreach($taskFile in 'homswet.cut','homswet.ifp','homswet.dat') {if(!(Test-Path -LiteralPath (Join-Path $taskOutput ('original-cuts\'+$taskFile)))){throw "Original cutscene resource is missing: $taskFile"}}
[pscustomobject]@{Prepared=$true;Installed=$false;NativeModelIdsAssigned=$false;GameTested=$false;PedRetargetVerified=$false;CutscenePlayable=$false;MissionPlayable=$false;Files=$taskFiles;Validation=$taskValidation} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskOutput 'manifest.json') -Encoding UTF8
Write-Output "Prepared original first mission models and cutscene files at $taskOutput; native registration/animation integration still pending"
