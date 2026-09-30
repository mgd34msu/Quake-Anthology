#include "internal.h"
#include "../library/internal.h"
#include "../goals/internal.h"
#include "../chat/internal.h"
#include "../save_fields.h"
#include "qa/bot_runtime_assets_save.h"

typedef struct saved_asset {
    qa_bot_saved_asset_kind kind;
    void *object;
    bool cached;
} saved_asset;
struct qa_bot_saved_assets { saved_asset *assets; size_t count, capacity; };
static const uint8_t magic[8] = {'Q', 'A', 'B', 'A', 'S', 'E', 'T', 0};

static bool fail(qa_error *error, const char *message)
{ qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message); return false; }
static void retain(saved_asset asset)
{
    switch (asset.kind) {
    case QA_BOT_SAVED_WEIGHTS: qa_bot_weights_retain(asset.object); break;
    case QA_BOT_SAVED_CHARACTER: qa_bot_character_retain(asset.object); break;
    case QA_BOT_SAVED_WEAPONS: qa_bot_weapons_retain(asset.object); break;
    case QA_BOT_SAVED_ITEMS: qa_bot_items_retain(asset.object); break;
    case QA_BOT_SAVED_CHAT: qa_bot_chat_asset_retain(asset.object); break;
    }
}
void qa_bot_saved_assets_free(qa_bot_saved_assets *set)
{
    if (!set) return;
    for (size_t i = 0; i < set->count; ++i) switch (set->assets[i].kind) {
    case QA_BOT_SAVED_WEIGHTS: qa_bot_weights_release(set->assets[i].object); break;
    case QA_BOT_SAVED_CHARACTER: qa_bot_character_release(set->assets[i].object); break;
    case QA_BOT_SAVED_WEAPONS: qa_bot_weapons_release(set->assets[i].object); break;
    case QA_BOT_SAVED_ITEMS: qa_bot_items_release(set->assets[i].object); break;
    case QA_BOT_SAVED_CHAT: qa_bot_chat_asset_release(set->assets[i].object); break;
    }
    free(set->assets); free(set);
}
bool qa_bot_saved_asset_id(const qa_bot_saved_assets *set, qa_bot_saved_asset_kind kind,
                           const void *object, uint64_t *out, qa_error *error)
{
    if (!set || !object || !out) return fail(error, "Missing actual bot asset reference");
    for (size_t i = 0; i < set->count; ++i)
        if (set->assets[i].kind == kind && set->assets[i].object == object) { *out = (uint64_t)i; return true; }
    return fail(error, "Bot owner references an asset outside its actual runtime registry");
}
bool qa_bot_saved_asset_resolve(const qa_bot_saved_assets *set, qa_bot_saved_asset_kind kind,
                                uint64_t id, const void **out, qa_error *error)
{
    if (!set || !out || id >= set->count || set->assets[id].kind != kind || !set->assets[id].object)
        return fail(error, "Saved bot asset identity has another kind or owner");
    *out = set->assets[id].object; return true;
}
static bool add(qa_bot_saved_assets *set, qa_bot_saved_asset_kind kind, void *object, bool cached, qa_error *error)
{
    if (!object) return true;
    for (size_t i = 0; i < set->count; ++i) if (set->assets[i].object == object) {
        if (set->assets[i].kind != kind || (cached && set->assets[i].cached))
            return fail(error, "Bot library cache has a duplicate or mismatched asset");
        if (cached) set->assets[i].cached = true;
        return true;
    }
    if (set->count == set->capacity) {
        size_t next = set->capacity ? set->capacity * 2 : 32;
        if (next < set->capacity || next > SIZE_MAX / sizeof(*set->assets))
            return fail(error, "Bot asset registry extent overflow");
        saved_asset *assets = realloc(set->assets, next * sizeof(*assets));
        if (!assets) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Indexing actual bot runtime assets"); return false; }
        set->assets = assets; set->capacity = next;
    }
    saved_asset asset = {.kind = kind, .object = object, .cached = cached};
    retain(asset); set->assets[set->count++] = asset; return true;
}
static bool collect(const qa_bot_runtime *runtime, qa_bot_saved_assets *set, qa_error *error)
{
    const qa_bot_library *library = runtime->library;
#define CACHE(field, type, kind) \
    for (type *asset = library->field; asset; asset = asset->next) \
        if (!add(set, kind, asset, true, error)) return false;
    CACHE(weights, qa_bot_weights, QA_BOT_SAVED_WEIGHTS)
    CACHE(characters, qa_bot_character, QA_BOT_SAVED_CHARACTER)
    CACHE(weapon_configs, qa_bot_weapons, QA_BOT_SAVED_WEAPONS)
    CACHE(item_configs, qa_bot_items, QA_BOT_SAVED_ITEMS)
    CACHE(chat_assets, qa_bot_chat_asset, QA_BOT_SAVED_CHAT)
#undef CACHE
    qa_bot_character *last = NULL;
    for (qa_bot_character *asset = library->characters; asset; asset = asset->next) last = asset;
    if (last != library->last_character)
        return fail(error, "Bot character cache tail differs from its actual ordered list");
    if (!add(set, QA_BOT_SAVED_WEAPONS, runtime->weapon_config, false, error)) return false;
    for (uint32_t i = 0; i < runtime->options.maximum_states; ++i) {
        if (!add(set, QA_BOT_SAVED_CHARACTER, runtime->characters[i], false, error) ||
            !add(set, QA_BOT_SAVED_WEIGHTS, runtime->weapons[i].weights, false, error)) return false;
        const qa_bot_weapon_selector *selector = runtime->weapons[i].selector;
        if (selector && (!add(set, QA_BOT_SAVED_WEAPONS, selector->config, false, error) ||
                         !add(set, QA_BOT_SAVED_WEIGHTS, selector->weights, false, error))) return false;
    }
    if (runtime->goals) {
        if (!add(set, QA_BOT_SAVED_ITEMS, runtime->goals->items, false, error)) return false;
        for (const bot_goal_weights *weights = runtime->goals->weights; weights; weights = weights->next)
            if (!add(set, QA_BOT_SAVED_WEIGHTS, weights->weights, false, error)) return false;
    }
    if (runtime->chat_system) {
        const qa_bot_chat_options *options = &runtime->chat_system->options;
        if (!add(set, QA_BOT_SAVED_CHAT, options->synonyms, false, error) ||
            !add(set, QA_BOT_SAVED_CHAT, options->randoms, false, error) ||
            !add(set, QA_BOT_SAVED_CHAT, options->matches, false, error) ||
            !add(set, QA_BOT_SAVED_CHAT, options->replies, false, error)) return false;
        for (const qa_bot_chat *state = runtime->chat_system->states; state; state = state->next)
            if (!add(set, QA_BOT_SAVED_CHAT, state->initial, false, error)) return false;
    }
    return true;
}
static bool same_topology(const qa_bot_weights *a, const qa_bot_weights *b)
{
    const qa_bot_weights_view *x = &a->view, *y = &b->view;
    if (x->weight_count != y->weight_count || x->node_count != y->node_count ||
        x->maximum_inventory_index != y->maximum_inventory_index || strcmp(x->path, y->path)) return false;
    for (size_t i = 0; i < x->weight_count; ++i)
        if (x->weights[i].root != y->weights[i].root || x->weights[i].end != y->weights[i].end ||
            strcmp(x->weights[i].name, y->weights[i].name)) return false;
    for (size_t i = 0; i < x->node_count; ++i) {
        const qa_bot_weight_node *p = &x->nodes[i], *q = &y->nodes[i];
        if (p->inventory != q->inventory || p->threshold != q->threshold || p->child != q->child ||
            p->next != q->next || p->balanced != q->balanced) return false;
    }
    return true;
}
static bool asset_fields(qa_source_save_io *io, qa_bot_saved_assets *set, size_t index)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    saved_asset *asset = &set->assets[index]; uint32_t kind = (uint32_t)asset->kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_BOT_SAVED_CHAT || !qa_source_save_bool(io, &asset->cached))
        return bot_save_fail(io, QA_ERROR_FORMAT, "Unknown runtime bot asset kind");
    asset->kind = (qa_bot_saved_asset_kind)kind;
    bool ok = false;
    switch (asset->kind) {
    case QA_BOT_SAVED_WEIGHTS: {
        size_t group = index;
        if (!reading) for (size_t i = 0; i < index; ++i)
            if (set->assets[i].kind == QA_BOT_SAVED_WEIGHTS &&
                ((qa_bot_weights *)set->assets[i].object)->topology == ((qa_bot_weights *)asset->object)->topology) { group = i; break; }
        qa_bot_weights *weights = reading ? NULL : asset->object;
        ok = qa_source_save_count(io, &group, index) && bot_save_weights_fields(io, weights, reading ? &weights : NULL);
        if (reading) asset->object = weights;
        if (ok && reading && group != index) {
            saved_asset *first = &set->assets[group];
            ok = first->kind == QA_BOT_SAVED_WEIGHTS && same_topology(first->object, weights);
            if (ok) {
                bot_weight_topology *topology = ((qa_bot_weights *)first->object)->topology;
                atomic_fetch_add_explicit(&topology->references, 1, memory_order_relaxed);
                bot_weight_topology_release(weights->topology); weights->topology = topology; bot_weights_view(weights);
            }
        }
        break;
    }
    case QA_BOT_SAVED_CHARACTER: {
        qa_bot_character *object = reading ? NULL : asset->object;
        ok = bot_save_character_fields(io, object, reading ? &object : NULL); if (reading) asset->object = object; break;
    }
    case QA_BOT_SAVED_WEAPONS: {
        qa_bot_weapons *object = reading ? NULL : asset->object;
        ok = bot_save_weapons_fields(io, object, reading ? &object : NULL); if (reading) asset->object = object; break;
    }
    case QA_BOT_SAVED_ITEMS: {
        qa_bot_items *object = reading ? NULL : asset->object;
        ok = bot_save_items_fields(io, object, reading ? &object : NULL); if (reading) asset->object = object; break;
    }
    case QA_BOT_SAVED_CHAT: {
        qa_bot_chat_asset *object = reading ? NULL : asset->object;
        ok = bot_save_chat_asset_fields(io, object, reading ? &object : NULL); if (reading) asset->object = object; break;
    }
    }
    if (!ok && !io->failed) return bot_save_fail(io, QA_ERROR_FORMAT, "Mismatched shared bot weight topology");
    return ok;
}
bool qa_bot_runtime_assets_capture(const qa_bot_runtime *runtime, qa_buffer *out, qa_bot_saved_assets **refs, qa_error *error)
{
    if (!runtime || !runtime->library || !runtime->characters || !runtime->weapons || !out || !refs || *refs ||
        !qa_bot_runtime_can_destroy(runtime)) return fail(error, "Bot asset capture requires actual idle runtime owners");
    qa_bot_saved_assets *set = calloc(1, sizeof(*set));
    if (!set) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating actual bot asset registry"); return false; }
    qa_source_save_io io = {0};
    bool ok = collect(runtime, set, error) && qa_source_save_writer(&io, NULL, error) && bot_save_signature(&io, magic) &&
        qa_source_save_count(&io, &set->count, SIZE_MAX);
    for (size_t i = 0; ok && i < set->count; ++i) ok = asset_fields(&io, set, i);
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!ok) { qa_bot_saved_assets_free(set); return false; }
    *refs = set; return true;
}
bool qa_bot_runtime_assets_restore(qa_bot_runtime *runtime, qa_bytes bytes, qa_bot_saved_assets **out, qa_error *error)
{
    qa_bot_library *library = runtime ? runtime->library : NULL;
    if (!library || !out || *out || !qa_bot_runtime_can_destroy(runtime) || library->weights || library->characters ||
        library->weapon_configs || library->item_configs || library->chat_assets || library->last_character)
        return fail(error, "Bot asset restore requires an empty detached actual library cache");
    qa_bot_saved_assets *set = calloc(1, sizeof(*set));
    if (!set) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring bot asset registry"); return false; }
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && bot_save_signature(&io, magic) &&
        qa_source_save_count(&io, &set->count, bytes.size / 5) && set->count <= SIZE_MAX / sizeof(*set->assets);
    if (ok && set->count && !(set->assets = calloc(set->count, sizeof(*set->assets))))
        ok = bot_save_fail(&io, QA_ERROR_MEMORY, "Restoring bot asset index");
    if (!set->assets) set->count = 0;
    for (size_t i = 0; ok && i < set->count; ++i) ok = asset_fields(&io, set, i);
    if (ok) ok = qa_source_save_finish(&io, NULL);
    if (ok) {
        qa_bot_weights **weights = &library->weights; qa_bot_character **characters = &library->characters;
        qa_bot_weapons **weapons = &library->weapon_configs; qa_bot_items **items = &library->item_configs;
        qa_bot_chat_asset **chats = &library->chat_assets;
        for (size_t i = 0; i < set->count; ++i) {
            saved_asset asset = set->assets[i]; if (!asset.cached) continue;
            retain(asset);
            switch (asset.kind) {
            case QA_BOT_SAVED_WEIGHTS: *weights = asset.object; weights = &(*weights)->next; break;
            case QA_BOT_SAVED_CHARACTER: *characters = asset.object; library->last_character = *characters; characters = &(*characters)->next; break;
            case QA_BOT_SAVED_WEAPONS: *weapons = asset.object; weapons = &(*weapons)->next; break;
            case QA_BOT_SAVED_ITEMS: *items = asset.object; items = &(*items)->next; break;
            case QA_BOT_SAVED_CHAT: *chats = asset.object; chats = &(*chats)->next; break;
            }
        }
        *out = set;
    } else qa_bot_saved_assets_free(set);
    if (!ok && (!error || error->code == QA_OK)) fail(error, "Invalid runtime bot asset registry");
    qa_source_save_dispose(&io); return ok;
}
