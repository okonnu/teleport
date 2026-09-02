#!/bin/zsh
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
version="${1:-1.1.0}"
dist_dir="$repo_dir/dist"
app_dir="$dist_dir/BetterDisplayHotkeys.app"
contents_dir="$app_dir/Contents"
binary_dir="$contents_dir/MacOS"
build_dir="$(mktemp -d /tmp/betterdisplay-hotkeys-build.XXXXXX)"

cleanup() {
    rm -rf "$build_dir"
}
trap cleanup EXIT

rm -rf "$dist_dir"
mkdir -p "$binary_dir"

sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
frameworks=(-framework AppKit -framework ApplicationServices -framework Carbon)

swiftc -O -target arm64-apple-macosx13.0 -sdk "$sdk_path" \
    "${frameworks[@]}" "$repo_dir/BetterDisplayHotkeys.swift" \
    -o "$build_dir/BetterDisplayHotkeys-arm64"

swiftc -O -target x86_64-apple-macosx13.0 -sdk "$sdk_path" \
    "${frameworks[@]}" "$repo_dir/BetterDisplayHotkeys.swift" \
    -o "$build_dir/BetterDisplayHotkeys-x86_64"

lipo -create \
    "$build_dir/BetterDisplayHotkeys-arm64" \
    "$build_dir/BetterDisplayHotkeys-x86_64" \
    -output "$binary_dir/BetterDisplayHotkeys"

cp "$repo_dir/packaging/Info.plist" "$contents_dir/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $version" "$contents_dir/Info.plist"
chmod 755 "$binary_dir/BetterDisplayHotkeys"
codesign --force --deep --sign - --identifier com.codex.BetterDisplayHotkeys "$app_dir"

archive="$dist_dir/BetterDisplayHotkeys-macOS.zip"
ditto -c -k --sequesterRsrc --keepParent "$app_dir" "$archive"
(cd "$dist_dir" && shasum -a 256 "$(basename "$archive")" > "$(basename "$archive").sha256")

codesign --verify --deep --strict --verbose=2 "$app_dir"
lipo -info "$binary_dir/BetterDisplayHotkeys"
echo "Built $archive"
