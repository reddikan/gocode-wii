/*
 * GO CODE - screen-space interface: the squad cards, objectives, radio subtitles, control hints,
 * menus, briefing, debrief and the help pages.
 */
#include "game_int.h"
#include "sprites.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include "fastmath.h"

#define WHITE 0xFFFFFFFFu
#define DIM   0x9AA4B4FFu
#define PANEL 0x0C0F16D8u
#define ACC   0xFFC93AFFu

static void panel(float x, float y, float w, float h, uint32_t c)
{
    gfx_rect(x, y, w, h, c);
    gfx_rect(x, y, w, 1, 0xFFFFFF18u);
    gfx_rect(x, y + h - 1, w, 1, 0x00000060u);
}

static void bar(float x, float y, float w, float h, float k, uint32_t c)
{
    if (k < 0) k = 0; if (k > 1) k = 1;
    gfx_rect(x, y, w, h, 0x00000090u);
    gfx_rect(x, y, w * k, h, c);
    gfx_rect(x, y, w * k, 1, col_a(0xFFFFFFFFu, 0.3f));
}

static void time_str(char *b, int n, float t)
{
    int s = (int)t;
    snprintf(b, (size_t)n, "%d:%02d", s / 60, s % 60);
}

static const char *state_text(const Game *g, const Op *o)
{
    if (o->state == OS_DOWN) return "DOWN";
    if (o->manual) return "MANUAL";
    if (o->target >= 0) return "ENGAGING";
    switch (o->state) {
    case OS_MOVE: return o->act_t > 0 ? "READY" : "MOVING";
    case OS_HOLD: return "HOLDING";
    case OS_ACTION: case OS_DELAY: return g->go_t < 2.0f ? "GO!" : "ACTING";
    case OS_BLOCKED: return "WAIT BREACH";
    case OS_REVIVE: return "REVIVING";
    case OS_REVIVE + 100: return "TO TEAMMATE";
    default: return (o->cur >= o->nwp && o->nwp > 0) ? "IN PLACE" : "STANDBY";
    }
}

static const char *ab_name(int ab)
{
    switch (ab) { case AB_FLASH: return "FLASH"; case AB_BREACH: return "BREACH"; case AB_DRONE: return "DRONE"; case AB_SMOKE: return "SMOKE"; }
    return "";
}

static void squad_cards(const Game *g)
{
    int n = 0;
    for (int i = 0; i < NOPS; i++) if (g->op[i].present) n++;
    float cw = 150, ch = 40, gap = 6;
    float x = VW * 0.5f - ((float)n * cw + (float)(n - 1) * gap) * 0.5f, y = VH - ch - 6;
    for (int i = 0; i < NOPS; i++) {
        const Op *o = &g->op[i];
        if (!o->present) continue;
        uint32_t c = op_color(i);
        int sel = i == g->sel;
        panel(x, y, cw, ch, sel ? 0x182030F0u : PANEL);
        gfx_rect(x, y, 3, ch, c);
        if (sel) { gfx_rect(x, y - 2, cw, 2, c); }
        /* portrait */
        gfx_rect(x + 7, y + 5, 30, 30, 0x00000080u);
        sprite_draw(o->state == OS_DOWN ? SP_OP_DOWN : SP_OP, x + 22, y + 21, -PI_F * 0.5f, 1.7f, c, o->state == OS_DOWN ? 0x808080FFu : WHITE);
        gfx_text(op_name(i), x + 42, y + 5, 1.4f, c);
        gfx_text(op_role(i), x + 42 + gfx_text_width(op_name(i), 1.4f) + 6, y + 6.5f, 0.9f, DIM);
        bar(x + 42, y + 17, cw - 50, 4, o->hp / 100.0f, o->hp > 50 ? 0x60D070FFu : (o->hp > 25 ? 0xE0C040FFu : 0xE04030FFu));
        /* ability charges */
        const char *an = ab_name(ability_of(i));
        gfx_text(an, x + 42, y + 26, 0.9f, DIM);
        float ax = x + 42 + gfx_text_width(an, 0.9f) + 4;
        for (int k = 0; k < o->charges && k < 4; k++) gfx_rect(ax + (float)k * 6, y + 26, 4, 6, ACC);
        gfx_text_r(state_text(g, o), x + cw - 6, y + 26, 0.9f, o->state == OS_DOWN ? 0xFF5040FFu : (o->target >= 0 ? 0xFF8A50FFu : WHITE));
        x += cw + gap;
    }
}

static void top_bar(const Game *g)
{
    const Mission *ms = &MISSIONS[g->mission];
    panel(0, 0, VW, 22, PANEL);
    char b[64];
    snprintf(b, sizeof b, "%d  %s", g->mission + 1, ms->name);
    gfx_text(b, 10, 6, 1.5f, WHITE);
    int sec = 0, alive = 0, known = 0;
    for (int h = 0; h < g->nhs; h++) if (g->hs[h].state == HS_SECURED) sec++;
    for (int i = 0; i < g->nen; i++) if (g->en[i].alive) { alive++; if (g->en[i].seen || g->en[i].mark > 0 || g->en[i].ghost) known++; }
    snprintf(b, sizeof b, "HOSTAGES %d/%d", sec, g->nhs);
    gfx_text(b, VW * 0.5f - 120, 7, 1.2f, sec == g->nhs ? 0x60FF90FFu : 0xF0F4FFFFu);
    snprintf(b, sizeof b, "HOSTILES %d", alive);
    gfx_text(b, VW * 0.5f + 20, 7, 1.2f, 0xFF7A60FFu);
    if (g->scene == SC_EXEC) {
        time_str(b, sizeof b, g->mtime);
        gfx_text_r(b, VW - 10, 6, 1.5f, WHITE);
        if (g->alarm) {
            float p = 0.5f + 0.5f * sinf(g->t * 8.0f);
            gfx_text_r("ALARM", VW - 70, 7, 1.2f, col_a(0xFF4030FFu, 0.5f + 0.5f * p));
        }
    } else {
        gfx_text_r("PLANNING", VW - 10, 7, 1.2f, ACC);
    }
}

static void hint_line(const Game *g)
{
    const char *h;
    if (g->scene == SC_PLAN)
        h = "A WAYPOINT   B UNDO   Y WAIT FOR GO   X ABILITY   ZL/ZR OPERATOR   + START";
    else if (g->tac)
        h = "REPLAN:  A WAYPOINT   B UNDO   Y WAIT FOR GO   X ABILITY   R RESUME";
    else
        h = "X GO CODE   ZL/ZR SELECT   STICK TAKE CONTROL   A BACK TO PLAN   Y ABILITY   R REPLAN   L CAMERA";
    gfx_text_sc(h, VW * 0.5f, VH - 58, 0.95f, 0xC8D0DCC0u);
}

static void radio_line(const Game *g)
{
    if (g->radio_t <= 0) return;
    float a = g->radio_t > 0.4f ? 1.0f : g->radio_t / 0.4f;
    uint32_t c = g->radio_op >= 0 ? op_color(g->radio_op) : (g->radio_op == -2 ? 0xFF7050FFu : ACC);
    float w = gfx_text_width(g->radio, 1.4f) + 30;
    float x = VW * 0.5f - w * 0.5f, y = 30;
    gfx_rect(x, y, w, 18, col_a(0x0A0C12E0u, a));
    gfx_rect(x, y, 3, 18, col_a(c, a));
    /* a little signal meter */
    for (int k = 0; k < 3; k++) gfx_rect(x + 8 + (float)k * 3, y + 12 - (float)k * 3, 2, 3 + (float)k * 3, col_a(c, a * (0.5f + 0.5f * sinf(g->t * 20.0f + (float)k))));
    gfx_text(g->radio, x + 22, y + 5, 1.4f, col_a(WHITE, a));
}

static void banner(const Game *g)
{
    if (g->scene != SC_EXEC || g->tac || g->result != RES_NONE) return;
    if (g->all_ready) {
        float p = 0.6f + 0.4f * sinf(g->t * 7.0f);
        const char *s = "ALL IN POSITION - PRESS X FOR GO";
        float w = gfx_text_width(s, 2.0f) + 30;
        gfx_rect(VW * 0.5f - w * 0.5f, 56, w, 24, col_a(0x1A6A2AE0u, p));
        gfx_text_sc(s, VW * 0.5f, 62, 2.0f, WHITE);
    }
    if (g->go_t < 1.2f) {
        float k = g->go_t / 1.2f;
        gfx_text_sc("GO", VW * 0.5f, VH * 0.36f - k * 10, 6.0f + k * 3.0f, col_a(0x60FF90FFu, 1.0f - k));
    }
}

void draw_hud(const Game *g)
{
    gfx_view_reset();
    gfx_blend(BL_ALPHA);
    if (g->scene == SC_PLAN || g->scene == SC_EXEC) {
        top_bar(g);
        squad_cards(g);
        hint_line(g);
        radio_line(g);
        banner(g);
        if (g->tac) {
            gfx_rect(0, 22, VW, 3, 0xFFC93A80u);
            gfx_text_sc("TACTICAL PAUSE - CHANGE THE PLAN, R TO RESUME", VW * 0.5f, 58, 1.4f, ACC);
        }
        if (g->scene == SC_PLAN && g->scene_t < 4.0f) {
            float a = g->scene_t < 3.0f ? 1.0f : 4.0f - g->scene_t;
            gfx_text_sc("PLAN THE ASSAULT", VW * 0.5f, 60, 2.0f, col_a(ACC, a));
        }
        if (g->result != RES_NONE) {
            float a = g->end_t < 0.5f ? g->end_t / 0.5f : 1.0f;
            gfx_rect(0, VH * 0.4f - 30, VW, 60, col_a(0x000000C0u, a));
            gfx_text_sc(g->result == RES_WIN ? "MISSION COMPLETE" : "MISSION FAILED", VW * 0.5f, VH * 0.4f - 12, 3.4f,
                        col_a(g->result == RES_WIN ? 0x60FF90FFu : 0xFF5040FFu, a));
        }
    }
}

/* ------------------------------------------------------------------ */
/* menus                                                                */
/* ------------------------------------------------------------------ */
static void menu_items(const Game *g, const char **items, int n, int sel, float x, float y, float sz, int centred)
{
    for (int i = 0; i < n; i++) {
        uint32_t c = i == sel ? ACC : 0xC0C8D4FFu;
        float yy = y + (float)i * sz * 10.5f;
        if (centred) {
            if (i == sel) {
                float w = gfx_text_width(items[i], sz);
                gfx_rect(x - w * 0.5f - 24, yy - 4, w + 48, sz * 7 + 8, 0xFFC93A20u);
                gfx_text(">", x - w * 0.5f - 20 + sinf(g->t * 6) * 2, yy, sz, ACC);
            }
            gfx_text_sc(items[i], x, yy, sz, c);
        } else {
            if (i == sel) { gfx_rect(x - 10, yy - 4, 300, sz * 7 + 8, 0xFFC93A20u); gfx_text(">", x - 8 + sinf(g->t * 6) * 2, yy, sz, ACC); }
            gfx_text_s(items[i], x + 8, yy, sz, c);
        }
    }
}

static void vignette(float a)
{
    gfx_rect(0, 0, VW, VH, col_a(0x05070CFFu, a));
}

static void wrap_text(const char *s, float x, float y, float w, float sz, uint32_t c)
{
    char line[128];
    int n = 0;
    float yy = y;
    const char *p = s;
    while (*p) {
        const char *q = p;
        while (*q && *q != ' ') q++;
        int wl = (int)(q - p);
        char test[160];
        memcpy(test, line, (size_t)n); test[n] = 0;
        if (n) strcat(test, " ");
        strncat(test, p, (size_t)wl);
        if (gfx_text_width(test, sz) > w && n) {
            line[n] = 0;
            gfx_text_s(line, x, yy, sz, c);
            yy += sz * 10;
            n = 0;
            memcpy(line, p, (size_t)wl); n = wl;
        } else {
            if (n) line[n++] = ' ';
            memcpy(line + n, p, (size_t)wl); n += wl;
        }
        p = q;
        while (*p == ' ') p++;
    }
    if (n) { line[n] = 0; gfx_text_s(line, x, yy, sz, c); }
}

static void stars(float x, float y, int n, int of, float s)
{
    for (int i = 0; i < of; i++) {
        float cx = x + (float)i * s * 2.4f;
        uint32_t c = i < n ? ACC : 0x404858FFu;
        for (int k = 0; k < 5; k++) {
            float a = -PI_F * 0.5f + (float)k * TAU_F / 5.0f, b = a + TAU_F / 10.0f, b2 = a - TAU_F / 10.0f;
            gfx_tri1(cx, y, cx + cosf(a) * s, y + sinf(a) * s, cx + cosf(b) * s * 0.42f, y + sinf(b) * s * 0.42f, c);
            gfx_tri1(cx, y, cx + cosf(a) * s, y + sinf(a) * s, cx + cosf(b2) * s * 0.42f, y + sinf(b2) * s * 0.42f, c);
        }
    }
}

static void title(const Game *g)
{
    vignette(0.55f);
    float t = g->t;
    /* police lights washing over the scene */
    gfx_blend(BL_ADD);
    float red = fmaxf(0.0f, sinf(t * 7.0f)), blue = fmaxf(0.0f, sinf(t * 7.0f + PI_F));
    gfx_glow(-40, VH * 0.75f, 420, col_a(0xFF2020FFu, 0.16f * red));
    gfx_glow(VW + 40, VH * 0.75f, 420, col_a(0x2040FFFFu, 0.18f * blue));
    gfx_blend(BL_ALPHA);
    /* logo */
    float y = 70;
    gfx_text_sc("GO CODE", VW * 0.5f + 3, y + 3, 9.0f, 0x00000090u);
    gfx_text_top("GO CODE", VW * 0.5f - gfx_text_width("GO CODE", 9.0f) * 0.5f, y, 9.0f, WHITE, 5);
    gfx_text_top("GO CODE", VW * 0.5f - gfx_text_width("GO CODE", 9.0f) * 0.5f, y, 9.0f, ACC, 2);
    gfx_rect(VW * 0.5f - 160, y + 70, 320, 2, ACC);
    gfx_text_sc("SQUAD TACTICS  -  PLAN.  STACK.  BREACH.", VW * 0.5f, y + 80, 1.3f, DIM);
    /* a little squad lineup */
    for (int i = 0; i < NOPS; i++) {
        float sx = VW * 0.5f - 120 + (float)i * 80, sy = y + 128 + sinf(t * 2 + (float)i) * 1.5f;
        gfx_blend(BL_ADD);
        gfx_glow(sx, sy, 26, col_a(op_color(i), 0.25f));
        gfx_blend(BL_ALPHA);
        sprite_draw(SP_RIFLE + i, sx, sy, -PI_F * 0.5f, 3.6f, op_color(i), WHITE);
        gfx_text_sc(op_name(i), sx, sy + 30, 1.2f, op_color(i));
    }
    const char *items[5];
    char m[24], s[24];
    snprintf(m, sizeof m, "MUSIC: %s", g->sv.music ? "ON" : "OFF");
    snprintf(s, sizeof s, "SOUND: %s", g->sv.sfx ? "ON" : "OFF");
    items[0] = g->sv.unlocked > 1 || g->sv.stars[0] ? "CONTINUE" : "START";
    items[1] = "MISSIONS"; items[2] = "HOW TO PLAY"; items[3] = m; items[4] = s;
    menu_items(g, items, 5, g->menu_sel, VW * 0.5f, 268, 2.0f, 1);
    const char *st = g->storage_ok ? (g->save_fail ? "SAVE FAILED - CHECK THE SD CARD" : "PROGRESS SAVES TO THE SD CARD") : "NO SD CARD - PROGRESS WON'T BE SAVED";
    gfx_text_sc(st, VW * 0.5f, VH - 22, 1.0f, g->storage_ok && !g->save_fail ? 0x7A8494FFu : 0xFF8060FFu);
}

static void select_screen(const Game *g)
{
    vignette(0.62f);
    gfx_text_sc("MISSIONS", VW * 0.5f, 22, 3.0f, ACC);
    float x = 120, y = 66;
    int total = 0;
    for (int i = 0; i < NMISSIONS; i++) total += g->sv.stars[i];
    char b[64];
    snprintf(b, sizeof b, "STARS %d/%d", total, NMISSIONS * 3);
    gfx_text_r(b, VW - 30, 30, 1.3f, ACC);
    for (int i = 0; i < NMISSIONS; i++) {
        int locked = i >= g->sv.unlocked;
        float yy = y + (float)i * 36;
        int sel = i == g->sel_mission;
        panel(x, yy, VW - 240, 30, sel ? 0x1E2A3CF0u : 0x0C0F16C0u);
        if (sel) gfx_rect(x, yy, 4, 30, ACC);
        snprintf(b, sizeof b, "%02d", i + 1);
        gfx_text(b, x + 14, yy + 8, 2.0f, locked ? 0x50586AFFu : ACC);
        gfx_text(locked ? "LOCKED" : MISSIONS[i].name, x + 60, yy + 6, 1.6f, locked ? 0x50586AFFu : WHITE);
        if (!locked) gfx_text(MISSIONS[i].place, x + 60, yy + 19, 0.9f, DIM);
        if (!locked) stars(VW - 230, yy + 15, g->sv.stars[i], 3, 7);
        if (!locked && g->sv.best[i]) { time_str(b, sizeof b, (float)g->sv.best[i]); gfx_text_r(b, VW - 140, yy + 10, 1.3f, DIM); }
    }
    gfx_text_sc("A BRIEFING    B BACK", VW * 0.5f, VH - 22, 1.2f, DIM);
}

static void brief_screen(const Game *g)
{
    vignette(0.66f);
    const Mission *ms = &MISSIONS[g->sel_mission];
    float x = 90, y = 40;
    char b[64];
    snprintf(b, sizeof b, "MISSION %d", g->sel_mission + 1);
    gfx_text(b, x, y, 1.5f, ACC);
    gfx_text_s(ms->name, x, y + 16, 4.0f, WHITE);
    gfx_text(ms->place, x, y + 50, 1.3f, DIM);
    gfx_rect(x, y + 66, VW - 2 * x, 2, 0xFFC93A60u);
    wrap_text(ms->brief, x, y + 80, VW - 2 * x, 1.6f, WHITE);
    gfx_text("TIP", x, y + 132, 1.4f, ACC);
    wrap_text(ms->tip, x + 30, y + 132, VW - 2 * x - 30, 1.3f, 0xC8D4E4FFu);
    gfx_text("SQUAD", x, y + 212, 1.4f, ACC);
    float sx = x;
    for (int i = 0; i < NOPS; i++) {
        if (!(ms->squad & (1 << i))) continue;
        panel(sx, y + 230, 150, 72, PANEL);
        gfx_rect(sx, y + 230, 3, 72, op_color(i));
        sprite_draw(SP_RIFLE + i, sx + 28, y + 266, -PI_F * 0.5f, 2.4f, op_color(i), WHITE);
        gfx_text(op_name(i), sx + 52, y + 238, 1.6f, op_color(i));
        gfx_text(op_role(i), sx + 52, y + 254, 1.0f, DIM);
        static const char *AB[NOPS] = { "FLASHBANG", "BREACH CHARGE", "DRONE SCAN", "SMOKE + REVIVE" };
        gfx_text(AB[i], sx + 52, y + 268, 1.0f, WHITE);
        static const char *GUN[NOPS] = { "ASSAULT RIFLE", "SHOTGUN", "SILENCED SMG", "PISTOL" };
        gfx_text(GUN[i], sx + 52, y + 282, 0.9f, DIM);
        sx += 160;
    }
    snprintf(b, sizeof b, "PAR TIME %d:%02d", (int)ms->par / 60, (int)ms->par % 60);
    gfx_text_r(b, VW - x, y + 212, 1.2f, DIM);
    float p = 0.6f + 0.4f * sinf(g->t * 4.0f);
    gfx_text_sc("A  PLAN THE ASSAULT        B  BACK", VW * 0.5f, VH - 24, 1.5f, col_a(ACC, p));
}

static void debrief(const Game *g)
{
    float a = g->scene_t < 0.5f ? g->scene_t / 0.5f : 1.0f;
    vignette(0.7f * a);
    int won = g->result == RES_WIN;
    gfx_text_sc(won ? "MISSION COMPLETE" : "MISSION FAILED", VW * 0.5f, 50, 4.0f, col_a(won ? 0x60FF90FFu : 0xFF5040FFu, a));
    gfx_text_sc(MISSIONS[g->mission].name, VW * 0.5f, 92, 1.6f, col_a(DIM, a));
    char b[64];
    float y = 130;
    if (won) {
        int shown = (int)((g->scene_t - 0.6f) / 0.35f);
        if (shown > g->stars) shown = g->stars;
        if (shown < 0) shown = 0;
        stars(VW * 0.5f - 34, y + 10, shown, 3, 14);
        y += 40;
        const char *why[3] = { "MISSION COMPLETE", "NOBODY WENT DOWN", "UNDER PAR TIME" };
        int got[3] = { 1, !g->downed_any, g->mtime <= MISSIONS[g->mission].par };
        for (int i = 0; i < 3; i++) gfx_text_sc(why[i], VW * 0.5f, y + (float)i * 14, 1.2f, got[i] ? ACC : 0x50586AFFu);
        y += 50;
    } else {
        gfx_text_sc(g->result == RES_FAIL_HOSTAGE ? "A HOSTAGE WAS KILLED" : "THE SQUAD WENT DOWN", VW * 0.5f, y, 1.6f, WHITE);
        gfx_text_sc(g->result == RES_FAIL_HOSTAGE ? "TAKE OUT OR FLASH THE GUARDS BEFORE THEIR TIMERS RUN OUT" : "FLASH ROOMS, AND STACK UP BEFORE YOU GO IN",
                    VW * 0.5f, y + 20, 1.1f, DIM);
        y += 60;
    }
    char t[16];
    time_str(t, sizeof t, g->mtime);
    snprintf(b, sizeof b, "TIME %s   HOSTILES DOWN %d   ACCURACY %d%%", t, g->kills, g->shots_fired ? g->shots_hit * 100 / g->shots_fired : 0);
    gfx_text_sc(b, VW * 0.5f, y, 1.3f, WHITE);
    const char *items[3];
    items[0] = won ? (g->mission + 1 < NMISSIONS ? "NEXT MISSION" : "PLAY AGAIN") : "TRY AGAIN - KEEP THE PLAN";
    items[1] = won ? "REPLAY" : "START OVER";
    items[2] = "MISSIONS";
    if (g->scene_t > 0.8f) menu_items(g, items, 3, g->menu_sel, VW * 0.5f, y + 40, 1.8f, 1);
    if (g->saved_t > 0) gfx_text_r("SAVED", VW - 16, VH - 20, 1.2f, col_a(0x60FF90FFu, g->saved_t > 1 ? 1 : g->saved_t));
}

static void pause_menu(const Game *g)
{
    vignette(0.6f);
    gfx_text_sc(g->scene == SC_PLAN ? "PLANNING" : "PAUSED", VW * 0.5f, 110, 3.0f, ACC);
    const char *items[5];
    char m[24], s[24];
    if (g->scene == SC_PLAN) {
        items[0] = "START THE ASSAULT"; items[1] = "CLEAR THIS OPERATOR'S PLAN"; items[2] = "CLEAR ALL PLANS"; items[3] = "HOW TO PLAY"; items[4] = "ABORT MISSION";
    } else {
        snprintf(m, sizeof m, "MUSIC: %s", g->sv.music ? "ON" : "OFF");
        snprintf(s, sizeof s, "SOUND: %s", g->sv.sfx ? "ON" : "OFF");
        items[0] = "RESUME"; items[1] = "RESTART - KEEP THE PLAN"; items[2] = m; items[3] = s; items[4] = "ABORT MISSION";
    }
    menu_items(g, items, 5, g->pause_sel, VW * 0.5f, 170, 1.9f, 1);
}

static void help(const Game *g)
{
    vignette(0.85f);
    float x = 70, y = 40;
    char b[32];
    snprintf(b, sizeof b, "HOW TO PLAY  %d/3", g->help_page);
    gfx_text(b, x, y, 2.2f, ACC);
    y += 34;
    static const char *P1[] = {
        "1  PLAN",
        "Every mission starts with a floor plan. Pick an operator with ZL / ZR and",
        "lay down waypoints with A - they find their own way between them. B undoes.",
        "",
        "Y on a waypoint: WAIT FOR GO. The operator stops there until you call the",
        "GO code. Stack two operators on two doors and they go in together.",
        "",
        "X on a waypoint: use the ability there (flash or smoke lands on the cursor,",
        "Knox breaches the door or cracked wall next to him). Pike's drone (X) looks",
        "into the room under the cursor right away. + opens the menu to START.",
    };
    static const char *P2[] = {
        "2  GO",
        "Your squad follows the plan and shoots on sight. When everyone is waiting",
        "the music builds - press X to call the GO code: charges blow, flashbangs",
        "pop, and the team storms in.",
        "",
        "Push the left stick to take direct control of the selected operator",
        "(right stick aims, they still shoot by themselves). A hands them back to",
        "the plan. Y uses their ability now. R pauses everything so you can change",
        "the plan. L switches the camera between the squad and the selected one.",
    };
    static const char *P3[] = {
        "3  STAY ALIVE",
        "A guard who sees you, or hears the fight close by, starts counting down",
        "to hurt their hostage. Kill, flash or blow the door on them in time.",
        "",
        "Flashed or stunned hostiles can't shoot. Walls, windows, shelves and",
        "closed doors block sight. Smoke blocks everyone's sight for a while.",
        "Pike's silenced gun doesn't raise the alarm. Mae revives teammates.",
        "",
        "Stars: complete the mission, nobody goes down, beat the par time.",
        "Progress saves to the SD card after every mission.",
    };
    const char **P = g->help_page == 1 ? P1 : (g->help_page == 2 ? P2 : P3);
    int n = g->help_page == 1 ? 10 : (g->help_page == 2 ? 9 : 10);
    for (int i = 0; i < n; i++) gfx_text_s(P[i], x, y + (float)i * 22, i == 0 ? 2.0f : 1.45f, i == 0 ? WHITE : 0xD0D8E4FFu);
    gfx_text_sc("A NEXT    B CLOSE", VW * 0.5f, VH - 26, 1.4f, DIM);
}

void draw_menus(const Game *g)
{
    gfx_view_reset();
    gfx_blend(BL_ALPHA);
    if (g->help_page > 0) { vignette(0.6f); help(g); return; }
    switch (g->scene) {
    case SC_TITLE: title(g); break;
    case SC_SELECT: select_screen(g); break;
    case SC_BRIEF: brief_screen(g); break;
    case SC_DEBRIEF: debrief(g); break;
    default: break;
    }
    if (g->pause) pause_menu(g);
}
