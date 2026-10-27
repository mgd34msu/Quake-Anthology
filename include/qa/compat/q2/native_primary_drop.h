/* Port of src/compat/q2/native-primary-drop.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PRIMARY_DROP_H
#define QA_COMPAT_Q2_NATIVE_PRIMARY_DROP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_primary_commands.h"
#include "qa/contracts/content.h"

typedef struct qa_compat_q2_native_primary_drop_profile {
  qa_content_digest digest; qa_native_abi abi;
  double client_pointer, client_inventory, client_cursor, client_weapon, client_pending;
  double named, inv_entry, inv_admitted, find, lookup_return, allocate, free_entry;
  bool has_consumer; double consumer_entry, consumer_join;
  const qa_compat_q2_native_region_span_fwd *callbacks; size_t callback_count;
  const qa_compat_q2_native_region_span_fwd *debits; size_t debit_count;
} qa_compat_q2_native_primary_drop_profile;
typedef struct qa_compat_q2_native_primary_drop_hooks {
  void *ctx; void (*print)(void *ctx, qa_actor_id a, const char *text);
  bool (*dropped)(void *ctx, qa_actor_id a, qa_actor_id pickup, qa_item_id item, double count);
} qa_compat_q2_native_primary_drop_hooks;
typedef struct qa_compat_q2_native_primary_drop {
  qa_compat_q2_native_primary_weapon_host host; const qa_compat_q2_native_primary_drop_profile *profile;
  qa_compat_q2_native_primary_drop_hooks hooks; bool closed;
} qa_compat_q2_native_primary_drop;
void qa_compat_q2_native_primary_drop_init(qa_compat_q2_native_primary_drop *s, qa_compat_q2_native_primary_weapon_host h, const qa_compat_q2_native_primary_drop_profile *p, qa_compat_q2_native_primary_drop_hooks hk);
void qa_compat_q2_native_primary_drop_close(qa_compat_q2_native_primary_drop *s);
#endif
