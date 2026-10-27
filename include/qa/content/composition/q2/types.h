/*
 * Port of src/content/composition/q2/types.ts (types only).
 * Mirrored types from unported modules (opaque or partial,
 * collision-proof qa_comp_q2_* names; TS owners noted inline).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q2_TYPES_H
#define QA_CONTENT_COMPOSITION_Q2_TYPES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/equipment.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/core/cvars/index.h"

/* Q2ClassicProgram | "mg2" | "n64" (Q2RereleaseProgram), one enum. */
typedef enum qa_comp_q2_program {
  QA_COMP_Q2_BASEQ2, QA_COMP_Q2_XATRIX, QA_COMP_Q2_ROGUE, QA_COMP_Q2_MG2, QA_COMP_Q2_N64
} qa_comp_q2_program;
const char *qa_comp_q2_program_name(qa_comp_q2_program program);

/* LmctfTravel (src/content/q2/multiplayer/lmctf/types.ts): only its
 * rules pointer is forwarded by composition. */
typedef struct qa_comp_lmctf_travel {
  const void *rules;
  const void *rest; /* remaining travel payload, forwarded whole */
} qa_comp_lmctf_travel;

/* TS Q2MatchSelection. DeathBall skins/goal limit are live cvar reads
 * when cvars is set (match-selection.ts getters); otherwise the stored
 * values apply. Access through the qa_comp_q2_match_selection_* fns. */
typedef enum qa_comp_q2_match_kind {
  QA_COMP_Q2_MATCH_CTF, QA_COMP_Q2_MATCH_LMCTF, QA_COMP_Q2_MATCH_STANDARD,
  QA_COMP_Q2_MATCH_TAG, QA_COMP_Q2_MATCH_HORDE, QA_COMP_Q2_MATCH_DEATHBALL
} qa_comp_q2_match_kind;
const char *qa_comp_q2_match_kind_name(qa_comp_q2_match_kind kind);
typedef struct qa_comp_q2_match_selection {
  qa_comp_q2_match_kind kind;
  bool has_travel;
  const qa_comp_lmctf_travel *travel;
  qa_cvar_registry *cvars; /* deathball live getters; NULL = stored values */
  const char *team1_skin;
  const char *team2_skin;
  double goal_limit;
} qa_comp_q2_match_selection;
/* Malloc'd for the cvar-backed form, borrowed otherwise; see _skin_free. */
const char *qa_comp_q2_match_selection_team1_skin(const qa_comp_q2_match_selection *selection, bool *owned);
const char *qa_comp_q2_match_selection_team2_skin(const qa_comp_q2_match_selection *selection, bool *owned);
double qa_comp_q2_match_selection_goal_limit(const qa_comp_q2_match_selection *selection);

/* Opaque event payloads (TS owners: q2/multiplayer/ctf/types.ts,
 * q2/multiplayer/lmctf/types.ts, q2/missionpacks/types.ts,
 * q2/missionpacks/entities/types.ts). */
typedef struct qa_comp_q2_ctf_event qa_comp_q2_ctf_event;
typedef struct qa_comp_lmctf_event qa_comp_lmctf_event;
typedef struct qa_comp_q2_missionpack_player_effect qa_comp_q2_missionpack_player_effect;
typedef struct qa_comp_q2_missionpack_entity_event qa_comp_q2_missionpack_entity_event;

typedef enum qa_comp_q2_event_kind {
  QA_COMP_Q2_EVENT_CTF, QA_COMP_Q2_EVENT_LMCTF, QA_COMP_Q2_EVENT_GRAPPLE_PREDICTION,
  QA_COMP_Q2_EVENT_KICK, QA_COMP_Q2_EVENT_MISSIONPACK_PLAYER, QA_COMP_Q2_EVENT_MISSIONPACK_ENTITY
} qa_comp_q2_event_kind;
typedef struct qa_comp_q2_event {
  qa_comp_q2_event_kind kind;
  const qa_comp_q2_ctf_event *ctf_event;
  const qa_comp_lmctf_event *lmctf_event;
  const qa_comp_q2_missionpack_player_effect *player_effect;
  const qa_comp_q2_missionpack_entity_event *entity_event;
  qa_actor_id actor;
  bool suppressed;
} qa_comp_q2_event;

typedef struct qa_comp_q2_powerups {
  double quad_until;
  double double_until;
  double invulnerability_until;
} qa_comp_q2_powerups;

/** TS Q2CompositionServices as fn+ctx. Optional members are NULL-able. */
typedef struct qa_comp_q2_services {
  void *ctx;
  bool has_deathmatch_flags;
  double (*flags_read)(void *ctx);
  void (*flags_write)(void *ctx, double flags);
  const qa_shared_grapple_control *shared_grapple;
  /* Q2RandomItemSettings (q2/missionpacks/random-items.ts), opaque. */
  const void *(*random_items)(void *ctx);
  bool (*drop_quad_fire)(void *ctx);
  double (*gravity)(void *ctx);
  void (*emit)(void *ctx, const qa_comp_q2_event *event);
  bool hunter_camera;
  bool strong_mines;
  qa_comp_q2_powerups (*foreign_powerups)(void *ctx, qa_actor_id actor);
} qa_comp_q2_services;

/* Q2FoundationHost / Q2GameOptions minus edition (q2/foundation/host.ts):
 * only the fields composition reads are mirrored. */
typedef struct qa_comp_q2_game_options {
  const char *mode; /* "deathmatch" | ... */
  double deathmatch_flags;
  qa_provider_id provider;
} qa_comp_q2_game_options;

/* Player movement commands (playerHooks.setMovement argument). */
typedef struct qa_comp_q2_movement_command {
  const char *kind; /* "noclip" | "teleport" */
  bool enabled;     /* noclip */
  qa_vec3 origin, angles, velocity, command_angles; /* teleport */
  double hold_milliseconds;
  bool spectator;
} qa_comp_q2_movement_command;

/* Q2WeaponInput (q2/foundation/weapons): only haste is adjusted here;
 * the remaining fields ride in `rest` untouched. */
typedef struct qa_comp_q2_weapon_input {
  bool haste;
  const void *rest;
} qa_comp_q2_weapon_input;

struct qa_comp_q2_entity;
struct qa_comp_q2_game_services;
/* Q2PlayerHooks surface used by composition (q2/base/player). Optional
 * members are NULL-able. score's recipient/change/means follow TS. */
typedef struct qa_comp_q2_player_hooks {
  void *ctx;
  void (*set_movement)(void *ctx, qa_actor_id actor, qa_comp_q2_movement_command command);
  qa_vec3 (*movement_command_angles)(void *ctx, qa_actor_id actor);
  qa_comp_q2_weapon_input (*weapon_input)(void *ctx, qa_actor_id actor);
  void (*diagnostic)(void *ctx, const char *text); /* host.diagnostic */
  void (*score)(void *ctx, struct qa_comp_q2_entity *victim, struct qa_comp_q2_entity *attacker,
                struct qa_comp_q2_game_services *game, double change, double means,
                struct qa_comp_q2_entity *recipient);
  void (*player_spawned)(void *ctx, struct qa_comp_q2_entity *entity, struct qa_comp_q2_game_services *game);
  struct qa_comp_q2_entity *(*select_spawn)(void *ctx, struct qa_comp_q2_entity *entity,
                                            struct qa_comp_q2_game_services *game);
  bool (*command)(void *ctx, struct qa_comp_q2_entity *entity, struct qa_comp_q2_game_services *game,
                  const char *name, const char *const *args, size_t arg_count);
  void (*before_death_inventory)(void *ctx, struct qa_comp_q2_entity *entity,
                                 struct qa_comp_q2_game_services *game, const qa_attack_provenance *attack);
  void (*death)(void *ctx, struct qa_comp_q2_entity *entity, struct qa_comp_q2_game_services *game,
                const qa_attack_provenance *attack);
  void (*disconnect)(void *ctx, struct qa_comp_q2_entity *entity, struct qa_comp_q2_game_services *game);
  double (*quad_fire_drop_until_base)(void *ctx, qa_actor_id actor); /* forwarded, unused here */
} qa_comp_q2_player_hooks;

/* Q2ItemHooks surface used here (q2/foundation/items.ts); `rest` is
 * forwarded to the item-module constructor unchanged. */
typedef struct qa_comp_q2_item_hooks {
  void *ctx;
  const void *rest;
  bool (*weapon_picked)(void *ctx, qa_actor_id actor, qa_item_id item, bool first);
  void (*ammo_pack)(void *ctx, qa_actor_id actor, struct qa_comp_q2_game_services *game, bool full);
} qa_comp_q2_item_hooks;

/* Q2RereleaseHooks surface used here (q2/rerelease/types.ts). */
typedef struct qa_comp_q2_rerelease_hooks {
  void *ctx;
  const void *rest;
  bool (*monster_holds_health_bar)(void *ctx, qa_actor_id actor);
  void (*clear_expansion_powerups)(void *ctx, qa_actor_id actor);
} qa_comp_q2_rerelease_hooks;

/* TS Q2CompositionOptions flattened: edition is "classic" | "rerelease";
 * rerelease-only members are ignored for classic. host/weapons and the
 * rules/options/campaign payloads stay opaque forwards. */
typedef struct qa_comp_q2_options {
  const char *edition;
  qa_comp_q2_program program;
  void *host;
  qa_comp_q2_game_options options;
  void *weapons;
  qa_comp_q2_item_hooks item_hooks;
  qa_comp_q2_player_hooks player_hooks;
  const void *player_rules;    /* Partial<Q2PlayerRules>, optional */
  bool has_match;
  qa_comp_q2_match_selection match;
  const void *entity_hooks;    /* Pick<Q2BaseEntityHooks, ...> */
  qa_comp_q2_services services;
  qa_comp_q2_rerelease_hooks rerelease_hooks;
  const void *rerelease_options; /* Partial<Q2RereleaseOptions>, optional */
  const void *campaign;          /* Q2RereleaseCampaignState, optional */
} qa_comp_q2_options;

#endif
