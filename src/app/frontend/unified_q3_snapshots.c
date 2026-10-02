/* SnapshotRuntime/cg_snapshot.c private CLIENT state. */
#include "unified_q3_snapshots.h"
#include "../../presentation/q3_native/trajectory.h"
#include "../../presentation/q3_native/entity_save.h"
#include "qa/network_q3_fields_save.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct cached_snapshot {
    qa_q3_snapshot value;
    qa_q3_entity entities[256];
    qa_actor_id bindings[QA_Q3_ENTITIES];
} cached_snapshot;
typedef struct cached_entity {
    qa_q3_entity current, next;
    qa_actor_id next_actor;
    int32_t snapshot_number;
    bool published, interpolate;
} cached_entity;
struct frontend_unified_q3_snapshots {
    frontend_unified_q3_snapshots_options options;
    cached_snapshot snap, next;
    cached_entity rows[QA_Q3_ENTITIES];
    q3n_entity entities[QA_Q3_ENTITIES];
    qa_q3_player predicted_player;
    qa_q3_entity predicted_state, predicted_next;
    q3n_entity predicted_entity;
    qa_q3_player previous_player;
    qa_vec3 correction;
    int32_t correction_time;
    uint64_t revision, scope, command_receipt;
    int32_t time, processed, commands;
    q3n_compiled_stage stage;
    bool has_snap, has_next, has_prediction, hyperspace, this_teleport, next_teleport;
    bool busy, callback, faulted, transition_alias;
};
static bool fail(qa_error *e, qa_status code, const char *message)
{ qa_error_set(e,code,0,"%s",message); return false; }
static int32_t word(uint32_t bits) { int32_t value; memcpy(&value,&bits,sizeof(value)); return value; }
static qa_vec3 vector(const float value[3]) { return qa_v3(value[0],value[1],value[2]); }
static bool frame_current(void *, const q3n_compiled_frame *);
static bool entity_read(void *, const q3n_compiled_frame *, uint32_t, q3n_compiled_entity *, qa_error *);
static bool entity_event(void *, const q3n_compiled_frame *, uint32_t, int32_t, int32_t, qa_error *);
static bool entity_trajectory(void *, const q3n_compiled_entity *, int32_t, int32_t, int32_t, int32_t, qa_error *);
static bool entity_weapon(void *, const q3n_compiled_entity *, int32_t, int32_t, qa_error *);
static bool error_clear(void *, const q3n_compiled_frame *, qa_error *);
static bool trace_number(void *, const q3n_compiled_frame *, const qa_trace_result *, int32_t *, qa_error *);
static const qa_q3_snapshot *next_view(const frontend_unified_q3_snapshots *s)
{ return s->has_next ? (s->transition_alias ? &s->snap.value : &s->next.value) : NULL; }
static bool frame(frontend_unified_q3_snapshots *s, q3n_compiled_frame *out, qa_error *e)
{
    q3n_compiled_source_view source;
    if (!q3n_compiled_source_read(frontend_unified_q3_client_source(s->options.client),&source,e)) return false;
    *out = (q3n_compiled_frame){.source=source,.owner=s,.revision=s->revision,.scope=s->callback ? s->scope : 0,
        .stage=s->callback ? s->stage : Q3N_COMPILED_COMPLETED_FRAME,.time=s->time,.processed_snapshot=s->processed,
        .reached_command=s->commands,.snapshot=s->has_snap ? &s->snap.value : NULL,.next_snapshot=next_view(s),
        .entities=s->entities,.prediction_error=s->correction,.prediction_error_time=s->correction_time,
        .hyperspace=s->hyperspace,.this_frame_teleport=s->this_teleport,.next_frame_teleport=s->next_teleport,
        .context=s,.current=frame_current,.entity=entity_read,.entity_event=entity_event,.entity_trajectory=entity_trajectory,
        .entity_weapon=entity_weapon,.prediction_error_clear=error_clear,.trace_number=trace_number};
    out->predicted_player = &s->predicted_player; out->predicted_state = &s->predicted_state;
    out->predicted_next_state = &s->predicted_next; out->predicted_entity = &s->predicted_entity;
    if (s->callback && s->stage == Q3N_COMPILED_PLAYER_TRANSITION) {
        out->transition_player = &s->snap.value.player; out->previous_player = &s->previous_player;
    }
    return q3n_compiled_frame_current(out) || fail(e,QA_ERROR_ARGUMENT,"Compiled CG frame lost its actual entered cache scope");
}
static bool frame_current(void *context, const q3n_compiled_frame *f)
{
    frontend_unified_q3_snapshots *s = context;
    return s && f && f->owner == s && !s->faulted && f->revision == s->revision &&
        f->scope == (s->callback ? s->scope : 0) &&
        f->stage == (s->callback ? s->stage : Q3N_COMPILED_COMPLETED_FRAME) &&
        (s->callback ? s->busy : !s->busy) && f->time == s->time && f->processed_snapshot == s->processed &&
        f->reached_command == s->commands && f->snapshot == (s->has_snap ? &s->snap.value : NULL) &&
        f->next_snapshot == next_view(s) && f->entities == s->entities &&
        f->predicted_player == &s->predicted_player && f->predicted_state == &s->predicted_state &&
        f->predicted_next_state == &s->predicted_next && f->predicted_entity == &s->predicted_entity &&
        f->transition_player == (s->callback && s->stage == Q3N_COMPILED_PLAYER_TRANSITION ? &s->snap.value.player : NULL) &&
        f->previous_player == (s->callback && s->stage == Q3N_COMPILED_PLAYER_TRANSITION ? &s->previous_player : NULL) &&
        frontend_unified_q3_client_current(s->options.client);
}
static bool entity_read(void *context,const q3n_compiled_frame *f,uint32_t number,q3n_compiled_entity *out,qa_error *e)
{
    frontend_unified_q3_snapshots *s = context;
    if (!out || number >= QA_Q3_ENTITY_NONE || !frame_current(s,f)) return fail(e,QA_ERROR_ARGUMENT,"Compiled centity left its actual cache");
    cached_entity *row = s->rows+number; q3n_entity *cent = s->entities+number;
    *out = (q3n_compiled_entity){.frame=f,.current=&row->current,.next=&row->next,.presentation=cent,
        .actor=cent->actor,.number=number,.snapshot_number=row->snapshot_number,.published=row->published,
        .current_valid=cent->valid,.interpolate=row->interpolate}; return true;
}
static bool entity_event(void *context,const q3n_compiled_frame *f,uint32_t number,int32_t event,int32_t parameter,qa_error *e)
{
    frontend_unified_q3_snapshots *s = context;
    if (number >= QA_Q3_ENTITY_NONE || !frame_current(s,f) || !s->rows[number].published)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled event mutation lost its genuine currentState");
    s->rows[number].current.event = event; s->rows[number].current.eventParm = parameter; return true;
}
static bool entity_trajectory(void *context,const q3n_compiled_entity *row,int32_t cb,int32_t nb,int32_t ca,int32_t na,qa_error *e)
{
    frontend_unified_q3_snapshots *s = context;
    if (!row || !frame_current(s,row->frame) || row->current->pos.type != cb || row->next->pos.type != nb || ca != 1 || na != 1)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled smoothing lost its expected real cached trajectories");
    qa_q3_entity *current = row->predicted ? &s->predicted_state : &s->rows[row->number].current;
    qa_q3_entity *next = row->predicted ? &s->predicted_next : &s->rows[row->number].next;
    current->pos.type = ca; next->pos.type = na; return true;
}
static bool entity_weapon(void *context,const q3n_compiled_entity *row,int32_t before,int32_t after,qa_error *e)
{
    frontend_unified_q3_snapshots *s = context;
    if (!row || !frame_current(s,row->frame) || row->current->weapon != before)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled weapon mutation lost its expected genuine currentState");
    qa_q3_entity *current = row->predicted ? &s->predicted_state : &s->rows[row->number].current;
    current->weapon = after; return true;
}
static bool error_clear(void *context,const q3n_compiled_frame *f,qa_error *e)
{
    frontend_unified_q3_snapshots *s = context;
    if (!frame_current(s,f)) return fail(e,QA_ERROR_ARGUMENT,"Compiled prediction error clear left its actual private owner");
    s->correction_time = 0; return true;
}
static bool trace_number(void *context,const q3n_compiled_frame *f,const qa_trace_result *hit,int32_t *number,qa_error *e)
{
    frontend_unified_q3_snapshots *s = context;
    return frame_current(s,f) && s->options.trace_number(s->options.context,f,hit,number,e) && frame_current(s,f);
}
static bool create(const frontend_unified_q3_snapshots_options *o,bool restoring,
    frontend_unified_q3_snapshots **out,qa_error *e)
{
    if (!o || !o->client || !o->context || !o->reached || !o->respawn || !o->reset_player || !o->event ||
        !o->transition_player || !o->lagometer || !o->warning || !o->trace_number || !out || *out ||
        !(restoring ? frontend_unified_q3_client_checkpoint_current(o->client) : frontend_unified_q3_client_current(o->client)))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled cache requires its real CLIENT and source consumers");
    frontend_unified_q3_snapshots *s = calloc(1,sizeof(*s));
    if (!s) return fail(e,QA_ERROR_MEMORY,"Retaining actual compiled CG centities");
    q3n_compiled_source_view source;
    bool current = restoring ? q3n_compiled_source_checkpoint_read(frontend_unified_q3_client_source(o->client),&source,e) :
        q3n_compiled_source_read(frontend_unified_q3_client_source(o->client),&source,e);
    if (!current) { free(s); return false; }
    s->options = *o; s->revision = 1;
    /* ClientGameState constructs PlayerStateRecord(product,0,0,0), before
     * CG_Init or the first snapshot event. This is private CG storage. */
    s->predicted_player.product = source.basis.product; s->previous_player.product = source.basis.product;
    s->predicted_entity.actor = source.basis.viewer;
    *out = s; return true;
}
bool frontend_unified_q3_snapshots_create(const frontend_unified_q3_snapshots_options *o,
    frontend_unified_q3_snapshots **out,qa_error *e)
{ return create(o,false,out,e); }
bool frontend_unified_q3_snapshots_idle(const frontend_unified_q3_snapshots *s) { return !s || !s->busy; }
bool frontend_unified_q3_snapshots_destroy(frontend_unified_q3_snapshots **out,qa_error *e)
{
    if (!out || !*out) return true;
    if (!frontend_unified_q3_snapshots_idle(*out)) return fail(e,QA_ERROR_ARGUMENT,"Compiled CG cache retains its actual entered callback");
    free(*out); *out = NULL; return true;
}
q3n_entity *frontend_unified_q3_snapshots_storage(frontend_unified_q3_snapshots *s) { return s ? s->entities : NULL; }
static bool begin(frontend_unified_q3_snapshots *s,q3n_compiled_stage stage,q3n_compiled_frame *out,qa_error *e)
{
    if (s->scope == UINT64_MAX) return fail(e,QA_ERROR_FORMAT,"Compiled CG callback scope is exhausted");
    s->callback = true; s->stage = stage; ++s->scope;
    if (frame(s,out,e)) return true;
    s->callback = false; return false;
}
static bool end(frontend_unified_q3_snapshots *s,const q3n_compiled_frame *f,bool ok)
{ bool current = frame_current(s,f) && q3n_compiled_source_current(&f->source); s->callback = false; return ok && current; }
bool frontend_unified_q3_snapshots_initialize(frontend_unified_q3_snapshots *s,
    bool (*initialize)(void *,const q3n_compiled_frame *,qa_error *),void *context,qa_error *e)
{
    if (!s || !initialize || s->busy || s->faulted || s->has_snap) return fail(e,QA_ERROR_ARGUMENT,"Compiled Init requires its actual pre-snapshot cache");
    s->busy = true; q3n_compiled_frame f;
    bool ok = begin(s,Q3N_COMPILED_INITIALIZATION,&f,e);
    if (ok) ok = end(s,&f,initialize(context,&f,e));
    s->busy = false;
    if (ok) ok = frontend_unified_q3_client_initialization_complete(s->options.client,e);
    return ok;
}
static void player_entity(qa_q3_player *p,qa_q3_entity *s)
{
    s->number = p->clientNum; s->eType = p->pmType == 2 || p->pmType == 5 || p->stats[0] <= -40 ? 10 : 1;
    s->pos.type = 1; memcpy(s->pos.base,p->origin,sizeof(s->pos.base)); memcpy(s->pos.delta,p->velocity,sizeof(s->pos.delta));
    s->apos.type = 1; memcpy(s->apos.base,p->viewangles,sizeof(s->apos.base)); s->angles2[1] = (float)p->movementDir;
    s->legsAnim = p->legsAnim; s->torsoAnim = p->torsoAnim; s->clientNum = p->clientNum;
    s->eFlags = p->stats[0] <= 0 ? p->eFlags|1 : p->eFlags&~1;
    if (p->externalEvent) { s->event = p->externalEvent; s->eventParm = p->externalEventParm; }
    else if (p->entityEventSequence < p->eventSequence) {
        int32_t oldest = word((uint32_t)p->eventSequence-2u);
        if (p->entityEventSequence < oldest) p->entityEventSequence = oldest;
        uint32_t sequence = (uint32_t)p->entityEventSequence;
        s->event = p->events[sequence&1u]|(int32_t)((sequence&3u)<<8); s->eventParm = p->eventParms[sequence&1u];
        p->entityEventSequence = word(sequence+1u);
    }
    s->weapon = p->weapon; s->groundEntityNum = p->groundEntityNum; s->powerups = 0;
    for (unsigned i = 0; i < 16; ++i) if (p->powerups[i]) s->powerups |= (int32_t)(1u<<i);
    s->loopSound = p->loopSound; s->generic1 = p->generic1;
}
static bool store(frontend_unified_q3_snapshots *s,cached_snapshot *out,const qa_q3_snapshot *in,qa_error *e)
{
    if (!in || !in->valid || in->entity_count > 256) return fail(e,QA_ERROR_FORMAT,"Compiled snapshot has no actual bounded Source records");
    out->value = *in; out->value.entities = out->entities;
    memcpy(out->entities,in->entities,in->entity_count*sizeof(*out->entities));
    for (uint32_t i = 0; i < QA_Q3_ENTITY_NONE; ++i) {
        bool present;
        if (!frontend_unified_q3_client_snapshot_actor(s->options.client,in->message_number,i,out->bindings+i,&present,e)) return false;
    }
    return true;
}
static void published(frontend_unified_q3_snapshots *s,uint32_t number,const cached_snapshot *snapshot)
{
    cached_entity *row = s->rows+number; q3n_entity *cent = s->entities+number;
    qa_actor_id actor = snapshot->bindings[number];
    if (!qa_actor_id_equal(cent->actor,actor)) memset(cent,0,sizeof(*cent));
    cent->actor = actor; cent->physical = number; cent->loop_stopped = false;
    row->snapshot_number = snapshot->value.message_number; row->published = true;
}
static bool commands(frontend_unified_q3_snapshots *s,int32_t target,qa_error *e)
{
    while (s->commands < target) {
        frontend_unified_q3_command command;
        if (s->commands == INT32_MAX || !frontend_unified_q3_client_command(s->options.client,s->commands+1,&command,e)) return false;
        ++s->commands; q3n_compiled_frame f;
        if (!begin(s,Q3N_COMPILED_SNAPSHOT_CALLBACK,&f,e)) return false;
        if (!end(s,&f,s->options.reached(s->options.context,&f,&command,e))) return false;
    }
    return true;
}
static bool reset(frontend_unified_q3_snapshots *s,uint32_t number,qa_error *e)
{
    cached_entity *row = s->rows+number; q3n_entity *cent = s->entities+number;
    if (cent->snapshot_time < word((uint32_t)s->time-300u)) cent->previous_event = 0;
    cent->trail_time = s->snap.value.server_time; cent->lerp_origin = vector(row->current.origin); cent->lerp_angles = vector(row->current.angles);
    if (row->current.eType != 1) return true;
    q3n_compiled_frame f; q3n_compiled_entity entity;
    if (!begin(s,Q3N_COMPILED_SNAPSHOT_CALLBACK,&f,e)) return false;
    bool ok = q3n_compiled_frame_entity(&f,number,&entity,e) && s->options.reset_player(s->options.context,&f,&entity,e);
    return end(s,&f,ok);
}
static bool event(frontend_unified_q3_snapshots *s,uint32_t number,qa_error *e)
{
    cached_entity *row = s->rows+number; q3n_entity *cent = s->entities+number; qa_q3_entity scratch = row->current;
    if (scratch.eType > 13) {
        if (cent->previous_event) return true;
        cent->previous_event = 1; if (scratch.eFlags&16) scratch.number = scratch.otherEntityNum; scratch.event = scratch.eType-13;
    } else {
        if (scratch.event == cent->previous_event) return true;
        cent->previous_event = scratch.event; if (!(scratch.event&~0x300)) return true;
    }
    if (!q3n_trajectory(&scratch.pos,s->snap.value.server_time,&cent->lerp_origin,e)) return false;
    q3n_compiled_frame f; q3n_compiled_entity entity;
    if (!begin(s,Q3N_COMPILED_SNAPSHOT_CALLBACK,&f,e)) return false;
    bool ok = q3n_compiled_frame_entity(&f,number,&entity,e) &&
        s->options.event(s->options.context,&f,&entity,&scratch,cent->lerp_origin,e);
    return end(s,&f,ok);
}
static bool initial(frontend_unified_q3_snapshots *s,qa_error *e)
{
    s->has_snap = true; uint32_t local = (uint32_t)s->snap.value.player.clientNum;
    player_entity(&s->snap.value.player,&s->rows[local].current); published(s,local,&s->snap);
    if (!commands(s,s->snap.value.server_command_number,e)) return false;
    q3n_compiled_frame f;
    if (!begin(s,Q3N_COMPILED_SNAPSHOT_CALLBACK,&f,e)) return false;
    if (!end(s,&f,s->options.respawn(s->options.context,&f,e))) return false;
    for (size_t i = 0; i < s->snap.value.entity_count; ++i) {
        const qa_q3_entity *state = s->snap.entities+i; uint32_t number = (uint32_t)state->number;
        s->rows[number].current = *state; published(s,number,&s->snap); s->entities[number].valid = true; s->rows[number].interpolate = false;
        if (!reset(s,number,e) || !event(s,number,e)) return false;
    }
    return true;
}
static void next(frontend_unified_q3_snapshots *s)
{
    s->has_next = true; uint32_t local = (uint32_t)s->next.value.player.clientNum;
    player_entity(&s->next.value.player,&s->rows[local].next); s->rows[local].next_actor = s->next.bindings[local];
    s->rows[s->snap.value.player.clientNum].interpolate = true;
    for (size_t i = 0; i < s->next.value.entity_count; ++i) {
        const qa_q3_entity *state = s->next.entities+i; uint32_t number = (uint32_t)state->number; cached_entity *row = s->rows+number;
        row->next = *state; row->next_actor = s->next.bindings[number];
        row->interpolate = s->entities[number].valid && qa_actor_id_equal(s->entities[number].actor,row->next_actor) && !((row->current.eFlags^state->eFlags)&4);
    }
    s->next_teleport = ((s->next.value.player.eFlags^s->snap.value.player.eFlags)&4) != 0 ||
        s->next.value.player.clientNum != s->snap.value.player.clientNum || ((s->next.value.flags^s->snap.value.flags)&4) != 0;
}
static bool transition(frontend_unified_q3_snapshots *s,bool no_predict,bool synchronous,qa_error *e)
{
    if (!commands(s,s->next.value.server_command_number,e)) return false;
    s->previous_player = s->snap.value.player;
    for (size_t i = 0; i < s->snap.value.entity_count; ++i) s->entities[s->snap.entities[i].number].valid = false;
    s->snap = s->next; s->snap.value.entities = s->snap.entities;
    s->transition_alias = true;
    uint32_t local = (uint32_t)s->snap.value.player.clientNum;
    player_entity(&s->snap.value.player,&s->rows[local].current); published(s,local,&s->snap); s->rows[local].interpolate = false;
    for (size_t i = 0; i < s->snap.value.entity_count; ++i) {
        uint32_t number = (uint32_t)s->snap.entities[i].number; cached_entity *row = s->rows+number;
        row->current = row->next; published(s,number,&s->snap); s->entities[number].valid = true;
        if (!row->interpolate && !reset(s,number,e)) return false;
        row->interpolate = false;
        if (!event(s,number,e)) return false;
        s->entities[number].snapshot_time = s->snap.value.server_time;
    }
    s->has_next = false; s->transition_alias = false;
    if ((s->snap.value.player.eFlags^s->previous_player.eFlags)&4) s->this_teleport = true;
    if (!(no_predict || synchronous || (s->snap.value.player.pmFlags&4096))) return true;
    q3n_compiled_frame f;
    if (!begin(s,Q3N_COMPILED_PLAYER_TRANSITION,&f,e)) return false;
    return end(s,&f,s->options.transition_player(s->options.context,&f,&s->snap.value.player,&s->previous_player,e));
}
static bool read_next(frontend_unified_q3_snapshots *s,int32_t latest,cached_snapshot *out,bool *found,qa_error *e)
{
    *found = false;
    while (s->processed < latest) {
        if (s->processed == INT32_MAX) return fail(e,QA_ERROR_FORMAT,"Compiled CG snapshot sequence is exhausted");
        const qa_q3_snapshot *snapshot = NULL;
        if (!frontend_unified_q3_client_snapshot(s->options.client,++s->processed,&snapshot,e) ||
            !s->options.lagometer(s->options.context,snapshot,e) || !frontend_unified_q3_client_current(s->options.client)) return false;
        if (snapshot) { *found = true; return store(s,out,snapshot,e); }
    }
    return true;
}
bool frontend_unified_q3_snapshots_process(frontend_unified_q3_snapshots *s,int32_t time,bool no_predict,bool synchronous,qa_error *e)
{
    if (!s || s->busy || s->faulted || s->revision == UINT64_MAX) return fail(e,QA_ERROR_ARGUMENT,"Compiled snapshots require their actual returned cache");
    int32_t latest, source_time;
    if (!frontend_unified_q3_client_latest(s->options.client,&latest,&source_time,e) || latest < s->processed) return false;
    s->busy = true; s->time = time; ++s->revision; bool ok = true;
    if ((int64_t)latest > (int64_t)s->processed+1000) {
        char message[128]; snprintf(message,sizeof(message),"WARNING: CG_ReadNextSnapshot: way out of range, %d > %d",latest,s->processed);
        ok = s->options.warning(s->options.context,message,e) && frontend_unified_q3_client_current(s->options.client);
    }
    while (ok && !s->has_snap) {
        bool found; ok = read_next(s,latest,&s->snap,&found,e);
        if (!ok || !found) break;
        if (!(s->snap.value.flags&2)) ok = initial(s,e);
    }
    while (ok && s->has_snap) {
        if (!s->has_next) {
            bool found; ok = read_next(s,latest,&s->next,&found,e);
            if (!ok || !found) break;
            next(s);
            if (s->next.value.server_time < s->snap.value.server_time) ok = fail(e,QA_ERROR_FORMAT,"CG_ProcessSnapshots: Source time went backwards");
        }
        if (!ok || (s->time >= s->snap.value.server_time && s->time < s->next.value.server_time)) break;
        ok = transition(s,no_predict,synchronous,e);
    }
    if (ok && s->has_snap && s->time < s->snap.value.server_time) s->time = s->snap.value.server_time;
    if (ok && s->has_next && s->next.value.server_time <= s->time) ok = fail(e,QA_ERROR_FORMAT,"CG_ProcessSnapshots: actual next snapshot is not in the future");
    if (ok) ok = frontend_unified_q3_client_current(s->options.client);
    s->transition_alias = false; s->faulted = !ok; s->busy = false; return ok;
}
bool frontend_unified_q3_snapshots_read(const frontend_unified_q3_snapshots *s,q3n_compiled_frame *out,qa_error *e)
{
    if (!s || !out || s->busy || s->faulted || !s->has_snap) return fail(e,QA_ERROR_ARGUMENT,"Compiled CG draw requires its genuine completed snapshot cache");
    return frame((frontend_unified_q3_snapshots *)s,out,e);
}
bool frontend_unified_q3_snapshots_prediction(frontend_unified_q3_snapshots *s,const qa_q3_player *player,
    uint64_t receipt,qa_vec3 correction,int32_t correction_time,bool hyperspace,qa_error *e)
{
    q3n_compiled_source_view source;
    if (!s || !player || s->busy || s->faulted || !s->has_snap || s->revision == UINT64_MAX || !qa_vec_finite(correction) ||
        !q3n_compiled_source_read(frontend_unified_q3_client_source(s->options.client),&source,e) ||
        player->product != source.basis.product || player->clientNum != s->snap.value.player.clientNum)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled predicted PS requires its actual merged predictor receipt");
    if (s->has_prediction && receipt < s->command_receipt) return fail(e,QA_ERROR_ARGUMENT,"Compiled prediction receipt rewound");
    if (!s->has_prediction || receipt != s->command_receipt) {
        s->predicted_player = *player; s->command_receipt = receipt; s->has_prediction = true;
        s->predicted_entity.actor = source.basis.viewer;
        s->predicted_entity.physical = (uint32_t)player->clientNum; s->predicted_entity.loop_stopped = false;
    }
    s->correction = correction; s->correction_time = correction_time; s->hyperspace = hyperspace; ++s->revision; return true;
}

static bool actor_fields(void *context,qa_source_save_io *io,qa_actor_id *actor)
{
    frontend_unified_q3_snapshots *s = context;
    return frontend_unified_q3_client_actor_fields(s->options.client,io,actor);
}
static bool record_fields(qa_source_save_io *io,void *value,bool player,qa_q3_product product)
{
    uint8_t bytes[4096]; size_t size = 0;
    bool reading = io->direction == QA_SOURCE_SAVE_READ, ok = true;
    if (!reading) {
        qa_net_writer writer; qa_net_writer_init(&writer,bytes,sizeof(bytes),io->error);
        ok = player ? qa_q3_save_player_fields(&writer,value) : qa_q3_save_entity_fields(&writer,value);
        if (ok) size = qa_net_writer_size(&writer);
    }
    if (ok) ok = qa_source_save_count(io,&size,sizeof(bytes)) && qa_source_save_bytes(io,bytes,size);
    if (ok && reading) {
        qa_net_reader reader; qa_net_reader_init(&reader,(qa_bytes){bytes,size},io->error);
        ok = (player ? qa_q3_restore_player_fields(&reader,value,product) : qa_q3_restore_entity_fields(&reader,value)) && qa_net_reader_finish(&reader);
    }
    return ok;
}
static bool snapshot_fields(frontend_unified_q3_snapshots *s,qa_source_save_io *io,cached_snapshot *row,qa_q3_product product)
{
    qa_q3_snapshot *v = &row->value;
    bool ok = qa_source_save_bool(io,&v->valid) && v->valid && qa_source_save_i32(io,&v->message_number) &&
        v->message_number > 0 && v->message_number <= s->processed && qa_source_save_i32(io,&v->server_time) &&
        qa_source_save_i32(io,&v->delta_number) && qa_source_save_i32(io,&v->server_command_number) &&
        v->server_command_number >= 0 && qa_source_save_u8(io,&v->flags) && !(v->flags&~4u) &&
        qa_source_save_u8(io,&v->area_bytes) && v->area_bytes == 32 && qa_source_save_bytes(io,v->area_mask,sizeof(v->area_mask)) &&
        record_fields(io,&v->player,true,product) && v->player.clientNum >= 0 && v->player.clientNum < 64 &&
        qa_source_save_count(io,&v->entity_count,256);
    v->entities = row->entities;
    for (size_t i = 0; ok && i < v->entity_count; ++i)
        ok = record_fields(io,row->entities+i,false,product) && row->entities[i].number >= 0 && row->entities[i].number < QA_Q3_ENTITY_NONE &&
            (!i || row->entities[i].number > row->entities[i-1].number);
    for (size_t i = 0; ok && i < QA_Q3_ENTITIES; ++i) ok = actor_fields(s,io,row->bindings+i);
    return ok;
}
static bool fields(frontend_unified_q3_snapshots *s,qa_source_save_io *io)
{
    q3n_compiled_source_view source;
    if (!q3n_compiled_source_checkpoint_read(frontend_unified_q3_client_source(s->options.client),&source,io->error)) return false;
    char magic[4] = {'Q','3','C','G'}; uint32_t version = 1;
    bool ok = qa_source_save_bytes(io,magic,4) && !memcmp(magic,"Q3CG",4) && qa_source_save_u32(io,&version) && version == 1 &&
        q3n_compiled_source_fields(io,source.owner) && qa_source_save_u64(io,&s->revision) && s->revision &&
        qa_source_save_u64(io,&s->scope) && qa_source_save_i32(io,&s->time) && qa_source_save_i32(io,&s->processed) && s->processed >= 0 &&
        qa_source_save_i32(io,&s->commands) && s->commands == source.basis.reached_command &&
        qa_source_save_bool(io,&s->has_snap) && qa_source_save_bool(io,&s->has_next) && (!s->has_next || s->has_snap) &&
        qa_source_save_bool(io,&s->has_prediction) && (!s->has_prediction || s->has_snap) &&
        qa_source_save_bool(io,&s->hyperspace) && qa_source_save_bool(io,&s->this_teleport) && qa_source_save_bool(io,&s->next_teleport) &&
        qa_source_save_vec3(io,&s->correction) && qa_vec_finite(s->correction) && qa_source_save_i32(io,&s->correction_time) &&
        qa_source_save_u64(io,&s->command_receipt);
    if (ok && s->has_snap) ok = snapshot_fields(s,io,&s->snap,source.basis.product);
    if (ok && s->has_next) ok = snapshot_fields(s,io,&s->next,source.basis.product) && s->next.value.message_number > s->snap.value.message_number &&
        s->next.value.server_time > s->time;
    for (size_t i = 0; ok && i < QA_Q3_ENTITIES; ++i) {
        cached_entity *row = s->rows+i;
        ok = record_fields(io,&row->current,false,source.basis.product) && record_fields(io,&row->next,false,source.basis.product) &&
            actor_fields(s,io,&row->next_actor) && qa_source_save_i32(io,&row->snapshot_number) && row->snapshot_number <= s->processed &&
            qa_source_save_bool(io,&row->published) && qa_source_save_bool(io,&row->interpolate) &&
            q3n_entity_codec_ref(io,s->entities+i,s,actor_fields) &&
            (!s->entities[i].valid || row->published) && (!row->published ||
                (row->current.number == (int32_t)i && s->entities[i].physical == i && s->entities[i].actor.registry));
    }
    if (ok) ok = record_fields(io,&s->predicted_player,true,source.basis.product) &&
        record_fields(io,&s->predicted_state,false,source.basis.product) && record_fields(io,&s->predicted_next,false,source.basis.product) &&
        q3n_entity_codec_ref(io,&s->predicted_entity,s,actor_fields) && qa_actor_id_equal(s->predicted_entity.actor,source.basis.viewer);
    if (ok) ok = record_fields(io,&s->previous_player,true,source.basis.product);
    return ok && q3n_compiled_source_checkpoint_current(&source);
}
bool frontend_unified_q3_snapshots_checkpoint(const frontend_unified_q3_snapshots *s,qa_buffer *out,qa_error *e)
{
    if (!s || !out || out->data || !frontend_unified_q3_snapshots_idle(s) || s->faulted ||
        !frontend_unified_q3_client_checkpoint_current(s->options.client)) return fail(e,QA_ERROR_ARGUMENT,"Compiled CG cold capture requires its actual returned cache");
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io,NULL,e) && fields((frontend_unified_q3_snapshots *)s,&io) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_unified_q3_snapshots_restore(const frontend_unified_q3_snapshots_options *options,qa_bytes bytes,
    frontend_unified_q3_snapshots **out,qa_error *e)
{
    if (!out || *out) return fail(e,QA_ERROR_ARGUMENT,"Compiled CG restore requires an empty actual child");
    frontend_unified_q3_snapshots *s = NULL; qa_source_save_io io = {0};
    bool ok = create(options,true,&s,e) && qa_source_save_reader(&io,NULL,bytes,e) && fields(s,&io) && io.offset == io.input.size;
    if (ok) *out = s; else frontend_unified_q3_snapshots_destroy(&s,NULL);
    qa_source_save_dispose(&io); return ok || (e && e->code ? false : fail(e,QA_ERROR_FORMAT,"Compiled CG cold cache is inconsistent"));
}
