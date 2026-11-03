/* Port of src/content/q1/quakec/damage-call.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_QUAKEC_DAMAGE_CALL_H
#define QA_CONTENT_Q1_QUAKEC_DAMAGE_CALL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q1/quakec/compat_types.h"

/* TS QcDamageRole order: self, inflictor, attacker, amount. */
typedef enum qa_q1qc_damage_role {
  QA_Q1QC_ROLE_SELF, QA_Q1QC_ROLE_INFLICTOR, QA_Q1QC_ROLE_ATTACKER, QA_Q1QC_ROLE_AMOUNT
} qa_q1qc_damage_role;
typedef struct qa_q1qc_damage_role_location {
  bool is_global; /* false = "argument" */
  int64_t index; int64_t frame_word; /* argument */
  int64_t word; /* global */
} qa_q1qc_damage_role_location;
typedef struct qa_q1qc_role_locations {
  qa_q1qc_damage_role_location *items; size_t count;
} qa_q1qc_role_locations;
/* TS QcDamageCallLayout (heap owned). declaration points at declaration_copy,
 * a shallow copy of the caller's call (its strings and arrays stay borrowed from
 * the compatibility declaration), so a call passed by value may go out of scope. */
typedef struct qa_q1qc_damage_call_layout {
  int64_t function_index;
  const char **parameters; size_t parameter_count;
  qa_q1qc_role_locations roles[4];
  const qa_mod_source_call *declaration; bool has_declaration;
  qa_mod_source_call declaration_copy;
} qa_q1qc_damage_call_layout;
void qa_q1qc_damage_call_layout_destroy(qa_q1qc_damage_call_layout *layout);
bool qa_q1qc_damage_roles_equal(const qa_q1qc_damage_call_layout *a,
                                const qa_q1qc_damage_call_layout *b);
/* declaration NULL = TS undefined. */
qa_q1qc_damage_call_layout *qa_q1qc_damage_call_layout_create(
    const qa_q1qc_program *program, const qa_mod_source_call *declaration);
qa_q1qc_damage_call_layout *qa_q1qc_standard_damage_call_layout(int64_t function_index,
                                                                int64_t parameter_start);
/* TS QcDamageCallValues (entity references travel as numbers). */
typedef struct qa_q1qc_damage_call_values {
  double self; double inflictor; double attacker; double amount;
} qa_q1qc_damage_call_values;
qa_q1qc_damage_call_values qa_q1qc_read_damage_call(qa_q1qc_machine *machine,
                                                    const qa_q1qc_damage_call_layout *layout);
void qa_q1qc_project_damage_call(qa_q1qc_machine *machine,
                                 const qa_q1qc_damage_call_layout *layout,
                                 qa_q1qc_damage_call_values captured,
                                 qa_q1qc_damage_call_values effective,
                                 qa_q1qc_function_execution execute);

#endif
