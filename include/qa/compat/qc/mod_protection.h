/* Port of src/compat/qc/mod-protection.ts. GPL-2.0-or-later.
 * Regions are owned (free with qa_q1qc_armor_stage_destroy each).
 * watch runs fn+ctx and returns its double result. */
#ifndef QA_COMPAT_QC_MOD_PROTECTION_H
#define QA_COMPAT_QC_MOD_PROTECTION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/machine.h"
#include "qa/compat/qc/source_call.h"
#include "qa/content/q1/quakec/armor_stage.h"
#include "qa/world/session/mods.h"
qa_q1qc_armor_stage **qa_compat_qc_protection_regions(qa_compat_qc_program *program, qa_mod_callback_declaration declaration, size_t *out_count);
void qa_compat_qc_protection_regions_free(qa_q1qc_armor_stage **regions, size_t count);
typedef struct qa_compat_qc_mod_protection_operations {
  void *ctx; qa_compat_qc_machine *machine;
  int64_t (*reference)(void *ctx, qa_actor_id actor);
  qa_actor_id (*actor)(void *ctx, int64_t reference);
  double (*invoke)(void *ctx, qa_mod_source_call call, const qa_compat_qc_mod_input_entry *inputs, size_t input_count, const qa_q1qc_armor_stage *region);
  size_t (*pickups)(void *ctx, qa_actor_id actor, qa_protection_channel channel, const qa_original_pickup_rule **out, size_t capacity); /* nullable */
} qa_compat_qc_mod_protection_operations;
typedef struct qa_compat_qc_mod_protection LaneImpl;
typedef struct qa_compat_qc_mod_protection {
  qa_mod_callback_declaration declaration; qa_provider_id provider;
  qa_mod_host_services *services; qa_compat_qc_mod_protection_operations operations;
  qa_q1qc_armor_stage **regions; size_t region_count;
  struct ProtEntry *entries; size_t entry_count, entry_capacity;
  struct ProtStage *stages; size_t stage_count, stage_capacity;
  struct ProtBind **bind_storage; size_t bind_count, bind_capacity;
  struct qa_compat_qc_mod_protection *self_ref;
} qa_compat_qc_mod_protection;
void qa_compat_qc_mod_protection_init(qa_compat_qc_mod_protection *p, qa_mod_callback_declaration declaration, qa_provider_id provider, qa_mod_host_services *services, qa_compat_qc_mod_protection_operations operations);
void qa_compat_qc_mod_protection_destroy(qa_compat_qc_mod_protection *p);
void qa_compat_qc_mod_protection_reserve(qa_compat_qc_mod_protection *p, qa_actor_id actor);
void qa_compat_qc_mod_protection_activate(qa_compat_qc_mod_protection *p, qa_actor_id actor);
void qa_compat_qc_mod_protection_observe(qa_compat_qc_mod_protection *p, qa_compat_qc_entity_store_observation store);
double qa_compat_qc_mod_protection_watch(qa_compat_qc_mod_protection *p, qa_actor_id actor, qa_protection_observer observer, void *ctx, double (*run)(void *ctx));
void qa_compat_qc_mod_protection_release(qa_compat_qc_mod_protection *p, qa_actor_id actor);
void qa_compat_qc_mod_protection_close(qa_compat_qc_mod_protection *p);
void qa_compat_qc_mod_protection_assert_idle(qa_compat_qc_mod_protection *p);
#endif
