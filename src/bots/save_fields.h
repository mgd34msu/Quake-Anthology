#ifndef QA_BOT_SAVE_FIELDS_H
#define QA_BOT_SAVE_FIELDS_H

#include "qa/source_save.h"
#include "qa/bot_library.h"
#include "qa/bot_chat.h"
#include <stdlib.h>
#include <string.h>

static inline bool bot_save_fail(qa_source_save_io *io, qa_status code, const char *message)
{
    if (!io->failed)
        qa_error_set(io->error, code, io->offset, "%s", message);
    io->failed = true;
    return false;
}

static inline bool bot_save_signature(qa_source_save_io *io, const uint8_t expected[8])
{
    uint8_t magic[8];
    memcpy(magic, expected, sizeof(magic));
    uint32_t version = 1;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || !qa_source_save_u32(io, &version))
        return false;
    return !memcmp(magic, expected, sizeof(magic)) && version == 1 ? true :
        bot_save_fail(io, QA_ERROR_FORMAT, "Unsupported bot continuation schema");
}

/* These strings belong to bot assets and never enter the canonical table. */
static inline bool bot_save_text(qa_source_save_io *io, const char **text)
{
    const char *source = io->direction == QA_SOURCE_SAVE_WRITE ? *text : NULL;
    bool present = source != NULL;
    if (!qa_source_save_bool(io, &present))
        return false;
    if (!present) {
        if (io->direction == QA_SOURCE_SAVE_READ)
            *text = NULL;
        return true;
    }
    size_t length = source ? strlen(source) : 0;
    if (!qa_source_save_count(io, &length, SIZE_MAX - 1))
        return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE)
        return qa_source_save_bytes(io, (void *)source, length);
    if (io->offset > io->input.size || length > io->input.size - io->offset)
        return bot_save_fail(io, QA_ERROR_FORMAT, "Truncated private bot string");
    char *owned = malloc(length + 1);
    if (!owned)
        return bot_save_fail(io, QA_ERROR_MEMORY, "Restoring private bot string");
    if (!qa_source_save_bytes(io, owned, length) || memchr(owned, 0, length)) {
        free(owned);
        return bot_save_fail(io, QA_ERROR_FORMAT, "Embedded NUL in private bot string");
    }
    owned[length] = 0;
    *text = owned;
    return true;
}

bool bot_save_weights_fields(qa_source_save_io *, const qa_bot_weights *, qa_bot_weights **);
bool bot_save_character_fields(qa_source_save_io *, const qa_bot_character *, qa_bot_character **);
bool bot_save_weapons_fields(qa_source_save_io *, const qa_bot_weapons *, qa_bot_weapons **);
bool bot_save_items_fields(qa_source_save_io *, const qa_bot_items *, qa_bot_items **);
bool bot_save_chat_asset_fields(qa_source_save_io *, const qa_bot_chat_asset *, qa_bot_chat_asset **);

#endif
