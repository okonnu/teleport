import AppKit
import ApplicationServices
import Carbon.HIToolbox

private let appVersion = "1.3.2"
private let displayName = "LC49G95T"
private let macInput = 1
private let linuxInput = 16
private let preferenceKey = "UbuntuShortcutsEnabled"
private let injectedEventMarker: Int64 = 0x54454C45504F5254
private let app = NSApplication.shared

private let betterDisplayCLICandidates = [
    "/opt/homebrew/bin/betterdisplaycli",
    "/usr/local/bin/betterdisplaycli"
]

private let configurationDirectory = FileManager.default.homeDirectoryForCurrentUser
    .appendingPathComponent("Library/Application Support/Teleport", isDirectory: true)
private let configurationURL = configurationDirectory.appendingPathComponent("shortcuts.json")

private struct ShortcutConfiguration: Codable {
    let version: Int
    let terminalBundleIDs: [String]
    let rules: [ShortcutRule]
}

private struct ShortcutRule: Codable {
    let from: String
    let to: String?
    let action: String?
    let argument: String?
    let scope: String?
}

private struct ParsedChord {
    let keyCode: CGKeyCode?
    let modifiers: CGEventFlags
}

private struct ResolvedRule {
    let definition: ShortcutRule
    let source: ParsedChord
    let target: ParsedChord?
}

private enum MacAction {
    case shortcut(String)
    case openApplication(String)
    case process(String, [String])
}

private enum ConfigurationError: Error, CustomStringConvertible {
    case invalidChord(String)
    case missingTarget(String)
    case duplicateSource(String)
    case unsupportedAction(String)
    case missingArgument(String)
    case invalidScope(String)

    var description: String {
        switch self {
        case .invalidChord(let chord): return "Invalid shortcut chord: \(chord)"
        case .missingTarget(let source): return "Rule \(source) needs either to or action"
        case .duplicateSource(let source): return "Duplicate shortcut: \(source)"
        case .unsupportedAction(let action): return "Unsupported action: \(action)"
        case .missingArgument(let source): return "Rule \(source) needs an argument"
        case .invalidScope(let scope): return "Invalid scope: \(scope)"
        }
    }
}

private var eventTap: CFMachPort?
private var eventTapSource: CFRunLoopSource?
private var ubuntuShortcutsEnabled = UserDefaults.standard.bool(forKey: preferenceKey)
private var activeRemappingAvailable = false
private var windowsKeyIsDown = false
private var windowsKeyWasUsed = false
private var currentConfiguration = defaultConfiguration()
private var resolvedRules: [ResolvedRule] = []
private var configurationModificationDate: Date?
private var configurationStatus = "Default configuration"
private var frontmostBundleID = NSWorkspace.shared.frontmostApplication?.bundleIdentifier

private var betterDisplayCLI: String? {
    betterDisplayCLICandidates.first { FileManager.default.isExecutableFile(atPath: $0) }
}

private func log(_ message: String) {
    fputs("\(Date()): \(message)\n", stderr)
    fflush(stderr)
}

private func run(_ executable: String, _ arguments: [String]) {
    DispatchQueue.global(qos: .userInitiated).async {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: executable)
        task.arguments = arguments
        task.standardOutput = FileHandle.nullDevice
        task.standardError = FileHandle.nullDevice

        do {
            try task.run()
            task.waitUntilExit()
            log("\(executable) finished with status \(task.terminationStatus)")
        } catch {
            log("Could not run \(executable): \(error)")
        }
    }
}

private func switchInput(to value: Int) {
    guard let betterDisplayCLI else {
        log("BetterDisplay CLI was not found")
        return
    }

    run(betterDisplayCLI, [
        "set",
        "-namelike=\(displayName)",
        "-ddc=\(value)",
        "-vcp=inputSelect"
    ])
}

private func defaultConfiguration() -> ShortcutConfiguration {
    let terminals = [
        "com.apple.Terminal",
        "com.googlecode.iterm2",
        "dev.warp.Warp-Stable",
        "net.kovidgoyal.kitty",
        "org.alacritty",
        "com.github.wez.wezterm",
        "com.microsoft.VSCode"
    ]

    func action(_ from: String, _ name: String, argument: String? = nil, scope: String? = nil) -> ShortcutRule {
        ShortcutRule(from: from, to: nil, action: name, argument: argument, scope: scope)
    }

    return ShortcutConfiguration(version: 2, terminalBundleIDs: terminals, rules: [
        action("ctrl+a", "selectAll", scope: "nonTerminal"),
        action("ctrl+c", "copy", scope: "nonTerminal"),
        action("ctrl+v", "paste", scope: "nonTerminal"),
        action("ctrl+x", "cut", scope: "nonTerminal"),
        action("ctrl+z", "undo", scope: "nonTerminal"),
        action("ctrl+shift+z", "redo", scope: "nonTerminal"),
        action("ctrl+f", "find", scope: "nonTerminal"),
        action("ctrl+s", "save", scope: "nonTerminal"),
        action("ctrl+p", "print", scope: "nonTerminal"),
        action("ctrl+n", "new", scope: "nonTerminal"),
        action("ctrl+o", "open", scope: "nonTerminal"),
        action("ctrl+t", "newTab", scope: "nonTerminal"),
        action("ctrl+w", "closeWindow", scope: "nonTerminal"),
        action("ctrl+l", "focusLocation", scope: "nonTerminal"),
        action("ctrl+delete", "deleteNextWord", scope: "nonTerminal"),
        action("alt+tab", "nextApplication"),
        action("alt+shift+tab", "previousApplication"),
        action("alt+f4", "closeWindow"),
        action("alt+f2", "spotlight"),
        action("alt+left", "back"),
        action("alt+right", "forward"),
        action("win", "spotlight"),
        action("win+e", "openFinder"),
        action("win+d", "showDesktop"),
        action("win+l", "lockScreen"),
        action("win+tab", "missionControl"),
        action("win+left", "tileLeft"),
        action("win+right", "tileRight"),
        action("win+up", "tileUp"),
        action("win+down", "tileDown"),
        action("print_screen", "screenshotControls"),
        action("shift+print_screen", "captureArea"),
        action("alt+print_screen", "captureWindow"),
        action("ctrl+print_screen", "copyScreen"),
        action("ctrl+shift+print_screen", "copyArea"),
        action("ctrl+alt+delete", "forceQuit"),
        action("ctrl+alt+t", "openTerminal")
    ])
}

private let keyCodes: [String: CGKeyCode] = [
    "a": CGKeyCode(kVK_ANSI_A), "c": CGKeyCode(kVK_ANSI_C),
    "d": CGKeyCode(kVK_ANSI_D), "e": CGKeyCode(kVK_ANSI_E),
    "f": CGKeyCode(kVK_ANSI_F), "l": CGKeyCode(kVK_ANSI_L),
    "m": CGKeyCode(kVK_ANSI_M), "n": CGKeyCode(kVK_ANSI_N),
    "o": CGKeyCode(kVK_ANSI_O), "p": CGKeyCode(kVK_ANSI_P),
    "q": CGKeyCode(kVK_ANSI_Q), "s": CGKeyCode(kVK_ANSI_S),
    "t": CGKeyCode(kVK_ANSI_T), "u": CGKeyCode(kVK_ANSI_U),
    "v": CGKeyCode(kVK_ANSI_V), "w": CGKeyCode(kVK_ANSI_W),
    "x": CGKeyCode(kVK_ANSI_X), "z": CGKeyCode(kVK_ANSI_Z),
    "space": CGKeyCode(kVK_Space), "tab": CGKeyCode(kVK_Tab),
    "escape": CGKeyCode(kVK_Escape), "delete": CGKeyCode(kVK_ForwardDelete),
    "left": CGKeyCode(kVK_LeftArrow), "right": CGKeyCode(kVK_RightArrow),
    "up": CGKeyCode(kVK_UpArrow), "down": CGKeyCode(kVK_DownArrow),
    "left_bracket": CGKeyCode(kVK_ANSI_LeftBracket),
    "right_bracket": CGKeyCode(kVK_ANSI_RightBracket),
    "f2": CGKeyCode(kVK_F2), "f3": CGKeyCode(kVK_F3), "f4": CGKeyCode(kVK_F4),
    "print_screen": CGKeyCode(kVK_F13)
]

private let macActions: [String: MacAction] = [
    "selectAll": .shortcut("cmd+a"),
    "copy": .shortcut("cmd+c"),
    "paste": .shortcut("cmd+v"),
    "cut": .shortcut("cmd+x"),
    "undo": .shortcut("cmd+z"),
    "redo": .shortcut("cmd+shift+z"),
    "find": .shortcut("cmd+f"),
    "save": .shortcut("cmd+s"),
    "print": .shortcut("cmd+p"),
    "new": .shortcut("cmd+n"),
    "open": .shortcut("cmd+o"),
    "newTab": .shortcut("cmd+t"),
    "closeWindow": .shortcut("cmd+w"),
    "focusLocation": .shortcut("cmd+l"),
    "deleteNextWord": .shortcut("alt+delete"),
    "nextApplication": .shortcut("cmd+tab"),
    "previousApplication": .shortcut("cmd+shift+tab"),
    "spotlight": .shortcut("cmd+space"),
    "back": .shortcut("cmd+left_bracket"),
    "forward": .shortcut("cmd+right_bracket"),
    "openFinder": .openApplication("Finder"),
    "showDesktop": .shortcut("cmd+f3"),
    "lockScreen": .shortcut("ctrl+cmd+q"),
    "missionControl": .shortcut("ctrl+up"),
    "tileLeft": .shortcut("fn+ctrl+left"),
    "tileRight": .shortcut("fn+ctrl+right"),
    "tileUp": .shortcut("fn+ctrl+up"),
    "tileDown": .shortcut("fn+ctrl+down"),
    "screenshotControls": .process("/usr/bin/open", ["-a", "Screenshot"]),
    "captureArea": .process("/usr/sbin/screencapture", ["-i"]),
    "captureWindow": .process("/usr/sbin/screencapture", ["-i", "-W"]),
    "copyScreen": .process("/usr/sbin/screencapture", ["-c"]),
    "copyArea": .process("/usr/sbin/screencapture", ["-i", "-c"]),
    "forceQuit": .shortcut("cmd+alt+escape"),
    "openTerminal": .process("/usr/bin/open", ["-n", "-a", "Terminal"])
]

private func parseChord(_ value: String) throws -> ParsedChord {
    let parts = value.lowercased().split(separator: "+").map(String.init)
    guard !parts.isEmpty else { throw ConfigurationError.invalidChord(value) }

    var modifiers: CGEventFlags = []
    var keyCode: CGKeyCode?

    for part in parts {
        switch part {
        case "ctrl", "control": modifiers.insert(.maskControl)
        case "alt", "option": modifiers.insert(.maskAlternate)
        case "win", "cmd", "command": modifiers.insert(.maskCommand)
        case "shift": modifiers.insert(.maskShift)
        case "fn": modifiers.insert(.maskSecondaryFn)
        default:
            guard keyCode == nil, let code = keyCodes[part] else {
                throw ConfigurationError.invalidChord(value)
            }
            keyCode = code
        }
    }

    if keyCode == nil && modifiers != [.maskCommand] {
        throw ConfigurationError.invalidChord(value)
    }
    return ParsedChord(keyCode: keyCode, modifiers: modifiers)
}

private let supportedScopes: Set<String> = ["all", "nonTerminal", "terminalOnly"]

private func resolveConfiguration(_ configuration: ShortcutConfiguration) throws -> [ResolvedRule] {
    var seen: Set<String> = []
    var result: [ResolvedRule] = []

    for definition in configuration.rules {
        let normalizedSource = definition.from.lowercased().replacingOccurrences(of: " ", with: "")
        guard seen.insert(normalizedSource).inserted else {
            throw ConfigurationError.duplicateSource(definition.from)
        }

        if let scope = definition.scope, !supportedScopes.contains(scope) {
            throw ConfigurationError.invalidScope(scope)
        }

        let source = try parseChord(normalizedSource)
        var target = try definition.to.map(parseChord)
        if target == nil && definition.action == nil {
            throw ConfigurationError.missingTarget(definition.from)
        }
        if target != nil && definition.action != nil {
            throw ConfigurationError.missingTarget(definition.from)
        }
        if let action = definition.action {
            guard action == "openApplication" || macActions[action] != nil else {
                throw ConfigurationError.unsupportedAction(action)
            }
            if action == "openApplication" && (definition.argument?.isEmpty ?? true) {
                throw ConfigurationError.missingArgument(definition.from)
            }
            if let macAction = macActions[action], case .shortcut(let chord) = macAction {
                target = try parseChord(chord)
            }
        }
        result.append(ResolvedRule(definition: definition, source: source, target: target))
    }
    return result
}

private func writeDefaultConfigurationIfNeeded() throws {
    guard !FileManager.default.fileExists(atPath: configurationURL.path) else { return }
    try FileManager.default.createDirectory(at: configurationDirectory, withIntermediateDirectories: true)
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
    let data = try encoder.encode(defaultConfiguration())
    try data.write(to: configurationURL, options: .atomic)
}

@discardableResult
private func loadConfiguration() -> Bool {
    do {
        try writeDefaultConfigurationIfNeeded()
        let data = try Data(contentsOf: configurationURL)
        let configuration = try JSONDecoder().decode(ShortcutConfiguration.self, from: data)
        let rules = try resolveConfiguration(configuration)
        currentConfiguration = configuration
        resolvedRules = rules
        configurationModificationDate = try? configurationURL.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate
        configurationStatus = "Loaded \(rules.count) shortcuts"
        log("\(configurationStatus) from \(configurationURL.path)")
        return true
    } catch {
        configurationModificationDate = try? configurationURL.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate
        configurationStatus = "Configuration error"
        log("Could not load shortcut configuration: \(error)")
        return false
    }
}

private func relevantFlags(_ flags: CGEventFlags) -> CGEventFlags {
    flags.intersection([.maskControl, .maskAlternate, .maskCommand, .maskShift, .maskSecondaryFn])
}

private func sourceFlags(keyCode: CGKeyCode, eventFlags: CGEventFlags) -> CGEventFlags {
    var flags = relevantFlags(eventFlags)

    // Many external Windows keyboards expose Print Screen as F13 with the
    // hardware-only Function flag set. Treat that flag as part of the key,
    // not as an extra modifier, so the configured "print_screen" rule matches.
    if keyCode == CGKeyCode(kVK_F13) {
        flags.remove(.maskSecondaryFn)
    }

    return flags
}

private func isTerminalApplication() -> Bool {
    guard let frontmostBundleID else { return false }
    return currentConfiguration.terminalBundleIDs.contains(frontmostBundleID)
}

private func ruleApplies(_ rule: ResolvedRule) -> Bool {
    switch rule.definition.scope ?? "all" {
    case "nonTerminal": return !isTerminalApplication()
    case "terminalOnly": return isTerminalApplication()
    default: return true
    }
}

private func matchingRule(keyCode: CGKeyCode, flags: CGEventFlags) -> ResolvedRule? {
    resolvedRules.first {
        $0.source.keyCode == keyCode && $0.source.modifiers == flags && ruleApplies($0)
    }
}

private func windowsKeyRule() -> ResolvedRule? {
    resolvedRules.first {
        $0.source.keyCode == nil && $0.source.modifiers == [.maskCommand] && ruleApplies($0)
    }
}

private func postShortcut(_ chord: ParsedChord) {
    guard let keyCode = chord.keyCode else { return }
    DispatchQueue.main.async {
        guard
            let keyDown = CGEvent(keyboardEventSource: nil, virtualKey: keyCode, keyDown: true),
            let keyUp = CGEvent(keyboardEventSource: nil, virtualKey: keyCode, keyDown: false)
        else { return }

        for event in [keyDown, keyUp] {
            event.flags = chord.modifiers
            event.setIntegerValueField(.eventSourceUserData, value: injectedEventMarker)
        }
        keyDown.post(tap: .cghidEventTap)
        keyUp.post(tap: .cghidEventTap)
    }
}

private func performAction(_ rule: ResolvedRule) {
    guard let actionName = rule.definition.action else { return }
    if actionName == "openApplication" {
        if let name = rule.definition.argument { run("/usr/bin/open", ["-a", name]) }
        return
    }

    guard let action = macActions[actionName] else { return }
    switch action {
    case .shortcut(let chord):
        if let parsed = try? parseChord(chord) { postShortcut(parsed) }
    case .openApplication(let name):
        run("/usr/bin/open", ["-a", name])
    case .process(let executable, let arguments):
        run(executable, arguments)
    }
}

private func executeRule(_ rule: ResolvedRule) {
    if let target = rule.target {
        postShortcut(target)
    } else {
        performAction(rule)
    }
    log("Shortcut \(rule.definition.from) executed")
}

private let eventCallback: CGEventTapCallBack = { _, type, event, _ in
    if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
        if let eventTap { CGEvent.tapEnable(tap: eventTap, enable: true) }
        return Unmanaged.passUnretained(event)
    }

    if event.getIntegerValueField(.eventSourceUserData) == injectedEventMarker {
        return Unmanaged.passUnretained(event)
    }

    if type == .leftMouseDown || type == .rightMouseDown || type == .otherMouseDown {
        if windowsKeyIsDown { windowsKeyWasUsed = true }
        return Unmanaged.passUnretained(event)
    }

    let keyCode = CGKeyCode(event.getIntegerValueField(.keyboardEventKeycode))
    let flags = sourceFlags(keyCode: keyCode, eventFlags: event.flags)

    if type == .flagsChanged {
        let isWindowsKey = keyCode == CGKeyCode(kVK_Command) || keyCode == CGKeyCode(kVK_RightCommand)
        guard isWindowsKey, ubuntuShortcutsEnabled, activeRemappingAvailable, windowsKeyRule() != nil else {
            return Unmanaged.passUnretained(event)
        }

        if flags.contains(.maskCommand) {
            windowsKeyIsDown = true
            windowsKeyWasUsed = false
        } else if windowsKeyIsDown {
            let rule = windowsKeyRule()
            let shouldExecute = !windowsKeyWasUsed
            windowsKeyIsDown = false
            windowsKeyWasUsed = false
            if shouldExecute, let rule { executeRule(rule) }
        }
        return Unmanaged.passUnretained(event)
    }

    guard type == .keyDown || type == .keyUp else {
        return Unmanaged.passUnretained(event)
    }

    let isKeyDown = type == .keyDown
    let isRepeat = event.getIntegerValueField(.keyboardEventAutorepeat) != 0

    // KVM hotkeys are fixed and always pass through to Deskflow.
    if flags == [.maskControl, .maskAlternate] && isKeyDown && !isRepeat {
        if keyCode == CGKeyCode(kVK_ANSI_M) {
            switchInput(to: macInput)
            return Unmanaged.passUnretained(event)
        }
        if keyCode == CGKeyCode(kVK_ANSI_U) {
            switchInput(to: linuxInput)
            return Unmanaged.passUnretained(event)
        }
    }

    guard ubuntuShortcutsEnabled && activeRemappingAvailable else {
        return Unmanaged.passUnretained(event)
    }

    if windowsKeyIsDown && isKeyDown { windowsKeyWasUsed = true }

    guard let rule = matchingRule(keyCode: keyCode, flags: flags) else {
        return Unmanaged.passUnretained(event)
    }

    if isKeyDown && !isRepeat { executeRule(rule) }
    return nil
}

private func installEventTap() {
    if let source = eventTapSource {
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, .commonModes)
    }
    if let tap = eventTap { CFMachPortInvalidate(tap) }

    activeRemappingAvailable = AXIsProcessTrusted()
    let options: CGEventTapOptions = activeRemappingAvailable ? .defaultTap : .listenOnly
    let types: [CGEventType] = [
        .keyDown, .keyUp, .flagsChanged, .leftMouseDown, .rightMouseDown, .otherMouseDown
    ]
    let eventMask = types.reduce(CGEventMask(0)) { $0 | (CGEventMask(1) << $1.rawValue) }

    eventTap = CGEvent.tapCreate(
        tap: .cgSessionEventTap,
        place: .headInsertEventTap,
        options: options,
        eventsOfInterest: eventMask,
        callback: eventCallback,
        userInfo: nil
    )

    guard let eventTap else {
        log("Unable to create event tap; enable Input Monitoring")
        return
    }
    eventTapSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0)
    if let eventTapSource {
        CFRunLoopAddSource(CFRunLoopGetCurrent(), eventTapSource, .commonModes)
    }
    CGEvent.tapEnable(tap: eventTap, enable: true)
    log(activeRemappingAvailable ? "Active shortcut remapping is available" : "Running in listen-only mode")
}

private func runSelfTest() -> Int32 {
    do {
        guard sourceFlags(
            keyCode: CGKeyCode(kVK_F13),
            eventFlags: [.maskSecondaryFn]
        ).isEmpty else {
            throw ConfigurationError.invalidChord("print_screen hardware flags")
        }
        print("PASS physical print_screen Function-flag normalization")

        let encoder = JSONEncoder()
        let encodedConfiguration = try encoder.encode(defaultConfiguration())
        let configuration = try JSONDecoder().decode(ShortcutConfiguration.self, from: encodedConfiguration)
        let rules = try resolveConfiguration(configuration)
        let expectedSources: Set<String> = [
            "ctrl+a", "ctrl+c", "ctrl+v", "ctrl+x", "ctrl+z", "ctrl+shift+z",
            "ctrl+f", "ctrl+s", "ctrl+p", "ctrl+n", "ctrl+o", "ctrl+t", "ctrl+w",
            "ctrl+l", "ctrl+delete", "alt+tab", "alt+shift+tab", "alt+f4", "alt+f2",
            "alt+left", "alt+right", "win", "win+e", "win+d", "win+l", "win+tab",
            "win+left", "win+right", "win+up", "win+down", "print_screen",
            "shift+print_screen", "alt+print_screen", "ctrl+print_screen",
            "ctrl+shift+print_screen", "ctrl+alt+delete", "ctrl+alt+t"
        ]
        let actualSources = Set(rules.map { $0.definition.from })
        guard actualSources == expectedSources else {
            print("FAIL: expected \(expectedSources.count) rules, found \(actualSources.count)")
            return 1
        }
        let expectedActions = [
            "ctrl+a": "selectAll", "ctrl+c": "copy", "ctrl+v": "paste",
            "ctrl+x": "cut", "ctrl+z": "undo", "ctrl+shift+z": "redo",
            "ctrl+f": "find", "ctrl+s": "save", "ctrl+p": "print",
            "ctrl+n": "new", "ctrl+o": "open", "ctrl+t": "newTab",
            "ctrl+w": "closeWindow", "ctrl+l": "focusLocation",
            "ctrl+delete": "deleteNextWord", "alt+tab": "nextApplication",
            "alt+shift+tab": "previousApplication", "alt+f4": "closeWindow",
            "alt+f2": "spotlight", "alt+left": "back", "alt+right": "forward",
            "win": "spotlight", "win+e": "openFinder", "win+d": "showDesktop",
            "win+l": "lockScreen", "win+tab": "missionControl",
            "win+left": "tileLeft", "win+right": "tileRight",
            "win+up": "tileUp", "win+down": "tileDown",
            "print_screen": "screenshotControls", "shift+print_screen": "captureArea",
            "alt+print_screen": "captureWindow", "ctrl+print_screen": "copyScreen",
            "ctrl+shift+print_screen": "copyArea", "ctrl+alt+delete": "forceQuit",
            "ctrl+alt+t": "openTerminal"
        ]
        let actualActions = Dictionary(uniqueKeysWithValues: rules.compactMap { rule in
            rule.definition.action.map { (rule.definition.from, $0) }
        })
        guard actualActions == expectedActions else {
            print("FAIL: one or more shortcuts map to the wrong macOS action")
            return 1
        }
        guard FileManager.default.isExecutableFile(atPath: "/usr/bin/open") else {
            print("FAIL: /usr/bin/open is unavailable")
            return 1
        }
        guard FileManager.default.isExecutableFile(atPath: "/usr/sbin/screencapture") else {
            print("FAIL: /usr/sbin/screencapture is unavailable")
            return 1
        }
        guard FileManager.default.fileExists(atPath: "/System/Applications/Utilities/Screenshot.app") else {
            print("FAIL: Screenshot.app is unavailable")
            return 1
        }
        guard NSWorkspace.shared.urlForApplication(withBundleIdentifier: "com.apple.Terminal") != nil else {
            print("FAIL: Terminal.app is unavailable")
            return 1
        }
        let duplicateRule = configuration.rules[0]
        let duplicateConfiguration = ShortcutConfiguration(
            version: configuration.version,
            terminalBundleIDs: configuration.terminalBundleIDs,
            rules: configuration.rules + [duplicateRule]
        )
        do {
            _ = try resolveConfiguration(duplicateConfiguration)
            print("FAIL: duplicate shortcut was accepted")
            return 1
        } catch ConfigurationError.duplicateSource {
            print("PASS duplicate shortcut rejection")
        }
        for rule in rules.sorted(by: { $0.definition.from < $1.definition.from }) {
            let destination = rule.definition.to ?? "action:\(rule.definition.action ?? "missing")"
            print("PASS \(rule.definition.from) -> \(destination)")
        }
        print("PASS: all \(rules.count) shortcut mappings validated")
        return 0
    } catch {
        print("FAIL: \(error)")
        return 1
    }
}

private final class MenuController: NSObject {
    private let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let toggleItem = NSMenuItem(title: "Ubuntu shortcuts", action: #selector(toggleShortcuts), keyEquivalent: "")
    private let configurationItem = NSMenuItem(title: "Shortcut configuration", action: nil, keyEquivalent: "")
    private let inputPermissionItem = NSMenuItem(title: "Enable Input Monitoring", action: #selector(openInputMonitoring), keyEquivalent: "")
    private let accessibilityItem = NSMenuItem(title: "Enable Accessibility permission", action: #selector(openAccessibility), keyEquivalent: "")

    override init() {
        super.init()
        toggleItem.target = self
        inputPermissionItem.target = self
        accessibilityItem.target = self
        configurationItem.isEnabled = false

        let menu = NSMenu()
        menu.addItem(toggleItem)
        menu.addItem(configurationItem)

        let editItem = NSMenuItem(title: "Edit shortcuts JSON", action: #selector(editConfiguration), keyEquivalent: "")
        editItem.target = self
        menu.addItem(editItem)

        let reloadItem = NSMenuItem(title: "Reload shortcuts", action: #selector(reloadConfiguration), keyEquivalent: "")
        reloadItem.target = self
        menu.addItem(reloadItem)
        menu.addItem(.separator())
        menu.addItem(inputPermissionItem)
        menu.addItem(accessibilityItem)
        menu.addItem(.separator())

        let macItem = NSMenuItem(title: "Switch display to Mac", action: #selector(switchToMac), keyEquivalent: "")
        macItem.target = self
        menu.addItem(macItem)
        let ubuntuItem = NSMenuItem(title: "Switch display to Ubuntu", action: #selector(switchToUbuntu), keyEquivalent: "")
        ubuntuItem.target = self
        menu.addItem(ubuntuItem)
        menu.addItem(.separator())

        let quitItem = NSMenuItem(title: "Quit Teleport", action: #selector(quit), keyEquivalent: "q")
        quitItem.target = self
        menu.addItem(quitItem)
        statusItem.menu = menu
        statusItem.button?.toolTip = "Teleport Ubuntu shortcuts"
        refresh()
    }

    func refresh() {
        toggleItem.state = ubuntuShortcutsEnabled ? .on : .off
        statusItem.button?.title = ubuntuShortcutsEnabled ? "UK On" : "UK Off"
        configurationItem.title = configurationStatus
        inputPermissionItem.isHidden = CGPreflightListenEventAccess()
        accessibilityItem.isHidden = activeRemappingAvailable
    }

    @objc private func toggleShortcuts() {
        ubuntuShortcutsEnabled.toggle()
        UserDefaults.standard.set(ubuntuShortcutsEnabled, forKey: preferenceKey)
        if ubuntuShortcutsEnabled && !activeRemappingAvailable { requestAccessibilityPermission() }
        refresh()
        log("Ubuntu shortcuts \(ubuntuShortcutsEnabled ? "enabled" : "disabled")")
    }

    @objc private func editConfiguration() {
        run("/usr/bin/open", ["-e", configurationURL.path])
    }

    @objc private func reloadConfiguration() {
        _ = loadConfiguration()
        refresh()
    }

    @objc private func openAccessibility() { requestAccessibilityPermission() }
    @objc private func openInputMonitoring() { requestInputMonitoringPermission() }
    @objc private func switchToMac() { switchInput(to: macInput) }
    @objc private func switchToUbuntu() { switchInput(to: linuxInput) }
    @objc private func quit() { NSApplication.shared.terminate(nil) }
}

private func requestAccessibilityPermission() {
    let promptKey = kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String
    _ = AXIsProcessTrustedWithOptions([promptKey: true] as CFDictionary)
    NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")!)
}

private func requestInputMonitoringPermission() {
    _ = CGRequestListenEventAccess()
    NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent")!)
}

if CommandLine.arguments.contains("--version") {
    print("Teleport \(appVersion)")
    exit(0)
}
if CommandLine.arguments.contains("--self-test") {
    exit(runSelfTest())
}

_ = loadConfiguration()
if !CGPreflightListenEventAccess() { _ = CGRequestListenEventAccess() }
installEventTap()
private let menuController = MenuController()

NSWorkspace.shared.notificationCenter.addObserver(
    forName: NSWorkspace.didActivateApplicationNotification,
    object: nil,
    queue: .main
) { notification in
    frontmostBundleID = (notification.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication)?.bundleIdentifier
}

Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { _ in
    let trusted = AXIsProcessTrusted()
    if trusted != activeRemappingAvailable || (eventTap == nil && CGPreflightListenEventAccess()) {
        installEventTap()
    }

    let date = try? configurationURL.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate
    if date != configurationModificationDate {
        _ = loadConfiguration()
    }
    menuController.refresh()
}

NSWorkspace.shared.openApplication(
    at: URL(fileURLWithPath: "/Applications/BetterDisplay.app"),
    configuration: NSWorkspace.OpenConfiguration()
)

log("Menu-bar helper started; Ubuntu shortcuts are \(ubuntuShortcutsEnabled ? "on" : "off")")
app.setActivationPolicy(.accessory)
app.run()
