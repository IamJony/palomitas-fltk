#!/bin/bash
# embed2.sh - Extractor universal de enlaces de video (HLS/MP4)

usage() {
    cat <<'EOF'
Uso: ./embed2.sh [--debug] <url_embed>

Extrae la URL directa (HLS/MP4) de un enlace embed de video.

Opciones:
  --debug, -d    Muestra trazas de depuración por stderr
  -h, --help     Muestra esta ayuda

Ejemplos:
  ./embed2.sh "https://filemoon.sx/e/abc123xyz"
  ./embed2.sh "https://vimeos.net/embed-2nulhg4mnuz7.html"
  ./embed2.sh --debug "https://streamwish.to/e/abc123"

Salida:
  Imprime SOLO la URL del HLS/MP4 en stdout (o nada si falla).
  Exit 0 si encontró enlace, 1 si no.
EOF
    exit 0
}

DEBUG=0
ARGS=()
for arg in "$@"; do
    case "$arg" in
        --debug|-d) DEBUG=1 ;;
        -h|--help) usage ;;
        *) ARGS+=("$arg") ;;
    esac
done

URL="${ARGS[0]}"
[[ -z "$URL" ]] && usage

log() { (( DEBUG )) && echo "[debug] $*" >&2; }

UA="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/123.0.0.0 Safari/537.36"
DOMAIN=$(echo "$URL" | sed -E 's|https?://([^/]+).*|\1|')
BASE_URL=$(echo "$URL" | sed -E 's|(https?://[^/]+).*|\1|')
URL_PATH=$(echo "$URL" | sed -E 's|https?://[^/]+||')
log "dominio=$DOMAIN base=$BASE_URL path=$URL_PATH"

TMP=$(mktemp /tmp/embed_XXXXXX)
TMP_UNPACK=$(mktemp /tmp/embed_unpack_XXXXXX)
trap 'rm -f "$TMP" "$TMP_UNPACK"' EXIT

fetch() {
    local u="$1"
    local extra=("${@:2}")
    timeout 15 curl -s -L \
        --connect-timeout 5 \
        --max-time 12 \
        --retry 0 \
        --compressed \
        -H "User-Agent: $UA" \
        -H "Referer: $URL" \
        -H "Accept: */*" \
        "${extra[@]}" \
        "$u"
}

# ==================================================================
# Decodificador P.A.C.K.E.R. en bash puro
# ==================================================================

dec_to_base() {
    local n="$1" base="$2" tok=""
    (( n == 0 )) && { echo "0"; return; }
    while (( n > 0 )); do
        local r=$((n % base))
        if (( r < 10 )); then
            tok="$(printf '%d' $r)$tok"
        else
            tok="$(printf "\\$(printf '%03o' $((r - 10 + 97)))")$tok"
        fi
        n=$((n / base))
    done
    echo "$tok"
}

extract_packer_block() {
    local content="$1"
    local block=""

    # Acepta .split('|'), .split("|") y .split(/\|/)
    if command -v perl >/dev/null 2>&1; then
        block=$(printf '%s' "$content" | perl -0777 -ne '
            if (/(eval\(function\(p,a,c,k,e,d\).*?\.split\(\s*(?:
                    [\x27"]\|[\x27"]
                    |
                    \/\\?\|?\/
                )\s*\)\s*\)\))/sx) {
                print $1;
            }
        ' 2>/dev/null)
    fi

    if [[ -z "$block" ]]; then
        block=$(printf '%s' "$content" | tr '\n' ' ' | \
            grep -oE "eval\(function\(p,a,c,k,e,d\).{1,800000}\.split\((['\"]\|['\"]|/\\\\?\|?/).{0,300}\)\)" | head -1)
    fi

    if [[ -z "$block" ]]; then
        block=$(printf '%s' "$content" | tr '\n' ' ' | \
            grep -oE "eval\(function\(p,a,c,k,e,d\).{1,800000}\)\)" | head -1)
    fi

    printf '%s' "$block"
}

unpack_packer() {
    local content="$1"
    local packer a c p k

    packer=$(extract_packer_block "$content")
    [[ -z "$packer" ]] && { log "no packer"; return 1; }
    log "packer len=${#packer}"

    # a y c: los dos números tras el primer argumento string
    a=$(printf '%s' "$packer" | perl -ne 'print $1 if /,\s*(\d+),\s*(\d+),\s*\x27/' 2>/dev/null)
    c=$(printf '%s' "$packer" | perl -ne 'print $2 if /,\s*(\d+),\s*(\d+),\s*\x27/' 2>/dev/null)
    [[ -z "$a" ]] && a=$(echo "$packer" | sed -E "s/.*\}\('[^']*',([0-9]+),([0-9]+),.*/\1/")
    [[ -z "$c" ]] && c=$(echo "$packer" | sed -E "s/.*\}\('[^']*',([0-9]+),([0-9]+),.*/\2/")

    # p: primer string entre }( y ',a,c,'
    p=$(printf '%s' "$packer" | sed -E "s/.*\}\('(.*)',[0-9]+,[0-9]+,'.*/\1/")

    # k: string entre ,a,c,' y '.split(...)
    k=$(printf '%s' "$packer" | perl -ne 'print $1 if /,\s*\d+,\s*\d+,\s*\x27([^\x27]*)\x27\.split/' 2>/dev/null)
    [[ -z "$k" ]] && k=$(echo "$packer" | sed -E "s/.*,[0-9]+,[0-9]+,(?:'([^']*)'|\"([^\"]*)\")\.split.*/\1\2/")

    log "packer: a=$a c=$c |p|=${#p} |k|=${#k}"
    [[ -z "$p" || -z "$k" ]] && return 1

    IFS='|' read -ra K_ARRAY <<< "$k"
    local out="$p"
    local total=${#K_ARRAY[@]}

    # IMPORTANTE: orden inverso como el packer original (while(c--))
    for (( i=total-1; i>=0; i-- )); do
        [[ -z "${K_ARRAY[$i]}" ]] && continue
        local token
        token=$(dec_to_base "$i" "$a")
        local replacement="${K_ARRAY[$i]//&/\\&}"
        out=$(printf '%s' "$out" | sed -E "s/\\b${token}\\b/${replacement}/g")
    done
    echo "$out"
}

# ==================================================================
# Extracción de URL dentro de contenido
# ==================================================================

extract_url_from_content() {
    local content="$1"
    local link=""

    # 1) URL directa .m3u8 / .mp4
    link=$(echo "$content" | grep -oP 'https?://[^"'\'' ]+\.(m3u8|mp4)[^"'\'' ]*' | head -1)
    [[ -n "$link" ]] && { echo "$link"; return 0; }

    # 2) file: / src: con extensión
    link=$(echo "$content" | grep -oP '(?:file|src|source|url)\s*[:=]\s*["'"'"']\K[^"'"'"']+\.(m3u8|mp4)[^"'"'"']*' | head -1)
    [[ -n "$link" ]] && { echo "$link"; return 0; }

    # 3) sources: [ "..." ]
    link=$(echo "$content" | grep -oP 'sources\s*:\s*\[\s*["'"'"']\K[^"'"'"']+' | head -1)
    [[ -n "$link" ]] && { echo "$link"; return 0; }

    # 4) Concatenación JS: "a"+"b"+"c"
    local concat
    concat=$(echo "$content" | grep -oP '"(?:[^"\\]|\\.)*"(?:\s*\+\s*"(?:[^"\\]|\\.)*")+' | head -1)
    if [[ -n "$concat" ]]; then
        link=$(echo "$concat" | grep -oP '"[^"]*"' | tr -d '"' | tr -d '\n')
        if [[ "$link" =~ https?:// ]] && [[ "$link" =~ \.(m3u8|mp4) ]]; then
            echo "$link"
            return 0
        fi
    fi

    # 5) Escapes \x2f \x3a
    link=$(echo "$content" | grep -oE 'https?(:|%3A|\\x3a|\\x3A)(//|%2F%2F|\\x2f\\x2f|\\x2F\\x2F)[^"'\'' ]+\.m3u8[^"'\'' ]*' | head -1)
    [[ -n "$link" ]] && { echo "$link" | sed -E 's/\\x([0-9a-fA-F]{2})/%\1/g'; return 0; }

    # 6) base64 embebido que decodifica a m3u8
    local b64
    for b64 in $(echo "$content" | grep -oP '[A-Za-z0-9+/=]{60,}' | head -5); do
        local dec
        dec=$(echo "$b64" | base64 -d 2>/dev/null)
        if echo "$dec" | grep -qE '\.(m3u8|mp4)'; then
            link=$(echo "$dec" | grep -oP 'https?://[^"'\'' ]+\.(m3u8|mp4)[^"'\'' ]*' | head -1)
            [[ -n "$link" ]] && { echo "$link"; return 0; }
        fi
    done

    # 7) master.m3u8 relativo
    link=$(echo "$content" | grep -oE '[^"'\'' ]*master\.m3u8[^"'\'' ]*' | head -1)
    [[ -n "$link" ]] && { echo "$link"; return 0; }

    # 8) Construcción por patrón image → hls2/<path>_/master.m3u8
    local img
    img=$(echo "$content" | grep -oE 'https?://[^"'\'' ]+/i/[0-9]+/[0-9]+/[a-zA-Z0-9]+\.(jpg|jpeg|png|webp)' | head -1)
    if [[ -n "$img" ]]; then
        local base path_mid built
        base=$(echo "$img" | sed -E 's|(https?://[^/]+).*|\1|')
        path_mid=$(echo "$img" | grep -oE '/i/[0-9]+/[0-9]+/[a-zA-Z0-9]+' | sed -E 's|^/i/|/hls2/|; s|$|_|')
        built="${base}${path_mid}/master.m3u8"
        log "construido por patrón image: $built"
        echo "$built"
        return 0
    fi

    return 1
}

# ==================================================================
# Métodos de extracción
# ==================================================================

method_pass_md5() {
    log "-> método 1 (pass_md5)"
    local redirect u2 resp pass final token expiry rand
    redirect=$(timeout 10 curl -s -I -o /dev/null -w '%{redirect_url}' \
        --connect-timeout 4 --max-time 8 \
        -H "User-Agent: $UA" "$URL" 2>/dev/null)
    [[ -z "$redirect" ]] && redirect="$URL"
    u2=$(echo "$redirect" | sed 's|https://[^/]*|https://d000d.com|')
    resp=$(fetch "$u2")
    pass=$(echo "$resp" | grep -oE '/pass_md5/[a-zA-Z0-9_-]+/[a-zA-Z0-9_-]+' | head -1)
    [[ -z "$pass" ]] && return 1
    final=$(fetch "https://d0000d.com$pass" -H "Referer: https://d0000d.com/")
    [[ -z "$final" ]] && return 1
    token=$(echo "$pass" | awk -F'/' '{print $NF}')
    expiry=$(($(date +%s%N) / 1000000))
    rand=$(tr -dc 'a-zA-Z0-9' < /dev/urandom | head -c 10)
    echo "${final}${rand}?token=${token}&expiry=${expiry}"
}

method_botlink() {
    log "-> método 2 (botlink)"
    local id data link
    id=$(echo "$URL" | sed -nE 's/.*\/e\/([a-zA-Z0-9]+).*/\1/p')
    [[ -z "$id" ]] && return 1
    for d in strtape.cloud streamtape.com streamtape.to; do
        data=$(fetch "https://$d/e/$id")
        [[ -z "$data" ]] && continue
        link=$(echo "$data" | grep -oP "id=['\"]botlink['\"][^>]*>[^<]*" | head -1 | sed -E "s/.*>[^']*'([^']*)'\s*\+\s*'([^']*)'.*/\1\2/")
        [[ -z "$link" ]] && link=$(echo "$data" | grep -oP "getElementById\('botlink'\).*?'\K[^']+'" | head -1)
        [[ -z "$link" ]] && link=$(echo "$data" | grep -oP "\.innerHTML\s*=\s*'([^']*)'\s*\+\s*\K[^;]+" | head -1 | tr -d "'\"")
        if [[ -n "$link" ]]; then
            echo "https://$d/get_video?id=${link}&stream=1"
            return 0
        fi
    done
    return 1
}

method_redirect_b64() {
    log "-> método 3 (redirect + base64)"
    local content loc newcontent link
    content=$(fetch "$URL")
    [[ -z "$content" ]] && return 1
    loc=$(echo "$content" | grep -oP "window\.location\.href\s*=\s*['\"]?\K[^'\";]+" | head -1)
    [[ -z "$loc" ]] && loc=$(echo "$content" | grep -oP "location\.href\s*=\s*['\"]?\K[^'\";]+" | head -1)
    newcontent="$content"
    if [[ -n "$loc" ]]; then
        [[ "$loc" =~ ^// ]] && loc="https:$loc"
        newcontent=$(fetch "$loc")
        [[ -z "$newcontent" ]] && return 1
    fi
    link=$(echo "$newcontent" | grep -oP "'mp4':\s*'\K[^']+" | head -1)
    [[ -n "$link" ]] && { echo "$link" | base64 -d 2>/dev/null; return 0; }
    link=$(extract_url_from_content "$newcontent")
    [[ -n "$link" ]] && { echo "$link"; return 0; }
    return 1
}

method_packer() {
    log "-> método 4 (packer)"
    local content unpacked link
    content=$(fetch "$URL")
    [[ -z "$content" ]] && return 1
    echo "$content" > "$TMP"

    if echo "$content" | grep -q "eval(function(p,a,c,k,e,d)"; then
        unpacked=$(unpack_packer "$content")
        [[ -n "$unpacked" ]] && { echo "$unpacked" > "$TMP_UNPACK"; content="$unpacked"; }
    fi

    link=$(extract_url_from_content "$content")
    [[ -n "$link" ]] && { echo "$link"; return 0; }
    return 1
}

method_sources() {
    log "-> método 5 (sources/file/src/video)"
    local content link embed
    embed="$URL"
    if [[ "$URL_PATH" =~ ^/([a-zA-Z0-9]+)/?$ ]]; then
        embed="${BASE_URL}/embed-${BASH_REMATCH[1]}.html"
    fi
    content=$(fetch "$embed")
    [[ -z "$content" ]] && content=$(fetch "$URL")
    [[ -z "$content" ]] && return 1
    link=$(extract_url_from_content "$content")
    [[ -n "$link" ]] && { echo "$link"; return 0; }
    return 1
}

method_m3u8() {
    log "-> método 6 (m3u8)"
    local content link
    content=$(fetch "$URL")
    [[ -z "$content" ]] && return 1
    link=$(echo "$content" | grep -oP '(?<=file:["'"'"'])[^"'"'"']*\.m3u8[^"'"'"']*' | head -1)
    [[ -z "$link" ]] && link=$(echo "$content" | grep -oP '(?<=src=["'"'"'])[^"'"'"']*\.m3u8[^"'"'"']*' | head -1)
    [[ -z "$link" ]] && link=$(extract_url_from_content "$content")
    [[ -n "$link" ]] && { echo "$link"; return 0; }
    return 1
}

method_generic() {
    log "-> método 7 (genérico + packer + js externos)"
    local content unpacked link js jsc
    content=$(fetch "$URL")
    [[ -z "$content" ]] && return 1

    if echo "$content" | grep -q "eval(function(p,a,c,k,e,d)"; then
        log "packer en genérico"
        unpacked=$(unpack_packer "$content")
        [[ -n "$unpacked" ]] && content="$content
$unpacked"
    fi

    link=$(extract_url_from_content "$content")
    [[ -n "$link" ]] && { echo "$link"; return 0; }

    # Buscar en scripts JS externos
    for js in $(echo "$content" | grep -oP 'src="\K[^"]+\.js[^"]*' | head -5); do
        [[ "$js" =~ ^https?:// ]] || js="${BASE_URL}${js}"
        log "descargando js: $js"
        jsc=$(fetch "$js")
        [[ -z "$jsc" ]] && continue
        link=$(extract_url_from_content "$jsc")
        [[ -n "$link" ]] && { echo "$link"; return 0; }
    done
    return 1
}

# ==================================================================
# Dispatcher
# ==================================================================

normalize() {
    local l="$1"
    [[ -z "$l" ]] && return 1
    [[ "$l" =~ ^// ]] && l="https:$l"
    [[ "$l" =~ ^/ ]] && l="${BASE_URL}$l"
    [[ "$l" =~ ^https?:// ]] || return 1
    echo "$l"
}

RESULT=""
CANDIDATES=()

# Solo métodos relevantes según el patrón de la URL
if [[ "$URL_PATH" =~ ^/[edv]/[a-zA-Z0-9]+/?$ ]]; then
    CANDIDATES+=(method_pass_md5)
    CANDIDATES+=(method_redirect_b64)
fi

if [[ "$URL_PATH" =~ ^/e/[a-zA-Z0-9]+/?$ ]]; then
    CANDIDATES+=(method_botlink)
fi

CANDIDATES+=(method_packer)

if [[ "$URL_PATH" =~ ^/[a-zA-Z0-9]+/?$ ]] || [[ "$URL_PATH" =~ embed- ]]; then
    CANDIDATES+=(method_sources)
fi

CANDIDATES+=(method_m3u8)
CANDIDATES+=(method_generic)

log "métodos candidatos: ${CANDIDATES[*]}"

# Las funciones ya tienen timeout interno en curl (fetch),
# por eso NO usamos "timeout" externo (no puede invocar funciones bash).
for method in "${CANDIDATES[@]}"; do
    log "probando $method"
    if (( DEBUG )); then
        raw=$("$method")
    else
        raw=$("$method" 2>/dev/null)
    fi
    if [[ -n "$raw" ]]; then
        norm=$(normalize "$raw")
        if [[ -n "$norm" ]]; then
            log "éxito con $method: $norm"
            RESULT="$norm"
            break
        fi
    fi
done

if [[ -n "$RESULT" ]]; then
    echo "$RESULT"
    exit 0
fi

# Recursión a iframe si existe
if [[ -s "$TMP" ]]; then
    IFRAME=$(grep -oP '<iframe[^>]+src="\K[^"]+' "$TMP" | head -1)
    if [[ -n "$IFRAME" ]]; then
        [[ "$IFRAME" =~ ^// ]] && IFRAME="https:$IFRAME"
        [[ "$IFRAME" =~ ^/ ]] && IFRAME="${BASE_URL}$IFRAME"
        log "recursión a iframe: $IFRAME"
        exec "$0" ${DEBUG:+--debug} "$IFRAME"
    fi
fi

log "sin resultado"
exit 1
