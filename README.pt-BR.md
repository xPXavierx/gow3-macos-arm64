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

## Shaders comunitários

O launcher atualizado tem o painel **Shaders comunitários**, com dois caminhos:

- **Receber e preparar:** clique em Jogar para a verificação automática diária ou
  em **Atualizar catálogo oficial** para verificar agora. Não exige conta GitHub.
  Atualizar apenas baixa/verifica; shaders e pipelines compatíveis são preparados
  do seu próprio dump ao iniciar o jogo e ficam no cache deste Mac.
- **Compartilhar descobertas:** jogue, volte ao launcher e escolha **Contribuir pelo
  GitHub**. O Finder seleciona o arquivo de texto gerado; entre na conta, arraste-o
  para o formulário oficial e confirme. Apenas abrir o navegador não faz o envio.

Contribuições são validadas e revisadas antes de entrar no catálogo oficial assinado.
Não se compartilham dumps, saves, logs ou o cache compilado da GPU de outro jogador.
A cobertura cresce por toda a campanha; ainda não garante ausência de stutter.

**Disponível desde a release 0.4.20.** Baixe o aplicativo atualizado em Releases.
O serviço do catálogo oficial já está disponível.
O [guia para jogadores](docs/community-shaders.pt-BR.md) explica todos os passos e
a diferença entre atualizar, compilar e limpar o cache.

## Recursos implementados

- Código da CPU compilado antecipadamente para ARM64, sem Rosetta, JIT ou
  interpretação durante o jogo. Instruções sem suporte geram um diagnóstico.
- Launcher em inglês, português brasileiro e espanhol. Inglês na primeira abertura;
  pasta do dump, idioma e configurações ficam salvos.
- Saída em 720p, 1080p, 1440p ou 4K. Renderização nativa ou MetalFX espacial em
  Qualidade, Equilibrado e Desempenho. Quando ativado, o HUD mostra resolução de render e MetalFX.
  Desmarcar a opção no launcher oculta os dois HUDs de diagnóstico.
- Pipelines gráficos conhecidos são preparados antes de jogar. Shaders novos
  são aguardados, sem omitir desenhos; o primeiro uso ainda pode causar pausas.
- Botão para limpar o cache de shaders, preservando saves, configurações e a
  preparação ARM64. A limpeza fica bloqueada durante uma sessão do jogo.
- Cap de 60 FPS ou Ilimitado, V-Sync, tela cheia Retina e elegibilidade ao Game Mode.
  Com cap de 60 FPS, troca temporariamente para 60 Hz e restaura a
  configuração anterior ao sair. Preserva resolução e escala Retina. Ilimitado não solicita a mudança para 60 Hz.
- Entrada SDL3, áudio/vídeo FFmpeg e ATRAC9, backup de saves e as regras de leitura
  de texturas do jogo. Gráficos via Vulkan/shadPS4 e MoltenVK.

A preparação ARM64 da CPU e a dos gráficos são etapas diferentes. O backend
gráfico converte shaders do PS4 para SPIR-V e MoltenVK/Metal durante a execução.
Os pipelines conhecidos são aquecidos antes de jogar; ainda não existe um catálogo
Metal completo preparado offline, e compilações inéditas podem causar pausas.

Testes limitados de combate atingiram aproximadamente 60 FPS em 4K nativo no M3
Max. Isso não garante o mesmo desempenho em toda a campanha. O frame gen foi retirado da build e do launcher. O upscaling MetalFX continua disponível. O experimento de texturas maiores foi encerrado; não
há pacote de texturas distribuído. UltraWide, tutoriais e launchers Windows/Linux
não fazem parte deste port.

## Atualizações do upstream

Este fork incorpora correções selecionadas do SpartanPort, mantendo o runtime ARM64 e o launcher macOS. A versão 0.4.21 inclui as correções de memória, mutex, aliases de texturas, readbacks, entrada de shaders, registros e publicação de fences validadas localmente. São backports adaptados: o contador “commits behind” do GitHub mede a ancestralidade e não diminui apenas porque correções equivalentes foram portadas.

Veja o [estado da integração e as pendências](docs/upstream-status.md), incluindo os dois commits mais novos revisados em 1 de novembro de 2026.

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
