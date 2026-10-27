/*
 * Port of src/content/mods/match.ts: source team/primary-match /
 * objective declaration readers. Objective Scalar/Reference/Call
 * payloads are opaque malloc'd elements read through fn+ctx
 * readers (nullable fields store NULL). Returned lists are
 * malloc'd; the list readers return NULL only on OOM.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MODS_MATCH_H
#define QA_CONTENT_MODS_MATCH_H

#include <stddef.h>

#include "qa/contracts/source_match.h"
#include "qa/persistence/value.h"

qa_source_team_value *qa_read_source_team_values(const qa_save_reader *reader,
                                                 size_t *out_count);
qa_source_team_alias *qa_read_source_team_aliases(const qa_save_reader *reader,
                                                  size_t *out_count);
qa_source_primary_match qa_read_source_primary_match(const qa_save_reader *reader,
                                                     double client_bytes);
qa_source_objective_declaration *qa_read_source_objectives(
    const qa_save_reader *reader,
    void (*read_scalar)(void *ctx, const qa_save_reader *reader, void *out),
    void *scalar_ctx, size_t scalar_size,
    void (*read_reference)(void *ctx, const qa_save_reader *reader, void *out),
    void *reference_ctx, size_t reference_size,
    void (*read_call)(void *ctx, const qa_save_reader *reader, void *out),
    void *call_ctx, size_t call_size, size_t *out_count);

#endif
