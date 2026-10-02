#include "internal.h"
#include "items_source.h"
#include "character_reader.h"

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
void qa_bot_items_retain(qa_bot_items *c) {
    if (c != NULL)
        atomic_fetch_add_explicit(&c->references, 1, memory_order_relaxed);
}
void qa_bot_items_release(qa_bot_items *c) {
    if (c != NULL && atomic_fetch_sub_explicit(&c->references, 1, memory_order_acq_rel) == 1) {
        qa_script_close(c->reader);
        free(c->script_host);
        (void)qa_bot_memory_release(c->memory, NULL);
        free(c->items);
        free(c->members);
        qa_arena_destroy(&c->arena);
        free(c);
    }
}
const qa_bot_items_view *qa_bot_items_read(const qa_bot_items *c) {
    return c != NULL && bot_items_project((qa_bot_items *)c, NULL) ? &c->view : NULL;
}
bool qa_bot_items_view_read(const qa_bot_items *c, const qa_bot_items_view **out, qa_error *error) {
    if (!out || !c) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing item view owner/output"); return false; }
    *out = NULL;
    if (!bot_items_project((qa_bot_items *)c, error)) return false;
    *out = &c->view; return true;
}
typedef struct item_write { qa_bot_items *config; size_t index; qa_bot_item_info *value; } item_write;
static bool written(void *context, qa_error *error) {
    item_write *write = context;
    return bot_items_store(write->config, write->index, write->value, error);
}
static bool load_source(qa_bot_library *library, const char *path, size_t capacity,
                       qa_bot_items **out, qa_error *e) {
    if (library == NULL || path == NULL || out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing bot item resource path/output");
        return false;
    }
    qa_script *s = NULL; bot_character_reader *host = NULL;
    if (!bot_character_reader_create(&library->options.scripts, &host, e)) return false;
    qa_script_services scripts = bot_character_reader_services(host);
    if (!qa_script_open(path, &scripts, &library->options.preprocessor, &s, e)) { free(host); return false; }
    const char *resolved = qa_script_position(s).path;
    qa_bot_items *c = NULL;
    if (!resolved || !*resolved || !bot_items_create(library->memory, resolved, capacity, &c, e)) {
        qa_script_close(s);
        free(host);
        return false;
    }
    c->script_host = host;
    c->next = library->item_configs; library->item_configs = c;
    qa_bot_items_retain(c); c->reader = s; c->active = true;
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
        uint32_t count;
        if (!bot_items_header_count(c, &count, e)) goto fail;
        if (count == capacity) {
            bot_fail(s, "Too many bot item definitions", e);
            goto fail;
        }
        qa_bot_item_info value = {0}, *item = &value;
        qa_bot_memory_span cell;
        if (!bot_items_cell(c, count, &cell, e)) goto fail;
        memset(cell.data, 0, cell.size); *item = (qa_bot_item_info){0};
        item_write write = {c, count, item};
        if (!bot_read_string(s, item->classname, sizeof(item->classname), e) ||
            !written(&write, e) ||
            !bot_structure_source(s, item, fields, sizeof(fields) / sizeof(*fields), &write, written, e))
            goto fail;
        item->number = (int32_t)count;
        if (!written(&write, e) || !bot_items_member(c, count, e) || !bot_items_count(c, count + 1, e)) goto fail;
    }
    qa_script_close(s); c->reader = NULL; c->active = false; c->ready = true;
    if (!bot_items_project(c, e)) { qa_bot_items_release(c); return false; }
    *out = c;
    return true;
fail:
    c->active = false;
    if (!host->callback_failed && e && (e->code == QA_ERROR_FORMAT || e->code == QA_ERROR_NOT_FOUND)) {
        qa_script_close(s); c->reader = NULL;
        (void)qa_bot_items_free(c, NULL);
    }
    qa_bot_items_release(c);
    return false;
}
bool qa_bot_items_load(qa_bot_library *library, const char *path, size_t capacity,
                       qa_bot_items **out, qa_error *e) {
    if (!library || library->item_loading || qa_bot_memory_disposed(library->memory)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Item source owner is absent, disposed or loading"); return false;
    }
    library->item_loading = true;
    bool okay = load_source(library, path, capacity, out, e);
    library->item_loading = false; return okay;
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
    qa_bot_memory *memory = NULL; qa_bot_items *c = NULL;
    if (!qa_bot_memory_create(NULL, &memory, e)) return false;
    bool okay = bot_items_create(memory, view->path, view->capacity, &c, e);
    (void)qa_bot_memory_release(memory, NULL);
    if (!okay) return false;
    for (size_t i = 0; i < view->capacity; ++i)
        if (view->items && !bot_items_store(c, i, view->items + i, e)) { qa_bot_items_release(c); return false; }
    if (!bot_items_count(c, (uint32_t)view->count, e) || !bot_items_members_restore(c, e) || !bot_items_project(c, e)) {
        qa_bot_items_release(c); return false;
    }
    c->ready = true;
    *out = c;
    return true;
invalid:
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid bot item checkpoint");
    return false;
}
