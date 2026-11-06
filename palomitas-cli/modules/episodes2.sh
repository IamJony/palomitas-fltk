#!/bin/bash
# episodes2.sh - Genera JSON de episodios (silencioso, sin salida en pantalla)
source "$(dirname "$0")/config.sh" --server2

POST_ID="$1"
POST_TYPE="${2:-tvshows}"

init_session > /dev/null 2>&1

RESPONSE=$(curl -s "${BASE_URL}/wp-json/wpf/v1/episodes?post_id=${POST_ID}" \
    -b "$COOKIE_FILE" -H "User-Agent: $USER_AGENT" \
    -H "X-Requested-With: XMLHttpRequest" -H "Referer: $BASE_URL/" --compressed 2>/dev/null)

ERROR=$(echo "$RESPONSE" | jq -r '.error // false' 2>/dev/null)
if [[ "$ERROR" == "true" ]]; then
    exit 1
fi

echo "$RESPONSE" > "$TEMP_DIR/episodes_${POST_ID}.json"

echo "$RESPONSE" | jq '
  .data
  | sort_by(.season_number, .episode_number)
  | map({
      id: ._id,
      season: .season_number,
      episode: .episode_number,
      type: .episode_type,
      title: .slug,
      runtime: .runtime,
      rating: .vote_average
    })
' > "$TEMP_DIR/episodes_${POST_ID}_clean.json" 2>/dev/null

cleanup > /dev/null 2>&1
exit 0
