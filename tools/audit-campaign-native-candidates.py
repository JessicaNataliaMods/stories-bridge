"""Compare conservative SCM inventory with primary LCS/SA decode signatures.

Matching names/counts are review candidates, never automatic runtime support.
No original game assets are embedded in this tool or its JSON output.
"""
import argparse
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
PRIMITIVE_TYPES = {'Char', 'Car', 'Boat', 'Heli', 'Object', 'Pickup', 'ScriptFire',
                   'Blip', 'SeatId', 'WheelId', 'any', 'int', 'float', 'bool'}


def audit(inventory):
    database = json.loads((ROOT/'vendor/sa-reference/sa-commands.json').read_text())
    native = {row['name']: row for row in database['extensions'][0]['commands']}
    source = (ROOT / 'local/references/res-relcs/src/control/ScriptDebug.cpp').read_text()
    pattern = r'REGISTER_COMMAND\((COMMAND_[A-Z0-9_]+), INPUT_ARGUMENTS\(([^)]*)\), OUTPUT_ARGUMENTS\(([^)]*)\), (true|false)'
    original = {name: (len(re.findall('ARGTYPE_', inputs)),
                      len(re.findall('ARGTYPE_', outputs)), predicate == 'true')
                for name, inputs, outputs, predicate in re.findall(pattern, source)}
    rows = []
    for use in inventory['commands']:
        if use['sourceHandlerClauses']:
            continue
        name = use['name']
        command = native.get(name.removeprefix('COMMAND_'))
        if not command or command.get('attrs', {}).get('is_unsupported') or command.get('attrs', {}).get('is_nop'):
            continue
        inputs, outputs = command.get('input', []), command.get('output', [])
        signature = (len(inputs), len(outputs), bool(command.get('attrs', {}).get('is_condition')))
        types = [row['type'] for row in inputs]
        output_types = [row['type'] for row in outputs]
        rows.append(dict(name=name, uses=use['instructionSites'], sa=command['id'],
                         original=original.get(name), native=signature, types=types,
                         outputs=output_types,
                         plainNumericCandidate=original.get(name) == signature and
                         all(value in PRIMITIVE_TYPES for value in types + output_types)))
    rows.sort(key=lambda row: -row['uses'])
    return dict(runtimeSupportVerified=False, matchingSignaturesRequireSemanticReview=True,
                originalDebugSignaturesMayContainErrors=True, candidates=rows)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inventory', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    args = parser.parse_args()
    result = audit(json.loads(args.inventory.read_text(encoding='utf-8-sig')))
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    for row in result['candidates']:
        print(('NUMERIC' if row['plainNumericCandidate'] else 'ADAPTER'), row)
