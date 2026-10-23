#!/usr/bin/env bash
# Instala o DS2TexInject na pasta do Dead Space 2 e prepara as texturas.
# Uso: ./install.sh ["/caminho/para/Dead Space 2"]
# Sem argumento, procura nas bibliotecas da Steam; se nao achar, pergunta onde esta o jogo.
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

# Nao achou: pede para o usuario apontar a pasta (janela se houver zenity/kdialog, senao no terminal)
while [ -z "$GAME" ] || [ ! -f "$GAME/deadspace2.exe" ]; do
    [ -n "$GAME" ] && echo "deadspace2.exe nao esta em: $GAME"
    echo "Pasta do Dead Space 2 nao encontrada automaticamente."
    GAME=""
    if [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && command -v zenity >/dev/null; then
        GAME=$(zenity --file-selection --title="Escolha o deadspace2.exe" --file-filter="deadspace2.exe | deadspace2.exe" 2>/dev/null || true)
    elif [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && command -v kdialog >/dev/null; then
        GAME=$(kdialog --title "Escolha o deadspace2.exe" --getopenfilename "$HOME" "deadspace2.exe" 2>/dev/null || true)
    elif [ -t 0 ]; then
        read -rp "Caminho da pasta do jogo (Enter para cancelar): " GAME
    fi
    [ -z "$GAME" ] && { echo "Cancelado. Rode: ./install.sh \"/caminho/para/Dead Space 2\""; exit 1; }
    GAME="${GAME/#\~/$HOME}"
    case "$GAME" in */deadspace2.exe) GAME="$(dirname "$GAME")" ;; esac
done
echo "Jogo: $GAME"

ASI="$HERE/dist/DS2TexInject.asi"
[ -f "$ASI" ] || "$HERE/build.sh"

if [ ! -f "$GAME/dinput8.dll" ]; then
    echo "Nenhum ASI loader (dinput8.dll) na pasta do jogo: sem ele o plugin nao e carregado."
    ans=n
    [ -t 0 ] && read -rp "Baixar e instalar o MarkerPatch agora? (recomendado) [S/n] " ans
    if [[ "${ans:-s}" =~ ^[SsYy]?$ ]] && command -v unzip >/dev/null; then
        tmp=$(mktemp -d)
        if curl -fsSL -o "$tmp/MarkerPatch.zip" https://github.com/Wemino/MarkerPatch/releases/latest/download/MarkerPatch.zip \
            && unzip -qq -n "$tmp/MarkerPatch.zip" -d "$GAME"; then
            echo "MarkerPatch instalado."
        fi
        rm -rf "$tmp"
    fi
    if [ ! -f "$GAME/dinput8.dll" ]; then
        echo "AVISO: instale um ASI loader: MarkerPatch (recomendado, https://github.com/Wemino/MarkerPatch)"
        echo "       ou Ultimate ASI Loader (versao Win32)."
    fi
fi

mkdir -p "$GAME/plugins" "$GAME/texmod"
cp "$ASI" "$GAME/plugins/"
[ -f "$GAME/DS2TexInject.ini" ] || cp "$HERE/DS2TexInject.ini" "$GAME/"
echo "Plugin instalado em $GAME/plugins/DS2TexInject.asi"

if compgen -G "$GAME/texmod/*.tpf" >/dev/null || compgen -G "$GAME/texmod/*.zip" >/dev/null; then
    python3 "$HERE/ds2tex.py" "$GAME/texmod"
else
    echo
    echo "Coloque seus .tpf em \"$GAME/texmod/\" e rode:"
    echo "  python3 \"$HERE/ds2tex.py\" \"$GAME/texmod\""
fi
