#!/bin/bash

source config.sh

PLAYER_URL="$1"
URL_PELICULA="$2"

HTML=$(curl -s "$PLAYER_URL" \
  -H "User-Agent: $USER_AGENT" \
  -H "Referer: $URL_PELICULA" \
  --compressed)

VIDEO=$(echo "$HTML" | grep -oP 'sources:\s*\[\s*\{\s*"file"\s*:\s*"\K[^"]+' | head -1)
SUBS=$(echo "$HTML" | grep -oP 'tracks:\s*\[\s*\{\s*"file"\s*:\s*"\K[^"]+\.srt' | head -1)
IMAGE=$(echo "$HTML" | grep -oP '"image"\s*:\s*"\K[^"]+' | head -1)

VIDEO=$(echo "$VIDEO" | sed 's/\\\//\//g')
SUBS=$(echo "$SUBS" | sed 's/\\\//\//g')
IMAGE=$(echo "$IMAGE" | sed 's/\\\//\//g')

cat > "$TEMP_DIR"/output.json << EOF
{
  "embed": "$PLAYER_URL",
  "video": "$VIDEO",
  "subtitle": "$SUBS",
  "preview": "$IMAGE",
  "type": "$(echo "$VIDEO" | grep -q "m3u8" && echo "hls" || echo "direct")"
}
EOF

cat "$TEMP_DIR"/output.json
