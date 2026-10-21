param([Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$Version)
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskOut=(Resolve-Path -LiteralPath (Join-Path $taskRoot 'out')).Path
$taskCache=Join-Path $taskRoot 'tools\__pycache__'
$taskStage=[IO.Path]::GetFullPath((Join-Path $taskOut "retired-python-cache-v$Version"))
$taskHelper=Join-Path $taskRoot 'build\recycle-helper\Release\recycle-folder.exe'
$taskReportFile=Join-Path $taskRoot "reports\storage-python-cache-v$Version.json"
if($taskOut -ne (Join-Path $taskRoot 'out') -or [IO.Path]::GetDirectoryName($taskStage) -ne $taskOut){throw 'Invalid cache retirement scope'}
if(!(Test-Path -LiteralPath $taskCache)){throw 'No cache to retire'}
if((Test-Path -LiteralPath $taskStage) -or (Test-Path -LiteralPath $taskReportFile)){throw 'Retirement stage/report already exists; review it before continuing'}
if(!(Test-Path -LiteralPath $taskHelper -PathType Leaf)){throw 'Native recycle helper is unavailable; cache unchanged'}
foreach($taskPath in @($taskRoot,$taskOut,$PSScriptRoot,$taskCache)){
    if((Get-Item -LiteralPath $taskPath).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Cache scope includes a reparse point'}
}
$taskItems=@(Get-ChildItem -LiteralPath $taskCache -Force)
if(!$taskItems.Count -or @($taskItems|Where-Object {$_.PSIsContainer -or ($_.Attributes -band [IO.FileAttributes]::ReparsePoint)}).Count){throw 'Cache has unexpected entries'}
$taskSources=@($taskItems|ForEach-Object {
    if($_.Name -notmatch '^([A-Za-z0-9_-]+)\.cpython-[0-9]+(?:\.opt-[0-9]+)?\.pyc$'){throw 'Unexpected cache filename'}
    $taskPath=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot ($Matches[1]+'.py')))
    if([IO.Path]::GetDirectoryName($taskPath) -ne $PSScriptRoot -or !(Test-Path -LiteralPath $taskPath -PathType Leaf)){throw 'Matching source is missing or outside tools'}
    if((Get-Item -LiteralPath $taskPath).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Source is a reparse point'}
    [pscustomobject]@{Path=$taskPath;Sha256=(Get-FileHash -LiteralPath $taskPath).Hash}
})
$taskReport=[ordered]@{Method='Windows IFileOperation / Recycle Bin';OriginalPath=$taskCache;StagedPath=$taskStage;
    RecycleBinPath=$null;Files=$taskItems.Count;LogicalBytes=($taskItems|Measure-Object Length -Sum).Sum;
    Sources=$taskSources;SourcesPreserved=0;PermanentDeleteFallback=$false;Completed=$false}
$taskReport|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $taskReportFile
Move-Item -LiteralPath $taskCache -Destination $taskStage
$taskResult=& $taskHelper $taskStage $taskOut
if($LASTEXITCODE -ne 0 -or (Test-Path -LiteralPath $taskStage) -or !$taskResult){throw 'Native recycling failed; no permanent deletion attempted'}
foreach($taskSource in $taskSources){if((Get-FileHash -LiteralPath $taskSource.Path).Hash -ne $taskSource.Sha256){throw 'Source hash changed during retirement'}}
$taskReport.RecycleBinPath=$taskResult -join "`n";$taskReport.SourcesPreserved=$taskSources.Count;$taskReport.Completed=$true
$taskReport|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $taskReportFile
[pscustomobject]@{Completed=$true;Files=$taskItems.Count;SourcesPreserved=$taskSources.Count;RecycleBinPath=$taskReport.RecycleBinPath}|ConvertTo-Json -Compress
