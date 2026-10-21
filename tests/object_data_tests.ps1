param([Parameter(Mandatory)][string]$ProjectDirectory)
$ErrorActionPreference='Stop'
. (Join-Path $ProjectDirectory 'tools\object-data.ps1')
$taskPhysics='99999.0, 99999.0 0.99, 0.1, 50.0, 0.0, 1.0, 1, 4, 1, 0, 0, 0.0, 0.0, 0.0, none'
$taskBroken=";header`r`nxref_garagedoor $taskPhysics`r`n*********melee weapons*************`r`nlc1095 $taskPhysics`r`n* ;end of file`r`n"
if(@(Get-NativeObjectDataRows $taskBroken | Where-Object {$_.Name -ieq 'lc1095'}).Count){throw 'Parser must stop at the first star-prefixed line'}
$taskFixed=Set-FirstMissionDoorPhysics $taskBroken
$taskRows=@(Get-NativeObjectDataRows $taskFixed)
if(@($taskRows | Where-Object {$_.Name -ieq 'lc1095'}).Count -ne 1){throw 'Repaired door must be readable to the native parser'}
if($taskFixed.IndexOf('lc1095') -gt $taskFixed.IndexOf('*********melee')){throw 'Door remains after native parser termination'}
if((Set-FirstMissionDoorPhysics $taskFixed) -ne $taskFixed){throw 'Repeated installation must preserve the repaired file'}
$taskUnusable="*stop`r`nxref_garagedoor $taskPhysics`r`n"
$taskRejected=$false
try{Set-FirstMissionDoorPhysics $taskUnusable | Out-Null}catch{$taskRejected=$true}
if(!$taskRejected){throw 'An unreadable physics template must be rejected'}
Write-Output 'PASS: native object.dat termination reproduced; misplaced garage physics repaired and repeated installation preserved'
