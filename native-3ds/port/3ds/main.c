#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "melee_port/archive.h"
#include "melee_port/input.h"
#include "melee_port/engine.h"

static MpArchive archive;
static void *archive_bytes;
#ifdef MP_BE8_TEST
extern uint32_t *mp_be_fixups_start[], *mp_be_fixups_end[];
extern int mp_be_engine_probe(void *, unsigned, unsigned *);
static void be8_archive_test(FILE *log)
{
    unsigned fixups=0;
    for(uint32_t **p=mp_be_fixups_start;p<mp_be_fixups_end;++p) {
        **p=__builtin_bswap32(**p); ++fixups;
    }
    /* ARM instruction/data caches must observe the now-big-endian literals. */
    GSPGPU_FlushDataCache((void*)0x00100000,0x00100000);
    FILE *f=fopen("sdmc:/3ds/melee/files/PlFx.dat","rb");
    if(!f) { if(log) { fputs("BE8 Fox missing\n",log); fflush(log); } return; }
    fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
    void *data=malloc(size); fread(data,1,size,f); fclose(f);
    unsigned result[6]={0};
    if(log) { fprintf(log,"BE8 entering (%u fixups)\n",fixups); fflush(log); }
    int status=mp_be_engine_probe(data,size,result);
    if(log) {
        fprintf(log,"BE8 archive status=%d words:",status);
        for(int i=0;i<6;++i) fprintf(log," %08x",__builtin_bswap32(result[i]));
        fputc('\n',log); fflush(log);
    }
    free(data);
}
#endif
static char archive_status[80] = "PlCo.dat: not loaded";

static void trace(const char *s)
{
    svcOutputDebugString(s, strlen(s));
}

#ifdef MP_SMOKE_TEST
static void capture(gfxScreen_t screen, const char *name)
{
    u16 w, h;
    const u8 *fb = gfxGetFramebuffer(screen, GFX_LEFT, &w, &h);
    const size_t bytes = (size_t)w*h*(screen == GFX_TOP ? 4 : 2);
    if (screen == GFX_TOP) GSPGPU_InvalidateDataCache(fb, bytes);
    else GSPGPU_FlushDataCache(fb, bytes); /* The console is CPU-rendered. */
    FILE *f = fopen(name, "wb");
    if (f) { fwrite(fb, 1, bytes, f); fclose(f); }
}
#endif

static void load_archive(void)
{
    FILE *f = fopen("sdmc:/3ds/melee/files/PlCo.dat", "rb");
    if (!f) {
        snprintf(archive_status, sizeof(archive_status), "PlCo.dat: missing (see README)");
        return;
    }
    if (fseek(f, 0, SEEK_END)) { fclose(f); return; }
    long size = ftell(f);
    if (size < 32 || size > 16 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        snprintf(archive_status, sizeof(archive_status), "PlCo.dat: invalid size/read error");
        return;
    }
    archive_bytes = malloc((size_t)size);
    if (!archive_bytes) {
        fclose(f);
        snprintf(archive_status, sizeof(archive_status), "PlCo.dat: out of memory");
        return;
    }
    size_t read = fread(archive_bytes, 1, (size_t)size, f);
    fclose(f);
    if (read != (size_t)size || mp_archive_open(&archive, archive_bytes, read)) {
        free(archive_bytes); archive_bytes = NULL;
        snprintf(archive_status, sizeof(archive_status), "PlCo.dat: invalid HSD archive");
        return;
    }
    uint32_t root;
    int found = mp_archive_find(&archive, "ftLoadCommonData", &root);
    snprintf(archive_status, sizeof(archive_status), "PlCo.dat: %lu roots; common %s",
             (unsigned long)archive.public_count, found == 0 ? "found" : "missing");
}

int main(void)
{
    trace("melee: main entered\n");
    /* Four-byte pixels keep framebuffer readback aligned across memory pages. */
    gfxInit(GSP_RGBA8_OES, GSP_RGB565_OES, false);
    trace("melee: gfx initialized\n");
    consoleInit(GFX_BOTTOM, NULL);
    setvbuf(stdout, NULL, _IONBF, 0);
    bool is_new = false;
    APT_CheckNew3DS(&is_new);
    if (is_new) osSetSpeedupEnable(true);
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) { trace("melee: C3D init failed\n"); gfxExit(); return 1; }
    trace("melee: C3D initialized\n");
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) { trace("melee: C2D init failed\n"); C3D_Fini(); gfxExit(); return 1; }
    trace("melee: C2D initialized\n");
    C2D_Prepare();
    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    if (!top) { C2D_Fini(); C3D_Fini(); gfxExit(); return 1; }
    C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT,
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    load_archive();
    trace("melee: archive probe complete\n");
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/melee", 0777);
    FILE *log = fopen("sdmc:/3ds/melee/diagnostics.log", "a");
    const int engine_ok = mp_engine_selftest();
    trace(engine_ok ? "melee: upstream engine tests PASS\n" : "melee: upstream engine tests FAIL\n");
    if (log) {
        fprintf(log, "Melee port diagnostics; new3ds=%d; engine=%s\n%s\n",
                is_new, engine_ok ? "pass" : "FAIL", archive_status);
        fflush(log);
    }
    {
        static const unsigned char be_sample[4] __attribute__((aligned(4))) = {1,2,3,4};
        unsigned be_value;
        __asm__ volatile("setend be\n\tldr %0, [%1]\n\tsetend le"
                         : "=&r"(be_value) : "r"(be_sample) : "memory");
        if (log) { fprintf(log, "ARM BE8 word=%08x\n", be_value); fflush(log); }
    }
#ifdef MP_BE8_TEST
    be8_archive_test(log);
#endif
    uint32_t frames = 0;
    while (aptMainLoop()) {
        hidScanInput();
        u32 held = hidKeysHeld();
        if (held & KEY_SELECT) break;
        circlePosition circle, cstick;
        hidCircleRead(&circle);
        hidCstickRead(&cstick);
        MpInput in = {held, circle.dx, circle.dy, cstick.dx, cstick.dy};
        PADStatus pad[4]; mp_input_map(&in, pad);
        if ((frames++ % 10) == 0) {
            printf("\x1b[H\x1b[2JMelee 3DS - port diagnostics\n");
            printf("Gameplay is not integrated.\n\n");
            printf("Hardware: %s\n", is_new ? "New 3DS" : "Original 3DS");
            printf("Upstream RNG / spline: %s\n", engine_ok ? "PASS" : "FAIL");
            printf("%s\n\n", archive_status);
            printf("GC buttons: %04x\n", pad[0].button);
            printf("Stick: %4d %4d\nC-stick: %4d %4d\n",
                   pad[0].stickX, pad[0].stickY, pad[0].substickX, pad[0].substickY);
            printf("L/R: %3u %3u\n\n", pad[0].triggerLeft, pad[0].triggerRight);
            printf("A/B/X/Y: GC face buttons\nL/R: shield   ZL/ZR: grab\nSELECT: exit\n\n");
            printf("Top: upstream Bezier curve\nand live Circle Pad marker\n");
            if (archive_bytes) {
                MpArchiveSymbol s;
                for (uint32_t i=0; i<archive.public_count && i<3; ++i)
                    if (!mp_archive_public(&archive, i, &s)) printf("%.35s\n", s.name);
            }
            fflush(stdout);
        }
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, C2D_Color32(12, 18, 28, 255));
        C2D_SceneBegin(top);
        for (int x=0; x<400; x+=40)
            C2D_DrawLine(x,0,C2D_Color32(24,36,50,255),x,240,C2D_Color32(24,36,50,255),1,0);
        for (int y=0; y<240; y+=40)
            C2D_DrawLine(0,y,C2D_Color32(24,36,50,255),400,y,C2D_Color32(24,36,50,255),1,0);
        float px, py; mp_engine_curve(0.f, &px, &py);
        for (int i=1; i<=64; ++i) {
            float nx, ny; mp_engine_curve(i/64.f, &nx, &ny);
            C2D_DrawLine(px,py,C2D_Color32(74,220,168,255),
                         nx,ny,C2D_Color32(74,220,168,255),2,0);
            px=nx; py=ny;
        }
        C2D_DrawCircleSolid(200 + pad[0].stickX*1.8f,
                           120 - pad[0].stickY*1.2f, 0, 6,
                           C2D_Color32(255,199,83,255));
        C3D_FrameEnd(0);
        if (frames == 1) {
            trace("melee: first rendered frame\n");
            if (log) { fputs("First frame submitted\n", log); fflush(log); }
        }
        if (frames == 180 && log) {
            fputs("180 rendered frames completed\n", log); fflush(log);
        }
#ifdef MP_SMOKE_TEST
        if (frames == 180) {
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            capture(GFX_TOP, "sdmc:/3ds/melee/top-screen.abgr");
            capture(GFX_BOTTOM, "sdmc:/3ds/melee/bottom-screen.rgb565");
            C3D_FrameEnd(0);
            break;
        }
#endif
    }
    if (log) { fputs("Clean exit\n", log); fclose(log); }
    free(archive_bytes);
    C2D_Fini(); C3D_Fini(); gfxExit();
    return 0;
}
