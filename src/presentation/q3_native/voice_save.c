#include "server_commands_internal.h"
#include "../q3/internal.h"

static bool text(qa_source_save_io *io, char *value, size_t capacity)
{
    return qa_source_save_bytes(io, value, capacity) && memchr(value, 0, capacity) != NULL;
}
static bool sound(qa_source_save_io *io, q3n_server_commands *o, int32_t *value)
{
    if (!qa_source_save_i32(io, value) || *value < 0) return false;
    return !*value || ((size_t)*value <= o->options.assets->sound_count &&
        o->options.assets->sounds[(size_t)*value - 1]);
}
bool q3n_voice_fields(qa_source_save_io *io, q3n_server_commands *o)
{
    q3n_voice *v = &o->voice;
    if (!qa_source_save_bool(io, &v->loaded) ||
        !qa_source_save_i32(io, &v->in) || v->in < 0 || v->in >= 32 ||
        !qa_source_save_i32(io, &v->out) || v->out < 0 || v->out > 32 ||
        !qa_source_save_i32(io, &v->time)) return false;
    for (unsigned i = 0; i < 8; ++i) {
        q3n_voice_list *list = &v->lists[i];
        if (!text(io, list->name, sizeof(list->name)) ||
            !qa_source_save_i32(io, &list->gender) || list->gender < QA_MODEL_MALE || list->gender > QA_MODEL_NEUTER ||
            !qa_source_save_i32(io, &list->count) || list->count < 0 || list->count > 64) return false;
        for (unsigned j = 0; j < 64; ++j) {
            q3n_voice_chat *chat = &list->chats[j];
            if (!text(io, chat->id, sizeof(chat->id)) || !qa_source_save_i32(io, &chat->count) ||
                chat->count < 0 || chat->count > 64) return false;
            for (unsigned k = 0; k < 64; ++k)
                if (!sound(io, o, &chat->sounds[k]) || !text(io, chat->text[k], sizeof(chat->text[k]))) return false;
        }
    }
    for (unsigned i = 0; i < 64; ++i)
        if (!text(io, v->heads[i].head, sizeof(v->heads[i].head)) ||
            !qa_source_save_i32(io, &v->heads[i].list) || v->heads[i].list < 0 || v->heads[i].list >= 8) return false;
    for (unsigned i = 0; i < 32; ++i) {
        q3n_buffered_voice *voice = &v->buffer[i];
        if (!qa_source_save_i32(io, &voice->client) || voice->client < 0 || voice->client >= 64 ||
            !sound(io, o, &voice->sound) || !qa_source_save_bool(io, &voice->voice_only) ||
            !text(io, voice->command, sizeof(voice->command)) || !text(io, voice->message, sizeof(voice->message))) return false;
    }
    qa_common_parser *parser = &v->parser;
    if (!qa_source_save_count(io, &parser->token_length, QA_COMMON_TOKEN_CAPACITY) ||
        !qa_source_save_count(io, &parser->name_length, QA_COMMON_TOKEN_CAPACITY - 1) ||
        !qa_source_save_bytes(io, parser->token, sizeof(parser->token)) ||
        !qa_source_save_bytes(io, parser->name, sizeof(parser->name)) ||
        parser->token[parser->token_length] || parser->name[parser->name_length] ||
        !qa_source_save_i32(io, &parser->line) || parser->line < 0) return false;
    if (o->options.product == QA_Q3_ARENA && (v->loaded || v->in || v->out || v->time)) return false;
    return !o->initialized || o->options.product != QA_Q3_TEAM_ARENA || v->loaded;
}
