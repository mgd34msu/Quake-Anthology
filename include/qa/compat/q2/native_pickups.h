/* Port of src/compat/q2/native-pickups.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PICKUPS_H
#define QA_COMPAT_Q2_NATIVE_PICKUPS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/classic/layout.h"
#include "qa/compat/q2/native_primary_reader.h"
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/pickups.h"
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
/** TS NativePickupSupplyEvaluation; quantity NULL = absent. */
typedef struct qa_compat_q2_native_pickup_supply_evaluation {
  qa_pickup_supply_offer offer;
  void *ctx;
  qa_source_pickup_quantity (*quantity)(void *ctx, double count, double capacity);
} qa_compat_q2_native_pickup_supply_evaluation;
typedef void (*qa_compat_q2_native_pickup_consume_fn)(void *consume_ctx, void *execute_ctx, void (*execute)(void *execute_ctx));
/** TS NativePickupHost. current returns false for the TS null. with_protection
 * may be NULL; it has the shape of qa_compat_q2_with_rerelease_primary_protection. */
typedef struct qa_compat_q2_native_pickup_host {
  qa_compat_q2_memory *memory;
  qa_compat_q2_runner *runner;
  void *ctx;
  qa_guest_call_result (*invoke)(void *ctx, qa_guest_address target, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count);
  qa_raw_entity_view (*record)(void *ctx, qa_guest_address address);
  bool (*current)(void *ctx, qa_raw_entity_view record, qa_actor_id *out);
  const qa_original_pickup_admission *(*admission)(void *ctx);
  void *(*with_protection)(void *ctx, qa_raw_entity_view recipient, qa_protection_channel channel, void *operation_ctx, void (*current)(void *operation_ctx),
                           void *(*operation)(void *operation_ctx, void *consume_ctx, qa_compat_q2_native_pickup_consume_fn consume), bool (*committed)(void *operation_ctx));
} qa_compat_q2_native_pickup_host;
/* Held by value in the guest hosts; has_supply/supply are read by them. */
typedef struct qa_compat_q2_native_primary_pickups {
  qa_compat_q2_native_pickup_host host; qa_guest_address image; const qa_compat_q2_native_pickup_profile *profile;
  bool has_supply; qa_compat_q2_native_pickup_supply supply; bool closed;
  void **frames; size_t frame_count, frame_capacity; /* PickupFrame*, NULL entries allowed */
  void *removals; /* private */
} qa_compat_q2_native_primary_pickups;
void qa_compat_q2_native_primary_pickups_init(qa_compat_q2_native_primary_pickups *s, qa_compat_q2_native_pickup_host host, qa_guest_address image, const qa_compat_q2_native_pickup_profile *profile);
void qa_compat_q2_native_primary_pickups_assert_idle(const qa_compat_q2_native_primary_pickups *s);
void qa_compat_q2_native_primary_pickups_bind_supply(qa_compat_q2_native_primary_pickups *s, qa_compat_q2_native_pickup_supply supply);
/* TS bindSupply's remover. */
void qa_compat_q2_native_primary_pickups_unbind_supply(qa_compat_q2_native_primary_pickups *s, qa_compat_q2_native_pickup_supply supply);
/* Valid only while the held source grant settles. */
qa_compat_q2_native_pickup_supply_evaluation qa_compat_q2_native_primary_pickups_evaluate_supply(qa_compat_q2_native_primary_pickups *s, const qa_original_pickup_offer *offer);
void qa_compat_q2_native_primary_pickups_close(qa_compat_q2_native_primary_pickups *s);
#endif
