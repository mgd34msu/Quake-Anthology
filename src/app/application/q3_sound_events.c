#include "q3_sound_events.h"
#include "guest_q3_private.h"
#include "unified_events.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include "../../network/q3/event_detect.h"

#include <stdlib.h>

typedef struct application_q3_sound_cursor {
    qa_actor_id actor;
    int32_t previous_event;
    bool event_only_fired;
} application_q3_sound_cursor;

struct application_q3_sound_events {
    application_q3_sound_cursor entities[QA_Q3_SOURCE_ENTITIES];
};

bool application_q3_sound_events_prepare(application_provider *p, qa_error *error)
{
    if (p->product->family != QA_GAME_Q3 || p->q3_sound_events) return true;
    p->q3_sound_events = calloc(1, sizeof(*p->q3_sound_events));
    return p->q3_sound_events ||
        application_fail(error, QA_ERROR_MEMORY, "Cannot allocate Q3 sound observation state");
}

void application_q3_sound_events_destroy(application_provider *p)
{
    free(p->q3_sound_events);
    p->q3_sound_events = NULL;
}

static bool audible(application_provider *p, int32_t event, int32_t parameter, int32_t flags)
{
    event &= ~0x300;
    if (event >= Q3N_EV_USE_ITEM0 && event <= Q3N_EV_USE_ITEM14) {
        int32_t item = event - Q3N_EV_USE_ITEM0;
        return item != 1 && (p->product->campaign_id != QA_CAMPAIGN_MISSIONPACK ||
            (item != 3 && item != 4));
    }
    switch (event) {
    case Q3N_EV_FOOTSTEP: case Q3N_EV_FOOTSTEP_METAL: case Q3N_EV_FOOTSPLASH:
    case Q3N_EV_FOOTWADE: case Q3N_EV_SWIM:
    case Q3N_EV_FALL_SHORT: case Q3N_EV_FALL_MEDIUM: case Q3N_EV_FALL_FAR:
    case Q3N_EV_JUMP_PAD: case Q3N_EV_JUMP:
    case Q3N_EV_WATER_TOUCH: case Q3N_EV_WATER_LEAVE:
    case Q3N_EV_WATER_UNDER: case Q3N_EV_WATER_CLEAR:
    case Q3N_EV_ITEM_PICKUP: case Q3N_EV_GLOBAL_ITEM_PICKUP:
    case Q3N_EV_CHANGE_WEAPON: case Q3N_EV_FIRE_WEAPON:
    case Q3N_EV_ITEM_RESPAWN: case Q3N_EV_ITEM_POP:
    case Q3N_EV_TELEPORT_IN: case Q3N_EV_TELEPORT_OUT: case Q3N_EV_GRENADE_BOUNCE:
    case Q3N_EV_GENERAL_SOUND: case Q3N_EV_GLOBAL_SOUND: case Q3N_EV_TEAM_SOUND:
    case Q3N_EV_BULLET_WALL:
    case Q3N_EV_MISSILE_HIT: case Q3N_EV_MISSILE_MISS: case Q3N_EV_MISSILE_METAL:
    case Q3N_EV_PAIN: case Q3N_EV_DEATH1: case Q3N_EV_DEATH2: case Q3N_EV_DEATH3:
    case Q3N_EV_QUAD: case Q3N_EV_BATTLESUIT: case Q3N_EV_REGEN:
    case Q3N_EV_PROX_STICK: case Q3N_EV_PROX_TRIGGER: case Q3N_EV_KAMIKAZE:
    case Q3N_EV_OBELISK_PAIN: case Q3N_EV_INVUL_IMPACT: case Q3N_EV_JUICED:
    case Q3N_EV_TAUNT:
        return true;
    case Q3N_EV_GIB:
        return !(flags & 512);
    case Q3N_EV_RAIL:
        return parameter != 255;
    default:
        return false;
    }
}

static bool sound_emit(application_provider *p, qa_actor_id actor, qa_vec3 origin,
    int32_t channel, float volume, float attenuation, qa_actor_id recipient,
    uint64_t time, int32_t source_slot, bool has_slot, qa_error *error)
{
    qa_unified_simulation_payload event = {.kind = QA_UNIFIED_SIMULATION_SOUND,
        .value.sound = {.resource = 0, .actor = actor, .origin = origin, .channel = channel,
            .volume = volume, .attenuation = attenuation}};
    return application_unified_event_emit(p->application, p->owner, NULL, &event,
        (qa_actor_id){0}, recipient, time, source_slot, has_slot, false, error);
}

static bool source_actor(application_provider *p, uint32_t slot, qa_actor_id *actor,
    qa_error *error)
{
    *actor = (qa_actor_id){0};
    if (p->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_source_binding binding;
        if (!qa_q3_source_binding_read(p->state.q3, slot, &binding, error)) return false;
        if (binding.in_use) *actor = binding.actor;
        return true;
    }
    struct application_q3_guest *guest = q3g_engine(p);
    return qa_q3_host_actor(guest->game->host, slot, false, actor, error);
}

static bool source_client(application_provider *p, uint32_t slot,
    qa_actor_id *actor, qa_error *error)
{
    *actor = (qa_actor_id){0};
    if (p->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_native_client client;
        if (!qa_q3_client_slot_read(p->state.q3, slot, &client, error)) return false;
        if (client.rule.connected == QA_Q3_CLIENT_DISCONNECTED) return true;
        return source_actor(p, slot, actor, error);
    }
    q3g_client *client = &q3g_engine(p)->clients[slot];
    if (client->allocated && client->connected && !client->pending_retirement)
        *actor = client->actor;
    return true;
}

static bool source_entity(application_provider *p, uint32_t slot, qa_q3_entity *entity,
    qa_actor_id *actor, qa_vec3 *origin, uint32_t *flags, int32_t *single_client,
    bool *linked, qa_error *error)
{
    *actor = (qa_actor_id){0};
    if (p->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_source_binding binding;
        if (!qa_q3_source_binding_read(p->state.q3, slot, &binding, error)) return false;
        if (!binding.in_use) { *linked = false; return true; }
        qa_q3_wire_visibility visibility;
        if (!qa_q3_wire_entity_read(p->state.q3, slot, entity, &visibility, error)) return false;
        *linked = visibility.linked;
        *flags = visibility.server_flags;
        *single_client = visibility.single_client;
        *actor = binding.actor;
        if (!*linked || (*flags & QA_Q3_SVF_NOCLIENT)) return true;
        if (!qa_q3_source_current_origin_read(p->state.q3, *actor, origin, error)) return false;
        if (entity->solid == 0xffffff) {
            qa_body_link_state body;
            if (qa_world_link_state(p->application->world, *actor, &body))
                *origin = qa_vec_scale(qa_vec_add(body.absolute_bounds.mins,
                    body.absolute_bounds.maxs), 0.5f);
        }
        return true;
    }
    qa_qvm_entity_shared shared;
    qa_q3_host *host = q3g_engine(p)->game->host;
    if (!qa_q3_host_source_entity(host, slot, entity, &shared, error)) return false;
    *linked = shared.linked;
    *flags = (uint32_t)shared.server_flags;
    *single_client = shared.single_client;
    if (!*linked || (*flags & QA_Q3_SVF_NOCLIENT)) return true;
    *origin = shared.inline_model ? qa_vec_scale(qa_vec_add(shared.absolute_bounds.mins,
        shared.absolute_bounds.maxs), 0.5f) : shared.origin;
    return source_actor(p, slot, actor, error);
}

bool application_q3_sound_events_frame(application_provider *p,
    const qa_source_frame *frame, qa_error *error)
{
    struct application_q3_sound_events *events = p->q3_sound_events;
    if (!events) return true;
    uint32_t count, clients;
    if (p->kind == APPLICATION_PROVIDER_Q3) {
        if (!qa_q3_source_entity_count(p->state.q3, &count, error) ||
            !qa_q3_source_max_clients(p->state.q3, &clients, error)) return false;
    } else {
        qa_q3_host_game_data data;
        struct application_q3_guest *guest = q3g_engine(p);
        if (!guest || !guest->game || !qa_q3_host_game_data_read(guest->game->host, &data)) return true;
        count = data.entity_count;
        clients = data.client_count;
    }
    for (uint32_t slot = 0; slot < count && slot < QA_Q3_SOURCE_WORLD; ++slot) {
        qa_q3_entity state;
        qa_actor_id actor;
        qa_vec3 origin;
        uint32_t flags;
        int32_t single_client;
        bool linked;
        if (!source_entity(p, slot, &state, &actor, &origin, &flags,
            &single_client, &linked, error)) return false;
        application_q3_sound_cursor *cursor = events->entities + slot;
        if (!linked || !actor.registry || (flags & QA_Q3_SVF_NOCLIENT)) {
            *cursor = (application_q3_sound_cursor){0};
            continue;
        }
        if (!qa_actor_id_equal(cursor->actor, actor))
            *cursor = (application_q3_sound_cursor){.actor = actor};
        if (!qa_q3_entity_event_detect(&state, &cursor->previous_event,
            &cursor->event_only_fired) || !audible(p, state.event, state.eventParm, state.eFlags)) continue;
        if (state.number >= 0 && state.number < (int32_t)count &&
            state.number < (int32_t)QA_Q3_SOURCE_WORLD &&
            state.number != (int32_t)slot) {
            qa_actor_id source;
            if (!source_actor(p, (uint32_t)state.number, &source, error)) return false;
            if (source.registry) actor = source;
        }
        uint32_t restricted = QA_Q3_SVF_SINGLECLIENT | QA_Q3_SVF_NOTSINGLECLIENT | QA_Q3_SVF_CLIENTMASK;
        if (!(flags & restricted)) {
            if (!sound_emit(p, actor, origin, 0, 1, 1, (qa_actor_id){0},
                frame->time_ns, state.number, true, error)) return false;
            continue;
        }
        for (uint32_t client = 0; client < clients; ++client) {
            if (!qa_q3_entity_audience_contains(flags, single_client, client)) continue;
            qa_actor_id recipient;
            if (!source_client(p, client, &recipient, error)) return false;
            if (recipient.registry && !sound_emit(p, actor, origin, 0, 1, 1,
                recipient, frame->time_ns, state.number, true, error)) return false;
        }
    }
    return true;
}

bool application_q3_sound_builtin(qa_application *app, const qa_builtin_event *event,
    qa_error *error)
{
    if (event->family != QA_GAME_Q3 || !event->actor.registry ||
        (event->kind != QA_BUILTIN_SOUND && event->kind != QA_BUILTIN_ANIMATION)) return true;
    application_provider *p = application_provider_lookup(app, event->provider,
        APPLICATION_PROVIDER_LIVE);
    if (!p) return true;
    bool action = event->kind == QA_BUILTIN_ANIMATION;
    if (action ? !audible(p, event->code, event->count, 0) : event->volume <= 0) return true;
    uint32_t slot;
    bool physical = p->kind == APPLICATION_PROVIDER_Q3
        ? qa_q3_source_actor_slot(p->state.q3, event->actor, &slot, NULL)
        : q3g_engine(p) && q3g_engine(p)->game &&
            qa_q3_host_actor_slot(q3g_engine(p)->game->host, event->actor, &slot, NULL);
    return physical || sound_emit(p, event->actor, event->origin, action ? 0 : event->channel,
        action ? 1 : event->volume, action ? 1 : event->attenuation,
        (qa_actor_id){0}, event->time_ns, 0, false, error);
}
