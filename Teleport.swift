import AppKit
import ApplicationServices
import Carbon.HIToolbox

private let appVersion = "1.2.0"
private let betterDisplayCLICandidates = [
    "/opt/homebrew/bin/betterdisplaycli",
    "/usr/local/bin/betterdisplaycli"
]
private let displayName = "LC49G95T"
private let macInput = 1
private let linuxInput = 16
private let preferenceKey = "UbuntuShortcutsEnabled"
private let app = NSApplication.shared

private var eventTap: CFMachPort?
private var eventTapSource: CFRunLoopSource?
private var ubuntuShortcutsEnabled = UserDefaults.standard.bool(forKey: preferenceKey)
private var activeRemappingAvailable = false

private var betterDisplayCLI: String? {
    betterDisplayCLICandidates.first { FileManager.default.isExecutableFile(atPath: $0) }
}

private let terminalBundleIDs: Set<String> = [
    "com.apple.Terminal",
    "com.googlecode.iterm2",
    "dev.warp.Warp-Stable",
    "net.kovidgoyal.kitty",
    "org.alacritty",
    "com.github.wez.wezterm",
    "com.microsoft.VSCode"
]

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

private func relevantFlags(_ flags: CGEventFlags) -> CGEventFlags {
    flags.intersection([.maskControl, .maskAlternate, .maskCommand, .maskShift])
}

private func replaceModifiers(
    on event: CGEvent,
    with modifiers: CGEventFlags,
    keyCode: CGKeyCode? = nil
) -> Unmanaged<CGEvent> {
    var flags = event.flags
    flags.remove([.maskControl, .maskAlternate, .maskCommand, .maskShift])
    flags.formUnion(modifiers)
    event.flags = flags

    if let keyCode {
        event.setIntegerValueField(.keyboardEventKeycode, value: Int64(keyCode))
    }

    return Unmanaged.passUnretained(event)
}

private func isTerminalApplication() -> Bool {
    guard let bundleID = NSWorkspace.shared.frontmostApplication?.bundleIdentifier else {
        return false
    }
    return terminalBundleIDs.contains(bundleID)
}

private func openFinder() {
    DispatchQueue.main.async {
        NSWorkspace.shared.open(URL(fileURLWithPath: NSHomeDirectory()))
    }
}

private func handleScreenshot(flags: CGEventFlags, type: CGEventType) -> Bool {
    guard type == .keyDown else {
        return true
    }

    switch flags {
    case [.maskAlternate]:
        run("/usr/sbin/screencapture", ["-i", "-W"])
    case [.maskControl]:
        run("/usr/sbin/screencapture", ["-c"])
    case [.maskControl, .maskShift]:
        run("/usr/sbin/screencapture", ["-i", "-c"])
    default:
        return false
    }

    return true
}

private let keyboardCallback: CGEventTapCallBack = { _, type, event, _ in
    if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
        if let eventTap {
            CGEvent.tapEnable(tap: eventTap, enable: true)
        }
        return Unmanaged.passUnretained(event)
    }

    guard type == .keyDown || type == .keyUp else {
        return Unmanaged.passUnretained(event)
    }

    let flags = relevantFlags(event.flags)
    let keyCode = CGKeyCode(event.getIntegerValueField(.keyboardEventKeycode))
    let isKeyDown = type == .keyDown
    let isRepeat = event.getIntegerValueField(.keyboardEventAutorepeat) != 0

    // KVM hotkeys always remain active and pass through to Deskflow.
    if flags == [.maskControl, .maskAlternate] && isKeyDown && !isRepeat {
        if keyCode == CGKeyCode(kVK_ANSI_M) {
            switchInput(to: macInput)
        } else if keyCode == CGKeyCode(kVK_ANSI_U) {
            switchInput(to: linuxInput)
        }
    }

    guard ubuntuShortcutsEnabled && activeRemappingAvailable else {
        return Unmanaged.passUnretained(event)
    }

    // Control shortcuts. Terminal-style applications keep native Control keys.
    if flags == [.maskControl] && !isTerminalApplication() {
        let commandKeys: Set<CGKeyCode> = [
            CGKeyCode(kVK_ANSI_A), CGKeyCode(kVK_ANSI_C), CGKeyCode(kVK_ANSI_F),
            CGKeyCode(kVK_ANSI_L), CGKeyCode(kVK_ANSI_N), CGKeyCode(kVK_ANSI_O),
            CGKeyCode(kVK_ANSI_P), CGKeyCode(kVK_ANSI_S), CGKeyCode(kVK_ANSI_T),
            CGKeyCode(kVK_ANSI_V), CGKeyCode(kVK_ANSI_W), CGKeyCode(kVK_ANSI_X),
            CGKeyCode(kVK_ANSI_Z)
        ]

        if commandKeys.contains(keyCode) {
            return replaceModifiers(on: event, with: [.maskCommand])
        }

        if keyCode == CGKeyCode(kVK_ForwardDelete) {
            return replaceModifiers(on: event, with: [.maskAlternate])
        }
    }

    if flags == [.maskControl, .maskShift] && keyCode == CGKeyCode(kVK_ANSI_Z) && !isTerminalApplication() {
        return replaceModifiers(on: event, with: [.maskCommand, .maskShift])
    }

    // Alt shortcuts.
    if flags == [.maskAlternate] {
        switch Int(keyCode) {
        case kVK_Tab:
            return replaceModifiers(on: event, with: [.maskCommand])
        case kVK_F4:
            return replaceModifiers(on: event, with: [.maskCommand], keyCode: CGKeyCode(kVK_ANSI_W))
        case kVK_F2:
            return replaceModifiers(on: event, with: [.maskCommand], keyCode: CGKeyCode(kVK_Space))
        case kVK_LeftArrow:
            return replaceModifiers(on: event, with: [.maskCommand], keyCode: CGKeyCode(kVK_ANSI_LeftBracket))
        case kVK_RightArrow:
            return replaceModifiers(on: event, with: [.maskCommand], keyCode: CGKeyCode(kVK_ANSI_RightBracket))
        default:
            break
        }
    }

    if flags == [.maskAlternate, .maskShift] && keyCode == CGKeyCode(kVK_Tab) {
        return replaceModifiers(on: event, with: [.maskCommand, .maskShift])
    }

    // Windows-key shortcuts. A Windows key is reported as Command by macOS.
    if flags == [.maskCommand] {
        switch Int(keyCode) {
        case kVK_ANSI_E:
            if isKeyDown && !isRepeat {
                openFinder()
            }
            return nil
        case kVK_ANSI_D:
            return replaceModifiers(on: event, with: [.maskCommand], keyCode: CGKeyCode(kVK_F3))
        case kVK_ANSI_L:
            return replaceModifiers(on: event, with: [.maskControl, .maskCommand], keyCode: CGKeyCode(kVK_ANSI_Q))
        case kVK_Tab:
            return replaceModifiers(on: event, with: [.maskControl], keyCode: CGKeyCode(kVK_UpArrow))
        case kVK_LeftArrow, kVK_RightArrow, kVK_UpArrow, kVK_DownArrow:
            return replaceModifiers(on: event, with: [.maskControl, .maskSecondaryFn])
        default:
            break
        }
    }

    // On standard PC keyboards, Print Screen is exposed to macOS as F13.
    if keyCode == CGKeyCode(kVK_F13) {
        if handleScreenshot(flags: flags, type: type) {
            return nil
        }
        if flags.isEmpty {
            return replaceModifiers(on: event, with: [.maskCommand, .maskShift], keyCode: CGKeyCode(kVK_ANSI_5))
        }
        if flags == [.maskShift] {
            return replaceModifiers(on: event, with: [.maskCommand, .maskShift], keyCode: CGKeyCode(kVK_ANSI_4))
        }
    }

    // Ctrl-Alt-Delete opens Force Quit. Plain Delete already works natively.
    if flags == [.maskControl, .maskAlternate] && keyCode == CGKeyCode(kVK_ForwardDelete) {
        return replaceModifiers(on: event, with: [.maskCommand, .maskAlternate], keyCode: CGKeyCode(kVK_Escape))
    }

    return Unmanaged.passUnretained(event)
}

private func installEventTap() {
    if let source = eventTapSource {
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, .commonModes)
    }
    if let tap = eventTap {
        CFMachPortInvalidate(tap)
    }

    activeRemappingAvailable = AXIsProcessTrusted()
    let options: CGEventTapOptions = activeRemappingAvailable ? .defaultTap : .listenOnly
    let eventMask = (CGEventMask(1) << CGEventType.keyDown.rawValue)
        | (CGEventMask(1) << CGEventType.keyUp.rawValue)

    eventTap = CGEvent.tapCreate(
        tap: .cgSessionEventTap,
        place: .headInsertEventTap,
        options: options,
        eventsOfInterest: eventMask,
        callback: keyboardCallback,
        userInfo: nil
    )

    guard let eventTap else {
        log("Unable to create keyboard event tap; enable Input Monitoring")
        return
    }

    eventTapSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0)
    if let eventTapSource {
        CFRunLoopAddSource(CFRunLoopGetCurrent(), eventTapSource, .commonModes)
    }
    CGEvent.tapEnable(tap: eventTap, enable: true)
    log(activeRemappingAvailable ? "Active shortcut remapping is available" : "Running in listen-only mode")
}

private final class MenuController: NSObject {
    private let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let toggleItem = NSMenuItem(title: "Ubuntu shortcuts", action: #selector(toggleShortcuts), keyEquivalent: "")
    private let inputPermissionItem = NSMenuItem(title: "Enable Input Monitoring", action: #selector(openInputMonitoring), keyEquivalent: "")
    private let permissionItem = NSMenuItem(title: "Enable Accessibility permission", action: #selector(openAccessibility), keyEquivalent: "")

    override init() {
        super.init()

        toggleItem.target = self
        inputPermissionItem.target = self
        permissionItem.target = self

        let menu = NSMenu()
        menu.addItem(toggleItem)
        menu.addItem(inputPermissionItem)
        menu.addItem(permissionItem)
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
        statusItem.button?.toolTip = "Ubuntu keyboard shortcuts"
        refresh()
    }

    func refresh() {
        toggleItem.state = ubuntuShortcutsEnabled ? .on : .off
        statusItem.button?.title = ubuntuShortcutsEnabled ? "UK On" : "UK Off"
        inputPermissionItem.isHidden = CGPreflightListenEventAccess()
        permissionItem.isHidden = activeRemappingAvailable
    }

    @objc private func toggleShortcuts() {
        ubuntuShortcutsEnabled.toggle()
        UserDefaults.standard.set(ubuntuShortcutsEnabled, forKey: preferenceKey)

        if ubuntuShortcutsEnabled && !activeRemappingAvailable {
            requestAccessibilityPermission()
        }

        refresh()
        log("Ubuntu shortcuts \(ubuntuShortcutsEnabled ? "enabled" : "disabled")")
    }

    @objc private func openAccessibility() {
        requestAccessibilityPermission()
    }

    @objc private func openInputMonitoring() {
        requestInputMonitoringPermission()
    }

    @objc private func switchToMac() {
        switchInput(to: macInput)
    }

    @objc private func switchToUbuntu() {
        switchInput(to: linuxInput)
    }

    @objc private func quit() {
        NSApplication.shared.terminate(nil)
    }
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

if !CGPreflightListenEventAccess() {
    _ = CGRequestListenEventAccess()
}

installEventTap()
private let menuController = MenuController()

// Pick up Accessibility approval without requiring a logout or reinstall.
Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { _ in
    let trusted = AXIsProcessTrusted()
    if trusted != activeRemappingAvailable || (eventTap == nil && CGPreflightListenEventAccess()) {
        installEventTap()
        menuController.refresh()
    }
}

NSWorkspace.shared.openApplication(
    at: URL(fileURLWithPath: "/Applications/BetterDisplay.app"),
    configuration: NSWorkspace.OpenConfiguration()
)

log("Menu-bar helper started; Ubuntu shortcuts are \(ubuntuShortcutsEnabled ? "on" : "off")")
app.setActivationPolicy(.accessory)
app.run()
