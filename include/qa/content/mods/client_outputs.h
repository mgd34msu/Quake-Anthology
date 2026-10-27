/*
 * Port of src/content/mods/client-outputs.ts. TS generics S/V
 * become opaque malloc'd elements (scalar_size/vector_size bytes)
 * stored in the declaration's field/height/min/max slots.
 * Returns a malloc'd array; NULL only on OOM. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MODS_CLIENT_OUTPUTS_H
#define QA_CONTENT_MODS_CLIENT_OUTPUTS_H

#include <stddef.h>

#include "qa/contracts/mod_client_outputs.h"
#include "qa/persistence/value.h"

qa_mod_client_output_declaration *qa_read_client_output_declarations(
    const qa_save_reader *reader,
    void (*read_scalar)(void *ctx, const qa_save_reader *reader, void *out),
    void *scalar_ctx, size_t scalar_size,
    void (*read_vector)(void *ctx, const qa_save_reader *reader, void *out),
    void *vector_ctx, size_t vector_size, size_t *out_count);

#endif
