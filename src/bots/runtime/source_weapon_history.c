#include "source_weapon_history.h"
#include "source_weapon_setup.h"
#include "../library/internal.h"
#include "../library/source_weapon_resource_history.h"
#include <stdlib.h>
#include <string.h>

typedef struct weapon_resource_image {
    qa_bot_weapons *object;
    bot_weapon_resource *resource;
    qa_bot_memory_checkpoint *external;
    bool registered;
    struct weapon_resource_image *next;
} weapon_resource_image;
typedef struct weapon_resource_restore {
    qa_bot_weapons *object;
    bot_weapon_resource *resource;
    qa_bot_memory_prepared *external;
    bool registered;
    struct weapon_resource_restore *next;
} weapon_resource_restore;
struct bot_weapon_pointer_history {
    qa_bot_runtime *runtime;
    uint64_t generation,revision;
    bot_weapon_diagnostic *diagnostics;
    size_t diagnostic_count;
    qa_bot_library *library;
    weapon_resource_image *resources,*last_resource;
    bot_weapon_pointers *owner;
    bot_weapon_pointers state;
    bot_weapon_state *states;
    uint32_t count;
    qa_bot_weapons *config;
};
struct bot_weapon_pointer_restore {
    qa_bot_runtime *runtime;
    uint64_t generation,revision;
    bot_weapon_diagnostic *diagnostics;
    size_t diagnostic_count;
    qa_bot_library *library;
    weapon_resource_restore *resources,*last_resource;
    qa_bot_weapons **old_registered;
    size_t old_count;
    bot_weapon_pointers *owner;
    bot_weapon_pointers state;
    bot_weapon_state *states,*destination;
    uint32_t count;
    qa_bot_weapons *config,**config_destination;
};
static bool copy(const bot_weapon_pointers *source,bot_weapon_pointers *out,qa_error *error) {
    *out=(bot_weapon_pointers){.next_pointer=source->next_pointer};
    for(const bot_weapon_config_identity *row=source->configs;row;row=row->next) {
        bot_weapon_config_identity *next=malloc(sizeof(*next));
        if(!next) goto failed;
        *next=*row;next->next=NULL;qa_bot_weights_retain(next->config);
        if(out->last_config) out->last_config->next=next;else out->configs=next;
        out->last_config=next;
    }
    for(const bot_weapon_pointer *row=source->first;row;row=row->next) {
        bot_weapon_pointer *next=malloc(sizeof(*next));
        if(!next) goto failed;
        *next=*row;next->next=NULL;
        if(next->kind==BOT_WEAPON_POINTER_CONFIG) qa_bot_weights_retain(next->config);
        if(out->last) out->last->next=next;else out->first=next;
        out->last=next;
    }
    return true;
failed:
    bot_weapon_pointers_clear(out);
    qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source weapon pointer history");return false;
}
void bot_weapon_pointer_history_destroy(bot_weapon_pointer_history *image) {
    if(!image) return;
    bot_weapon_diagnostics_dispose(image->diagnostics,image->diagnostic_count);
    while(image->resources) {
        weapon_resource_image *row=image->resources;image->resources=row->next;
        bot_weapon_resource_destroy(row->resource);
        qa_bot_memory_checkpoint_destroy(row->external);
        qa_bot_weapons_release(row->object);free(row);
    }
    bot_weapon_pointers_clear(&image->state);qa_bot_weapons_release(image->config);free(image->states);free(image);
}
static bool resource_capture(bot_weapon_pointer_history *image,qa_bot_weapons *object,bool registered,qa_error *error) {
    if(!object) return true;
    for(weapon_resource_image *row=image->resources;row;row=row->next)
        if(row->object==object) {
            if(!registered) return true;
            qa_error_set(error,QA_ERROR_FORMAT,0,"Weapon resource history repeats a library binding");return false;
        }
    if(!object->source || (registered && object->source->memory!=image->library->memory)) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Weapon history differs from its actual library MEMORY");return false;
    }
    weapon_resource_image *row=calloc(1,sizeof(*row));
    if(!row) {qa_error_set(error,QA_ERROR_MEMORY,0,"Capturing retained weapon resource history");return false;}
    row->object=object;row->registered=registered;qa_bot_weapons_retain(object);
    if(image->last_resource) image->last_resource->next=row;else image->resources=row;
    image->last_resource=row;
    return bot_weapon_resource_clone(object->source,true,&row->resource,error) &&
        (object->source->memory==image->library->memory ||
         qa_bot_memory_checkpoint_capture(object->source->memory,&row->external,error));
}
bool bot_weapon_pointer_capture(qa_bot_runtime *runtime,bot_weapon_pointers *pointers,bot_weapon_state *states,uint32_t count,
    qa_bot_weapons *config,bot_fuzzy_history *fuzzy,
    bot_weapon_pointer_history **out,qa_error *error) {
    qa_bot_library *library=runtime?runtime->library:NULL;
    if(!library || !pointers || !states || !count || SIZE_MAX/count<sizeof(*states) || !fuzzy || !out || *out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Weapon pointer capture requires actual owners and empty output");return false;
    }
    bot_weapon_pointer_history *image=calloc(1,sizeof(*image));
    if(!image) {qa_error_set(error,QA_ERROR_MEMORY,0,"Capturing source weapon pointer owner");return false;}
    image->owner=pointers;image->library=library;
    image->runtime=runtime;image->generation=runtime->weapon_generation;image->revision=runtime->weapon_setup_revision;
    image->diagnostic_count=runtime->weapon_diagnostic_count;
    if(!bot_weapon_diagnostics_copy(runtime->weapon_diagnostics,image->diagnostic_count,&image->diagnostics,error)) {
        bot_weapon_pointer_history_destroy(image);return false;
    }
    image->count=count;image->config=config;qa_bot_weapons_retain(config);
    image->states=malloc((size_t)count*sizeof(*states));
    if(!image->states) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Capturing all physical source weapon states");
        bot_weapon_pointer_history_destroy(image);return false;
    }
    memcpy(image->states,states,(size_t)count*sizeof(*states));
    for(uint32_t i=0;i<count;++i) if(states[i].used) {
        bot_weapon_record qualified;
        if(!bot_weapon_record_bind(states[i].record.memory,states[i].record.allocation,&qualified,error)) {
            bot_weapon_pointer_history_destroy(image);return false;
        }
    }
    if(!copy(pointers,&image->state,error)) {bot_weapon_pointer_history_destroy(image);return false;}
    for(bot_weapon_config_identity *row=pointers->configs;row;row=row->next)
        if(!bot_fuzzy_history_include(fuzzy,row->config,error)) {
            bot_weapon_pointer_history_destroy(image);return false;
        }
    for(bot_weapon_pointer *row=pointers->first;row;row=row->next)
        if(row->kind==BOT_WEAPON_POINTER_CONFIG && !bot_fuzzy_history_include(fuzzy,row->config,error)) {
            bot_weapon_pointer_history_destroy(image);return false;
        }
    for(qa_bot_weapons *resource=library->weapon_configs;resource;resource=resource->next)
        if(!resource_capture(image,resource,true,error)) {bot_weapon_pointer_history_destroy(image);return false;}
    if(!resource_capture(image,config,false,error)) {bot_weapon_pointer_history_destroy(image);return false;}
    *out=image;return true;
}
static bool resources_prepare(bot_weapon_pointer_restore *plan,const bot_weapon_pointer_history *image,
    const qa_bot_memory_prepared *memory,qa_error *error) {
    for(qa_bot_weapons *object=plan->library->weapon_configs;object;object=object->next) {
        if(plan->old_count==SIZE_MAX/sizeof(*plan->old_registered)) {
            qa_error_set(error,QA_ERROR_MEMORY,0,"Weapon binding history exceeds native storage");return false;
        }
        ++plan->old_count;
    }
    if(plan->old_count) {
        plan->old_registered=malloc(plan->old_count*sizeof(*plan->old_registered));
        if(!plan->old_registered) {qa_error_set(error,QA_ERROR_MEMORY,0,"Preparing old weapon resource ownership");return false;}
        size_t index=0;
        for(qa_bot_weapons *object=plan->library->weapon_configs;object;object=object->next)
            plan->old_registered[index++]=object;
    }
    for(const weapon_resource_image *source=image->resources;source;source=source->next) {
        weapon_resource_restore *row=calloc(1,sizeof(*row));
        if(!row) {qa_error_set(error,QA_ERROR_MEMORY,0,"Preparing reached weapon resource history");return false;}
        row->object=source->object;row->registered=source->registered;qa_bot_weapons_retain(row->object);
        if(plan->last_resource) plan->last_resource->next=row;else plan->resources=row;
        plan->last_resource=row;
        if(!bot_weapon_resource_clone(source->resource,false,&row->resource,error)) return false;
        if(source->external && !qa_bot_memory_checkpoint_prepare(source->resource->memory,
            source->external,&row->external,error)) return false;
        if(row->resource->record.allocation.owner &&
           !qa_bot_memory_checkpoint_resolve(row->external?row->external:memory,
            source->resource->record.allocation,&row->resource->record.allocation,error)) return false;
    }
    return true;
}
bool bot_weapon_pointer_prepare(qa_bot_runtime *runtime,bot_weapon_pointers *pointers,bot_weapon_state *states,uint32_t count,
    qa_bot_weapons **config,const bot_weapon_pointer_history *image,
    const qa_bot_memory_prepared *memory,bot_weapon_pointer_restore **out,qa_error *error) {
    qa_bot_library *library=runtime?runtime->library:NULL;
    if(!library || !pointers || !states || !config || !image || image->runtime!=runtime || image->library!=library || image->owner!=pointers || count!=image->count || !memory || !out || *out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Weapon pointer restore differs from its captured owner");return false;
    }
    bot_weapon_pointer_restore *plan=calloc(1,sizeof(*plan));
    if(!plan) {qa_error_set(error,QA_ERROR_MEMORY,0,"Preparing source weapon pointer aliases");return false;}
    plan->owner=pointers;plan->library=library;
    plan->runtime=runtime;plan->generation=image->generation;plan->revision=image->revision;
    plan->diagnostic_count=image->diagnostic_count;
    if(!bot_weapon_diagnostics_copy(image->diagnostics,image->diagnostic_count,&plan->diagnostics,error)) {
        bot_weapon_pointer_finish(plan,false);return false;
    }
    if(!copy(&image->state,&plan->state,error)) {bot_weapon_pointer_finish(plan,false);return false;}
    plan->destination=states;plan->count=count;plan->config_destination=config;
    plan->config=image->config;qa_bot_weapons_retain(plan->config);
    plan->states=malloc((size_t)count*sizeof(*states));
    if(!plan->states) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Preparing all physical source weapon states");
        bot_weapon_pointer_finish(plan,false);return false;
    }
    memcpy(plan->states,image->states,(size_t)count*sizeof(*states));
    for(uint32_t i=0;i<count;++i) if(plan->states[i].used &&
       !qa_bot_memory_checkpoint_resolve(memory,plan->states[i].record.allocation,&plan->states[i].record.allocation,error)) {
        bot_weapon_pointer_finish(plan,false);return false;
    }
    for(bot_weapon_pointer *row=plan->state.first;row;row=row->next)
        if(row->kind==BOT_WEAPON_POINTER_INDEXES &&
           !qa_bot_memory_checkpoint_resolve(memory,row->indexes,&row->indexes,error)) {
            bot_weapon_pointer_finish(plan,false);return false;
        }
    if(!resources_prepare(plan,image,memory,error)) {bot_weapon_pointer_finish(plan,false);return false;}
    *out=plan;return true;
}
void bot_weapon_pointer_finish(bot_weapon_pointer_restore *plan,bool commit) {
    if(!plan) return;
    if(commit) {
        bot_runtime_weapon_diagnostics_clear(plan->runtime);
        plan->runtime->weapon_diagnostics=plan->diagnostics;plan->diagnostics=NULL;
        plan->runtime->weapon_diagnostic_count=plan->runtime->weapon_diagnostic_capacity=plan->diagnostic_count;
        plan->runtime->weapon_generation=plan->generation;plan->runtime->weapon_setup_revision=plan->revision;
        qa_bot_weapons **registered=&plan->library->weapon_configs;
        for(weapon_resource_restore *row=plan->resources;row;row=row->next) {
            qa_bot_memory_checkpoint_finish(row->external,true);row->external=NULL;
            (void)qa_script_adopt_memory(row->resource->reader,NULL);
            bot_weapon_resource *old=row->object->source;row->object->source=row->resource;
            row->resource=NULL;bot_weapon_resource_destroy(old);
            free(row->object->weapons);free(row->object->projectiles);
            row->object->weapons=NULL;row->object->projectiles=NULL;
            row->object->view=(qa_bot_weapons_view){0};
            if(row->registered) {
                qa_bot_weapons_retain(row->object);*registered=row->object;registered=&row->object->next;
            }
        }
        *registered=NULL;
        for(size_t i=0;i<plan->old_count;++i) qa_bot_weapons_release(plan->old_registered[i]);
        bot_weapon_pointers old=*plan->owner;*plan->owner=plan->state;
        bot_weapon_pointers_clear(&old);
        memcpy(plan->destination,plan->states,(size_t)plan->count*sizeof(*plan->states));
        qa_bot_weapons_release(*plan->config_destination);*plan->config_destination=plan->config;
    } else {bot_weapon_pointers_clear(&plan->state);qa_bot_weapons_release(plan->config);}
    while(plan->resources) {
        weapon_resource_restore *row=plan->resources;plan->resources=row->next;
        qa_bot_memory_checkpoint_finish(row->external,false);
        bot_weapon_resource_destroy(row->resource);qa_bot_weapons_release(row->object);free(row);
    }
    bot_weapon_diagnostics_dispose(plan->diagnostics,plan->diagnostic_count);
    free(plan->old_registered);free(plan->states);free(plan);
}
