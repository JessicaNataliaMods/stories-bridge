param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProjectDirectory)
$ErrorActionPreference='Stop'
# Parse native code as file data. No module is loaded and no native routine runs.
$taskBytes=[IO.File]::ReadAllBytes($Executable)
$taskNt=[BitConverter]::ToInt32($taskBytes,60)
$taskSections=$taskNt+24+[BitConverter]::ToUInt16($taskBytes,$taskNt+20)
$taskFound=$false
for($taskI=0;$taskI -lt [BitConverter]::ToUInt16($taskBytes,$taskNt+6);$taskI++){
    $taskAt=$taskSections+$taskI*40
    $taskRva=[BitConverter]::ToUInt32($taskBytes,$taskAt+12)
    $taskSize=[BitConverter]::ToUInt32($taskBytes,$taskAt+16)
    if(0x17B474 -lt $taskRva -or 0x17B47C -gt $taskRva+$taskSize){continue}
    $taskRaw=[BitConverter]::ToUInt32($taskBytes,$taskAt+20)+0x17B474-$taskRva
    $taskActual=[BitConverter]::ToString($taskBytes,$taskRaw,8)
    if($taskActual -ne '8B-CE-5E-E9-F4-B6-FF-FF'){throw 'Native pause tail is not the verified thiscall transfer'}
    $taskFound=$true
}
if(!$taskFound){throw 'Native pause transfer not found'}
function Read-NativeBytes([uint32]$taskAddress,[int]$taskLength) {
    $taskOffset=$taskAddress-0x400000
    for($taskIndex=0;$taskIndex -lt [BitConverter]::ToUInt16($taskBytes,$taskNt+6);$taskIndex++){
        $taskHeader=$taskSections+$taskIndex*40
        $taskStart=[BitConverter]::ToUInt32($taskBytes,$taskHeader+12)
        $taskLengthOnDisk=[BitConverter]::ToUInt32($taskBytes,$taskHeader+16)
        if($taskOffset -ge $taskStart -and $taskOffset+$taskLength -le $taskStart+$taskLengthOnDisk){
            return [BitConverter]::ToString($taskBytes,[BitConverter]::ToUInt32($taskBytes,$taskHeader+20)+$taskOffset-$taskStart,$taskLength)
        }
    }
    throw 'Native garage address is outside executable data'
}
foreach($taskSeekTable in @(@(0x859DF8,0x46ACE0,0x46F6A0),@(0x85A388,0x493800,0x496600),@(0x86FBB4,0x661EA0,0x665600),@(0x87004C,0x66CB20,0x6733E0))) {
    if((Read-NativeBytes ($taskSeekTable[0]+16) 4) -ne [BitConverter]::ToString([BitConverter]::GetBytes([uint32]$taskSeekTable[1])) -or
       (Read-NativeBytes ($taskSeekTable[0]+36) 4) -ne [BitConverter]::ToString([BitConverter]::GetBytes([uint32]$taskSeekTable[2])) -or
       (Read-NativeBytes $taskSeekTable[1] 6) -ne 'B8-8B-03-00-00-C3'){throw 'Native seek specialization type/first-subtask slot differs'}
}
foreach($taskProfile in @(
    @(0x73C252,14,'C6-05-C0-A7-C8-00-01-C6-05-C1-A7-C8-00-01'),
    @(0x73A31B,10,'A2-C0-A7-C8-00-A2-C1-A7-C8-00'),
    @(0x489410,14,'8A-0D-7C-E9-C6-00-33-C0-84-C9-0F-94-C0-C3'),
    @(0x53FD30,10,'66-83-B9-0E-01-00-00-00-75-18'),
    @(0x50BFF0,7,'66-8B-81-30-08-00-00'),
    @(0x5E873A,7,'F6-86-72-04-00-00-01'),
    @(0x56E210,28,'8B-44-24-04-85-C0-7D-07-0F-B6-05-74-CD-B7-00-69-C0-90-01-00-00-8B-80-98-CD-B7-00-C3'),
    @(0x43CE90,18,'0F-BE-81-18-07-00-00-6B-C0-1C-8D-84-08-A0-05-00-00-C3'),
    @(0x6B1600,40,'6A-FF-68-78-81-84-00-64-A1-00-00-00-00-50-64-89-25-00-00-00-00-83-EC-58-53-55-56-57-8B-7C-24-78-8B-F1-8D-8E-A0-05-00-00'),
    @(0x6B17E5,13,'80-FB-04-75-4A-8A-44-24-7C-84-C0-75-2F'),
    @(0x6C2709,6,'B8-11-00-00-00-C3'),
    @(0x6C272C,4,'09-27-6C-00'),
    @(0x421339,7,'C6-05-28-5B-8A-00-01'),
    @(0x421346,6,'88-1D-C1-90-96-00'),
    @(0x53FB70,16,'8B-44-24-04-69-C0-34-01-00-00-05-58-34-B7-00-C3'),
    @(0x53F910,10,'8B-44-24-04-89-41-74-C2-04-00'),
    @(0x4654B0,5,'E9-5B-93-10-01'),
    @(0x156E810,41,'51-55-56-8B-E9-57-8D-7D-04-C7-44-24-0C-4B-00-00-00-8A-47-FC-3A-44-24-18-0F-85-AB-00-00-00-8B-0F-3B-4C-24-14-0F-85-9F-00-00'),
    @(0x6AF145,7,'F6-87-68-08-00-00-20'),
    @(0x401860,10,'0F-B6-44-01-09-E9-D8-09-2C-00'),
    @(0x6C21C0,24,'8B-44-24-04-85-C0-7C-0D-83-F8-06-7D-08-8A-54-24-08-88-54-08-09-C2-08-00'),
    @(0x6A4510,10,'C7-87-E4-05-00-00-00-00-00-C0'),
    # Entity family layouts/thiscall routines and actual saved-stat/HUD fields.
    @(0x447C90,28,'0F-BF-44-24-04-69-C0-D8-00-00-00-8A-90-95-C0-96-00-33-C9-80-FA-05-0F-94-C1-8A-C1-C3'),
    @(0x447E10,36,'0F-BF-54-24-04-69-D2-D8-00-00-00-8A-8A-96-C0-96-00-8D-92-96-C0-96-00-8A-C1-C0-E8-02-80-E1-FB-24-01-88-0A-C3'),
    @(0x448BE0,30,'83-EC-10-53-55-8B-6C-24-1C-8B-D1-8B-4D-14-85-C9-56-57-89-54-24-10-8D-41-30-75-03-8D-45-04'),
    @(0x44B49D,9,'A0-0A-C0-96-00-84-C0-75-52'),
    @(0x6D24F0,17,'56-8B-F1-8A-86-A8-04-00-00-8A-C8-80-E1-07-80-F9-01'),
    @(0x6D2593,25,'8B-8E-CC-04-00-00-85-C9-8D-86-E0-04-00-00-66-C7-86-DE-04-00-00-E8-03-89-08'),
    @(0x54FFF0,37,'8B-4C-24-04-8B-15-94-44-B7-00-53-56-8B-72-04-8B-C1-C1-F8-08-8A-1C-30-5E-3A-D9-5B-75-0B-8B-0A-69-C0-18-0A-00-00'),
    @(0x550050,37,'8B-4C-24-04-8B-15-9C-44-B7-00-53-56-8B-72-04-8B-C1-C1-F8-08-8A-1C-30-5E-3A-D9-5B-75-0B-8B-0A-69-C0-9C-01-00-00'),
    @(0x54FF90,37,'8B-4C-24-04-8B-15-90-44-B7-00-53-56-8B-72-04-8B-C1-C1-F8-08-8A-1C-30-5E-3A-D9-5B-75-0B-8B-0A-69-C0-C4-07-00-00'),
    @(0x439A80,13,'83-EC-0C-56-8B-F1-8B-4E-14-85-C9-74-51'),
    @(0x43D3B0,67,'56-57-6A-00-E8-A7-41-00-00-8B-7C-24-10-83-C4-04-83-FF-34-BE-1C-00-00-00-74-02-8B-F7-E8-BF-3F-00-00-84-C0-74-15-6A-00-66-C7-05-04-A7-96-00-04-00-E8-CB-54-00-00-83-C4-04-EB-09-66-C7-05-04-A7-96-00-01-00'),
    @(0x43D465,25,'8A-44-24-2C-8A-4C-24-30-8B-15-84-CB-B7-00-A2-4A-9A-96-00-88-0D-49-9A-96-00'),
    @(0x43D1E0,7,'66-A1-04-A7-96-00-C3'),
    @(0x6A0050,20,'83-EC-20-56-57-8B-7C-24-2C-8A-07-3C-20-8B-F1-E9-53-3A-D6-FF'),
    @(0x72A494,5,'A3-0C-13-C8-00'),
    @(0x72A4B1,5,'A3-24-13-C8-00'),
    @(0x72A4F5,19,'66-A3-18-13-C8-00-66-A3-20-13-C8-00-66-A3-1C-13-C8-00-C3'),
    @(0x52D020,40,'A0-55-01-B7-00-8A-0D-54-01-B7-00-8A-15-53-01-B7-00-A2-4D-01-B7-00-A0-52-01-B7-00-88-0D-4C-01-B7-00-66-8B-0D-50-01-B7-00'),
    @(0x484623,49,'8A-4E-36-A1-7C-3C-A4-00-80-E1-F8-80-F9-10-75-13-83-F8-02-74-0E-56-E8-82-F9-F9-FF-A1-7C-3C-A4-00-83-C4-04-8A-56-36-C0-E0-03-80-E2-07-0A-C2-88-46-36'),
    @(0x411990,32,'56-8B-F1-8B-46-14-85-C0-75-11-E8-C1-DB-13-00-8B-46-14-50-8D-4E-04-E8-05-D8-13-00-8B-46-14-5E-C3'),
    @(0x6D1080,12,'8B-81-84-03-00-00-8B-80-CC-00-00-00'),
    @(0x558E40,33,'66-8B-44-24-04-66-3D-52-00-73-0B-0F-B7-C0-D9-04-85-80-93-B7-00-C3-0F-B7-C8-DB-04-8D-20-8E-B7-00-C3'),
    @(0x48567E,23,'6A-01-8B-CE-E8-F9-E9-FD-FF-66-8B-0D-78-3C-A4-00-66-89-0D-DC-B1-BA-00'),
    @(0x54FF60,11,'8B-4C-24-04-56-8B-35-90-44-B7-00'),
    # Damage gate must precede response calculation, including direct weapon
    # calls that would bypass a vtable-only hook.
    @(0x4B35A0,13,'83-EC-0C-56-8B-F1-8A-46-42-84-C0-74-10'),
    @(0x73A687,9,'E8-14-8F-D7-FF-84-C0-75-33'),
    @(0x490F29,24,'6A-01-6A-08-6A-00-E8-4C-62-F7-FF-83-C4-08-50-6A-04-8B-CF-E8-AF-D5-FF-FF'),
    @(0x465C20,13,'64-A1-00-00-00-00-6A-FF-68-18-7D-83-00'),
    @(0x63C340,7,'6A-FF-68-23-18-84-00'),
    @(0x63C366,19,'8B-44-24-2C-8A-4C-24-34-33-DB-89-46-08-C7-06-04-E9-86-00'),
    @(0x4471E0,16,'51-D9-44-24-08-D8-5C-24-14-DF-E0-F6-C4-05-7A-06'),
    @(0x447570,6,'88-81-94-C0-96-00'),
    @(0x447581,6,'8D-89-8C-C0-96-00'),
    @(0x4475B3,3,'80-CB-08'),
    @(0x4475D4,3,'80-CA-20'),
    @(0x44BDA7,5,'E8-44-D8-FF-FF'),
    @(0x44BE44,5,'E8-B7-DA-FF-FF'),
    @(0x601230,7,'6A-FF-68-AB-E7-83-00'),
    @(0x601390,7,'56-57-8B-F9-8D-77-04'),
    @(0x6010A0,8,'56-8D-71-04-6A-01-8B-CE'),
    @(0x6924F0,10,'8A-41-18-84-C0-75-29-8A-41-19'),
    @(0x5DEBC0,24,'8B-44-24-04-8B-15-EC-BB-C0-00-6B-C0-34-03-C2-89-81-9C-05-00-00-C2-04-00'),
    @(0x6A0050,16,'83-EC-20-56-57-8B-7C-24-2C-8A-07-3C-20-8B-F1-E9'),
    @(0x69DD50,16,'A1-70-A5-C1-00-53-8B-5C-24-0C-55-8B-6C-24-28-33'),
    @(0x588BE0,16,'A0-C0-AE-BA-00-84-C0-0F-85-4F-01-00-00-A0-14-C0'),
    @(0x6337A0,34,'8A-51-0A-8B-44-24-04-8B-80-7C-04-00-00-53-8A-59-09-8A-49-08-05-38-02-00-00-88-58-0D-88-48-0C-88-50-0E'),
    @(0x743C85,15,'8B-44-24-04-0F-BF-C0-6B-C0-70-05-B8-AA-C8-00'),
    @(0x48ABF5,12,'8B-98-7C-04-00-00-81-C3-30-01-00-00'),
    @(0x5F1C40,10,'8B-4C-24-08-8B-41-14-83-EC-0C'),
    @(0x600BE0,10,'8B-44-24-04-89-81-BC-00-00-00'),
    @(0x600BF0,10,'8B-44-24-04-89-81-C0-00-00-00'),
    @(0x5FFA61,12,'8B-8D-7C-04-00-00-D9-81-BC-00-00-00'),
    # Verified retail pad/debug-camera reads and next-instance extra selectors.
    @(0x48AEDC,9,'6A-01-8B-CE-E8-9B-91-FD-FF'),
    @(0x48A3D2,17,'8B-0D-B0-F7-B6-00-8B-15-A8-F7-B6-00-A1-AC-F7-B6-00'),
    @(0x48C9F4,33,'6A-03-8B-CE-E8-83-76-FD-FF-8A-0D-7C-3C-A4-00-8A-15-80-3C-A4-00-88-0D-58-64-8A-00-88-15-59-64-8A-00'),
    # Native targettable setter collects two inputs and invokes the object flag.
    @(0x482CDB,9,'6A-02-8B-CE-E8-9C-13-FE-FF'),
    @(0x482D16,7,'6A-01-E8-E3-C5-11-00'),
    # Native standard seek task type 907 and the verified control routine.
    @(0x46ACE0,6,'B8-8B-03-00-00-C3'),
    @(0x46F800,14,'6A-FF-68-F2-80-83-00-64-A1-00-00-00-00-50'),
    # Native 0496 matches original car wheel order, bike 2/3 aliases and ANY=4.
    @(0x48AFD6,7,'83-B8-90-05-00-00-09'),
    @(0x48AFEA,5,'83-F9-04-75-1A'),
    @(0x48B009,19,'83-F9-02-75-04-33-C9-EB-0A-83-F9-03-75-0B-B9-01-00-00-00'),
    @(0x48B02F,5,'83-F9-04-75-28'),
    # Original radar/fire/path/pickup/kill families use these retail handlers.
    @(0x583C70,23,'8B-54-24-04-83-FA-FF-74-43-8B-CA-81-E1-FF-FF-00-00-8D-04-89-C1-E0-03'),
    @(0x47C6AA,9,'6A-05-8B-CE-E8-CD-79-FE-FF'),
    @(0x47FEEC,9,'6A-05-8B-CE-E8-8B-41-FE-FF'),
    @(0x47F457,9,'6A-01-8B-CE-E8-20-4C-FE-FF'),
    @(0x47F47A,9,'6A-02-8B-CE-E8-FD-4B-FE-FF'),
    @(0x47F496,3,'0F-BF-C0'),
    @(0x1564C20,20,'8B-44-24-08-8B-4C-24-04-8D-14-48-66-8B-04-55-50-9A-96-00-C3'),
    @(0x1561630,28,'8B-44-24-04-8D-04-45-50-9A-96-00-B9-20-03-00-00-66-C7-00-00-00-83-C0-04-49-75-F5-C3'),
    @(0x48B19D,9,'6A-05-8B-CE-E8-DA-8E-FD-FF'),
    @(0x48B7B5,9,'6A-05-8B-CE-E8-C2-88-FD-FF'),
    @(0x4552A0,5,'E9-0B-F9-10-01'),
    @(0x1564BB0,37,'8B-4C-24-04-83-F9-FF-74-18-8B-C1-25-FF-FF-00-00-8B-D0-C1-E2-05-C1-E9-10-66-3B-8A-DA-88-97-00-74-03-83-C8-FF-C3'),
    @(0x4740F6,21,'8B-88-94-05-00-00-83-F9-03-75-11-C7-80-4C-08-00-00-AE-47-61-3E')
)) {
    $taskActualProfile=Read-NativeBytes $taskProfile[0] $taskProfile[1]
    if($taskActualProfile -ne $taskProfile[2]){throw ('Native profile mismatch at 0x{0:X}: expected {1}, found {2}' -f $taskProfile[0],$taskProfile[2],$taskActualProfile)}
}
$taskSdk=[IO.File]::ReadAllText((Join-Path $ProjectDirectory 'vendor\sa-reference\sdk-CMenuManager.h'))
$taskTextPolicy=[IO.File]::ReadAllText((Join-Path $ProjectDirectory 'vendor\sa-reference\TheScripts.h'))
$taskCamera=[IO.File]::ReadAllText((Join-Path $ProjectDirectory 'vendor\sa-reference\sdk-CCamera.h'))
if($taskCamera -notmatch 'VALIDATE_OFFSET\(CCamera, m_mCameraMatrix, 0x974\)'){
    throw 'Native spatial audio listener matrix offset differs'
}
if($taskTextPolicy -notmatch 'bAddNextMessageToPreviousBriefs = StaticRef<bool>\(0xA4448B\)'){
    throw 'Native previous-brief policy address differs'
}
foreach($taskPair in @(@('m_bMenuActive','5C'),@('m_bShutDownFrontEndRequested','32'),@('m_bStartUpFrontEndRequested','33'),@('m_bPrefsShowSubtitles','44'))){
    if($taskSdk -notmatch ('VALIDATE_OFFSET\(CMenuManager, '+$taskPair[0]+', 0x'+$taskPair[1]+'\)')){throw 'Native frontend field offset differs'}
}
$taskEnums=[IO.File]::ReadAllText((Join-Path $ProjectDirectory 'vendor\sa-reference\eOnscreenCounter.h'))
$taskMessages=[IO.File]::ReadAllText((Join-Path $ProjectDirectory 'vendor\sa-reference\Messages.h'))
if($taskMessages -notmatch 'BriefMessages.*0xC1A7F0' -or $taskMessages -notmatch 'BIGMessages.*0xC1A970' -or
   0xC1A970-0xC1A7F0 -ne 8*0x30){throw 'Native mission message queue address/stride differs'}
$taskPed=[IO.File]::ReadAllText((Join-Path $ProjectDirectory 'vendor\sa-reference\sdk-CPed.h'))
if($taskPed -notmatch 'VALIDATE_OFFSET\(CPed, m_pIntelligence, 0x47C\)'){throw 'Native character intelligence offset differs'}
if($taskPed -notmatch 'VALIDATE_OFFSET\(CPed, m_fHealth, 0x540\)' -or $taskPed -notmatch 'VALIDATE_OFFSET\(CPed, m_nPedType, 0x598\)'){
    throw 'Native dead-character health/type offsets differ'
}
$taskTask=[IO.File]::ReadAllText((Join-Path $ProjectDirectory 'vendor\sa-reference\Tasks_Task.h'))
$taskComplex=[IO.File]::ReadAllText((Join-Path $ProjectDirectory 'vendor\sa-reference\Tasks_TaskComplex.h'))
$taskManager=[IO.File]::ReadAllText((Join-Path $ProjectDirectory 'vendor\sa-reference\Tasks_TaskManager.h'))
$taskSeek=[IO.File]::ReadAllText((Join-Path $ProjectDirectory 'vendor\sa-reference\Tasks_TaskTypes_SeekEntity_TaskComplexSeekEntity.h'))
if($taskTask -notmatch 'VALIDATE_SIZE\(CTask, 0x8\)' -or $taskComplex -notmatch 'VALIDATE_SIZE\(CTaskComplex, 0xC\)' -or
   $taskTask -notmatch '(?s)virtual ~CTask.*?virtual CTask\* Clone.*?virtual CTask\* GetSubTask.*?virtual bool IsSimple.*?virtual eTaskType GetTaskType' -or
   $taskSeek -notmatch '(?s)CEntity\* m_entityToSeek.*?int32 m_seekInterval.*?int32 m_scanInterval.*?float m_maxEntityDist2D.*?float m_moveStateRadius.*?float m_minEntityDist2D' -or
   $taskSeek -notmatch 'm_minEntityDist2D == 0.f \|\| pedToSeekPosDist2DSq > sq\(m_minEntityDist2D\)') {throw 'Native seek task common prefix, virtual slots or route branch differs'}
$taskPrimary=[regex]::Match($taskManager,'(?s)enum ePrimaryTasks.*?\{(.*?)TASK_PRIMARY_MAX').Groups[1].Value
$taskSecondary=[regex]::Match($taskManager,'(?s)enum eSecondaryTask.*?\{(.*?)TASK_SECONDARY_MAX').Groups[1].Value
if([regex]::Matches($taskPrimary,'\bTASK_PRIMARY_(?!INVALID)\w+').Count -ne 5 -or [regex]::Matches($taskSecondary,'\bTASK_SECONDARY_(?!INVALID)\w+').Count -ne 6){throw 'Native task root counts differ'}
if($taskPed -notmatch 'VALIDATE_OFFSET\(CPed, m_pStats, 0x59C\)'){throw 'Native character personality pointer offset differs'}
$taskPedFlags=[regex]::Match($taskPed,'(?s)#pragma pack\(push, 1\)(.*?)#pragma pack\(pop\)').Groups[1].Value
$taskPedFlags=[regex]::Replace($taskPedFlags,'//[^\r\n]*','')
$taskFlagNames=@([regex]::Matches($taskPedFlags,'bool\s+(\w+)\s*:\s*1;') | ForEach-Object {$_.Groups[1].Value})
if($taskPed -notmatch '(?s)int field_468;.*?#pragma pack\(push, 1\)' -or $taskFlagNames[56] -ne 'bDontFight' -or $taskFlagNames[25] -ne 'bCrouchWhenShooting' -or $taskFlagNames[19] -ne 'bStopAndShoot' -or $taskFlagNames[22] -ne 'bKindaStayInSamePlace' -or $taskFlagNames[62] -ne 'bCrouchWhenScared'){throw 'Native character fight/crouch/stationary shooter packed flag offsets differ'}
$taskModels=[IO.File]::ReadAllText((Join-Path $ProjectDirectory 'vendor\sa-reference\BaseModelInfo.h'))
foreach($taskPair in @(@('WEAPON',4),@('VEHICLE',6),@('PED',7))){
    if($taskModels -notmatch ('MODEL_INFO_'+$taskPair[0]+'\s*=\s*'+$taskPair[1])){throw 'Native equivalent model class differs from the primary enum'}
}
$taskPlayerInfo=[IO.File]::ReadAllText((Join-Path $ProjectDirectory 'vendor\sa-reference\sdk-CPlayerInfo.h'))
foreach($taskPair in @(@('m_nCollectablesPickedUp','C0'),@('m_nTotalNumCollectables','C4'))) {
    if($taskPlayerInfo -notmatch ('VALIDATE_OFFSET\(CPlayerInfo, '+$taskPair[0]+', 0x'+$taskPair[1]+'\)')){throw 'Native collectable counter field offset differs'}
}
if($taskPlayerInfo -notmatch 'VALIDATE_SIZE\(CPlayerInfo, 0x190\)'){throw 'Native player-info stride differs'}
if($taskEnums -notmatch 'INCREASE\s*=\s*0' -or $taskEnums -notmatch 'DECREASE\s*=\s*1'){throw 'Native clock direction differs'}
$taskTimer=[IO.File]::ReadAllText((Join-Path $ProjectDirectory 'vendor\sa-reference\OnscreenTimer.cpp'))
foreach($taskPair in @(@('AddClock','44CD50'),@('ClearClock','44CE60'),@('AddCounter','44CDA0'),@('ClearCounter','44CE80'))){
    if($taskTimer -notmatch ('RH_ScopedInstall\('+$taskPair[0]+', 0x'+$taskPair[1]+'\)')){throw 'Native HUD function profile differs'}
}
Write-Output 'PASS: executable pause tail, frontend/player collectable offsets, HUD clock direction and native garage entry/name/type/rotation/camera layout; gameExecuted=false'
