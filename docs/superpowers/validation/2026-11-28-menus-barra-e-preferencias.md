# Validação — menus da barra e preferências da Issue #20

Branch `enhancement/20-menus-barra` · BASE `d204050` · plano `.superpowers/sdd/2026-11-28-menus-barra-e-preferencias/` · data 2026-11-28.

## Ambiente e escopo

- CachyOS, Qt `6.12.1`, GCC `16.2.1`, CMake/CTest; execução headless com `QT_QPA_PLATFORM=offscreen` e `timeout` externo.
- Paleta aplicada nas capturas: `~/.local/state/quickshell/user/generated/app-palette.json`; estilo Fusion, o mesmo do binário `fm`.
- Nenhuma dependência nova. `QSettings("fm","fm")` continua isolado nos testes por `XDG_CONFIG_HOME` temporário.
- Não houve alteração de código de produção nesta task: as Tasks 1–6 já entregam menus, ações, ordenação, colunas e legendas; o `ThemeManager` existente já tem `QFileSystemWatcher` com debounce de 150 ms.

## Testes novos (`tests/mainwindow_test.cpp`)

- `paletteChangeKeepsMenusAndFocusReadable`: abre `viewMenu`, marca `sortSizeDesc` e `gridSizeAction`, troca a paleta com `setThemeFile()` (duas chamadas explícitas) e confere marcas, ações habilitadas e sem texto vazio em `filesMenu`/`viewMenu`, além de `hasFocus()` em `filesMenuButton`/`viewMenuButton` após Tab e Shift+Tab. **Cobertura de regressão da paleta estática — não é o teste de recarga em runtime.**
- `liveThemeReloadKeepsOpenMenuState`: escreve `palette-A.json` em `palette.json` dentro de `QTemporaryDir`, chama `setThemeFile()` **uma única vez**, marca `sortDateNewest` e `gridDateAction`, abre `viewMenu` e o deixa visível. Sobrescreve o **mesmo caminho** com `palette-B.json` sem chamar `setThemeFile()` de novo. `QTRY_VERIFY_WITH_TIMEOUT` (3 s) confirma `QPalette::Window` de `#112233` → `#abcdef`; marcas e menu permanecem; após fechar o popup, Tab alcança `viewMenuButton` com `hasFocus()` e foco de tabulação visível. A checagem de foco vem depois do `Esc` porque o popup aberto detém o teclado; a sobrevivência do menu aberto é verificada antes.

### RED → GREEN

Os dois casos passaram na primeira execução, então o RED foi provado por mutação controlada em `src/ThemeManager.cpp`, com restauração literal em seguida:

| Mutação temporária | Teste | Falha observada |
|---|---|---|
| Comentar os dois `connect` de `fileChanged`/`directoryChanged` → `m_debounce.start()` | `liveThemeReloadKeepsOpenMenuState` | `FAIL!` em `QTRY_VERIFY_WITH_TIMEOUT`: paleta continuou `#112233` (9,25 s até o timeout) |
| Comentar `p.setColor(QPalette::Window, surface)` | `paletteChangeKeepsMenusAndFocusReadable` | `FAIL!`: esperado `#112233`, obtido `#111113` |

`git diff -- src/ThemeManager.cpp` ficou vazio após a restauração; nenhuma mudança de produção entrou no commit. Os binários de Debug/Release usados na validação final foram recompilados com a fonte restaurada.

## Comandos e resultados

```bash
cmake --build build-debug -j4 && cd build-debug && QT_QPA_PLATFORM=offscreen timeout 180s ctest --output-on-failure
cmake --build build-release -j4 && cd build-release && QT_QPA_PLATFORM=offscreen timeout 180s ctest --output-on-failure
```

| Build | CTest | Tempo CTest | Tempo build+teste |
|---|---|---|---|
| Debug | **6/6, 100%** | 6,01 s | 6,02 s (build incremental) |
| Release | **6/6, 100%** | 6,14 s | 14,01 s |

A primeira execução completa após o refactor dos testes teve `filetab_test` 60/61: `executableChooserLaunchSurvivesTabClose` falhou em `!pid.isEmpty()` (`tests/filetab_test.cpp:1543`). O caso isolado passou 5/5 em seguida (`Totals: 3 passed`), e as duas execuções completas posteriores — Debug e Release — passaram 6/6. Hipótese de causa (não corrigida aqui, por ser fora do escopo da Task 7): `QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidFile), 3000)` observa a criação do arquivo antes de o script escrever o PID; sob carga, a leitura imediata pode devolver texto vazio. Nenhum arquivo de produção ou teste de `filetab_test` foi alterado; o achado fica para o revisor decidir.

Contagem por executável (Release; Debug idêntica): `theme_manager_test` 10/10, `filetab_test` 61/61, `mainwindow_test` 35/35 (antes 33), `uipreferences_test` 7/7, `properties_dialog_test` 8/8, `bench_smoke` PASS — 121 casos QTest + 1 smoke. Zero `warning:`/`error:` nos dois builds. Os `QWARN` do plugin offscreen (`propagateSizeHints`, `raise`, `grabbing the keyboard`, `kf.windowsystem`) são pré-existentes e não são warnings de compilação nem falhas.

Comando isolado dos casos novos (após o refactor, Release): `QT_QPA_PLATFORM=offscreen timeout 180s ./build-release/mainwindow_test paletteChangeKeepsMenusAndFocusReadable liveThemeReloadKeepsOpenMenuState` → `Totals: 4 passed, 0 failed` em 322 ms (inclui `initTestCase`/`cleanupTestCase`); `mainwindow_test` completo: `35 passed`.

## Benchmark (Release)

Protocolo: sete rodadas, primeira descartada, mediana das seis restantes; diretório temporário por rodada com `bench/` criado, `XDG_CONFIG_HOME` isolado, mesma paleta da shell e `QT_QPA_PLATFORM=offscreen`.

```bash
for i in $(seq 1 7); do
  T=$(mktemp -d /tmp/opencode/fm20-bench-XXXXXX)
  mkdir -p "$T/bench"
  XDG_CONFIG_HOME="$T" QT_QPA_PLATFORM=offscreen timeout 10 \
    ./build-release/fm --theme-file /home/alvaro/.local/state/quickshell/user/generated/app-palette.json --bench "$T/bench" 2>&1 \
    | grep -o '[0-9]\+ ms'
  rm -rf "$T"
done | tee /tmp/opencode/fm-issue20-bench.txt
tail -6 /tmp/opencode/fm-issue20-bench.txt | sed 's/ ms//' | sort -n \
  | awk '{a[NR]=$1} END{printf "mediana: %.1f ms\n", (a[3]+a[4])/2}'
```

Rodadas: `112, 104, 111, 110, 121, 102, 104 ms`. Descartada a primeira (`112 ms`), as seis restantes ordenadas são `102, 104, 104, 110, 111, 121 ms` → **mediana 107,0 ms**.

Comparação histórica (documento `2026-11-27-identidade-visual-presets.md`, PR #19): base `89,5 ms`, HEAD `90,0 ms`. Diferenças aritméticas: **+17,5 ms** contra a base e **+17,0 ms** contra o HEAD do PR #19. Essas medianas históricas foram medidas em outra sessão, não em pares alternados com este binário; o número de 107,0 ms deve ser lido como observação desta sessão, não como prova de regressão causal.

## Capturas dos menus (três presets e 320 px)

Geradas offscreen com o mesmo estilo Fusion e a paleta da shell: um utilitário temporário fora do repositório (`/tmp/opencode/fm20-capturas`) instanciou `MainWindow`, aplicou `defaultUiPreferences(preset)` e compôs `QWidget::render()` da janela com o popup. Os rótulos abaixo foram conferidos por OCR (`tesseract -l spa+eng --psm 11`); este modelo não inspecionou as imagens visualmente.

| Preset / largura | `Arquivos` | `Visualização` |
|---|---|---|
| Compacta (900 px) | [menu-arquivos-compacta.png](capturas/menu-arquivos-compacta.png) — Nova janela, Nova aba (Ctrl+T), Mostrar todas as abas, Preferências…, Atalhos de teclado, Ajuda, Sobre o fm | [menu-visualizacao-compacta.png](capturas/menu-visualizacao-compacta.png) — Lista detalhada, Grade de ícones (Ctrl+1), oito ordenações, ocultos, pré-visualização, zoom e legendas |
| Equilibrada (900 px) | [menu-arquivos-equilibrada.png](capturas/menu-arquivos-equilibrada.png) — mesmos itens sobre lista com colunas Nome/Tamanho/Tipo/Data | [menu-visualizacao-equilibrada.png](capturas/menu-visualizacao-equilibrada.png) — mesmos itens sobre a lista com colunas |
| Avançada (900 px) | [menu-arquivos-avancada.png](capturas/menu-arquivos-avancada.png) — mesmos itens com colunas completas e pré-visualização | [menu-visualizacao-avancada.png](capturas/menu-visualizacao-avancada.png) — mesmos itens com legenda de atalhos na toolbar |
| 320 px (Equilibrada) | [menu-arquivos-estreita-320.png](capturas/menu-arquivos-estreita-320.png) — botões Arquivos e Visualização continuam visíveis; popup mantém os sete itens legíveis | [menu-visualizacao-estreita-320.png](capturas/menu-visualizacao-estreita-320.png) — popup reposicionado dentro da janela estreita; OCR confirma as oito ordenações e ações de exibição |

Limitação das capturas: o platform offscreen não aplica geometria de tela real, então o popup da janela de 320 px foi reposicionado para caber no quadro; a cadeia de foco e a abertura por teclado nessa largura são verificadas por `keyboardOpensMenusAt320InEveryPreset` e pelos dois testes novos. Nenhuma captura de sessão Wayland foi feita nesta task.

## Resultado

Task 7 concluída: regressão de paleta estática registrada, recarga ao vivo dependente apenas do watcher/debounce existentes, suíte completa verde em Debug e Release, benchmark com mediana registrada e capturas dos dois menus nos três presets e a 320 px.
