param([Parameter(Mandatory)][string]$WorkDirectory)
$ErrorActionPreference='Stop'
$taskProject=Split-Path -Parent $PSScriptRoot
$taskWork=Join-Path ([IO.Path]::GetFullPath($WorkDirectory)) ('bootstrap-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskWork -Force|Out-Null
$taskGenerator=Join-Path $taskProject 'tools\make-bootstrap.ps1'
$taskValidator=Join-Path $taskProject 'tools\validate-bootstrap.ps1'
$taskGood=Join-Path $taskWork 'valid.scm'
& $taskGenerator -OutputFile $taskGood|Out-Null
$taskAudit=(& $taskValidator -ScmFile $taskGood)|ConvertFrom-Json
if(!$taskAudit.NativeBootstrapValidated -or !$taskAudit.NativePlayerGlobalsExported -or $taskAudit.InitializedLocals -ne 0 -or $taskAudit.InitializedGlobals -ne 4){throw 'Generated bootstrap did not initialize native mod player exports'}
function Expect-Rejection([string]$File,[string]$Pattern){
    $taskRejected=$false
    try{& $taskValidator -ScmFile $File|Out-Null}catch{
        if($_.Exception.Message -notmatch $Pattern){throw}
        $taskRejected=$true
    }
    if(!$taskRejected){throw "Bootstrap validator accepted invalid script: $File"}
}
# This is the exact installed script that produced the reported 0x465FA1 crash.
Expect-Rejection (Join-Path $PSScriptRoot 'fixtures\bootstrap-unsupported-load-collision.scm') 'Unsupported bootstrap opcode 0x03F7'
# Exact installed script that yielded before creating the camera's player target.
Expect-Rejection (Join-Path $PSScriptRoot 'fixtures\bootstrap-wait-before-player.scm') 'WAIT before native player creation'
$taskOriginal=[IO.File]::ReadAllBytes($taskGood)
$taskMutant=$taskOriginal.Clone();$taskMutant[32]=1
$taskFile=Join-Path $taskWork 'occupied-hud-bank.scm';[IO.File]::WriteAllBytes($taskFile,$taskMutant)
Expect-Rejection $taskFile 'Reserved native campaign HUD bank is occupied'
$taskCreate=$taskAudit.Instructions|Where-Object {$_.Opcode -eq 0x0053}
$taskMutant=$taskOriginal.Clone();$taskMutant[$taskCreate.Parameters[4].Offset]=1
$taskFile=Join-Path $taskWork 'constant-output.scm';[IO.File]::WriteAllBytes($taskFile,$taskMutant)
Expect-Rejection $taskFile 'Invalid bootstrap variable operand'
$taskGetPed=$taskAudit.Instructions|Where-Object {$_.Opcode -eq 0x01F5}
$taskMutant=$taskOriginal.Clone();$taskMutant[$taskGetPed.Parameters[0].Offset+1]=28
$taskFile=Join-Path $taskWork 'uninitialized-player.scm';[IO.File]::WriteAllBytes($taskFile,$taskMutant)
Expect-Rejection $taskFile 'uninitialized handle'
# Exact old private-local bindings: existing mods read globals 8/12 as zero.
$taskMutant=$taskOriginal.Clone()
foreach($taskBinding in @(@(697,0),@(702,0),@(705,1),@(768,1),@(788,0))){
    $taskMutant[$taskBinding[0]]=3;$taskMutant[$taskBinding[0]+1]=$taskBinding[1]
}
$taskFile=Join-Path $taskWork 'missing-mod-player-exports.scm';[IO.File]::WriteAllBytes($taskFile,$taskMutant)
Expect-Rejection $taskFile 'export the initialized native player globals'
$taskJump=$taskAudit.Instructions|Where-Object {$_.Opcode -eq 2}
$taskMutant=$taskOriginal.Clone();$taskDestination=[BitConverter]::GetBytes([int]($taskAudit.LoopOffset+1))
[Array]::Copy($taskDestination,0,$taskMutant,$taskJump.Parameters[0].Offset+1,4)
$taskFile=Join-Path $taskWork 'jump-into-parameter.scm';[IO.File]::WriteAllBytes($taskFile,$taskMutant)
Expect-Rejection $taskFile 'WAIT instruction boundary'
$taskStage=@($taskAudit.Instructions|Where-Object {$_.Opcode -eq 4 -and $_.Parameters[0].Value -eq 20})[0]
$taskMutant=$taskOriginal.Clone();$taskMutant[$taskStage.Parameters[1].Offset+1]=3
$taskFile=Join-Path $taskWork 'incorrect-stage.scm';[IO.File]::WriteAllBytes($taskFile,$taskMutant)
Expect-Rejection $taskFile 'diagnostic stage value is invalid'
Write-Output 'Bootstrap regression passed: native mod player globals before first yield, both old crash scripts, output destination, handle initialization, stage values and loop boundary.'
