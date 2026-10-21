param([Parameter(Mandatory)][string]$ProjectDirectory)
$ErrorActionPreference='Stop'
$taskDatabase=Get-Content -LiteralPath (Join-Path $ProjectDirectory 'vendor\sa-reference\sa-commands.json') -Raw | ConvertFrom-Json
$taskCommands=@{}
foreach($taskExtension in $taskDatabase.extensions) {foreach($taskCommand in $taskExtension.commands) {
    if(!$taskCommands.ContainsKey($taskCommand.id)) {$taskCommands[$taskCommand.id]=$taskCommand}
}}
$taskSource=[IO.File]::ReadAllText((Join-Path $ProjectDirectory 'src\native.cpp'))
$taskSource+=[IO.File]::ReadAllText((Join-Path $ProjectDirectory 'src\native_common.inc'))
$taskPatterns=[regex]::Matches($taskSource,'\{(COMMAND_[A-Z0-9_]+),0x([A-Fa-f0-9]+),([^{}]+)\}')
$taskCount=0
foreach($taskMatch in $taskPatterns) {
    $taskName=$taskMatch.Groups[1].Value
    $taskFields=$taskMatch.Groups[3].Value.Split(',') | ForEach-Object {$_.Trim()}
    if($taskFields.Count -lt 5){throw 'Malformed adapter specification'}
    $taskId=([Convert]::ToUInt16($taskMatch.Groups[2].Value,16)).ToString('X4')
    if($taskId -eq '0000') {
        if($taskName -notin @('COMMAND_GET_PLAYER_COORDINATES','COMMAND_SET_PLAYER_COORDINATES')) {throw "Unexpected composite adapter: $taskName"}
        continue
    }
    if(!$taskCommands.ContainsKey($taskId)) {throw "Unknown native command $taskId for $taskName"}
    $taskCommand=$taskCommands[$taskId]
    if($taskCommand.attrs.is_unsupported -or $taskCommand.attrs.is_nop) {throw "Native command $taskId is unsupported/NOP: $taskName"}
    $taskInputs=[int]$taskFields[0]
    $taskStringPosition=if($taskFields.Count -gt 5){[int]$taskFields[5]}else{-1}
    $taskGaragePosition=if($taskFields.Count -gt 9){[int]$taskFields[9]}else{-1}
    if($taskStringPosition -ge 0){$taskInputs++}
    if($taskGaragePosition -ge 0){
        if($taskStringPosition -ge 0){throw 'A native packet cannot contain both a garage name and another text operand'}
        $taskStringPosition=$taskGaragePosition
    }
    # These consume the original LCS operands and construct a larger SA task.
    # Native packet values and the extra ground query are covered by C++ tests.
    if($taskName -in @('COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT','COMMAND_SET_CHAR_OBJ_RUN_TO_COORD','COMMAND_SET_CHAR_OBJ_SPRINT_TO_COORD')) {
        if($taskId -ne '05D3' -or $taskInputs -ne 3){throw 'Unexpected walking composite adapter'}
        $taskInputs=6
    }
    if($taskName -eq 'COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER') {
        if($taskId -ne '05CA' -or $taskInputs -ne 2){throw 'Unexpected passenger composite adapter'}
        $taskInputs=4
    }
    if($taskName -eq 'COMMAND_GET_PAD_BUTTON_STATE') {
        if($taskId -ne '00E2' -or $taskInputs -ne 1){throw 'Unexpected LCS pad adapter'}
        $taskInputs=2
    }
    if($taskName -in @('COMMAND_SET_CHAR_OBJ_WAIT_ON_FOOT','COMMAND_CHAR_SET_IDLE')) {
        if($taskId -ne '05BA' -or $taskInputs -ne 1){throw 'Unexpected waiting composite adapter'}
        $taskInputs=2
    }
    if($taskName -eq 'COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER') {
        if($taskId -ne '05CB' -or $taskInputs -ne 2){throw 'Unexpected driver composite adapter'}
        $taskInputs=3
    }
    if($taskName -in @('COMMAND_SET_CHAR_OBJ_GOTO_CHAR_ON_FOOT','COMMAND_SET_CHAR_OBJ_GOTO_PLAYER_ON_FOOT','COMMAND_SET_CHAR_OBJ_WALK_TO_CHAR')) {
        if($taskId -ne '05D9' -or $taskInputs -ne 2){throw 'Unexpected character navigation composite adapter'}
        $taskInputs=4
    }
    if($taskName -in @('COMMAND_SET_CHAR_OBJ_FLEE_CHAR_ON_FOOT_ALWAYS','COMMAND_SET_CHAR_OBJ_FLEE_PLAYER_ON_FOOT_ALWAYS')) {
        if($taskId -ne '05DB' -or $taskInputs -ne 2){throw 'Unexpected character fleeing composite adapter'}
        $taskInputs=4
    }
    if($taskName -in @('COMMAND_CHAR_LOOK_AT_CHAR_ALWAYS','COMMAND_PLAYER_LOOK_AT_CHAR_ALWAYS','COMMAND_CHAR_LOOK_AT_PLAYER_ALWAYS')) {
        if($taskId -ne '05BF' -or $taskInputs -ne 2){throw 'Unexpected look-at adapter'}
        $taskInputs=3
    }
    if($taskName -eq 'COMMAND_SET_CHAR_OBJ_AIM_GUN_AT_CHAR') {
        if($taskId -ne '0635' -or $taskInputs -ne 2){throw 'Unexpected aiming composite adapter'}
        $taskInputs=3
    }
    if($taskName -eq 'COMMAND_MAKE_OBJECT_TARGETTABLE') {
        if($taskId -ne '035D' -or $taskInputs -ne 1){throw 'Unexpected targettable object adapter'}
        $taskInputs=2
    }
    $taskExpectedInputs=if($taskCommand.input){$taskCommand.input.Count}else{0}
    if($taskName -eq 'COMMAND_START_SCRIPT_FIRE') {
        if($taskId -ne '02CF' -or $taskInputs -ne 3){throw 'Unexpected original fire adapter'}
        $taskInputs=5
    }
    if($taskName -eq 'COMMAND_RESET_NUM_OF_MODELS_KILLED_BY_PLAYER') {
        if($taskId -ne '0297' -or $taskInputs -ne 0){throw 'Unexpected original kill-bank reset adapter'}
        $taskInputs=1
    }
    if($taskName -eq 'COMMAND_GET_NUM_OF_MODELS_KILLED_BY_PLAYER') {
        if($taskId -ne '0298' -or $taskInputs -ne 1){throw 'Unexpected original kill-bank query adapter'}
        $taskInputs=2
    }
    if($taskName -in @('COMMAND_ADD_HOSPITAL_RESTART','COMMAND_ADD_POLICE_RESTART')) {
        if($taskId -notin @('016C','016D') -or $taskInputs -ne 4){throw 'Unexpected original restart declaration'}
        $taskInputs=5
    }
    $taskExpectedOutputs=if($taskCommand.output){$taskCommand.output.Count}else{0}
    if($taskInputs -ne $taskExpectedInputs -or [int]$taskFields[1] -ne $taskExpectedOutputs) {
        throw "Parameter signature mismatch $taskName -> $taskId"
    }
    if(($taskFields[3] -eq 'true') -ne [bool]$taskCommand.attrs.is_condition) {throw "Predicate signature mismatch $taskName -> $taskId"}
    for($taskPosition=0;$taskPosition -lt $taskExpectedInputs;$taskPosition++) {
        $taskNativeIsString=$taskCommand.input[$taskPosition].type -in @('string','gxt_key','GarageName')
        if($taskNativeIsString -ne ($taskStringPosition -eq $taskPosition)) {
            throw "Operand type mismatch $taskName -> $taskId at input $taskPosition : expected $($taskCommand.input[$taskPosition].type)"
        }
    }
    if($taskFields.Count -gt 6 -and $taskFields[6] -eq 'true' -and $taskCommand.input[0].type -ne 'Char') {
        throw "Player-to-ped adapter $taskName does not target a native Char operand"
    }
    $taskCount++
}
if($taskCount -lt 70) {throw 'Native adapter scan was unexpectedly incomplete'}
foreach($taskId in @('0623','0629')) {
    $taskCommand=$taskCommands[$taskId]
    if($taskCommand.attrs.is_unsupported -or $taskCommand.attrs.is_nop -or $taskCommand.attrs.is_condition -or
       $taskCommand.input.Count -ne 2 -or $taskCommand.output.Count -or
       $taskCommand.input[0].type -ne 'StatId' -or $taskCommand.input[1].type -ne 'int') {
        throw 'Native statistic composite packet differs from the independent primary signature'
    }
}
if($taskCommands['0384'].attrs.is_unsupported -or $taskCommands['0384'].input.Count -ne 4 -or
   $taskCommands['0384'].input[0].type -ne 'gxt_key' -or $taskCommands['0384'].input[1].type -ne 'gxt_key' -or
   $taskCommands['0384'].input[2].type -ne 'int' -or $taskCommands['0384'].output.Count){throw 'Native template/replacement message differs from the primary signature'}
foreach($taskPair in @(@('0627','int'),@('0628','float'))) {
    $taskCommand=$taskCommands[$taskPair[0]]
    if($taskCommand.attrs.is_unsupported -or $taskCommand.attrs.is_nop -or $taskCommand.attrs.is_condition -or
       $taskCommand.input.Count -ne 2 -or $taskCommand.input[0].type -ne 'StatId' -or
       $taskCommand.input[1].type -ne $taskPair[1] -or $taskCommand.output.Count){throw 'Native jump record must preserve distinct integer/float operands'}
}
foreach($taskPacket in @(@('062A',@('StatId','float'),@()),@('0653',@('StatId'),@('float')))) {
    $taskCommand=$taskCommands[$taskPacket[0]]
    $taskOutputs=if($taskCommand.output){$taskCommand.output.Count}else{0}
    if($taskCommand.attrs.is_unsupported -or $taskCommand.attrs.is_nop -or $taskCommand.attrs.is_condition -or
       $taskCommand.input.Count -ne $taskPacket[1].Count -or $taskOutputs -ne $taskPacket[2].Count){throw 'Native weapon expenditure signature differs'}
    for($taskI=0;$taskI -lt $taskPacket[1].Count;$taskI++){if($taskCommand.input[$taskI].type -ne $taskPacket[1][$taskI]){throw 'Native expenditure input types differ'}}
    for($taskI=0;$taskI -lt $taskPacket[2].Count;$taskI++){if($taskCommand.output[$taskI].type -ne $taskPacket[2][$taskI]){throw 'Native expenditure output types differ'}}
}
foreach($taskPacket in @(@('0494',@('PadId'),@('int','int','int','int')),@('0454',@(),@('float','float','float')),@('0506',@('model_vehicle','int','int'),@()),@('035D',@('Object','bool'),@()))) {
    $taskCommand=$taskCommands[$taskPacket[0]]
    $taskInputs=if($taskCommand.input){$taskCommand.input.Count}else{0}
    $taskOutputs=if($taskCommand.output){$taskCommand.output.Count}else{0}
    if($taskCommand.attrs.is_unsupported -or $taskCommand.attrs.is_nop -or $taskCommand.attrs.is_condition -or
       $taskInputs -ne $taskPacket[1].Count -or $taskOutputs -ne $taskPacket[2].Count){throw 'Native controller/camera/components/targettable signature differs'}
    for($taskI=0;$taskI -lt $taskInputs;$taskI++){if($taskCommand.input[$taskI].type -ne $taskPacket[1][$taskI]){throw ('Native shared input type differs: '+$taskPacket[0])}}
    for($taskI=0;$taskI -lt $taskOutputs;$taskI++){if($taskCommand.output[$taskI].type -ne $taskPacket[2][$taskI]){throw ('Native shared output type differs: '+$taskPacket[0])}}
}
foreach($taskPacket in @(@('092B',@('Group','int'),@('Char')),@('04D7',@('Char','bool'),@()))) {
    $taskCommand=$taskCommands[$taskPacket[0]]
    $taskOutputs=if($taskCommand.output){$taskCommand.output.Count}else{0}
    if($taskCommand.attrs.is_unsupported -or $taskCommand.attrs.is_nop -or $taskCommand.attrs.is_condition -or
       $taskCommand.input.Count -ne $taskPacket[1].Count -or $taskOutputs -ne $taskPacket[2].Count){throw 'Native follower enumeration/freeze packet has an invalid signature'}
    for($taskI=0;$taskI -lt $taskPacket[1].Count;$taskI++){if($taskCommand.input[$taskI].type -ne $taskPacket[1][$taskI]){throw 'Native follower input types differ'}}
    for($taskI=0;$taskI -lt $taskPacket[2].Count;$taskI++){if($taskCommand.output[$taskI].type -ne $taskPacket[2][$taskI]){throw 'Native follower output types differ'}}
}
foreach($taskId in @('04EE','04EF')) {
    $taskCommand=$taskCommands[$taskId]
    if($taskCommand.attrs.is_unsupported -or $taskCommand.attrs.is_nop -or $taskCommand.input.Count -ne 1 -or
       $taskCommand.input[0].type -ne 'string' -or $taskCommand.output.Count -or
       [bool]$taskCommand.attrs.is_condition -ne ($taskId -eq '04EE')){throw 'Native named animation residency/removal signature differs'}
}
Write-Output "PASS: $taskCount adapters checked against independent SA counts, predicates, text/numeric operand types and player/ped translations; unsupported and NOP commands rejected"
foreach($taskPacket in @(@('0940',@('Group','bool'),@(),$false),@('062E',@('Char','TaskCommand'),@('TaskStatus'),$false),@('01EA',@('Car'),@('int'),$false),@('046C',@('Car'),@('Char'),$false),@('0431',@('Car','SeatId'),@(),$true),@('05F5',@('Char','float','float','float','MoveState','int'),@(),$false))) {
    $taskCommand=$taskCommands[$taskPacket[0]]
    $taskOutputs=if($taskCommand.output){$taskCommand.output.Count}else{0}
    if($taskCommand.attrs.is_unsupported -or $taskCommand.attrs.is_nop -or [bool]$taskCommand.attrs.is_condition -ne $taskPacket[3] -or
       $taskCommand.input.Count -ne $taskPacket[1].Count -or $taskOutputs -ne $taskPacket[2].Count){throw ('Native transport/navigation packet has invalid signature: '+$taskPacket[0])}
    for($taskI=0;$taskI -lt $taskPacket[1].Count;$taskI++){if($taskCommand.input[$taskI].type -ne $taskPacket[1][$taskI]){throw ('Native transport input type differs: '+$taskPacket[0])}}
    for($taskI=0;$taskI -lt $taskPacket[2].Count;$taskI++){if($taskCommand.output[$taskI].type -ne $taskPacket[2][$taskI]){throw ('Native transport output type differs: '+$taskPacket[0])}}
}
