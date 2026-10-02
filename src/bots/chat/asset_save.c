#include "internal.h"
#include "../save_fields.h"
#include "qa/bot_chat_save.h"
#include "source_initial_save.h"

static const uint8_t magic[8] = {'Q', 'A', 'B', 'C', 'H', 'A', 'T', 3};

static bool range_fields(qa_source_save_io *io, qa_bot_chat_range *range)
{
    return qa_source_save_u32(io, &range->first) && qa_source_save_u32(io, &range->count);
}

static void decoded_clear(qa_bot_chat_asset *asset)
{
    if (asset->messages)
        for (size_t i = 0; i < asset->view.message_count; ++i) free((void *)asset->messages[i]);
    if (asset->alternatives)
        for (size_t i = 0; i < asset->view.alternative_count; ++i) free((void *)asset->alternatives[i]);
    if (asset->synonyms)
        for (size_t i = 0; i < asset->view.synonym_count; ++i) free((void *)asset->synonyms[i].text);
    if (asset->lists)
        for (size_t i = 0; i < asset->view.list_count; ++i) free((void *)asset->lists[i].name);
    if (asset->keys)
        for (size_t i = 0; i < asset->view.key_count; ++i)
            if (asset->keys[i].kind == QA_BOT_CHAT_WORD || asset->keys[i].kind == QA_BOT_CHAT_BOT_NAMES)
                free((void *)asset->keys[i].data.text);
    free((void *)asset->view.path); free((void *)asset->view.name);
    free(asset->messages); free(asset->alternatives); free(asset->synonyms); free(asset->groups);
    free(asset->lists); free(asset->pieces); free(asset->templates); free(asset->keys); free(asset->replies);
    free(asset->cooldowns);
}

bool bot_save_chat_asset_fields(qa_source_save_io *io, const qa_bot_chat_asset *source,
                                 qa_bot_chat_asset **out)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t raw=!reading && source && source->initial_source?1u:
        !reading && source && source->packed_source?2u:0u;
    if(!qa_source_save_u32(io,&raw) || raw>2) return false;
    if(raw==1) return bot_chat_initial_asset_fields(io,source,NULL,NULL,out);
    if(raw==2) return bot_chat_packed_fields(io,source,NULL,NULL,out);
    qa_bot_chat_asset decoded = {0};
    qa_bot_chat_asset_view view = reading ? (qa_bot_chat_asset_view){0} : source->view;
    uint32_t kind = (uint32_t)view.kind;
    bool ok = qa_source_save_u32(io, &kind) && kind <= QA_BOT_CHAT_INITIAL &&
        bot_save_text(io, &view.path) && view.path && bot_save_text(io, &view.name) && view.name;
    view.kind = (qa_bot_chat_asset_kind)kind;
    size_t minimum = 0;
#define COUNT(field, width) \
    if (ok) { \
        ok = qa_source_save_count(io, &view.field, UINT32_MAX - 1u); \
        if (ok && view.field > (SIZE_MAX - minimum) / (width)) \
            ok = bot_save_fail(io, QA_ERROR_FORMAT, "Bot chat asset extent overflow"); \
        if (ok) minimum += view.field * (width); \
    }
    COUNT(message_count, 9); COUNT(alternative_count, 9); COUNT(synonym_count, 13);
    COUNT(group_count, 16); COUNT(list_count, 17); COUNT(piece_count, 8);
    COUNT(template_count, 20); COUNT(key_count, 8); COUNT(reply_count, 20);
#undef COUNT
    decoded.view = view;
    if (reading && ok && minimum > io->input.size - io->offset)
        ok = bot_save_fail(io, QA_ERROR_FORMAT, "Truncated bot chat asset tables");
#define ALLOC(field, count) \
    if (reading && ok && view.count) { \
        if (view.count > SIZE_MAX / sizeof(*decoded.field)) \
            ok = bot_save_fail(io, QA_ERROR_FORMAT, "Bot chat asset allocation overflow"); \
        else if (!(decoded.field = calloc(view.count, sizeof(*decoded.field)))) \
            ok = bot_save_fail(io, QA_ERROR_MEMORY, "Restoring bot chat asset table"); \
    }
    ALLOC(messages, message_count); ALLOC(alternatives, alternative_count); ALLOC(synonyms, synonym_count);
    ALLOC(groups, group_count); ALLOC(lists, list_count); ALLOC(pieces, piece_count);
    ALLOC(templates, template_count); ALLOC(keys, key_count); ALLOC(replies, reply_count);
#undef ALLOC
    for (size_t i = 0; ok && i < view.message_count; ++i) {
        const char *text = reading ? NULL : view.messages[i];
        ok = bot_save_text(io, &text) && text;
        if (reading) decoded.messages[i] = text;
    }
    for (size_t i = 0; ok && i < view.alternative_count; ++i) {
        const char *text = reading ? NULL : view.alternatives[i];
        ok = bot_save_text(io, &text) && text;
        if (reading) decoded.alternatives[i] = text;
    }
    for (size_t i = 0; ok && i < view.synonym_count; ++i) {
        qa_bot_chat_synonym value = reading ? (qa_bot_chat_synonym){0} : view.synonyms[i];
        ok = bot_save_text(io, &value.text) && value.text;
        if (reading) decoded.synonyms[i] = value;
        if (ok) ok = qa_source_save_f32(io, &value.weight);
        if (reading) decoded.synonyms[i] = value;
    }
    for (size_t i = 0; ok && i < view.group_count; ++i) {
        qa_bot_chat_synonyms value = reading ? (qa_bot_chat_synonyms){0} : view.groups[i];
        ok = qa_source_save_u32(io, &value.context) && qa_source_save_f32(io, &value.total_weight) &&
            range_fields(io, &value.entries);
        if (reading) decoded.groups[i] = value;
    }
    for (size_t i = 0; ok && i < view.list_count; ++i) {
        qa_bot_chat_list value = reading ? (qa_bot_chat_list){0} : view.lists[i];
        ok = bot_save_text(io, &value.name) && value.name;
        if (reading) decoded.lists[i] = value;
        if (ok) ok = range_fields(io, &value.messages);
        if (reading) decoded.lists[i] = value;
    }
    for (size_t i = 0; ok && i < view.piece_count; ++i) {
        qa_bot_chat_piece value = reading ? (qa_bot_chat_piece){0} : view.pieces[i];
        uint32_t tag = (uint32_t)value.kind;
        ok = qa_source_save_u32(io, &tag);
        value.kind = (qa_bot_chat_piece_kind)tag;
        if (ok && tag == QA_BOT_CHAT_VARIABLE) ok = qa_source_save_u32(io, &value.data.variable);
        else if (ok && tag == QA_BOT_CHAT_ALTERNATIVES) ok = range_fields(io, &value.data.alternatives);
        else if (ok) ok = bot_save_fail(io, QA_ERROR_FORMAT, "Unknown bot chat piece kind");
        if (reading) decoded.pieces[i] = value;
    }
    for (size_t i = 0; ok && i < view.template_count; ++i) {
        qa_bot_chat_template value = reading ? (qa_bot_chat_template){0} : view.templates[i];
        ok = qa_source_save_u32(io, &value.context) && qa_source_save_i32(io, &value.type) &&
            qa_source_save_i32(io, &value.subtype) && range_fields(io, &value.pieces);
        if (reading) decoded.templates[i] = value;
    }
    for (size_t i = 0; ok && i < view.key_count; ++i) {
        qa_bot_chat_key value = reading ? (qa_bot_chat_key){0} : view.keys[i];
        uint32_t mode = (uint32_t)value.mode, tag = (uint32_t)value.kind;
        ok = qa_source_save_u32(io, &mode) && mode <= QA_BOT_CHAT_NOT && qa_source_save_u32(io, &tag);
        value.mode = (qa_bot_chat_key_mode)mode; value.kind = (qa_bot_chat_key_kind)tag;
        if (ok) switch (value.kind) {
        case QA_BOT_CHAT_NAME: break;
        case QA_BOT_CHAT_GENDER: ok = qa_source_save_u32(io, &value.data.gender); break;
        case QA_BOT_CHAT_BOT_NAMES:
        case QA_BOT_CHAT_WORD: ok = bot_save_text(io, &value.data.text) && value.data.text; break;
        case QA_BOT_CHAT_PATTERN: ok = range_fields(io, &value.data.pieces); break;
        default: ok = bot_save_fail(io, QA_ERROR_FORMAT, "Unknown bot chat key kind"); break;
        }
        if (reading) decoded.keys[i] = value;
    }
    for (size_t i = 0; ok && i < view.reply_count; ++i) {
        qa_bot_chat_reply value = reading ? (qa_bot_chat_reply){0} : view.replies[i];
        ok = qa_source_save_f32(io, &value.priority) && range_fields(io, &value.keys) && range_fields(io, &value.messages);
        if (reading) decoded.replies[i] = value;
    }
    size_t cooldown_count = 0;
    const float *cooldowns = reading ? NULL : qa_bot_chat_cooldowns(source, &cooldown_count);
    if (ok) ok = qa_source_save_count(io, &cooldown_count, view.message_count) &&
        cooldown_count == ((view.kind == QA_BOT_CHAT_INITIAL || view.kind == QA_BOT_CHAT_REPLIES) ? view.message_count : 0);
    if (ok && reading && cooldown_count) {
        if (cooldown_count > (io->input.size - io->offset) / 4 || cooldown_count > SIZE_MAX / sizeof(float))
            ok = bot_save_fail(io, QA_ERROR_FORMAT, "Truncated bot chat cooldowns");
        else if (!(decoded.cooldowns = malloc(cooldown_count * sizeof(*decoded.cooldowns))))
            ok = bot_save_fail(io, QA_ERROR_MEMORY, "Restoring bot chat cooldowns");
    }
    for (size_t i = 0; ok && i < cooldown_count; ++i) {
        float value = reading ? 0 : cooldowns[i];
        ok = qa_source_save_f32(io, &value);
        if (reading) decoded.cooldowns[i] = value;
    }
    qa_bot_chat_asset *candidate = NULL;
    if (ok && reading) {
        chat_asset_view(&decoded);
        ok = qa_bot_chat_asset_restore(&decoded.view, &candidate, io->error) &&
            qa_bot_chat_cooldowns_restore(candidate, decoded.cooldowns, cooldown_count, io->error);
        if (!ok) io->failed = true;
    }
    if (reading) {
        decoded_clear(&decoded);
        if (ok) *out = candidate;
        else qa_bot_chat_asset_release(candidate);
    }
    if (!ok && !io->failed)
        return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid bot chat asset continuation");
    return ok;
}

bool qa_bot_chat_asset_capture(const qa_bot_chat_asset *asset, qa_buffer *out, qa_error *error)
{
    if (!asset || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing bot chat asset/output"); return false;
    }
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && bot_save_signature(&io, magic) &&
        bot_save_chat_asset_fields(&io, asset, NULL) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}

bool qa_bot_chat_asset_restore_bytes(qa_bytes bytes, qa_bot_chat_asset **out, qa_error *error)
{
    if (!out || *out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot chat asset restore requires empty output"); return false;
    }
    qa_source_save_io io = {0}; qa_bot_chat_asset *candidate = NULL;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && bot_save_signature(&io, magic) &&
        bot_save_chat_asset_fields(&io, NULL, &candidate) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) { qa_bot_chat_asset_release(candidate); return false; }
    *out = candidate; return true;
}
