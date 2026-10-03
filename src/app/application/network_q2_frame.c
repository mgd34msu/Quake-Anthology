#include "network_q2_private.h"
#include "native_q2_visibility.h"
#include "qa/hud_q2.h"
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
            double word = fmod(trunc((double)state->pmove.delta_angles_f[i] * (65536.0 / 360.0)), 65536.0);
            if (word < 0) word += 65536;
            uint16_t bits = (uint16_t)word;
            memcpy(&state->pmove.delta_angles[i], &bits, sizeof(bits));
        }
        state->pmove.float_delta_angles = false;
    }
    /* These are genuine KEX fields; 1038/4038 supplies client identity in SERVERDATA. */
    state->team_id = 0;
    state->fog = (qa_q2_player_fog){0};
}

static bool builtin_entity(qa_application_network_q2 *owner, uint32_t slot,
    qa_q2_entity *out, bool *present, qa_error *error)
{
    qa_q2_game *game = (qa_q2_game *)owner->host.source.source.game;
    qa_q2_wire_source_entity source;
    *present = false;
    if (!qa_q2_wire_entity_read(game, slot, &source, error)) return false;
    if (!source.binding.in_use || (source.server_flags & 1)) return true;
    qa_q2_visual visual = source.visual;
    qa_application_visual_view selected;
    qa_error observation = {0};
    bool selected_visual = qa_application_visual_read(owner->app, source.binding.actor, &selected, &observation);
    if (!selected_visual && observation.code != QA_OK && observation.code != QA_ERROR_NOT_FOUND) {
        if (error) *error = observation;
        return false;
    }
    if (!(selected_visual ? selected.visible : source.has_visual && visual.visible)) return true;
    int32_t old_frame = selected_visual ? selected.old_frame : visual.old_frame;
    bool flare = selected_visual ? selected.q2_flare.present : source.flare;
    qa_q2_entity value = {.number = slot, .frame = (uint32_t)(selected_visual ? selected.frame : visual.frame),
        .old_frame = old_frame >= 0 ? (uint32_t)old_frame : 0,
        .skinnum = (uint32_t)(selected_visual ? selected.skin : visual.skin),
        .effects = selected_visual ? selected.effects : visual.effects,
        .renderfx = selected_visual ? selected.render_flags : visual.render_flags,
        .alpha = selected_visual ? selected.alpha : visual.alpha,
        .scale = selected_visual ? selected.scale : visual.scale,
        .loop_volume = source.volume, .loop_attenuation = source.attenuation, .event = source.event};
    vector(value.origin, source.body.origin); vector(value.old_origin, source.body.origin);
    vector(value.angles, source.body.angles);
    uint32_t *models[] = {&value.modelindex, &value.modelindex2, &value.modelindex3, &value.modelindex4};
    for (unsigned i = 0; !flare && i < 4; ++i) {
        const char *path = selected_visual ? selected.models[i] : text(owner, visual.models[i]);
        if (!(selected_visual ? application_network_q2_visual_resource(owner, &selected, i, models[i], error) :
            application_network_q2_resource(owner, 0, path, models[i], error))) return false;
    }
    if (flare) {
        float start = selected_visual ? selected.q2_flare.fade_start : source.flare_start;
        float end = selected_visual ? selected.q2_flare.fade_end : source.flare_end;
        const char *image = selected_visual ? selected.q2_flare.image : text(owner, source.flare_image);
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
        qa_q2_player_projection(game, source.binding.actor, &player)) {
        value.modelindex = 255; value.skinnum = player.slot;
        if (source.has_weapon && source.weapon.weapon != QA_Q2_WEAPON_NONE) {
            const qa_q2_weapon_definition *weapon = qa_q2_weapon_definition_at(game, source.weapon.weapon);
            if (!weapon || weapon->player_model < 0 || weapon->player_model > 255)
                return application_fail(error, QA_ERROR_FORMAT, "Q2 Source weapon has no representable player model");
            value.modelindex2 = 255; value.skinnum |= (uint32_t)weapon->player_model << 8;
        }
    }
    if (!application_network_q2_resource(owner, 1, text(owner, source.loop_sound), &value.sound, error)) return false;
    if (value.alpha == 1) value.alpha = 0;
    if (value.scale == 1) value.scale = 0;
    if (source.solid == QA_PHYSICS_BRUSH) value.solid = 31;
    else if (source.solid == QA_PHYSICS_BOX && !(source.server_flags & 2))
        value.solid = packed_solid(owner->host.protocol, source.body.bounds);
    if (qa_actor_id_equal(owner->event_actors[slot], source.binding.actor)) value.event = owner->events[slot];
    qa_q2_wire_binding after;
    if (!qa_q2_wire_actor(game, source.binding.actor, &after, error) || after.source_slot != slot ||
        source.body_serial != qa_world_body_storage_serial(owner->app->world, source.binding.actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 entity publication changed its physical actor/body binding");
    entity_profile(owner, &value);
    *present = visible_state(&value); *out = value;
    return true;
}

static struct application_native_q2 *original_engine(qa_application_network_q2 *owner)
{
    application_provider *provider = application_network_q2_provider(owner);
    return provider ? provider->state.native.q2_engine : NULL;
}

static bool original_weapon_skin(qa_application_network_q2 *owner,
    const struct application_native_q2 *engine, uint32_t *skin, qa_error *error)
{
    uint32_t ordinal = (*skin >> 8) & 255u;
    if (!ordinal) return true;
    const char *weapon = NULL; uint32_t source_ordinal = 0;
    for (uint32_t i = 1; i < engine->resource_limit[0]; ++i) {
        const char *path = engine->configstrings[engine->resource_base[0] + i];
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
    qa_q2_entity *out, bool *present, qa_error *error)
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
    qa_native_host_q2_entity source = {0};
    if (row->original) {
        if (!qa_native_host_q2_wire_entity(engine->provider->state.native.host, row->source_slot, &source, error)) return false;
        if (!source.in_use || !qa_actor_id_equal(source.binding.actor, row->actor)) return true;
        if (source.server_flags & 1) return true;
        value = source.state; value.number = number;
        if (owner->host.source.edition == QA_Q2_CLASSIC && value.solid && value.solid != 31 &&
            (owner->host.protocol.kind == QA_NET_Q2PRO_36 ||
             (owner->host.protocol.kind == QA_NET_R1Q2_35 && owner->host.protocol.revision >= 1905)))
            value.solid = packed_solid(owner->host.protocol, source.bounds);
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
        if (!visual.visible) return true;
        vector(value.origin, visual.body.origin); vector(value.old_origin, visual.body.origin);
        vector(value.angles, visual.body.angles);
        value.frame = (uint32_t)visual.frame; value.old_frame = visual.old_frame >= 0 ? (uint32_t)visual.old_frame : 0;
        value.skinnum = (uint32_t)visual.skin; value.effects = visual.effects;
        value.renderfx = visual.render_flags; value.alpha = visual.alpha; value.scale = visual.scale == 1 ? 0 : visual.scale;
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

bool application_network_q2_entities(qa_application_network_q2 *owner, qa_error *error)
{
    uint32_t extent;
    bool builtin = owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN;
    if (builtin) {
        if (!qa_q2_wire_extent(owner->host.source.source.game, &extent, error)) return false;
    } else {
        struct application_native_q2 *engine = original_engine(owner);
        if (!application_native_q2_wire_prepare(engine, error)) return false;
        extent = engine->wire_engine->capacity;
    }
    if (extent > owner->entity_capacity) return application_fail(error, QA_ERROR_FORMAT, "Q2 entity publication exceeds its admitted physical extent");
    owner->entity_count = 0;
    for (uint32_t slot = 1; slot < extent; ++slot) {
        qa_q2_entity value;
        bool present;
        if (builtin) {
            if (!builtin_entity(owner, slot, &value, &present, error)) return false;
        } else {
            if (!original_entity(owner, slot, &value, &present, error)) return false;
        }
        if (present) owner->entities[owner->entity_count++] = value;
    }
    return qa_application_native_q2_presentation_current(owner->app, &owner->host.source) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Q2 entity publication changed its completed Source frame");
}

bool qa_application_network_q2_motion(qa_application_network_q2 *owner,
    const qa_q2_source_motion **out, qa_error *error)
{
    if (!out || !application_network_q2_current(owner, error)) return false;
    uint64_t interval = owner->host.source.clock_config.interval_ns;
    if (!interval || UINT64_C(1000000000) / interval > 60)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q2 native frame history exceeds its genuine Source FPS profile");
    bool builtin = owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN;
    uint32_t extent;
    if (builtin) {
        if (!qa_q2_wire_extent(owner->host.source.source.game, &extent, error)) return false;
    } else {
        struct application_native_q2 *engine = original_engine(owner);
        if (!application_native_q2_wire_prepare(engine, error)) return false;
        extent = engine->wire_engine->capacity;
    }
    if (extent > owner->entity_capacity)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 Source history exceeds its real physical namespace");
    qa_q2_source_motion motion = {.source_owner = owner->host.source.source_owner,
        .source_frame = owner->host.source.clock.frame_number, .rows = owner->motion_rows};
    for (uint32_t slot = 1; slot < extent; ++slot) {
        qa_q2_source_entity_motion row = {.source_slot = slot, .source_frame = motion.source_frame};
        if (builtin) {
            qa_q2_wire_source_entity source;
            if (!qa_q2_wire_entity_read((qa_q2_game *)owner->host.source.source.game, slot, &source, error)) return false;
            if (!source.binding.in_use) continue;
            row.actor = source.binding.actor; row.origin = source.body.origin;
            row.link_count = source.lifetime.link_count; row.creation_frame = source.lifetime.creation_frame;
            row.creation_origin = source.lifetime.creation_origin; row.creation_present = source.lifetime.present;
            for (size_t i = 0; i < 8; ++i)
                row.origins[i] = (qa_q2_source_origin){.source_frame = source.lifetime.origins[i].source_frame,
                    .origin = source.lifetime.origins[i].origin, .present = source.lifetime.origins[i].present};
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
            qa_native_host_q2_entity source;
            if (!qa_native_host_q2_wire_entity((qa_native_host *)owner->host.source.source.original.host,
                binding->source_slot, &source, error)) return false;
            if (!source.in_use) continue;
            row.actor = source.binding.actor;
            row.origin = qa_v3(source.state.origin[0], source.state.origin[1], source.state.origin[2]);
            row.link_count = source.link_count; row.creation_frame = source.creation_frame;
            row.creation_origin = source.creation_origin; row.creation_present = source.creation_present;
            for (size_t i = 0; i < 8; ++i)
                row.origins[i] = (qa_q2_source_origin){.source_frame = source.origins[i].source_frame,
                    .origin = source.origins[i].origin, .present = source.origins[i].present};
            }
        }
        if (!row.actor.registry || !qa_actors_get(qa_session_actors(owner->app->session), row.actor) ||
            !qa_vec_finite(row.origin) || !qa_vec_finite(row.creation_origin) ||
            (row.creation_present && (!row.link_count || row.creation_frame > motion.source_frame)) ||
            (!row.creation_present && (row.creation_frame || row.creation_origin.x || row.creation_origin.y || row.creation_origin.z)))
            return application_fail(error, QA_ERROR_FORMAT, "Q2 motion lost its actual Source generation or creation clock");
        for (size_t i = 0; i < 8; ++i) {
            const qa_q2_source_origin *origin = &row.origins[i];
            if (!qa_vec_finite(origin->origin) || (origin->present ? (!row.creation_present ||
                origin->source_frame < row.creation_frame || origin->source_frame > motion.source_frame ||
                (origin->source_frame & 7u) != i) :
                (origin->source_frame || origin->origin.x || origin->origin.y || origin->origin.z)))
                return application_fail(error, QA_ERROR_FORMAT, "Q2 motion history is not an actual Source link receipt");
        }
        owner->motion_rows[motion.count++] = row;
    }
    if (!qa_application_native_q2_presentation_current(owner->app, &owner->host.source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 Source motion changed during publication");
    owner->motion = motion; *out = &owner->motion;
    return true;
}

static void integer_statistic(int32_t value, int16_t *out)
{
    uint16_t bits = (uint16_t)(uint32_t)value;
    memcpy(out, &bits, sizeof(bits));
}

static bool statistic(double value, int16_t *out, qa_error *error)
{
    if (!isfinite(value))
        return application_fail(error, QA_ERROR_FORMAT, "Q2 Source statistic is not finite");
    double narrowed = fmod(trunc(value), 65536);
    if (narrowed < 0) narrowed += 65536;
    uint16_t bits = (uint16_t)narrowed;
    memcpy(out, &bits, sizeof(bits));
    return true;
}

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
        qa_hud_q2_stat_references refs = {0};
        const char *layout = owner->configs[5] ? owner->configs[5] : "";
        bool rr = owner->host.source.edition == QA_Q2_RERELEASE;
        if (!qa_hud_q2_layout_stat_references(layout, rr, &refs, error)) return false;
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
    qa_q2_wire_movement movement;
    qa_q2_wire_source_entity entity;
    if (!qa_q2_wire_movement_read(game, actor, &movement, error) ||
        !qa_q2_wire_view_read(game, actor, &retained, error) ||
        !qa_q2_wire_entity_read(game, physical.source_slot, &entity, error)) return false;
    qa_q2_player value = {.clientnum = (int32_t)(physical.source_slot - 1)};
    if (movement.state.kind == QA_MOVEMENT_Q2_CLASSIC) {
        const qa_q2_movement_state *state = &movement.state.data.q2;
        value.pmove.type = state->type; value.pmove.flags = (int32_t)state->flags;
        value.pmove.time = state->time_eight_ms; value.pmove.gravity = state->gravity;
        for (unsigned i = 0; i < 3; ++i) {
            value.pmove.origin[i] = state->origin_eighths[i]; value.pmove.velocity[i] = state->velocity_eighths[i];
            value.pmove.origin_f[i] = (float)state->origin_eighths[i] / 8;
            value.pmove.velocity_f[i] = (float)state->velocity_eighths[i] / 8;
            value.pmove.delta_angles[i] = state->delta_angle_shorts[i];
        }
    } else if (movement.state.kind == QA_MOVEMENT_Q2_RERELEASE) {
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
        value.gunrate = (uint32_t)entity.weapon.gun_rate;
    }
    uint32_t health_icon, ammo_icon, armor_icon;
    if (!application_network_q2_resource(owner, 2, "i_health", &health_icon, error) || health_icon > INT16_MAX ||
        !application_network_q2_resource(owner, 2, text(owner, view.ammo_icon), &ammo_icon, error) || ammo_icon > INT16_MAX ||
        !application_network_q2_resource(owner, 2, text(owner, view.armor_icon), &armor_icon, error) || armor_icon > INT16_MAX ||
        !statistic(view.health, &value.stats[1], error) ||
        !statistic(view.armor, &value.stats[5], error)) return false;
    integer_statistic(view.ammo_count, &value.stats[3]);
    integer_statistic(view.timer_seconds, &value.stats[10]);
    integer_statistic(view.layouts, &value.stats[13]);
    integer_statistic(view.score, &value.stats[14]);
    integer_statistic(view.flashes, &value.stats[15]);
    value.stats[0] = (int16_t)health_icon; value.stats[17] = view.spectator ? 1 : 0;
    value.stats[2] = (int16_t)ammo_icon; value.stats[4] = (int16_t)armor_icon;
    for (size_t i = 0; i < qa_q2_item_count(game); ++i) {
        const qa_q2_item_definition *item = qa_q2_item_at(game, i);
        uint32_t image;
        if (view.selected_item && item->item == view.selected_item) {
            if (i + 1 > INT16_MAX || !application_network_q2_resource(owner, 2, item->icon, &image, error) || image > INT16_MAX) return false;
            value.stats[12] = (int16_t)(i + 1); value.stats[6] = (int16_t)image;
        }
        if (view.timer_item && item->item == view.timer_item) {
            if (!application_network_q2_resource(owner, 2, item->icon, &image, error) || image > INT16_MAX) return false;
            value.stats[9] = (int16_t)image;
        }
    }
    if (owner->host.source.edition == QA_Q2_RERELEASE)
        integer_statistic(view.hit_marker_damage, &value.stats[50]);
    player_profile(owner, &value);
    *out = value;
    return true;
}

typedef struct q2_recipient {
    qa_actor_id actor;
    uint32_t slot;
    qa_vec3 origin;
    qa_collision_leaf leaf;
    int32_t clusters[64];
    size_t cluster_count;
} q2_recipient;

static bool cluster_visible(qa_collision_geometry *geometry, const q2_recipient *recipient,
    int32_t target, bool phs, bool *out, qa_error *error)
{
    *out = false;
    for (size_t i = 0; i < recipient->cluster_count; ++i) {
        bool visible;
        if (!qa_collision_cluster_visible(geometry, recipient->clusters[i], target, phs, &visible, error)) return false;
        if (visible) { *out = true; break; }
    }
    return true;
}

static bool headnode_visible(qa_collision_geometry *geometry, const q2_recipient *recipient,
    int32_t headnode, bool phs, bool *out, qa_error *error)
{
    const qa_bsp_view *map = qa_collision_bsp(geometry);
    size_t nodes = qa_bsp_record_count(map, QA_BSP_NODES);
    if (nodes > (SIZE_MAX / sizeof(int32_t) - 1) / 2)
        return application_fail(error, QA_ERROR_MEMORY, "Q2 source visibility headnode extent overflows");
    size_t capacity = nodes * 2 + 1, count = 1, visits = 0;
    int32_t *pending = malloc(capacity * sizeof(*pending));
    if (!pending) return application_fail(error, QA_ERROR_MEMORY, "Observing actual Q2 source visibility headnode");
    pending[0] = headnode; *out = false;
    bool ok = true;
    while (ok && count && !*out) {
        int32_t child = pending[--count];
        if (++visits > capacity) { ok = application_fail(error, QA_ERROR_FORMAT, "Q2 visibility headnode is cyclic"); break; }
        if (child < 0) {
            uint32_t index = (uint32_t)(-(int64_t)child - 1);
            qa_collision_leaf leaf;
            ok = qa_collision_leaf_at(geometry, index, &leaf, error) &&
                cluster_visible(geometry, recipient, (int32_t)leaf.cluster, phs, out, error);
        } else {
            qa_bsp_node node;
            ok = count <= capacity - 2 && qa_bsp_read_node(map, (uint32_t)child, &node, error);
            if (ok) { pending[count++] = node.children[1]; pending[count++] = node.children[0]; }
        }
    }
    free(pending); return ok;
}

static bool source_visible(qa_application_network_q2 *owner, const q2_recipient *recipient,
    const qa_q2_entity *state, uint32_t *leaves, size_t leaf_capacity,
    bool *out, bool *owned, qa_error *error)
{
    qa_collision_geometry *geometry = owner->app->geometry;
    bool builtin = owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN;
    bool sdk = false;
    bool rr = owner->host.source.edition == QA_Q2_RERELEASE;
    qa_bounds bounds;
    uint32_t flags;
    qa_native_host_q2_entity original = {0};
    if (builtin) {
        qa_q2_wire_source_entity source;
        if (!qa_q2_wire_entity_read((qa_q2_game *)owner->host.source.source.game, state->number, &source, error)) return false;
        if (!source.binding.in_use) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 entity retired during recipient visibility");
        flags = source.server_flags; *owned = qa_actor_id_equal(source.owner, recipient->actor);
        qa_linked_body linked;
        if (qa_world_linked(owner->app->world, source.binding.actor, &linked)) bounds = linked.absolute_bounds;
        else bounds = (qa_bounds){qa_vec_sub(qa_vec_add(source.body.origin, source.body.bounds.mins), qa_v3(1, 1, 1)),
            qa_vec_add(qa_vec_add(source.body.origin, source.body.bounds.maxs), qa_v3(1, 1, 1))};
    } else {
        struct application_native_q2 *engine = original_engine(owner);
        if (!engine || !engine->wire_engine || state->number >= engine->wire_engine->capacity)
            return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 visibility lost its Engine namespace");
        const application_native_q2_wire_row *binding = &engine->wire_engine->rows[state->number];
        if (!binding->occupied) return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 Engine actor retired during visibility");
        sdk = binding->original;
        if (sdk) {
            if (!qa_native_host_q2_wire_entity(engine->provider->state.native.host, binding->source_slot, &original, error)) return false;
            if (!original.in_use || !qa_actor_id_equal(original.binding.actor, binding->actor))
                return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 edict retired during visibility");
            flags = original.server_flags; bounds = original.absolute_bounds;
            *owned = original.owner_slot == recipient->slot;
        } else {
            qa_linked_body linked;
            qa_actor_collision collision = {0};
            qa_error collision_error = {0};
            if (!qa_world_linked(owner->app->world, binding->actor, &linked)) { *out = false; *owned = false; return true; }
            bounds = linked.absolute_bounds; flags = 0; *owned = false;
            if (qa_world_get_collision(owner->app->world, binding->actor, &collision, &collision_error))
                *owned = qa_actor_id_equal(collision.owner, recipient->actor);
            else if (collision_error.code != QA_OK) { if (error) *error = collision_error; return false; }
        }
    }
    *out = false;
    if (flags & 1) return true;
    if (sdk && rr && (flags & 256)) {
        struct application_native_q2 *engine = original_engine(owner);
        bool admitted;
        if (!application_native_q2_visibility_read(engine, original.binding.source_slot,
                original.binding.actor, recipient->slot, recipient->actor, &admitted, error)) return false;
        if (!admitted) return true;
    }
    const qa_cvar_view *novis = qa_cvars_find(owner->host.cvars, "sv_novis");
    if (state->number == recipient->slot || (rr && ((flags & 1024) || (novis && novis->number != 0)))) { *out = true; return true; }
    bool beam = (state->renderfx & 128) != 0, shadow = rr && (state->renderfx & 16384) != 0;
    bool phs = beam || (rr && (shadow || state->sound));
    bool area = false, visible = false;
    if (sdk && !rr) {
        if (!qa_collision_areas_connected(geometry, (int32_t)recipient->leaf.area, original.areas[0], &area, error)) return false;
        if (!area && original.areas[1] &&
            !qa_collision_areas_connected(geometry, (int32_t)recipient->leaf.area, original.areas[1], &area, error)) return false;
        if (!area) return true;
        if (beam && original.cluster_count > 0) {
            if (!qa_collision_cluster_visible(geometry, (int32_t)recipient->leaf.cluster, original.clusters[0], true, &visible, error)) return false;
        } else if (original.cluster_count == -1) {
            if (!headnode_visible(geometry, recipient, original.headnode, false, &visible, error)) return false;
        } else for (int32_t i = 0; !visible && i < original.cluster_count; ++i)
            if (!cluster_visible(geometry, recipient, original.clusters[i], false, &visible, error)) return false;
    } else {
        qa_leaf_list list;
        if (!qa_collision_box_leaves(geometry, bounds, leaves, leaf_capacity, &list, error) || list.overflow)
            return application_fail(error, QA_ERROR_FORMAT, "Q2 source entity leaf membership exceeds its geometry");
        for (size_t i = 0; i < list.count; ++i) {
            qa_collision_leaf leaf;
            bool connected, seen;
            if (!qa_collision_leaf_at(geometry, leaves[i], &leaf, error) ||
                !qa_collision_areas_connected(geometry, (int32_t)recipient->leaf.area, (int32_t)leaf.area, &connected, error)) return false;
            area |= connected;
            if (builtin && beam && !rr) {
                seen = false;
                if (!i && !qa_collision_cluster_visible(geometry,
                    (int32_t)recipient->leaf.cluster, (int32_t)leaf.cluster,
                    true, &seen, error)) return false;
            } else if (!cluster_visible(geometry, recipient, (int32_t)leaf.cluster, phs, &seen, error)) return false;
            visible |= seen;
        }
        if (!area) return true;
    }
    if (!visible) return true;
    qa_vec3 origin = qa_v3(state->origin[0], state->origin[1], state->origin[2]);
    float distance = qa_vec_length(qa_vec_sub(recipient->origin, origin));
    if (rr && state->sound) {
        float attenuation = state->loop_attenuation == -1 ? 0 :
            state->loop_attenuation > 0 && state->loop_attenuation != 3 ? state->loop_attenuation * .0006f : .003f;
        if ((distance - 80) * attenuation <= 1) { *out = true; return true; }
        if (!state->modelindex) return true;
        if (!beam) {
            visible = false;
            qa_leaf_list list;
            if (!qa_collision_box_leaves(geometry, bounds, leaves, leaf_capacity, &list, error)) return false;
            for (size_t i = 0; !visible && i < list.count; ++i) {
                qa_collision_leaf leaf;
                if (!qa_collision_leaf_at(geometry, leaves[i], &leaf, error) ||
                    !cluster_visible(geometry, recipient, (int32_t)leaf.cluster, false, &visible, error)) return false;
            }
        }
        *out = beam || visible; return true;
    }
    *out = state->modelindex || shadow || distance <= 400;
    return true;
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
    size_t leaf_capacity = qa_bsp_record_count(map, QA_BSP_LEAVES);
    if (!leaf_capacity || leaf_capacity > SIZE_MAX / sizeof(uint32_t))
        return application_fail(error, QA_ERROR_FORMAT, "Q2 publication has no bounded geometry leaves");
    uint32_t *leaves = malloc(leaf_capacity * sizeof(*leaves));
    if (!leaves) return application_fail(error, QA_ERROR_MEMORY, "Observing actual Q2 recipient leaves");
    qa_q2_wire_frame value = {.valid = true, .delta_frame = -1, .player_count = seats};
    uint32_t frame_bits = (uint32_t)owner->host.source.clock.frame_number;
    memcpy(&value.server_frame, &frame_bits, sizeof(frame_bits));
    q2_recipient recipients[QA_Q2_MAX_SEATS] = {0};
    bool ok = true;
    for (size_t i = 0; ok && i < seats; ++i) {
        qa_network_q2_player player;
        qa_q2_player *state = &value.players[i].player;
        ok = qa_application_network_q2_player(owner, actors[i], &player, error) &&
            application_network_q2_player_state(owner, actors[i], state, error);
        if (!ok) break;
        for (size_t j = 0; j < i; ++j) if (recipients[j].slot == player.source_slot)
            ok = application_fail(error, QA_ERROR_FORMAT, "Q2 frame seats alias the same real source player");
        q2_recipient *recipient = &recipients[i];
        recipient->actor = actors[i]; recipient->slot = player.source_slot;
        recipient->origin = qa_v3(state->pmove.origin_f[0] + state->viewoffset[0],
            state->pmove.origin_f[1] + state->viewoffset[1], state->pmove.origin_f[2] + state->viewoffset[2]);
        size_t written;
        if (ok) ok = qa_collision_point_leaf(owner->app->geometry, recipient->origin, &recipient->leaf, error) &&
            qa_collision_area_bits(owner->app->geometry, (int32_t)recipient->leaf.area,
                owner->area_bits[i], QA_Q2_MAX_AREABITS, &written, error);
        if (!ok) break;
        value.players[i].area_bits = (qa_bytes){owner->area_bits[i], written};
        uint32_t fat_leaves[64];
        qa_leaf_list fat;
        qa_bounds bounds = {qa_vec_sub(recipient->origin, qa_v3(8, 8, 8)), qa_vec_add(recipient->origin, qa_v3(8, 8, 8))};
        ok = qa_collision_box_leaves(owner->app->geometry, bounds, fat_leaves, 64, &fat, error);
        if (ok && !fat.count) ok = application_fail(error, QA_ERROR_FORMAT, "Q2 Source fat PVS has no geometry leaf");
        for (size_t j = 0; ok && j < fat.count; ++j) {
            qa_collision_leaf leaf;
            ok = qa_collision_leaf_at(owner->app->geometry, fat_leaves[j], &leaf, error);
            if (ok) recipient->clusters[recipient->cluster_count++] = (int32_t)leaf.cluster;
        }
    }
    if (ok) ok = application_network_q2_entities(owner, error);
    size_t count = 0;
    for (size_t i = 0; ok && i < owner->entity_count; ++i) {
        qa_q2_entity state = owner->entities[i];
        bool visible = false, owned = false;
        uint32_t hidden = 0;
        for (size_t j = 0; ok && j < seats; ++j) {
            bool seen = false, own = false;
            ok = source_visible(owner, &recipients[j], &state, leaves, leaf_capacity, &seen, &own, error);
            visible |= seen; owned |= own;
            if (!seen) hidden |= UINT32_C(1) << j;
        }
        if (ok && visible) {
            if (kex_fields(owner)) state.instance_bits = hidden;
            if (owned) state.solid = 0;
            owner->entities[count++] = state;
        }
    }
    free(leaves);
    if (ok && !qa_application_native_q2_presentation_current(owner->app, &owner->host.source))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Q2 frame changed its actual Source receipt during visibility");
    if (!ok) return false;
    owner->entity_count = count; value.entities = owner->entities; value.entity_count = count;
    *out = value;
    return true;
}
