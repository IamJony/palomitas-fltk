#!/bin/sh
# By IamJony
set -e

D=$(cd "$(dirname "$0")" && pwd)
APP=palomitas
VER=1.0.0
OUT="$D/tcz"

# Verificar que el binario y los scripts existen
[ -x "$D/palomitas-fltk" ] || { echo "ERROR: run make in $D first"; exit 1; }
[ -f "$D/palomitas-cli/Palomitas.sh" ] || { echo "ERROR: falta palomitas-cli/Palomitas.sh"; exit 1; }

command -v mksquashfs >/dev/null || tce-load -i squashfs-tools >/dev/null

P=$(mktemp -d)
chmod 755 "$P"

# Directorios
install -d "$P/usr/local/bin"
install -d "$P/usr/local/bin/palomitas-cli"
install -d "$P/usr/local/share/pixmaps"
install -d "$P/usr/local/share/applications"

# Binario GUI
install -m 755 "$D/palomitas-fltk" "$P/usr/local/bin/palomitas-fltk"

# Scripts CLI (bajo /usr/local/bin/palomitas-cli/)
install -m 755 "$D/palomitas-cli/Palomitas.sh" "$P/usr/local/bin/palomitas-cli/Palomitas.sh"
install -m 755 "$D/palomitas-cli/search.sh"    "$P/usr/local/bin/palomitas-cli/search.sh"
install -m 755 "$D/palomitas-cli/iframe.sh"    "$P/usr/local/bin/palomitas-cli/iframe.sh"
install -m 755 "$D/palomitas-cli/embed.sh"     "$P/usr/local/bin/palomitas-cli/embed.sh"
install -m 644 "$D/palomitas-cli/config.sh"    "$P/usr/local/bin/palomitas-cli/config.sh"

# Wrapper CLI: /usr/local/bin/palomitas-cli → ejecuta el .sh
cat > "$P/usr/local/bin/palomitas-cli" <<'WRAPPER'
#!/bin/sh
exec /usr/local/bin/palomitas-cli/Palomitas.sh "$@"
WRAPPER
chmod 755 "$P/usr/local/bin/palomitas-cli"

# Icono y .desktop (GUI y CLI)
install -m 644 "$D/data/palomitas.png" "$P/usr/local/share/pixmaps/palomitas.png"
install -m 644 "$D/palomitas.desktop" "$P/usr/local/share/applications/palomitas.desktop"
install -m 644 "$D/palomitas-cli.desktop" "$P/usr/local/share/applications/palomitas-cli.desktop"

# Permisos
find "$P" -type d -exec chmod 755 {} +

# Empaquetar
mkdir -p "$OUT"
mksquashfs "$P" "$OUT/$APP.tcz" -all-root -noappend -no-progress -b 4096 -no-xattrs >/dev/null
(cd "$P" && find usr -type f | sed 's|^|/|' | sort) > "$OUT/$APP.tcz.list"
rm -rf "$P"

printf '%s\n' \
    fltk-1.3.tcz \
    curl.tcz \
    jq.tcz \
    yt-dlp.tcz \
    mplayer.tcz \
    > "$OUT/$APP.tcz.dep"

# Checksum
(cd "$OUT" && md5sum $APP.tcz > $APP.tcz.md5.txt)

# Info
SIZE=$(du -k "$OUT/$APP.tcz" | cut -f1)
cat > "$OUT/$APP.tcz.info" <<INFO
Title:          $APP.tcz
Description:    palomitas: ver peliculas en espanol
Version:        $VER
Author:         IamJony
Original-site:  https://github.com/IamJony
Copying-policy: MIT
Size:           ${SIZE}K
Extension_by:   IamJony
Tags:           peliculas video fltk streaming cli
Comments:       Incluye GUI (palomitas-fltk) y CLI (palomitas-cli).
                GUI: interfaz FLTK con grilla de caratulas.
                CLI: menu de terminal para buscar y reproducir.
                Requiere curl, jq, yt-dlp y mplayer.
Change-log:     $(date +%Y/%m/%d) $VER primera version
Current:        $(date +%Y/%m/%d) $VER
INFO

echo "OK: $OUT/$APP.tcz (${SIZE}K)"
