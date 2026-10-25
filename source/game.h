/*
 * GO CODE - a pixel-art squad tactics shooter for the Wii.
 *
 * Plan the assault on a top-down floor plan, stack your operators on the doors, then call the GO code
 * and watch the breach. The game code is plain C99 and knows nothing about the console: it takes a
 * BTN_* bitmask plus two analog sticks, draws into the gfx triangle list, and emits sound events and
 * music-layer volumes that the platform layer (main.c) plays. Progress is a small blob the platform
 * layer saves to and loads from the SD card.
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include "audio.h"

/* ---- input ---- */
enum {
    BTN_UP    = 1 << 0,
    BTN_DOWN  = 1 << 1,
    BTN_LEFT  = 1 << 2,
    BTN_RIGHT = 1 << 3,
    BTN_A     = 1 << 4,
    BTN_B     = 1 << 5,
    BTN_X     = 1 << 6,
    BTN_Y     = 1 << 7,
    BTN_ZL    = 1 << 8,     /* ZL: previous operator        */
    BTN_ZR    = 1 << 9,     /* ZR: next operator            */
    BTN_PLUS  = 1 << 10,    /* menu                         */
    BTN_L     = 1 << 11,    /* L: camera                    */
    BTN_R     = 1 << 12     /* R: tactical pause / replan   */
};

typedef struct {
    uint32_t held, pressed;
    float    sx, sy;        /* left stick, -1..1 (y down is +) */
    float    rx, ry;        /* right stick */
} Input;

typedef struct Game Game;

extern const int GAME_SIZE;

void game_init(Game *g);                         /* once: also synthesizes the sound and music */
void game_update(Game *g, const Input *in, float dt);
void game_render(const Game *g);                 /* fills the gfx triangle list */
/* pops the next sound event, or -1. pitch: 128 = normal. pan: -1 left .. 1 right. vol: 0..1 */
int  game_pop_sound(Game *g, int *pitch, float *pan, float *vol);
/* volume (0..1) wanted for each music track right now (music.h). The three mission layers run in sync. */
void game_music(const Game *g, float vol[]);

/* ---- saving ---- */
#define SAVE_MAX 256
/* tell the game whether the platform could open storage (shown on the title screen) */
void game_storage(Game *g, int ok);
/* load a save blob found at start-up */
void game_load(Game *g, const void *buf, int len);
/* if the game wants to save, copies the blob to buf and returns its length; otherwise 0 */
int  game_save_request(Game *g, void *buf, int max);
/* the platform reports how the last save went (1 ok, 0 failed) */
void game_save_result(Game *g, int ok);

#endif
