# Extração cancelável com publicação atômica — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** cancelar a extração sem travar a UI e publicar o resultado final somente após sucesso do bsdtar.

**Architecture:** `extract()` passa a descompactar num `QTemporaryDir` oculto ao lado do destino. `runBsdtar()` recebe, opcionalmente, um `QProgressDialog` não modal, o caminho temporário e um callback de publicação; cancelar mata o `QProcess`, e cancelamento/falha removem o temporário. `Extrair em pasta própria` publica por rename; `Extrair aqui` move as entradas para a pasta atual depois do sucesso.

**Tech Stack:** C++17, Qt 6 (Widgets, Core), `bsdtar`, CTest/QtTest.

**Spec:** `docs/superpowers/specs/2026-11-26-extracao-cancelavel-design.md`

## Global Constraints

- Nenhuma dependência nova; apenas Qt6 e `bsdtar` já usados pelo projeto.
- `bsdtar` continua sendo o único executador da extração (proteção zip-slip).
- `runBsdtar()` deve manter comportamento atual para `compress()` quando os novos parâmetros não são passados.
- `QProgressDialog` deve ser `Qt::NonModal` — a interface precisa continuar responsiva.
- Nada de integração em `main`; todo trabalho fica em `feat/estabilidade`.
- Testes com `bsdtar` falso devem restaurar `PATH` ao final de cada caso.

## Review Focus

- `bsdtar` encerra com erro → nenhuma pasta final e nenhum resíduo: `extractionFailureDoesNotPublishFolder` (Task 1).
- Usuário clica Cancelar com arquivos já no temporário → processo morto, temporário removido: `extractionCancelRemovesTemporaryFolder` (Task 1).
- Extração termina com sucesso → pasta final aparece com o conteúdo: `extractionSuccessPublishesFolder` (Task 1).
- `Extrair aqui` falha → pasta atual permanece sem os arquivos parciais: `extractHereFailureLeavesCurrentFolderUntouched` (Task 2).
- `Extrair aqui` tem sucesso → arquivos aparecem na pasta atual: `extractHerePublishesFiles` (Task 2).

---

### Task 1: extração em pasta própria com cancelamento e falha limpa

**Files:**
- Modify: `src/FileTab.h` (declaração de `runBsdtar`, includes)
- Modify: `src/FileTab.cpp` (`runBsdtar`, `extract`)
- Test: `tests/filetab_test.cpp`

**Interfaces:**
- Consumes: `copyRecursive`, `removePath`, `uniquePath`, `archiveSuffix` (já existem em `FileTab.cpp`), `selectedPaths()`.
- Produces: `FileTab::runBsdtar(const QStringList &args, const QString &okMessage, QProgressDialog *progress = nullptr, const QString &scratch = QString(), std::function<bool()> publish = {})`; helpers de teste `installFakeBsdtar(const QString &dir, const QString &mode)` e `scratchDirs(const QString &parent)`.

- [ ] **Step 1: escrever os testes RED**

Em `tests/filetab_test.cpp`, adicionar `#include <QProgressDialog>` e `#include <QScopeGuard>` e, no namespace anônimo, `installFakeBsdtar(dir, mode)` — script `bsdtar` executável que interpreta `-C <dir>`, grava `parcial.txt` nesse diretório e age conforme `mode`: `ok` sai 0, `fail` sai 1, `slow` grava e termina com `exec sleep 30` (para o kill do `QProcess` encerrar também o filho). Adicionar `scratchDirs(parent)` listando só diretórios ocultos `.fm-extract-*`. Declarar três slots:
  - `extractionFailureDoesNotPublishFolder`: cria `arquivo.zip`, instala modo `fail`, guarda `PATH` com `QScopeGuard`, abre menu no arquivo e dispara `Extrair em pasta própria`; espera `statusMessage` começar com `Falhou:`; afirma que `QDir(root).filePath("arquivo")` não existe e `scratchDirs(root).isEmpty()`.
  - `extractionCancelRemovesTemporaryFolder`: modo `slow`; dispara a mesma ação; espera `QProgressDialog` aparecer e `parcial.txt` existir em algum scratch; chama `dlg->cancel()`; espera status `Extração cancelada`; afirma ausência da pasta final e `scratchDirs(root).isEmpty()`.
  - `extractionSuccessPublishesFolder`: modo `ok`; dispara a ação; espera `arquivo/parcial.txt` existir; afirma `scratchDirs(root).isEmpty()`.

Reutilizar `runMenuAction` e `openContextMenu(view, row)` já presentes no arquivo para abrir `Novo`/menu e disparar a ação — não introduzir novo mecanismo.

- [ ] **Step 2: confirmar que falham**

Run: `cmake --build build -j4 && ctest --test-dir build -R filetab_test -V`
Expected: os três casos novos falham — hoje a pasta final é criada antes do processo e não há cancelamento.

- [ ] **Step 3: implementar `runBsdtar` opcional**

Em `src/FileTab.h`, incluir `<QProgressDialog>` e `<functional>` e trocar a declaração pela assinatura da seção Interfaces. Em `src/FileTab.cpp`: manter o corpo atual quando `progress == nullptr`; quando não nulo, emitir `statusMessage(tr("Extraindo…"))`, guardar `canceled` num `std::shared_ptr<bool>` compartilhado entre `QObject::connect(progress, &QProgressDialog::canceled, ...)` (que chama `p->kill()`) e o lambda de `finished`; em `errorOccurred(FailedToStart)`, em `finished` com cancelamento/código != 0, remover `scratch` com `QDir(scratch).removeRecursively()` e fechar/`deleteLater()` o diálogo; em sucesso chamar `publish()`, e só emitir `okMessage` se `publish()` retornar `true`.

- [ ] **Step 4: implementar `extract` para `intoFolder == true`**

Em `extract()`: calcular `dest` com `uniquePath` como hoje, mas **não** criar `dest` ainda; criar `QTemporaryDir tmp(QDir(m_path).filePath(QStringLiteral(".fm-extract-XXXXXX")))`, chamar `tmp.setAutoRemove(false)`, montar `progress` não modal (`setRange(0, 0)`, `setMinimumDuration(0)`, `setCancelButtonText(tr("Cancelar"))`, `show()`) e passar a `runBsdtar`. O `publish` captura `scratch = tmp.path()` e `dest` por valor e retorna `false` se `dest` já existir, senão `QDir().rename(scratch, dest)`. Para `intoFolder == false`, deixar o corpo atual por enquanto (Task 2 a substitui).

- [ ] **Step 5: rodar os testes**

Run: `cmake --build build -j4 && ctest --test-dir build -R filetab_test -V`
Expected: os cinco casos de `filetab_test` passam (2 antigos + 3 novos, mais `initTestCase`/`cleanupTestCase`).

- [ ] **Step 6: commit**

```bash
git add src/FileTab.h src/FileTab.cpp tests/filetab_test.cpp
git commit -m "feat: cancela extracao e publica pasta final so no sucesso"
```

### Task 2: `Extrair aqui` sem arquivos parciais visíveis

**Files:**
- Modify: `src/FileTab.cpp` (`extract`, ramo `intoFolder == false`)
- Test: `tests/filetab_test.cpp`

**Interfaces:**
- Consumes: `runBsdtar(...)` da Task 1, `copyRecursive`, `removePath`, `scratchDirs`, `installFakeBsdtar`.
- Produces: nenhuma nova API pública; apenas o comportamento do ramo `intoFolder == false`.

- [ ] **Step 1: escrever os testes RED**

Adicionar dois slots:
  - `extractHereFailureLeavesCurrentFolderUntouched`: modo `fail`, dispara `Extrair aqui`; espera `Falhou:`; afirma que `parcial.txt` não existe em `root` e `scratchDirs(root).isEmpty()`.
  - `extractHerePublishesFiles`: modo `ok`, dispara `Extrair aqui`; espera `root/parcial.txt` existir; afirma `scratchDirs(root).isEmpty()`.

- [ ] **Step 2: confirmar que falham**

Run: `ctest --test-dir build -R filetab_test -V`
Expected: os dois casos novos falham enquanto a extração usa a pasta atual como destino direto.

- [ ] **Step 3: publicar as entradas depois do sucesso**

No ramo `intoFolder == false` de `extract()`, usar o mesmo `QTemporaryDir`/`QProgressDialog` da Task 1 e manter `okMessage = tr("Extraído em %1").arg(m_path)`. O `publish` itera `QDir(scratch).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)`; para cada entrada chama `removePath(dst)` se `dst` já existir, depois `QFile::rename(src, dst)`, com fallback `copyRecursive(src, dst) && removePath(src)`; retorna `false` se qualquer entrada falhar.

- [ ] **Step 4: rodar os testes**

Run: `cmake --build build -j4 && ctest --test-dir build -V`
Expected: três alvos PASS; `filetab_test` com 7 casos funcionais + `initTestCase`/`cleanupTestCase` = 9 no `Totals`.

- [ ] **Step 5: commit**

```bash
git add src/FileTab.cpp tests/filetab_test.cpp
git commit -m "feat: publica extracao aqui apenas apos sucesso"
```

### Task 3: relatório da branch e verificação final

**Files:**
- Modify: `docs/superpowers/validation/2026-11-26-umbriel.md`
- Modify: `.superpowers/sdd/2026-11-26-fm-estabilidade/progress.md`

**Interfaces:**
- Consumes: resultados das Tasks 1 e 2; comandos de verificação já usados na branch.

- [ ] **Step 1: registrar resultado**

No relatório, trocar a seção de operações por um item de extração cancelável: temporário, botão Cancelar, publicação só no sucesso, limpeza em falha/cancelamento, casos automatizados com `bsdtar` falso; manter a ressalva de que a validação manual no desktop não foi concluída. Atualizar contagens de `filetab_test`. No ledger, acrescentar o que cada task entregou.

- [ ] **Step 2: verificar e commitar**

Run: `cmake --build build -j4 && ctest --test-dir build -V && git status --short`
Expected: build sem warnings, 3 alvos PASS, working tree limpo após o commit.

```bash
git add docs/superpowers/validation/2026-11-26-umbriel.md .superpowers/sdd/2026-11-26-fm-estabilidade/progress.md
git commit -m "docs: registra cancelamento da extracao na validacao da branch"
git push origin feat/estabilidade
```
