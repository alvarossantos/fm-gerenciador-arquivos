# Menus Arquivos/Visualização e preferências refinadas (Issue #20) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dividir a barra do `fm` em `Arquivos` e `Visualização`, com ordenação por aba, colunas escolhidas individualmente e legendas de grade, sem perder presets, paleta da shell, atalhos ou navegação.

**Architecture:** `MainWindow` passa a ser o único dono das `QAction`s compartilhadas e dos dois menus; `FileTab` guarda o estado de ordenação e aplica as legendas na grade; `UiPreferences` normaliza colunas e persiste as novas opções. Nenhuma classe de janela ou modelo é duplicada.

**Tech Stack:** C++17, Qt6 Widgets/Test, `QSettings`, `QActionGroup`, `QHeaderView`, `QStyledItemDelegate`.

**Spec:** `docs/superpowers/specs/2026-11-28-menus-barra-e-preferencias-design.md`

## Global Constraints

- Branch `enhancement/20-menus-barra`, base `main` em `2158d4e`, Issue #20, spec aprovada em `9661b8e`.
- Nenhuma dependência nova; apenas Qt6 Widgets/Test já usados.
- `QSettings("fm","fm")`. Chaves existentes: `ui/preset`, `ui/density`, `ui/iconSize`, `ui/columns`, `ui/preview`. Novas: `ui/gridSize`, `ui/gridDate` (padrão `false`).
- Presets: `Compacta`, `Equilibrada` (padrão), `Avançada`; trocar de preset descarta ajustes manuais.
- Limite de toolbar estreita: `width() < 720` — os dois menus nunca entram em `rules[]`.
- Teste de janela estreita: **320 px**, três presets, ambos os menus visíveis e acionáveis por `Tab` + `Espaço`/`Enter`, fecháveis com `Esc`.
- `objectName`s obrigatórios: `filesMenuButton`, `viewMenuButton`, `filesMenu`, `viewMenu`, `showAllTabsAction`, `showAllTabsMenu`, `newWindowAction`, `preferencesAction`, `shortcutsAction`, `helpAction`, `aboutAction`, `focusPathAction`, `closeTabAction`, `listViewAction`, `gridViewAction`, `refreshAction`, `sortNameAsc`, `sortNameDesc`, `sortDateNewest`, `sortDateOldest`, `sortSizeDesc`, `sortSizeAsc`, `sortTypeAsc`, `sortTypeDesc`, `gridSizeAction`, `gridDateAction`, `customColumn0`..`customColumn3`.
- Ordenação padrão de aba nova: coluna `0`, `Qt::AscendingOrder`; não persistida entre reinícios.
- `Mostrar todas as abas`: ordem da faixa, aba ativa marcada, caminho completo quando `tabText` repetir; `setCurrentIndex` é a única mutação.
- Colunas: `Nome` sempre visível e desabilitado no diálogo; `normalizeColumns()` sanitiza em vez de rejeitar.
- Legendas: `iconSize`/zoom controlam só o ícone; a célula precisa reservar uma linha extra por legenda ativa.
- Sem warnings em Debug/Release; CTest completo em ambos; `--bench` Release com sete rodadas, primeira descartada, mediana das seis.

## Review Focus

| Entrada / condição | Comportamento esperado | Onde fica o teste |
|---|---|---|
| Janela a 320 px nos três presets | Ambos os menus visíveis; `Tab` alcança os botões, `Espaço`/`Enter` abre, `Esc` fecha | Task 2, `keyboardOpensMenusAt320InEveryPreset` |
| Duas abas com o mesmo nome de pasta | Suspenso mostra caminho completo para as repetidas e marca a ativa | Task 2, `tabsMenuUsesFullPathForDuplicates` |
| `Atualizar` recriando o modelo | A ordem real dos itens continua igual na lista e na grade | Task 3, `sortSurvivesRefreshInBothViews` + Task 4, `sortIsPerTabAndSurvivesRefresh` |
| Config corrompida de `ui/columns` (`{"0","0","9"}`) | Sanitiza para `{"0"}` sem descartar os outros ajustes | Task 5, `normalizeColumnsSanitizes` |
| Nome longo com duas legendas em zoom 50% e 200% | Nenhuma legenda cortada; altura da célula acompanha o delegate sem alterar `iconSize` | Task 6, `gridLegendsReserveCellHeightAtBothZooms` |

---

## File Structure

| Arquivo | Responsabilidade |
|---|---|
| `src/MainWindow.h/.cpp` | Menus, ações compartilhadas, suspenso de abas, sincronização de ordem, diálogos locais |
| `src/FileTab.h/.cpp` | Estado/aplicação da ordenação, delegate local das legendas, injeção das ações de visão |
| `src/UiPreferences.h/.cpp` | `normalizeColumns()`, campos `gridSize`/`gridDate` e persistência |
| `tests/mainwindow_test.cpp` | Menus, teclado, suspenso de abas, sincronização, diálogos, colunas, paleta |
| `tests/filetab_test.cpp` | Ordem real das visões, `refreshModel()`, geometria das legendas |
| `tests/uipreferences_test.cpp` | Normalização de colunas e round-trip das novas chaves |

---

## Sprint 1 — Barra e ações compartilhadas

### Task 1: Ações de Lista/Grade compartilhadas e rótulos de atalho

**Files:**
- Modify: `src/MainWindow.h`, `src/MainWindow.cpp`
- Modify: `src/FileTab.h`, `src/FileTab.cpp`
- Test: `tests/mainwindow_test.cpp`

**Interfaces:**
- Consumes: `m_iconsAct`, `m_previewAct`, `m_hiddenAct`, `m_zoomInAct`, `m_zoomOutAct`, `m_zoomResetAct` (já existentes).
- Produces:
  - `QAction *m_listAct` com `objectName="listViewAction"`, checável, sem atalho próprio.
  - `m_iconsAct` passa a ter `objectName="gridViewAction"`.
  - `QActionGroup` exclusivo ligando `m_listAct`/`m_iconsAct`, sem laço de sinal.
  - `FileTab::setViewActions(QAction *list, QAction *grid, QAction *preview, QAction *hidden, QAction *zoomIn, QAction *zoomOut, QAction *zoomReset)` — substitui a assinatura atual.
  - Ações sem texto `focusPathAct`/`closeTabAct` passam a ter `objectName="focusPathAction"`/`"closeTabAction"` e textos `Editar caminho`/`Fechar aba`.

- [ ] **Step 1: Escrever teste falhante em `tests/mainwindow_test.cpp`**

```cpp
void MainWindowTest::shortcutActionsHaveReadableLabels()
{
    MainWindow w(QDir::homePath());
    auto *focus = w.findChild<QAction *>(QStringLiteral("focusPathAction"));
    auto *close = w.findChild<QAction *>(QStringLiteral("closeTabAction"));
    QVERIFY(focus && close);
    QCOMPARE(focus->text(), QStringLiteral("Editar caminho"));
    QCOMPARE(close->text(), QStringLiteral("Fechar aba"));
    QCOMPARE(focus->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_L));
    QCOMPARE(close->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_W));
}
```

Inclua `viewModeSwitchesBothWaysFromToolbarAndContextMenu`: em `Grade → Lista → Grade`, alterne primeiro pela toolbar (`gridViewAction`/`listViewAction`) e depois pelo menu de contexto (`findChild<QMenu*>` da aba, ações `Lista detalhada`/`Grade de ícones`). A cada passo confirme **marcação** (`gridViewAction->isChecked()`) e **visualização efetiva** (`QStackedWidget::currentWidget()` é `QListView`/`QTreeView`). Ajuste também `viewMenuSharesToolbarStateAcrossTabs` para usar as mesmas instâncias da janela.

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R mainwindow_test`
Expected: FAIL — `focusPathAction` e `listViewAction` não existem.

- [ ] **Step 3: Implementar**

Em `MainWindow`, crie `m_listAct`, atribua os `objectName`s e coloque as duas ações num `QActionGroup` exclusivo. **Concentre a mudança de visualização no `toggled` de `m_iconsAct`**: marcar `m_listAct` desmarca `m_iconsAct` pelo próprio grupo, que emite `toggled(false)` e troca para lista; marcar `m_iconsAct` emite `toggled(true)` e troca para grade. Não crie conexão inversa nem transição extra. Atualize `addTab()` e todas as chamadas de `setViewActions`. Em `FileTab::addViewMenu()`, use as ações injetadas em vez das locais, mantendo a ordem `Lista detalhada`, `Grade de ícones`, separador, prévia, ocultos, separador, zoom.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R mainwindow_test`
Expected: PASS, incluindo `viewMenuSharesToolbarStateAcrossTabs` e o novo caso.

- [ ] **Step 5: Commit**

```bash
git add src/MainWindow.h src/MainWindow.cpp src/FileTab.h src/FileTab.cpp tests/mainwindow_test.cpp
git commit -m "refactor: compartilha ações de visão e rotula atalhos"
```

### Task 2: Menus `Arquivos`/`Visualização`, suspenso de abas e teclado

**Files:**
- Modify: `src/MainWindow.h`, `src/MainWindow.cpp`
- Test: `tests/mainwindow_test.cpp`

**Interfaces:**
- Consumes: ações da Task 1 e `m_newTabAct`, `m_restoreDefaultsAct`, ações de zoom/prévia/ocultos.
- Produces:
  - `QToolButton`s `filesMenuButton`/`viewMenuButton` com menus `filesMenu`/`viewMenu`, ambos `InstantPopup`.
  - `QAction`s `newWindowAction` (`Ctrl+N`), `showAllTabsAction` com menu `showAllTabsMenu`, `preferencesAction`, `shortcutsAction`, `helpAction`, `aboutAction`.
  - `void rebuildTabsMenu()` — repopula `showAllTabsMenu` em `aboutToShow`.
  - `void openShortcutsDialog()`, `void openHelpDialog()`, `void openAboutDialog()`.

- [ ] **Step 1: Escrever testes falhantes em `tests/mainwindow_test.cpp`**

Três casos:

```cpp
void MainWindowTest::keyboardOpensMenusAt320InEveryPreset()
{
    // Para cada preset em {Compacta, Equilibrada, Avançada}:
    //   w.resize(320, 600); QApplication::processEvents();
    //   para cada botão em {filesMenuButton, viewMenuButton}:
    //     QVERIFY(btn->isVisible());
    //     percorra Tab até QApplication::focusWidget() == btn (limite 30 teclas);
    //     QVERIFY(btn->hasFocus());
    //     QTest::keyClick(btn, Qt::Key_Space);  QVERIFY(btn->menu()->isVisible());
    //     QTest::keyClick(btn, Qt::Key_Escape); QVERIFY(!btn->menu()->isVisible());
    //     repita com Qt::Key_Enter para a abertura.
}
```

```cpp
void MainWindowTest::tabsMenuUsesFullPathForDuplicates()
{
    // Crie duas pastas com o mesmo nome sob pais diferentes; abra uma aba em cada.
    // Na aba 1, navegue para uma subpasta e volte (histórico criado) e só então
    // selecione um arquivo na pasta atual, antes de trocar de aba.
    // Abra showAllTabsMenu e confirme: 2 ações, ordem da faixa, textos com caminho
    // completo, exatamente uma checada (a ativa).
    // Com o menu aberto: setas Down/Up percorrem as ações, Enter ativa a escolhida
    // e Esc fecha sem trocar de aba.
    // Depois de trocar, volte à aba 1 e confirme currentIndex, currentPath,
    // seleção do arquivo e botão Voltar habilitado.
}
```

```cpp
void MainWindowTest::localDialogsHaveReadableText()
{
    // Trigger shortcutsAction; localize o QMessageBox modal com QTimer e confirme
    // que o texto contém "Editar caminho", "Ctrl+L", "Fechar aba", "Ctrl+W".
    // Ajuda e Sobre devem ter texto não vazio.
}
```

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R mainwindow_test`
Expected: FAIL — botões e ações ausentes.

- [ ] **Step 3: Implementar**

Insira `filesMenuButton` antes de `m_back` e `viewMenuButton` junto de Lista/Grade. Em largura normal use `ToolButtonTextOnly`; abaixo de 720 px use ícone com `toolTip`/`accessibleName` preservando os rótulos. Monte os menus na ordem do spec. `rebuildTabsMenu()` usa `m_tabs->tabText()`/`FileTab::currentPath()`, marcando a ativa e usando o caminho completo quando houver repetição. `openShortcutsDialog()` percorre `window->actions()` e formata `text()` + `shortcut().toString()`, filtrando ações sem texto. `Nova janela` usa `QProcess::startDetached(QCoreApplication::applicationFilePath(), { currentTab()->currentPath() })`.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R mainwindow_test`
Expected: PASS nos três presets e a 320 px.

- [ ] **Step 5: Commit**

```bash
git add src/MainWindow.h src/MainWindow.cpp tests/mainwindow_test.cpp
git commit -m "feat: menus Arquivos e Visualização com suspenso de abas"
```

## Sprint 2 — Ordenação por aba

### Task 3: Estado de ordenação na `FileTab`

**Files:**
- Modify: `src/FileTab.h`, `src/FileTab.cpp`
- Test: `tests/filetab_test.cpp`

**Interfaces:**
- Consumes: `ThumbnailProxy`, `m_tree`, `m_icons` existentes.
- Produces:
  - `int sortColumn() const`, `Qt::SortOrder sortOrder() const`.
  - `void setSort(int column, Qt::SortOrder order)`.
  - `void applySort()` — reaplica `sortByColumn()` no modelo.
  - Sinal `void sortChanged(int column, Qt::SortOrder order)`.
  - `refreshAction` com `objectName="refreshAction"` na ação `Atualizar` existente.

- [ ] **Step 1: Escrever teste falhante em `tests/filetab_test.cpp`**

```cpp
void FileTabTest::sortSurvivesRefreshInBothViews()
{
    // Crie a.txt, b.txt, c.txt na pasta.
    FileTab tab(dir.path());
    const QStringList asc { "a.txt", "b.txt", "c.txt" };
    const QStringList desc { "c.txt", "b.txt", "a.txt" };

    tab.setSort(0, Qt::AscendingOrder);
    // Leia a sequência real em QTreeView E, separadamente, em QListView:
    //   cada uma deve ser exatamente asc — não compare uma com a outra.
    tab.setSort(0, Qt::DescendingOrder);
    // Repita: cada visão deve ser exatamente desc.
    // Trigger refreshAction (objectName) e repita: cada visão continua desc.
    // QSignalSpy em sortChanged recebe (0, AscendingOrder) e (0, DescendingOrder).
}
```

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R filetab_test`
Expected: FAIL — `setSort`/`sortColumn` não existem.

- [ ] **Step 3: Implementar**

Guarde `m_sortColumn=0` e `m_sortOrder=Qt::AscendingOrder`. `setSort()` valida coluna `0..3`, bloqueia sinais durante `applySort()`, grava os campos e emite `sortChanged`. Conecte `QHeaderView::sortIndicatorChanged` ao mesmo `setSort`. Chame `applySort()` ao final de `refreshModel()` e na construção, substituindo o `sortByColumn(0, Ascending)` fixo.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R filetab_test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/FileTab.h src/FileTab.cpp tests/filetab_test.cpp
git commit -m "feat: ordenação explícita por aba com reaplicação após atualizar"
```

### Task 4: Ações de ordenação sincronizadas com a aba ativa

**Files:**
- Modify: `src/MainWindow.h`, `src/MainWindow.cpp`
- Test: `tests/mainwindow_test.cpp`

**Interfaces:**
- Consumes: `FileTab::setSort(int, Qt::SortOrder)`, `sortColumn()`, `sortOrder()`, `sortChanged` da Task 3.
- Produces: oito ações checáveis num `QActionGroup` exclusivo, nos `objectName`s da seção Global Constraints, e `void syncSortActions()`.

- [ ] **Step 1: Escrever teste falhante em `tests/mainwindow_test.cpp`**

```cpp
void MainWindowTest::sortIsPerTabAndSurvivesRefresh()
{
    // Aba 1: a1.txt, b1.txt, c1.txt. Aba 2: a2.txt, b2.txt, c2.txt.
    const QStringList aba1Desc { "c1.txt", "b1.txt", "a1.txt" };
    const QStringList aba2Asc  { "a2.txt", "b2.txt", "c2.txt" };

    // Escolha sortNameDesc na aba 1 e sortNameAsc na aba 2.
    // Para CADA aba e CADA visão (QTreeView e QListView separadamente), compare
    // a sequência real com a lista literal correspondente — nunca compare uma
    // visão com a outra.
    // Alterne Lista/Grade com listViewAction/gridViewAction, trigger refreshAction
    // e repita a comparação literal. Volte para cada aba e confirme que só a
    // ação daquela ordem está checada.
    // Clique no cabeçalho da coluna 0 na aba 1 (invertendo para asc) e confirme
    // que sortNameAsc passa a ser a marcação ativa e a sequência vira
    // { "a1.txt", "b1.txt", "c1.txt" } nas duas visões.
}
```

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R mainwindow_test`
Expected: FAIL — ações de ordenação ausentes.

- [ ] **Step 3: Implementar**

Crie as oito ações no `viewMenu` após Lista/Grade, com os textos do spec e `objectName`s exatos. Cada `triggered` chama `currentTab()->setSort(coluna, direção)`. `syncSortActions()` lê a aba ativa e marca apenas a ação correspondente; conecte `QTabWidget::currentChanged` e `FileTab::sortChanged` a ele. Use `QSignalBlocker` ao ajustar checagens.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R mainwindow_test`
Expected: PASS, com ordem real conferida nas duas visões e após `Atualizar`.

- [ ] **Step 5: Commit**

```bash
git add src/MainWindow.h src/MainWindow.cpp tests/mainwindow_test.cpp
git commit -m "feat: ordenação sincronizada entre menu, lista e grade"
```

## Sprint 3 — Colunas e legendas

### Task 5: Escolha individual das colunas

**Files:**
- Modify: `src/UiPreferences.h`, `src/UiPreferences.cpp`
- Modify: `src/MainWindow.cpp`
- Test: `tests/uipreferences_test.cpp`, `tests/mainwindow_test.cpp`

**Interfaces:**
- Consumes: `columns` (`QStringList`), `defaultUiPreferences(UiPreset)`.
- Produces:
  - `QStringList normalizeColumns(const QStringList &input, UiPreset preset)`.
  - Quatro `QCheckBox` no diálogo: `customColumn0` (desabilitado, checado), `customColumn1`, `customColumn2`, `customColumn3`.

- [ ] **Step 1: Escrever testes falhantes**

Em `tests/uipreferences_test.cpp`:

```cpp
void UiPreferencesTest::normalizeColumnsSanitizes()
{
    QCOMPARE(normalizeColumns({"0", "0", "9"}, UiPreset::Balanced), QStringList{"0"});
    QCOMPARE(normalizeColumns({"1", "2"}, UiPreset::Balanced), QStringList{"0", "1", "2"});
    QCOMPARE(normalizeColumns({}, UiPreset::Compact),
             defaultUiPreferences(UiPreset::Compact).columns);
}
```

Em `tests/mainwindow_test.cpp`, reescreva `customizeDialogPersistsManualAdjustments`: marque `customColumn0`+`customColumn3`, aceite, confirme `ui/columns == {"0","3"}`, reabra o diálogo e veja `customColumn3` checado e `customColumn1`/`customColumn2` limpos. Após reiniciar `MainWindow`, confirme **duas** colunas visíveis na `QTreeView` (índices `0` e `3`) e que as colunas `1` (`Tamanho`) e `2` (`Tipo`) continuam ocultas.

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R 'uipreferences_test|mainwindow_test'`
Expected: FAIL — `normalizeColumns` e as caixas não existem.

- [ ] **Step 3: Implementar**

`normalizeColumns()` filtra `0..3`, remove duplicatas, ordena e insere `0` quando ausente; se o resultado ficar vazio, devolve o default do preset. Em `openCustomizeDialog()`, troque `customColumns` pelas quatro caixas e monte a lista no aceite. `loadUiPreferences()` passa a usar `normalizeColumns()`.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R 'uipreferences_test|mainwindow_test'`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/UiPreferences.h src/UiPreferences.cpp src/MainWindow.cpp tests/uipreferences_test.cpp tests/mainwindow_test.cpp
git commit -m "feat: escolha individual das colunas visíveis"
```

### Task 6: Legendas de tamanho e modificação na grade

**Files:**
- Modify: `src/UiPreferences.h`, `src/UiPreferences.cpp`
- Modify: `src/MainWindow.cpp`, `src/FileTab.cpp`
- Test: `tests/uipreferences_test.cpp`, `tests/filetab_test.cpp`

**Interfaces:**
- Consumes: `ThumbnailProxy::data()` nas colunas 1 e 3, `iconSize`/zoom existentes.
- Produces:
  - Campos `bool gridSize = false`, `bool gridDate = false` e chaves `ui/gridSize`, `ui/gridDate`.
  - `FileTab::setGridLegends(bool size, bool date)`, `bool gridSize() const`, `bool gridDate() const`.
  - Ações checáveis `gridSizeAction`, `gridDateAction` no fim do `viewMenu`.
  - `GridLegendDelegate` local em `FileTab.cpp`, seguindo o padrão de `DensityDelegate`.

- [ ] **Step 1: Escrever testes falhantes**

```cpp
void FileTabTest::gridLegendsReserveCellHeightAtBothZooms()
{
    // Nomes longos, duas legendas ligadas via setGridLegends(true, true).
    // Para tab.setZoom(50) e tab.setZoom(200):
    //   delegate = m_icons->itemDelegate();
    //   QVERIFY(delegate->sizeHint(option, index).height() >= base + 2 * lineHeight);
    //   QVERIFY(m_icons->gridSize().height() >= delegate->sizeHint(option, index).height());
    //   QCOMPARE(m_icons->iconSize(), tamanho esperado para o zoom).
}
```

Em `tests/uipreferences_test.cpp`, inclua `gridSize`/`gridDate` no round-trip e confirme `false` quando as chaves estiverem ausentes.

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R 'uipreferences_test|filetab_test'`
Expected: FAIL — campos e `setGridLegends` ausentes.

- [ ] **Step 3: Implementar**

Persista as duas chaves com `saveUiPreferences()` e reaplique em `applyUiPreferences()`. No delegate, pinte nome e linhas opcionais e acrescente `QFontMetrics::height()` por legenda em `sizeHint()`. Se o teste mostrar `gridSize()` fixo menor que o `sizeHint()`, ajuste `setZoom()` para somar a altura das legendas ao `gridSize` calculado a partir de `m_gridBase`, sem tocar em `iconSize`; chame `setZoom(m_zoom)` ao final de `setGridLegends()` para reaplicar. Conecte as ações do menu ao setter e à persistência imediata.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R 'uipreferences_test|filetab_test'`
Expected: PASS em zoom baixo e alto.

- [ ] **Step 5: Commit**

```bash
git add src/UiPreferences.h src/UiPreferences.cpp src/MainWindow.cpp src/FileTab.cpp tests/uipreferences_test.cpp tests/filetab_test.cpp
git commit -m "feat: legendas de tamanho e data na grade"
```

## Sprint 4 — Paleta, janela estreita e fechamento

### Task 7: Paleta em runtime, verificação final e benchmark

**Files:**
- Modify: `tests/mainwindow_test.cpp`
- Modify: `docs/superpowers/validation/2026-11-28-menus-barra-e-preferencias.md` (criar)

**Interfaces:**
- Consumes: menus e ações das Tasks 1–6, `ThemeManager` existente.
- Produces: relatório de validação com CTest Debug/Release e medianas do bench.

- [ ] **Step 1: Escrever teste falhante**

```cpp
void MainWindowTest::paletteChangeKeepsMenusAndFocusReadable()
{
    // Abra um dos menus, marque uma ação de ordenação e uma legenda.
    // Confirme: marcas inalteradas, ações habilitadas, botões com hasFocus()
    // após Tab e nenhuma ação com texto vazio nos dois menus.
    // Se passar de primeira, registre como cobertura de regressão — não é o
    // teste de recarga em runtime.
}

void MainWindowTest::liveThemeReloadKeepsOpenMenuState()
{
    // Escreva palette-A.json no caminho observado e chame setThemeFile() uma
    // única vez para iniciar o watch. Abra viewMenu e deixe aberto.
    // Sobrescreva palette-B.json NO MESMO CAMINHO, sem chamar setThemeFile()
    // novamente. QFileSystemWatcher + debounce de 150 ms deve recarregar.
    // QTRY_VERIFY_WITH_TIMEOUT: qApp->palette().color(QPalette::Window) mudou
    // de A para B; marcas de ordenação/legenda continuam as mesmas; o menu
    // continua visível; Tab ainda alcança viewMenuButton com foco visível.
}
```

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R mainwindow_test`
Expected: FAIL — caso ainda não implementado/registrado.

- [ ] **Step 3: Implementar o necessário e validar tudo**

Ajuste apenas o que o teste revelar (estados devem vir das `QAction`s, não de stylesheet manual). Para a recarga ao vivo, dependa apenas do `QFileSystemWatcher`/debounce já existente em `ThemeManager`: **não chame `setThemeFile()` na segunda escrita**. Rode o suite completo nos dois builds e o benchmark Release com o protocolo dos Global Constraints.

Run:
```bash
cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure
cmake --build build-release -j4 && ctest --test-dir build-release --output-on-failure
```
Expected: 100% dos testes em ambos, zero warnings.

- [ ] **Step 4: Escrever o relatório e commitar**

Registre comandos, contagem de testes, tempos, medianas e capturas dos menus nos três presets e a 320 px em `docs/superpowers/validation/2026-11-28-menus-barra-e-preferencias.md`.

```bash
git add tests/mainwindow_test.cpp docs/superpowers/validation/2026-11-28-menus-barra-e-preferencias.md
git commit -m "docs: valida menus e preferências da Issue #20"
```
