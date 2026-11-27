# Navegação: breadcrumb clicável e destaque da pasta ativa — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Substituir o campo único de caminho por segmentos clicáveis e destacar no painel lateral a pasta ativa de cada aba, mantendo histórico por aba.

**Architecture:** `MainWindow` passa a abrigar um widget de breadcrumb com botões por segmento, um botão `…` para ancestrais ocultos e o `QLineEdit` escondido até `Ctrl+L`. `FileTab` continua dono do histórico e ganha uma função privada que só atualiza a seleção visual dos lugares. A cobertura vive em um novo alvo `mainwindow_test`, que exercita a UI real das duas abas.

**Tech Stack:** C++17, Qt 6 Widgets/Test, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-11-26-navegacao-breadcrumb-design.md`

## Global Constraints

- Branch `feat/navegacao-visual`, base `main` em `b780f73`, Issue #5.
- Qt 6 Widgets, C++17, `-Wall -Wextra`; nenhuma dependência nova.
- Histórico permanece por aba em `FileTab::m_history`/`m_histIdx`.
- Testes CTest rodam com `QT_QPA_PLATFORM=offscreen`.
- Sem push direto em `main`; entrega final via PR com a Issue referenciada.

## Review Focus

- **Caminhos com espaços e acentos nos segmentos:** devem navegar exatamente e sobreviver a Voltar/Avançar — teste `breadcrumbClickThenBackUsesActiveTabHistory` (Task 2).
- **`/` e home:** `Raiz (/)` só para `/`; `Pasta pessoal` como fallback de subpastas da home — teste `placesHighlightUsesAncestorRules` (Task 1).
- **`Documentos2` vs `Documentos`:** prefixo de texto não pode virar ancestral — teste `placesHighlightRejectsPrefixCollision` (Task 1).
- **Caminho inválido no `Enter`:** campo aberto com o texto, status informado, pasta e histórico intactos — teste `pathEditKeepsInvalidTextAndEscRestores` (Task 3).
- **Troca de aba com edição pendente e janela estreita:** texto pendente é descartado; a pasta atual continua visível e todo ancestral oculto aparece no menu `…` — testes `tabChangeDiscardsPendingEdit` (Task 3) e `overflowMenuKeepsCurrentAndNavigates` (Task 2).

---

### Task 1: Destaque dos lugares e retorno de `goTo()`

**Files:**
- Modify: `src/FileTab.h`
- Modify: `src/FileTab.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/mainwindow_test.cpp`

**Interfaces:**
- Consumes: `FileTab::goTo(const QString &, bool)`, `back()`, `forward()`, `canGoBack()`, `canGoForward()`, `currentPath()`.
- Produces: `void setPlacesForTests(const QStringList &paths)` (hook global; lista vazia restaura `QStandardPaths`); `bool FileTab::goTo(...)` (false só para pasta inexistente/ilegível); `QListWidget` com `objectName == "places"`; `void updatePlacesSelection()` privado, chamado ao fim de `goTo()`.

- [ ] **Step 1: Escrever os testes RED**

Em `tests/mainwindow_test.cpp`, incluir `QtTest`, `QApplication`, `QDir`, `QListWidget`, `QTemporaryDir`, `FileTab.h`. Criar `QTemporaryDir`, `QDir(dir).mkpath(...)` para `Documentos/projeto/sub`, `Documentos2`, `Downloads/teste`. Cada caso chama `setPlacesForTests(...)` com `QScopeGuard` restaurando `{})`. Usar `findChild<QListWidget*>("places")` e comparar `currentItem()->data(Qt::UserRole)`.

Casos (nomes exatos):
- `placesHighlightFollowsGoToBackForward`: com lugares `{home, documents}`, `goTo(documents + "/projeto")` → destaque em `Documentos`; `back()` → `home`; `forward()` → `Documentos`; verificar `canGoBack()`/`canGoForward()` a cada passo e `currentPath()` inalterado pela seleção.
- `placesHighlightUsesAncestorRules`: com lugares `{home, documents, documents + "/projeto"}`, `goTo(documents + "/projeto/sub")` → destaque em `projeto` (casa exata/ancestral mais específico); depois `goTo(home + "/outro")` → `Pasta pessoal`; `goTo("/")` → `Raiz (/)`.
- `placesHighlightRejectsPrefixCollision`: com lugares `{home, documents}` — **sem** `Documentos2` —, `goTo(documents2)` → destaque em `Pasta pessoal`, nunca em `Documentos`. Cadastrar `Documentos2` mascararia o erro de prefixo por casa exata.

- [ ] **Step 2: Rodar e confirmar RED**

```bash
cmake -S . -B build -DBUILD_TESTING=ON >/dev/null && cmake --build build -j4 >/dev/null 2>&1
env QT_QPA_PLATFORM=offscreen ./build/mainwindow_test placesHighlightFollowsGoToBackForward placesHighlightUsesAncestorRules placesHighlightRejectsPrefixCollision -v1
```

Esperado: falha de compilação/link por `setPlacesForTests` e alvo ausente; depois, com o alvo criado, FAIL nas assertivas de destaque.

- [ ] **Step 3: Implementar o mínimo**

Em `FileTab.h/.cpp`: hook global `setPlacesForTests`; no construtor, usar os caminhos do hook quando não vazios, mantendo rótulos (`Pasta pessoal` para home, `Raiz (/)` para `/`, `QFileInfo(path).fileName()` para os demais); `m_places->setObjectName("places")`; mudar `goTo` para `bool` (false nos dois `return` de erro, true nos demais); extrair `updatePlacesSelection()` privado. Regra: `QDir::cleanPath` + `split('/', Qt::SkipEmptyParts)`; **todos** os segmentos do lugar precisam coincidir, em ordem, com o início dos segmentos do caminho atual — nunca prefixo de texto. Casa exata primeiro, depois maior número de segmentos coincidentes; fallback home; `/` só para `/`. Chamar no fim de `goTo()`, usando `setCurrentItem()` sem navegar.

Em `CMakeLists.txt`: criar `mainwindow_test` com `tests/mainwindow_test.cpp src/MainWindow.cpp src/FileTab.cpp src/ThemeManager.cpp src/ThumbnailProxy.cpp`, include `src`, link `Qt6::Widgets Qt6::Test`, `add_test(NAME mainwindow_test ...)`, propriedade `ENVIRONMENT "QT_QPA_PLATFORM=offscreen"`.

- [ ] **Step 4: Rodar e confirmar GREEN**

Repetir o comando do Step 2. Esperado: `Totals: 5 passed, 0 failed` (3 casos + `initTestCase`/`cleanupTestCase`).

- [ ] **Step 5: Commit**

```bash
git add src/FileTab.h src/FileTab.cpp CMakeLists.txt tests/mainwindow_test.cpp
git commit -m "test: cobre destaque dos lugares e retorno de goTo"
```

### Task 2: Breadcrumb, menu `…` e clique por segmento

**Files:**
- Modify: `src/MainWindow.h`
- Modify: `src/MainWindow.cpp`
- Modify: `tests/mainwindow_test.cpp`

**Interfaces:**
- Consumes: `FileTab::goTo()` (bool), `currentPath()`, `pathChanged`, `openInNewTab`.
- Produces: widgets `objectName` `breadcrumbButton` (cada um com o caminho acumulado em `Qt::UserRole`), `breadcrumbOverflow` (`QToolButton` com `QMenu`), `pathEdit` (`QLineEdit`); `void rebuildBreadcrumb()` privado; `QString elideMiddle(const QString &, int)` no anon namespace de `MainWindow.cpp`.

- [ ] **Step 1: Escrever os testes RED**

- `breadcrumbFollowsActiveTab`: abrir `MainWindow` em `documents/projeto`; encontrar `QToolButton` por `objectName`; conferir que o último tem `data(Qt::UserRole) == documents/projeto`. Ativar ação que começa com `Nova aba` e chamar `goTo(downloads/teste)` diretamente no `FileTab` da nova aba; conferir o breadcrumb da nova aba. Trocar `QTabWidget::setCurrentIndex(0)` e conferir, juntos, breadcrumb e destaque do `places` da aba 0; repetir para a aba 1.
- `breadcrumbClickThenBackUsesActiveTabHistory`: na aba 0, navegar para `documents/projeto/sub`, clicar com `QTest::mouseClick` no botão cujo `data` seja `documents`, acionar a ação `Voltar` e conferir retorno a `documents/projeto/sub`; aba 1 permanece em `downloads/teste`.
- `overflowMenuKeepsCurrentAndNavigates`: com lugares `{home, documents}`, redimensionar para 420x600, criar `documents + "/a/b/c/d/com espaços"`, navegar até lá. Para cada ancestral, afirmar que ele está em botão visível ou em ação do menu de `breadcrumbOverflow`; a pasta atual precisa estar em botão visível. Se `/` não couber, afirmar que ele está no menu. Localizar a ação cujo `data() == documents` no menu (confirmando que ela existe), dispará-la e conferir `currentPath() == documents`.

- [ ] **Step 2: Rodar e confirmar RED**

```bash
cmake --build build -j4 >/dev/null 2>&1
env QT_QPA_PLATFORM=offscreen ./build/mainwindow_test breadcrumbFollowsActiveTab breadcrumbClickThenBackUsesActiveTabHistory overflowMenuKeepsCurrentAndNavigates -v1
```

Esperado: FAIL — botões `breadcrumbButton` não existem.

- [ ] **Step 3: Implementar o mínimo**

Em `MainWindow.h`: declarar `void rebuildBreadcrumb()`, `void resizeEvent(QResizeEvent *) override`, membros `QWidget *m_pathBar`, `QToolButton *m_overflow` e manter `m_pathEdit`. Em `MainWindow.cpp`: trocar `tb->addWidget(m_pathEdit)` por `tb->addWidget(m_pathBar)`, com `QHBoxLayout` horizontal; incluir `<QResizeEvent>` e `<QToolButton>`; criar botões com `objectName` e o destino em `Qt::UserRole`, tooltip com caminho completo e `elideMiddle()` nos rótulos; cada clique chama `currentTab()->goTo(data)`, que entra no histórico da aba ativa. Cada ação do menu `…` usa `QAction::setData(caminho)` e o mesmo handler. Construir segmentos a partir de `QDir::cleanPath`, começando por `/`. Enquanto a soma das larguras exceder `m_pathBar->width()`, mover segmentos do meio para o menu `…` (primeiro os intermediários; se precisar, a raiz), mantendo sempre o último visível. Ligar `rebuildBreadcrumb()` a `pathChanged`, `currentChanged` e `resizeEvent`.

- [ ] **Step 4: Rodar e confirmar GREEN**

Repetir o comando do Step 2. Esperado: `Totals: 5 passed, 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add src/MainWindow.h src/MainWindow.cpp tests/mainwindow_test.cpp
git commit -m "feat: breadcrumb clicavel com menu de ancestrais"
```

### Task 3: Modo de edição do caminho

**Files:**
- Modify: `src/MainWindow.h`
- Modify: `src/MainWindow.cpp`
- Modify: `tests/mainwindow_test.cpp`

**Interfaces:**
- Consumes: `FileTab::goTo()` bool, `rebuildBreadcrumb()`, `pathEdit`.
- Produces: `void beginPathEdit()`, `void endPathEdit()`, ligados a `Ctrl+L`, `returnPressed`, shortcut `Esc` no campo e `currentChanged`.

- [ ] **Step 1: Escrever os testes RED**

- `pathEditKeepsInvalidTextAndEscRestores`: em `MainWindow` na home, `Ctrl+L` → campo visível, focado e texto selecionado; `QTest::keyClicks` com caminho existente + `Return` → pasta mudou, campo escondido, breadcrumb atualizado. `Ctrl+L`, digitar `QTemporaryDir` + `/nao-existe-fm`, `Return` → campo segue visível com o texto exato, `currentPath()` e `canGoBack()` inalterados, `QStatusBar::currentMessage()` contém `Pasta inválida`. `Esc` → campo escondido e breadcrumb igual à pasta atual.
- `tabChangeDiscardsPendingEdit`: abrir duas abas; na aba 0, `Ctrl+L` e digitar caminho da aba 1 sem `Return`; `tabs->setCurrentIndex(1)` → campo escondido e breadcrumb da aba 1; enviar `Return` à janela → `currentPath()` da aba 0 **e** da aba 1 continuam os originais.

- [ ] **Step 2: Rodar e confirmar RED**

```bash
cmake --build build -j4 >/dev/null 2>&1
env QT_QPA_PLATFORM=offscreen ./build/mainwindow_test pathEditKeepsInvalidTextAndEscRestores tabChangeDiscardsPendingEdit -v1
```

Esperado: FAIL — edição ainda fecha/commits com caminho inválido e não reage à troca de aba.

- [ ] **Step 3: Implementar o mínimo**

`Ctrl+L` chama `beginPathEdit()` (mostrar campo, `setFocus`, `selectAll`, esconder botões). `returnPressed`: expandir `~` e conferir `QFileInfo`; se `goTo()` retornar true, `endPathEdit()`; se false, manter campo e texto. `Esc`: shortcut com contexto `Qt::WidgetShortcut` no campo chama `endPathEdit()` sem navegar. `currentChanged` também chama `endPathEdit()` antes de `rebuildBreadcrumb()`, descartando o texto pendente. Erro de `goTo()` já chega ao `statusBar()` via `statusMessage`.

- [ ] **Step 4: Rodar e confirmar GREEN**

Repetir o comando do Step 2. Esperado: `Totals: 4 passed, 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add src/MainWindow.h src/MainWindow.cpp tests/mainwindow_test.cpp
git commit -m "feat: edicao do caminho com Ctrl+L, Enter e Esc"
```

### Task 4: Verificação do recorte

**Files:** nenhum (verificação e evidência).

- [ ] **Step 1: Suíte completa em Debug, diretório próprio**

```bash
DEBUG_DIR=/tmp/opencode/fm-nav-debug
rm -rf "$DEBUG_DIR"
cmake -S . -B "$DEBUG_DIR" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON > /tmp/opencode/nav-debug-config.log 2>&1
cmake --build "$DEBUG_DIR" -j4 > /tmp/opencode/nav-debug-build.log 2>&1
grep -c 'warning:' /tmp/opencode/nav-debug-build.log || true
ctest --test-dir "$DEBUG_DIR" -V > /tmp/opencode/nav-debug-ctest.log 2>&1
```

Se configurar ou compilar falhar, preservar os logs e parar. Esperado: `0` warnings, `100% tests passed out of 4`, `filetab_test` 16/16, `theme_manager_test` 9/9, `mainwindow_test` 10/10, `bench_smoke` PASS.

- [ ] **Step 2: Suíte completa em Release, diretório temporário novo**

```bash
RELEASE_DIR="$(mktemp -d /tmp/opencode/fm-nav-release-XXXXXX)"
echo "release_dir=$RELEASE_DIR"
cmake -S . -B "$RELEASE_DIR" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON > /tmp/opencode/nav-release-config.log 2>&1
cmake --build "$RELEASE_DIR" -j4 > /tmp/opencode/nav-release-build.log 2>&1
grep -c 'warning:' /tmp/opencode/nav-release-build.log || true
ctest --test-dir "$RELEASE_DIR" -V > /tmp/opencode/nav-release-ctest.log 2>&1
```

Se configurar ou compilar falhar, preservar os logs e parar. Esperado: `0` warnings e `100% tests passed out of 4`.

- [ ] **Step 3: Revisão do diff e registro**

Rodar `git diff main..HEAD --stat` e conferir que nada de grade/lista/abas foi alterado neste recorte. Nenhuma captura, paleta ou `--bench` entra aqui — ficam para o fechamento da Issue #5.

## Self-review

- Especificação coberta: segmentos, elipse/menu, edição, erro no `Enter`, `Esc`, troca de aba, destaque por segmentos, `Documentos2`, testes pedidos, histórico por aba.
- Fora deste plano por decisão explícita: grade, lista, abas, capturas, paleta ao vivo, `--bench`.
- Nomes consistentes: `setPlacesForTests`, `updatePlacesSelection`, `rebuildBreadcrumb`, `beginPathEdit`, `endPathEdit`, `breadcrumbButton`, `breadcrumbOverflow`, `pathEdit`, `places`.
