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
    if (!isfinite(value)) return 0;
    double bits = fmod(trunc((double)value), 4294967296.0);
    if (bits < 0) bits += 4294967296.0;
    return (uint32_t)bits;
}
static bool eye_cluster(struct application_qc_state *engine, int32_t reference,
                         int32_t *out, qa_error *error)
{
    qa_vec3 origin, offset; qa_collision_leaf leaf;
    if (!vector_field(engine, reference, "origin", &origin, error) ||
        !vector_field(engine, reference, "view_ofs", &offset, error) ||
        !qa_collision_point_leaf(qa_world_geometry(engine->world), qa_vec_add(origin, offset), &leaf, error)) return false;
    *out = leaf.cluster; return true;
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
    return !*alive || application_qc_reference(engine, client->actor, reference, error);
}
static bool check_client(struct application_qc_state *engine, qa_qc_instance *vm, qa_error *error)
{
    const qa_qc_definition *time = qa_qc_program_find_global(engine->provider->state.qc.program, "time");
    float now;
    if (time == NULL || time->type != QA_QC_FLOAT || !qa_qc_global_float(vm, time->offset, &now, error)) return false;
    if (!isfinite(now)) return application_fail(error, QA_ERROR_FORMAT, "Invalid QuakeC checkclient clock");
    if ((double)now - engine->check_time >= 0.1) {
        uint32_t previous = engine->check_slot ? engine->check_slot : 1;
        uint32_t slot = previous == engine->max_clients ? 1 : previous + 1;
        for (;;) {
            int32_t reference; float health = 0, flags = 0; bool alive;
            if (!check_client_reference(engine, slot, &reference, &alive, error)) return false;
            if (alive && (!application_qc_float(engine, reference, "health", &health, error) ||
                !application_qc_float(engine, reference, "flags", &flags, error))) return false;
            if (slot == previous || (alive && !(health <= 0) && !(source_flags(flags) & 128u))) break;
            slot = slot == engine->max_clients ? 1 : slot + 1;
        }
        engine->check_slot = slot; engine->check_time = now; engine->check_cluster = -1;
        int32_t reference; bool alive;
        if (!check_client_reference(engine, slot, &reference, &alive, error) ||
            ((alive || (!engine->provider->state.qc.qualified && engine->profile == QA_QC_QUAKEWORLD)) &&
             !eye_cluster(engine, reference, &engine->check_cluster, error))) return false;
    }
    if (engine->check_slot == 0 || engine->check_cluster < 0) return qa_qc_return_int(vm, 0, error);
    int32_t reference, observer, cluster; float health; bool visible, alive;
    if (!check_client_reference(engine, engine->check_slot, &reference, &alive, error)) return false;
    if (!alive) return qa_qc_return_int(vm, 0, error);
    if (!application_qc_float(engine, reference, "health", &health, error) ||
        !global_reference(engine, "self", &observer, error) || !eye_cluster(engine, observer, &cluster, error) ||
        !qa_collision_cluster_visible(qa_world_geometry(engine->world), engine->check_cluster, cluster, false, &visible, error)) return false;
    return qa_qc_return_int(vm, !(health <= 0) && visible ? reference : 0, error);
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
    const qa_cvar_view *threshold = qa_cvars_find(engine->cvars, "sv_aim");
    const qa_cvar_view *teamplay = qa_cvars_find(engine->cvars, "teamplay");
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
        if (!qa_format_fixed(def->type == QA_QC_FLOAT ? scalar : value, 6, number, sizeof(number), error)) return false;
        snprintf(line, sizeof(line), "%s: %s\n", def->name, number);
        if (!emit_text(engine, (qa_actor_id){0}, line, 1, error)) return false;
    }
    return true;
}
static bool static_entity(struct application_qc_state *engine, qa_qc_instance *vm, qa_error *error)
{
    int32_t reference; qa_actor_id actor; qa_vec3 origin, angles; float model, frame, color, skin;
    if (!qa_qc_arg_int(vm, 0, &reference, error) || !qa_qc_reference_actor(vm, reference, &actor, error) ||
        !vector_field(engine, reference, "origin", &origin, error) || !vector_field(engine, reference, "angles", &angles, error) ||
        !application_qc_float(engine, reference, "modelindex", &model, error) ||
        !application_qc_float(engine, reference, "frame", &frame, error) ||
        !application_qc_float(engine, reference, "colormap", &color, error) ||
        !application_qc_float(engine, reference, "skin", &skin, error)) return false;
    if (!isfinite(model) || model < 0 || model > 255 || !isfinite(frame) || frame < 0 || frame > 255 ||
        !isfinite(color) || color < 0 || color > 255 || !isfinite(skin) || skin < 0 || skin > 255)
        return application_fail(error, QA_ERROR_FORMAT, "QuakeC static model exceeds source protocol range");
    uint8_t bytes[64]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    qa_q1_entity entity = {.model = (uint32_t)model, .frame = (uint32_t)frame,
        .colormap = (uint32_t)color, .skin = (uint32_t)skin,
        .origin = {origin.x, origin.y, origin.z}, .angles = {angles.x, angles.y, angles.z}};
    bool ok;
    if (engine->profile == QA_QC_QUAKEWORLD) {
        qa_qw_service service = {.kind = QA_QW_STATIC, .data.baseline = entity};
        ok = qa_qw_service_write(&writer, engine->protocol, &service, NULL);
    } else {
        qa_nq_message message = {.op = QA_NQ_STATIC, .data.entity = entity};
        ok = qa_nq_write(&writer, engine->protocol, (qa_nq_options){.standard_quake = true}, &message, NULL, 0);
    }
    qa_application_protocol_event event = {.payload = {bytes, qa_net_writer_size(&writer)}, .destination = 3, .signon = true};
    return ok && application_emit_protocol(engine->provider, &event, error) &&
           qa_actors_get(qa_session_actors(engine->services.session), actor) != NULL && qa_qc_remove_entity(vm, reference, error);
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
        uint8_t *bytes = malloc(strlen(text) + 2);
        if (bytes == NULL) return application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC client command");
        bytes[0] = 9; memcpy(bytes + 1, text, strlen(text) + 1);
        qa_application_protocol_event event = {.recipient = actor, .payload = {bytes, strlen(text) + 2}, .destination = 1, .reliable = true};
        bool ok = application_emit_protocol(engine->provider, &event, error); free(bytes); return ok;
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
        char *copy = malloc(length + 1); uint8_t *bytes = malloc(length + 3);
        if (copy == NULL || bytes == NULL) { free(copy); free(bytes); return application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC lightstyle"); }
        memcpy(copy, pattern, length + 1); bytes[0] = 12; bytes[1] = (uint8_t)style; memcpy(bytes + 2, pattern, length + 1);
        qa_application_protocol_event event = {.payload = {bytes, length + 3}, .destination = engine->loading ? 3 : 2,
                                               .reliable = !engine->loading, .signon = engine->loading};
        bool ok = application_emit_protocol(engine->provider, &event, error) &&
            qa_builtin_emit(&engine->services, &light, error);
        free(bytes);
        if (ok) { free(engine->lightstyles[(uint32_t)style]); engine->lightstyles[(uint32_t)style] = copy; } else free(copy);
        return ok;
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
