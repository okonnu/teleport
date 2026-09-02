# BetterDisplay Deskflow KVM

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

Then install the latest BetterDisplayHotkeys release:

```bash
curl -fsSL https://raw.githubusercontent.com/okonnu/betterdisplay-deskflow-kvm/main/install.sh | zsh
```

The installer downloads the universal macOS app to `~/Applications`, starts it
at login, reveals it in Finder, and opens Input Monitoring. Add
`BetterDisplayHotkeys.app` to Input Monitoring and turn it on.

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

Control shortcuts remain native in Terminal, iTerm, Warp, Kitty, Alacritty,
WezTerm, and VS Code.

The keyboard layer needs Accessibility permission. Turn it on from the menu and
choose `Enable Accessibility permission` when prompted.

## Build a release

Command Line Tools for Xcode are required.

```bash
./scripts/build-release.sh 1.1.0
```

The universal Apple Silicon and Intel app and its checksum are written to
`dist`. Pushing a version tag runs the GitHub release workflow.

## Monitor input values

The original Samsung LC49G95T uses:

- HDMI: 1
- DisplayPort 1: 15
- DisplayPort 2: 16

Edit the constants at the top of `BetterDisplayHotkeys.swift` before building if
your monitor or connections differ.

## Privacy

Input Monitoring lets the helper see keyboard events. Accessibility lets the
optional keyboard layer translate selected shortcuts. The source does not save
keystrokes or contain networking code.

## License

MIT
