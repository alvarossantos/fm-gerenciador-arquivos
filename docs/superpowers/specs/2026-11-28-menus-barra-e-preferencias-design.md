# Design — Menus Arquivos/Visualização e preferências refinadas (Issue #20)

## Intenção

Organizar a barra superior do `fm` como no Nautilus, sem trocar a identidade do
projeto: `Arquivos` à esquerda, `Visualização` à direita junto de
Lista/Grade. As três direções visuais continuam sendo presets da mesma janela
(`Compacta`, `Equilibrada`, `Avançada`, padrão `Equilibrada`) e a paleta gerada
pela shell continua sendo a única fonte de cor.

Esta issue também fecha a limitação deixada pelo PR #19: `Colunas visíveis`
deixará de oferecer conjuntos prefixados e passará a aceitar `Nome` +
`Modificação` (ou qualquer outro subconjunto), com `Nome` sempre presente.

## Objetivos

- Dois menus persistentes na toolbar, acessíveis por mouse e teclado em janela
  estreita e nos três presets, reaproveitando as `QAction`s existentes.
- `Mostrar todas as abas` como menu suspenso: ordem da faixa, aba ativa marcada,
  caminho completo quando houver nomes repetidos, navegação por setas/Enter/Esc.
- Ordenação como estado explícito da `FileTab`, sincronizada entre cabeçalho,
  menu, lista, grade e troca de aba; reaplicada após `Atualizar`.
- Escolha individual das colunas em `Personalizar…`, persistida após reabrir o
  diálogo e reiniciar o `fm`.
- Legendas opcionais de tamanho e data de modificação na grade.
- Nenhuma regressão em `Ctrl+L`, breadcrumb, histórico/seleção por aba, menus de
  contexto ou troca de paleta em runtime.

## Não-objetivos

- Não alterar `QFileSystemModel`, `ThumbnailProxy`, `QTabWidget`, `ThemeManager`
  nem a semântica dos menus por alvo da Issue #6.
- Não adicionar dependências: apenas Qt6 Widgets/Test já usados.
- Não implementar Desfazer/Refazer; as ações entram somente quando a Issue #11
  tornar os comandos funcionais. Não deixar placeholders desabilitados.
- Não persistir a ordenação entre reinícios: ela vale por aba, na sessão corrente.
  Novas abas começam em `Nome`, ascendente.
- Não criar `QMenuBar`, nova classe de janela nem visão geral em grade de abas.

## Restrições

- Branch `enhancement/20-menus-barra`, base `main` em `2158d4e` (PR #19). Issue
  #20 aberta; dependência do PR #19 já integrada.
- `QSettings("fm","fm")`; chaves atuais `ui/preset`, `ui/density`, `ui/iconSize`,
  `ui/columns`, `ui/preview` continuam válidas. Novas chaves: `ui/gridSize`,
  `ui/gridDate`.
- `syncToolbarActions()` decide quais controles secundários aparecem na toolbar;
  as ações recolhidas continuam vivas e checáveis no menu `Visualização`.
- A ordem esperada do submenu `Exibir` nos menus de contexto
  (`Lista detalhada`, `Grade de ícones`, separador, prévia, ocultos, separador,
  zoom) e os `objectName`s usados pelos testes (`moreMenu`, `interfaceMenu`,
  `places`, `customizeDialog`) são preservados.

## Abordagens consideradas

**A — escolhida, para os três eixos.** (1) Dois `QToolButton` com
`InstantPopup` nas pontas da toolbar, sempre presentes; (2) coluna/direção como
campos da `FileTab`, com um único `applySort()` acionado por cabeçalho, menu e
`refreshModel()`; (3) quatro `QCheckBox` no `Personalizar…`, com `Nome` fixo.
Trade-off: alterações concentradas em `MainWindow`, `FileTab` e
`UiPreferences`, mas nenhuma classe nova de modelo e nenhuma duplicação de
`QAction`.

**B — alternativas rejeitadas.** Menus dentro do `⋮` perdem a posição
esquerda/direita pedida; `QMenuBar` muda a identidade do `fm`. Um
`QSortFilterProxyModel` dedicado seria mais robusto, mas o proxy atual já é
compartilhado entre lista e grade, então ele não traria comportamento novo.
`QListWidget` checável para colunas dá o mesmo resultado com mais código, e um
combo com os 16 subconjuntos seria ilegível. Um estado de ordem no `MainWindow`
por índice de aba quebra ao fechar ou mover abas.

## Decisões de implementação

### Barra e menus (`MainWindow`)

- Inserir `filesMenuButton` (`objectName="filesMenuButton"`) antes de `m_back` e
  `viewMenuButton` (`objectName="viewMenuButton"`) junto do par
  Lista/Grade, no lado direito. Ambos usam `QToolButton::InstantPopup`, não
  ganham atalho próprio e permanecem na toolbar mesmo a 320 px.
- `syncToolbarActions()` continua recolhendo apenas controles secundários; os
  dois menus nunca entram na lista `rules[]` e nunca são destruídos. Assim a
  largura do breadcrumb é afetada só pelos dois botões, de tamanho fixo.
- `Arquivos` contém, nesta ordem: `Nova janela` (nova `QAction`, `Ctrl+N`,
  inicia `QCoreApplication::applicationFilePath()` com o caminho da aba ativa),
  `Nova aba` (`m_newTabAct` existente), separador, `Mostrar todas as abas`,
  separador, `Preferências…` (chama `openCustomizeDialog()`), separador,
  `Atalhos de teclado`, `Ajuda` e `Sobre o fm`. Os três últimos usam
  `QMessageBox` com o texto já existente no projeto/atalhos das ações; nenhuma
  janela nova é introduzida.
- `Mostrar todas as abas` cria as ações em `aboutToShow`, na ordem exata do
  `QTabWidget`. Cada ação é checável; a ativa vem marcada. Quando duas abas têm
  o mesmo `tabText`, o rótulo passa a ser o caminho completo da aba. Escolher
  uma ação chama apenas `setCurrentIndex(i)` — nenhum dado de aba é tocado,
  então caminho, seleção e histórico permanecem. `QMenu` já fornece setas,
  `Enter` e `Esc`.
- `Visualização` contém: par exclusivo `Lista detalhada` / `Grade de ícones`,
  separador, oito ações de ordenação, separador, `Mostrar arquivos ocultos` e
  `Mostrar prévia`, separador, zoom `−`, `+`, `Restaurar zoom`, separador,
  `Mostrar tamanho na grade` e `Mostrar data de modificação na grade`.
- `Lista detalhada` / `Grade de ícones` passam a ser `QAction`s compartilhadas
  criadas no `MainWindow` e injetadas em `FileTab::addViewMenu()`, substituindo
  as ações locais atuais. `m_iconsAct` continua existindo como fonte única de
  verdade; um `QActionGroup` exclusivo mantém as duas marcas coerentes, sem
  laço de sinais. A ordem do submenu `Exibir` nos menus de contexto é mantida.
- As ações de zoom, prévia, ocultos e lista/grade são **as mesmas instâncias**
  da toolbar e dos menus de contexto. Nenhuma ação duplicada, nenhum atalho
  repetido e nenhuma mudança na contagem de `window->actions()` além das ações
  explicitamente novas (`Nova janela`, par Lista/Grade, oito de ordenação, duas
  de legendas e duas de menu).

### Ordenação por aba (`FileTab` + `MainWindow`)

- `FileTab` ganha `m_sortColumn` (0..3) e `m_sortOrder` (`Qt::AscendingOrder` /
  `Qt::DescendingOrder`), ambos com getter público, e `applySort()` que reaplica
  `sortByColumn()` no modelo. `refreshModel()` chama `applySort()` depois de
  reatribuir modelos e índices, cobrindo o comando `Atualizar`.
- `QHeaderView::sortIndicatorChanged` e o menu passam pelo mesmo
  `setSort(int, Qt::SortOrder)`, que bloqueia sinais durante a reaplicação e
  emite `sortChanged(int, Qt::SortOrder)`. `MainWindow` conecta esse sinal para
  marcar a ação correspondente.
- O menu de ordenação tem oito ações checáveis num `QActionGroup` exclusivo:
  `Nome (A–Z)`, `Nome (Z–A)`, `Modificação (mais recente)`,
  `Modificação (mais antiga)`, `Tamanho (maior)`, `Tamanho (menor)`,
  `Tipo (A–Z)` e `Tipo (Z–A)`. Coluna 0/3/1/2 respectivamente; as direções
  cobrem também o que o clique no cabeçalho alterna. Esta é uma decisão do
  autor do spec para manter marcação e clique equivalentes.
- `MainWindow::currentChanged` atualiza as marcas a partir da aba ativa; trocar
  de aba não altera a ordem de nenhuma aba. Alternar lista/grade apenas troca o
  widget do `QStackedWidget` e não toca no estado.

### Colunas individuais (`UiPreferences` + `MainWindow`)

- `normalizeColumns()` em `UiPreferences.cpp` substitui `validColumn()` na
  leitura: remove duplicatas, ordena, aceita somente `0..3` e garante `0`
  (`Nome`). Lista vazia, valor fora do intervalo ou ausência de `Nome` caem no
  default do preset — mesma política de JSON de tema inválido. Listas antigas
  já persistidas (`{"0"}`, `{"0","1","2","3"}`) continuam válidas, sem
  migração.
- `openCustomizeDialog()` troca `customColumns` (`QComboBox`) por quatro
  `QCheckBox`: `customColumn0` (`Nome`, `setEnabled(false)`, sempre checado),
  `customColumn1` (`Tamanho`), `customColumn2` (`Tipo`) e `customColumn3`
  (`Modificação`). No aceite, a lista é montada a partir das caixas marcadas e
  passa por `normalizeColumns()` antes de `applyUiPreferences()`.
- `Restaurar padrões` continua voltando à Equilibrada e reaplicando suas
  colunas padrão; ajustes manuais de colunas são descartados como hoje.

### Legendas da grade

- `UiPreferences` ganha `gridSize` e `gridDate` (bool, padrão `false` em todos
  os presets) e as chaves `ui/gridSize`, `ui/gridDate`. Escolha do autor do
  spec: manter a grade limpa nos três presets e deixar o usuário ligar cada
  legenda; `Restaurar padrões` volta a `false/false`.
- `FileTab::setGridLegends(bool size, bool date)` instala um
  `GridLegendDelegate` no `QListView`, que desenha o nome e, abaixo dele, o
  tamanho (`QFileSystemModel` coluna 1) e/ou a data de modificação (coluna 3)
  obtidos pelo proxy. Nenhuma biblioteca nova.
- As duas ações do menu `Visualização` são checáveis e persistem a mudança
  imediatamente, como `ui/preview`. Um getter em `FileTab` expõe o estado para
  testes.

### Paleta, teclado e janela estreita

- Todos os menus são feitos das `QAction`s da janela, então `ThemeManager`
  atualiza texto, ícones e estados de foco/seleção sem código extra. A troca de
  paleta com menu aberto é coberta por teste: marcas e `enabled` não mudam.
- `QToolButton::InstantPopup` + ações já registradas em `addAction()` garantem
  abertura por teclado. Em 320 px, os dois menus continuam visíveis e
  navegáveis; só os controles secundários são recolhidos.
- Objeto `Mostrar todas as abas` não cria nem fecha aba; `Ctrl+L`,
  `beginPathEdit`/`endPathEdit`, `Ctrl+W`, os ancestrais do `m_overflow` e os
  menus de contexto permanecem intocados.

## Verificação

- `mainwindow_test`: menus presentes nos três presets e a 320 px; ordem dos
  itens de `Arquivos`; suspenso de abas com nomes repetidos mostrando caminho,
  ativa marcada, e `setCurrentIndex` preservando caminho/seleção/histórico.
- `filetab_test` / `mainwindow_test`: duas abas com ordens diferentes; alternar
  lista/grade; clicar cabeçalho e escolher no menu; verificar a **ordem real
  dos itens** no modelo das duas visões, inclusive após `Atualizar`, e a marca
  ativa no menu ao trocar de aba.
- `mainwindow_test`: salvar `Nome + Modificação`, reabrir o diálogo com as
  caixas corretas, reiniciar `MainWindow` e conferir a preferência e as colunas
  visíveis.
- `uipreferences_test`: `normalizeColumns()` rejeita lista vazia, duplicata,
  índice inválido e lista sem `Nome`; round-trip com as novas chaves de grade.
- `theme_manager_test`: troca de paleta com os menus abertos, marcas legíveis.
- Build Debug/Release sem warnings e CTest completo em ambos. `--bench` Release
  mantém o protocolo de sete rodadas, primeira descartada, mediana das seis.

## O que fica para o planejamento

O plano será dividido em quatro sprints verificáveis, uma por entrega: menus e
ações compartilhadas; ordenação por aba; colunas individuais e legendas;
paleta/janela estreita e fechamento. Cada sprint termina com CTest e commit
próprio antes da próxima.
