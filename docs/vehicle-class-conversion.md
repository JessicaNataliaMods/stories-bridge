# Conversão de classes de veículos

`tools/prepare-campaign-vehicles.py` prepara modelos, texturas, colisões,
definições, cores e física a partir dos arquivos fornecidos pela pessoa usuária.
As entradas e saídas de jogo permanecem no workspace privado. O comando não
instala arquivos nem executa o jogo.

O conversor de geometria oferece `--sa-car WHEEL SCALE COLLISION`,
`--sa-bike COLLISION` e `--sa-generic-boat COLLISION`. Cada classe tem sua
validação correspondente: `--validate-sa-car`, `--validate-sa-bike` e
`--validate-sa-generic-boat`. O ferry usa o adaptador separado `--sa-boat`;
seus marcadores, portas e rampas não devem ser substituídos pelo perfil genérico.

As motos mantêm suas rodas e pivôs originais. Barcos genéricos mantêm os corpos
de alta/baixa resolução, assento e componentes móveis existentes. Todos recebem
o contrato de materiais e colisão embutida do SA. O RC Bandit normaliza somente
a caixa dos nomes dos dois corpos originais antes de usar o adaptador de carro.

No Rhino, a raiz original da torre `windscreen_dummy` recebe `misc_a` e
`barrel_dummy` recebe `misc_c`. Só renomear a boca não basta: o processamento
de dano do para-brisa no SA destrói seus frames filhos antes de preencher os
nós do veículo. A torre persistente evita isso. Um frame vazio `misc_b` permite
ao SA elevar o ponto de disparo; a malha original da torre permanece rígida,
pois o LCS não fornece uma malha independente para elevar o canhão. A posição
da boca em repouso é preservada. Sem esse nó, o fallback retail deixa a posição
usada pelo recuo sem inicialização; o validador recusa o layout incompleto.
`--validate-sa-rhino-muzzle` verifica essa hierarquia sem reaprovar materiais.

O Rhino usa o tamanho de roda do IDE uma única vez. Como o SA desenha seis
rodas por lado, o raio visual da malha original é limitado pelo intervalo
entre os centros nativos, inclusive durante a rotação. A largura da roda,
os parâmetros físicos do IDE/handling e a colisão não são alterados. Essa
adaptação visual ao layout do SA não reproduz o layout de três rodas por lado
do LCS.

O reflexo explícito ENVMAP conserva o coeficiente original do MatFX; o brilho
do EnvMat recebe o coeficiente limitado a 0–1 e o fator 0,5 utilizado pelo shader de veículos LCS antes da
quantização para o byte normalizado do SA. Não se adiciona especular onde a
origem não o define. Essa adaptação não constitui aprovação visual em jogo.

`vehicle_config_fields.py` converte o registro comum e os registros de classe
do handling. Motos usam a seção `!`; barcos usam `%`, com a altura da câmera
reposicionada para a ordem do parser SA. O montador integra os registros
especializados junto com o comum e recusa alterações concorrentes nesses
registros. Compartilhamento de uma chave de handling exige valores convertidos
idênticos e não pode alterar um veículo SA não selecionado.

Helicópteros usam `--sa-heli COLLISION WHEEL_SIZE`, com validação
`--validate-sa-heli`. Chassis e rotores recebem os nomes consumidos pelo
CHeli nativo; os slots de rotores estáticos ausentes têm consumidores com
verificação de nulo. Os quatro pontos de suspensão são obrigatórios. Quando
faltam todos, o adaptador os deriva dos quatro cantos do envelope da geometria
original de `chassis_hi`, no espaço da raiz, compensando metade do tamanho de
roda do IDE em Z. Isso adapta os helicópteros ambiente à suspensão do SA;
fragmentos de patins não são tratados como pontos de contato.
São frames vazios; nenhuma roda ou malha de rotor é fabricada. Um conjunto
parcial de pontos é recusado.

O Dodo usa `--sa-dodo-plane WHEEL SCALE COLLISION` e
`--validate-sa-dodo-plane`: preserva os quatro pontos de roda originais e usa
o CPlane do SA. As alternativas danificadas de asa, porta-malas e para-brisa
permanecem na origem, mas não têm slots de dano correspondentes nesse consumidor;
o renderizador nativo oculta essas alternativas.

Airtrain e Dead Dodo usam `--sa-ambient-plane COLLISION WHEEL_SIZE` e
`--validate-sa-ambient-plane`. Conservam as duas malhas originais do trem de
pouso, com rodas estáticas. Quatro frames vazios fornecem os pontos de suspensão:
o nariz único fornece o par dianteiro e os dois grupos de vértices das rodas
traseiras fornecem o par traseiro. O ID de LOD do IDE fica na proveniência;
não é interpretado como modelo de roda do SA. Nenhuma roda é duplicada.

O ferry também integra a frota pelo slot TROPIC, exigindo seu adaptador
dedicado `--sa-boat`. O preparador separa portas e rampas da malha original
com auditoria do multiconjunto de triângulos no espaço do modelo. A física
de barco usa os valores `% FERRY` como adaptação explícita ao consumidor SA;
o ferry original usa CFerry próprio. Os serviços existentes continuam no seu
adaptador de runtime; esta conversão não declara esses serviços concluídos.

Trens usam `--sa-train COLLISION` e `--validate-sa-train`. O CTrain nativo
dispensa a suspensão de automóvel. O adaptador conserva as rodas estáticas
originais e associa os pivôs das portas laterais e seus corpos de baixa
resolução aos nomes do SA; não inventa bogies.

O handling de aeronaves usa a seção `$` e o índice da tabela do executável
fornecido, validado dentro da faixa do consumidor nativo. Campos exclusivos
de trem de pouso e vento recebem adaptação explícita, registrada na proveniência.
O bloqueio de visão em primeira pessoa usa o flag equivalente do SA.

Se a colisão nomeada estiver ausente ou vazia, o preparador consulta o modelo
original no GAME.DTZ fornecido. Preserva limites, volumes e superfícies antes
da conversão única para SA. Para Escape, cujo ponteiro original é nulo, uma
adaptação explícita gera colisão pelos triângulos rígidos da malha original,
excluindo rotores e LODs duplicados, com superfície CAR. Essa colisão derivada
é identificada no manifesto e não constitui recuperação da física retail.
`--extend-existing` verifica os hashes e reutiliza os modelos já preparados,
convertendo somente as classes acrescentadas. Essas verificações offline não
constituem aprovação visual, física ou de instalação em jogo.
