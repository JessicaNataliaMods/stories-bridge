"""Mount converted weapon visuals in a new campaign stage, without installing.

The existing SA weapon types keep their behavior. Shared donor slots have one
explicit canonical visual; all original variants remain in the conversion.
"""
import argparse
import collections
import hashlib
import json
import shutil
import sys
from pathlib import Path
TOOLS_DIR = str(Path(__file__).resolve().parent)
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)
from package_metadata import make_portable


SOURCE_MANIFEST = 'metadata/source-manifests/weapon-conversion/manifest.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def mount(converted, package, game):
    manifest_path = converted / 'manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if not manifest.get('complete') or manifest['failures'] or manifest['gameExecuted']:
        raise ValueError('Weapon conversion must be complete and verified offline')
    campaign_path = package / 'metadata/campaign.json'
    campaign = json.loads(campaign_path.read_text(encoding='utf-8'))
    if campaign['version'] <= 69 or campaign.get('weaponVisuals'):
        raise ValueError('Use a new unmounted stage after v69')
    snapshot_path = package / SOURCE_MANIFEST
    snapshot_path.parent.mkdir(parents=True, exist_ok=True)
    if snapshot_path.exists():
        raise ValueError('Stage already contains a weapon conversion source snapshot')
    shutil.copyfile(manifest_path, snapshot_path)
    source_manifest_sha = digest(snapshot_path)
    by_id = {r['originalModelId']: r for r in manifest['converted']}
    groups = collections.defaultdict(list)
    for line in (package / 'data/lcs/native-equivalents.tsv').read_text().splitlines():
        fields = line.split('\t')
        if len(fields) != 5 or int(fields[0]) not in by_id:
            continue
        source, native, name, target, kind = fields
        if kind != 'object' or name != by_id[int(source)]['originalName'].lower():
            raise ValueError('Weapon source binding mismatch')
        groups[(int(native), target)].append(by_id[int(source)])
    if sum(map(len, groups.values())) != len(by_id):
        raise ValueError('Missing/duplicate weapon model bindings')
    # SA micro_uzi has no identically named original; choose the original Uzi.
    canonical = {'micro_uzi': 'uzi'}
    definitions = {}
    section = ''
    for line in (game / 'data/default.ide').read_text(encoding='ascii').splitlines():
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        if ',' not in line:
            section = '' if line.lower() == 'end' else line.lower()
        elif section == 'weap':
            f = [v.strip() for v in line.split(',')]
            definitions[int(f[0])] = f
    files, slots = [], []
    pending = []
    for (model, target), sources in sorted(groups.items()):
        definition = definitions[model]
        if definition[1:3] != [target, target]:
            raise ValueError('Native weapon model/dictionary identity changed')
        selected = [r for r in sources if r['originalName'].lower() == canonical.get(target, target)]
        if len(sources) == 1:
            selected = sources
        if len(selected) != 1:
            raise ValueError('Shared weapon target needs explicit selection: ' + target)
        row = selected[0]
        slots.append(dict(nativeModel=model, nativeName=target,
                          selectedOriginal=row['originalName'], originalModel=row['originalModelId'],
                          aliases=[r['originalName'] for r in sources if r is not row]))
        for extension in ('.dff', '.txd'):
            source = converted / 'rw-assets' / (row['asset'] + extension)
            actual_sha = digest(source)
            relative = 'weapons/' + target + extension
            pending.append((source, relative, actual_sha))
            files.append(dict(relative=relative, sha256=actual_sha))
    # Refuse another mod's override rather than quietly relying on load order.
    names = {Path(relative).name.casefold() for _, relative, _ in pending}
    destination = (game / 'modloader/stories_bridge').resolve()
    for path in (game / 'modloader').rglob('*'):
        if path.is_file() and path.name.casefold() in names and not path.resolve().is_relative_to(destination):
            raise ValueError('Conflicting weapon override: ' + str(path))
    for source, relative, _ in pending:
        target = package / relative
        if target.exists():
            raise ValueError('Stage already contains weapon override: ' + str(target))
    for source, relative, _ in pending:
        target = package / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read_bytes())
    report = dict(format=1, sourceManifest=SOURCE_MANIFEST, sourceManifestSha256=source_manifest_sha,
                  slots=slots, files=files, installed=False, gameExecuted=False,
                  behaviorChanged=False, audioChanged=False, animationsChanged=False,
                  handAlignmentRuntimeVerified=False)
    relative = 'metadata/weapon-visuals.json'
    metadata = package / relative
    metadata.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    campaign['weaponVisuals'] = dict(manifest=relative, sha256=digest(metadata), slots=len(slots))
    campaign['originalResources'] += files + [dict(relative=relative, sha256=digest(metadata))]
    campaign['nativeStartupValidated'] = False
    campaign.pop('startupValidationSha256', None)
    campaign_path.write_text(json.dumps(campaign, indent=2) + '\n', encoding='utf-8')
    make_portable(package)
    report = json.loads(metadata.read_text(encoding='utf-8'))
    report['sourceManifestSha256'] = digest(package / report['sourceManifest'])
    metadata.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    campaign = json.loads(campaign_path.read_text(encoding='utf-8'))
    campaign['weaponVisuals']['sha256'] = digest(metadata)
    for row in campaign['originalResources']:
        if row.get('relative') == relative:
            row['sha256'] = campaign['weaponVisuals']['sha256']
    campaign_path.write_text(json.dumps(campaign, indent=2) + '\n', encoding='utf-8')
    return dict(slots=len(slots), files=len(files), deferredAliases=sum(len(r['aliases']) for r in slots),
                package=package.name, installed=False, gameExecuted=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--converted', type=Path, required=True, help='weapons directory containing manifest.json')
    parser.add_argument('--package', type=Path, required=True, help='New campaign stage, never an installed mod')
    parser.add_argument('--game', type=Path, required=True)
    args = parser.parse_args()
    for path in (args.package, args.package.resolve()):
        if path.resolve().is_relative_to((args.game / 'modloader').resolve()):
            parser.error('Mount only a separate stage; use the approved installer afterwards')
    print(json.dumps(mount(args.converted.resolve(), args.package.resolve(), args.game.resolve())))
