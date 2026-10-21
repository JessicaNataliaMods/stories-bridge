param(
    [Parameter(Mandatory)][int]$CurrentVersion,
    [Parameter(Mandatory)][int]$RollbackVersion,
    [Parameter(Mandatory)][int[]]$RetiredVersions
)
$ErrorActionPreference='Stop'
$taskProject=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskOut=(Resolve-Path -LiteralPath (Join-Path $taskProject 'out')).Path
if($taskOut -ne (Join-Path $taskProject 'out') -or $RollbackVersion -ge $CurrentVersion){throw 'Invalid stage scope'}
$taskProtected=@("campaign-copy4-v$RollbackVersion","campaign-copy4-v$CurrentVersion")
$taskCandidates=@($RetiredVersions | ForEach-Object {
    if($_ -le 0 -or $_ -ge $RollbackVersion){throw 'Retirement must precede the retained rollback'}
    "campaign-copy4-v$_"
})
$taskExecutableFiles=@(Get-Item -LiteralPath (Join-Path $taskProject 'CMakeLists.txt'))+@(Get-ChildItem -LiteralPath (Join-Path $taskProject 'src'),(Join-Path $taskProject 'include'),(Join-Path $taskProject 'tests'),$PSScriptRoot -Recurse -File |
    Where-Object {$_.Extension -in @('.py','.ps1','.cpp','.h','.hpp','.inc','.bat','.cmake')})
foreach($taskName in $taskCandidates){
    if($taskExecutableFiles | Select-String -SimpleMatch -Pattern $taskName){throw ('Executable reference remains: '+$taskName)}
    foreach($taskKeep in $taskProtected){
        $taskJson=Get-Content -LiteralPath (Join-Path $taskOut ($taskKeep+'\metadata\campaign.json')) -Raw
        if($taskJson.Contains($taskName)){throw ('Retained campaign metadata references '+$taskName)}
    }
}
$taskHashes=@{}
foreach($taskKeep in $taskProtected){
    $taskRoot=(Resolve-Path -LiteralPath (Join-Path $taskOut $taskKeep)).Path
    $taskMetadata=Get-Content -LiteralPath (Join-Path $taskRoot 'metadata\campaign.json') -Raw | ConvertFrom-Json
    $taskValidation=Get-Content -LiteralPath (Join-Path $taskRoot 'metadata\package-validation.json') -Raw | ConvertFrom-Json
    if(!$taskMetadata.nativeStartupValidated -or !$taskValidation.valid){throw 'Retained stage is not validated'}
    $taskManifest=Get-Content -LiteralPath (Join-Path $taskRoot 'metadata\package-files.json') -Raw | ConvertFrom-Json
    foreach($taskEntry in $taskManifest){
        $taskFile=[IO.Path]::GetFullPath((Join-Path $taskRoot $taskEntry.relative))
        if(!$taskFile.StartsWith($taskRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Protected manifest leaves stage'}
        $taskHash=(Get-FileHash -LiteralPath $taskFile).Hash
        if($taskHash -ne $taskEntry.sha256){throw ('Protected file hash mismatch: '+$taskFile)}
        $taskHashes[$taskFile]=$taskHash
    }
}
foreach($taskName in $taskCandidates){
    $taskFolder=(Resolve-Path -LiteralPath (Join-Path $taskOut $taskName)).Path
    if([IO.Path]::GetDirectoryName($taskFolder) -ne $taskOut){throw 'Recycle target leaves output root'}
    $taskItems=@(Get-Item -LiteralPath $taskFolder)+@(Get-ChildItem -LiteralPath $taskFolder -Recurse -Force)
    if($taskItems | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}){throw 'Candidate contains a junction or symlink'}
    $taskFiles=@($taskItems | Where-Object {!$_.PSIsContainer})
    $taskReport=[ordered]@{Method='Windows IFileOperation / Recycle Bin';OriginalPath=$taskFolder;RecycleBinPath=$null;Files=$taskFiles.Count;
        LogicalBytes=($taskFiles | Measure-Object -Property Length -Sum).Sum;ImmutableAssetsSharedByHardlinks=$true;PermanentDeleteFallback=$false;
        CurrentVersion=$CurrentVersion;RetainedRollback=$RollbackVersion;GameExecuted=$false;Completed=$false}
    $taskReportFile=Join-Path $taskProject ('reports\storage-retired-'+$taskName.Replace('copy4-','')+'.json')
    $taskReport | ConvertTo-Json | Set-Content -LiteralPath $taskReportFile
    $taskResult=& (Join-Path $taskProject 'build\recycle-helper\Release\recycle-folder.exe') $taskFolder $taskOut
    if($LASTEXITCODE -ne 0 -or (Test-Path -LiteralPath $taskFolder) -or !$taskResult){throw ('Recycle failed for '+$taskName)}
    $taskReport.RecycleBinPath=($taskResult -join "`n");$taskReport.Completed=$true
    $taskReport | ConvertTo-Json | Set-Content -LiteralPath $taskReportFile
    $taskReport | ConvertTo-Json -Compress
}
foreach($taskFile in $taskHashes.Keys){if((Get-FileHash -LiteralPath $taskFile).Hash -ne $taskHashes[$taskFile]){throw 'Protected asset changed during recycling'}}
Write-Output ('Protected package files verified after recycling: '+$taskHashes.Count)
