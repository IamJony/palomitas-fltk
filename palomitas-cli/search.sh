#!/bin/bash
# search.sh - Búsqueda de peliculas
# Uso:
#   ./search.sh "rock"

source config.sh

if [[ ! $1 ]]; then
    echo "Uso: $0 <término> [página]"
    exit 1
fi

KEYWORD=$(echo "$1" | sed 's/ /%20/g')

init_session

curl -s "$API_URL/search/?keyword=$KEYWORD&nonce=$NONCE" \
  -b "$COOKIE_FILE" \
  -H "User-Agent: $USER_AGENT" \
  -H "Accept: application/json" \
  -H "X-Requested-With: XMLHttpRequest" \
  -H "Referer: $BASE_URL" \
  --compressed | jq '.' > "$TEMP_DIR"/buscar.json
