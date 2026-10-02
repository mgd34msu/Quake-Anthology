#include "internal.h"
#include "../save_fields.h"
#include "qa/bot_assets_save.h"
#include "source_weapon_standalone_save.h"
#include "items_source.h"

bool bot_save_weapons_fields(qa_source_save_io *io, const qa_bot_weapons *source,
                             qa_bot_weapons **out)
{
    return bot_weapons_standalone_fields(io,source,out);
}

bool bot_save_items_fields(qa_source_save_io *io, const qa_bot_items *source, qa_bot_items **out)
{
    return bot_items_standalone_fields(io, source, out);
}

#define ASSET_API(name, type, tag) \
bool qa_bot_##name##_save_capture(const type *source, qa_buffer *out, qa_error *error) \
{ \
    if (!source || !out) { \
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing bot " #name " capture owner/output"); \
        return false; \
    } \
    static const uint8_t magic[8] = tag; \
    qa_source_save_io io = {0}; \
    bool ok = qa_source_save_writer(&io, NULL, error) && bot_save_signature(&io, magic) && \
        bot_save_##name##_fields(&io, source, NULL) && qa_source_save_finish(&io, out); \
    qa_source_save_dispose(&io); \
    return ok; \
} \
bool qa_bot_##name##_save_restore(qa_bytes bytes, type **out, qa_error *error) \
{ \
    if (!out || *out) { \
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot " #name " restore requires an empty output"); \
        return false; \
    } \
    static const uint8_t magic[8] = tag; \
    qa_source_save_io io = {0}; type *candidate = NULL; \
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && bot_save_signature(&io, magic) && \
        bot_save_##name##_fields(&io, NULL, &candidate) && qa_source_save_finish(&io, NULL); \
    qa_source_save_dispose(&io); \
    if (!ok) { qa_bot_##name##_release(candidate); return false; } \
    *out = candidate; \
    return true; \
}

ASSET_API(weapons, qa_bot_weapons, "QABWRAW")
ASSET_API(items, qa_bot_items, "QABITM2")
#undef ASSET_API
