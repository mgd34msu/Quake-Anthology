#include "internal.h"

static bool short_text(qa_q2_game *g, qa_string_id text, qa_string_id *out, qa_error *e) {
    const char *source = qa_strings_cstr(qa_session_strings(g->services.session), text);
    if (!source)
        source = "";
    size_t size = strlen(source);
    if (size > 511)
        size = 511;
    char copy[512];
    memcpy(copy, source, size);
    copy[size] = 0;
    return qa_builtin_resource(&g->services, copy, out, e);
}
bool q2_rerelease_goal_use(qa_q2_game *g, q2_actor *a, qa_actor_id activator, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_entities *r = g->entity_runtime;
    if (s->kind == Q2E_HELP) {
        qa_string_id *text = (s->spawnflags & 1) ? &r->primary : &r->secondary;
        uint32_t *changes = (s->spawnflags & 1) ? &r->primary_changes : &r->secondary_changes;
        if (*text != s->message) {
            if (!short_text(g, s->message, text, e))
                return false;
            (*changes)++;
            if (!q2_map_event(g,
                              &(qa_q2_map_event){.kind = QA_Q2_MAP_HELP,
                                                 .actor = a->id,
                                                 .slot = (s->spawnflags & 1) ? 1 : 2,
                                                 .text = s->message},
                              e))
                return false;
        }
        return !q2_actor_live(g, a->id) || !(s->spawnflags & 2) ||
               q2_rerelease_poi(g, a, activator, e);
    }
    const char *noise = q2_field_text(g, s, "noise");
    if (!q2_entity_sound(g, a, *noise ? noise : "misc/secret.wav", 2, 1, 1, 0, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (s->kind == Q2E_SECRET)
        r->found_secrets++;
    else {
        r->found_goals++;
        if (r->found_goals == r->total_goals && !(s->spawnflags & 1)) {
            char number[32];
            float value = q2_field_float(g, s, "sounds", 0);
            snprintf(number, sizeof(number), "%.9g", value);
            qa_string_id track;
            if (!qa_builtin_resource(&g->services, number, &track, e) ||
                !q2_map_event(
                    g,
                    &(qa_q2_map_event){.kind = QA_Q2_MAP_MUSIC, .actor = a->id, .resource = track},
                    e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
        }
        if (r->has_goals) {
            r->goal_number++;
            r->primary_changes++;
            qa_builtin_snapshot_frame *players = q2_player_roster(g, e);
            if (!players)
                return false;
            bool okay = true;
            for (size_t i = 0; i < players->snapshot.count; i++) {
                q2_actor *player = q2_actor_get(g, players->snapshot.ids[i], false, NULL);
                if (player && player->client && !q2_rerelease_notify(g, player, e)) {
                    okay = false;
                    break;
                }
                if (!q2_actor_live(g, a->id))
                    break;
            }
            qa_builtin_snapshot_release(players);
            if (!okay)
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
        }
    }
    if (!q2_map_event(
            g,
            &(qa_q2_map_event){.kind = s->kind == Q2E_SECRET ? QA_Q2_MAP_SECRET : QA_Q2_MAP_GOAL,
                               .actor = a->id,
                               .recipient = activator,
                               .count = 1},
            e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (!q2_entity_targets(g, a, activator, false, e))
        return false;
    return !q2_actor_live(g, a->id) || qa_session_release(g->services.session, a->id, e);
}
static bool objective(qa_q2_game *g, q2_actor *a, qa_string_id text, unsigned slot, bool talk,
                      qa_error *e) {
    qa_builtin_message_arg argument = {.kind = QA_BUILTIN_MESSAGE_STRING, .value.text = text};
    if (!talk && !qa_builtin_resource(&g->services,
                                      slot == 1 ? "$g_primary_mission_objective"
                                                : "$g_secondary_mission_objective",
                                      &text, e))
        return false;
    return q2_map_event(g,
                        &(qa_q2_map_event){.kind = QA_Q2_MAP_MISSION_OBJECTIVE,
                                           .recipient = a->id,
                                           .text = text,
                                           .slot = (int)slot,
                                           .flags = talk ? 1u : 0u,
                                           .arguments = talk ? NULL : &argument,
                                           .argument_count = talk ? 0 : 1,
                                           .visible = true},
                        e);
}
bool q2_rerelease_notify(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entities *r = g->entity_runtime;
    q2_client_state *s = a->client;
    if (g->options.deathmatch || !s || !s->info.connected || !s->spawned ||
        g->now_ns < q2_deadline(s->entered_ns, 300 * Q2_MS))
        return true;
    if (r->has_goals) {
        if (r->primary_changes != r->secondary_changes) {
            const char *goal = qa_strings_cstr(qa_session_strings(g->services.session), r->goals);
            for (unsigned i = 0; i < r->goal_number; i++) {
                const char *next = strchr(goal, '\t');
                if (!next) {
                    qa_error_set(e, QA_ERROR_FORMAT, 0,
                                 "Quake 64 goal index exceeds authored goal list");
                    return false;
                }
                goal = next + 1;
            }
            size_t size = strcspn(goal, "\t");
            if (size > 511)
                size = 511;
            char copy[512];
            memcpy(copy, goal, size);
            copy[size] = 0;
            if (!qa_builtin_resource(&g->services, copy, &r->primary, e))
                return false;
            r->secondary_changes = r->primary_changes;
        }
        if (s->mission_primary != r->primary_changes) {
            if (!objective(g, a, r->primary, 1, true, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            s->mission_primary = r->primary_changes;
        }
    } else {
        if (s->mission_primary != r->primary_changes) {
            s->mission_primary = r->primary_changes;
            s->mission_changed = 1;
            s->mission_time_ns = q2_deadline(g->now_ns, 5 * Q2_NS);
            if (r->primary && !objective(g, a, r->primary, 1, false, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
        }
        if (s->mission_secondary != r->secondary_changes) {
            s->mission_secondary = r->secondary_changes;
            s->mission_changed = 1;
            s->mission_time_ns = q2_deadline(g->now_ns, 5 * Q2_NS);
            if (r->secondary && !objective(g, a, r->secondary, 2, false, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
        }
    }
    return true;
}
bool q2_rerelease_goal_frame(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_client_state *s = a->client;
    if (s->mission_changed && s->mission_changed <= 3 && s->mission_time_ns < g->now_ns) {
        if (s->mission_changed == 1 && !q2_entity_sound(g, a, "misc/pc_up.wav", 0, 1, 3, 0, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        s->mission_changed++;
        s->mission_time_ns = q2_deadline(g->now_ns, 5 * Q2_NS);
    }
    return q2_map_event(g,
                        &(qa_q2_map_event){.kind = QA_Q2_MAP_MISSION_STATUS,
                                           .recipient = a->id,
                                           .visible = s->mission_changed >= 1 &&
                                                      s->mission_changed <= 2 &&
                                                      (g->now_ns / Q2_MS) % 1000 < 500},
                        e);
}
bool qa_q2_player_help_computer(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    if (!a)
        return false;
    if (g->player_runtime->intermission)
        return true;
    q2_client_state *s = a->client;
    q2_entities *r = g->entity_runtime;
    s->show_inventory = s->show_scores = false;
    if (s->show_help &&
        (s->mission_primary == r->primary_changes || s->mission_secondary == r->secondary_changes))
        s->show_help = false;
    else {
        s->show_help = true;
        s->mission_changed = 0;
    }
    if (!q2_player_emit(
            g,
            &(qa_q2_player_event){.kind = QA_Q2_PLAYER_HELP, .actor = id, .visible = s->show_help},
            e))
        return false;
    return !q2_actor_live(g, id) ||
           q2_map_event(g,
                        &(qa_q2_map_event){.kind = QA_Q2_MAP_HELP_COMPUTER,
                                           .recipient = id,
                                           .text = r->primary,
                                           .resource = r->secondary,
                                           .visible = s->show_help,
                                           .flags = s->show_help ? 1u : 0u},
                        e);
}
bool qa_q2_entities_player_reset(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    if (!a)
        return false;
    if (g->options.edition != QA_Q2_RERELEASE)
        return true;
    a->client->wanted_fog = g->entity_runtime->world_fog;
    a->client->fog_transition = 0;
    a->client->spawned = !a->client->awaiting_respawn;
    qa_q2_fog fog = a->client->wanted_fog;
    if (!q2_map_event(g, &(qa_q2_map_event){.kind = QA_Q2_MAP_FOG, .recipient = id, .fog = fog}, e))
        return false;
    if (q2_actor_live(g, id))
        a->client->fog = fog;
    return true;
}
