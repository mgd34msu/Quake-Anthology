/* Port of src/content/q3/base/combat-bridge.ts. GPL-2.0-or-later.
 * The landed game/combat context is a simplified mirror: TS live
 * getters become a refresh-on-access context pointer, the TS attack()
 * and dispatch() closures become bridge functions, and the TS
 * q3DamageFeedback/q3ForeignDamageFeedback exports (not in the landed
 * combat header) are host callbacks. Progress-wrapper adapter ctxs
 * are malloc'd per resume and intentionally not reclaimed. */
#ifndef QA_CONTENT_Q3_BASE_COMBAT_BRIDGE_H
#define QA_CONTENT_Q3_BASE_COMBAT_BRIDGE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/combat.h"
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/game/use_participant.h"
#include "qa/content/q3/base/records.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/world.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/persistence/value.h"
#include "qa/world/gameplay/armor.h"
#include "qa/world/gameplay/authority.h"

/* TS Q3CombatBridgeHost. debug_damage NULL fn is TS null; the three
 * missionpack callbacks are only used for the missionpack product.
 * apply_damage/is_live fill the landed combat context's simplified
 * authority surface. */
typedef struct qa_q3_base_combat_bridge_host {
  qa_q3_base_product product;
  qa_gameplay_authority *authority;
  qa_q3_entity_pool *entities;
  qa_q3_base_entity_records *records;
  qa_q3_base_server_world *world;
  qa_q3_base_actor_spatial_queries *spatial;
  qa_provider_id weapon_provider, combat_provider, inventory_provider, movement_provider;
  bool has_damage_powerup_owner; qa_provider_id damage_powerup_owner;
  bool has_source_damage_modifier; qa_source_damage_modifier source_damage_modifier;
  void *ctx;
  qa_victim_armor_context (*armor_context)(void *ctx, qa_damage_request request);
  double (*time)(void *ctx);
  double (*intermission_queued)(void *ctx);
  double (*game_type)(void *ctx);
  bool (*friendly_fire)(void *ctx);
  double (*knockback)(void *ctx);
  void (*debug_damage)(void *ctx, qa_q3_damage_diagnostic diagnostic);
  void (*check_hurt_carrier)(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker);
  bool (*log_accuracy_hit)(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker);
  bool (*is_live)(void *ctx, qa_actor_id actor);
  void (*apply_damage)(void *ctx, qa_actor_id target, double amount, double knockback,
      qa_vec3 direction, qa_vec3 point, bool radius);
  void (*damage_feedback)(void *ctx, qa_q3_damage_call call, qa_damage_decision decision);
  void (*foreign_damage_feedback)(void *ctx, qa_q3_game_entity *target,
      qa_q3_game_entity *attacker, qa_damage_decision decision);
  bool (*projectile_parent)(void *ctx, qa_actor_id actor, qa_actor_id *out);
  bool (*check_obelisk_attack)(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker);
  void (*invulnerability_effect)(void *ctx, qa_q3_game_entity *target, qa_vec3 direction, qa_vec3 point);
} qa_q3_base_combat_bridge_host;

/* TS captureSaveState() result shape. */
typedef struct qa_q3_base_combat_bridge_save_state { double sequence; } qa_q3_base_combat_bridge_save_state;

typedef struct qa_q3_base_combat_bridge {
  qa_q3_base_combat_bridge_host host;
  qa_q3_combat_context context;
  qa_q3_damage_call *calls; size_t call_count, call_capacity;
  double sequence;
  qa_actor_id provenance_actors[16]; size_t provenance_index;
} qa_q3_base_combat_bridge;
void qa_q3_base_combat_bridge_init(qa_q3_base_combat_bridge *bridge, qa_q3_base_combat_bridge_host host);
void qa_q3_base_combat_bridge_destroy(qa_q3_base_combat_bridge *bridge);
qa_q3_base_combat_bridge_save_state qa_q3_base_combat_bridge_capture_save_state(qa_q3_base_combat_bridge *bridge);
void qa_q3_base_combat_bridge_restore_save_state(qa_q3_base_combat_bridge *bridge, const qa_save_value *value);
/* TS readonly context: refreshed from the host getters on each call. */
qa_q3_combat_context *qa_q3_base_combat_bridge_context(qa_q3_base_combat_bridge *bridge);
/* TS currentCall: false is TS null. */
bool qa_q3_base_combat_bridge_current_call(qa_q3_base_combat_bridge *bridge, qa_q3_damage_call *out);
/* TS dispatch() stack halves. */
void qa_q3_base_combat_bridge_begin_call(qa_q3_base_combat_bridge *bridge, qa_q3_damage_call call);
void qa_q3_base_combat_bridge_end_call(qa_q3_base_combat_bridge *bridge);
/* TS context.attack(). weapon NULL picks the TS fallback chain;
 * has_projectile false is TS undefined. Provenance actor pointers
 * borrow bridge ring storage (16 slots). */
qa_attack_provenance qa_q3_base_combat_bridge_attack(qa_q3_base_combat_bridge *bridge,
    qa_q3_damage_participant inflictor, qa_q3_use_participant attacker, qa_item_id weapon,
    double means_of_death, double flags, bool has_projectile, qa_actor_id originating_projectile);
void qa_q3_base_combat_bridge_before_reaction(qa_q3_base_combat_bridge *bridge, qa_damage_decision decision);
/* TS policy(). The returned policy's ctx is malloc'd; release it with
 * qa_q3_base_combat_bridge_policy_destroy when unregistered. */
qa_combat_policy qa_q3_base_combat_bridge_policy(qa_q3_base_combat_bridge *bridge);
void qa_q3_base_combat_bridge_policy_destroy(qa_combat_policy policy);
#endif
