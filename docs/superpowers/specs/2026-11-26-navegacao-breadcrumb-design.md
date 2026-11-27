# Navegação: breadcrumb clicável e destaque da pasta ativa

Issue: #5 · Branch: `feat/navegacao-visual` · Base: `main` em `b780f73`.

## Intenção

Substituir o campo único de caminho por segmentos clicáveis e mostrar no painel lateral onde cada aba está. É o primeiro recorte do redesenho: define a navegação antes de mexer em grade, lista e abas. Quem usa o fm passa a ver a pasta atual sem precisar ler uma linha inteira e consegue voltar a qualquer ancestral com um clique.

## Recorte deste documento

Entra: breadcrumb na `MainWindow`, modo de edição com `Ctrl+L`/`Enter`/`Esc`, destaque de lugar no `FileTab`, testes em `mainwindow_test`.

Fica para o próximo recorte da mesma branch: grade, lista e abas (aparência, zoom, colunas, faixa de abas com uma aba só), capturas no Umbriel/iNiR, checagem de foco e paleta ao vivo e `--bench` em Release com comparação contra 180 ms.

## Objetivos

- Breadcrumb acompanha `pathChanged` da aba ativa e a troca de aba; cada segmento navega com `goTo()` na aba ativa.
- `Ctrl+L` abre o campo com o caminho atual selecionado; `Enter` navega; `Esc` descarta a edição e restaura os segmentos da pasta atual.
- Painel lateral destaca o lugar ancestral mais específico da pasta atual, sem alterar caminho nem histórico.
- Caminhos longos e janela estreita mantêm a pasta atual visível e oferecem todos os ancestrais.

## Não-objetivos

- Mudar a política de histórico: ele continua por aba, em `FileTab::m_history`.
- Criar favoritos, recentes, busca ou dispositivos; estes são PRs posteriores do roadmap.
- Medir performance ou comparar capturas neste recorte.

## Anti-objetivos

- Breadcrumb que navegue na aba errada após troca de aba.
- Destaque lateral que chame `goTo()` ou limpe seleção de arquivos da view.
- Campo de edição que feche ao receber caminho inválido,perdendo o que foi digitado.

## Restrições

- Qt 6 Widgets, C++17, sem dependências novas.
- Toolbar atual já tem Voltar/Avançar/Subir, ações de exibição e Nova aba; o breadcrumb entra sem deslocar os atalhos existentes.
- `QStandardPaths` fornece os lugares; a ordem e os ícones atuais são preservados.
- Testes rodam offscreen; `QStandardPaths::setTestModeEnabled` e um hook de lugares tornam os caminhos determinísticos.

## Abordagens

**A — botões por segmento na toolbar + `QLineEdit` no modo de edição (escolhida).** Um widget leve na toolbar contém `QToolButton`s com o caminho acumulado de cada segmento, um botão `…` com menu dos ancestrais agrupados e o `QLineEdit` escondido até `Ctrl+L`. Mantém histórico por aba e permite elidir rótulos e testar por `objectName`. Custo: lógica local na `MainWindow` e um novo alvo de teste; sem classe reutilizável, o que é suficiente para um único uso.

**B — `QAction` por segmento.** Menos código, mas ação de toolbar não elide texto nem oferece menu por segmento de forma confiável; pior justamente em caminhos longos e janela estreita. Descartada.

## Decisões de comportamento

- **Segmentos:** raiz `/`, cada componente do caminho e a pasta atual. Rótulo longo usa elipse no meio; tooltip traz o nome completo. Clique chama `goTo()` na aba ativa e entra no histórico daquela aba.
- **Caminho longo / janela estreita:** manter visíveis o ancestral inicial e a pasta atual; os demais são agrupados num botão `…` com `QMenu` contendo uma ação por ancestral oculto, com o caminho completo. Se não houver espaço, a própria raiz entra no menu. Recalcular ao redimensionar e cobrir com teste: estreitar a janela, conferir a pasta atual visível, abrir o menu, verificar cada ancestral oculto e navegar por um deles.
- **Edição:** `Ctrl+L` mostra o campo, foca e seleciona. `Enter` com caminho válido navega e volta aos segmentos. `Enter` com caminho inválido mantém o campo aberto com o texto digitado, informa o erro no status e não altera pasta nem histórico. `Esc` volta aos segmentos da pasta atual. Trocar de aba encerra a edição, descarta o texto pendente e mostra o breadcrumb da aba recém-ativada — assim um `Enter` posterior não aplica o caminho digitado na aba errada.
- **Destaque lateral:** comparar caminhos limpos por segmentos. Casa exata primeiro; depois o ancestral mais específico; `Pasta pessoal` como fallback para subpastas da home; `Raiz (/)` apenas para `/`. `~/Documentos2` não casa com `~/Documentos`. O item da home passa a se chamar `Pasta pessoal`.
- Atualizar o destaque só muda a seleção visual do `QListWidget`: nunca chama `goTo()`, nunca mexe em `m_path`, `m_history` ou seleção da view. A cobertura é feita pela navegação normal (`goTo()`, Voltar e Avançar), observando a seleção do painel; `updatePlacesSelection()` continua privado, sem `Q_INVOKABLE`.

## Implementação prevista

- `MainWindow`: widget de breadcrumb com `objectName` nos botões e no campo; funções privadas para construir segmentos, medir largura, elidir e abrir o menu `…`; ligação a `pathChanged` e `currentChanged`, encerrando a edição neste último; `Esc` via shortcut com contexto restrito ao campo. `FileTab::goTo()` passa a retornar `bool` para que `Enter` saiba se pode sair do modo de edição; o erro continua sendo reportado pelo sinal `statusMessage`.
- `FileTab`: `void updatePlacesSelection()` privado, com a regra de ancestral e chamada ao fim de `goTo()`; hook de teste para definir os lugares; `itemClicked` continua navegando normalmente.
- `tests/mainwindow_test.cpp`: novo alvo CTest ligando `MainWindow.cpp`, `FileTab.cpp`, `ThumbnailProxy.cpp` e `ThemeManager.cpp`. Casos:
  1. breadcrumb e destaque acompanham a troca entre duas abas (`~/Documentos/projeto` e `~/Downloads/teste`);
  2. clique num segmento seguido de Voltar usa o histórico da aba ativa;
  3. destaque observado via `goTo()`, Voltar e Avançar, com pasta e histórico preservados;
  4. `Documentos2` não herda o destaque de `Documentos`;
  5. `Ctrl+L`/`Enter`/`Esc`, incluindo caminho inválido;
  6. menu `…` em janela estreita, com navegação por um ancestral oculto;
  7. troca de aba descarta texto pendente e mostra o breadcrumb da aba ativada.

## Evidência e pendências

Build Release sem warnings e `ctest -V` completo são obrigatórios antes do PR. Capturas da lista e da grade, foco por teclado, troca de paleta ao vivo e sete rodadas de `--bench` (descartar a primeira, mediana das seis contra 180 ms) ficam para o fim do recorte maior da Issue #5, ainda nesta branch.
