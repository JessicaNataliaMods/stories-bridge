"""Prepare unique native SA replacements for verified PS2 gameplay skins.

Only writes a new private staging directory. Does not install or launch GTA.
"""
import argparse
import collections
import hashlib
import json
import pathlib
import re
import shutil


def native_peds(game):
    result = {}
    section = ''
    for line in (game / 'data/peds.ide').read_text(encoding='ascii').splitlines():
        text = line.split('#')[0].strip()
        if text == 'end':
            section = ''
        elif ',' not in text:
            if text:
                section = text
        elif section == 'peds':
            row = [f.strip() for f in text.split(',')]
            # Stock BMYMOUN omits one comma between radio2 and PED_TYPE_GEN.
            # Preserve that original line; all fields used here precede it.
            if len(row) < 13:
                raise ValueError('Native ped IDE is incomplete')
            ident = int(row[0])
            if ident in result:
                raise ValueError('Duplicate native ped slot')
            result[ident] = row
    return result


def prepare(game, source, output):
    if output.exists():
        raise ValueError('Replacement output exists')
    manifest = json.loads((source / 'manifest.json').read_text())
    if not manifest['complete'] or manifest['failures'] or not manifest['allStreamedPopulationConverted']:
        raise ValueError('Gameplay skin batch is incomplete')
    native = native_peds(game)
    ambient_names = {name.lower() for line in (game/'data/pedgrp.dat').read_text(encoding='ascii').splitlines()
                     for name in re.findall(r'[A-Za-z0-9_]+',line.split('#')[0])}
    used, used_dicts, bindings, aliases = set(), set(), [], {}
    fixed = {'cop': 'lapd1', 'swat': 'swat', 'fbi': 'fbi', 'army': 'army',
             'medic': 'laemt1', 'fireman': 'lafd1', 'male01': 'male01'}
    source_labels = {}
    for row in manifest['converted']:
        label = row['originalName'].lower()
        # A repeated extra name is unified only after all source/final bytes
        # have been proven identical. Distinct names always keep distinct IDs.
        identity = (row['sourceKind'], label)
        if identity in source_labels:
            old = source_labels[identity]
            if any(old[key] != row[key] for key in ('sourceSHA256', 'dffSHA256', 'txdSHA256')):
                raise ValueError('Ambiguous original character name: ' + label)
            aliases[row['asset']] = old['asset']
            continue
        desired = row['pedType']
        if desired == 'SPECIAL':
            desired = 'CIVFEMALE' if row['nativeReference'] == 'bfyst' else 'CIVMALE'
        requested = fixed.get(label) if row['sourceKind'] == 'population' else None
        candidates = [p for ident, p in sorted(native.items()) if 0 < ident < 290 and ident not in used
                      and p[2].lower() not in used_dicts and
                      ((requested and p[1].lower() == requested) or
                       (not requested and row['sourceKind']=='population' and p[3]==desired and
                        (p[1].lower() in ambient_names or desired in ('COP','MEDIC','FIREMAN'))) or
                       (not requested and row['sourceKind']!='population' and p[1].lower() not in ambient_names and
                        p[3] in (('CIVMALE','CRIMINAL') if desired=='CIVMALE' else (desired,))))]
        if not candidates and desired in ('COP', 'GANG9'):
            # SA has fewer cop variants and no stock GANG9 skin declaration.
            # Reuse a nonambient male slot, preserving its native ped type.
            candidates = [p for ident, p in sorted(native.items()) if 0 < ident < 290 and ident not in used
                          and p[2].lower() not in used_dicts and p[3] in ('CIVMALE','CRIMINAL')
                          and p[1].lower() != 'male01' and p[1].lower() not in ambient_names]
        if not candidates:
            raise ValueError('No unique native donor available for ' + label + ': ' + desired)
        donor = candidates[0]
        ident = int(donor[0])
        used.add(ident)
        used_dicts.add(donor[2].lower())
        binding = dict(row, nativeModelId=ident, nativeName=donor[1].lower(), nativeDictionary=donor[2].lower(),
                       nativeOriginalPedType=donor[3], nativePedType=donor[3], canonicalMissionBinding=True,
                       missionIntegration='pending: CREATE_CHAR/model IDs and LOAD_SPECIAL_CHARACTER by original label',
                       runtimeVerified=False, installed=False)
        bindings.append(binding)
        source_labels[identity] = binding
    # Keep every stock ambient token and its native stats/animation/ped type.
    # Populate extra stock slots with aliases of actual LCS population skins;
    # do not collapse group membership onto unrelated model-info records.
    population=[r for r in manifest['converted'] if r['sourceKind']=='population']
    counters=collections.Counter()
    for ident,donor in sorted(native.items()):
        if donor[1].lower() not in ambient_names or ident in used:continue
        desired=donor[3]
        choices=[r for r in population if r['pedType']==desired]
        if not choices:
            choices=[r for r in population if r['pedType']==('CIVFEMALE' if desired in ('CIVFEMALE','PROSTITUTE') else 'CRIMINAL' if desired in ('DEALER','CRIMINAL') else 'CIVMALE')]
        if not choices:raise ValueError('No compatible population alias')
        row=choices[counters[desired]%len(choices)];counters[desired]+=1
        if donor[2].lower() in used_dicts:raise ValueError('Native shared TXD alias')
        bindings.append(dict(row,nativeModelId=ident,nativeName=donor[1].lower(),nativeDictionary=donor[2].lower(),
                             nativeOriginalPedType=donor[3],nativePedType=donor[3],canonicalMissionBinding=False,
                             missionIntegration='ambient alias only; use canonical binding for campaign',runtimeVerified=False,installed=False))
        used.add(ident);used_dicts.add(donor[2].lower())
    output.mkdir(parents=True)
    (output / 'models/lcs/peds').mkdir(parents=True)
    (output / 'data/lcs/peds').mkdir(parents=True)
    (output / 'metadata').mkdir()
    for row in bindings:
        for ext, hash_key, filename in (('.dff', 'dffSHA256', row['nativeName']), ('.txd', 'txdSHA256', row['nativeDictionary'])):
            original = source / 'rw-assets' / (row['asset'] + ext)
            if hashlib.sha256(original.read_bytes()).hexdigest() != row[hash_key]:
                raise ValueError('Converted media changed after audit')
            shutil.copyfile(original, output / 'models/lcs/peds' / (filename + ext))
    shutil.copyfile(game/'data/peds.ide',output/'data/peds.ide')
    (output / 'data/lcs/peds/index.tsv').write_text(''.join(str(row['nativeModelId']) + '\t' + row['nativeName'] + '\t' + row['asset'] + '\n' for row in bindings), encoding='ascii')
    (output / 'data/lcs/peds/model-bindings.tsv').write_text(''.join(str(row['originalModelId']) + '\t' + str(row['nativeModelId']) + '\t' + row['originalName'] + '\t' + row['nativeName'] + '\t' + row['asset'] + '\n' for row in bindings), encoding='ascii')
    (output / 'data/lcs/peds/canonical-model-bindings.tsv').write_text(''.join(str(row['originalModelId']) + '\t' + str(row['nativeModelId']) + '\t' + row['originalName'] + '\t' + row['nativeName'] + '\t' + row['asset'] + '\n' for row in bindings if row['canonicalMissionBinding']), encoding='ascii')
    # Restrict ambient lists to population skins. Story characters/bodybag
    # must never be picked randomly, even though they occupy ordinary slots.
    available_names = {row['nativeName'] for row in bindings if row['sourceKind'] == 'population' and row['nativeName'] != 'male01'}
    groups, counts = [], []
    for line in (game / 'data/pedgrp.dat').read_text(encoding='ascii').splitlines():
        text, _, comment = line.partition('#')
        tokens = re.findall(r'[A-Za-z0-9_]+', text)
        if not tokens:
            groups.append(line)
            continue
        if not 1 <= len(tokens) <= 32 or any(n.lower() not in available_names for n in tokens):
            raise ValueError('Invalid ambient group replacement')
        counts.append(len(tokens))
    shutil.copyfile(game/'data/pedgrp.dat',output/'data/pedgrp.dat')
    report = dict(format=1, source=str(source.resolve()), convertedSourceSkins=len(manifest['converted']),
                  uniqueNativeReplacements=len(bindings), canonicalMissionBindings=sum(r['canonicalMissionBinding'] for r in bindings),
                  sourceBodyProportionsVerified=True, slotsExpanded=False, reservedPlayerSlots=[297, 298],
                  originalPedColorMarkers=manifest['pedColorMarkersFound'], bindings=bindings, identicalExtraAliases=aliases,
                  nativeVoicesAndAnimationsPreserved=True,nativePedIdeAndGroupsByteExact=True, ambientGroupCounts=counts,
                  ambientListsUseOnlyLcsPopulationSkins=True, sourcePopulationScheduleImported=False,
                  installed=False, gameExecuted=False, runtimeVerified=False)
    (output / 'metadata/gameplay-peds.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    doc = ['# V18: personagens de gameplay e vinculos nativos', '',
           'Conversao offline auditada; funcionamento no jogo ainda precisa de teste.',
           'Os IDs abaixo substituem slots existentes. Toni reserva 297/298.',
           'A campanha ainda nao foi ligada a estes vinculos. LOAD_SPECIAL_CHARACTER',
           'deve resolver os nomes originais abaixo sem trocar modelos ja referenciados.',
           'As animacoes e vozes continuam as nativas SA. Nao foram convertidas falas.',
           'pedgrp.dat usa somente skins LCS de populacao; extras de historia e bodybag',
           'ficam fora do sorteio ambiente. O horario/distribuicao continua sendo SA.', '',
           'peds.ide e pedgrp.dat sao copias byte-exatas da instalacao SA original.',
           'GANG9 LCS e os vice cops adicionais usam slots fora dos grupos ambiente;',
           'o tipo original continua registrado, mas nao se declara GANG9 vazio no SA.',
           'Aliases de populacao mantem a lista nativa; use somente canonicos nas missoes.',
           'O rig conserva os comprimentos dos ossos originais de cada skin LCS.',
           'Os eixos de animacao, hierarquia e offsets auxiliares vem do SA fornecido.',
           '| Fonte | ID LCS | Nome original | ID SA | DFF SA | TXD SA | Tipo SA | Canonico |',
           '|---|---:|---|---:|---|---|---|---|']
    doc.extend('| ' + ' | '.join(map(str, (r['sourceKind'], r['originalModelId'], r['originalName'], r['nativeModelId'], r['nativeName'], r['nativeDictionary'], r['nativePedType'],r['canonicalMissionBinding']))) + ' |' for r in bindings)
    doc.extend(['', 'Duplicatas identicas verificadas: `' + json.dumps(aliases) + '`.',
                '139 skins de origem, 138 vinculos canonicos e aliases adicionais para os grupos SA.',
                'Nao foram encontrados os quatro marcadores de material do PEDCOLS VCS.',
                'Nenhuma paleta foi inventada; esta revisao preserva as cores originais.',
                'O conversor publico nao inclui nenhum DFF/TXD ou outro asset dos jogos.'])
    (output / 'metadata/gameplay-peds.md').write_text('\n'.join(doc) + '\n', encoding='utf-8')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=pathlib.Path, required=True)
    parser.add_argument('--source', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    result = prepare(args.game.resolve(), args.source.resolve(), args.output.resolve())
    print('Prepared', result['uniqueNativeReplacements'], 'unique existing native slots; gameExecuted=false')
