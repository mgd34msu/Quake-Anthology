#include "guest_qc_internal.h"
#include "map_travel_private.h"
#include "qa/application_network_qw.h"
#include "guest_qc_rerelease.h"
#include <stdio.h>

static bool vector_field(struct application_qc_state *engine, int32_t reference,
                          const char *name, qa_vec3 *out, qa_error *error)
{
    const qa_qc_definition *field = application_qc_field(engine, name, QA_QC_VECTOR, error);
    return field != NULL && qa_qc_entity_vector(engine->provider->state.qc.instance, reference, field->offset, out, error);
}
static bool info_key(struct application_qc_state *engine, qa_qc_instance *vm, qa_error *error)
{
    int32_t reference; const char *key, *value = "";
    if (!qa_qc_arg_int(vm, 0, &reference, error) || !qa_qc_arg_string(vm, 1, &key, error)) return false;
    qa_qc_entity_layout layout = qa_qc_default_entity_layout(engine->provider->state.qc.program, engine->profile);
    if (reference < 0 || !layout.stride_bytes || (uint32_t)reference % layout.stride_bytes)
        return application_fail(error, QA_ERROR_FORMAT, "QuakeC infokey has an invalid physical entity reference");
    uint32_t slot = (uint32_t)reference / layout.stride_bytes;
    int32_t actual_reference;
    if (!qa_qc_slot_reference(vm, slot, &actual_reference, error)) return false;
    if (actual_reference != reference)
        return application_fail(error, QA_ERROR_FORMAT, "QuakeC infokey reference differs from its actual source layout");
    qa_qw_info info = {0};
    if (slot == 0) {
        const qa_cvar_view *cvar = qa_cvars_find(engine->cvars, key);
        if (cvar) value = cvar->value;
    } else if (slot <= engine->max_clients) {
        const application_qc_client *client = &engine->clients[slot];
        if (client->connected) {
            qa_qc_slot_binding binding;
            const qa_actor_record *record = qa_actors_get(qa_session_actors(engine->services.session), client->actor);
            if (!qa_qc_slot(vm, slot, &binding) || !record ||
                binding.owner != record->owner || binding.source_slot != (record->has_source ? record->source_slot : 0) ||
                (binding.kind != QA_QC_SLOT_BORROWED && (binding.kind != QA_QC_SLOT_OWNED ||
                 engine->provider->state.qc.qualified || engine->profile != QA_QC_QUAKEWORLD ||
                 record->owner != engine->provider->owner || !record->has_source || record->source_slot != slot)) ||
                !qa_actor_id_equal(binding.actor, client->actor))
                return application_fail(error, QA_ERROR_FORMAT, "QuakeC infokey client differs from its source binding");
            const char *raw;
            if (!qa_application_network_qw_userinfo_read(engine->provider->application, client->actor, &raw, error) ||
                !qa_qw_info_parse(raw, &info, error)) return false;
            const char *entry = qa_qw_info_get(&info, key);
            if (entry) value = entry;
        }
    }
    size_t length = strlen(key);
    if (length > SIZE_MAX - 48) {
        qa_qw_info_free(&info);
        return application_fail(error, QA_ERROR_MEMORY, "QuakeC infokey engine string name is too long");
    }
    char *name = malloc(length + 48);
    if (!name) {
        qa_qw_info_free(&info);
        return application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC infokey engine string name");
    }
    bool qualified = engine->provider->state.qc.qualified != NULL;
    snprintf(name, length + 48, "%s:%u:%s", qualified ? "mod-infokey" : "qw-infokey",
        qualified ? (uint32_t)reference : slot, key);
    size_t capacity = 1024;
    if (qualified && strlen(value) >= capacity) capacity = strlen(value) + 1;
    int32_t string;
    bool ok = qa_qc_engine_string(vm, name, value, capacity, &string, error) &&
        qa_qc_return_int(vm, string, error);
    free(name); qa_qw_info_free(&info);
    return ok;
}
static bool global_reference(struct application_qc_state *engine, const char *name,
                              int32_t *out, qa_error *error)
{
    const qa_qc_definition *global = qa_qc_program_find_global(engine->provider->state.qc.program, name);
    if (global == NULL || global->type != QA_QC_ENTITY)
        return application_fail(error, QA_ERROR_FORMAT, "Missing QuakeC entity global");
    return qa_qc_global_int(engine->provider->state.qc.instance, global->offset, out, error);
}
static uint32_t source_flags(float value)
{
    return (uint32_t)qa_source_float_to_i32(value);
}
static bool check_client_eye_reference(struct application_qc_state *engine, int32_t reference,
    qa_vec3 *out, qa_error *error) {
    qa_vec3 origin, offset;
    if (!vector_field(engine, reference, "origin", &origin, error) ||
        !vector_field(engine, reference, "view_ofs", &offset, error)) return false;
    *out = qa_vec_add(origin, offset);
    return true;
}
static bool check_client_reference(struct application_qc_state *engine, uint32_t slot,
    int32_t *reference, bool *alive, qa_error *error)
{
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    if (!engine->provider->state.qc.qualified && engine->profile == QA_QC_QUAKEWORLD) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(vm, slot, &binding))
            return application_fail(error, QA_ERROR_FORMAT, "QW checkclient has no reserved physical source actor");
        if (binding.kind == QA_QC_SLOT_FREE) {
            *alive = false;
            return qa_qc_slot_reference(vm, slot, reference, error);
        }
        if (binding.kind != QA_QC_SLOT_OWNED && binding.kind != QA_QC_SLOT_BORROWED)
            return application_fail(error, QA_ERROR_FORMAT, "QW checkclient has an invalid reserved source binding");
        const qa_actor_record *record = qa_actors_get(qa_session_actors(engine->services.session), binding.actor);
        if (!record || binding.owner != record->owner ||
            binding.source_slot != (record->has_source ? record->source_slot : 0) ||
            (binding.kind == QA_QC_SLOT_OWNED && (record->owner != engine->provider->owner ||
             !record->has_source || record->source_slot != slot)))
            return application_fail(error, QA_ERROR_FORMAT, "QW checkclient reserved actor differs from its source generation");
        *alive = true;
        return qa_qc_slot_reference(vm, slot, reference, error);
    }
    application_qc_client *client = &engine->clients[slot];
    *alive = client->connected && qa_actors_get(qa_session_actors(engine->services.session), client->actor) != NULL;
    return *alive ? application_qc_reference(engine, client->actor, reference, error) :
        qa_qc_slot_reference(vm, slot, reference, error);
}
static bool check_client_row(void *opaque, uint32_t slot, bool selection,
    qa_builtin_check_client_row *out, qa_error *error) {
    struct application_qc_state *engine = opaque;
    int32_t reference;
    *out = (qa_builtin_check_client_row){0};
    if (!check_client_reference(engine, slot, &reference, &out->present, error)) return false;
    if (!out->present) return true;
    if (!application_qc_float(engine, reference, "health", &out->health, error)) return false;
    if (selection && !(out->health <= 0)) {
        float flags;
        if (!application_qc_float(engine, reference, "flags", &flags, error)) return false;
        out->no_target = (source_flags(flags) & 128u) != 0;
    }
    return true;
}
static bool check_client_eye(void *opaque, uint32_t slot, qa_vec3 *out, qa_error *error) {
    struct application_qc_state *engine = opaque;
    int32_t reference;
    bool present;
    return check_client_reference(engine, slot, &reference, &present, error) &&
        check_client_eye_reference(engine, reference, out, error);
}
static bool check_client_observer_eye(void *opaque, qa_vec3 *out, qa_error *error) {
    struct application_qc_state *engine = opaque;
    int32_t reference;
    return global_reference(engine, "self", &reference, error) &&
        check_client_eye_reference(engine, reference, out, error);
}
static bool check_client(struct application_qc_state *engine, qa_qc_instance *vm, qa_error *error)
{
    qa_builtin_check_client_query query = {.session = engine->services.session,
        .provider = engine->provider->owner, .world = engine->world,
        .capacity = engine->max_clients, .slot = &engine->check_slot,
        .time = &engine->check_time, .cluster = &engine->check_cluster,
        .context = engine, .client = check_client_row, .client_eye = check_client_eye,
        .observer_eye = check_client_observer_eye};
    uint32_t slot;
    if (!qa_builtin_check_client(&query, &slot, error)) return false;
    if (!slot) return qa_qc_return_int(vm, 0, error);
    int32_t reference;
    bool present;
    if (!check_client_reference(engine, slot, &reference, &present, error)) return false;
    if (!present) return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC checkclient lost its returned physical Source player");
    return qa_qc_return_int(vm, reference, error);
}
static bool aim_eligible(struct application_qc_state *engine, int32_t source, qa_actor_id shooter,
                          qa_actor_id actor, float teamplay, bool *eligible, qa_error *error)
{
    *eligible = false;
    if (qa_actor_id_equal(shooter, actor) || qa_actors_get(qa_session_actors(engine->services.session), actor) == NULL) return true;
    int32_t target; float damage, source_team, team;
    if (!application_qc_reference(engine, actor, &target, error) ||
        !application_qc_float(engine, target, "takedamage", &damage, error)) return false;
    if (damage != 2) return true;
    if (teamplay != 0) {
        if (!application_qc_float(engine, source, "team", &source_team, error) ||
            !application_qc_float(engine, target, "team", &team, error)) return false;
        if (source_team > 0 && source_team == team) return true;
    }
    *eligible = true; return true;
}
static bool aim_trace(struct application_qc_state *engine, qa_actor_id shooter,
                       qa_vec3 start, qa_vec3 end, qa_trace_result *out, qa_error *error)
{
    qa_trace_query query = {.start = start, .end = end, .shape = {.kind = QA_SHAPE_POINT},
                            .policy = qa_collision_default_policy(QA_COLLISION_Q1), .pass_actor = shooter};
    return qa_world_trace(engine->world, &query, out, error);
}
static bool aim(struct application_qc_state *engine, qa_qc_instance *vm, qa_error *error)
{
    int32_t reference; qa_actor_id actor; qa_body_state body;
    qa_vec3 forward;
    const qa_qc_definition *direction = qa_qc_program_find_global(engine->provider->state.qc.program, "v_forward");
    if (!qa_qc_arg_int(vm, 0, &reference, error) || !qa_qc_reference_actor(vm, reference, &actor, error) ||
        direction == NULL || direction->type != QA_QC_VECTOR || !qa_qc_global_vector(vm, direction->offset, &forward, error) ||
        !qa_world_body_read(engine->world, actor, &body, error)) return false;
    const qa_cvar_view *threshold = qa_cvars_read(engine->cvars, engine->cvar_handles.sv_aim);
    const qa_cvar_view *teamplay = qa_cvars_read(engine->cvars, engine->cvar_handles.teamplay);
    float best = threshold->number, teams = teamplay->number;
    qa_vec3 start = body.origin; start.z += 20;
    qa_trace_result trace; bool eligible;
    if (!aim_trace(engine, actor, start, qa_vec_add(start, qa_vec_scale(forward, 2048)), &trace, error)) return false;
    if (trace.hit == QA_TRACE_HIT_ACTOR) {
        if (!aim_eligible(engine, reference, actor, trace.actor, teams, &eligible, error)) return false;
        if (eligible) return qa_qc_return_vector(vm, forward, error);
    }
    /* A nested query gets its own snapshot; callbacks may reenter aim. */
    qa_builtin_actor_snapshot targets = {0};
    bool ok = qa_builtin_observations(&engine->services, &targets, error), selected = false;
    qa_vec3 selected_origin = {0};
    for (size_t i = 0; ok && i < targets.count; ++i) {
        qa_actor_id target = targets.ids[i];
        ok = aim_eligible(engine, reference, actor, target, teams, &eligible, error);
        if (!ok || !eligible) continue;
        qa_body_state target_body;
        if (!qa_world_body_read(engine->world, target, &target_body, error)) { ok = false; break; }
        qa_vec3 center = qa_vec_add(target_body.origin, qa_vec_scale(qa_vec_add(target_body.bounds.mins, target_body.bounds.maxs), 0.5f));
        float alignment = qa_vec_dot(qa_vec_normalize(qa_vec_sub(center, start)), forward);
        if (alignment < best) continue;
        ok = aim_trace(engine, actor, start, center, &trace, error);
        if (ok && trace.hit == QA_TRACE_HIT_ACTOR && qa_actor_id_equal(trace.actor, target)) {
            best = alignment; selected_origin = target_body.origin; selected = true;
        }
    }
    qa_builtin_snapshot_free(&targets);
    if (!ok) return false;
    if (selected) {
        qa_vec3 delta = qa_vec_sub(selected_origin, body.origin);
        forward = qa_vec_scale(forward, qa_vec_dot(delta, forward)); forward.z = delta.z;
        forward = qa_vec_normalize(forward);
    }
    return qa_qc_return_vector(vm, forward, error);
}
static bool emit_text(struct application_qc_state *engine, qa_actor_id actor,
                       const char *text, uint32_t flags, qa_error *error)
{
    qa_builtin_event event = {.kind = QA_BUILTIN_MESSAGE, .family = QA_GAME_Q1,
        .provider = engine->provider->owner, .actor = actor, .flags = flags, .time_ns = engine->source_time_ns};
    return qa_builtin_resource(&engine->services, text, &event.text, error) && qa_builtin_emit(&engine->services, &event, error);
}
static bool debug_entity(struct application_qc_state *engine, qa_qc_instance *vm, int32_t reference, qa_error *error)
{
    qa_qc_program_info info = qa_qc_program_describe(engine->provider->state.qc.program);
    for (uint32_t i = 0; i < info.field_count; ++i) {
        const qa_qc_definition *def = qa_qc_program_field(engine->provider->state.qc.program, i);
        if (def == NULL || !*def->name || def->type == QA_QC_VOID) continue;
        int32_t value;
        if (!qa_qc_entity_int(vm, reference, def->offset, &value, error)) return false;
        if (!value) continue;
        char number[64], line[256]; float scalar;
        memcpy(&scalar, &value, sizeof(scalar));
        if (!qa_format_fixed(def->type == QA_QC_FLOAT ? (double)scalar : (double)value, 6, number, sizeof(number), error)) return false;
        snprintf(line, sizeof(line), "%s: %s\n", def->name, number);
        if (!emit_text(engine, (qa_actor_id){0}, line, 1, error)) return false;
    }
    return true;
}
static bool static_entity(struct application_qc_state *engine, qa_qc_instance *vm, qa_error *error)
{
    int32_t reference, model_string; qa_actor_id actor; qa_vec3 origin, angles; float frame, color, skin;
    const qa_qc_definition *model_field = application_qc_field(engine, "model", QA_QC_STRING, error);
    const char *model_path; qa_qc_game_resource model;
    if (!qa_qc_arg_int(vm, 0, &reference, error) || !qa_qc_reference_actor(vm, reference, &actor, error) ||
        !vector_field(engine, reference, "origin", &origin, error) || !vector_field(engine, reference, "angles", &angles, error) ||
        !model_field || !qa_qc_entity_int(vm, reference, model_field->offset, &model_string, error) ||
        !qa_qc_string(vm, model_string, &model_path, error) ||
        !application_qc_resource_lookup(engine, QA_QC_RESOURCE_MODEL, model_path, false, &model, error) ||
        !application_qc_float(engine, reference, "frame", &frame, error) ||
        !application_qc_float(engine, reference, "colormap", &color, error) ||
        !application_qc_float(engine, reference, "skin", &skin, error)) return false;
    if (model.index > 255 || !isfinite(frame) || frame < 0 || frame > 255 ||
        !isfinite(color) || color < 0 || color > 255 || !isfinite(skin) || skin < 0 || skin > 255)
        return application_fail(error, QA_ERROR_FORMAT, "QuakeC static model exceeds source protocol range");
    qa_q1_entity entity; qa_q1_entity_init(&entity);
    entity.model = model.index; entity.frame = (uint32_t)frame;
    entity.colormap = (uint32_t)color; entity.skin = (uint32_t)skin;
    entity.origin[0] = origin.x; entity.origin[1] = origin.y; entity.origin[2] = origin.z;
    entity.angles[0] = angles.x; entity.angles[1] = angles.y; entity.angles[2] = angles.z;
    qa_qw_service service = {.kind = QA_QW_STATIC, .data.baseline = entity};
    qa_nq_message message = {.op = QA_NQ_STATIC, .data.entity = entity};
    qa_application_protocol_event event = {.encoding_protocol = engine->protocol,
        .standard_quake = true, .destination = 3, .signon = true};
    if (engine->profile == QA_QC_QUAKEWORLD) {
        event.qw = &service;
    } else {
        event.nq = &message;
    }
    application_event_admission admission = application_emit_protocol(engine->provider, &event, error);
    if (admission != APPLICATION_EVENT_APPENDED) return admission != APPLICATION_EVENT_FAILED;
    return qa_actors_get(qa_session_actors(engine->services.session), actor) != NULL &&
        qa_qc_remove_entity(vm, reference, error);
}
bool application_qc_import(void *opaque, qa_qc_instance *vm, qa_qc_builtin builtin,
                            const char *name, qa_error *error)
{
    struct application_qc_state *engine = opaque; (void)name;
    if (vm != engine->provider->state.qc.instance)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC import belongs to another provider");
    if (builtin >= QA_QC_BUILTIN_WRITEBYTE && builtin <= QA_QC_BUILTIN_WRITEENTITY)
        return application_qc_write_message(engine, vm, builtin, error);
    if (builtin>=QA_QC_BUILTIN_SETCOLOR && builtin<=QA_QC_BUILTIN_EX_CLEARPROMPT)
        return application_qc_rerelease_import(engine,vm,builtin,error);
    switch (builtin) {
    case QA_QC_BUILTIN_CHECKCLIENT: return check_client(engine, vm, error);
    case QA_QC_BUILTIN_AIM: return aim(engine, vm, error);
    case QA_QC_BUILTIN_MULTICAST: return application_qc_multicast(engine, vm, error);
    case QA_QC_BUILTIN_MAKESTATIC: return static_entity(engine, vm, error);
    case QA_QC_BUILTIN_STUFFCMD: {
        int32_t reference; qa_actor_id actor; const char *text;
        if (!qa_qc_arg_int(vm, 0, &reference, error) || !qa_qc_reference_actor(vm, reference, &actor, error) ||
            !qa_qc_arg_string(vm, 1, &text, error)) return false;
        bool connected = false;
        for (uint32_t i = 1; i <= engine->max_clients; ++i)
            if (engine->clients[i].connected && qa_actor_id_equal(actor, engine->clients[i].actor)) connected = true;
        if (!connected) return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC stuffcmd target is not a connected client");
        qa_nq_message message = {.op = QA_NQ_STUFFTEXT, .data.text = text};
        qa_qw_service service = {.kind = QA_QW_STUFFTEXT, .data.text.value = text};
        qa_application_protocol_event event = {.recipient = actor,
            .encoding_protocol = engine->protocol, .standard_quake = true,
            .destination = 1, .reliable = true};
        if (engine->profile == QA_QC_QUAKEWORLD) event.qw = &service;
        else event.nq = &message;
        return application_emit_protocol(engine->provider, &event, error);
    }
    case QA_QC_BUILTIN_LIGHTSTYLE: {
        float style; const char *pattern;
        if (!qa_qc_arg_float(vm, 0, &style, error) || !qa_qc_arg_string(vm, 1, &pattern, error)) return false;
        if (!isfinite(style) || style < 0 || style >= 64)
            return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC lightstyle outside source table");
        qa_builtin_event light = {.kind = QA_BUILTIN_LIGHT,
            .family = QA_GAME_Q1, .provider = engine->provider->owner,
            .code = (int32_t)style, .time_ns = engine->source_time_ns};
        if (!qa_builtin_resource(&engine->services, pattern, &light.resource, error))
            return false;
        size_t length = strlen(pattern);
        char *copy = malloc(length + 1);
        if (copy == NULL) return application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC lightstyle");
        memcpy(copy, pattern, length + 1);
        const char *prior = engine->lightstyles[(uint32_t)style];
        if (!prior || strcmp(prior, copy)) ++engine->lightstyle_revision;
        free(engine->lightstyles[(uint32_t)style]); engine->lightstyles[(uint32_t)style] = copy;
        return qa_builtin_emit(&engine->services, &light, error);
    }
    case QA_QC_BUILTIN_CHANGELEVEL: {
        const char *map; int32_t self; qa_actor_id actor = {0};
        if (!qa_qc_arg_string(vm, 0, &map, error) || !global_reference(engine, "self", &self, error)) return false;
        if (self != 0 && !qa_qc_reference_actor(vm, self, &actor, error)) return false;
        qa_application_travel_request request = {.provider = engine->provider->owner, .cause = actor,
            .expression = map, .carry_players = true};
        return application_source_queue_travel(engine->provider->application, &request, error);
    }
    case QA_QC_BUILTIN_SETSPAWNPARMS: {
        int32_t reference; qa_actor_id actor;
        if (!qa_qc_arg_int(vm, 0, &reference, error) || !qa_qc_reference_actor(vm, reference, &actor, error)) return false;
        application_qc_client *client = NULL;
        for (uint32_t i = 1; i <= engine->max_clients; ++i)
            if (engine->clients[i].connected && qa_actor_id_equal(actor, engine->clients[i].actor)) { client = &engine->clients[i]; break; }
        if (client == NULL) return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC spawn parms require a client");
        for (unsigned i = 0; i < 16; ++i) {
            char key[16]; snprintf(key, sizeof(key), "parm%u", i + 1);
            const qa_qc_definition *def = qa_qc_program_find_global(engine->provider->state.qc.program, key);
            if (def == NULL || def->type != QA_QC_FLOAT || !qa_qc_set_global_float(vm, def->offset, client->parms[i], error)) return false;
        }
        return true;
    }
    case QA_QC_BUILTIN_EPRINT: { int32_t entity; return qa_qc_arg_int(vm, 0, &entity, error) && debug_entity(engine, vm, entity, error); }
    case QA_QC_BUILTIN_COREDUMP:
        for (uint32_t i = 0; i < qa_qc_entity_count(vm); ++i) {
            qa_qc_slot_binding binding;
            if (!qa_qc_slot(vm, i, &binding) || binding.kind == QA_QC_SLOT_FREE) continue;
            qa_qc_entity_layout layout = qa_qc_default_entity_layout(engine->provider->state.qc.program, engine->profile);
            if (!debug_entity(engine, vm, (int32_t)(i * layout.stride_bytes), error)) return false;
        }
        return true;
    case QA_QC_BUILTIN_LOGFRAG: {
        int32_t killer, victim; qa_actor_id first, second;
        if (!qa_qc_arg_int(vm, 0, &killer, error) || !qa_qc_arg_int(vm, 1, &victim, error) ||
            !qa_qc_reference_actor(vm, killer, &first, error) || !qa_qc_reference_actor(vm, victim, &second, error)) return false;
        qa_builtin_event event = {.kind = QA_BUILTIN_MESSAGE, .family = QA_GAME_Q1, .code = 79, .flags = 1,
            .provider = engine->provider->owner, .actor = second, .other = first, .time_ns = engine->source_time_ns};
        return qa_builtin_emit(&engine->services, &event, error);
    }
    case QA_QC_BUILTIN_INFOKEY: return info_key(engine, vm, error);
    default: return application_fail(error, QA_ERROR_UNSUPPORTED, "QuakeC engine extension has no concrete application owner");
    }
}
