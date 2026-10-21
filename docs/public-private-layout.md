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
