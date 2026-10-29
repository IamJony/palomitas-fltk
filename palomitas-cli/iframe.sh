#!/bin/bash

source config.sh

# Validar argumentos
if [ $# -eq 0 ]; then
    echo "Uso: $0 <URL DE LA PELICULA>"
    echo "Ejemplo: $0 https://XXXXXX.com/pelicula/rocky/"
    exit 1
fi

URL_PELICULA="$1"


#echo -e "${CYAN}=== Analizando: $URL_PELICULA ===${NC}"

# Inicializar sesión
init_session

# 1. Descargar página
log_info "Descargando página..."
curl -s -b "$COOKIE_FILE" "$URL_PELICULA" \
    -H "User-Agent: $USER_AGENT" \
    -o "$TEMP_HTML"

# Verificar si fue exitoso
if [ $? -ne 0 ]; then
    log_error "Error al descargar la página"
    cleanup
    exit 1
fi

# 2. Extraer POST_ID del shortlink
log_info "Extrayendo ID de la película..."
POST_ID=$(grep "shortlink" "$TEMP_HTML" | grep -o "?p=[0-9]\+" | head -1 | sed 's/?p=//')

if [ -z "$POST_ID" ]; then
    log_error "No se pudo extraer el ID de la película"
    cleanup
    exit 1
fi

log_success "ID encontrado: $POST_ID"

# 3. Hacer petición AJAX para obtener datos del reproductor
log_info "Consultando API del reproductor..."
RESPONSE=$(curl -s -b "$COOKIE_FILE" "$AJAX_URL" \
    --compressed \
    -X POST \
    -H "User-Agent: $USER_AGENT" \
    -H "Accept: */*" \
    -H "Accept-Language: es-MX,es;q=0.8,en-US;q=0.5,en;q=0.3" \
    -H "Accept-Encoding: gzip, deflate, br, zstd" \
    -H "Content-Type: application/x-www-form-urlencoded; charset=UTF-8" \
    -H "X-Requested-With: XMLHttpRequest" \
    -H "Origin: $BASE_URL" \
    -H "Sec-GPC: 1" \
    -H "Connection: keep-alive" \
    -H "Referer: $URL_PELICULA" \
    -H "Sec-Fetch-Dest: empty" \
    -H "Sec-Fetch-Mode: cors" \
    -H "Sec-Fetch-Site: same-origin" \
    -H "Pragma: no-cache" \
    -H "Cache-Control: no-cache" \
    -H "TE: trailers" \
    --data-raw "action=doo_player_ajax&post=$POST_ID&nume=1&type=movie")

if [ $? -ne 0 ]; then
    log_error "Error al consultar la API"
    cleanup
    exit 1
fi

# Guardar respuesta en peli.json
echo "$RESPONSE" > "$TEMP_DIR"/peli.json

# Verificar que se creó el archivo
if [ -f ""$TEMP_DIR"/peli.json" ]; then
    log_success "Datos guardados en peli.json"
    
    # Mostrar contenido (opcional, para depuración)
    if command -v jq &> /dev/null; then
        echo ""
        echo "Contenido de peli.json:"
        jq . "$TEMP_DIR"/peli.json
    else
        echo "Respuesta: $RESPONSE"
    fi
else
    log_error "No se pudo crear peli.json"
    echo "Respuesta recibida:"
    echo "$RESPONSE"
fi

# Limpiar archivos temporales
cleanup
