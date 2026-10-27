/* Port of src/content/q2/base/player/character.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_PLAYER_CHARACTER_H
#define QA_CONTENT_Q2_BASE_PLAYER_CHARACTER_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/content/q2/base/player/checkpoint.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/items.h"

/** TS Q2CharacterGib. */
typedef struct qa_q2_character_gib {
  const char *model;
  qa_vec3 origin, velocity, angular_velocity;
  double expires_at;
} qa_q2_character_gib;

/** TS Q2CharacterHost. weapon false return is TS null. motion kind:
 * "toss"|"bounce", solid: "box"|"none". */
typedef struct qa_q2_character_host {
  qa_shared_body_table *bodies;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  void *ctx;
  double (*now)(void *ctx);
  double (*random)(void *ctx);
  qa_q2_player_movement (*movement)(void *ctx, qa_actor_id actor);
  double (*point_contents)(void *ctx, qa_vec3 point);
  qa_q2_player_powerups (*powerups)(void *ctx, qa_actor_id actor);
  bool (*weapon)(void *ctx, qa_actor_id actor, qa_q2_character_weapon *out);
  void (*emit)(void *ctx, qa_q2_presentation_event event);
  void (*view)(void *ctx, qa_actor_id actor, qa_q2_base_player_view view);
  void (*noise)(void *ctx, qa_actor_id actor, qa_vec3 origin);
  void (*environment_damage)(void *ctx, qa_owned_actor actor, double amount, double means, double flags);
  /* The selected campaign/inventory owns death drops, scoring and
   * respawn travel. */
  void (*died)(void *ctx, qa_owned_actor actor, qa_death_reaction reaction);
  void (*request_respawn)(void *ctx, qa_owned_actor actor);
  void (*motion)(void *ctx, qa_owned_actor actor, const char *kind, const char *solid);
  void (*spawn_gib)(void *ctx, qa_q2_character_gib gib);
} qa_q2_character_host;

/** TS Q2CharacterOptions. has_edition false = TS undefined edition;
 * view_rules NULL = TS undefined. */
typedef struct qa_q2_character_options {
  bool has_edition;
  qa_q2_edition edition;
  const char *model;
  double skin, slot;
  qa_q2_mode mode;
  double deathmatch_flags;
  bool environment;
  const qa_q2_base_player_rules *view_rules;
  uint32_t view_rules_mask;
} qa_q2_character_options;

/**
 * TS Q2CharacterActor: a Q2 character on an existing actor. It
 * allocates no actor, body, arsenal or inventory. The entity is
 * owned by the actor object; state/rules are exposed mutably as in
 * TS. last_attack storage backs entity.last_attack.
 */
typedef struct qa_q2_character_actor {
  qa_owned_actor actor;
  qa_q2_character_host host;
  qa_q2_character_options options;
  qa_q2_entity entity;
  qa_attack_provenance last_attack_storage;
  qa_actor_id attack_attacker_storage, attack_inflictor_storage;
  qa_q2_base_player_state state;
  qa_q2_base_player_rules rules;
  double pain_index, death_index;
} qa_q2_character_actor;

/* NULL only on OOM. */
qa_q2_character_actor *qa_q2_character_actor_create(qa_owned_actor actor, qa_q2_character_host host, qa_q2_character_options options);
void qa_q2_character_actor_destroy(qa_q2_character_actor *character);
qa_q2_base_character_checkpoint qa_q2_character_actor_capture(qa_q2_character_actor *character);
void qa_q2_character_actor_restore(qa_q2_character_actor *character, qa_q2_base_character_checkpoint checkpoint,
                                   qa_actor_id (*resolve)(void *ctx, qa_saved_actor_id saved), void *resolve_ctx);
void qa_q2_character_actor_pain(qa_q2_character_actor *character, qa_pain_reaction reaction);
void qa_q2_character_actor_record_damage(qa_q2_character_actor *character, qa_damage_decision decision);
void qa_q2_character_actor_die(qa_q2_character_actor *character, qa_death_reaction reaction);
void qa_q2_character_actor_after_client_think(qa_q2_character_actor *character);
void qa_q2_character_actor_begin_frame(qa_q2_character_actor *character);
/* Called after selected campaign and inventory owners have respawned
 * the same actor. */
void qa_q2_character_actor_respawned(qa_q2_character_actor *character);
/* priority: "attack"|"pain"|"reverse". */
void qa_q2_character_actor_set_animation(qa_q2_character_actor *character, const char *priority, double first, double last);
void qa_q2_character_actor_end_frame(qa_q2_character_actor *character, bool intermission);

#endif
