/*
 * GO CODE - procedurally composed and synthesized music.
 *
 *   MUS_MENU     brooding title theme: low pads, a lonely piano, rain-soaked pulse
 *   MUS_BED      mission layer 1, tension bed: sub drone, dark pads, a ticking clock
 *   MUS_PRESS    mission layer 2, pressure: pulsing 16th bass synth, string tremolo, heartbeat
 *   MUS_ASSAULT  mission layer 3, the breach: full drums, driving bass riff, brass stabs, arps
 *
 * The three mission layers are the same length and tempo and play in sync from the start: the game
 * fades them in and out, so the music ramps up as the squad moves in, and slams in on the GO code.
 */
#ifndef MUSIC_H
#define MUSIC_H

#include <stdint.h>

#define MUS_RATE   22050
enum { MUS_MENU, MUS_BED, MUS_PRESS, MUS_ASSAULT, MUS_TRACKS };

void           music_init(void);            /* compose + render every track (call once) */
const int16_t *music_data(int track);
int            music_len(int track);        /* samples, a multiple of 16 */
int            music_volume(int track);     /* playback volume at full level, 0..255 */

#endif
