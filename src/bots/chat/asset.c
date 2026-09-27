#include "internal.h"

void chat_asset_view(qa_bot_chat_asset *a) {
    a->view.messages = a->messages;
    a->view.alternatives = a->alternatives;
    a->view.synonyms = a->synonyms;
    a->view.groups = a->groups;
    a->view.lists = a->lists;
    a->view.pieces = a->pieces;
    a->view.templates = a->templates;
    a->view.keys = a->keys;
    a->view.replies = a->replies;
}
bool chat_asset_allocate(qa_bot_chat_asset_kind kind, const char *path, const char *name,
                         qa_bot_chat_asset **out, qa_error *e) {
    qa_bot_chat_asset *a = calloc(1, sizeof(*a));
    if (a == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating bot chat asset");
        return false;
    }
    atomic_init(&a->references, 1);
    a->view.kind = kind;
    a->view.path = bot_string(&a->arena, (qa_bytes){(const uint8_t *)path, strlen(path)}, e);
    if (name == NULL)
        name = "";
    a->view.name = bot_string(&a->arena, (qa_bytes){(const uint8_t *)name, strlen(name)}, e);
    if (a->view.path == NULL || a->view.name == NULL) {
        qa_bot_chat_asset_release(a);
        return false;
    }
    *out = a;
    return true;
}
void qa_bot_chat_asset_retain(qa_bot_chat_asset *a) {
    if (a != NULL)
        atomic_fetch_add_explicit(&a->references, 1, memory_order_relaxed);
}
void qa_bot_chat_asset_release(qa_bot_chat_asset *a) {
    if (a == NULL || atomic_fetch_sub_explicit(&a->references, 1, memory_order_acq_rel) != 1)
        return;
    free(a->messages);
    free(a->alternatives);
    free(a->synonyms);
    free(a->groups);
    free(a->lists);
    free(a->pieces);
    free(a->templates);
    free(a->keys);
    free(a->replies);
    free(a->cooldowns);
    qa_arena_destroy(&a->arena);
    free(a);
}
void bot_chat_assets_close(qa_bot_library *library) {
    for (qa_bot_chat_asset *a = library->chat_assets; a != NULL;) {
        qa_bot_chat_asset *next = a->next;
        qa_bot_chat_asset_release(a);
        a = next;
    }
    library->chat_assets = NULL;
}
const qa_bot_chat_asset_view *qa_bot_chat_asset_read(const qa_bot_chat_asset *a) {
    return a == NULL ? NULL : &a->view;
}
bool chat_asset_finish(qa_bot_chat_asset *a, qa_error *e) {
    chat_asset_view(a);
    if ((a->view.kind == QA_BOT_CHAT_INITIAL || a->view.kind == QA_BOT_CHAT_REPLIES) &&
        a->view.message_count != 0) {
        if (a->view.message_count > SIZE_MAX / sizeof(float)) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Bot chat cooldown capacity overflow");
            return false;
        }
        a->cooldowns = malloc(a->view.message_count * sizeof(*a->cooldowns));
        if (a->cooldowns == NULL) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating shared bot chat cooldowns");
            return false;
        }
        for (size_t i = 0; i < a->view.message_count; ++i)
            a->cooldowns[i] = -40;
    }
    return true;
}
bool chat_asset_parse(qa_bot_library *library, qa_bot_chat_asset *a, qa_error *e) {
    qa_script *s;
    if (!qa_script_open(a->view.path, &library->options.scripts, &library->options.preprocessor, &s,
                        e))
        return false;
    bool ok = false;
    switch (a->view.kind) {
    case QA_BOT_CHAT_SYNONYMS:
        ok = chat_parse_synonyms(a, s, e);
        break;
    case QA_BOT_CHAT_RANDOMS:
        ok = chat_parse_randoms(a, s, e);
        break;
    case QA_BOT_CHAT_MATCHES:
        ok = chat_parse_matches(a, s, e);
        break;
    case QA_BOT_CHAT_REPLIES:
        ok = chat_parse_replies(library, a, s, e);
        break;
    case QA_BOT_CHAT_INITIAL:
        ok = chat_parse_initial(a, s, e);
        break;
    }
    qa_script_close(s);
    return ok && chat_asset_finish(a, e);
}
bool qa_bot_chat_asset_load(qa_bot_library *library, qa_bot_chat_asset_kind kind, const char *path,
                            const char *name, qa_bot_chat_asset **out, qa_error *e) {
    bool cached;
    return chat_asset_load(library, kind, path, name, out, &cached, e);
}
bool chat_asset_load(qa_bot_library *library, qa_bot_chat_asset_kind kind, const char *path,
                     const char *name, qa_bot_chat_asset **out, bool *cached, qa_error *e) {
    if (library == NULL || path == NULL || out == NULL || kind < QA_BOT_CHAT_SYNONYMS ||
        kind > QA_BOT_CHAT_INITIAL || (kind == QA_BOT_CHAT_INITIAL && name == NULL)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot chat asset request");
        return false;
    }
    if (name == NULL)
        name = "";
    *cached = false;
    if (!bot_reload_characters(library))
        for (qa_bot_chat_asset *a = library->chat_assets; a != NULL; a = a->next)
            if (a->view.kind == kind && strcmp(a->view.path, path) == 0 &&
                strcmp(a->view.name, name) == 0) {
                qa_bot_chat_asset_retain(a);
                *out = a;
                *cached = true;
                return true;
            }
    qa_bot_chat_asset *a;
    if (!chat_asset_allocate(kind, path, name, &a, e))
        return false;
    if (!chat_asset_parse(library, a, e)) {
        qa_bot_chat_asset_release(a);
        return false;
    }
    if (!bot_reload_characters(library)) {
        a->next = library->chat_assets;
        library->chat_assets = a;
        qa_bot_chat_asset_retain(a);
    }
    *out = a;
    return true;
}
const float *qa_bot_chat_cooldowns(const qa_bot_chat_asset *a, size_t *count) {
    if (count != NULL)
        *count = a == NULL || a->cooldowns == NULL ? 0 : a->view.message_count;
    return a == NULL ? NULL : a->cooldowns;
}
bool qa_bot_chat_cooldowns_restore(qa_bot_chat_asset *a, const float *times, size_t count,
                                   qa_error *e) {
    size_t expected;
    qa_bot_chat_cooldowns(a, &expected);
    if (a == NULL || count != expected || (count != 0 && times == NULL))
        goto invalid;
    for (size_t i = 0; i < count; ++i)
        if (!isfinite(times[i]))
            goto invalid;
    if (count != 0)
        memcpy(a->cooldowns, times, count * sizeof(*times));
    return true;
invalid:
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid shared bot chat cooldown checkpoint");
    return false;
}
