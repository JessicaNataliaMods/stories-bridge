# Saves e arquivos de usuário

A ASI usa uma pasta exclusiva para saves, sidecars `.lcs`, configurações
`gta_sa.set`, Gallery e User Tracks. As chaves ficam em `[LCS]` no
`LcsSaBridge.ini`, ao lado da ASI:

```ini
PortableUserFiles=1
UserFilesFolder=GTA Liberty City Stories User Files
```

`PortableUserFiles=1` é o padrão: cria a pasta indicada junto de **gta_sa.exe**,
independentemente da localização da ASI ou do diretório de trabalho.
`PortableUserFiles=0` usa a pasta Documentos resolvida pelo Windows, acrescentando
o mesmo nome exclusivo. Nenhum modo usa `GTA San Andreas User Files`.

`UserFilesFolder` aceita um único nome de pasta, sem caminhos absolutos,
separadores, `..`, nomes de dispositivo ou caracteres proibidos pelo Windows.
Alterar qualquer uma das opções exige reiniciar o jogo. As duas localizações
são independentes: alternar o modo não transfere nem apaga arquivos existentes.
Ao transferir saves manualmente, manter cada `GTASAsf*.b` com seu `.b.lcs`.

O diretório precisa ser gravável e representável pelas APIs de arquivos do SA.
Uma configuração inválida, conflito no hook ou falha ao preparar a pasta
interrompe essa inicialização com erro; não escolhe silenciosamente a pasta
compartilhada do SA. Não cria junctions nem apaga saves anteriores.

A implementação é própria para SA e usa como referência a técnica de
[portablegta.cpp, do GTAmodding/miscmods](https://github.com/GTAmodding/miscmods/blob/master/portablegta.cpp):
redirecionar o resolvedor nativo e os buffers de Gallery/User Tracks antes da
inicialização do RenderWare. O registro antecipado só altera a chamada de evento;
a resolução e a criação de diretórios ocorrem no callback, fora do loader lock.
