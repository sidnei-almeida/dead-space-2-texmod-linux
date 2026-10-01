#!/usr/bin/env bash
# Compila dist/DS2TexInject.asi (DLL Windows 32-bit).
# Usa i686-w64-mingw32-g++ (pacman -S mingw-w64-gcc) ou llvm-mingw em ~/.local/opt.
set -euo pipefail
cd "$(dirname "$0")"

CXX=""
if command -v i686-w64-mingw32-g++ >/dev/null; then
    CXX=i686-w64-mingw32-g++
    EXTRA=(-static-libgcc -static-libstdc++)
else
    LLVM_MINGW=$(ls -d "$HOME"/.local/opt/llvm-mingw-*/ 2>/dev/null | tail -1 || true)
    if [ -z "$LLVM_MINGW" ]; then
        echo "Nenhum compilador mingw encontrado. Baixando llvm-mingw para ~/.local/opt (sem sudo)..."
        TAG=$(curl -fsSL https://api.github.com/repos/mstorsjo/llvm-mingw/releases/latest | grep -m1 '"tag_name"' | cut -d'"' -f4)
        mkdir -p "$HOME/.local/opt"
        curl -fL "https://github.com/mstorsjo/llvm-mingw/releases/download/$TAG/llvm-mingw-$TAG-ucrt-ubuntu-22.04-x86_64.tar.xz" \
            | tar xJ -C "$HOME/.local/opt"
        LLVM_MINGW=$(ls -d "$HOME"/.local/opt/llvm-mingw-*/ | tail -1)
    fi
    CXX="$LLVM_MINGW/bin/i686-w64-mingw32-clang++"
    EXTRA=(-static)
fi

mkdir -p dist
"$CXX" -O2 -std=c++17 -shared -s -Wall -Wno-unused-function "${EXTRA[@]}" \
    -o dist/DS2TexInject.asi src/ds2texinject.cpp -luuid
echo "ok -> dist/DS2TexInject.asi"
