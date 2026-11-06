#!/bin/bash
# search2.sh - Búsqueda server2
# Uso: ./search2.sh "spider"

source "$(dirname "$0")/config.sh" --server2

if [[ ! $1 ]]; then
    echo "Uso: $0 <término>"
    exit 1
fi

KEYWORD="$1"
QUERY=$(echo "$KEYWORD" | sed 's/ /%20/g')

init_session

log_info "Buscando '$KEYWORD' en $SERVER"

RESPONSE=$(curl -s "${API_URL}/search?post_type=movies,tvshows,animes&query=${QUERY}&posts_per_page=20" \
    -b "$COOKIE_FILE" \
    -H "User-Agent: $USER_AGENT" \
    -H "X-Requested-With: XMLHttpRequest" \
    -H "Referer: $BASE_URL/" \
    --compressed)

ERROR=$(echo "$RESPONSE" | jq -r '.error // false')
if [[ "$ERROR" == "true" ]]; then
    log_error "Error de la API: $(echo "$RESPONSE" | jq -r '.message')"
    exit 1
fi

echo "$RESPONSE" | jq --arg base "$BASE_URL" '
  .data.posts
  | map({
      key: (._id | tostring),
      value: {
        title: .title,
        url: (
          if .type == "movies" then "\($base)/peliculas/\(.slug)/"
          elif .type == "tvshows" then "\($base)/series/\(.slug)/"
          elif .type == "animes" then "\($base)/animes/\(.slug)/"
          else "\($base)/\(.slug)/"
          end
        ),
        img: (if .images.poster then "\($base)/wp-content/uploads\(.images.poster)" else "" end),
        type: .type,
        extra: {
          date: (if .release_date then (.release_date | split("-")[0]) else "" end),
          imdb: (.rating // "")
        }
      }
    })
  | from_entries
' > "$TEMP_DIR/buscar.json"

TOTAL=$(jq 'length' "$TEMP_DIR/buscar.json" 2>/dev/null || echo 0)
log_success "$TOTAL resultados"

cleanup
