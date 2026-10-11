#include "internal.h"
#include "qa/game_q1_checkpoint.h"
#include "qa/game_q1_source_entities.h"
#include "../wire_internal.h"
#include <errno.h>
#include <limits.h>

bool q1_map_fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
bool q1_map_present_intermission(qa_q1_game *g, qa_actor_id source, uint32_t stage,
                                  const qa_q1_intermission_result *result, qa_error *error) {
    if (result->kind == QA_Q1_INTERMISSION_SELL) {
        qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT, .family = QA_GAME_Q1,
                                  .provider = g->options.provider, .time_ns = g->time_ns};
        return qa_builtin_resource(&g->services, "sell-screen", &event.resource, error) &&
               qa_builtin_emit(&g->services, &event, error);
    }
    if (result->kind != QA_Q1_INTERMISSION_FINALE)
        return true;
    qa_builtin_event music = {.kind = QA_BUILTIN_EFFECT, .family = QA_GAME_Q1,
                              .provider = g->options.provider, .time_ns = g->time_ns,
                              .code = result->track, .count = 3};
    if (!qa_builtin_resource(&g->services, "music", &music.resource, error) ||
        !qa_builtin_emit(&g->services, &music, error))
        return false;
    if (!q1_alive(g, source))
        return true;
    const char *text = qa_strings_cstr(qa_session_strings(g->services.session), result->text);
    return q1_map_finale_emit(g, stage, text, error);
}
static bool map_options_valid(const qa_q1_game *g, const qa_q1_map_options *options,
                              qa_error *error) {
    if (!g || g->destroy_pending || !options || !options->targets || !options->level || !options->server_flags ||
        !options->static_model || !options->ambient || !options->lightstyle ||
        !options->set_skill || !options->secret_found || !g->services.actor_traits ||
        !g->services.physics || !g->services.physics->services.read ||
        !g->services.physics->services.write || !g->services.motion_changed ||
        (!!options->path_read != !!options->path_change))
        return q1_map_fail(error, "invalid Q1 map service binding");
    return true;
}
bool qa_q1_game_maps_bind(qa_q1_game *g, const qa_q1_map_options *options, qa_error *error) {
    if (!map_options_valid(g, options, error))
        return false;
    if (g->maps)
        return q1_map_fail(error, "Q1 map services already bound");
    g->maps = calloc(1, sizeof(*g->maps));
    if (!g->maps) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 map runtime");
        return false;
    }
    g->maps->options = *options;
    g->maps->lightning_end = -1;
    return true;
}
static void door_groups_free(q1_door_group *group) {
    while (group) {
        q1_door_group *next = group->next;
        free(group->members);
        free(group);
        group = next;
    }
}
bool qa_q1_game_rogue_runes_claim(qa_q1_game *g, bool *newly_claimed, qa_error *error) {
    if (!newly_claimed) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Rogue rune claim requires an output");
        return false;
    }
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    *newly_claimed = false;
    qa_actor_id world = g->maps && g->maps->world_actor.registry
        ? g->maps->world_actor
        : g->services.physics ? g->services.physics->world_actor : (qa_actor_id){0};
    if (g->options.program == QA_Q1_ROGUE && g->options.deathmatch != 0 &&
        (g->options.gamecfg & 1u) && q1_alive(g, world)) {
        if (!q1_ref_equal(g->rogue_runes_world, q1_ref_from(g, world))) {
            g->rogue_runes_world = q1_ref_from(g, world);
            g->rogue_runes_started = false;
        }
        if (!g->rogue_runes_started) {
            g->rogue_runes_started = true;
            *newly_claimed = true;
        }
    }
    qa_q1_game_operation_end(&operation);
    return true;
}
bool qa_q1_game_begin_map(qa_q1_game *g, const qa_q1_map_options *options, qa_error *error) {
    if (!map_options_valid(g, options, error))
        return false;
    if (!g->maps || g->observation_depth || !qa_session_safe(g->services.session) ||
        qa_actors_count(qa_session_actors(g->services.session)))
        return q1_map_fail(error, "Q1 map reset requires a retired world at a safe point");
    if (!qa_strings_text(qa_session_strings(g->services.session), options->current_map).size)
        return q1_map_fail(error, "Q1 map reset requires an interned map identity");
    for (qa_builtin_snapshot_frame *snapshot = g->snapshots; snapshot; snapshot = snapshot->next)
        if (snapshot->active)
            return q1_map_fail(error, "Q1 map reset has an active actor query");
    for (uint32_t slot = 0; slot < g->capacity; ++slot)
        if (g->actors[slot] || g->players[slot])
            return q1_map_fail(error, "Q1 map reset requires completed actor release callbacks");

    q1_wire_map_reset(g);
    if (g->host.source_info_map_reset) g->host.source_info_map_reset(g->host.context);
    qa_q1_map_options replacement = *options;
    q1_map_state *states = g->maps->allocated;
    q1_rotate_target *rotated = g->maps->rotated_targets;
    qa_actor_id *frame_ticks = g->maps->frame_ticks;
    q1_addon_contact *contacts = g->maps->addon_contacts;
    if (contacts)
        memset(contacts, 0, g->capacity * sizeof(*contacts));
    if (rotated)
        memset(rotated, 0, g->capacity * sizeof(*rotated));
    door_groups_free(g->maps->door_groups);
    *g->maps = (q1_map_runtime){.options = replacement, .allocated = states,
                               .rotated_targets = rotated, .frame_ticks = frame_ticks,
                               .addon_contacts = contacts,
                               .lightning_end = -1};
    while (states) {
        q1_map_state *next = states->allocated_next;
        *states = (q1_map_state){.allocated_next = next, .pool_next = g->maps->spare};
        g->maps->spare = states;
        states = next;
    }
    g->spare_actors = g->retired_actors = NULL;
    for (q1_actor *actor = g->allocated_actors; actor; actor = actor->allocation_next) {
        q1_actor *next = actor->allocation_next;
        *actor = (q1_actor){.allocation_next = next, .pool_next = g->spare_actors};
        g->spare_actors = actor;
    }
    g->spare_players = g->retired_players = NULL;
    for (q1_player *player = g->allocated_players; player; player = player->allocation_next) {
        q1_player *next = player->allocation_next;
        *player = (q1_player){.allocation_next = next, .pool_next = g->spare_players};
        g->spare_players = player;
    }
    for (qa_builtin_snapshot_frame *snapshot = g->snapshots; snapshot; snapshot = snapshot->next)
        snapshot->snapshot.count = 0;
    g->total_monsters = g->killed_monsters = g->hellknight_melee = 0;
    g->authored_gremlins = g->spawned_gremlins = 0;
    g->source_captures[0] = g->source_captures[1] = 0;
    g->qw_rj = g->options.quakeworld ? 1 : 0;
    g->sight_actor = g->horn_charmer = (q1_ref){0};
    g->rogue_runes_world = g->body_queue_head = (q1_ref){0};
    g->rogue_runes_started = false;
    g->time_ns = Q1_SOURCE_INITIAL_TIME_NS;
    g->time = (double)g->time_ns / 1000000000.0;
    g->elapsed = g->sight_time = 0;
    qa_q1_game_finale_reset(g);
    g->forward = g->right = g->up = qa_v3(0, 0, 0);
    g->run_straight = g->enemy_visible = false;
    g->rune_knight_melee = g->enemy_range = 0;
    g->options.world_type = 0;
    return true;
}
static bool target_read(void *context, qa_actor_id actor, qa_authored_target *out) {
    return qa_q1_game_authored_target(context, actor, out);
}
static bool target_use(void *context, qa_actor_id actor, qa_actor_id other, qa_actor_id activator,
                       qa_error *error) {
    return qa_q1_game_use_from(context, actor, other, activator, error);
}
static bool target_set_target(void *context, qa_actor_id actor, qa_string_id value,
                              qa_error *error) {
    q1_actor *entity = q1_entity(context, actor);
    if (!entity || !entity->native)
        return q1_map_fail(error, "Q1 target field owner is unavailable");
    entity->target = value;
    return true;
}
static bool target_set_targetname(void *context, qa_actor_id actor, qa_string_id value,
                                  qa_error *error) {
    qa_q1_game *game = context;
    q1_actor *entity = q1_entity(game, actor);
    if (!entity || !entity->native)
        return q1_map_fail(error, "Q1 targetname field owner is unavailable");
    entity->targetname = value;
    qa_targets_changed(game->maps->options.targets, actor);
    return true;
}
static bool target_set_delay(void *context, qa_actor_id actor, float value, qa_error *error) {
    q1_actor *entity = q1_entity(context, actor);
    if (!entity || !entity->native || !isfinite(value))
        return q1_map_fail(error, "Q1 delay field owner is unavailable");
    entity->delay = value;
    return true;
}
static bool target_field(void *context, qa_actor_id actor, qa_string_id key, qa_target_field *out) {
    qa_q1_game *g = context;
    q1_actor *entity = q1_entity(g, actor);
    if (!entity)
        return false;
    if (entity->kind == Q1_ROGUE_TEAM_STATE) {
        static const qa_target_key names[QA_Q1_ROGUE_FIELDS] = {
            QA_TARGET_KEY_STEAM, QA_TARGET_KEY_CTF_FLAGS, QA_TARGET_KEY_CTF_KILLED, QA_TARGET_KEY_SUICIDE_COUNT, QA_TARGET_KEY_CTF_LASTHURTCARRIER,
            QA_TARGET_KEY_CTF_LASTFRAGGEDCARRIER, QA_TARGET_KEY_CTF_LASTRETURNEDFLAG, QA_TARGET_KEY_CTF_FLAGSINCE, QA_TARGET_KEY_FLY_SOUND};
        for (size_t i = 0; i < QA_Q1_ROGUE_FIELDS; ++i)
            if ((key == g->field_keys[names[i]])) {
                *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                    .value.number = entity->state.rogue_fields[i]};
                return true;
            }
    }
    if (g->maps && qa_actor_id_equal(actor, g->maps->world_actor) &&
        (key == g->field_keys[QA_TARGET_KEY_ROGUE_NEXTTEAMUPDTIME])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
            .value.number = entity->rogue_next_update};
        return true;
    }
    if (g->maps && g->options.program == QA_Q1_CTF &&
        qa_actor_id_equal(actor, g->maps->world_actor) &&
        ((key == g->field_keys[QA_TARGET_KEY_CTF_LASTCAPTURE]) || (key == g->field_keys[QA_TARGET_KEY_CTF_LASTCAPTURETEAM]))) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
            .value.number = (key == g->field_keys[QA_TARGET_KEY_CTF_LASTCAPTURE]) ?
                entity->ctf_last_capture : entity->ctf_last_capture_team};
        return true;
    }
    if (entity->kind == Q1_SOURCE_CTF_FLAG) {
        if ((key == g->field_keys[QA_TARGET_KEY_CTF_RETURN])) {
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                .value.number = entity->state.source_flag.return_time};
            return true;
        }
        if ((key == g->field_keys[QA_TARGET_KEY_CTF_BASE])) {
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_VECTOR,
                .value.vector = entity->state.source_flag.base};
            return true;
        }
        if ((key == g->field_keys[QA_TARGET_KEY_MANGLE])) {
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_VECTOR,
                .value.vector = entity->state.source_flag.angles};
            return true;
        }
    }
    if (entity->kind == Q1_SOURCE_ROGUE_TAG &&
        ((key == g->field_keys[QA_TARGET_KEY_TAG_FRAGS]) || (key == g->field_keys[QA_TARGET_KEY_TAG_MESSAGE_TIME]))) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
            .value.number = (key == g->field_keys[QA_TARGET_KEY_TAG_FRAGS]) ? entity->state.source_tag.frags :
                entity->state.source_tag.message_time};
        return true;
    }
    if (entity->kind == Q1_SOURCE_ROGUE_RUNE && (key == g->field_keys[QA_TARGET_KEY_RUNE])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER, .value.number = entity->state.rogue_rune};
        return true;
    }
    if (g->maps && qa_actor_id_equal(actor, g->maps->world_actor) && (key == g->field_keys[QA_TARGET_KEY_ROGUE_RUNES_SPAWNED])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER, .value.number = entity->rogue_runes_spawned};
        return true;
    }
    if (entity->kind == Q1_SOURCE_ROGUE_FLAG || entity->kind == Q1_SOURCE_ROGUE_FLAG_BASE) {
        const qa_target_key names[] = {QA_TARGET_KEY_TEAM, QA_TARGET_KEY_CNT, QA_TARGET_KEY_SUPER_TIME};
        for (size_t i = 0; i < 3; ++i) if ((key == g->field_keys[names[i]])) {
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                .value.number = entity->state.rogue_flag.values[i]};
            return true;
        }
        if ((key == g->field_keys[QA_TARGET_KEY_OLDORIGIN]) || (key == g->field_keys[QA_TARGET_KEY_MANGLE])) {
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_VECTOR,
                .value.vector = (key == g->field_keys[QA_TARGET_KEY_OLDORIGIN]) ? entity->state.rogue_flag.origin :
                    entity->state.rogue_flag.angles};
            return true;
        }
    }
    if (entity->kind == Q1_SOURCE_CTF_RUNE && (key == g->field_keys[QA_TARGET_KEY_CTF_RUNE])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_TEXT,
            .value.text = entity->state.source_rune.rune};
        return true;
    }
    if (g->maps && g->options.program == QA_Q1_CTF &&
        qa_actor_id_equal(actor, g->maps->world_actor) && (key == g->field_keys[QA_TARGET_KEY_CTF_RUNESSPAWNED])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
            .value.number = entity->ctf_runes_spawned};
        return true;
    }
    if (entity->map && q1_map_is_fog(entity->map->kind)) {
        if ((key == g->field_keys[QA_TARGET_KEY_FOG_DENSITY])) {
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                                     .value.number = entity->map->fog_density};
            return true;
        }
        if ((key == g->field_keys[QA_TARGET_KEY_FOG_COLOR])) {
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_VECTOR,
                                     .value.vector = entity->map->fog_color};
            return true;
        }
        if ((key == g->field_keys[QA_TARGET_KEY_FOG_INFO_ENTITY])) {
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_TEXT,
                                     .value.text = entity->map->fog_info_entity};
            return true;
        }
    }
    if ((key == g->field_keys[QA_TARGET_KEY_IS_FROZEN]) || (key == g->field_keys[QA_TARGET_KEY_ADDON_FROZENDAMAGEABLE]) ||
        (key == g->field_keys[QA_TARGET_KEY_STOREDNEXTTHINK]) || (key == g->field_keys[QA_TARGET_KEY_ALPHA])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
            .value.number = (key == g->field_keys[QA_TARGET_KEY_IS_FROZEN]) ? entity->frozen.active
                            : (key == g->field_keys[QA_TARGET_KEY_ADDON_FROZENDAMAGEABLE]) ? entity->frozen.damageable
                            : (key == g->field_keys[QA_TARGET_KEY_ALPHA]) ? entity->alpha
                            : entity->map && entity->map->kind == Q1_MAP_ADDON_SHAKE
                                ? entity->map->active_until : entity->frozen.next_think};
        return true;
    }
    static const struct {
        qa_target_key name;
        size_t offset;
    } strings[] = {{QA_TARGET_KEY_CLASSNAME, offsetof(q1_actor, classname)},
                   {QA_TARGET_KEY_TARGETNAME, offsetof(q1_actor, targetname)},
                   {QA_TARGET_KEY_TARGET, offsetof(q1_actor, target)},
                   {QA_TARGET_KEY_KILLTARGET, offsetof(q1_actor, killtarget)},
                   {QA_TARGET_KEY_MESSAGE, offsetof(q1_actor, message)}},
      numbers[] = {{QA_TARGET_KEY_SPEED, offsetof(q1_actor, speed)},
                   {QA_TARGET_KEY_DMG, offsetof(q1_actor, damage)},
                   {QA_TARGET_KEY_DAMAGE, offsetof(q1_actor, damage)},
                   {QA_TARGET_KEY_WAIT, offsetof(q1_actor, wait)},
                   {QA_TARGET_KEY_DELAY, offsetof(q1_actor, delay)}};
    for (size_t i = 0; i < sizeof(strings) / sizeof(*strings); ++i)
        if ((key == g->field_keys[strings[i].name])) {
            const qa_string_id *value = (const void *)((const char *)entity + strings[i].offset);
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_TEXT, .value.text = *value};
            return true;
        }
    for (size_t i = 0; i < sizeof(numbers) / sizeof(*numbers); ++i)
        if ((key == g->field_keys[numbers[i].name])) {
            const float *value = (const void *)((const char *)entity + numbers[i].offset);
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER, .value.number = *value};
            return true;
        }
    if ((key == g->field_keys[QA_TARGET_KEY_SPAWNFLAGS])) {
        *out =
            (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER, .value.number = entity->spawnflags};
        return true;
    }
    if (entity->map && (key == g->field_keys[QA_TARGET_KEY_STYLE])) {
        *out =
            (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER, .value.number = entity->map->style};
        return true;
    }
    if (entity->map && (key == g->field_keys[QA_TARGET_KEY_CATEGORY])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_TEXT,
                                 .value.text = entity->map->category};
        return true;
    }
    if (entity->map && (key == g->field_keys[QA_TARGET_KEY_GOAL_STATE])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                                 .value.number = entity->map->goal_state};
        return true;
    }
    if (entity->map && (key == g->field_keys[QA_TARGET_KEY_STATE])) {
        bool moving = q1_map_is_mover(entity->map->kind) || q1_map_is_rogue_plat(entity->map->kind);
        if (!moving) {
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                                     .value.number = entity->map->field_state};
            return true;
        }
        static const q1_runtime_name states[] = {Q1_NAME_BOTTOM, Q1_NAME_UP, Q1_NAME_TOP, Q1_NAME_DOWN};
        qa_string_id value = g->runtime_names[states[entity->map->pending.mover.position]];
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_TEXT, .value.text = value};
        return true;
    }
    if (entity->map && entity->map->kind == Q1_MAP_COOP_POINT && (key == g->field_keys[QA_TARGET_KEY_ITEMS])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                                 .value.number = entity->map->coop_weapons};
        return true;
    }
    if (entity->map &&
        ((key == g->field_keys[QA_TARGET_KEY_CURRENTAMMO]) || (key == g->field_keys[QA_TARGET_KEY_WEAPON]) || (key == g->field_keys[QA_TARGET_KEY_FRAGS]))) {
        *out =
            (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                              .value.number = (key == g->field_keys[QA_TARGET_KEY_WEAPON])  ? entity->map->weapon
                                              : (key == g->field_keys[QA_TARGET_KEY_FRAGS]) ? entity->map->frags
                                                                      : entity->map->current_ammo};
        return true;
    }
    if (entity->map && entity->map->kind == Q1_MAP_ENDING_ACTOR) {
        if ((key == g->field_keys[QA_TARGET_KEY_AMMO_ROCKETS1]) || (key == g->field_keys[QA_TARGET_KEY_PAUSETIME])) {
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                                     .value.number = (key == g->field_keys[QA_TARGET_KEY_AMMO_ROCKETS1])
                                                         ? entity->map->pending.follower.rockets
                                                         : entity->map->pause_time};
            return true;
        }
        if ((key == g->field_keys[QA_TARGET_KEY_V_ANGLE])) {
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_VECTOR,
                                     .value.vector = entity->map->pending.follower.view_angles};
            return true;
        }
    }
    if (entity->map && entity->map->kind == Q1_MAP_TIME_MACHINE && (key == g->field_keys[QA_TARGET_KEY_PAIN_FINISHED])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                                 .value.number = entity->map->cooldown};
        return true;
    }
    if (entity->map && q1_map_is_rogue_hazard(entity->map->kind)) {
        const q1_map_state *state = entity->map;
        bool cooldown = (state->kind == Q1_MAP_BUZZSAW && (key == g->field_keys[QA_TARGET_KEY_PAIN_FINISHED])) ||
                        (state->kind == Q1_MAP_LTRAIL_START && (key == g->field_keys[QA_TARGET_KEY_LTRAILLASTUSED]));
        bool active = (state->kind >= Q1_MAP_LTRAIL_START && (key == g->field_keys[QA_TARGET_KEY_ITEMS])) ||
                      (state->kind <= Q1_MAP_BUZZSAW && (key == g->field_keys[QA_TARGET_KEY_ATTACK_FINISHED]));
        if (cooldown || active || (state->kind == Q1_MAP_BUZZSAW && (key == g->field_keys[QA_TARGET_KEY_PAUSETIME]))) {
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                                     .value.number = cooldown ? state->cooldown
                                                     : active ? state->active_until
                                                              : state->pause_time};
            return true;
        }
    }
    if (entity->map && ((key == g->field_keys[QA_TARGET_KEY_HEIGHT]) ||
                        ((entity->map->kind == Q1_MAP_ROGUE_PLAT ||
                          entity->map->kind == Q1_MAP_ELECTRODE_TARGET ||
                          entity->map->electrode_button) && (key == g->field_keys[QA_TARGET_KEY_CNT])))) {
        *out =
            (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                              .value.number = (key == g->field_keys[QA_TARGET_KEY_HEIGHT]) ? entity->map->height
                                                                     : entity->map->counter_value};
        return true;
    }
    if (g->maps && qa_actor_id_equal(actor, g->maps->world_actor) &&
        (key == g->field_keys[QA_TARGET_KEY_ROGUE_IMPACTVELOCITY])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                                 .value.number = g->maps->pendulum_impact};
        return true;
    }
    if (g->maps && qa_actor_id_equal(actor, g->maps->world_actor) &&
        (key == g->field_keys[QA_TARGET_KEY_ROGUE_ELVBUTNDIR])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                                 .value.number = g->maps->elevator_direction};
        return true;
    }
    if (g->maps && qa_actor_id_equal(actor, g->maps->world_actor) &&
        ((key == g->field_keys[QA_TARGET_KEY_ROGUE_EARTHQUAKE_ACTIVE]) || (key == g->field_keys[QA_TARGET_KEY_ROGUE_EARTHQUAKE_INTENSITY]))) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                                 .value.number = (key == g->field_keys[QA_TARGET_KEY_ROGUE_EARTHQUAKE_ACTIVE])
                                                     ? g->maps->rogue_quake_active
                                                     : g->maps->rogue_quake_intensity};
        return true;
    }
    if (g->maps && qa_actor_id_equal(actor, g->maps->world_actor) &&
        ((key == g->field_keys[QA_TARGET_KEY_ROGUE_CUTSCENE_RUNNING]) || (key == g->field_keys[QA_TARGET_KEY_ROGUE_ENDING_STARTED]) ||
         (key == g->field_keys[QA_TARGET_KEY_ROGUE_ACTORSTAGE]))) {
        *out = (qa_target_field){
            .kind = QA_TARGET_FIELD_NUMBER,
            .value.number = (key == g->field_keys[QA_TARGET_KEY_ROGUE_ACTORSTAGE])       ? g->maps->rogue_actor_stage
                            : (key == g->field_keys[QA_TARGET_KEY_ROGUE_ENDING_STARTED]) ? g->maps->rogue_ending_started
                                                                   : g->maps->rogue_cutscene};
        return true;
    }
    if (entity->map &&
        ((entity->map->kind == Q1_MAP_HIP_COUNTER && (key == g->field_keys[QA_TARGET_KEY_COUNTER_STATE])) ||
         (key == g->field_keys[QA_TARGET_KEY_GRAVITY]))) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                                 .value.number = (key == g->field_keys[QA_TARGET_KEY_COUNTER_STATE])
                                                     ? entity->map->counter_value
                                                     : entity->map->gravity};
        return true;
    }
    if (entity->map && (key == g->field_keys[QA_TARGET_KEY_EVENT])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_TEXT, .value.text = entity->map->event};
        return true;
    }
    if (entity->map && (key == g->field_keys[QA_TARGET_KEY_MDL])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_TEXT, .value.text = entity->map->mdl};
        return true;
    }
    if (entity->map && ((key == g->field_keys[QA_TARGET_KEY_GROUP]) || (key == g->field_keys[QA_TARGET_KEY_PATH]) ||
                        (key == g->field_keys[QA_TARGET_KEY_NOISE]) || (key == g->field_keys[QA_TARGET_KEY_NOISE1]))) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_TEXT,
                                 .value.text = (key == g->field_keys[QA_TARGET_KEY_GROUP]) ? entity->map->group
                                               : (key == g->field_keys[QA_TARGET_KEY_PATH]) ? entity->map->path
                                               : entity->map->noise[(key == g->field_keys[QA_TARGET_KEY_NOISE1])]};
        return true;
    }
    if (entity->map && (key == g->field_keys[QA_TARGET_KEY_ROTATE])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_VECTOR,
                                 .value.vector = entity->map->rotate};
        return true;
    }
    if (entity->map && ((key == g->field_keys[QA_TARGET_KEY_SPAWNFUNCTION]) || (key == g->field_keys[QA_TARGET_KEY_SPAWNCLASSNAME]))) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_TEXT,
                                 .value.text = (key == g->field_keys[QA_TARGET_KEY_SPAWNFUNCTION])
                                                   ? entity->map->spawn_function
                                                   : entity->map->spawn_classname};
        return true;
    }
    if (entity->map && ((key == g->field_keys[QA_TARGET_KEY_SPAWNMULTI]) || (key == g->field_keys[QA_TARGET_KEY_SPAWNSILENT]))) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                                 .value.number = (key == g->field_keys[QA_TARGET_KEY_SPAWNMULTI])
                                                     ? entity->map->spawn_multi
                                                     : entity->map->spawn_silent};
        return true;
    }
    if (entity->map && (key == g->field_keys[QA_TARGET_KEY_MANGLE])) {
        *out =
            (qa_target_field){.kind = QA_TARGET_FIELD_VECTOR, .value.vector = entity->map->mangle};
        return true;
    }
    if (entity->map && entity->map->has_view_offset && (key == g->field_keys[QA_TARGET_KEY_VIEW_OFS])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_VECTOR,
                                 .value.vector = entity->map->view_offset};
        return true;
    }
    if (entity->kind == Q1_MONSTER && (key == g->field_keys[QA_TARGET_KEY_WETSUIT_TIME])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                                 .value.number = entity->state.monster.follow_until};
        return true;
    }
    return false;
}
bool qa_q1_game_target_binding(qa_q1_game *g, qa_actor_id actor, qa_target_binding *out,
                                qa_error *error) {
    if (!g || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q1 target binding request");
        return false;
    }
    q1_actor *entity = q1_entity(g, actor);
    if (!g->maps || !entity || !entity->native ||
        (g->continuation_pending && !entity->restored_target))
        return false;
    *out = (qa_target_binding){.actor = entity->id,
                                 .context = g,
                                 .source = g->options.quakeworld ? QA_RULESET_QUAKEWORLD
                                                                 : QA_RULESET_NETQUAKE,
                                 .read = target_read,
                                 .use = target_use,
                                 .field = target_field,
                                 .set_target = target_set_target,
                                 .set_delay = target_set_delay,
                                 .set_targetname = target_set_targetname};
    return true;
}
bool q1_map_bind_target(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (!g->maps || !entity->native)
        return true;
    qa_target_binding binding;
    if (!qa_q1_game_target_binding(g, entity->id, &binding, error))
        return false;
    return qa_targets_bind(g->maps->options.targets, &binding, error);
}
qa_string_id qa_q1_game_map_name(const qa_q1_game *g) {
    return g && g->maps ? g->maps->options.current_map : QA_STRING_NONE;
}
uint32_t qa_q1_game_campaign_flags(const qa_q1_game *g) {
    return g && g->maps ? *g->maps->options.server_flags : 0;
}
q1_map_state *q1_map_allocate(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (entity->map)
        return entity->map;
    if (!g->maps) {
        q1_map_fail(error, "Q1 authored map services are not bound");
        return NULL;
    }
    q1_map_state *state = g->maps->spare;
    if (state) {
        g->maps->spare = state->pool_next;
        q1_map_state *allocated = state->allocated_next;
        *state = (q1_map_state){.allocated_next = allocated};
    } else {
        state = calloc(1, sizeof(*state));
        if (!state) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 map actor");
            return NULL;
        }
        state->allocated_next = g->maps->allocated;
        g->maps->allocated = state;
    }
    entity->map = state;
    state->netname = entity->source_netname;
    entity->source_netname = 0;
    return state;
}
void q1_map_actor_released(qa_q1_game *g, q1_actor *entity) {
    q1_map_frame_tick_remove(g, entity->id);
    if (g->maps && entity->native)
        qa_targets_unbind_context(g->maps->options.targets, entity->id, g);
    if (!entity->map)
        return;
    entity->map->pool_next = g->maps->retired;
    g->maps->retired = entity->map;
    entity->map = NULL;
}
void q1_map_frame_begin(qa_q1_game *g) {
    if (!g->maps)
        return;
    while (g->maps->retired) {
        q1_map_state *state = g->maps->retired;
        g->maps->retired = state->pool_next;
        state->pool_next = g->maps->spare;
        g->maps->spare = state;
    }
}
static bool level_frame_current(qa_q1_game *game, const q1_map_runtime *maps,
    const qa_q1_level *level, qa_error *error) {
    if (!game->destroy_pending && !game->continuation_pending && game->maps == maps &&
        maps->options.level == level && q1_alive(game, maps->world_actor)) return true;
    return q1_map_fail(error, "Q1 source limit check lost its actual level/world owner");
}
bool q1_map_level_frame(qa_q1_game *game, const qa_source_frame *frame, qa_error *error) {
    if (!game->maps || !game->options.deathmatch || !game->maps->world_actor.registry)
        return true;
    q1_map_runtime *maps = game->maps;
    qa_q1_level *level = maps->options.level;
    if (!level || !frame || !game->host.cvars ||
        !level_frame_current(game, maps, level, error))
        return q1_map_fail(error, "Q1 source limits require their real frame and cvar owner");
    double seconds = game->time;
    uint64_t time_ns = game->time_ns;
    size_t count = 0;
    for (uint32_t i = 0; i < game->capacity; ++i) {
        const q1_player *player = game->players[i];
        if (player && player->source_client && q1_alive(game, player->id)) ++count;
    }
    if (count > SIZE_MAX / sizeof(float))
        return q1_map_fail(error, "Q1 source score extent overflows");
    float *scores = count ? malloc(count * sizeof(*scores)) : NULL;
    if (count && !scores) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Reading actual Q1 source scores");
        return false;
    }
    size_t written = 0;
    for (uint32_t i = 0; i < game->capacity; ++i) {
        const q1_player *player = game->players[i];
        if (player && player->source_client && q1_alive(game, player->id))
            scores[written++] = player->source_frags;
    }
    float minutes, frags;
    if (!q1_source_value(game, QA_Q1_SOURCE_TIMELIMIT, 0, &minutes, error) ||
        !level_frame_current(game, maps, level, error) ||
        !q1_source_value(game, QA_Q1_SOURCE_FRAGLIMIT, 0, &frags, error) ||
        !level_frame_current(game, maps, level, error)) {
        free(scores);
        return false;
    }
    const qa_q1_level_state *state = qa_q1_level_read(level);
    if (state->next_map || (minutes == 0 && frags == 0)) {
        free(scores);
        return true;
    }
    bool reached = minutes != 0 && seconds >= (double)minutes * 60;
    for (size_t i = 0; i < written; ++i)
        if (frags != 0 && scores[i] >= frags) reached = true;
    if (!reached) {
        free(scores);
        return true;
    }
    qa_q1_source_entity changelevel = {0};
    bool found = false;
    qa_bytes map_name = qa_strings_text(qa_session_strings(game->services.session),
        maps->options.current_map);
    bool start = map_name.size == sizeof("start") - 1 &&
        !memcmp(map_name.data, "start", sizeof("start") - 1);
    bool okay = start || qa_q1_source_entity_first(game, "trigger_changelevel",
        &changelevel, &found, error);
    qa_string_id destination = 0;
    if (okay && found) {
        const q1_actor *entity = q1_entity_const(game, changelevel.actor);
        if (!entity || entity->kind != Q1_MAP || !entity->map)
            okay = q1_map_fail(error, "Q1 source limit lost its actual changelevel declaration");
        else destination = entity->map->map;
    }
    if (okay) {
        game->time_ns = time_ns;
        game->time = seconds;
        bool triggered;
        okay = qa_q1_level_check_limits(level, seconds, scores, written, minutes, frags,
            destination, &triggered, error) && level_frame_current(game, maps, level, error);
    }
    free(scores);
    return okay;
}
void q1_map_destroy(qa_q1_game *g) {
    if (!g->maps)
        return;
    for (uint32_t i = 0; i < g->capacity; ++i) {
        q1_actor *entity = g->actors[i];
        if (entity && entity->active && entity->native) {
            qa_targets_unbind_context(g->maps->options.targets, entity->id, g);
            entity->map = NULL;
        }
    }
    q1_map_state *state = g->maps->allocated;
    while (state) {
        q1_map_state *next = state->allocated_next;
        free(state);
        state = next;
    }
    door_groups_free(g->maps->door_groups);
    free(g->maps->rotated_targets);
    free(g->maps->frame_ticks);
    free(g->maps->addon_contacts);
    free(g->maps);
    g->maps = NULL;
}
bool q1_map_clone(qa_q1_game *g, const q1_actor *source, q1_actor *destination, qa_error *error) {
    if (!source->map)
        return true;
    q1_map_state *copy = q1_map_allocate(g, destination, error);
    if (!copy)
        return false;
    q1_map_state *allocated = copy->allocated_next;
    *copy = *source->map;
    copy->allocated_next = allocated;
    copy->pool_next = NULL;
    if (g->maps->rotated_targets) {
        q1_rotate_target *row = &g->maps->rotated_targets[source->id.slot];
        if (qa_actor_id_equal(row->actor, source->id)) {
            g->maps->rotated_targets[destination->id.slot] = *row;
            g->maps->rotated_targets[destination->id.slot].actor = destination->id;
        }
    }
    return true;
}
bool q1_map_collision(const q1_actor *entity, qa_actor_collision *collision) {
    if (!entity->map || !entity->map->has_inline_model || entity->physics.solid != QA_PHYSICS_BRUSH)
        return false;
    collision->inline_model = true;
    collision->model = entity->map->inline_model;
    return true;
}
bool q1_map_schedule(qa_q1_game *g, q1_actor *entity, double delay, q1_map_action action,
                     qa_error *error) {
    if (!isfinite(delay))
        return q1_map_fail(error, "invalid Q1 map think delay");
    if (entity->physics.motion == QA_PHYSICS_PUSH) {
        double local, due;
        if (!q1_local_time(entity, &local, error) ||
            !q1_think_deadline(local, delay, &due, error))
            return false;
        qa_scheduler_cancel(qa_session_scheduler(g->services.session), entity->id);
        entity->think = Q1_THINK_MAP;
        entity->next_think = due;
    } else if (!q1_schedule(g, entity, delay, Q1_THINK_MAP, error))
        return false;
    entity->map->action = action;
    return true;
}
void q1_map_cancel(qa_q1_game *g, q1_actor *entity) {
    qa_scheduler_cancel(qa_session_scheduler(g->services.session), entity->id);
    entity->think = Q1_THINK_NONE;
    entity->next_think = -1;
    if (entity->map)
        entity->map->action = Q1_MAP_IDLE;
}
bool q1_map_damageable(qa_q1_game *g, q1_actor *entity, bool enabled, qa_error *error) {
    qa_combat_state traits;
    if (!qa_combat_read_traits(g->services.combat, entity->id, &traits, error))
        return false;
    traits.can_take_damage = enabled;
    return qa_combat_set_traits(g->services.combat, entity->id, &traits, error);
}
bool q1_map_player(qa_q1_game *g, qa_actor_id actor) {
    qa_builtin_actor_traits traits;
    return q1_alive(g, actor) && g->services.actor_traits &&
           g->services.actor_traits(g->services.context, actor, &traits) && traits.player;
}
bool q1_map_targets(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    return qa_targets_use(g->maps->options.targets, entity->id, activator, g->time_ns, error);
}
bool q1_map_ambient(qa_q1_game *g, qa_vec3 origin, const char *sound, float volume,
                    qa_error *error) {
    qa_string_id resource;
    return qa_builtin_resource(&g->services, sound, &resource, error) &&
           g->maps->options.ambient(g->maps->options.context, origin, resource, volume, 3, error);
}
bool q1_map_lightstyle(qa_q1_game *g, q1_actor *entity, const char *pattern, qa_error *error) {
    qa_string_id resource;
    return qa_builtin_resource(&g->services, pattern, &resource, error) &&
           g->maps->options.lightstyle(g->maps->options.context, entity->map->style, resource,
                                       error);
}

static bool fields(qa_q1_game *g, q1_actor *entity, const qa_q1_map_fields *source,
                   qa_error *error) {
    if (!source)
        return true;
    const float numbers[] = {
        source->height,        source->lip,          source->width,
        source->length,        source->pause_time,   source->volume,
        source->duration,      source->distance,     source->next_think_seconds,
        source->local_time_seconds,
        source->counter_value, source->spawn_multi,  source->spawn_silent,
        source->gravity,       source->current_ammo, source->pain_finished,
        source->weapon,        source->frags, source->goal_state, source->fog_density};
    for (size_t i = 0; i < sizeof(numbers) / sizeof(*numbers); ++i)
        if (!isfinite(numbers[i]))
            return q1_map_fail(error, "nonfinite Q1 authored field");
    if (!qa_vec_finite(source->mangle) || !qa_vec_finite(source->movedir) ||
        !qa_vec_finite(source->rotate) || !qa_vec_finite(source->dest) ||
        !qa_vec_finite(source->dest2) || !qa_vec_finite(source->pos2) ||
        !qa_vec_finite(source->angular_velocity) || !qa_vec_finite(source->particle_size) ||
        !qa_vec_finite(source->fog_color) ||
        (source->has_view_offset && !qa_vec_finite(source->view_offset)))
        return q1_map_fail(error, "invalid Q1 authored direction");
    q1_map_state *state = entity->map;
    const char *input[] = {
        source->model,   source->map,    source->noise,          source->noise1,
        source->noise2,  source->noise3, source->endtext,        source->intermissiontext,
        source->netname, source->event,  source->spawn_function, source->spawn_classname,
        source->group, source->path, source->category, source->fog_info_entity, source->mdl};
    qa_string_id *output[] = {
        &state->original_model, &state->map,      &state->noise[0],       &state->noise[1],
        &state->noise[2],       &state->noise[3], &state->endtext,        &state->intermissiontext,
        &state->netname,        &state->event,    &state->spawn_function, &state->spawn_classname,
        &state->group, &state->path, &state->category, &state->fog_info_entity, &state->mdl};
    for (size_t i = 0; i < sizeof(input) / sizeof(*input); ++i)
        if (input[i] && input[i][0] &&
            !qa_builtin_resource(&g->services, input[i], output[i], error))
            return false;
    entity->model = state->original_model;
    entity->physics.q1_pusher.local_seconds = source->local_time_seconds;
    state->mangle = source->mangle;
    state->rotate = source->rotate;
    state->dest = source->dest;
    state->dest2 = source->dest2;
    state->has_dest2 = source->has_dest2;
    state->pos2 = source->pos2;
    state->particle_size = source->particle_size;
    state->fog_color = source->fog_color;
    state->fog_density = source->fog_density;
    entity->skin = source->skin;
    if (q1_map_is_addon_brush(state->kind)) {
        entity->physics.angular_velocity = source->angular_velocity;
        entity->frame = source->frame;
    }
    state->view_offset = source->view_offset;
    state->has_view_offset = source->has_view_offset;
    state->height = source->height;
    state->lip = source->lip;
    state->has_movedir = source->has_movedir;
    state->movedir = source->movedir;
    state->width = source->width;
    state->length = source->length;
    state->pause_time = source->pause_time;
    state->volume = source->volume;
    state->duration = source->duration;
    state->distance = source->distance;
    state->initial_think = source->next_think_seconds;
    if (q1_map_is_hip_hazard(state->kind))
        state->pending.hazard.enabled = source->initial_state != 0;
    state->sounds = source->sounds;
    state->style = source->style;
    state->color_map = source->color_map;
    state->impulse = source->impulse;
    state->counter_value = source->counter_value;
    state->goal_state = source->goal_state;
    state->field_state = (float)source->initial_state;
    state->particle_color = source->particle_color;
    state->spawn_multi = source->spawn_multi;
    state->spawn_silent = source->spawn_silent;
    state->gravity = source->gravity;
    state->current_ammo = source->current_ammo;
    state->weapon = source->weapon;
    state->frags = source->frags;
    if (state->kind == Q1_MAP_TIME_MACHINE)
        state->cooldown = source->pain_finished;
    if (source->model && source->model[0] == '*') {
        const char *number = source->model + 1;
        char *end;
        errno = 0;
        unsigned long model = strtoul(number, &end, 10);
        if (*number < '0' || *number > '9' || *end || errno || model > UINT32_MAX)
            return q1_map_fail(error, "invalid Q1 inline model name");
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
            !qa_collision_model_bounds(qa_world_geometry(g->services.world), (uint32_t)model,
                                       &body.bounds, error) ||
            !qa_world_body_write(g->services.world, entity->id, &body, error))
            return false;
        state->has_inline_model = true;
        state->inline_model = (uint32_t)model;
    }
    return true;
}
q1_map_kind q1_map_classify(const char *name) {
    static const struct {
        const char *name;
        q1_map_kind kind;
    } classes[] = {{"worldspawn", Q1_MAP_WORLD},
                   {"rubble_generator", Q1_MAP_ROGUE_RUBBLE_SOURCE},
                   {"light_lantern", Q1_MAP_ROGUE_LAMP},
                   {"pendulum", Q1_MAP_PENDULUM},
                   {"func_new_plat", Q1_MAP_ROGUE_PLAT},
                   {"func_elvtr_button", Q1_MAP_ELEVATOR_BUTTON},
                   {"item_time_machine", Q1_MAP_TIME_MACHINE},
                   {"item_time_core", Q1_MAP_TIME_CORE},
                   {"earthquake", Q1_MAP_ROGUE_QUAKE},
                   {"trigger_earthquake", Q1_MAP_ROGUE_QUAKE_FIELD},
                   {"trigger_earthquake_kill", Q1_MAP_ROGUE_QUAKE_KILL},
                   {"buzzsaw", Q1_MAP_BUZZSAW},
                   {"ltrail_start", Q1_MAP_LTRAIL_START},
                   {"ltrail_relay", Q1_MAP_LTRAIL_RELAY},
                   {"ltrail_end", Q1_MAP_LTRAIL_END},
                   {"func_counter", Q1_MAP_HIP_COUNTER},
                   {"func_oncount", Q1_MAP_ONCOUNT},
                   {"trigger_command", Q1_MAP_ONCOUNT},
                   {"trigger_usekey", Q1_MAP_USE_KEY},
                   {"trigger_remove", Q1_MAP_REMOVE_TRIGGER},
                   {"trigger_setgravity", Q1_MAP_GRAVITY_TRIGGER},
                   {"trigger_decoy_use", Q1_MAP_DECOY_TRIGGER},
                   {"trigger_waterfall", Q1_MAP_WATERFALL},
                   {"trigger_damagethreshold", Q1_MAP_THRESHOLD},
                   {"func_breakawaywall", Q1_MAP_BREAKAWAY},
                   {"func_spawn", Q1_MAP_SPAWNER},
                   {"func_spawn_small", Q1_MAP_SPAWNER},
                   {"func_wall", Q1_MAP_WALL},
                   {"func_door", Q1_MAP_DOOR},
                   {"func_button", Q1_MAP_BUTTON},
                   {"func_axe_button", Q1_MAP_BUTTON},
                   {"trigger_door_relay", Q1_MAP_ADDON_DOOR_RELAY},
                   {"trigger_doorgroup_relay", Q1_MAP_ADDON_DOOR_GROUP},
                   {"trigger_lore", Q1_MAP_ADDON_LORE},
                   {"trigger_music", Q1_MAP_ADDON_MUSIC},
                   {"trigger_heal", Q1_MAP_ADDON_HEAL},
                   {"trigger_quad", Q1_MAP_ADDON_QUAD},
                   {"trigger_teleport_silent", Q1_MAP_ADDON_SILENT_TELEPORT},
                   {"trigger_cutscene", Q1_MAP_ADDON_CUTSCENE},
                   {"trigger_relay_setskill", Q1_MAP_ADDON_SKILL},
                   {"trigger_explosion_repeater", Q1_MAP_ADDON_EXPLOSION_REPEATER},
                   {"horde_manager", Q1_MAP_HORDE_MANAGER},
                   {"info_monster_start", Q1_MAP_HORDE_NORMAL},
                   {"info_monster_start_ranged", Q1_MAP_HORDE_RANGED},
                   {"info_monster_start_flying", Q1_MAP_HORDE_FLYING},
                   {"info_monster_start_boss", Q1_MAP_HORDE_BOSS},
                   {"info_horde_ammo", Q1_MAP_HORDE_AMMO},
                   {"info_horde_item", Q1_MAP_HORDE_ITEM},
                   {"info_horde_key", Q1_MAP_HORDE_KEY},
                   {"trigger_screenshake", Q1_MAP_ADDON_SHAKE},
                   {"trigger_sound", Q1_MAP_ADDON_SOUND},
                   {"trigger_lightning", Q1_MAP_ADDON_LIGHTNING},
                   {"trigger_fade", Q1_MAP_ADDON_FADE_TRIGGER},
                   {"trigger_freeze", Q1_MAP_ADDON_FREEZE},
                   {"particle_embers", Q1_MAP_ADDON_EMBERS},
                   {"particle_embers_tall", Q1_MAP_ADDON_EMBERS_TALL},
                   {"particle_tele", Q1_MAP_ADDON_PARTICLE_TELE},
                   {"particle_tele_fountain", Q1_MAP_ADDON_FOUNTAIN},
                   {"func_door_secret", Q1_MAP_SECRET_DOOR},
                   {"func_plat", Q1_MAP_PLAT},
                   {"func_train", Q1_MAP_TRAIN},
                   {"trap_spike_mine", Q1_MAP_SPIKE_MINE},
                   {"trap_lightning", Q1_MAP_HIP_LIGHTNING},
                   {"trap_lightning_triggered", Q1_MAP_HIP_LIGHTNING_TRIGGERED},
                   {"trap_lightning_switched", Q1_MAP_HIP_LIGHTNING_SWITCHED},
                   {"trap_tesla_coil", Q1_MAP_TESLA},
                   {"info_rotate", Q1_MAP_ROTATE_INFO},
                   {"path_rotate", Q1_MAP_ROTATE_PATH},
                   {"rotate_object", Q1_MAP_ROTATE_OBJECT},
                   {"func_rotate_entity", Q1_MAP_ROTATE_ENTITY},
                   {"func_rotate_door", Q1_MAP_ROTATE_DOOR},
                   {"func_movewall", Q1_MAP_MOVEWALL},
                   {"func_clock", Q1_MAP_CLOCK},
                   {"func_rotate_train", Q1_MAP_ROTATE_TRAIN},
                   {"dynamiclight", Q1_MAP_DYNAMIC_LIGHT},
                   {"target_lightramp", Q1_MAP_LIGHT_RAMP},
                   {"light_candle", Q1_MAP_CANDLE},
                   {"light_flame_gas", Q1_MAP_GAS_FLAME},
                   {"misc_rope", Q1_MAP_ROPE},
                   {"func_bob", Q1_MAP_ADDON_BOB},
                   {"func_toss", Q1_MAP_ADDON_TOSS},
                   {"func_shatter", Q1_MAP_ADDON_SHATTER},
                   {"func_debris", Q1_MAP_ADDON_DEBRIS},
                   {"func_explode", Q1_MAP_ADDON_EXPLODE},
                   {"func_hurt", Q1_MAP_ADDON_HURT},
                   {"func_fade", Q1_MAP_ADDON_FADE},
                   {"misc_model", Q1_MAP_ADDON_MODEL},
                   {"rotate_object_continuously", Q1_MAP_ADDON_ROTATE},
                   {"info_rotate_axis", Q1_MAP_ADDON_AXIS},
                   {"func_breakable", Q1_MAP_ADDON_BREAKABLE},
                   {"trap_gods_wrath", Q1_MAP_GODS_WRATH},
                   {"trap_gravity_well", Q1_MAP_GRAVITY_WELL},
                   {"func_train2", Q1_MAP_TRAIN2},
                   {"func_bobbingwater", Q1_MAP_BOBBING_WATER},
                   {"func_pushable", Q1_MAP_PUSHABLE},
                   {"misc_teleporttrain", Q1_MAP_TRAIN},
                   {"func_episodegate", Q1_MAP_GATE},
                   {"func_bossgate", Q1_MAP_GATE},
                   {"func_illusionary", Q1_MAP_STATIC},
                   {"misc_corpse", Q1_MAP_STATIC},
                   {"item_sigil", Q1_MAP_SIGIL},
                   {"misc_rune_indicator", Q1_MAP_RUNE_INDICATOR},
                   {"mge2m2_rune_pickup_fixer", Q1_MAP_SIGIL_FIXER},
                   {"mge2m2_electrode_target", Q1_MAP_ELECTRODE_TARGET},
                   {"mge2m2_electrode_button", Q1_MAP_BUTTON},
                   {"mge2m2_rune_egg_opener", Q1_MAP_EGG_OPENER},
                   {"info_fog", Q1_MAP_FOG_INFO},
                   {"trigger_fog", Q1_MAP_FOG_TRIGGER},
                   {"trigger_fog_transition", Q1_MAP_FOG_TRANSITION},
                   {"trap_spikeshooter", Q1_MAP_SHOOTER},
                   {"trap_shooter", Q1_MAP_SHOOTER},
                   {"misc_fireball", Q1_MAP_FIREBALL_SOURCE},
                   {"air_bubbles", Q1_MAP_BUBBLES},
                   {"light_globe", Q1_MAP_STATIC},
                   {"light_torch_small_walltorch", Q1_MAP_STATIC},
                   {"light_flame_large_yellow", Q1_MAP_STATIC},
                   {"light_flame_small_yellow", Q1_MAP_STATIC},
                   {"light_flame_small_white", Q1_MAP_STATIC},
                   {"ambient_suck_wind", Q1_MAP_AMBIENT},
                   {"ambient_flouro_buzz", Q1_MAP_AMBIENT},
                   {"ambient_drip", Q1_MAP_AMBIENT},
                   {"ambient_thunder", Q1_MAP_AMBIENT},
                   {"ambient_light_buzz", Q1_MAP_AMBIENT},
                   {"ambient_swamp1", Q1_MAP_AMBIENT},
                   {"ambient_swamp2", Q1_MAP_AMBIENT},
                   {"ambient_drone", Q1_MAP_AMBIENT},
                   {"ambient_comp_hum", Q1_MAP_AMBIENT},
                   {"ambient_generic", Q1_MAP_AMBIENT},
                   {"viewthing", Q1_MAP_VIEW},
                   {"misc_noisemaker", Q1_MAP_NOISE},
                   {"event_lightning", Q1_MAP_LIGHTNING},
                   {"play_sound", Q1_MAP_SOUND},
                   {"play_sound_triggered", Q1_MAP_SOUND},
                   {"random_thunder", Q1_MAP_SOUND},
                   {"random_thunder_triggered", Q1_MAP_SOUND},
                   {"ambient_humming", Q1_MAP_HIP_AMBIENT},
                   {"ambient_rushing", Q1_MAP_HIP_AMBIENT},
                   {"ambient_running_water", Q1_MAP_HIP_AMBIENT},
                   {"ambient_fan_blowing", Q1_MAP_HIP_AMBIENT},
                   {"ambient_waterfall", Q1_MAP_HIP_AMBIENT},
                   {"ambient_riftpower", Q1_MAP_HIP_AMBIENT},
                   {"info_command", Q1_MAP_COMMAND},
                   {"effect_teleport", Q1_MAP_TELEPORT_EFFECT},
                   {"func_exploder", Q1_MAP_EXPLODER},
                   {"func_multi_exploder", Q1_MAP_EXPLODER},
                   {"func_rubble", Q1_MAP_RUBBLE_SOURCE},
                   {"func_rubble1", Q1_MAP_RUBBLE_SOURCE},
                   {"func_rubble2", Q1_MAP_RUBBLE_SOURCE},
                   {"func_rubble3", Q1_MAP_RUBBLE_SOURCE},
                   {"func_earthquake", Q1_MAP_EARTHQUAKE},
                   {"effect_finale", Q1_MAP_HIP_FINALE},
                   {"info_startendtext", Q1_MAP_START_ENDTEXT},
                   {"func_particlefield", Q1_MAP_PARTICLE_FIELD},
                   {"func_togglewall", Q1_MAP_TOGGLE_WALL},
                   {"wallsprite", Q1_MAP_WALL_SPRITE},
                   {"misc_sacrifice", Q1_MAP_SACRIFICE},
                   {"trigger_multiple", Q1_MAP_MULTI},
                   {"trigger_once", Q1_MAP_MULTI},
                   {"trigger_secret", Q1_MAP_MULTI},
                   {"trigger_counter", Q1_MAP_COUNTER},
                   {"trigger_counter_timed", Q1_MAP_ADDON_COUNTER_TIMED},
                   {"trigger_repeater", Q1_MAP_ADDON_REPEATER},
                   {"trigger_multitouch", Q1_MAP_ADDON_MULTITOUCH},
                   {"trigger_explosion", Q1_MAP_ADDON_EXPLOSION},
                   {"trigger_changetarget", Q1_MAP_ADDON_CHANGE_TARGET},
                   {"trigger_cleanup_corpses", Q1_MAP_ADDON_CLEANUP},
                   {"mge2m2_cleanup_corpses", Q1_MAP_ADDON_CLEANUP},
                   {"trigger_always", Q1_MAP_ADDON_ALWAYS},
                   {"trigger_rune_relay", Q1_MAP_ADDON_RUNE_RELAY},
                   {"trigger_rune_counter", Q1_MAP_ADDON_RUNE_COUNTER},
                   {"trigger_bloodynightmare_relay", Q1_MAP_ADDON_BN_RELAY},
                   {"trigger_sacrifice_counter", Q1_MAP_ADDON_SACRIFICE_COUNTER},
                   {"trigger_check_sacrifices", Q1_MAP_ADDON_CHECK_SACRIFICES},
                   {"trigger_relay_killmonster", Q1_MAP_ADDON_KILL_MONSTER},
                   {"trigger_health_relay", Q1_MAP_ADDON_HEALTH_RELAY},
                   {"trigger_relay", Q1_MAP_RELAY},
                   {"trigger_teleport", Q1_MAP_TELEPORT},
                   {"info_teleport_destination", Q1_MAP_DESTINATION},
                   {"trigger_hurt", Q1_MAP_HURT},
                   {"trigger_push", Q1_MAP_PUSH},
                   {"trigger_shelter_portal", Q1_MAP_SHELTER},
                   {"trigger_changelevel", Q1_MAP_CHANGELEVEL},
                   {"hub_trigger_changelevel", Q1_MAP_CHANGELEVEL},
                   {"trigger_setskill", Q1_MAP_SETSKILL},
                   {"trigger_onlyregistered", Q1_MAP_REGISTERED},
                   {"trigger_monsterjump", Q1_MAP_MONSTERJUMP},
                   {"path_corner", Q1_MAP_PATH},
                   {"path_follow", Q1_MAP_FOLLOW},
                   {"path_follow2", Q1_MAP_FOLLOW},
                   {"target_cancelpause", Q1_MAP_CANCEL_PAUSE},
                   {"target_switchpath", Q1_MAP_SWITCH_PATH},
                   {"info_player_start", Q1_MAP_POINT},
                   {"info_player_start_hub", Q1_MAP_POINT},
                   {"info_player_start2", Q1_MAP_POINT},
                   {"info_player_coop", Q1_MAP_COOP_POINT},
                   {"trigger_activate_coop_spawns", Q1_MAP_COOP_ACTIVATE},
                   {"info_player_deathmatch", Q1_MAP_POINT},
                   {"info_intermission", Q1_MAP_POINT},
                   {"info_notnull", Q1_MAP_POINT},
                   {"testplayerstart", Q1_MAP_POINT},
                   {"light", Q1_MAP_LIGHT},
                   {"light_fluoro", Q1_MAP_LIGHT},
                   {"light_fluorospark", Q1_MAP_LIGHT},
                   {"misc_explobox", Q1_MAP_BARREL},
                   {"misc_explobox2", Q1_MAP_BARREL}};
    for (size_t i = 0; i < sizeof(classes) / sizeof(*classes); ++i)
        if (!strcmp(name, classes[i].name))
            return classes[i].kind;
    return Q1_MAP_FIELDS;
}
bool q1_map_spawn(qa_q1_game *g, q1_actor *entity, const qa_q1_spawn *spawn, bool *handled,
                  qa_error *error) {
    if (!strcmp(spawn->classname, "info_null")) {
        *handled = true;
        return q1_remove(g, entity, error);
    }
    q1_map_kind kind = q1_map_classify(spawn->classname);
    if (g->options.program == QA_Q1_ROGUE) {
        bool wall = !strcmp(spawn->classname, "func_ctf_wall");
        bool teleport = !strcmp(spawn->classname, "trigger_teleport") && (spawn->spawnflags & 4u);
        if (!strcmp(spawn->classname, "info_player_team1") ||
            !strcmp(spawn->classname, "info_player_team2")) kind = Q1_MAP_POINT;
        if (wall || teleport) {
            qa_actor_id actor = entity->id;
            float mode;
            if (!g->host.cvars || !q1_source_value(g, QA_Q1_SOURCE_TEAMPLAY, 0, &mode, error)) {
                if (!error || error->code == QA_OK)
                    q1_map_fail(error, "Rogue authored constructor lost its actual source teamplay");
                return false;
            }
            if (!q1_alive(g, actor) || q1_entity(g, actor) != entity)
                return q1_map_fail(error, "Rogue authored constructor retired during its source policy read");
            if (mode != 4 && mode != 5 && mode != 6) {
                *handled = true;
                return q1_remove(g, entity, error);
            }
            if (wall) kind = Q1_MAP_WALL;
        }
    }
    if (g->options.program == QA_Q1_CTF) {
        qa_bytes classname = {(const uint8_t *)spawn->classname, strlen(spawn->classname)};
        if (q1_ctf_monster_removed(classname)) {
            *handled = true;
            return q1_remove(g, entity, error);
        }
        if (!strcmp(spawn->classname, "trigger_voteexit"))
            kind = Q1_MAP_CTF_VOTE_EXIT;
        else if (!strcmp(spawn->classname, "trigger_changelevel"))
            kind = Q1_MAP_CTF_CHANGELEVEL;
        else if (!strcmp(spawn->classname, "info_vote_destination"))
            kind = Q1_MAP_DESTINATION;
        else if (!strcmp(spawn->classname, "func_ctf_wall"))
            kind = Q1_MAP_WALL;
        else if (!strcmp(spawn->classname, "info_player_team1") ||
                 !strcmp(spawn->classname, "info_player_team2"))
            kind = Q1_MAP_POINT;
    }
    if (g->options.program == QA_Q1_ROGUE && !strcmp(spawn->classname, "trigger_explosion"))
        kind = Q1_MAP_ROGUE_EXPLOSION_TRIGGER;
    if (g->options.program == QA_Q1_ROGUE && !strcmp(spawn->classname, "light_candle"))
        kind = Q1_MAP_ROGUE_LAMP;
    bool addon = g->options.program == QA_Q1_DOPA || g->options.program == QA_Q1_MG1 ||
                 g->options.program == QA_Q1_MG3;
    if (g->options.program != QA_Q1_MG1 && g->options.program != QA_Q1_MG3) {
        if (kind == Q1_MAP_COOP_POINT)
            kind = Q1_MAP_POINT;
        else if (kind == Q1_MAP_COOP_ACTIVATE)
            kind = Q1_MAP_FIELDS;
    }
    if (!addon && (q1_map_is_addon_effect(kind) || q1_map_is_fog(kind)))
        kind = Q1_MAP_FIELDS;
    if (g->options.program != QA_Q1_MG3 && q1_map_is_addon_control(kind))
        kind = Q1_MAP_FIELDS;
    if (g->options.program != QA_Q1_DOPA && g->options.program != QA_Q1_MG1 &&
        q1_map_is_horde(kind))
        kind = Q1_MAP_FIELDS;
    if ((!addon && (q1_map_is_addon_campaign(kind) ||
                    !strcmp(spawn->classname, "info_player_start_hub") ||
                    !strcmp(spawn->classname, "hub_trigger_changelevel"))) ||
        (g->options.program == QA_Q1_MG3 &&
         (kind == Q1_MAP_SIGIL_FIXER || kind == Q1_MAP_ELECTRODE_TARGET ||
          kind == Q1_MAP_EGG_OPENER || !strcmp(spawn->classname, "hub_trigger_changelevel"))) ||
        ((g->options.program != QA_Q1_DOPA && g->options.program != QA_Q1_MG1) &&
         !strcmp(spawn->classname, "mge2m2_electrode_button")) ||
        (g->options.program != QA_Q1_MG3 && !strcmp(spawn->classname, "func_axe_button")))
        kind = Q1_MAP_FIELDS;
    if (addon && kind == Q1_MAP_COUNTER)
        kind = Q1_MAP_ADDON_COUNTER;
    if ((!addon && q1_map_is_addon_trigger(kind)) ||
        (g->options.program != QA_Q1_MG3 && q1_map_is_addon_trigger(kind) &&
         kind >= Q1_MAP_ADDON_ALWAYS) ||
        (g->options.program == QA_Q1_MG3 &&
         !strcmp(spawn->classname, "mge2m2_cleanup_corpses")))
        kind = Q1_MAP_FIELDS;
    bool addon_static = !strcmp(spawn->classname, "misc_corpse") ||
                        !strcmp(spawn->classname, "ambient_generic");
    if ((!addon && (addon_static || q1_map_is_addon_visual(kind) || q1_map_is_addon_brush(kind) ||
                    kind == Q1_MAP_SHELTER)) ||
        ((kind == Q1_MAP_ROPE || kind == Q1_MAP_ADDON_BREAKABLE) &&
         g->options.program != QA_Q1_MG3))
        kind = Q1_MAP_FIELDS;
    if (g->options.program != QA_Q1_ROGUE &&
        (kind == Q1_MAP_PENDULUM || q1_map_is_rogue_plat(kind) || q1_map_is_time_actor(kind) ||
         q1_map_is_rogue_hazard(kind) || q1_map_is_rogue_misc(kind)))
        kind = Q1_MAP_FIELDS;
    if (g->options.program != QA_Q1_MG3 &&
        (kind == Q1_MAP_CANCEL_PAUSE || kind == Q1_MAP_SWITCH_PATH))
        kind = Q1_MAP_FIELDS;
    if (g->options.program != QA_Q1_HIPNOTIC &&
        (kind == Q1_MAP_FOLLOW || kind == Q1_MAP_TRAIN2 || kind == Q1_MAP_BOBBING_WATER ||
         kind == Q1_MAP_PUSHABLE || kind == Q1_MAP_SPAWNER || q1_map_is_hip_trigger(kind) ||
         q1_map_is_hip_hazard(kind) || q1_map_is_rotation(kind) ||
         kind == Q1_MAP_HIP_FINALE || kind == Q1_MAP_START_ENDTEXT))
        kind = Q1_MAP_FIELDS;
    *handled = kind != Q1_MAP_FIELDS;
    if (!*handled && !spawn->map_fields)
        return true;
    q1_map_state *state = q1_map_allocate(g, entity, error);
    if (!state)
        return false;
    state->kind = kind;
    state->electrode_button = *handled && !strcmp(spawn->classname, "mge2m2_electrode_button");
    if (!fields(g, entity, spawn->map_fields, error))
        return false;
    if (kind == Q1_MAP_LIGHT && q1_classnamed(g, entity->id, g->runtime_names[Q1_NAME_LIGHT_FLUOROSPARK]) && !state->style)
        state->style = 10;
    if (!*handled)
        return true;
    entity->kind = Q1_MAP;
    if (q1_map_is_ctf(kind))
        return q1_map_ctf_spawn(g, entity, error);
    if (q1_map_is_rogue_misc(kind))
        return q1_map_rogue_misc_spawn(g, entity, error);
    if (q1_map_is_fog(kind))
        return q1_map_addon_fog_spawn(g, entity, error);
    if (q1_map_is_addon_effect(kind))
        return q1_map_addon_effect_spawn(g, entity, error);
    if (q1_map_is_horde(kind))
        return q1_map_horde_spawn(g, entity, error);
    if (q1_map_is_addon_control(kind))
        return q1_map_addon_control_spawn(g, entity, error);
    if (q1_map_is_addon_campaign(kind))
        return q1_map_addon_campaign_spawn(g, entity, error);
    if (addon && kind == Q1_MAP_SIGIL)
        return q1_map_addon_sigil_spawn(g, entity, error);
    if (addon && kind == Q1_MAP_GATE && !strcmp(spawn->classname, "func_bossgate"))
        return q1_map_addon_bossgate_spawn(g, entity, error);
    if (q1_map_is_addon_trigger(kind))
        return q1_map_addon_trigger_spawn(g, entity, error);
    if (q1_map_is_addon_brush(kind))
        return q1_map_addon_brush_spawn(g, entity, error);
    if (q1_map_is_addon_field(g, kind))
        return q1_map_addon_field_spawn(g, entity, error);
    if (q1_map_is_addon_visual(kind))
        return q1_map_addon_visual_spawn(g, entity, error);
    if (addon && (kind == Q1_MAP_LIGHT || kind == Q1_MAP_STATIC)) {
        bool selected;
        if (!q1_map_addon_light_spawn(g, entity, &selected, error))
            return false;
        if (selected)
            return true;
    }
    if (q1_map_is_rotation(kind))
        return q1_map_rotation_spawn(g, entity, error);
    if (q1_map_is_hip_hazard(kind))
        return q1_map_hip_hazard_spawn(g, entity, error);
    if (q1_map_is_rogue_hazard(kind))
        return q1_map_rogue_hazard_spawn(g, entity, error);
    if (q1_map_is_time_actor(kind))
        return q1_map_time_spawn(g, entity, error);
    if (q1_map_is_rogue_plat(kind))
        return q1_map_rogue_plat_spawn(g, entity, error);
    if (kind == Q1_MAP_PENDULUM)
        return q1_map_pendulum_spawn(g, entity, error);
    if (q1_map_is_hip_trigger(kind))
        return q1_map_hip_trigger_spawn(g, entity, error);
    if (kind == Q1_MAP_SPAWNER)
        return q1_map_hip_spawner_spawn(g, entity, spawn, error);
    if (kind == Q1_MAP_BOBBING_WATER || kind == Q1_MAP_PUSHABLE)
        return q1_map_hip_brush_spawn(g, entity, error);
    if (kind == Q1_MAP_SACRIFICE)
        return q1_map_sacrifice_spawn(g, entity, error);
    if (q1_map_is_mover(kind))
        return q1_map_mover_spawn(g, entity, error);
    if (kind >= Q1_MAP_PARTICLE_FIELD)
        return q1_map_hip_particles_spawn(g, entity, error);
    if (kind >= Q1_MAP_SOUND)
        return q1_map_hip_misc_spawn(g, entity, error);
    if (kind >= Q1_MAP_GATE)
        return q1_map_special_spawn(g, entity, error);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    switch (kind) {
    case Q1_MAP_WORLD: {
        entity->physics.motion = QA_PHYSICS_PUSH;
        entity->physics.solid = QA_PHYSICS_BRUSH;
        g->maps->world_actor = entity->id;
        if (!q1_body_queue_initialize(g, error)) return false;
        static const char *const styles[] = {"m",
                                             "mmnmmommommnonmmonqnmmo",
                                             "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba",
                                             "mmmmmaaaaammmmmaaaaaabcdefgabcdefg",
                                             "mamamamamama",
                                             "jklmnopqrstuvwxyzyxwvutsrqponmlkj",
                                             "nmonqnmomnmomomno",
                                             "mmmaaaabcdefgmmmmaaaammmaamm",
                                             "mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa",
                                             "aaaaaaaazzzzzzzz",
                                             "mmamammmmammamamaaamammma",
                                             "abcdefghijklmnopqrrqponmlkjihgfedcba"};
        g->options.world_type = spawn->map_fields ? spawn->map_fields->world_type : 0;
        for (size_t i = 0; i < sizeof(styles) / sizeof(*styles); ++i) {
            qa_string_id pattern;
            if (!qa_builtin_resource(&g->services, styles[i], &pattern, error) ||
                !g->maps->options.lightstyle(g->maps->options.context, (int32_t)i, pattern, error))
                return false;
            if (!q1_alive(g, entity->id))
                return true;
        }
        return true;
    }
    case Q1_MAP_WALL:
        if (!state->has_inline_model)
            return q1_map_fail(error, "Q1 wall has no brush model");
        entity->physics.motion = QA_PHYSICS_PUSH;
        entity->physics.solid = QA_PHYSICS_BRUSH;
        body.angles = qa_v3(0, 0, 0);
        state->use_enabled = !q1_classnamed(g, entity->id, g->runtime_names[Q1_NAME_FUNC_CTF_WALL]);
        break;
    case Q1_MAP_POINT:
        if (q1_classnamed(g, entity->id, g->runtime_names[Q1_NAME_INFO_PLAYER_DEATHMATCH]) &&
            !q1_source_runes_start(g, error)) return false;
        break;
    case Q1_MAP_DESTINATION:
        if (!q1_map_text(g, entity->targetname))
            return q1_map_fail(error, "Q1 teleport destination has no targetname");
        state->mangle = body.angles;
        if (g->options.program == QA_Q1_CTF &&
            q1_classnamed(g, entity->id, g->runtime_names[Q1_NAME_INFO_VOTE_DESTINATION]))
            entity->model = QA_STRING_NONE;
        body.angles = qa_v3(0, 0, 0);
        body.origin.z += 27;
        break;
    case Q1_MAP_PATH:
        if (!q1_map_text(g, entity->targetname))
            return q1_map_fail(error, "Q1 path corner has no targetname");
        if (g->options.program == QA_Q1_MG3 && entity->wait < 0)
            entity->wait = 999999;
        entity->physics.solid = QA_PHYSICS_TRIGGER;
        state->touch_enabled = true;
        body.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
        break;
    case Q1_MAP_FOLLOW:
        entity->physics.solid = QA_PHYSICS_TRIGGER;
        state->touch_enabled = true;
        if (!strcmp(spawn->classname, "path_follow2"))
            body.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
        else {
            entity->physics.motion = QA_PHYSICS_STATIONARY;
            entity->model = QA_STRING_NONE;
        }
        break;
    case Q1_MAP_CANCEL_PAUSE:
    case Q1_MAP_SWITCH_PATH:
        if (!q1_map_text(g, entity->target) || !q1_map_text(g, entity->targetname) ||
            (kind == Q1_MAP_SWITCH_PATH && !q1_map_text(g, state->netname)))
            return q1_map_fail(error, "Q1 path control has missing authored target fields");
        state->use_enabled = true;
        break;
    case Q1_MAP_LIGHT:
        if (q1_classnamed(g, entity->id, g->runtime_names[Q1_NAME_LIGHT]) && !q1_map_text(g, entity->targetname))
            return q1_remove(g, entity, error);
        if (state->style >= 32 && !q1_classnamed(g, entity->id, g->runtime_names[Q1_NAME_LIGHT_FLUOROSPARK])) {
            state->use_enabled = true;
            if (!q1_map_lightstyle(g, entity, entity->spawnflags & 1 ? "a" : "m", error))
                return false;
            if (!q1_alive(g, entity->id))
                return true;
        }
        if (q1_classnamed(g, entity->id, g->runtime_names[Q1_NAME_LIGHT_FLUORO]) ||
            q1_classnamed(g, entity->id, g->runtime_names[Q1_NAME_LIGHT_FLUOROSPARK]))
            if (!q1_map_ambient(g, body.origin,
                                q1_classnamed(g, entity->id, g->runtime_names[Q1_NAME_LIGHT_FLUORO])
                                    ? "ambience/fl_hum1.wav"
                                    : "ambience/buzz1.wav",
                                .5f, error))
                return false;
        break;
    case Q1_MAP_BARREL: {
        bool small = q1_classnamed(g, entity->id, g->runtime_names[Q1_NAME_MISC_EXPLOBOX2]);
        if (!q1_model(g, entity, small ? "maps/b_exbox2.bsp" : "maps/b_explob.bsp", error) ||
            !qa_combat_set_health(g->services.combat, entity->id, 20, error) ||
            !q1_map_damageable(g, entity, true, error))
            return false;
        entity->physics.solid = QA_PHYSICS_BOX;
        entity->aimed_damage = true;
        body.bounds = (qa_bounds){{0, 0, 0}, {32, 32, small ? 32 : 64}};
        body.origin.z += 2;
        qa_trace_query query = {.start = body.origin,
                                .end = qa_vec_add(body.origin, qa_v3(0, 0, -256)),
                                .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
                                .pass_actor = entity->id,
                                .policy = qa_collision_default_policy(QA_GAME_Q1)};
        qa_trace_result trace;
        if (!qa_world_trace(g->services.world, &query, &trace, error))
            return false;
        if (trace.fraction < 1 && !trace.all_solid) {
            if (body.origin.z - trace.end.z > 250)
                return q1_remove(g, entity, error);
            body.origin = trace.end;
            body.ground = trace.hit == QA_TRACE_HIT_WORLD ? qa_actor_reference_source(g->options.provider, 0) : q1_ref_from(g, trace.actor);
            entity->physics.flags |= QA_PHYSICS_ONGROUND;
        }
        break;
    }
    default:
        return q1_map_trigger_spawn(g, entity, error);
    }
    return !q1_alive(g, entity->id) ||
           (qa_world_body_write(g->services.world, entity->id, &body, error) &&
            q1_link(g, entity, error));
}

bool q1_map_use(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_actor_id activator,
                qa_error *error) {
    if (!entity->map || !entity->map->use_enabled)
        return true;
    if (q1_map_is_rogue_misc(entity->map->kind))
        return q1_map_rogue_misc_use(g, entity, error);
    if (q1_map_is_fog(entity->map->kind))
        return q1_map_addon_fog_activate(g, entity, other, error);
    if (q1_map_is_addon_effect(entity->map->kind))
        return q1_map_addon_effect_use(g, entity, activator, error);
    if (q1_map_is_horde(entity->map->kind))
        return q1_map_horde_use(g, entity, error);
    if (q1_map_is_addon_control(entity->map->kind))
        return q1_map_addon_control_use(g, entity, activator, error);
    if (q1_map_is_addon_campaign(entity->map->kind))
        return q1_map_addon_campaign_use(g, entity, activator, error);
    if (q1_map_is_addon_trigger(entity->map->kind))
        return q1_map_addon_trigger_use(g, entity, activator, error);
    if (q1_map_is_addon_brush(entity->map->kind))
        return q1_map_addon_brush_use(g, entity, error);
    if (q1_map_is_addon_field(g, entity->map->kind))
        return q1_map_addon_field_use(g, entity, error);
    if (q1_map_is_addon_visual(entity->map->kind))
        return q1_map_addon_visual_use(g, entity, error);
    if (q1_map_is_rotation(entity->map->kind))
        return q1_map_rotation_use(g, entity, error);
    if (q1_map_is_hip_hazard(entity->map->kind))
        return q1_map_hip_hazard_use(g, entity, activator, error);
    if (q1_map_is_rogue_hazard(entity->map->kind))
        return q1_map_rogue_hazard_use(g, entity, other, activator, error);
    if (q1_map_is_rogue_plat(entity->map->kind))
        return q1_map_rogue_plat_use(g, entity, other, activator, error);
    if (entity->map->kind == Q1_MAP_PENDULUM)
        return q1_map_pendulum_use(g, entity, error);
    if (q1_map_is_hip_trigger(entity->map->kind))
        return q1_map_hip_trigger_use(g, entity, other, activator, error);
    if (entity->map->kind == Q1_MAP_SPAWNER)
        return q1_map_hip_spawner_use(g, entity, error);
    if (entity->map->kind == Q1_MAP_CANCEL_PAUSE || entity->map->kind == Q1_MAP_SWITCH_PATH)
        return q1_map_path_use(g, entity, error);
    if (entity->map->kind == Q1_MAP_SACRIFICE) {
        entity->activator = q1_ref_from(g, activator);
        return q1_map_sacrifice_gib(g, entity, error);
    }
    if (q1_map_is_mover(entity->map->kind))
        return q1_map_mover_use(g, entity, activator, error);
    if (entity->map->kind >= Q1_MAP_PARTICLE_FIELD)
        return q1_map_hip_particles_use(g, entity, other, error);
    if (entity->map->kind >= Q1_MAP_SOUND)
        return q1_map_hip_misc_use(g, entity, activator, error);
    if (entity->map->kind >= Q1_MAP_GATE)
        return q1_map_special_use(g, entity, activator, error);
    if (entity->map->kind == Q1_MAP_WALL) {
        entity->frame = 1 - entity->frame;
        return true;
    }
    if (entity->map->kind == Q1_MAP_LIGHT) {
        entity->spawnflags ^= 1;
        return q1_map_lightstyle(g, entity, entity->spawnflags & 1 ? "a" : "m", error);
    }
    return q1_map_trigger_use(g, entity, other, activator, error);
}
bool q1_map_touch(qa_q1_game *g, q1_actor *entity, const qa_touch_contact *contact,
                  qa_error *error) {
    if (entity->map && q1_map_is_ctf(entity->map->kind))
        return !entity->map->touch_enabled ||
               q1_map_ctf_touch(g, entity, contact->other, error);
    if (entity->map && q1_map_is_rogue_misc(entity->map->kind))
        return !entity->map->touch_enabled ||
               q1_map_rogue_misc_touch(g, entity, contact->other, error);
    if (entity->map && q1_map_is_fog(entity->map->kind))
        return !entity->map->touch_enabled ||
               q1_map_addon_fog_activate(g, entity, contact->other, error);
    if (entity->map && entity->map->electrode_button)
        return !entity->map->touch_enabled ||
               q1_map_addon_electrode_touch(g, entity, contact->other, error);
    if (entity->map && q1_map_is_addon_effect(entity->map->kind))
        return true;
    if (entity->map && q1_map_is_horde(entity->map->kind))
        return true;
    if (entity->map && q1_map_is_addon_control(entity->map->kind))
        return !entity->map->touch_enabled ||
               q1_map_addon_control_touch(g, entity, contact->other, error);
    if (entity->map && q1_map_is_addon_campaign(entity->map->kind))
        return true;
    if (entity->map && q1_map_is_addon_trigger(entity->map->kind))
        return !entity->map->touch_enabled ||
               q1_map_addon_trigger_touch(g, entity, contact->other, error);
    if (entity->map && q1_map_is_addon_brush(entity->map->kind))
        return !entity->map->touch_enabled ||
               q1_map_addon_brush_touch(g, entity, contact->other, error);
    if (entity->map && q1_map_is_addon_field(g, entity->map->kind))
        return !entity->map->touch_enabled ||
               q1_map_addon_field_touch(g, entity, contact->other, error);
    if (entity->map && q1_map_is_rotation(entity->map->kind))
        return !entity->map->touch_enabled ||
               q1_map_rotation_touch(g, entity, contact->other, false, error);
    if (entity->map && q1_map_is_addon_visual(entity->map->kind))
        return true;
    if (entity->map && entity->map->touch_enabled && q1_map_is_hip_hazard(entity->map->kind))
        return q1_map_hip_hazard_touch(g, entity, contact->other, error);
    if (entity->map && entity->map->touch_enabled && q1_map_is_rogue_hazard(entity->map->kind))
        return q1_map_rogue_hazard_touch(g, entity, contact->other, error);
    if (entity->map && entity->map->touch_enabled && q1_map_is_rogue_plat(entity->map->kind))
        return q1_map_rogue_plat_touch(g, entity, contact->other, error);
    if (entity->map && entity->map->touch_enabled && entity->map->kind == Q1_MAP_PENDULUM)
        return q1_map_pendulum_touch(g, entity, contact->other, error);
    if (entity->map && entity->map->touch_enabled && q1_map_is_hip_trigger(entity->map->kind))
        return q1_map_hip_trigger_touch(g, entity, contact->other, error);
    if (entity->map && entity->map->kind == Q1_MAP_PUSHABLE_PROXY)
        return q1_map_pushable_touch(g, entity, contact->other, error);
    if (entity->map && entity->map->touch_enabled && entity->map->kind >= Q1_MAP_PARTICLE_FIELD)
        return q1_map_hip_particles_touch(g, entity, contact->other, error);
    if (entity->map && entity->map->touch_enabled && q1_map_is_mover(entity->map->kind))
        return q1_map_mover_touch(g, entity, contact->other, error);
    if (entity->map && entity->map->touch_enabled && entity->map->kind >= Q1_MAP_SOUND)
        return q1_map_hip_misc_touch(g, entity, contact->other, error);
    if (entity->map && entity->map->touch_enabled && entity->map->kind >= Q1_MAP_GATE)
        return q1_map_special_touch(g, entity, contact->other, error);
    return !entity->map || !entity->map->touch_enabled ||
           q1_map_trigger_touch(g, entity, contact, error);
}
bool q1_map_blocked(qa_q1_game *g, q1_actor *entity, qa_actor_id obstacle, qa_error *error) {
    if (entity->map && q1_map_is_addon_brush(entity->map->kind))
        return q1_map_addon_brush_blocked(g, entity, obstacle, error);
    if (entity->map && q1_map_is_rotation(entity->map->kind))
        return q1_map_rotation_touch(g, entity, obstacle, true, error);
    if (entity->map && q1_map_is_rogue_plat(entity->map->kind))
        return q1_map_rogue_plat_blocked(g, entity, obstacle, error);
    return !entity->map || !q1_map_is_mover(entity->map->kind) ||
           q1_map_mover_blocked(g, entity, obstacle, error);
}
bool q1_map_reaction(qa_q1_game *g, q1_actor *entity, const qa_damage_outcome *outcome,
                     qa_error *error) {
    if (q1_map_is_rogue_misc(entity->map->kind))
        return q1_map_rogue_misc_reaction(g, entity, outcome, error);
    if (q1_map_is_addon_effect(entity->map->kind))
        return true;
    if (q1_map_is_horde(entity->map->kind))
        return true;
    if (q1_map_is_addon_control(entity->map->kind))
        return true;
    if (q1_map_is_addon_trigger(entity->map->kind))
        return true;
    if (q1_map_is_addon_brush(entity->map->kind))
        return q1_map_addon_brush_reaction(g, entity, outcome, error);
    if (q1_map_is_addon_field(g, entity->map->kind))
        return true;
    if (q1_map_is_rotation(entity->map->kind))
        return true;
    if (q1_map_is_addon_visual(entity->map->kind))
        return true;
    if (q1_map_is_hip_hazard(entity->map->kind))
        return q1_map_hip_hazard_reaction(g, entity, outcome, error);
    if (q1_map_is_time_actor(entity->map->kind))
        return q1_map_time_reaction(g, entity, outcome, error);
    if (q1_map_is_rogue_plat(entity->map->kind))
        return q1_map_rogue_plat_reaction(g, entity, outcome, error);
    if (entity->map->kind == Q1_MAP_THRESHOLD)
        return q1_map_hip_trigger_reaction(g, entity, outcome, error);
    if (q1_map_is_mover(entity->map->kind))
        return q1_map_mover_reaction(g, entity, outcome, error);
    if (outcome->result.reaction != QA_REACTION_DEATH)
        return true;
    if (entity->map->kind == Q1_MAP_SACRIFICE)
        return q1_map_sacrifice_gib(g, entity, error);
    if (entity->map->kind == Q1_MAP_MULTI)
        return !q1_map_grounded(g, entity, outcome->request.attack.attacker) ||
               q1_map_multi_fire(g, entity, outcome->request.attack.attacker, error);
    if (entity->map->kind != Q1_MAP_BARREL)
        return true;
    entity->activator = q1_ref_from(g, outcome->request.attack.attacker);
    return qa_builtin_resource(&g->services, "explo_box", &entity->classname, error) &&
           q1_map_damageable(g, entity, false, error) &&
           q1_map_schedule(g, entity, .3, Q1_MAP_BARREL_EXPLODE, error);
}
bool q1_map_think(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_map_state *state = entity->map;
    q1_map_action action = state->action;
    state->action = Q1_MAP_IDLE;
    if (action == Q1_MAP_CTF_NEXTLEVEL)
        return q1_map_ctf_nextlevel_think(g, entity, error);
    if (action == Q1_MAP_FOREIGN_REMOVE) {
        if (state->kind != Q1_MAP_DELAY || !g->maps->options.retire_actor)
            return q1_map_fail(error, "invalid Q1 foreign removal continuation owner");
        qa_actor_id helper = entity->id, target = q1_ref_actor(g, entity->owner);
        if (q1_alive(g, target) &&
            !g->maps->options.retire_actor(g->maps->options.context, target, error))
            return false;
        entity = q1_entity(g, helper);
        return !entity || q1_remove(g, entity, error);
    }
    if (action == Q1_MAP_ROGUE_RUBBLE_THROW) {
        if (state->kind != Q1_MAP_ROGUE_RUBBLE_SOURCE)
            return q1_map_fail(error, "invalid Rogue rubble continuation owner");
        return q1_map_rogue_rubble_throw(g, entity, error);
    }
    if (action >= Q1_MAP_ADDON_SHAKE_TICK && action <= Q1_MAP_ADDON_PARTICLE_TICK) {
        if (!q1_map_addon_effect_action_matches(state->kind, action))
            return q1_map_fail(error, "invalid Q1 addon effect continuation owner");
        return q1_map_addon_effect_think(g, entity, action, error);
    }
    if (action == Q1_MAP_ADDON_EXPLOSION_REPEAT) {
        if (state->kind != Q1_MAP_ADDON_EXPLOSION_REPEATER)
            return q1_map_fail(error, "invalid Q1 explosion repeater continuation owner");
        return q1_map_addon_control_think(g, entity, error);
    }
    if (action == Q1_MAP_CAMPAIGN_USE_TARGETS || action == Q1_MAP_SIGIL_FIX) {
        if (!q1_map_campaign_action_matches(state->kind, action))
            return q1_map_fail(error, "invalid Q1 campaign continuation owner");
        return q1_map_addon_campaign_think(g, entity, action, error);
    }
    if (action >= Q1_MAP_ADDON_COUNTER_RESET && action <= Q1_MAP_ADDON_EXPLOSION_FIRE)
        return q1_map_addon_trigger_think(g, entity, action, error);
    if (action >= Q1_MAP_ADDON_BOB_STEP && action <= Q1_MAP_ADDON_BREAKABLE_STOP)
        return q1_map_addon_brush_think(g, entity, action, error);
    if (action == Q1_MAP_LIGHT_RAMP_INIT)
        return q1_map_addon_visual_think(g, entity, error);
    if (action >= Q1_MAP_ROTATE_FIRST && action <= Q1_MAP_ROTATE_TRAIN_TICK)
        return q1_map_rotation_think(g, entity, action, error);
    if (action >= Q1_MAP_MINE_FIRST && action <= Q1_MAP_GRAVITY_PULL)
        return q1_map_hip_hazard_think(g, entity, action, error);
    if (action >= Q1_MAP_ROGUE_QUAKE_START && action <= Q1_MAP_LTRAIL_CHAIN)
        return q1_map_rogue_hazard_think(g, entity, action, error);
    if (action >= Q1_MAP_ENDING_CONTROL && action <= Q1_MAP_CAMERA_TRACK)
        return q1_map_ending_think(g, entity, action, error);
    if (action >= Q1_MAP_TIME_BOOM_THINK && action <= Q1_MAP_TIME_CRASH_THINK)
        return q1_map_time_think(g, entity, action, error);
    if (action >= Q1_MAP_ROGUE_PLAT_UP && action <= Q1_MAP_ELEVATOR_BUTTON_DONE)
        return q1_map_rogue_plat_think(g, entity, action, error);
    if (action == Q1_MAP_PENDULUM_SWING)
        return q1_map_pendulum_think(g, entity, error);
    if (action == Q1_MAP_COUNTER_START || action == Q1_MAP_COUNTER_TICK)
        return q1_map_hip_trigger_think(g, entity, action, error);
    if (action == Q1_MAP_BOB_WATER)
        return q1_map_bob_water(g, entity, error);
    if (action == Q1_MAP_SACRIFICE_ANIMATE || action == Q1_MAP_SACRIFICE_FLOAT)
        return q1_map_sacrifice_think(g, entity, action, error);
    if (action >= Q1_MAP_MOVE_DONE && action <= Q1_MAP_TRAIN_WAIT)
        return q1_map_mover_think(g, entity, action, error);
    if (action >= Q1_MAP_SOUND_REPEAT)
        return q1_map_hip_misc_think(g, entity, action, error);
    if (action >= Q1_MAP_LIGHTNING_FIRE)
        return q1_map_boss_think(g, entity, action, error);
    if (action >= Q1_MAP_SIGIL_PLACE)
        return q1_map_special_think(g, entity, action, error);
    switch (action) {
    case Q1_MAP_IDLE:
        return true;
    case Q1_MAP_REMOVE:
        return q1_remove(g, entity, error);
    case Q1_MAP_REARM:
        if (state->kind == Q1_MAP_HURT) {
            entity->physics.solid = QA_PHYSICS_TRIGGER;
            if (q1_map_is_addon_field(g, state->kind))
                q1_map_cancel(g, entity);
        }
        else if (entity->max_health > 0) {
            if (!qa_combat_set_health(g->services.combat, entity->id, entity->max_health, error) ||
                !q1_map_damageable(g, entity, true, error))
                return false;
            entity->physics.solid = QA_PHYSICS_BOX;
        }
        return q1_link(g, entity, error);
    case Q1_MAP_DELAYED_USE: {
        qa_target_use use = {.source = entity->id,
            .activator = q1_ref_actor(g, entity->activator),
            .dialect = state->pending.delayed.dialect,
            .fields = {.classname = entity->classname, .target = entity->target,
                .killtarget = entity->killtarget, .message = entity->message,
                .shader_old = state->pending.delayed.shader_old,
                .shader_new = state->pending.delayed.shader_new},
            .time_ns = g->time_ns};
        if (!qa_targets_use_now(g->maps->options.targets, &use, error))
            return false;
        return !q1_alive(g, entity->id) || q1_remove(g, entity, error);
    }
    case Q1_MAP_BEGIN_LEVEL:
        if (g->options.program == QA_Q1_DOPA || g->options.program == QA_Q1_MG1 ||
            g->options.program == QA_Q1_MG3)
            return q1_map_addon_changelevel_begin(g, entity, error);
        return qa_q1_level_begin(g->maps->options.level, state->map, q1_ref_actor(g, entity->activator), g->time,
                                 error);
    case Q1_MAP_PENDING_LEVEL:
        if (!qa_q1_level_begin_pending(g->maps->options.level, g->time, error))
            return false;
        return !q1_alive(g, entity->id) || q1_remove(g, entity, error);
    case Q1_MAP_FINALE_TIMER:
        if (!qa_q1_campaign_source_timer(g->maps->options.campaign_source, state->pending.finale,
                                         error))
            return false;
        return !q1_alive(g, entity->id) || q1_remove(g, entity, error);
    case Q1_MAP_BARREL_EXPLODE: {
        if (!q1_radius(g, entity->id, q1_ref_actor(g, entity->activator), 160, (qa_actor_id){0}, QA_Q1_WEAPON_COUNT,
                       error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
            !q1_sound(g, entity->id, "weapons/r_exp3.wav", 0, 1, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
        if (!q1_effect(g, QA_BUILTIN_EXPLOSION, entity->id,
                       qa_vec_add(body.origin, qa_v3(0, 0, 32)), 0, 0, error))
            return false;
        return !q1_alive(g, entity->id) || q1_remove(g, entity, error);
    }
    default:
        return q1_map_fail(error, "unknown Q1 map continuation");
    }
}

bool q1_map_timer(qa_q1_game *g, const char *name, q1_actor **out, qa_error *error) {
    if (!g || !g->maps)
        return q1_map_fail(error, "Q1 map services are not bound");
    q1_actor *entity;
    if (!q1_create(g, name, Q1_MAP, (qa_actor_id){0}, &entity, error))
        return false;
    if (!q1_map_allocate(g, entity, error)) {
        (void)q1_remove(g, entity, NULL);
        return false;
    }
    entity->map->kind = Q1_MAP_DELAY;
    *out = entity;
    return true;
}
bool qa_q1_game_map_defer_targets(qa_q1_game *g, const qa_target_use *use, qa_error *error) {
    if (!use || !isfinite(use->fields.delay_seconds))
        return q1_map_fail(error, "invalid Q1 delayed target use");
    q1_actor *entity;
    if (!q1_map_timer(g, "DelayedUse", &entity, error))
        return false;
    entity->map->pending.delayed.dialect = use->dialect;
    entity->map->pending.delayed.shader_old = use->fields.shader_old;
    entity->map->pending.delayed.shader_new = use->fields.shader_new;
    entity->activator = q1_ref_from(g, use->activator);
    entity->target = use->fields.target;
    entity->killtarget = use->fields.killtarget;
    entity->message = use->fields.message;
    if (q1_map_schedule(g, entity, use->fields.delay_seconds, Q1_MAP_DELAYED_USE, error))
        return true;
    (void)q1_remove(g, entity, NULL);
    return false;
}
bool qa_q1_game_map_defer_level(qa_q1_game *g, double delay, qa_error *error) {
    q1_actor *entity;
    if (!q1_map_timer(g, "nextlevel", &entity, error))
        return false;
    if (q1_map_schedule(g, entity, delay, Q1_MAP_PENDING_LEVEL, error))
        return true;
    (void)q1_remove(g, entity, NULL);
    return false;
}
bool qa_q1_game_map_defer_remove(qa_q1_game *g, qa_actor_id target, double delay,
                                qa_error *error) {
    if (!g || g->destroy_pending || !g->maps || !g->maps->options.retire_actor ||
        !isfinite(delay) || delay < 0)
        return q1_map_fail(error, "invalid Q1 delayed actor removal");
    if (!q1_alive(g, target))
        return true;
    qa_q1_game_operation operation;
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    q1_actor *entity = NULL;
    bool ok = q1_map_timer(g, "DelayedRemove", &entity, error);
    if (ok) {
        entity->owner = q1_ref_from(g, target);
        ok = q1_map_schedule(g, entity, delay, Q1_MAP_FOREIGN_REMOVE, error);
        if (!ok && q1_alive(g, entity->id))
            (void)q1_remove(g, entity, NULL);
    }
    if (ok && !qa_q1_game_operation_live(&operation)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 source retired during delayed removal");
        ok = false;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool qa_q1_game_map_defer_finale(qa_q1_game *g, qa_q1_campaign_timer kind, double delay,
                                 qa_error *error) {
    if (!g || !g->maps || !g->maps->options.campaign_source || kind < QA_Q1_CAMPAIGN_CHECK_FINALE ||
        kind > QA_Q1_CAMPAIGN_FINISH_FINALE)
        return q1_map_fail(error, "invalid Q1 campaign timer");
    q1_actor *entity;
    if (!q1_map_timer(g, "finale_timer", &entity, error))
        return false;
    entity->map->pending.finale = kind;
    if (q1_map_schedule(g, entity, delay, Q1_MAP_FINALE_TIMER, error))
        return true;
    (void)q1_remove(g, entity, NULL);
    return false;
}
qa_string_id qa_q1_game_map_text(const qa_q1_game *g, qa_actor_id actor, qa_q1_campaign_text kind) {
    const q1_actor *entity = q1_entity_const(g, actor);
    if (!entity || !entity->map)
        return QA_STRING_NONE;
    return kind == QA_Q1_CAMPAIGN_ENDTEXT             ? entity->map->endtext
           : kind == QA_Q1_CAMPAIGN_INTERMISSION_TEXT ? entity->map->intermissiontext
                                                      : QA_STRING_NONE;
}
bool qa_q1_game_map_set_text(qa_q1_game *g, qa_actor_id actor, qa_q1_campaign_text kind,
                             qa_string_id text, qa_error *error) {
    q1_actor *entity = g ? q1_entity(g, actor) : NULL;
    if (!entity || !entity->map ||
        (text && !qa_strings_cstr(qa_session_strings(g->services.session), text)))
        return q1_map_fail(error, "invalid Q1 map text target");
    switch (kind) {
    case QA_Q1_CAMPAIGN_ENDTEXT:
        entity->map->endtext = text;
        return true;
    case QA_Q1_CAMPAIGN_INTERMISSION_TEXT:
        entity->map->intermissiontext = text;
        return true;
    }
    return q1_map_fail(error, "unknown Q1 map text field");
}
void qa_q1_game_map_secrets(const qa_q1_game *g, uint32_t *total, uint32_t *found) {
    if (total)
        *total = g && g->maps ? g->maps->total_secrets : 0;
    if (found)
        *found = g && g->maps ? g->maps->found_secrets : 0;
}
