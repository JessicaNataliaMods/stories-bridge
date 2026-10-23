# Personagens de gameplay: slots, tipos e animacoes

`tools/prepare-gameplay-peds.py` coloca cada skin convertida do LCS em um slot existente do SA; `tools/revise-gameplay-peds.py` corrige o resultado no pacote instalado (idempotente, `--apply` grava) e `tools/gameplay_ped_profile.py` valida.

## Regras da revisao

- **Motoristas de taxi.** Os slots `STAT_TAXIDRIVER` do SA (220, 261, 262; 182/206/234 nas outras cidades) sao os motoristas de todo taxi ambiente. Personagens de historia nao podem ficar neles: Vincenzo estava em 262 e todo taxi da rua era dirigido por ele. O `taxi_d` do LCS ocupa agora o 262 (canonico) e os demais slots de taxista (aliases).
- **Motoristas fixos por veiculo.** O SA escolhe o piloto de alguns veiculos pelo modelo (`CPopulation`, switch em 0x6117F0): stretch 409 -> 255, Mr. Whoopee 423 -> 264, Securicar 428 -> 71, Pizzaboy 448 -> 155, Freeway 463 -> 247/248, BMX 481 -> 23. Stretch, Securicar e Freeway aparecem no transito (classes `executive`, `big`, `motorbike`), entao 255, 247 e 248 mostram skins de populacao (`b_man1`, `criminal01`, `criminal02`); os personagens de historia que estavam la (sal_01, Ned_01, paulie) foram para outros slots.
- **Slots de personagens de historia.** Slots que o SA nao usa em populacao nem em veiculos: os extras ja ocupavam slots fora do `pedgrp.dat`; os deslocados vao para 190 a 195 (skins de namorada, so usadas por script no SA), que passam a ser CIVMALE, `STAT_STD_MISSION`, grupo `man`, voz `VOICE_GEN_NOVOICE`.
- **Grupo de animacao e stats.** Cada skin mantinha o grupo de animacao do slot do SA (ex.: `oldman` em membros de gangue). Agora skins de populacao usam o grupo da definicao LCS (`DEFAULT.IDE`), exceto policiais/medicos/bombeiros e o grupo `pro` das prostitutas do SA; extras usam `man` (mulheres mantem um grupo feminino) com `STAT_STD_MISSION`, como os slots de personagem especial do LCS. Slots que deixaram de ser personagens de historia recuperam o stat original do SA.
- As chamadas das missoes resolvem os personagens por nome (`canonical-model-bindings.tsv`) e o `taxi_d` pela tabela `native-equivalents.tsv`; ambos sao regravados pela revisao, sem alterar os scripts.

## Estado

`data/peds.ide` deixa de ser copia byte-exata do SA nos slots vinculados (somente tipo, stat, grupo de animacao e vozes); `pedgrp.dat` continua identico. Validado offline por `gameplay_ped_profile.py`; nao verificado no jogo.
