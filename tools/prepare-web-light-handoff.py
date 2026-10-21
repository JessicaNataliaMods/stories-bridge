"""Build a code-first Web snapshot; no bulk assets or hydration required."""
import hashlib
import io
import json
from pathlib import Path
import zipfile

ROOT=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()

def main():
    output=ROOT/'out/LCS_WEB_V57_LEVE.zip'
    if output.exists():raise ValueError('Refusing existing published output')
    base=ROOT/'out/LCS_WEB_HANDOFF_V57_COMPLETO.zip'
    exclude={'BASELINE.json','BASELINE.sha256','handoff/baseline-editable.zip',
        'dependency-index-v57.json','dependency-index-v57.sha256','hydrate_dependencies.py',
        'PROMPT_COM_DEPENDENCIAS.txt','lcs-sa/reports/web-dependencies-v57.json'}
    with zipfile.ZipFile(base) as source:
        old=json.loads(source.read('BASELINE.json'))
        content={name:source.read(name) for name in source.namelist() if name not in exclude}
    tools=json.loads((ROOT/'reports/web-dependencies-v57.json').read_text(encoding='utf-8'))['offlineLinuxWheels']
    with zipfile.ZipFile(ROOT/'out/web-dependencies-v57/LCS_WEB_DEPENDENCIES_V57_PART01.zip') as source:
        for row in tools:
            name='offline-python/'+row['filename'];data=source.read(name)
            if sha(data)!=row['sha256']:raise ValueError('Wheel differs from official pinned hash')
            content[name]=data
    content['offline-python/SOURCES.json']=(json.dumps(tools,indent=2)+'\n').encode()
    prompt='''Use somente LCS_WEB_V57_LEVE.zip. Este é o perfil atual do handoff; instruções antigas sobre enviar sete ZIPs ou hidratar assets completos estão substituídas. Não peça, leia, extraia ou restaure os seis PART ZIPs para começar. Se já houver alterações de outra base, preserve o último retorno e não as sobrescreva nem misture baseIds.

Objetivo: portar os comandos ausentes do MAIN.SCM original para o bridge SA, com código real do adaptador, backend, ownership/estado/hooks, ligação no ASI e testes offline. Base instalada v57:973 opcodes,877 cláusulas,96 ausentes;86 testes Windows históricos passaram. Esses números não provam campanha completa ou execução Linux dos novos patches.

Extraia apenas este ZIP. Leia LEIA_PRIMEIRO.md e PLANO_EXECUCAO.md; rode python -B handoff.py --verify e --probe. Não leia toda a história de AGENTS nem todos os fontes; consulte só o módulo do lote. Se faltarem Capstone/CMake/Ninja, use os wheels Linux x86_64/aarch64 de offline-python/ com pip --no-index, dentro do sandbox Web. Não há GCC/Clang ou SDK Windows neste pacote: confirme o compilador real, sem inventar resultado. Não execute os binários originais do jogo, Wine, ASI, instaladores ou dispositivos de áudio.

Primeiro lote: CANCEL_REMOTE_MODE1512, documentado em lcs-sa/docs/cancel-remote-port-progress.md. Ele não exige mapas, texturas ou imagens IMG. Prove o contrato no ELF e no PE incluídos; preserve DD after-remote, limpe DF fade e o ponteiro remoto, não exploda o RC e não use0409 vazio. Antes de compilar, rode python -B lcs-sa/tests/original_remote_contracts.py evidence/native-sa/gta_sa.exe. Faça primeiro python -B run_portable_tests.py --smoke --jobs 1. No fim da família, execute a regressão completa disponível com python -B run_portable_tests.py --jobs 1 e os testes novos explicitamente. Não repita a regressão inteira a cada edição. Use execução sequencial e guarde stdout/stderr em logs pequenos.

Implemente o backend Windows também, incluindo perfis de ABI e testes Win32; registre honestamente os testes que só poderão ser executados localmente. Não troque SEH/ABI por macros vazias para compilar no Linux. Não use NOPs, sucesso fictício, valores/predicados inventados ou progressão forçada; não enfraqueça testes/referências. Preserve missões, cutscenes, mapa, player/NPCs aprovados, exports e tarefas de outros mods.

Ao concluir um lote, escreva .web-work/RETURN_REPORT.md e exporte python -B handoff.py --export R01 --report .web-work/RETURN_REPORT.md. Continue outras famílias independentes sem aguardar auditoria entre lotes; retornos são cumulativos, até512 arquivos. Se uma família realmente precisar de um asset, documente somente os nomes, caminhos e consumidores exatos em lcs-sa/docs/WEB_INPUTS_REQUIRED.json; continue outra família. O Codex local pode extrair um complemento pequeno por família. Não solicite nem restaure o corpus completo. Para assets gerados, entregue receita reproduzível, código e hashes; a auditoria local reproduzirá o resultado.

Entregue alterações efetivas, testes/logs reais e o último web-return-Rxx.zip. Não termine só com plano ou pseudocódigo. Não instale, publique ou declare campanha/mods verificados. A auditoria local incluirá revisão, compilação Win32 e verificações nativas antes de qualquer instalação.
'''
    readme='''# Entrada leve para portar comandos

Envie somente LCS_WEB_V57_LEVE.zip e cole PROMPT_LEVE.txt. Nenhum PART ZIP é necessário para iniciar CANCEL_REMOTE_MODE1512 e os testes portáveis selecionados. O pacote contém todos os fontes/testes/referências do snapshot, MAIN.SCM, ELF e PE somente para inspeção e os wheels Linux de Capstone/CMake/Ninja. Não contém mapa, texturas, grandes IMG ou cópias de stages. Os executáveis originais nunca devem ser lançados.

1. Verifique com `python -B handoff.py --verify` e `--probe`.
2. Se ferramentas faltarem: `python -m pip install --no-index --find-links offline-python --target .web-work/python-deps capstone==5.0.7 cmake==3.31.6 ninja==1.11.1.4`. Inclua esse diretório em PYTHONPATH e seu subdiretório bin em PATH. Instale apenas dentro do sandbox Web. GCC/Clang precisa existir no ambiente.
3. Execute o checker original_remote_contracts.py com o PE incluído, depois `python -B run_portable_tests.py --smoke --jobs 1`. São3 testes CTest e10 checkers Python; a compilação inicial ainda pode levar tempo, mas não restaura assets grandes. No Linux, a configuração Debug desativa símbolos e otimização para reduzir o custo, preservando asserções.
4. Porte a família e execute seus testes novos. Ao concluí-la, rode o conjunto completo de35 testes CTest e10 checkers Python sem --smoke, com --jobs 1. Logs históricos Windows não validam patches novos.
5. Exporte o retorno cumulativo e continue outra família. Para recursos novos, peça apenas um complemento nominal pequeno; não use o corpus de8,92GB como pré-requisito geral. Não imprima dumps binários ou relatórios enormes no chat.

Siga PLANO_EXECUCAO.md e o preflight do comando. Histórias de staging/hidratação antigas nos docs descrevem outras fases; este perfil tem prioridade para a sessão Web. Instalação, SDK/ABI/SEH/WinMM e testes nativos Windows serão auditados localmente.
'''
    plan='''# Execução com custo controlado

Primeiro prove o ambiente e o contrato original; depois compile o smoke sequencial, implemente1512 com todos os efeitos reais e testes novos, e execute a regressão portável no fechamento da família. Leia lcs-sa/docs/cancel-remote-port-progress.md. Preserve a distinção entre veículo físico e remoto, geração/foco, referência do ponteiro, ordem/falhas, DD e DF e a câmera.

Não instale ou execute o jogo. Não rode hidratadores ou empacotadores de corpus. Nenhuma imagem de mapa é necessária para1512. Estão incluídos fontes do backend, adaptadores, ASI, testes e os dois executáveis somente como dados. Escreva também os testes Win32, declarando-os pendentes quando não executados.

Após exportar R01, continue famílias que possam ser provadas com os dados incluídos. Registre dependências reais por nome/caminho/consumidor em WEB_INPUTS_REQUIRED.json e avance outra família independente. Não descarte as96 pendências nem afirme que todas são livres de assets. Para conversão de recurso, entregue código, receita e hashes; o insumo específico será fornecido em pacote pequeno quando necessário.

Conserve logs pequenos, estado curto e retorno cumulativo com baseId. A auditoria local continua obrigatória. Jamais transforme ausência de dados, falha parcial ou teste não executado em sucesso.
'''
    for name,value in {'PROMPT_LEVE.txt':prompt,'PROMPT_INICIAL.txt':prompt,'LEIA_PRIMEIRO.md':readme,
        'PLANO_EXECUCAO.md':plan,'ESCOPO_DO_PORTE.md':plan,'ESTADO_E_LIMITES.md':readme,
        'lcs-sa/AGENTS.md':readme}.items():content[name]=value.encode('utf-8')
    runner=content['run_portable_tests.py'].decode('utf-8')
    head,tail=runner.split('def main():',1)
    tail=tail.replace('SUITE','suite').replace("default=2)","default=1)")
    tail=tail.replace('    args = parser.parse_args()',"    parser.add_argument('--smoke', action='store_true', help='Run3 initial CTest checks instead of the full35')\n    args = parser.parse_args()\n    suite = {name: SUITE[name] for name in ('core','native_translation','objective_receipt_lifecycle')} if args.smoke else SUITE")
    tail=tail.replace("'-DCMAKE_BUILD_TYPE=Debug', '-DASSET_VALIDATION_PYTHON=' + sys.executable,","'-DCMAKE_BUILD_TYPE=Debug', *(['-DCMAKE_CXX_FLAGS_DEBUG=-O0 -g0'] if os.name != 'nt' else []), '-DASSET_VALIDATION_PYTHON=' + sys.executable,")
    tail=tail.replace("set(suite.values()) | {'lcs-campaign-audit'}","set(suite.values()) | (set() if args.smoke else {'lcs-campaign-audit'})")
    runner=head+'def main():'+tail;compile(runner,'run_portable_tests.py','exec')
    content['run_portable_tests.py']=runner.encode()
    flags={row['path']:row['editable'] for row in old['files']}
    editable=lambda name:flags.get(name,False)
    original=io.BytesIO()
    with zipfile.ZipFile(original,'w',zipfile.ZIP_DEFLATED) as z:
        for name,data in sorted(content.items()):
            if editable(name):z.writestr(name,data)
    content['handoff/baseline-editable.zip']=original.getvalue()
    manifest={k:v for k,v in old.items() if k!='files'}
    manifest.update(profile='code_first_light',bulkAssetsIncluded=False,firstBatchRequiresBulkAssets=False,
        files=[dict(path=name,bytes=len(data),sha256=sha(data),editable=editable(name)) for name,data in sorted(content.items())])
    raw=(json.dumps(manifest,indent=2)+'\n').encode();base_id=sha(raw)
    content['BASELINE.json']=raw;content['BASELINE.sha256']=(base_id+'  BASELINE.json\n').encode()
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for name,data in sorted(content.items()):z.writestr(name,data)
    with zipfile.ZipFile(output) as z:
        if z.testzip() is not None:raise ValueError('ZIP integrity failure')
        for row in manifest['files']:
            if sha(z.read(row['path']))!=row['sha256']:raise ValueError('ZIP source hash differs')
    if output.stat().st_size>100_000_000:raise ValueError('Code-first ZIP exceeds100MB budget')
    result=dict(path=str(output),bytes=output.stat().st_size,uncompressedBytes=sum(map(len,content.values())),
        files=len(content),baseId=base_id,sha256=sha(output.read_bytes()),profile='code_first_light',
        bulkAssetsIncluded=False,gameExecuted=False,installed=False)
    catalog_path=ROOT/'reports/web-handoff-v57-bases.json';catalog=json.loads(catalog_path.read_text(encoding='utf-8'))
    catalog.append(result);catalog_path.write_text(json.dumps(catalog,indent=2)+'\n',encoding='utf-8')
    for name in ('web-handoff-v57.json','web-light-v57.json'):(ROOT/'reports'/name).write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    (ROOT/'handoff/PROMPT_LEVE.txt').write_text(prompt,encoding='utf-8')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
