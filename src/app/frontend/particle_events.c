#include "internal.h"
#include "qa/scene_effects.h"
#include "visual_access.h"
#include "qa/game_q2_feedback.h"
#include "qa/application_native_q2_presentation.h"
#include "qa/application_native_q2_client.h"
#include "particle_clock.h"
#include "q2_client_lerp.h"
#include "q2_animation.h"
#include "particle_delivery.h"
#include "particle_audio.h"
#include "native_q2_messages.h"
#include "qa/application_native_q2_delivery.h"
#include "qa/application_selected_effects.h"
#include "source_client_registry.h"
#include "selected_effects_particles.h"
#include "q2_entity_effects.h"
#include "qa/allocation_gate.h"
#include "q2_temporary_beams.h"
#include "selected_effects_q1_temporary.h"
#include "legacy_render_policy.h"
#include "native_q3_client.h"
#include "qa/game_q1_weapons.h"
#include "remote_q2_effects.h"
#include "qa/application_ui_names.h"
#include "material_movies.h"
#include "qa/stamp.h"

typedef struct frontend_particle_owner {
    struct frontend_particle_owner *next;
    qa_actor_owner provider;
    qa_game_family family;
    qa_actor_owner world_source;
    qa_actor_id recipient;
    uint64_t map_identity;
    qa_scene_resources *images;
    qa_scene_image *particle_image;
    qa_builtin_random random;
    float gravity;
    uint64_t q2_interval_ns;
    qa_q2_edition q2_edition;
    frontend_fx_q1_state *q1;
    bool q1_quakeworld, q1_rerelease;
    qa_scene_model *q1_beam_models[4]; /* borrowed from the actual appearance owner */
    qa_frontend *frontend;
    uint32_t seat;
    frontend_remote_q2_effects *q2_effects;
    qa_arena q2_storage;
    frontend_remote_q2_effects_pose *q2_poses;
    size_t q2_pose_count;
    qa_stamp_set q2_pose_stamps;
    uint64_t entity_frame, entity_server_frame;
    double animation_server_ms, animation_client_ms;
    double entity_milliseconds;
    float entity_frame_seconds;
    bool entity_sampled, entity_advance, entity_events_ready, entity_events;
    uint64_t entity_event_sample,q2_event_frame;
} frontend_particle_owner;
typedef struct frontend_q2_client_sample {
    qa_application_native_q2_client client;
    qa_application_native_q2_player_sample player, previous;
    frontend_q2_animation gun_animation;
    uint32_t physical_seat;
    uint64_t frame;
} frontend_q2_client_sample;
typedef struct frontend_q2_entity_sample {
    qa_application_native_q2_entity_sample current, previous;
    frontend_q2_animation animation;
    uint64_t frame;
} frontend_q2_entity_sample;
typedef struct frontend_q1_trail {
    qa_actor_id actor;
    qa_actor_owner provider;
    const qa_model *model;
    qa_vec3 origin;
    uint64_t frame;
} frontend_q1_trail;
typedef struct frontend_visual_sample {
    qa_application_visual_view view;
    qa_actor_id actor;
    frontend_particle_owner *q2_owners[4];
    frontend_q2_animation animation;
    qa_actor_owner animation_provider;
    qa_string_id animation_models[4];
    qa_vec3 animation_origin;
    uint64_t animation_source_frame, animation_sample_frame;
    double animation_server_ms, animation_client_ms, animation_tick_ms;
    uint32_t animation_previous_frame;
    qa_q2_edition animation_edition;
    uint64_t frame;
    bool found, sampled;
} frontend_visual_sample;
typedef struct frontend_particle_cvars {
    uint64_t view_identity;
    qa_cvar_handle smooth_explosions, disable_particles, disable_explosions, dlight_hacks;
    qa_cvar_handle hand, gun, gun_fov;
    frontend_remote_q2_effects_cvars effects;
} frontend_particle_cvars;
struct frontend_particle_state {
    qa_arena storage;
    frontend_particle_owner *owners;
    frontend_particle_cvars cvars[QA_INPUT_LOCAL_SEATS];
    uint64_t sample_ns, previous_sample_ns;
    qa_actor_owner clock_source;
    qa_session *clock_session;
    uint64_t clock_map_revision, clock_generation, client_ns, client_host_ns, server_ns, server_frame;
    uint64_t client_frame, client_interval_ns;
    bool client_clock, client_pending;
    frontend_q2_entity_sample *source_entities;
    size_t source_entity_count, source_entity_capacity;
    frontend_q2_client_sample source_clients[QA_INPUT_LOCAL_SEATS];
    size_t source_client_count;
    bool source_ready;
    qa_q2_edition source_edition;
    frontend_q1_trail *q1_trails;
    size_t q1_trail_capacity;
    frontend_visual_sample *visual_samples;
    size_t visual_sample_capacity;
};
bool frontend_particle_prepare(qa_frontend *frontend, qa_error *error)
{
    if (frontend->particles) return true;
    frontend->particles = calloc(1, sizeof(*frontend->particles));
    if (!frontend->particles)
        return frontend_fail(error, QA_ERROR_MEMORY, "allocating frontend particle continuation");
    frontend_particle_state *state=frontend->particles;
    size_t capacity=qa_actors_capacity(qa_session_actor_registry(qa_application_session(frontend->application)));
    size_t bytes=capacity*(sizeof(*state->q1_trails)+sizeof(*state->visual_samples)+sizeof(*state->source_entities))+
        3*_Alignof(max_align_t);
    bool okay=qa_arena_reserve(&state->storage,bytes,error);
    if (okay) {
        state->q1_trails=qa_arena_alloc(&state->storage,capacity*sizeof(*state->q1_trails),_Alignof(frontend_q1_trail),error);
        state->visual_samples=qa_arena_alloc(&state->storage,capacity*sizeof(*state->visual_samples),_Alignof(frontend_visual_sample),error);
        state->source_entities=qa_arena_alloc(&state->storage,capacity*sizeof(*state->source_entities),_Alignof(frontend_q2_entity_sample),error);
        okay=state->q1_trails && state->visual_samples && state->source_entities;
    }
    if (!okay) { qa_arena_destroy(&state->storage);free(state);frontend->particles=NULL;return false; }
    memset(state->q1_trails,0,capacity*sizeof(*state->q1_trails));
    memset(state->visual_samples,0,capacity*sizeof(*state->visual_samples));
    memset(state->source_entities,0,capacity*sizeof(*state->source_entities));
    state->q1_trail_capacity=state->visual_sample_capacity=state->source_entity_capacity=capacity;
    qa_arena_seal(&state->storage);
    frontend->particles->sample_ns = frontend->particles->previous_sample_ns =
        qa_session_elapsed(qa_application_session(frontend->application));
    return true;
}
static frontend_particle_cvars *particle_cvars_bind(qa_frontend *frontend,
    uint32_t seat, const qa_cvars *registry, qa_error *error)
{
    if (!frontend_particle_prepare(frontend,error)) return NULL;
    frontend_particle_cvars *bindings=frontend->particles->cvars+seat;
    uint64_t identity=qa_cvars_view_identity(registry);
    if (bindings->view_identity!=identity) {
        *bindings=(frontend_particle_cvars){.view_identity=identity,
            .smooth_explosions=qa_cvars_resolve(registry,"cl_smooth_explosions"),
            .disable_particles=qa_cvars_resolve(registry,"cl_disable_particles"),
            .disable_explosions=qa_cvars_resolve(registry,"cl_disable_explosions"),
            .dlight_hacks=qa_cvars_resolve(registry,"cl_dlight_hacks"),
            .hand=qa_cvars_resolve(registry,"hand"),
            .gun=qa_cvars_resolve(registry,"cl_gun"),
            .gun_fov=qa_cvars_resolve(registry,"cl_gunfov")};
        frontend_remote_q2_effects_cvars_bind((qa_cvars *)registry,&bindings->effects);
    }
    return bindings;
}

bool frontend_particle_visual_read(qa_frontend *frontend,qa_actor_id actor,
    qa_application_visual_view *out,bool *found,qa_error *error)
{
    if (!frontend_particle_prepare(frontend,error)) return false;
    frontend_particle_state *state=frontend->particles;
    if ((size_t)actor.slot>=state->visual_sample_capacity) {
        qa_allocation_gate_capacity_exhausted();
        return frontend_fail(error,QA_ERROR_MEMORY,"Visual sample actor capacity exhausted");
    }
    frontend_visual_sample *sample=state->visual_samples+actor.slot;
    if (!sample->sampled || sample->frame!=frontend->frame_number ||
        !qa_actor_id_equal(sample->actor,actor)) {
        qa_application_visual_view view;qa_error observed={0};
        bool present=qa_application_visual_read(frontend->application,actor,&view,&observed);
        if (!present && observed.code!=QA_ERROR_NOT_FOUND) {
            if (error) *error=observed;
            return false;
        }
        if (!sample->sampled || !qa_actor_id_equal(sample->actor,actor))
            *sample=(frontend_visual_sample){.actor=actor};
        sample->frame=frontend->frame_number;sample->found=present;sample->sampled=true;
        memset(sample->q2_owners,0,sizeof(sample->q2_owners));
        if (present) sample->view=view;
    }
    *found=sample->found;
    if (*found) *out=sample->view;
    return true;
}

bool frontend_particle_source_begin(qa_frontend *frontend, uint64_t elapsed_ns, qa_error *error)
{
    qa_application_map_view map;
    if (frontend->options.dedicated || frontend_network_remote(frontend) ||
        qa_application_get_state(frontend->application) != QA_APPLICATION_RUNNING ||
        !qa_application_map_read(frontend->application, &map)) return true;
    qa_application_native_q2_presentation source;
    bool found;
    if (!qa_application_native_q2_presentation_selected(frontend->application, &source, &found, error))
        return false;
    if (!found) {
        if (frontend->particles) frontend->particles->client_clock = frontend->particles->client_pending = false;
        return true;
    }
    bool local = false;
    for (uint32_t i = 0; i < frontend->options.seats; ++i) {
        uint32_t seat;
        qa_application_native_q2_client client;
        bool present;
        if (!frontend_seat_launch_id_read(frontend, i, &seat)) continue;
        if (!qa_application_native_q2_presentation_local(frontend->application, &source,
                seat, &client, &present, error)) return false;
        local = local || present;
    }
    if (!local) {
        if (frontend->particles) frontend->particles->client_clock = frontend->particles->client_pending = false;
        return true;
    }
    if (!frontend_particle_prepare(frontend, error)) return false;
    frontend_particle_state *state = frontend->particles;
    if (state->client_pending)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 client sampling has an uncompleted committed frame");
    if (!state->client_clock || state->clock_source != source.source_owner ||
        state->clock_map_revision != source.map_revision || state->clock_session!=source.session ||
        state->clock_generation!=source.publication_generation) {
        state->client_ns = 0;
        state->source_ready = false;
        state->clock_source = source.source_owner;
        state->clock_map_revision = source.map_revision;
        state->clock_session=source.session; state->clock_generation=source.publication_generation;
        state->client_clock = true;
    }
    state->client_interval_ns=source.clock_config.interval_ns;
    if (!state->client_interval_ns)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q2 client sample lost its real Source interval");
    if (frontend->time_ns < elapsed_ns || state->client_ns > UINT64_MAX - elapsed_ns)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 client sample clock duration overflow");
    state->client_ns += elapsed_ns;
    state->client_host_ns = frontend->time_ns;
    state->client_frame = frontend->frame_number;
    state->client_pending = true;
    return true;
}

bool frontend_particle_source_complete(qa_frontend *frontend, qa_error *error)
{
    frontend_particle_state *state = frontend->particles;
    if (!state || !state->client_clock) return true;
    qa_application_native_q2_presentation source;
    bool found;
    if (!state->client_pending || state->client_frame != frontend->frame_number ||
        state->client_host_ns != frontend->time_ns ||
        !qa_application_native_q2_presentation_selected(frontend->application, &source, &found, error) ||
        !found || source.clock_config.interval_ns!=state->client_interval_ns ||
        source.source_owner != state->clock_source ||
        source.map_revision != state->clock_map_revision || source.session!=state->clock_session ||
        source.publication_generation!=state->clock_generation)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 client sampling lost its committed physical source frame");
    if (!state->source_ready || source.clock.frame.number!=state->server_frame) {
        uint32_t extent;
        if (!qa_application_native_q2_presentation_extent(frontend->application,&source,&extent,error)) return false;
        if (extent>state->source_entity_capacity) {
            qa_allocation_gate_capacity_exhausted();
            return frontend_fail(error,QA_ERROR_MEMORY,"Q2 sample actor capacity exhausted");
        }
        for (uint32_t slot=0;slot<extent;++slot) {
            qa_application_native_q2_entity_sample entity={0};
            bool present;
            if (!qa_application_native_q2_presentation_sample(frontend->application,&source,slot,&entity,&present,error)) return false;
            frontend_q2_entity_sample *held=state->source_entities+slot;
            bool continuous=state->source_ready && held->frame+1==source.clock.frame.number &&
                present && qa_actor_id_equal(held->current.actor,entity.actor) &&
                frontend_q2_lerp_entity_continuous(&held->current,&entity);
            frontend_q2_animation_commit(&held->animation,entity.frame,
                entity.render_flags&(UINT32_C(1)<<22)?entity.old_frame:held->current.frame,
                entity.render_flags,(double)source.server_time_ns/1000000.0,continuous);
            held->previous=continuous ? held->current : entity;
            if (!continuous && present && entity.event!=7) held->previous.origin=entity.previous_origin;
            held->current=entity; held->frame=source.clock.frame.number;
        }
        state->source_entity_count=extent;
        state->source_client_count=frontend->options.seats;
        for (uint32_t physical=0;physical<frontend->options.seats;++physical) {
            frontend_q2_client_sample *held=state->source_clients+physical;
            uint32_t seat;
            qa_application_native_q2_client client={0};
            qa_application_native_q2_player_sample player={0};
            bool present=false;
            if (frontend_seat_launch_id_read(frontend,physical,&seat)) {
                if (!qa_application_native_q2_presentation_local(frontend->application,&source,seat,&client,&present,error)) return false;
                if (present && !qa_application_native_q2_presentation_player(frontend->application,&source,&client,&player,error)) return false;
            }
            bool continuous=state->source_ready && held->frame+1==source.clock.frame.number &&
                player.present && held->player.present && qa_actor_id_equal(held->client.actor,client.actor) &&
                frontend_q2_lerp_near(held->player.origin,player.origin,256);
            if (continuous && player.edition==QA_Q2_RERELEASE) {
                const qa_application_native_q2_entity_sample *entity=client.client_slot+1<extent &&
                    state->source_entities[client.client_slot+1].current.actor.registry ?
                    &state->source_entities[client.client_slot+1].current : NULL;
                continuous=(!entity || (entity->event!=6 && entity->event!=7)) &&
                    !((held->player.render_flags^player.render_flags)&16u);
            }
            frontend_q2_animation_commit(&held->gun_animation,player.gun_frame,held->player.gun_frame,0,
                (double)source.server_time_ns/1000000.0,
                continuous && player.gun_model==held->player.gun_model && player.gun_frame!=0);
            held->previous=continuous ? held->player : player;
            held->player=player; held->client=client; held->physical_seat=physical;
            held->frame=source.clock.frame.number;
        }
        if (!qa_application_native_q2_presentation_current(frontend->application,&source))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 Source sample changed its completed frame");
        state->source_ready=true;
    }
    state->source_edition=source.edition;
    uint64_t lower = source.server_time_ns > state->client_interval_ns ?
        source.server_time_ns - state->client_interval_ns : 0;
    if (state->client_ns > source.server_time_ns) state->client_ns = source.server_time_ns;
    else if (state->client_ns < lower) state->client_ns = lower;
    state->server_ns = source.server_time_ns;
    state->server_frame = source.clock.frame.number;
    state->client_pending = false;
    return true;
}

bool frontend_particle_source_cancel(qa_frontend *frontend, qa_error *error)
{
    frontend_particle_state *state = frontend->particles;
    if (!state || !state->client_pending) return true;
    if (state->client_frame != frontend->frame_number || state->client_host_ns != frontend->time_ns)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 client cancellation lost its entered host frame");
    state->client_pending = false;
    state->source_ready = false;
    state->source_entity_count = state->source_client_count = 0;
    return true;
}

static bool client_sample(qa_frontend *frontend, uint64_t *sample, uint64_t *server,
    float *back_lerp, bool *physical, qa_error *error)
{
    frontend_particle_state *state = frontend->particles;
    *physical = state && state->client_clock;
    if (!*physical) {
        *sample = *server = qa_session_elapsed(qa_application_session(frontend->application));
        *back_lerp = 0;
        return true;
    }
    qa_application_native_q2_presentation source;
    bool found;
    if (state->client_pending ||
        !qa_application_native_q2_presentation_selected(frontend->application, &source, &found, error) ||
        !found || !state->client_interval_ns || source.clock_config.interval_ns!=state->client_interval_ns ||
        source.source_owner != state->clock_source ||
        source.map_revision != state->clock_map_revision || source.server_time_ns != state->server_ns ||
        source.clock.frame.number != state->server_frame || state->client_ns > state->server_ns ||
        state->server_ns - state->client_ns > state->client_interval_ns)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 particles lost their actual completed client sample");
    *sample = state->client_ns;
    *server = state->server_ns;
    *back_lerp = (float)((double)(state->server_ns - state->client_ns) / (double)state->client_interval_ns);
    return true;
}
bool frontend_particle_q2_client_time(qa_frontend *frontend,qa_actor_owner owner,
    double *seconds,bool *found,qa_error *error)
{
    frontend_particle_state *state=frontend->particles;
    *found=state && state->client_clock && state->clock_source==owner;
    if (!*found) return true;
    uint64_t sample,server;float back_lerp;bool physical;
    if (!client_sample(frontend,&sample,&server,&back_lerp,&physical,error)) return false;
    *seconds=(double)(sample/UINT64_C(1000000))/1000.0;
    return true;
}
bool frontend_particle_q2_player_sample(qa_frontend *frontend, uint32_t seat, qa_actor_id actor,
    qa_application_native_q2_player_sample *out, uint32_t *old_gun_frame,
    float *back_lerp, bool *found, qa_error *error)
{
    if (!out || !old_gun_frame || !back_lerp || !found)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 client view requires its output fields");
    *found=false; *back_lerp=0;
    frontend_particle_state *state=frontend->particles;
    if (!state || !state->client_clock || !state->source_ready || state->client_pending || seat>=state->source_client_count) return true;
    const frontend_q2_client_sample *client=state->source_clients+seat;
    if (!client->player.present || !qa_actor_id_equal(client->client.actor,actor)) return true;
    *back_lerp=(float)((double)(state->server_ns-state->client_ns)/(double)state->client_interval_ns);
    frontend_q2_lerp_player(&client->previous,&client->player,1-*back_lerp,out,old_gun_frame);
    frontend_q2_animation_sample animation=frontend_q2_animation_lerp(&client->gun_animation,
        client->player.edition==QA_Q2_RERELEASE,true,out->gun_frame,*old_gun_frame,0,
        (double)state->client_ns/1000000.0,(double)state->client_interval_ns/1000000.0,
        1000.0/(client->player.gun_rate>0?client->player.gun_rate:10),*back_lerp);
    out->gun_frame=animation.frame;*old_gun_frame=animation.old_frame;*back_lerp=animation.back_lerp;
    *found=true;
    return true;
}

bool frontend_particle_q2_entity_sample(qa_frontend *frontend, qa_application_visual_view *view,
    float *back_lerp, qa_error *error)
{
    if (!view || !back_lerp) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 client entity requires its view");
    *back_lerp=0;
    frontend_particle_state *state=frontend->particles;
    if (!state || !view->actor.registry) return true;
    const qa_actor_record *record=state->client_clock && state->source_ready && !state->client_pending ?
        qa_actors_get(qa_world_actors(qa_application_world(frontend->application)),view->actor):NULL;
    if (record &&
        record->owner==state->clock_source && record->has_source &&
        record->source_slot<state->source_entity_count) {
        const frontend_q2_entity_sample *held=state->source_entities+record->source_slot;
        if (!qa_actor_id_equal(held->current.actor,view->actor)) return true;
        qa_application_native_q2_entity_sample sample;
        uint32_t old_frame;
        *back_lerp=(float)((double)(state->server_ns-state->client_ns)/(double)state->client_interval_ns);
        frontend_q2_lerp_entity(&held->previous,&held->current,1-*back_lerp,&sample,&old_frame);
        view->body.origin=sample.origin;view->body.angles=sample.angles;
        view->previous_origin=sample.previous_origin;
        if (view->family==QA_GAME_Q2 && view->provider==state->clock_source) {
            frontend_q2_animation_sample animation=frontend_q2_animation_lerp(&held->animation,
                state->source_edition==QA_Q2_RERELEASE,false,sample.frame,
                sample.render_flags&(UINT32_C(1)<<22)?sample.old_frame:old_frame,sample.render_flags,
                (double)state->client_ns/1000000.0,(double)state->client_interval_ns/1000000.0,100,*back_lerp);
            view->visual.frame=(int32_t)animation.frame;view->visual.old_frame=(int32_t)animation.old_frame;
            *back_lerp=animation.back_lerp;
            return true;
        }
        *back_lerp=0;
    }
    if (view->family==QA_GAME_Q2 && (size_t)view->actor.slot<state->visual_sample_capacity) {
        const frontend_visual_sample *held=state->visual_samples+view->actor.slot;
        if (qa_actor_id_equal(held->actor,view->actor) &&
            held->animation_sample_frame==frontend->frame_number && held->animation_provider==view->provider) {
            frontend_q2_animation_sample animation=frontend_q2_animation_lerp(&held->animation,
                held->animation_edition==QA_Q2_RERELEASE,false,
                view->visual.frame>=0?(uint32_t)view->visual.frame:0,
                view->visual.render_flags&(UINT32_C(1)<<22)?(view->visual.old_frame>=0?(uint32_t)view->visual.old_frame:0):held->animation_previous_frame,
                view->visual.render_flags,held->animation_client_ms,held->animation_tick_ms,100,
                (float)((held->animation_server_ms-held->animation_client_ms)/held->animation_tick_ms));
            view->visual.frame=(int32_t)animation.frame;view->visual.old_frame=(int32_t)animation.old_frame;
            *back_lerp=animation.back_lerp;
        }
    }
    return true;
}

static bool particle_world_owner(qa_frontend *frontend, qa_actor_owner *out)
{
    const qa_launch_choices *choices = qa_launch_snapshot_choices(qa_application_launch(frontend->application));
    const qa_launch_binding *binding = choices ? qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "") : NULL;
    return binding && binding->instance &&
        qa_application_provider_owner(frontend->application, binding->instance, out);
}
static bool delivery_world_current(qa_frontend *frontend, qa_actor_owner world_source,
    uint64_t map_identity)
{
    qa_application_map_view map;
    qa_actor_owner physical = 0;
    qa_collision_geometry *geometry = qa_world_geometry(qa_application_world(frontend->application));
    return world_source && map_identity && geometry &&
        qa_application_map_read(frontend->application, &map) &&
        particle_world_owner(frontend, &physical) &&
        physical == world_source && qa_collision_map_identity(geometry) == map_identity;
}
static bool q2_delivery_admit(qa_frontend *frontend, qa_actor_owner provider,
    const qa_application_q2_audience *audience)
{
    const char *reason = NULL;
    if (!audience->captured) reason = "Q2 effect has no captured source delivery";
    else if (audience->source != provider) reason = "Q2 effect delivery belongs to another producer";
    else if (!delivery_world_current(frontend, audience->world_source, audience->map_identity))
        reason = "Q2 effect delivery belongs to a retired world";
    if (reason) {
        qa_error skipped = {0};
        qa_error_set(&skipped, QA_ERROR_UNSUPPORTED, 0, "%s", reason);
        qa_application_feature_report(frontend->application, "Q2 particle delivery", &skipped);
        return false;
    }
    return audience->count != 0;
}
static bool particle_client_current(qa_frontend *frontend, const frontend_particle_owner *owner)
{
    if (!delivery_world_current(frontend, owner->world_source, owner->map_identity)) return false;
    for (uint32_t seat = 0; seat < frontend->options.seats; ++seat) {
        if (frontend_network_local_input_owned(frontend,seat)) continue;
        qa_actor_id actor;
        if (frontend_seat_actor_read(frontend, seat, &actor) &&
            qa_actor_id_equal(actor, owner->recipient)) return true;
    }
    return false;
}
typedef struct frontend_q2_sample {
    double milliseconds;
    uint64_t time_ns;
    float back_lerp;
    bool physical;
} frontend_q2_sample;
static bool q2_owner_sample(qa_frontend *frontend, const frontend_particle_owner *owner,
    frontend_q2_sample *out, qa_error *error)
{
    if (!owner->q2_effects || !delivery_world_current(frontend, owner->world_source, owner->map_identity))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 sample lost its delivered physical world");
    uint64_t sample, server;
    float back_lerp;
    bool physical;
    if (!client_sample(frontend, &sample, &server, &back_lerp, &physical, error)) return false;
    if (physical && frontend->particles->clock_source == owner->provider) {
        *out = (frontend_q2_sample){.milliseconds = (double)(sample / UINT64_C(1000000)),
            .time_ns = sample, .back_lerp = back_lerp, .physical = true};
        return true;
    }
    qa_application_selected_effects source;
    if (!qa_application_effects_producer_read(frontend->application, owner->provider,
            &source, error)) return false;
    if (source.provider != owner->provider || source.family != QA_GAME_Q2 ||
        !qa_application_selected_effects_current(frontend->application, &source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 sampling lost its actual emitting clock receipt");
    *out = (frontend_q2_sample){.time_ns = source.source_time_ns,
        .milliseconds = (double)source.source_time_ns / 1000000.0};
    return true;
}
static void particle_owner_free(frontend_particle_owner *owner)
{
    frontend_remote_q2_effects_destroy(&owner->q2_effects,NULL);
    qa_scene_image_release(owner->particle_image);qa_arena_destroy(&owner->q2_storage);
    free(owner->q1);free(owner);
}
static void particle_clients_retire(qa_frontend *frontend)
{
    frontend_particle_owner **link = frontend->particles ? &frontend->particles->owners : NULL;
    while (link && *link) {
        frontend_particle_owner *owner = *link;
        if (owner->q2_effects && !particle_client_current(frontend, owner)) {
            *link = owner->next; particle_owner_free(owner);
        } else link = &owner->next;
    }
}
static bool particle_images(qa_frontend *frontend, qa_actor_owner provider,
    qa_game_family family, qa_scene_resources **out, qa_error *error)
{
    const char *path = family == QA_GAME_Q1 ? "gfx/palette.lmp" : "pics/colormap.pcx";
    qa_vfs *files = qa_application_provider_files(frontend->application, provider);
    bool found = false;
    uint64_t bytes;
    if (files && !qa_vfs_probe(files, path, &found, &bytes, error)) return false;
    if (found) return frontend_event_images(frontend, provider, family, out, error);
    const qa_launch_snapshot *snapshot = qa_application_launch(frontend->application);
    qa_catalog *catalog = qa_application_catalog(frontend->application);
    for (size_t i = 0; i < qa_launch_snapshot_instance_count(snapshot); ++i) {
        const qa_launch_instance *instance = qa_launch_snapshot_instance(snapshot, i);
        const qa_product *product = qa_catalog_product(catalog, instance->selection.product);
        qa_actor_owner owner;
        if (!product || product->family != family ||
            !qa_application_provider_owner(frontend->application, instance->selection.instance, &owner) ||
            owner == provider) continue;
        files = qa_application_provider_files(frontend->application, owner);
        if (!files) continue;
        if (!qa_vfs_probe(files, path, &found, &bytes, error)) return false;
        if (found) return frontend_event_images(frontend, owner, family, out, error);
    }
    return frontend_fail(error, QA_ERROR_NOT_FOUND, "Particle family has no mounted palette source");
}
static bool local_q2_current(void *context, qa_error *error)
{
    (void)error;
    frontend_particle_owner *owner=context;
    return particle_client_current(owner->frontend,owner);
}
static bool local_q2_source_current(void *context,
    const frontend_remote_q2_effects_source *source,qa_error *error)
{
    (void)source;
    return local_q2_current(context,error);
}
static bool local_q2_pose(void *context,qa_actor_id actor,
    frontend_remote_q2_effects_pose *out,qa_error *error)
{
    frontend_particle_owner *owner=context;
    qa_application_visual_view view;bool found;
    if (!frontend_particle_visual_read(owner->frontend,actor,&view,&found,error)) return false;
    if (!found) { *out=(frontend_remote_q2_effects_pose){0}; return true; }
    *out=(frontend_remote_q2_effects_pose){.actor=actor,.origin=view.body.origin,
        .angles=view.body.angles,.effects=view.visual.effects,.frame=view.visual.frame,
        .model_present=view.visual.has_inline_model || view.visual.models[0],
        .model_identity=view.visual.models[0]};
    return true;
}
static bool local_q2_number_pose(void *context,uint32_t number,
    frontend_remote_q2_effects_pose *out,qa_error *error)
{
    frontend_particle_owner *owner=context;
    const qa_actor_record *record;uint32_t cursor=0;
    qa_actor_registry *actors=qa_world_actors(qa_application_world(owner->frontend->application));
    while (qa_actors_next(actors,&cursor,&record))
        if (record->owner==owner->provider && record->has_source && record->source_slot==number)
            return local_q2_pose(context,record->id,out,error);
    *out=(frontend_remote_q2_effects_pose){0};return true;
}
static bool local_q2_actor_live(void *context,qa_actor_id actor,bool *live,qa_error *error)
{
    (void)error;
    frontend_particle_owner *owner=context;
    *live=qa_actors_get(qa_world_actors(qa_application_world(owner->frontend->application)),actor)!=NULL;
    return true;
}
static bool local_q2_viewer(void *context,qa_actor_id *actor,qa_error *error)
{
    (void)error;*actor=((frontend_particle_owner *)context)->recipient;return true;
}
static bool local_q2_model(void *context,const char *path,bool acquire,
    qa_scene_model **out,qa_error *error)
{
    frontend_particle_owner *owner=context;qa_frontend *frontend=owner->frontend;
    *out=NULL;
    if (acquire) {
        qa_vfs *files=qa_application_provider_files(frontend->application,owner->provider);
        bool found;uint64_t bytes;
        if (!qa_vfs_probe(files,path,&found,&bytes,error)) return false;
        if (!found) return true;
        qa_string_id id;frontend_visual_model_view model;
        if (!qa_strings_intern_cstr(qa_session_strings(qa_application_session(frontend->application)),path,&id,error) ||
            !frontend_visual_model_acquire(frontend,owner->provider,QA_GAME_Q2,id,NULL,&model,error)) return false;
        *out=model.scene;return true;
    }
    for (size_t i=0;i<frontend_visual_owner_count(frontend);++i) {
        frontend_visual_owner_view media;
        if (!frontend_visual_owner_read(frontend,i,&media) || media.owner!=owner->provider || media.family!=QA_GAME_Q2) continue;
        for (size_t j=0;j<frontend_visual_model_count(frontend,i);++j) {
            frontend_visual_model_view model;
            if (frontend_visual_model_read(frontend,i,j,&model) && !strcmp(model.path,path)) { *out=model.scene;return true; }
        }
    }
    return true;
}
static bool local_q2_sound(void *context,const char *path,qa_vec3 origin,qa_actor_id actor,
    double milliseconds,int32_t channel,float volume,float attenuation,double delay,qa_error *error)
{
    frontend_particle_owner *owner=context;qa_frontend *frontend=owner->frontend;qa_string_id resource;
    if (!qa_strings_intern_cstr(qa_session_strings(qa_application_session(frontend->application)),path,&resource,error)) return false;
    qa_builtin_event event={.kind=QA_BUILTIN_SOUND,.family=QA_GAME_Q2,.provider=owner->provider,
        .resource=resource,.actor=actor,.origin=origin,.channel=channel,.volume=volume,
        .attenuation=attenuation,.time_ns=(uint64_t)(fmax(0,milliseconds+delay*1000)*1000000)};
    return frontend_particle_sound(frontend,&event,owner->seat,owner->recipient,error);
}
static bool local_q2_controls(void *context,frontend_remote_q2_effects_controls *out,qa_error *error)
{
    frontend_particle_owner *owner=context;qa_frontend *frontend=owner->frontend;
    frontend_source_client_registry client;bool found;
    if (!frontend_source_client_registry_read(frontend,owner->seat,&client,&found,error)) return false;
    if (!found) return false;
    frontend_particle_cvars *bindings=particle_cvars_bind(frontend,owner->seat,client.cvars,error);
    if (!bindings) return false;
    frontend_remote_q2_effects_control_source source={.cvars=client.cvars,.handles=&bindings->effects,
        .gun=bindings->gun,.console=client.console,.context=owner,.current=local_q2_current};
    return frontend_remote_q2_effects_controls_read(&source,out,error);
}
static bool local_q2_interval(void *context,double *milliseconds,qa_error *error)
{
    (void)error;*milliseconds=(double)((frontend_particle_owner *)context)->q2_interval_ns/1000000.0;return true;
}
static bool local_q2_render_clock(void *context,uint64_t *wall,uint64_t *sequence,qa_error *error)
{
    (void)error;qa_frontend *frontend=((frontend_particle_owner *)context)->frontend;
    *wall=frontend->wall_time_ns/UINT64_C(1000000);*sequence=frontend->frame_number;return true;
}
static bool local_q2_trace(void *context,const qa_trace_query *query,qa_trace_result *out,qa_error *error)
{
    frontend_particle_owner *owner=context;
    return qa_world_trace(qa_application_world(owner->frontend->application),query,out,error);
}
static bool local_q2_create(qa_frontend *frontend,frontend_particle_owner *owner,qa_error *error)
{
    frontend_visual_owner_view media;qa_application_map_view map;
    if (!qa_application_map_read(frontend->application,&map) ||
        !frontend_visual_media_acquire(frontend,owner->provider,QA_GAME_Q2,&media,error)) return false;
    for (uint32_t seat=0;seat<frontend->options.seats;++seat) {
        qa_actor_id actor;
        if (frontend_seat_actor_read(frontend,seat,&actor) && qa_actor_id_equal(actor,owner->recipient)) { owner->seat=seat;break; }
    }
    size_t capacity=qa_actors_capacity(qa_world_actors(qa_application_world(frontend->application)));
    if (!qa_arena_reserve(&owner->q2_storage,capacity*(sizeof(*owner->q2_poses)+sizeof(uint32_t))+2*_Alignof(max_align_t),error)) return false;
    owner->q2_poses=qa_arena_alloc(&owner->q2_storage,capacity*sizeof(*owner->q2_poses),_Alignof(frontend_remote_q2_effects_pose),error);
    uint32_t *marks=qa_arena_alloc(&owner->q2_storage,capacity*sizeof(*marks),_Alignof(uint32_t),error);
    if (!owner->q2_poses || !marks) return false;
    qa_stamp_set_init(&owner->q2_pose_stamps,marks,capacity);qa_arena_seal(&owner->q2_storage);
    frontend_remote_q2_effects_source source={.session=qa_application_session(frontend->application),
        .actor_capacity=(uint32_t)capacity,.identity=owner->provider,.content_generation=map.revision,
        .profile=owner->q2_edition==QA_Q2_RERELEASE?FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE:FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC,
        .map=map.resource,.files=media.mounts,.images=owner->images,.materials=media.materials,
        .world=frontend->scene_world,.white=qa_scene_white(owner->images),.context=owner,
        .video_frame=frontend_material_movies_frontend_resolve,.video_context=frontend,
        .current=local_q2_source_current,.actor=local_q2_number_pose,.actor_pose=local_q2_pose,
        .actor_live=local_q2_actor_live,.viewer=local_q2_viewer,.model=local_q2_model,.sound=local_q2_sound,
        .controls=local_q2_controls,.frame_milliseconds=local_q2_interval,
        .render_clock=local_q2_render_clock,.trace=local_q2_trace};
    return frontend_remote_q2_effects_create(&source,&owner->q2_effects,error);
}
static bool particle_owner(qa_frontend *frontend, qa_actor_owner provider, qa_game_family family,
    qa_actor_owner world_source, uint64_t map_identity, qa_actor_id recipient,
    frontend_particle_owner **out, qa_error *error)
{
    *out = NULL;
    if (family == QA_GAME_Q2 && (!recipient.registry ||
            !delivery_world_current(frontend, world_source, map_identity)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 particle owner lost its actual delivered world");
    if (!frontend_particle_prepare(frontend, error)) return false;
    for (frontend_particle_owner *owner = frontend->particles->owners; owner; owner = owner->next)
        if (owner->provider == provider && owner->family == family &&
            owner->world_source == world_source && owner->map_identity == map_identity &&
            qa_actor_id_equal(owner->recipient, recipient)) { *out = owner; return true; }
    qa_q2_edition edition = QA_Q2_CLASSIC;
    uint64_t interval = 0;
    if (family==QA_GAME_Q2) {
        bool found;
        if (!qa_application_native_q2_source_clock_read(frontend->application,provider,
                &edition,&interval,&found,error)) return false;
        if (!found) {
            qa_error skipped = {0};
            qa_error_set(&skipped, QA_ERROR_UNSUPPORTED, 0, "Q2 effect producer has no admitted Source profile and interval");
            qa_application_feature_report(frontend->application, "Q2 particle producer", &skipped);
            return true;
        }
    }
    frontend_particle_owner *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "allocating provider particle pool");
    owner->frontend=frontend;owner->provider = provider; owner->family = family;
    owner->world_source = world_source; owner->map_identity = map_identity; owner->recipient = recipient;
    owner->q2_edition = edition; owner->q2_interval_ns = interval;
    qa_builtin_random_seed(&owner->random, 1);
    if (family == QA_GAME_Q1) {
        owner->q1 = calloc(1, sizeof(*owner->q1));
        if (owner->q1) frontend_fx_q1_state_initialize(owner->q1, 32, 24);
        const char *instance = qa_application_provider_instance(frontend->application, provider);
        const qa_launch_instance *source = instance ?
            qa_launch_snapshot_find(qa_application_launch(frontend->application), instance) : NULL;
        owner->q1_quakeworld = source && source->selection.clock.kind == QA_RULESET_QUAKEWORLD;
        const qa_product *product = source ? qa_catalog_product(qa_application_catalog(frontend->application),
            source->selection.product) : NULL;
        owner->q1_rerelease = product && product->edition == QA_EDITION_RERELEASE;
    }
    bool ok=particle_images(frontend,provider,family,&owner->images,error);
    if (ok) ok=family==QA_GAME_Q1 ? owner->q1 &&
        qa_scene_particle_image(owner->images,QA_GAME_Q1,&owner->particle_image,error) :
        local_q2_create(frontend,owner,error);
    if (!ok) { particle_owner_free(owner);return false; }
    owner->next = frontend->particles->owners; frontend->particles->owners = owner; *out = owner; return true;
}
void frontend_particle_retire(qa_frontend *frontend)
{
    if (!frontend->particles) return;
    while (frontend->particles->owners) {
        frontend_particle_owner *owner = frontend->particles->owners;
        frontend->particles->owners = owner->next;
        particle_owner_free(owner);
    }
    qa_arena_destroy(&frontend->particles->storage);
    free(frontend->particles); frontend->particles = NULL;
}
void frontend_particle_reset_round(qa_frontend *frontend)
{
    frontend_particle_state *state = frontend->particles;
    if (!state) return;
    for (frontend_particle_owner *owner = state->owners; owner; owner = owner->next) {
        if (owner->q2_effects) frontend_remote_q2_effects_reset(owner->q2_effects);
        owner->q2_pose_count=0;owner->entity_sampled=owner->entity_events_ready=false;
        qa_stamp_set_begin(&owner->q2_pose_stamps);
        if (owner->q1) {
            owner->q1->particles.count=0;
            memset(owner->q1->lights,0,sizeof(owner->q1->lights));
            memset(owner->q1->beams,0,sizeof(owner->q1->beams));
        }
        memset(owner->q1_beam_models,0,sizeof(owner->q1_beam_models));
    }
    state->sample_ns = state->previous_sample_ns = qa_session_elapsed(qa_application_session(frontend->application));
    if (state->q1_trails) memset(state->q1_trails, 0, state->q1_trail_capacity * sizeof(*state->q1_trails));
    if (state->visual_samples) memset(state->visual_samples,0,
        state->visual_sample_capacity*sizeof(*state->visual_samples));
}
static void q2_entity_clock(qa_frontend *frontend,frontend_particle_owner *owner,double milliseconds)
{
    if (owner->entity_sampled && owner->entity_frame==frontend->frame_number) return;
    owner->entity_frame_seconds=owner->entity_sampled && milliseconds>owner->entity_milliseconds ?
        (float)((milliseconds-owner->entity_milliseconds)*.001):0;
    owner->entity_advance=!owner->entity_sampled || milliseconds>owner->entity_milliseconds;
    owner->entity_milliseconds=milliseconds;owner->entity_frame=frontend->frame_number;
    owner->q2_pose_count=0;qa_stamp_set_begin(&owner->q2_pose_stamps);owner->entity_sampled=true;
}
static bool q2_entity_admit(qa_frontend *frontend,frontend_particle_owner *owner,
    const frontend_q2_entity_pose *pose,const qa_scene_world_input *world,
    qa_error *error)
{
    (void)frontend;(void)world;(void)error;
    if (qa_stamp_set_mark(&owner->q2_pose_stamps,pose->actor.slot))
        owner->q2_poses[owner->q2_pose_count++]=*pose;
    return true;
}
static bool q2_emitting_clock(qa_frontend *frontend,frontend_particle_owner *owner,qa_error *error)
{
    if (!owner->entity_events_ready || owner->entity_event_sample!=frontend->frame_number) {
        qa_clock_state source;
        if (!qa_session_clock(qa_application_session(frontend->application),owner->provider,&source))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 effects lost their emitting Source clock");
        owner->entity_events=!owner->entity_events_ready || owner->entity_server_frame!=source.frame.number;
        owner->entity_server_frame=source.frame.number;owner->entity_events_ready=true;
        uint64_t lower=source.frame.time_ns>owner->q2_interval_ns ?
            source.frame.time_ns-owner->q2_interval_ns:0;
        uint64_t fraction=source.debt_ns<owner->q2_interval_ns?source.debt_ns:owner->q2_interval_ns;
        owner->animation_server_ms=(double)source.frame.time_ns/1000000.0;
        owner->animation_client_ms=(double)(lower+fraction)/1000000.0;
        owner->entity_event_sample=frontend->frame_number;
    }
    return true;
}
static bool q2_beam_clock(qa_frontend *frontend,frontend_particle_owner *owner,
    uint64_t native_sample,bool physical,double *milliseconds,qa_error *error)
{
    if (physical && owner->provider==frontend->particles->clock_source) {
        *milliseconds=(double)native_sample/1000000.0;
        return true;
    }
    if (!q2_emitting_clock(frontend,owner,error)) return false;
    *milliseconds=owner->animation_client_ms;
    return true;
}
static bool q2_visual_entity_admit(qa_frontend *frontend,uint32_t seat,
    const qa_application_visual_view *view,const qa_scene_world_input *world,
    frontend_particle_owner **out,qa_error *error)
{
    *out=NULL;
    if (frontend_network_local_input_owned(frontend,seat)) return true;
    if (view->family!=QA_GAME_Q2) return true;
    qa_actor_id recipient;
    if (!frontend_seat_actor_read(frontend,seat,&recipient)) return true;
    qa_collision_geometry *geometry=qa_world_geometry(qa_application_world(frontend->application));
    const qa_launch_choices *choices=qa_launch_snapshot_choices(qa_application_launch(frontend->application));
    const qa_launch_binding *binding=choices?qa_launch_binding_for(choices,
        (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,""):NULL;
    qa_actor_owner primary=0;
    if (!geometry || !binding || !qa_application_provider_owner(frontend->application,binding->instance,&primary)) return true;
    frontend_particle_owner *owner;
    if (!particle_owner(frontend,view->provider,QA_GAME_Q2,primary,
        qa_collision_map_identity(geometry),recipient,&owner,error)) return false;
    if (!owner) return true;
    frontend_q2_sample sample;
    if (!q2_owner_sample(frontend,owner,&sample,error)) return false;
    q2_entity_clock(frontend,owner,sample.milliseconds);
    if (!q2_emitting_clock(frontend,owner,error)) return false;
    frontend_particle_state *state=frontend->particles;
    frontend_visual_sample *visual=state->visual_samples+view->actor.slot;
    if (qa_actor_id_equal(visual->actor,view->actor) &&
        (!visual->animation.ready || visual->animation_sample_frame!=frontend->frame_number)) {
        const qa_application_visual_view *raw=&visual->view;
        bool continuous=visual->animation.ready && visual->animation_provider==raw->provider &&
            (visual->animation_source_frame==owner->entity_server_frame ||
             visual->animation_source_frame+1==owner->entity_server_frame) &&
            !memcmp(visual->animation_models,raw->visual.models,sizeof(raw->visual.models)) &&
            frontend_q2_lerp_near(visual->animation_origin,raw->body.origin,512);
        if (!continuous || visual->animation_source_frame!=owner->entity_server_frame) {
            visual->animation_previous_frame=continuous?visual->animation.frame:(raw->visual.frame>=0?(uint32_t)raw->visual.frame:0);
            frontend_q2_animation_commit(&visual->animation,raw->visual.frame>=0?(uint32_t)raw->visual.frame:0,
                raw->visual.render_flags&(UINT32_C(1)<<22)?(raw->visual.old_frame>=0?(uint32_t)raw->visual.old_frame:0):visual->animation.frame,
                raw->visual.render_flags,owner->animation_server_ms,continuous);
        }
        visual->animation_provider=raw->provider;visual->animation_source_frame=owner->entity_server_frame;
        memcpy(visual->animation_models,raw->visual.models,sizeof(raw->visual.models));
        visual->animation_origin=raw->body.origin;visual->animation_server_ms=owner->animation_server_ms;
        visual->animation_client_ms=owner->animation_client_ms;
        visual->animation_tick_ms=(double)owner->q2_interval_ns/1000000.0;
        visual->animation_edition=owner->q2_edition;visual->animation_sample_frame=frontend->frame_number;
    }
    *out=owner;
    if (!view->visual.effects && !(view->visual.render_flags&128u)) return true;
    uint32_t model=0,event=0;
    const qa_actor_record *record=qa_actors_get(qa_world_actors(qa_application_world(frontend->application)),view->actor);
    if (state->source_ready && record && record->owner==state->clock_source && record->has_source &&
        record->source_slot<state->source_entity_count &&
        qa_actor_id_equal(state->source_entities[record->source_slot].current.actor,view->actor)) {
        model=state->source_entities[record->source_slot].current.models[0];
        event=state->source_entities[record->source_slot].current.event;
    }
    frontend_q2_entity_pose pose={.actor=view->actor,.model_index=model,.effects=view->visual.effects,
        .event=event,.frame=view->visual.frame,.origin=view->body.origin,.angles=view->body.angles,
        .model_present=view->visual.has_inline_model || qa_strings_text(qa_session_strings(qa_application_session(frontend->application)), view->visual.models[0]).size,
        .model_identity=view->visual.models[0]};
    if (!q2_entity_admit(frontend,owner,&pose,world,error)) return false;
    *out=owner;return true;
}
static bool entity_effects_prepare(qa_frontend *frontend,uint32_t seat,qa_actor_id recipient,
    const qa_scene_world_input *world,qa_error *error)
{
    frontend_particle_state *state=frontend->particles;
    if (state->client_clock && state->source_ready && !state->client_pending) {
        qa_collision_geometry *geometry=qa_world_geometry(qa_application_world(frontend->application));
        frontend_particle_owner *owner;
        if (!geometry || !particle_owner(frontend,state->clock_source,QA_GAME_Q2,state->clock_source,
            qa_collision_map_identity(geometry),recipient,&owner,error)) return false;
        if (owner) {
            frontend_q2_sample sample;
            if (!q2_owner_sample(frontend,owner,&sample,error)) return false;
            q2_entity_clock(frontend,owner,sample.milliseconds);
            bool events=!owner->entity_events_ready || owner->entity_server_frame!=state->server_frame;
            for (size_t i=0;i<state->source_entity_count;++i) {
                const frontend_q2_entity_sample *held=state->source_entities+i;
                if (!held->current.actor.registry) continue;
                qa_application_native_q2_entity_sample entity;uint32_t old_frame;
                frontend_q2_lerp_entity(&held->previous,&held->current,1-sample.back_lerp,&entity,&old_frame);
                frontend_q2_entity_pose pose={.actor=entity.actor,.number=entity.source_slot,.event=entity.event,
                    .model_index=entity.models[0],.effects=entity.effects,.frame=(int32_t)entity.frame,
                    .origin=entity.origin,.angles=entity.angles,.model_present=entity.models[0]!=0};
                if (!q2_entity_admit(frontend,owner,&pose,world,error)) return false;
            }
            owner->entity_events_ready=true;owner->entity_server_frame=state->server_frame;
            owner->entity_event_sample=frontend->frame_number;owner->entity_events=events;
        }
    }
    const qa_actor_record *record;uint32_t cursor=0;
    qa_actor_registry *actors=qa_world_actors(qa_application_world(frontend->application));
    while (qa_actors_next(actors,&cursor,&record)) {
        if (frontend_native_q3_actor_admitted(frontend,seat,record->id)) continue;
        qa_application_visual_view view;bool found;
        if (!frontend_particle_visual_read(frontend,record->id,&view,&found,error)) return false;
        if (!found) continue;
        const char *path = qa_strings_cstr(qa_session_strings(qa_application_session(frontend->application)), view.visual.models[0]);
        size_t length = path ? strlen(path) : 0;
        bool mdl = length >= 4 && !strcmp(path + length - 4, ".mdl");
        if (view.family==QA_GAME_Q1 || view.q1_effects || mdl) {
            const qa_model *source = NULL;
            if (!view.visual.has_inline_model && path && *path && path[0] != '*' &&
                !(length >= 4 && !strcmp(path + length - 4, ".bsp"))) {
                frontend_visual_model_view model;
                if (!frontend_visual_model_acquire(frontend,view.provider,view.family,view.visual.models[0],
                        view.model_resources[0],&model,error)) return false;
                source = model.model;
            }
            if (!frontend_particle_q1_entity(frontend,&view,source,error)) return false;
        }
        if (view.family==QA_GAME_Q2) {
            float back_lerp;
            frontend_particle_owner *owner;
            if (!frontend_particle_q2_entity_sample(frontend,&view,&back_lerp,error) ||
                !q2_visual_entity_admit(frontend,seat,&view,world,&owner,error)) return false;
            state->visual_samples[record->id.slot].q2_owners[seat]=owner;
        }
    }
    return true;
}
bool frontend_particle_q2_entity(qa_frontend *frontend,uint32_t seat,
    const qa_application_visual_view *view,const qa_scene_world_input *world,
    qa_scene_frame *frame,bool *beam,qa_error *error)
{
    *beam=false;
    if (view->family!=QA_GAME_Q2 || !(view->visual.render_flags&128u) || view->model_beam) return true;
    frontend_particle_owner *owner=frontend->particles->visual_samples[view->actor.slot].q2_owners[seat];
    if (!owner) return true;
    if ((view->visual.render_flags&128u) && !view->model_beam) {
        if (!frontend_remote_q2_effects_entity_beam(owner->q2_effects,&world->view,
            view->body.origin,view->previous_origin,(uint32_t)view->visual.skin,
            view->visual.frame,frame,error)) return false;
        *beam=true;
    }
    return true;
}
static bool q1_temporary(const qa_builtin_event *event, qa_string_id colored_explosion, qa_q1_temp *out)
{
    *out = (qa_q1_temp){.kind = QA_Q1_TEMP_POINT, .count = 1};
    qa_vec3 origin = event->origin;
    switch (event->kind) {
    case QA_BUILTIN_IMPACT:
        if (event->code == 2) {
            out->type = 2;
            out->count = (uint8_t)(int32_t)event->value;
            if (event->flags & QA_Q1_IMPACT_GROUPED) {
                origin = event->end;
            }
        } else if (event->code == 3) out->type = 0;
        else if (event->code == 4) out->type = 1;
        else if (event->code == 7 || event->code == 8 || event->code == 10)
            out->type = (uint8_t)event->code;
        else return false;
        break;
    case QA_BUILTIN_EXPLOSION: out->type = event->code == 1 ? 4 : event->code == 10 ? 10 : 3; break;
    case QA_BUILTIN_TELEPORT: out->type = 11; break;
    case QA_BUILTIN_BEAM:
        if (event->code < 1 || event->code > 4) return false;
        out->kind = QA_Q1_TEMP_BEAM;
        out->type = event->code == 1 ? 5 : event->code == 2 ? 6 : event->code == 3 ? 9 : 13;
        out->end[0] = event->end.x; out->end[1] = event->end.y; out->end[2] = event->end.z;
        break;
    case QA_BUILTIN_EFFECT:
        if (event->resource!=colored_explosion || event->count <= 0) return false;
        out->kind = QA_Q1_TEMP_COLORS;
        out->color_start = (uint8_t)event->code; out->color_length = (uint8_t)event->count;
        break;
    default: return false;
    }
    out->origin[0] = origin.x; out->origin[1] = origin.y; out->origin[2] = origin.z;
    return true;
}

static bool q1_temporary_apply(qa_frontend *frontend, frontend_particle_owner *owner,
    const qa_q1_temp *event, qa_actor_id actor, bool quakeworld, double seconds,
    bool received, qa_error *error)
{
    if (event->kind == QA_Q1_TEMP_BEAM) {
        const char *path = frontend_fx_q1_beam_model(event->type);
        if (!path) return frontend_fail(error, QA_ERROR_FORMAT, "Q1 beam has no Source model");
        uint8_t model_index = (uint8_t)frontend_fx_q1_beam_index(event->type);
        if (!owner->q1_beam_models[model_index]) {
            frontend_visual_model_view model;
            qa_string_id path_name;
            if (!qa_strings_intern_cstr(qa_session_strings(qa_application_session(frontend->application)),path,&path_name,error) ||
                !frontend_visual_model_acquire(frontend, owner->provider, QA_GAME_Q1,
                    path_name, NULL, &model, error)) return false;
            owner->q1_beam_models[model_index] = model.scene;
        }
    }
    const char *path;
    if (!frontend_fx_q1_state_temporary(owner->q1, &owner->random, event, actor,
            received, quakeworld, seconds, &path))
        return frontend_fail(error, QA_ERROR_FORMAT, "Q1 temporary effect has no Source recipe");
    if (path) {
        qa_string_id resource;
        if (!qa_strings_intern_cstr(qa_session_strings(qa_application_session(frontend->application)),
                path, &resource, error)) return false;
        qa_builtin_event sound = {.kind = QA_BUILTIN_SOUND, .family = QA_GAME_Q1,
            .provider = owner->provider, .resource = resource,
            .origin = {event->origin[0], event->origin[1], event->origin[2]}, .volume = 1, .attenuation = 1};
        if (!received) return frontend_event_sound(frontend, &sound, error);
        for (uint32_t seat = 0; seat < frontend->options.seats; ++seat) {
            if (frontend_network_local_input_owned(frontend,seat)) continue;
            qa_actor_id recipient;
            if (frontend_seat_actor_read(frontend, seat, &recipient) && qa_actor_id_equal(recipient, owner->recipient))
                return frontend_particle_sound(frontend, &sound, seat, recipient, error);
        }
    }
    return true;
}

static bool q1_particle_event(qa_frontend *frontend, const qa_builtin_event *event, qa_error *error)
{
    qa_q1_temp temporary;
    qa_string_id colored_explosion=qa_application_ui_names_read(frontend->application)->colored_explosion;
    bool raw = event->kind == QA_BUILTIN_PARTICLES;
    bool blood = event->kind == QA_BUILTIN_IMPACT && event->code == 1;
    if (!raw && !blood && !q1_temporary(event, colored_explosion, &temporary)) return true;
    frontend_particle_owner *owner;
    if (!particle_owner(frontend, event->provider, QA_GAME_Q1, 0, 0,
            (qa_actor_id){0}, &owner, error)) return false;
    double seconds = (double)frontend->particles->sample_ns / 1e9;
    if (raw) frontend_fx_q1_particle_event(&owner->q1->particles, &owner->random, event->origin,
        event->direction, event->code, event->count, seconds);
    else if (blood) {
        if (owner->q1_quakeworld) {
            temporary = (qa_q1_temp){.kind = QA_Q1_TEMP_POINT, .type = 12,
                .count = event->flags & QA_Q1_IMPACT_GROUPED ? (uint8_t)(int32_t)event->value : 1,
                .origin = {event->origin.x, event->origin.y, event->origin.z}};
            (void)frontend_fx_q1_temporary_particles(&owner->q1->particles, &owner->random, &temporary, true, seconds);
        } else frontend_fx_q1_particle_event(&owner->q1->particles, &owner->random, event->origin,
            qa_v3(0, 0, 0), 73, (int32_t)(event->value * 2), seconds);
    } else return q1_temporary_apply(frontend, owner, &temporary, event->actor,
        owner->q1_quakeworld, seconds, false, error);
    return true;
}

bool frontend_particle_q1_temporary(qa_frontend *frontend, qa_actor_owner provider,
    qa_actor_id recipient, const qa_q1_temp *temporary, qa_actor_id actor, bool quakeworld, qa_error *error)
{
    frontend_particle_owner *owner;
    if (!particle_owner(frontend, provider, QA_GAME_Q1, provider, frontend->map_revision,
            recipient, &owner, error)) return false;
    double seconds = (double)qa_session_elapsed(qa_application_session(frontend->application)) / 1e9;
    return q1_temporary_apply(frontend, owner, temporary, actor, quakeworld, seconds, true, error);
}

bool frontend_particle_q1_entity(qa_frontend *frontend, const qa_application_visual_view *view,
    const qa_model *model, qa_error *error)
{
    uint32_t effects = view->q1_effects | (view->family == QA_GAME_Q1 ? (uint32_t)view->visual.effects : 0);
    uint32_t flags = model && model->format == QA_MODEL_MDL ? (uint32_t)model->flags : 0;
    if (!(effects & UINT32_C(0xff)) && !(flags & UINT32_C(0xf7))) return true;
    if (!frontend_particle_prepare(frontend, error)) return false;
    frontend_particle_state *state = frontend->particles;
    size_t slot = view->actor.slot;
    if (slot >= state->q1_trail_capacity) {
        qa_allocation_gate_capacity_exhausted();
        return frontend_fail(error,QA_ERROR_MEMORY,"Q1 trail actor capacity exhausted");
    }
    frontend_q1_trail *trail = state->q1_trails + slot;
    bool same = qa_actor_id_equal(trail->actor, view->actor) && trail->provider == view->provider && trail->model == model;
    if (same && trail->frame == frontend->frame_number) return true;
    qa_vec3 origin = view->body.origin, start = same ? trail->origin : origin;
    qa_vec3 delta = qa_vec_sub(origin, start);
    if (fabsf(delta.x) > 100 || fabsf(delta.y) > 100 || fabsf(delta.z) > 100) start = origin;
    *trail = (frontend_q1_trail){.actor = view->actor, .provider = view->provider,
        .model = model, .origin = origin, .frame = frontend->frame_number};
    frontend_particle_owner *owner;
    if (!particle_owner(frontend, view->provider, QA_GAME_Q1, 0, 0, (qa_actor_id){0}, &owner, error)) return false;
    double seconds = (double)state->sample_ns / 1e9;
    qa_vec3 light_origin;
    frontend_fx_q1_light_recipe recipe;
    bool light_present=frontend_fx_q1_entity_effects(&owner->q1->particles,&owner->random,start,origin,
        view->body.angles,effects,flags,owner->q1_quakeworld,owner->q1_rerelease,
        seconds,&light_origin,&recipe);
    if (light_present) {
        frontend_fx_q1_state_light(owner->q1, view->actor, 0, light_origin, seconds, &recipe);
    }
    return true;
}
bool frontend_particle_q2_temporary(qa_frontend *frontend,
    const qa_application_protocol_event *message,const qa_application_q2_audience *audience,
    const qa_q2_temp_entity *temporary,const qa_actor_id *actors,qa_error *error)
{
    if (!q2_delivery_admit(frontend,message->provider,audience)) return true;
    uint64_t client,server;float back;bool physical;
    if (!client_sample(frontend,&client,&server,&back,&physical,error)) return false;
    for (uint32_t seat=0;seat<frontend->options.seats;++seat) {
        if (frontend_network_local_input_owned(frontend,seat)) continue;
        qa_actor_id recipient;if (!frontend_seat_actor_read(frontend,seat,&recipient)) continue;
        bool received=false;
        for (size_t i=0;i<audience->count;++i) received|=qa_actor_id_equal(recipient,audience->recipients[i].actor);
        if (!received) continue;
        frontend_particle_owner *owner;
        if (!particle_owner(frontend,message->provider,QA_GAME_Q2,audience->world_source,
            audience->map_identity,recipient,&owner,error)) return false;
        if (!owner) continue;
        double time;
        if (!q2_beam_clock(frontend,owner,client,physical,&time,error) ||
            !frontend_remote_q2_effects_temporary(owner->q2_effects,temporary,actors,time,
                (double)audience->source_time_ns/1000000.0,error)) return false;
    }
    return true;
}
static qa_q2_temp_field q2_temp_vector(qa_q2_temp_field_name name,qa_vec3 value)
{ return (qa_q2_temp_field){.name=name,.kind=QA_Q2_TEMP_VECTOR,.value.vector={value.x,value.y,value.z}}; }
static qa_q2_temp_field q2_temp_integer(qa_q2_temp_field_name name,int32_t value)
{ return (qa_q2_temp_field){.name=name,.kind=QA_Q2_TEMP_INTEGER,.value.integer=value}; }
static bool q2_builtin_beam(qa_frontend *frontend,const qa_builtin_event *event,
    const qa_application_q2_audience *audience,const frontend_q2_beam_recipe *recipe,
    uint64_t sample,bool physical,qa_error *error)
{
    if (!q2_delivery_admit(frontend,event->provider,audience)) return true;
    for (uint32_t seat=0;seat<frontend->options.seats;++seat) {
        if (frontend_network_local_input_owned(frontend,seat)) continue;
        qa_actor_id recipient;if (!frontend_seat_actor_read(frontend,seat,&recipient)) continue;
        bool received=false;
        for (size_t i=0;i<audience->count;++i) received|=qa_actor_id_equal(recipient,audience->recipients[i].actor);
        if (!received) continue;
        frontend_particle_owner *owner;
        if (!particle_owner(frontend,event->provider,QA_GAME_Q2,audience->world_source,
            audience->map_identity,recipient,&owner,error)) return false;
        if (!owner) continue;
        double time;
        if (!q2_beam_clock(frontend,owner,sample,physical,&time,error) ||
            !frontend_remote_q2_effects_beam(owner->q2_effects,recipe,event->actor,event->other,
                event->origin,event->end,time,error)) return false;
    }
    return true;
}
bool frontend_particle_events(qa_frontend *frontend, qa_error *error)
{
    particle_clients_retire(frontend);
    if (frontend->particles) frontend->particles->sample_ns =
        qa_session_elapsed(qa_application_session(frontend->application));
    uint64_t q2_sample, server;
    float back_lerp;
    bool physical;
    if (!client_sample(frontend, &q2_sample, &server, &back_lerp, &physical, error)) return false;
    if (!frontend_native_q2_messages(frontend,error)) return false;
    for (uint64_t i = qa_application_events_local_first(frontend->application);
        i < qa_application_events_next(frontend->application); ++i) {
        qa_application_event_view output;
        if (!qa_application_event_read(frontend->application, &(qa_application_event_cursor){.id = i, .projection = 0}, &output) ||
            output.kind != QA_APPLICATION_EVENT_BUILTIN) continue;
        qa_builtin_event event = *output.value.builtin;
        if (event.family==QA_GAME_Q2 && (event.kind==QA_BUILTIN_TELEPORT ||
            (event.kind==QA_BUILTIN_Q2_ENTITY_EVENT && (event.code==6 || event.code==7))) &&
            frontend->particles && (size_t)event.actor.slot<frontend->particles->visual_sample_capacity) {
            frontend_visual_sample *visual=frontend->particles->visual_samples+event.actor.slot;
            if (qa_actor_id_equal(visual->actor,event.actor)) visual->animation.ready=false;
        }
        if (event.family == QA_GAME_Q1) {
            if (!q1_particle_event(frontend, &event, error)) return false;
        } else if (event.family==QA_GAME_Q2 && event.kind==QA_BUILTIN_BEAM) {
            qa_application_q2_effect_name name=qa_application_q2_effect_name_read(frontend->application,event.resource);
            frontend_q2_beam_recipe recipe;qa_q2_temp_entity temporary;
            bool segment=name.beam && frontend_q2_temporary_segment((uint32_t)name.beam-1u,event.origin,event.end,&temporary);
            if (segment || (name.beam && frontend_q2_beam_recipe_read((uint32_t)name.beam-1u,event.direction,
                event.value>0?event.value:.1,name.independent,&recipe))) {
                qa_application_q2_audience audience=*output.q2_audience;
                if (segment) {
                    qa_application_protocol_event message={.provider=event.provider,
                        .dialect=audience.source_frame.kind,.time_ns=event.time_ns};
                    if (!frontend_particle_q2_temporary(frontend,&message,&audience,&temporary,NULL,error)) return false;
                } else if (!q2_builtin_beam(frontend,&event,&audience,&recipe,q2_sample,physical,error)) return false;
            }
        } else if (event.family == QA_GAME_Q2 && event.kind == QA_BUILTIN_PARTICLES) {
            qa_application_q2_audience audience=*output.q2_audience;
            qa_q2_temp_entity temporary={.type=(uint8_t)event.code,.field_count=2,
                .fields={q2_temp_vector(QA_Q2_TEMP_POSITION1,event.origin),
                    q2_temp_vector(QA_Q2_TEMP_DIRECTION,event.direction)}};
            qa_application_protocol_event message={.provider=event.provider,
                .dialect=audience.source_frame.kind,.time_ns=event.time_ns};
            if (!frontend_particle_q2_temporary(frontend,&message,&audience,&temporary,NULL,error)) return false;
        }
    }
    for (uint64_t i = qa_application_events_local_first(frontend->application);
        i < qa_application_events_next(frontend->application); ++i) {
        qa_application_event_view output;
        if (!qa_application_event_read(frontend->application, &(qa_application_event_cursor){.id = i, .projection = 0}, &output) ||
            output.kind != QA_APPLICATION_EVENT_Q2_MAP) continue;
        qa_application_q2_map_event source = *output.value.q2_map;
        if (source.event.kind != QA_Q2_MAP_STEAM && source.event.kind != QA_Q2_MAP_FORCE_WALL) continue;
        qa_application_q2_audience audience=*output.q2_audience;
        qa_q2_temp_entity temporary={.type=source.event.kind==QA_Q2_MAP_FORCE_WALL?QA_Q2_TE_FORCEWALL:QA_Q2_TE_STEAM,
            .field_count=source.event.kind==QA_Q2_MAP_FORCE_WALL?3:7};
        temporary.fields[0]=q2_temp_vector(QA_Q2_TEMP_POSITION1,source.event.origin);
        if (source.event.kind==QA_Q2_MAP_FORCE_WALL) {
            temporary.fields[1]=q2_temp_vector(QA_Q2_TEMP_POSITION2,source.event.direction);
            temporary.fields[2]=q2_temp_integer(QA_Q2_TEMP_COLOR,source.event.style);
        } else {
            temporary.fields[1]=q2_temp_vector(QA_Q2_TEMP_DIRECTION,source.event.direction);
            temporary.fields[2]=q2_temp_integer(QA_Q2_TEMP_COLOR,source.event.style);
            temporary.fields[3]=q2_temp_integer(QA_Q2_TEMP_COUNT,source.event.count);
            temporary.fields[4]=q2_temp_integer(QA_Q2_TEMP_ENTITY1,source.event.slot);
            temporary.fields[5]=q2_temp_integer(QA_Q2_TEMP_ENTITY2,(int32_t)source.event.value);
            temporary.fields[6]=q2_temp_integer(QA_Q2_TEMP_TIME,(int32_t)source.event.duration);
        }
        qa_application_protocol_event message={.provider=source.provider,
            .dialect=audience.source_frame.kind,.time_ns=audience.source_time_ns};
        if (!frontend_particle_q2_temporary(frontend,&message,&audience,&temporary,NULL,error)) return false;
    }
    return true;
}
static bool local_q2_sample(qa_frontend *frontend,frontend_particle_owner *owner,
    const qa_scene_world_input *world,frontend_remote_q2_effects_sample *out,qa_error *error)
{
    frontend_q2_sample time;
    if (!q2_owner_sample(frontend,owner,&time,error)) return false;
    q2_entity_clock(frontend,owner,time.milliseconds);
    if (!q2_emitting_clock(frontend,owner,error)) return false;
    const frontend_particle_cvars *bindings=NULL;
    frontend_source_client_registry client;bool found;
    if (!frontend_source_client_registry_read(frontend,owner->seat,&client,&found,error)) return false;
    if (found) bindings=particle_cvars_bind(frontend,owner->seat,client.cvars,error);
    const qa_cvar_view *hand=bindings?qa_cvars_read(client.cvars,bindings->hand):NULL;
    *out=(frontend_remote_q2_effects_sample){.milliseconds=time.milliseconds,
        .server_milliseconds=owner->animation_server_ms,.fraction=1-time.back_lerp,
        .frame_sequence=frontend->frame_number,.entities=owner->q2_poses,.entity_count=owner->q2_pose_count,
        .view=world->view,.viewer=owner->recipient,.hand=hand?hand->integer:0,
        .hardware=frontend->gl!=NULL,.frame_seconds=owner->entity_frame_seconds,
        .world_input=world};
    qa_body_state body;
    if (qa_world_body_read(qa_application_world(frontend->application),owner->recipient,&body,error)) {
        out->viewer_origin=body.origin;out->viewer_origin_present=true;
    }
    return true;
}
bool frontend_particle_world(qa_frontend *frontend, uint32_t seat,
    qa_scene_world_input *world, qa_error *error)
{
    if (!frontend || !world || seat>=frontend->options.seats || frontend->resource_inventory)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 explosion lights require their actual physical seat");
    if (frontend_network_local_input_owned(frontend,seat)) return true;
    if (!frontend_particle_prepare(frontend,error)) return false;
    qa_actor_id recipient;
    if (!frontend_seat_actor_read(frontend,seat,&recipient)) return true;
    if (!entity_effects_prepare(frontend,seat,recipient,world,error)) return false;
    qa_scene_light pending[64];
    double seconds = (double)qa_session_elapsed(qa_application_session(frontend->application)) / 1e9;
    for (frontend_particle_owner *owner=frontend->particles->owners;owner;owner=owner->next) {
        size_t count=0;const qa_scene_light *effect_lights=pending;
        if (owner->q1) {
            if (owner->recipient.registry && !qa_actor_id_equal(owner->recipient,recipient)) continue;
            for (size_t i=0;i<32;++i) {
                const frontend_fx_q1_light *light=owner->q1->lights+i;
                float radius=light->radius-light->decay*(float)fmax(0,seconds-light->born);
                if (!light->active || light->die<seconds || radius<=0) continue;
                pending[count++]=(qa_scene_light){.origin=light->origin,.color=light->color,
                    .radius=radius,.minimum=light->minimum,.scale=1,.additive=true,
                    .family=QA_GAME_Q1,.identity=light->identity};
            }
        } else {
            if (!owner->q2_effects || !qa_actor_id_equal(owner->recipient,recipient) ||
                !delivery_world_current(frontend,owner->world_source,owner->map_identity)) continue;
            frontend_remote_q2_effects_sample sample;
            if (!local_q2_sample(frontend,owner,world,&sample,error)) return false;
            if (owner->entity_events && owner->q2_event_frame!=frontend->frame_number) {
                if (!frontend_remote_q2_effects_frame_particles(owner->q2_effects,&sample,error)) return false;
                owner->q2_event_frame=frontend->frame_number;
            }
            if (!frontend_remote_q2_effects_prepare(owner->q2_effects,&sample,&effect_lights,&count,error)) return false;
        }
        if (!count) continue;
        if (world->light_count>SIZE_MAX-count ||
            world->light_count+count>SIZE_MAX/sizeof(qa_scene_light))
            return frontend_fail(error,QA_ERROR_MEMORY,"Q2 explosion light array exceeds native storage");
        qa_scene_light *lights=qa_arena_alloc(&frontend->frame.storage,
            (world->light_count+count)*sizeof(*lights),_Alignof(qa_scene_light),error);
        if (!lights) return false;
        if (world->light_count) memcpy(lights,world->lights,world->light_count*sizeof(*lights));
        memcpy(lights+world->light_count,effect_lights,count*sizeof(*lights));
        world->lights=lights; world->light_count+=count;
    }
    return true;
}
bool frontend_particle_draw(qa_frontend *frontend, uint32_t seat, const qa_scene_world_input *world, qa_error *error)
{
    if (!frontend || !world || seat >= frontend->options.seats)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Particle draw requires its actual physical seat");
    if (frontend_network_local_input_owned(frontend,seat)) return true;
    const qa_scene_view *view = &world->view;
    if (!frontend->particles && !frontend_particle_prepare(frontend,error)) return false;
    uint64_t now = qa_session_elapsed(qa_application_session(frontend->application));
    double seconds = (double)now / 1e9;
    for (frontend_particle_owner *owner = frontend->particles->owners; owner; owner = owner->next) {
        if (owner->q1 && owner->recipient.registry) {
            qa_actor_id recipient;
            if (!frontend_seat_actor_read(frontend, seat, &recipient) ||
                !qa_actor_id_equal(recipient, owner->recipient)) continue;
        }
        if (owner->q1) {
            qa_actor_id viewer={0};
            (void)frontend_seat_actor_read(frontend,seat,&viewer);
            for (size_t i=0;i<24;++i) {
                const frontend_fx_q1_beam *beam=owner->q1->beams+i;
                if (!beam->active || beam->die<seconds) continue;
                uint8_t model_index=(uint8_t)frontend_fx_q1_beam_index(beam->type);
                qa_vec3 start=beam->start;
                if (viewer.registry && qa_actor_id_equal(viewer,beam->actor)) {
                    qa_body_state body;
                    if (!qa_world_body_read(qa_application_world(frontend->application),viewer,&body,error)) return false;
                    start=body.origin;
                }
                frontend_fx_q1_beam_cursor cursor;frontend_fx_q1_beam_begin(&cursor,start,beam->end);
                qa_model_transform placement;
                while (frontend_fx_q1_beam_next(&cursor,&owner->random,&placement)) {
                    qa_vec3 origin=qa_v3(placement.origin[0],placement.origin[1],placement.origin[2]);
                    qa_scene_model_input input={.view=*view,.transform=placement,.previous_origin=origin,
                        .family=QA_GAME_Q1,.color={1,1,1,1},.seconds=world->seconds,
                        .source_path=frontend_fx_q1_beam_model(beam->type),.entity=beam->actor.slot,
                        .identity_light=world->identity_light,.ambient={1,1,1},
                        .video_frame=world->video_frame,.video_context=world->video_context};
                    if (frontend->scene_world && !qa_scene_world_sample_light_input(frontend->scene_world,
                            world,origin,&input.ambient,&input.directed,&input.light_direction,error)) return false;
                    if (!frontend_legacy_model_input(frontend->scene_world,world,&input,error) ||
                        !qa_scene_model_submit(owner->q1_beam_models[model_index],&input,&frontend->frame,error)) return false;
                }
            }
        }
        if (owner->q2_effects) {
            qa_actor_id recipient;
            if (!frontend_seat_actor_read(frontend,seat,&recipient) || !qa_actor_id_equal(recipient,owner->recipient) ||
                !delivery_world_current(frontend,owner->world_source,owner->map_identity)) continue;
            frontend_remote_q2_effects_sample effects;
            const qa_scene_light *lights;size_t count;
            if (!local_q2_sample(frontend,owner,world,&effects,error) ||
                !frontend_remote_q2_effects_prepare(owner->q2_effects,&effects,&lights,&count,error) ||
                !frontend_remote_q2_effects_draw(owner->q2_effects,&effects,true,true,&frontend->frame,error)) return false;
            continue;
        }
        qa_bytes palette;
        qa_game_family family = owner->family == QA_GAME_Q1 ? QA_GAME_Q1 : QA_GAME_Q2;
        if (!qa_scene_resources_palette(owner->images, family, &palette, error)) return false;
        size_t particle_count = owner->q1->particles.count;
        qa_scene_particle_sample *particles = qa_scene_particles_alloc(&frontend->frame, particle_count, error);
        if (particle_count && !particles) return false;
        qa_scene_particle_batch batch = {.view = *view, .family = family,
            .image = owner->particle_image, .samples = particles};
        for (size_t i = particle_count; i > 0; --i) {
            qa_vec3 origin; float alpha = 1; uint32_t index;
            const qa_scene_q1_particle_state *particle=&owner->q1->particles.values.q1[i-1];
            if (particle->die<seconds) continue;
            origin=particle->origin;index=particle->color&255;
            qa_vec4 color = {palette.data[index * 3] / 255.0f, palette.data[index * 3 + 1] / 255.0f,
                palette.data[index * 3 + 2] / 255.0f, alpha};
            particles[batch.count++] = (qa_scene_particle_sample){.origin = origin, .color = color};
        }
        if (!qa_scene_particles(&frontend->frame, &batch, error)) return false;
    }
    return true;
}
bool frontend_particle_advance(qa_frontend *frontend, qa_error *error)
{
    particle_clients_retire(frontend);
    if (!frontend->particles) return true;
    frontend_particle_state *state = frontend->particles;
    float gravity = 0;
    bool gravity_read = false;
    for (frontend_particle_owner *owner = state->owners; owner; owner = owner->next) {
        if (!owner->q1) continue;
        if (!gravity_read) {
            qa_actor_owner world;
            if (!particle_world_owner(frontend, &world) ||
                !qa_application_provider_gravity(frontend->application, world, &gravity) || !isfinite(gravity))
                return frontend_fail(error, QA_ERROR_NOT_FOUND, "Particle world has no authoritative gravity");
            gravity_read = true;
        }
        owner->gravity = gravity;
    }
    uint64_t now = qa_session_elapsed(qa_application_session(frontend->application));
    double seconds = (double)now / 1e9;
    double elapsed = now >= state->previous_sample_ns ? (double)(now - state->previous_sample_ns) / 1e9 : 0;
    state->sample_ns = state->previous_sample_ns = now;
    for (frontend_particle_owner *owner = state->owners; owner; owner = owner->next) {
        if (owner->q1) frontend_fx_q1_advance(&owner->q1->particles,seconds,elapsed,owner->gravity);
    }
    return true;
}
