/* Port of src/compat/q2/rerelease/source-state.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_RERELEASE_SOURCE_STATE_H
#define QA_COMPAT_Q2_RERELEASE_SOURCE_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/rerelease/module.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/world.h"
#include "qa/world/actors/body.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"

typedef struct qa_compat_q2_rerelease_actor_addresses {
  void *ctx;
  bool (*actor)(void *ctx, qa_guest_address address, qa_actor_id *out); /* false = TS null */
  qa_guest_address (*address)(void *ctx, qa_actor_id actor);
} qa_compat_q2_rerelease_actor_addresses;
typedef struct qa_compat_q2_rerelease_inventory_item {
  qa_item_id item; double source_index;
  bool capacity_is_ammo; double capacity_source_index; double capacity_count;
} qa_compat_q2_rerelease_inventory_item;
typedef struct qa_compat_q2_rerelease_protection_traits { bool invulnerable; const char *team /* TS team: string|null */; bool no_knockback; } qa_compat_q2_rerelease_protection_traits;
typedef struct qa_compat_q2_rerelease_source_protection {
  void *ctx;
  qa_armor_state (*armor)(void *ctx);
  void (*write_armor)(void *ctx, qa_armor_state value);
  qa_compat_q2_rerelease_protection_traits (*traits)(void *ctx);
} qa_compat_q2_rerelease_source_protection;

typedef struct qa_compat_q2_rerelease_source_client qa_compat_q2_rerelease_source_client;
/* NULL only on OOM. */
qa_compat_q2_rerelease_source_client *qa_compat_q2_rerelease_source_client_create(
    qa_guest_address address, qa_compat_q2_rerelease_guest_module *module, const qa_compat_q2_rerelease_client_profile *profile);
void qa_compat_q2_rerelease_source_client_destroy(qa_compat_q2_rerelease_source_client *c);
qa_guest_address qa_compat_q2_rerelease_source_client_get_address(const qa_compat_q2_rerelease_source_client *c);
qa_guest_address qa_compat_q2_rerelease_source_client_at(const qa_compat_q2_rerelease_source_client *c, const char *name);
int64_t qa_compat_q2_rerelease_source_client_invincible_until_milliseconds(const qa_compat_q2_rerelease_source_client *c);
/* Items are copied into the binding ctx (owned by c). */
qa_inventory_state_binding qa_compat_q2_rerelease_source_client_inventory(qa_compat_q2_rerelease_source_client *c,
                                                                          const qa_compat_q2_rerelease_inventory_item *items, size_t count);
qa_power_armor_cell_binding qa_compat_q2_rerelease_source_client_power_armor_cells(qa_compat_q2_rerelease_source_client *c);

typedef struct qa_compat_q2_rerelease_source_edict qa_compat_q2_rerelease_source_edict;
/* NULL only on OOM. */
qa_compat_q2_rerelease_source_edict *qa_compat_q2_rerelease_source_edict_create(qa_raw_entity_view raw, qa_compat_q2_rerelease_guest_module *module);
void qa_compat_q2_rerelease_source_edict_destroy(qa_compat_q2_rerelease_source_edict *e);
qa_raw_entity_view qa_compat_q2_rerelease_source_edict_get_raw(const qa_compat_q2_rerelease_source_edict *e);
qa_guest_address qa_compat_q2_rerelease_source_edict_at(const qa_compat_q2_rerelease_source_edict *e, const char *name);
double qa_compat_q2_rerelease_source_edict_generation(const qa_compat_q2_rerelease_source_edict *e);
double qa_compat_q2_rerelease_source_edict_get_health(const qa_compat_q2_rerelease_source_edict *e);
void qa_compat_q2_rerelease_source_edict_set_health(qa_compat_q2_rerelease_source_edict *e, double value);
bool qa_compat_q2_rerelease_source_edict_damageable(const qa_compat_q2_rerelease_source_edict *e);
/* False is the TS null. */
bool qa_compat_q2_rerelease_source_edict_client(const qa_compat_q2_rerelease_source_edict *e, qa_guest_address *out);
qa_combat_state_binding qa_compat_q2_rerelease_source_edict_combat(qa_compat_q2_rerelease_source_edict *e, qa_compat_q2_rerelease_source_protection protection);
qa_vec3 qa_compat_q2_rerelease_source_edict_vector(const qa_compat_q2_rerelease_source_edict *e, const char *name);
void qa_compat_q2_rerelease_source_edict_write_vector(const qa_compat_q2_rerelease_source_edict *e, const char *name, qa_vec3 value);
qa_body_state_binding qa_compat_q2_rerelease_source_edict_body(qa_compat_q2_rerelease_source_edict *e, qa_compat_q2_rerelease_actor_addresses addresses);
typedef qa_guest_call_result (*qa_compat_q2_rerelease_encode_trace_fn)(void *ctx, qa_trace_result trace);
qa_actor_callbacks qa_compat_q2_rerelease_source_edict_callbacks(qa_compat_q2_rerelease_source_edict *e,
                                                                 qa_compat_q2_rerelease_actor_addresses addresses,
                                                                 void *encode_ctx, qa_compat_q2_rerelease_encode_trace_fn encode_trace);
qa_guest_call_result qa_compat_q2_rerelease_source_edict_call(qa_compat_q2_rerelease_source_edict *e, const char *name,
                                                              const qa_guest_call_value *arguments, size_t count, const qa_raw_entity_view *other);
#endif
