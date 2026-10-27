/* Port of src/compat/q2/native-mod-items.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_ITEMS_H
#define QA_COMPAT_Q2_NATIVE_MOD_ITEMS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/foundation/support.h"
#include "qa/contracts/native_mod_items.h"
#include "qa/persistence/value.h"

typedef struct qa_compat_q2_native_item_checkpoint { const double *counts; size_t count; } qa_compat_q2_native_item_checkpoint;
void qa_compat_q2_validate_native_mod_items(const qa_native_mod_declaration *declaration);
typedef struct qa_compat_q2_native_mod_items_state { void *host_ctx; const qa_native_mod_declaration *declaration; bool closed; } qa_compat_q2_native_mod_items_state;
void qa_compat_q2_native_mod_items_state_init(qa_compat_q2_native_mod_items_state *s, void *ctx, const qa_native_mod_declaration *d);
void qa_compat_q2_native_mod_items_state_close(qa_compat_q2_native_mod_items_state *s);
#endif
