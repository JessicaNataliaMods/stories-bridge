# Regras do projeto

Leia CONVENTIONS.md uma vez por contexto. O código canônico fica neste repositório `stories-bridge`; o diretório irmão `private-bridge` é exclusivamente local e nunca deve ser publicado.

- **Sem assets ou dados confidenciais no Git**, inclusive no histórico: ROMs, executáveis, SCM, DFF/TXD/COL, áudio, animações, dumps, tabelas de mapa extraídas, fixtures originais, metadados privados, credenciais e logs ficam no privado. Não seguir links locais para preparar um commit. Não usar `git add -f` ou contornar os hooks.
- Inputs em `private-bridge/originals`; referências em `references`; dados gerados em `generated`/`metadata`; builds em `build`; outputs, evidências e histórico em `workspace`. A estrutura e os links locais estão em docs/public-private-layout.md.
- Antes de commitar/publicar, executar `python tools/check-public-tree.py --staged` e revisar o diff e a lista do índice. Código com constantes de interfaces e testes autorais pode ser público; dados extraídos do jogo não podem ser disfarçados de `.inc`, código ou JSON.
- Preservar IA, comportamentos e sistemas nativos do SA; implementar particularidades LCS apenas quando necessárias ao conteúdo. Preservar scripts, missões, progressão, mapa, player/exports e compatibilidade com mods. Sem NOPs, sucesso fictício ou progressão forçada.
- Foco singleplayer. Debug só pode ser excluído mediante prova de não uso relevante. Ferry e minigames têm prioridade mínima, salvo dependência de missão demonstrada.
- Compilar somente alvos afetados e testar os caminhos alterados. Não fazer build geral/suíte inteira por padrão. Reutilizar provas aprovadas; logs extensos ficam no privado. Instalação exige startup/pacote aprovados e rollback preservado, somente no destino autorizado da configuração privada.
- Não abrir GTA, executáveis dos jogos, Euryopa ou dispositivos de áudio. Não pedir gameplay. Não iniciar builds/instalações concorrentes. Uma equipe por workspace.
- Delegação autorizada: coordenador integra e é o único a compilar/testar/instalar; até três implementadores Luna xhigh em frentes independentes e arquivos exclusivos. Sem infraestrutura ou buscas sem consumidor concreto.
- Estado local: v80 entregue; PNS confirmado funcionando pelo usuário. SAVE/LOAD completo e crash de saída continuam pendentes. Evidências e retomada detalhada ficam nos relatórios privados, não no Git. Não afirmar campanha completa por contagem de comandos.
