/*
 * Port of src/settings/server/q2-owner.ts. TS bindQ2PlayerCvars and
 * bindQ2ServerCvars replace rule properties with cvar-backed accessors; the
 * C rule structs are plain values, so a binding object keeps the cvar side
 * (the needpass hook and string storage) and pull/push copy between the
 * structs and the cvars: pull is every TS getter, push is every TS setter.
 * Q2PlayerRules defaults used by registration (timeLimitMinutes 0,
 * fragLimit 0) and the Q2CtfRules captureLimit default (0) are literals from
 * the content types files.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SERVER_Q2_OWNER_H
#define QA_SETTINGS_SERVER_Q2_OWNER_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q2/missionpacks/random_items.h"
#include "qa/content/q2/multiplayer/lmctf/types.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/core/cvars/index.h"
#include "qa/content/q2/base/player/types.h"
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

/* TS q2RereleaseItemServices: for the q2-rerelease dialect, randomItems and
 * dropQuadFire read the cvars on each call; other dialects get neither. */
typedef struct qa_q2_rerelease_item_state {
  qa_cvar_registry *cvars;
} qa_q2_rerelease_item_state;
bool qa_q2_rerelease_item_services(qa_cvar_registry *cvars, qa_q2_rerelease_item_state *out);
/* Q2CompositionServices.randomItems / dropQuadFire with ctx = the services. */
qa_q2mp_random_item_settings qa_q2_rerelease_random_items(void *ctx);
bool qa_q2_rerelease_drop_quad_fire(void *ctx);

/* TS bindQ2PlayerCvars / bindQ2ServerCvars state. Rule string fields point
 * into this storage after a pull. */
typedef struct qa_q2_cvar_binding {
  qa_cvar_registry *cvars;
  qa_q2_base_player_rules *rules;
  qa_q2_rerelease_options *rerelease; /* NULL: classic map list */
  qa_lmctf_rules *lmctf;              /* bindQ2ServerCvars: Q2Lmctf mode */
  double *capture_limit;              /* bindQ2ServerCvars: Q2Ctf mode rules */
  bool server;                        /* timelimit/fraglimit bound */
  int64_t password_bindings[2];
  char *password, *spectator_password;
  char **map_list; size_t map_list_count;
} qa_q2_cvar_binding;
/* bindQ2PlayerCvars: installs the needpass hook, sets needpass, and pulls. */
void qa_bind_q2_player_cvars(qa_q2_cvar_binding *binding, qa_cvar_registry *cvars, qa_q2_base_player_rules *rules,
                             qa_q2_rerelease_options *rerelease);
/* bindQ2ServerCvars: the player binding, then LMCTF console rules or the
 * timelimit/fraglimit accessors, and CTF captureLimit. */
void qa_bind_q2_server_cvars(qa_q2_cvar_binding *binding, qa_cvar_registry *cvars, qa_q2_base_player_rules *rules,
                             qa_q2_rerelease_options *rerelease, qa_lmctf_rules *lmctf, double *capture_limit);
void qa_q2_cvar_binding_pull(qa_q2_cvar_binding *binding);
void qa_q2_cvar_binding_push(qa_q2_cvar_binding *binding);
void qa_q2_cvar_binding_destroy(qa_q2_cvar_binding *binding);

#endif
