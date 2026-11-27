# Menus de contexto e criação — Issue #6

## Intenção

Fazer o menu de contexto responder ao tipo do alvo e ampliar a criação por tipo MIME
e por modelos, sem duplicar atalhos nem criar um segundo centro de estado para a
visualização. O ponto que motivou a revisão foi o menu da área vazia: hoje ele
mostra as mesmas ações de item e não oferece forma de ajustar a visão ali mesmo.

## Objetivos

- Menu diferente para área vazia, arquivo, pasta e seleção múltipla, com o alvo
  correto em cada caso.
- `Abrir com…` baseado no tipo MIME, com os aplicativos do sistema, e fallback
  para executável avulso.
- Submenu `Novo` agrupado por tipo e modelos da pasta pessoal.
- Submenu `Exibir` com todos os controles existentes, reaproveitando as `QAction`s
  da barra — estado e atalhos permanecem na `MainWindow`.

## Não objetivos

- Abertura em lote (`Abrir com…` só para um arquivo).
- `Fixar em Favoritos` (#9) e `Renomear em lote` (#12).
- Registrar atalhos novos: as ações injetadas não entram em `MainWindow::addAction`.
- Persistir a escolha de `Abrir com…`: ela vale só para aquela abertura.

## Anti-objetivos

- Montar linha de comando para a shell ou interpolação de caminho em `Exec`.
- Criar ações paralelas para modo/zoom/preview/ocultos com estado próprio:
  isso deixaria barra e menu divergentes.
- Limpar a seleção quando o clique direito cair sobre item já selecionado.

## Restrições

- Qt 6.12, C++, `QMenu`/`QAction`, `QProcess` com lista de argumentos.
- `xdg-mime` e `gio` estão disponíveis na sessão (verificado em 2026-11-27).
- A `MainWindow` é dona do estado de exibição compartilhado entre abas;
  `FileTab` apenas consome as ações e constrói o menu.
- Pasta de modelos: `QStandardPaths::TemplatesLocation`.
- Testes com `QT_QPA_PLATFORM=offscreen`, `CTest` e pasta descartável.

## Abordagens consideradas

### Submenu Exibir

1. **Injetar as `QAction`s da `MainWindow` no `FileTab` (escolhida).**
   `FileTab::setViewActions(...)` recebe os ponteiros de modo, prévia, ocultos e
   zoom. O menu inseriu as próprias `QAction`s; estado, texto e atalhos vêm da
   barra. Custo: um método novo no `FileTab` e uma chamada em `addTab()`.
2. Mover as ações para o `FileTab` e fazer a barra consumi-las. Sem injeção,
   mas mexe na barra e nos testes da MainWindow; diff maior.
3. Emitir o clique direito e deixar a `MainWindow` montar o menu. Sem injeção,
   mas o menu passa a viver em dois lugares e a janela precisa conhecer seleção.

### Abrir com…

1. **`xdg-mime` + `gio` (escolhida).** `xdg-mime query default` põe o padrão na
   frente; `gio mime` fornece as associações; `gio launch` interpreta `Exec` de
   `.desktop` sem shell. `QFileDialog` + `QProcess` para executável avulso.
2. Ler `mimeapps.list` e fazer parsing de `Exec` à mão. Mais frágil (campos
   `FieldCode`, aspas, `%f`/`%U`) e duplica o que o GIO já faz.

## Decisões de implementação

### Alvo do clique direito (`FileTab::showContextMenu`)

- Item clicado **já selecionado**: manter `selectedPaths()` intacto; o menu
  fala com o grupo.
- Item clicado **fora da seleção**: `clearSelection()`, selecionar só aquele
  item (coluna 0, linhas) e usá-lo como alvo.
- Área vazia: `clearSelection()`; o alvo é `m_path` (pasta atual).
- As ações capturam `sel` por valor quando o menu é construído, para que o
  alvo não mude se a seleção mudar enquanto o menu está aberto.

### Composição dos menus

- **Área vazia:** Nova pasta…, Novo ▸, Colar, Atualizar, Exibir ▸,
  Abrir terminal aqui, Propriedades da pasta.
- **Arquivo:** Abrir, Abrir com… ▸, Copiar caminho, Novo ▸, Copiar, Recortar,
  Colar, Renomear, Mover para a lixeira, Excluir permanentemente, Compactar…,
  Propriedades.
- **Pasta:** Abrir, Abrir em nova aba, Abrir terminal nesta pasta, Copiar caminho,
  Novo ▸, Copiar, Recortar, Colar, Renomear, remoção, Compactar…, Propriedades.
- **Seleção múltipla:** Copiar, Recortar, Copiar caminhos, Colar, Compactar…,
  Mover para a lixeira, Excluir permanentemente. Sem Renomear/Propriedades de um
  item só. `Novo ▸` permanece disponível, como hoje.
- `Colar` continua desabilitado quando a área de transferência não tem URLs.

### Submenu Novo

Ordem: Pasta…; Arquivo vazio…; Texto (.txt); Markdown (.md); separador;
`Código e dados ▸` com HTML, JSON, shell e Python; separador; `Modelos ▸` com os
arquivos de `TemplatesLocation`, quando houver. Nomes, extensões e conteúdos
devem permanecer os atuais.

### Submenu Exibir

- `Lista detalhada` e `Grade de ícones`: ações locais ao submenu, `checkable`,
  sem atalho. A marcação vem de `m_iconsAct->isChecked()`. Escolher um modo
  chama `m_iconsAct->toggle()` **somente se** o modo atual for diferente — a
  mesma ação da barra, sem registrar um segundo atalho.
- Prévia, Arquivos ocultos, Aumentar zoom, Diminuir zoom, Restaurar zoom: as
  próprias `QAction`s da barra, inseridas diretamente. Separadores antes de
  Prévia e antes do zoom.
- O submenu é montado a cada abertura, então a marcação reflete o estado
  corrente da janela também em outra aba.

### Abrir com…

- MIME via `QMimeDatabase::mimeTypeForFile(QFileInfo(path)).name()`.
- Lista: `xdg-mime query default <mime>` (primeiro) e `gio mime <mime>` executado
  com `LC_ALL=C` para que o cabeçalho seja estável (`Default application for`,
  `Registered applications`); deduplicar por id `.desktop`, preservar a ordem;
  cada entrada mostra o nome legível do `.desktop` quando disponível.
- Lançamento: `gio launch <id.desktop> <caminho>` via `QProcess::startDetached`
  com argumentos separados. `Escolher executável…` usa `QFileDialog` e inicia o
  binário com o caminho como argumento, sem shell.
- Cancelamento do diálogo não inicia nada. Sem associações, o submenu ainda
  oferece `Escolher executável…`.
- Para testes, um hook de lançamento captura `(comando, argumentos, cwd)` em vez
  de iniciar processos reais, seguindo o padrão dos hooks já existentes.

### Atualizar e terminal

- `Atualizar`: `QFileSystemModel` não expõe `refresh()` público; a re-leitura é
  forçada por `setRootPath(m_path)`, com o `rootIndex` do proxy restaurado em
  seguida. Verificado no Qt 6.12 desta máquina.
- Terminal externo: resolver o primeiro disponível entre `x-terminal-emulator`,
  `foot`, `kitty`, `alacritty`, `konsole`, `gnome-terminal`, `xfce4-terminal`;
  nesta máquina `kitty`, `alacritty` e `konsole` estão presentes. Iniciar com
  diretório de trabalho igual à pasta do alvo.

## Critérios de aceite

- Clique direito real na lista e na grade abre o menu correto para vazio,
  arquivo, pasta e seleção múltipla.
- Clique em item já selecionado preserva o grupo; clique fora da seleção torna
  aquele item o único alvo; as ações agem sobre o alvo esperado.
- `Colar` desabilitado com área de transferência vazia.
- Criar por tipo e por modelo gera nome, extensão e conteúdo esperados.
- `Abrir com…`: arquivo com associações, sem associações, caminho com espaços e
  acentos e cancelamento sem iniciar aplicativo.
- Escolher `Grade de ícones` pelo menu, voltar à lista pela barra e reabrir
  `Exibir` em outra aba: marcação e barra refletem o mesmo estado.
- Build Release sem warnings; `ctest` em Debug e Release.

## O que fica para o builder

- Confirmar o comportamento exato de `QAction::toggle()` no submenu de modo e
  usar o caminho que não inverte o estado quando o modo já é o desejado.
- Definir o nome do hook de teste e a forma de simular `xdg-mime`/`gio`.
- Ordem final dos separadores e ícones, seguindo o que já existe no menu.
