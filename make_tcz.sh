#!/bin/sh
# By IamJony
set -e

D=$(cd "$(dirname "$0")" && pwd)
APP=palomitas
VER=1.0.0
OUT="$D/tcz"
STAGE="$D/tcz-stage"

# Verificar binario y scripts
[ -x "$D/palomitas" ] || { echo "ERROR: compila primero (make)"; exit 1; }
[ -f "$D/palomitas-cli/Palomitas.sh" ] || { echo "ERROR: falta palomitas-cli/Palomitas.sh"; exit 1; }

# Instalar squashfs-tools si falta
if ! command -v mksquashfs >/dev/null 2>&1; then
    tce-load -wi squashfs-tools >/dev/null 2>&1 || true
fi
command -v mksquashfs >/dev/null 2>&1 || { echo "ERROR: falta mksquashfs"; exit 1; }

# Preparar stage (raíz falsa)
rm -rf "$STAGE"
mkdir -p "$STAGE/usr/local/bin/palomitas-cli"
mkdir -p "$STAGE/usr/local/share/pixmaps"
mkdir -p "$STAGE/usr/local/share/applications"

# Binario GUI (renombrado a palomitas-fltk dentro del paquete)
install -m 755 "$D/palomitas" "$STAGE/usr/local/bin/palomitas-fltk"

# Scripts CLI en /usr/local/bin/palomitas-cli/
install -m 755 "$D/palomitas-cli/Palomitas.sh" "$STAGE/usr/local/bin/palomitas-cli/Palomitas.sh"
install -m 755 "$D/palomitas-cli/search.sh"    "$STAGE/usr/local/bin/palomitas-cli/search.sh"
install -m 755 "$D/palomitas-cli/iframe.sh"    "$STAGE/usr/local/bin/palomitas-cli/iframe.sh"
install -m 755 "$D/palomitas-cli/embed.sh"     "$STAGE/usr/local/bin/palomitas-cli/embed.sh"
install -m 644 "$D/palomitas-cli/config.sh"    "$STAGE/usr/local/bin/palomitas-cli/config.sh"
install -m 644 "$D/palomitas-cli/util.sh"      "$STAGE/usr/local/bin/palomitas-cli/util.sh"

# Wrapper CLI en /usr/local/bin/palomitas-cli-launcher (no choca con el directorio)
cat > "$STAGE/usr/local/bin/palomitas-cli-launcher" <<'WRAPPER'
#!/bin/sh
cd /usr/local/bin/palomitas-cli/ && exec bash ./Palomitas.sh "$@"
WRAPPER
chmod 755 "$STAGE/usr/local/bin/palomitas-cli-launcher"

# Icono y .desktop (condicionales)
[ -f "$D/data/palomitas.png" ] && install -m 644 "$D/data/palomitas.png" "$STAGE/usr/local/share/pixmaps/palomitas.png"
[ -f "$D/palomitas.desktop" ] && install -m 644 "$D/palomitas.desktop" "$STAGE/usr/local/share/applications/palomitas.desktop"
[ -f "$D/palomitas-cli.desktop" ] && install -m 644 "$D/palomitas-cli.desktop" "$STAGE/usr/local/share/applications/palomitas-cli.desktop"

# Permisos
find "$STAGE" -type d -exec chmod 755 {} +

# Empaquetar
mkdir -p "$OUT"
mksquashfs "$STAGE" "$OUT/$APP.tcz" -all-root -noappend -no-progress -b 4096 -no-xattrs >/dev/null
(cd "$STAGE" && find usr -type f | sed 's|^|/|' | sort) > "$OUT/$APP.tcz.list"

# Limpiar stage
rm -rf "$STAGE"

# Dependencias
printf '%s\n' fltk-1.3.tcz curl.tcz jq.tcz bash.tcz curl.tcz yt-dlp.tcz mplayer-cli.tcz > "$OUT/$APP.tcz.dep"

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
Comments:       Incluye GUI (palomitas-fltk) y CLI (palomitas-cli-launcher).
Change-log:     $(date +%Y/%m/%d) $VER primera version
Current:        $(date +%Y/%m/%d) $VER
INFO

echo "OK: $OUT/$APP.tcz (${SIZE}K)"
