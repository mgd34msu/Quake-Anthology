/*
 * Port of src/settings/server/lmctf-cvars.ts. TS installs live accessors on the
 * LmctfRules object; a C struct has none, so the registry stays the owner of the
 * bound values: qa_lmctf_rules_pull_cvars is the TS getter side, called before
 * the rules are read, and qa_lmctf_rules_set_* is the setter side.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SERVER_LMCTF_CVARS_H
#define QA_SETTINGS_SERVER_LMCTF_CVARS_H

#include <stddef.h>

#include "qa/content/q2/multiplayer/lmctf/types.h"
#include "qa/core/cvars/index.h"

/* TS LMCTF_CONSOLE_NAMES, in source order. */
extern const char *const qa_lmctf_console_names[];
extern const size_t qa_lmctf_console_name_count;

/* Registers every LMCTF console cvar (and maplist_file) with its
 * source default and flags, as bindLmctfConsoleRules does before
 * binding the rules object. */
void qa_register_lmctf_console_cvars(qa_cvar_registry *cvars);

/* TS bindLmctfConsoleRules: registers each cvar, seeds a cvar that did not exist
 * from a non-default rule value, then pulls the registry into rules. */
void qa_bind_lmctf_console_rules(qa_cvar_registry *cvars, qa_lmctf_rules *rules);
void qa_lmctf_rules_pull_cvars(qa_cvar_registry *cvars, qa_lmctf_rules *rules);
/* Setter side, by console name; numeric and boolean rules take the TS String(value). */
void qa_lmctf_rules_set_number(qa_cvar_registry *cvars, qa_lmctf_rules *rules, const char *name, double value);
void qa_lmctf_rules_set_string(qa_cvar_registry *cvars, qa_lmctf_rules *rules, const char *name, const char *value);

#endif
