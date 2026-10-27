/*
 * Port of src/content/mods/client-input.ts. TS generics Call /
 * Output become opaque element storage read through fn+ctx
 * readers (read_output may be NULL, mirroring the optional TS
 * parameter). Returns a malloc'd binding array (NULL only on
 * OOM, with *out_count unset); element call/output lists are
 * malloc'd too. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MODS_CLIENT_INPUT_H
#define QA_CONTENT_MODS_CLIENT_INPUT_H

#include <stddef.h>

#include "qa/contracts/mod_callbacks.h"
#include "qa/persistence/value.h"

qa_mod_client_input_binding *qa_read_mod_client_input(
    const qa_save_reader *reader,
    void (*read_call)(void *ctx, const qa_save_reader *reader, void *out),
    void *call_ctx, size_t call_elem_size,
    void (*read_output)(void *ctx, const qa_save_reader *reader, void *out),
    void *output_ctx, size_t output_elem_size, size_t *out_count);

#endif
