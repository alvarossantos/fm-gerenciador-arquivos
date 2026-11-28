# Design — identidade visual com três presets (Issue #18)

## Intenção

Refinar o `fm` mantendo **uma única base de código**. As três direções comparadas
(Compacta, Equilibrada e Avançada) viram presets selecionáveis da interface, com
a **Equilibrada como padrão**. O usuário aprovou as três como presets em 2026-11-27,
substituindo o critério original de "uma direção aprovada" por "três direções
disponíveis, com padrão explícito".

Parte-se do que já existe: navegação/breadcrumb e `Ctrl+L` (#5), menus por alvo
e `Abrir com…` (#6), `ThemeManager` com paleta gerada pelo iNiR. Não se reimplementa
nenhum desses recursos.

## Objetivos

- Alternar entre Compacta, Equilibrada e Avançada sem reconstruir a janela; trocar
  de preset aplica os padrões do preset escolhido e reinicia ajustes manuais.
- Persistir preset, densidade, colunas, tamanho de ícones e prévia; `Restaurar padrões`
  volta para a Equilibrada, remove ajustes manuais, zera o zoom para 100% e reaplica
  as preferências em todas as abas.
- Barra superior, lateral, menus e propriedades reagem ao preset, com a mesma
  paleta e estados claros de foco, seleção e desabilitado.
- Janela de propriedades útil para arquivo, imagem e pasta; análise de tamanho
  e contagem de pastas em segundo plano, cooperativa com cancelamento e sem travar a UI.
- Continuar utilizável em janela estreita, por teclado e por mouse.

## Não-objetivos

- Não criar três classes de janela, três binaries ou branches por direção.
- Não mudar a semântica dos menus por alvo (#6), `Colar` na pasta da aba, `Ctrl+L`,
  histórico por aba, favoritos (#9), dispositivos (#13), painel dividido (#14)
  ou terminal embutido (#15).
- Não trocar `QTabWidget`, `QTreeView`, `QListView` ou o `ThemeManager`.
- Não adicionar dependências: apenas Qt6 Widgets/Test já usados (sem QtConcurrent).

## Abordagens consideradas

**A — presets sobre os widgets atuais (escolhida).** Um `UiPreset` e um
`UiPreferences` pequenos em `FileTab.h`; `MainWindow` persiste e aplica na barra,
`FileTab` aplica na lateral, listas e menus. Custo: um conjunto de parâmetros por
preset e um diálogo novo de propriedades. Vantagem: menor diff, testes existentes
continuam válidos, nenhuma duplicação de UI.

**B — três classes de layout (`CompactView`, `BalancedView`, `AdvancedView`).**
Rejeitada: triplicaria conexões de modelo, atalhos e ciclo de vida para ganho zero
de comportamento; quebraria os hooks de teste atuais.

**C — cada direção em uma branch/PR separado.** Rejeitada: a Issue #18 pede
comparação e escolha; manter as três como presets preserva a comparação após o merge
e evita três revisões do mesmo `FileTab`.

## Decisões de implementação

### Modelo e persistência

- `enum class UiPreset { Compact, Balanced, Advanced }` e `struct UiPreferences`
  em `src/UiPreferences.h`, incluído por `FileTab.h`, `MainWindow.cpp` e pelo diálogo
  de propriedades, com campos: `preset`, `density` (0..2), `iconSize` (32/48/64),
  `columns` (`QStringList` de índices de coluna do modelo), `preview` (bool).
- `QSettings("fm","fm")` ganha chaves `ui/preset`, `ui/density`, `ui/iconSize`,
  `ui/columns`, `ui/preview`. Leitura em `MainWindow` antes de criar abas; escrita
  imediata ao trocar preset ou ajuste. Chave ausente usa o valor padrão **do preset
  selecionado**; valor inválido também cai nesse padrão — mesma política já usada
  para JSON de tema inválido.
- Trocar de preset substitui a preferência corrente pelos padrões do preset escolhido:
  ajustes manuais anteriores são descartados, não mesclados, e a mudança é reaplicada
  em todas as abas abertas.
- `Restaurar padrões` (ação no menu de personalização da barra) grava a Equilibrada
  com `density=1`, `iconSize=48`, colunas padrão e `preview=false`, remove ajustes
  manuais, zera o zoom para 100%, reaplica em todas as abas e emite `statusMessage`.
  O estado restaurado deve persistir após destruir e recriar `MainWindow`.
- Os três presets e `Restaurar padrões` ficam num submenu `Interface` do `⋮` da
  toolbar, com ações checáveis. `Mais…` (Compacta) e `Personalizar…` (Avançada) na
  lateral ancoram esse mesmo menu; não há diálogo novo de personalização.

### Barra superior (`MainWindow`)

- A toolbar existente é reaproveitada: `m_back`, `m_forward`, `m_up`,
  `m_pathContainer`, `m_iconsAct`, `m_previewAct`, `m_hiddenAct`, ações de zoom e
  Nova aba. Presets alteram **visibilidade/estilo**, não criam ações paralelas.
- Compacta: só navegação + caminho; controles de visualização e zoom ficam num
  menu `⋮` e o breadcrumb mostra no máximo 2 segmentos antes do overflow.
- Equilibrada: mantém Novo e a alternância Lista/Ícones visíveis; breadcrumb até
  4 segmentos; prévia/ocultos/zoom no `⋮`.
- Avançada: mantém Novo, Lista/Ícones, prévia, ocultos e zoom visíveis; breadcrumb
  sem limite artificial (o overflow existente continua decidindo por largura).
- Um controle recolhido da toolbar **não destrói nem desconecta sua `QAction`**: a
  mesma instância continua no menu `⋮` e ligada ao atalho. Na Compacta e em janela
  estreita, Lista/Ícones, prévia, ocultos e zoom devem continuar acionáveis por menu
  e teclado, com o mesmo estado checado/habilitado de antes do recolhimento.
- `Ctrl+L`, `beginPathEdit`/`endPathEdit`, histórico por aba e menu de ancestrais
  do `m_overflow` permanecem intactos. Em janela estreita, ações não essenciais
  são movidas para o `⋮` conforme o preset; os ancestrais continuam acessíveis.

### Painel lateral e listas (`FileTab`)

- `m_places` passa a ter cabeçalhos de grupo (`Lugares`, `Da aba`, `Dispositivos`)
  como itens não selecionáveis, marcados com um papel explícito (por exemplo,
  `Qt::UserRole` com valor `Section`). A lógica que identifica o lugar ancestral
  **ignora** esses cabeçalhos; só itens de lugar participam da correspondência.
  Presets apenas escondem grupos: Compacta = só Lugares (+ `Mais…`),
  Equilibrada = Lugares e Da aba, Avançada = todos + entrada `Personalizar…`.
- `applyUiPreferences()` aplica: `density` na altura das linhas, `iconSize` nas
  duas visões, `columns` escondendo colunas no `QTreeView`, `preview` via
  `setPreviewVisible`, e ``zoom`` resetado a 100 quando o preset muda. `setZoom`
  existente continua funcionando depois disso.
- Coordenação: `Da aba` hospeda Favoritos/Recentes/Lixeira (#9) e `Dispositivos`
  a #13 sem implementar essas issues aqui.

### Menu de contexto (`FileTab::showContextMenu`)

- O alvo e as ações continuam exatamente os da #6 (área vazia, arquivo, pasta,
  seleção múltipla; lista e grade). `Colar` continua endereçando `m_path`.
- O preset controla apenas **detalhe**: Compacta = rótulos e separadores curtos,
  sem coluna de atalhos; Equilibrada = separadores por grupo e atalhos; Avançada =
  submenus completos (`Abrir com…`, `Exibir`, `Compactar…`) e atalhos em todos os níveis.
- `Abrir com…`, copiar caminho, terminal, compactar e extrair permanecem disponíveis
  quando aplicáveis, independente do preset.

### Propriedades (novo `src/PropertiesDialog.h/.cpp`)

- Substitui o `QMessageBox::information` atual por um `QDialog` não modal, com
  ícone/miniatura, nome e tipo no cabeçalho, seções `Geral`, `Datas`, `Permissões`
  e botões `Copiar caminho` e `Abrir pasta pai` (desabilitado quando não fizer sentido).
- Layout por preset: Compacta = seções recolhidas; Equilibrada = abas;
  Avançada = seções simultâneas com trilha lateral e estado da varredura.
- Para diretórios, tamanho e contagem de pastas são calculados por um worker em
  `QThread` (sem QtConcurrent); o worker não guarda ponteiro do diálogo — usa um
  token atômico compartilhado para cancelamento e um sinal conectado ao diálogo,
  que é automaticamente desconectado quando o receptor morre.
  A varredura usa tamanho aparente (`QFileInfo::size`), inclui arquivos ocultos,
  **não segue symlinks**, conta pastas recursivamente e ignora subpastas ilegíveis
  com aviso no resultado.
- A varredura é cooperativa: o worker verifica um pedido de interrupção a cada
  item e emite um sinal de conclusão ao sair. Ao fechar o diálogo, basta pedir a
  interrupção — **nenhum `wait()` na thread da interface**, que poderia travar
  justamente no cenário de pasta grande. O worker termina de forma assíncrona e
  libera seus recursos no próprio `finished`. Ponteiros crus não são retidos.
- Teste obrigatório: fechar o diálogo durante a varredura e reabrir propriedades
  de outro item sem travar e sem receber resultado do diálogo anterior.
- Ícone de arquivo vem de `QMimeDatabase`; miniatura de imagem pode ser obtida
  pelo `ThumbnailProxy` existente, sem nova fila de trabalho.

### Tema e estados

- `ThemeManager` continua sendo a única fonte de paleta. Ajuste mínimo no QSS:
  regras explícitas de `:focus` para `QTreeView`/`QListView`, `:disabled` para
  itens de menu e botões, mantendo `Highlight`/`HighlightedText`/`Disabled` da
  paleta. Isso cobre barra, lateral, menus e diálogo sem folhas de estilo por preset.

## Verificação

- TDD por sprint; `filetab_test`, `mainwindow_test`, `theme_manager_test` e um
  novo `properties_dialog_test` (ou slots em `filetab_test`, se não justificar alvo).
- Cenário de teste: `QTemporaryDir`, presets aplicados e persistidos via
  `QSettings` com `XDG_CONFIG_HOME` temporário; restauração exata das variáveis.
- Cobertura mínima: preset persistido após destruir/recriar `MainWindow`;
  `Restaurar padrões` voltando à Equilibrada e persistindo após reiniciar o `fm`;
  troca de preset descartando ajustes manuais; chave `QSettings` ausente caindo no
  padrão do preset; colunas/ícone/densidade/prévia; `QAction` compartilhada viva no
  menu e no teclado na Compacta e em janela estreita; cabeçalhos de grupo fora da
  identificação de lugar ancestral; menu por alvo preservado; `Colar` na pasta da
  aba; propriedades de arquivo/imagem/pasta; varredura assíncrona cancelada ao
  fechar, com reabertura em outro item; foco/seleção/desabilitado com paleta válida
  e inválida; breadcrumb em janela estreita com `Ctrl+L`.
- Builds Debug/Release sem warnings; `ctest -V` completo em ambos.
- `--bench` Release: **sete rodadas no total, primeira descartada, mediana das seis
  restantes**. Baseline medido antes das mudanças: **92,0 ms**. Comparar com a
  mediana final no mesmo ambiente; registrar ambas em `docs/superpowers/validation/`.
- Validação visual no iNiR/Niri com capturas: barra, lateral, menus, propriedades
  e troca de paleta ao vivo, uma vez por preset.

## Sprints propostas

1. **Preferências e densidade** — enum/struct, `QSettings`, troca de preset,
   `Restaurar padrões`, toolbar/breadcrumb/lateral/colunas/ícones/prévia nos três presets.
2. **Menus e propriedades** — detalhe do menu por preset e `PropertiesDialog`
   com varredura assíncrona cooperativa e ações.
3. **Tema, verificação e PR** — estados de foco/desabilitado, suíte completa,
   bench antes/depois, capturas e PR referenciando `#18`.

## Pendente para o implementador

- Registrar a side-by-side usada na decisão em
  `docs/superpowers/validation/2026-11-27-identidade-visual-propostas.html`
  (cópia da tela do brainstorming).
- Fontes consultadas: iNiR theming (docs migraram para `inir.space/docs`),
  `~/.local/state/quickshell/user/generated/app-palette.json` e `src/ThemeManager.cpp`.
