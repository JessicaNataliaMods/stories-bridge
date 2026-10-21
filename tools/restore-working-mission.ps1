param([Parameter(Mandatory)][string]$GameDirectory)
$ErrorActionPreference='Stop'
$taskProject=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskGame=(Resolve-Path -LiteralPath $GameDirectory).Path.TrimEnd('\')
$taskSource=Join-Path $taskGame '.lcs-sa-backups\native-campaign-20261009-153744'
$taskManifest=Get-Content -LiteralPath (Join-Path $taskSource 'install-manifest.json') -Raw | ConvertFrom-Json
if($taskManifest.GameDirectory -ne $taskGame){throw 'Backup belongs to another game installation'}
function Assert-Closed {
    foreach($taskProcess in @(Get-CimInstance Win32_Process -Filter "Name='gta_sa.exe' OR Name='euryopa.exe'")){
        if($taskProcess.Name -ieq 'euryopa.exe' -or !$taskProcess.ExecutablePath -or
           [IO.Path]::GetFullPath($taskProcess.ExecutablePath).StartsWith($taskGame+'\',[StringComparison]::OrdinalIgnoreCase)){
            throw 'Target GTA or map viewer is open; restoration never starts or closes programs'
        }
    }
}
Assert-Closed
$taskRows=@($taskManifest.Files | Where-Object { $_.Relative -in @('scripts\LcsSaBridge.asi','scripts\LcsSaBridge.ini') })
if($taskRows.Count -ne 2){throw 'Backup must contain the exact bridge binary/configuration pair'}
foreach($taskRow in $taskRows){
    $taskPath=[IO.Path]::GetFullPath((Join-Path $taskSource $taskRow.Relative))
    if(!$taskPath.StartsWith($taskSource+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe backup source'}
    if((Get-FileHash -LiteralPath $taskPath).Hash -ne $taskRow.OldHash){throw 'Working backup hash mismatch'}
    $taskDestination=[IO.Path]::GetFullPath((Join-Path $taskGame $taskRow.Relative))
    if(!$taskDestination.StartsWith($taskGame+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe restoration target'}
    if(!(Test-Path -LiteralPath $taskDestination -PathType Leaf)){throw 'Existing bridge file missing'}
}
$taskIni=[IO.File]::ReadAllText((Join-Path $taskSource 'scripts\LcsSaBridge.ini'))
if($taskIni -notmatch '(?im)^Mode=FirstMission\s*$'){throw 'Backup does not contain the working FirstMission profile'}
$taskBackup=Join-Path $taskGame ('.lcs-sa-backups\regression-restore-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
if(Test-Path -LiteralPath $taskBackup){throw 'Rollback backup already exists'}
New-Item -ItemType Directory -Path (Join-Path $taskBackup 'scripts') | Out-Null
$taskRecords=@()
foreach($taskRow in $taskRows){
    $taskDestination=Join-Path $taskGame $taskRow.Relative
    $taskBeforeHash=(Get-FileHash -LiteralPath $taskDestination).Hash
    $taskSaved=Join-Path $taskBackup $taskRow.Relative
    Copy-Item -LiteralPath $taskDestination -Destination $taskSaved
    if((Get-FileHash -LiteralPath $taskSaved).Hash -ne $taskBeforeHash){throw 'Regression backup hash mismatch'}
    $taskRecords += [pscustomobject]@{Relative=$taskRow.Relative;BeforeHash=$taskBeforeHash;RestoredHash=$taskRow.OldHash}
}
$taskLog=Join-Path $taskGame 'scripts\LcsSaBridge.log'
if(Test-Path -LiteralPath $taskLog){Copy-Item -LiteralPath $taskLog -Destination (Join-Path $taskBackup 'scripts\LcsSaBridge.log')}
Assert-Closed
$taskChanged=@()
try {
    foreach($taskRow in $taskRows){
        $taskRecord=$taskRecords | Where-Object Relative -EQ $taskRow.Relative
        $taskDestination=Join-Path $taskGame $taskRow.Relative
        if((Get-FileHash -LiteralPath $taskDestination).Hash -ne $taskRecord.BeforeHash){throw 'Bridge changed during backup'}
        $taskChanged += $taskRow
        Copy-Item -LiteralPath (Join-Path $taskSource $taskRow.Relative) -Destination $taskDestination -Force
        if((Get-FileHash -LiteralPath $taskDestination).Hash -ne $taskRow.OldHash){throw 'Restored bridge hash mismatch'}
    }
} catch {
    foreach($taskRow in $taskChanged){
        Copy-Item -LiteralPath (Join-Path $taskBackup $taskRow.Relative) -Destination (Join-Path $taskGame $taskRow.Relative) -Force
    }
    throw
}
$taskReport=[pscustomobject]@{GameDirectory=$taskGame;RestoredFrom=$taskSource;RegressionBackup=$taskBackup;InstalledMode='FirstMission';Files=$taskRecords;Cause='Native equivalent validation rejected player model 0 by the null IDE placeholder name before campaign startup';GameExecuted=$false;RestoredAt=(Get-Date).ToString('o')}
$taskReport | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskBackup 'restore-manifest.json') -Encoding UTF8
$taskReport | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskProject 'reports\working-mission-restore.json') -Encoding UTF8
Write-Output "Restored verified FirstMission ASI/configuration pair. Regression backup: $taskBackup. GameExecuted=false."
