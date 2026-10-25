/*
 * GO CODE - pixel sprites. Each one is painted at start-up from a few shapes on a small canvas,
 * outlined automatically, and stored as runs. See sprites.h.
 */
#include "sprites.h"
#include "gfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "fastmath.h"

#define CW 22
#define CH 16
#define MAXRUN 1600

typedef struct { int8_t x0, x1, y; uint8_t c; } Run;
typedef struct { int r0, n; float px, py; } Spr;

static Run runs[MAXRUN];
static int nrun;
static Spr spr[SP_COUNT];
static uint8_t cv[CH][CW];
static int ready;

static const uint32_t PAL[32] = {
    0x00000000u,
    0x0B0C0FFFu,  /*  1 outline          */
    0x262C36FFu,  /*  2 uniform dark     */
    0x3A4352FFu,  /*  3 uniform          */
    0x55606FFFu,  /*  4 uniform light    */
    0x2B3127FFu,  /*  5 helmet dark      */
    0x414A38FFu,  /*  6 helmet           */
    0x5F6B50FFu,  /*  7 helmet light     */
    0x2C2824FFu,  /*  8 glove            */
    0xC99C7AFFu,  /*  9 skin             */
    0x96705AFFu,  /* 10 skin shade       */
    0x17191DFFu,  /* 11 gun metal        */
    0x434850FFu,  /* 12 gun light        */
    0xDAD6CCFFu,  /* 13 white shirt      */
    0xA8875AFFu,  /* 14 rope / tan       */
    0xFFFFFFFFu,  /* 15 TEAM             */
    0x7A2B28FFu,  /* 16 hoodie           */
    0x9C3D36FFu,  /* 17 hoodie light     */
    0x4A1A18FFu,  /* 18 hoodie dark      */
    0x3B3B3EFFu,  /* 19 jacket           */
    0x56565AFFu,  /* 20 jacket light     */
    0x141416FFu,  /* 21 balaclava        */
    0x4C5232FFu,  /* 22 heavy armour     */
    0x666E44FFu,  /* 23 heavy light      */
    0x1E2016FFu,  /* 24 heavy helmet     */
    0x6B4A2CFFu,  /* 25 wood stock       */
    0x3B5A8AFFu,  /* 26 jeans            */
    0x5A3A22FFu,  /* 27 hair             */
    0xB4B0A6FFu,  /* 28 shirt shade      */
    0x2A4A70FFu,  /* 29 jeans dark       */
    0x8A6A3AFFu,  /* 30 belt             */
    0x6E6A60FFu,  /* 31 pouch            */
};

static void clear(void) { memset(cv, 0, sizeof cv); }
static void px(int x, int y, int c) { if (x >= 0 && y >= 0 && x < CW && y < CH) cv[y][x] = (uint8_t)c; }
static void rect(int x0, int y0, int x1, int y1, int c) { for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) px(x, y, c); }
static void ell(float cx, float cy, float rx, float ry, int c)
{
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++) {
            float dx = ((float)x + 0.5f - cx) / rx, dy = ((float)y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f) cv[y][x] = (uint8_t)c;
        }
}
static void line(int x0, int y0, int x1, int y1, int c)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
static void outline(void)
{
    static uint8_t o[CH][CW];
    memcpy(o, cv, sizeof o);
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++) {
            if (cv[y][x]) continue;
            int n = (x > 0 && cv[y][x - 1] && cv[y][x - 1] != 1) || (x < CW - 1 && cv[y][x + 1] && cv[y][x + 1] != 1) ||
                    (y > 0 && cv[y - 1][x] && cv[y - 1][x] != 1) || (y < CH - 1 && cv[y + 1][x] && cv[y + 1][x] != 1);
            if (n) o[y][x] = 1;
        }
    memcpy(cv, o, sizeof o);
}
static void bake(int id, float pivx, float pivy)
{
    outline();
    Spr *s = &spr[id];
    s->r0 = nrun; s->px = pivx; s->py = pivy;
    for (int y = 0; y < CH; y++) {
        int x = 0;
        while (x < CW) {
            int c = cv[y][x];
            if (!c) { x++; continue; }
            int x0 = x;
            while (x < CW && cv[y][x] == c) x++;
            if (nrun < MAXRUN) { runs[nrun].x0 = (int8_t)x0; runs[nrun].x1 = (int8_t)(x - 1); runs[nrun].y = (int8_t)y; runs[nrun].c = (uint8_t)c; nrun++; }
        }
    }
    s->n = nrun - s->r0;
}

/* ---- the cast (facing +x, body centred near (8, 8)) ---- */
static void op_body(void)
{
    rect(4, 5, 5, 10, 2);                 /* backpack */
    rect(4, 6, 4, 9, 31);
    ell(8.0f, 8.0f, 2.8f, 4.6f, 3);       /* shoulders + vest */
    ell(7.6f, 6.6f, 1.6f, 2.0f, 4);
    rect(9, 6, 9, 10, 2);                 /* chest rig */
    px(9, 7, 31); px(9, 9, 31);
    px(8, 4, 15); px(7, 4, 15); px(8, 12, 15); px(7, 12, 15);   /* team colour on the shoulders */
}
static void op_head(void)
{
    ell(8.8f, 8.0f, 2.3f, 2.2f, 6);       /* helmet */
    ell(8.4f, 7.5f, 1.2f, 1.1f, 7);
    px(8, 8, 15); px(9, 8, 15);           /* team stripe */
    px(10, 9, 5);
}
static void arms_long(void) { line(8, 5, 12, 8, 3); line(8, 11, 11, 10, 3); px(12, 8, 8); px(11, 10, 8); }
static void arms_short(void) { line(8, 5, 11, 7, 3); line(8, 11, 11, 9, 3); px(11, 7, 8); px(11, 9, 8); }

static void gun_rifle(int stock)
{
    rect(9, 9, 18, 9, 11); rect(11, 8, 14, 8, 12);
    px(13, 10, 11); px(19, 9, 11);
    if (stock) { rect(8, 9, 10, 9, 25); rect(14, 9, 16, 9, 25); }
}
static void gun_shotgun(void) { rect(9, 8, 17, 9, 11); rect(12, 10, 14, 10, 25); rect(11, 8, 13, 8, 12); }
static void gun_smg(void) { rect(9, 9, 14, 9, 11); rect(15, 9, 18, 9, 12); px(12, 10, 11); px(11, 8, 12); }
static void gun_pistol(void) { rect(11, 8, 14, 8, 11); px(12, 9, 11); px(13, 8, 12); }

void sprites_init(void)
{
    if (ready) return;
    ready = 1;
    nrun = 0;
    /* operators, one per weapon */
    const int OPS[4] = { SP_RIFLE, SP_SHOTGUN, SP_SMG, SP_PISTOL };
    for (int k = 0; k < 4; k++) {
        clear();
        op_body();
        switch (k) {
        case 0: gun_rifle(0); arms_long(); break;
        case 1: gun_shotgun(); arms_long(); break;
        case 2: gun_smg(); arms_short(); break;
        default: gun_pistol(); arms_short(); break;
        }
        op_head();
        bake(OPS[k], 8.0f, 8.0f);
    }
    /* SP_OP: an unarmed body for previews */
    clear(); op_body(); arms_short(); op_head(); bake(SP_OP, 8.0f, 8.0f);
    /* operator down: lying on the back, arms out */
    clear();
    ell(8.0f, 8.0f, 4.6f, 2.6f, 3); ell(7.0f, 7.4f, 2.0f, 1.2f, 4);
    line(6, 5, 4, 3, 3); line(9, 11, 10, 13, 3);
    rect(3, 7, 3, 9, 26);
    ell(13.4f, 8.0f, 2.1f, 2.0f, 6); px(13, 8, 15); px(14, 8, 15);
    px(6, 6, 15); px(6, 10, 15);
    bake(SP_OP_DOWN, 8.0f, 8.0f);

    /* thug: hoodie, pistol */
    clear();
    ell(8.0f, 8.0f, 2.6f, 4.3f, 16); ell(7.4f, 6.8f, 1.4f, 1.8f, 17);
    line(8, 11, 11, 10, 16); px(11, 10, 9); gun_pistol(); rect(11, 9, 14, 9, 11); px(12, 8, 0);
    ell(8.9f, 8.0f, 2.2f, 2.1f, 18); px(10, 8, 18); px(8, 7, 16);
    bake(SP_THUG, 8.0f, 8.0f);
    /* gunman: jacket, balaclava, AK */
    clear();
    ell(8.0f, 8.0f, 2.7f, 4.5f, 19); ell(7.4f, 6.6f, 1.5f, 1.9f, 20);
    gun_rifle(1); arms_long(); line(8, 5, 12, 8, 19); line(8, 11, 11, 10, 19); px(12, 8, 8); px(11, 10, 8);
    ell(8.9f, 8.0f, 2.1f, 2.0f, 21); px(10, 8, 9);
    bake(SP_GUNMAN, 8.0f, 8.0f);
    /* heavy: armour plates, helmet, shotgun */
    clear();
    ell(7.8f, 8.0f, 3.3f, 5.4f, 22); ell(7.2f, 6.4f, 1.8f, 2.4f, 23); rect(9, 5, 10, 11, 22); rect(10, 6, 10, 10, 23);
    gun_shotgun(); line(8, 4, 12, 8, 22); line(8, 12, 12, 10, 22);
    ell(9.0f, 8.0f, 2.5f, 2.4f, 24); px(10, 7, 5);
    bake(SP_HEAVY, 8.0f, 8.0f);
    /* a dead hostile (tinted per type when drawn) */
    clear();
    ell(8.0f, 8.0f, 4.6f, 2.7f, 19); ell(7.0f, 7.4f, 2.0f, 1.2f, 20);
    line(6, 5, 3, 4, 19); line(10, 11, 12, 13, 19);
    ell(13.4f, 8.2f, 2.0f, 1.9f, 21);
    bake(SP_EN_DEAD, 8.0f, 8.0f);

    /* hostage: sitting, hands tied behind, legs out front */
    clear();
    rect(9, 6, 15, 7, 26); rect(9, 9, 15, 10, 26); rect(10, 7, 15, 7, 29); rect(10, 10, 15, 10, 29);
    rect(16, 6, 16, 7, 8); rect(16, 9, 16, 10, 8);
    ell(7.6f, 8.0f, 2.6f, 4.0f, 13); ell(7.0f, 6.8f, 1.3f, 1.6f, 28);
    rect(4, 7, 5, 9, 14);                      /* the rope */
    ell(7.6f, 8.0f, 1.9f, 1.8f, 27); px(8, 8, 9);
    bake(SP_HOSTAGE, 8.0f, 8.0f);
    /* freed: arms forward, sitting up */
    clear();
    rect(9, 6, 15, 7, 26); rect(9, 9, 15, 10, 26); rect(10, 7, 15, 7, 29); rect(10, 10, 15, 10, 29);
    rect(16, 6, 16, 7, 8); rect(16, 9, 16, 10, 8);
    ell(7.6f, 8.0f, 2.6f, 4.0f, 13); ell(7.0f, 6.8f, 1.3f, 1.6f, 28);
    line(8, 4, 11, 6, 13); line(8, 12, 11, 10, 13); px(11, 6, 9); px(11, 10, 9);
    ell(7.6f, 8.0f, 1.9f, 1.8f, 27); px(8, 8, 9);
    bake(SP_HOSTAGE_FREE, 8.0f, 8.0f);
    clear();
    ell(8.0f, 8.0f, 4.4f, 2.5f, 13); rect(2, 7, 4, 9, 26); ell(13.2f, 8.0f, 1.9f, 1.8f, 27);
    bake(SP_HOSTAGE_DEAD, 8.0f, 8.0f);

    /* a boot */
    clear(); rect(8, 8, 9, 8, 8); bake(SP_FOOT, 8.5f, 8.5f);
    (void)SP_E_RIFLE;
}

static uint32_t mul(uint32_t a, uint32_t b)
{
    if (b == 0xFFFFFFFFu) return a;
    uint32_t r = ((a >> 24) * (b >> 24) / 255) << 24, g = (((a >> 16) & 255) * ((b >> 16) & 255) / 255) << 16;
    uint32_t bl = (((a >> 8) & 255) * ((b >> 8) & 255) / 255) << 8, al = (a & 255) * (b & 255) / 255;
    return r | g | bl | al;
}

void sprite_draw(int s, float x, float y, float a, float pxs, uint32_t team, uint32_t tint)
{
    if (!ready) sprites_init();
    const Spr *sp = &spr[s];
    float ca = cosf(a) * pxs, sa = sinf(a) * pxs;
    for (int i = 0; i < sp->n; i++) {
        const Run *r = &runs[sp->r0 + i];
        uint32_t c = r->c == PAL_TEAM ? team : PAL[r->c];
        c = mul(c, tint);
        float lx0 = (float)r->x0 - sp->px, lx1 = (float)r->x1 + 1.0f - sp->px;
        float ly0 = (float)r->y - sp->py, ly1 = ly0 + 1.0f;
        float x00 = x + lx0 * ca - ly0 * sa, y00 = y + lx0 * sa + ly0 * ca;
        float x10 = x + lx1 * ca - ly0 * sa, y10 = y + lx1 * sa + ly0 * ca;
        float x11 = x + lx1 * ca - ly1 * sa, y11 = y + lx1 * sa + ly1 * ca;
        float x01 = x + lx0 * ca - ly1 * sa, y01 = y + lx0 * sa + ly1 * ca;
        gfx_quad4(x00, y00, c, x10, y10, c, x11, y11, c, x01, y01, c);
    }
}
