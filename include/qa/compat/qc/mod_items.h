/* Port of src/compat/qc/mod-items.ts. GPL-2.0-or-later.
 * Session inventory/weapon binding surfaces are operations
 * fn+ctx (TS ModHostServices picks); QC field projection,
 * validation, and store observation are implemented here. */
#ifndef QA_COMPAT_QC_MOD_ITEMS_H
#define QA_COMPAT_QC_MOD_ITEMS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/machine.h"
#include "qa/compat/qc/source_call.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/source_items.h"
#include "qa/content/q1/quakec/weapon_stage.h"
void qa_compat_qc_validate_items(qa_compat_qc_program *program, qa_mod_callback_declaration declaration);
typedef struct qa_compat_qc_mod_items_operations {
  void *ctx;
  qa_compat_qc_machine *(*machine)(void *ctx);
  int64_t (*reference)(void *ctx, qa_actor_id actor);
  qa_actor_id (*actor)(void *ctx, int64_t reference);
  double (*invoke)(void *ctx, qa_mod_source_call call, const qa_compat_qc_mod_input_entry *inputs, size_t input_count);
  /* inventory.bindItems lease + weapons.bind, via session owner */
  qa_source_item_lease (*bind_items)(void *ctx, qa_owned_actor actor, const qa_source_item_admission *items, size_t count, void *state_ctx, const qa_inventory_entry *(*read)(void *, size_t *), bool (*entry)(void *, qa_item_id, qa_inventory_entry *), void (*write)(void *, qa_inventory_entry), bool (*mutable_capacity)(void *, qa_item_id), void (*invoke_action)(void *, qa_item_id, const char *));
  bool (*client_is_current)(void *ctx, qa_actor_id actor);
  bool (*weapon_bind)(void *ctx, qa_owned_actor actor, qa_source_weapon_binding binding, void (**remove)(void *), void **remove_ctx);
  bool (*weapon_selected)(void *ctx, qa_actor_id actor);
  double (*inventory_count)(void *ctx, qa_actor_id actor, qa_item_id item);
  double (*time_seconds)(void *ctx);
  bool (*resolve_owned)(void *ctx, qa_actor_id actor, qa_owned_actor *out);
  /* media resources: model path -> resolved resource */
  qa_content_id media_content;
  bool (*media_resource)(void *ctx, const char *path, qa_resolved_resource_reference *out);
} qa_compat_qc_mod_items_operations;
typedef struct qa_compat_qc_mod_items_entry {
  qa_owned_actor actor; int64_t reference; qa_source_item_lease lease;
  void (*remove_weapon)(void *); void *remove_weapon_ctx;
} qa_compat_qc_mod_items_entry;
typedef struct qa_compat_qc_mod_items {
  const qa_mod_qc_items *definition; qa_provider_id provider;
  qa_compat_qc_mod_items_operations operations;
  qa_source_item_admission *admissions; size_t admission_count;
  qa_source_item_definition *definitions;
  qa_compat_qc_mod_items_entry *entries; size_t entry_count, entry_capacity;
  int64_t *watched_words; size_t watched_word_count;
  bool has_weapons;
  qa_q1qc_weapon_stage *weapon_stage; qa_q1qc_weapon_stage_binding stage_binding;
  qa_q1qc_program qprogram; qa_q1qc_definition *qglobals, *qfields; qa_q1qc_function *qfunctions; qa_q1qc_statement *qstatements;
  qa_q1qc_entity_memory qentities; qa_q1qc_machine qmachine;
  qa_weapon_hud_icon *icon_storage; qa_source_item_action (*action_storage)[2];
  struct qa_compat_qc_mod_items_state { struct qa_compat_qc_mod_items *items; qa_actor_id actor; qa_inventory_entry *buffer; size_t buffer_capacity; } **state_storage; size_t state_count, state_capacity;
  struct qa_compat_qc_mod_items_weapon **weapon_storage; size_t weapon_count, weapon_capacity;
  struct qa_compat_qc_mod_items *self_ref;
} qa_compat_qc_mod_items;
bool qa_compat_qc_mod_items_init(qa_compat_qc_mod_items *items, const qa_mod_qc_items *definition, qa_provider_id provider, qa_compat_qc_mod_items_operations operations, qa_compat_qc_program *program);
void qa_compat_qc_mod_items_destroy(qa_compat_qc_mod_items *items);
void qa_compat_qc_mod_items_admit(qa_compat_qc_mod_items *items, qa_actor_id actor);
void qa_compat_qc_mod_items_observe(qa_compat_qc_mod_items *items, qa_compat_qc_entity_store_observation store, const qa_original_pickup_execution *pickup);
void qa_compat_qc_mod_items_release(qa_compat_qc_mod_items *items, qa_actor_id actor);
void qa_compat_qc_mod_items_close(qa_compat_qc_mod_items *items);
#endif
