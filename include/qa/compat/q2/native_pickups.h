/* Port of src/compat/q2/native-pickups.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PICKUPS_H
#define QA_COMPAT_Q2_NATIVE_PICKUPS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_primary_reader.h"
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/foundation/support.h"

typedef struct qa_compat_q2_pickup_consumer { double entry; qa_compat_q2_guest_call_signature signature; qa_protection_channel protection; } qa_compat_q2_pickup_consumer;
typedef struct qa_compat_q2_native_pickup_grant {
  double entry, recipient_entry, recipient_join; const char *resource;
  const qa_compat_q2_pickup_consumer *consumers; size_t consumer_count; bool has_supply; const char *supply_kind;
  double supply_entry; const char *supply_amount; double supply_ammo_return, supply_settle, supply_auto_entry, supply_auto_join;
} qa_compat_q2_native_pickup_grant;
typedef struct qa_compat_q2_native_pickup_profile {
  qa_content_digest digest; double touch, grant_return, targets_return;
  qa_compat_q2_guest_call_signature touch_signature, grant_signature;
  const qa_compat_q2_native_pickup_grant *grants; size_t grant_count;
  double items_table, items_stride, items_count, items_classname, items_pickup;
  double entity_item, entity_count, entity_spawnflags, entity_inuse; double entity_inuse_bytes; bool has_entity_generation; double entity_generation;
  double time_address; const char *time_storage;
  double supply_client, supply_inventory, supply_flags, supply_weapon_flag;
  double ammo_entry; qa_compat_q2_guest_call_signature ammo_signature; bool has_ammo_stop; double ammo_stop, ammo_tag;
  const double *ammo_capacities; size_t ammo_capacity_count; double ammo_capacity_bytes;
} qa_compat_q2_native_pickup_profile;
typedef struct qa_compat_q2_native_pickup_supply { void *ctx; bool (*owns)(void *ctx, qa_actor_id a, qa_item_id item, qa_actor_id pickup); } qa_compat_q2_native_pickup_supply;
typedef struct qa_compat_q2_native_primary_pickups { const qa_compat_q2_native_pickup_profile *profile; const char *host_digest; bool has_supply; qa_compat_q2_native_pickup_supply supply; bool closed; } qa_compat_q2_native_primary_pickups;
void qa_compat_q2_native_primary_pickups_init(qa_compat_q2_native_primary_pickups *s, const char *host_digest, bool has_protection, const qa_compat_q2_native_pickup_profile *p);
void qa_compat_q2_native_primary_pickups_bind_supply(qa_compat_q2_native_primary_pickups *s, qa_compat_q2_native_pickup_supply supply);
void qa_compat_q2_native_primary_pickups_close(qa_compat_q2_native_primary_pickups *s);
#endif
