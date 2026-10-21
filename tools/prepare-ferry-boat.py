"""Prepare the original ferry in its existing TROPIC counterpart slot, offline."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(game, output, converter):
    if output.exists():
        raise ValueError('Output already exists; preserve/review it before another preparation')
    bundle = ROOT/'out/ferry-assets-review-20261019'
    prior = json.loads((bundle/'MANIFEST.json').read_text(encoding='utf-8'))
    for row in prior['files']:
        if digest(bundle/row['relative']) != row['sha256']:
            raise ValueError('Prepared original ferry input changed: '+row['relative'])
    equivalents = (ROOT/'out/campaign-copy4-v64/data/lcs/native-equivalents.tsv').read_text(encoding='ascii')
    if '192\t454\tferry\ttropic\tvehicle' not in equivalents.splitlines():
        raise ValueError('Approved ferry counterpart changed')
    spec = importlib.util.spec_from_file_location('fleet', ROOT/'tools/prepare-campaign-vehicles.py')
    fleet = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fleet)
    output.mkdir(parents=True)
    overlay = output/'overlay'
    for relative in ('vehicles', 'data', 'models/lcs'):
        (overlay/relative).mkdir(parents=True, exist_ok=True)
    logs = output/'logs'
    logs.mkdir()

    def run(name, *args):
        result = subprocess.run([str(converter), *map(str, args)], capture_output=True, text=True)
        (logs/name).write_text(result.stdout+result.stderr, encoding='utf-8')
        if result.returncode:
            raise RuntimeError(name+' failed; see prepared log')

    original_col = bundle/'source/ferry-original.col'
    (output/'original-renamed-ferry.col').write_bytes(fleet.alias_collision(original_col.read_bytes(), 'tropic'))
    run('collision.log', output/'original-renamed-ferry.col', '--map-col', overlay/'models/lcs/ferry.col')
    run('collision-validation.log', overlay/'models/lcs/ferry.col', '--validate-col')
    # Original door/ramp geometry. The raw PS2 clump (GAME.DTZ model 192) has only chassis_vlo/chassis_hi; the door leaves and
    # ramp plates are triangles merged into chassis_hi exactly on the hinge points of the four dummy frames. The split moves
    # them into real atomics attached to door_front/back_dummy and ramp_front/back_dummy (retail component layout) and audits
    # that the world-space triangle set is unchanged; lcs-convert-rw then adapts the 6-atomic clump to the SA boat.
    doors = output/'doors'
    doors.mkdir()
    split = subprocess.run([sys.executable, '-B', str(Path(__file__).resolve().parent/'split_ferry_doors.py'),
                            str(bundle/'models/ferry.dff'), str(doors/'ferry-doors.dff'), '--audit', str(logs/'door-split-audit.json')],
                           capture_output=True, text=True)
    (logs/'door-split.log').write_text(split.stdout+split.stderr, encoding='utf-8')
    if split.returncode:
        raise RuntimeError('door-split.log failed; see prepared log')
    audit = json.loads((logs/'door-split-audit.json').read_text(encoding='utf-8'))
    if not audit['world_space_triangle_multiset_identical'] or audit['moved'] != dict(door_front=8, door_back=8, ramp_front=16, ramp_back=16):
        raise ValueError('Door/ramp split no longer matches the audited original geometry')
    run('boat-conversion.log', doors/'ferry-doors.dff', overlay/'vehicles/tropic.dff', '--sa-boat', overlay/'models/lcs/ferry.col')
    run('textures.log', bundle/'models/ferry.txd', '--merge-txd',
        ROOT/'out/campaign-entities-v2/generic.txd', overlay/'vehicles/tropic.txd')
    run('boat-validation.log', overlay/'vehicles/tropic.dff', '--validate-sa-boat')
    run('geometry-validation.log', overlay/'vehicles', '--validate-actors')
    run('hierarchy.log', overlay/'vehicles/tropic.dff', '--info')

    native_ide = (game/'data/vehicles.ide').read_text(encoding='ascii')
    native_rows = fleet.read_ide(game/'data/vehicles.ide')
    slot = next(row for row in native_rows if int(row[0]) == 454)
    if slot[1:5] != ['tropic', 'tropic', 'boat', 'TROPIC']:
        raise ValueError('TROPIC is not the expected native boat class')
    # Keep the native display key and bounded model/handling slot. The source
    # ferry is not a leisure population vehicle and has no random extras.
    replacement = slot[:]
    replacement[6:11] = ['null', 'ignore', '1', '0', '0']
    pattern = r'^454\s*,[^\r\n]+'
    ide, count = re.subn(pattern, ', '.join(replacement), native_ide, flags=re.M)
    if count != 1:
        raise ValueError('Ambiguous native boat definition')
    (overlay/'data/vehicles.ide').write_text(ide, encoding='ascii')
    if len(fleet.read_ide(overlay/'data/vehicles.ide')) != len(native_rows):
        raise ValueError('Native model pool capacity changed')

    source = ROOT/'out/lcs-converted/DATA/HANDLING.CFG'
    native_handling = (game/'data/handling.cfg').read_text(encoding='ascii')
    source_ide = [row for row in fleet.read_ide(ROOT/'out/lcs-converted/DATA/DEFAULT.IDE')
                  if int(row[0]) == 192 and row[1:5] == ['ferry', 'ferry', 'ferry', 'FERRY']]
    if len(source_ide) != 1:
        raise ValueError('Original ferry IDE binding is missing or ambiguous')
    adapted = fleet.adapt_handling(fleet.handling_rows(source, 34)['FERRY'], 'TROPIC', source_ide[0])
    general, unknown = adapted['fields'], adapted['unmapped_flags']
    if unknown:
        raise ValueError('Unmapped source handling bits')
    handling, count = re.subn(r'^TROPIC[ \t]+[^\r\n]+', '\t'.join(general), native_handling, flags=re.M)
    boat = [line.split() for line in source.read_text(encoding='ascii').splitlines()
            if line.strip().startswith('%') and line.split()[1] == 'FERRY']
    native_boat = [line.split() for line in native_handling.splitlines()
                   if line.strip().startswith('%') and line.split()[1] == 'TROPIC']
    if count != 1 or len(boat) != 1 or len(native_boat) != 1 or len(boat[0]) != len(native_boat[0]):
        raise ValueError('Ambiguous or incompatible boat handling fields')
    if not all(math.isfinite(float(value)) for value in boat[0][2:]):
        raise ValueError('Nonfinite original boat handling')
    boat[0][1] = 'TROPIC'
    handling, count = re.subn(r'^%[ \t]+TROPIC[ \t]+[^\r\n]+', '\t'.join(boat[0]), handling, flags=re.M)
    if count != 1:
        raise ValueError('Native boat-specific handling missing')
    (overlay/'data/handling.cfg').write_text(handling, encoding='ascii')
    fleet.handling_rows(overlay/'data/handling.cfg', 36)
    files = [{'relative':p.relative_to(overlay).as_posix(), 'sha256':digest(p), 'bytes':p.stat().st_size}
             for p in sorted(overlay.rglob('*')) if p.is_file()]
    manifest = dict(prepared=True, installed=False, gameExecuted=False, sourceModel=192,
                    converterSha256=digest(converter), hierarchyValidation='logs/boat-validation.log',
                    nativeModel=454, nativeName='tropic', nativeClass='boat', nativeHandling='TROPIC',
                    originalGeometry=True, embeddedCollision=True, originalTexturePrecedence=True, nativeDisplayKey=slot[5],
                    doorRampAtomics=dict(door_front_dummy=1, door_back_dummy=1, ramp_front_dummy=1, ramp_back_dummy=1),
                    doorSplitAudit='logs/door-split-audit.json',
                    ferryCommandsCompleted=[], sourceBundle=str(bundle), sourceBundleManifest=digest(bundle/'MANIFEST.json'),
                    nativeInputs=[dict(path=str(game/'data'/name), sha256=digest(game/'data'/name))
                                  for name in ('vehicles.ide','handling.cfg')], files=files)
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    return manifest


def mount(package, prepared):
    """Mount verified files in a NEW stage; atomic replacement never mutates rollback hardlinks."""
    manifest = json.loads((prepared/'manifest.json').read_text(encoding='utf-8'))
    campaign_path = package/'metadata/campaign.json'
    campaign = json.loads(campaign_path.read_text(encoding='utf-8'))
    if campaign['version'] <= 64:
        raise ValueError('Ferry mount requires a new stage after v64')
    fleet_report = None
    fleet_path = None
    fleet_pins = {}
    if campaign.get('vehicleFleet'):
        fleet_path = package/campaign['vehicleFleet']['manifest']
        if digest(fleet_path) != campaign['vehicleFleet']['sha256']:
            raise ValueError('Mounted vehicle fleet metadata changed')
        fleet_report = json.loads(fleet_path.read_text(encoding='utf-8'))
        fleet_pins = {row['relative']: row['afterSha256'] for row in fleet_report['Configs']}
    if campaign.get('ferryBoat'):
        previous = json.loads((package/'metadata/ferry-boat.json').read_text(encoding='utf-8'))
        if previous['nativeModel'] != 454 or previous['nativeName'] != 'tropic':
            raise ValueError('Existing ferry registration belongs to another slot')
        for row in previous['files']:
            if digest(package/row['relative']) != row['sha256']:
                # A later approved fleet mount changes these shared config
                # files. Its independently pinned result is the new baseline.
                if row['relative'] not in ('data/vehicles.ide', 'data/handling.cfg') or \
                        fleet_pins.get(row['relative']) != digest(package/row['relative']):
                    raise ValueError('Existing stage ferry file changed outside the approved mount')
    equivalents = (package/'data/lcs/native-equivalents.tsv').read_text(encoding='ascii')
    if '192\t454\tferry\ttropic\tvehicle' not in equivalents.splitlines():
        raise ValueError('Stage does not carry the approved ferry binding')

    def write(path, data):
        if not path.resolve().is_relative_to(package.resolve()):
            raise ValueError('Mount target leaves the explicit package directory')
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_name(path.name+'.ferry-tmp')
        with temporary.open('xb') as stream:
            stream.write(data)
        temporary.replace(path)

    mounted_manifest = json.loads(json.dumps(manifest))
    for row in mounted_manifest['files']:
        source = prepared/'overlay'/row['relative']
        if digest(source) != row['sha256']:
            raise ValueError('Prepared ferry file changed: '+row['relative'])
        data = source.read_bytes()
        patterns = {
            'data/vehicles.ide': (br'(?m)^454[ \t]*,[^\r\n]*',),
            'data/handling.cfg': (br'(?m)^TROPIC[ \t]+[^\r\n]*', br'(?m)^%[ \t]+TROPIC[ \t]+[^\r\n]*'),
        }.get(row['relative'], ())
        if patterns:
            # Only the ferry rows belong to this mount. Preserve all sixty
            # converted cars and any other approved package configuration.
            merged = (package/row['relative']).read_bytes()
            for pattern in patterns:
                replacement = re.findall(pattern, data)
                if len(replacement) != 1:
                    raise ValueError('Ambiguous prepared ferry config row')
                merged, count = re.subn(pattern, lambda match: replacement[0], merged)
                if count != 1:
                    raise ValueError('Ambiguous stage ferry config row')
            data = merged
        write(package/row['relative'], data)
        row['preparedSha256'] = row['sha256']
        row['sha256'] = digest(package/row['relative'])
        row['bytes'] = len(data)
    profile_path = package/'data/gta.dat'
    profile = profile_path.read_text(encoding='utf-8')
    anchor = 'COLFILE 0 MODELS\\LCS\\WORLD.COL'
    ferry_line = 'COLFILE 0 MODELS\\LCS\\FERRY.COL'
    if profile.count(anchor) != 1 or profile.count(ferry_line) > 1:
        raise ValueError('Unexpected level profile for ferry collision registration')
    if ferry_line not in profile:
        profile = profile.replace(anchor, anchor+'\n'+ferry_line, 1)
    write(profile_path, profile.encode('utf-8'))
    write(package/'metadata/ferry-boat.json', (json.dumps(mounted_manifest, indent=2)+'\n').encode('utf-8'))
    campaign['ferryBoat'] = dict(source=str(prepared), manifest='metadata/ferry-boat.json',
                                 sha256=digest(package/'metadata/ferry-boat.json'), nativeModel=454,
                                 nativeName='tropic', gameExecuted=False, ferryCommandsCompleted=[])
    hashes = {row['relative']: row['sha256'] for row in mounted_manifest['files']}
    hashes['data/gta.dat'] = digest(profile_path)
    for section in ('originalResources', 'preservedApprovedResources'):
        for row in campaign.get(section, []):
            if row.get('relative') in hashes:
                row['sha256'] = hashes[row['relative']]
    if fleet_report is not None:
        for row in fleet_report['Configs']:
            if row['relative'] in hashes:
                row['afterSha256'] = hashes[row['relative']]
        write(fleet_path, (json.dumps(fleet_report, indent=2)+'\n').encode('utf-8'))
        campaign['vehicleFleet']['sha256'] = digest(fleet_path)
    campaign['nativeStartupValidated'] = False
    campaign.pop('startupValidationSha256', None)
    write(campaign_path, (json.dumps(campaign, indent=2)+'\n').encode('utf-8'))
    return dict(mounted=True, stage=str(package), nativeModel=454, gameExecuted=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT/'out/ferry-boat-tropic-20261019')
    parser.add_argument('--converter', type=Path, default=ROOT/'build/Release/lcs-convert-rw.exe')
    parser.add_argument('--mount-package', type=Path, help='Mount an already prepared overlay in a NEW stage')
    args = parser.parse_args()
    if args.mount_package:
        print(json.dumps(mount(args.mount_package.resolve(), args.output.resolve())))
        raise SystemExit(0)
    result = prepare(args.game.resolve(), args.output.resolve(), args.converter.resolve())
    print(json.dumps(dict(prepared=result['prepared'], nativeModel=result['nativeModel'], files=len(result['files']), gameExecuted=False)))
