/*
 * GO CODE - internal game state shared by game.c (flow + planning), sim.c (the assault),
 * level.c (maps, rooms, paths, sight lines), draw.c (the world) and hud.c (menus + HUD).
 */
#ifndef GAME_INT_H
#define GAME_INT_H

#include <stdint.h>
#include "game.h"
#include "gfx.h"
#include "music.h"

#define PI_F  3.14159265f
#define TAU_F 6.28318531f

/* ------------------------------------------------------------------ */
/* map                                                                  */
/* ------------------------------------------------------------------ */
#define MW 36                    /* max map size in tiles */
#define MH 20
#define TS 24.0f                 /* world pixels per tile */
#define AP 2.0f                  /* one art pixel, in world pixels (a tile is 12 art pixels) */

enum { K_FLOOR, K_WALL, K_SOFT, K_WINDOW, K_DOOR, K_RDOOR, K_RUBBLE };          /* tile kinds */
enum { M_GRASS, M_PAVE, M_WOOD, M_CARPET, M_TILE, M_CONCRETE, M_MARBLE, M_DANCE, M_NMAT };   /* floor materials */
enum { F_NONE, F_TABLE, F_SOFA, F_DESK, F_BED, F_CRATE, F_COUNTER, F_PLANT, F_SHELF, F_CAR, F_NFURN };
enum { LT_NONE, LT_LAMP, LT_FLUO, LT_NEON, LT_STREET };

/* tile flags */
#define TF_OUT      1            /* outdoors: rain falls here */
#define TF_SEEN     2            /* explored (an operator has seen it, or the drone has) */
#define TF_VIS      4            /* visible to an operator this frame */
#define TF_PLANBR   8            /* a breach is planned here: paths may go through */
#define TF_BROKEN   16           /* window glass broken */

typedef struct {
    uint8_t kind, mat, furn, light;
    uint8_t flags, room, open, var;     /* open: door swing 0..255 (doors), var: random per tile */
    float   swing;                      /* door: current swing 0..1 (animated), sign gives the side */
} Tile;

#define MAX_ROOMS 40
#define MAX_LIGHTS 48

typedef struct {
    float x, y;                          /* world px */
    float r, g, b, rad;                  /* colour, radius in px */
    int   type;                          /* LT_* */
    float flick;                         /* 0 steady, else flicker strength */
    int   n0, n1;                        /* range in the static light-weight list */
    int   on;
} SLight;

/* ------------------------------------------------------------------ */
/* actors                                                               */
/* ------------------------------------------------------------------ */
enum { OP_VEGA, OP_KNOX, OP_PIKE, OP_MAE, NOPS };
enum { AB_NONE, AB_FLASH, AB_BREACH, AB_DRONE, AB_SMOKE };
enum { WP_RIFLE, WP_SHOTGUN, WP_SMG, WP_PISTOL, WP_E_PISTOL, WP_E_RIFLE, WP_E_SHOTGUN, NWEAP };

#define MAX_WP 14
#define MAX_PATH 160

typedef struct {
    int8_t  tx, ty;
    uint8_t hold;                        /* wait here for the GO code */
    uint8_t ab;                          /* use the ability here */
    float   ax, ay;                      /* ability target (tile coords) */
} Waypoint;

enum { OS_IDLE, OS_MOVE, OS_HOLD, OS_ACTION, OS_DELAY, OS_BLOCKED, OS_DOWN, OS_REVIVE };

typedef struct {
    int      present;
    float    x, y;                       /* tile coords (centre of tile = +0.5) */
    float    face;                       /* body facing (radians) */
    float    hp;
    int      state;
    int      manual;                     /* the player is steering this one */
    Waypoint wp[MAX_WP];
    int      nwp, cur;                   /* plan, next waypoint to reach */
    uint8_t  path[MAX_PATH][2];
    int      plen, pidx;
    int      target;                     /* enemy index or -1 */
    float    react, cool, burst_cool;
    int      burst;
    float    act_t;                      /* ability / delay timer */
    int      act_phase;
    int      charges;
    float    blind;
    int      revive_tgt; float revive_t;
    float    walk, recoil, muzzle;
    float    hurt;                       /* red flash when hit */
    float    say_cool;
    int      arrived_say;
    float    stuck;
    int      kills, shots, hits;
} Op;

enum { EN_THUG, EN_GUNMAN, EN_HEAVY, NETYPE };
enum { ES_IDLE, ES_WANDER, ES_ALERT, ES_COMBAT, ES_SEARCH, ES_DEAD };

#define MAX_EN 24
#define MAX_HS 4

typedef struct {
    int   type, state, alive;
    float x, y, face, want_face;
    float hp;
    float hx, hy, hface;                 /* home */
    int   guard;                         /* hostage index or -1 */
    float react, cool, burst_cool;
    int   burst, target;
    float stun, blind;
    float lx, ly;                        /* where to look / go */
    float look_t, wander_t;
    uint8_t path[24][2];
    int   plen, pidx;
    int   seen;                          /* visible to the squad this frame */
    int   ghost;                         /* last known position is shown */
    float gx, gy, gface;
    float mark;                          /* revealed by a scan */
    int   torch;                         /* carries a flashlight */
    float walk, recoil, muzzle, hurt, die_t;
    float alerted_t;
} Enemy;

enum { HS_CAPTIVE, HS_SECURED, HS_DEAD };

typedef struct {
    float x, y;
    int   state;
    float exec;                          /* execution countdown left (once the guard is triggered) */
    int   triggered;
    float bob, secured_t;
} Hostage;

/* ------------------------------------------------------------------ */
/* effects                                                              */
/* ------------------------------------------------------------------ */
enum { PT_SPARK, PT_SMOKE, PT_DEBRIS, PT_CASING, PT_DUST, PT_BLOOD, PT_GLASS, PT_SPLASH, PT_EMBER };
#define MAX_PT 700
typedef struct { uint8_t type; float x, y, z, vx, vy, vz, life, max, size, rot, vr; uint32_t c; } Particle;

enum { DC_HOLE, DC_SCORCH, DC_BLOOD, DC_GLASS, DC_SPLINTER, DC_CASING };
#define MAX_DC 400
typedef struct { uint8_t type; float x, y, rot, size; uint32_t c; } Decal;

#define MAX_TR 64
typedef struct { float x0, y0, x1, y1, life; int enemy; } Tracer;

#define MAX_DL 24
typedef struct { float x, y, rad, r, g, b, life, max; } DLight;    /* world px */

#define MAX_SMOKE 6
typedef struct { float x, y, r, life; } Smoke;                     /* tiles */

#define MAX_NADE 6
typedef struct { int type; float x0, y0, x1, y1, t, dur; int owner; } Nade;   /* thrown flash / smoke (tiles) */

#define MAX_CHARGE 4
typedef struct { int tx, ty, live, owner; float t; } Charge;       /* planted breach charge */

#define MAX_SND 48
typedef struct { int id, pitch; float pan, vol; } SndEv;

/* ------------------------------------------------------------------ */
/* missions                                                             */
/* ------------------------------------------------------------------ */
#define NMISSIONS 10

typedef struct {
    const char *name, *place, *brief, *tip;
    int   squad;                         /* bitmask of operators */
    int   theme;                         /* palette / mood */
    float exec_time;                     /* hostage execution countdown */
    float par;                           /* seconds for the third star */
    int   intel;                         /* 1: every hostile starts revealed */
    const char *map[MH + 1];
} Mission;

extern const Mission MISSIONS[NMISSIONS];

typedef struct {
    float amb_r, amb_g, amb_b;           /* indoor ambient */
    float out_r, out_g, out_b;           /* outdoor ambient (moon) */
    int   rain, lightning;
    uint32_t wall_out, wall_in, wall_top;
} Theme;
extern const Theme THEMES[];

/* ------------------------------------------------------------------ */
/* save                                                                 */
/* ------------------------------------------------------------------ */
#define SAVE_MAGIC 0x474F4344u           /* "GOCD" */
typedef struct {
    uint32_t magic;
    uint8_t  ver, unlocked, music, sfx;
    uint8_t  stars[16];
    uint16_t best[16];                   /* best time, seconds (0 = none) */
    uint8_t  helped, last;
    uint8_t  pad[6];
} SaveData;

/* ------------------------------------------------------------------ */
/* the game                                                             */
/* ------------------------------------------------------------------ */
enum { SC_TITLE, SC_SELECT, SC_BRIEF, SC_PLAN, SC_EXEC, SC_DEBRIEF };
enum { RES_NONE, RES_WIN, RES_FAIL_HOSTAGE, RES_FAIL_SQUAD };

#define LG_MAX ((MW * 2 + 1) * (MH * 2 + 1))
#define MAX_LW 30000                     /* static light weights */

struct Game {
    int   scene;
    float t, scene_t;
    uint32_t rng;

    /* title / menus */
    int   menu_sel, sel_mission, pause, pause_sel, confirm;
    int   storage_ok, save_dirty, save_fail;
    float saved_t;
    SaveData sv;
    int   help_page;                     /* >0: help overlay shown */

    /* map */
    int   mission, W, H;
    Tile  tile[MH][MW];
    float ox, oy;                        /* unused (world origin is 0,0) */
    int   nrooms;
    int   room_seen[MAX_ROOMS];
    SLight light[MAX_LIGHTS];
    int   nlight;
    uint16_t lw_idx[MAX_LW];
    float lw_w[MAX_LW];
    int   nlw, lights_dirty;
    float lgr[LG_MAX], lgg[LG_MAX], lgb[LG_MAX];   /* light grid, half-tile resolution */
    int   lgw, lgh;
    const Theme *th;

    /* actors */
    Op      op[NOPS];
    Enemy   en[MAX_EN];
    int     nen;
    Hostage hs[MAX_HS];
    int     nhs;
    int     starts[NOPS][2];

    /* planning / execution */
    int   sel;                           /* selected operator */
    float cx, cy;                        /* cursor (tile coords) */
    float rep_t;
    int   tac;                           /* execution paused for replanning */
    float mtime;
    int   alarm;
    float alarm_t;                       /* how long the alarm has been up */
    float go_t;                          /* time since the last GO */
    int   gos;
    int   all_ready;
    float ready_t;
    float combat_t;                      /* time since the last shot */
    float intensity;                     /* 0..1 how hot the fight is */
    int   result;
    float end_t;
    int   stars, downed_any, last_ping, area_clear;
    float vis_t;
    int   kills, shots_fired, shots_hit;

    /* the plan last used, for a retry */
    Waypoint last_wp[NOPS][MAX_WP];
    int   last_nwp[NOPS], last_plan_mission;

    /* camera */
    float camx, camy, zoom, shake, shake_x, shake_y;
    int   cam_mode;                      /* 0 squad, 1 follow selected */
    float flash_t;                       /* white screen flash */
    float lightning, next_lightning, thunder_t;

    /* radio subtitle */
    char  radio[48];
    float radio_t;
    int   radio_op;

    /* effects */
    Particle pt[MAX_PT]; int npt;
    Decal    dc[MAX_DC]; int ndc, dc_head;
    Tracer   tr[MAX_TR];
    DLight   dl[MAX_DL];
    Smoke    sm[MAX_SMOKE];
    Nade     nd[MAX_NADE];
    Charge   ch[MAX_CHARGE];

    /* audio */
    SndEv snd[MAX_SND];
    int   snd_head, snd_tail;
    float mus[MUS_TRACKS], mus_target[MUS_TRACKS];
    float riser_t;
};

/* ---- shared helpers (game.c) ---- */
float rnd(Game *g);                                  /* 0..1 */
float rnds(Game *g);                                 /* -1..1 */
void  sfx(Game *g, int id, float pitch, float x, float vol);   /* x: tile x for panning (-1 = centre) */
void  radio(Game *g, int op, const char *text, int voice);
void  save_now(Game *g);

/* ---- level.c ---- */
void  level_load(Game *g, int mission);
int   in_map(const Game *g, int x, int y);
int   passable(const Game *g, int x, int y);          /* walkable now */
int   plan_passable(const Game *g, int x, int y);     /* walkable once planned breaches are done */
int   opaque(const Game *g, int x, int y);
int   breachable(const Game *g, int x, int y);
int   los(const Game *g, float x0, float y0, float x1, float y1, int smoke);   /* tile coords */
float ray_len(const Game *g, float x0, float y0, float a, float maxd);        /* tiles to the first wall */
int   find_path(const Game *g, int sx, int sy, int tx, int ty, int planning, uint8_t (*out)[2], int max);
int   room_at(const Game *g, float x, float y);
void  lights_rebuild(Game *g);
void  lights_frame(Game *g);
void  update_vis(Game *g);
void  open_door(Game *g, int x, int y, float fromx, float fromy);
void  break_tile(Game *g, int x, int y);
int   door_dir(const Game *g, int x, int y);         /* 0: door spans x (passage is vertical), 1: spans y */

/* ---- sim.c ---- */
void  sim_start(Game *g);
void  sim_update(Game *g, const Input *in, float dt);
void  sim_fx(Game *g, float dt);
void  go_code(Game *g);
int   ability_of(int op);
const char *op_name(int op);
const char *op_role(int op);
uint32_t op_color(int op);
void  plan_reroute(Game *g, int i);
void  add_light(Game *g, float x, float y, float rad, float r, float gg, float b, float life);
void  spawn(Game *g, int type, float x, float y, float z, float vx, float vy, float vz, float life, float size, uint32_t c);
void  decal(Game *g, int type, float x, float y, float size, uint32_t c);
void  noise_at(Game *g, float x, float y, float rad, int alarm);
void  sim_use_ability(Game *g, int i);
void  sim_detonate(Game *g, int c);

/* ---- draw.c / hud.c ---- */
void  draw_world(const Game *g);
void  draw_hud(const Game *g);
void  draw_menus(const Game *g);
void  world_to_screen(const Game *g, float wx, float wy, float *sx, float *sy);

#endif
