#!/bin/sh
# get.sh - download the latest goonstein build into ./goonstein.
#
#   curl -fsSL https://raw.githubusercontent.com/Yugendren/goonstein/main/get.sh | sh
#   ./goonstein/goonstein
#
# Re-running updates in place: the binary and assets/ are replaced, anything else you left in
# ./goonstein (logs, screenshots, saves) is kept. No GitHub account and no build toolchain needed.
set -eu

REPO="Yugendren/goonstein"
TAG="latest"
DEST="goonstein"

die() { echo "get.sh: $*" >&2; exit 1; }

# ---------------------------------------------------------------- which build
os="$(uname -s)"
arch="$(uname -m)"
case "$os" in
    Darwin)
        case "$arch" in
            arm64|aarch64) asset="goonstein-macos-arm64.zip" ;;
            *) die "only Apple Silicon Macs are built (this is $arch); build from source: see README.md" ;;
        esac
        ;;
    Linux)
        case "$arch" in
            x86_64|amd64) asset="goonstein-linux-x86_64.zip" ;;
            *) die "only x86-64 Linux is built (this is $arch); build from source: see README.md" ;;
        esac
        ;;
    *)
        die "unsupported OS '$os' — on Windows use get.ps1"
        ;;
esac

command -v curl >/dev/null 2>&1 || die "curl not found"

# bsdtar reads zips too, which covers the odd container image without unzip.
if command -v unzip >/dev/null 2>&1; then
    extract() { unzip -q "$1" -d "$2"; }
elif command -v bsdtar >/dev/null 2>&1; then
    extract() { bsdtar -xf "$1" -C "$2"; }
else
    die "neither unzip nor bsdtar found; install unzip"
fi

url="https://github.com/$REPO/releases/download/$TAG/$asset"

# ---------------------------------------------------------------- download and unpack
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM

echo "get.sh: downloading $asset"
curl -fL --retry 3 --progress-bar -o "$tmp/$asset" "$url" \
    || die "download failed: $url"

mkdir -p "$tmp/x"
extract "$tmp/$asset" "$tmp/x"
[ -f "$tmp/x/goonstein/goonstein" ] || die "unexpected archive layout in $asset"

# Replace only what we ship, so a re-run is an update rather than a wipe.
mkdir -p "$DEST"
rm -rf "$DEST/assets" "$DEST/goonstein"
cp -R "$tmp/x/goonstein/." "$DEST/"
chmod +x "$DEST/goonstein"

# The build is not signed or notarized, so Gatekeeper would refuse a downloaded binary.
if [ "$os" = "Darwin" ] && command -v xattr >/dev/null 2>&1; then
    xattr -dr com.apple.quarantine "$DEST" 2>/dev/null || true
fi

echo "get.sh: installed into ./$DEST"
echo "get.sh: run it with   ./$DEST/goonstein"
