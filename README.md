# OpenRaces

An ASI mod for **Need for Speed: Most Wanted (2012)**, PC version 1.4.0.0 (`NFS13.exe`).

## Features

- **Every race for every car.** Every event appears on the map and in free roam for whatever car you're driving, and you can start it from either.
- **Parts stay tied to their original cars.** Parts are only awarded when an event is run in one of the cars it was originally assigned to. In any other car you still earn Speed Points, but no parts.
- **EasyDrive works as before.** The EasyDrive Races menu, part unlocks and the "NEW" badge all use each car's original event list.
- **Free-roam markers follow you.** The game only has room for 20 event markers at once. The mod fills them with the 20 events nearest you and refreshes them every 100 m.
- **Random opponents.** In every race, each AI racer gets a random car, and a random paint colour, from the same class as your car.
- **Most Wanted races are left alone.** All 14 of them (base game plus DLC) keep their original opponent.

## Installation

1. Install an ASI loader, for example [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) as `dinput8.dll`, in the game folder.
2. Copy `dist/OpenRaces.asi` and `dist/OpenRaces.ini` into the game's `scripts` folder.
3. Use an **unmodified** `GAMELOGIC/GAMEPLAY.BNDL`. The mod opens up the event lists in memory and needs the original lists to decide who gets parts.

## Settings (`OpenRaces.ini`)

| Setting | Default | Effect |
|---|---|---|
| `Enabled` | 1 | Randomize AI opponents |
| `RandomizePaint` | 1 | Give randomized opponents a random paint colour |
| `SkipMostWantedRaces` | 1 | Leave Most Wanted races untouched |
| `AllEventsForAllCars` | 1 | Open every event to every car |
| `FixNewEventsFlag` | 1 | Base the EasyDrive "NEW" badge on the original event lists |
| `AvoidDuplicateOpponents` | 1 | Avoid giving two opponents the same car |
| `AvoidPlayerCar` | 1 | Avoid giving an opponent your own car |
| `WriteLog` | 1 | Write `OpenRaces.log` next to the ASI |

## Building

`./build.sh` builds `dist/OpenRaces.asi` with clang and lld (LLVM 18). It needs no Windows SDK or C runtime.

To change which cars can be picked as opponents, or their classes, edit the table in `src/cars.h`. The classes come from the game's own `VEHICLES/VEHICLELIST.BNDL`.

## Compatibility

The mod only works with `NFS13.exe` version 1.4.0.0. At startup it checks the bytes at every place it hooks. If anything doesn't match, it changes nothing and writes an error to the log.
