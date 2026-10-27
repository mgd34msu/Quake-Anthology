/*
 * Port of src/core/cvars/numbers.ts: Quake's Q_atof and Quake III's
 * Cvar_SetValue float-to-text. C accommodations: text is a
 * NUL-terminated byte string (TS charAt past the end yields "", whose
 * charCode reads act like the C NUL here); the result of
 * qa_cvar_value_text is a malloc'd copy, NULL only on allocation
 * failure. The TS BigInt exact-rounding is reproduced with a small
 * fixed bignum; bits come from the foundation binary helpers (the TS
 * imports float32ToBits, which is foundation float32Bits).
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_CVARS_NUMBERS_H
#define QA_CORE_CVARS_NUMBERS_H

#include <stdbool.h>

double qa_quake_atof(const char *text);
/* C-locale %f of the binary32 value: six decimals, ties to even.
 * Aborts (RangeError) when the rounded float is not finite. */
char *qa_cvar_value_text(double input, bool integer_shortcut);

#endif
