/*
 * GO CODE - pixel-art sprites, baked at start-up from simple shapes into rows of runs.
 *
 * Sprites face +x (right). Each run of same-coloured pixels becomes one quad, so a sprite can
 * be drawn at any angle and scale: crisp pixel art that still turns smoothly.
 */
#ifndef SPRITES_H
#define SPRITES_H

#include <stdint.h>

enum {
    SP_OP,            /* operator body (team colour slot) */
    SP_OP_DOWN,
    SP_THUG, SP_GUNMAN, SP_HEAVY,
    SP_EN_DEAD,
    SP_HOSTAGE, SP_HOSTAGE_FREE, SP_HOSTAGE_DEAD,
    SP_RIFLE, SP_SHOTGUN, SP_SMG, SP_PISTOL, SP_E_RIFLE,
    SP_FOOT,
    SP_COUNT
};

/* palette slots: 0 = empty; TEAM is replaced by the operator's colour when drawn */
#define PAL_TEAM 15

void sprites_init(void);
/* draw sprite s centred on (x, y) (world px), rotated by a, one art pixel = px world px.
 * team: colour for the team slot. tint: multiplied in (0xFFFFFFFF = none). */
void sprite_draw(int s, float x, float y, float a, float px, uint32_t team, uint32_t tint);

#endif
