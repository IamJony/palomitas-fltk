#!/bin/bash
# iframe.sh - Reproductores server1
# Uso: ./iframe.sh <URL DE LA PELICULA>

source "$(dirname "$0")/config.sh" --server1

if [ $# -eq 0 ]; then
    echo "Uso: $0 <URL DE LA PELICULA>"
    exit 1
fi

URL_PELICULA="$1"

init_session

log_info "Obteniendo reproductores en $SERVER..."

# 1. Descargar la página para obtener el ID
wget -q --no-check-certificate -U "$USER_AGENT" --load-cookies="$COOKIE_FILE" -O "$TEMP_HTML" "$URL_PELICULA"

if [ $? -ne 0 ] || [ ! -s "$TEMP_HTML" ]; then
    log_error "Error al descargar la página"
    cleanup
    exit 1
fi

# Extraer POST ID (Método robusto: busca data-post o shortlink)
POST_ID=$(grep -oP 'data-post="\K[0-9]+' "$TEMP_HTML" | head -1)
if [ -z "$POST_ID" ]; then
    POST_ID=$(grep "shortlink" "$TEMP_HTML" | grep -o "?p=[0-9]\+" | head -1 | sed 's/?p=//')
fi

if [ -z "$POST_ID" ]; then
    log_error "No se pudo extraer el ID de la película"
    # Debug: guardar html para revisar
    cp "$TEMP_HTML" "$TEMP_DIR/debug_page.html"
    cleanup
    exit 1
fi

log_success "ID encontrado: $POST_ID"

# Determinar tipo (movie, tv, anime) basado en la URL
TYPE="movie"
if echo "$URL_PELICULA" | grep -q "/series/"; then TYPE="tv"; fi
if echo "$URL_PELICULA" | grep -q "/anime/"; then TYPE="anime"; fi

# 2. Consultar API AJAX
RESPONSE=$(wget -q --no-check-certificate -O - \
    --header="X-Requested-With: XMLHttpRequest" \
    --header="Referer: $URL_PELICULA" \
    --header="User-Agent: $USER_AGENT" \
    --post-data="action=doo_player_ajax&post=$POST_ID&nume=1&type=$TYPE" \
    "$AJAX_URL" 2>/dev/null)

if [ -z "$RESPONSE" ]; then
    log_error "Error al consultar la API (Respuesta vacía)"
    cleanup
    exit 1
fi

# Guardar respuesta cruda
echo "$RESPONSE" > "$TEMP_DIR/player_${POST_ID}.json"

EMBED_URL=""

# 3. Extracción Inteligente
# Verificamos si la respuesta es JSON válido
if echo "$RESPONSE" | jq . > /dev/null 2>&1; then
    # Es JSON: Extraer embed_url directamente
    EMBED_URL=$(echo "$RESPONSE" | jq -r '.embed_url // .src // ""' 2>/dev/null)
else
    # No es JSON (HTML o texto plano): Usar Regex antiguo
    EMBED_URL=$(echo "$RESPONSE" | grep -oP 'src=["\x27]?\K[^"\x27\s]+(?=["\x27])' | head -1)
    if [ -z "$EMBED_URL" ]; then
        EMBED_URL=$(echo "$RESPONSE" | grep -o 'src=\\*"[^"]*\\*"' | head -1 | sed 's/src=\\*"//;s/\\*"//')
    fi
fi

# Limpieza final de caracteres de escape comunes en JSON (\u002F o \/)
EMBED_URL=$(echo "$EMBED_URL" | sed 's|\\/|/|g' | sed 's|%2F|/|g')

if [ -z "$EMBED_URL" ] || [ "$EMBED_URL" == "null" ]; then
    log_error "No se detectó 'embed_url'. Respuesta: $RESPONSE"
    cleanup
    exit 1
fi

log_success "Player encontrado: $EMBED_URL"

jq -n --arg url "$EMBED_URL" --arg type "$TYPE" '{embed_url: $url, url: $url, type: $type}' > "$TEMP_DIR/peli.json"

cleanup
