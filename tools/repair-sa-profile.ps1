param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [string]$WorldDirectory=(Join-Path $PSScriptRoot '..\out\world-stage'),
    [string]$InstalledManifest=(Join-Path $PSScriptRoot '..\reports\world-preview-install.json')
)
$ErrorActionPreference='Stop'
if(Get-Process -Name gta_sa -ErrorAction SilentlyContinue){throw 'Close GTA SA before repair; this script never opens or closes it'}
$taskGame=(Resolve-Path -LiteralPath $GameDirectory).Path
$taskWorld=(Resolve-Path -LiteralPath $WorldDirectory).Path
$taskProject=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskInstall=Get-Content -LiteralPath $InstalledManifest -Raw|ConvertFrom-Json
if($taskInstall.GameDirectory -ne $taskGame){throw 'Installation manifest belongs to another game directory'}
$taskProfile=(& (Join-Path $PSScriptRoot 'inspect-install.ps1') -GameDirectory $taskGame)|ConvertFrom-Json
# This repair changes only data/gta.dat. Validate the native entry points used by
# the already installed bridge; do not patch or replace a changed executable.
if(!$taskProfile.GameProcessHookMatches -or !$taskProfile.NativeScriptDispatcherMatches){throw 'Native bridge entry points do not match the installed profile'}
$taskPrevious=@($taskInstall.Files|Where-Object {$_.Relative -eq 'data\gta.dat'})
if($taskPrevious.Count -ne 1){throw 'No unique installed level profile in manifest'}
$taskDestination=Join-Path $taskGame 'data\gta.dat'
$taskSource=Join-Path $taskWorld 'data\gta.dat'
$taskOldHash=(Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash
$taskNewHash=(Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash
if($taskOldHash -eq $taskNewHash){Write-Output 'The repaired native engine profile is already installed';return}
if($taskOldHash -ne $taskPrevious[0].NewHash){throw 'Installed profile was edited after installation; preserve those edits before repair'}
& (Join-Path $PSScriptRoot 'validate-sa-profile.ps1') -GameDirectory $taskGame -WorldDirectory $taskWorld
if(!$?){throw 'Native engine resource validation failed'}
$taskBackup=Join-Path $taskGame ('.lcs-sa-backups\world-profile-fix-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
if(Test-Path -LiteralPath $taskBackup){throw 'Repair backup already exists'}
New-Item -ItemType Directory -Path (Join-Path $taskBackup 'data') -Force|Out-Null
$taskSaved=Join-Path $taskBackup 'data\gta.dat'
Copy-Item -LiteralPath $taskDestination -Destination $taskSaved
if((Get-FileHash -LiteralPath $taskSaved -Algorithm SHA256).Hash -ne $taskOldHash){throw 'Repair backup verification failed; game untouched'}
$taskRow=[pscustomobject]@{Source=$taskSource;Relative='data\gta.dat';Destination=$taskDestination;OldHash=$taskOldHash;NewHash=$taskNewHash;Existed=$true}
$taskRepair=[pscustomobject]@{GameDirectory=$taskGame;BackupDirectory=$taskBackup;PreviousManifest=[IO.Path]::GetFullPath($InstalledManifest);ExecutableSHA256=$taskProfile.SHA256;Mode='WorldPreview';Reason='Restore native generic definitions used by C3dMarkers and pickups';InstalledAt=(Get-Date).ToString('o');Files=@($taskRow);GameExecuted=$false;CampaignPlayable=$false}
# Save rollback information before changing the installed profile.
$taskRepair|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $taskBackup 'install-manifest.json') -Encoding utf8
try{
    Copy-Item -LiteralPath $taskSource -Destination $taskDestination -Force
    if((Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash -ne $taskNewHash){throw 'Repair installation verification failed'}
}catch{
    Copy-Item -LiteralPath $taskSaved -Destination $taskDestination -Force
    throw
}
$taskRepair|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $taskProject 'reports\world-profile-repair.json') -Encoding utf8
Write-Output "Native engine profile repaired. Backup: $taskBackup. Game not executed."
