# Animações do LCS nos pedestres convertidos

Os pedestres convertidos (`convert_ped.cpp`, `--sa-ped`) mantêm cada skin original em coordenadas locais dos ossos e usam a árvore de frames do SA. As animações do LCS e do SA seguem a mesma orientação absoluta (um `sprint_civi` de cada jogo difere cerca de 1°), então uma animação original fica exata quando cada osso termina com a mesma orientação de mundo que tem no rig original.

## O que muda entre os dois esqueletos

Mesmos ids, eixos e pais, exceto as coxas: no LCS são filhas de `Spine`, no SA de `Pelvis`. Copiar a chave da coxa sem mais nada deixa os pés de 6 a 16 cm fora do lugar sempre que a coluna se mexe (pés escorregando e dobrados errado). A conversão grava a coxa como `spine * coxa` (ordem de matrizes linha do RenderWare), reamostrada onde os tempos de coxa e coluna diferem. Medido numa simulação dos dois rigs (`tools/ped_anim_audit.py`): erro de junta mediano 3,5 cm, máximo 11 cm em movimentos extremos (rolagem do carro), contra 6 a 16 cm sem a regra.

Resto da regra (`tools/lcs_ifp.py`): quaternion do ANPK é o conjugado do ANP3; só o osso raiz mantém translação; ossos que a skin de 24 ossos não tem (Belly, seios, Jaw, sobrancelhas, Finger01, props) são ignorados; tempos em 1/60 s.

## O que é gerado

| Saída | Ferramenta | Conteúdo |
|---|---|---|
| `anim/ped.ifp` | `tools/prepare-ped-animations.py` | Bloco ANP3 `ped` do SA com as chaves do LCS em todas as animações de mesmo nome (164) e nas 38 com nome só do SA mapeadas a uma animação equivalente do LCS (tabela `ALIASES`: variantes de habilidade de direção, gaits, travessia de rua...). As 62 animações que só existem no LCS são acrescentadas. As 92 sem equivalente (escalada, nado, combos corpo a corpo, reações de dano, kart...) continuam dados do SA. |
| `data/lcs/animations/LG00.ifp.bin`, `named/LN00.ifp.bin` | `tools/prepare-gameplay-animations.py`, `prepare-named-animations.py` | Gestos de missão e bloco `riot`, com a mesma regra da coxa. Todo o resto dos registros continua byte a byte. |

As animações de veículo, assento, pulo e queda (`CAR_`, `BIKE_`, `Drive`, `KART_`, `SEAT_`, `TRAIN_`, `DOOR_`, `JUMP_`, `FALL_`, `EV_`) mantêm a duração do SA, porque o código de tarefas do SA espera tempos fixos delas (portas, assento, impulso do pulo): os tempos das chaves originais são escalados quando a duração difere menos que um fator dois. As demais usam o tempo original.

## Verificação

- `tests/lcs_ifp_tests.py`: dados sintéticos (ordem do quaternion, coxa, codificação, bloco, ANPK).
- `tests/gameplay_animation_tests.py`, `named_animation_tests.py`, `campaign_animation_storage_tests.py`: matemática independente confirma que cada coxa é `spine * coxa` e que o resto é idêntico à origem.
- `tools/ped_anim_audit.py --original-rig <skin original>.dff --final-rig <skin convertida>.dff --original-ifp ... --converted-ifp ...`: erro de junta de cada animação nos dois rigs.
- `tools/validate-modloader-map.py` regenera `anim/ped.ifp` a partir das fontes e compara byte a byte.

## Estado

Estágio `campaign-copy4-v124`, instalado (registro `pedAnimations124` no `campaign.json`): validação de inicialização (50 verificações) e do pacote aprovadas. Nada observado no jogo. Os blocos de arma (`anim/weapons`) continuam os do SA (`weapon_animations.VERIFIED` vazio): a conversão de armas depende do alinhamento da arma na mão, ainda não verificado, e fica fora deste lote. As animações de cutscene (`cuts.img`) tocam nos modelos de cutscene de cada cena, que trazem o próprio esqueleto (as coxas ficam sob `Pelvis` na maioria e sob `Spine` em poucos), e não passam por esta regra.
