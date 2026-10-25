# Juiz das narrações

Rubrica para dar nota às narrações do mestre narrador. É uma ferramenta de
desenvolvimento: mede se uma mudança (instruções, exemplos, banco, modelo)
melhorou a narração. Não roda durante o jogo.

## Entrada

Cada item é uma rodada (ou a abertura) de uma partida de `tools/narrator_play.cpp`:
a situação enviada ao narrador (`grupo`, `inimigos`, `eventos`, `contexto`) e
uma ou mais narrações dessa mesma rodada, rotuladas A, B... em ordem sorteada.
O juiz não sabe de qual versão veio cada narração.

Os eventos são a verdade do jogo. Linhas como "X se prepara: 'carta'" e
"X usa 'carta'" dizem quem fez o quê; os números (rolagens, CA, dano, PV)
existem para o juiz, não para a narração.

## Critérios (1 a 5, 5 é o melhor)

- **fidelidade**: narra os eventos que importam (acertos, erros, quedas,
  curas, condições, fim do combate), na ordem, sem contradizê-los. Pode
  resumir eventos menores; não pode dizer que alguém acertou quando errou,
  caiu quando não caiu, ou que o combate acabou quando não acabou.
- **autoria**: cada ação é de quem o evento diz. Trocar quem atacou, quem
  usou uma carta, quem foi curado ou quem caiu é erro grave.
- **invencao**: nada de fatos que os eventos não trazem: números (PV, dano,
  rolagens), mortes, ferimentos ou golpes que não houve, objetos e armas
  específicos, lugares e detalhes que mudam o que aconteceu. Ambiente e
  sensações (som, poeira, respiração) são permitidos. 5 = nada inventado.
- **herois**: os heróis são do jogador. Sem falas, pensamentos ou decisões
  dos heróis (descrever o que fizeram nos eventos é permitido). Na abertura,
  nada de decidir o que farão.
- **portugues**: correção, concordância (inclusive de gênero), palavras
  existentes, sem inglês, frases que fazem sentido.
- **vida**: narra como um bom mestre de mesa: imagens concretas, ritmo,
  variedade. Lista seca de eventos ou frases vagas e genéricas valem pouco.

Uma narração vazia (`null`) recebe 1 em tudo.

## Saída

Um JSON por item, numa lista:

```json
{"item": "arena fighter+wizard 1 #3",
 "notas": {"A": {"fidelidade": 4, "autoria": 5, "invencao": 3, "herois": 5, "portugues": 4, "vida": 4},
           "B": {"fidelidade": 3, "autoria": 2, "invencao": 4, "herois": 5, "portugues": 5, "vida": 3}},
 "preferida": "A",
 "erros": {"A": ["inventa 'sangue escorrendo' num esqueleto"], "B": ["diz que Bento usou 'Golpe Firme', que foi de Aria"]}}
```

`preferida` é "A", "B" ou "empate". `erros` cita cada problema em poucas
palavras, com o trecho quando ajudar.
