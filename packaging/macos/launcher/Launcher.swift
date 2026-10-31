// SPDX-License-Identifier: GPL-2.0-or-later
import AppKit
import Foundation
import SwiftUI
import Darwin
import CoreText
import UniformTypeIdentifiers

// Register the bundled title face for this process only. The dump's localized
// menu resources name their atlas TXR_Copperplate_en (SMF2Copperplate_en).
// Use macOS's Copperplate outlines for live, accented, accessible menu text.
enum GameStyle {
    static let ivory = Color(red:0.89,green:0.85,blue:0.76)
    static let gold = Color(red:0.76,green:0.60,blue:0.34)
    static let muted = Color(red:0.57,green:0.54,blue:0.48)
    static let red = Color(red:0.67,green:0.09,blue:0.065)
    static var titleName = "GodOfWar"
    static let menuName = NSFont(name:"Copperplate-Bold",size:16) != nil ? "Copperplate-Bold" : "Georgia-Bold"
    static func menu(_ size:CGFloat) -> Font { .custom(menuName,size:size) }
    static func registerFonts(_ resources:URL) {
        let url = resources.appendingPathComponent("Branding/GODOFWAR.TTF")
        guard FileManager.default.fileExists(atPath:url.path) else { return }
        CTFontManagerRegisterFontsForURL(url as CFURL,.process,nil)
        if let descriptors = CTFontManagerCreateFontDescriptorsFromURL(url as CFURL) as? [CTFontDescriptor],
           let descriptor = descriptors.first {
            titleName = CTFontCopyPostScriptName(CTFontCreateWithFontDescriptor(descriptor,48,nil)) as String
        }
    }
}

struct Preferences: Codable {
    var language = "en"
    var resolution = "2160"
    var upscaling = "native"
    var textures = "original"
    var cap = "60"
    var hud = true
    var shortcuts = true
    var gamePath: String? = nil
    var bookmark: Data? = nil
    init() {}
    enum CodingKeys: String, CodingKey { case language, resolution, upscaling, textures, cap, hud, shortcuts, gamePath, bookmark }
    init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy:CodingKeys.self)
        language = try values.decodeIfPresent(String.self,forKey:.language) ?? "en"
        resolution = try values.decodeIfPresent(String.self,forKey:.resolution) ?? "2160"
        upscaling = try values.decodeIfPresent(String.self,forKey:.upscaling) ?? "native"
        textures = try values.decodeIfPresent(String.self,forKey:.textures) ?? "original"
        cap = try values.decodeIfPresent(String.self,forKey:.cap) ?? "60"
        // Old interpolation choices forced Unlimited. Retire them into the safe 60 FPS mode.
        enum LegacyKeys: String, CodingKey { case framegen }
        let legacy = try decoder.container(keyedBy:LegacyKeys.self)
        if let mode = try legacy.decodeIfPresent(String.self,forKey:.framegen), ["1","2","3"].contains(mode) { cap = "60" }
        hud = try values.decodeIfPresent(Bool.self,forKey:.hud) ?? true
        shortcuts = try values.decodeIfPresent(Bool.self,forKey:.shortcuts) ?? true
        gamePath = try values.decodeIfPresent(String.self,forKey:.gamePath)
        bookmark = try values.decodeIfPresent(Data.self,forKey:.bookmark)
    }
}

let translations: [String: [String: String]] = [
"pt-BR": [
 "shaderGuideTitle":"Shaders comunitários", "shaderGuideIntro":"Receba o catálogo revisado e ajude a ampliá-lo jogando.",
 "shaderReceiveTitle":"Receber e preparar", "shaderReceive1":"Clique em Jogar. O launcher verifica atualizações uma vez por dia, sem exigir conta GitHub.",
 "shaderReceive2":"Antes da partida, este Mac prepara as combinações compatíveis usando seu próprio dump. O cache é reaproveitado nas próximas execuções.",
 "shaderReceive3":"Atualizar agora apenas baixa e verifica o catálogo. A compilação acontece na próxima abertura do jogo.",
 "shaderShareTitle":"Compartilhar descobertas", "shaderShare1":"Jogue normalmente. O jogo registra novas combinações de shaders e pipelines automaticamente.",
 "shaderShare2":"Clique em Contribuir pelo GitHub. O Finder seleciona o arquivo preparado e o navegador abre o formulário oficial.",
 "shaderShare3":"Entre no GitHub, arraste o arquivo selecionado para o formulário e confirme o envio. Nada é enviado automaticamente.",
 "shaderGuideTrust":"Recebemos apenas catálogos revisados e assinados. As contribuições não incluem dump, saves ou logs e passam por revisão antes da publicação.",
 "shaderGuideCoverage":"O catálogo reúne descobertas aprovadas de qualquer área da campanha. Ainda não cobre tudo nem elimina todas as pausas de compilação.",
 "shaderGuideLink":"Guia completo no GitHub", "shaderAdvanced":"Avançado", "shaderDiscoveries":"Novas descobertas para compartilhar:",
 "updateOfficial":"Atualizar catálogo oficial…","contributeGithub":"Contribuir pelo GitHub…","checkingOfficial":"Verificando catálogo oficial…","officialUpdated":"Catálogo oficial verificado. Será preparado na próxima abertura do jogo.","contributionReady":"Sua contribuição está pronta","contributionInstructions":"O arquivo de texto contém descrições gráficas; não inclui dump, saves ou logs. O Finder mostrará o arquivo selecionado. No formulário oficial do GitHub, entre na sua conta, arraste esse arquivo para o campo indicado e confirme o envio. A contribuição será revisada antes de entrar no catálogo oficial.","continueGithub":"Continuar no GitHub","noDiscoveries":"Nenhuma descoberta nova para compartilhar. Continue jogando outras áreas.","newDiscoveries":"Novas descobertas disponíveis no Catálogo comunitário","catalogueFailed":"Não foi possível atualizar o catálogo. A versão local foi preservada.",
 "textures":"Texturas", "originalTextures":"Originais", "hdTextures":"2× · Kratos e abertura",
 "quitMenu":"Sair de God of War III", "subtitle":"REMASTERED · APPLE SILICON", "settings":"Prepare sua próxima partida",
 "resolution":"Resolução de saída", "upscaling":"Upscaling", "framerate":"Limite de quadros",
 "native":"Nativo", "quality":"MetalFX · Qualidade", "balanced":"MetalFX · Equilibrado", "performance":"MetalFX · Desempenho",
 "unlimited":"Ilimitado", "language":"Idioma do launcher", "hud":"Mostrar HUDs de desempenho",
 "folder":"Pasta do jogo", "choose":"Escolher pasta…", "change":"Alterar…", "missing":"Selecione seu dump para começar.",
 "found":"Pasta selecionada · versão conferida ao jogar", "lost":"A pasta não está disponível. Conecte o disco ou selecione-a novamente.",
 "play":"Jogar", "stop":"Encerrar jogo", "starting":"Preparando o jogo…", "running":"Jogo em execução", "finished":"Tudo pronto para a próxima partida.",
 "fixed":"V-Sync ligado · Retina · Game Mode em tela cheia", "limitNote":"Ilimitado remove o limite de 60. V-Sync e o teto de segurança do motor (240 FPS) continuam valendo.",
 "upscaleNote":"MetalFX espacial: sem geração de quadros e sem histórico temporal.",
 "selectTitle":"Onde está seu God of War III?", "selectMessage":"Escolha a pasta do dump extraído que contém eboot.bin, sce_module e sce_sys. Ela será lembrada nas próximas aberturas.",
 "select":"Usar esta pasta", "invalidTitle":"Pasta do jogo não encontrada", "invalid":"Escolha o dump extraído. Um arquivo .PKG ou .ISO ainda precisa ser extraído.",
 "failed":"O jogo não iniciou. Consulte os logs.", "errorTitle":"Não foi possível iniciar o jogo", "error":"Confira se o disco está conectado e se o dump corresponde à versão preparada. O diagnóstico foi salvo nos logs.",
 "logs":"Abrir logs", "ok":"OK", "quitTitle":"Encerrar o jogo também?", "quitMessage":"O jogo ainda está aberto. Saia pelo menu do jogo para salvar seu progresso antes de fechar o launcher.",
 "shortcuts":"Atalhos: 0 atalhos · 1 desempenho · 2 MetalFX · 3 resolução · 4 limite de 60 FPS",
 "communityShaders":"Catálogo comunitário", "importShaders":"Importar catálogo…", "exportShaders":"Exportar descobertas…", "catalogueBusy":"Processando catálogo…", "catalogueReady":"Catálogo pronto", "catalogueNote":"As descrições serão compiladas neste Mac na próxima abertura. Nenhum arquivo é enviado automaticamente.",
 "clearShaders":"Limpar cache de shaders", "clearingShaders":"Limpando cache de shaders…",
 "clearShadersHelp":"Limpa apenas o cache gráfico. Ele será reconstruído ao jogar; saves, configurações e a recompilação ARM64 são preservados.",
 "shadersCleared":"Cache de shaders limpo. Será reconstruído ao jogar.", "shadersEmpty":"O cache de shaders já está vazio.",
 "shadersBusy":"Feche o jogo e aguarde o fim da sessão antes de limpar o cache.", "shadersFailed":"Não foi possível limpar o cache de shaders.",
 "keep":"Voltar", "quit":"Encerrar", "local":"Comunidade · ARM64 nativo", "input":"Renderização", "output":"saída",
 "version":"Testado: CUSA01623 v1.02 · O executável e os módulos são verificados antes da preparação.",
 "graphics_index":"Preparando o catálogo gráfico · primeira abertura",
 "checking":"Conferindo a versão do dump…",
 "scanning":"Preparando os arquivos do seu dump…",
 "generating":"Recompilando para Apple Silicon…",
 "compiling":"Compilando o jogo · primeira preparação",
 "linking":"Finalizando e verificando o jogo…",
 "ready":"Preparação concluída. Iniciando…",
 "cancelled":"Preparação interrompida. Clique em Jogar para continuar.",
 "cancel":"Cancelar",
 "firstRun":"A primeira preparação pode levar vários minutos. O resultado será salvo neste Mac.",
 "dump_version":"Dump incompatível. Esta versão foi testada com CUSA01623 v1.02 e confere o conteúdo do executável e dos módulos. Outros updates, regiões e executáveis modificados não são aceitos.",
 "dump_missing":"O dump está incompleto. Selecione a pasta extraída com os executáveis e os arquivos de dados do jogo.",
 "disk_space":"Libere pelo menos 3 GB no disco do Mac para preparar o jogo.",
 "prepare_failed":"Não foi possível concluir a preparação. Abra os logs para ver o diagnóstico e clique em Jogar para tentar novamente.",
 "runtime_aot":"O jogo foi encerrado por uma instrução ainda não suportada pelo recompilador ARM64. O diagnóstico está nos logs. Isso não indica corrupção do dump.", "runtime_failed":"O jogo foi encerrado inesperadamente. Abra os logs para consultar o erro. Este aviso, por si só, não indica problema no dump.",
 "architecture":"É necessário um Mac com Apple Silicon e macOS 26 ou superior. Rosetta não é usado.",
 "quitPreparingTitle":"Cancelar a preparação?",
 "quitPreparingMessage":"O trabalho já concluído será guardado. Na próxima abertura, clique em Jogar para continuar."
],
"en": [
 "shaderGuideTitle":"Community shaders", "shaderGuideIntro":"Get the reviewed catalogue and help expand it by playing.",
 "shaderReceiveTitle":"Download and prepare", "shaderReceive1":"Click Play. The launcher checks for updates once a day. No GitHub account is needed.",
 "shaderReceive2":"Before gameplay, this Mac prepares compatible combinations using your own dump. The cache is reused on later launches.",
 "shaderReceive3":"Update now only downloads and verifies the catalogue. Compilation happens the next time you start the game.",
 "shaderShareTitle":"Share discoveries", "shaderShare1":"Play normally. The game automatically records new shader and pipeline combinations.",
 "shaderShare2":"Click Contribute on GitHub. Finder selects the prepared file and your browser opens the official form.",
 "shaderShare3":"Sign in to GitHub, drag the selected file into the form and submit it. Nothing is uploaded automatically.",
 "shaderGuideTrust":"Only reviewed, signed catalogues are downloaded. Contributions contain no dump, saves or logs and are reviewed before publication.",
 "shaderGuideCoverage":"The catalogue combines approved discoveries from any campaign area. It does not cover everything or eliminate every compilation pause yet.",
 "shaderGuideLink":"Full guide on GitHub", "shaderAdvanced":"Advanced", "shaderDiscoveries":"New discoveries to share:",
 "updateOfficial":"Update official catalogue…","contributeGithub":"Contribute on GitHub…","checkingOfficial":"Checking official catalogue…","officialUpdated":"Official catalogue verified. It will be prepared next time the game starts.","contributionReady":"Your contribution is ready","contributionInstructions":"The text file contains graphics descriptions, without the dump, saves or logs. Finder will show the selected file. Sign in to GitHub, drag that file into the indicated field in the official form and confirm submission. Contributions are reviewed before entering the official catalogue.","continueGithub":"Continue on GitHub","noDiscoveries":"No new discoveries to share yet. Keep playing other areas.","newDiscoveries":"New discoveries available in Community catalogue","catalogueFailed":"Could not update the catalogue. The local version was preserved.",
 "textures":"Textures", "originalTextures":"Original", "hdTextures":"2× · Kratos & opening",
 "quitMenu":"Quit God of War III", "subtitle":"REMASTERED · APPLE SILICON", "settings":"Ready for your next session",
 "resolution":"Output resolution", "upscaling":"Upscaling", "framerate":"Frame rate cap",
 "native":"Native", "quality":"MetalFX · Quality", "balanced":"MetalFX · Balanced", "performance":"MetalFX · Performance",
 "unlimited":"Unlimited", "language":"Launcher language", "hud":"Show performance HUDs",
 "folder":"Game folder", "choose":"Choose folder…", "change":"Change…", "missing":"Select your game dump to get started.",
 "found":"Folder selected · version checked on Play", "lost":"The folder is unavailable. Connect the drive or select it again.",
 "play":"Play", "stop":"Stop game", "starting":"Preparing the game…", "running":"Game running", "finished":"Ready for your next session.",
 "fixed":"V-Sync on · Retina · Game Mode in fullscreen", "limitNote":"Unlimited removes the 60 FPS cap. V-Sync and the engine's safety ceiling (240 FPS) still apply.",
 "upscaleNote":"Spatial MetalFX: no frame generation or temporal history.",
 "selectTitle":"Where is your God of War III?", "selectMessage":"Choose the extracted dump folder containing eboot.bin, sce_module and sce_sys. It will be remembered next time.",
 "select":"Use this folder", "invalidTitle":"Game folder not found", "invalid":"Choose an extracted dump. A .PKG or .ISO file must be extracted first.",
 "failed":"The game could not start. Check the logs.", "errorTitle":"The game could not start", "error":"Check that the drive is connected and the dump matches the prepared version. Diagnostics were saved in the logs.",
 "logs":"Open logs", "ok":"OK", "quitTitle":"Stop the game too?", "quitMessage":"The game is still open. Exit through the game's menu to save your progress before closing the launcher.",
 "shortcuts":"Keys: 0 shortcuts · 1 performance · 2 MetalFX · 3 resolution · 4 cap at 60 FPS",
 "communityShaders":"Community catalogue", "importShaders":"Import catalogue…", "exportShaders":"Export discoveries…", "catalogueBusy":"Processing catalogue…", "catalogueReady":"Catalogue ready", "catalogueNote":"Descriptions will be compiled on this Mac at next launch. No files are uploaded automatically.",
 "clearShaders":"Clear shader cache", "clearingShaders":"Clearing shader cache…",
 "clearShadersHelp":"Clears only the graphics cache. It will be rebuilt when you play; saves, settings and ARM64 preparation are preserved.",
 "shadersCleared":"Shader cache cleared. It will be rebuilt when you play.", "shadersEmpty":"The shader cache is already empty.",
 "shadersBusy":"Close the game and wait for the session to finish before clearing the cache.", "shadersFailed":"The shader cache could not be cleared.",
 "keep":"Go back", "quit":"Stop", "local":"Community · Native ARM64", "input":"Rendering", "output":"output",
 "version":"Tested: CUSA01623 v1.02 · Executable and modules are verified before preparation.",
 "graphics_index":"Preparing graphics catalogue · first launch",
 "checking":"Checking your dump version…",
 "scanning":"Preparing files from your dump…",
 "generating":"Recompiling for Apple Silicon…",
 "compiling":"Compiling the game · first preparation",
 "linking":"Finalizing and verifying the game…",
 "ready":"Preparation complete. Starting…",
 "cancelled":"Preparation stopped. Click Play to continue.",
 "cancel":"Cancel",
 "firstRun":"First preparation may take several minutes. The result will be saved on this Mac.",
 "dump_version":"Incompatible dump. This release was tested with CUSA01623 v1.02 and verifies executable and module contents. Other updates, regions and modified executables are not accepted.",
 "dump_missing":"The dump is incomplete. Select the extracted folder containing the executables and game data files.",
 "disk_space":"Free at least 3 GB on your Mac to prepare the game.",
 "prepare_failed":"Preparation could not finish. Open the logs for diagnostics, then click Play to try again.",
 "runtime_aot":"The game stopped at an instruction not yet supported by the ARM64 recompiler. Diagnostics are in the logs. This does not indicate a corrupt dump.", "runtime_failed":"The game stopped unexpectedly. Open the logs for the error details. This message alone does not indicate a dump problem.",
 "architecture":"An Apple Silicon Mac with macOS 26 or later is required. Rosetta is not used.",
 "quitPreparingTitle":"Cancel preparation?",
 "quitPreparingMessage":"Completed work will be kept. Click Play next time to continue."
],
"es": [
 "shaderGuideTitle":"Shaders comunitarios", "shaderGuideIntro":"Recibe el catálogo revisado y ayuda a ampliarlo jugando.",
 "shaderReceiveTitle":"Descargar y preparar", "shaderReceive1":"Pulsa Jugar. El launcher busca actualizaciones una vez al día, sin exigir una cuenta de GitHub.",
 "shaderReceive2":"Antes de jugar, este Mac prepara las combinaciones compatibles usando tu propio dump. La caché se reutiliza al volver a iniciar.",
 "shaderReceive3":"Actualizar ahora solo descarga y verifica el catálogo. La compilación ocurre al iniciar el juego de nuevo.",
 "shaderShareTitle":"Compartir descubrimientos", "shaderShare1":"Juega normalmente. El juego registra nuevas combinaciones de shaders y pipelines automáticamente.",
 "shaderShare2":"Pulsa Contribuir en GitHub. Finder selecciona el archivo preparado y el navegador abre el formulario oficial.",
 "shaderShare3":"Inicia sesión en GitHub, arrastra el archivo seleccionado al formulario y confirma el envío. Nada se envía automáticamente.",
 "shaderGuideTrust":"Solo se descargan catálogos revisados y firmados. Las contribuciones no incluyen dump, partidas ni registros y se revisan antes de publicarse.",
 "shaderGuideCoverage":"El catálogo reúne descubrimientos aprobados de cualquier zona de la campaña. Todavía no cubre todo ni elimina todas las pausas de compilación.",
 "shaderGuideLink":"Guía completa en GitHub", "shaderAdvanced":"Avanzado", "shaderDiscoveries":"Nuevos descubrimientos para compartir:",
 "updateOfficial":"Actualizar catálogo oficial…","contributeGithub":"Contribuir en GitHub…","checkingOfficial":"Verificando catálogo oficial…","officialUpdated":"Catálogo oficial verificado. Se preparará al volver a iniciar el juego.","contributionReady":"Tu contribución está lista","contributionInstructions":"El archivo de texto contiene descripciones gráficas, sin el dump, partidas ni registros. Finder mostrará el archivo seleccionado. Inicia sesión en GitHub, arrastra el archivo al campo indicado del formulario oficial y confirma el envío. Se revisará antes de entrar en el catálogo oficial.","continueGithub":"Continuar en GitHub","noDiscoveries":"Todavía no hay descubrimientos nuevos para compartir. Sigue jugando otras áreas.","newDiscoveries":"Nuevos descubrimientos en Catálogo comunitario","catalogueFailed":"No se pudo actualizar el catálogo. Se conservó la versión local.",
 "textures":"Texturas", "originalTextures":"Originales", "hdTextures":"2× · Kratos e inicio",
 "quitMenu":"Salir de God of War III", "subtitle":"REMASTERED · APPLE SILICON", "settings":"Prepara tu próxima partida",
 "resolution":"Resolución de salida", "upscaling":"Reescalado", "framerate":"Límite de fotogramas",
 "native":"Nativo", "quality":"MetalFX · Calidad", "balanced":"MetalFX · Equilibrado", "performance":"MetalFX · Rendimiento",
 "unlimited":"Sin límite", "language":"Idioma del launcher", "hud":"Mostrar HUD de rendimiento",
 "folder":"Carpeta del juego", "choose":"Elegir carpeta…", "change":"Cambiar…", "missing":"Selecciona tu dump para empezar.",
 "found":"Carpeta seleccionada · versión comprobada al jugar", "lost":"La carpeta no está disponible. Conecta el disco o selecciónala de nuevo.",
 "play":"Jugar", "stop":"Cerrar juego", "starting":"Preparando el juego…", "running":"Juego en ejecución", "finished":"Todo listo para la próxima partida.",
 "fixed":"V-Sync activado · Retina · Game Mode en pantalla completa", "limitNote":"Sin límite elimina el tope de 60 FPS. Siguen vigentes V-Sync y el límite de seguridad del motor (240 FPS).",
 "upscaleNote":"MetalFX espacial: sin generación de fotogramas ni historial temporal.",
 "selectTitle":"¿Dónde está tu God of War III?", "selectMessage":"Elige la carpeta del dump extraído que contiene eboot.bin, sce_module y sce_sys. Se recordará para la próxima vez.",
 "select":"Usar esta carpeta", "invalidTitle":"Carpeta del juego no encontrada", "invalid":"Elige un dump extraído. Un archivo .PKG o .ISO necesita extraerse primero.",
 "failed":"El juego no se inició. Consulta los logs.", "errorTitle":"No se pudo iniciar el juego", "error":"Comprueba que el disco esté conectado y que el dump corresponda a la versión preparada. El diagnóstico se guardó en los logs.",
 "logs":"Abrir logs", "ok":"Aceptar", "quitTitle":"¿Cerrar también el juego?", "quitMessage":"El juego sigue abierto. Sal desde su menú para guardar tu progreso antes de cerrar el launcher.",
 "shortcuts":"Teclas: 0 atajos · 1 rendimiento · 2 MetalFX · 3 resolución · 4 límite de 60 FPS",
 "communityShaders":"Catálogo comunitario", "importShaders":"Importar catálogo…", "exportShaders":"Exportar descubrimientos…", "catalogueBusy":"Procesando catálogo…", "catalogueReady":"Catálogo listo", "catalogueNote":"Las descripciones se compilarán en este Mac al iniciar. No se envían archivos automáticamente.",
 "clearShaders":"Borrar caché de shaders", "clearingShaders":"Borrando caché de shaders…",
 "clearShadersHelp":"Borra solo la caché gráfica. Se reconstruirá al jugar; se conservan partidas, ajustes y la recompilación ARM64.",
 "shadersCleared":"Caché de shaders borrada. Se reconstruirá al jugar.", "shadersEmpty":"La caché de shaders ya está vacía.",
 "shadersBusy":"Cierra el juego y espera a que termine la sesión antes de borrar la caché.", "shadersFailed":"No se pudo borrar la caché de shaders.",
 "keep":"Volver", "quit":"Cerrar", "local":"Comunidad · ARM64 nativo", "input":"Renderizado", "output":"salida",
 "version":"Probado: CUSA01623 v1.02 · Se verifican el ejecutable y los módulos antes de preparar.",
 "graphics_index":"Preparando catálogo gráfico · primer inicio",
 "checking":"Comprobando la versión del dump…",
 "scanning":"Preparando los archivos de tu dump…",
 "generating":"Recompilando para Apple Silicon…",
 "compiling":"Compilando el juego · primera preparación",
 "linking":"Finalizando y verificando el juego…",
 "ready":"Preparación completa. Iniciando…",
 "cancelled":"Preparación detenida. Pulsa Jugar para continuar.",
 "cancel":"Cancelar",
 "firstRun":"La primera preparación puede tardar varios minutos. El resultado se guardará en este Mac.",
 "dump_version":"Dump incompatible. Esta versión fue probada con CUSA01623 v1.02 y verifica el contenido del ejecutable y los módulos. No se aceptan otras actualizaciones, regiones ni ejecutables modificados.",
 "dump_missing":"El dump está incompleto. Selecciona la carpeta extraída con los ejecutables y los archivos de datos del juego.",
 "disk_space":"Libera al menos 3 GB en tu Mac para preparar el juego.",
 "prepare_failed":"No se pudo completar la preparación. Abre los logs para ver el diagnóstico y pulsa Jugar para reintentar.",
 "runtime_aot":"El juego se detuvo en una instrucción aún no soportada por el recompilador ARM64. El diagnóstico está en los logs. Esto no indica corrupción del dump.", "runtime_failed":"El juego se cerró inesperadamente. Abre los logs para consultar el error. Este aviso, por sí solo, no indica un problema con el dump.",
 "architecture":"Se requiere un Mac con Apple Silicon y macOS 26 o posterior. No se usa Rosetta.",
 "quitPreparingTitle":"¿Cancelar la preparación?",
 "quitPreparingMessage":"Se conservará el trabajo completado. Pulsa Jugar la próxima vez para continuar."
]]

func inputSize(_ p: Preferences) -> String {
    let heights = ["720":720, "1080":1080, "1440":1440, "2160":2160]
    let h = heights[p.resolution] ?? 2160
    let scales = ["native":1.0, "quality":1.5, "balanced":1.7, "performance":2.0]
    if p.upscaling == "native" { return "\(h * 16 / 9) × \(h)" }
    let units = Int((Double(h) / (scales[p.upscaling] ?? 1) / 18).rounded(.toNearestOrEven))
    return "\(units * 32) × \(units * 18)"
}

func isDump(_ url: URL) -> Bool {
    let fm = FileManager.default
    var dir: ObjCBool = false
    return fm.fileExists(atPath:url.appendingPathComponent("eboot.bin").path) &&
        fm.fileExists(atPath:url.appendingPathComponent("sce_module").path, isDirectory:&dir) && dir.boolValue &&
        fm.fileExists(atPath:url.appendingPathComponent("sce_sys").path, isDirectory:&dir) && dir.boolValue
}
func findDump(_ url: URL, depth: Int = 2) -> URL? {
    if isDump(url) { return url }
    guard depth > 0, let children = try? FileManager.default.contentsOfDirectory(at:url,
        includingPropertiesForKeys:[.isDirectoryKey], options:[.skipsHiddenFiles,.skipsPackageDescendants]), children.count < 64 else { return nil }
    for child in children.sorted(by:{$0.path < $1.path}) {
        if (try? child.resourceValues(forKeys:[.isDirectoryKey]).isDirectory) == true,
           let found = findDump(child,depth:depth-1) { return found }
    }
    return nil
}

struct PreparationState: Decodable {
    var phase: String
    var fraction: Double?
    var completed: Int?
    var total: Int?
    var error_code: String?
}

func failureKey(log: String, preparation: PreparationState?) -> String {
    if log.contains("ARM64 AOT coverage gap") { return "runtime_aot" }
    if log.contains("DUMP_VERSION:") { return "dump_version" }
    if let state = preparation, state.phase == "failed", let code = state.error_code { return code }
    return "runtime_failed"
}

// All display profiles share this game's graphics cache. Take the same flock as
// session.py, including across launcher copies, and never follow redirected
// child directories into saves or prepared CPU code.
enum ShaderCache {
    enum Failure: Error { case inUse, redirectedPath }
    static func clear(support: URL) throws -> Bool {
        let fm = FileManager.default
        let root = support.standardizedFileURL.resolvingSymlinksInPath()
        func directoryExists(_ url: URL) throws -> Bool {
            do {
                let attributes = try fm.attributesOfItem(atPath:url.path)
                guard attributes[.type] as? FileAttributeType == .typeDirectory else {
                    throw Failure.redirectedPath
                }
                return true
            } catch let error as NSError where error.domain == NSCocoaErrorDomain &&
                [NSFileNoSuchFileError, NSFileReadNoSuchFileError].contains(error.code) {
                return false
            }
        }
        guard try directoryExists(root) else { return false }
        let descriptor = Darwin.open(root.appendingPathComponent("session.lock").path,
            O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0o600)
        guard descriptor >= 0 else { throw POSIXError(POSIXErrorCode(rawValue:errno) ?? .EIO) }
        defer { Darwin.close(descriptor) }
        guard flock(descriptor, LOCK_EX | LOCK_NB) == 0 else {
            if errno == EWOULDBLOCK || errno == EAGAIN { throw Failure.inUse }
            throw POSIXError(POSIXErrorCode(rawValue:errno) ?? .EIO)
        }
        defer { _ = flock(descriptor, LOCK_UN) }
        var cache = root
        for component in ["User", "cache", "CUSA01623"] {
            cache.appendPathComponent(component)
            guard try directoryExists(cache) else { return false }
        }
        try fm.removeItem(at:cache)
        return true
    }
}

final class LauncherModel: ObservableObject {
    @Published var prefs: Preferences
    @Published var busy = false
    @Published var running = false
    @Published var clearingShaders = false
    @Published var catalogueBusy = false
    @Published var newCatalogueRecords = 0
    @Published var showCatalogueGuide = false
    var catalogueCheckedThisLaunch = false
    @Published var statusKey = ""
    @Published var progress: Double? = nil
    @Published var progressDetail = ""
    var statusState: PreparationState? {
        guard let data = try? Data(contentsOf:support.appendingPathComponent("preparation-status.json")) else { return nil }
        return try? JSONDecoder().decode(PreparationState.self,from:data)
    }
    let support: URL
    let resources: URL
    var process: Process?
    var logHandle: FileHandle?
    var timer: Timer?
    var stopping = false
    var choosingFolder = false
    var gameURL: URL? {
        guard let path = prefs.gamePath else { return nil }
        return URL(fileURLWithPath:path)
    }
    var valid: Bool { gameURL.map(isDump) ?? false }
    func t(_ key: String) -> String { translations[prefs.language]?[key] ?? translations["en"]?[key] ?? key }
    var hasTexturePack: Bool { FileManager.default.fileExists(atPath:resources.appendingPathComponent("TexturePack").path) }
    init(support: URL, resources: URL) {
        self.support = support; self.resources = resources
        let file = support.appendingPathComponent("preferences.json")
        prefs = (try? JSONDecoder().decode(Preferences.self, from:Data(contentsOf:file))) ?? Preferences()
        if !["pt-BR","en","es"].contains(prefs.language) { prefs.language = "en" }
        if !["720","1080","1440","2160"].contains(prefs.resolution) { prefs.resolution = "2160" }
        if !["native","quality","balanced","performance"].contains(prefs.upscaling) { prefs.upscaling = "native" }
        if !["original","2x"].contains(prefs.textures) || !hasTexturePack { prefs.textures = "original" }
        if !["60","0"].contains(prefs.cap) { prefs.cap = "60" }
        if let bookmark = prefs.bookmark {
            var stale = false
            if let url = try? URL(resolvingBookmarkData:bookmark, options:[.withoutUI], relativeTo:nil, bookmarkDataIsStale:&stale) {
                prefs.gamePath = url.path
                if stale { prefs.bookmark = try? url.bookmarkData(options:[],includingResourceValuesForKeys:nil,relativeTo:nil) }
            }
        }
    }
    func save() {
        do {
            try FileManager.default.createDirectory(at:support,withIntermediateDirectories:true)
            try JSONEncoder().encode(prefs).write(to:support.appendingPathComponent("preferences.json"), options:.atomic)
            NSApp?.mainMenu?.items.first?.submenu?.items.first?.title = t("quitMenu")
        } catch { showError(error.localizedDescription) }
    }
    func chooseFolder() {
        guard !choosingFolder else { return }
        choosingFolder = true
        defer { choosingFolder = false }
        let panel = NSOpenPanel()
        panel.title = t("selectTitle"); panel.message = t("selectMessage"); panel.prompt = t("select")
        panel.canChooseDirectories = true; panel.canChooseFiles = false; panel.allowsMultipleSelection = false
        if let old = gameURL { panel.directoryURL = old.deletingLastPathComponent() }
        guard panel.runModal() == .OK, let chosen = panel.url else { return }
        guard let found = findDump(chosen) else {
            let alert = NSAlert(); alert.messageText = t("invalidTitle"); alert.informativeText = t("invalid")
            alert.addButton(withTitle:t("ok")); alert.runModal(); return
        }
        prefs.gamePath = found.path
        prefs.bookmark = try? found.bookmarkData(options:[], includingResourceValuesForKeys:nil,relativeTo:nil)
        statusKey = ""; save()
    }
    func showError(_ detail: String) {
        NSApp.activate(ignoringOtherApps:true)
        let alert = NSAlert(); alert.alertStyle = .warning
        alert.messageText = t("errorTitle"); alert.informativeText = detail
        alert.addButton(withTitle:t("ok")); alert.addButton(withTitle:t("logs"))
        if alert.runModal() == .alertSecondButtonReturn { openLogs() }
    }
    func openLogs() {
        let logs = support.appendingPathComponent("Logs")
        try? FileManager.default.createDirectory(at:logs,withIntermediateDirectories:true)
        NSWorkspace.shared.open(logs)
    }
    func clearShaders() {
        guard !busy && !clearingShaders && !catalogueBusy else { return }
        clearingShaders = true; statusKey = "clearingShaders"
        let directory = support
        DispatchQueue.global(qos:.userInitiated).async { [weak self] in
            let result = Result { try ShaderCache.clear(support:directory) }
            DispatchQueue.main.async {
                guard let self = self else { return }
                self.clearingShaders = false
                switch result {
                case .success(let removed): self.statusKey = removed ? "shadersCleared" : "shadersEmpty"
                case .failure(ShaderCache.Failure.inUse): self.statusKey = "shadersBusy"
                case .failure(let error):
                    self.statusKey = "shadersFailed"
                    let alert = NSAlert(); alert.alertStyle = .warning
                    alert.messageText = self.t("shadersFailed"); alert.informativeText = error.localizedDescription
                    alert.addButton(withTitle:self.t("ok")); alert.runModal()
                }
            }
        }
    }
    func signedCatalogue(_ action: String, quiet: Bool = false, completion: (() -> Void)? = nil) {
        guard !busy && !clearingShaders && !catalogueBusy else { return }
        catalogueBusy = true
        if action != "pending" { statusKey = action == "refresh" ? "checkingOfficial" : "catalogueBusy" }
        let base = resources, directory = support
        DispatchQueue.global(qos:.userInitiated).async { [weak self] in
            let result = Result { () throws -> [String:Any] in
                let task=Process(), output=Pipe()
                task.executableURL=base.appendingPathComponent("Python/bin/python3.14")
                task.arguments=["-I","-S","-B",base.appendingPathComponent("catalog_service.py").path,
                    action,"--resources",base.path,"--support",directory.path]
                task.standardOutput=output; task.standardError=FileHandle.nullDevice; task.standardInput=FileHandle.nullDevice
                try task.run()
                let data=output.fileHandleForReading.readDataToEndOfFile(); task.waitUntilExit()
                guard task.terminationStatus == 0,
                      let info=try JSONSerialization.jsonObject(with:data) as? [String:Any] else {
                    throw NSError(domain:"Catalogue",code:1)
                }
                return info
            }
            DispatchQueue.main.async {
                guard let self=self else { return }; self.catalogueBusy=false
                switch result {
                case .success(let info):
                    if action == "pending" {
                        self.newCatalogueRecords=(info["new_records"] as? Int) ?? 0
                        if self.newCatalogueRecords > 0 { self.statusKey="newDiscoveries" }
                    } else if action == "contribute" {
                        if info["empty"] as? Bool == true {
                            self.statusKey="noDiscoveries"
                        } else if let path=info["file"] as? String {
                            self.statusKey="contributionReady"
                            let alert=NSAlert(); alert.messageText=self.t("contributionReady")
                            alert.informativeText=self.t("contributionInstructions")
                            alert.addButton(withTitle:self.t("continueGithub")); alert.addButton(withTitle:self.t("keep"))
                            if alert.runModal() == .alertFirstButtonReturn {
                                NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath:path)])
                                // Fixed project URL; no network response controls the browser destination.
                                let page=URL(string:"https://github.com/xPXavierx/gow3-macos-arm64/issues/new?template=shader_contribution.yml")!
                                NSWorkspace.shared.open(page)
                            }
                        }
                    } else if !quiet { self.statusKey="officialUpdated"; self.catalogueAlert("officialUpdated") }
                case .failure:
                    if !quiet { self.statusKey="catalogueFailed"; self.catalogueAlert("catalogueFailed") }
                }
                completion?()
            }
        }
    }
    func catalogueAlert(_ key: String) {
        let alert=NSAlert(); alert.messageText=t(key)
        alert.addButton(withTitle:t("ok")); alert.runModal()
    }
    func openCatalogueGuide() {
        let name = prefs.language == "pt-BR" ? "community-shaders.pt-BR.md" : prefs.language == "es" ? "community-shaders.es.md" : "community-shaders.md"
        NSWorkspace.shared.open(URL(string:"https://github.com/xPXavierx/gow3-macos-arm64/blob/macos-arm64/docs/" + name)!)
    }
    func communityCatalogue(importing: Bool) {
        guard !busy && !clearingShaders && !catalogueBusy else { return }
        let selected: URL
        if importing {
            let panel = NSOpenPanel(); panel.title = t("importShaders")
            panel.canChooseDirectories = false; panel.allowsMultipleSelection = false
            panel.allowedContentTypes = [UTType(filenameExtension:"jsonl") ?? .json]
            guard panel.runModal() == .OK, let url = panel.url else { return }; selected = url
        } else {
            let panel = NSSavePanel(); panel.title = t("exportShaders")
            panel.nameFieldStringValue = "GOW3-CUSA01623-1.02-catalogue.jsonl"
            panel.allowedContentTypes = [UTType(filenameExtension:"jsonl") ?? .json]
            guard panel.runModal() == .OK, let url = panel.url else { return }; selected = url
        }
        catalogueBusy = true; statusKey = "catalogueBusy"
        let base = resources; let directory = support
        DispatchQueue.global(qos:.userInitiated).async { [weak self] in
            let result = Result { () throws -> String in
                let task = Process(); let output = Pipe()
                task.executableURL = base.appendingPathComponent("Python/bin/python3.14")
                let args = importing ? ["import", selected.path, directory.appendingPathComponent("Community/imported").path] : ["export", directory.path, selected.path]
                task.arguments = ["-I","-S","-B",base.appendingPathComponent("community_catalog.py").path] + args + [base.appendingPathComponent("CommunityCatalog.jsonl").path]
                task.standardOutput = output; task.standardError = output
                try task.run()
                let data = output.fileHandleForReading.readDataToEndOfFile(); task.waitUntilExit()
                let text = String(data:data,encoding:.utf8) ?? ""
                guard task.terminationStatus == 0 else { throw NSError(domain:"CommunityCatalogue", code:Int(task.terminationStatus), userInfo:[NSLocalizedDescriptionKey:text]) }
                return text
            }
            DispatchQueue.main.async {
                guard let self = self else { return }; self.catalogueBusy = false
                switch result {
                case .success:
                    self.statusKey = "catalogueReady"
                    let alert = NSAlert(); alert.messageText = self.t("catalogueReady"); alert.informativeText = self.t("catalogueNote")
                    alert.addButton(withTitle:self.t("ok")); alert.runModal()
                case .failure(let error): self.statusKey = ""; self.showError(error.localizedDescription)
                }
            }
        }
    }
    func play() {
        guard !busy && !clearingShaders && !catalogueBusy else { return }
        if !valid { chooseFolder() }
        guard valid, let game = gameURL else { return }
        if !catalogueCheckedThisLaunch {
            catalogueCheckedThisLaunch=true
            signedCatalogue("refresh",quiet:true) { [weak self] in self?.play() }
            return
        }
        save(); busy = true; running = false; stopping = false; statusKey = "starting"
        progress = nil; progressDetail = ""
        try? FileManager.default.removeItem(at:support.appendingPathComponent("preparation-status.json"))
        do {
            let logs = support.appendingPathComponent("Logs")
            try FileManager.default.createDirectory(at:logs,withIntermediateDirectories:true)
            let stamp = ISO8601DateFormatter().string(from:Date()).replacingOccurrences(of:":",with:"-")
            let log = logs.appendingPathComponent("session-\(stamp).log")
            FileManager.default.createFile(atPath:log.path,contents:nil)
            logHandle = try FileHandle(forWritingTo:log)
            let task = Process()
            task.executableURL = resources.appendingPathComponent("Python/bin/python3.14")
            task.arguments = ["-I","-S","-B",resources.appendingPathComponent("session.py").path,
                "--resources",resources.path,"--support",support.path,"--game",game.path,
                "--resolution",prefs.resolution,"--upscaling",prefs.upscaling,"--cap",prefs.cap,"--hud",prefs.hud ? "1" : "0", "--language",prefs.language,"--shortcuts",prefs.shortcuts ? "1":"0"]
            task.arguments! += ["--textures",prefs.textures]
            task.standardOutput = logHandle; task.standardError = logHandle; task.standardInput = FileHandle.nullDevice
            task.currentDirectoryURL = support
            task.environment = ["PATH":"/usr/bin:/bin:/usr/sbin:/sbin","HOME":FileManager.default.homeDirectoryForCurrentUser.path,
                                "TMPDIR":NSTemporaryDirectory(),"LANG":"en_US.UTF-8"]
            task.terminationHandler = { [weak self] process in
                let exitCode = process.terminationStatus
                DispatchQueue.main.async {
                    guard let self = self else { return }
                    self.timer?.invalidate(); self.timer = nil
                    try? self.logHandle?.close(); self.logHandle = nil
                    if let data=try? Data(contentsOf:self.support.appendingPathComponent("runtime-graphics.json")),
                       let state=try? JSONSerialization.jsonObject(with:data) as? [String:Any],
                       let resolution=state["resolution"] as? Int, (0...3).contains(resolution),
                       let scaling=state["scaling"] as? Int, (0...3).contains(scaling) {
                        self.prefs.resolution=["720","1080","1440","2160"][resolution]
                        self.prefs.upscaling=["native","quality","balanced","performance"][scaling]
                        self.prefs.cap=(state["cap"] as? Int)==60 ? "60":"0"
                        self.prefs.hud=(state["hud"] as? Bool) ?? self.prefs.hud
                        self.prefs.shortcuts=(state["shortcuts"] as? Bool) ?? self.prefs.shortcuts
                        self.save()
                    }
                    let wasRunning = self.running
                    self.busy = false; self.running = false; self.process = nil; self.progress = nil; self.progressDetail = ""
                    self.statusKey = self.stopping && !wasRunning ? "cancelled" : "finished"
                    if exitCode != 0 && !self.stopping {
                        self.statusKey = "failed"
                        let diagnostic = (try? String(contentsOf:log,encoding:.utf8)) ?? ""
                        self.showError(self.t(failureKey(log:diagnostic,preparation:self.statusState)))
                    } else {
                        self.signedCatalogue("pending",quiet:true)
                    }
                }
            }
            process = task
            try task.run()
            timer = Timer.scheduledTimer(withTimeInterval:0.5,repeats:true) { [weak self] _ in
                guard let self = self else { return }
                if !self.running, let state = self.statusState {
                    self.statusKey = state.phase; self.progress = state.fraction
                    self.progressDetail = state.completed.flatMap { n in state.total.map { " \(n)/\($0)" } } ?? ""
                }
                guard let profile = try? String(contentsOf:self.support.appendingPathComponent("active-profile.txt"),encoding:.utf8) else { return }
                let pid = URL(fileURLWithPath:profile).appendingPathComponent(".native-test.pid")
                if FileManager.default.fileExists(atPath:pid.path) { self.running = true; self.statusKey = "running"; self.progress = nil }
            }
        } catch {
            busy = false; running = false; process = nil; try? logHandle?.close(); logHandle = nil
            showError(error.localizedDescription)
        }
    }
    func stop() {
        stopping = true
        process?.interrupt()
    }
}

// An AppKit popup keeps native keyboard/VoiceOver selection without the rounded
// SwiftUI picker bezel. Both the selected value and popup use the menu family.
final class FlatPopupButton:NSPopUpButton {
    override var focusRingMaskBounds:NSRect { bounds }
    override func drawFocusRingMask() { NSBezierPath(rect:bounds.insetBy(dx:1,dy:1)).fill() }
}

struct MenuChoice: NSViewRepresentable {
    let label: String
    @Binding var selection: String
    let options: [(String,String)]
    func makeCoordinator() -> Coordinator { Coordinator(selection:$selection) }
    func makeNSView(context:Context) -> NSPopUpButton {
        let button = FlatPopupButton(frame:.zero,pullsDown:false)
        button.isBordered = false
        button.font = NSFont(name:GameStyle.menuName,size:15)
        button.focusRingType = .exterior
        button.appearance = NSAppearance(named:.darkAqua)
        button.target = context.coordinator; button.action = #selector(Coordinator.changed(_:))
        (button.cell as? NSPopUpButtonCell)?.arrowPosition = .noArrow
        return button
    }
    func updateNSView(_ button:NSPopUpButton,context:Context) {
        context.coordinator.selection = $selection
        context.coordinator.values = options.map { $0.0 }
        button.isEnabled = context.environment.isEnabled
        if button.itemTitles != options.map({$0.1}) {
            button.removeAllItems()
            let face = NSFont(name:GameStyle.menuName,size:15) ?? NSFont.systemFont(ofSize:15)
            for (_,title) in options {
                button.addItem(withTitle:title)
                button.lastItem?.attributedTitle = NSAttributedString(string:title,attributes:[
                    .font:face,.foregroundColor:NSColor(red:0.89,green:0.85,blue:0.76,alpha:1)])
            }
        }
        button.selectItem(at:options.firstIndex(where:{$0.0 == selection}) ?? 0)
        button.setAccessibilityLabel(label)
        button.setAccessibilityIdentifier(label)
    }
    final class Coordinator:NSObject {
        var selection:Binding<String>
        var values:[String] = []
        init(selection:Binding<String>) { self.selection = selection }
        @objc func changed(_ sender:NSPopUpButton) {
            guard values.indices.contains(sender.indexOfSelectedItem) else { return }
            selection.wrappedValue = values[sender.indexOfSelectedItem]
        }
    }
}

struct MenuRow: View {
    let label:String
    @Binding var selection:String
    let options:[(String,String)]
    @State private var hovering = false
    var body:some View {
        HStack(spacing:12) {
            Text(label).font(GameStyle.menu(16)).foregroundColor(hovering ? GameStyle.gold : GameStyle.ivory)
                .frame(width:212,alignment:.leading)
            MenuChoice(label:label,selection:$selection,options:options).frame(width:250,height:30)
                .overlay(alignment:.trailing) {
                    Image(systemName:"chevron.down").font(.system(size:9,weight:.bold)).foregroundColor(GameStyle.gold)
                        .padding(.trailing,7).accessibilityHidden(true).allowsHitTesting(false)
                }
        }
        .padding(.horizontal,12).frame(height:40)
        .background(hovering ? GameStyle.red.opacity(0.13) : Color.black.opacity(0.16))
        .overlay(alignment:.leading) { Rectangle().fill(hovering ? GameStyle.red : Color.clear).frame(width:2) }
        .overlay(alignment:.bottom) { Rectangle().fill(GameStyle.gold.opacity(0.17)).frame(height:1) }
        .onHover { hovering = $0 }
    }
}

struct GameButtonStyle: ButtonStyle {
    var prominent = false
    @Environment(\.isEnabled) var enabled
    func makeBody(configuration:Configuration) -> some View {
        configuration.label
            .foregroundColor(prominent ? GameStyle.ivory : GameStyle.gold)
            .padding(.horizontal,prominent ? 28 : 0).padding(.vertical,prominent ? 11 : 4)
            .background(prominent ? GameStyle.red.opacity(configuration.isPressed ? 0.55 : 0.87) : Color.clear)
            .overlay { if prominent { Rectangle().stroke(GameStyle.gold.opacity(0.65),lineWidth:1) } }
            .opacity(enabled ? 1 : 0.4)
            .contentShape(Rectangle())
    }
}

struct CatalogueGuideView: View {
    @ObservedObject var model: LauncherModel
    func tr(_ key: String) -> String { model.t(key) }
    func steps(_ keys: [String]) -> some View {
        VStack(alignment:.leading,spacing:16) {
            ForEach(Array(keys.enumerated()),id:\.offset) { number,key in
                HStack(alignment:.top,spacing:10) {
                    Text("\(number+1)").font(GameStyle.menu(17)).foregroundColor(GameStyle.gold).frame(width:18)
                    Text(tr(key)).font(.system(size:13)).foregroundColor(GameStyle.ivory)
                        .fixedSize(horizontal:false,vertical:true)
                }
            }
        }
    }
    var body: some View {
        VStack(alignment:.leading,spacing:18) {
            Text(tr("shaderGuideTitle")).font(GameStyle.menu(25)).foregroundColor(GameStyle.gold)
            Text(tr("shaderGuideIntro")).font(.system(size:14)).foregroundColor(GameStyle.ivory)
            Rectangle().fill(GameStyle.red).frame(height:2)
            HStack(alignment:.top,spacing:32) {
                VStack(alignment:.leading,spacing:18) {
                    Text(tr("shaderReceiveTitle")).font(GameStyle.menu(17)).foregroundColor(GameStyle.gold)
                    steps(["shaderReceive1","shaderReceive2","shaderReceive3"])
                    Button(tr("updateOfficial")) {
                        model.showCatalogueGuide=false; model.signedCatalogue("update")
                    }.buttonStyle(GameButtonStyle(prominent:true)).font(GameStyle.menu(12))
                }.frame(maxWidth:.infinity,alignment:.topLeading)
                Rectangle().fill(GameStyle.gold.opacity(0.25)).frame(width:1)
                VStack(alignment:.leading,spacing:18) {
                    Text(tr("shaderShareTitle")).font(GameStyle.menu(17)).foregroundColor(GameStyle.gold)
                    steps(["shaderShare1","shaderShare2","shaderShare3"])
                    Button(tr("contributeGithub")) {
                        model.showCatalogueGuide=false; model.signedCatalogue("contribute")
                    }.buttonStyle(GameButtonStyle(prominent:true)).font(GameStyle.menu(12))
                    if model.newCatalogueRecords > 0 {
                        Text(tr("shaderDiscoveries") + " \(model.newCatalogueRecords)")
                            .font(.system(size:12)).foregroundColor(GameStyle.gold)
                    }
                }.frame(maxWidth:.infinity,alignment:.topLeading)
            }.fixedSize(horizontal:false,vertical:true)
            Text(tr("shaderGuideTrust")).font(.system(size:12)).foregroundColor(GameStyle.ivory)
                .fixedSize(horizontal:false,vertical:true)
            Text(tr("shaderGuideCoverage")).font(.system(size:12)).foregroundColor(GameStyle.muted)
                .fixedSize(horizontal:false,vertical:true)
            HStack(spacing:24) {
                Button(tr("shaderGuideLink")) { model.openCatalogueGuide() }
                    .buttonStyle(GameButtonStyle()).font(GameStyle.menu(11))
                Menu(tr("shaderAdvanced")) {
                    Button(tr("exportShaders")) {
                        model.showCatalogueGuide=false; model.communityCatalogue(importing:false)
                    }
                }.menuStyle(.borderlessButton).fixedSize().font(GameStyle.menu(11)).foregroundColor(GameStyle.gold)
                Spacer()
                Button(tr("keep")) { model.showCatalogueGuide=false }
                    .buttonStyle(GameButtonStyle()).font(GameStyle.menu(12)).keyboardShortcut(.cancelAction)
            }.padding(.top,4)
        }.padding(32).frame(width:920).background(Color(red:0.055,green:0.035,blue:0.03))
         .overlay(Rectangle().stroke(GameStyle.gold.opacity(0.5),lineWidth:1))
         .accessibilityIdentifier("communityShaderGuide")
    }
}

struct LauncherView: View {
    @ObservedObject var model: LauncherModel
    func tr(_ key: String) -> String { model.t(key) }
    var body: some View {
        ZStack(alignment:.topLeading) {
            Color.black
            if let art = NSImage(contentsOf:model.resources.appendingPathComponent("Branding/GOW-3-Art.jpg")) {
                Image(nsImage:art).resizable().scaledToFill().frame(width:1080,height:720).clipped()
                    .accessibilityHidden(true)
            }
            LinearGradient(stops:[.init(color:.black.opacity(0.97),location:0),
                .init(color:.black.opacity(0.94),location:0.43),.init(color:.black.opacity(0.52),location:0.61),
                .init(color:.black.opacity(0.06),location:1)],startPoint:.leading,endPoint:.trailing)
            LinearGradient(stops:[.init(color:.clear,location:0.57),.init(color:.black.opacity(0.94),location:1)],
                           startPoint:.top,endPoint:.bottom)
            VStack(alignment:.leading,spacing:0) {
                VStack(alignment:.leading,spacing:6) {
                    Text("GOD OF WAR III").font(.custom(GameStyle.titleName,size:48))
                        .foregroundStyle(LinearGradient(colors:[Color(red:0.95,green:0.83,blue:0.53),GameStyle.gold,Color(red:0.46,green:0.30,blue:0.13)],startPoint:.top,endPoint:.bottom))
                        .shadow(color:.black,radius:1,x:0,y:2).fixedSize()
                    Text("REMASTERED").font(GameStyle.menu(16)).tracking(6).foregroundColor(GameStyle.gold)
                    Rectangle().fill(LinearGradient(colors:[GameStyle.red,GameStyle.red.opacity(0)],startPoint:.leading,endPoint:.trailing))
                        .frame(width:230,height:2).padding(.top,11)
                }.padding(.bottom,20)
                VStack(alignment:.leading,spacing:0) {
                    MenuRow(label:tr("resolution"),selection:$model.prefs.resolution,
                            options:[("720","720p · 1280 × 720"),("1080","1080p · 1920 × 1080"),
                                     ("1440","1440p · 2560 × 1440"),("2160","4K · 3840 × 2160")])
                    MenuRow(label:tr("upscaling"),selection:$model.prefs.upscaling,
                            options:["native","quality","balanced","performance"].map { ($0,tr($0)) })
                    MenuRow(label:tr("framerate"),selection:$model.prefs.cap,
                            options:[("60","60 FPS"),("0",tr("unlimited"))])
                    if model.hasTexturePack {
                        MenuRow(label:tr("textures"),selection:$model.prefs.textures,
                                options:[("original",tr("originalTextures")),("2x",tr("hdTextures"))])
                    }
                    MenuRow(label:tr("language"),selection:$model.prefs.language,
                            options:[("pt-BR","Português brasileiro"),("en","English"),("es","Español")])
                    Text("\(tr("input")): \(inputSize(model.prefs))  →  \(model.prefs.resolution == "2160" ? "4K" : model.prefs.resolution + "p") \(tr("output"))")
                        .font(.system(size:11)).foregroundColor(GameStyle.gold).padding(.top,14).padding(.leading,12)
                    Toggle(isOn:$model.prefs.hud) {
                        Text(tr("hud")).font(GameStyle.menu(13)).foregroundColor(GameStyle.ivory)
                    }.toggleStyle(SquareToggleStyle()).padding(.top,17).padding(.leading,12)
                    Text(tr("shortcuts"))
                        .font(.system(size:11)).foregroundColor(GameStyle.muted).fixedSize(horizontal:false,vertical:true)
                        .frame(height:34,alignment:.topLeading).padding(.top,12).padding(.horizontal,12)
                }.frame(width:506).disabled(model.busy || model.clearingShaders)
                HStack(alignment:.top,spacing:16) {
                    VStack(alignment:.leading,spacing:5) {
                        Text(tr("folder")).font(GameStyle.menu(13)).foregroundColor(GameStyle.gold)
                        Text(model.valid ? (model.gameURL?.path ?? "") : tr(model.prefs.gamePath == nil ? "missing" : "lost"))
                            .font(.system(size:11)).foregroundColor(GameStyle.muted).lineLimit(2).truncationMode(.middle)
                            .frame(height:30,alignment:.topLeading).help(model.gameURL?.path ?? "")
                    }
                    Spacer(minLength:0)
                    Button(tr(model.valid ? "change" : "choose")) { model.chooseFolder() }
                        .font(GameStyle.menu(12)).buttonStyle(GameButtonStyle()).disabled(model.busy || model.clearingShaders)
                }.frame(width:482).padding(.horizontal,12).padding(.top,20)
                HStack(spacing:16) {
                    Button(action:{model.busy ? model.stop() : model.play()}) {
                        HStack(spacing:12) {
                            Text(tr(model.busy ? (model.running ? "stop" : "cancel") : "play")).font(GameStyle.menu(22))
                            Image(systemName:model.busy ? "stop.fill" : "chevron.right").font(.system(size:13,weight:.bold))
                        }.frame(minWidth:124)
                    }.buttonStyle(GameButtonStyle(prominent:true)).keyboardShortcut(.defaultAction).disabled(model.clearingShaders || model.catalogueBusy)
                    if model.busy && !model.running { ProgressView().controlSize(.small) }
                    VStack(alignment:.leading,spacing:5) {
                        Text(tr(model.statusKey.isEmpty ? (model.valid ? "found" : "missing") : model.statusKey) + model.progressDetail)
                            .font(.system(size:11)).foregroundColor(GameStyle.ivory).fixedSize(horizontal:false,vertical:true)
                        if model.busy && !model.running, let value = model.progress {
                            ProgressView(value:value).tint(GameStyle.gold).frame(width:268)
                        }
                    }
                }.frame(width:506,alignment:.leading).padding(.top,22)
                Text(tr("version")).font(.system(size:10)).foregroundColor(GameStyle.gold)
                    .frame(width:506,alignment:.leading).fixedSize(horizontal:false,vertical:true).padding(.top,10)
                if model.busy && !model.running {
                    Text(tr("firstRun")).font(.system(size:10)).foregroundColor(GameStyle.muted)
                        .frame(width:506,alignment:.leading).fixedSize(horizontal:false,vertical:true).padding(.top,4)
                }
                Spacer(minLength:0)
                HStack(alignment:.bottom) {
                    Text(tr("fixed")).font(.system(size:10)).foregroundColor(GameStyle.muted)
                        .frame(height:18,alignment:.center)
                    Spacer()
                    Button(tr("shaderGuideTitle") + (model.newCatalogueRecords > 0 ? " (\(model.newCatalogueRecords))" : "") + "…") {
                        model.showCatalogueGuide=true
                    }.font(GameStyle.menu(11)).buttonStyle(GameButtonStyle()).fixedSize()
                        .accessibilityIdentifier("communityShaders")
                        .disabled(model.busy || model.clearingShaders || model.catalogueBusy)
                    Button(tr(model.clearingShaders ? "clearingShaders" : "clearShaders")) { model.clearShaders() }
                        .font(GameStyle.menu(11)).buttonStyle(GameButtonStyle())
                        .disabled(model.busy || model.clearingShaders || model.catalogueBusy).help(tr("clearShadersHelp"))
                        .accessibilityIdentifier("clearShaderCache")
                    Button(tr("logs")) { model.openLogs() }.font(GameStyle.menu(11)).buttonStyle(GameButtonStyle())
                }
            }.padding(.top,40).padding(.horizontal,44).padding(.bottom,26)
        }.frame(width:1080,height:720).preferredColorScheme(.dark)
         .disabled(model.showCatalogueGuide)
         .overlay {
             if model.showCatalogueGuide {
                 ZStack {
                     Color.black.opacity(0.8).ignoresSafeArea()
                     CatalogueGuideView(model:model)
                 }.accessibilityAddTraits(.isModal)
             }
         }
         .onChange(of:model.prefs.language) { model.save() }
         .onChange(of:model.prefs.resolution) { model.save() }
         .onChange(of:model.prefs.upscaling) { model.save() }
         .onChange(of:model.prefs.textures) { model.save() }
         .onChange(of:model.prefs.cap) { model.save() }
         .onChange(of:model.prefs.hud) { model.save() }
    }
}

struct SquareToggleStyle:ToggleStyle {
    func makeBody(configuration:Configuration) -> some View {
        Button { configuration.isOn.toggle() } label: {
            HStack(spacing:10) {
                ZStack {
                    Rectangle().stroke(GameStyle.gold.opacity(0.65),lineWidth:1).frame(width:14,height:14)
                    if configuration.isOn { Rectangle().fill(GameStyle.gold).frame(width:8,height:8) }
                }
                configuration.label
            }.contentShape(Rectangle())
        }.buttonStyle(.plain).accessibilityElement(children:.combine)
         .accessibilityValue(configuration.isOn ? "1" : "0")
         .accessibilityAddTraits(.isToggle)
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    var window: NSWindow!
    var model: LauncherModel!
    let support: URL
    init(support: URL) { self.support = support }
    func applicationDidFinishLaunching(_ notification: Notification) {
        GameStyle.registerFonts(Bundle.main.resourceURL!)
        model = LauncherModel(support:support,resources:Bundle.main.resourceURL!)
        let view = NSHostingView(rootView:LauncherView(model:model))
        let size = view.fittingSize
        window = NSWindow(contentRect:NSRect(origin:.zero,size:size),styleMask:[.titled,.closable,.miniaturizable,.fullSizeContentView],backing:.buffered,defer:false)
        window.title = "God of War III · Apple Silicon"
        window.titleVisibility = .hidden; window.titlebarAppearsTransparent = true
        window.backgroundColor = .black; window.isMovableByWindowBackground = true
        window.contentView = view; window.delegate = self
        if let icon = NSImage(contentsOf:Bundle.main.resourceURL!.appendingPathComponent("AppIcon.icns")) {
            NSApp.applicationIconImage = icon
        }
        window.isReleasedWhenClosed = false; window.center(); window.makeKeyAndOrderFront(nil)
        let menu = NSMenu()
        let app = NSMenuItem(); menu.addItem(app); app.submenu = NSMenu()
        app.submenu?.addItem(withTitle:model.t("quitMenu"),action:#selector(NSApplication.terminate(_:)),keyEquivalent:"q")
        NSApp.mainMenu = menu; NSApp.activate(ignoringOtherApps:true)
        // Render only this app's own content for visual QA; never capture desktop
        // contents. Normal launches neither snapshot nor touch the bundle.
        if let i = CommandLine.arguments.firstIndex(of:"--preview"), i+1 < CommandLine.arguments.count {
            if CommandLine.arguments.contains("--preview-catalogue") { model.showCatalogueGuide=true }
            DispatchQueue.main.asyncAfter(deadline:.now()+0.2) {
                view.layoutSubtreeIfNeeded()
                view.displayIfNeeded()
                let bitmap = view.bitmapImageRepForCachingDisplay(in:view.bounds)!
                view.cacheDisplay(in:view.bounds,to:bitmap)
                try! bitmap.representation(using:.png,properties:[:])!.write(to:URL(fileURLWithPath:CommandLine.arguments[i+1]))
                NSApp.terminate(nil)
            }
            return
        }
        if !model.valid { DispatchQueue.main.async { self.model.chooseFolder() } }
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender:NSApplication) -> Bool { true }
    func applicationShouldTerminate(_ sender:NSApplication) -> NSApplication.TerminateReply {
        guard model?.busy == true else { return .terminateNow }
        let alert = NSAlert(); alert.messageText = model.t(model.running ? "quitTitle" : "quitPreparingTitle"); alert.informativeText = model.t(model.running ? "quitMessage" : "quitPreparingMessage")
        alert.addButton(withTitle:model.t("keep")); alert.addButton(withTitle:model.t("quit"))
        if alert.runModal() == .alertSecondButtonReturn { model.stop(); return .terminateNow }
        return .terminateCancel
    }
    func applicationShouldHandleReopen(_ sender:NSApplication,hasVisibleWindows flag:Bool) -> Bool {
        window.makeKeyAndOrderFront(nil); return true
    }
}

let args = CommandLine.arguments
if let index = args.firstIndex(of:"--self-test"), index+1 < args.count {
    GameStyle.registerFonts(Bundle.main.resourceURL!)
    precondition(NSFont(name:GameStyle.titleName,size:48) != nil)
    precondition(GameStyle.menuName == "Copperplate-Bold")
    let menuFont = CTFontCreateWithName(GameStyle.menuName as CFString,16,nil)
    let accented = Array("Resolução Português brasileiro Español Límite × 3840".utf16)
    var glyphs = Array(repeating:CGGlyph(0),count:accented.count)
    precondition(CTFontGetGlyphsForCharacters(menuFont,accented,&glyphs,accented.count))
    let keys = Set(translations["en"]!.keys)
    precondition(translations.values.allSatisfy { Set($0.keys) == keys && !$0.values.contains("") })
    precondition(failureKey(log:"STOP: ARM64 AOT coverage gap",preparation:nil) == "runtime_aot")
    precondition(failureKey(log:"Host fault",preparation:nil) == "runtime_failed")
    precondition(failureKey(log:"DUMP_VERSION: incompatible",preparation:nil) == "dump_version")
    precondition(failureKey(log:"",preparation:PreparationState(phase:"failed",error_code:"disk_space")) == "disk_space")
    let p = Preferences()
    let legacy = try! JSONDecoder().decode(Preferences.self,from:Data("{\"gamePath\":\"/example\"}".utf8))
    precondition(p.language == "en" && legacy.language == "en")
    precondition(legacy.gamePath == "/example" && legacy.cap == "60")
    precondition(legacy.textures == "original")
    let retired = try! JSONDecoder().decode(Preferences.self,from:Data("{\"framegen\":\"3\",\"cap\":\"0\",\"upscaling\":\"balanced\"}".utf8))
    precondition(retired.cap == "60" && retired.upscaling == "balanced")
    precondition(!String(data:try! JSONEncoder().encode(retired),encoding:.utf8)!.contains("framegen"))
    var hd = Preferences(); hd.textures = "2x"
    precondition((try! JSONDecoder().decode(Preferences.self,from:JSONEncoder().encode(hd))).textures == "2x")
    precondition((try! JSONDecoder().decode(Preferences.self,from:JSONEncoder().encode(p))).resolution == "2160")
    precondition(inputSize(p) == "3840 × 2160")
    var q = p; q.upscaling = "quality"; precondition(inputSize(q) == "2560 × 1440")
    q.upscaling = "balanced"; precondition(inputSize(q) == "2272 × 1278")
    let fixture = URL(fileURLWithPath:args[index+1]).deletingLastPathComponent().appendingPathComponent("preference-fixture-" + UUID().uuidString)
    let dump = fixture.appendingPathComponent("Dump/CUSA01623")
    try! FileManager.default.createDirectory(at:dump.appendingPathComponent("sce_module"),withIntermediateDirectories:true)
    try! FileManager.default.createDirectory(at:dump.appendingPathComponent("sce_sys"),withIntermediateDirectories:true)
    try! Data([0]).write(to:dump.appendingPathComponent("eboot.bin"))
    precondition(findDump(fixture)?.standardizedFileURL.path == dump.standardizedFileURL.path)
    precondition(findDump(fixture.appendingPathComponent("missing")) == nil)
    let state = fixture.appendingPathComponent("State")
    let first = LauncherModel(support:state,resources:Bundle.main.resourceURL!)
    precondition(first.prefs.language == "en")
    first.prefs.language = "es"; first.prefs.resolution = "1080"; first.prefs.upscaling = "balanced"; first.prefs.cap = "0"
    first.prefs.hud = false; first.prefs.shortcuts = false
    first.prefs.textures = first.hasTexturePack ? "2x" : "original"
    first.prefs.gamePath = dump.path
    first.prefs.bookmark = try! dump.bookmarkData(options:[],includingResourceValuesForKeys:nil,relativeTo:nil)
    first.save()
    let second = LauncherModel(support:state,resources:Bundle.main.resourceURL!)
    precondition(second.valid && second.prefs.language == "es" && second.prefs.resolution == "1080" && second.prefs.upscaling == "balanced" && second.prefs.cap == "0")
    precondition(second.prefs.textures == first.prefs.textures)
    precondition(!second.prefs.hud && !second.prefs.shortcuts)
    let fm = FileManager.default
    let gameCache = state.appendingPathComponent("User/cache/CUSA01623")
    let protectedPaths = ["preferences.json", "User/savedata/1/CUSA01623/AUTOSAVE/SAVEDATA",
        "User/settings.toml", "Prepared/recipe/aot/libgow3-recompiled.dylib",
        "Profiles/3840x2160-60/aot-patches.json", "CompilerCache/game.o",
        "User/cache/OTHER_GAME/pipeline.key", "Community/imported/shared.jsonl",
        "Community/contributions/current.jsonl", "Community/official/abi/trust-state.json",
        "Community/official/abi/current.json", "Community/official/abi/generation/manifest.sig"]
    for relative in protectedPaths where relative != "preferences.json" {
        let file = state.appendingPathComponent(relative)
        try! fm.createDirectory(at:file.deletingLastPathComponent(),withIntermediateDirectories:true)
        try! Data(("Keep " + relative).utf8).write(to:file)
    }
    let preserved = protectedPaths.map { (state.appendingPathComponent($0), try! Data(contentsOf:state.appendingPathComponent($0))) }
    func verifyPreserved() {
        for (file, contents) in preserved { precondition((try! Data(contentsOf:file)) == contents) }
    }
    try! fm.createDirectory(at:gameCache,withIntermediateDirectories:true)
    try! Data([1,2,3]).write(to:gameCache.appendingPathComponent("shader.spv"))
    try! Data([4,5,6]).write(to:gameCache.appendingPathComponent("pipeline.key"))
    // Another Python desktop session owns this exact flock for its whole life.
    let holder = Process(), lease = Pipe(), ready = Pipe()
    holder.executableURL = Bundle.main.resourceURL!.appendingPathComponent("Python/bin/python3.14")
    holder.arguments = ["-I","-S","-B","-c",
        "import fcntl,sys; f=open(sys.argv[1],'a+'); fcntl.flock(f,fcntl.LOCK_EX); print('ready',flush=True); sys.stdin.read()",
        state.appendingPathComponent("session.lock").path]
    holder.standardInput = lease; holder.standardOutput = ready
    try! holder.run()
    var pollReady = pollfd(fd:ready.fileHandleForReading.fileDescriptor,events:Int16(POLLIN),revents:0)
    precondition(poll(&pollReady,1,5000) == 1)
    precondition(String(data:ready.fileHandleForReading.availableData,encoding:.utf8) == "ready\n")
    do {
        _ = try ShaderCache.clear(support:state)
        preconditionFailure("Cleared a cache owned by another game session")
    } catch ShaderCache.Failure.inUse {} catch { preconditionFailure("Wrong lock error: \(error)") }
    precondition(fm.fileExists(atPath:gameCache.appendingPathComponent("shader.spv").path))
    verifyPreserved()
    try! lease.fileHandleForWriting.close(); holder.waitUntilExit(); precondition(holder.terminationStatus == 0)
    first.busy = true; first.clearShaders()
    precondition(!first.clearingShaders && fm.fileExists(atPath:gameCache.path))
    first.busy = false; first.clearShaders(); first.play()
    precondition(first.process == nil && !first.busy)
    let clearDeadline = Date().addingTimeInterval(5)
    while first.clearingShaders && Date() < clearDeadline { RunLoop.current.run(until:Date().addingTimeInterval(0.01)) }
    precondition(!first.clearingShaders && first.statusKey == "shadersCleared")
    precondition(!fm.fileExists(atPath:gameCache.path)); verifyPreserved()
    precondition((try! ShaderCache.clear(support:state)) == false)
    let absent = fixture.appendingPathComponent("NeverCreated")
    precondition((try! ShaderCache.clear(support:absent)) == false && !fm.fileExists(atPath:absent.path))
    // Refuse symlinks at any cache path component, even dangling ones.
    let cpuDirectory = state.appendingPathComponent("Prepared")
    for relative in ["User/cache/CUSA01623", "User/cache", "User"] {
        let path = state.appendingPathComponent(relative), held = state.appendingPathComponent("held-directory")
        let existed = fm.fileExists(atPath:path.path)
        if existed { try! fm.moveItem(at:path,to:held) }
        try! fm.createSymbolicLink(at:path,withDestinationURL:cpuDirectory)
        do { _ = try ShaderCache.clear(support:state); preconditionFailure("Followed cache symlink") }
        catch ShaderCache.Failure.redirectedPath {} catch { preconditionFailure("Wrong path error: \(error)") }
        try! fm.removeItem(at:path)
        if existed { try! fm.moveItem(at:held,to:path) }
        verifyPreserved()
    }
    try! fm.createSymbolicLink(at:gameCache,withDestinationURL:absent)
    do { _ = try ShaderCache.clear(support:state); preconditionFailure("Accepted dangling cache symlink") }
    catch ShaderCache.Failure.redirectedPath {} catch { preconditionFailure("Wrong dangling-link error: \(error)") }
    try! fm.removeItem(at:gameCache); verifyPreserved()
    let lock = state.appendingPathComponent("session.lock")
    try! fm.removeItem(at:lock)
    try! fm.createSymbolicLink(at:lock,withDestinationURL:state.appendingPathComponent("preferences.json"))
    do { _ = try ShaderCache.clear(support:state); preconditionFailure("Followed lock symlink") } catch {}
    try! fm.removeItem(at:lock); verifyPreserved()
    try! fm.createDirectory(at:gameCache,withIntermediateDirectories:true)
    try! fm.createSymbolicLink(at:gameCache.appendingPathComponent("savedata-link"),
        withDestinationURL:state.appendingPathComponent("User/savedata"))
    precondition(try! ShaderCache.clear(support:state)); verifyPreserved()
    try! FileManager.default.removeItem(at:dump)
    precondition(!second.valid)
    try! FileManager.default.removeItem(at:fixture)
    let result: [String:Any] = ["folder_and_settings_persisted":true,"missing_folder_detected":true,"nested_dump_discovery":true,"languages":translations.keys.sorted(),"translation_keys":keys.count,"preferences_roundtrip":true,"dimensions":true,
        "retina":Bundle.main.object(forInfoDictionaryKey:"NSHighResolutionCapable") as? Bool == true,
        "game_mode":Bundle.main.object(forInfoDictionaryKey:"LSSupportsGameMode") as? Bool == true,
        "title_font":GameStyle.titleName,"menu_font":GameStyle.menuName,"menu_accents_supported":true,
        "first_launch_language":"en","saved_language_preserved":true,
        "shader_cache_clear_preserves_saves_settings_aot":true,
        "shader_cache_clear_interprocess_session_lock":true,
        "shader_cache_clear_busy_and_play_guards":true,
        "shader_cache_clear_missing_cache_safe":true,
        "shader_cache_clear_symlink_guards":true,
        "artwork_bundled":FileManager.default.fileExists(atPath:Bundle.main.resourceURL!.appendingPathComponent("Branding/GOW-3-Art.jpg").path)]
    try! JSONSerialization.data(withJSONObject:result,options:[.prettyPrinted,.sortedKeys]).write(to:URL(fileURLWithPath:args[index+1]))
    exit(0)
}
let support: URL
if let i = args.firstIndex(of:"--support"), i+1 < args.count { support = URL(fileURLWithPath:args[i+1]) }
else { support = FileManager.default.urls(for:.applicationSupportDirectory,in:.userDomainMask)[0].appendingPathComponent("God of War III ARM64") }
let application = NSApplication.shared
application.setActivationPolicy(.regular)
let delegate = AppDelegate(support:support)
application.delegate = delegate
application.run()
