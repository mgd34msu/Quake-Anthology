#include "internal.h"

bool qa_modes_player_hurt(qa_modes *m, qa_mode_id id, const qa_damage_request *request,
                          qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !request)
        return mode_fail(e, "invalid mode hurt");
    mode_member *target = mode_member_get(m, v, request->target);
    mode_member *attacker = mode_member_get(m, v, request->attack.attacker);
    if (target && attacker && target->flag.registry &&
        !qa_modes_same_team(m, request->target, request->attack.attacker)) {
        attacker->stats.hurt_carrier = true;
        attacker->stats.hurt_carrier_ns = v->value.time_ns;
    }
    return true;
}
bool mode_update_ghosts(qa_modes *m, mode_instance *v, qa_error *e) {
    if (v->value.rules.source != QA_MODE_Q2_CTF)
        return true;
    for (uint32_t i = 0; i < m->actor_capacity; ++i) {
        mode_ghost *ghost = &v->ghosts[i];
        mode_member *p = ghost->code ? mode_member_get(m, v, ghost->actor) : NULL;
        if (!p || p->ghost_code != ghost->code)
            continue;
        if (!qa_modes_score(m, ghost->actor, &ghost->score, e))
            return false;
        ghost->stats = p->stats;
    }
    return true;
}
bool qa_modes_player_death_component(qa_modes *m, qa_mode_id id, const qa_damage_outcome *outcome,
                                     bool primary_score, const qa_mode_frag *ordinary,
                                     qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !outcome)
        return mode_fail(e, "invalid mode death");
    if (!v->value.rules.enabled)
        return true;
    qa_actor_id victim = outcome->request.target, attacker = outcome->request.attack.attacker;
    mode_member *dead = mode_member_get(m, v, victim), *killer = mode_member_get(m, v, attacker);
    if (!dead)
        return true;
    mode_stat_add(v, &dead->stats.deaths, 1);
    bool self = qa_actor_id_equal(victim, attacker),
         friendly = killer && !self && qa_modes_same_team(m, victim, attacker);
    if (v->value.rules.kind == QA_MODE_HORDE) {
        qa_actor_id recipient = ordinary ? ordinary->recipient : killer ? attacker : victim;
        int32_t change = ordinary ? ordinary->delta : !killer || self ? -1 : 1;
        if (primary_score && recipient.registry && !qa_modes_add_score(m, id, recipient, change, e))
            return false;
        dead->spawn_state = 1;
        return mode_horde_death(m, v, outcome, e);
    } else if (v->value.rules.kind == QA_MODE_TAG) {
        if (!mode_tag_death(m, v, outcome, primary_score, ordinary, e))
            return false;
    } else {
        qa_actor_id recipient = ordinary ? ordinary->recipient : killer ? attacker : victim;
        bool team_penalty =
            friendly && (v->value.rules.source >= QA_MODE_Q2 || v->value.rules.teamplay == 2);
        int32_t change = ordinary ? ordinary->delta : !killer || self || team_penalty ? -1 : 1;
        if (v->value.rules.source == QA_MODE_THREEWAVE && killer && !self) {
            if (v->value.rules.teamplay < 0)
                change = v->value.rules.teamplay;
            else if (friendly && v->value.rules.teamplay != 2 && !(v->value.rules.teamplay & 8))
                change = 1;
        }
        if (primary_score && recipient.registry && !qa_modes_add_score(m, id, recipient, change, e))
            return false;
        if (killer && !self && !friendly)
            mode_stat_add(v, &killer->stats.kills, 1);
        if (primary_score && v->value.rules.kind == QA_MODE_TEAM_DEATHMATCH && killer &&
            v->value.rules.source >= QA_MODE_Q3) {
            qa_team_id team;
            if (!qa_modes_team(m, attacker, &team, e))
                return false;
            if (mode_team_index(v, team) >= 0 && !qa_modes_team_score(m, id, team, change, e))
                return false;
        }
        if ((change > 0 || v->value.rules.source == QA_MODE_ROGUE) &&
            v->value.rules.kind >= QA_MODE_CTF && v->value.rules.kind <= QA_MODE_HARVESTER &&
            !mode_flag_bonus(m, v, attacker, victim, e))
            return false;
        if (v->value.rules.source == QA_MODE_THREEWAVE && friendly &&
            v->value.rules.teamplay >= 0 && (v->value.rules.teamplay & 16)) {
            qa_string_id type;
            if (!qa_builtin_resource(&m->options.services, "ctf:teamkill", &type, e))
                return false;
            qa_damage_request punishment = {
                .target = attacker,
                .amount = 1000,
                .attack = {.attacker = attacker,
                           .inflictor = attacker,
                           .weapon_provider = m->options.owner,
                           .time_ns = v->value.time_ns,
                           .cause = {.kind = QA_CAUSE_Q1, .source.q1 = {.death_type = type}}}};
            if (!mode_damage(m, QA_GAME_Q1, &punishment, e) ||
                !qa_modes_add_score(m, id, attacker, 1, e))
                return false;
        }
    }
    if (v->value.rules.kind == QA_MODE_HARVESTER) {
        mode_object *neutral = mode_object_get(m, v->bases[2]);
        qa_team_id team;
        if (neutral && qa_modes_team(m, victim, &team, e)) {
            qa_mode_object_spec cube = {.kind = QA_MODE_OBJECT_CUBE,
                                        .team = team,
                                        .origin = neutral->home,
                                        .suspended = true};
            cube.origin.z += 44;
            qa_actor_id actor;
            if (!qa_modes_spawn_object(m, id, &cube, &actor, e))
                return false;
            mode_object *o = mode_object_get(m, actor);
            qa_body_state body;
            if (o && qa_world_body_read(m->options.services.world, actor, &body, e)) {
                body.velocity.x = (mode_random_float(m) - .5f) * 200;
                body.velocity.y = (mode_random_float(m) - .5f) * 200;
                body.velocity.z = 200;
                o->physics.motion = QA_PHYSICS_TOSS;
                o->expire_ns = v->value.time_ns + 30 * MODE_SECOND;
                if (!qa_world_body_write(m->options.services.world, actor, &body, e))
                    return false;
            }
        }
        dead->stats.tokens = 0;
    }
    if (v->value.rules.source == QA_MODE_ROGUE) {
        mode_object *flag = mode_object_get(m, dead->flag);
        if (flag)
            for (size_t i = 0; i < m->players_order.count; ++i) {
                mode_member *member = mode_member_get(m, v, m->players_order.ids[i]);
                qa_team_id team;
                if (member && qa_modes_team(m, member->actor, &team, NULL) &&
                    (v->value.rules.teamplay == 5 || team == flag->spec.team))
                    member->stats.hurt_carrier = false;
            }
    }
    dead->spawn_state = 1;
    return qa_modes_drop(m, id, victim, true, e) && mode_update_ghosts(m, v, e) &&
           qa_modes_rank(m, id, e);
}
bool qa_modes_player_death(qa_modes *m, qa_mode_id id, const qa_damage_outcome *outcome,
                           qa_error *e) {
    return qa_modes_player_death_component(m, id, outcome, true, NULL, e);
}
bool qa_modes_player_respawn(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!p)
        return mode_fail(e, "unknown respawn participant");
    p->stats.hurt_carrier = false;
    p->stats.flag_since_ns = 0;
    p->stats.tokens = 0;
    return mode_horde_respawn(m, v, actor, e);
}
