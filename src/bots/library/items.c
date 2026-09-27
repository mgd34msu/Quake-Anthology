#include "internal.h"

#define FIELD(field, text, kind)                                                                   \
    {text, offsetof(qa_bot_item_info, field), sizeof(((qa_bot_item_info *)0)->field), kind}
static const bot_field fields[] = {FIELD(name, "name", BOT_FIELD_STRING),
                                   FIELD(model, "model", BOT_FIELD_STRING),
                                   FIELD(model_index, "modelindex", BOT_FIELD_INT),
                                   FIELD(type, "type", BOT_FIELD_INT),
                                   FIELD(inventory, "index", BOT_FIELD_INT),
                                   FIELD(respawn_seconds, "respawntime", BOT_FIELD_FLOAT),
                                   FIELD(mins, "mins", BOT_FIELD_VECTOR),
                                   FIELD(maxs, "maxs", BOT_FIELD_VECTOR)};
#undef FIELD
static bool create(const char *path, size_t capacity, qa_bot_items **out, qa_error *e) {
    if (capacity > INT32_MAX || capacity > SIZE_MAX / sizeof(qa_bot_item_info)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot item capacity");
        return false;
    }
    qa_bot_items *c = calloc(1, sizeof(*c));
    if (c == NULL)
        goto memory;
    atomic_init(&c->references, 1);
    c->view.path = bot_string(&c->arena, (qa_bytes){(const uint8_t *)path, strlen(path)}, e);
    c->items = calloc(capacity == 0 ? 1 : capacity, sizeof(*c->items));
    if (c->view.path == NULL || c->items == NULL) {
        qa_bot_items_release(c);
        goto memory;
    }
    c->view.items = c->items;
    c->view.capacity = capacity;
    *out = c;
    return true;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating bot item configuration");
    return false;
}
void qa_bot_items_retain(qa_bot_items *c) {
    if (c != NULL)
        atomic_fetch_add_explicit(&c->references, 1, memory_order_relaxed);
}
void qa_bot_items_release(qa_bot_items *c) {
    if (c != NULL && atomic_fetch_sub_explicit(&c->references, 1, memory_order_acq_rel) == 1) {
        free(c->items);
        qa_arena_destroy(&c->arena);
        free(c);
    }
}
const qa_bot_items_view *qa_bot_items_read(const qa_bot_items *c) {
    return c == NULL ? NULL : &c->view;
}
bool qa_bot_items_load(qa_bot_library *library, const char *path, size_t capacity,
                       qa_bot_items **out, qa_error *e) {
    if (library == NULL || path == NULL || out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing bot item resource path/output");
        return false;
    }
    for (qa_bot_items *c = library->item_configs; c != NULL; c = c->next)
        if (c->view.capacity == capacity && strcmp(c->view.path, path) == 0) {
            qa_bot_items_retain(c);
            *out = c;
            return true;
        }
    qa_script *s;
    if (!qa_script_open(path, &library->options.scripts, &library->options.preprocessor, &s, e))
        return false;
    qa_bot_items *c;
    if (!create(path, capacity, &c, e)) {
        qa_script_close(s);
        return false;
    }
    for (;;) {
        qa_script_token token;
        bool found;
        if (!qa_script_next(s, &token, &found, e))
            goto fail;
        if (!found)
            break;
        if (!qa_script_token_is(&token, "iteminfo")) {
            bot_fail(s, "Unknown bot item definition", e);
            goto fail;
        }
        if (c->view.count == capacity) {
            bot_fail(s, "Too many bot item definitions", e);
            goto fail;
        }
        qa_bot_item_info *item = c->items + c->view.count;
        if (!bot_read_string(s, item->classname, sizeof(item->classname), e) ||
            !bot_structure(s, item, fields, sizeof(fields) / sizeof(*fields), e))
            goto fail;
        item->number = (int32_t)c->view.count;
        ++c->view.count;
    }
    qa_script_close(s);
    c->next = library->item_configs;
    library->item_configs = c;
    qa_bot_items_retain(c);
    *out = c;
    return true;
fail:
    qa_script_close(s);
    qa_bot_items_release(c);
    return false;
}
bool qa_bot_items_restore(const qa_bot_items_view *view, qa_bot_items **out, qa_error *e) {
    if (view == NULL || out == NULL || view->path == NULL || view->capacity > INT32_MAX ||
        view->capacity > SIZE_MAX / sizeof(qa_bot_item_info) || view->count > view->capacity ||
        (view->count != 0 && view->items == NULL))
        goto invalid;
    for (size_t i = 0; i < view->count; ++i) {
        const qa_bot_item_info *item = view->items + i;
        if (memchr(item->classname, 0, sizeof(item->classname)) == NULL ||
            memchr(item->name, 0, sizeof(item->name)) == NULL ||
            memchr(item->model, 0, sizeof(item->model)) == NULL ||
            !isfinite(item->respawn_seconds) || !qa_vec_finite(item->mins) ||
            !qa_vec_finite(item->maxs) || item->number != (int64_t)i)
            goto invalid;
    }
    qa_bot_items *c;
    if (!create(view->path, view->capacity, &c, e))
        return false;
    if (view->count != 0)
        memcpy(c->items, view->items, view->count * sizeof(*c->items));
    c->view.count = view->count;
    *out = c;
    return true;
invalid:
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid bot item checkpoint");
    return false;
}
