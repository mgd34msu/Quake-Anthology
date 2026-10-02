#include "internal.h"
#include "qa/scene_effects.h"
#include "save_private.h"
#include "visual_access.h"
#include "qa/game_q2_feedback.h"
#include "qa/application_native_q2_presentation.h"
#include "qa/application_native_q2_client.h"
#include "particle_clock.h"
#include "particle_delivery.h"
#include "particle_audio.h"
#include "native_q2_messages.h"
#include "qa/application_native_q2_delivery.h"
#include "qa/application_selected_effects.h"
#include "source_client_registry.h"

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
    qa_scene_q1_particle_state *q1;
    qa_scene_q2_particle_state *q2;
    size_t count;
    frontend_q2_impact impacts[FRONTEND_Q2_IMPACT_CAPACITY];
    qa_scene_model *impact_models[9]; /* Borrowed from this frontend's actual appearance owner. */
    frontend_q2_laser lasers[FRONTEND_Q2_LASER_CAPACITY];
    frontend_q2_dlight lights[FRONTEND_Q2_DLIGHT_CAPACITY];
    frontend_steam steam[FRONTEND_STEAM_CAPACITY];
    size_t steam_count;
} frontend_particle_owner;
typedef struct frontend_q2_client_sample {
    qa_application_native_q2_client client;
    qa_application_native_q2_player_sample player;
    uint32_t physical_seat;
} frontend_q2_client_sample;
struct frontend_particle_state {
    frontend_particle_owner *owners;
    uint64_t sample_ns;
    qa_actor_owner clock_source;
    uint64_t clock_map_revision, client_ns, client_host_ns, server_ns, server_frame;
    uint64_t client_frame, client_interval_ns;
    bool client_clock, client_pending;
    qa_application_native_q2_entity_sample *source_entities;
    size_t source_entity_count;
    frontend_q2_client_sample *source_clients;
    size_t source_client_count;
};
static bool particle_state(qa_frontend *frontend, qa_error *error)
{
    if (frontend->particles) return true;
    frontend->particles = calloc(1, sizeof(*frontend->particles));
    if (!frontend->particles)
        return frontend_fail(error, QA_ERROR_MEMORY, "allocating frontend particle continuation");
    frontend->particles->sample_ns = qa_session_elapsed(qa_application_session(frontend->application));
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
        state->clock_map_revision != source.map_revision) {
        state->client_ns = 0;
        state->clock_source = source.source_owner;
        state->clock_map_revision = source.map_revision;
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
        source.map_revision != state->clock_map_revision)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 client sampling lost its committed physical source frame");
    uint32_t extent;
    if (!qa_application_native_q2_presentation_extent(frontend->application,&source,&extent,error)) return false;
    if ((uint64_t)extent*sizeof(*state->source_entities)>SIZE_MAX ||
        (uint64_t)frontend->options.seats*sizeof(*state->source_clients)>SIZE_MAX)
        return frontend_fail(error,QA_ERROR_MEMORY,"Q2 Source sample exceeds native storage");
    qa_application_native_q2_entity_sample *entities=extent?calloc(extent,sizeof(*entities)):NULL;
    frontend_q2_client_sample *clients=calloc(frontend->options.seats,sizeof(*clients));
    if ((extent && !entities) || !clients) {
        free(entities); free(clients);
        return frontend_fail(error,QA_ERROR_MEMORY,"Retaining completed local Q2 Source samples");
    }
    size_t entity_count=0,client_count=0;
    bool ok=true;
    for (uint32_t slot=0;ok && slot<extent;++slot) {
        qa_application_native_q2_entity_sample entity;
        bool present;
        ok=qa_application_native_q2_presentation_sample(frontend->application,&source,slot,&entity,&present,error);
        if (ok && present) entities[entity_count++]=entity;
    }
    for (uint32_t physical=0;ok && physical<frontend->options.seats;++physical) {
        uint32_t seat;
        if (!frontend_seat_launch_id_read(frontend,physical,&seat)) continue;
        qa_application_native_q2_client client;
        bool present;
        ok=qa_application_native_q2_presentation_local(frontend->application,&source,seat,&client,&present,error);
        if (ok && present) {
            frontend_q2_client_sample sample={.client=client,.physical_seat=physical};
            ok=qa_application_native_q2_presentation_player(frontend->application,&source,&client,&sample.player,error);
            if (ok) clients[client_count++]=sample;
        }
    }
    for (size_t i=0;ok && i<entity_count;++i)
        if (!qa_actors_get(qa_session_actors(source.session),entities[i].actor))
            ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 Source sample retired an earlier captured actor");
    if (ok && !qa_application_native_q2_presentation_current(frontend->application,&source))
        ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 Source sample changed its completed frame");
    if (!ok) { free(entities); free(clients); return false; }
    free(state->source_entities); free(state->source_clients);
    state->source_entities=entities; state->source_entity_count=entity_count;
    state->source_clients=clients; state->source_client_count=client_count;
    uint64_t lower = source.server_time_ns > state->client_interval_ns ?
        source.server_time_ns - state->client_interval_ns : 0;
    if (state->client_ns > source.server_time_ns) state->client_ns = source.server_time_ns;
    else if (state->client_ns < lower) state->client_ns = lower;
    state->server_ns = source.server_time_ns;
    state->server_frame = source.clock.frame.number;
    state->client_pending = false;
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
    if (state->client_pending || state->client_frame != frontend->frame_number ||
        state->client_host_ns != frontend->time_ns ||
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
static bool impact_model(qa_frontend *, frontend_particle_owner *, uint8_t,
    bool acquire, qa_scene_model **, qa_error *);
static bool delivery_world_current(qa_frontend *frontend, qa_actor_owner world_source,
    uint64_t map_identity)
{
    qa_application_map_view map;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(qa_application_launch(frontend->application));
    const qa_launch_binding *binding = choices ? qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "") : NULL;
    qa_actor_owner physical = 0;
    qa_collision_geometry *geometry = qa_world_geometry(qa_application_world(frontend->application));
    return binding && binding->instance && world_source && map_identity && geometry &&
        qa_application_map_read(frontend->application, &map) &&
        qa_application_provider_owner(frontend->application, binding->instance, &physical) &&
        physical == world_source && qa_collision_map_identity(geometry) == map_identity;
}
static bool particle_client_current(qa_frontend *frontend, const frontend_particle_owner *owner)
{
    if (!delivery_world_current(frontend, owner->world_source, owner->map_identity)) return false;
    for (uint32_t seat = 0; seat < frontend->options.seats; ++seat) {
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
    bool negative, physical;
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
    if (physical) {
        if (frontend->particles->clock_source != owner->world_source)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 client sample differs from its delivered world");
        *out = (frontend_q2_sample){.milliseconds = (double)(sample / UINT64_C(1000000)),
            .time_ns = sample, .back_lerp = back_lerp, .physical = true};
        return true;
    }
    qa_application_selected_effects source;
    if (!qa_application_effects_producer_read(frontend->application, owner->world_source,
            &source, error)) return false;
    if (source.provider != owner->world_source || source.primary != owner->world_source ||
        !qa_application_selected_effects_current(frontend->application, &source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 sampling lost its actual ENTITIES clock receipt");
    frontend_q2_sample view = {0};
    if (source.primary_kind == QA_APPLICATION_EFFECTS_Q3) {
        view.milliseconds = source.sample_time_ms;
        view.negative = view.milliseconds < 0;
        if (!view.negative) view.time_ns = (uint64_t)view.milliseconds * UINT64_C(1000000);
    } else {
        view.time_ns = source.source_time_ns;
        view.milliseconds = (double)view.time_ns / 1000000.0;
    }
    *out = view;
    return true;
}
static void particle_owner_free(frontend_particle_owner *owner)
{
    qa_scene_image_release(owner->particle_image);
    free(owner->q1); free(owner->q2); free(owner);
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
static bool particle_owner(qa_frontend *frontend, qa_actor_owner provider, qa_game_family family,
    qa_actor_owner world_source, uint64_t map_identity, qa_actor_id recipient,
    frontend_particle_owner **out, qa_error *error)
{
    if (family == QA_GAME_Q2 && (!recipient.registry ||
            !delivery_world_current(frontend, world_source, map_identity)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 particle owner lost its actual delivered world");
    if (!particle_state(frontend, error)) return false;
    for (frontend_particle_owner *owner = frontend->particles->owners; owner; owner = owner->next)
        if (owner->provider == provider && owner->family == family &&
            owner->world_source == world_source && owner->map_identity == map_identity &&
            qa_actor_id_equal(owner->recipient, recipient)) { *out = owner; return true; }
    frontend_particle_owner *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "allocating provider particle pool");
    owner->provider = provider; owner->family = family;
    owner->world_source = world_source; owner->map_identity = map_identity; owner->recipient = recipient;
    if (family==QA_GAME_Q2) {
        bool found;
        if (!qa_application_native_q2_source_clock_read(frontend->application,provider,
                &owner->q2_edition,&owner->q2_interval_ns,&found,error) || !found) {
            free(owner);
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 particles require their actual Source profile and interval");
        }
    }
    qa_builtin_random_seed(&owner->random, 1);
    if (family == QA_GAME_Q1) owner->q1 = calloc(FRONTEND_PARTICLE_CAPACITY, sizeof(*owner->q1));
    else owner->q2 = calloc(FRONTEND_PARTICLE_CAPACITY, sizeof(*owner->q2));
    bool ok = (owner->q1 || owner->q2) && frontend_event_images(frontend, provider, family, &owner->images, error) &&
        qa_scene_particle_image(owner->images, family == QA_GAME_Q1 ? QA_SCENE_Q1 : QA_SCENE_Q2, &owner->particle_image, error);
    if (!ok) {
        if (!owner->q1 && !owner->q2) frontend_fail(error, QA_ERROR_MEMORY, "allocating bounded source particle pool");
        qa_scene_image_release(owner->particle_image); free(owner->q1); free(owner->q2); free(owner); return false;
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
    free(frontend->particles->source_clients);
    free(frontend->particles); frontend->particles = NULL;
}
void frontend_particle_reset_round(qa_frontend *frontend)
{
    frontend_particle_state *state = frontend->particles;
    if (!state) return;
    for (frontend_particle_owner *owner = state->owners; owner; owner = owner->next) {
        owner->count = 0;
        memset(owner->impacts, 0, sizeof(owner->impacts));
        memset(owner->lasers, 0, sizeof(owner->lasers));
        memset(owner->lights, 0, sizeof(owner->lights));
        owner->steam_count = 0;
        memset(owner->steam, 0, sizeof(owner->steam));
    }
    state->sample_ns = qa_session_elapsed(qa_application_session(frontend->application));
}
static bool particle_signature(qa_source_save_io *io)
{
    uint8_t magic[4] = {'Q', 'A', 'P', 'T'}; uint32_t version = 10;
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "QAPT", 4) &&
        qa_source_save_u32(io, &version) && version == 10;
}
static bool source_sample_fields(qa_frontend *frontend,qa_source_save_io *io,frontend_particle_state *state)
{
    size_t entities=state->source_entity_count,clients=state->source_client_count;
    size_t maximum=io->direction==QA_SOURCE_SAVE_READ ? io->input.size/57 : UINT32_MAX;
    if (!qa_source_save_count(io,&entities,maximum) ||
        !qa_source_save_count(io,&clients,frontend->options.seats)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (entities>SIZE_MAX/sizeof(*state->source_entities)) return false;
        state->source_entities=entities?calloc(entities,sizeof(*state->source_entities)):NULL;
        state->source_clients=clients?calloc(clients,sizeof(*state->source_clients)):NULL;
        if ((entities && !state->source_entities) || (clients && !state->source_clients)) return false;
        state->source_entity_count=entities; state->source_client_count=clients;
    }
    for (size_t i=0;i<entities;++i) {
        qa_application_native_q2_entity_sample *entity=state->source_entities+i;
        if (!qa_source_save_actor(io,&entity->actor) || !entity->actor.registry ||
            !qa_source_save_u32(io,&entity->source_slot) ||
            (i && entity->source_slot<=state->source_entities[i-1].source_slot) ||
            !qa_source_save_vec3(io,&entity->origin) || !qa_vec_finite(entity->origin) ||
            !qa_source_save_vec3(io,&entity->angles) || !qa_vec_finite(entity->angles) ||
            !qa_source_save_vec3(io,&entity->solid_bounds.mins) || !qa_vec_finite(entity->solid_bounds.mins) ||
            !qa_source_save_vec3(io,&entity->solid_bounds.maxs) || !qa_vec_finite(entity->solid_bounds.maxs) ||
            !qa_source_save_f32(io,&entity->solid_radius) || !isfinite(entity->solid_radius) ||
            entity->solid_radius<0 || entity->solid_radius!=
                qa_vec_length(qa_vec_sub(entity->solid_bounds.maxs,entity->solid_bounds.mins))*.5f) return false;
    }
    for (size_t i=0;i<clients;++i) {
        frontend_q2_client_sample *client=state->source_clients+i;
        if (!qa_source_save_actor(io,&client->client.actor) || !client->client.actor.registry ||
            !qa_source_save_u32(io,&client->client.seat) || !qa_source_save_u32(io,&client->client.client_slot) ||
            !qa_source_save_u32(io,&client->physical_seat) || client->physical_seat>=frontend->options.seats ||
            (i && client->physical_seat<=state->source_clients[i-1].physical_seat) ||
            !qa_source_save_bool(io,&client->player.present) ||
            !qa_source_save_vec3(io,&client->player.origin) || !qa_vec_finite(client->player.origin) ||
            !qa_source_save_vec3(io,&client->player.view_angles) || !qa_vec_finite(client->player.view_angles) ||
            !qa_source_save_vec3(io,&client->player.view_offset) || !qa_vec_finite(client->player.view_offset) ||
            !qa_source_save_vec3(io,&client->player.gun_offset) || !qa_vec_finite(client->player.gun_offset) ||
            (!client->player.present && (client->player.origin.x || client->player.origin.y || client->player.origin.z ||
                client->player.view_angles.x || client->player.view_angles.y || client->player.view_angles.z ||
                client->player.view_offset.x || client->player.view_offset.y || client->player.view_offset.z ||
                client->player.gun_offset.x || client->player.gun_offset.y || client->player.gun_offset.z))) return false;
    }
    return true;
}
static bool client_clock_fields(qa_frontend *frontend, qa_source_save_io *io,
    frontend_particle_state *state)
{
    if (state->client_pending || !qa_source_save_bool(io, &state->client_clock)) return false;
    if (!state->client_clock) return true;
    if (!frontend_save_provider(io, frontend->application, &state->clock_source) ||
        !state->clock_source || !qa_source_save_u64(io, &state->client_ns) ||
        !qa_source_save_u64(io, &state->client_host_ns) || !qa_source_save_u64(io, &state->server_ns) ||
        !qa_source_save_u64(io, &state->server_frame) || !qa_source_save_u64(io, &state->client_frame) ||
        !qa_source_save_u64(io,&state->client_interval_ns) || !state->client_interval_ns ||
        state->client_ns > state->server_ns || state->server_ns - state->client_ns > state->client_interval_ns)
        return false;
    if (!source_sample_fields(frontend,io,state)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        /* MEDIA imports precede the real saved session-clock join. Keep the
         * decoded receipt inert; final canonical capture qualifies it only
         * after application_save_foundation_finish restores those clocks. */
        qa_application_map_view map;
        if (!qa_application_map_read(frontend->application, &map)) return false;
        state->clock_map_revision = map.revision;
        return true;
    }
    if (state->client_host_ns != frontend->time_ns ||
        state->client_frame == UINT64_MAX ||
        state->client_frame + 1 != frontend->frame_number)
        return false;
    qa_application_native_q2_presentation source;
    bool found;
    if (!qa_application_native_q2_presentation_retained_selected(frontend->application, &source, &found, io->error) ||
        !found || source.clock_config.interval_ns!=state->client_interval_ns || source.source_owner != state->clock_source ||
        source.server_time_ns != state->server_ns || source.clock.frame.number != state->server_frame)
        return false;
    if (state->clock_map_revision != source.map_revision) return false;
    for (size_t i=0;i<state->source_client_count;++i) {
        const frontend_q2_client_sample *client=state->source_clients+i;
        qa_application_native_q2_client actual;
        uint32_t seat;
        if (!frontend_seat_launch_id_read(frontend,client->physical_seat,&seat) || seat!=client->client.seat ||
            !qa_application_native_q2_presentation_local(frontend->application,&source,seat,&actual,&found,io->error) ||
            !found || actual.client_slot!=client->client.client_slot || !qa_actor_id_equal(actual.actor,client->client.actor))
            return false;
    }
    return true;
}
static bool particle_fields(qa_source_save_io *io, frontend_particle_owner *owner)
{
    if (owner->q2) {
        uint32_t edition=(uint32_t)owner->q2_edition;
        uint64_t interval=owner->q2_interval_ns;
        if (!qa_source_save_u32(io,&edition) || edition!=(uint32_t)owner->q2_edition ||
            !qa_source_save_u64(io,&interval) || interval!=owner->q2_interval_ns) return false;
    }
    if (!frontend_save_random(io, &owner->random) || !qa_source_save_f32(io, &owner->gravity) ||
        !isfinite(owner->gravity) || !qa_source_save_count(io, &owner->count, FRONTEND_PARTICLE_CAPACITY)) return false;
    for (size_t i = 0; i < owner->count; ++i) {
        if (owner->q1) {
            qa_scene_q1_particle_state p = owner->q1[i]; uint32_t kind = p.kind;
            if (!qa_source_save_vec3(io, &p.origin) || !qa_source_save_vec3(io, &p.velocity) ||
                !qa_source_save_f32(io, &p.ramp) || !qa_source_save_f64(io, &p.die) ||
                !qa_source_save_u32(io, &p.color) || !qa_source_save_u32(io, &kind) || kind > QA_Q1_PARTICLE_SLOW_GRAVITY ||
                !qa_vec_finite(p.origin) || !qa_vec_finite(p.velocity) || !isfinite(p.ramp) || !isfinite(p.die)) return false;
            if (io->direction == QA_SOURCE_SAVE_READ) { p.kind = (qa_scene_q1_particle_kind)kind; owner->q1[i] = p; }
        } else {
            qa_scene_q2_particle_state p = owner->q2[i];
            if (!qa_source_save_i64(io, &p.spawn_milliseconds) || !qa_source_save_vec3(io, &p.origin) ||
                !qa_source_save_vec3(io, &p.velocity) || !qa_source_save_vec3(io, &p.acceleration) ||
                !qa_source_save_u32(io, &p.color) || !qa_source_save_f32(io, &p.alpha) || !qa_source_save_f32(io, &p.alpha_velocity) ||
                !qa_vec_finite(p.origin) || !qa_vec_finite(p.velocity) || !qa_vec_finite(p.acceleration) ||
                !isfinite(p.alpha) || !isfinite(p.alpha_velocity)) return false;
            if (io->direction == QA_SOURCE_SAVE_READ) owner->q2[i] = p;
        }
    }
    for (size_t i = 0; owner->q2 && i < FRONTEND_Q2_IMPACT_CAPACITY; ++i) {
        frontend_q2_impact *impact = &owner->impacts[i];
        if (!qa_source_save_u8(io, &impact->kind) || impact->kind > 11 ||
            !qa_source_save_i64(io, &impact->start_milliseconds) ||
            !qa_source_save_vec3(io, &impact->origin) || !qa_vec_finite(impact->origin) ||
            !qa_source_save_u8(io,&impact->frames) || !qa_source_save_u8(io,&impact->base_frame) ||
            !qa_source_save_f32(io,&impact->pitch) || !isfinite(impact->pitch) ||
            !qa_source_save_f32(io,&impact->yaw) || !isfinite(impact->yaw) ||
            !qa_source_save_f32(io,&impact->light_radius) || !isfinite(impact->light_radius) ||
            !qa_source_save_bool(io,&impact->light_only) || !qa_source_save_bool(io,&impact->no_light) ||
            ((impact->light_only || impact->no_light) && impact->kind!=3)) return false;
        if (impact->kind==10) {
            if (impact->frames!=2 || impact->base_frame || impact->pitch || impact->yaw ||
                impact->light_radius<100 || impact->light_radius>174 ||
                impact->light_radius!=truncf(impact->light_radius)) return false;
        } else if (impact->light_radius && (impact->kind!=3 || impact->light_radius!=200)) return false;
        else if (impact->kind>=6) {
            if (impact->frames!=4 || impact->base_frame || impact->pitch<0 || impact->pitch>180 ||
                impact->yaw < -180 || impact->yaw>270) return false;
        } else {
            if (impact->pitch) return false;
            if (impact->kind>=3 ?
                (impact->kind==4 ? (impact->frames!=4 || impact->base_frame || impact->yaw) :
                    ((impact->frames!=15 && impact->frames!=19) ||
                     (impact->frames==19 ? impact->base_frame!=30 :
                        (impact->base_frame!=0 && impact->base_frame!=15)) || impact->yaw<0 || impact->yaw>=360)) :
                (impact->frames || impact->base_frame || impact->yaw)) return false;
            if (impact->kind==5 && impact->frames!=15) return false;
        }
    }
    for (size_t i = 0; owner->q2 && i < FRONTEND_Q2_LASER_CAPACITY; ++i) {
        frontend_q2_laser *laser = &owner->lasers[i];
        if (!qa_source_save_bool(io, &laser->active) ||
            !qa_source_save_vec3(io, &laser->start) || !qa_vec_finite(laser->start) ||
            !qa_source_save_vec3(io, &laser->end) || !qa_vec_finite(laser->end) ||
            !qa_source_save_i64(io, &laser->end_milliseconds) ||
            laser->end_milliseconds < 0 || (laser->active && laser->end_milliseconds < 100) ||
            !qa_source_save_u32(io, &laser->color) || laser->color > UINT8_MAX) return false;
    }
    for (size_t i=0;owner->q2 && i<FRONTEND_Q2_DLIGHT_CAPACITY;++i) {
        frontend_q2_dlight *light=&owner->lights[i];
        if (!qa_source_save_bool(io,&light->active) || !qa_source_save_vec3(io,&light->origin) ||
            !qa_vec_finite(light->origin) || !qa_source_save_i64(io,&light->birth_milliseconds) ||
            !qa_source_save_i64(io,&light->end_milliseconds) ||
            !qa_source_save_actor(io,&light->actor) || !qa_source_save_u32(io,&light->source_entity) ||
            !qa_source_save_u8(io,&light->kind) || light->kind>2 || light->source_entity>UINT16_MAX ||
            (light->active && (!light->kind || (light->kind==1 ?
                (light->actor.registry || light->source_entity) : !light->actor.registry))) ||
            (light->active && (light->birth_milliseconds<0 ||
                light->birth_milliseconds>INT64_MAX-100 ||
                light->end_milliseconds!=light->birth_milliseconds+100)) ||
            (!light->active && (light->kind || light->actor.registry || light->source_entity ||
                light->birth_milliseconds || light->end_milliseconds ||
                light->origin.x || light->origin.y || light->origin.z))) return false;
    }
    if (!qa_source_save_count(io, &owner->steam_count, FRONTEND_STEAM_CAPACITY) ||
        (owner->q1 && owner->steam_count)) return false;
    for (size_t i = 0; i < owner->steam_count; ++i) {
        frontend_steam *steam = &owner->steam[i];
        if (!qa_source_save_bool(io,&steam->expired) || !frontend_save_q2_event(io, &steam->event) || steam->event.kind != QA_Q2_MAP_STEAM ||
            !qa_source_save_u64(io, &steam->end_ns) || !qa_source_save_u64(io, &steam->next_ns) ||
            !qa_source_save_u8(io,&steam->kind) || steam->kind>2 || !steam->event.slot ||
            (steam->kind && (steam->expired || steam->event.count || steam->event.style || steam->event.value ||
                steam->event.duration || steam->event.direction.x || steam->event.direction.y ||
                steam->event.direction.z || (steam->kind==2 && steam->event.slot!=21000) ||
                steam->end_ns<steam->next_ns || steam->end_ns-steam->next_ns!=
                    (steam->kind==1?UINT64_C(2100000000):UINT64_C(1000000000))))) return false;
    }
    return true;
}
static bool owner_identity(qa_source_save_io *io, qa_frontend *frontend,
    qa_actor_owner *provider, uint32_t *family, qa_actor_owner *world_source,
    uint64_t *map_identity, qa_actor_id *recipient)
{
    if (!frontend_save_provider(io, frontend->application, provider) || !*provider ||
        !qa_source_save_u32(io, family) || (*family != QA_GAME_Q1 && *family != QA_GAME_Q2) ||
        !frontend_save_provider(io, frontend->application, world_source) ||
        !qa_source_save_u64(io, map_identity) || !qa_source_save_actor(io, recipient)) return false;
    if (*family == QA_GAME_Q1) return !*world_source && !*map_identity && !recipient->registry;
    return recipient->registry && delivery_world_current(frontend, *world_source, *map_identity);
}
bool frontend_particle_checkpoint(qa_frontend *frontend, qa_buffer *out, qa_error *error)
{
    qa_source_save_io io = {0};
    if (!out || !qa_source_save_writer(&io, qa_application_session(frontend->application), error)) return false;
    bool present = frontend->particles != NULL;
    bool ok = particle_signature(&io) && qa_source_save_bool(&io, &present);
    if (present && ok) {
        frontend_particle_state *state = frontend->particles;
        uint64_t sample = state->sample_ns; size_t count = 0;
        for (frontend_particle_owner *owner = state->owners; owner; owner = owner->next) ++count;
        ok = qa_source_save_u64(&io, &sample) && client_clock_fields(frontend, &io, state) &&
            qa_source_save_count(&io, &count, SIZE_MAX);
        for (frontend_particle_owner *owner = state->owners; ok && owner; owner = owner->next) {
            frontend_particle_owner copy = *owner; uint32_t family = owner->family;
            ok = owner_identity(&io, frontend, &copy.provider, &family, &copy.world_source,
                &copy.map_identity, &copy.recipient) && particle_fields(&io, &copy);
        }
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    if (!ok && (!error || error->code == QA_OK)) frontend_fail(error, QA_ERROR_FORMAT, "invalid retained particle continuation");
    qa_source_save_dispose(&io); return ok;
}
bool frontend_particle_restore(qa_frontend *frontend, qa_bytes bytes, qa_error *error)
{
    if (frontend->particles) return frontend_fail(error, QA_ERROR_ARGUMENT, "particle restore requires an empty detached candidate");
    qa_source_save_io io = {0}; bool present = false; size_t count = 0;
    bool ok = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) &&
        particle_signature(&io) && qa_source_save_bool(&io, &present);
    if (ok && present) {
        frontend->particles = calloc(1, sizeof(*frontend->particles));
        if (!frontend->particles) ok = frontend_fail(error, QA_ERROR_MEMORY, "restoring particle owner");
        else ok = qa_source_save_u64(&io, &frontend->particles->sample_ns) &&
            client_clock_fields(frontend, &io, frontend->particles) && qa_source_save_count(&io, &count, bytes.size / 8);
        for (size_t i = 0; ok && i < count; ++i) {
            qa_actor_owner provider = 0, world_source = 0; uint32_t family = 0;
            uint64_t map_identity = 0; qa_actor_id recipient = {0};
            ok = owner_identity(&io, frontend, &provider, &family, &world_source, &map_identity, &recipient);
            for (frontend_particle_owner *p = frontend->particles->owners; ok && p; p = p->next)
                if (p->provider == provider && p->family == family && p->world_source == world_source &&
                    p->map_identity == map_identity && qa_actor_id_equal(p->recipient, recipient)) ok = false;
            frontend_particle_owner *owner = NULL;
            if (ok) ok = particle_owner(frontend, provider, (qa_game_family)family, world_source,
                map_identity, recipient, &owner, error) && particle_fields(&io, owner);
            for (size_t j = 0; ok && owner && owner->q2 && j < FRONTEND_Q2_IMPACT_CAPACITY; ++j) {
                qa_scene_model *model;
                if (owner->impacts[j].kind && owner->impacts[j].kind!=10)
                    ok = impact_model(frontend, owner, owner->impacts[j].kind, false, &model, error);
            }
        }
        if (ok) {
            frontend_particle_owner *ordered = NULL, *owner = frontend->particles->owners;
            while (owner) { frontend_particle_owner *next = owner->next; owner->next = ordered; ordered = owner; owner = next; }
            frontend->particles->owners = ordered;
        }
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    if (!ok) {
        frontend_particle_retire(frontend);
        if (!error || error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "invalid saved particle continuation");
    }
    qa_source_save_dispose(&io); return ok;
}
static uint32_t particle_random(frontend_particle_owner *owner)
{ return qa_builtin_random_integer(&owner->random); }
static float particle_unit(frontend_particle_owner *owner)
{ return qa_builtin_random_unit(&owner->random); }
static float particle_signed(frontend_particle_owner *owner)
{ return particle_unit(owner) * 2 - 1; }
static const char *impact_path(const frontend_particle_owner *owner, uint8_t kind)
{
    if (kind==11) return "models/objects/explode/tris.md2";
    if (kind==5 && owner->q2_edition==QA_Q2_RERELEASE) return "models/objects/r_explode/tris.md2";
    static const char *const paths[]={"models/objects/smoke/tris.md2","models/objects/flash/tris.md2",
        "models/objects/r_explode/tris.md2","sprites/s_bfg2.sp2","models/objects/r_explode2/tris.md2",
        "models/objects/explode/tris.md2","models/objects/explode/tris.md2","models/objects/explode/tris.md2",
        "models/objects/explode/tris.md2"};
    return kind>=1 && kind<=9 ? paths[kind-1] : NULL;
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
    qa_console_dialect dialect=qa_cvars_dialect(client.cvars);
    if (dialect!=QA_CONSOLE_Q2 && dialect!=QA_CONSOLE_Q2_RERELEASE) return true;
    const qa_cvar_view *row=qa_cvars_find(client.cvars,"cl_smooth_explosions");
    out->smooth=row && row->integer!=0;
    out->modern=row && !(row->flags&QA_CVAR_USER_CREATED);
    row=qa_cvars_find(client.cvars,"cl_disable_particles");
    if (row) out->disable_particles=(uint32_t)row->integer;
    row=qa_cvars_find(client.cvars,"cl_disable_explosions");
    if (row) out->disable_explosions=(uint32_t)row->integer;
    row=qa_cvars_find(client.cvars,"cl_dlight_hacks");
    if (row) out->dlight_hacks=(uint32_t)row->integer;
    return frontend_source_client_registry_current(frontend,&client) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 explosion control lost its physical CLIENT registry");
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
            media.owner != owner->provider || media.family != QA_SCENE_Q2) continue;
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
    for (int i = 0; i < count && owner->count < FRONTEND_PARTICLE_CAPACITY; ++i) {
        uint32_t selected = color + (fixed ? 0 : particle_random(owner) & 7);
        float distance = (float)(particle_random(owner) & (fixed ? 7 : 31));
        qa_vec3 origin, velocity;
        origin.x = event->origin.x + (float)(particle_random(owner) & 7) - 4 + distance * event->direction.x;
        velocity.x = particle_signed(owner) * 20;
        origin.y = event->origin.y + (float)(particle_random(owner) & 7) - 4 + distance * event->direction.y;
        velocity.y = particle_signed(owner) * 20;
        origin.z = event->origin.z + (float)(particle_random(owner) & 7) - 4 + distance * event->direction.z;
        velocity.z = particle_signed(owner) * 20;
        owner->q2[owner->count++] = (qa_scene_q2_particle_state){
            .spawn_milliseconds = (int64_t)(birth / UINT64_C(1000000)),
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
static void q1_particles(frontend_particle_owner *owner, const qa_builtin_event *event)
{
    double seconds = (double)event->time_ns / 1e9;
    for (int i = 0; i < event->count && owner->count < FRONTEND_PARTICLE_CAPACITY; ++i) {
        qa_scene_q1_particle_state *particle = &owner->q1[owner->count++];
        if (event->count == 1024) {
            float ramp = (float)(particle_random(owner) & 3);
            qa_vec3 origin, velocity;
            origin.x = event->origin.x + (float)(particle_random(owner) % 32) - 16;
            velocity.x = (float)(particle_random(owner) % 512) - 256;
            origin.y = event->origin.y + (float)(particle_random(owner) % 32) - 16;
            velocity.y = (float)(particle_random(owner) % 512) - 256;
            origin.z = event->origin.z + (float)(particle_random(owner) % 32) - 16;
            velocity.z = (float)(particle_random(owner) % 512) - 256;
            *particle = (qa_scene_q1_particle_state){.origin = origin, .velocity = velocity,
                .ramp = ramp, .color = 111, .die = seconds + 5,
                .kind = (i & 1) ? QA_Q1_PARTICLE_EXPLODE : QA_Q1_PARTICLE_EXPLODE2};
        } else {
            double die = seconds + .1 * (particle_random(owner) % 5);
            uint32_t color = ((uint32_t)event->code & ~7u) + (particle_random(owner) & 7);
            qa_vec3 jitter;
            jitter.x = (float)(particle_random(owner) & 15) - 8;
            jitter.y = (float)(particle_random(owner) & 15) - 8;
            jitter.z = (float)(particle_random(owner) & 15) - 8;
            *particle = (qa_scene_q1_particle_state){.origin = qa_vec_add(event->origin, jitter),
                .velocity = qa_vec_scale(event->direction, 15), .color = color, .die = die,
                .kind = QA_Q1_PARTICLE_SLOW_GRAVITY};
        }
    }
}
static bool steam_particles(frontend_particle_owner *owner, const qa_q2_map_event *event,
    uint64_t now, bool smoke)
{
    qa_vec3 direction = event->direction, initial = qa_v3(direction.z, -direction.x, direction.y);
    qa_vec3 right = qa_vec_normalize(qa_vec_sub(initial, qa_vec_scale(direction, qa_vec_dot(initial, direction))));
    qa_vec3 up = qa_vec_cross(right, direction);
    for (int i = 0; i < event->count; ++i) {
        if (owner->count == FRONTEND_PARTICLE_CAPACITY) return false;
        uint32_t color = (uint32_t)event->style + (particle_random(owner) & 7);
        qa_vec3 jitter;
        jitter.x = event->value * .1f * particle_signed(owner);
        jitter.y = event->value * .1f * particle_signed(owner);
        jitter.z = event->value * .1f * particle_signed(owner);
        qa_vec3 velocity = qa_vec_add(qa_vec_scale(direction, event->value), qa_vec_scale(right, particle_signed(owner) * event->value / 3));
        velocity = qa_vec_add(velocity, qa_vec_scale(up, particle_signed(owner) * event->value / 3));
        owner->q2[owner->count++] = (qa_scene_q2_particle_state){.spawn_milliseconds = (int64_t)(now / UINT64_C(1000000)),
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
    for (double distance = 0; distance < length && owner->count < FRONTEND_PARTICLE_CAPACITY; distance += 4) {
        if (particle_unit(owner) <= .3f) continue;
        float alpha_velocity = -1 / (3 + particle_unit(owner) * .5f);
        qa_vec3 jitter;
        jitter.x = particle_signed(owner) * 3; jitter.y = particle_signed(owner) * 3; jitter.z = particle_signed(owner) * 3;
        qa_vec3 origin = qa_vec_add(qa_vec_add(event->origin, qa_vec_scale(direction, (float)distance)), jitter);
        owner->q2[owner->count++] = (qa_scene_q2_particle_state){.spawn_milliseconds = (int64_t)(now / UINT64_C(1000000)),
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
        qa_vec3 seed = qa_v3(direction.z, -direction.x, direction.y);
        qa_vec3 right = qa_vec_normalize(qa_vec_sub(seed,
            qa_vec_scale(direction, qa_vec_dot(seed, direction))));
        qa_vec3 up = qa_vec_cross(right, direction);
        for (double distance = 0; distance < length; ++distance) {
            if (owner->count == FRONTEND_PARTICLE_CAPACITY) return true;
            float angle = (float)(distance * .1);
            qa_vec3 outward = qa_vec_add(qa_vec_scale(right, (float)cos(angle)),
                qa_vec_scale(up, (float)sin(angle)));
            qa_scene_q2_particle_state particle = {
                .spawn_milliseconds = (int64_t)(now / UINT64_C(1000000)),
                .origin = qa_vec_add(move, qa_vec_scale(outward, 3)),
                .velocity = qa_vec_scale(outward, 6), .alpha = 1};
            particle.alpha_velocity = -1 / (1 + particle_unit(owner) * .2f);
            particle.color = 0x74 + (particle_random(owner) & 7);
            owner->q2[owner->count++] = particle;
            move = qa_vec_add(move, direction);
        }
        move = event->origin;
        qa_vec3 step = qa_vec_scale(direction, .75f);
        for (double distance = 0; distance < length && owner->count < FRONTEND_PARTICLE_CAPACITY;
             distance += .75, move = qa_vec_add(move, step)) {
            qa_scene_q2_particle_state particle = {
                .spawn_milliseconds = (int64_t)(now / UINT64_C(1000000)), .alpha = 1};
            particle.alpha_velocity = -1 / (.6f + particle_unit(owner) * .2f);
            particle.color = particle_random(owner) & 15;
            particle.origin.x = move.x + particle_signed(owner) * 3;
            particle.velocity.x = particle_signed(owner) * 3;
            particle.origin.y = move.y + particle_signed(owner) * 3;
            particle.velocity.y = particle_signed(owner) * 3;
            particle.origin.z = move.z + particle_signed(owner) * 3;
            particle.velocity.z = particle_signed(owner) * 3;
            owner->q2[owner->count++] = particle;
        }
        return true;
    }
    qa_vec3 step = qa_vec_scale(direction, spacing);
    for (double distance = 0; distance < length && owner->count < FRONTEND_PARTICLE_CAPACITY;
         distance += spacing, move = qa_vec_add(move, step)) {
        qa_scene_q2_particle_state particle = {
            .spawn_milliseconds = (int64_t)(now / UINT64_C(1000000)),
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
        owner->q2[owner->count++] = particle;
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
    for (unsigned i=0;i<count && owner->count<FRONTEND_PARTICLE_CAPACITY;++i) {
        qa_scene_q2_particle_state particle={.spawn_milliseconds=(int64_t)(now/UINT64_C(1000000)),
            .alpha=1,.alpha_velocity=-10000};
        particle.color=colors[particle_random(owner)&3];
        particle.origin=qa_vec_add(sustain->event.origin,
            qa_vec_scale(q2_random_direction(owner),radius));
        owner->q2[owner->count++]=particle;
    }
}

static void q2_power_splash(frontend_particle_owner *owner,
    const qa_application_native_q2_entity_sample *entity,uint64_t now)
{
    qa_vec3 origin=qa_vec_add(entity->origin,
        qa_vec_scale(qa_vec_add(entity->solid_bounds.mins,entity->solid_bounds.maxs),.5f));
    for (unsigned i=0;i<256 && owner->count<FRONTEND_PARTICLE_CAPACITY;++i) {
        qa_scene_q2_particle_state particle={.spawn_milliseconds=(int64_t)(now/UINT64_C(1000000)),.alpha=1};
        particle.color=208+(particle_random(owner)&3);
        qa_vec3 direction=q2_random_direction(owner);
        particle.origin=qa_vec_add(origin,qa_vec_scale(direction,entity->solid_radius));
        particle.velocity=qa_vec_scale(direction,40);
        particle.alpha_velocity=-1/(.5f+particle_unit(owner)*.3f);
        owner->q2[owner->count++]=particle;
    }
}

static bool q2_entity_sample(qa_frontend *frontend,const frontend_particle_owner *owner,
    uint32_t physical,uint32_t slot,qa_actor_id actor,
    const qa_application_native_q2_entity_sample **out,bool *found,qa_error *error)
{
    *found=false;
    frontend_particle_state *state=frontend->particles;
    uint64_t sample,server; float back_lerp; bool physical_clock;
    if (!client_sample(frontend,&sample,&server,&back_lerp,&physical_clock,error)) return false;
    if (!physical_clock || state->clock_source!=owner->provider)
        return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Q2 entity effect requires its completed local Source dictionary");
    bool recipient=false;
    for (size_t i=0;i<state->source_client_count;++i) {
        const frontend_q2_client_sample *client=state->source_clients+i;
        if (client->physical_seat!=physical || !qa_actor_id_equal(client->client.actor,owner->recipient)) continue;
        uint32_t authored;
        qa_application_native_q2_presentation source;
        qa_application_native_q2_client actual;
        bool present;
        if (!frontend_seat_launch_id_read(frontend,physical,&authored) || authored!=client->client.seat ||
            !qa_application_native_q2_presentation_selected(frontend->application,&source,&present,error) || !present ||
            !qa_application_native_q2_presentation_local(frontend->application,&source,authored,&actual,&present,error) ||
            !present || actual.client_slot!=client->client.client_slot || !qa_actor_id_equal(actual.actor,client->client.actor))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 entity effect lost its captured physical client");
        recipient=true; break;
    }
    if (!recipient) return true;
    for (size_t i=0;i<state->source_entity_count;++i) {
        const qa_application_native_q2_entity_sample *entity=state->source_entities+i;
        if (entity->source_slot!=slot || !qa_actor_id_equal(entity->actor,actor)) continue;
        *out=entity; *found=true; break;
    }
    return true;
}

static void q2_berserk_particles(frontend_particle_owner *owner,
    qa_vec3 origin, qa_vec3 direction, uint64_t now)
{
    qa_vec3 seed=qa_v3(direction.z,-direction.x,direction.y);
    qa_vec3 right=qa_vec_normalize(qa_vec_sub(seed,qa_vec_scale(direction,qa_vec_dot(seed,direction))));
    qa_vec3 up=qa_vec_cross(right,direction);
    for (unsigned i=0;i<700 && owner->count<FRONTEND_PARTICLE_CAPACITY;++i) {
        qa_scene_q2_particle_state particle={.spawn_milliseconds=(int64_t)(now/UINT64_C(1000000)),
            .origin=origin,.alpha=1};
        particle.color=110+2*(particle_random(owner)&3);
        particle.velocity=qa_vec_scale(direction,particle_unit(owner)*192);
        particle.velocity=qa_vec_add(particle.velocity,qa_vec_scale(right,particle_signed(owner)*192));
        particle.velocity=qa_vec_add(particle.velocity,qa_vec_scale(up,particle_signed(owner)*192));
        particle.alpha_velocity=-1/(.5f+particle_unit(owner)*.3f);
        owner->q2[owner->count++]=particle;
    }
}

static void q2_burst_particles(frontend_particle_owner *owner, qa_vec3 origin,
    uint64_t now, uint8_t type, bool rerelease)
{
    int64_t birth = (int64_t)(now / UINT64_C(1000000));
    if (type == QA_Q2_TE_WIDOWSPLASH) {
        static const uint32_t colors[] = {16, 104, 168, 144};
        for (unsigned i = 0; i < 256 && owner->count < FRONTEND_PARTICLE_CAPACITY; ++i) {
            qa_scene_q2_particle_state particle = {.spawn_milliseconds = birth, .alpha = 1};
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
            owner->q2[owner->count++] = particle;
        }
        return;
    }
    if (type == QA_Q2_TE_TELEPORT_EFFECT || type == QA_Q2_TE_DBALL_GOAL) {
        for (int i = -16; i <= 16; i += 4)
            for (int j = -16; j <= 16; j += 4)
                for (int k = -16; k <= 32; k += 4) {
                    if (owner->count == FRONTEND_PARTICLE_CAPACITY) return;
                    qa_scene_q2_particle_state particle = {.spawn_milliseconds = birth,
                        .alpha = 1, .acceleration = {0, 0, -40}};
                    particle.color = 7 + (particle_random(owner) & 7);
                    particle.alpha_velocity = -1 / (.3f + (float)(particle_random(owner) & 7) * .02f);
                    particle.origin.x = origin.x + (float)i + (float)(particle_random(owner) & 3);
                    particle.origin.y = origin.y + (float)j + (float)(particle_random(owner) & 3);
                    particle.origin.z = origin.z + (float)k + (float)(particle_random(owner) & 3);
                    qa_vec3 direction = qa_vec_normalize(qa_v3((float)(j * 8), (float)(i * 8), (float)(k * 8)));
                    particle.velocity = qa_vec_scale(direction, 50 + (float)(particle_random(owner) & 63));
                    owner->q2[owner->count++] = particle;
                }
        return;
    }
    bool big = type == QA_Q2_TE_BOSSTPORT;
    for (int i = 0; i < (big ? 4096 : 256) && owner->count < FRONTEND_PARTICLE_CAPACITY; ++i) {
        qa_scene_q2_particle_state particle = {.spawn_milliseconds = birth, .alpha = 1};
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
        owner->q2[owner->count++] = particle;
    }
}
static void q2_blaster_particles(frontend_particle_owner *owner, const qa_builtin_event *event,
    uint64_t now, uint8_t type)
{
    uint32_t color=type==QA_Q2_TE_BLASTER2 ? 0xd0u : type==QA_Q2_TE_FLECHETTE ? 0x6fu :
        type==QA_Q2_TE_BLUEHYPERBLASTER_2 ? 0xb0u : 0xe0u;
    for (unsigned i=0;i<40 && owner->count<FRONTEND_PARTICLE_CAPACITY;++i) {
        qa_scene_q2_particle_state particle={.spawn_milliseconds=(int64_t)(now/UINT64_C(1000000)),
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
        owner->q2[owner->count++]=particle;
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
    for (unsigned i=0;i<128 && owner->count<FRONTEND_PARTICLE_CAPACITY;++i) {
        qa_scene_q2_particle_state particle={.spawn_milliseconds=milliseconds,
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
        owner->q2[owner->count++]=particle;
    }
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
    if (code<0 && !steam && !widow && !nuke && !wall && !smoke && !heat && !trail && !burst && !laser && !poly && !blaster && !hyper && !tracker && !flashlight && !berserk && !power) return true;
    if (!audience->captured || audience->source!=message->provider)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Original Q2 effect has no actual delivery receipt");
    const qa_q2_temp_field *position=temporary_field(temporary,QA_Q2_TEMP_POSITION1,QA_Q2_TEMP_VECTOR);
    const qa_q2_temp_field *direction=temporary_field(temporary,
        wall || trail || laser || temporary->type==QA_Q2_TE_BLUEHYPERBLASTER ?
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
            audience->source_frame.kind==QA_CLOCK_Q2_RERELEASE;
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
        uint64_t birth=physical && owner->provider==frontend->particles->clock_source ?
            sample:audience->source_time_ns;
        if (code>=0) {
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
            const qa_application_native_q2_entity_sample *entity;
            bool present;
            if (!q2_entity_sample(frontend,owner,seat,(uint32_t)flashlight_entity->value.integer,
                    flashlight_actor,&entity,&present,error)) return false;
            if (present) q2_power_splash(owner,entity,birth);
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
            float yaw=effect.direction.x ? (float)(atan2(effect.direction.y,effect.direction.x)*180/3.14159265358979323846) :
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
                    audience->source_frame.kind == QA_CLOCK_Q2_RERELEASE);
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
                audience->source_frame.kind == QA_CLOCK_Q2_RERELEASE);
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

bool frontend_particle_events(qa_frontend *frontend, qa_error *error)
{
    particle_clients_retire(frontend);
    uint64_t q2_sample, server;
    float back_lerp;
    bool physical;
    if (!client_sample(frontend, &q2_sample, &server, &back_lerp, &physical, error)) return false;
    if (!frontend_native_q2_messages(frontend,error)) return false;
    for (size_t i = 0; i < qa_application_event_count(frontend->application); ++i) {
        qa_builtin_event event;
        if (!qa_application_event_at(frontend->application, i, &event)) return frontend_fail(error, QA_ERROR_ARGUMENT, "particle event queue changed");
        if (event.kind != QA_BUILTIN_PARTICLES ||
            (event.family != QA_GAME_Q1 && event.family != QA_GAME_Q2) ||
            (event.family == QA_GAME_Q1 && event.count <= 0)) continue;
        frontend_particle_owner *owner;
        if (event.family == QA_GAME_Q1) {
            if (!particle_owner(frontend, event.provider, event.family, 0, 0,
                    (qa_actor_id){0}, &owner, error)) return false;
            q1_particles(owner, &event);
        } else {
            qa_application_q2_audience audience;
            if (!qa_application_event_q2_audience_at(frontend->application, i, &audience) ||
                !audience.captured || audience.source != event.provider)
                return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 particles have no captured source delivery");
            for (uint32_t seat = 0; seat < frontend->options.seats; ++seat) {
                qa_actor_id recipient;
                if (!frontend_seat_actor_read(frontend, seat, &recipient)) continue;
                bool received = false;
                for (size_t j = 0; j < audience.count; ++j)
                    received = received || qa_actor_id_equal(recipient, audience.recipients[j].actor);
                if (!received) continue;
                if (!particle_owner(frontend, event.provider, QA_GAME_Q2, audience.world_source,
                        audience.map_identity, recipient, &owner, error) ||
                    !q2_damage_particles(frontend, owner, &event, seat, audience.source_time_ns, NULL, error)) return false;
            }
        }
    }
    for (size_t i = 0; i < qa_application_q2_map_event_count(frontend->application); ++i) {
        qa_application_q2_map_event source;
        if (!qa_application_q2_map_event_at(frontend->application, i, &source)) return frontend_fail(error, QA_ERROR_ARGUMENT, "steam event queue changed");
        if (source.event.kind != QA_Q2_MAP_STEAM && source.event.kind != QA_Q2_MAP_FORCE_WALL) continue;
        qa_application_q2_audience audience;
        if (!qa_application_q2_map_event_audience_at(frontend->application, i, &audience) ||
            !audience.captured || audience.source != source.provider)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 map particles have no captured source delivery");
        for (uint32_t seat = 0; seat < frontend->options.seats; ++seat) {
            qa_actor_id recipient;
            if (!frontend_seat_actor_read(frontend, seat, &recipient)) continue;
            bool received = false;
            for (size_t j = 0; j < audience.count; ++j)
                received = received || qa_actor_id_equal(recipient, audience.recipients[j].actor);
            if (!received) continue;
            frontend_particle_owner *owner;
            if (!particle_owner(frontend, source.provider, QA_GAME_Q2, audience.world_source,
                    audience.map_identity, recipient, &owner, error)) return false;
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
        if (steam.expired || (!sample->negative && steam.end_ns<sample->time_ns)) continue;
        if (!sample->negative && steam.next_ns<=sample->time_ns) {
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
    if (!frontend->particles) return true;
    qa_actor_id recipient;
    if (!frontend_seat_actor_read(frontend,seat,&recipient)) return true;
    frontend_q2_controls controls;
    if (!q2_controls(frontend,seat,&controls,error)) return false;
    qa_scene_light pending[FRONTEND_Q2_IMPACT_CAPACITY+FRONTEND_Q2_DLIGHT_CAPACITY];
    for (frontend_particle_owner *owner=frontend->particles->owners;owner;owner=owner->next) {
        if (!owner->q2 || !qa_actor_id_equal(owner->recipient,recipient) ||
            !delivery_world_current(frontend,owner->world_source,owner->map_identity)) continue;
        frontend_q2_sample sample;
        if (!q2_owner_sample(frontend,owner,&sample,error)) return false;
        size_t count=0;
        for (size_t i=0;i<FRONTEND_Q2_IMPACT_CAPACITY;++i) {
            const frontend_q2_impact *impact=&owner->impacts[i];
            if (impact->kind<3 || impact->no_light) continue;
            double fraction=impact_fraction(impact,&sample);
            if (floor(fraction)>=(double)(impact_frames(impact)-1)) continue;
            float radius=(impact->light_radius ? impact->light_radius : impact->kind==11 ? 550.f : impact->kind>=6 ?
                (impact->kind==6 && owner->q2_edition==QA_Q2_RERELEASE ? 200.f : 150.f) : 350.f)*impact_alpha(owner,impact,&sample,fraction,controls.smooth);
            if (radius<=0) continue;
            pending[count++]=(qa_scene_light){.origin=impact->origin,
                .color=impact->kind==10 ? (qa_vec3){1,1,.3f} : impact->kind==4 || impact->kind==7 ? (qa_vec3){0,1,0} :
                    impact->kind==6 ? (qa_vec3){1,1,0} : impact->kind==8 || impact->kind==11 ? (qa_vec3){.19f,.41f,.75f} :
                    impact->kind==9 ? (qa_vec3){0,0,1} : (qa_vec3){1,.5f,.5f},
                .radius=radius,.scale=1,
                .additive=true,.family=QA_SCENE_Q2};
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
                .radius=radius,.scale=1,.additive=true,.family=QA_SCENE_Q2};
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
bool frontend_particle_draw(qa_frontend *frontend, uint32_t seat, const qa_scene_view *view, qa_error *error)
{
    if (!frontend || !view || seat >= frontend->options.seats)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Particle draw requires its actual physical seat");
    if (!frontend->particles) return true;
    frontend_q2_controls controls;
    if (!q2_controls(frontend,seat,&controls,error)) return false;
    uint64_t now = qa_session_elapsed(qa_application_session(frontend->application));
    double seconds = (double)now / 1e9;
    for (frontend_particle_owner *owner = frontend->particles->owners; owner; owner = owner->next) {
        frontend_q2_sample sample = {0};
        if (owner->q2) {
            qa_actor_id recipient;
            if (!frontend_seat_actor_read(frontend, seat, &recipient) ||
                !qa_actor_id_equal(recipient, owner->recipient) ||
                !delivery_world_current(frontend, owner->world_source, owner->map_identity)) continue;
            if (!q2_owner_sample(frontend, owner, &sample, error)) return false;
            q2_sustains_sample(owner,&sample);
            /* The Source allocates sustain particles before AddParticles
             * frees instant particles sampled by the preceding scene. */
            size_t retained=0;
            for (size_t i=0;i<owner->count;++i) {
                const qa_scene_q2_particle_state *particle=owner->q2+i;
                if (particle->alpha_velocity==-10000 && particle->alpha<=0) continue;
                owner->q2[retained++]=*particle;
            }
            owner->count=retained;
        }
        qa_bytes palette;
        qa_scene_family family = owner->family == QA_GAME_Q1 ? QA_SCENE_Q1 : QA_SCENE_Q2;
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
                .previous_origin = impact->origin, .family = QA_SCENE_Q2,
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
            if (!laser->active || laser->end_milliseconds < sample.milliseconds) continue;
            qa_scene_vec4 color = {palette.data[laser->color * 3] / 255.0f,
                palette.data[laser->color * 3 + 1] / 255.0f,
                palette.data[laser->color * 3 + 2] / 255.0f, .3f};
            if (!qa_scene_beam(&frontend->frame, view, laser->start, laser->end,
                    4, color, NULL, error)) return false;
        }
        for (size_t i = owner->count; i > 0; --i) {
            qa_vec3 origin; float alpha = 1; uint32_t index;
            if (owner->q1) {
                const qa_scene_q1_particle_state *particle = &owner->q1[i - 1];
                if (particle->die < seconds) continue;
                origin = particle->origin; index = particle->color & 255;
            } else {
                const qa_scene_q2_particle_state *particle = &owner->q2[i - 1];
                if (!qa_scene_q2_particle_sample_at(particle, sample.milliseconds, &origin, &alpha)) continue;
                index = particle->color & 255;
            }
            qa_scene_vec4 color = {palette.data[index * 3] / 255.0f, palette.data[index * 3 + 1] / 255.0f,
                palette.data[index * 3 + 2] / 255.0f, alpha};
            if (!qa_scene_indexed_particle(&frontend->frame, view, family, origin, 1, color, owner->particle_image, error)) return false;
            if (owner->q2 && owner->q2[i-1].alpha_velocity==-10000)
                owner->q2[i-1].alpha=0;
        }
    }
    return true;
}
bool frontend_particle_advance(qa_frontend *frontend, qa_error *error)
{
    particle_clients_retire(frontend);
    if (!frontend->particles) return true;
    frontend_particle_state *state = frontend->particles;
    for (frontend_particle_owner *owner = state->owners; owner; owner = owner->next)
        if (owner->q1 && (!qa_application_provider_gravity(frontend->application, owner->provider, &owner->gravity) ||
                !isfinite(owner->gravity)))
            return frontend_fail(error, QA_ERROR_NOT_FOUND, "particle provider has no authoritative gravity");
    uint64_t now = qa_session_elapsed(qa_application_session(frontend->application));
    double seconds = (double)now / 1e9;
    double elapsed = now >= state->sample_ns ? (double)(now - state->sample_ns) / 1e9 : 0;
    state->sample_ns = now;
    for (frontend_particle_owner *owner = state->owners; owner; owner = owner->next) {
        frontend_q2_sample sample = {0};
        if (owner->q2 && !q2_owner_sample(frontend, owner, &sample, error)) return false;
        for (size_t i = 0; owner->q2 && i < FRONTEND_Q2_IMPACT_CAPACITY; ++i) {
            frontend_q2_impact *impact = &owner->impacts[i];
            double source_frame = floor(impact_fraction(impact,&sample));
            if (impact->kind && source_frame >= (double)(impact_frames(impact)-1))
                *impact = (frontend_q2_impact){0};
        }
        size_t retained = 0;
        for (size_t i = 0; i < owner->count; ++i) {
            if (owner->q1) {
                qa_scene_q1_particle_state particle = owner->q1[i];
                if (particle.die < seconds) continue;
                qa_scene_q1_particle_advance(&particle, elapsed, owner->gravity);
                owner->q1[retained++] = particle;
            } else {
                qa_vec3 origin; float alpha;
                if (owner->q2[i].alpha_velocity!=-10000 &&
                    !qa_scene_q2_particle_sample_at(&owner->q2[i], sample.milliseconds, &origin, &alpha)) continue;
                owner->q2[retained++] = owner->q2[i];
            }
        }
        owner->count = retained;
    }
    return true;
}
