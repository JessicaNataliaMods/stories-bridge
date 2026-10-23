# Objetos e iluminação do mapa

O mapa convertido precisa de mais que DFF, TXD e colisão. O carregador de IPL
do SA usa a associação ObjectData para distinguir prédios de objetos físicos.
Sem ela, um poste com colisão correta continua sendo um prédio imóvel.

`mount-campaign-garage-doors.py` monta também os demais modelos presentes no
OBJECT.DAT original, usando os aliases do pacote. Conserva os parâmetros
físicos do LCS e traduz códigos de dano para os consumidores nativos do SA.
Os efeitos específicos de fragmentos do LCS usam destruição genérica do SA;
isso não reproduz seus fragmentos e partículas particulares. Garagens e
objetos explosivos conservam seus consumidores existentes. Um código sem
tradução é erro de conversão, nunca uma definição imóvel silenciosa.

`ps2_effects.py` lê os efeitos por modelo no GAME.DTZ e escreve o plugin de
efeitos da Geometry do SA. As posições são locais ao modelo e recebem a
transformação nativa da instância; não se duplicam efeitos nos modelos de
camadas de renderização. `map_effects.py` adapta um arquivo IMG já convertido
sem reconverter a geometria e sem modificar o inode de um rollback compartilhado.
O validador reconstrói o resultado a partir dos originais da pessoa usuária.

O modelo de semáforo recebe seu ID por `model-bindings.tsv`. A ASI intercepta
o passe de efeitos da entidade visível e chama o controlador visual do SA
somente para esse modelo. Os demais modelos seguem o alvo original do passe.
Os ciclos e as decisões do tráfego continuam nativos.

`prepare-map-feature-update.py` monta uma atualização sobre os arquivos locais
atuais, preservando personalizações de armas, HUD, scripts e veículos. O
estágio ainda precisa passar nas validações de startup e pacote antes de
qualquer instalação. Relatórios, tabelas de efeitos e assets gerados ficam
exclusivamente no workspace privado. Validação offline não comprova o
resultado visual dentro do jogo.
