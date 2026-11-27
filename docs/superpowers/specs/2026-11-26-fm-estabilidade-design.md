# fm — estabilizar o esqueleto no Umbriel/iNiR

Data: 2026-11-26

## Intenção

Antes de adicionar recursos, provar duas coisas no desktop real: que o gerenciador
segue a paleta publicada pela shell ativa, e que o tempo até a primeira pintura é
baixo o suficiente para justificar a escolha de C++/Qt. A entrega não muda layout
nem elementos de UI, não adiciona dependências e não mexe em config gerenciada
pelo iNiR. As cores **podem** mudar de tom: a origem passa a ser
`app-palette.json`, que diverge de `colors.json` em 56 valores. Isso é intencional
e é justamente o que a validação de tema precisa comprovar.

Ambiente confirmado: CachyOS (Arch), compositor **Umbriel** (`/usr/bin/umbriel`),
shell **iNiR** (`inir-shell`), Wayland `wayland-0`. Qt 6.12.1, CMake 4.5.1 e
libarchive 3.8.11 já instalados. O tarball base compila em Release sem warnings.

## Objetivos

1. Fixar a fonte do tema no launcher, de forma explícita.
2. Fazer `ThemeManager` reter a última paleta válida durante gravações
   transitórias e reaplicar quando o JSON voltar a ser válido.
3. Medir o **tempo até a primeira pintura** com `--bench` e reportar a mediana
   de várias execuções Release.
4. Rodar o binário na sessão Umbriel e validar navegação, miniaturas e
   operações básicas em arquivos descartáveis.

## Não-objetivos

- Detectar automaticamente qual shell está ativa ou trocar a fonte com o app
  aberto (isso é a opção B, adiada).
- Cancelamento de extração, publicação atômica da pasta final, progresso
  percentual, desfazer, UDisks2, busca, split, terminal, breadcrumb.
- Trocar `QFileSystemModel` por modelo próprio ou migrar para QML (opção C).
- Instalação sistêmica, `sudo`, edição de `~/.config/umbriel/config.d/*`.

## Anti-objetivos

- Quebrar a compatibilidade com o formato de paleta do Ryoku
  (`~/.cache/ryoku/colors.json`, chaves camelCase): `ThemeManager::norm()` já
  unifica camelCase e snake_case; não regredir isso.
- Deixar o app com a paleta padrão do Qt quando o JSON estiver temporariamente
  inválido e já houver uma paleta aplicada.

## Restrições e decisões tomadas

- **Fonte do tema:** `~/.local/state/quickshell/user/generated/app-palette.json`.
  A documentação oficial do iNiR define `colors.json` como paleta da shell/UI e
  `app-palette.json` como paleta para targets de app; `gtk-kde.json`, o target de
  apps Qt/KDE, declara `app-palette.json` + `palette.json` como entradas. O
  `15-umbriel.sh` usa `colors.json` para as cores do compositor, não para apps.
  Os dois JSONs divergem em 56 valores (ex.: `on_surface`), então a escolha
  importa. Ambos cobrem todas as chaves que `ThemeManager` consulta.
- **Árvore canônica:** `~/Projetos/fm`, copiada do tarball
  `~/Downloads/fm-gerenciador-arquivos.tar.gz`, com `git init` e um commit da
  baseline antes de qualquer edição. `/tmp/opencode/fm-inspect/fm` é só a
  extração temporária usada para leitura; nada é editado lá. Decisão minha;
  o usuário pode trocar antes da aprovação.
- **Sem Issue remota:** projeto local, sem remote configurado. Cada entrega
  vira um commit com mensagem descritiva. Decisão minha.
- **Binary:** build Release dentro do repo; cópia para `~/.local/bin/fm` (escopo
  de usuário, sem sudo).
- **Launcher:** `~/.local/share/applications/fm.desktop`, com
  `Exec=/home/alvaro/.local/bin/fm --theme-file /home/alvaro/.local/state/quickshell/user/generated/app-palette.json %u`
  e `TryExec=/home/alvaro/.local/bin/fm`. Não existe arquivo anterior; se
  existir, faça backup antes de sobrescrever. Atualizar o banco com
  `update-desktop-database ~/.local/share/applications`.
- **`--bench` encerra após a primeira pintura.** Decisão minha: permite N
  execuções seguidas sem deixar janelas abertas na sessão do usuário.

## Abordagens consideradas

- **A — patch mínimo + validação (escolhida).** Três mudanças pequenas no
  código existente, launcher explícito e bateria de testes na sessão real.
  Não cria abstração nova.
- **B — adaptadores por shell.** Detector de formato/fonte e troca em tempo
  real. Adiada pelo próprio usuário para quando houver necessidade real.
- **C — modelo de arquivos próprio / QML.** Adiada; só entra se medições em
  pastas grandes apontarem o modelo como gargalo.

## O que será feito

### 1. `main.cpp`

- Manter a precedência atual `--theme-file` > `FM_THEME_FILE` > QSettings >
  padrão; apenas trocar o padrão documentado para `app-palette.json` e tirar o
  comentário "confira no seu iNiR".
- Adicionar `--bench`: iniciar `QElapsedTimer` na primeira linha de `main()`,
  ligar o sinal de primeira pintura do `MainWindow` e imprimir uma vez no
  stderr: `tempo até a primeira pintura: N ms`.

### 2. `MainWindow.h/.cpp`

- Override de `paintEvent` com flag one-shot e sinal `firstPaint()`.
- Em modo `--bench`, encerrar o app logo após a primeira pintura.

### 3. `ThemeManager.h/.cpp`

- `reload()` passa a não chamar `applyDefault()` quando o arquivo estiver
  ausente/inválido **e** já existir paleta aplicada (`m_applied == true`).
- Marcar `m_applied = true` em `applyShellColors()`;
  `applyDefault()` marca `m_applied = false`.
- Manter o `QFileSystemWatcher` no arquivo **e** no diretório (já existe) para
  sobreviver a substituição atômica via `mv`.

### 4. Validação

- Compilar Release sem warnings.
- Sequência de tema, com cópias do JSON real em arquivo temporário:
  válido → inválido (`{`) → válido → substituição atômica (`mv` de um
  `mktemp`). Conferir, por inspeção visual e/ou log, que as cores não voltam
  ao padrão na etapa inválida e que a nova paleta entra sozinha.
- `--bench`: sempre com o argumento `/tmp/opencode/fm-bench` (pasta vazia,
  criada antes, para que a métrica meça startup e não o conteúdo de `~`).
  7 execuções Release, descartar a primeira (aquecimento), mediana das 6
  restantes. Reportar a mediana e o intervalo; `~` fica de fora desta medição.
- Teste na sessão Umbriel com diretório descartável em `/tmp/opencode`:
  navegar (entrar/sair/voltar), abrir imagem para conferir miniatura, criar
  arquivo pelo menu Novo, copiar/colar e renomear, compactar e extrair.
  Nada tocar em `~/Documentos` ou `~`.

## Critério de aceite

- `--theme-file` aponta para `app-palette.json` no `.desktop` instalado.
- Paleta sobrevive a JSON inválido e a troca atômica, sem reiniciar o app.
- `--bench` imprime a métrica uma vez e sai; mediana reportada.
- Navegação, miniatura e operações básicas funcionam na sessão Wayland.
- Nenhuma mudança em arquivos gerenciados pelo iNiR ou em sistema.

## Próxima entrega (fora desta)

Cancelamento da extração com publicação da pasta final só após sucesso:
extrair para diretório temporário e renomear para o destino ao terminar com
sucesso; cancelamento cancela o `QProcess` e remove o temporário.

## Fontes consultadas

- https://inir.space/docs/theming-architecture/
- https://inir.space/docs/compositors/
- Configuração local: `~/.config/quickshell/inir/scripts/colors/targets/gtk-kde.json`,
  `~/.config/quickshell/inir/scripts/colors/modules/15-umbriel.sh`,
  `~/.config/umbriel/config.d/50-startup.toml`.
