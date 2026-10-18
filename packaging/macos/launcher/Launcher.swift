// SPDX-License-Identifier: GPL-2.0-or-later
import AppKit
import SwiftUI
import Darwin
import CoreText

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
    var cap = "60"
    var hud = true
    var gamePath: String? = nil
    var bookmark: Data? = nil
    init() {}
    enum CodingKeys: String, CodingKey { case language, resolution, upscaling, cap, hud, gamePath, bookmark }
    init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy:CodingKeys.self)
        language = try values.decodeIfPresent(String.self,forKey:.language) ?? "en"
        resolution = try values.decodeIfPresent(String.self,forKey:.resolution) ?? "2160"
        upscaling = try values.decodeIfPresent(String.self,forKey:.upscaling) ?? "native"
        cap = try values.decodeIfPresent(String.self,forKey:.cap) ?? "60"
        hud = try values.decodeIfPresent(Bool.self,forKey:.hud) ?? true
        gamePath = try values.decodeIfPresent(String.self,forKey:.gamePath)
        bookmark = try values.decodeIfPresent(Data.self,forKey:.bookmark)
    }
}

let translations: [String: [String: String]] = [
"pt-BR": [
 "quitMenu":"Sair de God of War III", "subtitle":"REMASTERED · APPLE SILICON", "settings":"Prepare sua próxima partida",
 "resolution":"Resolução de saída", "upscaling":"Upscaling", "framerate":"Limite de quadros",
 "native":"Nativo", "quality":"MetalFX · Qualidade", "balanced":"MetalFX · Equilibrado", "performance":"MetalFX · Desempenho",
 "unlimited":"Ilimitado", "language":"Idioma do launcher", "hud":"Mostrar HUD de desempenho do Metal",
 "folder":"Pasta do jogo", "choose":"Escolher pasta…", "change":"Alterar…", "missing":"Selecione seu dump para começar.",
 "found":"Pasta encontrada · pronta para jogar", "lost":"A pasta não está disponível. Conecte o disco ou selecione-a novamente.",
 "play":"Jogar", "stop":"Encerrar jogo", "starting":"Preparando o jogo…", "running":"Jogo em execução", "finished":"Tudo pronto para a próxima partida.",
 "fixed":"V-Sync ligado · Retina · Game Mode em tela cheia", "limitNote":"Ilimitado remove o limite de 60. V-Sync e o teto de segurança do motor (240 FPS) continuam valendo.",
 "upscaleNote":"MetalFX espacial: sem geração de quadros e sem histórico temporal.",
 "selectTitle":"Onde está seu God of War III?", "selectMessage":"Escolha a pasta do dump extraído que contém eboot.bin, sce_module e sce_sys. Ela será lembrada nas próximas aberturas.",
 "select":"Usar esta pasta", "invalidTitle":"Pasta do jogo não encontrada", "invalid":"Escolha o dump extraído. Um arquivo .PKG ou .ISO ainda precisa ser extraído.",
 "failed":"O jogo não iniciou. Consulte os logs.", "errorTitle":"Não foi possível iniciar o jogo", "error":"Confira se o disco está conectado e se o dump corresponde à versão preparada. O diagnóstico foi salvo nos logs.",
 "logs":"Abrir logs", "ok":"OK", "quitTitle":"Encerrar o jogo também?", "quitMessage":"O jogo ainda está aberto. Saia pelo menu do jogo para salvar seu progresso antes de fechar o launcher.",
 "keep":"Voltar", "quit":"Encerrar", "local":"Versão local · ARM64 nativo", "input":"Renderização", "output":"saída",
 "version":"Este aplicativo local usa o executável CUSA01623 v1.02 validado neste port. O dump permanece na pasta escolhida."
],
"en": [
 "quitMenu":"Quit God of War III", "subtitle":"REMASTERED · APPLE SILICON", "settings":"Ready for your next session",
 "resolution":"Output resolution", "upscaling":"Upscaling", "framerate":"Frame rate cap",
 "native":"Native", "quality":"MetalFX · Quality", "balanced":"MetalFX · Balanced", "performance":"MetalFX · Performance",
 "unlimited":"Unlimited", "language":"Launcher language", "hud":"Show Metal performance HUD",
 "folder":"Game folder", "choose":"Choose folder…", "change":"Change…", "missing":"Select your game dump to get started.",
 "found":"Folder found · ready to play", "lost":"The folder is unavailable. Connect the drive or select it again.",
 "play":"Play", "stop":"Stop game", "starting":"Preparing the game…", "running":"Game running", "finished":"Ready for your next session.",
 "fixed":"V-Sync on · Retina · Game Mode in fullscreen", "limitNote":"Unlimited removes the 60 FPS cap. V-Sync and the engine's safety ceiling (240 FPS) still apply.",
 "upscaleNote":"Spatial MetalFX: no frame generation or temporal history.",
 "selectTitle":"Where is your God of War III?", "selectMessage":"Choose the extracted dump folder containing eboot.bin, sce_module and sce_sys. It will be remembered next time.",
 "select":"Use this folder", "invalidTitle":"Game folder not found", "invalid":"Choose an extracted dump. A .PKG or .ISO file must be extracted first.",
 "failed":"The game could not start. Check the logs.", "errorTitle":"The game could not start", "error":"Check that the drive is connected and the dump matches the prepared version. Diagnostics were saved in the logs.",
 "logs":"Open logs", "ok":"OK", "quitTitle":"Stop the game too?", "quitMessage":"The game is still open. Exit through the game's menu to save your progress before closing the launcher.",
 "keep":"Go back", "quit":"Stop", "local":"Local build · Native ARM64", "input":"Rendering", "output":"output",
 "version":"This local app uses the CUSA01623 v1.02 executable validated for this port. Your dump stays in the selected folder."
],
"es": [
 "quitMenu":"Salir de God of War III", "subtitle":"REMASTERED · APPLE SILICON", "settings":"Prepara tu próxima partida",
 "resolution":"Resolución de salida", "upscaling":"Reescalado", "framerate":"Límite de fotogramas",
 "native":"Nativo", "quality":"MetalFX · Calidad", "balanced":"MetalFX · Equilibrado", "performance":"MetalFX · Rendimiento",
 "unlimited":"Sin límite", "language":"Idioma del launcher", "hud":"Mostrar HUD de rendimiento de Metal",
 "folder":"Carpeta del juego", "choose":"Elegir carpeta…", "change":"Cambiar…", "missing":"Selecciona tu dump para empezar.",
 "found":"Carpeta encontrada · lista para jugar", "lost":"La carpeta no está disponible. Conecta el disco o selecciónala de nuevo.",
 "play":"Jugar", "stop":"Cerrar juego", "starting":"Preparando el juego…", "running":"Juego en ejecución", "finished":"Todo listo para la próxima partida.",
 "fixed":"V-Sync activado · Retina · Game Mode en pantalla completa", "limitNote":"Sin límite elimina el tope de 60 FPS. Siguen vigentes V-Sync y el límite de seguridad del motor (240 FPS).",
 "upscaleNote":"MetalFX espacial: sin generación de fotogramas ni historial temporal.",
 "selectTitle":"¿Dónde está tu God of War III?", "selectMessage":"Elige la carpeta del dump extraído que contiene eboot.bin, sce_module y sce_sys. Se recordará para la próxima vez.",
 "select":"Usar esta carpeta", "invalidTitle":"Carpeta del juego no encontrada", "invalid":"Elige un dump extraído. Un archivo .PKG o .ISO necesita extraerse primero.",
 "failed":"El juego no se inició. Consulta los logs.", "errorTitle":"No se pudo iniciar el juego", "error":"Comprueba que el disco esté conectado y que el dump corresponda a la versión preparada. El diagnóstico se guardó en los logs.",
 "logs":"Abrir logs", "ok":"Aceptar", "quitTitle":"¿Cerrar también el juego?", "quitMessage":"El juego sigue abierto. Sal desde su menú para guardar tu progreso antes de cerrar el launcher.",
 "keep":"Volver", "quit":"Cerrar", "local":"Versión local · ARM64 nativo", "input":"Renderizado", "output":"salida",
 "version":"Esta aplicación local usa el ejecutable CUSA01623 v1.02 validado en este port. El dump permanece en la carpeta elegida."
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

final class LauncherModel: ObservableObject {
    @Published var prefs: Preferences
    @Published var busy = false
    @Published var running = false
    @Published var statusKey = ""
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
    init(support: URL, resources: URL) {
        self.support = support; self.resources = resources
        let file = support.appendingPathComponent("preferences.json")
        prefs = (try? JSONDecoder().decode(Preferences.self, from:Data(contentsOf:file))) ?? Preferences()
        if !["pt-BR","en","es"].contains(prefs.language) { prefs.language = "en" }
        if !["720","1080","1440","2160"].contains(prefs.resolution) { prefs.resolution = "2160" }
        if !["native","quality","balanced","performance"].contains(prefs.upscaling) { prefs.upscaling = "native" }
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
    func play() {
        guard !busy else { return }
        if !valid { chooseFolder() }
        guard valid, let game = gameURL else { return }
        save(); busy = true; running = false; stopping = false; statusKey = "starting"
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
                "--resolution",prefs.resolution,"--upscaling",prefs.upscaling,"--cap",prefs.cap,"--hud",prefs.hud ? "1" : "0", "--language",prefs.language]
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
                    self.busy = false; self.running = false; self.process = nil; self.statusKey = "finished"
                    if exitCode != 0 && !self.stopping { self.statusKey = "failed"; self.showError(self.t("error")) }
                }
            }
            process = task
            try task.run()
            timer = Timer.scheduledTimer(withTimeInterval:0.5,repeats:true) { [weak self] _ in
                guard let self = self, let profile = try? String(contentsOf:self.support.appendingPathComponent("active-profile.txt"),encoding:.utf8) else { return }
                let pid = URL(fileURLWithPath:profile).appendingPathComponent(".native-test.pid")
                if FileManager.default.fileExists(atPath:pid.path) { self.running = true; self.statusKey = "running" }
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
        .padding(.horizontal,12).frame(height:46)
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
                }.padding(.bottom,27)
                VStack(alignment:.leading,spacing:0) {
                    MenuRow(label:tr("resolution"),selection:$model.prefs.resolution,
                            options:[("720","720p · 1280 × 720"),("1080","1080p · 1920 × 1080"),
                                     ("1440","1440p · 2560 × 1440"),("2160","4K · 3840 × 2160")])
                    MenuRow(label:tr("upscaling"),selection:$model.prefs.upscaling,
                            options:["native","quality","balanced","performance"].map { ($0,tr($0)) })
                    MenuRow(label:tr("framerate"),selection:$model.prefs.cap,
                            options:[("60","60 FPS"),("0",tr("unlimited"))])
                    MenuRow(label:tr("language"),selection:$model.prefs.language,
                            options:[("pt-BR","Português brasileiro"),("en","English"),("es","Español")])
                    Text("\(tr("input")): \(inputSize(model.prefs))  →  \(model.prefs.resolution == "2160" ? "4K" : model.prefs.resolution + "p") \(tr("output"))")
                        .font(.system(size:11)).foregroundColor(GameStyle.gold).padding(.top,14).padding(.leading,12)
                    Toggle(isOn:$model.prefs.hud) {
                        Text(tr("hud")).font(GameStyle.menu(13)).foregroundColor(GameStyle.ivory)
                    }.toggleStyle(SquareToggleStyle()).padding(.top,17).padding(.leading,12)
                    Text(tr(model.prefs.cap == "0" ? "limitNote" : "upscaleNote"))
                        .font(.system(size:11)).foregroundColor(GameStyle.muted).fixedSize(horizontal:false,vertical:true)
                        .frame(height:34,alignment:.topLeading).padding(.top,12).padding(.horizontal,12)
                }.frame(width:506).disabled(model.busy)
                HStack(alignment:.top,spacing:16) {
                    VStack(alignment:.leading,spacing:5) {
                        Text(tr("folder")).font(GameStyle.menu(13)).foregroundColor(GameStyle.gold)
                        Text(model.valid ? (model.gameURL?.path ?? "") : tr(model.prefs.gamePath == nil ? "missing" : "lost"))
                            .font(.system(size:11)).foregroundColor(GameStyle.muted).lineLimit(2).truncationMode(.middle)
                            .frame(height:30,alignment:.topLeading).help(model.gameURL?.path ?? "")
                    }
                    Spacer(minLength:0)
                    Button(tr(model.valid ? "change" : "choose")) { model.chooseFolder() }
                        .font(GameStyle.menu(12)).buttonStyle(GameButtonStyle()).disabled(model.busy)
                }.frame(width:482).padding(.horizontal,12).padding(.top,20)
                HStack(spacing:16) {
                    Button(action:{model.busy ? model.stop() : model.play()}) {
                        HStack(spacing:12) {
                            Text(tr(model.busy ? "stop" : "play")).font(GameStyle.menu(22))
                            Image(systemName:model.busy ? "stop.fill" : "chevron.right").font(.system(size:13,weight:.bold))
                        }.frame(minWidth:124)
                    }.buttonStyle(GameButtonStyle(prominent:true)).keyboardShortcut(.defaultAction)
                    if model.busy && !model.running { ProgressView().controlSize(.small) }
                    Text(tr(model.statusKey.isEmpty ? (model.valid ? "found" : "missing") : model.statusKey))
                        .font(.system(size:11)).foregroundColor(GameStyle.muted).fixedSize(horizontal:false,vertical:true)
                }.frame(width:506,alignment:.leading).padding(.top,22)
                Spacer(minLength:0)
                HStack(alignment:.bottom) {
                    Text(tr("fixed")).font(.system(size:10)).foregroundColor(GameStyle.muted)
                        .frame(height:18,alignment:.center)
                    Spacer()
                    Button(tr("logs")) { model.openLogs() }.font(GameStyle.menu(11)).buttonStyle(GameButtonStyle())
                }
            }.padding(.top,55).padding(.horizontal,44).padding(.bottom,26)
        }.frame(width:1080,height:720).preferredColorScheme(.dark)
         .onChange(of:model.prefs.language) { model.save() }
         .onChange(of:model.prefs.resolution) { model.save() }
         .onChange(of:model.prefs.upscaling) { model.save() }
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
        let alert = NSAlert(); alert.messageText = model.t("quitTitle"); alert.informativeText = model.t("quitMessage")
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
    let p = Preferences()
    let legacy = try! JSONDecoder().decode(Preferences.self,from:Data("{\"gamePath\":\"/example\"}".utf8))
    precondition(p.language == "en" && legacy.language == "en")
    precondition(legacy.gamePath == "/example" && legacy.cap == "60")
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
    first.prefs.gamePath = dump.path
    first.prefs.bookmark = try! dump.bookmarkData(options:[],includingResourceValuesForKeys:nil,relativeTo:nil)
    first.save()
    let second = LauncherModel(support:state,resources:Bundle.main.resourceURL!)
    precondition(second.valid && second.prefs.language == "es" && second.prefs.resolution == "1080" && second.prefs.upscaling == "balanced" && second.prefs.cap == "0")
    try! FileManager.default.removeItem(at:dump)
    precondition(!second.valid)
    try! FileManager.default.removeItem(at:fixture)
    let result: [String:Any] = ["folder_and_settings_persisted":true,"missing_folder_detected":true,"nested_dump_discovery":true,"languages":translations.keys.sorted(),"translation_keys":keys.count,"preferences_roundtrip":true,"dimensions":true,
        "retina":Bundle.main.object(forInfoDictionaryKey:"NSHighResolutionCapable") as? Bool == true,
        "game_mode":Bundle.main.object(forInfoDictionaryKey:"LSSupportsGameMode") as? Bool == true,
        "title_font":GameStyle.titleName,"menu_font":GameStyle.menuName,"menu_accents_supported":true,
        "first_launch_language":"en","saved_language_preserved":true,
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
