#include "network_predictor.h"
#include "remote_q3_services.h"
#include "remote_q3_runtime.h"
#include "remote_q3_frame.h"
#include "network_restore_prediction.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct frontend_network_predictor {
    qa_frontend *frontend;
    qa_application *application;
    frontend_remote_prediction *prediction;
};
static bool fail(qa_error *error, const char *message)
{ return frontend_fail(error, QA_ERROR_ARGUMENT, message); }
static bool float_equal(float a, float b)
{ uint32_t x, y; memcpy(&x, &a, sizeof(x)); memcpy(&y, &b, sizeof(y)); return x == y; }
static bool vector_equal(qa_vec3 a, qa_vec3 b)
{ return float_equal(a.x,b.x) && float_equal(a.y,b.y) && float_equal(a.z,b.z); }
static bool bounds_equal(qa_bounds a, qa_bounds b)
{ return vector_equal(a.mins,b.mins) && vector_equal(a.maxs,b.maxs); }
static bool posture_equal(qa_movement_posture a, qa_movement_posture b)
{ return bounds_equal(a.bounds,b.bounds) && float_equal(a.view_height,b.view_height); }
static bool clock_equal(const qa_clock_config *a,const qa_clock_config *b)
{
    return a->kind==b->kind && a->initial_time_ns==b->initial_time_ns &&
        a->interval_ns==b->interval_ns && a->minimum_frame_ns==b->minimum_frame_ns &&
        a->maximum_frame_ns==b->maximum_frame_ns && a->initial_lead_ns==b->initial_lead_ns &&
        a->maximum_steps==b->maximum_steps;
}
static bool numeric_equal(const qa_application_movement_numeric *a,
    const qa_application_movement_numeric *b)
{
    return a->id==b->id && a->radix==b->radix && a->scalar_mantissa_bits==b->scalar_mantissa_bits &&
        a->double_mantissa_bits==b->double_mantissa_bits && a->evaluation_method==b->evaluation_method &&
        a->rounding==b->rounding && a->native_c==b->native_c && a->qw_origin_binary64==b->qw_origin_binary64;
}
static bool environment_equal(const qa_movement_environment *a,const qa_movement_environment *b)
{
    return float_equal(a->health,b->health) && a->flight==b->flight && a->haste==b->haste &&
        a->invulnerable==b->invulnerable && float_equal(a->gravity_multiplier,b->gravity_multiplier) &&
        float_equal(a->speed_multiplier,b->speed_multiplier) && a->fixed_pose==b->fixed_pose &&
        a->fixed_crouched==b->fixed_crouched && posture_equal(a->pose,b->pose) &&
        a->has_body_bounds==b->has_body_bounds && bounds_equal(a->body_bounds,b->body_bounds) &&
        a->has_mode==b->has_mode && a->mode==b->mode && a->has_stance==b->has_stance && a->crouched==b->crouched;
}
static bool q1_parameters_equal(const qa_q1_movement_parameters *a, const qa_q1_movement_parameters *b)
{
    return float_equal(a->gravity,b->gravity) && float_equal(a->stop_speed,b->stop_speed) &&
        float_equal(a->max_speed,b->max_speed) && float_equal(a->spectator_max_speed,b->spectator_max_speed) &&
        float_equal(a->accelerate,b->accelerate) && float_equal(a->air_accelerate,b->air_accelerate) &&
        float_equal(a->water_accelerate,b->water_accelerate) && float_equal(a->friction,b->friction) &&
        float_equal(a->water_friction,b->water_friction) && float_equal(a->entity_gravity,b->entity_gravity);
}
static bool profile_equal(const qa_movement_profile *a, const qa_movement_profile *b)
{
    if(a->kind!=b->kind) return false;
    switch(a->kind) {
    case QA_MOVEMENT_NETQUAKE:
        return q1_parameters_equal(&a->data.nq.parameters,&b->data.nq.parameters) &&
            a->data.nq.edition==b->data.nq.edition && float_equal(a->data.nq.edge_friction,b->data.nq.edge_friction) &&
            float_equal(a->data.nq.max_velocity,b->data.nq.max_velocity) &&
            float_equal(a->data.nq.ideal_pitch_scale,b->data.nq.ideal_pitch_scale) &&
            float_equal(a->data.nq.roll_speed,b->data.nq.roll_speed) && float_equal(a->data.nq.roll_angle,b->data.nq.roll_angle) &&
            a->data.nq.no_clip_angle_hack==b->data.nq.no_clip_angle_hack && a->data.nq.no_step==b->data.nq.no_step &&
            a->data.nq.source_jump_authority==b->data.nq.source_jump_authority &&
            a->data.nq.preserve_fixangle_roll==b->data.nq.preserve_fixangle_roll;
    case QA_MOVEMENT_QUAKEWORLD:
        return q1_parameters_equal(&a->data.qw.parameters,&b->data.qw.parameters) &&
            a->data.qw.maximum_command_ms==b->data.qw.maximum_command_ms && a->data.qw.shared_controls==b->data.qw.shared_controls;
    case QA_MOVEMENT_Q2_CLASSIC:
        return float_equal(a->data.q2.air_accelerate,b->data.q2.air_accelerate) &&
            a->data.q2.snap_initial==b->data.q2.snap_initial && a->data.q2.strafejump_hack==b->data.q2.strafejump_hack;
    case QA_MOVEMENT_Q2_RERELEASE:
        return float_equal(a->data.q2r.air_accelerate,b->data.q2r.air_accelerate) && a->data.q2r.n64_physics==b->data.q2r.n64_physics;
    case QA_MOVEMENT_Q3:
        return a->data.q3.missionpack==b->data.q3.missionpack && a->data.q3.no_footsteps==b->data.q3.no_footsteps &&
            a->data.q3.fixed_ms==b->data.q3.fixed_ms;
    }
    return false;
}
static bool configuration_identity(const qa_application_control_prediction_configuration *a,
    const qa_application_control_prediction_configuration *b)
{
    return qa_actor_id_equal(a->input.actor,b->input.actor) && a->movement==b->movement &&
        a->profile_id==b->profile_id && clock_equal(&a->clock,&b->clock) && numeric_equal(&a->numeric,&b->numeric) &&
        numeric_equal(&a->prediction_numeric,&b->prediction_numeric) &&
        a->character==b->character && a->arsenal==b->arsenal && a->input.state.kind==b->input.state.kind &&
        a->input.profile.kind==b->input.profile.kind && a->q3_character==b->q3_character && a->q3_arsenal==b->q3_arsenal &&
        a->native_q3_character==b->native_q3_character && a->native_q3_arsenal==b->native_q3_arsenal;
}
static bool configuration_current(void *context, const qa_application_control_prediction_configuration *configuration)
{
    frontend_network_predictor *owner=context;
    qa_application_control_prediction_configuration actual; qa_error ignored={0};
    return owner && configuration && owner->frontend && owner->application==owner->frontend->application &&
        qa_application_control_prediction_read(owner->frontend->application,configuration->input.actor,&actual,&ignored) &&
        configuration_identity(configuration,&actual);
}
static bool native_children(frontend_network_predictor *owner, const frontend_network_prediction_source *network,
    frontend_remote_q3 **out, frontend_remote_q3_services_view *services, qa_error *error)
{
    frontend_remote_q3 *found=NULL;
    for(size_t i=0;i<frontend_remote_q3_count(owner->frontend);++i) {
        frontend_remote_q3 *row=frontend_remote_q3_at(owner->frontend,i);
        frontend_remote_q3_resources resources; qa_error ignored={0};
        if(!frontend_remote_q3_resources_read(row,&resources,&ignored)) continue;
        const qa_application_q3_client_context *receiver=&resources.domain.source.receiver;
        if(receiver->receiver!=network->receiver.receiver || receiver->seat!=network->receiver.seat ||
            receiver->service_owner!=network->receiver.service_owner || resources.map!=network->map ||
            resources.geometry!=network->geometry) continue;
        if(found) return fail(error,"Prediction has two current native CLIENT resource parents");
        found=row;
    }
    if(!found || !frontend_remote_q3_services_read(found,services,error) ||
        !frontend_remote_q3_frames_read(found) || !frontend_remote_q3_runtime_read(found))
        return fail(error,"Prediction lacks its actual compiled CLIENT service and frame parents");
    *out=found; return true;
}
static bool source_observe(void *context, frontend_remote_prediction_source *out, bool *present,
    frontend_network_prediction_source *observed, qa_error *error)
{
    frontend_network_predictor *owner=context; frontend_network_prediction_source network;
    if(!owner || !out || !present || !owner->frontend || owner->application!=owner->frontend->application)
        return fail(error,"Prediction source needs its retained Network owner and application");
    memset(out,0,sizeof(*out)); *present=false;
    bool available=false;
    bool importing=frontend_network_restore_prediction_pending(owner->frontend);
    if(!(importing?frontend_network_restore_prediction_read(owner->frontend,&network,&available,error):
        frontend_network_prediction_source_read(owner->frontend,&network,&available,error))) return false;
    if(!available) return true;
    if(!network.receiver.native_source) return fail(error,"Compiled prediction cannot borrow an original CGAME cache");
    frontend_remote_q3 *row=NULL; frontend_remote_q3_services_view services;
    qa_native_q3_remote_client_cache cache; q3n_remote_source_view source;
    if(!native_children(owner,&network,&row,&services,error) ||
        !(importing?frontend_network_restore_prediction_input_read(owner->frontend,&out->input,&available,error):
          frontend_network_prediction_input_read(owner->frontend,&out->input,&available,error)) || !available ||
        !qa_native_q3_remote_client_cache_read(services.client,&cache,error) ||
        !q3n_remote_source_read(services.source,&source,error) ||
        !qa_application_control_prediction_read(owner->frontend->application,network.viewer,&out->configuration,error)) return false;
    frontend_remote_q3_frame_import_view imported={0};
    frontend_remote_q3_frame *frame=frontend_remote_q3_frames_read(row);
    bool imported_frame=importing && !frontend_remote_q3_frame_initialized_current(frame);
    if(imported_frame) {
        if(!frontend_remote_q3_frame_import_read(frame,&imported,error) ||
            imported.snapshots.source.owner!=source.owner ||
            !qa_net_client_id_equal(imported.snapshots.source.publication.connection,network.connection) ||
            imported.snapshots.source.publication.epoch!=network.epoch ||
            imported.snapshots.source.publication.restart_generation!=network.restart_generation ||
            !qa_actor_id_equal(imported.snapshots.source.publication.viewer,network.viewer) ||
            !frontend_remote_q3_runtime_previous_time_import_read(frontend_remote_q3_runtime_read(row),&imported,
                &out->previous_presentation_time,error)) return false;
    } else if(!frontend_remote_q3_runtime_previous_time_read(frontend_remote_q3_runtime_read(row),&source,
        &out->previous_presentation_time,error)) return false;
    out->geometry=network.geometry; out->scene=network.scene;
    out->settings_owner=cache.owner;
    out->restart_generation=network.restart_generation; out->receipt_time_ns=owner->frontend->wall_time_ns;
    out->settings=(frontend_remote_prediction_settings){.game_type=source.game_type,.dm_flags=source.dm_flags,
        .pmove_msec=cache.pmove_msec,.error_decay_integer=cache.error_decay_integer,.show_miss=cache.show_miss,
        .error_decay_value=cache.error_decay,.demo_playback=source.publication.demo_playback,
        .no_predict=cache.no_predict!=0,.synchronous_clients=cache.synchronous_clients!=0,
        .predict_items=cache.predict_items!=0,.pmove_fixed=cache.pmove_fixed!=0};
    bool acknowledged=importing?
        frontend_network_restore_prediction_acknowledgement(owner->frontend,&network,&out->has_acknowledged_sequence,
            &out->acknowledged_sequence,&out->history_unavailable,error):
        frontend_network_prediction_acknowledgement(owner->frontend,&network,&out->has_acknowledged_sequence,
            &out->acknowledged_sequence,&out->history_unavailable,error);
    if(!acknowledged ||
        !qa_native_q3_remote_client_cache_current(services.client,&cache) || !q3n_remote_source_current(&source) ||
        !(importing?frontend_network_restore_prediction_current(owner->frontend,&network):
          frontend_network_prediction_source_current(owner->frontend,&network)) ||
        !(importing?frontend_network_restore_prediction_input_current(owner->frontend,&out->input):
          frontend_network_prediction_input_current(owner->frontend,&out->input)) ||
        (imported_frame && !frontend_remote_q3_frame_import_current(&imported)) ||
        (importing && !imported_frame && !frontend_remote_q3_frame_initialized_current(frame)))
        return fail(error,"Prediction source changed during its actual cache and transport observation");
    if(observed) *observed=network;
    *present=true; return true;
}
static bool source_read(void *context, frontend_remote_prediction_source *out, bool *present, qa_error *error)
{ return source_observe(context,out,present,NULL,error); }
static bool settings_equal(const frontend_remote_prediction_settings *a, const frontend_remote_prediction_settings *b)
{
    return a->game_type==b->game_type && a->dm_flags==b->dm_flags && a->pmove_msec==b->pmove_msec &&
        a->error_decay_integer==b->error_decay_integer && a->show_miss==b->show_miss &&
        float_equal(a->error_decay_value,b->error_decay_value) && a->demo_playback==b->demo_playback &&
        a->no_predict==b->no_predict && a->synchronous_clients==b->synchronous_clients &&
        a->predict_items==b->predict_items && a->pmove_fixed==b->pmove_fixed;
}
static bool source_current_observe(void *context, const frontend_remote_prediction_source *source,
    frontend_network_prediction_source *observed)
{
    frontend_network_predictor *owner=context; frontend_remote_prediction_source now; bool present=false; qa_error ignored={0};
    frontend_network_prediction_source network;
    if(!source || !source_observe(context,&now,&present,&network,&ignored) || !present ||
        !(frontend_network_restore_prediction_pending(owner->frontend)?
          frontend_network_restore_prediction_input_current(owner->frontend,&source->input):
          frontend_network_prediction_input_current(owner->frontend,&source->input)) ||
        !configuration_identity(&source->configuration,&now.configuration) ||
        !profile_equal(&source->configuration.input.profile,&now.configuration.input.profile) ||
        !environment_equal(&source->configuration.input.environment,&now.configuration.input.environment) ||
        source->configuration.has_client_view_offset!=now.configuration.has_client_view_offset ||
        !vector_equal(source->configuration.client_view_offset,now.configuration.client_view_offset) ||
        !posture_equal(source->configuration.input.standing,now.configuration.input.standing) ||
        !posture_equal(source->configuration.input.crouched,now.configuration.input.crouched) ||
        !posture_equal(source->configuration.input.dead,now.configuration.input.dead) ||
        !bounds_equal(source->configuration.input.invulnerability_bounds,now.configuration.input.invulnerability_bounds) ||
        source->configuration.input.shape.kind!=now.configuration.input.shape.kind ||
        !bounds_equal(source->configuration.input.shape.bounds,now.configuration.input.shape.bounds) ||
        source->configuration.input.q1_solid!=now.configuration.input.q1_solid ||
        source->configuration.input.has_source_punch_angles!=now.configuration.input.has_source_punch_angles ||
        !vector_equal(source->configuration.input.source_punch_angles,now.configuration.input.source_punch_angles) ||
        source->configuration.input.has_trace_policy!=now.configuration.input.has_trace_policy ||
        source->configuration.input.trace_policy.family!=now.configuration.input.trace_policy.family ||
        source->configuration.input.trace_policy.contents_mask!=now.configuration.input.trace_policy.contents_mask ||
        source->configuration.input.trace_policy.q1_move!=now.configuration.input.trace_policy.q1_move ||
        source->configuration.input.trace_policy.q1_hull!=now.configuration.input.trace_policy.q1_hull ||
        source->configuration.input.trace_policy.q2_merged_contents!=now.configuration.input.trace_policy.q2_merged_contents ||
        source->configuration.input.trace_policy.curves!=now.configuration.input.trace_policy.curves ||
        source->configuration.input.trace_policy.player_curve_clip!=now.configuration.input.trace_policy.player_curve_clip) return false;
    bool current=source->settings_owner==now.settings_owner && source->geometry==now.geometry && source->scene.snapshot==now.scene.snapshot &&
        source->scene.next_snapshot==now.scene.next_snapshot && source->scene.prediction_snapshot==now.scene.prediction_snapshot &&
        source->scene.revision==now.scene.revision && source->scene.time==now.scene.time && source->scene.physics_time==now.scene.physics_time &&
        source->scene.processed_snapshot==now.scene.processed_snapshot && source->scene.this_frame_teleport==now.scene.this_frame_teleport &&
        source->scene.next_frame_teleport==now.scene.next_frame_teleport && source->receipt_time_ns==now.receipt_time_ns &&
        source->restart_generation==now.restart_generation && source->previous_presentation_time==now.previous_presentation_time &&
        source->has_acknowledged_sequence==now.has_acknowledged_sequence &&
        source->acknowledged_sequence==now.acknowledged_sequence && source->history_unavailable==now.history_unavailable &&
        settings_equal(&source->settings,&now.settings);
    if(current && observed) *observed=network;
    return current;
}
static bool source_current(void *context, const frontend_remote_prediction_source *source)
{ return source_current_observe(context,source,NULL); }
static bool actor_at(void *context, uint32_t number, qa_actor_id *out, bool *present, qa_error *error)
{ return frontend_network_prediction_actor_at(((frontend_network_predictor *)context)->frontend,number,out,present,error); }
static bool number_of(void *context, qa_actor_id actor, uint32_t *out, bool *present, qa_error *error)
{ return frontend_network_prediction_number_of(((frontend_network_predictor *)context)->frontend,actor,out,present,error); }
static bool trace(void *context, const frontend_remote_prediction_source *source,
    const qa_trace_query *query, qa_trace_result *out, qa_error *error)
{
    frontend_network_predictor *owner=context; frontend_network_prediction_source network;
    return source_current_observe(owner,source,&network) && frontend_network_prediction_trace(owner->frontend,&network,query,out,error);
}
static bool contents(void *context, const frontend_remote_prediction_source *source,
    const qa_point_query *query, qa_point_contents *out, qa_error *error)
{
    frontend_network_predictor *owner=context; frontend_network_prediction_source network;
    return source_current_observe(owner,source,&network) && frontend_network_prediction_point_contents(owner->frontend,&network,query,out,error);
}
static bool is_bsp(void *context, const frontend_remote_prediction_source *source,
    const qa_trace_result *hit, bool *out, qa_error *error)
{
    frontend_network_predictor *owner=context; frontend_network_prediction_source network;
    return source_current_observe(owner,source,&network) && frontend_network_prediction_is_bsp(owner->frontend,&network,hit,out,error);
}
static bool adjust_mover(void *context, const frontend_remote_prediction_source *source, qa_vec3 origin,
    int32_t number, int32_t from_time, int32_t to_time, qa_vec3 *out, qa_error *error)
{
    frontend_network_predictor *owner=context; frontend_network_prediction_source network;
    return source_current_observe(owner,source,&network) &&
        frontend_network_prediction_adjust_mover(owner->frontend,&network,origin,number,from_time,to_time,out,error);
}
static bool trigger_count(void *context, const frontend_remote_prediction_source *source, size_t *out, qa_error *error)
{
    frontend_network_predictor *owner=context; frontend_network_prediction_source network;
    return source_current_observe(owner,source,&network) && frontend_network_prediction_trigger_count(owner->frontend,&network,out,error);
}
static bool trigger_at(void *context, const frontend_remote_prediction_source *source, size_t index,
    qa_q3_prediction_scene_entity_view *out, bool *present, qa_error *error)
{
    frontend_network_predictor *owner=context; frontend_network_prediction_source network;
    return source_current_observe(owner,source,&network) && frontend_network_prediction_trigger_at(owner->frontend,&network,index,out,present,error);
}
static bool overlap(void *context, const frontend_remote_prediction_source *source,
    const qa_q3_prediction_scene_entity_view *entity, qa_vec3 origin, qa_bounds bounds, bool *out, qa_error *error)
{
    frontend_network_predictor *owner=context; frontend_network_prediction_source network;
    return source_current_observe(owner,source,&network) &&
        frontend_network_prediction_trigger_overlap(owner->frontend,&network,entity,origin,bounds,out,error);
}
static bool item_position(void *context, const frontend_remote_prediction_source *source,
    const qa_q3_prediction_scene_entity_view *entity, qa_vec3 *out, qa_error *error)
{
    frontend_network_predictor *owner=context; frontend_network_prediction_source network;
    return source_current_observe(owner,source,&network) && frontend_network_prediction_item_position(owner->frontend,&network,entity,out,error);
}
static bool item_misc_time(void *context, const frontend_remote_prediction_source *source,
    const qa_q3_prediction_scene_entity_view *entity, int32_t *out, qa_error *error)
{
    frontend_network_predictor *owner=context; frontend_network_prediction_source network;
    frontend_remote_q3 *row=NULL; frontend_remote_q3_services_view services;
    return source_current_observe(owner,source,&network) && native_children(owner,&network,&row,&services,error) &&
        frontend_remote_snapshots_misc_time_read(frontend_remote_q3_frame_snapshots(frontend_remote_q3_frames_read(row)),
            &network,entity,out,error);
}
static bool set_pmove_msec(void *context, const frontend_remote_prediction_source *source, int32_t value, qa_error *error)
{
    frontend_network_predictor *owner=context; frontend_network_prediction_source network;
    if(!source_current_observe(owner,source,&network)) return false;
    char text[16]; snprintf(text,sizeof(text),"%d",value);
    if(!qa_cvars_set(network.receiver.cvars,"pmove_msec",text,true,error)) return false;
    const qa_cvar_view *actual=qa_cvars_find(network.receiver.cvars,"pmove_msec");
    return actual && actual->integer==value && !strcmp(actual->value,text) &&
        (source_current(owner,source) || fail(error,"Prediction clamp changed its actual cached source receipt"));
}
static bool warning(void *context, const frontend_remote_prediction_source *source, const char *text, qa_error *error)
{
    frontend_network_predictor *owner=context;
    if(!text || !source_current(owner,source)) return fail(error,"Prediction warning lost its real CLIENT source");
    frontend_console_print(owner->frontend,&source->input.receiver.command_context,text);
    return source_current(owner,source) || fail(error,"Prediction warning changed its actual source receipt");
}
static frontend_remote_prediction_options options(frontend_network_predictor *owner)
{
    return (frontend_remote_prediction_options){.session=qa_application_session(owner->frontend->application),
        .context=owner,.configuration_current=configuration_current,.source_read=source_read,.source_current=source_current,
        .actor_at=actor_at,.number_of=number_of,.trace=trace,.point_contents=contents,.is_bsp=is_bsp,.adjust_mover=adjust_mover,
        .trigger_count=trigger_count,.trigger_at=trigger_at,.trigger_overlap=overlap,.item_position=item_position,
        .item_misc_time=item_misc_time,.set_pmove_msec=set_pmove_msec,.warning=warning};
}
bool frontend_network_predictor_create(qa_frontend *f, const qa_application_control_prediction_configuration *initial,
    frontend_network_predictor **out, qa_error *error)
{
    if(!f || !initial || !out || *out) return fail(error,"Prediction construction needs its actual cold configuration");
    frontend_network_predictor *owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining the Network prediction bindings");
    owner->frontend=f; owner->application=f->application;
    frontend_remote_prediction_options actual=options(owner); actual.initial_configuration=*initial;
    if(!frontend_remote_prediction_create(&actual,&owner->prediction,error)) { free(owner); return false; }
    *out=owner; return true;
}
bool frontend_network_predictor_restore(qa_frontend *f, qa_bytes bytes, frontend_network_predictor **out, qa_error *error)
{
    if(!f || !out || *out) return fail(error,"Prediction restore needs an empty actual candidate holder");
    frontend_network_predictor *owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining restored Network prediction bindings");
    owner->frontend=f; owner->application=f->application; frontend_remote_prediction_options actual=options(owner);
    if(!frontend_remote_prediction_restore_new(&actual,bytes,&owner->prediction,error)) { free(owner); return false; }
    *out=owner; return true;
}
void frontend_network_predictor_destroy(frontend_network_predictor *owner)
{ if(owner) { frontend_remote_prediction_destroy(owner->prediction); free(owner); } }
frontend_remote_prediction *frontend_network_predictor_read(const frontend_network_predictor *owner)
{ return owner?owner->prediction:NULL; }
bool frontend_network_predictor_source_current(frontend_network_predictor *owner,
    const frontend_remote_prediction_source *source)
{ return source_current(owner,source); }
bool frontend_network_predictor_bound(const frontend_network_predictor *owner, const qa_frontend *f)
{ return !owner || (f && owner->frontend==f && owner->application==f->application && owner->prediction); }
void frontend_network_predictor_rebind(frontend_network_predictor *owner, qa_frontend *f)
{ if(owner) owner->frontend=f; }
