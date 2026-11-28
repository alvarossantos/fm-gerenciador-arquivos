# Identidade visual com três presets (Issue #18) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Uma única interface `fm` com três presets selecionáveis (Compacta, Equilibrada, Avançada), preferências persistentes e diálogo de propriedades não bloqueante.

**Architecture:** `UiPreferences` concentra enum/struct/defaults/persistência; `MainWindow` aplica toolbar, breadcrumb e `Restaurar padrões`; `FileTab` aplica densidade, colunas, ícones, prévia, grupos da lateral e detalhe dos menus; `PropertiesDialog` faz varredura em `QThread` com cancelamento cooperativo. Nenhuma classe de janela é duplicada.

**Tech Stack:** C++17, Qt6 Widgets/Test, `QSettings`, `QThread`, `std::atomic_bool`, `QSignalSpy`.

**Spec:** `docs/superpowers/specs/2026-11-27-identidade-visual-presets-design.md`

## Global Constraints

- Branch: `feat/identidade-visual-presets`, base `origin/main` em `b65fd92` (PR #17, Issue #6).
- Nenhuma dependência nova; apenas Qt6 Widgets/Test já usados. **Sem QtConcurrent.**
- `QSettings("fm","fm")`; chaves exatas: `ui/preset`, `ui/density`, `ui/iconSize`, `ui/columns`, `ui/preview`.
- Defaults: Compacta = `density=0, iconSize=32, columns={"0"}, preview=false`; Equilibrada = `density=1, iconSize=48, columns={"0","1","2","3"}, preview=false`; Avançada = `density=2, iconSize=64, columns={"0","1","2","3"}, preview=true`.
- Trocar de preset **descarta** ajustes manuais e reaplica em todas as abas; chave ausente ou inválida usa o default do preset selecionado.
- `Restaurar padrões` (texto exato) volta à Equilibrada, limpa ajustes, zera zoom para 100%, reaplica em todas as abas e persiste após recriar `MainWindow`.
- Controle recolhido da toolbar: a mesma `QAction` permanece no menu `⋮` e nos atalhos; nunca `setVisible(false)` na ação.
- Cabeçalhos de grupo da lateral são marcados com `PlaceSectionRole` e ignorados por `updatePlacesSelection()`.
- Varredura de pasta: tamanho aparente, inclui ocultos, não segue symlinks, conta pastas recursivamente; **nenhum `wait()` na thread da interface**.
- Sem warnings em Debug/Release; CTest completo em ambos.
- `--bench` Release: **sete rodadas no total, primeira descartada, mediana das seis restantes**; baseline **92,0 ms**.
- Commits frequentes, um por Task. Mensagens em pt-BR ou padrão `feat:`/`test:` do repositório.

## Review Focus

| Entrada / condição | Comportamento esperado | Teste que fixa |
|---|---|---|
| Chave `QSettings` ausente ou corrompida | Default do preset selecionado, sem travar | Task 1, step 1 |
| Controle recolhido na Compacta / janela estreita | `QAction` compartilhada viva no menu e no teclado | Task 3, step 1 |
| Cabeçalho `Lugares` na lateral | Nunca confundido com lugar ancestral | Task 2, step 1 |
| Fechar diálogo durante varredura de pasta grande | Cancelamento assíncrono, sem `wait()`, reabertura limpa | Task 7, step 1 |
| Paleta de tema inválida + foco/desabilitado | Estados legíveis, última paleta válida preservada | Task 8, step 1 |

---

## Sprint 1 — Preferências, densidade e presets

### Task 1: Modelo `UiPreferences` e persistência

**Files:**
- Create: `src/UiPreferences.h`, `src/UiPreferences.cpp`, `tests/uipreferences_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: nada.
- Produces:
  - `enum class UiPreset { Compact, Balanced, Advanced };`
  - `struct UiPreferences { UiPreset preset; int density; int iconSize; QStringList columns; bool preview; };`
  - `UiPreferences defaultUiPreferences(UiPreset preset);`
  - `UiPreferences loadUiPreferences(QSettings &settings);`
  - `void saveUiPreferences(QSettings &settings, const UiPreferences &prefs);`
  - `QString uiPresetKey(UiPreset preset);` → `"compact"`, `"balanced"`, `"advanced"`.

- [ ] **Step 1: Escrever teste falhante `tests/uipreferences_test.cpp`**

Use `QTemporaryDir`, `qputenv("XDG_CONFIG_HOME", ...)` antes de construir `QSettings`, e `QScopeGuard` para restaurar o valor anterior. Casos:
  - `defaultsMatchMockups`: confira os três trios `density/iconSize/preview/columns` exatos dos Global Constraints para `defaultUiPreferences`.
  - `missingKeyUsesSelectedPreset`: com `ui/preset=compact` e sem `ui/iconSize`, `loadUiPreferences().iconSize == 32`; com `ui/preset=advanced` e sem `ui/preview`, `preview == true`.
  - `invalidValuesFallBackToPreset`: gravar `ui/density=99`, `ui/preset=balanced`; resultado `density == 1`.
  - `roundTrip`: `saveUiPreferences` seguido de `loadUiPreferences` preserva todos os campos.

- [ ] **Step 2: Rodar e confirmar falha de compilação**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R uipreferences_test`
Expected: FAIL — `#include "UiPreferences.h"` não encontrado.

- [ ] **Step 3: Implementar o modelo em `src/UiPreferences.h/.cpp`**

`loadUiPreferences` lê `ui/preset` com `QLatin1String`; valor não reconhecido cai em `Balanced`. Para cada campo, use `QSettings::value(key, defaultValueDoPreset)`; `columns` só é aceita se não estiver vazia e todos os itens forem índices 0..3. `saveUiPreferences` grava os cinco valores. Sem `Q_OBJECT`.

- [ ] **Step 4: Ligar ao CMake**

Em `CMakeLists.txt`: adicionar `src/UiPreferences.cpp` às fontes de `fm`, `filetab_test` e `mainwindow_test`; criar `uipreferences_test` com `tests/uipreferences_test.cpp src/UiPreferences.cpp`, include dir `src`, link `Qt6::Widgets Qt6::Test`, `add_test`, ENVIRONMENT `QT_QPA_PLATFORM=offscreen`.

- [ ] **Step 5: Rodar e confirmar passagem**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R uipreferences_test`
Expected: PASS (4 casos).

- [ ] **Step 6: Commit**

```bash
git add src/UiPreferences.h src/UiPreferences.cpp tests/uipreferences_test.cpp CMakeLists.txt
git commit -m "test: modelo e persistência das preferências de interface"
```

---

### Task 2: `FileTab::applyUiPreferences` e grupos da lateral

**Files:**
- Modify: `src/FileTab.h`, `src/FileTab.cpp`, `tests/filetab_test.cpp`

**Interfaces:**
- Consumes: `UiPreferences`, `defaultUiPreferences` (Task 1).
- Produces:
  - `void FileTab::applyUiPreferences(const UiPreferences &prefs);`
  - `static constexpr int PlaceSectionRole = Qt::UserRole + 1;`
  - `QListWidgetItem` de seção: `setData(PlaceSectionRole, QStringLiteral("section"))`, `setFlags(item->flags() & ~Qt::ItemIsSelectable)`.
  - `m_places->setObjectName("places")` permanece; seções têm `objectName` vazio.

- [ ] **Step 1: Escrever testes falhantes em `tests/filetab_test.cpp`**

  - `presetChangesIconAndColumns`: criar `FileTab` sobre `QTemporaryDir` com 5 arquivos; chamar `applyUiPreferences(defaultUiPreferences(UiPreset::Compact))`; conferir `m_icons->iconSize() == QSize(32,32)`, só a coluna 0 visível em `m_tree->header()`, grid proporcional. Repetir com `Advanced`: 64 px e quatro colunas.
  - `densityChangesRowHeight`: `density=0` e `density=2` devem produzir `sizeHint().height()` diferente para um item de `m_tree`.
  - `previewFollowsPreferences`: `Advanced` mostra `previewPanel`; `Balanced` esconde.
  - `sectionHeadersAreNotPlaces`: após `applyUiPreferences(Balanced)`, `updatePlacesSelection()` deve selecionar o lugar ancorado em `m_path`, nunca um item com `data(PlaceSectionRole)=="section"`; use `setPlacesForTests` com `QDir::homePath()` e um subdir.

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R filetab_test`
Expected: FAIL — `applyUiPreferences` não declarado.

- [ ] **Step 3: Implementar em `FileTab.h/.cpp`**

Guardar `UiPreferences m_ui`. Inserir seções `Lugares`, `Da aba`, `Dispositivos` no `m_places` com `PlaceSectionRole`; esconder grupos conforme preset (Compacta: só Lugares + `Mais…`; Equilibrada: Lugares + Da aba; Avançada: todos + `Personalizar…`). Recalcular `m_treeIconBase`, `m_gridIconBase`, `m_gridBase` a partir de `iconSize` antes de chamar `setZoom`. Aplicar densidade com um `QStyledItemDelegate` local `DensityDelegate` em `FileTab.cpp` cujo `sizeHint()` devolve 22/28/34 px para `density` 0/1/2 (sem tocar em cor), e com `m_places->setSpacing` na lateral. Esconder colunas com `m_tree->header()->setSectionHidden(i, true/false)`. `updatePlacesSelection()` começa com `if (it->data(PlaceSectionRole).isValid()) continue;`.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R filetab_test`
Expected: PASS (incluindo todos os casos existentes).

- [ ] **Step 5: Commit**

```bash
git add src/FileTab.h src/FileTab.cpp tests/filetab_test.cpp
git commit -m "feat: aplica densidade, colunas, ícones e grupos por preset"
```

---

### Task 3: Menu de presets, toolbar compartilhada e breadcrumb

**Files:**
- Modify: `src/MainWindow.h`, `src/MainWindow.cpp`, `tests/mainwindow_test.cpp`

**Interfaces:**
- Consumes: `loadUiPreferences`, `saveUiPreferences`, `uiPresetKey` (Task 1); `FileTab::applyUiPreferences` (Task 2).
- Produces:
  - `void MainWindow::applyUiPreferences(const UiPreferences &prefs);`
  - `UiPreferences MainWindow::uiPreferences() const;`
  - `QAction *m_moreAct` (objectName `moreMenu`) com menu; submenu `Interface` (objectName `interfaceMenu`); ações `presetCompactAct`, `presetBalancedAct`, `presetAdvancedAct`, `restoreDefaultsAct` (texto exato `Restaurar padrões`), as três de preset checáveis e a de restaurar não checável.
  - `QToolBar *m_toolbar`; `int m_breadcrumbLimit` (Compacta 2, Equilibrada 4, Avançada -1).
  - Ações recolhidas são adicionadas ao `moreMenu` com o mesmo `text()` e a mesma `QAction`.

- [ ] **Step 1: Escrever testes falhantes em `tests/mainwindow_test.cpp`**

  - `sharedActionsSurviveToolbarRemoval`: construir janela, trocar para Compacta pelo `interfaceMenu`; achar `m_iconsAct` e `m_zoomInAct` por `findAction`; conferir `m_toolbar->widgetForAction(a) == nullptr`, existir no `moreMenu` uma ação com o mesmo `text()` e `shortcut()`, e `a->shortcut()` inalterado; disparar `a->trigger()` e conferir que a visão mudou e o zoom mudou em todas as abas.
  - `narrowWindowKeepsActionsAndAncestors`: redimensionar para 320 px com Compacta; conferir que "Pré-visualização" e zoom continuam no `moreMenu`, `Ctrl+L` com `QTest::keyClick` foca `pathEdit`, e `breadcrumbOverflow` tem ação para o ancestral oculto.
  - `breadcrumbLimitPerPreset`: com caminho de 5 segmentos, Compacta mostra ≤2 `breadcrumbButton`s e `breadcrumbOverflow` visível; Avançada mostra mais que Compacta.
  - `switchPresetResetsManualAdjustments`: ajustar zoom e ícones, trocar para Avançada, conferir `uiPreferences()` com defaults Avançada (preview true, icon 64).

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R mainwindow_test`
Expected: FAIL — menu `interfaceMenu` ausente.

- [ ] **Step 3: Implementar toolbar e menu em `MainWindow.cpp`**

Guardar `m_toolbar = tb`. Criar `m_moreAct` com `QMenu` e submenu `Interface`;Actions para os três presets e restaurar. Conectar preset ao novo `applyUiPreferences`, gravar via `saveUiPreferences`. Para recolher controles usar **somente** `m_toolbar->removeAction(a)` / `m_toolbar->insertAction(anchor, a)`; as ações já estão em `addAction(this)` (atalhos) e passam a ser adicionadas ao menu `⋮` quando recolhidas. Em `rebuildBreadcrumb`, aplicar `m_breadcrumbLimit` antes do ciclo de largura existente: esconder do índice `limit` em diante e mandá-los para `m_overflow`. `applyUiPreferences` chama `FileTab::applyUiPreferences` em todas as abas e reseta zoom via `m_zoomResetAct->trigger()`.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R mainwindow_test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/MainWindow.h src/MainWindow.cpp tests/mainwindow_test.cpp
git commit -m "feat: menu de presets, ações compartilhadas e breadcrumb por preset"
```

---

### Task 4: Restaurar padrões e persistência após reinício

**Files:**
- Modify: `src/MainWindow.cpp`, `tests/mainwindow_test.cpp`

**Interfaces:**
- Consumes: `defaultUiPreferences(UiPreset::Balanced)`, `uiPreferences()` (Tasks 1 e 3).
- Produces: nenhuma nova API; `restoreDefaultsAct` dispara `applyUiPreferences(defaultUiPreferences(Balanced))`.

- [ ] **Step 1: Escrever teste falhante `restoreDefaultsPersistsAfterRestart`**

Com `XDG_CONFIG_HOME` temporário: criar janela, trocar para Avançada, ajustar manualmente, clicar `restoreDefaultsAct`; conferir `uiPreferences() == defaultUiPreferences(Balanced)` e todas as abas com `zoom()==100`. Destruir a janela, criar outra `MainWindow` com o mesmo `QSettings`, conferir `uiPreferences()` ainda Equilibrada e `ui/preset == "balanced"` no arquivo de configuração. Restaurar env com `QScopeGuard`.

- [ ] **Step 2: Rodar e confirmar falha**

Run: `ctest --test-dir build-debug --output-on-failure -R mainwindow_test`
Expected: FAIL — ação não gravava os defaults.

- [ ] **Step 3: Implementar**

`restoreDefaultsAct` chama `applyUiPreferences(defaultUiPreferences(UiPreset::Balanced))`, emite `statusMessage` via `statusBar()->showMessage(tr("Padrões restaurados"))` e grava `window/geometry` normalmente no `closeEvent`. Garantir que `applyUiPreferences` sempre persiste antes de retornar.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R mainwindow_test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/MainWindow.cpp tests/mainwindow_test.cpp
git commit -m "test: restauração persiste após reiniciar o fm"
```

---

## Sprint 2 — Menus e propriedades

### Task 5: Detalhe do menu de contexto por preset

**Files:**
- Modify: `src/FileTab.h`, `src/FileTab.cpp`, `tests/filetab_test.cpp`

**Interfaces:**
- Consumes: `m_ui` (Task 2).
- Produces:
  - `QString FileTab::menuLabel(const QString &label, const QString &shortcut) const;`
  - `void FileTab::compactMenuSeparators(QMenu *menu) const;`
  - Ação `Colar` com `objectName = "pasteAction"` e `setData(m_path)`.

- [ ] **Step 1: Escrever testes falhantes**

Usar `setContextMenuHookForTests` para capturar o menu. 
  - `menuShortcutsByPreset`: em `Compact`, nenhum rótulo contém `\t`; em `Balanced` e `Advanced`, "Abrir" contém `\tEnter` e "Propriedades" contém `\tAlt+Enter`.
  - `menuTargetsUnchanged`: para arquivo, pasta, seleção múltipla e área vazia, os rótulos centrais (`Abrir com…`, `Copiar caminho`, `Compactar…`, `Propriedades`, `Colar`) continuam presentes nos três presets, e `pasteAction->data().toString() == tab->currentPath()`.

- [ ] **Step 2: Rodar e confirmar falha**

Run: `ctest --test-dir build-debug --output-on-failure -R filetab_test`
Expected: FAIL — `menuLabel` ausente.

- [ ] **Step 3: Implementar**

`menuLabel` retorna `label` em Compact e `label + "\t" + shortcut` nos outros (string vazia ⇒ `label`). Aplicar nos rótulos de `showContextMenu` e `addViewMenu`/`addOpenWithMenu`. Em Compact, `compactMenuSeparators` remove separadores consecutivos e no início/fim. Não alterar alvos nem condicionalidade das ações — apenas texto.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R filetab_test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/FileTab.h src/FileTab.cpp tests/filetab_test.cpp
git commit -m "feat: detalhe do menu de contexto por preset"
```

---

### Task 6: `PropertiesDialog` — interface e abertura

**Files:**
- Create: `src/PropertiesDialog.h`, `src/PropertiesDialog.cpp`, `tests/properties_dialog_test.cpp`
- Modify: `src/FileTab.h`, `src/FileTab.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `UiPreferences` (Task 1).
- Produces:
  - `class PropertiesDialog : public QDialog` com
    `explicit PropertiesDialog(const QString &path, const UiPreferences &prefs, QWidget *parent = nullptr);`,
    `QString path() const;`, `void requestCancel();`
  - Hook: `using PropertiesDialogHook = std::function<void(PropertiesDialog *)>; void setPropertiesDialogHookForTests(PropertiesDialogHook hook);` em `FileTab.h`.
  - ObjectNames: `propertiesSections`, `propertiesGeneral`, `propertiesSize`, `propertiesFolderCount`, `propertiesStatus`, `copyPathButton`, `openParentButton`.

- [ ] **Step 1: Escrever teste falhante `tests/properties_dialog_test.cpp`**

  - `layoutByPreset`: para um arquivo temporário, Compacta usa `QToolBox`, Equilibrada `QTabWidget` com abas `Geral/Datas/Permissões`, Avançada mostra as três seções simultâneas.
  - `fileFields`: `propertiesSize` mostra o tamanho via `QLocale().formattedDataSize`; `openParentButton` habilitado; `copyPathButton` copia para `QGuiApplication::clipboard()`.
  - `nonModalFromMenu`: em `filetab_test`, abrir contexto em um arquivo, clicar "Propriedades" e conferir pelo hook que o diálogo é `isVisible()` e `FileTab` continua respondendo a `QTest::keyClick`.

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R properties_dialog_test`
Expected: FAIL — classe não existe.

- [ ] **Step 3: Implementar diálogo e ligar `FileTab::showProperties`**

Diálogo `Qt::NonModal`, cabeçalho com ícone de `QMimeDatabase`, nome e tipo; botões `Copiar caminho` e `Abrir pasta pai`. Trocar o `QMessageBox::information` de `showProperties` por `new PropertiesDialog(path, m_ui, this)` + `show()` + chamada ao hook de teste. Registrar `src/PropertiesDialog.cpp` nas fontes de `fm`, `filetab_test`, `mainwindow_test` e no novo alvo `properties_dialog_test` (link `Qt6::Widgets Qt6::Test`, ENVIRONMENT offscreen).

- [ ] **Step 4: Rodar e confirmar passagem**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R "properties_dialog_test|filetab_test"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/PropertiesDialog.h src/PropertiesDialog.cpp src/FileTab.h src/FileTab.cpp CMakeLists.txt tests/properties_dialog_test.cpp tests/filetab_test.cpp
git commit -m "feat: janela de propriedades não modal por preset"
```

---

### Task 7: Varredura assíncrona cooperativa

**Files:**
- Modify: `src/PropertiesDialog.h`, `src/PropertiesDialog.cpp`, `tests/properties_dialog_test.cpp`

**Interfaces:**
- Consumes: `PropertiesDialog` (Task 6).
- Produces:
  - `class DirectoryScanWorker : public QObject` (definido em `PropertiesDialog.cpp`, visível só no `.cpp`).
  - `signals: void scanFinished(const QString &path, qint64 bytes, int folders, const QStringList &warnings);`
  - `static int activeScansForTests();`
  - `using ScanItemHook = std::function<void()>; void setScanItemHookForTests(ScanItemHook hook);`
  - `std::shared_ptr<std::atomic_bool>` como token de cancelamento compartilhado com o worker.

- [ ] **Step 1: Escrever teste falhante `closeDuringScanDoesNotBlockAndReopenWorks`**

Criar árvore com 200 arquivos em `QTemporaryDir`. Registrar `ScanItemHook` que sinaliza `QEventLoop::quit()` na primeira chamada e depois bloqueia até `release`. Abrir `PropertiesDialog` na pasta; esperar o hook; chamar `close()` e medir que retorna imediatamente (`QElapsedTimer::elapsed() < 200`); liberar o hook; processar eventos até `activeScansForTests() == 0`; abrir diálogo para outro arquivo e conferir `scanFinished` com o tamanho correto via `QSignalSpy`. `QScopeGuard` restaura o hook.

- [ ] **Step 2: Rodar e confirmar falha**

Run: `ctest --test-dir build-debug --output-on-failure -R properties_dialog_test`
Expected: FAIL — sinal `scanFinished` inexistente.

- [ ] **Step 3: Implementar worker e cancelamento**

Worker roda em `QThread` próprio; a cada item verifica o token atômico e chama o hook de teste (se setado). Conta `QFileInfo::size` de arquivos, incrementa `folders` para diretórios recursivamente, usa `QDirIterator::NoIteratorFlags` (não segue symlinks), inclui `QDir::Hidden`, ignora subpastas ilegíveis acumulando aviso. Emite `scanFinished`; `QThread::quit()` é chamado pelo próprio worker ao terminar e `QThread::finished` conecta a `deleteLater` do worker e da thread. Destrutor do diálogo apenas `m_cancel->store(true)` — **sem `wait()`**. A conexão sinal/slot é automática entre threads; nenhum ponteiro bruto do diálogo no worker.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R properties_dialog_test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/PropertiesDialog.h src/PropertiesDialog.cpp tests/properties_dialog_test.cpp
git commit -m "test: varredura de propriedades cancelável sem bloquear a UI"
```

---

## Sprint 3 — Tema, verificação e PR

### Task 8: Estados de foco e desabilitado no `ThemeManager`

**Files:**
- Modify: `src/ThemeManager.cpp`, `tests/theme_manager_test.cpp`

**Interfaces:**
- Consumes: paleta aplicada por `ThemeManager`.
- Produces: nenhuma API nova; apenas QSS adicional.

- [ ] **Step 1: Escrever teste falhante `focusAndDisabledStatesUsePalette`**

Com JSON de paleta válido, conferir que o QSS resultante contém regras para `QTreeView:focus`, `QListView:focus`, `QMenu::item:disabled` e `QPushButton:disabled`, e que usam `Highlight`/`HighlightedText`/`Disabled` da paleta. Repetir com JSON inválido e conferir que a última paleta válida continua aplicada (caso existente do teste).

- [ ] **Step 2: Rodar e confirmar falha**

Run: `ctest --test-dir build-debug --output-on-failure -R theme_manager_test`
Expected: FAIL — regras ausentes.

- [ ] **Step 3: Implementar QSS mínimo**

Adicionar apenas regras de contorno/foco e cor de itens desabilitados, sem alterar cores de fundo dos presets. Manter a política atual de JSON inválido.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: `cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure -R theme_manager_test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/ThemeManager.cpp tests/theme_manager_test.cpp
git commit -m "feat: estados de foco e desabilitado na paleta"
```

---

### Task 9: Verificação completa, bench e PR

**Files:**
- Create: `docs/superpowers/validation/2026-11-27-identidade-visual-presets.md`
- Modify: nenhum código-fonte.

**Interfaces:**
- Consumes: tudo das Tasks 1–8.

- [ ] **Step 1: Suíte Debug e Release sem warnings**

```bash
cmake --build build-debug -j4 2>&1 | tee /tmp/opencode/fm-i18-debug.log
cmake --build build-release -j4 2>&1 | tee /tmp/opencode/fm-i18-release.log
grep -iE 'warning:|error:' /tmp/opencode/fm-i18-debug.log /tmp/opencode/fm-i18-release.log || echo "sem warnings"
ctest --test-dir build-debug --output-on-failure
ctest --test-dir build-release --output-on-failure
```
Expected: zero warnings/errors; todos os testes PASS (incluindo `bench_smoke`).

- [ ] **Step 2: Bench com a redação padronizada**

Sete rodadas no total, primeira descartada, mediana das seis restantes:

```bash
for i in $(seq 1 7); do
  T=$(mktemp -d /tmp/opencode/fm-bench-XXXXXX)
  XDG_CONFIG_HOME="$T" QT_QPA_PLATFORM=offscreen timeout 10 ./build-release/fm --bench "$T/bench" 2>&1 \
    | grep -o 'tempo até a primeira pintura: [0-9]\+ ms' | grep -o '[0-9]\+ ms'
  rm -rf "$T"
done | tee /tmp/opencode/fm-i18-bench-final.txt
tail -6 /tmp/opencode/fm-i18-bench-final.txt | sed 's/ ms//' | sort -n \
  | awk '{a[NR]=$1} END{printf "mediana: %.1f ms\n", (a[3]+a[4])/2}'
```
Expected: mediana registrada; comparar com baseline **92,0 ms**.

- [ ] **Step 3: Validação visual no iNiR/Niri**

Launcher existente com `--theme-file ~/.local/state/quickshell/user/generated/app-palette.json`. Capturar um quadro por preset (barra, lateral, menu, propriedades) e um com janela estreita + `Ctrl+L`. Trocar a paleta ao vivo e conferir que os três presets acompanham. Anexar ao documento de validação.

- [ ] **Step 4: Escrever relatório**

`docs/superpowers/validation/2026-11-27-identidade-visual-presets.md`: ambiente, comandos, contagem de testes, valores de bench (92,0 ms → mediana final), capturas, resultados da troca de paleta e link para a side-by-side `2026-11-27-identidade-visual-propostas.html`.

- [ ] **Step 5: Commit e PR**

```bash
git add docs/superpowers/validation/2026-11-27-identidade-visual-presets.md
git commit -m "docs: valida presets de interface e registra benchmark"
git push -u origin feat/identidade-visual-presets
gh pr create --base main --head feat/identidade-visual-presets \
  --title "Refina a identidade visual do fm e adiciona presets de interface" \
  --body "Closes #18"
```
Expected: PR aberto com `Closes #18`, sem push direto em `main`.

---

## Self-review

- **Cobertura do spec:** enum/struct/persistência (T1), preset troca/restauração (T3/T4), toolbar/ações compartilhadas/breadcrumb/estreita (T3), lateral/cabeçalhos (T2), menus (T5), propriedades por preset (T6), varredura cooperativa (T7), tema (T8), verificação/bench/PR (T9). Sem lacunas.
- **Tipos e nomes:** `UiPreferences` é consumido com as mesmas assinaturas em T1→T7; `applyUiPreferences` é o único ponto de aplicação; `restoreDefaultsAct` é definido em T3 e usado em T4; `PropertiesDialog` é definido em T6 e estendido em T7.
- **Proporção:** o plano decide nomes, valores e testes; corpos de algoritmo ficaram fora, exceto a política de varredura que o spec fixou.
