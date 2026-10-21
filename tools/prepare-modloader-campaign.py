"""Package user-converted campaign resources over an approved Modloader map/skin package.

This tool ships as source without Rockstar media. Original source files use a
.bin suffix so Modloader cannot confuse the LCS SCM/GXT with SA's native files.
"""
import argparse, hashlib, json, pathlib, shutil
import importlib.util
from campaign_animation_storage import prepare as prepare_animation_storage
from bootstrap_player_exports import export_player_globals, report as bootstrap_report

ROOT = pathlib.Path(__file__).resolve().parents[1]
LANGUAGES = ('american', 'french', 'german', 'italian', 'spanish')
MAIN_SHA256 = '2ab65c950b86f02078b56fbac10724c0bf90eb2329bddc7a480c98cc8a7a65d4'

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def prepare(args):
    base, output = args.base.resolve(), args.output.resolve()
    if output.exists() or base == output or base in output.parents:
        raise ValueError('Use a new staging directory outside the input package')
    manifest = json.loads((base/'metadata/package-validation.json').read_text())
    if not manifest['valid'] or not manifest['gameplayPeds']['valid']:
        raise ValueError('Approved map/skin package must pass independent validation')
    if digest(args.data/'DATA/MAIN.SCM') != MAIN_SHA256:
        raise ValueError('Original PS2 mission script changed')
    shutil.copytree(base, output)
    files = []
    def copy(source, relative):
        target = output/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if digest(source) != digest(target):
            raise ValueError('Copy bytes differ: '+relative)
        files.append(dict(relative=relative, source=str(source.resolve()), sha256=digest(source)))
    original = 'metadata/lcs-source/'
    for relative in ('DATA/MAIN.SCM','DATA/GTA3.ZON','DATA/WEAPON.DAT','DATA/PATHS/FERRY1.DAT','audio/sfx.RAW','audio/sfx.sdt'):
        copy(args.data/relative, original+relative+'.bin')
    for language in LANGUAGES:
        relative='TEXT/'+language+'.gxt'
        copy(args.data/relative, original+relative+'.bin')
        copy(args.text/'text'/f'{language}.gxt', f'text/{language}.gxt')
    for index in range(14):
        relative=f'audio/news/NEWS_{chr(65+index)}.wav'
        copy(args.data/relative, original+relative+'.bin')
    animation_directory=output/'data/lcs/animations'
    animation_storage=prepare_animation_storage(args.gestures,args.named,animation_directory)
    for path in sorted(animation_directory.rglob('*')):
        if path.is_file():
            files.append(dict(relative=path.relative_to(output).as_posix(),source=str(path.resolve()),sha256=digest(path)))
    cut_manifest=json.loads((args.cuts/'manifest.json').read_text())
    for row in cut_manifest['Files']:
        relative=row['Relative'].replace('\\','/')
        source=args.cuts/relative
        if digest(source).upper()!=row['Sha256']:
            raise ValueError('Changed prepared scene archive/audio')
        copy(source,relative)
    # Native population definitions stay byte-identical; only script identities
    # choose the corresponding canonical converted skin.
    canonical=[line.split('\t') for line in (base/'data/lcs/peds/canonical-model-bindings.tsv').read_text().splitlines()]
    identities={int(row[0]):row for row in canonical if row[0]!='None'}
    equivalents=[]
    for line in args.equivalents.read_text().splitlines():
        row=line.split('\t'); source_id=int(row[0])
        if source_id in identities:
            skin=identities[source_id]
            if row[4]!='ped' or row[2].lower()!=skin[2].lower():
                raise ValueError('Canonical original ped identity differs')
            row[1],row[3]=skin[1],skin[3]
        equivalents.append(row)
    equivalent_path=output/'data/lcs/native-equivalents.tsv'
    equivalent_path.write_text(''.join('\t'.join(row)+'\n' for row in equivalents))
    source_map=json.loads((args.map_source/'manifest.json').read_text())
    primary={row['name'].lower():int(row.get('sourceModel',key)) for key,row in source_map['models'].items()}
    world_rows=[line.split('\t') for line in (base/'data/lcs/model-bindings.tsv').read_text().splitlines()]
    mission_world=[row for row in world_rows if row[2].lower() in primary and int(row[0])==primary[row[2].lower()]]
    if len({row[0] for row in mission_world})!=len(mission_world):raise ValueError('Ambiguous original primary world models')
    (output/'data/lcs/campaign-model-bindings.tsv').write_text(''.join('\t'.join(row)+'\n' for row in mission_world))
    spec=importlib.util.spec_from_file_location('campaign_anchors',ROOT/'tools/prepare-campaign-world-anchors.py')
    anchors=importlib.util.module_from_spec(spec);spec.loader.exec_module(anchors)
    anchor_rows=anchors.prepare(args.rom,output,args.map_source)
    with (output/'data/lcs/campaign-model-bindings.tsv').open('a') as stream:stream.write('\n'.join(anchor_rows)+'\n')
    spec=importlib.util.spec_from_file_location('campaign_interiors',ROOT/'tools/prepare-campaign-interiors.py')
    interiors=importlib.util.module_from_spec(spec);spec.loader.exec_module(interiors)
    interiors.prepare(args.rom,output,args.map_source)
    live=args.game.resolve()/'modloader/stories_bridge'
    ini='[LCS]\nMode=Campaign\nOriginalMapRenderRules=1\nPlayerSkins=1\nPlayerOutfit=plr3\nGameplaySkins=1\nEquivalentEntities=1\nSourceFileSuffix=.bin\n'
    for key,relative in (('WorldDirectory','data/lcs'),('DataDirectory','metadata/lcs-source'),('ResourceDirectory',''),('CutsceneDirectory','data/lcs/cutscenes')):
        ini+=key+'='+str(live/relative)+'\n'
    (output/'LcsSaBridge.ini').write_text(ini,encoding='utf-16')
    copy(ROOT/'build/Release/LcsSaBridge.asi','LcsSaBridge.asi')
    metadata=json.loads((output/'metadata/map-preview.json').read_text())
    metadata.update(version='48',mode='Campaign',campaignEnabled=True,gameExecuted=False)
    (output/'metadata/map-preview.json').write_text(json.dumps(metadata,indent=2))
    bootstrap=output/'data/script/main.scm'
    bootstrap_before=bootstrap.read_bytes();bootstrap_after=export_player_globals(bootstrap_before)
    bootstrap.write_bytes(bootstrap_after)
    preserved=[]
    for row in json.loads((base/'metadata/package-files.json').read_text()):
        relative=row['relative']
        if relative.startswith(('models/','data/')) and relative!='data/gta.dat':
            expected=digest(output/relative) if relative=='data/script/main.scm' else row['sha256'].lower()
            if digest(output/relative)!=expected:
                raise ValueError('Approved native map/skin/bootstrap changed: '+relative)
            preserved.append(dict(relative=relative,sha256=expected))
    campaign=dict(version=48,mode='Campaign',gameExecuted=False,completeCampaignVerified=False,
                  nativeBootstrapPlayerExports=bootstrap_report(bootstrap_before,bootstrap_after),
                  nativeStartupValidated=False,
                  originalScriptSha256=MAIN_SHA256,originalResources=files,
                  equivalentSha256=digest(equivalent_path),canonicalBindings=138,
                  namedSpecialCharacterSlots=21,preparedCutscenes=len(cut_manifest['Scenes']),
                  primaryWorldBindings=len(mission_world)+len(anchor_rows),primaryWorldBindingSha256=digest(output/'data/lcs/campaign-model-bindings.tsv'),
                  preservedApprovedResources=preserved,basePackage=str(base),
                  runtimeSafeguards=dict(nativeTrafficIdentity='verified-vehicle-class-disjoint-model-id',
                                         scriptFailurePolicy='quarantine-every-thread-preserve-dependencies',
                                         scriptNameAllowlist=False,retainBlockedInstruction=True,
                                         failedScriptCodeLease=True,automaticFailedCommandReplay=False,
                                         frameUpdateFaultScopes=True,frameUpdateRetryMilliseconds=250,
                                         frameDiagnosticRepeatMilliseconds=5000,dependencyRecoveryGate=True,
                                         sameFrameFaultRetirement=True,
                                         engineAdapterOwnership=True,consumedFailureFallback=False,
                                         spatialAudioPerVoiceHandles=True,originalNativeThreatScanFlags=True,
                                         originalWeaponInventorySlots=True,typedDeadCharacterVisibility=True,nativeThreatRadiusPolicy=True,
                                         nativePacketEntryReceipts=True,nativeEnteredFailureRequiresRepair=True,sourcePlayerAreaShootingPredicate=True,
                                         nativeWeaponExpenditure=True,paidShopRegression=True,insufficientShopFundsRegression=True,
                                         nativeAnalogueAxes=True,nativeDebugCameraSource=True,nextVehicleComponentSelectors=True,liveTargettableObjectGuard=True,
                                         nativeModPlayerGlobals=True,livePlayerHandleRefreshBeforeCleo=True,
                                         sourceGangWeaponDamageMask=True,sourceDirectedWanderTask=True,sourceWaitInCarTask=True,
                                         sourceEntityFamilyPackets=True,sourcePhysicsUnits=True,
                                         boundedGenerationalEntityHandles=True,sourceOrientationAxisOrder=True,
                                         nativeSignedServiceStatistics=True,sourcePlayerRewardHudFlash=True,entityBackendFaultInjectionRegression=True,
                                         boundedGenerationalRadarHandles=True,sourceVehicleClassPredicates=True,
                                         sourceScriptFireParameterExpansion=True,sourceUnsignedModelKillCounters=True,infrastructureFamilyPreflightRegression=True,
                                         boundedGenerationalPickupHandles=True,sourcePickupIntegerRevenue=True,
                                         sourceBoatWaterState=True,verifiedHelicopterRotorField=True,
                                         typedOriginalRampages=True,nativeRampageHeadshotAbi=True,
                                         nativeRampageConflictGuard=True,interruptedRampageWeaponIdentity=True,rampageEnteredFailureRequiresRepair=True,
                                         ownedOriginalWeatherClockSnapshots=True,sourceWeatherRestoreFallthrough=True,
                                         sourceClockPreservesNativeCalendar=True,sourceClockPreservesNativeBackup=True,sourceVehicleStatusTranslation=True,
                                         typedOriginalTrafficServices=True,nativeCleanupCollisionRelease=True,sourceBootStateTransition=True,sourceTankDetonationFlag=True,boundedDrunkSteeringBuffer=True,trafficServicePreflightRegression=True,
                                         sourceFirstPersonWeaponModes=True,boundedNativeDrowningQuery=True,focusedPlayerClipStorage=True,guardedBootVisualRemoval=True,
                                         consumedOriginalPhotographLatch=True,observedNativeCreditsState=True,isolatedCameraControlRelease=True,
                                         ownedOriginalActivityRecords=True,retailPS2RecordInitializers=True,boundedOriginalRecordArrays=True,retailCostumeUnlockMask=True,
                                         retailLocomotionNameTranslation=True,boundedNativeAnimGroupLookup=True,isolatedPedLocomotionWrite=True,nativePlayerHavocCounter=True,
                                         retailOriginalFloatMaxima=True,hostFpuIndependentActivityRecords=True,retailSignedScoreConversion=True,
                                         originalVehicleAttachmentContracts=True,ownedAttachmentReferences=True,sourceHelicopterSpeedByte=True,
                                         liveVehicleRamController=True,guardedCharacterObjectAttachment=True,
                                         guardedCollisionConsumption=True,originalNonexplosiveCarWreck=True,
                                         fixedOriginalWreckDamageStates=True,vehicleDamageEnteredFailureRequiresRepair=True,
                                         typedThreeNumberBigMessages=True,narrowedOriginalBigMessageStyle=True,nativeBigMessageEnteredFailureRequiresRepair=True,
                                         originalDeliveryTouristFloatCounters=True,eeSingleGuardFloatAddition=True,hostFpuIndependentCounterSums=True,
                                         typedOriginalGarageServices=True,namedGaragePredicateBinding=True,
                                         destructiveResprayPredicate=True,sourceBombTypeAndInstaller=True,garageServicePreflightRegression=True,
                                         companionActualTaskTree=True,companionRetryMilliseconds=2000,
                                         companionStatusQuery=False,companionHelperFailure='defer-actor-preserve-original-vm',
                                         companionQueryRetryMilliseconds=250,companionDiagnosticRepeatMilliseconds=5000,
                                         singlePassengerRearSeat=0,
                                         noSyntheticMissionSuccess=True),
                  animationStorage=dict(nativeBlocks=animation_storage['nativeBlocks'],automaticStreamingBlocks=0,
                                        sha256=digest(animation_directory/'storage.json')))
    (output/'metadata/campaign.json').write_text(json.dumps(campaign,indent=2))
    (output/'LEIA.txt').write_text('Campanha experimental LCS sobre mapa/personagens aprovados. Nova partida.\n'
        'A intro e os objetivos usam o MAIN.SCM original. O jogo inteiro ainda requer implementacao e testes.\n'
        'Diagnosticos: LcsSaBridge.log nesta pasta. Nao distribuir este pacote de assets; distribuir somente o conversor.\n')
    return dict(prepared=True,package=str(output),originalResources=len(files),preparedCutscenes=len(cut_manifest['Scenes']),
                approvedResourcesPreserved=len(preserved),gameExecuted=False)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base',type=pathlib.Path,required=True)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--game',type=pathlib.Path,required=True)
    for key,default in (('data','lcs-converted'),('text','mission-resources'),('gestures','gameplay-animations-v3'),
                        ('named','named-animations'),('cuts','campaign-cutscenes-arrival')):
        parser.add_argument('--'+key,type=pathlib.Path,default=ROOT/'out'/default)
    parser.add_argument('--equivalents',type=pathlib.Path,default=ROOT/'out/native-equivalents.tsv')
    parser.add_argument('--map-source',type=pathlib.Path,default=ROOT/'out/ps2-map-reconverted-v13')
    parser.add_argument('--rom',type=pathlib.Path,default=ROOT / 'local/originals/lcs-ps2')
    print(json.dumps(prepare(parser.parse_args()),indent=2))
