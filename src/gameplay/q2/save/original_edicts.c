#include "original_edicts.h"
#include "internal.h"
#include "original_symbols.h"
#include "qa/text.h"

/* Original g_save field traversal, which also defines the string tail order. */
const q2_original_string_field q2_original_edict_strings[11] = {
    {"classname", 280}, {"model", 268}, {"target", 296}, {"targetname", 300},
    {"pathtarget", 312}, {"deathtarget", 316}, {"killtarget", 304},
    {"combattarget", 320}, {"message", 276}, {"team", 308}, {"map", 504}
};

static bool malformed(q2_original_record_io *io, size_t position, const char *message)
{
    qa_error_set(io->error, QA_ERROR_FORMAT, position, "%s", message);
    return false;
}

/* Source wait fields remain float seconds in both editions. */
bool q2_original_seconds(q2_original_record_io *io, const char *name,
    uint16_t offset, uint64_t *time)
{
    float value = io->reading ? 0 : (float)((double)*time / (double)Q2_NS);
    if (!q2_original_scalar(io, name, Q2_ORIGINAL_F32, offset, offset, offset, &value)) return false;
    if (io->reading) {
        /* Rerelease gtime_t::from_sec(float) truncates the float product to
         * integer milliseconds. Classic game DLLs retain float seconds. */
        uint64_t unit = io->edition == QA_Q2_RERELEASE ? Q2_MS : 1;
        double ticks = io->edition == QA_Q2_RERELEASE ? (double)(value * 1000.0f) :
            (double)value * (double)Q2_NS;
        if (!isfinite(ticks) || ticks < 0 || ticks >= (double)UINT64_MAX / (double)unit)
            return malformed(io, offset, "Original Q2 float-second deadline is invalid");
        *time = (uint64_t)ticks * unit;
    }
    return true;
}

bool q2_original_function(qa_q2_game *game, q2_original_record_io *io,
    const char *field, uint16_t offset, const char *name)
{
    if (io->reading) return malformed(io, offset, "Original Q2 callback writer requires its Source name");
    if (io->edition == QA_Q2_RERELEASE) {
        if (name) {
            qa_json_writer_key(io->writer, field);
            qa_json_writer_string(io->writer, name);
        }
        if (io->writer->failed) { if (io->error) *io->error = io->writer->failure; return false; }
        return true;
    }
    int32_t code = 0;
    return (!name || q2_original_symbol_encode(Q2_ORIGINAL_FUNCTION, game->options.product,
        name, &code, io->error)) &&
        q2_original_scalar(io, field, Q2_ORIGINAL_I32, offset, offset, offset, &code);
}

bool q2_original_function_matches(qa_q2_game *game, q2_original_record_io *io,
    const char *field, uint16_t offset, const char *name, bool *matches)
{
    if (!io->reading) return malformed(io, offset, "Original Q2 callback reader requires its Source file");
    if (io->edition == QA_Q2_RERELEASE) {
        qa_json_id id = qa_json_get(io->document, io->object, field);
        if (id == QA_JSON_NONE || qa_json_type(io->document, id) == QA_JSON_NULL) {
            *matches = name == NULL;
            return true;
        }
        qa_buffer text = {0};
        bool okay = qa_json_string(io->document, id, &text, io->error);
        if (okay) *matches = name && text.size == strlen(name) && !memcmp(text.data, name, text.size);
        qa_buffer_free(&text);
        return okay;
    }
    int32_t value = 0;
    if (!q2_original_scalar(io, field, Q2_ORIGINAL_I32, offset, offset, offset, &value)) return false;
    if (!name) { *matches = value == 0; return true; }
    int32_t expected;
    qa_error absent = {0};
    *matches = q2_original_symbol_encode(Q2_ORIGINAL_FUNCTION, game->options.product,
        name, &expected, &absent) && value == expected;
    return true;
}

bool q2_original_string(qa_q2_game *game, q2_original_record_io *io,
    const char *name, uint16_t offset, qa_string_id *value)
{
    if (io->edition == QA_Q2_RERELEASE)
        return q2_original_source_text(io, game, name, value, 65536);
    if (offset == UINT16_MAX) {
        if (io->reading) *value = QA_STRING_NONE;
        return true;
    }
    if (!io->reading) {
        qa_bytes text = *value ? qa_strings_text(qa_session_strings(game->services.session), *value) :
            (qa_bytes){0};
        if ((*value && !text.data) || text.size >= INT32_MAX || memchr(text.data, 0, text.size))
            return malformed(io, offset, "Original Q2 string has no terminated Source value");
        uint32_t size = text.data ? (uint32_t)text.size + 1 : 0;
        if (!q2_original_scalar(io, name, Q2_ORIGINAL_U32, offset, offset, offset, &size))
            return false;
        uint8_t zero = 0;
        return !size || (io->string_tail &&
            q2_save_raw(io->string_tail, (void *)text.data, text.size) &&
            q2_save_raw(io->string_tail, &zero, 1));
    }
    size_t position = 0;
    uint32_t length = 0;
    if (offset != 204) {
        bool found = false;
        for (size_t i = 0; i < sizeof(q2_original_edict_strings) / sizeof(q2_original_edict_strings[0]); ++i) {
            uint16_t member = q2_original_edict_strings[i].offset;
            if (member > io->input.size || 4 > io->input.size - member)
                return malformed(io, member, "Original Q2 string is outside its edict");
            uint32_t count = qa_load_u32le(io->input.data + member);
            if (count > INT32_MAX || position > io->strings.size || count > io->strings.size - position)
                return malformed(io, position, "Original Q2 string exceeds its saved tail");
            if (member == offset) { length = count; found = true; break; }
            position += count;
        }
        if (!found) return malformed(io, offset, "Original Q2 string has no Source field");
    } else {
        if (offset > io->input.size || 4 > io->input.size - offset)
            return malformed(io, offset, "Original Q2 changemap is outside its level");
        length = qa_load_u32le(io->input.data + offset);
    }
    if (!length) { *value = QA_STRING_NONE; return true; }
    if (length > INT32_MAX || position > io->strings.size || length > io->strings.size - position ||
        io->strings.data[position + length - 1] ||
        memchr(io->strings.data + position, 0, length - 1))
        return malformed(io, position, "Original Q2 string tail has an invalid terminator");
    return qa_strings_intern(qa_session_strings(game->services.session),
        (qa_bytes){io->strings.data + position, length - 1}, value, io->error);
}

static bool physical_reference(qa_q2_game *game, q2_original_record_io *io,
    const char *name, uint16_t offset, int32_t *number)
{
    if (io->edition == QA_Q2_RERELEASE) {
        if (!io->reading) {
            if (*number >= 0) { qa_json_writer_key(io->writer, name); qa_json_writer_number(io->writer, *number); }
            if (io->writer->failed) { if (io->error) *io->error = io->writer->failure; return false; }
            return true;
        }
        qa_json_id id = qa_json_get(io->document, io->object, name);
        *number = -1;
        if (id != QA_JSON_NONE && qa_json_type(io->document, id) != QA_JSON_NULL) {
            int64_t integer;
            if (!qa_json_i64(io->document, id, &integer, io->error)) return false;
            if (integer < 0 || (uint64_t)integer >= game->wire_capacity)
                return malformed(io, id, "Original Q2 pointer exceeds its physical edict table");
            *number = (int32_t)integer;
        }
    } else if (!q2_original_scalar(io, name, Q2_ORIGINAL_I32, offset, offset, offset, number)) return false;
    if (*number < -1 || (*number >= 0 && (uint32_t)*number >= game->wire_capacity))
        return malformed(io, offset, "Original Q2 pointer exceeds its physical edict table");
    return true;
}

bool q2_original_reference(qa_q2_game *game, q2_original_record_io *io,
    const char *name, uint16_t offset, qa_actor_id *actor)
{
    int32_t number = -1;
    if (!io->reading && actor->registry) {
        uint32_t slot;
        if (!qa_q2_wire_entity_number(game, *actor, &slot, io->error) || slot >= game->wire_capacity || slot > INT32_MAX)
            return malformed(io, offset, "Original Q2 pointer has no physical edict provenance");
        number = (int32_t)slot;
    }
    if (!physical_reference(game, io, name, offset, &number)) return false;
    if (io->reading) *actor = number < 0 ? (qa_actor_id){0} : game->wire_actors[(uint32_t)number];
    return true;
}

bool q2_original_source_reference(qa_q2_game *game, q2_original_record_io *io,
    const char *name, uint16_t offset, qa_actor_reference *reference)
{
    int32_t number = -1;
    if (!io->reading && reference->kind == QA_ACTOR_REFERENCE_SOURCE) {
        if (reference->value.source.owner != game->options.owner || reference->value.source.slot >= game->wire_capacity)
            return malformed(io, offset, "Original Q2 reference belongs to another physical Source");
        number = (int32_t)reference->value.source.slot;
    } else if (!io->reading && qa_actor_reference_present(*reference)) {
        qa_actor_id actor = reference->value.actor;
        uint32_t slot;
        if (!qa_q2_wire_entity_number(game, actor, &slot, io->error) || slot >= game->wire_capacity || slot > INT32_MAX)
            return malformed(io, offset, "Original Q2 reference has no physical edict provenance");
        number = (int32_t)slot;
    }
    if (!physical_reference(game, io, name, offset, &number)) return false;
    if (io->reading) *reference = number < 0 ? (qa_actor_reference){0} :
        qa_actor_reference_source(game->options.owner, (uint32_t)number);
    return true;
}

bool q2_original_body(qa_q2_game *game, q2_original_record_io *io, qa_body_state *body)
{
    return (io->references_only ||
        (q2_original_scalar(io, "s.origin", Q2_ORIGINAL_VECTOR, 4, 4, 4, &body->origin) &&
        q2_original_scalar(io, "s.angles", Q2_ORIGINAL_VECTOR, 16, 16, 16, &body->angles) &&
        q2_original_scalar(io, "velocity", Q2_ORIGINAL_VECTOR, 376, 376, 376, &body->velocity) &&
        q2_original_scalar(io, "mins", Q2_ORIGINAL_VECTOR, 188, 188, 188, &body->bounds.mins) &&
        q2_original_scalar(io, "maxs", Q2_ORIGINAL_VECTOR, 200, 200, 200, &body->bounds.maxs))) &&
        q2_original_source_reference(game, io, "groundentity", 552, &body->ground);
}


static bool physics_record(qa_q2_game *game, q2_original_record_io *io, q2_actor *actor)
{
    qa_physics_properties *physics = &actor->physics;
    if ((io->reading || (!actor->entity && !actor->monster && !(actor->item && actor->item->companion) && actor->projectile.kind == Q2_PROJECTILE_NONE)) &&
        (!q2_original_source_reference(game, io, "enemy", 540, &physics->enemy) ||
         !q2_original_source_reference(game, io, "goalentity", 412, &physics->goal))) return false;
    if (io->references_only) return true;
    static const qa_physics_motion motion[] = {QA_PHYSICS_STATIONARY, QA_PHYSICS_NOCLIP,
        QA_PHYSICS_PUSH, QA_PHYSICS_STOP, QA_PHYSICS_STEP, QA_PHYSICS_STEP,
        QA_PHYSICS_FLY, QA_PHYSICS_TOSS, QA_PHYSICS_FLY_MISSILE, QA_PHYSICS_BOUNCE,
        QA_PHYSICS_WALL_BOUNCE, QA_PHYSICS_NEW_TOSS};
    int32_t movetype = 0, solid = 0;
    if (!io->reading) {
        bool found = false;
        for (size_t i = 0; i < sizeof(motion) / sizeof(motion[0]); ++i)
            if (physics->motion == motion[i]) { movetype = (int32_t)i; found = true; break; }
        if (actor->client && !actor->client->info.noclip) movetype = 4;
        else if (physics->motion == QA_PHYSICS_STEP) movetype = 5;
        if (!found) return malformed(io, 260, "Q2 motion has no original movetype");
        solid = physics->solid == QA_PHYSICS_TRIGGER ? 1 : physics->solid == QA_PHYSICS_BOX ||
            physics->solid == QA_PHYSICS_CORPSE ? 2 : physics->solid == QA_PHYSICS_BRUSH ? 3 : 0;
    }
    float yaw_speed = actor->entity && actor->entity->kind == Q2E_EYE && actor->entity->q64 ?
        actor->entity->q64->vision_cone : physics->yaw_speed;
    if (!q2_original_scalar(io, "movetype", Q2_ORIGINAL_I32, 260, 260, 260, &movetype) ||
        !q2_original_scalar(io, "solid", Q2_ORIGINAL_I32, 248, 248, 248, &solid) ||
        !q2_original_scalar(io, "clipmask", Q2_ORIGINAL_U32, 252, 252, 252, &physics->clip_mask) ||
        !q2_original_scalar(io, "avelocity", Q2_ORIGINAL_VECTOR, 388, 388, 388, &physics->angular_velocity) ||
        !q2_original_scalar(io, "gravity", Q2_ORIGINAL_F32, 408, 408, 408, &physics->gravity_scale) ||
        !q2_original_scalar(io, "ideal_yaw", Q2_ORIGINAL_F32, 424, 424, 424, &physics->ideal_yaw) ||
        !q2_original_scalar(io, "yaw_speed", Q2_ORIGINAL_F32, 420, 420, 420, &yaw_speed) ||
        !q2_original_scalar(io, "watertype", Q2_ORIGINAL_I32, 608, 608, 608, &physics->water_type) ||
        !q2_original_scalar(io, "waterlevel", Q2_ORIGINAL_I32, 612, 612, 612, &physics->water_level)) return false;
    if (io->reading) {
        if (movetype < 0 || (size_t)movetype >= sizeof(motion) / sizeof(motion[0]) || solid < 0 || solid > 3)
            return malformed(io, 260, "Original Q2 physics has an invalid movetype or solid");
        physics->family = QA_COLLISION_Q2;
        physics->yaw_speed = yaw_speed;
        physics->q2_rerelease = io->edition == QA_Q2_RERELEASE;
        physics->motion = motion[movetype];
        physics->solid = solid == 1 ? QA_PHYSICS_TRIGGER : solid == 2 ? QA_PHYSICS_BOX :
            solid == 3 ? QA_PHYSICS_BRUSH : QA_PHYSICS_NOT_SOLID;
        if (io->edition == QA_Q2_RERELEASE &&
            qa_json_get(io->document, io->object, "gravity") == QA_JSON_NONE) physics->gravity_scale = 1;
        actor->physics_bound = true;
    }
    if (io->edition == QA_Q2_RERELEASE || io->product == QA_Q2_ROGUE) {
        if (!q2_original_scalar(io, "gravityVector", Q2_ORIGINAL_VECTOR,
            UINT16_MAX, UINT16_MAX, 1008, &physics->gravity_direction)) return false;
        if (io->reading && io->edition == QA_Q2_RERELEASE &&
            qa_json_get(io->document, io->object, "gravityVector") == QA_JSON_NONE)
            physics->gravity_direction = qa_v3(0, 0, -1);
    } else if (io->reading) physics->gravity_direction = qa_v3(0, 0, -1);
    return true;
}

static q2_actor *appearance_player(qa_q2_game *game, q2_actor *actor)
{
    if (actor->client) return actor;
    qa_actor_id owner = actor->entity ? actor->entity->owner : (qa_actor_id){0};
    if (actor->item && actor->item->companion) {
        q2_companion *companion = actor->item->companion;
        owner = qa_actor_reference_resolve(qa_session_actors(game->services.session), companion->credit);
        if (!owner.registry) {
            q2_actor *base = q2_actor_get(game, qa_actor_reference_resolve(qa_session_actors(game->services.session), companion->owner), false, NULL);
            if (base && base->item && base->item->companion) owner = qa_actor_reference_resolve(qa_session_actors(game->services.session), base->item->companion->owner);
        }
    }
    q2_actor *player = q2_actor_get(game, owner, false, NULL);
    return player && player->client ? player : NULL;
}

static bool original_player_model(qa_q2_game *game, q2_original_record_io *io,
    const qa_q2_save_level *engine, q2_actor *actor, uint32_t skin,
    size_t component, qa_string_id *model, int *native_skin)
{
    uint32_t slot = skin & 255u;
    if (slot >= game->wire_clients || !engine)
        return malformed(io, 60, "Original Q2 player model has no physical client skin");
    q2_actor *player = q2_actor_get(game, game->wire_actors[slot + 1], false, NULL);
    if (component == 0 && player && player->client && player->client->rule.visual.models[0]) {
        *model = player->client->rule.visual.models[0];
        *native_skin = player->client->rule.visual.skin;
        if (actor->client && actor->client->rule.corpse) actor->client->info.slot = slot;
        return true;
    }
    qa_q2_config_layout layout;
    if (!q2_original_config_layout(io->edition, engine, &layout, io->error)) return false;
    const char *config = qa_q2_save_configstring(engine, layout.player_skins + slot);
    const char *name = strchr(config, '\\');
    name = name ? name + 1 : "male/grunt";
    size_t length = strcspn(name, "/");
    if (!length || length > 255) return malformed(io, 60, "Original Q2 player skin has no model name");
    const char *member = "tris.md2";
    if (component) {
        member = "weapon.md2";
        uint32_t wanted = (skin >> 8) & 255u, ordinal = 0;
        for (uint32_t i = 1; i < layout.max_models; ++i) {
            const char *entry = qa_q2_save_configstring(engine, layout.models + i);
            if (entry[0] == '#' && ++ordinal == wanted) { member = entry + 1; break; }
        }
    }
    char path[384];
    int size = snprintf(path, sizeof(path), "players/%.*s/%s", (int)length, name, member);
    if (size < 0 || (size_t)size >= sizeof(path))
        return malformed(io, 60, "Original Q2 player model exceeds its content path");
    *native_skin = player && player->client ? player->client->rule.visual.skin : 0;
    return qa_builtin_resource(&game->services, path, model, io->error);
}

static bool visual_record(qa_q2_game *game, q2_original_record_io *io,
    q2_actor *actor, const qa_q2_save_level *engine, qa_entity_visual *visual)
{
    static const char *names[] = {"s.modelindex", "s.modelindex2", "s.modelindex3", "s.modelindex4"};
    q2_actor *player = appearance_player(game, actor);
    bool virtual_player = (actor->client && !actor->client->rule.gibbed) ||
        (actor->entity && actor->entity->kind == Q2E_CAMERA_DUMMY) ||
        (actor->item && actor->item->companion && actor->item->companion->kind == Q2_DOPPLEGANGER_BODY);
    int32_t skin = visual->skin;
    if (!io->reading && virtual_player && visual->models[0] && player) {
        const qa_q2_weapon_definition *weapon = player->weapon_bound ?
            qa_q2_weapon_definition_at(game, player->weapon.weapon) : NULL;
        uint32_t number = player->client->info.slot | (uint32_t)(weapon ? weapon->player_model : 0) << 8;
        memcpy(&skin, &number, sizeof(skin));
    }
    if (!q2_original_scalar(io, "s.skinnum", Q2_ORIGINAL_I32, 60, 60, 60, &skin)) return false;
    bool custom_skin = false;
    for (size_t i = 0; i < 4; ++i) {
        uint16_t offset = (uint16_t)(40 + i * 4);
        int32_t index = 0;
        if (io->reading || (virtual_player && i < 2)) {
            if (!io->reading) index = i == 0 ? visual->models[0] ? 255 : 0 :
                visual->models[0] && (visual->models[1] || (player && !player->client->info.dead && player->weapon_bound)) ? 255 : 0;
            if (!q2_original_scalar(io, names[i], Q2_ORIGINAL_I32, offset, offset, offset, &index)) return false;
            if (!io->reading) continue;
            if (index == 255 && i < 2) {
                if (!original_player_model(game, io, engine, actor, (uint32_t)skin, i,
                    visual->models + i, &visual->skin)) return false;
                custom_skin = true;
                continue;
            }
        }
        bool world = actor->wire_bound && actor->wire_slot == 0;
        bool beam = (actor->projectile.kind == Q2_BFG_LASER || actor->projectile.kind == Q2_RERELEASE_SPAWN_BEAM);
        if ((world || beam) && i == 0) {
            index = io->reading ? 0 : 1;
            if (!q2_original_scalar(io, names[i], Q2_ORIGINAL_I32, offset, offset, offset, &index)) return false;
            if (io->reading) {
                if (beam) {
                    if (index != 1) return malformed(io, offset, "Original BFG laser lacks MODELINDEX_WORLD");
                    visual->models[i] = 0;
                } else if (!qa_builtin_resource(&game->services, "*0", visual->models + i, io->error)) return false;
            }
        } else if (!q2_original_resource(game, io, engine, names[i], offset, offset, offset,
            32, visual->models + i)) return false;
    }
    if (io->reading && !custom_skin) visual->skin = skin;
    if (!q2_original_scalar(io, "s.frame", Q2_ORIGINAL_I32, 56, 56, 56, &visual->frame) ||
        !q2_original_scalar(io, "s.effects", Q2_ORIGINAL_U64, 64, 64, 64, &visual->effects) ||
        !q2_original_scalar(io, "s.renderfx", Q2_ORIGINAL_U32, 68, 68, 68, &visual->render_flags)) return false;
    if (io->reading) {
        visual->old_frame = visual->frame;
        visual->visible = true;
        visual->scale = visual->alpha = 1;
        if (actor->client) actor->client->rule.gibbed = !custom_skin && visual->models[0] && (visual->effects & 2u);
    }
    if (io->edition == QA_Q2_RERELEASE) {
        if (!q2_original_scalar(io, "s.alpha", Q2_ORIGINAL_F32,
            UINT16_MAX, UINT16_MAX, UINT16_MAX, &visual->alpha) ||
            !q2_original_scalar(io, "s.scale", Q2_ORIGINAL_F32,
            UINT16_MAX, UINT16_MAX, UINT16_MAX, &visual->scale)) return false;
        if (io->reading) {
            if (qa_json_get(io->document, io->object, "s.alpha") == QA_JSON_NONE) visual->alpha = 1;
            if (qa_json_get(io->document, io->object, "s.scale") == QA_JSON_NONE) visual->scale = 0;
        }
    }
    return true;
}

bool q2_original_edict_visual(qa_q2_game *game, q2_original_record_io *io,
    q2_actor *actor, const qa_q2_save_level *engine, qa_error *error)
{
    if (!io->reading || io->references_only) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Original Q2 final visual join requires decoded Source state");
        return false;
    }
    qa_entity_visual visual = {.alpha = 1, .scale = 1};
    (void)qa_q2_presentation_read(game, actor->id, &visual);
    if (!visual_record(game, io, actor, engine, &visual)) return false;
    uint32_t flags = 0;
    if (!q2_original_scalar(io, "svflags", Q2_ORIGINAL_U32, 184, 184, 184, &flags)) return false;
    visual.visible = (flags & 1u) == 0;
    if (actor->entity) actor->entity->visual = visual;
    if (actor->item) { actor->item->visual = visual; actor->item->visible = visual.visible; }
    if (actor->client) actor->client->rule.visual = visual;
    if (actor->projectile.kind != Q2_PROJECTILE_NONE) {
        actor->projectile.model = visual.models[0]; actor->projectile.frame = visual.frame;
        actor->projectile.skin = visual.skin; actor->projectile.effects = visual.effects;
        actor->projectile.render_flags = visual.render_flags; actor->projectile.alpha = visual.alpha;
        actor->projectile.scale = visual.scale; actor->projectile.visible = visual.visible;
        if (io->edition == QA_Q2_RERELEASE) {
            uint64_t entity_flags = 0;
            if (!q2_original_scalar(io, "flags", Q2_ORIGINAL_U64, 264, 264, 264, &entity_flags)) return false;
            actor->projectile.dodgeable = (entity_flags & (UINT64_C(1) << 25)) != 0;
        }
    }
    if (actor->monster) {
        actor->monster->model = visual.models[0]; actor->monster->frame = visual.frame;
        actor->monster->old_frame = visual.frame; actor->monster->skin = visual.skin;
        actor->monster->render_flags = visual.render_flags; actor->monster->entity_scale = visual.scale;
    }
    actor->alpha = visual.alpha;
    qa_string_id sound = 0;
    if (!(actor->entity && actor->entity->kind == Q2E_SPEAKER) &&
        !q2_original_resource(game, io, engine, "s.sound", 76, 76, 76, 288, &sound)) return false;
    if (actor->projectile.kind != Q2_PROJECTILE_NONE) actor->projectile.loop_sound = sound;
    else if (actor->client) actor->client->rule.loop_sound = sound;
    else if (actor->monster) actor->monster->weapon_sound = sound;
    else if (actor->item && actor->item->companion) actor->item->companion->loop_sound = sound;
    else if (actor->entity && actor->entity->kind != Q2E_SPEAKER) actor->entity->loop_sound = sound;
    return true;
}

bool q2_original_edict_record(qa_q2_game *game, q2_original_record_io *io,
    q2_actor *actor, const qa_q2_save_level *engine, qa_error *error)
{
    qa_body_state body;
    if (!qa_world_body_read(game->services.world, actor->id, &body, error) ||
        !q2_original_body(game, io, &body) || !physics_record(game, io, actor)) return false;
    if (io->references_only)
        return qa_world_body_write(game->services.world, actor->id, &body, error);
    if (actor->client) {
        int32_t maximum = io->reading ? 0 : actor->powers ?
            qa_source_float_to_i32(actor->powers->maximum_health) : 0;
        int32_t height = io->reading ? 0 : qa_source_float_to_i32(actor->client->info.view_height);
        int32_t dead = io->reading ? 0 : actor->client->info.dead ? 2 : 0;
        if (!q2_original_scalar(io, "max_health", Q2_ORIGINAL_I32, 484, 484, 484, &maximum) ||
            !q2_original_scalar(io, "viewheight", Q2_ORIGINAL_I32, 508, 508, 508, &height) ||
            !q2_original_scalar(io, "deadflag", Q2_ORIGINAL_I32, 492, 492, 492, &dead)) return false;
        if (io->reading) {
            actor->client->info.dead = dead != 0;
            actor->client->info.view_height = (float)height;
            if (actor->powers) actor->powers->maximum_health = (float)maximum;
        } else if (!q2_original_function(game, io, "pain", 452,
                actor->client->rule.corpse ? NULL : "player_pain") ||
            !q2_original_function(game, io, "die", 456,
                actor->client->rule.corpse ? actor->client->info.dead ? "body_die" : NULL : "player_die")) return false;
    }
    if (!io->reading && actor->wire_lifetime.link_count > UINT32_MAX)
        return malformed(io, 92, "Q2 Source link count exceeds its original field");
    uint32_t links = io->reading ? 0 : (uint32_t)actor->wire_lifetime.link_count;
    if (!q2_original_scalar(io, "linkcount", Q2_ORIGINAL_U32, 92, 92, 92, &links)) return false;
    if (io->reading) actor->wire_lifetime.link_count = links;
    uint32_t event = io->reading ? 0 : actor->wire_event_frame == game->wire_frame ? actor->wire_event : 0;
    if (!q2_original_scalar(io, "s.event", Q2_ORIGINAL_U32, 80, 80, 80, &event)) return false;
    if (io->reading) { actor->wire_event = event; actor->wire_event_frame = game->wire_frame; }
    bool beam = (actor->projectile.kind == Q2_BFG_LASER || actor->projectile.kind == Q2_RERELEASE_SPAWN_BEAM) ||
        (actor->entity && (actor->entity->kind == Q2E_LASER || actor->entity->scenery == Q2S_MAL_LASER));
    qa_vec3 previous = body.origin;
    if (!beam && !q2_original_scalar(io, "s.old_origin", Q2_ORIGINAL_VECTOR, 28, 28, 28, &previous)) return false;
    bool speaker = actor->entity && actor->entity->kind == Q2E_SPEAKER;
    qa_string_id sound = actor->projectile.kind != Q2_PROJECTILE_NONE ? actor->projectile.loop_sound :
        actor->client ? actor->client->rule.loop_sound : actor->monster ? actor->monster->weapon_sound :
        actor->item && actor->item->companion ? actor->item->companion->loop_sound :
        actor->entity ? actor->entity->loop_sound : 0;
    if (!speaker && !q2_original_resource(game, io, engine, "s.sound", 76, 76, 76, 288, &sound)) return false;
    if (io->reading) {
        if (actor->projectile.kind != Q2_PROJECTILE_NONE) actor->projectile.loop_sound = sound;
        else if (actor->client) actor->client->rule.loop_sound = sound;
        else if (actor->monster) actor->monster->weapon_sound = sound;
        else if (actor->item && actor->item->companion) actor->item->companion->loop_sound = sound;
    else if (actor->entity && actor->entity->kind != Q2E_SPEAKER) actor->entity->loop_sound = sound;
    }
    qa_combat_state combat = {0};
    bool has_combat = qa_combat_read(game->services.combat, actor->id, &combat, NULL);
    float source_health = has_combat ? combat.health : actor->entity ? actor->entity->health : 0;
    if (actor->entity && actor->entity->scenery == Q2S_CLOCK) source_health = (float)actor->entity->clock_value;
    int32_t health = io->reading ? 0 : qa_source_float_to_i32(source_health);
    int32_t mass = io->reading ? 0 : qa_source_float_to_i32(combat.mass);
    int32_t damage = io->reading ? 0 : combat.can_take_damage ?
        actor->item && actor->item->companion && actor->item->companion->kind == Q2_DOPPLEGANGER &&
            io->edition == QA_Q2_CLASSIC ? 2 : 1 : 0;
    uint64_t flags = io->reading ? 0 :
        (actor->physics.flags & QA_PHYSICS_FLYING ? 1u : 0) |
        (actor->physics.flags & QA_PHYSICS_SWIMMING ? 2u : 0) |
        (combat.invulnerable ? 16u : 0) | (actor->client && actor->client->info.notarget ? 32u : 0) |
        (actor->physics.flags & QA_PHYSICS_PARTIAL_GROUND ? 256u : 0) |
        (actor->physics.flags & QA_PHYSICS_TEAM_SLAVE ? 1024u : 0) |
        (combat.no_knockback ? 2048u : 0) | (combat.armor.powered.kind ? 4096u : 0) |
        (actor->environment_flags & 200u) | (actor->client && actor->client->rule.sphere_camera.registry ? 16384u : 0);
    qa_entity_visual visual = {.alpha = 1, .scale = 1};
    if (!io->reading) (void)qa_q2_presentation_read(game, actor->id, &visual);
    uint32_t svflags = io->reading ? 0 : (!visual.visible ? 1u : 0) |
        (actor->physics.flags & QA_PHYSICS_MONSTER ? 4u : 0) |
        (actor->physics.flags & QA_PHYSICS_DEAD ? 2u : 0);
    if (!io->reading && io->edition == QA_Q2_RERELEASE) {
        qa_actor_reference master = actor->item && (actor->item->think != Q2_ITEM_FLOOR || !actor->entity)
            ? actor->item->spawn.team_master : actor->entity ? actor->entity->team_master : (qa_actor_reference){0};
        bool teamed = (actor->item && actor->item->spawn.team) || (actor->entity && actor->entity->team);
        if (teamed && !(actor->physics.flags & QA_PHYSICS_TEAM_SLAVE) &&
            qa_actor_id_equal(qa_actor_reference_resolve(qa_session_actors(game->services.session), master), actor->id))
            flags |= UINT64_C(1) << 29;
        flags |= actor->physics.flags & QA_PHYSICS_ALWAYS_TOUCH ? UINT64_C(1) << 28 : 0;
        flags |= actor->character_no_damage_effects ? UINT64_C(1) << 20 : 0;
        if (actor->client && !actor->client->rule.corpse) svflags |= 8u;
        if (actor->item && game->options.cooperative && game->item_runtime->options.instanced_coop) svflags |= 256u;
        q2_projectile_kind kind = actor->projectile.kind;
        if (actor->projectile.dodgeable) flags |= UINT64_C(1) << 25;
        if (kind == Q2_GRENADE || kind == Q2_PROX || kind == Q2_TESLA || kind == Q2_TRAP) flags |= UINT64_C(1) << 32;
        if (kind == Q2_PROX || kind == Q2_TESLA || kind == Q2_TRAP) flags |= (UINT64_C(1) << 17) | (UINT64_C(1) << 13);
        if (kind == Q2_NUKE) flags |= UINT64_C(1) << 17;
        if (actor->entity && actor->entity->scenery == Q2S_BARREL) flags |= UINT64_C(1) << 32;
        if (kind == Q2_BOLT || kind == Q2_GREEN_BOLT || kind == Q2_BLUE_BOLT || kind == Q2_ROCKET ||
            kind == Q2_HEAT_ROCKET || kind == Q2_GRENADE || kind == Q2_BFG_BALL || kind == Q2_ION ||
            kind == Q2_PLASMA || kind == Q2_FLECHETTE || kind == Q2_TRACKER || kind == Q2_PROBOSCIS ||
            (kind == Q2_PROX && actor->projectile.phase == 0)) svflags |= 128u;
    } else if (!io->reading) {
        if (actor->projectile.kind == Q2_BOLT) svflags |= 2u;
        if (actor->projectile.kind == Q2_PROX || actor->projectile.kind == Q2_TESLA) flags |= 8192u;
        if (actor->projectile.kind == Q2_PROX || actor->projectile.kind == Q2_TESLA || actor->projectile.kind == Q2_NUKE) svflags |= 16u;
    }
    if ((!(actor->entity && actor->entity->kind == Q2E_DYNAMIC_LIGHT) &&
         !q2_original_scalar(io, "health", Q2_ORIGINAL_I32, 480, 480, 480, &health)) ||
        (actor->projectile.kind != Q2_TRAP &&
         !q2_original_scalar(io, "mass", Q2_ORIGINAL_I32, 400, 400, 400, &mass)) ||
        !q2_original_scalar(io, "takedamage", Q2_ORIGINAL_I32, 512, 512, 512, &damage) ||
        !q2_original_scalar(io, "flags", Q2_ORIGINAL_U64, 264, 264, 264, &flags) ||
        !q2_original_scalar(io, "svflags", Q2_ORIGINAL_U32, 184, 184, 184, &svflags)) return false;
    if (!io->reading && !visual_record(game, io, actor, engine, &visual)) return false;
    if (io->reading) {
        actor->environment_flags = (uint32_t)flags & 200u;
        if (actor->client) actor->client->rule.sphere_vehicle = (flags & 16384u) != 0;
        actor->physics.flags = (flags & 1u ? QA_PHYSICS_FLYING : 0) |
            (flags & 2u ? QA_PHYSICS_SWIMMING : 0) |
            (flags & 256u ? QA_PHYSICS_PARTIAL_GROUND : 0) |
            (flags & 1024u ? QA_PHYSICS_TEAM_SLAVE : 0) |
            (svflags & 4u ? QA_PHYSICS_MONSTER : 0) |
            (svflags & 2u ? QA_PHYSICS_DEAD : 0) |
            (io->edition == QA_Q2_RERELEASE && (flags & (UINT64_C(1) << 28)) ? QA_PHYSICS_ALWAYS_TOUCH : 0) |
            (actor->client && !actor->client->rule.corpse ? QA_PHYSICS_PLAYER : 0);
        actor->character_no_damage_effects = io->edition == QA_Q2_RERELEASE && (flags & (UINT64_C(1) << 20)) != 0;
        combat.health = (float)health; combat.mass = (float)mass;
        combat.can_take_damage = damage != 0; combat.invulnerable = (flags & 16u) != 0;
        combat.no_knockback = (flags & 2048u) != 0;
        const qa_actor_record *record = qa_actors_get(qa_session_actors(game->services.session), actor->id);
        const char *classname = record ? qa_strings_cstr(qa_session_strings(game->services.session), record->definition) : NULL;
        bool monster = classname && q2m_definition_for(game, classname);
        if ((has_combat || damage || actor->monster || monster) &&
            !(has_combat ? (qa_combat_set_health(game->services.combat, actor->id, combat.health, error) &&
                qa_combat_set_traits(game->services.combat, actor->id, &combat, error)) :
                qa_combat_create_actor(game->services.combat, actor->id, &combat, error))) return false;
        return qa_world_body_write(game->services.world, actor->id, &body, error);
    }
    return true;
}
