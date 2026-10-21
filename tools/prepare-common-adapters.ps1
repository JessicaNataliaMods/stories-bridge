param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskReverse=Join-Path $taskRoot '..\res-relcs\src\control'
$taskDatabase=Get-Content -LiteralPath (Join-Path $taskRoot 'vendor\sa-reference\sa-commands.json') -Raw | ConvertFrom-Json
$taskCommands=@{}
foreach($taskCommand in $taskDatabase.extensions[0].commands){$taskCommands[$taskCommand.name]=$taskCommand}
$taskDebug=Get-Content -LiteralPath (Join-Path $taskReverse 'ScriptDebug.cpp') -Raw
$taskSpecs=@{}
foreach($taskMatch in [regex]::Matches($taskDebug,'REGISTER_COMMAND\((COMMAND_[A-Z0-9_]+), INPUT_ARGUMENTS\(([^)]*)\), OUTPUT_ARGUMENTS\(([^)]*)\), (true|false)')) {
    $taskSpecs[$taskMatch.Groups[1].Value]=@([regex]::Matches($taskMatch.Groups[2].Value,'ARGTYPE_').Count,[regex]::Matches($taskMatch.Groups[3].Value,'ARGTYPE_').Count,($taskMatch.Groups[4].Value -eq 'true'))
}
# ScriptDebug incorrectly labels this getter as a predicate. The executable
# source in Script6.cpp only collects one handle and stores two colours.
$taskColoursSource=[regex]::Match([IO.File]::ReadAllText((Join-Path $taskReverse 'Script6.cpp')),'(?s)case COMMAND_GET_CAR_COLOURS:(.*?)case COMMAND_SET_ALL_CARS_CAN_BE_DAMAGED:').Groups[1].Value
if($taskColoursSource -notmatch 'CollectParameters\(&m_nIp, 1\)' -or $taskColoursSource -notmatch 'StoreParameters\(&m_nIp, 2\)' -or $taskColoursSource -match 'UpdateCompareFlag'){throw 'Original car colour getter changed'}
$taskSpecs['COMMAND_GET_CAR_COLOURS']=@(1,2,$false)
# Original Script7 only collects the flag and changes the remote-contact
# detonation setting; the debug inventory incorrectly calls it a predicate.
$taskRcSource=[regex]::Match([IO.File]::ReadAllText((Join-Path $taskReverse 'Script7.cpp')),'(?s)case COMMAND_SET_ENABLE_RC_DETONATE_ON_CONTACT:(.*?)(?=\r?\n\tcase COMMAND_)').Groups[1].Value
if($taskRcSource -notmatch 'bDisableRemoteDetonationOnContact' -or $taskRcSource -notmatch 'CollectParameters\(&m_nIp, 1\)' -or $taskRcSource -match 'UpdateCompareFlag'){throw 'Original RC contact detonation handler changed'}
$taskSpecs['COMMAND_SET_ENABLE_RC_DETONATE_ON_CONTACT']=@(1,0,$false)
# Reviewed families using native entity handles, numeric arguments, boolean
# flags and world coordinates. No IDs for LCS weapons/models/zones/audio,
# script-variable pointers, mission progress or saves pass through this list.
$taskAllow=@'
DOES_CHAR_EXIST DOES_OBJECT_EXIST
SET_CHAR_STAY_IN_SAME_PLACE SET_CHAR_ONLY_DAMAGED_BY_PLAYER SET_CHAR_ACCURACY
SET_CHAR_SUFFERS_CRITICAL_HITS SET_CHAR_NEVER_TARGETTED SET_CHAR_CAN_BE_SHOT_IN_VEHICLE
SET_CHAR_MAX_HEALTH SET_CHAR_CANT_BE_DRAGGED_OUT SET_CHAR_MONEY SET_CHAR_DROWNS_IN_WATER
START_CHAR_FIRE START_CAR_FIRE
SET_CHAR_BLEEDING SET_LOAD_COLLISION_FOR_CHAR_FLAG FREEZE_CHAR_POSITION
DONT_REMOVE_CHAR REMOVE_CHAR_ELEGANTLY CLEAR_CHAR_LAST_WEAPON_DAMAGE CLEAR_CHAR_LAST_DAMAGE_ENTITY
ADD_ARMOUR_TO_CHAR GET_CHAR_ARMOUR SHUT_CHAR_UP
HAS_CHAR_BEEN_DAMAGED_BY_CHAR HAS_CHAR_BEEN_DAMAGED_BY_CAR HAS_CAR_BEEN_DAMAGED_BY_CHAR HAS_CAR_BEEN_DAMAGED_BY_CAR
IS_CHAR_IN_AREA_2D IS_CHAR_IN_AREA_3D IS_CHAR_ON_SCREEN IS_CHAR_ON_FOOT IS_CHAR_IN_WATER
IS_CHAR_ON_ANY_BIKE IS_CHAR_MALE IS_CHAR_STOPPED IS_CHAR_DUCKING IS_CHAR_SHOOTING
IS_CHAR_SHOOTING_IN_AREA
IS_CHAR_SITTING_IN_CAR IS_CHAR_SITTING_IN_ANY_CAR
SET_CAR_ONLY_DAMAGED_BY_PLAYER SET_CAN_BURST_CAR_TYRES SET_CAR_CAN_BE_DAMAGED
CAR_WANDER_RANDOMLY SET_CAR_HEAVY EXPLODE_CAR IS_CAR_STUCK_ON_ROOF
ADD_UPSIDEDOWN_CAR_CHECK REMOVE_UPSIDEDOWN_CAR_CHECK ADD_STUCK_CAR_CHECK REMOVE_STUCK_CAR_CHECK IS_CAR_STUCK
TURN_CAR_TO_FACE_COORD STORE_CAR_CHAR_IS_IN_NO_SAVE GET_CAR_COLOURS SET_ALL_CARS_CAN_BE_DAMAGED SET_CAR_TRACTION
SET_CAR_AVOID_LEVEL_TRANSITIONS SET_CAR_RANDOM_ROUTE_SEED MARK_CAR_AS_CONVOY_CAR POP_CAR_BOOT
WARP_CHAR_FROM_CAR_TO_COORD HAS_CHAR_SPOTTED_CHAR DETACH_CHAR_FROM_CAR REMOVE_ALL_CHAR_WEAPONS
GET_NUMBER_OF_FOLLOWERS GET_DEAD_CHAR_PICKUP_COORDS EXPLODE_CHAR_HEAD
SET_CAR_STRONG SET_UPSIDEDOWN_CAR_NOT_DAMAGED SET_CAR_WATERTIGHT SET_CAR_VISIBLE
SET_LOAD_COLLISION_FOR_CAR_FLAG FREEZE_CAR_POSITION FREEZE_CAR_POSITION_AND_DONT_LOAD_COLLISION
IS_CAR_ON_SCREEN IS_CAR_ON_FIRE IS_CAR_UPRIGHT IS_CAR_IN_AIR_PROPER IS_CAR_VISIBLY_DAMAGED
IS_CAR_TYRE_BURST
IS_CAR_HEALTH_GREATER IS_CHAR_HEALTH_GREATER IS_CAR_STOPPED_IN_AREA_2D
GET_CAR_SPEED GET_NUMBER_OF_PASSENGERS GET_MAXIMUM_NUMBER_OF_PASSENGERS
CLOSE_ALL_CAR_DOORS SET_CAN_RESPRAY_CAR SET_TAXI_LIGHTS SWITCH_CAR_SIREN
CLEAR_CAR_LAST_DAMAGE_ENTITY CLEAR_CAR_LAST_WEAPON_DAMAGE CAR_SET_IDLE
GET_CAR_FORWARD_X GET_CAR_FORWARD_Y GET_CHAR_IN_CAR_PASSENGER_SEAT IS_CAR_PASSENGER_SEAT_FREE
FREEZE_OBJECT_POSITION MARK_OBJECT_AS_NO_LONGER_NEEDED DONT_REMOVE_OBJECT SET_OBJECT_COLLISION
SET_OBJECT_ROTATION SET_OBJECT_DYNAMIC SET_OBJECT_PROOFS SET_OBJECT_VELOCITY ADD_TO_OBJECT_VELOCITY
GET_OBJECT_COORDINATES IS_OBJECT_ON_SCREEN HAS_OBJECT_BEEN_DAMAGED CLEAR_OBJECT_LAST_WEAPON_DAMAGE
ROTATE_OBJECT SLIDE_OBJECT IS_AREA_OCCUPIED
PLACE_OBJECT_RELATIVE_TO_CAR SORT_OUT_OBJECT_COLLISION_WITH_CAR
GET_OFFSET_FROM_CAR_IN_WORLD_COORDS GET_OFFSET_FROM_CHAR_IN_WORLD_COORDS GET_OFFSET_FROM_OBJECT_IN_WORLD_COORDS
GET_GROUND_Z_FOR_3D_COORD IS_POINT_ON_SCREEN IS_POINT_OBSCURED_BY_A_MISSION_ENTITY
LOCATE_CHAR_ON_FOOT_2D LOCATE_CHAR_IN_CAR_2D LOCATE_STOPPED_CAR_3D
LOCATE_STOPPED_CHAR_ANY_MEANS_2D LOCATE_STOPPED_CHAR_ON_FOOT_2D LOCATE_STOPPED_CHAR_IN_CAR_2D
LOCATE_STOPPED_CHAR_ANY_MEANS_3D LOCATE_STOPPED_CHAR_ON_FOOT_3D LOCATE_STOPPED_CHAR_IN_CAR_3D
LOCATE_CHAR_ANY_MEANS_CHAR_2D LOCATE_CHAR_ON_FOOT_CHAR_2D LOCATE_CHAR_ANY_MEANS_CHAR_3D LOCATE_CHAR_ON_FOOT_CHAR_3D
LOCATE_CHAR_ANY_MEANS_CAR_2D LOCATE_CHAR_ON_FOOT_CAR_2D LOCATE_CHAR_IN_CAR_CAR_2D
LOCATE_CHAR_ANY_MEANS_CAR_3D LOCATE_CHAR_ON_FOOT_CAR_3D
LOCATE_CHAR_ON_FOOT_OBJECT_2D
CHANGE_BLIP_SCALE ADD_BLIP_FOR_PICKUP ADD_BLIP_FOR_OBJECT
SET_CAMERA_IN_FRONT_OF_PLAYER SHAKE_CAM REQUEST_COLLISION CLEAR_AREA_OF_CARS CLEAR_AREA_OF_CHARS
GET_DISTANCE_BETWEEN_COORDS_2D GET_DISTANCE_BETWEEN_COORDS_3D GENERATE_RANDOM_INT_IN_RANGE GENERATE_RANDOM_FLOAT_IN_RANGE
GET_MINUTES_TO_TIME_OF_DAY SET_TIME_SCALE SQRT SIN COS CONVERT_METRES_TO_FEET CONVERT_METRES_TO_FEET_INT
ARE_MEASUREMENTS_IN_METRES
BOAT_GOTO_COORDS BOAT_STOP SET_BOAT_CRUISE_SPEED ANCHOR_BOAT
REMOVE_ALL_SCRIPT_FIRES IS_SCRIPT_FIRE_EXTINGUISHED REMOVE_SCRIPT_FIRE
DRAW_SPHERE SET_CAMERA_ZOOM
SET_HELI_ORIENTATION SET_HELI_STABILISER
SET_ENABLE_RC_DETONATE SET_ENABLE_RC_DETONATE_ON_CONTACT REMOVE_RC_BUGGY
CREATE_RANDOM_CHAR_AS_DRIVER CREATE_RANDOM_CHAR_AS_PASSENGER
SET_CHAR_IS_CHRIS_CRIMINAL
CREATE_RANDOM_CHAR CREATE_RANDOM_CAR_FOR_CAR_PARK SET_MUSIC_DOES_FADE
GET_CLOSEST_CAR_NODE GET_NTH_CLOSEST_CAR_NODE GET_CLOSEST_CAR_NODE_WITH_HEADING GET_CLOSEST_CHAR_NODE
GENERATE_RANDOM_INT REGISTER_ODDJOB_MISSION_PASSED HAS_CHAR_BEEN_PHOTOGRAPHED
'@ -split '\s+' | Where-Object {$_}
$taskSource=Get-Content -LiteralPath (Join-Path $taskRoot 'src\native.cpp') -Raw
$taskExisting=@{}
foreach($taskMatch in [regex]::Matches($taskSource,'\{(COMMAND_[A-Z0-9_]+),0x')){$taskExisting[$taskMatch.Groups[1].Value]=$true}
$taskRows=[Collections.Generic.List[string]]::new()
$taskRows.Add('// Reviewed native entity/physics/query families. Generated by tools/prepare-common-adapters.ps1.')
foreach($taskName in $taskAllow) {
    $taskOriginal='COMMAND_'+$taskName
    if($taskExisting.ContainsKey($taskOriginal)){throw "Duplicate common adapter: $taskOriginal"}
    if(!$taskCommands.ContainsKey($taskName) -or !$taskSpecs.ContainsKey($taskOriginal)){throw "Missing primary signature: $taskName"}
    $taskCommand=$taskCommands[$taskName];$taskSpec=$taskSpecs[$taskOriginal]
    if($taskCommand.attrs.is_unsupported -or $taskCommand.attrs.is_nop){throw "Unsupported native handler: $taskName"}
    if($taskCommand.input.Count -ne $taskSpec[0] -or $taskCommand.output.Count -ne $taskSpec[1] -or [bool]$taskCommand.attrs.is_condition -ne $taskSpec[2]){throw "Original/native signature differs: $taskName"}
    foreach($taskInput in $taskCommand.input) {
        if($taskInput.type -notin @('Char','Car','Boat','Heli','Object','Pickup','ScriptFire','Blip','SeatId','WheelId','any','int','float','bool')){throw "Operand requires a specific adapter: $taskName / $($taskInput.type)"}
    }
    $taskWorld=if($taskName -match '^(GET_DISTANCE_BETWEEN_COORDS|GENERATE_RANDOM_|GET_MINUTES_TO_TIME_OF_DAY|SET_TIME_SCALE|SQRT$|SIN$|COS$|CONVERT_METRES_TO_FEET|REGISTER_ODDJOB_MISSION_PASSED$)'){'false'}else{'true'}
    $taskPredicate=$taskSpec[2].ToString().ToLowerInvariant()
    $taskRows.Add("{${taskOriginal},0x$($taskCommand.id),$($taskSpec[0]),$($taskSpec[1]),-1,$taskPredicate,$taskWorld},")
}
$taskText=($taskRows -join "`n")+"`n"
$taskTarget=Join-Path $taskRoot 'src\native_common.inc'
if($VerifyOnly) {
    if([IO.File]::ReadAllText($taskTarget) -ne $taskText){throw 'Common adapter source is stale or changed without primary signature validation'}
}else{[IO.File]::WriteAllText($taskTarget,$taskText,[Text.UTF8Encoding]::new($false))}
Write-Output "PASS: $($taskAllow.Count) common adapters validated against original LCS and native SA signatures; no game execution"
