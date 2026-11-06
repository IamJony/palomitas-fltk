#!/bin/sh
# By IamJony
set -e

D=$(cd "$(dirname "$0")" && pwd)
APP=palomitas
VER=1.0.1
OUT="$D/tcz"
STAGE="$D/tcz-stage"
PKG_DIR="/usr/local/share/palomitas"

# --- Verificaciones previas ---
[ -x "$D/palomitas" ] || { echo "ERROR: compila primero (make)"; exit 1; }
[ -f "$D/palomitas-cli/palomitas.sh" ] || { echo "ERROR: falta palomitas-cli/palomitas.sh"; exit 1; }
[ -f "$D/palomitas-cli/modules/search.sh" ] || { echo "ERROR: falta palomitas-cli/modules/search.sh"; exit 1; }

# --- Instalar squashfs-tools si falta ---
if ! command -v mksquashfs >/dev/null 2>&1; then
    tce-load -wi squashfs-tools >/dev/null 2>&1 || true
fi
command -v mksquashfs >/dev/null 2>&1 || { echo "ERROR: falta mksquashfs"; exit 1; }

MKSQ_OPTS="-all-root -noappend -no-progress -b 4096"
if mksquashfs 2>&1 | grep -q -- "-no-xattrs"; then
    MKSQ_OPTS="$MKSQ_OPTS -no-xattrs"
fi

# --- Preparar stage ---
rm -rf "$STAGE"
mkdir -p "$STAGE/usr/local/bin"
mkdir -p "$STAGE$PKG_DIR/asset"
mkdir -p "$STAGE$PKG_DIR/palomitas-cli/modules"
mkdir -p "$STAGE/usr/local/share/pixmaps"
mkdir -p "$STAGE/usr/local/share/applications"

# *** Binario DENTRO de la carpeta del paquete ***
install -m 755 "$D/palomitas" "$STAGE$PKG_DIR/palomitas"

# CLI principal
install -m 755 "$D/palomitas-cli/palomitas.sh" "$STAGE$PKG_DIR/palomitas-cli/palomitas.sh"

# Módulos CLI (todos los *.sh)
for f in "$D/palomitas-cli/modules/"*.sh; do
    [ -f "$f" ] || continue
    install -m 755 "$f" "$STAGE$PKG_DIR/palomitas-cli/modules/"
done

# Asset (home.jpeg)
[ -f "$D/asset/home.jpeg" ] && \
    install -m 644 "$D/asset/home.jpeg" "$STAGE$PKG_DIR/asset/home.jpeg"

# --- Wrappers en /usr/local/bin ---
# GUI: el .desktop debe llamar a este wrapper
cat > "$STAGE/usr/local/bin/palomitas" <<'WRAPPER'
#!/bin/sh
PKG=/usr/local/share/palomitas
cd "$PKG" || exit 1
exec "$PKG/palomitas" "$@"
WRAPPER
chmod 755 "$STAGE/usr/local/bin/palomitas"

# CLI
cat > "$STAGE/usr/local/bin/palomitas-cli" <<'WRAPPER'
#!/bin/sh
PKG=/usr/local/share/palomitas
cd "$PKG/palomitas-cli" || exit 1
exec bash ./palomitas.sh "$@"
WRAPPER
chmod 755 "$STAGE/usr/local/bin/palomitas-cli"

# --- Icono ---
[ -f "$D/data/palomitas.png" ] && \
    install -m 644 "$D/data/palomitas.png" "$STAGE/usr/local/share/pixmaps/palomitas.png"


cat > "$STAGE/usr/local/share/applications/palomitas.desktop" <<'DESK'
[Desktop Entry]
Type=Application
Name=palomitas
Comment=Mira peliculas en espanol
Exec=/usr/local/bin/palomitas
Icon=palomitas
X-FullPathIcon=/usr/local/share/pixmaps/palomitas.png
Terminal=false
Categories=Video;AudioVideo;
DESK

# --- Permisos ---
find "$STAGE" -type d -exec chmod 755 {} +

# --- Empaquetar ---
mkdir -p "$OUT"
mksquashfs "$STAGE" "$OUT/$APP.tcz" $MKSQ_OPTS >/dev/null
(cd "$STAGE" && find . -type f | sed 's|^\./|/|' | sort) > "$OUT/$APP.tcz.list"
rm -rf "$STAGE"

# --- Dependencias ---
printf '%s\n' fltk-1.3.tcz jq.tcz bash.tcz perl5.tcz curl.tcz wget.tcz yt-dlp.tcz mplayer-cli.tcz > "$OUT/$APP.tcz.dep"

# --- Checksum ---
(cd "$OUT" && md5sum "$APP.tcz" > "$APP.tcz.md5.txt")

# --- Info ---
SIZE=$(du -k "$OUT/$APP.tcz" | cut -f1)
cat > "$OUT/$APP.tcz.info" <<INFO
Title:          $APP.tcz
Description:    palomitas: ver peliculas en espanol
Version:        $VER
Author:         IamJony
Original-site:  https://github.com/IamJony/palomitas-fltk
Copying-policy: MIT
Size:           ${SIZE}K
Extension_by:   IamJony
Tags:           peliculas video fltk streaming cli
Comments:       Incluye GUI (palomitas) y CLI (palomitas-cli).
Change-log:     $(date +%Y/%m/%d) $VER
Current:        $(date +%Y/%m/%d) $VER
INFO

echo "OK: $OUT/$APP.tcz (${SIZE}K)"
