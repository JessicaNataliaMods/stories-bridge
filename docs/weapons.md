# Armas originais do LCS

A partir da v94 cada arma do LCS ocupa um tipo de arma e um modelo próprios do SA. As tabelas ficam em `tools/weapon_slots.py`; o runtime usa a mesma correspondência em `src/native.cpp` (`equivalent_weapon_counterparts`), e `tests/weapon_slot_contracts.py` confere que as duas são iguais.

## Tipos e modelos

O LCS tem 37 tipos de arma (a mesma lista do `WEAPON.DAT` original). Os modelos convertidos (ids 258 a 294) substituem modelos de arma do SA de mesmo papel. Quando o SA não tem uma arma equivalente livre, o LCS usa tipos que o SA não usa na campanha:

| Arma do LCS | Modelo original | Tipo do SA | Modelo do SA |
| --- | --- | --- | --- |
| ScrewDriver | `chisel` | `DILDO2` (11) | `gun_dildo2` (322) |
| GolfClub | `hockeystick` | `GOLFCLUB` (2) | `golfclub` (333) |
| Hammer | `handaxe` | `DILDO1` (10) | `gun_dildo1` (321) |
| Cleaver | `cleaver` | `VIBE2` (13) | `gun_vibe2` (324) |
| Machete | `machete` | `VIBE1` (12) | `gun_vibe1` (323) |
| SilencedIngram | `ingramsl` | `PISTOL_SILENCED` (23) | `silenced` (347) |
| M60 | `m60` | `COUNTRYRIFLE` (33) | `cuntgun` (357) |

As demais armas continuam no tipo do SA de mesmo papel (soco-inglês, cassetete, faca, taco, katana, motosserra, granadas, molotov, pistola, Python→Desert Eagle, escopetas, Tec-9, Uzi, MP5, M4, Ruger→AK-47, sniper, lança-foguetes, lança-chamas, minigun, detonador, câmera).

Exceções que continuam compartilhadas:

- **LaserScope** usa o tipo e o modelo do sniper: no SA a câmera de mira (zoom em primeira pessoa) é exclusiva do tipo sniper.
- **HeliCannon** usa a minigun (só aparece montado em helicóptero).
- `fingers` (293) não pertence a nenhum tipo de arma e fica com o soco-inglês.

Ao reaproveitar `PISTOL_SILENCED`, o tiro da Ingram fica silencioso também para os pedestres, o que corresponde à arma original.

## Minigun

No LCS o cano giratório é um modelo separado (`minigun2`, 294) desenhado sobre a mão direita do jogador (`CPed::Render`): mão × translação (0,829; −0,001; 0,226) × rotação fixa (−4,477°, 29,731°, 1,064°, multiplicados por `vecTestTemp` = −1) × rotação de giro no eixo X. O SA usa um único modelo com o quadro `minigun2`, que gira no eixo X local (pré-multiplicado) à mesma velocidade do campo de giro do jogador. A conversão junta os dois: o corpo convertido mais o quadro `minigun2` (filho da raiz) com a parte fixa da transformação e a geometria original do cano. O dicionário de texturas do cano é o mesmo do corpo.

## `weapon.dat`

`data/weapon.dat` é o `weapon.dat` do SA com os valores da arma original de cada tipo (a arma cujo modelo está montado nesse tipo):

- alcance da arma (coluna D) e alcance de mira (C, limitado ao alcance);
- munição por pente (K), dano (L);
- deslocamento do disparo (M, N, O), quando a linha original tem deslocamento ao longo do cano;
- velocidade, raio, duração e dispersão de projéteis e área (b..e);
- nos tipos reaproveitados: espaço de inventário do LCS (corpo a corpo 1, submetralhadora 4, pesadas 7) e, nos corpo a corpo, o combo de golpe (`KNIFE` para faca/chave de fenda, `BBALLBAT` para taco); a Ingram copia grupo de animação, laços e flags da Micro Uzi, e a M60 os da minigun.

Os três níveis de habilidade do SA recebem os mesmos valores (o LCS não tem habilidade com armas). Cadência e tempo de recarga do LCS não têm coluna no SA (vêm dos laços da animação) e não são convertidos.

A partir da v95, nas armas de fogo cujas animações foram convertidas (abaixo), todos os níveis de habilidade usam o grupo de animação do LCS e os laços do `WEAPON.DAT` original (colunas R..X → T..Z do SA): sem variantes de pouca habilidade nem pistolas duplas, como no LCS. A arma que já usa esse grupo no SA mantém as próprias flags; a que muda de grupo recebe as flags padrão da arma do SA dona do grupo (a Micro Uzi passa a usar o grupo `uzi`, de duas mãos, como a Uzi do LCS; a escopeta de cano curto passa ao grupo `buddy`).

## Animações das armas (v95)

Os blocos de animação de arma do SA (`anim/anim.img`, formato ANP3) são reconstruídos em `anim/weapons/<bloco>.ifp`, que o modloader (`std.stream`) usa no lugar das entradas de mesmo nome do `anim.img`. Cada animação que tem equivalente no LCS é trocada pela animação original convertida; as que só existem no SA (por exemplo `2guns_crouchfire`, `sawnoff_reload`) ficam byte a byte iguais.

| Bloco do SA | Animações trocadas |
| --- | --- |
| `colt45` | `colt45_fire`, `_reload`, `_crouchfire`, `_crouchreload` |
| `python` | `python_fire` (e `_fire_poor`), `_reload`, `_crouchfire`, `_crouchreload` |
| `shotgun` | `shotgun_fire` (e `_fire_poor`), `_crouchfire` |
| `buddy` | `buddy_fire` (e `_fire_poor`), `_reload`, `_crouchfire` |
| `uzi` | `UZI_fire` (e `_fire_poor`), `_reload`, `_crouchfire`, `_crouchreload` |
| `rifle` | `RIFLE_fire` (e `_fire_poor`), `_load`, `_crouchfire`, `_crouchload` |
| `flame` | `FLAME_fire` |
| `grenade` | `WEAPON_start_throw`, `WEAPON_throw`, `WEAPON_throwu` |

Conversão de cada animação (blocos ANPK do LCS em `out/img-v1`):

- ossos pelo nome do LCS para o id do SA (mesma numeração: 0–5, 21–25, 31–35, 41–44, 51–54); nós que não são ossos do pedestre (o `FLAME_fire` anima o nó `chnsaw`) são ignorados;
- rotação: o ANPK guarda o quaternion conjugado, o ANP3 não, então x, y, z mudam de sinal; escala 4096;
- tempo em 1/60 s;
- translação só na raiz (escala 1024): nos outros ossos a translação do LCS carrega o comprimento dos ossos do LCS e deformaria os pedestres do SA.

As convenções foram conferidas nos dados: o `colt45_reload` convertido reproduz os quaternions e a duração do `colt45_reload` do próprio SA.

Ficam como no SA: as animações corpo a corpo (o SA monta o combate a partir de conjuntos de combos com nomes próprios), o sniper (o bloco do LCS não tem o mapa de ossos verificado), o lança-foguetes (nomes diferentes) e a M60, que usa o grupo `flame`.

O validador do pacote (`tools/validate-modloader-map.py`) regenera o `weapon.dat`, a minigun e os blocos de animação a partir dos arquivos de origem e compara byte a byte. O instalador recusa outro mod que substitua `weapon.dat` ou esses blocos.
