# Validação — menus da barra e preferências da Issue #20

Branch `enhancement/20-menus-barra` · BASE `d204050` · plano `.superpowers/sdd/2026-11-28-menus-barra-e-preferencias/` · data 2026-11-28. Revalidação final do wave de fixes: BASE/HEAD de partida `35e96b5`.

## Ambiente e escopo

- CachyOS, Qt `6.12.1`, GCC `16.2.1`, CMake/CTest; execução headless com `QT_QPA_PLATFORM=offscreen` e `timeout` externo.
- Paleta aplicada nas capturas: `~/.local/state/quickshell/user/generated/app-palette.json`; estilo Fusion, o mesmo do binário `fm`.
- Nenhuma dependência nova. `QSettings("fm","fm")` continua isolado nos testes por `XDG_CONFIG_HOME` temporário.
- Nas Tasks 1–7 não houve alteração de produção: as Tasks 1–6 já entregam menus, ações, ordenação, colunas e legendas; o `ThemeManager` existente já tem `QFileSystemWatcher` com debounce de 150 ms. Os ajustes pós-revisão do PR #21 (abaixo) alteraram `MainWindow` e `main.cpp`.

## Testes novos (`tests/mainwindow_test.cpp`)

- `paletteChangeKeepsMenusAndFocusReadable`: abre `viewMenu`, marca `sortSizeDesc` e `gridSizeAction`, troca a paleta com `setThemeFile()` (duas chamadas explícitas) e confere marcas, ações habilitadas e sem texto vazio em `filesMenu`/`viewMenu`, além de `hasFocus()` em `filesMenuButton`/`viewMenuButton` após Tab e Shift+Tab. **Cobertura de regressão da paleta estática — não é o teste de recarga em runtime.**
- `liveThemeReloadKeepsOpenMenuState`: escreve `palette-A.json` em `palette.json` dentro de `QTemporaryDir`, chama `setThemeFile()` **uma única vez**, marca `sortDateNewest` e `gridDateAction`, abre `viewMenu` e o deixa visível. Sobrescreve o **mesmo caminho** com `palette-B.json` sem chamar `setThemeFile()` de novo. `QTRY_VERIFY_WITH_TIMEOUT` (3 s) confirma `QPalette::Window` de `#112233` → `#abcdef`; marcas e menu permanecem; após fechar o popup, Tab alcança `viewMenuButton` com `hasFocus()` e foco de tabulação visível. A checagem de foco vem depois do `Esc` porque o popup aberto detém o teclado; a sobrevivência do menu aberto é verificada antes.

### RED → GREEN

Os dois casos passaram na primeira execução, então o RED foi provado por mutação controlada em `src/ThemeManager.cpp`, com restauração literal em seguida:

| Mutação temporária | Teste | Falha observada |
|---|---|---|
| Comentar os dois `connect` de `fileChanged`/`directoryChanged` → `m_debounce.start()` | `liveThemeReloadKeepsOpenMenuState` | `FAIL!` em `QTRY_VERIFY_WITH_TIMEOUT`: paleta continuou `#112233`; limite do `QTRY` era 3 s (9,25 s era o tempo total do executável naquela rodada) |
| Comentar `p.setColor(QPalette::Window, surface)` | `paletteChangeKeepsMenusAndFocusReadable` | `FAIL!`: esperado `#112233`, obtido `#111113` |

`git diff -- src/ThemeManager.cpp` ficou vazio após a restauração; nenhuma mudança de produção entrou no commit. Os binários de Debug/Release usados na validação final foram recompilados com a fonte restaurada.

## Comandos e resultados

```bash
cmake --build build-debug -j4 && cd build-debug && QT_QPA_PLATFORM=offscreen timeout 180s ctest --output-on-failure
cmake --build build-release -j4 && cd build-release && QT_QPA_PLATFORM=offscreen timeout 180s ctest --output-on-failure
```

| Build | CTest | Tempo CTest | Tempo build+teste |
|---|---|---|---|
| Debug | **6/6, 100%** | 6,83 s | 16,32 s |
| Release | **6/6, 100%** | 6,50 s | 28,16 s |

A primeira execução completa após o refactor dos testes teve `filetab_test` 60/61: `executableChooserLaunchSurvivesTabClose` falhou em `!pid.isEmpty()` (`tests/filetab_test.cpp:1543`). O caso isolado passou 5/5 em seguida (`Totals: 3 passed`), e as duas execuções completas posteriores — Debug e Release — passaram 6/6. Hipótese de causa (não corrigida aqui, por ser fora do escopo da Task 7): `QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidFile), 3000)` observa a criação do arquivo antes de o script escrever o PID; sob carga, a leitura imediata pode devolver texto vazio. Naquela task, nenhum arquivo de produção ou teste de `filetab_test` foi alterado; o achado fica para o revisor decidir (o wave final de fixes alterou testes e produção, registrado em `final-fix-report.md`).

Contagem por executável (Release; Debug idêntica): `theme_manager_test` 10/10, `filetab_test` 64/64, `mainwindow_test` 37/37 (antes 35), `uipreferences_test` 7/7, `properties_dialog_test` 8/8, `bench_smoke` PASS — 126 casos QTest + 1 smoke. Zero `warning:`/`error:` nos dois builds. Os `QWARN` do plugin offscreen (`propagateSizeHints`, `raise`, `grabbing the keyboard`, `kf.windowsystem`) são pré-existentes e não são warnings de compilação nem falhas.

Comando isolado dos casos novos (após o refactor, Release): `QT_QPA_PLATFORM=offscreen timeout 180s ./build-release/mainwindow_test paletteChangeKeepsMenusAndFocusReadable liveThemeReloadKeepsOpenMenuState` → `Totals: 4 passed, 0 failed` em 322 ms (inclui `initTestCase`/`cleanupTestCase`); `mainwindow_test` completo: `37 passed` após o wave final.

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

Rodadas da revalidação final (sem carga concorrente): `142, 139, 140, 142, 141, 142, 140 ms`. Descartada a primeira (`142 ms`), as seis restantes ordenadas são `139, 140, 140, 141, 142, 142 ms` → **mediana 140,5 ms**. Uma medição preliminar feita em paralelo à contagem dos casos deu mediana 138,5 ms e não é a usada aqui.

Comparação histórica (documento `2026-11-27-identidade-visual-presets.md`, PR #19): base `89,5 ms`, HEAD `90,0 ms`. Diferenças aritméticas: **+51,0 ms** contra a base e **+50,5 ms** contra o HEAD do PR #19. Essas medianas históricas foram medidas em outra sessão, não em pares alternados com este binário; o número de 140,5 ms deve ser lido como observação desta sessão, não como prova de regressão causal. A mediana anterior deste documento (107,0 ms) foi remediada nos mesmos binários finais e não se reproduziu; nenhum bench pareado foi executado neste wave.

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

## Ajustes pós-revisão do PR #21 (2026-11-28)

Pedidos do usuário com o PR em rascunho; BASE desta rodada `1579fa4`.

1. **Terceiro menu removido.** `m_moreAct`/`moreMenu` saíram da toolbar e do código. O submenu `Interface` (Compacta/Equilibrada/Avançada, Restaurar padrões, Personalizar…) passou a viver em `Arquivos`, ao lado de `Preferências…`; nenhum comando ficou órfão. `syncToolbarActions()` agora só controla presença na toolbar — as ações recolhidas continuam em `Arquivos`/`Visualização`. Os três presets seguem acessíveis e marcados, verificado nos três por `onlyTwoMenusKeepEveryCommand`.
2. **Fonte efetiva do tema repassada.** `main.cpp` resolve `--theme-file` > `FM_THEME_FILE` > `QSettings` > padrão e chama `MainWindow::setThemeFile(path)`. `Nova janela` emite `newWindowRequested(args)` com `--theme-file <caminho>` quando há fonte; `main.cpp` conecta o sinal a `QProcess::startDetached(applicationFilePath(), args)`. O executor fica no composition root para que os testes não iniciem subprocessos. `newWindowForwardsEffectiveThemeFile` escreve um `theme.json` temporário e confere os três argumentos, além do caso sem tema (somente a pasta).

RED → GREEN: `onlyTwoMenusKeepEveryCommand` falhou em runtime enquanto `moreMenu` existia; `newWindowForwardsEffectiveThemeFile` falhou em compilação (`setThemeFile`/`newWindowRequested` ausentes). Durante o GREEN, uma primeira versão com `startDetached` dentro do `MainWindow` chegou a iniciar o próprio binário de teste como efeito colateral; o connect foi movido para `main.cpp` e o efeito desapareceu. Uma primeira posição do submenu `Interface` no fim de `Visualização` deslocou o sufixo esperado por `viewMenuSeparatesZoomFromGridLegends` e os índices fixos de `sortIsPerTabAndSurvivesRefresh`; mover `Interface` para `Arquivos` preservou o contrato do menu de visualização sem reescrever esses testes.

| Rodada | Build | CTest | Tempo | `warning:` |
|---|---|---|---|---|
| Ajustes pós-revisão | Debug | **6/6, 100%** | 6,39 s | 0 |
| Ajustes pós-revisão | Release | **6/6, 100%** | 6,51 s | 0 |

`mainwindow_test` passou de 37 para 39 casos; contagem total agora 128 casos QTest + 1 smoke. O benchmark de 140,5 ms **não foi repetido** nesta rodada (mudança é de montagem de menus e argumentos, não de hot path de pintura); o número do corpo do PR continua como medição da rodada anterior, com as mesmas ressalvas de não pareamento. Capturas existentes mostram o terceiro botão na Equilibrada a 900 px e precisam ser refeitas após esta mudança antes de sair do rascunho.
