param([Parameter(Mandatory)][string]$WorkDirectory)
$ErrorActionPreference='Stop'
$taskProject=Split-Path -Parent $PSScriptRoot
$taskWork=Join-Path ([IO.Path]::GetFullPath($WorkDirectory)) ('navigation-'+[Guid]::NewGuid().ToString('N'))
$taskGood=Join-Path $taskWork 'paths.img'
& (Join-Path $taskProject 'tools\prepare-navigation.ps1') -OutputFile $taskGood|Out-Null
$taskValidator=Join-Path $taskProject 'tools\validate-navigation.ps1'
$taskAudit=(& $taskValidator -ArchiveFile $taskGood)|ConvertFrom-Json
if(!$taskAudit.NativeNavigationValidated -or $taskAudit.Areas -ne 64){throw 'Preview graph must include all native path areas'}
function Expect-Rejection([byte[]]$Bytes,[string]$Name,[string]$Pattern){
    $taskFile=Join-Path $taskWork $Name;[IO.File]::WriteAllBytes($taskFile,$Bytes)
    $taskRejected=$false
    try{& $taskValidator -ArchiveFile $taskFile|Out-Null}catch{if($_.Exception.Message -notmatch $Pattern){throw};$taskRejected=$true}
    if(!$taskRejected){throw "Invalid navigation archive accepted: $Name"}
}
$taskOriginal=[IO.File]::ReadAllBytes($taskGood)
$taskMutant=$taskOriginal.Clone();$taskMutant[4]=63
Expect-Rejection $taskMutant 'missing-area.img' 'all 64 map areas'
$taskMutant=$taskOriginal.Clone();[Array]::Copy($taskMutant,16,$taskMutant,48,24)
Expect-Rejection $taskMutant 'duplicate-area.img' 'Duplicate or invalid'
$taskMutant=$taskOriginal.Clone();$taskOffset=[BitConverter]::ToUInt32($taskMutant,8)*2048;$taskMutant[$taskOffset]=1
Expect-Rejection $taskMutant 'invalid-node-count.img' 'Inconsistent native node counts'
Write-Output 'Navigation regression passed: loaded empty regions, missing region, duplicate region and inconsistent node payload.'
