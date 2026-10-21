param([int]$MissionCount=118)
$ErrorActionPreference='Stop'
$taskProject=Split-Path -Parent $PSScriptRoot
$taskInspector=Join-Path $taskProject 'build\Release\lcs-campaign-inspect.exe'
$taskMain=Join-Path $taskProject '..\lcs-ps2\DATA\MAIN.SCM'
$taskBindings=Join-Path $taskProject 'out\world-stage\data\lcs\model-bindings.tsv'
$taskEquivalents=Join-Path $taskProject 'out\native-equivalents.tsv'
$taskScenes=Join-Path $taskProject 'out\campaign-cutscenes-arrival\data\lcs\cutscenes\index.tsv'
$taskLogs=Join-Path $taskProject 'reports\mission-entry-probes'
New-Item -ItemType Directory -Path $taskLogs -Force | Out-Null
$taskRows=@()
for($taskIndex=0;$taskIndex -lt $MissionCount;$taskIndex++) {
    $taskOutput=@(& $taskInspector $taskMain $taskBindings $taskIndex --equivalents $taskEquivalents --cutscene-index $taskScenes 2>&1)
    $taskExit=$LASTEXITCODE
    $taskLog=Join-Path $taskLogs ($taskIndex.ToString('D3')+'.log')
    $taskOutput | Set-Content -LiteralPath $taskLog -Encoding UTF8
    $taskStop=@($taskOutput | Where-Object {$_ -match '^stop thread='})
    $taskCommand=$null;$taskDiagnostic=$null
    if($taskStop.Count -and $taskStop[0] -match 'command=(\S+) diagnostic=(.*)$'){$taskCommand=$Matches[1];$taskDiagnostic=$Matches[2]}
    elseif($taskExit){$taskDiagnostic=($taskOutput | Select-Object -Last 1).ToString()}
    $taskStatus=if($taskExit){'inspection-error'}elseif($taskStop.Count){'adapter-blocked'}else{'idle-entry-only'}
    $taskRows += [pscustomobject]@{Index=$taskIndex;Status=$taskStatus;FirstBlockingCommand=$taskCommand;Diagnostic=$taskDiagnostic;Log=$taskLog;ExitCode=$taskExit}
}
$taskReport=[pscustomobject]@{GameExecuted=$false;PlayabilityVerified=$false;MissionProgressionSimulated=$false;Scope='Each original block starts independently with zero script globals and an idle simulated engine; this checks entry adapters, not mission completion or story prerequisites';OriginalMainSha256=(Get-FileHash -LiteralPath $taskMain).Hash;BlocksInspected=$taskRows.Count;Results=$taskRows}
$taskReport | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskProject 'reports\all-mission-entry-probes.json') -Encoding UTF8
$taskRows | Group-Object Status | Select-Object Name,Count | Format-Table -AutoSize
$taskRows | Where-Object FirstBlockingCommand | Group-Object FirstBlockingCommand | Sort-Object Count -Descending | Select-Object Name,Count | Format-Table -AutoSize
