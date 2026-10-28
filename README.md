# DiRT-2-ACAT

**D2ACAT (DiRT 2: Any Car, Any Track)** is a mod that lets you race any car on any track in Free Race mode.

In the stock game, Free Race only offers the vehicle classes that belong to the selected mode (rally cars for Rally, trucks and buggies for Landrush, and so on). LAN mode has no such limit; it just warns *"This track/car combo could be carnage!"*. D2ACAT brings that freedom to Free Race. Every class you've unlocked shows up in every mode, on every track.

## Installation

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) as `dinput8.dll` in the game folder (next to `dirt2_game.exe`).
2. Copy `D2ACAT.asi` and `D2ACAT.ini` into a `scripts` folder in the game folder.
3. Start the game. `scripts\D2ACAT.log` should contain a line starting with `Hooked class-list function at`.

To uninstall, delete the two files or set `Enabled=0` in the ini.

## Settings (`D2ACAT.ini`)

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | `0` turns the mod off without removing it. |
| `ExpandEmptyLists` | `0` | Some mode/track pairs normally show no class list at all. `0` leaves those alone; `1` fills them with every class too. |
| `LogClassLists` | `0` | `1` writes each class list the game builds to `D2ACAT.log`. |

## How it works

`dirt2_game.exe` builds the Free Race vehicle-class list in a single function (`0x0051FCE0` in the 22 Jan 2010 build):

```c
void __thiscall BuildFreeRaceClassList(this, int mode, int track, GameVector<int>* out);
```

It switches on the selected mode and pushes only that mode's classes. Internal class IDs are: 0 rally, 1 rallycross, 2 trailblazer, 3 stock baja, 4 raid T1, 5 trophy trucks, 6 class 1 buggies, 8 baja + raid mix, 9 trucks + buggies mix. Only the Free Race class list calls it.

D2ACAT hooks the function. It calls the original once for each base mode (rally, rallycross, trailblazer, raid/landrush) and collects the class IDs, then calls it for the mode you actually picked. The collected IDs go after that mode's own classes, so the game's usual default stays first. Every ID comes from the game's own function, so its unlock checks still apply and locked classes stay hidden.

The function is found by byte signature. If your exe is a different build, the mod patches nothing and says so in the log.

## Compatibility

- Built and tested against `dirt2_game.exe` dated 22 Jan 2010 (v1.1).
- AI opponents have never been tuned for some car/track pairings (LAN has no AI), so expect the occasional odd AI behaviour.

## Building

The plugin has no C runtime and needs no Windows SDK, only kernel32 imports. It builds on Linux (or anywhere with LLVM) using clang, lld-link and llvm-dlltool:

```sh
sh build.sh
```

The output goes to `build/D2ACAT.asi`.
