/* Port of src/compat/qvm/game-inventory.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_GAME_INVENTORY_H
#define QA_COMPAT_QVM_GAME_INVENTORY_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/compat/qvm/game_data.h"
#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/module.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/qvm_mod_items.h"
#include "qa/world/gameplay/inventory.h"

typedef struct qa_qvm_capacity_context {
  qa_qvm_module *module;
  double client, entity, client_number;
} qa_qvm_capacity_context;

typedef struct qa_qvm_public_inventory_profile {
  qa_module_identity module;
  qa_qvm_abi_profile abi_profile;
  double weapons_offset, ammo_offset;
  void *ctx;
  double (*capacity)(void *ctx, qa_qvm_memory *memory, double weapon,
                     qa_qvm_capacity_context context);
} qa_qvm_public_inventory_profile;

typedef struct qa_qvm_private_inventory_profile {
  qa_module_identity module;
  qa_qvm_abi_profile abi_profile;
  double entity_stride, client_stride;
  const qa_qvm_image *image;
  const qa_qvm_item_storage *storage;
  size_t storage_count;
} qa_qvm_private_inventory_profile;

/* TS QvmInventoryProfile union: exactly one of public_/private_ set. */
typedef struct qa_qvm_inventory_profile {
  const qa_qvm_public_inventory_profile *public_profile;
  const qa_qvm_private_inventory_profile *private_profile;
} qa_qvm_inventory_profile;

typedef struct qa_qvm_inventory_weapon {
  double weapon;
  qa_item_id item;
  qa_item_id ammo; /* NULL is TS null */
} qa_qvm_inventory_weapon;

typedef struct qa_qvm_inventory_weapons_source {
  const qa_qvm_inventory_weapon *list; /* set for the static form */
  size_t count;
  void *ctx;
  const qa_qvm_inventory_weapon *(*get)(void *ctx, size_t *out_count); /* function form */
} qa_qvm_inventory_weapons_source;

typedef struct qa_qvm_inventory_options {
  qa_qvm_module *module;
  qa_qvm_game_data *data;
  qa_qvm_inventory_profile profile;
  qa_qvm_inventory_weapons_source weapons;
  void *ctx;
  double (*client)(void *ctx);
} qa_qvm_inventory_options;

/* The returned binding's ctx is owned by the binding; free with
 * qa_qvm_inventory_binding_destroy. NULL binding ctx only on OOM. */
qa_inventory_state_binding qa_qvm_inventory_binding(qa_qvm_inventory_options options);
void qa_qvm_inventory_binding_destroy(qa_inventory_state_binding *binding);

typedef struct qa_qvm_inventory_word { double address, value; } qa_qvm_inventory_word;

typedef struct qa_qvm_inventory_words {
  qa_qvm_inventory_word *entries; /* owned */
  size_t count;
} qa_qvm_inventory_words;

/* False only on OOM. */
bool qa_qvm_inventory_projection(qa_qvm_inventory_options options, qa_item_id item, double count,
                                 qa_qvm_inventory_words *out);
void qa_qvm_inventory_words_destroy(qa_qvm_inventory_words *words);

#endif
