/*
 * GO CODE - procedurally synthesized sound effects and radio voices.
 *
 * No audio files: every effect is generated at start-up from oscillators, filtered noise and a
 * small formant speech synthesizer (squad radio callouts, band-limited and crackly like a radio).
 * Samples are 16-bit signed mono at SFX_RATE Hz in the host's byte order (big-endian on the Wii),
 * packed into one 32-byte aligned pool.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#define SFX_RATE 32000

enum {
    /* weapons */
    SFX_RIFLE, SFX_SHOTGUN, SFX_SMG, SFX_PISTOL, SFX_E_PISTOL, SFX_E_RIFLE, SFX_E_SHOTGUN,
    SFX_CASING, SFX_RICOCHET, SFX_IMPACT, SFX_HITFLESH, SFX_BODYFALL, SFX_RELOAD, SFX_WHIZ,
    /* gadgets */
    SFX_THROW, SFX_FLASHBANG, SFX_PLANT, SFX_BREACH, SFX_SMOKE, SFX_DRONE, SFX_SCAN,
    /* world */
    SFX_DOOR, SFX_GLASS, SFX_THUNDER, SFX_STEP,
    /* interface */
    SFX_MOVE, SFX_SELECT, SFX_WAYPOINT, SFX_UNDO, SFX_HOLD, SFX_ERROR, SFX_CONFIRM, SFX_SQUELCH,
    SFX_GO, SFX_RISER, SFX_WIN, SFX_LOSE, SFX_PAUSE, SFX_ALERT, SFX_TICK, SFX_STAR, SFX_SECURE, SFX_MENU,
    /* radio voices: _M male, _F female */
    SFX_V_GO, SFX_V_BREACH, SFX_V_FLASH, SFX_V_SMOKE, SFX_V_SCAN,
    SFX_V_CONTACT_M, SFX_V_CONTACT_F, SFX_V_TANGO_M, SFX_V_TANGO_F, SFX_V_CLEAR_M, SFX_V_CLEAR_F,
    SFX_V_HOSTAGE_M, SFX_V_HOSTAGE_F, SFX_V_MANDOWN_M, SFX_V_MANDOWN_F, SFX_V_READY_M, SFX_V_READY_F,
    SFX_V_COMPLETE, SFX_V_HEY, SFX_V_GETEM, SFX_V_FAILED,
    SFX_COUNT
};

void           audio_init(void);               /* synthesize every effect (call once) */
const int16_t *audio_data(int id);             /* 32-byte aligned sample buffer */
int            audio_len(int id);              /* length in samples (multiple of 16) */
int            audio_volume(int id);           /* suggested playback volume 0..255 */
const int16_t *audio_pool(void);               /* the whole pool (for cache flushing) */
int            audio_pool_len(void);           /* samples */

#endif
