# HUD do LCS

A partir da v98 a ASI desenha a HUD de jogo com as texturas e fontes originais do LCS (PS2) no lugar das fontes do SA. A v106 estende as fontes ao menu principal, pausa, opções, saves, estatísticas e controles, preservando a lógica e o layout nativos dos menus.

- `LcsHud=1` (padrão) liga a camada; `LcsHud=0` mantém a HUD do SA. Se `models/lcs/lcs_fonts.txd` ou `lcs_hud.txd` faltarem, ou o RenderWare não ler os arquivos, o log registra `LCS UI unavailable, SA HUD kept` e a HUD do SA continua.
- Os TXDs vêm do conversor (`models/fonts.txd`, `models/hud.txd`), são convertidos de D3D8 para D3D9 e recebem o prefixo `lcs_` em `models/lcs/`. O prefixo impede que o modloader substitua as fontes e sprites nativos do SA, inclusive os usados pelo menu.
- Layout: o `Hud.cpp` do reStories, cujas posições vêm do PSP (base 480×272, escaladas por altura/272 nos dois eixos e ancoradas às bordas). Texturas e fontes são as do PS2; telas ultrawide não usam largura/480 para esticar os elementos.
- Fonte: `src/ui_font.cpp` (larguras, tokens `~r~` etc., quads por glifo, sombra); testes em `tests/ui_font_tests.cpp`.
- Desenho: `src/sa_ui_draw.cpp` lê os TXDs pelo RenderWare (`RwStreamOpen` 0x7ECEF0, `RwTexDictionaryStreamRead` 0x804C30, `RwTexDictionaryFindNamedTexture` 0x7F39F0) e envia quads por `RwIm2DRenderPrimitive` (0x734E90).
- Carga: na primeira frame que precisa (o RenderWare ainda não existe quando a ASI instala os ganchos).

## Correções de desenho

- A submissão usa `rwPRIMTYPETRISTRIP` (4) para os vértices TL/TR/BL/BR. Usar `rwPRIMTYPETRIFAN` (5) com essa ordem deixa um triângulo descoberto e sobrepõe parte dos outros, cortando barras, números e sprites.
- Cada submissão preserva os oito estados RenderWare que altera e os restaura também após falha de desenho. Isso evita deixar estados da camada LCS nos próximos consumidores nativos.
- A fonte `heading` sempre remapeia os caracteres para o atlas reduzido, tanto na medição quanto no desenho, conforme o contrato original de seleção da fonte.
- A escala própria do LCS usa a resolução efetiva e não reaplica os multiplicadores do Widescreen Fix. Isso preserva proporções em ultrawide, mas os ajustes de tamanho da HUD do SA no INI desse mod não configuram a camada LCS. Compatibilidade visual completa ainda requer observação em jogo.

## Fases
1. Contadores e cronômetro de missão (v98).
2. Vida, colete, dinheiro, relógio (v99). O SA é silenciado só nesses elementos, pelos pontos de chamada de `CHud::DrawPlayerInfo` (0x58EAF0: vida 0x58EE9A/0x58EF0D, colete 0x58EF6B/0x58EFDE, relógio 0x58EC21, dinheiro 0x58F607); a barra de fôlego do SA fica, pois o LCS não tem equivalente. Escala por altura (height/272), ancorada à direita.
3. Ícone de arma, munição e estrelas de procurado (v100). Ícone: textura com o nome do modelo original dentro do TXD de cada arma (`weapons/<modelo>.txd`); punhos usam o sprite `fist` do `hud.txd`. Pontos de corte do SA: ícone 0x58F944/0x58F9B1, munição 0x58FA25/0x58FABE (cdecl, 4 argumentos) e `DrawWanted` 0x58FBDB (0x58D9A0). Cada um só é pulado quando o LCS realmente desenhou.
4. Legendas, mensagens grandes, ajuda, nome de zona e de veículo (v101). O SA continua decidindo quando o texto aparece e como some; as chamadas de impressão dele (`CFont::PrintString` 0x71A700 e `PrintStringFromBottom` 0x71A820, cdecl) em dez pontos da HUD são entregues à camada do LCS, que desenha o mesmo texto com posição, escala, cor, sombra e caixa (ajuda) do LCS e o alfa do SA. Pontos: zona 0x58AE5D, veículo 0x58B156, legendas 0x58C68A, ajuda 0x58BD67, mensagens grandes 0/1/2/3/4/5 em 0x58CA30/0x58D470/0x58CC6A/0x58CFA0/0x58CD99/0x58D21A. Ficam com o SA: slot 6 (0x58CE9E), textos de script, nome do rádio.
5. Radar (v102). O SA mantém os tiles do mapa (os do LCS em `models/radar`), a máscara, o zoom e a lógica de blips. A ASI (a) move o radar para a caixa do LCS (82x82 unidades na v110, a 40 da esquerda e de baixo) trocando `CRadar::TransformRadarPointToScreenSpace` (0x583480; o original fica num trampolim para o mapa do menu de pausa), (b) troca os quatro desenhos do disco do SA em `CHud::DrawRadar` (0x58A823/0x58A8CD/0x58A977/0x58AA25) pelo `radardisc` do LCS (cópia preta 2 unidades abaixo) e (c) grava os ícones de radar do `hud.txd` do PS2 na tabela `CRadar::RadarBlipSprites` do SA (0xBAA250). Com `LcsHud`, a alocação compartilhada de `radar_sprites.hpp` mantém um slot por ícone e reserva o slot 41 para o waypoint nativo; Vincenzo e bomba usam slots distintos do waypoint. Ícones que o `hud.txd` do PS2 não tem (sobras do Vice City) ficam com a textura do SA.

## Identidade dos blips e LOAD

O waypoint do mapa é criado pelo SA fora da VM e usa o slot 41. Ele não pode compartilhar textura com um contato do LCS. A tradução de comandos e a montagem das texturas usam a mesma função de alocação.

Saves antigos preservam IDs de sprite nativos que podem ter colapsado vários contatos no mesmo ícone. A recuperação no LOAD consulta o MAIN original do usuário: exige sprite literal, global de saída contendo um handle possuído pelo porte, geração válida, mesmo tipo/alcance e coordenadas coincidentes. As coordenadas podem ser literais no criador ou constantes de um controlador nomeado, lançado no MAIN: nesse caso, a análise de fluxo verifica todos os escritores dos locais, inclusive GOSUBs no frame compartilhado e saídas de CALL. Z abaixo do mapa é tratado como sentinela de altura do chão; XY continua obrigatório. Casos ambíguos ou alterados por outro consumidor ficam intactos. Só o sprite de um blip existente é ajustado; não há replay de script, criação de blip ou alteração dos arquivos de save.

## Fontes dos menus

`sa_ui_menu.cpp` intercepta 45 chamadas do frontend e do desenho 2D: impressão, impressão ancorada embaixo, medição e descarga do buffer de fontes. O estado de fonte do SA fornece posição, escala, alinhamento, quebra, cor, sombra e contorno; `ui_menu.cpp` usa os glifos e as larguras do LCS. Os desenhos são adiados até a descarga nativa, preservando a ordem dos fundos e textos. A v108 inclui as descargas finais do Render2D e a chamada direta a RenderFontBuffer: as descargas do fundo do menu ocorrem antes das opções e não encerram a fila de texto do frame. Tokens de botões e fontes externas sem suporte usam o renderizador nativo com o estado capturado e na mesma ordem. A v109 também preserva o texto inteiro no renderizador e na medição nativos quando contém bytes de glifos estendidos ou UTF-8: os índices de acentos do SA/plugins não são índices do atlas LCS. Na v107, `LcsMenuFonts=1` (padrão) liga as fontes LCS nos menus; `LcsMenuFonts=0` mantém as fontes SA. A chave é independente de `LcsHud`: qualquer combinação é permitida. Alterações no INI entram em vigor ao reiniciar o jogo. A validação offline cobre layout e os pontos de chamada do executável; o resultado visual ainda depende de observação em jogo.

## Proporção do radar

A v110 adota a caixa 82x82 de FIX_RADAR do source de referência. A caixa antiga 94x76 produzia um radar achatado mesmo com escala uniforme. Mapa, máscara e moldura compartilham a caixa corrigida; a escala por altura mantém os dois eixos iguais em pixels, inclusive em 3440x1440, sem reaplicar multiplicadores do Widescreen Fix. As miras de sniper e câmera incluídas na v110 também usam escala por altura nos dois eixos. Testes cobrem 4:3, 16:9 e ultrawide; resultado visual ainda não observado pelo agente.

## Colete duplicado

A v112 corrige a identificação dos pontos de chamada: RenderArmorBar é 0x5890A0 (calls 0x58EF6B/0x58EFDE); 0x589190 é RenderBreathBar. As versões anteriores suprimiam fôlego e deixavam o colete branco do SA junto da barra azul do LCS. O fôlego nativo volta a permanecer intacto, e somente o colete do SA é suprimido após o desenho LCS bem-sucedido.

## Oxigênio no layout LCS

A v113 substitui somente o desenho do fôlego: barra branca à esquerda da vida, separada por4 unidades PSP, mesma largura/altura e contorno LCS. Os calls nativos58F131/58F1AD ainda determinam quando desenhar; a camada lê o fôlego real de CPlayerData e o máximo de CStats (modificador8), mantendo consumo, recuperação, condição de exibição e flash do SA. Sem HUD LCS ou em falha de leitura/desenho, o renderizador nativo continua. Testes cobrem alinhamento, branco, preenchimento e proporções4:3/16:9/ultrawide.

A v114 recorta o primeiro quarto transparente da moldura do oxigênio, mantém preenchimentos dentro do contorno visível e usa gap1PSP entre as bordas visíveis da vida e do oxigênio. A largura corresponde à região visível da vida, sem contar o padding da textura.
