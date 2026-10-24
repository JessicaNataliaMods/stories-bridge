# Telas, vídeos e marcadores do LCS (v128)

A v128 troca as imagens de abertura do SA pelas do LCS (PS2) e desenha os cilindros amarelos de missão com a malha original.

## Marcadores de missão

- O SA desenha esferas de script (`ADD_SPHERE`) e contatos (`ADD_SPRITE_BLIP_FOR_CONTACT_POINT`) com `C3dMarkers::PlaceMarkerSet`, na escala e na cor do SA (vermelho). O retail do LCS chama a mesma rotina (PS2 0x24B140) com o tipo 5, cor 255,255,128,228, período 2048 e três cascas (tamanho 1,00/0,93/0,86; rotação 1/2/-1). Os chamadores do PS2 (0x22AD64 esferas, 0x118474 contatos) confirmam as constantes.
- A ASI substitui os dois pontos de chamada do SA (0x15647FE em `DrawScriptSpheres`, no segmento `.HOODLUM` do executável, e 0x585CE4 no ramo de contatos do radar) por um desvio que entrega o cilindro ao `ScriptMarkerPool` privado. Tamanho e pulso vêm do chamador do SA, que usa os valores do retail; cor, malha (`markers/rw-assets/lcs_marker5.dff`), pulso, rotação e ajuste ao chão são os do LCS. Qualquer outro tipo de marcador, ou qualquer recusa do renderizador, mantém o marcador nativo do SA.
- Como o desvio fica nos chamadores nativos, não depende dos opcodes: marcadores continuam depois de SAVE/LOAD e dos contatos que o SA já oculta durante uma missão.
- `LcsMarkers=0` no `.ini` mantém os cilindros do SA. Setas de carro/pedestre e os marcadores `ADD_POINT_3D_MARKER`/`ADD_ARROW_3D_MARKER` (v63) não mudam.

## Telas

`tools/prepare-lcs-screens.py` lê os `.CHK` de `SPLASH` e os `.PSS` de `MOVIES` do disco do usuário (nada disso vai ao Git) e gera, sobre as texturas do SA, arquivos que o modloader sobrepõe:

| SA | LCS |
|----|-----|
| `models/txd/loadscs.txd` e `loadsc0..14.txd` (também `LOADSUK.txd`, `load0uk.txd`) | `LOADSC0..3`, repetidas; `nvidia`/`eax` ficam |
| `title_pc_EU/US` (primeira tela com o logo) | `SCEALE` (logo + aviso legal) |
| `models/txd/splash1..3.txd` | `SPLASH1..3` ("Welcome to Portland/Staunton/Shoreside") |
| `models/txd/intro1..4.txd` (cartões de legenda) | `INTRO1..4` |
| `models/fronten2.txd` `back2..back7` | um único `LOADSC1` esticado sobre o mosaico 3x2 do menu, escurecido (o front end do PS2 usa a tela de loading tingida; aqui a metade do brilho) |
| `movies/Logo.mpg` | `RSLEEDS.PSS` |
| `movies/GTAtitles.mpg` | `DM.PSS` (abertura) |

- `.CHK`: textura PS2 de 8 bits (INTRO: 4 bits) linear, 512 de largura, paleta com as entradas 8-15 e 16-23 trocadas. As texturas saem em D3D9 A8R8G8B8 (`txd` do SA).
- Filmes: o áudio dos `.PSS` é PCM 16 bits estéreo (`SShd`/`SSbd`), entrelaçado de 512 em 512 bytes; o vídeo é MPEG-2 512x448. O conversor remonta tudo em MPEG-1/MP2 640x480 4:3, desentrelaçado (`ffmpeg` é necessário no PATH ou via `--ffmpeg`). O modloader (`std.movies`) troca os filmes pelo nome.
- A etapa 128 do `prepare-campaign-code-update.py` gera os arquivos uma vez e os compartilha com os estágios seguintes por hardlink.

Ainda não observado em jogo: o resultado visual dos marcadores e a ordem do mosaico do menu (inferida das texturas do SA).
