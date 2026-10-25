/*
 * GO CODE - game flow: title, mission select, briefing, planning, the assault, debrief.
 * Also the planning controls, the camera, the music mix and the save data.
 */
#include "game_int.h"

#include <math.h>
#include <string.h>
#include "fastmath.h"

const int GAME_SIZE = (int)sizeof(Game);

/* ------------------------------------------------------------------ */
/* helpers                                                              */
/* ------------------------------------------------------------------ */
float rnd(Game *g)
{
    g->rng ^= g->rng << 13; g->rng ^= g->rng >> 17; g->rng ^= g->rng << 5;
    return (float)(g->rng >> 8) / 16777216.0f;
}
float rnds(Game *g) { return rnd(g) * 2.0f - 1.0f; }

void world_to_screen(const Game *g, float wx, float wy, float *sx, float *sy)
{
    *sx = (wx - g->camx) * g->zoom + VW * 0.5f + g->shake_x;
    *sy = (wy - g->camy) * g->zoom + VH * 0.5f + g->shake_y;
}

void sfx(Game *g, int id, float pitch, float x, float vol)
{
    if (!g->sv.sfx) return;
    int next = (g->snd_head + 1) % MAX_SND;
    if (next == g->snd_tail) return;
    float pan = 0;
    if (x >= 0) {
        float sx, sy;
        world_to_screen(g, x * TS, g->camy, &sx, &sy);
        pan = (sx - VW * 0.5f) / (VW * 0.5f) * 0.75f;
        if (pan > 0.9f) pan = 0.9f; else if (pan < -0.9f) pan = -0.9f;
        float off = fabsf(sx - VW * 0.5f) / (VW * 0.5f);
        if (off > 1.0f) vol *= 1.0f / (1.0f + (off - 1.0f) * 1.5f);
    }
    SndEv *e = &g->snd[g->snd_head];
    e->id = id; e->pitch = (int)(pitch * 128.0f); e->pan = pan; e->vol = vol;
    g->snd_head = next;
}

int game_pop_sound(Game *g, int *pitch, float *pan, float *vol)
{
    if (g->snd_tail == g->snd_head) return -1;
    SndEv *e = &g->snd[g->snd_tail];
    g->snd_tail = (g->snd_tail + 1) % MAX_SND;
    *pitch = e->pitch; *pan = e->pan; *vol = e->vol;
    return e->id;
}

void radio(Game *g, int op, const char *text, int voice)
{
    g->radio_op = op;
    g->radio[0] = 0;
    if (op >= 0) { strcpy(g->radio, op_name(op)); strcat(g->radio, ": "); }
    strncat(g->radio, text, sizeof g->radio - strlen(g->radio) - 1);
    g->radio_t = 3.0f;
    sfx(g, SFX_SQUELCH, 1.0f, -1, 0.5f);
    if (voice != SFX_SQUELCH) sfx(g, voice, 1.0f, -1, 1.0f);
}

void save_now(Game *g) { g->save_dirty = 1; }

void game_storage(Game *g, int ok) { g->storage_ok = ok; }

void game_load(Game *g, const void *buf, int len)
{
    SaveData s;
    if (len < (int)sizeof s) return;
    memcpy(&s, buf, sizeof s);
    if (s.magic != SAVE_MAGIC || s.ver != 1) return;
    if (s.unlocked < 1) s.unlocked = 1;
    if (s.unlocked > NMISSIONS) s.unlocked = NMISSIONS;
    g->sv = s;
    g->sel_mission = s.last < NMISSIONS ? s.last : 0;
}

int game_save_request(Game *g, void *buf, int max)
{
    if (!g->save_dirty || max < (int)sizeof(SaveData)) return 0;
    g->save_dirty = 0;
    memcpy(buf, &g->sv, sizeof g->sv);
    return (int)sizeof g->sv;
}

void game_save_result(Game *g, int ok) { if (ok) { g->saved_t = 2.0f; g->save_fail = 0; } else g->save_fail = 1; }

/* ------------------------------------------------------------------ */
/* init                                                                 */
/* ------------------------------------------------------------------ */
void game_init(Game *g)
{
    memset(g, 0, sizeof *g);
    g->rng = 0x9E3779B9u;
    g->sv.magic = SAVE_MAGIC; g->sv.ver = 1; g->sv.unlocked = 1; g->sv.music = 1; g->sv.sfx = 1;
    audio_init();
    music_init();
    g->scene = SC_TITLE;
    g->zoom = 1.0f;
    level_load(g, 0);                     /* a backdrop for the title */
    g->camx = (float)g->W * TS * 0.5f; g->camy = (float)g->H * TS * 0.5f;
    g->next_lightning = 4.0f;
}

static void set_scene(Game *g, int s) { g->scene = s; g->scene_t = 0; g->pause = 0; g->confirm = 0; }

static void start_mission(Game *g, int m)
{
    /* keep the effects/sound/save state, reset the rest of the mission */
    level_load(g, m);
    memset(g->op, 0, sizeof g->op);
    memset(g->pt, 0, sizeof g->pt); g->npt = 0;
    memset(g->dc, 0, sizeof g->dc); g->ndc = 0; g->dc_head = 0;
    memset(g->tr, 0, sizeof g->tr); memset(g->dl, 0, sizeof g->dl);
    memset(g->sm, 0, sizeof g->sm); memset(g->nd, 0, sizeof g->nd); memset(g->ch, 0, sizeof g->ch);
    memset(g->room_seen, 0, sizeof g->room_seen);
    const Mission *ms = &MISSIONS[m];
    int k = 0;
    for (int i = 0; i < NOPS; i++) {
        Op *o = &g->op[i];
        if (!(ms->squad & (1 << i))) continue;
        o->present = 1;
        o->x = (float)g->starts[k][0] + 0.5f; o->y = (float)g->starts[k][1] + 0.5f;
        o->face = -PI_F * 0.5f;
        o->hp = 100; o->state = OS_IDLE; o->target = -1; o->cur = 0;
        o->charges = (i == OP_KNOX) ? 3 : 2;
        k++;
    }
    g->sel = 0;
    while (!g->op[g->sel].present) g->sel++;
    g->cx = g->op[g->sel].x; g->cy = g->op[g->sel].y - 2.0f;
    if (ms->intel) for (int i = 0; i < g->nen; i++) { g->en[i].mark = 999; g->en[i].ghost = 1; g->en[i].gx = g->en[i].x; g->en[i].gy = g->en[i].y; g->en[i].gface = g->en[i].face; }
    g->tac = 0; g->result = RES_NONE; g->alarm = 0; g->alarm_t = 0; g->gos = 0; g->mtime = 0; g->downed_any = 0;
    g->last_ping = 0; g->area_clear = 0; g->kills = g->shots_fired = g->shots_hit = 0; g->all_ready = 0; g->intensity = 0;
    g->combat_t = 99; g->go_t = 99; g->radio_t = 0; g->cam_mode = 0;
    update_vis(g);
    g->sv.last = (uint8_t)m;
    set_scene(g, SC_PLAN);
    if (!g->sv.helped) { g->help_page = 1; g->sv.helped = 1; save_now(g); }
}

/* ------------------------------------------------------------------ */
/* planning                                                             */
/* ------------------------------------------------------------------ */
/* where the operator's plan currently ends (tile) */
static void plan_end(const Game *g, int i, int *tx, int *ty)
{
    const Op *o = &g->op[i];
    if (o->nwp > 0 && o->nwp > o->cur) { *tx = o->wp[o->nwp - 1].tx; *ty = o->wp[o->nwp - 1].ty; }
    else { *tx = (int)o->x; *ty = (int)o->y; }
}

static int first_editable(const Game *g, const Op *o)
{
    if (g->scene == SC_PLAN) return 0;
    return o->cur < 0 ? 0 : o->cur;
}

static void refresh_planbr(Game *g)
{
    for (int y = 0; y < g->H; y++) for (int x = 0; x < g->W; x++) g->tile[y][x].flags &= (uint8_t)~TF_PLANBR;
    for (int i = 0; i < NOPS; i++) {
        const Op *o = &g->op[i];
        for (int k = 0; k < o->nwp; k++)
            if (o->wp[k].ab == AB_BREACH) g->tile[(int)o->wp[k].ay][(int)o->wp[k].ax].flags |= TF_PLANBR;
    }
    for (int i = 0; i < MAX_CHARGE; i++) if (g->ch[i].live) g->tile[g->ch[i].ty][g->ch[i].tx].flags |= TF_PLANBR;
}

static void plan_error(Game *g, const char *msg)
{
    sfx(g, SFX_ERROR, 1.0f, -1, 0.8f);
    strcpy(g->radio, msg);
    g->radio_op = -2;
    g->radio_t = 2.2f;
}

static void plan_add(Game *g)
{
    Op *o = &g->op[g->sel];
    int tx = (int)g->cx, ty = (int)g->cy;
    if (o->state == OS_DOWN) { plan_error(g, "OPERATOR IS DOWN"); return; }
    if (o->nwp >= MAX_WP) { plan_error(g, "TOO MANY WAYPOINTS"); return; }
    if (!plan_passable(g, tx, ty)) { plan_error(g, "CAN'T GO THERE"); return; }
    int sx, sy;
    plan_end(g, g->sel, &sx, &sy);
    if (sx == tx && sy == ty) return;
    static uint8_t tmp[MAX_PATH][2];
    if (find_path(g, sx, sy, tx, ty, 1, tmp, MAX_PATH) < 0) { plan_error(g, "NO ROUTE - BREACH A WAY IN?"); return; }
    if (o->cur >= o->nwp && g->scene == SC_EXEC) { o->nwp = 0; o->cur = 0; }
    Waypoint *w = &o->wp[o->nwp++];
    memset(w, 0, sizeof *w);
    w->tx = (int8_t)tx; w->ty = (int8_t)ty;
    sfx(g, SFX_WAYPOINT, 1.0f + 0.04f * (float)o->nwp, -1, 0.7f);
    if (g->scene == SC_EXEC && !o->manual && (o->state == OS_IDLE || o->state == OS_BLOCKED)) plan_reroute(g, g->sel);
}

static void plan_undo(Game *g)
{
    Op *o = &g->op[g->sel];
    if (o->nwp <= first_editable(g, o)) { sfx(g, SFX_ERROR, 1.0f, -1, 0.5f); return; }
    Waypoint *w = &o->wp[o->nwp - 1];
    if (g->scene == SC_EXEC && o->nwp - 1 == o->cur && (o->state == OS_HOLD || o->state == OS_ACTION || o->state == OS_DELAY)) {
        sfx(g, SFX_ERROR, 1.0f, -1, 0.5f); return;
    }
    (void)w;
    o->nwp--;
    if (g->scene == SC_EXEC && o->cur >= o->nwp && o->state == OS_MOVE) { o->state = OS_IDLE; o->plen = 0; }
    refresh_planbr(g);
    sfx(g, SFX_UNDO, 1.0f, -1, 0.7f);
}

static int last_wp_editable(Game *g, Op *o)
{
    if (o->nwp <= first_editable(g, o)) { plan_error(g, "PLACE A WAYPOINT FIRST (A)"); return 0; }
    if (g->scene == SC_EXEC && o->nwp - 1 == o->cur && o->state != OS_MOVE && o->state != OS_BLOCKED) {
        plan_error(g, "ALREADY THERE"); return 0;
    }
    return 1;
}

static void plan_hold(Game *g)
{
    Op *o = &g->op[g->sel];
    if (!last_wp_editable(g, o)) return;
    Waypoint *w = &o->wp[o->nwp - 1];
    w->hold = !w->hold;
    sfx(g, w->hold ? SFX_HOLD : SFX_UNDO, 1.0f, -1, 0.8f);
}

static void drone_scan(Game *g, float x, float y)
{
    Op *p = &g->op[OP_PIKE];
    int r = room_at(g, x, y);
    if (r == 0 || r >= MAX_ROOMS || !in_map(g, (int)x, (int)y) || g->tile[(int)y][(int)x].kind != K_FLOOR) { plan_error(g, "POINT THE DRONE AT A ROOM"); return; }
    if (p->charges <= 0) { plan_error(g, "NO DRONES LEFT"); return; }
    p->charges--;
    for (int ty = 0; ty < g->H; ty++)
        for (int tx = 0; tx < g->W; tx++) {
            Tile *t = &g->tile[ty][tx];
            if (t->room == r) t->flags |= TF_SEEN;
        }
    for (int ty = 0; ty < g->H; ty++)                  /* the room's walls and doors too */
        for (int tx = 0; tx < g->W; tx++) {
            Tile *t = &g->tile[ty][tx];
            if (t->kind == K_FLOOR) continue;
            for (int d = 0; d < 4; d++) {
                static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
                int nx = tx + DX[d], ny = ty + DY[d];
                if (in_map(g, nx, ny) && g->tile[ny][nx].room == r && g->tile[ny][nx].kind == K_FLOOR) t->flags |= TF_SEEN;
            }
        }
    int found = 0;
    for (int i = 0; i < g->nen; i++) {
        Enemy *e = &g->en[i];
        if (!e->alive || room_at(g, e->x, e->y) != r) continue;
        e->mark = 999; e->ghost = 1; e->gx = e->x; e->gy = e->y; e->gface = e->face;
        found++;
    }
    g->room_seen[r] = 1;
    sfx(g, SFX_DRONE, 1.0f, x, 0.8f);
    sfx(g, SFX_SCAN, 1.0f, x, 0.8f);
    char buf[40];
    if (found == 0) strcpy(buf, "DRONE: ROOM EMPTY");
    else { strcpy(buf, "DRONE: "); buf[7] = (char)('0' + (found > 9 ? 9 : found)); buf[8] = 0; strcat(buf, found == 1 ? " HOSTILE" : " HOSTILES"); }
    radio(g, OP_PIKE, buf + 7, SFX_V_SCAN);
    strcpy(g->radio, "PIKE: "); strcat(g->radio, buf + 7);
}

static void plan_ability(Game *g)
{
    Op *o = &g->op[g->sel];
    int ab = ability_of(g->sel);
    if (ab == AB_DRONE) { drone_scan(g, g->cx, g->cy); return; }
    if (!last_wp_editable(g, o)) return;
    Waypoint *w = &o->wp[o->nwp - 1];
    if (ab == AB_BREACH) {
        if (w->ab == AB_BREACH) { w->ab = AB_NONE; refresh_planbr(g); sfx(g, SFX_UNDO, 1.0f, -1, 0.7f); return; }
        int best = -1, bx = 0, by = 0; float bd = 1e9f;
        static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
        for (int d = 0; d < 8; d++) {
            int x = w->tx + DX[d], y = w->ty + DY[d];
            if (!breachable(g, x, y)) continue;
            float dx = (float)x + 0.5f - g->cx, dy = (float)y + 0.5f - g->cy, dd = dx * dx + dy * dy + (d >= 4 ? 0.5f : 0);
            if (dd < bd) { bd = dd; best = d; bx = x; by = y; }
        }
        if (best < 0) { plan_error(g, "KNOX MUST STAND NEXT TO A DOOR OR CRACKED WALL"); return; }
        int used = 0;
        for (int k = 0; k < o->nwp - 1; k++) if (o->wp[k].ab == AB_BREACH) used++;
        if (used >= o->charges) { plan_error(g, "NO CHARGES LEFT"); return; }
        w->ab = AB_BREACH; w->ax = (float)bx; w->ay = (float)by;
        refresh_planbr(g);
        sfx(g, SFX_PLANT, 1.0f, -1, 0.7f);
        return;
    }
    /* flash / smoke: thrown at the cursor */
    float tx = (float)(int)g->cx, ty = (float)(int)g->cy;
    if (w->ab == ab && (int)w->ax == (int)tx && (int)w->ay == (int)ty) { w->ab = AB_NONE; sfx(g, SFX_UNDO, 1.0f, -1, 0.7f); return; }
    float dx = tx - (float)w->tx, dy = ty - (float)w->ty;
    if (dx * dx + dy * dy > 8.0f * 8.0f) { plan_error(g, "TOO FAR TO THROW"); return; }
    if (dx == 0 && dy == 0) { plan_error(g, "MOVE THE CURSOR TO WHERE IT SHOULD LAND"); return; }
    int used = 0;
    for (int k = 0; k < o->nwp - 1; k++) if (o->wp[k].ab == ab) used++;
    if (used >= o->charges) { plan_error(g, ab == AB_FLASH ? "NO FLASHBANGS LEFT" : "NO SMOKE LEFT"); return; }
    w->ab = (uint8_t)ab; w->ax = tx; w->ay = ty;
    sfx(g, SFX_HOLD, 1.3f, -1, 0.7f);
}

static void select_op(Game *g, int dir)
{
    for (int k = 0; k < NOPS; k++) {
        g->sel = (g->sel + dir + NOPS) % NOPS;
        if (g->op[g->sel].present) break;
    }
    sfx(g, SFX_SELECT, 1.0f, -1, 0.7f);
    if (g->scene == SC_PLAN || g->tac) {
        int tx, ty;
        plan_end(g, g->sel, &tx, &ty);
        g->cx = (float)tx + 0.5f; g->cy = (float)ty + 0.5f;
    }
}

static void cursor_update(Game *g, const Input *in, float dt)
{
    float sp = 9.0f;
    float ox = g->cx, oy = g->cy;
    g->cx += in->sx * sp * dt; g->cy += in->sy * sp * dt;
    uint32_t d = in->held & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT);
    int step = 0;
    if (in->pressed & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)) { step = 1; g->rep_t = 0.28f; }
    else if (d) { g->rep_t -= dt; if (g->rep_t <= 0) { step = 1; g->rep_t = 0.07f; } }
    if (step) {
        g->cx = floorf(g->cx) + 0.5f; g->cy = floorf(g->cy) + 0.5f;
        if (d & BTN_LEFT) g->cx -= 1; if (d & BTN_RIGHT) g->cx += 1;
        if (d & BTN_UP) g->cy -= 1; if (d & BTN_DOWN) g->cy += 1;
    }
    if (g->cx < 0.5f) g->cx = 0.5f; if (g->cy < 0.5f) g->cy = 0.5f;
    if (g->cx > (float)g->W - 0.5f) g->cx = (float)g->W - 0.5f;
    if (g->cy > (float)g->H - 0.5f) g->cy = (float)g->H - 0.5f;
    if ((int)ox != (int)g->cx || (int)oy != (int)g->cy) sfx(g, SFX_MOVE, 1.0f, -1, 0.25f);
}

static void planning_input(Game *g, const Input *in, float dt)
{
    cursor_update(g, in, dt);
    if (in->pressed & BTN_ZL) select_op(g, -1);
    if (in->pressed & BTN_ZR) select_op(g, 1);
    if (in->pressed & BTN_A) plan_add(g);
    if (in->pressed & BTN_B) plan_undo(g);
    if (in->pressed & BTN_Y) plan_hold(g);
    if (in->pressed & BTN_X) plan_ability(g);
}

/* ------------------------------------------------------------------ */
/* the assault: direct control                                          */
/* ------------------------------------------------------------------ */
static void use_now(Game *g)
{
    Op *o = &g->op[g->sel];
    if (o->state == OS_DOWN) return;
    int ab = ability_of(g->sel);
    if (ab == AB_DRONE) {
        if (o->charges <= 0) { plan_error(g, "NO DRONES LEFT"); return; }
        o->charges--;
        int n = 0;
        for (int i = 0; i < g->nen; i++) {
            Enemy *e = &g->en[i];
            float dx = e->x - o->x, dy = e->y - o->y;
            if (e->alive && dx * dx + dy * dy < 10.0f * 10.0f) { e->mark = 8.0f; e->ghost = 1; e->gx = e->x; e->gy = e->y; n++; }
        }
        sfx(g, SFX_SCAN, 1.0f, o->x, 0.9f);
        radio(g, OP_PIKE, n ? "SCAN - HOSTILES MARKED" : "SCAN - ALL CLEAR", SFX_V_SCAN);
        g->dl[0].life = 0;
        add_light(g, o->x * TS, o->y * TS, 3.0f * TS, 0.2f, 1.2f, 0.6f, 0.5f);
        return;
    }
    if (o->charges <= 0) { plan_error(g, "NOTHING LEFT"); return; }
    if (ab == AB_BREACH) {
        int bx = -1, by = -1; float bd = 9;
        for (int y = (int)o->y - 1; y <= (int)o->y + 1; y++)
            for (int x = (int)o->x - 1; x <= (int)o->x + 1; x++) {
                if (!breachable(g, x, y)) continue;
                float dx = (float)x + 0.5f - o->x, dy = (float)y + 0.5f - o->y, d = dx * dx + dy * dy;
                if (d < bd) { bd = d; bx = x; by = y; }
            }
        if (bx < 0) { plan_error(g, "NOTHING TO BREACH HERE"); return; }
        for (int i = 0; i < MAX_CHARGE; i++)
            if (!g->ch[i].live) {
                g->ch[i].live = 1; g->ch[i].tx = bx; g->ch[i].ty = by; g->ch[i].owner = g->sel; g->ch[i].t = 1.6f;
                o->charges--;
                sfx(g, SFX_PLANT, 1.0f, o->x, 0.8f);
                radio(g, g->sel, "SETTING CHARGE", SFX_SQUELCH);
                break;
            }
        return;
    }
    /* throw where the operator is facing */
    float tx = o->x + cosf(o->face) * 5.0f, ty = o->y + sinf(o->face) * 5.0f;
    Waypoint save;
    int had = o->nwp > 0;
    (void)had;
    /* reuse the waypoint machinery: a temporary waypoint at the operator */
    if (o->nwp < MAX_WP) {
        int k = o->nwp;
        save = o->wp[k];
        o->wp[k].tx = (int8_t)o->x; o->wp[k].ty = (int8_t)o->y; o->wp[k].hold = 0;
        o->wp[k].ab = (uint8_t)ab; o->wp[k].ax = tx - 0.5f; o->wp[k].ay = ty - 0.5f;
        int cur = o->cur;
        o->cur = k;
        sim_use_ability(g, g->sel);
        o->cur = cur;
        o->wp[k] = save;
    }
}

static void exec_input(Game *g, const Input *in, float dt)
{
    (void)dt;
    Op *o = &g->op[g->sel];
    if (in->pressed & BTN_X) go_code(g);
    if (in->pressed & BTN_ZL) select_op(g, -1);
    if (in->pressed & BTN_ZR) select_op(g, 1);
    if (in->pressed & BTN_L) { g->cam_mode = !g->cam_mode; sfx(g, SFX_SELECT, 0.8f, -1, 0.5f); }
    if (o->state != OS_DOWN && o->present) {
        float m = sqrtf(in->sx * in->sx + in->sy * in->sy);
        if (m > 0.4f && !o->manual) {
            o->manual = 1; o->plen = 0;
            if (o->state != OS_REVIVE) o->state = OS_IDLE;
            g->cam_mode = 1;
            sfx(g, SFX_SELECT, 1.2f, -1, 0.5f);
        }
        if ((in->pressed & BTN_A) && o->manual) {
            o->manual = 0;
            if (o->cur < 0) o->cur = 0;
            if (o->cur < o->nwp) plan_reroute(g, g->sel);
            else o->state = OS_IDLE;
            radio(g, g->sel, "BACK ON THE PLAN", SFX_SQUELCH);
        }
        if (in->pressed & BTN_Y) use_now(g);
    }
}

/* ------------------------------------------------------------------ */
/* camera                                                               */
/* ------------------------------------------------------------------ */
static void camera_update(Game *g, float dt)
{
    float W = (float)g->W * TS, H = (float)g->H * TS;
    float fit = fminf((VW - 12.0f) / W, (VH - 88.0f) / H);
    float tz = fit, tx = W * 0.5f, ty = H * 0.5f + 17.0f / fit;      /* sit the map between the top bar and the squad cards */
    if (g->scene == SC_EXEC && !g->tac) {
        const Op *s = &g->op[g->sel];
        if (g->cam_mode == 1 && s->present) {
            tz = fmaxf(fit, 1.75f); tx = s->x * TS; ty = s->y * TS;
        } else {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (int i = 0; i < NOPS; i++) {
                const Op *o = &g->op[i];
                if (!o->present) continue;
                float px = o->x, py = o->y;
                x0 = fminf(x0, px); y0 = fminf(y0, py); x1 = fmaxf(x1, px); y1 = fmaxf(y1, py);
                if (o->target >= 0) { const Enemy *e = &g->en[o->target]; x0 = fminf(x0, e->x); y0 = fminf(y0, e->y); x1 = fmaxf(x1, e->x); y1 = fmaxf(y1, e->y); }
            }
            float bw = (x1 - x0 + 9.0f) * TS, bh = (y1 - y0 + 7.0f) * TS;
            tz = fminf((VW - 12.0f) / bw, (VH - 78.0f) / bh);
            if (tz < fit) tz = fit;
            if (tz > 1.7f) tz = 1.7f;
            tx = (x0 + x1) * 0.5f * TS; ty = (y0 + y1) * 0.5f * TS;
        }
        /* don't show much beyond the map */
        float hw = VW * 0.5f / tz, hh = (VH - 70.0f) * 0.5f / tz;
        if (W > hw * 2) { if (tx < hw) tx = hw; if (tx > W - hw) tx = W - hw; } else tx = W * 0.5f;
        if (H > hh * 2) { if (ty < hh - 30.0f / tz) ty = hh - 30.0f / tz; if (ty > H - hh + 40.0f / tz) ty = H - hh + 40.0f / tz; } else ty = H * 0.5f + 6.0f / tz;
    } else if ((g->scene == SC_PLAN || g->tac) && g->cam_mode) {
        tz = fmaxf(fit, 1.7f); tx = g->cx * TS; ty = g->cy * TS;
    } else if (g->scene == SC_TITLE || g->scene == SC_SELECT || g->scene == SC_BRIEF) {
        tz = fit * 1.25f;
        tx = W * 0.5f + sinf(g->t * 0.05f) * W * 0.12f;
        ty = H * 0.5f + cosf(g->t * 0.04f) * H * 0.08f;
    }
    float k = 1.0f - expf(-dt * 4.0f);
    g->zoom += (tz - g->zoom) * k;
    g->camx += (tx - g->camx) * k;
    g->camy += (ty - g->camy) * k;
    if (g->shake > 0) { g->shake_x = rnds(g) * g->shake; g->shake_y = rnds(g) * g->shake; }
    else g->shake_x = g->shake_y = 0;
}

/* ------------------------------------------------------------------ */
/* music                                                                */
/* ------------------------------------------------------------------ */
static void music_update(Game *g, float dt)
{
    float *t = g->mus_target;
    for (int i = 0; i < MUS_TRACKS; i++) t[i] = 0;
    switch (g->scene) {
    case SC_TITLE: case SC_SELECT: case SC_BRIEF:
        t[MUS_MENU] = 1.0f; break;
    case SC_PLAN:
        t[MUS_BED] = 0.8f; break;
    case SC_EXEC: {
        int moving = 0;
        for (int i = 0; i < NOPS; i++) if (g->op[i].present && (g->op[i].state == OS_MOVE || g->op[i].manual)) moving = 1;
        float hot = g->combat_t < 5.0f ? 1.0f : (g->combat_t < 11.0f ? 1.0f - (g->combat_t - 5.0f) / 6.0f : 0.0f);
        if (g->go_t < 7.0f) hot = fmaxf(hot, 1.0f - fmaxf(0, g->go_t - 4.0f) / 3.0f);
        if (g->alarm && hot < 0.45f) hot = 0.45f;
        g->intensity = hot;
        t[MUS_BED] = 1.0f;
        t[MUS_PRESS] = g->all_ready ? 1.0f : (moving ? 0.6f : 0.35f);
        if (hot > 0.3f) t[MUS_PRESS] = fmaxf(t[MUS_PRESS], 0.8f);
        t[MUS_ASSAULT] = hot;
        if (g->tac) { t[MUS_ASSAULT] *= 0.35f; t[MUS_PRESS] *= 0.6f; }
        if (g->result != RES_NONE) { t[MUS_ASSAULT] = 0; t[MUS_PRESS] = 0; t[MUS_BED] = 0.4f; }
        break;
    }
    case SC_DEBRIEF:
        t[MUS_MENU] = g->scene_t > 2.5f ? 0.8f : 0.0f; t[MUS_BED] = g->scene_t < 2.5f ? 0.3f : 0.0f; break;
    }
    for (int i = 0; i < MUS_TRACKS; i++) {
        float up = (i == MUS_ASSAULT) ? 6.0f : 1.2f, down = (i == MUS_ASSAULT) ? 0.35f : 0.6f;
        if (g->scene != SC_EXEC) down = 1.2f;
        float d = t[i] - g->mus[i];
        float r = (d > 0 ? up : down) * dt;
        if (d > r) d = r; else if (d < -r) d = -r;
        g->mus[i] += d;
    }
}

void game_music(const Game *g, float vol[])
{
    for (int i = 0; i < MUS_TRACKS; i++) vol[i] = g->sv.music ? g->mus[i] : 0.0f;
}

/* ------------------------------------------------------------------ */
/* weather                                                              */
/* ------------------------------------------------------------------ */
static void weather_update(Game *g, float dt)
{
    if (g->lightning > 0) g->lightning -= dt * 3.0f;
    if (g->lightning < 0) g->lightning = 0;
    if (g->th->lightning) {
        g->next_lightning -= dt;
        if (g->next_lightning <= 0) {
            g->lightning = 1.0f;
            g->next_lightning = 9.0f + rnd(g) * 14.0f;
            g->thunder_t = 0.4f + rnd(g) * 1.6f;
        }
        if (g->thunder_t > 0) {
            g->thunder_t -= dt;
            if (g->lightning < 0.5f && g->lightning > 0.42f) g->lightning = 0.85f;      /* the double flicker */
            if (g->thunder_t <= 0) sfx(g, SFX_THUNDER, 0.9f + rnd(g) * 0.2f, -1, 0.9f);
        }
    }
    if (g->th->rain && (g->scene == SC_PLAN || g->scene == SC_EXEC || g->scene == SC_TITLE || g->scene == SC_SELECT || g->scene == SC_BRIEF)) {
        int n = (int)(dt * 90.0f) + (rnd(g) < 0.5f ? 1 : 0);
        for (int i = 0; i < n; i++) {
            float x = rnd(g) * (float)g->W, y = rnd(g) * (float)g->H;
            int tx = (int)x, ty = (int)y;
            if (in_map(g, tx, ty) && (g->tile[ty][tx].flags & TF_OUT) && g->tile[ty][tx].furn != F_CAR)
                spawn(g, PT_SPLASH, x, y, 0, 0, 0, 0, 0.3f, 1.0f, 0xA0B8D0FFu);
        }
    }
}

/* ------------------------------------------------------------------ */
/* the frame                                                            */
/* ------------------------------------------------------------------ */
static int menu_nav(Game *g, const Input *in, int *sel, int n)
{
    int moved = 0;
    if (in->pressed & BTN_UP) { *sel = (*sel + n - 1) % n; moved = 1; }
    if (in->pressed & BTN_DOWN) { *sel = (*sel + 1) % n; moved = 1; }
    if (in->sy < -0.6f || in->sy > 0.6f) {
        g->rep_t -= 1.0f / 60.0f;
        if (g->rep_t <= 0) { *sel = (*sel + (in->sy > 0 ? 1 : n - 1)) % n; moved = 1; g->rep_t = 0.22f; }
    } else if (!(in->held & (BTN_UP | BTN_DOWN))) g->rep_t = 0;
    if (moved) sfx(g, SFX_MOVE, 1.0f, -1, 0.5f);
    return moved;
}

static void retry_with_plan(Game *g);

static void pause_input(Game *g, const Input *in)
{
    int n = g->scene == SC_PLAN ? 5 : 5;
    menu_nav(g, in, &g->pause_sel, n);
    if (in->pressed & (BTN_B | BTN_PLUS)) { g->pause = 0; sfx(g, SFX_PAUSE, 0.8f, -1, 0.6f); return; }
    if (!(in->pressed & BTN_A)) return;
    sfx(g, SFX_CONFIRM, 1.0f, -1, 0.7f);
    if (g->scene == SC_PLAN) {
        switch (g->pause_sel) {
        case 0: {   /* start */
            int any = 0;
            for (int i = 0; i < NOPS; i++) if (g->op[i].present && g->op[i].nwp > 0) any = 1;
            if (!any) { g->pause = 0; plan_error(g, "GIVE YOUR OPERATORS A PLAN FIRST"); return; }
            g->pause = 0;
            for (int i = 0; i < NOPS; i++) { memcpy(g->last_wp[i], g->op[i].wp, sizeof g->op[i].wp); g->last_nwp[i] = g->op[i].nwp; }
            g->last_plan_mission = g->mission + 1;
            set_scene(g, SC_EXEC);
            sim_start(g);
            return;
        }
        case 1: { Op *o = &g->op[g->sel]; o->nwp = 0; refresh_planbr(g); g->pause = 0; return; }
        case 2: for (int i = 0; i < NOPS; i++) g->op[i].nwp = 0; refresh_planbr(g); g->pause = 0; return;
        case 3: g->help_page = 1; g->pause = 0; return;
        case 4: set_scene(g, SC_SELECT); return;
        }
    } else {
        switch (g->pause_sel) {
        case 0: g->pause = 0; return;
        case 1: retry_with_plan(g); return;
        case 2: g->sv.music = !g->sv.music; save_now(g); return;
        case 3: g->sv.sfx = !g->sv.sfx; save_now(g); return;
        case 4: set_scene(g, SC_SELECT); return;
        }
    }
}

/* restart the mission with the plan used last time */
static void retry_with_plan(Game *g)
{
    start_mission(g, g->mission);
    if (g->last_plan_mission != g->mission + 1) return;
    for (int i = 0; i < NOPS; i++) {
        if (!g->op[i].present) continue;
        memcpy(g->op[i].wp, g->last_wp[i], sizeof g->op[i].wp);
        g->op[i].nwp = g->last_nwp[i];
    }
    refresh_planbr(g);
    g->help_page = 0;
}

static void finish_mission(Game *g)
{
    if (g->result == RES_WIN) {
        int st = 1 + (g->downed_any ? 0 : 1) + (g->mtime <= MISSIONS[g->mission].par ? 1 : 0);
        g->stars = st;
        int m = g->mission;
        if (st > g->sv.stars[m]) g->sv.stars[m] = (uint8_t)st;
        int secs = (int)g->mtime + 1;
        if (g->sv.best[m] == 0 || secs < g->sv.best[m]) g->sv.best[m] = (uint16_t)secs;
        if (g->sv.unlocked < m + 2 && m + 2 <= NMISSIONS) g->sv.unlocked = (uint8_t)(m + 2);
        if (m + 1 < NMISSIONS) g->sv.last = (uint8_t)(m + 1);
        save_now(g);
        sfx(g, SFX_WIN, 1.0f, -1, 1.0f);
    } else {
        g->stars = 0;
        sfx(g, SFX_LOSE, 1.0f, -1, 1.0f);
    }
    set_scene(g, SC_DEBRIEF);
    g->menu_sel = 0;
}

void game_update(Game *g, const Input *in, float dt)
{
    g->t += dt;
    g->scene_t += dt;
    if (g->radio_t > 0) g->radio_t -= dt;
    if (g->saved_t > 0) g->saved_t -= dt;

    if (g->help_page > 0) {
        if (in->pressed & (BTN_A | BTN_RIGHT)) { g->help_page++; sfx(g, SFX_SELECT, 1.0f, -1, 0.6f); if (g->help_page > 3) g->help_page = 0; }
        else if (in->pressed & (BTN_LEFT)) { if (g->help_page > 1) g->help_page--; }
        else if (in->pressed & (BTN_B | BTN_PLUS)) g->help_page = 0;
        camera_update(g, dt);
        music_update(g, dt);
        return;
    }

    switch (g->scene) {
    case SC_TITLE: {
        menu_nav(g, in, &g->menu_sel, 5);
        if (in->pressed & BTN_A) {
            sfx(g, SFX_CONFIRM, 1.0f, -1, 0.8f);
            switch (g->menu_sel) {
            case 0: g->sel_mission = g->sv.last < g->sv.unlocked ? g->sv.last : g->sv.unlocked - 1; set_scene(g, SC_BRIEF); break;
            case 1: g->sel_mission = g->sv.last < g->sv.unlocked ? g->sv.last : 0; set_scene(g, SC_SELECT); break;
            case 2: g->help_page = 1; break;
            case 3: g->sv.music = !g->sv.music; save_now(g); break;
            case 4: g->sv.sfx = !g->sv.sfx; save_now(g); break;
            }
        }
        sim_fx(g, dt);
        break;
    }
    case SC_SELECT: {
        int prev = g->sel_mission;
        menu_nav(g, in, &g->sel_mission, NMISSIONS);
        if (g->sel_mission != prev && g->sel_mission >= g->sv.unlocked) { /* locked ones can be looked at */ }
        if (in->pressed & BTN_A) {
            if (g->sel_mission < g->sv.unlocked) { sfx(g, SFX_CONFIRM, 1.0f, -1, 0.8f); set_scene(g, SC_BRIEF); }
            else sfx(g, SFX_ERROR, 1.0f, -1, 0.7f);
        }
        if (in->pressed & BTN_B) { sfx(g, SFX_UNDO, 1.0f, -1, 0.6f); set_scene(g, SC_TITLE); }
        sim_fx(g, dt);
        break;
    }
    case SC_BRIEF:
        if (g->scene_t < 0.05f && g->mission != g->sel_mission) level_load(g, g->sel_mission);
        if (in->pressed & BTN_A) { sfx(g, SFX_GO, 1.2f, -1, 0.6f); start_mission(g, g->sel_mission); }
        if (in->pressed & BTN_B) { sfx(g, SFX_UNDO, 1.0f, -1, 0.6f); set_scene(g, SC_SELECT); }
        sim_fx(g, dt);
        break;
    case SC_PLAN:
        if (g->pause) { pause_input(g, in); break; }
        if (in->pressed & BTN_PLUS) { g->pause = 1; g->pause_sel = 0; sfx(g, SFX_PAUSE, 1.0f, -1, 0.6f); break; }
        planning_input(g, in, dt);
        sim_fx(g, dt);
        break;
    case SC_EXEC:
        if (g->pause) { pause_input(g, in); break; }
        if (in->pressed & BTN_PLUS) { g->pause = 1; g->pause_sel = 0; sfx(g, SFX_PAUSE, 1.0f, -1, 0.6f); break; }
        if (g->result == RES_NONE && (in->pressed & BTN_R)) {
            g->tac = !g->tac;
            sfx(g, SFX_PAUSE, g->tac ? 1.0f : 1.3f, -1, 0.7f);
            if (g->tac) { int tx, ty; plan_end(g, g->sel, &tx, &ty); g->cx = (float)tx + 0.5f; g->cy = (float)ty + 0.5f; }
        }
        if (g->tac) {
            planning_input(g, in, dt);
            if (in->pressed & BTN_PLUS) g->tac = 0;
            break;
        }
        exec_input(g, in, dt);
        /* charges set by hand blow after a short fuse */
        for (int i = 0; i < MAX_CHARGE; i++)
            if (g->ch[i].live && g->ch[i].t > 0) {
                g->ch[i].t -= dt;
                if (g->ch[i].t <= 0) { radio(g, g->ch[i].owner, "BREACHING!", SFX_V_BREACH); sim_detonate(g, i); }
            }
        sim_update(g, in, dt);
        if (g->result != RES_NONE && g->end_t > 2.6f) finish_mission(g);
        break;
    case SC_DEBRIEF:
        sim_fx(g, dt);
        if (g->scene_t < 0.8f) break;
        menu_nav(g, in, &g->menu_sel, 3);
        if (in->pressed & BTN_A) {
            sfx(g, SFX_CONFIRM, 1.0f, -1, 0.8f);
            int won = g->result == RES_WIN;
            if (g->menu_sel == 0) {
                if (won && g->mission + 1 < NMISSIONS) { g->sel_mission = g->mission + 1; set_scene(g, SC_BRIEF); }
                else if (won) start_mission(g, g->mission);
                else retry_with_plan(g);
            } else if (g->menu_sel == 1) start_mission(g, g->mission);
            else { g->sel_mission = g->mission; set_scene(g, SC_SELECT); }
        }
        break;
    }
    if (g->scene == SC_PLAN && !g->pause && (in->pressed & BTN_L)) { g->cam_mode = !g->cam_mode; }
    weather_update(g, dt);
    camera_update(g, dt);
    music_update(g, dt);
    lights_frame(g);
}

void game_render(const Game *g)
{
    gfx_begin();
    draw_world(g);
    draw_hud(g);
    draw_menus(g);
}
