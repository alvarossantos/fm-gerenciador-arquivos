# Validação — presets de identidade visual do `fm`

Issue: [#18](https://github.com/alvarossantos/fm-gerenciador-arquivos/issues/18) · branch `feat/identidade-visual-presets` · base `origin/main` em `b65fd92` · commit validado `32b7938`.

## Ambiente

- CachyOS, kernel `7.3.6-3-cachyos`, Qt `6.12.1`, CMake/CTest.
- Sessão Wayland ativa: compositor **Umbriel** (`ryoku wm status` reporta provedor `hyprland` apenas no config tree, `Live: false`). Não havia Niri/Hyprland live nesta sessão.
- Paleta da shell: `~/.local/state/quickshell/user/generated/app-palette.json` (`surface=#111113`, `primary=#ca8aba`).
- Capturas detalhadas geradas com `QT_QPA_PLATFORM=offscreen` + `QWidget::grab()`; uma captura da sessão real obtida com `grim`.

## Verificação

```bash
cmake --build build-debug -j4
cmake --build build-release -j4
ctest --test-dir build-debug --output-on-failure
ctest --test-dir build-release --output-on-failure
```

- Builds Debug e Release: exit 0; nenhum `warning:` ou `error:`.
- CTest Debug: **6/6** (`theme_manager_test`, `bench_smoke`, `filetab_test`, `mainwindow_test`, `uipreferences_test`, `properties_dialog_test`).
- CTest Release: **6/6**. Houve uma falha transitória isolada de `filetab_test` durante a primeira execução sob carga; o teste isolado passou e três execuções completas seguintes passaram 6/6. Nenhuma mudança de código foi feita após essa falha.

## Benchmark

Protocolo do plano: 7 rodadas, primeira descartada, mediana das 6 restantes.

```bash
for i in $(seq 1 7); do
  T=$(mktemp -d /tmp/opencode/fm-bench-XXXXXX)
  XDG_CONFIG_HOME="$T" QT_QPA_PLATFORM=offscreen timeout 10 \
    ./build-release/fm --bench "$T/bench" 2>&1 | grep -o '[0-9]\+ ms'
  rm -rf "$T"
done | tee /tmp/opencode/fm-i18-bench-final3.txt
tail -6 /tmp/opencode/fm-i18-bench-final3.txt | sed 's/ ms//' | sort -n \
  | awk '{a[NR]=$1} END{printf "mediana: %.1f ms\n", (a[3]+a[4])/2}'
```

Rodadas: `142, 157, 162, 138, 135, 146, 144 ms` → **mediana final: 141,0 ms**.
Baseline registrado no plano: **92,0 ms**. Rodadas intermediárias desta sessão: `136,5 ms` e `145,0 ms`.

Para separar custo real de ruído do ambiente, baseline e HEAD foram medidos alternadamente na mesma carga (7 pares, primeira rodada descartada): baseline `b65fd92` = **138,0 ms**, HEAD `32b7938` = **140,0 ms**. O diff pareado é ~2 ms; os valores absolutos estão inflados pela carga da máquina (`load average` ~3,8). A diferença contra 92,0 ms não deve ser lida como regressão de ~49 ms sem esse contexto. Binários Release de baseline e HEAD têm o mesmo `-O3 -DNDEBUG -flto=auto`.

## Validação visual

Presets aplicados via `QSettings [ui] preset=compact|balanced|advanced` e via `applyUiPreferences()`; paleta da shell carregada com `--theme-file`.

| Captura | Evidência |
|---|---|
| [`compacta.png`](capturas/compacta.png) | lista densa, sem cabeçalhos de coluna, lateral enxuta, breadcrumb curto |
| [`equilibrada.png`](capturas/equilibrada.png) | lista com ícones, colunas e lateral completos (padrão) |
| [`avancada.png`](capturas/avancada.png) | colunas completas, cabeçalhos de grupo, mais espaço |
| [`compacta-menu.png`](capturas/compacta-menu.png), [`equilibrada-menu.png`](capturas/equilibrada-menu.png) | menu de contexto com `Abrir`, `Propriedades`, etc.; sufixos de atalho na Equilibrada |
| [`propriedades-avancada.png`](capturas/propriedades-avancada.png) | diálogo não modal com cabeçalho e cálculo em segundo plano |
| [`estreita-ctrl-l.png`](capturas/estreita-ctrl-l.png) | janela 620 px, toolbar recolhida e `Ctrl+L` ativo |
| [`compact-live.png`](capturas/compact-live.png) | janela real na sessão Wayland/Umbriel via `grim`, sem crash; a janela ficou estreita no canto da tela |

Troca de paleta em runtime: gerado `paleta-trocada.json` a partir da paleta da shell, alterando `surface`, `background`, `surface_container*`, `primary` e `primary_container`. Após `ThemeManager::setThemeFile()`, a paleta do `QApplication` passou a `window=#131a26` / `highlight=#7fd1c1`, e as capturas [`compacta-paleta-trocada.png`](capturas/compacta-paleta-trocada.png), [`equilibrada-paleta-trocada.png`](capturas/equilibrada-paleta-trocada.png) e [`avancada-paleta-trocada.png`](capturas/avancada-paleta-trocada.png) diferem das originais em todos os três presets. Nenhum fallback de JSON inválido foi alterado (coberto por `theme_manager_test`).

Limitação: o compositor ativo não é Niri; a janela live ficou estreita no canto da tela e a injeção de teclado por `ydotool` não foi confiável. Por isso as capturas de interação (menu, `Ctrl+L`, propriedades) foram feitas offscreen com o mesmo estilo Fusion e a mesma paleta.

Comparação das três direções: [`2026-11-27-identidade-visual-propostas.html`](2026-11-27-identidade-visual-propostas.html).

## Resultado

Task 9 concluída: build limpo, 6/6 em Debug e Release, benchmark registrado, capturas dos três presets e da troca de paleta. Pronto para push e PR referenciando `Closes #18`.
