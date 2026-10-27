/*
 * Port of src/content/mods/item-actions.ts. The TS generic Call
 * payload is read through a fn+ctx reader into malloc'd storage
 * of call_elem_size bytes (caller frees use_call/drop_call).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MODS_ITEM_ACTIONS_H
#define QA_CONTENT_MODS_ITEM_ACTIONS_H

#include <stddef.h>

#include "qa/contracts/source_items.h"
#include "qa/persistence/value.h"

qa_source_item_action_calls qa_read_item_actions(
    const qa_save_reader *reader,
    void (*read_call)(void *ctx, const qa_save_reader *reader, void *out),
    void *read_ctx, size_t call_elem_size);

#endif
