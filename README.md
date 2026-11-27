# fm — gerenciador de arquivos (C++/Qt6)

## Dependências (Arch/CachyOS)
    sudo pacman -S qt6-base cmake libarchive      # libarchive fornece o bsdtar

## Compilar e rodar
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ./build/fm [pasta]

## Tema da shell
O app lê um JSON de cores (estilo matugen/Material You, chaves como `surface`,
`on_surface`, `primary`, `on_primary`, `surface_container_high`...) e recarrega
sozinho quando o arquivo muda.

Ordem de busca do arquivo:
1. `--theme-file /caminho/app-palette.json`
2. variável `FM_THEME_FILE`
3. `QSettings` (chave `theme/file`, em ~/.config/fm/fm.conf)
4. padrão: `~/.local/state/quickshell/user/generated/app-palette.json`

No iNiR, `colors.json` é a paleta da shell/UI; `app-palette.json` é a paleta
publicada para apps Qt/GTK. O launcher em `packaging/fm.desktop` fixa a fonte.

Se o JSON tiver `colors.dark`/`colors.light`, escolha com `FM_THEME_MODE=dark|light`.
Sem arquivo válido, usa a paleta padrão do Qt.

## Atalhos
Alt+←/→/↑ navegar · Ctrl+T/W abas · Ctrl+L barra de caminho · Ctrl+1 ícones/detalhes
F11 pré-visualização · Ctrl+H ocultos · Ctrl+C/X/V · F2 renomear · Del lixeira · Shift+Del excluir

## Estrutura
- src/ThemeManager   paleta + stylesheet a partir do JSON da shell (com watcher)
- src/ThumbnailProxy miniaturas assíncronas (proxy sobre QFileSystemModel)
- src/FileTab        views, painel de lugares, preview, operações, compactar/extrair
- src/MainWindow     abas, toolbar, barra de caminho
