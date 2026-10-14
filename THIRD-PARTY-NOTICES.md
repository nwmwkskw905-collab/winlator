# Third-party notices

Este documento lista dependências de código presentes nesta revisão e fontes examinadas. Não implica que Wine, Box64 ou qualquer runtime Windows esteja compilado no aplicativo.

## Código presente

### Port iOS / harness `PHASE_02_RECONSTRUCTED_POC`

- Fonte: [nwmwkskw905-collab/port-winlator-ios](https://github.com/nwmwkskw905-collab/port-winlator-ios), `main` em `a37546267e3974bc260140c567c8d0fc46b95421`; branch de reconstrução examinada: `ios-phase-02-reconstructed-poc` em `0fefb9cdfccd55d710cfc515fd400c024ff34158` (consulte `SOURCE-REVISION.txt` para o SHA completo correto).
- Licença declarada no repositório de origem: GNU LGPL v2.1; cópia mantida em `LICENSE`.
- Atribuições/copyright e cabeçalhos dos arquivos iOS foram preservados ao importar `ios/`.

### Box64 — experimento de mapeamento de registradores

- Fonte upstream referenciada pelo experimento: [ptitSeb/box64](https://github.com/ptitSeb/box64), commit `2f130fab1d6e1a4ee8a71dc60cfdfcc839ad192a` (registrado no relatório da Fase 04 da árvore importada).
- Licença upstream: MIT; deve ser distribuída com o aviso de copyright e permissão do Box64 se o código correspondente for redistribuído.
- `ios/box64-registers/` guarda patch, headers/trechos, harness e relatórios de um experimento isolado. Não é a biblioteca Box64 completa, não faz parte do target iOS e não é chamado pelo app. Os avisos originais presentes nesses arquivos foram preservados.

## Referências analisadas, não integradas/redistribuídas

- Winlator Android original: [brunodev85/winlator](https://github.com/brunodev85/winlator), branch `main`; é um aplicativo Android que integra Wine/Box64 e recursos Mesa/DXVK específicos de Android/Linux. Nenhum APK, runtime Android, driver Turnip/Adreno ou asset compactado foi copiado para este target iOS.
- Aplicação Android atualizada: [brunodev85/winlator-app](https://github.com/brunodev85/winlator-app), branch `main`; não foi integrada.
- Vortek (`brunodev85/vortek`, main @ `b1730c5def9b575672e671aee11d79ae7adc63d1`, LGPL-2.1) e Gladio (`brunodev85/gladio`, main @ `116c0d14dedbea3bd057f98f1db138bb1efe225e`, LGPL-2.1) estão listados como submódulos Android no port original. São wrappers Vulkan/GLES para drivers host Android/Linux; não são usados nem copiados para o target iOS/Metal.
- Box64 iOS static library: [cloverfield11/box64-ios-library](https://github.com/cloverfield11/box64-ios-library), branch `main`; não foi integrada.
- BoxiOS: [cloverfield11/BoxiOS](https://github.com/cloverfield11/BoxiOS), branch `main`; não foi integrado. O README descreve execução de binários Linux x86/x86_64 por Box86/Box64 e um bridge gráfico experimental, não um runtime Wine/PE para Windows.
- Wine, DXVK, VKD3D, Mesa, MoltenVK, SDL, ALSA e PulseAudio: não estão incluídos no produto iOS deste checkout. Nenhuma licença é atribuída a binários inexistentes.

## Frameworks Apple

O app usa SwiftUI, Foundation, UIKit e APIs públicas fornecidas pelo SDK Apple no build. Os termos da Apple/Xcode aplicam-se ao SDK e à redistribuição de componentes Apple; os binários do SDK não são copiados para este repositório.

Antes de incluir ou redistribuir qualquer novo runtime, biblioteca estática, driver, firmware ou asset, atualize este arquivo com versão, commit, licença e obrigações de fonte/notices correspondentes.

### Box64 upstream MIT license text

The following notice is included because the isolated Box64 ARM64 emitter/mapping experiment in `ios/box64-registers/` contains upstream Box64-derived files or excerpts:

```
MIT License

Copyright (c) 2020 ptitSeb

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
