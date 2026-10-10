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
#include "q2_temporary_beams.h"
#include "selected_effects_q1_temporary.h"
#include "legacy_render_policy.h"
#include "native_q3_client.h"
#include "qa/game_q1_weapons.h"

enum { FRONTEND_PARTICLE_CAPACITY = 4096, FRONTEND_STEAM_CAPACITY = 32,
       FRONTEND_Q2_IMPACT_CAPACITY = 32, FRONTEND_Q2_LASER_CAPACITY = 32,
       FRONTEND_Q2_DLIGHT_CAPACITY = 32 };
typedef struct frontend_q2_dlight {
    qa_vec3 origin;
    qa_actor_id actor;
    uint32_t source_entity;
    uint8_t kind;
    int64_t birth_milliseconds,end_milliseconds;
    bool active;
} frontend_q2_dlight;
typedef struct frontend_q2_laser {
    qa_vec3 start, end;
    int64_t end_milliseconds;
    uint32_t color;
    bool active;
} frontend_q2_laser;
typedef struct frontend_q2_impact {
    qa_vec3 origin;
    int64_t start_milliseconds;
    uint8_t kind; /* Smoke/flash, rocket/BFG/big poly, blaster misc, welding light. */
    uint8_t frames, base_frame;
    float pitch, yaw, light_radius;
    bool light_only, no_light;
} frontend_q2_impact;
typedef struct frontend_steam {
    qa_q2_map_event event;
    uint64_t end_ns, next_ns;
    uint8_t kind; /* Steam, widow beamout, nuke blast in the same Source pool. */
    bool expired; /* Negative wire duration, pending the next real scene sample. */
} frontend_steam;
typedef struct frontend_q1_temporary_light {
    qa_actor_id actor;
    qa_vec3 origin;
    frontend_fx_q1_light_recipe recipe;
    double born, die;
    uint64_t identity;
    bool active;
} frontend_q1_temporary_light;
typedef struct frontend_q1_temporary_beam {
    qa_actor_id actor;
    uint32_t source_entity;
    qa_vec3 start, end;
    double die;
    uint8_t type;
    bool active;
} frontend_q1_temporary_beam;
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
    frontend_fx_particles *q1;
    bool q1_quakeworld, q1_rerelease;
    frontend_q1_temporary_light q1_lights[32];
    frontend_q1_temporary_beam q1_beams[24];
    qa_scene_model *q1_beam_models[4]; /* borrowed from the actual appearance owner */
    frontend_fx_particles *q2_particles;
    frontend_fx_q2_particle *q2;
    frontend_q2_temporary_beam beams[32],player_beams[32];
    qa_scene_model *beam_models[Q2FX_MODEL_COUNT];
    frontend_q2_beam_random beam_random;
    double beam_sample_ms;
    bool beam_sampled,beam_active;
    frontend_q2_entity_cache entity_trails;
    qa_scene_light entity_lights[32];
    size_t entity_light_count;
    uint64_t entity_frame, entity_server_frame;
    double animation_server_ms, animation_client_ms;
    double entity_milliseconds;
    float entity_frame_seconds;
    bool entity_sampled, entity_advance, entity_events_ready, entity_events;
    uint64_t entity_event_sample;
    frontend_q2_impact impacts[FRONTEND_Q2_IMPACT_CAPACITY];
    qa_scene_model *impact_models[9]; /* Borrowed from this frontend's actual appearance owner. */
    frontend_q2_laser lasers[FRONTEND_Q2_LASER_CAPACITY];
    frontend_q2_dlight lights[FRONTEND_Q2_DLIGHT_CAPACITY];
    frontend_steam steam[FRONTEND_STEAM_CAPACITY];
    size_t steam_count;
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
} frontend_particle_cvars;
struct frontend_particle_state {
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
static bool particle_state(qa_frontend *frontend, qa_error *error)
{
    if (frontend->particles) return true;
    frontend->particles = calloc(1, sizeof(*frontend->particles));
    if (!frontend->particles)
        return frontend_fail(error, QA_ERROR_MEMORY, "allocating frontend particle continuation");
    frontend->particles->sample_ns = frontend->particles->previous_sample_ns =
        qa_session_elapsed(qa_application_session(frontend->application));
    return true;
}
static const frontend_particle_cvars *particle_cvars_bind(qa_frontend *frontend,
    uint32_t seat, const qa_cvars *registry, qa_error *error)
{
    if (!particle_state(frontend,error)) return NULL;
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
    }
    return bindings;
}

bool frontend_particle_visual_read(qa_frontend *frontend,qa_actor_id actor,
    qa_application_visual_view *out,bool *found,qa_error *error)
{
    if (!particle_state(frontend,error)) return false;
    frontend_particle_state *state=frontend->particles;
    if ((size_t)actor.slot>=state->visual_sample_capacity) {
        size_t capacity=state->visual_sample_capacity?state->visual_sample_capacity:128;
        while (capacity<=(size_t)actor.slot) {
            if (capacity>SIZE_MAX/2) return frontend_fail(error,QA_ERROR_MEMORY,"Visual actor table overflow");
            capacity*=2;
        }
        if (capacity>SIZE_MAX/sizeof(*state->visual_samples))
            return frontend_fail(error,QA_ERROR_MEMORY,"Visual actor table overflow");
        frontend_visual_sample *rows=realloc(state->visual_samples,capacity*sizeof(*rows));
        if (!rows) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining frame visual samples");
        memset(rows+state->visual_sample_capacity,0,
            (capacity-state->visual_sample_capacity)*sizeof(*rows));
        state->visual_samples=rows;state->visual_sample_capacity=capacity;
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
    if (!particle_state(frontend, error)) return false;
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
        if ((uint64_t)extent*sizeof(*state->source_entities)>SIZE_MAX)
            return frontend_fail(error,QA_ERROR_MEMORY,"Q2 Source sample exceeds native storage");
        if (extent>state->source_entity_capacity) {
            frontend_q2_entity_sample *entities=realloc(state->source_entities,(size_t)extent*sizeof(*entities));
            if (!entities) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining completed Q2 entity samples");
            memset(entities+state->source_entity_capacity,0,
                ((size_t)extent-state->source_entity_capacity)*sizeof(*entities));
            state->source_entities=entities; state->source_entity_capacity=extent;
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

static bool impact_model(qa_frontend *, frontend_particle_owner *, uint8_t,
    bool acquire, qa_scene_model **, qa_error *);
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
    if (!owner->q2 || !delivery_world_current(frontend, owner->world_source, owner->map_identity))
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
    qa_scene_image_release(owner->particle_image);
    free(owner->entity_trails.rows); free(owner->q1); free(owner->q2_particles); free(owner);
}
static void particle_clients_retire(qa_frontend *frontend)
{
    frontend_particle_owner **link = frontend->particles ? &frontend->particles->owners : NULL;
    while (link && *link) {
        frontend_particle_owner *owner = *link;
        if (owner->q2 && !particle_client_current(frontend, owner)) {
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
static bool particle_owner(qa_frontend *frontend, qa_actor_owner provider, qa_game_family family,
    qa_actor_owner world_source, uint64_t map_identity, qa_actor_id recipient,
    frontend_particle_owner **out, qa_error *error)
{
    *out = NULL;
    if (family == QA_GAME_Q2 && (!recipient.registry ||
            !delivery_world_current(frontend, world_source, map_identity)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 particle owner lost its actual delivered world");
    if (!particle_state(frontend, error)) return false;
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
    owner->provider = provider; owner->family = family;
    owner->world_source = world_source; owner->map_identity = map_identity; owner->recipient = recipient;
    owner->q2_edition = edition; owner->q2_interval_ns = interval;
    qa_builtin_random_seed(&owner->random, 1);
    if (family == QA_GAME_Q1) {
        owner->q1 = calloc(1, sizeof(*owner->q1));
        if (owner->q1) owner->q1->family = family;
        const char *instance = qa_application_provider_instance(frontend->application, provider);
        const qa_launch_instance *source = instance ?
            qa_launch_snapshot_find(qa_application_launch(frontend->application), instance) : NULL;
        owner->q1_quakeworld = source && source->selection.clock.kind == QA_RULESET_QUAKEWORLD;
        const qa_product *product = source ? qa_catalog_product(qa_application_catalog(frontend->application),
            source->selection.product) : NULL;
        owner->q1_rerelease = product && product->edition == QA_EDITION_RERELEASE;
    }
    else {
        owner->q2_particles = calloc(1, sizeof(*owner->q2_particles));
        if (owner->q2_particles) {
            frontend_q2_effect_particles_initialize(owner->q2_particles,&owner->random,
                owner->q2_edition==QA_Q2_RERELEASE);
            owner->q2 = owner->q2_particles->values.q2;
        }
    }
    bool ok = (owner->q1 || owner->q2) && particle_images(frontend, provider, family, &owner->images, error) &&
        qa_scene_particle_image(owner->images, family == QA_GAME_Q1 ? QA_GAME_Q1 : QA_GAME_Q2, &owner->particle_image, error);
    if (!ok) {
        if (!owner->q1 && !owner->q2) frontend_fail(error, QA_ERROR_MEMORY, "allocating bounded source particle pool");
        qa_scene_image_release(owner->particle_image); free(owner->entity_trails.rows); free(owner->q1); free(owner->q2_particles); free(owner); return false;
    }
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
    free(frontend->particles->source_entities);
    free(frontend->particles->q1_trails);
    free(frontend->particles->visual_samples);
    free(frontend->particles); frontend->particles = NULL;
}
void frontend_particle_reset_round(qa_frontend *frontend)
{
    frontend_particle_state *state = frontend->particles;
    if (!state) return;
    for (frontend_particle_owner *owner = state->owners; owner; owner = owner->next) {
        if (owner->q2_particles) {
            owner->q2_particles->count = 0;
            if (owner->entity_trails.rows) memset(owner->entity_trails.rows,0,
                owner->entity_trails.capacity*sizeof(*owner->entity_trails.rows));
            owner->entity_light_count=0;owner->entity_sampled=owner->entity_events_ready=false;
        }
        if (owner->q1) owner->q1->count = 0;
        memset(owner->q1_lights, 0, sizeof(owner->q1_lights));
        memset(owner->beams,0,sizeof(owner->beams));memset(owner->player_beams,0,sizeof(owner->player_beams));
        memset(owner->beam_models,0,sizeof(owner->beam_models));owner->beam_random=(frontend_q2_beam_random){0};
        owner->beam_sampled=owner->beam_active=false;
        memset(owner->q1_beams, 0, sizeof(owner->q1_beams));
        memset(owner->q1_beam_models, 0, sizeof(owner->q1_beam_models));
        memset(owner->impacts, 0, sizeof(owner->impacts));
        memset(owner->lasers, 0, sizeof(owner->lasers));
        memset(owner->lights, 0, sizeof(owner->lights));
        owner->steam_count = 0;
        memset(owner->steam, 0, sizeof(owner->steam));
    }
    state->sample_ns = state->previous_sample_ns = qa_session_elapsed(qa_application_session(frontend->application));
    if (state->q1_trails) memset(state->q1_trails, 0, state->q1_trail_capacity * sizeof(*state->q1_trails));
    if (state->visual_samples) memset(state->visual_samples,0,
        state->visual_sample_capacity*sizeof(*state->visual_samples));
}
static uint32_t particle_random(frontend_particle_owner *owner)
{ return qa_builtin_random_integer(&owner->random); }
static float particle_unit(frontend_particle_owner *owner)
{ return qa_builtin_random_unit(&owner->random); }
static float particle_signed(frontend_particle_owner *owner)
{ return particle_unit(owner) * 2 - 1; }
static const char *impact_path(const frontend_particle_owner *owner,uint8_t kind)
{
    if (kind==11) return q2fx_model_paths[Q2FX_EXPLODE];
    if (kind==5 && owner->q2_edition==QA_Q2_RERELEASE) return q2fx_model_paths[Q2FX_ROCKET];
    static const q2fx_model models[]={Q2FX_SMOKE,Q2FX_FLASH,Q2FX_ROCKET,Q2FX_BFG,Q2FX_BIG,
        Q2FX_EXPLODE,Q2FX_EXPLODE,Q2FX_EXPLODE,Q2FX_EXPLODE};
    return kind>=1 && kind<=9?q2fx_model_paths[models[kind-1]]:NULL;
}
static uint32_t impact_frames(const frontend_q2_impact *impact)
{ return impact->kind==1 ? 4u : impact->kind==2 ? 2u : impact->frames; }
static double impact_fraction(const frontend_q2_impact *impact, const frontend_q2_sample *sample)
{
    double milliseconds=sample->physical ? sample->milliseconds : floor(sample->milliseconds+.5);
    return (milliseconds-(double)impact->start_milliseconds)/100;
}
typedef struct frontend_q2_controls {
    bool smooth, modern;
    uint32_t disable_particles,disable_explosions,dlight_hacks;
} frontend_q2_controls;
static bool q2_controls(qa_frontend *frontend,uint32_t seat,frontend_q2_controls *out,qa_error *error)
{
    frontend_source_client_registry client;
    bool found;
    *out=(frontend_q2_controls){0};
    if (!frontend_source_client_registry_read(frontend,seat,&client,&found,error)) return false;
    if (!found) return true;
    qa_ruleset_id dialect=qa_cvars_dialect(client.cvars);
    if (dialect!=QA_RULESET_Q2_CLASSIC && dialect!=QA_RULESET_Q2_RERELEASE) return true;
    const frontend_particle_cvars *bindings=particle_cvars_bind(frontend,seat,client.cvars,error);
    if (!bindings) return false;
    const qa_cvar_view *row=qa_cvars_read(client.cvars,bindings->smooth_explosions);
    out->smooth=row && row->integer!=0;
    out->modern=row && !(row->flags&QA_CVAR_USER_CREATED);
    row=qa_cvars_read(client.cvars,bindings->disable_particles);
    if (row) out->disable_particles=(uint32_t)row->integer;
    row=qa_cvars_read(client.cvars,bindings->disable_explosions);
    if (row) out->disable_explosions=(uint32_t)row->integer;
    row=qa_cvars_read(client.cvars,bindings->dlight_hacks);
    if (row) out->dlight_hacks=(uint32_t)row->integer;
    return frontend_source_client_registry_current(frontend,&client) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 explosion control lost its physical CLIENT registry");
}
typedef struct q2_entity_context {
    qa_frontend *frontend;
    frontend_particle_owner *owner;
} q2_entity_context;
static void q2_entity_light(void *context,const qa_scene_light *light)
{
    frontend_particle_owner *owner=((q2_entity_context *)context)->owner;
    if (owner->entity_light_count<32) owner->entity_lights[owner->entity_light_count++]=*light;
}
static bool q2_entity_trace(void *context,const qa_trace_query *query,
    qa_trace_result *out,qa_error *error)
{
    qa_frontend *frontend=((q2_entity_context *)context)->frontend;
    return qa_world_trace(qa_application_world(frontend->application),query,out,error);
}
static void q2_entity_clock(qa_frontend *frontend,frontend_particle_owner *owner,double milliseconds)
{
    if (owner->entity_sampled && owner->entity_frame==frontend->frame_number) return;
    owner->entity_frame_seconds=owner->entity_sampled && milliseconds>owner->entity_milliseconds ?
        (float)((milliseconds-owner->entity_milliseconds)*.001):0;
    owner->entity_advance=!owner->entity_sampled || milliseconds>owner->entity_milliseconds;
    owner->entity_milliseconds=milliseconds;owner->entity_frame=frontend->frame_number;
    owner->entity_light_count=0;owner->entity_sampled=true;
}
static bool q2_entity_admit(qa_frontend *frontend,frontend_particle_owner *owner,
    const frontend_q2_entity_pose *pose,const qa_scene_world_input *world,
    const frontend_q2_controls *controls,bool frame_particles,qa_error *error)
{
    if (!frontend_q2_entity_cache_reserve(&owner->entity_trails,pose->actor.slot,error)) return false;
    frontend_q2_entity_trail *trail=&owner->entity_trails.rows[pose->actor.slot];
    if (qa_actor_id_equal(trail->actor,pose->actor) && trail->sample_frame==frontend->frame_number) return true;
    q2_entity_context context={frontend,owner};
    frontend_q2_entity_effects effects={.particles=owner->q2_particles,.random=&owner->random,
        .rerelease=owner->q2_edition==QA_Q2_RERELEASE,.disable_particles=controls->disable_particles,
        .dlight_hacks=controls->dlight_hacks,.context=&context,.light=q2_entity_light,.trace=q2_entity_trace};
    frontend_q2_entity_effect_view sample={.milliseconds=owner->entity_milliseconds,
        .view=world->view,.viewer=owner->recipient,.frame_seconds=owner->entity_frame_seconds};
    if (frame_particles) {
        frontend_q2_entity_frame_particles(&effects,pose,sample.milliseconds);
        if (pose->event==1) frontend_fx_q2_respawn_particles(owner->q2_particles,&owner->random,
            pose->origin,sample.milliseconds*.001,FRONTEND_FX_Q2_ITEM);
        else if (pose->event==6) frontend_fx_q2_teleport(owner->q2_particles,&owner->random,
            pose->origin,sample.milliseconds*.001);
    }
    if (!frontend_q2_entity_effect(&effects,&sample,pose,trail,owner->entity_advance,error)) return false;
    trail->sample_frame=frontend->frame_number;return true;
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
    const frontend_q2_controls *controls,frontend_particle_owner **out,qa_error *error)
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
    if (!q2_entity_admit(frontend,owner,&pose,world,controls,owner->entity_events,error)) return false;
    *out=owner;return true;
}
static bool entity_effects_prepare(qa_frontend *frontend,uint32_t seat,qa_actor_id recipient,
    const qa_scene_world_input *world,const frontend_q2_controls *controls,qa_error *error)
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
                if (!q2_entity_admit(frontend,owner,&pose,world,controls,events,error)) return false;
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
                if (!frontend_visual_model_acquire(frontend,view.provider,view.family,path,
                        view.model_resources[0],&model,error)) return false;
                source = model.model;
            }
            if (!frontend_particle_q1_entity(frontend,&view,source,error)) return false;
        }
        if (view.family==QA_GAME_Q2) {
            float back_lerp;
            frontend_particle_owner *owner;
            if (!frontend_particle_q2_entity_sample(frontend,&view,&back_lerp,error) ||
                !q2_visual_entity_admit(frontend,seat,&view,world,controls,&owner,error)) return false;
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
        qa_bytes palette;
        if (!qa_scene_resources_palette(owner->images,QA_GAME_Q2,&palette,error) ||
            !frontend_q2_entity_beam(&owner->random,palette,qa_scene_white(owner->images),
                &world->view,view->body.origin,view->previous_origin,(uint32_t)view->visual.skin,
                view->visual.frame,frame,error)) return false;
        *beam=true;
    }
    return true;
}

static float impact_alpha(const frontend_particle_owner *owner, const frontend_q2_impact *impact, const frontend_q2_sample *sample,
    double fraction,bool smooth)
{
    if (impact->kind==10) return 1;
    if (impact->light_only) return (float)(1-fraction/(double)(impact_frames(impact)-1));
    if (impact->kind==1 || impact->kind>=6) return (float)(1-fraction/3);
    if (impact->kind==2) return 1;
    if (owner->q2_edition==QA_Q2_RERELEASE || smooth) {
        double fade=fraction/(double)(impact_frames(impact)-1);
        return (float)(1-fade*fade*fade);
    }
    double frame=floor(fraction);
    if (!sample->physical && frame<0) frame=0;
    return (float)((16-frame)/16);
}
static bool impact_model(qa_frontend *frontend, frontend_particle_owner *owner,
    uint8_t kind, bool acquire, qa_scene_model **out, qa_error *error)
{
    if (kind==11) kind=8;
    if (kind < 1 || kind > 9)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Invalid Q2 impact model recipe");
    if (owner->impact_models[kind - 1]) { *out = owner->impact_models[kind - 1]; return true; }
    if (acquire) {
        frontend_visual_model_view model;
        if (!frontend_visual_model_acquire(frontend, owner->provider, QA_GAME_Q2,
                impact_path(owner,kind), NULL, &model, error)) return false;
        owner->impact_models[kind - 1] = model.scene; *out = model.scene; return true;
    }
    size_t count = frontend_visual_owner_count(frontend);
    for (size_t i = 0; i < count; ++i) {
        frontend_visual_owner_view media;
        if (!frontend_visual_owner_read(frontend, i, &media) ||
            media.owner != owner->provider || media.family != QA_GAME_Q2) continue;
        size_t models = frontend_visual_model_count(frontend, i);
        for (size_t j = 0; j < models; ++j) {
            frontend_visual_model_view model;
            if (frontend_visual_model_read(frontend, i, j, &model) &&
                !strcmp(model.path, impact_path(owner,kind))) {
                owner->impact_models[kind - 1] = model.scene; *out = model.scene; return true;
            }
        }
    }
    return frontend_fail(error, QA_ERROR_NOT_FOUND, "Q2 impact lost its actual retained model owner");
}
static frontend_q2_impact *impact_allocate(frontend_particle_owner *owner, int64_t now)
{
    size_t oldest = 0; int64_t start = now;
    for (size_t i = 0; i < FRONTEND_Q2_IMPACT_CAPACITY; ++i)
        if (!owner->impacts[i].kind) return &owner->impacts[i];
    for (size_t i = 0; i < FRONTEND_Q2_IMPACT_CAPACITY; ++i)
        if (owner->impacts[i].start_milliseconds < start) {
            start = owner->impacts[i].start_milliseconds; oldest = i;
        }
    return &owner->impacts[oldest];
}
typedef struct frontend_q2_particle_recipe {
    uint32_t color;
    int count;
    bool fixed, upward, splash_sparks;
} frontend_q2_particle_recipe;
static bool q2_fixed_sound(qa_frontend *frontend, frontend_particle_owner *owner,
    const qa_builtin_event *event, uint32_t seat, const char *sound,
    float attenuation, qa_error *error)
{
    qa_string_id resource;
    if (!qa_strings_intern_cstr(qa_session_strings(qa_application_session(frontend->application)),
            sound, &resource, error)) return false;
    qa_builtin_event audio = {.kind = QA_BUILTIN_SOUND, .family = QA_GAME_Q2,
        .provider = event->provider, .resource = resource, .origin = event->origin,
        .volume = 1, .attenuation = attenuation, .time_ns = event->time_ns};
    return frontend_particle_sound(frontend, &audio, seat, owner->recipient, error);
}
static bool q2_damage_particles(qa_frontend *frontend, frontend_particle_owner *owner,
    const qa_builtin_event *event, uint32_t seat, uint64_t source_time_ns,
    const frontend_q2_particle_recipe *recipe, qa_error *error)
{
    frontend_q2_controls controls;
    if (!q2_controls(frontend,seat,&controls,error)) return false;
    if (event->code==QA_Q2_DAMAGE_BLOOD && (controls.disable_particles&16u)) return true;
    uint64_t sample, server;
    float back_lerp;
    bool physical;
    if (!client_sample(frontend, &sample, &server, &back_lerp, &physical, error)) return false;
    uint64_t birth = physical && owner->provider == frontend->particles->clock_source ?
        sample : source_time_ns;
    uint64_t impact_time = source_time_ns;
    uint32_t color; int count; bool fixed = false;
    const char *sound = NULL;
    float attenuation = 1;
    if (recipe) {
        color = recipe->color; count = recipe->count; fixed = recipe->fixed;
    } else switch (event->code) {
    case QA_Q2_TE_GUNSHOT: color = 0; count = 40; break;
    case QA_Q2_TE_SHOTGUN: color = 0; count = 20; break;
    case QA_Q2_DAMAGE_BLOOD: color = 0xe8; count = 60; break;
    case QA_Q2_DAMAGE_SPARKS: case QA_Q2_DAMAGE_BULLET_SPARKS: color = 0xe0; count = 6; break;
    case QA_Q2_DAMAGE_SCREEN_SPARKS: color = 0xd0; count = 40; sound = "weapons/lashit.wav"; break;
    case QA_Q2_DAMAGE_SHIELD_SPARKS: color = 0xb0; count = 40; sound = "weapons/lashit.wav"; break;
    case QA_Q2_DAMAGE_GREEN_BLOOD: color = 0xdf; count = 30; fixed = true; break;
    case QA_Q2_DAMAGE_MORE_BLOOD: color = 0xe8; count = 250; break;
    case QA_Q2_DAMAGE_ELECTRIC_SPARKS: color = 0x75; count = 40; sound = "weapons/lashit.wav"; break;
    default: return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Unknown Q2 damage particle recipe");
    }
    for (int i = 0; i < count && owner->q2_particles->count < FRONTEND_PARTICLE_CAPACITY; ++i) {
        uint32_t selected = color + (fixed ? 0 : particle_random(owner) & 7);
        float distance = (float)(particle_random(owner) & (fixed ? 7 : 31));
        qa_vec3 origin, velocity;
        origin.x = event->origin.x + (float)(particle_random(owner) & 7) - 4 + distance * event->direction.x;
        velocity.x = particle_signed(owner) * 20;
        origin.y = event->origin.y + (float)(particle_random(owner) & 7) - 4 + distance * event->direction.y;
        velocity.y = particle_signed(owner) * 20;
        origin.z = event->origin.z + (float)(particle_random(owner) & 7) - 4 + distance * event->direction.z;
        velocity.z = particle_signed(owner) * 20;
        owner->q2[owner->q2_particles->count++] = (frontend_fx_q2_particle){
            .spawn_milliseconds = (double)(birth / UINT64_C(1000000)),
            .origin = origin, .velocity = velocity,
            .acceleration = {0, 0, recipe && recipe->upward ? 40 : -40},
            .color = selected, .alpha = 1, .alpha_velocity = -1 / (.5f + particle_unit(owner) * .3f)};
    }
    if (event->code == QA_Q2_TE_GUNSHOT || event->code == QA_Q2_TE_SHOTGUN ||
        event->code == QA_Q2_DAMAGE_BULLET_SPARKS) {
        int64_t now = (int64_t)(impact_time / UINT64_C(1000000));
        for (uint8_t kind = 1; kind <= 2; ++kind) {
            qa_scene_model *model;
            if (!impact_model(frontend, owner, kind, true, &model, error)) return false;
            *impact_allocate(owner, now) = (frontend_q2_impact){.kind = kind,
                .origin = event->origin, .start_milliseconds = now - (int64_t)(owner->q2_interval_ns/UINT64_C(1000000))};
        }
        if (event->code != QA_Q2_TE_SHOTGUN) {
            uint32_t choice = particle_random(owner) & 15;
            if (choice >= 1 && choice <= 3) {
                static const char *const ricochets[] = {"world/ric1.wav", "world/ric2.wav", "world/ric3.wav"};
                sound = ricochets[choice - 1];
            }
        }
    }
    if (recipe && recipe->splash_sparks) {
        static const char *const sparks[] = {"world/spark5.wav", "world/spark6.wav", "world/spark7.wav"};
        uint32_t choice = particle_random(owner) & 3;
        sound = sparks[choice < 2 ? choice : 2]; attenuation = 3;
    }
    if (sound && !q2_fixed_sound(frontend, owner, event, seat, sound, attenuation, error)) return false;
    return true;
}
static bool q1_temporary(const qa_builtin_event *event, const char *resource, qa_q1_temp *out)
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
        if (!resource || strcmp(resource, "colored-explosion") || event->count <= 0) return false;
        out->kind = QA_Q1_TEMP_COLORS;
        out->color_start = (uint8_t)event->code; out->color_length = (uint8_t)event->count;
        break;
    default: return false;
    }
    out->origin[0] = origin.x; out->origin[1] = origin.y; out->origin[2] = origin.z;
    return true;
}

static frontend_q1_temporary_light *q1_light(frontend_particle_owner *owner,
    qa_actor_id actor, double seconds)
{
    size_t at = 32;
    if (actor.registry)
        for (size_t i = 0; i < 32; ++i)
            if (qa_actor_id_equal(owner->q1_lights[i].actor,actor)) { at = i; break; }
    if (at == 32)
        for (size_t i = 0; i < 32; ++i)
            if (!owner->q1_lights[i].active || owner->q1_lights[i].die < seconds) { at = i; break; }
    if (at == 32) at = 0;
    frontend_q1_temporary_light *light = owner->q1_lights + at;
    uint64_t identity = light->identity ? light->identity : qa_scene_identity();
    *light = (frontend_q1_temporary_light){.actor=actor,.identity=identity,.born=seconds,.active=true};
    return light;
}

static bool q1_temporary_apply(qa_frontend *frontend, frontend_particle_owner *owner,
    const qa_q1_temp *event, qa_actor_id actor, bool quakeworld, double seconds,
    bool received, qa_error *error)
{
    if (event->kind == QA_Q1_TEMP_BEAM) {
        const char *path = frontend_fx_q1_beam_model(event->type);
        if (!path) return frontend_fail(error, QA_ERROR_FORMAT, "Q1 beam has no Source model");
        uint8_t model_index = event->type == 5 ? 0 : event->type == 6 ? 1 : event->type == 9 ? 2 : 3;
        if (!owner->q1_beam_models[model_index]) {
            frontend_visual_model_view model;
            if (!frontend_visual_model_acquire(frontend, owner->provider, QA_GAME_Q1,
                    path, NULL, &model, error)) return false;
            owner->q1_beam_models[model_index] = model.scene;
        }
        size_t slot = 24;
        for (size_t i = 0; i < 24; ++i)
            if (owner->q1_beams[i].active && (received ?
                owner->q1_beams[i].source_entity == event->entity :
                qa_actor_id_equal(owner->q1_beams[i].actor, actor))) { slot = i; break; }
        if (slot == 24) for (size_t i = 0; i < 24; ++i)
            if (!owner->q1_beams[i].active || owner->q1_beams[i].die < seconds) { slot = i; break; }
        if (slot < 24) owner->q1_beams[slot] = (frontend_q1_temporary_beam){.actor = actor, .source_entity = event->entity,
            .start = {event->origin[0], event->origin[1], event->origin[2]},
            .end = {event->end[0], event->end[1], event->end[2]}, .die = seconds + .2,
            .type = event->type, .active = true};
        return true;
    }
    if (!frontend_fx_q1_temporary_particles(owner->q1, &owner->random, event, quakeworld, seconds))
        return frontend_fail(error, QA_ERROR_FORMAT, "Q1 temporary effect has no Source recipe");
    frontend_fx_q1_light_recipe recipe;
    if (frontend_fx_q1_temporary_light(event, &recipe)) {
        frontend_q1_temporary_light *light=q1_light(owner,(qa_actor_id){0},seconds);
        light->origin=qa_v3(event->origin[0],event->origin[1],event->origin[2]);
        light->recipe=recipe; light->die=seconds+recipe.duration;
    }
    const char *path = frontend_fx_q1_temporary_sound(event, &owner->random);
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
    const char *resource = qa_strings_cstr(qa_session_strings(qa_application_session(frontend->application)), event->resource);
    bool raw = event->kind == QA_BUILTIN_PARTICLES;
    bool blood = event->kind == QA_BUILTIN_IMPACT && event->code == 1;
    if (!raw && !blood && !q1_temporary(event, resource, &temporary)) return true;
    frontend_particle_owner *owner;
    if (!particle_owner(frontend, event->provider, QA_GAME_Q1, 0, 0,
            (qa_actor_id){0}, &owner, error)) return false;
    double seconds = (double)frontend->particles->sample_ns / 1e9;
    if (raw) frontend_fx_q1_particle_event(owner->q1, &owner->random, event->origin,
        event->direction, event->code, event->count, seconds);
    else if (blood) {
        if (owner->q1_quakeworld) {
            temporary = (qa_q1_temp){.kind = QA_Q1_TEMP_POINT, .type = 12,
                .count = event->flags & QA_Q1_IMPACT_GROUPED ? (uint8_t)(int32_t)event->value : 1,
                .origin = {event->origin.x, event->origin.y, event->origin.z}};
            (void)frontend_fx_q1_temporary_particles(owner->q1, &owner->random, &temporary, true, seconds);
        } else frontend_fx_q1_particle_event(owner->q1, &owner->random, event->origin,
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
    if (!particle_state(frontend, error)) return false;
    frontend_particle_state *state = frontend->particles;
    size_t slot = view->actor.slot;
    if (slot >= state->q1_trail_capacity) {
        size_t extent = (size_t)qa_actors_capacity(qa_world_actors(qa_application_world(frontend->application)));
        size_t capacity = state->q1_trail_capacity ? state->q1_trail_capacity : 64;
        while (capacity <= slot && capacity < extent) capacity = capacity > extent / 2 ? extent : capacity * 2;
        frontend_q1_trail *trails = realloc(state->q1_trails, capacity * sizeof(*trails));
        if (!trails) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining Source entity trail origins");
        memset(trails + state->q1_trail_capacity, 0, (capacity - state->q1_trail_capacity) * sizeof(*trails));
        state->q1_trails = trails; state->q1_trail_capacity = capacity;
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
    bool light_present=frontend_fx_q1_entity_effects(owner->q1,&owner->random,start,origin,
        view->body.angles,effects,flags,owner->q1_quakeworld,owner->q1_rerelease,
        seconds,&light_origin,&recipe);
    if (light_present) {
        frontend_q1_temporary_light *light=q1_light(owner,view->actor,seconds);
        light->origin=light_origin; light->recipe=recipe; light->die=seconds+recipe.duration;
    }
    return true;
}
static bool steam_particles(frontend_particle_owner *owner, const qa_q2_map_event *event,
    uint64_t now, bool smoke)
{
    qa_vec3 direction = event->direction, initial = qa_v3(direction.z, -direction.x, direction.y);
    qa_vec3 right = qa_vec_normalize(qa_vec_sub(initial, qa_vec_scale(direction, qa_vec_dot(initial, direction))));
    qa_vec3 up = qa_vec_cross(right, direction);
    for (int i = 0; i < event->count; ++i) {
        if (owner->q2_particles->count == FRONTEND_PARTICLE_CAPACITY) return false;
        uint32_t color = (uint32_t)event->style + (particle_random(owner) & 7);
        qa_vec3 jitter;
        jitter.x = event->value * .1f * particle_signed(owner);
        jitter.y = event->value * .1f * particle_signed(owner);
        jitter.z = event->value * .1f * particle_signed(owner);
        qa_vec3 velocity = qa_vec_add(qa_vec_scale(direction, event->value), qa_vec_scale(right, particle_signed(owner) * event->value / 3));
        velocity = qa_vec_add(velocity, qa_vec_scale(up, particle_signed(owner) * event->value / 3));
        owner->q2[owner->q2_particles->count++] = (frontend_fx_q2_particle){.spawn_milliseconds = (double)(now / UINT64_C(1000000)),
            .origin = qa_vec_add(event->origin, jitter), .velocity = velocity,
            .acceleration = {0, 0, smoke ? 0 : -20}, .color = color, .alpha = 1,
            .alpha_velocity = -1 / (.5f + particle_unit(owner) * .3f)};
    }
    return true;
}
static bool force_wall(frontend_particle_owner *owner, const qa_q2_map_event *event, uint64_t now, qa_error *error)
{
    qa_vec3 delta = qa_vec_sub(event->direction, event->origin);
    float length = qa_vec_length(delta);
    if (!isfinite(length)) return frontend_fail(error, QA_ERROR_ARGUMENT, "force wall exceeds native coordinate range");
    qa_vec3 direction = qa_vec_normalize(delta);
    for (double distance = 0; distance < length && owner->q2_particles->count < FRONTEND_PARTICLE_CAPACITY; distance += 4) {
        if (particle_unit(owner) <= .3f) continue;
        float alpha_velocity = -1 / (3 + particle_unit(owner) * .5f);
        qa_vec3 jitter;
        jitter.x = particle_signed(owner) * 3; jitter.y = particle_signed(owner) * 3; jitter.z = particle_signed(owner) * 3;
        qa_vec3 origin = qa_vec_add(qa_vec_add(event->origin, qa_vec_scale(direction, (float)distance)), jitter);
        owner->q2[owner->q2_particles->count++] = (frontend_fx_q2_particle){.spawn_milliseconds = (double)(now / UINT64_C(1000000)),
            .origin = origin, .velocity = {0, 0, -40 - particle_signed(owner) * 10},
            .color = (uint32_t)event->style, .alpha = 1, .alpha_velocity = alpha_velocity};
    }
    return true;
}
static bool q2_trail_particles(frontend_particle_owner *owner, const qa_builtin_event *event,
    uint64_t now, uint8_t type, qa_error *error)
{
    qa_vec3 delta = qa_vec_sub(event->direction, event->origin);
    float length = qa_vec_length(delta);
    if (!isfinite(length))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 trail exceeds native coordinate range");
    bool debug = type == QA_Q2_TE_DEBUGTRAIL;
    bool dense = type == QA_Q2_TE_BUBBLETRAIL2;
    float spacing = debug ? 3 : dense ? 8 : 32;
    qa_vec3 move = event->origin;
    qa_vec3 direction = qa_vec_normalize(delta);
    if (type == QA_Q2_TE_RAILTRAIL) {
        frontend_fx_q2_rail(owner->q2_particles,&owner->random,event->origin,event->direction,(double)now*1e-9);
        return true;
    }
    if (type == QA_Q2_TE_BUBBLETRAIL) {
        frontend_fx_q2_bubbles(owner->q2_particles,&owner->random,event->origin,event->direction,(double)now*1e-9);
        return true;
    }
    qa_vec3 step = qa_vec_scale(direction, spacing);
    for (double distance = 0; distance < length && owner->q2_particles->count < FRONTEND_PARTICLE_CAPACITY;
         distance += spacing, move = qa_vec_add(move, step)) {
        frontend_fx_q2_particle particle = {
            .spawn_milliseconds = (double)(now / UINT64_C(1000000)),
            .origin = move, .alpha = 1};
        if (debug) {
            particle.alpha_velocity = -.1f;
            particle.color = 0x74 + (particle_random(owner) & 7);
        } else {
            particle.alpha_velocity = -1 / (1 + particle_unit(owner) * (dense ? .1f : .2f));
            particle.color = 4 + (particle_random(owner) & 7);
            particle.origin.x += particle_signed(owner) * 2;
            particle.velocity.x = particle_signed(owner) * (dense ? 10 : 5);
            particle.origin.y += particle_signed(owner) * 2;
            particle.velocity.y = particle_signed(owner) * (dense ? 10 : 5);
            particle.origin.z += particle_signed(owner) * 2 - (dense ? 4 : 0);
            particle.velocity.z = particle_signed(owner) * (dense ? 10 : 5) + (dense ? 20 : 6);
        }
        owner->q2[owner->q2_particles->count++] = particle;
    }
    return true;
}
static const qa_q2_temp_field *temporary_field(const qa_q2_temp_entity *temporary,
    qa_q2_temp_field_name name, qa_q2_temp_field_kind kind)
{
    for (size_t i=0;i<temporary->field_count;++i)
        if (temporary->fields[i].name==name && temporary->fields[i].kind==kind)
            return &temporary->fields[i];
    return NULL;
}

static qa_vec3 q2_random_direction(frontend_particle_owner *owner)
{
    if (owner->q2_edition==QA_Q2_RERELEASE) {
        float x,y,squared;
        do {
            x=particle_signed(owner); y=particle_signed(owner);
            squared=x*x+y*y;
        } while (squared>1);
        float radial=2*sqrtf(1-squared);
        return qa_v3(x*radial,y*radial,-1+2*squared);
    }
    qa_vec3 direction;
    direction.x=particle_signed(owner);
    direction.y=particle_signed(owner);
    direction.z=particle_signed(owner);
    return qa_vec_normalize(direction);
}

static void q2_sustain_particles(frontend_particle_owner *owner,
    const frontend_steam *sustain, uint64_t now)
{
    static const uint32_t widow_colors[]={16,104,168,144};
    static const uint32_t nuke_colors[]={110,112,114,116};
    const uint32_t *colors=sustain->kind==1?widow_colors:nuke_colors;
    double duration=sustain->kind==1?2100000000.0:1000000000.0;
    float ratio=(float)(1-(double)(sustain->end_ns-now)/duration);
    float radius=(sustain->kind==1?45:200)*ratio;
    unsigned count=sustain->kind==1?300u:700u;
    for (unsigned i=0;i<count && owner->q2_particles->count<FRONTEND_PARTICLE_CAPACITY;++i) {
        frontend_fx_q2_particle particle={.spawn_milliseconds=(double)(now/UINT64_C(1000000)),
            .alpha=1,.alpha_velocity=-10000};
        particle.color=colors[particle_random(owner)&3];
        particle.origin=qa_vec_add(sustain->event.origin,
            qa_vec_scale(q2_random_direction(owner),radius));
        owner->q2[owner->q2_particles->count++]=particle;
    }
}

static void q2_power_splash(frontend_particle_owner *owner,
    const qa_body_state *entity,uint64_t now)
{
    qa_vec3 origin=qa_vec_add(entity->origin,
        qa_vec_scale(qa_vec_add(entity->bounds.mins,entity->bounds.maxs),.5f));
    float radius=qa_vec_length(qa_vec_sub(entity->bounds.maxs,entity->bounds.mins))*.5f;
    for (unsigned i=0;i<256 && owner->q2_particles->count<FRONTEND_PARTICLE_CAPACITY;++i) {
        frontend_fx_q2_particle particle={.spawn_milliseconds=(double)(now/UINT64_C(1000000)),.alpha=1};
        particle.color=208+(particle_random(owner)&3);
        qa_vec3 direction=q2_random_direction(owner);
        particle.origin=qa_vec_add(origin,qa_vec_scale(direction,radius));
        particle.velocity=qa_vec_scale(direction,40);
        particle.alpha_velocity=-1/(.5f+particle_unit(owner)*.3f);
        owner->q2[owner->q2_particles->count++]=particle;
    }
}

static void q2_berserk_particles(frontend_particle_owner *owner,
    qa_vec3 origin, qa_vec3 direction, uint64_t now)
{
    qa_vec3 seed=qa_v3(direction.z,-direction.x,direction.y);
    qa_vec3 right=qa_vec_normalize(qa_vec_sub(seed,qa_vec_scale(direction,qa_vec_dot(seed,direction))));
    qa_vec3 up=qa_vec_cross(right,direction);
    for (unsigned i=0;i<700 && owner->q2_particles->count<FRONTEND_PARTICLE_CAPACITY;++i) {
        frontend_fx_q2_particle particle={.spawn_milliseconds=(double)(now/UINT64_C(1000000)),
            .origin=origin,.alpha=1};
        particle.color=110+2*(particle_random(owner)&3);
        particle.velocity=qa_vec_scale(direction,particle_unit(owner)*192);
        particle.velocity=qa_vec_add(particle.velocity,qa_vec_scale(right,particle_signed(owner)*192));
        particle.velocity=qa_vec_add(particle.velocity,qa_vec_scale(up,particle_signed(owner)*192));
        particle.alpha_velocity=-1/(.5f+particle_unit(owner)*.3f);
        owner->q2[owner->q2_particles->count++]=particle;
    }
}

static void q2_burst_particles(frontend_particle_owner *owner, qa_vec3 origin,
    uint64_t now, uint8_t type, bool rerelease)
{
    int64_t birth = (int64_t)(now / UINT64_C(1000000));
    if (type == QA_Q2_TE_WIDOWSPLASH) {
        static const uint32_t colors[] = {16, 104, 168, 144};
        for (unsigned i = 0; i < 256 && owner->q2_particles->count < FRONTEND_PARTICLE_CAPACITY; ++i) {
            frontend_fx_q2_particle particle = {.spawn_milliseconds = (double)birth, .alpha = 1};
            particle.color = colors[particle_random(owner) & 3];
            qa_vec3 direction;
            if (rerelease) {
                float x, y, squared;
                do {
                    x = particle_signed(owner); y = particle_signed(owner);
                    squared = x * x + y * y;
                } while (squared > 1);
                float radial = 2 * sqrtf(1 - squared);
                direction = qa_v3(x * radial, y * radial, -1 + 2 * squared);
            } else {
                direction.x = particle_signed(owner);
                direction.y = particle_signed(owner);
                direction.z = particle_signed(owner);
                direction = qa_vec_normalize(direction);
            }
            particle.origin = qa_vec_add(origin, qa_vec_scale(direction, 45));
            particle.velocity = qa_vec_scale(direction, 40);
            particle.alpha_velocity = -.8f / (.5f + particle_unit(owner) * .3f);
            owner->q2[owner->q2_particles->count++] = particle;
        }
        return;
    }
    if (type == QA_Q2_TE_TELEPORT_EFFECT || type == QA_Q2_TE_DBALL_GOAL) {
        for (int i = -16; i <= 16; i += 4)
            for (int j = -16; j <= 16; j += 4)
                for (int k = -16; k <= 32; k += 4) {
                    if (owner->q2_particles->count == FRONTEND_PARTICLE_CAPACITY) return;
                    frontend_fx_q2_particle particle = {.spawn_milliseconds = (double)birth,
                        .alpha = 1, .acceleration = {0, 0, -40}};
                    particle.color = 7 + (particle_random(owner) & 7);
                    particle.alpha_velocity = -1 / (.3f + (float)(particle_random(owner) & 7) * .02f);
                    particle.origin.x = origin.x + (float)i + (float)(particle_random(owner) & 3);
                    particle.origin.y = origin.y + (float)j + (float)(particle_random(owner) & 3);
                    particle.origin.z = origin.z + (float)k + (float)(particle_random(owner) & 3);
                    qa_vec3 direction = qa_vec_normalize(qa_v3((float)(j * 8), (float)(i * 8), (float)(k * 8)));
                    particle.velocity = qa_vec_scale(direction, 50 + (float)(particle_random(owner) & 63));
                    owner->q2[owner->q2_particles->count++] = particle;
                }
        return;
    }
    bool big = type == QA_Q2_TE_BOSSTPORT;
    for (int i = 0; i < (big ? 4096 : 256) && owner->q2_particles->count < FRONTEND_PARTICLE_CAPACITY; ++i) {
        frontend_fx_q2_particle particle = {.spawn_milliseconds = (double)birth, .alpha = 1};
        if (big) {
            static const uint32_t colors[] = {16, 104, 168, 144};
            particle.color = colors[particle_random(owner) & 3];
            float angle = (float)(3.14159265358979323846 * 2 *
                (particle_random(owner) & 1023) / 1023.0);
            float distance = (float)(particle_random(owner) & 31);
            double x = cos(angle), y = sin(angle);
            particle.origin.x = (float)(origin.x + x * distance);
            particle.velocity.x = (float)(x * (70 + (particle_random(owner) & 63)));
            particle.acceleration.x = (float)(-x * 100);
            particle.origin.y = (float)(origin.y + y * distance);
            particle.velocity.y = (float)(y * (70 + (particle_random(owner) & 63)));
            particle.acceleration.y = (float)(-y * 100);
            particle.origin.z = origin.z + 8 + (float)(particle_random(owner) % 90);
            particle.velocity.z = -100 + (float)(particle_random(owner) & 31);
            particle.acceleration.z = 160;
            particle.alpha_velocity = -.3f / (.5f + particle_unit(owner) * .3f);
        } else {
            particle.color = (type==QA_Q2_TE_BFG_BIGEXPLOSION ? 0xd0u : 0xe0u) + (particle_random(owner) & 7);
            particle.origin.x = origin.x + (float)((int32_t)(particle_random(owner) % 32) - 16);
            particle.velocity.x = (float)(particle_random(owner) % 384) - 192;
            particle.origin.y = origin.y + (float)((int32_t)(particle_random(owner) % 32) - 16);
            particle.velocity.y = (float)(particle_random(owner) % 384) - 192;
            particle.origin.z = origin.z + (float)((int32_t)(particle_random(owner) % 32) - 16);
            particle.velocity.z = (float)(particle_random(owner) % 384) - 192;
            particle.acceleration.z = -40;
            particle.alpha_velocity = -.8f / (.5f + particle_unit(owner) * .3f);
        }
        owner->q2[owner->q2_particles->count++] = particle;
    }
}
static void q2_blaster_particles(frontend_particle_owner *owner, const qa_builtin_event *event,
    uint64_t now, uint8_t type)
{
    uint32_t color=type==QA_Q2_TE_BLASTER2 ? 0xd0u : type==QA_Q2_TE_FLECHETTE ? 0x6fu :
        type==QA_Q2_TE_BLUEHYPERBLASTER_2 ? 0xb0u : 0xe0u;
    for (unsigned i=0;i<40 && owner->q2_particles->count<FRONTEND_PARTICLE_CAPACITY;++i) {
        frontend_fx_q2_particle particle={.spawn_milliseconds=(double)(now/UINT64_C(1000000)),
            .alpha=1,.acceleration={0,0,-40}};
        particle.color=color+(particle_random(owner)&7);
        float distance=(float)(particle_random(owner)&15);
        particle.origin.x=event->origin.x+(float)(particle_random(owner)&7)-4+distance*event->direction.x;
        particle.velocity.x=event->direction.x*30+particle_signed(owner)*40;
        particle.origin.y=event->origin.y+(float)(particle_random(owner)&7)-4+distance*event->direction.y;
        particle.velocity.y=event->direction.y*30+particle_signed(owner)*40;
        particle.origin.z=event->origin.z+(float)(particle_random(owner)&7)-4+distance*event->direction.z;
        particle.velocity.z=event->direction.z*30+particle_signed(owner)*40;
        particle.alpha_velocity=-1/(.5f+particle_unit(owner)*.3f);
        owner->q2[owner->q2_particles->count++]=particle;
    }
}
static void q2_dlight_set(frontend_particle_owner *owner,qa_vec3 origin,uint64_t birth,
    uint8_t kind,uint32_t entity,qa_actor_id actor)
{
    int64_t milliseconds=(int64_t)(birth/UINT64_C(1000000));
    size_t slot=0;
    bool keyed=false;
    if (entity) for (size_t i=0;i<FRONTEND_Q2_DLIGHT_CAPACITY;++i)
        if (owner->lights[i].active && owner->lights[i].source_entity==entity) {
            slot=i; keyed=true; break;
        }
    for (size_t i=0;!keyed && i<FRONTEND_Q2_DLIGHT_CAPACITY;++i)
        if (!owner->lights[i].active || owner->lights[i].end_milliseconds<milliseconds) {
            slot=i; break;
        }
    owner->lights[slot]=(frontend_q2_dlight){.origin=origin,.birth_milliseconds=milliseconds,
        .end_milliseconds=milliseconds+100,.active=true,.kind=kind,.source_entity=entity,.actor=actor};
}
static void q2_tracker_explosion(frontend_particle_owner *owner,qa_vec3 origin,uint64_t birth)
{
    int64_t milliseconds=(int64_t)(birth/UINT64_C(1000000));
    q2_dlight_set(owner,origin,birth,1,0,(qa_actor_id){0});
    for (unsigned i=0;i<128 && owner->q2_particles->count<FRONTEND_PARTICLE_CAPACITY;++i) {
        frontend_fx_q2_particle particle={.spawn_milliseconds=(double)milliseconds,
            .alpha=1,.acceleration={0,0,-40}};
        (void)particle_random(owner);
        particle.color=0;
        particle.origin.x=origin.x+(float)(particle_random(owner)%32)-16;
        particle.velocity.x=(float)(particle_random(owner)%256)-128;
        particle.origin.y=origin.y+(float)(particle_random(owner)%32)-16;
        particle.velocity.y=(float)(particle_random(owner)%256)-128;
        particle.origin.z=origin.z+(float)(particle_random(owner)%32)-16;
        particle.velocity.z=(float)(particle_random(owner)%256)-128;
        particle.alpha_velocity=-.4f/(.6f+particle_unit(owner)*.2f);
        owner->q2[owner->q2_particles->count++]=particle;
    }
}
static bool q2_temporary_beam_admit(qa_frontend *frontend,frontend_particle_owner *owner,
    uint32_t seat,const frontend_q2_beam_recipe *recipe,qa_actor_id actor,qa_actor_id destination,
    qa_vec3 start,qa_vec3 end,double milliseconds,qa_error *error)
{
    if (!owner->beam_models[recipe->model]) {
        frontend_visual_model_view model;
        if (!frontend_visual_model_acquire(frontend,owner->provider,QA_GAME_Q2,
            q2fx_model_paths[recipe->model],NULL,&model,error)) return false;
        owner->beam_models[recipe->model]=model.scene;
    }
    bool rerelease=owner->q2_edition==QA_Q2_RERELEASE;
    frontend_q2_temporary_beam *beam=frontend_q2_beam_retain(recipe->player?owner->player_beams:owner->beams,
        32,rerelease,recipe,actor,destination,start,end,milliseconds);
    if (beam) owner->beam_active=true;
    if (recipe->lightning_sound && frontend_q2_beam_lightning_sound(beam,rerelease,milliseconds)) {
        qa_string_id resource;
        if (!qa_strings_intern_cstr(qa_session_strings(qa_application_session(frontend->application)),
            "weapons/tesla.wav",&resource,error)) return false;
        qa_builtin_event audio={.kind=QA_BUILTIN_SOUND,.family=QA_GAME_Q2,.provider=owner->provider,
            .resource=resource,.actor=actor,.origin=start,.channel=1,.volume=1,.attenuation=1,.time_ns=(uint64_t)(milliseconds*1000000.0)};
        if (!frontend_particle_sound(frontend,&audio,seat,owner->recipient,error)) return false;
    }
    return true;
}
static bool temporary_actor(const qa_application_protocol_event *message,const qa_q2_temp_entity *temporary,
    const qa_q2_temp_field *field,qa_actor_id *out,qa_error *error)
{
    uintptr_t base=(uintptr_t)message->payload.data,raw=(uintptr_t)temporary->raw.data;
    if (!field || field->kind!=QA_Q2_TEMP_INTEGER || field->value.integer<0 ||
        !message->payload.data || !temporary->raw.data || raw<base ||
        raw-base>message->payload.size || temporary->raw.size>message->payload.size-(raw-base) ||
        field->offset>=temporary->raw.size)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q2 entity effect lost its actual decoded packet operand");
    size_t offset=(size_t)(raw-base)+field->offset;
    bool found=false;
    for (size_t i=0;i<message->reference_count;++i) {
        const qa_application_protocol_reference *reference=message->references+i;
        if (reference->offset!=offset || reference->packed_sound) continue;
        if (!reference->actor.registry || (found && !qa_actor_id_equal(*out,reference->actor)))
            return frontend_fail(error,QA_ERROR_FORMAT,"Q2 entity effect has an ambiguous captured actor");
        *out=reference->actor; found=true;
    }
    return found || frontend_fail(error,QA_ERROR_UNSUPPORTED,"Q2 entity effect requires its emission-time full actor receipt");
}

bool frontend_particle_q2_temporary(qa_frontend *frontend,
    const qa_application_protocol_event *message, const qa_application_q2_audience *audience,
    const qa_q2_temp_entity *temporary, qa_error *error)
{
    int code=-1;
    frontend_q2_beam_recipe beam_recipe;
    bool beam_effect=frontend_q2_beam_recipe_read(temporary->type,(qa_vec3){0},&beam_recipe);
    switch (temporary->type) {
    case QA_Q2_TE_GUNSHOT: code=QA_Q2_TE_GUNSHOT; break;
    case QA_Q2_TE_SHOTGUN: code=QA_Q2_TE_SHOTGUN; break;
    case QA_Q2_TE_BLOOD: code=QA_Q2_DAMAGE_BLOOD; break;
    case QA_Q2_TE_SPARKS: code=QA_Q2_DAMAGE_SPARKS; break;
    case QA_Q2_TE_BULLET_SPARKS: code=QA_Q2_DAMAGE_BULLET_SPARKS; break;
    case QA_Q2_TE_SCREEN_SPARKS: code=QA_Q2_DAMAGE_SCREEN_SPARKS; break;
    case QA_Q2_TE_SHIELD_SPARKS: code=QA_Q2_DAMAGE_SHIELD_SPARKS; break;
    case QA_Q2_TE_GREENBLOOD: code=QA_Q2_DAMAGE_GREEN_BLOOD; break;
    case QA_Q2_TE_MOREBLOOD: code=QA_Q2_DAMAGE_MORE_BLOOD; break;
    case QA_Q2_TE_ELECTRIC_SPARKS: code=QA_Q2_DAMAGE_ELECTRIC_SPARKS; break;
    case QA_Q2_TE_SPLASH: case QA_Q2_TE_LASER_SPARKS: case QA_Q2_TE_TUNNEL_SPARKS:
    case QA_Q2_TE_WELDING_SPARKS:
        code=temporary->type; break;
    default: break;
    }
    bool steam=temporary->type==QA_Q2_TE_STEAM;
    bool widow=temporary->type==QA_Q2_TE_WIDOWBEAMOUT;
    bool nuke=temporary->type==QA_Q2_TE_NUKEBLAST;
    bool wall=temporary->type==QA_Q2_TE_FORCEWALL;
    bool smoke=temporary->type==QA_Q2_TE_CHAINFIST_SMOKE;
    bool heat=temporary->type==QA_Q2_TE_HEATBEAM_SPARKS ||
        temporary->type==QA_Q2_TE_HEATBEAM_STEAM;
    bool trail=temporary->type==QA_Q2_TE_BUBBLETRAIL ||
        temporary->type==QA_Q2_TE_BUBBLETRAIL2 || temporary->type==QA_Q2_TE_DEBUGTRAIL ||
        temporary->type==QA_Q2_TE_RAILTRAIL;
    bool laser=temporary->type==QA_Q2_TE_BFG_LASER || temporary->type==QA_Q2_TE_BFG_ZAP;
    bool hyper=temporary->type==QA_Q2_TE_BLUEHYPERBLASTER;
    bool tracker=temporary->type==QA_Q2_TE_TRACKER_EXPLOSION;
    bool flashlight=temporary->type==QA_Q2_TE_FLASHLIGHT;
    bool power=temporary->type==QA_Q2_TE_POWER_SPLASH;
    bool berserk=temporary->type==QA_Q2_TE_BERSERK_SLAM;
    bool blaster=temporary->type==QA_Q2_TE_BLASTER || temporary->type==QA_Q2_TE_BLASTER2 ||
        temporary->type==QA_Q2_TE_FLECHETTE || temporary->type==QA_Q2_TE_BLUEHYPERBLASTER_2;
    bool burst=temporary->type==QA_Q2_TE_BFG_BIGEXPLOSION ||
        temporary->type==QA_Q2_TE_BOSSTPORT || temporary->type==QA_Q2_TE_TELEPORT_EFFECT ||
        temporary->type==QA_Q2_TE_DBALL_GOAL || temporary->type==QA_Q2_TE_WIDOWSPLASH;
    bool poly=temporary->type==QA_Q2_TE_EXPLOSION1 || temporary->type==QA_Q2_TE_EXPLOSION2 ||
        temporary->type==QA_Q2_TE_EXPLOSION1_NL || temporary->type==QA_Q2_TE_EXPLOSION2_NL ||
        temporary->type==QA_Q2_TE_ROCKET_EXPLOSION || temporary->type==QA_Q2_TE_ROCKET_EXPLOSION_WATER ||
        temporary->type==QA_Q2_TE_GRENADE_EXPLOSION || temporary->type==QA_Q2_TE_GRENADE_EXPLOSION_WATER ||
        temporary->type==QA_Q2_TE_PLASMA_EXPLOSION || temporary->type==QA_Q2_TE_PLAIN_EXPLOSION ||
        temporary->type==QA_Q2_TE_EXPLOSION1_BIG || temporary->type==QA_Q2_TE_EXPLOSION1_NP ||
        temporary->type==QA_Q2_TE_BFG_EXPLOSION;
    /* Other decoded TE recipes remain available in the retained raw message;
     * they need their actual beam/explosion owners before they can submit. */
    if (code<0 && !steam && !widow && !nuke && !wall && !smoke && !heat && !trail && !burst && !laser && !poly && !blaster && !hyper && !tracker && !flashlight && !berserk && !power && !beam_effect) return true;
    if (!q2_delivery_admit(frontend, message->provider, audience)) return true;
    const qa_q2_temp_field *position=temporary_field(temporary,QA_Q2_TEMP_POSITION1,QA_Q2_TEMP_VECTOR);
    const qa_q2_temp_field *direction=temporary_field(temporary,
        wall || trail || laser || beam_effect || temporary->type==QA_Q2_TE_BLUEHYPERBLASTER ?
            QA_Q2_TEMP_POSITION2:QA_Q2_TEMP_DIRECTION,QA_Q2_TEMP_VECTOR);
    if (!power && (!position || (!smoke && !burst && !poly && !tracker && !flashlight && !widow && !nuke && !direction)))
        return frontend_fail(error,QA_ERROR_FORMAT,"Original Q2 effect lost its source vector fields");
    qa_builtin_event effect={.kind=QA_BUILTIN_PARTICLES,.family=QA_GAME_Q2,
        .provider=message->provider,.time_ns=audience->source_time_ns,.code=code,
        .origin=power ? (qa_vec3){0} : (qa_vec3){position->value.vector[0],position->value.vector[1],position->value.vector[2]},
        .direction=burst || poly || tracker || flashlight || widow || nuke || power ? (qa_vec3){0} : smoke ? (qa_vec3){0,0,1} :
            (qa_vec3){direction->value.vector[0],direction->value.vector[1],direction->value.vector[2]}};
    frontend_q2_particle_recipe recipe;
    bool electric_splash=false;
    bool explicit_recipe=temporary->type==QA_Q2_TE_SPLASH ||
        temporary->type==QA_Q2_TE_LASER_SPARKS || temporary->type==QA_Q2_TE_TUNNEL_SPARKS ||
        temporary->type==QA_Q2_TE_WELDING_SPARKS;
    if (explicit_recipe) {
        const qa_q2_temp_field *count=temporary_field(temporary,QA_Q2_TEMP_COUNT,QA_Q2_TEMP_INTEGER);
        const qa_q2_temp_field *color=temporary_field(temporary,QA_Q2_TEMP_COLOR,QA_Q2_TEMP_INTEGER);
        if (!count || !color || count->value.integer<0 || count->value.integer>UINT8_MAX ||
            color->value.integer<0 || color->value.integer>UINT8_MAX)
            return frontend_fail(error,QA_ERROR_FORMAT,"Original Q2 splash lost its source byte fields");
        static const uint32_t splash_colors[]={0,0xe0,0xb0,0x50,0xd0,0xe0,0xe8};
        bool splash=temporary->type==QA_Q2_TE_SPLASH;
        electric_splash=splash && color->value.integer==7 &&
            audience->source_frame.kind==QA_RULESET_Q2_RERELEASE;
        recipe=(frontend_q2_particle_recipe){.count=count->value.integer,
            .color=splash ? (color->value.integer>6?0:splash_colors[color->value.integer]) :
                (uint32_t)color->value.integer,
            .fixed=!splash,.upward=temporary->type==QA_Q2_TE_TUNNEL_SPARKS,
            .splash_sparks=splash && color->value.integer==1};
    }
    qa_q2_map_event event={.kind=wall?QA_Q2_MAP_FORCE_WALL:QA_Q2_MAP_STEAM,
        .origin=effect.origin,.direction=effect.direction};
    if (heat || smoke) {
        event.slot=-1;
        event.style=temporary->type==QA_Q2_TE_HEATBEAM_SPARKS ? 8 : smoke ? 0 : 0xe0;
        event.count=temporary->type==QA_Q2_TE_HEATBEAM_SPARKS ? 50 : 20;
        event.value=smoke ? 20 : 60;
    }
    int32_t duration=0;
    if (widow || nuke) {
        const qa_q2_temp_field *id=temporary_field(temporary,QA_Q2_TEMP_ENTITY1,QA_Q2_TEMP_INTEGER);
        if (widow && !id)
            return frontend_fail(error,QA_ERROR_FORMAT,"Q2 widow sustain lost its literal Source ID");
        event.slot=widow?id->value.integer:21000;
        if (!event.slot) return true;
        duration=widow?2100:1000;
    }
    qa_actor_id beam_actor={0},beam_destination={0};
    if (beam_effect) {
        const qa_q2_temp_field *actor=temporary_field(temporary,QA_Q2_TEMP_ENTITY1,QA_Q2_TEMP_INTEGER);
        if (!temporary_actor(message,temporary,actor,&beam_actor,error)) return false;
        if (beam_recipe.destination && !temporary_actor(message,temporary,
            temporary_field(temporary,QA_Q2_TEMP_ENTITY2,QA_Q2_TEMP_INTEGER),&beam_destination,error)) return false;
        if (temporary->type==QA_Q2_TE_GRAPPLE_CABLE) {
            const qa_q2_temp_field *offset=temporary_field(temporary,QA_Q2_TEMP_OFFSET,QA_Q2_TEMP_VECTOR);
            if (!offset) return frontend_fail(error,QA_ERROR_FORMAT,"Q2 grapple lost its Source offset");
            beam_recipe.offset=qa_v3(offset->value.vector[0],offset->value.vector[1],offset->value.vector[2]);
        }
    }
    qa_actor_id flashlight_actor={0};
    const qa_q2_temp_field *flashlight_entity=NULL;
    if (flashlight || power) {
        flashlight_entity=temporary_field(temporary,QA_Q2_TEMP_ENTITY1,QA_Q2_TEMP_INTEGER);
        if (!temporary_actor(message,temporary,flashlight_entity,&flashlight_actor,error)) return false;
    }
    if (wall || steam) {
        const qa_q2_temp_field *color=temporary_field(temporary,QA_Q2_TEMP_COLOR,QA_Q2_TEMP_INTEGER);
        if (!color) return frontend_fail(error,QA_ERROR_FORMAT,"Original Q2 effect has no source color");
        event.style=color->value.integer;
    }
    if (steam) {
        const qa_q2_temp_field *id=temporary_field(temporary,QA_Q2_TEMP_ENTITY1,QA_Q2_TEMP_INTEGER);
        const qa_q2_temp_field *count=temporary_field(temporary,QA_Q2_TEMP_COUNT,QA_Q2_TEMP_INTEGER);
        const qa_q2_temp_field *magnitude=temporary_field(temporary,QA_Q2_TEMP_ENTITY2,QA_Q2_TEMP_INTEGER);
        const qa_q2_temp_field *time=temporary_field(temporary,QA_Q2_TEMP_TIME,QA_Q2_TEMP_INTEGER);
        if (!id || !count || !magnitude || (id->value.integer!=-1 && !time))
            return frontend_fail(error,QA_ERROR_FORMAT,"Original Q2 steam lost its actual sustain fields");
        event.slot=id->value.integer; event.count=count->value.integer;
        event.value=(float)magnitude->value.integer;
        if (time) duration=time->value.integer;
        event.duration=(float)duration;
        /* The source sustain table treats id 0 as a free slot. */
        if (!event.slot) return true;
    }
    uint64_t sample,server; float back_lerp; bool physical;
    if (!client_sample(frontend,&sample,&server,&back_lerp,&physical,error)) return false;
    for (uint32_t seat=0;seat<frontend->options.seats;++seat) {
        if (frontend_network_local_input_owned(frontend,seat)) continue;
        qa_actor_id recipient;
        if (!frontend_seat_actor_read(frontend,seat,&recipient)) continue;
        bool received=false;
        for (size_t i=0;i<audience->count;++i)
            received=received || qa_actor_id_equal(recipient,audience->recipients[i].actor);
        if (!received) continue;
        frontend_q2_controls controls;
        if (!q2_controls(frontend,seat,&controls,error)) return false;
        frontend_particle_owner *owner;
        if (!particle_owner(frontend,message->provider,QA_GAME_Q2,audience->world_source,
                audience->map_identity,recipient,&owner,error)) return false;
        if (!owner) continue;
        uint64_t birth=physical && owner->provider==frontend->particles->clock_source ?
            sample:audience->source_time_ns;
        if (beam_effect) {
            double beam_time;
            if (!q2_beam_clock(frontend,owner,sample,physical,&beam_time,error) ||
                !q2_temporary_beam_admit(frontend,owner,seat,&beam_recipe,beam_actor,beam_destination,
                    effect.origin,effect.direction,beam_time,error)) return false;
        } else if (code>=0) {
            if (temporary->type==QA_Q2_TE_BLOOD && (controls.disable_particles&16u)) continue;
            if (electric_splash) {
                frontend_q2_particle_recipe first=recipe,second=recipe;
                first.color=0x6c; first.count=recipe.count/2; first.splash_sparks=false;
                second.color=0xb0; second.count=(recipe.count+1)/2; second.splash_sparks=true;
                if (!q2_damage_particles(frontend,owner,&effect,seat,audience->source_time_ns,&first,error) ||
                    !q2_damage_particles(frontend,owner,&effect,seat,audience->source_time_ns,&second,error)) return false;
            } else if (!q2_damage_particles(frontend,owner,&effect,seat,audience->source_time_ns,
                    explicit_recipe?&recipe:NULL,error)) return false;
            if (temporary->type==QA_Q2_TE_WELDING_SPARKS) {
                qa_scene_model *flash;
                if (!impact_model(frontend,owner,2,true,&flash,error)) return false;
                int64_t now=(int64_t)(audience->source_time_ns/UINT64_C(1000000));
                *impact_allocate(owner,now)=(frontend_q2_impact){.kind=10,.frames=2,
                    .origin=effect.origin,.start_milliseconds=now-
                        (int64_t)(owner->q2_interval_ns/UINT64_C(1000000)),
                    .light_radius=100+(float)(particle_random(owner)%75)};
            }
        } else if (widow || nuke) {
            if (owner->steam_count==FRONTEND_STEAM_CAPACITY) continue;
            uint64_t span=(uint64_t)duration*UINT64_C(1000000);
            if (span>UINT64_MAX-birth)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 sustain exceeds its real client clock");
            owner->steam[owner->steam_count++]=(frontend_steam){.event=event,
                .end_ns=birth+span,.next_ns=birth,.kind=(uint8_t)(widow?1:2)};
        } else if (power) {
            qa_world *physical_world=qa_application_world(frontend->application);
            if (!qa_actors_get(qa_world_actors(physical_world),flashlight_actor)) continue;
            if (!qa_world_body_storage_serial(physical_world,flashlight_actor)) {
                qa_error skipped = {0};
                qa_error_set(&skipped, QA_ERROR_UNSUPPORTED, 0, "Q2 power splash actor has no shared collision body");
                qa_application_feature_report(frontend->application, "Q2 power splash", &skipped);
                continue;
            }
            qa_body_state entity;
            if (!qa_world_body_read(physical_world,flashlight_actor,&entity,error)) return false;
            q2_power_splash(owner,&entity,birth);
        } else if (flashlight) {
            q2_dlight_set(owner,effect.origin,birth,2,(uint32_t)flashlight_entity->value.integer,flashlight_actor);
        } else if (tracker) {
            q2_tracker_explosion(owner,effect.origin,birth);
            if (!q2_fixed_sound(frontend,owner,&effect,seat,"weapons/disrupthit.wav",1,error)) return false;
        } else if (hyper) {
            q2_blaster_particles(owner,&effect,birth,temporary->type);
        } else if (blaster || berserk) {
            if (effect.direction.z < -1 || effect.direction.z>1)
                return frontend_fail(error,QA_ERROR_FORMAT,"Q2 blaster lost its actual encoded direction");
            uint8_t kind=(uint8_t)(berserk ? 11 : temporary->type==QA_Q2_TE_BLASTER ? 6 :
                temporary->type==QA_Q2_TE_BLASTER2 ? 7 : temporary->type==QA_Q2_TE_FLECHETTE ? 8 : 9);
            qa_scene_model *model;
            if (!impact_model(frontend,owner,kind,true,&model,error)) return false;
            if (berserk) q2_berserk_particles(owner,effect.origin,effect.direction,birth);
            else q2_blaster_particles(owner,&effect,birth,temporary->type);
            float yaw=effect.direction.x != 0.0f ? (float)(atan2(effect.direction.y,effect.direction.x)*180/3.14159265358979323846) :
                effect.direction.y>0 ? 90 : effect.direction.y<0 ? 270 : 0;
            *impact_allocate(owner,(int64_t)(sample/UINT64_C(1000000)))=(frontend_q2_impact){
                .kind=kind,.frames=4,.origin=effect.origin,
                .start_milliseconds=(int64_t)(audience->source_time_ns/UINT64_C(1000000))-
                    (int64_t)(owner->q2_interval_ns/UINT64_C(1000000)),
                .pitch=(float)(acos(effect.direction.z)*180/3.14159265358979323846),.yaw=yaw};
            if (!berserk && !q2_fixed_sound(frontend,owner,&effect,seat,"weapons/lashit.wav",1,error)) return false;
        } else if (poly) {
            bool grenade=temporary->type==QA_Q2_TE_EXPLOSION2 ||
                temporary->type==QA_Q2_TE_EXPLOSION2_NL ||
                temporary->type==QA_Q2_TE_GRENADE_EXPLOSION || temporary->type==QA_Q2_TE_GRENADE_EXPLOSION_WATER;
            bool bfg=temporary->type==QA_Q2_TE_BFG_EXPLOSION;
            uint8_t kind=bfg?4:temporary->type==QA_Q2_TE_EXPLOSION1_BIG?5:3;
            qa_scene_model *model;
            if (!impact_model(frontend,owner,kind,true,&model,error)) return false;
            int64_t start=(int64_t)(audience->source_time_ns/UINT64_C(1000000))-
                (int64_t)(owner->q2_interval_ns/UINT64_C(1000000));
            frontend_q2_impact explosion={.kind=kind,.origin=effect.origin,.start_milliseconds=start,
                .frames=(uint8_t)(bfg?4:grenade?19:15)};
            bool grenade_effect=temporary->type==QA_Q2_TE_GRENADE_EXPLOSION ||
                temporary->type==QA_Q2_TE_GRENADE_EXPLOSION_WATER;
            bool rocket_effect=temporary->type==QA_Q2_TE_ROCKET_EXPLOSION ||
                temporary->type==QA_Q2_TE_ROCKET_EXPLOSION_WATER;
            explosion.light_only=(grenade_effect && (controls.disable_explosions&1u)) ||
                (rocket_effect && (controls.disable_explosions&2u));
            explosion.no_light=temporary->type==QA_Q2_TE_EXPLOSION1_NL ||
                temporary->type==QA_Q2_TE_EXPLOSION2_NL;
            if ((grenade_effect || rocket_effect) && (controls.dlight_hacks&2u)) explosion.light_radius=200;
            if (!bfg) {
                explosion.yaw=(float)(particle_random(owner)%360);
                if (owner->q2_edition==QA_Q2_RERELEASE) {
                    explosion.base_frame=(uint8_t)(particle_unit(owner)<.5f?15:0);
                    if (grenade) explosion.base_frame=30;
                } else explosion.base_frame=(uint8_t)(grenade?30:particle_unit(owner)<.5f?15:0);
            }
            *impact_allocate(owner,(int64_t)(sample/UINT64_C(1000000)))=explosion;
            if (!bfg && temporary->type!=QA_Q2_TE_PLAIN_EXPLOSION &&
                temporary->type!=QA_Q2_TE_EXPLOSION1_BIG && temporary->type!=QA_Q2_TE_EXPLOSION1_NP &&
                !(grenade_effect && (controls.disable_particles&1u)) &&
                !(rocket_effect && (controls.disable_particles&4u)))
                q2_burst_particles(owner,effect.origin,birth,temporary->type,
                    audience->source_frame.kind == QA_RULESET_Q2_RERELEASE);
            if (!bfg && !q2_fixed_sound(frontend,owner,&effect,seat,
                temporary->type==QA_Q2_TE_ROCKET_EXPLOSION_WATER ||
                temporary->type==QA_Q2_TE_GRENADE_EXPLOSION_WATER ? "weapons/xpld_wat.wav" :
                grenade ? "weapons/grenlx1a.wav" : "weapons/rocklx1a.wav",1,error)) return false;
        } else if (laser) {
            int64_t milliseconds = (int64_t)(birth / UINT64_C(1000000));
            for (size_t i = 0; i < FRONTEND_Q2_LASER_CAPACITY; ++i) {
                if (owner->lasers[i].end_milliseconds >= milliseconds) continue;
                uint32_t color = (UINT32_C(0xd0d1d2d3) >> ((particle_random(owner) % 4) * 8)) & 255;
                owner->lasers[i] = (frontend_q2_laser){.start = effect.origin,
                    .end = effect.direction, .color = color, .end_milliseconds = milliseconds + 100,
                    .active = true};
                break;
            }
            if (temporary->type==QA_Q2_TE_BFG_ZAP) {
                qa_scene_model *model;
                if (!impact_model(frontend,owner,4,true,&model,error)) return false;
                *impact_allocate(owner,(int64_t)(sample/UINT64_C(1000000)))=(frontend_q2_impact){
                    .kind=4,.frames=4,.origin=effect.direction,
                    .start_milliseconds=(int64_t)(audience->source_time_ns/UINT64_C(1000000))-
                        (int64_t)(owner->q2_interval_ns/UINT64_C(1000000))};
            }
        } else if (burst) {
            q2_burst_particles(owner,effect.origin,birth,temporary->type,
                audience->source_frame.kind == QA_RULESET_Q2_RERELEASE);
            if (temporary->type==QA_Q2_TE_BOSSTPORT &&
                !q2_fixed_sound(frontend,owner,&effect,seat,"misc/bigtele.wav",0,error)) return false;
        } else if (trail) {
            if (!q2_trail_particles(owner,&effect,birth,temporary->type,error)) return false;
            if (temporary->type==QA_Q2_TE_BUBBLETRAIL2 &&
                !q2_fixed_sound(frontend,owner,&effect,seat,"weapons/lashit.wav",1,error)) return false;
            if (temporary->type==QA_Q2_TE_RAILTRAIL) {
                qa_builtin_event audio = effect;
                audio.origin = effect.direction;
                if (!q2_fixed_sound(frontend,owner,&audio,seat,"weapons/railgf1a.wav",1,error)) return false;
            }
        } else if (wall) {
            if (!force_wall(owner,&event,birth,error)) return false;
        } else if (event.slot==-1) {
            (void)steam_particles(owner,&event,birth,smoke);
            if (heat && !q2_fixed_sound(frontend,owner,&effect,seat,
                    "weapons/lashit.wav",1,error)) return false;
        }
        else if (owner->steam_count<FRONTEND_STEAM_CAPACITY) {
            uint64_t span=duration<0?0:(uint64_t)duration*UINT64_C(1000000);
            if (span>UINT64_MAX-birth)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Original Q2 steam exceeds its actual source clock");
            owner->steam[owner->steam_count++]=(frontend_steam){.event=event,
                .end_ns=birth+span,.next_ns=birth,.expired=duration<0};
        }
    }
    return true;
}

static bool q2_builtin_beam(qa_frontend *frontend,const qa_builtin_event *event,
    const qa_application_q2_audience *audience,const frontend_q2_beam_recipe *recipe,
    uint64_t sample,bool physical,qa_error *error)
{
    if (!q2_delivery_admit(frontend,event->provider,audience)) return true;
    for (uint32_t seat=0;seat<frontend->options.seats;++seat) {
        if (frontend_network_local_input_owned(frontend,seat)) continue;
        qa_actor_id recipient;if (!frontend_seat_actor_read(frontend,seat,&recipient)) continue;
        bool received=false;
        for (size_t i=0;i<audience->count;++i)
            received=received || qa_actor_id_equal(recipient,audience->recipients[i].actor);
        if (!received) continue;
        frontend_particle_owner *owner;
        if (!particle_owner(frontend,event->provider,QA_GAME_Q2,audience->world_source,
            audience->map_identity,recipient,&owner,error)) return false;
        if (!owner) continue;
        double beam_time;
        if (!q2_beam_clock(frontend,owner,sample,physical,&beam_time,error) ||
            !q2_temporary_beam_admit(frontend,owner,seat,recipe,event->actor,event->other,
                event->origin,event->end,beam_time,error)) return false;
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
        qa_builtin_event event;
        if (!qa_application_event_at(frontend->application, i, &event)) continue;
        if (event.family==QA_GAME_Q2 && (event.kind==QA_BUILTIN_TELEPORT ||
            (event.kind==QA_BUILTIN_Q2_ENTITY_EVENT && (event.code==6 || event.code==7))) &&
            frontend->particles && (size_t)event.actor.slot<frontend->particles->visual_sample_capacity) {
            frontend_visual_sample *visual=frontend->particles->visual_samples+event.actor.slot;
            if (qa_actor_id_equal(visual->actor,event.actor)) visual->animation.ready=false;
        }
        if (event.family == QA_GAME_Q1) {
            if (!q1_particle_event(frontend, &event, error)) return false;
        } else if (event.family==QA_GAME_Q2 && event.kind==QA_BUILTIN_BEAM) {
            const char *resource=qa_strings_cstr(qa_session_strings(qa_application_session(frontend->application)),event.resource);
            frontend_q2_beam_recipe recipe;qa_q2_temp_entity temporary;
            bool named=frontend_q2_named_temporary(resource,event.origin,event.end,&temporary);
            if (named || frontend_q2_beam_named_recipe(resource,event.direction,event.value,&recipe)) {
                qa_application_q2_audience audience={0};
                (void)qa_application_event_q2_audience_at(frontend->application,i,&audience);
                if (named) {
                    qa_application_protocol_event message={.provider=event.provider,
                        .dialect=audience.source_frame.kind,.time_ns=event.time_ns};
                    if (!frontend_particle_q2_temporary(frontend,&message,&audience,&temporary,error)) return false;
                } else if (!q2_builtin_beam(frontend,&event,&audience,&recipe,q2_sample,physical,error)) return false;
            }
        } else if (event.family == QA_GAME_Q2 && event.kind == QA_BUILTIN_PARTICLES) {
            frontend_particle_owner *owner;
            qa_application_q2_audience audience={0};
            (void)qa_application_event_q2_audience_at(frontend->application, i, &audience);
            if (!q2_delivery_admit(frontend,event.provider,&audience)) continue;
            for (uint32_t seat = 0; seat < frontend->options.seats; ++seat) {
                if (frontend_network_local_input_owned(frontend,seat)) continue;
                qa_actor_id recipient;
                if (!frontend_seat_actor_read(frontend, seat, &recipient)) continue;
                bool received = false;
                for (size_t j = 0; j < audience.count; ++j)
                    received = received || qa_actor_id_equal(recipient, audience.recipients[j].actor);
                if (!received) continue;
                if (!particle_owner(frontend, event.provider, QA_GAME_Q2, audience.world_source,
                        audience.map_identity, recipient, &owner, error)) return false;
                if (owner && !q2_damage_particles(frontend, owner, &event, seat,
                    audience.source_time_ns, NULL, error)) return false;
            }
        }
    }
    for (uint64_t i = qa_application_events_local_first(frontend->application);
        i < qa_application_events_next(frontend->application); ++i) {
        qa_application_q2_map_event source;
        if (!qa_application_q2_map_event_at(frontend->application, i, &source)) continue;
        if (source.event.kind != QA_Q2_MAP_STEAM && source.event.kind != QA_Q2_MAP_FORCE_WALL) continue;
        qa_application_q2_audience audience={0};
        (void)qa_application_q2_map_event_audience_at(frontend->application, i, &audience);
        if (!q2_delivery_admit(frontend,source.provider,&audience)) continue;
        for (uint32_t seat = 0; seat < frontend->options.seats; ++seat) {
            if (frontend_network_local_input_owned(frontend,seat)) continue;
            qa_actor_id recipient;
            if (!frontend_seat_actor_read(frontend, seat, &recipient)) continue;
            bool received = false;
            for (size_t j = 0; j < audience.count; ++j)
                received = received || qa_actor_id_equal(recipient, audience.recipients[j].actor);
            if (!received) continue;
            frontend_particle_owner *owner;
            if (!particle_owner(frontend, source.provider, QA_GAME_Q2, audience.world_source,
                    audience.map_identity, recipient, &owner, error)) return false;
            if (!owner) continue;
            uint64_t birth = physical && owner->provider == frontend->particles->clock_source ?
                q2_sample : audience.source_time_ns;
            if (source.event.kind == QA_Q2_MAP_FORCE_WALL) {
                if (!force_wall(owner, &source.event, birth, error)) return false;
                continue;
            }
            if (source.event.slot == -1) { (void)steam_particles(owner, &source.event, birth, false); continue; }
            if (!source.event.slot) continue;
            if (owner->steam_count == FRONTEND_STEAM_CAPACITY) continue;
            double ns = trunc((double)source.event.duration * 1e6);
            if (ns < 0 || ns >= (double)UINT64_MAX || (uint64_t)ns > UINT64_MAX - birth)
                return frontend_fail(error, QA_ERROR_ARGUMENT, "steam duration exceeds presentation clock");
            frontend_steam *steam = &owner->steam[owner->steam_count++];
            *steam = (frontend_steam){.event = source.event,
                .end_ns = birth + (uint64_t)ns, .next_ns = birth};
            steam->event.arguments = NULL; steam->event.argument_count = 0;
        }
    }
    return true;
}
static void q2_sustains_sample(frontend_particle_owner *owner,const frontend_q2_sample *sample)
{
    size_t retained=0;
    for (size_t i=0;i<owner->steam_count;++i) {
        frontend_steam steam=owner->steam[i];
        if (steam.expired || steam.end_ns<sample->time_ns) continue;
        if (steam.next_ns<=sample->time_ns) {
            if (steam.kind) q2_sustain_particles(owner,&steam,sample->time_ns);
            else {
                bool emitted=steam_particles(owner,&steam.event,sample->time_ns,false);
                if (emitted || owner->q2_edition==QA_Q2_RERELEASE)
                    steam.next_ns=steam.next_ns>UINT64_MAX-UINT64_C(100000000) ?
                        UINT64_MAX:steam.next_ns+UINT64_C(100000000);
            }
        }
        owner->steam[retained++]=steam;
    }
    owner->steam_count=retained;
}
bool frontend_particle_world(qa_frontend *frontend, uint32_t seat,
    qa_scene_world_input *world, qa_error *error)
{
    if (!frontend || !world || seat>=frontend->options.seats || frontend->resource_inventory)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 explosion lights require their actual physical seat");
    if (frontend_network_local_input_owned(frontend,seat)) return true;
    if (!particle_state(frontend,error)) return false;
    qa_actor_id recipient;
    if (!frontend_seat_actor_read(frontend,seat,&recipient)) return true;
    frontend_q2_controls controls;
    if (!q2_controls(frontend,seat,&controls,error)) return false;
    if (!entity_effects_prepare(frontend,seat,recipient,world,&controls,error)) return false;
    qa_scene_light pending[FRONTEND_Q2_IMPACT_CAPACITY+FRONTEND_Q2_DLIGHT_CAPACITY+32];
    double seconds = (double)qa_session_elapsed(qa_application_session(frontend->application)) / 1e9;
    for (frontend_particle_owner *owner=frontend->particles->owners;owner;owner=owner->next) {
        size_t count=0;
        if (owner->q1) {
            if (owner->recipient.registry && !qa_actor_id_equal(owner->recipient,recipient)) continue;
            for (size_t i=0;i<32;++i) {
                const frontend_q1_temporary_light *light=owner->q1_lights+i;
                float radius=light->recipe.radius-light->recipe.decay*(float)fmax(0,seconds-light->born);
                if (!light->active || light->die<seconds || radius<=0) continue;
                pending[count++]=(qa_scene_light){.origin=light->origin,.color=light->recipe.color,
                    .radius=radius,.minimum=light->recipe.minimum,.scale=1,.additive=true,
                    .family=QA_GAME_Q1,.identity=light->identity};
            }
        } else {
        if (!owner->q2 || !qa_actor_id_equal(owner->recipient,recipient) ||
            !delivery_world_current(frontend,owner->world_source,owner->map_identity)) continue;
        frontend_q2_sample sample;
        if (!q2_owner_sample(frontend,owner,&sample,error)) return false;
        count=owner->entity_light_count;
        if (count) memcpy(pending,owner->entity_lights,count*sizeof(*pending));
        for (size_t i=0;i<FRONTEND_Q2_IMPACT_CAPACITY;++i) {
            const frontend_q2_impact *impact=&owner->impacts[i];
            if (impact->kind<3 || impact->no_light) continue;
            double fraction=impact_fraction(impact,&sample);
            if (floor(fraction)>=(double)(impact_frames(impact)-1)) continue;
            float radius=(impact->light_radius != 0.0f ? impact->light_radius : impact->kind==11 ? 550.f : impact->kind>=6 ?
                (impact->kind==6 && owner->q2_edition==QA_Q2_RERELEASE ? 200.f : 150.f) : 350.f)*impact_alpha(owner,impact,&sample,fraction,controls.smooth);
            if (radius<=0) continue;
            pending[count++]=(qa_scene_light){.origin=impact->origin,
                .color=impact->kind==10 ? (qa_vec3){1,1,.3f} : impact->kind==4 || impact->kind==7 ? (qa_vec3){0,1,0} :
                    impact->kind==6 ? (qa_vec3){1,1,0} : impact->kind==8 || impact->kind==11 ? (qa_vec3){.19f,.41f,.75f} :
                    impact->kind==9 ? (qa_vec3){0,0,1} : (qa_vec3){1,.5f,.5f},
                .radius=radius,.scale=1,
                .additive=true,.family=QA_GAME_Q2};
        }
        for (size_t i=0;i<FRONTEND_Q2_DLIGHT_CAPACITY;++i) {
            const frontend_q2_dlight *light=&owner->lights[i];
            if (!light->active || sample.milliseconds>(double)light->end_milliseconds) continue;
            float radius=light->kind==1 ? 150.f : 400.f;
            if (owner->q2_edition==QA_Q2_RERELEASE)
                radius*=(float)(1-(sample.milliseconds-(double)light->birth_milliseconds)/100);
            if (radius<=0) continue;
            pending[count++]=(qa_scene_light){.origin=light->origin,
                .color=light->kind==1 ? (qa_vec3){-1,-1,-1} : (qa_vec3){1,1,1},
                .radius=radius,.scale=1,.additive=true,.family=QA_GAME_Q2};
        }
        }
        if (!count) continue;
        if (world->light_count>SIZE_MAX-count ||
            world->light_count+count>SIZE_MAX/sizeof(qa_scene_light))
            return frontend_fail(error,QA_ERROR_MEMORY,"Q2 explosion light array exceeds native storage");
        qa_scene_light *lights=qa_arena_alloc(&frontend->frame.storage,
            (world->light_count+count)*sizeof(*lights),_Alignof(qa_scene_light),error);
        if (!lights) return false;
        if (world->light_count) memcpy(lights,world->lights,world->light_count*sizeof(*lights));
        memcpy(lights+world->light_count,pending,count*sizeof(*lights));
        world->lights=lights; world->light_count+=count;
    }
    return true;
}
typedef struct q2_beam_draw_context {
    qa_frontend *frontend;
    frontend_particle_owner *owner;
    const qa_scene_world_input *world;
    double milliseconds;
} q2_beam_draw_context;
static bool local_beam_model_ready(void *context,q2fx_model model)
{ return ((q2_beam_draw_context *)context)->owner->beam_models[model]!=NULL; }
static bool local_beam_model_draw(void *context,const frontend_q2_beam_draw *draw,qa_error *error)
{
    q2_beam_draw_context *output=context;
    qa_model_transform placement;qa_model_transform_identity(&placement);
    placement.origin[0]=draw->origin.x;placement.origin[1]=draw->origin.y;placement.origin[2]=draw->origin.z;
    qa_vec3 axes[3];frontend_camera_axes(draw->angles,axes);
    for (size_t i=0;i<3;++i) {
        placement.axes[i][0]=axes[i].x;placement.axes[i][1]=axes[i].y;placement.axes[i][2]=axes[i].z;
        placement.scale[i]=i==0?draw->scale.x:i==1?draw->scale.y:draw->scale.z;
    }
    qa_scene_model_input input={.view=output->world->view,.transform=placement,.previous_origin=draw->origin,
        .family=QA_GAME_Q2,.frame=(uint32_t)draw->frame,.old_frame=(uint32_t)draw->old_frame,
        .skin=(uint32_t)draw->skin,.flags=draw->flags,.back_lerp=draw->back_lerp,.color={1,1,1,draw->alpha},
        .seconds=output->milliseconds*.001,.ambient={1,1,1},.identity_light=output->world->identity_light,
        .source_path=q2fx_model_paths[draw->model],.video_frame=output->world->video_frame,.video_context=output->world->video_context};
    if (output->frontend->scene_world && !qa_scene_world_sample_light_input(output->frontend->scene_world,
        output->world,draw->origin,&input.ambient,&input.directed,&input.light_direction,error)) return false;
    return frontend_legacy_model_input(output->frontend->scene_world,output->world,&input,error) &&
        qa_scene_model_submit(output->owner->beam_models[draw->model],&input,&output->frontend->frame,error);
}
static bool local_beam_render_clock(void *context,uint64_t *wall,uint64_t *frame,qa_error *error)
{
    (void)error;qa_frontend *frontend=((q2_beam_draw_context *)context)->frontend;
    *wall=frontend->wall_time_ns/UINT64_C(1000000);*frame=frontend->frame_number;return true;
}
static bool q2_beams_draw(qa_frontend *frontend,frontend_particle_owner *owner,uint32_t seat,
    const qa_scene_world_input *world,const frontend_q2_sample *sample,qa_error *error)
{
    if (!owner->beam_active) return true;
    double beam_time;
    if (!q2_beam_clock(frontend,owner,sample->time_ns,sample->physical,&beam_time,error)) return false;
    frontend_q2_beam_view view={.milliseconds=beam_time,.view=world->view,.viewer=owner->recipient,
        .hardware=frontend->gl!=NULL,.player_fov=atanf(1/world->view.projection.m[0])*114.59155902616464f};
    frontend_source_client_registry source;bool found;
    if (!frontend_source_client_registry_read(frontend,seat,&source,&found,error)) return false;
    if (found) {
        const frontend_particle_cvars *bindings=particle_cvars_bind(frontend,seat,source.cvars,error);
        if (!bindings) return false;
        const qa_cvar_view *setting=qa_cvars_read(source.cvars,bindings->hand);if (setting) view.hand=setting->integer;
        setting=qa_cvars_read(source.cvars,bindings->gun);if (setting) view.gun=setting->integer;
        setting=qa_cvars_read(source.cvars,bindings->gun_fov);if (setting) view.gun_fov=(float)setting->number;
    }
    qa_application_native_q2_player_sample player;uint32_t old_frame;float back_lerp;
    if (!frontend_particle_q2_player_sample(frontend,seat,owner->recipient,&player,&old_frame,&back_lerp,&found,error)) return false;
    if (found) {
        view.gun_offset=player.gun_offset;view.player_fov=player.fov;view.viewer_origin=player.origin;view.viewer_origin_present=true;
    } else {
        const frontend_seat *recipient=frontend->seats+seat;
        if (recipient->q2_view_ready && qa_actor_id_equal(recipient->q2_actor,owner->recipient)) {
            view.gun_offset=recipient->q2_view.gun_offset;view.player_fov=recipient->q2_view.fov;
        }
        qa_application_visual_view appearance;
        if (!frontend_particle_visual_read(frontend,owner->recipient,&appearance,&found,error)) return false;
        if (found) { view.viewer_origin=appearance.body.origin;view.viewer_origin_present=true; }
    }
    q2_beam_draw_context output={frontend,owner,world,view.milliseconds};
    frontend_q2_beam_context context={.rerelease=owner->q2_edition==QA_Q2_RERELEASE,
        .particles=owner->q2_particles,.random=&owner->random,.roll=&owner->beam_random,.context=&output,
        .model_ready=local_beam_model_ready,.draw=local_beam_model_draw,.render_clock=local_beam_render_clock};
    bool advance=!owner->beam_sampled || view.milliseconds>owner->beam_sample_ms;
    if (!frontend_q2_beams_prepare(&context,owner->beams,32,&view,advance,error) ||
        !frontend_q2_beams_prepare(&context,owner->player_beams,32,&view,advance,error)) return false;
    owner->beam_sample_ms=view.milliseconds;owner->beam_sampled=true;owner->beam_active=context.active_beams!=0;return true;
}
bool frontend_particle_draw(qa_frontend *frontend, uint32_t seat, const qa_scene_world_input *world, qa_error *error)
{
    if (!frontend || !world || seat >= frontend->options.seats)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Particle draw requires its actual physical seat");
    if (frontend_network_local_input_owned(frontend,seat)) return true;
    const qa_scene_view *view = &world->view;
    if (!frontend->particles && !particle_state(frontend,error)) return false;
    frontend_q2_controls controls;
    if (!q2_controls(frontend,seat,&controls,error)) return false;
    uint64_t now = qa_session_elapsed(qa_application_session(frontend->application));
    double seconds = (double)now / 1e9;
    for (frontend_particle_owner *owner = frontend->particles->owners; owner; owner = owner->next) {
        frontend_q2_sample sample = {0};
        if (owner->q1 && owner->recipient.registry) {
            qa_actor_id recipient;
            if (!frontend_seat_actor_read(frontend, seat, &recipient) ||
                !qa_actor_id_equal(recipient, owner->recipient)) continue;
        }
        if (owner->q1) {
            qa_actor_id viewer={0};
            (void)frontend_seat_actor_read(frontend,seat,&viewer);
            for (size_t i=0;i<24;++i) {
                const frontend_q1_temporary_beam *beam=owner->q1_beams+i;
                if (!beam->active || beam->die<seconds) continue;
                uint8_t model_index=beam->type==5?0:beam->type==6?1:beam->type==9?2:3;
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
        if (owner->q2) {
            qa_actor_id recipient;
            if (!frontend_seat_actor_read(frontend, seat, &recipient) ||
                !qa_actor_id_equal(recipient, owner->recipient) ||
                !delivery_world_current(frontend, owner->world_source, owner->map_identity)) continue;
            if (!q2_owner_sample(frontend, owner, &sample, error)) return false;
            if (!q2_beams_draw(frontend,owner,seat,world,&sample,error)) return false;
            q2_sustains_sample(owner,&sample);
            /* The Source allocates sustain particles before AddParticles
             * frees instant particles sampled by the preceding scene. */
            size_t retained=0;
            for (size_t i=0;i<owner->q2_particles->count;++i) {
                const frontend_fx_q2_particle *particle=owner->q2+i;
                if (particle->alpha_velocity==-10000 && particle->alpha<=0) continue;
                owner->q2[retained++]=*particle;
            }
            owner->q2_particles->count=retained;
        }
        qa_bytes palette;
        qa_game_family family = owner->family == QA_GAME_Q1 ? QA_GAME_Q1 : QA_GAME_Q2;
        if (!qa_scene_resources_palette(owner->images, family, &palette, error)) return false;
        for (size_t i = 0; owner->q2 && i < FRONTEND_Q2_IMPACT_CAPACITY; ++i) {
            const frontend_q2_impact *impact = &owner->impacts[i];
            if (!impact->kind || impact->kind==10 || impact->light_only) continue;
            double fraction = impact_fraction(impact,&sample);
            double source_frame = floor(fraction);
            if (source_frame >= (double)(impact_frames(impact)-1)) continue;
            uint32_t current = source_frame < 0 ? 0 : (uint32_t)source_frame;
            qa_scene_model *model;
            if (!impact_model(frontend, owner, impact->kind, false, &model, error)) return false;
            qa_model_transform placement; qa_model_transform_identity(&placement);
            placement.origin[0] = impact->origin.x;
            placement.origin[1] = impact->origin.y;
            placement.origin[2] = impact->origin.z;
            float model_scale=impact->kind==11 ? 3 : impact->kind==5 && owner->q2_edition==QA_Q2_RERELEASE ? 2 : 1;
            qa_vec3 axes[3]; frontend_camera_axes((qa_vec3){impact->pitch,impact->yaw,0},axes);
            for (unsigned axis=0;axis<3;++axis) {
                placement.axes[axis][0]=axes[axis].x*model_scale;
                placement.axes[axis][1]=axes[axis].y*model_scale;
                placement.axes[axis][2]=axes[axis].z*model_scale;
            }
            qa_scene_model_input input = {.view = *view, .transform = placement,
                .previous_origin = impact->origin, .family = QA_GAME_Q2,
                .color = {1, 1, 1, impact_alpha(owner,impact,&sample,fraction,controls.smooth)},
                .frame = impact->base_frame + current + 1, .old_frame = impact->base_frame + current,
                .skin=impact->kind>=6 ? (uint32_t)(impact->kind==9 || impact->kind==11 ? 2 : impact->kind-6) :
                    impact->kind>=3 ? (current<10 ? current>>1 : current<13 ? 5u : 6u) : 0,
                .back_lerp = sample.physical && owner->q2_edition==QA_Q2_CLASSIC && !controls.modern ? sample.back_lerp :
                    (float)(1 - (fraction - current)),
                .flags = impact->kind>=6 ? 40u : impact->kind == 1 ? 32u : 8u |
                    (impact->kind==4 || (impact->kind>=3 &&
                        (current>=10 || owner->q2_edition==QA_Q2_RERELEASE || controls.smooth)) ? 32u : 0u), .entity = (uint32_t)i,
                .identity_light = 1, .seconds = (double)sample.milliseconds / 1000,
                .ambient = {1, 1, 1}, .source_path = impact_path(owner,impact->kind)};
            if (frontend->scene_world)
                qa_scene_world_sample_light(frontend->scene_world, impact->origin,
                    &input.ambient, &input.directed, &input.light_direction);
            if (!qa_scene_model_submit(model, &input, &frontend->frame, error)) return false;
        }
        for (size_t i = 0; owner->q2 && i < FRONTEND_Q2_LASER_CAPACITY; ++i) {
            const frontend_q2_laser *laser = &owner->lasers[i];
            if (!laser->active || (double)laser->end_milliseconds < sample.milliseconds) continue;
            qa_vec4 color = {palette.data[laser->color * 3] / 255.0f,
                palette.data[laser->color * 3 + 1] / 255.0f,
                palette.data[laser->color * 3 + 2] / 255.0f, .3f};
            if (!qa_scene_beam(&frontend->frame, view, laser->start, laser->end,
                    4, color, NULL, error)) return false;
        }
        size_t particle_count = owner->q1 ? owner->q1->count : owner->q2_particles->count;
        qa_scene_particle_sample *particles = qa_scene_particles_alloc(&frontend->frame, particle_count, error);
        if (particle_count && !particles) return false;
        qa_scene_particle_batch batch = {.view = *view, .family = family,
            .image = owner->particle_image, .samples = particles};
        for (size_t i = particle_count; i > 0; --i) {
            qa_vec3 origin; float alpha = 1; uint32_t index;
            if (owner->q1) {
                const qa_scene_q1_particle_state *particle = &owner->q1->values.q1[i - 1];
                if (particle->die < seconds) continue;
                origin = particle->origin; index = particle->color & 255;
            } else {
                const frontend_fx_q2_particle *particle = &owner->q2[i - 1];
                if (!frontend_fx_q2_sample(particle, sample.milliseconds, &origin, &alpha)) continue;
                index = particle->color & 255;
            }
            qa_vec4 color = {palette.data[index * 3] / 255.0f, palette.data[index * 3 + 1] / 255.0f,
                palette.data[index * 3 + 2] / 255.0f, alpha};
            particles[batch.count++] = (qa_scene_particle_sample){.origin = origin, .color = color};
            if (owner->q2 && owner->q2[i-1].alpha_velocity==-10000)
                owner->q2[i-1].alpha=0;
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
        frontend_q2_sample sample = {0};
        if (owner->q2 && !q2_owner_sample(frontend, owner, &sample, error)) return false;
        for (size_t i = 0; owner->q2 && i < FRONTEND_Q2_IMPACT_CAPACITY; ++i) {
            frontend_q2_impact *impact = &owner->impacts[i];
            double source_frame = floor(impact_fraction(impact,&sample));
            if (impact->kind && source_frame >= (double)(impact_frames(impact)-1))
                *impact = (frontend_q2_impact){0};
        }
        if (owner->q1) {
            frontend_fx_q1_advance(owner->q1, seconds, elapsed, owner->gravity);
        } else {
            size_t retained = 0;
            for (size_t i = 0; i < owner->q2_particles->count; ++i) {
                qa_vec3 origin; float alpha;
                if (owner->q2[i].alpha_velocity!=-10000 &&
                    !frontend_fx_q2_sample(&owner->q2[i], sample.milliseconds, &origin, &alpha)) continue;
                owner->q2[retained++] = owner->q2[i];
            }
            owner->q2_particles->count = retained;
        }
    }
    return true;
}
