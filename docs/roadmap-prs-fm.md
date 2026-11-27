# Roadmap de PRs do fm

O fm é um gerenciador de arquivos em C++/Qt Widgets. A interface usa como referência a organização visual do Nautilus, com barra superior compacta, painel de lugares e grade de pastas. O fm mantém a paleta da shell e oferece prévia de imagens, modelos de criação, operações de arquivos e gerenciamento de compactados.

**Base de trabalho** · `main` em `b780f73` e release `v0.1.0` em `db6f547`. Os números abaixo indicam a ordem proposta, independentemente da numeração no GitHub.

## Fila de implementação

| Ordem | Título sugerido | Dependência funcional |
|---|---|---|
| 1 | Redesenha a navegação e as visualizações | — |
| 2 | Amplia os menus de contexto e os modelos de criação | — |
| 3 | Adiciona cancelamento à compactação | — |
| 4 | Executa cópia e movimentação em segundo plano | Extensão da interface de operações do PR 3 |
| 5 | Adiciona favoritos, recentes e página inicial | Painel lateral do PR 1 |
| 6 | Adiciona busca recursiva na pasta atual | Barra superior do PR 1 |
| 7 | Adiciona desfazer para operações de arquivos | PR 4 para estender o desfazer a movimentações |
| 8 | Adiciona renomeação em lote com prévia | PR 7 se a operação entrar no histórico |
| 9 | Integra dispositivos com UDisks2 | Painel lateral do PR 1 |
| 10 | Adiciona navegação com painel dividido | PRs 1 e 4 |
| 11 | Adiciona terminal embutido | PR 10 |

## PR 1 · Navegação e identidade visual

**Branch sugerida** · `feat/navegacao-visual`

- Reorganizar a barra superior em navegação, caminho e controles de exibição. Trocar o campo dominante de caminho por segmentos clicáveis. Usar `Ctrl+L` para editar, `Enter` para navegar e `Esc` para voltar aos segmentos.
- Dar mais espaço aos locais do painel lateral e indicar a pasta ativa. Esconder a faixa de abas quando houver uma única aba e manter visíveis os controles para abrir novas abas.
- Melhorar leitura da grade e da lista. Ajustar ícones, espaçamento, seleção, foco, larguras das colunas e alinhamento numérico. Oferecer controles de zoom na grade. Manter a prévia lateral opcional.
- Usar as cores semânticas da paleta da shell para fundo, texto, destaque, seleção e foco. Colocar a busca, os favoritos e os dispositivos na interface quando as respectivas funções estiverem prontas.

**Aceite** · Os segmentos funcionam com caminhos longos, espaços e caracteres acentuados. Voltar e avançar preservam o histórico de cada aba. Lista, grade e prévia continuam funcionando com teclado e mouse. Verificar janela estreita, foco visível e troca de paleta em tempo real no Umbriel/iNiR. Anexar capturas de lista e grade e a medição de abertura conforme o protocolo ao fim do documento.

## PR 2 · Menus de contexto e criação

**Branch sugerida** · `feat/menus-de-contexto`

| Contexto | Ações previstas |
|---|---|
| Área vazia | Nova pasta, Novo, Colar, Atualizar, Exibir, Abrir terminal aqui e Propriedades da pasta |
| Arquivo | Abrir, Abrir com…, Copiar caminho, ações de edição e propriedades |
| Pasta | Abrir em nova aba, Abrir terminal nesta pasta, Copiar caminho, ações de edição e propriedades |
| Seleção múltipla | Copiar, Recortar, Copiar caminhos, Compactar e ações de remoção aplicáveis à seleção |
| Arquivo compactado | Extrair aqui e Extrair em pasta própria |

- Organizar o submenu **Novo** com pasta, arquivo vazio, texto e Markdown próximos do topo. Agrupar HTML, JSON, shell e Python em **Código e dados** e manter os modelos da pasta pessoal em **Modelos**.
- Usar o tipo MIME para **Abrir com…** e abrir o terminal externo na pasta correspondente. Exibir atalhos existentes nos menus sem duplicar o processamento das teclas.
- Acrescentar **Fixar em Favoritos** e **Renomear em lote** quando os respectivos recursos estiverem disponíveis.

**Aceite** · Clique real sobre item e área vazia abre o menu correto tanto na lista quanto na grade. Clicar com o botão direito sobre um item de uma seleção múltipla preserva a seleção. As ações recebem o alvo correto. Colar fica indisponível quando a área de transferência está vazia. Criar arquivo por tipo e por modelo gera o nome, a extensão e o conteúdo esperados.

## PR 3 · Compactação com cancelamento

**Branch sugerida** · `feat/compactacao-cancelavel`

- Reaproveitar o fluxo assíncrono da extração. Gravar em temporário no mesmo sistema de arquivos do destino, mantendo a extensão necessária para o backend selecionar o formato. Publicar o pacote no nome final somente depois que o processo terminar com sucesso.
- Mostrar operação indeterminada com **Cancelar**. Tratar colisões no destino e preservar arquivos já existentes. Fechar a aba segue a política de cancelamento e limpeza usada na extração.

**Aceite** · A interface continua navegável durante a criação de ZIP e TAR.ZST. Os pacotes concluídos podem ser extraídos e têm o conteúdo esperado. Cancelamento, falha do backend ou destino ocupado preservam arquivos existentes e deixam o temporário sujeito à mesma garantia de encerramento seguro da extração.

## PR 4 · Cópia, movimentação e painel de operações

**Branch sugerida** · `feat/operacoes-em-segundo-plano`

- Executar cópia e movimentação fora da thread da interface. Exibir itens processados, bytes quando houver total conhecido, cancelamento e resumo dos resultados.
- Capturar origem, seleção e destino no início. Definir política de conflito por operação e publicar cada cópia após sua conclusão. Ao mover entre sistemas de arquivos, só remover a origem depois de concluir com sucesso a cópia do item.
- Usar um painel de operações que continue acessível ao trocar de aba.

**Aceite** · Copiar e mover pastas grandes na mesma unidade e entre unidades, inclusive cancelar no meio de um lote. Verificar resultado por item, conteúdo preservado, colisões, ausência de sobrescrita silenciosa e destino correto após navegar para outra pasta.

## PR 5 · Início, favoritos e recentes

**Branch sugerida** · `feat/inicio-e-favoritos`

- Adicionar uma página inicial com pastas fixadas, atalhos e ordem editável. Permitir abrir o fm nessa página, na pasta pessoal ou na última pasta visitada.
- Persistir favoritos com `QSettings`. Registrar os itens abertos pelo próprio fm para **Recentes**, com opção de limpar. Oferecer **Lixeira** na lateral com visualização e restauração dos itens suportados pelo backend.

**Aceite** · Fixar, reordenar e remover atalhos funciona após reiniciar. Um caminho indisponível pode ser removido ou corrigido. Abrir o fm com caminho explícito continua indo direto ao destino. Testar restauração com caminho original livre e ocupado.

## PR 6 · Busca recursiva

**Branch sugerida** · `feat/busca-local`

- Buscar por nome a partir da pasta atual. Mostrar resultados incrementais, com caminho relativo, opção **Abrir pasta do resultado** e cancelamento. Usar botão na barra e `Ctrl+F`.
- Tratar diretórios ilegíveis e evitar seguir links de diretório por padrão. Identificar a pasta em que a busca começou e descartar os resultados de buscas substituídas por uma nova consulta.

**Aceite** · Busca em árvore grande mantém a interface responsiva. Cancelar, mudar de consulta e sair da busca produz resultados consistentes. É possível abrir o arquivo ou sua pasta e voltar à navegação.

## PR 7 · Desfazer com Ctrl+Z

**Branch sugerida** · `feat/desfazer-operacoes`

- Começar por renomeação e movimentação para a lixeira quando houver referência de restauração. Registrar operações concluídas e exibir a ação a desfazer. Estender a movimentações após integrar o PR 4.
- Tratar caminhos originais ocupados e itens alterados externamente. Operações irreversíveis seguem com confirmação própria.

**Aceite** · `Ctrl+Z` restaura o caminho e o conteúdo esperados. Conflitos preservam os dois itens e informam o motivo. Falhas e cancelamentos só registram operações efetivamente concluídas e reversíveis.

## PR 8 · Renomeação em lote

**Branch sugerida** · `feat/renomeacao-em-lote`

- Adicionar substituição de texto, prefixo, sufixo e numeração. Mostrar nomes anteriores e propostos lado a lado, com tratamento explícito da extensão.
- Validar duplicatas, colisões com itens fora da seleção e trocas de nomes entre itens. Informar o resultado de cada alteração se uma falha interromper o lote.

**Aceite** · A prévia coincide com o resultado. Uma regra inválida pode ser corrigida antes de aplicar. Testar Unicode, colisões e troca de nomes entre dois arquivos.

## PR 9 · Dispositivos via UDisks2

**Branch sugerida** · `feat/dispositivos-udisks2`

- Mostrar volumes conectados com estado de montagem. Acionar montagem, desmontagem ou ejeção conforme as capacidades informadas pelo serviço. Refletir mudanças sem reiniciar.
- Usar a autorização da sessão e informar erros de dispositivo ocupado ou permissão insuficiente.

**Aceite** · Testar chegada e remoção de unidade, montagem, abertura do ponto de montagem e desmontagem. Se o serviço estiver indisponível, a navegação local permanece utilizável.

## PR 10 · Painel dividido

**Branch sugerida** · `feat/painel-dividido`

- Dividir a aba em dois painéis com navegação e seleção independentes. Indicar visualmente qual está ativo. Adicionar **Copiar para o outro painel** e **Mover para o outro painel**.
- Vincular barra de caminho, prévia e atalhos ao painel ativo. Reusar as operações assíncronas do PR 4.

**Aceite** · Alternar foco preserva as duas pastas e seus históricos. Copiar ou mover usa a origem e o destino corretos. Fechar a divisão preserva o painel ativo.

## PR 11 · Terminal embutido

**Branch sugerida** · `feat/terminal-embutido`

- Abrir o terminal sob o painel ativo, com a pasta corrente como diretório inicial. Definir a integração com o emulador e seu empacotamento no próprio PR. Oferecer sincronização de pasta como ação explícita.
- Encaminhar atalhos ao terminal quando ele estiver em foco. Tratar fechamento com sessão em execução.

**Aceite** · O terminal abre na pasta correta, inclusive com espaços no caminho. Alternar entre painéis e abas respeita o contexto. Atalhos do gerenciador deixam de interferir na digitação do terminal.

## Estrutura de cada PR

1. **Objetivo** · Problema observado e resultado esperado para quem usa o fm.
2. **Escopo e dependências** · Alterações da entrega, pontos de integração e PRs necessários.
3. **Critérios de aceite** · Comportamentos verificáveis, incluindo teclado, mouse e falhas relevantes.
4. **Evidência** · Comandos, resultados, capturas para mudanças visuais e verificações manuais ainda pendentes.

Build Release e CTest fazem parte da verificação. Nas mudanças visuais, usar Umbriel/Wayland com a paleta do iNiR e testar grade, lista, teclado e uma janela estreita. Usar pasta descartável nas operações com arquivos. Nas mudanças de inicialização, medir com `--bench` na mesma sessão, com a mesma paleta e a mesma pasta de teste. Registrar sete rodadas válidas, descartar a primeira e comparar a mediana das seis restantes com a referência de **180 ms até a primeira pintura da janela principal**. Registrar também mudanças de dependências.

## Ideias para depois dos PRs acima

- **Prévia rápida por Espaço** para imagens e textos, com suporte a PDF em uma entrega posterior.
- **Ações personalizadas** para abrir ferramentas escolhidas pelo usuário a partir do menu de contexto.
- **Sessão restaurável** com abas e pastas abertas, além de modo e zoom lembrados por pasta.
