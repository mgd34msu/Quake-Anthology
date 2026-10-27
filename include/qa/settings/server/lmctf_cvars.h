/*
 * Port of src/settings/server/lmctf-cvars.ts (partial): the
 * LMCTF_CONSOLE_NAMES table and the cvar registration half of
 * bindLmctfConsoleRules. The other half installs live
 * getter/setters on the content module's LmctfRules object via
 * Object.defineProperty (numeric/boolean/string rules read through
 * to the registry); src/content/q2/multiplayer/lmctf/types.ts is
 * not yet ported, so that binding is not declared here. Registering
 * with the definition defaults is observably identical to binding
 * a fresh createLmctfRules() object, whose values equal every
 * definition default (that is what registerQ2ServerCvars does).
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SERVER_LMCTF_CVARS_H
#define QA_SETTINGS_SERVER_LMCTF_CVARS_H

#include <stddef.h>

#include "qa/core/cvars/index.h"

/* TS LMCTF_CONSOLE_NAMES, in source order. */
extern const char *const qa_lmctf_console_names[];
extern const size_t qa_lmctf_console_name_count;

/* Registers every LMCTF console cvar (and maplist_file) with its
 * source default and flags, as bindLmctfConsoleRules does before
 * binding the rules object. */
void qa_register_lmctf_console_cvars(qa_cvar_registry *cvars);

#endif
