#!/bin/bash
# Configuración de Palomitas.sh
# Uso: source config.sh [--server1|--server2]


SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/util.sh"


SERVER="server1"
for arg in "$@"; do
    [[ "$arg" == "--server1" ]] && SERVER="server1"
    [[ "$arg" == "--server2" ]] && SERVER="server2"
done


_BASE1="66 94 94 90 89 16 5 5 69 68 70 83 90 79 70 67 89 4 73 69 71"
_BASE2="66 94 94 90 89 16 5 5 76 75 68 90 79 70 67 89 4 94 69"

# Selección de servidor
if [[ "$SERVER" == "server2" ]]; then
    BASE_URL=$(d "$_BASE2")
else
    SERVER="server1"
    BASE_URL=$(d "$_BASE1")
fi


if [[ "$SERVER" == "server1" ]]; then
    API_URL="${BASE_URL}/wp-json/dooplay"
    AJAX_URL="${BASE_URL}/wp-admin/admin-ajax.php"
    PLAYER_API=""
    SEARCH_MODE="html"
else
    API_URL="${BASE_URL}/api/rest"
    AJAX_URL="${BASE_URL}/api/rest/player"
    PLAYER_API="${BASE_URL}/api/rest/player"
    SEARCH_MODE="json"
fi

# Redirección real del dominio
if command -v curl &> /dev/null; then
    REDIRECTED_URL=$(curl -s -L -I -o /dev/null -w '%{url_effective}' \
        --max-time 10 "$BASE_URL" 2>/dev/null)
    if [ -n "$REDIRECTED_URL" ] && [[ "$REDIRECTED_URL" == http* ]]; then
        BASE_URL="${REDIRECTED_URL%/}"
        if [[ "$SERVER" == "server1" ]]; then
            API_URL="${BASE_URL}/wp-json/dooplay"
            AJAX_URL="${BASE_URL}/wp-admin/admin-ajax.php"
        else
            API_URL="${BASE_URL}/api/rest"
            AJAX_URL="${BASE_URL}/api/rest/player"
            PLAYER_API="${BASE_URL}/api/rest/player"
        fi
    fi
fi

# User Agent
USER_AGENT="Mozilla/5.0 (X11; Linux x86_64; rv:146.0) Gecko/20100101 Firefox/146.0"

# --- Temporales por servidor (no mezclar sesiones) ---
TEMP_DIR="/tmp/TEMP_Palomitas/${SERVER}"
COOKIE_FILE="$TEMP_DIR/palomitas_cookies.txt"
SESSION_FILE="$TEMP_DIR/palomitas_session.txt"
TEMP_HTML="$TEMP_DIR/palomitas_temp.html"

mkdir -p "$TEMP_DIR" 2>/dev/null

# Colores
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

log_error()   { echo -e "${RED}[ERROR]${NC} $1" >&2; }
log_success() { echo -e "${GREEN}[OK]${NC} $1"; }
log_info()    { echo -e "${CYAN}[INFO]${NC} $1"; }
log_warn()    { echo -e "${YELLOW}[WARN]${NC} $1"; }

# --- Helpers de validez de sesión ---

# Cookie existe, no vacía y tiene menos de 60 min
_cookie_valida() {
    [ -f "$COOKIE_FILE" ] || return 1
    [ -s "$COOKIE_FILE" ] || return 1
    local ahora mtime diff
    ahora=$(date +%s)
    mtime=$(stat -c %Y "$COOKIE_FILE" 2>/dev/null)
    [ -z "$mtime" ] && return 1
    diff=$((ahora - mtime))
    [ "$diff" -lt 3600 ] || return 1
    return 0
}

# El archivo de sesión existe y tiene un NONCE no vacío
_nonce_valido() {
    [ -f "$SESSION_FILE" ] || return 1
    grep -q '^NONCE=.' "$SESSION_FILE" 2>/dev/null || return 1
    return 0
}

# --- Sesión ---
init_session() {
    # ===== server2 =====
    if [[ "$SERVER" == "server2" ]]; then
        if _cookie_valida; then
            log_info "Sesión $SERVER reutilizada (cookie válida)"
            return 0
        fi
        log_info "Inicializando sesión $SERVER..."
        rm -f "$COOKIE_FILE"
        curl -s -c "$COOKIE_FILE" "$BASE_URL" \
            -H "User-Agent: $USER_AGENT" -o /dev/null
        log_success "Sesión $SERVER inicializada"
        return 0
    fi

    # server1 necesita cookie + nonce

    # Caso A: cookie válida + nonce válido, se reutiliza
    if _cookie_valida && _nonce_valido; then
        source "$SESSION_FILE" 2>/dev/null
        log_info "Sesión $SERVER reutilizada (cookie válida)"
        return 0
    fi

    # Caso B: cookie válida, falta nonce → solo pedir home para sacar nonce
    if _cookie_valida && ! _nonce_valido; then
        log_info "Cookie válida, obteniendo nonce..."
        curl -s -b "$COOKIE_FILE" "$BASE_URL" \
            -H "User-Agent: $USER_AGENT" -o "$TEMP_HTML"
        NONCE=$(grep -o 'nonce":"[^"]*"' "$TEMP_HTML" | head -1 | cut -d'"' -f3)
        rm -f "$TEMP_HTML"
        if [ -n "$NONCE" ]; then
            echo "NONCE=$NONCE" > "$SESSION_FILE"
            log_success "Nonce obtenido: $NONCE"
            return 0
        fi
        log_warn "No se obtuvo nonce, renovando sesión completa..."
    fi

    # Caso C: sin cookie o expirada → renovar todo
    log_info "Inicializando sesión $SERVER (renovando)..."
    rm -f "$COOKIE_FILE" "$SESSION_FILE"

    curl -s -c "$COOKIE_FILE" "$BASE_URL" \
        -H "User-Agent: $USER_AGENT" -o /dev/null
    curl -s -b "$COOKIE_FILE" "$BASE_URL" \
        -H "User-Agent: $USER_AGENT" -o "$TEMP_HTML"

    NONCE=$(grep -o 'nonce":"[^"]*"' "$TEMP_HTML" | head -1 | cut -d'"' -f3)
    rm -f "$TEMP_HTML"

    if [ -n "$NONCE" ]; then
        echo "NONCE=$NONCE" > "$SESSION_FILE"
        log_success "Sesión $SERVER inicializada (NONCE=$NONCE)"
    else
        echo "NONCE=" > "$SESSION_FILE"
        log_warn "Sesión $SERVER inicializada sin NONCE (revisa la home)"
    fi
}

cleanup() {
    rm -f "$TEMP_HTML" 2>/dev/null
}
