# Mira com a mao reta (Straight hand aim)

Origem: o mod Straight_Hand_Aim (antes CJHandStraight), incorporado como padrao na campanha. Codigo: `src/sa_hand_aim.cpp`, chamado de `install_hook` em `src/asi.cpp` (somente `Mode=Campaign`).

## O que faz

As animacoes de mira do SA deixam a mao das armas de uma mao girada de lado. Logo antes de `CPed::Render` a matriz do osso da mao direita (e da esquerda, com armas duplas) e reconstruida: eixo para frente na linha do antebraco, eixo `at` na vertical do mundo, dedos acompanhando a mao rigidamente. A arma copia o osso da mao, entao segue. A mira do jogo nao muda: so o giro da mao.

- Ganchos: slots `PreRender` (+0x44) e `Render` (+0x48) das cinco vtables de `CPed` (CPed 0x86C358, CPlayerPed 0x86D168, 0x86C0A8, 0x86C120, 0x86C200). Os ponteiros que ja estavam nos slots (funcao do jogo ou gancho de outro mod) sao chamados depois; nada e substituido.
- Pose original restaurada em `PreRender`, no mesmo esqueleto de onde foi salva, para a logica do jogo nunca ver a pose corrigida de um frame antigo.
- So corrige quem esta mirando: tarefa secundaria de ataque presente (`ped+0x47C` -> CPedIntelligence+4 -> CTaskManager, slot secundario 0 em +0x14; `m_pPed` em +0x2C confere o ped). Layout desconhecido nunca corrige.
- Entrada e saida da mira usam uma rampa (150 ms / 350 ms), e a correcao some suavemente com o braco quase vertical (pose de repouso), onde a referencia "em pe" e instavel.
- Armas: tipos 22, 23, 24, 26, 28 e 32 (pistola, silenciada, Desert Eagle, serrada, Micro Uzi, Tec-9) e qualquer arma que use o mesmo grupo de animacao (`CWeaponInfo+0x1C`) de um desses tipos; armas de mods entram sozinhas. A mao esquerda so e corrigida com a flag TWIN_PISTOL (`CWeaponInfo+0x18`, bit 0x800). Armas de duas maos nao sao tocadas.
- Offsets verificados no executavel: tipo do ped em `ped+0x598` (`CPed::IsPlayer`, 0x5DF8F0), inteligencia em `ped+0x47C`. Executavel diferente do 1.0 US: nada e instalado.

## `LcsSaBridge.ini`

Todas as chaves ficam em `[LCS]`, valem `1` quando ausentes e `0` desliga a parte: `HandAim` (tudo), `HandAimCJ`, `HandAimNPCs`, `HandAimGrove`, `HandAimBallas`, `HandAimOtherGangs` (Vagos, Rifa, Da Nang, Mafia, Triades, Aztecas), `HandAimCops`, `HandAimCivilians`, `HandAimOtherPeds`, `HandAimDualLeftHand`, `HandAimSameAnimationGroup`. Outras: `HandAimExtraWeaponTypes` (lista de tipos), `HandAimAxis` (0 antebraco, 1 eixo da mao, 2 mistura), `HandAimBlendPercent`, `HandAimMaxDeviationDegrees`, `HandAimBlendInMs`, `HandAimBlendOutMs`.

## Estado

Compilado no alvo `LcsSaBridge`; nao testado no jogo da campanha. O mod independente foi testado pelo usuario em um GTA SA normal.
