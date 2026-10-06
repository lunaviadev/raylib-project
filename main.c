#include "raylib.h"
#include "raymath.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SCREEN_W     1280
#define SCREEN_H     720

#define MAP_W        40
#define MAP_H        40
#define MAX_ROOMS    12
#define MAX_ENEMIES  32
#define MAX_ITEMS    8
#define MAX_LOG      5
#define FINAL_FLOOR  10

enum { TILE_WALL, TILE_FLOOR, TILE_STAIRS };
enum { SCREEN_TITLE, SCREEN_PLAY, SCREEN_DEAD, SCREEN_WIN };
enum { ENEMY_CRAWLER, ENEMY_BRUTE };

typedef struct { uint32_t state; } Rng;

static void RngSeed(Rng *r, uint32_t seed)
{
    uint32_t z = seed + 0x9E3779B9u;
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    z ^= z >> 16;
    r->state = z ? z : 1;
}

static uint32_t RngNext(Rng *r)
{
    uint32_t x = r->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return r->state = x;
}

static int RngRange(Rng *r, int min, int max)
{
    return min + (int)(RngNext(r) % (uint32_t)(max - min + 1));
}

typedef struct { int x, y, w, h; } Room;

typedef struct {
    int x, y;
    int hp, maxHp, atk;
    int type;
    bool alive;
    Vector3 draw;
    float flash;
} Enemy;

typedef struct { int x, y; bool taken; } Item;

typedef struct {
    const char *name;
    int light;
    int extraEnemies;
} DayPhase;

typedef struct {
    uint32_t seed;
    DayPhase phase;
    char seedTime[8];

    int floor;
    int turn;
    int kills;
    bool dead, won;

    unsigned char tiles[MAP_H][MAP_W];
    bool seen[MAP_H][MAP_W];
    bool visible[MAP_H][MAP_W];

    Room rooms[MAX_ROOMS];
    int roomCount;
    Enemy enemies[MAX_ENEMIES];
    int enemyCount;
    Item items[MAX_ITEMS];
    int itemCount;

    int px, py;
    int hp, maxHp, atk;
    Vector3 pDraw;
    float pFlash;
    float shake;

    char log[MAX_LOG][80];
    Rng rng;
} Game;

static Color Grey(int v)
{
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (Color){ (unsigned char)v, (unsigned char)v, (unsigned char)v, 255 };
}

static void AddLog(Game *g, const char *fmt, ...)
{
    for (int i = 0; i < MAX_LOG - 1; i++) strcpy(g->log[i], g->log[i + 1]);

    va_list args;
    va_start(args, fmt);
    vsnprintf(g->log[MAX_LOG - 1], sizeof(g->log[0]), fmt, args);
    va_end(args);
}

static int SeedHour(uint32_t seed)
{
    time_t t = (time_t)seed;
    struct tm *lt = localtime(&t);
    return lt ? lt->tm_hour : 12;
}

static void SeedDateString(uint32_t seed, const char *format, char *out, int size)
{
    time_t t = (time_t)seed;
    struct tm *lt = localtime(&t);
    if (lt) strftime(out, size, format, lt);
    else snprintf(out, size, "??");
}

static DayPhase PhaseForHour(int hour)
{
    if (hour >= 7 && hour < 18)  return (DayPhase){ "DAY",   8, 0 };
    if (hour >= 18 && hour < 21) return (DayPhase){ "DUSK",  6, 1 };
    if (hour >= 5 && hour < 7)   return (DayPhase){ "DAWN",  6, 1 };
    return (DayPhase){ "NIGHT", 5, 3 };
}

static const char *EnemyName(int type)
{
    return (type == ENEMY_BRUTE) ? "brute" : "crawler";
}

static bool InBounds(int x, int y)
{
    return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H;
}

static bool Walkable(const Game *g, int x, int y)
{
    return InBounds(x, y) && g->tiles[y][x] != TILE_WALL;
}

static int EnemyAt(const Game *g, int x, int y)
{
    for (int i = 0; i < g->enemyCount; i++)
        if (g->enemies[i].alive && g->enemies[i].x == x && g->enemies[i].y == y) return i;
    return -1;
}

static bool IsFree(const Game *g, int x, int y)
{
    return Walkable(g, x, y) && EnemyAt(g, x, y) < 0 && !(x == g->px && y == g->py);
}

static bool RoomOverlaps(const Game *g, Room r)
{
    for (int i = 0; i < g->roomCount; i++)
    {
        Room o = g->rooms[i];
        if (r.x - 1 < o.x + o.w && r.x + r.w + 1 > o.x &&
            r.y - 1 < o.y + o.h && r.y + r.h + 1 > o.y) return true;
    }
    return false;
}

static void CarveFloor(Game *g, int x, int y)
{
    if (x > 0 && y > 0 && x < MAP_W - 1 && y < MAP_H - 1) g->tiles[y][x] = TILE_FLOOR;
}

static void CarveRow(Game *g, int x1, int x2, int y)
{
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    for (int x = x1; x <= x2; x++) CarveFloor(g, x, y);
}

static void CarveColumn(Game *g, int y1, int y2, int x)
{
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    for (int y = y1; y <= y2; y++) CarveFloor(g, x, y);
}

static void CarveCorridor(Game *g, int x1, int y1, int x2, int y2)
{
    if (RngRange(&g->rng, 0, 1)) { CarveRow(g, x1, x2, y1); CarveColumn(g, y1, y2, x2); }
    else                         { CarveColumn(g, y1, y2, x1); CarveRow(g, x1, x2, y2); }
}

static void RandomPointInRoom(Game *g, Room r, int *x, int *y)
{
    *x = RngRange(&g->rng, r.x, r.x + r.w - 1);
    *y = RngRange(&g->rng, r.y, r.y + r.h - 1);
}

static void ComputeVisibility(Game *g);

static void GenerateFloor(Game *g)
{
    RngSeed(&g->rng, g->seed ^ ((uint32_t)g->floor * 0x27D4EB2Du));

    memset(g->tiles, TILE_WALL, sizeof(g->tiles));
    memset(g->seen, 0, sizeof(g->seen));
    g->roomCount = 0;
    g->enemyCount = 0;
    g->itemCount = 0;

    for (int attempt = 0; attempt < 400 && g->roomCount < MAX_ROOMS; attempt++)
    {
        Room r;
        r.w = RngRange(&g->rng, 4, 9);
        r.h = RngRange(&g->rng, 4, 8);
        r.x = RngRange(&g->rng, 1, MAP_W - r.w - 2);
        r.y = RngRange(&g->rng, 1, MAP_H - r.h - 2);
        if (RoomOverlaps(g, r)) continue;

        for (int y = r.y; y < r.y + r.h; y++)
            for (int x = r.x; x < r.x + r.w; x++) CarveFloor(g, x, y);

        if (g->roomCount > 0)
        {
            Room p = g->rooms[g->roomCount - 1];
            CarveCorridor(g, p.x + p.w/2, p.y + p.h/2, r.x + r.w/2, r.y + r.h/2);
        }
        g->rooms[g->roomCount++] = r;
    }

    Room first = g->rooms[0];
    Room last = g->rooms[g->roomCount - 1];
    g->px = first.x + first.w/2;
    g->py = first.y + first.h/2;
    g->tiles[last.y + last.h/2][last.x + last.w/2] = TILE_STAIRS;

    int wanted = 3 + g->floor + g->phase.extraEnemies;
    if (wanted > MAX_ENEMIES) wanted = MAX_ENEMIES;
    for (int i = 0; i < wanted && g->roomCount > 1; i++)
    {
        Room r = g->rooms[RngRange(&g->rng, 1, g->roomCount - 1)];
        int x, y;
        RandomPointInRoom(g, r, &x, &y);
        if (!IsFree(g, x, y) || g->tiles[y][x] == TILE_STAIRS) continue;

        Enemy *e = &g->enemies[g->enemyCount++];
        memset(e, 0, sizeof(*e));
        e->x = x;
        e->y = y;
        e->alive = true;
        e->type = (RngRange(&g->rng, 0, 99) < 10 + g->floor*6) ? ENEMY_BRUTE : ENEMY_CRAWLER;
        if (e->type == ENEMY_BRUTE) { e->maxHp = 5 + g->floor/2; e->atk = 2 + g->floor/4; }
        else                        { e->maxHp = 2 + g->floor/3; e->atk = 1 + g->floor/5; }
        e->hp = e->maxHp;
        e->draw = (Vector3){ (float)x, 0.0f, (float)y };
    }

    int potions = 2 + RngRange(&g->rng, 0, 1);
    for (int i = 0; i < potions && g->itemCount < MAX_ITEMS; i++)
    {
        Room r = g->rooms[RngRange(&g->rng, 0, g->roomCount - 1)];
        int x, y;
        RandomPointInRoom(g, r, &x, &y);
        if (!IsFree(g, x, y) || g->tiles[y][x] == TILE_STAIRS) continue;
        g->items[g->itemCount++] = (Item){ x, y, false };
    }

    g->pDraw = (Vector3){ (float)g->px, 0.0f, (float)g->py };
    ComputeVisibility(g);
}

static void InitRun(Game *g, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->seed = seed;
    g->phase = PhaseForHour(SeedHour(seed));
    SeedDateString(seed, "%H:%M", g->seedTime, sizeof(g->seedTime));

    g->floor = 1;
    g->maxHp = 10;
    g->hp = g->maxHp;
    g->atk = 2;
    GenerateFloor(g);

    AddLog(g, "Seed %u. The clock read %s - %s.", seed, g->seedTime, g->phase.name);
    AddLog(g, "Find the stairs. Reach floor %d to escape.", FINAL_FLOOR);
}

static bool LineOfSight(const Game *g, int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;)
    {
        if (x0 == x1 && y0 == y1) return true;
        int e2 = 2*err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
        if (x0 == x1 && y0 == y1) return true;
        if (g->tiles[y0][x0] == TILE_WALL) return false;
    }
}

static void ComputeVisibility(Game *g)
{
    int r = g->phase.light;
    memset(g->visible, 0, sizeof(g->visible));

    for (int dy = -r; dy <= r; dy++)
    {
        for (int dx = -r; dx <= r; dx++)
        {
            int x = g->px + dx, y = g->py + dy;
            if (!InBounds(x, y) || dx*dx + dy*dy > r*r + r) continue;
            if (LineOfSight(g, g->px, g->py, x, y))
            {
                g->visible[y][x] = true;
                g->seen[y][x] = true;
            }
        }
    }
}

static void EnemyStep(Game *g, Enemy *e, int sx, int sy)
{
    if ((sx || sy) && IsFree(g, e->x + sx, e->y + sy)) { e->x += sx; e->y += sy; }
}

static void EnemiesTakeTurn(Game *g)
{
    for (int i = 0; i < g->enemyCount; i++)
    {
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;
        if (e->type == ENEMY_BRUTE && (g->turn % 2)) continue;

        int ddx = g->px - e->x, ddy = g->py - e->y;

        if (abs(ddx) + abs(ddy) == 1)
        {
            g->hp -= e->atk;
            g->pFlash = 0.25f;
            g->shake = 0.25f;
            AddLog(g, "The %s hits you for %d.", EnemyName(e->type), e->atk);
            if (g->hp <= 0)
            {
                g->hp = 0;
                g->dead = true;
                AddLog(g, "You were killed by a %s on floor %d.", EnemyName(e->type), g->floor);
                return;
            }
            continue;
        }

        int sx = (ddx > 0) - (ddx < 0);
        int sy = (ddy > 0) - (ddy < 0);

        if (g->visible[e->y][e->x])
        {
            int ox = e->x, oy = e->y;
            if (abs(ddx) >= abs(ddy)) { EnemyStep(g, e, sx, 0); if (e->x == ox) EnemyStep(g, e, 0, sy); }
            else                      { EnemyStep(g, e, 0, sy); if (e->y == oy) EnemyStep(g, e, sx, 0); }
        }
        else if (RngRange(&g->rng, 0, 3) == 0)
        {
            static const int dirs[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
            int d = RngRange(&g->rng, 0, 3);
            EnemyStep(g, e, dirs[d][0], dirs[d][1]);
        }
    }
}

static void Descend(Game *g)
{
    g->floor++;
    if (g->floor > FINAL_FLOOR)
    {
        g->floor = FINAL_FLOOR;
        g->won = true;
        AddLog(g, "Daylight. You escaped in %d turns.", g->turn);
        return;
    }

    g->maxHp += 1;
    g->hp = (g->hp + 2 > g->maxHp) ? g->maxHp : g->hp + 2;
    g->atk = 2 + (g->floor - 1)/3;
    GenerateFloor(g);
    AddLog(g, "You descend to floor %d.", g->floor);
}

static void PlayerTakeTurn(Game *g, int dx, int dy)
{
    if (dx || dy)
    {
        int nx = g->px + dx, ny = g->py + dy;
        int ei = EnemyAt(g, nx, ny);

        if (ei >= 0)
        {
            Enemy *e = &g->enemies[ei];
            int dmg = g->atk + RngRange(&g->rng, 0, 1);
            e->hp -= dmg;
            e->flash = 0.2f;
            if (e->hp <= 0)
            {
                e->alive = false;
                g->kills++;
                AddLog(g, "You destroy the %s.", EnemyName(e->type));
            }
            else AddLog(g, "You hit the %s for %d.", EnemyName(e->type), dmg);
        }
        else if (!Walkable(g, nx, ny))
        {
            return;
        }
        else
        {
            g->px = nx;
            g->py = ny;

            for (int i = 0; i < g->itemCount; i++)
            {
                Item *it = &g->items[i];
                if (!it->taken && it->x == nx && it->y == ny)
                {
                    int heal = 4 + g->floor/2;
                    it->taken = true;
                    g->hp = (g->hp + heal > g->maxHp) ? g->maxHp : g->hp + heal;
                    AddLog(g, "You drink a potion. +%d HP.", heal);
                }
            }

            if (g->tiles[ny][nx] == TILE_STAIRS)
            {
                g->turn++;
                Descend(g);
                return;
            }
        }
    }

    g->turn++;
    EnemiesTakeTurn(g);
    ComputeVisibility(g);
}

static bool IsEdgeWall(const Game *g, int x, int y)
{
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (InBounds(x + dx, y + dy) && g->tiles[y + dy][x + dx] != TILE_WALL) return true;
    return false;
}

static void DrawWorld(const Game *g, bool revealAll, float time)
{
    float reach = (float)g->phase.light + 0.5f;
    float bob = sinf(time*3.0f)*0.06f;

    for (int y = 0; y < MAP_H; y++)
    {
        for (int x = 0; x < MAP_W; x++)
        {
            int t = g->tiles[y][x];
            bool vis = !revealAll && g->visible[y][x];
            if (!revealAll && !g->seen[y][x]) continue;
            if (t == TILE_WALL && !IsEdgeWall(g, x, y)) continue;

            float d = Vector2Distance((Vector2){ (float)x, (float)y }, (Vector2){ g->pDraw.x, g->pDraw.z });
            float f = Clamp(1.0f - d/reach, 0.0f, 1.0f);

            if (t == TILE_WALL)
            {
                Vector3 p = { (float)x, 0.5f, (float)y };
                bool inFront = !revealAll && y > g->pDraw.z && y - g->pDraw.z < 2.5f && fabsf(x - g->pDraw.x) < 1.5f;
                if (vis && inFront) DrawCubeWires(p, 1.0f, 1.0f, 1.0f, Grey(30 + (int)(225*f)));
                else if (vis)
                {
                    DrawCube(p, 1.0f, 1.0f, 1.0f, Grey(30 + (int)(225*f)));
                    DrawCubeWires(p, 1.0f, 1.0f, 1.0f, BLACK);
                }
                else DrawCubeWires(p, 1.0f, 1.0f, 1.0f, Grey(revealAll ? 110 : 45));
                continue;
            }

            Vector3 floorPos = { (float)x, 0.0f, (float)y };
            if (vis) DrawPlane(floorPos, (Vector2){ 0.94f, 0.94f }, Grey(18 + (int)(70*f)));
            else     DrawPlane(floorPos, (Vector2){ 0.94f, 0.94f }, Grey(revealAll ? 35 : 14));

            if (t == TILE_STAIRS)
            {
                Color c = (vis || revealAll) ? WHITE : Grey(80);
                DrawCubeWires((Vector3){ (float)x, 0.02f, (float)y }, 0.8f, 0.04f, 0.8f, c);
                if (vis || revealAll)
                    DrawCylinderWires((Vector3){ (float)x, 0.25f + bob, (float)y }, 0.0f, 0.3f, 0.5f, 4 + (int)(time*4) % 3, WHITE);
            }
        }
    }

    for (int i = 0; i < g->itemCount; i++)
    {
        const Item *it = &g->items[i];
        if (it->taken || (!revealAll && !g->visible[it->y][it->x])) continue;
        Vector3 p = { (float)it->x, 0.35f + bob, (float)it->y };
        DrawSphere(p, 0.13f, WHITE);
        DrawSphereWires(p, 0.24f, 4, 8, Grey(140));
    }

    for (int i = 0; i < g->enemyCount; i++)
    {
        const Enemy *e = &g->enemies[i];
        if (!e->alive || (!revealAll && !g->visible[e->y][e->x])) continue;

        bool brute = (e->type == ENEMY_BRUTE);
        Vector3 size = brute ? (Vector3){ 0.75f, 1.3f, 0.75f } : (Vector3){ 0.55f, 0.55f, 0.55f };
        float hop = brute ? 0.0f : fabsf(sinf(time*6.0f + (float)i))*0.12f;
        Vector3 p = { e->draw.x, size.y/2 + hop, e->draw.z };
        Color body = (e->flash > 0) ? WHITE : BLACK;
        Color edge = (e->flash > 0) ? BLACK : WHITE;

        DrawCubeV(p, size, body);
        DrawCubeWires(p, size.x, size.y, size.z, edge);

        float eyeY = p.y + size.y*0.2f, eyeZ = p.z + size.z/2 + 0.01f;
        DrawCube((Vector3){ p.x - 0.12f, eyeY, eyeZ }, 0.08f, 0.08f, 0.02f, edge);
        DrawCube((Vector3){ p.x + 0.12f, eyeY, eyeZ }, 0.08f, 0.08f, 0.02f, edge);
    }

    if (!revealAll)
    {
        Vector3 p = { g->pDraw.x, 0.4f, g->pDraw.z };
        bool hurt = g->pFlash > 0;
        DrawSphere(p, 0.32f, hurt ? BLACK : WHITE);
        DrawSphereWires(p, 0.33f, 6, 10, hurt ? WHITE : Grey(90));
        DrawCircle3D((Vector3){ p.x, 0.01f, p.z }, 0.45f, (Vector3){ 1, 0, 0 }, 90.0f, WHITE);
    }
}

static void DrawTextCentered(const char *text, int y, int size, Color color)
{
    DrawText(text, (GetScreenWidth() - MeasureText(text, size))/2, y, size, color);
}

static void DrawBar(int x, int y, int w, int h, int value, int max)
{
    DrawRectangleLines(x, y, w, h, WHITE);
    int fill = (max > 0) ? (w - 4)*value/max : 0;
    DrawRectangle(x + 2, y + 2, fill, h - 4, WHITE);
}

static void DrawEnemyHealthBars(const Game *g, Camera3D cam)
{
    for (int i = 0; i < g->enemyCount; i++)
    {
        const Enemy *e = &g->enemies[i];
        if (!e->alive || e->hp == e->maxHp || !g->visible[e->y][e->x]) continue;
        float top = (e->type == ENEMY_BRUTE) ? 1.9f : 1.1f;
        Vector2 s = GetWorldToScreen((Vector3){ e->draw.x, top, e->draw.z }, cam);
        DrawRectangle((int)s.x - 18, (int)s.y - 4, 36, 8, BLACK);
        DrawBar((int)s.x - 18, (int)s.y - 4, 36, 8, e->hp, e->maxHp);
    }
}

static void DrawHud(const Game *g)
{
    int w = GetScreenWidth(), h = GetScreenHeight();

    DrawRectangle(0, 0, w, 44, BLACK);
    DrawLine(0, 44, w, 44, WHITE);
    DrawText(TextFormat("SEED %u", g->seed), 16, 12, 20, WHITE);
    DrawText(TextFormat("FLOOR %d/%d", g->floor, FINAL_FLOOR), 230, 12, 20, WHITE);
    DrawText("HP", 390, 12, 20, WHITE);
    DrawBar(420, 12, 160, 20, g->hp, g->maxHp);
    DrawText(TextFormat("%d/%d", g->hp, g->maxHp), 592, 12, 20, WHITE);
    DrawText(TextFormat("KILLS %d   TURN %d", g->kills, g->turn), 680, 12, 20, Grey(170));

    const char *clock = TextFormat("%s %s  LIGHT %d", g->seedTime, g->phase.name, g->phase.light);
    DrawText(clock, w - MeasureText(clock, 20) - 16, 12, 20, WHITE);

    for (int i = 0; i < MAX_LOG; i++)
    {
        int age = MAX_LOG - 1 - i;
        DrawText(g->log[i], 16, h - 40 - age*22, 20, Grey(255 - age*45));
    }

    const char *help = "WASD/ARROWS move   SPACE wait   R retry seed   N new seed   ESC menu";
    DrawText(help, w - MeasureText(help, 10) - 16, h - 22, 10, Grey(150));

    if (g->pFlash > 0)
        DrawRectangleLinesEx((Rectangle){ 0, 0, (float)w, (float)h }, 30.0f*g->pFlash, WHITE);
}

static void DrawEndPanel(const Game *g, bool won)
{
    int w = GetScreenWidth(), h = GetScreenHeight();
    DrawRectangle(0, 0, w, h, Fade(BLACK, 0.75f));

    Rectangle box = { w/2.0f - 260, h/2.0f - 150, 520, 300 };
    DrawRectangleRec(box, BLACK);
    DrawRectangleLinesEx(box, 2, WHITE);

    int y = (int)box.y + 30;
    DrawTextCentered(won ? "YOU ESCAPED" : "YOU DIED", y, 50, WHITE);
    DrawTextCentered(won ? "the clock lets you go... this time" : TextFormat("on floor %d of %d", g->floor, FINAL_FLOOR), y + 64, 20, Grey(170));
    DrawTextCentered(TextFormat("SEED %u  (%s %s)", g->seed, g->seedTime, g->phase.name), y + 110, 20, WHITE);
    DrawTextCentered(TextFormat("TURNS %d    KILLS %d", g->turn, g->kills), y + 140, 20, WHITE);
    DrawTextCentered("[R] retry this seed   [N] new seed   [ESC] menu", y + 210, 20, Grey(200));
}

static Game game;
static Game preview;

static bool PressedOrRepeat(int key)
{
    return IsKeyPressed(key) || IsKeyPressedRepeat(key);
}

int main(void)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(SCREEN_W, SCREEN_H, "TIMESEED");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    Camera3D camera = { 0 };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    int screen = SCREEN_TITLE;
    uint32_t titleSeed = (uint32_t)time(NULL);
    bool seedLocked = false;
    bool typing = false;
    char typed[11] = { 0 };
    int typedLen = 0;
    bool quit = false;

    InitRun(&preview, titleSeed);

    while (!WindowShouldClose() && !quit)
    {
        float dt = GetFrameTime();
        float now = (float)GetTime();

        if (screen == SCREEN_TITLE)
        {
            if (typing)
            {
                int c = GetCharPressed();
                while (c > 0)
                {
                    if (c >= '0' && c <= '9' && typedLen < 10) { typed[typedLen++] = (char)c; typed[typedLen] = '\0'; }
                    c = GetCharPressed();
                }
                if (PressedOrRepeat(KEY_BACKSPACE) && typedLen > 0) typed[--typedLen] = '\0';
                if (IsKeyPressed(KEY_ESCAPE)) typing = false;
                if (IsKeyPressed(KEY_ENTER) && typedLen > 0)
                {
                    unsigned long long v = strtoull(typed, NULL, 10);
                    titleSeed = (v > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)v;
                    seedLocked = true;
                    typing = false;
                }
            }
            else
            {
                if (!seedLocked) titleSeed = (uint32_t)time(NULL);

                if (IsKeyPressed(KEY_ENTER)) { InitRun(&game, titleSeed); screen = SCREEN_PLAY; }
                else if (IsKeyPressed(KEY_T)) { typing = true; typedLen = 0; typed[0] = '\0'; }
                else if (IsKeyPressed(KEY_C)) seedLocked = false;
                else if (IsKeyPressed(KEY_ESCAPE)) quit = true;
            }

            if (preview.seed != titleSeed) InitRun(&preview, titleSeed);
        }
        else if (screen == SCREEN_PLAY)
        {
            int dx = 0, dy = 0;
            bool act = false;
            if (PressedOrRepeat(KEY_W) || PressedOrRepeat(KEY_UP))         { dy = -1; act = true; }
            else if (PressedOrRepeat(KEY_S) || PressedOrRepeat(KEY_DOWN))  { dy =  1; act = true; }
            else if (PressedOrRepeat(KEY_A) || PressedOrRepeat(KEY_LEFT))  { dx = -1; act = true; }
            else if (PressedOrRepeat(KEY_D) || PressedOrRepeat(KEY_RIGHT)) { dx =  1; act = true; }
            else if (PressedOrRepeat(KEY_SPACE)) act = true;

            if (act) PlayerTakeTurn(&game, dx, dy);

            if (game.dead) screen = SCREEN_DEAD;
            else if (game.won) screen = SCREEN_WIN;

            if (IsKeyPressed(KEY_ESCAPE)) screen = SCREEN_TITLE;
        }

        if (screen == SCREEN_PLAY || screen == SCREEN_DEAD || screen == SCREEN_WIN)
        {
            if (IsKeyPressed(KEY_R)) { InitRun(&game, game.seed); screen = SCREEN_PLAY; }
            else if (IsKeyPressed(KEY_N))
            {
                seedLocked = false;
                InitRun(&game, (uint32_t)time(NULL));
                screen = SCREEN_PLAY;
            }
            else if (screen != SCREEN_PLAY && IsKeyPressed(KEY_ESCAPE)) screen = SCREEN_TITLE;
        }

        if (screen != SCREEN_TITLE)
        {
            float k = 1.0f - expf(-15.0f*dt);
            game.pDraw = Vector3Lerp(game.pDraw, (Vector3){ (float)game.px, 0.0f, (float)game.py }, k);
            for (int i = 0; i < game.enemyCount; i++)
            {
                Enemy *e = &game.enemies[i];
                e->draw = Vector3Lerp(e->draw, (Vector3){ (float)e->x, 0.0f, (float)e->y }, k);
                if (e->flash > 0) e->flash -= dt;
            }
            if (game.pFlash > 0) game.pFlash -= dt;
            if (game.shake > 0) game.shake -= dt;

            Vector3 want = { game.pDraw.x, 0.0f, game.pDraw.z };
            if (Vector3Distance(camera.target, want) > 4.0f) camera.target = want;
            camera.target = Vector3Lerp(camera.target, want, 1.0f - expf(-8.0f*dt));
            camera.position = Vector3Add(camera.target, (Vector3){ 0.0f, 13.0f, 6.0f });
            if (game.shake > 0)
            {
                camera.position.x += GetRandomValue(-10, 10)*0.01f*game.shake;
                camera.position.y += GetRandomValue(-10, 10)*0.01f*game.shake;
            }
        }
        else
        {
            float a = now*0.15f;
            camera.target = (Vector3){ MAP_W/2.0f, 0.0f, MAP_H/2.0f };
            camera.position = (Vector3){ MAP_W/2.0f + cosf(a)*38.0f, 34.0f, MAP_H/2.0f + sinf(a)*38.0f };
        }

        BeginDrawing();
        ClearBackground(BLACK);

        if (screen == SCREEN_TITLE)
        {
            BeginMode3D(camera);
            DrawWorld(&preview, true, now);
            EndMode3D();

            int w = GetScreenWidth(), h = GetScreenHeight();
            DrawRectangle(0, h/2 - 190, w, 380, Fade(BLACK, 0.8f));
            DrawLine(0, h/2 - 190, w, h/2 - 190, WHITE);
            DrawLine(0, h/2 + 190, w, h/2 + 190, WHITE);

            char date[48];
            SeedDateString(titleSeed, "%d %b %Y   %H:%M:%S", date, sizeof(date));
            DayPhase phase = PhaseForHour(SeedHour(titleSeed));

            DrawTextCentered("TIMESEED", h/2 - 160, 80, WHITE);
            DrawTextCentered("a roguelike seeded by your clock", h/2 - 70, 20, Grey(170));

            if (typing)
            {
                DrawTextCentered(TextFormat("TYPE A SEED: %s%s", typed, ((int)(now*2) % 2) ? "_" : " "), h/2 - 20, 40, WHITE);
                DrawTextCentered("digits only   [ENTER] confirm   [ESC] cancel", h/2 + 40, 20, Grey(170));
            }
            else
            {
                DrawTextCentered(TextFormat("SEED %u", titleSeed), h/2 - 20, 40, WHITE);
                DrawTextCentered(TextFormat("%s   -   %s   -   LIGHT %d", date, phase.name, phase.light), h/2 + 30, 20, Grey(200));
                DrawTextCentered(seedLocked ? "seed locked   [C] go back to the clock" : "the seed ticks every second - press ENTER to catch one",
                                 h/2 + 60, 20, Grey(130));
                DrawTextCentered("[ENTER] descend     [T] type a seed     [ESC] quit", h/2 + 130, 20, WHITE);
            }
        }
        else
        {
            BeginMode3D(camera);
            DrawWorld(&game, false, now);
            EndMode3D();

            DrawEnemyHealthBars(&game, camera);
            DrawHud(&game);

            if (screen == SCREEN_DEAD) DrawEndPanel(&game, false);
            else if (screen == SCREEN_WIN) DrawEndPanel(&game, true);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
