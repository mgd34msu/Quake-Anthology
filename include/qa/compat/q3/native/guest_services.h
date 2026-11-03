/*
 * Port of src/compat/q3/native/guest-services.ts: Q_strncpyz and
 * Cvar_Update for native guest memory, shared by the Quake Live import
 * table and the native server-game host. GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_Q3_NATIVE_GUEST_SERVICES_H
#define QA_COMPAT_Q3_NATIVE_GUEST_SERVICES_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/guest/core/memory.h"

/* Q_strncpyz into guest memory: at most capacity - 1 characters and a terminator, the rest of the capacity zeroed. */
void qa_compat_q3_write_guest_string(qa_sparse_guest_memory *memory, qa_guest_core_address address, const char *value,
                                     int64_t capacity);
/* CvarRegistry.readVm pick: false for an unknown handle; *out_value is malloc'd (caller frees). */
typedef bool (*qa_compat_q3_cvar_read_vm_fn)(void *ctx, int64_t handle, double *out_modification_count, char **out_value,
                                             double *out_numeric_value, double *out_integer_value);
/* Cvar_Update for one vmCvar_t, which has the same int/int/float/int/char[256] layout on both guest widths. */
void qa_compat_q3_update_guest_cvar(qa_sparse_guest_memory *memory, qa_compat_q3_cvar_read_vm_fn read_vm, void *ctx,
                                    qa_guest_core_address address);

#endif
