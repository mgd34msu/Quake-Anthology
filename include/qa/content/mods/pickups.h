/*
 * Port of src/content/mods/pickups.ts. The TS generic Call is
 * opaque malloc'd storage (call_elem_size) in the rule's
 * operation.gate/grant slots. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MODS_PICKUPS_H
#define QA_CONTENT_MODS_PICKUPS_H

#include <stddef.h>

#include "qa/contracts/original_pickups.h"
#include "qa/persistence/value.h"

qa_mod_pickup_rule qa_read_mod_pickup_rule(
    const qa_save_reader *reader,
    void (*read_call)(void *ctx, const qa_save_reader *reader, void *out),
    void *read_ctx, size_t call_elem_size);

#endif
