# DEVICE-TEST-REPORT

**Dispositivo-alvo solicitado:** iPhone 13 / Apple A15 / iOS 17 ou posterior.
**Dispositivo conectado durante esta execução:** nenhum.
**Status da validação física:** `DEVICE_VALIDATION_PENDING` (`NOT_TESTED` nesta sessão).

## Evidência desta execução

- `PASS` — CMake/harness, CTest e 298 checks em host Linux x86_64.
- `PASS` — 62 verificações estáticas da estrutura Xcode.
- `PASS` — [run CI 38516877338](https://github.com/nwmwkskw905-collab/winlator/actions/runs/38516877338) compilou e validou um IPA unsigned para iPhoneOS ARM64; ZIP, estrutura `Payload`, plataforma, arquitetura, estado unsigned e dependências passaram. Artifact: [Winlator-iOS-Release-38516877338](https://api.github.com/repos/nwmwkskw905-collab/winlator/actions/artifacts/11871794795/zip). Isso não é teste de instalação/execução em dispositivo.
- `NOT_TESTED` — instalação ou execução em simulador.
- `NOT_TESTED` — instalação ou execução em iPhone físico nesta execução.
- `NOT_TESTED` — importar/inspecionar um `.exe`, executar PE x64, mostrar uma janela Windows, aceitar entrada ou produzir áudio Windows.

## Registros herdados do repositório de origem

A fonte importada possui `ios/Documentation/IPHONE13_PHYSICAL_RUN_01.md`, `_02.md` e `_03.md`. Os dois primeiros são documentos históricos apresentados pelo port como transcrições de resultados de um iPhone 13 para **testes de capacidades do Runtime PoC**, não de aplicações Windows. O documento `_03` registra um encerramento relatado pelo proprietário, sem relatório de teste capturado. Esses documentos foram preservados sem alteração e **não foram reproduzidos nem verificados independentemente nesta sessão**.

Também foi consultado o GitHub Actions do port original. No commit `a37546267e3974bc260140c567c8d0fc46b95421`, o run `37234225481` terminou `FAIL`: a etapa de build `iphoneos` e a etapa de testes do backend macOS falharam. Um run anterior (`37082888708`) terminou `success`, mas não é evidência para este checkout nem para runtime Windows. Os logs completos desses runs não puderam ser obtidos neste ambiente.

## Matriz de aceitação funcional

| Teste | Resultado nesta execução | Observação |
|---|---|---|
| A — inicialização do app iOS | `NOT_TESTED` | Sem build/execução iOS aqui. |
| B — CRUD/persistência de container | `NOT_TESTED` | Não há gerenciador de containers Wine integrado. |
| C — importar `.exe` | `NOT_TESTED` | Fluxo não implementado. |
| D — parser PE | `NOT_TESTED` | Nenhum parser PE no caminho do produto. |
| E — executar PE x64 | `NOT_TESTED` | Nenhum Wine/Box64 integrado. |
| F — APIs Windows | `NOT_TESTED` | Nenhuma API Win32/Ntdll. |
| G — janela Windows renderizada | `NOT_TESTED` | Nenhum processo Wine produziu uma janela. |
| H — toque/teclado/mouse em programa Windows | `NOT_TESTED` | Nenhuma cadeia de entrada para Win32. |
| I — áudio de programa Windows | `NOT_TESTED` | Nenhuma bridge de áudio Wine. |
| J — encerrar/reabrir sessão Windows | `NOT_TESTED` | Não há sessão Windows para abrir. |
| K — memória/estabilidade no iPhone | `NOT_TESTED` | Somente checks POSIX executados em Linux x86_64. |
| L — IPA arm64 `iphoneos` | `PASS` | Run `38516877338` gerou e validou o IPA unsigned; teste de execução em iPhone permanece separado e pendente. |
| M — instalação e uso em iPhone 13 | `NOT_TESTED` | Não existe dispositivo físico ligado ao agente. |

**Status final de runtime:** `RUNTIME_INCOMPLETE`.
**Status final do dispositivo:** `DEVICE_VALIDATION_PENDING` — o iPhone físico não foi testado. Isso não é o único bloqueio: o runtime Windows também não existe neste checkout.
