# Validação visual — menus de contexto (2026-11-27)

## Ambiente real

- Sessão graphical viva: compositor **Umbriel 0.1.0**, `XDG_CURRENT_DESKTOP=umbriel`,
  `WAYLAND_DISPLAY=wayland-0`, shell iNiR. `ryoku wm status` devolveu
  `Provider: hyprland`, `Live: false`, `Source: config tree` (configuração, não sessão Hyprland).
- **Diferença em relação ao brief:** ele descreve a sessão como iNiR/Niri; o sistema
  observado é Umbriel com proveniência de configuração Hyprland. Nada aqui assume Niri.
- Paleta da shell lida de `~/.cache/ryoku/colors.json` (matugen), sem alteração:
  `background #0f131c`, `foreground #dfe2ef`, `primary #adc6ff`,
  `primaryContainer #004494`, `surfaceContainer #1c2029`, `surfaceContainerLow #181c25`,
  `outline #8d909c`. Tema: `~/.config/ryoku/theme.json` =
  `{"followWallpaper":true,"gtkTheme":"system","scheme":"mono"}`.
- Binário: `./build-release/fm --theme-file /home/alvaro/.cache/ryoku/colors.json /tmp/opencode/fm-task9-data`.
- Entrada na sessão: `ydotool` (`wtype key ctrl+1` não entregou evento ao app: 0 px alterados;
  `ydotool key 29:1 2:1 2:0 29:0` alterou 99.960 px e trocou lista por grade).
  Foco/ponteiro: `umbriel msg window-focus <id>` e `umbriel msg window-focus-warp <id>`.
  Captação: `grim`; leitura: `tesseract ... -l spa+eng --psm 11` + contagem/diff de pixels.

## Comandos executados (sequência resumida)

```bash
ryoku wm status
cat ~/.cache/ryoku/colors.json ~/.config/ryoku/theme.json
cmake --build build-release -j4
ctest --test-dir build-release -V
setsid ./build-release/fm --theme-file ~/.cache/ryoku/colors.json /tmp/opencode/fm-task9-data &
umbriel msg window-focus "$ID"
umbriel msg window-focus-warp "$ID"
ydotool click 0xC1          # botão direito: abre menu de contexto
ydotool click 0xC0          # botão esquerdo: seleciona / ativa item
ydotool key 29:1 2:1 2:0 29:0   # Ctrl+1
ydotool key 59:1 59:0       # F2
ydotool key 29:1 35:1 35:0 29:0 # Ctrl+H
grim /tmp/opencode/fm-task9/<cena>-full.png
tesseract /tmp/opencode/fm-task9/<cena>-full.png stdout -l spa+eng --psm 11
```

O app foi redimensionado/flutuado com `umbriel msg window-toggle-floating`,
`window-toggle-maximize` e `window-modify-height-down` para posicionar cada alvo
sob o cursor fixo; o ponteiro relativo foi ajustado com `ydotool mousemove -- DX DY`.

## Capturas

Todas em `docs/superpowers/validation/capturas/`:

| Alvo | Lista | Grade |
|---|---|---|
| Área vazia | [menu-area-vazia-lista.png](capturas/menu-area-vazia-lista.png) | [menu-area-vazia-grade.png](capturas/menu-area-vazia-grade.png) |
| Arquivo | [menu-arquivo-lista.png](capturas/menu-arquivo-lista.png) | [menu-arquivo-grade.png](capturas/menu-arquivo-grade.png) |
| Pasta | [menu-pasta-lista.png](capturas/menu-pasta-lista.png) | [menu-pasta-grade.png](capturas/menu-pasta-grade.png) |
| Seleção múltipla | [menu-selecao-multipla-lista.png](capturas/menu-selecao-multipla-lista.png) | [menu-selecao-multipla-grade.png](capturas/menu-selecao-multipla-grade.png) |

Contexto e verificações adicionais:
[contexto-lista-tela-completa.png](capturas/contexto-lista-tela-completa.png),
[contexto-grade-tela-completa.png](capturas/contexto-grade-tela-completa.png),
[teclado-ctrl-h.png](capturas/teclado-ctrl-h.png),
[teclado-f2-renomear.png](capturas/teclado-f2-renomear.png),
[atualizar-watcher-off.png](capturas/atualizar-watcher-off.png),
[abrir-com-acento.png](capturas/abrir-com-acento.png).

## Checklist do brief

| Item | Resultado | Evidência |
|---|---|---|
| Menu da área vazia, arquivo, pasta e seleção múltipla na lista e na grade | **PASS** | 8 capturas acima; OCR confirmou, por alvo, `Nova pasta…/Novo/Colar/Atualizar/Exibir/Abrir terminal aqui/Propriedades da pasta`; `Abrir/Abrir com…/Copiar caminho/Copiar/Recortar/Colar/Renomear/Mover para a lixeira/Excluir permanentemente/Compactar…/Propriedades`; `Abrir em nova aba/Abrir terminal nesta pasta`; e, na seleção múltipla, `Copiar caminhos`, sem `Renomear`/`Abrir com…`. |
| Paleta da shell em ícones/seleção | **PARCIAL** | `#004494` (primaryContainer) e `#dfe2ef` (foreground) presentes nas capturas; `#181c25`/`#1c2029` (surfaceContainerLow/Container) dominam o fundo. O `#0f131c` exato não aparece por composição do tema/compositor. Não houve inspeção visual direta das imagens por este modelo. |
| `Ctrl+1` | **PASS** | 99.960 px alterados; lista → grade com nomes truncados. |
| Setas + `Enter` | **PASS** | `Home` + deslocamento e `Enter` abriram `outra-pasta` (título da janela confirmado via `umbriel windows --json`); `Alt+Left` retornou ao diretório original. |
| `F2` | **PASS** | Editor inline sobre `arquivo-alpha.txt`, fundo `#004494`; cancelado com `Escape`, sem renomear o arquivo. |
| `Ctrl+H` | **PASS** | `.oculto-de-teste` passou a constar na lista; 6.552 px alterados. |
| `Colar` desabilitado | **PASS** | No menu da área vazia, o texto de `Colar` tem ~50 px brutos (máx. RGB 103,107,118) contra ~210 px em `#dfe2ef` em `Atualizar` — contraste de item desabilitado. |
| `Exibir` coerente com a barra em outra aba | **PASS** | Com 2 abas abertas, `Ctrl+1` colocou a segunda aba em grade; ações de visão são compartilhadas (`MainWindow.cpp:127-129`) e o teste `viewMenuSharesToolbarStateAcrossTabs` passou. |
| `Abrir com…` com nome acentuado | **PARCIAL (com cobertura automatizada)** | O menu foi aberto sobre `relatório-ação.txt` e o item `Abrir com…` é legível em `abrir-com-acento.png`. O submenu não abriu de forma confiável na sessão: o popup não respondeu a seta direita/`Enter` sintéticos e o clique sintético caiu no diálogo de escolha de aplicativo. A resolução/caminho acentuado é coberta por `tests/filetab_test.cpp::gioLaunchPassesAccentedPathToDesktopApp` (`arquivo com acento á.txt` → `.desktop` → `gio launch` com o caminho integral), que passou em Release e Debug. Não se afirma aqui submenu validado ao vivo. |
| `Atualizar` com watcher desligado | **PASS** | `novo-externo.txt`, criado fora do app, não apareceu sozinho; com o menu aberto, `segundo-externo.txt` foi criado no filesystem e, após clicar em `Atualizar`, apareceu imediatamente na lista (`atualizar-watcher-off.png`). |
|Terminal/propriedades nos alvos corretos | **PASS** | Área vazia: `Abrir terminal aqui` + `Propriedades da pasta`; pasta: `Abrir terminal nesta pasta` + `Propriedades`; arquivo: apenas `Propriedades`. |
| Builds e testes finais | **PASS** | `cmake --build build-release -j4` sem warnings; `ctest --test-dir build-release -V` 4/4. Em Debug (`-DCMAKE_BUILD_TYPE=Debug`), 4/4 sem warnings de compilação. `QWARN` do Qt nos testes (plugin offscreen, `propagateSizeHints`) não são warnings de compilação nem falhas. |

## Limitações

- A conferência das imagens foi feita por OCR (`tesseract spa+eng`) e contagem/diff de
  pixels; este modelo não consegue inspecionar PNGs diretamente. Nenhuma captura foi
  interpretada além do que o OCR/pixel confirma.
- `#0f131c` não aparece como cor exata na imagem por composição do tema e do compositor;
  a presença dos tokens da paleta foi medida onde eles são esperados.
- O submenu `Abrir com…` não foi capturado ao vivo; a garantia equivalente é o teste
  automatizado citado acima.
- O brief cita Niri/iNiR, mas a sessão é Umbriel; a discrepância foi preservada, não corrigida
  artificialmente.
