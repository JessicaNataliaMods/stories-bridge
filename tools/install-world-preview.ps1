param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [string]$WorldDirectory=(Join-Path $PSScriptRoot '..\out\world-stage'),
    [string]$DataDirectory=(Join-Path $PSScriptRoot '..\out\lcs-converted')
)
$ErrorActionPreference='Stop'
$taskProject=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskGame=(Resolve-Path -LiteralPath $GameDirectory).Path
$taskWorld=(Resolve-Path -LiteralPath $WorldDirectory).Path
$taskData=(Resolve-Path -LiteralPath $DataDirectory).Path
if(Get-Process -Name gta_sa -ErrorAction SilentlyContinue){throw 'Close GTA SA before installing; this script never opens or closes the game'}
if(Get-Process -Name euryopa -ErrorAction SilentlyContinue){throw 'Close the map viewer before replacing its data files'}
$taskProfile=(& (Join-Path $PSScriptRoot 'inspect-install.ps1') -GameDirectory $taskGame)|ConvertFrom-Json
$taskKnownProfiles=@('590B4BE1BB005F4E07EDF6B32DF4A95B7BA403FB85472D68AA4275875B345B6F','F01A00CE950FA40CA1ED59DF0E789848C6EDCF6405456274965885D0929343AC')
if(!$taskProfile.GameProcessHookMatches -or !$taskProfile.NativeScriptLoopCallMatches -or !$taskProfile.NativeScriptStartupCallMatches -or !$taskProfile.NativeScriptDispatcherMatches -or $taskProfile.SHA256 -notin $taskKnownProfiles){throw 'Unverified SA executable profile'}
& (Join-Path $PSScriptRoot 'validate-world.ps1') -WorldDirectory $taskWorld
if(!$?){throw 'Offline map validation failed'}
& (Join-Path $PSScriptRoot 'validate-sa-profile.ps1') -GameDirectory $taskGame -WorldDirectory $taskWorld
if(!$?){throw 'Offline native engine resource validation failed'}
$taskReplacements=[Collections.Generic.List[object]]::new()
function Add-Replacement([string]$Source,[string]$Relative){
    if(!(Test-Path -LiteralPath $Source -PathType Leaf)){throw "Required file is missing: $Source"}
    $taskDestination=[IO.Path]::GetFullPath((Join-Path $taskGame $Relative))
    if(!$taskDestination.StartsWith($taskGame+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Install target outside game directory'}
    $taskReplacements.Add([pscustomobject]@{Source=$Source;Relative=$Relative;Destination=$taskDestination;OldHash=$null;NewHash=(Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash;Existed=(Test-Path -LiteralPath $taskDestination)})
}
foreach($taskName in @('gta.dat','water.dat','script\main.scm')){Add-Replacement (Join-Path $taskWorld ('data\'+$taskName)) ('data\'+$taskName)}
foreach($taskName in @('gta3.img','gta_int.img')){Add-Replacement (Join-Path $taskWorld ('models\sa-core-clean\'+$taskName)) ('models\'+$taskName)}
foreach($taskName in @('world.ide','model-bindings.json','model-bindings.tsv','world-manifest.json')){Add-Replacement (Join-Path $taskWorld ('data\lcs\'+$taskName)) ('data\lcs\'+$taskName)}
$taskManifest=Get-Content -LiteralPath (Join-Path $taskWorld 'data\lcs\world-manifest.json') -Raw|ConvertFrom-Json
foreach($taskPlacement in $taskManifest.PlacementFiles){if($taskPlacement.File -notmatch '^world[0-9]+\.ipl$'){throw 'Invalid placement file'};Add-Replacement (Join-Path $taskWorld ('data\lcs\'+$taskPlacement.File)) ('data\lcs\'+$taskPlacement.File)}
foreach($taskName in @('world.img','world.col','paths.img')){Add-Replacement (Join-Path $taskWorld ('models\lcs\'+$taskName)) ('models\lcs\'+$taskName)}
Add-Replacement (Join-Path $taskProject 'build\Release\LcsSaBridge.asi') 'scripts\LcsSaBridge.asi'
$taskIni=Join-Path $taskWorld 'LcsSaBridge.ini'
[IO.File]::WriteAllText($taskIni,"[LCS]`r`nMode=WorldPreview`r`nDataDirectory=$taskData`r`nWorldDirectory=$(Join-Path $taskGame 'data\lcs')`r`n",[Text.Encoding]::Unicode)
Add-Replacement $taskIni 'scripts\LcsSaBridge.ini'
if(!$taskProfile.HasAsiLoader){throw 'Install an ASI loader before this profile'}
$taskRequired=0
foreach($taskRow in $taskReplacements){$taskRequired+=(Get-Item -LiteralPath $taskRow.Source).Length;if($taskRow.Existed){$taskRequired+=(Get-Item -LiteralPath $taskRow.Destination).Length}}
$taskDrive=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($taskGame));if($taskDrive.AvailableFreeSpace -lt $taskRequired+104857600){throw 'Insufficient free space for installation and original-file backups'}
$taskBackup=Join-Path $taskGame ('.lcs-sa-backups\world-preview-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
if(Test-Path -LiteralPath $taskBackup){throw 'Backup destination already exists'}
New-Item -ItemType Directory -Path $taskBackup -Force | Out-Null
# Finish every backup before replacing the first file. A failed backup leaves
# the game untouched. Only this explicit file list is changed.
foreach($taskRow in $taskReplacements){
    if(!$taskRow.Existed){continue}
    $taskRow.OldHash=(Get-FileHash -LiteralPath $taskRow.Destination -Algorithm SHA256).Hash
    $taskCopy=Join-Path $taskBackup $taskRow.Relative
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($taskCopy)) -Force | Out-Null
    Copy-Item -LiteralPath $taskRow.Destination -Destination $taskCopy
    if((Get-FileHash -LiteralPath $taskCopy -Algorithm SHA256).Hash -ne $taskRow.OldHash){throw 'Backup verification failed; game untouched'}
}
$taskChanged=[Collections.Generic.List[object]]::new()
if(Get-Process -Name gta_sa,euryopa -ErrorAction SilentlyContinue){throw 'A game or viewer was opened during backup; files have not been replaced'}
try {
    foreach($taskRow in $taskReplacements){
        $taskChanged.Add($taskRow)
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($taskRow.Destination)) -Force | Out-Null
        Copy-Item -LiteralPath $taskRow.Source -Destination $taskRow.Destination -Force
        if((Get-FileHash -LiteralPath $taskRow.Destination -Algorithm SHA256).Hash -ne $taskRow.NewHash){throw "Installed file verification failed: $($taskRow.Relative)"}
    }
}catch{
    foreach($taskRow in $taskChanged){
        if($taskRow.Existed){Copy-Item -LiteralPath (Join-Path $taskBackup $taskRow.Relative) -Destination $taskRow.Destination -Force}
        elseif(Test-Path -LiteralPath $taskRow.Destination){Remove-Item -LiteralPath $taskRow.Destination -Force}
    }
    throw
}
$taskInstall=[pscustomobject]@{GameDirectory=$taskGame;BackupDirectory=$taskBackup;Mode='WorldPreview';InstalledAt=(Get-Date).ToString('o');Files=$taskReplacements;GameExecuted=$false;CampaignPlayable=$false}
$taskInstall|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $taskBackup 'install-manifest.json') -Encoding utf8
$taskInstall|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $taskProject 'reports\world-preview-install.json') -Encoding utf8
Write-Output "World preview installed. Verified original-file backup: $taskBackup. Game not executed. Campaign remains in development."
