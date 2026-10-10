#include "original_entities.h"
#include "original_edicts.h"
#include "qa/text.h"

/* g_func/g_trigger/g_target/g_misc names are the original save identities.
 * Runtime behavior stays in the existing typed entity services. */
typedef struct original_entity_class {
    const char *name;
    q2_entity_kind kind;
    q2_scenery_kind scenery;
    const char *use, *touch, *blocked, *die;
} original_entity_class;

#define CLASS(n, k, u, t, b, d) {n, k, Q2S_NONE, u, t, b, d}
#define SCENERY(n, k, u, t, d) {n, Q2E_SCENERY, k, u, t, NULL, d}
static const original_entity_class entity_classes[] = {
    CLASS("worldspawn", Q2E_WORLD, NULL, NULL, NULL, NULL),
    CLASS("trigger_multiple", Q2E_MULTI, "Use_Multi", "Touch_Multi", NULL, NULL),
    CLASS("trigger_once", Q2E_MULTI, "Use_Multi", "Touch_Multi", NULL, NULL),
    CLASS("trigger_relay", Q2E_RELAY, "trigger_relay_use", NULL, NULL, NULL),
    CLASS("trigger_counter", Q2E_COUNTER, "trigger_counter_use", NULL, NULL, NULL),
    CLASS("trigger_key", Q2E_KEY, "trigger_key_use", NULL, NULL, NULL),
    CLASS("target_speaker", Q2E_SPEAKER, "Use_Target_Speaker", NULL, NULL, NULL),
    CLASS("func_timer", Q2E_TIMER, "func_timer_use", NULL, NULL, NULL),
    CLASS("light", Q2E_LIGHT, "light_use", NULL, NULL, NULL),
    CLASS("func_areaportal", Q2E_PORTAL, "Use_Areaportal", NULL, NULL, NULL),
    CLASS("target_help", Q2E_HELP, "Use_Target_Help", NULL, NULL, NULL),
    CLASS("target_secret", Q2E_SECRET, "use_target_secret", NULL, NULL, NULL),
    CLASS("target_goal", Q2E_GOAL, "use_target_goal", NULL, NULL, NULL),
    CLASS("target_changelevel", Q2E_CHANGELEVEL, "use_target_changelevel", NULL, NULL, NULL),
    CLASS("target_explosion", Q2E_EXPLOSION, "use_target_explosion", NULL, NULL, NULL),
    CLASS("target_splash", Q2E_SPLASH, "use_target_splash", NULL, NULL, NULL),
    CLASS("trigger_push", Q2E_PUSH, "trigger_push_use", "trigger_push_touch", NULL, NULL),
    CLASS("trigger_hurt", Q2E_HURT, "hurt_use", "hurt_touch", NULL, NULL),
    CLASS("trigger_gravity", Q2E_GRAVITY, "trigger_gravity_use", "trigger_gravity_touch", NULL, NULL),
    CLASS("trigger_monsterjump", Q2E_MONSTERJUMP, "trigger_monsterjump_use", "trigger_monsterjump_touch", NULL, NULL),
    CLASS("trigger_teleport", Q2E_TELEPORT, NULL, "trigger_teleport_touch", NULL, NULL),
    CLASS("trigger_disguise", Q2E_DISGUISE, "trigger_disguise_use", "trigger_disguise_touch", NULL, NULL),
    CLASS("target_temp_entity", Q2E_TEMP, "Use_Target_Tent", NULL, NULL, NULL),
    CLASS("target_spawner", Q2E_SPAWNER, "use_target_spawner", NULL, NULL, NULL),
    CLASS("target_blaster", Q2E_BLASTER, "use_target_blaster", NULL, NULL, NULL),
    CLASS("target_crosslevel_trigger", Q2E_CROSS_TRIGGER, "trigger_crosslevel_trigger_use", NULL, NULL, NULL),
    CLASS("target_crosslevel_target", Q2E_CROSS_TARGET, NULL, NULL, NULL, NULL),
    CLASS("target_laser", Q2E_LASER, "target_laser_use", NULL, NULL, NULL),
    CLASS("target_lightramp", Q2E_LIGHTRAMP, "target_lightramp_use", NULL, NULL, NULL),
    CLASS("target_earthquake", Q2E_EARTHQUAKE, "target_earthquake_use", NULL, NULL, NULL),
    CLASS("target_steam", Q2E_STEAM, "use_target_steam", NULL, NULL, NULL),
    CLASS("target_anger", Q2E_ANGER, "target_anger_use", NULL, NULL, NULL),
    CLASS("target_killplayers", Q2E_KILLPLAYERS, "target_killplayers_use", NULL, NULL, NULL),
    CLASS("target_soundfx", Q2E_SOUND_FX, "use_target_soundfx", NULL, NULL, NULL),
    CLASS("target_gravity", Q2E_GLOBAL_GRAVITY, "use_target_gravity", NULL, NULL, NULL),
    CLASS("func_door", Q2E_DOOR, "door_use", "door_touch", "door_blocked", "door_killed"),
    CLASS("func_door_rotating", Q2E_DOOR, "door_use", "door_touch", "door_blocked", "door_killed"),
    CLASS("func_button", Q2E_BUTTON, "button_use", "button_touch", NULL, "button_killed"),
    CLASS("func_water", Q2E_WATER, "door_use", NULL, NULL, NULL),
    CLASS("func_train", Q2E_TRAIN, "train_use", NULL, "train_blocked", NULL),
    CLASS("misc_viper", Q2E_TRAIN, "misc_viper_use", NULL, NULL, NULL),
    CLASS("misc_strogg_ship", Q2E_TRAIN, "misc_strogg_ship_use", NULL, NULL, NULL),
    CLASS("misc_crashviper", Q2E_TRAIN, "misc_viper_use", NULL, NULL, NULL),
    CLASS("misc_transport", Q2E_TRAIN, "misc_strogg_ship_use", NULL, NULL, NULL),
    CLASS("func_rotating", Q2E_ROTATING, "rotating_use", "rotating_touch", "rotating_blocked", NULL),
    CLASS("path_corner", Q2E_PATH, NULL, "path_corner_touch", NULL, NULL),
    CLASS("point_combat", Q2E_COMBAT_POINT, NULL, "point_combat_touch", NULL, NULL),
    CLASS("door_trigger", Q2E_DOOR_TRIGGER, NULL, "Touch_DoorTrigger", NULL, NULL),
    CLASS("func_plat", Q2E_PLAT, "Use_Plat", NULL, "plat_blocked", NULL),
    CLASS("func_plat2", Q2E_PLAT, "Use_Plat2", NULL, "plat2_blocked", NULL),
    CLASS("plat_trigger", Q2E_PLAT_TRIGGER, NULL, "Touch_Plat_Center", NULL, NULL),
    CLASS("plat2_trigger", Q2E_PLAT_TRIGGER, NULL, "Touch_Plat_Center2", NULL, NULL),
    CLASS("func_door_secret", Q2E_SECRET_DOOR, "door_secret_use", "door_touch", "door_secret_blocked", "door_secret_die"),
    CLASS("func_door_secret2", Q2E_SECRET_DOOR, "fd_secret_use", "secret_touch", "secret_blocked", "fd_secret_killed"),
    CLASS("trigger_elevator", Q2E_ELEVATOR, "trigger_elevator_use", NULL, NULL, NULL),
    CLASS("func_conveyor", Q2E_CONVEYOR, "func_conveyor_use", NULL, NULL, NULL),
    CLASS("func_killbox", Q2E_KILLBOX, "use_killbox", NULL, NULL, NULL),
    CLASS("func_object", Q2E_OBJECT, "func_object_use", "func_object_touch", NULL, NULL),
    CLASS("func_force_wall", Q2E_FORCEWALL, "force_wall_use", NULL, NULL, NULL),
    SCENERY("func_wall", Q2S_WALL, "func_wall_use", NULL, NULL),
    SCENERY("func_explosive", Q2S_EXPLOSIVE, "func_explosive_use", NULL, "func_explosive_explode"),
    SCENERY("misc_explobox", Q2S_BARREL, NULL, "barrel_touch", "barrel_delay"),
    SCENERY("misc_banner", Q2S_BANNER, NULL, NULL, NULL),
    SCENERY("misc_ctf_banner", Q2S_BANNER, NULL, NULL, NULL),
    SCENERY("misc_ctf_small_banner", Q2S_BANNER, NULL, NULL, NULL),
    SCENERY("misc_satellite_dish", Q2S_SATELLITE, "misc_satellite_dish_use", NULL, NULL),
    SCENERY("misc_deadsoldier", Q2S_SOLDIER, NULL, NULL, "misc_deadsoldier_die"),
    SCENERY("misc_gib_head", Q2S_GIB, NULL, "gib_touch", "gib_die"),
    SCENERY("misc_gib_arm", Q2S_GIB, NULL, "gib_touch", "gib_die"),
    SCENERY("misc_gib_leg", Q2S_GIB, NULL, "gib_touch", "gib_die"),
    SCENERY("viewthing", Q2S_ANIMATION, NULL, NULL, NULL),
    SCENERY("misc_blackhole", Q2S_BLACKHOLE, "misc_blackhole_use", NULL, NULL),
    SCENERY("misc_eastertank", Q2S_ANIMATION, NULL, NULL, NULL),
    SCENERY("misc_easterchick", Q2S_ANIMATION, NULL, NULL, NULL),
    SCENERY("misc_easterchick2", Q2S_ANIMATION, NULL, NULL, NULL),
    SCENERY("monster_commander_body", Q2S_COMMANDER, "commander_body_use", NULL, NULL),
    SCENERY("misc_viper_bomb", Q2S_BOMB, "misc_viper_bomb_use", "misc_viper_bomb_touch", NULL),
    SCENERY("target_character", Q2S_CHARACTER, NULL, NULL, NULL),
    SCENERY("target_string", Q2S_STRING, "target_string_use", NULL, NULL),
    SCENERY("func_clock", Q2S_CLOCK, "func_clock_use", NULL, NULL),
    SCENERY("misc_teleporter", Q2S_TELEPORTER, NULL, NULL, NULL),
    SCENERY("teleporter", Q2S_TELEPORT_TRIGGER, NULL, "teleporter_touch", NULL),
    SCENERY("rotating_light", Q2S_ROTATING_LIGHT, "rotating_light_use", NULL, "rotating_light_killed"),
    SCENERY("func_object_repair", Q2S_REPAIR, NULL, NULL, NULL),
    SCENERY("misc_viper_missile", Q2S_MISSILE, "misc_viper_missile_use", NULL, NULL),
    SCENERY("misc_amb4", Q2S_AMBIENCE, NULL, NULL, NULL),
    SCENERY("misc_nuke", Q2S_NUKE, "misc_nuke_core_use", NULL, NULL),
    SCENERY("target_mal_laser", Q2S_MAL_LASER, "target_mal_laser_use", NULL, NULL),
    CLASS("misc_flare", Q2E_FLARE, "misc_flare_use", NULL, NULL, NULL),
    CLASS("info_world_text", Q2E_WORLD_TEXT, "info_world_text_use", NULL, NULL, NULL),
    CLASS("trigger_flashlight", Q2E_FLASHLIGHT, NULL, "trigger_flashlight_touch", NULL, NULL),
    CLASS("trigger_fog", Q2E_FOG, NULL, "trigger_fog_touch", NULL, NULL),
    CLASS("trigger_coop_relay", Q2E_COOP_RELAY, "trigger_coop_relay_use", NULL, NULL, NULL),
    CLASS("target_poi", Q2E_POI, "target_poi_use", NULL, NULL, NULL),
    CLASS("target_music", Q2E_MUSIC, "use_target_music", NULL, NULL, NULL),
    CLASS("target_sky", Q2E_SKY, "use_target_sky", NULL, NULL, NULL),
    CLASS("target_crossunit_trigger", Q2E_CROSS_UNIT_TRIGGER, "trigger_crossunit_trigger_use", NULL, NULL, NULL),
    CLASS("target_crossunit_target", Q2E_CROSS_UNIT_TARGET, NULL, NULL, NULL, NULL),
    CLASS("target_autosave", Q2E_AUTOSAVE, "use_target_autosave", NULL, NULL, NULL),
    CLASS("target_achievement", Q2E_ACHIEVEMENT, "use_target_achievement", NULL, NULL, NULL),
    CLASS("target_story", Q2E_STORY, "use_target_story", NULL, NULL, NULL),
    CLASS("target_healthbar", Q2E_HEALTHBAR, "use_target_healthbar", NULL, NULL, NULL),
    CLASS("target_light", Q2E_DYNAMIC_LIGHT, "target_light_use", NULL, NULL, NULL),
    CLASS("turret_base", Q2E_TURRET_BASE, NULL, NULL, "turret_blocked", NULL),
    CLASS("turret_breach", Q2E_TURRET_BREACH, NULL, NULL, "turret_blocked", NULL),
    CLASS("turret_driver", Q2E_TURRET_DRIVER, NULL, NULL, NULL, NULL),
    CLASS("func_eye", Q2E_EYE, NULL, NULL, NULL, NULL),
    CLASS("func_spinning", Q2E_SPINNING, NULL, NULL, NULL, NULL),
    CLASS("target_camera", Q2E_CAMERA, "use_target_camera", NULL, NULL, NULL),
    CLASS("target_camera_dummy", Q2E_CAMERA_DUMMY, NULL, NULL, NULL, NULL),
    CLASS("DelayedUse", Q2E_DELAYED_USE, NULL, NULL, NULL, NULL)
};
#undef CLASS
#undef SCENERY

static bool entity_error(q2_original_record_io *io, size_t field, const char *message)
{
    qa_error_set(io->error, QA_ERROR_FORMAT, field, "%s", message);
    return false;
}

static const original_entity_class *entity_class(qa_q2_game *g, const q2_entity_state *s)
{
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof(entity_classes) / sizeof(*entity_classes); ++i)
        if (!strcmp(name, entity_classes[i].name)) return entity_classes + i;
    if (s->kind == Q2E_DOOR_TRIGGER || s->kind == Q2E_PLAT_TRIGGER ||
        s->kind == Q2E_CAMERA_DUMMY ||
        (s->kind == Q2E_SCENERY && s->scenery == Q2S_TELEPORT_TRIGGER))
        for (size_t i = 0; i < sizeof(entity_classes) / sizeof(*entity_classes); ++i)
            if (entity_classes[i].kind == s->kind && entity_classes[i].scenery == s->scenery &&
                (s->kind != Q2E_PLAT_TRIGGER || strcmp(entity_classes[i].name,
                    s->stage == 2 ? "plat2_trigger" : "plat_trigger") == 0))
                return entity_classes + i;
    return NULL;
}

static bool scalar(q2_original_record_io *io, const char *name,
    q2_original_field_kind kind, uint16_t offset, void *value)
{
    return q2_original_scalar(io, name, kind, offset, offset, offset, value);
}

static bool original_mover_kind(q2_entity_kind kind)
{
    return kind == Q2E_DOOR || kind == Q2E_BUTTON || kind == Q2E_WATER ||
        kind == Q2E_TRAIN || kind == Q2E_PLAT || kind == Q2E_SECRET_DOOR;
}

static bool read_entity(qa_q2_game *g, q2_original_record_io *io, q2_actor *a)
{
    qa_string_id classname = 0;
    if (!q2_original_string(g, io, "classname", 280, &classname)) return false;
    if (!a->entity) {
        q2_entity_state state = {.classname = classname};
        const original_entity_class *type = entity_class(g, &state);
        if (!type) {
            /* Original generated trigger edicts have the default classname. */
            static const struct { const char *field, *name, *classname; uint16_t offset; } helpers[] = {
                {"touch", "Touch_DoorTrigger", "door_trigger", 444},
                {"touch", "Touch_Plat_Center", "plat_trigger", 444},
                {"touch", "Touch_Plat_Center2", "plat2_trigger", 444},
                {"touch", "teleporter_touch", "teleporter", 444},
                {"think", "target_camera_dummy_think", "target_camera_dummy", 436}
            };
            for (size_t i = 0; i < sizeof(helpers) / sizeof(*helpers); ++i) {
                bool match;
                if (!q2_original_function_matches(g, io, helpers[i].field, helpers[i].offset,
                    helpers[i].name, &match)) return false;
                if (match) {
                    if (!qa_strings_intern(qa_session_strings(g->services.session),
                        (qa_bytes){(const uint8_t *)helpers[i].classname, strlen(helpers[i].classname)},
                        &state.classname, io->error)) return false;
                    type = entity_class(g, &state);
                    break;
                }
            }
        }
        a->entity = q2_entity_state_take(g, io->error);
        if (!a->entity) {
            return false;
        }
        a->entity_game = g;
        a->entity->ordinal = UINT32_MAX;
        a->entity->classname = classname;
        a->entity->kind = type ? type->kind : Q2E_POINT;
        a->entity->scenery = type ? type->scenery : Q2S_NONE;
        if (type && !strcmp(type->name, "plat2_trigger")) a->entity->stage = 2;
    } else a->entity->classname = classname;
    if (a->entity->kind == Q2E_LIGHT) {
        bool dynamic;
        if (!q2_original_function_matches(g, io, "use", 448, "dynamic_light_use", &dynamic)) return false;
        if (dynamic) { a->entity->kind = Q2E_DYNAMIC_LIGHT; a->entity->stage = 1; }
    }
    qa_string_id model = 0;
    if (!q2_original_string(g, io, "model", 268, &model)) return false;
    const char *model_name = qa_strings_cstr(qa_session_strings(g->services.session), model);
    a->entity->has_inline = model_name && *model_name == '*';
    a->entity->collision = (qa_actor_collision){.family = QA_COLLISION_Q2, .shape = QA_SHAPE_BOX,
        .inline_model = a->entity->has_inline, .contents = qa_collision_contents_decode(1, QA_COLLISION_Q2)};
    if (a->entity->has_inline) {
        double index;
        if (!qa_parse_number((qa_bytes){(const uint8_t *)model_name + 1, strlen(model_name + 1)},
            &index, io->error)) return false;
        if (!isfinite(index) || index < 0 || index > UINT32_MAX || trunc(index) != index)
            return entity_error(io, 268, "Original Q2 inline model has an invalid index");
        a->entity->collision.model = (uint32_t)index;
    }
    int32_t solid = 0;
    if (!scalar(io, "solid", Q2_ORIGINAL_I32, 248, &solid)) return false;
    a->entity->collision.role = solid == 1 ? QA_COLLISION_TRIGGER : QA_COLLISION_SOLID;
    return true;
}

static bool entity_strings(qa_q2_game *g, q2_original_record_io *io, q2_entity_state *s)
{
    if (!io->reading) return true; /* The common edict owns the single string tail. */
    if (!q2_original_string(g, io, "target", 296, &s->target) ||
        !q2_original_string(g, io, "targetname", 300, &s->targetname) ||
        !q2_original_string(g, io, "killtarget", 304, &s->killtarget) ||
        !q2_original_string(g, io, "message", 276, &s->message) ||
        !q2_original_string(g, io, "team", 308, &s->team) ||
        !q2_original_string(g, io, "map", 504, &s->map)) return false;
    static const char *const names[] = {"pathtarget", "deathtarget", "combattarget"};
    static const uint16_t offsets[] = {312, 316, 320};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i) {
        qa_string_id value = 0;
        if (!q2_original_string(g, io, names[i], offsets[i], &value)) return false;
        qa_string_id key;
        if (!qa_strings_intern(qa_session_strings(g->services.session),
            (qa_bytes){(const uint8_t *)names[i], strlen(names[i])}, &key, io->error)) return false;
        size_t j;
        for (j = 0; j < s->field_count; ++j)
            if (s->fields[j].key == key) break;
        if (j == s->field_count) {
            if (!value) continue;
            q2_field *fields = qa_arena_grow(&g->entity_fields, s->fields,
                j * sizeof(*fields), (j + 1) * sizeof(*fields), _Alignof(q2_field), io->error);
            if (!fields) {
                qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring original Q2 target fields");
                return false;
            }
            s->fields = fields;
            s->field_count++;
        }
        s->fields[j] = (q2_field){key, value};
    }
    return true;
}

static bool entity_team_references(qa_q2_game *g, q2_original_record_io *io, q2_entity_state *s)
{
    return q2_original_source_reference(g, io, "teammaster", 564, &s->team_master) &&
        q2_original_source_reference(g, io, "teamchain", 560, &s->team_next);
}

static bool entity_references(qa_q2_game *g, q2_original_record_io *io, q2_entity_state *s)
{
    if (s->kind == Q2E_TURRET_DRIVER)
        return q2_original_reference(g, io, "owner", 256, &s->owner) &&
            entity_team_references(g, io, s) &&
            q2_original_reference(g, io, "target_ent", 324, &s->turret->breach);
    qa_actor_id *goal = s->kind == Q2E_ELEVATOR ? &s->enemy : &s->goal;
    bool move_target = s->kind == Q2E_ELEVATOR || s->kind == Q2E_CAMERA;
    uint16_t goal_offset = move_target ? 416 : s->kind == Q2E_DYNAMIC_LIGHT ? 536 : 412;
    const char *goal_name = move_target ? "movetarget" : s->kind == Q2E_DYNAMIC_LIGHT ? "chain" : "goalentity";
    return q2_original_reference(g, io, "owner", 256, &s->owner) &&
        q2_original_reference(g, io, "enemy", 540, &s->enemy) &&
        q2_original_reference(g, io, "activator", 548, &s->activator) &&
        q2_original_reference(g, io, goal_name, goal_offset, goal) &&
        entity_team_references(g, io, s) &&
        (!s->mover || q2_original_reference(g, io, "target_ent", 324, &s->mover->destination));
}

/* Each entry identifies a Source callback. Qualifiers resolve folded code
 * aliases using the actual entity kind instead of an address-name guess. */
typedef struct original_entity_think {
    const char *name;
    q2_entity_think think;
    int kind, scenery, stage;
    bool angular;
} original_entity_think;
#define THINK(n, t, k) {n, t, k, -1, -1, false}
#define ANGL(n, t) {n, t, -1, -1, -1, true}
#define STAGE(n, t, k, s) {n, t, k, -1, s, false}
#define ANIM(n, s, stage) {n, Q2ET_SCENERY, Q2E_SCENERY, s, stage, false}
static const original_entity_think entity_thinks[] = {
    THINK("G_FreeEdict", Q2ET_FREE, -1),
    THINK("multi_wait", Q2ET_MULTI_READY, Q2E_MULTI),
    THINK("func_timer_think", Q2ET_TIMER, Q2E_TIMER),
    THINK("target_explosion_explode", Q2ET_EXPLOSION, Q2E_EXPLOSION),
    THINK("target_laser_start", Q2ET_LASER_START, Q2E_LASER),
    THINK("target_laser_think", Q2ET_LASER, Q2E_LASER),
    THINK("target_lightramp_think", Q2ET_LIGHTRAMP, Q2E_LIGHTRAMP),
    THINK("target_crosslevel_target_think", Q2ET_CROSS, Q2E_CROSS_TARGET),
    THINK("target_crossunit_target_think", Q2ET_CROSS, Q2E_CROSS_UNIT_TARGET),
    THINK("target_earthquake_think", Q2ET_QUAKE, Q2E_EARTHQUAKE),
    THINK("target_steam_start", Q2ET_STEAM, Q2E_STEAM),
    THINK("update_target_soundfx", Q2ET_SOUND_FX, Q2E_SOUND_FX),
    THINK("trigger_push_active", Q2ET_PUSH, Q2E_PUSH),
    THINK("trigger_push_inactive", Q2ET_PUSH, Q2E_PUSH),
    THINK("Move_Begin", Q2ET_MOVE_BEGIN, -1),
    THINK("Move_Final", Q2ET_MOVE_FINAL, -1),
    THINK("Move_Done", Q2ET_MOVE_DONE, -1),
    ANGL("AngleMove_Begin", Q2ET_MOVE_BEGIN),
    ANGL("AngleMove_Final", Q2ET_MOVE_FINAL),
    ANGL("AngleMove_Done", Q2ET_MOVE_DONE),
    THINK("Think_AccelMove", Q2ET_MOVE_ACCEL, -1),
    THINK("Think_AccelMove_New", Q2ET_MOVE_ACCEL, -1),
    THINK("Think_CalcMoveSpeed", Q2ET_DOOR_PREPARE, -1),
    THINK("Think_SpawnDoorTrigger", Q2ET_DOOR_PREPARE, -1),
    THINK("door_go_down", Q2ET_DOOR_DOWN, Q2E_DOOR),
    THINK("door_go_down", Q2ET_DOOR_DOWN, Q2E_WATER),
    THINK("button_return", Q2ET_DOOR_DOWN, Q2E_BUTTON),
    THINK("func_train_find", Q2ET_TRAIN_FIND, Q2E_TRAIN),
    THINK("train_next", Q2ET_TRAIN_NEXT, Q2E_TRAIN),
    STAGE("door_secret_move2", Q2ET_SECRET_NEXT, Q2E_SECRET_DOOR, 1),
    STAGE("door_secret_move4", Q2ET_SECRET_NEXT, Q2E_SECRET_DOOR, 3),
    STAGE("door_secret_move6", Q2ET_SECRET_NEXT, Q2E_SECRET_DOOR, 5),
    STAGE("fd_secret_move2", Q2ET_SECRET_NEXT, Q2E_SECRET_DOOR, 1),
    STAGE("fd_secret_move4", Q2ET_SECRET_NEXT, Q2E_SECRET_DOOR, 3),
    STAGE("fd_secret_move6", Q2ET_SECRET_NEXT, Q2E_SECRET_DOOR, 5),
    THINK("func_object_release", Q2ET_OBJECT_FALL, Q2E_OBJECT),
    THINK("smart_water_go_up", Q2ET_SMART_WATER, Q2E_WATER),
    THINK("plat_go_down", Q2ET_PLAT_DOWN, Q2E_PLAT),
    THINK("plat2_go_down", Q2ET_PLAT_DOWN, Q2E_PLAT),
    THINK("plat2_go_up", Q2ET_PLAT_UP, Q2E_PLAT),
    THINK("rotating_accel", Q2ET_ROTATE_ACCEL, Q2E_ROTATING),
    THINK("rotating_decel", Q2ET_ROTATE_DECEL, Q2E_ROTATING),
    THINK("trigger_elevator_init", Q2ET_ELEVATOR, Q2E_ELEVATOR),
    THINK("force_wall_think", Q2ET_FORCEWALL, Q2E_FORCEWALL),
    ANIM("misc_banner_think", Q2S_BANNER, -1),
    ANIM("misc_satellite_dish_think", Q2S_SATELLITE, -1),
    ANIM("misc_blackhole_think", Q2S_BLACKHOLE, -1),
    ANIM("TH_viewthing", Q2S_ANIMATION, -1),
    ANIM("misc_eastertank_think", Q2S_ANIMATION, -1),
    ANIM("misc_easterchick_think", Q2S_ANIMATION, -1),
    ANIM("misc_easterchick2_think", Q2S_ANIMATION, -1),
    ANIM("commander_body_drop", Q2S_COMMANDER, 0),
    ANIM("commander_body_think", Q2S_COMMANDER, 1),
    ANIM("func_clock_think", Q2S_CLOCK, -1),
    ANIM("M_droptofloor", Q2S_BARREL, Q2_BARREL_DROP),
    ANIM("barrel_start", Q2S_BARREL, Q2_BARREL_DROP),
    ANIM("barrel_think", Q2S_BARREL, Q2_BARREL_IDLE),
    ANIM("barrel_burn", Q2S_BARREL, Q2_BARREL_BURN),
    ANIM("barrel_explode", Q2S_BARREL, Q2_BARREL_EXPLODE),
    ANIM("rotating_light_alarm", Q2S_ROTATING_LIGHT, -1),
    ANIM("object_repair_sparks", Q2S_REPAIR, 0),
    ANIM("object_repair_dead", Q2S_REPAIR, 1),
    ANIM("object_repair_fx", Q2S_REPAIR, 2),
    ANIM("amb4_think", Q2S_AMBIENCE, -1),
    ANIM("mal_laser_think", Q2S_MAL_LASER, -1),
    THINK("Think_Delay", Q2ET_DELAYED_USE, Q2E_DELAYED_USE),
    THINK("info_player_start_drop", Q2ET_PLAYER_START_DROP, Q2E_POINT),
    THINK("SP_CreateCoopSpots", Q2ET_PLAYER_SECURITY, Q2E_POINT),
    THINK("SP_FixCoopSpots", Q2ET_PLAYER_COOP_FIX, Q2E_POINT),
    THINK("info_world_text_think", Q2ET_WORLD_TEXT, Q2E_WORLD_TEXT),
    THINK("trigger_coop_relay_think", Q2ET_COOP_RELAY, Q2E_COOP_RELAY),
    THINK("target_poi_setup", Q2ET_POI, Q2E_POI),
    THINK("check_target_healthbar", Q2ET_HEALTHBAR, Q2E_HEALTHBAR),
    THINK("target_light_think", Q2ET_DYNAMIC_LIGHT, Q2E_DYNAMIC_LIGHT),
    THINK("target_light_flicker_think", Q2ET_LIGHT_FLICKER, Q2E_DYNAMIC_LIGHT),
    THINK("func_eye_setup", Q2ET_EYE_SETUP, Q2E_EYE),
    THINK("func_eye_think", Q2ET_EYE, Q2E_EYE),
    THINK("func_spinning_think", Q2ET_SPINNING, Q2E_SPINNING),
    THINK("update_target_camera", Q2ET_CAMERA, Q2E_CAMERA),
    THINK("target_camera_dummy_think", Q2ET_CAMERA_DUMMY, Q2E_CAMERA_DUMMY),
    THINK("turret_breach_finish_init", Q2ET_TURRET_INIT, Q2E_TURRET_BREACH),
    THINK("turret_breach_think", Q2ET_TURRET, Q2E_TURRET_BREACH)
};
#undef THINK
#undef ANGL
#undef STAGE
#undef ANIM

static bool think_applies(const original_entity_think *entry, const q2_entity_state *s)
{
    if ((entry->kind >= 0 && entry->kind != (int)s->kind) ||
        (entry->scenery >= 0 && entry->scenery != (int)s->scenery)) return false;
    if (entry->think >= Q2ET_MOVE_BEGIN && entry->think <= Q2ET_MOVE_ACCEL)
        return s->mover != NULL;
    if (entry->think == Q2ET_DOOR_PREPARE)
        return s->kind == Q2E_DOOR || s->kind == Q2E_BUTTON || s->kind == Q2E_WATER;
    return true;
}

static const char *scenery_think(qa_q2_game *g, const q2_entity_state *s)
{
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
    switch (s->scenery) {
    case Q2S_BANNER: return "misc_banner_think";
    case Q2S_SATELLITE: return "misc_satellite_dish_think";
    case Q2S_BLACKHOLE: return "misc_blackhole_think";
    case Q2S_ANIMATION:
        return !strcmp(name, "viewthing") ? "TH_viewthing" :
            !strcmp(name, "misc_eastertank") ? "misc_eastertank_think" :
            !strcmp(name, "misc_easterchick") ? "misc_easterchick_think" :
            !strcmp(name, "misc_easterchick2") ? "misc_easterchick2_think" : NULL;
    case Q2S_COMMANDER: return s->stage ? "commander_body_think" : "commander_body_drop";
    case Q2S_CLOCK: return "func_clock_think";
    case Q2S_BARREL:
        return s->stage == Q2_BARREL_EXPLODE ? "barrel_explode" :
            s->stage == Q2_BARREL_BURN ? "barrel_burn" :
            s->stage == Q2_BARREL_IDLE ? "barrel_think" :
            g->options.edition == QA_Q2_RERELEASE || g->options.product == QA_Q2_ROGUE ?
                "barrel_start" : "M_droptofloor";
    case Q2S_ROTATING_LIGHT: return "rotating_light_alarm";
    case Q2S_REPAIR: return s->stage == 1 ? "object_repair_dead" :
        s->stage == 2 ? "object_repair_fx" : "object_repair_sparks";
    case Q2S_AMBIENCE: return "amb4_think";
    case Q2S_MAL_LASER: return "mal_laser_think";
    default: return NULL;
    }
}

static const char *think_name(qa_q2_game *g, const q2_entity_state *s)
{
    if (s->think == Q2ET_MOVE_ACCEL && s->mover && s->mover->motion.curve)
        return "Think_AccelMove_New";
    if (s->think == Q2ET_NONE) {
        switch (s->kind) {
        case Q2E_TIMER: return "func_timer_think";
        case Q2E_LASER: return "target_laser_think";
        case Q2E_LIGHTRAMP: return "target_lightramp_think";
        case Q2E_EARTHQUAKE: return "target_earthquake_think";
        case Q2E_SCENERY: return s->scenery == Q2S_CLOCK ? "func_clock_think" : NULL;
        default: return NULL;
        }
    }
    if (s->think == Q2ET_SCENERY) return scenery_think(g, s);
    if (s->think == Q2ET_SECRET_NEXT) {
        static const char *const classic[] = {"door_secret_move2", "door_secret_move4", "door_secret_move6"};
        static const char *const rogue[] = {"fd_secret_move2", "fd_secret_move4", "fd_secret_move6"};
        const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
        int stage = s->mover ? s->mover->stage : 0;
        return stage == 1 || stage == 3 || stage == 5 ?
            (!strcmp(name, "func_door_secret2") ? rogue : classic)[(stage - 1) / 2] : NULL;
    }
    if (s->think == Q2ET_DOOR_PREPARE)
        return s->health != 0 || s->targetname ? "Think_CalcMoveSpeed" : "Think_SpawnDoorTrigger";
    if (s->think == Q2ET_PLAT_UP || s->think == Q2ET_PLAT_DOWN) {
        const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
        if (!strcmp(name, "func_plat2"))
            return s->think == Q2ET_PLAT_UP ? "plat2_go_up" : "plat2_go_down";
        return s->think == Q2ET_PLAT_DOWN ? "plat_go_down" : NULL;
    }
    for (size_t i = 0; i < sizeof(entity_thinks) / sizeof(*entity_thinks); ++i) {
        const original_entity_think *entry = entity_thinks + i;
        if (entry->think == s->think && think_applies(entry, s) &&
            (entry->think < Q2ET_MOVE_BEGIN || entry->think > Q2ET_MOVE_ACCEL ||
                entry->angular == (s->mover && s->mover->motion.angular))) return entry->name;
    }
    return NULL;
}

static bool entity_think(qa_q2_game *g, q2_original_record_io *io, q2_entity_state *s)
{
    if (!io->reading) {
        const char *name = think_name(g, s);
        if (!name && s->think != Q2ET_NONE)
            return entity_error(io, 436, "Q2 entity schedule has no original Source callback");
        return q2_original_function(g, io, "think", 436, name);
    }
    bool empty;
    if (!q2_original_function_matches(g, io, "think", 436, NULL, &empty)) return false;
    if (empty) { s->think = Q2ET_NONE; return true; }
    for (size_t i = 0; i < sizeof(entity_thinks) / sizeof(*entity_thinks); ++i) {
        const original_entity_think *entry = entity_thinks + i;
        if (!think_applies(entry, s)) continue;
        bool match;
        if (!q2_original_function_matches(g, io, "think", 436, entry->name, &match)) return false;
        if (!match) continue;
        s->think = s->due_ns ? entry->think : Q2ET_NONE;
        if (entry->stage >= 0) {
            if (entry->think == Q2ET_SECRET_NEXT) s->mover->stage = entry->stage;
            else s->stage = entry->stage;
        }
        if (s->mover && entry->think >= Q2ET_MOVE_BEGIN && entry->think <= Q2ET_MOVE_ACCEL)
            s->mover->motion.angular = entry->angular;
        return true;
    }
    return entity_error(io, 436, "Original Q2 entity think callback has no native continuation");
}

typedef struct original_entity_end {
    const char *name;
    q2_move_done done;
    int kind, stage;
} original_entity_end;
static const original_entity_end entity_ends[] = {
    {"door_hit_top", Q2MD_DOOR_TOP, Q2E_DOOR, -1},
    {"door_hit_bottom", Q2MD_DOOR_BOTTOM, Q2E_DOOR, -1},
    {"door_hit_top", Q2MD_DOOR_TOP, Q2E_WATER, -1},
    {"door_hit_bottom", Q2MD_DOOR_BOTTOM, Q2E_WATER, -1},
    {"button_wait", Q2MD_DOOR_TOP, Q2E_BUTTON, -1},
    {"button_done", Q2MD_DOOR_BOTTOM, Q2E_BUTTON, -1},
    {"train_wait", Q2MD_TRAIN_WAIT, Q2E_TRAIN, -1},
    {"plat_hit_top", Q2MD_PLAT_TOP, Q2E_PLAT, -1},
    {"plat_hit_bottom", Q2MD_PLAT_BOTTOM, Q2E_PLAT, -1},
    {"plat2_hit_top", Q2MD_PLAT_TOP, Q2E_PLAT, -1},
    {"plat2_hit_bottom", Q2MD_PLAT_BOTTOM, Q2E_PLAT, -1},
    {"door_secret_move1", Q2MD_SECRET_NEXT, Q2E_SECRET_DOOR, 0},
    {"door_secret_move3", Q2MD_SECRET_NEXT, Q2E_SECRET_DOOR, 2},
    {"door_secret_move5", Q2MD_SECRET_NEXT, Q2E_SECRET_DOOR, 4},
    {"door_secret_done", Q2MD_SECRET_NEXT, Q2E_SECRET_DOOR, 6},
    {"fd_secret_move1", Q2MD_SECRET_NEXT, Q2E_SECRET_DOOR, 0},
    {"fd_secret_move3", Q2MD_SECRET_NEXT, Q2E_SECRET_DOOR, 2},
    {"fd_secret_move5", Q2MD_SECRET_NEXT, Q2E_SECRET_DOOR, 4},
    {"fd_secret_done", Q2MD_SECRET_NEXT, Q2E_SECRET_DOOR, 6}
};

static bool entity_end(qa_q2_game *g, q2_original_record_io *io, q2_entity_state *s)
{
    q2_mover *m = s->mover;
    if (!io->reading) {
        const char *name = NULL;
        const char *classname = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
        for (size_t i = 0; i < sizeof(entity_ends) / sizeof(*entity_ends); ++i) {
            const original_entity_end *entry = entity_ends + i;
            if (entry->kind != (int)s->kind || entry->done != m->motion.done ||
                (entry->stage >= 0 && entry->stage != m->stage)) continue;
            bool second = !strcmp(classname, "func_door_secret2") || !strcmp(classname, "func_plat2");
            bool second_callback = !strncmp(entry->name, "fd_secret_", 10) ||
                !strncmp(entry->name, "plat2_", 6);
            if (second != second_callback) continue;
            name = entry->name;
            break;
        }
        if (!name && m->motion.done != Q2MD_NONE)
            return entity_error(io, 768, "Q2 mover completion has no original Source callback");
        return q2_original_function(g, io, "moveinfo.endfunc", 768, name);
    }
    bool empty;
    if (!q2_original_function_matches(g, io, "moveinfo.endfunc", 768, NULL, &empty)) return false;
    if (empty) { m->motion.done = Q2MD_NONE; return true; }
    for (size_t i = 0; i < sizeof(entity_ends) / sizeof(*entity_ends); ++i) {
        const original_entity_end *entry = entity_ends + i;
        if (entry->kind != (int)s->kind) continue;
        bool match;
        if (!q2_original_function_matches(g, io, "moveinfo.endfunc", 768, entry->name, &match)) return false;
        if (!match) continue;
        m->motion.done = entry->done;
        if (entry->stage >= 0 && m->moving) m->stage = entry->stage;
        return true;
    }
    return entity_error(io, 768, "Original Q2 mover end callback has no native continuation");
}

static bool entity_motion(qa_q2_game *g, q2_original_record_io *io, q2_actor *a)
{
    q2_entity_state *s = a->entity;
    q2_mover *m = s->mover;
    if (!m) return true;
    bool angular = !strcmp(qa_strings_cstr(qa_session_strings(g->services.session), s->classname),
        "func_door_rotating");
    q2_mover source = *m;
    if (!io->reading) {
        if (s->kind == Q2E_PLAT) { source.start = m->end; source.end = m->start; }
        else if (s->kind == Q2E_SECRET_DOOR) source.start = m->intermediate;
        if (angular && io->edition == QA_Q2_RERELEASE) source.motion.direction = m->safe_direction;
    }
    if (!q2_original_record(io, Q2_ORIGINAL_MOVER, &source)) return false;
    bool platform = s->kind == Q2E_PLAT;
    qa_vec3 start_origin = platform ? m->end : m->start;
    qa_vec3 end_origin = platform ? m->start : m->end;
    if (s->kind == Q2E_SECRET_DOOR) start_origin = m->intermediate;
    qa_vec3 start_angles = m->angular ? m->start : qa_v3(0, 0, 0);
    qa_vec3 end_angles = m->angular ? m->end : qa_v3(0, 0, 0);
    qa_vec3 reversed_angles = qa_vec_scale(m->end, -1);
    if (!io->reading && angular) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, io->error)) return false;
        start_origin = end_origin = body.origin;
    }
    float move_accel = s->accel, move_decel = s->decel;
    float move_speed = angular && m->moving ? m->motion.current_speed : s->speed;
    static const int phases[] = {1, 2, 0, 3}; /* Native bottom/up/top/down -> Source. */
    int phase = io->reading ? 0 : (unsigned)m->phase < 4 ? phases[m->phase] : -1;
    if (phase < 0) return entity_error(io, 732, "Q2 mover has an invalid Source phase");
    if (!scalar(io, "moveinfo.start_origin", Q2_ORIGINAL_VECTOR, 652, &start_origin) ||
        !scalar(io, "moveinfo.start_angles", Q2_ORIGINAL_VECTOR, 664, &start_angles) ||
        !scalar(io, "moveinfo.end_origin", Q2_ORIGINAL_VECTOR, 676, &end_origin) ||
        !scalar(io, "moveinfo.end_angles", Q2_ORIGINAL_VECTOR, 688, &end_angles) ||
        !scalar(io, "moveinfo.accel", Q2_ORIGINAL_F32, 712, &move_accel) ||
        !scalar(io, "moveinfo.speed", Q2_ORIGINAL_F32, 716, &move_speed) ||
        !scalar(io, "moveinfo.decel", Q2_ORIGINAL_F32, 720, &move_decel) ||
        !scalar(io, "moveinfo.wait", Q2_ORIGINAL_F32, 728, &s->wait) ||
        !scalar(io, "moveinfo.state", Q2_ORIGINAL_I32, 732, &phase)) return false;
    if (io->reading) {
        if ((unsigned)phase >= 4) return entity_error(io, 732, "Original Q2 mover has an invalid phase");
        static const int native_phases[] = {2, 0, 1, 3};
        m->phase = native_phases[phase];
        m->motion = source.motion;
        m->distance = source.distance;
        m->angular = angular;
        if (angular) {
            m->motion.current_speed = move_speed;
            if (io->edition == QA_Q2_RERELEASE) m->safe_direction = source.motion.direction;
        } else {
            s->speed = move_speed; s->accel = move_accel; s->decel = move_decel;
        }
        m->start = m->angular ? start_angles : platform ? end_origin : source.start;
        m->end = m->angular ? end_angles : platform ? start_origin : source.end;
        if (s->kind == Q2E_SECRET_DOOR) {
            m->intermediate = source.start;
            const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
            if (!strcmp(name, "func_door_secret2")) {
                if (!scalar(io, "move_origin", Q2_ORIGINAL_VECTOR, 616, &m->start)) return false;
            } else m->start = qa_v3(0, 0, 0);
        }
        m->activated = !s->targetname || (s->spawnflags &
            (s->kind == Q2E_SECRET_DOOR && !strcmp(qa_strings_cstr(
                qa_session_strings(g->services.session), s->classname), "func_door_secret2") ? 16u : 1u));
        m->ship = s->kind == Q2E_TRAIN && strcmp(qa_strings_cstr(
            qa_session_strings(g->services.session), s->classname), "func_train");
        m->moving = s->think >= Q2ET_MOVE_BEGIN && s->think <= Q2ET_MOVE_ACCEL;
        m->motion.angular = m->angular;
        m->motion.accelerated = s->think == Q2ET_MOVE_ACCEL;
    }
    if (angular && io->edition == QA_Q2_RERELEASE &&
        (!q2_original_scalar(io, "moveinfo.reversing", Q2_ORIGINAL_BOOL,
            UINT16_MAX, UINT16_MAX, UINT16_MAX, &m->reversed) ||
         !q2_original_scalar(io, "moveinfo.end_angles_reversed", Q2_ORIGINAL_VECTOR,
            UINT16_MAX, UINT16_MAX, UINT16_MAX, &reversed_angles))) return false;
    if (!entity_end(g, io, s)) return false;
    if (io->reading) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, io->error)) return false;
        if (s->kind == Q2E_SECRET_DOOR)
            m->motion.destination = m->stage == 0 || m->stage == 4 ? m->intermediate :
                m->stage == 2 ? m->end : m->start;
        else if (s->kind == Q2E_TRAIN)
            m->motion.destination = qa_vec_add(body.origin,
                qa_vec_scale(m->motion.direction, m->motion.remaining));
        else m->motion.destination = m->phase == 1 || m->phase == 2 ?
            angular && m->reversed ? reversed_angles : m->end : m->start;
        m->motion.reference = m->motion.angular ? body.angles : body.origin;
    }
    if (s->kind == Q2E_SECRET_DOOR && !strcmp(qa_strings_cstr(
        qa_session_strings(g->services.session), s->classname), "func_door_secret2") && !io->reading &&
        !scalar(io, "move_origin", Q2_ORIGINAL_VECTOR, 616, &m->start)) return false;
    if (!angular && io->edition == QA_Q2_RERELEASE &&
        !scalar(io, "moveinfo.dest", Q2_ORIGINAL_VECTOR, UINT16_MAX, &m->motion.destination)) return false;
    return true;
}

static bool curve_emit(void *context, float distance, qa_error *error)
{
    q2_original_record_io *io = context;
    qa_json_writer_number(io->writer, distance);
    if (!io->writer->failed) return true;
    if (error) *error = io->writer->failure;
    return false;
}

static bool entity_curve(qa_q2_game *g, q2_original_record_io *io, q2_entity_state *s)
{
    if (!s->mover || io->edition != QA_Q2_RERELEASE) return true;
    q2_motion *m = &s->mover->motion;
    if (io->reading) {
        bool accelerated;
        if (!q2_original_function_matches(g, io, "think", 436,
            "Think_AccelMove_New", &accelerated)) return false;
        if (!accelerated) return true;
    } else if (!m->curve) return true;
    qa_vec3 reference = m->reference;
    uint32_t frame = m->curve_frame, subframe = m->curve_subframe, subframes = m->curve_subframes;
    uint64_t frames_done = m->curve_frames_done;
    if (!q2_original_scalar(io, "moveinfo.curve_ref", Q2_ORIGINAL_VECTOR,
            UINT16_MAX, UINT16_MAX, UINT16_MAX, &reference) ||
        !q2_original_scalar(io, "moveinfo.curve_frame", Q2_ORIGINAL_U32,
            UINT16_MAX, UINT16_MAX, UINT16_MAX, &frame) ||
        !q2_original_scalar(io, "moveinfo.subframe", Q2_ORIGINAL_U8,
            UINT16_MAX, UINT16_MAX, UINT16_MAX, &subframe) ||
        !q2_original_scalar(io, "moveinfo.num_subframes", Q2_ORIGINAL_U8,
            UINT16_MAX, UINT16_MAX, UINT16_MAX, &subframes) ||
        !q2_original_scalar(io, "moveinfo.num_frames_done", Q2_ORIGINAL_U64,
            UINT16_MAX, UINT16_MAX, UINT16_MAX, &frames_done)) return false;
    if (io->reading) {
        if (!q2_move_curve_restore(m, s, reference, frame, subframe, subframes,
            frames_done, io->error)) return false;
        /* Source stores generated distances. Rebuild their scalar continuation
         * with the same motion kernel; no trajectory allocation survives load. */
        qa_json_id points = qa_json_get(io->document, io->object, "moveinfo.curve_positions");
        if (points == QA_JSON_NONE) return true;
        if (qa_json_type(io->document, points) != QA_JSON_ARRAY)
            return entity_error(io, points, "Original Q2 curve positions are not an array");
        size_t count = qa_json_size(io->document, points);
        if ((subframes && (frame >= count || !frame)) || (!subframes && frame > count))
            return entity_error(io, points, "Original Q2 curve frame exceeds its stored positions");
        if (!count) return true;
        size_t to = subframes ? frame : frame ? frame - 1 : 0;
        size_t from = to ? to - 1 : 0;
        double previous, next;
        if (!qa_json_number(io->document, qa_json_at(io->document, points, from),
                &previous, io->error) ||
            !qa_json_number(io->document, qa_json_at(io->document, points, to),
                &next, io->error)) return false;
        if (!isfinite(previous) || !isfinite(next) || previous < 0 || next < previous ||
            next > FLT_MAX)
            return entity_error(io, points, "Original Q2 curve sample has an invalid distance");
        m->curve_from = subframes || to ? (float)previous : 0;
        m->curve_to = (float)next;
        return true;
    }
    qa_json_writer_key(io->writer, "moveinfo.curve_positions");
    qa_json_writer_array(io->writer);
    if (!q2_move_curve_samples(s, m, curve_emit, io, io->error)) return false;
    qa_json_writer_end(io->writer);
    if (!io->writer->failed) return true;
    if (io->error) *io->error = io->writer->failure;
    return false;
}

static const char *static_callback(qa_q2_game *g, q2_actor *a,
    const original_entity_class *type, unsigned field)
{
    q2_entity_state *s = a->entity;
    if (field == 0) {
        if (!s->usable) return NULL;
        if (s->kind == Q2E_MULTI && a->physics.solid == QA_PHYSICS_NOT_SOLID) return "trigger_enable";
        if (s->kind == Q2E_PLAT && !strcmp(type->name, "func_plat2") && !s->mover->activated)
            return "plat2_activate";
        if (s->kind == Q2E_DOOR && s->mover->angular && (s->spawnflags & 0x10000) &&
            !s->mover->activated) return "Door_Activate";
        if (s->kind == Q2E_TRAIN && s->visual.visible) return "train_use";
        if (s->kind == Q2E_SCENERY && s->scenery == Q2S_EXPLOSIVE && s->stage == 1)
            return "func_explosive_spawn";
        if (s->kind == Q2E_SCENERY && s->scenery == Q2S_EXPLOSIVE && s->stage == 2)
            return "func_explosive_activate";
        if (s->kind == Q2E_DYNAMIC_LIGHT && !strcmp(type->name, "light")) return "dynamic_light_use";
        return type->use;
    }
    if (field == 1) return s->touchable ? type->touch : NULL;
    if (field == 2) return type->blocked;
    if (field == 3) {
        qa_combat_state combat;
        return qa_combat_read(g->services.combat, a->id, &combat, NULL) &&
            combat.can_take_damage ? type->die : NULL;
    }
    return NULL;
}

static bool entity_callbacks(qa_q2_game *g, q2_original_record_io *io, q2_actor *a)
{
    q2_entity_state *s = a->entity;
    const original_entity_class *type = entity_class(g, s);
    static const char *const fields[] = {"use", "touch", "blocked", "die"};
    static const uint16_t offsets[] = {448, 444, 440, 456};
    const char *blocked_field = io->edition == QA_Q2_RERELEASE ? "moveinfo.blocked" : "blocked";
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i) {
        const char *field = i == 2 ? blocked_field : fields[i];
        if (!io->reading) {
            const char *name = type ? static_callback(g, a, type, (unsigned)i) : NULL;
            if (!type && ((i == 0 && s->usable) || (i == 1 && s->touchable)))
                return entity_error(io, offsets[i], "Q2 entity action has no original classname callback");
            if (!q2_original_function(g, io, field, offsets[i], name)) return false;
            continue;
        }
        bool empty;
        if (!q2_original_function_matches(g, io, field, offsets[i], NULL, &empty)) return false;
        if (i == 0) s->usable = !empty;
        if (i == 1) s->touchable = !empty;
        if (empty) continue;
        const char *normal = !type ? NULL : i == 0 ? type->use : i == 1 ? type->touch :
            i == 2 ? type->blocked : type->die;
        bool match = false;
        if (normal && !q2_original_function_matches(g, io, field, offsets[i], normal, &match)) return false;
        if (!match) {
            static const struct { const char *name; q2_entity_kind kind; unsigned field; int stage; } extra[] = {
                {"trigger_enable", Q2E_MULTI, 0, -1},
                {"train_use", Q2E_TRAIN, 0, -1},
                {"plat2_activate", Q2E_PLAT, 0, -1},
                {"Door_Activate", Q2E_DOOR, 0, -1},
                {"func_explosive_spawn", Q2E_SCENERY, 0, 1},
                {"func_explosive_activate", Q2E_SCENERY, 0, 2},
                {"dynamic_light_use", Q2E_DYNAMIC_LIGHT, 0, -1},
                {"Touch_Plat_Center2", Q2E_PLAT_TRIGGER, 1, 2},
                {"smart_water_blocked", Q2E_WATER, 2, -1},
                {"door_killed", Q2E_SECRET_DOOR, 3, -1},
                {"door_touch", Q2E_SECRET_DOOR, 1, -1}
            };
            for (size_t j = 0; j < sizeof(extra) / sizeof(*extra); ++j) {
                if (extra[j].kind != s->kind || extra[j].field != i) continue;
                if (!q2_original_function_matches(g, io, field, offsets[i], extra[j].name, &match)) return false;
                if (match) {
                    if (extra[j].stage >= 0) s->stage = extra[j].stage;
                    if (s->mover && (!strcmp(extra[j].name, "plat2_activate") ||
                        !strcmp(extra[j].name, "Door_Activate"))) s->mover->activated = false;
                    break;
                }
            }
        }
        if (!match) return entity_error(io, offsets[i], "Original Q2 entity action has no native continuation");
    }
    bool empty;
    if (!io->reading) {
        const char *name = s->kind == Q2E_SCENERY && s->scenery == Q2S_BOMB && s->stage ?
            "misc_viper_bomb_prethink" : NULL;
        return q2_original_function(g, io, "prethink", 432, name) &&
            q2_original_function(g, io, "pain", 452, NULL);
    }
    if (!q2_original_function_matches(g, io, "pain", 452, NULL, &empty)) return false;
    if (!empty) return entity_error(io, 452, "Original Q2 map entity pain has no native continuation");
    if (!q2_original_function_matches(g, io, "prethink", 432, NULL, &empty)) return false;
    if (empty) return true;
    bool match;
    if (!q2_original_function_matches(g, io, "prethink", 432, "misc_viper_bomb_prethink", &match)) return false;
    if (match && s->kind == Q2E_SCENERY && s->scenery == Q2S_BOMB) { s->stage = 1; return true; }
    return entity_error(io, 432, "Original Q2 entity prethink has no native continuation");
}

static bool entity_turret(qa_q2_game *g, q2_original_record_io *io, q2_entity_state *s)
{
    if (s->kind != Q2E_TURRET_BREACH && s->kind != Q2E_TURRET_DRIVER &&
        !(io->edition == QA_Q2_RERELEASE && s->kind == Q2E_TURRET_BASE)) return true;
    if (!s->turret) {
        s->turret = calloc(1, sizeof(*s->turret));
        if (!s->turret) {
            qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring original Q2 turret state");
            return false;
        }
    }
    q2_turret *t = s->turret;
    if (io->edition == QA_Q2_RERELEASE && s->kind != Q2E_TURRET_DRIVER &&
        !scalar(io, "dmg_radius", Q2_ORIGINAL_F32, 524, &t->rocket_scale)) return false;
    if (s->kind == Q2E_TURRET_BASE) return true;
    if (s->kind == Q2E_TURRET_DRIVER) {
        qa_vec3 offset = qa_v3(t->radius, t->yaw_offset, t->height);
        if (!scalar(io, "move_origin", Q2_ORIGINAL_VECTOR, 616, &offset)) return false;
        if (io->reading) {
            t->radius = offset.x; t->yaw_offset = offset.y; t->height = offset.z;
            bool linking, attached;
            if (!q2_original_function_matches(g, io, "think", 436, "turret_driver_link", &linking) ||
                !q2_original_function_matches(g, io, "think", 436, "turret_driver_think", &attached)) return false;
            s->think = linking ? Q2ET_TURRET_LINK : attached ? Q2ET_TURRET_DRIVER : Q2ET_NONE;
        }
        return true;
    }
    qa_vec3 minimum = qa_v3(t->pitch_max, t->yaw_min, 0);
    qa_vec3 maximum = qa_v3(t->pitch_min, t->yaw_max, 0);
    if (!scalar(io, "pos1", Q2_ORIGINAL_VECTOR, 352, &minimum) ||
        !scalar(io, "pos2", Q2_ORIGINAL_VECTOR, 364, &maximum) ||
        !scalar(io, "move_origin", Q2_ORIGINAL_VECTOR, 616, &t->muzzle) ||
        !scalar(io, "move_angles", Q2_ORIGINAL_VECTOR, 628, &t->goal)) return false;
    if (io->reading) {
        t->pitch_max = minimum.x; t->yaw_min = minimum.y;
        t->pitch_min = maximum.x; t->yaw_max = maximum.y;
    }
    return true;
}

static bool entity_effects(qa_q2_game *g, q2_original_record_io *io, q2_entity_state *s,
    const qa_q2_save_level *engine)
{
    if ((s->kind == Q2E_LASER || (s->kind == Q2E_SCENERY && s->scenery == Q2S_MAL_LASER)) &&
        !scalar(io, "s.old_origin", Q2_ORIGINAL_VECTOR, 28, &s->beam_end)) return false;
    if (s->kind == Q2E_FORCEWALL) {
        if (!scalar(io, "pos1", Q2_ORIGINAL_VECTOR, 352, &s->direction) ||
            !scalar(io, "pos2", Q2_ORIGINAL_VECTOR, 364, &s->beam_end) ||
            !q2_original_scalar(io, "offset", Q2_ORIGINAL_VECTOR,
                UINT16_MAX, UINT16_MAX, 996, &s->multicast_origin)) return false;
    }
    bool speaker = s->kind == Q2E_SPEAKER;
    if (s->kind == Q2E_ROTATING &&
        !q2_original_resource(g, io, engine, "moveinfo.sound_middle", 704, 704, 704, 288,
            &s->noise)) return false;
    if (speaker || s->kind == Q2E_SOUND_FX || s->kind == Q2E_EARTHQUAKE ||
        (s->kind == Q2E_SCENERY && s->scenery == Q2S_EXPLOSIVE))
        if (!q2_original_resource(g, io, engine, "noise_index", 576, 576, 576, 288, &s->noise))
            return false;
    if (speaker) {
        qa_string_id loop = s->active ? s->noise : 0;
        if (!q2_original_resource(g, io, engine, "s.sound", 76, 76, 76, 288, &loop)) return false;
        if (io->reading) s->active = loop != 0;
    }
    if (s->kind == Q2E_CHANGELEVEL && io->reading) s->active = false;
    if (s->kind == Q2E_SCENERY && s->scenery == Q2S_BARREL &&
        (!scalar(io, "air_finished", Q2_ORIGINAL_TIME, 404, &s->air_ns) ||
         !scalar(io, "pain_debounce_time", Q2_ORIGINAL_TIME, 464, &s->pain_ns) ||
         !scalar(io, "damage_debounce_time", Q2_ORIGINAL_TIME, 472, &s->environment_ns))) return false;
    if (s->kind == Q2E_DYNAMIC_LIGHT) {
        int32_t enabled = s->active ? 1 : 0;
        if (!scalar(io, "health", Q2_ORIGINAL_I32, 480, &enabled)) return false;
        if (io->reading) s->active = enabled != 0;
    }
    return true;
}

static bool entity_q64(q2_original_record_io *io, q2_actor *a)
{
    q2_entity_state *s = a->entity;
    if (s->kind != Q2E_EYE && s->kind != Q2E_CAMERA && s->kind != Q2E_CAMERA_DUMMY) return true;
    if (io->edition != QA_Q2_RERELEASE)
        return entity_error(io, 0, "Q2 camera and eye continuation requires its original rerelease fields");
    if (!s->q64) {
        s->q64 = calloc(1, sizeof(*s->q64));
        if (!s->q64) {
            qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring original Q2 camera state");
            return false;
        }
    }
    q2_q64 *v = s->q64;
    if (s->kind == Q2E_EYE) {
        if (io->reading) v->vision_cone = a->physics.yaw_speed;
        return scalar(io, "move_origin", Q2_ORIGINAL_VECTOR, 616, &v->eye_position) &&
            scalar(io, "move_angles", Q2_ORIGINAL_VECTOR, 628, &v->neutral) &&
            scalar(io, "dmg_radius", Q2_ORIGINAL_F32, 528, &s->random);
    }
    uint32_t hackflags = v->hackflags;
    if (s->kind == Q2E_CAMERA_DUMMY && !io->reading) {
        if (v->fading) hackflags |= 2;
        else hackflags &= ~2u;
    }
    if (!q2_original_scalar(io, "hackflags", Q2_ORIGINAL_U32,
        UINT16_MAX, UINT16_MAX, UINT16_MAX, &hackflags)) return false;
    if (io->reading) v->hackflags = hackflags;
    if (s->kind == Q2E_CAMERA)
        return scalar(io, "moveinfo.remaining_distance", Q2_ORIGINAL_F32, 760, &v->remaining) &&
            scalar(io, "moveinfo.distance", Q2_ORIGINAL_F32, 724, &v->distance) &&
            scalar(io, "moveinfo.move_speed", Q2_ORIGINAL_F32, 752, &v->speed);
    if (io->reading) v->fading = (v->hackflags & 2) != 0;
    return q2_original_scalar(io, "timestamp", Q2_ORIGINAL_SECONDS_TIME,
            288, 288, 288, &v->fade_remaining) &&
        q2_original_scalar(io, "pain_debounce_time", Q2_ORIGINAL_SECONDS_TIME,
            464, 464, 464, &v->fade_duration);
}

bool q2_original_entity_record(qa_q2_game *g, q2_original_record_io *io, q2_actor *a,
    const qa_q2_save_level *engine, qa_error *error)
{
    if (!g || !io || !a) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Original Q2 entity needs its actual GAME owner");
        return false;
    }
    if (a->client || a->item || a->projectile.kind != Q2_PROJECTILE_NONE) return true;
    bool driver = a->monster && a->monster->definition->species == Q2M_TURRET_DRIVER;
    if (a->monster && !driver) {
        if (io->reading && !io->references_only &&
            (!read_entity(g, io, a) || !entity_strings(g, io, a->entity))) return false;
        return !a->entity || entity_team_references(g, io, a->entity);
    }
    if (io->reading && !io->references_only && !read_entity(g, io, a)) return false;
    q2_entity_state *s = a->entity;
    if (!s) return true;
    if (io->references_only) return entity_references(g, io, s);
    if (!entity_strings(g, io, s) || !q2_original_record(io, Q2_ORIGINAL_ENTITY, s)) return false;
    if (s->kind == Q2E_DOOR || s->kind == Q2E_BUTTON) {
        int32_t maximum = io->reading ? 0 : qa_source_float_to_i32(s->health);
        if (!scalar(io, "max_health", Q2_ORIGINAL_I32, 484, &maximum)) return false;
        if (io->reading) s->health = (float)maximum;
    }
    if (s->kind == Q2E_SCENERY && s->scenery == Q2S_CLOCK && io->reading &&
        !scalar(io, "health", Q2_ORIGINAL_I32, 480, &s->clock_value)) return false;
    if (io->reading && original_mover_kind(s->kind) && !q2_mover_state(a, error)) return false;
    int32_t damage = io->reading ? 0 : qa_source_float_to_i32(s->damage);
    uint64_t timestamp = s->kind == Q2E_EARTHQUAKE || s->kind == Q2E_PUSH ? s->expires_ns : s->timestamp_ns;
    uint64_t touch_time = s->kind == Q2E_DOOR || s->kind == Q2E_BUTTON || s->kind == Q2E_DOOR_TRIGGER ||
        s->kind == Q2E_SECRET_DOOR ? s->timestamp_ns : s->debounce_ns;
    if (!scalar(io, "dmg", Q2_ORIGINAL_I32, 516, &damage) ||
        (s->kind != Q2E_CAMERA_DUMMY && !scalar(io, "timestamp", Q2_ORIGINAL_TIME, 288, &timestamp)) ||
        !scalar(io, "touch_debounce_time", Q2_ORIGINAL_TIME, 460, &touch_time) ||
        !scalar(io, "last_move_time", Q2_ORIGINAL_TIME, 476, &s->sound_ns) ||
        (!driver && !entity_think(g, io, s)) || !entity_motion(g, io, a) || !entity_curve(g, io, s) ||
        !entity_turret(g, io, s) ||
        !entity_effects(g, io, s, engine) || !entity_q64(io, a) ||
        (!driver && !entity_callbacks(g, io, a)) || !entity_references(g, io, s)) return false;
    if (io->reading) {
        s->damage = (float)damage;
        if (s->kind == Q2E_EARTHQUAKE || s->kind == Q2E_PUSH) s->expires_ns = timestamp;
        else s->timestamp_ns = timestamp;
        if (s->kind == Q2E_DOOR || s->kind == Q2E_BUTTON || s->kind == Q2E_DOOR_TRIGGER ||
            s->kind == Q2E_SECRET_DOOR) s->timestamp_ns = touch_time;
        else s->debounce_ns = touch_time;
        if (s->kind == Q2E_SCENERY) {
            const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
            if (s->scenery == Q2S_BLACKHOLE) { s->animation_first = 0; s->animation_end = 19; }
            else if (s->scenery == Q2S_ANIMATION) {
                s->animation_first = !strcmp(name, "misc_eastertank") ? 254 :
                    !strcmp(name, "misc_easterchick") ? 208 : !strcmp(name, "misc_easterchick2") ? 248 : 0;
                s->animation_end = !strcmp(name, "misc_eastertank") ? 293 :
                    !strcmp(name, "misc_easterchick") ? 247 : !strcmp(name, "misc_easterchick2") ? 287 : 7;
            }
        }
    }
    return true;
}
