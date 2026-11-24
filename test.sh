#!/usr/bin/env bash
# Testes obrigatorios antes de um release, nos dois caminhos (Linux e Windows).
# Uso: ./test.sh [pacote.tpf ...]   (compila antes, para o teste valer para o codigo atual)
#
# 1. Plugin no Windows: dist/DS2TexInject.asi carrega num Wine limpo (sem DLLs que o Windows nao tem)
#    e funciona com um d3d9.dll falso que imita o d3d9 nativo do Windows, onde texturas, superficies
#    e devices de tipos diferentes sao classes diferentes (DXVK e Wine usam uma so). Cobre device de
#    overlay antes e depois do jogo, threads em paralelo, escrita pela superficie, UpdateTexture,
#    UpdateSurface e Reset, com Pool=managed, Pool=default e pasta do jogo sem permissao de escrita.
# 2. Instaladores: DS2TexInject-Setup.exe (Windows, no Wine) e install.sh + ds2tex.py (Linux)
#    instalam numa pasta de jogo falsa com os mesmos pacotes sinteticos (test/make_packs.py). O
#    plugin instalado tem que ser o de dist/ e os dois texmod/_cache tem que bater.
# 3. Opcional: os pacotes .tpf/.zip passados como argumento passam pelos dois instaladores e sao
#    comparados do mesmo jeito.
#
# O plugin no jogo de verdade (Proton/DXVK) continua sendo testado jogando. Precisa de: wine,
# mingw 32-bit (o mesmo do build.sh), python3 com Pillow e unzip.
set -euo pipefail
cd "$(dirname "$0")"

fail=0
ok() { printf '  ok     %s\n' "$*"; }
bad() { printf '  FALHOU %s\n' "$*"; fail=1; }

command -v wine >/dev/null && command -v winepath >/dev/null || { echo "Precisa do wine."; exit 1; }
command -v unzip >/dev/null || { echo "Precisa do unzip."; exit 1; }
python3 -I -c 'import PIL' 2>/dev/null || { echo "Precisa do Pillow (sudo pacman -S python-pillow)."; exit 1; }
CC32=$(command -v i686-w64-mingw32-gcc || ls "$HOME"/.local/opt/llvm-mingw-*/bin/i686-w64-mingw32-clang 2>/dev/null | tail -1 || true)
[ -n "$CC32" ] || { echo "Precisa de um compilador mingw 32-bit (o ./build.sh baixa um)."; exit 1; }
for p in "$@"; do [ -f "$p" ] || { echo "Pacote nao encontrado: $p"; exit 1; }; done

# Testa o que sai do codigo atual, nunca um binario velho de dist/
./build.sh >/dev/null || { echo "O build falhou."; exit 1; }

W=$(mktemp -d)
trap 'chmod -R u+w "$W" 2>/dev/null; rm -rf "$W"' EXIT

# Prefixo Wine so dos testes, limpo: sem as DLLs extras do Proton ou de outros programas, que
# esconderiam uma dependencia que o Windows nao tem (o bug da v1.4.1).
export WINEPREFIX="${DS2_TEST_WINEPREFIX:-${XDG_CACHE_HOME:-$HOME/.cache}/ds2texinject-test/wine}"
export WINEDEBUG=-all
if [ ! -d "$WINEPREFIX/drive_c" ]; then
    echo "Criando o prefixo Wine de teste em $WINEPREFIX..."
    mkdir -p "$WINEPREFIX"
    WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -i >/dev/null 2>&1
fi
extra=$(find "$WINEPREFIX/drive_c/windows" \( -iname 'libwinpthread*' -o -iname 'libgcc_s*' -o -iname 'libstdc++*' \) 2>/dev/null | head -3)
[ -z "$extra" ] || { echo "O prefixo de teste nao esta limpo (apague $WINEPREFIX):"; echo "$extra"; exit 1; }

"$CC32" -O1 -std=gnu11 -Wall -shared -static -s -Wl,--kill-at -o "$W/d3d9.dll" test/fake_d3d9.c
"$CC32" -O1 -std=gnu11 -Wall -static -s -o "$W/fakegame.exe" test/fakegame.c

echo "1. Plugin no Windows (Wine limpo, d3d9 que imita o nativo)"
# modo: managed | default | somente-leitura (pasta do jogo sem permissao, como C:\Program Files)
for mode in managed default somente-leitura; do
    pool=$mode arg=
    [ "$mode" = somente-leitura ] && pool=managed arg=--log-alt
    g="$W/plugin-$mode"
    mkdir -p "$g/plugins" "$g/texmod/_cache"
    cp dist/DS2TexInject.asi "$g/plugins/"
    cp "$W/d3d9.dll" "$W/fakegame.exe" "$g/"
    printf '[DS2TexInject]\r\nEnabled=1\r\nTextureDir=texmod\\_cache\r\nPool=%s\r\nLoadBudget=0\r\nLogHashes=1\r\n' "$pool" >"$g/DS2TexInject.ini"
    [ "$mode" = somente-leitura ] && chmod a-w "$g"
    echo " $( [ "$mode" = somente-leitura ] && echo "pasta do jogo sem permissao de escrita" || echo "Pool=$pool")"
    st=0
    (cd "$g" && WINEDLLOVERRIDES="d3d9=n;mscoree,mshtml=" timeout 120 wine fakegame.exe $arg) >"$W/out-$mode.txt" 2>&1 || st=$?
    tr -d '\r' <"$W/out-$mode.txt" | grep -vE '^(ASI loader|tudo certo)' | sed 's/^ */  /' || true
    if [ "$st" -ne 0 ]; then
        bad "$mode: saiu com $st. Fim do DS2TexInject.log:"
        tail -15 "$g/DS2TexInject.log" 2>/dev/null | tr -d '\r' | sed 's/^/    | /'
    fi
done

# Os dois instaladores na mesma situacao: pasta de jogo falsa, mesmos pacotes. O dinput8.dll falso
# evita que tentem baixar o MarkerPatch.
run_installers() { # $1 = nome do teste; o resto = pacotes a copiar (vazio = sinteticos)
    local name=$1 side g st
    shift
    for side in windows linux; do
        g="$W/$name-$side"
        mkdir -p "$g/texmod"
        : >"$g/deadspace2.exe"
        : >"$g/dinput8.dll"
        if [ $# -eq 0 ]; then python3 -I test/make_packs.py "$g/texmod"; else cp "$@" "$g/texmod/"; fi
    done
    st=0
    WINEDLLOVERRIDES="mscoree,mshtml=" timeout 1800 wine dist/DS2TexInject-Setup.exe "$(winepath -w "$W/$name-windows")" \
        --yes --no-pause >"$W/$name-setup.txt" 2>&1 || st=$?
    if [ "$st" -eq 0 ]; then ok "Windows: Setup.exe terminou sem erro"; else
        bad "Windows: Setup.exe saiu com $st:"
        tr -d '\r' <"$W/$name-setup.txt" | tail -15 | sed 's/^/    | /'
    fi
    st=0
    timeout 1800 ./install.sh "$W/$name-linux" </dev/null >"$W/$name-install.txt" 2>&1 || st=$?
    if [ "$st" -eq 0 ]; then ok "Linux: install.sh terminou sem erro"; else
        bad "Linux: install.sh saiu com $st:"
        tail -15 "$W/$name-install.txt" | sed 's/^/    | /'
    fi
    for side in windows linux; do
        g="$W/$name-$side"
        if cmp -s dist/DS2TexInject.asi "$g/plugins/DS2TexInject.asi"; then ok "$side: plugin instalado e o de dist/"
        else bad "$side: plugin instalado nao e o de dist/"; fi
        if [ -f "$g/DS2TexInject.ini" ]; then ok "$side: DS2TexInject.ini instalado"; else bad "$side: sem DS2TexInject.ini"; fi
    done
}

echo
echo "2. Instaladores com os mesmos pacotes sinteticos (Windows: Setup.exe no Wine | Linux: install.sh)"
run_installers sintetico
python3 -I test/compare_caches.py "$W/sintetico-windows/texmod/_cache" "$W/sintetico-linux/texmod/_cache" --sintetico || fail=1

if [ $# -gt 0 ]; then
    echo
    echo "3. Instaladores com os pacotes dados: $*"
    run_installers reais "$@"
    python3 -I test/compare_caches.py "$W/reais-windows/texmod/_cache" "$W/reais-linux/texmod/_cache" || fail=1
fi

echo
if [ "$fail" -ne 0 ]; then
    echo "FALHOU. Nao publique estes binarios."
    exit 1
fi
echo "Tudo certo. Binarios testados (confira com os do release):"
sha256sum dist/DS2TexInject.asi dist/DS2TexInject-Setup.exe DS2TexInject.ini
