param([Parameter(Mandatory)][string]$GameDirectory)
$ErrorActionPreference='Stop'
$taskProject=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskGame=(Resolve-Path -LiteralPath $GameDirectory).Path.TrimEnd('\')
function Assert-Closed {
    foreach($taskProcess in @(Get-CimInstance Win32_Process -Filter "Name='gta_sa.exe' OR Name='euryopa.exe'")) {
        if($taskProcess.Name -ieq 'euryopa.exe' -or !$taskProcess.ExecutablePath -or
           [IO.Path]::GetFullPath($taskProcess.ExecutablePath).StartsWith($taskGame+'\',[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Target GTA or map viewer is open; this installer never starts or closes either program'
        }
    }
}
Assert-Closed
$taskProfile=(& (Join-Path $PSScriptRoot 'inspect-install.ps1') -GameDirectory $taskGame) | ConvertFrom-Json
if($taskProfile.SHA256 -ne 'F01A00CE950FA40CA1ED59DF0E789848C6EDCF6405456274965885D0929343AC' -or
   !$taskProfile.NativeScriptDispatcherMatches -or !$taskProfile.NativeScriptLoopCallMatches -or
   !$taskProfile.NativeScriptStartupCallMatches -or !$taskProfile.HasAsiLoader){throw 'Unverified native executable profile'}
& (Join-Path $taskProject 'tests\campaign_native_profile.ps1') -Executable (Join-Path $taskGame 'gta_sa.exe') -ProjectDirectory $taskProject | Out-Null
$taskData=(Resolve-Path -LiteralPath (Join-Path $taskProject 'out\lcs-converted')).Path
if((Get-FileHash -LiteralPath (Join-Path $taskData 'DATA\MAIN.SCM')).Hash -ne '2AB65C950B86F02078B56FBAC10724C0BF90EB2329BDDC7A480C98CC8A7A65D4'){throw 'Original LCS campaign script changed'}
$taskNewsSource=(Resolve-Path -LiteralPath (Join-Path $taskProject '..\lcs-ps2\AUDIO\NEWS')).Path
& (Join-Path $taskProject 'build\Release\lcs-resource-tests.exe') $taskData $taskNewsSource | Out-Null
if($LASTEXITCODE){throw 'Original campaign text/speech or the 14 original radio-news streams failed validation'}
if((Get-FileHash -LiteralPath (Join-Path $taskGame 'data\script\main.scm')).Hash -ne 'BD7BAAF5BE023A7424DD31632F077AC3BACD519C7BB26698DCCF761B7D147FDC'){throw 'Native world bootstrap changed'}
$taskGestures=Join-Path $taskProject 'out\gameplay-animations-v3'
$taskPython=Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
& $taskPython (Join-Path $taskProject 'tests\gameplay_animation_tests.py') $taskGestures | Out-Null
if($LASTEXITCODE){throw 'Original gesture validation failed'}
$taskNamed=Join-Path $taskProject 'out\named-animations'
& $taskPython (Join-Path $taskProject 'tests\named_animation_tests.py') $taskNamed | Out-Null
if($LASTEXITCODE){throw 'Original named animation validation failed'}
$taskNamedManifest=Get-Content -LiteralPath (Join-Path $taskNamed 'manifest.json') -Raw | ConvertFrom-Json
$taskGestureManifest=Get-Content -LiteralPath (Join-Path $taskGestures 'manifest.json') -Raw | ConvertFrom-Json
$taskEquivalents=Join-Path $taskProject 'out\native-equivalents.tsv'
if((Get-FileHash -LiteralPath $taskEquivalents).Hash -ne '3DCE1745E460DCFB7A7E5FA3293E7FA030E08AFEC0AA5493E493D3E2E79D2345'){throw 'Native equivalent profile changed'}
$taskCutscenePackage=Join-Path $taskProject 'out\campaign-cutscenes-arrival'
& (Join-Path $PSScriptRoot 'validate-mounted-cutscenes.ps1') -PackageDirectory $taskCutscenePackage | Out-Null
$taskCutsceneManifest=Get-Content -LiteralPath (Join-Path $taskCutscenePackage 'manifest.json') -Raw | ConvertFrom-Json
foreach($taskFile in $taskCutsceneManifest.Files){
    if((Get-FileHash -LiteralPath (Join-Path $taskGame $taskFile.Relative)).Hash -ne $taskFile.Sha256){throw 'Installed cutscene resource differs from the prepared package'}
}
foreach($taskRelative in @('data\lcs\world.ide','data\lcs\model-bindings.tsv','data\lcs\world0.ipl','data\lcs\world1.ipl','data\lcs\world2.ipl')){
    if(!(Test-Path -LiteralPath (Join-Path $taskGame $taskRelative) -PathType Leaf)){throw "Mounted world file missing: $taskRelative"}
}
$taskStage=Join-Path $taskProject 'out\native-campaign-install'
New-Item -ItemType Directory -Path $taskStage -Force | Out-Null
$taskIniPath=Join-Path $taskGame 'scripts\LcsSaBridge.ini'
$taskIni=[IO.File]::ReadAllText($taskIniPath)
if($taskIni -notmatch '(?im)^\[LCS\]\s*$' -or $taskIni -notmatch '(?im)^Mode=(FirstMission|Campaign)\s*$'){throw 'Unexpected previous bridge configuration'}
foreach($taskSetting in @(@('DataDirectory',$taskData),@('WorldDirectory',(Join-Path $taskGame 'data\lcs')),@('CutsceneDirectory',(Join-Path $taskGame 'data\lcs\cutscenes')))){
    if($taskIni -notmatch ('(?im)^'+$taskSetting[0]+'='+[regex]::Escape($taskSetting[1])+'\s*$')){throw "Existing bridge path differs: $($taskSetting[0])"}
}
$taskIni=[regex]::Replace($taskIni,'(?im)^Mode=(FirstMission|Campaign)[ \t]*','Mode=Campaign')
if($taskIni -match '(?im)^EquivalentEntities='){ $taskIni=[regex]::Replace($taskIni,'(?im)^EquivalentEntities=.*$','EquivalentEntities=1') }
else { $taskIni=$taskIni.TrimEnd()+"`r`nEquivalentEntities=1`r`n" }
$taskStagedIni=Join-Path $taskStage 'LcsSaBridge.ini'
[IO.File]::WriteAllText($taskStagedIni,$taskIni,[Text.Encoding]::Unicode)
$taskRows=[Collections.Generic.List[object]]::new()
function Add-File([string]$Source,[string]$Relative) {
    if(!(Test-Path -LiteralPath $Source -PathType Leaf)){throw "Missing source: $Source"}
    $taskDestination=[IO.Path]::GetFullPath((Join-Path $taskGame $Relative))
    if(!$taskDestination.StartsWith($taskGame+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Install path escaped the game directory'}
    $taskRows.Add([pscustomobject]@{Source=$Source;Relative=$Relative;Destination=$taskDestination;Existed=(Test-Path -LiteralPath $taskDestination -PathType Leaf);OldHash=$null;NewHash=(Get-FileHash -LiteralPath $Source).Hash})
}
Add-File (Join-Path $taskProject 'build\Release\LcsSaBridge.asi') 'scripts\LcsSaBridge.asi'
Add-File $taskStagedIni 'scripts\LcsSaBridge.ini'
Add-File $taskEquivalents 'data\lcs\native-equivalents.tsv'
Add-File (Join-Path $taskGestures 'index.tsv') 'data\lcs\animations\index.tsv'
Add-File (Join-Path $taskNamed 'index.tsv') 'data\lcs\animations\named\index.tsv'
foreach($taskFile in $taskNamedManifest.files) {
    if($taskFile.path -cnotmatch '^LN\d{2}\.ifp$'){throw 'Unexpected named animation path'}
    Add-File (Join-Path $taskNamed $taskFile.path) ('data\lcs\animations\named\'+$taskFile.path)
}
foreach($taskFile in $taskGestureManifest.files) {
    if($taskFile.path -cnotmatch '^LG\d{2}\.ifp$'){throw 'Unexpected gameplay animation path'}
    Add-File (Join-Path $taskGestures $taskFile.path) ('data\lcs\animations\'+$taskFile.path)
}
$taskBackup=Join-Path $taskGame ('.lcs-sa-backups\native-campaign-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
if(Test-Path -LiteralPath $taskBackup){throw 'Backup already exists'}
New-Item -ItemType Directory -Path $taskBackup | Out-Null
foreach($taskRow in $taskRows){if($taskRow.Existed){
    $taskRow.OldHash=(Get-FileHash -LiteralPath $taskRow.Destination).Hash
    $taskCopy=Join-Path $taskBackup $taskRow.Relative
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($taskCopy)) -Force | Out-Null
    Copy-Item -LiteralPath $taskRow.Destination -Destination $taskCopy
    if((Get-FileHash -LiteralPath $taskCopy).Hash -ne $taskRow.OldHash){throw 'Backup verification failed; game files unchanged'}
}}
Assert-Closed
$taskChanged=[Collections.Generic.List[object]]::new()
try {
    foreach($taskRow in $taskRows){
        if($taskRow.Existed -and (Get-FileHash -LiteralPath $taskRow.Destination).Hash -ne $taskRow.OldHash){throw 'Game file changed during backup'}
        $taskChanged.Add($taskRow)
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($taskRow.Destination)) -Force | Out-Null
        Copy-Item -LiteralPath $taskRow.Source -Destination $taskRow.Destination -Force
        if((Get-FileHash -LiteralPath $taskRow.Destination).Hash -ne $taskRow.NewHash){throw 'Installed hash mismatch'}
    }
} catch {
    foreach($taskRow in $taskChanged){
        if(!$taskRow.Destination.StartsWith($taskGame+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe rollback path'}
        if($taskRow.Existed){Copy-Item -LiteralPath (Join-Path $taskBackup $taskRow.Relative) -Destination $taskRow.Destination -Force}
        elseif(Test-Path -LiteralPath $taskRow.Destination){Remove-Item -LiteralPath $taskRow.Destination -Force}
    }
    throw
}
$taskManifest=[pscustomobject]@{GameDirectory=$taskGame;BackupDirectory=$taskBackup;Mode='Campaign';EquivalentEntities=$true;NativeEntityBindings=231;OriginalGestureStates=71;OriginalAnimationBlocks=25;OriginalDynamicWaitStates=@(29);AirportRiotAnimations=7;OriginalNewsStreams=14;OriginalMainUnchanged=$true;InstalledAt=(Get-Date).ToString('o');Files=$taskRows;GameExecuted=$false;CampaignPlayable=$false;ProgressionVerifiedOffline='Original MAIN -> initialization -> INTRO -> HOMSWET -> VIC1 completion -> original VIC2 contact -> Slacker passenger task/boarding while player drives/dropoff -> VIC2 completion -> original VIC3 contact/navigation/physical combat -> VIC3 completion; simulated player/engine';RuntimeStartupFix='Validate the streamed player name in native model slot 0 rather than its pre-streaming null IDE placeholder';CompanionFix='Explicit native passenger task for companions joining after the leader entered a vehicle';NavigationFix='Original pednode policy applied to native seek tasks before child creation; original traffic-light wait cycle';AirportRiotFix='Original aportpk state 29 at IP 213289; complete RIOT block, real native partial gestures/cycles, explicit clear and lifecycle'}
$taskManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskBackup 'install-manifest.json') -Encoding UTF8
$taskManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskProject 'reports\native-campaign-install.json') -Encoding UTF8
Write-Output "Mode=Campaign installed; $($taskRows.Count) hashes verified, 231 native equivalents, 71 gesture states. Backup: $taskBackup. GameExecuted=false; CampaignPlayable=false."
