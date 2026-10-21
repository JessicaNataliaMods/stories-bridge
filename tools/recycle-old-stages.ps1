param([switch]$Preview)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Get-AuditedHash([string]$Path){
    $taskStream=[IO.File]::OpenRead($Path)
    $taskSha=[Security.Cryptography.SHA256]::Create()
    try{return [BitConverter]::ToString($taskSha.ComputeHash($taskStream)).Replace('-','')}
    finally{$taskStream.Dispose();$taskSha.Dispose()}
}
$taskProject=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskWorkspace=[IO.Directory]::GetParent($taskProject).FullName
$taskOut=(Resolve-Path -LiteralPath (Join-Path $taskProject 'out')).Path
$taskPlanFile=Join-Path $taskProject 'reports\storage-cleanup-candidates-20261011.json'
if((Get-AuditedHash $taskPlanFile) -ne '73A6BE5572D8F2CA31B011AAE0C1C2BF9A4F6058AE6A29554E51078EDA23394D'){
    throw 'Audited cleanup list changed; no recycling started'
}
$taskPlan=Get-Content -LiteralPath $taskPlanFile -Raw | ConvertFrom-Json
if($taskPlan.workspace -ne $taskWorkspace -or $taskPlan.paths.Count -ne 35){throw 'Workspace/list mismatch'}
$taskInventory=Get-Content -LiteralPath (Join-Path $taskProject 'reports\workspace-storage-inventory-20261011.json') -Raw | ConvertFrom-Json
$taskExpected=@{}
foreach($taskRow in $taskInventory.directories){$taskExpected[$taskRow.path]=$taskRow}
$taskProtected=@('campaign-copy4-v25','campaign-copy4-v24','campaign-copy4-v21',
    'modloader-gameplay-peds-v18-copy4','ps2-map-reconverted-v13','ps2-map-standalone-check-v13',
    'ps2-map-reconverted-v1','ps2-player-v18','ps2-peds-v18','world-stage','lcs-converted',
    'campaign-cutscenes-prepared','campaign-cutscenes-mounted','campaign-cutscenes-arrival',
    'campaign-entities-v2','gameplay-animations-v3','named-animations','first-mission-assets','img-v1')
foreach($taskName in $taskProtected){
    if(!(Test-Path -LiteralPath (Join-Path $taskOut $taskName) -PathType Container)){
        throw "Required source/test directory missing: $taskName"
    }
}
$taskProtectedFiles=@('out\campaign-copy4-v25\LcsSaBridge.asi',
    'out\campaign-copy4-v25\metadata\package-files.json',
    'out\campaign-copy4-v24\LcsSaBridge.asi','out\campaign-copy4-v21\metadata\campaign.json',
    'out\ps2-map-reconverted-v13\manifest.json','tools\ps2-source-revision.json')
$taskHashes=@{}
foreach($taskRelative in $taskProtectedFiles){
    $taskHashes[$taskRelative]=Get-AuditedHash (Join-Path $taskProject $taskRelative)
}
foreach($taskRoot in @($taskWorkspace,$taskProject,$taskOut)){
    if((Get-Item -LiteralPath $taskRoot -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){
        throw "Reparse point in project ancestry: $taskRoot"
    }
}
$taskSeen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$taskCandidates=[Collections.Generic.List[object]]::new()
$taskMissing=[Collections.Generic.List[string]]::new()
foreach($taskPath in $taskPlan.paths){
    $taskAbsolute=[IO.Path]::GetFullPath($taskPath)
    if([IO.Path]::GetDirectoryName($taskAbsolute) -ne $taskOut -or
       !$taskAbsolute.StartsWith($taskOut+'\',[StringComparison]::OrdinalIgnoreCase) -or
       !$taskSeen.Add($taskAbsolute) -or
       [IO.Path]::GetFileName($taskAbsolute) -in $taskProtected){throw 'Unsafe or repeated cleanup target'}
    if(!(Test-Path -LiteralPath $taskAbsolute)){$taskMissing.Add($taskAbsolute);continue}
    $taskResolved=(Resolve-Path -LiteralPath $taskAbsolute).Path
    if($taskResolved -ne $taskAbsolute){throw 'Resolved target differs from audited path'}
    $taskKey='lcs-sa/out/'+[IO.Path]::GetFileName($taskAbsolute)
    if(!$taskExpected.ContainsKey($taskKey)){throw 'Target not in size inventory'}
    $taskStack=[Collections.Generic.Stack[IO.DirectoryInfo]]::new()
    $taskStack.Push((Get-Item -LiteralPath $taskAbsolute -Force))
    [long]$taskBytes=0;[long]$taskFileCount=0
    while($taskStack.Count){
        $taskDirectory=$taskStack.Pop()
        if($taskDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse directory in cleanup tree'}
        foreach($taskEntry in $taskDirectory.EnumerateFileSystemInfos()){
            if($taskEntry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse entry in cleanup tree'}
            if($taskEntry.Attributes -band [IO.FileAttributes]::Directory){$taskStack.Push($taskEntry)}
            else{$taskBytes+=$taskEntry.Length;$taskFileCount++}
        }
    }
    if($taskBytes -ne $taskExpected[$taskKey].bytes -or $taskFileCount -ne $taskExpected[$taskKey].files){
        throw "Folder changed after audit; preserve it and re-audit: $taskAbsolute"
    }
    $taskCandidates.Add([pscustomobject]@{Path=$taskAbsolute;Bytes=$taskBytes;Files=$taskFileCount})
}
[long]$taskTotal=0
foreach($taskCandidate in $taskCandidates){$taskTotal+=$taskCandidate.Bytes}
Write-Host ("Verified {0} folders; {1:N2} GiB. Missing/already recycled: {2}" -f $taskCandidates.Count,($taskTotal/1GB),$taskMissing.Count)
if($Preview){$taskCandidates | Select-Object Path,Bytes,Files | Format-Table -AutoSize;exit 0}
if($taskCandidates.Count -eq 0){Write-Host 'No audited folders remain to recycle.';exit 0}
$taskHelper=Join-Path $taskProject 'build\recycle-helper\Release\recycle-folder.exe'
if(!(Test-Path -LiteralPath $taskHelper -PathType Leaf)){throw 'Recycle helper missing; no cleanup started'}
$taskReportFile=Join-Path $taskProject ('reports\storage-recycle-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.json')
$taskReport=[ordered]@{StartedAt=(Get-Date -Format o);Method='Windows IFileOperation / Recycle Bin';
    PermanentDeleteFallback=$false;PlannedBytes=$taskTotal;Completed=$false;Recycled=@();Missing=@($taskMissing);Error=$null;ProtectedVerified=$false}
function Save-RecycleReport {$taskReport | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $taskReportFile -Encoding UTF8}
Save-RecycleReport
try {
    foreach($taskCandidate in $taskCandidates){
        Write-Host ('Sending to Recycle Bin: '+$taskCandidate.Path)
        $taskTrash=@(& $taskHelper $taskCandidate.Path $taskOut)
        if($LASTEXITCODE -ne 0){throw ('Windows refused recycling: '+$taskCandidate.Path)}
        if(Test-Path -LiteralPath $taskCandidate.Path){throw 'Recycled source still exists'}
        $taskReport.Recycled+=@([ordered]@{OriginalPath=$taskCandidate.Path;RecycleBinPath=($taskTrash -join '');Bytes=$taskCandidate.Bytes;Files=$taskCandidate.Files})
        Save-RecycleReport
    }
    foreach($taskRelative in $taskProtectedFiles){
        if((Get-AuditedHash (Join-Path $taskProject $taskRelative)) -ne $taskHashes[$taskRelative]){
            throw ('Protected resource changed: '+$taskRelative)
        }
    }
    $taskReport.ProtectedVerified=$true;$taskReport.Completed=$true
    Save-RecycleReport
    Write-Host ("Recycled {0} folders. Report: {1}" -f $taskReport.Recycled.Count,$taskReportFile)
    Write-Host 'Disk space remains in the Recycle Bin until you empty it yourself.'
}catch{
    $taskReport.Error=$_.Exception.Message;Save-RecycleReport;throw
}
