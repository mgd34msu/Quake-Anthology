#include "guest_native_q2_private.h"
#include "guest_native_q2_attack.h"
#include "qa/application_native_q2_prediction.h"
#include "qa/native_host_q2_wire.h"
#include <math.h>

typedef struct prediction_layout {
    uint32_t client_pointer, client_bytes;
    uint32_t state, pending, shots, grenade, blew_up, end, priority, duck, run;
    uint8_t pointer_bytes, boolean_bytes;
} prediction_layout;

static bool word(const qa_json_document *doc, qa_json_id object, const char *name,
    uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(doc, qa_json_get(doc, object, name), &value, error)) return false;
    if (value > UINT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 prediction member exceeds uint32");
    *out = (uint32_t)value;
    return true;
}

static bool member(const qa_json_document *doc, qa_json_id layout, const char *name,
    const char *storage, uint32_t width, uint32_t extent, uint32_t *out, qa_error *error)
{
    qa_json_id fields = qa_json_get(doc, layout, "fields");
    bool found = false;
    for (size_t i = 0; i < qa_json_size(doc, fields); ++i) {
        qa_json_id field = qa_json_at(doc, fields, i);
        if (!qa_json_string_equal(doc, qa_json_get(doc, field, "name"), name)) continue;
        uint32_t count;
        if (found || !qa_json_string_equal(doc, qa_json_get(doc, field, "storage"), storage) ||
            !word(doc, field, "count", &count, error) || count != 1 ||
            !word(doc, field, "offset", out, error) || *out > extent || width > extent - *out)
            return application_fail(error, QA_ERROR_FORMAT, "Native Q2 prediction member changes its declared representation");
        found = true;
    }
    return found || application_fail(error, QA_ERROR_UNSUPPORTED,
        "Native Q2 prediction lacks its artifact-qualified client member");
}

static bool layout_read(struct application_native_q2 *engine, qa_launch_role role,
    prediction_layout *out, qa_error *error)
{
    qa_native_module_info info = qa_native_module_describe(engine->provider->state.native.module);
    const qa_json_document *doc = application_native_q2_attack_declaration_read(engine);
    if (!doc) return application_fail(error, QA_ERROR_ARGUMENT,
        "Native Q2 prediction requires its prepared original declaration index");
    qa_json_id root = qa_json_root(doc), weapons = qa_json_get(doc, root, "weapons");
    prediction_layout p = {.pointer_bytes = info.image.target.pointer_bytes,
        .boolean_bytes = engine->profile == QA_NATIVE_Q2_GAME_API3 ? 4 : 1};
    bool ok = word(doc, qa_json_get(doc, weapons, "entity"), "client", &p.client_pointer, error) &&
        word(doc, qa_json_get(doc, weapons, "client"), "byteLength", &p.client_bytes, error);
    if (ok && engine->profile == QA_NATIVE_Q2_GAME_API3) {
        qa_sha256_digest artifact;
        ok = qa_sha256_parse("8187df3fd5b4d435d8227434d3351aad2b47e546236403e52adcd4d275810c45", &artifact, error);
        if (ok && (p.pointer_bytes != 4 || p.client_pointer != 84 || p.client_bytes != 3832 ||
            !qa_sha256_equal(&artifact, &info.image.digest)))
            ok = application_fail(error, QA_ERROR_UNSUPPORTED,
                "Classic Q2 prediction requires its exact original Xatrix client layout");
        /* Artifact-matched i686 gclient_t. The animation members and newweapon
         * are declared by the original weapon/drop profiles. SDK g_local.h
         * places machinegun_shots immediately before anim_end and the two
         * grenade members after the four original powerup float timers. */
        p.state = 0xe00; p.pending = 0xddc; p.shots = 0xe78;
        p.end = 0xe7c; p.priority = 0xe80; p.duck = 0xe84; p.run = 0xe88;
        p.blew_up = 0xe9c; p.grenade = 0xea0;
    } else if (ok) {
        if (p.pointer_bytes != 8 || p.client_pointer != 120)
            ok = application_fail(error, QA_ERROR_FORMAT, "Rerelease Q2 prediction changes the actual client pointer ABI");
        qa_json_id layouts = qa_json_get(doc, qa_json_get(doc,
            qa_json_get(doc, root, "continuation"), "private"), "layouts"), client = 0;
        bool found = false;
        for (size_t i = 0; ok && i < qa_json_size(doc, layouts); ++i) {
            qa_json_id candidate = qa_json_at(doc, layouts, i);
            if (!qa_json_string_equal(doc, qa_json_get(doc, candidate, "domain"), "client")) continue;
            uint32_t extent;
            if (found || !word(doc, candidate, "byteLength", &extent, error) || extent != p.client_bytes)
                ok = application_fail(error, QA_ERROR_FORMAT, "Rerelease Q2 prediction repeats or changes its client layout");
            client = candidate; found = true;
        }
        if (ok && !found) ok = application_fail(error, QA_ERROR_UNSUPPORTED,
            "Rerelease Q2 prediction lacks the real declared private client");
        if (ok && role == QA_ROLE_ARSENAL)
            ok = member(doc, client, "weaponstate", "int32", 4, p.client_bytes, &p.state, error) &&
                member(doc, client, "newweapon", "pointer", 8, p.client_bytes, &p.pending, error) &&
                member(doc, client, "machinegun_shots", "int32", 4, p.client_bytes, &p.shots, error) &&
                member(doc, client, "grenade_time", "int64", 8, p.client_bytes, &p.grenade, error) &&
                member(doc, client, "grenade_blew_up", "bool8", 1, p.client_bytes, &p.blew_up, error);
        if (ok && role == QA_ROLE_CHARACTER)
            ok = member(doc, client, "anim_end", "int32", 4, p.client_bytes, &p.end, error) &&
                member(doc, client, "anim_priority", "int32", 4, p.client_bytes, &p.priority, error) &&
                member(doc, client, "anim_duck", "bool8", 1, p.client_bytes, &p.duck, error) &&
                member(doc, client, "anim_run", "bool8", 1, p.client_bytes, &p.run, error);
    }
    if (ok) *out = p;
    return ok;
}

static struct application_native_q2 *selected(qa_application *app, qa_actor_id actor,
    qa_launch_role role)
{
    application_provider *provider = application_provider_for(app, actor, role, "");
    struct application_native_q2 *engine = provider && provider->kind == APPLICATION_PROVIDER_NATIVE ?
        provider->state.native.q2_engine : NULL;
    return engine && (engine->profile == QA_NATIVE_Q2_GAME_API3 ||
        engine->profile == QA_NATIVE_Q2_GAME_API2023) ? engine : NULL;
}

static bool returned(qa_application *app, struct application_native_q2 *engine,
    qa_actor_id actor, qa_clock_state *clock)
{
    application_provider *p = engine->provider;
    return app->session && app->world && engine->world == app->world && p->application == app &&
        app->state == QA_APPLICATION_RUNNING && app->map_view_ready && !app->q3_round_active && !app->q3_world_restart &&
        !app->routing_snapshot && !app->destroy_requested &&
        (app->operation == APPLICATION_IDLE || app->operation == APPLICATION_ADVANCING ||
            (app->operation == APPLICATION_PERSISTING && (app->content_graph || app->capture_content_graph))) &&
        !app->frame_preparing && qa_world_idle(app->world) && qa_session_safe(app->session) &&
        !qa_session_faulted(app->session) && qa_actors_get(qa_session_actors(app->session), actor) &&
        p->constructed && p->attached && p->map_bound && !p->close_pending && p->launch && p->launch->content &&
        p->state.native.host && engine->declaration && engine->initialized && engine->map_ready &&
        !engine->shutting_down && !engine->activation_failed && application_native_q2_idle(p) &&
        !qa_native_terminal(qa_native_host_instance(p->state.native.host)) &&
        qa_session_clock(app->session, p->owner, clock) && clock->frame.provider == p->owner &&
        clock->frame.phase == QA_FRAME_EXIT && clock->frame.number == clock->frame_number &&
        clock->frame.kind == (engine->profile == QA_NATIVE_Q2_GAME_API3 ? QA_CLOCK_Q2_CLASSIC : QA_CLOCK_Q2_RERELEASE) &&
        (!clock->frame.number || (engine->frame.provider == clock->frame.provider &&
            engine->frame.kind == clock->frame.kind && engine->frame.number == clock->frame.number &&
            engine->frame.start_ns == clock->frame.start_ns && engine->frame.time_ns == clock->frame.time_ns &&
            engine->frame.elapsed_ns == clock->frame.elapsed_ns));
}

static bool read_at(qa_native_instance *instance, qa_native_address base, uint32_t offset,
    void *out, size_t count, qa_error *error)
{
    if (base > UINT64_MAX - offset) return application_fail(error, QA_ERROR_FORMAT,
        "Native Q2 prediction address overflows");
    return qa_native_read(instance, base + offset, out, count, error);
}

bool qa_application_native_q2_prediction_read(qa_application *app, qa_actor_id actor,
    qa_launch_role role, qa_application_native_q2_prediction *out, bool *found, qa_error *error)
{
    if (!app || !out || !found || (role != QA_ROLE_ARSENAL && role != QA_ROLE_CHARACTER))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 prediction requires its selected role and actor");
    *found = false;
    struct application_native_q2 *engine = selected(app, actor, role);
    if (!engine) return true;
    qa_clock_state clock;
    if (!returned(app, engine, actor, &clock)) return application_fail(error, QA_ERROR_ARGUMENT,
        "Native Q2 prediction requires its completed physical GAME and live actor");
    qa_application_native_q2_prediction value = {.actor = actor, .role = role,
        .owner = engine->provider->owner, .launch = engine->provider->launch,
        .host = engine->provider->state.native.host, .declaration = engine->declaration,
        .profile = engine->profile, .frame = clock.frame,
        .publication_generation = app->publication_generation, .map_revision = app->map_revision,
        .actors_revision = qa_actors_revision(qa_session_actors(app->session))};
    for (uint32_t slot = 1; slot < 257; ++slot) {
        const application_native_q2_client *client = engine->clients + slot;
        if (!client->connected || !client->begun || client->disconnect_started ||
            !qa_actor_id_equal(client->actor, actor)) continue;
        if (value.source_slot) return application_fail(error, QA_ERROR_FORMAT,
            "Native Q2 prediction repeats its physical client actor");
        value.source_slot = slot;
    }
    if (!value.source_slot) return application_fail(error, QA_ERROR_NOT_FOUND,
        "Selected original Q2 role has no admitted client for this actor");
    prediction_layout layout;
    if (!layout_read(engine, role, &layout, error)) return false;
    qa_native_instance *instance = qa_native_host_instance(engine->provider->state.native.host);
    qa_native_slot_binding binding;
    uint8_t bytes[8];
    if (!qa_native_slot(instance, value.source_slot, &binding, error) ||
        !qa_native_entity_address(instance, value.source_slot, &value.entity, error)) return false;
    if (binding.kind == QA_NATIVE_SLOT_FREE || !qa_actor_id_equal(binding.actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 prediction changed its physical actor binding");
    if (!read_at(instance, value.entity, layout.client_pointer, bytes, layout.pointer_bytes, error)) return false;
    value.client = layout.pointer_bytes == 4 ? qa_load_u32le(bytes) : qa_load_u64le(bytes);
    if (!value.client) return application_fail(error, QA_ERROR_FORMAT, "Native Q2 prediction has no actual client address");
    if (role == QA_ROLE_ARSENAL) {
        qa_q2_player player;
        if (!qa_native_host_q2_wire_player(engine->provider->state.native.host, value.source_slot, actor, &player, error)) return false;
        value.gun_frame = (int32_t)player.gunframe;
        if (!read_at(instance, value.client, layout.state, bytes, 4, error)) return false;
        value.weapon_state = qa_load_i32le(bytes);
        if (!read_at(instance, value.client, layout.shots, bytes, 4, error)) return false;
        value.machinegun_shots = qa_load_i32le(bytes);
        if (!read_at(instance, value.client, layout.pending, bytes, layout.pointer_bytes, error)) return false;
        qa_native_address pending = layout.pointer_bytes == 4 ? qa_load_u32le(bytes) : qa_load_u64le(bytes);
        if (!application_native_q2_attack_item_read(engine, value.source_slot, actor, pending, &value.pending_weapon, error)) return false;
        if (!read_at(instance, value.client, layout.blew_up, bytes, layout.boolean_bytes, error)) return false;
        value.grenade_blew_up = layout.boolean_bytes == 1 ? bytes[0] != 0 : qa_load_i32le(bytes) != 0;
        value.grenade_time_kind = engine->profile == QA_NATIVE_Q2_GAME_API3 ?
            QA_NATIVE_Q2_PREDICTION_SECONDS : QA_NATIVE_Q2_PREDICTION_MILLISECONDS;
        if (!read_at(instance, value.client, layout.grenade, bytes, layout.boolean_bytes == 4 ? 4 : 8, error)) return false;
        if (value.grenade_time_kind == QA_NATIVE_Q2_PREDICTION_SECONDS) {
            value.grenade_time.seconds = qa_load_f32le(bytes);
            if (!isfinite(value.grenade_time.seconds)) return application_fail(error, QA_ERROR_FORMAT,
                "Native Q2 grenade timer is not finite");
        } else value.grenade_time.milliseconds = (int64_t)qa_load_u64le(bytes);
    } else {
        qa_native_host_q2_entity entity;
        if (!qa_native_host_q2_wire_entity(engine->provider->state.native.host, value.source_slot, &entity, error)) return false;
        value.animation_frame = (int32_t)entity.state.frame;
        if (!read_at(instance, value.client, layout.end, bytes, 4, error)) return false;
        value.animation_end = qa_load_i32le(bytes);
        if (!read_at(instance, value.client, layout.priority, bytes, 4, error)) return false;
        value.animation_priority = qa_load_i32le(bytes);
        if (!read_at(instance, value.client, layout.duck, bytes, layout.boolean_bytes, error)) return false;
        value.animation_duck = layout.boolean_bytes == 1 ? bytes[0] != 0 : qa_load_i32le(bytes) != 0;
        if (!read_at(instance, value.client, layout.run, bytes, layout.boolean_bytes, error)) return false;
        value.animation_run = layout.boolean_bytes == 1 ? bytes[0] != 0 : qa_load_i32le(bytes) != 0;
    }
    if (selected(app, actor, role) != engine || !returned(app, engine, actor, &clock) ||
        clock.frame.number != value.frame.number || clock.frame.time_ns != value.frame.time_ns ||
        app->publication_generation != value.publication_generation || app->map_revision != value.map_revision ||
        qa_actors_revision(qa_session_actors(app->session)) != value.actors_revision)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 prediction changed during its returned-state read");
    *out = value; *found = true;
    return true;
}

bool qa_application_native_q2_prediction_current(qa_application *app,
    const qa_application_native_q2_prediction *held)
{
    qa_application_native_q2_prediction now;
    bool found;
    if (!held || !qa_application_native_q2_prediction_read(app, held->actor, held->role, &now, &found, NULL) || !found)
        return false;
    return now.owner == held->owner && now.launch == held->launch && now.host == held->host &&
        now.declaration == held->declaration && now.profile == held->profile &&
        now.entity == held->entity && now.client == held->client && now.source_slot == held->source_slot &&
        now.publication_generation == held->publication_generation && now.map_revision == held->map_revision &&
        now.actors_revision == held->actors_revision && now.frame.number == held->frame.number &&
        now.frame.time_ns == held->frame.time_ns && now.gun_frame == held->gun_frame &&
        now.weapon_state == held->weapon_state && now.pending_weapon == held->pending_weapon &&
        now.machinegun_shots == held->machinegun_shots && now.grenade_time_kind == held->grenade_time_kind &&
        (now.grenade_time_kind == QA_NATIVE_Q2_PREDICTION_SECONDS ?
            now.grenade_time.seconds == held->grenade_time.seconds :
            now.grenade_time.milliseconds == held->grenade_time.milliseconds) &&
        now.grenade_blew_up == held->grenade_blew_up && now.animation_frame == held->animation_frame &&
        now.animation_end == held->animation_end && now.animation_priority == held->animation_priority &&
        now.animation_duck == held->animation_duck && now.animation_run == held->animation_run;
}
