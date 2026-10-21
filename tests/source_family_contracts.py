"""Check family decode contracts against the original LCS and primary SA data."""
import pathlib,re
ROOT=pathlib.Path(__file__).resolve().parents[1]
original=(ROOT / 'local/references/res-relcs/src/control/ScriptDebug.cpp').read_text()
pattern=r'REGISTER_COMMAND\((COMMAND_[A-Z0-9_]+), INPUT_ARGUMENTS\(([^)]*)\), OUTPUT_ARGUMENTS\(([^)]*)\), (true|false)'
contracts={name:(len(re.findall('ARGTYPE_',inputs)),len(re.findall('ARGTYPE_',outputs)),predicate=='true')
           for name,inputs,outputs,predicate in re.findall(pattern,original)}
# ScriptDebug has a zero-output typo here. The executing source handler is
# authoritative: it consumes four bounds and stores one actual vehicle handle.
random_name='COMMAND_GET_RANDOM_CAR_IN_AREA_NO_CHECKS_NO_SAVE'
random_source=(ROOT / 'local/references/res-relcs/src/control/Script8.cpp').read_text()
random_handler=re.search(r'case '+random_name+r':(.*?)(?=\n\s*case COMMAND_)',random_source,re.S)[1]
assert contracts[random_name]==(4,0,False),'Review the source debug-table exception if it changes'
assert re.search(r'CollectParameters\(&m_nIp, 4\)',random_handler)
assert re.search(r'StoreParameters\(&m_nIp, 1\)',random_handler)
contracts[random_name]=(4,1,False)
# ScriptDebug also lists SET_PICKUP_VALUE with no operands. Verify against
# the actual integer-to-revenue handler, then use its two-input contract.
pickup_name='COMMAND_SET_PICKUP_VALUE'
pickup_source=(ROOT / 'local/references/res-relcs/src/control/Script10.cpp').read_text()
pickup_handler=re.search(r'case '+pickup_name+r':(.*?)(?=\n\s*case COMMAND_)',pickup_source,re.S)[1]
assert contracts[pickup_name]==(0,0,False),'Review the pickup debug-table exception if it changes'
assert re.search(r'CollectParameters\(&m_nIp, 2\)',pickup_handler)
assert 'CPickups::SetValue(GET_INTEGER_PARAM(0), GET_INTEGER_PARAM(1))' in pickup_handler
assert 'StoreParameters(' not in pickup_handler
contracts[pickup_name]=(2,0,False)
entity=(ROOT/'src/native_entity_families.inc').read_text()
entity_specs=re.findall(r'\{(COMMAND_\w+),EntityCommand::\w+,(\d+),(\d+)(?:,(true|false))?\}',entity)
stats=(ROOT/'src/native_statistics_family.inc').read_text()
stat_specs=re.findall(r'\{(COMMAND_\w+),SourceStatistic::\w+,(true|false)\}',stats)
# v76 adds the source critical-mission recovery missing from full campaign mode.
assert len(entity_specs)==51 and len(stat_specs)==8,'New family inventory changed; review every source signature'
restart_name='COMMAND_RESTART_CRITICAL_MISSION'
assert contracts[restart_name]==(4,0,False)
assert (restart_name,'4','0','') in entity_specs
restart_source=(ROOT / 'local/references/res-relcs/src/control/Script3.cpp').read_text()
restart_handler=re.search(r'case '+restart_name+r':(.*?)(?=\n\s*case COMMAND_)',restart_source,re.S)[1]
assert 'CollectParameters(&m_nIp, 4)' in restart_handler
assert restart_handler.index('OverrideNextRestart')<restart_handler.index('PlayerFailedCriticalMission')
# v58 adds exactly the reviewed zero-operand cancellation and save-menu query.
# Their independent retail ELF/PE checks also run in the startup/package gate.
assert contracts['COMMAND_CANCEL_REMOTE_MODE']==(0,0,False)
assert contracts['COMMAND_HAS_SAVE_GAME_FINISHED']==(0,0,True)
assert ('COMMAND_CANCEL_REMOTE_MODE','0','0','') in entity_specs
assert ('COMMAND_HAS_SAVE_GAME_FINISHED','0','0','true') in entity_specs
clear_name='COMMAND_CLEAR_AREA_OF_OBJECTS'
clear_source=(ROOT / 'local/references/res-relcs/src/control/Script9.cpp').read_text()
clear_handler=re.search(r'case '+clear_name+r':(.*?)(?=\n\s*case COMMAND_)',clear_source,re.S)[1]
assert contracts[clear_name]==(6,0,False)
assert 'CollectParameters(&m_nIp, 6)' in clear_handler and 'StoreParameters(' not in clear_handler
assert 'GetObjectPool()' in clear_handler and 'GetDummyPool()' in clear_handler and clear_handler.count('while (i--)')==2
assert 'pObject->CanBeDeleted()' in clear_handler and 'pDummy->IsObject()' in clear_handler
for prefix in ('pObject','pDummy'):
    assert clear_handler.index(prefix+'->DeleteRwObject()')<clear_handler.index('CWorld::Remove('+prefix+')')<clear_handler.index('delete '+prefix+';')
leave_name='COMMAND_IS_CHAR_LEAVING_VEHICLE_TO_DIE'
leave_source=(ROOT / 'local/references/res-relcs/src/control/Script7.cpp').read_text()
leave_handler=re.search(r'case '+leave_name+r':(.*?)(?=\n\s*case COMMAND_)',leave_source,re.S)[1]
assert contracts[leave_name]==(1,0,True)
assert 'CollectParameters(&m_nIp, 1)' in leave_handler
assert 'UpdateCompareFlag(pPed->m_objective == OBJECTIVE_LEAVE_CAR_AND_DIE)' in leave_handler
for name,inputs,outputs,predicate in entity_specs:
    assert contracts[name]==(int(inputs),int(outputs),predicate=='true'),(name,contracts[name],inputs,outputs)
for name,operand in stat_specs:
    assert contracts[name]==(int(operand=='true'),0,False),(name,contracts[name])
for name,signature in [('COMMAND_GET_ANGLE_BETWEEN_POINTS',(4,1,False)),
                       ('COMMAND_GET_DEBUG_CAMERA_POINT_AT',(0,3,False)),
                       ('COMMAND_IS_PLAYER_IN_REMOTE_MODE',(1,0,True))]:
    assert contracts[name]==signature,(name,contracts[name])
for filename,fields in [('sdk-CPhysical.h',{'m_vecMoveSpeed':0x44,'m_vecTurnSpeed':0x50}),
                        ('sdk-CPlayerInfo.h',{'m_bFireProof':0x14E,'m_nMaxHealth':0x14F,'m_nMaxArmour':0x150,'m_bGetOutOfJailFree':0x151}),
                        ('sdk-CVehicle.h',{'m_pHandlingData':0x384,'m_nAlarmState':0x45C,'m_nCreatedBy':0x4A4,'m_fHealth':0x4C0}),
                        ('sdk-CPed.h',{'m_fHealth':0x540,'m_fArmour':0x548}),
                        ('sdk-CPickup.h',{'m_fRevenueValue':0,'m_nReferenceIndex':0x1A,'m_nPickupType':0x1C}),
                        ('sdk-CBoat.h',{'m_nBoatFlags':0x5AC})]:
    source=(ROOT/'vendor/sa-reference'/filename).read_text()
    for field,offset in fields.items():
        match=re.search(r'VALIDATE_OFFSET\(\w+, '+field+r', (0x[0-9A-Fa-f]+)\)',source)
        assert match and int(match[1],16)==offset,(filename,field,offset)
enums=(ROOT/'vendor/sa-reference/eStats.h').read_text()
for name,value in [('CASH_MADE_IN_A_TAXI',149),('PASSENGERS_DROPPED_OFF',150),('PEOPLE_SAVED_IN_AN_AMBULANCE',151),
                   ('CRIMINALS_KILLED_ON_VIGILANTE_MISSION',152),('TOTAL_FIRES_EXTINGUISHED',153),
                   ('HIGHEST_VIGILANTE_MISSION_LEVEL',157),('HIGHEST_PARAMEDIC_MISSION_LEVEL',158),('HIGHEST_FIREFIGHTER_MISSION_LEVEL',159)]:
    assert re.search('STAT_'+name+r'\s*=\s*'+str(value)+r'\s*[,\n]',enums),(name,value)
assert 'StatTypesInt = StaticRef<std::array<int32, 223>>(0xB79000)' in (ROOT/'vendor/sa-reference/Stats.h').read_text()
assert re.search(r'enum eHudItem : int16', (ROOT/'vendor/sa-reference/eHud.h').read_text()),'HUD mutation must remain two bytes'
for name,inputs,outputs in [('COMMAND_ADD_BLIP_FOR_COORD_OLD',5,1),('COMMAND_START_SCRIPT_FIRE',3,1),
    ('COMMAND_CREATE_PROTECTION_PICKUP',5,1),('COMMAND_GET_CLOSEST_STRAIGHT_ROAD',5,7),
    ('COMMAND_RESET_NUM_OF_MODELS_KILLED_BY_PLAYER',0,0),('COMMAND_GET_NUM_OF_MODELS_KILLED_BY_PLAYER',1,1)]:
    assert contracts[name]==(inputs,outputs,False),(name,contracts[name])
vehicle=(ROOT/'vendor/sa-reference/sdk-CVehicle.h').read_text()
assert 'VALIDATE_OFFSET(CVehicle, m_nVehicleClass, 0x590)' in vehicle
assert 'VALIDATE_SIZE(CAutoPilot, 0x98)' in (ROOT/'vendor/sa-reference/sdk-CAutoPilot.h').read_text()
packed=vehicle[vehicle.index('bool bIsLawEnforcer'):vehicle.index('unsigned int  m_nCreationTime')]
flags=re.findall(r'bool\s+(\w+)\s*:\s*1;',packed)
assert flags.index('bIsBeingCarJacked')==19,'Jack flag byte/bit changed'
types=re.findall(r'\bVEHICLE_[A-Z]+\b',(ROOT/'vendor/sa-reference/eVehicleType.h').read_text())
assert types[6]=='VEHICLE_BOAT' and types[10]=='VEHICLE_BIKE' and types[11]=='VEHICLE_BMX'
radar=(ROOT/'vendor/sa-reference/source_game_sa_Radar.h').read_text()
assert 'VALIDATE_SIZE(tRadarTrace, 0x28)' in radar and 'MAX_RADAR_TRACES = 175' in radar
assert re.search(r'ms_RadarTrace.*0xBA86F0',radar)
assert 'VALIDATE_SIZE(CPickup, 0x20)' in (ROOT/'vendor/sa-reference/sdk-CPickup.h').read_text()
assert 'MAX_NUM_PICKUPS; // 620' in (ROOT/'vendor/sa-reference/sdk-CPickups.h').read_text()
for name,filename in [('COMMAND_START_KILL_FRENZY','Script3.cpp'),('COMMAND_START_KILL_FRENZY_HEADSHOT','Script4.cpp')]:
    assert contracts[name]==(9,0,False),(name,contracts[name])
    source=(ROOT / 'local/references/res-relcs/src/control'/filename).read_text()
    handler=re.search(r'case '+name+r':(.*?)(?=\n\s*case COMMAND_)',source,re.S)[1]
    assert 'GetTextByKeyFromScript(&m_nIp)' in handler and 'CollectParameters(&m_nIp, 8)' in handler
    assert 'CDarkel::StartFrenzy' in handler and ('!= 0, true)' if name.endswith('HEADSHOT') else '!= 0, false)') in handler
for name in ('COMMAND_STORE_WEATHER','COMMAND_RESTORE_WEATHER','COMMAND_STORE_CLOCK','COMMAND_RESTORE_CLOCK'):
    assert contracts[name]==(0,0,False),(name,contracts[name])
assert contracts['COMMAND_SET_CAR_STATUS']==(2,0,False)
source=(ROOT / 'local/references/res-relcs/src/control/Script3.cpp').read_text()
assert re.search(r'case COMMAND_RESTORE_WEATHER:\s*CWeather::RestoreWeatherState\(\);\s*case COMMAND_STORE_CLOCK:',source),'Source weather restore fallthrough changed'
weather_source=(ROOT / 'local/references/res-relcs/src/renderer/Weather.cpp').read_text()
weather_store=re.search(r'void CWeather::StoreWeatherState\(\)(.*?)(?=void CWeather::RestoreWeatherState)',weather_source,re.S)[1]
for field in ('InterpolationValue','Rain','NewWeatherType','OldWeatherType'):
    assert 'Stored_'+field+' = '+field+';' in weather_store
assert 'ForcedWeatherType' not in weather_store
clock_source=(ROOT / 'local/references/res-relcs/src/core/Clock.cpp').read_text()
clock_restore=re.search(r'CClock::RestoreClock\(void\)(.*?)(?=void)',clock_source,re.S)[1]
for field in ('Hours','Minutes','Seconds'):assert 'ms_nGameClock'+field+' = ms_Stored_nGameClock'+field+';' in clock_restore
assert 'Days' not in clock_restore and 'Months' not in clock_restore
native_clock=(ROOT/'vendor/sa-reference/source_Clock.h').read_text()
for field,kind,offset in [('ms_nGameClockHours','uint8',0xB70153),('ms_nGameClockMinutes','uint8',0xB70152),('ms_nGameClockSeconds','uint16',0xB70150)]:
    assert re.search(field+r' = StaticRef<'+kind+r'>\(0x'+format(offset,'X')+r'\)',native_clock)
native_weather=(ROOT/'vendor/sa-reference/source_Weather.h').read_text()
for field,kind,offset in [('InterpolationValue','float',0xC8130C),('Rain','float',0xC81324),('NewWeatherType','eWeatherType',0xC8131C),('OldWeatherType','eWeatherType',0xC81320)]:
    assert re.search(field+r' = StaticRef<'+kind+r'>\(0x'+format(offset,'X')+r'\)',native_weather)
# The debug signature incorrectly declares HAS_RESPRAY_HAPPENED as a non-
# predicate. Its actual source consumes a garage and updates the compare flag.
respray_name='COMMAND_HAS_RESPRAY_HAPPENED'
assert contracts[respray_name]==(1,0,False)
respray_source=(ROOT / 'local/references/res-relcs/src/control/Script4.cpp').read_text()
respray_handler=re.search(r'case '+respray_name+r':(.*?)(?=\n\s*case COMMAND_)',respray_source,re.S)[1]
assert 'CollectParameters(&m_nIp, 1)' in respray_handler and 'UpdateCompareFlag(CGarages::HasResprayHappened' in respray_handler
contracts[respray_name]=(1,0,True)
garage_specs=re.findall(r'\{(COMMAND_\w+),GarageCommand::\w+,(\d+),(true|false),(true|false)\}',(ROOT/'src/native_garage_family.inc').read_text())
assert len(garage_specs)==8
for name,inputs,predicate,named in garage_specs:
    assert contracts[name]==(int(inputs),0,predicate=='true'),(name,contracts[name])
garage_source=(ROOT / 'local/references/res-relcs/src/control/Garages.cpp').read_text()
assert re.search(r'HasCarBeenDroppedOffYet\(int16 garage\).*?m_eGarageState == GS_CLOSEDCONTAINSCAR',garage_source,re.S)
assert re.search(r'IsThisCarWithinGarageArea\(int16 garage, CEntity \* pCar\).*?IsEntityEntirelyInside3D\(pCar, 0.0f\)',garage_source,re.S)
assert re.search(r'HasResprayHappened\(int16 garage\).*?m_bResprayHappened = false;',garage_source,re.S)
vehicle_source=(ROOT / 'local/references/res-relcs/src/vehicles/Vehicle.h').read_text()
bomb_types=re.search(r'enum eBombType\s*\{(.*?)\}',vehicle_source,re.S)
assert bomb_types and re.findall(r'CARBOMB_\w+',bomb_types[1])==['CARBOMB_NONE','CARBOMB_TIMED','CARBOMB_ONIGNITION','CARBOMB_REMOTE','CARBOMB_TIMEDACTIVE','CARBOMB_ONIGNITIONACTIVE']
native_garage=(ROOT/'vendor/sa-reference/Garage.h').read_text()
assert 'VALIDATE_SIZE(CGarage, 0xD8)' in native_garage
native_flags=re.search(r'uint8 m_b0x1.*?m_bRespraysAlwaysFree : 1;',native_garage,re.S)[0]
assert re.findall(r'uint8\s+(\w+)\s*:\s*1;',native_flags).index('m_bUsedRespray')==2
native_garages=(ROOT/'vendor/sa-reference/Garages.h').read_text()
assert 'BombsAreFree = StaticRef<int8>(0x96C00A)' in native_garages and 'NumGarages = StaticRef<int32>(0x96C024)' in native_garages
assert 'RespraysAreFree = StaticRef<int8>(0x96C009)' in native_garages
free_respray_handler=re.search(r'case COMMAND_SET_FREE_RESPRAYS:(.*?)(?=\n\s*case COMMAND_)',respray_source,re.S)[1]
assert 'CollectParameters(&m_nIp, 1)' in free_respray_handler and 'SetFreeResprays(GET_INTEGER_PARAM(0) != 0)' in free_respray_handler
assert 'VALIDATE_OFFSET(CVehicle, m_pWhoInstalledBombOnMe, 0x4CC)' in vehicle and 'VALIDATE_OFFSET(CVehicle, m_wBombTimer, 0x4DE)' in vehicle
traffic_specs=re.findall(r'\{(COMMAND_\w+),TrafficCommand::\w+,(\d+)\}',(ROOT/'src/native_traffic_family.inc').read_text())
assert len(traffic_specs)==6
for name,inputs in traffic_specs:assert contracts[name]==(int(inputs),0,False),(name,contracts[name])
source_controls=ROOT / 'local/references/res-relcs/src/control'
cleanup_handler=re.search(r'case COMMAND_DONT_REMOVE_CAR:(.*?)(?=\n\s*case COMMAND_)',(source_controls/'Script2.cpp').read_text(),re.S)[1]
assert 'MissionCleanUp.RemoveEntityFromList(GET_INTEGER_PARAM(0), CLEANUP_CAR)' in cleanup_handler
assert 'Keep' not in cleanup_handler and 'RemoveEntityFromList(int32 handle' in (ROOT/'vendor/sa-reference/MissionCleanup.cpp').read_text()
for filename,name,assignment in [('Script10.cpp','COMMAND_ENABLE_EMERGENCY_VEHICLES','gbEmergencyVehiclesEnabled = GET_INTEGER_PARAM(0) != 0'),('Script6.cpp','COMMAND_SET_GENERATE_CARS_AROUND_CAMERA','bCarsGeneratedAroundCamera = (GET_INTEGER_PARAM(0) != 0)')]:
    handler=re.search(r'case '+name+r':(.*?)(?=\n\s*case COMMAND_)',(source_controls/filename).read_text(),re.S)[1];assert assignment in handler
carctrl=(ROOT/'vendor/sa-reference/CarCtrl.h').read_text()
assert 'bAllowEmergencyServicesToBeCreated = StaticRef<bool>(0x8A5B28)' in carctrl
assert 'bCarsGeneratedAroundCamera = StaticRef<bool>(0x9690C1)' in carctrl
cleanup=(ROOT/'vendor/sa-reference/sdk-CMissionCleanup.h').read_text()
for field,offset in [('type',0),('handle',4)]:assert f'VALIDATE_OFFSET(tMissionCleanupEntity, {field}, 0x{offset:X})' in cleanup
assert 'm_Objects[75]' in cleanup and 'VALIDATE_SIZE(tMissionCleanupEntity, 0x8)' in cleanup and 'VALIDATE_OFFSET(CMissionCleanup, m_Count, 0x258)' in cleanup
native_pad=(ROOT/'vendor/sa-reference/sdk-CPad.h').read_text()
assert 'SteeringLeftRightBuffer[10]' in native_pad and 'VALIDATE_OFFSET(CPad, DrunkDrivingBufferUsed, 0x74)' in native_pad and 'VALIDATE_SIZE(CPad, 0x134)' in native_pad
source_pad=(ROOT / 'local/references/res-relcs/src/core/Pad.h').read_text()
assert 'DRUNK_STEERING_BUFFER_SIZE = 10' in source_pad and 'DrunkDrivingBufferUsed = delay;' in source_pad
source_auto=(ROOT / 'local/references/res-relcs/src/vehicles/Automobile.cpp').read_text()
boot=re.search(r'CAutomobile::PopBootUsingPhysics\(void\)(.*?)(?=void)',source_auto,re.S)[1]
assert re.search(r'switch\(Damage.GetDoorStatus\(DOOR_BOOT\)\)\s*case DOOR_STATUS_OK:\s*case DOOR_STATUS_SMASHED:',boot)
assert 'Damage.SetDoorStatus(DOOR_BOOT, DOOR_STATUS_SWINGING)' in boot and 'Doors[DOOR_BOOT].m_fAngVel = -2.0f' in boot
native_auto=(ROOT/'vendor/sa-reference/Automobile.h').read_text()
assert 'VALIDATE_OFFSET(CAutomobile, autoFlags, 0x868)' in native_auto
native_auto_flags=re.search(r'bool bTaxiLight.*?} autoFlags;',native_auto,re.S)[0]
assert re.findall(r'bool\s+(\w+)\s*:',native_auto_flags).index('bTankExplodesCars')==5
assert 'VALIDATE_SIZE(CDamageManager, 0x18)' in (ROOT/'vendor/sa-reference/DamageManager.h').read_text()
assert 'VALIDATE_SIZE(CDoor, 0x18)' in (ROOT/'vendor/sa-reference/Door.h').read_text()
source7=(source_controls/'Script7.cpp').read_text()
drowning=re.search(r'case COMMAND_IS_CHAR_DROWNING_IN_WATER:(.*?)(?=\n\s*case COMMAND_)',source7,re.S)[1]
assert 'UpdateCompareFlag(pPed && pPed->bIsDrowning)' in drowning
ped=(ROOT/'vendor/sa-reference/sdk-CPed.h').read_text()
ped_flags=re.search(r'#pragma pack\(push, 1\)(.*?)#pragma pack\(pop\)',ped,re.S)[1]
ped_flags=re.sub(r'//[^\r\n]*','',ped_flags)
assert re.findall(r'bool\s+(\w+)\s*:\s*1;',ped_flags).index('bIsDrowning')==51
assert re.search(r'int field_468;\s*/\*.*?\*/\s*#pragma pack\(push, 1\)',ped,re.S) or re.search(r'int field_468;.*?#pragma pack\(push, 1\)',ped,re.S)
for field,offset in [('m_aWeapons',0x5A0),('m_nSelectedWepSlot',0x718),('m_pPlayerData',0x480)]:
    assert f'VALIDATE_OFFSET(CPed, {field}, 0x{offset:X})' in ped
assert 'CWeapon             m_aWeapons[13]' in ped
weapon=(ROOT/'vendor/sa-reference/sdk-CWeapon.h').read_text()
assert 'VALIDATE_SIZE(CWeapon, 0x1C)' in weapon and 'VALIDATE_OFFSET(CWeapon, m_nAmmoInClip, 0x8)' in weapon
assert re.search(r'unsigned int\s+m_nAmmoInClip;',weapon)
clip=re.search(r'case COMMAND_SET_PLAYER_CURRENT_WEAPON_AMMO_IN_CLIP:(.*?)(?=\n\s*case COMMAND_)',(source_controls/'Script10.cpp').read_text(),re.S)[1]
assert 'FindPlayerPed()->GetWeapon()' in clip and 'if (pWeapon && GET_INTEGER_PARAM(0))' in clip and 'pWeapon->m_nAmmoInClip = GET_INTEGER_PARAM(0)' in clip
world=(ROOT/'vendor/sa-reference/World.h').read_text()
assert 'PlayerInFocus = StaticRef<uint8>(0xB7CD74)' in world
camera=(ROOT / 'local/references/res-relcs/src/core/Camera.cpp').read_text()
camera_predicate=re.search(r'CCamera::Using1stPersonWeaponMode\(void\)(.*?)(?=void)',camera,re.S)[1]
assert re.findall(r'case CCam::(MODE_\w+)',camera_predicate)==['MODE_SNIPER','MODE_M16_1STPERSON','MODE_ROCKETLAUNCHER','MODE_HELICANNON_1STPERSON','MODE_CAMERA']
modes=(ROOT/'vendor/sa-reference/eCamMode.h').read_text()
for name,value in [('MODE_SNIPER',7),('MODE_M16_1STPERSON',34),('MODE_ROCKETLAUNCHER',8),('MODE_HELICANNON_1STPERSON',45),('MODE_CAMERA',46)]:
    assert int(re.search(r'\b'+name+r'\s*=\s*(0x[0-9A-Fa-f]+)',modes)[1],16)==value
camera_handler=re.search(r'case COMMAND_IS_PLAYER_USING_FIRST_PERSON_WEAPON_CAMERA:(.*?)(?=\n\s*case COMMAND_|\n\s*//case COMMAND_)',(source_controls/'Script8.cpp').read_text(),re.S)[1]
assert 'CollectParameters(&m_nIp, 1)' in camera_handler and 'UpdateCompareFlag(TheCamera.Using1stPersonWeaponMode())' in camera_handler and 'GET_INTEGER_PARAM' not in camera_handler
remove_boot=re.search(r'case COMMAND_REMOVE_CAR_BOOT:(.*?)(?=\n\s*case COMMAND_)',(source_controls/'Script9.cpp').read_text(),re.S)[1]
assert 'Damage.SetDoorStatus(DOOR_BOOT, DOOR_STATUS_MISSING)' in remove_boot and 'SetDoorDamage(CAR_BOOT, DOOR_BOOT, true)' in remove_boot
native_boot=(ROOT/'vendor/sa-reference/source_game_sa_Entity_Vehicle_Automobile.cpp').read_text()
assert 'RH_ScopedInstall(SetDoorDamage, 0x6B1600)' in native_boot and 'void CAutomobile::SetDoorDamage(eDoors doorIdx, bool withoutVisualEffect)' in native_boot
assert re.search(r'case DAMSTATE_NOTPRESENT:.*?if \(!withoutVisualEffect\)',native_boot,re.S)
photo=re.search(r'case COMMAND_HAS_PHOTOGRAPH_BEEN_TAKEN:(.*?)(?=\n\s*case COMMAND_)',source7,re.S)[1]
assert 'UpdateCompareFlag(CWeapon::bPhotographHasBeenTaken)' in photo and 'CWeapon::bPhotographHasBeenTaken = false' in photo
assert 'bool &CWeapon::bPhotographHasBeenTaken = *(bool*)0xC8A7C0;' in (ROOT/'vendor/sa-reference/sdk-CWeapon.cpp').read_text()
source6=(source_controls/'Script6.cpp').read_text()
credits_handler=re.search(r'case COMMAND_ARE_CREDITS_FINISHED:(.*?)(?=\n\s*case COMMAND_)',source6,re.S)[1]
assert 'UpdateCompareFlag(CCredits::AreCreditsDone())' in credits_handler
assert re.search(r'CCredits::AreCreditsDone\(void\)\s*\{\s*return !bCreditsGoing;', (ROOT / 'local/references/res-relcs/src/renderer/Credits.cpp').read_text())
assert 'bool &CCredits::bCreditsGoing = *(bool *)0xC6E97C;' in (ROOT/'vendor/sa-reference/sdk-CCredits.cpp').read_text()
control_handler=re.search(r'case COMMAND_ENABLE_PLAYER_CONTROL_CAMERA:(.*?)(?=\n\s*case COMMAND_)',source6,re.S)[1]
assert 'CPad::GetPad(0)->SetEnablePlayerControls(PLAYERCONTROL_CAMERA)' in control_handler
assert 'PLAYERCONTROL_CAMERA = 1' in source_pad and 'DisablePlayerControls &= ~who' in source_pad
assert 'VALIDATE_OFFSET(CPad, DisablePlayerControls, 0x10E)' in native_pad
# Walk-cycle identities come from the retail PS2 data table itself, including
# strings in .sdata. They do not depend on a PC conditional enum or SA IDs.
import importlib.util,sys
spec=importlib.util.spec_from_file_location('original_inspect',ROOT/'tools/inspect-original-command.py')
inspector=importlib.util.module_from_spec(spec);sys.modules[spec.name]=inspector;spec.loader.exec_module(inspector)
ps2=inspector.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23');ps2.require_profile()
style_header=(ROOT/'include/lcs/locomotion_groups.hpp').read_text()
style_names=re.findall(r'"([a-z0-9]+)"',style_header)
style_ids=[0,31,*range(38,54)]
assert len(style_names)==len(style_ids)==18
for group,name in zip(style_ids,style_names):
    row=0x399A68+24*group;pointer=ps2.word(row)
    assert ps2.read(pointer,len(name)+1)==name.encode()+b'\0',(group,name)
assert ps2.handler(586)['handler']==0x25CE9C
words=[ps2.word(0x25CE9C+n*4) for n in range(23)]
assert 0x24060002 in words and 0x0C042532 in words # CollectParameters(2)
assert 0x0C04481A in words                       # real generational PedPool::GetAt
assert ps2.word(0x25CED8)==0xAE6202E8            # original group field, not SA index
assert ps2.word(0x25CEE8)==0xFE6301A0            # original override flag is cleared
assert ps2.handler(1219)['handler']==0x185B30 and ps2.handler(1220)['handler']==0x185B6C
assert ps2.word(0x185B68)==0xAE40015C and ps2.word(0x185BAC)==0x8E42015C
for name in ['RESET_HAVOC_CAUSED_BY_PLAYER','GET_HAVOC_CAUSED_BY_PLAYER']:
    handler=re.search(r'case COMMAND_'+name+r':(.*?)(?=\n\s*case COMMAND_)',source7,re.S)[1]
    assert 'CWorld::Players[GET_INTEGER_PARAM(0)].m_nHavocLevel' in handler
assert 'VALIDATE_OFFSET(CPlayerInfo, m_nHavocCaused, 0x140)' in (ROOT/'vendor/sa-reference/sdk-CPlayerInfo.h').read_text()
assert 'plyrinfo.m_nHavocCaused += 20' in (ROOT/'vendor/sa-reference/source_game_sa_Entity_Vehicle_Automobile.cpp').read_text()
# PRINT_WITH_3_NUMBERS_BIG is present in retail, whereas native 036E reaches
# the default handler. Do not grant support by adding that numeric alias.
assert ps2.handler(883)['handler']==0x227E94
for address,word in [(0x227EEC,0x24060005),(0x227F04,0x96060010),
                     (0x227F14,0x24C6FFFF),(0x227F18,0x8E05000C),
                     (0x227F20,0x30C6FFFF),(0x227E84,0x0C0BE60A)]:
    assert ps2.word(address)==word,hex(address)
assert 'StaticRef<std::array<tBigMessage, 7>>' in (ROOT/'vendor/sa-reference/Messages.h').read_text()
print(f'PASS: {len(entity_specs)} entity family signatures plus statistic/environment/garage/traffic contracts, original numbered big-message ABI, original vehicle/attachment/damage operands, 18 retail PS2 locomotion identities and real player havoc counter, original photo/credits/control and camera/drowning/clip/boot/leave-and-die services, native widths/offsets and failure contracts; gameExecuted=false')
