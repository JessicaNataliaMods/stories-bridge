"""Audit native skin replacements without launching GTA."""
import hashlib
import json
import pathlib
import re


# peds.ide fields: type, stats, animation group, and the ped/voice types of the revised slots.
IDE_REVISABLE_FIELDS = {3, 4, 5, 11, 12, 13}


def ide(path):
    rows = {}
    for line in path.read_text(encoding='ascii').splitlines():
        text = line.split('#')[0].strip()
        if re.match(r'^\d+\s*,', text):
            fields = [f.strip() for f in text.split(',')]
            ident = int(fields[0])
            if ident in rows:
                raise ValueError('Duplicate ped IDE slot')
            rows[ident] = fields
    return rows


def groups(path):
    return [re.findall(r'[A-Za-z0-9_]+', line.split('#')[0])
            for line in path.read_text(encoding='ascii').splitlines()
            if line.split('#')[0].strip()]


def validate(package, game):
    profile_path = package / 'metadata/gameplay-peds.json'
    if not profile_path.exists():
        return None
    profile = json.loads(profile_path.read_text())
    source = pathlib.Path(profile['source'])
    manifest = json.loads((source / 'manifest.json').read_text())
    if not manifest['complete'] or manifest['failures'] or not manifest['allStreamedPopulationConverted']:
        raise ValueError('Incomplete ped conversion')
    if manifest['playerImgModified'] or len(manifest['converted']) != 139:
        raise ValueError('Player archive modified or source inventory changed')
    if 'reread_validation_passed=true' not in manifest['actorRereadValidation']:
        raise ValueError('Actor stream reread validation missing')
    rows = profile['bindings']
    original, installed = ide(game / 'data/peds.ide'), ide(package / 'data/peds.ide')
    if original.keys() != installed.keys() or len(rows) != profile['uniqueNativeReplacements']:
        raise ValueError('Native slot inventory changed')
    by_id = {r['nativeModelId']: r for r in rows}
    if len(by_id) != len(rows) or any(i <= 0 or i >= 290 for i in by_id):
        raise ValueError('Duplicate or reserved native skin slot')
    if len({r['nativeDictionary'].lower() for r in rows}) != len(rows):
        raise ValueError('Shared native TXD replacement')
    source_rows = {r['asset']: r for r in manifest['converted']}
    aliases = profile['identicalExtraAliases']
    canonical = [r for r in rows if r.get('canonicalMissionBinding')]
    if len(canonical) != 138 or len({r['asset'] for r in canonical}) != 138:
        raise ValueError('Canonical mission skin identities are incomplete/duplicated')
    canonical_index = ''.join(str(r['originalModelId'])+'\t'+str(r['nativeModelId'])+'\t'+r['originalName']+'\t'+r['nativeName']+'\t'+r['asset']+'\n' for r in canonical)
    if (package/'data/lcs/peds/canonical-model-bindings.tsv').read_text() != canonical_index:
        raise ValueError('Canonical mission mapping mismatch')
    if set(source_rows) != {r['asset'] for r in rows} | set(aliases):
        raise ValueError('Source skins omitted from bindings')
    for alias, canonical_asset in aliases.items():
        for ext in ('.dff', '.txd'):
            if (source / 'rw-assets' / (alias + ext)).read_bytes() != (source / 'rw-assets' / (canonical_asset + ext)).read_bytes():
                raise ValueError('Nonidentical source skins aliased together')
    expected_files = set()
    # tools/revise-gameplay-peds.py may retype a bound slot (type, stats, animation group, voices); the model/TXD
    # identity, car masks and everything else stay the stock SA values, and unbound slots are untouched.
    for ident, native in original.items():
        if installed[ident] != native:
            if ident not in by_id or len(installed[ident]) != len(native) or                     any(a != b for i, (a, b) in enumerate(zip(native, installed[ident])) if i not in IDE_REVISABLE_FIELDS):
                raise ValueError('Unexpected native IDE field change')
    for row in rows:
        native = original[row['nativeModelId']]
        if native[1].lower() != row['nativeName'].lower() or native[2].lower() != row['nativeDictionary'].lower():
            raise ValueError('Native skin identity mismatch')
        src = source_rows[row['asset']]
        if any(src[k] != row[k] for k in ('sourceSHA256', 'originalName', 'sourceKind', 'meshAudit')):
            raise ValueError('Source ped identity changed')
        audit = src['meshAudit']
        if audit['nativeBones'] != 32 or not audit['bindMatricesVerified'] or not audit['topologyUVPrelightWeightsPreserved']:
            raise ValueError('Ped mesh adaptation not verified')
        if not audit.get('rigAudit',{}).get('sourceBoneLengthsVerified'):
            raise ValueError('Character proportions were not verified')
        for ext, name in (('.dff', row['nativeName']), ('.txd', row['nativeDictionary'])):
            target = package / 'models/lcs/peds' / (name + ext)
            expected_files.add(target.name.lower())
            if target.read_bytes() != (source / 'rw-assets' / (row['asset'] + ext)).read_bytes():
                raise ValueError('Installed ped bytes differ from audited conversion')
    if {p.name.lower() for p in (package / 'models/lcs/peds').iterdir()} != expected_files:
        raise ValueError('Unexpected skin replacement files')
    index = ''.join(str(r['nativeModelId'])+'\t'+r['nativeName']+'\t'+r['asset']+'\n' for r in rows)
    if (package / 'data/lcs/peds/index.tsv').read_text() != index:
        raise ValueError('Runtime culling index mismatch')
    before, after = groups(game / 'data/pedgrp.dat'), groups(package / 'data/pedgrp.dat')
    ambient = {r['nativeName'].lower() for r in rows if r['sourceKind'] == 'population' and r['nativeName'] != 'male01'}
    if len(before) != len(after) or any(len(a) != len(b) or len(b) > 32 or any(n.lower() not in ambient for n in b) for a, b in zip(before, after)):
        raise ValueError('Ambient group contains story skin or invalid inventory')
    if (package/'data/pedgrp.dat').read_bytes() != (game/'data/pedgrp.dat').read_bytes():
        raise ValueError('Native population groups changed')
    ini = (package / 'LcsSaBridge.ini').read_text(encoding='utf-16')
    if not re.search(r'^GameplaySkins=1\s*$', ini, re.M):
        raise ValueError('Scoped skin culling is disabled')
    return dict(valid=True, sourceSkins=139, uniqueNativeReplacements=len(rows),
                populationSkins=manifest['populationSourceCount'], canonicalMissionBindings=len(canonical),
                nativePopulationByteExact=False, nativePedGroupsByteExact=True, sourceBodyProportionsVerified=True, gameExecuted=False,
                runtimeVerified=False, playerImgModified=False)
