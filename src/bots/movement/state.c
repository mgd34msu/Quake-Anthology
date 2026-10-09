#include "internal.h"
#include "../checkpoint_internal.h"
#include <stdio.h>

void bot_move_route_clear(bot_move_record *record) {
    qa_nav_route_free(&record->route);
    record->route_map=(qa_nav_map){0};record->route_actor=(qa_actor_id){0};
    record->route_goal=record->route_flags=record->route_move_flags=0;
    record->route_cursor=0;
}
bool bot_move_route_copy(bot_move_record *out,const bot_move_record *source,qa_error *e) {
    qa_nav_route copy=source->route;
    copy.graph=NULL;copy.nodes=NULL;copy.edges=NULL;copy.points=NULL;
    copy.node_capacity=copy.node_count;copy.edge_capacity=copy.edge_count;
    copy.point_capacity=0;copy.point_count=0;
    if(copy.node_count) copy.nodes=malloc(copy.node_count*sizeof(*copy.nodes));
    if(copy.edge_count) copy.edges=malloc(copy.edge_count*sizeof(*copy.edges));
    if((copy.node_count && !copy.nodes) || (copy.edge_count && !copy.edges)) {
        qa_nav_route_free(&copy);
        qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining actual bot route decisions");return false;
    }
    if(copy.node_count) memcpy(copy.nodes,source->route.nodes,copy.node_count*sizeof(*copy.nodes));
    if(copy.edge_count) memcpy(copy.edges,source->route.edges,copy.edge_count*sizeof(*copy.edges));
    copy.graph=source->route.graph;qa_nav_graph_retain(copy.graph);
    out->route=copy;out->route_map=source->route_map;out->route_actor=source->route_actor;
    out->route_goal=source->route_goal;out->route_flags=source->route_flags;
    out->route_move_flags=source->route_move_flags;out->route_cursor=source->route_cursor;
    return true;
}
bool bot_move_route_bind(bot_move_record *record,qa_bot_navigation *navigation,qa_error *e) {
    if(!record->route.found) return true;
    qa_navigation *runtime=qa_bot_navigation_runtime(navigation);
    const qa_nav_graph_view *graph=qa_navigation_graph(runtime);
    if(!graph || graph->map.format!=record->route_map.format ||
       !record->route.node_count || !record->route.edge_count ||
       record->route.node_count!=record->route.edge_count+1 ||
       record->route_cursor>record->route.edge_count ||
       record->route.node_count>graph->node_count ||
       record->route.nodes[record->route.node_count-1]!=
           qa_bot_navigation_node(navigation,record->route_goal))
        return bot_move_fail(e,"Retained bot route differs from its selected actual map");
    for(size_t i=0;i<record->route.edge_count;++i) {
        const qa_nav_edge *edge=qa_navigation_edge(runtime,record->route.edges[i]);
        if(!edge || edge->from!=record->route.nodes[i] || edge->to!=record->route.nodes[i+1])
            return bot_move_fail(e,"Retained bot route edge differs from its selected actual graph");
    }
    record->route_map=graph->map;
    record->route_actor=qa_bot_navigation_actor(navigation);
    return true;
}

bool bot_move_record_span(const bot_move_record *record, qa_bot_memory_span *out, qa_error *e) {
    qa_bot_memory_kind kind;
    if (!record || !record->owner ||
        !qa_bot_memory_bytes(record->owner->memory, record->allocation, out, e) ||
        !qa_bot_memory_kind_read(record->owner->memory,record->allocation,&kind,e)) return false;
    return out->size == BOT_MOVE_STATE_BYTES && kind==QA_BOT_MEMORY_HEAP ? true :
        bot_move_fail(e, "MovementState source allocation must contain exactly 772 bytes");
}
static uint8_t *record_bytes(const bot_move_record *record, uint32_t offset, uint32_t bytes) {
    bot_move_scope *scope = record->owner->scope;
    qa_bot_memory_span span;
    if (offset > BOT_MOVE_STATE_BYTES || bytes > BOT_MOVE_STATE_BYTES-offset ||
        !bot_move_record_span(record, &span, scope->error)) {
        if (offset > BOT_MOVE_STATE_BYTES || bytes > BOT_MOVE_STATE_BYTES-offset)
            bot_move_fail(scope->error, "MovementState source field is outside its allocation");
        longjmp(scope->jump, 1);
    }
    return span.data+offset;
}
static uint32_t read_word(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void write_word(uint8_t *p, uint32_t value) {
    for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(value>>(i*8));
}
uint32_t bot_move_word(const bot_move_record *r, bot_move_field field) {
    return read_word(record_bytes(r,(uint32_t)field,4));
}
int32_t bot_move_integer(const bot_move_record *r, bot_move_field field) {
    uint32_t value=bot_move_word(r,field);
    return value<=INT32_MAX ? (int32_t)value : -1-(int32_t)(UINT32_MAX-value);
}
float bot_move_float(const bot_move_record *r, bot_move_field field) {
    uint32_t bits=bot_move_word(r,field);float value;memcpy(&value,&bits,4);return value;
}
qa_vec3 bot_move_vector(const bot_move_record *r, bot_move_field field) {
    float x=bot_move_float(r,field);
    float y=bot_move_float(r,(bot_move_field)(field+4));
    float z=bot_move_float(r,(bot_move_field)(field+8));
    return qa_v3(x,y,z);
}
void bot_move_write_word(bot_move_record *r, bot_move_field field, uint32_t value) {
    write_word(record_bytes(r,(uint32_t)field,4),value);
}
void bot_move_write_float(bot_move_record *r, bot_move_field field, float value) {
    uint32_t bits;memcpy(&bits,&value,4);bot_move_write_word(r,field,bits);
}
void bot_move_write_vector(bot_move_record *r, bot_move_field field, qa_vec3 value) {
    bot_move_write_float(r,field,value.x);
    bot_move_write_float(r,(bot_move_field)(field+4),value.y);
    bot_move_write_float(r,(bot_move_field)(field+8),value.z);
}
void bot_move_spot_admit(const bot_move_record *r,int32_t index) {
    if(index<0 || index>=QA_BOT_AVOID_SPOTS) {
        bot_move_fail(r->owner->scope->error,"MovementState avoid spot index is outside its source array");
        longjmp(r->owner->scope->jump,1);
    }
}
qa_bot_avoid_spot bot_move_spot(const bot_move_record *r,int32_t index) {
    bot_move_spot_admit(r,index);
    bot_move_field at=(bot_move_field)(BM_AVOID_SPOTS+index*20);
    return (qa_bot_avoid_spot){bot_move_vector(r,at),bot_move_float(r,(bot_move_field)(at+12)),
                             bot_move_integer(r,(bot_move_field)(at+16))};
}
void bot_move_write_spot(bot_move_record *r,int32_t index,qa_bot_avoid_spot spot) {
    bot_move_spot_admit(r,index);
    bot_move_field at=(bot_move_field)(BM_AVOID_SPOTS+index*20);
    bot_move_write_vector(r,at,spot.origin);
    bot_move_write_float(r,(bot_move_field)(at+12),spot.radius);
    bot_move_write_word(r,(bot_move_field)(at+16),(uint32_t)spot.type);
}
static bool origin_component(void *context,unsigned component,float *out,qa_error *e) {
    (void)e;
    if(component>2) return false;
    *out=bot_move_float(context,(bot_move_field)(BM_ORIGIN+component*4));return true;
}
qa_bot_vector_source bot_move_origin_source(bot_move_record *r) {
    return (qa_bot_vector_source){.context=r,.read=origin_component};
}

bool bot_move_fail(qa_error *e, const char *message) {
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
bool bot_move_mutable(qa_bot_moves *m, qa_error *e) {
    return m && !m->busy ? true : bot_move_fail(e, "bot movement owner is absent or active");
}
bool qa_bot_moves_active(const qa_bot_moves *m) { return m && m->busy; }
bool qa_bot_moves_has_handle(const qa_bot_moves *m, uint32_t id) {
    return m && id && id <= m->maximum && m->slots[id - 1].used;
}
qa_bot_memory *qa_bot_moves_memory(const qa_bot_moves *m) { return m?m->memory:NULL; }
bool qa_bot_moves_allocation(const qa_bot_moves *m,uint32_t id,qa_bot_memory_allocation *out,qa_error *e) {
    if(!out) return bot_move_fail(e,"missing movement allocation output");
    bot_move_record *record=bot_move_state(m,id,e);qa_bot_memory_span bytes;
    if(!record || !bot_move_record_span(record,&bytes,e)) return false;
    *out=record->allocation;return true;
}
bot_move_record *bot_move_state(const qa_bot_moves *m, uint32_t id, qa_error *e) {
    if (!m || !id || id > m->maximum || !m->slots[id - 1].used) {
        bot_move_fail(e, "invalid bot movement state handle");
        return NULL;
    }
    return &m->slots[id - 1].state;
}
bot_move_record *bot_move_source_state(qa_bot_moves *m,uint32_t id) {
    if(id && id<=m->maximum && m->slots[id-1].used) return &m->slots[id-1].state;
    char text[96];
    snprintf(text,sizeof(text),!id || id>m->maximum?"move state handle %u out of range\n":"invalid move state %u\n",id);
    bool prior=m->busy;m->busy=true;
    if(m->services.diagnostic) m->services.diagnostic(m->services.context,QA_SCRIPT_FATAL,text);
    m->busy=prior;return NULL;
}
bool qa_bot_moves_create(uint32_t maximum, qa_bot_library *library, qa_bot_actions *actions,
                         const qa_bot_move_services *services, qa_bot_moves **out, qa_error *e) {
    if (!maximum || SIZE_MAX/maximum<sizeof(bot_move_slot) || !library || !actions ||
        !services || !services->navigation || !services->random.next || !out || *out)
        return bot_move_fail(e, "invalid bot movement services/capacity");
    qa_bot_moves *m = calloc(1, sizeof(*m));
    if (!m || !(m->slots = calloc(maximum, sizeof(*m->slots)))) {
        free(m);
        qa_error_set(e, QA_ERROR_MEMORY, maximum, "allocating native bot movement states");
        return false;
    }
    m->maximum = maximum;
    m->library = library;
    m->memory = qa_bot_library_memory(library);
    if (!qa_bot_memory_retain(m->memory,e)) { free(m->slots);free(m);return false; }
    m->actions = actions;
    m->services = *services;
    if (!qa_nav_workspace_create(&m->workspace, e)) {
        qa_bot_moves_destroy(m);
        return false;
    }
    *out = m;
    return true;
}
bool qa_bot_moves_prepare_graph(qa_bot_moves *m, size_t edges, qa_error *e) {
    if (edges <= m->visited.count && m->point_capacity)
        return true;
    if (edges > (SIZE_MAX - 1) / 2 ||
        edges * 2 + 1 > SIZE_MAX / sizeof(*m->points) ||
        edges > SIZE_MAX / sizeof(*m->visited.marks))
        return bot_move_fail(e, "bot route storage exceeds address range");
    size_t count = edges * 2 + 1;
    qa_vec3 *points = malloc(count * sizeof(*points));
    uint32_t *marks = edges ? malloc(edges * sizeof(*marks)) : NULL;
    if (!points || (edges && !marks)) {
        free(points); free(marks);
        qa_error_set(e, QA_ERROR_MEMORY, edges, "preparing bot route workspace");
        return false;
    }
    free(m->points); free(m->visited.marks);
    m->points = points;
    m->point_count = 0;
    m->point_capacity = count;
    qa_stamp_set_init(&m->visited, marks, edges);
    return true;
}
void qa_bot_moves_destroy(qa_bot_moves *m) {
    if (!m || m->busy)
        return;
    if (!qa_bot_memory_release(m->memory,NULL)) return;
    for(uint32_t i=0;i<m->maximum;++i) bot_move_route_clear(&m->slots[i].state);
    qa_nav_prediction_result_free(&m->prediction);
    qa_nav_route_free(&m->trajectory);
    qa_nav_workspace_destroy(m->workspace);
    free(m->points);
    free(m->visited.marks);
    free(m->slots);
    free(m);
}
bool qa_bot_moves_shutdown(qa_bot_moves *m,qa_error *e) {
    if (!m) return true;
    if (!bot_move_mutable(m,e)) return false;
    for(uint32_t i=0;i<m->maximum;++i) if(m->slots[i].used) {
        m->busy=true;
        bool ok=qa_bot_memory_free(m->memory,m->slots[i].state.allocation,e);
        m->busy=false;
        if(!ok) return false;
        bot_move_route_clear(&m->slots[i].state);
        m->slots[i]=(bot_move_slot){0};
    }
    return true;
}
const char *bot_move_variable_name(bot_move_variable variable) {
    static const char *const names[BOT_MOVE_VARIABLE_COUNT] = {
        "sv_step",          "sv_maxbarrier",     "sv_gravity",        "weapindex_rocketlauncher",
        "weapindex_bfg10k", "weapindex_grapple", "entitytypemissile", "offhandgrapple",
        "cmd_grappleon",    "cmd_grappleoff"};
    return (unsigned)variable < BOT_MOVE_VARIABLE_COUNT ? names[variable] : NULL;
}
bool qa_bot_moves_setup(qa_bot_moves *m, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    static const char *const defaults[BOT_MOVE_VARIABLE_COUNT] = {
        "18", "32", "800", "5", "9", "10", "3", "0", "grappleon", "grappleoff"};
    for (size_t i = 0; i < BOT_MOVE_VARIABLE_COUNT; ++i)
        if (!qa_bot_library_variable_default(m->library, bot_move_variable_name((bot_move_variable)i), defaults[i], &m->variables[i],
                                             e))
            return false;
    return true;
}
bool qa_bot_moves_time(qa_bot_moves *m, float time, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    if (!isfinite(time))
        return bot_move_fail(e, "invalid movement observation time");
    m->time = time;
    return true;
}
bool qa_bot_moves_allocate(qa_bot_moves *m, uint32_t *out, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    if (!out)
        return bot_move_fail(e, "missing move state handle output");
    *out = 0;
    for (uint32_t i = 0; i < m->maximum; ++i)
        if (!m->slots[i].used) {
            qa_bot_memory_allocation allocation;
            m->busy=true;
            bool ok=qa_bot_memory_allocate(m->memory,BOT_MOVE_STATE_BYTES,QA_BOT_MEMORY_HEAP,
                                           true,NULL,&allocation,e);
            m->busy=false;
            if(!ok) return false;
            m->slots[i] = (bot_move_slot){.used=true,.state={.owner=m,.allocation=allocation}};
            *out = i + 1;
            break;
        }
    return true;
}
bool qa_bot_moves_free(qa_bot_moves *m, uint32_t id, qa_error *e) {
    if (!bot_move_mutable(m, e)) return false;
    bot_move_record *record=bot_move_source_state(m,id);
    if (!record) return true;
    m->busy=true;
    bool ok=qa_bot_memory_free(m->memory,record->allocation,e);
    m->busy=false;
    if(!ok) return false;
    bot_move_route_clear(record);
    m->slots[id - 1] = (bot_move_slot){0};
    return true;
}
bool qa_bot_moves_initialize(qa_bot_moves *m, uint32_t id, const qa_bot_move_input *input,
                             qa_error *e) {
    qa_bot_move_init_source source = {.value = input};
    return qa_bot_moves_initialize_from(m, id, &source, e);
}
static bool input_integer(const qa_bot_move_init_source *source, qa_bot_move_init_field field,
                           int32_t *out, qa_error *e) {
    if (!source->value) return source->integer(source->context, field, out, e);
    const qa_bot_move_input *v = source->value;
    switch (field) {
    case QA_BOT_INIT_ENTITY: *out = v->entity; break;
    case QA_BOT_INIT_CLIENT: *out = v->client; break;
    case QA_BOT_INIT_PRESENCE: memcpy(out, &v->presence, sizeof(*out)); break;
    case QA_BOT_INIT_FLAGS: memcpy(out, &v->flags, sizeof(*out)); break;
    }
    return true;
}
static bool input_vector(const qa_bot_move_init_source *source, qa_bot_move_init_vector field,
                          qa_vec3 *out, qa_error *e) {
    if (source->value) {
        const qa_bot_move_input *v = source->value;
        *out = field == QA_BOT_INIT_ORIGIN ? v->origin :
            field == QA_BOT_INIT_VELOCITY ? v->velocity :
            field == QA_BOT_INIT_VIEW_OFFSET ? v->view_offset : v->view_angles;
        return true;
    }
    qa_vec3 value;
    if (!source->vector(source->context, field, 0, &value.x, e) ||
        !source->vector(source->context, field, 1, &value.y, e) ||
        !source->vector(source->context, field, 2, &value.z, e)) return false;
    *out = value;
    return true;
}
static bool initialize_from(qa_bot_moves *m,uint32_t id,const qa_bot_move_init_source *source,qa_error *e) {
    if(!bot_move_mutable(m,e)) return false;
    bot_move_record *s=bot_move_source_state(m,id);
    if(!s) return true;
    if(!source || (!source->value && (!source->integer || !source->vector || !source->think_time)))
        return bot_move_fail(e,"missing bot movement input reader");
    m->busy=true;
    int32_t word;qa_vec3 vector;float think;
    if(!input_integer(source,QA_BOT_INIT_FLAGS,&word,e)) return false;
    if((uint32_t)word&QA_BOT_MOVE_TELEPORTED) {
        s->walk_progress=false;bot_move_route_clear(s);
    }
    if(!input_vector(source,QA_BOT_INIT_ORIGIN,&vector,e)) return false;
    bot_move_write_vector(s,BM_ORIGIN,vector);
    if(!input_vector(source,QA_BOT_INIT_VELOCITY,&vector,e)) return false;
    bot_move_write_vector(s,BM_VELOCITY,vector);
    if(!input_vector(source,QA_BOT_INIT_VIEW_OFFSET,&vector,e)) return false;
    bot_move_write_vector(s,BM_VIEW_OFFSET,vector);
    if(!input_integer(source,QA_BOT_INIT_ENTITY,&word,e)) return false;
    bot_move_write_word(s,BM_ENTITY,(uint32_t)word);
    if(!input_integer(source,QA_BOT_INIT_CLIENT,&word,e)) return false;
    bot_move_write_word(s,BM_CLIENT,(uint32_t)word);
    if(source->value) think=source->value->think_time;
    else if(!source->think_time(source->context,&think,e)) return false;
    bot_move_write_float(s,BM_THINK_TIME,think);
    if(!input_integer(source,QA_BOT_INIT_PRESENCE,&word,e)) return false;
    bot_move_write_word(s,BM_PRESENCE,(uint32_t)word);
    if(!input_vector(source,QA_BOT_INIT_VIEW_ANGLES,&vector,e)) return false;
    bot_move_write_vector(s,BM_VIEW_ANGLES,vector);
    uint32_t flags=bot_move_word(s,BM_FLAGS);
    if(!input_integer(source,QA_BOT_INIT_FLAGS,&word,e)) return false;
    uint32_t mask=QA_BOT_MOVE_ON_GROUND|QA_BOT_MOVE_TELEPORTED|QA_BOT_MOVE_WATER_JUMP|
                  QA_BOT_MOVE_WALK|QA_BOT_MOVE_GRAPPLE_PULL;
    bot_move_write_word(s,BM_FLAGS,(flags&~mask)|((uint32_t)word&mask));
    return true;
}
bool qa_bot_moves_initialize_from(qa_bot_moves *m,uint32_t id,const qa_bot_move_init_source *source,qa_error *e) {
    BOT_MOVE_OPERATION(m,e,initialize_from(m,id,source,e));
}
static bool reset(qa_bot_moves *m,uint32_t id,qa_error *e) {
    if(!bot_move_mutable(m,e)) return false;
    bot_move_record *s=bot_move_source_state(m,id);
    if(!s) return true;
    bot_move_route_clear(s);
    s->walk_progress=false;
    memset(record_bytes(s,0,BOT_MOVE_STATE_BYTES),0,BOT_MOVE_STATE_BYTES);
    return true;
}
bool qa_bot_moves_reset(qa_bot_moves *m,uint32_t id,qa_error *e) {
    BOT_MOVE_OPERATION(m,e,reset(m,id,e));
}
static bool reset_avoid(qa_bot_moves *m,uint32_t id,bool last,qa_error *e) {
    if(!bot_move_mutable(m,e)) return false;
    bot_move_record *s=bot_move_source_state(m,id);
    if(!s) return true;
    if(!last) {
        bot_move_write_word(s,BM_AVOID_REACHABILITY,0);
        bot_move_write_float(s,BM_AVOID_TIME,0);
        bot_move_write_word(s,BM_AVOID_TRIES,0);
    } else if(bot_move_float(s,BM_AVOID_TIME)>0) {
        bot_move_write_float(s,BM_AVOID_TIME,0);
        if(bot_move_integer(s,BM_AVOID_SPOTS)>0)
            bot_move_write_word(s,BM_AVOID_TRIES,bot_move_word(s,BM_AVOID_TRIES)-1u);
    }
    return true;
}
bool qa_bot_moves_reset_avoid(qa_bot_moves *m,uint32_t id,bool last,qa_error *e) {
    BOT_MOVE_OPERATION(m,e,reset_avoid(m,id,last,e));
}
void bot_move_avoid(qa_bot_moves *m,bot_move_record *s,uint32_t reach,float duration) {
    if(bot_move_word(s,BM_AVOID_REACHABILITY)==reach) {
        uint32_t tries=bot_move_float(s,BM_AVOID_TIME)>m->time ? bot_move_word(s,BM_AVOID_TRIES)+1u : 1u;
        bot_move_write_word(s,BM_AVOID_TRIES,tries);
        bot_move_write_float(s,BM_AVOID_TIME,m->time+duration);
    } else if(bot_move_float(s,BM_AVOID_TIME)<m->time) {
        bot_move_write_word(s,BM_AVOID_REACHABILITY,reach);
        bot_move_write_float(s,BM_AVOID_TIME,m->time+duration);
        bot_move_write_word(s,BM_AVOID_TRIES,1);
    }
}
void bot_move_set_reach(bot_move_record *s,uint32_t reach) {
    if(bot_move_word(s,BM_LAST_REACHABILITY)!=reach) s->walk_progress=false;
    bot_move_write_word(s,BM_LAST_REACHABILITY,reach);
}
bool qa_bot_moves_avoid_spot(qa_bot_moves *m,uint32_t id,const qa_bot_avoid_spot *spot,qa_error *e) {
    if(!spot) return bot_move_fail(e,"missing bot avoid spot");
    qa_bot_vector_source source={.value=&spot->origin};
    return qa_bot_moves_avoid_spot_from(m,id,&source,spot->radius,spot->type,e);
}
static bool avoid_spot_from(qa_bot_moves *m,uint32_t id,const qa_bot_vector_source *source,
                             float radius,int32_t type,qa_error *e) {
    if(!bot_move_mutable(m,e)) return false;
    bot_move_record *s=bot_move_source_state(m,id);
    if(!s) return true;
    if(!type) bot_move_write_word(s,BM_AVOID_COUNT,0);
    else {
        int32_t count=bot_move_integer(s,BM_AVOID_COUNT);
        if(count>=QA_BOT_AVOID_SPOTS) return true;
        /* Source validates its array index before evaluating origin components. */
        bot_move_spot_admit(s,count);
        if(!source || (!source->value && !source->read)) return bot_move_fail(e,"missing bot avoid-spot origin fields");
        m->busy=true;
        qa_bot_avoid_spot spot={.radius=radius,.type=type};
        if(!qa_bot_vector_read(source,&spot.origin,e)) return false;
        bot_move_write_spot(s,count,spot);
        bot_move_write_word(s,BM_AVOID_COUNT,bot_move_word(s,BM_AVOID_COUNT)+1u);
    }
    return true;
}
bool qa_bot_moves_avoid_spot_from(qa_bot_moves *m,uint32_t id,const qa_bot_vector_source *source,
                                  float radius,int32_t type,qa_error *e) {
    BOT_MOVE_OPERATION(m,e,avoid_spot_from(m,id,source,radius,type,e));
}
void bot_move_record_snapshot(const bot_move_record *s,qa_bot_move_state *out) {
    *out=(qa_bot_move_state){0};
    out->input.origin=bot_move_vector(s,BM_ORIGIN);
    out->input.velocity=bot_move_vector(s,BM_VELOCITY);
    out->input.view_offset=bot_move_vector(s,BM_VIEW_OFFSET);
    out->input.entity=bot_move_integer(s,BM_ENTITY);
    out->input.client=bot_move_integer(s,BM_CLIENT);
    out->input.think_time=bot_move_float(s,BM_THINK_TIME);
    out->input.presence=bot_move_word(s,BM_PRESENCE);
    out->input.view_angles=bot_move_vector(s,BM_VIEW_ANGLES);
    out->input.flags=bot_move_word(s,BM_FLAGS);
    out->area=bot_move_word(s,BM_AREA);
    out->last_area=bot_move_word(s,BM_LAST_AREA);
    out->last_goal_area=bot_move_word(s,BM_LAST_GOAL_AREA);
    out->last_reachability=bot_move_word(s,BM_LAST_REACHABILITY);
    out->last_origin=bot_move_vector(s,BM_LAST_ORIGIN);
    out->reach_area=bot_move_word(s,BM_REACH_AREA);
    out->jump_reach=bot_move_word(s,BM_JUMP_REACH);
    out->grapple_visible_time=bot_move_float(s,BM_GRAPPLE_VISIBLE_TIME);
    out->last_grapple_distance=bot_move_float(s,BM_LAST_GRAPPLE_DISTANCE);
    out->reachability_time=bot_move_float(s,BM_REACHABILITY_TIME);
    out->avoid_reachability=bot_move_word(s,BM_AVOID_REACHABILITY);
    out->avoid_time=bot_move_float(s,BM_AVOID_TIME);
    out->avoid_tries=bot_move_integer(s,BM_AVOID_TRIES);
    for(int32_t i=0;i<QA_BOT_AVOID_SPOTS;++i) out->avoid_spots[i]=bot_move_spot(s,i);
    out->avoid_count=bot_move_word(s,BM_AVOID_COUNT);
    out->walk_progress=s->walk_progress;out->walk_edge=s->walk_edge;
}
static bool capture(qa_bot_moves *m,uint32_t id,qa_bot_move_state *out,qa_error *e) {
    bot_move_record *s=bot_move_state(m,id,e);
    if(!s) return false;
    if(!out) return bot_move_fail(e,"missing move checkpoint output");
    bot_move_record_snapshot(s,out);return true;
}
bool qa_bot_moves_capture(const qa_bot_moves *owner,uint32_t id,qa_bot_move_state *out,qa_error *e) {
    qa_bot_moves *m=(qa_bot_moves *)owner;
    BOT_MOVE_OPERATION(m,e,capture(m,id,out,e));
}
void bot_move_restore_lock(qa_bot_moves *m,bool locked) { m->busy=locked; }
static bool bot_move_restore_validate(qa_bot_moves *m,uint32_t id,const qa_bot_move_state *state,qa_error *e) {
    bot_move_record *record=bot_move_state(m,id,e);qa_bot_memory_span bytes;
    if(!record || !state || !bot_move_record_span(record,&bytes,e)) return false;
    if(state->walk_progress) {
        qa_bot_navigation *n=m->services.navigation(m->services.context,state->input.client);
        if(!n || !qa_navigation_edge(qa_bot_navigation_runtime(n),state->walk_edge))
            return bot_move_fail(e,"saved movement edge is absent from selected navigation");
    }
    return true;
}
static void bot_move_restore_commit(qa_bot_moves *m,uint32_t id,const qa_bot_move_state *state) {
    bot_move_record *s=&m->slots[id-1].state;
    bot_move_route_clear(s);
    bot_move_write_vector(s,BM_ORIGIN,state->input.origin);
    bot_move_write_vector(s,BM_VELOCITY,state->input.velocity);
    bot_move_write_vector(s,BM_VIEW_OFFSET,state->input.view_offset);
    bot_move_write_word(s,BM_ENTITY,(uint32_t)state->input.entity);
    bot_move_write_word(s,BM_CLIENT,(uint32_t)state->input.client);
    bot_move_write_float(s,BM_THINK_TIME,state->input.think_time);
    bot_move_write_word(s,BM_PRESENCE,state->input.presence);
    bot_move_write_vector(s,BM_VIEW_ANGLES,state->input.view_angles);
    bot_move_write_word(s,BM_AREA,state->area);
    bot_move_write_word(s,BM_LAST_AREA,state->last_area);
    bot_move_write_word(s,BM_LAST_GOAL_AREA,state->last_goal_area);
    bot_move_set_reach(s,state->last_reachability);
    bot_move_write_vector(s,BM_LAST_ORIGIN,state->last_origin);
    bot_move_write_word(s,BM_REACH_AREA,state->reach_area);
    bot_move_write_word(s,BM_FLAGS,state->input.flags);
    bot_move_write_word(s,BM_JUMP_REACH,state->jump_reach);
    bot_move_write_float(s,BM_GRAPPLE_VISIBLE_TIME,state->grapple_visible_time);
    bot_move_write_float(s,BM_LAST_GRAPPLE_DISTANCE,state->last_grapple_distance);
    bot_move_write_float(s,BM_REACHABILITY_TIME,state->reachability_time);
    bot_move_write_word(s,BM_AVOID_REACHABILITY,state->avoid_reachability);
    bot_move_write_float(s,BM_AVOID_TIME,state->avoid_time);
    bot_move_write_word(s,BM_AVOID_TRIES,(uint32_t)state->avoid_tries);
    for(int32_t i=0;i<QA_BOT_AVOID_SPOTS;++i) bot_move_write_spot(s,i,state->avoid_spots[i]);
    bot_move_write_word(s,BM_AVOID_COUNT,state->avoid_count);
    s->walk_progress=state->walk_progress;s->walk_edge=state->walk_edge;
}
static bool restore(qa_bot_moves *m,uint32_t id,const qa_bot_move_state *state,qa_error *e) {
    if(!bot_move_mutable(m,e)) return false;
    m->busy=true;
    if(!bot_move_restore_validate(m,id,state,e)) return false;
    bot_move_restore_commit(m,id,state);return true;
}
bool qa_bot_moves_restore(qa_bot_moves *m,uint32_t id,const qa_bot_move_state *state,qa_error *e) {
    BOT_MOVE_OPERATION(m,e,restore(m,id,state,e));
}
