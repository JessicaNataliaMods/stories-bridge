param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [string]$OriginalDllDirectory,
    [string]$DataDirectory = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'lcs-ps2')
)
$ErrorActionPreference='Stop'
$taskProjectRoot=Split-Path -Parent $PSScriptRoot
$taskGameRoot=(Resolve-Path -LiteralPath $GameDirectory).Path
$taskDataRoot=(Resolve-Path -LiteralPath $DataDirectory).Path
$taskProfile=(& (Join-Path $PSScriptRoot 'inspect-install.ps1') -GameDirectory $taskGameRoot) | ConvertFrom-Json
if (!$taskProfile.GameProcessHookMatches) { throw 'Game instruction layout does not match the supported profile' }
if (!(Test-Path -LiteralPath (Join-Path $taskDataRoot 'DATA\MAIN.SCM'))) { throw 'Original LCS MAIN.SCM missing' }
if(Get-Process -Name gta_sa -ErrorAction SilentlyContinue) { throw 'Close GTA SA before installing this test build' }
$taskAsi=Join-Path $taskProjectRoot 'build\Release\LcsSaBridge.asi'
$taskLoader=Join-Path $taskProjectRoot 'vendor\ultimate-asi-loader-9.7.4\dinput8.dll'
if (!(Test-Path -LiteralPath $taskAsi)) { throw 'Build LcsSaBridge first' }
$taskAdded=[Collections.Generic.List[object]]::new()
function InstallNewFile([string]$Source,[string]$Destination) {
    if(Test-Path -LiteralPath $Destination) { throw "Refusing to overwrite existing file: $Destination" }
    Copy-Item -LiteralPath $Source -Destination $Destination
    $taskAdded.Add([pscustomobject]@{path=$Destination;sha256=(Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash})
}
if(!$taskProfile.HasAsiLoader) {
    if (!(Test-Path -LiteralPath $taskLoader)) { throw 'Download official Ultimate ASI Loader 9.7.4 x86 first' }
    InstallNewFile $taskLoader (Join-Path $taskGameRoot 'dinput8.dll')
}
if($OriginalDllDirectory) {
    $taskDllRoot=(Resolve-Path -LiteralPath $OriginalDllDirectory).Path
    foreach($taskDll in 'vorbisfile.dll','vorbis.dll','ogg.dll','eax.dll') {
        $taskDestination=Join-Path $taskGameRoot $taskDll
        if(!(Test-Path -LiteralPath $taskDestination)) {
            InstallNewFile (Join-Path $taskDllRoot $taskDll) $taskDestination
        }
    }
}
$taskScripts=Join-Path $taskGameRoot 'scripts'
New-Item -ItemType Directory -Path $taskScripts -Force | Out-Null
InstallNewFile $taskAsi (Join-Path $taskScripts 'LcsSaBridge.asi')
$taskIni=Join-Path $taskScripts 'LcsSaBridge.ini'
if(Test-Path -LiteralPath $taskIni) { throw 'Existing bridge configuration found; preserve it before reinstalling' }
[IO.File]::WriteAllText($taskIni,"[LCS]`r`nDataDirectory=$taskDataRoot`r`n",[Text.Encoding]::Unicode)
$taskAdded.Add([pscustomobject]@{path=$taskIni;sha256=(Get-FileHash -LiteralPath $taskIni -Algorithm SHA256).Hash})
$taskReports=Join-Path $taskProjectRoot 'reports'
New-Item -ItemType Directory -Path $taskReports -Force | Out-Null
[pscustomobject]@{game=$taskProfile;addedFiles=$taskAdded;installedAt=(Get-Date).ToString('o')} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskReports 'test-install.json') -Encoding utf8
Write-Output "Installed diagnostic ASI in $taskScripts"
