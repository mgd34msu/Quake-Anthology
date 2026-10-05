#include "internal.h"

static bool species(q1_save_io *io, q1_monster *m) {
    const char *name = (io->values.direction == QA_SOURCE_SAVE_READ) ? NULL : m->species ? m->species->classname : NULL;
    if (!name && io->values.direction == QA_SOURCE_SAVE_WRITE)
        return q1_save_fail(io, "Q1 monster has no immutable species");
    uint8_t corpse = 0;
    if (io->values.direction == QA_SOURCE_SAVE_WRITE)
        for (uint8_t i = 1; i <= 2; ++i)
            if (m->species == q1_infected_form(QA_Q1_HELLKNIGHT, i))
                corpse = i;
    if (!q1_save_literal(io, &name) || !q1_save_u8(io, &corpse))
        return false;
    if (corpse > 2 || (corpse && strcmp(name, "monster_hell_knight")))
        return q1_save_fail(io, "Invalid saved Q1 corpse species");
    if (io->values.direction == QA_SOURCE_SAVE_READ) {
        m->species = corpse ? q1_infected_form(QA_Q1_HELLKNIGHT, corpse) : q1_species_find(name);
        if (!m->species)
            return q1_save_fail(io, "Unknown saved Q1 monster species");
    }
    return true;
}

bool q1_save_monster(q1_save_io *io, q1_monster *m) {
    if (!species(io, m))
        return false;
    Q1_SAVE(io, u64, m->birth_epoch);
    Q1_SAVE(io, bool, m->dead);
    if (!m->birth_epoch)
        return q1_save_fail(io, "Q1 monster continuation has no semantic birth");
    Q1_SAVE(io, string, m->path);
    Q1_SAVE(io, u16, m->current_frame);
    Q1_SAVE(io, u16, m->next_frame);
    if ((m->current_frame != UINT16_MAX && m->current_frame >= q1_frame_count) ||
        (m->next_frame != UINT16_MAX && m->next_frame >= q1_frame_count))
        return q1_save_fail(io, "Invalid Q1 monster frame continuation");
    Q1_SAVE(io, ref, m->enemy);
    Q1_SAVE(io, ref, m->old_enemy);
    Q1_SAVE(io, ref, m->charmer);
    Q1_SAVE(io, ref, m->charm_goal);
    Q1_SAVE(io, ref, m->move_target);
    Q1_SAVE(io, ref, m->previous_corner);
    Q1_SAVE(io, double, m->pause_until);
    Q1_SAVE(io, double, m->attack_finished);
    Q1_SAVE(io, double, m->pain_finished);
    Q1_SAVE(io, double, m->search_until);
    Q1_SAVE(io, double, m->idle_until);
    Q1_SAVE(io, double, m->straight_after);
    Q1_SAVE(io, double, m->dodge_after);
    Q1_SAVE(io, double, m->hostile_until);
    Q1_SAVE(io, double, m->follow_until);
    Q1_SAVE(io, u32, m->counter);
    Q1_SAVE(io, u32, m->lightning_count);
    Q1_SAVE(io, u8, m->attack_state);
    Q1_SAVE(io, u8, m->in_pain);
    Q1_SAVE(io, u8, m->hunting_charmer);
    Q1_SAVE(io, bool, m->sliding);
    Q1_SAVE(io, bool, m->lefty);
    Q1_SAVE(io, bool, m->counted_death);
    Q1_SAVE(io, bool, m->jump_touch);
    Q1_SAVE(io, bool, m->horde);
    Q1_SAVE(io, bool, m->path_end);
    Q1_SAVE(io, bool, m->addon.enabled);
    Q1_SAVE(io, bool, m->addon.waiting);
    Q1_SAVE(io, bool, m->addon.path_wait);
    Q1_SAVE(io, bool, m->addon.started);
    Q1_SAVE(io, bool, m->addon.rocket_ogre);
    Q1_SAVE(io, bool, m->addon.allow_path);
    Q1_SAVE(io, bool, m->addon.normal_use);
    Q1_SAVE(io, bool, m->addon.infected);
    Q1_SAVE(io, bool, m->addon.transformed);
    Q1_SAVE(io, bool, m->addon.risen);
    Q1_SAVE(io, bool, m->addon.infection_count_pending);
    Q1_SAVE(io, bool, m->addon.demodog);
    Q1_SAVE(io, u8, m->addon.infected_kind);
    Q1_SAVE(io, u8, m->addon.corpse);
    Q1_SAVE_ENUM(io, m->addon.heavy, Q1_HEAVY_RUNE_KNIGHT);
    Q1_SAVE_ENUM(io, m->addon.boss, Q1_BOSS_FINAL);
    Q1_SAVE(io, u8, m->addon.projectiles);
    Q1_SAVE(io, u8, m->addon.projectile_max);
    Q1_SAVE(io, u8, m->addon.combat_style);
    Q1_SAVE(io, double, m->addon.damage_at);
    if (m->addon.boss) {
        Q1_SAVE(io, vector, m->source.boss.anchor);
        Q1_SAVE(io, vector, m->source.boss.destination);
        Q1_SAVE(io, u16, m->source.boss.death_frame);
        Q1_SAVE(io, i32, m->source.boss.shots);
        Q1_SAVE(io, i32, m->source.boss.shocks);
        Q1_SAVE(io, bool, m->source.boss.touch);
        Q1_SAVE(io, bool, m->source.boss.immune);
        Q1_SAVE(io, bool, m->source.boss.awake);
        Q1_SAVE(io, bool, m->source.boss.swipe_side);
        Q1_SAVE(io, bool, m->source.boss.vortex_side);
        Q1_SAVE(io, u32, m->source.boss.phase);
        Q1_SAVE(io, u32, m->source.boss.cycles);
        Q1_SAVE(io, u32, m->source.boss.stage);
        for (size_t i = 0; i < 4; ++i)
            Q1_SAVE(io, string, m->source.boss.waves[i]);
    } else if (m->addon.heavy) {
        Q1_SAVE(io, ref, m->source.heavy.child);
        Q1_SAVE(io, u32, m->source.heavy.lightning_count);
        Q1_SAVE(io, i32, m->source.heavy.nails);
    } else
        switch (m->species->species) {
        case QA_Q1_EEL:
            Q1_SAVE(io, i16, m->source.eel.pitch);
            break;
        case QA_Q1_MUMMY:
            Q1_SAVE(io, bool, m->source.mummy.asleep);
            break;
        case QA_Q1_SWORD:
            Q1_SAVE(io, bool, m->source.sword.awakened);
            Q1_SAVE(io, bool, m->source.sword.pain_disabled);
            break;
        case QA_Q1_SCOURGE:
            Q1_SAVE(io, ref, m->source.scourge.trigger);
            Q1_SAVE(io, double, m->source.scourge.dodge_until);
            Q1_SAVE(io, bool, m->source.scourge.initialized);
            Q1_SAVE(io, bool, m->source.scourge.silent);
            Q1_SAVE(io, bool, m->source.scourge.previous_silent);
            break;
        case QA_Q1_MORPH:
            Q1_SAVE(io, u32, m->source.morph.children);
            break;
        case QA_Q1_DRAGON:
            Q1_SAVE(io, vector, m->source.dragon.last_velocity);
            Q1_SAVE(io, u16, m->source.dragon.missile);
            Q1_SAVE(io, u8, m->source.dragon.pain_sequence);
            Q1_SAVE(io, u8, m->source.dragon.death_state);
            Q1_SAVE(io, bool, m->source.dragon.attacking);
            break;
        case QA_Q1_ARMAGON:
            Q1_SAVE(io, ref, m->source.armagon.body);
            Q1_SAVE(io, vector, m->source.armagon.old_origin);
            Q1_SAVE(io, float, m->source.armagon.torso_yaw);
            Q1_SAVE(io, float, m->source.armagon.aim_threshold);
            Q1_SAVE(io, double, m->source.armagon.idle_at);
            Q1_SAVE(io, u8, m->source.armagon.repulse_state);
            Q1_SAVE(io, bool, m->source.armagon.behind);
            break;
        case QA_Q1_GREMLIN:
            Q1_SAVE(io, ref, m->source.gremlin.last_victim);
            Q1_SAVE(io, ref, m->source.gremlin.flee_goal);
            Q1_SAVE(io, vector, m->source.gremlin.view_angles);
            Q1_SAVE_ENUM(io, m->source.gremlin.weapon, QA_Q1_WEAPON_COUNT);
            Q1_SAVE(io, float, m->source.gremlin.current_ammo);
            Q1_SAVE(io, double, m->source.gremlin.protection_sound);
            Q1_SAVE(io, u8, m->source.gremlin.touch);
            Q1_SAVE(io, bool, m->source.gremlin.stolen);
            Q1_SAVE(io, bool, m->source.gremlin.gorging);
            Q1_SAVE(io, bool, m->source.gremlin.pain_disabled);
            break;
        default:
            break;
        }
    return true;
}
