# God of War III Remastered — macOS Apple Silicon

[English](README.md) · [Português brasileiro](README.pt-BR.md)

Port nativo ARM64 mantenido por [xPXavierx](https://github.com/xPXavierx), basado en
FelipePrado0/gow3_pc, bbport y shadPS4. Este repositorio contiene la aplicación para
macOS y el código necesario para compilarla.

## Descargar y jugar

1. Descarga la aplicación ARM64 desde [Releases](https://github.com/xPXavierx/gow3-macos-arm64/releases).
2. Ábrela y selecciona tu propio dump **extraído** del juego.
3. Pulsa Jugar. La aplicación compila automáticamente el perfil ARM64 la primera
   vez y reutiliza el resultado. Conserva el dump en la ubicación seleccionada.

Requisitos: **Apple Silicon, macOS 26 o posterior y CUSA01623 v1.02**. Los hashes
del ejecutable y los módulos determinan la compatibilidad; los metadatos probados
muestran APP_VER 01.00. Otras regiones, actualizaciones y ejecutables modificados
no están validados. Probado en M3 Max; otros Macs y la campaña completa siguen
pendientes. La preparación inicial requiere al menos 3 GB libres.

La aplicación incluye compilador offline, Python y bibliotecas nativas. No exige
Xcode, Homebrew, Terminal, Rosetta ni internet durante la preparación. La firma es
ad-hoc, sin notarización de Apple. Consulta la [guía de instalación y
compatibilidad](packaging/macos/launcher/COMMUNITY.md).

## Shaders comunitarios

El launcher actualizado tiene el panel **Shaders comunitarios**, con dos opciones:

- **Descargar y preparar:** pulsa Jugar para la comprobación automática diaria o
  **Actualizar catálogo oficial** para comprobarlo ahora. No exige cuenta de GitHub.
  Actualizar solo descarga/verifica; los shaders y pipelines compatibles se preparan
  desde tu propio dump al iniciar el juego y se guardan en la caché de este Mac.
- **Compartir descubrimientos:** juega, vuelve al launcher y elige **Contribuir en
  GitHub**. Finder selecciona el archivo de texto generado; inicia sesión, arrástralo
  al formulario oficial y confirma. Abrir el navegador no realiza el envío.

Las contribuciones se validan y revisan antes de entrar en el catálogo oficial firmado.
No se comparten dumps, partidas, registros ni la caché compilada de GPU de otro jugador.
La cobertura crece por toda la campaña; todavía no garantiza ausencia de stutter.

**Disponible en la release 0.4.20.** Descarga la aplicación actualizada en Releases.
El servicio del catálogo oficial ya está disponible.
La [guía para jugadores](docs/community-shaders.es.md) explica todos los pasos y
la diferencia entre actualizar, compilar y borrar la caché.

## Funciones implementadas

- CPU compilada previamente a ARM64; sin Rosetta, JIT ni intérprete durante el
  juego. Las instrucciones no soportadas producen un diagnóstico específico.
- Launcher en inglés, portugués brasileño y español. Inglés en el primer inicio;
  conserva el dump seleccionado, idioma y preferencias.
- Salida 720p, 1080p, 1440p y 4K. Renderizado nativo o MetalFX espacial en Calidad,
  Equilibrado y Rendimiento. Al activarlo, el HUD indica resolución de render y MetalFX.
  Desmarcar la opción del launcher oculta ambos HUD de diagnóstico.
- Los pipelines gráficos conocidos se preparan antes de jugar. Se espera a los
  shaders nuevos sin omitir dibujos; el primer uso aún puede provocar pausas.
- Botón para borrar la caché de shaders, conservando partidas, ajustes y la
  preparación ARM64. La limpieza se bloquea durante una sesión del juego.
- Límite de 60 FPS o Ilimitado, V-Sync, pantalla completa Retina y elegibilidad
  para Game Mode. Con límite de 60 FPS, selecciona temporalmente
  60 Hz y restaura la configuración anterior al salir. Mantiene resolución y
  escala Retina. Ilimitado no solicita 60 Hz.
- Entrada SDL3, audio/vídeo FFmpeg y ATRAC9, copia de seguridad de partidas y las
  reglas originales de lectura de texturas. Gráficos Vulkan/shadPS4 y MoltenVK.

La preparación ARM64 de la CPU y la de gráficos son etapas diferentes. El backend
gráfico convierte shaders de PS4 a SPIR-V y MoltenVK/Metal durante la ejecución.
Los pipelines conocidos se preparan antes de jugar; todavía no existe un catálogo
Metal completo preparado offline, y las variantes nuevas pueden causar pausas.

Pruebas limitadas de combate alcanzaron aproximadamente 60 FPS en 4K nativo en
M3 Max; no es una garantía para toda la campaña. Frame gen está excluido de la build y del launcher. El escalado MetalFX sigue disponible. El experimento de texturas de mayor resolución terminó;
no se distribuye un paquete. UltraWide y los tutoriales, launchers y paquetes de
Windows/Linux no forman parte de este port.

## Actualizaciones del upstream

Este fork incorpora correcciones seleccionadas de SpartanPort, conservando el runtime ARM64 y el launcher macOS. La versión 0.4.21 incluye las correcciones de memoria, mutex, alias de texturas, readbacks, entrada de shaders, registros y publicación de fences validadas localmente. Son backports adaptados: el contador “commits behind” de GitHub mide la ascendencia y no disminuye solo porque se hayan portado correcciones equivalentes.

Consulte el [estado de integración y trabajo pendiente](docs/upstream-status.md), incluidos los dos nuevos commits revisados el 1 de noviembre de 2026.

## Desarrollo y errores

El [README en inglés](README.md#build-and-contribute) enlaza los pasos para compilar
CPU, runtime, renderer, MoltenVK y la aplicación. `./build.sh --help` muestra los
objetivos macOS. Se conserva el núcleo compartido necesario y sus créditos.

Abre errores [en este repositorio](https://github.com/xPXavierx/gow3-macos-arm64/issues).
Incluye Mac/chip, macOS, versión de la app, resolución, MetalFX, límite de FPS y
pasos para reproducir. **Abrir logs** en el launcher muestra los diagnósticos.
Revisa las rutas locales antes de compartirlos. No publiques dumps, partidas,
texturas, código generado del juego ni capturas de memoria.

[Créditos](docs/CREDITS.md) · [GPL-2.0-o-posterior](LICENSE). Las dependencias
conservan sus licencias. Sin afiliación con Sony Interactive Entertainment ni
Santa Monica Studio. No distribuye archivos ni código preparado del juego.
