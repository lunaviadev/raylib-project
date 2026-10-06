# TIMESEED

A small 3D roguelike written in **C** with **raylib**, where your computer's clock is the seed.

## The idea

- The title screen shows the current Unix time as the **seed**, ticking every second, with a live wireframe preview of the dungeon that seed builds.
- Press **ENTER** to lock in the second you're on. That number generates every floor of your run.
- **Same seed means same dungeon.** You can retry a seed you died on (**R**), or type in a seed someone else shared with you (**T** on the title screen).
- The seed is also decoded back into a **time of day**, and that sets the mood of the run:

| Seed time     | Phase | Light radius | Extra enemies |
|---------------|-------|--------------|---------------|
| 07:00 - 17:59 | DAY   | 8            | +0            |
| 05:00 - 06:59 | DAWN  | 6            | +1            |
| 18:00 - 20:59 | DUSK  | 6            | +1            |
| 21:00 - 04:59 | NIGHT | 5            | +3            |

Reach the stairs on each floor. Get past floor 10 to escape.

## Controls

| Key                 | Action                               |
|---------------------|--------------------------------------|
| WASD / Arrow keys   | Move (walk into an enemy to attack)  |
| Space               | Wait a turn                          |
| R                   | Restart with the same seed           |
| N                   | Start a new run with a fresh clock seed |
| Esc                 | Back to the title screen (quit from the title) |
| T (title)           | Type a seed                          |
| C (title)           | Go back to the ticking clock seed    |

## Gameplay

- Turn based: enemies only move when you do.
- **Crawlers** (small cubes) are weak and fast. **Brutes** (tall cubes) hit harder but only move every other turn.
- White orbs are potions that heal you.
- You can only see what's inside your light radius. Places you've already seen stay on the map as wireframes.
- Each floor down gives you +1 max HP and a small heal. Every 3 floors your attack goes up.

## Building

Everything needed to build is in this repo. raylib 6.0 (headers and the prebuilt Windows static library) is in [`raylib/`](raylib/). You only need GCC (MinGW). The raylib Windows installer ships one at `C:\raylib\w64devkit`.

**Windows, double-click or from a terminal:**

```bat
build.bat
```

`build.bat run` builds and then launches the game. If `gcc` isn't on your PATH, the script falls back to `C:\raylib\w64devkit\bin`.

**Or with make (e.g. inside w64devkit):**

```sh
make run
```

**Or the raw command:**

```sh
gcc main.c -o game.exe -std=c99 -Wall -Wextra -O2 -Iraylib/include -Lraylib/lib -lraylib -lopengl32 -lgdi32 -lwinmm
```

## Project layout

```
main.c              the whole game (C99)
build.bat           Windows build script
Makefile            make build
raylib/include/     raylib.h, raymath.h, rlgl.h
raylib/lib/         libraylib.a (Windows, MinGW 64-bit)
raylib/LICENSE      raylib's zlib licence
```

## How the seed works (technical)

- The seed is `time(NULL)` (seconds since 1 Jan 1970) cast to a 32-bit unsigned int.
- The game uses its own xorshift32 random number generator, not `rand()`, so a seed gives the same result on any machine.
- Each floor reseeds the generator with `seed XOR (floor * constant)`, so floor 3 of a seed is always the same floor 3.
- Combat rolls and enemy wandering use the same generator, so the same seed plus the same key presses replays the exact same run.
- The time of day is read from the seed with `localtime()`, so it follows your computer's time zone. Someone sharing a seed from a different time zone may get a different phase.

## Credits

Made with [raylib](https://www.raylib.com/) by Ramon Santamaria (zlib licence).
