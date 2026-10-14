# Winlator iOS — estado real do port

Este repositório agora contém a árvore iOS de diagnóstico existente no port, importada de `nwmwkskw905-collab/port-winlator-ios` (`main`, commit `a37546267e3974bc260140c567c8d0fc46b95421`). **Isso não é um port funcional do Winlator para iOS.** O estado correto é `RUNTIME_INCOMPLETE`.

## O que existe e foi verificado nesta execução

- A PoC iOS `PHASE_02_RECONSTRUCTED_POC`: um harness C de capacidades Darwin/POSIX, uma ponte Objective-C/SwiftUI e testes para memória, threads, sinais, filesystem, IPC e um loader experimental.
- No host Linux x86_64 desta execução: CMake compilou sem warnings/erros; CTest passou **4/4**; o programa de testes executou **298 checks, 0 falhas**; a suíte reportou 62 records (`58 PASS`, `0 FAIL`, `0 BLOCKED`, `2 UNSUPPORTED`, `0 UNTESTED`, `2 NOT_APPLICABLE`). São resultados apenas de host Linux.
- Um pré-validador estrutural do projeto Xcode passou **62 checks**; o target está configurado para iOS 17.0 e o produto configurado é `WinlatorIOS.app` (`Winlator iOS Probe` na tela inicial). Isso não substitui `xcodebuild`.
- Um workflow manual (`workflow_dispatch`) e de validação para PRs foi adicionado para repetir os testes e, num runner macOS, compilar/validar um IPA de `iphoneos` ARM64. Pull requests não recebem secrets de assinatura. O artifact produzido é, no máximo, a PoC diagnóstica descrita abaixo.

## O que não existe

Não há Wine para iOS/Darwin integrado, carregador PE32/PE32+ funcional, Box64/Box86 integrado à aplicação, tradução x86/x86_64 para ARM64 ligada ao Wine, APIs Win32, desktop Windows, renderização de janelas de um processo Wine, áudio Windows ou ponte de entrada Windows. O loader atual entende somente o formato experimental `RTM1`, não PE; o microteste JIT executa um payload minúsculo da ISA do host, não código de um `.exe` Windows. O Box64 experimental em `ios/box64-registers/` não é compilado nem chamado pelo app.

Por isso, não importe executáveis esperando executá-los. Um eventual IPA deste workflow é uma **PoC de diagnóstico sem runtime Windows**, não uma versão funcional do Winlator e não evidência de compatibilidade com jogos. Os status atuais são `RUNTIME_INCOMPLETE` e `DEVICE_VALIDATION_PENDING`.

## Construção e evidências

As três tentativas Apple falharam no pipeline. Após corrigir o uso do header iOS-incompatível `mach/mach_vm.h` para `vm_region_64`, o run `38506076451` compilou e empacotou um IPA unsigned transitório, mas falhou na validação antes de publicar o artifact; não há IPA de release ou SHA-256. O próximo run inclui captura de diagnóstico completa dessa validação.

- [BUILD-REPORT.md](BUILD-REPORT.md) — testes executados aqui e estado de compilação Apple.
- [RUNTIME-REPORT.md](RUNTIME-REPORT.md) — componentes presentes/ausentes.
- [DEVICE-TEST-REPORT.md](DEVICE-TEST-REPORT.md) — separação entre host, runner Apple e iPhone físico.
- [COMPATIBILITY-MATRIX.md](COMPATIBILITY-MATRIX.md) — estados por funcionalidade.
- [INSTALL-WINDOWS.md](INSTALL-WINDOWS.md) — procedimento Sideloadly, caso um artifact IPA exista; deixa explícito o que ele não faz.
- [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) — licenças e fontes.
- [SOURCE-REVISION.txt](SOURCE-REVISION.txt) — revisões examinadas/importadas.
- `.github/workflows/winlator-ios-release.yml` — execução manual (`workflow_dispatch`) de testes host e build `iphoneos`.

O sandbox desta sessão é Linux e não possui Xcode, `xcrun`, `codesign`, um iPhone ou credenciais Apple. Não afirmo IPA compilado, assinatura, instalação física nem execução de Windows sem uma execução de CI/dispositivo que gere essa evidência.
