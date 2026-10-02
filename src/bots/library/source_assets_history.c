#include "source_assets_history.h"
#include "character_source.h"
#include "character_reader.h"
#include "items_source.h"
#include "../runtime/internal.h"
#include "../goals/internal.h"
#include "../memory/internal.h"

typedef struct store_image {
    bot_character_store *owner;
    uint64_t next_pointer;
    bot_character_string *strings;
    size_t count;
    qa_bot_memory_checkpoint *external;
    qa_bot_memory_prepared *prepared;
    struct store_image *next;
} store_image;
typedef struct character_image {
    qa_bot_character *owner;
    qa_bot_memory_allocation allocation;
    bool ready, retired, registered;
    qa_script *reader;
    bot_character_reader *host;
    struct character_image *next;
} character_image;
typedef struct item_image {
    qa_bot_items *owner;
    qa_bot_memory_allocation allocation;
    bool ready, registered, reader_retired;
    uint32_t *members;
    size_t count;
    qa_script *reader;
    bot_character_reader *host;
    qa_bot_memory_checkpoint *external;
    qa_bot_memory_prepared *prepared;
    struct item_image *next;
} item_image;
struct bot_source_assets_history {
    qa_bot_runtime *runtime;
    bot_character_store *library_store;
    store_image *stores;
    character_image *characters, *last_character;
    item_image *items, *last_item;
    qa_bot_character **handles;
    uint32_t count;
};
struct bot_source_assets_restore {
    bot_source_assets_history state;
    qa_bot_character **old_characters;
    size_t old_character_count;
    qa_bot_items **old_items;
    size_t old_item_count;
};
static bool fail(qa_error *error, const char *text) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", text); return false;
}
static void clear(bot_source_assets_history *image) {
    while (image->characters) {
        character_image *row = image->characters; image->characters = row->next;
        qa_script_close(row->reader); free(row->host); qa_bot_character_release(row->owner); free(row);
    }
    while (image->items) {
        item_image *row = image->items; image->items = row->next;
        qa_script_close(row->reader); free(row->host); free(row->members);
        qa_bot_memory_checkpoint_finish(row->prepared, false);
        qa_bot_memory_checkpoint_destroy(row->external); qa_bot_items_release(row->owner); free(row);
    }
    while (image->stores) {
        store_image *row = image->stores; image->stores = row->next;
        qa_bot_memory_checkpoint_finish(row->prepared, false);
        qa_bot_memory_checkpoint_destroy(row->external); bot_character_store_release(row->owner);
        free(row->strings); free(row);
    }
    free(image->handles);
}
void bot_source_assets_history_destroy(bot_source_assets_history *image) {
    if (image) { clear(image); free(image); }
}
static store_image *find_store(bot_source_assets_history *image, bot_character_store *store) {
    for (store_image *row = image->stores; row; row = row->next) if (row->owner == store) return row;
    return NULL;
}
static bool store_capture(bot_source_assets_history *image, bot_character_store *store, qa_error *error) {
    if (!store || find_store(image, store)) return true;
    store_image *row = calloc(1, sizeof(*row));
    if (!row) goto memory;
    row->owner = store; ++store->references; row->next = image->stores; image->stores = row;
    row->count = store->count; row->next_pointer = store->next_pointer;
    if (row->count > SIZE_MAX / sizeof(*row->strings)) return fail(error, "Character string history exceeds storage");
    row->strings = malloc((row->count ? row->count : 1) * sizeof(*row->strings));
    if (!row->strings) goto memory;
    if (row->count) memcpy(row->strings, store->strings, row->count * sizeof(*row->strings));
    for (size_t i = 0; i < row->count; ++i) if (row->strings[i].pointer) {
        qa_bot_memory_span span;
        if (!qa_bot_memory_bytes(store->memory, row->strings[i].allocation, &span, error) ||
            !memchr(span.data, 0, span.size)) return fail(error, "Character history string is not a live terminated allocation");
    }
    return store->memory == image->runtime->memory ||
        qa_bot_memory_checkpoint_capture(store->memory, &row->external, error);
memory:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Capturing character string allocation history"); return false;
}
static bool character_capture(bot_source_assets_history *image, qa_bot_character *c,
                               bool registered, qa_error *error) {
    if (!c) return true;
    for (character_image *row = image->characters; row; row = row->next)
        if (row->owner == c) return !registered || fail(error, "Character history repeats a physical library record");
    if (c->active || !c->source || !store_capture(image, c->source, error)) return false;
    if (!c->retired) {
        qa_bot_memory_span span;
        bot_memory_record *record = bot_memory_record_get(c->source->memory, c->allocation);
        if (!record || record->kind != QA_BOT_MEMORY_HEAP ||
            !qa_bot_memory_bytes(c->source->memory, c->allocation, &span, error) || span.size != BOT_CHARACTER_BYTES)
            return fail(error, "Character history profile is not its actual allocation");
        if (c->ready && !bot_character_project(c, error)) return false;
    }
    character_image *row = calloc(1, sizeof(*row));
    if (!row) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Capturing character profile history"); return false; }
    row->owner = c; qa_bot_character_retain(c); row->registered = registered;
    row->allocation = c->allocation; row->ready = c->ready; row->retired = c->retired;
    if (image->last_character) image->last_character->next = row; else image->characters = row;
    image->last_character = row;
    return bot_character_reader_copy(c->reader, c->script_host, &row->reader, &row->host, error);
}
static bool item_capture(bot_source_assets_history *image, qa_bot_items *c, bool registered, qa_error *error) {
    if (!c) return true;
    for (item_image *row = image->items; row; row = row->next)
        if (row->owner == c) return !registered || fail(error, "Item history repeats a physical library record");
    uint32_t count;
    if (c->active || !bot_items_header_count(c, &count, error)) return false;
    item_image *row = calloc(1, sizeof(*row));
    if (!row) goto memory;
    row->owner = c; qa_bot_items_retain(c); row->allocation = c->allocation;
    row->registered = registered; row->ready = c->ready; row->reader_retired = c->reader_retired; row->count = count;
    if (image->last_item) image->last_item->next = row; else image->items = row;
    image->last_item = row;
    row->members = malloc((count ? (size_t)count : 1) * sizeof(*row->members));
    if (!row->members) goto memory;
    for (uint32_t i = 0; i < count; ++i) row->members[i] = i;
    return bot_character_reader_copy(c->reader, c->script_host, &row->reader, &row->host, error) &&
        (c->memory == image->runtime->memory || qa_bot_memory_checkpoint_capture(c->memory, &row->external, error));
memory:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Capturing item configuration history"); return false;
}
bool bot_source_assets_capture(qa_bot_runtime *runtime, bot_source_assets_history **out, qa_error *error) {
    if (!runtime || !runtime->library || !out || *out || !qa_bot_library_idle(runtime->library))
        return fail(error, "Bot asset history requires the actual returned library");
    bot_source_assets_history *image = calloc(1, sizeof(*image));
    if (!image) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Capturing raw bot asset history"); return false; }
    image->runtime = runtime; image->count = runtime->options.maximum_states;
    image->library_store = runtime->library->character_store;
    image->handles = calloc(image->count, sizeof(*image->handles));
    bool okay = image->handles && store_capture(image, image->library_store, error);
    if (!image->handles) qa_error_set(error, QA_ERROR_MEMORY, 0, "Capturing physical character handles");
    for (qa_bot_character *c = runtime->library->characters; okay && c; c = c->next)
        okay = character_capture(image, c, true, error);
    for (uint32_t i = 0; okay && i < image->count; ++i) {
        image->handles[i] = runtime->characters[i];
        okay = character_capture(image, runtime->characters[i], false, error);
    }
    for (qa_bot_items *c = runtime->library->item_configs; okay && c; c = c->next)
        okay = item_capture(image, c, true, error);
    if (okay && runtime->goals) okay = item_capture(image, runtime->goals->items, false, error);
    if (!okay) { bot_source_assets_history_destroy(image); return false; }
    *out = image; return true;
}
bool bot_source_assets_prepare(qa_bot_runtime *runtime, const bot_source_assets_history *image,
                               const qa_bot_memory_prepared *memory, bot_source_assets_restore **out,
                               qa_error *error) {
    if (!runtime || !image || image->runtime != runtime || runtime->options.maximum_states != image->count ||
        !memory || !out || *out) return fail(error, "Raw bot asset restore differs from its physical owner");
    bot_source_assets_restore *plan = calloc(1, sizeof(*plan));
    if (!plan) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Preparing raw bot asset aliases"); return false; }
    bot_source_assets_history *state = &plan->state;
    state->runtime = runtime; state->count = image->count; state->library_store = image->library_store;
    state->handles = malloc((size_t)state->count * sizeof(*state->handles));
    if (!state->handles) goto memory;
    memcpy(state->handles, image->handles, (size_t)state->count * sizeof(*state->handles));
    for (qa_bot_character *c = runtime->library->characters; c; c = c->next) ++plan->old_character_count;
    for (qa_bot_items *c = runtime->library->item_configs; c; c = c->next) ++plan->old_item_count;
    plan->old_characters = calloc(plan->old_character_count ? plan->old_character_count : 1, sizeof(*plan->old_characters));
    plan->old_items = calloc(plan->old_item_count ? plan->old_item_count : 1, sizeof(*plan->old_items));
    if (!plan->old_characters || !plan->old_items) goto memory;
    size_t index = 0;
    for (qa_bot_character *c = runtime->library->characters; c; c = c->next) plan->old_characters[index++] = c;
    index = 0;
    for (qa_bot_items *c = runtime->library->item_configs; c; c = c->next) plan->old_items[index++] = c;
    for (const store_image *source = image->stores; source; source = source->next) {
        store_image *row = calloc(1, sizeof(*row)); if (!row) goto memory;
        row->owner = source->owner; ++row->owner->references;
        row->next = state->stores; state->stores = row;
        row->count = source->count; row->next_pointer = source->next_pointer;
        row->strings = malloc((row->count ? row->count : 1) * sizeof(*row->strings));
        if (!row->strings) goto memory;
        if (row->count) memcpy(row->strings, source->strings, row->count * sizeof(*row->strings));
        if (source->external && !qa_bot_memory_checkpoint_prepare(row->owner->memory, source->external, &row->prepared, error)) goto failed;
        for (size_t i = 0; i < row->count; ++i) if (row->strings[i].pointer &&
            !qa_bot_memory_checkpoint_resolve(row->prepared ? row->prepared : memory,
                row->strings[i].allocation, &row->strings[i].allocation, error)) goto failed;
    }
    for (const character_image *source = image->characters; source; source = source->next) {
        character_image *row = calloc(1, sizeof(*row)); if (!row) goto memory;
        *row = *source; row->next = NULL; row->reader = NULL; row->host = NULL; qa_bot_character_retain(row->owner);
        if (state->last_character) state->last_character->next = row; else state->characters = row;
        state->last_character = row;
        store_image *store = find_store(state, row->owner->source);
        if (!store || (!row->retired && !qa_bot_memory_checkpoint_resolve(store->prepared ? store->prepared : memory,
            row->allocation, &row->allocation, error)) ||
            !bot_character_reader_copy(source->reader, source->host, &row->reader, &row->host, error)) goto failed;
    }
    for (const item_image *source = image->items; source; source = source->next) {
        item_image *row = calloc(1, sizeof(*row)); if (!row) goto memory;
        *row = *source; row->next = NULL; row->members = NULL; row->reader = NULL; row->host = NULL;
        row->external = NULL; row->prepared = NULL; qa_bot_items_retain(row->owner);
        if (state->last_item) state->last_item->next = row; else state->items = row;
        state->last_item = row;
        row->members = malloc((row->count ? row->count : 1) * sizeof(*row->members)); if (!row->members) goto memory;
        if (row->count) memcpy(row->members, source->members, row->count * sizeof(*row->members));
        if (source->external && !qa_bot_memory_checkpoint_prepare(row->owner->memory, source->external, &row->prepared, error)) goto failed;
        if (!qa_bot_memory_checkpoint_resolve(row->prepared ? row->prepared : memory,
            row->allocation, &row->allocation, error) ||
            !bot_character_reader_copy(source->reader, source->host, &row->reader, &row->host, error)) goto failed;
    }
    *out = plan; return true;
memory:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Preparing retained bot resource metadata");
failed:
    bot_source_assets_finish(plan, false); return false;
}
void bot_source_assets_finish(bot_source_assets_restore *plan, bool commit) {
    if (!plan) return;
    bot_source_assets_history *state = &plan->state;
    qa_bot_runtime *runtime = state->runtime;
    if (commit) {
        for (store_image *row = state->stores; row; row = row->next) {
            qa_bot_memory_checkpoint_finish(row->prepared, true); row->prepared = NULL;
            free(row->owner->strings); row->owner->strings = row->strings; row->strings = NULL;
            row->owner->count = row->owner->capacity = row->count; row->owner->next_pointer = row->next_pointer;
        }
        bot_character_store *old_store = runtime->library->character_store;
        runtime->library->character_store = state->library_store;
        if (state->library_store) ++state->library_store->references;
        bot_character_store_release(old_store);
        qa_bot_character **tail = &runtime->library->characters;
        runtime->library->last_character = NULL;
        for (character_image *row = state->characters; row; row = row->next) {
            qa_bot_character *c = row->owner;
            qa_script_close(c->reader); free(c->script_host);
            c->reader = row->reader; row->reader = NULL; c->script_host = row->host; row->host = NULL;
            c->allocation = row->allocation; c->ready = row->ready; c->retired = row->retired;
            c->view = (qa_bot_character_view){0};
            if (row->registered) { qa_bot_character_retain(c); *tail = c; tail = &c->next; runtime->library->last_character = c; }
        }
        *tail = NULL;
        for (size_t i = 0; i < plan->old_character_count; ++i) qa_bot_character_release(plan->old_characters[i]);
        for (uint32_t i = 0; i < state->count; ++i) {
            qa_bot_character_retain(state->handles[i]); qa_bot_character_release(runtime->characters[i]);
            runtime->characters[i] = state->handles[i];
        }
        qa_bot_items **items = &runtime->library->item_configs;
        for (item_image *row = state->items; row; row = row->next) {
            qa_bot_items *c = row->owner;
            qa_bot_memory_checkpoint_finish(row->prepared, true); row->prepared = NULL;
            qa_script_close(c->reader); free(c->script_host);
            c->reader = row->reader; row->reader = NULL; c->script_host = row->host; row->host = NULL;
            c->allocation = row->allocation; c->ready = row->ready; c->reader_retired = row->reader_retired;
            free(c->members); c->members = row->members; row->members = NULL;
            c->member_count = c->member_capacity = row->count; c->view.count = row->count;
            if (row->registered) { qa_bot_items_retain(c); *items = c; items = &c->next; }
        }
        *items = NULL;
        for (size_t i = 0; i < plan->old_item_count; ++i) qa_bot_items_release(plan->old_items[i]);
    }
    clear(state); free(plan->old_characters); free(plan->old_items); free(plan);
}
