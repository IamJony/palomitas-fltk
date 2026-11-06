#!/bin/bash
# Palomitas CLI by IamJony

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_DIR="$BASE_DIR/modules"
CONFIG_FILE="$SCRIPT_DIR/config.sh"
STATE_FILE="$BASE_DIR/.palomitas_state"

if [ ! -f "$CONFIG_FILE" ]; then
    printf '%b\n' "\033[0;31mError: No se encuentra $CONFIG_FILE\033[0m"
    exit 1
fi

source "$CONFIG_FILE"

SERVER_FLAG="--server1"

if [ -f "$STATE_FILE" ]; then
    SAVED=$(cat "$STATE_FILE" 2>/dev/null | tr -d '[:space:]')
    case "$SAVED" in
        --server1|--server2) SERVER_FLAG="$SAVED" ;;
    esac
fi

save_state() {
    echo "$SERVER_FLAG" > "$STATE_FILE" 2>/dev/null
}

select_server_scripts() {
    case "$SERVER_FLAG" in
        --server1)
            SEARCH_SCRIPT="$SCRIPT_DIR/search.sh"
            IFRAME_SCRIPT="$SCRIPT_DIR/iframe.sh"
            EMBED_SCRIPT="$SCRIPT_DIR/embed.sh"
            EPISODES_SCRIPT=""
            ;;
        --server2)
            SEARCH_SCRIPT="$SCRIPT_DIR/search2.sh"
            IFRAME_SCRIPT="$SCRIPT_DIR/iframe2.sh"
            EMBED_SCRIPT="$SCRIPT_DIR/embed2.sh"
            EPISODES_SCRIPT="$SCRIPT_DIR/episodes2.sh"
            ;;
        *)
            SERVER_FLAG="--server1"
            SEARCH_SCRIPT="$SCRIPT_DIR/search.sh"
            IFRAME_SCRIPT="$SCRIPT_DIR/iframe.sh"
            EMBED_SCRIPT="$SCRIPT_DIR/embed.sh"
            EPISODES_SCRIPT=""
            ;;
    esac
}

if [ -f "$CONFIG_FILE" ]; then
    source "$CONFIG_FILE" "$SERVER_FLAG"
else
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
    BLUE='\033[0;34m'; CYAN='\033[0;36m'; MAGENTA='\033[0;35m'; NC='\033[0m'
fi

select_server_scripts

SEARCH_RESULTS=""
SELECTED_MOVIE=""
MOVIE_URL=""
PLAYER_URL=""
STREAM_URL=""
STREAM_TYPE=""
CONTENT_TYPE="movies"

DEFAULT_PLAYER="mplayer"

reload_config() {
    source "$CONFIG_FILE" "$SERVER_FLAG"
    select_server_scripts
    save_state
}

detect_player() {
    if command -v mpv &> /dev/null; then
        DEFAULT_PLAYER="mpv"
    elif command -v mplayer &> /dev/null; then
        DEFAULT_PLAYER="mplayer"
    else
        DEFAULT_PLAYER=""
    fi
}

clear_screen() {
    printf "\033c"
}

hr() {
    printf '%b\n' "${CYAN}--------------------------------------------------${NC}"
}

show_banner() {
    clear_screen
    local srv_label="SERVER2"
    [[ "$SERVER_FLAG" == "--server1" ]] && srv_label="SERVER1"
    printf "${CYAN}"
    printf "+---------------------------------------------+\n"
    printf "         ${YELLOW}Palomitas by IamJony v1.0.1${CYAN}  \n"
    printf "|   https://github.com/IamJony/palomitas-cli  |\n"
    printf "|       Buscar -> Extraer -> Reproducir       |\n"
    printf "+---------------------------------------------+\n"
    printf "        Servidor activo: ${GREEN}%-22s${CYAN}\n" "$srv_label"
    printf "+---------------------------------------------+\n"
    printf "${NC}"
}

show_main_menu() {
    printf "${CYAN}=== MENU PRINCIPAL ===${NC}\n\n"
    printf "  1) ${GREEN}Buscar peliculas${NC}\n"
    printf "  2) ${BLUE}Reproducir desde URL${NC}\n"
    printf "  3) ${YELLOW}Limpiar archivos${NC}\n"
    printf "  4) ${MAGENTA}Cambiar servidor${NC}\n"
    printf "  0) ${RED}Salir${NC}\n\n"
    printf "Selecciona una opcion: "
}

select_server() {
    printf "${CYAN}=== SELECCIONAR SERVIDOR ===${NC}\n\n"
    printf "  1) ${GREEN}Server1${NC}\n"
    printf "  2) ${BLUE}Server2${NC}\n"
    printf "  0) ${YELLOW}Volver${NC}\n\n"
    while true; do
        printf "Servidor: "
        read -r srv
        case "$srv" in
            1) SERVER_FLAG="--server1"; reload_config; printf "${GREEN}[OK] Server1 (guardado)${NC}\n"; sleep 1; return ;;
            2) SERVER_FLAG="--server2"; reload_config; printf "${GREEN}[OK] Server2 (guardado)${NC}\n"; sleep 1; return ;;
            0) return ;;
            *) printf "${RED}[X] Opcion invalida${NC}\n" ;;
        esac
    done
}

search_movies() {
    show_banner
    printf "${CYAN}=== BUSCAR PELICULAS ($SERVER) ===${NC}\n\n"

    if [ $# -eq 0 ]; then
        printf "Que pelicula buscas?: "
        read -r query
    else
        query="$*"
    fi

    if [ -z "$query" ]; then
        printf "${RED}[X] No se ingreso termino de busqueda${NC}\n"
        sleep 2
        return
    fi

    printf "\n${YELLOW}Buscando: $query${NC}\n\n"

    if [ -f "$SEARCH_SCRIPT" ]; then
        bash "$SEARCH_SCRIPT" "$query"
    else
        printf "${RED}[X] Error: No se encuentra $SEARCH_SCRIPT${NC}\n"
        sleep 2
        return
    fi

    if [ ! -f "$TEMP_DIR/buscar.json" ]; then
        printf "${RED}[X] No se encontraron resultados${NC}\n"
        sleep 2
        return
    fi

    process_search_results
}

process_search_results() {
    local results=()
    local titles=()
    local count=0

    if command -v jq &> /dev/null; then
        while IFS= read -r line; do
            if [ -n "$line" ]; then
                results+=("$line")
                titles+=("$(echo "$line" | cut -d'|' -f2)")
                ((count++))
            fi
        done < <(jq -r 'to_entries[] | "\(.key)|\(.value.title)|\(.value.url)"' "$TEMP_DIR/buscar.json" 2>/dev/null)
    else
        printf "${RED}[X] Necesitas instalar jq${NC}\n"
        sleep 3
        return
    fi

    if [ $count -eq 0 ]; then
        printf "${RED}[X] No se encontraron resultados${NC}\n"
        sleep 2
        return
    fi

    SEARCH_RESULTS=("${results[@]}")
    show_movie_selection "$count" "${titles[@]}"
}

show_movie_selection() {
    local count=$1
    shift
    local titles=("$@")

    printf "${GREEN}[OK] Encontradas $count peliculas:${NC}\n\n"

    for i in "${!titles[@]}"; do
        local entry="${SEARCH_RESULTS[$i]}"
        local entry_url
        entry_url=$(echo "$entry" | cut -d'|' -f3)
        local mtype
        mtype=$(jq -r --arg url "$entry_url" '
            to_entries[] | select(.value.url == $url) | .value.type // "?"
        ' "$TEMP_DIR/buscar.json" 2>/dev/null | head -1)

        if [ -z "$mtype" ] || [ "$mtype" == "?" ] || [ "$mtype" == "null" ]; then
            case "$entry_url" in
                */series/*) mtype="tvshows" ;;
                */animes/*) mtype="animes" ;;
                */pelicula/*|*/peliculas/*) mtype="movies" ;;
                *) mtype="movies" ;;
            esac
        fi

        printf "  ${CYAN}%2d)${NC} %s ${YELLOW}[%s]${NC}\n" $((i+1)) "${titles[$i]}" "$mtype"
    done

    printf "\n  ${YELLOW}0)${NC} Volver al menu principal\n\n"

    while true; do
        printf "Selecciona una pelicula (1-$count): "
        read -r choice
        if [[ "$choice" == "0" ]]; then return; fi
        if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "$count" ]; then
            select_movie $((choice-1))
            break
        else
            printf "${RED}[X] Opcion invalida. Intenta de nuevo.${NC}\n"
        fi
    done
}

select_movie() {
    local index=$1
    local selected="${SEARCH_RESULTS[$index]}"
    SELECTED_MOVIE=$(echo "$selected" | cut -d'|' -f2)
    MOVIE_URL=$(echo "$selected" | cut -d'|' -f3)
    printf "\n${GREEN}[OK] Seleccionada: $SELECTED_MOVIE${NC}\n\n"
    process_movie
}

select_episode_server2() {
    local post_id="$1"
    local post_type="$2"

    if [ ! -f "$EPISODES_SCRIPT" ]; then
        printf "${RED}[X] No se encuentra $EPISODES_SCRIPT${NC}\n"
        return 1
    fi

    printf "\n${YELLOW}Obteniendo episodios...${NC}\n"

    bash "$EPISODES_SCRIPT" "$post_id" "$post_type" 2>/dev/null
    if [ $? -ne 0 ]; then
        printf "${RED}[X] Error al obtener episodios${NC}\n"
        return 1
    fi

    local episodes_file="$TEMP_DIR/episodes_${post_id}_clean.json"
    if [ ! -f "$episodes_file" ]; then
        printf "${RED}[X] No se pudieron obtener los episodios${NC}\n"
        return 1
    fi

    local seasons=()
    mapfile -t seasons < <(jq -r '[.[].season] | unique | sort | .[]' "$episodes_file")

    if [ ${#seasons[@]} -eq 0 ]; then
        printf "${RED}[X] No hay temporadas disponibles${NC}\n"
        return 1
    fi

    local selected_season=""

    if [ ${#seasons[@]} -eq 1 ]; then
        selected_season="${seasons[0]}"
        printf "${GREEN}[OK] Temporada unica: $selected_season${NC}\n"
    else
        printf "\n${CYAN}=== TEMPORADAS DISPONIBLES ===${NC}\n"
        local i=1
        for s in "${seasons[@]}"; do
            local count
            count=$(jq --arg s "$s" '[.[] | select(.season == ($s|tonumber))] | length' "$episodes_file")
            printf "  ${CYAN}%2d)${NC} Temporada %s (%s episodios)\n" "$i" "$s" "$count"
            ((i++))
        done
        printf "  ${YELLOW} 0)${NC} Volver\n\n"

        while true; do
            printf "Selecciona temporada (1-${#seasons[@]}): "
            read -r season_choice
            if [[ "$season_choice" == "0" ]]; then return 1; fi
            if [[ "$season_choice" =~ ^[0-9]+$ ]] && [ "$season_choice" -ge 1 ] && [ "$season_choice" -le "${#seasons[@]}" ]; then
                selected_season="${seasons[$((season_choice-1))]}"
                break
            fi
            printf "${RED}[X] Opción inválida${NC}\n"
        done
    fi

    local season_eps="$TEMP_DIR/season_${post_id}_${selected_season}.json"
    jq --arg s "$selected_season" '[.[] | select(.season == ($s|tonumber))]' \
        "$episodes_file" > "$season_eps"

    local ep_count
    ep_count=$(jq 'length' "$season_eps")

    if [ "$ep_count" == "0" ]; then
        printf "${RED}[X] No hay episodios en la temporada $selected_season${NC}\n"
        return 1
    fi

    local ep_min ep_max
    ep_min=$(jq -r '[.[].episode] | min' "$season_eps")
    ep_max=$(jq -r '[.[].episode] | max' "$season_eps")

    printf "\n${CYAN}=== TEMPORADA $selected_season ===${NC}\n"
    printf "  Episodios disponibles: ${GREEN}E%s${NC} a ${GREEN}E%s${NC} (%s en total)\n\n" \
        "$ep_min" "$ep_max" "$ep_count"
    printf "  ${YELLOW}0)${NC} Volver\n\n"

    local ep_num=""
    while true; do
        printf "Escribe el número de episodio (E%s-E%s): " "$ep_min" "$ep_max"
        read -r ep_input
        if [[ "$ep_input" == "0" ]]; then return 1; fi
        if ! [[ "$ep_input" =~ ^[0-9]+$ ]]; then
            printf "${RED}[X] Debes escribir un número${NC}\n"
            continue
        fi
        if jq -e --argjson e "$ep_input" 'any(.[]; .episode == $e)' "$season_eps" > /dev/null 2>&1; then
            ep_num="$ep_input"
            break
        else
            printf "${RED}[X] No existe el episodio E%s en la temporada %s${NC}\n" "$ep_input" "$selected_season"
        fi
    done

    local ep_id ep_type ep_rating ep_runtime
    ep_id=$(jq -r --argjson e "$ep_num" '.[] | select(.episode == $e) | .id' "$season_eps")
    ep_type=$(jq -r --argjson e "$ep_num" '.[] | select(.episode == $e) | .type' "$season_eps")
    ep_rating=$(jq -r --argjson e "$ep_num" '.[] | select(.episode == $e) | .rating' "$season_eps")
    ep_runtime=$(jq -r --argjson e "$ep_num" '.[] | select(.episode == $e) | .runtime' "$season_eps")

    local finale_tag=""
    [[ "$ep_type" == "finale" ]] && finale_tag=" (Final)"

    printf "\n${GREEN}[OK] Seleccionado: S%sE%s%s - %smin - IMDb: %s${NC}\n" \
        "$selected_season" "$ep_num" "$finale_tag" "$ep_runtime" "$ep_rating"
    printf "${CYAN}[INFO] ID del episodio: $ep_id${NC}\n\n"

    SELECTED_MOVIE="$SELECTED_MOVIE - S${selected_season}E${ep_num}"

    bash "$IFRAME_SCRIPT" "$ep_id" "episodes"
    return $?
}

process_movie() {
    printf "${YELLOW}Extrayendo iframe...${NC}\n"

    if [ ! -f "$IFRAME_SCRIPT" ]; then
        printf "${RED}[X] Error: No se encuentra $IFRAME_SCRIPT${NC}\n"
        sleep 2
        return
    fi

    if [[ "$SERVER_FLAG" == "--server2" ]]; then
        local post_id post_type
        post_id=$(jq -r --arg url "$MOVIE_URL" '
            to_entries[] | select(.value.url == $url) | .key
        ' "$TEMP_DIR/buscar.json" 2>/dev/null | head -1)

        post_type=$(jq -r --arg url "$MOVIE_URL" '
            to_entries[] | select(.value.url == $url) | .value.type // "movies"
        ' "$TEMP_DIR/buscar.json" 2>/dev/null | head -1)

        if [ -z "$post_type" ] || [ "$post_type" == "null" ]; then
            post_type="movies"
            [[ "$MOVIE_URL" =~ /series/ ]] && post_type="tvshows"
            [[ "$MOVIE_URL" =~ /animes/ ]] && post_type="animes"
        fi

        if [ -z "$post_id" ]; then
            printf "${RED}[X] No se pudo resolver el ID para server2${NC}\n"
            sleep 2
            return
        fi

        printf "${CYAN}[INFO] ID: $post_id | Tipo: $post_type${NC}\n"

        if [[ "$post_type" == "tvshows" || "$post_type" == "animes" ]]; then
            if ! select_episode_server2 "$post_id" "$post_type"; then
                return
            fi
        else
            bash "$IFRAME_SCRIPT" "$post_id" "$post_type"
        fi
    else
        bash "$IFRAME_SCRIPT" "$MOVIE_URL"
    fi

    if [ ! -f "$TEMP_DIR/peli.json" ]; then
        printf "${RED}[X] No se pudo extraer el iframe${NC}\n"
        sleep 2
        return
    fi

    PLAYER_URL=""
    if command -v jq &> /dev/null; then
        PLAYER_URL=$(jq -r '.embed_url // .url // ""' "$TEMP_DIR/peli.json" 2>/dev/null)
        CONTENT_TYPE=$(jq -r '.type // "movies"' "$TEMP_DIR/peli.json" 2>/dev/null)
    fi

    if [ -z "$PLAYER_URL" ] || [ "$PLAYER_URL" == "null" ]; then
        PLAYER_URL=$(grep -o '"embed_url"[[:space:]]*:[[:space:]]*"[^"]*"' "$TEMP_DIR/peli.json" | head -1 | sed -E 's/.*"embed_url"[[:space:]]*:[[:space:]]*"//; s/"$//; s|\\/|/|g')
    fi
    if [ -z "$PLAYER_URL" ] || [ "$PLAYER_URL" == "null" ]; then
        PLAYER_URL=$(grep -o '"url"[[:space:]]*:[[:space:]]*"[^"]*"' "$TEMP_DIR/peli.json" | head -1 | sed -E 's/.*"url"[[:space:]]*:[[:space:]]*"//; s/"$//; s|\\/|/|g')
    fi

    if [ -z "$PLAYER_URL" ]; then
        printf "${RED}[X] No se pudo obtener URL del player${NC}\n"
        printf "Revisa el archivo $TEMP_DIR/peli.json manualmente\n"
        sleep 3
        return
    fi

    if [ -z "$CONTENT_TYPE" ] || [ "$CONTENT_TYPE" == "null" ]; then
        CONTENT_TYPE="movies"
    fi

    printf "${GREEN}[OK] Player encontrado${NC}\n"
    printf "${CYAN}[INFO] Tipo de contenido: $CONTENT_TYPE${NC}\n"
    printf "${BLUE}[EMBED] $PLAYER_URL${NC}\n\n"

    extract_stream
}

extract_stream() {
    printf "${YELLOW}Extrayendo stream...${NC}\n"

    if [ ! -f "$EMBED_SCRIPT" ]; then
        printf "${RED}[X] Error: No se encuentra $EMBED_SCRIPT${NC}\n"
        sleep 2
        return
    fi

    local RAW_OUTPUT
    RAW_OUTPUT=$(bash "$EMBED_SCRIPT" "$PLAYER_URL" 2>/dev/null)

    if [ -z "$RAW_OUTPUT" ]; then
        printf "${RED}[X] No se pudo obtener URL del stream${NC}\n"
        printf "Prueba manual: bash $EMBED_SCRIPT --debug \"$PLAYER_URL\"\n"
        sleep 3
        return
    fi

    STREAM_URL=""
    local TRIMMED="${RAW_OUTPUT#"${RAW_OUTPUT%%[![:space:]]*}"}"
    if [[ "$TRIMMED" == "{"* ]]; then
        if command -v jq &> /dev/null; then
            STREAM_URL=$(echo "$RAW_OUTPUT" | jq -r '.video // ""' 2>/dev/null)
        fi
        if [ -z "$STREAM_URL" ]; then
            STREAM_URL=$(echo "$RAW_OUTPUT" | grep -oP '"video"\s*:\s*"\K[^"]+' | head -1)
        fi
    else
        STREAM_URL="$RAW_OUTPUT"
    fi

    if [ -z "$STREAM_URL" ] || [ "$STREAM_URL" == "null" ]; then
        printf "${RED}[X] No se pudo obtener URL del stream${NC}\n"
        sleep 3
        return
    fi

    STREAM_TYPE="direct"
    echo "$STREAM_URL" | grep -q "m3u8" && STREAM_TYPE="hls"

    cat > "$TEMP_DIR/output.json" << EOF
{
  "embed": "$PLAYER_URL",
  "video": "$STREAM_URL",
  "subtitle": "",
  "preview": "",
  "type": "$STREAM_TYPE",
  "content_type": "$CONTENT_TYPE",
  "server": "$SERVER"
}
EOF

    printf "${GREEN}[OK] Stream extraido correctamente!${NC}\n"
    printf "${BLUE}Tipo: $STREAM_TYPE${NC}\n"
    printf "${MAGENTA}[HLS] $STREAM_URL${NC}\n\n"

    play_stream
}

get_referrer() {
    echo "$PLAYER_URL" | grep -oP '^https?://[^/]+'
}

play_with_mpv() {
    local referrer
    referrer=$(get_referrer)
    mpv --referrer="$referrer" \
        --title="$SELECTED_MOVIE" \
        --force-media-title="$SELECTED_MOVIE" \
        "$STREAM_URL"
}

play_with_mplayer() {
    local titulo="$SELECTED_MOVIE"
    local m3u8_url="$STREAM_URL"
    local referer
    referer=$(get_referrer)
    local ua="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/123.0.0.0 Safari/537.36"

    echo ""
    hr
    printf '%b\n' "${YELLOW}INICIANDO REPRODUCCION${NC}"
    hr
    echo ""
    printf '%b\n' "${GREEN}Reproduciendo: $titulo${NC}"

    sleep 1

    if ! command -v mplayer &> /dev/null; then
        printf '%b\n' "${RED}MPlayer no esta instalado${NC}"
        echo ""
        printf '%b\n' "${YELLOW}URL para reproducir manualmente:${NC}"
        echo "mplayer -zoom -cache 4096 -demuxer lavf \"$m3u8_url\""
        return 1
    fi

    if ! command -v yt-dlp &> /dev/null; then
        printf '%b\n' "${RED}yt-dlp no esta instalado${NC}"
        echo ""
        printf '%b\n' "${YELLOW}URL para reproducir manualmente:${NC}"
        echo "mplayer -zoom -cache 4096 -demuxer lavf \"$m3u8_url\""
        return 1
    fi

    echo ""
    printf '%b\n' "${YELLOW}> Reproduciendo con mplayer (via yt-dlp)...${NC}"

    yt-dlp --add-header "Referer: $referer" \
        --add-header "User-Agent: $ua" \
        -S "res:480" -o - "$m3u8_url" \
        2>"${TEMP_DIR}/ytdlp.log" \
        | mplayer -zoom -vf scale=640:480 -cache 6144 -demuxer lavf \
            -title "$titulo" - \
            >"${TEMP_DIR}/mplayer.log" 2>&1
}

play_stream() {
    detect_player

    printf "${CYAN}=== REPRODUCIR ===${NC}\n\n"
    printf "Pelicula: $SELECTED_MOVIE\n"
    printf "Tipo: ${YELLOW}$CONTENT_TYPE${NC}\n\n"
    printf "Opciones:\n"
    printf "  1) ${GREEN}Reproducir con mpv (best)${NC}"
    command -v mpv &> /dev/null || printf " ${RED}(no instalado)${NC}"
    printf "\n  2) ${MAGENTA}Reproducir con mplayer (480p)${NC}"
    command -v mplayer &> /dev/null || printf " ${RED}(no instalado)${NC}"
    command -v yt-dlp &> /dev/null || printf " ${RED}(falta yt-dlp)${NC}"
    printf "\n  3) ${BLUE}Descargar stream${NC}\n"
    printf "  4) ${YELLOW}Volver al menu${NC}\n\n"

    while true; do
        printf "Selecciona opcion (1-4): "
        read -r choice
        case $choice in
            1)
                if ! command -v mpv &> /dev/null; then
                    printf "${RED}[X] mpv no esta instalado${NC}\n"
                    continue
                fi
                printf "\n${GREEN}> Iniciando reproduccion con mpv...${NC}\n"
                printf "${YELLOW}Presiona 'q' para salir del reproductor${NC}\n"
                play_with_mpv
                ;;
            2)
                if ! command -v mplayer &> /dev/null; then
                    printf "${RED}[X] mplayer no esta instalado${NC}\n"
                    continue
                fi
                if ! command -v yt-dlp &> /dev/null; then
                    printf "${RED}[X] yt-dlp no esta instalado (necesario para mplayer)${NC}\n"
                    continue
                fi
                printf "\n${MAGENTA}> Iniciando reproduccion con mplayer...${NC}\n"
                printf "${YELLOW}Presiona 'q' para salir del reproductor${NC}\n"
                play_with_mplayer
                ;;
            3) echo "OPCION AUN NO DISPONIBLE" ;;
            4) return ;;
            *) clear; play_stream ;;
        esac
    done
}

play_from_url() {
    show_banner
    printf "${CYAN}=== REPRODUCIR DESDE URL ===${NC}\n\n"
    printf "Formato de URL: https://xxxxx.com/pelicula/nombre-pelicula/\n"
    printf "Ingresa la URL: "
    read -r url

    if [ -z "$url" ]; then
        printf "${RED}[X] No se ingreso URL${NC}\n"
        sleep 2
        return
    fi

    MOVIE_URL="$url"
    SELECTED_MOVIE="Pelicula desde URL"
    printf "\n${YELLOW}Procesando: $url${NC}\n"
    process_movie
}

cleanup_files() {
    show_banner
    printf "${CYAN}=== LIMPIAR ARCHIVOS ===${NC}\n\n"
    rm -f "$TEMP_DIR"/* 2>/dev/null
    printf "${GREEN}[OK] Archivos temporales eliminados${NC}\n"
    sleep 2
}

check_dependencies() {
    local missing=()
    if ! command -v curl &> /dev/null && ! command -v wget &> /dev/null; then
        missing+=("curl o wget")
    fi
    if ! command -v jq &> /dev/null; then
        missing+=("jq")
    fi
    if [ ${#missing[@]} -gt 0 ]; then
        printf "${RED}[X] Faltan dependencias:${NC}\n"
        for dep in "${missing[@]}"; do
            printf "  - $dep\n"
        done
        printf "\nInstala con:\n"
        printf "  Tiny Core: tce-load -wi ${missing[*]}\n"
        printf "  Ubuntu/Debian: sudo apt install ${missing[*]}\n\n"
        read -p "Presiona Enter para continuar de todos modos..."
    fi
    if ! command -v mpv &> /dev/null && ! command -v mplayer &> /dev/null; then
        printf "${YELLOW}[!] No se detecto ningun reproductor (mpv o mplayer).${NC}\n"
        printf "Instala uno con:\n"
        printf "  sudo apt install mpv\n"
        printf "  sudo apt install mplayer\n\n"
        read -p "Presiona Enter para continuar..."
    fi
    if ! command -v yt-dlp &> /dev/null; then
        printf "${YELLOW}[!] yt-dlp no esta instalado (necesario para mplayer).${NC}\n"
        printf "Instala con:\n"
        printf "  sudo apt install yt-dlp\n"
        printf "  o: pip install yt-dlp\n\n"
    fi
}

command_line_mode() {
    case "$1" in
        search|s)
            shift
            search_movies "$@"
            if [ -n "$MOVIE_URL" ] && [ "$2" == "--play" ]; then
                process_movie
                if [ -n "$STREAM_URL" ]; then
                    detect_player
                    if [ -n "$DEFAULT_PLAYER" ]; then
                        printf "\n${GREEN}> Iniciando reproduccion automatica con $DEFAULT_PLAYER...${NC}\n"
                        if [ "$DEFAULT_PLAYER" == "mpv" ]; then play_with_mpv; else play_with_mplayer; fi
                    fi
                fi
            fi
            ;;
        play|p)
            shift
            if [ $# -eq 0 ]; then
                play_from_url
            else
                MOVIE_URL="$1"
                SELECTED_MOVIE="Pelicula desde CLI"
                process_movie
                if [ -n "$STREAM_URL" ]; then
                    detect_player
                    if [ -n "$DEFAULT_PLAYER" ]; then
                        if [ "$DEFAULT_PLAYER" == "mpv" ]; then play_with_mpv; else play_with_mplayer; fi
                    fi
                fi
            fi
            ;;
        clean|c) cleanup_files ;;
        help|h) show_help ;;
        *)
            printf "${RED}Comando desconocido: $1${NC}\n"
            show_help
            exit 1
            ;;
    esac
    exit 0
}

show_help() {
    show_banner
    printf "${CYAN}=== AYUDA ===${NC}\n\n"
    printf "Modo interactivo:\n  $0\n\n"
    printf "Modo linea de comandos:\n"
    printf "  $0 search <termino>          Buscar peliculas\n"
    printf "  $0 search <termino> --play   Buscar y reproducir primera\n"
    printf "  $0 play <url>                Reproducir desde URL\n"
    printf "  $0 clean                     Limpiar archivos\n"
    printf "  $0 --server1                 Forzar server1\n"
    printf "  $0 --server2                 Forzar server2\n\n"
    printf "Alias:\n  s, search    Buscar\n  p, play      Reproducir\n  c, clean     Limpiar\n\n"
}

main() {
    local pos_args=()
    for arg in "$@"; do
        case "$arg" in
            --server1) SERVER_FLAG="--server1"; reload_config ;;
            --server2) SERVER_FLAG="--server2"; reload_config ;;
            *) pos_args+=("$arg") ;;
        esac
    done

    if [ ${#pos_args[@]} -gt 0 ]; then
        command_line_mode "${pos_args[@]}"
    fi

    check_dependencies

    while true; do
        show_banner
        show_main_menu
        read -r choice
        case $choice in
            1) search_movies ;;
            2) play_from_url ;;
            3) cleanup_files ;;
            4) select_server ;;
            0) printf "\n${GREEN}Hasta luego!${NC}\n\n"; exit 0 ;;
            *) printf "\n${RED}[X] Opcion invalida${NC}\n"; sleep 1 ;;
        esac
    done
}

trap 'printf "\n\n${RED}[X] Interrumpido por el usuario${NC}\n"; exit 1' INT

main "$@"
