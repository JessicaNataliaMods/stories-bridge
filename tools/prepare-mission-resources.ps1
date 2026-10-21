param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [string]$ConvertedDirectory=(Join-Path $PSScriptRoot '..\out\lcs-converted'),
    [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\mission-resources')
)
$ErrorActionPreference='Stop'
$taskSource=(Resolve-Path -LiteralPath $ConvertedDirectory).Path
$taskGame=(Resolve-Path -LiteralPath $GameDirectory).Path
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $taskOutput){throw 'Mission resource output already exists; choose a new staging directory'}
$taskGxt=Join-Path $PSScriptRoot '..\build\Release\lcs-gxt.exe'
$taskAudio=Join-Path $PSScriptRoot '..\build\Release\lcs-audio-inspect.exe'
$taskText=Join-Path $taskOutput 'text'
New-Item -ItemType Directory -Path $taskText -Force | Out-Null
$taskFiles=@()
foreach($taskLanguage in 'american','french','german','italian','spanish') {
    $taskOriginal=Join-Path $taskSource ('TEXT\'+$taskLanguage+'.gxt')
    $taskNative=Join-Path $taskGame ('text\'+$taskLanguage+'.gxt')
    $taskDestination=Join-Path $taskText ($taskLanguage+'.gxt')
    $taskValidation=& $taskGxt $taskOriginal $taskNative $taskDestination
    if($LASTEXITCODE){throw "Mission text conversion failed: $taskLanguage"}
    $taskFiles+=[pscustomobject]@{RelativePath=('text\'+$taskLanguage+'.gxt');SourceSha256=(Get-FileHash -LiteralPath $taskNative).Hash;Sha256=(Get-FileHash -LiteralPath $taskDestination).Hash;Validation=$taskValidation}
}
$taskAudioValidation=& $taskAudio (Join-Path $taskSource 'audio')
if($LASTEXITCODE){throw 'Original mission audio validation failed'}
[IO.File]::WriteAllLines((Join-Path $taskOutput 'audio-validation.txt'),[string[]]$taskAudioValidation)
$taskManifest=[pscustomobject]@{Prepared=$true;Installed=$false;GameTested=$false;CampaignPlayable=$false;Source=$taskSource;NativeGame=$taskGame;Files=$taskFiles;AudioValidation='audio-validation.txt'}
$taskManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskOutput 'manifest.json') -Encoding UTF8
Write-Output "Prepared original mission texts and speech validation at $taskOutput; game files were not changed"
