/* Port of src/compat/q2/native-mod-armor.ts. This decodes source storage; the
 * original damage body owns absorption and cell costs. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_ARMOR_H
#define QA_COMPAT_Q2_NATIVE_MOD_ARMOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_mod_host_api.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/foundation/support.h"

/* TS scalar(base, field, value?): writes when has_value, then reads. */
typedef double (*qa_compat_q2_native_mod_scalar_fn)(void *ctx, qa_guest_address base, qa_native_mod_scalar_field field, bool has_value, double value);
typedef struct qa_compat_q2_native_armor_regular {
  qa_item_id item; const qa_native_mod_armor_selection *selection; const qa_native_mod_armor_field *points; double normal_protection, energy_protection;
} qa_compat_q2_native_armor_regular;
typedef struct qa_compat_q2_native_mod_armor_state {
  qa_native_mod_armor definition; const qa_native_mod_declaration *declaration; const qa_compat_q2_native_mod_host *host;
  void *scalar_ctx; qa_compat_q2_native_mod_scalar_fn scalar;
  qa_native_mod_armor_field *fields; size_t field_count;
  qa_compat_q2_native_armor_regular *regular; size_t regular_count; /* uniform view of q2_regular|regular */
} qa_compat_q2_native_mod_armor_state;
void qa_compat_q2_native_mod_armor_state_init(qa_compat_q2_native_mod_armor_state *s, qa_native_mod_armor definition, const qa_native_mod_declaration *declaration,
    const qa_compat_q2_native_mod_host *host, qa_compat_q2_native_mod_scalar_fn scalar, void *scalar_ctx);
void qa_compat_q2_native_mod_armor_state_destroy(qa_compat_q2_native_mod_armor_state *s);
double qa_compat_q2_native_mod_armor_scalar_bytes(qa_native_mod_scalar_field field);
void qa_compat_q2_native_mod_armor_validate_value(qa_native_mod_scalar_field field, double value);
qa_armor_state qa_compat_q2_native_mod_armor_read(qa_compat_q2_native_mod_armor_state *s, double slot);
double qa_compat_q2_native_mod_armor_read_count(qa_compat_q2_native_mod_armor_state *s, double slot, qa_native_mod_armor_field field);
void qa_compat_q2_native_mod_armor_write_count(qa_compat_q2_native_mod_armor_state *s, double slot, qa_native_mod_armor_field field, double value);
qa_armor_state qa_compat_q2_native_mod_armor_normalize_legacy(qa_compat_q2_native_mod_armor_state *s, double slot, qa_armor_state armor);
void qa_compat_q2_native_mod_armor_validate_write(qa_compat_q2_native_mod_armor_state *s, double slot, qa_armor_state armor);
void qa_compat_q2_native_mod_armor_write(qa_compat_q2_native_mod_armor_state *s, double slot, qa_armor_state armor);
/* TS observe(slot, changed) returning its remover. */
typedef void (*qa_compat_q2_native_armor_changed_fn)(void *ctx, qa_armor_state before, qa_armor_state after);
typedef struct qa_compat_q2_native_armor_observation qa_compat_q2_native_armor_observation;
qa_compat_q2_native_armor_observation *qa_compat_q2_native_mod_armor_observe(qa_compat_q2_native_mod_armor_state *s, double slot, void *ctx, qa_compat_q2_native_armor_changed_fn changed);
/* Removes the observers and frees (idempotent over the stop flag). */
void qa_compat_q2_native_armor_observation_close(qa_compat_q2_native_armor_observation *observation);
#endif
