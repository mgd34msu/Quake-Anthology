#include "internal.h"

void qa_modes_checkpoint_free(qa_modes_checkpoint *saved) {
    if (!saved)
        return;
    for (size_t i = 0; saved->modes && i < saved->mode_count; ++i) {
        qa_mode_checkpoint *v = &saved->modes[i];
        free(v->members);
        free(v->ghosts);
        free(v->spawns);
        free(v->items);
        mode_horde_checkpoint_free(v->horde);
    }
    free(saved->players);
    free(saved->modes);
    free(saved->objects);
    free(saved->external_objectives);
    free(saved->mode_generations);
    *saved = (qa_modes_checkpoint){0};
}
static void capture_instance_fields(const mode_instance *v, qa_mode_checkpoint *out) {
    *out = (qa_mode_checkpoint){.id = v->id,
                                .value = v->value,
                                .ball = v->ball,
                                .tag = v->tag,
                                .tag_owner = v->tag_owner,
                                .last_ball_touch = v->last_ball_touch,
                                .ready_since_ns = v->ready_since_ns,
                                .next_second_ns = v->next_second_ns,
                                .tag_count = v->tag_count,
                                .remaining_seconds = v->remaining_seconds,
                                .countdown_announced = v->countdown_announced,
                                .restart_sent = v->restart_sent,
                                .relic_spawn_ns = v->relic_spawn_ns,
                                .team_location_ns = v->team_location_ns,
                                .rune_cursor = v->rune_cursor,
                                .rogue_spawn_spot = v->rogue_spawn_spot,
                                .relics_started = v->relics_started,
                                .rune_forward = v->rune_forward,
                                .q3_settings = v->q3_settings,
                                .q3_settings_present = v->q3_settings_present,
                                .q3_started_ms = v->q3_started_ms,
                                .q3_warmup_ms = v->q3_warmup_ms,
                                .q3_warmup_seen = v->q3_warmup_seen,
                                .next_location = v->next_location};
    memcpy(out->bases, v->bases, sizeof(out->bases));
    memcpy(out->votes, v->votes, sizeof(out->votes));
    memcpy(out->last_spawns, v->last_spawns, sizeof(out->last_spawns));
    memcpy(out->vote_started, v->vote_started, sizeof(out->vote_started));
    memcpy(out->flag_sound_ns, v->flag_sound_ns, sizeof(out->flag_sound_ns));
    memcpy(out->attack_sound_ns, v->attack_sound_ns, sizeof(out->attack_sound_ns));
}
static qa_mode_object_checkpoint capture_object(const mode_object *o) {
    return (qa_mode_object_checkpoint){.actor = o->actor,
                                       .mode = o->mode,
                                       .spec = o->spec,
                                       .value = o->value,
                                       .home = o->home,
                                       .base = o->base,
                                       .dropped_actor = o->dropped_actor,
                                       .collision = o->collision,
                                       .physics = o->physics,
                                       .next_ns = o->next_ns,
                                       .owner_until_ns = o->owner_until_ns,
                                       .animation_ns = o->animation_ns,
                                       .expire_ns = o->expire_ns,
                                       .born_ns = o->born_ns,
                                       .targets_used = o->targets_used,
                                       .has_physics = o->has_physics,
                                       .dropped = o->dropped,
                                       .global_animation = o->global_animation,
                                       .tag_stage = o->tag_stage};
}
static bool checkpoint_capture(qa_modes *m, qa_modes_checkpoint *out, qa_error *e) {
    for (uint32_t i = 0; i < m->mode_capacity; ++i)
        if (m->instances[i].active && !mode_relic_source_current(m, &m->instances[i], e)) return false;
    qa_modes_checkpoint saved = {
        .version = 8, .random = m->random, .attack_sequence = m->attack_sequence};
    saved.players = calloc(m->actor_capacity, sizeof(*saved.players));
    saved.modes = calloc(m->mode_capacity, sizeof(*saved.modes));
    saved.objects = calloc(m->actor_capacity, sizeof(*saved.objects));
    saved.external_objectives = calloc(m->objective_capacity, sizeof(*saved.external_objectives));
    saved.mode_generations = calloc(m->mode_capacity, sizeof(*saved.mode_generations));
    saved.generation_count = m->mode_capacity;
    if (!saved.players || !saved.modes || !saved.objects || !saved.external_objectives ||
        !saved.mode_generations)
        goto memory;
    for (uint32_t i = 0; i < m->actor_capacity; ++i) {
        mode_player *p = &m->players[i];
        mode_object *o = &m->objects[i];
        if (p->active && mode_live(m, p->value.actor)) {
            qa_mode_player_checkpoint player = {.value = p->value};
            saved.players[saved.player_count++] = player;
        }
        if (o->active && mode_live(m, o->actor))
            saved.objects[saved.object_count++] = capture_object(o);
    }
    for (uint32_t i = 0; i < m->mode_capacity; ++i) {
        mode_instance *v = &m->instances[i];
        saved.mode_generations[i] = v->id.generation;
        if (!v->active)
            continue;
        if (!mode_update_ghosts(m, v, e)) {
            qa_modes_checkpoint_free(&saved);
            return false;
        }
        qa_mode_checkpoint *entry = &saved.modes[saved.mode_count++];
        capture_instance_fields(v, entry);
        entry->members = calloc(m->actor_capacity, sizeof(*entry->members));
        entry->ghosts = calloc(m->actor_capacity, sizeof(*entry->ghosts));
        entry->spawns = v->spawn_count ? malloc(v->spawn_count * sizeof(*entry->spawns)) : NULL;
        entry->items = v->item_count ? malloc(v->item_count * sizeof(*entry->items)) : NULL;
        if (!entry->members || !entry->ghosts || (v->spawn_count && !entry->spawns) ||
            (v->item_count && !entry->items))
            goto memory;
        entry->item_count = v->item_count;
        if (v->item_count)
            memcpy(entry->items, v->items, v->item_count * sizeof(*entry->items));
        entry->spawn_count = v->spawn_count;
        if (v->spawn_count)
            memcpy(entry->spawns, v->spawns, v->spawn_count * sizeof(*entry->spawns));
        for (uint32_t j = 0; j < m->actor_capacity; ++j) {
            if (v->members[j].joined && mode_live(m, v->members[j].actor)) {
                mode_member member = v->members[j];
                if (member.external_owner) {
                    member.player.score = 0;
                    member.player.team = 0;
                }
                entry->members[entry->member_count++] = member;
            }
            if (v->ghosts[j].code)
                entry->ghosts[entry->ghost_count++] = v->ghosts[j];
        }
        if (!mode_horde_capture(m, v, &entry->horde, e)) {
            qa_modes_checkpoint_free(&saved);
            return false;
        }
    }
    for (uint32_t i = 0; i < m->objective_capacity; ++i) {
        mode_objective *o = &m->objectives[i];
        if (!o->active)
            continue;
        bool native = false;
        for (uint32_t j = 0; j < m->actor_capacity; ++j)
            if (m->objects[j].active && m->objects[j].objective.slot == i &&
                m->objects[j].objective.serial == o->serial) {
                native = true;
                break;
            }
        if (!native)
            saved.external_objectives[saved.external_objective_count++] =
                (qa_mode_objective_checkpoint){.mode = o->binding.mode, .id = o->binding.id,
                                               .owner = o->binding.owner,
                                               .campaign_gate = o->binding.campaign_gate,
                                               .bot_goal = o->binding.bot_goal};
    }
    *out = saved;
    return true;
memory:
    qa_modes_checkpoint_free(&saved);
    qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating mode checkpoint");
    return false;
}
bool qa_modes_checkpoint_capture(qa_modes *m, qa_modes_checkpoint *out, qa_error *e) {
    if (!m || !out || m->callback_depth)
        return mode_fail(e, "mode checkpoint requires an idle boundary");
    return MODE_CALLBACK(m, checkpoint_capture(m, out, e));
}
static bool reference(qa_modes *m, qa_actor_id actor) {
    return !actor.registry || mode_live(m, actor);
}
static bool bounds_valid(qa_bounds b) {
    return qa_vec_finite(b.mins) && qa_vec_finite(b.maxs) && b.mins.x <= b.maxs.x &&
           b.mins.y <= b.maxs.y && b.mins.z <= b.maxs.z;
}
static bool physics_valid(qa_modes *m, const qa_physics_properties *p) {
    return p->family >= QA_COLLISION_Q1 && p->family <= QA_COLLISION_Q3 &&
           p->motion >= QA_PHYSICS_STATIONARY && p->motion <= QA_PHYSICS_STEP &&
           p->solid >= QA_PHYSICS_NOT_SOLID && p->solid <= QA_PHYSICS_CORPSE &&
           qa_vec_finite(p->angular_velocity) && qa_vec_finite(p->gravity_direction) &&
           isfinite(p->gravity_scale) && isfinite(p->delta_yaw) && isfinite(p->ideal_yaw) &&
           isfinite(p->yaw_speed) && p->water_level >= 0 && p->water_level <= 3 &&
           reference(m, p->enemy) && reference(m, p->goal);
}
static bool held_object(const qa_modes_checkpoint *saved, qa_mode_id mode, qa_actor_id actor,
                        qa_actor_id carrier, qa_mode_object_kind kind) {
    if (!actor.registry)
        return true;
    for (size_t i = 0; i < saved->object_count; ++i) {
        const qa_mode_object_checkpoint *o = &saved->objects[i];
        if (qa_actor_id_equal(o->actor, actor))
            return o->mode.slot == mode.slot && o->mode.generation == mode.generation &&
                   o->spec.kind == kind && o->value.phase == QA_OBJECTIVE_CARRIED &&
                   qa_actor_id_equal(o->value.carrier, carrier);
    }
    return false;
}
static bool owned_object(const qa_modes_checkpoint *saved, qa_mode_id mode, qa_actor_id actor) {
    if (!actor.registry)
        return true;
    for (size_t i = 0; i < saved->object_count; ++i)
        if (qa_actor_id_equal(saved->objects[i].actor, actor))
            return saved->objects[i].mode.slot == mode.slot &&
                   saved->objects[i].mode.generation == mode.generation;
    return false;
}
static bool validate_instance(qa_modes *m, const qa_mode_checkpoint *v, qa_error *e) {
    if (v->id.slot >= m->mode_capacity || !v->id.generation || !mode_rules_valid(&v->value.rules) ||
        v->value.phase > QA_MODE_FINISHED || v->value.phase < QA_MODE_WAITING ||
        v->member_count > m->actor_capacity || v->ghost_count > m->actor_capacity ||
        (v->member_count && !v->members) || (v->ghost_count && !v->ghosts) ||
        (v->spawn_count && !v->spawns) || v->spawn_count > SIZE_MAX / sizeof(*v->spawns) ||
        (v->item_count && !v->items) || v->item_count > SIZE_MAX / sizeof(*v->items))
        return mode_fail(e, "invalid saved mode instance");
    if (!reference(m, v->rogue_spawn_spot) ||
        (v->rogue_spawn_spot.registry && v->value.rules.source != QA_MODE_ROGUE))
        return mode_fail(e, "invalid saved Rogue rune spawn cursor");
    for (size_t i = 0; i < v->member_count; ++i) {
        const mode_member *p = &v->members[i];
        if (!p->joined || !mode_live(m, p->actor) || !reference(m, p->flag) ||
            !reference(m, p->relic) || p->spawn_state < 0 || p->spawn_state > 2 ||
            p->introduction_frames < 0 || p->suicide_count < 0 ||
            !reference(m, p->player.follow_target) || p->player.automatic_follow < 0 ||
            p->player.automatic_follow > 2 ||
            !isfinite(p->player.ctf_status) || !isfinite(p->player.ctf_access) ||
            (p->external_owner && !m->options.hooks.restore_player_binding))
            return mode_fail(e, "invalid saved mode member");
        if ((p->rogue_rune && (v->value.rules.source != QA_MODE_ROGUE ||
                              p->rogue_rune > 8 || (p->rogue_rune & (p->rogue_rune - 1)))) ||
            (v->value.rules.source != QA_MODE_ROGUE && v->value.rules.source != QA_MODE_THREEWAVE &&
             (p->rune_sound_ns[0] || p->rune_sound_ns[1] || p->rune_sound_ns[2] || p->rune_sound_ns[3])) ||
            (v->value.rules.source == QA_MODE_ROGUE && p->rune_sound_ns[3]) ||
            (v->value.rules.source == QA_MODE_ROGUE && p->relic.registry))
            return mode_fail(e, "invalid saved Rogue rune carrier");
        for (int j = 0; j < 4; ++j)
            if (p->ballots[j] < -1 || p->ballots[j] > 1)
                return mode_fail(e, "invalid saved ballot");
        for (size_t j = 0; j < i; ++j)
            if (qa_actor_id_equal(v->members[j].actor, p->actor))
                return mode_fail(e, "duplicate saved mode member");
    }
    for (int i = 0; i < 3; ++i)
        if (!reference(m, v->bases[i]))
            return mode_fail(e, "invalid saved objective base");
    qa_strings *strings = qa_session_strings(m->options.services.session);
    for (size_t i = 0; i < v->item_count; ++i) {
        if (!qa_strings_text(strings, v->items[i].source).size ||
            !qa_strings_text(strings, v->items[i].inventory).size ||
            v->items[i].source == v->items[i].inventory)
            return mode_fail(e, "invalid saved mode item identity");
        for (size_t j = 0; j < i; ++j)
            if (v->items[i].source == v->items[j].source ||
                v->items[i].inventory == v->items[j].inventory)
                return mode_fail(e, "duplicate saved mode item identity");
    }
    for (size_t i = 0; i < v->spawn_count; ++i)
        if (!qa_vec_finite(v->spawns[i].origin) || !qa_vec_finite(v->spawns[i].angles))
            return mode_fail(e, "invalid saved player spawn");
    if (v->rogue_spawn_spot.registry) {
        bool found = false;
        for (size_t i = 0; i < v->spawn_count; ++i) {
            const char *name = qa_strings_cstr(strings, v->spawns[i].classname);
            if (qa_actor_id_equal(v->spawns[i].actor, v->rogue_spawn_spot) &&
                name && !strcmp(name, "info_player_deathmatch")) found = true;
        }
        if (!found) return mode_fail(e, "saved Rogue rune cursor is not a deathmatch spawn");
    }
    for (size_t i = 0; i < v->ghost_count; ++i) {
        if (v->ghosts[i].code < 10000 || v->ghosts[i].code > 99999)
            return mode_fail(e, "invalid saved ghost code");
        for (size_t j = 0; j < i; ++j)
            if (v->ghosts[i].code == v->ghosts[j].code)
                return mode_fail(e, "duplicate saved ghost code");
    }
    if (!reference(m, v->ball) || !reference(m, v->tag) || !reference(m, v->tag_owner))
        return mode_fail(e, "invalid saved mode actor");
    if (v->q3_settings_present) {
        if (v->value.rules.source < QA_MODE_Q3 || v->value.rules.kind < QA_MODE_FFA ||
            v->value.rules.kind > QA_MODE_HARVESTER || !m->options.hooks.q3_clock ||
            !m->options.hooks.q3_warmup_restart)
            return mode_fail(e, "saved Q3 settings have no actual source mode role");
    } else if (v->q3_settings.do_warmup || v->q3_settings.warmup_seconds ||
               v->q3_settings.time_limit_minutes || v->q3_settings.frag_limit ||
               v->q3_settings.capture_limit || v->q3_settings.warmup_modification_count ||
               v->q3_started_ms || v->q3_warmup_ms || v->q3_warmup_seen)
        return mode_fail(e, "absent Q3 settings claim source continuation");
    for (int i = 0; i < 4; ++i) {
        if (v->votes[i].yes < 0 || v->votes[i].no < 0 ||
            v->votes[i].intent.kind < QA_MATCH_NEXT_MAP ||
            v->votes[i].intent.kind > QA_MATCH_FRAG_LIMIT || !isfinite(v->votes[i].intent.value))
            return mode_fail(e, "invalid saved vote");
        const qa_mode_vote *vote = &v->votes[i];
        if (!mode_intent_command_valid(m, v->value.rules.source, &vote->intent,
                                       vote->active || vote->passed))
            return mode_fail(e, "saved vote lacks its actual bounded source command");
    }
    return true;
}
static bool restore_instance(qa_modes *m, const qa_mode_checkpoint *saved, qa_error *e) {
    mode_instance *v = &m->instances[saved->id.slot];
    *v = (mode_instance){.id = saved->id,
                         .value = saved->value,
                         .active = true,
                         .ball = saved->ball,
                         .tag = saved->tag,
                         .tag_owner = saved->tag_owner,
                         .last_ball_touch = saved->last_ball_touch,
                         .ready_since_ns = saved->ready_since_ns,
                         .next_second_ns = saved->next_second_ns,
                         .tag_count = saved->tag_count,
                         .remaining_seconds = saved->remaining_seconds,
                         .countdown_announced = saved->countdown_announced,
                         .restart_sent = saved->restart_sent,
                         .relic_spawn_ns = saved->relic_spawn_ns,
                         .team_location_ns = saved->team_location_ns,
                         .rune_cursor = saved->rune_cursor,
                         .rogue_spawn_spot = saved->rogue_spawn_spot,
                         .relics_started = saved->relics_started,
                         .rune_forward = saved->rune_forward,
                         .q3_settings = saved->q3_settings,
                         .q3_settings_present = saved->q3_settings_present,
                         .q3_started_ms = saved->q3_started_ms,
                         .q3_warmup_ms = saved->q3_warmup_ms,
                         .q3_warmup_seen = saved->q3_warmup_seen,
                         .next_location = saved->next_location};
    memcpy(v->bases, saved->bases, sizeof(v->bases));
    memcpy(v->votes, saved->votes, sizeof(v->votes));
    memcpy(v->last_spawns, saved->last_spawns, sizeof(v->last_spawns));
    memcpy(v->vote_started, saved->vote_started, sizeof(v->vote_started));
    memcpy(v->flag_sound_ns, saved->flag_sound_ns, sizeof(v->flag_sound_ns));
    memcpy(v->attack_sound_ns, saved->attack_sound_ns, sizeof(v->attack_sound_ns));
    v->members = calloc(m->actor_capacity, sizeof(*v->members));
    v->bindings = calloc(m->actor_capacity, sizeof(*v->bindings));
    v->ghosts = calloc(m->actor_capacity, sizeof(*v->ghosts));
    v->sorted = calloc(m->actor_capacity, sizeof(*v->sorted));
    v->ranks = calloc(m->actor_capacity, sizeof(*v->ranks));
    v->spawns = saved->spawn_count ? malloc(saved->spawn_count * sizeof(*v->spawns)) : NULL;
    v->items = saved->item_count ? malloc(saved->item_count * sizeof(*v->items)) : NULL;
    if (!v->members || !v->bindings || !v->ghosts || !v->sorted || !v->ranks ||
        (saved->spawn_count && !v->spawns) || (saved->item_count && !v->items)) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "restoring mode instance");
        return false;
    }
    for (size_t i = 0; i < saved->member_count; ++i)
        v->members[saved->members[i].actor.slot] = saved->members[i];
    for (size_t i = 0; i < saved->member_count; ++i) {
        const mode_member *p = &saved->members[i];
        if (!p->external_owner)
            continue;
        qa_match_binding binding = {0};
        qa_match_lease lease;
        if (!MODE_CALLBACK(m, m->options.hooks.restore_player_binding(m->options.hooks.context,
                v->id, p->actor, p->external_owner, &binding, e)))
            return false;
        if (binding.owner != p->external_owner)
            return mode_fail(e, "saved mode score/team owner could not be restored");
        if (!qa_modes_bind_player(m, v->id, p->actor, &binding, &lease, e))
            return false;
    }
    for (size_t i = 0; i < saved->ghost_count; ++i)
        v->ghosts[i] = saved->ghosts[i];
    v->spawn_count = saved->spawn_count;
    if (v->spawn_count)
        memcpy(v->spawns, saved->spawns, v->spawn_count * sizeof(*v->spawns));
    v->item_count = v->item_capacity = saved->item_count;
    if (v->item_count)
        memcpy(v->items, saved->items, v->item_count * sizeof(*v->items));
    return mode_horde_restore(m, v, saved->horde, e);
}
static bool checkpoint_restore(qa_modes *m, const qa_modes_checkpoint *saved,
                                bool reconnect, qa_error *e) {
    if (!m || m->callback_depth || !saved || saved->version != 8 ||
        saved->player_count > m->actor_capacity || saved->mode_count > m->mode_capacity ||
        saved->object_count > m->actor_capacity ||
        saved->external_objective_count > m->objective_capacity ||
        saved->generation_count > m->mode_capacity ||
        (saved->generation_count && !saved->mode_generations) ||
        (saved->player_count && !saved->players) || (saved->mode_count && !saved->modes) ||
        (saved->object_count && !saved->objects) ||
        (saved->external_objective_count && !saved->external_objectives))
        return mode_fail(e, "invalid mode checkpoint");
    for (uint32_t i = 0; i < m->mode_capacity; ++i)
        if (m->instances[i].active)
            return mode_fail(e, "mode restore requires an empty native mode set");
    for (uint32_t i = 0; i < m->actor_capacity; ++i)
        if (m->objects[i].active)
            return mode_fail(e, "mode restore requires empty native objectives");
    for (size_t i = 0; i < saved->mode_count; ++i) {
        if (!validate_instance(m, &saved->modes[i], e))
            return false;
        if (saved->modes[i].id.slot >= saved->generation_count ||
            saved->mode_generations[saved->modes[i].id.slot] != saved->modes[i].id.generation)
            return mode_fail(e, "invalid saved mode generation");
        const qa_mode_checkpoint *mode = &saved->modes[i];
        for (int j = 0; j < 3; ++j)
            if (!owned_object(saved, mode->id, mode->bases[j]))
                return mode_fail(e, "saved objective base belongs to another mode");
        if (!owned_object(saved, mode->id, mode->ball) ||
            !owned_object(saved, mode->id, mode->tag))
            return mode_fail(e, "saved native objective belongs to another mode");
        for (size_t j = 0; j < saved->modes[i].member_count; ++j) {
            const qa_mode_member_state *member = &saved->modes[i].members[j];
            if (!held_object(saved, saved->modes[i].id, member->flag, member->actor,
                             QA_MODE_OBJECT_FLAG) ||
                !held_object(saved, saved->modes[i].id, member->relic, member->actor,
                             QA_MODE_OBJECT_RELIC))
                return mode_fail(e, "saved carried object has a conflicting mode owner");
            bool found = false;
            for (size_t k = 0; k < saved->player_count; ++k)
                if (qa_actor_id_equal(saved->players[k].value.actor,
                                      saved->modes[i].members[j].actor)) {
                    found = true;
                    break;
                }
            if (!found)
                return mode_fail(e, "saved participant has no match player");
        }
        for (size_t j = 0; j < i; ++j) {
            if (saved->modes[i].id.slot == saved->modes[j].id.slot)
                return mode_fail(e, "duplicate saved mode slot");
            for (size_t a = 0; a < saved->modes[i].item_count; ++a)
                for (size_t b = 0; b < saved->modes[j].item_count; ++b)
                    if (saved->modes[i].items[a].inventory == saved->modes[j].items[b].inventory)
                        return mode_fail(e, "saved mode item belongs to multiple instances");
        }
    }
    for (size_t i = 0; i < saved->player_count; ++i) {
        const qa_mode_player_checkpoint *player = &saved->players[i];
        if (!mode_live(m, player->value.actor))
            return mode_fail(e, "invalid saved match player");
        for (size_t j = 0; j < i; ++j)
            if (qa_actor_id_equal(saved->players[j].value.actor, player->value.actor))
                return mode_fail(e, "duplicate saved match player");
    }
    for (size_t i = 0; reconnect && i < saved->external_objective_count; ++i) {
        const qa_mode_objective_checkpoint *expected = &saved->external_objectives[i];
        bool found = false;
        for (uint32_t j = 0; j < m->objective_capacity; ++j) {
            mode_objective *o = &m->objectives[j];
            if (o->active && o->binding.id == expected->id && o->binding.owner == expected->owner &&
                o->binding.mode.slot == expected->mode.slot &&
                o->binding.mode.generation == expected->mode.generation &&
                o->binding.campaign_gate == expected->campaign_gate &&
                o->binding.bot_goal == expected->bot_goal) {
                found = true;
                break;
            }
        }
        if (!found)
            return mode_fail(e, "source objective must restore before modes");
    }
    for (size_t i = 0; i < saved->object_count; ++i) {
        const qa_mode_object_checkpoint *o = &saved->objects[i];
        const qa_mode_checkpoint *owner = NULL;
        for (size_t j = 0; j < saved->mode_count; ++j)
            if (saved->modes[j].id.slot == o->mode.slot &&
                saved->modes[j].id.generation == o->mode.generation)
                owner = &saved->modes[j];
        if (!owner || !owned_object(saved, o->mode, o->base) ||
            !owned_object(saved, o->mode, o->dropped_actor))
            return mode_fail(e, "saved objective has a conflicting mode owner");
        if (o->spec.item) {
            bool found = false;
            for (size_t j = 0; j < owner->item_count; ++j)
                if (owner->items[j].source == o->spec.item)
                    found = true;
            if (!found)
                return mode_fail(e, "saved objective is missing its mode item handle");
        }
        if (o->value.carrier.registry) {
            bool found = false;
            for (size_t j = 0; j < owner->member_count; ++j)
                if (qa_actor_id_equal(owner->members[j].actor, o->value.carrier))
                    found = true;
            if (!found)
                return mode_fail(e, "saved objective carrier is outside its mode");
        }
        if (!mode_live(m, o->actor) || !qa_actor_id_equal(o->spec.actor, o->actor) ||
            o->value.mode.slot != o->mode.slot || o->value.mode.generation != o->mode.generation ||
            o->value.kind != o->spec.kind || o->value.team != o->spec.team ||
            o->value.relic != o->spec.relic || o->spec.kind < QA_MODE_OBJECT_FLAG ||
            o->spec.kind > QA_MODE_OBJECT_FLAG_BASE || o->spec.relic < QA_RELIC_RESISTANCE ||
            o->spec.relic >= QA_RELIC_COUNT || o->tag_stage < 0 || o->tag_stage > 4 ||
            o->value.phase < QA_OBJECTIVE_HOME || o->value.phase > QA_OBJECTIVE_COMPLETE ||
            !qa_vec_finite(o->home) || !qa_vec_finite(o->spec.origin) ||
            !qa_vec_finite(o->spec.angles) || !qa_vec_finite(o->spec.direction) ||
            !isfinite(o->spec.value) || (o->spec.has_bounds && !bounds_valid(o->spec.bounds)) ||
            !physics_valid(m, &o->physics) || o->collision.family < QA_COLLISION_Q1 ||
            o->collision.family > QA_COLLISION_Q3 || o->collision.role < QA_COLLISION_SOLID ||
            o->collision.role > QA_COLLISION_BOTH || !reference(m, o->value.carrier) ||
            !reference(m, o->value.previous_owner) || !reference(m, o->base) ||
            !reference(m, o->dropped_actor))
            return mode_fail(e, "invalid saved objective actor");
        for (size_t j = 0; j < i; ++j)
            if (qa_actor_id_equal(saved->objects[j].actor, o->actor))
                return mode_fail(e, "duplicate saved objective actor");
    }
    if (!reconnect) {
        if (m->source_restored || m->restored_objectives)
            return mode_fail(e, "mode source continuation is already restored");
        qa_mode_objective_checkpoint *expected = saved->external_objective_count
            ? malloc(saved->external_objective_count * sizeof(*expected)) : NULL;
        if (saved->external_objective_count && !expected) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "retaining restored mode objective bindings");
            return false;
        }
        if (saved->external_objective_count)
            memcpy(expected, saved->external_objectives, saved->external_objective_count * sizeof(*expected));
        m->restored_objectives = expected;
        m->restored_objective_count = saved->external_objective_count;
        m->source_restored = true;
    }
    m->random = saved->random;
    m->attack_sequence = saved->attack_sequence;
    for (size_t i = 0; i < saved->generation_count; ++i)
        m->instances[i].id = (qa_mode_id){(uint32_t)i, saved->mode_generations[i]};
    for (size_t i = 0; i < saved->player_count; ++i) {
        const qa_mode_player_checkpoint *player = &saved->players[i];
        mode_player *p = &m->players[player->value.actor.slot];
        p->value = player->value;
        p->active = true;
        p->modes = m;
    }
    for (size_t i = 0; i < saved->mode_count; ++i)
        if (!restore_instance(m, &saved->modes[i], e))
            return false;
    for (size_t i = 0; i < saved->object_count; ++i) {
        const qa_mode_object_checkpoint *s = &saved->objects[i];
        if (!mode_get(m, s->mode))
            return mode_fail(e, "saved objective has no native mode");
        mode_object *o = &m->objects[s->actor.slot];
        *o = (mode_object){.modes = m,
                           .actor = s->actor,
                           .mode = s->mode,
                           .spec = s->spec,
                           .value = s->value,
                           .home = s->home,
                           .base = s->base,
                           .dropped_actor = s->dropped_actor,
                           .collision = s->collision,
                           .physics = s->physics,
                           .next_ns = s->next_ns,
                           .owner_until_ns = s->owner_until_ns,
                           .animation_ns = s->animation_ns,
                           .expire_ns = s->expire_ns,
                           .born_ns = s->born_ns,
                           .targets_used = s->targets_used,
                           .has_physics = s->has_physics,
                           .dropped = s->dropped,
                           .global_animation = s->global_animation,
                           .tag_stage = s->tag_stage,
                           .active = true};
        if (reconnect && !mode_object_bind_objective(m, o, e))
            return false;
    }
    if (!reconnect) return true;
    for (uint32_t i = 0; i < m->mode_capacity; ++i)
        if (m->instances[i].active && !mode_relic_source_current(m, &m->instances[i], e)) return false;
    if (!qa_builtin_players(&m->options.services, &m->players_order, e) ||
        !qa_builtin_observations(&m->options.services, &m->observations, e))
        return false;
    for (size_t i = 0; i < saved->mode_count; ++i)
        if (!qa_modes_rank(m, saved->modes[i].id, e))
            return false;
    for (size_t i = 0; i < saved->player_count; ++i)
        if (!qa_modes_publish_items(m, saved->players[i].value.actor, e))
            return false;
    return true;
}
bool qa_modes_checkpoint_restore(qa_modes *m, const qa_modes_checkpoint *saved, qa_error *e) {
    return checkpoint_restore(m, saved, true, e);
}
bool mode_checkpoint_restore_source(qa_modes *m, const qa_modes_checkpoint *saved, qa_error *e) {
    return checkpoint_restore(m, saved, false, e);
}
bool qa_modes_reconnect(qa_modes *m, qa_error *e) {
    if (!m || m->callback_depth || !qa_session_safe(m->options.services.session) ||
        !qa_world_idle(m->options.services.world) || !qa_combat_idle(m->options.services.combat))
        return mode_fail(e, "mode reconnect requires idle shared owners");
    if (!m->source_restored) return true;
    for (uint32_t i = 0; i < m->mode_capacity; ++i)
        if (m->instances[i].active && !mode_relic_source_current(m, &m->instances[i], e)) return false;
    for (size_t i = 0; i < m->restored_objective_count; ++i) {
        const qa_mode_objective_checkpoint *expected = &m->restored_objectives[i];
        bool found = false;
        for (uint32_t j = 0; j < m->objective_capacity; ++j) {
            const mode_objective *o = &m->objectives[j];
            if (o->active && o->binding.id == expected->id && o->binding.owner == expected->owner &&
                o->binding.mode.slot == expected->mode.slot &&
                o->binding.mode.generation == expected->mode.generation &&
                o->binding.campaign_gate == expected->campaign_gate && o->binding.bot_goal == expected->bot_goal)
                found = true;
        }
        if (!found) return mode_fail(e, "source objective must bind before mode reconnect");
    }
    for (uint32_t i = 0; i < m->actor_capacity; ++i) {
        mode_object *object = &m->objects[i];
        if (object->active && !object->objective.serial && !mode_object_bind_objective(m, object, e))
            return false;
        mode_player *player = &m->players[i];
        if (player->active && !mode_items_reconnect(m, player->value.actor, e)) return false;
    }
    if (!qa_builtin_players(&m->options.services, &m->players_order, e) ||
        !qa_builtin_observations(&m->options.services, &m->observations, e)) return false;
    for (uint32_t i = 0; i < m->mode_capacity; ++i)
        if (m->instances[i].active && !qa_modes_rank(m, m->instances[i].id, e)) return false;
    free(m->restored_objectives); m->restored_objectives = NULL;
    m->restored_objective_count = 0; m->source_restored = false;
    return true;
}
