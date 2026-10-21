param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [string]$WorldDirectory=(Join-Path $PSScriptRoot '..\out\world-stage'),
    [string]$InstalledManifest=(Join-Path $PSScriptRoot '..\reports\world-preview-install.json')
)
$ErrorActionPreference='Stop'
$taskGame=(Resolve-Path -LiteralPath $GameDirectory).Path
function Assert-TargetGameClosed {
    $taskExecutable=[IO.Path]::GetFullPath((Join-Path $taskGame 'gta_sa.exe'))
    foreach($taskProcess in @(Get-Process -Name gta_sa -ErrorAction SilentlyContinue)){
        $taskProcessPath=$taskProcess.Path
        if(!$taskProcessPath){throw 'Cannot identify a running GTA process; no installed file replaced'}
        if([string]::Equals($taskProcessPath,$taskExecutable,[StringComparison]::OrdinalIgnoreCase)){throw 'Close this GTA SA test copy before repair; this script never opens or closes it'}
    }
}
Assert-TargetGameClosed
$taskWorld=(Resolve-Path -LiteralPath $WorldDirectory).Path
$taskProject=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskInstall=Get-Content -LiteralPath $InstalledManifest -Raw|ConvertFrom-Json
if($taskInstall.GameDirectory -ne $taskGame){throw 'Installation manifest belongs to another game directory'}
$taskPrevious=@($taskInstall.Files|Where-Object {$_.Relative -eq 'data\script\main.scm'})
if($taskPrevious.Count -ne 1){throw 'No unique installed bootstrap in manifest'}
$taskProfile=(& (Join-Path $PSScriptRoot 'inspect-install.ps1') -GameDirectory $taskGame)|ConvertFrom-Json
if(!$taskProfile.GameProcessHookMatches -or !$taskProfile.NativeScriptDispatcherMatches){throw 'Native bridge entry points do not match the installed profile'}
$taskSource=Join-Path $taskWorld 'data\script\main.scm'
$taskDestination=Join-Path $taskGame 'data\script\main.scm'
$taskAudit=(& (Join-Path $PSScriptRoot 'validate-bootstrap.ps1') -ScmFile $taskSource)|ConvertFrom-Json
$taskOldHash=(Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash
$taskNewHash=(Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash
if($taskOldHash -eq $taskNewHash){Write-Output 'The repaired bootstrap is already installed';return}
if($taskOldHash -ne $taskPrevious[0].NewHash){throw 'Installed bootstrap changed after installation; preserve those edits before repair'}
$taskBackup=Join-Path $taskGame ('.lcs-sa-backups\bootstrap-fix-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
if(Test-Path -LiteralPath $taskBackup){throw 'Repair backup already exists'}
New-Item -ItemType Directory -Path (Join-Path $taskBackup 'data\script') -Force|Out-Null
$taskSaved=Join-Path $taskBackup 'data\script\main.scm'
Copy-Item -LiteralPath $taskDestination -Destination $taskSaved
if((Get-FileHash -LiteralPath $taskSaved -Algorithm SHA256).Hash -ne $taskOldHash){throw 'Repair backup verification failed; game untouched'}
$taskRow=[pscustomobject]@{Source=$taskSource;Relative='data\script\main.scm';Destination=$taskDestination;OldHash=$taskOldHash;NewHash=$taskNewHash;Existed=$true}
$taskRepair=[pscustomobject]@{GameDirectory=$taskGame;BackupDirectory=$taskBackup;PreviousManifest=[IO.Path]::GetFullPath($InstalledManifest);ExecutableSHA256=$taskProfile.SHA256;Mode='WorldPreview';Reason='Remove unsupported native 03F7 and validate every bootstrap operand';InstalledAt=(Get-Date).ToString('o');BootstrapBytes=$taskAudit.Bytes;BootstrapCommands=$taskAudit.Commands;Files=@($taskRow);GameExecuted=$false;CampaignPlayable=$false}
$taskRepair|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $taskBackup 'install-manifest.json') -Encoding utf8
Assert-TargetGameClosed
if((Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash -ne $taskOldHash -or (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash -ne $taskNewHash){throw 'A bootstrap changed during backup; no installed file replaced'}
try{
    Copy-Item -LiteralPath $taskSource -Destination $taskDestination -Force
    if((Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash -ne $taskNewHash){throw 'Repair installation verification failed'}
    & (Join-Path $PSScriptRoot 'validate-bootstrap.ps1') -ScmFile $taskDestination|Out-Null
}catch{
    Copy-Item -LiteralPath $taskSaved -Destination $taskDestination -Force
    throw
}
$taskRepair|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $taskProject 'reports\bootstrap-repair.json') -Encoding utf8
Write-Output "Native bootstrap repaired and verified. Backup: $taskBackup. Game not executed."
