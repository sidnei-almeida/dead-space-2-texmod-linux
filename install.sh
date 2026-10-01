#!/usr/bin/env bash
# Instala o DS2TexInject na pasta do Dead Space 2 e prepara as texturas.
# Uso: ./install.sh ["/caminho/para/Dead Space 2"]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

GAME="${1:-}"
if [ -z "$GAME" ]; then
    for lib in "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
        [ -f "$lib/steamapps/libraryfolders.vdf" ] || continue
        while read -r p; do
            if [ -f "$p/steamapps/common/Dead Space 2/deadspace2.exe" ]; then GAME="$p/steamapps/common/Dead Space 2"; break 2; fi
        done < <(grep -oP '"path"\s+"\K[^"]+' "$lib/steamapps/libraryfolders.vdf"; echo "$lib")
    done
fi
if [ -z "$GAME" ] || [ ! -f "$GAME/deadspace2.exe" ]; then
    echo "Pasta do Dead Space 2 nao encontrada. Rode: ./install.sh \"/caminho/para/Dead Space 2\""; exit 1
fi
echo "Jogo: $GAME"

ASI="$HERE/dist/DS2TexInject.asi"
[ -f "$ASI" ] || "$HERE/build.sh"

if [ ! -f "$GAME/dinput8.dll" ]; then
    echo "AVISO: dinput8.dll nao encontrado. Instale o MarkerPatch (https://github.com/Wemino/MarkerPatch)"
    echo "       ou o Ultimate ASI Loader, senao o plugin nao e carregado."
fi

mkdir -p "$GAME/plugins" "$GAME/texmod"
cp "$ASI" "$GAME/plugins/"
[ -f "$GAME/DS2TexInject.ini" ] || cp "$HERE/DS2TexInject.ini" "$GAME/"
echo "Plugin instalado em $GAME/plugins/DS2TexInject.asi"

if ls "$GAME"/texmod/*.tpf >/dev/null 2>&1; then
    python3 "$HERE/ds2tex.py" "$GAME/texmod"
else
    echo
    echo "Coloque seus .tpf em \"$GAME/texmod/\" e rode:"
    echo "  python3 \"$HERE/ds2tex.py\" \"$GAME/texmod\""
fi
