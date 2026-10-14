# COMPATIBILITY-MATRIX

Estados abaixo descrevem apenas o que há evidência neste checkout; a suíte host não é uma matriz de compatibilidade Windows.

| Recurso | Estado | Limite comprovado |
|---|---|---|
| Harness C em Linux x86_64 | `PASS` | 4/4 CTest; 298 checks; 62 records. |
| Harness C em Darwin/macOS | `NOT_TESTED` nesta sessão | O workflow do port original falhou no teste macOS em um run recente. |
| Build iOS para `iphoneos` ARM64 | `BUILD_FAILED` | Runs `38505249187`/`38505466371` falharam no compile; `38506076451`/`38506481869` compilaram/empacotaram, mas falharam na validação. Evidência em `ios/Documentation/evidence/ci_ios_build_attempts_2026-10-14.txt`. |
| IPA validado | `BUILD_FAILED` / `NOT_PUBLISHED` | Os runs `38506076451`/`38506481869` empacotaram IPA unsigned transitório, mas o validador falhou; nenhum release foi publicado. |
| iPhone 13 físico | `DEVICE_VALIDATION_PENDING` | Nenhum dispositivo conectado; requer validação manual em dispositivo real. |
| Containers e prefixos Wine | `NOT_TESTED` | Não implementados. |
| PE32 (x86) | `NOT_TESTED` | Não há parser ou execução PE. |
| PE32+ (x86_64) | `NOT_TESTED` | Não há parser ou execução PE. |
| Wine / APIs Win32 | `NOT_TESTED` | Não integrados. |
| Box64 x86_64 | `NOT_TESTED` como parte deste app | Somente experimento isolado de registradores, sem motor ligado. |
| Box86 x86 | `NOT_TESTED` | Não integrado. |
| JIT/interpretador de código Windows | `NOT_TESTED` | O microteste local usa apenas payload do host; não há interpretador x86. |
| Metal/OpenGL/Direct3D/DXVK | `NOT_TESTED` | Sem janela ou renderer Wine. |
| Áudio Windows / Core Audio bridge | `NOT_TESTED` | Não integrado. |
| Entrada Win32/touch/gamepad | `NOT_TESTED` | Não integrada. |

**Classificação geral:** `RUNTIME_INCOMPLETE`. Não existe compatibilidade de jogos/aplicações Windows validada.
