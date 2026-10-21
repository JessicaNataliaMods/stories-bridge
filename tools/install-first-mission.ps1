param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [switch]$PrepareOnly,
    [switch]$WithCutscenes,
    [string]$CutscenePackageDirectory=(Join-Path $PSScriptRoot '..\out\campaign-cutscenes-arrival')
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'object-data.ps1')
$taskProject=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskGame=(Resolve-Path -LiteralPath $GameDirectory).Path.TrimEnd('\')
$taskData=(Resolve-Path -LiteralPath (Join-Path $taskProject 'out\lcs-converted')).Path
$taskStage=Join-Path $taskProject 'out\first-mission-test'
function Assert-Closed {
    foreach($taskProcess in @(Get-CimInstance Win32_Process -Filter "Name='gta_sa.exe' OR Name='euryopa.exe'")){
        if($taskProcess.Name -ieq 'euryopa.exe' -or !$taskProcess.ExecutablePath -or
           [IO.Path]::GetFullPath($taskProcess.ExecutablePath).StartsWith($taskGame+'\',[StringComparison]::OrdinalIgnoreCase)){
            throw 'Close the target GTA or map viewer before installing; this tool never opens or closes them'
        }
    }
}
Assert-Closed
$taskProfile=(& (Join-Path $PSScriptRoot 'inspect-install.ps1') -GameDirectory $taskGame)|ConvertFrom-Json
if($taskProfile.SHA256 -ne 'F01A00CE950FA40CA1ED59DF0E789848C6EDCF6405456274965885D0929343AC' -or
   !$taskProfile.NativeScriptDispatcherMatches -or !$taskProfile.NativeScriptLoopCallMatches -or
   !$taskProfile.NativeScriptStartupCallMatches -or !$taskProfile.HasAsiLoader){throw 'Unverified first-mission native executable profile'}
if((Get-FileHash -LiteralPath (Join-Path $taskData 'DATA\MAIN.SCM') -Algorithm SHA256).Hash -ne '2AB65C950B86F02078B56FBAC10724C0BF90EB2329BDDC7A480C98CC8A7A65D4'){throw 'Unexpected original LCS SCM'}
if((Get-FileHash -LiteralPath (Join-Path $taskGame 'data\script\main.scm') -Algorithm SHA256).Hash -ne 'BD7BAAF5BE023A7424DD31632F077AC3BACD519C7BB26698DCCF761B7D147FDC'){throw 'Native startup bootstrap changed; inspect before installing'}
New-Item -ItemType Directory -Path (Join-Path $taskStage 'data\lcs'),(Join-Path $taskStage 'scripts') -Force | Out-Null
$taskAscii=[Text.Encoding]::ASCII
$taskIde=[IO.File]::ReadAllText((Join-Path $taskGame 'data\lcs\world.ide'))
$taskDoorRows=[regex]::Matches($taskIde,'(?m)^10780, lc1095, lt312, 60, (0|2048)\r?$')
if($taskDoorRows.Count -ne 1){throw 'Original garage-door model definition changed'}
$taskIde=[regex]::Replace($taskIde,'(?m)^10780, lc1095, lt312, 60, (0|2048)\r?$','10780, lc1095, lt312, 60, 2048')
[IO.File]::WriteAllText((Join-Path $taskStage 'data\lcs\world.ide'),$taskIde,$taskAscii)
$taskObjects=[IO.File]::ReadAllText((Join-Path $taskGame 'data\object.dat'))
$taskObjects=Set-FirstMissionDoorPhysics $taskObjects
[IO.File]::WriteAllText((Join-Path $taskStage 'data\object.dat'),$taskObjects,$taskAscii)
# These eight bounds are from the verified original SET_GARAGE in initialization
# block zero. SA's GRGE loader creates a genuine script-controlled garage.
[IO.File]::WriteAllText((Join-Path $taskStage 'data\lcs\firstmission.ipl'),"grge`r`n1160.7 -267.5 16.25 1156 -267.5 1160.7 -273.7 19.25 0 19 LCSVIC1`r`nend`r`n",$taskAscii)
$taskLevel=[IO.File]::ReadAllText((Join-Path $taskGame 'data\gta.dat'))
if(![regex]::IsMatch($taskLevel,'(?im)^IPL DATA\\LCS\\FIRSTMISSION\.IPL\s*$')){
    $taskLevel=$taskLevel.TrimEnd()+"`r`nIPL DATA\LCS\FIRSTMISSION.IPL`r`n"
}
[IO.File]::WriteAllText((Join-Path $taskStage 'data\gta.dat'),$taskLevel,$taskAscii)
$taskIni="[LCS]`r`nMode=FirstMission`r`nDataDirectory=$taskData`r`nWorldDirectory=$(Join-Path $taskGame 'data\lcs')`r`n"
$taskCutManifest=$null;$taskCutPackage=[IO.Path]::GetFullPath($CutscenePackageDirectory)
if($WithCutscenes){
    & (Join-Path $PSScriptRoot 'validate-mounted-cutscenes.ps1') -PackageDirectory $taskCutPackage | Out-Null
    $taskCutManifest=Get-Content -LiteralPath (Join-Path $taskCutPackage 'manifest.json') -Raw | ConvertFrom-Json
    foreach($taskFile in $taskCutManifest.Files){
        $taskRelative=$taskFile.Relative
        if($taskRelative -notmatch '^(anim\\cuts\.img|models\\cutscene\.img|data\\lcs\\cutscenes\\index\.tsv|data\\lcs\\cutscenes\\audio\\[a-z0-9_]{1,8}\.wav)$'){throw 'Unexpected mounted cutscene install path'}
        $taskSource=Join-Path $taskCutPackage $taskRelative
        if((Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash -ne $taskFile.Sha256){throw 'Mounted cutscene package changed'}
        $taskStaged=Join-Path $taskStage $taskRelative
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($taskStaged)) -Force | Out-Null
        Copy-Item -LiteralPath $taskSource -Destination $taskStaged -Force
        if($taskRelative -eq 'anim\cuts.img'){$taskOriginal=$taskCutManifest.OriginalCutsArchiveSha256;$taskPrevious=$taskCutManifest.PredecessorCutsArchiveSha256}
        elseif($taskRelative -eq 'models\cutscene.img'){$taskOriginal=$taskCutManifest.OriginalModelArchiveSha256;$taskPrevious=$taskCutManifest.PredecessorModelArchiveSha256}
        else{continue}
        if((Get-FileHash -LiteralPath (Join-Path $taskGame $taskRelative) -Algorithm SHA256).Hash -notin @($taskOriginal,$taskPrevious,$taskFile.Sha256)){throw 'Native cutscene archive changed since package preparation'}
    }
    $taskIni+="CutsceneDirectory=$(Join-Path $taskGame 'data\lcs\cutscenes')`r`n"
}
[IO.File]::WriteAllText((Join-Path $taskStage 'scripts\LcsSaBridge.ini'),$taskIni,[Text.Encoding]::Unicode)
$taskResources=Get-Content -LiteralPath (Join-Path $taskProject 'out\mission-resources\manifest.json') -Raw | ConvertFrom-Json
foreach($taskFile in $taskResources.Files){
    if($taskFile.RelativePath -notmatch '^text\\(american|french|german|italian|spanish)\.gxt$'){throw 'Unexpected text resource path'}
    $taskOriginalHash=(Get-FileHash -LiteralPath (Join-Path $taskGame $taskFile.RelativePath) -Algorithm SHA256).Hash
    if($taskOriginalHash -notin @($taskFile.SourceSha256,$taskFile.Sha256)){throw "Native text changed since merge: $($taskFile.RelativePath)"}
    if((Get-FileHash -LiteralPath (Join-Path $taskProject ('out\mission-resources\'+$taskFile.RelativePath)) -Algorithm SHA256).Hash -ne $taskFile.Sha256){throw 'Prepared mission text changed'}
}
$taskValidation=(& (Join-Path $PSScriptRoot 'validate-sa-profile.ps1') -GameDirectory $taskGame -WorldDirectory $taskStage -ProfileFile (Join-Path $taskStage 'data\gta.dat') -CoreDirectory (Join-Path $taskGame 'models')) | ConvertFrom-Json
$taskValidation | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskProject 'reports\first-mission-native-profile.json') -Encoding UTF8
if(!$taskValidation.NativeProfileValidated){throw 'Native world resource validation failed'}
if($PrepareOnly){Write-Output "First-mission package prepared and validated: $taskStage. Game untouched.";return}
$taskRows=[Collections.Generic.List[object]]::new()
function Add-File([string]$Source,[string]$Relative){
    if(!(Test-Path -LiteralPath $Source -PathType Leaf)){throw "Missing install file: $Source"}
    $taskDestination=[IO.Path]::GetFullPath((Join-Path $taskGame $Relative))
    if(!$taskDestination.StartsWith($taskGame+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Install path outside game directory'}
    $taskRows.Add([pscustomobject]@{Source=$Source;Relative=$Relative;Destination=$taskDestination;Existed=(Test-Path -LiteralPath $taskDestination);OldHash=$null;NewHash=(Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash})
}
foreach($taskRelative in @('data\gta.dat','data\object.dat','data\lcs\world.ide','data\lcs\firstmission.ipl','scripts\LcsSaBridge.ini')){Add-File (Join-Path $taskStage $taskRelative) $taskRelative}
foreach($taskFile in $taskResources.Files){Add-File (Join-Path $taskProject ('out\mission-resources\'+$taskFile.RelativePath)) $taskFile.RelativePath}
Add-File (Join-Path $taskProject 'build\Release\LcsSaBridge.asi') 'scripts\LcsSaBridge.asi'
if($WithCutscenes){foreach($taskFile in $taskCutManifest.Files){Add-File (Join-Path $taskStage $taskFile.Relative) $taskFile.Relative}}
$taskBackup=Join-Path $taskGame ('.lcs-sa-backups\first-mission-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
if(Test-Path -LiteralPath $taskBackup){throw 'Backup destination already exists'}
New-Item -ItemType Directory -Path $taskBackup -Force | Out-Null
# Verify all backups before any game file is replaced.
foreach($taskRow in $taskRows){if($taskRow.Existed){
    $taskRow.OldHash=(Get-FileHash -LiteralPath $taskRow.Destination -Algorithm SHA256).Hash
    $taskCopy=Join-Path $taskBackup $taskRow.Relative
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($taskCopy)) -Force | Out-Null
    Copy-Item -LiteralPath $taskRow.Destination -Destination $taskCopy
    if((Get-FileHash -LiteralPath $taskCopy -Algorithm SHA256).Hash -ne $taskRow.OldHash){throw 'Backup verification failed; game untouched'}
}}
Assert-Closed
$taskChanged=[Collections.Generic.List[object]]::new()
try{
    foreach($taskRow in $taskRows){
        if($taskRow.Existed -and (Get-FileHash -LiteralPath $taskRow.Destination -Algorithm SHA256).Hash -ne $taskRow.OldHash){throw 'Game file changed during backup'}
        $taskChanged.Add($taskRow)
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($taskRow.Destination)) -Force | Out-Null
        Copy-Item -LiteralPath $taskRow.Source -Destination $taskRow.Destination -Force
        if((Get-FileHash -LiteralPath $taskRow.Destination -Algorithm SHA256).Hash -ne $taskRow.NewHash){throw 'Installed hash mismatch'}
    }
}catch{
    foreach($taskRow in $taskChanged){
        if($taskRow.Existed){Copy-Item -LiteralPath (Join-Path $taskBackup $taskRow.Relative) -Destination $taskRow.Destination -Force}
        elseif(Test-Path -LiteralPath $taskRow.Destination){Remove-Item -LiteralPath $taskRow.Destination -Force}
    }
    throw
}
$taskArrival=$WithCutscenes -and @($taskCutManifest.Scenes | Where-Object Name -ceq 'intro').Count -eq 1
$taskManifest=[pscustomobject]@{GameDirectory=$taskGame;BackupDirectory=$taskBackup;Mode='FirstMission';Mission='VIC1 / Home Sweet Home';InstalledAt=(Get-Date).ToString('o');Files=$taskRows;OriginalBytecode=$true;OpeningMovie=[bool]$WithCutscenes;ArrivalMovie=$taskArrival;CutsceneResourcesMounted=[bool]$WithCutscenes;NativeTestAppearances=$true;GameExecuted=$false;GameTested=$false;MissionPlayableConfirmed=$false;CampaignPlayable=$false}
$taskManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskBackup 'install-manifest.json') -Encoding UTF8
$taskManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskProject 'reports\first-mission-install.json') -Encoding UTF8
$taskResources.Installed=$true
$taskResources | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskProject 'out\mission-resources\manifest.json') -Encoding UTF8
if($WithCutscenes){$taskCutManifest.Installed=$true;$taskCutManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskCutPackage 'manifest.json') -Encoding UTF8}
Write-Output "First-mission test installed, all $($taskRows.Count) file hashes verified. Opening movie enabled: $([bool]$WithCutscenes). Backup: $taskBackup. Game not executed."
