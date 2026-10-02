#include "internal.h"
#include "qa/modes_map.h"

static bool named(const char *name, const char *a, const char *b, const char *c) {
    return !strcmp(name, a) || (b && !strcmp(name, b)) || (c && !strcmp(name, c));
}
bool qa_modes_classify_entity(qa_modes *m, qa_mode_id id, const qa_mode_map_entity *entity,
                              qa_mode_map_admission *out, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !entity || !entity->classname || !out || !qa_vec_finite(entity->origin) ||
        !qa_vec_finite(entity->angles))
        return mode_fail(e, "invalid mode map entity");
    const char *name = entity->classname;
    *out = (qa_mode_map_admission){0};
    qa_mode_object_spec *spec = &out->object;
    *spec = (qa_mode_object_spec){.actor = entity->actor,
                                  .origin = entity->origin,
                                  .angles = entity->angles,
                                  .bounds = entity->bounds,
                                  .target = entity->target,
                                  .message = entity->message,
                                  .flags = entity->spawnflags,
                                  .authored = true,
                                  .retain_body = entity->inline_model,
                                  .has_bounds = entity->inline_model};
    int team = -1;
    if (named(name, "item_flag_team1", "team_CTF_redflag", "info_flag_red"))
        team = 0;
    else if (named(name, "item_flag_team2", "team_CTF_blueflag", "info_flag_blue"))
        team = 1;
    else if (named(name, "item_flag", "team_CTF_neutralflag", NULL))
        team = 2;
    if (team >= 0 && v->value.rules.kind >= QA_MODE_CTF &&
        v->value.rules.kind <= QA_MODE_HARVESTER) {
        out->role = QA_MODE_MAP_OBJECT;
        spec->kind =
            v->value.rules.kind >= QA_MODE_OVERLOAD ? QA_MODE_OBJECT_OBELISK : QA_MODE_OBJECT_FLAG;
        spec->team = team < 2 ? v->value.rules.teams[team] : 0;
        return true;
    }
    if (named(name, "team_redobelisk", "team_blueobelisk", "team_neutralobelisk") &&
        (v->value.rules.kind == QA_MODE_OVERLOAD || v->value.rules.kind == QA_MODE_HARVESTER)) {
        out->role = QA_MODE_MAP_OBJECT;
        spec->kind = QA_MODE_OBJECT_OBELISK;
        spec->team = !strcmp(name, "team_redobelisk")    ? v->value.rules.teams[0]
                     : !strcmp(name, "team_blueobelisk") ? v->value.rules.teams[1]
                                                         : 0;
        return true;
    }
    if (v->value.rules.kind == QA_MODE_TAG &&
        named(name, "dm_tag_token", "dmatch_tag_token", NULL)) {
        out->role = QA_MODE_MAP_OBJECT;
        spec->kind = QA_MODE_OBJECT_TAG;
        return true;
    }
    if (v->value.rules.kind == QA_MODE_DEATHBALL) {
        if (named(name, "dm_dball_ball", "dm_dball_goal", "dm_dball_speed_change")) {
            out->role = QA_MODE_MAP_OBJECT;
            spec->kind = !strcmp(name, "dm_dball_ball")   ? QA_MODE_OBJECT_BALL
                         : !strcmp(name, "dm_dball_goal") ? QA_MODE_OBJECT_GOAL
                                                          : QA_MODE_OBJECT_SPEED;
            spec->value = spec->kind == QA_MODE_OBJECT_GOAL ? entity->wait : entity->speed;
            if (spec->kind == QA_MODE_OBJECT_GOAL)
                spec->team = v->value.rules.teams[entity->spawnflags & 1u ? 0 : 1];
            qa_builtin_angle_vectors(entity->angles, &spec->direction, NULL, NULL);
            return true;
        }
    }
    if (!strcmp(name, "target_location")) {
        out->role = QA_MODE_MAP_OBJECT;
        spec->kind = QA_MODE_OBJECT_LOCATION;
        spec->value = (float)entity->count;
        return true;
    }
    if (v->value.rules.kind == QA_MODE_HORDE) {
        if (!strcmp(name, "horde_manager")) {
            out->role = QA_MODE_MAP_HORDE_MANAGER;
            return true;
        }
        static const char *points[] = {
            "info_monster_start",      "info_monster_start_ranged", "info_monster_start_flying",
            "info_monster_start_boss", "info_horde_ammo",           "info_horde_item",
            "info_horde_key"};
        for (int i = 0; i < 7; ++i)
            if (!strcmp(name, points[i])) {
                out->role = QA_MODE_MAP_HORDE_POINT;
                out->horde = (qa_horde_point){.actor = entity->actor,
                                              .kind = (qa_horde_point_kind)i,
                                              .origin = entity->origin,
                                              .angles = entity->angles,
                                              .flags = entity->spawnflags,
                                              .target = entity->target};
                return true;
            }
    }
    team = -1;
    if (named(name, "info_player_team1", "team_CTF_redplayer", "team_CTF_redspawn") ||
        named(name, "info_player_red", "dm_dball_team1_start", NULL))
        team = 0;
    else if (named(name, "info_player_team2", "team_CTF_blueplayer", "team_CTF_bluespawn") ||
             named(name, "info_player_blue", "dm_dball_team2_start", NULL))
        team = 1;
    bool player = team >= 0 ||
                  named(name, "info_player_deathmatch", "info_player_start", "info_player_coop") ||
                  named(name, "info_player_start2", "testplayerstart", "info_vote_destination");
    if (player ||
        (v->value.rules.kind == QA_MODE_DEATHBALL && !strcmp(name, "dm_dball_ball_start"))) {
        out->role = QA_MODE_MAP_SPAWN;
        out->spawn = (qa_mode_spawnpoint){.actor = entity->actor,
                                          .origin = entity->origin,
                                          .angles = entity->angles,
                                          .team = team >= 0 ? v->value.rules.teams[team] : 0,
                                          .flags = entity->spawnflags,
                                          .no_bots = entity->no_bots,
                                          .no_humans = entity->no_humans};
        return qa_builtin_resource(&m->options.services, name, &out->spawn.classname, e);
    }
    return true;
}
static bool planned(qa_mode_map_admission *out, size_t capacity, size_t *count,
                    qa_mode_map_admission record, qa_error *e) {
    if (out && *count >= capacity)
        return mode_fail(e, "mode map plan output too small");
    if (out)
        out[*count] = record;
    ++*count;
    return true;
}
bool qa_modes_plan_map(qa_modes *m, qa_mode_id id, const qa_mode_map_admission *map, size_t n,
                       bool generate, qa_mode_map_admission *out, size_t capacity, size_t *count,
                       uint32_t *missing, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || (n && !map) || (capacity && !out) || !count || !missing)
        return mode_fail(e, "invalid objective plan");
    uint32_t required = QA_MODE_MAP_PLAYER_SPAWN, present = 0;
    qa_mode_kind kind = v->value.rules.kind;
    if (kind == QA_MODE_CTF || kind == QA_MODE_OVERLOAD)
        required |= QA_MODE_MAP_RED | QA_MODE_MAP_BLUE;
    else if (kind == QA_MODE_ONE_FLAG || kind == QA_MODE_HARVESTER)
        required |= QA_MODE_MAP_RED | QA_MODE_MAP_BLUE | QA_MODE_MAP_NEUTRAL;
    else if (kind == QA_MODE_HORDE)
        required |= QA_MODE_MAP_HORDE_CONTROLLER | QA_MODE_MAP_MONSTER_SPAWN;
    else if (kind == QA_MODE_DEATHBALL)
        required |= QA_MODE_MAP_BALL | QA_MODE_MAP_RED_GOAL | QA_MODE_MAP_BLUE_GOAL |
                    QA_MODE_MAP_BALL_SPAWN;
    else if (kind == QA_MODE_TAG)
        required |= QA_MODE_MAP_TAG;
    if (v->value.rules.source == QA_MODE_LMCTF && (v->value.rules.flags & 256u))
        required = QA_MODE_MAP_PLAYER_SPAWN;
    const qa_mode_spawnpoint *first = NULL, *red = NULL, *blue = NULL, *neutral = NULL;
    bool horde_loot[3] = {0};
    bool ordinary_spawn = false, team_spawn[2] = {0};
    for (size_t i = 0; i < n; ++i) {
        const qa_mode_map_admission *a = &map[i];
        if (a->role == QA_MODE_MAP_SPAWN) {
            const char *name = qa_strings_cstr(qa_session_strings(m->options.services.session),
                                               a->spawn.classname);
            if (name && !strcmp(name, "dm_dball_ball_start")) {
                present |= QA_MODE_MAP_BALL_SPAWN;
                continue;
            }
            if (name && !strcmp(name, "info_player_deathmatch"))
                ordinary_spawn = true;
            for (unsigned team = 0; team < 2; ++team)
                if (a->spawn.team && a->spawn.team == v->value.rules.teams[team])
                    team_spawn[team] = true;
            present |= QA_MODE_MAP_PLAYER_SPAWN;
            if (!first)
                first = &a->spawn;
            if (a->spawn.team == v->value.rules.teams[0] && a->spawn.team && !red)
                red = &a->spawn;
            if (a->spawn.team == v->value.rules.teams[1] && a->spawn.team && !blue)
                blue = &a->spawn;
        } else if (a->role == QA_MODE_MAP_HORDE_MANAGER)
            present |= QA_MODE_MAP_HORDE_CONTROLLER;
        else if (a->role == QA_MODE_MAP_HORDE_POINT) {
            if (a->horde.kind <= QA_HORDE_BOSS)
                present |= QA_MODE_MAP_MONSTER_SPAWN;
            else if (a->horde.kind <= QA_HORDE_KEY)
                horde_loot[a->horde.kind - QA_HORDE_AMMO] = true;
        } else if (a->role == QA_MODE_MAP_OBJECT) {
            const qa_mode_object_spec *s = &a->object;
            int team = mode_team_index(v, s->team);
            if (s->kind == QA_MODE_OBJECT_FLAG || s->kind == QA_MODE_OBJECT_OBELISK)
                present |= team == 0   ? QA_MODE_MAP_RED
                           : team == 1 ? QA_MODE_MAP_BLUE
                                       : QA_MODE_MAP_NEUTRAL;
            else if (s->kind == QA_MODE_OBJECT_BALL)
                present |= QA_MODE_MAP_BALL;
            else if (s->kind == QA_MODE_OBJECT_TAG)
                present |= QA_MODE_MAP_TAG;
            else if (s->kind == QA_MODE_OBJECT_GOAL)
                present |= team == 0 ? QA_MODE_MAP_RED_GOAL : QA_MODE_MAP_BLUE_GOAL;
        }
    }
    *missing = required & ~present;
    *count = 0;
    if (!generate || !first)
        return true;
    if (!red)
        red = first;
    if (!blue) {
        float far = -1;
        for (size_t i = 0; i < n; ++i)
            if (map[i].role == QA_MODE_MAP_SPAWN) {
                qa_vec3 d = qa_vec_sub(map[i].spawn.origin, red->origin);
                float distance = qa_vec_dot(d, d);
                if (distance > far) {
                    far = distance;
                    blue = &map[i].spawn;
                }
            }
    }
    bool separated = blue && qa_vec_length(qa_vec_sub(red->origin, blue->origin)) >= 64;
    if (!blue)
        blue = red;
    qa_vec3 midpoint = qa_vec_scale(qa_vec_add(red->origin, blue->origin), .5f);
    float near = INFINITY;
    for (size_t i = 0; i < n; ++i)
        if (map[i].role == QA_MODE_MAP_SPAWN) {
            float distance = qa_vec_length(qa_vec_sub(map[i].spawn.origin, midpoint));
            if (distance < near) {
                near = distance;
                neutral = &map[i].spawn;
            }
        }
    const qa_mode_spawnpoint *points[] = {red, blue, neutral};
    for (size_t i = 0; i < n; ++i)
        if (!ordinary_spawn && map[i].role == QA_MODE_MAP_SPAWN) {
            const qa_mode_spawnpoint *source = &map[i].spawn;
            const char *name =
                qa_strings_cstr(qa_session_strings(m->options.services.session), source->classname);
            if (name && !strcmp(name, "dm_dball_ball_start"))
                continue;
            qa_mode_map_admission generated = {.role = QA_MODE_MAP_SPAWN, .spawn = *source};
            generated.spawn.actor = (qa_actor_id){0};
            generated.spawn.team = 0;
            if (!qa_builtin_resource(&m->options.services, "info_player_deathmatch",
                                     &generated.spawn.classname, e) ||
                !planned(out, capacity, count, generated, e))
                return false;
        }
    if (separated && ((kind >= QA_MODE_TEAM_DEATHMATCH && kind <= QA_MODE_HARVESTER) ||
                      kind == QA_MODE_DEATHBALL)) {
        for (unsigned team = 0; team < 2; ++team)
            if (!team_spawn[team]) {
                unsigned variants = v->value.rules.source >= QA_MODE_Q3 ? 2 : 1;
                for (unsigned variant = 0; variant < variants; ++variant) {
                    qa_mode_map_admission generated = {
                        .role = QA_MODE_MAP_SPAWN,
                        .spawn = {.origin = points[team]->origin,
                                  .angles = points[team]->angles,
                                  .team = v->value.rules.teams[team]}};
                    const char *name =
                        kind == QA_MODE_DEATHBALL
                            ? (team ? "dm_dball_team2_start" : "dm_dball_team1_start")
                        : v->value.rules.source >= QA_MODE_Q3
                            ? (team ? (variant ? "team_CTF_bluespawn" : "team_CTF_blueplayer")
                                    : (variant ? "team_CTF_redspawn" : "team_CTF_redplayer"))
                        : team ? "info_player_team2"
                               : "info_player_team1";
                    if (!qa_builtin_resource(&m->options.services, name, &generated.spawn.classname,
                                             e) ||
                        !planned(out, capacity, count, generated, e))
                        return false;
                }
            }
    }
    for (unsigned i = 0; i < 3; ++i)
        if (separated && (*missing & (1u << i))) {
            if (!points[i])
                continue;
            qa_mode_map_admission generated = {
                .role = QA_MODE_MAP_OBJECT,
                .object = {.kind = kind == QA_MODE_OVERLOAD || kind == QA_MODE_HARVESTER
                                       ? QA_MODE_OBJECT_OBELISK
                                       : QA_MODE_OBJECT_FLAG,
                           .team = i < 2 ? v->value.rules.teams[i] : 0,
                           .origin = points[i]->origin,
                           .angles = points[i]->angles}};
            if (!planned(out, capacity, count, generated, e))
                return false;
            *missing &= ~(1u << i);
        }
    if (kind == QA_MODE_DEATHBALL) {
        if (*missing & QA_MODE_MAP_BALL) {
            qa_mode_map_admission generated = {
                .role = QA_MODE_MAP_OBJECT,
                .object = {.kind = QA_MODE_OBJECT_BALL, .origin = neutral->origin}};
            if (!planned(out, capacity, count, generated, e))
                return false;
            *missing &= ~(uint32_t)QA_MODE_MAP_BALL;
        }
        if (*missing & QA_MODE_MAP_BALL_SPAWN) {
            qa_mode_map_admission generated = {
                .role = QA_MODE_MAP_SPAWN,
                .spawn = {.origin = neutral->origin, .angles = neutral->angles}};
            if (!qa_builtin_resource(&m->options.services, "dm_dball_ball_start",
                                     &generated.spawn.classname, e) ||
                !planned(out, capacity, count, generated, e))
                return false;
            *missing &= ~(uint32_t)QA_MODE_MAP_BALL_SPAWN;
        }
        for (unsigned i = 0; i < 2; ++i)
            if (separated && (*missing & (QA_MODE_MAP_RED_GOAL << i))) {
                qa_mode_map_admission generated = {
                    .role = QA_MODE_MAP_OBJECT,
                    .object = {.kind = QA_MODE_OBJECT_GOAL,
                               .origin = points[i]->origin,
                               .angles = points[i]->angles,
                               .team = v->value.rules.teams[i],
                               .has_bounds = true,
                               .bounds = {{-32, -64, -24}, {32, 64, 64}},
                               .value = 10}};
                if (!planned(out, capacity, count, generated, e))
                    return false;
                *missing &= ~((uint32_t)QA_MODE_MAP_RED_GOAL << i);
            }
    }
    if (*missing & QA_MODE_MAP_TAG) {
        qa_mode_map_admission generated = {
            .role = QA_MODE_MAP_OBJECT,
            .object = {.kind = QA_MODE_OBJECT_TAG, .origin = neutral->origin}};
        if (!planned(out, capacity, count, generated, e))
            return false;
        *missing &= ~(uint32_t)QA_MODE_MAP_TAG;
    }
    if (kind == QA_MODE_HORDE) {
        if (*missing & QA_MODE_MAP_HORDE_CONTROLLER) {
            qa_mode_map_admission generated = {.role = QA_MODE_MAP_HORDE_MANAGER,
                                               .object = {.origin = first->origin}};
            if (!qa_builtin_resource(&m->options.services, "horde_event", &generated.object.target,
                                     e) ||
                !planned(out, capacity, count, generated, e))
                return false;
            *missing &= ~(uint32_t)QA_MODE_MAP_HORDE_CONTROLLER;
        }
        if ((*missing & QA_MODE_MAP_MONSTER_SPAWN) && separated) {
            for (size_t i = 0; i < n; ++i)
                if (map[i].role == QA_MODE_MAP_SPAWN &&
                    qa_vec_length(qa_vec_sub(map[i].spawn.origin, first->origin)) >= 64) {
                    qa_mode_map_admission generated = {.role = QA_MODE_MAP_HORDE_POINT,
                                                       .horde = {.kind = QA_HORDE_NORMAL,
                                                                 .origin = map[i].spawn.origin,
                                                                 .angles = map[i].spawn.angles}};
                    if (!planned(out, capacity, count, generated, e))
                        return false;
                }
            *missing &= ~(uint32_t)QA_MODE_MAP_MONSTER_SPAWN;
        }
        for (unsigned i = 0; i < 3; ++i)
            if (!horde_loot[i]) {
                qa_mode_map_admission generated = {
                    .role = QA_MODE_MAP_HORDE_POINT,
                    .horde = {.kind = (qa_horde_point_kind)(QA_HORDE_AMMO + i),
                              .origin = points[i]->origin,
                              .angles = points[i]->angles}};
                if (!planned(out, capacity, count, generated, e))
                    return false;
            }
    }
    return true;
}
bool qa_modes_start_relics(qa_modes *m, qa_mode_id id, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v)
        return mode_fail(e, "unknown relic mode");
    if (v->value.rules.source != QA_MODE_THREEWAVE && v->value.rules.source != QA_MODE_Q2_CTF &&
        v->value.rules.source != QA_MODE_LMCTF && v->value.rules.source != QA_MODE_ROGUE)
        return true;
    if (!v->value.rules.relics || v->relics_started || v->value.rules.start_map)
        return true;
    /* Rogue's world flag is committed by its first actual player frame. */
    if (v->value.rules.source == QA_MODE_ROGUE) return true;
    v->relics_started = true;
    if (v->value.rules.source == QA_MODE_THREEWAVE || v->value.rules.source == QA_MODE_Q2_CTF) {
        v->relic_spawn_ns =
            v->value.time_ns +
            (v->value.rules.source == QA_MODE_THREEWAVE ? MODE_SECOND / 10 : 2 * MODE_SECOND);
        return true;
    }
    return mode_relic_spawn_all(m, v, e);
}
bool mode_relic_spawn_all(qa_modes *m, mode_instance *v, qa_error *e) {
    qa_mode_id id = v->id;
    if (!qa_builtin_observations(&m->options.services, &m->observations, e))
        return false;
    if (v->value.rules.source == QA_MODE_THREEWAVE) {
        unsigned advances = (unsigned)ceilf(mode_random_float(m) * 10);
        v->rune_cursor = advances ? advances - 1 : SIZE_MAX;
    }
    int count = v->value.rules.source == QA_MODE_LMCTF ? 5 : 4;
    static const qa_relic_kind lm_order[] = {QA_RELIC_STRENGTH, QA_RELIC_HASTE, QA_RELIC_RESISTANCE,
                                             QA_RELIC_REGENERATION, QA_RELIC_VAMPIRE};
    for (int ordinal = 0; ordinal < count; ++ordinal) {
        int i = v->value.rules.source == QA_MODE_LMCTF ? (int)lm_order[ordinal] : ordinal;
        int bit = i == QA_RELIC_STRENGTH ? 1 : i == QA_RELIC_RESISTANCE ? 2 : 1 << i;
        if (v->value.rules.source == QA_MODE_LMCTF && !(v->value.rules.rune_mask & bit))
            continue;
        bool exists = false;
        for (uint32_t j = 0; j < m->actor_capacity; ++j)
            if (m->objects[j].active && m->objects[j].mode.slot == id.slot &&
                m->objects[j].mode.generation == id.generation &&
                m->objects[j].spec.kind == QA_MODE_OBJECT_RELIC &&
                m->objects[j].spec.relic == (qa_relic_kind)i)
                exists = true;
        if (exists)
            continue;
        qa_mode_object_spec spec = {
            .kind = QA_MODE_OBJECT_RELIC, .relic = (qa_relic_kind)i, .suspended = true};
        qa_actor_id actor;
        if (!qa_modes_spawn_object(m, id, &spec, &actor, e))
            return false;
        mode_object *o = mode_object_get(m, actor);
        if (!o || !mode_relic_place(m, v, o, true, e)) {
            if (mode_live(m, actor)) qa_session_release(m->options.services.session, actor, NULL);
            return false;
        }
        if (!qa_builtin_observations(&m->options.services, &m->observations, e))
            return false;
    }
    for (size_t i = 0; i < m->players_order.count; ++i)
        if (mode_member_get(m, v, m->players_order.ids[i]) &&
            !qa_modes_publish_items(m, m->players_order.ids[i], e))
            return false;
    return true;
}
