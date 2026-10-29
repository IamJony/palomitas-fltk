#!/bin/bash
# Configuracion inicial de Palomitas.sh
source util.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UTIL="$SCRIPT_DIR/util.sh"

source "$UTIL"

# URLs
BASE="66 94 94 90 89 16 5 5 69 68 70 83 90 79 70 67 89 4 73 69 71"
BASE_URL=$(d "$BASE")
API_URL="${BASE_URL}/wp-json/dooplay"
AJAX_URL="${BASE_URL}/wp-admin/admin-ajax.php"

# Redireccion
if command -v curl &> /dev/null; then
    REDIRECTED_URL=$(curl -s -L -I -o /dev/null -w '%{url_effective}' "$BASE_URL" 2>/dev/null)
    
    if [ -n "$REDIRECTED_URL" ] && [[ "$REDIRECTED_URL" == http* ]]; then
        BASE_URL="${REDIRECTED_URL%/}"
    fi
fi

# User Agent
USER_AGENT="Mozilla/5.0 (X11; Linux x86_64; rv:146.0) Gecko/20100101 Firefox/146.0"

# Archivos temporales
TEMP_DIR="/tmp/TEMP_Palomitas"
COOKIE_FILE="$TEMP_DIR/palomitas_cookies.txt"
SESSION_FILE="$TEMP_DIR/palomitas_session.txt"
TEMP_HTML="$TEMP_DIR/palomitas_temp.html"

# Crear directorio temporal
mkdir -p "$TEMP_DIR" 2>/dev/null

# Colores para la interfaz
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Funciones de utilidad
log_error() { echo -e "${RED}[ERROR]${NC} $1" >&2; }
log_success() { echo -e "${GREEN}[OK]${NC} $1"; }
log_info() { echo -e "${CYAN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

# Inicializar sesión
init_session() {
    if [ ! -f "$COOKIE_FILE" ] || [ "$(find "$COOKIE_FILE" -mmin +60 2>/dev/null)" ]; then
        log_info "Inicializando sesión..."
        curl -s -c "$COOKIE_FILE" "$BASE_URL" \
            -H "User-Agent: $USER_AGENT" \
            -o /dev/null
        
        # Extraer nonce inicial
        curl -s -b "$COOKIE_FILE" "$BASE_URL" \
            -H "User-Agent: $USER_AGENT" \
            -o "$TEMP_HTML"
        
        NONCE=$(grep -o 'nonce":"[^"]*"' "$TEMP_HTML" | head -1 | cut -d'"' -f3)
        echo "NONCE=$NONCE" > "$SESSION_FILE"
        log_success "Sesión inicializada"
    else
        log_info "Usando sesión existente"
    fi
    source "$SESSION_FILE" 2>/dev/null || true
}

# Limpiar archivos temporales
cleanup() {
    rm -f "$TEMP_HTML" 2>/dev/null
}
