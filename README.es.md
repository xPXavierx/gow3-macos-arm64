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

## Funciones implementadas

- CPU compilada previamente a ARM64; sin Rosetta, JIT ni intérprete durante el
  juego. Las instrucciones no soportadas producen un diagnóstico específico.
- Launcher en inglés, portugués brasileño y español. Inglés en el primer inicio;
  conserva el dump seleccionado, idioma y preferencias.
- Salida 720p, 1080p, 1440p y 4K. Renderizado nativo o MetalFX espacial en Calidad,
  Equilibrado y Rendimiento. El HUD indica resolución de render y MetalFX.
- Límite de 60 FPS o Ilimitado, V-Sync, pantalla completa Retina y elegibilidad
  para Game Mode. Con límite de 60 FPS y sin frame gen, selecciona temporalmente
  60 Hz y restaura la configuración anterior al salir. Mantiene resolución y
  escala Retina. Ilimitado y frame gen no solicitan 60 Hz.
- Entrada SDL3, audio/vídeo FFmpeg y ATRAC9, copia de seguridad de partidas y las
  reglas originales de lectura de texturas. Gráficos Vulkan/shadPS4 y MoltenVK.

Pruebas limitadas de combate alcanzaron aproximadamente 60 FPS en 4K nativo en
M3 Max; no es una garantía para toda la campaña. MetalFX frame gen sigue siendo
experimental y está desactivado en el launcher público. Se miden por separado los
FPS reales y presentados. El experimento de texturas de mayor resolución terminó;
no se distribuye un paquete. UltraWide y los tutoriales, launchers y paquetes de
Windows/Linux no forman parte de este port.

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
