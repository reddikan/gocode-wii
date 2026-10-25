/*
 * GO CODE - drawing the world: floors, walls, props, decals, characters, then a lighting pass
 * (a 2x multiply of a half-tile light grid over everything) and an additive glow pass
 * (lamps, muzzle flashes, tracers, flashlight beams, rain), then the planning overlay.
 */
#include "game_int.h"
#include "sprites.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include "fastmath.h"

static int vx0, vy0, vx1, vy1;          /* visible tile range */

static uint32_t h32(int x, int y, int k)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)k * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static uint32_t shade(uint32_t c, float k)
{
    int r = (int)((float)(c >> 24) * k), g = (int)((float)((c >> 16) & 255) * k), b = (int)((float)((c >> 8) & 255) * k);
    return RGBA(r, g, b, (int)(c & 255));
}
#define A(x) ((float)(x) * AP)          /* art pixels -> world px */

static void wrect(float x, float y, float w, float h, uint32_t c) { gfx_rect(x, y, w, h, c); }

static void set_view(const Game *g)
{
    gfx_view(g->shake_x, g->shake_y, g->zoom);
    gfx_pan(g->zoom * (VW * 0.5f - g->camx), g->zoom * (VH * 0.5f - g->camy));
    float hw = VW * 0.5f / g->zoom, hh = VH * 0.5f / g->zoom;
    vx0 = (int)floorf((g->camx - hw) / TS) - 1; vx1 = (int)floorf((g->camx + hw) / TS) + 1;
    vy0 = (int)floorf((g->camy - hh) / TS) - 1; vy1 = (int)floorf((g->camy + hh) / TS) + 1;
    if (vx0 < 0) vx0 = 0; if (vy0 < 0) vy0 = 0;
    if (vx1 >= g->W) vx1 = g->W - 1; if (vy1 >= g->H) vy1 = g->H - 1;
}

static int is_wallish(const Game *g, int x, int y)
{
    if (!in_map(g, x, y)) return 0;
    int k = g->tile[y][x].kind;
    return k == K_WALL || k == K_SOFT || k == K_WINDOW || k == K_RDOOR;
}
static int is_out(const Game *g, int x, int y) { return in_map(g, x, y) && (g->tile[y][x].flags & TF_OUT); }

/* ------------------------------------------------------------------ */
/* floors                                                               */
/* ------------------------------------------------------------------ */
static uint32_t carpet_col(int room)
{
    static const uint32_t C[6] = { 0x6E2C34FFu, 0x2F4068FFu, 0x3A5444FFu, 0x6A5A44FFu, 0x4A3458FFu, 0x5A2A2AFFu };
    return C[room % 6];
}

static void floor_tile(const Game *g, int x, int y)
{
    const Tile *t = &g->tile[y][x];
    float X = (float)x * TS, Y = (float)y * TS;
    uint32_t v = h32(x, y, 1);
    switch (t->mat) {
    case M_GRASS: {
        wrect(X, Y, TS, TS, 0x34502EFFu);
        for (int i = 0; i < 6; i++) {
            uint32_t r = h32(x, y, 10 + i);
            wrect(X + A(r % 12), Y + A((r >> 4) % 11), AP, A(1 + ((r >> 8) & 1)), (r >> 12) & 1 ? 0x46663AFFu : 0x2A4226FFu);
        }
        break;
    }
    case M_PAVE: {
        wrect(X, Y, TS, TS, (v & 3) == 0 ? 0x3F4146FFu : 0x434549FFu);
        for (int i = 0; i < 4; i++) {
            uint32_t r = h32(x, y, 20 + i);
            wrect(X + A(r % 12), Y + A((r >> 4) % 12), AP, AP, (r >> 9) & 1 ? 0x505258FFu : 0x36383CFFu);
        }
        if ((v % 23) == 0) gfx_poly(X + TS * 0.5f, Y + TS * 0.5f, TS * 0.45f, 10, 0, 0x2C3038C0u);   /* puddle */
        break;
    }
    case M_WOOD: {
        for (int p = 0; p < 4; p++) {
            uint32_t r = h32(x, y * 4 + p, 2);
            uint32_t c = (r & 3) == 0 ? 0x7C5233FFu : ((r & 3) == 1 ? 0x8A5D3AFFu : 0x845837FFu);
            wrect(X, Y + A(p * 3), TS, A(3), c);
            wrect(X, Y + A(p * 3 + 2) + AP * 0.5f, TS, AP * 0.5f, 0x4E321CC0u);
            int j = (int)((r >> 4) % 12);
            if ((r >> 9) & 1) wrect(X + A(j), Y + A(p * 3), AP * 0.5f, A(3), 0x4E321CA0u);
            if (((r >> 11) & 7) == 0) wrect(X + A((r >> 14) % 11), Y + A(p * 3 + 1), AP, AP, 0x5E3E24FFu);
        }
        break;
    }
    case M_CARPET: {
        uint32_t c = carpet_col(t->room);
        wrect(X, Y, TS, TS, c);
        for (int i = 0; i < 4; i++) {
            uint32_t r = h32(x, y, 30 + i);
            wrect(X + A(r % 12), Y + A((r >> 4) % 12), AP, AP, shade(c, (r >> 8) & 1 ? 1.18f : 0.82f));
        }
        break;
    }
    case M_TILE: {
        wrect(X, Y, TS, TS, 0xA9A79EFFu);
        wrect(X, Y, TS * 0.5f, TS * 0.5f, 0xC6C3B8FFu);
        wrect(X + TS * 0.5f, Y + TS * 0.5f, TS * 0.5f, TS * 0.5f, 0xC6C3B8FFu);
        wrect(X, Y + TS * 0.5f - AP * 0.25f, TS, AP * 0.5f, 0x7E7B72FFu);
        wrect(X + TS * 0.5f - AP * 0.25f, Y, AP * 0.5f, TS, 0x7E7B72FFu);
        wrect(X, Y, TS, AP * 0.5f, 0x7E7B72FFu); wrect(X, Y, AP * 0.5f, TS, 0x7E7B72FFu);
        break;
    }
    case M_CONCRETE: {
        wrect(X, Y, TS, TS, (v & 7) == 0 ? 0x6E6F70FFu : 0x747577FFu);
        if ((v >> 4) % 5 == 0) gfx_poly(X + A(3 + (v >> 8) % 6), Y + A(3 + (v >> 12) % 6), A(2 + (v >> 16) % 3), 7, 0, 0x5A5B5E80u);
        if ((v >> 7) % 7 == 0) gfx_line(X + A(1), Y + A((v >> 9) % 12), X + A(10), Y + A((v >> 13) % 12), AP * 0.5f, 0x55565AFFu, 0x55565AFFu);
        if (x % 4 == 0) wrect(X, Y, AP * 0.5f, TS, 0x5C5D60FFu);
        if (y % 4 == 0) wrect(X, Y, TS, AP * 0.5f, 0x5C5D60FFu);
        break;
    }
    case M_MARBLE: {
        wrect(X, Y, TS, TS, ((x + y) & 1) ? 0xD6D2C8FFu : 0xC8C3B8FFu);
        uint32_t r = h32(x, y, 40);
        gfx_line(X + A(r % 12), Y, X + A((r >> 4) % 12), Y + TS, AP * 0.5f, 0xADA79BFFu, 0xB9B3A8FFu);
        if ((r >> 8) & 1) gfx_line(X, Y + A((r >> 9) % 12), X + TS, Y + A((r >> 13) % 12), AP * 0.5f, 0xB3AD9FFFu, 0xADA79BFFu);
        wrect(X, Y, TS, AP * 0.5f, 0xA49C8CFFu); wrect(X, Y, AP * 0.5f, TS, 0xA49C8CFFu);
        break;
    }
    case M_DANCE: {
        wrect(X, Y, TS, TS, 0x1A1A22FFu);
        wrect(X + AP * 0.5f, Y + AP * 0.5f, TS * 0.5f - AP, TS * 0.5f - AP, 0x2A2A36FFu);
        wrect(X + TS * 0.5f + AP * 0.5f, Y + TS * 0.5f + AP * 0.5f, TS * 0.5f - AP, TS * 0.5f - AP, 0x2A2A36FFu);
        wrect(X + TS * 0.5f + AP * 0.5f, Y + AP * 0.5f, TS * 0.5f - AP, TS * 0.5f - AP, 0x24242EFFu);
        wrect(X + AP * 0.5f, Y + TS * 0.5f + AP * 0.5f, TS * 0.5f - AP, TS * 0.5f - AP, 0x24242EFFu);
        break;
    }
    }
    /* contact shadows from walls to the north and west (light from the top left) */
    if (t->kind == K_FLOOR || t->kind == K_RUBBLE || t->kind == K_DOOR) {
        if (is_wallish(g, x, y - 1)) gfx_rect_v(X, Y, TS, A(4), 0x00000090u, 0x00000000u);
        if (is_wallish(g, x - 1, y)) gfx_quad4(X, Y, 0x00000080u, X + A(4), Y, 0x00000000u, X + A(4), Y + TS, 0x00000000u, X, Y + TS, 0x00000080u);
        if (is_wallish(g, x + 1, y)) gfx_quad4(X + TS - A(2), Y, 0x00000000u, X + TS, Y, 0x00000050u, X + TS, Y + TS, 0x00000050u, X + TS - A(2), Y + TS, 0x00000000u);
    }
}

/* ------------------------------------------------------------------ */
/* walls, doors, windows                                                */
/* ------------------------------------------------------------------ */
static int wall_exterior(const Game *g, int x, int y)
{
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (is_out(g, x + dx, y + dy)) return 1;
    return 0;
}

static void wall_tile(const Game *g, int x, int y)
{
    const Tile *t = &g->tile[y][x];
    float X = (float)x * TS, Y = (float)y * TS;
    int ext = wall_exterior(g, x, y);
    uint32_t top = g->th->wall_top, rim = ext ? g->th->wall_out : g->th->wall_in;
    if (t->kind == K_SOFT) { top = 0x5A5650FFu; rim = 0x9C978CFFu; }
    wrect(X, Y, TS, TS, top);
    if (ext && t->kind == K_WALL) {        /* brick courses on the outside walls */
        for (int r = 0; r < 4; r++) wrect(X, Y + A(r * 3 + 2), TS, AP * 0.5f, shade(top, 0.8f));
    }
    /* rims where the wall meets the floor */
    float rw = A(2);
    int n = !is_wallish(g, x, y - 1) && in_map(g, x, y - 1) && g->tile[y - 1][x].kind != K_DOOR;
    int s = !is_wallish(g, x, y + 1) && in_map(g, x, y + 1) && g->tile[y + 1][x].kind != K_DOOR;
    int w = !is_wallish(g, x - 1, y) && in_map(g, x - 1, y) && g->tile[y][x - 1].kind != K_DOOR;
    int e = !is_wallish(g, x + 1, y) && in_map(g, x + 1, y) && g->tile[y][x + 1].kind != K_DOOR;
    if (n) wrect(X, Y, TS, rw, shade(rim, 1.1f));
    if (s) wrect(X, Y + TS - rw, TS, rw, shade(rim, 0.75f));
    if (w) wrect(X, Y, rw, TS, shade(rim, 0.95f));
    if (e) wrect(X + TS - rw, Y, rw, TS, shade(rim, 0.8f));
    if (t->kind == K_SOFT) {
        uint32_t cr = 0x2A2622FFu;
        gfx_line(X + A(2), Y + A(3), X + A(6), Y + A(6), AP * 0.6f, cr, cr);
        gfx_line(X + A(6), Y + A(6), X + A(5), Y + A(10), AP * 0.6f, cr, cr);
        gfx_line(X + A(6), Y + A(6), X + A(10), Y + A(8), AP * 0.6f, cr, cr);
    }
}

static void window_tile(const Game *g, int x, int y)
{
    const Tile *t = &g->tile[y][x];
    float X = (float)x * TS, Y = (float)y * TS;
    int horiz = is_wallish(g, x - 1, y) || is_wallish(g, x + 1, y);
    uint32_t frame = g->th->wall_top;
    wrect(X, Y, TS, TS, frame);
    uint32_t glass = (t->flags & TF_BROKEN) ? 0x00000000u : 0x7FB8D8B0u;
    if (horiz) {
        wrect(X, Y + A(4), TS, A(4), 0x10141AFFu);
        if (glass) { wrect(X + AP, Y + A(5), TS - A(2), A(2), glass); wrect(X + A(2), Y + A(5), A(3), AP * 0.5f, 0xE0F4FFC0u); }
        else for (int i = 0; i < 3; i++) gfx_tri1(X + A(i * 4), Y + A(4), X + A(i * 4 + 2), Y + A(6), X + A(i * 4 + 3), Y + A(4), 0x7FB8D8B0u);
        wrect(X + TS * 0.5f - AP * 0.5f, Y + A(4), AP, A(4), frame);
    } else {
        wrect(X + A(4), Y, A(4), TS, 0x10141AFFu);
        if (glass) { wrect(X + A(5), Y + AP, A(2), TS - A(2), glass); wrect(X + A(5), Y + A(2), AP * 0.5f, A(3), 0xE0F4FFC0u); }
        else for (int i = 0; i < 3; i++) gfx_tri1(X + A(4), Y + A(i * 4), X + A(6), Y + A(i * 4 + 2), X + A(4), Y + A(i * 4 + 3), 0x7FB8D8B0u);
        wrect(X + A(4), Y + TS * 0.5f - AP * 0.5f, A(4), AP, frame);
    }
}

static void door_tile(const Game *g, int x, int y)
{
    const Tile *t = &g->tile[y][x];
    float X = (float)x * TS, Y = (float)y * TS;
    int dir = door_dir(g, x, y);                 /* 0: spans x */
    int reinforced = t->kind == K_RDOOR;
    uint32_t slab = reinforced ? 0x5E6670FFu : 0x6E4A2CFFu, hi = reinforced ? 0x88929CFFu : 0x8E6440FFu;
    /* the frame posts */
    uint32_t post = g->th->wall_top;
    if (dir == 0) { wrect(X, Y + A(3), A(1), A(6), post); wrect(X + TS - A(1), Y + A(3), A(1), A(6), post); }
    else { wrect(X + A(3), Y, A(6), A(1), post); wrect(X + A(3), Y + TS - A(1), A(6), A(1), post); }
    float sw = t->open ? t->swing : 0.0f;
    if (reinforced && !t->open) sw = 0;
    float ang = sw * PI_F * 0.5f * ((t->var & 1) ? 1.0f : -1.0f);
    /* hinge at the left / top end; the slab rotates about it */
    float hx = dir == 0 ? X + A(1) : X + TS * 0.5f, hy = dir == 0 ? Y + TS * 0.5f : Y + A(1);
    float len = TS - A(2), th = A(reinforced ? 3 : 2);
    float ux = dir == 0 ? 1.0f : 0.0f, uy = dir == 0 ? 0.0f : 1.0f;
    float ca = cosf(ang), sa = sinf(ang);
    float dx = ux * ca - uy * sa, dy = ux * sa + uy * ca, nx = -dy * th * 0.5f, ny = dx * th * 0.5f;
    gfx_quad4(hx + nx + A(1), hy + ny + A(1), 0x00000060u, hx + dx * len + nx + A(1), hy + dy * len + ny + A(1), 0x00000060u,
              hx + dx * len - nx + A(1), hy + dy * len - ny + A(1), 0x00000060u, hx - nx + A(1), hy - ny + A(1), 0x00000060u);
    gfx_quad4(hx + nx, hy + ny, slab, hx + dx * len + nx, hy + dy * len + ny, slab, hx + dx * len - nx, hy + dy * len - ny, hi, hx - nx, hy - ny, hi);
    if (reinforced) {
        for (int i = 1; i < 5; i++) {
            float px = hx + dx * len * (float)i / 5.0f, py = hy + dy * len * (float)i / 5.0f;
            gfx_line(px - nx, py - ny, px + nx, py + ny, AP, (i & 1) ? 0xE8B830FFu : 0x202020FFu, (i & 1) ? 0xE8B830FFu : 0x202020FFu);
        }
    } else {
        float kx = hx + dx * len * 0.85f, ky = hy + dy * len * 0.85f;
        wrect(kx - AP * 0.5f, ky - AP * 0.5f, AP, AP, 0xD8B860FFu);
    }
}

static void rubble_tile(const Game *g, int x, int y)
{
    float X = (float)x * TS, Y = (float)y * TS;
    for (int i = 0; i < 7; i++) {
        uint32_t r = h32(x, y, 50 + i);
        float s = A(1 + (r >> 20) % 2);
        wrect(X + A(r % 11), Y + A((r >> 4) % 11), s, s, (r >> 8) & 1 ? 0x6A665EFFu : 0x4E4038FFu);
    }
    (void)g;
}

/* ------------------------------------------------------------------ */
/* furniture                                                            */
/* ------------------------------------------------------------------ */
static int same(const Game *g, int x, int y, int f) { return in_map(g, x, y) && g->tile[y][x].kind == K_FLOOR && g->tile[y][x].furn == f; }

static void furn_tile(const Game *g, int x, int y)
{
    const Tile *t = &g->tile[y][x];
    float X = (float)x * TS, Y = (float)y * TS;
    int f = t->furn;
    int L = same(g, x - 1, y, f), R = same(g, x + 1, y, f), U = same(g, x, y - 1, f), D = same(g, x, y + 1, f);
    float m = A(1);
    float x0 = X + (L ? 0 : m), y0 = Y + (U ? 0 : m), x1 = X + TS - (R ? 0 : m), y1 = Y + TS - (D ? 0 : m);
    uint32_t r = h32(x, y, 60);
    /* soft drop shadow */
    if (f != F_CAR) wrect(x0 + A(2), y0 + A(2), x1 - x0, y1 - y0, 0x00000060u);
    switch (f) {
    case F_TABLE: case F_DESK: {
        uint32_t c = f == F_TABLE ? 0x6A4428FFu : 0x4C3A2CFFu;
        wrect(x0, y0, x1 - x0, y1 - y0, c);
        wrect(x0, y0, x1 - x0, AP, shade(c, 1.25f));
        if (!D) wrect(x0, y1 - AP, x1 - x0, AP, shade(c, 0.7f));
        if (f == F_DESK) {
            wrect(X + A(3), Y + A(2), A(6), A(4), 0x1A1C20FFu);             /* monitor */
            wrect(X + A(4), Y + A(3), A(4), A(2), 0x2E6A8AFFu);
            wrect(X + A(3), Y + A(8), A(5), A(2), 0x2A2A2EFFu);             /* keyboard */
            if (r & 1) wrect(X + A(9), Y + A(8), A(1), A(2), 0xD8D8D0FFu);  /* a mug */
        } else if ((r & 3) == 0) {
            gfx_poly(X + A(5), Y + A(6), A(1.5f), 8, 0, 0xE0DCD0FFu);         /* a plate */
        } else if ((r & 3) == 1) {
            wrect(X + A(4), Y + A(4), A(2), A(3), 0x3A6A3AFFu);              /* a bottle */
        }
        break;
    }
    case F_SOFA: {
        uint32_t c = (t->room & 1) ? 0x5A2830FFu : 0x2E4A56FFu;
        wrect(x0, y0, x1 - x0, y1 - y0, c);
        wrect(x0, y0, x1 - x0, A(3), shade(c, 0.75f));                      /* back */
        if (!L) wrect(x0, y0, A(2), y1 - y0, shade(c, 0.8f));
        if (!R) wrect(x1 - A(2), y0, A(2), y1 - y0, shade(c, 0.8f));
        wrect(x0 + A(2), y0 + A(4), x1 - x0 - A(4), A(1), shade(c, 1.2f));
        if (L || R) wrect(X + (L ? 0 : TS - AP * 0.5f), y0 + A(3), AP * 0.5f, y1 - y0 - A(3), shade(c, 0.6f));
        break;
    }
    case F_BED: {
        wrect(x0, y0, x1 - x0, y1 - y0, 0x4A3424FFu);
        wrect(x0 + AP, y0 + AP, x1 - x0 - A(2), y1 - y0 - A(2), 0xD8D4CAFFu);
        wrect(x0 + AP, y0 + A(5), x1 - x0 - A(2), y1 - y0 - A(6), 0x3E5A86FFu);    /* blanket */
        wrect(x0 + AP, y0 + A(5), x1 - x0 - A(2), AP, 0x5A78A6FFu);
        if (!L) wrect(x0 + A(2), y0 + A(1.5f), A(4), A(3), 0xF0ECE4FFu);     /* pillow */
        if (!R && L) wrect(x1 - A(6), y0 + A(1.5f), A(4), A(3), 0xF0ECE4FFu);
        break;
    }
    case F_CRATE: {
        uint32_t c = (r & 1) ? 0x8A6A3EFFu : 0x7A5C34FFu;
        wrect(X + m, Y + m, TS - 2 * m, TS - 2 * m, c);
        wrect(X + m, Y + m, TS - 2 * m, AP, shade(c, 1.3f));
        float s = AP * 0.8f;
        gfx_line(X + m, Y + m, X + TS - m, Y + TS - m, s, shade(c, 0.7f), shade(c, 0.7f));
        gfx_line(X + TS - m, Y + m, X + m, Y + TS - m, s, shade(c, 0.7f), shade(c, 0.7f));
        wrect(X + m, Y + TS * 0.5f - AP * 0.5f, TS - 2 * m, AP, shade(c, 0.8f));
        break;
    }
    case F_COUNTER: {
        wrect(x0, y0, x1 - x0, y1 - y0, 0x9C9A94FFu);
        wrect(x0, y0, x1 - x0, AP, 0xC4C2BAFFu);
        if ((r & 3) == 0) { wrect(X + A(3), Y + A(3), A(6), A(5), 0x6E7680FFu); wrect(X + A(4), Y + A(4), A(4), A(3), 0x3A424AFFu); }
        else if ((r & 3) == 1) { for (int k = 0; k < 4; k++) gfx_ring(X + A(3.5f + (k & 1) * 5), Y + A(3.5f + (k >> 1) * 5), A(1.5f), AP * 0.6f, 8, 0, 0x2A2A2AFFu); }
        else if ((r & 3) == 2) wrect(X + A(4), Y + A(3), A(3), A(4), 0x3A2A1AFFu);    /* a till */
        break;
    }
    case F_PLANT: {
        gfx_poly(X + TS * 0.5f + A(1), Y + TS * 0.5f + A(1), A(4), 10, 0, 0x00000060u);
        gfx_poly(X + TS * 0.5f, Y + TS * 0.5f, A(3), 10, 0, 0x7A4A2EFFu);
        for (int k = 0; k < 6; k++) {
            float a = (float)k * 1.047f + (float)(r & 7);
            gfx_poly(X + TS * 0.5f + cosf(a) * A(2.5f), Y + TS * 0.5f + sinf(a) * A(2.5f), A(2.2f), 6, a, k & 1 ? 0x3E7A3AFFu : 0x2E6230FFu);
        }
        gfx_poly(X + TS * 0.5f, Y + TS * 0.5f, A(1.6f), 6, 0, 0x4E8E46FFu);
        break;
    }
    case F_SHELF: {
        wrect(x0, y0, x1 - x0, y1 - y0, 0x2A2420FFu);
        for (int k = 0; k < 3; k++) {
            uint32_t q = h32(x, y, 70 + k);
            uint32_t c = (q & 3) == 0 ? 0x9A3A30FFu : ((q & 3) == 1 ? 0x30608AFFu : ((q & 3) == 2 ? 0xC8A050FFu : 0x4A7A44FFu));
            wrect(X + A(1 + k * 3.5f), Y + A(2), A(3), A(8), c);
            wrect(X + A(1 + k * 3.5f), Y + A(2), A(3), AP, shade(c, 1.3f));
        }
        wrect(x0, y0, x1 - x0, AP, 0x4A403AFFu);
        break;
    }
    case F_CAR: {
        uint32_t c = (h32(x / 3, y, 9) & 1) ? 0x8A2A2AFFu : 0x2A4A6AFFu;
        if (!L) wrect(x0 + A(1), y0 + A(1), x1 - x0 + (R ? 0 : 0), y1 - y0, 0x00000070u);
        wrect(x0, y0 + A(1), x1 - x0, y1 - y0 - A(2), c);
        wrect(x0, y0 + A(1), x1 - x0, AP, shade(c, 1.35f));
        if (L && R) { wrect(X, Y + A(3), TS, A(6), shade(c, 0.6f)); wrect(X + A(1), Y + A(4), TS - A(2), A(4), 0x2A3A4AFFu); }
        if (!L) { wrect(x0, y0 + A(3), A(2), A(2), 0xE8E0C8FFu); wrect(x0, y1 - A(5), A(2), A(2), 0xE8E0C8FFu); }
        if (!R) { wrect(x1 - A(2), y0 + A(3), A(2), A(2), 0x8A1A1AFFu); wrect(x1 - A(2), y1 - A(5), A(2), A(2), 0x8A1A1AFFu); }
        break;
    }
    default: break;
    }
}

static void fixture(const Game *g, int x, int y)
{
    const Tile *t = &g->tile[y][x];
    float cx = ((float)x + 0.5f) * TS, cy = ((float)y + 0.5f) * TS;
    switch (t->light) {
    case LT_LAMP:
        gfx_poly(cx + A(1), cy + A(1), A(3), 10, 0, 0x00000060u);
        gfx_poly(cx, cy, A(3), 10, 0, 0xD8C8A0FFu);
        gfx_poly(cx, cy, A(1.4f), 8, 0, 0xFFF0C8FFu);
        break;
    case LT_FLUO:
        wrect(cx - A(5), cy - A(1.5f), A(10), A(3), 0x8A8C90FFu);
        wrect(cx - A(4.5f), cy - A(0.8f), A(9), A(1.6f), 0xF0F8FFFFu);
        break;
    case LT_NEON: {
        uint32_t c = (t->var & 1) ? 0xFF4AC0FFu : 0x40E0FFFFu;
        gfx_ring(cx, cy, A(3.5f), A(1), 12, 0, c);
        break;
    }
    case LT_STREET:
        gfx_poly(cx + A(2), cy + A(2), A(2.5f), 8, 0, 0x00000080u);
        gfx_poly(cx, cy, A(2), 8, 0, 0x404448FFu);
        gfx_poly(cx, cy, A(1.2f), 8, 0, 0xFFE0A0FFu);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* decals and particles                                                 */
/* ------------------------------------------------------------------ */
static void draw_decals(const Game *g)
{
    for (int i = 0; i < g->ndc; i++) {
        const Decal *d = &g->dc[i];
        float x = d->x * TS, y = d->y * TS, s = d->size * TS;
        switch (d->type) {
        case DC_BLOOD:
            gfx_poly(x, y, s, 9, d->rot, d->c);
            gfx_poly(x + cosf(d->rot) * s * 0.7f, y + sinf(d->rot) * s * 0.7f, s * 0.5f, 7, d->rot, d->c);
            break;
        case DC_SCORCH: gfx_poly2(x, y, s, 12, d->rot, 0x0A0806C0u, 0x0A080600u); break;
        case DC_HOLE: wrect(x - AP * 0.5f, y - AP * 0.5f, AP, AP, d->c); break;
        case DC_GLASS: wrect(x, y, AP * 0.7f, AP * 0.7f, d->c); break;
        case DC_CASING: wrect(x, y, AP, AP * 0.5f, d->c); break;
        case DC_SPLINTER: {
            float ca = cosf(d->rot) * s * 0.5f, sa = sinf(d->rot) * s * 0.5f;
            gfx_line(x - ca, y - sa, x + ca, y + sa, AP, d->c, d->c);
            break;
        }
        }
    }
}

static void draw_particles(const Game *g, int glow)
{
    for (int i = 0; i < g->npt; i++) {
        const Particle *p = &g->pt[i];
        float x = p->x * TS, y = p->y * TS - p->z * TS * 0.6f, f = p->life / p->max;
        switch (p->type) {
        case PT_SMOKE: case PT_DUST:
            if (glow) break;
            gfx_poly2(x, y, p->size * (1.4f - f * 0.4f), 8, p->rot, col_a(p->c, (p->type == PT_SMOKE ? 0.55f : 0.35f) * (f < 0.6f ? f / 0.6f : 1.0f)), col_a(p->c, 0.0f));
            break;
        case PT_SPARK: case PT_EMBER:
            if (!glow) break;
            gfx_capsule(x, y, p->vx, p->vy, AP * (1.0f + 2.0f * f) * (p->type == PT_EMBER ? 0.6f : 1.0f), AP * 0.8f, col_a(p->c, f));
            break;
        case PT_SPLASH:
            if (!glow) break;
            gfx_ring(x, y, AP * (0.5f + (1.0f - f) * 1.5f), AP * 0.4f, 6, 0, col_a(p->c, f * 0.45f));
            break;
        default:
            if (glow) break;
            if (p->z > 0.05f) wrect(p->x * TS - p->size * 0.5f + AP, p->y * TS - p->size * 0.5f + AP, p->size * AP * 0.6f, p->size * AP * 0.6f, 0x00000050u);
            {
                float s = p->size * AP * 0.6f, ca = cosf(p->rot) * s, sa = sinf(p->rot) * s;
                gfx_quad4(x - ca + sa, y - sa - ca, p->c, x + ca + sa, y + sa - ca, p->c, x + ca - sa, y + sa + ca, p->c, x - ca - sa, y - sa + ca, p->c);
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* characters                                                           */
/* ------------------------------------------------------------------ */
static const int OP_SPR[NOPS] = { SP_RIFLE, SP_SHOTGUN, SP_SMG, SP_PISTOL };

static void feet(float x, float y, float a, float walk, float moving)
{
    if (moving < 0.05f) return;
    float s = sinf(walk) * A(2.5f) * moving;
    float ca = cosf(a), sa = sinf(a);
    float lx = x - sa * A(2) + ca * s, ly = y + ca * A(2) + sa * s;
    float rx = x + sa * A(2) - ca * s, ry = y - ca * A(2) - sa * s;
    sprite_draw(SP_FOOT, lx, ly, a, AP * 1.2f, 0, 0xFFFFFFFFu);
    sprite_draw(SP_FOOT, rx, ry, a, AP * 1.2f, 0, 0xFFFFFFFFu);
}

static int enemy_shown(const Game *g, const Enemy *e)
{
    return e->seen || g->result != RES_NONE;
}

static void draw_characters(const Game *g)
{
    /* the fallen first */
    for (int i = 0; i < g->nen; i++) {
        const Enemy *e = &g->en[i];
        if (e->alive || !enemy_shown(g, e)) continue;
        uint32_t tint = e->type == EN_THUG ? 0xE09080FFu : (e->type == EN_HEAVY ? 0xA8B080FFu : 0xFFFFFFFFu);
        float k = e->die_t < 0.25f ? e->die_t / 0.25f : 1.0f;
        sprite_draw(SP_EN_DEAD, e->x * TS - cosf(e->face) * k * A(2), e->y * TS - sinf(e->face) * k * A(2), e->face + PI_F, AP, 0, tint);
    }
    for (int h = 0; h < g->nhs; h++) {
        const Hostage *hs = &g->hs[h];
        float x = hs->x * TS, y = hs->y * TS;
        gfx_poly(x + A(1), y + A(1), A(4.5f), 10, 0, 0x00000050u);
        if (hs->state == HS_DEAD) sprite_draw(SP_HOSTAGE_DEAD, x, y, PI_F * 0.5f, AP, 0, 0xFFFFFFFFu);
        else sprite_draw(hs->state == HS_SECURED ? SP_HOSTAGE_FREE : SP_HOSTAGE, x, y + sinf(hs->bob * 2.0f) * AP * 0.3f, PI_F * 0.5f, AP, 0, 0xFFFFFFFFu);
    }
    for (int i = 0; i < NOPS; i++) {
        const Op *o = &g->op[i];
        if (!o->present || o->state != OS_DOWN) continue;
        sprite_draw(SP_OP_DOWN, o->x * TS, o->y * TS, o->face, AP, op_color(i), 0xFFFFFFFFu);
    }
    /* the living */
    for (int i = 0; i < g->nen; i++) {
        const Enemy *e = &g->en[i];
        if (!e->alive || !enemy_shown(g, e)) continue;
        float x = e->x * TS - cosf(e->face) * e->recoil * AP, y = e->y * TS - sinf(e->face) * e->recoil * AP;
        gfx_poly(x + A(1), y + A(1.5f), A(5), 10, 0, 0x00000058u);
        feet(x, y, e->face, e->walk, e->plen > 0 ? 1.0f : 0.0f);
        uint32_t tint = e->hurt > 0 ? 0xFFB0B0FFu : 0xFFFFFFFFu;
        int s = e->type == EN_THUG ? SP_THUG : (e->type == EN_GUNMAN ? SP_GUNMAN : SP_HEAVY);
        sprite_draw(s, x, y, e->face, AP, 0, tint);
    }
    for (int i = 0; i < NOPS; i++) {
        const Op *o = &g->op[i];
        if (!o->present || o->state == OS_DOWN) continue;
        float x = o->x * TS - cosf(o->face) * o->recoil * AP, y = o->y * TS - sinf(o->face) * o->recoil * AP;
        gfx_poly(x + A(1), y + A(1.5f), A(5), 10, 0, 0x00000058u);
        int moving = (o->state == OS_MOVE && o->pidx < o->plen && o->target < 0) || (o->manual) || o->state == OS_REVIVE + 100;
        feet(x, y, o->face, o->walk, moving ? 1.0f : 0.0f);
        uint32_t tint = o->hurt > 0 ? 0xFFA0A0FFu : 0xFFFFFFFFu;
        sprite_draw(OP_SPR[i], x, y, o->face, AP, op_color(i), tint);
    }
}

/* ------------------------------------------------------------------ */
/* lighting pass                                                        */
/* ------------------------------------------------------------------ */
static uint32_t lcol(const Game *g, int k)
{
    int r = (int)(g->lgr[k] * 127.5f), gg = (int)(g->lgg[k] * 127.5f), b = (int)(g->lgb[k] * 127.5f);
    return RGBA(r, gg, b, 255);
}

static void lighting(const Game *g)
{
    gfx_blend(BL_MUL);
    int i0 = vx0 * 2, i1 = vx1 * 2 + 2, j0 = vy0 * 2, j1 = vy1 * 2 + 2;
    if (i1 >= g->lgw) i1 = g->lgw - 1;
    if (j1 >= g->lgh) j1 = g->lgh - 1;
    float h = TS * 0.5f;
    for (int j = j0; j < j1; j++) {
        uint32_t ca = lcol(g, j * g->lgw + i0), cb = lcol(g, (j + 1) * g->lgw + i0);
        for (int i = i0; i < i1; i++) {
            uint32_t cc = lcol(g, j * g->lgw + i + 1), cd = lcol(g, (j + 1) * g->lgw + i + 1);
            float x = (float)i * h, y = (float)j * h;
            gfx_quad4(x, y, ca, x + h, y, cc, x + h, y + h, cd, x, y + h, cb);
            ca = cc; cb = cd;
        }
    }
    gfx_blend(BL_ALPHA);
}

/* ------------------------------------------------------------------ */
/* glow pass                                                            */
/* ------------------------------------------------------------------ */
static void beam(const Game *g, float x, float y, float a, float len, float spread, uint32_t c)
{
    /* a soft cone of light in the air, clipped by the walls */
    const int N = 10;
    float px = 0, py = 0;
    uint32_t e = c & 0xFFFFFF00u;
    for (int k = 0; k <= N; k++) {
        float b = a - spread + 2.0f * spread * (float)k / (float)N;
        float l = ray_len(g, x, y, b, len);
        float qx = (x + cosf(b) * l) * TS, qy = (y + sinf(b) * l) * TS;
        if (k > 0) gfx_tri(x * TS, y * TS, c, px, py, e, qx, qy, e);
        px = qx; py = qy;
    }
}

static void glow_pass(const Game *g)
{
    gfx_blend(BL_ADD);
    float t = g->t;
    /* fixtures */
    for (int y = vy0; y <= vy1; y++)
        for (int x = vx0; x <= vx1; x++) {
            const Tile *tl = &g->tile[y][x];
            if (!(tl->flags & TF_SEEN)) continue;
            float cx = ((float)x + 0.5f) * TS, cy = ((float)y + 0.5f) * TS;
            switch (tl->light) {
            case LT_LAMP:   gfx_glow(cx, cy, TS * 1.1f, 0xFFB86030u); gfx_glow(cx, cy, A(4), 0xFFE0A070u); break;
            case LT_FLUO:   gfx_glow(cx, cy, TS * 0.9f, 0xB8D8FF28u); gfx_glow(cx, cy, A(6), 0xE0F0FF40u); break;
            case LT_NEON: { uint32_t c = (tl->var & 1) ? 0xFF40C0u : 0x40E0FFu;
                            gfx_glow(cx, cy, TS * 1.5f, (c << 8) | 0x30u); gfx_ring(cx, cy, A(3.5f), A(1.5f), 12, 0, (c << 8) | 0xA0u); break; }
            case LT_STREET: gfx_glow(cx, cy, TS * 2.0f, 0xFF903018u); gfx_glow(cx, cy, A(4), 0xFFD08060u); break;
            }
            if (tl->kind == K_FLOOR && tl->furn == F_DESK) {
                float fl = 0.8f + 0.2f * sinf(t * 3.0f + (float)x);
                gfx_glow(cx, cy - A(2), A(7), RGBA(80, 170, 230, (int)(50 * fl)));
            }
            if (tl->kind == K_FLOOR && tl->mat == M_DANCE) {
                float beat = t * 2.0f;
                int ph = ((x + y) + (int)beat) % 4;
                uint32_t c = ph == 0 ? 0xFF3AB000u : (ph == 1 ? 0x3AD8FF00u : (ph == 2 ? 0x8A50FF00u : 0xFFD03A00u));
                float pulse = 1.0f - (beat - floorf(beat));
                gfx_rect((float)x * TS + A(0.5f), (float)y * TS + A(0.5f), TS - AP, TS - AP, c | (uint32_t)(30 + 60 * pulse));
            }
            if (tl->kind == K_FLOOR && tl->furn == F_CAR && !same(g, x - 1, y, F_CAR)) {
                gfx_glow((float)x * TS, cy - A(3), A(8), 0xFFF0D030u);
                gfx_glow((float)x * TS, cy + A(3), A(8), 0xFFF0D030u);
            }
        }
    /* flashlight beams in the air */
    for (int i = 0; i < NOPS; i++) {
        const Op *o = &g->op[i];
        if (!o->present || o->state == OS_DOWN) continue;
        beam(g, o->x + cosf(o->face) * 0.4f, o->y + sinf(o->face) * 0.4f, o->face, 5.5f, 0.36f, 0xFFF4D014u);
        /* laser sight */
        float l = ray_len(g, o->x, o->y, o->face, 9.0f);
        uint32_t lc = o->target >= 0 ? 0xFF303070u : 0xFF303030u;
        gfx_line((o->x + cosf(o->face) * 0.55f) * TS, (o->y + sinf(o->face) * 0.55f) * TS, (o->x + cosf(o->face) * l) * TS, (o->y + sinf(o->face) * l) * TS, AP * 0.4f, lc, lc & 0xFFFFFF00u);
        gfx_glow((o->x + cosf(o->face) * l) * TS, (o->y + sinf(o->face) * l) * TS, A(1.5f), 0xFF404080u);
    }
    for (int i = 0; i < NOPS; i++) {
        const Op *o = &g->op[i];
        if (!o->present) continue;
        float x = o->x * TS - cosf(o->face) * o->recoil * AP, y = o->y * TS - sinf(o->face) * o->recoil * AP;
        sprite_draw(o->state == OS_DOWN ? SP_OP_DOWN : OP_SPR[i], x, y, o->face, AP, op_color(i), col_a(op_color(i), 0.22f));
    }
    for (int i = 0; i < g->nen; i++) {
        const Enemy *e = &g->en[i];
        if (!e->alive || !e->torch || !enemy_shown(g, e)) continue;
        beam(g, e->x + cosf(e->face) * 0.4f, e->y + sinf(e->face) * 0.4f, e->face, 4.5f, 0.32f, 0xFFD89010u);
    }
    /* muzzle flashes */
    for (int i = 0; i < NOPS; i++) {
        const Op *o = &g->op[i];
        if (!o->present || o->muzzle <= 0) continue;
        float mx = (o->x + cosf(o->face) * 0.62f) * TS, my = (o->y + sinf(o->face) * 0.62f) * TS;
        float k = o->muzzle / 0.06f;
        if (i == OP_PIKE) { gfx_glow(mx, my, A(3), col_a(0xFFD080FFu, 0.4f * k)); continue; }
        gfx_glow(mx, my, A(10), col_a(0xFFB050FFu, 0.45f * k));
        gfx_capsule(mx + cosf(o->face) * A(5), my + sinf(o->face) * A(5), cosf(o->face), sinf(o->face), A(6), A(3), col_a(0xFFF0C0FFu, k));
        gfx_tri1(mx, my, mx + cosf(o->face + 0.6f) * A(4), my + sinf(o->face + 0.6f) * A(4), mx + cosf(o->face) * A(2), my + sinf(o->face) * A(2), col_a(0xFFE0A0FFu, k));
        gfx_tri1(mx, my, mx + cosf(o->face - 0.6f) * A(4), my + sinf(o->face - 0.6f) * A(4), mx + cosf(o->face) * A(2), my + sinf(o->face) * A(2), col_a(0xFFE0A0FFu, k));
    }
    for (int i = 0; i < g->nen; i++) {
        const Enemy *e = &g->en[i];
        if (!e->alive || e->muzzle <= 0) continue;
        float mx = (e->x + cosf(e->face) * 0.62f) * TS, my = (e->y + sinf(e->face) * 0.62f) * TS;
        float k = e->muzzle / 0.06f;
        gfx_glow(mx, my, A(10), col_a(0xFF9040FFu, 0.45f * k));
        gfx_capsule(mx + cosf(e->face) * A(5), my + sinf(e->face) * A(5), cosf(e->face), sinf(e->face), A(6), A(3), col_a(0xFFE0A0FFu, k));
    }
    /* tracers */
    for (int i = 0; i < MAX_TR; i++) {
        const Tracer *tr = &g->tr[i];
        if (tr->life <= 0) continue;
        float k = tr->life / 0.07f;
        uint32_t c = tr->enemy ? 0xFF8A4AFFu : 0xFFE89AFFu;
        float x0 = tr->x0 * TS, y0 = tr->y0 * TS, x1 = tr->x1 * TS, y1 = tr->y1 * TS;
        float sx = x0 + (x1 - x0) * (1.0f - k) * 0.7f, sy = y0 + (y1 - y0) * (1.0f - k) * 0.7f;
        gfx_line(sx, sy, x1, y1, AP * 0.8f, col_a(c, 0.1f * k), col_a(c, 0.9f * k));
        gfx_glow(x1, y1, A(2.5f), col_a(c, 0.5f * k));
    }
    /* explosions and flashes */
    for (int i = 0; i < MAX_DL; i++) {
        const DLight *d = &g->dl[i];
        if (d->life <= 0 || d->max < 0.2f) continue;
        float k = d->life / d->max;
        gfx_glow(d->x, d->y, d->rad * 0.5f * (1.4f - k * 0.4f), RGBA((int)(d->r * 80), (int)(d->g * 80), (int)(d->b * 80), (int)(160 * k)));
    }
    draw_particles(g, 1);
    /* thrown gadgets in flight */
    for (int i = 0; i < MAX_NADE; i++) {
        const Nade *n = &g->nd[i];
        if (n->dur <= 0) continue;
        float k = n->t / n->dur; if (k > 1) k = 1;
        float x = (n->x0 + (n->x1 - n->x0) * k) * TS, y = (n->y0 + (n->y1 - n->y0) * k) * TS - sinf(k * PI_F) * TS * 0.6f;
        gfx_glow(x, y, A(2.5f), n->type == AB_FLASH ? 0xFFFFFF90u : 0xB0B0B060u);
    }
    /* planted charges blink */
    for (int i = 0; i < MAX_CHARGE; i++) {
        const Charge *c = &g->ch[i];
        if (!c->live) continue;
        float on = fmodf(g->t * (c->t > 0 ? 6.0f : 2.0f), 1.0f) < 0.5f ? 1.0f : 0.25f;
        gfx_glow(((float)c->tx + 0.5f) * TS, ((float)c->ty + 0.5f) * TS, A(3), col_a(0xFF3020FFu, on));
    }
    /* rain */
    if (g->th->rain) {
        const int N = 260;
        for (int i = 0; i < N; i++) {
            uint32_t r = h32(i, 0, 3);
            float fx = (float)(r & 0xFFFF) / 65536.0f, ph = (float)((r >> 16) & 0xFF) / 256.0f;
            float sp = 1.6f + (float)((r >> 24) & 7) * 0.1f;
            float u = fmodf(t * sp + ph, 1.0f);
            float x = (fx * (float)(g->W + 6) - 3.0f) + u * 1.2f, y = (u * (float)(g->H + 4) - 2.0f);
            int tx = (int)floorf(x), ty = (int)floorf(y);
            if (!is_out(g, tx, ty)) continue;
            float X = x * TS, Y = y * TS;
            gfx_line(X, Y, X + A(1.5f), Y + A(9), AP * 0.5f, 0xA8C0E000u, 0xA8C0E050u);
        }
    }
    gfx_blend(BL_ALPHA);
}

/* ------------------------------------------------------------------ */
/* overlays: plans, markers                                             */
/* ------------------------------------------------------------------ */
static void dashed(const uint8_t (*p)[2], int n, float sx, float sy, uint32_t c, float t, float w)
{
    float px = sx, py = sy, phase = fmodf(t * 24.0f, 12.0f);
    for (int i = 0; i < n; i++) {
        float qx = ((float)p[i][0] + 0.5f) * TS, qy = ((float)p[i][1] + 0.5f) * TS;
        float dx = qx - px, dy = qy - py, l = sqrtf(dx * dx + dy * dy);
        if (l > 0.01f) {
            float s = -phase;
            while (s < l) {
                float a = s < 0 ? 0 : s, b = s + 6.0f > l ? l : s + 6.0f;
                if (b > a) gfx_line(px + dx / l * a, py + dy / l * a, px + dx / l * b, py + dy / l * b, w, c, c);
                s += 12.0f;
            }
            phase = fmodf(phase + l, 12.0f);
        }
        px = qx; py = qy;
    }
}

static void icon_ability(int ab, float x, float y, float s, uint32_t c)
{
    switch (ab) {
    case AB_FLASH:
        for (int k = 0; k < 4; k++) { float a = (float)k * PI_F * 0.25f; gfx_line(x - cosf(a) * s, y - sinf(a) * s, x + cosf(a) * s, y + sinf(a) * s, s * 0.35f, c, c); }
        gfx_poly(x, y, s * 0.45f, 8, 0, 0xFFFFFFFFu);
        break;
    case AB_BREACH:
        gfx_rect(x - s * 0.8f, y - s * 0.55f, s * 1.6f, s * 1.1f, 0xE07A20FFu);
        gfx_rect(x - s * 0.5f, y - s * 0.25f, s * 1.0f, s * 0.5f, 0x301808FFu);
        gfx_rect(x - s * 0.15f, y - s * 0.15f, s * 0.3f, s * 0.3f, 0xFF3020FFu);
        break;
    case AB_SMOKE:
        gfx_poly(x - s * 0.35f, y, s * 0.55f, 8, 0, 0xC8C8C8FFu);
        gfx_poly(x + s * 0.35f, y + s * 0.1f, s * 0.5f, 8, 0, 0xB0B0B0FFu);
        gfx_poly(x, y - s * 0.3f, s * 0.5f, 8, 0, 0xDCDCDCFFu);
        break;
    case AB_DRONE:
        gfx_ring(x, y, s * 0.7f, s * 0.25f, 10, 0, c);
        gfx_poly(x, y, s * 0.25f, 6, 0, c);
        break;
    }
}

static void plan_overlay(const Game *g)
{
    static uint8_t path[MAX_PATH][2];
    int planning = g->scene == SC_PLAN || g->tac;
    float t = g->t;
    for (int i = 0; i < NOPS; i++) {
        const Op *o = &g->op[i];
        if (!o->present || o->state == OS_DOWN) continue;
        uint32_t c = op_color(i);
        int sel = i == g->sel && planning;
        uint32_t lc = col_a(c, sel ? 0.95f : (planning ? 0.55f : 0.3f));
        int sx = (int)o->x, sy = (int)o->y;
        float px = o->x * TS, py = o->y * TS;
        int k0 = g->scene == SC_PLAN ? 0 : (o->cur < 0 ? 0 : o->cur);
        if (g->scene == SC_EXEC && o->state == OS_MOVE && o->pidx < o->plen && !o->manual) {
            dashed((const uint8_t (*)[2])&o->path[o->pidx], o->plen - o->pidx, px, py, lc, t, AP * (sel ? 1.2f : 0.9f));
            if (o->cur < o->nwp) { sx = o->wp[o->cur].tx; sy = o->wp[o->cur].ty; px = ((float)sx + 0.5f) * TS; py = ((float)sy + 0.5f) * TS; }
            k0 = o->cur + 1;
        }
        for (int k = k0; k < o->nwp; k++) {
            const Waypoint *w = &o->wp[k];
            int n = find_path(g, sx, sy, w->tx, w->ty, 1, path, MAX_PATH);
            if (n > 0) dashed((const uint8_t (*)[2])path, n, px, py, lc, t, AP * (sel ? 1.2f : 0.9f));
            sx = w->tx; sy = w->ty; px = ((float)sx + 0.5f) * TS; py = ((float)sy + 0.5f) * TS;
        }
        /* preview to the cursor */
        if (sel && o->nwp < MAX_WP) {
            int n = find_path(g, sx, sy, (int)g->cx, (int)g->cy, 1, path, MAX_PATH);
            if (n > 0) dashed((const uint8_t (*)[2])path, n, px, py, col_a(c, 0.35f), t, AP * 0.8f);
        }
        /* markers */
        for (int k = (g->scene == SC_PLAN ? 0 : (o->cur < 0 ? 0 : o->cur)); k < o->nwp; k++) {
            const Waypoint *w = &o->wp[k];
            float x = ((float)w->tx + 0.5f) * TS, y = ((float)w->ty + 0.5f) * TS;
            uint32_t mc = col_a(c, sel ? 1.0f : (planning ? 0.7f : 0.45f));
            gfx_poly(x, y, A(3.2f), 12, 0, 0x000000A0u);
            gfx_ring(x, y, A(3.2f), AP, 12, 0, mc);
            char num[12];
            snprintf(num, sizeof num, "%d", k + 1);
            gfx_text_c(num, x + AP * 0.2f, y - AP * 1.6f, AP * 0.65f, mc);
            if (w->hold) {
                float bx = x + A(4), by = y - A(8);
                float pulse = (o->state == OS_HOLD && k == o->cur) ? 0.6f + 0.4f * sinf(t * 8.0f) : 1.0f;
                gfx_rect(bx - AP, by - AP, A(10), A(6), col_a(0xFFFFFFFFu, pulse));
                gfx_rect(bx - AP * 0.5f, by - AP * 0.5f, A(9), A(5), col_a(0x1A6A2AFFu, pulse));
                gfx_text("GO", bx + AP * 0.6f, by + AP * 0.2f, AP * 0.75f, 0xFFFFFFFFu);
            }
            if (w->ab != AB_NONE) {
                float ax = (w->ax + 0.5f) * TS, ay = (w->ay + 0.5f) * TS;
                if (w->ab == AB_BREACH) {
                    float pulse = 0.6f + 0.4f * sinf(t * 6.0f);
                    gfx_ring(ax, ay, A(6), AP, 4, PI_F * 0.25f, col_a(0xFF8A30FFu, pulse));
                    icon_ability(AB_BREACH, ax, ay, A(3), 0xFF8A30FFu);
                } else {
                    /* throw arc */
                    for (int s = 1; s < 10; s++) {
                        float u = (float)s / 10.0f;
                        float qx = x + (ax - x) * u, qy = y + (ay - y) * u - sinf(u * PI_F) * TS * 0.5f;
                        gfx_poly(qx, qy, AP * 0.6f, 4, 0, col_a(0xFFFFFFFFu, 0.7f));
                    }
                    if (sel) gfx_ring(ax, ay, w->ab == AB_FLASH ? 4.6f * TS : 2.3f * TS, AP * 0.5f, 24, t, col_a(w->ab == AB_FLASH ? 0xFFFFFFFFu : 0xB0B0B0FFu, 0.14f));
                    icon_ability(w->ab, ax, ay, A(3), 0xFFFFFFFFu);
                }
            }
        }
        /* name tag */
        if (planning || o->manual) {
            float ts = AP * (1.1f / (g->zoom < 1.2f ? g->zoom : 1.2f));
            if (i == g->sel || o->manual) gfx_text_sc(op_name(i), o->x * TS, o->y * TS - A(8) - ts * 7, ts, c);
            else { char in[2] = { op_name(i)[0], 0 }; gfx_text_sc(in, o->x * TS, o->y * TS + A(8), ts * 0.8f, col_a(c, 0.8f)); }
        }
    }
}

static void markers(const Game *g)
{
    float t = g->t;
    int planning = g->scene == SC_PLAN || g->tac;
    /* hostages */
    for (int h = 0; h < g->nhs; h++) {
        const Hostage *hs = &g->hs[h];
        float x = hs->x * TS, y = hs->y * TS;
        if (hs->state == HS_CAPTIVE) {
            float p = 0.6f + 0.4f * sinf(t * 4.0f);
            gfx_ring(x, y, A(7), AP, 16, 0, col_a(0xF0F4FFFFu, 0.5f * p));
            gfx_text_sc("HOSTAGE", x, y - A(11), AP * 0.75f / (g->zoom < 1.2f ? g->zoom : 1.2f) * 1.2f, col_a(0xF0F4FFFFu, p));
            if (hs->triggered && hs->exec < 90) {
                float k = hs->exec / MISSIONS[g->mission].exec_time;
                if (k < 0) k = 0;
                gfx_arc(x, y, A(8), A(9.5f), -PI_F * 0.5f, -PI_F * 0.5f + TAU_F * k, 24, 0xFF3020FFu, 0xFF3020FFu);
                char b[8]; snprintf(b, sizeof b, "%.1f", hs->exec > 0 ? hs->exec : 0.0f);
                gfx_text_sc(b, x, y + A(11), AP * 1.0f, 0xFF5040FFu);
            }
        } else if (hs->state == HS_SECURED) {
            gfx_ring(x, y, A(7), AP, 16, 0, 0x50FF80A0u);
            gfx_line(x - A(3), y, x - A(1), y + A(2.5f), AP, 0x60FF90FFu, 0x60FF90FFu);
            gfx_line(x - A(1), y + A(2.5f), x + A(3.5f), y - A(3), AP, 0x60FF90FFu, 0x60FF90FFu);
        }
    }
    /* hostiles we know about but can't see */
    for (int i = 0; i < g->nen; i++) {
        const Enemy *e = &g->en[i];
        if (!e->alive) continue;
        if (e->seen) {
            if (e->state == ES_COMBAT && e->react <= 0) { /* aiming at us */ }
            else if (e->stun > 0 || e->blind > 0) {
                for (int k = 0; k < 3; k++) {
                    float a = t * 5.0f + (float)k * 2.09f;
                    gfx_poly(e->x * TS + cosf(a) * A(5), e->y * TS - A(7) + sinf(a) * A(2), AP * 0.8f, 5, a, 0xFFF080FFu);
                }
            } else if (e->state == ES_ALERT || (e->state == ES_COMBAT && e->react > 0)) {
                gfx_text_sc(e->state == ES_ALERT ? "?" : "!", e->x * TS, e->y * TS - A(12), AP * 1.2f, e->state == ES_ALERT ? 0xFFD040FFu : 0xFF4030FFu);
            }
            continue;
        }
        if (!e->ghost && e->mark <= 0) continue;
        float x = (e->mark > 0 ? e->x : e->gx) * TS, y = (e->mark > 0 ? e->y : e->gy) * TS, f = e->mark > 0 ? e->face : e->gface;
        float a = e->mark > 0 ? 0.8f : 0.45f;
        gfx_blend(BL_ADD);
        sprite_draw(e->type == EN_THUG ? SP_THUG : (e->type == EN_GUNMAN ? SP_GUNMAN : SP_HEAVY), x, y, f, AP, 0, col_a(0xFF3020FFu, a * 0.5f));
        gfx_blend(BL_ALPHA);
        gfx_ring(x, y, A(6), AP * 0.8f, 10, 0, col_a(0xFF4030FFu, a));
        if (planning) {
            /* sight cone */
            gfx_arc(x, y, A(7), A(16), f - 0.6f, f + 0.6f, 6, col_a(0xFF4030FFu, 0.18f), col_a(0xFF4030FFu, 0.18f));
        }
    }
    /* rooms nobody has seen */
    if (g->scene == SC_PLAN || g->tac) {
        float sx[MAX_ROOMS], sy[MAX_ROOMS]; int n[MAX_ROOMS];
        memset(n, 0, sizeof n); memset(sx, 0, sizeof sx); memset(sy, 0, sizeof sy);
        for (int y = 0; y < g->H; y++)
            for (int x = 0; x < g->W; x++) {
                const Tile *tl = &g->tile[y][x];
                if (tl->kind != K_FLOOR || tl->room == 0 || tl->room >= MAX_ROOMS || g->room_seen[tl->room]) continue;
                sx[tl->room] += (float)x + 0.5f; sy[tl->room] += (float)y + 0.5f; n[tl->room]++;
            }
        for (int r = 1; r < MAX_ROOMS; r++)
            if (n[r] > 3) gfx_text_sc("?", sx[r] / (float)n[r] * TS, sy[r] / (float)n[r] * TS - A(3), AP * 1.6f, 0x7A9AD080u);
    }
    /* the selected operator */
    if (g->scene == SC_EXEC && !g->tac) {
        const Op *o = &g->op[g->sel];
        if (o->present) {
            float r = A(7.5f) + sinf(t * 5.0f) * AP * 0.5f;
            gfx_ring(o->x * TS, o->y * TS, r, AP * 0.7f, 16, 0, col_a(op_color(g->sel), o->manual ? 0.9f : 0.5f));
        }
        for (int i = 0; i < NOPS; i++) {
            const Op *p = &g->op[i];
            if (!p->present) continue;
            if (p->state == OS_DOWN) {
                float k = 0.5f + 0.5f * sinf(t * 6.0f);
                gfx_text_sc("DOWN", p->x * TS, p->y * TS - A(12), AP, col_a(0xFF4030FFu, 0.5f + 0.5f * k));
            } else if (p->state == OS_REVIVE) {
                gfx_arc(p->x * TS, p->y * TS, A(8), A(9), -PI_F * 0.5f, -PI_F * 0.5f + TAU_F * (p->revive_t / 2.4f), 16, 0x60FF90FFu, 0x60FF90FFu);
            }
        }
    }
    /* the cursor */
    if (planning) {
        float x = floorf(g->cx) * TS, y = floorf(g->cy) * TS;
        uint32_t c = op_color(g->sel);
        float p = 0.7f + 0.3f * sinf(t * 7.0f), s = A(3);
        uint32_t cc = col_a(c, p);
        gfx_rect(x, y, s, AP, cc); gfx_rect(x, y, AP, s, cc);
        gfx_rect(x + TS - s, y, s, AP, cc); gfx_rect(x + TS - AP, y, AP, s, cc);
        gfx_rect(x, y + TS - AP, s, AP, cc); gfx_rect(x, y + TS - s, AP, s, cc);
        gfx_rect(x + TS - s, y + TS - AP, s, AP, cc); gfx_rect(x + TS - AP, y + TS - s, AP, s, cc);
        gfx_rect(x + AP, y + AP, TS - 2 * AP, TS - 2 * AP, col_a(c, 0.12f));
    }
}

/* ------------------------------------------------------------------ */
void draw_world(const Game *g)
{
    if (!g->W) return;
    set_view(g);
    for (int y = vy0; y <= vy1; y++)
        for (int x = vx0; x <= vx1; x++) {
            const Tile *t = &g->tile[y][x];
            if (t->kind == K_FLOOR || t->kind == K_DOOR || t->kind == K_RUBBLE || t->kind == K_RDOOR) floor_tile(g, x, y);
            if (t->kind == K_RUBBLE) rubble_tile(g, x, y);
        }
    draw_decals(g);
    for (int y = vy0; y <= vy1; y++)
        for (int x = vx0; x <= vx1; x++) {
            const Tile *t = &g->tile[y][x];
            if (t->kind == K_FLOOR && t->furn) furn_tile(g, x, y);
            if (t->light) fixture(g, x, y);
        }
    draw_characters(g);
    for (int y = vy0; y <= vy1; y++)
        for (int x = vx0; x <= vx1; x++) {
            const Tile *t = &g->tile[y][x];
            if (t->kind == K_WALL || t->kind == K_SOFT) wall_tile(g, x, y);
            else if (t->kind == K_WINDOW) window_tile(g, x, y);
            else if (t->kind == K_DOOR || t->kind == K_RDOOR) door_tile(g, x, y);
        }
    draw_particles(g, 0);
    lighting(g);
    glow_pass(g);
    if (g->scene == SC_PLAN || g->scene == SC_EXEC) {
        plan_overlay(g);
        markers(g);
    }
    gfx_view_reset();
    if (g->lightning > 0.01f) gfx_rect(0, 0, VW, VH, RGBA(200, 210, 255, (int)(g->lightning * 30)));
    if (g->flash_t > 0) { gfx_blend(BL_ADD); gfx_rect(0, 0, VW, VH, RGBA(255, 255, 255, (int)(g->flash_t / 0.6f * 220))); gfx_blend(BL_ALPHA); }
}
