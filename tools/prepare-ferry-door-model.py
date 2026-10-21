"""Rebuild the ferry model WITH its original door/ramp geometry as real atomics, offline and deterministic.

    py -3 tools/prepare-ferry-door-model.py --converter build/Release/lcs-convert-rw.exe --output out/ferry-doors

Inputs (all inside the repository, hash-checked against the prepared bundle manifest):
  out/ferry-assets-review-20261019/models/ferry.dff   original-layout ferry, 2 atomics (chassis_vlo, chassis_hi)
  out/ferry-assets-review-20261019/models/ferry.col   mapped undocked collision (embedded into the SA boat as COL3)

Why a split is needed: the raw PS2 clump of model 192 (GAME.DTZ, GTA3PS2.IMG) has exactly two elements and the four
door/ramp dummies carry none; the door leaves and ramp plates are triangles merged into chassis_hi that sit on the hinge
points of those dummies (<0.01 m). tools/split_ferry_doors.py moves them into four atomics attached to the dummies and audits
that the world-space triangle multiset is unchanged. lcs-convert-rw --sa-boat (with the src/convert_vehicle.cpp ferry-door
rule) then produces the SA boat variant; both outputs are validated by the converter.

Outputs: ferry.dff (6 atomics, original layout), tropic.dff (SA boat 454), logs/*.log, door-split-audit.json, manifest.json.
"""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {'ferry.dff': '579651cdd317bddb1524d7de15dd3decba95a261f8425036e951f4f907c4bb9a',
            'tropic.dff': '2f31d859e723e6b7a9e28d78f6eb4974916139fc18ca05e50acb21ee734ce10b'}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--converter', type=Path, default=ROOT/'build/Release/lcs-convert-rw.exe')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--bundle', type=Path, default=ROOT/'out/ferry-assets-review-20261019')
    args = parser.parse_args()
    bundle, out = args.bundle.resolve(), args.output.resolve()
    if out.exists():
        raise SystemExit('Output already exists; preserve/review it before another preparation')
    prior = json.loads((bundle/'MANIFEST.json').read_text(encoding='utf-8'))
    for row in prior['files']:
        if digest(bundle/row['relative']) != row['sha256']:
            raise SystemExit('Prepared bundle input changed: '+row['relative'])
    (out/'logs').mkdir(parents=True)

    def run(name, command):
        result = subprocess.run([str(c) for c in command], capture_output=True, text=True)
        (out/'logs'/name).write_text(result.stdout+result.stderr, encoding='utf-8')
        if result.returncode:
            raise SystemExit(name+' failed; see the log')
        return result.stdout

    converter = args.converter.resolve()
    run('door-split.log', [sys.executable, '-B', Path(__file__).resolve().parent/'split_ferry_doors.py',
                            bundle/'models/ferry.dff', out/'ferry.dff', '--audit', out/'door-split-audit.json'])
    audit = json.loads((out/'door-split-audit.json').read_text(encoding='utf-8'))
    if not audit['world_space_triangle_multiset_identical'] or audit['moved'] != dict(door_front=8, door_back=8, ramp_front=16, ramp_back=16):
        raise SystemExit('The door/ramp split no longer matches the audited original geometry')
    run('boat-conversion.log', [converter, out/'ferry.dff', out/'tropic.dff', '--sa-boat', bundle/'models/ferry.col'])
    if 'sa_boat_hierarchy_validated=true' not in run('boat-validation.log', [converter, out/'tropic.dff', '--validate-sa-boat']):
        raise SystemExit('The converter does not accept the SA boat hierarchy')
    run('validate-actors.log', [converter, out, '--validate-actors'])
    manifest = {name: dict(sha256=digest(out/name), expected=EXPECTED[name], matchesExpected=digest(out/name) == EXPECTED[name]) for name in EXPECTED}
    manifest['converterSha256'] = digest(converter)
    manifest['gameExecuted'] = False
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(manifest))
    if not all(manifest[name]['matchesExpected'] for name in EXPECTED):
        raise SystemExit('Output differs from the audited hashes (converter or input drifted)')


if __name__ == '__main__':
    main()
