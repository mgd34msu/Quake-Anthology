#include "internal.h"

static bool target_use(q1_save_io *io, qa_target_use *use) {
    Q1_SAVE(io, actor, use->source);
    Q1_SAVE(io, actor, use->activator);
    Q1_SAVE_ENUM(io, use->dialect, QA_CLOCK_Q3);
    Q1_SAVE(io, string, use->fields.classname);
    Q1_SAVE(io, string, use->fields.targetname);
    Q1_SAVE(io, string, use->fields.target);
    Q1_SAVE(io, string, use->fields.killtarget);
    Q1_SAVE(io, string, use->fields.message);
    Q1_SAVE(io, string, use->fields.shader_old);
    Q1_SAVE(io, string, use->fields.shader_new);
    Q1_SAVE(io, float, use->fields.delay_seconds);
    Q1_SAVE(io, float, use->fields.wait_seconds);
    Q1_SAVE(io, u64, use->time_ns);
    Q1_SAVE(io, bool, use->live_fields);
    return true;
}
static bool movement(q1_save_io *io, q1_map_movement *m, q1_door_group **groups, size_t count) {
    Q1_SAVE(io, vector, m->pos1);
    Q1_SAVE(io, vector, m->pos2);
    Q1_SAVE(io, vector, m->dest1);
    Q1_SAVE(io, vector, m->dest2);
    Q1_SAVE(io, vector, m->destination);
    uint32_t group = 0;
    if (io->values.direction == QA_SOURCE_SAVE_WRITE && m->group) {
        for (size_t i = 0; i < count; ++i)
            if (groups[i] == m->group) {
                group = (uint32_t)i + 1;
                break;
            }
        if (!group)
            return q1_save_fail(io, "Q1 door references an unowned group");
    }
    Q1_SAVE(io, u32, group);
    if (group > count)
        return q1_save_fail(io, "Invalid Q1 door group identity");
    if (io->values.direction == QA_SOURCE_SAVE_READ)
        m->group = group ? groups[group - 1] : NULL;
    Q1_SAVE_ENUM(io, m->done, Q1_MAP_CTF_NEXTLEVEL);
    Q1_SAVE_ENUM(io, m->position, Q1_MAP_DOWN);
    Q1_SAVE(io, actor, m->goal);
    Q1_SAVE(io, float, m->next_speed);
    Q1_SAVE(io, bool, m->moving);
    Q1_SAVE(io, bool, m->activated);
    Q1_SAVE(io, double, m->rogue.last_use);
    Q1_SAVE(io, double, m->rogue.last_move);
    Q1_SAVE(io, double, m->rogue.go_time);
    Q1_SAVE(io, float, m->rogue.floor);
    Q1_SAVE(io, float, m->rogue.target_floor);
    Q1_SAVE(io, u8, m->rogue.go_to);
    Q1_SAVE(io, bool, m->rogue.called);
    Q1_SAVE(io, bool, m->rogue.disabled);
    return true;
}
bool q1_save_map(q1_save_io *io, q1_map_state *m, q1_door_group **groups, size_t count) {
    Q1_SAVE_ENUM(io, m->kind, Q1_MAP_CTF_CHANGELEVEL);
    Q1_SAVE_ENUM(io, m->action, Q1_MAP_CTF_NEXTLEVEL);
    Q1_SAVE(io, string, m->original_model);
    Q1_SAVE(io, string, m->map);
    for (size_t i = 0; i < 4; ++i)
        Q1_SAVE(io, string, m->noise[i]);
    Q1_SAVE(io, string, m->endtext);
    Q1_SAVE(io, string, m->intermissiontext);
    Q1_SAVE(io, string, m->netname);
    Q1_SAVE(io, string, m->event);
    Q1_SAVE(io, string, m->mdl);
    Q1_SAVE(io, string, m->spawn_function);
    Q1_SAVE(io, string, m->spawn_classname);
    Q1_SAVE(io, string, m->group);
    Q1_SAVE(io, string, m->path);
    Q1_SAVE(io, string, m->category);
    Q1_SAVE(io, string, m->fog_info_entity);
    Q1_SAVE(io, vector, m->rotate);
    Q1_SAVE(io, vector, m->dest);
    Q1_SAVE(io, vector, m->dest2);
    Q1_SAVE(io, vector, m->pos2);
    Q1_SAVE(io, vector, m->particle_size);
    Q1_SAVE(io, vector, m->fog_color);
    Q1_SAVE(io, float, m->fog_density);
    Q1_SAVE(io, vector, m->movedir);
    Q1_SAVE(io, vector, m->mangle);
    Q1_SAVE(io, vector, m->view_offset);
    Q1_SAVE(io, float, m->height);
    Q1_SAVE(io, float, m->lip);
    Q1_SAVE(io, float, m->width);
    Q1_SAVE(io, float, m->length);
    Q1_SAVE(io, float, m->pause_time);
    Q1_SAVE(io, float, m->volume);
    Q1_SAVE(io, float, m->duration);
    Q1_SAVE(io, float, m->distance);
    Q1_SAVE(io, float, m->initial_think);
    Q1_SAVE(io, float, m->spawn_multi);
    Q1_SAVE(io, float, m->spawn_silent);
    Q1_SAVE(io, float, m->gravity);
    Q1_SAVE(io, float, m->current_ammo);
    Q1_SAVE(io, float, m->weapon);
    Q1_SAVE(io, float, m->frags);
    Q1_SAVE(io, i32, m->sounds);
    Q1_SAVE(io, i32, m->style);
    Q1_SAVE(io, i32, m->color_map);
    Q1_SAVE(io, i32, m->impulse);
    Q1_SAVE(io, u32, m->inline_model);
    Q1_SAVE(io, u32, m->coop_weapons);
    Q1_SAVE(io, float, m->counter_value);
    Q1_SAVE(io, float, m->goal_state);
    Q1_SAVE(io, float, m->field_state);
    Q1_SAVE(io, i32, m->particle_color);
    Q1_SAVE(io, double, m->cooldown);
    Q1_SAVE(io, double, m->active_until);
    Q1_SAVE(io, bool, m->has_inline_model);
    Q1_SAVE(io, bool, m->has_movedir);
    Q1_SAVE(io, bool, m->has_view_offset);
    Q1_SAVE(io, bool, m->touch_enabled);
    Q1_SAVE(io, bool, m->use_enabled);
    Q1_SAVE(io, bool, m->dormant);
    Q1_SAVE(io, bool, m->effect_active);
    Q1_SAVE(io, bool, m->has_dest2);
    Q1_SAVE(io, bool, m->electrode_button);
    if (m->electrode_button && m->kind != Q1_MAP_BUTTON)
        return q1_save_fail(io, "Electrode button belongs to a different map continuation");
    Q1_SAVE(io, bool, m->spawn_template.valid);
    if (m->spawn_template.valid) {
        Q1_SAVE(io, string, m->spawn_template.model);
        Q1_SAVE(io, bounds, m->spawn_template.bounds);
        Q1_SAVE_ENUM(io, m->spawn_template.solid, QA_PHYSICS_CORPSE);
        Q1_SAVE_ENUM(io, m->spawn_template.think, Q1_THINK_SPAWN_TEMPLATE);
    }
    if ((m->action == Q1_MAP_DELAYED_USE || m->action == Q1_MAP_FINALE_TIMER ||
         m->action == Q1_MAP_FOREIGN_REMOVE) &&
        m->kind != Q1_MAP_DELAY)
        return q1_save_fail(io, "Q1 delayed callback belongs to a different map continuation");
    if (m->action == Q1_MAP_ROGUE_RUBBLE_THROW && m->kind != Q1_MAP_ROGUE_RUBBLE_SOURCE)
        return q1_save_fail(io, "Rogue rubble callback belongs to a different map continuation");
    if (m->action == Q1_MAP_CTF_NEXTLEVEL && m->kind != Q1_MAP_DELAY &&
        m->kind != Q1_MAP_CTF_CHANGELEVEL)
        return q1_save_fail(io, "ThreeWave nextlevel callback belongs to a different continuation");
    if (m->action >= Q1_MAP_MINE_FIRST && m->action <= Q1_MAP_GRAVITY_PULL &&
        !q1_map_is_hip_hazard(m->kind))
        return q1_save_fail(io, "Hipnotic hazard callback belongs to a different map continuation");
    if (m->action >= Q1_MAP_ROTATE_FIRST && m->action <= Q1_MAP_ROTATE_TRAIN_TICK &&
        !q1_map_is_rotation(m->kind))
        return q1_save_fail(io, "Hipnotic rotation callback belongs to a different map continuation");
    if (m->action == Q1_MAP_LIGHT_RAMP_INIT && m->kind != Q1_MAP_LIGHT_RAMP)
        return q1_save_fail(io, "Light ramp initialization belongs to a different map continuation");
    if (m->action >= Q1_MAP_ADDON_BOB_STEP && m->action <= Q1_MAP_ADDON_BREAKABLE_STOP &&
        !q1_map_addon_action_matches(m->kind,m->action))
        return q1_save_fail(io, "Addon brush callback belongs to a different map continuation");
    if (m->action >= Q1_MAP_ADDON_COUNTER_RESET && m->action <= Q1_MAP_ADDON_EXPLOSION_FIRE &&
        !q1_map_addon_trigger_action_matches(m->kind, m->action))
        return q1_save_fail(io, "Addon trigger callback belongs to a different map continuation");
    if ((m->action == Q1_MAP_CAMPAIGN_USE_TARGETS || m->action == Q1_MAP_SIGIL_FIX) &&
        !q1_map_campaign_action_matches(m->kind, m->action))
        return q1_save_fail(io, "Addon campaign callback belongs to a different map continuation");
    if (m->action == Q1_MAP_ADDON_EXPLOSION_REPEAT &&
        m->kind != Q1_MAP_ADDON_EXPLOSION_REPEATER)
        return q1_save_fail(io, "Explosion repeater callback belongs to a different map continuation");
    if (m->action >= Q1_MAP_ADDON_SHAKE_TICK && m->action <= Q1_MAP_ADDON_PARTICLE_TICK &&
        !q1_map_addon_effect_action_matches(m->kind, m->action))
        return q1_save_fail(io, "Addon effect callback belongs to a different map continuation");
    if (m->kind == Q1_MAP_ADDON_EXPLOSION_REPEATER) {
        Q1_SAVE(io, actor, m->pending.addon.chain);
        return true;
    }
    if (q1_map_is_addon_brush(m->kind)) {
        Q1_SAVE(io, vector, m->pending.brush.origin);
        Q1_SAVE(io, u8, m->pending.brush.phase);
        unsigned maximum = m->kind == Q1_MAP_ADDON_BOB || m->kind == Q1_MAP_ADDON_HURT ? 1 : 3;
        return m->pending.brush.phase <= maximum || q1_save_fail(io,"Invalid addon brush phase");
    }
    if (m->action == Q1_MAP_DELAYED_USE)
        return target_use(io, &m->pending.delayed);
    if (m->action == Q1_MAP_FINALE_TIMER) {
        Q1_SAVE_ENUM(io, m->pending.finale, QA_Q1_CAMPAIGN_FINISH_FINALE);
        return true;
    }
    if (q1_map_is_mover(m->kind) || q1_map_is_rogue_plat(m->kind))
        return movement(io, &m->pending.mover, groups, count);
    if (q1_map_is_addon_visual(m->kind)) {
        Q1_SAVE(io, actor, m->pending.addon.chain);
        Q1_SAVE(io, vector, m->pending.addon.origin);
        Q1_SAVE(io, u8, m->pending.addon.phase);
        return m->pending.addon.phase <= 3 || q1_save_fail(io, "Invalid Q1 light ramp state");
    }
    if (q1_map_is_rotation(m->kind)) {
        q1_map_rotation *r = &m->pending.rotation;
        Q1_SAVE(io, vector, r->origin);
        Q1_SAVE(io, vector, r->rate);
        Q1_SAVE(io, vector, r->dest1);
        Q1_SAVE(io, vector, r->dest2);
        Q1_SAVE(io, vector, r->destination);
        Q1_SAVE(io, vector, r->final_angle);
        Q1_SAVE(io, vector, r->final_destination);
        Q1_SAVE(io, actor, r->goal);
        Q1_SAVE(io, double, r->last_time);
        Q1_SAVE(io, double, r->end_time);
        Q1_SAVE(io, double, r->progress_start);
        Q1_SAVE(io, double, r->inverse_duration);
        Q1_SAVE(io, u8, r->phase);
        Q1_SAVE(io, u8, r->next);
        Q1_SAVE(io, bool, r->linked);
        return (r->phase <= 7 && r->next <= 3) ||
               q1_save_fail(io, "Invalid Hipnotic rotation phase");
    }
    if (q1_map_is_hip_hazard(m->kind)) {
        Q1_SAVE(io, actor, m->pending.hazard.enemy);
        Q1_SAVE(io, actor, m->pending.hazard.last_victim);
        Q1_SAVE(io, vector, m->pending.hazard.endpoint);
        Q1_SAVE(io, double, m->pending.hazard.search_until);
        Q1_SAVE(io, double, m->pending.hazard.pulse_until);
        Q1_SAVE(io, double, m->pending.hazard.cycle_until);
        Q1_SAVE(io, double, m->pending.hazard.sound_after);
        Q1_SAVE(io, double, m->pending.hazard.switch_due);
        Q1_SAVE(io, u8, m->pending.hazard.attack);
        Q1_SAVE(io, bool, m->pending.hazard.enabled);
        Q1_SAVE(io, bool, m->pending.hazard.pulsing);
        Q1_SAVE(io, bool, m->pending.hazard.killed);
        return m->pending.hazard.attack <= 3 ||
               q1_save_fail(io, "Invalid Hipnotic hazard attack state");
    }
    switch (m->kind) {
    case Q1_MAP_HORDE_MANAGER:
        Q1_SAVE(io, u32, m->pending.horde_start_flags);
        break;
    case Q1_MAP_PUSHABLE:
    case Q1_MAP_PUSHABLE_PROXY:
        Q1_SAVE(io, vector, m->pending.push_origin);
        break;
    case Q1_MAP_SPAWNER:
        Q1_SAVE(io, actor, m->pending.spawn_master);
        break;
    case Q1_MAP_PENDULUM:
        Q1_SAVE(io, u8, m->pending.pendulum_step);
        if (m->pending.pendulum_step >= 26)
            return q1_save_fail(io, "Invalid Q1 pendulum phase");
        break;
    case Q1_MAP_TIME_MACHINE:
    case Q1_MAP_TIME_CORE:
    case Q1_MAP_TIME_BOOM:
    case Q1_MAP_TIME_STOP:
        Q1_SAVE_ENUM(io, m->pending.time_reaction, Q1_TIME_CRASH);
        break;
    case Q1_MAP_ENDING_ACTOR:
    case Q1_MAP_BUZZSAW:
        Q1_SAVE(io, actor, m->pending.follower.move_target);
        Q1_SAVE(io, vector, m->pending.follower.view_angles);
        Q1_SAVE(io, float, m->pending.follower.rockets);
        Q1_SAVE(io, u8, m->pending.follower.fire_stage);
        break;
    case Q1_MAP_HIP_COUNTER:
        Q1_SAVE(io, float, m->pending.counter.ticks);
        Q1_SAVE(io, bool, m->pending.counter.running);
        Q1_SAVE(io, bool, m->pending.counter.stop_after_cycle);
        break;
    case Q1_MAP_BOBBING_WATER:
        Q1_SAVE(io, float, m->pending.bob.amplitude);
        Q1_SAVE(io, double, m->pending.bob.last_time);
        break;
    case Q1_MAP_PARTICLE_FIELD:
        Q1_SAVE(io, vector, m->pending.particles.start);
        Q1_SAVE(io, vector, m->pending.particles.end);
        {
            uint32_t plane = m->pending.particles.plane;
            Q1_SAVE(io, u32, plane);
            if (plane > 2)
                return q1_save_fail(io, "Invalid Q1 particle field plane");
            if (io->values.direction == QA_SOURCE_SAVE_READ)
                m->pending.particles.plane = plane;
        }
        break;
    case Q1_MAP_SHELTER:
        Q1_SAVE(io, vector, m->pending.addon.origin);
        break;
    default:
        break;
    }
    return true;
}
bool q1_save_map_runtime(q1_save_io *io, q1_map_runtime *m) {
    Q1_SAVE(io, string, m->options.current_map);
    Q1_SAVE(io, bool, m->options.registered);
    Q1_SAVE(io, actor, m->world_actor);
    for (size_t i = 0; i < 2; ++i)
        Q1_SAVE(io, actor, m->electrodes[i]);
    Q1_SAVE(io, actor, m->time_machine);
    Q1_SAVE(io, actor, m->ending_actor);
    Q1_SAVE(io, double, m->lightning_end);
    Q1_SAVE(io, float, m->pendulum_impact);
    Q1_SAVE(io, float, m->elevator_direction);
    Q1_SAVE(io, u32, m->finale.stage);
    Q1_SAVE(io, string, m->finale.map);
    Q1_SAVE(io, string, m->finale.text);
    Q1_SAVE(io, vector, m->finale.origin);
    Q1_SAVE(io, vector, m->finale.angles);
    Q1_SAVE(io, double, m->finale.exit_after);
    Q1_SAVE(io, bool, m->finale_started);
    Q1_SAVE(io, bool, m->finale_dismissed);
    Q1_SAVE(io, double, m->earthquake_end);
    Q1_SAVE(io, bool, m->quake_active);
    Q1_SAVE(io, bool, m->dump_coordinates);
    Q1_SAVE(io, actor, m->ctf_vote_leader);
    Q1_SAVE(io, double, m->ctf_vote_exit_time);
    Q1_SAVE(io, bool, m->final_new_game_travel);
    Q1_SAVE(io, bool, m->rogue_cutscene);
    Q1_SAVE(io, bool, m->rogue_ending_started);
    Q1_SAVE(io, bool, m->rogue_quake_active);
    Q1_SAVE(io, float, m->rogue_quake_intensity);
    Q1_SAVE(io, u8, m->rogue_actor_stage);
    Q1_SAVE(io, u32, m->total_secrets);
    Q1_SAVE(io, u32, m->found_secrets);
    Q1_SAVE(io, u32, m->frame_tick_count);
    if (m->frame_tick_count > io->game->capacity)
        return q1_save_fail(io, "Too many Q1 authored frame callbacks");
    if ((io->values.direction == QA_SOURCE_SAVE_READ) && m->frame_tick_count) {
        m->frame_ticks = calloc(io->game->capacity, sizeof(*m->frame_ticks));
        if (!m->frame_ticks) {
            qa_error_set(io->values.error, QA_ERROR_MEMORY, io->values.offset, "allocating saved Q1 frame callbacks");
            return false;
        }
    }
    for (uint32_t i = 0; i < m->frame_tick_count; ++i) {
        Q1_SAVE(io, actor, m->frame_ticks[i]);
        if (!m->frame_ticks[i].registry || m->frame_ticks[i].slot >= io->game->capacity)
            return q1_save_fail(io, "Invalid Q1 authored frame callback actor");
        for (uint32_t j = 0; j < i; ++j)
            if (qa_actor_id_equal(m->frame_ticks[i], m->frame_ticks[j]))
                return q1_save_fail(io, "Duplicate Q1 authored frame callback");
    }
    uint32_t count = 0;
    if (io->values.direction == QA_SOURCE_SAVE_WRITE && m->rotated_targets)
        for (uint32_t slot = 0; slot < io->game->capacity; ++slot)
            count += m->rotated_targets[slot].actor.registry != 0;
    Q1_SAVE(io, u32, count);
    if (count > io->game->capacity)
        return q1_save_fail(io, "Too many Q1 rotation targets");
    if ((io->values.direction == QA_SOURCE_SAVE_READ) && count) {
        m->rotated_targets = calloc(io->game->capacity, sizeof(*m->rotated_targets));
        if (!m->rotated_targets) {
            qa_error_set(io->values.error, QA_ERROR_MEMORY, io->values.offset, "allocating saved Q1 rotation targets");
            return false;
        }
    }
    uint32_t next = 0;
    for (uint32_t i = 0; i < count; ++i) {
        q1_rotate_target saved = {0};
        q1_rotate_target *row = &saved;
        if (io->values.direction == QA_SOURCE_SAVE_WRITE) {
            while (next < io->game->capacity && !m->rotated_targets[next].actor.registry)
                ++next;
            row = &m->rotated_targets[next++];
        }
        Q1_SAVE(io, actor, row->actor);
        Q1_SAVE(io, actor, row->owner);
        Q1_SAVE(io, vector, row->original);
        Q1_SAVE(io, vector, row->current);
        Q1_SAVE(io, u8, row->type);
        if (!row->actor.registry || row->actor.slot >= io->game->capacity || row->type > 2)
            return q1_save_fail(io, "Invalid Q1 rotation target");
        if (io->values.direction == QA_SOURCE_SAVE_READ) {
            if (m->rotated_targets[row->actor.slot].actor.registry)
                return q1_save_fail(io, "Duplicate Q1 rotation target");
            m->rotated_targets[row->actor.slot] = *row;
        }
    }
    count = 0;
    if (io->values.direction == QA_SOURCE_SAVE_WRITE && m->addon_contacts)
        for (uint32_t slot = 0; slot < io->game->capacity; ++slot)
            count += m->addon_contacts[slot].actor.registry != 0;
    Q1_SAVE(io, u32, count);
    if (count > io->game->capacity)
        return q1_save_fail(io, "Too many Q1 addon contact continuations");
    if ((io->values.direction == QA_SOURCE_SAVE_READ) && count) {
        m->addon_contacts = calloc(io->game->capacity, sizeof(*m->addon_contacts));
        if (!m->addon_contacts) {
            qa_error_set(io->values.error, QA_ERROR_MEMORY, io->values.offset, "allocating saved Q1 addon contacts");
            return false;
        }
    }
    next = 0;
    for (uint32_t i = 0; i < count; ++i) {
        q1_addon_contact saved = {0};
        q1_addon_contact *row = &saved;
        if (io->values.direction == QA_SOURCE_SAVE_WRITE) {
            while (next < io->game->capacity && !m->addon_contacts[next].actor.registry)
                ++next;
            row = &m->addon_contacts[next++];
        }
        Q1_SAVE(io, actor, row->actor);
        Q1_SAVE(io, actor, row->fog_active);
        Q1_SAVE(io, actor, row->secret_marker);
        Q1_SAVE(io, actor, row->exit_marker);
        Q1_SAVE(io, vector, row->fog_color);
        Q1_SAVE(io, float, row->fog_density);
        Q1_SAVE(io, double, row->fly_sound);
        Q1_SAVE(io, double, row->lore_active);
        Q1_SAVE(io, double, row->voted);
        Q1_SAVE(io, float, row->hunger_time);
        Q1_SAVE(io, float, row->super_time);
        Q1_SAVE(io, bool, row->has_hunger);
        Q1_SAVE(io, bool, row->sheltered);
        Q1_SAVE(io, bool, row->secret_hunter);
        Q1_SAVE(io, bool, row->exit_hunter);
        Q1_SAVE(io, bool, row->monster_hunter);
        Q1_SAVE(io, bool, row->buddha);
        Q1_SAVE(io, u32, row->effects);
        if (row->effects & ~8u)
            return q1_save_fail(io, "Invalid MG3 authored player effect");
        if (!row->actor.registry || row->actor.slot >= io->game->capacity)
            return q1_save_fail(io, "Invalid Q1 addon contact actor");
        if (io->values.direction == QA_SOURCE_SAVE_READ) {
            if (m->addon_contacts[row->actor.slot].actor.registry)
                return q1_save_fail(io, "Duplicate Q1 addon contact actor");
            m->addon_contacts[row->actor.slot] = *row;
        }
    }
    return true;
}
