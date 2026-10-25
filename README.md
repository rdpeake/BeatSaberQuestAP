# BeatSaberQuestAP

Play standalone Quest Beat Saber as part of an [Archipelago](https://archipelago.gg) multiworld.

This is a Quest port of [BeatSaberAP](https://github.com/CurrentlyPending/BeatSaberAP), the PCVR client by N00byKing and CurrentlyPending. It talks to the same `beat_saber.apworld`, so hosts generate seeds and write YAMLs exactly as they do for PC players, and PC and Quest players can share one multiworld.

## Requirements

- A modded install of Beat Saber **1.40.8** (see the [BSMG wiki](https://bsmg.wiki/quest-modding.html))
- SongCore, BSML, BeatSaverPlusPlus and web-utils, which your mod manager installs along with the mod
- PlaylistManager, to see the seed's playlists

## Installing

1. Download `BeatSaberQuestAP.qmod` from the [latest release](https://github.com/rdpeake/BeatSaberQuestAP/releases/latest).
2. Install it with your mod manager.
3. You don't need the host's `.apbs` patch: the mod gets the seed's songs from the server and builds the seed's playlists itself.

## Playing

Open **Archipelago** from the main menu's mod list, enter the host, port, slot name and password, and press **Connect**.

Joining a slot puts the game in Archipelago mode, which lasts until you press **Leave Archipelago**:

- Only songs you have unlocked appear in level select, and locked difficulties of a visible song cannot be started.
- If the connection drops, the game keeps retrying in the background and song availability stays locked to your last connection. Clears made while offline are saved and sent once you are back.
- Archipelago mode survives restarting the game. The mod reconnects on its own; if the room has a password it asks you to enter it again, since passwords are never saved.
- The menu lists any seed songs you are missing. Custom maps can be downloaded from BeatSaver with one button; DLC you do not own gets a button that opens its store page.
- Clearing an unlocked difficulty with no modifiers sends its checks. In accuracy-based modes each grade from C up to the one you reached counts.
- Song names in level select show how many of their checks are done, for example `[2/3]`.
- The seed's songs get Archipelago playlists built from the server's slot data: one per map type (Speed, Tech, Midspeed, Acc) in randomized modes, or one for preset modes. They list songs in unlock order and are removed when you leave Archipelago mode.
- With DeathLink on, failing an Archipelago song kills other players, and their deaths fail your current song. Deaths are not sent while offline.

Outside Archipelago mode the game behaves normally.

## Building a song list for your YAML

Press **Export song list for YAML** in the Archipelago menu. You don't need to be connected. The list covers every installed custom song, the base game and extras, and any DLC you own. It is copied to the clipboard, so you can paste it into the Quest browser or a chat app, and saved as `BeatSaberAP-songs.yaml` in the headset's `Download` folder (or the mod's folder if that can't be written).

Other apps on the headset, including the Files app, can't see that file because Beat Saber owns it. Copy it to a PC over USB instead, or with `adb pull /sdcard/Download/BeatSaberAP-songs.yaml`.

Copy its `songs:` block under `Beat Saber:` in your YAML, set `game_mode` to `presetPass` or `presetAccuracy`, and delete songs you don't want. Each entry uses the song's hardest Standard difficulty, and a comment lists the other difficulties it has. Custom songs that BeatSaver doesn't know are left out.

## Differences from the PC client

Seeds, items and location ids match the apworld, so PC and Quest players can share a room. A few client-side behaviours are deliberately different:

- Clears in practice mode or of a difficulty that is not unlocked yet are ignored, so progression cannot be skipped.
- An SSS rank counts as SS rather than sending nothing.
- A DeathLink you receive is not sent back out when it fails your song.
- Your password is never saved to disk; host, port and slot are remembered.
- Level select is filtered from the first successful connection until you leave Archipelago mode, including while offline.
- Location ids, item names and Bloq Key goal locking follow the v0.1.1 apworld, which differs from the PC client source on `master`.

## Building

Builds run on GitHub Actions for every push to `main` and `dev/**` branches; download `BeatSaberQuestAP.qmod` from the run's artifacts. Pushing a `v<version>` tag that matches `qpm.json` also creates a draft release with the qmod and the unstripped library for reading crash logs.

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

MIT; see `LICENSE`. Credits and the licenses of included third-party code are in `THIRD_PARTY_NOTICES.md`.
