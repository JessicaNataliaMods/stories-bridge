param([Parameter(Mandatory)][string]$GameDirectory)
$ErrorActionPreference='Stop'
$taskProject=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskGame=(Resolve-Path -LiteralPath $GameDirectory).Path.TrimEnd('\')
$taskSource=Join-Path $taskProject 'build\Release\LcsSaBridge.asi'
$taskDestination=Join-Path $taskGame 'scripts\LcsSaBridge.asi'
$taskIni=Join-Path $taskGame 'scripts\LcsSaBridge.ini'
function Assert-Closed {
    foreach($taskProcess in @(Get-CimInstance Win32_Process -Filter "Name='gta_sa.exe'")){
        if(!$taskProcess.ExecutablePath -or [IO.Path]::GetFullPath($taskProcess.ExecutablePath).StartsWith($taskGame+'\',[StringComparison]::OrdinalIgnoreCase)){
            throw 'Target GTA is running; bridge update deferred. This tool never opens or closes the game.'
        }
    }
}
Assert-Closed
if((Get-FileHash -LiteralPath (Join-Path $taskGame 'gta_sa.exe')).Hash -ne 'F01A00CE950FA40CA1ED59DF0E789848C6EDCF6405456274965885D0929343AC'){throw 'Native executable profile changed'}
& (Join-Path $taskProject 'tools\validate-bootstrap.ps1') -ScmFile (Join-Path $taskGame 'data\script\main.scm') | Out-Null
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $taskProject 'tests\campaign_native_profile.ps1') -Executable (Join-Path $taskGame 'gta_sa.exe') -ProjectDirectory $taskProject
if($LASTEXITCODE){throw 'Native shared-service profile failed'}
if(!(Test-Path -LiteralPath $taskDestination -PathType Leaf)){throw 'Expected existing bridge installation'}
$taskOld=(Get-FileHash -LiteralPath $taskDestination).Hash
$taskNew=(Get-FileHash -LiteralPath $taskSource).Hash
$taskIniHash=(Get-FileHash -LiteralPath $taskIni).Hash
$taskBackup=[IO.Path]::GetFullPath((Join-Path $taskGame ('.lcs-sa-backups\shared-bridge-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))))
if(!$taskBackup.StartsWith($taskGame+'\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $taskBackup)){throw 'Invalid or occupied bridge backup destination'}
New-Item -ItemType Directory -Path $taskBackup | Out-Null
$taskCopy=Join-Path $taskBackup 'LcsSaBridge.asi'
Copy-Item -LiteralPath $taskDestination -Destination $taskCopy
if((Get-FileHash -LiteralPath $taskCopy).Hash -ne $taskOld){throw 'Bridge backup failed verification'}
Assert-Closed
if((Get-FileHash -LiteralPath $taskDestination).Hash -ne $taskOld -or (Get-FileHash -LiteralPath $taskIni).Hash -ne $taskIniHash){throw 'Installation changed during backup'}
try {
    Copy-Item -LiteralPath $taskSource -Destination $taskDestination -Force
    if((Get-FileHash -LiteralPath $taskDestination).Hash -ne $taskNew){throw 'Installed bridge hash mismatch'}
    if((Get-FileHash -LiteralPath $taskIni).Hash -ne $taskIniHash){throw 'Existing campaign test profile changed'}
}catch {
    Copy-Item -LiteralPath $taskCopy -Destination $taskDestination -Force
    throw
}
$taskMode=[regex]::Match([IO.File]::ReadAllText($taskIni),'(?m)^Mode=([^\r\n]+)').Groups[1].Value
$taskReport=[pscustomobject]@{
    InstalledAt=(Get-Date).ToString('o');GameDirectory=$taskGame;BackupDirectory=$taskBackup
    PreviousBridgeSha256=$taskOld;BridgeSha256=$taskNew;IniSha256=$taskIniHash;Mode=$taskMode
    FilesChanged=@('scripts\LcsSaBridge.asi');GameExecuted=$false;CampaignPlayable=$false
}
$taskReport | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskBackup 'install-manifest.json') -Encoding UTF8
$taskReport | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskProject 'reports\campaign-shared-install.json') -Encoding UTF8
Write-Output "Shared bridge installed and hash verified; profile=$taskMode; backup=$taskBackup; gameExecuted=false; campaignPlayable=false"
