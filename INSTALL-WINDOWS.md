# Instalação pelo Windows (Sideloadly)

> **Aviso obrigatório:** um artifact IPA deste repositório é atualmente uma PoC diagnóstica (`Winlator iOS Probe`), não um Winlator funcional. Ela **não executa `.exe` Windows**. Instalar o IPA publicado não valida Wine, Box64, JIT ou compatibilidade de jogos.

## Baixar o artifact

1. O artifact unsigned atualmente validado está na [execução 38517367865](https://github.com/nwmwkskw905-collab/winlator/actions/runs/38517367865), como [Winlator-iOS-Release-38517367865 (ZIP)](https://api.github.com/repos/nwmwkskw905-collab/winlator/actions/artifacts/11872615647/zip). Para execuções futuras, abra o workflow [`Winlator iOS Build`](https://github.com/nwmwkskw905-collab/winlator/actions/workflows/winlator-ios-release.yml).
2. Confirme que a execução terminou com `success` e que o pacote inclui `Winlator-iOS-unsigned.ipa` ou `Winlator-iOS-signed.ipa`.
3. Baixe o artifact `Winlator-iOS-Release-…` da página da execução. O download do artifact é um ZIP do GitHub Actions; extraia esse ZIP no Windows para obter o `.ipa` e os relatórios.
4. Leia `BUILD-REPORT.md` e `SHA256SUMS.txt` dentro do artifact. Não instale um pacote cujo hash não corresponda ao relatório.

Um `.ipa` **unsigned** não é diretamente instalável. O arquivo não assinado precisa ser re-assinado por Sideloadly ou ferramenta legítima compatível. O nome `Winlator-iOS-unsigned.ipa` é intencional.

## Instalar um IPA unsigned com Sideloadly

1. Instale o Sideloadly a partir do site oficial do projeto e instale/atualize o Apple Devices ou iTunes oficial no Windows, se solicitado.
2. Conecte o iPhone por USB, desbloqueie-o e toque em **Confiar neste computador** se iOS perguntar.
3. Abra Sideloadly, selecione o dispositivo e arraste `Winlator-iOS-unsigned.ipa` para a área de IPA.
4. Inicie o sideload usando uma Apple ID autorizada dentro do Sideloadly. Nunca compartilhe senha, código de dois fatores ou certificado neste repositório/chat; credenciais ficam somente no cliente de assinatura.
5. Aguarde a conclusão e confira o status mostrado pela ferramenta. Se a assinatura expirar (contas gratuitas normalmente precisam reinstalar/renovar com frequência), assine novamente conforme as regras atuais da Apple.
6. No iPhone, se o iOS bloquear o primeiro lançamento, vá a **Ajustes → Geral → VPN e Gerenciamento de Dispositivo** (o nome pode variar), selecione o perfil do desenvolvedor e confirme a confiança.
7. Se iOS solicitar, habilite **Modo Desenvolvedor** em **Ajustes → Privacidade e Segurança → Modo Desenvolvedor** e reinicie o telefone conforme as instruções do sistema. Essa opção não concede JIT por si só.

## IPA assinado pelo workflow

O workflow usa a rota assinada somente se estiverem configurados, como GitHub Actions secrets, um certificado `.p12` e sua senha, um perfil de provisionamento compatível com o bundle ID `io.winlator.phase02.reconstructed`, e o Team ID. O perfil precisa incluir o UDID do iPhone para instalação de desenvolvimento/ad-hoc. O workflow valida esses dados e falha se o conjunto estiver presente, porém inválido. Sem o conjunto completo, ele cria explicitamente um IPA unsigned.

Secrets esperados: `IOS_CERTIFICATE_P12_BASE64`, `IOS_CERTIFICATE_PASSWORD`, `IOS_PROVISIONING_PROFILE_BASE64`, `IOS_TEAM_ID`; opcionalmente `IOS_SIGNING_IDENTITY` e `IOS_EXPORT_METHOD` (`development` ou `ad-hoc`). Não coloque os arquivos de assinatura no Git.

## Diagnóstico de instalação

- **“Unable to install” / perfil inválido:** verifique se o perfil está ativo, corresponde ao App ID e inclui o UDID; re-assine com perfil/certificado autorizados.
- **App abre e fecha imediatamente:** capture logs do dispositivo e consulte `DEVICE-TEST-REPORT.md`; não suponha que um erro de certificado seja um erro do runtime.
- **App instala e abre:** isso valida somente instalação/inicialização da PoC. O runtime Windows continua `RUNTIME_INCOMPLETE`.
- **JIT:** esta PoC não executa instruções x86/x64 Windows. Não há requisito de habilitar JIT para seu uso atual. Modo Desenvolvedor, sideload e `MAP_JIT`/entitlements são coisas distintas; não adicione entitlements privados nem presuma que o JIT foi concedido.

O status atual da validação em iPhone 13 é `DEVICE_VALIDATION_PENDING`. Só poderá ser alterado após instalar e executar em um dispositivo físico e guardar logs reais. Nenhuma instrução de instalação substitui esse teste.
