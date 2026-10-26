#!/usr/bin/env bash
# Build a Ubuntu 24.04 (noble) amd64 .deb that uses system Qt6.
#
# Follows Ubuntu/Debian packaging practice for a binary package:
# - Debian binary package layout (DEBIAN/control + payload)
# - Package-owned .desktop under /usr/share/applications (not written by postinst)
# - Icon in the hicolor theme (Icon=throne)
# - /usr/share/doc/<pkg>/{copyright,changelog.Debian.gz}
# - dpkg-deb --root-owner-group -Zzstd (Ubuntu 24 default compressor)
# - Depends list for noble Qt6 (t64 names with non-t64 alternatives)
#
# Usage:
#   THRONE_BIN=./build/Throne THRONE_CORE=./build/ThroneCore \
#     ./script/pack_ubuntu24_amd64.sh [version]
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DIST_LINUX="$ROOT/dist/linux"
# Non-native version (has debian revision) so changelog.Debian.gz is the correct name.
VERSION="${1:-1.0.2+al4al.dev-1}"
ARCH=amd64
THRONE_BIN=${THRONE_BIN:-$ROOT/build/Throne}
THRONE_CORE=${THRONE_CORE:-$ROOT/build/ThroneCore}
ICON_SRC=${ICON_SRC:-$ROOT/res/public/Throne.png}
OUT_DIR=${OUT_DIR:-$ROOT/deployment}
DEB_NAME="throne_${VERSION}_${ARCH}.deb"

for f in "$THRONE_BIN" "$THRONE_CORE" "$ICON_SRC"; do
  [[ -f $f ]] || { echo "missing: $f" >&2; exit 1; }
done

command -v dpkg-deb >/dev/null
command -v desktop-file-validate >/dev/null
command -v gzip >/dev/null

PKG=$(mktemp -d)
trap 'rm -rf "$PKG"' EXIT
chmod 0755 "$PKG"

# --- payload layout (FHS-ish for a proprietary-path desktop app) ---
mkdir -p \
  "$PKG/DEBIAN" \
  "$PKG/opt/Throne" \
  "$PKG/usr/share/applications" \
  "$PKG/usr/share/icons/hicolor/512x512/apps" \
  "$PKG/usr/share/doc/throne" \
  "$PKG/usr/lib/systemd/system" \
  "$PKG/etc/throne"

install -m 0755 "$THRONE_BIN" "$PKG/opt/Throne/Throne"
install -m 0755 "$THRONE_CORE" "$PKG/opt/Throne/ThroneCore"
# Keep a copy next to the binary for portable/Icon= path fallbacks upstream used.
install -m 0644 "$ICON_SRC" "$PKG/opt/Throne/Throne.png"
install -m 0644 "$ICON_SRC" "$PKG/usr/share/icons/hicolor/512x512/apps/throne.png"

# Strip debug from the GUI binary (local RelWithDebInfo builds are huge).
# Do not ship .debug in the installable .deb (optional separate -dbgsym later).
if command -v strip >/dev/null; then
  strip --strip-debug --strip-unneeded "$PKG/opt/Throne/Throne"
fi
rm -f "$PKG/opt/Throne/Throne.debug"

# Desktop entry as package payload (Ubuntu desktop / Debian Policy: not generated in postinst).
# Exec uses -appdata so configs live under the user config dir, not /opt.
cat >"$PKG/usr/share/applications/throne.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Version=1.0
Name=Throne
GenericName=Proxy Manager
Comment=Qt based cross-platform GUI proxy utility (sing-box / Xray)
Exec=/opt/Throne/Throne -appdata
Icon=throne
Terminal=false
Categories=Network;
Keywords=proxy;vpn;sing-box;xray;vless;nekoray;
StartupNotify=true
StartupWMClass=Throne
EOF
desktop-file-validate "$PKG/usr/share/applications/throne.desktop"

install -m 0644 "$DIST_LINUX/throne.service" "$PKG/usr/lib/systemd/system/throne.service"
install -m 0644 "$DIST_LINUX/throne.env" "$PKG/etc/throne/throne.env"
printf '%s\n' /etc/throne/throne.env >"$PKG/DEBIAN/conffiles"

# glibc floor from packed ELF (same idea as upstream pack_debian.sh)
GLIBC_NEED=$(
  (find "$PKG/opt/Throne" -type f \( -name Throne -o -name ThroneCore -o -name '*.so*' \) -exec readelf -V -W {} + 2>/dev/null || true) \
    | { grep -o 'GLIBC_[0-9][0-9.]*' || true; } | sed 's/^GLIBC_//' | sort -Vu | tail -n1
)

# Noble Qt6 ships *t64 package names; keep non-t64 alternatives for jammy-like systems.
DEPENDS="desktop-file-utils, libqt6core6t64 | libqt6core6, libqt6gui6t64 | libqt6gui6, libqt6network6t64 | libqt6network6, libqt6widgets6t64 | libqt6widgets6, libqt6dbus6t64 | libqt6dbus6, qt6-qpa-plugins, qt6-wayland, qt6-gtk-platformtheme, qt6-xdgdesktopportal-platformtheme, libxcb-cursor0, fonts-noto-color-emoji, libegl1, libgl1, libopengl0, libxkbcommon0, libx11-6, ca-certificates"
if [[ -n $GLIBC_NEED ]]; then
  DEPENDS="libc6 (>= $GLIBC_NEED), $DEPENDS"
fi

SIZE_KB=$(du -sk "$PKG" | awk '{print $1}')

cat >"$PKG/DEBIAN/control" <<EOF
Package: throne
Version: $VERSION
Architecture: $ARCH
Maintainer: AL4AL <noreply@github.com>
Section: net
Priority: optional
Homepage: https://github.com/AL4AL/Throne
Installed-Size: $SIZE_KB
Depends: $DEPENDS
Description: Qt GUI proxy utility (sing-box / Xray) — AL4AL desktop fork
 Throne is a Qt-based desktop proxy manager powered by sing-box and Xray.
 This AL4AL package is the desktop successor to archived AL4AL/nekoray and
 targets Ubuntu 24.04 with system Qt6 (not a bundled Qt tree).
EOF

cat >"$PKG/usr/share/doc/throne/copyright" <<'EOF'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: Throne
Upstream-Contact: https://github.com/throneproj/Throne
Source: https://github.com/AL4AL/Throne

Files: *
Copyright: 2024-2026 Throne contributors
Copyright: 2026 AL4AL
License: GPL-3.0-or-later

License: GPL-3.0-or-later
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 .
 On Debian systems, the full text of the GNU General Public License
 version 3 can be found in "/usr/share/common-licenses/GPL-3".
EOF

cat >"$PKG/usr/share/doc/throne/changelog.Debian" <<EOF
throne ($VERSION) noble; urgency=medium

  * Local Ubuntu 24.04 amd64 system-Qt package for AL4AL/Throne.
  * Desktop entry and hicolor icon shipped as package payload.
  * Includes Throne GUI built from AL4AL/Throne and ThroneCore for runtime.

 -- AL4AL <noreply@github.com>  $(date -R)
EOF
gzip -9n "$PKG/usr/share/doc/throne/changelog.Debian"

cat >"$PKG/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
# Package owns the .desktop file; only refresh caches / systemd units.
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true
command -v update-icon-caches >/dev/null 2>&1 && update-icon-caches /usr/share/icons/hicolor >/dev/null 2>&1 || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
if [ "$1" = configure ] && [ -d /run/systemd/system ]; then
    systemctl daemon-reload >/dev/null 2>&1 || true
    if [ -n "$2" ]; then
        systemctl try-restart throne.service >/dev/null 2>&1 || true
    fi
fi
exit 0
EOF

cat >"$PKG/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = remove ] && [ -d /run/systemd/system ]; then
    systemctl stop throne.service >/dev/null 2>&1 || true
fi
if [ "$1" = remove ] && command -v systemctl >/dev/null 2>&1; then
    systemctl --no-reload disable throne.service >/dev/null 2>&1 || true
fi
exit 0
EOF

cat >"$PKG/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload >/dev/null 2>&1 || true
fi
exit 0
EOF

chmod 0755 "$PKG/DEBIAN/postinst" "$PKG/DEBIAN/prerm" "$PKG/DEBIAN/postrm"

# Policy: directories 0755, non-executable data 0644 (umask must not leak group-writable bits).
find "$PKG" -type d -exec chmod 0755 {} +
find "$PKG" -type f -exec chmod a-s,go-w {} +
chmod 0644 "$PKG/usr/share/applications/throne.desktop" \
  "$PKG/usr/share/doc/throne/"* \
  "$PKG/usr/share/icons/hicolor/512x512/apps/throne.png" \
  "$PKG/opt/Throne/Throne.png" \
  "$PKG/etc/throne/throne.env" \
  "$PKG/usr/lib/systemd/system/throne.service"
chmod 0755 "$PKG/opt/Throne/Throne" "$PKG/opt/Throne/ThroneCore" \
  "$PKG/DEBIAN/postinst" "$PKG/DEBIAN/prerm" "$PKG/DEBIAN/postrm"

mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/$DEB_NAME"
# Ubuntu 24.04 uses zstd-compressed debs by default in many paths; Policy allows it.
dpkg-deb -Zzstd -z19 --root-owner-group --build "$PKG" "$OUT"
echo "built $OUT"
dpkg-deb -I "$OUT"
echo "---"
dpkg-deb -c "$OUT" | head -40
