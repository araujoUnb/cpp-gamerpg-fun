# cpp-gamerpg-fun

Especificação completa: [docs/SPEC.md](docs/SPEC.md). Ler antes de mudar regras.

## Build e testes

```
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/cli/gamerpg_cli                 # joga a arena no terminal
./build/cli/gamerpg_cli --auto --seed 3 # assiste o jogador automático
./build/cli/gamerpg_cli --bench 2000    # velocidade e taxa de vitória (use build Release)
./build/cli/gamerpg_cli --dificuldade hard # easy, normal (padrão), hard; standard = mestre clássico sem ajuste
./build/tools/gamerpg_gm_train --help   # treina os pesos do mestre e calibra as dificuldades (rodar no lab10, ver treino/)
```

## Regras de trabalho

- Seguir a ordem de desenvolvimento da SPEC. Nada de frontend (Godot) antes da etapa 4 rodar no terminal.
- `core/` não depende de `bindings/`, `godot/` nem de nada gráfico. Sem estado global; aleatoriedade sempre via `rpg::dice::Rng` injetado.
- Não usar `std::uniform_int_distribution` nem `std::shuffle` com distribuições da STL: o resultado muda entre libstdc++, libc++ e MSVC e quebra replays. Usar os métodos de `Rng`.
- Valores de cartas, classes e monstros vêm de `data/`, nunca do código.
- Toda regra nova vem com teste em `tests/`.
- Código em inglês; textos do jogo em português.
- Escopo atual: uma única história (campanha de 3 cenários), com as 12 classes do SRD.
- Núcleo reutilizável: números de regra ficam em `data/rules/ruleset.json`; funções de regra recebem o `Ruleset`, nunca usam constantes fixas. Rodar os testes também com `-fsanitize=address,undefined`.
- Desempenho importa (treino de IA): nada de `std::map`/`std::set` nem cópia de string em laços de simulação. Medir com `--bench` em Release antes e depois de mexer no combate.
