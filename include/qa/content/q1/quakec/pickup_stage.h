/* Port of src/content/q1/quakec/pickup-stage.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_QUAKEC_PICKUP_STAGE_H
#define QA_CONTENT_Q1_QUAKEC_PICKUP_STAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q1/quakec/compat_types.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/qc_pickup_callers.h"

/* TS QcPickupDescriptor.supply. quantity is the QcPickupScalar shape;
 * leave is a global word when has_leave. */
typedef struct qa_q1qc_pickup_supply {
  qa_item_id item;
  bool quantity_is_global;
  const char *quantity_name; /* field form */
  int64_t quantity_word;     /* global form */
  bool has_leave; int64_t leave;
} qa_q1qc_pickup_supply;
/* TS QcPickupDescriptor. value is string or number by descriptor kind
 * (cargo/constant carry value_string in the pinned tables). */
typedef struct qa_q1qc_pickup_descriptor {
  bool value_is_string;
  const char *value_string;
  double value_number;
  qa_item_id item;
  bool has_resource; qa_pickup_resource resource; /* !has = TS null */
  bool has_count; qa_qc_pickup_scalar count;
  bool has_supply; qa_q1qc_pickup_supply supply;
} qa_q1qc_pickup_descriptor;
/* TS QcPickupRegion.operation union on kind (exact strings). */
typedef struct qa_q1qc_pickup_operation {
  const char *kind; /* decision|grant|weapon-selection|admission|consume|
                       consumed-selection|source-effect|cargo-ownership|
                       cargo-current|counter */
  int64_t word;     /* decision / cargo-* */
  double accepted;  /* decision */
  const char *field; /* counter */
  qa_item_id item;   /* counter */
} qa_q1qc_pickup_operation;
typedef struct qa_q1qc_pickup_region {
  qa_q1qc_inline_region region;
  bool self_recipient; /* TS self?: "recipient" */
  qa_q1qc_pickup_operation operation;
} qa_q1qc_pickup_region;
typedef struct qa_q1qc_pickup_word_item { int64_t word; qa_item_id item; } qa_q1qc_pickup_word_item;
typedef struct qa_q1qc_pickup_counter { const char *field; qa_item_id item; } qa_q1qc_pickup_counter;
/* TS QcPickupStage. Heap owned arrays. descriptor_kind:
 * "constant"|"string"|"float"|"cargo"; values holds the single
 * constant/cargo descriptor or the string/float table. */
typedef struct qa_q1qc_pickup_stage {
  int64_t function_index;
  bool has_dropped; qa_qc_pickup_scalar dropped;
  const char *descriptor_kind;
  const char *descriptor_field; /* string/float */
  qa_q1qc_pickup_descriptor *values; size_t value_count;
  qa_q1qc_pickup_counter *counters; size_t counter_count;         /* cargo */
  qa_q1qc_pickup_word_item *cargo_weapons; size_t cargo_weapon_count; /* cargo */
  qa_q1qc_pickup_region *regions; size_t region_count;
  bool has_source_selection;
  int64_t selection_function;
  int64_t *selection_calls; size_t selection_call_count;
  qa_q1qc_pickup_word_item *selection_weapons; size_t selection_weapon_count;
  bool has_source_effect; int64_t source_effect_word; double source_effect_value;
} qa_q1qc_pickup_stage;
/* TS qcPickupStages: malloc'd stage array (NULL when empty). */
qa_q1qc_pickup_stage *qa_q1qc_pickup_stages(const qa_q1qc_program *program, size_t *out_count);
void qa_q1qc_pickup_stages_destroy(qa_q1qc_pickup_stage *stages, size_t count);
void qa_q1qc_pickup_stage_clear(qa_q1qc_pickup_stage *stage);

#endif
