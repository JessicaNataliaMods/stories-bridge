param([Parameter(Mandatory)][string]$ArchiveFile, [string]$PythonExecutable)
$ErrorActionPreference='Stop'
if(!$PythonExecutable){
    $taskCandidates=@((Join-Path (Split-Path -Parent $PSScriptRoot) 'python\python.exe'),
        (Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'))
    foreach($taskCandidate in $taskCandidates){if(Test-Path -LiteralPath $taskCandidate){$PythonExecutable=$taskCandidate;break}}
    if(!$PythonExecutable){$taskCommand=Get-Command python -ErrorAction SilentlyContinue;if($taskCommand){$PythonExecutable=$taskCommand.Source}}
}
if(!$PythonExecutable){throw 'Python 3 is required for the independent native path graph validator'}
$taskOldPreference=$ErrorActionPreference
try{
    $ErrorActionPreference='Continue'
    $taskResult=& $PythonExecutable (Join-Path $PSScriptRoot 'validate-sa-paths.py') (Resolve-Path -LiteralPath $ArchiveFile).Path 2>&1
    $taskExitCode=$LASTEXITCODE
}finally{$ErrorActionPreference=$taskOldPreference}
if($taskExitCode -ne 0){throw ($taskResult -join "`n")}
Write-Output ($taskResult -join "`n")
