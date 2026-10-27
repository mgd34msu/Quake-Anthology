/*
 * Port of src/input/keys.ts: Quake III cl_keys.c key-name table.
 * C strings are bytes, so single-character names use the byte
 * value where the TS uses the UTF-16 code unit (identical for
 * ASCII; see docs/PORTING.md). GPL-2.0-or-later.
 */
#ifndef QA_INPUT_KEYS_H
#define QA_INPUT_KEYS_H

/* A NULL value is the TS null. */
double qa_string_to_keynum(const char *value);
/* Malloc'd key name; caller frees. NULL only on allocation failure. */
char *qa_keynum_to_string(double key);

#endif
