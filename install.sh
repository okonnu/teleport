#!/bin/zsh
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
install_dir="$HOME/Library/Application Support/BetterDisplayHotkeys"
binary_path="$install_dir/BetterDisplayHotkeys"
agent_path="$HOME/Library/LaunchAgents/com.example.betterdisplay-hotkeys.plist"
log_path="$HOME/Library/Logs/BetterDisplayHotkeys.log"

mkdir -p "$install_dir" "$HOME/Library/LaunchAgents"

swiftc -O \
  -framework AppKit \
  -framework Carbon \
  "$script_dir/BetterDisplayHotkeys.swift" \
  -o "$binary_path"

codesign --force --sign - "$binary_path"

cat > "$agent_path" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.example.betterdisplay-hotkeys</string>
    <key>ProgramArguments</key>
    <array>
        <string>$binary_path</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>ProcessType</key>
    <string>Interactive</string>
    <key>StandardOutPath</key>
    <string>$log_path</string>
    <key>StandardErrorPath</key>
    <string>$log_path</string>
</dict>
</plist>
PLIST

launchctl bootout "gui/$(id -u)" "$agent_path" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$agent_path"

open 'x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent'

echo "Installed: $binary_path"
echo "Add that file under Privacy and Security, Input Monitoring."
echo "Then run: launchctl kickstart -k gui/$(id -u)/com.example.betterdisplay-hotkeys"
