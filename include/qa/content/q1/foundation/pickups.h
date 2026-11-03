/*
 * Port of src/content/q1/foundation/pickups.ts. GPL-2.0-or-later.
 * See notes in .c.
 */
#ifndef QA_CONTENT_Q1_FOUNDATION_PICKUPS_H
#define QA_CONTENT_Q1_FOUNDATION_PICKUPS_H

#include "qa/content/q1/foundation/entity_services.h"
#include "qa/contracts/pickups.h"



/* TS observeQ1Supply; false = null. A weapon offer's ammo list points at
 * game->observed_supply_ammo, valid until the next observe/preview call. */
bool qa_q1_observe_supply(qa_q1_services *game, qa_actor_id pickup, qa_actor_id other, qa_pickup_supply_observation *out);
/* TS previewQ1Supply; false = null. Release *out with
 * qa_free_pickup_supply_preview (the admission's preview when a host
 * pickup admission is installed, else previewPickupGrants). */
bool qa_q1_preview_supply(qa_q1_services *game, qa_actor_id pickup, qa_actor_id recipient, qa_pickup_supply_preview *out);
void qa_q1_ammo_pickup_selection(qa_q1_services *game, qa_q1_player_state *player, qa_q1_weapon before, bool auto_switch);
void qa_q1_weapon_pickup_selection(qa_q1_services *game, qa_q1_player_state *player, qa_q1_weapon weapon, const char *selection);
bool qa_q1_spawn_pickup(qa_q1_services *game, qa_q1_actor *entity);
bool qa_q1_give_pickup(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other);
/* TS touchQ1Pickup: continuation as fn+ctx (original/complete). */
typedef struct qa_q1_pickup_continuation {
  void *ctx;
  bool (*original)(void *ctx);
  void (*complete)(void *ctx, bool taken);
  bool (*eligible)(void *ctx); /* optional; NULL = true */
} qa_q1_pickup_continuation;
void qa_q1_touch_pickup(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, qa_item_id item,
                        const char *resource_kind, qa_q1_pickup_continuation continuation);
/* TS touchQ1Pickup(..., cargo): cargo NULL = TS undefined. */
void qa_q1_touch_pickup_cargo(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, qa_item_id item,
                              const char *resource_kind, qa_q1_pickup_continuation continuation,
                              const qa_pickup_cargo_entry *cargo, size_t cargo_count);
void qa_q1_register_pickup_callbacks(qa_q1_services *game);
/* TS composition/q1/q3-pickups.ts foreignQ3Pickup: Q3-authored items on a Q1
 * primary, as a game->foreign_pickup hook (installed by
 * qa_comp_q1_register_q3_pickup_hook). The definition it fills is private to
 * pickups.c, whose take dispatch carries the Q3 grant kinds, so the code stays
 * there; only the composition decides whether a game reaches it. */
bool qa_q1_foreign_q3_pickup(void *ctx, const qa_q1_actor *entity, struct qa_q1_pickup_def *out);

#endif
