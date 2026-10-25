/*
 * GO CODE - sound effect synthesis. See audio.h.
 *
 * Every effect is built at start-up from a small synth (band-limited and FM oscillators, resonant
 * filtered noise and a short room echo). Gunshots are a sharp crack, a body thump and a room tail;
 * the radio callouts are formant speech squeezed through a radio band-pass with a squelch.
 */
#include "audio.h"
#include "speech.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAXLEN 96000                       /* longest single effect: 3 s at 32 kHz */
#define POOLN  (2600000)                   /* all effects together (~5 MB) */

static int16_t POOL[POOLN] __attribute__((aligned(32)));
static int     pool_used;
static int     sfx_off[SFX_COUNT], sfx_len[SFX_COUNT];
static float   mixbuf[MAXLEN];
static float   outbuf[MAXLEN];
static int     mixend;
static uint32_t rng = 0x2545F491u;
static int     ready;

/* playback volume per effect (every effect is normalised first, then scaled by this) */
static const uint8_t VOL[SFX_COUNT] = {
    /* RIFLE */ 175, /* SHOTGUN */ 210, /* SMG */ 120, /* PISTOL */ 165, /* E_PISTOL */ 150, /* E_RIFLE */ 160, /* E_SHOTGUN */ 190,
    /* CASING */ 70, /* RICOCHET */ 90, /* IMPACT */ 90, /* HITFLESH */ 110, /* BODYFALL */ 150, /* RELOAD */ 110, /* WHIZ */ 90,
    /* THROW */ 120, /* FLASHBANG */ 245, /* PLANT */ 150, /* BREACH */ 255, /* SMOKE */ 170, /* DRONE */ 140, /* SCAN */ 150,
    /* DOOR */ 130, /* GLASS */ 170, /* THUNDER */ 220, /* STEP */ 60,
    /* MOVE */ 55, /* SELECT */ 120, /* WAYPOINT */ 130, /* UNDO */ 110, /* HOLD */ 140, /* ERROR */ 130, /* CONFIRM */ 150, /* SQUELCH */ 90,
    /* GO */ 255, /* RISER */ 170, /* WIN */ 210, /* LOSE */ 200, /* PAUSE */ 110, /* ALERT */ 190, /* TICK */ 120, /* STAR */ 170, /* SECURE */ 180, /* MENU */ 130,
    /* voices */ 230, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 230, 200, 200, 230,
};

enum { W_SIN, W_TRI, W_SOFT, W_FM };           /* oscillator shapes */
enum { F_LP, F_BP, F_HP };                      /* noise filter types */

#define SR ((float)SFX_RATE)
#define TABN 4096
static float sine_tab[TABN + 1];

static inline float sine01(float ph)            /* ph in [0,1) */
{
    float x = ph * (float)TABN;
    int i = (int)x;
    float fr = x - (float)i;
    return sine_tab[i] + (sine_tab[i + 1] - sine_tab[i]) * fr;
}

static float frand(void)                        /* -1 .. 1 */
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float)(rng >> 8) / 8388608.0f - 1.0f;
}

/*
 * One pitched voice mixed into the effect being built.
 *   start, dur : seconds
 *   f0 -> f1   : exponential pitch glide in Hz
 *   atk        : attack time (s);  tau : decay time constant (s), 0 = sustained
 *   W_SIN      : pure sine
 *   W_TRI      : soft triangle (sine + a few odd harmonics)
 *   W_SOFT     : warm band-limited saw; a = extra high-harmonic roll-off (0 bright .. 2 dark)
 *   W_FM       : two-operator FM; a = modulator/carrier ratio, b = peak modulation index (fades with the note)
 */
static void tone(float start, float dur, float f0, float f1, float vol, float atk, float tau, int wave, float a, float b)
{
    int s0 = (int)(start * SR), n = (int)(dur * SR);
    float ph = 0.0f, pm = 0.0f;
    float glide = (f1 == f0) ? 1.0f : powf(f1 / f0, 1.0f / (float)(n > 1 ? n : 1));
    float f = f0, hw[10];
    for (int h = 1; h <= 9; h++) hw[h] = 1.0f / powf((float)h, 1.0f + a);
    float env = 1.0f, k = (tau > 0.0f) ? expf(-1.0f / (tau * SR)) : 1.0f;
    float atk_n = atk * SR > 1.0f ? atk * SR : 1.0f;
    float rel_n = 0.006f * SR;
    for (int i = 0; i < n && s0 + i < MAXLEN; i++) {
        float amp = env;
        if ((float)i < atk_n) amp *= (float)i / atk_n;
        if ((float)(n - i) < rel_n) amp *= (float)(n - i) / rel_n;
        float v;
        switch (wave) {
        case W_TRI:
            v = sine01(ph) + 0.111f * sine01(ph * 3.0f - floorf(ph * 3.0f)) * -1.0f + 0.04f * sine01(ph * 5.0f - floorf(ph * 5.0f));
            break;
        case W_SOFT: {
            v = 0.0f;
            int nh = (int)(0.42f * SR / (f > 1.0f ? f : 1.0f));
            if (nh > 9) nh = 9;
            for (int h = 1; h <= nh; h++) {
                float x = ph * (float)h;
                v += sine01(x - floorf(x)) * hw[h];
            }
            break;
        }
        case W_FM: {
            float idx = b * env;
            float m = sine01(pm);
            float x = ph + idx * m / (2.0f * (float)M_PI) * 2.0f;
            x -= floorf(x);
            v = sine01(x);
            break;
        }
        default:
            v = sine01(ph);
            break;
        }
        mixbuf[s0 + i] += v * amp * vol;
        if (s0 + i + 1 > mixend) mixend = s0 + i + 1;
        ph += f / SR; if (ph >= 1.0f) ph -= 1.0f;
        pm += f * a / SR; pm -= floorf(pm);
        f *= glide;
        env *= k;
    }
}

/* resonant filtered noise: fc0 -> fc1 is the filter cutoff sweep in Hz, q the resonance */
static void noise(float start, float dur, float fc0, float fc1, float q, float vol, float atk, float tau, int mode)
{
    int s0 = (int)(start * SR), n = (int)(dur * SR);
    float z1 = 0.0f, z2 = 0.0f;                  /* transposed direct form II state */
    float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float env = 1.0f, k = (tau > 0.0f) ? expf(-1.0f / (tau * SR)) : 1.0f;
    float atk_n = atk * SR > 1.0f ? atk * SR : 1.0f;
    float rel_n = 0.006f * SR;
    float ratio = (fc1 == fc0) ? 1.0f : powf(fc1 / fc0, 1.0f / (float)(n > 1 ? n : 1));
    float fc = fc0;
    for (int i = 0; i < n && s0 + i < MAXLEN; i++) {
        if ((i & 7) == 0) {                       /* retune the filter every 8 samples */
            float w = 2.0f * (float)M_PI * (fc < 0.45f * SR ? fc : 0.45f * SR) / SR;
            float cw = cosf(w), alpha = sinf(w) / (2.0f * q), a0 = 1.0f + alpha;
            switch (mode) {
            case F_LP: b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; break;
            case F_HP: b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; break;
            default:   b0 = alpha; b1 = 0; b2 = -alpha; break;
            }
            a1 = -2.0f * cw / a0; a2 = (1.0f - alpha) / a0;
            b0 /= a0; b1 /= a0; b2 /= a0;
        }
        float x = frand();
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        float amp = env;
        if ((float)i < atk_n) amp *= (float)i / atk_n;
        if ((float)(n - i) < rel_n) amp *= (float)(n - i) / rel_n;
        mixbuf[s0 + i] += y * amp * vol;
        if (s0 + i + 1 > mixend) mixend = s0 + i + 1;
        env *= k;
        fc *= ratio;
    }
}

static void begin(void)
{
    memset(mixbuf, 0, sizeof mixbuf);
    mixend = 0;
}

/* mix down with a two-tap room echo, normalise, store in the pool as 16-bit */
static void finish_fx(int id, float wet, float room)
{
    int d1 = (int)((0.045f + 0.07f * room) * SR), d2 = d1 * 2 + (int)(0.013f * SR);
    int tail = (wet > 0.0f) ? d2 + (int)(0.05f * SR) : (int)(0.03f * SR);
    int len = mixend + tail;
    if (len > MAXLEN) len = MAXLEN;
    float peak = 0.0f;
    for (int i = 0; i < len; i++) {
        float x = (i < mixend) ? mixbuf[i] : 0.0f;
        if (i >= d1 && i - d1 < mixend) x += wet * 0.6f * mixbuf[i - d1];
        if (i >= d2 && i - d2 < mixend) x += wet * 0.3f * mixbuf[i - d2];
        outbuf[i] = x;
        if (fabsf(x) > peak) peak = fabsf(x);
    }
    int last = len;
    while (last > 1 && fabsf(outbuf[last - 1]) < peak * 0.003f) last--;
    last += (int)(0.02f * SR);
    if (last < len) len = last;
    int fade = (int)(0.02f * SR);
    float scale = peak > 0.0f ? 0.85f * 32000.0f / peak : 0.0f;
    int outlen = (len + 15) & ~15;
    if (pool_used + outlen > POOLN) outlen = (POOLN - pool_used) & ~15;
    if (outlen < 16) { sfx_off[id] = 0; sfx_len[id] = 16; return; }
    int16_t *dst = POOL + pool_used;
    memset(dst, 0, (size_t)outlen * 2);
    for (int i = 0; i < len && i < outlen; i++) {
        float x = outbuf[i];
        if (len - i < fade) x *= (float)(len - i) / (float)fade;
        dst[i] = (int16_t)(x * scale);
    }
    sfx_off[id] = pool_used;
    sfx_len[id] = outlen;
    pool_used += outlen;
}

/* soft saturation for a bit of grit */
static float sat(float x) { return x / (1.0f + fabsf(x)); }

static void bell(float t, float f, float vol, float tau)
{
    tone(t, tau * 5.0f, f, f, vol, 0.002f, tau, W_FM, 3.0f, 1.1f);
    tone(t, tau * 3.0f, f * 2.0f, f * 2.0f, vol * 0.25f, 0.002f, tau * 0.5f, W_SIN, 0, 0);
}

/* a squad member on the radio: formant speech, band-passed to a handset, a little crunch and hiss,
 * and the squelch tail when they let go of the button */
static void radio_voice(int id, const char *phrase, float pitch, float rate, float growl, int handset)
{
    begin();
    int n = speech_render(phrase, pitch, rate, growl, mixbuf + 800, MAXLEN - 6000, SR);
    float peak = 1e-6f;
    for (int i = 0; i < n; i++) if (fabsf(mixbuf[800 + i]) > peak) peak = fabsf(mixbuf[800 + i]);
    float hp1 = 0, hp1p = 0, lp1 = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
        float x = mixbuf[800 + i] / peak;
        if (handset) {
            hp1 = 0.94f * (hp1 + x - hp1p); hp1p = x;           /* ~300 Hz high-pass */
            lp1 += (hp1 - lp1) * 0.42f; lp2 += (lp1 - lp2) * 0.42f;   /* ~3 kHz low-pass */
            x = sat(lp2 * 2.6f) * 0.9f + frand() * 0.035f;
        } else {
            x = sat(x * 1.6f);
        }
        mixbuf[800 + i] = x;
    }
    mixend = 800 + n;
    if (handset) {
        noise(0.0f, 0.025f, 2500, 2500, 1.2f, 0.25f, 0.001f, 0.01f, F_BP);               /* key up */
        float te = (float)(800 + n) / SR;
        noise(te, 0.09f, 3500, 1800, 0.8f, 0.35f, 0.002f, 0.04f, F_BP);                  /* squelch */
        tone(te, 0.03f, 1600, 1600, 0.12f, 0.001f, 0.01f, W_SIN, 0, 0);
    }
    finish_fx(id, handset ? 0.0f : 0.2f, 0.3f);
}

/* a gunshot: crack + body + room tail. bright: high crack level, body: low thump Hz, size: tail length */
static void gunshot(float bright, float body, float size, float dark)
{
    noise(0.0f, 0.06f, 9000 - dark * 3000, 2500, 0.7f, 1.0f * bright, 0.0005f, 0.012f, F_HP);
    noise(0.0f, 0.12f + size * 0.1f, 2600 - dark * 900, 500, 0.8f, 0.9f, 0.0008f, 0.04f + size * 0.05f, F_LP);
    tone(0.0f, 0.16f + size * 0.1f, body * 2.2f, body * 0.7f, 0.8f, 0.0008f, 0.035f + size * 0.03f, W_SIN, 0, 0);
    noise(0.012f, 0.4f + size * 0.6f, 1400 - dark * 400, 300, 0.7f, 0.18f + 0.1f * size, 0.01f, 0.12f + size * 0.2f, F_LP);   /* the room */
}

void audio_init(void)
{
    if (ready) return;
    ready = 1;
    pool_used = 0;
    for (int i = 0; i <= TABN; i++) sine_tab[i] = sinf((float)i * 2.0f * (float)M_PI / (float)TABN);

    /* --- squad weapons --- */
    begin(); gunshot(1.0f, 90, 0.6f, 0.0f); finish_fx(SFX_RIFLE, 0.3f, 0.6f);
    begin();                                                         /* shotgun: a deep boom, then the pump */
    gunshot(0.9f, 70, 1.0f, 0.3f);
    tone(0.0f, 0.3f, 120, 38, 0.9f, 0.001f, 0.09f, W_SIN, 0, 0);
    noise(0.36f, 0.03f, 2200, 2200, 2.0f, 0.35f, 0.001f, 0.01f, F_BP);
    noise(0.47f, 0.04f, 1600, 1600, 2.0f, 0.4f, 0.001f, 0.012f, F_BP);
    tone(0.47f, 0.04f, 800, 600, 0.12f, 0.001f, 0.01f, W_SIN, 0, 0);
    finish_fx(SFX_SHOTGUN, 0.35f, 0.7f);
    begin();                                                         /* silenced SMG: a muffled thup and the bolt */
    noise(0.0f, 0.05f, 900, 500, 1.2f, 0.9f, 0.001f, 0.015f, F_BP);
    tone(0.0f, 0.06f, 260, 90, 0.6f, 0.001f, 0.02f, W_SIN, 0, 0);
    noise(0.008f, 0.02f, 6000, 6000, 3.0f, 0.25f, 0.0005f, 0.004f, F_BP);
    finish_fx(SFX_SMG, 0.08f, 0.2f);
    begin(); gunshot(1.1f, 120, 0.4f, 0.0f); finish_fx(SFX_PISTOL, 0.3f, 0.5f);
    begin(); gunshot(0.8f, 110, 0.5f, 0.4f); finish_fx(SFX_E_PISTOL, 0.35f, 0.6f);
    begin(); gunshot(0.85f, 80, 0.7f, 0.5f); noise(0.0f, 0.05f, 1800, 900, 2.0f, 0.3f, 0.001f, 0.02f, F_BP); finish_fx(SFX_E_RIFLE, 0.35f, 0.7f);
    begin(); gunshot(0.8f, 65, 1.0f, 0.6f); tone(0.0f, 0.3f, 110, 35, 0.9f, 0.001f, 0.09f, W_SIN, 0, 0); finish_fx(SFX_E_SHOTGUN, 0.4f, 0.8f);

    begin(); bell(0, 4100, 0.5f, 0.025f); bell(0.05f, 5200, 0.3f, 0.02f); finish_fx(SFX_CASING, 0.0f, 0.0f);
    begin();                                                         /* ricochet: a whining ping */
    tone(0, 0.3f, 3200, 1100, 0.5f, 0.001f, 0.1f, W_FM, 1.5f, 0.6f);
    noise(0, 0.03f, 5000, 5000, 1.5f, 0.4f, 0.001f, 0.01f, F_BP);
    finish_fx(SFX_RICOCHET, 0.2f, 0.3f);
    begin();                                                         /* bullet into plaster */
    noise(0, 0.08f, 1800, 600, 1.2f, 0.9f, 0.001f, 0.02f, F_BP);
    tone(0, 0.06f, 220, 90, 0.4f, 0.001f, 0.02f, W_SIN, 0, 0);
    for (int i = 0; i < 3; i++) noise(0.03f + 0.02f * (float)i, 0.02f, 3000, 3000, 2.0f, 0.15f, 0.001f, 0.006f, F_BP);
    finish_fx(SFX_IMPACT, 0.1f, 0.2f);
    begin();                                                         /* a hit: muted thwack */
    noise(0, 0.07f, 700, 250, 1.0f, 1.0f, 0.001f, 0.02f, F_LP);
    tone(0, 0.08f, 140, 60, 0.6f, 0.001f, 0.025f, W_SIN, 0, 0);
    finish_fx(SFX_HITFLESH, 0.0f, 0.0f);
    begin();                                                         /* body falls */
    noise(0, 0.25f, 400, 120, 0.9f, 0.9f, 0.003f, 0.06f, F_LP);
    tone(0, 0.2f, 85, 50, 0.7f, 0.002f, 0.05f, W_SIN, 0, 0);
    noise(0.14f, 0.15f, 300, 120, 0.9f, 0.4f, 0.003f, 0.04f, F_LP);
    finish_fx(SFX_BODYFALL, 0.15f, 0.3f);
    begin();                                                         /* reload: click-clack */
    noise(0, 0.02f, 3000, 3000, 3.0f, 0.6f, 0.0005f, 0.006f, F_BP); tone(0, 0.03f, 900, 700, 0.2f, 0.001f, 0.01f, W_SIN, 0, 0);
    noise(0.18f, 0.03f, 2200, 2200, 3.0f, 0.8f, 0.0005f, 0.008f, F_BP); tone(0.18f, 0.04f, 600, 500, 0.25f, 0.001f, 0.012f, W_SIN, 0, 0);
    finish_fx(SFX_RELOAD, 0.1f, 0.1f);
    begin();                                                         /* a bullet flying past */
    noise(0, 0.12f, 3500, 1200, 3.0f, 0.8f, 0.01f, 0.03f, F_BP);
    tone(0, 0.1f, 2600, 1500, 0.15f, 0.005f, 0.03f, W_SIN, 0, 0);
    finish_fx(SFX_WHIZ, 0.0f, 0.0f);

    /* --- gadgets --- */
    begin();                                                         /* throw: pin, spoon, whoosh */
    bell(0, 3300, 0.35f, 0.02f);
    noise(0.05f, 0.03f, 4000, 4000, 2.0f, 0.3f, 0.001f, 0.008f, F_BP);
    noise(0.08f, 0.25f, 600, 1800, 1.0f, 0.4f, 0.08f, 0.08f, F_BP);
    finish_fx(SFX_THROW, 0.1f, 0.2f);
    begin();                                                         /* flashbang: the bang, then the ears ring */
    noise(0, 0.5f, 10000, 1500, 0.6f, 1.0f, 0.0005f, 0.07f, F_HP);
    noise(0, 0.8f, 3000, 200, 0.7f, 0.9f, 0.001f, 0.18f, F_LP);
    tone(0, 0.5f, 140, 40, 0.8f, 0.001f, 0.12f, W_SIN, 0, 0);
    tone(0.06f, 2.6f, 3600, 3550, 0.11f, 0.15f, 0.9f, W_SIN, 0, 0);
    tone(0.06f, 2.4f, 3610, 3580, 0.06f, 0.2f, 0.8f, W_SIN, 0, 0);
    finish_fx(SFX_FLASHBANG, 0.45f, 0.8f);
    begin();                                                         /* charge set: clunk + arming beeps */
    tone(0, 0.08f, 300, 160, 0.6f, 0.001f, 0.025f, W_SIN, 0, 0);
    noise(0, 0.05f, 1500, 800, 1.5f, 0.5f, 0.001f, 0.015f, F_BP);
    for (int i = 0; i < 3; i++) tone(0.15f + 0.11f * (float)i, 0.06f, 1900, 1900, 0.25f, 0.002f, 0, W_TRI, 0, 0);
    finish_fx(SFX_PLANT, 0.15f, 0.2f);
    begin();                                                         /* breach: a door coming apart */
    tone(0, 1.2f, 75, 24, 1.0f, 0.001f, 0.35f, W_SIN, 0, 0);
    noise(0, 1.3f, 3500, 90, 0.6f, 1.0f, 0.001f, 0.32f, F_LP);
    noise(0, 0.12f, 9000, 3000, 0.8f, 0.7f, 0.0005f, 0.03f, F_HP);
    for (int i = 0; i < 9; i++) noise(0.05f + 0.06f * (float)i, 0.06f, 2600 - 150 * (float)i, 900, 2.0f, 0.3f, 0.001f, 0.02f, F_BP);   /* splinters + debris */
    noise(0.3f, 1.4f, 500, 120, 0.8f, 0.25f, 0.2f, 0.6f, F_LP);     /* rumble */
    finish_fx(SFX_BREACH, 0.45f, 0.9f);
    begin();                                                         /* smoke: pop and a long hiss */
    tone(0, 0.08f, 300, 120, 0.6f, 0.001f, 0.02f, W_SIN, 0, 0);
    noise(0.02f, 2.2f, 6000, 3000, 0.6f, 0.7f, 0.1f, 0.8f, F_HP);
    noise(0.02f, 1.8f, 1500, 800, 0.8f, 0.3f, 0.1f, 0.7f, F_BP);
    finish_fx(SFX_SMOKE, 0.15f, 0.4f);
    begin();                                                         /* drone: little rotors whirring past */
    for (int i = 0; i < 4; i++) tone(0.0f, 1.0f, 170 + 6 * (float)i, 190 + 6 * (float)i, 0.2f, 0.15f, 0.8f, W_SOFT, 0.2f, 0);
    noise(0, 1.0f, 2400, 2800, 4.0f, 0.2f, 0.2f, 0.6f, F_BP);
    finish_fx(SFX_DRONE, 0.0f, 0.0f);
    begin();                                                         /* scan: sonar ping */
    tone(0, 0.8f, 1400, 1400, 0.6f, 0.002f, 0.25f, W_SIN, 0, 0);
    tone(0, 0.6f, 2800, 2800, 0.15f, 0.002f, 0.12f, W_SIN, 0, 0);
    finish_fx(SFX_SCAN, 0.6f, 0.9f);

    /* --- world --- */
    begin();                                                         /* door: latch and a creak */
    noise(0, 0.03f, 2500, 2500, 2.0f, 0.6f, 0.001f, 0.008f, F_BP);
    tone(0.04f, 0.45f, 380, 520, 0.25f, 0.05f, 0.25f, W_FM, 2.6f, 2.5f);
    noise(0.04f, 0.4f, 900, 1300, 6.0f, 0.15f, 0.05f, 0.2f, F_BP);
    finish_fx(SFX_DOOR, 0.15f, 0.3f);
    begin();                                                         /* glass shattering */
    noise(0, 0.5f, 7000, 3000, 0.7f, 0.7f, 0.001f, 0.1f, F_HP);
    for (int i = 0; i < 14; i++) bell(0.01f + 0.025f * (float)i + 0.01f * frand(), 2600.0f + 2200.0f * (0.5f + 0.5f * frand()), 0.18f, 0.03f);
    finish_fx(SFX_GLASS, 0.2f, 0.3f);
    begin();                                                         /* thunder */
    noise(0.0f, 2.8f, 260, 60, 0.7f, 1.0f, 0.03f, 0.8f, F_LP);
    noise(0.15f, 1.6f, 500, 90, 0.9f, 0.5f, 0.05f, 0.4f, F_LP);
    noise(0.55f, 1.8f, 300, 70, 0.9f, 0.55f, 0.08f, 0.5f, F_LP);
    tone(0.0f, 2.4f, 42, 30, 0.5f, 0.05f, 0.8f, W_SIN, 0, 0);
    finish_fx(SFX_THUNDER, 0.0f, 0.0f);
    begin(); noise(0, 0.05f, 900, 400, 1.0f, 0.5f, 0.002f, 0.015f, F_LP); finish_fx(SFX_STEP, 0.0f, 0.0f);

    /* --- interface --- */
    begin(); tone(0, 0.04f, 1500, 1300, 0.5f, 0.001f, 0.01f, W_SIN, 0, 0); finish_fx(SFX_MOVE, 0.0f, 0.0f);
    begin(); tone(0, 0.06f, 900, 1100, 0.5f, 0.002f, 0.025f, W_SIN, 0, 0); tone(0.04f, 0.1f, 1500, 1500, 0.35f, 0.002f, 0.04f, W_FM, 2.0f, 0.5f); finish_fx(SFX_SELECT, 0.2f, 0.2f);
    begin(); tone(0, 0.12f, 600, 900, 0.5f, 0.002f, 0.05f, W_SIN, 0, 0); bell(0.03f, 1800, 0.25f, 0.05f); finish_fx(SFX_WAYPOINT, 0.2f, 0.2f);
    begin(); tone(0, 0.12f, 900, 500, 0.5f, 0.002f, 0.05f, W_SIN, 0, 0); finish_fx(SFX_UNDO, 0.1f, 0.1f);
    begin(); tone(0, 0.07f, 1200, 1200, 0.35f, 0.002f, 0, W_TRI, 0, 0); tone(0.09f, 0.1f, 1600, 1600, 0.35f, 0.002f, 0, W_TRI, 0, 0); finish_fx(SFX_HOLD, 0.15f, 0.2f);
    begin(); tone(0, 0.12f, 160, 150, 0.7f, 0.003f, 0.05f, W_SOFT, 0.0f, 0); tone(0.1f, 0.14f, 125, 115, 0.7f, 0.003f, 0.05f, W_SOFT, 0.0f, 0); finish_fx(SFX_ERROR, 0.0f, 0.0f);
    begin(); bell(0, 1318, 0.4f, 0.06f); bell(0.07f, 1975, 0.4f, 0.09f); finish_fx(SFX_CONFIRM, 0.3f, 0.3f);
    begin(); noise(0, 0.07f, 2600, 2000, 1.0f, 0.6f, 0.002f, 0.025f, F_BP); tone(0, 0.025f, 1700, 1700, 0.2f, 0.001f, 0.01f, W_SIN, 0, 0); finish_fx(SFX_SQUELCH, 0.0f, 0.0f);
    begin();                                                         /* GO: a huge cinematic hit */
    tone(0, 2.2f, 55, 52, 0.7f, 0.01f, 0.9f, W_SOFT, 0.2f, 0);
    tone(0, 2.2f, 82.4f, 78, 0.5f, 0.01f, 0.9f, W_SOFT, 0.3f, 0);
    tone(0, 2.0f, 110, 104, 0.35f, 0.02f, 0.7f, W_SOFT, 0.5f, 0);
    tone(0, 0.6f, 120, 34, 1.0f, 0.001f, 0.18f, W_SIN, 0, 0);
    noise(0, 0.9f, 1800, 120, 0.7f, 0.8f, 0.001f, 0.25f, F_LP);
    noise(0, 0.08f, 6000, 3000, 0.8f, 0.4f, 0.001f, 0.02f, F_HP);
    finish_fx(SFX_GO, 0.4f, 1.0f);
    begin();                                                         /* riser: tension building to the GO */
    noise(0, 2.6f, 300, 5000, 2.5f, 0.6f, 2.2f, 0, F_BP);
    tone(0, 2.6f, 110, 440, 0.3f, 2.0f, 0, W_SOFT, 0.6f, 0);
    tone(0, 2.6f, 116, 466, 0.2f, 2.0f, 0, W_SOFT, 0.6f, 0);
    for (int i = 0; i < 12; i++) tone(0.6f + 0.17f * (float)i, 0.06f, 2000, 2000, 0.05f + 0.01f * (float)i, 0.002f, 0.02f, W_TRI, 0, 0);
    finish_fx(SFX_RISER, 0.2f, 0.5f);
    begin();                                                         /* mission complete */
    {
        static const float NF[6] = { 523.25f, 659.25f, 783.99f, 1046.5f, 783.99f, 1046.5f };
        static const float TT[6] = { 0, 0.12f, 0.24f, 0.36f, 0.6f, 0.72f };
        for (int i = 0; i < 6; i++) tone(TT[i], 0.6f, NF[i], NF[i], 0.35f, 0.005f, 0.3f, W_FM, 2.0f, 1.0f);
        tone(0.72f, 1.6f, 130.8f, 130.8f, 0.4f, 0.03f, 0.7f, W_SOFT, 0.6f, 0);
        tone(0.72f, 1.6f, 196.0f, 196.0f, 0.3f, 0.03f, 0.7f, W_SOFT, 0.7f, 0);
    }
    finish_fx(SFX_WIN, 0.5f, 0.7f);
    begin();                                                         /* mission failed: low, dissonant */
    tone(0, 2.0f, 98, 92, 0.6f, 0.02f, 0.8f, W_SOFT, 0.4f, 0);
    tone(0, 2.0f, 104, 97, 0.5f, 0.02f, 0.8f, W_SOFT, 0.5f, 0);
    tone(0.2f, 1.8f, 147, 138, 0.3f, 0.05f, 0.7f, W_FM, 1.0f, 1.0f);
    noise(0, 1.5f, 400, 120, 0.8f, 0.4f, 0.05f, 0.6f, F_LP);
    finish_fx(SFX_LOSE, 0.4f, 0.7f);
    begin(); tone(0, 0.18f, 1200, 500, 0.5f, 0.004f, 0.07f, W_SIN, 0, 0); finish_fx(SFX_PAUSE, 0.3f, 0.3f);
    begin();                                                         /* spotted! a sharp rising stab */
    tone(0, 0.18f, 880, 880, 0.4f, 0.002f, 0.08f, W_SOFT, 0.0f, 0);
    tone(0.09f, 0.35f, 1318, 1318, 0.45f, 0.002f, 0.12f, W_SOFT, 0.0f, 0);
    tone(0.09f, 0.35f, 1975, 1975, 0.2f, 0.002f, 0.1f, W_FM, 2.0f, 1.0f);
    finish_fx(SFX_ALERT, 0.25f, 0.4f);
    begin(); noise(0, 0.02f, 4000, 4000, 4.0f, 0.6f, 0.0005f, 0.006f, F_BP); tone(0, 0.03f, 2000, 2000, 0.3f, 0.001f, 0.008f, W_SIN, 0, 0); finish_fx(SFX_TICK, 0.1f, 0.1f);
    begin(); bell(0, 1568, 0.45f, 0.1f); bell(0.05f, 2093, 0.35f, 0.12f); finish_fx(SFX_STAR, 0.4f, 0.4f);
    begin(); tone(0, 0.2f, 784, 784, 0.4f, 0.004f, 0.1f, W_FM, 2.0f, 0.6f); tone(0.1f, 0.4f, 1175, 1175, 0.4f, 0.004f, 0.18f, W_FM, 2.0f, 0.6f); finish_fx(SFX_SECURE, 0.35f, 0.4f);
    begin(); noise(0, 0.5f, 300, 2400, 1.2f, 0.5f, 0.2f, 0.12f, F_BP); finish_fx(SFX_MENU, 0.2f, 0.3f);

    /* --- radio voices: male ~1.3, female ~2.0 --- */
    const float M = 1.3f, F = 2.05f;
    radio_voice(SFX_V_GO,        "G OW^ _ G OW^ _ G OW^", F, 0.85f, 0.0f, 1);
    radio_voice(SFX_V_BREACH,    "B R IY^ T SH IH NG", M, 0.95f, 0.1f, 1);
    radio_voice(SFX_V_FLASH,     "F L AE^ SH _ AW T", F, 0.95f, 0.0f, 1);
    radio_voice(SFX_V_SMOKE,     "S M OW^ K _ AW T", F, 0.95f, 0.0f, 1);
    radio_voice(SFX_V_SCAN,      "S K AE^ N IH NG", M, 1.0f, 0.0f, 1);
    radio_voice(SFX_V_CONTACT_M, "K AA^ N T AE K T", M, 0.9f, 0.1f, 1);
    radio_voice(SFX_V_CONTACT_F, "K AA^ N T AE K T", F, 0.9f, 0.0f, 1);
    radio_voice(SFX_V_TANGO_M,   "T AE^ NG G OW _ D AW^ N", M, 0.9f, 0.1f, 1);
    radio_voice(SFX_V_TANGO_F,   "T AE^ NG G OW _ D AW^ N", F, 0.9f, 0.0f, 1);
    radio_voice(SFX_V_CLEAR_M,   "R UW M _ K L IH^ R", M, 1.0f, 0.0f, 1);
    radio_voice(SFX_V_CLEAR_F,   "R UW M _ K L IH^ R", F, 1.0f, 0.0f, 1);
    radio_voice(SFX_V_HOSTAGE_M, "HH AA^ S T IH D ZH _ S IH K Y UH^ R", M, 0.9f, 0.0f, 1);
    radio_voice(SFX_V_HOSTAGE_F, "HH AA^ S T IH D ZH _ S IH K Y UH^ R", F, 0.9f, 0.0f, 1);
    radio_voice(SFX_V_MANDOWN_M, "M AE^ N _ D AW^ N", M, 0.9f, 0.2f, 1);
    radio_voice(SFX_V_MANDOWN_F, "M AE^ N _ D AW^ N", F, 0.9f, 0.0f, 1);
    radio_voice(SFX_V_READY_M,   "IH N _ P AX Z IH^ SH AX N", M, 0.95f, 0.0f, 1);
    radio_voice(SFX_V_READY_F,   "IH N _ P AX Z IH^ SH AX N", F, 0.95f, 0.0f, 1);
    radio_voice(SFX_V_COMPLETE,  "M IH^ SH AX N _ K AX M P L IY^ T", 1.05f, 1.0f, 0.1f, 1);
    radio_voice(SFX_V_HEY,       "HH EY^", 1.1f, 1.2f, 0.7f, 0);
    radio_voice(SFX_V_GETEM,     "G EH^ T _ DH EH M", 1.0f, 0.9f, 0.6f, 0);
    radio_voice(SFX_V_FAILED,    "M IH^ SH AX N _ F EY^ L D", 1.05f, 1.0f, 0.1f, 1);
}

const int16_t *audio_data(int id) { return POOL + sfx_off[id]; }
int            audio_len(int id)  { return sfx_len[id]; }
int            audio_volume(int id) { return VOL[id]; }
const int16_t *audio_pool(void) { return POOL; }
int            audio_pool_len(void) { return pool_used; }
