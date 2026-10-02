#include "internal.h"
#include "../save_fields.h"
#include "qa/bot_assets_save.h"
#include "source_fuzzy_standalone_save.h"
#include "character_source.h"

bool bot_save_weights_fields(qa_source_save_io *io, const qa_bot_weights *source,
                             qa_bot_weights **out)
{
    return bot_weights_source_fields(io,source,out);
}

bool bot_save_character_fields(qa_source_save_io *io, const qa_bot_character *source,
                               qa_bot_character **out)
{
    return bot_character_standalone_fields(io, source, out);
}

static const uint8_t weight_magic[8] = {'Q', 'A', 'B', 'W', 'R', 'A', 'W', 0};
static const uint8_t character_magic[8] = {'Q', 'A', 'B', 'C', 'H', 'A', 'R', 2};

bool qa_bot_weights_save_capture(const qa_bot_weights *source, qa_buffer *out, qa_error *error)
{
    if (!source || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing bot weight capture owner/output");
        return false;
    }
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && bot_save_signature(&io, weight_magic) &&
        bot_save_weights_fields(&io, source, NULL) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool qa_bot_weights_save_restore(qa_bytes bytes, qa_bot_weights **out, qa_error *error)
{
    if (!out || *out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot weight restore requires an empty output");
        return false;
    }
    qa_source_save_io io = {0}; qa_bot_weights *candidate = NULL;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && bot_save_signature(&io, weight_magic) &&
        bot_save_weights_fields(&io, NULL, &candidate) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) { qa_bot_weights_release(candidate); return false; }
    *out = candidate;
    return true;
}

bool qa_bot_character_save_capture(const qa_bot_character *source, qa_buffer *out, qa_error *error)
{
    if (!source || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing bot character capture owner/output");
        return false;
    }
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && bot_save_signature(&io, character_magic) &&
        bot_save_character_fields(&io, source, NULL) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool qa_bot_character_save_restore(qa_bytes bytes, qa_bot_character **out, qa_error *error)
{
    if (!out || *out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot character restore requires an empty output");
        return false;
    }
    qa_source_save_io io = {0}; qa_bot_character *candidate = NULL;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && bot_save_signature(&io, character_magic) &&
        bot_save_character_fields(&io, NULL, &candidate) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) { qa_bot_character_release(candidate); return false; }
    *out = candidate;
    return true;
}
