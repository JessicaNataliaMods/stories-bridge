"""Stage a campaign code update while sharing immutable local game assets.

The public tool contains code only. Staged packages contain the user's private
game data and must not be redistributed. Metadata/ASI are regular copies;
unchanged data/model/audio assets use local hardlinks to avoid duplicating GBs.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import pathlib
import shutil
from pathlib import Path
from bootstrap_player_exports import export_player_globals, report as bootstrap_report

ROOT = pathlib.Path(__file__).resolve().parents[1]


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def immutable(relative):
    return relative.parts[0] in ('data', 'models', 'anim', 'text') or relative.parts[:2] == ('metadata', 'lcs-source')


def mutable_profile(relative, version):
    # Converted zones (v82) and timecycle (v83) are regenerated; other data stays shared.
    return (version >= 32 and relative.as_posix() == 'data/script/main.scm') or \
           (version >= 82 and relative.as_posix() in ('data/gta.dat', 'data/lcs/info.zon', 'data/lcs/map.zon')) or \
           (version >= 83 and relative.as_posix() == 'data/timecyc.dat') or \
           (version >= 84 and relative.as_posix() == 'models/lcs/paths.img') or \
           (version >= 94 and relative.as_posix() == 'data/lcs/native-equivalents.tsv')


PATH_KEYS = ('WorldDirectory', 'DataDirectory', 'ResourceDirectory', 'CutsceneDirectory', 'AudioDirectory')


def packaged_audio(relative):
    # From v90 the ASI reads the user's own PS2 AUDIO folder; no audio ships.
    parts = tuple(part.lower() for part in relative.parts)
    return parts[:3] == ('metadata', 'lcs-source', 'audio') or parts[:4] == ('data', 'lcs', 'cutscenes', 'audio')


def replace_mutable(target, data):
    # A file copied from the base may be a hardlink shared with the rollback:
    # write a new inode instead of modifying the shared one.
    temporary = target.with_name(target.name + '.new')
    temporary.write_bytes(data)
    os.replace(temporary, target)


def sa_weapon_models(game):
    names, section = {}, ''
    for line in (game/'data/default.ide').read_text(encoding='latin1').splitlines():
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        if ',' not in line:
            section = '' if line.lower() == 'end' else line.lower()
        elif section == 'weap':
            fields = [value.strip() for value in line.split(',')]
            names[int(fields[0])] = fields[1].lower()
    return names


def remount_weapons(output, campaign, game):
    """v94: every original weapon on its own SA weapon model (tools/weapon_slots.py),
    the minigun body and barrel in one clump, and SA weapon.dat values from the
    original WEAPON.DAT. Returns the weapon-visuals manifest."""
    slots_module = tool_module('weapon_slots')
    old_manifest = json.loads((output/'metadata/weapon-visuals.json').read_text(encoding='utf-8'))
    source_manifest = Path(old_manifest['sourceManifest'])
    if digest(source_manifest) != old_manifest['sourceManifestSha256']:
        raise ValueError('Weapon conversion provenance changed')
    converted = json.loads(source_manifest.read_text(encoding='utf-8'))
    if not converted['complete'] or converted['failures'] or converted['gameExecuted']:
        raise ValueError('Weapon conversion must be complete and verified offline')
    by_name = {row['originalName'].lower(): row for row in converted['converted']}
    models = {row['originalModelId']: row['originalName'].lower() for row in converted['converted']}
    sa_models = sa_weapon_models(game)
    sa_ids = {name: model for model, name in sa_models.items()}
    # Script model bindings follow the new slots.
    bindings = output/'data/lcs/native-equivalents.tsv'
    # The vehicle fleet record pins the bindings as mounted before this step;
    # keep that hash and a digest of the untouched non-weapon rows.
    entity_rows = [line for line in bindings.read_text(encoding='ascii').splitlines() if not line.endswith('\tobject')]
    campaign['weaponEquivalents94'] = dict(beforeSha256=digest(bindings),
        entityRowsSha256=hashlib.sha256('\n'.join(entity_rows).encode('ascii')).hexdigest())
    rows = []
    for line in bindings.read_text(encoding='ascii').splitlines():
        fields = line.split('\t')
        if len(fields) == 5 and fields[4] == 'object' and fields[2] in slots_module.WEAPON_TARGETS:
            target = slots_module.WEAPON_TARGETS[fields[2]]
            fields[1], fields[3] = str(sa_ids[target]), target
        rows.append('\t'.join(fields))
    replace_mutable(bindings, ('\n'.join(rows) + '\n').encode('ascii'))
    campaign['equivalentSha256'] = digest(bindings)
    groups = {}
    for name in models.values():
        groups.setdefault(slots_module.WEAPON_TARGETS[name], []).append(name)
    for row in old_manifest['files']:
        (output/row['relative']).unlink(missing_ok=True)
    slots, files = [], []
    rw = source_manifest.parent/'rw-assets'
    def source_bytes(row, extension, key):
        data = (rw/(row['asset'] + extension)).read_bytes()
        if hashlib.sha256(data).hexdigest() != row[key]:
            raise ValueError('Converted weapon changed: ' + row['asset'])
        return data
    for target, names in sorted(groups.items()):
        selected = slots_module.CANONICAL.get(target, names[0] if len(names) == 1 else None)
        if selected is None:
            raise ValueError('Shared weapon target needs a canonical original: ' + target)
        row = by_name[selected]
        dff, txd = source_bytes(row, '.dff', 'dffSHA256'), source_bytes(row, '.txd', 'txdSHA256')
        slot = dict(nativeModel=sa_ids[target], nativeName=target, selectedOriginal=row['originalName'],
                    originalModel=row['originalModelId'], aliases=sorted(set(names) - {selected}))
        if target == 'minigun':
            barrel = by_name['minigun2']
            dff = slots_module.assemble_minigun(dff, source_bytes(barrel, '.dff', 'dffSHA256'))
            txd = slots_module.merge_textures(txd, source_bytes(barrel, '.txd', 'txdSHA256'))
            slot['assembly'] = dict(barrel='minigun2', frame='minigun2', rule='LCS CPed minigun top atomic')
            slot['aliases'].remove('minigun2')
        for data, extension in ((dff, '.dff'), (txd, '.txd')):
            relative = 'weapons/' + target + extension
            (output/relative).parent.mkdir(parents=True, exist_ok=True)
            (output/relative).write_bytes(data)
            files.append(dict(relative=relative, sha256=digest(output/relative)))
        slots.append(slot)
    weapon_dat = output/'data/weapon.dat'
    weapon_dat.write_bytes(slots_module.convert_weapon_dat(
        (game/'data/weapon.dat').read_text(encoding='latin1'),
        (output/'metadata/lcs-source/DATA/WEAPON.DAT.bin').read_text(encoding='latin1'),
        models, sa_models).encode('latin1'))
    manifest = dict(format=2, sourceManifest=str(source_manifest), sourceManifestSha256=digest(source_manifest),
                    slots=slots, files=files, weaponDat=dict(relative='data/weapon.dat', sha256=digest(weapon_dat)),
                    installed=False, gameExecuted=False, behaviorChanged=True, audioChanged=False,
                    animationsChanged=False, handAlignmentRuntimeVerified=False)
    (output/'metadata/weapon-visuals.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    old_files = {row['relative'] for row in old_manifest['files']}
    campaign['originalResources'] = [row for row in campaign['originalResources']
                                     if row['relative'] not in old_files and row['relative'] != 'metadata/weapon-visuals.json']
    campaign['originalResources'] += files + [dict(relative='data/weapon.dat', sha256=digest(weapon_dat)),
        dict(relative='metadata/weapon-visuals.json', sha256=digest(output/'metadata/weapon-visuals.json'))]
    campaign['weaponVisuals'] = dict(manifest='metadata/weapon-visuals.json',
                                     sha256=digest(output/'metadata/weapon-visuals.json'), slots=len(slots))
    return manifest


def tool_module(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), ROOT/'tools'/(name+'.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def prepare(base, output, version, notes, refresh, original_data):
    base, output = base.resolve(), output.resolve()
    if base == output or base in output.parents or output in base.parents:
        raise ValueError('Stage must be separate from its input package')
    campaign = json.loads((base/'metadata/campaign.json').read_text())
    if campaign['mode'] != 'Campaign' or campaign['completeCampaignVerified'] or campaign['version'] >= version:
        raise ValueError('Invalid campaign base/version')
    if output.exists() and not refresh:
        raise ValueError('Output exists; use --refresh only after reviewing it')
    linked = 0
    if not output.exists():
        output.mkdir(parents=True)
    if not output.is_dir() or output.is_symlink() or output.is_junction():
        raise ValueError('Staging root must be a normal directory')
    for source in sorted(base.rglob('*')):
            if source.is_symlink() or source.is_junction():
                raise ValueError('Links in the source package need a separate audit')
            relative = source.relative_to(base)
            if version >= 90 and packaged_audio(relative):
                continue
            if source.is_dir():
                (output/relative).mkdir(exist_ok=True)
                continue
            if relative.parts[0] == 'metadata' and source.name.endswith('-notes.md'):
                continue
            if version >= 90 and packaged_audio(relative):
                continue
            target = output/relative
            if immutable(relative) and not mutable_profile(relative, version):
                if target.exists():
                    if target.is_symlink() or target.stat().st_size != source.stat().st_size or digest(target) != digest(source):
                        raise ValueError('Existing immutable stage resource differs: '+str(relative))
                else:
                    os.link(source, target)
                    linked += 1
            else:
                if target.exists() and target.stat().st_nlink != 1:
                    if not mutable_profile(relative, version):
                        raise ValueError('Mutable stage metadata shares an inode: '+str(relative))
                    target.unlink()  # drop only this stage's link to a regenerated data file
                shutil.copy2(source, target)
    source_asi = ROOT/'build/Release/LcsSaBridge.asi'
    target_asi = output/'LcsSaBridge.asi'
    if target_asi.exists() and target_asi.stat().st_nlink != 1:
        raise ValueError('Mutable ASI must not share an inode with a rollback')
    shutil.copy2(source_asi, target_asi)
    campaign['version'] = version
    campaign['nativeStartupValidated'] = False
    campaign.pop('startupValidationSha256', None)
    for row in campaign['originalResources']:
        if row['relative'] == 'LcsSaBridge.asi':
            row['sha256'] = digest(target_asi)
            row['source'] = str(source_asi)
    if version >= 27:
        source_weapons=original_data.resolve()/'DATA/WEAPON.DAT'
        relative='metadata/lcs-source/DATA/WEAPON.DAT.bin'
        target=output/relative
        if target.exists():
            if digest(target)!=digest(source_weapons):
                raise ValueError('Original weapon definitions changed')
        else:
            shutil.copy2(source_weapons,target)
        if not any(row['relative']==relative for row in campaign['originalResources']):
            campaign['originalResources'].append(dict(relative=relative,source=str(source_weapons),sha256=digest(source_weapons)))
        campaign['runtimeSafeguards'].update(originalWeaponInventorySlots=True,typedDeadCharacterVisibility=True,nativeThreatRadiusPolicy=True)
    # Original ferry track (FERRY1.DAT): optional source data read by the ferry service; absent data only leaves the
    # ferry commands refused, so it never invalidates an older stage.
    source_ferry=original_data.resolve()/'DATA/PATHS/FERRY1.DAT'
    if source_ferry.is_file():
        relative='metadata/lcs-source/DATA/PATHS/FERRY1.DAT.bin'
        target=output/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            if digest(target)!=digest(source_ferry):
                raise ValueError('Original ferry track changed')
        else:
            shutil.copy2(source_ferry,target)
        if not any(row['relative']==relative for row in campaign['originalResources']):
            campaign['originalResources'].append(dict(relative=relative,source=str(source_ferry),sha256=digest(source_ferry)))
    # Retail docked ferry collision (ms_colModelFerryDocked, COL1): optional derived asset pinned to the prepared bundle;
    # absent data only leaves the ferry collision switch refused.
    source_docked=ROOT/'out/ferry-assets-review-20261019/models/ferry-docked.col'
    if source_docked.is_file():
        if digest(source_docked)!='b8f16d0e223cc75da102c604a058e4640d10826321e3f109580c9a827992ecca':
            raise ValueError('Prepared docked ferry collision changed')
        relative='models/lcs/ferry-docked.col'
        target=output/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            if digest(target)!=digest(source_docked):
                raise ValueError('Docked ferry collision in the stage changed')
        else:
            shutil.copy2(source_docked,target)
        if not any(row['relative']==relative for row in campaign['originalResources']):
            campaign['originalResources'].append(dict(relative=relative,source=str(source_docked),sha256=digest(source_docked)))
    if version >= 29:
        campaign['runtimeSafeguards'].update(nativePacketEntryReceipts=True,nativeEnteredFailureRequiresRepair=True,sourcePlayerAreaShootingPredicate=True)
    if version >= 30:
        campaign['runtimeSafeguards'].update(nativeWeaponExpenditure=True,paidShopRegression=True,insufficientShopFundsRegression=True)
    if version >= 31:
        campaign['runtimeSafeguards'].update(nativeAnalogueAxes=True,nativeDebugCameraSource=True,nextVehicleComponentSelectors=True,liveTargettableObjectGuard=True)
    if version >= 32:
        bootstrap=output/'data/script/main.scm'
        if bootstrap.stat().st_nlink != 1:
            raise ValueError('Mutable bootstrap must not share an inode with a rollback')
        before=(base/'data/script/main.scm').read_bytes()
        after=export_player_globals(before)
        bootstrap.write_bytes(after)
        campaign['nativeBootstrapPlayerExports']=bootstrap_report(before,after)
        for row in campaign['preservedApprovedResources']:
            if row['relative']=='data/script/main.scm':row['sha256']=digest(bootstrap)
        campaign['runtimeSafeguards'].update(nativeModPlayerGlobals=True,livePlayerHandleRefreshBeforeCleo=True,
            sourceGangWeaponDamageMask=True,sourceDirectedWanderTask=True,sourceWaitInCarTask=True)
    campaign['runtimeSafeguards'].update(engineAdapterOwnership=True, consumedFailureFallback=False,
        spatialAudioPerVoiceHandles=True, originalNativeThreatScanFlags=True)
    if version >= 33:
        campaign['runtimeSafeguards'].update(sourceEntityFamilyPackets=True,sourcePhysicsUnits=True,
            boundedGenerationalEntityHandles=True,sourceOrientationAxisOrder=True,
            nativeSignedServiceStatistics=True,sourcePlayerRewardHudFlash=True,entityBackendFaultInjectionRegression=True)
    if version >= 34:
        campaign['runtimeSafeguards'].update(boundedGenerationalRadarHandles=True,sourceVehicleClassPredicates=True,
            sourceScriptFireParameterExpansion=True,sourceUnsignedModelKillCounters=True,infrastructureFamilyPreflightRegression=True)
    if version >= 35:
        campaign['runtimeSafeguards'].update(boundedGenerationalPickupHandles=True,sourcePickupIntegerRevenue=True,
            sourceBoatWaterState=True,verifiedHelicopterRotorField=True)
    if version >= 36:
        campaign['runtimeSafeguards'].update(typedOriginalRampages=True,nativeRampageHeadshotAbi=True,
            nativeRampageConflictGuard=True,interruptedRampageWeaponIdentity=True,rampageEnteredFailureRequiresRepair=True)
    if version >= 37:
        campaign['runtimeSafeguards'].update(ownedOriginalWeatherClockSnapshots=True,sourceWeatherRestoreFallthrough=True,
            sourceClockPreservesNativeCalendar=True,sourceClockPreservesNativeBackup=True,sourceVehicleStatusTranslation=True)
    if version >= 38:
        campaign['runtimeSafeguards'].update(typedOriginalGarageServices=True,namedGaragePredicateBinding=True,
            destructiveResprayPredicate=True,sourceBombTypeAndInstaller=True,garageServicePreflightRegression=True)
    if version >= 39:
        campaign['runtimeSafeguards'].update(typedOriginalTrafficServices=True,nativeCleanupCollisionRelease=True,sourceBootStateTransition=True,sourceTankDetonationFlag=True,boundedDrunkSteeringBuffer=True,trafficServicePreflightRegression=True)
    if version >= 40:
        campaign['runtimeSafeguards'].update(sourceFirstPersonWeaponModes=True,boundedNativeDrowningQuery=True,focusedPlayerClipStorage=True,guardedBootVisualRemoval=True)
    if version >= 41:
        campaign['runtimeSafeguards'].update(consumedOriginalPhotographLatch=True,observedNativeCreditsState=True,isolatedCameraControlRelease=True)
    if version >= 42:
        campaign['runtimeSafeguards'].update(ownedOriginalActivityRecords=True,retailPS2RecordInitializers=True,boundedOriginalRecordArrays=True,retailCostumeUnlockMask=True)
    if version >= 43:
        campaign['runtimeSafeguards'].update(retailLocomotionNameTranslation=True,boundedNativeAnimGroupLookup=True,isolatedPedLocomotionWrite=True,nativePlayerHavocCounter=True)
    if version >= 44:
        campaign['runtimeSafeguards'].update(retailOriginalFloatMaxima=True,hostFpuIndependentActivityRecords=True,retailSignedScoreConversion=True)
    if version >= 45:
        campaign['runtimeSafeguards'].update(originalVehicleAttachmentContracts=True,ownedAttachmentReferences=True,
            sourceHelicopterSpeedByte=True,liveVehicleRamController=True,guardedCharacterObjectAttachment=True)
    if version >= 46:
        campaign['runtimeSafeguards'].update(guardedCollisionConsumption=True,originalNonexplosiveCarWreck=True,
            fixedOriginalWreckDamageStates=True,vehicleDamageEnteredFailureRequiresRepair=True)
    if version >= 47:
        campaign['runtimeSafeguards'].update(typedThreeNumberBigMessages=True,narrowedOriginalBigMessageStyle=True,
            nativeBigMessageEnteredFailureRequiresRepair=True)
    if version >= 48:
        campaign['runtimeSafeguards'].update(originalDeliveryTouristFloatCounters=True,eeSingleGuardFloatAddition=True,
            hostFpuIndependentCounterSums=True)
    if version >= 49:
        campaign['runtimeSafeguards'].update(typedFreeResprayPolicy=True,independentResprayBombshopPolicies=True)
    if version >= 50:
        campaign['runtimeSafeguards'].update(retailOriginalWanderPath=True,boundedWanderWorldQueries=True,guardedWanderPedGeneration=True)
    if version >= 51:
        campaign['runtimeSafeguards'].update(originalIslandStatisticsSetters=True,retailIslandBooleanNormalization=True,
            experimentalObjectiveCaptureEnabled=False)
    if version >= 52:
        campaign['runtimeSafeguards'].update(typedLeavingVehicleToDie=True,readOnlyPrimaryGoalInspection=True,
            noVirtualGoalQueryCalls=True)
    if version >= 53:
        campaign['runtimeSafeguards'].update(typedRemoteVehicleExplosion=True,guardedFocusedRemoteVehicle=True,
            nativeRemoteExplosionEnteredFailureReceipt=True)
    if version >= 94 and not campaign.get('weaponSlots94'):
        game_directory = Path(json.loads((output/'metadata/map-preview.json').read_text())['gameDirectory'])
        weapons = remount_weapons(output, campaign, game_directory)
        campaign['weaponSlots94'] = dict(slots=len(weapons['slots']), weaponDat='data/weapon.dat',
            separatedOriginals=['chisel','hockeystick','handaxe','cleaver','machete','ingramsl','m60'],
            minigunBarrel='frame minigun2', sharedTargets={'sniper': ['laser'], 'brassknuckle': ['fingers']},
            animationsConverted=False, gameExecuted=False)
    if version >= 90:
        campaign['originalResources']=[row for row in campaign['originalResources']
                                       if not packaged_audio(pathlib.PurePosixPath(row['relative']))]
    if version >= 57:
        source_finale=ROOT / 'local/originals/lcs-ps2/AUDIO/MUSIC/FINALE.VB'
        expected='0490cd090214c4ef44bd8f419a5923484c24fb0b005761b9c535734cd526413c'
        if source_finale.stat().st_size!=3440640 or digest(source_finale)!=expected:
            raise ValueError('Original FINALE.VB profile differs')
        relative='AUDIO/MUSIC/FINALE.VB' if version >= 90 else 'metadata/lcs-source/AUDIO/MUSIC/FINALE.VB.bin'
        if version < 90:
            target=output/relative;target.parent.mkdir(parents=True,exist_ok=True)
            if target.exists():
                if digest(target)!=expected:raise ValueError('Staged original FINALE.VB differs')
            else:shutil.copy2(source_finale,target)
            if not any(row['relative']==relative for row in campaign['originalResources']):
                campaign['originalResources'].append(dict(relative=relative,source=str(source_finale),sha256=expected))
        campaign['endingTune']=dict(track=63,source=relative,sha256=expected,sourceBytes=3440640,
            decodedFrequency=32000,decodedChannels=2,decodedPcmBytes=12042240,
            frontendMode=0,stopMode=1,gameExecuted=False)
        campaign['runtimeSafeguards'].update(originalFinaleStream=True,ownedFinaleAudioPhase=True,
            originalFinaleModeTransitions=True,finaleEnteredFailureQuarantine=True,liveNativeMusicMasterAndFader=True)
    if version >= 64:
        source_city=ROOT / 'local/originals/lcs-ps2/AUDIO/MUSIC/CITY.VB'
        expected_city='eae0705d9e10fa59c8f6c552c7e4731f7a0c7718ab2b9b469848fb0e4025e93b'
        if source_city.stat().st_size!=21954560 or digest(source_city)!=expected_city:
            raise ValueError('Original CITY.VB profile differs')
        relative_city='AUDIO/MUSIC/CITY.VB' if version >= 90 else 'metadata/lcs-source/AUDIO/MUSIC/CITY.VB.bin'
        if version < 90:
            target_city=output/relative_city;target_city.parent.mkdir(parents=True,exist_ok=True)
            if target_city.exists():
                if digest(target_city)!=expected_city:raise ValueError('Staged original CITY.VB differs')
            else:shutil.copy2(source_city,target_city)
            if not any(row['relative']==relative_city for row in campaign['originalResources']):
                campaign['originalResources'].append(dict(relative=relative_city,source=str(source_city),sha256=expected_city))
        campaign['cityMusic']=dict(source=relative_city,sha256=expected_city,sourceBytes=21954560,
            retailAreaTracks={'5':17,'17':18},selectors=list(range(40,46)),gameExecuted=False)
    if version >= 66:
        for name in ('DEFAULT.IDE', 'HANDLING.CFG'):
            source_catalog=original_data.resolve()/'DATA'/name
            relative_catalog='metadata/lcs-source/DATA/'+name+'.bin'
            target_catalog=output/relative_catalog
            expected_catalog=digest(source_catalog)
            if target_catalog.exists():
                if digest(target_catalog)!=expected_catalog:raise ValueError('Staged source vehicle catalogue differs: '+name)
            else:shutil.copy2(source_catalog,target_catalog)
            if not any(row['relative']==relative_catalog for row in campaign['originalResources']):
                campaign['originalResources'].append(dict(relative=relative_catalog,source=str(source_catalog),sha256=expected_catalog))
        campaign['runtimeSafeguards']['originalVehicleCatalogueStaged']=True
    if version >= 58:
        campaign['runtimeSafeguards'].update(originalRemoteCancellation=True,
            focusedPedGenerationCancellation=True,observedNativeSaveMenuState=True,
            generationOwnedPassengerSeatPolicy=True,originalPickupFloatRadius=True)
        campaign['localBatch58']=dict(sourceCommands=[990,1485,1487,1512],
            withheldPendingProof=[334,1586],webReturn='R11 corrected subset',gameExecuted=False)
    if version >= 59:
        campaign['runtimeSafeguards'].update(checkedPcmReleaseBeforeReuse=True,
            stablePcmHeaderLifetime=True,retainedPcmDriverRefusal=True,originalAudioLoopMetadata=True)
        campaign['audioStorage59']=dict(newSourceCommands=[],continuous402403Registered=False,
            finalDriverRefusal='retained Voice/header/PCM for process lifetime',gameExecuted=False)
    if version >= 60:
        campaign['runtimeSafeguards'].update(persistentScriptCoronas=True,privateStableCoronaIdentities=True,
            guardedCoronaGroundXY=True,coronaEnteredFaultQuarantine=True,sharedCoronaPoolOwnership=True)
        campaign['scriptCoronas60']=dict(sourceCommands=[1481,1482],nativeRegister='6FC580',
            groundXY='569660',drawDistance=450,gameExecuted=False,renderVisibilityClaimed=False)
    if version >= 61:
        campaign['runtimeSafeguards'].update(originalCollectiveTable=True,
            generationCheckedCollectiveMembers=True,collectivePartialFailureQuarantine=True)
        campaign['collectives61']=dict(sourceCommands=[636,638,616],sharedCapacity=32,
            worldQueryLimit=64,vehicleOccupants=True,gameExecuted=False)
    if version >= 62:
        campaign['runtimeSafeguards'].update(originalHudCounterPresentation=True,
            ownedNativeHudDraw=True,foreignHudAndFontPreserved=True,
            retailCounterTotalClamp=True,continuous402403Registered=False)
        campaign['hudPresentation62']=dict(sourceCommands=[1576,1577,1634,1636,1644],
            missionHudCall='58FBEE',nativeHudDraw='58B180',originalOnlyRgbRendering=True,
            newGameAssets=False,gameExecuted=False,visualGameplayVerified=False)
    if version >= 63:
        markers=ROOT/'out/script-marker-resources'
        marker_manifest=json.loads((markers/'manifest.json').read_text())
        if not marker_manifest.get('valid') or marker_manifest.get('gameExecuted') or \
           [r['type'] for r in marker_manifest['meshes']]!=[1,2,5] or \
           marker_manifest['texture'].get('originalPixelsPreserved') is not True:
            raise ValueError('Verified original marker resources missing')
        marker_files=[('rw-assets/'+r['file'],r['sha256']) for r in marker_manifest['meshes']]
        marker_files.append((marker_manifest['texture']['file'].replace('\\','/'),marker_manifest['texture']['sha256']))
        for relative,expected in marker_files:
            source=markers/relative;target=output/'markers'/relative
            if digest(source)!=expected:raise ValueError('Original marker resource changed: '+relative)
            target.parent.mkdir(parents=True,exist_ok=True)
            if target.exists() and target.stat().st_nlink!=1:raise ValueError('Mutable marker target shares rollback inode')
            shutil.copy2(source,target)
        shutil.copy2(markers/'manifest.json',output/'metadata/script-marker-resources.json')
        campaign['runtimeSafeguards'].update(originalScriptMarkerIdentity=True,
            privateOriginalMarkerPool=True,ownedNativeMarkerDraw=True,
            sourceMarkerCopyFadeLifetime=True,nativeMarkerCollisionAndGround=True)
        campaign['scriptMarkers63']=dict(sourceCommands=[1579,1611],capacity=32,
            sourceTypes=[2,5],nativeTypeAliases=False,frameCall='725067',shutdownCall='723391',
            resourceManifest='metadata/script-marker-resources.json',gameExecuted=False,
            visualGameplayVerified=False)
    if version >= 82:
        # Native SA zones from the user's GTA3.ZON (tools/convert-zones.py): LCS
        # name zones replace SA's INFO.ZON, one LCS map zone replaces MAP.ZON.
        source_zones = output/'metadata/lcs-source/DATA/GTA3.ZON.bin'
        gxt = output/'text/american.gxt'
        info, mapzones, zone_report = tool_module('convert-zones').convert(source_zones.read_bytes().decode('ascii'), gxt)
        written = {}
        for name, text in (('info.zon', info), ('map.zon', mapzones)):
            target = output/'data/lcs'/name
            replace_mutable(target, text.encode('ascii'))
            written[name] = digest(target)
        profile = output/'data/gta.dat'
        lines = (base/'data/gta.dat').read_text(encoding='ascii').splitlines()
        replaced = {r'IPL DATA\MAP.ZON': r'IPL DATA\LCS\MAP.ZON', r'IPL DATA\INFO.ZON': r'IPL DATA\LCS\INFO.ZON'}
        if sum(line in replaced for line in lines) != 2 and sum(line in replaced.values() for line in lines) != 2:
            raise ValueError('Base gta.dat must register exactly the native or converted MAP.ZON and INFO.ZON')
        temporary = profile.with_suffix('.dat.new')
        temporary.write_text('\n'.join(replaced.get(line, line) for line in lines) + '\n', encoding='ascii')
        os.replace(temporary, profile)  # breaks the shared rollback inode
        zone_report.update(source='metadata/lcs-source/DATA/GTA3.ZON.bin', sourceSha256=digest(source_zones),
                           gxtSha256=digest(gxt), infoSha256=written['info.zon'], mapSha256=written['map.zon'],
                           profileSha256=digest(profile))
        campaign['zones82'] = zone_report
    if version >= 83:
        # Retail LCS timecycle (GAME.DTZ) over the user's native SA timecyc.dat
        # (tools/convert-timecyc.py); SA-only fields stay native.
        rom = ROOT/'local/originals/lcs-ps2'
        native = pathlib.Path(json.loads((output/'metadata/map-preview.json').read_text())['gameDirectory'])/'data/timecyc.dat'
        text, cycle_report = tool_module('convert-timecyc').convert_files(rom, native)
        target = output/'data/timecyc.dat'
        replace_mutable(target, text.encode('ascii'))
        cycle_report.update(rom=str(rom), native=str(native), outputSha256=digest(target))
        campaign['timecycle83'] = cycle_report
    if version >= 84:
        # Navigation re-exported by tools/ps2_paths.py with SA's storage rule
        # (each car path link in the area of its own position).
        # v86 orients links at their lower-address endpoint; v87 adds native pedestrian link fields.
        navigation = ROOT/('out/ps2-navigation-v87' if version >= 87 else 'out/ps2-navigation-v86' if version >= 86 else 'out/ps2-navigation-v84')
        audit = tool_module('traffic_profile').audit
        graph = audit.validate(navigation/'paths.img')
        conversion = json.loads((navigation/'conversion.json').read_text())
        if graph['PathsImgSha256'] != conversion['pathsImgSha256'] or not graph['VehicleNodes'] or not graph['PedNodes']:
            raise ValueError('Converted navigation missing or hash mismatch')
        target = output/'models/lcs/paths.img'
        replace_mutable(target, (navigation/'paths.img').read_bytes())
        for row in campaign['preservedApprovedResources']:
            if row['relative'] == 'models/lcs/paths.img':
                row['sha256'] = digest(target)
        preview_path = output/'metadata/map-preview.json'
        preview = json.loads(preview_path.read_text())
        preview['navigation']['graph'] = graph
        preview_path.write_text(json.dumps(preview, indent=2))
        (output/'metadata/navigation.json').write_text(json.dumps(dict(conversion=conversion, nativeValidation=graph), indent=2), encoding='utf-8')
        campaign['navigation84'] = dict(source=str(navigation), pathsImgSha256=graph['PathsImgSha256'],
                                        linkStorage='area of link position (native SA rule)', gameExecuted=False)
    if version >= 90:
        if version >= 92:
            campaign['originalRadio92'] = dict(stations=10, setting='LcsRadio', default=1,
                saState='CAERadioTrackManager 0x8CB6F8 (IsRadioOn, active station +0xE9)',
                silencedNativeStream='music master CAEAudioHardware+0x410 while an LCS station plays',
                volume='menu radio volume 0xBA6798 times fader +0x418; 25 during mission speech', gameExecuted=False)
        campaign['originalAudio90'] = dict(packagedAudio=False, folder='AUDIO next to LcsSaBridge.asi', banks=67, sampleRecords=5661,
            refusedHeaderlessEngineRecords=27, streams=['NEWS', 'CUTSCENE', 'MUSIC/CITY.VB', 'MUSIC/FINALE.VB'],
            gameExecuted=False)
        # Self-contained package: the ASI derives every folder from its own
        # location, so the INI carries no paths.
        ini_path = output/'LcsSaBridge.ini'
        ini = ini_path.read_bytes().decode('utf-16')
        kept = [line for line in ini.splitlines() if line.split('=', 1)[0] not in PATH_KEYS]
        if version >= 92 and not any(line.startswith('LcsRadio=') for line in kept):
            kept.append('LcsRadio=1')  # docs/audio.md: 0 keeps SA's radio
        if kept != ini.splitlines():
            if ini_path.stat().st_nlink != 1:
                raise ValueError('Mutable INI must not share an inode with a rollback')
            ini_path.write_bytes(('\r\n'.join(kept)+'\r\n').encode('utf-16'))
        readme = output/'LEIA.txt'
        notice = ('Audio: copie a pasta AUDIO do seu LCS de PS2 (SLUS-21423) para esta pasta, ao lado do '
                  'LcsSaBridge.asi. Nenhum audio vem neste pacote.\n')
        text = readme.read_text(encoding='utf-8')
        if notice not in text:
            if readme.stat().st_nlink != 1:
                raise ValueError('Mutable readme must not share an inode with a rollback')
            readme.write_text(text.rstrip('\n')+'\n'+notice, encoding='utf-8')
    (output/'metadata/campaign.json').write_text(json.dumps(campaign, indent=2))
    preview = json.loads((output/'metadata/map-preview.json').read_text())
    preview['version'] = str(version)
    (output/'metadata/map-preview.json').write_text(json.dumps(preview, indent=2))
    shutil.copy2(notes, output/'metadata'/f'campaign-v{version}-notes.md')
    preserved = 0
    for source in base.rglob('*'):
        relative = source.relative_to(base)
        if version >= 90 and packaged_audio(relative):
            continue
        if source.is_file() and immutable(relative) and not mutable_profile(relative, version):
            if digest(source) != digest(output/relative):
                raise ValueError('Original immutable resource changed: '+str(relative))
            preserved += 1
    return dict(stage=str(output), version=version, codeOnlyUpdate=version<57, immutableResourcesPreserved=preserved,
                hardlinkedNewAssets=linked, asiSha256=digest(target_asi), gameExecuted=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--version', type=int, required=True)
    parser.add_argument('--notes', type=pathlib.Path, required=True)
    parser.add_argument('--refresh', action='store_true')
    parser.add_argument('--original-data',type=pathlib.Path,default=ROOT/'out/lcs-converted')
    args = parser.parse_args()
    print(json.dumps(prepare(args.base, args.output, args.version, args.notes, args.refresh,args.original_data), indent=2))
