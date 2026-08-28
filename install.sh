#!/bin/sh
set -e

REPO="https://github.com/ChelpanovIlya/fixclip.git"
PREFIX="${PREFIX:-/usr/local}"
TMPDIR=$(mktemp -d)

cleanup() { rm -rf "$TMPDIR"; }
trap cleanup EXIT

need() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "error: '$1' not found. Install it first." >&2
        exit 1
    }
}

check_lib() {
    echo "int main(){return 0;}" | "$CC" -xc -o /dev/null - "$@" 2>/dev/null || {
        echo "error: missing libraries. Install dev packages:" >&2
        echo "" >&2
        echo "  Debian/Ubuntu:  sudo apt install libx11-dev libxfixes-dev" >&2
        echo "  Fedora:         sudo dnf install libX11-devel libXfixes-devel" >&2
        echo "  Arch:           sudo pacman -S libx11 libxfixes" >&2
        exit 1
    }
}

need git
need gcc
need make

CC="${CC:-gcc}"
check_lib -lX11 -lXfixes

echo "Cloning fixclip..."
git clone --depth=1 "$REPO" "$TMPDIR/fixclip"

echo "Building..."
make -C "$TMPDIR/fixclip" CC="$CC"

echo "Installing to $PREFIX/bin/ ..."
make -C "$TMPDIR/fixclip" install PREFIX="$PREFIX"

echo ""
echo "Done. Run: fixclip -f"
