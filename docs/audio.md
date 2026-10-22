# Áudio original do LCS

A ponte não distribui áudio. O pacote da campanha (a partir da v90) lê em tempo de execução a pasta `AUDIO` da cópia de PS2 da própria pessoa usuária (Liberty City Stories, SLUS-21423).

## Instalação

Copie a pasta `AUDIO` do disco de PS2 para dentro da pasta do mod, ao lado do `LcsSaBridge.asi`:

```
modloader/stories_bridge/
  LcsSaBridge.asi
  LcsSaBridge.ini
  AUDIO/
    CUTSCENE/  MUSIC/  NEWS/  SET0/ ... SET6/
```

Não há caminho a configurar. Todas as pastas da campanha são subpastas fixas do `.asi`; o `.ini` do pacote não contém caminhos. O instalador (`tools/install-modloader-map.ps1`) preserva a pasta `AUDIO` nas atualizações.

## O que é lido

| Conteúdo | Origem | Leitura |
| --- | --- | --- |
| Efeitos e falas de missão | `SET0..SET6/SFXn_PS.SDT` + `.RAW` (67 bancos, 5661 amostras) | decodificados sob demanda |
| Notícias de rádio | `NEWS/NEWS_A..N.VB` | decodificadas ao tocar |
| Voz das cutscenes | `CUTSCENE/<NOME>.VB` | decodificada ao preparar a cena |
| Música de ambiente e final | `MUSIC/CITY.VB`, `MUSIC/FINALE.VB` | como antes, da mesma pasta |
| Rádios | `MUSIC/HEAD.VB` ... `MUSIC/LCFR.VB` | transmitidas do disco (ver abaixo) |

### Bancos de amostras

Cada `.SDT` tem registros de 12 bytes (`offset`, `tamanho`, `frequência`) sobre ADPCM do SPU2 em `.RAW`. A tabela de amostras do jogo (`eSfxSample`) segue a ordem: banco 0, banco 1, bancos 3 a 66 e, por último, o banco 2.

- Amostras com cabeçalho `VAGp`: o primeiro quadro (silencioso) é omitido; a reprodução termina no primeiro quadro com a marca de fim; se esse quadro tiver a marca de repetição, o loop começa no quadro com a marca de início de loop.
- Amostras sem cabeçalho começam no quadro de início de loop (é o caso do sino e do aviso da ponte levadiça).
- Cada amostra é decodificada como uma voz do SPU2 a partir do key-on: histórico do preditor zerado e saturação em 16 bits.
- Os 27 registros sem cabeçalho dos bancos de motor (`SET1`) não trazem frequência no `.SDT` e são recusados com diagnóstico. A ponte ainda não toca som de motor do LCS.

O `sfx.RAW` convertido usado antes da v90 carregava o histórico do preditor de uma amostra para a seguinte e dava a volta (em vez de saturar) em estouros. Por isso o áudio atual difere dele em poucas amostras iniciais de alguns sons; os metadados (frequência, loop, duração) são idênticos.

### Cutscenes e notícias

Usam o decodificador estéreo de 32 kHz (blocos de canal de 0x2000 bytes). Cada cutscene decodificada precisa ter o mesmo SHA-256 da onda canônica registrada em `data/lcs/cutscenes/index.tsv`; se não tiver, a voz da cena é recusada.

## Rádio

Com `LcsRadio=1` (padrão), as estações do LCS substituem o áudio das rádios do SA. O SA continua cuidando da escolha de estação, da troca (retune), de ligar e desligar no veículo e do HUD; a ponte só troca o que se ouve:

- Enquanto uma estação do SA está ativa, a estação correspondente do LCS toca por uma saída própria, lida bloco a bloco de `AUDIO/MUSIC`.
- O volume é o volume de rádio do menu (`0xBA6798`, 0..64) vezes o fader de música do SA (`CAEAudioHardware+0x418`), então o menu, os fades de cutscene e de missão continuam valendo. Durante falas de missão o rádio fica em 25, como no `cMusicManager::ServiceTrack`.
- Enquanto a estação do LCS toca, o volume mestre de música do SA (`+0x410`) fica em zero para silenciar a transmissão do próprio SA. Ele volta ao valor do menu quando o rádio desliga ou quando a estação é a de músicas do usuário.
- Ao começar uma estação, o volume sobe como no `ServiceTrack`: 10 quadros em silêncio e depois +6 por quadro até 100.
- Sintonia: enquanto o SA troca de estação (passos pendentes em `0x8CB6F8+0x6C`/`+0x70`), a estação para e toca o chiado do rádio da polícia (`SFX_POLICE_RADIO_CRACKLE`, em loop, volume aleatório de 15 a 34) e até dois cliques de dial (`SFX_RADIO_DIAL_1..3`, volume 127, intervalo de 800 ms) depois de cada toque, como no `cMusicManager` do LCS. O chiado do próprio SA fica silenciado. Esses sons seguem o volume de efeitos do SA.
- Nome da estação: o SA monta a chave de texto `FEA_R%d` em `GetRadioStationName` (`0x4E8380`). As três chamadas (`0x4E9E21`, `0x55B36A`, `0x55B3AE`) passam pela ponte, que devolve a chave original do LCS (`FEA_FM0..9`, presente no texto do pacote em todos os idiomas). Com `LcsRadio=0` as chamadas não são alteradas.

Estações (ordem da tabela de streams do LCS) e a posição do SA que toca cada uma:

| LCS | Arquivo | Posição do SA |
| --- | --- | --- |
| Head Radio | `HEAD.VB` | Radio X (7) |
| Double Clef FM | `DOUBLE.VB` | K-Rose (2) |
| K-Jah | `KJAH.VB` | K-Jah West (9) |
| Rise FM | `RISE.VB` | SF-UR (5) |
| Lips 106 | `LIPS.VB` | CSR 103.9 (8) |
| Radio del Mundo | `MUNDO.VB` | Master Sounds (10) |
| MSX 98 | `MSX.VB` | Bounce FM (4) |
| Flashback 95.6 | `FLASH.VB` | K-DST (3) |
| The Liberty Jam | `LCJ.VB` | Playback FM (1) e Radio Los Santos (6) |
| LCFR | `LCFR.VB` | WCTR (11) |

O SA tem onze posições para dez estações, por isso duas posições tocam The Liberty Jam. Músicas do usuário (12), o rádio da polícia (0) e rádio desligado (13) continuam nativos.

Comandos de script:

- `SET_RADIO_CHANNEL estação, posição` sintoniza a posição do SA da estação pedida (10 = desligado) e, se a posição for informada, a estação recomeça dali.
- `GET_RADIO_CHANNEL` devolve a estação do LCS da posição ativa; -1 para o rádio da polícia; 10 para músicas do usuário e desligado.
- Sem o rádio do LCS (`LcsRadio=0`), esses comandos mantêm o comportamento anterior (rádio nativo desligado durante as falas de missão e mapeamento de gêneros).

Posição de cada estação: como em `cMusicManager::GetTrackStartPos`, a estação continua "tocando" enquanto não é ouvida, avançando pelo tempo decorrido (no máximo 270 s por verificação) e voltando ao início no fim do arquivo. As posições iniciais de jogo novo vêm da tabela `NewGameRadioTimers` da referência reLCS.

Limitações atuais:

- As posições das estações não são gravadas no save; recomeçam das posições de jogo novo ao carregar.
- Enquanto uma estação do LCS toca, a música de ambiente `CITY` do script também fica sem som (no LCS rádio e ambiente dividem o mesmo stream).

## Opções do `.ini`

| Chave | Padrão | Efeito |
| --- | --- | --- |
| `LcsRadio` | `1` | `0` mantém as rádios do SA |

Sons de veículos, armas e pedestres do LCS ainda não estão implementados; cada um receberá a sua chave quando existir, e desligá-la manterá o som do SA.

## Validação

- `tests/ps2_audio_source_tests.cpp`: compara os metadados das 5661 amostras com o índice convertido, decodifica todos os nomes de áudio de missão, as 14 notícias (idênticas às ondas preparadas) e confere o hash das 21 cutscenes.
- `tests/lcs_radio_tests.cpp`: mapeamento SA↔LCS, regra de posição do retail, cálculo de bloco e decodificação do stream lido do disco.

Os dois rodam na validação de inicialização do pacote (`tools/validate-campaign-startup.py`).
