# Estabilidade do fm no Umbriel/iNiR — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tornar o esqueleto C++/Qt6 confiável no desktop real: fonte de tema explícita, paleta resistente a gravações transitórias, métrica de abertura e validação em sessão Wayland.

**Architecture:** Três mudanças pequenas no código existente (`main.cpp`, `MainWindow`, `ThemeManager`) mais um launcher `.desktop` e uma bateria de validação. Um harness QtTest headless cobre o leitor de tema; um smoke test cobre `--bench`. Nenhuma dependência nova de runtime.

**Tech Stack:** C++17, Qt 6 Widgets + QtTest, CMake 4.5.1, bsdtar/libarchive, CachyOS/Umbriel/iNiR.

**Spec:** `/tmp/opencode/fm-inspect/fm/docs/superpowers/specs/2026-11-26-fm-estabilidade-design.md` (copiado para `~/Projetos/fm/docs/superpowers/specs/` na Task 1)

## Global Constraints

- Sistema: CachyOS (Arch); compositor Umbriel; shell iNiR; `WAYLAND_DISPLAY=wayland-0`.
- Qt 6.12.1, CMake 4.5.1, libarchive 3.8.11 — já instalados; não instalar nada.
- Repositório canônico: `~/Projetos/fm`. Fonte temporária somente para leitura: `/tmp/opencode/fm-inspect/fm`.
- Baseline commitada antes de qualquer edição de código.
- Paleta do launcher: `/home/alvaro/.local/state/quickshell/user/generated/app-palette.json`.
- Precedência do tema: `--theme-file` > `FM_THEME_FILE` > QSettings > padrão.
- Padrão documentado no `main.cpp` passa a ser `app-palette.json` (era `colors.json`).
- `QFileSystemWatcher` continua observando arquivo **e** diretório; debounce de 150 ms permanece.
- `--bench`: cronômetro na primeira linha de `main()`, primeiro `paintEvent` da janela principal, impressão única, auto-encerramento; saída exata `tempo até a primeira pintura: N ms`, relatada como "tempo até a primeira pintura da janela principal".
- QtTest e todos os alvos de teste ficam dentro de `if(BUILD_TESTING)` após `include(CTest)`; com `-DBUILD_TESTING=OFF` o build do app não depende de QtTest.
- Medição: 7 execuções Release com `/tmp/opencode/fm-bench` vazio; descartar a 1ª; mediana das 6 restantes.
- Sem `sudo`, sem escrita em `~/.config/umbriel/config.d/*` nem em arquivos gerenciados pelo iNiR.
- Nenhuma operação de teste em `~` ou `~/Documentos`: usar `/tmp/opencode/fm-check`.

## Review Focus

- JSON truncado depois de uma paleta válida: o app deve manter as cores anteriores em vez de cair no padrão do Qt — pinned por `invalidJsonKeepsLastPalette` (Task 2).
- Substituições atômicas consecutivas (`commit()` do `QSaveFile`): o watcher deve reaplicar a nova paleta na 1ª e continuar acompanhando o caminho na 2ª, sem restart — pinned por `atomicReplaceAppliesNewPalette` (Task 2).
- Arquivo de tema ausente no boot: deve usar a paleta padrão do Qt sem travar — pinned por `missingFileAtStartupUsesDefault` (Task 3).
- Paleta camelCase do Ryoku (`onSurface`, `primaryContainer`): `norm()` não pode regredir — pinned por `camelCasePaletteWorks` (Task 3).
- `--bench` deve imprimir a métrica exatamente uma vez e sair; duas pinturas não podem duplicar a linha — pinned por `tests/bench_smoke.sh` (Task 4).

---

### Task 1: Repositório canônico e harness de teste

**Files:**
- Create: `~/Projetos/fm/` (cópia de `/tmp/opencode/fm-inspect/fm/`)
- Create: `.gitignore` (conteúdo: `build/`)
- Modify: `CMakeLists.txt`
- Create: `tests/theme_manager_test.cpp`

**Interfaces:**
- Consumes: código baseline do tarball, já compilável em Release.
- Produces: alvo CTest `theme_manager_test` com `QT_QPA_PLATFORM=offscreen`; padrão de teste para as Tasks 2 e 3.

- [ ] **Step 1: Criar o repositório e comitar o baseline**

Criar `~/Projetos/fm/.gitignore` com a linha `build/` antes do `git add`.

```bash
mkdir -p ~/Projetos/fm
cp -a /tmp/opencode/fm-inspect/fm/. ~/Projetos/fm/
cd ~/Projetos/fm && git init -q && git add -A && git commit -q -m "chore: importa esqueleto fm do tarball"
```

Expected: `git status --short` vazio.

- [ ] **Step 2: Acrescentar QtTest ao CMake atrás de `BUILD_TESTING`**

Em `CMakeLists.txt`: manter `find_package(Qt6 REQUIRED COMPONENTS Widgets)` como está; adicionar `include(CTest)` (define `BUILD_TESTING`, ON por padrão). Dentro de `if(BUILD_TESTING)`: `find_package(Qt6 REQUIRED COMPONENTS Test)`, `add_executable(theme_manager_test tests/theme_manager_test.cpp src/ThemeManager.cpp)`, `target_link_libraries(theme_manager_test PRIVATE Qt6::Widgets Qt6::Test)`, `add_test(NAME theme_manager_test COMMAND theme_manager_test)` e `set_tests_properties(theme_manager_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")`. Incluir `src/ThemeManager.cpp` no alvo de teste é obrigatório: o arquivo não faz parte da biblioteca do app. Conferir depois com `cmake -S . -B /tmp/opencode/fm-no-tests -DBUILD_TESTING=OFF && cmake --build /tmp/opencode/fm-no-tests -j4` — deve compilar sem mencionar QtTest.

- [ ] **Step 3: Escrever o primeiro teste (regressão)**

`tests/theme_manager_test.cpp`: classe de teste QtTest com `Q_OBJECT`, `QTEST_MAIN`, `initTestCase()` guardando `qApp->palette().color(QPalette::Window)` como `m_defaultWindow` e `init()` restaurando paleta e stylesheet antes de cada caso. Caso `validJsonAppliesPalette()`: gravar `{"surface":"#112233","primary":"#445566"}` num `QTemporaryDir`, chamar `ThemeManager::setThemeFile(caminho)`, conferir `QCOMPARE(qApp->palette().color(QPalette::Window), QColor("#112233"))`.

- [ ] **Step 4: Rodar e conferir**

Run: `cmake -S ~/Projetos/fm -B ~/Projetos/fm/build -DCMAKE_BUILD_TYPE=Release && cmake --build ~/Projetos/fm/build -j4 && ctest --test-dir ~/Projetos/fm/build -R theme_manager_test -V`
Expected: PASS (o código baseline já aplica a paleta).

- [ ] **Step 5: Commit**

```bash
cd ~/Projetos/fm && git add CMakeLists.txt tests/theme_manager_test.cpp && git commit -m "test: harness QtTest para ThemeManager"
```

### Task 2: Paleta resiliente a gravação e troca atômica

**Files:**
- Modify: `theme_manager_test.cpp`
- Modify: `src/ThemeManager.h`, `src/ThemeManager.cpp`

**Interfaces:**
- Consumes: `theme_manager_test` da Task 1; `ThemeManager::setThemeFile(const QString&)`.
- Produces: estado interno `m_applied` (bool, privado); comportamento de retenção da última paleta válida.

- [ ] **Step 1: Escrever os testes que falham**

`invalidJsonKeepsLastPalette()`: aplicar JSON válido com `surface=#112233`, sobrescrever o mesmo arquivo com `{`, esperar 400 ms (`QTest::qWait`), `QCOMPARE` da cor de `QPalette::Window` com `#112233`.
`atomicReplaceAppliesNewPalette()`: aplicar válido com `#112233`; abrir `QSaveFile` apontando para o **mesmo caminho** do JSON, chamar `setDirectWriteFallback(false)` (desativa a escrita direta), escrever o JSON com `surface=#abcdef` e `commit()` — o `commit()` é que faz o `rename` atômico sobre o destino existente; `QTRY_COMPARE_WITH_TIMEOUT(..., 2000)` a cor com `#abcdef`. Em seguida repetir o processo com `surface=#123456` e conferir `QTRY_COMPARE_WITH_TIMEOUT(..., 2000)` com `#123456`: a 2ª troca prova que o watcher continuou no caminho após a 1ª. `QFile::rename()` não é usado: ele falha quando o destino já existe e o teste não exercitaria o watcher. Incluir `<QSaveFile>`.

- [ ] **Step 2: Rodar e verificar a falha**

Recompilar primeiro, depois rodar:
`cmake --build ~/Projetos/fm/build -j4 && ctest --test-dir ~/Projetos/fm/build -R theme_manager_test -V`
Expected: FAIL em `invalidJsonKeepsLastPalette()` — hoje `reload()` chama `applyDefault()` e troca a cor. Sem o rebuild, o binário de teste antigo daria falso resultado.

- [ ] **Step 3: Implementar a retenção**

Em `ThemeManager.h`: membro privado `bool m_applied = false;`.
Em `ThemeManager.cpp`: em `reload()`, no ramo em que não há `surface`/`background`, chamar `applyDefault()` **somente se** `!m_applied`, então `return`; em `applyDefault()`, `m_applied = false`; no fim de `applyShellColors()`, `m_applied = true` antes do `emit`.

- [ ] **Step 4: Rodar e conferir**

Run: `cmake --build ~/Projetos/fm/build -j4 && ctest --test-dir ~/Projetos/fm/build -R theme_manager_test -V`
Expected: PASS nos dois casos novos.

- [ ] **Step 5: Commit**

```bash
cd ~/Projetos/fm && git add src/ThemeManager.h src/ThemeManager.cpp tests/theme_manager_test.cpp && git commit -m "fix: mantém última paleta válida durante gravação do tema"
```

### Task 3: Casos de borda do leitor de paleta

**Files:**
- Modify: `theme_manager_test.cpp`
- Modify (somente se os testes falharem): `src/ThemeManager.cpp`

**Interfaces:**
- Consumes: `ThemeManager::setThemeFile`, member `m_applied` da Task 2.
- Produces: garantia de que os dois formatos reais de JSON continuam suportados.

- [ ] **Step 1: Escrever os testes de borda**

`missingFileAtStartupUsesDefault()`: `setThemeFile(caminhoInexistente)`; `QCOMPARE(qApp->palette().color(QPalette::Window), m_defaultWindow)`.
`camelCasePaletteWorks()`: gravar `{"surface":"#123456","onSurface":"#654321","primaryContainer":"#abcdef"}`; aplicar; `QCOMPARE` da cor de `QPalette::Window` com `#123456` e `QVERIFY(qApp->styleSheet().contains("#abcdef"))`.

- [ ] **Step 2: Rodar**

Run: `cmake --build ~/Projetos/fm/build -j4 && ctest --test-dir ~/Projetos/fm/build -R theme_manager_test -V`
Expected: PASS. Se falhar, ajustar apenas `norm()` ou o fallback de `pick()` em `ThemeManager.cpp` e repetir até PASS.

- [ ] **Step 3: Commit**

```bash
cd ~/Projetos/fm && git add tests/theme_manager_test.cpp src/ThemeManager.cpp && git commit -m "test: fixa suporte a paleta ausente e chaves camelCase"
```

### Task 4: Métrica tempo até a primeira pintura

**Files:**
- Modify: `src/main.cpp`, `src/MainWindow.h`, `src/MainWindow.cpp`, `CMakeLists.txt`
- Create: `tests/bench_smoke.sh`

**Interfaces:**
- Consumes: binário `fm`; `QElapsedTimer` iniciado na primeira linha de `main()`.
- Produces: `--bench` (bool) e `MainWindow::setBenchMode(bool)`; sinal `MainWindow::firstPaint()` sem parâmetros; saída exata `tempo até a primeira pintura: N ms` no stderr, uma vez.

- [ ] **Step 1: Escrever o smoke test**

`tests/bench_smoke.sh` (bash, `set -euo pipefail`): criar `tmp="$(mktemp -d)"` com `trap 'rm -rf "$tmp"' EXIT`; `mkdir -p "$tmp/bench" "$tmp/config"`; capturar stderr de `XDG_CONFIG_HOME="$tmp/config" QT_QPA_PLATFORM=offscreen timeout 10 "$1" --bench "$tmp/bench" 2>&1`; contar com `grep -cE '^tempo até a primeira pintura: [0-9]+ ms$'` e exigir exatamente 1. `XDG_CONFIG_HOME` isolado impede que `QSettings` locais mudem a geometria da janela; cada execução usa diretório próprio e é limpa pelo `trap`. exit 0 é conferido pelo `set -e`.
Registrar no CMake: `add_test(NAME bench_smoke COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/bench_smoke.sh $<TARGET_FILE:fm>)`.

- [ ] **Step 2: Rodar e verificar a falha**

Run: `cmake --build ~/Projetos/fm/build -j4 && ctest --test-dir ~/Projetos/fm/build -R bench_smoke -V`
Expected: FAIL — `--bench` ainda não existe.

- [ ] **Step 3: Implementar**

`main.cpp`: `QElapsedTimer elapsed; elapsed.start();` como primeira instrução de `main()`; opção `QCommandLineOption benchOpt(QStringLiteral("bench"), QStringLiteral("Mede até a primeira pintura e sai"))`; ligar `QObject::connect(&w, &MainWindow::firstPaint, ...)` **antes** de `w.show()`, imprimindo no stderr com `std::fprintf` o texto exato da Interface; chamar `w.setBenchMode(parser.isSet(benchOpt))`.
`MainWindow.h`: `public: void setBenchMode(bool on);`, `signals: void firstPaint();`, `protected: void paintEvent(QPaintEvent *e) override;`, membros `bool m_bench = false; bool m_painted = false;`.
`MainWindow.cpp`: `setBenchMode()` só guarda o valor; `paintEvent()` chama `QMainWindow::paintEvent(e)` e, se `m_bench && !m_painted`, marca `m_painted = true`, emite `firstPaint()` e agenda `QTimer::singleShot(0, qApp, &QCoreApplication::quit)` (includes `QCoreApplication` e `QTimer`).

- [ ] **Step 4: Rodar e conferir**

Run: `cmake --build ~/Projetos/fm/build -j4 && ctest --test-dir ~/Projetos/fm/build -V`
Expected: todos os testes PASS, sem warnings de compilação.

- [ ] **Step 5: Commit**

```bash
cd ~/Projetos/fm && git add src/main.cpp src/MainWindow.h src/MainWindow.cpp CMakeLists.txt tests/bench_smoke.sh && git commit -m "feat: mede tempo até a primeira pintura com --bench"
```

### Task 5: Launcher explícito e binário de usuário

**Files:**
- Create: `~/.local/share/applications/fm.desktop`
- Copy: `~/Projetos/fm/build/fm` → `~/.local/bin/fm`
- Create (somente se já existir): backup `fm.desktop.bak`

**Interfaces:**
- Consumes: binário Release da Task 4.
- Produces: entrada de launcher com `Exec` fixando `--theme-file`.

- [ ] **Step 1: Verificar e fazer backup de launcher e binário**

Bash com `set -euo pipefail`. Sem `|| true`: se `cp` falhar, a instalação para. Nomes de backup únicos por timestamp, um para cada arquivo existente:

```bash
if [ -e "$HOME/.local/share/applications/fm.desktop" ]; then
  bk="$HOME/.local/share/applications/fm.desktop.bak.$(date +%Y%m%d-%H%M%S)"
  cp "$HOME/.local/share/applications/fm.desktop" "$bk"
  printf 'backup: %s\n' "$bk"
fi
if [ -e "$HOME/.local/bin/fm" ]; then
  bk="$HOME/.local/bin/fm.bak.$(date +%Y%m%d-%H%M%S)"
  cp "$HOME/.local/bin/fm" "$bk"
  printf 'backup: %s\n' "$bk"
fi
```

Hoje nenhum dos dois existe, mas a ordem continua válida se o usuário os criar antes da execução.

- [ ] **Step 2: Escrever o `.desktop`**

`[Desktop Entry]`, `Version=1.0`, `Type=Application`, `Name=fm`, `GenericName=Gerenciador de Arquivos`, `Icon=folder`, `Terminal=false`, `Categories=Utility;FileManager;`, `MimeType=inode/directory;`,
`TryExec=/home/alvaro/.local/bin/fm`,
`Exec=/home/alvaro/.local/bin/fm --theme-file /home/alvaro/.local/state/quickshell/user/generated/app-palette.json %f`.
`%f`, não `%u`: `main.cpp` passa o argumento posicional direto para `FileTab` como caminho, sem `QUrl::fromUserInput()` nem `toLocalFile()`; uma URL `file://` chegaria como texto e falharia.

- [ ] **Step 3: Instalar e atualizar banco**

```bash
install -m 755 ~/Projetos/fm/build/fm ~/.local/bin/fm
update-desktop-database ~/.local/share/applications
```

- [ ] **Step 4: Verificar**

Run: `desktop-file-validate ~/.local/share/applications/fm.desktop && grep -F 'Exec=/home/alvaro/.local/bin/fm --theme-file /home/alvaro/.local/state/quickshell/user/generated/app-palette.json %f' ~/.local/share/applications/fm.desktop`
Expected: sem saída de erro do validador e linha `Exec` encontrada. Se existia arquivo anterior, conferir que o `.bak` correspondente existe e é diferente do novo. Sem commit: arquivos ficam fora do repo.

### Task 6: Validação na sessão Umbriel

**Files:**
- Create: `docs/superpowers/validation/2026-11-26-umbriel.md`

**Interfaces:**
- Consumes: `~/.local/bin/fm` e o `.desktop` da Task 5.
- Produces: relatório com resultados observados e mediana do `--bench`.

- [ ] **Step 1: Preparar arquivos descartáveis**

`/tmp/opencode/fm-check/{sub,pics}`; `a.txt`, `b.md`, um PNG copiado de `~/Imagens/Screenshots/`. Nada em `~`.

- [ ] **Step 2: Testar na sessão Wayland**

Abrir `~/.local/bin/fm /tmp/opencode/fm-check`. Conferir: entrar/sair de `sub`, Alt+← voltar, miniatura do PNG na visão em ícones, Novo → arquivo `.txt`, copiar/colar e F2, compactar em zip e extrair em pasta própria. Registrar falhas com o que apareceu na tela.

- [ ] **Step 3: Testar a sequência de tema com o app aberto**

Iniciar uma segunda instância com `--theme-file /tmp/opencode/fm-theme/colors.json`: válido (`#112233`) → inválido (`{`) → válido (`#abcdef`) → substituição atômica com `mktemp` + `mv`. Conferir visualmente que as cores não voltam ao padrão no passo inválido e que mudam sozinhas nos passos válidos.

- [ ] **Step 4: Medir a abertura**

Bash com `set -euo pipefail`. Usar o mesmo `--theme-file` do launcher. Cada rodada é validada individualmente: exit 0 e exatamente uma linha de medição.

```bash
theme=/home/alvaro/.local/state/quickshell/user/generated/app-palette.json
samples=/tmp/opencode/fm-bench-samples.txt
: > "$samples"
for i in $(seq 1 7); do
  out="$(~/.local/bin/fm --bench --theme-file "$theme" /tmp/opencode/fm-bench 2>&1)" || {
    printf 'rodada %d: exit != 0\n' "$i"; exit 1;
  }
  n="$(printf '%s\n' "$out" | grep -cE '^tempo até a primeira pintura: [0-9]+ ms$')"
  [ "$n" -eq 1 ] || { printf 'rodada %d: %s medições\n' "$i" "$n"; exit 1; }
  printf '%s\n' "$out" | sed -nE 's/^tempo até a primeira pintura: ([0-9]+) ms$/\1/p' >> "$samples"
done
test "$(wc -l < "$samples")" -eq 7
tail -n +2 "$samples" | python3 -c 'import sys,statistics; print(statistics.median(map(int, sys.stdin)))'
```

Filtrar as linhas **antes** de descartar a primeira: `2>&1` também captura avisos do Qt. Uma rodada sem saída ou com duas medições falha na hora — o total de sete não basta. Mediana das 6 após o aquecimento. Rodar na sessão Umbriel, sem `QT_QPA_PLATFORM`.

- [ ] **Step 5: Registrar e commitar**

Escrever `docs/superpowers/validation/2026-11-26-umbriel.md` com: comando do launcher, resultados de navegação/miniatura/operações, resultado da sequência de tema, 7 amostras válidas, mediana e eventuais falhas. Rotular a métrica como **tempo até a primeira pintura da janela principal**; a string impressa no stderr continua `tempo até a primeira pintura: N ms`.
```bash
cd ~/Projetos/fm && git add docs/superpowers/validation/2026-11-26-umbriel.md && git commit -m "docs: validação no Umbriel/iNiR"
```
