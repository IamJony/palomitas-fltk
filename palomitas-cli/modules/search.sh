#!/bin/bash
# search.sh - Búsqueda de peliculas server1
# Uso: ./search.sh "rock"

source "$(dirname "$0")/config.sh" --server1

if [[ ! $1 ]]; then
    echo "Uso: $0 <término>"
    exit 1
fi

KEYWORD=$(echo "$1" | sed 's/ /%20/g')

init_session

log_info "Buscando '$KEYWORD' en $SERVER"

curl -s "$API_URL/search/?keyword=$KEYWORD&nonce=$NONCE" \
  -b "$COOKIE_FILE" \
  -H "User-Agent: $USER_AGENT" \
  -H "Accept: application/json" \
  -H "X-Requested-With: XMLHttpRequest" \
  -H "Referer: $BASE_URL" \
  --compressed | jq '.' > "$TEMP_DIR/buscar.json"

TOTAL=$(jq 'length' "$TEMP_DIR/buscar.json" 2>/dev/null || echo 0)
log_success "$TOTAL resultados"

cleanup
