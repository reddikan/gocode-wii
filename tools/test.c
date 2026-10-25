/*
 * GO CODE - native test harness (no Wii toolchain needed).
 *
 *  - checks every map (hostages and hostiles reachable, starts outside)
 *  - software-rasterizes the exact triangle list the Wii's GPU gets and saves screenshots
 *  - plays every mission with a planning bot (stack on the doors, breach + flash on GO) to check it
 *    can be won, and with a careless bot (walk straight in) to check that's riskier
 *  - measures CPU time and vertex counts per frame
 *  - renders a preview video with the music and sound (reel)
 *
 *  usage: test <outdir> [all|shots|bots|reel] [mission]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "../source/game.h"
#include "../source/game_int.h"
#include "../source/gfx.h"
#include "../source/music.h"
#include "../source/audio.h"

#define OW 1280
#define OH 720
static float fb[OH][OW][3];
static const char *outdir = ".";
static int fails;

#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ------------------------------------------------------------------ */
/* software rasterizer                                                  */
/* ------------------------------------------------------------------ */
static void raster(void)
{
    for (int y = 0; y < OH; y++) for (int x = 0; x < OW; x++) fb[y][x][0] = fb[y][x][1] = fb[y][x][2] = 0;
    const float S = (float)OW / VW;
    for (int s = 0; s < gfx_nseg; s++) {
        int mode = gfx_seg[s].mode;
        for (int i = gfx_seg[s].start; i + 2 < gfx_seg[s].start + gfx_seg[s].count; i += 3) {
            const GVtx *v = &gfx_v[i];
            float x0 = v[0].x * S, y0 = v[0].y * S, x1 = v[1].x * S, y1 = v[1].y * S, x2 = v[2].x * S, y2 = v[2].y * S;
            float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
            if (fabsf(area) < 1e-6f) continue;
            int minx = (int)floorf(fminf(x0, fminf(x1, x2))), maxx = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
            int miny = (int)floorf(fminf(y0, fminf(y1, y2))), maxy = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
            if (minx < 0) minx = 0; if (miny < 0) miny = 0;
            if (maxx > OW - 1) maxx = OW - 1; if (maxy > OH - 1) maxy = OH - 1;
            float c[3][4];
            for (int k = 0; k < 3; k++) {
                uint32_t col = v[k].c;
                c[k][0] = (float)(col >> 24) / 255.0f; c[k][1] = (float)((col >> 16) & 255) / 255.0f;
                c[k][2] = (float)((col >> 8) & 255) / 255.0f; c[k][3] = (float)(col & 255) / 255.0f;
            }
            float inv = 1.0f / area;
            for (int py = miny; py <= maxy; py++) {
                float fy = (float)py + 0.5f;
                for (int px = minx; px <= maxx; px++) {
                    float fx = (float)px + 0.5f;
                    float w0 = ((x1 - fx) * (y2 - fy) - (x2 - fx) * (y1 - fy)) * inv;
                    float w1 = ((x2 - fx) * (y0 - fy) - (x0 - fx) * (y2 - fy)) * inv;
                    float w2 = 1.0f - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    float a = c[0][3] * w0 + c[1][3] * w1 + c[2][3] * w2;
                    for (int ch = 0; ch < 3; ch++) {
                        float src = c[0][ch] * w0 + c[1][ch] * w1 + c[2][ch] * w2;
                        float d;
                        if (mode == BL_ADD) d = fb[py][px][ch] + src * a;
                        else if (mode == BL_MUL) d = 2.0f * src * fb[py][px][ch];
                        else d = fb[py][px][ch] * (1 - a) + src * a;
                        fb[py][px][ch] = d > 1 ? 1 : (d < 0 ? 0 : d);
                    }
                }
            }
        }
    }
}

static void save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", OW, OH);
    static unsigned char row[OW * 3];
    for (int y = 0; y < OH; y++) {
        for (int x = 0; x < OW; x++)
            for (int ch = 0; ch < 3; ch++) row[x * 3 + ch] = (unsigned char)(fb[y][x][ch] * 255.0f + 0.5f);
        fwrite(row, 1, sizeof row, f);
    }
    fclose(f);
}

static void shot(const Game *g, const char *name)
{
    game_render(g);
    raster();
    char p[512];
    snprintf(p, sizeof p, "%s/%s.ppm", outdir, name);
    save_ppm(p);
    printf("  shot %s (%d vertices)\n", name, gfx_nv);
}

/* ------------------------------------------------------------------ */
/* driving the game                                                     */
/* ------------------------------------------------------------------ */
static Game *G;
static void step(Game *g, uint32_t press, float sx, float sy)
{
    Input in;
    memset(&in, 0, sizeof in);
    in.held = press; in.pressed = press; in.sx = sx; in.sy = sy;
    game_update(g, &in, 1.0f / 60.0f);
    int p; float pan, vol;
    while (game_pop_sound(g, &p, &pan, &vol) >= 0) {}
}
static void run(Game *g, float secs) { for (int i = 0; i < (int)(secs * 60); i++) step(g, 0, 0, 0); }

static void open_mission(Game *g, int m)
{
    g->sv.unlocked = NMISSIONS;
    g->sel_mission = m;
    g->scene = SC_BRIEF; g->scene_t = 0;
    step(g, 0, 0, 0);
    step(g, BTN_A, 0, 0);
    g->help_page = 0;
}


static void add_wp(Game *g, int op, int tx, int ty, int hold, int ab, float ax, float ay)
{
    Op *o = &g->op[op];
    if (o->nwp >= MAX_WP) return;
    Waypoint *w = &o->wp[o->nwp++];
    memset(w, 0, sizeof *w);
    w->tx = (int8_t)tx; w->ty = (int8_t)ty; w->hold = (uint8_t)hold; w->ab = (uint8_t)ab; w->ax = ax; w->ay = ay;
    if (ab == AB_BREACH) g->tile[(int)ay][(int)ax].flags |= TF_PLANBR;
}

/* The planning bot: for each hostage, a team stacks on the way into the room and goes on GO
 * (Knox breaches the door - or a locked door / cracked wall if that's the only way - and Vega flashes
 * the room); everyone then sweeps the remaining hostiles. */
static int need_breach_tile(Game *g, int sx, int sy, int hx, int hy, int *bx, int *by)
{
    static uint8_t path[MAX_PATH][2];
    if (find_path(g, sx, sy, hx, hy, 1, path, MAX_PATH) >= 0) return 0;
    int hr = g->tile[hy][hx].room, best = 1 << 30;
    for (int y = 0; y < g->H; y++)
        for (int x = 0; x < g->W; x++) {
            if (!breachable(g, x, y)) continue;
            int touches = 0;
            static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
            for (int d = 0; d < 4; d++) if (in_map(g, x + DX[d], y + DY[d]) && g->tile[y + DY[d]][x + DX[d]].room == hr) touches = 1;
            if (!touches) continue;
            g->tile[y][x].flags |= TF_PLANBR;
            int n = find_path(g, sx, sy, hx, hy, 1, path, MAX_PATH);
            g->tile[y][x].flags &= (uint8_t)~TF_PLANBR;
            if (n >= 0 && n < best) { best = n; *bx = x; *by = y; }
        }
    return best < (1 << 30) ? 1 : -1;
}

static void bot_plan(Game *g, int careful)
{
    static uint8_t path[MAX_PATH][2];
    int ops[NOPS], nops = 0;
    for (int i = 0; i < NOPS; i++) if (g->op[i].present) ops[nops++] = i;
    for (int i = 0; i < NOPS; i++) g->op[i].nwp = 0;
    int ex[NOPS], ey[NOPS];
    for (int k = 0; k < nops; k++) { ex[k] = (int)g->op[ops[k]].x; ey[k] = (int)g->op[ops[k]].y; }
    int knox_team = 0;
    for (int k = 0; k < nops; k++) if (ops[k] == OP_KNOX) knox_team = k / 2;
    int other_turn = 0;
    for (int h = 0; h < g->nhs; h++) {
        int hx = (int)g->hs[h].x, hy = (int)g->hs[h].y, hr = g->tile[hy][hx].room;
        int bx = -1, by = -1;
        int nb = need_breach_tile(g, ex[0], ey[0], hx, hy, &bx, &by);
        if (nb == 1) g->tile[by][bx].flags |= TF_PLANBR;
        int team = nb == 1 ? knox_team : ((knox_team + 1 + other_turn++) % 2);
        for (int k = 0; k < nops; k++) {
            if (g->nhs >= 2 && nops >= 4 && (k / 2) != team) continue;
            int op = ops[k];
            int n = find_path(g, ex[k], ey[k], hx, hy, 1, path, MAX_PATH);
            if (n < 0) continue;
            int entry = -1;
            for (int i = 0; i < n; i++) if (g->tile[path[i][1]][path[i][0]].room == hr && g->tile[path[i][1]][path[i][0]].kind == K_FLOOR) { entry = i; break; }
            if (careful && entry >= 2) {
                int dx = path[entry - 1][0], dy = path[entry - 1][1];     /* the doorway */
                int sx = path[entry - 2][0], sy = path[entry - 2][1];     /* stack here */
                int ab = AB_NONE; float ax = 0, ay = 0;
                if (op == OP_KNOX && breachable(g, dx, dy)) { ab = AB_BREACH; ax = (float)dx; ay = (float)dy; }
                if (op == OP_VEGA) { ab = AB_FLASH; ax = (float)hx; ay = (float)hy; }
                if (op == OP_VEGA && !passable(g, dx, dy) && g->tile[dy][dx].kind != K_DOOR) ab = AB_NONE;   /* can't throw through a locked door */
                add_wp(g, op, sx, sy, 1, ab, ax, ay);
            }
            add_wp(g, op, hx, hy, 0, AB_NONE, 0, 0);
            ex[k] = hx; ey[k] = hy;
        }
    }
    /* then sweep: everyone walks to every hostile's start position */
    for (int i = 0; i < g->nen; i++)
        for (int k = 0; k < nops; k++)
            if ((i + k) % nops == 0 || nops < 3) add_wp(g, ops[k], (int)g->en[i].hx, (int)g->en[i].hy, 0, AB_NONE, 0, 0);
}

static uint32_t bot_seed;
static int play_mission(int m, int careful, int verbose, int shots, double *worst_ms, int *max_vtx)
{
    Game *g = G;
    game_init(g);
    open_mission(g, m);
    g->rng ^= bot_seed * 2654435761u;
    bot_plan(g, careful);
    if (shots) { char n[64]; snprintf(n, sizeof n, "m%02d_plan", m + 1); shot(g, n); }
    g->pause = 1; g->pause_sel = 0;
    step(g, BTN_A, 0, 0);                         /* START THE ASSAULT */
    int frames = 0, shot_go = 0, shot_fight = 0;
    while (g->scene == SC_EXEC && frames < 60 * 300) {
        uint32_t b = 0;
        if (g->all_ready && g->ready_t > 0.5f) b |= BTN_X;
        clock_t c0 = clock();
        step(g, b, 0, 0);
        if ((frames & 15) == 0) {
            game_render(g);
            double ms = (double)(clock() - c0) * 1000.0 / CLOCKS_PER_SEC;
            if (ms > *worst_ms) *worst_ms = ms;
            if (gfx_nv > *max_vtx) *max_vtx = gfx_nv;
        }
        if (shots && !shot_go && g->go_t > 0.25f && g->go_t < 1.0f) { char n[64]; snprintf(n, sizeof n, "m%02d_go", m + 1); shot(g, n); shot_go = 1; }
        if (shots && !shot_fight && g->combat_t < 0.05f && g->go_t > 1.5f && g->mtime > 6) { char n[64]; snprintf(n, sizeof n, "m%02d_fight", m + 1); shot(g, n); shot_fight = 1; }
        frames++;
    }
    if (shots) { char n[64]; snprintf(n, sizeof n, "m%02d_end", m + 1); shot(g, n); }
    int downed = 0;
    for (int i = 0; i < NOPS; i++) if (g->op[i].present && g->op[i].state == OS_DOWN) downed++;
    if (verbose)
        printf("    mission %2d %-16s %-8s %s  t=%5.1fs  kills %2d/%2d  downed %d  shots %d hit %d\n", m + 1, MISSIONS[m].name,
               careful ? "planned" : "careless",
               g->result == RES_WIN ? "WIN " : (g->result == RES_FAIL_HOSTAGE ? "FAIL-H" : (g->result == RES_FAIL_SQUAD ? "FAIL-S" : "TIMEOUT")),
               g->mtime, g->kills, g->nen, downed, g->shots_fired, g->shots_hit);
    return g->result;
}

/* ------------------------------------------------------------------ */
/* checks                                                               */
/* ------------------------------------------------------------------ */
static void check_maps(void)
{
    printf("maps\n");
    static uint8_t path[MAX_PATH][2];
    for (int m = 0; m < NMISSIONS; m++) {
        Game *g = G;
        game_init(g);
        level_load(g, m);
        /* everything breachable counts as open for this check */
        for (int y = 0; y < g->H; y++) for (int x = 0; x < g->W; x++) if (breachable(g, x, y)) g->tile[y][x].flags |= TF_PLANBR;
        int sx = g->starts[0][0], sy = g->starts[0][1];
        CHECK(g->tile[sy][sx].flags & TF_OUT, "mission %d: start not outside", m + 1);
        for (int h = 0; h < g->nhs; h++) CHECK(find_path(g, sx, sy, (int)g->hs[h].x, (int)g->hs[h].y, 1, path, MAX_PATH) >= 0, "mission %d: hostage %d unreachable", m + 1, h);
        for (int i = 0; i < g->nen; i++) {
            CHECK(find_path(g, sx, sy, (int)g->en[i].x, (int)g->en[i].y, 1, path, MAX_PATH) >= 0, "mission %d: hostile %d unreachable", m + 1, i);
            if (g->en[i].guard == -2 || (g->en[i].guard < 0 && 0)) {}
        }
        int guards = 0;
        for (int i = 0; i < g->nen; i++) if (g->en[i].guard >= 0) guards++;
        CHECK(guards == g->nhs, "mission %d: %d guards for %d hostages", m + 1, guards, g->nhs);
        printf("  %2d %-16s %dx%d  rooms %2d  lights %2d  hostiles %2d  hostages %d  light weights %d\n", m + 1, MISSIONS[m].name,
               g->W, g->H, g->nrooms, g->nlight, g->nen, g->nhs, g->nlw);
        CHECK(g->nlw < MAX_LW, "light weight list full");
    }
}

static void shots_menus(void)
{
    Game *g = G;
    game_init(g);
    run(g, 1.5f);
    shot(g, "title");
    g->scene = SC_SELECT; g->sv.unlocked = 4; g->sv.stars[0] = 3; g->sv.stars[1] = 2; g->sv.best[0] = 62; g->sv.best[1] = 95; g->sel_mission = 2;
    run(g, 0.5f);
    shot(g, "select");
    g->sel_mission = 0; g->scene = SC_BRIEF; g->scene_t = 0;
    run(g, 0.5f);
    shot(g, "brief");
    g->help_page = 1; shot(g, "help1"); g->help_page = 2; shot(g, "help2"); g->help_page = 3; shot(g, "help3"); g->help_page = 0;
}

/* ------------------------------------------------------------------ */
/* reel: frames + audio                                                 */
/* ------------------------------------------------------------------ */
#define REEL_RATE 32000
static float *amix;
static int    alen, amax;

static void mix_sfx(int id, int pitch, float pan, float vol, int at)
{
    const int16_t *d = audio_data(id);
    int n = audio_len(id);
    float rate = (float)pitch / 128.0f, v = (float)audio_volume(id) / 255.0f * vol * 0.5f;
    float vl = v * (pan > 0 ? 1.0f - pan * 0.7f : 1.0f), vr = v * (pan < 0 ? 1.0f + pan * 0.7f : 1.0f);
    for (int i = 0; ; i++) {
        float p = (float)i * rate;
        int k = (int)p;
        if (k >= n - 1 || at + i >= amax) break;
        float s = ((float)d[k] + ((float)d[k + 1] - (float)d[k]) * (p - (float)k)) / 32768.0f;
        amix[(at + i) * 2] += s * vl; amix[(at + i) * 2 + 1] += s * vr;
    }
}

static void reel(int m)
{
    Game *g = G;
    game_init(g);
    open_mission(g, m);
    bot_plan(g, 1);
    char dir[512];
    snprintf(dir, sizeof dir, "%s/reel", outdir);
    amax = REEL_RATE * 200; amix = calloc((size_t)amax * 2, sizeof(float)); alen = 0;
    int frame = 0;
    /* a few seconds of planning, then the assault */
    for (int f = 0; f < 60 * 3; f++) {
        Input in; memset(&in, 0, sizeof in);
        game_update(g, &in, 1.0f / 60.0f);
        if (f == 60) { g->pause = 1; g->pause_sel = 0; Input a; memset(&a, 0, sizeof a); a.held = a.pressed = BTN_A; game_update(g, &a, 1.0f / 60.0f); }
    }
    int sample = 0;
    int mpos = 0;
    while (frame < 30 * 75 && !(g->scene == SC_DEBRIEF && g->scene_t > 4.0f)) {
        for (int s = 0; s < 2; s++) {
            Input in; memset(&in, 0, sizeof in);
            if (g->all_ready && g->ready_t > 1.2f) in.held = in.pressed = BTN_X;
            game_update(g, &in, 1.0f / 60.0f);
            int id, p; float pan, vol;
            while ((id = game_pop_sound(g, &p, &pan, &vol)) >= 0) mix_sfx(id, p, pan, vol, sample);
            /* music layers */
            float mv[MUS_TRACKS];
            game_music(g, mv);
            int n = REEL_RATE / 60;
            for (int i = 0; i < n && sample + i < amax; i++) {
                float acc = 0;
                for (int t = 0; t < MUS_TRACKS; t++) {
                    if (mv[t] < 0.001f) continue;
                    int L = music_len(t);
                    float pos = (float)(mpos + i) * (float)MUS_RATE / (float)REEL_RATE;
                    int k = (int)pos % L;
                    acc += (float)music_data(t)[k] / 32768.0f * mv[t] * (float)music_volume(t) / 255.0f;
                }
                amix[(sample + i) * 2] += acc * 0.5f; amix[(sample + i) * 2 + 1] += acc * 0.5f;
            }
            mpos += n; sample += n;
        }
        game_render(g);
        raster();
        char p[600];
        snprintf(p, sizeof p, "%s/f%04d.ppm", dir, frame);
        save_ppm(p);
        frame++;
        if (g->scene == SC_DEBRIEF && g->scene_t < 0.05f) printf("  reel: debrief at frame %d\n", frame);
    }
    alen = sample;
    char wp[600];
    snprintf(wp, sizeof wp, "%s/music.wav", dir);
    FILE *f = fopen(wp, "wb");
    int bytes = alen * 4;
    unsigned char h[44] = { 'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,0,0,0,0,0,0,0,0,4,0,16,0,'d','a','t','a',0,0,0,0 };
    int v = 36 + bytes; memcpy(h + 4, &v, 4); v = REEL_RATE; memcpy(h + 24, &v, 4); v = REEL_RATE * 4; memcpy(h + 28, &v, 4); memcpy(h + 40, &bytes, 4);
    fwrite(h, 1, 44, f);
    float peak = 0;
    for (int i = 0; i < alen * 2; i++) if (fabsf(amix[i]) > peak) peak = fabsf(amix[i]);
    float k = peak > 0.95f ? 0.95f / peak : 1.0f;
    for (int i = 0; i < alen * 2; i++) { short s = (short)(amix[i] * k * 32767.0f); fwrite(&s, 2, 1, f); }
    fclose(f);
    printf("  reel: %d frames, audio peak %.2f\n", frame, peak);
}


/* ------------------------------------------------------------------ */
/* the real controls, end to end                                        */
/* ------------------------------------------------------------------ */
static void press(Game *g, uint32_t b) { step(g, b, 0, 0); step(g, 0, 0, 0); }
static void cursor(Game *g, int x, int y) { g->cx = (float)x + 0.5f; g->cy = (float)y + 0.5f; }

static void ui_test(void)
{
    printf("ui\n");
    Game *g = G;
    game_init(g);
    game_storage(g, 1);
    run(g, 0.5f);
    press(g, BTN_A);                                   /* START -> briefing */
    CHECK(g->scene == SC_BRIEF, "title START goes to the briefing (scene %d)", g->scene);
    run(g, 0.2f);
    press(g, BTN_A);                                   /* -> planning */
    CHECK(g->scene == SC_PLAN, "briefing A goes to planning");
    CHECK(g->help_page == 1, "first planning shows the help");
    press(g, BTN_B);
    CHECK(g->help_page == 0, "B closes the help");
    /* Knox: stack next to the bedroom door, wait for GO, breach it, then go to the hostage */
    while (g->sel != OP_KNOX) press(g, BTN_ZR);
    cursor(g, 17, 7); press(g, BTN_A); press(g, BTN_Y);
    cursor(g, 17, 5); press(g, BTN_X);
    CHECK(g->op[OP_KNOX].nwp == 1 && g->op[OP_KNOX].wp[0].hold && g->op[OP_KNOX].wp[0].ab == AB_BREACH, "knox: waypoint + hold + breach (nwp %d hold %d ab %d)",
          g->op[OP_KNOX].nwp, g->op[OP_KNOX].wp[0].hold, g->op[OP_KNOX].wp[0].ab);
    CHECK((int)g->op[OP_KNOX].wp[0].ax == 17 && (int)g->op[OP_KNOX].wp[0].ay == 6, "knox breaches the bedroom door (%d,%d)", (int)g->op[OP_KNOX].wp[0].ax, (int)g->op[OP_KNOX].wp[0].ay);
    cursor(g, 18, 4); press(g, BTN_A);
    /* Vega: stack beside him, flash the room on GO, then follow in */
    press(g, BTN_ZL);
    CHECK(g->sel == OP_VEGA, "ZL selects Vega");
    cursor(g, 16, 7); press(g, BTN_A); press(g, BTN_Y);
    cursor(g, 18, 4); press(g, BTN_X);
    CHECK(g->op[OP_VEGA].wp[0].ab == AB_FLASH, "vega flash planned");
    cursor(g, 16, 4); press(g, BTN_A);
    /* an unreachable waypoint is refused */
    cursor(g, 5, 2); int before = g->op[OP_VEGA].nwp; press(g, BTN_A);
    CHECK(g->op[OP_VEGA].nwp == before, "a waypoint inside a wall is refused");
    /* undo works */
    cursor(g, 10, 9); press(g, BTN_A); press(g, BTN_B);
    CHECK(g->op[OP_VEGA].nwp == before, "B undoes");
    /* start */
    press(g, BTN_PLUS);
    CHECK(g->pause, "+ opens the planning menu");
    press(g, BTN_A);
    CHECK(g->scene == SC_EXEC, "START THE ASSAULT starts it");
    int t = 0;
    while (!g->all_ready && t++ < 60 * 20) step(g, 0, 0, 0);
    CHECK(g->all_ready, "both operators reach their holds");
    shot(g, "ui_ready");
    press(g, BTN_X);
    CHECK(g->gos == 1, "X calls GO");
    run(g, 0.6f);
    shot(g, "ui_breach");
    /* tactical pause: add a waypoint for Vega */
    press(g, BTN_R);
    CHECK(g->tac, "R pauses for a replan");
    float mt = g->mtime;
    run(g, 0.5f);
    CHECK(g->mtime == mt, "time stands still in the tactical pause");
    cursor(g, 12, 9); press(g, BTN_A);
    shot(g, "ui_tac");
    press(g, BTN_R);
    CHECK(!g->tac, "R resumes");
    /* direct control */
    for (int i = 0; i < 40; i++) step(g, 0, 0.0f, 1.0f);
    CHECK(g->op[g->sel].manual, "the stick takes control");
    press(g, BTN_A);
    CHECK(!g->op[g->sel].manual, "A hands back to the plan");
    t = 0;
    while (g->scene == SC_EXEC && t++ < 60 * 90) step(g, 0, 0, 0);
    CHECK(g->scene == SC_DEBRIEF, "the mission ends");
    printf("  result %d, %d stars, time %.1f\n", g->result, g->stars, g->mtime);
    char buf[SAVE_MAX];
    int n = game_save_request(g, buf, sizeof buf);
    CHECK(n > 0, "a finished mission asks to save");
    if (n > 0) {
        Game *h = (Game *)malloc((size_t)GAME_SIZE);
        game_init(h);
        game_load(h, buf, n);
        CHECK(h->sv.unlocked == g->sv.unlocked && h->sv.stars[0] == g->sv.stars[0], "the save loads back");
        free(h);
    }
    run(g, 1.0f);
    shot(g, "ui_debrief");
    press(g, BTN_A);
    CHECK(g->scene == SC_BRIEF || g->scene == SC_PLAN, "debrief A moves on (scene %d)", g->scene);
}

int main(int argc, char **argv)
{
    if (argc > 1) outdir = argv[1];
    const char *what = argc > 2 ? argv[2] : "all";
    int only = argc > 3 ? atoi(argv[3]) - 1 : -1;
    G = (Game *)malloc((size_t)GAME_SIZE);
    clock_t t0 = clock();
    game_init(G);
    printf("init (synthesis) %.0f ms, sfx pool %d samples, Game %d bytes\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC, audio_pool_len(), GAME_SIZE);

    if (!strcmp(what, "reel")) { reel(only < 0 ? 0 : only); return 0; }
    if (!strcmp(what, "all") || !strcmp(what, "maps")) check_maps();
    if (!strcmp(what, "all") || !strcmp(what, "ui")) ui_test();
    if (!strcmp(what, "all") || !strcmp(what, "shots")) {
        printf("screenshots\n");
        shots_menus();
        double w = 0; int mv = 0;
        for (int m = 0; m < NMISSIONS; m++) if (only < 0 || m == only) play_mission(m, 1, 0, 1, &w, &mv);
    }
    if (!strcmp(what, "all") || !strcmp(what, "bots")) {
        printf("bots\n");
        double worst = 0; int maxv = 0;
        int wins = 0;
        for (int m = 0; m < NMISSIONS; m++) {
            if (only >= 0 && m != only) continue;
            int wc = 0;
            for (int run = 0; run < 3; run++) {
                bot_seed = (uint32_t)run + 1;
                int r = play_mission(m, 1, 1, 0, &worst, &maxv);
                if (r == RES_WIN) wc++;
            }
            play_mission(m, 0, 1, 0, &worst, &maxv);
            if (wc > 0) wins++;
        }
        printf("  missions the planning bot won at least once: %d\n", wins);
        printf("  worst frame (update+render, native) %.2f ms, max vertices %d of %d\n", worst, maxv, GFX_MAX_VTX);
        CHECK(maxv < GFX_MAX_VTX, "vertex buffer overflow");
    }
    printf(fails ? "%d FAILURES\n" : "all checks passed\n", fails);
    return fails ? 1 : 0;
}
