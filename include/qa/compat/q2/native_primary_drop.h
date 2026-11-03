/* Port of src/compat/q2/native-primary-drop.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PRIMARY_DROP_H
#define QA_COMPAT_Q2_NATIVE_PRIMARY_DROP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_primary_commands.h"
#include "qa/compat/q2/native_primary_inventory.h"
#include "qa/contracts/content.h"

typedef struct qa_compat_q2_native_primary_drop_profile {
  qa_content_digest digest; qa_native_abi abi;
  double client_pointer, client_inventory, client_cursor, client_weapon, client_pending;
  double named, inv_entry, inv_admitted, find, lookup_return, allocate, free_entry;
  bool has_consumer; double consumer_entry, consumer_join;
  const qa_compat_q2_native_region_span_fwd *callbacks; size_t callback_count;
  const qa_compat_q2_native_region_span_fwd *debits; size_t debit_count;
} qa_compat_q2_native_primary_drop_profile;
typedef struct qa_compat_q2_native_drop_projection { qa_item_id source; qa_item_id current; qa_item_id pending; } qa_compat_q2_native_drop_projection;
/* TS action() result: an invocable closure or null. */
typedef struct qa_compat_q2_native_drop_action { void *ctx; void (*invoke)(void *ctx); } qa_compat_q2_native_drop_action;
/** TS NativePrimaryDropHooks. rows false = TS null; selected NULL = TS null; action fn NULL = TS null. */
typedef struct qa_compat_q2_native_primary_drop_hooks {
  void *ctx;
  bool (*rows)(void *ctx, qa_actor_id actor, const qa_compat_q2_native_inventory_row **out, size_t *out_count);
  qa_item_id (*selected)(void *ctx, qa_actor_id actor);
  qa_compat_q2_native_drop_projection (*projection)(void *ctx, qa_actor_id actor, qa_item_id item);
  bool (*dropped)(void *ctx, qa_actor_id actor, qa_actor_id pickup, qa_item_id item, double count);
  void (*consume)(void *ctx, qa_actor_id actor, void *execute_ctx, void (*execute)(void *execute_ctx));
  qa_compat_q2_native_drop_action (*action)(void *ctx, qa_actor_id actor, qa_item_id item);
  void (*print)(void *ctx, qa_actor_id actor, const char *text);
} qa_compat_q2_native_primary_drop_hooks;
typedef struct qa_compat_q2_native_primary_drop qa_compat_q2_native_primary_drop;
qa_compat_q2_native_primary_drop *qa_compat_q2_native_primary_drop_create(qa_compat_q2_native_primary_weapon_host host, const qa_compat_q2_native_primary_drop_profile *profile, qa_compat_q2_native_primary_commands *commands, qa_compat_q2_native_primary_drop_hooks hooks);
void qa_compat_q2_native_primary_drop_close(qa_compat_q2_native_primary_drop *s);
void qa_compat_q2_native_primary_drop_destroy(qa_compat_q2_native_primary_drop *s);
#endif
