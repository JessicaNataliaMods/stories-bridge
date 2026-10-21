param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][string]$ReportFile
)
$ErrorActionPreference='Stop'
$taskGame=(Resolve-Path -LiteralPath $GameDirectory).Path
$taskPackage=(Resolve-Path -LiteralPath $PackageDirectory).Path
$taskTarget=[IO.Path]::GetFullPath((Join-Path $taskGame 'modloader\stories_bridge'))
if(!$taskTarget.StartsWith($taskGame+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Install path leaves the named game directory'}
function Assert-TargetGameClosed {
    foreach($taskProcess in Get-Process -Name gta_sa,euryopa -ErrorAction SilentlyContinue){
        try{$taskExecutable=$taskProcess.Path}catch{throw 'Cannot identify an open game/viewer executable; no install started'}
        if(!$taskExecutable){throw 'Cannot identify an open game/viewer executable; no install started'}
        $taskExecutable=[IO.Path]::GetFullPath($taskExecutable)
        if($taskExecutable.StartsWith($taskGame+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){
            throw "Close the game/viewer from the target installation before updating: $taskExecutable"
        }
    }
}
Assert-TargetGameClosed
$taskMetadata=Get-Content -LiteralPath (Join-Path $taskPackage 'metadata\map-preview.json') -Raw|ConvertFrom-Json
$taskValidation=Get-Content -LiteralPath (Join-Path $taskPackage 'metadata\package-validation.json') -Raw|ConvertFrom-Json
if(!$taskValidation.valid -or !$taskValidation.nativeObjectCollisions.valid -or $taskMetadata.mode -notin @('WorldPreview','Campaign')){throw 'Package validation/mode/native collision mismatch'}
if($taskMetadata.mode -eq 'Campaign'){
    if(!$taskValidation.campaign.valid -or !$taskValidation.campaign.enabled -or !$taskValidation.campaign.nativeStartupValidated -or $taskValidation.campaign.gameExecuted -or $taskValidation.campaign.completeCampaignVerified){throw 'Campaign source/resource/startup validation missing or invalid; installation blocked'}
    & (Join-Path $PSScriptRoot '..\tests\campaign_native_profile.ps1') -Executable (Join-Path $taskGame 'gta_sa.exe') -ProjectDirectory (Join-Path $PSScriptRoot '..') | Out-Null
}
if([IO.Path]::GetFullPath($taskMetadata.gameDirectory) -ne $taskGame){throw 'Package was prepared for a different installation'}
if((Get-FileHash -LiteralPath (Join-Path $taskGame 'gta_sa.exe')).Hash -ne $taskMetadata.nativeExecutableSHA256){throw 'Game executable changed after preparation'}
foreach($taskName in @('gta3.img','gta_int.img')){
    if((Get-FileHash -LiteralPath (Join-Path $taskGame ('models\'+$taskName))).Hash -ne $taskMetadata.originalArchiveHashes.$taskName){throw 'Original game archive changed after preparation'}
}
# A single data override replaces the base file. Multiple mods or readme data
# activate std.data merging, which would reintroduce SA's placements/water.
$taskModloader=Join-Path $taskGame 'modloader'
$taskSkinNames=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
if(Test-Path -LiteralPath (Join-Path $taskPackage 'metadata\gameplay-peds.json')){
    if(!$taskValidation.gameplayPeds.valid){throw 'Gameplay skin package is not validated'}
    foreach($taskSkin in Get-ChildItem -LiteralPath (Join-Path $taskPackage 'models\lcs\peds') -File){$null=$taskSkinNames.Add($taskSkin.Name)}
}
foreach($taskFile in Get-ChildItem -LiteralPath $taskModloader -Recurse -File){
    if($taskFile.FullName.StartsWith($taskTarget+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){continue}
    if($taskFile.Name -in @('gta.dat','water.dat','main.scm','gta3.img','gta_int.img') -or ($taskMetadata.mode -eq 'Campaign' -and $taskFile.Name -in @('cuts.img','cutscene.img','american.gxt','french.gxt','german.gxt','italian.gxt','spanish.gxt','LcsSaBridge.asi'))){throw "Conflicting Modloader override: $($taskFile.FullName)"}
    if($taskSkinNames.Contains($taskFile.Name) -or ($taskSkinNames.Count -gt 0 -and $taskFile.Name -in @('peds.ide','pedgrp.dat'))){throw "Conflicting gameplay skin override: $($taskFile.FullName)"}
    if($taskFile.Extension -in @('.txt','.md') -and $taskFile.FullName -notlike '*\.data\*'){
        if(Select-String -LiteralPath $taskFile.FullName -Pattern '^(?:IDE|IPL|IMG|CDIMAGE|COLFILE \d|TEXDICTION|MODELFILE|HIERFILE) \w+[\\/].*\.(?:IDE|IPL|ZON|IMG|COL|TXD|DFF)\s*$' -Quiet){throw "Conflicting level entries in readme: $($taskFile.FullName)"}
    }
}
$taskProfile=(& (Join-Path $PSScriptRoot 'inspect-install.ps1') -GameDirectory $taskGame)|ConvertFrom-Json
if(!$taskProfile.HasAsiLoader -or !$taskProfile.GameProcessHookMatches -or !$taskProfile.NativeScriptStartupCallMatches -or !$taskProfile.NativeScriptLoopCallMatches -or !$taskProfile.NativeScriptDispatcherMatches){throw 'Required native executable/ASI profile does not match'}
$taskNative=(& (Join-Path $PSScriptRoot 'validate-sa-profile.ps1') -GameDirectory $taskGame -WorldDirectory $taskPackage)|ConvertFrom-Json
$taskBootstrap=(& (Join-Path $PSScriptRoot 'validate-bootstrap.ps1') -ScmFile (Join-Path $taskPackage 'data\script\main.scm'))|ConvertFrom-Json
if(!$taskNative.NativeProfileValidated -or !$taskBootstrap.NativeBootstrapValidated){throw 'Native resource/bootstrap validation failed'}
$taskManifest=Get-Content -LiteralPath (Join-Path $taskPackage 'metadata\package-files.json') -Raw|ConvertFrom-Json
$taskRows=[Collections.Generic.List[object]]::new()
foreach($taskFile in @($taskManifest)+@([pscustomobject]@{relative='metadata/package-files.json';bytes=(Get-Item -LiteralPath (Join-Path $taskPackage 'metadata\package-files.json')).Length;sha256=(Get-FileHash -LiteralPath (Join-Path $taskPackage 'metadata\package-files.json')).Hash})){
    $taskRelative=$taskFile.relative.Replace('/',[IO.Path]::DirectorySeparatorChar)
    $taskSource=[IO.Path]::GetFullPath((Join-Path $taskPackage $taskRelative))
    $taskDestination=[IO.Path]::GetFullPath((Join-Path $taskTarget $taskRelative))
    if(!$taskSource.StartsWith($taskPackage+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or !$taskDestination.StartsWith($taskTarget+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe package path'}
    if((Get-Item -LiteralPath $taskSource).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Package contains a reparse point'}
    if((Get-Item -LiteralPath $taskSource).Length -ne $taskFile.bytes -or (Get-FileHash -LiteralPath $taskSource).Hash -ne $taskFile.sha256){throw "Staging integrity check failed: $taskRelative"}
    $taskExisted=Test-Path -LiteralPath $taskDestination -PathType Leaf
    $taskOldHash=if($taskExisted){(Get-FileHash -LiteralPath $taskDestination).Hash}else{$null}
    $taskRows.Add([pscustomobject]@{Relative=$taskRelative;Source=$taskSource;Destination=$taskDestination;Existed=$taskExisted;OldHash=$taskOldHash;NewHash=$taskFile.sha256;Changed=(!$taskExisted -or $taskOldHash -ne $taskFile.sha256)})
}
# Refuse to leave stale assets from an earlier package active.
$taskObsoleteRows=[Collections.Generic.List[object]]::new()
if(Test-Path -LiteralPath $taskTarget){
    $taskPreviousFiles=@{}
    $taskPreviousManifest=Join-Path $taskTarget 'metadata\package-files.json'
    if(Test-Path -LiteralPath $taskPreviousManifest){
        foreach($taskPrevious in (Get-Content -LiteralPath $taskPreviousManifest -Raw|ConvertFrom-Json)){
            $taskPreviousPath=[IO.Path]::GetFullPath((Join-Path $taskTarget $taskPrevious.relative.Replace('/',[IO.Path]::DirectorySeparatorChar)))
            if(!$taskPreviousPath.StartsWith($taskTarget+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe previous manifest path'}
            $taskPreviousFiles[$taskPreviousPath]=$taskPrevious
        }
    }
    $taskExpected=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($taskRow in $taskRows){$null=$taskExpected.Add($taskRow.Destination)}
    foreach($taskFile in Get-ChildItem -LiteralPath $taskTarget -Recurse -File){
        # Keep the bridge's own runtime diagnostic log across an update.
        if($taskFile.FullName -eq (Join-Path $taskTarget 'LcsSaBridge.log')){continue}
        if(!$taskExpected.Contains($taskFile.FullName)){
            if(!$taskPreviousFiles.ContainsKey($taskFile.FullName)){throw "Existing untracked file in package folder: $($taskFile.FullName)"}
            $taskPrevious=$taskPreviousFiles[$taskFile.FullName]
            if((Get-FileHash -LiteralPath $taskFile.FullName).Hash -ne $taskPrevious.sha256){throw "Obsolete package asset was edited; preserve it before installation: $($taskFile.FullName)"}
            $taskObsoleteRows.Add([pscustomobject]@{Relative=$taskPrevious.relative.Replace('/',[IO.Path]::DirectorySeparatorChar);Source=$null;Destination=$taskFile.FullName;Existed=$true;OldHash=$taskPrevious.sha256;NewHash=$null})
        }
    }
}
$taskBackup=Join-Path $taskGame ('.lcs-sa-backups\stories-map-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
if(Test-Path -LiteralPath $taskBackup){throw 'Backup destination exists'}
[long]$taskNeeded=104857600
foreach($taskRow in $taskRows){if($taskRow.Changed){$taskNeeded+=(Get-Item -LiteralPath $taskRow.Source).Length;if($taskRow.Existed){$taskNeeded+=(Get-Item -LiteralPath $taskRow.Destination).Length}}}
foreach($taskRow in $taskObsoleteRows){$taskNeeded+=(Get-Item -LiteralPath $taskRow.Destination).Length}
if([IO.DriveInfo]::new([IO.Path]::GetPathRoot($taskGame)).AvailableFreeSpace -lt $taskNeeded){throw 'Insufficient free space'}
New-Item -ItemType Directory -Path $taskBackup -Force | Out-Null
foreach($taskRow in @($taskRows.ToArray())+@($taskObsoleteRows.ToArray())){
    if(!$taskRow.Existed -or ($taskRow.PSObject.Properties['Changed'] -and !$taskRow.Changed)){continue}
    $taskRow.OldHash=(Get-FileHash -LiteralPath $taskRow.Destination).Hash
    $taskSaved=Join-Path $taskBackup $taskRow.Relative
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($taskSaved)) -Force|Out-Null
    Copy-Item -LiteralPath $taskRow.Destination -Destination $taskSaved
    if((Get-FileHash -LiteralPath $taskSaved).Hash -ne $taskRow.OldHash){throw 'Backup hash mismatch; original installation unchanged'}
}
$taskChanged=[Collections.Generic.List[object]]::new()
$taskResult=[pscustomobject]@{GameDirectory=$taskGame;PackageDirectory=$taskTarget;BackupDirectory=$taskBackup;Mode=$taskMetadata.mode;Installed=$false;GameExecuted=$false;CampaignEnabled=($taskMetadata.mode -eq 'Campaign');InstalledAt=(Get-Date).ToString('o');Files=$taskRows;ObsoleteFilesBackedUp=$taskObsoleteRows;BackupMode='changed-files-only';NativeResourceValidation=$taskNative;NativeBootstrapCommands=$taskBootstrap.Commands;PackageValidation=$taskValidation;OriginalRootFilesChanged=$false}
$taskResult|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $taskBackup 'install-manifest.json') -Encoding utf8
Assert-TargetGameClosed
try{
    foreach($taskRow in $taskObsoleteRows){
        if(!$taskRow.Destination.StartsWith($taskTarget+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe obsolete file path'}
        $taskChanged.Add($taskRow)
        Remove-Item -LiteralPath $taskRow.Destination
    }
    foreach($taskRow in $taskRows){
        if(!$taskRow.Changed){continue}
        $taskChanged.Add($taskRow)
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($taskRow.Destination)) -Force|Out-Null
        Copy-Item -LiteralPath $taskRow.Source -Destination $taskRow.Destination -Force
        if((Get-FileHash -LiteralPath $taskRow.Destination).Hash -ne $taskRow.NewHash){throw "Installed hash mismatch: $($taskRow.Relative)"}
    }
    foreach($taskRow in $taskRows){
        if((Get-FileHash -LiteralPath $taskRow.Destination).Hash -ne $taskRow.NewHash){throw "Final installed hash mismatch: $($taskRow.Relative)"}
    }
}catch{
    foreach($taskRow in $taskChanged){
        if($taskRow.Existed){Copy-Item -LiteralPath (Join-Path $taskBackup $taskRow.Relative) -Destination $taskRow.Destination -Force}
        elseif(Test-Path -LiteralPath $taskRow.Destination){Remove-Item -LiteralPath $taskRow.Destination -Force}
    }
    throw
}
$taskResult.Installed=$true
$taskResult|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $taskBackup 'install-manifest.json') -Encoding utf8
$taskResult|ConvertTo-Json -Depth 7|Set-Content -LiteralPath $ReportFile -Encoding utf8
[pscustomobject]@{Installed=$true;VerifiedFiles=$taskRows.Count;ChangedFiles=@($taskRows|Where-Object Changed).Count;PackageDirectory=$taskTarget;BackupDirectory=$taskBackup;Mode=$taskMetadata.mode;GameExecuted=$false}|ConvertTo-Json
