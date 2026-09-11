/* The matching decomp uses an int-sized boolean. Every engine include must
 * agree, including headers that include stdbool.h directly. libctru uses the
 * compiler's ordinary C boolean in separate translation units. */
#ifdef MP_GAME_ABI
#include <MSL/stdbool.h>
#else
#include_next <stdbool.h>
#endif
