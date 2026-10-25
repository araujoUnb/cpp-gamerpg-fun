# RPG de Tabuleiro Tático (projeto em C++)

> **Escopo atual:** começar com **uma única história** (uma campanha curta de 3 cenários),
> que é a primeira região do mapa do mundo. A estrutura de pastas por região continua
> valendo, para que outras regiões e histórias possam ser adicionadas depois sem mudar o código.

## Visão geral

Jogo de RPG de tabuleiro digital, para um jogador controlando um grupo de heróis, com histórias independentes em formato de campanha. Combina três ideias:

1. **Ficha de personagem inspirada no D&D 5e.** Seis atributos, classes, níveis e o d20 como resolução de testes.
2. **Combate por cartas com duas metades**, inspirado no estilo de jogos como Gloomhaven. As cartas do personagem são ao mesmo tempo ações e recurso de resistência.
3. **Histórias ramificadas** escritas fora do código, com escolhas, testes de atributo e consequências.

Não usar nomes, textos, arte, classes ou monstros protegidos de Gloomhaven ou de D&D. Somente as mecânicas gerais servem de inspiração. A exceção é o que está no SRD: as 12 classes básicas, perícias, atributos e tabelas de progressão do SRD podem ser usados. Subclasses, monstros e textos fora do SRD continuam proibidos, e as cartas são sempre criação original. Regras do D&D podem vir do SRD 5.x, publicado pela Wizards of the Coast sob licença Creative Commons CC-BY-4.0, com o crédito exigido incluído na tela de créditos e em `LICENSES/SRD.md`.

## Stack

- C++20, CMake, build multiplataforma (Linux, Windows, macOS)
- **Núcleo de regras sem dependência gráfica**, em biblioteca estática `core/`. Tudo que é regra mora aqui e é testável sem abrir janela.
- **Núcleo reutilizável.** O núcleo deve servir para outros cenários e outros RPGs. Os números de regra (faixa de atributos, nível máximo, tabela de proficiência, tamanho de mão, perícias) ficam em `data/rules/ruleset.json`, não no código. Entradas inválidas geram erro claro, com o arquivo e o campo, e nunca comportamento indefinido.
- **Frontend em Godot 4**, em `godot/`, ligado ao núcleo via GDExtension (godot-cpp). A interface, as animações e os efeitos ficam em GDScript e em cenas do Godot. As regras ficam só no C++.
- **Ponte GDExtension** em `bindings/`: uma camada fina que expõe o núcleo ao Godot. Ela não contém regras.
- O núcleo não pode depender do Godot nem da ponte. Ele continua compilando e passando nos testes sozinho, sem o Godot instalado.
- Dados do jogo em JSON (nlohmann/json): classes, cartas, monstros, cenários
- Histórias em ink, com o runtime inkcpp, em fase posterior
- Testes com doctest

### Direção visual

O visual do jogo é prioridade: deve ser bonito, não apenas funcional. Mão de cartas com movimento (inclinar ao passar o mouse, arrastar para jogar), dados animados, números de dano flutuantes, partículas nas magias, iluminação 2D no tabuleiro e uma interface com tema próprio e consistente. A cor e o retrato de cada classe vêm dos dados (`visual` no JSON da classe).
- Dependências via CMake FetchContent

## Estrutura de pastas

```
core/
  dice/        rolagem d20, vantagem e desvantagem, gerador com semente
  character/   atributos, modificadores, classes, níveis, ficha
  cards/       cartas, baralho, mão, descarte, cartas perdidas
  combat/      grade hexagonal, iniciativa, turnos, efeitos, condições
  monsters/    baralho de ações dos monstros e perfis do mestre do jogo
  scenario/    mapa, objetivos, gatilhos
  campaign/    progresso entre cenários, recompensas, flags de história
  data/        carregamento e validação dos JSON
bindings/      GDExtension: expõe o núcleo ao Godot, sem regras
cli/           executável de terminal (etapa 4): joga e mede um cenário
tools/         ferramentas fora do jogo: treino do mestre do jogo
godot/         projeto Godot: cenas, GDScript, temas, shaders, arte
data/          JSON de conteúdo
stories/       arquivos .ink
tests/
```

## Regras do jogo

### Ficha

- Atributos: FOR, DES, CON, INT, SAB, CAR, valores de 8 a 20 (definidos no ruleset)
- Modificador = (valor - 10) / 2, arredondando para baixo
- Bônus de proficiência por nível, como no D&D 5e, em tabela no ruleset
- Pontos de vida máximos = base da classe + PV por nível × (nível − 1) + modificador de CON × nível, nunca abaixo do nível. Com os valores do 5e, a base é o dado de vida cheio e o PV por nível é a média do dado.
- Classe de Armadura = base + mod. de DES (com limite opcional) + mod. de um atributo extra opcional, o que cobre armaduras e a defesa sem armadura de Bárbaro e Monge
- Testes de resistência: a classe define dois atributos proficientes
- 18 perícias do SRD. A classe define quantas o jogador escolhe e de qual lista.
- Nível máximo por campanha: 9
- Cada nível libera novas cartas da classe, e o jogador escolhe quais entram no baralho

### Testes

- Rolagem: d20 + modificador do atributo + proficiência, se aplicável. Teste de atributo puro não soma proficiência; teste de resistência e perícia somam quando o herói é proficiente.
- Sucesso se o total for maior ou igual à CD ou à Classe de Armadura do alvo
- Vantagem: rolar dois d20 e ficar com o maior. Desvantagem: ficar com o menor. As duas juntas se anulam.
- 20 natural é crítico e dobra os dados de dano. 1 natural é falha automática.
- Toda rolagem usa um gerador com semente, para permitir replays e testes determinísticos

### Cartas

Cada carta tem:

- **Iniciativa**, número de 1 a 99. Menor age primeiro.
- **Metade de cima** com uma ação, geralmente ataque ou magia
- **Metade de baixo** com outra ação, geralmente movimento ou utilidade
- Indicação se cada metade é **perdível**. Uma metade perdível, quando usada, manda a carta para a pilha de perdidas em vez do descarte.

Turno de cada herói:

1. No início da rodada, cada herói escolhe **duas cartas** da mão, em segredo
2. Uma das duas define a iniciativa do herói na rodada
3. Na sua vez, o herói usa a **metade de cima de uma** carta e a **metade de baixo da outra**
4. Qualquer metade pode ser trocada por uma ação básica: ataque 2 na metade de cima ou movimento 2 na metade de baixo
5. As cartas vão para o descarte, ou para as perdidas se a metade usada for perdível

Ataques feitos por cartas usam o d20 contra a CA do alvo. O valor da carta é o dano base, somado ao modificador do atributo da classe.

Detalhes definidos na etapa 3:

- A ordem das duas cartas escolhidas importa: a primeira define a iniciativa.
- Trocar uma metade por ação básica nunca perde a carta, mesmo que aquela metade seja perdível.
- Cada carta é uma lista de ações por metade, com os tipos `move`, `attack`, `heal` e `condition`, e cada tipo aceita só os campos que fazem sentido para ele. O formato está em `data/cards/*.json`.
- O baralho do herói tem exatamente o tamanho da mão da classe, com cartas da própria classe e de nível até o nível do herói. Ele fica guardado na ficha.
- Os números das cartas (ação básica, mínimo de cartas no descarte para descansar, cura e iniciativa do descanso longo) ficam em `ruleset.json`.

### Descanso e exaustão

- O tamanho da mão é definido pela classe, entre 8 e 12 cartas, e é a resistência do herói
- **Descanso curto:** no fim da rodada, embaralha o descarte, perde uma carta aleatória e recupera o resto
- **Descanso longo:** usa a rodada inteira, escolhe qual carta perder, recupera o descarte e cura 2 PV
- Um herói fica **exausto** e sai do cenário se não tiver cartas suficientes para jogar duas ou se chegar a 0 PV, depois de falhar nos testes de morte
- Os dois descansos exigem pelo menos 2 cartas no descarte. Com menos de 2 cartas na mão, o herói é obrigado a descansar. Se nem isso for possível, ele fica exausto.
- Isso cria a pressão de tempo do cenário, porque cada carta perdida aproxima a exaustão

### Motor de combate

- O combate é uma máquina de estados (`core/combat/combat.hpp`). Ela avança sozinha (turnos de monstro, rolagens, fim de rodada) até precisar de uma **decisão** de herói, com a lista explícita de opções válidas. Decisões com uma única opção são tomadas automaticamente.
- Tudo o que acontece vira um **evento** estruturado, em ordem. O frontend anima os eventos e os testes os verificam.
- O mesmo motor serve ao terminal, ao Godot e ao treino de IA. A interface "decisão → opções → escolher" é o ambiente de aprendizado por reforço.
- O estado completo, inclusive um turno em andamento, é salvo e carregado em JSON. Carregar e repetir as mesmas escolhas reproduz a batalha exatamente.
- Iniciativa: menor primeiro; em empate, heróis antes de monstros, depois a segunda carta do herói.
- Crítico de ataque: dobra o valor da carta, não o modificador.
- Herói a 0 PV fica caído. No fim de cada rodada ele faz um teste contra a morte: CD 10, 20 natural volta com 1 PV e 1 natural conta como duas falhas. Com 3 sucessos fica estável, com 3 falhas morre. Uma cura o traz de volta. Os números ficam em `ruleset.json`.
- Derrota quando nenhum herói está de pé; vitória quando todos os monstros caem.
- Uma condição dura N turnos da criatura afetada. Se foi aplicada no próprio turno dela, esse turno não conta.
- `AutoPlayer` é um jogador heurístico para os heróis. É usado no modo automático, no teste de balanceamento e como referência de comparação para a IA treinada.

### Monstros

- Cada tipo de monstro tem um pequeno baralho de ações. A cada rodada uma carta é virada, com iniciativa e ação.
- Os monstros são jogados por um **mestre do jogo**, escolhido ao criar o combate e guardado no save. Os perfis ficam em `data/game_masters/`. Em qualquer perfil, o monstro respeita a ação da carta: um monstro passa por outros monstros, mas não termina o movimento em casa ocupada, e evita armadilhas. Empurrar e puxar ainda não são usados por monstros.
- **Mestre clássico** (`classic`): andar até o herói mais próximo que consegue atacar e atacar o mais próximo. "Mais próximo" é medido em passos reais, contornando obstáculos e heróis.
- **Mestre tático** (`tactical`, o padrão do terminal): para cada casa onde o movimento pode terminar, soma peso × característica: dano esperado do ataque seguinte (chance exata no d20 contra a CA), chance de derrubar o herói, herói ferido, herói que cura, monstros já ao lado do alvo, heróis ao lado da casa final (pesos separados para quem ataca corpo a corpo e à distância), passos que faltam até o herói mais próximo quando ninguém está ao alcance e movimento gasto. O ataque e as condições vão nos alvos de maior pontuação. As características são inteiras, com 400 valendo 1, para que a decisão seja exata em qualquer plataforma.
- Os pesos do mestre tático são treinados fora do jogo (`tools/gm_train.cpp`, método da entropia cruzada) contra o `AutoPlayer`, em todos os cenários e pares de classes, e gravados em JSON.
- Estatísticas em JSON: CA, PV, atributos, dano, imunidades

### Dificuldade

O jogador escolhe a dificuldade ao começar a campanha. Em toda dificuldade os heróis precisam ter uma chance real de vencer: o mestre joga para desafiar, não para tornar a vitória impossível.

- Os níveis ficam em `data/difficulties/`: **fácil**, **normal** e **difícil**. Cada um define o perfil do mestre do jogo e os números que o calibram: a chance, por turno de monstro, de o monstro agir pela regra clássica (um "deslize" do mestre), e uma porcentagem dos PV dos monstros.
- Os níveis são calibrados com o treino fora do jogo, contra o `AutoPlayer`, em todos os cenários e pares de classes. Alvos de vitória dos heróis, na média: fácil por volta de 75%, normal por volta de 60%, difícil por volta de 45%.
- **Piso de vitória:** em cada nível, nenhum confronto testado (cenário × par de classes) pode ficar abaixo de um piso de vitórias do `AutoPlayer`: 50% no fácil, 30% no normal e 15% no difícil. Um confronto abaixo do piso é corrigido nos dados (cenário, monstros ou calibração do nível), nunca com regra escondida no código. A ferramenta de treino relata os confrontos abaixo do piso.
- **Mestre honesto:** o mestre nunca usa informação que os heróis não têm na mesa: as cartas escolhidas em segredo, a ordem dos baralhos e os dados futuros. Ele vê o tabuleiro, os PV, as condições e as cartas já reveladas.
- A dificuldade fica no save, e trocar de dificuldade só vale a partir do próximo cenário.

### Condições

Começar com poucas: envenenado, atordoado, imobilizado, fortalecido, protegido. Cada uma com duração em rodadas e efeito declarado em JSON. Os efeitos descritos hoje em `pt-BR.json` (envenenado: +1 de dano recebido; atordoado: perde o turno; imobilizado: não se move; fortalecido: ataca com vantagem; protegido: −1 de dano por ataque) são provisórios até a etapa 4.

### Tabuleiro

- Grade hexagonal com coordenadas axiais
- Obstáculos, terreno difícil, armadilhas e portas
- Linha de visão e alcance calculados no núcleo
- Pathfinding A* para heróis e monstros

Detalhes definidos na etapa 5:

- Terreno difícil custa 2 de movimento. Salto ignora o custo do terreno.
- Porta fechada bloqueia a visão. Só herói entra nela, e ela abre na hora. Monstros não abrem portas.
- Armadilha dispara uma vez, quando a criatura entra na casa (no salto, só se for a casa de chegada), e depois some. Empurrar pode jogar um monstro numa armadilha, mas não através de uma porta.
- O herói escolhe o destino, e o caminho percorrido é o mais barato. Entre caminhos de mesmo custo, vale o que passa por menos armadilhas. Monstros evitam armadilhas.
- Ataque, cura e condição com alcance maior que 1 exigem linha de visão, traçada entre os centros das casas. Linhas que passam rente às arestas são livres se um dos lados estiver livre, e a visão é sempre simétrica.
- O evento de movimento traz o caminho casa a casa, para o frontend animar.

### Equipamento

O equipamento se **ganha dentro da história** e se **equipa fora do combate**.

- **Onde se ganha:** baús e recompensas nos cenários, escolhas e recompensas na história em ink ("aceitar a lança do ferreiro?") e a loja da região, com o ouro da campanha.
- **Onde se equipa:** no acampamento, entre um cenário e outro. Durante o combate o equipamento não muda.
- Os itens ficam em `data/items/` (e nos itens próprios de cada região), com nome e descrição em `lang/`. Nenhum valor de item fica no código.
- **Espaços por herói:** uma arma, uma armadura, um escudo e até dois consumíveis. O inventário da campanha é do grupo; o que está equipado fica na ficha.
- **Arma:** muda o ataque básico (valor, alcance, condição) e pode trazer até 2 cartas de arma. As cartas de arma entram no baralho no lugar de cartas da classe, e o baralho continua com o tamanho da mão da classe.
- **Armadura e escudo:** a armadura troca a base da CA e o limite de DES da classe; o escudo soma à CA. É a fórmula da ficha que já existe, com os números vindos do item.
- **Consumível:** usado no combate como a metade de uma carta (por exemplo, poção: cura 3 na metade de baixo) e gasto depois do uso.
- **Proficiência:** a classe diz que tipos de arma e armadura usa sem penalidade (listas do SRD). Fora da proficiência, o ataque com a arma tem desvantagem, e a armadura impede descanso longo.

### Mapa do mundo e regiões

- O jogo tem um mapa do mundo dividido em **regiões**. Cada região é uma campanha com história, cenários, monstros, itens e loja próprios: a floresta com arcos e lanças, a montanha com machados e armaduras pesadas, e assim por diante.
- Cada região fica em sua pasta dentro de `stories/` e `data/regions/`. O mapa do mundo (`data/world.json`) diz onde cada região fica, como se chega a ela e que flag a libera.
- O grupo leva entre as regiões o nível, o equipamento, o inventário e o ouro. As flags guardam as escolhas feitas em cada história.
- Por enquanto existe só uma região: a campanha de 3 cenários. O mapa do mundo já mostra as outras como fechadas.

### Campanha e histórias

- Uma campanha é a história de uma região: uma sequência de cenários ligados por trechos de história em ink
- O ink pode pedir testes de atributo, que o núcleo resolve e devolve o resultado para a história
- O ink pode dar itens, ouro e flags, e abrir a loja
- Flags de campanha guardam escolhas e liberam ou bloqueiam cenários e regiões
- Cada campanha fica em sua pasta dentro de `stories/` e `data/`. Por enquanto existe só uma.

## Ordem de desenvolvimento

1. **Dados e testes.** `dice/` com semente, vantagem, crítico, e testes cobrindo tudo
2. **Ficha.** Atributos, modificadores, classes vindas de JSON
3. **Cartas.** Mão, descarte, perdidas, escolha de duas cartas, descansos, exaustão
4. **Combate em modo texto.** Um cenário pequeno rodando no terminal com dois heróis e três monstros, para validar as regras antes de qualquer gráfico
5. **Tabuleiro hexagonal.** Movimento, alcance, linha de visão, A*
6. **Frontend Godot.** Ponte GDExtension, tabuleiro hexagonal, peças, mão de cartas com animação, rolagens de dado animadas, efeitos de partícula e interface com tema próprio
7. **Campanha.** Encadear cenários, progressão de nível entre eles, equipamento, acampamento e loja, mapa do mundo com a primeira região aberta
8. **Histórias em ink.** Integração do inkcpp e testes de atributo vindos da história

Não pular para o frontend antes da etapa 4 funcionar no terminal.

## Conteúdo inicial

- As 12 classes do SRD: Bárbaro, Bardo, Clérigo, Druida, Guerreiro, Monge, Paladino, Patrulheiro, Ladino, Feiticeiro, Bruxo e Mago. Cada uma com 12 a 15 cartas originais até o nível 3.
- 4 tipos de monstro
- 1 história: campanha curta de 3 cenários para testar o fluxo inteiro, que é a primeira região do mapa do mundo
- Itens da primeira região: algumas armas, armaduras, escudos e consumíveis, o bastante para testar o equipamento

## Convenções

- Núcleo sem estado global. Toda aleatoriedade passa por um gerador injetado.
- Estado do jogo serializável em JSON, para salvar e carregar
- Regras e conteúdo separados: nada de valores de carta ou monstro escritos no código
- Cada regra nova vem com teste
- Nomes de código em inglês, textos do jogo em português, com estrutura pronta para tradução
