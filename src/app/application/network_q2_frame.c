#include "network_q2_private.h"
#include "visual_visibility.h"
#include "qa/hud_q2.h"
#include "qa/text.h"
#include <math.h>

static void vector(float out[3], qa_vec3 value)
{
    out[0] = value.x; out[1] = value.y; out[2] = value.z;
}

static const char *text(qa_application_network_q2 *owner, qa_string_id id)
{
    return id ? qa_strings_cstr(qa_session_strings(owner->app->session), id) : "";
}

static uint32_t clamp(float value, uint32_t minimum, uint32_t maximum)
{
    if (value < (float)minimum) return minimum;
    if (value > (float)maximum) return maximum;
    return (uint32_t)truncf(value);
}

static uint32_t packed_solid(qa_net_protocol_id protocol, qa_bounds bounds)
{
    bool short_word = protocol.kind == QA_NET_Q2_34 ||
        (protocol.kind == QA_NET_R1Q2_35 && protocol.revision < 1905);
    if (short_word) return clamp(bounds.maxs.x / 8, 1, 31) |
        (clamp(-bounds.mins.z / 8, 1, 31) << 5) | (clamp((bounds.maxs.z + 32) / 8, 1, 63) << 10);
    if (protocol.kind == QA_NET_R1Q2_35 || protocol.kind == QA_NET_Q2PRO_36)
        return clamp(bounds.maxs.x, 1, 255) | (clamp(-bounds.mins.z, 0, 255) << 8) |
            (clamp(bounds.maxs.z + 32768, 0, 65535) << 16);
    return clamp(bounds.maxs.x, 1, 255) | (clamp(bounds.maxs.y, 1, 255) << 8) |
        (clamp(-bounds.mins.z, 0, 255) << 16) | (clamp(bounds.maxs.z + 32, 0, 255) << 24);
}

static bool visible_state(const qa_q2_entity *state)
{
    return state->modelindex || state->modelindex2 || state->modelindex3 || state->modelindex4 ||
        state->effects || state->morefx || state->sound || state->event || (state->renderfx & 16384);
}

static bool kex_fields(const qa_application_network_q2 *owner)
{
    return owner->host.protocol.kind == QA_NET_Q2KEX_2023;
}

static void entity_profile(qa_application_network_q2 *owner, qa_q2_entity *state)
{
    state->morefx |= (uint32_t)(state->effects >> 32);
    state->effects = (uint32_t)state->effects;
    if (!kex_fields(owner)) state->instance_bits = state->owner = state->old_frame = 0;
}

static void player_profile(qa_application_network_q2 *owner, qa_q2_player *state)
{
    if (!kex_fields(owner) && owner->host.protocol.kind != QA_NET_Q2PRO_36) {
        state->clientnum = 0; state->clientnum_present = false;
    }
    if (owner->host.source.edition != QA_Q2_RERELEASE || kex_fields(owner)) return;
    if (state->pmove.float_delta_angles) {
        for (unsigned i = 0; i < 3; ++i) {
            uint16_t bits = qa_angle_to_word(state->pmove.delta_angles_f[i]);
            memcpy(&state->pmove.delta_angles[i], &bits, sizeof(bits));
        }
        state->pmove.float_delta_angles = false;
    }
    /* These are genuine KEX fields; 1038/4038 supplies client identity in SERVERDATA. */
    state->team_id = 0;
    state->fog = (qa_q2_player_fog){0};
}

static struct application_native_q2 *original_engine(qa_application_network_q2 *owner)
{
    application_provider *provider = application_network_q2_provider(owner);
    return provider ? provider->state.native.q2_engine : NULL;
}

struct application_q2_wire_capture {
    qa_application_native_q2_entity_prefix *rows;
    int32_t *visibility_pending;
    size_t visibility_pending_capacity;
    uint32_t count, capacity;
    qa_application_native_q2_presentation source;
    qa_world *world;
    qa_collision_geometry *geometry;
    uint64_t application_frame, mutation, actors_revision, revision;
    bool ready;
};

void application_network_q2_capture_dispose(application_provider *provider)
{
    struct application_q2_wire_capture *capture = provider->q2_wire_capture;
    if (!capture) return;
    free(capture->visibility_pending); free(capture->rows); free(capture);
    provider->q2_wire_capture = NULL;
}

static bool capture_source(qa_application_network_q2 *owner,
    struct application_q2_wire_capture **out, qa_error *error)
{
    application_provider *provider = application_network_q2_provider(owner);
    if (!provider) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 capture lost its actual physical Source owner");
    qa_application *app = owner->app;
    uint64_t frame = application_frame_revision(app), mutation = app->snapshot_mutation;
    uint64_t actors = qa_actors_revision(qa_session_actors(app->session));
    if (!provider->q2_wire_capture) {
        provider->q2_wire_capture = calloc(1, sizeof(*provider->q2_wire_capture));
        if (!provider->q2_wire_capture)
            return application_fail(error, QA_ERROR_MEMORY, "Retaining completed Q2 Source capture");
    }
    struct application_q2_wire_capture *capture = provider->q2_wire_capture;
    *out = capture;
    if (capture->ready && capture->application_frame == frame && capture->mutation == mutation &&
        capture->actors_revision == actors && capture->world == app->world && capture->geometry == app->geometry &&
        qa_application_native_q2_presentation_current(app, &capture->source)) return true;
    capture->ready = false;
    uint32_t count;
    if (!qa_application_native_q2_presentation_extent(app, &owner->host.source, &count, error)) return false;
    if (count > owner->entity_capacity)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 entity publication exceeds its admitted physical extent");
    if (count > capture->capacity) {
        qa_application_native_q2_entity_prefix *rows = realloc(capture->rows, (size_t)count * sizeof(*rows));
        if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Growing completed Q2 Source capture");
        capture->rows = rows; capture->capacity = count;
    }
    if (count) memset(capture->rows, 0, (size_t)count * sizeof(*capture->rows));
    bool builtin = owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN;
    if (!builtin && owner->host.source.edition == QA_Q2_CLASSIC) {
        size_t capacity = application_q2_visibility_pending_capacity(app);
        if (!capacity) return application_fail(error, QA_ERROR_MEMORY, "Q2 visibility headnode extent overflows");
        if (capacity > capture->visibility_pending_capacity) {
            int32_t *pending = realloc(capture->visibility_pending, capacity * sizeof(*pending));
            if (!pending) return application_fail(error, QA_ERROR_MEMORY, "Growing Q2 capture visibility scratch");
            capture->visibility_pending = pending; capture->visibility_pending_capacity = capacity;
        }
    }
    for (uint32_t slot = builtin ? 1u : 0u; slot < count; ++slot) {
        bool present;
        if (!qa_application_native_q2_presentation_entity(app, &owner->host.source, slot,
                &capture->rows[slot], &present, error)) return false;
    }
    if (!builtin && !application_native_q2_wire_prepare(original_engine(owner), capture->rows, count, error)) return false;
    if (app->snapshot_mutation != mutation || application_frame_revision(app) != frame ||
        qa_actors_revision(qa_session_actors(app->session)) != actors ||
        !qa_application_native_q2_presentation_current(app, &owner->host.source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 capture changed its completed physical Source boundary");
    capture->count = count; capture->source = owner->host.source;
    capture->application_frame = frame; capture->mutation = mutation; capture->actors_revision = actors;
    capture->world = app->world; capture->geometry = app->geometry; ++capture->revision; capture->ready = true;
    return true;
}

static const qa_application_native_q2_entity_prefix *captured_entity(
    const struct application_q2_wire_capture *capture, uint32_t slot, qa_error *error)
{
    if (slot >= capture->count) {
        application_fail(error, QA_ERROR_ARGUMENT, "Q2 wire edict is outside its physical source table");
        return NULL;
    }
    return &capture->rows[slot];
}

static bool builtin_entity(qa_application_network_q2 *owner, uint32_t slot,
    const struct application_q2_wire_capture *capture, qa_q2_entity *out, bool *present, qa_error *error)
{
    qa_q2_game *game = (qa_q2_game *)owner->host.source.source.game;
    *present = false;
    const qa_application_native_q2_entity_prefix *prefix = captured_entity(capture, slot, error);
    if (!prefix) return false;
    const qa_q2_wire_source_entity *source = &prefix->source.builtin;
    if (!source->binding.in_use || (source->server_flags & 1)) return true;
    qa_entity_visual visual = source->visual;
    qa_application_visual_view selected;
    qa_error observation = {0};
    bool selected_visual = qa_application_visual_read(owner->app, source->binding.actor, &selected, &observation);
    if (!selected_visual && observation.code != QA_OK && observation.code != QA_ERROR_NOT_FOUND) {
        if (error) *error = observation;
        return false;
    }
    if (!(selected_visual ? selected.visual.visible : source->has_visual && visual.visible)) return true;
    int32_t old_frame = selected_visual ? selected.visual.old_frame : visual.old_frame;
    bool flare = selected_visual ? selected.q2_flare.present : source->flare;
    qa_q2_entity value = {.number = slot, .frame = (uint32_t)(selected_visual ? selected.visual.frame : visual.frame),
        .old_frame = old_frame >= 0 ? (uint32_t)old_frame : 0,
        .skinnum = (uint32_t)(selected_visual ? selected.visual.skin : visual.skin),
        .effects = selected_visual ? selected.visual.effects : visual.effects,
        .renderfx = selected_visual ? selected.visual.render_flags : visual.render_flags,
        .alpha = selected_visual ? selected.visual.alpha : visual.alpha,
        .scale = selected_visual ? selected.visual.scale : visual.scale,
        .loop_volume = source->volume, .loop_attenuation = source->attenuation, .event = source->event};
    vector(value.origin, source->body.origin); vector(value.old_origin, source->body.origin);
    if (value.renderfx & 128u)
        vector(value.old_origin, selected_visual ? selected.previous_origin : source->previous_origin);
    vector(value.angles, source->body.angles);
    uint32_t *models[] = {&value.modelindex, &value.modelindex2, &value.modelindex3, &value.modelindex4};
    for (unsigned i = 0; !flare && i < 4; ++i) {
        const char *path = text(owner, selected_visual ? selected.visual.models[i] : visual.models[i]);
        if (!(selected_visual ? application_network_q2_visual_resource(owner, &selected, i, models[i], error) :
            application_network_q2_resource(owner, 0, path, models[i], error))) return false;
    }
    if (flare) {
        float start = selected_visual ? selected.q2_flare.fade_start : source->flare_start;
        float end = selected_visual ? selected.q2_flare.fade_end : source->flare_end;
        const char *image = selected_visual ? selected.q2_flare.image : text(owner, source->flare_image);
        if (!isfinite(start) || !isfinite(end) || start < 0 || (double)start > UINT32_MAX ||
            end < 0 || (double)end > UINT32_MAX)
            return application_fail(error, QA_ERROR_FORMAT, "Q2 flare leaves its actual Source wire field range");
        value.modelindex = 1; value.modelindex2 = (uint32_t)start;
        value.modelindex3 = (uint32_t)end; value.frame = 0;
        if ((value.renderfx & 256) &&
            !application_network_q2_resource(owner, 2, image && *image ? image : "misc/flare.tga", &value.frame, error)) return false;
    }
    qa_builtin_player_info player;
    if ((!selected_visual || selected.provider == owner->host.source.source_owner) &&
        qa_q2_player_projection(game, source->binding.actor, NULL, &player)) {
        value.modelindex = 255; value.skinnum = player.slot;
        if (source->has_weapon && source->weapon.weapon != QA_Q2_WEAPON_NONE) {
            const qa_q2_weapon_definition *weapon = qa_q2_weapon_definition_at(game, source->weapon.weapon);
            if (!weapon || weapon->player_model < 0 || weapon->player_model > 255)
                return application_fail(error, QA_ERROR_FORMAT, "Q2 Source weapon has no representable player model");
            value.modelindex2 = 255; value.skinnum |= (uint32_t)weapon->player_model << 8;
        }
    }
    if (!application_network_q2_resource(owner, 1, text(owner, source->loop_sound), &value.sound, error)) return false;
    if (value.alpha == 1) value.alpha = 0;
    if (value.scale == 1) value.scale = 0;
    if (source->solid == QA_PHYSICS_BRUSH) value.solid = 31;
    else if (source->solid == QA_PHYSICS_BOX && !(source->server_flags & 2))
        value.solid = packed_solid(owner->host.protocol, source->body.bounds);
    if (qa_actor_id_equal(owner->event_actors[slot], source->binding.actor)) value.event = owner->events[slot];
    qa_q2_wire_binding after;
    if (!qa_q2_wire_actor(game, source->binding.actor, &after, error) || after.source_slot != slot ||
        source->body_serial != qa_world_body_storage_serial(owner->app->world, source->binding.actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 entity publication changed its physical actor/body binding");
    entity_profile(owner, &value);
    *present = visible_state(&value); *out = value;
    return true;
}

static bool original_weapon_skin(qa_application_network_q2 *owner,
    const struct application_native_q2 *engine, uint32_t *skin, qa_error *error)
{
    uint32_t ordinal = (*skin >> 8) & 255u;
    if (!ordinal) return true;
    const char *weapon = NULL; uint32_t source_ordinal = 0;
    for (uint32_t i = 1; i < engine->resource_limit[0]; ++i) {
        const char *path = qa_strings_cstr(qa_session_strings(engine->provider->application->session), engine->configstrings[engine->resource_base[0] + i]);
        if (!path || path[0] != '#') continue;
        if (++source_ordinal == ordinal) { weapon = path; break; }
    }
    uint32_t target_ordinal = 0;
    if (weapon) {
        application_q2_resource_table *table = &owner->resources[0];
        bool found = false;
        for (uint32_t i = 1; i <= table->count; ++i) {
            const char *path = table->paths[i];
            if (!path || path[0] != '#') continue;
            ++target_ordinal;
            if (!strcmp(path, weapon)) { found = true; break; }
        }
        if (!found || target_ordinal > 255)
            return application_fail(error, QA_ERROR_FORMAT, "Q2 player weapon has no retained target appearance declaration");
    }
    *skin = (*skin & ~UINT32_C(0xff00)) | (target_ordinal << 8);
    return true;
}

static bool original_entity(qa_application_network_q2 *owner, uint32_t number,
    const struct application_q2_wire_capture *capture, qa_q2_entity *out, bool *present, qa_error *error)
{
    struct application_native_q2 *engine = original_engine(owner);
    if (!engine || !engine->wire_engine || number >= engine->wire_engine->capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 entity lost its installed Engine wire namespace");
    const application_native_q2_wire_row *row = &engine->wire_engine->rows[number];
    *present = false;
    if (!row->occupied) return true;
    if (!qa_actors_get(qa_session_actors(owner->app->session), row->actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 Engine row lost its live full actor");
    qa_q2_entity value = {.number = number};
    const qa_native_host_q2_entity *source = NULL;
    if (row->original) {
        const qa_application_native_q2_entity_prefix *prefix = captured_entity(capture, row->source_slot, error);
        if (!prefix) return false;
        source = &prefix->source.original;
        if (!source->in_use || !qa_actor_id_equal(source->binding.actor, row->actor)) return true;
        if (source->server_flags & 1) return true;
        value = source->state; value.number = number;
        if (owner->host.source.edition == QA_Q2_CLASSIC && value.solid && value.solid != 31 &&
            (owner->host.protocol.kind == QA_NET_Q2PRO_36 ||
             (owner->host.protocol.kind == QA_NET_R1Q2_35 && owner->host.protocol.revision >= 1905)))
            value.solid = packed_solid(owner->host.protocol, source->bounds);
        if (kex_fields(owner) && value.owner) {
            qa_native_slot_binding binding;
            if (!qa_native_slot(qa_native_host_instance(engine->provider->state.native.host), value.owner, &binding, error)) return false;
            if (binding.kind != QA_NATIVE_SLOT_FREE &&
                !application_native_q2_wire_number(engine, binding.actor, &value.owner, error)) return false;
        }
    }
    qa_application_visual_view visual;
    qa_error observation = {0};
    bool selected = qa_application_visual_read(owner->app, row->actor, &visual, &observation);
    if (!selected && observation.code != QA_OK && observation.code != QA_ERROR_NOT_FOUND) {
        if (error) *error = observation;
        return false;
    }
    if (selected && visual.provider != owner->host.source.source_owner) {
        if (!visual.visual.visible) return true;
        vector(value.origin, visual.body.origin); vector(value.old_origin, visual.body.origin);
        if (visual.visual.render_flags & 128u) vector(value.old_origin, visual.previous_origin);
        vector(value.angles, visual.body.angles);
        value.frame = (uint32_t)visual.visual.frame; value.old_frame = visual.visual.old_frame >= 0 ? (uint32_t)visual.visual.old_frame : 0;
        value.skinnum = (uint32_t)visual.visual.skin; value.effects = visual.visual.effects;
        value.renderfx = visual.visual.render_flags; value.alpha = visual.visual.alpha; value.scale = visual.visual.scale == 1 ? 0 : visual.visual.scale;
        uint32_t *models[] = {&value.modelindex, &value.modelindex2, &value.modelindex3, &value.modelindex4};
        for (unsigned i = 0; i < 4; ++i)
            if (!application_network_q2_visual_resource(owner, &visual, i, models[i], error)) return false;
        if (!row->original) {
            qa_actor_collision collision = {0};
            qa_error collision_error = {0};
            if (qa_world_get_collision(owner->app->world, row->actor, &collision, &collision_error)) {
                if (collision.role != QA_COLLISION_TRIGGER) value.solid = collision.inline_model ? 31 :
                    packed_solid(owner->host.protocol, visual.body.bounds);
            } else if (collision_error.code != QA_OK) { if (error) *error = collision_error; return false; }
        }
    } else if (!row->original) return true;
    else {
        uint32_t *models[] = {&value.modelindex, &value.modelindex2, &value.modelindex3, &value.modelindex4};
        bool flare = (value.renderfx & (UINT32_C(1) << 21)) != 0;
        for (unsigned i = 0; !flare && i < 4; ++i)
            if (*models[i] != 255 && !application_network_q2_source_resource(owner, 0, *models[i], models[i], error)) return false;
        bool player = value.modelindex == 255 || value.modelindex2 == 255 || value.modelindex3 == 255 || value.modelindex4 == 255;
        if (player && !original_weapon_skin(owner, engine, &value.skinnum, error)) return false;
        if ((value.renderfx & 256) && !player && !(value.renderfx & 128)) {
            uint32_t *image = flare ? &value.frame : &value.skinnum;
            if (!application_network_q2_source_resource(owner, 2, *image, image, error)) return false;
        }
    }
    if (row->original && !application_network_q2_source_resource(owner, 1, value.sound, &value.sound, error)) return false;
    entity_profile(owner, &value);
    *out = value; *present = visible_state(&value); return true;
}

bool application_network_q2_source_resources(qa_application_network_q2 *owner, qa_error *error)
{
    if (owner->host.source.kind != QA_APPLICATION_NATIVE_Q2_BUILTIN) return true;
    struct application_q2_wire_capture *capture;
    if (!capture_source(owner, &capture, error)) return false;
    if (owner->source_resource_revision == capture->revision) return true;
    uint32_t ignored;
    for (uint32_t slot = 1; slot < capture->count; ++slot) {
        const qa_q2_wire_source_entity *source = &capture->rows[slot].source.builtin;
        if (!source->binding.in_use) continue;
        for (unsigned i = 0; !source->flare && i < 4; ++i)
            if (!application_network_q2_resource(owner, 0, text(owner, source->visual.models[i]),
                &ignored, error)) return false;
        if (source->flare && (source->visual.render_flags & 256) &&
            !application_network_q2_resource(owner, 2, text(owner, source->flare_image), &ignored, error)) return false;
        if (!application_network_q2_resource(owner, 1, text(owner, source->loop_sound), &ignored, error) ||
            !application_network_q2_resource(owner, 1, text(owner, source->precache_sound), &ignored, error) ||
            (source->has_weapon && !application_network_q2_resource(owner, 1,
                text(owner, source->weapon.loop_sound), &ignored, error))) return false;
    }
    owner->source_resource_revision = capture->revision;
    return true;
}

bool application_network_q2_entities(qa_application_network_q2 *owner, qa_error *error)
{
    struct application_q2_wire_capture *capture;
    if (!capture_source(owner, &capture, error)) return false;
    uint64_t actors_revision = qa_actors_revision(qa_session_actors(owner->app->session));
    if (owner->source_entities_ready && owner->source_frame == owner->host.source.clock.frame_number &&
        owner->source_application_frame == application_frame_revision(owner->app) &&
        owner->source_mutation == owner->app->snapshot_mutation &&
        owner->source_actors_revision == actors_revision) return true;
    owner->source_entities_ready = false;
    uint32_t extent;
    bool builtin = owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN;
    if (builtin) {
        extent = capture->count;
    } else {
        struct application_native_q2 *engine = original_engine(owner);
        extent = engine->wire_engine->capacity;
    }
    if (extent > owner->entity_capacity) return application_fail(error, QA_ERROR_FORMAT, "Q2 entity publication exceeds its admitted physical extent");
    owner->source_entity_count = 0;
    for (uint32_t slot = 1; slot < extent; ++slot) {
        qa_q2_entity value;
        bool present;
        if (builtin) {
            if (!builtin_entity(owner, slot, capture, &value, &present, error)) return false;
        } else {
            if (!original_entity(owner, slot, capture, &value, &present, error)) return false;
        }
        if (present) owner->source_entities[owner->source_entity_count++] = value;
    }
    if (!qa_application_native_q2_presentation_current(owner->app, &owner->host.source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 entity publication changed its completed Source frame");
    owner->source_frame = owner->host.source.clock.frame_number;
    owner->source_application_frame = application_frame_revision(owner->app);
    owner->source_mutation = owner->app->snapshot_mutation;
    owner->source_actors_revision = actors_revision;
    owner->source_entities_ready = true;
    return true;
}

bool qa_application_network_q2_motion(qa_application_network_q2 *owner,
    const qa_q2_source_motion **out, qa_error *error)
{
    if (!out || !application_network_q2_current(owner, error)) return false;
    uint64_t interval = owner->host.source.clock_config.interval_ns;
    if (!interval || UINT64_C(1000000000) / interval > 60)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q2 native frame history exceeds its genuine Source FPS profile");
    struct application_q2_wire_capture *capture;
    if (!capture_source(owner, &capture, error)) return false;
    bool builtin = owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN;
    uint32_t extent;
    if (builtin) {
        extent = capture->count;
    } else {
        struct application_native_q2 *engine = original_engine(owner);
        extent = engine->wire_engine->capacity;
    }
    if (extent > owner->entity_capacity)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 Source history exceeds its real physical namespace");
    qa_q2_source_motion motion = {.source_owner = owner->host.source.source_owner,
        .source_frame = owner->host.source.clock.frame_number, .rows = owner->motion_rows};
    for (uint32_t slot = 1; slot < extent; ++slot) {
        qa_q2_source_entity_motion row = {.source_slot = slot, .source_frame = motion.source_frame};
        if (builtin) {
            const qa_application_native_q2_entity_prefix *prefix = captured_entity(capture, slot, error);
            if (!prefix) return false;
            const qa_q2_wire_source_entity *source = &prefix->source.builtin;
            if (!source->binding.in_use) continue;
            row.actor = source->binding.actor; row.origin = source->body.origin;
            row.link_count = source->lifetime.link_count; row.creation_frame = source->lifetime.creation_frame;
            row.creation_origin = source->lifetime.creation_origin; row.creation_present = source->lifetime.present;
            for (size_t i = 0; i < 8; ++i)
                row.origins[i] = (qa_q2_source_origin){.source_frame = source->lifetime.origins[i].source_frame,
                    .origin = source->lifetime.origins[i].origin, .present = source->lifetime.origins[i].present};
        } else {
            struct application_native_q2 *engine = original_engine(owner);
            const application_native_q2_wire_row *binding = &engine->wire_engine->rows[slot];
            if (!binding->occupied) continue;
            if (!binding->original) {
                row = binding->motion; row.source_frame = motion.source_frame;
                qa_body_state body;
                if (!qa_world_body_read(owner->app->world, binding->actor, &body, error)) return false;
                row.origin = body.origin;
            } else {
            const qa_application_native_q2_entity_prefix *prefix = captured_entity(capture, binding->source_slot, error);
            if (!prefix) return false;
            const qa_native_host_q2_entity *source = &prefix->source.original;
            if (!source->in_use) continue;
            row.actor = source->binding.actor;
            row.origin = qa_v3(source->state.origin[0], source->state.origin[1], source->state.origin[2]);
            row.link_count = source->link_count; row.creation_frame = source->creation_frame;
            row.creation_origin = source->creation_origin; row.creation_present = source->creation_present;
            for (size_t i = 0; i < 8; ++i)
                row.origins[i] = (qa_q2_source_origin){.source_frame = source->origins[i].source_frame,
                    .origin = source->origins[i].origin, .present = source->origins[i].present};
            }
        }
        if (!row.actor.registry || !qa_actors_get(qa_session_actors(owner->app->session), row.actor) ||
            !qa_vec_finite(row.origin) || !qa_vec_finite(row.creation_origin) ||
            (row.creation_present && (!row.link_count || row.creation_frame > motion.source_frame)) ||
            (!row.creation_present && (row.creation_frame || row.creation_origin.x != 0 || row.creation_origin.y != 0 || row.creation_origin.z != 0)))
            return application_fail(error, QA_ERROR_FORMAT, "Q2 motion lost its actual Source generation or creation clock");
        for (size_t i = 0; i < 8; ++i) {
            const qa_q2_source_origin *origin = &row.origins[i];
            if (!qa_vec_finite(origin->origin) || (origin->present ? (!row.creation_present ||
                origin->source_frame < row.creation_frame || origin->source_frame > motion.source_frame ||
                (origin->source_frame & 7u) != i) :
                (origin->source_frame || origin->origin.x != 0 || origin->origin.y != 0 || origin->origin.z != 0)))
                return application_fail(error, QA_ERROR_FORMAT, "Q2 motion history is not an actual Source link receipt");
        }
        owner->motion_rows[motion.count++] = row;
    }
    if (!qa_application_native_q2_presentation_current(owner->app, &owner->host.source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 Source motion changed during publication");
    owner->motion = motion; *out = &owner->motion;
    return true;
}

static bool hud_image(void *context, const char *name, uint32_t *out, qa_error *error)
{ return application_network_q2_resource(context, 2, name, out, error); }

bool application_network_q2_player_state(qa_application_network_q2 *owner, qa_actor_id actor,
    qa_q2_player *out, qa_error *error)
{
    qa_network_q2_player physical;
    if (!qa_application_network_q2_player(owner, actor, &physical, error)) return false;
    if (owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL) {
        qa_q2_player value;
        if (!qa_native_host_q2_wire_player((qa_native_host *)owner->host.source.source.original.host,
            physical.source_slot, actor, &value, error) ||
            !application_network_q2_source_resource(owner, 0, value.gunindex, &value.gunindex, error)) return false;
        qa_hud_q2_stat_references refs = owner->status_references;
        bool rr = owner->host.source.edition == QA_Q2_RERELEASE;
        const application_q2_layout_receipt *overlay = &owner->layouts[physical.source_slot];
        if (qa_actor_id_equal(overlay->actor, actor)) {
            refs.images |= overlay->references.images; refs.configstrings |= overlay->references.configstrings;
        }
        const unsigned images[] = {0, 2, 4, 6, 7, 9, 11};
        for (size_t i = 0; i < sizeof(images) / sizeof(*images); ++i) refs.images |= UINT64_C(1) << images[i];
        refs.configstrings |= (UINT64_C(1) << 8) | (UINT64_C(1) << 16);
        if (rr) {
            const unsigned extra[] = {18, 20, 22, 23, 24, 25, 26, 27, 30, 44, 45, 46};
            for (size_t i = 0; i < sizeof(extra) / sizeof(*extra); ++i) refs.images |= UINT64_C(1) << extra[i];
            refs.configstrings |= (UINT64_C(1) << 29) | (UINT64_C(1) << 31) |
                (UINT64_C(1) << 48) | (UINT64_C(1) << 51);
        }
        if (refs.images & refs.configstrings)
            return application_fail(error, QA_ERROR_FORMAT, "Q2 Source layout uses one stat as both image and config index");
        for (unsigned i = 0; i < QA_Q2_MAX_STATS; ++i) {
            uint64_t bit = UINT64_C(1) << i;
            if (!(bit & (refs.images | refs.configstrings)) || !value.stats[i]) continue;
            if (value.stats[i] < 0) return application_fail(error, QA_ERROR_FORMAT, "Q2 Source HUD has a negative registered index");
            uint32_t mapped;
            if (!(refs.images & bit ? application_network_q2_source_resource(owner, 2, (uint32_t)value.stats[i], &mapped, error) :
                application_network_q2_source_config(owner, (uint32_t)value.stats[i], &mapped, error))) return false;
            if (mapped > INT16_MAX) return application_fail(error, QA_ERROR_FORMAT, "Q2 Source HUD index exceeds its actual stat word");
            value.stats[i] = (int16_t)mapped;
        }
        player_profile(owner, &value);
        *out = value; return true;
    }
    qa_q2_game *game = (qa_q2_game *)owner->host.source.source.game;
    qa_q2_wire_view retained;
    qa_movement_result movement;
    qa_q2_wire_source_entity entity;
    if (!qa_q2_player_movement_read(game, actor, &movement, NULL, error) ||
        !qa_q2_wire_view_read(game, actor, &retained, error) ||
        !qa_q2_wire_entity_read(game, physical.source_slot, &entity, error)) return false;
    qa_q2_player value = {.clientnum = (int32_t)(physical.source_slot - 1)};
    if (movement.state.kind == QA_RULESET_Q2_CLASSIC) {
        const qa_q2_movement_state *state = &movement.state.data.q2;
        value.pmove.type = state->type; value.pmove.flags = (int32_t)state->flags;
        value.pmove.time = state->time_eight_ms; value.pmove.gravity = state->gravity;
        for (unsigned i = 0; i < 3; ++i) {
            value.pmove.origin[i] = state->origin_eighths[i]; value.pmove.velocity[i] = state->velocity_eighths[i];
            value.pmove.origin_f[i] = (float)state->origin_eighths[i] / 8;
            value.pmove.velocity_f[i] = (float)state->velocity_eighths[i] / 8;
            value.pmove.delta_angles[i] = state->delta_angle_shorts[i];
        }
    } else if (movement.state.kind == QA_RULESET_Q2_RERELEASE) {
        const qa_q2r_movement_state *state = &movement.state.data.q2r;
        if (state->time_ms > INT32_MAX || !isfinite(state->view_height) ||
            state->view_height < INT8_MIN || state->view_height > INT8_MAX)
            return application_fail(error, QA_ERROR_FORMAT, "Q2 Source movement exceeds its wire time/view height");
        value.pmove.type = state->type; value.pmove.flags = (int32_t)state->flags;
        value.pmove.time = (int32_t)state->time_ms; value.pmove.gravity = state->gravity;
        value.pmove.viewheight = (int32_t)truncf(state->view_height); value.pmove.float_delta_angles = true;
        vector(value.pmove.origin_f, state->origin); vector(value.pmove.velocity_f, state->velocity);
        vector(value.pmove.delta_angles_f, state->delta_angles);
    } else return application_fail(error, QA_ERROR_UNSUPPORTED, "Q2 frame requires an actual Q2 Source pmove receipt");
    qa_q2_player_view view = retained.present ? retained.view : (qa_q2_player_view){
        .angles = movement.view_angles, .offset = movement.view_offset, .fov = retained.view.fov};
    if (!qa_vec_finite(view.angles) || !qa_vec_finite(view.offset) || !qa_vec_finite(view.kick_angles) ||
        !qa_vec_finite(view.gun_angles) || !qa_vec_finite(view.gun_offset) || !isfinite(view.fov) ||
        !isfinite(view.blend.x) || !isfinite(view.blend.y) || !isfinite(view.blend.z) || !isfinite(view.blend.w))
        return application_fail(error, QA_ERROR_FORMAT, "Q2 Source VIEW contains invalid wire fields");
    if (!retained.present) {
        qa_combat_state combat;
        qa_q2_player_info player;
        if (!qa_combat_read(owner->app->combat, actor, &combat, error) || !qa_q2_player_read(game, actor, &player)) return false;
        view.health = combat.health; view.armor = combat.armor.regular.points;
        view.score = player.score; view.spectator = player.spectator; view.selected_item = player.selected_item;
    }
    vector(value.viewangles, view.angles); vector(value.viewoffset, view.offset);
    vector(value.kick_angles, view.kick_angles); vector(value.gunangles, view.gun_angles);
    vector(value.gunoffset, view.gun_offset);
    value.blend[0] = view.blend.x; value.blend[1] = view.blend.y;
    value.blend[2] = view.blend.z; value.blend[3] = view.blend.w;
    value.fov = view.fov; value.rdflags = view.underwater ? 1 : 0;
    if (entity.has_weapon) {
        const qa_q2_weapon_definition *definition = qa_q2_weapon_definition_at(game, entity.weapon.weapon);
        if (entity.weapon.weapon != QA_Q2_WEAPON_NONE && (!definition ||
            !application_network_q2_resource(owner, 0, definition->view_model, &value.gunindex, error))) return false;
        value.gunframe = (uint32_t)entity.weapon.frame; value.gunskin = (uint32_t)entity.weapon.view_skin;
        if (!isfinite(entity.weapon.gun_rate) || entity.weapon.gun_rate < 0 || (double)entity.weapon.gun_rate > UINT32_MAX)
            return application_fail(error, QA_ERROR_FORMAT, "Q2 Source gun rate exceeds its wire field");
        value.gunrate = owner->host.source.edition == QA_Q2_CLASSIC && entity.weapon.gun_rate == 10 ?
            0 : (uint32_t)entity.weapon.gun_rate;
    }
    if (!qa_q2_wire_stats(game, &view, &(qa_q2_wire_stat_resources){
        .context = owner, .items_base = owner->item_base, .image = hud_image}, value.stats, error)) return false;
    player_profile(owner, &value);
    *out = value;
    return true;
}

static bool source_visible(qa_application_network_q2 *owner, const application_q2_visibility_recipient *recipient,
    const qa_q2_entity *state, const struct application_q2_wire_capture *capture,
    bool novis, bool *out, bool *owned, qa_error *error)
{
    bool builtin = owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN;
    qa_bounds bounds;
    qa_actor_id visibility_actor;
    uint32_t flags;
    const qa_native_host_q2_entity *original = NULL;
    if (builtin) {
        const qa_application_native_q2_entity_prefix *prefix = captured_entity(capture, state->number, error);
        if (!prefix) return false;
        const qa_q2_wire_source_entity *source = &prefix->source.builtin;
        if (!source->binding.in_use) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 entity retired during recipient visibility");
        visibility_actor = source->binding.actor;
        flags = source->server_flags; *owned = qa_actor_id_equal(source->owner, recipient->actor);
        qa_linked_body linked;
        if (qa_world_linked(owner->app->world, source->binding.actor, &linked)) bounds = linked.absolute_bounds;
        else bounds = (qa_bounds){qa_vec_sub(qa_vec_add(source->body.origin, source->body.bounds.mins), qa_v3(1, 1, 1)),
            qa_vec_add(qa_vec_add(source->body.origin, source->body.bounds.maxs), qa_v3(1, 1, 1))};
    } else {
        struct application_native_q2 *engine = original_engine(owner);
        if (!engine || !engine->wire_engine || state->number >= engine->wire_engine->capacity)
            return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 visibility lost its Engine namespace");
        const application_native_q2_wire_row *binding = &engine->wire_engine->rows[state->number];
        if (!binding->occupied) return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 Engine actor retired during visibility");
        visibility_actor = binding->actor;
        if (binding->original) {
            const qa_application_native_q2_entity_prefix *prefix = captured_entity(capture, binding->source_slot, error);
            if (!prefix) return false;
            original = &prefix->source.original;
            if (!original->in_use || !qa_actor_id_equal(original->binding.actor, binding->actor))
                return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 edict retired during visibility");
            flags = original->server_flags; bounds = original->absolute_bounds;
            *owned = original->owner_slot == recipient->slot;
        } else {
            qa_linked_body linked;
            qa_actor_collision collision = {0};
            qa_error collision_error = {0};
            if (!qa_world_linked(owner->app->world, binding->actor, &linked)) { *out = false; *owned = false; return true; }
            bounds = linked.absolute_bounds; flags = 0; *owned = false;
            if (qa_world_get_collision(owner->app->world, binding->actor, &collision, &collision_error)) {
                const qa_actor_record *recipient_actor = qa_actors_get(qa_session_actors(owner->app->session), recipient->actor);
                *owned = collision.owner.kind == QA_ACTOR_REFERENCE_SOURCE ?
                    recipient_actor && recipient_actor->has_source &&
                    recipient_actor->owner == collision.owner.value.source.owner &&
                    recipient_actor->source_slot == collision.owner.value.source.slot :
                    collision.owner.kind == QA_ACTOR_REFERENCE_LIFETIME && qa_actor_id_equal(collision.owner.value.actor, recipient->actor);
            }
            else if (collision_error.code != QA_OK) { if (error) *error = collision_error; return false; }
        }
    }
    application_q2_visibility_entity entity = {.actor = visibility_actor,
        .bounds = bounds, .flags = flags, .original = original};
    return application_q2_visibility_test(owner->app, original_engine(owner),
        owner->host.source.edition == QA_Q2_RERELEASE, builtin,
        recipient, &entity, state, novis, out, error);
}

bool qa_application_network_q2_frame(qa_application_network_q2 *owner, const qa_actor_id *actors,
    size_t seats, qa_q2_wire_frame *out, qa_error *error)
{
    if (!out || !actors || !seats || seats > QA_Q2_MAX_SEATS ||
        (seats > 1 && owner && owner->host.protocol.kind != QA_NET_Q2KEX_2023) ||
        !application_network_q2_current(owner, error) || !application_network_q2_observe(owner, error)) return false;
    const qa_bsp_view *map = qa_collision_bsp(owner->app->geometry);
    if (owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL && map->family != QA_BSP_Q2)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Original Q2 frame requires its actual Q2 portal geometry");
    const qa_cvar_view *novis = qa_cvars_read(owner->host.cvars, owner->sv_novis);
    bool no_visibility = novis && novis->number != 0;
    qa_q2_wire_frame value = {.valid = true, .delta_frame = -1, .player_count = seats};
    uint32_t frame_bits = (uint32_t)owner->host.source.clock.frame_number;
    memcpy(&value.server_frame, &frame_bits, sizeof(frame_bits));
    application_q2_visibility_recipient recipients[QA_Q2_MAX_SEATS] = {0};
    bool ok = true;
    for (size_t i = 0; ok && i < seats; ++i) {
        qa_network_q2_player player;
        qa_q2_player *state = &value.players[i].player;
        ok = qa_application_network_q2_player(owner, actors[i], &player, error) &&
            application_network_q2_player_state(owner, actors[i], state, error);
        if (!ok) break;
        for (size_t j = 0; j < i; ++j) if (recipients[j].slot == player.source_slot)
            ok = application_fail(error, QA_ERROR_FORMAT, "Q2 frame seats alias the same real source player");
        application_q2_visibility_recipient *recipient = &recipients[i];
        qa_vec3 origin = qa_v3(state->pmove.origin_f[0] + state->viewoffset[0],
            state->pmove.origin_f[1] + state->viewoffset[1], state->pmove.origin_f[2] + state->viewoffset[2]);
        size_t written;
        if (ok) ok = application_q2_visibility_recipient_prepare(owner->app, actors[i],
            player.source_slot, origin, recipient, error) &&
            qa_collision_area_bits(owner->app->geometry, (int32_t)recipient->leaf.area,
                owner->area_bits[i], QA_Q2_MAX_AREABITS, &written, error);
        if (!ok) break;
        value.players[i].area_bits = (qa_bytes){owner->area_bits[i], written};
    }
    if (ok) ok = application_network_q2_entities(owner, error);
    struct application_q2_wire_capture *capture = NULL;
    if (ok) ok = capture_source(owner, &capture, error);
    if (ok) for (size_t i = 0; i < seats; ++i) {
        recipients[i].pending = capture->visibility_pending;
        recipients[i].pending_capacity = capture->visibility_pending ?
            application_q2_visibility_pending_capacity(owner->app) : 0;
    }
    size_t count = 0;
    for (size_t i = 0; ok && i < owner->source_entity_count; ++i) {
        qa_q2_entity state = owner->source_entities[i];
        bool visible = false, owned = false;
        uint32_t hidden = 0;
        for (size_t j = 0; ok && j < seats; ++j) {
            bool seen = false, own = false;
            ok = source_visible(owner, &recipients[j], &state, capture, no_visibility, &seen, &own, error);
            visible |= seen; owned |= own;
            if (!seen) hidden |= UINT32_C(1) << j;
        }
        if (ok && visible) {
            if (kex_fields(owner)) state.instance_bits = hidden;
            if (owned) state.solid = 0;
            owner->entities[count++] = state;
        }
    }
    if (ok && !qa_application_native_q2_presentation_current(owner->app, &owner->host.source))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Q2 frame changed its actual Source receipt during visibility");
    if (!ok) return false;
    owner->entity_count = count; value.entities = owner->entities; value.entity_count = count;
    *out = value;
    return true;
}
