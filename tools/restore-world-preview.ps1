param([Parameter(Mandatory)][string]$ManifestFile)
$ErrorActionPreference='Stop'
if(Get-Process -Name gta_sa -ErrorAction SilentlyContinue){throw 'Close GTA SA before restoring; this script never controls the game'}
$taskManifest=Get-Content -LiteralPath $ManifestFile -Raw|ConvertFrom-Json
$taskGame=(Resolve-Path -LiteralPath $taskManifest.GameDirectory).Path
$taskBackup=(Resolve-Path -LiteralPath $taskManifest.BackupDirectory).Path
if(!$taskBackup.StartsWith($taskGame+'\.lcs-sa-backups\',[StringComparison]::OrdinalIgnoreCase)){throw 'Backup is outside the game backup directory'}
$taskAllowed=@('data\gta.dat','data\water.dat','data\script\main.scm','models\gta3.img','models\gta_int.img','data\lcs\world.ide','data\lcs\world.ipl','data\lcs\model-bindings.json','data\lcs\model-bindings.tsv','data\lcs\world-manifest.json','models\lcs\world.img','models\lcs\world.col','scripts\LcsSaBridge.asi','scripts\LcsSaBridge.ini')
foreach($taskRow in $taskManifest.Files){
    if($taskRow.Relative -notin $taskAllowed -and $taskRow.Relative -ne 'models\lcs\paths.img' -and $taskRow.Relative -notmatch '^data\\lcs\\world[0-9]+\.ipl$'){throw 'Unexpected restore target'}
    $taskDestination=[IO.Path]::GetFullPath((Join-Path $taskGame $taskRow.Relative))
    if(!$taskDestination.StartsWith($taskGame+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Restore target outside game directory'}
    if(!(Test-Path -LiteralPath $taskDestination) -or (Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash -ne $taskRow.NewHash){throw "File changed after installation; preserve it before restoring: $taskDestination"}
    if($taskRow.Existed -and (Get-FileHash -LiteralPath (Join-Path $taskBackup $taskRow.Relative) -Algorithm SHA256).Hash -ne $taskRow.OldHash){throw 'Original backup hash mismatch'}
}
foreach($taskRow in $taskManifest.Files){
    $taskDestination=Join-Path $taskGame $taskRow.Relative
    if($taskRow.Existed){Copy-Item -LiteralPath (Join-Path $taskBackup $taskRow.Relative) -Destination $taskDestination -Force}
    else{Remove-Item -LiteralPath $taskDestination -Force}
}
Write-Output 'Original files restored; backup retained. Game not executed.'
