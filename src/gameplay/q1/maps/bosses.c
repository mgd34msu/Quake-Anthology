#include "internal.h"

static q1_actor *first_class(qa_q1_game *g, qa_string_id name) {
    uint32_t cursor = 0;
    const qa_actor_record *record;
    while (qa_actors_next(qa_session_actors(g->services.session), &cursor, &record)) {
        q1_actor *entity = q1_entity(g, record->id);
        if (entity && q1_classnamed(g, entity->id, name))
            return entity;
    }
    return NULL;
}
static bool lightning_fire(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_actor *first = q1_entity(g, q1_ref_actor(g, g->maps->electrodes[0]));
    q1_actor *second = q1_entity(g, q1_ref_actor(g, g->maps->electrodes[1]));
    if (!first || !second || !first->map || !second->map)
        return q1_map_fail(error, "Q1 lightning electrodes are missing");
    if (g->time >= g->maps->lightning_end) {
        if (!q1_map_door_down(g, first, error))
            return false;
        return !q1_alive(g, second->id) || q1_map_door_down(g, second, error);
    }
    qa_body_state a, b;
    if (!qa_world_body_read(g->services.world, first->id, &a, error) ||
        !qa_world_body_read(g->services.world, second->id, &b, error))
        return false;
    qa_vec3 start = qa_vec_scale(qa_vec_add(a.bounds.mins, a.bounds.maxs), .5f);
    qa_vec3 end = qa_vec_scale(qa_vec_add(b.bounds.mins, b.bounds.maxs), .5f);
    start.z = a.origin.z + a.bounds.mins.z - 16;
    end.z = b.origin.z + b.bounds.mins.z - 16;
    end = qa_vec_sub(end, qa_vec_scale(qa_vec_normalize(qa_vec_sub(end, start)), 100));
    qa_builtin_event event = {.kind = QA_BUILTIN_BEAM,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = q1_alive(g, g->maps->world_actor) ? g->maps->world_actor
                                                                         : entity->id,
                              .origin = start,
                              .end = end,
                              .code = 3,
                              .time_ns = g->time_ns};
    return qa_builtin_emit(&g->services, &event, error) &&
           (!q1_alive(g, entity->id) ||
            q1_map_schedule(g, entity, .1, Q1_MAP_LIGHTNING_FIRE, error));
}
bool q1_map_lightning_use(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    if (g->maps->lightning_end >= g->time + 1)
        return true;
    qa_string_id name;
    if (!qa_builtin_resource(&g->services, "lightning", &name, error))
        return false;
    q1_actor *electrodes[2] = {0};
    size_t count = 0;
    uint32_t cursor = 0;
    const qa_actor_record *record;
    while (count < 2 && qa_actors_next(qa_session_actors(g->services.session), &cursor, &record)) {
        q1_actor *candidate = q1_entity(g, record->id);
        if (candidate && candidate->target == name)
            electrodes[count++] = candidate;
    }
    if (count != 2 || !electrodes[0]->map || !electrodes[1]->map ||
        electrodes[0]->map->kind != Q1_MAP_DOOR || electrodes[1]->map->kind != Q1_MAP_DOOR)
        return q1_map_fail(error, "Q1 lightning requires two authored electrode doors");
    q1_map_position position = electrodes[0]->map->pending.mover.position;
    if (position != electrodes[1]->map->pending.mover.position ||
        (position != Q1_MAP_TOP && position != Q1_MAP_BOTTOM))
        return true;
    for (size_t i = 0; i < 2; ++i) {
        q1_map_cancel(g, electrodes[i]);
        g->maps->electrodes[i] = q1_ref_from(g, electrodes[i]->id);
    }
    g->maps->lightning_end = g->time + 1;
    if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_MISC_POWER_WAV], 2, 1, 1, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (!lightning_fire(g, entity, error))
        return false;
    q1_actor *boss = first_class(g, g->runtime_names[Q1_NAME_MONSTER_BOSS]);
    if (!boss || boss->kind != Q1_MONSTER)
        return true;
    boss->state.monster.enemy = q1_ref_from(g, activator);
    if (!q1_alive(g, electrodes[0]->id) ||
        electrodes[0]->map->pending.mover.position != Q1_MAP_TOP || q1_health(g, boss->id) <= 0)
        return true;
    if (!q1_sound_resource(g, boss->id, g->runtime_names[Q1_NAME_RESOURCE_BOSS1_PAIN_WAV], 2, 1, 1, error))
        return false;
    if (!q1_alive(g, boss->id))
        return true;
    float health = q1_health(g, boss->id) - 1;
    return qa_combat_set_health(g->services.combat, boss->id, health, error) &&
           (!q1_alive(g, boss->id) || q1_monster_play(g, boss,
                                                      health >= 2   ? "boss_shocka1"
                                                      : health == 1 ? "boss_shockb1"
                                                                    : "boss_shockc1",
                                                      error));
}
bool q1_map_finale_emit(qa_q1_game *g, uint32_t stage, const char *text, qa_error *error) {
    if (!g->maps->options.finale)
        return q1_map_fail(error, "Q1 finale requires a presentation owner");
    qa_q1_map_finale_view view = g->maps->finale;
    view.stage = stage;
    view.text = QA_STRING_NONE;
    if (text && *text && !qa_builtin_resource(&g->services, text, &view.text, error))
        return false;
    g->maps->finale = view;
    return g->maps->options.finale(g->maps->options.context, &view, error);
}
static bool style(qa_q1_game *g, const char *text, qa_error *error) {
    qa_string_id pattern;
    return qa_builtin_resource(&g->services, text, &pattern, error) &&
           g->maps->options.lightstyle(g->maps->options.context, 0, pattern, error);
}
static bool achievement(qa_q1_game *g, const char *text, qa_error *error) {
    qa_builtin_event event = {.kind = QA_BUILTIN_ACHIEVEMENT,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .time_ns = g->time_ns};
    return qa_builtin_resource(&g->services, text, &event.text, error) &&
           qa_builtin_emit(&g->services, &event, error);
}
static bool finale_begin(qa_q1_game *g, q1_actor *oldone, qa_error *error) {
    if (g->maps->finale_started)
        return true;
    q1_actor *position = first_class(g, g->runtime_names[Q1_NAME_INFO_INTERMISSION]);
    q1_actor *train = first_class(g, g->runtime_names[Q1_NAME_MISC_TELEPORTTRAIN]);
    if (!position || !position->map || !train)
        return q1_map_fail(error, "Q1 finale requires intermission and teleport train actors");
    qa_body_state view;
    if (!qa_world_body_read(g->services.world, position->id, &view, error))
        return false;
    qa_actor_id train_id = train->id;
    qa_vec3 angles = position->map->mangle;
    qa_string_id start;
    if (!qa_builtin_resource(&g->services, "start", &start, error))
        return false;
    g->maps->finale_started = true;
    g->maps->finale_dismissed = false;
    if (!q1_monster_count_kill(g, oldone, q1_ref_actor(g, oldone->state.monster.enemy), error))
        return false;
    if (!q1_alive(g, oldone->id))
        return true;
    q1_map_cancel(g, oldone);
    train = q1_entity(g, train_id);
    if (train && !q1_remove(g, train, error))
        return false;
    g->maps->finale = (qa_q1_map_finale_view){
        .map = start, .origin = view.origin, .angles = angles, .exit_after = g->time + 10000000};
    if (!qa_q1_level_cutscene(g->maps->options.level, start, q1_ref_actor(g, oldone->state.monster.enemy),
                              g->maps->finale.exit_after, error))
        return false;
    qa_builtin_snapshot_frame *players;
    if (!q1_snapshot_players(g, &players, error))
        return false;
    bool ok = true;
    for (size_t i = 0; i < players->snapshot.count && ok; ++i) {
        qa_actor_id actor = players->snapshot.ids[i];
        if (!q1_alive(g, actor))
            continue;
        qa_body_state body;
        if (!(ok = qa_world_body_read(g->services.world, actor, &body, error)))
            break;
        body.origin = view.origin;
        body.angles = angles;
        body.velocity = qa_v3(0, 0, 0);
        ok = qa_world_body_write(g->services.world, actor, &body, error) &&
             qa_world_link(g->services.world, actor, NULL, error);
        if (!ok || !q1_alive(g, actor))
            continue;
        qa_builtin_motion_change changed = {.reason = QA_BUILTIN_MOTION_TELEPORT,
                                            .body = body,
                                            .view_angles = angles,
                                            .force_view_angles = true};
        ok = g->services.motion_changed(g->services.context, actor, &changed, error);
        if (!ok || !q1_alive(g, actor))
            continue;
        qa_combat_state traits;
        if (!(ok = qa_combat_read_traits(g->services.combat, actor, &traits, error)))
            break;
        traits.can_take_damage = false;
        ok = qa_combat_set_traits(g->services.combat, actor, &traits, error);
    }
    qa_builtin_snapshot_release(players);
    if (!ok || !q1_map_finale_emit(g, 1, "", error))
        return false;
    const char *map =
        qa_strings_cstr(qa_session_strings(g->services.session), g->maps->options.current_map);
    if (g->options.edition == QA_Q1_RERELEASE && map && !strcmp(map, "end")) {
        if (!achievement(g, "ACH_DEFEAT_SHUB", error) ||
            (g->options.skill == 3 && !achievement(g, "ACH_DEFEAT_SHUB_NIGHTMARE", error)))
            return false;
    }
    if (!q1_alive(g, oldone->id))
        return true;
    q1_actor *timer;
    if (!q1_map_timer(g, g->runtime_names[Q1_NAME_CLASS_FINALE_TIMER], &timer, error))
        return false;
    timer->owner = q1_ref_from(g, oldone->id);
    return q1_map_schedule(g, timer, 1, Q1_MAP_FINALE_TWO, error);
}
static bool finale_finish(qa_q1_game *g, q1_actor *oldone, qa_error *error) {
    if (!g->maps->finale_started || g->maps->finale.stage >= 4)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, oldone->id, &body, error) ||
        !q1_sound_resource(g, oldone->id, g->runtime_names[Q1_NAME_RESOURCE_BOSS2_POP2_WAV], 2, 1, 1, error))
        return false;
    if (!q1_alive(g, oldone->id))
        return true;
    for (int z = 16; z <= 144; z += 96)
        for (int x = -64; x <= 64; x += 32)
            for (int y = -64; y <= 64; y += 32) {
                float random = q1_random(g);
                if (!q1_gib_at(g, oldone->id,
                               qa_vec_add(body.origin, qa_v3((float)x, (float)y, (float)z)), -999,
                               random < .3f   ? g->runtime_names[Q1_NAME_RESOURCE_PROGS_GIB1_MDL]
                               : random < .6f ? g->runtime_names[Q1_NAME_RESOURCE_PROGS_GIB2_MDL]
                                              : g->runtime_names[Q1_NAME_RESOURCE_PROGS_GIB3_MDL],
                               error))
                    return false;
            }
    if (!q1_map_finale_emit(
            g, 4, qa_q1_finale_text(g->options.edition == QA_Q1_RERELEASE, "$qc_finale_end"),
            error))
        return false;
    q1_actor *victory;
    if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_FINALE_PLAYER], Q1_ENTITY, (qa_actor_id){0}, &victory, error))
        return false;
    victory->frame = 1;
    qa_body_state pose = {.origin = qa_vec_sub(body.origin, qa_v3(32, 264, 0)),
                          .angles = {0, 290, 0}};
    if (!q1_model(g, victory, g->runtime_names[Q1_NAME_RESOURCE_PROGS_PLAYER_MDL], error) ||
        !qa_world_body_write(g->services.world, victory->id, &pose, error) ||
        !q1_link(g, victory, error) || (q1_alive(g, oldone->id) && !q1_remove(g, oldone, error)) ||
        !style(g, "m", error))
        return false;
    if (g->options.edition == QA_Q1_CLASSIC)
        return true;
    q1_actor *timer;
    return q1_map_timer(g, g->runtime_names[Q1_NAME_CLASS_FINALE_WAIT], &timer, error) &&
           q1_map_schedule(g, timer, 1, Q1_MAP_FINALE_WAIT, error);
}
bool qa_q1_game_map_finale(qa_q1_game *g, qa_actor_id actor, bool finish, qa_error *error) {
    q1_actor *oldone = g ? q1_entity(g, actor) : NULL;
    if (!g || !g->maps || !g->maps->options.finale || !oldone || oldone->kind != Q1_MONSTER ||
        oldone->state.monster.species->species != QA_Q1_OLDONE ||
        (g->options.edition != QA_Q1_CLASSIC && !g->options.coop &&
         !g->maps->options.finish_campaign))
        return q1_map_fail(error, "invalid Q1 finale binding or source actor");
    return finish ? finale_finish(g, oldone, error) : finale_begin(g, oldone, error);
}
void qa_q1_game_map_dismiss_finale(qa_q1_game *g) {
    if (g && !g->destroy_pending && g->maps) {
        g->maps->finale_dismissed = true;
        g->finale_polled = g->finale_acknowledged = true;
        g->finale_last_poll = g->time;
    }
}
bool q1_map_boss_think(qa_q1_game *g, q1_actor *entity, q1_map_action action, qa_error *error) {
    if (action == Q1_MAP_LIGHTNING_FIRE)
        return lightning_fire(g, entity, error);
    if (action == Q1_MAP_FINALE_WAIT) {
        if (!g->maps->finale_dismissed && g->maps->options.finale_finished)
            g->maps->finale_dismissed = g->maps->options.finale_finished(g->maps->options.context);
        if (!g->maps->finale_dismissed)
            return q1_map_schedule(g, entity, .1, Q1_MAP_FINALE_WAIT, error);
        return q1_map_finale_emit(g, 5, "", error) &&
               (!q1_alive(g, entity->id) ||
                q1_map_schedule(g, entity, 5, Q1_MAP_FINALE_SIX, error));
    }
    if (action == Q1_MAP_FINALE_SIX) {
        if (!q1_map_finale_emit(g, 6, "", error))
            return false;
        bool ok = g->options.coop
                      ? qa_q1_level_travel(g->maps->options.level, g->maps->finale.map,
                                           (qa_actor_id){0}, error)
                      : g->maps->options.finish_campaign(g->maps->options.context, error);
        return ok && (!q1_alive(g, entity->id) || q1_remove(g, entity, error));
    }
    q1_actor *oldone = q1_entity(g, q1_ref_actor(g, entity->owner));
    if (!oldone || oldone->kind != Q1_MONSTER)
        return q1_map_fail(error, "Q1 finale continuation lost its source actor");
    if (action == Q1_MAP_FINALE_TWO) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, oldone->id, &body, error) ||
            !q1_effect(g, QA_BUILTIN_TELEPORT, oldone->id,
                       qa_vec_sub(body.origin, qa_v3(0, 100, 0)), 1, 0, error))
            return false;
        if (!q1_alive(g, oldone->id))
            return true;
        return q1_sound_resource(g, oldone->id, g->runtime_names[Q1_NAME_RESOURCE_MISC_R_TELE1_WAV], 2, 1, 1, error) &&
               q1_map_finale_emit(g, 2, "", error) &&
               (!q1_alive(g, entity->id) ||
                q1_map_schedule(g, entity, 2, Q1_MAP_FINALE_THREE, error));
    }
    if (action == Q1_MAP_FINALE_THREE) {
        if (!q1_sound_resource(g, oldone->id, g->runtime_names[Q1_NAME_RESOURCE_BOSS2_DEATH_WAV], 2, 1, 1, error) ||
            !style(g, "abcdefghijklmlkjihgfedcb", error) || !q1_map_finale_emit(g, 3, "", error))
            return false;
        if (q1_alive(g, oldone->id)) {
            oldone->state.monster.next_frame = q1_frame_index("old_thrash1");
            if (!q1_schedule(g, oldone, .1, Q1_THINK_MONSTER_FRAME, error))
                return false;
        }
        return !q1_alive(g, entity->id) || q1_remove(g, entity, error);
    }
    return q1_map_fail(error, "unknown Q1 boss continuation");
}
