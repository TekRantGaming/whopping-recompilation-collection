#!/usr/bin/env bash
# Builds the collection's Linux AppImage in WSL (Ubuntu-24.04) from the launcher and the three games built by
# build_launcher.sh and build_games.sh: one AppImage, the launcher starts each game from beside itself.
# Made after King Kong Recompiled's tools/build_appimage.sh (its notes: run from a copy, see AppRun).
#
#   wsl -d Ubuntu-24.04 -u root -- bash /mnt/d/BK-whopping-launcher/tools/linux/build_appimage.sh <version>
#
# Output: <repo>/dist/linux/WhoppingCollection-v<version>-linux-x86_64.AppImage
set -euo pipefail
VERSION="${1:?version}"
WORK=${WHOPPING_LINUX:-/root/whopping-linux}
SRC="$WORK/src"
SDK="$WORK/sdk"
OUTDIR=/mnt/d/BK-whopping-launcher/dist/linux
APPDIR="$WORK/AppDir"
TOOL="${WHOPPING_APPIMAGETOOL:-/root/kk-linux-beta/work/appimagetool-x86_64.AppImage}"

echo "== AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib"
cp "$SRC/build-linux/whopping_launcher" "$APPDIR/usr/bin/"
cp "$SRC/games/sneak_king/out/build/bk-linux-release/sneak_king" "$APPDIR/usr/bin/"
cp "$SRC/games/big_bumpin/out/build/bb-linux-release/big_bumpin" "$APPDIR/usr/bin/"
cp "$SRC/games/pocketbike_racer/out/build/pr-linux-release/pocketbike_racer" "$APPDIR/usr/bin/"
# The runtime and the GPU plugin the games load beside themselves.
for lib in librexruntime.so librexgpu-xenos.so libTracyClient.so; do
  [ ! -e "$SDK/lib/$lib" ] || cp -a "$SDK/lib/$lib" "$APPDIR/usr/bin/"
done
strip --strip-unneeded "$APPDIR/usr/bin/"* 2>/dev/null || true
# The SDK needs a GCC 13 C++ runtime (GLIBCXX_3.4.32); bundled for older distributions (AppRun decides).
for lib in libstdc++.so.6 libgcc_s.so.1; do
  cp -L "/usr/lib/x86_64-linux-gnu/$lib" "$APPDIR/usr/lib/"
done
# The launcher's font (Noto Sans, OFL), which it looks for in usr/share/fonts.
mkdir -p "$APPDIR/usr/share/fonts" "$APPDIR/usr/share/licenses/noto-sans"
cp "$SRC/tools/linux/fonts/"NotoSans-*.ttf "$APPDIR/usr/share/fonts/"
cp "$SRC/tools/linux/fonts/OFL.txt" "$APPDIR/usr/share/licenses/noto-sans/"
cp /mnt/d/BK-whopping-launcher/docs/steam/icon.png "$APPDIR/whopping_collection.png"
cat > "$APPDIR/whopping_collection.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=The Whopping Recompilation Collection
Comment=Sneak King, Big Bumpin' and PocketBike Racer, native PC ports of the Xbox 360 games
Exec=whopping_launcher
Icon=whopping_collection
Categories=Game;
Terminal=false
EOF
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
# Run from a copy outside the AppImage's mount (as King Kong Recompiled does). The mount is served by a helper
# process that ends as soon as it gets a signal; Steam signals every process of a game at once when it stops it,
# so a game still quitting could lose its own code and crash. The copy is made once per AppImage file (its size
# and time: an update gets a new one) under ~/.cache/whopping_collection/appimage; older copies are removed.
# WHOPPING_APPIMAGE_COPY=0 runs from the mount.
if [ -n "$APPIMAGE" ] && [ "${WHOPPING_APPIMAGE_COPY:-1}" != 0 ]; then
  ID="$(stat -c '%s-%Y' "$APPIMAGE" 2>/dev/null)"
  BASE="${XDG_CACHE_HOME:-$HOME/.cache}/whopping_collection/appimage"
  if [ -n "$ID" ] && [ ! -f "$BASE/$ID/.complete" ]; then
    for old in "$BASE"/*; do [ "$old" = "$BASE/$ID" ] || rm -rf "$old"; done
    PART="$BASE/.part-$$"
    rm -rf "$BASE/$ID"
    if mkdir -p "$PART" && cp -a "$HERE/." "$PART/" && touch "$PART/.complete"; then
      mv -T "$PART" "$BASE/$ID" 2>/dev/null || rm -rf "$PART"
    else
      rm -rf "$PART"
    fi
  fi
  if [ -n "$ID" ] && [ -f "$BASE/$ID/.complete" ]; then HERE="$BASE/$ID"; fi
fi
LIBS="$HERE/usr/bin"
# The bundled C++ runtime only when the system's is older than the one the games need; a newer system copy is
# kept for the graphics drivers.
SYS_CXX="$(ldconfig -p 2>/dev/null | awk '/libstdc\+\+\.so\.6 .*x86-64/ {print $NF; exit}')"
if [ -z "$SYS_CXX" ] || ! grep -q GLIBCXX_3.4.32 "$SYS_CXX" 2>/dev/null; then LIBS="$LIBS:$HERE/usr/lib"; fi
export LD_LIBRARY_PATH="$LIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/bin/whopping_launcher" "$@"
EOF
chmod +x "$APPDIR/AppRun"

echo "== Game data check"
# Nothing from the games may ship: the player's own disc provides it.
if find "$APPDIR" \( -iname '*.xex' -o -iname 'image.bin' -o -iname '*.iso' -o -iname '*.xmv' -o -iname '*.wmv' \) | grep .; then
  echo "error: files from the game are in the AppDir (above)" >&2
  exit 1
fi

echo "== Missing libraries check"
for f in whopping_launcher sneak_king big_bumpin pocketbike_racer librexruntime.so librexgpu-xenos.so; do
  [ -f "$APPDIR/usr/bin/$f" ] || { echo "error: missing $f" >&2; exit 1; }
  if LD_LIBRARY_PATH="$APPDIR/usr/bin:$APPDIR/usr/lib" ldd "$APPDIR/usr/bin/$f" | grep 'not found'; then
    echo "error: $f needs a library that is not there" >&2
    exit 1
  fi
done

echo "== AppImage"
mkdir -p "$OUTDIR"
OUT="$OUTDIR/WhoppingCollection-v$VERSION-linux-x86_64.AppImage"
rm -f "$OUT"
ARCH=x86_64 "$TOOL" --appimage-extract-and-run "$APPDIR" "$OUT" > "$WORK/appimagetool.log" 2>&1 || { tail -20 "$WORK/appimagetool.log"; exit 1; }
ls -la "$OUT"
sha256sum "$OUT"
