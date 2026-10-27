/* Port of src/content/q3/equipment/pickup-profile.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_EQUIPMENT_PICKUP_PROFILE_H
#define QA_CONTENT_Q3_EQUIPMENT_PICKUP_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/equipment/lrctf_grapple_profile.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/math.h"
extern const char qa_stock_q3_pickup_digest[];
/* Mirror of QvmItemLayout numeric form (TS owner: src/compat/qvm/item-catalog.ts). */
typedef struct qa_q3_equipment_qvm_item_layout {
  double address, count, stride;
  double field_class_name, field_pickup_name, field_type, field_tag;
  double weapon_type, ammo_type;
} qa_q3_equipment_qvm_item_layout;
/* Mirror of QvmPickupEligibility DataViews (TS owner: src/compat/qvm/game-pickups.ts):
 * byte views over call words / item / player records. */
typedef struct qa_q3_equipment_qvm_pickup_eligibility {
  qa_byte_view call_words, item, player;
} qa_q3_equipment_qvm_pickup_eligibility;
/* Mirror of QvmRegionEvaluation (TS owner: src/compat/qvm/regions.ts). */
typedef struct qa_q3_equipment_qvm_region_evaluation {
  double entry, join; const double *inputs; size_t input_count; bool has_result; double result;
} qa_q3_equipment_qvm_region_evaluation;
/* Mirror of QvmPickupGrant (TS owner: src/compat/qvm/game-pickups.ts). */
typedef struct qa_q3_equipment_qvm_pickup_grant {
  double item_type, entry;
  const double *calls; size_t call_count;
  bool operation_is_region;
  double accepted_return; /* return op */
  double region_entry, region_join, quantity; /* region op */
  bool has_weapon;
  double weapon_bits_offset, weapon_ammo_offset;
  qa_q3_equipment_qvm_region_evaluation weapon_quantity;
  void *eligible_ctx;
  bool (*eligible)(void *ctx, qa_q3_equipment_qvm_pickup_eligibility context);
} qa_q3_equipment_qvm_pickup_grant;
/* Mirror of QvmPickupProfile (TS owner: src/compat/qvm/game-pickups.ts). */
typedef struct qa_q3_equipment_qvm_pickup_profile {
  qa_module_identity module; const char *abi_profile;
  double entity_stride, client_stride;
  double field_inuse, field_client, field_health, field_item, field_count, field_flags;
  double dropped_flag;
  qa_q3_equipment_qvm_item_layout items;
  double touch;
  double gate_entry; const double *gate_calls; size_t gate_call_count;
  double gate_item_argument, gate_player_argument;
  double targets_entry; const double *targets_calls; size_t targets_call_count;
  double free_entry;
  const double *objective_types; size_t objective_type_count;
  const qa_q3_equipment_qvm_pickup_grant *grants; size_t grant_count;
} qa_q3_equipment_qvm_pickup_profile;
bool qa_q3_native_pickup_profile(const qa_q3_equipment_qvm_artifact *artifact,
    qa_q3_equipment_qvm_pickup_profile *out);
#endif
