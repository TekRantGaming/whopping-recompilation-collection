# The Whopping Recompilation Collection

All three Burger King Xbox 360 games in one collection, as native Windows PC ports:
**Sneak King**, **Big Bumpin'** and **PocketBike Racer**.

Each game is a static recompilation of the original Xbox 360 game, built with the
ReXGlue SDK. No game files come with the collection: you install each game from a
disc image you made from your own disc.

## Download

Get **`WhoppingCollection-vX.Y.Z-windows-x64.zip`** from the
[latest release](https://github.com/TekRantGaming/whopping-recompilation-collection/releases/latest), unzip it
anywhere and start `whopping_launcher.exe`. The launcher downloads the games for you and keeps the games and itself
up to date. (The other zips in each release are the games themselves; the launcher fetches them.)

Windows 10 or 11, 64 bit, with a DirectX 12 graphics card.

## Your discs

Make a disc image (.iso) of each game from your own disc. The launcher finds images on your PC by itself, or you can
choose the file. Only the title ID is checked, so any good dump of these discs works. The images the ports were made
with:

| Game | Title ID | Media ID | Version | Image tested | SHA1 |
|---|---|---|---|---|---|
| Sneak King | 4D538802 | 5D787B9D | 0.0.0.70 | 7,027,228,672 bytes | `BDE18507EBF74060D43625BE8CE2B20C604DAF3E` |
| Big Bumpin' | 4D538801 | 56F9F46F | 0.0.0.70 | 7,825,162,240 bytes | `51FBF8E78A25D466295568FBE9752C7070961779` |
| PocketBike Racer | 4D538803 | 346E875D | 0.0.0.70 | 7,825,162,240 bytes | `6A801254B1691717445253A1F4E068115F16F7B9` |

The launcher's install screen shows the full details for each disc (MD5 and CRC32 too).

## The launcher

* A console style dashboard you can use entirely with a controller, or with the mouse and keyboard.
* Each game installs separately: Download, then Install from your disc, then Play from its page.
* Settings for each game: window and display, frame rate (60 by default, up to no limit), render resolution and
  upscaling (AMD FSR 1, NVIDIA NIS), anti aliasing, texture filtering, controller options (invert, sensitivity,
  deadzone, vibration, button remapping), keyboard and mouse with key bindings, and sound.
* Achievements with notifications in game, save backups and restore, and automatic updates.
* **Back to the launcher while playing:** hold Back and Start together on the controller, or hold Esc.
* Other TRG Projects: the other TekRant Gaming ports, and King Kong Recompiled to install and play.

## Building from source

See [BUILDING.md](BUILDING.md). The game code is generated from each game's own `default.xex`, so building a game
needs your own disc too.

## Credits

* The ports and launcher: TekRant Gaming.
* ReXGlue SDK (static recompilation runtime), Dear ImGui, SDL, stb.
* The TRG Launcher (updater and settings stores), shared by the TekRant Gaming ports.
* Game art from [SteamGridDB](https://www.steamgriddb.com).

Sneak King, Big Bumpin' and PocketBike Racer were made by Blitz Games for King Games (2006). This project is not
affiliated with or endorsed by Burger King, Blitz Games or Microsoft. All trademarks belong to their owners.
