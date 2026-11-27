# Design — extração cancelável com publicação atômica (2026-11-26)

## Intenção

Hoje `extract()` cria a pasta final antes de iniciar o `bsdtar`, e `runBsdtar()`
roda o processo em background sem cancelamento. Consequência: a pasta aparece
vazia ou parcialmente preenchida, inclusive se a extração falhar. Queremos que
o usuário possa cancelar sem travar a interface e que o resultado final só
fique visível depois que o processo terminar com sucesso. Em cancelamento ou
falha, nada da extração permanece — o diretório temporário é apagado.

## Objetivos

- Botão **Cancelar** durante a extração, com a UI ainda responsiva.
- `Extrair em pasta própria`: a pasta com nome final só existe após sucesso.
- `Extrair aqui`: os itens só aparecem na pasta atual após sucesso.
- Cancelar ou falhar remove todo o conteúdo temporário.
- Status claro: `Extraindo…`, `Extraído em …`, `Extração cancelada`, falha com erro do bsdtar.

## Não-objetivos

- Não mudar compactação, nem adicionar barra de progresso percentual (bsdtar não informa progresso).
- Não criar abstração nova nem arquivo de código novo.
- Não integrar em `main` nesta etapa: trabalho fica em `feat/estabilidade`.

## Restrições

- `bsdtar` já protege contra zip-slip; continua sendo o único executador.
- `QTemporaryDir` e `QProgressDialog` já vêm com Qt6; nenhuma dependência nova.
- `runBsdtar()` também atende `compress()`; qualquer parâmetro novo precisa ter valor padrão.

## Abordagens consideradas

**A — mínima em `FileTab` (escolhida).** `extract()` cria um `QTemporaryDir`
oculto ao lado do destino, extrai para ele e mostra um `QProgressDialog`
não modal com botão Cancelar. No `finished`, sucesso publica o resultado;
cancelamento/falha apaga o temporário. Vantagem: toca só em `FileTab.cpp`/`.h`,
sem nova classe. Custo: em `Extrair aqui` a publicação é uma movimentação curta
dos itens do temporário para a pasta atual, feita depois do sucesso.

**B — classe `ExtractionTask`.** Isolaria processo, temporário, diálogo e
commit numa classe própria, mais fácil de testar isoladamente. Rejeitada: cria
uma abstração para um único fluxo, contrariando o mínimo suficiente.

**C — cancelar limpando o destino final.** Manteria o destino atual e removeria
o parcial no cancelamento. Rejeitada: a pasta continua visível durante a
operação, que é justamente o problema relatado.

## Decisões de implementação

- `QProgressDialog` com `Qt::NonModal`, faixa indeterminada (`setRange(0, 0)`),
  `setMinimumDuration(0)` e texto do botão `Cancelar`; sinal `canceled` pede
  `QProcess::kill()` e marca o estado como cancelado.
- `QTemporaryDir` com template `.fm-extract-XXXXXX` dentro da pasta pai do
  destino — mesmo filesystem, para rename barato.
- `Extrair em pasta própria`: ao sucesso, `setAutoRemove(false)` e
  `QDir::rename(temp, destino)`; se `destino` já existir, aborta e apaga o temp.
- `Extrair aqui`: ao sucesso, mover cada entrada de nível superior do temp
  para `m_path` (`QFile::rename`, com `copyRecursive` como fallback entre
  filesystems), removendo destino existente para preservar o comportamento do
  bsdtar de sobrescrever.
- `runBsdtar()` recebe opcionalmente o diálogo, o temporário e um callback de
  publicação; sem eles, comporta-se como hoje (compactação intacta).
- Testes: caso novo em `filetab_test.cpp` com `bsdtar` falso no `PATH`, cobrindo
  falha (arquivo criado no temp, processo encerrado com erro, nenhuma pasta
  final) e cancelamento (processo killado, temp removido).

## Pendente para o implementador

- Conferir o comportamento real do `QProgressDialog` não modal no offscreen e
  no Umbriel; se o botão não aparecer, ajustar a forma de localizá-lo no teste
  sem tornar o diálogo modal.
- Registrar no relatório da branch o que foi automatizado e o que exigiria
  validação manual no desktop.
