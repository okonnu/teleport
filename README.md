# Teleport

![Teleport](assets/Teleport-logo.png)

Switch a monitor, keyboard, and mouse between macOS and Ubuntu with one shortcut.
It also adds an optional Ubuntu-style keyboard layer to macOS.

This build is configured for:

- Samsung Odyssey G9 LC49G95T
- Mac on HDMI
- Ubuntu on DisplayPort 2
- Mac running as the Deskflow server

## Install

Install BetterDisplay and Deskflow first:

```bash
brew install --cask betterdisplay
brew install deskflow
```

Then install the latest Teleport release:

```bash
curl -fsSL https://raw.githubusercontent.com/okonnu/teleport/main/install.sh | zsh
```

The installer downloads the universal macOS app to `~/Applications`, starts it
at login, reveals it in Finder, and opens Input Monitoring. Add
`Teleport.app` to Input Monitoring and turn it on.

The app is locally signed but not Apple-notarized. If you download the zip in a
browser, right-click the app and choose Open the first time.

## KVM shortcuts

- Control-Option-M switches the display to the Mac on HDMI.
- Control-Option-U switches the display to Ubuntu on DisplayPort 2.

These shortcuts remain active when the Ubuntu keyboard layer is off.

## Ubuntu keyboard layer

Use the `UK On` or `UK Off` item in the macOS menu bar. The layer is off by
default and remembers your choice.

The layer provides common Control shortcuts for copy, paste, cut, undo, redo,
select all, find, save, print, open, new, tabs, and the address bar. It also maps:

- Alt-Tab and Alt-Shift-Tab to application switching
- Alt-F4 to close window
- Alt-F2 to Spotlight
- Windows-E to Finder
- Windows-D to show desktop
- Windows-L to lock
- Windows-Tab to Mission Control
- Windows-arrow keys to window tiling
- Print Screen combinations to macOS screenshots
- Control-Alt-Delete to Force Quit
- Control-Alt-T to Terminal

Control shortcuts remain native in Terminal, iTerm, Warp, Kitty, Alacritty,
WezTerm, and VS Code.

The keyboard layer needs Accessibility permission. Turn it on from the menu and
choose `Enable Accessibility permission` when prompted.

## Edit shortcuts without rebuilding

Choose `Edit shortcuts JSON` from the Teleport menu. The configuration lives at:

```text
~/Library/Application Support/Teleport/shortcuts.json
```

Teleport reloads valid changes automatically. Normal rules map a shortcut to a
named macOS action:

```json
{
  "from": "ctrl+alt+t",
  "action": "openTerminal"
}
```

Available action constants include `copy`, `paste`, `cut`, `undo`, `redo`,
`selectAll`, `find`, `save`, `print`, `new`, `open`, `newTab`, `closeWindow`,
`focusLocation`, `deleteNextWord`, `nextApplication`, `previousApplication`,
`spotlight`, `back`, `forward`, `openFinder`, `openTerminal`, `showDesktop`,
`lockScreen`, `missionControl`, `tileLeft`, `tileRight`, `tileUp`, `tileDown`,
`screenshotControls`, `captureArea`, `captureWindow`, `copyScreen`, `copyArea`,
and `forceQuit`.

Advanced rules may use `to` with a raw target chord instead of `action`.
Modifier names are `ctrl`, `alt`, `shift`, `win`, `cmd`, and `fn`. The optional
scopes are `all`, `nonTerminal`, and `terminalOnly`.

If an edit is invalid, Teleport keeps the last valid configuration and reports
`Configuration error` in its menu. Fix the JSON and save again, or choose
`Reload shortcuts`.

## Build a release

Command Line Tools for Xcode are required.

```bash
./scripts/build-release.sh 1.3.0
./dist/Teleport.app/Contents/MacOS/Teleport --self-test
```

The universal Apple Silicon and Intel app and its checksum are written to
`dist`. Upload those two files to a GitHub release.

## Monitor input values

The original Samsung LC49G95T uses:

- HDMI: 1
- DisplayPort 1: 15
- DisplayPort 2: 16

Edit the constants at the top of `Teleport.swift` before building if
your monitor or connections differ.

## Privacy

Input Monitoring lets the helper see keyboard events. Accessibility lets the
optional keyboard layer translate selected shortcuts. The source does not save
keystrokes or contain networking code.

## License

MIT
