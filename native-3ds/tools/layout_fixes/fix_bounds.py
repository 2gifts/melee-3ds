"""Out-of-bounds neighbours that are not a named object at all."""
FIXES = {
    # The car insertion sort compared (and could swap) arr[0] with arr[-1],
    # the word before the array: on PPC the last Brinstar bubble word, on ARM
    # unrelated data used as a car index. Stop at the first element.
    'melee/gr/grmutecity.c': [
        ('        for (j = i; j >= 0; j--) {\n            s32 temp = arr[j];',
         '        for (j = i; j > 0; j--) {\n            s32 temp = arr[j];', 1),
    ],
}
