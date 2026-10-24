# Fronteira público/privado

```text
stories-bridge/                 # Git: código e documentação
private-bridge/                 # Nunca criar/publicar como repositório
  originals/lcs-ps2/            # Entradas fornecidas pela pessoa usuária
  references/                  # Reverse local e demais referências
  generated/                   # Includes com dados extraídos
  metadata/                    # Associações e fingerprints privados
  fixtures/                    # Arquivos originais usados por testes
  build/                       # Build atual; fora do checkout
  workspace/lcs-sa/            # Pacotes, relatórios, dependências e histórico local
  archives/                    # Histórico anterior preservado
```

`tools/setup-private-workspace.ps1` cria links de diretório locais ignorados: `local` aponta à raiz privada; `out`, `reports`, `vendor`, `assets`, `dist` e `handoff` apontam ao workspace privado; `build` aponta ao build privado; `tests/fixtures` aponta às fixtures privadas. São referências locais, não arquivos distribuídos. A verificação do índice rejeita qualquer arquivo que resolva para fora do repositório.

Os antigos caminhos de desenvolvimento podem continuar como links de compatibilidade, mas não são a origem canônica para novas alterações. A cópia histórica privada mantém evidências e rollback; não deve ser exportada como fonte pública.

Mesmo uma extensão textual não torna um arquivo público: camadas do mapa, posições, meshes serializados, scripts transcritos e dumps permanecem privados. O build recebe `world_sector_layers.inc` pelo include path privado. Os três JSON de referência do conversor ficam em `metadata`, não em `tools` versionado.

As entradas de jogo e a instalação aprovada nunca são apagadas durante a organização. Nenhum asset é transferido ao GitHub. Alterações do destino de instalação exigem autorização explícita.

## Metadados e entrega pública

Estágios antigos podem conter caminhos de origem nos JSONs. A preparação atual
remove referências absolutas externas e mantém referências internas relativas
à raiz do pacote. Entradas externas necessárias à reconversão são argumentos
explícitos, como `--game` e `--weapon-source`, sem caminhos pessoais gravados nos
metadados. Não apagar a pasta nem distribuir um estágio local como release.
Retirar caminhos pessoais não torna dados extraídos publicáveis.
A entrega pública de fontes usa somente os arquivos do índice Git,
sem percorrer os links locais, após `tools/check-public-tree.py --staged`.

O empacotador legado `tools/package-converter.py` está bloqueado antes da leitura
de entradas e da criação da saída: ele copiava metadados privados e `hashes.inc`
para o bundle. Sua reativação exige que cada pessoa gere as tabelas necessárias
das próprias entradas, sem incluir referências privadas no pacote distribuído.

O validador padrão `tools/validate-modloader-map.py --package PACOTE` verifica
estrutura, limites de arquivos e referências locais. Ele aceita conteúdo
personalizado e atualiza o inventário dos bytes atuais. Não compara vértices,
triângulos, texturas ou hashes com a conversão original e não afirma ter
validado visualmente a geometria. Comparações com a origem são provas de
desenvolvimento opcionais (`--verify-conversion --source ORIGEM --game SA`),
assim como `--verify-source-revision` no conversor. Não são exigências do runtime.

Hashes do inventário continuam úteis para conferir cópia e rollback: depois de
editar assets, executar novamente a validação estrutural antes de instalar.
O instalador verifica as interfaces do executável e os recursos do destino,
sem exigir o hash integral da instalação original. As cutscenes não são
recusadas por divergência de hash de archives ou áudio. A aprovação offline de
startup permanece separada; ela recebe `--game` explicitamente. Essas mudanças
não reescrevem automaticamente os pacotes e instalações já existentes.

Para migrar os JSONs de um pacote existente, usar
`tools/migrate-package-metadata.py --package PACOTE --backup ROLLBACK` para
conferir a prévia e acrescentar `--apply` para aplicar. O rollback deve ficar
fora do pacote. A migração cobre todos os JSONs do mod, inclusive
`data/lcs/animations/storage.json`, atualiza o inventário de metadados, preserva
o histórico da prova de startup e verifica que os demais arquivos não mudaram.

A pasta `GTA Liberty City Stories User Files`, criada junto ao executável pelo
modo portátil, contém saves e configurações pessoais. Ela também fica fora de
qualquer entrega pública. Consulte `docs/user-files.md`.
