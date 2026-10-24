# Áudio original do LCS

A ponte não distribui áudio. O pacote da campanha (a partir da v90) lê em tempo de execução a pasta `AUDIO` da cópia de PS2 da própria pessoa usuária (Liberty City Stories, SLUS-21423).

## Instalação

Copie a pasta `AUDIO` do disco de PS2 para dentro da pasta do mod, ao lado do `LcsSaBridge.asi`:

```
modloader/stories_bridge/
  LcsSaBridge.asi
  LcsSaBridge.ini
  AUDIO/
    CUTSCENE/  MUSIC/  NEWS/  SET0/ ... SET6/
```

Não há caminho a configurar. Todas as pastas da campanha são subpastas fixas do `.asi`; o `.ini` do pacote não contém caminhos. O instalador (`tools/install-modloader-map.ps1`) preserva a pasta `AUDIO` nas atualizações.

## O que é lido

| Conteúdo | Origem | Leitura |
| --- | --- | --- |
| Efeitos e falas de missão | `SET0..SET6/SFXn_PS.SDT` + `.RAW` (67 bancos, 5661 amostras) | decodificados sob demanda |
| Notícias de rádio | `NEWS/NEWS_A..N.VB` | decodificadas ao tocar |
| Voz das cutscenes | `CUTSCENE/<NOME>.VB` | decodificada ao preparar a cena |
| Música de ambiente e final | `MUSIC/CITY.VB`, `MUSIC/FINALE.VB` | como antes, da mesma pasta |
| Rádios | `MUSIC/HEAD.VB` ... `MUSIC/LCFR.VB` | transmitidas do disco (ver abaixo) |

### Bancos de amostras

Cada `.SDT` tem registros de 12 bytes (`offset`, `tamanho`, `frequência`) sobre ADPCM do SPU2 em `.RAW`. A tabela de amostras do jogo (`eSfxSample`) segue a ordem: banco 0, banco 1, bancos 3 a 66 e, por último, o banco 2.

- Amostras com cabeçalho `VAGp`: o primeiro quadro (silencioso) é omitido; a reprodução termina no primeiro quadro com a marca de fim; se esse quadro tiver a marca de repetição, o loop começa no quadro com a marca de início de loop.
- Amostras sem cabeçalho começam no quadro de início de loop (é o caso do sino e do aviso da ponte levadiça).
- Cada amostra é decodificada como uma voz do SPU2 a partir do key-on: histórico do preditor zerado e saturação em 16 bits.
- Os registros sem cabeçalho dos bancos de motor (`SET1`) não trazem frequência utilizável no `.SDT`. O adaptador de veículos fornece explicitamente a taxa do consumidor de motor; os demais consumidores continuam recusando registros sem taxa.

O `sfx.RAW` convertido usado antes da v90 carregava o histórico do preditor de uma amostra para a seguinte e dava a volta (em vez de saturar) em estouros. Por isso o áudio atual difere dele em poucas amostras iniciais de alguns sons; os metadados (frequência, loop, duração) são idênticos.

### Cutscenes e notícias

Usam o decodificador estéreo de 32 kHz (blocos de canal de 0x2000 bytes). Cada cutscene decodificada precisa ter o mesmo SHA-256 da onda canônica registrada em `data/lcs/cutscenes/index.tsv`; se não tiver, a voz da cena é recusada.

## Rádio

Com `LcsRadio=1` (padrão), as estações do LCS substituem o áudio das rádios do SA. O SA continua cuidando da escolha de estação, da troca (retune), de ligar e desligar no veículo e do HUD; a ponte só troca o que se ouve:

- Enquanto uma estação do SA está ativa, a estação correspondente do LCS toca por uma saída própria, lida bloco a bloco de `AUDIO/MUSIC`.
- O volume é o volume de rádio do menu (`0xBA6798`, 0..64) vezes o fader de música do SA (`CAEAudioHardware+0x418`), então o menu, os fades de cutscene e de missão continuam valendo. Durante falas de missão o rádio fica em 25, como no `cMusicManager::ServiceTrack`.
- Enquanto a estação do LCS toca, o volume mestre de música do SA (`+0x410`) fica em zero para silenciar a transmissão do próprio SA. Ele volta ao valor do menu quando o rádio desliga ou quando a estação é a de músicas do usuário.
- Ao começar uma estação, o volume sobe como no `ServiceTrack`: 10 quadros em silêncio e depois +6 por quadro até 100.
- Sintonia: enquanto o SA troca de estação (passos pendentes em `0x8CB6F8+0x6C`/`+0x70`), a estação para e toca o chiado do rádio da polícia (`SFX_POLICE_RADIO_CRACKLE`, em loop, volume aleatório de 15 a 34) e até dois cliques de dial (`SFX_RADIO_DIAL_1..3`, volume 127, intervalo de 800 ms) depois de cada toque, como no `cMusicManager` do LCS. O chiado do próprio SA fica silenciado. Esses sons seguem o volume de efeitos do SA.
- Nome da estação: o SA monta a chave de texto `FEA_R%d` em `GetRadioStationName` (`0x4E8380`). As três chamadas (`0x4E9E21`, `0x55B36A`, `0x55B3AE`) passam pela ponte, que devolve a chave original do LCS (`FEA_FM0..9`, presente no texto do pacote em todos os idiomas). Com `LcsRadio=0` as chamadas não são alteradas.

Estações (ordem da tabela de streams do LCS) e a posição do SA que toca cada uma:

| LCS | Arquivo | Posição do SA |
| --- | --- | --- |
| Head Radio | `HEAD.VB` | Radio X (7) |
| Double Clef FM | `DOUBLE.VB` | K-Rose (2) |
| K-Jah | `KJAH.VB` | K-Jah West (9) |
| Rise FM | `RISE.VB` | SF-UR (5) |
| Lips 106 | `LIPS.VB` | CSR 103.9 (8) |
| Radio del Mundo | `MUNDO.VB` | Master Sounds (10) |
| MSX 98 | `MSX.VB` | Bounce FM (4) |
| Flashback 95.6 | `FLASH.VB` | K-DST (3) |
| The Liberty Jam | `LCJ.VB` | Playback FM (1) e Radio Los Santos (6) |
| LCFR | `LCFR.VB` | WCTR (11) |

O SA tem onze posições para dez estações, por isso duas posições tocam The Liberty Jam. Músicas do usuário (12), o rádio da polícia (0) e rádio desligado (13) continuam nativos.

Comandos de script:

- `SET_RADIO_CHANNEL estação, posição` sintoniza a posição do SA da estação pedida (10 = desligado) e, se a posição for informada, a estação recomeça dali.
- `GET_RADIO_CHANNEL` devolve a estação do LCS da posição ativa; -1 para o rádio da polícia; 10 para músicas do usuário e desligado.
- Sem o rádio do LCS (`LcsRadio=0`), esses comandos mantêm o comportamento anterior (rádio nativo desligado durante as falas de missão e mapeamento de gêneros).

Posição de cada estação: como em `cMusicManager::GetTrackStartPos`, a estação continua "tocando" enquanto não é ouvida, avançando pelo tempo decorrido (no máximo 270 s por verificação) e voltando ao início no fim do arquivo. As posições iniciais de jogo novo vêm da tabela `NewGameRadioTimers` da referência reLCS.

Limitações atuais:

- As posições das estações não são gravadas no save; recomeçam das posições de jogo novo ao carregar.
- Enquanto uma estação do LCS toca, a música de ambiente `CITY` do script também fica sem som (no LCS rádio e ambiente dividem o mesmo stream).

## Sons de armas

Com `LcsWeaponSounds=1` (padrão), o tiro e a recarga de armas de fogo tocam a amostra do LCS da arma original que foi convertida naquele tipo do SA; com `0` o SA toca o som dele e nenhum gancho é instalado. Falta de áudio no pacote, perfil do exe diferente ou falta de voz livre também mantêm o som do SA (o log registra o motivo).

O SA decide o som por tipo de arma em `CAEWeaponAudioEntity::WeaponFire` (`0x504F80`) e `WeaponReload` (`0x503690`), ambos `thiscall(tipo, ped, evento)`. A ponte troca as quatro chamadas existentes (`0x4E6A3D`/`0x4E6A5B` na `CAEPedWeaponAudioEntity::AddAudioEvent`, que serve jogador e NPCs, e `0x4DFAC6`/`0x4DFAD6` no despachante de armas de veículo). O perfil confere os bytes das duas funções e o destino das quatro chamadas antes de instalar.

**Atribuição:** o tipo do SA identifica a arma do LCS porque a conversão dá um tipo próprio a cada arma (docs/weapons.md; a mesma tabela `WEAPON_TYPES` de `tools/weapon_slots.py` e `native.cpp`, que `tests/weapon_slot_contracts.py` compara com `include/lcs/weapon_audio.hpp`). `tests/weapon_audio_tests.cpp` prova, com a pasta AUDIO real, que cada um dos 14 tipos tem exatamente uma arma dona, que não há segundo dono não documentado e que as amostras decodificam como áudio de verdade.

| Tipo do SA | Arma do LCS | Tiro (índice SFX, taxa) |
| --- | --- | --- |
| 22 pistola | Colt45 | `COLT45` L/R, taxa da amostra |
| 24 Desert Eagle | Python | `PYTHON` L/R |
| 25 escopeta | Shotgun | `SHOTGUN` L/R |
| 26 cano serrado | Stubby | `SHOTGUN` L/R |
| 27 SPAS-12 | Spas12 | `SPAS12` L/R |
| 28 Micro Uzi | Uzi | `UZI` L/R |
| 23 pistola silenciada | Ingram silenciada | `TEC` L/R a 34000 Hz |
| 29 MP5 | MP5 | `MP5` L/R |
| 30 AK-47 | Ruger | `RUGER` L/R |
| 31 M4 | M4 | `RUGER` L/R a 43150 Hz |
| 32 Tec-9 | Tec9 | `TEC` L/R a 17000 Hz |
| 33 rifle do campo | M60 | `M60` L/R |
| 34 sniper | SniperRifle | `SNIPER` L/R a 25472 Hz |
| 35 lança-foguetes | RocketLauncher | `ROCKET` (mono) |

As regras são as de `cAudioManager::ProcessPedOneShots` (`SOUND_WEAPON_SHOT_FIRED`/`SOUND_WEAPON_RELOAD`): alcance de 120 m (recarga 30 m), volume do evento por arma (`table[i]%m+b`), `ComputeVolume` com queda quadrática depois de 1/5 do alcance, variação de taxa (`RandomDisplacement`) e, a menos de 20% do alcance, o par L/R tocado em 2D (esquerdo no canal esquerdo, direito no direito); mais longe só a amostra esquerda, posicionada. O par vira uma única voz estéreo.

O lança-chamas (37) e a minigun (38) têm um serviço próprio de loops, descrito abaixo. Spray, extintor, câmera, armas brancas, granadas e eco do tiro continuam nos respectivos consumidores nativos. O laser scope usa o tipo do sniper e portanto o som do sniper. O lança-foguetes também é "estéreo" no original, mas `SFX_ROCKET_LEFT` é seguido por `SFX_ROCKET_RELOAD` (um espaço reservado mudo de 728 bytes na mídia retail), então o disparo toca mono e a recarga do lança-foguetes é silenciosa como no original.

Tiros e recargas só suprimem o áudio nativo depois de o mixer aceitar o PCM. Falha de dispositivo ou falta de espaço mantém o som do SA.

### Mixer de efeitos e armas contínuas

Veículos, tiros, interface e os novos adaptadores usam uma saída PCM estéreo compartilhada. Ganho, panorâmica e mudança de frequência são aplicados por voz em software; atualizar um loop mantém sua posição, e o limitador contém picos de sobreposição. A saída usa blocos de 10 ms e não abre um dispositivo para cada evento. O volume de efeitos do SA é aplicado uma única vez. Os testes exercitam o núcleo PCM sem abrir dispositivos.

Os eventos de disparo da minigun mantêm `MINIGUN_FIRE_LEFT` e reproduzem `MINIGUN_STOP` ao terminar. O evento de giro sem munição mantém o consumidor do SA. O lança-chamas mantém `FLAMETHROWER_LEFT/RIGHT` e seu início de chama; na mídia PS2 o canal direito é um único quadro silencioso, preservado explicitamente. O pedido dura três quadros de áudio na minigun e seis no lança-chamas, com tolerância de 50 ms para o agendamento SA. Pausa e encerramento liberam apenas as vozes possuídas pela família. A variação de frequência da chama pelo índice de entidade LCS não é reproduzida: esse índice não existe no consumidor SA.

## Sons de veículos

Quatro chaves independentes (padrão `1`; `0` mantém o som do SA daquele tipo e não instala o gancho): `LcsVehicleHorns`, `LcsVehicleSirens`, `LcsVehicleDoors`, `LcsVehicleEngines`. Só os 87 veículos convertidos são tocados; qualquer outro modelo, som sem voz livre ou erro mantém o som do SA.

**Atribuição ao modelo certo.** O som segue o veículo *original* que o slot do SA representa, não o modelo do SA: o slot vem de `data/lcs/native-equivalents.tsv` (a mesma ligação do conversor) e a linha de som é `aVehicleSettings[modelo LCS − 130]` do reLCS, copiada pelo CMake sem transcrição (87 linhas e 24 pares de motor, o configure falha se mudarem). `tests/vehicle_audio_tests.cpp` confere: 87 slots distintos, uma linha por veículo, e provas independentes de que a ordem das linhas é a dos modelos (ambulância com `AMBULANCE_SI`, polícia/enforcer/FBI/bombeiro com `POLICE_SIREN_SLOW`, Mr. Whoopee com a música do sorveteiro, ônibus com porta de ônibus, Faggio/Pizzaboy com o motor de scooter, Angel com o V-twin, PCJ-600 com a moto esportiva, Stinger com o motor Porsche). O teste ainda decodifica todas as amostras usadas e imprime a tabela completa "veículo LCS → slot SA → buzina, sirene, porta, banco de motor".

| Chave | Gancho no SA | O que toca |
| --- | --- | --- |
| `LcsVehicleHorns` | `PlayHornOrSiren` (0x4F99D0, chamada em 0x500366) | buzina em loop da linha (amostra e taxa), 40 m, volume 80; o alarme usa a mesma buzina |
| `LcsVehicleSirens` | idem | sirene da linha (110 m, volume 80); com buzina apertada, `SIREN_FAST` (exceto bombeiro e Mr. Whoopee; FBI a 12668 Hz) |
| `LcsVehicleDoors` | `AddAudioEvent` (0x4F6420, desvio de entrada) eventos 0x4E..0x59 | abrir/fechar porta, capô e porta-malas: `OLD/NEW/TRUCK_DOOR` ou `AIR_BRAKES` (ônibus), 50 m, volumes 117..126/122..126 como no original |
| `LcsVehicleEngines` | `ProcessPlayerVehicleEngine` (0x4FBB10) e `ProcessDummyVehicleEngine` (0x501480), 4 chamadas; `AddAudioEvent` 0x67 | motor em loop (marcha lenta com pedal < 0,02, senão a amostra de aceleração), taxa 14000..27000 Hz, volume 75..90 (120 no jogador), 50 m; partida `CAR_STARTER` |

O perfil confere os bytes das funções do SA e o destino de cada chamada antes de instalar; o desvio de `AddAudioEvent` só entra se o prólogo for exatamente `push -1; jmp 0x40727C`. Posições e velocidades vêm do próprio `tVehicleParams` do SA (velocidade, `m_MaxVelocity`, marcha pela tabela de câmbio, pedal suavizado `m_GasPedalAudioRevs`), com os deslocamentos conferidos no exe.

O motor usa o modelo de motor de IA do LCS também para o jogador, sem a troca de marcha detalhada do `ProcessPlayersVehicleEngine`. Marcha lenta e aceleração ficam no mesmo mixer; a posse só muda depois de ambos os loops estarem disponíveis. O banco com marcha lenta silenciosa (RC e carrinho de golfe) mantém esse silêncio. Aviões, helicópteros e barcos ainda usam o motor do SA.

## Sons de interface

Chave `LcsInterfaceSounds` (padrão `1`; `0` mantém os sons de menu, pickups e HUD do SA e não instala nada). O SA envia todo som de interface por `CAudioEngine::ReportFrontendAudioEvent` (0x506EA0), que repassa a `CAEFrontendAudioEntity::AddAudioEvent`. A ponte redireciona as 90 chamadas dessa função (lista conferida por decodificação do exe; a função em si não é alterada) e toca a amostra que o `cAudioManager::ProcessFrontEnd` do LCS toca para o evento equivalente, em uma saída própria, a volume 127 vezes o volume de efeitos do menu.

| Evento do SA | Som do LCS |
| --- | --- |
| selecionar / voltar / destacar opção | `FE_SELECT` / `FE_BACK` / `FE_HIGHLIGHT` |
| erro de menu | `FE_ERROR` (esquerdo e direito) |
| início do menu, info, aviso na tela | `FE_INFO` (esquerdo e direito) |
| dinheiro, vida, adrenalina, colete, repintura | `MONEY` (esquerdo e direito) |
| arma pega/comprada, bomba colocada | `WEAPON` esquerdo, direito e centro |
| garagem recusa (sem dinheiro, carro quente, já com bomba) | `WEAPON` a 5382 Hz |
| coletável, droga, parte de missão, contagem 3-2-1 | `PART_MISSION_COMPLETE` esquerdo, direito e centro |
| largada "go" | `GO` |
| tique do cronômetro | `TIMER` |
| clique do rádio ligar / desligar | `RADIO_CLICK` a 32000 / 28509 Hz |
| sniper / foguete sem poder atirar | `ERROR_FIRE_RIFLE` / `ERROR_FIRE_ROCKET_LAUNCHER` |
| bala passando | `BULLET_PASS_1/2` com pan 20 ou 107 |
| teste de áudio | `FE_NOISE_BURST_1..3` |

Pickups e jingles de missão que chegam a menos de 100 ms do anterior são descartados, como o `ProcessFrontEnd` faz com cinco quadros. Continuam com o SA: música de carregamento, scanner da polícia, amplificador, frenesi, sintonia do rádio (já tratada pela rádio do LCS) e travamento de míssil. `tests/interface_audio_tests.cpp` confere os 35 eventos mapeados e que todas as amostras decodificam.

## Opções do `.ini`

| Chave | Padrão | Efeito |
| --- | --- | --- |
| `LcsRadio` | `1` | `0` mantém as rádios do SA |
| `LcsWeaponSounds` | `1` | `0` mantém o tiro e a recarga de armas do SA |
| `LcsVehicleHorns` | `1` | `0` mantém a buzina do SA |
| `LcsVehicleSirens` | `1` | `0` mantém as sirenes do SA |
| `LcsVehicleDoors` | `1` | `0` mantém os sons de portas do SA |
| `LcsVehicleEngines` | `1` | `0` mantém o motor e a partida do SA |
| `LcsInterfaceSounds` | `1` | `0` mantém os sons de menu, pickups e HUD do SA |
| `LcsSilentLoading` | `1` | filtra somente o início da música de carregamento do SA; `0` restaura essa música |
| `LcsPedSounds` | `1` | substitui contextos de voz comprovados; `0` mantém as vozes SA |
| `LcsVehicleSurfaceSounds` | `1` | ruído de pista, derrapagem e ré nos veículos convertidos; `0` mantém o SA |

### Vozes de pedestres

O gancho de `CPed::Say` chama primeiro `AddSayEvent` e preserva seu retorno. Só um evento nativo aceito, imediato e sem as opções frontend/force-audible pode receber a voz LCS. O agendamento, a seleção de prioridade e os callbacks nativos continuam ativos. Quando o PCM substituto é aceito, apenas o volume da voz nativa agendada é silenciado antes de seu serviço; uma recusa restaura o volume original.

Dor, morte, fogo e pânico usam os grupos masculino/feminino do LCS; o jogador usa Toni e o fallback genérico do original. A morte de Toni é silêncio explícito no original. O alcance é de 40 m, com volume 127 em linha de visão e 31 atrás de paredes. Os samples desses grupos são pré-carregados em uma fila própria; um sample indisponível mantém o SA. A atribuição por gênero/tipo é uma adaptação ao SA, não uma reconstrução de vozes individuais por modelo. Contextos ainda sem correspondência comprovada mantêm o som nativo.

As identidades numéricas de dor, fogo e pânico são conferidas com o [enum público do gta-reversed](https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Audio/Enums/PedSpeechContexts.h), separadamente dos testes de bytes do consumidor SA. Testes por nomes de constantes, sozinhos, não provam essa correspondência.

## Validação

- `tests/ps2_audio_source_tests.cpp`: compara os metadados das 5661 amostras com o índice convertido, decodifica todos os nomes de áudio de missão, as 14 notícias (idênticas às ondas preparadas) e confere o hash das 21 cutscenes.
- `tests/lcs_radio_tests.cpp`: mapeamento SA↔LCS, regra de posição do retail, cálculo de bloco e decodificação do stream lido do disco.

Os dois rodam na validação de inicialização do pacote (`tools/validate-campaign-startup.py`).

### Escopo e diálogos de missão

Impactos de bala/objetos em superfícies, chuva, trovões e demais efeitos de ambiente continuam no SA, por escolha do usuário. Não há ganchos novos nesses consumidores.

Cada fala de missão/NEWS começa com volume e pan explícitos antes de publicar PCM. Um novo diálogo 2D não herda o ganho de uma fala distante. Falas posicionadas já terminadas deixam de emitir atualizações espaciais; o estado de conclusão observado pelo script continua vindo da reprodução real.

O ruído de pista, piso molhado, derrapagem e marcha à ré usam os pedidos e campos do veículo SA. Os processadores nativos continuam executando e mantendo seus sons; o ganho do duplicado só é reduzido quando o mixer aceita a substituição. Amostras indisponíveis, superfície desconhecida ou modelo ambíguo mantêm o SA. As vozes próprias expiram quando o veículo deixa de ser servido e são liberadas na pausa.

As falas contextuais comprovadas de policiais, SWAT/FBI, paramédicos e personagens identificados usam resolução pelo nome equivalente exato. Perseguição/bump de policial exigem os predicados de estado/wanted legíveis. A seleção entre bancos usa identidade estável do ator SA, uma adaptação ao índice de entidade do LCS.

Capô e porta-malas mantêm o consumidor nativo do SA: a movimentação desses painéis em danos não é convertida em um pedido de porta de passageiro do LCS.

### Saida PCM e continuidade

O backend das falas de missao, NEWS, CITY e sons de script aplica ganho, pan e velocidade no PCM de cada voz. Nao usa os controles de volume/frequencia do dispositivo WinMM. Tres blocos de 50 ms sao preparados com a saida pausada e permanecem imutaveis enquanto publicados. O fim de uma fala exige que o driver devolva todos os blocos finais; a leitura antecipada do decoder nao conclui comandos de missao.

O mixer de efeitos mantem doze blocos de 10 ms (120 ms), em vez da fila anterior de apenas 30 ms. A saida so inicia apos o preenchimento completo e os blocos devolvidos sao repostos na ordem de submissao. Essa reserva pode acrescentar ate 120 ms de latencia. O teste do Runtime real, com WinMM simulado, mantem a sequencia PCM sem underruns em quatro atrasos de callback de 60 ms. Isso valida a fila offline; nao e observacao de reproducao dentro do jogo.

Os testes de isolamento verificam os bytes PCM recebidos pelo driver simulado, a independencia de ganho entre vozes, velocidade/loops, pausa, conclusao real e retencao segura de buffers quando o driver recusa liberacao. Nenhum teste abre dispositivo de audio.

### Encerramento de buzinas e sirenes

O gancho chama `PlayHornOrSiren` em todos os frames, inclusive quando os dois canais foram assumidos pelo LCS. Cada canal assumido recebe zero no consumidor nativo, permitindo encerrar a voz SA iniciada durante o carregamento do sample; pular a chamada deixava esse fallback tocando junto do LCS e sem receber o comando de parar. `vehicle_horn_dispatch_tests.cpp` cobre a transicao de fallback para LCS, cancelamento e posse independente dos canais.

Cada pedido de loop tem uma revisao unica entre recriacoes do slot. O worker revalida essa revisao e o pedido ativo depois de decodificar, usando o volume/frequencia/pan mais recentes. Cancelamento e publicacao sao serializados junto ao estado do mixer; um pedido atrasado nao pode recriar um loop cancelado nem apagar uma geracao mais nova.

### Tiros ambientes distantes do SA

Na campanha ativa, a chamada dedicada de `CAudioEngine::Service` para `ServiceAmbientGunFire` e filtrada. Os eventos de armas, NPCs atirando e recargas continuam nos consumidores existentes. O destino do CALL e validado antes de instalar o gancho; fora da campanha, o gerador nativo continua sendo chamado. O teste isolado confere o roteamento e le o perfil do executavel somente como dados.

### Motor: estado nativo e pitch

O processador de motor SA continua executando em todos os frames antes de ler marcha/rotacao para os loops LCS. A marcha vem do campo real do veiculo, em vez de ser inferida pela velocidade. O contato das rodas usa os campos nativos de carro/moto. As frequencias absolutas dos loops continuam na curva LCS adaptada; esta correcao nao abaixa arbitrariamente o pitch dos arquivos.

Quando o LCS aceita o motor, apenas os slots de motor nativos correspondentes recebem ganho silencioso; buzina, sirene, danos e outros efeitos ficam fora desse filtro. Antes do proximo processamento SA, o ganho anterior e restaurado somente se ponteiro, dono, identidade e sentinela ainda correspondem. Falha no filtro cancela os loops LCS e mantem o processamento SA; troca de mundo limpa o cache. O teste isolado exercita o codigo real sobre objetos simulados e le os bytes de perfil em memoria nao executavel, sem jogo ou dispositivo de audio.
