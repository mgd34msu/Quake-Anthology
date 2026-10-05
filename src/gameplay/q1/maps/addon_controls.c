#include "internal.h"
#include <float.h>
#include <stdio.h>

static q1_actor *control(qa_q1_game *g, qa_actor_id id) {
    q1_actor *e = q1_entity(g, id);
    return e && e->map && q1_map_is_addon_control(e->map->kind) ? e : NULL;
}
static bool source_deadline(double value, double *out, qa_error *error) {
    if (!isfinite(value) || fabs(value) >= 0x1.ffffffp127) {
        q1_map_fail(error, "Q1 addon trigger deadline exceeds native range");
        return false;
    }
    *out = fabs(value) > FLT_MAX ? (value < 0 ? -FLT_MAX : FLT_MAX) : (float)value;
    return true;
}
static bool broadcast(qa_q1_game *g, qa_actor_id source, const char *text, qa_error *error) {
    qa_builtin_snapshot_frame *players;
    if (!q1_snapshot_players(g, &players, error))
        return false;
    bool ok = true;
    for (size_t i = 0; ok && i < players->snapshot.count && control(g, source); ++i)
        if (q1_alive(g, players->snapshot.ids[i]))
            ok = q1_message(g, players->snapshot.ids[i], text, error);
    qa_builtin_snapshot_release(players);
    return ok;
}
static bool source_top(qa_q1_game *g, qa_actor_id actor) {
    q1_actor *e = q1_entity(g, actor);
    if (e && e->map && q1_map_is_mover(e->map->kind)) {
        q1_map_position position = e->map->pending.mover.position;
        return position == Q1_MAP_TOP || position == Q1_MAP_UP;
    }
    qa_target_field value;
    if (!qa_targets_field(g->maps->options.targets, actor, "state", &value) ||
        value.kind != QA_TARGET_FIELD_TEXT)
        return false;
    const char *text = qa_strings_cstr(qa_session_strings(g->services.session), value.value.text);
    return text && (!strcmp(text, "top") || !strcmp(text, "up"));
}
static bool door_relay(qa_q1_game *g, q1_actor *e, qa_actor_id activator, qa_error *error) {
    qa_actor_id id = e->id;
    uint32_t flags = e->spawnflags;
    qa_builtin_snapshot_frame *targets;
    if (!q1_snapshot_targets(g, g->maps->options.targets, e->target, &targets, error))
        return false;
    bool ok = true;
    for (size_t i = 0; ok && i < targets->snapshot.count && control(g, id); ++i) {
        qa_actor_id actor = targets->snapshot.ids[i];
        bool top = source_top(g, actor);
        if (!control(g, id) || !q1_alive(g, actor) || (flags & (top ? 1u : 2u)))
            continue;
        qa_authored_target fields;
        if (!qa_targets_read(g->maps->options.targets, actor, &fields))
            continue;
        if (!control(g, id) || !q1_alive(g, actor))
            continue;
        const char *classname = qa_strings_cstr(qa_session_strings(g->services.session), fields.classname);
        if (!classname || (strcmp(classname, "func_door") && strcmp(classname, "func_button")))
            continue;
        q1_actor *target = q1_entity(g, actor);
        if (target && target->map &&
            (target->map->kind == Q1_MAP_DOOR || target->map->kind == Q1_MAP_BUTTON))
            ok = q1_map_addon_relay_mover(g, target, top, activator, error);
        else if (g->maps->options.relay_mover)
            ok = g->maps->options.relay_mover(g->maps->options.context, actor, top, activator, error);
        else
            ok = q1_map_fail(error, "Q1 door relay requires the selected foreign mover owner");
    }
    qa_builtin_snapshot_release(targets);
    return ok;
}
static bool door_group(qa_q1_game *g, q1_actor *e, qa_actor_id activator, qa_error *error) {
    qa_actor_id id = e->id;
    qa_string_id category = e->map->category;
    qa_target_cursor cursor = {0};
    qa_actor_id actor;
    while (qa_targets_next_authored(g->maps->options.targets, NULL, &cursor, &actor)) {
        qa_target_field field;
        if (!control(g, id))
            return true;
        if (!qa_targets_field(g->maps->options.targets, actor, "category", &field) ||
            field.kind != QA_TARGET_FIELD_TEXT || field.value.text != category)
            continue;
        bool top = source_top(g, actor);
        double goal = 0;
        (void)qa_targets_number(g->maps->options.targets, actor, "goal_state", &goal);
        if (!control(g, id))
            return true;
        if ((!top && goal == 0) || (top && goal == 1))
            return true;
    }
    e = control(g, id);
    return !e || q1_map_targets(g, e, activator, error);
}
static bool music(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    int32_t track = e->map->style;
    if (e->map->sounds == track)
        return true;
    qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT, .family = QA_GAME_Q1,
                              .provider = g->options.provider, .actor = id,
                              .code = track, .count = track, .time_ns = g->time_ns};
    if (!qa_builtin_resource(&g->services, "music", &event.resource, error) ||
        !qa_builtin_emit(&g->services, &event, error))
        return false;
    qa_target_cursor cursor = {0};
    qa_actor_id actor;
    while (control(g, id) && qa_targets_next_authored(g->maps->options.targets, "trigger_music", &cursor, &actor)) {
        q1_actor *target = control(g, actor);
        if (target && target->map->kind == Q1_MAP_ADDON_MUSIC)
            target->map->sounds = track;
    }
    return true;
}
static bool set_skill(qa_q1_game *g, q1_actor *e, qa_actor_id activator, qa_error *error) {
    qa_actor_id id = e->id;
    qa_string_id message = e->message;
    if (!q1_map_player(g, activator) || !control(g, id))
        return true;
    const char *text = qa_strings_cstr(qa_session_strings(g->services.session), message);
    bool bloody = text && !strcmp(text, "4");
    if (!bloody)
        *g->maps->options.server_flags &= ~(uint32_t)QA_Q1_BLOODY_NIGHTMARE_ACTIVE;
    int32_t skill = text && !strcmp(text, "0") ? 0 : text && !strcmp(text, "1") ? 1 :
                    text && !strcmp(text, "2") ? 2 : text && (!strcmp(text, "3") || bloody) ? 3 : -1;
    if (skill < 0)
        return true;
    if (bloody)
        *g->maps->options.server_flags |= QA_Q1_BLOODY_NIGHTMARE_ACTIVE;
    static const char *const messages[] = {"$mg3_hub_selected_easy", "$mg3_hub_selected_normal",
                                           "$mg3_hub_selected_hard", "$mg3_hub_selected_nightmare"};
    if (!broadcast(g, id, bloody ? "$mg3_selected_bloody_nightmare" : messages[skill], error))
        return false;
    if (!control(g, id))
        return true;
    if (!g->maps->options.set_skill(g->maps->options.context, skill, error))
        return false;
    if (!control(g, id))
        return true;
    g->options.skill = (uint8_t)skill;
    if (!g->maps->options.server_command)
        return q1_map_fail(error, "Q1 skill relay requires the server cvar owner");
    char command[16];
    snprintf(command, sizeof(command), "skill %d", skill);
    qa_string_id name;
    return qa_builtin_resource(&g->services, command, &name, error) &&
           (!control(g, id) ||
             g->maps->options.server_command(g->maps->options.context, name, error));
}
static bool repeat_explosion(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    if (e->delay > 0) {
        double delay = e->delay;
        e->delay = 0;
        return q1_map_schedule(g, e, delay, Q1_MAP_ADDON_EXPLOSION_REPEAT, error);
    }
    q1_actor *previous = q1_entity(g, q1_ref_actor(g, e->map->pending.addon.chain));
    if (previous && previous->map && previous->map->kind == Q1_MAP_ADDON_EXPLOSION &&
        q1_classnamed(g, previous->id, "spawned_explosion") && !q1_remove(g, previous, error))
        return false;
    e = control(g, id);
    if (!e)
        return true;
    q1_actor *child;
    if (!q1_create(g, "spawned_explosion", Q1_MAP, id, &child, error))
        return false;
    qa_actor_id child_id = child->id;
    q1_map_state *state = q1_map_allocate(g, child, error);
    if (!state)
        goto fail;
    state->kind = Q1_MAP_ADDON_EXPLOSION;
    state->use_enabled = true;
    e = control(g, id);
    if (!e)
        goto retire;
    e->map->pending.addon.chain = q1_ref_from(g, child_id);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        goto fail;
    if (!control(g, id) || !q1_entity(g, child_id))
        goto retire;
    if (!qa_world_body_write(g->services.world, child_id, &(qa_body_state){.origin = body.origin}, error))
        goto fail;
    if (!control(g, id))
        goto retire;
    child = q1_entity(g, child_id);
    if (child && !q1_map_addon_trigger_use(g, child, (qa_actor_id){0}, error))
        goto fail;
    e = control(g, id);
    if (!e)
        goto retire;
    if ((e->spawnflags & 4) && --e->count == 0)
        return q1_remove(g, e, error);
    return q1_map_schedule(g, e, e->wait + (double)q1_random(g) * e->map->pause_time,
                           Q1_MAP_ADDON_EXPLOSION_REPEAT, error);
fail:
    if (qa_actors_get(qa_session_actors(g->services.session), child_id))
        (void)qa_session_release(g->services.session, child_id, NULL);
    return false;
retire:
    return !qa_actors_get(qa_session_actors(g->services.session), child_id) ||
           qa_session_release(g->services.session, child_id, error);
}
bool q1_map_addon_control_spawn(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    q1_map_kind kind = e->map->kind;
    bool touch = kind == Q1_MAP_ADDON_LORE || kind == Q1_MAP_ADDON_HEAL ||
                 kind == Q1_MAP_ADDON_QUAD || kind == Q1_MAP_ADDON_SILENT_TELEPORT ||
                 kind == Q1_MAP_ADDON_CUTSCENE;
    bool filtered_use = kind == Q1_MAP_ADDON_DOOR_RELAY || kind == Q1_MAP_ADDON_DOOR_GROUP ||
                        kind == Q1_MAP_ADDON_MUSIC;
    if ((touch || filtered_use) &&
        ((g->options.coop ? filtered_use && (e->spawnflags & 131072u)
                         : (e->spawnflags & 32768u)) ||
         (e->spawnflags & (262144u << qa_q1_mg3_rune_count(*g->maps->options.server_flags)))))
        return q1_remove(g, e, error);
    switch (kind) {
    case Q1_MAP_ADDON_DOOR_RELAY:
    case Q1_MAP_ADDON_DOOR_GROUP:
        if (!q1_map_text(g, e->target) ||
            (kind == Q1_MAP_ADDON_DOOR_GROUP && !q1_map_text(g, e->map->category)))
            return q1_remove(g, e, error);
        e->map->use_enabled = true;
        return true;
    case Q1_MAP_ADDON_MUSIC: {
        q1_actor *world = q1_entity(g, g->maps->world_actor);
        e->map->sounds = world && world->map ? world->map->sounds : 0;
        if (!q1_map_text(g, e->targetname))
            return q1_remove(g, e, error);
        if (!e->map->style)
            e->map->style = 3;
        e->map->use_enabled = true;
        return true;
    }
    case Q1_MAP_ADDON_SKILL:
        e->map->use_enabled = true;
        return true;
    case Q1_MAP_ADDON_EXPLOSION_REPEATER:
        e->wait = e->wait != 0 ? e->wait : .8f;
        e->map->use_enabled = true;
        return true;
    case Q1_MAP_ADDON_LORE: {
        const char *map = qa_strings_cstr(qa_session_strings(g->services.session), g->maps->options.current_map);
        const char *message = qa_strings_cstr(qa_session_strings(g->services.session), e->message);
        uint32_t flags = *g->maps->options.server_flags;
        if (map && message && !strcmp(map, "map4") &&
            !strcmp(message, "$mg3_hint_bloody_nightmare_new") && (flags & 448u))
            return q1_remove(g, e, error);
        if (map && message && !strcmp(map, "hub"))
            for (unsigned i = 0; i < 3; ++i) {
                char original[40], completed[48];
                snprintf(original, sizeof(original), "$mg3_hub_rune%u_hint", i + 1);
                if ((flags & (1u << i)) && !strcmp(message, original)) {
                    snprintf(completed, sizeof(completed), "$mg3_hub_rune%u_hint_complete", i + 1);
                    if (!qa_builtin_resource(&g->services, completed, &e->message, error))
                        return false;
                    break;
                }
            }
        break;
    }
    case Q1_MAP_ADDON_HEAL:
        e->damage = e->damage != 0 ? e->damage : 1;
        e->wait = e->wait != 0 ? e->wait : .1f;
        break;
    case Q1_MAP_ADDON_SILENT_TELEPORT:
        e->map->height = e->map->height != 0 ? e->map->height : -2048;
        break;
    default:
        break;
    }
    if (!q1_map_trigger_init(g, e, true, error))
        return false;
    e = control(g, id);
    if (e)
        e->map->touch_enabled = true;
    return !e || q1_link(g, e, error);
}
bool q1_map_addon_control_use(qa_q1_game *g, q1_actor *e, qa_actor_id activator, qa_error *error) {
    switch (e->map->kind) {
    case Q1_MAP_ADDON_DOOR_RELAY: return door_relay(g, e, activator, error);
    case Q1_MAP_ADDON_DOOR_GROUP: return door_group(g, e, activator, error);
    case Q1_MAP_ADDON_MUSIC: return music(g, e, error);
    case Q1_MAP_ADDON_SKILL: return set_skill(g, e, activator, error);
    case Q1_MAP_ADDON_EXPLOSION_REPEATER: return repeat_explosion(g, e, error);
    default: return true;
    }
}
static bool silent_teleport(qa_q1_game *g, q1_actor *e, qa_actor_id other, qa_error *error) {
    qa_actor_id id = e->id;
    float height = e->map->height;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, other, &body, error))
        return false;
    if (!control(g, id) || !q1_alive(g, other))
        return true;
    qa_vec3 destination = body.origin;
    destination.z += height + (height < 0 ? 1 : 0);
    if (!qa_vec_finite(destination))
        return q1_map_fail(error, "Q1 silent teleport destination exceeds native coordinates");
    qa_actor_id death;
    if (!q1_spawn_teledeath(g, destination, other, .2, false, &death, error))
        return false;
    qa_builtin_snapshot_frame *victims;
    if (!q1_snapshot_actors(g, &victims, error))
        return false;
    qa_bounds box = {qa_vec_add(destination, qa_vec_sub(body.bounds.mins, qa_v3(1, 1, 1))),
                     qa_vec_add(destination, qa_vec_add(body.bounds.maxs, qa_v3(1, 1, 1)))};
    bool ok = true;
    for (size_t i = 0; ok && i < victims->snapshot.count && control(g, id) && q1_alive(g, death); ++i) {
        qa_actor_id victim = victims->snapshot.ids[i];
        if (!q1_alive(g, victim))
            continue;
        qa_body_state state;
        if (!qa_world_body_read(g->services.world, victim, &state, error)) {
            ok = false;
            break;
        }
        q1_actor *helper = q1_entity(g, death);
        qa_bounds bounds = {qa_vec_add(state.origin, state.bounds.mins),
                            qa_vec_add(state.origin, state.bounds.maxs)};
        if (control(g, id) && helper && q1_alive(g, victim) && qa_bounds_overlap(box, bounds))
            ok = q1_teledeath_touch(g, helper, victim, error);
    }
    qa_builtin_snapshot_release(victims);
    if (!ok || !control(g, id) || !q1_alive(g, other))
        return ok;
    body.origin = destination;
    if (!qa_world_body_write(g->services.world, other, &body, error))
        return false;
    if (!control(g, id) || !q1_alive(g, other))
        return true;
    if (g->services.motion_changed &&
        !g->services.motion_changed(g->services.context, other,
              &(qa_builtin_motion_change){.reason = QA_BUILTIN_MOTION_TELEPORT, .body = body}, error))
        return false;
    return !q1_alive(g, other) || qa_world_link(g->services.world, other, NULL, error);
}
static bool cutscene(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    e->map->touch_enabled = false;
    qa_target_cursor cursor = {0};
    qa_actor_id camera;
    if (!qa_targets_next_authored(g->maps->options.targets, "info_intermission", &cursor, &camera))
        return q1_map_fail(error, "Q1 addon cutscene has no intermission camera");
    qa_body_state body;
    qa_vec3 angles = qa_v3(0, 0, 0);
    if (!qa_world_body_read(g->services.world, camera, &body, error))
        return false;
    (void)qa_targets_vector(g->maps->options.targets, camera, "mangle", &angles);
    if (!control(g, id))
        return true;
    if (!g->maps->options.control_player)
        return q1_map_fail(error, "Q1 addon cutscene requires selected player control owners");
    qa_builtin_snapshot_frame *players;
    if (!q1_snapshot_players(g, &players, error))
        return false;
    bool ok = true;
    for (size_t i = 0; ok && i < players->snapshot.count && control(g, id); ++i)
        if (q1_alive(g, players->snapshot.ids[i]))
            ok = g->maps->options.control_player(g->maps->options.context, players->snapshot.ids[i],
                                                 body.origin, angles, qa_v3(0, 0, 0), error);
    qa_builtin_snapshot_release(players);
    if (!ok || !control(g, id))
        return ok;
    qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT, .family = QA_GAME_Q1,
                              .provider = g->options.provider, .actor = id,
                              .origin = body.origin, .direction = angles, .time_ns = g->time_ns};
    return qa_builtin_resource(&g->services, "cutscene", &event.resource, error) &&
           qa_builtin_emit(&g->services, &event, error);
}
bool q1_map_addon_control_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other, qa_error *error) {
    qa_actor_id id = e->id;
    q1_map_kind kind = e->map->kind;
    if (!q1_map_player(g, other) || !control(g, id) || !q1_alive(g, other))
        return true;
    if (kind == Q1_MAP_ADDON_LORE) {
        bool admitted;
        qa_string_id message = e->message;
        if (!q1_map_addon_lore_admit(g, other, &admitted, error))
            return false;
        const char *text = qa_strings_cstr(qa_session_strings(g->services.session), message);
        return !admitted || q1_message(g, other, text ? text : "", error);
    }
    if (kind == Q1_MAP_ADDON_HEAL) {
        if (e->map->cooldown > g->time)
            return true;
        float damage = e->damage;
        double deadline = g->time + e->wait;
        float health = q1_health(g, other);
        qa_builtin_actor_traits traits;
        bool found = g->services.actor_traits &&
                     g->services.actor_traits(g->services.context, other, &traits);
        if (!control(g, id) || !q1_alive(g, other))
            return true;
        if (found && health > 0 && health < traits.max_health &&
            !qa_combat_set_health(g->services.combat, other, fminf(health + damage, traits.max_health), error))
            return false;
        e = control(g, id);
        return !e || source_deadline(deadline, &e->map->cooldown, error);
    }
    if (kind == Q1_MAP_ADDON_QUAD) {
        q1_player *player = q1_player_get(g, other);
        double expires = player ? player->power_expires[QA_Q1_QUAD] : 0;
        if (g->services.powerups) {
            qa_builtin_powerups view;
            if (!g->services.powerups(g->services.context, g->options.provider, other, &view, error))
                return false;
            expires = (double)view.quad_until_ns / 1000000000.0;
        }
        if (!control(g, id) || !q1_alive(g, other))
            return true;
        if (expires <= g->time && !q1_sound(g, other, "items/damage.wav", 3, 1, error))
            return false;
        if (!control(g, id) || !q1_alive(g, other))
            return true;
        double until;
        if (!source_deadline(g->time + .1, &until, error)) return false;
        if (!q1_map_addon_quad_mark(g, other, error))
            return false;
        if (g->maps->options.grant_quad) {
            if (!g->maps->options.grant_quad(g->maps->options.context, other, until, error))
                return false;
        } else if (q1_player_get(g, other)) {
            if (!qa_q1_player_power(g, other, QA_Q1_QUAD, until, error))
                return false;
        } else
            return q1_map_fail(error, "Q1 quad trigger requires the selected effect owner");
        e = control(g, id);
        return !e || q1_link(g, e, error);
    }
    if (kind == Q1_MAP_ADDON_SILENT_TELEPORT)
        return silent_teleport(g, e, other, error);
    if (kind == Q1_MAP_ADDON_CUTSCENE)
        return cutscene(g, e, error);
    return true;
}
bool q1_map_addon_control_think(qa_q1_game *g, q1_actor *e, qa_error *error) {
    return repeat_explosion(g, e, error);
}
