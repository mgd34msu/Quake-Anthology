#include "internal.h"
#include "../library/internal.h"
#include "../goals/internal.h"
#include "../chat/internal.h"
#include "../save_fields.h"
#include "qa/bot_runtime_assets_save.h"
#include "../library/source_fuzzy_view.h"
#include "../library/source_fuzzy_store.h"
#include "source_weapon_setup.h"
#include "../library/source_weapon_library.h"

typedef struct saved_asset {
    qa_bot_saved_asset_kind kind;
    void *object;
} saved_asset;
struct qa_bot_saved_assets {
    saved_asset *assets;size_t count,capacity;
    qa_bot_library *library;
    qa_bot_runtime *runtime;
};
static const uint8_t magic[8] = {'Q', 'A', 'B', 'A', 'R', 'A', 'W', 6};

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
static bool add(qa_bot_saved_assets *set, qa_bot_saved_asset_kind kind, void *object, qa_error *error)
{
    if (!object) return true;
    for (size_t i = 0; i < set->count; ++i) if (set->assets[i].object == object) {
        if (set->assets[i].kind != kind)
            return fail(error, "Bot library cache has a duplicate or mismatched asset");
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
    saved_asset asset = {.kind = kind, .object = object};
    retain(asset); set->assets[set->count++] = asset; return true;
}
static bool collect(const qa_bot_runtime *runtime, qa_bot_saved_assets *set, qa_error *error)
{
    if (!add(set, QA_BOT_SAVED_WEAPONS, runtime->weapon_config, error)) return false;
    for(const bot_weapon_config_identity *identity=runtime->weapon_pointers.configs;identity;identity=identity->next)
        if(!identity->config->source->disposed &&
           !add(set,QA_BOT_SAVED_WEIGHTS,identity->config,error)) return false;
    for(const bot_weapon_pointer *pointer=runtime->weapon_pointers.first;pointer;pointer=pointer->next)
        if(pointer->kind==BOT_WEAPON_POINTER_CONFIG && !pointer->config->source->disposed &&
           !add(set,QA_BOT_SAVED_WEIGHTS,pointer->config,error)) return false;
    for (uint32_t i = 0; i < runtime->options.maximum_states; ++i) {
        if (!add(set, QA_BOT_SAVED_CHARACTER, runtime->characters[i], error)) return false;
    }
    if (runtime->goals) {
        if (!add(set, QA_BOT_SAVED_ITEMS, runtime->goals->items, error)) return false;
        for (const bot_goal_weights *weights = runtime->goals->weights; weights; weights = weights->next)
            if (!add(set, QA_BOT_SAVED_WEIGHTS, weights->weights, error)) return false;
    }
    if (runtime->chat_system) {
        const qa_bot_chat_options *options = &runtime->chat_system->options;
        if (!add(set, QA_BOT_SAVED_CHAT, options->synonyms, error) ||
            !add(set, QA_BOT_SAVED_CHAT, options->randoms, error) ||
            !add(set, QA_BOT_SAVED_CHAT, options->matches, error) ||
            !add(set, QA_BOT_SAVED_CHAT, options->replies, error)) return false;
        for(size_t index=0;index<64;++index) if(runtime->chat_system->initial_cache[index].owner) {
            qa_bot_memory_span bytes;
            if(!qa_bot_memory_bytes(runtime->memory,runtime->chat_system->initial_cache[index],&bytes,error) || bytes.size!=132) return false;
            uint32_t pointer=chat_raw_word(bytes.data);
            for(qa_bot_chat_asset *asset=runtime->library->chat_assets;pointer && asset;asset=asset->next)
                if(asset->initial_source && asset->initial_source->initial.pointer==pointer) {
                    if(!add(set,QA_BOT_SAVED_CHAT,asset,error)) return false;
                    break;
                }
        }
        for (const qa_bot_chat *state = runtime->chat_system->states; state; state = state->next)
            if (!add(set, QA_BOT_SAVED_CHAT, state->initial, error)) return false;
    }
    return true;
}
/* File recipes select installed data. Only mutable fuzzy values and shared
 * message cooldowns follow them; parsed definitions and parser storage do not. */
static bool asset_fields(qa_source_save_io *io, qa_bot_saved_assets *set, size_t index)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    saved_asset *asset=&set->assets[index];uint32_t kind=(uint32_t)asset->kind;
    const char *path=NULL,*name=NULL;float skill=0;size_t first=0,second=0;
    if(!qa_source_save_u32(io,&kind) || kind>QA_BOT_SAVED_CHAT)
        return bot_save_fail(io,QA_ERROR_FORMAT,"Unknown bot asset recipe");
    asset->kind=(qa_bot_saved_asset_kind)kind;
    if(!reading) switch(asset->kind) {
    case QA_BOT_SAVED_WEIGHTS:path=((qa_bot_weights *)asset->object)->source->path;break;
    case QA_BOT_SAVED_CHARACTER: {
        const qa_bot_character_view *view=NULL;
        if(!qa_bot_character_view_read(asset->object,&view,io->error)) return false;
        path=view->path;skill=view->skill;break;
    }
    case QA_BOT_SAVED_WEAPONS: {
        const qa_bot_weapons_view *view=qa_bot_weapons_read(asset->object);
        if(!view) return bot_save_fail(io,QA_ERROR_FORMAT,"Weapon recipe has no loaded configuration");
        path=view->path;first=view->weapon_capacity;second=view->projectile_capacity;break;
    }
    case QA_BOT_SAVED_ITEMS: {
        const qa_bot_items_view *view=qa_bot_items_read(asset->object);
        if(!view) return bot_save_fail(io,QA_ERROR_FORMAT,"Item recipe has no loaded configuration");
        path=view->path;first=view->capacity;break;
    }
    case QA_BOT_SAVED_CHAT: {
        const qa_bot_chat_asset_view *view=qa_bot_chat_asset_read(asset->object);
        if(!view) return bot_save_fail(io,QA_ERROR_FORMAT,"Chat recipe has no loaded definitions");
        path=view->path;name=view->name;first=(size_t)view->kind;break;
    }
    }
    bool ok=bot_save_text(io,&path) && path && (*path || asset->kind==QA_BOT_SAVED_ITEMS);
    if(ok) switch(asset->kind) {
    case QA_BOT_SAVED_WEIGHTS: {
        size_t shared=index;
        if(!reading) for(size_t prior=0;prior<index;++prior)
            if(set->assets[prior].kind==QA_BOT_SAVED_WEIGHTS &&
               ((qa_bot_weights *)set->assets[prior].object)->source==((qa_bot_weights *)asset->object)->source) {shared=prior;break;}
        ok=qa_source_save_count(io,&shared,index);
        qa_bot_weights *weights=asset->object;bool cached=false;const char *filename=NULL;
        if(ok && shared==index) {
            if(!reading) {
                qa_bytes source_name;
                ok=bot_fuzzy_config_filename(&weights->source->source,&source_name,io->error);
                if(ok) {ok=source_name.size<64;filename=(const char *)source_name.data;}
                for(size_t slot=0;slot<128;++slot)
                    if(set->library->fuzzy_store->cached[slot]==weights->source) cached=true;
            }
            if(ok) ok=qa_source_save_bool(io,&cached) && bot_save_text(io,&filename) && filename && strlen(filename)<64;
        }
        if(ok && reading) {
            if(shared<index) {
                ok=set->assets[shared].kind==QA_BOT_SAVED_WEIGHTS;
                if(ok) {weights=set->assets[shared].object;qa_bot_weights_retain(weights);asset->object=weights;}
            } else {
                qa_bot_weights *installed=NULL;
                const char *request=cached && *filename && strlen(filename)<63?filename:path;
                ok=qa_bot_weights_load(set->library,request,&installed,io->error);
                const qa_bot_weights_view *view=ok?qa_bot_weights_read(installed):NULL;
                if(ok && cached) {weights=installed;installed=NULL;}
                else if(ok) ok=view && qa_bot_weights_restore(view,&weights,io->error);
                if(ok) ok=bot_fuzzy_config_filename_write(&weights->source->source,
                    (qa_bytes){(const uint8_t *)filename,strlen(filename)},io->error);
                if(ok && cached) {
                    bot_fuzzy_store *store=set->library->fuzzy_store;size_t available=128;bool present=false;
                    for(size_t slot=0;slot<128;++slot) {
                        if(store->cached[slot]==weights->source) present=true;
                        if(!store->cached[slot] && available==128) available=slot;
                    }
                    if(!present && available==128) ok=bot_save_fail(io,QA_ERROR_FORMAT,"Restored fuzzy cache exceeds its actual Source capacity");
                    else if(!present) {
                        bot_fuzzy_owned_retain(weights->source);store->cached[available]=weights->source;
                        if(store->cached_count<=available) store->cached_count=available+1;
                    }
                }
                qa_bot_weights_release(installed);asset->object=weights;
            }
        }
        if(ok && shared==index) {
            const qa_bot_weights_view *view=qa_bot_weights_read(weights);
            size_t count=view?view->node_count:0;
            ok=view && qa_source_save_count(io,&count,SIZE_MAX/sizeof(qa_bot_weight_value));
            if(ok && reading && count!=view->node_count)
                ok=bot_save_fail(io,QA_ERROR_FORMAT,"Saved fuzzy values differ from installed definitions");
            qa_bot_weight_value *values=ok && reading?calloc(count?count:1,sizeof(*values)):NULL;
            if(ok && reading && !values) ok=bot_save_fail(io,QA_ERROR_MEMORY,"Restoring mutable fuzzy values");
            for(size_t node=0;ok && node<count;++node) {
                qa_bot_weight_value value=reading?(qa_bot_weight_value){0}:view->values[node];
                ok=qa_source_save_f32(io,&value.weight) && qa_source_save_f32(io,&value.minimum) && qa_source_save_f32(io,&value.maximum);
                if(reading && ok) values[node]=value;
            }
            if(ok && reading) ok=bot_weights_source_values_restore(weights,values,count,io->error);
            free(values);
        }
        if(reading) free((void *)filename);
        break;
    }
    case QA_BOT_SAVED_CHARACTER:
        ok=qa_source_save_f32(io,&skill);
        if(ok && reading) ok=qa_bot_character_load(set->library,path,skill,(qa_bot_character **)&asset->object,io->error);
        break;
    case QA_BOT_SAVED_WEAPONS:
        ok=qa_source_save_count(io,&first,INT32_MAX) && qa_source_save_count(io,&second,INT32_MAX);
        if(ok && reading) {
            bot_weapon_resource_host host=bot_runtime_weapon_host(set->runtime);bool source_failure=false;
            ok=bot_weapons_load_source(set->library,path,first,second,&host,(qa_bot_weapons **)&asset->object,&source_failure,io->error);
        }
        break;
    case QA_BOT_SAVED_ITEMS:
        ok=qa_source_save_count(io,&first,INT32_MAX);
        if(ok && !*path) {
            ok=first==0;
            if(ok && reading) {
                qa_bot_items_view empty={.path=""};
                ok=qa_bot_items_restore(&empty,(qa_bot_items **)&asset->object,io->error);
            }
        } else if(ok && reading) ok=qa_bot_items_load(set->library,path,first,(qa_bot_items **)&asset->object,io->error);
        break;
    case QA_BOT_SAVED_CHAT: {
        ok=qa_source_save_count(io,&first,QA_BOT_CHAT_INITIAL) && bot_save_text(io,&name);
        if(ok && reading) ok=qa_bot_chat_asset_load(set->library,(qa_bot_chat_asset_kind)first,path,name,(qa_bot_chat_asset **)&asset->object,io->error);
        size_t count=0;const float *current=ok?qa_bot_chat_cooldowns(asset->object,&count):NULL;
        size_t saved_count=count;
        if(ok) ok=qa_source_save_count(io,&saved_count,SIZE_MAX/sizeof(float)) && saved_count==count;
        float *values=ok && reading?calloc(count?count:1,sizeof(*values)):NULL;
        if(ok && reading && !values) ok=bot_save_fail(io,QA_ERROR_MEMORY,"Restoring mutable chat cooldowns");
        for(size_t message=0;ok && message<count;++message) {
            float value=reading?0:current[message];ok=qa_source_save_f32(io,&value);
            if(ok && reading) values[message]=value;
        }
        if(ok && reading) ok=qa_bot_chat_cooldowns_restore(asset->object,values,count,io->error);
        free(values);break;
    }
    }
    if(!ok && !io->failed) {
        if(!io->error || io->error->code==QA_OK)
            qa_error_set(io->error,QA_ERROR_FORMAT,io->offset,
                "Cannot rebuild saved bot asset '%s' or apply its mutable state",path?path:"");
        io->failed=true;
    }
    if(reading) {free((void *)path);free((void *)name);}
    return ok;
}
bool qa_bot_runtime_assets_capture(const qa_bot_runtime *runtime,qa_buffer *out,qa_bot_saved_assets **refs,qa_error *error)
{
    if(!runtime || !runtime->library || !runtime->characters || !runtime->weapons || !out || !refs || *refs ||
       !qa_bot_runtime_can_destroy(runtime)) return fail(error,"Bot asset capture requires an idle runtime");
    qa_bot_saved_assets *set=calloc(1,sizeof(*set));
    if(!set) {qa_error_set(error,QA_ERROR_MEMORY,0,"Indexing live bot assets");return false;}
    set->library=runtime->library;set->runtime=(qa_bot_runtime *)runtime;
    qa_source_save_io io={0};
    bool ok=collect(runtime,set,error) && qa_source_save_writer(&io,NULL,error) && bot_save_signature(&io,magic) &&
        qa_source_save_count(&io,&set->count,SIZE_MAX);
    for(size_t i=0;ok && i<set->count;++i) ok=asset_fields(&io,set,i);
    if(ok) ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if(ok) *refs=set;else qa_bot_saved_assets_free(set);return ok;
}
bool qa_bot_runtime_assets_restore(qa_bot_runtime *runtime,qa_bytes bytes,qa_bot_saved_assets **out,qa_error *error)
{
    if(!runtime || !runtime->library || !out || *out || !qa_bot_runtime_can_destroy(runtime))
        return fail(error,"Bot asset restore requires its actual detached runtime");
    qa_bot_saved_assets *set=calloc(1,sizeof(*set));
    if(!set) {qa_error_set(error,QA_ERROR_MEMORY,0,"Restoring live bot asset references");return false;}
    set->library=runtime->library;set->runtime=runtime;
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && bot_save_signature(&io,magic) &&
        qa_source_save_count(&io,&set->count,bytes.size/12) && set->count<=SIZE_MAX/sizeof(*set->assets);
    if(ok && set->count) {
        set->assets=calloc(set->count,sizeof(*set->assets));
        if(!set->assets) {set->count=0;ok=bot_save_fail(&io,QA_ERROR_MEMORY,"Restoring bot asset references");}
    }
    /* Rebuild library data before publishing the saved physical handle table.
     * Normal character loads publish handles through these runtime callbacks. */
    bool (*available)(void *)=runtime->library->character_available;
    bool (*publish_character)(void *,qa_bot_character *,qa_error *)=runtime->library->character_publish;
    runtime->library->character_available=NULL;runtime->library->character_publish=NULL;
    for(size_t i=0;ok && i<set->count;++i) ok=asset_fields(&io,set,i);
    runtime->library->character_available=available;runtime->library->character_publish=publish_character;
    if(ok) ok=qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(ok) *out=set;else qa_bot_saved_assets_free(set);return ok;
}
