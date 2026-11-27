# Ajustes visuais finais da Issue #5

## Intenção

Fechar o recorte de apresentação da Issue #5 no mesmo `feat/navegacao-visual`: dar mais espaço ao painel de lugares, remover a faixa de abas quando ela não informa nada (uma aba só) e melhorar a leitura de grade/lista com zoom e refinamento de colunas, ícones, seleção e foco. A navegação em si já está pronta e não muda.

## Escopo

- Incluído: esconder `QTabBar` com uma aba (mantendo Nova aba e fechar aba acessíveis na toolbar e por atalho); zoom na grade (`Ctrl++`, `Ctrl+-`, `Ctrl+0`); ajustes de colunas, alinhamento numérico, espaçamento, ícones, seleção e foco nas duas visões; mais espaço para o painel de lugares.
- Não incluído: persistência de modo/zoom (fica para a issue de sessão restaurável); reconstruir a prévia lateral (F11 já funciona); substituir `QTabWidget`.

## Decisões

- **Abordagem A (escolhida):** ajustes in-place sobre `QTabWidget`, `QTreeView` e `QListView`. A alternativa B (faixa customizada) duplicaria mover/fechar/trocar de abas que o `QTabWidget` já fornece, sem ganho para o aceite.
- Zoom e modo vivem apenas enquanto o app está aberto — decisão do usuário, para não antecipar a issue de sessão restaurável.
- `setZoom(int)` na `FileTab` ajusta `iconSize`, `gridSize` e altura das linhas da visão em detalhes; ações na `MainWindow` aplicam o zoom na aba ativa e o refletem nas demais, mantendo coerência com o comportamento atual de `m_iconsAct`/`m_previewAct`/`m_hiddenAct`.
- Fechar aba continua disponível via `Ctrl+W` e ação na toolbar quando a faixa estiver escondida; Nova aba continua em `Ctrl+T` e na toolbar.
- Prévia lateral permanece opcional via F11, sem mudança de comportamento.

## Restrições

- CTest em Debug e Release, build Release sem warnings.
- Verificação visual no Umbriel/iNiR: lista, grade, foco por teclado, janela estreita e troca de paleta ao vivo.
- `--bench` Release: sete rodadas, descartar a primeira, mediana das seis contra 180 ms.
- Sem push direto em `main`; PR referencia `#5`.

## Verificação

Testes automatizados cobrem: faixa escondida com uma aba e visível com duas; botão Nova aba permanece visível; zoom altera `iconSize`/`gridSize` e respeita limites; ajustes de coluna/alinhamento não quebram seleção nem navegação. O restante (capturas, teclado, paleta, bench) segue o protocolo em `docs/superpowers/validation/2026-11-26-umbriel.md`.
