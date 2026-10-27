/*
 * Port of src/settings/server/q1.ts: Q1 menu.c GameOptions teamplay
 * choices (Rogue adds its source objective modes). The returned
 * collection points at static definitions; do not free. `program`
 * is "standard" | "rogue" | "ctf" (anything else is the standard
 * choice list, as the TS comparisons fall through).
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SERVER_Q1_H
#define QA_SETTINGS_SERVER_Q1_H

#include "qa/settings/server/types.h"

qa_server_setting_collection qa_q1_match_settings(const char *program);

#endif
