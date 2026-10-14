# BUILD-REPORT — estado deste checkout

**Data da execução local:** 2026-10-14 UTC
**Status do runtime:** `RUNTIME_INCOMPLETE`
**Validação física:** `DEVICE_VALIDATION_PENDING` — nenhum iPhone foi conectado/testado nesta sessão.
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
| Build `iphoneos` ARM64 / `.app` / IPA | `NOT_TESTED` | Sem Xcode local; o workflow recém-adicionado ainda precisa de execução. |
| Assinatura | `NOT_TESTED` | Nenhum certificado/perfil foi fornecido nesta execução. |
| Instalação/execução no iPhone 13 | `NOT_TESTED` | Nenhum dispositivo conectado a esta sessão. |

O relatório de teste host está preservado em `ios/Documentation/evidence/host_phase02_report.txt` e `ios/Documentation/evidence/arena_host_run_2026-10-14.txt`. O teste `loader.run_valid_module` exercita o formato experimental `RTM1` e um payload mínimo da ISA do host. **Não** é carregamento PE, Wine, Box64 ou execução x86/x64 de Windows.

## Workflow GitHub Actions

Foi criado `.github/workflows/winlator-ios-release.yml`, manual via `workflow_dispatch` e automático em PRs para `main` que alterem a árvore iOS/relatórios. O job Linux compila e testa o harness; o job `macos-26` tenta construir/validar um IPA `iphoneos` arm64. Pull requests sempre ignoram secrets de assinatura; apenas o disparo manual pode usar o conjunto completo de secrets para assinatura development/ad-hoc. Sem secrets no disparo manual, a rota gera um IPA rotulado explicitamente como unsigned. A execução deste workflow, o nome/estado do artifact e o SHA-256 **ainda não estão comprovados** por este relatório.

### Histórico do workflow da fonte importada (não é build deste checkout)

A execução mais recente consultada no repositório de origem, commit `a37546267e3974bc260140c567c8d0fc46b95421`, foi [run 37234225481](https://github.com/nwmwkskw905-collab/port-winlator-ios/actions/runs/37234225481) e terminou `FAIL`: a etapa `iphoneos` e o teste do harness macOS falharam. Uma execução anterior, [run 37082888708](https://github.com/nwmwkskw905-collab/port-winlator-ios/actions/runs/37082888708), terminou `success`. Esses runs pertencem a outro repositório/revisão; seus artifacts não foram usados como entrega deste checkout.

## Campos de release

| Campo | Valor comprovado nesta execução local |
|---|---|
| Xcode / SDK | `NOT_TESTED` — indisponíveis localmente |
| Arquitetura do host | `x86_64 Linux` |
| Arquitetura `iphoneos` | `NOT_TESTED` |
| Compilação iOS / criação do IPA | `NOT_TESTED` |
| Dependências runtime incorporadas | Nenhum Wine/Box64/runtime Windows integrado; o target iOS não foi compilado aqui. |
| Assinatura / entitlements incorporados | `NOT_TESTED` |
| SHA-256 do IPA | `NOT_TESTED` — não há IPA local. |
| Artifact/link de IPA desta revisão | `NOT_TESTED` — execute o workflow manual para produzir/validar o artifact. |

**`BUILD_FAILED` não foi observado localmente; build Apple/IPA permanece `NOT_TESTED`.** Mesmo que o workflow gere um IPA, o produto continua `RUNTIME_INCOMPLETE` até existir e ser testado um runtime Windows real.
