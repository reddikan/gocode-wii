/*
 * GO CODE - music. See music.h.
 *
 * A tiny offline synth: each instrument renders whole notes into a float loop buffer (notes that run
 * past the end wrap round to the start, so every loop is seamless), then a small Schroeder reverb is
 * run round the loop twice and the result is normalised to 16-bit. The three mission layers share
 * tempo, length and harmony (C minor: Cm - Ab - Fm - G), so any mix of them is in time and in tune.
 */
#include "music.h"

#include <math.h>
#include <string.h>
#include "fastmath.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define SR    ((float)MUS_RATE)
#define TAU_F 6.28318530718f

#define MUS_MAX (MUS_RATE * 39 + 16)
static int16_t MUS[MUS_TRACKS][MUS_MAX] __attribute__((aligned(32)));
static int     mus_len[MUS_TRACKS];
static float   mix[MUS_MAX];
static int     N;                         /* length of the loop being built */
static float   BEAT;                      /* seconds per beat */
static uint32_t rng = 0x1234567u;
static int     ready;

static const uint8_t VOL[MUS_TRACKS] = { 120, 112, 104, 128 };

static float frnd(void)                  /* -1..1 */
{
    rng = rng * 1664525u + 1013904223u;
    return (float)(rng >> 8) / 8388608.0f - 1.0f;
}

static float mtof(float m) { return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f); }

static inline void put(int i, float v)
{
    i %= N;
    if (i < 0) i += N;
    mix[i] += v;
}

/* attack / exponential decay / release envelope */
static inline float env_adr(int i, int n, float atk, float tau, float rel)
{
    float t = (float)i / SR, e = 1.0f;
    if (t < atk) e = t / atk;
    if (tau > 0) e *= expf(-t / tau);
    float left = (float)(n - i) / SR;
    if (left < rel) e *= left / rel;
    return e;
}

/* ------------------------------------------------------------------ */
/* instruments (times in beats)                                         */
/* ------------------------------------------------------------------ */

/* warm pad: two detuned band-limited saws through a 2-pole low-pass, slow attack */
static void pad(float b, float dur, float m, float vel, float cutoff)
{
    int s0 = (int)(b * BEAT * SR), n = (int)((dur * BEAT + 1.2f) * SR);
    float f = mtof(m), ph1 = 0, ph2 = 0.37f, lp1 = 0, lp2 = 0;
    float k = 1.0f - expf(-TAU_F * cutoff / SR);
    float d1 = f * 1.004f / SR, d2 = f * 0.996f / SR;
    for (int i = 0; i < n; i++) {
        float s = 0;
        /* polyBLEP saws */
        for (int o = 0; o < 2; o++) {
            float *ph = o ? &ph2 : &ph1, dt = o ? d2 : d1, x = 2.0f * *ph - 1.0f, t = *ph;
            if (t < dt) { t /= dt; x -= t + t - t * t - 1.0f; }
            else if (t > 1.0f - dt) { t = (t - 1.0f) / dt; x -= t * t + t + t + 1.0f; }
            s += x;
            *ph += dt; if (*ph >= 1.0f) *ph -= 1.0f;
        }
        lp1 += (s - lp1) * k; lp2 += (lp1 - lp2) * k;
        float e = env_adr(i, n, 0.9f, 0, 1.2f);
        put(s0 + i, lp2 * e * vel * 0.18f);
    }
}

/* electric piano: FM with a decaying index plus a soft sine body */
static void epiano(float b, float dur, float m, float vel)
{
    int s0 = (int)(b * BEAT * SR), n = (int)((dur * BEAT + 1.5f) * SR);
    float f = mtof(m), pc = 0, pm = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR, idx = 1.6f * expf(-t * 3.0f);
        float e = env_adr(i, n, 0.004f, 1.4f, 0.3f);
        float v = sinf(TAU_F * pc + idx * sinf(TAU_F * pm)) + 0.35f * sinf(TAU_F * pc * 2.0f) * expf(-t * 2.0f);
        put(s0 + i, v * e * vel * 0.32f);
        pc += f / SR; pc -= floorf(pc);
        pm += f * 1.0f / SR; pm -= floorf(pm);
    }
}

/* celesta / glassy bell */
static void bell(float b, float m, float vel, float decay)
{
    int s0 = (int)(b * BEAT * SR), n = (int)(decay * 5.0f * SR);
    float f = mtof(m), pc = 0, pm = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float e = env_adr(i, n, 0.002f, decay, 0.05f);
        float v = sinf(TAU_F * pc + 1.2f * expf(-t * 6.0f) * sinf(TAU_F * pm)) + 0.25f * sinf(TAU_F * pc * 4.0f) * expf(-t * 8.0f);
        put(s0 + i, v * e * vel * 0.28f);
        pc += f / SR; pc -= floorf(pc);
        pm += f * 3.5f / SR; pm -= floorf(pm);
    }
}

/* plucked string (Karplus-Strong): guitar-ish when bright, pizzicato bass when dark */
static void pluck(float b, float dur, float m, float vel, float bright)
{
    static float ks[2048];
    int s0 = (int)(b * BEAT * SR), n = (int)((dur * BEAT + 0.6f) * SR);
    float f = mtof(m);
    int L = (int)(SR / f);
    if (L < 2) L = 2;
    if (L > 2047) L = 2047;
    float lp = 0;
    for (int i = 0; i < L; i++) { lp += (frnd() - lp) * bright; ks[i] = lp; }
    int p = 0;
    float damp = 0.4975f + 0.0024f * bright;
    for (int i = 0; i < n; i++) {
        int q = p + 1 == L ? 0 : p + 1;
        float y = ks[p];
        ks[p] = (ks[p] + ks[q]) * damp;
        p = q;
        float e = env_adr(i, n, 0.001f, 0, 0.08f);
        put(s0 + i, y * e * vel * 0.9f);
    }
}




/* soft round bass */
static void bass(float b, float dur, float m, float vel)
{
    int s0 = (int)(b * BEAT * SR), n = (int)((dur * BEAT + 0.1f) * SR);
    float f = mtof(m), ph = 0;
    for (int i = 0; i < n; i++) {
        float e = env_adr(i, n, 0.01f, dur * BEAT * 1.5f, 0.08f);
        float v = sinf(TAU_F * ph) + 0.2f * sinf(TAU_F * ph * 2.0f);
        put(s0 + i, v * e * vel * 0.4f);
        ph += f / SR; ph -= floorf(ph);
    }
}

/* low drone with a slow swell */
static void drone(float m, float vel)
{
    float f = mtof(m), ph = 0;
    for (int i = 0; i < N; i++) {
        float t = (float)i / (float)N;
        float sw = 0.6f + 0.4f * sinf(TAU_F * t * 2.0f);   /* whole cycles, so it loops cleanly */
        put(i, (sinf(TAU_F * ph) + 0.3f * sinf(TAU_F * ph * 1.5f)) * sw * vel * 0.12f);
        ph += f / SR; ph -= floorf(ph);
    }
}

static void kick(float b, float vel)
{
    int s0 = (int)(b * BEAT * SR), n = (int)(0.28f * SR);
    float ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR, f = 45.0f + 75.0f * expf(-t * 30.0f);
        put(s0 + i, sinf(TAU_F * ph) * expf(-t * 12.0f) * vel * 0.55f);
        ph += f / SR;
    }
}

/* noise percussion: shaker (bright, short) or finger snap / clap (band-passed) */
static void noise_hit(float b, float vel, float decay, float centre, float q)
{
    int s0 = (int)(b * BEAT * SR), n = (int)(decay * 6.0f * SR);
    float w = TAU_F * centre / SR, a = sinf(w) / (2.0f * q), cw = cosf(w), a0 = 1.0f + a;
    float b0 = a / a0, b2 = -a / a0, a1 = -2.0f * cw / a0, a2 = (1.0f - a) / a0, z1 = 0, z2 = 0;
    for (int i = 0; i < n; i++) {
        float x = frnd(), y = b0 * x + z1;
        z1 = -a1 * y + z2;
        z2 = b2 * x - a2 * y;
        float t = (float)i / SR;
        put(s0 + i, y * expf(-t / decay) * vel * 0.5f);
    }
}


/* ------------------------------------------------------------------ */
/* reverb (Schroeder: 4 combs + 2 allpasses), run round the loop twice  */
/* ------------------------------------------------------------------ */
static void reverb(float amount, float size)
{
    static float cb[4][3000], ap[2][800];
    static const int CL[4] = { 1116, 1188, 1277, 1356 }, AL[2] = { 556, 341 };
    int cl[4], al[2];
    for (int k = 0; k < 4; k++) cl[k] = (int)((float)CL[k] * 0.69f * size);   /* tuned for 44.1k, scaled to 22k */
    for (int k = 0; k < 2; k++) al[k] = (int)((float)AL[k] * 0.69f);
    memset(cb, 0, sizeof cb); memset(ap, 0, sizeof ap);
    int cp[4] = {0}, apos[2] = {0};
    float fs[4] = {0};
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < N; i++) {
            float x = mix[i] * 0.25f, out = 0;
            for (int k = 0; k < 4; k++) {
                float y = cb[k][cp[k]];
                fs[k] = y * 0.6f + fs[k] * 0.4f;              /* damping */
                cb[k][cp[k]] = x + fs[k] * 0.82f;
                if (++cp[k] >= cl[k]) cp[k] = 0;
                out += y;
            }
            for (int k = 0; k < 2; k++) {
                float bo = ap[k][apos[k]];
                float y = -out + bo;
                ap[k][apos[k]] = out + bo * 0.5f;
                if (++apos[k] >= al[k]) apos[k] = 0;
                out = y;
            }
            /* second time round: add the reverb in place (each sample is read as dry before it is written) */
            if (pass) mix[i] += out * amount;
        }
}

static void begin(float bpm, int beats)
{
    BEAT = 60.0f / bpm;
    N = ((int)((float)beats * BEAT * SR) + 15) & ~15;
    if (N > MUS_MAX) N = MUS_MAX;
    memset(mix, 0, sizeof mix);
}

static void finish(int track)
{
    float peak = 0;
    for (int i = 0; i < N; i++) if (fabsf(mix[i]) > peak) peak = fabsf(mix[i]);
    float k = peak > 0 ? 0.85f * 32000.0f / peak : 0;
    for (int i = 0; i < N; i++) MUS[track][i] = (int16_t)(mix[i] * k);
    mus_len[track] = N;
}


/* ------------------------------------------------------------------ */
/* more instruments                                                     */
/* ------------------------------------------------------------------ */

/* snare: a tuned body and a burst of bright noise */
static void snare(float b, float vel)
{
    int s0 = (int)(b * BEAT * SR), n = (int)(0.3f * SR);
    float ph = 0, lp = 0, hp = 0, hpp = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float body = sinf(TAU_F * ph) * expf(-t * 28.0f);
        ph += (190.0f - 40.0f * t) / SR;
        float nz = frnd();
        lp += (nz - lp) * 0.6f;
        hp = 0.9f * (hp + lp - hpp); hpp = lp;
        put(s0 + i, (body * 0.5f + hp * expf(-t * 16.0f) * 0.9f) * vel * 0.5f);
    }
}

/* toms: a pitched thump */
static void tom(float b, float m, float vel)
{
    int s0 = (int)(b * BEAT * SR), n = (int)(0.45f * SR);
    float f = mtof(m), ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        put(s0 + i, sinf(TAU_F * ph) * expf(-t * 9.0f) * vel * 0.5f + frnd() * expf(-t * 60.0f) * vel * 0.1f);
        ph += f * (1.0f + 0.6f * expf(-t * 25.0f)) / SR;
    }
}

/* crash cymbal: long bright noise */
static void crash(float b, float vel)
{
    int s0 = (int)(b * BEAT * SR), n = (int)(2.2f * SR);
    float hp = 0, hpp = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR, nz = frnd();
        hp = 0.85f * (hp + nz - hpp); hpp = nz;
        put(s0 + i, hp * expf(-t * 2.2f) * vel * 0.22f);
    }
}

/* big low impact for the bed: a boom with a long tail */
static void boom(float b, float vel)
{
    int s0 = (int)(b * BEAT * SR), n = (int)(2.4f * SR);
    float ph = 0, lp = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR, f = 38.0f + 40.0f * expf(-t * 14.0f);
        lp += (frnd() - lp) * 0.02f;
        put(s0 + i, (sinf(TAU_F * ph) * 0.8f + lp * 2.0f) * expf(-t * 1.6f) * vel * 0.5f);
        ph += f / SR;
    }
}

/* a gritty synth bass: saw through a resonant-ish low-pass with an envelope, soft-clipped */
static void sbass(float b, float dur, float m, float vel, float bright)
{
    int s0 = (int)(b * BEAT * SR), n = (int)((dur * BEAT + 0.04f) * SR);
    float f = mtof(m), ph = 0, lp1 = 0, lp2 = 0;
    float dt = f / SR;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float x = 2.0f * ph - 1.0f, u = ph;
        if (u < dt) { u /= dt; x -= u + u - u * u - 1.0f; }
        else if (u > 1.0f - dt) { u = (u - 1.0f) / dt; x -= u * u + u + u + 1.0f; }
        ph += dt; if (ph >= 1.0f) ph -= 1.0f;
        float cut = 180.0f + bright * 1400.0f * expf(-t * 18.0f);
        float k = 1.0f - expf(-TAU_F * cut / SR);
        lp1 += (x - lp1) * k; lp2 += (lp1 - lp2) * k;
        float y = lp2 * 1.8f; y = y / (1.0f + fabsf(y));
        float e = env_adr(i, n, 0.003f, 0, 0.03f);
        put(s0 + i, y * e * vel * 0.55f);
    }
}

/* brass / string stab: several detuned saws, bright attack closing down */
static void stab(float b, float dur, const float *notes, int nn, float vel)
{
    for (int k = 0; k < nn; k++) {
        int s0 = (int)(b * BEAT * SR), n = (int)((dur * BEAT + 0.25f) * SR);
        float f = mtof(notes[k]);
        float ph[3] = { 0, 0.33f, 0.71f }, det[3] = { 1.0f, 1.006f, 0.994f };
        float lp1 = 0, lp2 = 0;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR, s = 0;
            for (int o = 0; o < 3; o++) {
                float dt = f * det[o] / SR, x = 2.0f * ph[o] - 1.0f, u = ph[o];
                if (u < dt) { u /= dt; x -= u + u - u * u - 1.0f; }
                else if (u > 1.0f - dt) { u = (u - 1.0f) / dt; x -= u * u + u + u + 1.0f; }
                s += x;
                ph[o] += dt; if (ph[o] >= 1.0f) ph[o] -= 1.0f;
            }
            float cut = 500.0f + 3000.0f * expf(-t * 7.0f);
            float kk = 1.0f - expf(-TAU_F * cut / SR);
            lp1 += (s - lp1) * kk; lp2 += (lp1 - lp2) * kk;
            float e = env_adr(i, n, 0.01f, 0, 0.2f);
            put(s0 + i, lp2 * e * vel * 0.11f);
        }
    }
}

/* string tremolo: a pad chopped in 16ths */
static void tremolo(float b, float dur, float m, float vel)
{
    int s0 = (int)(b * BEAT * SR), n = (int)((dur * BEAT + 0.3f) * SR);
    float f = mtof(m), ph1 = 0, ph2 = 0.4f, lp = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float s = sinf(TAU_F * ph1) + 0.5f * sinf(TAU_F * ph1 * 2.0f) + 0.3f * sinf(TAU_F * ph2 * 3.0f);
        ph1 += f * (1.0f + 0.003f * sinf(TAU_F * 5.5f * t)) / SR; ph1 -= floorf(ph1);
        ph2 += f * 1.002f / SR; ph2 -= floorf(ph2);
        lp += (s - lp) * 0.3f;
        float trem = 0.55f + 0.45f * sinf(TAU_F * t / (BEAT * 0.25f));
        float e = env_adr(i, n, 0.15f, 0, 0.3f);
        put(s0 + i, lp * trem * e * vel * 0.13f);
    }
}

/* ------------------------------------------------------------------ */
/* the harmony                                                          */
/* ------------------------------------------------------------------ */
/* 16 bars, two per chord, played twice: Cm  Ab  Fm  G */
static const float ROOT[4]   = { 36, 32, 29, 31 };                         /* C2 Ab1 F1 G1 */
static const float CHORD[4][3] = { {60, 63, 67}, {56, 60, 63}, {53, 56, 60}, {55, 59, 62} };
static int chord_at(int bar) { return (bar / 2) % 4; }

/* ------------------------------------------------------------------ */
/* MENU - brooding title theme, 72 bpm                                  */
/* ------------------------------------------------------------------ */
static void track_menu(void)
{
    begin(72, 32);
    static const float PADS[4][4] = { {48, 55, 60, 63}, {44, 51, 56, 60}, {39, 46, 55, 58}, {43, 50, 55, 59} };
    static const float R[4] = { 36, 32, 27, 31 };
    for (int bar = 0; bar < 8; bar++) {
        int c = (bar / 2) % 4;
        for (int k = 0; k < 4; k++) pad((float)bar * 4, 4.3f, PADS[c][k], 0.5f, 800.0f);
        bass((float)bar * 4, 3.8f, R[c], 0.45f);
        for (int q = 0; q < 8; q++) sbass((float)bar * 4 + (float)q * 0.5f, 0.3f, R[c] + 12, (q & 1) ? 0.18f : 0.28f, 0.25f);
        kick((float)bar * 4, 0.35f);
    }
    static const float MEL[][3] = {
        {0, 2, 75}, {2, 1, 74}, {3, 1, 72}, {4, 3, 72}, {7, 1, 70},
        {8, 2, 75}, {10, 1, 77}, {11, 1, 79}, {12, 4, 75},
        {16, 2, 79}, {18, 1, 77}, {19, 1, 75}, {20, 3, 74}, {23, 1, 71},
        {24, 2, 72}, {26, 2, 74}, {28, 4, 72},
    };
    for (unsigned i = 0; i < sizeof MEL / sizeof MEL[0]; i++) epiano(MEL[i][0], MEL[i][1], MEL[i][2], 0.45f);
    for (int b = 0; b < 32; b += 2) noise_hit((float)b + 1.0f, 0.06f, 0.02f, 7000, 2.0f);
    drone(24, 0.5f);
    reverb(0.6f, 1.4f);
    finish(MUS_MENU);
}

/* ------------------------------------------------------------------ */
/* layer 1: BED - tension, 100 bpm                                      */
/* ------------------------------------------------------------------ */
static void track_bed(void)
{
    begin(100, 64);
    for (int bar = 0; bar < 16; bar++) {
        int c = chord_at(bar);
        if ((bar & 1) == 0) {
            for (int k = 0; k < 3; k++) pad((float)bar * 4, 8.3f, CHORD[c][k] - 12, 0.5f, 650.0f);
            bass((float)bar * 4, 7.5f, ROOT[c], 0.45f);
        }
        /* the clock: a dry tick on every 8th, heavier on the beat */
        for (int q = 0; q < 8; q++) noise_hit((float)bar * 4 + (float)q * 0.5f, (q & 1) ? 0.05f : 0.09f, 0.012f, (q & 1) ? 5200.0f : 3800.0f, 3.0f);
        if (bar % 4 == 0) boom((float)bar * 4, 0.55f);
    }
    drone(24, 0.55f);
    static const float DROP[][2] = { {6, 79}, {14.5f, 75}, {22, 84}, {30, 74}, {38, 79}, {46.5f, 75}, {54, 80}, {62, 74} };
    for (unsigned i = 0; i < sizeof DROP / sizeof DROP[0]; i++) bell(DROP[i][0], DROP[i][1], 0.2f, 0.6f);
    reverb(0.5f, 1.3f);
    finish(MUS_BED);
}

/* ------------------------------------------------------------------ */
/* layer 2: PRESSURE                                                     */
/* ------------------------------------------------------------------ */
static void track_press(void)
{
    begin(100, 64);
    for (int bar = 0; bar < 16; bar++) {
        int c = chord_at(bar);
        float b0 = (float)bar * 4;
        /* pulsing 16ths, accents on 1 and the "and" of 2 */
        for (int q = 0; q < 16; q++) {
            float acc = (q == 0 || q == 6 || q == 10) ? 1.0f : 0.6f;
            sbass(b0 + (float)q * 0.25f, 0.2f, ROOT[c] + 12, 0.5f * acc, 0.5f * acc);
        }
        if ((bar & 1) == 0)
            for (int k = 0; k < 3; k++) tremolo(b0, 8.0f, CHORD[c][k] + 12, 0.55f);
        /* heartbeat */
        for (int bt = 0; bt < 4; bt += 2) { kick(b0 + (float)bt, 0.55f); kick(b0 + (float)bt + 0.33f, 0.35f); }
        /* build on the last bar of each eight */
        if (bar % 8 == 7) for (int q = 0; q < 8; q++) tom(b0 + (float)q * 0.5f, 45.0f + (float)q, 0.25f + 0.04f * (float)q);
    }
    reverb(0.35f, 1.1f);
    finish(MUS_PRESS);
}

/* ------------------------------------------------------------------ */
/* layer 3: ASSAULT                                                      */
/* ------------------------------------------------------------------ */
static void track_assault(void)
{
    begin(100, 64);
    for (int bar = 0; bar < 16; bar++) {
        int c = chord_at(bar);
        float b0 = (float)bar * 4;
        /* drums */
        static const float KICKS[] = { 0, 1.5f, 2, 2.75f, 3.5f };
        for (unsigned k = 0; k < sizeof KICKS / sizeof KICKS[0]; k++) kick(b0 + KICKS[k], 0.95f);
        snare(b0 + 1, 0.9f); snare(b0 + 3, 0.9f);
        if (bar % 2 == 1) snare(b0 + 3.75f, 0.4f);
        for (int q = 0; q < 16; q++) noise_hit(b0 + (float)q * 0.25f, (q & 1) ? 0.04f : 0.085f, 0.014f, 9000.0f, 2.0f);
        if (bar % 4 == 0) crash(b0, 0.6f);
        if (bar % 4 == 3) for (int q = 0; q < 4; q++) tom(b0 + 2.0f + (float)q * 0.5f, 50.0f - (float)q * 3.0f, 0.55f);
        /* driving bass: octaves in 8ths */
        for (int q = 0; q < 8; q++) sbass(b0 + (float)q * 0.5f, 0.42f, ROOT[c] + ((q & 1) ? 12 : 0), 0.85f, 0.9f);
        /* brass stabs on the change */
        if ((bar & 1) == 0) {
            float st[3] = { CHORD[c][0], CHORD[c][1], CHORD[c][2] };
            stab(b0, 0.8f, st, 3, 0.9f);
            stab(b0 + 1.5f, 0.4f, st, 3, 0.7f);
        } else {
            float st[3] = { CHORD[c][0], CHORD[c][1], CHORD[c][2] };
            stab(b0 + 2.5f, 1.2f, st, 3, 0.75f);
        }
        /* a high arpeggio */
        for (int q = 0; q < 16; q++) {
            float m = CHORD[c][q % 3] + 12.0f + ((q / 3) % 2 ? 12.0f : 0.0f);
            pluck(b0 + (float)q * 0.25f, 0.2f, m, 0.22f, 0.7f);
        }
    }
    reverb(0.25f, 1.0f);
    finish(MUS_ASSAULT);
}

void music_init(void)
{
    if (ready) return;
    ready = 1;
    track_menu();
    track_bed();
    track_press();
    track_assault();
}

const int16_t *music_data(int track) { return MUS[track]; }
int            music_len(int track)  { return mus_len[track]; }
int            music_volume(int track) { return VOL[track]; }
