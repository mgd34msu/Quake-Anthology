#include "remote_q3_frame.h"
#include "remote_q3_private.h"
#include "remote_q3_runtime.h"
#include "remote_q3_compiled_video.h"
#include "../../presentation/q3_native/entity_save.h"
#include "qa/network_q3_fields_save.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct frontend_remote_q3_frame {
    frontend_remote_q3 *row;
    q3n_remote_source *source;
    frontend_remote_q3_frame_callbacks callbacks;
    frontend_remote_snapshots *snapshots;
    frontend_remote_snapshots_view snapshot;
    frontend_remote_prediction *predictor;
    frontend_remote_prediction_source prediction_source;
    frontend_remote_prediction_view prediction;
    const frontend_remote_prediction_cgame_seed *seed;
    frontend_remote_snapshots_view seed_snapshot;
    qa_q3_player player, previous;
    qa_q3_entity predicted, predicted_next;
    q3n_entity predicted_entity;
    const qa_q3_player *transition, *transition_previous;
    const char *information_text;
    q3n_remote_frame *entered;
    uint64_t scope;
    q3n_remote_frame_stage stage;
    bool busy, building, processing, has_prediction, retiring, faulted, init_entered, init_finished, importing, video_constructor;
};
static bool fail(qa_error *e,const char *text)
{ return e && e->code!=QA_OK?false:frontend_fail(e,QA_ERROR_ARGUMENT,text); }
static bool prediction_identity(const frontend_remote_prediction_source *source,const qa_native_q3_remote_client_basis *basis)
{
    const qa_application_q3_client_context *a=&source->input.receiver,*b=&basis->client;
    return source->scene.prediction_snapshot && qa_net_client_id_equal(source->input.connection,basis->connection) &&
        source->input.epoch==basis->epoch && source->restart_generation==basis->restart_generation &&
        source->geometry==basis->geometry && a->session==b->session && a->receiver==b->receiver && a->seat==b->seat &&
        a->service_owner==b->service_owner && a->frontend_lifetime==b->frontend_lifetime &&
        a->console==b->console && a->cvars==b->cvars && a->source_client==b->source_client && a->native_source==b->native_source;
}
static bool seed_current(void *context,const frontend_remote_prediction_source *source,
    const frontend_remote_prediction_cgame_seed *seed)
{
    frontend_remote_q3_frame *owner=context;
    return owner && source && seed && owner->seed==seed && owner->processing && !owner->busy &&
        !owner->retiring && !owner->faulted && !owner->importing && owner->row->frames==owner && seed->owner==owner &&
        seed->context==owner && seed->scope==owner->scope && seed->snapshot==owner->seed_snapshot.snapshot &&
        seed->next_snapshot==owner->seed_snapshot.next_snapshot &&
        seed->this_frame_teleport==owner->seed_snapshot.this_frame_teleport &&
        seed->next_frame_teleport==owner->seed_snapshot.next_frame_teleport &&
        frontend_remote_snapshots_current(owner->snapshots,&owner->seed_snapshot) &&
        prediction_identity(source,&owner->seed_snapshot.source.basis) &&
        qa_actor_id_equal(source->configuration.input.actor,owner->seed_snapshot.source.publication.viewer);
}
static bool teleport_take(frontend_remote_q3_frame *owner,qa_error *e)
{
    bool pending=false;
    return owner->callbacks.teleport_take(owner->callbacks.context,&pending,e) &&
        (!pending || frontend_remote_snapshots_mark_teleport(owner->snapshots,e));
}
bool frontend_remote_q3_frame_idle(const frontend_remote_q3_frame *owner)
{ return !owner || (!owner->busy && !owner->processing && frontend_remote_snapshots_idle(owner->snapshots)); }
bool frontend_remote_q3_frame_initialized_current(const frontend_remote_q3_frame *owner)
{
    q3n_remote_source_view source;
    return owner && owner->init_entered && owner->init_finished && !owner->building && !owner->entered &&
        !owner->retiring && !owner->faulted && !owner->importing && owner->row->frames==owner &&
        frontend_remote_q3_frame_idle(owner) && q3n_remote_source_read(owner->source,&source,NULL) &&
        source.basis.application==owner->row->application && source.basis.client.initialized &&
        q3n_remote_source_current(&source);
}
frontend_remote_q3 *frontend_remote_q3_frame_parent(const frontend_remote_q3_frame *owner)
{ return owner?owner->row:NULL; }
void *frontend_remote_q3_frame_callbacks_context(const frontend_remote_q3_frame *owner)
{ return owner?owner->callbacks.context:NULL; }
frontend_remote_snapshots *frontend_remote_q3_frame_snapshots(const frontend_remote_q3_frame *owner)
{ return owner?owner->snapshots:NULL; }
static bool current(void *context,const q3n_remote_frame *frame)
{
    frontend_remote_q3_frame *owner=context;
    if(!owner || owner->retiring || owner->faulted || owner->importing || !owner->busy || (!owner->building && owner->entered!=frame) || !owner->scope ||
        owner->row->frames!=owner || frame->source.basis.application!=owner->row->application ||
        frame->predicted_player!=&owner->player || frame->predicted_state!=&owner->predicted ||
        frame->predicted_next_state!=&owner->predicted_next || frame->predicted_entity!=&owner->predicted_entity ||
        frame->transition_player!=owner->transition || frame->previous_player!=owner->transition_previous ||
        frame->snapshots.stage!=owner->stage) return false;
    bool snapshot_current=owner->stage==Q3N_REMOTE_SNAPSHOT_CALLBACK?
        frontend_remote_snapshots_callback_current(owner->snapshots,&owner->snapshot):
        frontend_remote_snapshots_current(owner->snapshots,&owner->snapshot);
    if(!snapshot_current || frame->snapshots.owner!=owner->snapshots ||
        frame->snapshots.revision!=owner->snapshot.revision || frame->snapshots.callback_scope!=owner->snapshot.callback_scope ||
        frame->snapshots.entities!=frontend_remote_snapshots_storage(owner->snapshots) ||
        frame->snapshots.snapshot!=owner->snapshot.snapshot || frame->snapshots.next_snapshot!=owner->snapshot.next_snapshot ||
        frame->snapshots.time!=owner->snapshot.time || frame->snapshots.processed_message!=owner->snapshot.processed_message ||
        frame->snapshots.command_sequence!=owner->snapshot.command_sequence ||
        frame->snapshots.this_frame_teleport!=owner->snapshot.this_frame_teleport ||
        frame->snapshots.next_frame_teleport!=owner->snapshot.next_frame_teleport ||
        frame->initialization_scope!=(owner->stage==Q3N_REMOTE_INITIALIZATION?owner->scope:0) ||
        frame->video_initialization!=(owner->stage==Q3N_REMOTE_INITIALIZATION && owner->video_constructor) ||
        frame->console_scope!=(owner->stage==Q3N_REMOTE_CONSOLE?owner->scope:0) ||
        frame->awaiting_snapshot_scope!=(owner->stage==Q3N_REMOTE_AWAITING_SNAPSHOT?owner->scope:0) ||
        frame->loading_information_scope!=(owner->stage==Q3N_REMOTE_LOADING_INFORMATION?owner->scope:0) ||
        frame->loading_information_text!=owner->information_text ||
        frame->transition_scope!=(owner->stage==Q3N_REMOTE_PREDICTION_CALLBACK?owner->scope:0)) return false;
    if(owner->stage==Q3N_REMOTE_INITIALIZATION)
        return frame->initialization_scope==owner->scope && !frame->transition_scope &&
            (owner->video_constructor?
                frontend_remote_q3_compiled_video_parent_is(owner->row,owner->row->compiled_video):
                frame->source.publication.initializing && frame->source.reached_command==frame->source.publication.initial_command) &&
            !frame->source.basis.client.initialized &&
            !frame->prediction.owner && !frame->prediction.player;
    if(owner->stage==Q3N_REMOTE_SNAPSHOT_CALLBACK || owner->stage==Q3N_REMOTE_CONSOLE ||
        owner->stage==Q3N_REMOTE_AWAITING_SNAPSHOT || owner->stage==Q3N_REMOTE_LOADING_INFORMATION || !owner->has_prediction)
        return !frame->prediction.owner && !frame->prediction.player;
    frontend_remote_prediction_view prediction;
    return frame->prediction.owner==owner->predictor && frame->prediction.player==&owner->prediction.player &&
        frame->prediction.receipt_time_ns==owner->prediction_source.receipt_time_ns &&
        frame->prediction.command_sequence==owner->prediction.sequence &&
        owner->prediction_source.scene.prediction_snapshot &&
        frame->prediction.seed_message==owner->prediction_source.scene.prediction_snapshot->message_number &&
        frame->prediction.presentation_time==owner->prediction_source.scene.time &&
        frame->prediction.physics_time==owner->prediction_source.scene.physics_time &&
        frontend_remote_prediction_read(owner->predictor,&owner->prediction_source,&prediction);
}
static bool information_current(void *context,const q3n_remote_source_view *source,const char *text)
{
    const frontend_remote_q3_frame *owner=context;
    return owner && owner->busy && owner->processing && owner->stage==Q3N_REMOTE_LOADING_INFORMATION &&
        text==owner->information_text && owner->row->frames==owner && owner->row->runtime &&
        owner->callbacks.context==owner->row->runtime &&
        frontend_remote_q3_runtime_information_current(owner->row->runtime,source,text);
}
static bool entity(void *context,const q3n_remote_frame *frame,uint32_t number,q3n_remote_entity *out,qa_error *e)
{
    frontend_remote_q3_frame *owner=context; const frontend_remote_centity *row;
    if(!current(owner,frame) || !out) return fail(e,"Remote entity borrow lost its entered CGAME frame");
    bool okay=owner->stage==Q3N_REMOTE_SNAPSHOT_CALLBACK?
        frontend_remote_snapshots_callback_entity(owner->snapshots,&owner->snapshot,number,&row,e):
        frontend_remote_snapshots_entity(owner->snapshots,&owner->snapshot,number,&row,e);
    if(!okay) return false;
    *out=(q3n_remote_entity){.frame=frame,.current=&row->current,.next=&row->next,.presentation=row->presentation,
        .number=number,.publication_message=row->publication_message,.published=row->published,
        .current_valid=row->presentation->valid,.interpolate=row->interpolate};
    return current(owner,frame);
}
static bool entity_event(void *context,const q3n_remote_frame *frame,uint32_t number,
    int32_t event,int32_t parameter,qa_error *e)
{
    frontend_remote_q3_frame *owner=context; frontend_remote_centity *row;
    if(!current(owner,frame) || !frontend_remote_snapshots_entity_write(owner->snapshots,
        &owner->snapshot,number,&row,e)) return false;
    row->current.event=event; row->current.eventParm=parameter;
    return current(owner,frame) || fail(e,"Remote event store expired its entered cache receipt");
}
static bool entity_trajectory(void *context,const q3n_remote_entity *row,
    int32_t before,int32_t next_before,int32_t after,int32_t next_after,qa_error *e)
{
    frontend_remote_q3_frame *owner=context;
    if(!row || !q3n_remote_entity_current(row) || !current(owner,row->frame))
        return fail(e,"Remote trajectory store lost its retained private entity");
    if(!row->predicted) return frontend_remote_snapshots_entity_trajectory(owner->snapshots,
        &owner->snapshot,row->number,before,next_before,after,next_after,e);
    if(row->current!=&owner->predicted || row->next!=&owner->predicted_next ||
        owner->predicted.pos.type!=before || owner->predicted_next.pos.type!=next_before)
        return fail(e,"Predicted trajectory store differs from its actual current and next state");
    owner->predicted.pos.type=after; owner->predicted_next.pos.type=next_after;
    return q3n_remote_entity_current(row);
}
static bool entity_weapon(void *context,const q3n_remote_entity *row,int32_t before,int32_t after,qa_error *e)
{
    frontend_remote_q3_frame *owner=context;
    if(!row || !q3n_remote_entity_current(row) || !current(owner,row->frame))
        return fail(e,"Remote weapon store lost its retained private entity");
    if(!row->predicted) return frontend_remote_snapshots_entity_weapon(owner->snapshots,
        &owner->snapshot,row->number,before,after,e);
    if(row->current!=&owner->predicted || owner->predicted.weapon!=before)
        return fail(e,"Predicted weapon store differs from its authored prior value");
    owner->predicted.weapon=after; return q3n_remote_entity_current(row);
}
static bool error_clear(void *context,const q3n_remote_frame *frame,qa_error *e)
{
    frontend_remote_q3_frame *owner=context;
    bool okay=current(owner,frame) && owner->has_prediction &&
        frontend_remote_prediction_error_clear(owner->predictor,&owner->prediction_source,e) &&
        frontend_remote_prediction_read(owner->predictor,&owner->prediction_source,&owner->prediction);
    if(okay) owner->entered->prediction.prediction_error_time=owner->prediction.prediction_error_time;
    return okay;
}
static bool trace_number(void *context,const q3n_remote_frame *frame,const qa_trace_result *hit,int32_t *out,qa_error *e)
{
    frontend_remote_q3_frame *owner=context;
    return current(owner,frame) && owner->callbacks.trace_number(owner->callbacks.context,frame,hit,out,e) &&
        current(owner,frame);
}
static bool enter(frontend_remote_q3_frame *owner,const q3n_remote_source_view *source,
    q3n_remote_frame_stage stage,const qa_q3_player *player,const qa_q3_player *previous,
    q3n_remote_frame *frame,qa_error *e)
{
    if(!owner || owner->retiring || owner->faulted || owner->importing || owner->busy || owner->scope==UINT64_MAX || !source ||
        owner->row->frames!=owner || !q3n_remote_source_current(source))
        return fail(e,"Remote frame entry requires its actual returned CGAME owner");
    bool snapshot_ok=stage==Q3N_REMOTE_SNAPSHOT_CALLBACK?
        frontend_remote_snapshots_callback_read(owner->snapshots,source,&owner->snapshot):
        frontend_remote_snapshots_read(owner->snapshots,&owner->snapshot);
    if(!snapshot_ok) return fail(e,"Remote frame entry lost its actual snapshot callback or completed cut");
    owner->stage=stage; owner->transition=player; owner->transition_previous=previous;
    ++owner->scope; owner->busy=true; owner->entered=frame;
    q3n_remote_snapshot_receipt snapshot={
        .owner=owner->snapshots,.revision=owner->snapshot.revision,.stage=stage,
        .callback_scope=owner->snapshot.callback_scope,.snapshot=owner->snapshot.snapshot,
        .next_snapshot=owner->snapshot.next_snapshot,.entities=frontend_remote_snapshots_storage(owner->snapshots),
        .time=owner->snapshot.time,.processed_message=owner->snapshot.processed_message,
        .command_sequence=owner->snapshot.command_sequence,.this_frame_teleport=owner->snapshot.this_frame_teleport,
        .next_frame_teleport=owner->snapshot.next_frame_teleport};
    q3n_remote_prediction_receipt prediction={0};
    if(owner->has_prediction && (stage==Q3N_REMOTE_PREDICTION_CALLBACK || stage==Q3N_REMOTE_COMPLETED_FRAME))
        prediction=(q3n_remote_prediction_receipt){
        .owner=owner->predictor,.player=&owner->prediction.player,.receipt_time_ns=owner->prediction_source.receipt_time_ns,
        .command_sequence=owner->prediction.sequence,
        .seed_message=owner->prediction_source.scene.prediction_snapshot->message_number,
        .presentation_time=owner->prediction_source.scene.time,.physics_time=owner->prediction_source.scene.physics_time,
        .prediction_error=owner->prediction.prediction_error,.prediction_error_time=owner->prediction.prediction_error_time,
        .hyperspace=owner->prediction.hyperspace};
    q3n_remote_frame_options options={.source=*source,.snapshots=snapshot,.prediction=prediction,
        .predicted_state=&owner->predicted,.predicted_next_state=&owner->predicted_next,
        .predicted_entity=&owner->predicted_entity,.predicted_player=&owner->player,
        .transition_player=player,.previous_player=previous,
        .initialization_scope=stage==Q3N_REMOTE_INITIALIZATION?owner->scope:0,
        .console_scope=stage==Q3N_REMOTE_CONSOLE?owner->scope:0,
        .awaiting_snapshot_scope=stage==Q3N_REMOTE_AWAITING_SNAPSHOT?owner->scope:0,
        .video_initialization=stage==Q3N_REMOTE_INITIALIZATION && owner->video_constructor,
        .loading_information_scope=stage==Q3N_REMOTE_LOADING_INFORMATION?owner->scope:0,
        .loading_information_text=owner->information_text,
        .loading_information_current=stage==Q3N_REMOTE_LOADING_INFORMATION?information_current:NULL,
        .transition_scope=stage==Q3N_REMOTE_PREDICTION_CALLBACK?owner->scope:0,
        .context=owner,.current=current,.entity=entity,.entity_event=entity_event,
        .entity_trajectory=entity_trajectory,.entity_weapon=entity_weapon,
        .prediction_error_clear=error_clear,.trace_number=trace_number};
    owner->building=true;
    bool okay=q3n_remote_frame_read(&options,frame,e);
    owner->building=false;
    if(!okay) { owner->entered=NULL; owner->busy=false; owner->transition=owner->transition_previous=NULL; }
    return okay;
}
static void leave(frontend_remote_q3_frame *owner)
{ owner->entered=NULL; owner->busy=false; owner->transition=owner->transition_previous=NULL; owner->information_text=NULL; }
static bool reached(void *context,const q3n_remote_command *command,qa_error *e)
{
    frontend_remote_q3_frame *owner=context; q3n_remote_source_view source; q3n_remote_frame frame;
    if(!q3n_remote_source_read(owner->source,&source,e)) return false;
    if(!enter(owner,&source,Q3N_REMOTE_SNAPSHOT_CALLBACK,NULL,NULL,&frame,e)) return false;
    bool okay=owner->callbacks.reached(owner->callbacks.context,&frame,command,e); leave(owner); return okay;
}
static bool respawn(void *context,const q3n_remote_source_view *source,qa_error *e)
{
    frontend_remote_q3_frame *owner=context; q3n_remote_frame frame;
    if(!enter(owner,source,Q3N_REMOTE_SNAPSHOT_CALLBACK,NULL,NULL,&frame,e)) return false;
    bool okay=owner->callbacks.respawn(owner->callbacks.context,&frame,e); leave(owner); return okay;
}
static bool reset_player(void *context,const q3n_remote_source_view *source,frontend_remote_centity *row,qa_error *e)
{
    frontend_remote_q3_frame *owner=context; q3n_remote_frame frame;
    if(!enter(owner,source,Q3N_REMOTE_SNAPSHOT_CALLBACK,NULL,NULL,&frame,e)) return false;
    bool okay=owner->callbacks.reset_player(owner->callbacks.context,&frame,row,e); leave(owner); return okay;
}
static bool event(void *context,const q3n_remote_source_view *source,frontend_remote_centity *row,
    const qa_q3_entity *scratch,qa_vec3 position,int32_t time,qa_error *e)
{
    frontend_remote_q3_frame *owner=context; q3n_remote_frame frame;
    if(!enter(owner,source,Q3N_REMOTE_SNAPSHOT_CALLBACK,NULL,NULL,&frame,e)) return false;
    bool okay=owner->callbacks.event(owner->callbacks.context,&frame,row,scratch,position,time,e);
    leave(owner); return okay;
}
static bool transition_player(void *context,const q3n_remote_source_view *source,
    const qa_q3_player *player,const qa_q3_player *previous,qa_error *e)
{
    frontend_remote_q3_frame *owner=context; q3n_remote_frame frame;
    if(!enter(owner,source,Q3N_REMOTE_SNAPSHOT_CALLBACK,player,previous,&frame,e)) return false;
    bool okay=owner->callbacks.transition_player(owner->callbacks.context,&frame,player,previous,e);
    leave(owner); return okay;
}
static bool lagometer(void *context,const qa_q3_snapshot *snapshot,int32_t ping,qa_error *e)
{
    frontend_remote_q3_frame *owner=context;
    return owner->callbacks.lagometer(owner->callbacks.context,snapshot,ping,e);
}
static bool warning(void *context,const char *text,qa_error *e)
{
    frontend_remote_q3_frame *owner=context;
    return owner->callbacks.warning(owner->callbacks.context,text,e);
}
static frontend_remote_snapshots_options snapshot_options(frontend_remote_q3_frame *owner,qa_q3_product product)
{
    return (frontend_remote_snapshots_options){.frontend=owner->row->frontend,.source=owner->source,
        .product=product,.context=owner,.reached=reached,.respawn=respawn,.reset_player=reset_player,
        .event=event,.transition_player=transition_player,.lagometer=lagometer,.warning=warning};
}
static bool create_frame(frontend_remote_q3 *row,const frontend_remote_q3_frame_callbacks *callbacks,
    bool video,frontend_remote_q3_frame **out,qa_error *e)
{
    frontend_remote_q3_services_view children; q3n_remote_source_view source;
    if(!row || (video && !frontend_remote_q3_compiled_video_parent_is(row,row->compiled_video)) ||
        row->frontend->capture || row->frontend->resource_inventory || row->frames || row->constructing || row->users || !out || *out || !callbacks || !callbacks->context ||
        !callbacks->reached || !callbacks->respawn || !callbacks->reset_player || !callbacks->event ||
        !callbacks->transition_player || !callbacks->prediction_completed || !callbacks->teleport_take || !callbacks->lagometer ||
        !callbacks->warning || !callbacks->trace_number ||
        !frontend_remote_q3_services_read(row,&children,e) || !q3n_remote_source_read(children.source,&source,e))
        return fail(e,"Remote CGAME frame construction requires its actual source and all authored consumers");
    frontend_remote_q3_frame *owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining remote CGAME player and frame continuation");
    owner->row=row; owner->source=children.source; owner->callbacks=*callbacks;
    owner->video_constructor=video;
    owner->player.product=owner->previous.product=source.basis.product;
    row->frames=owner; *out=owner;
    frontend_remote_snapshots_options options=snapshot_options(owner,source.basis.product);
    return video?frontend_remote_snapshots_create_video(&options,&source,&owner->snapshots,e):
        frontend_remote_snapshots_create(&options,&source,&owner->snapshots,e);
}
bool frontend_remote_q3_frame_create(frontend_remote_q3 *row,const frontend_remote_q3_frame_callbacks *callbacks,
    frontend_remote_q3_frame **out,qa_error *e)
{ return create_frame(row,callbacks,false,out,e); }
bool frontend_remote_q3_frame_create_video(frontend_remote_q3 *row,const frontend_remote_q3_frame_callbacks *callbacks,
    frontend_remote_q3_frame **out,qa_error *e)
{ return create_frame(row,callbacks,true,out,e); }
bool frontend_remote_q3_frame_destroy(frontend_remote_q3_frame **owned,qa_error *e)
{
    if(!owned || !*owned) return true;
    frontend_remote_q3_frame *owner=*owned;
    if(owner->row->frames!=owner || !frontend_remote_q3_frame_idle(owner) || owner->row->frontend->capture)
        return fail(e,"Remote CGAME frame retirement retains an entered source consumer");
    owner->retiring=true;
    if(!frontend_remote_snapshots_destroy(owner->snapshots,e)) return false;
    owner->snapshots=NULL;
    if(owner->row->runtime &&
        !frontend_remote_q3_runtime_unbind_frames(owner->row->runtime,owner,e)) return false;
    owner->row->frames=NULL; free(owner); *owned=NULL; return true;
}
bool frontend_remote_q3_frame_initialize(frontend_remote_q3_frame *owner,void *context,
    bool (*initialize)(void *,const q3n_remote_frame *,qa_error *),qa_error *e)
{
    q3n_remote_source_view source; q3n_remote_frame frame;
    if(!owner || !context || !initialize || owner->processing || owner->has_prediction ||
        owner->init_entered ||
        !q3n_remote_source_read(owner->source,&source,e) || source.basis.client.initialized ||
        !enter(owner,&source,Q3N_REMOTE_INITIALIZATION,NULL,NULL,&frame,e))
        return fail(e,"Remote CGAME Init requires its actual uninitialized constructor entry");
    owner->init_entered=true;
    bool okay=initialize(context,&frame,e); owner->init_finished=okay; owner->faulted=!okay; leave(owner); return okay;
}
bool frontend_remote_q3_frame_process(frontend_remote_q3_frame *owner,
    const frontend_remote_snapshot_settings *settings,qa_error *e)
{
    q3n_remote_source_view source;
    if(!owner || owner->retiring || owner->faulted || owner->importing || !owner->init_finished ||
        !frontend_remote_q3_frame_idle(owner) || owner->row->frames!=owner ||
        !q3n_remote_source_read(owner->source,&source,e) || !source.basis.client.initialized)
        return fail(e,"Remote snapshot processing requires its returned CGAME frame owner");
    owner->processing=true;
    bool okay=frontend_remote_snapshots_process(owner->snapshots,settings,e);
    if(okay) okay=teleport_take(owner,e);
    owner->processing=false; owner->faulted=!okay; return okay;
}
bool frontend_remote_q3_frame_predict(frontend_remote_q3_frame *owner,frontend_remote_prediction *predictor,
    bool *present,qa_error *e)
{
    frontend_remote_snapshots_view snapshot; frontend_remote_prediction_view predicted;
    frontend_remote_prediction_source source; frontend_network_prediction_source network,refreshed;
    if(!owner || !predictor || !present || owner->retiring || owner->faulted || owner->importing || !owner->init_finished || owner->scope>UINT64_MAX-2 ||
        !frontend_remote_q3_frame_idle(owner) ||
        owner->row->frames!=owner || !frontend_remote_snapshots_read(owner->snapshots,&snapshot) || !snapshot.snapshot)
        return fail(e,"Remote prediction completion requires its real snapshot and predictor owners");
    *present=false; owner->processing=true;
    bool available=false,source_present=false;
    bool okay=frontend_network_prediction_source_read(owner->row->frontend,&network,&source_present,e);
    if(okay && source_present) okay=frontend_network_prediction_teleport_feedback(owner->row->frontend,
        &network,&snapshot,&refreshed,e) && frontend_remote_prediction_source_read(predictor,&source,&source_present,e);
    if(okay && source_present) {
        okay=prediction_identity(&source,&snapshot.source.basis) &&
            source.scene.snapshot==refreshed.scene.snapshot && source.scene.next_snapshot==refreshed.scene.next_snapshot &&
            source.scene.prediction_snapshot==refreshed.scene.prediction_snapshot && source.scene.revision==refreshed.scene.revision &&
            source.scene.time==refreshed.scene.time && source.scene.physics_time==refreshed.scene.physics_time;
        if(okay) {
            frontend_remote_prediction_cgame_seed seed={.owner=owner,.context=owner,.scope=++owner->scope,
                .snapshot=snapshot.snapshot,.next_snapshot=snapshot.next_snapshot,
                .this_frame_teleport=snapshot.this_frame_teleport,.next_frame_teleport=snapshot.next_frame_teleport,
                .current=seed_current};
            owner->seed_snapshot=snapshot; owner->seed=&seed;
            okay=seed_current(owner,&source,&seed) &&
                frontend_remote_prediction_replay_seed(predictor,&seed,&predicted,&available,e);
            owner->seed=NULL;
        }
    }
    if(okay && available) okay=frontend_remote_prediction_read(predictor,&source,&predicted);
    if(okay && available && predicted.consumed_teleport) {
        frontend_network_prediction_source consumed;
        okay=frontend_remote_snapshots_consume_teleport(owner->snapshots,e) &&
            frontend_remote_snapshots_read(owner->snapshots,&snapshot) &&
            frontend_network_prediction_teleport_consume(owner->row->frontend,&refreshed,&snapshot,
                predictor,&source,&consumed,e) &&
            frontend_remote_prediction_source_read(predictor,&source,&source_present,e) && source_present;
        if(okay) okay=prediction_identity(&source,&snapshot.source.basis) &&
            source.scene.snapshot==consumed.scene.snapshot && source.scene.next_snapshot==consumed.scene.next_snapshot &&
            source.scene.prediction_snapshot==consumed.scene.prediction_snapshot && source.scene.revision==consumed.scene.revision &&
            source.scene.time==consumed.scene.time && source.scene.physics_time==consumed.scene.physics_time &&
            !source.scene.this_frame_teleport && frontend_remote_prediction_read(predictor,&source,&predicted);
    }
    if(okay && available) {
        owner->previous=owner->has_prediction?owner->player:snapshot.snapshot->player;
        owner->player=predicted.player;
        owner->prediction=predicted; owner->prediction_source=source; owner->predictor=predictor; owner->has_prediction=true;
        q3n_remote_source_view publication; q3n_remote_frame frame;
        okay=q3n_remote_source_read(owner->source,&publication,e) &&
            enter(owner,&publication,Q3N_REMOTE_PREDICTION_CALLBACK,&owner->player,&owner->previous,&frame,e);
        if(okay) {
            if(predicted.status==FRONTEND_REMOTE_PREDICTION_PREDICTED || predicted.status==FRONTEND_REMOTE_PREDICTION_UNCHANGED)
                okay=owner->callbacks.transition_player(owner->callbacks.context,&frame,&owner->player,&owner->previous,e);
            if(okay) okay=owner->callbacks.prediction_completed(owner->callbacks.context,&frame,predicted.status,e);
            leave(owner);
        }
        if(okay) okay=teleport_take(owner,e);
    }
    owner->processing=false; owner->faulted=!okay; *present=okay && available;
    return okay || fail(e,"Remote prediction completion expired its actual receipt");
}
bool frontend_remote_q3_frame_draw(frontend_remote_q3_frame *owner,void *context,
    bool (*draw)(void *,const q3n_remote_frame *,qa_error *),qa_error *e)
{
    q3n_remote_source_view source; q3n_remote_frame frame;
    if(!owner || !context || !draw || owner->processing || !owner->has_prediction ||
        !q3n_remote_source_read(owner->source,&source,e) ||
        !enter(owner,&source,Q3N_REMOTE_COMPLETED_FRAME,NULL,NULL,&frame,e))
        return fail(e,"Remote packet draw requires its completed actual prediction and snapshots");
    bool okay=draw(context,&frame,e); owner->faulted=!okay; leave(owner); return okay;
}
static bool returned_call(frontend_remote_q3_frame *owner,q3n_remote_frame_stage stage,
    void *context,bool (*execute)(void *,const q3n_remote_frame *,qa_error *),qa_error *e)
{
    q3n_remote_source_view source; q3n_remote_frame frame;
    if(!owner || !context || !execute || !owner->init_finished ||
        !frontend_remote_q3_frame_idle(owner) || owner->row->frames!=owner ||
        !q3n_remote_source_read(owner->source,&source,e) || !source.basis.client.initialized)
        return fail(e,"Remote lexical call requires its returned initialized CGAME owner");
    owner->processing=true;
    bool okay=enter(owner,&source,stage,NULL,NULL,&frame,e);
    if(okay) {
        okay=execute(context,&frame,e);
        if(okay && !q3n_remote_frame_current(&frame))
            okay=fail(e,"Remote lexical call expired its actual source and cache receipt");
        if(stage==Q3N_REMOTE_AWAITING_SNAPSHOT || stage==Q3N_REMOTE_LOADING_INFORMATION) owner->faulted=!okay;
        leave(owner);
    }
    owner->processing=false; return okay;
}
bool frontend_remote_q3_frame_command(frontend_remote_q3_frame *owner,void *context,
    bool (*execute)(void *,const q3n_remote_frame *,qa_error *),qa_error *e)
{ return returned_call(owner,Q3N_REMOTE_CONSOLE,context,execute,e); }
bool frontend_remote_q3_frame_waiting(frontend_remote_q3_frame *owner,void *context,
    bool (*draw)(void *,const q3n_remote_frame *,qa_error *),qa_error *e)
{ return returned_call(owner,Q3N_REMOTE_AWAITING_SNAPSHOT,context,draw,e); }
bool frontend_remote_q3_frame_information(frontend_remote_q3_frame *owner,void *context,
    bool (*draw)(void *,const q3n_remote_frame *,qa_error *),bool *present,qa_error *e)
{
    q3n_remote_source_view source; const char *text=NULL;
    if(!owner || !present || !frontend_remote_q3_frame_idle(owner) || !owner->row->runtime ||
        owner->callbacks.context!=owner->row->runtime || !q3n_remote_source_read(owner->source,&source,e) ||
        !frontend_remote_q3_runtime_information_read(owner->row->runtime,&source,&text,e))
        return fail(e,"Loading information requires its actual prepared runtime reason");
    *present=false;
    if(!*text) return true;
    owner->information_text=text;
    bool okay=returned_call(owner,Q3N_REMOTE_LOADING_INFORMATION,context,draw,e);
    owner->information_text=NULL; *present=okay; return okay;
}
bool frontend_remote_q3_frame_event_publish(frontend_remote_q3_frame *owner,
    const q3n_remote_frame *frame,int32_t before,qa_error *e)
{
    if(!current(owner,frame) || owner->stage!=Q3N_REMOTE_COMPLETED_FRAME || !owner->has_prediction ||
        !frontend_remote_prediction_entity_event_publish(owner->predictor,&owner->prediction_source,
            before,owner->player.entityEventSequence,e)) return false;
    return frontend_remote_prediction_read(owner->predictor,&owner->prediction_source,&owner->prediction) && current(owner,frame);
}
bool frontend_remote_q3_frame_prediction_status(const frontend_remote_q3_frame *owner,
    const q3n_remote_frame *frame,frontend_remote_prediction_status *out,qa_error *e)
{
    if(!out || !owner || !owner->has_prediction || !current((void *)owner,frame) ||
        (owner->stage!=Q3N_REMOTE_COMPLETED_FRAME && owner->stage!=Q3N_REMOTE_PREDICTION_CALLBACK))
        return fail(e,"Prediction status requires its actual completed CGAME receipt");
    *out=owner->prediction.status; return true;
}
bool frontend_remote_q3_frame_prediction_source(const frontend_remote_q3_frame *owner,
    const q3n_remote_frame *frame,frontend_remote_prediction_source *out,qa_error *e)
{
    if(!out || !owner || !owner->has_prediction || !current((void *)owner,frame) ||
        (owner->stage!=Q3N_REMOTE_COMPLETED_FRAME && owner->stage!=Q3N_REMOTE_PREDICTION_CALLBACK))
        return fail(e,"Prediction source borrow requires its actual entered CGAME receipt");
    *out=owner->prediction_source; return true;
}
bool frontend_remote_q3_frame_network_source(const frontend_remote_q3_frame *owner,
    const q3n_remote_frame *frame,frontend_network_prediction_source *out,qa_error *e)
{
    frontend_network_prediction_source source; bool present=false;
    if(!out || !owner || !current((void *)owner,frame) ||
        !frontend_network_prediction_source_read(owner->row->frontend,&source,&present,e) || !present)
        return fail(e,"Network trace source requires its actual entered CGAME recipient");
    const qa_native_q3_remote_client_basis *basis=&frame->source.basis;
    const qa_application_q3_client_context *a=&source.receiver,*b=&basis->client;
    if(!qa_net_client_id_equal(source.connection,basis->connection) || source.epoch!=basis->epoch ||
        source.restart_generation!=basis->restart_generation || source.geometry!=basis->geometry || source.map!=basis->map ||
        !qa_actor_id_equal(source.viewer,frame->source.publication.viewer) || a->session!=b->session ||
        a->receiver!=b->receiver || a->seat!=b->seat || a->service_owner!=b->service_owner ||
        a->frontend_lifetime!=b->frontend_lifetime || a->console!=b->console || a->cvars!=b->cvars ||
        a->source_client!=b->source_client || a->native_source!=b->native_source ||
        !frontend_network_prediction_source_current(owner->row->frontend,&source) || !current((void *)owner,frame))
        return fail(e,"Network trace source differs from its retained physical CGAME namespace");
    if(owner->has_prediction && (owner->stage==Q3N_REMOTE_COMPLETED_FRAME || owner->stage==Q3N_REMOTE_PREDICTION_CALLBACK) &&
        (source.scene.snapshot!=owner->prediction_source.scene.snapshot ||
         source.scene.next_snapshot!=owner->prediction_source.scene.next_snapshot ||
         source.scene.prediction_snapshot!=owner->prediction_source.scene.prediction_snapshot ||
         source.scene.revision!=owner->prediction_source.scene.revision ||
         source.scene.time!=owner->prediction_source.scene.time || source.scene.physics_time!=owner->prediction_source.scene.physics_time))
        return fail(e,"Network trace source changed after the actual prediction completion");
    *out=source; return true;
}
static bool player_fields(qa_source_save_io *io,qa_q3_player *player,qa_q3_product product)
{
    uint8_t bytes[sizeof(qa_q3_player)+sizeof(qa_q3_entity)]; size_t size=0;
    if(io->direction==QA_SOURCE_SAVE_WRITE) {
        qa_net_writer writer; qa_net_writer_init(&writer,bytes,sizeof(bytes),io->error);
        if(!qa_q3_save_player_fields(&writer,player)) return false;
        size=qa_net_writer_size(&writer);
    }
    if(!qa_source_save_count(io,&size,sizeof(bytes)) || !qa_source_save_bytes(io,bytes,size)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        qa_net_reader reader; qa_net_reader_init(&reader,(qa_bytes){bytes,size},io->error);
        if(!qa_q3_restore_player_fields(&reader,player,product) || !qa_net_reader_finish(&reader)) return false;
    }
    return player->product==product && player->clientNum>=0 && player->clientNum<64;
}
static bool entity_fields(qa_source_save_io *io,qa_q3_entity *entity)
{
    uint8_t bytes[sizeof(qa_q3_player)+sizeof(qa_q3_entity)]; size_t size=0;
    if(io->direction==QA_SOURCE_SAVE_WRITE) {
        qa_net_writer writer; qa_net_writer_init(&writer,bytes,sizeof(bytes),io->error);
        if(!qa_q3_save_entity_fields(&writer,entity)) return false;
        size=qa_net_writer_size(&writer);
    }
    if(!qa_source_save_count(io,&size,sizeof(bytes)) || !qa_source_save_bytes(io,bytes,size)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        qa_net_reader reader; qa_net_reader_init(&reader,(qa_bytes){bytes,size},io->error);
        if(!qa_q3_restore_entity_fields(&reader,entity) || !qa_net_reader_finish(&reader)) return false;
    }
    return entity->number>=0 && entity->number<64;
}
static bool same_player(const qa_q3_player *a,const qa_q3_player *b)
{
    uint8_t first[sizeof(qa_q3_player)+sizeof(qa_q3_entity)],second[sizeof(first)];
    qa_net_writer left,right; qa_net_writer_init(&left,first,sizeof(first),NULL);
    qa_net_writer_init(&right,second,sizeof(second),NULL);
    return qa_q3_save_player_fields(&left,a) && qa_q3_save_player_fields(&right,b) &&
        qa_net_writer_size(&left)==qa_net_writer_size(&right) &&
        !memcmp(first,second,qa_net_writer_size(&left));
}
static bool fields(qa_source_save_io *io,frontend_remote_q3_frame *owner,qa_q3_product product)
{
    uint8_t magic[4]={'Q','R','F','G'}; uint32_t version=2;
    return qa_source_save_bytes(io,magic,sizeof(magic)) && !memcmp(magic,"QRFG",sizeof(magic)) &&
        qa_source_save_u32(io,&version) && version==2 && qa_source_save_u64(io,&owner->scope) &&
        qa_source_save_bool(io,&owner->video_constructor) &&
        qa_source_save_bool(io,&owner->init_entered) && qa_source_save_bool(io,&owner->init_finished) &&
        (!owner->init_finished || owner->init_entered) && qa_source_save_bool(io,&owner->has_prediction) &&
        (!owner->has_prediction || owner->init_finished) && player_fields(io,&owner->player,product) &&
        player_fields(io,&owner->previous,product) && entity_fields(io,&owner->predicted) &&
        entity_fields(io,&owner->predicted_next) && q3n_entity_codec(io,&owner->predicted_entity) &&
        qa_actor_id_equal(owner->predicted_entity.actor,(qa_actor_id){0});
}
static bool snapshot_blob(qa_source_save_io *io,qa_buffer *output,qa_bytes *input)
{
    size_t size=io->direction==QA_SOURCE_SAVE_WRITE?output->size:0;
    if(!qa_source_save_count(io,&size,UINT32_MAX) || !size) return false;
    if(io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,output->data,size);
    if(io->offset>io->input.size || size>io->input.size-io->offset) return false;
    *input=(qa_bytes){io->input.data+io->offset,size}; io->offset+=size; return true;
}
bool frontend_remote_q3_frame_checkpoint(const frontend_remote_q3_frame *owner,qa_buffer *out,qa_error *e)
{
    q3n_remote_source_view source;
    if(!owner || !out || out->data || out->size || owner->retiring || owner->faulted || owner->importing || !frontend_remote_q3_frame_idle(owner) ||
        owner->row->frames!=owner || !q3n_remote_source_read(owner->source,&source,e))
        return fail(e,"Remote CGAME capture requires its returned actual owner");
    if(owner->has_prediction) {
        frontend_remote_prediction_view predicted;
        if(!frontend_remote_prediction_read(owner->predictor,&owner->prediction_source,&predicted) ||
            !same_player(&owner->player,&predicted.player))
            return fail(e,"Remote working player capture lacks its completed predictor cursor feedback");
    }
    if(owner->init_finished!=source.basis.client.initialized)
        return fail(e,"Remote CGAME capture requires its actual completed physical Init publication");
    frontend_remote_q3_frame copy=*owner; qa_source_save_io io={0}; qa_buffer snapshots={0}; qa_bytes input={0};
    bool okay=frontend_remote_snapshots_checkpoint(owner->snapshots,&snapshots,e) &&
        qa_source_save_writer(&io,source.basis.session,e) && fields(&io,&copy,source.basis.product) &&
        snapshot_blob(&io,&snapshots,&input) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&snapshots); return okay;
}
bool frontend_remote_q3_frame_prepare_restored(frontend_remote_q3 *row,
    const frontend_remote_q3_frame_callbacks *callbacks,qa_bytes bytes,frontend_remote_q3_frame **out,qa_error *e)
{
    if(!row || !row->importing || !row->frontend->source_restoring || row->frontend->capture ||
        row->frontend->resource_inventory)
        return fail(e,"Remote CGAME import requires its actual staged resource parent");
    if(!frontend_remote_q3_frame_create(row,callbacks,out,e)) return false;
    frontend_remote_q3_frame *owner=*out; owner->importing=true;
    q3n_remote_source_view source; qa_source_save_io io={0};
    qa_buffer output={0}; qa_bytes snapshots={0};
    bool okay=q3n_remote_source_read(owner->source,&source,e) &&
        qa_source_save_reader(&io,source.basis.session,bytes,e) && fields(&io,owner,source.basis.product) &&
        snapshot_blob(&io,&output,&snapshots) && io.offset==io.input.size;
    qa_source_save_dispose(&io);
    if(okay) okay=owner->init_finished==source.basis.client.initialized;
    if(okay) {
        frontend_remote_snapshots_options options=snapshot_options(owner,source.basis.product);
        frontend_remote_snapshots *restored=NULL;
        okay=frontend_remote_snapshots_restore(&options,&source,snapshots,&restored,e);
        if(okay) {
            okay=frontend_remote_snapshots_destroy(owner->snapshots,e);
            if(okay) owner->snapshots=restored;
            else frontend_remote_snapshots_destroy(restored,NULL);
        }
    }
    owner->faulted=!okay;
    return okay || frontend_fail(e,QA_ERROR_FORMAT,"Invalid remote CGAME predicted-player and snapshot continuation");
}
bool frontend_remote_q3_frame_import_current(const frontend_remote_q3_frame_import_view *view)
{
    const frontend_remote_q3_frame *owner=view?view->owner:NULL;
    return owner && owner->importing && owner->row->importing && owner->row->frontend->source_restoring &&
        !owner->row->frontend->capture && !owner->row->frontend->resource_inventory &&
        !owner->retiring && !owner->faulted && !owner->building && !owner->entered &&
        frontend_remote_q3_frame_idle(owner) && owner->row->frames==owner &&
        view->player==&owner->player && view->has_prediction==owner->has_prediction &&
        view->init_finished==owner->init_finished && view->snapshots.source.owner==owner->source &&
        view->snapshots.source.basis.client.initialized==owner->init_finished &&
        frontend_remote_snapshots_current(owner->snapshots,&view->snapshots);
}
bool frontend_remote_q3_frame_import_read(const frontend_remote_q3_frame *owner,
    frontend_remote_q3_frame_import_view *out,qa_error *e)
{
    if(!owner || !out) return fail(e,"Remote CGAME import requires its retained cache owner");
    frontend_remote_q3_frame_import_view view={.owner=owner,.player=&owner->player,
        .has_prediction=owner->has_prediction,.init_finished=owner->init_finished};
    if(!frontend_remote_snapshots_read(owner->snapshots,&view.snapshots) ||
        !frontend_remote_q3_frame_import_current(&view))
        return fail(e,"Remote CGAME import cache changed its actual staged source");
    *out=view; return true;
}
bool frontend_remote_q3_frame_finish_restore(frontend_remote_q3_frame *owner,
    frontend_remote_prediction *predictor,const frontend_remote_prediction_source *prediction_source,qa_error *e)
{
    frontend_remote_q3_frame_import_view view;
    if(!frontend_remote_q3_frame_import_read(owner,&view,e)) return false;
    if(owner->has_prediction) {
        frontend_remote_prediction_view prediction;
        if(!predictor || !prediction_source || !prediction_identity(prediction_source,&view.snapshots.source.basis) ||
            !qa_actor_id_equal(prediction_source->configuration.input.actor,view.snapshots.source.publication.viewer) ||
            !frontend_remote_prediction_read(predictor,prediction_source,&prediction) ||
            !same_player(&owner->player,&prediction.player) || !frontend_remote_q3_frame_import_current(&view))
            return frontend_fail(e,QA_ERROR_FORMAT,"Restored CGAME player differs from its actual predictor continuation");
        owner->predictor=predictor; owner->prediction_source=*prediction_source; owner->prediction=prediction;
    }
    owner->importing=false; return true;
}
bool frontend_remote_q3_frame_restore(frontend_remote_q3 *row,const frontend_remote_q3_frame_callbacks *callbacks,
    frontend_remote_prediction *predictor,const frontend_remote_prediction_source *prediction_source,
    qa_bytes bytes,frontend_remote_q3_frame **out,qa_error *e)
{
    return frontend_remote_q3_frame_prepare_restored(row,callbacks,bytes,out,e) &&
        frontend_remote_q3_frame_finish_restore(*out,predictor,prediction_source,e);
}
