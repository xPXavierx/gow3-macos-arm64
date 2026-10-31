# Shaders comunitários: baixar, preparar e contribuir

[English](community-shaders.md) · [Español](community-shaders.es.md)

**Disponível na [release 0.4.20](https://github.com/xPXavierx/gow3-macos-arm64/releases/tag/v0.4.20).**
Downloads antigos podem não ter o botão **Shaders comunitários**. O catálogo
oficial e o validador de contribuições do GitHub estão disponíveis.

**Jogar → descobrir combinações → contribuir → revisar → todos recebem a atualização.**

O catálogo compartilhado contém descrições para reconstruir shaders e pipelines
gráficos. O Mac de cada jogador usa seu próprio dump compatível para compilar o
resultado nativo da GPU. Você não baixa o cache compilado de outro jogador.

## Baixar e preparar — sem conta GitHub

1. Abra o launcher, selecione seu dump compatível e extraído e clique em **Jogar**.
2. Antes do primeiro Jogar da sessão do launcher, ele verifica atualizações oficiais,
   no máximo uma vez por dia. Baixa e verifica o catálogo assinado quando disponível.
3. Antes da partida, o Mac prepara as combinações compatíveis e reconstruíveis do
   catálogo usando seu dump. Aguarde a preparação terminar.
4. O resultado fica no cache local. Resultados compatíveis já preparados são
   reaproveitados; atualizar não significa recompilar tudo do zero.

Para conferir manualmente, use **Shaders comunitários → Atualizar catálogo oficial**.
**Isso apenas baixa e verifica. Clique em Jogar depois para fazer a preparação.**

Não é preciso conta GitHub para baixar. Se o serviço estiver indisponível, o jogo
pode usar as descrições incluídas no aplicativo, os dados locais válidos e o cache
existente. Mantenha o dump acessível: o catálogo não substitui os arquivos do jogo.

## Contribuir — com conta GitHub

1. Jogue normalmente. O jogo registra novas combinações automaticamente.
2. Saia do jogo e volte ao launcher. Ele indica descobertas novas que ainda não
   estão no catálogo incluído no app ou no catálogo oficial baixado.
3. Abra **Shaders comunitários → Contribuir pelo GitHub** e confirme **Continuar no GitHub**.
4. O Finder seleciona um arquivo `GOW3-<digest>.txt`. O navegador abre o formulário oficial.
5. Entre na sua conta GitHub, arraste **esse arquivo selecionado** para o campo de
   anexo e clique em **Create** para enviar a issue. Informar os trechos jogados é opcional.

**Abrir o navegador não envia nada.** É necessário anexar o arquivo e confirmar o
formulário. Não precisa Terminal, procurar pastas de cache nem selecionar shaders.
Se não houver descobertas novas, continue jogando outras áreas; não precisa enviar
nada. O aplicativo não sabe se você concluiu o envio no navegador.

O arquivo inclui apenas descrições: sem dump, saves, logs ou programas compilados
da GPU. Não anexe esses arquivos por conta própria. **Exportar descobertas**, em
**Avançado**, serve para diagnóstico manual; não envia nada nem substitui o fluxo de contribuição.

## O que acontece depois do envio?

O validador do GitHub verifica o anexo como dados limitados. Passar nessa verificação
significa **pronto para revisão**, não aceito, compilado ou publicado. Os mantenedores
revisam e testam as novas combinações, removem duplicatas e publicam o catálogo
assinado. Os jogadores recebem essa atualização aprovada pelo launcher. Nenhum
anexo de uma issue é instalado automaticamente no Mac de outros jogadores.

O catálogo reúne descobertas aprovadas de qualquer área da campanha. Ainda não
cobre todo o jogo nem garante ausência de stutter. Entradas inválidas ou incompletas
não contam como preparadas; combinações inéditas ainda podem precisar de compilação.
Os desenhos necessários são aguardados, sem serem omitidos.

**Limpar cache de shaders** descarta os gráficos compilados localmente. Preserva
saves, configurações, preparação ARM64 e dados do catálogo oficial baixado; ao jogar,
o cache gráfico é reconstruído. Não é necessário limpar para receber atualizações.

Validação, assinaturas e publicação pelos mantenedores estão no
[guia técnico do serviço](community-catalogue-service.md).
