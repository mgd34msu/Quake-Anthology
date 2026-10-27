/*
 * Port of src/settings/server/q2-owner.ts (partial): the rerelease
 * deathmatch-flag projection, saved-cvar hydration, the setting
 * collection selection, and server cvar registration. Not ported:
 * q2RereleaseItemServices (returns content-module
 * Q2CompositionServices closures), bindQ2ServerCvars and
 * bindQ2PlayerCvars (install Object.defineProperty bindings on the
 * content module's Q2PlayerRules/Q2RereleaseOptions objects and on
 * Q2ProductRuntime mode instances). The content tree is not yet
 * ported, so those three are not declared here. Q2PlayerRules
 * defaults used by registration (timeLimitMinutes 0, fragLimit 0)
 * and the Q2CtfRules captureLimit default (0) are literals from
 * the content types files.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SERVER_Q2_OWNER_H
#define QA_SETTINGS_SERVER_Q2_OWNER_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/core/cvars/index.h"
#include "qa/persistence/value.h"
#include "qa/settings/server/types.h"

/* TS q2SourceDeathmatchFlags(cvars): Math.trunc(dmflags), with the
 * rerelease g_dm_* bits folded in for the q2-rerelease dialect. */
double qa_q2_source_deathmatch_flags(qa_cvar_registry *cvars);

/* TS savedQ2ServerCvarsHave / restoreQ2ServerCvars over the TS
 * `unknown` save state (a qa_save_value tree). Invalid states abort
 * as the TS restore throws. */
bool qa_saved_q2_server_cvars_have(qa_cvar_registry *cvars,
                                   const qa_save_value *value, const char *name);
void qa_restore_q2_server_cvars(qa_cvar_registry *cvars,
                                const qa_save_value *value);

/* TS q2ServerSettingCollections(match, q2Combat, rerelease = false).
 * Fills out[] (capacity 6) and returns the collection count. */
size_t qa_q2_server_setting_collections(
    const char *match, bool q2_combat, bool rerelease,
    qa_server_setting_collection out[6]);

/* TS registerQ2ServerCvars(cvars, match). The q2:lmctf branch
 * registers the LMCTF console cvars (binding a fresh rules object
 * has no further observable registry effect). */
void qa_register_q2_server_cvars(qa_cvar_registry *cvars, const char *match);

#endif
