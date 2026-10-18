# God of War III Remastered — macOS Apple Silicon

[English](README.md) · [Español](README.es.md)

Port nativo ARM64 mantido por [xPXavierx](https://github.com/xPXavierx), baseado em
FelipePrado0/gow3_pc, bbport e shadPS4. Este repositório contém o aplicativo para
macOS e o código necessário para compilá-lo.

## Baixar e jogar

1. Baixe o aplicativo ARM64 em [Releases](https://github.com/xPXavierx/gow3-macos-arm64/releases).
2. Abra e selecione seu próprio dump **extraído** do jogo.
3. Clique em Jogar. O aplicativo faz a compilação inicial ARM64 automaticamente e
   reaproveita o resultado nas próximas execuções. Mantenha o dump disponível.

Requisitos: **Apple Silicon, macOS 26 ou superior e CUSA01623 v1.02**. Os hashes do
executável e dos módulos determinam a compatibilidade; o metadado extraído testado
mostra APP_VER 01.00. Outras regiões, updates e executáveis modificados não foram
validados. Testado em M3 Max; outros Macs e a campanha inteira ainda precisam de
testes. Reserve pelo menos 3 GB livres para a preparação inicial.

O aplicativo inclui compilador offline, Python e bibliotecas nativas. Para jogar,
não é preciso Xcode, Homebrew, Terminal, Rosetta nem internet durante a preparação.
A assinatura é ad-hoc, sem notarização Apple. Veja o [guia de instalação e
compatibilidade](packaging/macos/launcher/COMMUNITY.md).

## Recursos implementados

- Código da CPU compilado antecipadamente para ARM64, sem Rosetta, JIT ou
  interpretação durante o jogo. Instruções sem suporte geram um diagnóstico.
- Launcher em inglês, português brasileiro e espanhol. Inglês na primeira abertura;
  pasta do dump, idioma e configurações ficam salvos.
- Saída em 720p, 1080p, 1440p ou 4K. Renderização nativa ou MetalFX espacial em
  Qualidade, Equilibrado e Desempenho. O HUD mostra resolução de render e MetalFX.
- Cap de 60 FPS ou Ilimitado, V-Sync, tela cheia Retina e elegibilidade ao Game Mode.
  Com cap de 60 FPS e sem frame gen, troca temporariamente para 60 Hz e restaura a
  configuração anterior ao sair. Preserva resolução e escala Retina. Ilimitado e
  frame gen não solicitam a mudança para 60 Hz.
- Entrada SDL3, áudio/vídeo FFmpeg e ATRAC9, backup de saves e as regras de leitura
  de texturas do jogo. Gráficos via Vulkan/shadPS4 e MoltenVK.

Testes limitados de combate atingiram aproximadamente 60 FPS em 4K nativo no M3
Max. Isso não garante o mesmo desempenho em toda a campanha. Frame gen MetalFX
continua experimental, desativado no launcher público. FPS reais e apresentados
são medidos separadamente. O experimento de texturas maiores foi encerrado; não
há pacote de texturas distribuído. UltraWide, tutoriais e launchers Windows/Linux
não fazem parte deste port.

## Desenvolvimento e problemas

O [README em inglês](README.md#build-and-contribute) reúne os guias para compilar
CPU, runtime, renderer, MoltenVK e o aplicativo da comunidade. Use
`./build.sh --help` para os alvos macOS. O núcleo compartilhado necessário à
compilação e seus créditos foram preservados.

Abra problemas [neste repositório](https://github.com/xPXavierx/gow3-macos-arm64/issues).
Informe Mac/chip, macOS, versão do app, resolução, MetalFX, limite de FPS e passos
para reproduzir. O botão **Abrir logs** do launcher abre os diagnósticos da sessão.
Revise caminhos locais antes de compartilhar. Não publique dumps, saves, texturas,
código gerado do jogo ou capturas de memória.

[Créditos e origem do código](docs/CREDITS.md) · [GPL-2.0-ou-posterior](LICENSE).
As dependências preservam suas licenças. Projeto sem vínculo com Sony Interactive
Entertainment ou Santa Monica Studio. Não distribui arquivos ou código preparado
do jogo.
