"""Update only navigation/population over the map accepted by the user."""
import argparse
import hashlib
import json
import pathlib
import shutil

from traffic_profile import mount_navigation


def prepare(base, navigation, output):
    if output.exists():
        raise ValueError('Output exists; choose a new directory')
    manifest = json.loads((base / 'metadata/package-files.json').read_text())
    for row in manifest:
        path = base / row['relative']
        with path.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        if path.stat().st_size != row['bytes'] or digest.lower() != row['sha256'].lower():
            raise ValueError('Accepted map baseline changed: ' + row['relative'])
    shutil.copytree(base, output, ignore=shutil.ignore_patterns('LcsSaBridge.log'))
    traffic = mount_navigation(output, navigation)
    metadata_path = output / 'metadata/map-preview.json'
    metadata = json.loads(metadata_path.read_text())
    metadata.update(version=15, navigation=traffic, mapAcceptedByUser=True,
                    acceptedMapVersion=14, mapAssetBytesChanged=False)
    metadata_path.write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    (output / 'LEIA.txt').write_text(
        'Stories Bridge v15 - mapa aprovado e paths PS2\n\n'
        'Abra uma NOVA PARTIDA para testar pedestres e trafego nativo do SA.\n'
        'A campanha permanece desativada neste perfil.\n'
        'Os assets/colisao/iluminacao/culling do mapa v14 foram preservados.\n'
        'O grafo PS2 completo substitui o grafo vazio: 64 areas nativas SA.\n'
        'Densidade de carros e pedestres: 1.0, usando os modelos originais do SA.\n'
        'Conexoes, travessias, semaforos, direcoes e contagens de faixas preservados.\n'
        'O SA usa faixas de 5.4 m; o LCS usa 5.0 m. Verifique alinhamento em ruas estreitas.\n'
        'Validacao de dados concluida; comportamento do trafego requer teste no jogo.\n'
        'Este pacote privado contem dados dos seus jogos. Distribua somente o conversor.\n'
        'Nenhum jogo foi aberto pelo agente.\n', encoding='utf-8')
    # Guarantee that this update never reconverts or replaces accepted geometry,
    # textures, collisions, lighting policies, native model banks or the ASI.
    allowed = {'models/lcs/paths.img', 'data/script/main.scm', 'metadata/map-preview.json',
               'metadata/package-validation.json', 'metadata/package-files.json', 'LEIA.txt'}
    for row in manifest:
        if row['relative'] in allowed:
            continue
        with (output / row['relative']).open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest().lower() != row['sha256'].lower():
                raise ValueError('Accepted map resource changed unexpectedly')
    print(json.dumps(dict(prepared=True, output=str(output), traffic=traffic, mapAssetsPreserved=True), indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base', required=True, type=pathlib.Path)
    p.add_argument('--navigation', required=True, type=pathlib.Path)
    p.add_argument('--output', required=True, type=pathlib.Path)
    args = p.parse_args()
    prepare(args.base, args.navigation, args.output)
