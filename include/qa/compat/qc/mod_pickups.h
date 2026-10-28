/* Port of src/compat/qc/mod-pickups.ts. GPL-2.0-or-later.
 * Inventory bindPickup is an operations fn (session owner);
 * rules are built per actor and cached in the struct. */
#ifndef QA_COMPAT_QC_MOD_PICKUPS_H
#define QA_COMPAT_QC_MOD_PICKUPS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/source_call.h"
#include "qa/contracts/original_pickups.h"
#include "qa/world/session/mods.h"
typedef struct qa_compat_qc_mod_pickups_operations {
  void *ctx;
  double (*invoke)(void *ctx, qa_mod_source_call call, const qa_compat_qc_mod_input_entry *inputs, size_t input_count);
  /* TS watch(actor, observer, run) */
  qa_original_pickup_decision (*watch)(void *ctx, qa_actor_id actor, qa_original_pickup_execution observer, void *run_ctx, qa_original_pickup_decision (*run)(void *run_ctx));
  bool (*bind_pickup)(void *ctx, qa_owned_actor actor, const qa_original_pickup_rule *rules, size_t rule_count, void (**remove)(void *), void **remove_ctx);
  bool (*resolve_owned)(void *ctx, qa_actor_id actor, qa_owned_actor *out);
  bool (*client_of)(void *ctx, qa_actor_id actor, qa_client_id *out);
  bool (*actor_of_client)(void *ctx, qa_client_id client, qa_actor_id *out);
} qa_compat_qc_mod_pickups_operations;
typedef struct qa_compat_qc_mod_pickups {
  qa_mod_callback_declaration declaration; qa_provider_id provider;
  qa_mod_host_services *services; qa_compat_qc_mod_pickups_operations operations;
  /* per-actor state */
  struct qa_compat_qc_mod_pickups_entry { qa_actor_id actor; qa_client_id client; void (*remove[8])(void *); void *remove_ctx[8]; size_t remove_count;
    qa_original_pickup_rule *rules; size_t rule_count; qa_original_pickup_rule *pickup_rules; } *entries;
  size_t entry_count, entry_capacity;
  struct qa_compat_qc_mod_pickups_take **take_storage; size_t take_count, take_capacity;
  struct qa_compat_qc_mod_pickups *self_ref;
} qa_compat_qc_mod_pickups;
void qa_compat_qc_mod_pickups_init(qa_compat_qc_mod_pickups *p, qa_mod_callback_declaration declaration, qa_provider_id provider, qa_mod_host_services *services, qa_compat_qc_mod_pickups_operations operations);
void qa_compat_qc_mod_pickups_destroy(qa_compat_qc_mod_pickups *p);
void qa_compat_qc_mod_pickups_admit(qa_compat_qc_mod_pickups *p, qa_actor_id actor);
size_t qa_compat_qc_mod_pickups_protection(qa_compat_qc_mod_pickups *p, qa_actor_id actor, qa_protection_channel channel, const qa_original_pickup_rule **out, size_t capacity);
void qa_compat_qc_mod_pickups_release(qa_compat_qc_mod_pickups *p, qa_actor_id actor);
void qa_compat_qc_mod_pickups_close(qa_compat_qc_mod_pickups *p);
#endif
