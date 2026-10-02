#include "internal.h"
#include <stdio.h>

static uint32_t word(const uint8_t *bytes) {
    return (uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
}
static void store(uint8_t *bytes,uint32_t value) {
    for(uint32_t index=0;index<4;++index) bytes[index]=(uint8_t)(value>>(index*8));
}
bool bot_chat_system_library_bind(qa_bot_chat_system *system,qa_bot_library *library,qa_error *error) {
    if(!system || !library || system->retired || system->restoring ||
       (system->library && system->library!=library)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat state requires its actual single library owner");return false;
    }
    if(system->memory!=library->memory) {
        if(system->states || system->console_capacity) {
            qa_error_set(error,QA_ERROR_ARGUMENT,0,"Live chat records cannot migrate to a different MEMORY owner");return false;
        }
        if(!qa_bot_memory_retain(library->memory,error)) return false;
        (void)qa_bot_memory_release(system->memory,NULL);
        system->memory=library->memory;
    }
    system->library=library;return true;
}
bool chat_initial_free(qa_bot_chat *state,qa_error *error) {
    qa_bot_chat_asset *initial;
    if(!chat_state_initial(state,&initial,error)) return false;
    if(initial && initial->initial_source &&
       !bot_chat_initial_free(&initial->initial_source->initial,error)) return false;
    return qa_bot_chat_set_initial(state,NULL,error);
}
static bool current(const qa_bot_chat *state,uint64_t revision) {
    return !state->retired && !state->system->retired && state->initial_revision==revision;
}
static bool report(qa_bot_chat *state,qa_script_severity severity,const char *format,
    const char *name,const char *path,qa_error *error) {
    int length=snprintf(NULL,0,format,name,path);
    if(length<0) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Formatting initial chat source report");return false;}
    char *message=malloc((size_t)length+1);
    if(!message) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining initial chat source report");return false;}
    (void)snprintf(message,(size_t)length+1,format,name,path);
    bool ok=chat_print(state->system,severity,message,error);free(message);return ok;
}
static bool report_sources(qa_bot_chat *state,uint64_t revision,qa_script_severity severity,
    const char *format,const qa_bot_chat_text_source *first,const qa_bot_chat_text_source *second,
    qa_bot_chat_asset *metadata,qa_error *error) {
    char *name=NULL,*path=NULL;
    bool ok=chat_text_copy(first,SIZE_MAX,&name,error) && current(state,revision);
    if(ok && second) ok=chat_text_copy(second,SIZE_MAX,&path,error) && current(state,revision);
    if(ok && metadata) {
        metadata->view.name=bot_string(&metadata->arena,(qa_bytes){(const uint8_t *)name,strlen(name)},error);
        metadata->view.path=bot_string(&metadata->arena,(qa_bytes){(const uint8_t *)path,strlen(path)},error);
        ok=metadata->view.name && metadata->view.path;
    }
    if(ok) ok=report(state,severity,format,name,path?path:"",error);
    else if(error && error->code==QA_OK) qa_error_set(error,QA_ERROR_ARGUMENT,0,"Initial chat source arguments retired during read");
    free(name);free(path);return ok;
}
static bool equals(const uint8_t *stored,size_t width,const qa_bot_chat_text_source *source,
    qa_bot_chat *state,uint64_t revision,bool *out,qa_error *error) {
    *out=false;
    uint8_t expected[64];
    if(width>sizeof(expected)) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Cached chat comparison exceeds its source field");return false;}
    memcpy(expected,stored,width);
    for(size_t index=0;index<=width;++index) {
        qa_bytes text;
        if(!chat_text_read(source,index+1,&text,error)) return false;
        if(!current(state,revision)) return true;
        uint8_t first=index<width?expected[index]:0,second=index<text.size?text.data[index]:0;
        if(first!=second) return true;
        if(!first) {*out=true;return true;}
    }
    return true;
}
static bool resolve(qa_bot_library *library,uint32_t pointer,qa_bot_chat_asset **out,qa_error *error) {
    *out=NULL;if(!pointer) return true;
    for(qa_bot_chat_asset *asset=library->chat_assets;asset;asset=asset->next)
        if(asset->initial_source && asset->initial_source->published &&
           asset->initial_source->initial.pointer==pointer) {*out=asset;return true;}
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Cached chat pointer has no actual initial allocation view");return false;
}
static bool load(qa_bot_chat *state,qa_bot_library *library,const qa_bot_chat_text_source *path,
    const qa_bot_chat_text_source *name,
    bool developer,int32_t *result,qa_error *error) {
    if(!bot_chat_system_library_bind(state->system,library,error) ||
       !chat_initial_free(state,error)) return false;
    uint64_t revision=state->initial_revision;
    bool reload=state->system->services.reload_characters?
        state->system->services.reload_characters(state->system->services.context):bot_reload_characters(library);
    if(!current(state,revision)) return true;
    size_t available=0;
    if(!reload) {
        available=64;
        for(size_t index=0;index<64;++index) {
            qa_bot_memory_allocation allocation=state->system->initial_cache[index];
            if(!allocation.owner) {if(available==64) available=index;continue;}
            qa_bot_memory_span bytes;
            if(!qa_bot_memory_bytes(library->memory,allocation,&bytes,error)) return false;
            if(bytes.size!=132) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Cached chat allocation has an invalid extent");return false;}
            bool same;
            if(!equals(bytes.data+4,64,path,state,revision,&same,error)) return false;
            if(!current(state,revision)) return true;
            if(!same) continue;
            if(!qa_bot_memory_bytes(library->memory,allocation,&bytes,error)) return false;
            if(!equals(bytes.data+68,64,name,state,revision,&same,error)) return false;
            if(!current(state,revision)) return true;
            if(!same) continue;
            if(!qa_bot_memory_bytes(library->memory,allocation,&bytes,error)) return false;
            qa_bot_chat_asset *asset;
            if(!resolve(library,word(bytes.data),&asset,error) || !qa_bot_chat_set_initial(state,asset,error)) return false;
            *result=0;return true;
        }
        if(available==64) return report_sources(state,revision,QA_SCRIPT_FATAL,
            "ichatdata table full; couldn't load chat %s from %s",name,path,NULL,error);
    }
    qa_bot_chat_asset *asset=NULL;bool source_failure=false;
    bool ok=chat_initial_asset_load_from(library,path,name,state,revision,&asset,&source_failure,error);
    if(!current(state,revision)) {
        bool aborted=asset && asset->initial_source && asset->initial_source->retired_abort &&
            !asset->initial_source->service_failed && !asset->initial_source->report_failed &&
            !asset->initial_source->own_failure;
        qa_bot_chat_asset_release(asset);
        if(aborted && error) *error=(qa_error){0};
        return ok || aborted;
    }
    if(!ok && source_failure) {
        if(asset->initial_source->failure==BOT_CHAT_INITIAL_ROOT_MISSING)
            ok=report_sources(state,revision,QA_SCRIPT_ERROR,"counldn't load %s",path,NULL,NULL,error);
        else if(asset->initial_source->failure==BOT_CHAT_INITIAL_NAME_MISSING)
            ok=report_sources(state,revision,QA_SCRIPT_ERROR,"couldn't find chat %s in %s",name,path,NULL,error);
        else ok=true;
        if(ok && current(state,revision))
            ok=report_sources(state,revision,QA_SCRIPT_FATAL,"couldn't load chat %s from %s",name,path,NULL,error);
        if(ok && error) *error=(qa_error){0};
    } else if(ok) {
        ok=report_sources(state,revision,QA_SCRIPT_INFO,"loaded %s from %s",name,path,asset,error);
        if(ok && current(state,revision)) {
            if(state->system->services.developer)
                developer=state->system->services.developer(state->system->services.context);
            if(developer && current(state,revision)) ok=qa_bot_chat_check_integrity(state->system,asset,error);
        }
        if(ok && current(state,revision)) {
            ok=qa_bot_chat_set_initial(state,asset,error);
            uint64_t published=state->initial_revision;
            bool cache=false;
            if(ok) cache=!(state->system->services.reload_characters?
                state->system->services.reload_characters(state->system->services.context):bot_reload_characters(library));
            if(ok && current(state,published) && cache) {
                qa_bot_memory_allocation allocation;
                ok=qa_bot_memory_allocate(library->memory,132,QA_BOT_MEMORY_HEAP,true,NULL,&allocation,error);
                if(ok && current(state,published)) {
                    /* Publication precedes the fields, so a reached callback
                     * failure preserves the genuine partially filled cache. */
                    state->system->initial_cache[available]=allocation;
                    qa_bot_memory_span bytes;
                    ok=qa_bot_memory_bytes(library->memory,allocation,&bytes,error);
                    if(ok) {
                        store(bytes.data,asset->initial_source->initial.pointer);
                        qa_bytes value;
                        ok=chat_text_read(name,63,&value,error);
                        if(ok && current(state,published)) {
                            ok=qa_bot_memory_bytes(library->memory,allocation,&bytes,error);
                            if(ok) {
                                if(value.size) memcpy(bytes.data+68,value.data,value.size);
                                bytes.data[68+value.size]=0;
                                ok=chat_text_read(path,63,&value,error);
                            }
                            if(ok && current(state,published)) {
                                ok=qa_bot_memory_bytes(library->memory,allocation,&bytes,error);
                                if(ok) {
                                    if(value.size) memcpy(bytes.data+4,value.data,value.size);
                                    bytes.data[4+value.size]=0;
                                }
                            }
                        }
                    }
                }
            }
            if(ok && current(state,published)) *result=0;
        }
    }
    qa_bot_chat_asset_release(asset);return ok;
}
bool qa_bot_chat_load_initial(qa_bot_chat *state,qa_bot_library *library,const char *path,
    const char *name,bool developer,int32_t *result,qa_error *error) {
    if(!state || state->retired || state->system->restoring || !library || !path || !name || !result) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Initial chat load requires its actual state/library/text/output");return false;
    }
    qa_bot_chat_text_source path_source=chat_text_source(path),name_source=chat_text_source(name);
    return qa_bot_chat_load_initial_from(state,library,&path_source,&name_source,developer,result,error);
}
bool qa_bot_chat_load_initial_from(qa_bot_chat *state,qa_bot_library *library,
    const qa_bot_chat_text_source *path,const qa_bot_chat_text_source *name,
    bool developer,int32_t *result,qa_error *error) {
    if(!state || state->retired || state->system->restoring || !library || !path || !path->read ||
       !name || !name->read || !result) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Initial chat load requires actual argument/state owners");return false;
    }
    *result=8;chat_retain(state);
    bool ok=load(state,library,path,name,developer,result,error);
    if(state->retired) *result=8;
    chat_release(state);return ok;
}
