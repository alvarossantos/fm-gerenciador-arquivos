# Menus de contexto e criação (Issue #6) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Menus de contexto coerentes com o alvo (vazio, arquivo, pasta, seleção múltipla), criação por tipo/modelo, `Abrir com…` por MIME e submenu `Exibir` ligado às ações da janela.

**Architecture:** Tudo em `FileTab`, com a `MainWindow` injetando as `QAction`s de visualização. Um hook de inspeção de menu e hooks de launch/MIME/templates tornam os testes determinísticos sem tocar em processos reais nem nas associações da máquina.

**Tech Stack:** C++17, Qt 6 Widgets, QtTest, CMake/CTest, `gio`/`xdg-mime` via `QProcess`.

**Spec:** `docs/superpowers/specs/2026-11-27-menus-de-contexto-design.md`

## Global Constraints

- Build Release sem warnings; `ctest --test-dir build-release -V` verde (4/4 alvos).
- `QT_QPA_PLATFORM=offscreen` nos testes; pastas descartáveis (`QTemporaryDir`) para arquivos.
- Nenhum atalho novo: ações injetadas não entram em `MainWindow::addAction`.
- `Colar` sempre em `m_path`; alvo de item capturado no momento do menu.
- PR referencia `#6`; sem push direto em `main`.
- Comandos de verificação usam `build` (Debug) e `build-release`.

## Review Focus

- Cada ação entra no menu na task que a implementa; Task 8 só valida a composição final e a colagem.
- Clique direito sobre item já selecionado não pode desfazer o grupo — Task 1 testa clipboard com 2 URLs.
- Item fora da seleção vira único alvo — Task 1 testa o alvo capturado.
- Ação disparada após mudança de seleção ainda age sobre o alvo do menu — Task 2 testa com `before` trocando a seleção.
- Caminho com espaços/acentos em `Abrir com…` — Task 5 usa `.desktop` temporário real e.args separados.
- Barra e menu divergentes em outra aba — Task 4 testa estado compartilhado entre abas e ausência de atalho duplicado.
- `Colar` sobre subpasta cria ao lado, não dentro — Task 8 testa os dois destinos.

---

### Task 1: Alvo do clique direito e inspeção do menu

**Files:**
- Modify: `src/FileTab.h` (hooks, fwd decls), `src/FileTab.cpp:535-545,614`
- Test: `tests/filetab_test.cpp:29-108`

**Interfaces:**
- Consumes: `showContextMenu`, `currentView`, `selectedPaths`, helpers `findRow`/`selectRow` existentes.
- Produces: `setContextMenuHookForTests(std::function<void(QMenu *, const QStringList &)>)`; hook chamado imediatamente antes de `menu.exec`, com o menu já montado e o alvo capturado. Helper de teste `openContextMenu(QAbstractItemView *view, const QModelIndex &index = QModelIndex(), int column = 0)`.

- [ ] **Step 1: Escrever os testes que falham**

Em `tests/filetab_test.cpp`: generalizar `openContextMenu` para `QAbstractItemView *`, calcular `const QPoint vp = index.isValid() ? view->visualRect(index.siblingAtColumn(column)).center() : QPoint(5, 400);`, remover `mapTo(view, vp)` e enviar `QContextMenuEvent` com `vp` direto. Declarar slots: `contextMenuTargetOnFirstRow`, `contextMenuTargetOnDetailColumn`, `contextMenuTargetOnIcons`, `contextMenuPreservesMultiSelection`, `contextMenuOutsideSelectionReplacesTarget`. Padrão comum: `QTemporaryDir`, `FileTab tab(root)`, `tab.show()`, `QTRY_VERIFY_WITH_TIMEOUT(...rowCount > 0, 3000)`, instalar hook com `QScopeGuard` para limpar, abrir menu, guardar `targets` e `QVERIFY` o caminho esperado. Para a grade, `tab.setIconMode(true)` e selecionar o `QListView` cujo `viewMode() == QListView::IconMode` — `findChild<QListView *>()` sozinho também encontra o painel de lugares. Ao clicar fora da seleção, usar `selectionModel()->select(i, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows)` explicitamente.

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build -j4 && env QT_QPA_PLATFORM=offscreen ./build/filetab_test contextMenuTargetOnFirstRow contextMenuTargetOnDetailColumn contextMenuTargetOnIcons contextMenuPreservesMultiSelection contextMenuOutsideSelectionReplacesTarget -v1`
Expected: FAIL — hook ainda não existe / alvo errado na coluna de detalhes.

- [ ] **Step 3: Implementar**

Em `FileTab.h`, adicionar `#include <QAction>` (ou fwd decl), `class QMenu;`, o `using ContextMenuHook = std::function<void(QMenu *, const QStringList &)>;` e `void setContextMenuHookForTests(ContextMenuHook hook);` no escopo de arquivo, seguindo o padrão de `RestoreBackupHook`. Em `FileTab.cpp`, variável estática em namespace anônimo + setter. Em `showContextMenu`: trocar `v->indexAt(v->viewport()->mapFrom(v, pos))` por `v->indexAt(pos)`; quando o índice é válido e não selecionado, `v->clearSelection()` antes de `v->setCurrentIndex(idx.siblingAtColumn(0))`; `menu.exec(v->viewport()->mapToGlobal(pos))`; chamar o hook (se definido) entre a montagem e o `exec`.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: comando do Step 2. Expected: PASS (5 testes).

- [ ] **Step 5: Commit**

```bash
git add src/FileTab.h src/FileTab.cpp tests/filetab_test.cpp
git commit -m "fix: corrige alvo do menu de contexto e adiciona hook de inspecao"
```

### Task 2: Alvo explícito nas ações de item e copiar caminho(s)

**Files:**
- Modify: `src/FileTab.h:63-68`, `src/FileTab.cpp:299-304,588-601,619-720`
- Test: `tests/filetab_test.cpp`

**Interfaces:**
- Consumes: `selectedPaths()`, hook da Task 1.
- Produces: `void copyToClipboard(bool cut, const QStringList &paths)`, `void trashSelected(const QStringList &paths)`, `void deleteSelected(const QStringList &paths)`, `void renameCurrent(const QString &path)`, `void copyPathsToClipboard(const QStringList &paths)`.

- [ ] **Step 1: Escrever os testes que falham**

Adicionar slots `menuActionsUseFrozenTarget` e `copyPathActionsWriteTextToClipboard`. No primeiro: selecionar `origem.txt`, abrir menu, usar `runMenuAction({"Copiar"}, [=]{ selectRow("outro.txt"); })` para trocar a seleção depois de montado o menu; afirmar **primeiro** que a seleção visual mudou (`view->selectionModel()->currentIndex()` aponta para `outro.txt`) e só então que `QGuiApplication::clipboard()->mimeData()->urls()` contém só `origem.txt`. No segundo: menu de arquivo → `Copiar caminho`, conferir `mimeData()->text()` igual ao caminho; selecionar dois itens → `Copiar caminhos`, conferir as duas linhas em ordem.

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build -j4 && env QT_QPA_PLATFORM=offscreen ./build/filetab_test menuActionsUseFrozenTarget copyPathActionsWriteTextToClipboard -v1`
Expected: FAIL — "ação do menu não encontrada".

- [ ] **Step 3: Implementar**

Refatorar as quatro assinaturas para receber o alvo; nos atalhos (`FileTab.cpp:299-304`) passar `selectedPaths()`, exceto F2, que passa `pathOf(currentView()->currentIndex())` para renomear sempre o item corrente, mesmo com multisseleção; `renameCurrent(path)` localiza o índice via `m_proxy->mapFromSource(m_model->index(path))` e chama `currentView()->edit(...)`. Nos lambdas do menu, capturar `sel`/`first` por valor e passar às funções. `copyPathsToClipboard` cria `QMimeData` com `setText(paths.join(QLatin1Char('\n')))`. Ligar `Copiar caminho` ao menu de arquivo e de pasta e `Copiar caminhos` ao menu de seleção múltipla já nesta task, com `sel`/`first` capturados.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: comando do Step 2, depois `env QT_QPA_PLATFORM=offscreen ./build/filetab_test -v1` (regressão dos testes existentes). Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/FileTab.h src/FileTab.cpp tests/filetab_test.cpp
git commit -m "feat: congela alvo das acoes e adiciona copiar caminho"
```

### Task 3: Submenu Novo reagrupado e modelos

**Files:**
- Modify: `src/FileTab.h` (setter de templates), `src/FileTab.cpp:561-585` (novo `addNewMenu(QMenu *)`)
- Test: `tests/filetab_test.cpp`

**Interfaces:**
- Consumes: `newFolder()`, `newFile(suggested, content, executable)`, `newFromTemplate(path)`, hook da Task 1.
- Produces: `void setTemplatesDirForTests(const QString &dir);` (padrão: `QStandardPaths::TemplatesLocation`), `void addNewMenu(QMenu *menu)`.

- [ ] **Step 1: Escrever os testes que falham**

Adicionar `newSubmenuGroupsAndContents` e `newFromPersonalTemplate`. Usar o hook para coletar rótulos recursivamente (`QMenu::actions()`, seguindo `QAction::menu()`). Afirmar ordem: `Pasta…`, `Arquivo vazio…`, `Texto (.txt)`, `Markdown (.md)`, separador, submenu `Código e dados` com `HTML (.html)`, `JSON (.json)`, `Script shell (.sh)`, `Script Python (.py)`, separador, `Modelos`. Nos testes de criação, disparar via `runMenuAction` com helper que aceita o nome no `QInputDialog`: conferir nome, extensão e conteúdo (`# Título\n` para Markdown, shebang + bit executável para `.sh`, `{}` para JSON). Para modelos, apontar `setTemplatesDirForTests` para subpasta do `QTemporaryDir` com `modelo com acento.txt`, disparar `Modelos > ...` e conferir cópia byte a byte.

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build -j4 && env QT_QPA_PLATFORM=offscreen ./build/filetab_test newSubmenuGroupsAndContents newFromPersonalTemplate -v1`
Expected: FAIL — rótulos antigos (`Arquivo de texto (.txt)`).

- [ ] **Step 3: Implementar**

Extrair a montagem atual para `addNewMenu(QMenu *)`. Ordem e conteúdos exatamente os da spec; manter os ícones atuais. `Modelos` só aparece quando a pasta tiver arquivos. Pasta de modelos = `templatesDirForTests` se definida, senão `QStandardPaths::writableLocation(QStandardPaths::TemplatesLocation)`. Atualizar chamadas antigas em todos os menus para `addNewMenu(menu)`.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: comando do Step 2; depois atualizar e rodar o teste existente `contextMenuCreateCopyPasteRename` (trocar o rótulo antigo por `Texto (.txt)`). Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/FileTab.h src/FileTab.cpp tests/filetab_test.cpp
git commit -m "feat: reagrupa submenu Novo e cobre criacao por modelo"
```

### Task 4: Submenu Exibir com ações da MainWindow

**Files:**
- Modify: `src/FileTab.h:30-44,81-99`, `src/FileTab.cpp` (novo `addViewMenu`), `src/MainWindow.cpp:180-188`
- Test: `tests/mainwindow_test.cpp`

**Interfaces:**
- Consumes: `m_iconsAct`, `m_previewAct`, `m_hiddenAct`, `m_zoomInAct`, `m_zoomOutAct`, `m_zoomResetAct` (`MainWindow.h:45-46`).
- Produces: `void FileTab::setViewActions(QAction *icons, QAction *preview, QAction *hidden, QAction *zoomIn, QAction *zoomOut, QAction *zoomReset);` e ponteiros `m_iconsAct`… `m_zoomResetAct` no `FileTab`.

- [ ] **Step 1: Escrever os testes que falham**

Em `mainwindow_test.cpp`, adicionar `viewMenuSharesToolbarStateAcrossTabs`: com `MainWindow w(home)`, abrir menu de contexto da aba ativa (usar o hook de inspeção, tornando-o público em `FileTab.h`), coletar rótulos do submenu `Exibir` (`Lista detalhada`, `Grade de ícones`, separador, rótulos exatos das ações da barra, separador, zoom). Afirmar: ações locais de modo não têm `shortcut()`; `Grade de ícones` está `isChecked()` quando `m_iconsAct` está marcado; disparar `Grade de ícones` e conferir `m_iconsAct->isChecked()` e todas as abas em modo ícone; alternar pela barra e reabrir o menu em outra aba, conferindo a marcação atualizada. Afirmar também que `w.actions()` não ganhou ações novas (atalhos não duplicados).

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build -j4 && env QT_QPA_PLATFORM=offscreen ./build/mainwindow_test viewMenuSharesToolbarStateAcrossTabs -v1`
Expected: FAIL — submenu ausente.

- [ ] **Step 3: Implementar**

Em `FileTab`, guardar os seis ponteiros e implementar `addViewMenu(QMenu *)`: duas ações `checkable` locais (`Lista detalhada`, `Grade de ícones`) sem atalho, com `setChecked` conforme `m_iconsAct->isChecked()`; `triggered` aplica `m_iconsAct->setChecked(modoDesejado)` (nunca `toggle()`). Inserir as quatro/cinco `QAction`s injetadas diretamente (prévia, ocultos, zoom in/out/reset) com separador antes de prévia e antes do zoom. Em `MainWindow::addTab`, chamar `tab->setViewActions(m_iconsAct, m_previewAct, m_hiddenAct, m_zoomInAct, m_zoomOutAct, m_zoomResetAct)` antes de aplicar os estados. Ligar o submenu `Exibir` **somente** ao menu da área vazia nesta task; conforme a spec, nenhuma das demais composições recebe `Exibir` — nem na Task 8.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: comando do Step 2; depois a suíte `mainwindow_test` completa. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/FileTab.h src/FileTab.cpp src/MainWindow.cpp tests/mainwindow_test.cpp
git commit -m "feat: submenu Exibir reusa acoes da barra sem duplicar atalhos"
```

### Task 5: Abrir com… por MIME

**Files:**
- Modify: `src/FileTab.h` (hooks), `src/FileTab.cpp` (novo `addOpenWithMenu`, `openWithDesktop`)
- Test: `tests/filetab_test.cpp`

**Interfaces:**
- Consumes: `QMimeDatabase`, hook da Task 1.
- Produces: `setMimeQueryForTests(const QStringList &ids)`, `setExecutableChooserForTests(std::function<QString()>)`, `setLaunchHookForTests(std::function<bool(const QString &, const QStringList &, const QString &)>)`; `QString desktopFileForId(const QString &id) const`.

- [ ] **Step 1: Escrever os testes que falham**

Os testes:
- `openWithUsesDesktopPathAndSeparateArgs`: criar `.desktop` temporário com `Name=Editor de Teste`, configurar `setMimeQueryForTests({"tmp.editor.desktop"})` e `setLaunchHookForTests` capturando `(program, args, cwd)` num struct; abrir menu sobre arquivo `arquivo com acento.txt`, disparar `Abrir com… > Editor de Teste`; afirmar `program == "gio"`, `args == {"launch", <caminho absoluto do .desktop>, <caminho do arquivo>}` e `cwd.isEmpty()`.
- `gioLaunchPassesAccentedPathToDesktopApp`: sem hook de launch, criar `XDG_DATA_HOME` temporário, `<tmp>/applications/test-editor.desktop` (`Name=Editor Descartável`, `Exec=<tmp>/capture.sh %f`) e `capture.sh` que grava `"$1"` em `<tmp>/recebido.txt`. Usar `qputenv("XDG_DATA_HOME", tmp)` para que `desktopFileForId("test-editor.desktop")` resolva pelo XDG; `setMimeQueryForTests({"test-editor.desktop"})`; disparar a entrada do menu e `QTRY_VERIFY_WITH_TIMEOUT` que `recebido.txt` contém exatamente o caminho com espaços e acentos. Restaurar `XDG_DATA_HOME` com `QScopeGuard`.
- `openWithLaunchFailureReportsStatus`: `.desktop` com `Exec` apontando para binário inexistente; `QSignalSpy spy(&tab, &FileTab::statusMessage)`; disparar a entrada; `QTRY_VERIFY` que alguma mensagem contém `Falha` — prova que o `exitCode` do `gio launch` foi capturado, não só o `start()`.
- `openWithCancelStartsNothing` e `openWithWithoutAssociationsOffersChooser`: `setExecutableChooserForTests([]{ return QString(); })` não registra launch; sem ids, submenu contém só `Escolher executável…`.
- Resolução de id `.desktop`: `desktopFileForIdPrefersXdgDataDirsOverDefaultHome`, `desktopFileForIdPrefersFirstXdgDataDir`, `desktopFileForIdResolvesSubdirectoryId`, `desktopFileForIdPrefersXdgDataHomeOverDataDirs` e `desktopFileForIdIgnoresRelativeXdgPaths`. Usar `setTestEnv`/`qScopeGuard` para restaurar `HOME`, `XDG_DATA_HOME` e `XDG_DATA_DIRS`; afirmar precedência home > dirs, ordem entre dirs, caminho direto para id com subdiretório e descarte de caminhos relativos.

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build -j4 && env QT_QPA_PLATFORM=offscreen ./build/filetab_test openWithUsesDesktopPathAndSeparateArgs gioLaunchPassesAccentedPathToDesktopApp openWithLaunchFailureReportsStatus openWithCancelStartsNothing openWithWithoutAssociationsOffersChooser desktopFileForIdPrefersXdgDataDirsOverDefaultHome desktopFileForIdPrefersFirstXdgDataDir desktopFileForIdResolvesSubdirectoryId desktopFileForIdPrefersXdgDataHomeOverDataDirs desktopFileForIdIgnoresRelativeXdgPaths -v1`
Expected: FAIL — submenu/actions ausentes.

- [ ] **Step 3: Implementar**

`addOpenWithMenu`: MIME via `QMimeDatabase().mimeTypeForFile(QFileInfo(path)).name()`; criar o submenu já com `Escolher executável…` e disparar as consultas de forma **assíncrona** — `QProcess` filho do `FileTab`, conectado a `finished`, sem `waitForFinished`/`waitForStarted` na thread da UI; um contador espera as duas consultas e preenche o submenu numa única vez, guardado por `QPointer<QMenu>`. ids = hook se definido, senão `xdg-mime query default <mime>` (primeiro) e `gio mime <mime>` com `LC_ALL=C` (linhas entre `Registered applications:` e `Recommended applications:`); dedup preservando ordem. Cada entrada mostra `Name=` do `.desktop` resolvido, com fallback para o id; ação chama `openWithDesktop(desktopFileForId(id), path)`. Sem ids, `Escolher executável…` permanece, usando `setExecutableChooserForTests` ou `QFileDialog::getOpenFileName`. `desktopFileForId` aceita id absoluto existente e procura primeiro em `$XDG_DATA_HOME/applications` (ou `~/.local/share/applications` se a variável não estiver definida), depois em `$XDG_DATA_DIRS/applications` separado por `:` (ou `/usr/local/share/applications` e `/usr/share/applications` se não estiver definido), deduplicando raízes e preservando a ordem; valores relativos são ignorados em favor do default correspondente; ids com subdiretório são resolvidos pelo caminho direto sob `applications/`, sem varredura recursiva. `openWithDesktop` usa o hook de launch ou `QProcess::start("gio", {"launch", desktop, path})` com `finished` conectado: `exitCode() != 0` ou erro de processo → `emit statusMessage(tr("Falha ao abrir o arquivo"))`. Ligar `Abrir com…` ao menu de arquivo nesta task.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: comando do Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/FileTab.h src/FileTab.cpp tests/filetab_test.cpp
git commit -m "feat: Abrir com... por MIME com .desktop resolvido e hook de launch"
```

### Task 6: Terminal e Propriedades da pasta

**Files:**
- Modify: `src/FileTab.cpp` (`openTerminal`, entradas de menu)
- Test: `tests/filetab_test.cpp`

**Interfaces:**
- Consumes: `setLaunchHookForTests` da Task 5, `showProperties(const QString &)`.
- Produces: `void openTerminal(const QString &dir)`; entradas `Abrir terminal aqui`, `Abrir terminal nesta pasta`, `Propriedades da pasta`.

- [ ] **Step 1: Escrever os testes que falham**

Adicionar `terminalUsesTargetDirectory`: capturar launch, abrir menu sobre subpasta, disparar `Abrir terminal nesta pasta`, afirmar `cwd == sub` e args vazios. Adicionar `emptyAreaTerminalUsesCurrentDirectory`: menu em área vazia, `Abrir terminal aqui`, `cwd == root`. Adicionar `propertiesOfCurrentFolder`: hook de `QMessageBox`? Usar `QTimer::singleShot(0, ...)` para localizar o `QMessageBox` ativo, conferir que o texto contém o nome da pasta, e fechá-lo com `QTest::keyClick(... Key_Return)`.

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build -j4 && env QT_QPA_PLATFORM=offscreen ./build/filetab_test terminalUsesTargetDirectory emptyAreaTerminalUsesCurrentDirectory propertiesOfCurrentFolder -v1`
Expected: FAIL.

- [ ] **Step 3: Implementar**

`openTerminal(dir)`: primeiro disponível em `x-terminal-emulator`, `foot`, `kitty`, `alacritty`, `konsole`, `gnome-terminal`, `xfce4-terminal`; hook de launch com `(program, {}, dir)` ou `QProcess::startDetached(program, {}, dir)`. Ligar `Abrir terminal aqui` ao menu da área vazia com `m_path`, `Abrir terminal nesta pasta` e `Propriedades da pasta` ao menu de pasta nesta task.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: comando do Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/FileTab.cpp tests/filetab_test.cpp
git commit -m "feat: acoes de terminal e propriedades da pasta"
```

### Task 7: Atualizar recriando o modelo

**Files:**
- Modify: `src/FileTab.h`, `src/FileTab.cpp:140-160,285-292,396-399`
- Test: `tests/filetab_test.cpp`

**Interfaces:**
- Consumes: `m_model`, `m_proxy`, `m_tree`, `m_icons`, `goTo`, `canGoBack`.
- Produces: `void refreshModel()`; entrada `Atualizar` no menu da área vazia.

- [ ] **Step 1: Escrever os testes que falham**

Adicionar `refreshRereadsWithoutWatcherAndKeepsHistory`: criar `root/sub`, `tab.goTo(root)`, `tab.goTo(sub, true)`; obter `auto *model = tab.findChild<QFileSystemModel *>()` e chamar `model->setRootPath(QString())` **antes** de qualquer alteração no sistema de arquivos, desligando o watcher da pasta (o modelo é filho de `FileTab`, então `findChild` o alcança). Criar `sub/novo.txt` e afirmar que a visão ainda não mostra o arquivo; disparar `Atualizar` pelo menu da área vazia; `QTRY_VERIFY_WITH_TIMEOUT` que `novo.txt` aparece nas duas visões; afirmar `tab.currentPath() == sub`, `tab.canGoBack()` verdadeiro e, após `tab.back()`, caminho `root`. Repetir com remoção de arquivo para provar releitura nos dois sentidos.

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build -j4 && env QT_QPA_PLATFORM=offscreen ./build/filetab_test refreshRereadsWithoutWatcherAndKeepsHistory -v1`
Expected: FAIL — ação ausente.

- [ ] **Step 3: Implementar**

`refreshModel()`: criar novo `QFileSystemModel(this)` com `setReadOnly(false)` e o mesmo filtro; atribuir `m_model = novo`; criar novo `ThumbnailProxy(m_model, this)` e atribuir `m_proxy`; `m_model->setRootPath(m_path)`; `m_tree->setModel(m_proxy)` e `m_icons->setModel(m_proxy)`; `m_icons->setSelectionModel(m_tree->selectionModel())`; reconectar `selectionChanged` do novo `m_tree->selectionModel()` para `onSelectionChanged`; reaplicar `rootIndex` mapeado nas duas visões; apagar modelos/proxy antigos via `deleteLater()`. Não chamar `goTo` e não tocar `m_history`/`m_histIdx`. Ligar `Atualizar` ao menu da área vazia nesta task.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: comando do Step 2 e depois a suíte `filetab_test` completa. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/FileTab.h src/FileTab.cpp tests/filetab_test.cpp
git commit -m "feat: Atualizar recria modelo e preserva navegacao"
```

### Task 8: Composição dos quatro menus e destino da colagem

**Files:**
- Modify: `src/FileTab.cpp:535-615`
- Test: `tests/filetab_test.cpp`

**Interfaces:**
- Consumes: todos os helpers e ações das Tasks 1-7; hook da Task 1.
- Produces: `showContextMenu` com quatro composições; `Colar` desabilitado sem `hasUrls()` e sempre em `m_path`.

- [ ] **Step 1: Escrever os testes que falham**

Adicionar `contextMenuCompositionByTarget`: com o hook, coletar rótulos diretos de cada menu e afirmar exatamente — vazio: `Nova pasta…`, `Novo`, `Colar`, `Atualizar`, `Exibir`, `Abrir terminal aqui`, `Propriedades da pasta`; arquivo: `Abrir`, `Abrir com…`, `Copiar caminho`, `Novo`, `Copiar`, `Recortar`, `Colar`, `Renomear`, `Mover para a lixeira`, `Excluir permanentemente`, `Compactar…`, `Propriedades`; pasta: `Abrir`, `Abrir em nova aba`, `Abrir terminal nesta pasta`, `Copiar caminho`, `Novo`, `Copiar`, `Recortar`, `Colar`, `Renomear`, `Mover para a lixeira`, `Excluir permanentemente`, `Compactar…`, `Propriedades`; múltipla: `Copiar`, `Recortar`, `Copiar caminhos`, `Colar`, `Compactar…`, `Mover para a lixeira`, `Excluir permanentemente`, `Novo`. Afirmar que um arquivo compactado acrescenta `Extrair aqui`/`Extrair em pasta própria`. Com clipboard limpo, afirmar `Colar` desabilitado. Adicionar `pasteOverSubfolderUsesCurrentDirectory`: copiar arquivo, abrir menu sobre subpasta e colar → arquivo criado em `root`; entrar em `sub` e colar de novo → arquivo criado em `sub`.

- [ ] **Step 2: Rodar e confirmar falha**

Run: `cmake --build build -j4 && env QT_QPA_PLATFORM=offscreen ./build/filetab_test contextMenuCompositionByTarget pasteOverSubfolderUsesCurrentDirectory -v1`
Expected: FAIL.

- [ ] **Step 3: Implementar**

Reescrever apenas a composição de `showContextMenu` com quatro ramos (`sel.isEmpty()`, `one && isDir`, `one`, `sel.size() > 1`), reaproveitando as ações já ligadas nas tasks anteriores; adicionar o que faltar para fechar a composição exata. `Nova pasta…` chama `newFolder()`. `Colar` permanece ligado a `paste()` sem parâmetros. Separadores conforme a spec; nenhuma ação duplicada dentro do mesmo menu.

- [ ] **Step 4: Rodar e confirmar passagem**

Run: comando do Step 2; depois suítes completas em Debug e Release:

```bash
cmake --build build -j4 && ctest --test-dir build -V
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON && cmake --build build-release -j4 && ctest --test-dir build-release -V
```
Expected: PASS, 4/4 alvos, sem warnings.

- [ ] **Step 5: Commit e PR**

```bash
git add src/FileTab.h src/FileTab.cpp tests/filetab_test.cpp
git commit -m "feat: menus de contexto por alvo e colagem na pasta da aba"
gh pr create --draft --fill --body "Closes #6"
```

### Task 9: Evidência visual na sessão iNiR

**Files:**
- Create: `docs/superpowers/validation/2026-11-27-menus-de-contexto.md`

**Interfaces:**
- Consumes: build Release da Task 8, sessão `WAYLAND_DISPLAY=wayland-0`.

- [ ] **Step 1: Rodar a aplicação e capturar o menu nos quatro alvos**

Run: `./build-release/fm` (ou o binário equivalente do `CMakeLists.txt`) na sessão graphical atual. Com `grim`, capturar menu da área vazia, de arquivo, de pasta e de seleção múltipla, na lista e na grade. Conferir paleta da shell nos ícones/seleção e navegação por teclado (`Ctrl+1`, setas, `Enter`, `F2`, `Ctrl+H`).

- [ ] **Step 2: Registrar o protocolo**

Escrever `docs/superpowers/validation/2026-11-27-menus-de-contexto.md` com os comandos, as capturas referenciadas e o resultado do checklist: menu correto por alvo, `Colar` desabilitado, `Exibir` coerente com a barra em outra aba, `Abrir com…` com arquivo de nome acentuado, releitura via `Atualizar`.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/validation/2026-11-27-menus-de-contexto.md
git commit -m "docs: evidence visual dos menus de contexto no iNiR"
gh pr ready
```
