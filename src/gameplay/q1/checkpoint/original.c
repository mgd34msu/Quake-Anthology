#include "internal.h"
#include "../wire_internal.h"
#include "qa/game_q1_source_entities.h"
#include "qa/game_q1_composition.h"
#include "qa/q1_text.h"
#include "qa/text.h"
#include "qa/game_q1_travel.h"
#include "qa/game_q1_bots.h"
#include "qa/qc_text_save.h"
#include <stdio.h>
#include <stddef.h>

typedef enum original_storage {
    ORIGINAL_FLOAT, ORIGINAL_DOUBLE, ORIGINAL_I32, ORIGINAL_U32, ORIGINAL_I16,
    ORIGINAL_U16, ORIGINAL_U8, ORIGINAL_BOOL, ORIGINAL_VECTOR,
    ORIGINAL_STRING, ORIGINAL_ACTOR, ORIGINAL_REF
} original_storage;
typedef struct original_field {
    const char *name;
    original_storage storage;
    size_t offset;
} original_field;
static const char *const ammo_fields[QA_Q1_AMMO_COUNT] = {
    "ammo_shells", "ammo_nails", "ammo_rockets", "ammo_cells",
    "ammo_lava_nails", "ammo_multi_rockets", "ammo_plasma"
};
static const char *ammo_field(const qa_q1_game *game, unsigned index) {
    static const char *const rogue_base[] = {"ammo_shells1", "ammo_nails1", "ammo_rockets1", "ammo_cells1"};
    return game->options.program == QA_Q1_ROGUE && index < 4 ? rogue_base[index] : ammo_fields[index];
}
#define FIELD(type, member, name, storage) {name, ORIGINAL_##storage, offsetof(type, member)}
static const original_field entity_fields[] = {
    FIELD(q1_actor, classname, "classname", STRING),
    FIELD(q1_actor, model, "model", STRING),
    FIELD(q1_actor, frame, "frame", I32),
    FIELD(q1_actor, skin, "skin", I32),
    FIELD(q1_actor, effects, "effects", U32),
    FIELD(q1_actor, max_health, "max_health", FLOAT),
    FIELD(q1_actor, spawnflags, "spawnflags", U32),
    FIELD(q1_actor, target, "target", STRING),
    FIELD(q1_actor, targetname, "targetname", STRING),
    FIELD(q1_actor, owner, "owner", REF),
    FIELD(q1_actor, message, "message", STRING),
    FIELD(q1_actor, killtarget, "killtarget", STRING),
    FIELD(q1_actor, speed, "speed", FLOAT),
    FIELD(q1_actor, wait, "wait", FLOAT),
    FIELD(q1_actor, delay, "delay", FLOAT),
    FIELD(q1_actor, damage, "dmg", FLOAT),
    FIELD(q1_actor, count, "count", FLOAT),
    FIELD(q1_actor, source_netname, "netname", STRING),
    FIELD(q1_actor, source_death_type, "deathtype", STRING)
};
static const original_field body_queue_fields[] = {
    FIELD(q1_actor, state.body.color_map, "colormap", I32)
};
static const original_field combat_fields[] = {
    FIELD(qa_combat_state, health, "health", FLOAT),
    FIELD(qa_combat_state, armor.regular.points, "armorvalue", DOUBLE),
    FIELD(qa_combat_state, armor.regular.protection.q1_absorption, "armortype", FLOAT)
};
static const original_field body_fields[] = {
    FIELD(qa_body_state, origin, "origin", VECTOR),
    FIELD(qa_body_state, velocity, "velocity", VECTOR),
    FIELD(qa_body_state, angles, "angles", VECTOR),
    FIELD(qa_body_state, bounds.mins, "mins", VECTOR),
    FIELD(qa_body_state, bounds.maxs, "maxs", VECTOR),
    FIELD(qa_body_state, ground, "groundentity", REF)
};
static const original_field physics_fields[] = {
    FIELD(qa_physics_properties, angular_velocity, "avelocity", VECTOR),
    FIELD(qa_physics_properties, water_level, "waterlevel", I32),
    FIELD(qa_physics_properties, water_type, "watertype", I32),
    FIELD(qa_physics_properties, ideal_yaw, "ideal_yaw", FLOAT),
    FIELD(qa_physics_properties, yaw_speed, "yaw_speed", FLOAT),
    FIELD(qa_physics_properties, gravity_scale, "gravity", FLOAT),
    FIELD(qa_physics_properties, enemy, "enemy", REF),
    FIELD(qa_physics_properties, goal, "goalentity", REF),
    FIELD(qa_physics_properties, q1_pusher.local_seconds, "ltime", DOUBLE)
};
static const original_field player_fields[] = {
    FIELD(q1_player, weapon_frame, "weaponframe", I32),
    FIELD(q1_player, current_ammo, "currentammo", FLOAT),
    FIELD(q1_player, punch, "punchangle", VECTOR),
    FIELD(q1_player, input.view_angles, "v_angle", VECTOR),
    FIELD(q1_player, input.attack, "button0", BOOL),
    FIELD(q1_player, input.use, "button1", BOOL),
    FIELD(q1_player, input.jump, "button2", BOOL),
    FIELD(q1_player, source_impulse, "impulse", I32),
    FIELD(q1_player, source_frags, "frags", FLOAT),
    FIELD(q1_player, source_team, "team", FLOAT),
    FIELD(q1_player, input.teleport_until, "teleport_time", FLOAT),
    FIELD(q1_player, attack_finished, "attack_finished", DOUBLE),
    FIELD(q1_player, hostile_until, "show_hostile", DOUBLE),
    FIELD(q1_player, lightning_sound_at, "t_width", DOUBLE),
    FIELD(q1_player, mega_rot_at, "healthrot_nextcheck", DOUBLE),
    FIELD(q1_player, air_finished, "air_finished", DOUBLE),
    FIELD(q1_player, drown_damage, "dmg", FLOAT),
    FIELD(q1_player, hazard_at, "dmgtime", DOUBLE),
    FIELD(q1_player, power_expires[QA_Q1_QUAD], "super_damage_finished", DOUBLE),
    FIELD(q1_player, power_expires[QA_Q1_INVULNERABILITY], "invincible_finished", DOUBLE),
    FIELD(q1_player, power_expires[QA_Q1_INVISIBILITY], "invisible_finished", DOUBLE),
    FIELD(q1_player, power_expires[QA_Q1_SUIT], "radsuit_finished", DOUBLE),
    FIELD(q1_player, power_expires[QA_Q1_WETSUIT], "wetsuit_finished", DOUBLE),
    FIELD(q1_player, power_expires[QA_Q1_EMPATHY], "empathy_finished", DOUBLE),
    FIELD(q1_player, power_expires[QA_Q1_SHIELD], "shield_finished", DOUBLE),
    FIELD(q1_player, power_expires[QA_Q1_ANTIGRAV], "antigrav_finished", DOUBLE),
    FIELD(q1_player, power_expires[QA_Q1_LAVA_SUIT], "lavasuit_finished", DOUBLE),
    FIELD(q1_player, power_flash[QA_Q1_QUAD], "super_time", DOUBLE),
    FIELD(q1_player, power_flash[QA_Q1_INVULNERABILITY], "invincible_time", DOUBLE),
    FIELD(q1_player, power_flash[QA_Q1_INVISIBILITY], "invisible_time", DOUBLE),
    FIELD(q1_player, power_flash[QA_Q1_SUIT], "rad_time", DOUBLE),
    FIELD(q1_player, power_flash[QA_Q1_WETSUIT], "wetsuit_time", DOUBLE),
    FIELD(q1_player, power_flash[QA_Q1_EMPATHY], "empathy_time", DOUBLE),
    FIELD(q1_player, power_flash[QA_Q1_SHIELD], "shield_time", DOUBLE),
    FIELD(q1_player, power_flash[QA_Q1_ANTIGRAV], "antigrav_time", DOUBLE),
    FIELD(q1_player, power_flash[QA_Q1_LAVA_SUIT], "lavasuit_time", DOUBLE),
    FIELD(q1_player, scuba_at, "swim_flag", DOUBLE),
    FIELD(q1_player, shield_until, "shield_death_time", DOUBLE),
    FIELD(q1_player, shield_sound_at, "shieldSoundTime", DOUBLE)
};
static const original_field character_fields[] = {
    FIELD(q1_character, frame, "frame", I32),
    FIELD(q1_character, model, "model", STRING),
    FIELD(q1_character, view_offset, "view_ofs", VECTOR),
    FIELD(q1_character, walk_frame, "walkframe", U16),
    FIELD(q1_character, pain_until, "pain_finished", DOUBLE),
    FIELD(q1_character, fall_speed, "jump_flag", FLOAT)
};
static const original_field monster_fields[] = {
    FIELD(q1_monster, enemy, "enemy", REF),
    FIELD(q1_monster, old_enemy, "oldenemy", REF),
    FIELD(q1_monster, move_target, "movetarget", REF),
    FIELD(q1_monster, pause_until, "pausetime", DOUBLE),
    FIELD(q1_monster, attack_finished, "attack_finished", DOUBLE),
    FIELD(q1_monster, pain_finished, "pain_finished", DOUBLE),
    FIELD(q1_monster, search_until, "search_time", DOUBLE),
    FIELD(q1_monster, idle_until, "wait", DOUBLE),
    FIELD(q1_monster, hostile_until, "show_hostile", DOUBLE),
    FIELD(q1_monster, attack_state, "attack_state", U8),
    FIELD(q1_monster, lefty, "lefty", BOOL),
    FIELD(q1_monster, in_pain, "inpain", U8),
    FIELD(q1_monster, counter, "cnt", U32)
};
static const original_field eel_fields[] = {
    FIELD(q1_monster, source.eel.pitch, "weapon", I16)
};
static const original_field scourge_fields[] = {
    FIELD(q1_monster, source.scourge.trigger, "lastvictim", REF),
    FIELD(q1_monster, source.scourge.dodge_until, "duration", DOUBLE),
    FIELD(q1_monster, source.scourge.initialized, "state", BOOL),
    FIELD(q1_monster, source.scourge.silent, "spawnsilent", BOOL),
    FIELD(q1_monster, source.scourge.previous_silent, "spawnmulti", BOOL)
};
static const original_field map_fields[] = {
    FIELD(q1_map_state, map, "map", STRING),
    FIELD(q1_map_state, noise[0], "noise", STRING),
    FIELD(q1_map_state, noise[1], "noise1", STRING),
    FIELD(q1_map_state, noise[2], "noise2", STRING),
    FIELD(q1_map_state, noise[3], "noise3", STRING),
    FIELD(q1_map_state, mdl, "mdl", STRING),
    FIELD(q1_map_state, movedir, "movedir", VECTOR),
    FIELD(q1_map_state, mangle, "mangle", VECTOR),
    FIELD(q1_map_state, height, "height", FLOAT),
    FIELD(q1_map_state, lip, "lip", FLOAT),
    FIELD(q1_map_state, width, "t_width", FLOAT),
    FIELD(q1_map_state, length, "t_length", FLOAT),
    FIELD(q1_map_state, pause_time, "pausetime", FLOAT),
    FIELD(q1_map_state, sounds, "sounds", I32),
    FIELD(q1_map_state, style, "style", I32),
    FIELD(q1_map_state, color_map, "colormap", I32)
};
static const original_field world_fields[] = {
    FIELD(qa_q1_options, world_type, "worldtype", I32)
};
static const original_field game_globals[] = {
    FIELD(qa_q1_game, force_retouch, "force_retouch", U32),
    FIELD(qa_q1_game, total_monsters, "total_monsters", U32),
    FIELD(qa_q1_game, killed_monsters, "killed_monsters", U32),
    FIELD(qa_q1_game, enemy_range, "enemy_range", U8),
    FIELD(qa_q1_game, sight_actor, "sight_entity", REF),
    FIELD(qa_q1_game, sight_time, "sight_entity_time", DOUBLE),
    FIELD(qa_q1_game, hellknight_melee, "hknight_type", U32),
    FIELD(qa_q1_game, forward.x, "v_forward_x", FLOAT),
    FIELD(qa_q1_game, forward.y, "v_forward_y", FLOAT),
    FIELD(qa_q1_game, forward.z, "v_forward_z", FLOAT),
    FIELD(qa_q1_game, up.x, "v_up_x", FLOAT),
    FIELD(qa_q1_game, up.y, "v_up_y", FLOAT),
    FIELD(qa_q1_game, up.z, "v_up_z", FLOAT),
    FIELD(qa_q1_game, right.x, "v_right_x", FLOAT),
    FIELD(qa_q1_game, right.y, "v_right_y", FLOAT),
    FIELD(qa_q1_game, right.z, "v_right_z", FLOAT)
};
static const original_field map_globals[] = {
    FIELD(q1_map_runtime, total_secrets, "total_secrets", U32),
    FIELD(q1_map_runtime, found_secrets, "found_secrets", U32),
    FIELD(q1_map_runtime, lightning_end, "lightning_end", DOUBLE),
    FIELD(q1_map_runtime, electrodes[0], "le1", REF),
    FIELD(q1_map_runtime, electrodes[1], "le2", REF)
};
static const original_field enemy_globals[] = {
    FIELD(qa_q1_game, enemy_visible, "enemy_vis", BOOL),
    FIELD(qa_q1_game, enemy_visible, "enemy_visible", BOOL)
};
static const original_field server_globals[] = {{"serverflags", ORIGINAL_U32, 0}};
#undef FIELD
static original_field body_queue_global(const qa_q1_game *game,char name[32]) {
    snprintf(name,32,"%s_head",q1_body_queue_classname(game));
    return (original_field){name,ORIGINAL_REF,offsetof(qa_q1_game,body_queue_head)};
}
static bool fail(qa_error *error, const char *text) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", text); return false;
}
static bool number(qa_q1_save_record *record, const char *key, double value,
    bool required, qa_error *error) {
    float source = (float)value; uint32_t bits; memcpy(&bits, &source, 4);
    if (!required && !bits) return true;
    qa_q1_save_value field = {.kind = QA_Q1_SAVE_FLOAT, .value.number = source};
    return qa_q1_save_record_value(record, key, &field, error);
}
static bool vector(qa_q1_save_record *record, const char *key, qa_vec3 value,
    qa_error *error) {
    uint32_t bits[3]; memcpy(bits, &value, sizeof(bits));
    if (!(bits[0] | bits[1] | bits[2])) return true;
    qa_q1_save_value field = {.kind = QA_Q1_SAVE_VECTOR, .value.vector = value};
    return qa_q1_save_record_value(record, key, &field, error);
}
static bool text(qa_q1_save_record *record, const char *key, const char *value,
    qa_q1_save_value_kind kind, qa_error *error) {
    if (!value || !*value) return true;
    qa_q1_save_value field = {.kind = kind, .value.text = value};
    return qa_q1_save_record_value(record, key, &field, error);
}
static bool actor_id(qa_q1_wire_receipt *receipt, qa_q1_save_record *record,
    const char *key, qa_actor_id id, bool required, qa_error *error) {
    uint32_t slot = 0;
    if (id.registry && !qa_q1_wire_actor_slot(receipt, id, &slot)) {
        const q1_wire_state *wire = receipt->operation.game->wire;
        uint32_t candidate = receipt->client_slots + 1;
        while (candidate < receipt->entity_slots &&
            (!wire->edicts[candidate].free || !qa_actor_id_equal(wire->edicts[candidate].released,id))) ++candidate;
        if (candidate == receipt->entity_slots)
            return fail(error, "Original field refers outside its physical Source edicts");
        slot = candidate;
    }
    if (!required && !slot) return true;
    qa_q1_save_value field = {.kind = QA_Q1_SAVE_ENTITY, .value.entity = slot};
    return qa_q1_save_record_value(record, key, &field, error);
}
static bool actor(qa_q1_wire_receipt *receipt, qa_q1_save_record *record,
    const char *key, q1_ref reference, bool required, qa_error *error) {
    uint32_t slot = 0;
    if (reference.kind == QA_ACTOR_REFERENCE_SOURCE) {
        if (reference.value.source.owner != receipt->operation.game->options.provider ||
            reference.value.source.slot >= receipt->entity_slots)
            return fail(error, "Original reference leaves its actual physical Source edicts");
        slot = reference.value.source.slot;
    } else if (reference.kind == QA_ACTOR_REFERENCE_LIFETIME)
        return actor_id(receipt, record, key, reference.value.actor, required, error);
    if (!required && !slot) return true;
    qa_q1_save_value field = {.kind = QA_Q1_SAVE_ENTITY, .value.entity = slot};
    return qa_q1_save_record_value(record, key, &field, error);
}
static bool fields(qa_q1_wire_receipt *receipt, qa_q1_save_record *record,
    const void *owner, const original_field *table, size_t count, bool required, qa_error *error) {
    const qa_strings *strings = qa_session_strings(receipt->operation.game->services.session);
    for (size_t i = 0; i < count; ++i) {
        const original_field *field = table + i;
        const uint8_t *p = (const uint8_t *)owner + field->offset;
        bool okay;
        switch (field->storage) {
        case ORIGINAL_FLOAT: okay = number(record, field->name, *(const float *)p, required, error); break;
        case ORIGINAL_DOUBLE: okay = number(record, field->name, *(const double *)p, required, error); break;
        case ORIGINAL_I32: okay = number(record, field->name, *(const int32_t *)p, required, error); break;
        case ORIGINAL_I16: okay = number(record, field->name, *(const int16_t *)p, required, error); break;
        case ORIGINAL_U32: okay = number(record, field->name, *(const uint32_t *)p, required, error); break;
        case ORIGINAL_U16: okay = number(record, field->name, *(const uint16_t *)p, required, error); break;
        case ORIGINAL_U8: okay = number(record, field->name, *p, required, error); break;
        case ORIGINAL_BOOL: okay = number(record, field->name, *(const bool *)p, required, error); break;
        case ORIGINAL_VECTOR: okay = vector(record, field->name, *(const qa_vec3 *)p, error); break;
        case ORIGINAL_STRING: {
            qa_string_id id = *(const qa_string_id *)p;
            const char *value = id ? qa_strings_cstr(strings, id) : NULL;
            okay = (!id || value) && text(record, field->name, value, QA_Q1_SAVE_STRING, error); break;
        }
        case ORIGINAL_ACTOR: okay = actor_id(receipt, record, field->name, *(const qa_actor_id *)p, required, error); break;
        case ORIGINAL_REF: okay = actor(receipt, record, field->name, *(const q1_ref *)p, required, error); break;
        default: okay = false; break;
        }
        if (!okay) return false;
    }
    return true;
}
#define FIELDS(receipt, record, owner, table, error) \
    fields(receipt, record, owner, table, sizeof(table) / sizeof(*(table)), false, error)

#define GLOBAL_FIELDS(receipt, record, owner, table, error) \
    fields(receipt, record, owner, table, sizeof(table) / sizeof(*(table)), true, error)

typedef struct original_map_callback { q1_map_action action; const char *name; } original_map_callback;
static const original_map_callback map_callbacks[] = {
    {Q1_MAP_REARM, "multi_wait"}, {Q1_MAP_REMOVE, "SUB_Remove"},
    {Q1_MAP_DELAYED_USE, "DelayThink"}, {Q1_MAP_BEGIN_LEVEL, "execute_changelevel"},
    {Q1_MAP_BARREL_EXPLODE, "barrel_explode"}, {Q1_MAP_MOVE_DONE, "SUB_CalcMoveDone"},
    {Q1_MAP_DOOR_TOP, "door_hit_top"}, {Q1_MAP_DOOR_BOTTOM, "door_hit_bottom"},
    {Q1_MAP_DOOR_DOWN, "door_go_down"}, {Q1_MAP_BUTTON_TOP, "button_wait"},
    {Q1_MAP_BUTTON_BOTTOM, "button_done"}, {Q1_MAP_BUTTON_RETURN, "button_return"},
    {Q1_MAP_SECRET_FIRST, "fd_secret_move1"}, {Q1_MAP_SECRET_SECOND, "fd_secret_move2"},
    {Q1_MAP_SECRET_TOP, "fd_secret_move3"}, {Q1_MAP_SECRET_RETURN, "fd_secret_move4"},
    {Q1_MAP_SECRET_LAST_WAIT, "fd_secret_move5"}, {Q1_MAP_SECRET_LAST, "fd_secret_move6"},
    {Q1_MAP_SECRET_BOTTOM, "fd_secret_done"}, {Q1_MAP_PLAT_TOP, "plat_hit_top"},
    {Q1_MAP_PLAT_BOTTOM, "plat_hit_bottom"}, {Q1_MAP_PLAT_DOWN, "plat_go_down"},
    {Q1_MAP_TRAIN_FIND, "func_train_find"}, {Q1_MAP_TRAIN_NEXT, "train_next"},
    {Q1_MAP_TRAIN_WAIT, "train_wait"}, {Q1_MAP_SIGIL_PLACE, "StartItem"},
    {Q1_MAP_SHOOTER_FIRE, "shooter_think"}, {Q1_MAP_FIREBALL_FLY, "fire_fly"},
    {Q1_MAP_BUBBLES_MAKE, "make_bubbles"}, {Q1_MAP_NOISE_REPEAT, "noise_think"},
    {Q1_MAP_LIGHTNING_FIRE, "lightning_fire"}
};
static const char *map_think(q1_map_action action) {
    if (action == Q1_MAP_IDLE) return NULL;
    for (size_t i = 0; i < sizeof(map_callbacks) / sizeof(*map_callbacks); ++i)
        if (map_callbacks[i].action == action) return map_callbacks[i].name;
    return NULL;
}
typedef struct original_think_callback { q1_think_kind kind; const char *name; } original_think_callback;
static const original_think_callback think_callbacks[] = {
    {Q1_THINK_REMOVE, "SUB_Remove"}, {Q1_THINK_EXPLODE, "GrenadeExplode"},
    {Q1_THINK_VORE, "ShalHome"}, {Q1_THINK_SPRITE, "s_explode1"},
    {Q1_THINK_WIZARD, "Wiz_FastFire"}, {Q1_THINK_RESPAWN, "SUB_regen"},
    {Q1_THINK_MEGA_ROT, "item_megahealth_rot"}, {Q1_THINK_ITEM_PLACE, "PlaceItem"},
    {Q1_THINK_DEATH_BUBBLES, "DeathBubblesSpawn"}, {Q1_THINK_BUBBLE, "bubble_bob"},
    {Q1_THINK_HIP_LASER, "HIP_LaserThink"}, {Q1_THINK_PROX_WATCH, "ProximityBomb"},
    {Q1_THINK_PROX_EXPLODE, "ProximityExplode"},
    {Q1_THINK_WRATH_HOME, "WrathHome"}, {Q1_THINK_SCOURGE_TRIGGER, "ScourgeTriggerThink"},
    {Q1_THINK_MULTI_SPLIT, "MultiGrenadeThink"}, {Q1_THINK_MINI_EXPLODE, "MiniGrenadeExplode"},
    {Q1_THINK_MULTI_EXPLODE, "MultiGrenadeExplode"}, {Q1_THINK_MULTI_ACQUIRE, "MultiRocketThink"},
    {Q1_THINK_MULTI_HOME, "MultiRocketThink"}, {Q1_THINK_PLASMA_LAUNCH, "PlasmaThink"},
    {Q1_THINK_TELEPORT_FOG, "SUB_Remove"}
};
static const char *entity_think(const q1_actor *entity) {
    if (entity->think == Q1_THINK_NONE) return NULL;
    if (entity->think == Q1_THINK_MAP) return entity->map ? map_think(entity->map->action) : NULL;
    if (entity->think == Q1_THINK_MONSTER_FRAME)
        return entity->state.monster.next_frame < q1_frame_count ?
            q1_frames[entity->state.monster.next_frame].name : NULL;
    if (entity->think == Q1_THINK_MONSTER_START)
        return entity->physics.flags & QA_PHYSICS_FLYING ? "flymonster_start_go" :
            entity->physics.flags & QA_PHYSICS_SWIMMING ? "swimmonster_start_go" : "walkmonster_start_go";
    if (entity->think == Q1_THINK_MONSTER_FOUND) return "FoundTarget";
    if (entity->think == Q1_THINK_SPRITE) {
        static const char *const frames[] = {"s_explode2","s_explode3","s_explode4",
            "s_explode5","s_explode6","SUB_Remove"};
        return entity->frame >= 0 && entity->frame < 6 ? frames[entity->frame] : NULL;
    }
    for (size_t i = 0; i < sizeof(think_callbacks) / sizeof(*think_callbacks); ++i)
        if (think_callbacks[i].kind == entity->think) return think_callbacks[i].name;
    return NULL;
}
static bool callback(qa_q1_save_record *record, const char *key, const char *name, qa_error *error) {
    return text(record, key, name, QA_Q1_SAVE_FUNCTION, error);
}
static bool source_order(const qa_qc_program *program, qa_q1_save_record *record,
    bool global, qa_error *error) {
    qa_qc_program_info info = qa_qc_program_describe(program);
    qa_q1_save_record ordered = {0};
    uint32_t count = global ? info.global_count : info.field_count;
    ordered.pairs = count ? calloc(count, sizeof(*ordered.pairs)) : NULL;
    if (count && !ordered.pairs) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Ordering original Source definitions"); return false;
    }
    bool okay = true;
    for (uint32_t i = global ? 0 : 1; okay && i < count; ++i) {
        const qa_qc_definition *definition = global ? qa_qc_program_global(program, i) : qa_qc_program_field(program, i);
        if (global && (!definition->save || (definition->type != QA_QC_FLOAT &&
            definition->type != QA_QC_STRING && definition->type != QA_QC_ENTITY))) continue;
        size_t length = strlen(definition->name);
        if (!global && length >= 2 && definition->name[length - 2] == '_') continue;
        qa_q1_save_pair *pair = NULL;
        for (size_t j = 0; j < record->count; ++j)
            if (record->pairs[j].key && !strcmp(record->pairs[j].key, definition->name)) pair = record->pairs + j;
        if (pair) {
            if (definition->type == QA_QC_FUNCTION &&
                !qa_qc_program_find_function(program, pair->value, NULL)) {
                qa_error_set(error, QA_ERROR_FORMAT, i, "Native callback %s is absent from actual Source progs", pair->value);
                okay = false; break;
            }
            ordered.pairs[ordered.count++] = *pair; *pair = (qa_q1_save_pair){0};
        } else if (global) {
            int32_t word;
            if (!qa_qc_program_initial_int(program, definition->offset, &word, error)) { okay = false; break; }
            qa_q1_save_value value = {0};
            if (definition->type == QA_QC_FLOAT) {
                value.kind = QA_Q1_SAVE_FLOAT; memcpy(&value.value.number, &word, 4);
            } else if (definition->type == QA_QC_STRING) {
                if (word) { okay = fail(error, "Original initialized string needs its actual Source literal"); break; }
                value.kind = QA_Q1_SAVE_STRING; value.value.text = "";
            } else {
                if (word) { okay = fail(error, "Original initialized entity lacks its physical Source row"); break; }
                value.kind = QA_Q1_SAVE_ENTITY; value.value.entity = 0;
            }
            qa_q1_save_record missing = {0};
            okay = qa_q1_save_record_value(&missing, definition->name, &value, error);
            if (okay) { ordered.pairs[ordered.count++] = missing.pairs[0]; free(missing.pairs); }
        }
    }
    qa_q1_save_record_destroy(record);
    if (!okay) { qa_q1_save_record_destroy(&ordered); return false; }
    *record = ordered; return true;
}
static bool map_functions(const q1_actor *entity, qa_q1_save_record *record, qa_error *error) {
    const q1_map_state *map = entity->map; const char *touch = NULL, *use = NULL, *blocked = NULL;
    switch (map->kind) {
    case Q1_MAP_MULTI: touch = "multi_touch"; use = "multi_use"; break;
    case Q1_MAP_COUNTER: use = "counter_use"; break;
    case Q1_MAP_RELAY: use = "SUB_UseTargets"; break;
    case Q1_MAP_TELEPORT: touch = "teleport_touch"; use = "teleport_use"; break;
    case Q1_MAP_HURT: touch = "hurt_touch"; break;
    case Q1_MAP_PUSH: touch = "trigger_push_touch"; break;
    case Q1_MAP_CHANGELEVEL: touch = "changelevel_touch"; break;
    case Q1_MAP_SETSKILL: touch = "trigger_skill_touch"; break;
    case Q1_MAP_REGISTERED: touch = "trigger_onlyregistered_touch"; break;
    case Q1_MAP_MONSTERJUMP: touch = "trigger_monsterjump_touch"; break;
    case Q1_MAP_LIGHT: use = "light_use"; break;
    case Q1_MAP_WALL: case Q1_MAP_GATE: use = "func_wall_use"; break;
    case Q1_MAP_PATH: touch = "t_movetarget"; break;
    case Q1_MAP_LIGHTNING: use = "lightning_use"; break;
    case Q1_MAP_BARREL: break;
    case Q1_MAP_DOOR: touch = "door_touch"; use = "door_use"; blocked = "door_blocked"; break;
    case Q1_MAP_BUTTON: touch = "button_touch"; use = "button_use"; break;
    case Q1_MAP_SECRET_DOOR: touch = "secret_touch"; use = "fd_secret_use"; blocked = "secret_blocked"; break;
    case Q1_MAP_PLAT: use = map->dormant ? "plat_use" : "SUB_Null"; blocked = "plat_crush"; break;
    case Q1_MAP_TRAIN: case Q1_MAP_TRAIN2: use = "train_use"; blocked = "train_blocked"; break;
    case Q1_MAP_DOOR_TRIGGER: touch = "door_trigger_touch"; break;
    case Q1_MAP_PLAT_TRIGGER: touch = "plat_center_touch"; break;
    case Q1_MAP_SIGIL: touch = "sigil_touch"; break;
    case Q1_MAP_SHOOTER: use = "spikeshooter_use"; break;
    case Q1_MAP_FIREBALL: touch = "fire_touch"; break;
    default:
        if (map->touch_enabled || map->use_enabled)
            return fail(error, "Original native map callback has no Source field projection");
        break;
    }
    return callback(record, "touch", map->touch_enabled ? touch : NULL, error) &&
        callback(record, "use", map->use_enabled ? use : NULL, error) && callback(record, "blocked", blocked, error);
}
static bool monster_functions(const q1_monster *monster, qa_q1_save_record *record, qa_error *error) {
    const q1_species *species = monster->species;
    static const char *const pain[] = {"army_pain", "dog_pain", "knight_pain", "enf_pain",
        "demon1_pain", "ogre_pain", "hknight_pain", "sham_pain", "Wiz_Pain", "shalrath_pain",
        NULL, "fish_pain", "zombie_pain", NULL, "nopain",
        [QA_Q1_EEL] = "eel_pain1", [QA_Q1_SWORD] = "sword_pain", [QA_Q1_WRATH] = "wrath_pain",
        [QA_Q1_SUPER_WRATH] = "overlord_pain", [QA_Q1_MUMMY] = "mummy_pain",
        [QA_Q1_SCOURGE] = "scourge_pain"};
    static const char *const die[] = {"army_die", "dog_die", "knight_die", "enf_die", "demon_die",
        "ogre_die", "hknight_die", "sham_die", "wiz_die", "shalrath_die", "tbaby_die1",
        "f_death1", "zombie_die", NULL, "finale_1",
        [QA_Q1_EEL] = "eel_death", [QA_Q1_SWORD] = "sword_die", [QA_Q1_WRATH] = "wrath_die02",
        [QA_Q1_SUPER_WRATH] = "overlord_die02", [QA_Q1_MUMMY] = "mummy_die",
        [QA_Q1_SCOURGE] = "scourge_die"};
    static const char *const melee[] = {NULL, "dog_atta1", "knight_atk1", NULL, "Demon_MeleeAttack",
        "ogre_melee", "hknight_melee", "sham_melee", NULL, NULL, "tbaby_jump1", "f_attack1",
        [QA_Q1_EEL] = "eel_attack1", [QA_Q1_SWORD] = "sword_atk1",
        [QA_Q1_SUPER_WRATH] = "overlord_melee", [QA_Q1_SCOURGE] = "scourge_melee"};
    if (!species || (species->species > QA_Q1_OLDONE &&
        species->species != QA_Q1_EEL && species->species != QA_Q1_SWORD &&
        species->species != QA_Q1_WRATH && species->species != QA_Q1_SUPER_WRATH &&
        species->species != QA_Q1_MUMMY && species->species != QA_Q1_SCOURGE))
        return fail(error, "Original native expansion monster callbacks require their Source projection");
    unsigned index = (unsigned)species->species;
    if (species->species == QA_Q1_BOSS) return callback(record,"use","boss_awake",error);
    if (species->species == QA_Q1_OLDONE)
        return callback(record,"th_pain","nopain",error) && callback(record,"th_die","finale_1",error);
    const char *missile = species->species == QA_Q1_WIZARD ? "Wiz_Missile" :
        species->species == QA_Q1_ZOMBIE ? "zombie_missile" : species->missile;
    const char *touch = NULL;
    if (monster->jump_touch) {
        touch = species->species == QA_Q1_DOG ? "Dog_JumpTouch" :
            species->species == QA_Q1_DEMON ? "Demon_JumpTouch" :
            species->species == QA_Q1_TARBABY ? "Tar_JumpTouch" : NULL;
        if (!touch) return fail(error, "Original monster contact has no compiled Source callback");
    }
    bool sleeping = species->species == QA_Q1_MUMMY && monster->source.mummy.asleep;
    const char *run = species->species == QA_Q1_SWORD && !monster->source.sword.awakened ?
        "sword_pause" : species->run;
    const char *hurt = species->species == QA_Q1_SWORD && monster->source.sword.pain_disabled ?
        "SUB_Null" : pain[index];
    return callback(record, "th_stand", sleeping ? "mummy_sleep" : species->stand, error) &&
        callback(record, "th_walk", sleeping ? "mummy_wake" : species->walk, error) &&
        callback(record, "th_run", sleeping ? "mummy_wake" : run, error) &&
        callback(record, "th_missile", sleeping ? "mummy_wake" : missile, error) &&
        callback(record, "th_melee", index < sizeof(melee) / sizeof(*melee) ? melee[index] : NULL, error) &&
        callback(record, "th_pain", sleeping ? "mummy_wake" : hurt, error) && callback(record, "th_die", die[index], error) &&
        callback(record, "touch", touch, error) &&
        callback(record, "use", q1_ref_present(monster->enemy) || monster->dead ? "SUB_Null" : "monster_use", error);
}

typedef struct original_player_animation {
    uint16_t first, count;
    const char *prefix;
    bool attack;
} original_player_animation;
static const original_player_animation player_animations[] = {
    {29,6,"player_axpain",false}, {35,6,"player_pain",false},
    {103,2,"player_nail",true}, {105,2,"player_light",true},
    {107,6,"player_rocket",true}, {113,6,"player_shot",true},
    {119,4,"player_axe",true}, {125,4,"player_axeb",true},
    {131,4,"player_axec",true}, {137,4,"player_axed",true}
};
static bool player_functions(const q1_player *player, qa_q1_save_record *record, qa_error *error) {
    const q1_character *c = &player->character_state;
    const char *think = c->locomotion == 2 ? "player_run" : "player_stand1";
    char name[48]; double next = c->next_animation;
    if (c->life != QA_Q1_ALIVE || c->pose.custom_model)
        return fail(error, "Original player animation requires its living stock Source model");
    for (size_t i = 0; i < sizeof(player_animations) / sizeof(*player_animations); ++i) {
        const original_player_animation *a = player_animations + i;
        if (!c->animation.count || c->animation.first != a->first) continue;
        unsigned frame = c->animation_frame + 2;
        if (player->continuous) {
            frame = c->frame == a->first ? 2 : 1; next = player->next_weapon_frame;
        }
        if (frame <= a->count) { snprintf(name, sizeof(name), "%s%u", a->prefix, frame); think = name; }
        else think = "player_run";
        break;
    }
    return callback(record, "think", think, error) && number(record, "nextthink", next, false, error) &&
        callback(record, "th_pain", "player_pain", error) && callback(record, "th_die", "PlayerDie", error) &&
        number(record, "deadflag", 0, false, error);
}
typedef struct original_projectile {
    q1_projectile_kind kind;
    const char *classname, *touch;
    qa_q1_weapon weapon;
    const char *native_classname;
} original_projectile;
static const original_field hip_laser_fields[] = {
    {"lastvictim", ORIGINAL_REF, offsetof(q1_projectile, activator)},
    {"old_velocity", ORIGINAL_VECTOR, offsetof(q1_projectile, movedir)},
    {"attack_finished", ORIGINAL_DOUBLE, offsetof(q1_projectile, expires)},
    {"cnt", ORIGINAL_U32, offsetof(q1_projectile, count)},
    {"dmg", ORIGINAL_FLOAT, offsetof(q1_projectile, damage)}
};
static const original_field proximity_fields[] = {
    {"lastvictim", ORIGINAL_REF, offsetof(q1_projectile, activator)},
    {"spawnmaster", ORIGINAL_REF, offsetof(q1_projectile, surface)},
    {"delay", ORIGINAL_DOUBLE, offsetof(q1_projectile, expires)},
    {"state", ORIGINAL_U32, offsetof(q1_projectile, count)}
};
static const original_projectile projectiles[] = {
    {Q1_SPIKE,"spike","spike_touch",QA_Q1_NAILGUN,"spike"},
    {Q1_SUPERSPIKE,"spike","superspike_touch",QA_Q1_SUPER_NAILGUN,"superspike"},
    {Q1_ROCKET,"missile","T_MissileTouch",QA_Q1_ROCKET,"missile"},
    {Q1_GRENADE,"grenade","GrenadeTouch",QA_Q1_GRENADE,"grenade"},
    {Q1_WIZARD_SPIKE,"wizspike","spike_touch",QA_Q1_WEAPON_COUNT,"wizard_spike"},
    {Q1_KNIGHT_SPIKE,"knightspike","spike_touch",QA_Q1_WEAPON_COUNT,"knight_spike"},
    {Q1_ENFORCER_LASER,"","Laser_Touch",QA_Q1_WEAPON_COUNT,"enforcer_laser"},
    {Q1_OGRE_GRENADE,"","OgreGrenadeTouch",QA_Q1_WEAPON_COUNT,"ogre_grenade"},
    {Q1_ZOMBIE_GRENADE,"","ZombieGrenadeTouch",QA_Q1_WEAPON_COUNT,"zombie_grenade"},
    {Q1_VORE_BALL,"","ShalMissileTouch",QA_Q1_WEAPON_COUNT,"vore_ball"},
    {Q1_LAVA_BALL,"","T_MissileTouch",QA_Q1_WEAPON_COUNT,"chthon_lavaball"},
    {Q1_HIP_LASER,"hiplaser","HIP_LaserTouch",QA_Q1_LASER,"hiplaser"},
    {Q1_PROXIMITY,"proximity_grenade","ProximityGrenadeTouch",QA_Q1_PROXIMITY,"proximity_grenade"},
    {Q1_WRATH_MISSILE,"","WrathMissileTouch",QA_Q1_WEAPON_COUNT,"wrath_missile"}
};
static bool projectile_fields(qa_q1_wire_receipt *receipt, const q1_actor *entity,
    qa_q1_save_record *record, qa_error *error) {
    const q1_projectile *p = &entity->state.projectile;
    const original_projectile *source = NULL;
    for (size_t i = 0; i < sizeof(projectiles) / sizeof(*projectiles); ++i)
        if (projectiles[i].kind == p->kind) source = projectiles + i;
    if (!source) return fail(error, "Original projectile requires its actual Source callback projection");
    if (p->kind == Q1_HIP_LASER && !FIELDS(receipt,record,p,hip_laser_fields,error)) return false;
    if (p->kind == Q1_PROXIMITY &&
        (!FIELDS(receipt,record,p,proximity_fields,error) ||
         !callback(record,"th_die","ProximityGrenadeExplode",error) ||
         (p->detonating && !text(record,"deathtype","exploding",QA_Q1_SAVE_STRING,error)))) return false;
    return text(record, "classname", source->classname, QA_Q1_SAVE_STRING, error) &&
        callback(record, "touch", entity->think == Q1_THINK_SPRITE ? "SUB_Null" :
            entity->touch_disabled ? NULL : p->remove_touch ? "SUB_Remove" : source->touch, error) &&
        actor(receipt, record, "enemy", p->enemy, false, error) &&
        actor(receipt, record, "owner", entity->owner, false, error);
}
static bool pickup_fields(qa_q1_wire_receipt *receipt, const q1_actor *entity,
    qa_q1_save_record *record, qa_error *error) {
    const q1_pickup *p = &entity->state.pickup;
    static const char *const touches[] = {"health_touch","armor_touch","ammo_touch", "weapon_touch",
        "key_touch","powerup_touch","BackpackTouch"};
    if (p->kind >= sizeof(touches) / sizeof(*touches))
        return fail(error, "Original item requires its actual Source callback projection");
    const qa_strings *strings = qa_session_strings(receipt->operation.game->services.session);
    if (!callback(record, "touch", touches[p->kind], error) ||
        !text(record, "mdl", qa_strings_cstr(strings, p->original_model), QA_Q1_SAVE_STRING, error) ||
        !text(record, "noise", qa_strings_cstr(strings, p->sound), QA_Q1_SAVE_STRING, error)) return false;
    if (p->kind == 0 && !number(record, "healamount", p->count, false, error)) return false;
    if (p->kind == 2 && !number(record, "aflag", p->count, false, error)) return false;
    if (p->kind == 0 && !number(record,"healtype",p->mega?2:p->count==25?1:0,false,error)) return false;
    if (q1_ref_present(p->holder) && !actor(receipt,record,"owner",p->holder,false,error)) return false;
    if (p->kind == 6) {
        uint32_t bit = 0;
        for (unsigned i = 0; i < 32; ++i) { qa_q1_weapon weapon;
            if (qa_q1_weapon_source(receipt->operation.game->options.program, UINT32_C(1) << i, &weapon) &&
                weapon == p->weapon) bit = UINT32_C(1) << i;
        }
        if (!number(record, "items", bit, false, error)) return false;
        for (unsigned i = 0; i < QA_Q1_AMMO_COUNT; ++i)
            if (!number(record, ammo_field(receipt->operation.game,i), p->ammo[i], false, error)) return false;
    }
    return true;
}
static bool mover_sounds(const q1_actor *entity, qa_q1_save_record *record, qa_error *error) {
    switch (entity->map->kind) {
    case Q1_MAP_DOOR:
        return text(record,"noise1",q1_map_door_sound(entity,false),QA_Q1_SAVE_STRING,error) &&
            text(record,"noise2",q1_map_door_sound(entity,true),QA_Q1_SAVE_STRING,error);
    case Q1_MAP_SECRET_DOOR:
        return text(record,"noise1",q1_map_secret_first_sound(entity),QA_Q1_SAVE_STRING,error) &&
            text(record,"noise2",q1_map_secret_sound(entity,true),QA_Q1_SAVE_STRING,error) &&
            text(record,"noise3",q1_map_secret_sound(entity,false),QA_Q1_SAVE_STRING,error);
    case Q1_MAP_BUTTON:
        return text(record,"noise",q1_map_button_sound(entity),QA_Q1_SAVE_STRING,error);
    case Q1_MAP_PLAT:
        return text(record,"noise",q1_map_plat_sound(entity,true),QA_Q1_SAVE_STRING,error) &&
            text(record,"noise1",q1_map_plat_sound(entity,false),QA_Q1_SAVE_STRING,error);
    default: return true;
    }
}
static bool sprite_remove(const qa_q1_game *game, const q1_actor *entity) {
    if (entity->kind != Q1_TIMER || entity->think != Q1_THINK_REMOVE) return false;
    const char *model = qa_strings_cstr(qa_session_strings(game->services.session), entity->model);
    return model && !strcmp(model,"progs/s_explod.spr");
}
static const char *sprite_classname(const qa_q1_game *game, const q1_actor *entity) {
    const char *name = qa_strings_cstr(qa_session_strings(game->services.session), entity->classname);
    if (!name || !strcmp(name,"explosion")) return NULL;
    for (size_t i=0; i<sizeof(projectiles)/sizeof(*projectiles); ++i)
        if (!strcmp(name,projectiles[i].native_classname)) return projectiles[i].classname;
    return name;
}
static bool entity_capture(qa_q1_wire_receipt *receipt, q1_actor *entity,
    q1_player *player, const qa_movement_state *movement, qa_q1_save_record *record, qa_error *error) {
    qa_q1_game *game = receipt->operation.game; qa_body_state body; qa_combat_state combat;
    qa_actor_id id = player ? player->id : entity->id;
    qa_physics_properties physics;
    if (entity) physics = entity->physics;
    else if (!game->services.physics || !game->services.physics->services.read ||
        !game->services.physics->services.read(game->services.physics->services.context, id, &physics))
        return fail(error, "Original player lost its physical Source movement owner");
    if (!qa_world_body_read(game->services.world, id, &body, error) ||
        !qa_combat_read(game->services.combat, id, &combat, error)) return false;
    q1_actor source_entity;
    if (entity) {
        source_entity = *entity;
        if (entity->kind == Q1_PROJECTILE || entity->think == Q1_THINK_WIZARD ||
            entity->think == Q1_THINK_DEATH_BUBBLES || entity->think == Q1_THINK_SCOURGE_TRIGGER ||
            ((entity->think == Q1_THINK_SPRITE || sprite_remove(game,entity)) && !entity->map)) source_entity.classname = 0;
        if (entity->think == Q1_THINK_BUBBLE || entity->think == Q1_THINK_DEATH_BUBBLES) source_entity.count = 0;
        if (entity->think == Q1_THINK_SCOURGE_TRIGGER) source_entity.delay = 0;
    }
    if ((entity && !FIELDS(receipt, record, &source_entity, entity_fields, error)) ||
        !FIELDS(receipt, record, &body, body_fields, error) || !FIELDS(receipt, record, &physics, physics_fields, error)) return false;
    if (entity && entity->kind != Q1_PROJECTILE && !entity->map &&
        (entity->think == Q1_THINK_SPRITE || sprite_remove(game,entity)) &&
        !text(record,"classname",sprite_classname(game,entity),QA_Q1_SAVE_STRING,error)) return false;
    static const int motion[] = {0, 8, 7, 7, 6, 6, 10, 11, 5, 9, 4};
    static const int solid[] = {0, 1, 2, 4, 2};
    uint32_t flags; bool found;
    if (player) { flags = movement->data.nq.flags; found = true; }
    else if (!qa_q1_source_movement_flags_read(game, id, &flags, &found, error) || !found)
        return fail(error, "Original edict has no physical Source flag owner");
    qa_q1_character_view visible = {0};
    if (player && !qa_q1_character_read(game,player->id,&visible))
        return fail(error, "Original player lost its actual Source character");
    uint32_t model = 0;
    qa_string_id model_name = player ? visible.model : entity->model;
    if (model_name && !qa_q1_wire_index(receipt, true, model_name, &model))
        return fail(error, "Original model is outside its real Source precache");
    if (!number(record, "movetype", player ? movement->data.nq.move_type : motion[physics.motion], false, error) ||
        !number(record, "solid", player ? 3 : solid[physics.solid], false, error) ||
        !number(record, "flags", flags, false, error) || !number(record, "modelindex", model, false, error) ||
        !FIELDS(receipt, record, &combat, combat_fields, error) ||
        !number(record, "takedamage", combat.can_take_damage ? player || entity->aimed_damage ? 2 : 1 : 0, false, error) ||
        !vector(record, "size", qa_vec_sub(body.bounds.maxs, body.bounds.mins), error)) return false;
    if (player) {
        qa_q1_wire_player wire;
        q1_player source_player = *player;
        source_player.mega_rot_at = fmax(0, source_player.mega_rot_at);
        for (unsigned i=0; i<QA_Q1_POWER_COUNT; ++i)
            if (player->power_order[i] && player->power_expires[i] != 0 &&
                player->power_flash[i] == 0 && !(player->power_warned & (1u << i)))
                source_player.power_flash[i] = 1;
        q1_character source_character = player->character_state;
        source_character.model = visible.model; source_character.frame = visible.frame;
        source_character.pain_until = fmax(source_character.pain_until,player->drown_at);
        if (!FIELDS(receipt, record, &source_player, player_fields, error) ||
            !FIELDS(receipt, record, &source_character, character_fields, error) ||
            !qa_q1_wire_player_read(receipt, player->id, &wire, error)) return false;
        if (!text(record, "classname", "player", QA_Q1_SAVE_STRING, error) ||
            !number(record, "max_health", player->max_health, false, error) ||
            !number(record, "colormap", player->client_slot + 1, false, error) ||
            !number(record, "idealpitch", movement->data.nq.ideal_pitch, false, error) ||
            !vector(record,"movedir",movement->data.nq.water_jump_direction,error)) return false;
        uint32_t items = wire.weapons | wire.powers | wire.ammo_items;
        static const char *const keys[] = {"q1:key/silver", "q1:key/gold"};
        for (unsigned i = 0; i < 2; ++i) {
            qa_string_id key = qa_strings_find(qa_session_strings(game->services.session),
                (qa_bytes){(const uint8_t *)keys[i],strlen(keys[i])});
            double count = 0;
            if (key && !qa_inventory_count_read(game->services.inventory,player->id,key,&count,error)) return false;
            if (count != 0) items |= 131072u << i;
        }
        if (combat.armor.regular.points > 0 && game->options.program != QA_Q1_ROGUE)
            items |= combat.armor.regular.protection.q1_absorption >= .8f ? 32768u :
                combat.armor.regular.protection.q1_absorption >= .6f ? 16384u : 8192u;
        uint32_t extra_items = wire.extra_items >> 23;
        if (game->options.program == QA_Q1_ROGUE) {
            if (combat.armor.regular.points > 0)
                extra_items |= combat.armor.regular.protection.q1_absorption >= .8f ? 4u :
                    combat.armor.regular.protection.q1_absorption >= .6f ? 2u : 1u;
            if (player->power_expires[QA_Q1_ANTIGRAV] > game->time) extra_items |= 128u;
        } else if (game->options.program == QA_Q1_ID1 &&
                   game->options.edition == QA_Q1_RERELEASE && player->mega_rot_at >= 0)
            items |= 65536u;
        double ammo[QA_Q1_AMMO_COUNT] = {0};
        unsigned ammo_count = game->options.program == QA_Q1_ROGUE ? QA_Q1_AMMO_COUNT : 4;
        for (unsigned i=0; i<ammo_count; ++i) {
            if (!qa_inventory_count_read(game->services.inventory,player->id,game->ammo[i],ammo+i,error) ||
                !number(record,ammo_field(game,i),ammo[i],false,error)) return false;
        }
        if (game->options.program == QA_Q1_ROGUE) {
            static const unsigned powered[] = {0,QA_Q1_LAVA_NAILS,QA_Q1_MULTI_ROCKETS,QA_Q1_PLASMA_CELLS};
            for (unsigned i=0; i<4; ++i)
                if (!number(record,ammo_fields[i],ammo[wire.weapon>=4096u?powered[i]:i],false,error)) return false;
        }
        if (!number(record, "items", items, false, error) || !number(record,"items2",extra_items,false,error) ||
            !number(record, "weapon", wire.weapon, false, error) ||
            !text(record, "weaponmodel", qa_strings_cstr(qa_session_strings(game->services.session), wire.weapon_model),
                QA_Q1_SAVE_STRING, error)) return false;
        const char *name=NULL;
        if (!qa_q1_source_client_info(game,player->id,"name",&name) ||
            !text(record,"netname",name?name:"",QA_Q1_SAVE_STRING,error)) return false;
        return player_functions(player, record, error);
    }
    const char *think = entity->think == Q1_THINK_EXPLODE &&
        entity->kind == Q1_PROJECTILE && entity->state.projectile.kind == Q1_OGRE_GRENADE ?
        "OgreGrenadeExplode" : entity_think(entity);
    if (entity->think != Q1_THINK_NONE && !think)
        return fail(error, "Original native think has no exact Source callback projection");
    if (!callback(record, "think", think, error) || !number(record, "nextthink", entity->next_think, false, error)) return false;
    if ((entity->think == Q1_THINK_SPRITE || sprite_remove(game,entity)) && entity->kind != Q1_PROJECTILE &&
        !callback(record,"touch","SUB_Null",error)) return false;
    if (entity->think == Q1_THINK_WIZARD &&
        (!actor(receipt,record,"enemy",entity->state.projectile.enemy,false,error) ||
         !vector(record,"movedir",entity->state.projectile.right,error))) return false;
    if (entity->think == Q1_THINK_BUBBLE && !number(record,"cnt",entity->count,false,error)) return false;
    if (entity->think == Q1_THINK_DEATH_BUBBLES &&
        !number(record,"bubble_count",entity->count,false,error)) return false;
    if (entity->think == Q1_THINK_SCOURGE_TRIGGER &&
        (!actor(receipt,record,"lastvictim",entity->activator,false,error) ||
         !number(record,"duration",entity->delay,false,error) ||
         !callback(record,"touch",entity->touch_disabled ? NULL : "ScourgeTriggerTouch",error))) return false;
    if (entity->kind == Q1_BODY && !FIELDS(receipt,record,entity,body_queue_fields,error)) return false;
    if (entity->kind == Q1_PROJECTILE && !projectile_fields(receipt, entity, record, error)) return false;
    if (entity->kind == Q1_PICKUP && !pickup_fields(receipt, entity, record, error)) return false;
    if (entity->kind == Q1_MONSTER) {
        qa_builtin_actor_traits traits;
        if (!game->services.actor_traits || !game->services.actor_traits(game->services.context,entity->id,&traits) ||
            !vector(record,"view_ofs",qa_v3(0,0,traits.view_height),error)) return false;
        if (entity->state.monster.species->species == QA_Q1_EEL &&
            !FIELDS(receipt, record, &entity->state.monster, eel_fields, error)) return false;
        if (entity->state.monster.species->species == QA_Q1_SCOURGE &&
            !FIELDS(receipt,record,&entity->state.monster,scourge_fields,error)) return false;
        return FIELDS(receipt, record, &entity->state.monster, monster_fields, error) &&
            monster_functions(&entity->state.monster, record, error);
    }
    if (entity->map) {
        if (entity->map->kind == Q1_MAP_BARREL && !callback(record,"th_die","barrel_explode",error)) return false;
        if (entity->map->kind == Q1_MAP_WORLD && !FIELDS(receipt,record,&game->options,world_fields,error)) return false;
        if (entity->map->action == Q1_MAP_DELAYED_USE &&
            !actor(receipt,record,"enemy",entity->activator,false,error)) return false;
        if (!FIELDS(receipt, record, entity->map, map_fields, error) ||
            !mover_sounds(entity,record,error) || !map_functions(entity, record, error)) return false;
        if (q1_map_is_mover(entity->map->kind)) {
            const q1_map_movement *move = &entity->map->pending.mover;
            static const unsigned state[] = {1, 2, 0, 3};
            if (entity->map->kind == Q1_MAP_DOOR && move->group && move->group->count) {
                size_t member = 0;
                while (member < move->group->count && !q1_ref_equal(move->group->members[member],q1_ref_from(game,entity->id))) ++member;
                if (member == move->group->count) return fail(error,"Source door lost its actual retained group");
                if (!actor(receipt,record,"owner",move->group->members[0],false,error) ||
                    !actor(receipt,record,"enemy",move->group->members[(member+1)%move->group->count],false,error)) return false;
            }
            if (!vector(record, "pos1", move->pos1, error) || !vector(record, "pos2", move->pos2, error) ||
                !vector(record, "dest1", move->dest1, error) || !vector(record, "dest2", move->dest2, error) ||
                !vector(record, "finaldest", move->destination, error) ||
                !number(record, "state", state[move->position], false, error) ||
                !callback(record, "think1", map_think(move->done), error)) return false;
        }
    }
    return true;
}
bool qa_q1_game_original_capture(qa_q1_game *game, const qa_qc_program *program,
    const qa_movement_state *movement, qa_q1_save_data *save, qa_error *error) {
    if (!game || !save || save->entities || save->entity_count || save->globals.count ||
        game->destroy_pending || game->continuation_pending || game->observation_depth ||
        game->options.quakeworld || game->options.max_clients != 1 || game->options.deathmatch ||
        !program || !movement || movement->kind != QA_MOVEMENT_NETQUAKE ||
        !game->wire || game->wire->loading || !game->maps ||
        !qa_session_safe(game->services.session) || !qa_world_idle(game->services.world))
        return fail(error, "Original native capture requires its idle single-player Source");
    if (!q1_body_queue_validate(game,error)) return false;
    if (game->maps->finale_started || qa_q1_level_read(game->maps->options.level)->intermission)
        return fail(error, "Cannot save an original Quake game in intermission");
    qa_q1_wire_receipt receipt = {0};
    if (!qa_q1_wire_read_begin(game, &receipt, error)) return false;
    save->time = (float)game->time;
    save->entity_count = game->wire->next_dynamic;
    save->entities = calloc(save->entity_count, sizeof(*save->entities));
    bool okay = save->entities != NULL;
    if (!okay) qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating original physical Source edicts");
    for (unsigned i = 0; okay && i < 64; ++i) {
        const char *pattern = qa_strings_cstr(qa_session_strings(game->services.session), game->wire->lightstyles[i]);
        if (!pattern || !*pattern) pattern = "m";
        size_t size = strlen(pattern); save->lightstyles[i] = malloc(size + 1);
        if (!save->lightstyles[i]) { qa_error_set(error, QA_ERROR_MEMORY, i, "Retaining Source lightstyle"); okay = false; }
        else memcpy(save->lightstyles[i], pattern, size + 1);
    }
    qa_q1_save_record *globals = &save->globals;
    if (okay) okay = number(globals, "time", game->time, true, error) &&
        number(globals, "frametime", game->elapsed, true, error) &&
        text(globals, "mapname", save->map, QA_Q1_SAVE_STRING, error) &&
        number(globals, "deathmatch", game->options.deathmatch, true, error) &&
        number(globals, "coop", game->options.coop, true, error) &&
        number(globals, "teamplay", game->options.teamplay, true, error) &&
        number(globals, "skill", game->options.skill, true, error) &&
        GLOBAL_FIELDS(&receipt, globals, game, game_globals, error) &&
        GLOBAL_FIELDS(&receipt, globals, game->maps, map_globals, error) &&
        GLOBAL_FIELDS(&receipt, globals, game->maps->options.server_flags, server_globals, error) &&
        fields(&receipt, globals, game, enemy_globals + (game->options.edition == QA_Q1_RERELEASE), 1, true, error);
    char queue_name[32];original_field queue_global=body_queue_global(game,queue_name);
    if (okay) okay=fields(&receipt,globals,game,&queue_global,1,true,error);
    uint32_t eyes = 0, model_player = 0;
    if (okay) okay = qa_q1_wire_index(&receipt, true, game->eyes_model, &eyes) &&
        qa_q1_wire_index(&receipt, true, game->player_model, &model_player) &&
        number(globals, "modelindex_eyes", eyes, true, error) && number(globals, "modelindex_player", model_player, true, error);
    for (unsigned i = 0; okay && i < 16; ++i) {
        char name[16]; snprintf(name, sizeof(name), "parm%u", i + 1);
        okay = number(globals, name, save->spawn_parameters[i], true, error);
    }
    if (okay && game->options.program == QA_Q1_HIPNOTIC) {
        uint32_t mines = 0;
        for (uint32_t i=0; i<game->capacity; ++i) {
            const q1_actor *entity = game->actors[i];
            if (entity && entity->active && entity->kind == Q1_PROJECTILE &&
                entity->state.projectile.kind == Q1_PROXIMITY && !entity->state.projectile.detonating)
                ++mines;
        }
        okay = number(globals,"NumProximityGrenades",mines,true,error);
    }
    if (okay) okay = source_order(program, globals, true, error);
    for (uint32_t slot = 0; okay && slot < save->entity_count; ++slot) {
        qa_actor_id id;
        if (!qa_q1_wire_actor_at(&receipt, slot, &id)) continue;
        q1_actor *entity = q1_entity(game, id); q1_player *player = q1_player_get(game, id);
        if (!entity && !player) { okay = fail(error, "Original physical Source row lacks its native state"); break; }
        if (entity && entity->think == Q1_THINK_AXE) continue;
        okay = entity_capture(&receipt, entity, player, movement, save->entities + slot, error) &&
            source_order(program, save->entities + slot, false, error);
    }
    qa_q1_wire_read_end(&receipt);
    return okay;
}

static const char *saved(const qa_q1_save_record *record, const char *name) {
    const char *value = NULL;
    for (size_t i = 0; i < record->count; ++i)
        if (!strcmp(record->pairs[i].key, name)) value = record->pairs[i].value;
    return value;
}
static float saved_number(const qa_q1_save_record *record, const char *name) {
    const char *value = saved(record, name);
    double number = 0;
    if (value) (void)qa_parse_atof(value, &number, NULL);
    return (float)number;
}
static bool saved_vector(const char *value, qa_vec3 *out, qa_error *error) {
    return qa_q1_save_vector_decode(value ? value : "", out, error);
}
static bool saved_actor(const char *value, const qa_actor_id *slots, size_t count,
    qa_actor_id *out, qa_error *error) {
    uint32_t slot;
    if (!qa_q1_save_entity_decode(value ? value : "0", &slot, error) || slot >= count)
        return fail(error, "Original reference exceeds its actual physical edict extent");
    if (slot && !slots[slot].registry) {
        qa_error_set(error,QA_ERROR_UNSUPPORTED,slot,"Original physical reference requires its free Source edict custody");
        return false;
    }
    *out = slot ? slots[slot] : (qa_actor_id){0};
    return true;
}
static bool saved_ref(qa_q1_game *game, const char *value, size_t count,
    q1_ref *out, qa_error *error) {
    uint32_t slot;
    if (!qa_q1_save_entity_decode(value ? value : "0", &slot, error) || slot >= count)
        return fail(error, "Original reference exceeds its actual physical edict extent");
    *out = q1_ref_source(game, slot);
    return true;
}
static bool restore_fields(qa_strings *strings, qa_actor_owner source, const qa_q1_save_record *record,
    void *owner, const original_field *table, size_t count,
    const qa_actor_id *slots, size_t slot_count, qa_error *error) {
    for (size_t i = 0; i < count; ++i) {
        const original_field *field = table + i;
        uint8_t *p = (uint8_t *)owner + field->offset;
        const char *value = saved(record, field->name);
        double parsed = 0;
        if (value && !qa_parse_atof(value, &parsed, error)) return false;
        float number = (float)parsed;
        switch (field->storage) {
        case ORIGINAL_FLOAT: *(float *)p = number; break;
        case ORIGINAL_DOUBLE: *(double *)p = number; break;
        case ORIGINAL_I16:
            if (!isfinite(number) || number < INT16_MIN || number > INT16_MAX)
                return fail(error, "Original signed field exceeds its native Source domain");
            *(int16_t *)p = (int16_t)number; break;
        case ORIGINAL_I32:
            if (!isfinite(number) || number < INT32_MIN || number >= 2147483648.0)
                return fail(error, "Original integer field exceeds its native Source domain");
            *(int32_t *)p = (int32_t)number; break;
        case ORIGINAL_U32:
            if (!isfinite(number) || number < 0 || number >= 4294967296.0)
                return fail(error, "Original unsigned field exceeds its native Source domain");
            *(uint32_t *)p = (uint32_t)number; break;
        case ORIGINAL_U16:
            if (!isfinite(number) || number < 0 || number > UINT16_MAX)
                return fail(error, "Original frame exceeds its native Source domain");
            *(uint16_t *)p = (uint16_t)number; break;
        case ORIGINAL_U8:
            if (!isfinite(number) || number < 0 || number > UINT8_MAX)
                return fail(error, "Original state exceeds its native Source domain");
            *p = (uint8_t)number; break;
        case ORIGINAL_BOOL: *(bool *)p = number != 0; break;
        case ORIGINAL_VECTOR: if (!saved_vector(value, (qa_vec3 *)p, error)) return false; break;
        case ORIGINAL_ACTOR: if (!saved_actor(value, slots, slot_count, (qa_actor_id *)p, error)) return false; break;
        case ORIGINAL_REF: {
            uint32_t slot;
            if (!qa_q1_save_entity_decode(value ? value : "0", &slot, error) || slot >= slot_count)
                return fail(error, "Original reference exceeds its actual physical edict extent");
            *(q1_ref *)p = qa_actor_reference_source(source, slot); break;
        }
        case ORIGINAL_STRING: {
            qa_string_id id = 0;
            if (value && *value) {
                char *decoded = NULL;
                bool okay = qa_q1_save_string_decode(value, &decoded, error) &&
                    qa_strings_intern_cstr(strings, decoded, &id, error);
                free(decoded); if (!okay) return false;
            }
            *(qa_string_id *)p = id; break;
        }
        }
    }
    return true;
}
#define RESTORE_FIELDS(game, record, owner, table, slots, count, error) \
    restore_fields(qa_session_strings((game)->services.session), (game)->options.provider, \
        record, owner, table, sizeof(table) / sizeof(*(table)), slots, count, error)
static bool restore_physics(const qa_q1_save_record *record, qa_physics_properties *physics,
    uint32_t *source_flags, qa_error *error) {
    float motion = saved_number(record, "movetype"), solid = saved_number(record, "solid"),
        flags = saved_number(record, "flags");
    if (!isfinite(motion) || motion < 0 || motion > 11 || !isfinite(solid) || solid < 0 || solid > 4)
        return fail(error,"Original physical policy exceeds its actual Source domain");
    switch ((int)motion) {
    case 0: physics->motion = QA_PHYSICS_STATIONARY; break;
    case 4: case 3: physics->motion = QA_PHYSICS_STEP; break;
    case 5: physics->motion = QA_PHYSICS_FLY; break;
    case 6: physics->motion = QA_PHYSICS_TOSS; break;
    case 7: physics->motion = QA_PHYSICS_PUSH; break;
    case 8: physics->motion = QA_PHYSICS_NOCLIP; break;
    case 9: physics->motion = QA_PHYSICS_FLY_MISSILE; break;
    case 10: physics->motion = QA_PHYSICS_BOUNCE; break;
    case 11: physics->motion = QA_PHYSICS_WALL_BOUNCE; break;
    default: return fail(error, "Original movetype has no admitted native Source policy");
    }
    switch ((int)solid) {
    case 0: physics->solid = QA_PHYSICS_NOT_SOLID; break;
    case 1: physics->solid = QA_PHYSICS_TRIGGER; break;
    case 2: case 3: physics->solid = QA_PHYSICS_BOX; break;
    case 4: physics->solid = QA_PHYSICS_BRUSH; break;
    default: return fail(error, "Original solid has no admitted native Source policy");
    }
    if (!isfinite(flags) || flags < 0 || flags >= 4294967296.0)
        return fail(error, "Original flags exceed their Source word");
    *source_flags = (uint32_t)flags;
    physics->flags = 0;
    if (*source_flags & 1) physics->flags |= QA_PHYSICS_FLYING;
    if (*source_flags & 2) physics->flags |= QA_PHYSICS_SWIMMING;
    if (*source_flags & 8) physics->flags |= QA_PHYSICS_PLAYER;
    if (*source_flags & 32) physics->flags |= QA_PHYSICS_MONSTER;
    if (*source_flags & 512) physics->flags |= QA_PHYSICS_ONGROUND;
    if (*source_flags & 1024) physics->flags |= QA_PHYSICS_PARTIAL_GROUND;
    if (physics->gravity_scale == 0) physics->gravity_scale = 1;
    return true;
}
static bool restore_think(qa_q1_game *game, q1_actor *entity,
    const qa_q1_save_record *record, qa_error *error) {
    const char *name = saved(record, "think");
    entity->think = Q1_THINK_NONE; entity->next_think = saved_number(record, "nextthink");
    if (!name || !*name) return true;
    if (!strncmp(name,"s_explode",9) && name[9] >= '1' && name[9] <= '6' && !name[10]) {
        entity->think = Q1_THINK_SPRITE; return true;
    }
    if (entity->kind == Q1_MONSTER) {
        uint16_t frame = q1_frame_index(name);
        if (frame != UINT16_MAX) {
            entity->state.monster.next_frame = frame; entity->think = Q1_THINK_MONSTER_FRAME;
            return true;
        }
        if (!strcmp(name, "walkmonster_start_go") || !strcmp(name, "swimmonster_start_go") ||
            !strcmp(name, "flymonster_start_go")) { entity->think = Q1_THINK_MONSTER_START; return true; }
        if (!strcmp(name, "FoundTarget")) { entity->think = Q1_THINK_MONSTER_FOUND; return true; }
    }
    if (entity->map) {
        for (size_t i = 0; i < sizeof(map_callbacks) / sizeof(*map_callbacks); ++i)
            if (!strcmp(name, map_callbacks[i].name)) {
                entity->think = Q1_THINK_MAP; entity->map->action = map_callbacks[i].action; return true;
            }
    }
    if (!strcmp(name, "OgreGrenadeExplode")) { entity->think = Q1_THINK_EXPLODE; return true; }
    for (size_t i = 0; i < sizeof(think_callbacks) / sizeof(*think_callbacks); ++i)
        if (!strcmp(name, think_callbacks[i].name)) { entity->think = think_callbacks[i].kind; return true; }
    (void)game;
    return fail(error, "Original think has no genuine compiled Source continuation");
}
static bool restore_inventory(qa_q1_game *game, q1_player *player,
    const qa_q1_save_record *record, qa_error *error) {
    float source_items = saved_number(record, "items");
    if (!isfinite(source_items) || source_items < 0 || source_items >= 4294967296.0)
        return fail(error, "Original inventory exceeds its genuine Source word");
    uint32_t bits = (uint32_t)source_items;
    size_t count = 0, written = 0;
    if (!qa_inventory_entries(game->services.inventory, player->id, NULL, 0, &count, error)) return false;
    qa_inventory_entry *entries = count ? calloc(count, sizeof(*entries)) : NULL;
    if (count && !entries) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring Source inventory"); return false; }
    bool okay = qa_inventory_entries(game->services.inventory, player->id, entries, count, &written, error);
    for (size_t i = 0; okay && i < written; ++i) {
        qa_inventory_entry *entry = entries + i; bool matched = false;
        for (unsigned shift = 0; shift < 32; ++shift) {
            qa_q1_weapon weapon; uint32_t bit = UINT32_C(1) << shift;
            if (qa_q1_weapon_source(game->options.program, bit, &weapon) && entry->item == game->weapons[weapon]) {
                entry->count = (bits & bit) != 0; matched = true; break;
            }
        }
        for (unsigned j = 0; j < QA_Q1_AMMO_COUNT; ++j)
            if (entry->item == game->ammo[j]) { entry->count = saved_number(record, ammo_field(game,j)); matched = true; }
        const char *name = qa_strings_cstr(qa_session_strings(game->services.session), entry->item);
        if (name && !strcmp(name, "q1:key/silver")) { entry->count = (bits & 131072u) != 0; matched = true; }
        if (name && !strcmp(name, "q1:key/gold")) { entry->count = (bits & 262144u) != 0; matched = true; }
        if (matched) okay = qa_inventory_configure(game->services.inventory, player->id, entry, NULL, NULL, error);
    }
    free(entries);
    float selected = saved_number(record, "weapon");
    if (!okay) return false;
    if (!isfinite(selected) || selected <= 0 || selected >= 4294967296.0 ||
        !qa_q1_weapon_source(game->options.program, (uint32_t)selected, &player->weapon))
        return fail(error, "Original selected weapon lacks its genuine compiled Source identity");
    return true;
}
static bool restore_player(q1_player *player, const qa_q1_save_record *record, qa_error *error) {
    q1_character *c = &player->character_state;
    const char *think = saved(record, "think");
    c->life = QA_Q1_ALIVE; c->animation = (qa_q1_frame_range){0};
    c->death_animation = c->attack_animation = false;
    c->pose.axe_pose = player->weapon == QA_Q1_AXE;
    c->locomotion = think && !strcmp(think, "player_run") ? 2 : 1;
    c->next_animation = saved_number(record, "nextthink");
    player->continuous = false; player->animation_at = -1;
    player->animation_base = 1; player->next_weapon_frame = c->next_animation;
    if (!think || !*think || !strcmp(think, "player_run") || !strcmp(think, "player_stand1")) return true;
    for (size_t i = 0; i < sizeof(player_animations) / sizeof(*player_animations); ++i) {
        const original_player_animation *a = player_animations + i; size_t prefix = strlen(a->prefix);
        if (strncmp(think, a->prefix, prefix)) continue;
        unsigned frame = (unsigned)strtoul(think + prefix, NULL, 10);
        if (!frame || frame > a->count) continue;
        c->animation = (qa_q1_frame_range){a->first, a->count};
        c->animation_frame = frame >= 2 ? (uint16_t)(frame - 2) : a->count - 1;
        c->attack_animation = a->attack;
        player->continuous = a->first == 103 || a->first == 105;
        if (a->attack && !player->continuous) {
            player->animation_base = a->first == 125 || a->first == 137 ? 5 : 1;
            int32_t completed = player->weapon_frame - player->animation_base;
            if (completed < 0) completed = 0;
            player->animation_at = c->next_animation - (completed + 1) * .1;
        }
        player->nail_side = a->first == 103 && frame == 2 ? -1 : 1;
        return true;
    }
    return fail(error, "Original player callback has no actual compiled Source animation");
}
static const original_projectile *saved_projectile(const qa_q1_save_record *record) {
    const char *name = saved(record, "classname"), *touch = saved(record, "touch");
    for (size_t i = 0; i < sizeof(projectiles) / sizeof(*projectiles); ++i) {
        if (strcmp(name ? name : "",projectiles[i].classname)) continue;
        if (touch && !strcmp(touch,projectiles[i].touch)) return projectiles + i;
    }
    const char *model = saved(record,"model");
    if ((!name || !*name) && touch && !strcmp(touch,"SUB_Remove") && model && !strcmp(model,"progs/zom_gib.mdl"))
        return projectiles+Q1_ZOMBIE_GRENADE;
    return NULL;
}
typedef struct original_class {
    const char *name;
    const q1_species *species;
    const original_projectile *projectile;
    q1_map_kind map;
    q1_entity_kind kind;
} original_class;
static bool original_classify(const qa_q1_save_record *record, original_class *out, qa_error *error) {
    const char *name = saved(record, "classname");
    const char *think = saved(record, "think");
    const char *touch = saved(record, "touch");
    const char *model = saved(record, "model");
    bool explosion = think && !strcmp(think,"SUB_Remove") && model && !strcmp(model,"progs/s_explod.spr");
    const q1_species *species = name ? q1_species_find(name) : NULL;
    const original_projectile *projectile = saved_projectile(record);
    q1_map_kind map = name ? q1_map_classify(name) : Q1_MAP_FIELDS;
    if (touch && !strcmp(touch,"door_trigger_touch")) map = Q1_MAP_DOOR_TRIGGER;
    if (touch && (!strcmp(touch,"plat_center_touch") || !strcmp(touch,"plat_outside_touch"))) map = Q1_MAP_PLAT_TRIGGER;
    if (think && !strcmp(think,"DelayThink")) map = Q1_MAP_DELAY;
    if (think && !strcmp(think,"barrel_explode")) map = Q1_MAP_BARREL;
    if (think && !strcmp(think,"fire_fly")) map = Q1_MAP_FIREBALL;
    bool body=name && (!strcmp(name,"bodyque") || !strcmp(name,"bodyqueue"));
    q1_entity_kind kind = body ? Q1_BODY : species ? Q1_MONSTER : projectile ? Q1_PROJECTILE :
        map != Q1_MAP_FIELDS ? Q1_MAP : Q1_ENTITY;
    if (think && (!strcmp(think,"Wiz_FastFire") || !strcmp(think,"DeathBubblesSpawn") ||
        !strcmp(think,"bubble_bob") || !strcmp(think,"ScourgeTriggerThink") || !strncmp(think,"s_explode",9))) kind = Q1_TIMER;
    if (explosion) kind = Q1_TIMER;
    if (touch && (!strcmp(touch,"health_touch") || !strcmp(touch,"armor_touch") ||
        !strcmp(touch,"ammo_touch") || !strcmp(touch,"weapon_touch") || !strcmp(touch,"key_touch") ||
        !strcmp(touch,"powerup_touch") || !strcmp(touch,"BackpackTouch"))) kind = Q1_PICKUP;
    if (projectile) name = projectile->native_classname;
    if (!name || !*name) {
        if (kind == Q1_PICKUP && touch && !strcmp(touch,"BackpackTouch")) name = "item_backpack";
        else if (think && !strcmp(think,"bubble_bob")) name = "bubble";
        else if (think && !strcmp(think,"DeathBubblesSpawn")) name = "death_bubbles";
        else if (think && !strcmp(think,"Wiz_FastFire")) name = "wizard_fastfire";
        else if (think && !strcmp(think,"ScourgeTriggerThink")) name = "scourge_trigger";
        else if (explosion || (think && !strncmp(think,"s_explode",9))) name = "explosion";
        else if (think && !strcmp(think,"DelayThink")) { name = "delayed_use"; map = Q1_MAP_DELAY; kind = Q1_MAP; }
        else if (think && !strcmp(think,"SUB_Remove")) name = "gib";
        else return fail(error, "Original anonymous edict has no actual Source discriminator");
    }
    *out = (original_class){name, species, projectile, map, kind}; return true;
}
static bool create_original(qa_q1_game *game, const qa_q1_save_record *record,
    uint32_t slot, q1_actor **out, qa_error *error) {
    original_class source;
    if (!original_classify(record, &source, error)) return false;
    const char *name = source.name;
    const q1_species *species = source.species;
    const original_projectile *projectile = source.projectile;
    q1_map_kind map = source.map;
    q1_entity_kind kind = source.kind;
    if (!q1_create_source(game, name, kind, slot, out, error)) return false;
    q1_actor *entity = *out;
    if (species) entity->state.monster.species = species;
    if (map != Q1_MAP_FIELDS) {
        if (!q1_map_allocate(game, entity, error)) return false;
        entity->map->kind = map;
    }
    if (kind == Q1_PICKUP) {
        entity->spawnflags = (uint32_t)saved_number(record, "spawnflags");
        if (!q1_pickup_define(game, entity, error)) return false;
    }
    if (projectile) {
        entity->state.projectile.kind = projectile->kind;
        entity->state.projectile.weapon = projectile->weapon;
    }
    return true;
}
static bool restore_monster_callbacks(q1_monster *monster,
    const qa_q1_save_record *record,qa_error *error) {
    if (!monster->species) return fail(error,"Original monster lacks its actual Source species");
    monster->dead=saved_number(record,"health")<=0;monster->counted_death=monster->dead;
    const char *touch=saved(record,"touch");
    monster->jump_touch=touch && (!strcmp(touch,"Dog_JumpTouch") ||
        !strcmp(touch,"Demon_JumpTouch") || !strcmp(touch,"Tar_JumpTouch"));
    monster->current_frame=UINT16_MAX;
    if (monster->species->species==QA_Q1_SWORD) {
        const char *run=saved(record,"th_run"),*pain=saved(record,"th_pain");
        monster->source.sword.awakened=run && !strcmp(run,"sword_run1");
        monster->source.sword.pain_disabled=pain && !strcmp(pain,"SUB_Null");
    }
    if (monster->species->species==QA_Q1_MUMMY) {
        const char *stand=saved(record,"th_stand");
        monster->source.mummy.asleep=stand && !strcmp(stand,"mummy_sleep");
    }
    return true;
}

static bool restore_map(qa_strings *strings,qa_actor_owner source,
    const qa_q1_save_record *record,q1_actor *entity,const qa_actor_id *slots,
    size_t count,qa_error *error) {
    q1_map_state *map = entity->map;
    map->touch_enabled = saved(record,"touch") != NULL; map->use_enabled = saved(record,"use") != NULL;
    map->original_model = entity->model; map->dormant = saved(record,"use") && !strcmp(saved(record,"use"),"plat_use");
    const char *model = qa_strings_cstr(strings, entity->model);
    if (model && *model == '*') {
        char *end; unsigned long index = strtoul(model + 1, &end, 10);
        if (*end || index > UINT32_MAX) return fail(error, "Original brush has invalid physical inline model");
        map->has_inline_model = true; map->inline_model = (uint32_t)index;
    }
    if (q1_map_is_mover(map->kind)) {
        q1_map_movement *move = &map->pending.mover;
        if (!saved_vector(saved(record,"pos1"), &move->pos1, error) ||
            !saved_vector(saved(record,"pos2"), &move->pos2, error) ||
            !saved_vector(saved(record,"dest1"), &move->dest1, error) ||
            !saved_vector(saved(record,"dest2"), &move->dest2, error) ||
            !saved_vector(saved(record,"finaldest"), &move->destination, error)) return false;
        float position = saved_number(record,"state");
        move->position = position == 0 ? Q1_MAP_TOP : position == 1 ? Q1_MAP_BOTTOM :
            position == 2 ? Q1_MAP_UP : Q1_MAP_DOWN;
        move->moving = position == 2 || position == 3 || (saved(record,"think") && !strcmp(saved(record,"think"),"SUB_CalcMoveDone")); move->done = Q1_MAP_IDLE;
        const char *done = saved(record,"think1");
        for (size_t i = 0; done && i < sizeof(map_callbacks) / sizeof(*map_callbacks); ++i)
            if (!strcmp(done, map_callbacks[i].name)) move->done = map_callbacks[i].action;
        move->group = NULL;
    }
    if (map->action == Q1_MAP_DELAYED_USE) {
        original_field activator={"enemy",ORIGINAL_REF,offsetof(q1_actor,activator)};
        if (!restore_fields(strings,source,record,entity,&activator,1,slots,count,error)) return false;
        map->pending.delayed.dialect = QA_CLOCK_NETQUAKE;
    }
    return true;
}
typedef struct original_admission {
    qa_strings *strings;
    qa_actor_owner source;
    size_t slots;
    const qa_q1_save_record *record;
    uint8_t *consumed;
} original_admission;
static void admitted_key(original_admission *admission, const char *name) {
    for (size_t i=0; i<admission->record->count; ++i)
        if (!strcmp(admission->record->pairs[i].key,name)) admission->consumed[i]=1;
}
static bool unsupported(qa_error *error, const char *message) {
    qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"%s",message); return false;
}
static bool admit_fields(original_admission *admission, void *owner,
    const original_field *table, size_t count, qa_error *error) {
    for (size_t i=0;i<count;++i)
        if (table[i].storage==ORIGINAL_ACTOR)
            return unsupported(error,"Source lifetime field lacks its physical native reference");
    if (!restore_fields(admission->strings,admission->source,admission->record,owner,table,count,
        NULL,admission->slots,error)) return false;
    for (size_t i=0; i<count; ++i) {
        const original_field *field=table+i;
        float value=saved_number(admission->record,field->name);
        if ((field->storage==ORIGINAL_FLOAT || field->storage==ORIGINAL_DOUBLE) && !isfinite(value))
            return unsupported(error,"Source scalar has no finite native state");
        if (field->storage==ORIGINAL_VECTOR &&
            !qa_vec_finite(*(qa_vec3 *)((uint8_t *)owner+field->offset)))
            return unsupported(error,"Source vector has no finite native state");
        if ((field->storage==ORIGINAL_I32 || field->storage==ORIGINAL_U32 ||
             field->storage==ORIGINAL_I16 || field->storage==ORIGINAL_U16 ||
             field->storage==ORIGINAL_U8) && truncf(value)!=value)
            return unsupported(error,"Source fractional field has no lossless native integer state");
        if (field->storage==ORIGINAL_BOOL && value!=0 && value!=1)
            return unsupported(error,"Source boolean field has no lossless native state");
        admitted_key(admission,field->name);
    }
    return true;
}
#define ADMIT_FIELDS(admission,owner,table,error) \
    admit_fields(admission,owner,table,sizeof(table)/sizeof(*(table)),error)
static bool admit_number(original_admission *admission,const char *name,
    float expected,qa_error *error) {
    float actual=saved_number(admission->record,name);
    if (actual!=expected) return unsupported(error,"Source scalar differs from the native derived state");
    admitted_key(admission,name);return true;
}
static bool admit_word(original_admission *admission,const char *name,qa_error *error) {
    float value=saved_number(admission->record,name);
    if (!isfinite(value) || value<0 || value>=4294967296.0 || truncf(value)!=value)
        return unsupported(error,"Source word has no lossless native state");
    admitted_key(admission,name);return true;
}
static bool admit_string(original_admission *admission,const char *name,qa_error *error) {
    const char *value=saved(admission->record,name);char *decoded=NULL;
    if (!qa_q1_save_string_decode(value?value:"",&decoded,error)) return false;
    free(decoded);admitted_key(admission,name);return true;
}
static bool admit_text(original_admission *admission,const char *name,
    const char *expected,qa_error *error) {
    const char *value=saved(admission->record,name);
    char *decoded=NULL;
    if (!qa_q1_save_string_decode(value?value:"",&decoded,error)) return false;
    bool equal=!strcmp(decoded,expected?expected:"");free(decoded);
    if (!equal) return unsupported(error,"Source string differs from the native derived state");
    admitted_key(admission,name);return true;
}
static bool admit_vector(original_admission *admission,const char *name,
    qa_vec3 expected,qa_error *error) {
    qa_vec3 actual;
    if (!saved_vector(saved(admission->record,name),&actual,error)) return false;
    if (actual.x!=expected.x || actual.y!=expected.y || actual.z!=expected.z)
        return unsupported(error,"Source vector differs from the native derived state");
    admitted_key(admission,name);return true;
}
static bool admit_callbacks(original_admission *admission,
    const qa_q1_save_record *expected,qa_error *error) {
    static const char *const callbacks[]={"touch","use","blocked","th_stand","th_walk",
        "th_run","th_missile","th_melee","th_pain","th_die"};
    for (size_t i=0;i<sizeof(callbacks)/sizeof(*callbacks);++i) {
        const char *name=callbacks[i],*actual=saved(admission->record,name),*wanted=saved(expected,name);
        if (strcmp(actual?actual:"",wanted?wanted:""))
            return unsupported(error,"Source callback differs from the compiled continuation");
        admitted_key(admission,name);
    }
    return true;
}
static bool admit_complete(const original_admission *admission,qa_error *error) {
    for (size_t i=0;i<admission->record->count;++i)
        if (!admission->consumed[i] && *admission->record->pairs[i].key!='_')
            return unsupported(error,"Source field lacks its paired native inverse");
    return true;
}
static bool admit_globals(original_admission *admission,const qa_qc_program *program,
    qa_q1_program native_program,qa_q1_edition edition,const qa_q1_save_data *save,qa_error *error) {
    qa_q1_game game={.options={.program=native_program,.edition=edition}};
    q1_map_runtime maps={0};uint32_t flags=0;
    char queue_name[32];original_field queue_global=body_queue_global(&game,queue_name);
    if (!admit_fields(admission,&game,&queue_global,1,error)) return false;
    if (!ADMIT_FIELDS(admission,&game,game_globals,error) ||
        !ADMIT_FIELDS(admission,&maps,map_globals,error) ||
        !ADMIT_FIELDS(admission,&flags,server_globals,error) ||
        !admit_fields(admission,&game,enemy_globals+(edition==QA_Q1_RERELEASE),1,error)) return false;
    if (!admit_number(admission,"time",(float)save->time,error) ||
        !admit_number(admission,"frametime",0,error) ||
        !admit_text(admission,"mapname",save->map,error) ||
        !admit_number(admission,"deathmatch",0,error) || !admit_number(admission,"coop",0,error) ||
        !admit_number(admission,"teamplay",0,error) || !admit_number(admission,"skill",(float)save->skill,error)) return false;
    for (unsigned i=0;i<16;++i) {
        char name[16];snprintf(name,sizeof(name),"parm%u",i+1);
        if (!admit_number(admission,name,(float)save->spawn_parameters[i],error)) return false;
    }
    /* Model ordinals are qualified against the actual constructed map's one
     * ordered precache owner before the inverse can mutate an edict. */
    if (!admit_word(admission,"modelindex_eyes",error) ||
        !admit_word(admission,"modelindex_player",error)) return false;
    for (size_t i=0;i<admission->record->count;++i) {
        if (admission->consumed[i]) continue;
        const qa_q1_save_pair *pair=admission->record->pairs+i;
        const qa_qc_definition *definition=qa_qc_program_find_global(program,pair->key);
        int32_t initial;
        if (!definition || !qa_qc_program_initial_int(program,definition->offset,&initial,error)) return false;
        if (definition->type==QA_QC_FLOAT) {
            float expected;memcpy(&expected,&initial,sizeof(expected));
            float actual=saved_number(admission->record,pair->key);
            uint32_t expected_bits,actual_bits;
            memcpy(&expected_bits,&expected,4);memcpy(&actual_bits,&actual,4);
            if (actual_bits!=expected_bits)
                return unsupported(error,"Mutable Source global lacks its paired native inverse");
        } else if (definition->type==QA_QC_ENTITY) {
            uint32_t slot;
            if (initial || !qa_q1_save_entity_decode(saved(admission->record,pair->key),&slot,error) || slot)
                return unsupported(error,"Mutable Source reference global lacks its paired native inverse");
        } else if (definition->type==QA_QC_STRING) {
            if (initial || !admit_text(admission,pair->key,"",error))
                return unsupported(error,"Mutable Source string global lacks its paired native inverse");
        } else return unsupported(error,"Source global has no native state representation");
        admission->consumed[i]=1;
    }
    return true;
}
static bool admit_entity(original_admission *admission,qa_q1_program program,
    size_t slot,qa_error *error) {
    const qa_q1_save_record *record=admission->record;
    qa_body_state body={0};qa_physics_properties physics=qa_physics_properties_default(QA_COLLISION_Q1);
    uint32_t flags;
    if (!ADMIT_FIELDS(admission,&body,body_fields,error) ||
        !ADMIT_FIELDS(admission,&physics,physics_fields,error) ||
        !restore_physics(record,&physics,&flags,error)) return false;
    if (!admit_word(admission,"flags",error) || !admit_word(admission,"movetype",error) ||
        !admit_word(admission,"solid",error)) return false;
    if (!admit_word(admission,"modelindex",error)) return false;
    if (saved(record,"size") && !admit_vector(admission,"size",qa_vec_sub(body.bounds.maxs,body.bounds.mins),error)) return false;
    admitted_key(admission,"size");
    qa_combat_state combat={0};combat.armor.regular.kind=QA_ARMOR_Q1;
    if (!ADMIT_FIELDS(admission,&combat,combat_fields,error) || !qa_armor_validate(&combat.armor,error)) return false;
    if (body.bounds.mins.x>body.bounds.maxs.x || body.bounds.mins.y>body.bounds.maxs.y || body.bounds.mins.z>body.bounds.maxs.z)
        return unsupported(error,"Source bounds have no native physical body");
    float damage=saved_number(record,"takedamage");
    if (damage!=0 && damage!=1 && damage!=2) return unsupported(error,"Source damage policy has no native state");
    admitted_key(admission,"takedamage");
    qa_q1_save_record callbacks={0};bool okay=false;
    if (slot==1) {
        q1_player player={0};
        if (!(saved_number(record,"health")>0)) {
            unsupported(error,"Source dead player has no living native inverse");goto done;
        }
        if (!admit_text(admission,"classname","player",error) ||
            !ADMIT_FIELDS(admission,&player,player_fields,error) ||
            !ADMIT_FIELDS(admission,&player.character_state,character_fields,error) ||
            !admit_word(admission,"items",error) || !admit_word(admission,"weapon",error)) goto done;
        float selected=saved_number(record,"weapon");
        if (selected<=0 || !qa_q1_weapon_source(program,(uint32_t)selected,&player.weapon)) {
            unsupported(error,"Source weapon has no compiled identity");goto done;
        }
        /* The inverse restores weapons and keys directly, and powers from their
         * expiry fields. Other item bits and view-model identities still need
         * a paired inverse before this row may enter native execution. */
        uint32_t represented=131072u|262144u;
        for (unsigned shift=0;shift<32;++shift) {
            qa_q1_weapon weapon;
            if (qa_q1_weapon_source(program,UINT32_C(1)<<shift,&weapon)) represented|=UINT32_C(1)<<shift;
        }
        if ((uint32_t)saved_number(record,"items") & ~represented) {
            unsupported(error,"Source inventory bit lacks its paired native inverse");goto done;
        }
        for (unsigned i=0;i<(program==QA_Q1_ROGUE?QA_Q1_AMMO_COUNT:4);++i) {
            qa_q1_game settings={.options={.program=program}};
            const char *name=ammo_field(&settings,i);
            if (!isfinite(saved_number(record,name)) || saved_number(record,name)<0) {
                unsupported(error,"Source ammunition has no native inventory state");goto done;
            }
            admitted_key(admission,name);
        }
        if (!admit_number(admission,"items2",0,error) || !admit_number(admission,"deadflag",0,error) ||
            !admit_number(admission,"colormap",1,error) || !admit_string(admission,"netname",error) ||
            !admit_string(admission,"weaponmodel",error)) goto done;
        if (!isfinite(saved_number(record,"max_health")) || !isfinite(saved_number(record,"idealpitch"))) {
            unsupported(error,"Source player scalar has no finite native state");goto done;
        }
        admitted_key(admission,"max_health");admitted_key(admission,"idealpitch");
        qa_vec3 water_jump;
        if (!saved_vector(saved(record,"movedir"),&water_jump,error)) goto done;
        admitted_key(admission,"movedir");
        if (!restore_player(&player,record,error) || !player_functions(&player,&callbacks,error) ||
            strcmp(saved(record,"think")?saved(record,"think"):"",saved(&callbacks,"think")?saved(&callbacks,"think"):"")) {
            unsupported(error,"Source player animation is not preserved by the native inverse");goto done;
        }
        admitted_key(admission,"think");admitted_key(admission,"nextthink");
    } else {
        original_class source;
        if (!original_classify(record,&source,error)) goto done;
        bool map_supported=source.map==Q1_MAP_FIELDS || source.map==Q1_MAP_WORLD ||
            source.map==Q1_MAP_POINT || source.map==Q1_MAP_PATH || source.map==Q1_MAP_DESTINATION;
        if (source.kind==Q1_PICKUP || !map_supported || source.kind==Q1_ENTITY) {
            unsupported(error,"Source map/item state still lacks its complete paired native inverse");goto done;
        }
        q1_actor entity={.kind=source.kind};q1_map_state map={.kind=source.map};
        if (!ADMIT_FIELDS(admission,&entity,entity_fields,error)) goto done;
        if (source.map!=Q1_MAP_FIELDS) entity.map=&map;
        if (!restore_think(NULL,&entity,record,error)) goto done;
        admitted_key(admission,"think");admitted_key(admission,"nextthink");
        if (entity.kind==Q1_MONSTER) {
            q1_monster *monster=&entity.state.monster;monster->species=source.species;
            if (!ADMIT_FIELDS(admission,monster,monster_fields,error)) goto done;
            if (source.species->species==QA_Q1_EEL && !ADMIT_FIELDS(admission,monster,eel_fields,error)) goto done;
            if (source.species->species==QA_Q1_SCOURGE && !ADMIT_FIELDS(admission,monster,scourge_fields,error)) goto done;
            if (!restore_monster_callbacks(monster,record,error)) goto done;
            if (!monster_functions(monster,&callbacks,error)) goto done;
        } else if (entity.kind==Q1_BODY) {
            if (!ADMIT_FIELDS(admission,&entity,body_queue_fields,error) ||
                entity.think!=Q1_THINK_NONE || entity.next_think!=0 || physics.solid!=QA_PHYSICS_NOT_SOLID ||
                saved_number(record,"movetype")==3) {
                unsupported(error,"Source body continuation differs from its persistent native queue");goto done;
            }
        } else if (entity.map) {
            if (!ADMIT_FIELDS(admission,&map,map_fields,error) ||
                !restore_map(admission->strings,admission->source,record,&entity,NULL,admission->slots,error)) goto done;
            if (source.map==Q1_MAP_WORLD) {
                qa_q1_options options={0};
                if (slot || !admit_text(admission,"classname","worldspawn",error) ||
                    !ADMIT_FIELDS(admission,&options,world_fields,error)) goto done;
            } else if (!slot || entity.think!=Q1_THINK_NONE || entity.next_think!=0 ||
                saved_number(record,"movetype")==3 || saved_number(record,"solid")==3) {
                unsupported(error,"Source map point has an unrepresented scheduled continuation");goto done;
            }
            if (!map_functions(&entity,&callbacks,error)) goto done;
        } else if (source.projectile) {
            entity.state.projectile.kind=source.projectile->kind;
            if (source.projectile->kind==Q1_HIP_LASER && !ADMIT_FIELDS(admission,&entity.state.projectile,hip_laser_fields,error)) goto done;
            if (source.projectile->kind==Q1_PROXIMITY && !ADMIT_FIELDS(admission,&entity.state.projectile,proximity_fields,error)) goto done;
            if (!callback(&callbacks,"touch",source.projectile->touch,error)) goto done;
            if (source.projectile->kind==Q1_PROXIMITY && !callback(&callbacks,"th_die","ProximityGrenadeExplode",error)) goto done;
        } else {
            unsupported(error,"Source anonymous continuation needs its complete paired state admission");goto done;
        }
    }
    okay=admit_callbacks(admission,&callbacks,error) && admit_complete(admission,error);
done:
    qa_q1_save_record_destroy(&callbacks);return okay;
}
static bool admit_body_queue(qa_q1_program program,qa_q1_edition edition,
    const qa_q1_save_data *save,qa_error *error) {
    qa_q1_game options={.options={.program=program,.edition=edition}};
    char name[32];original_field global=body_queue_global(&options,name);
    const char *classname=q1_body_queue_classname(&options);
    q1_ref nodes[4],links[4],head;size_t count=0;
    original_admission admission={.source=1,.slots=save->entity_count,.record=&save->globals};
    uint32_t slot;
    if (!qa_q1_save_entity_decode(saved(admission.record,global.name),&slot,error) || slot>=save->entity_count)
        return unsupported(error,"Source body head leaves its physical edicts");
    head=qa_actor_reference_source(admission.source,slot);
    for (size_t i=2;i<save->entity_count;++i) {
        const char *body=saved(save->entities+i,"classname");
        if (!body || strcmp(body,classname)) continue;
        if (count==4 || !qa_q1_save_entity_decode(saved(save->entities+i,"owner"),&slot,error) || slot>=save->entity_count)
            return unsupported(error,"Source body queue lacks its four physical links");
        nodes[count]=qa_actor_reference_source(admission.source,(uint32_t)i);
        links[count++]=qa_actor_reference_source(admission.source,slot);
    }
    if (count!=4 || !q1_body_queue_ring_valid(head,nodes,links))
        return unsupported(error,"Source body queue lacks its four linked physical edicts");
    return true;
}

bool qa_q1_game_original_admit(qa_q1_program native_program,qa_q1_edition edition,
    const qa_qc_program *program,const qa_q1_save_data *save,bool *supported,qa_error *error) {
    if (!supported || !program || !save || native_program>QA_Q1_CTF || edition>QA_Q1_RERELEASE)
        return fail(error,"Original native admission requires actual Source metadata and parsed state");
    *supported=false;
    if (!qa_q1_save_singleplayer(save,error) || !qa_qc_text_program_ready(program,save,error)) return false;
    if (save->entity_count>UINT32_MAX || !save->entities[1].count) return true;
    size_t count=save->globals.count;
    for (size_t i=0;i<save->entity_count;++i) if (save->entities[i].count>count) count=save->entities[i].count;
    uint8_t *consumed=count?calloc(count,1):NULL;qa_strings *strings=NULL;
    if (count && !consumed) {qa_error_set(error,QA_ERROR_MEMORY,0,"Reading original native state domains");return false;}
    if (!qa_strings_create(&strings,error)) {free(consumed);return false;}
    original_admission admission={strings,1,save->entity_count,&save->globals,consumed};qa_error local={0};
    bool okay=admit_globals(&admission,program,native_program,edition,save,&local) &&
        admit_body_queue(native_program,edition,save,&local);
    for (size_t slot=0;okay && slot<save->entity_count;++slot) {
        if (!save->entities[slot].count) continue;
        memset(consumed,0,count);admission.record=save->entities+slot;
        okay=admit_entity(&admission,native_program,slot,&local);
    }
    qa_strings_destroy(strings);free(consumed);
    if (!okay && local.code==QA_ERROR_MEMORY) {if(error)*error=local;return false;}
    *supported=okay;return true;
}

static bool restore_player_name(qa_q1_game *game,const q1_player *player,
    const qa_q1_save_record *record,qa_error *error) {
    const char *value=saved(record,"netname"),*current=NULL;char *name=NULL;
    if (!qa_q1_save_string_decode(value?value:"",&name,error)) return false;
    bool okay=qa_q1_source_client_info(game,player->id,"name",&current);
    if (okay && strcmp(name,current?current:""))
        okay=qa_q1_source_client_name(game,player->id,name,error);
    free(name);return okay;
}

static bool restore_entity(qa_q1_game *game, q1_actor *entity, q1_player *player,
    const qa_q1_save_record *record, const qa_actor_id *slots, size_t count, qa_movement_state *movement, qa_error *error) {
    qa_actor_id id = player ? player->id : entity->id;
    qa_body_state body;
    qa_combat_state combat;
    qa_physics_properties physics = entity ? entity->physics : qa_physics_properties_default(QA_COLLISION_Q1);
    uint32_t flags;
    if (!qa_world_body_read(game->services.world, id, &body, error) ||
        !qa_combat_read_traits(game->services.combat, id, &combat, error) ||
        !RESTORE_FIELDS(game, record, &body, body_fields, slots, count, error) ||
        !RESTORE_FIELDS(game, record, &physics, physics_fields, slots, count, error) ||
        !RESTORE_FIELDS(game, record, &combat, combat_fields, slots, count, error) ||
        !restore_physics(record, &physics, &flags, error)) return false;
    combat.can_take_damage = saved_number(record, "takedamage") != 0;
    combat.armor.regular.kind = QA_ARMOR_Q1;
    if (!qa_world_body_write(game->services.world, id, &body, error) ||
        !qa_combat_set_traits(game->services.combat, id, &combat, error) ||
        !qa_combat_set_health(game->services.combat, id, combat.health, error) ||
        !qa_combat_set_armor(game->services.combat, id, &combat.armor, error)) return false;
    if (player) {
        q1_powers_forget(player);
        if (!RESTORE_FIELDS(game, record, player, player_fields, slots, count, error) ||
            !RESTORE_FIELDS(game, record, &player->character_state, character_fields, slots, count, error) ||
            !restore_inventory(game, player, record, error)) return false;
        player->source_god_mode = (flags & 64u) != 0;
        player->source_no_target = (flags & 128u) != 0;
        if (game->options.program != QA_Q1_ID1 || game->options.edition != QA_Q1_RERELEASE ||
            !((uint32_t)saved_number(record, "items") & 65536u)) player->mega_rot_at = -1;
        player->power_sequence = 0;
        player->power_warned = player->power_lost = 0;
        for (unsigned i=0; i<QA_Q1_POWER_COUNT; ++i) {
            double expires = player->power_expires[i];
            if (!q1_power_assign(game,id,(qa_q1_power)i,expires,expires != 0,error)) return false;
            if (expires != 0 && player->power_flash[i] != 1)
                player->power_warned |= (uint16_t)(1u << i);
        }
        player->max_health = saved_number(record, "max_health");
        player->character_state.input.water_level = (uint8_t)physics.water_level;
        player->character_state.input.water_type = physics.water_type;
        if (player->character_state.model == game->eyes_model) {
            player->character_state.model = game->player_model;
            player->character_state.input.invisible = true;
        }
        player->input.water_level = (uint8_t)physics.water_level; player->input.water_type = physics.water_type;
        player->character_state.air_until = player->air_finished;
        player->character_state.drown_damage = player->drown_damage;
        player->drown_at = player->character_state.pain_until;
        player->character_state.hazard_at = player->hazard_at;
        player->character_state.input.axe_pose = player->weapon == QA_Q1_AXE;
        if (!restore_player(player, record, error)) return false;
        movement->kind = QA_MOVEMENT_NETQUAKE;
        movement->data.nq = (qa_nq_movement_state){.origin = body.origin,.old_origin = body.origin,
            .velocity = body.velocity,.angles = body.angles,.angular_velocity = physics.angular_velocity,
            .view_angles = player->input.view_angles,.punch_angles = player->punch,
            .move_type = (int32_t)saved_number(record,"movetype"),.health = combat.health,.flags = flags,
            .water_level = physics.water_level,.water_type = physics.water_type,
            .teleport_time_seconds = player->input.teleport_until,.ideal_pitch = saved_number(record,"idealpitch")};
        qa_actor_id ground = qa_actor_reference_resolve(qa_session_actors(game->services.session), body.ground);
        bool world_ground = (body.ground.kind == QA_ACTOR_REFERENCE_SOURCE &&
            body.ground.value.source.owner == game->options.provider && !body.ground.value.source.slot) ||
            qa_actor_id_equal(ground, slots[0]);
        movement->data.nq.ground = (qa_movement_ground){.actor = ground,
            .hit = flags & 512 ? world_ground ? QA_TRACE_HIT_WORLD : QA_TRACE_HIT_ACTOR : QA_TRACE_HIT_NONE};
        if (!saved_vector(saved(record,"movedir"),&movement->data.nq.water_jump_direction,error)) return false;
        return game->services.physics && game->services.physics->services.write &&
            game->services.physics->services.write(game->services.physics->services.context, id, &physics, error) &&
            restore_player_name(game,player,record,error);
    }
    qa_string_id native_classname = entity->classname;
    if (!RESTORE_FIELDS(game, record, entity, entity_fields, slots, count, error)) return false;
    if (!entity->classname) entity->classname = native_classname;
    entity->physics = physics; entity->source_movement_flags = flags;
    entity->aimed_damage = saved_number(record, "takedamage") == 2;
    const char *source_touch = saved(record,"touch");
    entity->touch_disabled = !source_touch || !strcmp(source_touch,"SUB_Null");
    if (!restore_think(game, entity, record, error)) return false;
    const char *source_think = saved(record,"think");
    if (source_think && !strcmp(source_think,"Wiz_FastFire") &&
        (!saved_ref(game,saved(record,"enemy"),count,&entity->state.projectile.enemy,error) ||
         !saved_vector(saved(record,"movedir"),&entity->state.projectile.right,error))) return false;
    if (source_think && !strcmp(source_think,"ScourgeTriggerThink")) {
        if (!saved_ref(game,saved(record,"lastvictim"),count,&entity->activator,error)) return false;
        entity->delay = saved_number(record,"duration");
    }
    if (source_think && !strcmp(source_think,"bubble_bob")) entity->count = saved_number(record,"cnt");
    if (source_think && !strcmp(source_think,"DeathBubblesSpawn"))
        entity->count = saved_number(record,"bubble_count") - saved_number(record,"air_finished");
    if (entity->kind == Q1_MONSTER) {
        q1_monster *monster = &entity->state.monster;
        if (!RESTORE_FIELDS(game, record, monster, monster_fields, slots, count, error)) return false;
        if (monster->species && monster->species->species == QA_Q1_EEL &&
            !RESTORE_FIELDS(game,record,monster,eel_fields,slots,count,error)) return false;
        if (monster->species && monster->species->species == QA_Q1_SCOURGE &&
            !RESTORE_FIELDS(game,record,monster,scourge_fields,slots,count,error)) return false;
        if (!restore_monster_callbacks(monster,record,error)) return false;
        const char *target = qa_strings_cstr(qa_session_strings(game->services.session), entity->target);
        if (target && !qa_strings_intern_cstr(qa_session_strings(game->services.session), target, &monster->path, error)) return false;
    }
    if (entity->map) {
        if (!RESTORE_FIELDS(game,record,entity->map,map_fields,slots,count,error) ||
            !restore_map(qa_session_strings(game->services.session),game->options.provider,
                record,entity,slots,count,error)) return false;
        if (entity->map->kind==Q1_MAP_WORLD &&
            !RESTORE_FIELDS(game,record,&game->options,world_fields,slots,count,error)) return false;
    }
    if (entity->kind==Q1_BODY &&
        !RESTORE_FIELDS(game,record,entity,body_queue_fields,slots,count,error)) return false;
    if (entity->kind == Q1_PICKUP) {
        q1_pickup *item = &entity->state.pickup;
        item->hidden = !entity->model; item->holder = entity->owner;
        if (item->kind == 0 && saved(record,"healamount")) item->count = saved_number(record,"healamount");
        if (item->kind == 2 && saved(record,"aflag")) item->count = saved_number(record,"aflag");
        if (item->kind == 6) {
            for (unsigned i = 0; i < QA_Q1_AMMO_COUNT; ++i) item->ammo[i] = saved_number(record,ammo_field(game,i));
            uint32_t bits = (uint32_t)saved_number(record,"items");
            item->weapon = QA_Q1_WEAPON_COUNT;
            (void)qa_q1_weapon_source(game->options.program,bits,&item->weapon);
        }
    }
    if (entity->kind == Q1_PROJECTILE) {
        q1_projectile *p = &entity->state.projectile;
        if (!saved_ref(game,saved(record,"enemy"),count,&p->enemy,error)) return false;
        p->activator = entity->owner;
        if (p->kind == Q1_HIP_LASER &&
            !RESTORE_FIELDS(game,record,p,hip_laser_fields,slots,count,error)) return false;
        if (p->kind == Q1_PROXIMITY) {
            if (!RESTORE_FIELDS(game,record,p,proximity_fields,slots,count,error)) return false;
            p->detonating = entity->think == Q1_THINK_PROX_EXPLODE;
        }
        p->attack = q1_attack(game,q1_ref_actor(game,p->activator),id,p->weapon);
        p->attack.projectile = id;
        if (!qa_attack_next(&game->attack_sequence,&p->attack,error)) return false;
        p->remove_touch = saved(record,"touch") && !strcmp(saved(record,"touch"),"SUB_Remove");
    }
    return q1_map_bind_target(game, entity, error);
}
static bool restore_groups(qa_q1_game *game, const qa_q1_save_data *save,
    const qa_actor_id *slots, qa_error *error) {
    while (game->maps->door_groups) {
        q1_door_group *group = game->maps->door_groups; game->maps->door_groups = group->next;
        free(group->members); free(group);
    }
    for (uint32_t i = 2; i < save->entity_count; ++i) {
        q1_actor *entity = q1_entity(game,slots[i]);
        if (!entity || !entity->map || entity->map->kind != Q1_MAP_DOOR || entity->map->pending.mover.group) continue;
        uint32_t master = 0;
        const char *owner = saved(save->entities+i,"owner");
        if (owner && !qa_q1_save_entity_decode(owner,&master,error)) return false;
        if (!master) master = i;
        if (master >= save->entity_count) return fail(error,"Original door master exceeds its physical edicts");
        q1_door_group *group = calloc(1,sizeof(*group));
        if (!group) { qa_error_set(error,QA_ERROR_MEMORY,0,"Restoring Source door group"); return false; }
        group->next = game->maps->door_groups; game->maps->door_groups = group;
        uint32_t member = master;
        do {
            q1_actor *door = q1_entity(game,slots[member]);
            if (!door || !door->map || door->map->kind != Q1_MAP_DOOR || door->map->pending.mover.group ||
                group->count >= save->entity_count)
                return fail(error,"Original door ring differs from its actual physical members");
            q1_ref *members = realloc(group->members,(group->count+1)*sizeof(*members));
            if (!members) { qa_error_set(error,QA_ERROR_MEMORY,0,"Restoring Source door members"); return false; }
            group->members = members; group->members[group->count++] = q1_ref_from(game,door->id);
            door->map->pending.mover.group = group;
            uint32_t next = 0;
            const char *link = saved(save->entities+member,"enemy");
            if (link && !qa_q1_save_entity_decode(link,&next,error)) return false;
            if (!next) next = master;
            if (next >= save->entity_count) return fail(error,"Original door ring exceeds its physical edicts");
            member = next;
        } while (member != master);
    }
    return true;
}
static bool original_model_fits(const qa_q1_wire_receipt *receipt,
    const qa_strings *strings,const qa_q1_save_record *record,qa_error *error) {
    float ordinal=saved_number(record,"modelindex");
    if (!isfinite(ordinal) || ordinal<0 || ordinal>=4294967296.0 || truncf(ordinal)!=ordinal)
        return unsupported(error,"Original model ordinal has no lossless native state");
    uint32_t index=(uint32_t)ordinal;
    if (index>=receipt->model_count)
        return unsupported(error,"Original model ordinal leaves the constructed Source precache");
    const char *value=saved(record,"model");char *model=NULL;
    if (!qa_q1_save_string_decode(value?value:"",&model,error)) return false;
    const char *expected=qa_strings_cstr(strings,receipt->models[index]);
    bool equal=!strcmp(model,expected?expected:"");free(model);
    return equal || unsupported(error,"Original model name differs from its constructed Source ordinal");
}
static bool original_precache_fits(qa_q1_game *game,const qa_q1_save_data *save,
    const qa_q1_wire_receipt *receipt,qa_error *error) {
    const qa_strings *strings=qa_session_strings(game->services.session);
    uint32_t index;
    if (!qa_q1_wire_index(receipt,true,game->eyes_model,&index) ||
        saved_number(&save->globals,"modelindex_eyes")!=(float)index ||
        !qa_q1_wire_index(receipt,true,game->player_model,&index) ||
        saved_number(&save->globals,"modelindex_player")!=(float)index)
        return unsupported(error,"Original player model globals differ from the constructed Source precache");
    for (size_t i=0;i<save->entity_count;++i)
        if (save->entities[i].count && !original_model_fits(receipt,strings,save->entities+i,error)) return false;
    const qa_q1_save_record *record=save->entities+1;q1_player player={0};
    float selected=saved_number(record,"weapon");
    if (!isfinite(selected) || selected<=0 || selected>=4294967296.0 || truncf(selected)!=selected ||
        !qa_q1_weapon_source(game->options.program,(uint32_t)selected,&player.weapon))
        return unsupported(error,"Original selected weapon lacks its compiled model identity");
    const char *value=saved(record,"weaponmodel");char *model=NULL;
    if (!qa_q1_save_string_decode(value?value:"",&model,error)) return false;
    const char *expected=qa_strings_cstr(strings,q1_weapon_model(game,&player));
    bool equal=!strcmp(model,expected?expected:"");free(model);
    if (!equal) return unsupported(error,"Original weapon model differs from its compiled Source weapon");
    return true;
}
bool qa_q1_game_original_fit(qa_q1_game *game,const qa_q1_save_data *save,
    bool *supported,qa_error *error) {
    if (!game || !save || !supported || !game->wire || game->wire->loading ||
        game->destroy_pending || game->continuation_pending || game->observation_depth ||
        game->options.quakeworld || game->options.max_clients!=1 || game->options.deathmatch ||
        !qa_session_safe(game->services.session) || !qa_world_idle(game->services.world))
        return fail(error,"Original model admission requires its idle constructed Source precache");
    *supported=false;
    if (!qa_q1_save_singleplayer(save,error)) return false;
    qa_q1_wire_receipt receipt={0};
    if (!qa_q1_wire_read_begin(game,&receipt,error)) return false;
    qa_error local={0};bool okay=original_precache_fits(game,save,&receipt,&local);
    qa_q1_wire_read_end(&receipt);
    if (!okay && local.code!=QA_ERROR_UNSUPPORTED) {if (error) *error=local;return false;}
    *supported=okay;if (error) *error=(qa_error){0};return true;
}

bool qa_q1_game_original_restore(qa_q1_game *game, const qa_qc_program *program,
    const qa_q1_save_data *save, qa_movement_state *movement, qa_error *error) {
    if (!movement || !game || !program || !save || !game->wire || game->wire->loading || !game->maps ||
        game->destroy_pending || game->continuation_pending || game->observation_depth ||
        game->options.quakeworld || game->options.max_clients != 1 || game->options.deathmatch ||
        !qa_session_safe(game->services.session) || !qa_world_idle(game->services.world) ||
        !qa_q1_save_singleplayer(save,error) || save->entity_count > UINT32_MAX ||
        (game->wire->edict_limit && save->entity_count > game->wire->edict_limit))
        return fail(error,"Original restore requires its idle compiled single-player Source");
    bool supported;
    if (!qa_q1_game_original_fit(game,save,&supported,error)) return false;
    if (!supported) return unsupported(error,"Original model identities differ from the actual constructed map");
    qa_actor_id *slots = calloc(save->entity_count,sizeof(*slots));
    if (!slots) { qa_error_set(error,QA_ERROR_MEMORY,0,"Restoring physical Source ordinal links"); return false; }
    qa_q1_wire_receipt receipt = {0};
    bool okay = qa_q1_wire_read_begin(game,&receipt,error);
    for (uint32_t slot=0; okay && slot<game->wire->next_dynamic; ++slot) {
        qa_actor_id id;
        if (!qa_q1_wire_actor_at(&receipt,slot,&id)) continue;
        const char *name = slot<save->entity_count ? saved(save->entities+slot,"classname") : NULL;
        q1_actor *entity = q1_entity(game,id);
        bool keep = slot<2 || (slot<save->entity_count && save->entities[slot].count && entity &&
            name && !strcmp(name,qa_strings_cstr(qa_session_strings(game->services.session),entity->classname)));
        if (keep) slots[slot] = id;
        else { okay = entity && q1_remove(game,entity,error); }
    }
    qa_q1_wire_read_end(&receipt);
    if (okay && (!slots[0].registry || !slots[1].registry)) okay=fail(error,"Original candidate lost its actual world/player edicts");
    if (okay) {
        okay = q1_wire_edict_extent(game->wire, (uint32_t)save->entity_count, error);
        if (okay) {
            game->wire->next_dynamic = (uint32_t)save->entity_count;
            for (size_t i=2; i<save->entity_count; ++i)
                game->wire->edicts[i] = (q1_wire_edict){.free = !save->entities[i].count};
        }
        for (uint32_t slot=2; okay && slot<save->entity_count; ++slot) {
            if (!save->entities[slot].count || slots[slot].registry) continue;
            q1_actor *entity;
            okay = create_original(game,save->entities+slot,slot,&entity,error);
            if (okay) slots[slot] = entity->id;
        }
    }
    if (okay) {
        game->time = (float)save->time; game->elapsed = 0;
        game->time_ns = (uint64_t)ceil((double)(float)save->time*1e9);
        char queue_name[32];original_field queue_global=body_queue_global(game,queue_name);
        okay=restore_fields(qa_session_strings(game->services.session),game->options.provider,&save->globals,
            game,&queue_global,1,slots,save->entity_count,error);
        if (okay) okay = RESTORE_FIELDS(game, &save->globals, game, game_globals, slots, save->entity_count, error) &&
            RESTORE_FIELDS(game, &save->globals, game->maps, map_globals, slots, save->entity_count, error) &&
            RESTORE_FIELDS(game, &save->globals, game->maps->options.server_flags, server_globals, slots, save->entity_count, error) &&
            restore_fields(qa_session_strings(game->services.session), game->options.provider, &save->globals,
                game, enemy_globals + (game->options.edition == QA_Q1_RERELEASE), 1, slots, save->entity_count, error);
    }
    for (size_t slot=0; okay && slot<save->entity_count; ++slot) {
        if (!save->entities[slot].count) continue;
        q1_player *player = q1_player_get(game,slots[slot]); q1_actor *entity=q1_entity(game,slots[slot]);
        if (!player && !entity) { okay=fail(error,"Original row lacks its actual compiled owner"); break; }
        qa_scheduler_cancel(qa_session_scheduler(game->services.session),slots[slot]);
        okay = restore_entity(game,entity,player,save->entities+slot,slots,save->entity_count,movement,error);
    }
    if (okay) okay=q1_body_queue_validate(game,error);
    if (okay) okay=restore_groups(game,save,slots,error);
    for (size_t slot=0; okay && slot<save->entity_count; ++slot) {
        if (!save->entities[slot].count) continue;
        q1_actor *entity=q1_entity(game,slots[slot]);
        if (!entity) { okay=qa_world_link(game->services.world,slots[slot],NULL,error); continue; }
        okay=q1_link(game,entity,error);
        if (okay && entity->think!=Q1_THINK_NONE && entity->next_think>0) {
            double due=entity->next_think; q1_think_kind kind=entity->think;
            okay=q1_schedule(game,entity,due-game->time,kind,error);
        }
    }
    if (okay) {
        const char *next = saved(save->entities+1,"think");
        for (size_t i=0; next && okay && i<sizeof(player_animations)/sizeof(*player_animations); ++i) {
            const original_player_animation *a=player_animations+i;
            if (a->first<119 || strncmp(next,a->prefix,strlen(a->prefix))) continue;
            unsigned frame=(unsigned)strtoul(next+strlen(a->prefix),NULL,10);
            if (frame==2 || frame==3) {
                q1_actor *timer;
                double due=saved_number(save->entities+1,"nextthink")+(frame==2?.1:0);
                okay=q1_create(game,"axe_strike",Q1_TIMER,slots[1],&timer,error) &&
                    q1_schedule(game,timer,due-game->time,Q1_THINK_AXE,error);
            }
            break;
        }
    }
    for (unsigned i=0; okay && i<64; ++i)
        okay=qa_strings_intern_cstr(qa_session_strings(game->services.session),save->lightstyles[i],&game->wire->lightstyles[i],error);
    if (okay) { ++game->wire->lightstyle_revision; q1_wire_changed(game->wire); }
    free(slots); return okay;
}
