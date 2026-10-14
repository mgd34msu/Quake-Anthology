#include "network_q2_private.h"
#include "unified_events.h"
#include "unified_q2_native_events.h"
#include "control_frame.h"
#include "qa/binary.h"
#include "qa/localization.h"
#include "qa/text.h"

static bool unsupported(qa_application_network_q2 *owner, const char *name,
                        const char *reason)
{
    qa_error error = {0};
    qa_error_set(&error, QA_ERROR_UNSUPPORTED, 0, "%s: %s", name ? name : "Q2 presentation", reason);
    qa_application_feature_report(owner->app, "Q2 protocol presentation", &error);
    return true;
}

static bool builtin_missing(qa_application_network_q2 *owner, const qa_builtin_event *event,
                            const char *reason)
{
    const char *name = qa_strings_cstr(qa_session_strings(owner->app->session), event->resource);
    return unsupported(owner, name && *name ? name : "Q2 builtin", reason);
}

static void vector(float out[3], qa_vec3 value)
{ out[0] = value.x; out[1] = value.y; out[2] = value.z; }

static bool message(qa_application_network_q2 *owner, const qa_builtin_event *source,
    const qa_q2_codec *codec, qa_q2_server_emit_fn emit, void *user, qa_error *error)
{
    qa_strings *strings = qa_session_strings(owner->app->session);
    const char *text = source->message_format ? source->message_format : qa_strings_cstr(strings, source->text);
    if (!text) return builtin_missing(owner, source, "message has no retained text");
    qa_q2_server_record record = {.opcode = source->kind == QA_BUILTIN_CENTERPRINT ? 15 : 10,
        .event = {.kind = source->kind == QA_BUILTIN_CENTERPRINT ? QA_Q2_SVC_CENTERPRINT : QA_Q2_SVC_PRINT}};
    if (source->kind == QA_BUILTIN_MESSAGE && (source->code < 0 || source->code > UINT8_MAX))
        return builtin_missing(owner, source, "print level exceeds the original byte");
    record.event.data.print.level = (uint8_t)source->code;
    record.event.data.print.text = text;
    if (!source->argument_count && *text != '$') return emit(user, &record, error);
    qa_error failure = {0};
    qa_unified_frame_lease *lease = application_control_storage_acquire(owner->app, &failure);
    if (!lease) return builtin_missing(owner, source, failure.message);
    qa_arena *storage = qa_unified_frame_lease_arena(lease);
    bool okay = false;
    if (source->argument_count > SIZE_MAX / (sizeof(const char *) + 32)) goto out;
    const char **arguments = source->argument_count ? qa_arena_alloc(storage,
        source->argument_count * (sizeof(*arguments) + 32), _Alignof(const char *), &failure) : NULL;
    if (source->argument_count && !arguments) goto out;
    char *numbers = arguments ? (char *)(arguments + source->argument_count) : NULL;
    for (size_t i = 0; i < source->argument_count; ++i) {
        const qa_builtin_message_arg *arg = source->arguments + i;
        if (arg->kind == QA_BUILTIN_MESSAGE_STRING) arguments[i] = qa_strings_cstr(strings, arg->value.text);
        else if (arg->kind == QA_BUILTIN_MESSAGE_TEXT) arguments[i] = arg->value.literal;
        else {
            arguments[i] = numbers + i * 32;
            if (!qa_format_number(arg->value.number, numbers + i * 32, &failure)) goto out;
        }
        if (!arguments[i]) goto out;
    }
    if (qa_q2_protocol_rerelease(codec->protocol)) {
        qa_q2_kex_locprint *localized = &record.event.data.localized;
        record.opcode = 26;
        record.event.kind = QA_Q2_SVC_LOCALIZED_PRINT;
        memset(localized, 0, sizeof(*localized));
        if (source->argument_count > sizeof(localized->args) / sizeof(*localized->args) ||
            strlen(text) >= sizeof(localized->base)) goto out;
        localized->flags = source->kind == QA_BUILTIN_CENTERPRINT ? 5u : (uint8_t)source->code;
        memcpy(localized->base, text, strlen(text) + 1);
        localized->arg_count = source->argument_count;
        for (size_t i = 0; i < source->argument_count; ++i) {
            size_t length = strlen(arguments[i]);
            if (length >= sizeof(localized->args[i])) goto out;
            memcpy(localized->args[i], arguments[i], length + 1);
        }
    } else {
        if (*text == '$') goto out;
        qa_buffer formatted = {0};
        if (!qa_localize_presentation_arena(NULL, text, arguments, source->argument_count,
                true, storage, &formatted, &failure)) goto out;
        record.event.data.print.text = (const char *)formatted.data;
    }
    okay = true;
out:
    if (okay) okay = emit(user, &record, error);
    else okay = builtin_missing(owner, source, failure.message[0] ? failure.message :
        "localized text cannot be represented by this protocol");
    qa_unified_frame_lease_release(lease);
    return okay;
}

static bool temporary(qa_application_network_q2 *owner, const qa_builtin_event *source,
    qa_q2_server_emit_fn emit, void *user, qa_error *error)
{
    qa_application_q2_effect_name recipe = qa_application_q2_effect_name_read(owner->app, source->resource);
    uint16_t selected = source->kind == QA_BUILTIN_BEAM ? recipe.beam : recipe.effect;
    if (!source->resource && source->kind == QA_BUILTIN_PARTICLES && source->code >= 0 && source->code < 256)
        selected = (uint16_t)(source->code + 1);
    if (!selected || selected > 256)
        return builtin_missing(owner, source, "no original temporary-entity representation");
    qa_q2_server_record record = {.opcode = 3, .event = {.kind = QA_Q2_SVC_TEMP_ENTITY}};
    qa_q2_temp_entity *value = &record.event.data.temporary;
    if (!qa_q2_temp_entity_prepare((uint8_t)(selected - 1), false, value))
        return builtin_missing(owner, source, "temporary entity is unsupported by the shared codec");
    qa_error failure = {0};
    for (size_t i = 0; i < value->field_count; ++i) {
        qa_q2_temp_field *field = value->fields + i;
        switch (field->name) {
        case QA_Q2_TEMP_POSITION1: vector(field->value.vector, source->origin); break;
        case QA_Q2_TEMP_POSITION2: vector(field->value.vector, source->end); break;
        case QA_Q2_TEMP_DIRECTION: case QA_Q2_TEMP_OFFSET: vector(field->value.vector, source->direction); break;
        case QA_Q2_TEMP_COUNT: field->value.integer = (uint8_t)source->count; break;
        case QA_Q2_TEMP_COLOR: field->value.integer = (uint8_t)source->code; break;
        case QA_Q2_TEMP_TIME:
            return builtin_missing(owner, source, "timed steam uses its typed map-event projection");
        case QA_Q2_TEMP_ENTITY1: case QA_Q2_TEMP_ENTITY2: {
            if (value->type == QA_Q2_TE_WIDOWBEAMOUT && field->name == QA_Q2_TEMP_ENTITY1) {
                field->value.integer = source->code;
                break;
            }
            if (value->type == QA_Q2_TE_STEAM)
                return builtin_missing(owner, source, "steam uses its typed map-event projection");
            uint32_t number = 0;
            qa_actor_id actor = field->name == QA_Q2_TEMP_ENTITY1 ? source->actor : source->other;
            if (!actor.registry || !qa_application_network_q2_event_entity(owner, source->provider,
                    actor, &number, &failure)) return builtin_missing(owner, source,
                        failure.message[0] ? failure.message : "temporary entity has no full Source actor");
            if (number > INT16_MAX)
                return builtin_missing(owner, source, "temporary entity actor exceeds the original short");
            field->value.integer = (int32_t)number;
            break;
        }
        }
    }
    return emit(user, &record, error);
}

bool qa_application_network_q2_builtin_records(qa_application_network_q2 *owner,
    const qa_builtin_event *source, const qa_q2_codec *codec, qa_q2_server_emit_fn emit,
    void *user, qa_error *error)
{
    if (!owner || !source || !codec || !emit) return false;
    if (source->family != QA_GAME_Q2) return true;
    qa_q2_server_record record = {0};
    qa_error failure = {0};
    switch (source->kind) {
    case QA_BUILTIN_SOUND: {
        if (source->flags & 1u) return true;
        qa_strings *strings = qa_session_strings(owner->app->session);
        const char *name = qa_strings_cstr(strings, source->resource);
        if (!name || !*name) return true;
        qa_application_protocol_resource_reference reference = {.kind = QA_NATIVE_HOST_SOUND,
            .name = name, .name_id = source->resource};
        bool found;
        uint32_t index;
        if (!application_unified_event_resource_lookup_receipt(owner->app, source->provider,
                QA_NATIVE_HOST_SOUND, source->resource, &reference.resource_key,
                &reference.resource_custody, &found, &failure) ||
            !qa_application_network_q2_event_resource(owner, source->provider, &reference, &index, &failure))
            return builtin_missing(owner, source, failure.message);
        if (index > UINT16_MAX) return builtin_missing(owner, source, "sound index exceeds the original word");
        record.opcode = 9;
        record.event.kind = QA_Q2_SVC_SOUND;
        qa_q2_kex_sound *sound = &record.event.data.sound;
        *sound = (qa_q2_kex_sound){.index = (uint16_t)index, .channel = (uint8_t)((uint32_t)source->channel & 7u),
            .volume = source->volume, .attenuation = source->attenuation, .has_position = true};
        vector(sound->position, source->origin);
        if (source->actor.registry && !qa_application_network_q2_event_entity(owner, source->provider,
                source->actor, &sound->entity, &failure)) return builtin_missing(owner, source, failure.message);
        sound->flags = qa_q2_sound_flags(codec, sound);
        break;
    }
    case QA_BUILTIN_MUZZLE: {
        uint32_t number;
        if (!source->actor.registry || !qa_application_network_q2_event_entity(owner, source->provider,
                source->actor, &number, &failure)) return builtin_missing(owner, source,
                    failure.message[0] ? failure.message : "muzzleflash has no full Source actor");
        if (source->code < 0 || number > UINT16_MAX)
            return builtin_missing(owner, source, "muzzleflash exceeds its original fields");
        bool monster = source->resource == owner->app->ui_names.monster_muzzle;
        record.opcode = monster ? 2 : 1;
        record.event.kind = QA_Q2_SVC_MUZZLEFLASH;
        record.event.data.muzzle.entity = number;
        record.event.data.muzzle.flash = (uint32_t)source->code;
        record.event.data.muzzle.monster = monster;
        record.event.data.muzzle.silenced = !monster && (source->flags & 128u) != 0;
        break;
    }
    case QA_BUILTIN_CENTERPRINT: case QA_BUILTIN_MESSAGE:
        return message(owner, source, codec, emit, user, error);
    case QA_BUILTIN_BEAM: case QA_BUILTIN_PARTICLES: case QA_BUILTIN_IMPACT:
    case QA_BUILTIN_EXPLOSION: case QA_BUILTIN_EFFECT: case QA_BUILTIN_TELEPORT:
        return temporary(owner, source, emit, user, error);
    default: return true;
    }
    return emit(user, &record, error);
}

bool qa_application_network_q2_player_records(qa_application_network_q2 *owner,
    const qa_q2_player_event *source, const qa_q2_codec *codec, qa_q2_server_emit_fn emit,
    void *user, qa_error *error)
{
    if (!owner || !source || !codec || !emit) return false;
    qa_q2_server_record record = {0};
    int16_t counts[256];
    switch (source->kind) {
    case QA_Q2_PLAYER_PRINT:
        if (source->level < 0 || source->level > UINT8_MAX)
            return unsupported(owner, "Q2 player print", "print level exceeds the original byte");
        record.opcode = 10;
        record.event.kind = QA_Q2_SVC_PRINT;
        record.event.data.print.level = (uint8_t)source->level;
        record.event.data.print.text = source->text;
        break;
    case QA_Q2_PLAYER_STUFFTEXT:
        record.opcode = 11;
        record.event.kind = QA_Q2_SVC_COMMAND;
        record.event.data.print.text = source->text;
        break;
    case QA_Q2_PLAYER_LAYOUT:
        record.opcode = 4;
        record.event.kind = QA_Q2_SVC_LAYOUT;
        record.event.data.print.text = source->visible && source->text ? source->text : "";
        break;
    case QA_Q2_PLAYER_INVENTORY:
        if (!source->visible) return true;
        if (owner->host.source.kind != QA_APPLICATION_NATIVE_Q2_BUILTIN)
            return unsupported(owner, "Q2 inventory", "native host keeps its original typed protocol inventory");
        application_q2_inventory_counts((const qa_q2_game *)owner->host.source.source.game, source, counts);
        record.opcode = 5;
        record.event.kind = QA_Q2_SVC_INVENTORY;
        record.event.data.inventory.counts = counts;
        record.event.data.inventory.count = 256;
        break;
    default: return true;
    }
    if ((record.event.kind == QA_Q2_SVC_PRINT || record.event.kind == QA_Q2_SVC_COMMAND) &&
        !record.event.data.print.text) return unsupported(owner, "Q2 player text", "no retained Source text");
    return emit(user, &record, error);
}

bool qa_application_network_q2_map_records(qa_application_network_q2 *owner,
    const qa_q2_map_event *source, const qa_q2_codec *codec, qa_q2_server_emit_fn emit,
    void *user, qa_error *error)
{
    if (!owner || !source || !codec || !emit) return false;
    uint8_t type;
    switch (source->kind) {
    case QA_Q2_MAP_STEAM: type = QA_Q2_TE_STEAM; break;
    case QA_Q2_MAP_FORCE_WALL: type = QA_Q2_TE_FORCEWALL; break;
    default: return true;
    }
    qa_q2_server_record record = {.opcode = 3, .event = {.kind = QA_Q2_SVC_TEMP_ENTITY}};
    qa_q2_temp_entity *value = &record.event.data.temporary;
    if (!qa_q2_temp_entity_prepare(type, false, value))
        return unsupported(owner, type == QA_Q2_TE_STEAM ? "Q2 steam" : "Q2 force wall",
            "temporary entity is unsupported by the shared codec");
    if (type == QA_Q2_TE_STEAM && source->slot == -1) --value->field_count;
    for (size_t i = 0; i < value->field_count; ++i) {
        qa_q2_temp_field *field = value->fields + i;
        switch (field->name) {
        case QA_Q2_TEMP_POSITION1: vector(field->value.vector, source->origin); break;
        case QA_Q2_TEMP_POSITION2: case QA_Q2_TEMP_DIRECTION:
            vector(field->value.vector, source->direction); break;
        case QA_Q2_TEMP_COUNT: field->value.integer = (uint8_t)source->count; break;
        case QA_Q2_TEMP_COLOR: field->value.integer = (uint8_t)source->style; break;
        case QA_Q2_TEMP_TIME: field->value.integer = qa_source_to_i32(source->duration); break;
        case QA_Q2_TEMP_ENTITY1: case QA_Q2_TEMP_ENTITY2: {
            int32_t integer = field->name == QA_Q2_TEMP_ENTITY1 ? source->slot :
                qa_source_to_i32(source->value);
            uint8_t bytes[2];
            qa_store_u16le(bytes, (uint16_t)(uint32_t)integer);
            field->value.integer = qa_load_i16le(bytes);
            break;
        }
        case QA_Q2_TEMP_OFFSET:
            return unsupported(owner, "Q2 map temporary entity", "map event has no offset field");
        }
    }
    return emit(user, &record, error);
}
