#!/usr/bin/env bash
# Build nova.exe and the Windows installer on Linux (no Windows machine needed):
#   llvm-mingw cross-compiler → nova.exe, Inno Setup under a portable Wine → NovaForge-Setup-<ver>-x64.exe
# Tools are downloaded once into $NOVA_TOOLS (default: ~/.cache/novaforge-tools).
#   scripts/build-windows-cross.sh            build everything
#   SKIP_EXE=1 scripts/build-windows-cross.sh  only the installer (reuses build-win/nova.exe)
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
VER=$(grep -o 'VERSION = "[0-9.]*"' src/config.hpp | grep -o '[0-9.]*')
T=${NOVA_TOOLS:-$HOME/.cache/novaforge-tools}; mkdir -p "$T"

LLVM_MINGW=${LLVM_MINGW:-llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64}
WINE=${WINE:-wine-11.19-amd64-wow64}
INNO=${INNO:-6.7.3}

fetch() { [ -e "$T/$2" ] || { echo "↓ $1"; curl -fL# -o "$T/$2.part" "$1" && mv "$T/$2.part" "$T/$2"; }; }
if [ ! -d "$T/$LLVM_MINGW" ]; then
  fetch "https://github.com/mstorsjo/llvm-mingw/releases/download/$(echo $LLVM_MINGW | cut -d- -f3)/$LLVM_MINGW.tar.xz" llvm.tar.xz
  tar xf "$T/llvm.tar.xz" -C "$T"
fi
if [ ! -d "$T/$WINE" ]; then
  fetch "https://github.com/Kron4ek/Wine-Builds/releases/download/$(echo $WINE | cut -d- -f2)/$WINE.tar.xz" wine.tar.xz
  tar xf "$T/wine.tar.xz" -C "$T"
fi
export PATH="$T/$WINE/bin:$T/$LLVM_MINGW/bin:$PATH"
export WINEPREFIX="$T/wineprefix" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" LANG=${LANG:-C.UTF-8}
# Wine needs an X display to install Inno Setup; start a virtual one if there is none
if [ -z "${DISPLAY:-}" ] && command -v Xvfb >/dev/null; then Xvfb :97 -screen 0 1024x768x24 >/dev/null 2>&1 & export DISPLAY=:97; sleep 1; fi
ISCC="$WINEPREFIX/drive_c/InnoSetup/ISCC.exe"
if [ ! -f "$ISCC" ]; then
  fetch "https://github.com/jrsoftware/issrc/releases/download/is-${INNO//./_}/innosetup-$INNO.exe" innosetup.exe
  wineboot -i >/dev/null 2>&1 || true
  wine "$T/innosetup.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER '/DIR=C:\InnoSetup' >/dev/null 2>&1
  [ -f "$ISCC" ] || { echo "Inno Setup не установился (нужен Xvfb или DISPLAY)"; exit 1; }
fi

mkdir -p build-win
if [ -z "${SKIP_EXE:-}" ]; then
  echo "▸ nova.exe $VER"
  x86_64-w64-mingw32-windres res/nova.rc -O coff -o build-win/nova.res.o
  x86_64-w64-mingw32-clang++ -std=c++17 -O2 -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -Wno-unknown-pragmas \
    src/main.cpp build-win/nova.res.o -o build-win/nova.exe -static -s -lws2_32 -lshell32 -lole32 -luuid -ladvapi32
fi
wine build-win/nova.exe --version | tr -d '\r'

echo "▸ installer"
( cd installer/windows && wine "$ISCC" /Q "/DAppVersion=$VER" "/DNovaExe=$(winepath -w "$ROOT/build-win/nova.exe")" novaforge.iss )
SETUP="dist/NovaForge-Setup-$VER-x64.exe"
( cd dist && sha256sum "$(basename "$SETUP")" > "$(basename "$SETUP").sha256" )
echo "✓ $SETUP"
