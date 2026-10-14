# BUILD-REPORT — estado deste checkout

**Data da execução local:** 2026-10-14 UTC
**Status do runtime:** `RUNTIME_INCOMPLETE`
**Validação física:** `DEVICE_VALIDATION_PENDING` — nenhum iPhone foi conectado/testado nesta sessão.
**Build/IPA CI atual:** `BUILD_FAILED` — quatro runs falharam no pipeline macOS; dois empacotaram IPA transitório, mas nenhum foi validado nem publicado.
**IPA nesta execução local:** `NOT_TESTED` — nenhum IPA foi produzido neste host Linux.

## Revisão e ferramentas

- Repositório-alvo: `nwmwkskw905-collab/winlator`, branch fixa `arena/f755ab5e-winlator`.
- Base do checkout: `25d5faf827d064b2bd145f43293c231c02519f0a` (`Initial commit`).
- Código iOS importado de `nwmwkskw905-collab/port-winlator-ios`, `main` em `a37546267e3974bc260140c567c8d0fc46b95421`.
- Host observado: Linux x86_64; GCC 12.2.0; CMake 4.4.4 instalado temporariamente para a validação local.
- Xcode: `NOT_TESTED` — `xcodebuild` não está instalado neste host.
- SDK `iphoneos`: `NOT_TESTED` — `xcrun`/SDK Apple não estão instalados neste host.
- Arquitetura validada localmente: x86_64 Linux; **não** é um binário iOS.

## Testes executados aqui

| Verificação | Resultado | Escopo da evidência |
|---|---|---|
| `sh ios/tools/build_host_harness.sh` — build CMake | `PASS` | Compilação do harness em Linux x86_64; 0 warnings e 0 erros reportados pelo script. |
| CTest | `PASS` | 4/4 testes concluídos em Linux x86_64. |
| `phase02_unit_tests` | `PASS` | 298 checks, 0 falhas, em Linux x86_64. |
| `phase02_poc --suite all` | `PASS` | 62 records: 58 PASS, 0 FAIL, 0 BLOCKED, 2 UNSUPPORTED, 0 UNTESTED, 2 NOT_APPLICABLE; somente Linux x86_64. |
| Gerador + validador do `.xcodeproj` | `PASS` | 62 verificações estruturais; não executa o parser Xcode nem compila o app. |
| Identidade configurada | `PASS` | Produto `WinlatorIOS.app`, deployment target iOS 17.0, título `Winlator iOS Probe`; configuração, não evidência de IPA. |
| Auditorias de APIs/headers Apple, interfaces, composição, capacidade/JIT/SHM, Fix 06 e estabilização | `PASS` | Auditorias estáticas, sem SDK Apple. |
| Auditoria de configuração de entitlements | `PASS` | 7 regras estáticas; nenhum certificado/perfil/entitlement foi concedido ou testado. |
| Sintaxe do workflow e relatórios dinâmicos | `PASS` | Parse YAML, `bash -n`, compilação dos blocos Python e teste de atualização em cópias temporárias; não é execução do workflow. |
| Build `iphoneos` ARM64 / `.app` / IPA | `BUILD_FAILED` | Runs `38505249187`/`38505466371` falharam na compilação; `38506076451`/`38506481869` compilaram/empacotaram, mas falharam na validação. Evidência em `ios/Documentation/evidence/ci_ios_build_attempts_2026-10-14.txt`. |
| Assinatura | `NOT_TESTED` | Nenhum certificado/perfil foi fornecido nesta execução. |
| Instalação/execução no iPhone 13 | `NOT_TESTED` | Nenhum dispositivo conectado a esta sessão. |

O relatório de teste host está preservado em `ios/Documentation/evidence/host_phase02_report.txt` e `ios/Documentation/evidence/arena_host_run_2026-10-14.txt`. O teste `loader.run_valid_module` exercita o formato experimental `RTM1` e um payload mínimo da ISA do host. **Não** é carregamento PE, Wine, Box64 ou execução x86/x64 de Windows.

## Workflow GitHub Actions

Foi criado `.github/workflows/winlator-ios-release.yml`, manual via `workflow_dispatch` e automático em PRs para `main` que alterem a árvore iOS/relatórios. Os jobs Linux passaram. Nos runs `38505249187`/`38505466371`, o compile falhou pelo header iOS-incompatível `<mach/mach_vm.h>`/`mach_vm_region`; a chamada foi substituída por `vm_region_64`. Nos runs `38506076451`/`38506481869`, o build e o empacotamento unsigned passaram, mas a validação do IPA falhou. O último erro foi a ordem incorreta dos argumentos de `lipo`; ela foi corrigida e aguarda novo run. Nenhum release IPA ou SHA-256 foi publicado. O workflow captura o log completo da validação e publica as linhas de falha como anotações. Pull requests ignoram secrets de assinatura; apenas o disparo manual pode usar o conjunto completo de secrets para assinatura development/ad-hoc.

### Histórico do workflow da fonte importada (não é build deste checkout)

A execução mais recente consultada no repositório de origem, commit `a37546267e3974bc260140c567c8d0fc46b95421`, foi [run 37234225481](https://github.com/nwmwkskw905-collab/port-winlator-ios/actions/runs/37234225481) e terminou `FAIL`: a etapa `iphoneos` e o teste do harness macOS falharam. Uma execução anterior, [run 37082888708](https://github.com/nwmwkskw905-collab/port-winlator-ios/actions/runs/37082888708), terminou `success`. Esses runs pertencem a outro repositório/revisão; seus artifacts não foram usados como entrega deste checkout.

## Campos de release

| Campo | Valor comprovado neste checkout (host Linux e runs GitHub Actions citados) |
|---|---|
| Xcode / SDK | `NOT_TESTED` — indisponíveis localmente |
| Arquitetura do host | `x86_64 Linux` |
| Arquitetura `iphoneos` | `NOT_VERIFIED` — alvo compilado com `ARCHS=arm64`, mas a validação falhou antes de disponibilizar um binário de release. |
| Compilação iOS / criação do IPA | `BUILD_FAILED` nos quatro runs macOS observados |
| Dependências runtime incorporadas | Nenhum Wine/Box64/runtime Windows integrado; o `.app` transitório é somente a PoC diagnóstica. |
| Assinatura / entitlements incorporados | `NOT_TESTED` |
| SHA-256 do IPA | `NOT_GENERATED` — a validação falhou antes da etapa que calcula o hash de release. |
| Artifact/link de IPA desta revisão | `NOT_CREATED` — os IPA transitórios dos runs `38506076451`/`38506481869` não passaram validação e não foram publicados; somente evidência de falha foi enviada. |

**`BUILD_FAILED` foi observado nos quatro runs listados acima.** Os runs `38506076451` e `38506481869` geraram IPA unsigned transitório, mas a validação falhou e nenhum foi publicado; não há SHA-256 de release. A ordem do argumento de `lipo` foi corrigida e exige novo run. Mesmo que um IPA validado seja publicado, o produto continua `RUNTIME_INCOMPLETE` até existir e ser testado um runtime Windows real.
