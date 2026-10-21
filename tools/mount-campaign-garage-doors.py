"""Mount original LCS garage doors through SA ObjectData and its native controller."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('door_map', ROOT/'tools/prepare-modloader-map.py')
maps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(maps)

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def inputs(package, original):
    definitions = {}
    for raw in (original/'DATA/OBJECT.DAT').read_text().splitlines():
        raw = raw.split(';')[0].strip()
        if not raw or raw.startswith(('#', '*')):
            continue
        fields = re.split(r'[\s,]+', raw)
        if fields[0].lower() in maps.PHYSICAL_DOOR_MODEL_NAMES:
            if len(fields) < 11:
                raise ValueError('Incomplete original door ObjectData: '+fields[0])
            definitions[fields[0].lower()] = fields[1:11]
    bindings = {}
    for raw in (package/'data/lcs/model-bindings.tsv').read_text().splitlines():
        source, native, name, alias = raw.split('\t')
        # Legacy names remain in IsModelIndexADoor although their ObjectData
        # row is commented out or absent (e.g. impex_door). They are not a
        # source dynamic door and must keep their original static placement.
        if name.lower() in maps.PHYSICAL_DOOR_MODEL_NAMES and name.lower() in definitions:
            bindings[alias] = (source, native, name, alias)
    placements = []
    for path in sorted((package/'data/lcs').glob('world[012].ipl')):
        section = ''; index = 0
        for raw in path.read_text().splitlines():
            raw = raw.strip()
            if not raw or raw.startswith('#'):
                continue
            if ',' not in raw:
                section = raw.lower(); continue
            if section != 'inst':
                continue
            fields = [v.strip() for v in raw.split(',')]
            if fields[1] in bindings:
                item = bindings[fields[1]]
                if fields[0] != item[1]:
                    raise ValueError('Door IPL identity mismatch')
                placements.append('\t'.join((*item, path.name, str(index), *fields[3:6], 'object')))
            index += 1
    used = {line.split('\t')[3] for line in placements}
    if not used:
        raise ValueError('No original garage door placements')
    rows = ['; LCS garage doors only; merged by Modloader std.data with native OBJECT.DAT']
    for alias in sorted(used):
        item = bindings[alias]
        if item[2].lower() not in definitions:
            raise ValueError('Placed original garage ObjectData missing: '+item[2])
        # The ten original physical fields have the same SA meaning. SA adds
        # explosion/FX selectors and optional FX parameters; none are authored here.
        rows.append(', '.join((alias, *definitions[item[2].lower()], '0', '0', '0.0', '0.0', '0.0', 'none')))
    rows.append('*')
    return bindings, used, '\n'.join(placements)+'\n', '\n'.join(rows)+'\n'

def replace(path, data):
    # Package assets can be hardlinked to the approved rollback.
    temp = path.with_name(path.name+'.door-update')
    temp.write_bytes(data); temp.replace(path)

def mount(package, original):
    bindings, used, placements, objects = inputs(package, original)
    path = package/'data/lcs/world.ide'
    rows = []; changed = []
    section = ''
    for raw in path.read_text().splitlines():
        if raw in ('objs','tobj','txdp','end'):
            section = '' if raw == 'end' else raw
        elif section == 'objs':
            fields = [v.strip() for v in raw.split(',')]
            if fields[1] in used:
                fields[4] = str(int(fields[4], 0) | 0x800)
                raw = ', '.join(fields); changed.append(fields[1])
        rows.append(raw)
    if set(changed) != used or len(changed) != len(used):
        raise ValueError('Door IDE inventory mismatch')
    replace(path, ('\n'.join(rows)+'\n').encode('ascii'))
    replace(package/'data/object.dat', objects.encode('ascii'))
    replace(package/'data/lcs/door-bindings.tsv', placements.encode('ascii'))
    report = dict(source=str(original.resolve()), sourceObjectDataSha256=digest(original/'DATA/OBJECT.DAT'),
                  models=len(used), placements=len(placements.splitlines()),
                  aliases=sorted(used), gameExecuted=False,
                  files=[dict(relative=name, sha256=digest(package/name)) for name in
                         ('data/lcs/world.ide','data/object.dat','data/lcs/door-bindings.tsv')])
    relative = 'metadata/garage-doors.json'
    replace(package/relative, (json.dumps(report, indent=2)+'\n').encode())
    campaign_path = package/'metadata/campaign.json'
    campaign = json.loads(campaign_path.read_text())
    campaign['garageDoors'] = dict(manifest=relative, sha256=digest(package/relative))
    for row in campaign['preservedApprovedResources']:
        if row['relative'] == 'data/lcs/world.ide':
            row['sha256'] = digest(path)
    replace(campaign_path, json.dumps(campaign, indent=2).encode())
    return report

def audit(package):
    campaign = json.loads((package/'metadata/campaign.json').read_text())
    pin = campaign['garageDoors']; manifest = package/pin['manifest']
    if digest(manifest) != pin['sha256']:
        raise ValueError('Garage door manifest changed')
    report = json.loads(manifest.read_text()); original = Path(report['source'])
    if digest(original/'DATA/OBJECT.DAT') != report['sourceObjectDataSha256']:
        raise ValueError('Original garage ObjectData changed')
    bindings, used, placements, objects = inputs(package, original)
    if (package/'data/object.dat').read_text() != objects or (package/'data/lcs/door-bindings.tsv').read_text() != placements:
        raise ValueError('Garage doors differ from original data/IPL identities')
    if report['models'] != len(used) or report['placements'] != len(placements.splitlines()) or report['aliases'] != sorted(used):
        raise ValueError('Garage inventory changed')
    for row in report['files']:
        if digest(package/row['relative']) != row['sha256']:
            raise ValueError('Garage package input changed: '+row['relative'])
    marked = {fields[1] for section, fields in maps.definitions(package/'data/lcs/world.ide')
              if section == 'objs' and fields[1] in used and int(fields[4], 0)&0x800}
    if marked != used:
        raise ValueError('Native garage door model flag missing')
    return dict(valid=True, models=len(used), placements=len(placements.splitlines()), gameExecuted=False)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--original', type=Path, default=ROOT/'out/lcs-converted')
    parser.add_argument('--audit', action='store_true')
    args = parser.parse_args()
    print(json.dumps(audit(args.package) if args.audit else mount(args.package, args.original), indent=2))
