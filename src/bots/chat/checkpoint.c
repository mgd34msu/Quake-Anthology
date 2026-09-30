#include "internal.h"
#include "../checkpoint_internal.h"

static bool range_valid(qa_bot_chat_range range, size_t count) {
    return range.first <= count && range.count <= count - range.first;
}
static bool asset_valid(const qa_bot_chat_asset_view *v) {
    if (v == NULL || v->path == NULL || v->name == NULL || v->kind < QA_BOT_CHAT_SYNONYMS ||
        v->kind > QA_BOT_CHAT_INITIAL)
        return false;
#define ARRAY_VALID(field, count, type)                                                            \
    if (v->count >= UINT32_MAX || v->count > SIZE_MAX / sizeof(type) ||                            \
        (v->count != 0 && v->field == NULL))                                                       \
    return false
    ARRAY_VALID(messages, message_count, const char *);
    ARRAY_VALID(alternatives, alternative_count, const char *);
    ARRAY_VALID(synonyms, synonym_count, qa_bot_chat_synonym);
    ARRAY_VALID(groups, group_count, qa_bot_chat_synonyms);
    ARRAY_VALID(lists, list_count, qa_bot_chat_list);
    ARRAY_VALID(pieces, piece_count, qa_bot_chat_piece);
    ARRAY_VALID(templates, template_count, qa_bot_chat_template);
    ARRAY_VALID(keys, key_count, qa_bot_chat_key);
    ARRAY_VALID(replies, reply_count, qa_bot_chat_reply);
#undef ARRAY_VALID
    if (v->kind != QA_BOT_CHAT_SYNONYMS && (v->synonym_count != 0 || v->group_count != 0))
        return false;
    if (v->kind != QA_BOT_CHAT_RANDOMS && v->kind != QA_BOT_CHAT_INITIAL && v->list_count != 0)
        return false;
    if (v->kind != QA_BOT_CHAT_MATCHES && v->template_count != 0)
        return false;
    if (v->kind != QA_BOT_CHAT_REPLIES && (v->key_count != 0 || v->reply_count != 0))
        return false;
    if (v->kind != QA_BOT_CHAT_MATCHES && v->kind != QA_BOT_CHAT_REPLIES &&
        (v->piece_count != 0 || v->alternative_count != 0))
        return false;
    if ((v->kind == QA_BOT_CHAT_SYNONYMS || v->kind == QA_BOT_CHAT_MATCHES) &&
        v->message_count != 0)
        return false;
    for (size_t i = 0; i < v->message_count; ++i)
        if (v->messages[i] == NULL || strlen(v->messages[i]) >= 256)
            return false;
    for (size_t i = 0; i < v->alternative_count; ++i)
        if (v->alternatives[i] == NULL)
            return false;
    for (size_t i = 0; i < v->synonym_count; ++i)
        if (v->synonyms[i].text == NULL || v->synonyms[i].text[0] == 0 ||
            !isfinite(v->synonyms[i].weight))
            return false;
    for (size_t i = 0; i < v->group_count; ++i) {
        const qa_bot_chat_synonyms *group = v->groups + i;
        if (group->entries.count < 2 || !range_valid(group->entries, v->synonym_count))
            return false;
    }
    for (size_t i = 0; i < v->list_count; ++i)
        if (v->lists[i].name == NULL || !range_valid(v->lists[i].messages, v->message_count))
            return false;
    for (size_t i = 0; i < v->piece_count; ++i) {
        const qa_bot_chat_piece *piece = v->pieces + i;
        if (piece->kind == QA_BOT_CHAT_VARIABLE) {
            if (piece->data.variable >= 8)
                return false;
        } else if (piece->kind != QA_BOT_CHAT_ALTERNATIVES || piece->data.alternatives.count == 0 ||
                   !range_valid(piece->data.alternatives, v->alternative_count))
            return false;
    }
    for (size_t i = 0; i < v->template_count; ++i)
        if (v->templates[i].pieces.count == 0 ||
            !range_valid(v->templates[i].pieces, v->piece_count))
            return false;
    for (size_t i = 0; i < v->key_count; ++i) {
        const qa_bot_chat_key *key = v->keys + i;
        if (key->mode < QA_BOT_CHAT_ANY || key->mode > QA_BOT_CHAT_NOT)
            return false;
        switch (key->kind) {
        case QA_BOT_CHAT_NAME:
            break;
        case QA_BOT_CHAT_GENDER:
            if (key->data.gender > 2)
                return false;
            break;
        case QA_BOT_CHAT_BOT_NAMES:
        case QA_BOT_CHAT_WORD:
            if (key->data.text == NULL)
                return false;
            break;
        case QA_BOT_CHAT_PATTERN:
            if (key->data.pieces.count == 0 || !range_valid(key->data.pieces, v->piece_count))
                return false;
            break;
        default:
            return false;
        }
    }
    for (size_t i = 0; i < v->reply_count; ++i) {
        const qa_bot_chat_reply *reply = v->replies + i;
        if (!isfinite(reply->priority) || reply->keys.count == 0 ||
            !range_valid(reply->keys, v->key_count) ||
            !range_valid(reply->messages, v->message_count))
            return false;
    }
    return true;
}
static const char *copy_text(qa_bot_chat_asset *a, const char *text, qa_error *e) {
    return bot_string(&a->arena, (qa_bytes){(const uint8_t *)text, strlen(text)}, e);
}
bool qa_bot_chat_asset_restore(const qa_bot_chat_asset_view *v, qa_bot_chat_asset **out,
                               qa_error *e) {
    if (out == NULL || !asset_valid(v)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid bot chat asset checkpoint");
        return false;
    }
    qa_bot_chat_asset *a;
    if (!chat_asset_allocate(v->kind, v->path, v->name, &a, e))
        return false;
#define COPY_ARRAY(field, count, capacity)                                                         \
    do {                                                                                           \
        if (!bot_grow((void **)&a->field, &a->capacity, v->count, sizeof(*a->field), e))           \
            goto fail;                                                                             \
        if (v->count != 0)                                                                         \
            memcpy(a->field, v->field, v->count * sizeof(*a->field));                              \
        a->view.count = v->count;                                                                  \
    } while (0)
    COPY_ARRAY(messages, message_count, message_capacity);
    COPY_ARRAY(alternatives, alternative_count, alternative_capacity);
    COPY_ARRAY(synonyms, synonym_count, synonym_capacity);
    COPY_ARRAY(groups, group_count, group_capacity);
    COPY_ARRAY(lists, list_count, list_capacity);
    COPY_ARRAY(pieces, piece_count, piece_capacity);
    COPY_ARRAY(templates, template_count, template_capacity);
    COPY_ARRAY(keys, key_count, key_capacity);
    COPY_ARRAY(replies, reply_count, reply_capacity);
#undef COPY_ARRAY
    for (size_t i = 0; i < v->message_count; ++i)
        if ((a->messages[i] = copy_text(a, v->messages[i], e)) == NULL)
            goto fail;
    for (size_t i = 0; i < v->alternative_count; ++i)
        if ((a->alternatives[i] = copy_text(a, v->alternatives[i], e)) == NULL)
            goto fail;
    for (size_t i = 0; i < v->synonym_count; ++i)
        if ((a->synonyms[i].text = copy_text(a, v->synonyms[i].text, e)) == NULL)
            goto fail;
    for (size_t i = 0; i < v->list_count; ++i)
        if ((a->lists[i].name = copy_text(a, v->lists[i].name, e)) == NULL)
            goto fail;
    for (size_t i = 0; i < v->key_count; ++i)
        if (v->keys[i].kind == QA_BOT_CHAT_WORD || v->keys[i].kind == QA_BOT_CHAT_BOT_NAMES)
            if ((a->keys[i].data.text = copy_text(a, v->keys[i].data.text, e)) == NULL)
                goto fail;
    if (!chat_asset_finish(a, e))
        goto fail;
    *out = a;
    return true;
fail:
    qa_bot_chat_asset_release(a);
    return false;
}
bool qa_bot_chat_capture_state(const qa_bot_chat *s, qa_bot_chat_state *out, qa_error *e) {
    if (s == NULL || out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing bot chat checkpoint input/output");
        return false;
    }
    qa_bot_chat_state state = {.client = s->client,
                               .gender = s->gender,
                               .last_handle = s->last_handle,
                               .console_count = s->console_count};
    memcpy(state.name, s->name, sizeof(state.name));
    memcpy(state.message, s->message, sizeof(state.message));
    if (state.console_count != 0) {
        if (state.console_count > SIZE_MAX / sizeof(*state.console))
            goto memory;
        state.console = malloc(state.console_count * sizeof(*state.console));
        if (state.console == NULL)
            goto memory;
        size_t n = 0;
        for (uint32_t i = s->first_console; i != QA_BOT_NO_INDEX; i = s->system->console[i].next)
            state.console[n++] = s->system->console[i].message;
    }
    *out = state;
    return true;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, 0, "Capturing bot console messages");
    return false;
}
static bool valid_state(const qa_bot_chat_state *state, qa_error *e) {
    if (state == NULL || state->gender > 2 || state->last_handle > 8192 ||
        memchr(state->name, 0, sizeof(state->name)) == NULL ||
        memchr(state->message, 0, sizeof(state->message)) == NULL ||
        state->console_count >= UINT32_MAX || (state->console_count != 0 && state->console == NULL))
        goto invalid;
    for (size_t i = 0; i < state->console_count; ++i)
        if (state->console[i].handle == 0 || state->console[i].handle > 8192 ||
            !isfinite(state->console[i].time) ||
            memchr(state->console[i].text, 0, sizeof(state->console[i].text)) == NULL)
            goto invalid;
    return true;
invalid:
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid per-bot chat checkpoint");
    return false;
}
static void clear_console(qa_bot_chat *s) {
    qa_bot_chat_system *system=s->system;
    uint32_t index=s->first_console;
    while (index!=QA_BOT_NO_INDEX) {
        chat_console_cell *cell=&system->console[index];
        uint32_t next=cell->next;
        cell->next=system->free_console;cell->previous=QA_BOT_NO_INDEX;
        system->free_console=index;
        index=next;
    }
    system->console_count-=s->console_count;
    s->console_count=0;s->first_console=s->last_console=QA_BOT_NO_INDEX;
}
static void commit_state(qa_bot_chat *s, const qa_bot_chat_state *state) {
    s->client=state->client;s->gender=state->gender;s->last_handle=state->last_handle;
    memcpy(s->name,state->name,sizeof(s->name));memcpy(s->message,state->message,sizeof(s->message));
    qa_bot_chat_system *system=s->system;
    for (size_t i=0;i<state->console_count;++i) {
        uint32_t index=system->free_console;
        chat_console_cell *cell=&system->console[index];
        system->free_console=cell->next;
        *cell=(chat_console_cell){.message=state->console[i],.previous=s->last_console,.next=QA_BOT_NO_INDEX};
        if (s->last_console!=QA_BOT_NO_INDEX) system->console[s->last_console].next=index;
        else s->first_console=index;
        s->last_console=index;++s->console_count;++system->console_count;
    }
}
bool qa_bot_chat_restore_state(qa_bot_chat *s, const qa_bot_chat_state *state, qa_error *e) {
    if (!s || s->retired || s->system->restoring || !valid_state(state,e)) return false;
    size_t other = s->system->console_count - s->console_count;
    if (state->console_count > SIZE_MAX - other ||
        !chat_reserve_console(s->system, other + state->console_count, e))
        return false;
    clear_console(s);commit_state(s,state);
    return true;
}
struct bot_chat_restore {
    const bot_chat_restore_entry *entries;
    size_t count;
    qa_bot_chat_system *system;
};
bool bot_chat_restore_prepare(const bot_chat_restore_entry *entries, size_t count,
                              bot_chat_restore **out, qa_error *e) {
    *out=NULL;
    if (!count) return true;
    qa_bot_chat_system *system=entries[0].chat?entries[0].chat->system:NULL;
    if (!system || system->retired || system->restoring) goto invalid;
    size_t removed=0,added=0;
    for (size_t i=0;i<count;++i) {
        qa_bot_chat *s=entries[i].chat;
        if (!s || s->retired || s->system!=system || !valid_state(entries[i].state,e)) goto invalid;
        for (size_t j=0;j<i;++j) if (entries[j].chat==s) goto invalid;
        if (s->console_count>SIZE_MAX-removed || entries[i].state->console_count>SIZE_MAX-added) goto invalid;
        removed+=s->console_count;added+=entries[i].state->console_count;
    }
    if (removed>system->console_count || added>SIZE_MAX-(system->console_count-removed)) goto invalid;
    bot_chat_restore *prepared=malloc(sizeof(*prepared));
    if (!prepared) { qa_error_set(e,QA_ERROR_MEMORY,0,"preparing shared bot chat checkpoint");return false; }
    if (!chat_reserve_console(system,system->console_count-removed+added,e)) {free(prepared);return false;}
    *prepared=(bot_chat_restore){.entries=entries,.count=count,.system=system};
    system->restoring=true;
    *out=prepared;return true;
invalid:
    qa_error_set(e,QA_ERROR_FORMAT,0,"invalid shared bot chat checkpoint bindings");return false;
}
void bot_chat_restore_finish(bot_chat_restore *prepared, bool commit) {
    if (!prepared) return;
    if (commit) {
        for (size_t i=0;i<prepared->count;++i) clear_console(prepared->entries[i].chat);
        for (size_t i=0;i<prepared->count;++i) commit_state(prepared->entries[i].chat,prepared->entries[i].state);
    }
    prepared->system->restoring=false;
    free(prepared);
}
void qa_bot_chat_state_free(qa_bot_chat_state *state) {
    if (state != NULL) {
        free(state->console);
        *state = (qa_bot_chat_state){0};
    }
}
