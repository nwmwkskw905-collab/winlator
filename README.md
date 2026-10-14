# Winlator iOS — estado real do port

Este repositório agora contém a árvore iOS de diagnóstico existente no port, importada de `nwmwkskw905-collab/port-winlator-ios` (`main`, commit `a37546267e3974bc260140c567c8d0fc46b95421`). **Isso não é um port funcional do Winlator para iOS.** O estado correto é `RUNTIME_INCOMPLETE`.

## O que existe e foi verificado nesta execução

- A PoC iOS `PHASE_02_RECONSTRUCTED_POC`: um harness C de capacidades Darwin/POSIX, uma ponte Objective-C/SwiftUI e testes para memória, threads, sinais, filesystem, IPC e um loader experimental.
- No host Linux x86_64 desta execução: CMake compilou sem warnings/erros; CTest passou **4/4**; o programa de testes executou **298 checks, 0 falhas**; a suíte reportou 62 records (`58 PASS`, `0 FAIL`, `0 BLOCKED`, `2 UNSUPPORTED`, `0 UNTESTED`, `2 NOT_APPLICABLE`). São resultados apenas de host Linux.
- Um pré-validador estrutural do projeto Xcode passou **62 checks**; o target está configurado para iOS 17.0 e o produto configurado é `WinlatorIOS.app` (`Winlator iOS Probe` na tela inicial). Isso não substitui `xcodebuild`.
- Um workflow manual (`workflow_dispatch`) e de validação de código para PRs foi adicionado para repetir os testes e, num runner macOS, compilar/validar um IPA de `iphoneos` ARM64. Pull requests não recebem secrets de assinatura. O artifact produzido é, no máximo, a PoC diagnóstica descrita abaixo.

## O que não existe

Não há Wine para iOS/Darwin integrado, carregador PE32/PE32+ funcional, Box64/Box86 integrado à aplicação, tradução x86/x86_64 para ARM64 ligada ao Wine, APIs Win32, desktop Windows, renderização de janelas de um processo Wine, áudio Windows ou ponte de entrada Windows. O loader atual entende somente o formato experimental `RTM1`, não PE; o microteste JIT executa um payload minúsculo da ISA do host, não código de um `.exe` Windows. O Box64 experimental em `ios/box64-registers/` não é compilado nem chamado pelo app.

Por isso, não importe executáveis esperando executá-los. O IPA já produzido por este workflow é uma **PoC de diagnóstico sem runtime Windows**, não uma versão funcional do Winlator e não evidência de compatibilidade com jogos. Os status atuais são `RUNTIME_INCOMPLETE` e `DEVICE_VALIDATION_PENDING`.

## Construção e evidências

As quatro primeiras tentativas Apple falharam, mas a correção foi validada em [run 38516877338](https://github.com/nwmwkskw905-collab/winlator/actions/runs/38516877338), que compilou e validou o IPA unsigned. Artifact: [Winlator-iOS-Release-38516877338 (ZIP)](https://api.github.com/repos/nwmwkskw905-collab/winlator/actions/artifacts/11871794795/zip), com `Winlator-iOS-unsigned.ipa`; SHA-256 `66c0b3bcf686fa374bd247eaac94af4063618e2bf25ad14eb5dfecf4e94482cd`. A validação do artefato não é teste em iPhone; `DEVICE_VALIDATION_PENDING` e `RUNTIME_INCOMPLETE` permanecem.

- [BUILD-REPORT.md](BUILD-REPORT.md) — testes locais, resultado do build/IPA Apple e hash do artifact validado.
- [RUNTIME-REPORT.md](RUNTIME-REPORT.md) — componentes presentes/ausentes.
- [DEVICE-TEST-REPORT.md](DEVICE-TEST-REPORT.md) — separação entre host, runner Apple e iPhone físico.
- [COMPATIBILITY-MATRIX.md](COMPATIBILITY-MATRIX.md) — estados por funcionalidade.
- [INSTALL-WINDOWS.md](INSTALL-WINDOWS.md) — procedimento Sideloadly para o IPA unsigned publicado; deixa explícito o que ele não faz.
- [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) — licenças e fontes.
- [SOURCE-REVISION.txt](SOURCE-REVISION.txt) — revisões examinadas/importadas.
- `.github/workflows/winlator-ios-release.yml` — execução manual (`workflow_dispatch`) de testes host e build `iphoneos`.

O sandbox desta sessão é Linux e não possui Xcode, `xcrun`, `codesign`, um iPhone ou credenciais Apple. O CI comprovou a compilação e validação estrutural do IPA unsigned; não houve assinatura de distribuição, instalação física nem execução de Windows. Esses últimos itens exigem evidências reais de assinatura/dispositivo/runtime.
