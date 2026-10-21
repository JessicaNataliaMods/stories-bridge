"""Mount Toni over the accepted map/traffic package, without player.img."""
import argparse
import hashlib
import json
import pathlib
import shutil
import struct


def append_skins(original, assets, output):
    with original.open('rb') as stream:
        magic, count = struct.unpack('<4sI', stream.read(8))
        if magic != b'VER2':
            raise ValueError('Expected native world IMG v2')
        entries = [struct.unpack('<IHH24s', stream.read(32)) for _ in range(count)]
        files = sorted(assets.glob('*'))
        if len(files) != 32 or any(f.suffix not in ('.dff', '.txd') for f in files):
            raise ValueError('Expected exactly 16 original outfits')
        names = {row[3].split(b'\0')[0].decode('ascii').lower() for row in entries}
        if names & {f.name.lower() for f in files}:
            raise ValueError('Toni namespace collision')
        new_count = count + len(files)
        position = (8 + new_count * 32 + 2047) // 2048
        headers = []
        for sector, size, compressed, name in entries:
            length = compressed or size
            headers.append((position, length, 0, name))
            position += length
        for file in files:
            length = (file.stat().st_size + 2047) // 2048
            headers.append((position, length, 0, file.name.encode('ascii').ljust(24, b'\0')))
            position += length
        with output.open('wb') as target:
            target.write(struct.pack('<4sI', b'VER2', new_count))
            for row in headers:
                target.write(struct.pack('<IHH24s', *row))
            target.write(b'\0' * (headers[0][0] * 2048 - target.tell()))
            for old, new in zip(entries, headers):
                stream.seek(old[0] * 2048)
                data = stream.read((old[2] or old[1]) * 2048)
                if len(data) != new[1] * 2048:
                    raise ValueError('Truncated accepted map resource')
                target.write(data)
            for file, row in zip(files, headers[count:]):
                data = file.read_bytes()
                target.write(data)
                target.write(b'\0' * (row[1] * 2048 - len(data)))


def prepare(base, player, output, bridge):
    if output.exists():
        raise ValueError('Output exists')
    for row in json.loads((base / 'metadata/package-files.json').read_text()):
        path = base / row['relative']
        if hashlib.sha256(path.read_bytes()).hexdigest() != row['sha256'].lower():
            raise ValueError('Accepted base changed: ' + row['relative'])
    report = json.loads((player / 'manifest.json').read_text())
    if len(report['outfits']) != 16 or report['playerImgModified']:
        raise ValueError('Original Toni inventory incomplete')
    audit = json.loads((player / 'player-validation.json').read_text())
    if not audit['valid'] or len(audit['outfits']) != 16:
        raise ValueError('Original Toni independent audit incomplete')
    shutil.copytree(base, output, ignore=shutil.ignore_patterns('LcsSaBridge.log'))
    append_skins(base / 'models/lcs/world.img', player / 'rw-assets', output / 'models/lcs/world.img')
    target = output / 'data/lcs/player'
    target.mkdir()
    shutil.copyfile(player / 'index.tsv', target / 'index.tsv')
    shutil.copyfile(player / 'manifest.json', output / 'metadata/player-skins.json')
    shutil.copyfile(player / 'player-validation.json', output / 'metadata/player-validation.json')
    shutil.copyfile(bridge, output / 'LcsSaBridge.asi')
    ini = output / 'LcsSaBridge.ini'
    raw_ini = ini.read_bytes()
    ini_text = raw_ini.decode('utf-16' if raw_ini.startswith(b'\xff\xfe') else 'utf-8-sig')
    ini.write_text(ini_text + 'PlayerSkins=1\nPlayerOutfit=plr3\n', encoding='utf-16')
    metadata = output / 'metadata/map-preview.json'
    data = json.loads(metadata.read_text())
    data.update(version=16, playerSkins=dict(source=str(player.resolve()), outfits=16,
                initial='plr3', firstMission='plr', slots=[297,298], playerImgModified=False,
                nativeImgSlotsAdded=0), acceptedMapVersion=14, mapAssetBytesChanged=False)
    metadata.write_text(json.dumps(data, indent=2), encoding='utf-8')
    docs = pathlib.Path(__file__).resolve().parents[1] / 'docs/player-outfits.md'
    shutil.copyfile(docs, target / 'player-outfits.md')
    (output / 'LEIA.txt').write_text(
        'Stories Bridge v16 - Toni PS2, 16 roupas de gameplay\n\n'
        'Inicie uma NOVA PARTIDA para testar o Toni com a roupa de chegada (plr3).\n'
        'Mapa e trafego v15 preservados. Este perfil continua WorldPreview.\n'
        'player.img permanece original. Toni usa DFF/TXD completos em world.img.\n'
        'Para outra roupa, feche o jogo e altere PlayerOutfit no LcsSaBridge.ini.\n'
        'Valores: plr, plr2, plr3, ... plr16.\n'
        'A integracao da campanha processa UNDRESS_CHAR/DRESS_CHAR e IS_PLAYER_WEARING.\n'
        'Veja data/lcs/player/player-outfits.md para retomar o modo Campaign.\n'
        'Campanha e cutscenes nao sao ativadas por esta atualizacao de player.\n'
        'Nenhum jogo foi aberto pelo agente. A animacao precisa de teste no jogo.\n'
        'Este pacote privado contem dados dos seus jogos; distribua somente o conversor.\n', encoding='utf-8')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('base', 'player', 'output', 'bridge'):
        p.add_argument('--' + name, required=True, type=pathlib.Path)
    args = p.parse_args()
    prepare(args.base, args.player, args.output, args.bridge)
