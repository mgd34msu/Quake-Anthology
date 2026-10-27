/* Port of src/content/q1/quakec/damage-scale.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_QUAKEC_DAMAGE_SCALE_H
#define QA_CONTENT_Q1_QUAKEC_DAMAGE_SCALE_H

#include <stddef.h>
#include <stdint.h>

#include "qa/content/q1/quakec/compat_types.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/mod_callbacks.h"

typedef struct qa_q1qc_damage_scale_constant { int64_t word; int32_t bits; } qa_q1qc_damage_scale_constant;
/* TS QcDamageScale (heap owned). call is a shallow copy borrowing the
 * caller's argument/global arrays, as the TS holds the call object. */
typedef struct qa_q1qc_damage_scale {
  const char *kind; /* "multiplier" | "identity" | "transform" */
  qa_q1qc_inline_region region;
  int64_t *scratch; size_t scratch_count;
  qa_q1qc_damage_scale_constant *constants; size_t constant_count;
  qa_mod_source_call call;
} qa_q1qc_damage_scale;
/* source NULL = TS undefined -> NULL return (TS null). */
qa_q1qc_damage_scale *qa_q1qc_damage_scale_create(const qa_q1qc_program *program,
                                                  const qa_mod_source_call *call,
                                                  const qa_mod_qc_damage_scale *source);
void qa_q1qc_damage_scale_destroy(qa_q1qc_damage_scale *scale);
double qa_q1qc_evaluate_damage_scale(qa_q1qc_machine *machine,
                                     const qa_q1qc_damage_scale *scale,
                                     qa_actor_id owner, int64_t actor_reference, double seconds);
double qa_q1qc_evaluate_damage_amount(qa_q1qc_machine *machine,
                                      const qa_q1qc_damage_scale *scale,
                                      qa_actor_id owner, int64_t actor_reference,
                                      double seconds, double amount);

#endif
