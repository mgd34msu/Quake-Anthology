/*
 * Port of src/content/composition/q2/types.ts. The composition options
 * reference the real Q2 types: Q2FoundationHost (qa_q2_foundation_host),
 * Q2GameOptions (qa_q2_game_options), Q2Weapons (qa_q2w_weapons),
 * Q2ItemHooks, Q2PlayerHooks / Q2PlayerRules (base/player/types.h),
 * Q2BaseEntityHooks, Q2RereleaseHooks / Q2RereleaseOptions /
 * Q2RereleaseCampaignState, and the CTF / LMCTF / mission-pack event
 * types. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q2_TYPES_H
#define QA_CONTENT_COMPOSITION_Q2_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q2/base/entities/types.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/content/q2/missionpacks/entities/types.h"
#include "qa/content/q2/missionpacks/random_items.h"
#include "qa/content/q2/missionpacks/types.h"
#include "qa/content/q2/multiplayer/ctf/types.h"
#include "qa/content/q2/multiplayer/lmctf/types.h"
#include "qa/content/q2/rerelease/campaign.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/contracts/equipment.h"
#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"

/* TS Q2ClassicProgram ("baseq2"|"xatrix"|"rogue") and
 * Q2RereleaseProgram (adds "mg2"|"n64"), in that order. */
typedef enum qa_q2_program {
  QA_Q2_PROGRAM_BASEQ2, QA_Q2_PROGRAM_XATRIX, QA_Q2_PROGRAM_ROGUE, QA_Q2_PROGRAM_MG2, QA_Q2_PROGRAM_N64
} qa_q2_program;
const char *qa_q2_program_name(qa_q2_program program);

/* TS Q2MatchSelection kinds. */
typedef enum qa_q2_match_kind {
  QA_Q2_MATCH_CTF, QA_Q2_MATCH_LMCTF, QA_Q2_MATCH_STANDARD, QA_Q2_MATCH_TAG, QA_Q2_MATCH_HORDE,
  QA_Q2_MATCH_DEATHBALL
} qa_q2_match_kind;
const char *qa_q2_match_kind_name(qa_q2_match_kind kind);

/** TS Q2MatchSelection. lmctf: travel NULL = TS undefined (borrowed).
 * deathball: when cvars is set the three fields are the live getters of
 * sourceQ2MatchSelection (dball_team1_skin, dball_team2_skin, goallimit);
 * otherwise the stored values. Read them through the accessors below. */
typedef struct qa_q2_match_selection {
  qa_q2_match_kind kind;
  const qa_lmctf_travel *travel;
  qa_cvar_registry *cvars;
  const char *team1_skin, *team2_skin;
  double goal_limit;
} qa_q2_match_selection;
/* The returned skin is malloc'd (free it). */
char *qa_q2_match_selection_team1_skin(const qa_q2_match_selection *selection);
char *qa_q2_match_selection_team2_skin(const qa_q2_match_selection *selection);
double qa_q2_match_selection_goal_limit(const qa_q2_match_selection *selection);

/** TS Q2CompositionEvent union on kind. Only the member of `kind` applies;
 * event payloads are borrowed for the emit call. */
typedef enum qa_q2_composition_event_kind {
  QA_Q2_COMPOSITION_EVENT_CTF, QA_Q2_COMPOSITION_EVENT_LMCTF, QA_Q2_COMPOSITION_EVENT_GRAPPLE_PREDICTION,
  QA_Q2_COMPOSITION_EVENT_KICK, QA_Q2_COMPOSITION_EVENT_MISSIONPACK_PLAYER, QA_Q2_COMPOSITION_EVENT_MISSIONPACK_ENTITY
} qa_q2_composition_event_kind;
typedef struct qa_q2_composition_event {
  qa_q2_composition_event_kind kind;
  const qa_q2_ctf_event *ctf;
  const qa_lmctf_event *lmctf;
  qa_actor_id actor;     /* grapple-prediction, kick */
  bool suppressed;       /* grapple-prediction */
  const qa_q2mp_player_effect *player_effect;
  const qa_q2mp_entity_event *entity_event;
} qa_q2_composition_event;

/** foreignPowerups() result. */
typedef struct qa_q2_composition_powerups {
  double quad_until, double_until, invulnerability_until;
} qa_q2_composition_powerups;

/** TS Q2CompositionServices. Optional TS members are NULL-able function
 * pointers; deathmatch_flags_read/write together are TS deathmatchFlags. */
typedef struct qa_q2_composition_services {
  void *ctx;
  double (*deathmatch_flags_read)(void *ctx);
  void (*deathmatch_flags_write)(void *ctx, double flags);
  qa_shared_grapple_control *shared_grapple; /* optional, borrowed */
  qa_q2mp_random_item_settings (*random_items)(void *ctx); /* optional */
  bool (*drop_quad_fire)(void *ctx);                        /* optional */
  double (*gravity)(void *ctx);
  void (*emit)(void *ctx, qa_q2_composition_event event);
  bool hunter_camera;
  bool strong_mines;
  qa_q2_composition_powerups (*foreign_powerups)(void *ctx, qa_actor_id actor);
} qa_q2_composition_services;

/** TS Pick<Q2BaseEntityHooks, "playerPush" | "setActorGravity" | "localTime">. */
typedef struct qa_q2_composition_entity_hooks {
  void *ctx;
  void (*player_push)(void *ctx, qa_actor_id player, qa_vec3 velocity);
  void (*set_actor_gravity)(void *ctx, qa_actor_id actor, double gravity);
  qa_q2_base_local_time (*local_time)(void *ctx);
} qa_q2_composition_entity_hooks;

/** TS Q2CompositionOptions (Q2CompositionCommon & the edition union).
 * options.edition is ignored (TS Omit<Q2GameOptions, "edition">); edition
 * selects the union member. player_rules_mask 0 = TS undefined playerRules
 * (QA_Q2_RULE_* bits, base/player/types.h). match NULL = TS undefined.
 * The rerelease_* members and campaign apply to the rerelease edition only;
 * rerelease_options NULL and campaign NULL are TS undefined. host, weapons,
 * match, rerelease_options and campaign are borrowed and must outlive the
 * runtime. */
typedef struct qa_q2_composition_options {
  qa_q2_edition edition;
  qa_q2_program program;
  qa_q2_foundation_host *host;
  qa_q2_game_options options;
  qa_q2w_weapons *weapons;
  qa_q2_item_hooks item_hooks;
  qa_q2_player_hooks player_hooks;
  qa_q2_base_player_rules player_rules;
  uint32_t player_rules_mask;
  const qa_q2_match_selection *match;
  qa_q2_composition_entity_hooks entity_hooks;
  qa_q2_composition_services services;
  qa_q2_rerelease_hooks rerelease_hooks;
  const qa_q2_rerelease_options_changes *rerelease_options;
  qa_q2_rerelease_campaign_state *campaign;
} qa_q2_composition_options;

#endif
