/*
 * GO CODE - missions, map parsing, rooms, sight lines, path finding, lighting and fog of war.
 */
#include "game_int.h"
#include "maps.inc"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "fastmath.h"

/* ------------------------------------------------------------------ */
/* moods                                                                */
/* ------------------------------------------------------------------ */
const Theme THEMES[] = {
    /* 0 rainy night */  { 0.13f, 0.13f, 0.16f,  0.20f, 0.24f, 0.36f,  1, 1, 0x6A4A3EFFu, 0x8C8578FFu, 0x2A2730FFu },
    /* 1 clear night */  { 0.14f, 0.14f, 0.17f,  0.24f, 0.28f, 0.40f,  0, 0, 0x6E6A66FFu, 0x93897CFFu, 0x2C2A2EFFu },
    /* 2 neon */         { 0.13f, 0.10f, 0.16f,  0.22f, 0.18f, 0.32f,  1, 0, 0x403A4EFFu, 0x5A4E6AFFu, 0x221E2AFFu },
    /* 3 storm */        { 0.11f, 0.11f, 0.14f,  0.17f, 0.21f, 0.33f,  1, 1, 0x5C4A44FFu, 0x8A8070FFu, 0x262328FFu },
    /* 4 city glow */    { 0.15f, 0.14f, 0.17f,  0.30f, 0.24f, 0.30f,  0, 0, 0x55606AFFu, 0xA09C94FFu, 0x2A2C30FFu },
};

#define SQ_ALL ((1 << OP_VEGA) | (1 << OP_KNOX) | (1 << OP_PIKE) | (1 << OP_MAE))

const Mission MISSIONS[NMISSIONS] = {
    { "SAFEHOUSE", "WESTSIDE, 02:10",
      "A family is held in the back bedroom. Four armed men in the house.",
      "Plan a path for each operator with A. Put KNOX on a door, press Y so he waits for the GO code, then X to set his breach. Press + to start, then X to call GO.",
      (1 << OP_VEGA) | (1 << OP_KNOX), 0, 7.0f, 40, 1, { MAP1 } },
    { "CORNER STORE", "DOCKS ROAD, 03:40",
      "Robbery gone wrong. The clerk is held in the back office. The storage door is reinforced.",
      "PIKE's drone shows who is inside a room: in planning, point at a room and press X. His gun is silenced - quiet kills don't raise the alarm.",
      (1 << OP_VEGA) | (1 << OP_KNOX) | (1 << OP_PIKE), 0, 6.5f, 45, 0, { MAP2 } },
    { "MOTEL 9", "ROUTE 40, 01:15",
      "Two hostages in two rooms. Thin walls between some units - breach through them.",
      "MAE throws smoke to block sight lines and revives downed teammates on her own. Hold everyone, then breach both rooms on one GO.",
      SQ_ALL, 0, 6.0f, 70, 0, { MAP3 } },
    { "OFFICE FLOOR", "MIDTOWN, 23:30",
      "Hostages in the corner office and the server room. The server room door is reinforced.",
      "Flash a room before you go in: blinded hostiles can't shoot and guards can't harm hostages.",
      SQ_ALL, 1, 6.0f, 60, 0, { MAP4 } },
    { "WAREHOUSE 17", "INDUSTRIAL PARK, 04:05",
      "A big open floor with shelving. Hostages in both side rooms.",
      "Shelves block sight. Use R during the assault to pause and change the plan.",
      SQ_ALL, 3, 5.5f, 80, 0, { MAP5 } },
    { "NEON ROOM", "THE STRIP, 00:50",
      "A nightclub takeover. Two hostages in the VIP lounge and the office.",
      "Two doors into the back rooms. Split the team and use two GO codes.",
      SQ_ALL, 2, 5.5f, 70, 0, { MAP6 } },
    { "EMBASSY ROW", "DIPLOMATIC QUARTER, 02:45",
      "The ambassador is held in a locked office. Reinforced double doors.",
      "Cracked walls lead into the office from the side - KNOX can blow them.",
      SQ_ALL, 0, 5.0f, 70, 0, { MAP7 } },
    { "PENTHOUSE", "HARBOR TOWER, 22:20",
      "A rooftop suite. Two guests held in the bedrooms.",
      "Take it slow - every room has a sight line to the next.",
      SQ_ALL, 4, 5.0f, 60, 0, { MAP8 } },
    { "FIRST CITY BANK", "FINANCIAL DISTRICT, 03:00",
      "Hostages in the manager's office and the vault. The vault is reinforced on both sides.",
      "A breach is loud. Get eyes on the guards before you blow the vault.",
      SQ_ALL, 0, 4.5f, 85, 0, { MAP9 } },
    { "THE MANSION", "HILLSIDE ESTATE, 04:30",
      "Three hostages across the house, one in the locked safe room.",
      "Everything you have learned. Good luck.",
      SQ_ALL, 3, 4.5f, 110, 0, { MAP10 } },
};

/* ------------------------------------------------------------------ */
/* tile queries                                                         */
/* ------------------------------------------------------------------ */
int in_map(const Game *g, int x, int y) { return x >= 0 && y >= 0 && x < g->W && y < g->H; }

int passable(const Game *g, int x, int y)
{
    if (!in_map(g, x, y)) return 0;
    const Tile *t = &g->tile[y][x];
    switch (t->kind) {
    case K_FLOOR:  return t->furn == F_NONE;
    case K_DOOR:   return 1;
    case K_RUBBLE: return 1;
    default:       return 0;
    }
}

int breachable(const Game *g, int x, int y)
{
    if (!in_map(g, x, y)) return 0;
    int k = g->tile[y][x].kind;
    return k == K_SOFT || k == K_RDOOR || (k == K_DOOR && !g->tile[y][x].open);
}

int plan_passable(const Game *g, int x, int y)
{
    if (passable(g, x, y)) return 1;
    return in_map(g, x, y) && (g->tile[y][x].flags & TF_PLANBR) && breachable(g, x, y);
}

int opaque(const Game *g, int x, int y)
{
    if (!in_map(g, x, y)) return 1;
    const Tile *t = &g->tile[y][x];
    switch (t->kind) {
    case K_WALL: case K_SOFT: return 1;
    case K_DOOR: case K_RDOOR: return !t->open;
    case K_FLOOR: return t->furn == F_SHELF;
    default: return 0;
    }
}

int door_dir(const Game *g, int x, int y)
{
    /* walls (or more doors) left and right: the door spans x and you pass through it vertically */
    int l = in_map(g, x - 1, y) && g->tile[y][x - 1].kind != K_FLOOR && g->tile[y][x - 1].kind != K_RUBBLE;
    int r = in_map(g, x + 1, y) && g->tile[y][x + 1].kind != K_FLOOR && g->tile[y][x + 1].kind != K_RUBBLE;
    return (l && r) ? 0 : 1;
}

int room_at(const Game *g, float x, float y)
{
    int tx = (int)floorf(x), ty = (int)floorf(y);
    if (!in_map(g, tx, ty)) return 0;
    return g->tile[ty][tx].room;
}

static int smoke_blocks(const Game *g, float x0, float y0, float x1, float y1)
{
    for (int i = 0; i < MAX_SMOKE; i++) {
        const Smoke *s = &g->sm[i];
        if (s->life <= 0) continue;
        float r = s->r * (s->life < 1.5f ? s->life / 1.5f : 1.0f) * 0.85f;
        /* distance from the cloud's centre to the segment */
        float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
        float t = l2 > 0 ? ((s->x - x0) * dx + (s->y - y0) * dy) / l2 : 0;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        float px = x0 + dx * t - s->x, py = y0 + dy * t - s->y;
        if (px * px + py * py < r * r) return 1;
    }
    return 0;
}

/* what blocks a person's sight (and bullets): walls, closed doors, shelves and windows (blinds down) */
static int blocks_sight(const Game *g, int x, int y)
{
    if (opaque(g, x, y)) return 1;
    return in_map(g, x, y) && g->tile[y][x].kind == K_WINDOW;
}

/* grid walk (Amanatides & Woo): is any tile strictly between the two points opaque?
 * sight: windows block too (people), otherwise only light blockers count (light) */
static int los_walk(const Game *g, float x0, float y0, float x1, float y1, int include_end, int sight)
{
    int (*blk)(const Game *, int, int) = sight ? blocks_sight : opaque;
    int tx = (int)floorf(x0), ty = (int)floorf(y0);
    int ex = (int)floorf(x1), ey = (int)floorf(y1);
    float dx = x1 - x0, dy = y1 - y0;
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    float tdx = dx != 0 ? fabsf(1.0f / dx) : 1e30f, tdy = dy != 0 ? fabsf(1.0f / dy) : 1e30f;
    float tmx = dx != 0 ? ((dx > 0 ? (float)(tx + 1) - x0 : x0 - (float)tx) * tdx) : 1e30f;
    float tmy = dy != 0 ? ((dy > 0 ? (float)(ty + 1) - y0 : y0 - (float)ty) * tdy) : 1e30f;
    for (int n = 0; n < 200; n++) {
        if (tx == ex && ty == ey) return include_end ? !blk(g, tx, ty) : 1;
        if (tmx < tmy) { tmx += tdx; tx += sx; }
        else           { tmy += tdy; ty += sy; }
        if (tx == ex && ty == ey && !include_end) return 1;
        if (blk(g, tx, ty)) return 0;
    }
    return 1;
}
static int los_ex(const Game *g, float x0, float y0, float x1, float y1, int include_end) { return los_walk(g, x0, y0, x1, y1, include_end, 0); }

int los(const Game *g, float x0, float y0, float x1, float y1, int smoke)
{
    if (!los_walk(g, x0, y0, x1, y1, 0, 1)) return 0;
    if (smoke && smoke_blocks(g, x0, y0, x1, y1)) return 0;
    return 1;
}

float ray_len(const Game *g, float x0, float y0, float a, float maxd)
{
    float dx = cosf(a), dy = sinf(a);
    int tx = (int)floorf(x0), ty = (int)floorf(y0);
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    float tdx = fabsf(dx) > 1e-6f ? fabsf(1.0f / dx) : 1e30f, tdy = fabsf(dy) > 1e-6f ? fabsf(1.0f / dy) : 1e30f;
    float tmx = fabsf(dx) > 1e-6f ? ((dx > 0 ? (float)(tx + 1) - x0 : x0 - (float)tx) * tdx) : 1e30f;
    float tmy = fabsf(dy) > 1e-6f ? ((dy > 0 ? (float)(ty + 1) - y0 : y0 - (float)ty) * tdy) : 1e30f;
    float t = 0;
    for (int n = 0; n < 80; n++) {
        if (tmx < tmy) { t = tmx; tmx += tdx; tx += sx; }
        else           { t = tmy; tmy += tdy; ty += sy; }
        if (t >= maxd) return maxd;
        if (blocks_sight(g, tx, ty)) return t;
    }
    return maxd;
}

/* ------------------------------------------------------------------ */
/* path finding: A* on the tile grid, 8 directions, no corner cutting   */
/* ------------------------------------------------------------------ */
int find_path(const Game *g, int sx, int sy, int tx, int ty, int planning, uint8_t (*out)[2], int max)
{
    static int16_t cost[MH][MW], par[MH][MW];
    static uint8_t state[MH][MW];                 /* 0 new, 1 open, 2 closed */
    static int16_t open[MW * MH];
    int nopen = 0;
    int (*ok)(const Game *, int, int) = planning ? plan_passable : passable;
    if (!in_map(g, tx, ty) || !ok(g, tx, ty) || !in_map(g, sx, sy)) return -1;
    if (sx == tx && sy == ty) return 0;
    memset(state, 0, sizeof state);
    cost[sy][sx] = 0; par[sy][sx] = -1; state[sy][sx] = 1;
    open[nopen++] = (int16_t)(sy * MW + sx);
    static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    for (;;) {
        int bi = -1, best = 1 << 30;
        for (int i = 0; i < nopen; i++) {
            int x = open[i] % MW, y = open[i] / MW;
            int hx = abs(x - tx), hy = abs(y - ty);
            int h = 10 * (hx > hy ? hx : hy) + 4 * (hx < hy ? hx : hy);
            if (cost[y][x] + h < best) { best = cost[y][x] + h; bi = i; }
        }
        if (bi < 0) return -1;
        int bx = open[bi] % MW, by = open[bi] / MW;
        open[bi] = open[--nopen];
        if (bx == tx && by == ty) break;
        state[by][bx] = 2;
        int from_door = g->tile[by][bx].kind != K_FLOOR;
        for (int d = 0; d < 8; d++) {
            int nx = bx + DX[d], ny = by + DY[d];
            if (!in_map(g, nx, ny) || state[ny][nx] == 2) continue;
            if (!ok(g, nx, ny)) continue;
            int step = 10;
            if (d >= 4) {
                if (!ok(g, bx + DX[d], by) || !ok(g, bx, by + DY[d])) continue;      /* no corner cutting */
                if (from_door || g->tile[ny][nx].kind != K_FLOOR) continue;          /* go straight through doorways */
                if (g->tile[by][bx + DX[d]].kind != K_FLOOR || g->tile[by + DY[d]][bx].kind != K_FLOOR) continue;
                step = 14;
            }
            if (g->tile[ny][nx].kind == K_DOOR) step += 4;
            if (!passable(g, nx, ny)) step += 20;                                   /* through a planned breach */
            int c = cost[by][bx] + step;
            if (state[ny][nx] == 0) {
                cost[ny][nx] = (int16_t)c; par[ny][nx] = (int16_t)(by * MW + bx); state[ny][nx] = 1;
                open[nopen++] = (int16_t)(ny * MW + nx);
            } else if (c < cost[ny][nx]) {
                cost[ny][nx] = (int16_t)c; par[ny][nx] = (int16_t)(by * MW + bx);
            }
        }
    }
    /* walk back */
    int n = 0, x = tx, y = ty;
    while (!(x == sx && y == sy)) {
        n++;
        int p = par[y][x];
        if (p < 0) break;
        x = p % MW; y = p / MW;
    }
    if (n > max) return -1;
    x = tx; y = ty;
    for (int i = n - 1; i >= 0; i--) {
        out[i][0] = (uint8_t)x; out[i][1] = (uint8_t)y;
        int p = par[y][x];
        x = p % MW; y = p / MW;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* loading                                                              */
/* ------------------------------------------------------------------ */
static uint32_t hash2(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static int mat_of(char c)
{
    switch (c) {
    case '.': return M_GRASS; case ',': return M_PAVE; case 'w': return M_WOOD; case 'c': return M_CARPET;
    case 't': return M_TILE; case 'k': return M_CONCRETE; case 'm': return M_MARBLE; case 'd': return M_DANCE;
    default: return -1;
    }
}

static void add_slight(Game *g, float tx, float ty, int type, int var)
{
    if (g->nlight >= MAX_LIGHTS) return;
    SLight *l = &g->light[g->nlight++];
    memset(l, 0, sizeof *l);
    l->x = tx; l->y = ty; l->type = type; l->on = 1;
    switch (type) {
    case LT_LAMP:   l->r = 1.00f; l->g = 0.70f; l->b = 0.40f; l->rad = 4.6f; break;
    case LT_FLUO:   l->r = 0.72f; l->g = 0.88f; l->b = 1.00f; l->rad = 5.0f; l->flick = (var % 3 == 0) ? 1.0f : 0.0f; break;
    case LT_NEON:   if (var & 1) { l->r = 1.0f; l->g = 0.22f; l->b = 0.75f; } else { l->r = 0.2f; l->g = 0.85f; l->b = 1.0f; }
                    l->rad = 4.2f; break;
    case LT_STREET: l->r = 1.00f; l->g = 0.58f; l->b = 0.24f; l->rad = 5.5f; break;
    default:        l->r = 0.30f; l->g = 0.38f; l->b = 0.58f; l->rad = 3.0f; break;      /* moonlight through a window */
    }
}

static void find_rooms(Game *g)
{
    static int16_t qx[MW * MH], qy[MW * MH];
    for (int y = 0; y < g->H; y++)
        for (int x = 0; x < g->W; x++) g->tile[y][x].room = 255;
    int nr = 1;
    for (int y = 0; y < g->H; y++)
        for (int x = 0; x < g->W; x++) {
            Tile *t = &g->tile[y][x];
            if (t->room != 255 || t->kind != K_FLOOR) continue;
            int id = (t->flags & TF_OUT) ? 0 : (nr < MAX_ROOMS ? nr++ : MAX_ROOMS - 1);
            int qh = 0, qt = 0;
            qx[qt] = (int16_t)x; qy[qt] = (int16_t)y; qt++; t->room = (uint8_t)id;
            while (qh < qt) {
                int cx = qx[qh], cy = qy[qh]; qh++;
                static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
                for (int d = 0; d < 4; d++) {
                    int nx = cx + DX[d], ny = cy + DY[d];
                    if (!in_map(g, nx, ny)) continue;
                    Tile *n = &g->tile[ny][nx];
                    if (n->room != 255 || n->kind != K_FLOOR) continue;
                    if ((n->flags & TF_OUT) != (t->flags & TF_OUT)) continue;
                    n->room = (uint8_t)id;
                    qx[qt] = (int16_t)nx; qy[qt] = (int16_t)ny; qt++;
                }
            }
        }
    /* doors, rubble and walls take the room of a floor neighbour (for fog), else stay 255 */
    g->nrooms = nr;
}

void level_load(Game *g, int mission)
{
    const Mission *ms = &MISSIONS[mission];
    g->mission = mission;
    g->th = &THEMES[ms->theme];
    g->H = 0;
    while (g->H < MH && ms->map[g->H]) g->H++;
    g->W = (int)strlen(ms->map[0]);
    if (g->W > MW) g->W = MW;
    memset(g->tile, 0, sizeof g->tile);
    g->nlight = 0; g->nen = 0; g->nhs = 0;
    int nst = 0;
    int mat[MH][MW];

    for (int y = 0; y < g->H; y++)
        for (int x = 0; x < g->W; x++) {
            char c = ms->map[y][x];
            Tile *t = &g->tile[y][x];
            t->var = (uint8_t)(hash2(x, y) & 255);
            t->kind = K_FLOOR;
            mat[y][x] = mat_of(c);
            switch (c) {
            case '#': t->kind = K_WALL; break;
            case '%': t->kind = K_SOFT; break;
            case 'W': t->kind = K_WINDOW; break;
            case 'D': t->kind = K_DOOR; break;
            case 'R': t->kind = K_RDOOR; break;
            case 'T': t->furn = F_TABLE; break;
            case 'S': t->furn = F_SOFA; break;
            case 'K': t->furn = F_DESK; break;
            case 'B': t->furn = F_BED; break;
            case 'X': t->furn = F_CRATE; break;
            case 'C': t->furn = F_COUNTER; break;
            case 'P': t->furn = F_PLANT; break;
            case 'H': t->furn = F_SHELF; break;
            case 'V': t->furn = F_CAR; break;
            case 'l': t->light = LT_LAMP; break;
            case 'f': t->light = LT_FLUO; break;
            case 'n': t->light = LT_NEON; break;
            case 'L': t->light = LT_STREET; break;
            case 'e': case 'g': case 'h': case 'G':
                if (g->nen < MAX_EN) {
                    Enemy *e = &g->en[g->nen++];
                    memset(e, 0, sizeof *e);
                    e->type = c == 'g' ? EN_GUNMAN : (c == 'h' ? EN_HEAVY : EN_THUG);
                    e->x = e->hx = (float)x + 0.5f; e->y = e->hy = (float)y + 0.5f;
                    e->guard = c == 'G' ? -2 : -1;
                    e->alive = 1; e->target = -1;
                }
                break;
            case '@':
                if (g->nhs < MAX_HS) {
                    Hostage *h = &g->hs[g->nhs++];
                    memset(h, 0, sizeof *h);
                    h->x = (float)x + 0.5f; h->y = (float)y + 0.5f; h->exec = 99;
                }
                break;
            case 's':
                if (nst < NOPS) { g->starts[nst][0] = x; g->starts[nst][1] = y; nst++; }
                break;
            default: break;
            }
        }
    /* infer the floor under props and actors from the neighbours */
    for (int pass = 0; pass < 8; pass++)
        for (int y = 0; y < g->H; y++)
            for (int x = 0; x < g->W; x++) {
                if (mat[y][x] >= 0) continue;
                int cnt[M_NMAT] = { 0 }, best = -1, bc = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        if (!in_map(g, x + dx, y + dy)) continue;
                        int m = mat[y + dy][x + dx];
                        if (m >= 0 && g->tile[y + dy][x + dx].kind == K_FLOOR) cnt[m] += (dx == 0 || dy == 0) ? 2 : 1;
                    }
                for (int m = 0; m < M_NMAT; m++) if (cnt[m] > bc) { bc = cnt[m]; best = m; }
                if (best >= 0 && g->tile[y][x].kind == K_FLOOR) mat[y][x] = best;
            }
    for (int y = 0; y < g->H; y++)
        for (int x = 0; x < g->W; x++) {
            Tile *t = &g->tile[y][x];
            t->mat = (uint8_t)(mat[y][x] < 0 ? M_CONCRETE : mat[y][x]);
            if (t->kind == K_FLOOR && (t->mat == M_GRASS || t->mat == M_PAVE)) t->flags |= TF_OUT | TF_SEEN;
        }
    /* walls / doors / windows next to the outside count as seen (you can see the building) */
    for (int y = 0; y < g->H; y++)
        for (int x = 0; x < g->W; x++) {
            Tile *t = &g->tile[y][x];
            if (t->kind == K_FLOOR) continue;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (in_map(g, x + dx, y + dy) && (g->tile[y + dy][x + dx].flags & TF_OUT)) t->flags |= TF_SEEN;
        }
    find_rooms(g);

    /* lights */
    for (int y = 0; y < g->H; y++)
        for (int x = 0; x < g->W; x++) {
            Tile *t = &g->tile[y][x];
            if (t->light) add_slight(g, (float)x + 0.5f, (float)y + 0.5f, t->light, t->var);
        }
    /* moonlight falls in through windows: a soft light just inside each one */
    for (int y = 0; y < g->H; y++)
        for (int x = 0; x < g->W; x++) {
            if (g->tile[y][x].kind != K_WINDOW) continue;
            static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d], ny = y + DY[d];
                if (in_map(g, nx, ny) && g->tile[ny][nx].kind == K_FLOOR && !(g->tile[ny][nx].flags & TF_OUT))
                    add_slight(g, (float)x + 0.5f + (float)DX[d] * 0.6f, (float)y + 0.5f + (float)DY[d] * 0.6f, LT_NONE, 0);
            }
        }

    /* hostage guards: the nearest hostage */
    for (int i = 0; i < g->nen; i++) {
        Enemy *e = &g->en[i];
        e->torch = (hash2(i, mission) & 3) == 0 && e->guard == -1;
        if (e->guard != -2) continue;
        int best = -1; float bd = 1e9f;
        for (int h = 0; h < g->nhs; h++) {
            float dx = g->hs[h].x - e->x, dy = g->hs[h].y - e->y, d = dx * dx + dy * dy;
            int taken = 0;
            for (int j = 0; j < g->nen; j++) if (j != i && g->en[j].guard == h) taken = 1;
            if (!taken && d < bd) { bd = d; best = h; }
        }
        e->guard = best;
    }
    for (int i = 0; i < g->nen; i++) {
        Enemy *e = &g->en[i];
        /* face the middle of the room, or the hostage */
        float fx = 0, fy = 0;
        if (e->guard >= 0) { fx = g->hs[e->guard].x - e->x; fy = g->hs[e->guard].y - e->y; }
        else {
            int n = 0, r = room_at(g, e->x, e->y);
            for (int y = 0; y < g->H; y++) for (int x = 0; x < g->W; x++)
                if (g->tile[y][x].room == r && g->tile[y][x].kind == K_FLOOR) { fx += (float)x + 0.5f; fy += (float)y + 0.5f; n++; }
            if (n) { fx = fx / (float)n - e->x; fy = fy / (float)n - e->y; }
            if (fabsf(fx) + fabsf(fy) < 0.3f) { fx = (hash2(i, 7) & 1) ? 1.0f : -1.0f; fy = 0.3f; }
        }
        e->face = e->hface = e->want_face = atan2f(fy, fx);
        e->state = (hash2(i, mission + 3) % 3 == 0 && e->guard < 0) ? ES_WANDER : ES_IDLE;
        e->wander_t = 1.0f + (float)(hash2(i, 11) % 40) * 0.1f;
    }
    g->lgw = g->W * 2 + 1;
    g->lgh = g->H * 2 + 1;
    lights_rebuild(g);
}

/* ------------------------------------------------------------------ */
/* doors and breaches                                                   */
/* ------------------------------------------------------------------ */
void open_door(Game *g, int x, int y, float fromx, float fromy)
{
    if (!in_map(g, x, y)) return;
    Tile *t = &g->tile[y][x];
    if (t->kind != K_DOOR || t->open) return;
    t->open = 1;
    /* swing away from whoever opens it */
    int dir = door_dir(g, x, y);
    float side = dir == 0 ? ((float)y + 0.5f - fromy) : ((float)x + 0.5f - fromx);
    t->var = (uint8_t)((t->var & 0xFE) | (side >= 0 ? 1 : 0));
    g->lights_dirty = 1;
}

void break_tile(Game *g, int x, int y)
{
    if (!in_map(g, x, y)) return;
    Tile *t = &g->tile[y][x];
    t->kind = K_RUBBLE;
    t->open = 1;
    t->flags &= (uint8_t)~TF_PLANBR;
    /* rubble joins the room it opens into */
    g->lights_dirty = 1;
}

/* ------------------------------------------------------------------ */
/* lighting                                                             */
/* ------------------------------------------------------------------ */
static int pt_outdoor(const Game *g, int i, int j)
{
    /* grid point (i, j) sits at tile coords (i/2, j/2): look at the tiles touching it */
    int x0 = (i - 1) / 2, x1 = i / 2, y0 = (j - 1) / 2, y1 = j / 2;
    int out = 0, n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (!in_map(g, x, y)) continue;
            n++;
            if (g->tile[y][x].flags & TF_OUT) out++;
        }
    return n && out * 2 >= n;
}

void lights_rebuild(Game *g)
{
    g->nlw = 0;
    for (int k = 0; k < g->nlight; k++) {
        SLight *l = &g->light[k];
        l->n0 = g->nlw;
        int i0 = (int)((l->x - l->rad) * 2.0f) - 1, i1 = (int)((l->x + l->rad) * 2.0f) + 1;
        int j0 = (int)((l->y - l->rad) * 2.0f) - 1, j1 = (int)((l->y + l->rad) * 2.0f) + 1;
        if (i0 < 0) i0 = 0; if (j0 < 0) j0 = 0;
        if (i1 >= g->lgw) i1 = g->lgw - 1; if (j1 >= g->lgh) j1 = g->lgh - 1;
        for (int j = j0; j <= j1; j++)
            for (int i = i0; i <= i1; i++) {
                float px = (float)i * 0.5f, py = (float)j * 0.5f;
                float dx = px - l->x, dy = py - l->y, d = sqrtf(dx * dx + dy * dy);
                if (d >= l->rad) continue;
                /* nudge the point a little towards the light so wall faces catch it */
                float nx = px, ny = py;
                if (d > 0.01f) { nx -= dx / d * 0.12f; ny -= dy / d * 0.12f; }
                if (!los_ex(g, l->x, l->y, nx, ny, 1)) continue;
                float f = 1.0f - d / l->rad;
                f = f * f * (l->type == LT_NONE ? 0.8f : 1.0f);
                if (g->nlw >= MAX_LW) break;
                g->lw_idx[g->nlw] = (uint16_t)(j * g->lgw + i);
                g->lw_w[g->nlw] = f;
                g->nlw++;
            }
        l->n1 = g->nlw;
    }
    g->lights_dirty = 0;
}

static void add_dyn(Game *g, float lx, float ly, float rad, float r, float gg, float b, float cone, float cw)
{
    int i0 = (int)((lx - rad) * 2.0f), i1 = (int)((lx + rad) * 2.0f) + 1;
    int j0 = (int)((ly - rad) * 2.0f), j1 = (int)((ly + rad) * 2.0f) + 1;
    if (i0 < 0) i0 = 0; if (j0 < 0) j0 = 0;
    if (i1 >= g->lgw) i1 = g->lgw - 1; if (j1 >= g->lgh) j1 = g->lgh - 1;
    float cx = cosf(cone), cy = sinf(cone), ccos = cosf(cw);
    for (int j = j0; j <= j1; j++)
        for (int i = i0; i <= i1; i++) {
            float px = (float)i * 0.5f, py = (float)j * 0.5f;
            float dx = px - lx, dy = py - ly, d2 = dx * dx + dy * dy;
            if (d2 >= rad * rad) continue;
            float d = sqrtf(d2), f = 1.0f - d / rad;
            f *= f;
            if (cw > 0) {
                if (d < 0.01f) continue;
                float c = (dx * cx + dy * cy) / d;
                if (c < ccos) continue;
                float edge = (c - ccos) / (1.0f - ccos);
                f *= edge > 0.35f ? 1.0f : edge / 0.35f;
            }
            float nx = px, ny = py;
            if (d > 0.01f) { nx -= dx / d * 0.12f; ny -= dy / d * 0.12f; }
            if (!los_ex(g, lx, ly, nx, ny, 1)) continue;
            int k = j * g->lgw + i;
            g->lgr[k] += r * f; g->lgg[k] += gg * f; g->lgb[k] += b * f;
        }
}

void lights_frame(Game *g)
{
    if (g->lights_dirty) lights_rebuild(g);
    const Theme *th = g->th;
    int n = g->lgw * g->lgh;
    float lit = g->lightning;
    for (int j = 0; j < g->lgh; j++)
        for (int i = 0; i < g->lgw; i++) {
            int k = j * g->lgw + i;
            if (pt_outdoor(g, i, j)) {
                g->lgr[k] = th->out_r + lit * 1.1f; g->lgg[k] = th->out_g + lit * 1.15f; g->lgb[k] = th->out_b + lit * 1.3f;
            } else {
                g->lgr[k] = th->amb_r; g->lgg[k] = th->amb_g; g->lgb[k] = th->amb_b;
            }
        }
    for (int k = 0; k < g->nlight; k++) {
        const SLight *l = &g->light[k];
        float s = 1.05f;
        if (l->type == LT_NONE) s = 0.9f + lit * 3.0f;
        else if (l->flick > 0) {
            float ph = g->t * 1.3f + (float)k * 2.1f;
            float f = sinf(ph * 7.0f) * sinf(ph * 2.3f + 1.0f);
            if (f > 0.82f) s = 0.25f;
            else if (f > 0.7f) s = 0.7f;
        } else if (l->type == LT_NEON) s = 0.95f + 0.12f * sinf(g->t * 9.0f + (float)k);
        for (int q = l->n0; q < l->n1; q++) {
            int idx = g->lw_idx[q];
            float w = g->lw_w[q] * s;
            g->lgr[idx] += l->r * w; g->lgg[idx] += l->g * w; g->lgb[idx] += l->b * w;
        }
    }
    /* flashlights */
    for (int i = 0; i < NOPS; i++) {
        const Op *o = &g->op[i];
        if (!o->present || o->state == OS_DOWN) continue;
        add_dyn(g, o->x + cosf(o->face) * 0.3f, o->y + sinf(o->face) * 0.3f, 6.0f, 0.85f, 0.85f, 0.75f, o->face, 0.42f);
        add_dyn(g, o->x, o->y, 1.6f, 0.32f, 0.32f, 0.3f, 0, 0);
    }
    for (int i = 0; i < g->nen; i++) {
        const Enemy *e = &g->en[i];
        if (!e->alive || !e->torch) continue;
        add_dyn(g, e->x + cosf(e->face) * 0.3f, e->y + sinf(e->face) * 0.3f, 5.0f, 0.75f, 0.65f, 0.45f, e->face, 0.38f);
    }
    for (int i = 0; i < g->nen; i++) {
        const Enemy *e = &g->en[i];
        if (e->alive && e->seen) add_dyn(g, e->x, e->y, 1.3f, 0.2f, 0.17f, 0.15f, 0, 0);
    }
    for (int i = 0; i < MAX_DL; i++) {
        const DLight *d = &g->dl[i];
        if (d->life <= 0) continue;
        float f = d->life / d->max;
        add_dyn(g, d->x / TS, d->y / TS, d->rad / TS, d->r * f, d->g * f, d->b * f, 0, 0);
    }
    /* fog of war: unexplored inside is a dim blueprint */
    for (int j = 0; j < g->lgh; j++)
        for (int i = 0; i < g->lgw; i++) {
            int x0 = (i - 1) / 2, x1 = i / 2, y0 = (j - 1) / 2, y1 = j / 2, seen = 0;
            for (int y = y0; y <= y1 && !seen; y++)
                for (int x = x0; x <= x1; x++)
                    if (in_map(g, x, y) && (g->tile[y][x].flags & TF_SEEN)) { seen = 1; break; }
            if (!seen) {
                int k = j * g->lgw + i;
                g->lgr[k] = 0.16f; g->lgg[k] = 0.20f; g->lgb[k] = 0.32f;
            }
        }
    (void)n;
}

/* ------------------------------------------------------------------ */
/* what the squad can see                                               */
/* ------------------------------------------------------------------ */
void update_vis(Game *g)
{
    for (int y = 0; y < g->H; y++)
        for (int x = 0; x < g->W; x++) g->tile[y][x].flags &= (uint8_t)~TF_VIS;
    for (int i = 0; i < NOPS; i++) {
        const Op *o = &g->op[i];
        if (!o->present || o->state == OS_DOWN) continue;
        int R = 13;
        int x0 = (int)o->x - R, x1 = (int)o->x + R, y0 = (int)o->y - R, y1 = (int)o->y + R;
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
        if (x1 >= g->W) x1 = g->W - 1; if (y1 >= g->H) y1 = g->H - 1;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                Tile *t = &g->tile[y][x];
                float dx = (float)x + 0.5f - o->x, dy = (float)y + 0.5f - o->y;
                if (dx * dx + dy * dy > (float)(R * R)) continue;
                if (los_walk(g, o->x, o->y, (float)x + 0.5f, (float)y + 0.5f, 0, 1)) {
                    t->flags |= TF_VIS | TF_SEEN;
                    if (t->room < MAX_ROOMS && t->room > 0) g->room_seen[t->room] = 1;
                }
            }
    }
}
