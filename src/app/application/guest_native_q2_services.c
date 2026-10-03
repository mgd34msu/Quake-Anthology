#include "guest_native_q2_private.h"
#include "native_q2_client_stages.h"
#include "native_q2_source_actors.h"
#include "guest_native_q2_combat.h"
#include "control_frame.h"
#include "guest_native_q2_input.h"
#include "native_q2_delivery.h"
#include "native_q2_callbacks.h"
#include "native_q2_protocol_resources.h"
#include "unified_events.h"
#include "qa/network_q2_messages.h"
#include "qa/native_host_q2_wire.h"
#include "qa/application_network_q2.h"
#include <math.h>

static bool protocol(struct application_native_q2 *engine, const qa_q2_server_event *source,
                       qa_actor_id recipient, bool reliable, qa_error *error)
{
    qa_q2_codec codec;
    qa_net_protocol_id id = {.kind = engine->profile == QA_NATIVE_Q2_GAME_API3 ? QA_NET_Q2_34 : QA_NET_Q2KEX_2023};
    if (!qa_q2_codec_init(&codec, id, error)) return false;
    uint8_t bytes[65536]; qa_net_writer writer;
    qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    if (!qa_q2_server_event_write(&codec, &writer, source)) return false;
    qa_application_protocol_event event = {.recipient = recipient,
        .payload = {bytes, qa_net_writer_size(&writer)}, .reliable = reliable,
        .multicast = !recipient.registry};
    if (engine->profile == QA_NATIVE_Q2_CGAME_API2023)
        return application_emit_protocol(engine->provider, &event, error);
    qa_native_host_message packet = {.target = recipient.registry ? QA_NATIVE_HOST_UNICAST : QA_NATIVE_HOST_MULTICAST,
        .payload = event.payload, .client = recipient, .destination = 0, .reliable = reliable};
    qa_application_q2_protocol_delivery delivery = {.profile = engine->profile, .original = true};
    if (engine->initialized && engine->map_ready && engine->calls &&
        !application_native_q2_message_capture(engine, &packet, &delivery, error)) return false;
    application_native_q2_protocol_resources resources = {0};
    bool ok = application_native_q2_protocol_resources_capture(engine, event.payload,
        event.references, event.reference_count, &resources, error);
    if (ok) {
        event.resources = resources.rows; event.resource_count = resources.count;
        event.references = resources.references; event.reference_count = resources.reference_count;
        ok = application_emit_q2_protocol(engine->provider, &event, &delivery, error);
    }
    application_native_q2_protocol_resources_dispose(&resources);
    application_native_q2_delivery_dispose(&delivery.audience);
    return ok;
}

static void print(void *opaque, const qa_native_host_print *source)
{
    struct application_native_q2 *engine = opaque;
    qa_command_context context = engine->command_context;
    context.actor = source->client;
    if (source->kind == QA_NATIVE_HOST_PRINT_DEBUG ||
        qa_console_output_redirected(engine->provider->application->console))
        qa_console_emit(engine->provider->application->console, &context, source->text);
    if (source->kind != QA_NATIVE_HOST_PRINT_DEBUG) {
        qa_q2_server_event event = {.kind = source->kind == QA_NATIVE_HOST_PRINT_CENTER ?
            QA_Q2_SVC_CENTERPRINT : QA_Q2_SVC_PRINT,
            .data.print = {.level = (uint8_t)source->level, .text = source->text}};
        qa_error error = {0};
        if (!protocol(engine, &event, source->client, true, &error))
            application_fault(engine->provider->application, &error);
    }
    if (source->kind != QA_NATIVE_HOST_PRINT_DEBUG &&
        !qa_console_output_redirected(engine->provider->application->console) && engine->platform.print)
        engine->platform.print(engine->platform.context, source);
}

static bool config_get(void *opaque, int32_t index, const char **out, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    if (engine->profile == QA_NATIVE_Q2_CGAME_API2023) {
        uint32_t slot;
        struct application_native_q2 *source = application_native_q2_hud_source(engine, &slot, error);
        if (!source) return false;
        engine = source;
    }
    if (!out || index < 0 || (uint32_t)index >= engine->configstring_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 configstring exceeds its original API table");
    *out = engine->configstrings[index] ? engine->configstrings[index] : "";
    return true;
}

static bool register_file(struct application_native_q2 *, qa_native_host_resource_kind,
    const char *, qa_error *);

static bool config_set(void *opaque, int32_t index, const char *value, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    const char *prior;
    if (!value || !config_get(engine, index, &prior, error)) return false;
    if (!strcmp(prior, value)) return true;
    for (unsigned kind = 0; kind <= QA_NATIVE_HOST_IMAGE; ++kind)
        if ((uint32_t)index > engine->resource_base[kind] &&
            (uint32_t)index - engine->resource_base[kind] < engine->resource_limit[kind] &&
            !register_file(engine, (qa_native_host_resource_kind)kind, value, error)) return false;
    size_t size = strlen(value);
    char *copy = malloc(size + 1);
    if (!copy) return application_fail(error, QA_ERROR_MEMORY, "Retaining native Q2 configstring");
    memcpy(copy, value, size + 1);
    free(engine->configstrings[index]); engine->configstrings[index] = copy;
    ++engine->config_revision;
    if (!engine->map_ready) return true;
    qa_q2_server_event event = {.kind = QA_Q2_SVC_CONFIGSTRING,
        .data.config = {.index = (uint16_t)index, .value = copy}};
    return protocol(engine, &event, (qa_actor_id){0}, true, error);
}

static bool register_file(struct application_native_q2 *engine, qa_native_host_resource_kind kind,
    const char *name, qa_error *error)
{
    if (!*name || *name == '*') return true;
    char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY]; bool found;
    if (!application_unified_event_resource_lookup_kind(engine->provider->application,
        engine->provider->owner, kind, name, id, &found, error)) return false;
    if (found) return true;
    if (kind == QA_NATIVE_HOST_IMAGE) {
        if (!engine->platform.resource_precache) return true;
        const qa_vfs *view = NULL; qa_resource *held = NULL; qa_vfs_acquisition opening = {0};
        bool ok = engine->platform.resource_precache(engine->platform.context, kind, name,
            &view, &held, &opening, &found, error);
        if (ok && found) ok = application_unified_event_resource_register_acquired(engine->provider->application,
            engine->provider->owner, kind, name, view, held, &opening, id, error);
        if (ok && !found && (held || opening.path || opening.lookup_path || opening.link_source ||
            opening.link_target || opening.mount || opening.resource_id))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Absent Q2 image precache retained an unclaimed opening");
        qa_resource_release(held); qa_vfs_acquisition_dispose(&opening); return ok;
    }
    const char *opening = *name == '#' ? name + 1 : name;
    size_t length = strlen(opening), prefix = kind == QA_NATIVE_HOST_SOUND && *name != '#' ? 6 : 0;
    if (length > SIZE_MAX - 7)
        return application_fail(error, QA_ERROR_MEMORY, "Native Q2 resource precache path is too large");
    char *path = malloc(length + 7);
    if (!path) return application_fail(error, QA_ERROR_MEMORY, "Retaining native Q2 resource precache path");
    if (prefix) memcpy(path, "sound/", prefix);
    memcpy(path + prefix, opening, length + 1);
    qa_resource *held = NULL; qa_vfs_acquisition receipt = {0}; qa_error acquisition = {0};
    bool ok = qa_vfs_acquire_receipt(engine->provider->launch->content, path, &held, &receipt, &acquisition);
    free(path);
    if (!ok) {
        qa_resource_release(held); qa_vfs_acquisition_dispose(&receipt);
        if (acquisition.code == QA_ERROR_NOT_FOUND) return true;
        if (error) *error = acquisition;
        return false;
    }
    ok = application_unified_event_resource_register_acquired(engine->provider->application,
        engine->provider->owner, kind, name, engine->provider->launch->content, held, &receipt, id, error);
    qa_resource_release(held); qa_vfs_acquisition_dispose(&receipt);
    return ok;
}

static bool resource(void *opaque, qa_native_host_resource_kind kind, const char *name,
                       int32_t *out, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    if (!out || !name || (unsigned)kind > QA_NATIVE_HOST_IMAGE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid native Q2 resource namespace");
    *out = 0;
    if (!*name) return true;
    uint32_t base = engine->resource_base[kind], maximum = engine->resource_limit[kind];
    for (uint32_t i = 1; i < maximum; ++i) {
        const char *value = engine->configstrings[base + i];
        if (value && !strcmp(value, name)) {
            if (!register_file(engine, kind, name, error)) return false;
            *out = (int32_t)i; return true;
        }
        if (!value || !*value) {
            if (!register_file(engine, kind, name, error) ||
                !config_set(engine, (int32_t)(base + i), name, error)) return false;
            *out = (int32_t)i; return true;
        }
    }
    return application_fail(error, QA_ERROR_MEMORY, "Native Q2 source resource namespace is full");
}

static bool command(void *opaque, qa_native_host_command_view *out, qa_error *error)
{
    (void)error;
    struct application_native_q2 *engine = opaque;
    *out = (qa_native_host_command_view){.count = engine->arguments.count,
        .arguments = (const char *const *)engine->arguments.values, .tail = engine->arguments.args_text};
    return true;
}

static bool message(void *opaque, const qa_native_host_message *source, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    if (source->reference_count > SIZE_MAX / sizeof(qa_application_protocol_reference) ||
        (source->reference_count && !source->references))
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 message reference extent is invalid");
    qa_application_protocol_reference *references = source->reference_count ?
        calloc(source->reference_count, sizeof(*references)) : NULL;
    if (source->reference_count && !references)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining written native Q2 message identities");
    for (size_t i = 0; i < source->reference_count; ++i)
        references[i] = (qa_application_protocol_reference){.offset = source->references[i].offset,
            .actor = source->references[i].actor};
    qa_application_protocol_event event = {.recipient = source->client, .origin = source->origin,
        .payload = source->payload, .destination = source->destination,
        .references = references, .reference_count = source->reference_count,
        .reliable = source->reliable, .multicast = source->target == QA_NATIVE_HOST_MULTICAST};
    qa_application_q2_protocol_delivery delivery;
    if (!application_native_q2_message_capture(engine,source,&delivery,error)) { free(references); return false; }
    bool duplicate = false;
    bool keyed = delivery.dupe_key && delivery.audience.captured && delivery.audience.count;
    if (keyed && !qa_application_network_q2_unicast(engine->provider->application,
        engine->provider->owner, source->client, delivery.dupe_key, false, &duplicate, error)) {
        free(references); application_native_q2_delivery_dispose(&delivery.audience); return false;
    }
    if (duplicate) {
        free(references); application_native_q2_delivery_dispose(&delivery.audience); return true;
    }
    application_native_q2_protocol_resources resources = {0};
    bool emitted = application_native_q2_protocol_resources_capture(engine, event.payload,
        event.references, event.reference_count, &resources, error);
    if (emitted) {
        event.resources = resources.rows; event.resource_count = resources.count;
        event.references = resources.references; event.reference_count = resources.reference_count;
        emitted = application_emit_q2_protocol(engine->provider,&event,&delivery,error);
    }
    if (emitted && keyed) emitted = qa_application_network_q2_unicast(engine->provider->application,
        engine->provider->owner, source->client, delivery.dupe_key, true, &duplicate, error);
    free(references);
    application_native_q2_protocol_resources_dispose(&resources);
    application_native_q2_delivery_dispose(&delivery.audience);
    if (!emitted) return false;
    return !engine->platform.message || engine->platform.message(engine->platform.context, source, error);
}

static bool sound(void *opaque, const qa_native_host_sound *source, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    if (source->index <= 0 || (uint32_t)source->index >= engine->resource_limit[QA_NATIVE_HOST_SOUND])
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 sound references an unregistered source index");
    const char *name = engine->configstrings[engine->resource_base[QA_NATIVE_HOST_SOUND] + (uint32_t)source->index];
    if (!name || !*name) return application_fail(error, QA_ERROR_FORMAT, "Native Q2 sound has no admitted source resource");
    qa_native_host_sound named = *source; named.name = name;
    bool rerelease = engine->profile == QA_NATIVE_Q2_GAME_API2023;
    if (!isfinite(source->volume) || source->volume < 0 || source->volume > 1 ||
        !isfinite(source->attenuation) || source->attenuation < 0 || source->attenuation > 4 ||
        !isfinite(source->time_offset) || source->time_offset < 0 || source->time_offset > .255f ||
        (!rerelease && !source->actor.registry) ||
        (!source->actor.registry && !source->positioned))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 sound parameters exceed the genuine Source ranges");
    if (source->local && !source->client.registry) return true;
    uint32_t slot = 0;
    qa_native_host_q2_entity actual = {0};
    if (source->actor.registry) {
        qa_native_entity_table table;
        qa_native_instance *instance = qa_native_host_instance(engine->provider->state.native.host);
        if (!instance || !qa_native_entity_table_get(instance, &table, error)) return false;
        bool found = false;
        for (uint32_t i = 0; i < table.capacity; ++i) {
            qa_native_slot_binding binding;
            if (!qa_native_slot(instance, i, &binding, error)) return false;
            if (binding.kind != QA_NATIVE_SLOT_FREE && qa_actor_id_equal(binding.actor, source->actor)) {
                slot = i; found = true; break;
            }
        }
        if (!found) return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 sound actor projection retired");
        if (!qa_native_host_q2_wire_entity_import(engine->provider->state.native.host, slot, &actual, error)) return false;
        if (!qa_actor_id_equal(actual.binding.actor, source->actor))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 sound Source binding changed during its import");
    }
    qa_vec3 origin = source->origin;
    if (!source->positioned) {
        origin = (qa_vec3){actual.state.origin[0], actual.state.origin[1], actual.state.origin[2]};
        if (actual.solid == 3)
            origin = qa_vec_add(origin, qa_vec_scale(qa_vec_add(actual.bounds.mins, actual.bounds.maxs), .5f));
    }
    if (!qa_vec_finite(origin))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 sound Source origin is not finite");
    bool positioned = rerelease || source->positioned || (actual.server_flags & 1u) || actual.solid == 3;
    if (source->actor.registry && !qa_application_network_q2_entity_number(engine->provider->application,
        engine->provider->owner, source->actor, &slot, error)) return false;
    bool no_phs = (source->channel & 8u) != 0;
    bool reliable = (source->channel & 16u) != 0 && (rerelease || !no_phs);
    qa_q2_server_event event = {.kind = QA_Q2_SVC_SOUND, .data.sound = {
        .flags = source->actor.registry ? 8u : 0u, .index = (uint16_t)source->index,
        .volume = source->volume, .attenuation = source->attenuation,
        .time_offset = source->time_offset, .entity = slot,
        .channel = source->actor.registry ? source->channel & 7u : 0u,
        .has_position = positioned, .position = {origin.x, origin.y, origin.z}}};
    qa_q2_codec codec; uint8_t bytes[64]; qa_net_writer writer;
    qa_net_protocol_id profile = {.kind = rerelease ? QA_NET_Q2REPRO_1038 : QA_NET_Q2_34};
    if (!qa_q2_codec_init(&codec, profile, error)) return false;
    qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    if (!qa_q2_server_event_write(&codec, &writer, &event)) return false;
    qa_native_host_message packet = {.target = source->local ? QA_NATIVE_HOST_UNICAST : QA_NATIVE_HOST_MULTICAST,
        .payload = {bytes, qa_net_writer_size(&writer)}, .origin = origin, .client = source->client,
        .destination = no_phs || source->attenuation == 0 ? 0 : 1,
        .flags = source->local ? source->flags : 0, .reliable = reliable, .positioned = true};
    qa_application_protocol_reference reference = {.actor = source->actor, .packed_sound = true};
    if (source->actor.registry)
        reference.offset = 2u + ((bytes[1] & 32u) ? 2u : 1u) +
            ((bytes[1] & 1u) != 0) + ((bytes[1] & 2u) != 0) + ((bytes[1] & 16u) != 0);
    qa_application_protocol_event publication = {.recipient = packet.client, .origin = origin,
        .payload = packet.payload, .destination = packet.destination, .reliable = reliable,
        .multicast = !source->local, .references = source->actor.registry ? &reference : NULL,
        .reference_count = source->actor.registry ? 1u : 0u};
    qa_application_q2_protocol_delivery delivery;
    if (!application_native_q2_message_capture(engine, &packet, &delivery, error)) return false;
    bool duplicate = false;
    bool keyed = delivery.dupe_key && delivery.audience.captured && delivery.audience.count;
    if (keyed && !qa_application_network_q2_unicast(engine->provider->application,
        engine->provider->owner, source->client, delivery.dupe_key, false, &duplicate, error)) {
        application_native_q2_delivery_dispose(&delivery.audience); return false;
    }
    if (duplicate) { application_native_q2_delivery_dispose(&delivery.audience); return true; }
    qa_actor_id *recipients = delivery.audience.count ? malloc(delivery.audience.count * sizeof(*recipients)) : NULL;
    if (delivery.audience.count && !recipients) {
        application_native_q2_delivery_dispose(&delivery.audience);
        return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q2 sound recipients");
    }
    for (size_t i = 0; i < delivery.audience.count; ++i) recipients[i] = delivery.audience.recipients[i].actor;
    named.origin = origin; named.positioned = positioned; named.reliable = reliable;
    named.channel = event.data.sound.channel; named.recipients = recipients;
    named.recipient_count = delivery.audience.count; named.audience_captured = delivery.audience.captured;
    application_native_q2_protocol_resources resources = {0};
    bool ok = application_native_q2_protocol_resources_capture(engine, publication.payload,
        publication.references, publication.reference_count, &resources, error);
    if (ok) {
        publication.resources = resources.rows; publication.resource_count = resources.count;
        publication.references = resources.references; publication.reference_count = resources.reference_count;
        ok = application_emit_q2_protocol(engine->provider, &publication, &delivery, error);
    }
    if (ok && keyed) ok = qa_application_network_q2_unicast(engine->provider->application,
        engine->provider->owner, source->client, delivery.dupe_key, true, &duplicate, error);
    if (ok && named.audience_captured && engine->platform.sound)
        ok = engine->platform.sound(engine->platform.context, &named, error);
    application_native_q2_protocol_resources_dispose(&resources);
    free(recipients); application_native_q2_delivery_dispose(&delivery.audience);
    return ok;
}

static uint32_t server_frame(void *opaque)
{
    return (uint32_t)application_native_q2_stages_frame(opaque);
}

static uint64_t source_frame(void *opaque)
{
    return application_native_q2_stages_frame(opaque);
}

static bool entity_number(void *opaque, qa_actor_id actor, uint32_t *out, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    return qa_application_network_q2_entity_number(engine->provider->application,
        engine->provider->owner, actor, out, error);
}

static bool hud_view(void *opaque, uint32_t seat, qa_native_host_q2_hud_view *out, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    return engine->platform.hud_view
        ? engine->platform.hud_view(engine->platform.context, seat, out, error)
        : application_fail(error, QA_ERROR_UNSUPPORTED, "Native Q2 HUD viewport owner is absent");
}

qa_native_host_engine_services application_native_q2_services(struct application_native_q2 *engine)
{
    return (qa_native_host_engine_services){.context = engine, .print = print,
        .configstring_get = config_get, .configstring_set = config_set, .resource_index = resource,
        .command = command, .message = message, .sound = sound, .server_frame = server_frame,
        .source_frame = source_frame,
        .entity_number = entity_number,
        .checkpoint = application_native_q2_capture_engine, .restore = application_native_q2_restore_engine,
        .content_files = engine->provider->launch->content, .cvars = engine->cvars,
        .hud_view = hud_view,
        .source_before = engine->callbacks ? application_native_q2_callbacks_source_before : NULL,
        .source_import = engine->callbacks ? application_native_q2_callbacks_import : NULL,
        .source_after = engine->callbacks ? application_native_q2_callbacks_source_after : NULL};
}

static bool movement_prepare(void *opaque, qa_native_host *host, qa_native_address record,
    qa_movement_input *input, qa_error *error)
{
    (void)record;
    struct application_native_q2 *engine = opaque;
    qa_application *app = engine->provider->application;
    uint32_t slot = engine->current_client;
    if (!slot || slot >= 257 || !engine->clients[slot].connected ||
        !engine->clients[slot].begun || host != engine->provider->state.native.host)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 Pmove requires the active admitted client call");
    qa_actor_id actor = engine->clients[slot].actor;
    const application_control_external_stage *stage = engine->movement_stage;
    const application_native_q2_input_stage *raw = engine->input_stage;
    if (raw && (!qa_actor_id_equal(raw->actor, actor) || !raw->current ||
        !raw->current(raw->context, actor)))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 Pmove lost its raw Source command stage");
    if (stage && (stage->application != app || !qa_actor_id_equal(stage->actor, actor) ||
        !stage->current || !stage->current(stage)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 Pmove lost its retained source turn");
    const application_control_context *control_source = !raw && !stage
        ? application_control_frame_current(app, actor) : NULL;
    if (!raw && !stage && !control_source)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 Pmove has no actual source control admission");
    if (!qa_actors_get(qa_session_actors(app->session), actor) ||
        (!raw && application_provider_for(app, actor, QA_ROLE_MOVEMENT, NULL) != engine->provider))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native Q2 Pmove cannot replace another selected movement owner");
    qa_body_state body; qa_combat_state combat;
    if (!qa_world_body_read(engine->world, actor, &body, error) ||
        !qa_combat_read_traits(app->combat, actor, &combat, error)) return false;
    input->actor = actor;
    input->command.sequence = engine->current_command_sequence;
    input->time_ns = raw ? raw->time_ns : stage ? application_control_time(&stage->source)
        : application_control_time(control_source);
    input->elapsed_ns = (uint64_t)input->command.milliseconds * UINT64_C(1000000);
    input->environment.health = combat.health;
    const qa_cvar_view *air = qa_cvars_find(engine->cvars, "sv_airaccelerate");
    if (!air || !isfinite(air->number))
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 Pmove lost its physical Source air acceleration");
    input->profile.data.q2.air_accelerate = (float)air->number;
    application_provider *character = application_provider_for(app, actor, QA_ROLE_CHARACTER, NULL);
    if (character != engine->provider) {
        input->environment.has_body_bounds = true; input->environment.body_bounds = body.bounds;
        qa_application_control_view control;
        if (qa_application_control_read(app, actor, &control)) {
            input->standing.bounds = app->controls[actor.slot].standing_bounds;
            input->standing.view_height = control.view_height;
            input->environment.flight = control.flight;
            input->environment.gravity_multiplier = control.gravity_multiplier;
        }
    }
    return true;
}

static bool movement_execute(void *opaque, qa_native_host *host, qa_native_address record,
    const qa_movement_input *input, const qa_movement_services *services,
    qa_movement_result *result, qa_error *error)
{
    (void)record;
    struct application_native_q2 *engine = opaque;
    qa_application *app = engine->provider->application;
    const application_native_q2_input_stage *stage = engine->input_stage;
    if (host != engine->provider->state.native.host)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 movement execution has another physical host");
    if (!stage || application_provider_for(app, input->actor, QA_ROLE_MOVEMENT, NULL) == engine->provider)
        return qa_movement_move(input, services, result, error);
    if (!qa_actor_id_equal(stage->actor,input->actor) || !stage->current(stage->context,input->actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 selected movement lost its raw Source stage");
    if (!stage->move(stage->context,input,services,result,error)) return false;
    if (!qa_actor_id_equal(result->actor,input->actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 selected movement returned another Source actor");
    if(result->status==QA_MOVEMENT_ACTOR_REMOVED && !qa_actors_get(qa_session_actors(app->session),input->actor)) return true;
    if (!stage->current(stage->context,input->actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 selected movement returned another Source actor or stage");
    qa_vec3 origin=qa_movement_origin(&result->state), velocity=qa_movement_velocity(&result->state);
    /* Source flags, gravity, timers and delta angles remain its own namespace. */
    qa_movement_state physical=input->state;
    if (!qa_movement_set_origin(&physical,origin,error) ||
        !qa_movement_set_velocity(&physical,velocity,error)) return false;
    if (physical.kind==QA_MOVEMENT_Q2_RERELEASE) physical.data.q2r.view_height=result->view_height;
    result->state=physical;
    return true;
}

static bool movement_commit(void *opaque, qa_native_host *host, qa_native_address record,
    const qa_movement_result *result, qa_error *error)
{
    (void)record;
    struct application_native_q2 *engine = opaque;
    qa_application *app = engine->provider->application;
    uint32_t slot = engine->current_client;
    if (host != engine->provider->state.native.host || !slot || slot >= 257 ||
        !qa_actor_id_equal(result->actor, engine->clients[slot].actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 Pmove completion differs from its active source client");
    if (result->status == QA_MOVEMENT_ACTOR_REMOVED ||
        !qa_actors_get(qa_session_actors(app->session), result->actor)) return true;
    const application_native_q2_input_stage *raw=engine->input_stage;
    if (raw && (!qa_actor_id_equal(raw->actor,result->actor) || !raw->current(raw->context,result->actor)))
        return application_fail(error,QA_ERROR_NOT_FOUND,"Native Q2 Pmove completion lost its raw Source stage");
    if (raw && application_provider_for(app,result->actor,QA_ROLE_MOVEMENT,NULL)!=engine->provider)
        return true;
    const application_control_external_stage *stage = engine->movement_stage;
    if (stage && (stage->application != app || !qa_actor_id_equal(stage->actor, result->actor) ||
        !stage->current || !stage->current(stage)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 Pmove completion lost its retained source turn");
    if (result->actor.slot >= app->control_capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 Pmove has no shared control projection");
    application_control_record *control = &app->controls[result->actor.slot];
    if (!control->active || !qa_actor_id_equal(control->actor, result->actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 Pmove control generation differs");
    control->state = result->state; control->bounds = result->bounds;
    control->ground = result->ground; control->water_level = result->water_level;
    control->water_type = result->water_type; control->view_height = result->view_height;
    control->view_angles = result->view_angles; control->view_offset = result->view_offset;
    if (raw) {
        qa_movement_result copy=*result;
        copy.contacts=NULL;
        copy.contact_capacity=copy.contact_count;
        if (result->contact_count) {
            if (result->contact_count>SIZE_MAX/sizeof(*result->contacts))
                return application_fail(error,QA_ERROR_FORMAT,"Native Q2 Pmove contact count exceeds its result storage");
            copy.contacts=malloc(result->contact_count*sizeof(*copy.contacts));
            if (!copy.contacts) return application_fail(error,QA_ERROR_MEMORY,"Retaining actual native Q2 Pmove contacts");
            memcpy(copy.contacts,result->contacts,result->contact_count*sizeof(*copy.contacts));
        }
        qa_movement_result_free(&control->result); control->result=copy;
    }
    return true;
}

qa_native_host_movement_services application_native_q2_movement_services(struct application_native_q2 *engine)
{
    return (qa_native_host_movement_services){.context = engine, .prepare = movement_prepare,
        .execute = movement_execute, .commit = movement_commit};
}

bool application_native_q2_project(void *opaque, qa_native_host *host, uint32_t slot,
    qa_native_address address, qa_actor_id *out, bool *present, qa_error *error)
{
    (void)host; (void)address;
    struct application_native_q2 *engine = opaque;
    *out = (qa_actor_id){0}; *present = false;
    if (slot > 0 && slot < 257 && engine->clients[slot].actor.registry) {
        *out = engine->clients[slot].actor;
        if (!qa_actors_get(qa_session_actors(engine->provider->application->session), *out))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 projected client generation retired");
        *present = true;
    }
    return true;
}

bool application_native_q2_address(void *opaque, qa_native_host *host, qa_actor_id actor,
    qa_native_address *out, bool *present, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    *out = 0; *present = false;
    if (!qa_actors_get(qa_session_actors(engine->provider->application->session), actor)) return true;
    for (uint32_t i = 1; i < 257; ++i)
        if (qa_actor_id_equal(engine->clients[i].actor, actor)) {
            if (!qa_native_entity_address(qa_native_host_instance(host), i, out, error)) return false;
            *present = true; return true;
        }
    return true;
}

bool application_native_q2_bind(void *opaque, qa_native_host *host, uint32_t slot,
    qa_actor_id actor, qa_error *error)
{
    (void)host;
    struct application_native_q2 *engine = opaque;
    return qa_session_bind_execution(engine->provider->application->session, actor,
        engine->provider->owner, error) &&
        (application_native_q2_source_actors_declared(engine) ?
         application_native_q2_source_actors_admit(engine, slot, actor, error) :
         application_native_q2_combat_admit(engine, slot, actor, true, error));
}
