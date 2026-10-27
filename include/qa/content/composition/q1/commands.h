/*
 * Port of src/content/composition/q1/commands.ts: weapons.qc
 * W_ChangeWeapon, CycleWeaponCommand and base ImpulseCommands.
 * Mirrored surfaces from unported modules (collision-proof names):
 *  - qa_comp_q1_game: the Q1EntityServices/Q1Foundation calls this
 *    module's files make (TS owners: src/content/q1/foundation/
 *    entity-services.ts and runtime.ts), as one fn+ctx table.
 *  - qa_comp_q1_player_state: Q1PlayerState (src/content/q1/foundation/
 *    types.ts), only the fields composition reads or writes.
 *  - QA_COMP_Q1_WEAPONS: WEAPONS (src/content/q1/foundation/types.ts).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q1_COMMANDS_H
#define QA_CONTENT_COMPOSITION_Q1_COMMANDS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/composition/q1/types.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/pickups.h"

extern const char *const QA_COMP_Q1_WEAPONS[8];
#define QA_COMP_Q1_WEAPON_COUNT 8

/* Mirrored Q1PlayerState fields used by composition. weapon is a
 * Q1BaseWeapon name ("axe", ...) or NULL. */
typedef struct qa_comp_q1_player_state {
  qa_owned_actor actor;
  const char *weapon;
  double max_health;
  double attack_finished;
  bool attack_held;
  bool jump_held;
  double teleport_until;
} qa_comp_q1_player_state;

/* combat.setRegularArmor argument: kind "none" or "q1". */
typedef struct qa_comp_q1_armor {
  const char *kind;
  double points;
  double absorption;
  qa_item_id item;
} qa_comp_q1_armor;

typedef struct qa_comp_q1_game {
  void *ctx;
  const char *edition; /* "classic" | "rerelease" */
  double deathmatch;
  bool coop;
  qa_item_id (*weapon_item)(void *ctx, const char *weapon);
  qa_item_id (*weapon_ammo)(void *ctx, const char *weapon); /* NULL = TS null */
  double (*inventory_count)(void *ctx, qa_actor_id actor, qa_item_id item);
  const qa_inventory_entry *(*inventory_entries)(void *ctx, qa_actor_id actor, size_t *out_count);
  void (*inventory_configure)(void *ctx, qa_owned_actor actor, qa_inventory_entry entry);
  void (*message)(void *ctx, qa_actor_id actor, const char *text, bool center,
                  const char *const *args, size_t arg_count);
  bool (*select_weapon)(void *ctx, qa_owned_actor actor, const char *weapon);
  void (*give_powerup)(void *ctx, qa_comp_q1_player_state *player, const char *kind);
  qa_comp_q1_player_state *(*player)(void *ctx, qa_actor_id actor);
  const char *const *(*registered_weapons)(void *ctx, size_t *out_count);
  const qa_pickup_admission *pickup_admission; /* NULL = TS null */
  void (*set_health)(void *ctx, qa_owned_actor actor, double health);
  void (*set_armor)(void *ctx, qa_owned_actor actor, qa_comp_q1_armor armor);
  /* Entity lifecycle used by give.ts. */
  qa_comp_q1_actor *(*create)(void *ctx, const char *classname);
  void (*spawn_entity)(void *ctx, qa_comp_q1_actor *item);
  bool (*live)(void *ctx, qa_comp_q1_actor *item);
  void (*cancel)(void *ctx, qa_comp_q1_actor *item);
  void (*remove)(void *ctx, qa_comp_q1_actor *item);
  qa_owned_actor (*actor_owned)(void *ctx, qa_comp_q1_actor *item);
  bool (*actor_has_touch)(void *ctx, qa_comp_q1_actor *item);
  void (*actor_set_solid)(void *ctx, qa_comp_q1_actor *item, const char *solid);
  void (*callbacks_touch)(void *ctx, qa_owned_actor self, qa_actor_id other);
} qa_comp_q1_game;

/* The Q1SourceComposition fields commands.ts destructures; campaign is
 * base.campaign (the runtime forwards it to the selection binding). */
typedef struct qa_comp_q1_impulse_source {
  qa_comp_q1_game *game;
  const qa_comp_q1_services *services;
  qa_comp_q1_program program;
  qa_comp_q1_campaign_binding campaign;
} qa_comp_q1_impulse_source;

/** TS q1WeaponImpulse(). */
bool qa_comp_q1_weapon_impulse(qa_comp_q1_game *game, qa_comp_q1_player_state *player, double impulse);
/** TS baseQ1Impulse(). */
bool qa_comp_base_q1_impulse(const qa_comp_q1_impulse_source *source, qa_comp_q1_player_state *player,
                             double impulse);

#endif
