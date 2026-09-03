#!/bin/zsh
set -euo pipefail

repo="okonnu/teleport"
asset="Teleport-macOS.zip"
download_url="https://github.com/$repo/releases/latest/download/$asset"
install_dir="$HOME/Applications"
app_path="$install_dir/Teleport.app"
backup_path="$install_dir/Teleport.previous.app"
agent_path="$HOME/Library/LaunchAgents/com.okonnu.teleport.plist"
log_path="$HOME/Library/Logs/Teleport.log"
service="com.okonnu.teleport"
old_service="com.codex.betterdisplay-hotkeys"
old_agent="$HOME/Library/LaunchAgents/$old_service.plist"
old_app="$install_dir/BetterDisplayHotkeys.app"
temp_dir="$(mktemp -d /tmp/teleport-install.XXXXXX)"

cleanup() {
    rm -rf "$temp_dir"
}
trap cleanup EXIT

echo "Downloading Teleport..."
curl --fail --location --retry 3 "$download_url" -o "$temp_dir/$asset"
ditto -x -k "$temp_dir/$asset" "$temp_dir/unpacked"

source_app="$temp_dir/unpacked/Teleport.app"
if [[ ! -x "$source_app/Contents/MacOS/Teleport" ]]; then
    echo "The downloaded archive does not contain a valid app." >&2
    exit 1
fi

mkdir -p "$install_dir" "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
launchctl bootout "gui/$(id -u)/$service" 2>/dev/null || true
launchctl bootout "gui/$(id -u)/$old_service" 2>/dev/null || true

rm -f "$old_agent" "$old_agent.disabled"
rm -rf \
    "$old_app" \
    "$install_dir/BetterDisplayHotkeys.previous.app" \
    "$install_dir/BetterDisplayHotkeys.retired.app" \
    "$HOME/Library/Application Support/BetterDisplayHotkeys"
rm -f "$HOME/Library/Logs/BetterDisplayHotkeys.log"

rm -rf "$backup_path"
if [[ -d "$app_path" ]]; then
    mv "$app_path" "$backup_path"
fi
ditto "$source_app" "$app_path"

plutil -create xml1 "$agent_path"
plutil -insert Label -string "$service" "$agent_path"
plutil -insert ProgramArguments -json "[\"$app_path/Contents/MacOS/Teleport\"]" "$agent_path"
plutil -insert RunAtLoad -bool true "$agent_path"
plutil -insert KeepAlive -bool true "$agent_path"
plutil -insert ProcessType -string Interactive "$agent_path"
plutil -insert StandardOutPath -string "$log_path" "$agent_path"
plutil -insert StandardErrorPath -string "$log_path" "$agent_path"

launchctl bootstrap "gui/$(id -u)" "$agent_path"
open -R "$app_path"
open 'x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent'

echo
echo "Installed $app_path"
echo "Add Teleport.app to Input Monitoring and enable it."
echo "Use the UK Off menu-bar item to enable Ubuntu shortcuts."
echo "The previous Teleport app, when present, is kept at $backup_path"
