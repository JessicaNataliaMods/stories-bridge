# Objetos e iluminação do mapa

O mapa convertido precisa de mais que DFF, TXD e colisão. O carregador de IPL
do SA usa a associação ObjectData para distinguir prédios de objetos físicos.
Sem ela, um poste com colisão correta continua sendo um prédio imóvel.

`mount-campaign-garage-doors.py` monta também os demais modelos presentes no
OBJECT.DAT original, usando os aliases do pacote. Conserva os parâmetros
físicos do LCS e traduz códigos de dano para os consumidores nativos do SA.
Os efeitos de dano não podem ser traduzidos para remoção do objeto sem uma
representação visual. A troca de malha exige o par original comprovado e o
atomic `{modelo}_dam`, associado pelo bit IDE 0x1000. A fragmentação exige o
plugin Geometry `0x253F2FD`, construído com a própria geometria, UVs, cores e
materiais do objeto, antes de habilitar os efeitos 200/202 do SA. Sem essa
geometria, conserva-se a física de queda, sem mandar ocultar o objeto. Isso
não reproduz as partículas e os sons particulares de cada dano LCS. Garagens e
objetos explosivos conservam seus consumidores existentes. Um código sem
tradução é erro de conversão, nunca uma definição imóvel silenciosa.

`ps2_effects.py` lê os efeitos por modelo no GAME.DTZ e escreve o plugin de
efeitos da Geometry do SA. As posições são locais ao modelo e recebem a
transformação nativa da instância; não se duplicam efeitos nos modelos de
camadas de renderização. `map_effects.py` adapta um arquivo IMG já convertido
sem reconverter a geometria e sem modificar o inode de um rollback compartilhado.
O validador reconstrói o resultado a partir dos originais da pessoa usuária.

O modelo de semáforo recebe seu ID por `model-bindings.tsv`. A ASI intercepta
o passe de efeitos da entidade visível. Para esse modelo, seleciona a cor
com as funções nativas de orientação e fase visual do SA e chama o passe
nativo usando tamanho, alcance e texturas das luzes LCS. A seleção temporária
é restaurada antes de processar outra instância. Os demais modelos seguem o
alvo original. Os ciclos e as decisões do tráfego continuam nativos.

A v115 falhou na observação do usuário: os objetos sumiam ao receber dano.
Os postes foram depois confirmados acesos à noite; o apagamento relatado
permaneceu específico dos semáforos. A correção seguinte requer compilação
e validação antes de instalação; inspeção estática não prova seu resultado visual.

`prepare-map-feature-update.py` monta uma atualização sobre os arquivos locais
atuais, preservando personalizações de armas, HUD, scripts e veículos. O
estágio ainda precisa passar nas validações de startup e pacote antes de
qualquer instalação. Relatórios, tabelas de efeitos e assets gerados ficam
exclusivamente no workspace privado. Validação offline não comprova o
resultado visual dentro do jogo.
