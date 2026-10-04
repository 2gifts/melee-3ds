"""Places where the decomp's C does not do what the console's code does.

ftCo_800ADE48 (CPU AI, every CPU and Nana): when the fighter is in hitlag
(x221A_b3), the console's code leaves r31 ("switch_cmd") as the caller left
it. Every caller holds a pointer there (fp, &fp->cpu), so the test after it
is true and the CPU switches to behaviour 0x12, knockback DI (ftCo_800AC5A0;
DOL 800ae1f8..800ae284). The decomp's switch_cmd is uninitialised on that
path; clang took it as 0, so a hit CPU kept its old behaviour. Found from an
Ice Climbers desync (hardware run 9): Nana kept walking toward Popo in
hitlag and smash-DI'd back and forth where the console's Nana held still.
"""

FIXES = {
    'melee/ft/kinds/ftCommon/ftCo_0A01.c': [
        ('        } else {\n            data2->xF9_b0 = false;\n',
         '        } else {\n'
         '            /* The console reads the caller\'s r31 here: a pointer. */\n'
         '            switch_cmd = 1;\n'
         '            data2->xF9_b0 = false;\n', 1),
    ],
}
