#include "internal.h"

static const char *actor_class(qa_modes *m, qa_actor_id actor) {
    qa_builtin_actor_traits traits = {0};
    if (!m->options.services.actor_traits ||
        !m->options.services.actor_traits(m->options.services.context, actor, &traits))
        return NULL;
    return qa_strings_cstr(qa_session_strings(m->options.services.session), traits.classname);
}
static bool relic_spot(qa_modes *m, mode_instance *v, bool initial, qa_vec3 *origin, qa_error *e) {
    if (v->value.rules.source == QA_MODE_LMCTF) {
        qa_actor_id selected = {0};
        float farthest = 0;
        if (initial)
            for (size_t i = 0; i < m->observations.count; ++i) {
                qa_actor_id actor = m->observations.ids[i];
                const char *name = actor_class(m, actor);
                if (!name || strcmp(name, "item_health"))
                    continue;
                qa_body_state health;
                if (!qa_world_body_read(m->options.services.world, actor, &health, e))
                    return false;
                float nearest = 9999999;
                bool found[QA_RELIC_COUNT] = {0};
                for (size_t j = i + 1; j < m->observations.count; ++j) {
                    mode_object *rune = mode_object_get(m, m->observations.ids[j]);
                    if (!rune || rune->spec.kind != QA_MODE_OBJECT_RELIC ||
                        rune->mode.slot != v->id.slot ||
                        rune->mode.generation != v->id.generation || found[rune->spec.relic])
                        continue;
                    found[rune->spec.relic] = true;
                    qa_body_state body;
                    if (!qa_world_body_read(m->options.services.world, rune->actor, &body, e))
                        return false;
                    nearest = fminf(nearest, qa_vec_length(qa_vec_sub(health.origin, body.origin)));
                }
                if (nearest > farthest) {
                    farthest = nearest;
                    selected = actor;
                }
            }
        static const char *health_classes[] = {"item_health_small", "item_health_large",
                                               "item_health"};
        for (size_t kind = 0; !selected.registry && kind < 3; ++kind) {
            size_t count = 0;
            for (size_t i = 0; i < m->observations.count; ++i) {
                const char *name = actor_class(m, m->observations.ids[i]);
                if (name && !strcmp(name, health_classes[kind]))
                    ++count;
            }
            if (!count)
                continue;
            size_t chosen = (size_t)(mode_random_float(m) * (float)count);
            if (chosen > 20)
                chosen = 20;
            if (chosen)
                --chosen;
            for (size_t i = 0; i < m->observations.count; ++i) {
                const char *name = actor_class(m, m->observations.ids[i]);
                if (name && !strcmp(name, health_classes[kind]) && !chosen--) {
                    selected = m->observations.ids[i];
                    break;
                }
            }
        }
        if (!selected.registry)
            selected = v->bases[0];
        if (mode_live(m, selected)) {
            qa_body_state body;
            if (!qa_world_body_read(m->options.services.world, selected, &body, e))
                return false;
            *origin = body.origin;
            return true;
        }
    }
    qa_string_id classname;
    if (!qa_builtin_resource(&m->options.services, "info_player_deathmatch", &classname, e))
        return false;
    size_t count = 0;
    for (size_t i = 0; i < v->spawn_count; ++i)
        if (v->spawns[i].classname == classname)
            ++count;
    if (!count) {
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "no relic spawn point");
        return false;
    }
    size_t chosen;
    if (v->value.rules.source == QA_MODE_THREEWAVE) {
        ++v->rune_cursor;
        chosen = v->rune_cursor % count;
    } else {
        int index = -1, remaining = (int)(mode_random_float(m) * 16);
        while (remaining-- > 0)
            index = (size_t)(index + 1) == count ? -1 : index + 1;
        chosen = index < 0 ? 0 : (size_t)index;
    }
    for (size_t i = 0; i < v->spawn_count; ++i)
        if (v->spawns[i].classname == classname && !chosen--) {
            *origin = v->spawns[i].origin;
            return true;
        }
    return mode_fail(e, "relic spawn traversal changed");
}
bool mode_relic_place(qa_modes *m, mode_instance *v, mode_object *o, bool initial, qa_error *e) {
    qa_vec3 origin;
    qa_error local = {0};
    if (!relic_spot(m, v, initial, &origin, &local)) {
        if (local.code != QA_ERROR_NOT_FOUND) {
            if (e)
                *e = local;
            return false;
        }
        o->expire_ns = v->value.time_ns + 60 * MODE_SECOND;
        return initial ? mode_object_hide(m, o, true, e) : true;
    }
    qa_body_state body;
    if (!qa_world_body_read(m->options.services.world, o->actor, &body, e))
        return false;
    body.origin = origin;
    body.ground = (qa_actor_id){0};
    if (v->value.rules.source == QA_MODE_THREEWAVE) {
        body.origin.z -= 24;
        body.velocity.x = -500 + mode_random_float(m) * 1000;
        body.velocity.y = -500 + mode_random_float(m) * 1000;
        body.velocity.z = 400;
    } else if (v->value.rules.source == QA_MODE_LMCTF) {
        qa_trace_query query = {
            .start = origin,
            .end = origin,
            .shape = {QA_SHAPE_BOX, body.bounds},
            .policy = {.family = QA_COLLISION_Q2, .contents_mask = 3, .q1_hull = -1}};
        query.end.z += 48;
        qa_trace_result trace;
        if (!qa_world_trace(m->options.services.world, &query, &trace, e))
            return false;
        if (trace.fraction == 1 && !trace.all_solid)
            body.origin = query.end;
        body.velocity.x = -2000 + mode_random_float(m) * 4000;
        body.velocity.y = -2000 + mode_random_float(m) * 4000;
        body.velocity.z = 800 + mode_random_float(m) * 200;
    } else {
        body.origin.z += 16;
        float yaw = floorf(mode_random_float(m) * 360) * .01745329251994329577f;
        body.velocity = qa_v3(100 * cosf(yaw), 100 * sinf(yaw), 300);
    }
    o->value.phase = QA_OBJECTIVE_HOME;
    o->value.carrier = (qa_actor_id){0};
    o->value.previous_owner = (qa_actor_id){0};
    o->physics.motion = QA_PHYSICS_TOSS;
    o->dropped = false;
    o->value.deadline_ns = 0;
    o->expire_ns = v->value.time_ns + (v->value.rules.source == QA_MODE_THREEWAVE ? 120
                                       : v->value.rules.source == QA_MODE_LMCTF   ? 30
                                                                                  : 60) *
                                          MODE_SECOND;
    return qa_world_body_write(m->options.services.world, o->actor, &body, e) &&
           mode_object_hide(m, o, false, e);
}

bool qa_modes_has_relic(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_relic_kind kind) {
    mode_instance *v = mode_get(m, id);
    if (!v || !v->value.rules.enabled || !v->value.rules.relics)
        return false;
    mode_member *p = mode_member_get(m, v, actor);
    mode_object *o = p ? mode_object_get(m, p->relic) : NULL;
    return o && o->spec.kind == QA_MODE_OBJECT_RELIC && o->spec.relic == kind &&
           qa_actor_id_equal(o->value.carrier, actor);
}
bool mode_relic_touch(qa_modes *m, mode_instance *v, mode_object *o, qa_actor_id actor,
                      bool *accepted, qa_error *e) {
    mode_member *p = mode_member_get(m, v, actor);
    if (!p || !v->value.rules.relics)
        return true;
    if (v->value.rules.source == QA_MODE_Q2_CTF &&
        (v->value.phase == QA_MODE_SETUP || v->value.phase == QA_MODE_COUNTDOWN))
        return true;
    if (v->value.rules.source == QA_MODE_LMCTF &&
        !(v->value.rules.rune_mask & (o->spec.relic == QA_RELIC_STRENGTH     ? 1
                                      : o->spec.relic == QA_RELIC_RESISTANCE ? 2
                                                                             : 1 << o->spec.relic)))
        return true;
    if (mode_object_get(m, p->relic)) {
        if (v->value.time_ns >= p->notice_ns) {
            p->notice_ns =
                v->value.time_ns + (v->value.rules.source == QA_MODE_Q2_CTF ? 2 : 5) * MODE_SECOND;
            return mode_event(m, v, QA_MODE_MESSAGE, actor, (qa_actor_id){0}, o->actor, 0, 1,
                              QA_MODE_RELIC_TAKEN, e);
        }
        return true;
    }
    if (!mode_object_count(m, v, o, actor, 1, e))
        return false;
    p->relic = o->actor;
    o->value.carrier = actor;
    o->value.phase = QA_OBJECTIVE_CARRIED;
    if (v->value.rules.source == QA_MODE_Q2_CTF)
        p->regen_ns = v->value.time_ns;
    o->expire_ns = 0;
    o->value.deadline_ns = 0;
    o->physics.motion = QA_PHYSICS_STATIONARY;
    if (!mode_object_hide(m, o, true, e))
        return false;
    *accepted = true;
    return mode_event(m, v, QA_MODE_RELIC_TAKEN, actor, (qa_actor_id){0}, o->actor, p->last_team,
                      o->spec.relic, 0, e);
}
bool qa_modes_tech_sound(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_relic_kind kind,
                         bool quad, bool silenced, bool *handled, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!handled)
        return mode_fail(e, "missing tech sound result");
    *handled = p && qa_modes_has_relic(m, id, actor, kind);
    if (!*handled || v->value.time_ns < p->tech_sound_ns)
        return true;
    p->tech_sound_ns = v->value.time_ns + MODE_SECOND;
    const char *sound = kind == QA_RELIC_STRENGTH ? (quad ? "ctf/tech2x.wav" : "ctf/tech2.wav")
                        : kind == QA_RELIC_HASTE  ? "ctf/tech3.wav"
                        : kind == QA_RELIC_REGENERATION ? "ctf/tech4.wav"
                                                        : "ctf/tech1.wav";
    if (v->value.rules.source <= QA_MODE_Q1_HORDE)
        sound = kind == QA_RELIC_RESISTANCE ? "rune/rune1.wav"
                : kind == QA_RELIC_STRENGTH ? "rune/rune2.wav"
                                            : "rune/rune4.wav";
    if (v->value.rules.source == QA_MODE_LMCTF)
        sound = kind == QA_RELIC_HASTE          ? "player/lava1.wav"
                : kind == QA_RELIC_STRENGTH     ? "ctf/strength.wav"
                : kind == QA_RELIC_REGENERATION ? "ctf/regen.wav"
                                                : "ctf/resist.wav";
    return mode_sound(m, v, actor, sound, silenced ? .2f : 1, e);
}
bool qa_modes_attack_damage(qa_modes *m, qa_mode_id id, qa_actor_id actor, float amount, float *out,
                            qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !out || !isfinite(amount))
        return mode_fail(e, "invalid rune attack");
    *out = amount;
    if (!qa_modes_has_relic(m, id, actor, QA_RELIC_STRENGTH))
        return true;
    *out = v->value.rules.source == QA_MODE_LMCTF ? truncf(amount * 1.75f) : amount * 2;
    return true;
}
bool qa_modes_resist_damage(qa_modes *m, qa_mode_id id, qa_actor_id actor, float amount, float *out,
                            qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !out || !isfinite(amount))
        return mode_fail(e, "invalid rune resistance");
    *out = amount;
    if (!qa_modes_has_relic(m, id, actor, QA_RELIC_RESISTANCE))
        return true;
    *out = v->value.rules.source == QA_MODE_LMCTF    ? truncf(amount / 1.75f)
           : v->value.rules.source == QA_MODE_Q2_CTF ? truncf(amount * .5f)
                                                     : amount * .5f;
    if (v->value.rules.source == QA_MODE_Q2_CTF)
        return mode_sound(m, v, actor, "ctf/tech1.wav", 1, e);
    if (v->value.rules.source == QA_MODE_LMCTF)
        return mode_sound(m, v, actor, "ctf/resist.wav", 1, e);
    if (v->value.rules.source == QA_MODE_THREEWAVE) {
        bool handled;
        return qa_modes_tech_sound(m, id, actor, QA_RELIC_RESISTANCE, false, false, &handled, e);
    }
    return true;
}
bool qa_modes_after_damage(qa_modes *m, qa_mode_id id, const qa_damage_outcome *outcome,
                           qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !outcome)
        return mode_fail(e, "invalid mode damage outcome");
    qa_actor_id actor = outcome->request.attack.attacker;
    if (v->value.rules.source != QA_MODE_LMCTF || outcome->result.applied_damage <= 0 ||
        !qa_modes_has_relic(m, id, actor, QA_RELIC_VAMPIRE) || !mode_alive(m, actor))
        return true;
    qa_builtin_actor_traits traits = {0};
    if (m->options.services.actor_traits)
        m->options.services.actor_traits(m->options.services.context, outcome->request.target,
                                         &traits);
    bool body = false;
    if (traits.classname) {
        const char *name =
            qa_strings_cstr(qa_session_strings(m->options.services.session), traits.classname);
        body = name && strcmp(name, "bodyque") == 0;
    }
    if (!traits.player && !body)
        return true;
    qa_combat_state state;
    if (!qa_combat_read(m->options.services.combat, actor, &state, e))
        return false;
    float damage = outcome->result.applied_damage;
    int32_t gain = (damage >= (float)INT32_MAX ? INT32_MAX : (int32_t)damage) >> (body ? 2 : 1);
    if (state.health >= 250 || !gain)
        return true;
    return qa_combat_set_health(m->options.services.combat, actor,
                                fminf(250, state.health + (float)gain), e) &&
           mode_sound(m, v, actor, "brain/brnatck3.wav", 1, e);
}
bool mode_relic_frame(qa_modes *m, mode_instance *v, qa_actor_id actor, mode_member *p,
                      qa_error *e) {
    if (!qa_modes_has_relic(m, v->id, actor, QA_RELIC_REGENERATION) || !mode_alive(m, actor))
        return true;
    qa_combat_state state;
    if (!qa_combat_read(m->options.services.combat, actor, &state, e))
        return false;
    qa_regular_armor armor = state.armor.regular;
    uint64_t now = v->value.time_ns;
    if (v->value.rules.source == QA_MODE_LMCTF) {
        int32_t frame = (int32_t)(now / (MODE_SECOND / 10));
        float health = state.health / 5;
        int heart = health <= 5 ? 5 : health >= 25 ? 25 : (int)health;
        if ((int64_t)frame < (int64_t)p->regen_frame + heart)
            return true;
        p->regen_frame = frame;
        qa_builtin_actor_traits traits = {.max_health = 100};
        if (m->options.services.actor_traits)
            m->options.services.actor_traits(m->options.services.context, actor, &traits);
        float maximum = traits.max_health > 0 ? traits.max_health : 100;
        if (state.health < maximum + 25 &&
            !qa_combat_set_health(m->options.services.combat, actor,
                                  fminf(maximum + 25, state.health + (float)(heart / 3)), e))
            return false;
        if (armor.kind == QA_ARMOR_NONE || armor.points == 0) {
            qa_item_id jacket;
            if (!qa_builtin_resource(&m->options.services, "q2:item_armor_jacket", &jacket, e))
                return false;
            armor = (qa_regular_armor){.kind = QA_ARMOR_Q2,
                                       .points = (float)(heart / 4),
                                       .item = jacket,
                                       .protection.q2 = {.normal = .3f, .energy = 0}};
        } else
            armor.points = fminf(200, armor.points + (float)(heart / 3));
        return qa_combat_set_regular_armor(m->options.services.combat, actor, &armor, e) &&
               mode_sound(m, v, actor, "ctf/regen.wav", 1, e);
    }
    if (p->regen_ns >= now)
        return true;
    uint64_t delay = 0;
    if (state.health < 150) {
        if (!qa_combat_set_health(m->options.services.combat, actor, fminf(150, state.health + 5),
                                  e))
            return false;
        delay += MODE_SECOND / 2;
    }
    bool can_armor = armor.points > 0;
    if (v->value.rules.source == QA_MODE_THREEWAVE)
        can_armor = armor.kind != QA_ARMOR_NONE &&
                    (armor.kind != QA_ARMOR_Q1 || armor.protection.q1_absorption > 0);
    if (can_armor && armor.points < 150) {
        armor.points = fminf(150, armor.points + 5);
        if (!qa_combat_set_regular_armor(m->options.services.combat, actor, &armor, e))
            return false;
        delay += MODE_SECOND / 2;
    }
    p->regen_ns = now + delay;
    if (delay) {
        bool handled;
        return qa_modes_tech_sound(m, v->id, actor, QA_RELIC_REGENERATION, false, false, &handled,
                                   e);
    }
    return true;
}
bool qa_modes_grapple_allowed(qa_modes *m, qa_mode_id id, qa_actor_id owner, qa_actor_id target,
                              bool pulse) {
    mode_instance *v = mode_get(m, id);
    if (!v)
        return true;
    if (!mode_player_get(m, target))
        return true;
    if (v->value.rules.source == QA_MODE_THREEWAVE) {
        if (!v->value.rules.teamplay)
            return true;
        mode_member *a = mode_member_get(m, v, owner), *b = mode_member_get(m, v, target);
        qa_team_id team;
        if (!a || !qa_modes_team(m, target, &team, NULL))
            return true;
        return (pulse && b ? b->last_team : team) != a->last_team;
    }
    if (v->value.rules.source == QA_MODE_LMCTF && pulse && (v->value.rules.flags & 64u))
        return false;
    if (qa_actor_id_equal(owner, target))
        return true;
    if (qa_modes_same_team(m, owner, target))
        return false;
    return true;
}
bool qa_modes_grapple_hit(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!p)
        return true;
    return mode_event(m, v, QA_MODE_ROSTER, actor, (qa_actor_id){0}, (qa_actor_id){0}, p->last_team,
                      1, QA_BUILTIN_IMPACT, e);
}
