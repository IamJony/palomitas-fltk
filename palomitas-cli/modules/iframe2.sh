#!/bin/bash
# iframe2.sh - Reproductores server2
# Uso: ./iframe2.sh <id> [type]
#   type: movies (default) | tvshows | animes

source "$(dirname "$0")/config.sh" --server2

if [[ ! $1 ]]; then
    echo "Uso: $0 <id> [type]"
    echo "   type: movies (default) | tvshows | animes"
    exit 1
fi

POST_ID="$1"
POST_TYPE="${2:-movies}"

init_session

log_info "Obteniendo reproductores para ID $POST_ID ($POST_TYPE) en $SERVER..."

RESPONSE=$(curl -s "${PLAYER_API}?post_id=${POST_ID}&post_type=${POST_TYPE}" \
    -b "$COOKIE_FILE" \
    -H "User-Agent: $USER_AGENT" \
    -H "X-Requested-With: XMLHttpRequest" \
    -H "Referer: $BASE_URL/" \
    --compressed)

ERROR=$(echo "$RESPONSE" | jq -r '.error // false')
if [[ "$ERROR" == "true" ]]; then
    log_error "Error: $(echo "$RESPONSE" | jq -r '.message')"
    exit 1
fi

# Guardar respuesta cruda
echo "$RESPONSE" > "$TEMP_DIR/player_${POST_ID}.json"

# Transformar a formato limpio
echo "$RESPONSE" | jq '
  .data
  | to_entries
  | map(select(.key != "embeds"))
  | map({
      server: .key,
      lang: .value.lang,
      quality: .value.quality,
      url: .value.url
    })
' > "$TEMP_DIR/player_${POST_ID}_clean.json"

TOTAL=$(jq 'length' "$TEMP_DIR/player_${POST_ID}_clean.json" 2>/dev/null || echo 0)
if [[ "$TOTAL" == "0" ]]; then
    log_warn "No hay reproductores disponibles para ID $POST_ID."
    exit 1
fi

log_success "$TOTAL reproductor(es) encontrado(s):"
echo ""
jq -r '.[] | "[\(.server)] \(.quality) | \(.lang)\n   \(.url)"' "$TEMP_DIR/player_${POST_ID}_clean.json"

# ============================================================
# ESCRIBIR peli.json con el primer reproductor + tipo de contenido
# ============================================================
PLAYER_URL=$(jq -r '.[0].url // ""' "$TEMP_DIR/player_${POST_ID}_clean.json")
PLAYER_LANG=$(jq -r '.[0].lang // ""' "$TEMP_DIR/player_${POST_ID}_clean.json")
PLAYER_QUALITY=$(jq -r '.[0].quality // ""' "$TEMP_DIR/player_${POST_ID}_clean.json")

if [[ -n "$PLAYER_URL" ]] && [[ "$PLAYER_URL" != "null" ]]; then
    jq -n \
        --arg url "$PLAYER_URL" \
        --arg type "$POST_TYPE" \
        --arg lang "$PLAYER_LANG" \
        --arg quality "$PLAYER_QUALITY" \
        '{embed_url: $url, url: $url, type: $type, lang: $lang, quality: $quality}' \
        > "$TEMP_DIR/peli.json"
    log_success "peli.json escrito: $PLAYER_URL (tipo: $POST_TYPE)"
else
    log_error "No se encontró URL de reproductor para ID $POST_ID"
    exit 1
fi

cleanup
