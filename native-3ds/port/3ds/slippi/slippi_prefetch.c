/* Slippi experiment: read a match's files into RAM before networking starts.
 *
 * A cold match load opens about 25 SD files (130-250 ms per open on the
 * console) while the PC peer waits at its first frame. These are the files a
 * match load opens, measured with the open log on each legal stage: the ones
 * every match uses, the stage's, and this player's fighter. Read from the boot
 * menu (START), in that order, until the 10 MB budget is spent. Nothing is read
 * in the background later: SD reads while matching and connecting kept the
 * opponent's selections from arriving on the console. config.ini prefetch=0
 * turns this off. */
#include <stdio.h>
#include <string.h>

int mp_native_file_cache(const char *name);

static const char *const common_files[] = {
    "LbRb.dat", "EfMnData.dat", "LbRf.dat", "PdPm.dat", "TyDatai.usd", "PlCo.dat", "GmPause.usd",
    "SdIntro.dat", "IfCoGet.dat", "LbBf.dat", "audio/us/clink.ssm", "EfCoData.dat", "IfAll.usd", "ItCo.usd",
};
static const struct {
    int stage;
    const char *files[6];
} stage_files[] = {
    {2, {"GrIz.dat"}},
    {3, {"GrPs.usd", "audio/us/pstadium.ssm", "GrPs1.dat", "GrPs2.dat", "GrPs3.dat", "GrPs4.dat"}},
    {8, {"GrSt.dat"}},
    {28, {"GrOp.dat", "audio/us/pupupu.ssm"}},
    {31, {"GrNBa.dat"}},
    {32, {"GrNLa.dat", "audio/us/last.ssm"}},
};
/* External character id: file code, effects code, sound bank. */
static const struct {
    const char *code, *effects, *bank;
} fighters[26] = {
    {"Ca", "Ca", "captain"}, {"Dk", "Dk", "dk"}, {"Fx", "Fx", "fox"}, {"Gw", NULL, "gw"},
    {"Kb", "Kb", "kirby"}, {"Kp", "Kp", "koopa"}, {"Lk", "Lk", "link"}, {"Lg", "Lg", "luigi"},
    {"Mr", "Mr", "mario"}, {"Ms", "Ms", "mars"}, {"Mt", "Mt", "mewtwo"}, {"Ns", "Ns", "ness"},
    {"Pe", "Pe", "peach"}, {"Pk", "Pk", "pikachu"}, {"Pp", "Ic", "ice"}, {"Pr", "Pr", "purin"},
    {"Ss", "Ss", "samus"}, {"Ys", "Ys", "yoshi"}, {"Zd", "Zd", "zs"}, {"Sk", "Zd", "zs"},
    {"Fc", "Fx", "falco"}, {"Cl", "Lk", "clink"}, {"Dr", "Mr", "drmario"}, {"Fe", "Fe", "emblem"},
    {"Pc", "Pk", "pichu"}, {"Gn", "Gn", "ganon"},
};

static unsigned done_files, done_total;

static void cache(const char *name)
{
    if (mp_native_file_cache(name) == 1) {
        ++done_files;
    }
    ++done_total;
    printf("\x1b[20;1H  Preparing match files... %u     ", done_total);
}

static void fighter_core(const char *code)
{
    char name[32];
    snprintf(name, sizeof name, "Pl%s.dat", code);
    cache(name);
    snprintf(name, sizeof name, "Pl%sAJ.dat", code);
    cache(name);
}

/* Files a stage opens during the match, not at its load: Pokemon Stadium's
 * transformations (one SD open, ~300 ms, each time it changes; the console
 * then runs behind the PC). Read once the match's stage is known, which may
 * be the opponent's pick, during the match load. */
void slippi_prefetch_stage_extras(int stage)
{
    static const char *const stadium[] = {"GrPs1.dat", "GrPs2.dat", "GrPs3.dat", "GrPs4.dat"};
    unsigned i, cached = 0;
    if (stage != 3) {
        return;
    }
    for (i = 0; i < sizeof stadium / sizeof stadium[0]; i++) {
        if (mp_native_file_cache(stadium[i]) == 1) {
            ++cached;
        }
    }
    {
        extern void mp_native_log(const char *);
        extern unsigned mp_native_prefetch_bytes(void);
        char text[120];
        snprintf(text, sizeof text, "Slippi load: %u of 4 Stadium transformation files in RAM (%u KB cached)\n", cached,
                 mp_native_prefetch_bytes() / 1024);
        mp_native_log(text);
    }
}

void slippi_prefetch_match(int character, int color, int stage)
{
    unsigned i, k;
    char name[40];
    done_files = done_total = 0;
    {
        /* The SD index first: without it, files kept in subfolders are not
         * found (on the console only 2 of 20 were cached). Startup reuses it. */
        extern void mp_native_files_index(void);
        printf("\x1b[20;1H  Finding game files...");
        mp_native_files_index();
    }
    for (i = 0; i < sizeof common_files / sizeof common_files[0]; i++) {
        cache(common_files[i]);
    }
    for (i = 0; i < sizeof stage_files / sizeof stage_files[0]; i++) {
        if (stage_files[i].stage == stage) {
            for (k = 0; k < 6 && stage_files[i].files[k] != NULL; k++) {
                cache(stage_files[i].files[k]);
            }
        }
    }
    if (character >= 0 && character < 26) {
        fighter_core(fighters[character].code);
        if (fighters[character].effects != NULL) {
            snprintf(name, sizeof name, "Ef%sData.dat", fighters[character].effects);
            cache(name);
        }
        snprintf(name, sizeof name, "audio/us/%s.ssm", fighters[character].bank);
        cache(name);
        if (character == 14) {
            fighter_core("Nn");
        } else if (character == 18) {
            fighter_core("Sk");
        } else if (character == 19) {
            fighter_core("Zd");
        }
        /* Costume files (one per colour, ~0.35 MB each) are left to the match
         * load: one SD open, and the memory stays free for the match. */
        (void) color;
    }
    {
        extern void mp_native_log(const char *);
        extern unsigned mp_native_prefetch_bytes(void);
        char text[120];
        snprintf(text, sizeof text, "Slippi load: %u files (%u KB) cached in RAM before matchmaking\n", done_files,
                 mp_native_prefetch_bytes() / 1024);
        mp_native_log(text);
    }
}
