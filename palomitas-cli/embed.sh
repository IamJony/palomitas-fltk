#!/bin/bash

source config.sh

PLAYER_URL="$1"
URL_PELICULA="$2"

if [ -z "$PLAYER_URL" ]; then
    echo '{"error": "Falta PLAYER_URL"}' > "$TEMP_DIR/output.json"
    exit 1
fi

# 1. Descargamos el HTML del player usando wget (ignorando SSL)
# Usamos --referer para engañar al servidor y --no-check-certificate para Tiny Core
HTML=$(wget -q --no-check-certificate -O - \
    -U "$USER_AGENT" \
    --referer="$URL_PELICULA" \
    "$PLAYER_URL" 2>/dev/null)

if [ -z "$HTML" ]; then
    echo '{"error": "No se pudo descargar el player HTML"}' > "$TEMP_DIR/output.json"
    exit 1
fi

# 2. Extraemos el video, subtítulos e imagen
# Buscamos el patrón: sources: [{ "file": "URL" 
VIDEO=$(echo "$HTML" | grep -o 'sources:[[:space:]]*\[[[:space:]]*{[[:space:]]*"file"[[:space:]]*:[[:space:]]*"[^"]*' | head -1 | sed 's/.*"file"[[:space:]]*:[[:space:]]*"//')

# Si no encuentra el video, intentamos un fallback buscando m3u8 o mp4 directamente
if [ -z "$VIDEO" ]; then
    VIDEO=$(echo "$HTML" | grep -o 'https\?://[^"]*\.m3u8[^"]*' | head -1)
fi
if [ -z "$VIDEO" ]; then
    VIDEO=$(echo "$HTML" | grep -o 'https\?://[^"]*\.mp4[^"]*' | head -1)
fi

SUBS=$(echo "$HTML" | grep -o 'tracks:[[:space:]]*\[[[:space:]]*{[[:space:]]*"file"[[:space:]]*:[[:space:]]*"[^"]*\.srt' | head -1 | sed 's/.*"file"[[:space:]]*:[[:space:]]*"//')
IMAGE=$(echo "$HTML" | grep -o '"image"[[:space:]]*:[[:space:]]*"[^"]*' | head -1 | sed 's/.*"image"[[:space:]]*:[[:space:]]*"//')

# Limpiamos los escapes de barra invertida típicos del JSON de JWPlayer
VIDEO=$(echo "$VIDEO" | sed 's/\\\//\//g')
SUBS=$(echo "$SUBS" | sed 's/\\\//\//g')
IMAGE=$(echo "$IMAGE" | sed 's/\\\//\//g')

# 3. Escribimos el JSON de salida
if [ -n "$VIDEO" ]; then
    TYPE="direct"
    if echo "$VIDEO" | grep -q "m3u8"; then
        TYPE="hls"
    fi

    cat > "$TEMP_DIR"/output.json << EOF
{
  "embed": "$PLAYER_URL",
  "video": "$VIDEO",
  "subtitle": "$SUBS",
  "preview": "$IMAGE",
  "type": "$TYPE"
}
EOF
else
    # Si no hay video, guardamos un error
    cat > "$TEMP_DIR"/output.json << EOF
{
  "error": "No se encontro stream de video en el player",
  "embed": "$PLAYER_URL"
}
EOF
fi

cat "$TEMP_DIR"/output.json
