#include "internal.h"
#include "map_travel_private.h"
#include "network_q1_signon.h"
#include "native_q1_wire.h"
#include "equipment_events.h"
#include "equipment_runtime.h"
#include "native_q2_delivery.h"
#include "native_q1_composition.h"
#include "unified_q1_events.h"
#include "unified_q2_events.h"
#include "unified_q2_native_events.h"
#include "guest_q3_weapons_services.h"
#include "guest_native_q2_private.h"
#include "network_q2_private.h"
#include "qa/application_q3_round.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool valid_string(const qa_application *application, qa_string_id id)
{
    return id == QA_STRING_NONE ||
           qa_strings_text(qa_session_strings(application->session), id).data != NULL;
}

static bool valid_q1_source_event(qa_application *app,
    const qa_builtin_event *event, qa_error *error)
{
    if (event->family != QA_GAME_Q1 || !event->provider || event->argument_count ||
        !qa_actor_id_equal(event->other, (qa_actor_id){0}) || event->resource)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 source event has invalid source payload");
    if (!application_native_q1_composition_current(app, event->provider,
            QA_MODE_THREEWAVE, error)) return false;
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    uint64_t time_ns;
    double seconds;
    if (!qa_q1_game_clock_read(source->state.q1, &time_ns, &seconds) ||
        event->time_ns != time_ns)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 source event differs from its genuine source clock");
    if (event->kind == QA_BUILTIN_SOURCE_LOG) {
        bool observer;
        if (!event->text)
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Q1 source log has no source action");
        return application_native_q1_composition_player_current(app, event->provider,
            QA_MODE_THREEWAVE, event->actor, &observer, error);
    }
    if (event->kind == QA_BUILTIN_SOURCE_PROMPT || event->kind == QA_BUILTIN_CLEAR_PROMPT) {
        bool observer;
        if ((event->kind == QA_BUILTIN_SOURCE_PROMPT && !event->text) ||
            (event->kind == QA_BUILTIN_CLEAR_PROMPT && (event->text || event->prompt_choice_count)))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q1 source prompt has invalid title or choices");
        return application_native_q1_composition_player_current(app, event->provider,
            QA_MODE_THREEWAVE, event->actor, &observer, error);
    }
    return (qa_actor_id_equal(event->actor, (qa_actor_id){0}) && !event->text &&
            isfinite(event->ctf_capture.total)) ||
        application_fail(error, QA_ERROR_ARGUMENT,
            "CTF capture requires its genuine team total without an addressed actor");
}

static bool valid_event(qa_application *application,
                        const qa_builtin_event *event, qa_error *error)
{
    if (event == NULL || (unsigned)event->kind > QA_BUILTIN_Q2_ENTITY_EVENT ||
        (unsigned)event->family > QA_GAME_Q3 ||
        (event->argument_count != 0 && event->arguments == NULL) ||
        event->argument_count > SIZE_MAX / sizeof(*event->arguments) ||
        (event->prompt_choice_count && !event->prompt_choices) ||
        event->prompt_choice_count > SIZE_MAX / sizeof(*event->prompt_choices) ||
        !valid_string(application, event->resource) ||
        !valid_string(application, event->text) ||
        !qa_vec_finite(event->origin) || !qa_vec_finite(event->end) ||
        !qa_vec_finite(event->direction) || !isfinite(event->volume) ||
        !isfinite(event->attenuation) || !isfinite(event->value))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "gameplay emitted an invalid event");
    if ((unsigned)event->q2_multicast.kind>QA_BUILTIN_Q2_MULTICAST_PHS_LINE ||
        !qa_vec_finite(event->q2_multicast.origin) ||
        (event->q2_multicast.kind!=QA_BUILTIN_Q2_MULTICAST_NONE &&
         (event->family!=QA_GAME_Q2 || !event->provider)))
        return application_fail(error,QA_ERROR_ARGUMENT,"Gameplay event has invalid Source multicast input");
    if (event->kind == QA_BUILTIN_LOG &&
        (event->family != QA_GAME_Q3 || !event->provider ||
         event->text == QA_STRING_NONE || event->argument_count ||
         !qa_actor_id_equal(event->actor, (qa_actor_id){0}) ||
         !qa_actor_id_equal(event->other, (qa_actor_id){0})))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 source log requires provider-owned text");
    if (event->has_muzzle_pose &&
        (event->family != QA_GAME_Q2 || event->kind != QA_BUILTIN_MUZZLE ||
         !event->provider || !qa_actors_get(qa_session_actors(application->session), event->actor) ||
         !qa_vec_finite(event->muzzle_angles) || !isfinite(event->muzzle_scale) || event->muzzle_scale <= 0))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 muzzle pose has no genuine finite Source receipt");
    if (!event->has_muzzle_pose &&
        (event->muzzle_scale != 0.0f || event->muzzle_angles.x != 0.0f || event->muzzle_angles.y != 0.0f || event->muzzle_angles.z != 0.0f))
        return application_fail(error, QA_ERROR_ARGUMENT, "Absent muzzle pose has retained Source fields");
    if (event->kind == QA_BUILTIN_CTF_STATUS &&
        (event->family != QA_GAME_Q1 || !event->provider ||
         !qa_actors_get(qa_session_actors(application->session), event->actor) ||
         event->argument_count || !isfinite(event->ctf_status.red) ||
         !isfinite(event->ctf_status.blue) || !isfinite(event->ctf_status.flags) ||
         !isfinite(event->ctf_status.rune_items)))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "CTF status requires its actual source and addressed actor");
    if ((event->kind == QA_BUILTIN_SOURCE_LOG || event->kind == QA_BUILTIN_CTF_CAPTURE ||
         event->kind == QA_BUILTIN_SOURCE_PROMPT || event->kind == QA_BUILTIN_CLEAR_PROMPT) &&
        !valid_q1_source_event(application, event, error)) return false;
    if (event->kind != QA_BUILTIN_SOURCE_PROMPT && event->prompt_choice_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Non-prompt event has prompt choices");
    for (size_t i = 0; i < event->prompt_choice_count; ++i)
        if (!event->prompt_choices[i].label || !valid_string(application, event->prompt_choices[i].label))
            return application_fail(error, QA_ERROR_ARGUMENT, "Source prompt has an invalid label alias");
    if (event->kind == QA_BUILTIN_Q1_POWERUP &&
        (event->family != QA_GAME_Q1 || !event->provider || !event->actor.registry ||
         event->other.registry || event->text || event->resource || event->argument_count ||
         event->q1_powerup.power >= QA_Q1_POWER_COUNT || !isfinite(event->q1_powerup.expires)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 powerup requires its actual typed source expiry");
    if (event->family == QA_GAME_Q2 && event->kind == QA_BUILTIN_ITEM && event->code == 0) {
        if (!event->item || !valid_string(application, event->item))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 pickup requires its authored canonical item");
    } else if (event->item)
        return application_fail(error, QA_ERROR_ARGUMENT, "Non-pickup event has a Q2 pickup item");
    if (event->kind == QA_BUILTIN_Q2_PLAYER_ANIMATION || event->kind == QA_BUILTIN_Q2_ENTITY_EVENT) {
        if (event->family != QA_GAME_Q2 || !event->provider ||
            !qa_actors_get(qa_session_actors(application->session), event->actor) ||
            (event->kind == QA_BUILTIN_Q2_PLAYER_ANIMATION && (event->code < 0 || event->code > 2)))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 animation requires its actual source and live actor");
        bool found = false;
        for (const application_provider *source = application->live_providers; source; source = source->next_live)
            if (source->owner == event->provider && source->kind == APPLICATION_PROVIDER_Q2 && !source->close_pending)
                found = true;
        if (!found) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 animation lost its actual native source");
    }
    for (size_t index = 0; index < event->argument_count; ++index) {
        const qa_builtin_message_arg *argument = &event->arguments[index];
        if ((argument->kind == QA_BUILTIN_MESSAGE_STRING &&
             !valid_string(application, argument->value.text)) ||
            (argument->kind == QA_BUILTIN_MESSAGE_NUMBER &&
             !isfinite(argument->value.number)) ||
            (argument->kind != QA_BUILTIN_MESSAGE_STRING &&
             argument->kind != QA_BUILTIN_MESSAGE_NUMBER))
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "gameplay emitted an invalid message argument");
    }
    return true;
}

static qa_game_family progress_family(const qa_launch_instance *instance)
{
    switch (instance->selection.clock.kind) {
    case QA_CLOCK_NETQUAKE:
    case QA_CLOCK_QUAKEWORLD:
        return QA_GAME_Q1;
    case QA_CLOCK_Q2_CLASSIC:
    case QA_CLOCK_Q2_RERELEASE:
        return QA_GAME_Q2;
    case QA_CLOCK_Q3:
        return QA_GAME_Q3;
    }
    return QA_GAME_Q3;
}

static void *event_storage(void *storage, size_t count, size_t *capacity,
                            size_t width, size_t initial, qa_error *error)
{
    if (count < *capacity)
        return storage;
    size_t next = *capacity ? *capacity : initial;
    if (*capacity && next > SIZE_MAX / 2) {
        application_fail(error, QA_ERROR_MEMORY, "application event capacity is exhausted");
        return NULL;
    }
    if (*capacity) next *= 2;
    if (next > SIZE_MAX / width) {
        application_fail(error, QA_ERROR_MEMORY, "application event extent overflows");
        return NULL;
    }
    void *grown = realloc(storage, next * width);
    if (!grown) {
        application_fail(error, QA_ERROR_MEMORY, "cannot retain application event");
        return NULL;
    }
    *capacity = next;
    return grown;
}

static bool reserve_event(qa_application *application, qa_error *error)
{
    application_event_record *storage = event_storage(application->events,
        application->event_count, &application->event_capacity, sizeof(*storage), 64, error);
    if (!storage) return false;
    application->events = storage;
    return true;
}

static bool reserve_q2_map_event(qa_application *application, qa_error *error)
{
    application_q2_map_event_record *storage = event_storage(application->q2_map_events,
        application->q2_map_event_count, &application->q2_map_event_capacity,
        sizeof(*storage), 32, error);
    if (!storage) return false;
    application->q2_map_events = storage;
    return true;
}

static bool reserve_q3_map_event(qa_application *application, qa_error *error)
{
    qa_application_q3_map_event *storage = event_storage(application->q3_map_events,
        application->q3_map_event_count, &application->q3_map_event_capacity,
        sizeof(*storage), 32, error);
    if (!storage) return false;
    application->q3_map_events = storage;
    return true;
}

static bool valid_arguments(const qa_application *application,
                            const qa_builtin_message_arg *arguments,
                            size_t count, qa_error *error)
{
    if ((count != 0 && arguments == NULL) ||
        count > SIZE_MAX / sizeof(*arguments))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "map event has invalid message arguments");
    for (size_t index = 0; index < count; ++index) {
        const qa_builtin_message_arg *argument = &arguments[index];
        if ((argument->kind == QA_BUILTIN_MESSAGE_STRING &&
             !valid_string(application, argument->value.text)) ||
            (argument->kind == QA_BUILTIN_MESSAGE_NUMBER &&
             !isfinite(argument->value.number)) ||
            (argument->kind != QA_BUILTIN_MESSAGE_STRING &&
             argument->kind != QA_BUILTIN_MESSAGE_NUMBER))
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "map event has an invalid message argument");
    }
    return true;
}

bool application_emit_q2_map(application_provider *provider,
                             const qa_q2_map_event *event, qa_error *error)
{
    qa_application *application =
        provider == NULL ? NULL : provider->application;
    if (application == NULL || event == NULL ||
        application->destroy_requested || application->session == NULL ||
        (unsigned)event->kind > QA_Q2_MAP_HELP_COMPUTER ||
        !valid_string(application, event->text) ||
        !valid_string(application, event->resource) ||
        !qa_vec_finite(event->origin) || !qa_vec_finite(event->direction) ||
        !qa_vec_finite(event->color) || !qa_vec_finite(event->fog.color) ||
        !qa_vec_finite(event->fog.start_color) ||
        !qa_vec_finite(event->fog.end_color) || !isfinite(event->value) ||
        !isfinite(event->duration) || !isfinite(event->radius) ||
        !isfinite(event->alpha) || !isfinite(event->intensity) ||
        !isfinite(event->fade_start) || !isfinite(event->fade_end) ||
        !isfinite(event->cone_cosine) || !isfinite(event->fog.density) ||
        !isfinite(event->fog.sky_factor) ||
        !isfinite(event->fog.start_distance) ||
        !isfinite(event->fog.end_distance) ||
        !isfinite(event->fog.falloff) ||
        !isfinite(event->fog.height_density))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "gameplay emitted an invalid Q2 map event");
    if (!valid_arguments(application, event->arguments,
                         event->argument_count, error))
        return false;
    if ((event->level_count && !event->levels) || event->level_count > QA_Q2_CAMPAIGN_LEVEL_LIMIT ||
        (event->kind != QA_Q2_MAP_END_UNIT && (event->level_count || event->button_time_ns)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 map event has an invalid unit report");
    for (size_t i = 0; i < event->level_count; ++i)
        if (!event->levels[i].map || !valid_string(application, event->levels[i].map) ||
            !valid_string(application, event->levels[i].name) || !isfinite(event->levels[i].time_seconds))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 unit report lost its actual level or clock");
    if (event->kind == QA_Q2_MAP_WORLD_TEXT &&
        !application_unified_world_text_emit(application, provider->owner, event, error)) return false;
    qa_application_q2_audience audience={0},retained={0};
    if (event->kind==QA_Q2_MAP_STEAM || event->kind==QA_Q2_MAP_FORCE_WALL) {
        qa_vec3 multicast_origin=event->origin;
        if (event->kind==QA_Q2_MAP_FORCE_WALL &&
            (provider->kind!=APPLICATION_PROVIDER_Q2 ||
             !qa_q2_force_wall_multicast_origin(provider->state.q2,event->actor,&multicast_origin)))
            return application_fail(error,QA_ERROR_ARGUMENT,"Forcewall delivery lost its genuine spawn midpoint");
        if (!application_native_q2_delivery_capture(provider,
                &(qa_builtin_q2_multicast){QA_BUILTIN_Q2_MULTICAST_PVS,multicast_origin},
                (qa_vec3){0},&audience,error)) return false;
    }
    bool ready=reserve_q2_map_event(application,error) &&
        application_event_journal_reserve(application,error) &&
        application_native_q2_delivery_retain(application,&audience,&retained,error);
    application_native_q2_delivery_dispose(&audience);
    if (!ready) return false;
    qa_builtin_message_arg *arguments = NULL;
    if (event->argument_count != 0) {
        size_t bytes = event->argument_count * sizeof(*event->arguments);
        arguments = qa_arena_alloc(&application->event_arena, bytes,
                                   _Alignof(qa_builtin_message_arg), error);
        if (arguments == NULL)
            return false;
        memcpy(arguments, event->arguments, bytes);
    }
    qa_q2_campaign_level *levels = NULL;
    if (event->level_count) {
        size_t bytes = event->level_count * sizeof(*levels);
        levels = qa_arena_alloc(&application->event_arena, bytes, _Alignof(qa_q2_campaign_level), error);
        if (!levels) return false;
        memcpy(levels, event->levels, bytes);
    }
    if (!application_unified_q2_native_map(provider, event, &retained, error) ||
        (event->kind == QA_Q2_MAP_AUTOSAVE && !application_map_autosave_request(application, error))) return false;
    application_q2_map_event_record *record =
        &application->q2_map_events[application->q2_map_event_count];
    *record = (application_q2_map_event_record){.audience=retained,.source={
        .provider = provider->owner,
        .time_ns = qa_session_elapsed(application->session),
        .event = *event,
    }};
    record->source.event.arguments = arguments;
    record->source.event.levels = levels;
    application_event_journal_append(application, APPLICATION_EVENT_Q2_MAP,
        application->q2_map_event_count++, provider->owner);
    return true;
}

bool application_emit_q3_map(application_provider *provider,
                             const qa_q3_map_event *event, qa_error *error)
{
    qa_application *application =
        provider == NULL ? NULL : provider->application;
    if (application == NULL || event == NULL ||
        application->destroy_requested || application->session == NULL ||
        (unsigned)event->kind > QA_Q3_MAP_AREA_PORTAL ||
        !valid_string(application, event->name) ||
        !valid_string(application, event->text) ||
        !qa_vec_finite(event->origin) || !qa_vec_finite(event->angles) ||
        !qa_vec_finite(event->direction) ||
        !qa_vec_finite(event->destination) || !isfinite(event->value))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "gameplay emitted an invalid Q3 map event");
    if (!reserve_q3_map_event(application, error) ||
        !application_event_journal_reserve(application, error))
        return false;
    application->q3_map_events[application->q3_map_event_count] =
        (qa_application_q3_map_event){
            .provider = provider->owner,
            .time_ns = qa_session_elapsed(application->session),
            .event = *event,
        };
    application_event_journal_append(application, APPLICATION_EVENT_Q3_MAP,
        application->q3_map_event_count++, provider->owner);
    return true;
}

static bool record_progress(application_provider *provider,
                            qa_actor_id participant_actor,
                            qa_progress_kind kind, qa_string_id value,
                            qa_error *error)
{
    qa_application *application =
        provider == NULL ? NULL : provider->application;
    if (application == NULL || application->session == NULL || value == 0)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "progress event has no application identity");
    if (application->progress == NULL)
        return true;
    qa_bytes value_text = qa_strings_text(
        qa_session_strings(application->session), value);
    if (value_text.data == NULL || value_text.size == 0)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "progress event has empty content");
    const qa_launch_snapshot *snapshot =
        qa_configuration_current(application->configuration);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    const qa_launch_instance *instance =
        snapshot == NULL || provider->launch == NULL
            ? NULL
            : qa_launch_snapshot_find(
                  snapshot, provider->launch->selection.instance);
    const qa_product *product = qa_catalog_product(
        qa_launch_snapshot_catalog(snapshot),
        instance == NULL ? QA_PRODUCT_NONE : instance->selection.product);
    if (choices == NULL || instance == NULL || product == NULL ||
        product->identity == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "progress event lost its launch identity");
    const char *prefix = kind == QA_PROGRESS_ACHIEVEMENT
                             ? "achievement:"
                             : "level:";
    size_t prefix_length = strlen(prefix);
    size_t identity_length = strlen(product->identity);
    if (identity_length > SIZE_MAX - prefix_length - 1 ||
        value_text.size >
            SIZE_MAX - prefix_length - identity_length - 1)
        return application_fail(error, QA_ERROR_MEMORY,
                                "progress event identity is too large");
    size_t event_length =
        prefix_length + identity_length + 1 + value_text.size;
    uint8_t *event_text = malloc(event_length);
    if (event_text == NULL)
        return application_fail(error, QA_ERROR_MEMORY,
                                "cannot retain progress event identity");
    memcpy(event_text, prefix, prefix_length);
    memcpy(event_text + prefix_length, product->identity, identity_length);
    event_text[prefix_length + identity_length] = ':';
    memcpy(event_text + prefix_length + identity_length + 1,
           value_text.data, value_text.size);

    bool ok = true;
    for (size_t index = 0; index < choices->seat_count; ++index) {
        const qa_launch_seat *seat = &choices->seats[index];
        if (!seat->local ||
            (participant_actor.registry != 0 &&
             !qa_actor_id_equal(participant_actor, seat->actor)))
            continue;
        char participant[32];
        int length = snprintf(participant, sizeof(participant),
                              "local-seat:%" PRIu32, seat->id);
        if (length < 0 || (size_t)length >= sizeof(participant)) {
            ok = application_fail(error, QA_ERROR_MEMORY,
                                  "local seat progress identity is too large");
            break;
        }
        qa_progress_event progress = {
            .kind = kind,
            .source = progress_family(instance),
            .participant = {(const uint8_t *)participant, (size_t)length},
            .event = {event_text, event_length},
        };
        if (kind == QA_PROGRESS_ACHIEVEMENT)
            progress.value.award = value_text;
        else
            progress.value.map = value_text;
        bool inserted;
        if (!qa_player_progress_record(application->progress, &progress,
                                       &inserted, error)) {
            ok = false;
            break;
        }
    }
    free(event_text);
    return ok;
}

bool application_record_achievement(application_provider *provider,
                                    qa_actor_id participant,
                                    qa_string_id award, qa_error *error)
{
    return record_progress(provider, participant, QA_PROGRESS_ACHIEVEMENT,
                           award, error);
}

bool application_record_level(application_provider *provider,
                              qa_string_id map, qa_error *error)
{
    return record_progress(provider, (qa_actor_id){0},
                           QA_PROGRESS_LEVEL_COMPLETED, map, error);
}

static bool emit_event(qa_application *application, const qa_builtin_event *event,
    const qa_application_q2_audience *audience, qa_error *error)
{
    if (!application_q3_weapons_services_q2_muzzle(application, event, error) ||
        !application_native_q1_wire_emit(application, event, error) ||
        !application_unified_q1_event(application, event, error) ||
        !application_unified_q2_native_builtin(application, event, audience, error) ||
        !reserve_event(application, error) ||
        !application_event_journal_reserve(application, error))
        return false;

    qa_builtin_message_arg *arguments = NULL;
    if (event->argument_count != 0) {
        size_t bytes = event->argument_count * sizeof(*event->arguments);
        arguments = qa_arena_alloc(&application->event_arena, bytes,
                                   _Alignof(qa_builtin_message_arg), error);
        if (arguments == NULL)
            return false;
        memcpy(arguments, event->arguments, bytes);
    }

    qa_builtin_prompt_choice *choices = NULL;
    if (event->prompt_choice_count) {
        size_t bytes = event->prompt_choice_count * sizeof(*choices);
        choices = qa_arena_alloc(&application->event_arena, bytes, _Alignof(qa_builtin_prompt_choice), error);
        if (!choices) return false;
        memcpy(choices, event->prompt_choices, bytes);
    }
    application_event_record *record =
        &application->events[application->event_count];
    if (!application_native_q2_delivery_retain(application,audience,&record->q2_audience,error)) return false;
    record->event = *event;
    record->event.arguments = arguments;
    record->event.prompt_choices = choices;
    application_event_journal_append(application, APPLICATION_EVENT_BUILTIN,
        application->event_count++, event->provider);
    return true;
}

bool application_emit(void *opaque, const qa_builtin_event *event, qa_error *error)
{
    qa_application *application=opaque;
    qa_application_q2_audience audience={0};
    if (!application || application->destroy_requested || !application->session)
        return application_fail(error,QA_ERROR_ARGUMENT,"Gameplay event has no live application owner");
    if (!valid_event(application,event,error)) return false;
    if (event->q2_multicast.kind!=QA_BUILTIN_Q2_MULTICAST_NONE) {
        application_provider *source=NULL;
        for (application_provider *provider=application->live_providers;provider;provider=provider->next_live)
            if (provider->owner==event->provider && provider->constructed && provider->attached && !provider->close_pending) {
                source=provider; break;
            }
        if (!application_native_q2_delivery_capture(source,&event->q2_multicast,event->end,
                &audience,error)) return false;
    }
    bool ok=emit_event(application,event,&audience,error);
    application_native_q2_delivery_dispose(&audience);
    return ok;
}

static bool event_text(qa_application *application, const char *source,
                        const char **out, qa_error *error)
{
    if (!source) {
        *out = NULL;
        return true;
    }
    size_t length = strlen(source);
    if (length == SIZE_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "application event text overflows");
    char *copy = qa_arena_alloc(&application->event_arena, length + 1, 1, error);
    if (!copy) return false;
    memcpy(copy, source, length + 1);
    *out = copy;
    return true;
}

bool application_emit_q2_player(application_provider *provider,
                                 const qa_q2_player_event *event, qa_error *error)
{
    qa_application *application = provider ? provider->application : NULL;
    if (!application || !application->session || application->destroy_requested ||
        !event || (unsigned)event->kind > QA_Q2_PLAYER_ALPHA ||
        !qa_vec_finite(event->origin) || !qa_vec_finite(event->direction) ||
        !isfinite(event->damage) || !isfinite(event->alpha) ||
        (event->kind == QA_Q2_PLAYER_SCOREBOARD && event->count && !event->scores) ||
        (event->kind == QA_Q2_PLAYER_INVENTORY && event->count && !event->inventory) ||
        event->count > SIZE_MAX / sizeof(qa_q2_score_row) ||
        event->count > SIZE_MAX / sizeof(qa_inventory_entry))
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q2 player event");
    qa_application_q2_player_event *storage = event_storage(application->q2_player_events,
        application->q2_player_event_count, &application->q2_player_event_capacity,
        sizeof(*storage), 32, error);
    if (!storage) return false;
    application->q2_player_events = storage;
    if (!application_event_journal_reserve(application,error)) return false;
    const qa_application_network_q2_recipient_view *recipients = NULL;
    size_t recipient_count = 0;
    if (event->kind == QA_Q2_PLAYER_PRINT && provider->q2_recipient_binding &&
        !application_network_q2_print_recipients(provider, event->actor, &application->event_arena,
            &recipients, &recipient_count, error)) return false;
    if (!application_unified_q2_native_player(provider, event, error)) return false;
    qa_q2_player_event copied = *event;
    if (!event_text(application, event->text, &copied.text, error) ||
        !event_text(application, event->skin, &copied.skin, error))
        return false;
    copied.scores = NULL;
    copied.inventory = NULL;
    if (event->scores && event->count) {
        qa_q2_score_row *scores = qa_arena_alloc(&application->event_arena,
            event->count * sizeof(*scores), _Alignof(qa_q2_score_row), error);
        if (!scores) return false;
        memcpy(scores, event->scores, event->count * sizeof(*scores));
        for (size_t i = 0; i < event->count; ++i)
            if (!event_text(application, event->scores[i].name, &scores[i].name, error))
                return false;
        copied.scores = scores;
    }
    if (event->inventory && event->count) {
        qa_inventory_entry *inventory = qa_arena_alloc(&application->event_arena,
            event->count * sizeof(*inventory), _Alignof(qa_inventory_entry), error);
        if (!inventory) return false;
        memcpy(inventory, event->inventory, event->count * sizeof(*inventory));
        copied.inventory = inventory;
    }
    storage[application->q2_player_event_count] = (qa_application_q2_player_event){
        .provider = provider->owner, .time_ns = qa_session_elapsed(application->session),
        .event = copied, .recipients = recipients, .recipient_count = recipient_count};
    application_event_journal_append(application, APPLICATION_EVENT_Q2_PLAYER,
        application->q2_player_event_count++, provider->owner);
    return true;
}

static bool emit_protocol(application_provider *provider,
    const qa_application_protocol_event *event,
    const qa_application_q2_protocol_delivery *delivery, qa_error *error)
{
    qa_application *application = provider ? provider->application : NULL;
    if (!application || !application->session || application->destroy_requested || !event ||
        (event->payload.size && !event->payload.data) || !qa_vec_finite(event->origin) ||
        (event->reference_count && !event->references) ||
        event->reference_count > SIZE_MAX / sizeof(*event->references) ||
        (event->resource_count && !event->resources) ||
        event->resource_count > SIZE_MAX / sizeof(*event->resources))
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid source protocol event");
    for (size_t i = 0; i < event->reference_count; ++i)
        if (event->payload.size < 2 || event->references[i].offset > event->payload.size - 2)
            return application_fail(error, QA_ERROR_ARGUMENT, "source protocol reference exceeds payload");
    for (size_t i = 0; i < event->resource_count; ++i) {
        const qa_application_protocol_resource_reference *resource = event->resources + i;
        if ((unsigned)resource->kind > QA_NATIVE_HOST_IMAGE || !resource->name ||
            resource->record_ordinal >= event->payload.size || resource->resource_key[QA_APPLICATION_RESOURCE_KEY_CAPACITY - 1] ||
            (!resource->resource_key[0] && resource->resource_custody))
            return application_fail(error, QA_ERROR_ARGUMENT, "Source protocol resource lost its immutable precache receipt");
        if (resource->resource_key[0]) {
            const qa_resource *held; const qa_vfs *view; const qa_vfs_acquisition *opening;
            if (!application_unified_event_resource_receipt_read(application, resource->resource_key,
                resource->resource_custody, &held, &view, &opening, error)) return false;
        }
    }
    application_protocol_record *storage = event_storage(application->protocol_events,
        application->protocol_event_count, &application->protocol_event_capacity,
        sizeof(*storage), 32, error);
    if (!storage) return false;
    application->protocol_events = storage;
    if (!application_event_journal_reserve(application,error)) return false;
    uint8_t *payload = event->payload.size ? qa_arena_alloc(&application->event_arena,
        event->payload.size, 1, error) : NULL;
    if (event->payload.size && !payload) return false;
    if (event->payload.size) memcpy(payload, event->payload.data, event->payload.size);
    qa_application_protocol_reference *references = event->reference_count ?
        qa_arena_alloc(&application->event_arena, event->reference_count * sizeof(*references),
                         _Alignof(qa_application_protocol_reference), error) : NULL;
    if (event->reference_count && !references) return false;
    if (event->reference_count)
        memcpy(references, event->references, event->reference_count * sizeof(*references));
    qa_application_protocol_resource_reference *resources = event->resource_count ?
        qa_arena_alloc(&application->event_arena, event->resource_count * sizeof(*resources),
            _Alignof(qa_application_protocol_resource_reference), error) : NULL;
    if (event->resource_count && !resources) return false;
    for (size_t i = 0; i < event->resource_count; ++i) {
        resources[i] = event->resources[i];
        size_t length = strlen(resources[i].name);
        if (length == SIZE_MAX) return application_fail(error, QA_ERROR_MEMORY, "Source resource spelling extent overflows");
        char *name = qa_arena_alloc(&application->event_arena, length + 1, 1, error);
        if (!name) return false;
        memcpy(name, resources[i].name, length + 1); resources[i].name = name;
    }
    qa_application_protocol_event copied = *event;
    copied.provider = provider->owner;
    copied.dialect = provider->launch->selection.clock.kind;
    if (provider->kind != APPLICATION_PROVIDER_Q1) {
        qa_clock_state clock;
        if (qa_session_clock(application->session, provider->owner, &clock)) copied.time_ns = clock.frame.time_ns;
        else {
            const struct application_native_q2 *engine=provider->kind==APPLICATION_PROVIDER_NATIVE?
                provider->state.native.q2_engine:NULL;
            bool entering=provider->product && provider->product->family==QA_GAME_Q2 &&
                !provider->constructed && engine && engine->provider==provider &&
                (engine->profile==QA_NATIVE_Q2_GAME_API3 || engine->profile==QA_NATIVE_Q2_GAME_API2023) &&
                engine->prepared && engine->calls && engine->host_constructing &&
                (!delivery || !delivery->audience.captured);
            for (size_t i=0;entering && i<sizeof(engine->clients)/sizeof(*engine->clients);++i)
                if (engine->clients[i].connected) entering=false;
            if (!entering)
                return application_fail(error, QA_ERROR_ARGUMENT, "Source protocol lost its genuine emission clock");
        }
    }
    copied.payload = (qa_bytes){payload, event->payload.size};
    copied.references = references;
    copied.resources = resources;
    if (copied.signon &&
        !application_q1_signon_retain(provider, &copied, error))
        return false;
    application_protocol_record record = {.event = copied};
    if (delivery) {
        record.q2 = *delivery;
        if (!application_native_q2_delivery_retain(application, &delivery->audience,
                &record.q2.audience, error)) return false;
        if (delivery->audience.captured) record.event.time_ns = delivery->audience.source_time_ns;
    }
    if (!application_unified_q2_protocol_event(provider, &record.event,
            delivery ? &record.q2 : NULL, error)) return false;
    storage[application->protocol_event_count] = record;
    application_event_journal_append(application, APPLICATION_EVENT_PROTOCOL,
        application->protocol_event_count++, provider->owner);
    return true;
}

bool application_emit_protocol(application_provider *provider,
    const qa_application_protocol_event *event, qa_error *error)
{ return emit_protocol(provider, event, NULL, error); }

bool application_emit_q2_protocol(application_provider *provider,
    const qa_application_protocol_event *event,
    const qa_application_q2_protocol_delivery *delivery, qa_error *error)
{
    if (!provider || !delivery || !delivery->original ||
        (delivery->profile != QA_NATIVE_Q2_GAME_API3 && delivery->profile != QA_NATIVE_Q2_GAME_API2023) ||
        (delivery->audience.captured && delivery->audience.source != provider->owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 protocol lost its actual source receipt");
    return emit_protocol(provider, event, delivery, error);
}

size_t qa_application_q2_player_event_count(const qa_application *application)
{
    return application ? application->q2_player_event_count : 0;
}

bool qa_application_q2_player_event_at(const qa_application *application, size_t index,
                                       qa_application_q2_player_event *out)
{
    if (!application || !out || index >= application->q2_player_event_count) return false;
    *out = application->q2_player_events[index];
    return true;
}

size_t qa_application_protocol_event_count(const qa_application *application)
{
    return application ? application->protocol_event_count : 0;
}

uint64_t qa_application_protocol_events_generation(const qa_application *application)
{
    return application ? application->protocol_events_generation : 0;
}

bool qa_application_protocol_event_at(const qa_application *application, size_t index,
                                      qa_application_protocol_event *out)
{
    if (!application || !out || index >= application->protocol_event_count) return false;
    *out = application->protocol_events[index].event;
    return true;
}

bool qa_application_protocol_q2_delivery_at(const qa_application *application,
    size_t index, qa_application_q2_protocol_delivery *out)
{
    if (!application || !out || index >= application->protocol_event_count) return false;
    *out = application->protocol_events[index].q2;
    return true;
}

size_t qa_application_event_count(const qa_application *application)
{
    return application == NULL ? 0 : application->event_count;
}

bool qa_application_event_at(const qa_application *application, size_t index,
                             qa_builtin_event *out)
{
    if (application == NULL || out == NULL ||
        index >= application->event_count)
        return false;
    *out = application->events[index].event;
    return true;
}

size_t qa_application_q2_map_event_count(const qa_application *application)
{
    return application == NULL ? 0 : application->q2_map_event_count;
}

bool qa_application_q2_map_event_at(const qa_application *application,
                                    size_t index,
                                    qa_application_q2_map_event *out)
{
    if (application == NULL || out == NULL ||
        index >= application->q2_map_event_count)
        return false;
    *out = application->q2_map_events[index].source;
    return true;
}

bool qa_application_event_q2_audience_at(const qa_application *app,size_t index,
    qa_application_q2_audience *out)
{
    if (!app || !out || index>=app->event_count) return false;
    *out=app->events[index].q2_audience;
    return true;
}

bool qa_application_q2_map_event_audience_at(const qa_application *app,size_t index,
    qa_application_q2_audience *out)
{
    if (!app || !out || index>=app->q2_map_event_count) return false;
    *out=app->q2_map_events[index].audience;
    return true;
}

size_t qa_application_q3_map_event_count(const qa_application *application)
{
    return application == NULL ? 0 : application->q3_map_event_count;
}

bool qa_application_q3_map_event_at(const qa_application *application,
                                    size_t index,
                                    qa_application_q3_map_event *out)
{
    if (application == NULL || out == NULL ||
        index >= application->q3_map_event_count)
        return false;
    *out = application->q3_map_events[index];
    return true;
}

static bool clear_events(qa_application *application, qa_error *error)
{
    if (application->protocol_events_generation == UINT64_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "protocol event generation is exhausted");
    if ((application->equipment && !qa_equipment_idle(application->equipment)) ||
        !application_equipment_runtime_idle(application->equipment_runtime))
        return application_fail(error, QA_ERROR_ARGUMENT, "gear event consumption retains a source operation");
    application_equipment_events *gear = application_equipment_runtime_events(application->equipment_runtime);
    if (!application_equipment_events_clear_ready(gear, error)) return false;
    ++application->protocol_events_generation;
    application->event_count = 0;
    application->q2_map_event_count = 0;
    application->q3_map_event_count = 0;
    application->q2_player_event_count = 0;
    application->protocol_event_count = 0;
    application->event_journal_count = 0;
    application_unified_events_clear(application);
    qa_arena_reset(&application->event_arena);
    application_equipment_events_clear(gear);
    return true;
}

bool qa_application_clear_events(qa_application *application, qa_error *error)
{
    if (application == NULL || application->operation != APPLICATION_IDLE ||
        application->destroy_requested || application->finalizing ||
        application->q3_round_active || application->frame_preparing ||
        application->publication_started)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "event consumption requires an idle application");
    return clear_events(application, error);
}

bool qa_application_q3_round_clear_events(qa_application *application,
    qa_actor_owner source, qa_error *error)
{
    if (!application || !application->q3_round_active || !application->frame_preparing)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 round event consumption requires its retained driver cut");
    return qa_application_q3_round_callback_ready(application, source, error) &&
        clear_events(application, error);
}
