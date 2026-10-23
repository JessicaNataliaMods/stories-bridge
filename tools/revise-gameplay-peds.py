"""Revise the gameplay ped slots of an installed campaign package. Never executes GTA.

Problems of the v18 slot assignment, fixed here:
  * Story characters (Vincenzo, Tool, M_Hole) sat on SA's taxi-driver slots (STAT_TAXIDRIVER:
    220, 261, 262), so every ambient taxi was driven by them. They now use slots SA never spawns
    (the script-only girlfriend skins, retyped as male story characters) and the LCS taxi driver
    (taxi_d) takes the SA taxi-driver slots.
  * Every skin kept the animation group and stats of the SA slot it replaced (old man walk on gang
    members). Population skins now use the animation group of their LCS definition; story characters
    use 'man' (women keep a woman group) with STAT_STD_MISSION, like the LCS special-character slots.

Inputs: the installed package and the converted-skin batch (manifest.json + rw-assets) it came from.
Idempotent. Without --apply it only reports.
"""
import argparse
import hashlib
import json
import pathlib
import re
import shutil

STORY_DONORS = (190, 191, 192, 193, 194, 195)    # script-only girlfriend skins: never spawned by SA code
TAXI_STAT = 'STAT_TAXIDRIVER'
# Driver peds SA hardcodes for ambient vehicles that the LCS fleet spawns in traffic (CPopulation vehicle->driver
# switch at 0x6117F0): stretch -> 255, freeway -> 247/248. A story character there would drive every such vehicle.
# slot -> population skin shown there instead.
AMBIENT_DRIVER_ALIAS = {255: 'b_man1', 247: 'criminal01', 248: 'criminal02'}
KEEP_TYPES = {'COP', 'MEDIC', 'FIREMAN'}          # SA's own emergency-service behaviour stays
FEMALE_ANIMS = {'woman', 'busywoman', 'sexywoman'}
SA_ANIMS = {'man', 'shuffle', 'oldman', 'gang1', 'gang2', 'oldfatman', 'fatman', 'jogger', 'drunkman', 'blindman', 'swat',
            'woman', 'shopping', 'busywoman', 'sexywoman', 'pro', 'oldwoman', 'fatwoman', 'jogwoman', 'oldfatwoman', 'skate'}


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def lcs_anim_groups(package):
    """LCS ped id -> animation group name, from the original DEFAULT.IDE."""
    result = {}
    text = (package / 'metadata/lcs-source/DATA/DEFAULT.IDE.bin').read_bytes().decode('latin1')
    for line in text.replace('\r', '').split('\n'):
        row = [f.strip() for f in line.split('#')[0].split(',')]
        if len(row) >= 6 and row[0].isdigit() and row[5] in SA_ANIMS:
            result[int(row[0])] = row[5]
    return result


class Ide:
    def __init__(self, path):
        self.path = path
        self.lines = path.read_bytes().decode('latin1').split('\r\n')
        self.rows = {}
        for index, line in enumerate(self.lines):
            text = line.split('#')[0].strip()
            if re.match(r'^\d+,', text):
                self.rows[int(text.split(',')[0])] = index

    def get(self, slot, index):
        return self.lines[self.rows[slot]].split(',')[index].strip()

    def set(self, slot, index, value):
        n = self.rows[slot]
        parts = self.lines[n].split(',')
        original = parts[index]
        lead = original[:len(original) - len(original.lstrip())] or ' '
        trail = original[len(original.rstrip()):]
        parts[index] = lead + value + trail
        self.lines[n] = ','.join(parts)

    def write(self):
        self.path.write_bytes('\r\n'.join(self.lines).encode('latin1'))


def revise(package, source, apply):
    meta_path = package / 'metadata/gameplay-peds.json'
    meta = json.loads(meta_path.read_text(encoding='utf-8'))
    manifest = {r['asset']: r for r in json.loads((source / 'manifest.json').read_text())['converted']}
    bindings = meta['bindings']
    ide = Ide(package / 'data/peds.ide')
    anims = lcs_anim_groups(package)
    stock = Ide(package.parent.parent / 'data/peds.ide')      # the untouched SA file of the game directory
    log = []
    by_slot = {b['nativeModelId']: b for b in bindings}
    taxi_slots = sorted(s for s in ide.rows if ide.get(s, 4) == TAXI_STAT)
    taxi = next(b for b in bindings if b['originalName'] == 'taxi_d' and b['canonicalMissionBinding'])
    male03 = next(b for b in bindings if b['originalName'] == 'male03' and b['canonicalMissionBinding'])

    def retarget(binding, slot, asset_row, canonical):
        """Make `binding` the skin `asset_row` on SA slot `slot` (keeps the binding's identity keys)."""
        fresh = dict(asset_row)
        fresh.update(nativeModelId=slot, nativeName=ide.get(slot, 1).lower(), nativeDictionary=ide.get(slot, 2).lower(),
                     nativeOriginalPedType=ide.get(slot, 3), nativePedType=ide.get(slot, 3), canonicalMissionBinding=canonical,
                     missionIntegration=binding['missionIntegration'] if canonical == binding['canonicalMissionBinding']
                     else ('pending: CREATE_CHAR/model IDs and LOAD_SPECIAL_CHARACTER by original label' if canonical
                           else 'ambient alias only; use canonical binding for campaign'),
                     runtimeVerified=False, installed=False)
        return fresh

    canonical_taxi_slot = next(s for s in taxi_slots if ide.get(s, 1).lower() == 'bmocd')
    taxi_asset = manifest[taxi['asset']]
    free = [s for s in STORY_DONORS if s not in by_slot]
    out = []
    for b in bindings:
        slot = b['nativeModelId']
        if b['canonicalMissionBinding'] and b['sourceKind'] == 'extraGameplay' and (slot in taxi_slots or slot in AMBIENT_DRIVER_ALIAS):
            # Story character on a taxi-driver slot: move it to a never-spawned slot, retyped as a male story ped.
            if not free:
                raise ValueError('No free story donor left for ' + b['originalName'])
            new_slot = free.pop(0)
            ide_type_before = ide.get(new_slot, 3)
            fresh = retarget(b, new_slot, manifest[b['asset']], True)
            fresh['nativeOriginalPedType'], fresh['nativePedType'] = ide_type_before, 'CIVMALE'
            for index, value in ((3, 'CIVMALE'), (4, 'STAT_STD_MISSION'), (5, 'man'), (11, 'PED_TYPE_GEN'),
                                 (12, 'VOICE_GEN_NOVOICE'), (13, 'VOICE_GEN_NOVOICE')):
                ide.set(new_slot, index, value)
            log.append(f"{b['originalName']}: slot {slot} ({b['nativeName']}) -> {new_slot} ({fresh['nativeName']})")
            out.append(fresh)
            if slot in AMBIENT_DRIVER_ALIAS:
                shown = next(x for x in bindings if x['canonicalMissionBinding'] and x['originalName'] == AMBIENT_DRIVER_ALIAS[slot])
                out.append(retarget(b, slot, manifest[shown['asset']], False))
                log.append(f'slot {slot} ({b["nativeName"]}) -> {shown["originalName"]} alias')
            elif slot != canonical_taxi_slot:
                out.append(retarget(b, slot, taxi_asset, False))
                log.append(f'slot {slot} ({b["nativeName"]}) -> taxi_d alias')
        elif b is taxi:
            if slot != canonical_taxi_slot:
                out.append(retarget(b, canonical_taxi_slot, taxi_asset, True))
                out.append(retarget(b, slot, manifest[male03['asset']], False))
                log.append(f"taxi_d: slot {slot} ({b['nativeName']}) -> {canonical_taxi_slot} (bmocd); slot {slot} becomes an alias of male03")
            else:
                out.append(b)
        elif slot in taxi_slots and slot != canonical_taxi_slot and not b['canonicalMissionBinding']:
            if b['asset'] != taxi['asset']:
                log.append(f'slot {slot} ({b["nativeName"]}) -> taxi_d alias (was {b["originalName"]})')
            out.append(retarget(b, slot, taxi_asset, False))
        else:
            out.append(b)
    bindings = out
    # 3) Animation group and stats follow the skin, not the SA slot it replaced.
    changed_anim = []
    for b in bindings:
        slot = b['nativeModelId']
        if ide.get(slot, 3) in KEEP_TYPES or b['nativeName'] in ('lapd1', 'swat'):
            continue
        before = ide.get(slot, 5)
        if before == 'pro':                          # SA prostitutes keep their solicitation group
            continue
        if b['sourceKind'] == 'extraGameplay':
            female = b['nativePedType'] in ('CIVFEMALE', 'PROSTITUTE') or before in FEMALE_ANIMS or b['originalName'].lower() in ('campwom', 'kas_01', 'mar_01', 'wkas_01', 'zzzzz')
            wanted = before if female and before in FEMALE_ANIMS else ('woman' if female else 'man')
            if ide.get(slot, 4) != 'STAT_STD_MISSION':
                ide.set(slot, 4, 'STAT_STD_MISSION')
        else:
            wanted = anims.get(b['originalModelId'], before)
            if ide.get(slot, 4) != stock.get(slot, 4):          # slot of a former story character: SA stats again
                ide.set(slot, 4, stock.get(slot, 4))
        if wanted != before and (ide.get(slot, 4) != TAXI_STAT or b['sourceKind'] != 'population'):
            ide.set(slot, 5, wanted)
            changed_anim.append(f"{slot} {b['nativeName']}: {before} -> {wanted} ({b['originalName']})")
    log.extend(['anim ' + line for line in changed_anim])
    if not apply:
        return log
    # ---- write everything -------------------------------------------------------------------------------------
    ide.write()
    for b in bindings:
        for ext, key, name in (('.dff', 'dffSHA256', b['nativeName']), ('.txd', 'txdSHA256', b['nativeDictionary'])):
            origin = source / 'rw-assets' / (b['asset'] + ext)
            if sha(origin) != b[key]:
                raise ValueError('Converted media changed after audit: ' + b['asset'])
            target = package / 'models/lcs/peds' / (name + ext)
            if not target.exists() or sha(target) != b[key]:
                shutil.copyfile(origin, target)
    peds = package / 'data/lcs/peds'
    (peds / 'index.tsv').write_text(''.join(f"{b['nativeModelId']}\t{b['nativeName']}\t{b['asset']}\n" for b in bindings), encoding='ascii')
    row = lambda b: f"{b['originalModelId']}\t{b['nativeModelId']}\t{b['originalName']}\t{b['nativeName']}\t{b['asset']}\n"
    (peds / 'model-bindings.tsv').write_text(''.join(row(b) for b in bindings), encoding='ascii')
    (peds / 'canonical-model-bindings.tsv').write_text(''.join(row(b) for b in bindings if b['canonicalMissionBinding']), encoding='ascii')
    # native-equivalents: LCS ped id, SA id, original name, SA model name, 'ped'
    path = package / 'data/lcs/native-equivalents.tsv'
    rows = [line.split('\t') for line in path.read_text().splitlines()]
    canon = {b['originalModelId']: b for b in bindings if b['canonicalMissionBinding'] and b['originalModelId'] is not None}
    for r in rows:
        if r[4] == 'ped' and r[0].isdigit() and int(r[0]) in canon:
            b = canon[int(r[0])]
            r[1], r[3] = str(b['nativeModelId']), b['nativeName']
    path.write_text(''.join('\t'.join(r) + '\n' for r in rows))
    md_path = package / 'metadata/gameplay-peds.md'
    md = md_path.read_text(encoding='utf-8').split(chr(10))
    start = next(i for i, l in enumerate(md) if l.startswith('|---')) + 1
    end = next(i for i in range(start, len(md)) if not md[i].startswith('|'))
    table = ['| ' + ' | '.join(map(str, (r['sourceKind'], r['originalModelId'], r['originalName'], r['nativeModelId'], r['nativeName'],
                                          r['nativeDictionary'], r['nativePedType'], r['canonicalMissionBinding']))) + ' |' for r in bindings]
    note = ['', 'Revisao (tools/revise-gameplay-peds.py): personagens de historia nao ficam mais nos slots de taxista do SA',
            '(220/261/262, STAT_TAXIDRIVER); o taxi_d do LCS ocupa esses slots. Slots 190-192 (skins de namorada, so usadas por',
            'script no SA) viraram personagens masculinos de historia. Grupo de animacao e stats de cada slot seguem a skin LCS',
            '(populacao) ou man/STAT_STD_MISSION (historia). data/peds.ide deixou de ser copia byte-exata do SA nessas linhas.']
    md = md[:start] + table + md[end:]
    if not any('Revisao (tools/revise-gameplay-peds.py)' in l for l in md):
        md[-1:-1] = note
    md_path.write_text(chr(10).join(md), encoding='utf-8')
    meta['bindings'] = bindings
    meta['uniqueNativeReplacements'] = len(bindings)
    meta['canonicalMissionBindings'] = sum(b['canonicalMissionBinding'] for b in bindings)
    meta['revision'] = dict(takenFrom='tools/revise-gameplay-peds.py', taxiDriverSlots=taxi_slots, storyDonors=list(STORY_DONORS),
                            nativePedIdeAndGroupsByteExact=False, note='anim group/stats follow the LCS skin; taxi slots hold taxi_d')
    meta['nativePedIdeAndGroupsByteExact'] = False
    meta_path.write_text(json.dumps(meta, indent=2), encoding='utf-8')
    return log


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=pathlib.Path, required=True)
    parser.add_argument('--source', type=pathlib.Path, required=True)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    for line in revise(args.package.resolve(), args.source.resolve(), args.apply):
        print(line)
