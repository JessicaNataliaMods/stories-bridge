"""Stage the original ped animations over the exact current local package.

Replaces only the animation resources: anim/ped.ifp (tools/prepare-ped-animations.py)
and the mission gesture/named blocks (tools/campaign_animation_storage.py, with the
thigh retarget of tools/lcs_ifp.py). The ASI, map, vehicles, weapons, HUD and
audio are copied unchanged. The normal startup and package validators still
apply; nothing is installed and GTA is never executed.
"""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path

import campaign_animation_storage

ROOT = Path(__file__).resolve().parents[1]
STORAGE = 'data/lcs/animations'


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def clone(base, output):
    """Metadata, ASI and ini are copied; every other file shares the base inode."""
    linked = 0
    for source in sorted(base.rglob('*')):
        relative = source.relative_to(base)
        if source.is_symlink() or source.is_junction():
            raise ValueError('Package source must contain no reparse points')
        if relative.parts[0].lower() == 'audio' or source.suffix.lower() == '.log':
            continue
        target = output / relative
        if source.is_dir():
            target.mkdir(exist_ok=True)
        elif relative.parts[0] == 'metadata' or source.suffix.lower() in ('.asi', '.ini'):
            shutil.copy2(source, target)
        else:
            os.link(source, target)
            linked += 1
    return linked


def replace(target, source):
    """New inode: the old one is shared with the base package and its rollback."""
    temporary = target.with_name(target.name + '.new')
    shutil.copyfile(source, temporary)
    os.replace(temporary, target)


def prepare(base, output, version, ped, gestures, named, asi=None):
    base, output = base.resolve(), output.resolve()
    if output.exists() or base == output or base in output.parents or output in base.parents:
        raise ValueError('Choose a new separate private staging directory')
    campaign = json.loads((base / 'metadata/campaign.json').read_text(encoding='utf-8-sig'))
    if campaign['mode'] != 'Campaign' or campaign['version'] >= version:
        raise ValueError('Invalid campaign source/version')
    ped_report = json.loads((ped / 'report.json').read_text())
    if ped_report['gameExecuted'] or ped_report['installed'] or digest(ped / 'ped.ifp') != ped_report['output']['sha256']:
        raise ValueError('Prepared ped animation block differs from its report')
    output.mkdir(parents=True)
    linked = clone(base, output)
    # Mission blocks: the same bundling as the original package, from the retargeted sources.
    bundle = output / '.animation-storage'
    storage = campaign_animation_storage.prepare(gestures, named, bundle)
    changed = {}
    for source in sorted(bundle.rglob('*')):
        if source.is_dir():
            continue
        relative = STORAGE + '/' + source.relative_to(bundle).as_posix()
        target = output / relative
        if not target.exists():
            raise ValueError('Unexpected animation storage file: ' + relative)
        replace(target, source)
        changed[relative] = digest(target)
    shutil.rmtree(bundle)
    (output / 'anim').mkdir(exist_ok=True)
    replace(output / 'anim/ped.ifp', ped / 'ped.ifp')
    changed['anim/ped.ifp'] = digest(output / 'anim/ped.ifp')
    if asi:
        # The installed ASI may be newer than the base stage (ASI-only updates); keep it.
        replace(output / 'LcsSaBridge.asi', asi)
        for row in campaign['originalResources']:
            if row['relative'] == 'LcsSaBridge.asi':
                row['sha256'] = digest(output / 'LcsSaBridge.asi')
                row['source'] = str(asi)
    campaign['version'] = version
    campaign['nativeStartupValidated'] = False
    campaign.pop('startupValidationSha256', None)
    rows = {row['relative']: row for row in campaign['originalResources']}
    for relative, sha in changed.items():
        if relative in rows:
            rows[relative]['sha256'] = sha
        else:
            campaign['originalResources'].append(dict(relative=relative, sha256=sha))
    campaign['animationStorage']['sha256'] = changed[STORAGE + '/storage.json']
    campaign['pedAnimations124'] = dict(
        block='anim/ped.ifp', sha256=changed['anim/ped.ifp'], report=ped_report,
        originalSource='out/lcs-converted/anim/ped.ifp',
        gestureSources=dict(directory=str(gestures), manifestSha256=digest(gestures / 'manifest.json')),
        namedSources=dict(directory=str(named), manifestSha256=digest(named / 'manifest.json')),
        thighsRelativeToPelvis=True, weaponBlocksUnchanged=True, gameExecuted=False, visualVerified=False)
    (output / 'metadata/campaign.json').write_text(json.dumps(campaign, indent=2) + '\n')
    preview = json.loads((output / 'metadata/map-preview.json').read_text(encoding='utf-8-sig'))
    preview['version'] = str(version)
    (output / 'metadata/map-preview.json').write_text(json.dumps(preview, indent=2) + '\n')
    return dict(stage=str(output), version=version, sharedAssets=linked, replaced=changed, gameExecuted=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', type=int, required=True)
    parser.add_argument('--ped', type=Path, default=ROOT / 'out/ped-animations-v1')
    parser.add_argument('--gestures', type=Path, default=ROOT / 'out/gameplay-animations-v4')
    parser.add_argument('--named', type=Path, default=ROOT / 'out/named-animations-v2')
    parser.add_argument('--asi', type=Path, help='ASI to carry instead of the base stage ASI')
    arguments = parser.parse_args()
    print(json.dumps(prepare(arguments.base, arguments.output, arguments.version, arguments.ped,
                             arguments.gestures, arguments.named, arguments.asi), indent=2))
