#include "internal.h"
#include "source_fuzzy_store.h"
#include "source_weapon_resource.h"
#include "character_source.h"
#include "../chat/internal.h"
#include "../checkpoint_internal.h"
#include "qa/script_defines_save.h"

bool bot_grow(void **data, size_t *capacity, size_t need, size_t stride, qa_error *e) {
    if (need <= *capacity)
        return true;
    size_t count = *capacity == 0 ? 16 : *capacity;
    while (count < need && count <= SIZE_MAX / 2)
        count *= 2;
    if (count < need || count > SIZE_MAX / stride) {
        qa_error_set(e, QA_ERROR_MEMORY, need, "Bot resource capacity overflow");
        return false;
    }
    void *next = realloc(*data, count * stride);
    if (next == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, need, "Allocating bot resource storage");
        return false;
    }
    *data = next;
    *capacity = count;
    return true;
}
char *bot_string(qa_arena *arena, qa_bytes bytes, qa_error *e) {
    if (bytes.size == SIZE_MAX) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Bot string size overflow");
        return NULL;
    }
    char *out = qa_arena_alloc(arena, bytes.size + 1, 1, e);
    if (out == NULL)
        return NULL;
    if (bytes.size != 0)
        memcpy(out, bytes.data, bytes.size);
    out[bytes.size] = 0;
    return out;
}
bool bot_fail(qa_script *s, const char *message, qa_error *e) {
    qa_script_location location = qa_script_position(s);
    qa_error_set(e, QA_ERROR_FORMAT, location.offset, "%s:%u: %s",
                 location.path == NULL ? "<bot>" : location.path, location.line, message);
    return false;
}
void bot_warning(qa_bot_library *library, qa_script *s, const char *message) {
    const qa_script_services *services = &library->options.scripts;
    if (services->diagnostic != NULL) {
        qa_script_diagnostic d = {QA_SCRIPT_WARNING, qa_script_position(s), message};
        services->diagnostic(services->context, &d);
    }
}
bool bot_token(qa_script *s, qa_script_token *out, qa_error *e) {
    bool found;
    if (!qa_script_next(s, out, &found, e))
        return false;
    return found || bot_fail(s, "Unexpected end of bot resource", e);
}
bool bot_number(qa_script *s, float *out, bool signed_value, qa_error *e) {
    qa_script_token token;
    if (!bot_token(s, &token, e))
        return false;
    bool negative = qa_script_token_is(&token, "-");
    if (negative && !bot_token(s, &token, e))
        return false;
    if (token.kind != QA_SCRIPT_NUMBER || !isfinite(token.number) || !isfinite((float)token.number))
        return bot_fail(s, "Expected finite bot numeric value", e);
    *out = (float)token.number;
    if (negative && signed_value)
        *out = -*out;
    return true;
}
bool bot_integer(qa_script *s, int32_t *out, qa_error *e) {
    qa_script_token token;
    if (!bot_token(s, &token, e))
        return false;
    if (token.kind != QA_SCRIPT_NUMBER || (token.subtype & QA_SCRIPT_INTEGER) == 0)
        return bot_fail(s, "Expected integer bot resource value", e);
    *out = token.integer;
    return true;
}
float bot_random(const qa_bot_random_source *source) {
    return (float)(source->next(source->context) & 32767) / 32767.0f;
}
bool qa_bot_library_create(const qa_bot_library_options *options, qa_bot_library **out,
                           qa_error *e) {
    if (options == NULL || out == NULL || !qa_script_services_valid(&options->scripts)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot resource services/output");
        return false;
    }
    qa_bot_library *library = calloc(1, sizeof(*library));
    if (library == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating shared bot resource library");
        return false;
    }
    library->options = *options;
    if(!qa_bot_memory_create(NULL,&library->memory,e)) {free(library);return false;}
    library->options.scripts.memory=qa_bot_memory_script_services(library->memory);
    library->options.scripts.file_text=true;
    if (options->preprocessor.globals) {
        qa_script_defines_retain((qa_script_defines *)options->preprocessor.globals);
    } else {
        qa_script_defines *globals = NULL;
        if (!qa_script_defines_create(&globals, e)) {
            (void)qa_bot_memory_release(library->memory,NULL);
            free(library);
            return false;
        }
        library->options.preprocessor.globals = globals;
    }
    if(!qa_script_defines_bind_memory((qa_script_defines *)library->options.preprocessor.globals,
        qa_bot_memory_script_services(library->memory),e)) {qa_bot_library_destroy(library);return false;}
    if (options->preprocessor.include_path != NULL) {
        library->options.preprocessor.include_path =
            bot_string(&library->arena,
                       (qa_bytes){(const uint8_t *)options->preprocessor.include_path,
                                  strlen(options->preprocessor.include_path)},
                       e);
        if (library->options.preprocessor.include_path == NULL) {
            qa_bot_library_destroy(library);
            return false;
        }
    }
    if (options->scripts.date != NULL) {
        library->options.scripts.date = bot_string(
            &library->arena,
            (qa_bytes){(const uint8_t *)options->scripts.date, strlen(options->scripts.date)}, e);
        if (library->options.scripts.date == NULL) {
            qa_bot_library_destroy(library);
            return false;
        }
    }
    if (options->scripts.time != NULL) {
        library->options.scripts.time = bot_string(
            &library->arena,
            (qa_bytes){(const uint8_t *)options->scripts.time, strlen(options->scripts.time)}, e);
        if (library->options.scripts.time == NULL) {
            qa_bot_library_destroy(library);
            return false;
        }
    }
    if(!bot_fuzzy_store_create(library,&library->fuzzy_store,e)) {
        qa_bot_library_destroy(library);return false;
    }
    *out = library;
    return true;
}
const qa_script_defines *qa_bot_library_global_defines(const qa_bot_library *library) {
    return library ? library->options.preprocessor.globals : NULL;
}
struct bot_define_history {qa_script_defines *owner;qa_buffer bytes;};
struct bot_define_history_restore {qa_script_defines_prepared *globals;};
bool bot_define_history_capture(qa_bot_library *library,bot_define_history **out,qa_error *error) {
    if(!library || !out || *out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Global history requires its library and empty output");return false;
    }
    bot_define_history *image=calloc(1,sizeof(*image));
    if(!image) {qa_error_set(error,QA_ERROR_MEMORY,0,"Capturing global define history");return false;}
    image->owner=(qa_script_defines *)library->options.preprocessor.globals;
    qa_script_defines_retain(image->owner);
    if(!qa_script_defines_save_capture(image->owner,&image->bytes,error)) {
        bot_define_history_destroy(image);return false;
    }
    *out=image;return true;
}
void bot_define_history_destroy(bot_define_history *image) {
    if(!image) return;
    qa_buffer_free(&image->bytes);qa_script_defines_release(image->owner);free(image);
}
static bool define_history_alias(void *context,size_t reference,qa_bytes bytes,
    qa_script_memory_allocation *out,qa_script_memory_span *span,qa_error *error) {
    return qa_bot_memory_checkpoint_script_alias(context,reference,bytes,out,span,error);
}
bool bot_define_history_prepare(qa_bot_library *library,const bot_define_history *image,
    const qa_bot_memory_prepared *memory,bot_define_history_restore **out,qa_error *error) {
    if(!library || !image || !memory || !out || *out || image->owner!=library->options.preprocessor.globals) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Global history owner differs from its library");return false;
    }
    bot_define_history_restore *plan=calloc(1,sizeof(*plan));
    if(!plan) {qa_error_set(error,QA_ERROR_MEMORY,0,"Preparing global define history");return false;}
    if(!qa_script_defines_save_prepare(image->owner,(qa_bytes){image->bytes.data,image->bytes.size},
        (void *)memory,define_history_alias,&plan->globals,error)) {free(plan);return false;}
    *out=plan;return true;
}
void bot_define_history_finish(bot_define_history_restore *plan,bool commit) {
    if(!plan) return;
    qa_script_defines_save_finish(plan->globals,commit);free(plan);
}
qa_bot_memory *qa_bot_library_memory(const qa_bot_library *library) {
    return library?library->memory:NULL;
}
bool qa_bot_library_pc_bind(qa_bot_library *library, void *owner, bool (*idle)(void *),
    bool (*close)(void *, bool, qa_error *), qa_error *error) {
    if (!library || !owner || !idle || !close ||
        (library->pc_owner && library->pc_owner != owner)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot PC namespace differs from its actual library owner");
        return false;
    }
    library->pc_owner=owner; library->pc_idle=idle; library->pc_close=close; return true;
}
bool qa_bot_library_pc_close(qa_bot_library *library, bool source, qa_error *error) {
    if (!library || !library->pc_owner) return true;
    void *owner=library->pc_owner;
    if (!library->pc_idle(owner)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Bot PC shutdown overlaps entered source operations"); return false;
    }
    if (!library->pc_close(owner,source,error)) return false;
    qa_bot_library_pc_unbind(library,owner); return true;
}
void qa_bot_library_pc_unbind(qa_bot_library *library, const void *owner) {
    if (library && library->pc_owner==owner) {
        library->pc_owner=NULL; library->pc_idle=NULL; library->pc_close=NULL;
    }
}
bool qa_bot_library_idle(const qa_bot_library *library) {
    if(!library) return true;
    if(library->pc_owner && !library->pc_idle(library->pc_owner)) return false;
    if(library->character_loading || library->item_loading || !qa_bot_memory_idle(library->memory) ||
       (library->fuzzy_store && library->fuzzy_store->active)) return false;
    for (qa_bot_character *c = library->characters; c; c = c->next)
        if (c->active) return false;
    for (qa_bot_items *c = library->item_configs; c; c = c->next)
        if (c->active) return false;
    for(qa_bot_weapons *config=library->weapon_configs;config;config=config->next)
        if(config->source && config->source->active) return false;
    for(qa_bot_chat_asset *asset=library->chat_assets;asset;asset=asset->next)
        if((asset->initial_source && asset->initial_source->active) ||
           (asset->packed_source && asset->packed_source->active)) return false;
    return true;
}
bool qa_bot_library_weights_shutdown(qa_bot_library *library,qa_error *error) {
    return library && library->fuzzy_store?bot_fuzzy_store_shutdown(library->fuzzy_store,error):true;
}
bool qa_bot_library_log_bind(qa_bot_library *library, qa_bot_log *log, qa_error *error) {
    if (!library) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot log binding requires its actual library owner");
        return false;
    }
    library->log = log;
    return true;
}
qa_bot_log *qa_bot_library_log(const qa_bot_library *library) {
    return library ? library->log : NULL;
}
bool qa_bot_library_global_define(qa_bot_library *library, const char *definition, qa_error *e) {
    if (!library) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Bot global definition needs its actual library owner");
        return false;
    }
    return qa_script_defines_add((qa_script_defines *)library->options.preprocessor.globals, definition, e);
}
void qa_bot_library_destroy(qa_bot_library *library) {
    if (library == NULL || !qa_bot_library_idle(library))
        return;
    if (!qa_bot_library_pc_close(library,false,NULL)) return;
    for (qa_bot_weights *c = library->weights; c != NULL;) {
        qa_bot_weights *next = c->next;
        qa_bot_weights_release(c);
        c = next;
    }
    for (qa_bot_character *c = library->characters; c != NULL;) {
        qa_bot_character *next = c->next;
        qa_script_close(c->reader); c->reader = NULL;
        qa_bot_character_release(c);
        c = next;
    }
    for (qa_bot_weapons *c = library->weapon_configs; c != NULL;) {
        qa_bot_weapons *next = c->next;
        qa_bot_weapons_release(c);
        c = next;
    }
    for (qa_bot_items *c = library->item_configs; c != NULL;) {
        qa_bot_items *next = c->next;
        qa_script_close(c->reader); c->reader = NULL;
        qa_bot_items_release(c);
        c = next;
    }
    bot_chat_assets_close(library);
    bot_fuzzy_store_dispose(library->fuzzy_store);
    bot_character_store_release(library->character_store);
    qa_bot_library_variables_clear(library);
    qa_script_defines_release((qa_script_defines *)library->options.preprocessor.globals);
    qa_arena_destroy(&library->arena);
    (void)qa_bot_memory_dispose(library->memory,NULL);
    (void)qa_bot_memory_release(library->memory,NULL);
    free(library);
}
void qa_bot_library_reload(qa_bot_library *library, bool reload) {
    if (library != NULL)
        library->options.reload_characters = reload;
}
bool bot_reload_characters(const qa_bot_library *library) {
    const qa_bot_variable *variable = qa_bot_library_variable(library, "bot_reloadcharacters");
    return variable ? variable->value != 0 : library->options.reload_characters;
}
