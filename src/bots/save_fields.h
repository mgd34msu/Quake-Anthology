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
    if (!qa_source_save_bytes(io, magic, sizeof(magic)))
        return false;
    return !memcmp(magic, expected, sizeof(magic)) ? true :
        bot_save_fail(io, QA_ERROR_FORMAT, "Invalid bot continuation signature");
}

/* These strings belong to bot assets and never enter the canonical table. */
static inline bool bot_save_text(qa_source_save_io *io, const char **text)
{
    char *owned = io->direction == QA_SOURCE_SAVE_WRITE ? (char *)*text : NULL;
    if (!qa_source_save_owned_text(io, &owned))
        return false;
    if (io->direction == QA_SOURCE_SAVE_READ)
        *text = owned;
    return true;
}

bool bot_save_weights_fields(qa_source_save_io *, const qa_bot_weights *, qa_bot_weights **);
bool bot_save_character_fields(qa_source_save_io *, const qa_bot_character *, qa_bot_character **);
bool bot_save_weapons_fields(qa_source_save_io *, const qa_bot_weapons *, qa_bot_weapons **);
bool bot_save_items_fields(qa_source_save_io *, const qa_bot_items *, qa_bot_items **);
bool bot_save_chat_asset_fields(qa_source_save_io *, const qa_bot_chat_asset *, qa_bot_chat_asset **);

#endif
