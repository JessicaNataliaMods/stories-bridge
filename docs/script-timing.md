# Ritmo dos scripts e embarque de acompanhantes

## Ritmo dos scripts (v96)

No PS2 o LCS executa os scripts uma vez por quadro, a 30 quadros por segundo. Muitos scripts contam ciclos de `WAIT 0` em vez de tempo: em Smash and Grab (`VIC5`) a resistência da quadrilha começa em 100 e perde 1 ponto a cada 6 ciclos enquanto os policiais estão vivos, ou seja, 600 ciclos = 20 s; ao chegar a 0 a van explode e a missão falha.

A ponte executava a VM a cada quadro do SA. A 60 FPS esse prazo caía para 10 s, e em taxas maiores para menos ainda, o que fazia a missão falhar ao chegar ao local.

`include/lcs/script_cadence.hpp` (`ScriptCadence`) faz a VM rodar a 30 ciclos por segundo, qualquer que seja a taxa de quadros do SA:

- cada ciclo recebe o tempo decorrido desde o ciclo anterior (`WAIT n` e cronômetros continuam medidos em milissegundos);
- o passo de tempo nativo dos quadros intermediários é somado e entregue ao ciclo, limitado a 3 (como `CTimer`);
- um quadro lento não gera vários ciclos seguidos contra o mesmo estado do mundo;
- o toque de pular diálogo é mantido entre quadros até o próximo ciclo.

`tests/runtime_tests.cpp` (`original_script_cadence`) confere que um prazo contado em ciclos dura o mesmo a 30, 60 e 144 FPS.

## Embarque de acompanhantes

Quando vários acompanhantes entram no carro do jogador ao mesmo tempo (os três sobreviventes de Smash and Grab), a consulta de banco livre (`0431`) só vê ocupantes, não quem está andando até a porta. Por isso todos escolhiam o mesmo banco vazio, abriam a porta já tomada e iam procurar outra.

Agora cada pedido de embarque reserva o seu banco (`Companion::seat`) até o acompanhante ocupar o carro ou a tarefa nativa terminar; os outros escolhem bancos diferentes. `tests/companion_navigation_tests.cpp` cobre três acompanhantes pedindo entrada antes de qualquer banco estar ocupado.
