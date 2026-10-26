# Building the collection

One repo holds the launcher and the three games:

| Path | What |
|---|---|
| `src/`, `CMakeLists.txt`, `build.bat` | The launcher (Dear ImGui, SDL2, OpenGL; the TRG Launcher in `third_party/trg-launcher`) |
| `games/sneak_king`, `games/big_bumpin`, `games/pocketbike_racer` | The three ports (ReXGlue projects) |
| `games/shared/src` | Code the three games share: achievement notifications, the way back to the launcher, render quality modes, the game window, the FPE guard, the achievement list export |
| `tools/` | The release script, and the source of the collection's logo |

## Requirements

* Visual Studio 2022 with the C++ tools (the games build with its LLVM clang), CMake 3.25+, Ninja, Python 3.
* The ReXGlue SDK with TekRant Gaming's patches (FSR 1 and NIS among them), installed to an install prefix. Link it
  as `tools/rexglue/win-amd64` (a junction to the SDK's `out/install/win-amd64`).

## The launcher

```
build.bat
```

Output: `build/whopping_launcher.exe`.

## A game

Each game needs code generated from your own disc's `default.xex` (never committed: it is derived from the game):

1. Extract the disc image into `games/<game>/assets` (the folder that holds `default.xex`).
2. Run the ReXGlue codegen with the game's `<game>_manifest.toml` (it writes `games/<game>/generated/`).
3. Build: `games/<game>/build.bat <short>-release`, where short is `bk` (Sneak King), `bb` (Big Bumpin') or
   `pr` (PocketBike Racer).

Output: `games/<game>/out/build/<short>-release/<game>.exe`.

## Releases

Set `VERSION` to the release's version (the launcher shows it, and every zip's file list carries it), build each
game's release, then:

```
powershell -ExecutionPolicy Bypass -File tools\make_release.ps1 -Version 1.0.0 -Out dist\releases
```

Writes `dist\releases\v1.0.0\` with the launcher zip, the three game zips and one `SHA256SUMS.txt`, each zip with
its file list for the updater. Every zip goes through VirusTotal before a release is published.

## Linux

The Linux build runs in WSL (`Ubuntu-24.04`, clang 20) with a Linux build of the same patched ReXGlue SDK (Vulkan) in
`/root/whopping-linux/sdk`. From Windows:

```
wsl -d Ubuntu-24.04 -u root -- bash /mnt/d/BK-whopping-launcher/tools/linux/sync_to_wsl.sh
wsl -d Ubuntu-24.04 -u root -- bash /mnt/d/BK-whopping-launcher/tools/linux/build_games.sh
wsl -d Ubuntu-24.04 -u root -- bash /mnt/d/BK-whopping-launcher/tools/linux/build_launcher.sh
wsl -d Ubuntu-24.04 -u root -- bash /mnt/d/BK-whopping-launcher/tools/linux/build_appimage.sh 1.0.0
```

The last one writes `dist\linux\WhoppingCollection-v1.0.0-linux-x86_64.AppImage`: the launcher and the three games in
one AppImage (the launcher starts each game from beside itself), with Noto Sans (OFL) for the launcher. Its line goes
into the release's `SHA256SUMS.txt` beside the zips, and the AppImage goes on both releases: the one players see and
the packages release the launcher updates itself from.
