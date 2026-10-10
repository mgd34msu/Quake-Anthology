#include "remote_q1_prediction.h"
#include "remote_q1_private.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct sent_command {
    uint32_t sequence;
    uint64_t sent_ns;
    qa_qw_command command;
    qa_qw_movement_state continuation;
    bool predicted;
} sent_command;
typedef struct packet_receipt {
    uint64_t sent_ns,received_ns;
    int32_t phase;
    bool invalid;
} packet_receipt;
struct frontend_remote_q1_prediction {
    qa_movement_result scratch;
    qa_qw_movement_state received, predicted;
    sent_command history[64];
    packet_receipt packets[64];
    int64_t latency[256];
    size_t count;
    uint32_t acknowledged, last_sent, received_sequence, discarded_sequence;
    float view_height;
    bool initialized,has_sent,discarded;
};
static qa_vec3 vector(const float *v) { return qa_v3(v[0],v[1],v[2]); }
static bool retain(frontend_remote_q1 *row,qa_error *error)
{
    if(row->prediction) return true;
    row->prediction=calloc(1,sizeof(*row->prediction));
    if (!row->prediction) return remote_q1_fail(error,QA_ERROR_MEMORY,"Retaining QW command prediction");
    if (!qa_movement_result_reserve(&row->prediction->scratch,
        (size_t)qa_actors_capacity(row->options.domain.actors)+1,error)) {
        free(row->prediction); row->prediction=NULL; return false;
    }
    return true;
}
void remote_q1_prediction_clear(frontend_remote_q1 *row)
{
    if (!row) return;
    (void)qa_world_destroy(row->collision_world,NULL); row->collision_world=NULL;
    free(row->collision_actors); row->collision_actors=NULL;
    free(row->collision_models); row->collision_models=NULL;
    row->collision_count=row->collision_capacity=0;
    qa_collision_destroy(row->collision); row->collision = NULL;
    if (row->prediction) qa_movement_result_free(&row->prediction->scratch);
    free(row->prediction); row->prediction = NULL;
}
bool remote_q1_collision_acquire(frontend_remote_q1 *row, qa_collision_geometry **out, qa_error *error)
{
    if (!row || !out || !remote_q1_mutable(row) || !remote_q1_live(row,error) || !row->loaded || !row->map)
        return remote_q1_fail(error,QA_ERROR_ARGUMENT,"Q1 collision requires its actual received map owner");
    *out=row->collision; return true;
}
static bool geometry(frontend_remote_q1 *row,qa_error *error)
{
    qa_collision_geometry *actual;
    return remote_q1_collision_acquire(row,&actual,error);
}
bool remote_q1_prediction_prepare(frontend_remote_q1 *row,qa_error *error)
{
    row->solid_players=qa_cvars_resolve(row->options.domain.cvars,"cl_solid_players");
    row->collision_capacity=qa_actors_capacity(row->options.domain.actors);
    row->collision_actors=calloc(row->collision_capacity,sizeof(*row->collision_actors));
    row->collision_models=calloc(row->model_count,sizeof(*row->collision_models));
    if(!row->collision_actors || !row->collision_models ||
        !qa_world_create(row->options.domain.actors,row->collision,NULL,1,&row->collision_world,error)) return false;
    for(size_t i=0;i<row->model_count;++i) if(row->models[i][0]=='*') {
        char *end; unsigned long model=strtoul(row->models[i]+1,&end,10);
        if(end!=row->models[i]+1 && !*end && model && model<qa_collision_model_count(row->collision))
            row->collision_models[i]=(qa_entity_model_field){.model=(uint32_t)model,.present=true};
    }
    qa_world_query_rules query_rules={.actors=row->collision_actors,
        .merge=qa_q1_is_qw(row->options.domain.protocol)?QA_WORLD_MERGE_QW_CLIENT:QA_WORLD_MERGE_SERVER};
    qa_world_set_query_rules(row->collision_world,&query_rules);
    return !qa_q1_is_qw(row->options.domain.protocol) || retain(row,error);
}
static bool publish_body(frontend_remote_q1 *row,const qa_q1_entity *entity,
    const qa_entity_model_field *model,qa_error *error)
{
    qa_body_state state={.origin=vector(entity->origin)};
    qa_actor_collision collision={.family=QA_GAME_Q1,.shape=QA_SHAPE_BOX,
        .contents=qa_collision_bit(QA_CONTENT_SOLID),.role=QA_COLLISION_SOLID};
    if(model) {
        if(!model->present)
            return remote_q1_fail(error,QA_ERROR_FORMAT,"Received QW brush has no actual collision model");
        if(!qa_collision_model_bounds(row->collision,model->model,&state.bounds,error)) return false;
        collision.inline_model=true; collision.model=model->model;
    } else state.bounds=(qa_bounds){qa_v3(-16,-16,-24),qa_v3(16,16,32)};
    qa_actor_id actor;
    if(!remote_q1_actor_read(row,entity->number,&actor,error)) return false;
    if(row->collision_count==row->collision_capacity)
        return remote_q1_fail(error,QA_ERROR_MEMORY,"Received solids exceed the loaded entity capacity");
    if(!qa_world_body_write(row->collision_world,actor,&state,error) ||
        !qa_world_set_collision(row->collision_world,actor,&collision,error) ||
        !qa_world_link(row->collision_world,actor,NULL,error)) return false;
    row->collision_actors[row->collision_count++]=actor;
    return true;
}
bool remote_q1_prediction_publish(frontend_remote_q1 *row,qa_error *error)
{
    if(!row->collision_world) return true;
    qa_actor_registry *actors=row->options.domain.actors;
    for(size_t i=0;i<row->collision_count;++i)
        if(qa_actors_get(actors,row->collision_actors[i]) &&
            !qa_world_unlink(row->collision_world,row->collision_actors[i],error)) return false;
    row->collision_count=0;
    qa_world_query_rules rules={.actors=row->collision_actors,
        .merge=qa_q1_is_qw(row->options.domain.protocol)?QA_WORLD_MERGE_QW_CLIENT:QA_WORLD_MERGE_SERVER};
    qa_world_set_query_rules(row->collision_world,&rules);
    const qa_cvar_view *solid_players=qa_cvars_read(row->options.domain.cvars,row->solid_players);
    bool players_solid=!solid_players || solid_players->number!=0;
    const qa_q1_entity *players[32]={0};
    for(size_t i=0;i<row->current.count;++i) {
        const qa_q1_entity *entity=row->current.rows+i;
        if(entity->number==row->view_entity) continue;
        if(entity->number && entity->number<=32 && row->qw_player_valid[entity->number-1]) {
            if(players_solid && !(row->qw_players[entity->number-1].flags&QA_QW_PF_DEAD))
                players[entity->number-1]=entity;
        } else if(entity->model && entity->model<=row->model_count && row->models[entity->model-1][0]=='*' &&
            !publish_body(row,entity,row->collision_models+entity->model-1,error)) return false;
    }
    /* Original QW adds brush physents first, then solid players by slot. */
    for(size_t i=0;i<32;++i)
        if(players[i] && !publish_body(row,players[i],NULL,error)) return false;
    rules.count=row->collision_count;
    qa_world_set_query_rules(row->collision_world,&rules);
    return true;
}
static bool trace(void *context,const qa_trace_query *query,qa_trace_result *out,qa_error *error)
{
    frontend_remote_q1 *row=context;
    qa_trace_query q=*query; q.target=(qa_collision_target){0}; q.pass_actor=(qa_actor_id){0}; q.pass_source=(qa_actor_reference){0};
    return qa_world_trace(row->collision_world,&q,out,error);
}
bool remote_q1_prediction_camera_trace(frontend_remote_q1 *row,qa_vec3 start,qa_vec3 end,
    qa_trace_result *out,qa_error *error)
{
    if(!row || !out || !qa_vec_finite(start) || !qa_vec_finite(end) || !row->loaded ||
        !qa_q1_is_qw(row->options.domain.protocol) || !remote_q1_mutable(row) || !remote_q1_live(row,error) ||
        !retain(row,error)) return false;
    qa_trace_query query={.start=start,.end=end,
        .shape={QA_SHAPE_BOX,{qa_v3(-16,-16,-24),qa_v3(16,16,32)}},
        .policy=qa_collision_default_policy(QA_GAME_Q1)};
    query.policy.q1_move=QA_Q1_MOVE_NO_MONSTERS;
    query.policy.q1_hull=1;
    ++row->busy;
    bool ok=geometry(row,error) && trace(row,&query,out,error);
    --row->busy;
    return ok && remote_q1_live(row,error);
}
static bool contents(void *context,const qa_point_query *query,qa_point_contents *out,qa_error *error)
{
    frontend_remote_q1 *row=context;
    qa_point_query q=*query; q.target=(qa_collision_target){0};
    return qa_world_point_contents(row->collision_world,&q,out,error);
}
static bool is_brush(void *context,const qa_trace_result *hit,bool *out,qa_error *error)
{ (void)context; (void)error; *out=hit->hit==QA_TRACE_HIT_WORLD || hit->model!=0; return true; }
static bool exhausted(const frontend_remote_q1_prediction *p)
{
    uint32_t latest=p->count?p->history[p->count-1].sequence:p->received_sequence;
    return (p->discarded && p->discarded_sequence>p->received_sequence) ||
        (latest>p->received_sequence && latest-p->received_sequence>=63);
}
static bool replay(frontend_remote_q1 *row,qa_error *error)
{
    frontend_remote_q1_prediction *p=row->prediction;
    if(!p->initialized || exhausted(p)) return true;
    if(!geometry(row,error)) return false;
    qa_qw_movement_state state=p->received;
    qa_actor_id actor;
    if(!remote_q1_actor_read(row,row->view_entity,&actor,error)) return false;
    const qa_qw_movevars *v=&row->qw.movement;
    qa_movement_profile profile=qa_movement_profile_default(QA_RULESET_QUAKEWORLD);
    profile.data.qw.maximum_command_ms=50; profile.data.qw.shared_controls=false;
    profile.data.qw.parameters=(qa_q1_movement_parameters){v->gravity,v->stop_speed,v->max_speed,
        v->spectator_max_speed,v->accelerate,v->air_accelerate,v->water_accelerate,v->friction,v->water_friction,v->entity_gravity};
    qa_movement_services services={.context=row,.trace=trace,.point_contents=contents,.is_bsp=is_brush};
    qa_movement_result *result=&p->scratch; bool ok=true;
    for(size_t i=0;ok && i<p->count;++i) {
        sent_command *entry=p->history+i;
        qa_movement_input input=qa_movement_input_default(QA_RULESET_QUAKEWORLD,actor);
        input.profile=profile; input.state.data.qw=state; input.prediction=true;
        input.environment.health=(float)row->data.health;
        input.environment.gravity_multiplier=1;
        input.time_ns=entry->sent_ns; input.elapsed_ns=(uint64_t)entry->command.msec*1000000;
        input.standing.bounds=input.crouched.bounds=input.dead.bounds=(qa_bounds){qa_v3(-16,-16,-24),qa_v3(16,16,32)};
        input.standing.view_height=input.crouched.view_height=input.dead.view_height=p->view_height;
        input.command=(qa_usercmd){.kind=QA_RULESET_QUAKEWORLD,.sequence=entry->sequence,
            .milliseconds=entry->command.msec,.angles=vector(entry->command.angles),
            .forward_move=entry->command.forward,.side_move=entry->command.side,.up_move=entry->command.up,
            .buttons=entry->command.buttons,.impulse=entry->command.impulse};
        ok=qa_movement_move(&input,&services,result,error);
        if(ok && result->status!=QA_MOVEMENT_ACTIVE) ok=remote_q1_fail(error,QA_ERROR_ARGUMENT,"Received QW prediction actor was retired");
        if(ok) { state=result->state.data.qw; state.old_buttons=entry->command.buttons;
            entry->continuation=state; entry->predicted=true; }
    }
    if(ok) p->predicted=state;
    return ok;
}
bool remote_q1_prediction_sent(frontend_remote_q1 *row,uint32_t sequence,
    const qa_qw_command *command,uint64_t sent,qa_error *error)
{
    if(!row || !command || row->busy || !remote_q1_mutable(row) || !remote_q1_live(row,error) || !qa_q1_is_qw(row->options.domain.protocol) ||
        sequence>INT32_MAX) return remote_q1_fail(error,QA_ERROR_ARGUMENT,"QW prediction requires an accepted native command");
    if(!retain(row,error)) return false;
    frontend_remote_q1_prediction *p=row->prediction;
    if(p->has_sent && sequence<=p->last_sent) return true;
    if(p->count && sent<p->history[p->count-1].sent_ns) return remote_q1_fail(error,QA_ERROR_ARGUMENT,"QW input timestamp regressed");
    if(p->count==64) { p->discarded=true; p->discarded_sequence=p->history[0].sequence;
        memmove(p->history,p->history+1,63*sizeof(*p->history)); --p->count; }
    p->history[p->count++]=(sent_command){.sequence=sequence,.sent_ns=sent,.command=*command}; p->last_sent=sequence;
    p->has_sent=true;
    p->packets[sequence&63]=(packet_receipt){.sent_ns=sent,.phase=-1};
    ++row->busy; bool ok=replay(row,error); --row->busy; return ok && remote_q1_live(row,error);
}
bool remote_q1_prediction_acknowledged(frontend_remote_q1 *row,uint32_t sequence,qa_error *error)
{
    if(!row || row->busy || !remote_q1_mutable(row) || !remote_q1_live(row,error) || !qa_q1_is_qw(row->options.domain.protocol) || sequence>INT32_MAX)
        return remote_q1_fail(error,QA_ERROR_ARGUMENT,"QW acknowledgement lost its native receiver");
    if(!retain(row,error)) return false;
    if(sequence>row->prediction->acknowledged) row->prediction->acknowledged=sequence;
    return true;
}
bool remote_q1_prediction_receipt(frontend_remote_q1 *row,uint32_t sequence,uint64_t received,qa_error *error)
{
    if(!remote_q1_prediction_acknowledged(row,sequence,error)) return false;
    packet_receipt *packet=row->prediction->packets+(sequence&63);
    if(packet->sent_ns>received) return remote_q1_fail(error,QA_ERROR_ARGUMENT,"QW packet receipt precedes its actual send");
    packet->received_ns=received; packet->phase=0; return true;
}
bool remote_q1_prediction_choked(frontend_remote_q1 *row,uint8_t count,qa_error *error)
{
    if(!row || !remote_q1_mutable(row) || !remote_q1_live(row,error) || !retain(row,error)) return false;
    frontend_remote_q1_prediction *p=row->prediction;
    for(uint32_t i=0;i<count;++i) p->packets[(p->acknowledged-1-i)&63].phase=-2;
    return true;
}
bool remote_q1_prediction_invalid_delta(frontend_remote_q1 *row,qa_error *error)
{
    if(!row || !remote_q1_mutable(row) || !remote_q1_live(row,error) || !retain(row,error)) return false;
    row->prediction->packets[row->prediction->acknowledged&63].invalid=true; return true;
}
bool remote_q1_prediction_loss(frontend_remote_q1 *row,uint32_t outgoing,uint8_t *out,qa_error *error)
{
    if(!row || !out || outgoing>INT32_MAX || row->busy || !remote_q1_mutable(row) || !remote_q1_live(row,error) || !qa_q1_is_qw(row->options.domain.protocol) || !retain(row,error)) return false;
    frontend_remote_q1_prediction *p=row->prediction;
    for(uint32_t age=0;age<64;++age) {
        uint32_t sequence=(outgoing-age)&INT32_MAX;
        const packet_receipt *packet=p->packets+(sequence&63);
        int64_t latency=packet->phase==-1?9999:packet->phase==-2?10000:packet->invalid?9998:
            (int64_t)(((double)packet->received_ns-(double)packet->sent_ns)/1e9*20);
        p->latency[sequence&255]=latency;
    }
    unsigned lost=0;
    for(unsigned age=0;age<256;++age) if(p->latency[(outgoing-age)&255]==9999) ++lost;
    *out=(uint8_t)(lost*100/256); return true;
}
static bool receive_player(frontend_remote_q1 *row,const qa_vec3 *camera_origin,qa_error *error)
{
    if(!row || !remote_q1_mutable(row) || !remote_q1_live(row,error)) return false;
    if(!qa_q1_is_qw(row->options.domain.protocol) || row->qw_intermission ||
        !row->loaded || !row->has_data || !row->qw_player_valid[row->qw.player_slot]) return true;
    if(!retain(row,error)) return false;
    frontend_remote_q1_prediction *p=row->prediction;
    const qa_qw_player *own=row->qw_players+row->qw.player_slot;
    qa_qw_movement_state state=p->initialized?p->received:(qa_qw_movement_state){0};
    for(size_t i=0;i<p->count;++i) if(p->history[i].sequence==p->acknowledged && p->history[i].predicted)
        state=p->history[i].continuation;
    state.origin=qa_qw_origin_from_vec3(camera_origin?*camera_origin:vector(own->origin)); state.velocity=vector(own->velocity);
    state.angles=row->view_angles; state.dead=row->data.health<=0; state.spectator=row->qw.spectator?1:0;
    p->view_height=own->flags&QA_QW_PF_GIB?8:own->flags&QA_QW_PF_DEAD?-16:22;
    p->received=p->predicted=state; p->received_sequence=p->acknowledged; p->initialized=true;
    size_t retained=0;
    for(size_t i=0;i<p->count;++i) if(p->history[i].sequence>p->acknowledged) p->history[retained++]=p->history[i];
    p->count=retained;
    return replay(row,error);
}
bool remote_q1_prediction_receive(frontend_remote_q1 *row,qa_error *error)
{ return receive_player(row,NULL,error); }
bool remote_q1_prediction_camera_receive(frontend_remote_q1 *row,qa_vec3 origin,qa_error *error)
{
    if(!qa_vec_finite(origin)) return remote_q1_fail(error,QA_ERROR_ARGUMENT,"QW camera continuation origin is not finite");
    return receive_player(row,&origin,error);
}
bool remote_q1_prediction_read(const frontend_remote_q1 *row,qa_qw_movement_state *out,float *height,bool *present)
{
    if(!row || !out || !height || !present) return false;
    const frontend_remote_q1_prediction *p=row->prediction;
    *present=p && p->initialized && !exhausted(p) && !row->qw_intermission;
    if(*present) { *out=p->predicted; *height=p->view_height; }
    return true;
}
