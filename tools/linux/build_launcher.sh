#!/usr/bin/env bash
# Builds the launcher for Linux in the WSL build folder (after sync_to_wsl.sh).
#   wsl -d Ubuntu-24.04 -u root -- bash /mnt/d/BK-whopping-launcher/tools/linux/build_launcher.sh
set -uo pipefail
WORK=${WHOPPING_LINUX:-/root/whopping-linux}
cd "$WORK/src" || exit 1
log="$WORK/launcher-build.log"
{ cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 &&
  cmake --build build-linux -j 12 -- -k 0; } > "$log" 2>&1
rc=$?
echo "launcher: exit $rc"
grep -E "error:|undefined reference" "$log" | sort -u | head -40
[ $rc -eq 0 ] && ls -la build-linux/whopping_launcher
exit $rc
