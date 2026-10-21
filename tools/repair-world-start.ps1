param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [string]$WorldDirectory=(Join-Path $PSScriptRoot '..\out\world-stage'),
    [string[]]$InstalledManifests
)
$ErrorActionPreference='Stop'
$taskGame=(Resolve-Path -LiteralPath $GameDirectory).Path
$taskWorld=(Resolve-Path -LiteralPath $WorldDirectory).Path
$taskProject=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
function Assert-TargetGameClosed {
    $taskExecutable=Join-Path $taskGame 'gta_sa.exe'
    foreach($taskProcess in @(Get-Process -Name gta_sa -ErrorAction SilentlyContinue)){
        if(!$taskProcess.Path){throw 'Cannot identify a running GTA process; game files untouched'}
        if([string]::Equals($taskProcess.Path,$taskExecutable,[StringComparison]::OrdinalIgnoreCase)){throw 'Close the target GTA test copy before installing; this tool never opens or closes it'}
    }
}
Assert-TargetGameClosed
$taskProfile=(& (Join-Path $PSScriptRoot 'inspect-install.ps1') -GameDirectory $taskGame)|ConvertFrom-Json
if(!$taskProfile.NativeScriptDispatcherMatches -or !$taskProfile.NativeScriptLoopCallMatches -or !$taskProfile.NativeScriptStartupCallMatches){throw 'Native script startup/loop profile mismatch'}
$taskBootstrap=(& (Join-Path $PSScriptRoot 'validate-bootstrap.ps1') -ScmFile (Join-Path $taskWorld 'data\script\main.scm'))|ConvertFrom-Json
& (Join-Path $PSScriptRoot 'validate-sa-profile.ps1') -GameDirectory $taskGame -WorldDirectory $taskWorld|Out-Null
if(!$InstalledManifests){
    $InstalledManifests=@((Join-Path $taskProject 'reports\world-preview-install.json'),(Join-Path $taskProject 'reports\bootstrap-repair.json'))
    $taskLast=Join-Path $taskProject 'reports\startup-repair.json'
    if(Test-Path -LiteralPath $taskLast){$InstalledManifests+=$taskLast}
}
$taskKnown=@{}
foreach($taskFile in $InstalledManifests){
    $taskManifest=Get-Content -LiteralPath $taskFile -Raw|ConvertFrom-Json
    if($taskManifest.GameDirectory -ne $taskGame){throw 'Previous installation manifest belongs to another game directory'}
    foreach($taskRow in $taskManifest.Files){$taskKnown[$taskRow.Relative]=$taskRow.NewHash}
}
$taskFiles=[Collections.Generic.List[object]]::new()
foreach($taskRelative in @('data\gta.dat','data\script\main.scm','models\lcs\paths.img','scripts\LcsSaBridge.asi')){
    $taskSource=Join-Path $taskWorld $taskRelative
    if($taskRelative -eq 'scripts\LcsSaBridge.asi'){$taskSource=Join-Path $taskProject 'build\Release\LcsSaBridge.asi'}
    $taskDestination=[IO.Path]::GetFullPath((Join-Path $taskGame $taskRelative))
    if(!$taskDestination.StartsWith($taskGame+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Repair target outside authorized game directory'}
    $taskNewHash=(Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash
    $taskExisted=Test-Path -LiteralPath $taskDestination -PathType Leaf
    $taskOldHash=$null
    if($taskExisted){
        $taskOldHash=(Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash
        if($taskOldHash -eq $taskNewHash){continue}
        if(!$taskKnown.ContainsKey($taskRelative) -or $taskKnown[$taskRelative] -ne $taskOldHash){throw "User-modified or unmanaged file; preserve it before repair: $taskRelative"}
    }
    $taskFiles.Add([pscustomobject]@{Source=$taskSource;Relative=$taskRelative;Destination=$taskDestination;OldHash=$taskOldHash;NewHash=$taskNewHash;Existed=$taskExisted})
}
if(!$taskFiles.Count){Write-Output 'Startup repair already installed and verified';return}
$taskBackup=Join-Path $taskGame ('.lcs-sa-backups\startup-fix-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
if(Test-Path -LiteralPath $taskBackup){throw 'Startup repair backup already exists'}
New-Item -ItemType Directory -Path $taskBackup -Force|Out-Null
foreach($taskRow in $taskFiles){
    if(!$taskRow.Existed){continue}
    $taskCopy=Join-Path $taskBackup $taskRow.Relative
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($taskCopy)) -Force|Out-Null
    Copy-Item -LiteralPath $taskRow.Destination -Destination $taskCopy
    if((Get-FileHash -LiteralPath $taskCopy -Algorithm SHA256).Hash -ne $taskRow.OldHash){throw 'Backup verification failed; installed files untouched'}
}
$taskRepair=[pscustomobject]@{GameDirectory=$taskGame;BackupDirectory=$taskBackup;PreviousManifests=$InstalledManifests;ExecutableSHA256=$taskProfile.SHA256;Mode='WorldPreview';Reason='Create the native player before the initial camera update and record startup script processing';InstalledAt=(Get-Date).ToString('o');BootstrapBytes=$taskBootstrap.Bytes;BootstrapCommands=$taskBootstrap.Commands;Files=$taskFiles;GameExecuted=$false;CampaignPlayable=$false}
$taskRepair|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $taskBackup 'install-manifest.json') -Encoding utf8
Assert-TargetGameClosed
foreach($taskRow in $taskFiles){
    if((Get-FileHash -LiteralPath $taskRow.Source -Algorithm SHA256).Hash -ne $taskRow.NewHash){throw 'Staged file changed during backup'}
    if($taskRow.Existed -and (Get-FileHash -LiteralPath $taskRow.Destination -Algorithm SHA256).Hash -ne $taskRow.OldHash){throw 'Installed file changed during backup'}
    if(!$taskRow.Existed -and (Test-Path -LiteralPath $taskRow.Destination)){throw 'Target file appeared during backup'}
}
$taskChanged=[Collections.Generic.List[object]]::new()
try{
    foreach($taskRow in $taskFiles){
        $taskChanged.Add($taskRow)
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($taskRow.Destination)) -Force|Out-Null
        Copy-Item -LiteralPath $taskRow.Source -Destination $taskRow.Destination -Force
        if((Get-FileHash -LiteralPath $taskRow.Destination -Algorithm SHA256).Hash -ne $taskRow.NewHash){throw 'Startup repair installed hash mismatch'}
    }
    & (Join-Path $PSScriptRoot 'validate-bootstrap.ps1') -ScmFile (Join-Path $taskGame 'data\script\main.scm')|Out-Null
    & (Join-Path $PSScriptRoot 'validate-navigation.ps1') -ArchiveFile (Join-Path $taskGame 'models\lcs\paths.img')|Out-Null
}catch{
    foreach($taskRow in $taskChanged){
        if($taskRow.Existed){Copy-Item -LiteralPath (Join-Path $taskBackup $taskRow.Relative) -Destination $taskRow.Destination -Force}
        else{Remove-Item -LiteralPath $taskRow.Destination -Force}
    }
    throw
}
$taskRepair|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $taskProject 'reports\startup-repair.json') -Encoding utf8
Write-Output "World startup repair installed and verified. Backup: $taskBackup. Game not executed."
