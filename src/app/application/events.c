#include "internal.h"

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

static bool valid_event(const qa_application *application,
                        const qa_builtin_event *event, qa_error *error)
{
    if (event == NULL || (unsigned)event->kind > QA_BUILTIN_EFFECT ||
        (unsigned)event->family > QA_GAME_Q3 ||
        (event->argument_count != 0 && event->arguments == NULL) ||
        event->argument_count > SIZE_MAX / sizeof(*event->arguments) ||
        !valid_string(application, event->resource) ||
        !valid_string(application, event->text) ||
        !qa_vec_finite(event->origin) || !qa_vec_finite(event->end) ||
        !qa_vec_finite(event->direction) || !isfinite(event->volume) ||
        !isfinite(event->attenuation) || !isfinite(event->value))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "gameplay emitted an invalid event");
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

static bool reserve_event(qa_application *application, qa_error *error)
{
    if (application->event_count < application->event_capacity)
        return true;
    if (application->event_capacity > SIZE_MAX / 2 ||
        application->event_capacity * 2 >
            SIZE_MAX / sizeof(*application->events))
        return application_fail(error, QA_ERROR_MEMORY,
                                "gameplay event queue capacity is exhausted");
    size_t capacity = application->event_capacity == 0
                          ? 64
                          : application->event_capacity * 2;
    application_event_record *events =
        realloc(application->events, capacity * sizeof(*events));
    if (events == NULL)
        return application_fail(error, QA_ERROR_MEMORY,
                                "cannot retain gameplay event");
    application->events = events;
    application->event_capacity = capacity;
    return true;
}

static bool reserve_q2_map_event(qa_application *application,
                                 qa_error *error)
{
    if (application->q2_map_event_count <
        application->q2_map_event_capacity)
        return true;
    if (application->q2_map_event_capacity > SIZE_MAX / 2 ||
        application->q2_map_event_capacity * 2 >
            SIZE_MAX / sizeof(*application->q2_map_events))
        return application_fail(error, QA_ERROR_MEMORY,
                                "Q2 map event queue capacity is exhausted");
    size_t capacity = application->q2_map_event_capacity == 0
                          ? 32
                          : application->q2_map_event_capacity * 2;
    qa_application_q2_map_event *events =
        realloc(application->q2_map_events, capacity * sizeof(*events));
    if (events == NULL)
        return application_fail(error, QA_ERROR_MEMORY,
                                "cannot retain Q2 map event");
    application->q2_map_events = events;
    application->q2_map_event_capacity = capacity;
    return true;
}

static bool reserve_q3_map_event(qa_application *application,
                                 qa_error *error)
{
    if (application->q3_map_event_count <
        application->q3_map_event_capacity)
        return true;
    if (application->q3_map_event_capacity > SIZE_MAX / 2 ||
        application->q3_map_event_capacity * 2 >
            SIZE_MAX / sizeof(*application->q3_map_events))
        return application_fail(error, QA_ERROR_MEMORY,
                                "Q3 map event queue capacity is exhausted");
    size_t capacity = application->q3_map_event_capacity == 0
                          ? 32
                          : application->q3_map_event_capacity * 2;
    qa_application_q3_map_event *events =
        realloc(application->q3_map_events, capacity * sizeof(*events));
    if (events == NULL)
        return application_fail(error, QA_ERROR_MEMORY,
                                "cannot retain Q3 map event");
    application->q3_map_events = events;
    application->q3_map_event_capacity = capacity;
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
                         event->argument_count, error) ||
        !reserve_q2_map_event(application, error))
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
    qa_application_q2_map_event *record =
        &application->q2_map_events[application->q2_map_event_count++];
    *record = (qa_application_q2_map_event){
        .provider = provider->owner,
        .time_ns = qa_session_elapsed(application->session),
        .event = *event,
    };
    record->event.arguments = arguments;
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
    if (!reserve_q3_map_event(application, error))
        return false;
    application->q3_map_events[application->q3_map_event_count++] =
        (qa_application_q3_map_event){
            .provider = provider->owner,
            .time_ns = qa_session_elapsed(application->session),
            .event = *event,
        };
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

bool application_emit(void *opaque, const qa_builtin_event *event,
                      qa_error *error)
{
    qa_application *application = opaque;
    if (application == NULL || application->destroy_requested ||
        application->session == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "gameplay event has no live application owner");
    if (!valid_event(application, event, error) ||
        !reserve_event(application, error))
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

    application_event_record *record =
        &application->events[application->event_count++];
    record->event = *event;
    record->event.arguments = arguments;
    return true;
}

static void *event_storage(void *storage, size_t count, size_t *capacity,
                            size_t width, qa_error *error)
{
    if (count < *capacity)
        return storage;
    size_t next = *capacity ? *capacity : 32;
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
        sizeof(*storage), error);
    if (!storage) return false;
    application->q2_player_events = storage;
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
    storage[application->q2_player_event_count++] = (qa_application_q2_player_event){
        .provider = provider->owner, .time_ns = qa_session_elapsed(application->session),
        .event = copied};
    return true;
}

bool application_emit_protocol(application_provider *provider,
                                const qa_application_protocol_event *event, qa_error *error)
{
    qa_application *application = provider ? provider->application : NULL;
    if (!application || !application->session || application->destroy_requested || !event ||
        (event->payload.size && !event->payload.data) || !qa_vec_finite(event->origin) ||
        (event->reference_count && !event->references) ||
        event->reference_count > SIZE_MAX / sizeof(*event->references))
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid source protocol event");
    for (size_t i = 0; i < event->reference_count; ++i)
        if (event->payload.size < 2 || event->references[i].offset > event->payload.size - 2)
            return application_fail(error, QA_ERROR_ARGUMENT, "source protocol reference exceeds payload");
    qa_application_protocol_event *storage = event_storage(application->protocol_events,
        application->protocol_event_count, &application->protocol_event_capacity,
        sizeof(*storage), error);
    if (!storage) return false;
    application->protocol_events = storage;
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
    qa_application_protocol_event copied = *event;
    copied.provider = provider->owner;
    copied.dialect = provider->launch->selection.clock.kind;
    copied.time_ns = qa_session_elapsed(application->session);
    copied.payload = (qa_bytes){payload, event->payload.size};
    copied.references = references;
    storage[application->protocol_event_count++] = copied;
    return true;
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

bool qa_application_protocol_event_at(const qa_application *application, size_t index,
                                      qa_application_protocol_event *out)
{
    if (!application || !out || index >= application->protocol_event_count) return false;
    *out = application->protocol_events[index];
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
    *out = application->q2_map_events[index];
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

bool qa_application_clear_events(qa_application *application, qa_error *error)
{
    if (application == NULL || application->operation != APPLICATION_IDLE ||
        application->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "event consumption requires an idle application");
    application->event_count = 0;
    application->q2_map_event_count = 0;
    application->q3_map_event_count = 0;
    application->q2_player_event_count = 0;
    application->protocol_event_count = 0;
    qa_arena_reset(&application->event_arena);
    return true;
}
