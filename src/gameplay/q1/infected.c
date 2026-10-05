#include "internal.h"

const q1_species *q1_infected_form(qa_q1_species kind, unsigned corpse) {
    static const q1_species corpses[] = {{QA_Q1_HELLKNIGHT,
                                          "monster_hell_knight",
                                          "progs/hknight.mdl",
                                          "h_hellkn",
                                          "hknight/sight1.wav",
                                          "hknight_corpse1",
                                          "hknight_walk1",
                                          "hknight_corpse1_rise0",
                                          "hknight_magicc1",
                                          250,
                                          -40,
                                          {{-32, -32, -24}, {32, 32, 64}},
                                          0,
                                          true},
                                         {QA_Q1_HELLKNIGHT,
                                          "monster_hell_knight",
                                          "progs/hknight.mdl",
                                          "h_hellkn",
                                          "hknight/sight1.wav",
                                          "hknight_corpse2",
                                          "hknight_walk1",
                                          "hknight_corpse2_rise0",
                                          "hknight_magicc1",
                                          250,
                                          -40,
                                          {{-32, -32, -24}, {32, 32, 64}},
                                          0,
                                          true}};
    if (kind == QA_Q1_HELLKNIGHT && corpse >= 1 && corpse <= 2)
        return &corpses[corpse - 1];
    return q1_species_find(kind == QA_Q1_ZOMBIE  ? "monster_zombie"
                           : kind == QA_Q1_DEMON ? "monster_demon1"
                                                 : "monster_hell_knight");
}

bool q1_infected_action(qa_q1_game *g, q1_actor *entity, q1_frame_action action, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    switch (action) {
    case Q1_ACTION_INFECTED_CORPSE_HOLD:
        return q1_schedule(g, entity, 9999, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_INFECTED_TEST_RISE: {
        entity->physics.solid = QA_PHYSICS_BOX;
        bool moved;
        if (!qa_physics_walk_move(g->services.physics, entity->id, 0, 0, (float)g->elapsed, true,
                                  true, &moved, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
        if (!moved) {
            entity->physics.solid = QA_PHYSICS_NOT_SOLID;
            m->next_frame = m->current_frame;
            return q1_link(g, entity, error) &&
                   q1_schedule(g, entity, 5, Q1_THINK_MONSTER_FRAME, error);
        }
        return q1_link(g, entity, error) &&
               q1_sound(g, entity->id, "infected/death1_rev.wav", 2, 1, error);
    }
    case Q1_ACTION_INFECTED_RISE_PAIN:
        m->pain_finished = g->time + 1.5;
        return true;
    case Q1_ACTION_INFECTED_RESURRECT:
        m->addon.risen = true;
        m->species = q1_infected_form(QA_Q1_HELLKNIGHT, 0);
        return q1_monster_ai(g, entity, Q1_AI_STAND, 0, error);
    default:
        qa_error_set(error, QA_ERROR_FORMAT, action, "invalid infected monster continuation");
        return false;
    }
}

static bool gibs(qa_q1_game *g, q1_actor *entity, qa_vec3 origin, float health, bool demon,
                 qa_error *error) {
    for (unsigned i = 0; i < 3; ++i) {
        if (!q1_gib_at(g, entity->id, origin, health,
                       demon    ? "gib1"
                       : i == 0 ? "gib1"
                       : i == 1 ? "gib2"
                                : "gib3",
                       error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
    }
    return true;
}
static bool retarget(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (!q1_ref_present(m->enemy) ||
        !q1_classnamed(g, q1_ref_actor(g, m->enemy),
                       m->species->species == QA_Q1_ZOMBIE ? "monster_zombie" : "monster_demon1"))
        return true;
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_snapshot_players(g, &snapshot, error))
        return false;
    qa_actor_id player = {0};
    for (size_t i = 0; i < snapshot->snapshot.count; ++i)
        if (q1_health(g, snapshot->snapshot.ids[i]) > 0) {
            player = snapshot->snapshot.ids[i];
            break;
        }
    qa_builtin_snapshot_release(snapshot);
    if (!player.registry)
        return true;
    q1_actor *rival = q1_entity(g, q1_ref_actor(g, m->enemy));
    if (rival && rival->kind == Q1_MONSTER &&
        q1_ref_equal(rival->state.monster.enemy, q1_ref_from(g, entity->id))) {
        rival->state.monster.enemy = q1_ref_from(g, player);
        rival->physics.enemy = q1_ref_from(g, player);
    }
    m->enemy = q1_ref_from(g, player);
    entity->physics.enemy = q1_ref_from(g, player);
    return true;
}
bool q1_infected_die(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    float health = q1_health(g, entity->id);
    if (m->addon.transformed) {
        if (!q1_sound(g, entity->id, "zombie/z_gib.wav", 2, 1, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
        if (!q1_gib_head(g, entity, "h_zombie", health, error) ||
            !qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        return gibs(g, entity, body.origin, health, false, error);
    }
    if (m->birth_epoch == UINT64_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, entity->id.slot, "Q1 monster birth epoch exhausted");
        return false;
    }
    if (!q1_sound(g, entity->id, "player/udeath.wav", 2, 1, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (m->addon.infected_kind == QA_Q1_ARMY &&
        !q1_gib_at(g, entity->id, body.origin, health, "h_guard", error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (!gibs(g, entity, body.origin, health, false, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    bool zombie = m->addon.infected_kind == QA_Q1_ARMY || m->addon.infected_kind == QA_Q1_KNIGHT;
    m->addon.transformed = true;
    m->addon.risen = true;
    m->species = q1_infected_form(zombie ? QA_Q1_ZOMBIE : QA_Q1_DEMON, 0);
    m->counted_death = false;
    entity->max_health = m->species->health;
    entity->aimed_damage = true;
    if (!q1_model(g, entity, m->species->model, error) ||
        !qa_builtin_resource(&g->services, zombie ? "monster_zombie" : "monster_demon1",
                             &entity->classname, error) ||
        !qa_combat_set_health(g->services.combat, entity->id, m->species->health, error))
        return false;
    qa_combat_state combat;
    if (!qa_combat_read_traits(g->services.combat, entity->id, &combat, error))
        return false;
    combat.can_take_damage = true;
    if (!qa_combat_set_traits(g->services.combat, entity->id, &combat, error))
        return false;
    ++m->birth_epoch;
    m->dead = false;
    if (zombie)
        entity->spawnflags = 128;
    else {
        m->addon.combat_style = 2;
        m->pain_finished = g->time + 1;
        m->attack_finished = 0;
    }
    if (!retarget(g, entity, error))
        return false;
    bool moved;
    if (!qa_physics_walk_move(g->services.physics, entity->id, 0, 0, (float)g->elapsed, true, true,
                              &moved, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (moved)
        return q1_monster_play(g, entity, zombie ? "zombie_paina1" : "demon1_pain1", error);
    m->counted_death = true;
    m->dead = true;
    if (!q1_monster_death_report(g, entity, q1_ref_actor(g, m->enemy), true, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (!qa_combat_set_health(g->services.combat, entity->id, -100, error) ||
        !q1_sound(g, entity->id, zombie ? "zombie/z_gib.wav" : "player/udeath.wav", 2, 1, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (zombie) {
        if (!q1_gib_head(g, entity, "h_zombie", -100, error) ||
            !qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        return gibs(g, entity, body.origin, -100, false, error);
    }
    return gibs(g, entity, body.origin, -100, true, error) &&
           (!q1_alive(g, entity->id) || q1_gib_head(g, entity, "h_demon", -100, error));
}
