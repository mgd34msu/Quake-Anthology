/* Port of src/compat/q2/native-mod-armor.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_ARMOR_H
#define QA_COMPAT_Q2_NATIVE_MOD_ARMOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/foundation/support.h"
#include "qa/contracts/gameplay.h"

typedef struct qa_compat_q2_native_mod_armor_state {
  qa_native_mod_armor definition; const qa_native_mod_declaration *declaration; void *host_ctx;
  void *scalar_ctx; double (*scalar)(void *ctx, uint64_t base, qa_native_mod_scalar_field f, bool has_value, double value);
  uint64_t (*entity_base)(void *ctx, double slot); bool (*client_base)(void *ctx, double slot, uint64_t *out);
} qa_compat_q2_native_mod_armor_state;
void qa_compat_q2_native_mod_armor_state_init(qa_compat_q2_native_mod_armor_state *s, qa_native_mod_armor def, const qa_native_mod_declaration *decl);
double qa_compat_q2_native_mod_armor_scalar_bytes(qa_native_mod_scalar_field f);
void qa_compat_q2_native_mod_armor_validate_value(qa_native_mod_scalar_field f, double value);
#endif
