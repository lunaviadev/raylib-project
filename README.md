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

### In the browser (Emscripten)

The web build is already in `web/` (`index.html`, `index.js`, `index.wasm`). To rebuild it, run:

```bat
build_web.bat
```

That sets up emsdk from `C:\raylib\emsdk`, builds into `web/`, and makes `dungeon-of-time-web.zip` for itch.io. The `emcc` command it runs is:

```sh
emcc main.c -o web/index.html -std=gnu99 -Os -Wall -IC:/raylib/raylib/src C:/raylib/raylib/src/libraylib.web.a -DPLATFORM_WEB -sUSE_GLFW=3 -sASYNCIFY -sTOTAL_MEMORY=67108864 --shell-file C:/raylib/raylib/src/minshell.html
```

`-sASYNCIFY` lets the normal `while (!WindowShouldClose())` loop run in the browser without being rewritten.

Browsers won't load `.wasm` from a `file://` page, so serve the folder:

```sh
python -m http.server 8000 --directory web
```

Then open http://localhost:8000.

---

## Project layout

```
main.c              the whole game, written in C99
build.bat           Windows build script
build_web.bat       browser build script (Emscripten) + itch.io zip
web/                compiled browser build
Makefile            build with make
README.md           this file
raylib/include/     raylib.h, raymath.h, rlgl.h
raylib/lib/         libraylib.a (raylib 6.0, Windows 64-bit MinGW)
raylib/LICENSE      raylib's zlib licence
```

raylib is in the repo so the project builds on any Windows machine with GCC, with no separate raylib install needed.

---

# Dev Log: Dungeon of Time

![Dungeon of Time running in the browser](WEBGIF.gif)
game running in web

https://lunavia.itch.io/dungeon-of-time
password to access website is : UCAPW123

## The plan

When I first read the brief I wanted something small that still felt like a proper game, so I went with a roguelike. The hook came pretty quickly: what if the dungeon was seeded by the time on your clock? That way the data isn't just decoration. It decides the whole run, from the layout to the enemies to how dark it is. Playing at night gives you a smaller light radius and more enemies, so real-world data changes the mechanics, not just the background.

## Starting in 2D

I prototyped everything in 2D first, which was honestly the right call, because the hard part of a roguelike is the systems, not the visuals. I got room placement, corridors, turn-based movement, combat and field of view working with just coloured squares. Field of view took the longest. I used Bresenham's line algorithm to check line of sight to each tile, which I'd heard of before but never actually implemented. I also had to add a real web request. My first choice, worldtimeapi.org, failed in the browser because of CORS, so I switched to time.now, which returns the time and UTC offset for your location. I used Emscripten's fetch API so the request runs in the background, with the computer's clock as a fallback if it fails.

## Moving to 3D

Once it was playable it felt a bit flat, and raylib makes basic 3D pretty approachable, so I decided to move it to 3D. Because all the logic was grid-based, the swap was mostly a rendering change: tiles became cubes, the player became a sphere, and a camera follows behind. The real problems were about readability. Walls kept hiding the player, so I made walls near the camera draw as wireframes. I faked fog by fading walls to dark with distance, and kept everything black and white so it looked deliberate rather than unfinished. I also added smooth sliding between squares so movement didn't feel like teleporting.

## Getting it in the browser

The web build was where most of my unexpected time went. My game used a normal while loop, which browsers don't like, so I compiled with Emscripten's ASYNCIFY option instead of rewriting it. I also hit a version mismatch: my desktop project used raylib 6.0 headers but my web library was 5.5, so I had to compile against the matching ones. Even the build script caught me out. On Windows, emcc is a batch file, so without "call" in front of it my script just stopped after compiling and never packaged the build.

## What I learned

The biggest lesson was to build the systems first and worry about looks later. Because I prototyped in 2D, going 3D was polish rather than a rewrite. I also learned a lot about seeded randomness. I wrote my own xorshift generator instead of using rand(), so the same seed gives the same dungeon on any machine, which is what makes sharing seeds work. Finally, I learned that "it works on my machine" means very little for web builds, and testing in the browser earlier would have saved me a lot of time at the end.