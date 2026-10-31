# Shaders comunitarios: descargar, preparar y contribuir

[English](community-shaders.md) · [Português brasileiro](community-shaders.pt-BR.md)

**Disponible en la [release 0.4.20](https://github.com/xPXavierx/gow3-macos-arm64/releases/tag/v0.4.20).**
Las descargas antiguas pueden no tener el botón **Shaders comunitarios**. El catálogo
oficial y el validador de contribuciones de GitHub están disponibles.

**Jugar → descubrir combinaciones → contribuir → revisar → todos reciben la actualización.**

El catálogo compartido contiene descripciones para reconstruir shaders y pipelines
gráficos. El Mac de cada jugador utiliza su propio dump compatible para compilar el
resultado nativo de GPU. No se descarga la caché compilada de otro jugador.

## Descargar y preparar — sin cuenta de GitHub

1. Abre el launcher, selecciona tu dump compatible y extraído y pulsa **Jugar**.
2. Antes del primer Jugar de una sesión del launcher, busca actualizaciones oficiales,
   como máximo una vez al día. Descarga y verifica el catálogo firmado si está disponible.
3. Antes de jugar, el Mac prepara las combinaciones compatibles y reconstruibles del
   catálogo usando tu dump. Espera a que termine la preparación.
4. Los resultados se guardan en la caché local. Se reutilizan los resultados compatibles;
   actualizar no significa compilar todo desde cero.

Para comprobarlo manualmente, usa **Shaders comunitarios → Actualizar catálogo oficial**.
**Esto solo descarga y verifica. Pulsa Jugar después para realizar la preparación.**

No se necesita cuenta de GitHub para descargar. Si el servicio no está disponible,
el juego puede usar las descripciones incluidas, datos locales válidos y su caché
existente. Mantén el dump accesible: el catálogo no sustituye los archivos del juego.

## Contribuir — con cuenta de GitHub

1. Juega normalmente. El juego registra nuevas combinaciones automáticamente.
2. Sal del juego y vuelve al launcher. Indica descubrimientos nuevos que todavía no
   están en el catálogo incluido en la app o en el catálogo oficial descargado.
3. Abre **Shaders comunitarios → Contribuir en GitHub** y confirma **Continuar en GitHub**.
4. Finder selecciona un archivo `GOW3-<digest>.txt`. El navegador abre el formulario oficial.
5. Inicia sesión en GitHub, arrastra **ese archivo seleccionado** al campo de adjuntos
   y pulsa **Create** para enviar la issue. Indicar las zonas jugadas es opcional.

**Abrir el navegador no envía nada.** Hay que adjuntar el archivo y confirmar el
formulario. No se necesita Terminal, buscar carpetas de caché ni seleccionar shaders.
Si no hay descubrimientos nuevos, sigue jugando otras zonas; no hace falta enviar
nada. La aplicación no sabe si completaste el envío en el navegador.

El archivo contiene solo descripciones: sin dump, partidas, registros ni programas
compilados de GPU. No adjuntes esos archivos por tu cuenta. **Exportar descubrimientos**,
en **Avanzado**, sirve para diagnóstico manual; no envía nada ni sustituye la contribución.

## ¿Qué ocurre después del envío?

El validador de GitHub comprueba el adjunto como datos limitados. Superar esa prueba
significa **listo para revisión**, no aceptado, compilado ni publicado. Los mantenedores
revisan y prueban las nuevas combinaciones, eliminan duplicados y publican el catálogo
firmado. Los jugadores reciben esa actualización aprobada mediante el launcher.
Ningún adjunto de una issue se instala automáticamente en otros Macs.

El catálogo reúne descubrimientos aprobados de cualquier zona de la campaña. Todavía
no cubre todo el juego ni garantiza ausencia de stutter. Las entradas inválidas o
incompletas no cuentan como preparadas; las combinaciones inéditas aún pueden requerir
compilación. Se espera a los dibujos necesarios, sin omitirlos.

**Borrar caché de shaders** descarta los gráficos compilados localmente. Conserva
partidas, ajustes, preparación ARM64 y datos del catálogo oficial descargado; al jugar,
se reconstruye la caché gráfica. No es necesario borrarla para recibir actualizaciones.

Para validación, firmas y publicación por los mantenedores, consulta la
[guía técnica del servicio](community-catalogue-service.md).
