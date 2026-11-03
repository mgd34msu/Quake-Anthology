/* Port of src/world/session/mod-callback-inputs.ts: the named inputs a declared callback may
 * read, its common inputs plus those its destination operation supplies. QuakeC and QVM mods
 * validate against this one table. TS returns a Set; C answers membership and lists the set in
 * TS insertion order. GPL-2.0-or-later. */
#ifndef QA_WORLD_SESSION_MOD_CALLBACK_INPUTS_H
#define QA_WORLD_SESSION_MOD_CALLBACK_INPUTS_H
#include <stdbool.h>
#include <stddef.h>
/* Writes up to capacity names (static text) into out in Set insertion order; returns the set size (at most 10). */
size_t qa_mod_callback_inputs(const char *stage, const char *operation, const char **out, size_t capacity);
bool qa_mod_callback_input_available(const char *stage, const char *operation, const char *name);
#endif
