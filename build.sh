#!/usr/bin/env bash
# Compila dist/DS2TexInject.asi (DLL Windows 32-bit) e dist/DS2TexInject-Setup.exe (instalador
# para Windows, 64-bit, com o .asi e o .ini embutidos).
# Usa mingw-w64-gcc (pacman -S mingw-w64-gcc) ou llvm-mingw em ~/.local/opt.
set -euo pipefail
cd "$(dirname "$0")"

if command -v i686-w64-mingw32-g++ >/dev/null && command -v x86_64-w64-mingw32-g++ >/dev/null \
    && command -v x86_64-w64-mingw32-windres >/dev/null; then
    CXX32=i686-w64-mingw32-g++
    CXX64=x86_64-w64-mingw32-g++
    WINDRES=x86_64-w64-mingw32-windres
    EXTRA=(-static-libgcc -static-libstdc++)
    EXTRA64=(-static)
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
    CXX32="$LLVM_MINGW/bin/i686-w64-mingw32-clang++"
    CXX64="$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++"
    WINDRES="$LLVM_MINGW/bin/x86_64-w64-mingw32-windres"
    EXTRA=(-static)
    EXTRA64=(-static)
fi

mkdir -p dist
"$CXX32" -O2 -std=c++17 -shared -s -Wall -Wno-unused-function "${EXTRA[@]}" \
    -o dist/DS2TexInject.asi src/ds2texinject.cpp -luuid
echo "ok -> dist/DS2TexInject.asi"

"$WINDRES" -I. -O coff -o dist/ds2setup.res src/ds2setup.rc
"$CXX64" -O2 -std=c++17 -municode -s -Wall -Wno-unused-function "${EXTRA64[@]}" \
    -o dist/DS2TexInject-Setup.exe src/ds2setup.cpp dist/ds2setup.res -lcomdlg32 -lurlmon -lshell32 -ladvapi32
rm -f dist/ds2setup.res
echo "ok -> dist/DS2TexInject-Setup.exe"
