#include "boss_internal.h"

static bool retaliate(qa_q1_game *g, q1_actor *entity, qa_actor_id attacker, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (q1_ref_equal(q1_ref_from(g, attacker), m->charmer))
        return true;
    if (!q1_alive(g, attacker) || qa_actor_id_equal(attacker, entity->id))
        return true;
    qa_string_id classname = 0;
    q1_actor *native = q1_entity(g, attacker);
    if (native)
        classname = native->classname;
    else if (g->services.actor_traits) {
        qa_builtin_actor_traits traits;
        if (g->services.actor_traits(g->services.context, attacker, &traits))
            classname = traits.classname;
    }
    if (classname == entity->classname && m->species->species != QA_Q1_ARMY)
        return true;
    qa_bytes text = qa_strings_text(qa_session_strings(g->services.session), classname);
    if (text.size == 10 && !memcmp(text.data, "worldspawn", 10))
        return true;
    if (q1_ref_equal(m->enemy, q1_ref_from(g, attacker)))
        return true;
    qa_q1_target previous;
    if (q1_target(g, q1_ref_actor(g, m->enemy), &previous) && previous.player)
        m->old_enemy = m->enemy;
    return q1_monster_found(g, entity, attacker, error);
}

bool q1_monster_pain(qa_q1_game *g, q1_actor *entity, qa_actor_id attacker, float damage,
                     qa_error *error) {
    q1_monster *m = &entity->state.monster;
    qa_q1_species species = m->species->species;
    if (m->addon.boss == Q1_BOSS_GHOST)
        return true;
    if (m->addon.boss == Q1_BOSS_FINAL)
        return q1_major_boss_pain(g, entity, attacker, damage, error);
    if (m->addon.infected && m->addon.corpse && !m->addon.risen)
        return true;
    if ((entity->physics.flags & QA_PHYSICS_MONSTER) && !retaliate(g, entity, attacker, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (m->addon.boss == Q1_BOSS_OLDNEW)
        return q1_major_boss_pain(g, entity, attacker, damage, error);
    if (m->addon.boss == Q1_BOSS_ORB) {
        if (m->pain_finished > g->time || q1_random(g) * 200 > damage)
            return true;
        if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_ORB_ORB_PAIN_WAV], 2, 1, 1, error))
            return false;
        m->pain_finished = g->time + 8;
        return !q1_alive(g, entity->id) || q1_monster_play(g, entity, "orb_pain1", error);
    }
    if (m->addon.heavy != Q1_HEAVY_NONE)
        return q1_heavy_pain(g, entity, attacker, damage, error);
    if (species == QA_Q1_GREMLIN)
        return q1_gremlin_pain(g, entity, attacker, error);
    if (species >= QA_Q1_GREMLIN)
        return q1_mission_monster_pain(g, entity, damage, error);
    if (m->addon.rocket_ogre) {
        if (m->pain_finished > g->time || q1_random(g) * 200 > damage)
            return true;
        if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_ARMAGON_PAIN_WAV], 2, 1, 1, error))
            return false;
        float choice = q1_random(g);
        m->pain_finished = g->time + (choice < 0.75f ? 3 : 4);
        return !q1_alive(g, entity->id) || q1_monster_play(g, entity,
                                                           choice < 0.25f   ? "ogre_pain1"
                                                           : choice < 0.5f  ? "ogre_painb1"
                                                           : choice < 0.75f ? "ogre_painc1"
                                                           : choice < 0.88f ? "ogre_paind1"
                                                                            : "ogre_paine1",
                                                           error);
    }
    const char *frame = NULL;
    qa_string_id sound = 0;
    float r;
    switch (species) {
    case QA_Q1_ARMY:
        if (m->pain_finished > g->time)
            return true;
        if (g->options.program == QA_Q1_MG3 && g->options.skill > 2 &&
            q1_random(g) * 100 > damage)
            return true;
        r = q1_random(g);
        m->pain_finished = g->time + (r < 0.2f ? 0.6 : 1.1);
        frame = r < 0.2f ? "army_pain1" : r < 0.6f ? "army_painb1" : "army_painc1";
        sound = r < 0.2f ? g->runtime_names[Q1_NAME_RESOURCE_SOLDIER_PAIN1_WAV] : g->runtime_names[Q1_NAME_RESOURCE_SOLDIER_PAIN2_WAV];
        break;
    case QA_Q1_DOG:
        if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_DOG_DPAIN1_WAV], 2, 1, 1, error))
            return false;
        frame = m->addon.demodog      ? (q1_random(g) > 0.5f ? "demodog_pain1" : "demodog_painb1")
                : q1_random(g) > 0.5f ? "dog_pain1"
                                      : "dog_painb1";
        break;
    case QA_Q1_ZOMBIE:
        if (!qa_combat_set_health(g->services.combat, entity->id, 60, error))
            return false;
        if (damage < 9 || m->in_pain == 2)
            return true;
        if (damage >= 25 || (!m->in_pain && m->pain_finished > g->time)) {
            m->in_pain = 2;
            frame = "zombie_paine1";
            break;
        }
        if (m->in_pain) {
            m->pain_finished = g->time + 3;
            return true;
        }
        m->in_pain = 1;
        r = q1_random(g);
        frame = r < 0.25f   ? "zombie_paina1"
                : r < 0.5f  ? "zombie_painb1"
                : r < 0.75f ? "zombie_painc1"
                            : "zombie_paind1";
        break;
    case QA_Q1_FISH:
        frame = "f_pain1";
        break;
    case QA_Q1_WIZARD:
        if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_WIZARD_WPAIN_WAV], 2, 1, 1, error))
            return false;
        if (q1_random(g) * 70 > damage)
            return true;
        frame = "wiz_pain1";
        break;
    case QA_Q1_SHAMBLER:
        if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_SHAMBLER_SHURT2_WAV], 2, 1, 1, error))
            return false;
        if (q1_health(g, entity->id) <= 0 || q1_random(g) * 400 > damage ||
            m->pain_finished > g->time)
            return true;
        m->pain_finished = g->time + 2;
        frame = "sham_pain1";
        break;
    case QA_Q1_DEMON:
        if (m->jump_touch || m->pain_finished > g->time)
            return true;
        m->pain_finished = g->time + 1;
        if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_DEMON_DPAIN1_WAV], 2, 1, 1, error))
            return false;
        if (q1_random(g) * 200 > damage)
            return true;
        frame = "demon1_pain1";
        break;
    case QA_Q1_KNIGHT:
        if (m->pain_finished > g->time)
            return true;
        r = q1_random(g);
        sound = g->runtime_names[Q1_NAME_RESOURCE_KNIGHT_KHURT_WAV];
        m->pain_finished = g->time + 1;
        frame = r < 0.85f ? "knight_pain1" : "knight_painb1";
        break;
    case QA_Q1_ENFORCER:
        r = q1_random(g);
        if (m->pain_finished > g->time ||
            (m->addon.infected && g->options.skill > 2 && q1_random(g) * 200 > damage))
            return true;
        sound = r < 0.5f ? g->runtime_names[Q1_NAME_RESOURCE_ENFORCER_PAIN1_WAV] : g->runtime_names[Q1_NAME_RESOURCE_ENFORCER_PAIN2_WAV];
        m->pain_finished = g->time + (r < 0.7f ? 1 : 2);
        frame = r < 0.2f   ? "enf_paina1"
                : r < 0.4f ? "enf_painb1"
                : r < 0.7f ? "enf_painc1"
                           : "enf_paind1";
        break;
    case QA_Q1_OGRE:
        if (m->pain_finished > g->time)
            return true;
        if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_OGRE_OGPAIN1_WAV], 2, 1, 1, error))
            return false;
        r = q1_random(g);
        m->pain_finished = g->time + (r < 0.75f ? 1 : 2);
        frame = r < 0.25f   ? "ogre_pain1"
                : r < 0.5f  ? "ogre_painb1"
                : r < 0.75f ? "ogre_painc1"
                : r < 0.88f ? "ogre_paind1"
                            : "ogre_paine1";
        break;
    case QA_Q1_HELLKNIGHT:
        if (m->pain_finished > g->time)
            return true;
        if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_HKNIGHT_PAIN1_WAV], 2, 1, 1, error))
            return false;
        if (g->time - m->pain_finished <= 5 && q1_random(g) * 30 > damage)
            return true;
        m->pain_finished = g->time + 1;
        frame = "hknight_pain1";
        break;
    case QA_Q1_SHALRATH:
        if (m->pain_finished > g->time)
            return true;
        sound = g->runtime_names[Q1_NAME_RESOURCE_SHALRATH_PAIN_WAV];
        m->pain_finished = g->time + 3;
        frame = "shal_pain1";
        break;
    case QA_Q1_TARBABY:
    case QA_Q1_BOSS:
    case QA_Q1_OLDONE:
        return true;
    default:
        qa_error_set(error, QA_ERROR_UNSUPPORTED, species,
                     "Q1 monster pain controller not admitted");
        return false;
    }
    if (species == QA_Q1_ARMY && m->addon.enabled) {
        if (!q1_monster_play(g, entity, frame, error))
            return false;
        return !q1_alive(g, entity->id) || !sound || q1_sound_resource(g, entity->id, sound, 2, 1, 1, error);
    }
    if (sound && !q1_sound_resource(g, entity->id, sound, 2, 1, 1, error))
        return false;
    return !q1_alive(g, entity->id) || q1_monster_play(g, entity, frame, error);
}

bool qa_q1_game_monster_count(qa_q1_game *g, qa_actor_id monster, qa_actor_id attacker,
    bool killed, bool classic_fish, qa_error *error) {
    if (!killed) { g->total_monsters += classic_fish ? 2u : 1u; return true; }
    ++g->killed_monsters;
    return qa_builtin_emit(&g->services, &(qa_builtin_event){.kind = QA_BUILTIN_DEATH,
        .family = QA_GAME_Q1, .provider = g->options.provider, .actor = monster,
        .other = attacker, .time_ns = g->time_ns, .count = (int32_t)g->killed_monsters,
        .value = (float)g->total_monsters}, error);
}
bool q1_monster_death_report(qa_q1_game *g, q1_actor *entity, qa_actor_id killer, bool count,
                             qa_error *error) {
    if (g->host.monster_killed)
        return g->host.monster_killed(g->host.context, entity->id, killer, count, error);
    return !count || qa_q1_game_monster_count(g, entity->id, killer, true, false, error);
}
bool q1_monster_count_kill(qa_q1_game *g, q1_actor *entity, qa_actor_id killer, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (m->counted_death)
        return true;
    m->counted_death = true;
    qa_monster_mission mission;
    if (q1_monster_mission(g, entity->id, &mission)) {
        entity->physics.flags &= ~(uint32_t)(QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING);
        return mission.killed(mission.context, entity->id, killer, error);
    }
    bool count = g->host.count_monster_kill
                     ? g->host.count_monster_kill(g->host.context, entity->id)
                     : !(m->horde && m->species->species == QA_Q1_ZOMBIE);
    if (m->addon.infection_count_pending) {
        m->addon.infection_count_pending = false;
        count = false;
    }
    if (!q1_monster_death_report(g, entity, killer, count, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (g->options.edition == QA_Q1_RERELEASE && (entity->physics.flags & QA_PHYSICS_MONSTER) &&
        killer.registry && !qa_actor_id_equal(killer, entity->id)) {
        bool monster = false;
        q1_actor *native = q1_entity(g, killer);
        if (native)
            monster = (native->physics.flags & QA_PHYSICS_MONSTER) != 0;
        else if (g->services.actor_traits) {
            qa_builtin_actor_traits traits;
            monster =
                g->services.actor_traits(g->services.context, killer, &traits) && traits.monster;
        }
        if (monster) {
            qa_builtin_event event = {.kind = QA_BUILTIN_ACHIEVEMENT,
                                      .family = QA_GAME_Q1,
                                      .provider = g->options.provider,
                                      .actor = entity->id,
                                      .time_ns = g->time_ns};
            if (!qa_builtin_resource(&g->services, "ACH_FRIENDLY_FIRE", &event.text, error) ||
                !qa_builtin_emit(&g->services, &event, error))
                return false;
        }
    }
    entity->physics.flags &= ~(uint32_t)(QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING);
    if (!q1_alive(g, entity->id) ||
        (!qa_strings_text(qa_session_strings(g->services.session), entity->target).size &&
         !qa_strings_text(qa_session_strings(g->services.session), entity->killtarget).size))
        return true;
    if (!g->services.use_targets) {
        qa_error_set(error, QA_ERROR_ARGUMENT, entity->id.slot,
                     "Q1 monster death requires target dispatch");
        return false;
    }
    return g->services.use_targets(g->services.context, entity->id, killer, entity->target,
                                   entity->killtarget, entity->delay, error);
}

static bool gib_monster(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id source = entity->id;
    const q1_species *spec = entity->state.monster.species;
    bool foundation = !entity->state.monster.addon.enabled &&
                      (spec->species == QA_Q1_ARMY || spec->species == QA_Q1_DOG);
    float health = foundation ? q1_health(g, source) : 0;
    entity = q1_entity(g, source);
    if (!entity)
        return true;
    if (!q1_sound_resource(g, source, spec->species == QA_Q1_ZOMBIE ? g->runtime_names[Q1_NAME_RESOURCE_ZOMBIE_Z_GIB_WAV] : g->runtime_names[Q1_NAME_RESOURCE_PLAYER_UDEATH_WAV], 2, 1, 1, error))
        return false;
    entity = q1_entity(g, source);
    if (!entity)
        return true;
    if (foundation && spec->species == QA_Q1_ARMY &&
        !q1_gib_head(g, entity, (spec->head==Q1_NAME_COUNT?0:g->runtime_names[spec->head]), health, error))
        return false;
    for (unsigned i = 0; i < 3; ++i) {
        qa_string_id model = spec->species == QA_Q1_DOG || spec->species == QA_Q1_OGRE ? g->runtime_names[Q1_NAME_RESOURCE_PROGS_GIB3_MDL]
                            : spec->species == QA_Q1_DEMON                            ? g->runtime_names[Q1_NAME_RESOURCE_PROGS_GIB1_MDL]
                            : spec->species == QA_Q1_WIZARD                           ? g->runtime_names[Q1_NAME_RESOURCE_PROGS_GIB2_MDL]
                            : i == 0                                                  ? g->runtime_names[Q1_NAME_RESOURCE_PROGS_GIB1_MDL]
                            : i == 1                                                  ? g->runtime_names[Q1_NAME_RESOURCE_PROGS_GIB2_MDL]
                                                                                      : g->runtime_names[Q1_NAME_RESOURCE_PROGS_GIB3_MDL];
        if (!q1_entity(g, source))
            return true;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, source, &body, error))
            return !q1_entity(g, source);
        if (!q1_entity(g, source))
            return true;
        if (!foundation) {
            health = q1_health(g, source);
            if (!q1_entity(g, source))
                return true;
        }
        if (!q1_gib_at(g, source, body.origin, health, model, error))
            return false;
    }
    entity = q1_entity(g, source);
    if (!entity || (foundation && spec->species == QA_Q1_ARMY))
        return true;
    return foundation ? q1_gib_head(g, entity, (spec->head==Q1_NAME_COUNT?0:g->runtime_names[spec->head]), health, error)
                      : q1_gib(g, entity, (spec->head==Q1_NAME_COUNT?0:g->runtime_names[spec->head]), true, error);
}

bool q1_monster_die(qa_q1_game *g, q1_actor *entity, qa_actor_id attacker, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    const q1_species *spec = m->species;
    if (m->addon.boss != Q1_BOSS_NONE)
        return q1_boss_die(g, entity, attacker, error);
    if (q1_ref_present(m->charmer))
        entity->effects &= ~8u;
    if (m->counted_death)
        return true;
    m->dead = true;
    bool foundation =
        !m->addon.enabled && (spec->species == QA_Q1_ARMY || spec->species == QA_Q1_DOG);
    qa_actor_id killer = q1_ref_actor(g, foundation && q1_ref_present(m->enemy) ? m->enemy : q1_ref_from(g, attacker));
    if (!foundation) {
        m->enemy = q1_ref_from(g, attacker);
        if (q1_health(g, entity->id) < -99 &&
            !qa_combat_set_health(g->services.combat, entity->id, -99, error))
            return false;
    }
    m->jump_touch = false;
    if (spec->species == QA_Q1_GREMLIN)
        m->source.gremlin.touch = 0;
    qa_combat_state combat;
    if (!qa_combat_read_traits(g->services.combat, entity->id, &combat, error))
        return false;
    combat.can_take_damage = false;
    if (!qa_combat_set_traits(g->services.combat, entity->id, &combat, error))
        return false;
    if (spec->species == QA_Q1_OLDONE) {
        if (!g->host.finale) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 finale consumer missing");
            return false;
        }
        return g->host.finale(g->host.context, entity->id, false, error);
    }
    if (!q1_monster_count_kill(g, entity, killer, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (m->addon.infected && (!m->addon.transformed || spec->species == QA_Q1_ZOMBIE))
        return q1_infected_die(g, entity, error);
    if (m->addon.demodog)
        return q1_demodog_die(g, entity, error);
    if (m->addon.heavy != Q1_HEAVY_NONE)
        return q1_heavy_die(g, entity, error);
    if (spec->species == QA_Q1_GREMLIN)
        return q1_gremlin_die(g, entity, attacker, error);
    if (spec->species >= QA_Q1_GREMLIN)
        return q1_mission_monster_die(g, entity, error);
    if (q1_health(g, entity->id) < spec->gib_health && spec->head != Q1_NAME_COUNT)
        return gib_monster(g, entity, error);
    qa_string_id sound = 0;
    const char *frame = NULL;
    switch (spec->species) {
    case QA_Q1_ARMY:
        sound = g->runtime_names[Q1_NAME_RESOURCE_SOLDIER_DEATH1_WAV];
        break;
    case QA_Q1_DOG:
        sound = g->runtime_names[Q1_NAME_RESOURCE_DOG_DDEATH_WAV];
        entity->physics.solid = QA_PHYSICS_NOT_SOLID;
        break;
    case QA_Q1_KNIGHT:
        sound = g->runtime_names[Q1_NAME_RESOURCE_KNIGHT_KDEATH_WAV];
        break;
    case QA_Q1_ENFORCER:
        sound = g->runtime_names[Q1_NAME_RESOURCE_ENFORCER_DEATH1_WAV];
        break;
    case QA_Q1_DEMON:
        frame = "demon1_die1";
        break;
    case QA_Q1_OGRE:
        sound = m->addon.rocket_ogre ? g->runtime_names[Q1_NAME_RESOURCE_ARMAGON_SIGHT2_WAV] : g->runtime_names[Q1_NAME_RESOURCE_OGRE_OGDTH_WAV];
        break;
    case QA_Q1_HELLKNIGHT:
        sound = g->runtime_names[Q1_NAME_RESOURCE_HKNIGHT_DEATH1_WAV];
        break;
    case QA_Q1_SHAMBLER:
        sound = g->runtime_names[Q1_NAME_RESOURCE_SHAMBLER_SDEATH_WAV];
        frame = "sham_death1";
        break;
    case QA_Q1_WIZARD:
        entity->physics.motion = QA_PHYSICS_TOSS;
        frame = "wiz_death1";
        break;
    case QA_Q1_SHALRATH:
        sound = g->runtime_names[Q1_NAME_RESOURCE_SHALRATH_DEATH_WAV];
        entity->physics.solid = QA_PHYSICS_NOT_SOLID;
        frame = "shal_death1";
        break;
    case QA_Q1_TARBABY:
        frame = "tbaby_die1";
        break;
    case QA_Q1_FISH:
        frame = "f_death1";
        break;
    case QA_Q1_ZOMBIE:
        return gib_monster(g, entity, error);
    case QA_Q1_BOSS:
        frame = "boss_death1";
        break;
    default:
        qa_error_set(error, QA_ERROR_UNSUPPORTED, spec->species,
                     "Q1 monster death controller not admitted");
        return false;
    }
    if (sound && !q1_sound_resource(g, entity->id, sound, 2, 1, 1, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (!frame) {
        float r = q1_random(g);
        switch (spec->species) {
        case QA_Q1_ARMY:
            frame = r < 0.5f ? "army_die1" : "army_cdie1";
            break;
        case QA_Q1_DOG:
            frame = r > 0.5f ? "dog_die1" : "dog_dieb1";
            break;
        case QA_Q1_KNIGHT:
            frame = r < 0.5f ? "knight_die1" : "knight_dieb1";
            break;
        case QA_Q1_ENFORCER:
            frame = r > 0.5f ? "enf_die1" : "enf_fdie1";
            break;
        case QA_Q1_OGRE:
            frame = r < 0.5f ? "ogre_die1" : "ogre_bdie1";
            break;
        case QA_Q1_HELLKNIGHT:
            frame = r > 0.5f ? "hknight_die1" : "hknight_dieb1";
            break;
        default:
            return false;
        }
    }
    return q1_link(g, entity, error) && q1_monster_play(g, entity, frame, error);
}

bool q1_monster_use(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    qa_monster_mission mission;
    bool handled = false;
    if (q1_monster_mission(g, entity->id, &mission) &&
        !mission.use(mission.context, entity->id, activator, &handled, error)) return false;
    if (handled || !q1_alive(g, entity->id)) return true;
    if (!m->addon.normal_use &&
        (m->addon.boss == Q1_BOSS_GHOST || m->addon.boss == Q1_BOSS_SHUB_ZOMBIE))
        return true;
    if (!m->addon.normal_use && m->species->species == QA_Q1_LAVA_MAN &&
        g->options.program == QA_Q1_MG3)
        return q1_lavaman_use(g, entity, activator, error);
    if (m->addon.enabled) {
        if (m->addon.waiting)
            return q1_monster_start(g, entity, error);
        if (m->addon.path_wait) {
            m->addon.path_wait = false;
            if (q1_health(g, entity->id) <= 0 || q1_ref_present(m->enemy))
                return true;
            m->pause_until = 0;
            return q1_monster_play(g, entity, m->species->walk, error);
        }
        qa_q1_target traits;
        if (!q1_target(g, activator, &traits) || !traits.player) {
            qa_builtin_snapshot_frame *snapshot;
            if (!q1_snapshot_players(g, &snapshot, error))
                return false;
            activator = (qa_actor_id){0};
            for (size_t i = 0; i < snapshot->snapshot.count; ++i)
                if (q1_health(g, snapshot->snapshot.ids[i]) > 0) {
                    activator = snapshot->snapshot.ids[i];
                    break;
                }
            qa_builtin_snapshot_release(snapshot);
        }
    }
    if (!m->addon.normal_use && m->species->species == QA_Q1_BOSS &&
        m->addon.boss != Q1_BOSS_FINAL) {
        entity->physics.solid = QA_PHYSICS_BOX;
        qa_combat_state combat;
        qa_body_state body;
        if (!qa_combat_read_traits(g->services.combat, entity->id, &combat, error) ||
            !qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        combat.can_take_damage = false;
        m->enemy = q1_ref_from(g, activator);
        return qa_combat_set_traits(g->services.combat, entity->id, &combat, error) &&
               qa_combat_set_health(g->services.combat, entity->id, g->options.skill == 0 ? 1 : 3,
                                    error) &&
               q1_model(g, entity, g->runtime_names[Q1_NAME_RESOURCE_PROGS_BOSS_MDL], error) &&
               q1_effect(g, QA_BUILTIN_EXPLOSION, entity->id, body.origin, 0, 10, error) &&
               q1_link(g, entity, error) && q1_monster_play(g, entity, "boss_rise1", error);
    }
    qa_q1_target target;
    if (q1_ref_present(m->enemy) || q1_health(g, entity->id) <= 0 || !q1_target(g, activator, &target) ||
        !target.player || target.invisible || target.notarget)
        return true;
    m->enemy = q1_ref_from(g, activator);
    return q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FOUND, error);
}
