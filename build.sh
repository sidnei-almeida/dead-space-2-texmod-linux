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
    # -static (e nao so -static-libgcc -static-libstdc++): o thread_local do plugin usa o emutls do
    # GCC, que no modelo de threads posix chama pthread_mutex_*/pthread_once do libwinpthread-1.dll.
    # Essa DLL nao existe no Windows nem no Proton e o ASI loader recusa o plugin (bug da v1.4.1).
    EXTRA=(-static)
    EXTRA64=(-static)
else
    LLVM_MINGW=$(ls -d "$HOME"/.local/opt/llvm-mingw-*/ 2>/dev/null | tail -1 || true)
    if [ -z "$LLVM_MINGW" ]; then
        echo "Nenhum compilador mingw encontrado. Baixando llvm-mingw para ~/.local/opt (sem sudo)..."
        # A versao vem do redirect de releases/latest. Nao usar "curl | grep -m1": o grep fecha o
        # pipe cedo, o curl falha (erro 23) e o pipefail derruba o script sem mensagem.
        TAG=$(curl -fsSLI -o /dev/null -w '%{url_effective}' https://github.com/mstorsjo/llvm-mingw/releases/latest)
        TAG=${TAG##*/}
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

# Build reprodutivel: sem a data no cabecalho do executavel, o mesmo codigo gera sempre os mesmos
# bytes, e da para conferir pelo sha256 que o arquivo do release e o que passou no test.sh.
EXTRA+=(-Wl,--no-insert-timestamp)
EXTRA64+=(-Wl,--no-insert-timestamp)

mkdir -p dist
# sem sobras de um build anterior: se a compilacao falhar no meio, nao fica binario velho em dist/
rm -f dist/DS2TexInject.asi dist/DS2TexInject-Setup.exe
"$CXX32" -O2 -std=c++17 -shared -s -Wall -Wno-unused-function "${EXTRA[@]}" \
    -o dist/DS2TexInject.asi src/ds2texinject.cpp -luuid

# Na pasta do jogo so existem as DLLs do sistema. Qualquer outra dependencia (libwinpthread-1.dll,
# libgcc_s_dw2-1.dll, libstdc++-6.dll...) faz o LoadLibrary falhar (erro 126) e o plugin nao carrega.
# As api-ms-win-crt-* sao o Universal CRT: vem com o Windows 10/11 (no 7/8.1 so com a KB2999226).
# Se algo falhar, apaga os binarios (o Setup.exe embute o .asi) para um arquivo reprovado nunca
# chegar a um release.
fail_build() {
    echo "ERRO: $*" >&2
    rm -f dist/DS2TexInject.asi dist/DS2TexInject-Setup.exe
    exit 1
}
check_deps() {
    local out deps
    out=$("$1" -p "$2") || fail_build "o objdump ($1) falhou em $2"
    # KERNEL32 sempre aparece; sem ele a saida nao e a lista de imports esperada e a checagem nao vale
    grep -qiE '^[[:space:]]*DLL Name: kernel32[.]dll$' <<<"$out" ||
        fail_build "lista de DLLs de $2 nao encontrada na saida do $1"
    deps=$(sed -n 's/^[[:space:]]*DLL Name: //p' <<<"$out" |
        grep -viE '^(kernel32|user32|advapi32|shell32|comdlg32|urlmon|ole32|msvcrt|ucrtbase|api-ms-win-[a-z0-9-]+)[.]dll$' || true)
    [ -z "$deps" ] || fail_build "$2 depende de DLLs que nao existem no Windows:"$'\n'"$deps"
}
check_deps "${CXX32%-*}-objdump" dist/DS2TexInject.asi
echo "ok -> dist/DS2TexInject.asi"

"$WINDRES" -I. -O coff -o dist/ds2setup.res src/ds2setup.rc
"$CXX64" -O2 -std=c++17 -municode -s -Wall -Wno-unused-function "${EXTRA64[@]}" \
    -o dist/DS2TexInject-Setup.exe src/ds2setup.cpp dist/ds2setup.res -lcomdlg32 -lurlmon -lshell32 -ladvapi32
rm -f dist/ds2setup.res
check_deps "${CXX64%-*}-objdump" dist/DS2TexInject-Setup.exe
echo "ok -> dist/DS2TexInject-Setup.exe"
