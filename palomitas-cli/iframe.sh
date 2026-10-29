#!/bin/bash

source config.sh

# Validar argumentos
if [ $# -eq 0 ]; then
    echo "Uso: $0 <URL DE LA PELICULA>"
    exit 1
fi

URL_PELICULA="$1"

init_session

# 1. Descargar página usando wget
log_info "Descargando página..."
wget -q --no-check-certificate -U "$USER_AGENT" --load-cookies="$COOKIE_FILE" -O "$TEMP_HTML" "$URL_PELICULA"

if [ $? -ne 0 ] || [ ! -s "$TEMP_HTML" ]; then
    log_error "Error al descargar la página"
    cleanup
    exit 1
fi

# 2. Extraer POST_ID
log_info "Extrayendo ID de la película..."
POST_ID=$(grep "shortlink" "$TEMP_HTML" | grep -o "?p=[0-9]\+" | head -1 | sed 's/?p=//')

if [ -z "$POST_ID" ]; then
    log_error "No se pudo extraer el ID de la película"
    cleanup
    exit 1
fi

log_success "ID encontrado: $POST_ID"

# 3. Hacer petición AJAX usando wget (POST)
log_info "Consultando API del reproductor..."
# wget para POST: --post-data y --header
RESPONSE=$(wget -q --no-check-certificate -O - \
    --header="X-Requested-With: XMLHttpRequest" \
    --header="Referer: $URL_PELICULA" \
    --header="User-Agent: $USER_AGENT" \
    --post-data="action=doo_player_ajax&post=$POST_ID&nume=1&type=movie" \
    "$AJAX_URL")

if [ -z "$RESPONSE" ]; then
    log_error "Error al consultar la API"
    cleanup
    exit 1
fi

# 4. Extraer la URL del iframe del JSON de respuesta
EMBED_URL=$(echo "$RESPONSE" | grep -o 'src=\\*"[^"]*\\*"' | head -1 | sed 's/src=\\*"//;s/\\*"//')

# Si no encontramos el src, intentamos con comillas simples
if [ -z "$EMBED_URL" ]; then
    EMBED_URL=$(echo "$RESPONSE" | grep -o "src='[^']*'" | head -1 | sed "s/src='//;s/'//")
fi

# 5. Guardar el JSON final
if [ -n "$EMBED_URL" ]; then
    echo "{\"embed_url\": \"$EMBED_URL\", \"url\": \"$EMBED_URL\"}" > "$TEMP_DIR/peli.json"
    log_success "Player URL extraído: $EMBED_URL"
else
    # Si falla, guardamos el JSON crudo para depurar
    echo "$RESPONSE" > "$TEMP_DIR/peli.json"
    log_error "No se pudo extraer el iframe del JSON"
fi

cleanup
