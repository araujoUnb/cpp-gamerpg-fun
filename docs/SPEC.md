# RPG de Tabuleiro Tático (projeto em C++)

> **Escopo atual:** começar com **uma única história** (uma campanha curta de 3 cenários).
> A estrutura de pastas por campanha continua valendo, para que outras histórias
> possam ser adicionadas depois sem mudar o código.

## Visão geral

Jogo de RPG de tabuleiro digital, para um jogador controlando um grupo de heróis, com histórias independentes em formato de campanha. Combina três ideias:

1. **Ficha de personagem inspirada no D&D 5e.** Seis atributos, classes, níveis e o d20 como resolução de testes.
2. **Combate por cartas com duas metades**, inspirado no estilo de jogos como Gloomhaven. As cartas do personagem são ao mesmo tempo ações e recurso de resistência.
3. **Histórias ramificadas** escritas fora do código, com escolhas, testes de atributo e consequências.

Não usar nomes, textos, arte, classes ou monstros protegidos de Gloomhaven ou de D&D. Somente as mecânicas gerais servem de inspiração. Regras do D&D podem vir do SRD 5.x, publicado pela Wizards of the Coast sob licença Creative Commons CC-BY-4.0, com o crédito exigido incluído na tela de créditos e em `LICENSES/SRD.md`.

## Stack

- C++20, CMake, build multiplataforma (Linux, Windows, macOS)
- **Núcleo de regras sem dependência gráfica**, em biblioteca estática `core/`. Tudo que é regra mora aqui e é testável sem abrir janela.
- **Frontend inicial em raylib**, em `app/`. O núcleo não pode depender do frontend, para permitir migrar depois para Godot via GDExtension.
- Dados do jogo em JSON (nlohmann/json): classes, cartas, monstros, cenários
- Histórias em ink, com o runtime inkcpp, em fase posterior
- Testes com doctest
- Dependências via CMake FetchContent

## Estrutura de pastas

```
core/
  dice/        rolagem d20, vantagem e desvantagem, gerador com semente
  character/   atributos, modificadores, classes, níveis, ficha
  cards/       cartas, baralho, mão, descarte, cartas perdidas
  combat/      grade hexagonal, iniciativa, turnos, efeitos, condições
  monsters/    IA simples e baralho de ações dos monstros
  scenario/    mapa, objetivos, gatilhos
  campaign/    progresso entre cenários, recompensas, flags de história
  data/        carregamento e validação dos JSON
app/           frontend raylib
data/          JSON de conteúdo
stories/       arquivos .ink
tests/
```

## Regras do jogo

### Ficha

- Atributos: FOR, DES, CON, INT, SAB, CAR, valores de 8 a 20
- Modificador = (valor - 10) / 2, arredondando para baixo
- Bônus de proficiência por nível, como no D&D 5e
- Pontos de vida = valor base da classe + modificador de CON por nível
- Nível máximo por campanha: 9
- Cada nível libera novas cartas da classe, e o jogador escolhe quais entram no baralho

### Testes

- Rolagem: d20 + modificador do atributo + proficiência, se aplicável
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

### Descanso e exaustão

- O tamanho da mão é definido pela classe, entre 8 e 12 cartas, e é a resistência do herói
- **Descanso curto:** no fim da rodada, embaralha o descarte, perde uma carta aleatória e recupera o resto
- **Descanso longo:** usa a rodada inteira, escolhe qual carta perder, recupera o descarte e cura 2 PV
- Um herói fica **exausto** e sai do cenário se não tiver cartas suficientes para jogar duas ou se chegar a 0 PV, depois de falhar nos testes de morte
- Isso cria a pressão de tempo do cenário, porque cada carta perdida aproxima a exaustão

### Monstros

- Cada tipo de monstro tem um pequeno baralho de ações. A cada rodada uma carta é virada, com iniciativa e ação.
- IA simples: andar até o herói mais próximo que consegue atacar, atacar, e respeitar a ação da carta
- Estatísticas em JSON: CA, PV, atributos, dano, imunidades

### Condições

Começar com poucas: envenenado, atordoado, imobilizado, fortalecido, protegido. Cada uma com duração em rodadas e efeito declarado em JSON.

### Tabuleiro

- Grade hexagonal com coordenadas axiais
- Obstáculos, terreno difícil, armadilhas e portas
- Linha de visão e alcance calculados no núcleo
- Pathfinding A* para heróis e monstros

### Campanha e histórias

- Uma campanha é uma sequência de cenários ligados por trechos de história em ink
- O ink pode pedir testes de atributo, que o núcleo resolve e devolve o resultado para a história
- Flags de campanha guardam escolhas e liberam ou bloqueiam cenários
- Cada campanha fica em sua pasta dentro de `stories/` e `data/`. Por enquanto existe só uma.

## Ordem de desenvolvimento

1. **Dados e testes.** `dice/` com semente, vantagem, crítico, e testes cobrindo tudo
2. **Ficha.** Atributos, modificadores, classes vindas de JSON
3. **Cartas.** Mão, descarte, perdidas, escolha de duas cartas, descansos, exaustão
4. **Combate em modo texto.** Um cenário pequeno rodando no terminal com dois heróis e três monstros, para validar as regras antes de qualquer gráfico
5. **Tabuleiro hexagonal.** Movimento, alcance, linha de visão, A*
6. **Frontend raylib.** Desenhar a grade, peças, mão de cartas, rolagens animadas
7. **Campanha.** Encadear cenários, progressão de nível entre eles
8. **Histórias em ink.** Integração do inkcpp e testes de atributo vindos da história

Não pular para o frontend antes da etapa 4 funcionar no terminal.

## Conteúdo inicial

- 4 classes originais: uma de linha de frente, uma ágil, uma arcana e uma de suporte, com 12 a 15 cartas cada até o nível 3
- 4 tipos de monstro
- 1 história: campanha curta de 3 cenários para testar o fluxo inteiro

## Convenções

- Núcleo sem estado global. Toda aleatoriedade passa por um gerador injetado.
- Estado do jogo serializável em JSON, para salvar e carregar
- Regras e conteúdo separados: nada de valores de carta ou monstro escritos no código
- Cada regra nova vem com teste
- Nomes de código em inglês, textos do jogo em português, com estrutura pronta para tradução
