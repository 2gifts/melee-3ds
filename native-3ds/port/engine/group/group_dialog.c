/* Group play's top-screen dialogs (group_engine.h): Melee's own message box, the one the memory card notices use
 * (NtMsgWin.dat, built as gm_1ADD.c builds it), with free text. "End the session?" on the lobby's character select and
 * the session-lost notices on the VS menu.
 *
 * The box is the archive's window and button models on its own camera (GX link 14, drawn after the scene's cameras),
 * and SIS texts on that camera. The words are free text (no SdMsgBox.usd), so no SIS data slot is loaded: the texts use
 * font slot 3 only to find their camera, which leaves the VS menu's SdToy texts (slot 3) alone. The archive, the camera
 * and the text canvas are made once per scene, on the first dialog (mp_group_dialog_scene forgets them when a scene that
 * shows dialogs begins); each dialog's window, buttons and texts are freed when it closes, so the scene's text memory
 * does not run out over many dialogs.
 *
 * Input: A picks the highlighted button, Left / Right move, B picks the last button (No, or OK). The dialog reads pad 0
 * itself; the scene under it must ignore its input while mp_group_dialog_up(). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <melee/gm/gm_1601.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/sislib.h>
#include <group_engine.h>
#include <slippi_net_bridge.h>

void mp_platform_log(const char* text);
extern void mp_display_text_plane(HSD_CObj*, float);

#define FONT_SLOT 3          /* the SIS font index the texts find their camera by */
#define MAX_LINES 2
#define BUTTON_X 3.5F        /* the buttons' x (model units), as gm_1AED.c's two-button notices */
#define MSG_SCALE 0.079F     /* the notices' text scale (gm_1ADD.c): two lines */
#define MSG_SCALE1 0.09F     /* one line: larger, still inside the text box's dark fill */
#define WRAP_AT 18           /* a longer one-line message is set as two lines (at the space nearest its middle) */
#define BUTTON_SCALE 0.1F    /* the notices' Yes / No / OK (SdMsgBox's 2 to 4) */
/* Where things are on the top screen (400 x 240), measured in Azahar: the window's text box (right of the "!": inside
 * y 74 to 101, its dark fill x 136 to 270) and its buttons (y 109 to 132, x 164 to 236 for OK), and how the texts'
 * units map to it on this camera. */
#define TITLE_X 203.5F       /* the fill's centre (203): measured, a centred line lands within a pixel of its x */
#define TITLE_Y1 88.0F       /* one line: its centre (the text box's inside, y 74 to 101) */
#define TITLE_Y2 81.5F       /* two lines: the first's centre; the second is LINE_PX below */
#define LINE_PX 13.0F
#define BUTTON_Y 119.5F      /* the buttons' centre (120.5) less 1: a label lands 1 px low of where it is asked */
#define LABEL_DX 2.5F        /* a centred label at 0.1 lands this far left of where it is asked (measured: OK) */
#define PX_PER_MODEL 12.1F   /* a model unit at the window's depth (the buttons' centres, 3.5 either side of x 200) */
#define TEXT_W 24.0F         /* a line's box (text units: the fill's width); a longer line is squeezed into it.
                              * Centred text (alignment 1) is centred on its pos_x */
/* SY places a text's top: its line's centre is this far below, growing with the scale (measured at 0.079 and 0.1). */
#define TEXT_DY(scale) (119.0F * (scale) - 7.4F)
#define SX(px) (((px) - 202.0F) / 5.6F)
#define SY(py) (((py) - 120.0F) / 4.8F - 31.0F)
#define GOLD_R 0xFF
#define GOLD_G 0xCC
#define GOLD_B 0x33
#define DIM 0x66

static struct {
    SceneDesc* desc;           /* NtMsgWin.dat's ScNtcCommon_scene_data, once per scene */
    HSD_GObj* camera;
    int canvas;                /* the SIS canvas (HSD_SisLib_803A611C's index for FONT_SLOT) */
    int failed;                /* the archive could not be loaded in this scene */
    /* the dialog */
    int up;
    int buttons;               /* 1 or 2 */
    int selected;              /* 0 left, 1 right */
    int answer;                /* 0 none yet, else 1 + the button */
    int wait;                  /* frames before input counts (the press that opened it) */
    HSD_GObj* window;
    HSD_GObj* button[2];
    HSD_Text* message[MAX_LINES];
    HSD_Text* label[2];
    HSD_GObj* proc;
} dlg;

void mp_group_dialog_scene(void)
{
    memset(&dlg, 0, sizeof dlg);   /* the last scene's archive, camera and canvas went with it */
}

/* gm_801AECC4: model 0 is a button, model 1 the window. */
static HSD_GObj* model(int index, float x, float y)
{
    HSD_GObj* gobj = GObj_Create(9, 13, 0);
    HSD_JObj* jobj;
    if (gobj == NULL) {
        return NULL;
    }
    jobj = HSD_JObjLoadJoint(dlg.desc->models[index]->joint);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 14, 0);
    gm_8016895C(jobj, dlg.desc->models[index], 0);
    HSD_JObjReqAnimAll(jobj, 0.0F);
    HSD_JObjAnimAll(jobj);
    if (index == 1) {
        HSD_JObjSetFlagsAll(jobj->child->child->next->child->next, JOBJ_HIDDEN);
        HSD_JObjSetFlagsAll(jobj->child->child->next->next, JOBJ_HIDDEN);
    }
    HSD_JObjSetTranslateX(jobj, x);
    HSD_JObjSetTranslateY(jobj, y);
    return gobj;
}

/* The scene's cameras leave their depth in the buffer: the window would be hidden behind whatever is nearer (the
 * VS menu's panels), its dimming layer most of all. Its camera starts from a clear depth buffer instead, so the whole
 * box, and the dimmed screen around it, is drawn as on the menu's own Erase Data box. */
static void camera_draw(HSD_GObj* gobj, int pass)
{
    (void) pass;
    if (HSD_CObjSetCurrent(gobj->hsd_obj)) {
        HSD_CObjEraseScreen(gobj->hsd_obj, 0, 0, 1);
        HSD_GObj_80390ED0(gobj, 7);
        HSD_CObjEndCurrent();
    }
}

static int prepare(void)
{
    HSD_CObj* cobj;
    if (dlg.camera != NULL) {
        return 1;
    }
    if (dlg.failed) {
        return 0;
    }
    if (lbArchive_80016DBC("NtMsgWin.dat", &dlg.desc, "ScNtcCommon_scene_data", 0) == NULL || dlg.desc == NULL ||
        dlg.desc->cameras == NULL || dlg.desc->models == NULL)
    {
        dlg.failed = 1;
        mp_platform_log("group: dialog: NtMsgWin.dat did not load\n");
        return 0;
    }
    dlg.camera = GObj_Create(20, 21, 0);
    if (dlg.camera == NULL) {
        dlg.failed = 1;
        return 0;
    }
    cobj = HSD_CObjLoadDesc(dlg.desc->cameras[0].desc);
    HSD_GObjObject_80390A70(dlg.camera, HSD_GObj_CameraKind, cobj);
    mp_display_text_plane(cobj, 35.0F);   /* the text at the window's depth (presentation_mods.py) */
    GObj_SetupGXLinkMax(dlg.camera, camera_draw, 0xFF);   /* after the scene's own cameras: on top */
    dlg.camera->gxlink_prios = 1 << 14;
    dlg.canvas = HSD_SisLib_803A611C(FONT_SLOT, dlg.camera, 9, 13, 0, 14, 0, 18);
    return 1;
}

static HSD_Text* text(float x, float y, float w, float scale, int centred)
{
    HSD_Text* t = HSD_SisLib_803A6754(FONT_SLOT, dlg.canvas);
    if (t == NULL) {
        return NULL;
    }
    t->pos_x = x;
    t->pos_y = y;
    t->pos_z = 0.0F;
    t->box_size_x = w;
    t->box_size_y = 12.0F;
    t->x34.x = scale;
    t->x34.y = scale;
    t->default_alignment = centred;
    t->default_fitting = 1;   /* squeezed into the box when too wide */
    t->default_kerning = 1;   /* proportional, as the game's own texts */
    return t;
}

static void highlight(void)
{
    int i;
    for (i = 0; i < dlg.buttons; i++) {
        int on = i == dlg.selected;
        GXColor c;
        c.r = on ? GOLD_R : DIM;
        c.g = on ? GOLD_G : DIM;
        c.b = on ? GOLD_B : DIM;
        c.a = 0xFF;
        if (dlg.button[i] != NULL) {
            HSD_JObj* j = GET_JOBJ(dlg.button[i]);
            j->child->child->u.dobj->mobj->mat->diffuse.r = c.r;
            j->child->child->u.dobj->mobj->mat->diffuse.g = c.g;
            j->child->child->u.dobj->mobj->mat->diffuse.b = c.b;
        }
        if (dlg.label[i] != NULL) {
            HSD_SisLib_803A74F0(dlg.label[i], 0, &c);
        }
    }
}

static void free_box(void)
{
    int i;
    if (dlg.proc != NULL) {
        HSD_GObjFree(dlg.proc);
    }
    for (i = 0; i < 2; i++) {
        if (dlg.label[i] != NULL) {
            HSD_SisLib_803A5CC4(dlg.label[i]);
        }
        if (dlg.button[i] != NULL) {
            HSD_GObjFree(dlg.button[i]);
        }
        dlg.label[i] = NULL;
        dlg.button[i] = NULL;
    }
    for (i = 0; i < MAX_LINES; i++) {
        if (dlg.message[i] != NULL) {
            HSD_SisLib_803A5CC4(dlg.message[i]);
        }
        dlg.message[i] = NULL;
    }
    if (dlg.window != NULL) {
        HSD_GObjFree(dlg.window);
    }
    dlg.window = NULL;
    dlg.proc = NULL;
    dlg.up = 0;
}

static void think(HSD_GObj* gobj)
{
    u32 t;
    (void) gobj;
    if (!dlg.up || dlg.answer) {
        return;
    }
    if (dlg.wait > 0) {
        dlg.wait--;
        return;
    }
    t = HSD_PadCopyStatus[0].trigger;
    if (t & HSD_PAD_A) {
        lbAudioAx_80024030(1);
        dlg.answer = 1 + dlg.selected;
    } else if (t & HSD_PAD_B) {
        lbAudioAx_80024030(0);
        dlg.answer = dlg.buttons;   /* the last button: No, or OK */
    } else if (dlg.buttons == 2 && (t & (0x40000 | HSD_PAD_DPADLEFT)) && dlg.selected != 0) {
        lbAudioAx_80024030(2);
        dlg.selected = 0;
        highlight();
    } else if (dlg.buttons == 2 && (t & (0x80000 | HSD_PAD_DPADRIGHT)) && dlg.selected != 1) {
        lbAudioAx_80024030(2);
        dlg.selected = 1;
        highlight();
    }
}

int mp_group_dialog_open(const char* message, const char* left, const char* right, int selected)
{
    char lines[MAX_LINES][64];
    int n = 0, i;
    const char* p = message;
    char log[160];

    mp_group_dialog_close();
    if (!prepare()) {
        return 0;
    }
    while (*p != 0 && n < MAX_LINES) {   /* '\n' separates lines */
        const char* e = strchr(p, '\n');
        size_t len = e != NULL ? (size_t) (e - p) : strlen(p);
        if (len >= sizeof lines[0]) {
            len = sizeof lines[0] - 1;
        }
        memcpy(lines[n], p, len);
        lines[n++][len] = 0;
        p = e != NULL ? e + 1 : p + len;
    }
    if (n == 1 && strlen(lines[0]) > WRAP_AT) {   /* too long for one line at MSG_SCALE1: two lines */
        int len = (int) strlen(lines[0]), best = -1, k;
        for (k = 1; k < len - 1; k++) {
            if (lines[0][k] == ' ' && (best < 0 || abs(k - len / 2) < abs(best - len / 2))) {
                best = k;
            }
        }
        if (best > 0) {
            strcpy(lines[1], lines[0] + best + 1);
            lines[0][best] = 0;
            n = 2;
        }
    }
    dlg.buttons = right != NULL ? 2 : 1;
    dlg.selected = dlg.buttons == 2 && selected ? 1 : 0;
    dlg.answer = 0;
    dlg.wait = 10;
    dlg.window = model(1, 0.0F, 0.0F);
    for (i = 0; i < n; i++) {   /* a text per line, centred in the title band */
        float scale = n == 1 ? MSG_SCALE1 : MSG_SCALE;
        float y = (n == 1 ? TITLE_Y1 : TITLE_Y2) + LINE_PX * (float) i;
        dlg.message[i] = text(SX(TITLE_X), SY(y + TEXT_DY(scale)), TEXT_W, scale, 1);
        if (dlg.message[i] != NULL) {
            char sis[128];
            mp_platform_slippi_ui_sis(lines[i], sis, sizeof sis);   /* the game's own conversion lacks "?" */
            HSD_SisLib_803A6B98(dlg.message[i], 0.0F, 0.0F, "%s", sis);
        }
    }
    for (i = 0; i < dlg.buttons; i++) {
        float bx = dlg.buttons == 1 ? 0.0F : i == 0 ? -BUTTON_X : BUTTON_X;
        dlg.button[i] = model(0, bx, 0.0F);
        dlg.label[i] = text(SX(200.0F + PX_PER_MODEL * bx + LABEL_DX), SY(BUTTON_Y + TEXT_DY(BUTTON_SCALE)), 10.0F, BUTTON_SCALE, 1);
        if (dlg.label[i] != NULL) {
            char sis[64];
            mp_platform_slippi_ui_sis(i == 0 ? left : right, sis, sizeof sis);
            HSD_SisLib_803A6B98(dlg.label[i], 0.0F, 0.0F, "%s", sis);
        }
    }
    dlg.proc = GObj_Create(15, 17, 0);
    if (dlg.proc != NULL) {
        HSD_GObj_SetupProc(dlg.proc, think, 0);
    }
    dlg.up = 1;
    highlight();
    snprintf(log, sizeof log, "group: dialog \"%s\" [%s%s%s]\n", lines[0], left, right != NULL ? " / " : "",
             right != NULL ? right : "");
    mp_platform_log(log);
    return 1;
}

int mp_group_dialog_poll(void)
{
    int a;
    if (!dlg.up) {
        return -1;
    }
    if (!dlg.answer) {
        return 0;
    }
    a = dlg.answer;
    free_box();
    return a;
}

int mp_group_dialog_up(void)
{
    return dlg.up;
}

void mp_group_dialog_close(void)
{
    if (dlg.up) {
        free_box();
    }
}

/* Tests: poke a message's number here (the debugger stub) and the next call opens that dialog, as the game would show
 * it (group_lobby_screen.py calls this on "Choose a game."): every notice a session's end can give, the longest ones
 * included, and "End the session?". */
int mp_test_dialog;

void mp_group_dialog_test_frame(void)
{
    static const char* const message[] = {
        NULL, "The host left", "The host stopped responding", "Local wireless failed", "Local wireless is unavailable",
        "The session ended", "The connection was lost", "Please enable wireless and try again",
        "Could not join the game (0xC8A0CFF0)", "End the session?",
    };
    int i = mp_test_dialog;
    if (i <= 0 || i >= (int) (sizeof message / sizeof message[0])) {
        return;
    }
    mp_test_dialog = 0;
    if (i == 9) {
        (void) mp_group_dialog_open(message[i], "Yes", "No", 1);
    } else {
        (void) mp_group_dialog_open(message[i], "OK", NULL, 0);
    }
}
