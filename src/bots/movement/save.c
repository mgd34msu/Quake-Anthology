#include "internal.h"
#include "../save_fields.h"
#include "qa/bot_movement_save.h"
#include "qa/bots_allocator_save.h"
#include "../checkpoint_internal.h"

static const uint8_t magic[8]={'Q','A','B','M','O','V','E',1};

static bool same_allocation(qa_bot_memory_allocation a,qa_bot_memory_allocation b) {
    return a.owner==b.owner && a.slot==b.slot && a.generation==b.generation;
}
static bool empty(const qa_bot_moves *m) {
    for(uint32_t i=0;i<m->maximum;++i) if(m->slots[i].used) return false;
    return true;
}
static int32_t client_read(const qa_bot_memory_span *bytes) {
    const uint8_t *at=bytes->data+BM_CLIENT;
    uint32_t value=(uint32_t)at[0]|(uint32_t)at[1]<<8|(uint32_t)at[2]<<16|(uint32_t)at[3]<<24;
    return value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);
}
static void slots_clear(bot_move_slot *slots,uint32_t maximum) {
    if(!slots) return;
    for(uint32_t i=0;i<maximum;++i) bot_move_route_clear(&slots[i].state);
}
static bool slots_copy(bot_move_slot *out,const bot_move_slot *source,uint32_t maximum,qa_error *e) {
    for(uint32_t i=0;i<maximum;++i) {
        out[i]=source[i];out[i].state.route=(qa_nav_route){0};
        if(source[i].used && !bot_move_route_copy(&out[i].state,&source[i].state,e)) return false;
    }
    return true;
}
static bool route_fields(qa_source_save_io *io,bot_move_record *record) {
    if(!qa_source_save_bool(io,&record->route.found)) return false;
    if(!record->route.found) return true;
    uint32_t format=(uint32_t)record->route_map.format;
    if(!qa_source_save_u32(io,&format) ||
       !qa_source_save_bytes(io,record->route_map.digest,sizeof(record->route_map.digest)) ||
       !qa_source_save_u32(io,&record->route_goal) ||
       !qa_source_save_u32(io,&record->route_flags) ||
       !qa_source_save_u32(io,&record->route_move_flags) ||
       !qa_source_save_count(io,&record->route_cursor,SIZE_MAX)) return false;
    record->route_map.format=(qa_bsp_format)format;
    size_t *counts[2]={&record->route.node_count,&record->route.edge_count};
    uint32_t **arrays[2]={&record->route.nodes,&record->route.edges};
    for(size_t a=0;a<2;++a) {
        if(!qa_source_save_count(io,counts[a],SIZE_MAX/sizeof(uint32_t))) return false;
        if(io->direction==QA_SOURCE_SAVE_READ &&
           *counts[a]>(io->input.size-io->offset)/sizeof(uint32_t))
            return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated retained actual bot route");
        if(io->direction==QA_SOURCE_SAVE_READ && *counts[a]) {
            *arrays[a]=malloc(*counts[a]*sizeof(uint32_t));
            if(!*arrays[a]) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring retained actual bot route");
        }
        for(size_t i=0;i<*counts[a];++i)
            if(!qa_source_save_u32(io,&(*arrays[a])[i])) return false;
    }
    record->route.node_capacity=record->route.node_count;
    record->route.edge_capacity=record->route.edge_count;
    return true;
}
bool qa_bot_moves_save_capture(const qa_bot_moves *m,qa_buffer *out,qa_error *e) {
    if(!m || m->busy || !out || !m->memory || !m->maximum)
        return bot_move_fail(e,"Bot movement capture requires its actual idle owner");
    qa_source_save_io io={0};
    uint32_t maximum=m->maximum;float time=m->time;
    bool ok=qa_source_save_writer(&io,NULL,e) && bot_save_signature(&io,magic) &&
            qa_source_save_u32(&io,&maximum) && qa_source_save_f32(&io,&time);
    for(size_t i=0;ok && i<BOT_MOVE_VARIABLE_COUNT;++i) {
        const char *name=m->variables[i]?m->variables[i]->name:NULL;
        if(name && m->variables[i]!=qa_bot_library_variable(m->library,bot_move_variable_name((bot_move_variable)i)))
            ok=bot_save_fail(&io,QA_ERROR_FORMAT,"Movement variable differs from its actual LibVar role");
        if(ok) ok=bot_save_text(&io,&name);
    }
    for(uint32_t i=0;ok && i<maximum;++i) {
        const bot_move_slot *slot=&m->slots[i];
        bool used=slot->used;
        ok=qa_source_save_bool(&io,&used);
        if(!ok || !used) continue;
        qa_bot_memory_span bytes;size_t reference=0;
        bool progress=slot->state.walk_progress;uint32_t edge=slot->state.walk_edge;
        bot_move_record record=slot->state;
        ok=slot->state.owner==m && bot_move_record_span(&slot->state,&bytes,e) &&
           qa_bot_memory_reference(m->memory,slot->state.allocation,&reference,e) &&
           qa_source_save_count(&io,&reference,SIZE_MAX) &&
           qa_source_save_bool(&io,&progress) && qa_source_save_u32(&io,&edge) &&
           route_fields(&io,&record);
        for(uint32_t j=0;ok && j<i;++j)
            if(m->slots[j].used && same_allocation(m->slots[j].state.allocation,slot->state.allocation))
                ok=bot_save_fail(&io,QA_ERROR_FORMAT,"Movement handles share one source allocation");
    }
    if(ok) ok=qa_source_save_finish(&io,out);
    if(!ok && (!e || e->code==QA_OK)) bot_save_fail(&io,QA_ERROR_FORMAT,"Invalid movement source aliases");
    qa_source_save_dispose(&io);return ok;
}
bool qa_bot_moves_save_restore(qa_bot_moves *m,qa_bytes saved,qa_error *e) {
    if(!bot_move_mutable(m,e) || !empty(m))
        return bot_move_fail(e,"Movement import requires an empty handle store after MEMORY import");
    qa_source_save_io io={0};uint32_t maximum=0;float time=0;
    const qa_bot_variable *variables[BOT_MOVE_VARIABLE_COUNT]={0};
    bot_move_slot *slots=NULL;
    int32_t *clients=NULL;
    bool ok=qa_source_save_reader(&io,NULL,saved,e) && bot_save_signature(&io,magic) &&
        qa_source_save_u32(&io,&maximum) && maximum==m->maximum &&
        qa_source_save_f32(&io,&time);
    for(size_t i=0;ok && i<BOT_MOVE_VARIABLE_COUNT;++i) {
        const char *name=NULL;
        ok=bot_save_text(&io,&name);
        if(ok && name) {
            variables[i]=qa_bot_library_variable(m->library,bot_move_variable_name((bot_move_variable)i));
            ok=variables[i] && !strcmp(variables[i]->name,name);
        }
        free((void *)name);
    }
    if(ok && (io.offset>io.input.size || maximum>io.input.size-io.offset))
        ok=bot_save_fail(&io,QA_ERROR_FORMAT,"Truncated movement handle table");
    if(ok) {
        slots=calloc(maximum,sizeof(*slots));clients=calloc(maximum,sizeof(*clients));
        if(!slots || !clients) ok=bot_save_fail(&io,QA_ERROR_MEMORY,"Restoring movement allocation references");
    }
    for(uint32_t i=0;ok && i<maximum;++i) {
        bot_move_slot *slot=&slots[i];
        ok=qa_source_save_bool(&io,&slot->used);
        if(!ok || !slot->used) continue;
        size_t reference=0;qa_bot_memory_span bytes;
        slot->state.owner=m;
        ok=qa_source_save_count(&io,&reference,SIZE_MAX) &&
           qa_bot_memory_resolve(m->memory,reference,&slot->state.allocation,e) &&
           bot_move_record_span(&slot->state,&bytes,e) &&
           qa_source_save_bool(&io,&slot->state.walk_progress) &&
           qa_source_save_u32(&io,&slot->state.walk_edge) && route_fields(&io,&slot->state);
        if(ok) clients[i]=client_read(&bytes);
        for(uint32_t j=0;ok && j<i;++j)
            if(slots[j].used && same_allocation(slots[j].state.allocation,slot->state.allocation))
                ok=bot_save_fail(&io,QA_ERROR_FORMAT,"Restored movement handles share one source allocation");
    }
    if(ok) ok=qa_source_save_finish(&io,NULL);
    m->busy=true;
    for(uint32_t i=0;ok && i<maximum;++i) if(slots[i].used &&
        (slots[i].state.walk_progress || slots[i].state.route.found)) {
        qa_bot_navigation *navigation=m->services.navigation(m->services.context,clients[i]);
        ok=navigation && (!slots[i].state.walk_progress ||
            qa_navigation_edge(qa_bot_navigation_runtime(navigation),slots[i].state.walk_edge)) &&
            bot_move_route_bind(&slots[i].state,navigation,e);
    }
    /* Navigation callbacks may invalidate the actual aliases; publication must
     * not bind an allocation that disappeared while resolving managed edges. */
    for(uint32_t i=0;ok && i<maximum;++i) if(slots[i].used) {
        qa_bot_memory_span bytes;ok=bot_move_record_span(&slots[i].state,&bytes,e);
    }
    m->busy=false;
    if(ok) {
        slots_clear(m->slots,m->maximum);
        memcpy(m->slots,slots,(size_t)maximum*sizeof(*slots));
        memcpy(m->variables,variables,sizeof(variables));m->time=time;
        qa_nav_prediction_result_free(&m->prediction);qa_nav_route_free(&m->trajectory);
        m->point_count=0;m->visit_generation=0;
        if(m->visited) memset(m->visited,0,m->visited_capacity*sizeof(*m->visited));
    } else {
        slots_clear(slots,maximum);
        if(!e || e->code==QA_OK)
            qa_error_set(e,QA_ERROR_FORMAT,io.offset,"Invalid movement reference or selected route edge");
    }
    qa_source_save_dispose(&io);free(slots);free(clients);return ok;
}

struct bot_move_history {
    qa_bot_moves *owner;
    qa_bot_memory *memory;
    uint32_t maximum;
    bot_move_slot *slots;
    int32_t *clients;
    const qa_bot_variable *variables[BOT_MOVE_VARIABLE_COUNT];
    float time;
};
struct bot_move_history_restore {
    qa_bot_moves *owner;
    bot_move_history state;
};
void bot_move_history_destroy(bot_move_history *image) {
    if(!image) return;
    slots_clear(image->slots,image->maximum);
    free(image->slots);free(image->clients);free(image);
}
bool bot_move_history_capture(qa_bot_moves *m,bot_move_history **out,qa_error *e) {
    if(!bot_move_mutable(m,e) || !out || *out)
        return bot_move_fail(e,"Movement history requires its actual idle owner and empty output");
    bot_move_history *image=calloc(1,sizeof(*image));
    if(!image) {qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining MovementState allocation aliases");return false;}
    image->owner=m;image->memory=m->memory;image->maximum=m->maximum;image->time=m->time;
    image->slots=calloc(m->maximum,sizeof(*image->slots));
    image->clients=calloc(m->maximum,sizeof(*image->clients));
    if(!image->slots || !image->clients) {
        bot_move_history_destroy(image);qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining movement handle history");return false;
    }
    if(!slots_copy(image->slots,m->slots,m->maximum,e)) {bot_move_history_destroy(image);return false;}
    memcpy(image->variables,m->variables,sizeof(m->variables));
    for(uint32_t i=0;i<m->maximum;++i) if(image->slots[i].used) {
        qa_bot_memory_span bytes;
        if(image->slots[i].state.owner!=m || !bot_move_record_span(&image->slots[i].state,&bytes,e)) {
            bot_move_history_destroy(image);return false;
        }
        image->clients[i]=client_read(&bytes);
        for(uint32_t j=0;j<i;++j) if(image->slots[j].used &&
            same_allocation(image->slots[j].state.allocation,image->slots[i].state.allocation)) {
            bot_move_history_destroy(image);return bot_move_fail(e,"Movement history has duplicated raw allocation ownership");
        }
    }
    *out=image;return true;
}
bool bot_move_history_validate(qa_bot_moves *m,const bot_move_history *image,qa_error *e) {
    if(!m || !image || image->owner!=m || image->memory!=m->memory || image->maximum!=m->maximum)
        return bot_move_fail(e,"Movement history belongs to another owner or capacity");
    for(size_t i=0;i<BOT_MOVE_VARIABLE_COUNT;++i)
        if(image->variables[i] && image->variables[i]!=qa_bot_library_variable(m->library,
            bot_move_variable_name((bot_move_variable)i)))
            return bot_move_fail(e,"Movement history LibVar identity was replaced");
    for(uint32_t i=0;i<m->maximum;++i) if(image->slots[i].used &&
        (image->slots[i].state.walk_progress || image->slots[i].state.route.found)) {
        qa_bot_navigation *navigation=m->services.navigation(m->services.context,image->clients[i]);
        bot_move_record record=image->slots[i].state;
        if(!navigation || (record.walk_progress &&
            !qa_navigation_edge(qa_bot_navigation_runtime(navigation),record.walk_edge)) ||
           !bot_move_route_bind(&record,navigation,e))
            return bot_move_fail(e,"Movement history route is absent from selected navigation");
    }
    return true;
}
bool bot_move_history_prepare(qa_bot_moves *m,const bot_move_history *image,
    const qa_bot_memory_prepared *memory,bot_move_history_restore **out,qa_error *e) {
    if(!m || !image || image->owner!=m || image->memory!=m->memory || image->maximum!=m->maximum ||
       !memory || !out || *out || !m->busy)
        return bot_move_fail(e,"Movement restore requires its captured owner and prepared MEMORY");
    bot_move_history_restore *plan=malloc(sizeof(*plan));
    if(!plan) {qa_error_set(e,QA_ERROR_MEMORY,0,"Preparing MovementState allocation aliases");return false;}
    plan->owner=m;plan->state=*image;
    plan->state.slots=calloc(m->maximum,sizeof(*plan->state.slots));
    plan->state.clients=NULL;
    if(!plan->state.slots) {
        free(plan);qa_error_set(e,QA_ERROR_MEMORY,0,"Preparing movement handle aliases");return false;
    }
    if(!slots_copy(plan->state.slots,image->slots,m->maximum,e)) {
        slots_clear(plan->state.slots,m->maximum);free(plan->state.slots);free(plan);return false;
    }
    for(uint32_t i=0;i<m->maximum;++i) if(plan->state.slots[i].used &&
        !qa_bot_memory_checkpoint_resolve(memory,plan->state.slots[i].state.allocation,
             &plan->state.slots[i].state.allocation,e)) {
        slots_clear(plan->state.slots,m->maximum);free(plan->state.slots);free(plan);return false;
    }
    *out=plan;return true;
}
void bot_move_history_finish(bot_move_history_restore *plan,bool commit) {
    if(!plan) return;
    if(commit) {
        qa_bot_moves *m=plan->owner;
        slots_clear(m->slots,m->maximum);
        memcpy(m->slots,plan->state.slots,(size_t)m->maximum*sizeof(*m->slots));
        memcpy(m->variables,plan->state.variables,sizeof(m->variables));m->time=plan->state.time;
        qa_nav_prediction_result_free(&m->prediction);qa_nav_route_free(&m->trajectory);
        m->point_count=0;m->visit_generation=0;
        if(m->visited) memset(m->visited,0,m->visited_capacity*sizeof(*m->visited));
    } else slots_clear(plan->state.slots,plan->state.maximum);
    free(plan->state.slots);free(plan);
}
