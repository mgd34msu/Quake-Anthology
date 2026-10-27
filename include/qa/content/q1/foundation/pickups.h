/*
 * Port of src/content/q1/foundation/pickups.ts. GPL-2.0-or-later.
 * Foreign Q3 pickups (foreignQ3Pickup) need q3 item tables in this
 * actor model and currently resolve to no definition; supply
 * observation mirrors the native supply subset. See notes in .c.
 */
#ifndef QA_CONTENT_Q1_FOUNDATION_PICKUPS_H
#define QA_CONTENT_Q1_FOUNDATION_PICKUPS_H

#include "qa/content/q1/foundation/entity_services.h"

/* Mirrored PickupSupplyObservation subset (TS src/contracts/pickups.ts). */
typedef struct qa_q1_supply_observation {
  qa_actor_id actor;
  const char *kind; /* weapon|ammo */
  qa_item_id item;
  double amount;
  const char *availability; /* ready|respawning|inactive */
  bool eligible;
  double at_seconds;
} qa_q1_supply_observation;

bool qa_q1_observe_supply(qa_q1_services *game, qa_actor_id pickup, qa_actor_id other, qa_q1_supply_observation *out);
bool qa_q1_preview_supply(qa_q1_services *game, qa_actor_id pickup, qa_actor_id recipient, qa_q1_supply_observation *out);
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
void qa_q1_register_pickup_callbacks(qa_q1_services *game);

#endif
