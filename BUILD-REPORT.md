# BUILD-REPORT — estado deste checkout

**Data da execução local:** 2026-10-14 UTC
**Status do runtime:** `RUNTIME_INCOMPLETE`
**Validação física:** `DEVICE_VALIDATION_PENDING` — nenhum iPhone foi conectado/testado nesta sessão.
**Build/IPA CI confirmado:** `PASS` — [run 38517367865](https://github.com/nwmwkskw905-collab/winlator/actions/runs/38517367865) compilou `iphoneos` ARM64 e validou o IPA unsigned. Artefato `Winlator-iOS-Release-38517367865`; SHA-256 `6e067b4394d4eb6a2be27771e9f2162e4cf000b7d7f10dec5b41600f2d26b248` ([download ZIP](https://api.github.com/repos/nwmwkskw905-collab/winlator/actions/artifacts/11872615647/zip)).
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
| Build `iphoneos` ARM64 / `.app` / IPA | `PASS` | Run `38517367865`; build e IPA unsigned validados no runner macOS. Ver histórico em `ios/Documentation/evidence/ci_ios_build_attempts_2026-10-14.txt`. |
| Assinatura | `UNSIGNED` validado | O PR não forneceu secrets de assinatura; o validador confirmou ausência de assinatura e perfil. Nenhuma assinatura de distribuição foi testada. |
| Instalação/execução no iPhone 13 | `NOT_TESTED` | Nenhum dispositivo conectado a esta sessão. |

O relatório de teste host está preservado em `ios/Documentation/evidence/host_phase02_report.txt` e `ios/Documentation/evidence/arena_host_run_2026-10-14.txt`. O teste `loader.run_valid_module` exercita o formato experimental `RTM1` e um payload mínimo da ISA do host. **Não** é carregamento PE, Wine, Box64 ou execução x86/x64 de Windows.

## Workflow GitHub Actions

Foi criado `.github/workflows/winlator-ios-release.yml`, manual via `workflow_dispatch` e automático em PRs para `main` que alterem o próprio workflow ou arquivos de código/configuração em `ios/**`; `ios/Documentation/**` e relatórios não disparam build. Os runs `38505249187`/`38505466371` falharam devido ao header iOS-incompatível `<mach/mach_vm.h>`/`mach_vm_region`; a chamada foi substituída por `vm_region_64`. Os runs `38506076451`/`38506481869` compilaram e empacotaram, mas falharam na validação do IPA. Após corrigir a ordem de `lipo`, o run `38516877338` passou; a repetição [38517367865](https://github.com/nwmwkskw905-collab/winlator/actions/runs/38517367865), no commit `1305c69`, também passou nos jobs Linux e macOS e é o resultado mais recente. O validador passou os checks de integridade ZIP, `Payload`/bundle, plataforma `iphoneos`, arquitetura arm64 (`lipo -verify_arch` e `-archs`), assinatura unsigned e dependências (`otool -L`). O artifact mais recente é [Winlator-iOS-Release-38517367865](https://api.github.com/repos/nwmwkskw905-collab/winlator/actions/artifacts/11872615647/zip), contém `Winlator-iOS-unsigned.ipa`; SHA-256 `6e067b4394d4eb6a2be27771e9f2162e4cf000b7d7f10dec5b41600f2d26b248`. PRs ignoram secrets de assinatura; apenas o disparo manual pode usar secrets completos para assinatura development/ad-hoc.

### Histórico do workflow da fonte importada (não é build deste checkout)

A execução mais recente consultada no repositório de origem, commit `a37546267e3974bc260140c567c8d0fc46b95421`, foi [run 37234225481](https://github.com/nwmwkskw905-collab/port-winlator-ios/actions/runs/37234225481) e terminou `FAIL`: a etapa `iphoneos` e o teste do harness macOS falharam. Uma execução anterior, [run 37082888708](https://github.com/nwmwkskw905-collab/port-winlator-ios/actions/runs/37082888708), terminou `success`. Esses runs pertencem a outro repositório/revisão; seus artifacts não foram usados como entrega deste checkout.

## Campos de release

| Campo | Valor comprovado neste checkout (host Linux e runs GitHub Actions citados) |
|---|---|
| Xcode / SDK | `NOT_TESTED` — indisponíveis localmente |
| Arquitetura do host | `x86_64 Linux` |
| Arquitetura `iphoneos` | `PASS` — ARM64 confirmado no IPA com `xcrun lipo <executável> -verify_arch arm64` e listado com `xcrun lipo -archs <executável>`. |
| Compilação iOS / criação do IPA | `PASS` — run `38517367865`, IPA unsigned publicado após validação completa. |
| Dependências runtime incorporadas | Nenhum Wine/Box64/runtime Windows integrado; o `.app` transitório é somente a PoC diagnóstica. |
| Assinatura / entitlements incorporados | `UNSIGNED` — PR sem secrets; validação confirmou ausência de assinatura e perfil de provisionamento. Não é instalável sem re-assinatura legítima. |
| SHA-256 do IPA | `6e067b4394d4eb6a2be27771e9f2162e4cf000b7d7f10dec5b41600f2d26b248` — IPA unsigned do run `38517367865`. |
| Artifact/link de IPA desta revisão | [Winlator-iOS-Release-38517367865](https://api.github.com/repos/nwmwkskw905-collab/winlator/actions/artifacts/11872615647/zip) — contém `Winlator-iOS-unsigned.ipa` e relatórios. |

**Resultado atual do pipeline Apple:** o run `38517367865` concluiu com sucesso e o artefato unsigned passou pelas validações listadas. Isso comprova apenas build/empacotamento estrutural do app iOS; não comprova instalação, execução em iPhone físico ou compatibilidade Windows. O produto continua `RUNTIME_INCOMPLETE` até um runtime Windows real ser implementado e validado.
