#!/usr/bin/env bash
# Copies the collection into the Linux build folder in WSL (Ubuntu-24.04), so the Linux build runs on the Linux
# file system (fast, and file names keep their case). Each game's generated code and the TRG launcher come along.
#   wsl -d Ubuntu-24.04 -u root -- bash /mnt/d/BK-whopping-launcher/tools/linux/sync_to_wsl.sh
set -euo pipefail
SRC=/mnt/d/BK-whopping-launcher
WORK=${WHOPPING_LINUX:-/root/whopping-linux}
mkdir -p "$WORK/src"
# Each game's default.xex comes along (the build regenerates its code with the Linux rexglue); the rest of the
# extracted disc stays behind. -L: the generated code folders are junctions on Windows (symlinks here); copy what they point at.
rsync -aL --delete \
  --exclude '/build/' --exclude '/build-linux/' --exclude '/dist/' --exclude '/runs/' --exclude '/tools/rexglue/' --exclude '/ref/' \
  --exclude 'out/' --include '/games/*/assets/' --include '/games/*/assets/default.xex' --exclude '/games/*/assets/*' --exclude '.git/' --exclude '/third_party/trg-launcher/build/' \
  "$SRC/" "$WORK/src/"
mkdir -p "$WORK/src/tools/rexglue"
ln -sfn "$WORK/sdk" "$WORK/src/tools/rexglue/linux-amd64"
echo "synced to $WORK/src"
