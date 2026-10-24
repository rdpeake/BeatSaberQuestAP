# BeatSaberQuestAP

Play standalone Quest Beat Saber as part of an [Archipelago](https://archipelago.gg) multiworld.

This is a Quest port of [BeatSaberAP](https://github.com/CurrentlyPending/BeatSaberAP), the PCVR client by N00byKing and CurrentlyPending. It talks to the same `beat_saber.apworld`, so hosts generate seeds and write YAMLs exactly as they do for PC players, and PC and Quest players can share one multiworld.

## Requirements

- Beat Saber **1.40.8** modded with [ModsBeforeFriday](https://mbf.bsquest.xyz) (MBF downgrades a newer install automatically)
- Core mods for 1.40.8, which MBF installs: SongCore, BSML, SongDownloader, PlaylistManager

## Installing

1. Download `BeatSaberQuestAP.qmod` from the latest build.
2. Drag it into MBF.
3. Optionally, drag the host's `.apbs` patch into MBF as well. It is copied to the mod's folder and unpacked into a playlist on the next launch. You can also copy it by hand to `/sdcard/ModData/com.beatgames.beatsaber/Mods/BeatSaberQuestAP/` and press **Import .apbs playlist** in the Archipelago menu.

## Playing

Open **Archipelago** from the main menu's mod list, enter the host, port, slot name and password, and press **Connect**.

Joining a slot puts the game in Archipelago mode, which lasts until you press **Leave Archipelago**:

- Only songs you have unlocked appear in level select, and locked difficulties of a visible song cannot be started.
- If the connection drops, the game keeps retrying in the background and song availability stays locked to your last connection. Clears made while offline are saved and sent once you are back.
- Archipelago mode survives restarting the game. The mod reconnects on its own; if the room has a password it asks you to enter it again, since passwords are never saved.
- The menu lists any seed songs you are missing. Custom maps can be downloaded from BeatSaver with one button; DLC you do not own gets a button that opens its store page.
- Clearing an unlocked difficulty with no modifiers sends its checks. In accuracy-based modes each grade from C up to the one you reached counts.
- With DeathLink on, failing an Archipelago song kills other players, and their deaths fail your current song. Deaths are not sent while offline.

Outside Archipelago mode the game behaves normally.

## Differences from the PC client

Seeds, items and location ids match the apworld, so PC and Quest players can share a room. A few client-side behaviours are deliberately different:

- Clears in practice mode or of a difficulty that is not unlocked yet are ignored, so progression cannot be skipped.
- An SSS rank counts as SS rather than sending nothing.
- A DeathLink you receive is not sent back out when it fails your song.
- Your password is never saved to disk; host, port and slot are remembered.
- Level select is filtered from the first successful connection until you leave Archipelago mode, including while offline.
- Location ids, item names and Bloq Key goal locking follow the v0.1.1 apworld, which differs from the PC client source on `master`.

## Building

Builds run on GitHub Actions for every push to `main` and `dev/**` branches; download `BeatSaberQuestAP.qmod` from the run's artifacts.

To build locally you need PowerShell 7, CMake, Ninja, [qpm](https://github.com/QuestPackageManager/QPM.CLI) and Android NDK 27 (`qpm ndk download 27`; NDK 30 is too new for beatsaber-hook 6.4):

```
qpm restore
qpm s qmod
```

`qpm s copy` pushes a build to a headset over adb and restarts the game, and `qpm s log` streams the mod's log.

The Archipelago protocol, slot data parsing and unlock rules live in `core/` with no game dependencies, and have host tests:

```
cmake -G Ninja -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests
```

## License

LGPL-3.0, the same as the PC client this is ported from.
