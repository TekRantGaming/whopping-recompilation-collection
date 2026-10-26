#!/usr/bin/env bash
# Builds the three games for Linux in the WSL build folder (after sync_to_wsl.sh).
#   wsl -d Ubuntu-24.04 -u root -- bash /mnt/d/BK-whopping-launcher/tools/linux/build_games.sh [game...]
set -uo pipefail
WORK=${WHOPPING_LINUX:-/root/whopping-linux}
GAMES=("$@")
[ ${#GAMES[@]} -gt 0 ] || GAMES=(sneak_king big_bumpin pocketbike_racer)
status=0
for d in "${GAMES[@]}"; do
  case "$d" in
    sneak_king) s=bk ;;
    big_bumpin) s=bb ;;
    pocketbike_racer) s=pr ;;
    *) echo "unknown game $d"; exit 2 ;;
  esac
  cd "$WORK/src/games/$d" || exit 1
  log="$WORK/$d-build.log"
  # $ORIGIN: the game finds the runtime beside itself (inside the AppImage).
  { cmake --preset "$s-linux-release" -DCMAKE_BUILD_RPATH='$ORIGIN' && cmake --build "out/build/$s-linux-release" -j 12 -- -k 0; } > "$log" 2>&1
  rc=$?
  echo "$d: exit $rc"
  grep -E "error:" "$log" | sort -u | head -10
  [ $rc -eq 0 ] && ls -la "out/build/$s-linux-release/$d" || status=1
done
exit $status
