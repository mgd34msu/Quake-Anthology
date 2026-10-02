#include "internal.h"
#include "../checkpoint_internal.h"
#include <stdio.h>

bool bot_move_record_span(const bot_move_record *record, qa_bot_memory_span *out, qa_error *e) {
    if (!record || !record->owner ||
        !qa_bot_memory_bytes(record->owner->memory, record->allocation, out, e)) return false;
    return out->size == BOT_MOVE_STATE_BYTES ? true :
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
    return qa_v3(bot_move_float(r,field), bot_move_float(r,(bot_move_field)(field+4)),
                 bot_move_float(r,(bot_move_field)(field+8)));
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
qa_bot_avoid_spot bot_move_spot(const bot_move_record *r,int32_t index) {
    if(index<0 || index>=QA_BOT_AVOID_SPOTS) {
        bot_move_fail(r->owner->scope->error,"MovementState avoid spot index is outside its source array");
        longjmp(r->owner->scope->jump,1);
    }
    bot_move_field at=(bot_move_field)(BM_AVOID_SPOTS+index*20);
    return (qa_bot_avoid_spot){bot_move_vector(r,at),bot_move_float(r,(bot_move_field)(at+12)),
                             bot_move_integer(r,(bot_move_field)(at+16))};
}
void bot_move_write_spot(bot_move_record *r,int32_t index,qa_bot_avoid_spot spot) {
    (void)bot_move_spot(r,index);
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
    if (!maximum || maximum > BOT_MOVE_MAX_STATES || !library || !actions ||
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
void qa_bot_moves_destroy(qa_bot_moves *m) {
    if (!m || m->busy)
        return;
    if (!qa_bot_moves_shutdown(m,NULL) || !qa_bot_memory_release(m->memory,NULL)) return;
    qa_nav_prediction_result_free(&m->prediction);
    qa_nav_route_free(&m->trajectory);
    qa_nav_workspace_destroy(m->workspace);
    free(m->candidates);
    free(m->points);
    free(m->visited);
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
bool qa_bot_moves_initialize_from(qa_bot_moves *m, uint32_t id,
                                  const qa_bot_move_init_source *source, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    qa_bot_move_state *s = bot_move_source_state(m, id);
    if (!s)
        return true;
    if (!source || (!source->value &&
        (!source->integer || !source->vector || !source->think_time)))
        return bot_move_fail(e, "missing bot movement input reader");
    const qa_bot_move_input *input = source->value;
    if (input && (!qa_vec_finite(input->origin) || !qa_vec_finite(input->velocity) ||
        !qa_vec_finite(input->view_offset) || !qa_vec_finite(input->view_angles) ||
        !isfinite(input->think_time)))
        return bot_move_fail(e, "invalid bot movement input");
    m->busy = true;
    int32_t word;
    bool ok = input_integer(source, QA_BOT_INIT_FLAGS, &word, e);
    if (!ok) goto done;
    if ((uint32_t)word & QA_BOT_MOVE_TELEPORTED)
        s->walk_progress = false;
    if (!(ok = input_vector(source, QA_BOT_INIT_ORIGIN, &s->input.origin, e)) ||
        !(ok = input_vector(source, QA_BOT_INIT_VELOCITY, &s->input.velocity, e)) ||
        !(ok = input_vector(source, QA_BOT_INIT_VIEW_OFFSET, &s->input.view_offset, e)) ||
        !(ok = input_integer(source, QA_BOT_INIT_ENTITY, &s->input.entity, e)) ||
        !(ok = input_integer(source, QA_BOT_INIT_CLIENT, &s->input.client, e))) goto done;
    if (input) s->input.think_time = input->think_time;
    else if (!(ok = source->think_time(source->context, &s->input.think_time, e))) goto done;
    if (!(ok = input_integer(source, QA_BOT_INIT_PRESENCE, &word, e))) goto done;
    s->input.presence = (uint32_t)word;
    if (!(ok = input_vector(source, QA_BOT_INIT_VIEW_ANGLES, &s->input.view_angles, e)) ||
        !(ok = input_integer(source, QA_BOT_INIT_FLAGS, &word, e))) goto done;
    uint32_t mask = QA_BOT_MOVE_ON_GROUND | QA_BOT_MOVE_TELEPORTED | QA_BOT_MOVE_WATER_JUMP |
                    QA_BOT_MOVE_WALK | QA_BOT_MOVE_GRAPPLE_PULL;
    s->input.flags = (s->input.flags & ~mask) | ((uint32_t)word & mask);
done:
    m->busy = false;
    return ok;
}
bool qa_bot_moves_reset(qa_bot_moves *m, uint32_t id, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    qa_bot_move_state *s = bot_move_source_state(m, id);
    if (!s)
        return true;
    *s = (qa_bot_move_state){0};
    return true;
}
bool qa_bot_moves_reset_avoid(qa_bot_moves *m, uint32_t id, bool last, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    qa_bot_move_state *s = bot_move_source_state(m, id);
    if (!s)
        return true;
    if (!last) {
        s->avoid_reachability = 0;
        s->avoid_time = 0;
        s->avoid_tries = 0;
    } else if (s->avoid_time > 0) {
        s->avoid_time = 0;
        /* The donor defines the release32 probe into the first spot's x word.
         * Express that value deliberately without an out-of-bounds C access. */
        int32_t probe;
        memcpy(&probe, &s->avoid_spots[0].origin.x, sizeof(probe));
        if (probe > 0) {
            uint32_t word = (uint32_t)s->avoid_tries - 1;
            memcpy(&s->avoid_tries, &word, sizeof(word));
        }
    }
    return true;
}
void bot_move_avoid(qa_bot_moves *m, qa_bot_move_state *s, uint32_t reach, float duration) {
    if (s->avoid_reachability == reach) {
        uint32_t tries = s->avoid_time > m->time ? (uint32_t)s->avoid_tries + 1 : 1;
        memcpy(&s->avoid_tries, &tries, sizeof(tries));
        s->avoid_time = m->time + duration;
    } else if (s->avoid_time < m->time) {
        s->avoid_reachability = reach;
        s->avoid_time = m->time + duration;
        s->avoid_tries = 1;
    }
}
void bot_move_set_reach(qa_bot_move_state *s, uint32_t reach) {
    if (s->last_reachability != reach)
        s->walk_progress = false;
    s->last_reachability = reach;
}
bool qa_bot_moves_avoid_spot(qa_bot_moves *m, uint32_t id, const qa_bot_avoid_spot *spot,
                             qa_error *e) {
    if (!spot || !qa_vec_finite(spot->origin) || !isfinite(spot->radius))
        return bot_move_fail(e, "invalid bot avoid spot");
    qa_bot_vector_source source = {.value = &spot->origin};
    return qa_bot_moves_avoid_spot_from(m, id, &source, spot->radius, spot->type, e);
}
bool qa_bot_moves_avoid_spot_from(qa_bot_moves *m, uint32_t id,
                                  const qa_bot_vector_source *source, float radius,
                                  int32_t type, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    qa_bot_move_state *s = bot_move_source_state(m, id);
    if (!s)
        return true;
    if (!source || (!source->value && !source->read))
        return bot_move_fail(e, "missing bot avoid-spot origin fields");
    if (!type)
        s->avoid_count = 0;
    else if (s->avoid_count < QA_BOT_AVOID_SPOTS) {
        m->busy = true;
        qa_bot_avoid_spot spot = {.radius = radius, .type = type};
        bool ok = qa_bot_vector_read(source, &spot.origin, e);
        if (ok) s->avoid_spots[s->avoid_count++] = spot;
        m->busy = false;
        return ok;
    }
    return true;
}
bool qa_bot_moves_capture(const qa_bot_moves *m, uint32_t id, qa_bot_move_state *out, qa_error *e) {
    qa_bot_move_state *s = bot_move_state(m, id, e);
    if (!s)
        return false;
    if (!out)
        return bot_move_fail(e, "missing move checkpoint output");
    *out = *s;
    return true;
}
bool qa_bot_moves_restore(qa_bot_moves *m, uint32_t id, const qa_bot_move_state *state,
                          qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    m->busy=true;
    bool ok=bot_move_restore_validate(m,id,state,e);
    if (ok) bot_move_restore_commit(m,id,state);
    m->busy=false;
    return ok;
}
void bot_move_restore_lock(qa_bot_moves *m, bool locked) { m->busy=locked; }
bool bot_move_restore_validate(qa_bot_moves *m, uint32_t id, const qa_bot_move_state *state,
                                qa_error *e) {
    if (!id || id > m->maximum || !state || state->avoid_count > QA_BOT_AVOID_SPOTS ||
        !qa_vec_finite(state->input.origin) || !qa_vec_finite(state->input.velocity) ||
        !qa_vec_finite(state->input.view_offset) || !qa_vec_finite(state->input.view_angles) ||
        !qa_vec_finite(state->last_origin) || !isfinite(state->input.think_time) ||
        !isfinite(state->grapple_visible_time) || !isfinite(state->last_grapple_distance) ||
        !isfinite(state->reachability_time) || !isfinite(state->avoid_time))
        return bot_move_fail(e, "invalid bot movement checkpoint");
    for (size_t i = 0; i < state->avoid_count; ++i)
        if (!qa_vec_finite(state->avoid_spots[i].origin) || !isfinite(state->avoid_spots[i].radius))
            return bot_move_fail(e, "invalid saved avoid spot");
    if (state->walk_progress) {
        qa_bot_navigation *n = m->services.navigation(m->services.context, state->input.client);
        if (!n || !qa_navigation_edge(qa_bot_navigation_runtime(n), state->walk_edge))
            return bot_move_fail(e, "saved movement edge is absent from selected navigation");
    }
    return true;
}
void bot_move_restore_commit(qa_bot_moves *m, uint32_t id, const qa_bot_move_state *state) {
    m->slots[id-1]=(bot_move_slot){.used=true,.state=*state};
}
