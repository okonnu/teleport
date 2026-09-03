#!/bin/zsh
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
version="${1:-1.2.1}"
dist_dir="$repo_dir/dist"
app_dir="$dist_dir/Teleport.app"
contents_dir="$app_dir/Contents"
binary_dir="$contents_dir/MacOS"
resources_dir="$contents_dir/Resources"
build_dir="$(mktemp -d /tmp/teleport-build.XXXXXX)"

cleanup() {
    rm -rf "$build_dir"
}
trap cleanup EXIT

rm -rf "$dist_dir"
mkdir -p "$binary_dir" "$resources_dir"

sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
frameworks=(-framework AppKit -framework ApplicationServices -framework Carbon)

swiftc -O -target arm64-apple-macosx13.0 -sdk "$sdk_path" \
    "${frameworks[@]}" "$repo_dir/Teleport.swift" \
    -o "$build_dir/Teleport-arm64"

swiftc -O -target x86_64-apple-macosx13.0 -sdk "$sdk_path" \
    "${frameworks[@]}" "$repo_dir/Teleport.swift" \
    -o "$build_dir/Teleport-x86_64"

lipo -create \
    "$build_dir/Teleport-arm64" \
    "$build_dir/Teleport-x86_64" \
    -output "$binary_dir/Teleport"

iconset_dir="$build_dir/Teleport.iconset"
mkdir -p "$iconset_dir"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$repo_dir/assets/Teleport-icon.png" \
        --out "$iconset_dir/icon_${size}x${size}.png" >/dev/null
    double_size=$((size * 2))
    sips -z "$double_size" "$double_size" "$repo_dir/assets/Teleport-icon.png" \
        --out "$iconset_dir/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset_dir" -o "$resources_dir/Teleport.icns"

cp "$repo_dir/packaging/Info.plist" "$contents_dir/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $version" "$contents_dir/Info.plist"
chmod 755 "$binary_dir/Teleport"
codesign --force --deep --sign - --identifier com.okonnu.Teleport "$app_dir"

archive="$dist_dir/Teleport-macOS.zip"
ditto -c -k --sequesterRsrc --keepParent "$app_dir" "$archive"
(cd "$dist_dir" && shasum -a 256 "$(basename "$archive")" > "$(basename "$archive").sha256")

codesign --verify --deep --strict --verbose=2 "$app_dir"
lipo -info "$binary_dir/Teleport"
echo "Built $archive"
