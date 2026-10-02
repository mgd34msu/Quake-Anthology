#include "internal.h"
#include "../checkpoint_internal.h"
#include "../library/character_load.h"
#include "../library/internal.h"
#include "../library/source_weapon_library.h"
#include "qa/bots_log_consumers.h"
#include "qa/bot_weapons_source.h"
#include "source_weapon_setup.h"
#include "../chat/internal.h"
#include <stdio.h>

void bot_runtime_handles_close(qa_bot_runtime *r) {
    for (uint32_t i = 0; i < r->options.maximum_states; ++i) {
        if (r->characters) { qa_bot_character_release(r->characters[i]); r->characters[i] = NULL; }
        if (r->weapons) r->weapons[i] = (bot_weapon_state){0};
    }
    if(r->chats) for(uint32_t i=0;i<64;++i) {qa_bot_chat_destroy(r->chats[i]);r->chats[i]=NULL;}
    bot_weapon_pointers_clear(&r->weapon_pointers);
}
bool bot_runtime_chat_shutdown(qa_bot_runtime *runtime,qa_error *error) {
    qa_bot_chat_system *system=runtime->chat_system;
    if(!system) return true;
    system->revision=0;
    /* The source visits handles 0 through 63 and deliberately leaves 64. */
    for(uint32_t handle=1;handle<64;++handle) {
        qa_bot_chat *state=runtime->chats[handle-1];
        if(state) {
            chat_retain(state);
            bool ok=qa_bot_chat_free(state,error);
            if(state->retired) runtime->chats[handle-1]=NULL;
            chat_release(state);
            if(!ok) return false;
        }
        if(system->revision!=0) return true;
    }
    for(size_t index=0;index<64;++index) {
        qa_bot_memory_allocation allocation=system->initial_cache[index];
        if(!allocation.owner) continue;
        qa_bot_memory_span span;
        if(!qa_bot_memory_bytes(system->memory,allocation,&span,error)) return false;
        if(span.size!=132) return bot_runtime_fail(error,"Cached chat has an invalid source extent");
        uint32_t pointer=chat_raw_word(span.data);
        if(pointer) {
            qa_bot_chat_asset *asset=runtime->library->chat_assets;
            while(asset && (!asset->initial_source || !asset->initial_source->published ||
                asset->initial_source->initial.pointer!=pointer)) asset=asset->next;
            if(!asset) return bot_runtime_fail(error,"Cached chat has no retained source allocation view");
            if(!bot_chat_initial_free(&asset->initial_source->initial,error)) return false;
        }
        if(!qa_bot_memory_free(system->memory,allocation,error)) return false;
        system->initial_cache[index]=(qa_bot_memory_allocation){0};
    }
    if(system->console_heap.owner && !qa_bot_memory_free(system->memory,system->console_heap,error)) return false;
    system->console_heap=(qa_bot_memory_allocation){0};
    qa_bot_chat_asset *matches=system->options.matches;
    if(matches && matches->packed_source && !bot_chat_graph_free_root(&matches->packed_source->graph,
        BOT_CHAT_GRAPH_TEMPLATE,error)) return false;
    system->options.matches=NULL;qa_bot_chat_asset_release(matches);
    qa_bot_chat_asset **packed[2]={&system->options.randoms,&system->options.synonyms};
    for(size_t index=0;index<2;++index) {
        qa_bot_chat_asset *asset=*packed[index];
        if(asset && asset->packed_source && asset->packed_source->allocation.owner &&
           !qa_bot_memory_free(asset->packed_source->memory,asset->packed_source->allocation,error)) return false;
        *packed[index]=NULL;qa_bot_chat_asset_release(asset);
    }
    qa_bot_chat_asset *replies=system->options.replies;
    if(replies && replies->packed_source && !bot_chat_graph_free_root(&replies->packed_source->graph,
        BOT_CHAT_GRAPH_REPLY,error)) return false;
    system->options.replies=NULL;qa_bot_chat_asset_release(replies);
    return true;
}
const qa_bot_character *qa_bot_runtime_character(const qa_bot_runtime *r, uint32_t id) {
    return r && id && id <= r->options.maximum_states ? r->characters[id - 1] : NULL;
}
bool qa_bot_runtime_character_load(qa_bot_runtime *r, const char *path, float skill,
                                   uint32_t *out, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    if (!out || !path) return bot_runtime_fail(e, "missing character path/handle output");
    *out = 0;
    r->busy = true;
    qa_bot_character *character;
    bool interpolated;
    bool ok = bot_character_load(r->library, path, skill, &character, &interpolated, e);
    if (!ok) { r->busy = false; return false; }
    uint32_t available = 0, handle = 0;
    for (uint32_t i = 0; i < r->options.maximum_states; ++i) {
        if (r->characters[i] == character) { handle = i + 1; break; }
        if (!available && !r->characters[i]) available = i + 1;
    }
    if (handle || !available) qa_bot_character_release(character);
    else { r->characters[available - 1] = character; handle = available; }
    if (handle && interpolated)
        ok = qa_bot_character_dump(r->library, r->log, r->characters[handle - 1], e);
    if (ok) *out = handle;
    r->busy = false;
    return ok;
}
bool qa_bot_runtime_character_free(qa_bot_runtime *r, uint32_t id, qa_error *e) {
    if (r && r->closed) return true;
    if (!bot_runtime_mutable(r, e)) return false;
    const qa_bot_variable *reload = qa_bot_library_variable(r->library, "bot_reloadcharacters");
    if (!(reload ? reload->value != 0 : r->options.library.reload_characters)) return true;
    if (!qa_bot_runtime_character(r, id)) {
        if (r->services.diagnostic) {
            r->busy = true;
            r->services.diagnostic(r->services.context, QA_SCRIPT_ERROR, "invalid character handle");
            r->busy = false;
        }
        return true;
    }
    qa_bot_character_release(r->characters[id - 1]);
    r->characters[id - 1] = NULL;
    return true;
}
static bot_weapon_state *weapon_state(const qa_bot_runtime *r, uint32_t id, qa_error *e) {
    if (!r || !id || id > r->options.maximum_states || !r->weapons[id - 1].used) {
        bot_runtime_fail(e, "invalid weapon AI handle");
        return NULL;
    }
    return &r->weapons[id - 1];
}
bool qa_bot_runtime_weapon_has_handle(const qa_bot_runtime *r, uint32_t id) {
    return r && id && id <= r->options.maximum_states && r->weapons[id - 1].used;
}
static bool source_weapon_state(qa_bot_runtime *runtime,uint32_t id,bot_weapon_state **out,qa_error *error) {
    *out=NULL;
    if(qa_bot_runtime_weapon_has_handle(runtime,id)) {*out=&runtime->weapons[id-1];return true;}
    char text[80];int32_t signed_id;memcpy(&signed_id,&id,sizeof(id));
    if(!id || id>runtime->options.maximum_states)
        (void)snprintf(text,sizeof(text),"move state handle %d out of range",signed_id);
    else (void)snprintf(text,sizeof(text),"invalid move state %d",signed_id);
    return bot_runtime_weapon_emit(runtime,QA_SCRIPT_FATAL,text,"<weapon-ai>",error);
}
bool qa_bot_runtime_weapon_allocate(qa_bot_runtime *r, uint32_t *out, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    if (!out) return bot_runtime_fail(e, "missing weapon AI handle output");
    *out = 0;
    for (uint32_t i = 0; i < r->options.maximum_states; ++i)
        if (!r->weapons[i].used) {
            bot_weapon_record record;
            r->busy=true;
            bool ok=bot_weapon_record_allocate(r->memory,&record,e);
            r->busy=false;
            if(!ok) return false;
            r->weapons[i] = (bot_weapon_state){.used = true,.record=record};
            *out = i + 1;
            break;
        }
    return true;
}
static bool release_weapon_weights(qa_bot_runtime *runtime,bot_weapon_state *state,qa_error *error) {
    qa_bot_weights *config;
    if(!bot_weapon_config_get(&runtime->weapon_pointers,&state->record,&config,error)) return false;
    if(config && (!qa_bot_weights_free(config,error) ||
       !bot_weapon_config_set(&runtime->weapon_pointers,&state->record,NULL,error))) return false;
    qa_bot_memory_allocation indexes;bool present;
    return bot_weapon_indexes_get(&runtime->weapon_pointers,&state->record,&indexes,&present,error) &&
        (!present || (qa_bot_memory_free(runtime->memory,indexes,error) &&
                      bot_weapon_indexes_forget(&runtime->weapon_pointers,&state->record,error)));
}
bool bot_runtime_weapons_shutdown(qa_bot_runtime *runtime,qa_error *error) {
    if(runtime->weapon_generation==UINT64_C(9007199254740991) ||
       runtime->weapon_setup_revision==UINT64_C(9007199254740991))
        return bot_runtime_fail(error,"Weapon shutdown counter exceeds the source checkpoint integer domain");
    ++runtime->weapon_generation;++runtime->weapon_setup_revision;
    if(runtime->weapon_config && !bot_weapons_source_free(runtime->weapon_config,error)) return false;
    qa_bot_weapons_release(runtime->weapon_config);runtime->weapon_config=NULL;
    for(uint32_t i=0;i<runtime->options.maximum_states;++i) {
        bot_weapon_state *state=&runtime->weapons[i];
        if(!state->used) continue;
        ++state->revision;
        if(!release_weapon_weights(runtime,state,error) ||
           !qa_bot_memory_free(runtime->memory,state->record.allocation,error)) return false;
        *state=(bot_weapon_state){0};
    }
    return true;
}
bool qa_bot_runtime_weapon_free(qa_bot_runtime *r, uint32_t id, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    bot_weapon_state *s;
    if(!source_weapon_state(r,id,&s,e)) return false;
    if(!s) return true;
    r->busy=true;
    ++s->revision;
    bool ok=release_weapon_weights(r,s,e) && qa_bot_memory_free(r->memory,s->record.allocation,e);
    if(ok) *s = (bot_weapon_state){0};
    r->busy=false;
    return ok;
}
bool qa_bot_runtime_weapon_reset(qa_bot_runtime *r, uint32_t id, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    /* Source reset retains its only fields: weights and their index map. */
    bot_weapon_state *state;
    return source_weapon_state(r,id,&state,e) && (!state || bot_weapon_record_reset(&state->record,e));
}
bool qa_bot_runtime_weapon_capture(const qa_bot_runtime *r, uint32_t id,
                                     qa_bot_weights **out, qa_error *e) {
    bot_weapon_state *s=weapon_state(r,id,e);
    if(!s || !out) return bot_runtime_fail(e,"missing weapon AI checkpoint output");
    *out=NULL;
    qa_bot_weights *weights;
    return bot_weapon_config_get(&r->weapon_pointers,&s->record,&weights,e) &&
        (!weights || qa_bot_weights_clone(weights,out,e));
}
static bool bind_weapon_weights(qa_bot_runtime *runtime,bot_weapon_state *state,qa_bot_weights *weights,qa_error *error) {
    if(!bot_weapon_config_set(&runtime->weapon_pointers,&state->record,weights,error)) return false;
    if(!weights || !runtime->weapon_config) return true;
    int32_t capacity;
    if(!bot_weapons_source_capacity(runtime->weapon_config,&capacity,error)) return false;
    if(capacity<0) return bot_runtime_fail(error,"Weapon mapping has negative source allocation size");
    qa_bot_memory_allocation indexes;
    uint32_t allocated=(uint32_t)capacity;
    if(!bot_weapon_indexes_allocate(runtime->memory,allocated,&indexes,error)) return false;
    for(uint32_t index=0;;++index) {
        if(!bot_weapons_source_capacity(runtime->weapon_config,&capacity,error)) return false;
        if(capacity<=0 || index>=(uint32_t)capacity) break;
        char name[81];int32_t value;
        if(!bot_weapons_source_name(runtime->weapon_config,index,name,error) ||
           !qa_bot_weights_find_value(weights,name,&value,error) ||
           !bot_weapon_index_write(runtime->memory,indexes,index,value,error)) return false;
    }
    return bot_weapon_indexes_publish(&runtime->weapon_pointers,&state->record,indexes,error);
}
bool qa_bot_runtime_weapon_restore(qa_bot_runtime *r, uint32_t id,
                                     qa_bot_weights *weights, qa_error *e) {
    if(!bot_runtime_mutable(r,e)) return false;
    bot_weapon_state *s=weapon_state(r,id,e);
    if(!s) return false;
    r->busy=true;bool ok=bind_weapon_weights(r,s,weights,e);r->busy=false;return ok;
}
bool bot_runtime_weapons_capture(qa_bot_runtime *runtime,bot_fuzzy_history *fuzzy,
    bot_weapon_pointer_history **out,qa_error *error) {
    return bot_weapon_pointer_capture(runtime,&runtime->weapon_pointers,runtime->weapons,
        runtime->options.maximum_states,runtime->weapon_config,fuzzy,out,error);
}
bool bot_runtime_weapons_prepare(qa_bot_runtime *runtime,const bot_weapon_pointer_history *image,
    const qa_bot_memory_prepared *memory,bot_weapon_pointer_restore **out,qa_error *error) {
    return bot_weapon_pointer_prepare(runtime,&runtime->weapon_pointers,runtime->weapons,
        runtime->options.maximum_states,&runtime->weapon_config,image,memory,out,error);
}
static bool weapon_weights(qa_bot_runtime *r, uint32_t id, const char *path, void *context,
                             bool (*read)(void *, const char **, qa_error *),
                             int32_t *result, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    if(!result) return bot_runtime_fail(e,"missing weapon weight result");
    *result=11;
    bot_weapon_state *s;
    if(!source_weapon_state(r,id,&s,e)) return false;
    if(!s) return true;
    if (!path && !read) return bot_runtime_fail(e, "missing weapon weight path/result");
    r->busy=true;
    ++s->revision;
    uint64_t revision=s->revision,generation=r->weapon_generation;
    qa_bot_memory_allocation identity=s->record.allocation;
    if(!release_weapon_weights(r,s,e)) {r->busy=false;return false;}
    if (read && !read(context, &path, e)) { r->busy = false; return false; }
    if (!path) { r->busy = false; return bot_runtime_fail(e, "missing weapon weight path"); }
    qa_error local = {0};
    bool source_failure=false;
    qa_bot_weights *weights=NULL;
    bool ok = qa_bot_weights_load_result(r->library, path, &weights, &source_failure, &local);
    bool current=r->weapon_generation==generation && s->used && s->revision==revision &&
        s->record.allocation.owner==identity.owner && s->record.allocation.generation==identity.generation &&
        s->record.allocation.slot==identity.slot;
    if((ok || source_failure) && !current) {
        if(ok) ok=qa_bot_weights_free(weights,&local);
        else ok=true;
        qa_bot_weights_release(weights);r->busy=false;
        if(!ok && e) *e=local;
        return ok;
    }
    if (!ok && source_failure) {
        static const char prefix[]="couldn't load weapon config ";size_t length=strlen(path);
        if(length>SIZE_MAX-sizeof(prefix)) {
            qa_error_set(&local,QA_ERROR_MEMORY,0,"Weapon weight diagnostic exceeds native extent");
        } else {
            char *message=malloc(sizeof(prefix)+length);
            if(!message) qa_error_set(&local,QA_ERROR_MEMORY,0,"Retaining weapon weight source diagnostic");
            else {
                memcpy(message,prefix,sizeof(prefix)-1);memcpy(message+sizeof(prefix)-1,path,length+1);
                ok=bot_runtime_weapon_emit(r,QA_SCRIPT_FATAL,message,path,&local);free(message);
                if(ok) *result=11;
            }
        }
    } else if(ok) {
        ok=bind_weapon_weights(r,s,weights,&local);
        if(ok) *result=r->weapon_config?0:12;
    }
    qa_bot_weights_release(weights);
    r->busy = false;
    if (!ok && e) *e = local;
    return ok;
}
bool qa_bot_runtime_weapon_weights(qa_bot_runtime *r, uint32_t id, const char *path,
                                    int32_t *result, qa_error *e) {
    return weapon_weights(r, id, path, NULL, NULL, result, e);
}
bool qa_bot_runtime_weapon_weights_from(qa_bot_runtime *r, uint32_t id, void *context,
                                          bool (*read)(void *, const char **, qa_error *),
                                          int32_t *result, qa_error *e) {
    return weapon_weights(r, id, NULL, context, read, result, e);
}
static bool weapon_info_ready(qa_bot_runtime *r,uint32_t id,uint32_t number,bool *ready,qa_error *e) {
    *ready=false;
    if (!r->weapon_config) return true;
    int32_t capacity;
    if(!bot_weapons_source_capacity(r->weapon_config,&capacity,e)) return false;
    if (!number || capacity<0 || number > (uint32_t)capacity) {
        return bot_runtime_weapon_emit(r,QA_SCRIPT_ERROR,"weapon number out of range",
            r->weapon_config->source->path,e);
    }
    bot_weapon_state *state;
    if(!source_weapon_state(r,id,&state,e)) return false;
    if(!state) return true;
    *ready=true;
    return true;
}
bool qa_bot_runtime_weapon_info(qa_bot_runtime *r, uint32_t id, uint32_t number,
                                 qa_bot_weapon_info *weapon, qa_bot_projectile_info *projectile,
                                 bool *found, qa_error *e) {
    if(!r || !weapon || !projectile || !found) return bot_runtime_fail(e,"missing weapon AI output");
    *found=false;bool ready;
    if(!weapon_info_ready(r,id,number,&ready,e)) return false;
    if(!ready) return true;
    if(!bot_weapons_source_info(r->weapon_config,number,weapon,projectile,e)) return false;
    *found=true;return true;
}
bool qa_bot_runtime_weapon_source_info(qa_bot_runtime *r,uint32_t id,uint32_t number,
    qa_bytes *out,bool *found,qa_error *e) {
    if(!r || !out || !found) return bot_runtime_fail(e,"missing source weapon byte output");
    *found=false;bool ready;
    if(!weapon_info_ready(r,id,number,&ready,e)) return false;
    if(!ready) return true;
    if(!bot_weapons_source_bytes(r->weapon_config,number,out,e)) return false;
    *found=true;return true;
}
bool qa_bot_runtime_weapon_choose(qa_bot_runtime *r, uint32_t id, const int32_t *inventory,
                                   size_t count, uint32_t *out, qa_error *e) {
    qa_bot_inventory_view view = {.data = inventory, .count = count};
    return qa_bot_runtime_weapon_choose_view(r, id, &view, out, e);
}
static bool weapon_weight(qa_bot_runtime *runtime,bot_weapon_state *state,uint32_t number,
    const qa_bot_inventory_view *inventory,float *out,bool *found,qa_error *error) {
    *found=false;
    if(!runtime->weapon_config) return true;
    qa_bot_weights *weights;
    if(!bot_weapon_config_get(&runtime->weapon_pointers,&state->record,&weights,error)) return false;
    if(!weights) return true;
    qa_bot_memory_allocation indexes;bool present;
    if(!bot_weapon_indexes_get(&runtime->weapon_pointers,&state->record,&indexes,&present,error)) return false;
    if(!present) return true;
    bool valid;
    if(!bot_weapons_source_valid(runtime->weapon_config,number,&valid,error)) return false;
    if(!valid) return true;
    qa_bot_memory_span bytes;
    if(!qa_bot_memory_bytes(runtime->memory,indexes,&bytes,error)) return false;
    if((uint64_t)number*4>=bytes.size) return true;
    int32_t weight;
    if(!bot_weapon_index_read(runtime->memory,indexes,number,&weight,error)) return false;
    if(weight<0) return true;
    if(!bot_weapon_config_get(&runtime->weapon_pointers,&state->record,&weights,error)) return false;
    bool ok=qa_bot_weights_evaluate_view(weights,(uint32_t)weight,inventory,NULL,runtime->weapon_workspace,out,error);
    if(ok) *found=true;
    return ok;
}
bool qa_bot_runtime_weapon_choose_view(qa_bot_runtime *r, uint32_t id,
                                        const qa_bot_inventory_view *inventory,
                                        uint32_t *out, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    if (!out) return bot_runtime_fail(e, "missing chosen weapon output");
    *out = 0;
    bot_weapon_state *s;
    if(!source_weapon_state(r,id,&s,e)) return false;
    if(!s) return true;
    r->busy = true;
    qa_bot_weights *weights;
    bool ok=bot_weapon_config_get(&r->weapon_pointers,&s->record,&weights,e);
    if(ok && weights && r->weapon_config) {
        float best=0;
        for(uint32_t i=0;ok;++i) {
            int32_t capacity;bool member;
            ok=bot_weapons_source_capacity(r->weapon_config,&capacity,e);
            if(!ok || capacity<=0 || i>=(uint32_t)capacity) break;
            ok=bot_weapons_source_valid(r->weapon_config,i,&member,e);
            if(!ok) break;
            if(!member) continue;
            qa_bot_memory_allocation indexes;bool present;
            ok=bot_weapon_indexes_get(&r->weapon_pointers,&s->record,&indexes,&present,e);
            if(!ok || !present) break;
            float value;bool found;
            ok=weapon_weight(r,s,i,inventory,&value,&found,e);
            if(ok && found && value>best) {best=value;*out=i;}
        }
    }
    r->busy = false;
    return ok;
}
bool qa_bot_runtime_weapon_weight(qa_bot_runtime *r, uint32_t id, uint32_t weapon,
                                   const int32_t *inventory, size_t count, float *out,
                                   bool *found, qa_error *e) {
    qa_bot_inventory_view view={.data=inventory,.count=count};
    return qa_bot_runtime_weapon_weight_view(r,id,weapon,&view,out,found,e);
}
bool qa_bot_runtime_weapon_weight_view(qa_bot_runtime *r,uint32_t id,uint32_t weapon,
                                        const qa_bot_inventory_view *inventory,float *out,
                                        bool *found,qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    if (!out || !found) return bot_runtime_fail(e, "missing weapon weight output");
    *found = false;
    bot_weapon_state *s;
    if(!source_weapon_state(r,id,&s,e)) return false;
    if(!s) return true;
    r->busy=true;
    bool okay=weapon_weight(r,s,weapon,inventory,out,found,e);
    r->busy=false;
    return okay;
}
bool qa_bot_runtime_chat_allocate(qa_bot_runtime *r, uint32_t *out, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    if (!out || !r->chat_system) return bot_runtime_fail(e, "bot chat requires a live owner and handle output");
    *out = 0;
    for (uint32_t i = 0; i < 64; ++i)
        if (!r->chats[i]) {
            if (!qa_bot_chat_create(r->chat_system, 0, &r->chats[i], e)) return false;
            *out = i + 1;
            break;
        }
    return true;
}
qa_bot_chat *qa_bot_runtime_chat(qa_bot_runtime *r, uint32_t id) {
    return r && r->chats && id && id <= 64 ? r->chats[id - 1] : NULL;
}
bool qa_bot_runtime_chat_free(qa_bot_runtime *r, uint32_t id, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    qa_bot_chat *chat = qa_bot_runtime_chat(r, id);
    if(!chat) {
        int32_t signed_id;memcpy(&signed_id,&id,4);char message[96];
        (void)snprintf(message,sizeof(message),!id || id>64?"chat state handle %d out of range":"invalid chat state %d",signed_id);
        r->busy=true;bool ok=qa_bot_log_print(r->log,QA_SCRIPT_FATAL,message,e);r->busy=false;return ok;
    }
    chat_retain(chat);r->busy=true;bool ok=qa_bot_chat_free(chat,e);r->busy=false;
    if(ok && chat->retired) r->chats[id-1]=NULL;
    chat_release(chat);
    return ok;
}
bool qa_bot_runtime_chat_load(qa_bot_runtime *r, uint32_t id, const char *path, const char *name,
                               int32_t *result, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    qa_bot_chat *chat = qa_bot_runtime_chat(r, id);
    if (!chat) return bot_runtime_fail(e, "invalid bot chat handle");
    r->busy = true;
    bool ok = qa_bot_chat_load_initial(chat, r->library, path, name,
        false, result, e);
    r->busy = false;
    return ok;
}
bool qa_bot_runtime_chat_load_from(qa_bot_runtime *runtime,uint32_t id,
    const qa_bot_chat_text_source *path,const qa_bot_chat_text_source *name,
    int32_t *result,qa_error *error) {
    if(!bot_runtime_mutable(runtime,error)) return false;
    qa_bot_chat *chat=qa_bot_runtime_chat(runtime,id);
    if(!chat || !result) return bot_runtime_fail(error,"invalid bot chat handle/output");
    runtime->busy=true;
    bool ok=qa_bot_chat_load_initial_from(chat,runtime->library,path,name,false,result,error);
    runtime->busy=false;return ok;
}
bool qa_bot_runtime_goal_weights(qa_bot_runtime *r, uint32_t id, const char *path,
                                  int32_t *result, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    r->busy = true;
    bool ok = qa_bot_goals_load_weights(r->goals, id, r->library, path, result, e);
    r->busy = false;
    return ok;
}
