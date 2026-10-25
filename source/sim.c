/*
 * GO CODE - the assault: operators following their plans, hostiles, hostages, gunfire, gadgets
 * and all the little effects (tracers, sparks, smoke, debris, lights).
 */
#include "game_int.h"

#include <math.h>
#include <string.h>
#include "fastmath.h"

/* ------------------------------------------------------------------ */
/* tables                                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    float range, interval; int burst; float pause, dmg; int pellets; float spread, acc, noise; int sfx;
} Weapon;

static const Weapon WEAP[NWEAP] = {
    /* rifle      */ { 14.0f, 0.11f, 3, 0.36f, 30, 1, 0.035f, 0.88f, 11.0f, SFX_RIFLE },
    /* shotgun    */ {  7.0f, 0.80f, 1, 0.00f, 17, 7, 0.17f,  0.95f, 11.0f, SFX_SHOTGUN },
    /* smg (sil.) */ { 11.0f, 0.08f, 4, 0.36f, 21, 1, 0.050f, 0.82f,  2.2f, SFX_SMG },
    /* pistol     */ { 11.0f, 0.26f, 1, 0.00f, 34, 1, 0.035f, 0.86f, 10.0f, SFX_PISTOL },
    /* e pistol   */ { 11.0f, 0.42f, 1, 0.00f, 13, 1, 0.07f,  0.52f, 10.0f, SFX_E_PISTOL },
    /* e rifle    */ { 14.0f, 0.12f, 3, 0.70f, 12, 1, 0.07f,  0.46f, 12.0f, SFX_E_RIFLE },
    /* e shotgun  */ {  6.5f, 1.05f, 1, 0.00f,  9, 6, 0.20f,  0.80f, 11.0f, SFX_E_SHOTGUN },
};

static const int   OP_WEAP[NOPS]  = { WP_RIFLE, WP_SHOTGUN, WP_SMG, WP_PISTOL };
static const int   OP_AB[NOPS]    = { AB_FLASH, AB_BREACH, AB_DRONE, AB_SMOKE };
static const float OP_SPEED[NOPS] = { 2.45f, 2.25f, 2.6f, 2.45f };
static const float OP_REACT[NOPS] = { 0.26f, 0.30f, 0.22f, 0.30f };
static const int   OP_FEMALE[NOPS] = { 1, 0, 0, 1 };

static const int   EN_WEAP[NETYPE]  = { WP_E_PISTOL, WP_E_RIFLE, WP_E_SHOTGUN };
static const float EN_HP[NETYPE]    = { 70, 100, 190 };
static const float EN_REACT[NETYPE] = { 0.95f, 0.8f, 0.9f };
static const float EN_SPEED[NETYPE] = { 2.1f, 1.8f, 1.4f };

const char *op_name(int op) { static const char *N[NOPS] = { "VEGA", "KNOX", "PIKE", "MAE" }; return N[op]; }
const char *op_role(int op) { static const char *R[NOPS] = { "POINT", "BREACHER", "RECON", "MEDIC" }; return R[op]; }
uint32_t op_color(int op) { static const uint32_t C[NOPS] = { 0xFF6AD5FFu, 0xFFC93AFFu, 0x6CE67EFFu, 0x5AB4FFFFu }; return C[op]; }
int ability_of(int op) { return OP_AB[op]; }

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float angdiff(float a, float b)
{
    float d = b - a;
    while (d > PI_F) d -= TAU_F;
    while (d < -PI_F) d += TAU_F;
    return d;
}
static float turn_to(float a, float target, float rate)
{
    float d = angdiff(a, target);
    if (d > rate) d = rate; else if (d < -rate) d = -rate;
    a += d;
    if (a > PI_F) a -= TAU_F; else if (a < -PI_F) a += TAU_F;
    return a;
}
static float dist(float x0, float y0, float x1, float y1) { float dx = x1 - x0, dy = y1 - y0; return sqrtf(dx * dx + dy * dy); }

static int voice_for(int op, int male_id)
{
    /* the _M / _F pairs sit next to each other */
    return OP_FEMALE[op] ? male_id + 1 : male_id;
}

/* ------------------------------------------------------------------ */
/* effects                                                              */
/* ------------------------------------------------------------------ */
void spawn(Game *g, int type, float x, float y, float z, float vx, float vy, float vz, float life, float size, uint32_t c)
{
    Particle *p;
    if (g->npt < MAX_PT) p = &g->pt[g->npt++];
    else p = &g->pt[(int)(rnd(g) * (float)MAX_PT) % MAX_PT];
    p->type = (uint8_t)type; p->x = x; p->y = y; p->z = z; p->vx = vx; p->vy = vy; p->vz = vz;
    p->life = p->max = life; p->size = size; p->c = c; p->rot = rnd(g) * TAU_F; p->vr = rnds(g) * 8.0f;
}

void decal(Game *g, int type, float x, float y, float size, uint32_t c)
{
    Decal *d = &g->dc[g->dc_head];
    g->dc_head = (g->dc_head + 1) % MAX_DC;
    if (g->ndc < MAX_DC) g->ndc++;
    d->type = (uint8_t)type; d->x = x; d->y = y; d->size = size; d->c = c; d->rot = rnd(g) * TAU_F;
}

void add_light(Game *g, float x, float y, float rad, float r, float gg, float b, float life)
{
    int best = 0;
    for (int i = 0; i < MAX_DL; i++) {
        if (g->dl[i].life <= 0) { best = i; break; }
        if (g->dl[i].life < g->dl[best].life) best = i;
    }
    DLight *d = &g->dl[best];
    d->x = x; d->y = y; d->rad = rad; d->r = r; d->g = gg; d->b = b; d->life = d->max = life;
}

static void tracer(Game *g, float x0, float y0, float x1, float y1, int enemy)
{
    for (int i = 0; i < MAX_TR; i++)
        if (g->tr[i].life <= 0) {
            Tracer *t = &g->tr[i];
            t->x0 = x0; t->y0 = y0; t->x1 = x1; t->y1 = y1; t->life = 0.07f; t->enemy = enemy;
            return;
        }
}

static void kill_enemy(Game *g, int ei, int by);
static void hit_op(Game *g, int oi, float dmg);

static void shake(Game *g, float amt) { if (amt > g->shake) g->shake = amt; }

void noise_at(Game *g, float x, float y, float rad, int alarm)
{
    for (int i = 0; i < g->nen; i++) {
        Enemy *e = &g->en[i];
        if (!e->alive) continue;
        float d = dist(e->x, e->y, x, y);
        float r = los(g, e->x, e->y, x, y, 0) ? rad : rad * 0.6f;
        if (d > r) continue;
        if (e->state < ES_ALERT) {
            e->state = ES_ALERT;
            e->alerted_t = 0;
        }
        if (e->state != ES_COMBAT) { e->lx = x; e->ly = y; e->want_face = atan2f(y - e->y, x - e->x); }
    }
    if (alarm) g->alarm = 1;
}

/* ------------------------------------------------------------------ */
/* operators: plans                                                     */
/* ------------------------------------------------------------------ */
static void set_path_to_wp(Game *g, Op *o)
{
    const Waypoint *w = &o->wp[o->cur];
    int n = find_path(g, (int)o->x, (int)o->y, w->tx, w->ty, 1, o->path, MAX_PATH);
    if (n < 0) { o->plen = 0; o->pidx = 0; o->state = OS_BLOCKED; o->stuck = 0; return; }
    o->plen = n; o->pidx = 0; o->state = OS_MOVE;
}

static void next_wp(Game *g, Op *o)
{
    o->cur++;
    o->arrived_say = 0;
    if (o->cur >= o->nwp) { o->state = OS_IDLE; o->plen = 0; return; }
    set_path_to_wp(g, o);
}

void plan_reroute(Game *g, int i)
{
    Op *o = &g->op[i];
    if (o->state == OS_DOWN) return;
    if (o->cur < o->nwp && (o->state == OS_IDLE || o->state == OS_MOVE || o->state == OS_BLOCKED)) set_path_to_wp(g, o);
}

void sim_start(Game *g)
{
    for (int i = 0; i < g->nen; i++) g->en[i].hp = EN_HP[g->en[i].type];
    for (int i = 0; i < NOPS; i++) {
        Op *o = &g->op[i];
        if (!o->present) continue;
        o->cur = -1;
        o->target = -1;
        next_wp(g, o);
        if (o->state == OS_MOVE) o->act_t = 0.12f * (float)i;      /* a little stagger */
    }
    g->mtime = 0;
    g->alarm = 0;
    g->gos = 0;
    g->go_t = 99;
    g->combat_t = 99;
    g->result = RES_NONE;
    radio(g, -1, "ALL TEAMS, MOVE OUT", SFX_SQUELCH);
}

static int charge_at(const Game *g, int tx, int ty)
{
    for (int i = 0; i < MAX_CHARGE; i++)
        if (g->ch[i].live && g->ch[i].tx == tx && g->ch[i].ty == ty) return i;
    return -1;
}

static void plant_charge(Game *g, int op, int tx, int ty)
{
    if (charge_at(g, tx, ty) >= 0) return;
    for (int i = 0; i < MAX_CHARGE; i++)
        if (!g->ch[i].live) {
            g->ch[i].live = 1; g->ch[i].tx = tx; g->ch[i].ty = ty; g->ch[i].owner = op; g->ch[i].t = 0;
            sfx(g, SFX_PLANT, 1.0f, (float)tx, 0.8f);
            return;
        }
}

static void detonate(Game *g, int c)
{
    Charge *ch = &g->ch[c];
    if (!ch->live) return;
    ch->live = 0;
    int tx = ch->tx, ty = ch->ty;
    float cx = (float)tx + 0.5f, cy = (float)ty + 0.5f;
    const Op *pl = &g->op[ch->owner];
    float sx = cx - pl->x, sy = cy - pl->y, sl = sqrtf(sx * sx + sy * sy);
    if (sl < 0.01f) { sx = 0; sy = 1; sl = 1; }
    sx /= sl; sy /= sl;
    break_tile(g, tx, ty);
    sfx(g, SFX_BREACH, 1.0f, cx, 1.0f);
    shake(g, 9.0f);
    add_light(g, cx * TS, cy * TS, 6.5f * TS, 2.4f, 1.7f, 0.9f, 0.45f);
    for (int i = 0; i < 40; i++) {
        float a = atan2f(sy, sx) + rnds(g) * 1.2f, sp = 2.0f + rnd(g) * 7.0f;
        spawn(g, PT_DEBRIS, cx, cy, 0.3f, cosf(a) * sp, sinf(a) * sp, 2.0f + rnd(g) * 4.0f, 1.4f + rnd(g), 1.0f + rnd(g) * 2.0f,
              g->tile[ty][tx].mat == M_WOOD || g->tile[ty][tx].kind == K_RUBBLE ? 0x7A5634FFu : 0x8A8378FFu);
    }
    for (int i = 0; i < 18; i++) {
        float a = rnd(g) * TAU_F, sp = 0.5f + rnd(g) * 2.0f;
        spawn(g, PT_SMOKE, cx + rnds(g) * 0.4f, cy + rnds(g) * 0.4f, 0, cosf(a) * sp + sx, sinf(a) * sp + sy, 0, 2.5f + rnd(g) * 2.0f,
              10.0f + rnd(g) * 10.0f, 0x7A7470FFu);
    }
    for (int i = 0; i < 12; i++) spawn(g, PT_EMBER, cx, cy, 0.2f, sx * 4 + rnds(g) * 4, sy * 4 + rnds(g) * 4, 3.0f, 0.6f + rnd(g) * 0.5f, 1.5f, 0xFFB050FFu);
    decal(g, DC_SCORCH, cx + sx * 0.4f, cy + sy * 0.4f, 1.6f, 0x000000A0u);
    for (int i = 0; i < 6; i++) decal(g, DC_SPLINTER, cx + sx * (0.5f + rnd(g)) + rnds(g) * 0.7f, cy + sy * (0.5f + rnd(g)) + rnds(g) * 0.7f, 0.3f + rnd(g) * 0.3f, 0x5A4028FFu);
    /* the blast */
    for (int i = 0; i < g->nen; i++) {
        Enemy *e = &g->en[i];
        if (!e->alive) continue;
        float d = dist(e->x, e->y, cx, cy);
        if (d > 3.2f) continue;
        float side = (e->x - cx) * sx + (e->y - cy) * sy;
        if (side < -0.3f) continue;
        e->stun = fmaxf(e->stun, 2.6f * (1.0f - d / 4.0f) + 0.6f);
        e->hp -= 50.0f * (1.0f - d / 3.2f);
        e->hurt = 0.3f;
        if (e->state < ES_ALERT) e->state = ES_ALERT;
        if (e->hp <= 0) kill_enemy(g, i, ch->owner);
    }
    for (int i = 0; i < NOPS; i++) {
        Op *o = &g->op[i];
        if (!o->present || o->state == OS_DOWN) continue;
        float d = dist(o->x, o->y, cx, cy);
        float side = (o->x - cx) * sx + (o->y - cy) * sy;
        if (d < 1.6f && side > 0.2f) hit_op(g, i, 30);
    }
    /* windows nearby shatter */
    for (int y = ty - 2; y <= ty + 2; y++)
        for (int x = tx - 2; x <= tx + 2; x++)
            if (in_map(g, x, y) && g->tile[y][x].kind == K_WINDOW && !(g->tile[y][x].flags & TF_BROKEN)) {
                g->tile[y][x].flags |= TF_BROKEN;
                for (int k = 0; k < 6; k++) spawn(g, PT_GLASS, (float)x + 0.5f, (float)y + 0.5f, 0.6f, rnds(g) * 3, rnds(g) * 3, 2, 1.0f, 1.0f, 0xBFE8FFFFu);
            }
    noise_at(g, cx, cy, 16.0f, 1);
    g->combat_t = 0;
}

static void throw_nade(Game *g, int op, int type, float tx, float ty)
{
    Op *o = &g->op[op];
    /* stop short of walls */
    float x0 = o->x, y0 = o->y, dx = tx - x0, dy = ty - y0, l = sqrtf(dx * dx + dy * dy);
    if (l > 0.01f) {
        float a = atan2f(dy, dx), r = ray_len(g, x0, y0, a, l);
        if (r < l) { tx = x0 + cosf(a) * (r - 0.35f); ty = y0 + sinf(a) * (r - 0.35f); l = r - 0.35f; }
        o->face = a;
    }
    for (int i = 0; i < MAX_NADE; i++)
        if (g->nd[i].dur <= 0) {
            Nade *n = &g->nd[i];
            n->type = type; n->x0 = x0; n->y0 = y0; n->x1 = tx; n->y1 = ty; n->t = 0;
            n->dur = 0.25f + l * 0.07f; n->owner = op;
            break;
        }
    sfx(g, SFX_THROW, 1.0f, x0, 0.8f);
    o->charges--;
    if (type == AB_FLASH) radio(g, op, "FLASH OUT!", SFX_V_FLASH);
    else radio(g, op, "SMOKE OUT!", SFX_V_SMOKE);
}

static void nade_pop(Game *g, Nade *n)
{
    float x = n->x1, y = n->y1;
    if (n->type == AB_FLASH) {
        sfx(g, SFX_FLASHBANG, 1.0f, x, 1.0f);
        add_light(g, x * TS, y * TS, 7.0f * TS, 3.5f, 3.5f, 3.5f, 0.6f);
        shake(g, 4.0f);
        for (int i = 0; i < 14; i++) {
            float a = rnd(g) * TAU_F;
            spawn(g, PT_SPARK, x, y, 0.3f, cosf(a) * (3 + rnd(g) * 5), sinf(a) * (3 + rnd(g) * 5), 1.5f, 0.3f, 1.2f, 0xFFFFFFFFu);
        }
        for (int i = 0; i < 6; i++) spawn(g, PT_SMOKE, x + rnds(g) * 0.3f, y + rnds(g) * 0.3f, 0, rnds(g), rnds(g), 0, 2.0f, 8.0f, 0xC8C8C8FFu);
        for (int i = 0; i < g->nen; i++) {
            Enemy *e = &g->en[i];
            if (!e->alive) continue;
            float d = dist(e->x, e->y, x, y);
            if (d > 4.6f || !los(g, x, y, e->x, e->y, 1)) continue;
            e->blind = fmaxf(e->blind, 4.2f * (1.0f - d / 7.0f) + 0.8f);
            e->state = e->state < ES_ALERT ? ES_ALERT : e->state;
        }
        int near = 0;
        for (int i = 0; i < NOPS; i++) {
            Op *o = &g->op[i];
            if (!o->present || o->state == OS_DOWN) continue;
            float d = dist(o->x, o->y, x, y);
            if (d > 3.2f || !los(g, x, y, o->x, o->y, 1)) continue;
            float facing = cosf(o->face) * (x - o->x) + sinf(o->face) * (y - o->y);
            if (facing > 0) { o->blind = fmaxf(o->blind, 1.4f); near = 1; }
        }
        if (near) g->flash_t = 0.6f;
        noise_at(g, x, y, 11.0f, 1);
        g->combat_t = 0;
    } else {
        sfx(g, SFX_SMOKE, 1.0f, x, 0.9f);
        for (int i = 0; i < MAX_SMOKE; i++)
            if (g->sm[i].life <= 0) { g->sm[i].x = x; g->sm[i].y = y; g->sm[i].r = 2.3f; g->sm[i].life = 13.0f; break; }
        noise_at(g, x, y, 4.0f, 0);
    }
}

/* the ability waiting at the operator's current waypoint */
static int pending_ability(const Game *g, const Op *o)
{
    (void)g;
    if (o->cur < 0 || o->cur >= o->nwp) return AB_NONE;
    return o->wp[o->cur].ab;
}

static void use_wp_ability(Game *g, int i)
{
    Op *o = &g->op[i];
    const Waypoint *w = &o->wp[o->cur];
    switch (w->ab) {
    case AB_BREACH: {
        int c = charge_at(g, (int)w->ax, (int)w->ay);
        if (c < 0 && o->charges > 0 && breachable(g, (int)w->ax, (int)w->ay)) {
            plant_charge(g, i, (int)w->ax, (int)w->ay);
            o->charges--;
            c = charge_at(g, (int)w->ax, (int)w->ay);
        }
        if (c >= 0) { radio(g, i, "BREACHING!", SFX_V_BREACH); detonate(g, c); }
        break;
    }
    case AB_FLASH: case AB_SMOKE:
        if (o->charges > 0) throw_nade(g, i, w->ab, w->ax + 0.5f, w->ay + 0.5f);
        break;
    default: break;
    }
    o->wp[o->cur].ab = AB_NONE;        /* used */
}

void go_code(Game *g)
{
    int any_ab = 0, released = 0, holders = 0;
    for (int i = 0; i < NOPS; i++) if (g->op[i].present && g->op[i].state == OS_HOLD) holders++;
    if (!holders) {
        sfx(g, SFX_ERROR, 1.0f, -1, 0.6f);
        radio(g, -1, "NOBODY IS WAITING FOR THE GO CODE", SFX_SQUELCH);
        return;
    }
    for (int i = 0; i < NOPS; i++) {
        Op *o = &g->op[i];
        if (o->present && o->state == OS_HOLD && pending_ability(g, o) != AB_NONE) any_ab = 1;
    }
    for (int i = 0; i < NOPS; i++) {
        Op *o = &g->op[i];
        if (!o->present || o->state != OS_HOLD) continue;
        released++;
        if (pending_ability(g, o) != AB_NONE) {
            o->state = OS_DELAY;
            o->act_t = 0.05f + 0.1f * (float)i;      /* charges first, flashes right after */
            o->act_phase = 1;
            if (o->wp[o->cur].ab == AB_BREACH) o->act_t = 0.02f;
        } else {
            o->state = OS_DELAY;
            o->act_t = any_ab ? 1.05f : 0.12f + 0.08f * (float)i;
            o->act_phase = 0;
        }
    }
    g->gos++;
    g->go_t = 0;
    sfx(g, SFX_GO, 1.0f, -1, 1.0f);
    radio(g, -1, "GO! GO! GO!", SFX_V_GO);
    g->intensity = 1.0f;
}

/* ------------------------------------------------------------------ */
/* movement                                                             */
/* ------------------------------------------------------------------ */
static int solid_for_move(const Game *g, int x, int y)
{
    if (!in_map(g, x, y)) return 1;
    const Tile *t = &g->tile[y][x];
    if (t->kind == K_DOOR) return !t->open;
    return !passable(g, x, y);
}

/* push a circle out of solid tiles */
static void collide(const Game *g, float *px, float *py, float r)
{
    int cx = (int)floorf(*px), cy = (int)floorf(*py);
    for (int y = cy - 1; y <= cy + 1; y++)
        for (int x = cx - 1; x <= cx + 1; x++) {
            if (!solid_for_move(g, x, y)) continue;
            float nx = clampf(*px, (float)x, (float)x + 1), ny = clampf(*py, (float)y, (float)y + 1);
            float dx = *px - nx, dy = *py - ny, d2 = dx * dx + dy * dy;
            if (d2 >= r * r) continue;
            float d = sqrtf(d2);
            if (d < 1e-4f) { /* centre inside the tile: push out along the shortest axis */
                float l = *px - (float)x, rr = (float)x + 1 - *px, u = *py - (float)y, b = (float)y + 1 - *py;
                float m = fminf(fminf(l, rr), fminf(u, b));
                if (m == l) *px = (float)x - r; else if (m == rr) *px = (float)x + 1 + r;
                else if (m == u) *py = (float)y - r; else *py = (float)y + 1 + r;
                continue;
            }
            *px = nx + dx / d * r; *py = ny + dy / d * r;
        }
}

static void op_open_ahead(Game *g, Op *o, int tx, int ty)
{
    Tile *t = &g->tile[ty][tx];
    if (t->kind == K_DOOR && !t->open) {
        open_door(g, tx, ty, o->x, o->y);
        sfx(g, SFX_DOOR, 0.9f + rnd(g) * 0.2f, (float)tx, 0.6f);
        noise_at(g, (float)tx + 0.5f, (float)ty + 0.5f, 3.0f, 0);
    }
}

/* follow the path; returns 1 when the end is reached */
static int follow_path(Game *g, Op *o, float speed, float dt)
{
    if (o->pidx >= o->plen) return 1;
    int tx = o->path[o->pidx][0], ty = o->path[o->pidx][1];
    if (!passable(g, tx, ty)) { o->state = OS_BLOCKED; o->stuck = 0; return 0; }
    op_open_ahead(g, o, tx, ty);
    float gx = (float)tx + 0.5f, gy = (float)ty + 0.5f;
    float dx = gx - o->x, dy = gy - o->y, d = sqrtf(dx * dx + dy * dy);
    float step = speed * dt;
    if (d <= step + 0.02f) {
        o->x = gx; o->y = gy;
        o->pidx++;
        return o->pidx >= o->plen;
    }
    o->x += dx / d * step; o->y += dy / d * step;
    float pw = o->walk;
    o->walk += step * 3.2f;
    if ((int)(pw / PI_F) != (int)(o->walk / PI_F)) sfx(g, SFX_STEP, 0.9f + rnd(g) * 0.2f, o->x, 0.12f);
    if (o->target < 0) o->face = turn_to(o->face, atan2f(dy, dx), 9.0f * dt);
    return 0;
}

/* ------------------------------------------------------------------ */
/* shooting                                                             */
/* ------------------------------------------------------------------ */
static void bullet_miss(Game *g, float x0, float y0, float a, float range, int enemy)
{
    float l = ray_len(g, x0, y0, a, range + 4.0f);
    float hx = x0 + cosf(a) * l, hy = y0 + sinf(a) * l;
    tracer(g, x0, y0, hx, hy, enemy);
    if (l < range + 3.9f) {
        int tx = (int)floorf(hx + cosf(a) * 0.05f), ty = (int)floorf(hy + sinf(a) * 0.05f);
        if (in_map(g, tx, ty) && g->tile[ty][tx].kind == K_WINDOW && !(g->tile[ty][tx].flags & TF_BROKEN)) {
            g->tile[ty][tx].flags |= TF_BROKEN;
            sfx(g, SFX_GLASS, 0.9f + rnd(g) * 0.2f, hx, 0.8f);
            for (int k = 0; k < 8; k++) spawn(g, PT_GLASS, hx, hy, 0.6f, cosf(a) * 2 + rnds(g) * 2, sinf(a) * 2 + rnds(g) * 2, 2, 1.0f, 1.0f, 0xBFE8FFFFu);
        } else {
            for (int k = 0; k < 4; k++) {
                float b = a + PI_F + rnds(g) * 1.0f, s = 2.0f + rnd(g) * 4.0f;
                spawn(g, PT_SPARK, hx - cosf(a) * 0.05f, hy - sinf(a) * 0.05f, 0.5f, cosf(b) * s, sinf(b) * s, 1.0f, 0.18f + rnd(g) * 0.12f, 1.0f, 0xFFD27AFFu);
            }
            spawn(g, PT_DUST, hx - cosf(a) * 0.1f, hy - sinf(a) * 0.1f, 0.5f, -cosf(a) * 0.6f, -sinf(a) * 0.6f, 0, 0.7f, 4.0f, 0x9A9288FFu);
            decal(g, DC_HOLE, hx - cosf(a) * 0.04f, hy - sinf(a) * 0.04f, 0.1f, 0x1A1614FFu);
            if (rnd(g) < 0.25f) sfx(g, rnd(g) < 0.5f ? SFX_RICOCHET : SFX_IMPACT, 0.85f + rnd(g) * 0.3f, hx, 0.35f);
        }
    } else {
        decal(g, DC_HOLE, hx, hy, 0.08f, 0x1A161480u);
    }
}

static void muzzle(Game *g, float x, float y, float a, int wp, int enemy)
{
    float mx = x + cosf(a) * 0.55f, my = y + sinf(a) * 0.55f;
    add_light(g, mx * TS, my * TS, (wp == WP_SMG ? 2.0f : 3.6f) * TS, 1.6f, 1.2f, 0.6f, 0.07f);
    if (wp != WP_PISTOL && wp != WP_E_PISTOL) {
        float b = a + PI_F * 0.5f + rnds(g) * 0.4f, s = 2.0f + rnd(g);
        spawn(g, PT_CASING, x + cosf(a) * 0.2f, y + sinf(a) * 0.2f, 0.6f, cosf(b) * s, sinf(b) * s, 2.5f, 0.7f, 1.0f, wp == WP_SHOTGUN || wp == WP_E_SHOTGUN ? 0xC83C30FFu : 0xD8B048FFu);
    }
    (void)enemy;
}

static void hit_enemy(Game *g, int ei, float dmg, int by)
{
    Enemy *e = &g->en[ei];
    if (!e->alive) return;
    if (e->type == EN_HEAVY) dmg *= 0.75f;
    e->hp -= dmg;
    e->hurt = 0.2f;
    for (int k = 0; k < 3; k++) spawn(g, PT_BLOOD, e->x, e->y, 0.5f, rnds(g) * 2, rnds(g) * 2, 1.5f, 0.4f, 1.2f, 0x8C1A1AFFu);
    sfx(g, SFX_HITFLESH, 0.9f + rnd(g) * 0.2f, e->x, 0.5f);
    if (e->state < ES_COMBAT && by >= 0) {
        e->state = ES_COMBAT; e->react = 0.25f; e->target = by;
        e->want_face = atan2f(g->op[by].y - e->y, g->op[by].x - e->x);
    }
}

static void kill_enemy(Game *g, int ei, int by)
{
    Enemy *e = &g->en[ei];
    if (!e->alive) return;
    e->alive = 0; e->state = ES_DEAD; e->die_t = 0;
    decal(g, DC_BLOOD, e->x + cosf(e->face + PI_F) * 0.3f, e->y + sinf(e->face + PI_F) * 0.3f, 0.55f + rnd(g) * 0.2f, 0x5A0E0EFFu);
    sfx(g, SFX_BODYFALL, 0.9f + rnd(g) * 0.2f, e->x, 0.7f);
    noise_at(g, e->x, e->y, 2.5f, 0);
    g->kills++;
    if (by >= 0) {
        g->op[by].kills++;
        if (g->op[by].say_cool <= 0) { radio(g, by, "TANGO DOWN", voice_for(by, SFX_V_TANGO_M)); g->op[by].say_cool = 4.0f; }
    }
    if (e->guard >= 0 && g->hs[e->guard].state == HS_CAPTIVE) g->hs[e->guard].exec = 99;     /* guard gone: hostage safe */
}

static void hit_op(Game *g, int oi, float dmg)
{
    Op *o = &g->op[oi];
    if (o->state == OS_DOWN) return;
    o->hp -= dmg;
    o->hurt = 0.25f;
    spawn(g, PT_BLOOD, o->x, o->y, 0.5f, rnds(g) * 2, rnds(g) * 2, 1.5f, 0.4f, 1.1f, 0x8C1A1AFFu);
    sfx(g, SFX_HITFLESH, 0.8f + rnd(g) * 0.2f, o->x, 0.6f);
    if (o->hp <= 0) {
        o->hp = 0; o->state = OS_DOWN; o->manual = 0; o->target = -1; o->plen = 0;
        g->downed_any = 1;
        decal(g, DC_BLOOD, o->x, o->y, 0.5f, 0x5A0E0EFFu);
        sfx(g, SFX_BODYFALL, 0.8f, o->x, 0.8f);
        int sayer = -1;
        for (int i = 0; i < NOPS; i++) if (i != oi && g->op[i].present && g->op[i].state != OS_DOWN) { sayer = i; break; }
        if (sayer >= 0) {
            char buf[40];
            strcpy(buf, op_name(oi)); strcat(buf, " IS DOWN!");
            radio(g, sayer, buf, voice_for(sayer, SFX_V_MANDOWN_M));
        }
    }
}

/* one trigger pull (all pellets) */
static void fire(Game *g, float x, float y, float a, int wp, int enemy, int shooter, int target, float tdist, int tmoving, int smoving)
{
    const Weapon *w = &WEAP[wp];
    sfx(g, w->sfx, 0.94f + rnd(g) * 0.12f, x, enemy ? 0.85f : 0.95f);
    muzzle(g, x, y, a, wp, enemy);
    if (w->noise > 3.0f) noise_at(g, x, y, w->noise, 1);
    else noise_at(g, x, y, w->noise, 0);
    g->combat_t = 0;
    if (!enemy) { g->op[shooter].shots++; g->shots_fired++; g->op[shooter].muzzle = 0.06f; g->op[shooter].recoil = 1.0f; }
    else { g->en[shooter].muzzle = 0.06f; g->en[shooter].recoil = 1.0f; }

    float p = w->acc * clampf(1.18f - tdist / w->range * 0.75f, 0.15f, 1.0f);
    if (tmoving) p *= 0.85f;
    if (smoving) p *= 0.8f;
    if (enemy) {
        const Enemy *e = &g->en[shooter];
        p *= 0.9f + 0.04f * (float)g->mission;                     /* later missions: sharper shooters */
        if (e->alerted_t < 1.2f) p *= 0.7f;                         /* rushed first shots */
        if (target >= 0 && g->op[target].manual) p *= 0.9f;
    }
    float ox = x + cosf(a) * 0.5f, oy = y + sinf(a) * 0.5f;
    for (int k = 0; k < w->pellets; k++) {
        float pa = a + rnds(g) * w->spread;
        float pp = w->pellets > 1 ? p * clampf(1.4f - tdist / w->range, 0.1f, 1.0f) : p;
        int hit = target >= 0 && rnd(g) < pp;
        if (hit) {
            float tx = enemy ? g->op[target].x : g->en[target].x, ty = enemy ? g->op[target].y : g->en[target].y;
            tracer(g, ox, oy, tx + rnds(g) * 0.12f, ty + rnds(g) * 0.12f, enemy);
            if (enemy) hit_op(g, target, w->dmg);
            else {
                hit_enemy(g, target, w->dmg, shooter);
                g->op[shooter].hits++; g->shots_hit++;
            }
        } else {
            float miss = pa + (rnd(g) < 0.5f ? -1.0f : 1.0f) * (0.05f + rnd(g) * 0.08f);
            bullet_miss(g, ox, oy, miss, w->range, enemy);
            if (enemy && target >= 0 && rnd(g) < 0.3f) sfx(g, SFX_WHIZ, 0.9f + rnd(g) * 0.3f, g->op[target].x, 0.4f);
        }
    }
    if (!enemy) for (int i = 0; i < g->nen; i++) if (g->en[i].alive && g->en[i].hp <= 0) kill_enemy(g, i, shooter);
}

/* ------------------------------------------------------------------ */
/* operators: the frame                                                 */
/* ------------------------------------------------------------------ */
static int op_can_see(const Game *g, const Op *o, const Enemy *e, float maxd, float fov)
{
    float d = dist(o->x, o->y, e->x, e->y);
    if (d > maxd) return 0;
    if (fov < PI_F) {
        float a = atan2f(e->y - o->y, e->x - o->x);
        if (fabsf(angdiff(o->face, a)) > fov && d > 1.4f) return 0;
    }
    return los(g, o->x, o->y, e->x, e->y, 1);
}

static void op_combat(Game *g, int i, float dt)
{
    Op *o = &g->op[i];
    const Weapon *w = &WEAP[OP_WEAP[i]];
    /* pick a target */
    int best = -1; float bd = 1e9f;
    for (int k = 0; k < g->nen; k++) {
        const Enemy *e = &g->en[k];
        if (!e->alive) continue;
        float fov = (k == o->target) ? PI_F : 1.45f;
        if (!op_can_see(g, o, e, w->range + 2.0f, fov)) continue;
        float d = dist(o->x, o->y, e->x, e->y);
        if (e->guard >= 0 && g->alarm) d -= 6.0f;        /* a guard about to hurt a hostage goes first */
        if (e->state == ES_COMBAT) d -= 1.5f;
        if (d < bd) { bd = d; best = k; }
    }
    if (best != o->target) {
        if (best >= 0 && o->target < 0) {
            o->react = OP_REACT[i] * (0.8f + rnd(g) * 0.4f);
            if (o->say_cool <= 0 && g->combat_t > 3.0f) { radio(g, i, "CONTACT!", voice_for(i, SFX_V_CONTACT_M)); o->say_cool = 5.0f; }
        } else if (best >= 0) o->react = 0.12f;
        o->target = best;
    }
    if (o->target < 0) return;
    Enemy *e = &g->en[o->target];
    float a = atan2f(e->y - o->y, e->x - o->x);
    o->face = turn_to(o->face, a, 10.0f * dt);
    if (o->react > 0) { o->react -= dt; return; }
    if (o->blind > 0) return;
    if (fabsf(angdiff(o->face, a)) > 0.22f) return;
    o->cool -= dt;
    if (o->cool > 0) return;
    float d = dist(o->x, o->y, e->x, e->y);
    if (d > w->range + 1.0f) return;
    int moving = o->state == OS_MOVE || o->manual;
    fire(g, o->x, o->y, o->face, OP_WEAP[i], 0, i, o->target, d, e->state == ES_WANDER || e->plen > 0, moving);
    o->burst++;
    if (o->burst >= w->burst) { o->burst = 0; o->cool = w->interval + w->pause * (0.8f + rnd(g) * 0.4f); }
    else o->cool = w->interval;
}

static void op_arrive(Game *g, int i)
{
    Op *o = &g->op[i];
    Waypoint *w = &o->wp[o->cur];
    if (w->ab == AB_BREACH) {
        /* plant first; blow it now, or when the GO code comes */
        if (o->charges > 0 && breachable(g, (int)w->ax, (int)w->ay) && charge_at(g, (int)w->ax, (int)w->ay) < 0) {
            plant_charge(g, i, (int)w->ax, (int)w->ay);
            o->charges--;
        }
        o->face = atan2f(w->ay + 0.5f - o->y, w->ax + 0.5f - o->x);
        if (w->hold) { o->state = OS_HOLD; }
        else { o->state = OS_ACTION; o->act_t = 1.2f; o->act_phase = 0; }
    } else if (w->ab != AB_NONE) {
        if (w->hold) o->state = OS_HOLD;
        else { use_wp_ability(g, i); o->state = OS_ACTION; o->act_t = 0.75f; o->act_phase = 1; }
    } else if (w->hold) {
        o->state = OS_HOLD;
    } else {
        next_wp(g, o);
        return;
    }
    if (o->state == OS_HOLD && !o->arrived_say) {
        o->arrived_say = 1;
        radio(g, i, "IN POSITION", voice_for(i, SFX_V_READY_M));
    }
}

/* Mae picks up teammates on her own */
static int medic_think(Game *g, float dt)
{
    Op *m = &g->op[OP_MAE];
    if (!m->present || m->state == OS_DOWN || m->manual) return 0;
    if (m->state == OS_REVIVE) {
        Op *t = &g->op[m->revive_tgt];
        if (t->state != OS_DOWN) { m->state = OS_IDLE; plan_reroute(g, OP_MAE); return 1; }
        m->face = turn_to(m->face, atan2f(t->y - m->y, t->x - m->x), 8.0f * dt);
        if (m->target >= 0) return 1;                 /* shooting back: pause */
        m->revive_t += dt;
        if (m->revive_t >= 2.4f) {
            t->hp = 45; t->state = OS_IDLE; t->blind = 0;
            if (t->cur < t->nwp) plan_reroute(g, m->revive_tgt);
            sfx(g, SFX_SECURE, 1.2f, t->x, 0.7f);
            radio(g, OP_MAE, "BACK ON YOUR FEET", SFX_SQUELCH);
            m->state = OS_IDLE;
            if (m->cur < m->nwp) plan_reroute(g, OP_MAE);
        }
        return 1;
    }
    if (m->target >= 0 || m->state == OS_REVIVE + 100) return 0;
    for (int i = 0; i < NOPS; i++) {
        Op *t = &g->op[i];
        if (i == OP_MAE || !t->present || t->state != OS_DOWN) continue;
        float d = dist(m->x, m->y, t->x, t->y);
        if (d < 0.95f) { m->state = OS_REVIVE; m->revive_tgt = i; m->revive_t = 0; m->plen = 0; return 1; }
        if (d < 9.0f) {
            /* walk over: a path to the downed operator's tile */
            uint8_t tmp[MAX_PATH][2];
            int n = find_path(g, (int)m->x, (int)m->y, (int)t->x, (int)t->y, 0, tmp, MAX_PATH);
            if (n >= 0 && n < 16) {
                if (n > 0) n--;                       /* stop next to them */
                memcpy(m->path, tmp, sizeof tmp[0] * (size_t)n);
                m->plen = n; m->pidx = 0;
                m->state = OS_REVIVE + 100;            /* marker: walking to revive (handled below) */
                m->revive_tgt = i;
                return 0;
            }
        }
    }
    return 0;
}

static void ops_update(Game *g, const Input *in, float dt)
{
    medic_think(g, dt);
    for (int i = 0; i < NOPS; i++) {
        Op *o = &g->op[i];
        if (!o->present) continue;
        if (o->say_cool > 0) o->say_cool -= dt;
        if (o->hurt > 0) o->hurt -= dt;
        if (o->muzzle > 0) o->muzzle -= dt;
        o->recoil *= expf(-dt * 14.0f);
        if (o->blind > 0) o->blind -= dt;
        if (o->state == OS_DOWN) continue;

        op_combat(g, i, dt);
        int engaging = o->target >= 0;

        /* the player steering this one */
        if (o->manual && i == g->sel && !g->tac) {
            float mx = in->sx, my = in->sy;
            float m = sqrtf(mx * mx + my * my);
            if (m > 0.05f) {
                float sp = OP_SPEED[i] * 1.05f * (m > 1 ? 1 : m);
                float nx = o->x + mx / (m > 1 ? m : 1) * sp * dt, ny = o->y + my / (m > 1 ? m : 1) * sp * dt;
                int tx = (int)floorf(nx + mx * 0.3f), ty = (int)floorf(ny + my * 0.3f);
                if (in_map(g, tx, ty)) op_open_ahead(g, o, tx, ty);
                o->x = nx; o->y = ny;
                collide(g, &o->x, &o->y, 0.28f);
                o->walk += sp * dt * 3.2f;
                if (!engaging && sqrtf(in->rx * in->rx + in->ry * in->ry) < 0.3f) o->face = turn_to(o->face, atan2f(my, mx), 10.0f * dt);
            }
            if (sqrtf(in->rx * in->rx + in->ry * in->ry) > 0.3f && !engaging) o->face = turn_to(o->face, atan2f(in->ry, in->rx), 12.0f * dt);
            continue;
        }

        if (i == OP_MAE && o->state == OS_REVIVE + 100) {
            if (!engaging && follow_path(g, o, OP_SPEED[i] * 1.15f, dt)) { o->state = OS_REVIVE; o->revive_t = 0; }
            if (o->state == OS_BLOCKED) o->state = OS_IDLE;
            continue;
        }

        switch (o->state) {
        case OS_IDLE:
            /* plan done and the last hostiles are marked: clear the building */
            if ((g->last_ping || g->area_clear) && !engaging) {
                if (o->pidx >= o->plen) {
                    o->stuck -= dt;
                    if (o->stuck <= 0) {
                        o->stuck = 1.0f;
                        int best = -1; float bd = 1e9f, bx = 0, by = 0;
                        for (int k = 0; k < g->nen; k++) if (g->en[k].alive) { float d = dist(o->x, o->y, g->en[k].x, g->en[k].y); if (d < bd) { bd = d; best = k; bx = g->en[k].x; by = g->en[k].y; } }
                        if (best < 0)
                            for (int k = 0; k < g->nhs; k++) if (g->hs[k].state == HS_CAPTIVE) { float d = dist(o->x, o->y, g->hs[k].x, g->hs[k].y); if (d < bd) { bd = d; best = k; bx = g->hs[k].x; by = g->hs[k].y; } }
                        if (best >= 0) {
                            int n = find_path(g, (int)o->x, (int)o->y, (int)bx, (int)by, 0, o->path, MAX_PATH);
                            if (n > 1) { o->plen = n - 1; o->pidx = 0; }
                        }
                    }
                } else {
                    follow_path(g, o, OP_SPEED[i] * 0.8f, dt);
                    if (o->state == OS_BLOCKED) { o->state = OS_IDLE; o->plen = 0; }
                }
            }
            break;
        case OS_MOVE:
            if (o->act_t > 0) { o->act_t -= dt; break; }
            if (engaging) break;                         /* stop and fight */
            if (follow_path(g, o, OP_SPEED[i], dt)) op_arrive(g, i);
            break;
        case OS_ACTION:
            o->act_t -= dt;
            if (o->act_phase == 0 && o->act_t < 0.6f) {      /* unheld breach: blow it after the plant */
                const Waypoint *w = &o->wp[o->cur];
                int c = charge_at(g, (int)w->ax, (int)w->ay);
                if (c >= 0) { radio(g, i, "BREACHING!", SFX_V_BREACH); detonate(g, c); }
                o->wp[o->cur].ab = AB_NONE;
                o->act_phase = 1;
            }
            if (o->act_t <= 0) next_wp(g, o);
            break;
        case OS_DELAY:
            o->act_t -= dt;
            if (o->act_t <= 0) {
                if (o->act_phase == 1 && pending_ability(g, o) != AB_NONE) {
                    use_wp_ability(g, i);
                    o->act_t = o->wp[o->cur].ab == AB_NONE ? 0.55f : 0.3f;
                    o->act_phase = 0;
                } else next_wp(g, o);
            }
            break;
        case OS_BLOCKED:
            o->stuck += dt;
            if (o->plen > 0 && o->pidx < o->plen && passable(g, o->path[o->pidx][0], o->path[o->pidx][1])) o->state = OS_MOVE;
            else if (o->stuck > 1.0f && o->cur < o->nwp) {
                o->stuck = 0;
                const Waypoint *w = &o->wp[o->cur];
                int n = find_path(g, (int)o->x, (int)o->y, w->tx, w->ty, 0, o->path, MAX_PATH);
                if (n >= 0) { o->plen = n; o->pidx = 0; o->state = OS_MOVE; }
            }
            break;
        default: break;
        }
    }
    /* keep operators from standing inside each other */
    for (int i = 0; i < NOPS; i++)
        for (int j = i + 1; j < NOPS; j++) {
            Op *a = &g->op[i], *b = &g->op[j];
            if (!a->present || !b->present) continue;
            float dx = b->x - a->x, dy = b->y - a->y, d = sqrtf(dx * dx + dy * dy);
            if (d > 0.52f) continue;
            if (d < 0.01f) { dx = (float)(j - i) * 0.3f; dy = 0.2f; d = sqrtf(dx * dx + dy * dy); }
            float push = (0.52f - d) * 0.5f;
            int am = a->state == OS_MOVE && a->pidx < a->plen, bm = b->state == OS_MOVE && b->pidx < b->plen;
            float ka = am ? 0.0f : 1.0f, kb = bm ? 0.0f : 1.0f;      /* movers pass through; those standing make room */
            if (am && bm) continue;
            if (a->state == OS_DOWN) ka = 0; if (b->state == OS_DOWN) kb = 0;
            a->x -= dx / d * push * ka; a->y -= dy / d * push * ka;
            b->x += dx / d * push * kb; b->y += dy / d * push * kb;
            collide(g, &a->x, &a->y, 0.28f);
            collide(g, &b->x, &b->y, 0.28f);
        }
}

/* ------------------------------------------------------------------ */
/* hostiles                                                             */
/* ------------------------------------------------------------------ */
static void en_walk_to(Game *g, Enemy *e, float tx, float ty)
{
    uint8_t tmp[MAX_PATH][2];
    int n = find_path(g, (int)e->x, (int)e->y, (int)tx, (int)ty, 0, tmp, MAX_PATH);
    if (n < 0 || n > 24) { e->plen = 0; return; }
    memcpy(e->path, tmp, sizeof tmp[0] * (size_t)n);
    e->plen = n; e->pidx = 0;
}

static int en_follow(Game *g, Enemy *e, float speed, float dt, int face)
{
    if (e->pidx >= e->plen) { e->plen = 0; return 1; }
    int tx = e->path[e->pidx][0], ty = e->path[e->pidx][1];
    if (!passable(g, tx, ty)) { e->plen = 0; return 1; }
    Tile *t = &g->tile[ty][tx];
    if (t->kind == K_DOOR && !t->open) { open_door(g, tx, ty, e->x, e->y); sfx(g, SFX_DOOR, 1.0f, (float)tx, 0.5f); }
    float gx = (float)tx + 0.5f, gy = (float)ty + 0.5f, dx = gx - e->x, dy = gy - e->y, d = sqrtf(dx * dx + dy * dy);
    float step = speed * dt;
    if (d <= step + 0.02f) { e->x = gx; e->y = gy; e->pidx++; }
    else { e->x += dx / d * step; e->y += dy / d * step; }
    float pw = e->walk;
    e->walk += step * 3.2f;
    if ((int)(pw / PI_F) != (int)(e->walk / PI_F)) {
        /* you hear them before you see them */
        float near = 99;
        for (int k = 0; k < NOPS; k++) if (g->op[k].present && g->op[k].state != OS_DOWN) { float q = dist(g->op[k].x, g->op[k].y, e->x, e->y); if (q < near) near = q; }
        if (near < 7.0f) sfx(g, SFX_STEP, 0.75f + rnd(g) * 0.15f, e->x, 0.35f * (1.0f - near / 7.0f));
    }
    if (face) e->want_face = atan2f(dy, dx);
    return 0;
}

static void enemies_update(Game *g, float dt)
{
    for (int i = 0; i < g->nen; i++) {
        Enemy *e = &g->en[i];
        if (!e->alive) { e->die_t += dt; continue; }
        if (e->hurt > 0) e->hurt -= dt;
        if (e->muzzle > 0) e->muzzle -= dt;
        if (e->mark > 0 && e->mark < 900) e->mark -= dt;
        e->recoil *= expf(-dt * 14.0f);
        if (e->state >= ES_ALERT) e->alerted_t += dt;
        if (e->stun > 0) e->stun -= dt;
        if (e->blind > 0) e->blind -= dt;
        int disabled = e->stun > 0 || e->blind > 0;
        const Weapon *w = &WEAP[EN_WEAP[e->type]];

        if (disabled) {
            /* staggering, covering their eyes */
            e->want_face += rnds(g) * 6.0f * dt;
            e->face = turn_to(e->face, e->want_face, 3.0f * dt);
            e->target = -1;
            continue;
        }

        /* who can they see? */
        int best = -1; float bd = 1e9f;
        float fov = e->state >= ES_ALERT ? 1.95f : 1.05f;
        float range = e->state >= ES_ALERT ? 15.0f : 11.0f;
        for (int k = 0; k < NOPS; k++) {
            const Op *o = &g->op[k];
            if (!o->present || o->state == OS_DOWN) continue;
            float d = dist(e->x, e->y, o->x, o->y);
            if (d > range) continue;
            float a = atan2f(o->y - e->y, o->x - e->x);
            if (k != e->target && d > 1.3f && fabsf(angdiff(e->face, a)) > fov) continue;
            if (!los(g, e->x, e->y, o->x, o->y, 1)) continue;
            if (d < bd) { bd = d; best = k; }
        }

        if (best >= 0) {
            if (e->state != ES_COMBAT || e->target < 0) {
                float base = EN_REACT[e->type] * (e->state >= ES_ALERT ? 0.55f : 1.0f) * (1.0f - 0.025f * (float)g->mission);
                if (e->state != ES_COMBAT) e->react = base * (0.85f + rnd(g) * 0.3f);
                if (e->state < ES_ALERT) e->alerted_t = 0;
                e->state = ES_COMBAT;
            }
            e->target = best;
            const Op *o = &g->op[best];
            e->lx = o->x; e->ly = o->y;
            e->want_face = atan2f(o->y - e->y, o->x - e->x);
            e->face = turn_to(e->face, e->want_face, (e->type == EN_HEAVY ? 4.0f : 6.0f) * dt);
            e->look_t = 0;
            if (e->react > 0) {
                e->react -= dt;
                if (e->react <= 0) {
                    if (!g->alarm) sfx(g, SFX_ALERT, 1.0f, e->x, 0.8f);
                    sfx(g, rnd(g) < 0.5f ? SFX_V_HEY : SFX_V_GETEM, 0.95f + rnd(g) * 0.15f, e->x, 0.8f);
                    g->alarm = 1;
                }
                continue;
            }
            e->plen = 0;
            if (fabsf(angdiff(e->face, e->want_face)) < 0.25f) {
                e->cool -= dt;
                if (e->cool <= 0) {
                    fire(g, e->x, e->y, e->face, EN_WEAP[e->type], 1, i, best, bd, o->state == OS_MOVE, 0);
                    e->burst++;
                    if (e->burst >= w->burst) { e->burst = 0; e->cool = w->interval + w->pause * (0.8f + rnd(g) * 0.5f); }
                    else e->cool = w->interval;
                }
            }
            continue;
        }

        /* nobody in sight */
        if (e->state == ES_COMBAT) {
            e->look_t += dt;
            if (e->look_t > 1.6f) {
                e->state = ES_SEARCH; e->target = -1; e->look_t = 0;
                if (e->guard < 0 && e->type != EN_GUNMAN) en_walk_to(g, e, e->lx, e->ly);
            }
        }
        e->target = -1;
        if (e->guard >= 0) {
            /* guards stay with their hostage */
            if (e->state >= ES_ALERT) e->want_face = atan2f(e->ly - e->y, e->lx - e->x);
            e->face = turn_to(e->face, e->want_face, 4.0f * dt);
            continue;
        }
        switch (e->state) {
        case ES_IDLE:
            e->look_t += dt;
            if (e->look_t > 2.5f + (float)(i % 3)) { e->look_t = 0; e->want_face = e->hface + rnds(g) * 0.9f; }
            e->face = turn_to(e->face, e->want_face, 1.5f * dt);
            break;
        case ES_WANDER:
            if (e->plen > 0) en_follow(g, e, EN_SPEED[e->type] * 0.45f, dt, 1);
            else {
                e->wander_t -= dt;
                if (e->wander_t <= 0) {
                    e->wander_t = 2.0f + rnd(g) * 3.0f;
                    int r = room_at(g, e->hx, e->hy);
                    for (int tries = 0; tries < 12; tries++) {
                        int tx = (int)e->hx + (int)(rnds(g) * 4.0f), ty = (int)e->hy + (int)(rnds(g) * 3.0f);
                        if (passable(g, tx, ty) && g->tile[ty][tx].kind == K_FLOOR && g->tile[ty][tx].room == r) { en_walk_to(g, e, (float)tx + 0.5f, (float)ty + 0.5f); break; }
                    }
                }
            }
            e->face = turn_to(e->face, e->want_face, 3.0f * dt);
            break;
        case ES_ALERT:
            e->face = turn_to(e->face, e->want_face, 5.0f * dt);
            if (e->type == EN_THUG && e->plen == 0 && e->alerted_t > 0.8f && e->alerted_t < 1.0f && dist(e->x, e->y, e->lx, e->ly) < 9.0f)
                en_walk_to(g, e, e->lx, e->ly);
            if (e->plen > 0 && en_follow(g, e, EN_SPEED[e->type] * 0.6f, dt, 1)) e->want_face = atan2f(e->ly - e->y, e->lx - e->x);
            break;
        case ES_SEARCH:
            if (e->plen > 0) en_follow(g, e, EN_SPEED[e->type] * 0.7f, dt, 1);
            else {
                e->look_t += dt;
                if (e->look_t > 1.2f) { e->look_t = 0; e->want_face = e->face + rnds(g) * 2.0f; }
            }
            e->face = turn_to(e->face, e->want_face, 4.0f * dt);
            break;
        default: break;
        }
    }
    /* enemies apart from each other */
    for (int i = 0; i < g->nen; i++)
        for (int j = i + 1; j < g->nen; j++) {
            Enemy *a = &g->en[i], *b = &g->en[j];
            if (!a->alive || !b->alive) continue;
            float dx = b->x - a->x, dy = b->y - a->y, d = sqrtf(dx * dx + dy * dy);
            if (d > 0.5f || d < 0.001f) continue;
            float push = (0.5f - d) * 0.5f;
            a->x -= dx / d * push; a->y -= dy / d * push; b->x += dx / d * push; b->y += dy / d * push;
            collide(g, &a->x, &a->y, 0.28f); collide(g, &b->x, &b->y, 0.28f);
        }
}

/* ------------------------------------------------------------------ */
/* hostages                                                             */
/* ------------------------------------------------------------------ */
static void hostages_update(Game *g, float dt)
{
    for (int h = 0; h < g->nhs; h++) {
        Hostage *hs = &g->hs[h];
        hs->bob += dt;
        if (hs->state == HS_SECURED) { hs->secured_t += dt; continue; }
        if (hs->state == HS_DEAD) continue;
        /* is the guard able to act? */
        int gi = -1;
        for (int i = 0; i < g->nen; i++) if (g->en[i].guard == h && g->en[i].alive) gi = i;
        /* the guard snaps when the fight comes to them: they see you, hear it close by, or the alarm has
         * been going for a long time */
        if (gi >= 0 && !hs->triggered) {
            const Enemy *e = &g->en[gi];
            float dx = e->lx - e->x, dy = e->ly - e->y;
            if (e->state == ES_COMBAT || (e->state >= ES_ALERT && dx * dx + dy * dy < 6.0f * 6.0f && los(g, e->x, e->y, e->lx, e->ly, 0)) || g->alarm_t > 50.0f) {
                hs->triggered = 1;
                hs->exec = MISSIONS[g->mission].exec_time + (g->alarm_t > 50.0f && e->state < ES_ALERT ? 4.0f : 0.0f);
                sfx(g, SFX_ALERT, 0.8f, hs->x, 0.9f);
            }
        }
        if (hs->triggered && gi >= 0) {
            Enemy *e = &g->en[gi];
            if (e->stun <= 0 && e->blind <= 0) {
                float before = hs->exec;
                hs->exec -= dt;
                if ((int)(before * 2) != (int)(hs->exec * 2) && hs->exec > 0) sfx(g, SFX_TICK, hs->exec < 2.0f ? 1.3f : 1.0f, hs->x, 0.55f);
                if (hs->exec <= 0) {
                    e->face = atan2f(hs->y - e->y, hs->x - e->x);
                    fire(g, e->x, e->y, e->face, WP_E_PISTOL, 1, gi, -1, 1.0f, 0, 0);
                    hs->state = HS_DEAD;
                    decal(g, DC_BLOOD, hs->x, hs->y, 0.5f, 0x5A0E0EFFu);
                    g->result = RES_FAIL_HOSTAGE;
                    g->end_t = 0;
                    radio(g, -1, "HOSTAGE KILLED", SFX_V_FAILED);
                }
            }
        }
        /* rescue: an operator next to them with the guard gone */
        if (gi < 0) {
            for (int i = 0; i < NOPS; i++) {
                const Op *o = &g->op[i];
                if (!o->present || o->state == OS_DOWN) continue;
                if (dist(o->x, o->y, hs->x, hs->y) < 1.75f && los(g, o->x, o->y, hs->x, hs->y, 0)) {
                    hs->state = HS_SECURED; hs->secured_t = 0;
                    sfx(g, SFX_SECURE, 1.0f, hs->x, 0.9f);
                    radio(g, i, "HOSTAGE SECURE", voice_for(i, SFX_V_HOSTAGE_M));
                    break;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* the frame                                                            */
/* ------------------------------------------------------------------ */
void sim_update(Game *g, const Input *in, float dt)
{
    if (g->result != RES_NONE) {
        g->end_t += dt;
        sim_fx(g, dt);
        return;
    }
    g->mtime += dt;
    g->go_t += dt;
    if (g->alarm) {
        float before = g->alarm_t;
        g->alarm_t += dt;
        int captive = 0;
        for (int h = 0; h < g->nhs; h++) if (g->hs[h].state == HS_CAPTIVE && !g->hs[h].triggered) captive++;
        if (before < 35.0f && g->alarm_t >= 35.0f && captive) radio(g, -1, "GUARDS ARE PANICKING - GET TO THE HOSTAGES", SFX_SQUELCH);
    }
    g->combat_t += dt;

    ops_update(g, in, dt);
    enemies_update(g, dt);
    hostages_update(g, dt);

    /* thrown gadgets */
    for (int i = 0; i < MAX_NADE; i++) {
        Nade *n = &g->nd[i];
        if (n->dur <= 0) continue;
        n->t += dt;
        if (n->t >= n->dur + (n->type == AB_FLASH ? 0.35f : 0.1f)) { nade_pop(g, n); n->dur = 0; }
    }
    /* a held breach the player never released still counts down in a real assault? no: charges wait for the GO */

    /* fog of war a few times a second */
    g->vis_t -= dt;
    if (g->vis_t <= 0) { update_vis(g); g->vis_t = 0.1f; }
    for (int i = 0; i < g->nen; i++) {
        Enemy *e = &g->en[i];
        int tx = (int)e->x, ty = (int)e->y;
        int v = e->alive && in_map(g, tx, ty) && (g->tile[ty][tx].flags & TF_VIS);
        if (!e->alive && in_map(g, tx, ty) && (g->tile[ty][tx].flags & TF_SEEN)) v = 1;
        e->seen = v;
        if (v && e->alive) { e->ghost = 1; e->gx = e->x; e->gy = e->y; e->gface = e->face; }
    }

    /* readiness: everyone with a GO hold is in position */
    int holding = 0, waiting = 0;
    for (int i = 0; i < NOPS; i++) {
        const Op *o = &g->op[i];
        if (!o->present || o->state == OS_DOWN) continue;
        if (o->state == OS_HOLD) { holding++; continue; }
        for (int k = (o->cur < 0 ? 0 : o->cur); k < o->nwp; k++)
            if (o->wp[k].hold) { waiting++; break; }        /* still on the way to their next hold */
    }
    int ready = holding > 0 && waiting == 0;
    if (ready && !g->all_ready) {
        sfx(g, SFX_RISER, 1.0f, -1, 0.8f);
        g->riser_t = 0;
        radio(g, -1, "ALL TEAMS IN POSITION - PRESS X FOR GO", SFX_SQUELCH);
    }
    g->all_ready = ready;
    if (ready) g->ready_t += dt; else g->ready_t = 0;

    /* outcome */
    int alive_en = 0, captive = 0, ops_up = 0;
    for (int i = 0; i < g->nen; i++) if (g->en[i].alive) alive_en++;
    for (int h = 0; h < g->nhs; h++) if (g->hs[h].state == HS_CAPTIVE) captive++;
    for (int i = 0; i < NOPS; i++) if (g->op[i].present && g->op[i].state != OS_DOWN) ops_up++;
    if (alive_en == 0 && captive > 0 && !g->area_clear) {
        g->area_clear = 1;
        radio(g, -1, "AREA CLEAR - SECURE THE HOSTAGES", SFX_SQUELCH);
    }
    if (alive_en > 0 && alive_en <= 3 && captive == 0 && g->last_ping == 0) {
        g->last_ping = 1;
        for (int i = 0; i < g->nen; i++) if (g->en[i].alive) g->en[i].mark = 999;
        radio(g, -1, "HOSTAGES SAFE - CLEARING THE LAST HOSTILES", SFX_SQUELCH);
    }
    if (g->result == RES_NONE) {
        if (ops_up == 0) { g->result = RES_FAIL_SQUAD; g->end_t = 0; radio(g, -1, "SQUAD DOWN - MISSION FAILED", SFX_V_FAILED); }
        else if (alive_en == 0 && captive == 0) {
            g->result = RES_WIN; g->end_t = 0;
            radio(g, -1, "MISSION COMPLETE", SFX_V_COMPLETE);
        }
    }
    sim_fx(g, dt);
}

void sim_fx(Game *g, float dt)
{
    /* particles */
    for (int i = 0; i < g->npt; i++) {
        Particle *p = &g->pt[i];
        p->life -= dt;
        if (p->life <= 0) { *p = g->pt[--g->npt]; i--; continue; }
        switch (p->type) {
        case PT_SMOKE: case PT_DUST:
            p->x += p->vx * dt; p->y += p->vy * dt;
            p->vx *= expf(-dt * 1.5f); p->vy *= expf(-dt * 1.5f);
            p->size += dt * 3.0f;
            break;
        case PT_SPLASH:
            break;
        default: {
            float nx = p->x + p->vx * dt, ny = p->y + p->vy * dt;
            int tx = (int)floorf(nx), ty = (int)floorf(ny);
            if (opaque(g, tx, ty) && g->tile[ty][tx].kind != K_WINDOW) {
                if ((int)floorf(p->x) != tx) p->vx = -p->vx * 0.4f;
                if ((int)floorf(p->y) != ty) p->vy = -p->vy * 0.4f;
            } else { p->x = nx; p->y = ny; }
            p->vz -= 14.0f * dt;
            p->z += p->vz * dt;
            if (p->z <= 0) {
                p->z = 0;
                if (p->vz < -2.0f && (p->type == PT_CASING) && rnd(g) < 0.3f) sfx(g, SFX_CASING, 0.9f + rnd(g) * 0.3f, p->x, 0.25f);
                p->vz = -p->vz * 0.3f;
                p->vx *= 0.6f; p->vy *= 0.6f;
                if (p->type == PT_CASING && p->life < 0.15f) { decal(g, DC_CASING, p->x, p->y, 0.05f, p->c); p->life = 0; }
                if (p->type == PT_BLOOD) { decal(g, DC_BLOOD, p->x, p->y, 0.1f + rnd(g) * 0.08f, 0x6A1010FFu); p->life = 0; }
                if (p->type == PT_GLASS && p->life < 0.3f) { decal(g, DC_GLASS, p->x, p->y, 0.08f, 0xBFE8FF90u); p->life = 0; }
            }
            p->rot += p->vr * dt;
            break;
        }
        }
    }
    for (int i = 0; i < MAX_TR; i++) if (g->tr[i].life > 0) g->tr[i].life -= dt;
    for (int i = 0; i < MAX_DL; i++) if (g->dl[i].life > 0) g->dl[i].life -= dt;
    for (int i = 0; i < MAX_SMOKE; i++) {
        Smoke *s = &g->sm[i];
        if (s->life <= 0) continue;
        s->life -= dt;
        if (rnd(g) < dt * 14.0f && s->life > 1.0f) {
            float a = rnd(g) * TAU_F, r = rnd(g) * s->r * 0.8f;
            spawn(g, PT_SMOKE, s->x + cosf(a) * r, s->y + sinf(a) * r, 0, rnds(g) * 0.3f, rnds(g) * 0.3f, 0, 2.5f + rnd(g), 14.0f + rnd(g) * 10.0f, 0xB4B0AAFFu);
        }
    }
    /* doors swing open */
    for (int y = 0; y < g->H; y++)
        for (int x = 0; x < g->W; x++) {
            Tile *t = &g->tile[y][x];
            if (t->kind == K_DOOR && t->open && t->swing < 1.0f) { t->swing += dt * 4.0f; if (t->swing > 1.0f) t->swing = 1.0f; }
        }
    if (g->shake > 0) { g->shake -= dt * 30.0f; if (g->shake < 0) g->shake = 0; }
    if (g->flash_t > 0) g->flash_t -= dt;
}

/* used by the direct-control abilities in game.c */
void sim_use_ability(Game *g, int i) { use_wp_ability(g, i); }
void sim_detonate(Game *g, int c) { detonate(g, c); }
