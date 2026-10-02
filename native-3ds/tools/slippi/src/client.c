/* PC build of the 3DS network self-test (port/3ds/slippi/slippi_selftest.c):
 * the same code the 3DS runs at boot with selftest=1, so the client side can
 * be exercised against the fake peer before it goes into the emulator.
 *
 * Usage: slippi_client --dir DIR   (DIR has user.json and config.ini) */
#include "slippi_internal.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *dir = ".";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc) dir = argv[++i];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    sp_set_data_dir(dir);
    return slippi_selftest_run() == 0 ? 0 : 1;
}
