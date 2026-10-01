#include "internal.h"
#include "../save_fields.h"
#include "qa/bot_assets_save.h"
#include "source_fuzzy_standalone_save.h"

bool bot_save_weights_fields(qa_source_save_io *io, const qa_bot_weights *source,
                             qa_bot_weights **out)
{
    return bot_weights_source_fields(io,source,out);
}

bool bot_save_character_fields(qa_source_save_io *io, const qa_bot_character *source,
                               qa_bot_character **out)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_bot_character_view view = reading ? (qa_bot_character_view){0} : *qa_bot_character_read(source);
    bool ok = bot_save_text(io, &view.path) && view.path && qa_source_save_f32(io, &view.skill);
    for (size_t i = 0; ok && i < QA_BOT_CHARACTERISTICS; ++i) {
        qa_bot_character_value *value = view.values + i;
        uint32_t kind = (uint32_t)value->kind;
        ok = qa_source_save_u32(io, &kind);
        value->kind = (qa_bot_character_value_kind)kind;
        if (!ok)
            break;
        switch (value->kind) {
        case QA_BOT_CHARACTER_UNSET: break;
        case QA_BOT_CHARACTER_INTEGER: ok = qa_source_save_i32(io, &value->data.integer); break;
        case QA_BOT_CHARACTER_FLOAT: ok = qa_source_save_f32(io, &value->data.number); break;
        case QA_BOT_CHARACTER_STRING: ok = bot_save_text(io, &value->data.string) && value->data.string; break;
        default: ok = bot_save_fail(io, QA_ERROR_FORMAT, "Unknown bot character value kind"); break;
        }
    }
    qa_bot_character *candidate = NULL;
    if (ok && reading) {
        ok = qa_bot_character_restore(&view, &candidate, io->error);
        if (!ok)
            io->failed = true;
    }
    if (reading) {
        for (size_t i = 0; i < QA_BOT_CHARACTERISTICS; ++i)
            if (view.values[i].kind == QA_BOT_CHARACTER_STRING)
                free((void *)view.values[i].data.string);
        free((void *)view.path);
        if (ok)
            *out = candidate;
        else
            qa_bot_character_release(candidate);
    }
    if (!ok && !io->failed)
        return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid bot character fields");
    return ok;
}

static const uint8_t weight_magic[8] = {'Q', 'A', 'B', 'W', 'R', 'A', 'W', 0};
static const uint8_t character_magic[8] = {'Q', 'A', 'B', 'C', 'H', 'A', 'R', 0};

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
