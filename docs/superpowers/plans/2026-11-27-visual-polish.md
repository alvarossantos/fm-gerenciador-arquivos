# Visual Polish (Issue #5) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Esconder a faixa de abas com uma aba só, adicionar zoom na grade e refinar lista/grade sem mudar a navegação já entregue.

**Architecture:** Ajustes in-place em `MainWindow` (faixa, ações de zoom) e `FileTab` (zoom e propriedades das visões). Nenhuma classe nova, nenhuma persistência. Testes QtTest existentes são estendidos.

**Tech Stack:** C++17, Qt 6 Widgets, QtTest, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-11-27-visual-polish-design.md`

## Global Constraints

- Build Release sem warnings; CTest em Debug e Release 100% verde.
- Sem persistência de modo/zoom neste recorte.
- PR referencia `#5`; sem push direto em `main`.
- Verificação visual no Umbriel/iNiR, `--bench` Release contra 180 ms (7 rodadas, descartar a 1ª, mediana das 6).

## Review Focus

- Janela estreita com uma aba: faixa escondida sem cortar toolbar nem breadcrumb — teste de geometria em `mainwindow_test`.
- Fechar a última aba: não deixa janela sem aba nem faixa órfã — teste de `closeTab` em `mainwindow_test`.
- Zoom mínimo/máximo: `iconSize`/`gridSize` não colapsam nem estouram — teste de limites em `filetab_test`.
- Alternar modo de visão com zoom aplicado: zoom sobrevive à troca lista↔grade — teste em `filetab_test`.
- Foco por teclado e paleta ao vivo: verificação manual no Umbriel/iNiR, registrada no protocolo (não automatizável com QtTest offscreen).

---

### Task 1: Faixa de abas com uma aba

**Files:**
- Modify: `src/MainWindow.cpp` (`addTab`, `closeTab`, construtor)
- Test: `tests/mainwindow_test.cpp`

**Interfaces:**
- Consumes: `m_tabs` (`QTabWidget*`), `addTab`, `closeTab` existentes.
- Produces: `QTabBar` escondido com `count() <= 1` e visível com `count() > 1`; ação/botão Nova aba e `Ctrl+T`/`Ctrl+W` continuam funcionando.

- [ ] **Step 1: Write the failing test**

Em `tests/mainwindow_test.cpp`, declarar `tabBarHiddenWithSingleTab()` e `closingDownToOneTabHidesBarAgain()`. Usar `MainWindow w(home)`, `w.show()`, `QApplication::processEvents()`. Localizar `QTabBar*` via `w.findChild<QTabBar*>()`. Afirmar: com uma aba `!bar->isVisible()`; após `Ctrl+T` (disparar o QAction "Nova aba") `bar->isVisible()`; após fechar até uma aba, `!bar->isVisible()`; o QAction "Nova aba" permanece `isVisible()` em ambos os estados.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j4 && env QT_QPA_PLATFORM=offscreen ./build/mainwindow_test tabBarHiddenWithSingleTab closingDownToOneTabHidesBarAgain -v1`
Expected: FAIL no primeiro `!bar->isVisible()`.

- [ ] **Step 3: Implement**

Em `src/MainWindow.cpp`, adicionar método privado `syncTabBarVisibility()` que chama `m_tabs->tabBar()->setVisible(m_tabs->count() > 1)`. Chamá-lo ao fim de `addTab()` e depois de remover aba em `closeTab()`. Não alterar `setTabsClosable` nem `setMovable`.

- [ ] **Step 4: Run test to verify it passes**

Run: comando do Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/MainWindow.cpp tests/mainwindow_test.cpp
git commit -m "feat: esconde faixa de abas com uma aba"
```

---

### Task 2: Zoom na grade

**Files:**
- Modify: `src/FileTab.h` (assinatura), `src/FileTab.cpp`, `src/MainWindow.cpp`, `src/MainWindow.h`
- Test: `tests/filetab_test.cpp`, `tests/mainwindow_test.cpp`

**Interfaces:**
- Consumes: `m_icons`, `m_tree` existentes.
- Produces: `void FileTab::setZoom(int percent)`; `int FileTab::zoom() const`; limites `[50, 200]`; `zoomChanged(int)` signal; ações `zoomInAct`, `zoomOutAct`, `zoomResetAct` na `MainWindow` com atalhos `Ctrl++`, `Ctrl+-`, `Ctrl+0`.

- [ ] **Step 1: Write the failing test**

Em `tests/filetab_test.cpp`, declarar `zoomChangesIconAndGridSize()` e `zoomClampsAndSurvivesModeSwitch()`.Localizar `QListView` via `findChild<QListView*>()`; chamar `setZoom(150)` e afirmar `zoom()==150`, `iconSize().width() > 72`, `gridSize().width() > 112`; chamar `setZoom(500)` e afirmar `zoom()==200`; `setZoom(10)` e afirmar `zoom()==50`; alternar `setIconMode(false)` e de volta `true`, afirmar `zoom()==150`. Em `mainwindow_test.cpp`, declarar `zoomActionsReachActiveTab()`: disparar a ação `zoomIn` e afirmar `zoom() > 100`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j4 && env QT_QPA_PLATFORM=offscreen ./build/filetab_test zoomChangesIconAndGridSize zoomClampsAndSurvivesModeSwitch -v1`
Expected: FAIL com "no member named setZoom".

- [ ] **Step 3: Implement**

Em `FileTab`, guardar `m_zoom = 100`; `setZoom(int)` limita a `[50,200]`, aplica `iconSize(QSize(72*pct/100, 72*pct/100))`, `gridSize(QSize(qMax(96,112*pct/100), qMax(96,112*pct/100)))`, `m_tree->setIconSize(...)`, emite `zoomChanged(m_zoom)`. `zoom()` retorna `m_zoom`. `setIconMode` não altera `m_zoom`. Em `MainWindow`, criar as três ações, conectá-las a `setZoom(currentTab()->zoom() ± 25)` / `setZoom(100)` na aba ativa e registrá-las com `addAction`.

- [ ] **Step 4: Run test to verify it passes**

Run: comandos do Step 2 e `env QT_QPA_PLATFORM=offscreen ./build/mainwindow_test zoomActionsReachActiveTab -v1`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/FileTab.h src/FileTab.cpp src/MainWindow.h src/MainWindow.cpp tests/filetab_test.cpp tests/mainwindow_test.cpp
git commit -m "feat: adiciona zoom na grade e na lista"
```

---

### Task 3: Refino de lista/grade e espaço do painel

**Files:**
- Modify: `src/FileTab.cpp` (construtor, painel de lugares)
- Test: `tests/filetab_test.cpp`

**Interfaces:**
- Consumes: `m_tree`, `m_icons`, `m_places` existentes.
- Produces: colunas numéricas alinhadas à direita (`QHeaderView::AlignRight` nas colunas 1–3), `m_tree->setAlternatingRowColors(true)`, `m_icons->setSpacing(8)`, `m_places->setMinimumWidth(190)`.

- [ ] **Step 1: Write the failing test**

Em `tests/filetab_test.cpp`, declarar `viewRefinementsAreApplied()`: localizar `QTreeView*`, `QListView*` e `QListWidget*`; afirmar `header()->sectionResizeMode(0)==QHeaderView::Stretch`, `tree->alternatingRowColors()==true`, `icons->spacing()==8`, `places->minimumWidth()>=190`. Para alinhamento, verificar `tree->model()->headerData(1, Qt::Horizontal, Qt::TextAlignmentRole).toInt() & Qt::AlignRight` — isso funciona em qualquer versão do Qt 6 sem depender de `QHeaderView::setSectionAlignment`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j4 && env QT_QPA_PLATFORM=offscreen ./build/filetab_test viewRefinementsAreApplied -v1`
Expected: FAIL na primeira asserção de cor alternada.

- [ ] **Step 3: Implement**

Ajustar o construtor de `FileTab`: `setAlternatingRowColors(true)` no `m_tree`, `setSpacing(8)` no `m_icons`, `m_places->setMinimumWidth(190)`, alinhamento à direita nas colunas 1–3 do header (usar `QHeaderView::setSectionAlignment` se disponível; caso contrário, documentar a limitação da versão do Qt e manter apenasLargeza/estiramento). Manter seleção e navegação intactas.

- [ ] **Step 4: Run test to verify it passes**

Run: comando do Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/FileTab.cpp tests/filetab_test.cpp
git commit -m "feat: refina leitura de lista e grade"
```

---

### Task 4: Verificação final e PR

**Files:**
- Modify: `docs/superpowers/validation/2026-11-26-umbriel.md` (resultados)
- Sem código novo.

**Interfaces:**
- Consumes: todas as tarefas acima.
- Produces: PR aberto referenciando `#5`.

- [ ] **Step 1: Run full verification**

```bash
cmake --build build -j4 && ctest --test-dir build --output-on-failure
cmake --build build-release -j4 && ctest --test-dir build-release --output-on-failure
```
Expected: 100% em ambos; `grep -c 'warning:'` nos logs de build = 0.

- [ ] **Step 2: Verificação visual e bench**

Seguir `docs/superpowers/validation/2026-11-26-umbriel.md`: capturas de lista/grade, foco por teclado, troca de paleta ao vivo, 7 rodadas de `--bench` em Release (descartar 1ª, mediana das 6 vs 180 ms). Registrar resultados no próprio arquivo.

- [ ] **Step 3: Revisão e PR**

Rodar revisão estática (Codex), corrigir achados, abrir PR da branch `feat/navegacao-visual` para `main` com `Closes #5` na descrição.

- [ ] **Step 4: Commit final**

```bash
git add docs/superpowers/validation/2026-11-26-umbriel.md
git commit -m "docs: registra validacao visual e bench da Issue 5"
```
