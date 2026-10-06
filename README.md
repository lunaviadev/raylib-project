# TIMESEED

A 3D roguelike written in **C** with **raylib**, where your computer's clock is the seed.

Black and white graphics, turn-based, 10 floors, and every run can be repeated exactly.

---

## The idea

Most roguelikes generate a random dungeon each time you play. TIMESEED uses the **current time on your computer** as the random seed.

- The title screen shows the seed ticking up once a second, with a live wireframe preview of the dungeon that seed would build.
- Press **ENTER** to catch a second. That number becomes your run's seed and builds every floor.
- **Same seed means same dungeon.** Press **R** to replay a seed, or share the number and type it in with **T**.
- The seed is also turned back into a **time of day**, and that changes the run:

| Seed time     | Phase | Light radius | Extra enemies per floor |
|---------------|-------|--------------|-------------------------|
| 07:00 - 17:59 | DAY   | 8 tiles      | +0                      |
| 05:00 - 06:59 | DAWN  | 6 tiles      | +1                      |
| 18:00 - 20:59 | DUSK  | 6 tiles      | +1                      |
| 21:00 - 04:59 | NIGHT | 5 tiles      | +3                      |

So playing at night is harder: you see less and there are more enemies.

---

## How to play

Find the stairs on each floor and go down. Get past **floor 10** to escape.

| Key               | Action                                    |
|-------------------|-------------------------------------------|
| WASD / Arrow keys | Move. Walk into an enemy to attack it     |
| Space             | Wait one turn                             |
| R                 | Restart with the same seed                |
| N                 | New run with a fresh seed from the clock  |
| Esc               | Back to the title screen (quits from the title) |
| T *(title)*       | Type in a seed                            |
| C *(title)*       | Go back to the ticking clock seed         |

**What you'll see**

| Thing                | Looks like                          |
|----------------------|-------------------------------------|
| You                  | White sphere with a ring under it   |
| Crawler              | Small black cube that hops. Weak, moves every turn |
| Brute                | Tall black cube. Hits harder, moves every other turn |
| Potion               | Small floating white orb. Heals you |
| Stairs               | White square with a spinning arrow  |
| Places you've seen   | Faint wireframe                     |

**Rules**
- The game is turn-based: enemies only move when you move or wait.
- Enemies chase you when they're in your light. Otherwise they wander.
- Walking into a wall doesn't use up a turn.
- Each floor down gives +1 max HP and heals 2. Your attack goes up every 3 floors.
- Enemies get tougher and more common the deeper you go.

---

## Running it

### Notepad++ (raylib installer)

1. Open **Notepad++ for raylib** (the desktop shortcut from the raylib installer).
2. Open `main.c` from this folder.
3. Press **F6**, choose `raylib_compile_execute`, and click OK.

It builds `main.exe` and starts the game.

### build.bat

Double-click `build.bat`, or run it from a terminal:

```bat
build.bat run
```

This builds `game.exe` and starts it. If `gcc` isn't on your PATH, it uses the one in `C:\raylib\w64devkit\bin`.

### make

```sh
make run
```

### By hand

```sh
gcc main.c -o game.exe -std=c99 -Wall -Wextra -O2 -Iraylib/include -Lraylib/lib -lraylib -lopengl32 -lgdi32 -lwinmm
```

---

## Project layout

```
main.c              the whole game, written in C99
build.bat           Windows build script
Makefile            build with make
README.md           this file
raylib/include/     raylib.h, raymath.h, rlgl.h
raylib/lib/         libraylib.a (raylib 6.0, Windows 64-bit MinGW)
raylib/LICENSE      raylib's zlib licence
```

raylib is in the repo so the project builds on any Windows machine with GCC, with no separate raylib install needed.

---

## How the code works

Everything is in `main.c`, in this order.

### 1. Random numbers

| Function    | What it does |
|-------------|--------------|
| `RngSeed`   | Mixes the seed number so seeds one second apart still give very different dungeons |
| `RngNext`   | xorshift32: produces the next random 32-bit number |
| `RngRange`  | Random whole number between a min and max (inclusive) |

The game uses its own generator, not C's `rand()`. That way a seed gives exactly the same results on any computer.

### 2. Data

- `Game` holds the whole state of a run: seed, floor, map tiles, which tiles are seen or visible, rooms, enemies, potions, player stats, and the message log.
- `Enemy`, `Item` and `Room` are the smaller pieces inside it.
- `DayPhase` stores the light radius and extra enemy count for the seed's time of day.

### 3. Time of day

| Function         | What it does |
|------------------|--------------|
| `SeedHour`       | Treats the seed as a Unix timestamp and gets the hour from it with `localtime()` |
| `SeedDateString` | Formats the seed as a date and time for the title screen and HUD |
| `PhaseForHour`   | Picks DAY / DAWN / DUSK / NIGHT for that hour |

### 4. Dungeon generation

`GenerateFloor` builds one floor:

1. Reseeds the generator with `seed XOR (floor × constant)`, so floor 3 of a seed is always the same floor 3.
2. Fills the 40×40 map with walls.
3. Tries up to 400 times to place rooms that don't overlap (`RoomOverlaps`), up to 12 of them, carving each one out.
4. Joins each new room to the previous one with an L-shaped corridor (`CarveCorridor`, `CarveRow`, `CarveColumn`).
5. Puts the player in the first room and the stairs in the last room.
6. Places enemies (never in the starting room) and 2–3 potions.

`InitRun` resets everything for a new run and generates floor 1.

### 5. Field of view

- `ComputeVisibility` checks every tile inside the light radius.
- `LineOfSight` draws a straight line (Bresenham's line algorithm) from the player to that tile. If it hits a wall first, the tile is hidden.
- Visible tiles are also marked as **seen**, so they stay on the map as wireframes after you leave.

### 6. Turns

| Function          | What it does |
|-------------------|--------------|
| `PlayerTakeTurn`  | Moves the player, attacks an enemy if one is in the way, picks up potions, and takes the stairs. Then runs the enemies' turn |
| `EnemiesTakeTurn` | Each enemy attacks if next to you, chases if it's in your light, or sometimes wanders. Brutes skip every other turn |
| `Descend`         | Goes to the next floor, or wins the game after floor 10 |

Combat rolls and wandering use the same seeded generator. The same seed plus the same key presses always plays out the same way.

### 7. Drawing

- `DrawWorld` draws the 3D scene:
  - Walls are cubes that fade from white to dark the further they are from you (a simple fake fog).
  - Walls just in front of the camera are drawn as wireframes so they never hide the player.
  - Only walls next to a floor are drawn, so solid rock isn't rendered.
  - On the title screen the same function draws the whole map as wireframe (`revealAll`).
- `DrawHud`, `DrawBar`, `DrawEnemyHealthBars` and `DrawEndPanel` draw the 2D interface on top.
- `Grey()` builds the grey shades. Only black, white and greys are used anywhere.

### 8. Main loop

`main` opens the window, then every frame:

1. **Update**: handles input for the current screen (title, playing, dead, won).
2. **Animate**: slides the player and enemies smoothly towards their grid squares, fades out hit flashes, and moves the camera. On the title screen the camera slowly orbits the map.
3. **Draw**: draws the 3D world, then the UI.

---

## Notes

- The time of day comes from your computer's time zone. Someone in a different time zone may get a different phase, with different light and enemy numbers, from the same seed.
- Seeds are 32-bit numbers (0 to 4,294,967,295). Typed seeds bigger than that are capped.

## Credits

Made with [raylib](https://www.raylib.com/) by Ramon Santamaria (zlib licence).
