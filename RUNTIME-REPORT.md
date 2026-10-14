# RUNTIME-REPORT

**Estado:** `RUNTIME_INCOMPLETE`
**Conclusão:** este checkout não contém um runtime capaz de carregar e executar programas Windows.

## O que está integrado

- Um backend pequeno de diagnóstico para algumas primitivas POSIX/Darwin: páginas de memória, mudanças de proteção, consulta de página, threads/TLS, sinais, filesystem, sockets/IPC e detecção de algumas capacidades.
- Um executor de microteste que, quando o host permite, aloca uma pequena arena, escreve um payload trivial da ISA nativa, sincroniza o cache e verifica o valor de retorno. No teste local, isso ocorreu em Linux x86_64 e não demonstra JIT no iOS.
- Um carregador de formato de teste interno `RTM1`. Ele valida campos desse formato e pode chamar um payload mínimo; **não** reconhece `MZ`, PE32/PE32+, imports ou DLLs.
- Uma tela SwiftUI diagnóstica para disparar suites e exportar logs. Ela não é o desktop Winlator e não inicia uma aplicação Windows.

## Componentes Windows — não integrados

| Componente | Estado | Evidência/limite |
|---|---|---|
| Wine portado para Darwin/iOS | `NOT_TESTED` / ausente | Não há fontes/binários Wine integrados ao target iOS nem inicialização Win32. |
| Box64/Box86 na aplicação | `NOT_TESTED` / ausente | `ios/box64-registers/` contém experimentos isolados de mapeamento de registradores; não existe build do Box64 ligado ao app. |
| Tradução de instruções x86_64 → ARM64 | `NOT_TESTED` / ausente | Nenhum decoder, interpretador ou dynarec x86/x64 é chamado pelo produto. |
| Execução interpretada de PE | `NOT_TESTED` / ausente | Não existe caminho PE → CPU guest → APIs Windows. |
| Parser de PE / carregador de DLL | `NOT_TESTED` / ausente | `runtime_loader` aceita `RTM1`; não é parser PE. |
| Kernel32/Ntdll/User32/Gdi32 | `NOT_TESTED` / ausente | Nenhuma camada Win32 integrada. |
| Containers Windows | `NOT_TESTED` / ausente | O app diagnóstico não cria prefixos Wine ou `drive_c`. |
| Desktop/janelas Windows | `NOT_TESTED` / ausente | Nenhuma janela foi produzida por Wine nem apresentada pela UI. |
| Metal / OpenGL / Direct3D / WineD3D / DXVK | `NOT_TESTED` / ausente | Sem pipeline gráfico de Wine. |
| Áudio Windows | `NOT_TESTED` / ausente | Sem bridge Wine/Core Audio. |
| Touch/teclado/controle para Win32 | `NOT_TESTED` / ausente | Sem tradução de eventos para APIs Windows. |
| JIT no iOS | `NOT_TESTED` para produto/assinatura atual | A PoC mede capacidades; compilar código não concede entitlement. Nenhum resultado físico desta execução. |

## Interpretação dos testes locais

O resultado host de 62 records (58 PASS) verifica apenas o harness de diagnóstico em Linux x86_64. Os microtestes não exercitam instruções de um executável PE e não podem ser usados como prova de Wine, Box64, WoW64, API Win32, áudio, vídeo ou entrada no iPhone.

A árvore importada do port já rotula a PoC como `PHASE_02_RECONSTRUCTED_POC` e declara `IPHONE_VALIDATION_PENDING`. Os relatórios de runs físicos antigos permanecem como documentação histórica do repositório de origem; este relatório não os revalida nem os transforma em evidência de execução Windows.
