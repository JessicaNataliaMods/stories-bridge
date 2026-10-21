param([Parameter(Mandatory)][string]$ScmFile)
$ErrorActionPreference='Stop'
$taskCode=[IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $ScmFile).Path)
$script:taskCursor=0
function Read-Bytes([int]$Count){
    if($Count -lt 1 -or $script:taskCursor+$Count -gt $taskCode.Length){throw 'Truncated bootstrap instruction or parameter'}
    $taskStart=$script:taskCursor;$script:taskCursor+=$Count
    return ,$taskCode[$taskStart..($taskStart+$Count-1)]
}
function Read-U8 {return [int](Read-Bytes 1)[0]}
function Read-U16 {return [BitConverter]::ToUInt16((Read-Bytes 2),0)}
function Read-U32 {return [BitConverter]::ToUInt32((Read-Bytes 4),0)}
function Read-I32 {return [BitConverter]::ToInt32((Read-Bytes 4),0)}
$taskChunks=@{}
foreach($taskIndex in @(115,0,1,2,3,4)){
    $taskStart=$script:taskCursor
    if((Read-U16) -ne 2 -or (Read-U8) -ne 1){throw 'Invalid native SCM header jump'}
    $taskNext=Read-U32
    if((Read-U8) -ne $taskIndex -or $taskNext -le $script:taskCursor -or $taskNext -ge $taskCode.Length){throw 'Invalid native SCM chunk chain'}
    $taskChunks[$taskIndex]=[pscustomobject]@{Start=$taskStart;End=$taskNext}
    $script:taskCursor=$taskNext
}
$taskEntry=$script:taskCursor
$taskGlobalsEnd=$taskChunks[115].End
# The native mission HUD uses only these four cells. They must remain zero and
# outside every bootstrap operand, not overlap code or a mission/player handle.
foreach($taskCell in @(32,36,40,44)){
    if($taskCell+4 -gt $taskGlobalsEnd -or [BitConverter]::ToUInt32($taskCode,$taskCell) -ne 0){throw 'Reserved native campaign HUD bank is occupied'}
}
if([BitConverter]::ToUInt32($taskCode,$taskChunks[1].Start+8) -ne $taskCode.Length){throw 'Native main script size mismatch'}
if([BitConverter]::ToUInt32($taskCode,$taskChunks[1].Start+12) -ne 0 -or
   [BitConverter]::ToUInt32($taskCode,$taskChunks[1].Start+16) -ne 0){throw 'Preview bootstrap must not declare missions'}
# These operand signatures are native SA signatures, independent of the writer.
# i: integer, f: float, h: initialized handle, o: variable output, g: global output,
# s: fixed eight-byte text label. Unimplemented legacy opcodes are rejected.
$taskSignatures=@{0x03A4='s';0x016A='ii';0x00C0='ii';0x01EB='f';0x03DE='f';
    0x0053='ifffo';0x01F5='ho';0x03CB='fff';0x0001='i';0x00A1='hfff';
    0x01B4='hi';0x0373='';0x0004='gi';0x0002='i'}
$taskLocals=@{};$taskGlobals=@{};$taskInstructions=[Collections.Generic.List[object]]::new()
while($script:taskCursor -lt $taskCode.Length){
    $taskStart=$script:taskCursor;$taskOpcode=[int](Read-U16)
    if(!$taskSignatures.ContainsKey($taskOpcode)){throw ('Unsupported bootstrap opcode 0x{0:X4} at {1}' -f $taskOpcode,$taskStart)}
    $taskParams=[Collections.Generic.List[object]]::new();$taskOutputs=@();$taskGlobalOutputs=@()
    foreach($taskKind in $taskSignatures[$taskOpcode].ToCharArray()){
        $taskParameterOffset=$script:taskCursor;$taskType=Read-U8;$taskValue=$null
        if($taskKind -eq 's'){
            if($taskType -ne 9){throw 'SCRIPT_NAME requires a native short string'}
            $taskValue=[Text.Encoding]::ASCII.GetString((Read-Bytes 8)).TrimEnd([char]0)
        }elseif($taskKind -eq 'o' -or $taskKind -eq 'g' -or $taskKind -eq 'h'){
            if($taskType -notin @(2,3) -or ($taskKind -eq 'g' -and $taskType -ne 2)){throw 'Invalid bootstrap variable operand'}
            $taskValue=[int](Read-U16)
            if($taskType -eq 3){
                if($taskValue -ge 32){throw 'Bootstrap local variable outside native thread storage'}
                if($taskKind -eq 'h' -and !$taskLocals.ContainsKey($taskValue)){throw 'Bootstrap uses an uninitialized handle'}
                if($taskKind -eq 'o'){$taskOutputs+=$taskValue}
            }else{
                if($taskValue -lt 8 -or $taskValue+4 -gt $taskGlobalsEnd -or $taskValue%4){throw 'Bootstrap global variable outside reserved storage'}
                if($taskValue -in @(32,36,40,44)){throw 'Bootstrap uses reserved native campaign HUD bank'}
                if($taskKind -eq 'h' -and !$taskGlobals.ContainsKey($taskValue)){throw 'Bootstrap uses an uninitialized handle'}
                if($taskKind -eq 'o' -or $taskKind -eq 'g'){$taskGlobalOutputs+=$taskValue}
            }
        }elseif($taskKind -eq 'f'){
            if($taskType -ne 6){throw 'Bootstrap coordinate or density must be a native float'}
            $taskValue=[BitConverter]::ToSingle((Read-Bytes 4),0)
            if([single]::IsNaN($taskValue) -or [single]::IsInfinity($taskValue)){throw 'Non-finite bootstrap float'}
        }else{
            switch($taskType){
                1 {$taskValue=Read-I32}
                4 {$taskValue=Read-U8;if($taskValue -ge 128){$taskValue-=256}}
                5 {$taskValue=[BitConverter]::ToInt16((Read-Bytes 2),0)}
                default {throw 'Invalid bootstrap integer parameter'}
            }
        }
        $taskParams.Add([pscustomobject]@{Offset=$taskParameterOffset;Kind=[string]$taskKind;Type=$taskType;Value=$taskValue})
    }
    foreach($taskOutput in $taskOutputs){$taskLocals[$taskOutput]=$true}
    foreach($taskOutput in $taskGlobalOutputs){$taskGlobals[$taskOutput]=$true}
    $taskInstructions.Add([pscustomobject]@{Offset=$taskStart;End=$script:taskCursor;Opcode=$taskOpcode;Parameters=$taskParams})
}
if($taskInstructions.Count -lt 2 -or $taskInstructions[0].Opcode -ne 0x03A4){throw 'Bootstrap has no entry script name'}
$taskCreate=@($taskInstructions|Where-Object {$_.Opcode -eq 0x0053})
$taskScene=@($taskInstructions|Where-Object {$_.Opcode -eq 0x03CB})
$taskPosition=@($taskInstructions|Where-Object {$_.Opcode -eq 0x00A1})
if($taskCreate.Count -ne 1 -or $taskScene.Count -ne 1 -or $taskPosition.Count -ne 1 -or
   $taskCreate[0].Offset -ge $taskScene[0].Offset -or $taskScene[0].Offset -ge $taskPosition[0].Offset){throw 'Bootstrap player/scene initialization order is invalid'}
if(@($taskInstructions|Where-Object {$_.Opcode -eq 1 -and $_.Offset -lt $taskCreate[0].Offset}).Count){throw 'WAIT before native player creation would expose the camera to a null player'}
$taskPlayerChar=@($taskInstructions|Where-Object {$_.Opcode -eq 0x01F5})
if($taskCreate[0].Parameters[0].Value -ne 0 -or $taskCreate[0].Parameters[4].Type -ne 2 -or $taskCreate[0].Parameters[4].Value -ne 8 -or
   $taskPlayerChar.Count -ne 1 -or $taskPlayerChar[0].Parameters[0].Type -ne 2 -or $taskPlayerChar[0].Parameters[0].Value -ne 8 -or
   $taskPlayerChar[0].Parameters[1].Type -ne 2 -or $taskPlayerChar[0].Parameters[1].Value -ne 12 -or
   $taskPosition[0].Parameters[0].Type -ne 2 -or $taskPosition[0].Parameters[0].Value -ne 12 -or
   $taskCreate[0].End -ne $taskPlayerChar[0].Offset -or
   @($taskInstructions|Where-Object {$_.Opcode -eq 1 -and $_.Offset -lt $taskPlayerChar[0].End}).Count){throw 'Bootstrap must export the initialized native player globals before yielding to mods'}
$taskPlayerControl=@($taskInstructions|Where-Object {$_.Opcode -eq 0x01B4})
if($taskPlayerControl.Count -ne 1 -or $taskPlayerControl[0].Parameters[0].Type -ne 2 -or $taskPlayerControl[0].Parameters[0].Value -ne 8){throw 'Bootstrap player control uses a different exported player'}
$taskMarker=@($taskInstructions|Where-Object {$_.Opcode -eq 0x0004 -and $_.Parameters[0].Value -eq 16})
if($taskMarker.Count -ne 1 -or $taskMarker[0].Parameters[0].Value -ne 16 -or $taskMarker[0].Parameters[1].Value -ne 0x4C435357 -or $taskMarker[0].Offset -le $taskPosition[0].Offset){throw 'Bootstrap world-ready marker is invalid'}
$taskStages=@($taskInstructions|Where-Object {$_.Opcode -eq 0x0004 -and $_.Parameters[0].Value -eq 20})
if($taskStages.Count -ne 3 -or @($taskInstructions|Where-Object {$_.Opcode -eq 0x0004}).Count -ne 4){throw 'Bootstrap must emit three diagnostic stages and one ready marker'}
$taskOperations=@($taskCreate[0],$taskScene[0],$taskPosition[0])
for($taskI=0;$taskI -lt 3;$taskI++){
    $taskStage=$taskStages[$taskI]
    if($taskStage.Parameters[1].Value -ne $taskI+1){throw 'Bootstrap diagnostic stage value is invalid'}
    if($taskI -eq 0){
        if($taskStage.End -ne $taskCreate[0].Offset){throw 'Bootstrap diagnostic stage must immediately precede player creation without yielding'}
        continue
    }
    $taskWaitAfter=@($taskInstructions|Where-Object {$_.Offset -eq $taskStage.End})
    if($taskWaitAfter.Count -ne 1 -or $taskWaitAfter[0].Opcode -ne 1 -or $taskWaitAfter[0].Parameters[0].Value -ne 0 -or $taskWaitAfter[0].End -ne $taskOperations[$taskI].Offset){throw 'Bootstrap diagnostic stage must yield immediately before its native operation'}
}
$taskJump=$taskInstructions[$taskInstructions.Count-1]
$taskWait=$taskInstructions[$taskInstructions.Count-2]
if($taskJump.Opcode -ne 2 -or $taskWait.Opcode -ne 1 -or $taskWait.Parameters[0].Value -ne 0 -or $taskJump.Parameters[0].Value -ne $taskWait.Offset){throw 'Bootstrap loop must return to a WAIT instruction boundary'}
if(@($taskInstructions|Where-Object {$_.Opcode -eq 2}).Count -ne 1){throw 'Unexpected bootstrap control flow'}
[pscustomobject]@{NativeBootstrapValidated=$true;NativePlayerGlobalsExported=$true;PlayerIndexGlobal=8;PlayerActorGlobal=12;Entry=$taskEntry;Bytes=$taskCode.Length;Commands=$taskInstructions.Count;InitializedLocals=$taskLocals.Count;InitializedGlobals=$taskGlobals.Count;LoopOffset=$taskWait.Offset;Instructions=$taskInstructions;GameExecuted=$false}|ConvertTo-Json -Depth 6
