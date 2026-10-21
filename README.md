# Stories Bridge

Ponte experimental para executar scripts de **GTA Liberty City Stories (PS2)** no motor de **GTA San Andreas PC**, com um conversor offline de assets fornecidos pela própria pessoa usuária.

A campanha ainda está em desenvolvimento. Cobertura de comandos e testes offline não significam que todas as missões estejam jogáveis. SAVE/LOAD completo da campanha e um crash de encerramento seguem pendentes. O usuário confirmou a correção da porta do PNS na entrega local v80.

Este repositório contém **somente código, testes em código e documentação**. Não inclui ROMs, executáveis dos jogos, scripts SCM, modelos, texturas, colisões, áudio, animações, dumps, relatórios locais ou tabelas extraídas do mapa. Os dados devem vir de cópias próprias dos jogos.

## Organização

| Diretório público | Conteúdo |
| --- | --- |
| `src/`, `include/` | Runtime, adaptação aos sistemas SA e conversores |
| `tools/` | Conversão, empacotamento e validação |
| `tests/` | Código dos testes; fixtures de jogo ficam fora do Git |
| `docs/` | Arquitetura e regras de armazenamento |
| `third_party/fastlz/` | Biblioteca genérica, com licença preservada nos arquivos |

O diretório irmão `private-bridge/` guarda entradas originais, referências locais, dependências, dados gerados, builds, pacotes e evidências. Os atalhos locais de desenvolvimento são ignorados pelo Git e não são distribuídos. Consulte [a divisão público/privado](docs/public-private-layout.md).

## Desenvolvimento local

Requer Windows, Visual Studio 2022 com C++, CMake e Python 3.12 ou posterior. Prepare suas dependências e entradas privadas e execute:

```powershell
./tools/setup-private-workspace.ps1
cmake -S . -B ../private-bridge/build -A Win32
cmake --build ../private-bridge/build --config Release --target LcsSaBridge
```

O CMake exige o checkout de referência privado em `private-bridge/references/res-relcs`; conversores nativos usam as dependências privadas librw/librwgta. Includes derivados são gerados no build privado. O include de camadas condicionais do mapa deve estar em `private-bridge/generated/world_sector_layers.inc`, produzido a partir da conversão local. Nenhum desses dados está embutido neste checkout.

Execute somente os testes dos caminhos alterados. Os testes com dados de origem exigem os arquivos privados; a CI pública verifica a fronteira de publicação sem baixar jogos. A ferramenta atual ainda não é a interface gráfica final de conversão.

## Publicação

```powershell
py -3 tools/check-public-tree.py --staged
```

O setup ativa os hooks de commit/push. A allowlist, os hooks e a CI recusam arquivos de jogo, binários, links para dados privados e padrões reconhecidos de credenciais. Não use `git add -f` nem publique arquivos de `private-bridge`.

Licença do código original do projeto: [MIT](LICENSE). Dependências mantêm suas licenças. A licença do projeto não concede direitos sobre conteúdo dos jogos ou código de terceiros.
