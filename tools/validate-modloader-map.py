"""Independently compare the install preview against original converted files."""
import argparse
import hashlib
import json
import pathlib
import struct
import importlib.util
from traffic_profile import audit as path_audit
from native_collisions import retained_definitions, validate as validate_native_collisions
from gameplay_ped_profile import validate as validate_gameplay_peds
from bootstrap_player_exports import export_player_globals
import weapon_slots
import weapon_animations
from map_effects import expected_effects, inject_dff


def collision_records(path):
    data = path.read_bytes()
    offset = 0
    while offset < len(data):
        size = struct.unpack_from('<I', data, offset + 4)[0] + 8
        if data[offset:offset + 4] != b'COL2' or size < 104 or offset + size > len(data):
            raise ValueError('Invalid native collision record')
        yield data[offset:offset + size]
        offset += size


def validate_campaign(package, map_report):
    if map_report['mode'] != 'Campaign':
        return dict(enabled=False)
    campaign=json.loads((package/'metadata/campaign.json').read_text())
    if campaign.get('version',0)>=75 or campaign.get('garageDoors'):
        spec=importlib.util.spec_from_file_location('mounted_garage_doors',pathlib.Path(__file__).with_name('mount-campaign-garage-doors.py'))
        doors=importlib.util.module_from_spec(spec);spec.loader.exec_module(doors)
        doors.audit(package)
    if campaign['gameExecuted'] or campaign['completeCampaignVerified'] or not map_report['campaignEnabled']:
        raise ValueError('Invalid campaign claims/mode')
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream,'sha256').hexdigest()
    for row in campaign['originalResources']+campaign['preservedApprovedResources']:
        target=(package/row['relative']).resolve()
        if package.resolve() not in target.parents or digest(target)!=row['sha256'].lower():
            raise ValueError('Original campaign or approved map/skin resource changed: '+row['relative'])
    if campaign.get('version',0)>=32:
        code=(package/'data/script/main.scm').read_bytes()
        source_code=(pathlib.Path(campaign['basePackage'])/'data/script/main.scm').read_bytes()
        exports=campaign['nativeBootstrapPlayerExports']
        if export_player_globals(source_code)!=code or exports['sha256']!=digest(package/'data/script/main.scm') or \
           exports['playerIndexByteOffset']!=8 or exports['playerActorByteOffset']!=12 or \
           not exports['initializedBeforeFirstYield'] or not exports['refreshedBeforeNativeScripts'] or \
           exports['gameExecuted'] or exports['modsRuntimeVerified']:
            raise ValueError('Native mod player exports changed beyond reviewed bootstrap operands')
    original=package/'metadata/lcs-source'
    if campaign.get('version',0)>=66:
        for name in ('DEFAULT.IDE','HANDLING.CFG'):
            relative='metadata/lcs-source/DATA/'+name+'.bin'
            rows=[row for row in campaign['originalResources']if row['relative']==relative]
            if len(rows)!=1 or not (original/'DATA'/(name+'.bin')).is_file():
                raise ValueError('Missing original vehicle catalogue consumed by ASI: '+name)
    if campaign.get('version',0)>=90:
        audio=campaign.get('originalAudio90',{})
        if audio.get('packagedAudio') is not False or audio.get('sampleRecords')!=5661 or audio.get('gameExecuted') is not False:
            raise ValueError('Runtime PS2 audio metadata missing')
        for folder in ('metadata/lcs-source/audio','metadata/lcs-source/AUDIO','data/lcs/cutscenes/audio','AUDIO'):
            if (package/folder).exists():raise ValueError('Package must not contain audio: '+folder)
        if any(row['relative'].lower().startswith(('metadata/lcs-source/audio/','data/lcs/cutscenes/audio/')) for row in campaign['originalResources']):
            raise ValueError('Packaged audio still listed as an original resource')
        ini_lines=(package/'LcsSaBridge.ini').read_text(encoding='utf-16').splitlines()
        if any(line.split('=',1)[0].endswith('Directory') for line in ini_lines):
            raise ValueError('Self-contained campaign INI must not carry paths')
        if campaign.get('version',0)>=92 and not any(line in ('LcsRadio=0','LcsRadio=1') for line in ini_lines):
            raise ValueError('LcsRadio setting missing')
    if campaign.get('version',0)>=57:
        expected='0490cd090214c4ef44bd8f419a5923484c24fb0b005761b9c535734cd526413c'
        user_audio=campaign.get('version',0)>=90
        relative='AUDIO/MUSIC/FINALE.VB' if user_audio else 'metadata/lcs-source/AUDIO/MUSIC/FINALE.VB.bin'
        tune=campaign.get('endingTune',{})
        if tune!=dict(track=63,source=relative,sha256=expected,sourceBytes=3440640,
            decodedFrequency=32000,decodedChannels=2,decodedPcmBytes=12042240,
            frontendMode=0,stopMode=1,gameExecuted=False) or \
            (not user_audio and ((package/relative).stat().st_size!=3440640 or digest(package/relative)!=expected or
            not any(row['relative']==relative and row['sha256']==expected for row in campaign['originalResources']))):
            raise ValueError('Original finale stream or mode metadata differs')
    if campaign.get('version',0)>=27:
        relative='metadata/lcs-source/DATA/WEAPON.DAT.bin'
        if not any(row['relative']==relative for row in campaign['originalResources']):
            raise ValueError('Missing original mission weapon definitions')
        # v94+: data/weapon.dat is the SA table converted from the original
        # WEAPON.DAT (tools/weapon_slots.py); it is checked with the weapon slots.
        if campaign.get('version',0)<94 and list(package.rglob('weapon.dat')):
            raise ValueError('Original weapon data must not override SA weapon.dat')
        if campaign.get('version',0)>=94 and list(package.rglob('weapon.dat'))!=[package/'data/weapon.dat']:
            raise ValueError('Converted weapon.dat must be the only SA weapon table')
    if campaign.get('version',0)>=22:
        interiors=json.loads((package/'metadata/campaign-interiors.json').read_text())
        if interiors['gameExecuted'] or len(interiors['models'])!=15 or any(not row['nativeMeshReread'] or not row['triangles'] or row['vertices']>65535 for row in interiors['models']):
            raise ValueError('Invalid original interior render inventory')
        for row in interiors['files']:
            if digest(package/row['relative'])!=row['sha256']:raise ValueError('Interior render input changed: '+row['relative'])
    if digest(original/'DATA/MAIN.SCM.bin')!=campaign['originalScriptSha256'] or campaign['originalScriptSha256']!='2ab65c950b86f02078b56fbac10724c0bf90eb2329bddc7a480c98cc8a7a65d4':
        raise ValueError('Original LCS script changed')
    ini=dict(line.split('=',1) for line in (package/'LcsSaBridge.ini').read_text(encoding='utf-16').splitlines() if '=' in line)
    for key,value in {'Mode':'Campaign','OriginalMapRenderRules':'1','PlayerSkins':'1','GameplaySkins':'1','EquivalentEntities':'1','SourceFileSuffix':'.bin'}.items():
        if ini.get(key)!=value:raise ValueError('Invalid campaign configuration: '+key)
    live=pathlib.Path(map_report['gameDirectory'])/'modloader/stories_bridge'
    for key,relative in (('WorldDirectory','data/lcs'),('DataDirectory','metadata/lcs-source'),('ResourceDirectory',''),('CutsceneDirectory','data/lcs/cutscenes')):
        if campaign.get('version',0)>=90:break  # paths derived from the ASI location
        if pathlib.Path(ini[key])!=live/relative:raise ValueError('Campaign resource path leaves installed package: '+key)
    # Exactly one native bootstrap and one mounted file per language. Original
    # PS2 files are opaque .bin resources, never second Modloader replacements.
    if list(package.rglob('main.scm'))!=[package/'data/script/main.scm']:
        raise ValueError('Campaign source would replace native bootstrap')
    for language in ('american','french','german','italian','spanish'):
        if list(package.rglob(language+'.gxt'))!=[package/'text'/f'{language}.gxt']:
            raise ValueError('Duplicate mounted mission language')
    canonical=[line.split('\t') for line in (package/'data/lcs/peds/canonical-model-bindings.tsv').read_text().splitlines()]
    named={row[2].lower():int(row[1]) for row in canonical}
    if len(named)!=138 or campaign['namedSpecialCharacterSlots']!=21:
        raise ValueError('Incomplete named gameplay character inventory')
    equivalents=package/'data/lcs/native-equivalents.tsv'
    primary_file=package/'data/lcs/campaign-model-bindings.tsv'
    primary=[line.split('\t') for line in primary_file.read_text().splitlines()]
    mounted_world={tuple(line.split('\t')) for line in (package/'data/lcs/model-bindings.tsv').read_text().splitlines()}
    anchor_file=package/'data/lcs/campaign-anchor-bindings.tsv'
    if anchor_file.exists():mounted_world.update(tuple(line.split('\t')) for line in anchor_file.read_text().splitlines())
    if digest(primary_file)!=campaign['primaryWorldBindingSha256'] or len(primary)!=campaign['primaryWorldBindings'] or len({row[0] for row in primary})!=len(primary) or any(tuple(row) not in mounted_world for row in primary):
        raise ValueError('Primary campaign world bindings differ from actual mounted models')
    if digest(equivalents)!=campaign['equivalentSha256']:raise ValueError('Campaign native equivalents changed')
    table={int(row[0]):row for row in (line.split('\t') for line in equivalents.read_text().splitlines())}
    for row in canonical:
        if row[0]=='None':continue
        entry=table[int(row[0])]
        if entry[1]!=row[1] or entry[2].lower()!=row[2].lower() or entry[3]!=row[3] or entry[4]!='ped':
            raise ValueError('Mission requests an unrelated donor skin: '+row[2])
    scenes=(package/'data/lcs/cutscenes/index.tsv').read_text().splitlines()
    _,cuts,models=scenes[0].split('\t')
    if digest(package/'anim/cuts.img').upper()!=cuts or digest(package/'models/cutscene.img').upper()!=models:
        raise ValueError('Mounted cutscene archive differs from runtime index')
    names={}
    for line in scenes[1:]:
        name,alias,audio=line.split('\t')
        # v90+: lcs-ps2-audio-source-tests checks each decoded original stream
        # against this hash in the startup proof.
        if name in names or (campaign.get('version',0)<90 and digest(package/'data/lcs/cutscenes/audio'/f'{name}.wav').upper()!=audio):
            raise ValueError('Cutscene identity/audio differs')
        names[name]=alias
    if not {'intro','homswet'}.issubset(names) or len(names)!=campaign['preparedCutscenes']:
        raise ValueError('Opening movie missing from campaign')
    named_blocks=dict(line.split('\t') for line in (package/'data/lcs/animations/named/index.tsv').read_text().splitlines())
    if 'riot' not in named_blocks:raise ValueError('Original airport riot block missing')
    if campaign.get('version',0)>=21:
        animation_directory=package/'data/lcs/animations'
        storage=json.loads((animation_directory/'storage.json').read_text())
        if digest(animation_directory/'storage.json')!=campaign['animationStorage']['sha256'] or storage['nativeBlocks']!=2 or storage['automaticStreamingBlocks']!=0 or storage['logicalGestureGroups']!=25 or storage['states']!=71:
            raise ValueError('Unsafe campaign animation storage/budget')
        if list(animation_directory.rglob('*.ifp')):
            raise ValueError('Loose mission IFP files would overflow Modloader animation registration')
        for row in storage['files']:
            if digest(animation_directory/row['path'])!=row['sha256']:
                raise ValueError('Private mission animation changed')
    if campaign.get('nativeStartupValidated'):
        proof_file=package/'metadata/campaign-startup-validation.json'
        if digest(proof_file)!=campaign.get('startupValidationSha256'):raise ValueError('Campaign startup proof missing or changed')
        proof=json.loads(proof_file.read_text())
        expected_checks=22 if campaign.get('version',0)>=50 else 21 if campaign.get('version',0)>=47 else 20 if campaign.get('version',0)>=45 else 19 if campaign.get('version',0)>=44 else 18 if campaign.get('version',0)>=42 else 17 if campaign.get('version',0)>=39 else 16 if campaign.get('version',0)>=38 else 15 if campaign.get('version',0)>=37 else 14 if campaign.get('version',0)>=36 else 13 if campaign.get('version',0)>=34 else 12 if campaign.get('version',0)>=33 else 8 if campaign.get('version',0)>=32 else 6 if campaign.get('version',0)>=31 else 5 if campaign.get('version',0)>=30 else 3
        if campaign.get('version',0)>=63:
            expected_checks+=5
            marker=campaign.get('scriptMarkers63',{})
            if marker.get('sourceCommands')!=[1579,1611] or marker.get('capacity')!=32 or \
               marker.get('nativeTypeAliases') is not False or marker.get('visualGameplayVerified') is not False:
                raise ValueError('Invalid private source marker scope')
            if not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                ('originalScriptMarkerIdentity','privateOriginalMarkerPool','ownedNativeMarkerDraw',
                 'sourceMarkerCopyFadeLifetime','nativeMarkerCollisionAndGround')):
                raise ValueError('Source marker ownership/lifetime safeguards missing')
            required={'lcs-script-markers-tests.log','lcs-script-marker-pool-tests.log',
                      'lcs-script-marker-render-tests.log','original-marker-contracts.log',
                      'validate-script-marker-resources.log'}
            if not required.issubset({row['output'] for row in proof['checks'] if row['passed']}):
                raise ValueError('Original marker source/native/resources checks missing')
        if campaign.get('version',0)>=62:
            expected_checks+=3
            if not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                       ('originalHudCounterPresentation','ownedNativeHudDraw','foreignHudAndFontPreserved','retailCounterTotalClamp')):
                raise ValueError('Reviewed owned HUD presentation safeguards missing')
            hud=campaign.get('hudPresentation62',{})
            if hud.get('sourceCommands')!=[1576,1577,1634,1636,1644] or hud.get('visualGameplayVerified') is not False or \
               campaign['runtimeSafeguards'].get('continuous402403Registered') is not False:
                raise ValueError('Invalid HUD scope or partial continuous-sound registration')
            required={'lcs-hud-tests.exe','lcs-hud-render-tests.exe'}
            if not required.issubset({row['executable'] for row in proof['checks'] if row['passed']}) or \
               not any(row['passed'] and row['output']=='original-hud-presentation-contracts.log' for row in proof['checks']):
                raise ValueError('HUD VM/native-render/original contract checks missing')
        if campaign.get('version',0)>=61:
            expected_checks+=2
            if not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                    ('originalCollectiveTable','generationCheckedCollectiveMembers','collectivePartialFailureQuarantine')):
                raise ValueError('Reviewed collective safeguards missing')
        if campaign.get('version',0)>=60:
            expected_checks+=3
            if not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                    ('persistentScriptCoronas','privateStableCoronaIdentities','guardedCoronaGroundXY',
                     'coronaEnteredFaultQuarantine','sharedCoronaPoolOwnership')):
                raise ValueError('Reviewed script corona safeguards missing')
        if campaign.get('version',0)>=59:
            expected_checks+=1
            if not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                    ('checkedPcmReleaseBeforeReuse','stablePcmHeaderLifetime',
                     'retainedPcmDriverRefusal','originalAudioLoopMetadata')):
                raise ValueError('Reviewed v59 PCM storage safeguards missing')
        if campaign.get('version',0)>=58:
            expected_checks+=4
            if not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                       ('originalRemoteCancellation','focusedPedGenerationCancellation',
                        'observedNativeSaveMenuState','generationOwnedPassengerSeatPolicy',
                        'originalPickupFloatRadius')):
                raise ValueError('Reviewed v58 remote/menu/seat/pickup safeguards missing')
        if campaign.get('version',0)>=57:
            expected_checks+=4
            required={'lcs-ending-tune-transport-tests.exe','lcs-owned-audio-output-tests.exe','lcs-ending-tune-source-tests.exe'}
            if not required.issubset({row['executable'] for row in proof['checks'] if row['passed']}) or \
                (campaign.get('version',0)<90 and not any(row['relative']=='metadata/lcs-source/AUDIO/MUSIC/FINALE.VB.bin' for row in proof['inputs'])):
                raise ValueError('Finale startup checks or authenticated input missing')
        if campaign.get('version',0)>=94:
            expected_checks+=1
        if campaign.get('version',0)>=92:
            expected_checks+=1
            if 'lcs-radio-tests.exe' not in {row['executable'] for row in proof['checks'] if row['passed']}:
                raise ValueError('LCS radio check missing')
        if campaign.get('version',0)>=90:
            expected_checks+=1
            if 'lcs-ps2-audio-source-tests.exe' not in {row['executable'] for row in proof['checks'] if row['passed']}:
                raise ValueError('Runtime PS2 audio source check missing')
        if not proof['valid'] or not proof['engineMocked'] or proof['gameExecuted'] or proof['campaignPlayable'] or len(proof['checks'])!=expected_checks:raise ValueError('Campaign offline startup proof has invalid scope')
        if campaign.get('version',0)>=30:
            if campaign.get('version',0)>=50 and not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                    ('retailOriginalWanderPath','boundedWanderWorldQueries','guardedWanderPedGeneration')):
                raise ValueError('Original wander world-query safeguards missing')
            if campaign.get('version',0)>=48 and not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                    ('originalDeliveryTouristFloatCounters','eeSingleGuardFloatAddition','hostFpuIndependentCounterSums')):
                raise ValueError('Original delivery/tourist float safeguards missing')
            if campaign.get('version',0)>=49 and not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                    ('typedFreeResprayPolicy','independentResprayBombshopPolicies')):
                raise ValueError('Typed Pay and Spray policy safeguards missing')
            if campaign.get('version',0)>=47 and not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                    ('typedThreeNumberBigMessages','narrowedOriginalBigMessageStyle','nativeBigMessageEnteredFailureRequiresRepair')):
                raise ValueError('Original numbered big-message safeguards missing')
            if campaign.get('version',0)>=46 and not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                    ('guardedCollisionConsumption','originalNonexplosiveCarWreck','fixedOriginalWreckDamageStates','vehicleDamageEnteredFailureRequiresRepair')):
                raise ValueError('Original vehicle damage safeguards missing')
            if campaign.get('version',0)>=45 and not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                    ('originalVehicleAttachmentContracts','ownedAttachmentReferences','sourceHelicopterSpeedByte','liveVehicleRamController','guardedCharacterObjectAttachment')):
                raise ValueError('Original vehicle/attachment safeguards missing')
            if campaign.get('version',0)>=44 and not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                    ('retailOriginalFloatMaxima','hostFpuIndependentActivityRecords','retailSignedScoreConversion')):
                raise ValueError('Original float maximum/conversion safeguards missing')
            if campaign.get('version',0)>=43 and not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                    ('retailLocomotionNameTranslation','boundedNativeAnimGroupLookup','isolatedPedLocomotionWrite','nativePlayerHavocCounter')):
                raise ValueError('Original locomotion translation/storage safeguards missing')
            if campaign.get('version',0)>=42 and not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                    ('ownedOriginalActivityRecords','retailPS2RecordInitializers','boundedOriginalRecordArrays','retailCostumeUnlockMask')):
                raise ValueError('Original activity record/costume safeguards missing')
            if campaign.get('version',0)>=41 and not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                    ('consumedOriginalPhotographLatch','observedNativeCreditsState','isolatedCameraControlRelease')):
                raise ValueError('Original photo/credits/control safeguards missing')
            if campaign.get('version',0)>=40 and not all(campaign.get('runtimeSafeguards',{}).get(key) is True for key in
                    ('sourceFirstPersonWeaponModes','boundedNativeDrowningQuery','focusedPlayerClipStorage','guardedBootVisualRemoval')):
                raise ValueError('Original camera/drowning/clip/boot safeguards missing')
            required_outputs={'lcs-one-off-sound-tests.log','lcs-campaign-package-tests.log','lcs-campaign-equivalents-tests.log',
                              'lcs-campaign-equivalents-tests-paid-shop.log','lcs-campaign-equivalents-tests-empty-shop.log'}
            if campaign.get('version',0)>=31:required_outputs.add('lcs-campaign-shared-commands-tests.log')
            if campaign.get('version',0)>=32:required_outputs.update(('lcs-native-abi-tests.log','native-bootstrap-player-exports.log'))
            if campaign.get('version',0)>=33:required_outputs.update(('lcs-campaign-entity-family-tests.log','lcs-campaign-statistics-family-tests.log',
                'lcs-native-entity-backend-tests.log','source-family-contracts.log'))
            if campaign.get('version',0)>=34:required_outputs.add('lcs-campaign-infrastructure-family-tests.log')
            if campaign.get('version',0)>=36:required_outputs.add('lcs-campaign-frenzy-family-tests.log')
            if campaign.get('version',0)>=37:required_outputs.add('lcs-campaign-environment-family-tests.log')
            if campaign.get('version',0)>=38:required_outputs.add('lcs-campaign-garage-family-tests.log')
            if campaign.get('version',0)>=39:required_outputs.add('lcs-campaign-traffic-family-tests.log')
            if campaign.get('version',0)>=42:required_outputs.add('lcs-activity-statistics-tests.log')
            if campaign.get('version',0)>=44:required_outputs.add('original-activity-contracts.log')
            if campaign.get('version',0)>=45:required_outputs.add('original-vehicle-contracts.log')
            if campaign.get('version',0)>=47:required_outputs.add('lcs-mission-message-tests.log')
            if campaign.get('version',0)>=50:required_outputs.add('lcs-wander-path-tests.log')
            if campaign.get('version',0)>=57:required_outputs.update(('lcs-ending-tune-transport-tests.log',
                'lcs-owned-audio-output-tests.log','lcs-ending-tune-source-tests.log','original-ending-tune-contracts.log'))
            if campaign.get('version',0)>=58:required_outputs.update((
                'original-remote-cancel-contracts.log','original-save-menu-contracts.log',
                'original-front-seat-contracts.log','original-pickup-weapon-contracts.log'))
            if campaign.get('version',0)>=59:required_outputs.add('lcs-resource-tests.log')
            if campaign.get('version',0)>=60:required_outputs.update(('lcs-script-coronas-tests.log',
                'lcs-script-corona-backend-tests.log','original-script-corona-contracts.log'))
            if campaign.get('version',0)>=61:required_outputs.update(('lcs-collectives-tests.log','original-collective-contracts.log'))
            if campaign.get('version',0)>=62:required_outputs.update(('lcs-hud-tests.log','lcs-hud-render-tests.log','original-hud-presentation-contracts.log'))
            if campaign.get('version',0)>=63:required_outputs.update(('lcs-script-markers-tests.log',
                'lcs-script-marker-pool-tests.log','lcs-script-marker-render-tests.log',
                'original-marker-contracts.log','validate-script-marker-resources.log'))
            if campaign.get('version',0)>=90:required_outputs.add('lcs-ps2-audio-source-tests.log')
            if campaign.get('version',0)>=92:required_outputs.add('lcs-radio-tests.log')
            if campaign.get('version',0)>=94:required_outputs.add('weapon-slot-contracts.log')
            if {row['output'] for row in proof['checks']}!=required_outputs:raise ValueError('Campaign regression proof outputs do not match required checks')
        for row in proof['inputs']:
            if digest(package/row['relative'])!=row['sha256']:raise ValueError('Campaign startup input changed: '+row['relative'])
        for row in proof['checks']:
            if not row['passed'] or digest(package/'metadata'/row['output'])!=row['outputSha256']:raise ValueError('Campaign startup result changed')
    base=pathlib.Path(campaign['basePackage'])
    original_profile=(base/'data/gta.dat').read_text()
    expected_profile=original_profile.replace('COLFILE 0 MODELS\\LCS\\WORLD.COL','IDE DATA\\LCS\\CAMPAIGN-ANCHORS.IDE\nCOLFILE 0 MODELS\\LCS\\WORLD.COL')+'IPL DATA\\LCS\\CAMPAIGN-ANCHORS.IPL\n'
    if campaign.get('version',0)>=82:
        # Converted zones replace the native SA INFO.ZON/MAP.ZON registrations
        # and must regenerate byte-identically from the user's GTA3.ZON.
        zones=campaign['zones82']
        spec=importlib.util.spec_from_file_location('convert_zones',pathlib.Path(__file__).with_name('convert-zones.py'))
        converter=importlib.util.module_from_spec(spec);spec.loader.exec_module(converter)
        source=package/zones['source']
        if digest(source)!=zones['sourceSha256'] or digest(package/'text/american.gxt')!=zones['gxtSha256']:
            raise ValueError('Zone conversion inputs changed')
        info,mapzones,_=converter.convert(source.read_bytes().decode('ascii'),package/'text/american.gxt')
        for name,text,key in (('info.zon',info,'infoSha256'),('map.zon',mapzones,'mapSha256')):
            mounted=package/'data/lcs'/name
            if mounted.read_bytes()!=text.encode('ascii') or digest(mounted)!=zones[key]:
                raise ValueError('Converted zone file does not match its source: '+name)
        for native,converted in (('IPL DATA\\MAP.ZON','IPL DATA\\LCS\\MAP.ZON'),('IPL DATA\\INFO.ZON','IPL DATA\\LCS\\INFO.ZON')):
            if expected_profile.count(native+'\n')!=1:raise ValueError('Base profile lacks native zone registration')
            expected_profile=expected_profile.replace(native+'\n',converted+'\n')
    if campaign.get('version',0)>=83:
        # Converted timecycle must regenerate byte-identically from GAME.DTZ and native SA data.
        cycle=campaign['timecycle83']
        spec=importlib.util.spec_from_file_location('convert_timecyc',pathlib.Path(__file__).with_name('convert-timecyc.py'))
        timecyc=importlib.util.module_from_spec(spec);spec.loader.exec_module(timecyc)
        text,regenerated=timecyc.convert_files(cycle['rom'],cycle['native'])
        if regenerated['sourceChunkSha256']!=cycle['sourceChunkSha256'] or regenerated['nativeSha256']!=cycle['nativeSha256']:
            raise ValueError('Timecycle conversion inputs changed')
        if (package/'data/timecyc.dat').read_bytes()!=text.encode('ascii') or digest(package/'data/timecyc.dat')!=cycle['outputSha256']:
            raise ValueError('Converted timecycle does not match its source')
    fleet_ids=set()
    fleet_handling=set()
    if campaign.get('vehicleFleet'):
        fleet=campaign['vehicleFleet']
        metadata=package/fleet['manifest']
        if digest(metadata)!=fleet['sha256']:raise ValueError('Vehicle fleet manifest changed')
        mounted=json.loads(metadata.read_text(encoding='utf-8'))
        source=pathlib.Path(mounted['sourceManifest'])
        if digest(source)!=mounted['sourceManifestSha256']:raise ValueError('Vehicle fleet provenance changed')
        original_fleet=json.loads(source.read_text(encoding='utf-8'))
        original_rows={row['LcsId']:row for row in original_fleet['Vehicles'] if row['Prepared']}
        expected_files={row['RelativePath'].replace('\\','/'):row['Sha256'].lower() for row in original_fleet['OutputFiles']}
        expected_ide={int(line.split(',')[0]):[v.strip() for v in line.split(',')]
            for line in (source.parent/'vehicles.ide').read_text().splitlines()
            if line.split(',')[0].strip().isdigit() and ',' in line}
        mounted_ide={int(line.split(',')[0]):[v.strip() for v in line.split(',')]
            for line in (package/'data/vehicles.ide').read_text().splitlines()
            if line.split(',')[0].strip().isdigit() and ',' in line}
        handling=lambda path:{line.split()[0]:line.split() for line in path.read_text().splitlines()
            if line.strip() and not line.lstrip().startswith((';','#','%','!','$'))}
        expected_handling=handling(source.parent/'handling.cfg')
        actual_handling=handling(package/'data/handling.cfg')
        native_bindings=(package/'data/lcs/native-equivalents.tsv').read_text().splitlines()
        if mounted['installed'] or mounted['gameExecuted'] or len(mounted['VehicleMappings'])!=fleet['count']:
            raise ValueError('Invalid fleet claims or inventory')
        wanted={}
        for row in mounted['VehicleMappings']:
            if original_rows.get(row['LcsId'])!=row or not row.get('EmbeddedCollision'):
                raise ValueError('Unverified fleet vehicle or missing embedded collision')
            model=row['NativeId'];name=row['NativeName'];group=row['NativeHandling']
            # A handling group may be shared only as SA itself does (pizzaboy/faggio
            # MOPED), declared by both rows (tools/prepare-campaign-vehicles.py).
            declared={(other['NativeId'],other['NativeName']) for other in row.get('SharedNativeHandlingModels') or []}
            sharers={(other['NativeId'],other['NativeName']) for other in mounted['VehicleMappings']
                     if other is not row and other['NativeHandling']==group}
            if model in fleet_ids or (group in fleet_handling and sharers!=declared):raise ValueError('Shared fleet model/handling slot')
            fleet_ids.add(model);fleet_handling.add(group)
            if mounted_ide.get(model)!=expected_ide[model] or actual_handling[group]!=expected_handling[group]:
                raise ValueError('Fleet configuration differs from converted original')
            if '\t'.join(map(str,(row['LcsId'],model,row['Name'],name,'vehicle'))) not in native_bindings:
                raise ValueError('Fleet script binding mismatch')
            for stem,ext in ((name,'.dff'),(expected_ide[model][2],'.txd')):
                relative='vehicles/'+stem+ext
                if relative in wanted:raise ValueError('Shared fleet texture/model override')
                wanted[relative]=expected_files['models/'+stem+ext]
        if original_fleet.get('SharedTextures'):
            shared=original_fleet['SharedTextures']
            if (shared['PreparedPath'],shared['TargetPath'])!=('models/vehicle.txd','models/generic/vehicle.txd'):
                raise ValueError('Invalid fleet shared dictionary location')
            wanted[shared['TargetPath']]=expected_files[shared['PreparedPath']]
        if original_fleet.get('VehicleColours'):
            colours=original_fleet['VehicleColours']
            if (colours['PreparedPath'],colours['TargetPath'])!=('vehicle-colours.dat','data/lcs/vehicle-colours.dat'):
                raise ValueError('Invalid vehicle colours consumer resource')
            wanted[colours['TargetPath']]=expected_files[colours['PreparedPath']]
        if original_fleet.get('NativeColourLoader'):
            loader=original_fleet['NativeColourLoader']
            if (loader['PreparedPath'],loader['TargetPath'])!=('carcols.dat','data/carcols.dat'):
                raise ValueError('Invalid native colour loader resource')
            wanted[loader['TargetPath']]=expected_files[loader['PreparedPath']]
        if len(mounted['Files'])!=len(wanted):raise ValueError('Incomplete fleet file inventory')
        for row in mounted['Files']:
            if wanted.get(row['relative'])!=row['sha256'].lower() or digest(package/row['relative'])!=row['sha256'].lower():
                raise ValueError('Mounted fleet geometry/textures differ')
        for row in mounted['Configs']:
            if row['relative']=='data/lcs/native-equivalents.tsv' and campaign.get('weaponEquivalents94') and                digest(package/row['relative'])!=row['afterSha256'].lower():
                # v94 re-targets only the weapon object rows (tools/weapon_slots.py);
                # a fleet mounted after that step pins the current file directly.
                weapons_step=campaign['weaponEquivalents94']
                entity_rows=[line for line in (package/row['relative']).read_text(encoding='ascii').splitlines() if not line.endswith('\tobject')]
                # The fleet can have been regenerated after the weapon step.
                # Its historical whole-file hash then need not equal the
                # earlier weapon input hash. Every vehicle row was checked
                # against the pinned source above; preserve the weapon step's
                # exact non-object rows and validate the object rows below.
                if digest(package/row['relative'])!=campaign['equivalentSha256'] or \
                   hashlib.sha256('\n'.join(entity_rows).encode('ascii')).hexdigest()!=weapons_step['entityRowsSha256']:
                    raise ValueError('Fleet bindings changed beyond the weapon slot rows')
                continue
            if digest(package/row['relative'])!=row['afterSha256'].lower():raise ValueError('Fleet composite configuration changed')
    if campaign.get('weaponVisuals'):
        visual=campaign['weaponVisuals']
        manifest_file=package/visual['manifest']
        if digest(manifest_file)!=visual['sha256']:
            raise ValueError('Weapon visual manifest changed')
        weapons=json.loads(manifest_file.read_text(encoding='utf-8'))
        slotted=campaign.get('version',0)>=94
        animated=campaign.get('version',0)>=95
        if weapons['gameExecuted'] or weapons['installed'] or weapons['behaviorChanged']!=slotted or weapons['animationsChanged']!=animated or any(weapons[key] for key in
            ('audioChanged','handAlignmentRuntimeVerified')):
            raise ValueError('Invalid weapon visual scope or runtime claims')
        source=pathlib.Path(weapons['sourceManifest'])
        if digest(source)!=weapons['sourceManifestSha256']:
            raise ValueError('Weapon conversion provenance changed')
        converted=json.loads(source.read_text(encoding='utf-8'))
        by_id={row['originalModelId']:row for row in converted['converted']}
        bindings=(package/'data/lcs/native-equivalents.tsv').read_text().splitlines()
        if not converted['complete'] or converted['failures'] or len(weapons['slots'])!=visual['slots']:
            raise ValueError('Incomplete weapon conversion')
        expected={}
        by_name={row['originalName'].lower():row for row in converted['converted']}
        rw=source.parent/'rw-assets'
        for slot in weapons['slots']:
            row=by_id[slot['originalModel']]
            binding='\t'.join(map(str,(slot['originalModel'],slot['nativeModel'],
                slot['selectedOriginal'].lower(),slot['nativeName'],'object')))
            if binding not in bindings or row['originalName']!=slot['selectedOriginal']:
                raise ValueError('Weapon visual does not match script model binding')
            if slotted and weapon_slots.WEAPON_TARGETS[slot['selectedOriginal'].lower()]!=slot['nativeName']:
                raise ValueError('Weapon slot differs from the packaged slot plan')
            visual_row=row
            if slot.get('visualOriginal'):
                if weapon_slots.VISUAL_SOURCE_OVERRIDES.get(slot['selectedOriginal'].lower())!=slot['visualOriginal']:
                    raise ValueError('Unsupported separate weapon visual source')
                visual_row=by_name[slot['visualOriginal']]
            sums={'.dff':visual_row['dffSHA256'],'.txd':visual_row['txdSHA256']}
            if slot.get('assembly'):
                # Rebuild the minigun body+barrel clump from the converted sources.
                barrel=by_name[slot['assembly']['barrel']]
                sums={'.dff':hashlib.sha256(weapon_slots.assemble_minigun((rw/(row['asset']+'.dff')).read_bytes(),
                          (rw/(barrel['asset']+'.dff')).read_bytes())).hexdigest(),
                      '.txd':hashlib.sha256(weapon_slots.merge_textures((rw/(row['asset']+'.txd')).read_bytes(),
                          (rw/(barrel['asset']+'.txd')).read_bytes())).hexdigest()}
            for extension in ('.dff','.txd'):
                relative='weapons/'+slot['nativeName']+extension
                if relative in expected:raise ValueError('Duplicate weapon target')
                expected[relative]=sums[extension]
        if len(weapons['files'])!=len(expected):raise ValueError('Weapon file inventory mismatch')
        if slotted:
            game=pathlib.Path(map_report['gameDirectory'])
            sa_models={};section=''
            for line in (game/'data/default.ide').read_text(encoding='latin1').splitlines():
                line=line.split('#',1)[0].strip()
                if not line:continue
                if ',' not in line:section='' if line.lower()=='end' else line.lower()
                elif section=='weap':
                    fields=[value.strip() for value in line.split(',')];sa_models[int(fields[0])]=fields[1].lower()
            regenerated=weapon_slots.convert_weapon_dat((game/'data/weapon.dat').read_text(encoding='latin1'),
                (package/'metadata/lcs-source/DATA/WEAPON.DAT.bin').read_text(encoding='latin1'),
                {row['originalModelId']:row['originalName'].lower() for row in converted['converted']},sa_models,animations=animated).encode('latin1')
            if (package/'data/weapon.dat').read_bytes()!=regenerated or weapons['weaponDat']['sha256']!=hashlib.sha256(regenerated).hexdigest():
                raise ValueError('Converted weapon.dat differs from its sources')
        if animated:
            # Rebuild each SA gun block from SA's anim.img and the original blocks.
            step=campaign['weaponAnimations95']
            entries=weapon_animations.img_entries(pathlib.Path(map_report['gameDirectory'])/'anim/anim.img')
            root=pathlib.Path(__file__).resolve().parents[1]
            originals={}
            for name,row in step['originalSources'].items():
                data=(root/row['relative']).read_bytes()
                if hashlib.sha256(data).hexdigest()!=row['sha256']:raise ValueError('Original weapon animation source changed: '+name)
                originals[name]=data
            expected_blocks={}
            for block,mapping in weapon_animations.BLOCKS.items():
                data,_=weapon_animations.rebuild_block(entries[block+'.ifp'],{name:originals[name] for name,_ in mapping.values()})
                expected_blocks['anim/weapons/'+block+'.ifp']=hashlib.sha256(data).hexdigest()
            if {row['relative']:row['sha256'] for row in step['blocks']}!=expected_blocks or \
               any(digest(package/relative)!=sha for relative,sha in expected_blocks.items()) or \
               {path.relative_to(package).as_posix() for path in (package/'anim').rglob('*.ifp')}!=set(expected_blocks):
                raise ValueError('Converted weapon animation blocks differ from their sources')
        for row in weapons['files']:
            if expected.get(row['relative'])!=row['sha256'] or digest(package/row['relative'])!=row['sha256']:
                raise ValueError('Weapon visual differs from converted source')
    if campaign.get('ferryBoat'):
        ferry=campaign['ferryBoat']
        prepared=pathlib.Path(ferry['source'])
        manifest_file=package/'metadata/ferry-boat.json'
        if digest(manifest_file)!=ferry['sha256']:
            raise ValueError('Ferry model provenance changed')
        manifest=json.loads(manifest_file.read_text())
        prepared_manifest=json.loads((prepared/'manifest.json').read_text())
        # The ferry may link the complete fleet record it was mounted with.
        links={'completeFleetManifest','fleetVehicleCount'}
        if any(key in manifest for key in links) and (manifest.get('completeFleetManifest')!=campaign.get('vehicleFleet',{}).get('manifest') or
                manifest.get('fleetVehicleCount')!=campaign.get('vehicleFleet',{}).get('count')):
            raise ValueError('Ferry fleet link differs from the mounted fleet')
        if {k:v for k,v in manifest.items() if k!='files' and k not in links}!={k:v for k,v in prepared_manifest.items() if k!='files'}:
            raise ValueError('Ferry preparation metadata changed')
        prepared_files={row['relative']:row for row in prepared_manifest['files']}
        if len(prepared_files)!=len(prepared_manifest['files']):
            raise ValueError('Duplicate prepared ferry resource')
        if not manifest['prepared'] or manifest['installed'] or manifest['gameExecuted'] or \
           manifest['sourceModel']!=192 or manifest['nativeModel']!=454 or manifest['nativeClass']!='boat' or \
           manifest['nativeName']!='tropic' or manifest['ferryCommandsCompleted']:
            raise ValueError('Invalid ferry boat registration/claims')
        if '192\t454\tferry\ttropic\tvehicle' not in (package/'data/lcs/native-equivalents.tsv').read_text().splitlines():
            raise ValueError('Ferry counterpart is not the mounted boat slot')
        required={'vehicles/tropic.dff','vehicles/tropic.txd','models/lcs/ferry.col','data/vehicles.ide','data/handling.cfg'}
        if {row['relative']for row in manifest['files']}!=required:
            raise ValueError('Ferry file inventory changed')
        # A fleet that converts the ferry itself (LCS 192 -> tropic) owns the
        # tropic model files; the ferry record then pins the fleet's copies.
        fleet_owned={}
        if campaign.get('vehicleFleet'):
            fleet_record=json.loads((package/campaign['vehicleFleet']['manifest']).read_text(encoding='utf-8'))
            if any(row['LcsId']==192 and row['NativeName']=='tropic' for row in fleet_record['VehicleMappings']):
                fleet_owned={row['relative']:row['sha256'].lower() for row in fleet_record['Files'] if row['relative'].startswith('vehicles/tropic.')}
        for row in manifest['files']:
            if row['relative'] in fleet_owned:
                if row['sha256'].lower()!=fleet_owned[row['relative']] or digest(package/row['relative'])!=fleet_owned[row['relative']]:
                    raise ValueError('Fleet-owned ferry model differs from the fleet record')
                continue
            composite=bool(fleet_ids) and row['relative'] in ('data/vehicles.ide','data/handling.cfg')
            original=prepared_files.get(row['relative'])
            if not original:
                raise ValueError('Mounted ferry resource has no prepared origin')
            prepared_sha=row.get('preparedSha256',row['sha256'])
            # New mounts pin the merged file separately from the original
            # ferry-only overlay. Legacy mounts retain their original pins;
            # fleet/ferry row checks below still verify those composites.
            if prepared_sha!=original['sha256'] or digest(prepared/'overlay'/row['relative'])!=prepared_sha:
                raise ValueError('Prepared ferry resource differs from its provenance')
            if 'preparedSha256' in row:
                if digest(package/row['relative'])!=row['sha256'] or (package/row['relative']).stat().st_size!=row['bytes']:
                    raise ValueError('Mounted merged ferry resource changed')
                restored={k:v for k,v in row.items() if k!='preparedSha256'}
                restored.update(sha256=prepared_sha,bytes=original['bytes'])
                if restored!=original or (not composite and row['sha256']!=prepared_sha):
                    raise ValueError('Only fleet-shared ferry config files may be merged')
            elif row!=original or (not composite and digest(package/row['relative'])!=row['sha256']):
                raise ValueError('Mounted ferry resource differs from validated overlay')
        for row in manifest['nativeInputs']:
            if digest(pathlib.Path(row['path']))!=row['sha256']:
                raise ValueError('Native vehicle/handling reference changed')
        # Independent full-file checks: every other native definition is preserved.
        native_ide=(pathlib.Path(manifest['nativeInputs'][0]['path'])).read_text()
        mounted_ide=(package/'data/vehicles.ide').read_text()
        import re
        pattern=r'^454\s*,[^\r\n]+'
        rows=re.findall(pattern,mounted_ide,re.M)
        ide_mask=r'^(?:'+ '|'.join(map(str,sorted(fleet_ids|{454})))+r')\s*,[^\r\n]+'
        if len(rows)!=1 or re.sub(ide_mask,'<vehicle>',native_ide,flags=re.M)!=re.sub(ide_mask,'<vehicle>',mounted_ide,flags=re.M):
            raise ValueError('Unrelated native vehicle definitions changed')
        # A fleet converting the ferry owns its definition (its source vehicles.ide).
        approved_ide=(pathlib.Path(fleet_record['sourceManifest']).parent/'vehicles.ide') if fleet_owned else prepared/'overlay/data/vehicles.ide'
        if rows!=re.findall(pattern,approved_ide.read_text(),re.M):
            raise ValueError('Fleet overwrote the approved ferry definition')
        fields=[v.strip()for v in rows[0].split(',')]
        if fields[:5]!=['454','tropic','tropic','boat','TROPIC']:
            raise ValueError('Mounted ferry does not use CBoat/TROPIC')
        native_handling=pathlib.Path(manifest['nativeInputs'][1]['path']).read_text()
        mounted_handling=(package/'data/handling.cfg').read_text()
        pattern=r'^(?:TROPIC|%[ \t]+TROPIC)[ \t]+[^\r\n]+'
        # Fleet boats/bikes/aircraft also own their %/!/$ class lines.
        handling_mask=r'^(?:[%!$][ \t]+)?(?:'+ '|'.join(re.escape(name) for name in sorted(fleet_handling|{'TROPIC'}))+r')[ \t]+[^\r\n]+'
        if len(re.findall(pattern,mounted_handling,re.M))!=2 or \
           re.sub(handling_mask,'<vehicle>',native_handling,flags=re.M)!=re.sub(handling_mask,'<vehicle>',mounted_handling,flags=re.M):
            raise ValueError('Unrelated native handling changed')
        approved_handling=(pathlib.Path(fleet_record['sourceManifest']).parent/'handling.cfg') if fleet_owned else prepared/'overlay/data/handling.cfg'
        if [line.split() for line in re.findall(pattern,mounted_handling,re.M)]!=[line.split() for line in re.findall(pattern,approved_handling.read_text(),re.M)]:
            raise ValueError('Fleet overwrote the approved ferry handling')
        if 'sa_boat_hierarchy_validated=true' not in (prepared/'logs/boat-validation.log').read_text():
            raise ValueError('Native boat hierarchy validation missing')
        if campaign.get('version',0)>=66:
            if manifest.get('embeddedCollision') is not True or 'embedded_col_plugin=true' not in (prepared/'logs/boat-conversion.log').read_text(encoding='utf-8'):
                raise ValueError('Self-contained native boat collision validation missing')
        # Vehicle pipeline retains supported legacy COLL; map collision_records is deliberately COL2-only.
        col=(package/'models/lcs/ferry.col').read_bytes()
        if len(col)<92 or col[:4]!=b'COLL' or struct.unpack_from('<I',col,4)[0]+8!=len(col) or col[8:30].split(b'\0')[0]!=b'tropic':
            raise ValueError('Ferry collision is not bound to the intended native boat')
        if 'collision_validation_passed=true' not in (prepared/'logs/collision-validation.log').read_text():
            raise ValueError('Native vehicle collision validation missing')
        expected_profile=expected_profile.replace('COLFILE 0 MODELS\\LCS\\WORLD.COL',
            'COLFILE 0 MODELS\\LCS\\WORLD.COL\nCOLFILE 0 MODELS\\LCS\\FERRY.COL',1)
    if (package/'data/gta.dat').read_text()!=expected_profile:raise ValueError('Approved level profile changed beyond source carrier registration')
    return dict(enabled=True,valid=True,originalScriptUnmodified=True,originalScriptSha256=campaign['originalScriptSha256'],
                canonicalGameplayCharacters=len(named),preparedCutscenes=len(names),
                approvedResourcesPreserved=len(campaign['preservedApprovedResources']),
                originalResourcesVerified=len(campaign['originalResources']),nativeStartupValidated=campaign.get('nativeStartupValidated',False),
                gameExecuted=False,completeCampaignVerified=False)

def validate(package, source):
    report = json.loads((package / 'metadata/map-preview.json').read_text())
    effects = expected_effects(package)
    game = pathlib.Path(report['gameDirectory'])
    native_objects = validate_native_collisions(package, game, retained_definitions(game))
    models = report['modelAliases']
    dictionaries = report['textureDictionaryAliases']
    expected = {}
    for file in (source / 'rw-assets').iterdir():
        mapping = models if file.suffix.lower() == '.dff' else dictionaries
        name = mapping.get(file.stem.lower(), file.stem) + file.suffix.lower()
        if name.lower() in expected:
            raise ValueError('Duplicate runtime filename')
        expected[name.lower()] = file
    if report.get('playerSkins'):
        player = pathlib.Path(report['playerSkins']['source'])
        skins = json.loads((player / 'manifest.json').read_text())
        if len(skins['outfits']) != 16 or skins['playerImgModified']:
            raise ValueError('Invalid Toni skin inventory')
        if (package / 'data/lcs/player/index.tsv').read_bytes() != (player / 'index.tsv').read_bytes():
            raise ValueError('Original outfit labels changed during packaging')
        audit = json.loads((player / 'player-validation.json').read_text())
        if not audit['valid'] or len(audit['outfits']) != 16 or not audit['nativeSkeletonBindMatricesVerified']:
            raise ValueError('Independent Toni asset audit missing/invalid')
        if (package / 'metadata/player-validation.json').read_bytes() != (player / 'player-validation.json').read_bytes():
            raise ValueError('Toni validation report changed during packaging')
        for row in skins['outfits']:
            for extension, hash_name in (('.dff', 'dffSHA256'), ('.txd', 'txdSHA256')):
                name = row['asset'] + extension
                file = player / 'rw-assets' / name
                if name.lower() in expected or hashlib.sha256(file.read_bytes()).hexdigest() != row[hash_name]:
                    raise ValueError('Toni media changed/conflicting runtime name')
                expected[name.lower()] = file
    archive = package / 'models/lcs/world.img'
    checked = set()
    with archive.open('rb') as stream:
        signature, count = struct.unpack('<4sI', stream.read(8))
        if signature != b'VER2' or count != len(expected):
            raise ValueError('IMG directory count/signature mismatch')
        entries = [struct.unpack('<IHH24s', stream.read(32)) for _ in range(count)]
        intervals = []
        minimum = ((8 + count * 32 + 2047) // 2048) * 2048
        for sector, size, compressed_size, rawname in entries:
            name = rawname.split(b'\0')[0].decode('ascii').lower()
            start, length = sector * 2048, (compressed_size or size) * 2048
            if name not in expected or name in checked or start < minimum or start + length > archive.stat().st_size:
                raise ValueError('Invalid IMG entry: ' + name)
            checked.add(name)
            original = expected[name].read_bytes()
            if name in effects:
                original = inject_dff(original, effects[name])
            stream.seek(start)
            converted = stream.read(length)
            if converted[:len(original)] != original or any(converted[len(original):]):
                raise ValueError('Native DFF/TXD bytes changed during packaging: ' + name)
            intervals.append((start, start + length))
        ordered = sorted(intervals)
        if any(a[1] > b[0] for a, b in zip(ordered, ordered[1:])):
            raise ValueError('IMG entry regions overlap')
    before = list(collision_records(source / 'world.col'))
    after = list(collision_records(package / 'models/lcs/world.col'))
    if len(before) != len(after) or len(after) != report['collisions']:
        raise ValueError('Collision count changed')
    collisions = set()
    for original, installed in zip(before, after):
        name = original[8:30].split(b'\0')[0].decode('ascii')
        expected_name = models.get(name.lower(), report['unregisteredCollisionAliases'].get(name, name))
        runtime_name = installed[8:30].split(b'\0')[0].decode('ascii')
        if runtime_name != expected_name or original[:8] != installed[:8] or original[30:] != installed[30:]:
            raise ValueError('Collision geometry/surfaces changed or incorrect alias')
        if runtime_name.lower() in collisions:
            raise ValueError('Duplicate collision model name')
        collisions.add(runtime_name.lower())
    native_names = {row['name'] for row in json.loads((package / 'metadata/native-collisions.json').read_text())['records']}
    if native_names & collisions:
        raise ValueError('Native object collision would overwrite an LCS collision alias')
    definitions = {}
    section = ''
    for line in (package / 'data/lcs/world.ide').read_text().splitlines():
        if line in ('objs', 'tobj', 'txdp', 'end'):
            section = '' if line == 'end' else line
        elif section in ('objs', 'tobj'):
            fields = [field.strip() for field in line.split(',')]
            ident = int(fields[0])
            if ident in definitions or fields[1].lower() not in collisions:
                raise ValueError('Invalid native model/collision association')
            if fields[1].lower() + '.dff' not in checked or fields[2].lower() + '.txd' not in checked:
                raise ValueError('IDE references missing native media')
            definitions[ident] = fields[1]
    bindings = (package / 'data/lcs/model-bindings.tsv').read_text().splitlines()
    if len(bindings) != len(definitions):
        raise ValueError('Incomplete model bindings')
    for line in bindings:
        source_id, native_id, original, runtime = line.split('\t')
        if definitions[int(native_id)] != runtime or models.get(original.lower(), original) != runtime:
            raise ValueError('Runtime binding identity differs')
    placements = 0
    for file in (source / 'layout-review').glob('world*.ipl'):
        original_rows = file.read_text().splitlines()
        installed_rows = (package / 'data/lcs' / file.name).read_text().splitlines()
        if len(original_rows) != len(installed_rows):
            raise ValueError('Placement count changed')
        for original, installed in zip(original_rows, installed_rows):
            if ',' not in original:
                if installed != original:
                    raise ValueError('IPL sections changed')
                continue
            a = [v.strip() for v in original.split(',')]
            b = [v.strip() for v in installed.split(',')]
            a[1] = models.get(a[1].lower(), a[1])
            if a != b or definitions[int(b[0])] != b[1]:
                raise ValueError('Placement transform/LOD changed during packaging')
            placements += 1
    radar = list((package / 'models/radar').glob('*.txd'))
    if len(radar) != 144:
        raise ValueError('Missing radar tiles')
    for file in radar:
        if file.read_bytes() != (source / 'radar' / file.name).read_bytes():
            raise ValueError('Radar pixels changed')
    if (package / 'data/water.dat').read_bytes() != (source / 'water.dat').read_bytes():
        raise ValueError('Water changed during packaging')
    for name in ('render-ranges.tsv','island-zones.tsv'):
        if (package / 'data/lcs' / name).read_bytes() != (source / 'layout-review' / name).read_bytes():
            raise ValueError('Original render policy changed during packaging')
    rules=[line.split('\t') for line in (package/'data/lcs/render-ranges.tsv').read_text().splitlines()]
    if {int(r[0]) for r in rules} != set(definitions) or len(rules)!=len(definitions):
        raise ValueError('Map render policy has incomplete/duplicate native IDs')
    layout=json.loads((source/'layout-validation.json').read_text())
    if len(checked) != len(expected) or placements != layout['instances'] or len(definitions) != layout['modelDefinitions']:
        raise ValueError('Unexpected preview inventory')
    if any(len(r)!=6 or int(r[5]) not in (0,1) or int(r[1]) and int(r[5]) for r in rules):
        raise ValueError('Invalid physical collision/render policy')
    navigation = path_audit.validate(package / 'models/lcs/paths.img')
    if report.get('navigation'):
        if navigation['PathsImgSha256'] != report['navigation']['graph']['PathsImgSha256'] or not navigation['VehicleNodes'] or not navigation['PedNodes']:
            raise ValueError('Native population graph absent or changed during packaging')
        code = (package / 'data/script/main.scm').read_bytes()
        for start, opcode in ((651, 0x01eb), (658, 0x03de)):
            if code[start:start+3] != struct.pack('<HB', opcode, 6) or struct.unpack_from('<f', code, start+3)[0] != 1.0:
                raise ValueError('Native population density is not enabled in bootstrap')
    result = dict(valid=True, nativeAssetBytesPreserved=len(checked), collisionGeometryAndSurfaceBytesPreserved=len(after),
                  modelBindings=len(bindings), placementTransformsAndLodLinksPreserved=placements,
                  radarTilesPreserved=len(radar), waterPreserved=True, originalRenderRulesPreserved=True, gameExecuted=False,
                  completeGame=False, nativeNavigation=navigation, nativeObjectCollisions=native_objects)
    result['gameplayPeds'] = validate_gameplay_peds(package, game)
    result['campaign'] = validate_campaign(package, report)
    (package / 'metadata/package-validation.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    files = []
    for file in sorted(package.rglob('*')):
        if file.is_file() and file.name != 'package-files.json':
            with file.open('rb') as stream:
                digest = hashlib.file_digest(stream, 'sha256').hexdigest()
            files.append(dict(relative=str(file.relative_to(package)).replace('\\', '/'), bytes=file.stat().st_size, sha256=digest))
    (package / 'metadata/package-files.json').write_text(json.dumps(files, indent=2), encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=pathlib.Path, required=True)
    parser.add_argument('--source', type=pathlib.Path, required=True)
    args = parser.parse_args()
    print(json.dumps(validate(args.package, args.source), indent=2))
