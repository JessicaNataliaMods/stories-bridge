"""Classify missing MAIN.SCM opcode dispatches against the exact retail ELF.

Data-only audit. Sharing a handler or a non-default entry never establishes
native SA behavior or grants a source-handler clause. No game is executed.
"""
import argparse
from collections import defaultdict
import importlib.util
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser()
    p.add_argument('campaign_inventory',type=Path)
    args=p.parse_args()
    report=json.loads(args.campaign_inventory.read_text(encoding='utf-8'))
    if not report.get('inventoryComplete') or report.get('decodeErrors') or report.get('unresolvedDynamicLabels'):
        raise ValueError('Refusing unverified/partial MAIN.SCM inventory')
    module=ROOT/'tools/inspect-original-command.py'
    spec=importlib.util.spec_from_file_location('source_dispatch_gap',module)
    retail=importlib.util.module_from_spec(spec)
    sys.modules[spec.name]=retail
    spec.loader.exec_module(retail)
    original=retail.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
    original.require_profile()
    missing=[item for item in report['commands'] if not item['sourceHandlerClauses']]
    if len(missing)!=report['commandsWithoutSourceHandlerClause']:
        raise ValueError('Campaign missing-source count does not match entries')
    if len({item['opcode'] for item in missing})!=len(missing):
        raise ValueError('Duplicate source opcode')
    rows=[];owners=defaultdict(list)
    for item in missing:
        dispatch=original.handler(item['opcode'])
        row={'opcode':item['opcode'],'name':item['name'],'instructionSites':item['instructionSites'],
            'elfHandler':hex(dispatch['handler']),'retailDefault':dispatch['default']}
        rows.append(row)
        owners[dispatch['handler']].append(row['opcode'])
    default=[row for row in rows if row['retailDefault']]
    shared=[{'handler':hex(address),'opcodes':names} for address,names in owners.items() if len(names)>1]
    result={'gameExecuted':False,'saSupportGranted':False,'sourceClauses':report['uniqueCommands']-len(missing),
        'originalUnique':report['uniqueCommands'],'missingCount':len(rows),
        'retailDefaultCount':len(default),'retailDefault':default,'sharedRetailHandlers':shared,'missing':rows}
    print(json.dumps(result,indent=2))
    return 0
if __name__=='__main__':
    sys.exit(main())
