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

Aeronaves e trens continuam exigindo adaptação dos consumidores de componentes,
animação e física próprios. Em particular, reconhecer um nome de rotor no
callback de renderização não demonstra que os nós estáticos/móveis exigidos
pelo helicóptero nativo estejam completos. O manifesto registra bloqueios;
uma conversão de geometria isolada não conta como veículo integrado.
