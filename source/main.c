/*
 * GO CODE - Nintendo Wii platform layer (devkitPPC + libogc + libfat).
 *
 *   video : the game's triangle list is drawn by the Wii's GPU (GX): flat / gouraud shaded triangles
 *           with alpha, additive and 2x-multiply (lighting) blending, 60 fps, 16:9 anamorphic
 *           (or letterboxed inside 4:3 - press MINUS)
 *   input : Wii Classic Controller (in Wii Remote 1): buttons and both sticks
 *   audio : synthesized effects through ASND, pitch-shifted and panned per event; the title theme
 *           and the three mission music layers loop forever on voices 0-3, in sync, and the game
 *           fades them in and out
 *   save  : a small file on the SD card (sd:/apps/gocode/save.dat)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <sys/stat.h>

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>
#include <asndlib.h>
#include <fat.h>

#include "game.h"
#include "music.h"
#include "gfx.h"

#define FIFO_SIZE (512 * 1024)
#define X_HOME  (1u << 16)
#define X_MINUS (1u << 17)
#define SAVE_DIR  "sd:/apps/gocode"
#define SAVE_PATH "sd:/apps/gocode/save.dat"

static GXRModeObj *rmode;
static void       *xfb[2];
static int         fbi;
static int         wide = 1;
static volatile int quit;
static Game       *game;

/* ------------------------------------------------------------------ */
/* video                                                               */
/* ------------------------------------------------------------------ */
static void set_view(void)
{
    float w = (float)rmode->fbWidth, h = (float)rmode->efbHeight;
    if (wide) {
        GX_SetViewport(0, 0, w, h, 0, 1);
        GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    } else {
        float bh = h * 0.75f, top = (h - bh) * 0.5f;
        GX_SetViewport(0, top, w, bh, 0, 1);
        GX_SetScissor(0, (u32)top, rmode->fbWidth, (u32)bh);
    }
}

static void video_init(void)
{
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    if ((rmode->viTVMode >> 2) == VI_PAL) rmode = &TVEurgb60Hz480IntDf;     /* always 60 Hz */
    rmode->viWidth   = 678;
    rmode->viXOrigin = (VI_MAX_WIDTH_NTSC - rmode->viWidth) / 2;

    xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_Configure(rmode);
    VIDEO_ClearFrameBuffer(rmode, xfb[0], COLOR_BLACK);
    VIDEO_ClearFrameBuffer(rmode, xfb[1], COLOR_BLACK);
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    void *fifo = memalign(32, FIFO_SIZE);
    memset(fifo, 0, FIFO_SIZE);
    GX_Init(fifo, FIFO_SIZE);

    GXColor black = {0, 0, 0, 255};
    GX_SetCopyClear(black, 0x00FFFFFF);
    f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    u32 xfbh = GX_SetDispCopyYScale(yscale);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, xfbh);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering, ((rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));
    GX_SetPixelFmt(rmode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetDither(GX_ENABLE);
    set_view();

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(0);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetAlphaUpdate(GX_FALSE);
    GX_SetColorUpdate(GX_TRUE);
    GX_InvVtxCache();
    GX_InvalidateTexAll();

    Mtx44 proj;
    guOrtho(proj, 0, VH, 0, VW, 0, 300);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    Mtx view;
    guMtxIdentity(view);
    guMtxTransApply(view, view, 0.0f, 0.0f, -50.0f);
    GX_LoadPosMtxImm(view, GX_PNMTX0);
}

static void draw_list(void)
{
    for (int s = 0; s < gfx_nseg; s++) {
        const GSeg *seg = &gfx_seg[s];
        if (seg->count < 3) continue;
        switch (seg->mode) {
        case BL_ADD: GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR); break;
        case BL_MUL: GX_SetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_SRCCLR, GX_LO_CLEAR); break;   /* 2 * src * dst */
        default:     GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR); break;
        }
        int i = seg->start, end = seg->start + seg->count;
        while (i + 2 < end) {
            int n = end - i;
            if (n > 65532) n = 65532;
            n -= n % 3;
            GX_Begin(GX_TRIANGLES, GX_VTXFMT0, (u16)n);
            for (int k = 0; k < n; k++) {
                const GVtx *v = &gfx_v[i + k];
                GX_Position2f32(v->x, v->y);
                GX_Color1u32(v->c);
            }
            GX_End();
            i += n;
        }
    }
}

static int frame_pending;

static void show_previous(void)
{
    if (!frame_pending) { VIDEO_WaitVSync(); return; }
    GX_WaitDrawDone();
    VIDEO_SetNextFramebuffer(xfb[fbi ^ 1]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    frame_pending = 0;
}

static void present(void)
{
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(xfb[fbi], GX_TRUE);
    GX_SetDrawDone();
    GX_Flush();
    frame_pending = 1;
    fbi ^= 1;
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */
static float stick_axis(int pos, int center, int lo, int hi)
{
    float d = (float)(pos - center);
    float range = d >= 0 ? (float)(hi - center) : (float)(center - lo);
    if (range < 4.0f) return 0.0f;
    float v = d / range;
    return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
}

static void read_stick(const joystick_t *js, float *ox, float *oy)
{
    float x = stick_axis(js->pos.x, js->center.x, js->min.x, js->max.x);
    float y = -stick_axis(js->pos.y, js->center.y, js->min.y, js->max.y);
    if (x == 0.0f && y == 0.0f && js->mag > 0.0f && js->mag <= 1.5f) {
        float a = js->ang * 3.14159265f / 180.0f;
        x = sinf(a) * js->mag; y = -cosf(a) * js->mag;
    }
    float m = sqrtf(x * x + y * y);
    if (m < 0.2f || !isfinite(m)) { x = y = 0.0f; }
    else { float k = (m > 1.0f ? 1.0f : m); k = (k - 0.2f) / 0.8f / m; x *= k; y *= k; }
    *ox = x; *oy = y;
}

static u32 read_input(Input *in)
{
    WPAD_ScanPads();
    u32 h = WPAD_ButtonsHeld(0), b = 0;
    if (h & WPAD_CLASSIC_BUTTON_UP)     b |= BTN_UP;
    if (h & WPAD_CLASSIC_BUTTON_DOWN)   b |= BTN_DOWN;
    if (h & WPAD_CLASSIC_BUTTON_LEFT)   b |= BTN_LEFT;
    if (h & WPAD_CLASSIC_BUTTON_RIGHT)  b |= BTN_RIGHT;
    if (h & WPAD_CLASSIC_BUTTON_A)      b |= BTN_A;
    if (h & WPAD_CLASSIC_BUTTON_B)      b |= BTN_B;
    if (h & WPAD_CLASSIC_BUTTON_X)      b |= BTN_X;
    if (h & WPAD_CLASSIC_BUTTON_Y)      b |= BTN_Y;
    if (h & WPAD_CLASSIC_BUTTON_ZL)     b |= BTN_ZL;
    if (h & WPAD_CLASSIC_BUTTON_ZR)     b |= BTN_ZR;
    if (h & WPAD_CLASSIC_BUTTON_FULL_L) b |= BTN_L;
    if (h & WPAD_CLASSIC_BUTTON_FULL_R) b |= BTN_R;
    if (h & WPAD_CLASSIC_BUTTON_PLUS)   b |= BTN_PLUS;
    if (h & WPAD_CLASSIC_BUTTON_MINUS)  b |= X_MINUS;
    if ((h & WPAD_CLASSIC_BUTTON_HOME) || (h & WPAD_BUTTON_HOME)) b |= X_HOME;

    in->sx = in->sy = in->rx = in->ry = 0.0f;
    WPADData *wd = WPAD_Data(0);
    if (wd && wd->exp.type == WPAD_EXP_CLASSIC) {
        read_stick(&wd->exp.classic.ljs, &in->sx, &in->sy);
        read_stick(&wd->exp.classic.rjs, &in->rx, &in->ry);
    }
    return b;
}

/* ------------------------------------------------------------------ */
/* audio                                                               */
/* ------------------------------------------------------------------ */
#define FIRST_SFX_VOICE MUS_TRACKS

static void play_sfx(int id, int pitch, float pan, float vol)
{
    s32 v = -1;
    for (s32 k = FIRST_SFX_VOICE; k < 16; k++) if (ASND_StatusVoice(k) == SND_UNUSED) { v = k; break; }
    if (v < 0) return;
    float base = (float)audio_volume(id) * (vol > 1.0f ? 1.0f : vol);
    int vl = (int)(base * (pan > 0 ? 1.0f - pan * 0.7f : 1.0f));
    int vr = (int)(base * (pan < 0 ? 1.0f + pan * 0.7f : 1.0f));
    s32 rate = SFX_RATE * pitch / 128;
    if (rate > MAX_PITCH) rate = MAX_PITCH;
    ASND_SetVoice(v, VOICE_MONO_16BIT, rate, 0, (void *)audio_data(id), audio_len(id) * 2, vl, vr, NULL);
}

static void music_start(void)
{
    ASND_Pause(1);                                   /* start every loop on the same sample */
    for (int t = 0; t < MUS_TRACKS; t++)
        ASND_SetInfiniteVoice(t, VOICE_MONO_16BIT, MUS_RATE, 0, (void *)music_data(t), music_len(t) * 2, 0, 0);
    ASND_Pause(0);
}

/* ------------------------------------------------------------------ */
/* saving                                                              */
/* ------------------------------------------------------------------ */
static int sd_ok;

static void save_load(void)
{
    sd_ok = fatInitDefault();
    game_storage(game, sd_ok);
    if (!sd_ok) return;
    FILE *f = fopen(SAVE_PATH, "rb");
    if (!f) return;
    static char buf[SAVE_MAX];
    int n = (int)fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n > 0) game_load(game, buf, n);
}

static void save_write(void)
{
    static char buf[SAVE_MAX];
    int n = game_save_request(game, buf, sizeof buf);
    if (n <= 0 || !sd_ok) return;
    mkdir("sd:/apps", 0777);
    mkdir(SAVE_DIR, 0777);
    FILE *f = fopen(SAVE_PATH, "wb");
    int ok = 0;
    if (f) { ok = fwrite(buf, 1, (size_t)n, f) == (size_t)n; ok = (fclose(f) == 0) && ok; }
    game_save_result(game, ok);
}

/* one frame while the sound and music are being synthesized */
static void show_loading(void)
{
    gfx_begin();
    gfx_rect(0, 0, VW, VH, 0x0A0C12FFu);
    gfx_text_c("GO CODE", VW * 0.5f, VH * 0.5f - 40, 6.0f, 0xFFC93AFFu);
    gfx_text_c("LOADING THE SQUAD...", VW * 0.5f, VH * 0.5f + 20, 1.6f, 0x9AA4B4FFu);
    for (int k = 0; k < 2; k++) {          /* into both framebuffers */
        draw_list();
        present();
        show_previous();
    }
}

static void on_reset(u32 irq, void *ctx) { (void)irq; (void)ctx; quit = 1; }
static void on_power(void) { quit = 1; }

/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    video_init();
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC);
    ASND_Init();
    ASND_Pause(0);
    SYS_SetResetCallback(on_reset);
    SYS_SetPowerCallback(on_power);

    show_loading();
    game = (Game *)memalign(32, GAME_SIZE);
    game_init(game);                                 /* also synthesizes the sound and the music */
    DCFlushRange((void *)audio_pool(), (u32)audio_pool_len() * 2);
    for (int t = 0; t < MUS_TRACKS; t++) DCFlushRange((void *)music_data(t), (u32)music_len(t) * 2);
    save_load();
    music_start();

    const float dt = 1.0f / 60.0f;
    u32 prev = 0;
    u64 last = gettime();

    while (!quit) {
        Input in;
        u32 held = read_input(&in);
        u32 pressed = held & ~prev;
        prev = held;

        if (held & X_HOME) break;
        if (pressed & X_MINUS) { wide = !wide; set_view(); }

        u64 now = gettime();
        float elapsed = (float)diff_usec(last, now) * 1e-6f;
        last = now;
        int steps = (int)(elapsed / dt + 0.5f);
        if (steps < 1) steps = 1;
        if (steps > 3) steps = 3;
        for (int s = 0; s < steps; s++) {
            in.held = held & 0xFFFFu;
            in.pressed = s == 0 ? (pressed & 0xFFFFu) : 0;
            game_update(game, &in, dt);
        }

        int id, pitch;
        float pan, vol;
        while ((id = game_pop_sound(game, &pitch, &pan, &vol)) >= 0) play_sfx(id, pitch, pan, vol);

        float mv[MUS_TRACKS];
        game_music(game, mv);
        for (int t = 0; t < MUS_TRACKS; t++) {
            int v = (int)(mv[t] * (float)music_volume(t));
            ASND_ChangeVolumeVoice(t, v, v);
        }

        save_write();

        game_render(game);
        show_previous();
        draw_list();
        present();
    }

    ASND_End();
    return 0;
}
