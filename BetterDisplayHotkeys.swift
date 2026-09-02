import AppKit
import Carbon.HIToolbox

private let betterDisplayCLI = "/opt/homebrew/bin/betterdisplaycli"
private let displayName = "LC49G95T"
private let macInput = 1
private let linuxInput = 16
private let app = NSApplication.shared
private var eventTap: CFMachPort?

private func log(_ message: String) {
    fputs("\(Date()): \(message)\n", stderr)
    fflush(stderr)
}

private func switchInput(to value: Int) {
    DispatchQueue.global(qos: .userInitiated).async {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: betterDisplayCLI)
        task.arguments = [
            "set",
            "-namelike=\(displayName)",
            "-ddc=\(value)",
            "-vcp=inputSelect"
        ]
        task.standardOutput = FileHandle.nullDevice
        task.standardError = FileHandle.nullDevice

        do {
            try task.run()
            task.waitUntilExit()
            log("Input \(value), exit status \(task.terminationStatus)")
        } catch {
            log("Could not run BetterDisplay CLI: \(error)")
        }
    }
}

private let keyboardCallback: CGEventTapCallBack = { _, type, event, _ in
    if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
        if let eventTap {
            CGEvent.tapEnable(tap: eventTap, enable: true)
        }
        return Unmanaged.passUnretained(event)
    }

    guard type == .keyDown else {
        return Unmanaged.passUnretained(event)
    }

    let flags = event.flags
    let hasRequiredModifiers = flags.contains(.maskControl) && flags.contains(.maskAlternate)
    let hasExtraModifiers = flags.contains(.maskShift) || flags.contains(.maskCommand)
    let isRepeat = event.getIntegerValueField(.keyboardEventAutorepeat) != 0

    guard hasRequiredModifiers, !hasExtraModifiers, !isRepeat else {
        return Unmanaged.passUnretained(event)
    }

    let keyCode = UInt32(event.getIntegerValueField(.keyboardEventKeycode))
    if keyCode == UInt32(kVK_ANSI_M) {
        switchInput(to: macInput)
    } else if keyCode == UInt32(kVK_ANSI_U) {
        switchInput(to: linuxInput)
    }

    // Deskflow must receive the same event, so never consume it.
    return Unmanaged.passUnretained(event)
}

if !CGPreflightListenEventAccess() {
    _ = CGRequestListenEventAccess()
}

let eventMask = CGEventMask(1) << CGEventType.keyDown.rawValue
eventTap = CGEvent.tapCreate(
    tap: .cgSessionEventTap,
    place: .headInsertEventTap,
    options: .listenOnly,
    eventsOfInterest: eventMask,
    callback: keyboardCallback,
    userInfo: nil
)

guard let eventTap else {
    log("Enable Input Monitoring for BetterDisplayHotkeys")
    exit(2)
}

let source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0)
CFRunLoopAddSource(CFRunLoopGetCurrent(), source, .commonModes)
CGEvent.tapEnable(tap: eventTap, enable: true)

NSWorkspace.shared.openApplication(
    at: URL(fileURLWithPath: "/Applications/BetterDisplay.app"),
    configuration: NSWorkspace.OpenConfiguration()
)

log("Listening for Control-Option-M and Control-Option-U")
app.setActivationPolicy(.accessory)
app.run()
