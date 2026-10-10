#include "internal.h"

static float rogue_random(qa_modes *m, mode_instance *v) {
    return m->options.hooks.source_random
               ? m->options.hooks.source_random(m->options.hooks.context, v->id)
               : mode_random_float(m);
}
static qa_vec3 rogue_velocity(qa_modes *m, mode_instance *v) {
    float x = -300 + rogue_random(m, v) * 600;
    float y = -300 + rogue_random(m, v) * 600;
    return qa_v3(x, y, 300);
}
static bool rogue_message(qa_modes *m, mode_instance *v, qa_actor_id actor,
                            const char *text, qa_error *e) {
    qa_builtin_event event = {.kind = QA_BUILTIN_CENTERPRINT, .family = QA_GAME_Q1,
        .provider = m->options.owner, .actor = actor, .time_ns = v->value.time_ns,
        .flags = 1};
    return qa_builtin_resource(&m->options.services, text, &event.text, e) &&
           MODE_CALLBACK(m, m->options.hooks.emit
               ? m->options.hooks.emit(m->options.hooks.context, v->id, &event, e)
               : qa_builtin_emit(&m->options.services, &event, e));
}
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
    if (v->value.rules.source == QA_MODE_ROGUE) {
        size_t previous = SIZE_MAX, first = SIZE_MAX, next = SIZE_MAX;
        for (size_t i = 0; i < v->spawn_count; ++i) {
            qa_mode_spawnpoint *point = &v->spawns[i];
            if (point->classname != classname || !mode_live(m, point->actor)) continue;
            if (first == SIZE_MAX) first = i;
            if (previous != SIZE_MAX && next == SIZE_MAX) next = i;
            if (qa_actor_id_equal(point->actor, v->rogue_spawn_spot)) previous = i;
        }
        size_t selected = next == SIZE_MAX ? first : next;
        if (selected == SIZE_MAX)
            return mode_fail(e, "Rogue runes require a live deathmatch spawn point");
        qa_actor_id spot = v->spawns[selected].actor;
        qa_body_state body;
        if (!qa_world_body_read(m->options.services.world, spot, &body, e))
            return false;
        if (!mode_live(m, spot)) return mode_fail(e, "Rogue rune spawn retired during body read");
        v->rogue_spawn_spot = spot;
        *origin = body.origin;
        return true;
    }
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
static bool relic_place(qa_modes *m, mode_instance *v, mode_object *o, bool initial, qa_error *e) {
    qa_actor_id actor = o->actor;
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
    o = mode_object_get(m, actor);
    if (!o) return mode_fail(e, "rune retired during spawn point query");
    qa_body_state body;
    if (!qa_world_body_read(m->options.services.world, actor, &body, e))
        return false;
    o = mode_object_get(m, actor);
    if (!o) return mode_fail(e, "rune retired during placement body read");
    body.origin = origin;
    body.ground = (qa_actor_reference){0};
    if (v->value.rules.source == QA_MODE_ROGUE) {
        body.velocity = rogue_velocity(m, v);
        o = mode_object_get(m, actor);
        if (!o) return mode_fail(e, "Rogue rune retired during source velocity query");
    } else if (v->value.rules.source == QA_MODE_THREEWAVE) {
        body.origin.z -= 24;
        body.velocity.x = -500 + mode_random_float(m) * 1000;
        body.velocity.y = -500 + mode_random_float(m) * 1000;
        body.velocity.z = 400;
    } else if (v->value.rules.source == QA_MODE_LMCTF) {
        qa_trace_query query = {
            .start = origin,
            .end = origin,
            .shape = {QA_SHAPE_BOX, body.bounds},
            .policy = {.behavior = &qa_trace_behaviors[QA_RULESET_Q2_CLASSIC], .contents_mask = qa_collision_contents_mask(3, QA_GAME_Q2), .q1_hull = -1}};
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
    o->expire_ns = v->value.time_ns + (v->value.rules.source == QA_MODE_THREEWAVE ||
                                        v->value.rules.source == QA_MODE_ROGUE ? 120
                                       : v->value.rules.source == QA_MODE_LMCTF   ? 30
                                                                                  : 60) *
                                          MODE_SECOND;
    if (!qa_world_body_write(m->options.services.world, actor, &body, e)) return false;
    o = mode_object_get(m, actor);
    if (!o) return mode_fail(e, "rune retired during placement body write");
    if (!mode_object_hide(m, o, false, e)) return false;
    return mode_object_get(m, actor) != NULL || mode_fail(e, "rune retired during placement link");
}
bool mode_relic_place(qa_modes *m, mode_instance *v, mode_object *o, bool initial, qa_error *e) {
    return MODE_CALLBACK(m, relic_place(m, v, o, initial, e));
}

bool qa_modes_has_relic(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_relic_kind kind) {
    mode_instance *v = mode_get(m, id);
    if (!v || !v->value.rules.enabled ||
        (!v->value.rules.relics && v->value.rules.source != QA_MODE_ROGUE))
        return false;
    mode_member *p = mode_member_get(m, v, actor);
    if (v->value.rules.source == QA_MODE_ROGUE)
        return p && kind >= QA_RELIC_RESISTANCE && kind <= QA_RELIC_REGENERATION &&
               p->rogue_rune == (UINT32_C(1) << kind);
    mode_object *o = p ? mode_object_get(m, p->relic) : NULL;
    return o && o->spec.kind == QA_MODE_OBJECT_RELIC && o->spec.relic == kind &&
           qa_actor_id_equal(o->value.carrier, actor);
}
bool mode_relic_touch(qa_modes *m, mode_instance *v, mode_object *o, qa_actor_id actor,
                      bool *accepted, qa_error *e) {
    mode_member *p = mode_member_get(m, v, actor);
    if (!p || (!v->value.rules.relics && v->value.rules.source != QA_MODE_ROGUE))
        return true;
    if (v->value.rules.source == QA_MODE_ROGUE) {
        if (p->rogue_rune) {
            bool notice = p->notice_ns < v->value.time_ns;
            p->notice_ns = v->value.time_ns + 5 * MODE_SECOND;
            return !notice || rogue_message(m, v, actor, "$qc_already_have_rune", e);
        }
        if (o->spec.relic > QA_RELIC_REGENERATION) return true;
        p->rogue_rune = UINT32_C(1) << o->spec.relic;
        qa_actor_id rune = o->actor;
        static const char *messages[] = {"$qc_rune_resistance", "$qc_rune_strength",
                                        "$qc_rune_haste", "$qc_rune_regeneration"};
        const char *message = messages[o->spec.relic];
        *accepted = true;
        if (!mode_sound(m, v, actor, "weapons/pkup.wav", 1, e) ||
            !rogue_message(m, v, actor, message, e)) return false;
        return !mode_live(m, rune) || qa_session_release(m->options.services.session, rune, e);
    }
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
                      (int32_t)o->spec.relic, 0, e);
}
bool qa_modes_tech_sound(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_relic_kind kind,
                         bool quad, bool silenced, bool *handled, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!handled)
        return mode_fail(e, "missing tech sound result");
    *handled = p && qa_modes_has_relic(m, id, actor, kind);
    if (v && v->value.rules.source == QA_MODE_ROGUE) {
        if (!*handled || kind > QA_RELIC_HASTE ||
            p->rune_sound_ns[kind] >= v->value.time_ns) return true;
        p->rune_sound_ns[kind] = v->value.time_ns + MODE_SECOND;
        static const char *sounds[] = {"runes/end1.wav", "runes/end2.wav", "runes/end3.wav"};
        return mode_sound(m, v, actor, sounds[kind], 1, e);
    }
    if (v && v->value.rules.source == QA_MODE_THREEWAVE) {
        if (!*handled || kind > QA_RELIC_REGENERATION ||
            p->rune_sound_ns[kind] >= v->value.time_ns) return true;
        p->rune_sound_ns[kind] = v->value.time_ns + MODE_SECOND;
        const char *sound = kind == QA_RELIC_RESISTANCE ? "rune/rune1.wav"
            : kind == QA_RELIC_STRENGTH ? (quad ? "rune/rune22.wav" : "rune/rune2.wav")
            : kind == QA_RELIC_HASTE ? "rune/rune3.wav" : "rune/rune4.wav";
        return mode_sound_channel(m, v, actor, sound, 4, 1, e);
    }
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
    if (v->value.rules.source == QA_MODE_ROGUE && !v->value.rules.rogue_deathmatch) return true;
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
    if (v->value.rules.source == QA_MODE_ROGUE && !v->value.rules.rogue_deathmatch) return true;
    if (!qa_modes_has_relic(m, id, actor, QA_RELIC_RESISTANCE))
        return true;
    *out = v->value.rules.source == QA_MODE_LMCTF    ? truncf(amount / 1.75f)
           : v->value.rules.source == QA_MODE_Q2_CTF ? truncf(amount * .5f)
                                                     : amount * .5f;
    if (v->value.rules.source == QA_MODE_Q2_CTF)
        return mode_sound(m, v, actor, "ctf/tech1.wav", 1, e);
    if (v->value.rules.source == QA_MODE_LMCTF)
        return mode_sound(m, v, actor, "ctf/resist.wav", 1, e);
    if (v->value.rules.source == QA_MODE_THREEWAVE || v->value.rules.source == QA_MODE_ROGUE) {
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
bool mode_relic_source_current(qa_modes *m, mode_instance *v, qa_error *e) {
    if (v->value.rules.source != QA_MODE_ROGUE || !v->relics_started) return true;
    if (!m->options.hooks.rogue_runes_read)
        return mode_fail(e, "saved Rogue rune startup has no actual source-world reader");
    qa_actor_id world = {0}; bool started = false, okay;
    MODE_CALLBACK(m, okay = m->options.hooks.rogue_runes_read(
        m->options.hooks.context, v->id, &world, &started));
    const qa_physics *physics = m->options.services.physics;
    if (!okay || !started || !physics || !world.registry ||
        !qa_actor_id_equal(world, physics->world_actor) || !mode_live(m, world))
        return mode_fail(e, "saved Rogue rune startup differs from its actual source world");
    return true;
}
bool mode_relic_frame(qa_modes *m, mode_instance *v, qa_actor_id actor, mode_member *p,
                      qa_error *e) {
    if (v->value.rules.source == QA_MODE_ROGUE) {
        if (v->value.rules.rogue_deathmatch && v->value.rules.relics && !v->relics_started) {
            bool claimed = false, started = false;
            qa_actor_id world = {0};
            if (!m->options.hooks.rogue_runes_claim || !m->options.hooks.rogue_runes_read)
                return mode_fail(e, "Rogue rune startup has no actual source-world claim");
            bool okay;
            MODE_CALLBACK(m, okay = m->options.hooks.rogue_runes_claim(
                m->options.hooks.context, v->id, &claimed, e));
            if (!okay) return false;
            MODE_CALLBACK(m, okay = m->options.hooks.rogue_runes_read(
                m->options.hooks.context, v->id, &world, &started));
            if (!okay) return mode_fail(e, "Rogue rune startup source retired");
            p = mode_member_get(m, v, actor);
            if (!p) return mode_fail(e, "Rogue rune startup player retired");
            if (started) {
                if (!world.registry || !mode_live(m, world) || !m->options.services.physics ||
                    !qa_actor_id_equal(world, m->options.services.physics->world_actor))
                    return mode_fail(e, "Rogue rune startup source world retired");
                v->relics_started = true;
                if (claimed) v->relic_spawn_ns = v->value.time_ns + MODE_SECOND / 10;
            } else if (claimed) return mode_fail(e, "Rogue rune source claim was not committed");
        }
        if (!qa_modes_has_relic(m, v->id, actor, QA_RELIC_REGENERATION) ||
            p->regen_ns >= v->value.time_ns) return true;
        qa_combat_state state;
        if (!qa_combat_read(m->options.services.combat, actor, &state, e)) return false;
        if (state.health >= 100) return true;
        if (!mode_sound(m, v, actor, "runes/end4.wav", 1, e)) return false;
        if (!mode_member_get(m, v, actor)) return mode_fail(e, "Rogue rune carrier retired during regeneration");
        if (!qa_combat_set_health(m->options.services.combat, actor, fminf(100, state.health + 5), e))
            return false;
        p = mode_member_get(m, v, actor);
        if (!p) return mode_fail(e, "Rogue rune carrier retired during regeneration health store");
        p->regen_ns = v->value.time_ns + MODE_SECOND;
        return true;
    }
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
            armor.points = fminf(200, (float)armor.points + (float)(heart / 3));
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
        armor.points = fminf(150, (float)armor.points + 5);
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

bool mode_rogue_relic_drop(qa_modes *m, mode_instance *v, qa_actor_id actor, mode_member *p,
                            qa_error *e) {
    qa_body_state carrier;
    if (!qa_world_body_read(m->options.services.world, actor, &carrier, e)) return false;
    p = mode_member_get(m, v, actor);
    if (!p) return mode_fail(e, "Rogue rune carrier retired during drop body read");
    qa_relic_kind kind = QA_RELIC_RESISTANCE;
    while ((UINT32_C(1) << kind) != p->rogue_rune && kind < QA_RELIC_REGENERATION) ++kind;
    qa_mode_object_spec spec = {.kind = QA_MODE_OBJECT_RELIC, .relic = kind,
                                .origin = carrier.origin, .suspended = true};
    qa_actor_id rune;
    if (!qa_modes_spawn_object(m, v->id, &spec, &rune, e)) return false;
    mode_object *o = mode_object_get(m, rune);
    qa_body_state body;
    if (!o || !qa_world_body_read(m->options.services.world, rune, &body, e)) goto rollback;
    body.velocity = rogue_velocity(m, v);
    o = mode_object_get(m, rune);
    if (!o || !mode_member_get(m, v, actor)) {
        mode_fail(e, "Rogue rune or carrier retired during drop velocity query");
        goto rollback;
    }
    o->physics.motion = QA_PHYSICS_TOSS;
    o->expire_ns = v->value.time_ns + 120 * MODE_SECOND;
    if (!qa_world_body_write(m->options.services.world, rune, &body, e)) goto rollback;
    o = mode_object_get(m, rune);
    if (!o || !mode_member_get(m, v, actor)) {
        mode_fail(e, "Rogue rune or carrier retired during drop body write");
        goto rollback;
    }
    if (!mode_object_hide(m, o, false, e)) goto rollback;
    o = mode_object_get(m, rune);
    p = mode_member_get(m, v, actor);
    if (!o || !p) {
        mode_fail(e, "Rogue rune or carrier retired during drop link");
        goto rollback;
    }
    p->rogue_rune = 0;
    return true;
rollback:
    if (mode_live(m, rune)) qa_session_release(m->options.services.session, rune, NULL);
    return false;
}
bool qa_modes_grapple_allowed(qa_modes *m, qa_mode_id id, qa_actor_id owner, qa_actor_id target,
                              bool pulse) {
    mode_instance *v = mode_get(m, id);
    if (!v || !v->value.rules.enabled || !mode_member_get(m, v, owner) ||
        !mode_member_get(m, v, target))
        return true;
    if (!mode_player_get(m, target))
        return true;
    if (v->value.rules.source == QA_MODE_THREEWAVE) {
        if (!v->value.rules.teamplay)
            return true;
        mode_member *a = mode_member_get(m, v, owner), *b = mode_member_get(m, v, target);
        qa_team_id team;
        if (!a || !qa_modes_team(m, v->id, target, &team, NULL))
            return true;
        return (pulse && b ? b->last_team : team) != a->last_team;
    }
    if (v->value.rules.source == QA_MODE_LMCTF && pulse && (v->value.rules.flags & 64u))
        return false;
    if (qa_actor_id_equal(owner, target))
        return true;
    if (qa_modes_same_team(m, v->id, owner, target))
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
