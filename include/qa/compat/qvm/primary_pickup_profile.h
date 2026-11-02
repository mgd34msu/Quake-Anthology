/*
 * Port of src/compat/qvm/primary-pickup-profile.ts. Mirrored types
 * (parallel slice): qa_qvmy_item_layout mirrors QvmItemLayout and its
 * parser (TS owner src/compat/qvm/item-catalog.ts);
 * qa_qvmy_pickup_profile / qa_qvmy_pickup_grant mirror
 * QvmPickupProfile / QvmPickupGrant (TS owner
 * src/compat/qvm/game-pickups.ts). The grant eligible closure becomes
 * qa_qvmy_pickup_grant_eligible over a mirrored function call.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_PRIMARY_PICKUP_PROFILE_H
#define QA_COMPAT_QVM_PRIMARY_PICKUP_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/module.h"
#include "qa/persistence/value.h"

typedef struct qa_qvmy_item_layout {
  bool address_is_global;
  int64_t address;
  bool count_is_global;
  int64_t count;
  int64_t count_maximum;
  bool has_source; /* source "live" when true */
  int64_t stride;
  int64_t field_class_name;
  int64_t field_pickup_name;
  int64_t field_type;
  int64_t field_tag;
  int64_t weapon_type;
  int64_t ammo_type;
} qa_qvmy_item_layout;

bool qa_qvmy_item_layout_equal(const qa_qvmy_item_layout *left, const qa_qvmy_item_layout *right);

typedef struct qa_qvm_pickup_branch {
  int64_t instruction_index;
  bool taken;
} qa_qvm_pickup_branch;

typedef struct qa_qvmy_pickup_grant {
  int64_t item_type;
  int64_t entry;
  int64_t *calls; /* owned */
  size_t call_count;
  const char *operation_kind; /* "return" | "region" */
  int64_t accepted_return;     /* return */
  int64_t region_entry;        /* region */
  int64_t region_join;
  int64_t region_quantity;
  bool has_weapon;
  bool weapon_storage_inventory;
  int64_t weapon_bits_offset;
  int64_t weapon_ammo_offset;
  qa_qvm_region_evaluation weapon_quantity;
  int64_t *weapon_quantity_inputs; /* owned */
  qa_qvm_pickup_branch *branches; /* owned */
  size_t branch_count;
} qa_qvmy_pickup_grant;

bool qa_qvmy_pickup_grant_eligible(const qa_qvmy_pickup_grant *grant, qa_qvmy_function_call *call);

typedef struct qa_qvmy_pickup_profile {
  qa_module_identity module;
  qa_qvm_abi_profile abi_profile;
  int64_t entity_stride;
  int64_t client_stride;
  int64_t field_inuse, field_client, field_health, field_item, field_count, field_flags;
  int64_t dropped_flag;
  qa_qvmy_item_layout items;
  int64_t touch;
  int64_t gate_entry;
  int64_t *gate_calls; /* owned */
  size_t gate_call_count;
  int64_t gate_item_argument;
  int64_t gate_player_argument;
  int64_t targets_entry;
  int64_t *targets_calls; /* owned */
  size_t targets_call_count;
  int64_t free_entry;
  int64_t *objective_types; /* owned */
  size_t objective_type_count;
  qa_qvmy_pickup_grant *grants; /* owned */
  size_t grant_count;
} qa_qvmy_pickup_profile;

void qa_qvm_read_primary_pickup_profile(const qa_save_reader *reader, const qa_qvmy_artifact *artifact,
                                        qa_qvmy_pickup_profile *out);
void qa_qvmy_pickup_profile_free(qa_qvmy_pickup_profile *profile);

#endif
