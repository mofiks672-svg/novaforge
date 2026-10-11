#!/usr/bin/env sh
# NovaForge installer: builds from source and installs `nova` into $PREFIX/bin
set -e
PREFIX="${PREFIX:-$HOME/.local}"
cd "$(dirname "$0")/.."
if [ -x ./nova ]; then BIN=./nova; else
  command -v g++ >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1 || { echo "Нужен компилятор C++17 (g++ или clang++)"; exit 1; }
  make; BIN=build/nova
fi
command -v curl >/dev/null 2>&1 || echo "warning: curl не найден — он нужен для обращения к API моделей"
mkdir -p "$PREFIX/bin"
install -m 755 "$BIN" "$PREFIX/bin/nova"
echo "NovaForge установлен в $PREFIX/bin/nova"
case ":$PATH:" in *":$PREFIX/bin:"*) ;; *) echo "  Добавьте в PATH: export PATH=\"$PREFIX/bin:\$PATH\"";; esac
echo "  Начало работы: nova connect — подключить API, затем nova"
