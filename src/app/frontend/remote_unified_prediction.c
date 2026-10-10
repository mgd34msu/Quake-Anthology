#include "remote_unified_prediction_private.h"
#include "remote_unified_private.h"
#include "qa/text.h"
#include "remote_unified_save.h"
#include "qa/unified_frame_prediction.h"
#include "../../world/entity_internal.h"
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *e, qa_status code, const char *message)
{ return frontend_unified_fail(e,code,message); }
qa_trace_scratch *frontend_remote_unified_prediction_scratch(const frontend_remote_unified_prediction *p)
{ return p?qa_world_trace_scratch(p->scene,p->geometry):NULL; }
static bool actor_read(const frontend_remote_unified_prediction *p,const qa_unified_frame *frame,
    qa_actor_id source,qa_actor_id *out,qa_error *e)
{ return frontend_remote_unified_source_actor(p->replica,frame,source,p->importing,out,e); }
static bool ground_read(const frontend_remote_unified_prediction *p,const qa_unified_frame *frame,qa_movement_ground *ground,qa_error *e)
{ return ground->hit!=QA_TRACE_HIT_ACTOR || actor_read(p,frame,ground->actor,&ground->actor,e); }
static bool state_actors_read(const frontend_remote_unified_prediction *p,const qa_unified_frame *frame,qa_movement_state *state,qa_error *e)
{
    switch(state->kind){
    case QA_RULESET_NETQUAKE:return ground_read(p,frame,&state->data.nq.ground,e);
    case QA_RULESET_QUAKEWORLD:return ground_read(p,frame,&state->data.qw.ground,e);
    case QA_RULESET_Q2_CLASSIC:case QA_RULESET_Q2_RERELEASE:return true;
    case QA_RULESET_Q3:return ground_read(p,frame,&state->data.q3.ground,e) && actor_read(p,frame,state->data.q3.jump_pad,&state->data.q3.jump_pad,e);
    }
    return false;
}
static bool profile_read(const frontend_remote_unified_prediction *p,const qa_unified_frame_prediction *received,
    int *rounding,qa_error *e)
{
    const qa_recipe_provider *provider=p->importing ? frontend_remote_unified_provider_published(p->replica,QA_ROLE_MOVEMENT,"") :
        frontend_remote_unified_provider(p->replica,QA_ROLE_MOVEMENT,"");
    if (!provider || !received->profile_id || strcmp(qa_strings_cstr(p->replica->strings, received->profile_id),provider->selection.instance))
        return fail(e,QA_ERROR_FORMAT,"Prediction profile differs from its actual received player movement provider");
    const qa_clock_config *clock=&provider->selection.clock,*actual=&received->clock;
    if (actual->kind!=clock->kind || actual->interval_ns!=clock->interval_ns ||
        actual->minimum_frame_ns!=clock->minimum_frame_ns || actual->maximum_frame_ns!=clock->maximum_frame_ns)
        return fail(e,QA_ERROR_FORMAT,"Prediction clock differs from its admitted provider clock");
    const qa_movement_numeric *numeric=&received->numeric;
    if (!numeric->native_c || numeric->radix!=FLT_RADIX || numeric->scalar_mantissa_bits!=FLT_MANT_DIG ||
        numeric->double_mantissa_bits!=DBL_MANT_DIG || numeric->evaluation_method!=FLT_EVAL_METHOD ||
        numeric->qw_origin_binary64!=(received->profile.kind==QA_RULESET_QUAKEWORLD))
        return fail(e,QA_ERROR_UNSUPPORTED,"Received movement arithmetic differs from the actual native kernel");
    if ((int)numeric->rounding!=fegetround()) return fail(e,QA_ERROR_UNSUPPORTED,"Private prediction has a different rounding environment");
    *rounding=numeric->rounding; return true;
}
static bool scene_prepare(frontend_remote_unified_prediction *p,const qa_unified_frame *frame,qa_error *e)
{
    for(size_t i=0;i<p->pending_count;++i) p->pending_used[p->pending_rows[i].body.actor.slot]=false;
    p->pending_count=0;
    const qa_unified_world_frame *received=frame->world;
    if(received->collision_count>qa_actors_capacity(p->registry))
        return fail(e,QA_ERROR_FORMAT,"Prediction collision rows exceed the actor capacity");
    for (size_t i=0;i<received->collision_count;++i) {
        const qa_spatial_actor *row=received->collisions+i;
        qa_actor_id id; qa_body_state state=row->body.state; qa_actor_collision collision=row->collision;
        if (!actor_read(p,frame,row->body.actor,&id,e) || !id.registry ||
            !(p->importing ? frontend_remote_unified_actor_published(p->replica,row->body.actor.slot,row->body.actor.generation) :
                frontend_remote_unified_actor_present(p->replica,row->body.actor.slot,row->body.actor.generation)) ||
            !(state.ground.kind != QA_ACTOR_REFERENCE_LIFETIME || actor_read(p,frame,state.ground.value.actor,&state.ground.value.actor,e)) ||
            !(collision.owner.kind != QA_ACTOR_REFERENCE_LIFETIME || actor_read(p,frame,collision.owner.value.actor,&collision.owner.value.actor,e))) return false;
        if(p->pending_used[id.slot]) return fail(e,QA_ERROR_ARGUMENT,"Actor already has a body");
        p->pending_used[id.slot]=true;
        p->pending_rows[p->pending_count++]=(qa_spatial_actor){
            .body={id,state,row->body.absolute_bounds,row->body.link_count},.collision=collision};
    }
    return qa_world_collision_rows_validate(p->scene,p->pending_rows,p->pending_count,e);
}
static bool scene_publish(frontend_remote_unified_prediction *p,qa_error *e)
{
    qa_world_reset_bodies(p->scene);
    for(size_t i=0;i<p->pending_count;++i) {
        const qa_spatial_actor *row=p->pending_rows+i;
        qa_body_link_state link={.linked=true,.state=row->body.state,
            .absolute_bounds=row->body.absolute_bounds,.link_count=row->body.link_count};
        if(!qa_world_body_create(p->scene,row->body.actor,&row->body.state,e)
            || !qa_world_set_collision(p->scene,row->body.actor,&row->collision,e)
            || !qa_world_restore_link_state(p->scene,row->body.actor,&link,e)) {
            qa_world_reset_bodies(p->scene);
            qa_unified_document_destroy(p->snapshot_document);
            p->snapshot_document=NULL;
            p->received=false;
            return false;
        }
    }
    return true;
}
static bool current(const frontend_remote_unified_prediction *p, qa_error *e)
{
    if(!p || !(p->importing ? frontend_remote_unified_checkpoint_current(p->replica,e) :
        frontend_remote_unified_current(p->replica,e)) || frontend_remote_unified_recipe(p->replica)!=p->recipe ||
        frontend_remote_unified_registry(p->replica)!=p->registry || frontend_remote_unified_epoch(p->replica)!=p->epoch ||
        qa_executable_recipe_geometry(p->recipe)!=p->geometry)
        return fail(e,QA_ERROR_ARGUMENT,"Private prediction changed its actual replica, recipe, registry or epoch");
    if(p->received && fegetround()!=p->rounding) return fail(e,QA_ERROR_UNSUPPORTED,"Private prediction changed its admitted rounding environment");
    return true;
}
static bool create(frontend_remote_unified *replica,bool importing,
    frontend_remote_unified_prediction **out, qa_error *e)
{
    if(!replica || !out || *out || !(importing ? frontend_remote_unified_checkpoint_current(replica,e) :
        frontend_remote_unified_current(replica,e))) return false;
    qa_executable_recipe *recipe=frontend_remote_unified_recipe(replica);
    qa_actor_registry *registry=frontend_remote_unified_registry(replica);
    qa_collision_geometry *geometry=qa_executable_recipe_geometry(recipe);
    if(!recipe || !registry || !geometry) return fail(e,QA_ERROR_ARGUMENT,"Private prediction requires actual received map geometry and identities");
    frontend_remote_unified_prediction *p=calloc(1,sizeof(*p));
    if(!p) return fail(e,QA_ERROR_MEMORY,"Allocating private unified prediction owner");
    p->replica=replica; p->recipe=recipe; p->registry=registry; p->geometry=geometry;
    p->epoch=frontend_remote_unified_epoch(replica); p->discarded=-1;p->importing=importing;
    uint32_t capacity=qa_actors_capacity(registry);
    p->pending_rows=calloc(capacity,sizeof(*p->pending_rows));
    p->pending_used=calloc(capacity,sizeof(*p->pending_used));
    if(!p->pending_rows || !p->pending_used) {
        free(p->pending_rows); free(p->pending_used); free(p);
        return fail(e,QA_ERROR_MEMORY,"Allocating private prediction collision rows");
    }
    if(!qa_world_create(registry,geometry,NULL,1,&p->scene,e)) {
        free(p->pending_rows); free(p->pending_used); free(p); return false;
    }
    *out=p; return true;
}
bool frontend_remote_unified_prediction_create(frontend_remote_unified *replica,
    frontend_remote_unified_prediction **out,qa_error *e)
{ return create(replica,false,out,e); }
bool frontend_prediction_import_create(frontend_remote_unified *replica,
    frontend_remote_unified_prediction **out,qa_error *e)
{ return frontend_remote_unified_restore_pending(replica) && create(replica,true,out,e); }
bool frontend_remote_unified_prediction_receive(frontend_remote_unified_prediction *p,
    const qa_unified_document *document, qa_error *e)
{
    const qa_unified_frame *frame=qa_unified_document_frame(document);
    if(!p || p->busy || !frame || !frame->world || !frame->prediction || frame->epoch!=p->epoch || !current(p,e)) return false;
    p->busy=true;
    const qa_unified_frame_prediction *received=frame->prediction;
    prediction_snapshot s={.angles=received->view_angles,.offset=received->view_offset,
        .pml=received->rerelease_origin,.ground=received->ground,.height=received->view_height,
        .water_level=received->water_level,.water_type=received->water_type,
        .time_ms=received->command_time_ms,.sequence=received->sequence};
    s.input.state=received->state; s.input.profile=received->profile; s.input.environment=received->environment;
    s.input.standing=received->standing; s.input.crouched=received->crouched; s.input.dead=received->dead;
    s.input.invulnerability_bounds=received->invulnerability_bounds; s.input.current_bounds=received->bounds;
    qa_actor_id admitted; uint32_t source_entity; int rounding=0;
    qa_unified_document *copy=NULL;
    uint64_t authoritative_frame=frame->world->source.number;
    bool ok=frontend_remote_unified_player(p->replica,&admitted,&source_entity) && actor_read(p,frame,received->actor,&s.input.actor,e) &&
        qa_actor_id_equal(admitted,s.input.actor) && s.input.profile.kind==s.input.state.kind &&
        profile_read(p,received,&rounding,e) && state_actors_read(p,frame,&s.input.state,e) && ground_read(p,frame,&s.ground,e) &&
        scene_prepare(p,frame,e) &&
        qa_unified_document_retain(document,&copy,e) && current(p,e);
    if(ok && p->received && (s.sequence<p->snapshot.sequence || s.input.state.kind!=p->snapshot.input.state.kind))
        ok=fail(e,QA_ERROR_FORMAT,"Prediction snapshot rewinds its acknowledgement or changes movement family");
    if(ok) ok=scene_publish(p,e);
    if(ok) {
        qa_unified_document_destroy(p->snapshot_document);
        s.input.command.kind=s.input.state.kind; s.input.prediction=true; s.input.has_current_bounds=true;
        s.input.shape=(qa_trace_shape){QA_SHAPE_BOX,s.input.standing.bounds}; s.input.q1_solid=QA_Q1_SOLID_SLIDEBOX;
        s.input.view_offset=s.offset;
        p->snapshot_document=copy; copy=NULL; p->snapshot=s; p->received=true; p->rounding=rounding;
        p->authoritative_frame=authoritative_frame;
        size_t retired=0;
        while(retired<p->command_count && (int64_t)p->commands[retired].sequence<=s.sequence) ++retired;
        memmove(p->commands,p->commands+retired,(p->command_count-retired)*sizeof(*p->commands)); p->command_count-=retired;
    }
    qa_unified_document_destroy(copy);
    p->busy=false;
    if (!ok && (!e || e->code==QA_OK))
        fail(e,QA_ERROR_FORMAT,"Unified prediction does not match its actual admitted player snapshot");
    return ok;
}
static bool same_number(double a, double b)
{ return a==b && (a!=0 || signbit(a)==signbit(b)); }
static bool same_command(const qa_unified_movement *a, const qa_unified_movement *b)
{
    if(a->kind!=b->kind) return false;
#define EQ(field) same_number(a->data.field,b->data.field)
    switch(a->kind) {
    case QA_RULESET_NETQUAKE:
        return EQ(nq.acknowledged_seconds) && EQ(nq.angles.x) && EQ(nq.angles.y) && EQ(nq.angles.z) &&
            EQ(nq.forward) && EQ(nq.side) && EQ(nq.up) && EQ(nq.buttons) && EQ(nq.impulse);
    case QA_RULESET_QUAKEWORLD:
        return EQ(qw.milliseconds) && EQ(qw.angles.x) && EQ(qw.angles.y) && EQ(qw.angles.z) &&
            EQ(qw.forward) && EQ(qw.side) && EQ(qw.up) && EQ(qw.buttons) && EQ(qw.impulse);
    case QA_RULESET_Q2_CLASSIC:
        return EQ(q2.milliseconds) && EQ(q2.angle_shorts[0]) && EQ(q2.angle_shorts[1]) && EQ(q2.angle_shorts[2]) &&
            EQ(q2.forward) && EQ(q2.side) && EQ(q2.up) && EQ(q2.buttons) && EQ(q2.impulse) && EQ(q2.light_level);
    case QA_RULESET_Q2_RERELEASE:
        return EQ(q2r.milliseconds) && EQ(q2r.angles.x) && EQ(q2r.angles.y) && EQ(q2r.angles.z) &&
            EQ(q2r.forward) && EQ(q2r.side) && EQ(q2r.buttons) && EQ(q2r.server_frame);
    case QA_RULESET_Q3:
        return EQ(q3.server_time_ms) && EQ(q3.angle_words[0]) && EQ(q3.angle_words[1]) && EQ(q3.angle_words[2]) &&
            EQ(q3.buttons) && EQ(q3.weapon) && EQ(q3.forward) && EQ(q3.right) && EQ(q3.up);
    }
#undef EQ
    return false;
}
bool frontend_remote_unified_prediction_input(frontend_remote_unified_prediction *p,
    const qa_unified_input *input, double time_ms, qa_error *e)
{
    if(!p || p->busy || !p->received || !input || !isfinite(time_ms) || input->sequence>QA_UNIFIED_SAFE_INTEGER ||
        input->command.kind!=p->snapshot.input.state.kind || !current(p,e)) return false;
    qa_usercmd probe;
    if(!qa_application_control_project_unified(&input->command,&p->snapshot.input.state,input->sequence,&probe,e)) return false;
    for(size_t i=0;i<p->command_count;++i) if(input->sequence==p->commands[i].sequence)
        return (same_number(time_ms,p->commands[i].time_ms) && same_command(&input->command,&p->commands[i].raw)) ||
            fail(e,QA_ERROR_ARGUMENT,"Prediction retry changes its retained command or source time");
    if((int64_t)input->sequence<=p->snapshot.sequence || (p->command_count && input->sequence<=p->commands[p->command_count-1].sequence)) return true;
    if(p->command_count==64) {
        p->discarded=(int64_t)p->commands[0].sequence;
        memmove(p->commands,p->commands+1,63*sizeof(*p->commands)); --p->command_count;
    }
    p->commands[p->command_count++]=(prediction_command){input->command,input->sequence,time_ms};
    return true;
}
static bool trace(void *context, const qa_trace_query *q, qa_trace_result *out, qa_error *e)
{ return qa_world_trace(((frontend_remote_unified_prediction *)context)->scene,q,out,e); }
static bool contents(void *context, const qa_point_query *q, qa_point_contents *out, qa_error *e)
{ return qa_world_point_contents(((frontend_remote_unified_prediction *)context)->scene,q,out,e); }
static bool firing(void *context, const qa_movement_call *call)
{ (void)context; return (call->command->buttons&1u)!=0 && call->environment->health>0; }
static bool brush(void *context, const qa_trace_result *hit, bool *out, qa_error *e)
{
    frontend_remote_unified_prediction *p=context;
    if(hit->hit==QA_TRACE_HIT_WORLD) { *out=true; return true; }
    if(hit->hit==QA_TRACE_HIT_NONE) { *out=false; return true; }
    qa_actor_collision c;
    if(!qa_world_get_collision(p->scene,hit->actor,&c,e)) return false;
    *out=c.inline_model; return true;
}
static bool replay(frontend_remote_unified_prediction *p, prediction_snapshot *s,
    frontend_unified_prediction_status *status,const int32_t *prior_command_time,bool *matched,qa_error *e)
{
    if(matched) *matched=false;
    *s=p->snapshot;
    *status=FRONTEND_UNIFIED_PREDICTION_UNCHANGED;
    bool disabled=(s->input.state.kind==QA_RULESET_Q2_CLASSIC && (s->input.state.data.q2.flags&64u)) ||
        (s->input.state.kind==QA_RULESET_Q2_RERELEASE && (s->input.state.data.q2r.flags&64u));
    if(disabled) {
        *status=FRONTEND_UNIFIED_PREDICTION_DISABLED;
        if(p->command_count) {
            qa_usercmd command;
            const prediction_command *last=p->commands+p->command_count-1;
            if(!qa_application_control_project_unified(&last->raw,&s->input.state,last->sequence,&command,e)) return false;
            qa_input_command_basis from={.kind=command.kind,.relative=true}, to={.kind=command.kind};
            if(command.kind==QA_RULESET_Q2_CLASSIC) {
                from.words=from.signed_shorts=true;
                for(unsigned i=0;i<3;++i) from.delta_words[i]=s->input.state.data.q2.delta_angle_shorts[i];
            } else from.delta_angles=s->input.state.data.q2r.delta_angles;
            qa_usercmd absolute;
            qa_input_command_convert(&command,NULL,&from,&to,(qa_input_axis_rule){0},&absolute);
            s->angles=absolute.angles;
        }
        return true;
    }
    uint64_t last=p->command_count?p->commands[p->command_count-1].sequence:0;
    if(p->discarded>s->sequence || (p->command_count && (double)last-(double)s->sequence>=63)) { *status=FRONTEND_UNIFIED_PREDICTION_EXHAUSTED; return true; }
    qa_movement_services services={.context=p,.trace=trace,.point_contents=contents,.firing=firing,.is_bsp=brush};
    qa_movement_result result={0};
    bool ok=true;
    for(size_t i=0;ok && i<p->command_count;++i) {
        const prediction_command *entry=p->commands+i;
        qa_movement_input in=s->input;
        in.view_offset=s->offset; in.q2r_pml_origin=&s->pml; in.snap_initial=i==0;
        ok=qa_application_control_project_unified(&entry->raw,&in.state,entry->sequence,&in.command,e);
        if(!ok) break;
        double duration=in.state.kind==QA_RULESET_NETQUAKE || in.state.kind==QA_RULESET_Q3?fmax(0,entry->time_ms-s->time_ms):in.command.milliseconds;
        if(!isfinite(duration) || duration>=(double)UINT64_MAX/1000000.0 || entry->time_ms>=(double)UINT64_MAX/1000000.0)
            { ok=fail(e,QA_ERROR_FORMAT,"Prediction clock exceeds its native duration domain"); break; }
        in.elapsed_ns=(uint64_t)(duration*1000000.0);
        in.time_ns=entry->time_ms<=0?0:(uint64_t)(entry->time_ms*1000000.0);
        in.has_source_seconds=in.state.kind==QA_RULESET_NETQUAKE; in.source_seconds=entry->time_ms/1000;
        if(in.state.kind==QA_RULESET_Q2_CLASSIC) in.profile.data.q2.snap_initial=false;
        if(in.state.kind==QA_RULESET_Q3) in.environment.gravity_multiplier=1;
        if(in.state.kind==QA_RULESET_QUAKEWORLD && in.environment.has_stance) { in.profile.data.qw.shared_controls=true; in.shape.bounds=in.current_bounds; }
        bool boundary=prior_command_time&&in.state.kind==QA_RULESET_Q3&&
            in.state.data.q3.command_time_ms==*prior_command_time&&
            in.command.server_time_ms>in.state.data.q3.command_time_ms;
        ok=qa_movement_move(&in,&services,&result,e);
        if(ok && result.status!=QA_MOVEMENT_ACTIVE) ok=fail(e,QA_ERROR_FORMAT,"Private movement cannot retire an authoritative actor");
        if(ok) {
            if(boundary&&result.state.kind==QA_RULESET_Q3&&
                result.state.data.q3.command_time_ms!=in.state.data.q3.command_time_ms&&matched) *matched=true;
            s->input.state=result.state; s->input.current_bounds=result.bounds;
            s->angles=result.view_angles; s->height=result.view_height;
            s->ground=result.ground; s->water_level=result.water_level; s->water_type=result.water_type;
            s->sequence=(int64_t)entry->sequence; s->time_ms=entry->time_ms;
            *status=FRONTEND_UNIFIED_PREDICTION_ACTIVE;
        }
    }
    qa_movement_result_free(&result);
    return ok;
}
static bool read_prediction(frontend_remote_unified_prediction *p,frontend_unified_prediction_view *out,
    const int32_t *prior_command_time,bool *matched,qa_error *e)
{
    if(!p || p->busy || !p->received || !out || !current(p,e)) return false;
    p->busy=true;
    prediction_snapshot s; frontend_unified_prediction_status status;
    bool ok=replay(p,&s,&status,prior_command_time,matched,e) && current(p,e);
    if(ok) *out=(frontend_unified_prediction_view){.actor=s.input.actor,.state=s.input.state,
        .origin_shift=qa_vec_sub(qa_movement_origin(&s.input.state),qa_movement_origin(&p->snapshot.input.state)),
        .view_angles=s.angles,.view_offset=s.offset,.bounds=s.input.current_bounds,.ground=s.ground,.view_height=s.height,
        .command_time_ms=s.time_ms,.sequence=s.sequence,.authoritative_frame=p->authoritative_frame,.status=status};
    p->busy=false; return ok;
}
bool frontend_remote_unified_prediction_read(frontend_remote_unified_prediction *p,
    frontend_unified_prediction_view *out,qa_error *e)
{ return read_prediction(p,out,NULL,NULL,e); }
bool frontend_remote_unified_prediction_read_command_boundary(frontend_remote_unified_prediction *p,
    int32_t prior_command_time,frontend_unified_prediction_view *out,bool *matched,qa_error *e)
{
    if(!matched) return fail(e,QA_ERROR_ARGUMENT,"Q3 replay boundary requires its output receipt");
    bool actual=false;
    if(!read_prediction(p,out,&prior_command_time,&actual,e)) return false;
    *matched=actual; return true;
}
bool frontend_remote_unified_prediction_time(const frontend_remote_unified_prediction *p, double *out, qa_error *e)
{
    if(!p || p->busy || !p->received || !out || !current(p,e)) return false;
    *out=p->command_count?fmax(p->snapshot.time_ms,p->commands[p->command_count-1].time_ms):p->snapshot.time_ms;
    return true;
}
static void snapshot_read(const frontend_remote_unified_prediction *p,frontend_unified_prediction_view *out)
{
    const prediction_snapshot *s=&p->snapshot;
    *out=(frontend_unified_prediction_view){.actor=s->input.actor,.state=s->input.state,
        .view_angles=s->angles,.view_offset=s->offset,.bounds=s->input.current_bounds,.ground=s->ground,
        .view_height=s->height,.command_time_ms=s->time_ms,.sequence=s->sequence,.authoritative_frame=p->authoritative_frame,
        .status=FRONTEND_UNIFIED_PREDICTION_UNCHANGED};
}
bool frontend_remote_unified_prediction_snapshot(const frontend_remote_unified_prediction *p,
    frontend_unified_prediction_view *out, qa_error *e)
{
    if(!p || p->busy || !p->received || !out || !current(p,e)) return false;
    snapshot_read(p,out);return true;
}
bool frontend_prediction_checkpoint_snapshot(const frontend_remote_unified_prediction *p,
    frontend_unified_prediction_view *out,qa_error *e)
{
    if(!p || p->busy || p->importing || !p->received || !out ||
        !frontend_remote_unified_checkpoint_current(p->replica,e) ||
        p->recipe!=frontend_remote_unified_recipe(p->replica) ||
        p->registry!=frontend_remote_unified_registry(p->replica) ||
        p->epoch!=frontend_remote_unified_epoch(p->replica) ||
        p->geometry!=qa_executable_recipe_geometry(p->recipe)) return false;
    snapshot_read(p,out);return true;
}
bool frontend_remote_unified_prediction_idle(const frontend_remote_unified_prediction *p)
{ return p && !p->busy && (!p->scene || qa_world_idle(p->scene)); }
static bool received_actor(const frontend_remote_unified_prediction *p,qa_actor_id actor_id)
{
    qa_saved_actor_id wire;
    return qa_actors_get(p->registry,actor_id) && frontend_remote_unified_wire_actor(p->replica,actor_id,&wire) &&
        frontend_remote_unified_actor_published(p->replica,wire.slot,wire.generation);
}
bool frontend_remote_unified_prediction_trace(frontend_remote_unified_prediction *p,
    const qa_trace_query *query,qa_trace_result *out,qa_error *e)
{
    if (!p || !query || !out || !p->received || !p->scene ||
        !frontend_remote_unified_prediction_idle(p) || !current(p,e) ||
        (query->pass_actor.registry && !received_actor(p,query->pass_actor)))
        return fail(e,QA_ERROR_ARGUMENT,"Unified trace requires its returned received world and actual actor namespace");
    qa_trace_result result;
    p->busy=true;
    bool ok=qa_world_trace(p->scene,query,&result,e);
    p->busy=false;
    if (ok) ok=current(p,e) && (result.hit!=QA_TRACE_HIT_ACTOR || received_actor(p,result.actor));
    if (ok) *out=result;
    return ok;
}
bool frontend_remote_unified_prediction_body_read(const frontend_remote_unified_prediction *p,
    qa_actor_id actor_id,qa_body_state *out,qa_error *e)
{
    qa_body_state body;
    if (!p || !out || !p->received || !p->scene || !frontend_remote_unified_prediction_idle(p) ||
        !current(p,e) || !received_actor(p,actor_id))
        return fail(e,QA_ERROR_ARGUMENT,"Unified body read requires its actual received collision body");
    if (!qa_world_body_read(p->scene,actor_id,&body,e) || !current(p,e)) return false;
    *out=body; return true;
}
bool frontend_remote_unified_prediction_player_origin(frontend_remote_unified_prediction *p,qa_vec3 *out,qa_error *e)
{
    frontend_unified_prediction_view view; qa_body_state body;
    if(!out||!frontend_remote_unified_prediction_read(p,&view,e)||
        !frontend_remote_unified_prediction_body_read(p,view.actor,&body,e)||!current(p,e)) return false;
    *out=qa_vec_add(body.origin,view.origin_shift); return true;
}
bool frontend_remote_unified_prediction_point_contents(frontend_remote_unified_prediction *p,
    const qa_point_query *query,qa_point_contents *out,qa_error *e)
{
    if(!p||!query||!out||!p->received||!p->scene||!frontend_remote_unified_prediction_idle(p)||
        !current(p,e)||(query->pass_actor.registry&&!received_actor(p,query->pass_actor)))
        return fail(e,QA_ERROR_ARGUMENT,"Unified contents requires its returned received world and actual actor namespace");
    qa_point_contents result;
    p->busy=true;
    bool ok=qa_world_point_contents(p->scene,query,&result,e);
    p->busy=false;
    if(ok) ok=current(p,e);
    if(ok) *out=result;
    return ok;
}
const qa_unified_document *frontend_remote_unified_prediction_document(
    const frontend_remote_unified_prediction *p)
{
    qa_error e={0};
    return p && !p->busy && p->received && current(p,&e)?p->snapshot_document:NULL;
}
static bool q3_ground_number(const frontend_unified_q3_prediction_source *source,
    qa_movement_ground ground,int32_t *out,qa_error *e)
{
    if(ground.hit==QA_TRACE_HIT_WORLD) {*out=1022;return true;}
    if(ground.hit==QA_TRACE_HIT_NONE) {*out=1023;return true;}
    if(ground.hit!=QA_TRACE_HIT_ACTOR) return fail(e,QA_ERROR_FORMAT,"Q3 prediction ground has no actual collision role");
    uint32_t number=0;bool found=false;
    if(!source->number(source->context,ground.actor,&number,&found,e)) return false;
    if(found&&number>=1022) return fail(e,QA_ERROR_FORMAT,"Q3 actor ground maps outside actual Source entities");
    *out=found?(int32_t)number:1023;return true;
}
bool frontend_remote_unified_prediction_merged_q3(frontend_remote_unified_prediction *p,
    const qa_q3_player *baseline,qa_actor_id viewer,const frontend_unified_q3_prediction_source *source,
    qa_q3_player *out,frontend_unified_prediction_view *view,qa_error *e)
{
    if(!baseline||!out||!view||!source||!source->current||!source->number||
        !source->current(source->context,e)) return fail(e,QA_ERROR_ARGUMENT,"Q3 prediction merge requires its actual retained Source receipt");
    uint32_t viewer_number=0;bool found=false;
    frontend_unified_prediction_view predicted;
    if(!source->number(source->context,viewer,&viewer_number,&found,e)||!found||viewer_number>=1022||
        baseline->clientNum!=(int32_t)viewer_number||!frontend_remote_unified_prediction_read(p,&predicted,e)||
        !qa_actor_id_equal(viewer,predicted.actor)) return fail(e,QA_ERROR_ARGUMENT,"Q3 prediction merge changed its full Source viewer");
    qa_q3_player merged=*baseline;
    qa_vec3 origin=qa_movement_origin(&predicted.state),velocity=qa_movement_velocity(&predicted.state);
    merged.origin[0]=origin.x;merged.origin[1]=origin.y;merged.origin[2]=origin.z;
    merged.velocity[0]=velocity.x;merged.velocity[1]=velocity.y;merged.velocity[2]=velocity.z;
    merged.viewangles[0]=predicted.view_angles.x;merged.viewangles[1]=predicted.view_angles.y;merged.viewangles[2]=predicted.view_angles.z;
    merged.viewheight=qa_source_float_to_i32(predicted.view_height);
    if(!isfinite(predicted.command_time_ms)||predicted.command_time_ms < -0x1p63||predicted.command_time_ms >= 0x1p63)
        return fail(e,QA_ERROR_FORMAT,"Q3 prediction clock exceeds native millisecond storage");
    uint32_t clock=(uint32_t)(int64_t)predicted.command_time_ms;
    memcpy(&merged.commandTime,&clock,sizeof(clock));
    if(!q3_ground_number(source,predicted.ground,&merged.groundEntityNum,e)) return false;
    if(predicted.state.kind==QA_RULESET_Q3) {
        const qa_q3_movement_state *state=&predicted.state.data.q3;
        merged.commandTime=state->command_time_ms;merged.pmType=state->movement_type;
        memcpy(&merged.pmFlags,&state->movement_flags,sizeof(merged.pmFlags));
        merged.pmTime=state->movement_time_ms;merged.bobCycle=state->bob_cycle;
        for(unsigned i=0;i<3;++i) merged.deltaAngles[i]=state->delta_angle_words[i];
        if(!q3_ground_number(source,state->ground,&merged.groundEntityNum,e)) return false;
        merged.movementDir=state->movement_direction;
        memcpy(&merged.eFlags,&state->flags,sizeof(merged.eFlags));
        merged.pmoveFramecount=state->movement_frame;merged.jumppadFrame=state->jump_pad_frame;merged.jumppadEnt=0;
        if(state->jump_pad.registry) {
            uint32_t number=0;bool present=false;
            if(!source->number(source->context,state->jump_pad,&number,&present,e)) return false;
            if(present&&number>=1022) return fail(e,QA_ERROR_FORMAT,"Q3 jump pad maps outside actual Source entities");
            if(present) merged.jumppadEnt=(int32_t)number;
        }
    }
    if(!current(p,e)||!source->current(source->context,e)) return false;
    *out=merged;*view=predicted;return true;
}
bool frontend_remote_unified_prediction_destroy(frontend_remote_unified_prediction **owned, qa_error *e)
{
    if(!owned || !*owned) return true;
    frontend_remote_unified_prediction *p=*owned;
    if(!frontend_remote_unified_prediction_idle(p)) return fail(e,QA_ERROR_ARGUMENT,"Private prediction is entered during teardown");
    if(p->scene && !qa_world_destroy(p->scene,e)) return false;
    qa_unified_document_destroy(p->snapshot_document);
    free(p->pending_rows); free(p->pending_used);
    free(p); *owned=NULL; return true;
}
