#include "unified_q1_events.h"
#include "unified_events.h"
#include "native_q1_wire.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q1_source_obituary.h"

#include <stdlib.h>
#include <string.h>

static bool precache_source(application_provider *source, qa_error *error)
{
    return (source && source->application && source->kind == APPLICATION_PROVIDER_Q1 &&
        source->constructed && !source->close_pending && source->state.q1 && source->launch &&
        source->launch->content) || application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 precache lost its actual source content owner");
}

bool application_unified_q1_precache_reset(void *context, qa_error *error)
{
    application_provider *source = context;
    return precache_source(source, error) &&
        application_unified_event_registration_clear(source->application, source->owner, error);
}

bool application_unified_q1_sound_precache(void *context, const char *path, qa_error *error)
{
    application_provider *source = context;
    if (!path || !*path || !precache_source(source, error)) return false;
    char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY];
    bool found;
    if (!application_unified_event_resource_lookup(source->application, source->owner,
        path, id, &found, error)) return false;
    if (found) return true;
    size_t length = strlen(path);
    if (length > SIZE_MAX - 7)
        return application_fail(error, QA_ERROR_MEMORY, "Q1 sound precache path exceeds its extent");
    char *full = malloc(length + 7);
    if (!full) return application_fail(error, QA_ERROR_MEMORY, "Retaining a Q1 sound precache path");
    memcpy(full, "sound/", 6);
    memcpy(full + 6, path, length + 1);
    qa_resource *resource = NULL;
    bool okay = qa_vfs_acquire(source->launch->content, full, &resource, NULL, error);
    free(full);
    if (okay) okay = precache_source(source, error) &&
        application_unified_event_resource_register(source->application, source->owner,
            path, resource, id, error);
    qa_resource_release(resource);
    return okay;
}


bool application_unified_q1_event(qa_application *app, const qa_builtin_event *event,
    qa_error *error)
{
    if (!event || event->family != QA_GAME_Q1) return true;
    bool reached = false, linked = false;
    qa_actor_id simulation_recipient = {0};
    switch (event->kind) {
    case QA_BUILTIN_SOUND: case QA_BUILTIN_STOP_SOUND:
    case QA_BUILTIN_MESSAGE: case QA_BUILTIN_CENTERPRINT:
    case QA_BUILTIN_PARTICLES: case QA_BUILTIN_LIGHT:
    case QA_BUILTIN_EXPLOSION: case QA_BUILTIN_TELEPORT:
    case QA_BUILTIN_MUZZLE: case QA_BUILTIN_ACHIEVEMENT:
    case QA_BUILTIN_CTF_STATUS: case QA_BUILTIN_SOURCE_LOG:
    case QA_BUILTIN_CTF_CAPTURE: case QA_BUILTIN_SOURCE_PROMPT:
    case QA_BUILTIN_CLEAR_PROMPT: case QA_BUILTIN_Q1_POWERUP:
        reached = true; break;
    case QA_BUILTIN_BEAM: reached = event->code >= 1 && event->code <= 4; break;
    case QA_BUILTIN_IMPACT:
        reached = event->code == 1 || event->code == 2 || event->code == 3 ||
            event->code == 4 || event->code == 7 || event->code == 8 || event->code == 10; break;
    case QA_BUILTIN_ITEM: reached = event->other.registry != 0; break;
    case QA_BUILTIN_DEATH: reached = event->count != 0; break;
    case QA_BUILTIN_ANIMATION: {
        application_provider *p = app->live_providers;
        while (p && p->owner != event->provider) p = p->next_live;
        reached = p && p->kind == APPLICATION_PROVIDER_Q1 &&
            (event->flags & UINT32_C(0x7fffffff)) < QA_Q1_WEAPON_COUNT;
        break;
    }
    case QA_BUILTIN_EFFECT: {
        const char *name = qa_strings_cstr(qa_session_strings(app->session), event->resource);
        reached = (event->flags & UINT32_C(0x80000000)) != 0 ||
            (name && (!strcmp(name, "colored-explosion") || !strcmp(name, "developer-message") ||
                !strcmp(name, "music") || !strcmp(name, "cutscene") || !strcmp(name, "sell-screen")));
        if (!reached && !event->actor.registry && event->resource &&
            !(event->flags & UINT32_C(0x80000000))) {
            application_provider *p = app->live_providers;
            while (p && p->owner != event->provider) p = p->next_live;
            uint32_t index;
            reached = p && p->kind == APPLICATION_PROVIDER_Q1 &&
                qa_q1_wire_emission_index(p->state.q1, true, event->resource, &index);
        }
        break;
    }
    default: break;
    }
    if (!reached) return true;
    qa_unified_presentation_payload presentation = {.kind = QA_UNIFIED_PRESENTATION_BUILTIN};
    if (!application_unified_builtin_read(app, event, &presentation.value.builtin, error)) return false;
    qa_unified_simulation_payload simulation = {0};
    const qa_unified_simulation_payload *sim = NULL;
    char identity[QA_APPLICATION_RESOURCE_KEY_CAPACITY];
    bool okay = true;
    if (event->kind == QA_BUILTIN_SOUND) {
        bool found;
        const char *path = presentation.value.builtin.resource;
        okay = application_unified_event_resource_lookup(app, event->provider, path, identity, &found, error);
        if (okay && found) {
            bool ambient = (event->flags & 1u) != 0;
            simulation.kind = QA_UNIFIED_SIMULATION_SOUND;
            simulation.value.sound = (qa_unified_sound_event){.resource = identity,
                .actor = ambient ? (qa_actor_id){0} : event->actor, .origin = event->origin,
                .channel = ambient ? 0 : event->channel, .volume = event->volume,
                .attenuation = event->attenuation};
            sim = &simulation;
        }
    } else if (event->kind == QA_BUILTIN_MESSAGE || event->kind == QA_BUILTIN_CENTERPRINT) {
        bool center = event->kind == QA_BUILTIN_CENTERPRINT || !(event->flags & 2u);
        simulation.kind = QA_UNIFIED_SIMULATION_MESSAGE;
        simulation.value.message = (qa_unified_message_event){
            .kind = center ? QA_UNIFIED_MESSAGE_CENTER_PRINT : QA_UNIFIED_MESSAGE_PRINT,
            .text = presentation.value.builtin.text,
            .level = event->flags & QA_Q1_SOURCE_MESSAGE_LITERAL ? event->code : 2};
        sim = &simulation; linked = true; simulation_recipient = event->actor;
    }
    int32_t slot = 0; bool has_slot = false;
    const qa_actor_record *record = event->actor.registry ?
        qa_actors_get(qa_session_actors(app->session), event->actor) : NULL;
    if (record && record->has_source && record->owner == event->provider && record->source_slot <= INT32_MAX) {
        slot = (int32_t)record->source_slot; has_slot = true;
    }
    if (okay) okay = application_unified_event_emit(app, event->provider, &presentation, sim,
        (qa_actor_id){0}, simulation_recipient, event->time_ns, slot, has_slot, linked, error);
    application_unified_builtin_read_dispose(&presentation.value.builtin);
    return okay;
}
