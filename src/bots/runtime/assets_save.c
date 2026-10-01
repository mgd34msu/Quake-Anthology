#include "internal.h"
#include "../library/internal.h"
#include "../goals/internal.h"
#include "../chat/internal.h"
#include "../save_fields.h"
#include "qa/bot_runtime_assets_save.h"
#include "../library/source_fuzzy_save.h"
#include "../library/source_fuzzy_view.h"
#include "../library/source_fuzzy_standalone_save.h"

typedef struct saved_asset {
    qa_bot_saved_asset_kind kind;
    void *object;
    bool cached;
} saved_asset;
struct qa_bot_saved_assets {
    saved_asset *assets;size_t count,capacity;
    bot_fuzzy_store *source;
    bool owns_source;
};
static const uint8_t magic[8] = {'Q', 'A', 'B', 'A', 'R', 'A', 'W', 0};

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
    if(set->owns_source) bot_fuzzy_store_dispose(set->source);
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
    CACHE(characters, qa_bot_character, QA_BOT_SAVED_CHARACTER)
    CACHE(weapon_configs, qa_bot_weapons, QA_BOT_SAVED_WEAPONS)
    CACHE(item_configs, qa_bot_items, QA_BOT_SAVED_ITEMS)
    CACHE(chat_assets, qa_bot_chat_asset, QA_BOT_SAVED_CHAT)
#undef CACHE
    /* This is a native binding list, not the source cache. The real cache and
     * its holes are encoded once by the source store below. */
    for(qa_bot_weights *asset=library->weights;asset;asset=asset->next)
        if(asset->source && asset->source->store==library->fuzzy_store && asset->source->owned &&
           !asset->source->disposed && !add(set,QA_BOT_SAVED_WEIGHTS,asset,false,error)) return false;
    qa_bot_character *last = NULL;
    for (qa_bot_character *asset = library->characters; asset; asset = asset->next) last = asset;
    if (last != library->last_character)
        return fail(error, "Bot character cache tail differs from its actual ordered list");
    if (!add(set, QA_BOT_SAVED_WEAPONS, runtime->weapon_config, false, error)) return false;
    for(const bot_weapon_config_identity *identity=runtime->weapon_pointers.configs;identity;identity=identity->next)
        if(!identity->config->source->disposed &&
           !add(set,QA_BOT_SAVED_WEIGHTS,identity->config,false,error)) return false;
    for(const bot_weapon_pointer *pointer=runtime->weapon_pointers.first;pointer;pointer=pointer->next)
        if(pointer->kind==BOT_WEAPON_POINTER_CONFIG && !pointer->config->source->disposed &&
           !add(set,QA_BOT_SAVED_WEIGHTS,pointer->config,false,error)) return false;
    for (uint32_t i = 0; i < runtime->options.maximum_states; ++i) {
        if (!add(set, QA_BOT_SAVED_CHARACTER, runtime->characters[i], false, error)) return false;
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
        qa_bot_weights *weights = reading ? NULL : asset->object;
        bool local=!reading && weights && weights->source && set->source && weights->source->store==set->source;
        ok=qa_source_save_bool(io,&local) && !asset->cached;
        if(ok && local) {
            size_t reference=0;bot_fuzzy_owned *source=NULL;
            ok=set->source!=NULL;
            if(ok && !reading) ok=bot_fuzzy_store_reference(set->source,weights->source,&reference,io->error);
            if(ok) ok=qa_source_save_count(io,&reference,SIZE_MAX);
            if(ok && reading) {
                ok=bot_fuzzy_store_resolve(set->source,reference,&source,io->error);
                if(ok) {
                    weights=calloc(1,sizeof(*weights));
                    if(!weights) ok=bot_save_fail(io,QA_ERROR_MEMORY,"Restoring actual source weight binding");
                    else {
                        atomic_init(&weights->references,1);weights->source=source;bot_fuzzy_owned_retain(source);
                        ok=bot_weights_source_view(weights,io->error);
                    }
                }
            }
        } else if(ok) {
            size_t group=index;
            if(!reading) for(size_t prior=0;prior<index;++prior) {
                if(set->assets[prior].kind!=QA_BOT_SAVED_WEIGHTS) continue;
                qa_bot_weights *first=set->assets[prior].object;
                if(first->source->source.heap==weights->source->source.heap) {group=prior;break;}
            }
            ok=qa_source_save_count(io,&group,index);
            if(ok && group==index) ok=bot_save_weights_fields(io,weights,reading?&weights:NULL);
            else if(ok) {
                saved_asset *first=&set->assets[group];
                ok=first->kind==QA_BOT_SAVED_WEIGHTS &&
                    bot_weights_source_alias_fields(io,first->object,weights,reading?&weights:NULL);
            }
        }
        if(reading) {
            asset->object=weights;
            if(ok && local) for(size_t prior=0;prior<index;++prior) {
                if(set->assets[prior].kind==QA_BOT_SAVED_WEIGHTS &&
                   ((qa_bot_weights *)set->assets[prior].object)->source==weights->source) {
                    ok=bot_save_fail(io,QA_ERROR_FORMAT,"Duplicate actual source weight binding");break;
                }
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
static bool source_fields(qa_source_save_io *io,qa_bot_saved_assets *set,qa_bot_library *library)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ,present=!reading && set->source;
    if(!qa_source_save_bool(io,&present)) return false;
    if(!present) return true;
    qa_buffer bytes={0};size_t extent=0;
    bool ok=reading || bot_fuzzy_store_capture(set->source,&bytes,io->error);
    if(!reading) extent=bytes.size;
    if(ok) ok=qa_source_save_count(io,&extent,SIZE_MAX);
    if(ok && reading) {
        if(!library || io->offset>io->input.size || extent>io->input.size-io->offset)
            ok=bot_save_fail(io,QA_ERROR_FORMAT,"Runtime fuzzy aliases require actual imported library MEMORY");
        else {
            ok=bot_fuzzy_store_restore(library,(qa_bytes){io->input.data+io->offset,extent},&set->source,io->error);
            if(ok) {set->owns_source=true;io->offset+=extent;}
        }
    } else if(ok) ok=qa_source_save_bytes(io,bytes.data,extent);
    qa_buffer_free(&bytes);if(!ok) io->failed=true;return ok;
}
static bool encode(qa_bot_saved_assets *set, qa_buffer *out, qa_error *error)
{
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && bot_save_signature(&io, magic) && source_fields(&io,set,NULL) &&
        qa_source_save_count(&io, &set->count, SIZE_MAX);
    for (size_t i = 0; ok && i < set->count; ++i) ok = asset_fields(&io, set, i);
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_bot_goals_assets_capture(const qa_bot_goals *goals, qa_buffer *out, qa_bot_saved_assets **refs, qa_error *error)
{
    if (!goals || goals->busy || !out || !refs || *refs)
        return fail(error, "Goal asset capture requires an actual idle owner");
    qa_bot_saved_assets *set = calloc(1, sizeof(*set));
    if (!set) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating goal asset registry"); return false; }
    bool ok = add(set, QA_BOT_SAVED_ITEMS, goals->items, false, error);
    for (const bot_goal_weights *weights = goals->weights; ok && weights; weights = weights->next)
        ok = add(set, QA_BOT_SAVED_WEIGHTS, weights->weights, false, error);
    if (ok) ok = encode(set, out, error);
    if (!ok) { qa_bot_saved_assets_free(set); return false; }
    *refs = set; return true;
}
bool qa_bot_runtime_assets_capture(const qa_bot_runtime *runtime, qa_buffer *out, qa_bot_saved_assets **refs, qa_error *error)
{
    if (!runtime || !runtime->library || !runtime->characters || !runtime->weapons || !out || !refs || *refs ||
        !qa_bot_runtime_can_destroy(runtime)) return fail(error, "Bot asset capture requires actual idle runtime owners");
    qa_bot_saved_assets *set = calloc(1, sizeof(*set));
    if (!set) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating actual bot asset registry"); return false; }
    set->source=runtime->library->fuzzy_store;
    bool ok = collect(runtime, set, error) && encode(set, out, error);
    if (!ok) { qa_bot_saved_assets_free(set); return false; }
    *refs = set; return true;
}
static bool decode(qa_bytes bytes,qa_bot_library *library,qa_bot_saved_assets **out,qa_error *error)
{
    if (!out || *out) return fail(error, "Bot asset decode requires an empty registry output");
    qa_bot_saved_assets *set = calloc(1, sizeof(*set));
    if (!set) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring bot asset registry"); return false; }
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && bot_save_signature(&io, magic) && source_fields(&io,set,library) &&
        qa_source_save_count(&io, &set->count, bytes.size / 5) && set->count <= SIZE_MAX / sizeof(*set->assets);
    if (ok && set->count && !(set->assets = calloc(set->count, sizeof(*set->assets))))
        ok = bot_save_fail(&io, QA_ERROR_MEMORY, "Restoring bot asset index");
    if (!set->assets) set->count = 0;
    for (size_t i = 0; ok && i < set->count; ++i) ok = asset_fields(&io, set, i);
    if (ok) ok = qa_source_save_finish(&io, NULL);
    if (ok) *out = set;
    else qa_bot_saved_assets_free(set);
    if (!ok && (!error || error->code == QA_OK)) fail(error, "Invalid runtime bot asset registry");
    qa_source_save_dispose(&io); return ok;
}
bool qa_bot_saved_assets_decode(qa_bytes bytes,qa_bot_saved_assets **out,qa_error *error)
{
    return decode(bytes,NULL,out,error);
}
bool qa_bot_runtime_assets_restore(qa_bot_runtime *runtime, qa_bytes bytes, qa_bot_saved_assets **out, qa_error *error)
{
    qa_bot_library *library = runtime ? runtime->library : NULL;
    if (!library || !out || *out || !qa_bot_runtime_can_destroy(runtime) || library->weights || library->characters ||
        library->weapon_configs || library->item_configs || library->chat_assets || library->last_character)
        return fail(error, "Bot asset restore requires an empty detached actual library cache");
    qa_bot_saved_assets *set = NULL;
    if(!library->fuzzy_store || library->fuzzy_store->first || library->fuzzy_store->readers ||
       library->fuzzy_store->cached_count || library->fuzzy_store->heap.first)
        return fail(error,"Runtime fuzzy restore requires its actual empty source store");
    if (!decode(bytes,library,&set,error)) return false;
    if(!set->source) {qa_bot_saved_assets_free(set);return fail(error,"Runtime assets omit their actual fuzzy store");}
    bot_fuzzy_store_dispose(library->fuzzy_store);
    library->fuzzy_store=set->source;set->owns_source=false;
    {
        qa_bot_weights **weights = &library->weights; qa_bot_character **characters = &library->characters;
        qa_bot_weapons **weapons = &library->weapon_configs; qa_bot_items **items = &library->item_configs;
        qa_bot_chat_asset **chats = &library->chat_assets;
        for (size_t i = 0; i < set->count; ++i) {
            saved_asset asset = set->assets[i];
            bool source_weight=asset.kind==QA_BOT_SAVED_WEIGHTS &&
                ((qa_bot_weights *)asset.object)->source->store==library->fuzzy_store;
            if (!asset.cached && !source_weight) continue;
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
    }
    return true;
}
