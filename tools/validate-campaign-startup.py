"""Record bounded offline campaign checks; never execute GTA or claim playability."""
import argparse,hashlib,json,pathlib,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
# v90+: audio is read at runtime from the user's own PS2 AUDIO folder.
ORIGINAL_AUDIO=ROOT/'local/originals/lcs-ps2/AUDIO'
def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def validate(package,source,validation_build=None):
    validation_build=pathlib.Path(validation_build) if validation_build else ROOT/"build"
    metadata=package/'metadata/campaign.json';campaign=json.loads(metadata.read_text())
    campaign['nativeStartupValidated']=False;metadata.write_text(json.dumps(campaign,indent=2))
    commands=[(validation_build/'Release/lcs-one-off-sound-tests.exe',[source/'DATA/MAIN.SCM',source/'audio']),
              (validation_build/'Release/lcs-campaign-package-tests.exe',[package,source]),
              (validation_build/'Release/lcs-campaign-equivalents-tests.exe',[package/'metadata/lcs-source/DATA/MAIN.SCM.bin',source,
               package/'data/lcs/campaign-model-bindings.tsv',package/'data/lcs/native-equivalents.tsv',
               package/'data/lcs/cutscenes/index.tsv',package/'data/lcs/peds/canonical-model-bindings.tsv'])]
    if campaign.get('version',0)>=28:commands[2][1].append(4)
    if campaign.get('version',0)>=30:
        commands.extend([(commands[2][0],commands[2][1]+['paid-shop']),
                         (commands[2][0],commands[2][1]+['empty-shop'])])
    if campaign.get('version',0)>=31:
        commands.append((validation_build/'Release/lcs-campaign-shared-commands-tests.exe',
                         [source/'audio',package/'metadata/lcs-source/DATA/WEAPON.DAT.bin']))
    if campaign.get('version',0)>=32:
        map_report=json.loads((package/'metadata/map-preview.json').read_text())
        # The executable is read as PE data; only relocated parameter routines
        # run in the isolated test process, never the GTA process or its world.
        commands.append((validation_build/'Release/lcs-native-abi-tests.exe',
                         [pathlib.Path(map_report['gameDirectory'])/'gta_sa.exe']))
    if campaign.get('version',0)>=33:
        commands.extend([(validation_build/'Release/lcs-campaign-entity-family-tests.exe',[]),
                         (validation_build/'Release/lcs-campaign-statistics-family-tests.exe',[]),
                         (validation_build/'Release/lcs-native-entity-backend-tests.exe',
                          [pathlib.Path(map_report['gameDirectory'])/'gta_sa.exe']),
                         (pathlib.Path(sys.executable),['-B',ROOT/'tests/source_family_contracts.py'])])
    if campaign.get('version',0)>=34:
        commands.append((validation_build/'Release/lcs-campaign-infrastructure-family-tests.exe',[]))
    if campaign.get('version',0)>=36:
        commands.append((validation_build/'Release/lcs-campaign-frenzy-family-tests.exe',[]))
    if campaign.get('version',0)>=37:
        commands.append((validation_build/'Release/lcs-campaign-environment-family-tests.exe',[]))
    if campaign.get('version',0)>=38:
        commands.append((validation_build/'Release/lcs-campaign-garage-family-tests.exe',[]))
    if campaign.get('version',0)>=39:
        commands.append((validation_build/'Release/lcs-campaign-traffic-family-tests.exe',[]))
    if campaign.get('version',0)>=42:
        commands.append((validation_build/'Release/lcs-activity-statistics-tests.exe',[]))
    if campaign.get('version',0)>=44:
        commands.append((pathlib.Path(sys.executable),['-B',ROOT/'tests/original_activity_contracts.py']))
    if campaign.get('version',0)>=45:
        commands.append((pathlib.Path(sys.executable),['-B',ROOT/'tests/original_vehicle_contracts.py']))
    if campaign.get('version',0)>=47:
        commands.append((validation_build/'Release/lcs-mission-message-tests.exe',[]))
    if campaign.get('version',0)>=50:
        commands.append((validation_build/'Release/lcs-wander-path-tests.exe',[]))
    if campaign.get('version',0)>=57:
        commands.extend([(validation_build/'Release/lcs-ending-tune-transport-tests.exe',[]),
                         (validation_build/'Release/lcs-owned-audio-output-tests.exe',[]),
                         (validation_build/'Release/lcs-ending-tune-source-tests.exe',
                          [ORIGINAL_AUDIO/'MUSIC/FINALE.VB' if campaign.get('version',0)>=90 else package/'metadata/lcs-source/AUDIO/MUSIC/FINALE.VB.bin']),
                         (pathlib.Path(sys.executable),['-B',ROOT/'tests/original_ending_tune_contracts.py',
                          pathlib.Path(map_report['gameDirectory'])/'gta_sa.exe'])])
    if campaign.get('version',0)>=58:
        for name in ('original_remote_cancel_contracts','original_save_menu_contracts',
                     'original_front_seat_contracts','original_pickup_weapon_contracts'):
            commands.append((pathlib.Path(sys.executable),['-B',ROOT/'tests'/f'{name}.py',
                             pathlib.Path(map_report['gameDirectory'])/'gta_sa.exe']))
    if campaign.get('version',0)>=59:
        commands.append((validation_build/'Release/lcs-resource-tests.exe',[source]))
    if campaign.get('version',0)>=60:
        commands.extend([(validation_build/'Release/lcs-script-coronas-tests.exe',[]),
                         (validation_build/'Release/lcs-script-corona-backend-tests.exe',[]),
                         (pathlib.Path(sys.executable),['-B',ROOT/'tests/original_script_corona_contracts.py',
                          pathlib.Path(map_report['gameDirectory'])/'gta_sa.exe'])])
    if campaign.get('version',0)>=62:
        commands.extend([(validation_build/'Release/lcs-hud-tests.exe',[]),
                         (validation_build/'Release/lcs-hud-render-tests.exe',[]),
                         (pathlib.Path(sys.executable),['-B',ROOT/'tests/original_hud_presentation_contracts.py',
                          pathlib.Path(map_report['gameDirectory'])/'gta_sa.exe'])])
    if campaign.get('version',0)>=61:
        commands.extend([(validation_build/'Release/lcs-collectives-tests.exe',[]),
            (pathlib.Path(sys.executable),['-B',ROOT/'tests/original_collective_contracts.py',
             pathlib.Path(map_report['gameDirectory'])/'gta_sa.exe'])])
    if campaign.get('version',0)>=63:
        commands.extend([(validation_build/'Release/lcs-script-markers-tests.exe',[]),
                         (validation_build/'Release/lcs-script-marker-pool-tests.exe',[]),
                         (validation_build/'Release/lcs-script-marker-render-tests.exe',[validation_build/'marker-render-fixture']),
                         (pathlib.Path(sys.executable),['-B',ROOT/'tests/original_marker_contracts.py',
                          pathlib.Path(map_report['gameDirectory'])/'gta_sa.exe']),
                         (pathlib.Path(sys.executable),['-B',ROOT/'tools/validate-script-marker-resources.py',package])])
    if campaign.get('version',0)>=90:
        commands.append((validation_build/'Release/lcs-ps2-audio-source-tests.exe',
                         [ORIGINAL_AUDIO,source/'audio',package/'data/lcs/cutscenes/index.tsv']))
    if campaign.get('version',0)>=92:
        commands.append((validation_build/'Release/lcs-radio-tests.exe',[ORIGINAL_AUDIO]))
    checks=[]
    for exe,args in commands:
        result=subprocess.run([str(exe),*(str(p) for p in args)],capture_output=True,text=True,timeout=60)
        suffix='-'+args[-1] if args and args[-1] in ('paid-shop','empty-shop') else ''
        python_script=next((pathlib.Path(p) for p in args if pathlib.Path(p).suffix=='.py'),None) if exe==pathlib.Path(sys.executable) else None
        if exe==pathlib.Path(sys.executable) and python_script is None:raise ValueError('Python validation script missing')
        python_validator=python_script.stem.replace('_','-') if python_script else None
        name=(python_validator or exe.stem)+suffix+'.log'
        (package/'metadata'/name).write_text(result.stdout+result.stderr)
        if result.returncode or 'PASS:' not in result.stdout:raise RuntimeError(exe.name+' failed: '+result.stderr+'; see '+name)
        check=dict(executable=exe.name,sha256=digest(exe),output=name,outputSha256=digest(package/'metadata'/name),passed=True)
        if campaign.get('version',0)>=33 and python_script:check['validatorSha256']=digest(python_script)
        checks.append(check)
    if campaign.get('version',0)>=32:
        import shutil
        powershell=pathlib.Path(shutil.which('powershell') or '')
        if not powershell.is_file():raise RuntimeError('PowerShell is required for independent bootstrap validation')
        validator=ROOT/'tools/validate-bootstrap.ps1'
        result=subprocess.run([str(powershell),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(validator),
                               '-ScmFile',str(package/'data/script/main.scm')],capture_output=True,text=True,timeout=60)
        name='native-bootstrap-player-exports.log';(package/'metadata'/name).write_text(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError('Bootstrap player export validation failed: '+result.stderr)
        audit=json.loads(result.stdout)
        if not audit['NativeBootstrapValidated'] or not audit['NativePlayerGlobalsExported']:raise RuntimeError('Native mod player exports absent')
        checks.append(dict(executable=powershell.name,sha256=digest(powershell),validatorSha256=digest(validator),
                           output=name,outputSha256=digest(package/'metadata'/name),passed=True))
    paths=['LcsSaBridge.asi','data/lcs/campaign-model-bindings.tsv','data/lcs/campaign-anchor-bindings.tsv',
           'data/lcs/native-equivalents.tsv','data/lcs/peds/canonical-model-bindings.tsv','data/lcs/cutscenes/index.tsv',
           'metadata/lcs-source/DATA/MAIN.SCM.bin','data/gta.dat',
           'data/lcs/animations/storage.json','data/lcs/animations/index.tsv',
           'data/lcs/animations/LG00.ifp.bin','data/lcs/animations/named/LN00.ifp.bin']
    if campaign.get('version',0)>=27:paths.append('metadata/lcs-source/DATA/WEAPON.DAT.bin')
    if campaign.get('version',0)>=32:paths.append('data/script/main.scm')
    if 57<=campaign.get('version',0)<90:paths.append('metadata/lcs-source/AUDIO/MUSIC/FINALE.VB.bin')
    if 64<=campaign.get('version',0)<90:paths.append('metadata/lcs-source/AUDIO/MUSIC/CITY.VB.bin')
    if campaign.get('version',0)>=66:
        paths+=['metadata/lcs-source/DATA/DEFAULT.IDE.bin','metadata/lcs-source/DATA/HANDLING.CFG.bin']
    if campaign.get('version',0)>=82:paths+=['data/lcs/info.zon','data/lcs/map.zon','metadata/lcs-source/DATA/GTA3.ZON.bin']
    if campaign.get('version',0)>=83:paths.append('data/timecyc.dat')
    if campaign.get('version',0)>=63:
        paths+=['metadata/script-marker-resources.json','markers/rw-assets/lcs_marker1.dff',
                'markers/rw-assets/lcs_marker2.dff','markers/rw-assets/lcs_marker5.dff',
                'markers/texture-assets/lcs_markers.txd']
    if campaign.get('vehicleFleet'):
        paths += [campaign['vehicleFleet']['manifest'],'data/vehicles.ide','data/handling.cfg']
        for relative in ('data/lcs/vehicle-colours.dat','data/carcols.dat','models/generic/vehicle.txd'):
            if (package/relative).is_file():paths.append(relative)
    proof=dict(valid=True,gameExecuted=False,campaignPlayable=False,engineMocked=True,checks=checks,
               inputs=[dict(relative=p,sha256=digest(package/p)) for p in paths])
    proof_file=package/'metadata/campaign-startup-validation.json';proof_file.write_text(json.dumps(proof,indent=2))
    campaign['nativeStartupValidated']=True;campaign['startupValidationSha256']=digest(proof_file)
    metadata.write_text(json.dumps(campaign,indent=2));return proof
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package',type=pathlib.Path,required=True);parser.add_argument('--source',type=pathlib.Path,default=ROOT/'out/lcs-converted')
    parser.add_argument("--validation-build",type=pathlib.Path,help="Existing offline validator build; defaults to the current build")
    args=parser.parse_args();print(json.dumps(validate(args.package.resolve(),args.source.resolve(),args.validation_build),indent=2))
