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
  para a pasta capturada no início da extração (`destDir`), não para a pasta
  atual no fim. Destino existente é movido para backup dentro do temp antes
  de ser sobrescrito; se qualquer entrada falhar, as publicações e os backups
  já feitos são revertidos — nunca fica estado parcial.
- `runBsdtar()` recebe opcionalmente o diálogo, o temporário e um callback de
  publicação; sem eles, comporta-se como hoje (compactação intacta).
- Testes: casos em `filetab_test.cpp` com `bsdtar` falso no `PATH`, cobrindo
  falha, cancelamento, sucesso, rollback de publicação parcial, troca de pasta
  durante a extração e cancelamento em `Extrair aqui`.

## Pendente para o implementador

- `QProgressDialog::cancel()` só marca `wasCanceled()`; o sinal `canceled()`
  vem do clique no botão. O teste clica no botão.
- Diálogo é filho do `FileTab` e o processo é protegido por `QPointer` para
  evitar uso após destruição da aba; o scratch tem guarda de limpeza ligada a
  `destroyed`.
- Registrar no relatório da branch o que foi automatizado e o que exigiria
  validação manual no desktop.
