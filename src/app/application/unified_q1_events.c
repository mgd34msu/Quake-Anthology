#include "unified_q1_events.h"
#include "unified_events.h"
#include "unified_output_json.h"
#include "native_q1_wire.h"
#include "qa/game_q1_bots.h"
#include "qa/json.h"

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

static bool text_id(application_unified_json *json, qa_application *app,
    qa_string_id id, qa_error *error)
{
    qa_bytes text = qa_strings_text(qa_session_strings(app->session), id);
    qa_buffer quoted = {0};
    bool okay = qa_json_quote(text, &quoted, error) &&
        application_unified_json_append(json, (qa_bytes){quoted.data, quoted.size}, error);
    qa_buffer_free(&quoted);
    return okay;
}

static bool actor(application_unified_json *json, qa_actor_id id, qa_error *error)
{
    return id.registry ? application_unified_json_actor(json, id, error) :
        application_unified_json_text(json, "null", error);
}

static bool field_text(application_unified_json *json, qa_application *app,
    const char *key, qa_string_id value, qa_error *error)
{
    return application_unified_json_text(json, key, error) && text_id(json, app, value, error);
}

static bool field_number(application_unified_json *json, const char *key,
    double value, qa_error *error)
{
    return application_unified_json_text(json, key, error) &&
        application_unified_json_number(json, value, error);
}

static bool field_actor(application_unified_json *json, const char *key,
    qa_actor_id value, qa_error *error)
{
    return application_unified_json_text(json, key, error) && actor(json, value, error);
}

static bool field_vector(application_unified_json *json, const char *key,
    qa_vec3 value, qa_error *error)
{
    return application_unified_json_text(json, key, error) &&
        application_unified_json_vector(json, value, error);
}

static bool sound_channel(application_unified_json *json, int32_t channel, qa_error *error)
{
    static const char *const names[] = {"auto", "weapon", "voice", "item", "body"};
    if (!application_unified_json_text(json, ",\"channel\":", error)) return false;
    return channel >= 0 && channel <= 4 ?
        application_unified_json_string(json, names[channel], error) :
        application_unified_json_number(json, channel, error);
}

static bool arguments(application_unified_json *json, qa_application *app,
    const qa_builtin_event *event, qa_error *error)
{
    if (!event->argument_count) return true;
    if (!application_unified_json_text(json, ",\"args\":[", error)) return false;
    for (size_t i = 0; i < event->argument_count; ++i) {
        const qa_builtin_message_arg *argument = event->arguments + i;
        if (i && !application_unified_json_text(json, ",", error)) return false;
        if (argument->kind == QA_BUILTIN_MESSAGE_STRING) {
            if (!text_id(json, app, argument->value.text, error)) return false;
        } else if (!application_unified_json_number(json, argument->value.number, error)) return false;
    }
    return application_unified_json_text(json, "]", error);
}

static bool sound_simulation(application_unified_json *json, qa_application *app,
    const qa_builtin_event *event, qa_error *error)
{
    qa_bytes path = qa_strings_text(qa_session_strings(app->session), event->resource);
    char *requested = malloc(path.size + 1);
    if (!requested) {
        return application_fail(error, QA_ERROR_MEMORY, "Retaining the declared Q1 sound path");
    }
    if (path.size) memcpy(requested, path.data, path.size);
    requested[path.size] = 0;
    char identity[QA_APPLICATION_RESOURCE_KEY_CAPACITY];
    bool found;
    bool okay = application_unified_event_resource_lookup(app, event->provider,
        requested, identity, &found, error);
    free(requested);
    if (!okay) return false;
    if (!found) return true;
    bool ambient = (event->flags & 1u) != 0;
    return application_unified_json_text(json, "{\"kind\":\"sound\",\"resource\":", error) &&
        application_unified_json_string(json, identity, error) &&
        field_actor(json, ",\"actor\":", ambient ? (qa_actor_id){0} : event->actor, error) &&
        field_vector(json, ",\"origin\":", event->origin, error) &&
        field_number(json, ",\"channel\":", ambient ? 0 : event->channel, error) &&
        field_number(json, ",\"volume\":", event->volume, error) &&
        field_number(json, ",\"attenuation\":", event->attenuation, error) &&
        application_unified_json_text(json, "}", error);
}

static bool effect(application_unified_json *json, const qa_builtin_event *event,
    const char *kind, qa_actor_id source_actor, qa_error *error)
{
    return application_unified_json_text(json, "{\"kind\":\"q1\",\"event\":{\"kind\":\"effect\",\"effect\":", error) &&
        application_unified_json_string(json, kind, error) &&
        field_actor(json, ",\"actor\":", source_actor, error) &&
        field_vector(json, ",\"origin\":", event->origin, error) &&
        field_number(json, ",\"amount\":", event->value, error);
}

bool application_unified_q1_event(qa_application *app, const qa_builtin_event *event,
    qa_error *error)
{
    if (!event || event->family != QA_GAME_Q1) return true;
    application_unified_json presentation = {0}, simulation = {0};
    bool okay = true, linked = false;
    qa_actor_id recipient = {0}, simulation_recipient = {0};
    const char *effect_name = NULL;
    switch (event->kind) {
    case QA_BUILTIN_SOUND: {
        bool ambient = (event->flags & 1u) != 0;
        okay = application_unified_json_text(&presentation, ambient ?
            "{\"kind\":\"q1\",\"event\":{\"kind\":\"ambient\"" :
            "{\"kind\":\"q1\",\"event\":{\"kind\":\"sound\"", error) &&
            field_text(&presentation, app, ",\"path\":", event->resource, error) &&
            field_vector(&presentation, ",\"origin\":", event->origin, error) &&
            field_number(&presentation, ",\"volume\":", event->volume, error) &&
            field_number(&presentation, ",\"attenuation\":", event->attenuation, error);
        if (okay && !ambient) okay = field_actor(&presentation, ",\"actor\":", event->actor, error) &&
            sound_channel(&presentation, event->channel, error);
        if (okay) okay = sound_simulation(&simulation, app, event, error);
        break;
    }
    case QA_BUILTIN_STOP_SOUND:
        okay = application_unified_json_text(&presentation,
            "{\"kind\":\"q1\",\"event\":{\"kind\":\"stop-sound\"", error) &&
            field_actor(&presentation, ",\"actor\":", event->actor, error) &&
            field_number(&presentation, ",\"channel\":", event->channel, error);
        break;
    case QA_BUILTIN_ANIMATION: {
        application_provider *source = app->live_providers;
        while (source && source->owner != event->provider) source = source->next_live;
        uint32_t weapon = event->flags & UINT32_C(0x7fffffff);
        if (!source || source->kind != APPLICATION_PROVIDER_Q1 || weapon >= QA_Q1_WEAPON_COUNT) break;
        const char *identity = qa_q1_weapon_identity((qa_q1_weapon)weapon);
        const char *name = weapon == QA_Q1_CTF_GRAPPLE ? "ctf:grapple" : identity + strlen("q1:weapon/");
        okay = application_unified_json_text(&presentation,
            "{\"kind\":\"q1\",\"event\":{\"kind\":\"weapon\",\"weapon\":", error) &&
            application_unified_json_string(&presentation, name, error) &&
            field_actor(&presentation, ",\"player\":", event->actor, error) &&
            field_text(&presentation, app, ",\"viewModel\":", event->resource, error) &&
            field_number(&presentation, ",\"frame\":", event->frame, error) &&
            field_number(&presentation, ",\"punch\":", event->value, error);
        if (okay && (event->flags & UINT32_C(0x80000000))) {
            const char *attack = weapon == QA_Q1_AXE ? "axe" :
                weapon == QA_Q1_SHOTGUN || weapon == QA_Q1_SUPER_SHOTGUN ? "shotgun" :
                weapon == QA_Q1_NAILGUN || weapon == QA_Q1_SUPER_NAILGUN ? "nail" :
                weapon == QA_Q1_LIGHTNING ? "lightning" : "rocket";
            okay = application_unified_json_text(&presentation, ",\"attack\":{\"kind\":", error) &&
                application_unified_json_string(&presentation, attack, error);
            if (okay && weapon == QA_Q1_AXE)
                okay = field_number(&presentation, ",\"variant\":", event->code, error);
            if (okay) okay = application_unified_json_text(&presentation, "}", error);
        }
        break;
    }
    case QA_BUILTIN_MESSAGE: case QA_BUILTIN_CENTERPRINT: {
        bool center = event->kind == QA_BUILTIN_CENTERPRINT || !(event->flags & 2u);
        simulation_recipient = event->actor;
        okay = application_unified_json_text(&presentation,
            "{\"kind\":\"q1\",\"event\":{\"kind\":\"message\"", error) &&
            field_actor(&presentation, ",\"player\":", event->actor, error) &&
            field_text(&presentation, app, ",\"text\":", event->text, error) &&
            application_unified_json_text(&presentation, center ? ",\"center\":true" : ",\"center\":false", error) &&
            arguments(&presentation, app, event, error) &&
            application_unified_json_text(&simulation, center ?
                "{\"kind\":\"message\",\"event\":{\"kind\":\"center-print\",\"text\":" :
                "{\"kind\":\"message\",\"event\":{\"kind\":\"print\",\"level\":2,\"text\":", error) &&
            text_id(&simulation, app, event->text, error) &&
            application_unified_json_text(&simulation, "}}", error);
        linked = true;
        break;
    }
    case QA_BUILTIN_PARTICLES:
        okay = application_unified_json_text(&presentation,
            "{\"kind\":\"q1\",\"event\":{\"kind\":\"particles\"", error) &&
            field_vector(&presentation, ",\"origin\":", event->origin, error) &&
            field_vector(&presentation, ",\"direction\":", event->direction, error) &&
            field_number(&presentation, ",\"color\":", event->code, error) &&
            field_number(&presentation, ",\"count\":", event->count, error);
        break;
    case QA_BUILTIN_LIGHT:
        okay = application_unified_json_text(&presentation,
            "{\"kind\":\"q1\",\"event\":{\"kind\":\"lightstyle\"", error) &&
            field_number(&presentation, ",\"style\":", event->code, error) &&
            field_text(&presentation, app, ",\"pattern\":", event->resource, error);
        break;
    case QA_BUILTIN_BEAM: {
        static const char *const styles[] = {NULL, "lightning1", "lightning2", "lightning3", "grapple"};
        if (event->code < 1 || event->code > 4) break;
        okay = application_unified_json_text(&presentation,
            "{\"kind\":\"q1\",\"event\":{\"kind\":\"beam\",\"style\":", error) &&
            application_unified_json_string(&presentation, styles[event->code], error) &&
            field_actor(&presentation, ",\"actor\":", event->actor, error) &&
            field_vector(&presentation, ",\"start\":", event->origin, error) &&
            field_vector(&presentation, ",\"end\":", event->end, error);
        break;
    }
    case QA_BUILTIN_IMPACT:
        effect_name = event->code == 1 ? "blood" : event->code == 2 ? "gunshot" :
            event->code == 3 ? "superspike" : event->code == 4 ? "spike" :
            event->code == 7 ? "wizard-spike" : event->code == 8 ? "knight-spike" :
            event->code == 10 ? "lava-splash" : NULL;
        break;
    case QA_BUILTIN_EXPLOSION:
        effect_name = event->code == 1 ? "tar-explosion" : event->code == 10 ? "lava-splash" : "explosion";
        break;
    case QA_BUILTIN_TELEPORT: effect_name = "teleport"; break;
    case QA_BUILTIN_MUZZLE: effect_name = "muzzleflash"; break;
    case QA_BUILTIN_ITEM:
        if (event->other.registry) effect_name = "pickup";
        break;
    case QA_BUILTIN_DEATH:
        if (!event->count) break;
        okay = application_unified_json_text(&presentation,
            "{\"kind\":\"q1\",\"event\":{\"kind\":\"monster-killed\"", error) &&
            field_actor(&presentation, ",\"actor\":", event->actor, error) &&
            field_number(&presentation, ",\"total\":", event->value, error) &&
            field_number(&presentation, ",\"found\":", event->count, error);
        break;
    case QA_BUILTIN_EFFECT: {
        qa_bytes resource = qa_strings_text(qa_session_strings(app->session), event->resource);
        if (resource.size == sizeof("colored-explosion") - 1 &&
            !memcmp(resource.data, "colored-explosion", resource.size))
            okay = application_unified_json_text(&presentation,
                "{\"kind\":\"q1\",\"event\":{\"kind\":\"colored-explosion\"", error) &&
                field_vector(&presentation, ",\"origin\":", event->origin, error) &&
                field_number(&presentation, ",\"colorStart\":", event->code, error) &&
                field_number(&presentation, ",\"colorLength\":", event->count, error);
        else if (resource.size == sizeof("developer-message") - 1 &&
            !memcmp(resource.data, "developer-message", resource.size))
            okay = application_unified_json_text(&presentation,
                "{\"kind\":\"q1-composition\",\"event\":{\"kind\":\"developer-message\"", error) &&
                field_text(&presentation, app, ",\"text\":", event->text, error);
        else if (!event->actor.registry && event->resource &&
            !(event->flags & UINT32_C(0x80000000))) {
            /* Only the genuine held model declaration identifies a static
             * source model; generic authored effect resource names do not. */
            application_provider *source = app->live_providers;
            while (source && source->owner != event->provider) source = source->next_live;
            uint32_t index;
            if (source && source->kind == APPLICATION_PROVIDER_Q1 &&
                qa_q1_wire_emission_index(source->state.q1, true, event->resource, &index))
                okay = application_unified_json_text(&presentation,
                    "{\"kind\":\"q1\",\"event\":{\"kind\":\"static-model\"", error) &&
                    field_text(&presentation, app, ",\"path\":", event->resource, error) &&
                    field_number(&presentation, ",\"frame\":", event->frame, error) &&
                    field_number(&presentation, ",\"skin\":", event->code, error) &&
                    field_number(&presentation, ",\"colorMap\":", event->channel, error) &&
                    field_vector(&presentation, ",\"origin\":", event->origin, error) &&
                    field_vector(&presentation, ",\"angles\":", event->direction, error);
        }
        break;
    }
    case QA_BUILTIN_ACHIEVEMENT:
        okay = application_unified_json_text(&presentation,
            "{\"kind\":\"q1\",\"event\":{\"kind\":\"achievement\"", error) &&
            field_actor(&presentation, ",\"player\":", event->actor, error) &&
            field_text(&presentation, app, ",\"id\":", event->resource, error);
        break;
    case QA_BUILTIN_CTF_STATUS:
        okay = application_unified_json_text(&presentation,
            "{\"kind\":\"q1-composition\",\"event\":{\"kind\":\"ctf-status\"", error) &&
            field_actor(&presentation, ",\"actor\":", event->actor, error) &&
            application_unified_json_text(&presentation, ",\"status\":{", error) &&
            field_number(&presentation, "\"red\":", event->ctf_status.red, error) &&
            field_number(&presentation, ",\"blue\":", event->ctf_status.blue, error) &&
            field_number(&presentation, ",\"flags\":", event->ctf_status.flags, error) &&
            field_number(&presentation, ",\"runeItems\":", event->ctf_status.rune_items, error) &&
            application_unified_json_text(&presentation, "}", error);
        break;
    case QA_BUILTIN_SOURCE_LOG:
        okay = application_unified_json_text(&presentation,
            "{\"kind\":\"q1-composition\",\"event\":{\"kind\":\"source-log\"", error) &&
            field_actor(&presentation, ",\"actor\":", event->actor, error) &&
            field_text(&presentation, app, ",\"action\":", event->text, error);
        break;
    case QA_BUILTIN_CTF_CAPTURE:
        okay = application_unified_json_text(&presentation,
            "{\"kind\":\"q1-composition\",\"event\":{\"kind\":\"ctf-capture\",\"team\":", error) &&
            application_unified_json_string(&presentation, event->ctf_capture.blue ? "blue" : "red", error) &&
            field_number(&presentation, ",\"total\":", event->ctf_capture.total, error);
        break;
    case QA_BUILTIN_SOURCE_PROMPT:
        okay = application_unified_json_text(&presentation,
            "{\"kind\":\"q1-composition\",\"event\":{\"kind\":\"prompt\"", error) &&
            field_actor(&presentation, ",\"actor\":", event->actor, error) &&
            field_text(&presentation, app, ",\"title\":", event->text, error) &&
            application_unified_json_text(&presentation, ",\"choices\":[", error);
        for (size_t i = 0; okay && i < event->prompt_choice_count; ++i) {
            const qa_builtin_prompt_choice *choice = event->prompt_choices + i;
            okay = (!i || application_unified_json_text(&presentation, ",", error)) &&
                application_unified_json_text(&presentation, "{\"label\":", error) &&
                text_id(&presentation, app, choice->label, error) &&
                field_number(&presentation, ",\"impulse\":", choice->impulse, error) &&
                application_unified_json_text(&presentation, "}", error);
        }
        if (okay) okay = application_unified_json_text(&presentation, "]", error);
        break;
    case QA_BUILTIN_CLEAR_PROMPT:
        okay = application_unified_json_text(&presentation,
            "{\"kind\":\"q1-composition\",\"event\":{\"kind\":\"clear-prompt\"", error) &&
            field_actor(&presentation, ",\"actor\":", event->actor, error);
        break;
    case QA_BUILTIN_Q1_POWERUP: {
        static const char *const names[QA_Q1_POWER_COUNT] = {"quad", "invulnerability",
            "invisibility", "suit", "hipnotic:wetsuit", "hipnotic:empathy", "rogue:shield",
            "rogue:antigrav", "mg3:lavasuit"};
        if (event->q1_powerup.power >= QA_Q1_POWER_COUNT) {
            okay = application_fail(error, QA_ERROR_ARGUMENT, "Q1 event has an undeclared source power");
            break;
        }
        okay = application_unified_json_text(&presentation,
            "{\"kind\":\"q1\",\"event\":{\"kind\":\"powerup\",\"powerup\":", error) &&
            application_unified_json_string(&presentation, names[event->q1_powerup.power], error) &&
            field_actor(&presentation, ",\"player\":", event->actor, error) &&
            field_number(&presentation, ",\"expires\":", event->q1_powerup.expires, error);
        break;
    }
    default: break;
    }
    if (okay && effect_name) okay = effect(&presentation, event, effect_name, event->actor, error);
    if (okay && presentation.bytes.size) {
        okay = application_unified_json_text(&presentation, "}}", error);
        int32_t source_entity = 0;
        bool has_source_entity = false;
        const qa_actor_record *record = event->actor.registry ?
            qa_actors_get(qa_session_actors(app->session), event->actor) : NULL;
        if (record && record->has_source && record->owner == event->provider && record->source_slot <= INT32_MAX) {
            source_entity = (int32_t)record->source_slot;
            has_source_entity = true;
        }
        if (okay) okay = application_unified_event_emit(app, event->provider,
            (qa_bytes){presentation.bytes.data, presentation.bytes.size},
            (qa_bytes){simulation.bytes.data, simulation.bytes.size}, recipient,
            simulation_recipient, event->time_ns, source_entity, has_source_entity, linked, error);
    }
    application_unified_json_dispose(&presentation);
    application_unified_json_dispose(&simulation);
    return okay;
}
