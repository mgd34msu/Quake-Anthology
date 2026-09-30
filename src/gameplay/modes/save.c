#include "internal.h"
#include "qa/source_save.h"
#include "qa/persistence_fields.h"

#define FIELD(kind, value) do { if (!qa_source_save_##kind(io, &(value))) return false; } while (0)
#define ENUM(value, last) do { \
    uint32_t encoded = (uint32_t)(value); \
    FIELD(u32, encoded); \
    if (encoded > (uint32_t)(last)) return save_fail(io, "invalid mode save enum"); \
    if (io->direction == QA_SOURCE_SAVE_READ) (value) = encoded; \
} while (0)
#define ARRAY(values, count, maximum) do { \
    void *storage = (values); \
    if (!array(io, &storage, &(count), sizeof(*(values)), (maximum))) return false; \
    (values) = storage; \
} while (0)

static bool save_fail(qa_source_save_io *io, const char *message) {
    qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "%s", message);
    return false;
}
static bool array(qa_source_save_io *io, void **values, size_t *count, size_t size,
                  size_t maximum) {
    if (!qa_source_save_count(io, count, maximum)) return false;
    if (*count > SIZE_MAX / size) return save_fail(io, "mode save allocation overflow");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (*count > io->input.size - io->offset)
            return save_fail(io, "truncated mode save array");
        *values = *count ? calloc(*count, size) : NULL;
        if (*count && !*values) {
            qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating mode save array");
            return false;
        }
    }
    return true;
}
static bool mode_id(qa_source_save_io *io, qa_mode_id *p) {
    FIELD(u32, p->slot); FIELD(u64, p->generation); return true;
}
static bool signed_byte(qa_source_save_io *io, int8_t *p) {
    int32_t value = *p;
    FIELD(i32, value);
    if (value < INT8_MIN || value > INT8_MAX) return save_fail(io, "invalid mode save byte");
    if (io->direction == QA_SOURCE_SAVE_READ) *p = (int8_t)value;
    return true;
}
static bool index_value(qa_source_save_io *io, size_t *p) {
    uint64_t value = *p == SIZE_MAX ? UINT64_MAX : (uint64_t)*p;
    FIELD(u64, value);
    if (value != UINT64_MAX && value >= SIZE_MAX) return save_fail(io, "mode save index overflow");
    if (io->direction == QA_SOURCE_SAVE_READ) *p = value == UINT64_MAX ? SIZE_MAX : (size_t)value;
    return true;
}
static bool rules(qa_source_save_io *io, qa_mode_rules *p) {
    ENUM(p->source, QA_MODE_TEAM_ARENA); ENUM(p->kind, QA_MODE_HORDE);
    for (size_t i = 0; i < 3; ++i) FIELD(string, p->teams[i]);
    FIELD(string, p->forced_team);
    FIELD(i32, p->frag_limit); FIELD(i32, p->capture_limit); FIELD(i32, p->warmup_seconds);
    FIELD(i32, p->competition); FIELD(i32, p->setup_seconds); FIELD(i32, p->countdown_seconds);
    FIELD(i32, p->match_seconds); FIELD(i32, p->max_game_players); FIELD(i32, p->election_percent);
    FIELD(i32, p->teamplay); FIELD(i32, p->rune_mask); FIELD(i32, p->vote_limit);
    FIELD(u32, p->flags); FIELD(u32, p->referee_flags);
    FIELD(f32, p->time_limit_minutes); FIELD(f32, p->obelisk_health); FIELD(f32, p->obelisk_regen);
    FIELD(u64, p->obelisk_regen_ns); FIELD(u64, p->obelisk_respawn_ns);
    FIELD(bool, p->enabled); FIELD(bool, p->friendly_fire); FIELD(bool, p->force_join);
    FIELD(bool, p->match_lock); FIELD(bool, p->paused); FIELD(bool, p->auto_lock);
    FIELD(bool, p->relics); FIELD(bool, p->single_player_active); FIELD(bool, p->tournament_restart);
    FIELD(bool, p->q2_rerelease); FIELD(bool, p->start_map); FIELD(bool, p->force_balance);
    FIELD(bool, p->voting_disabled); FIELD(bool, p->rogue_deathmatch); return true;
}
static bool statistics(qa_source_save_io *io, qa_mode_statistics *p) {
    FIELD(i32, p->score); FIELD(i32, p->kills); FIELD(i32, p->deaths); FIELD(i32, p->captures);
    FIELD(i32, p->defenses); FIELD(i32, p->carrier_defenses); FIELD(i32, p->recoveries);
    FIELD(i32, p->assists); FIELD(i32, p->assist_awards); FIELD(i32, p->rank); FIELD(i32, p->tokens);
    FIELD(i32, p->streak); FIELD(u64, p->flag_since_ns); FIELD(u64, p->returned_ns);
    FIELD(u64, p->carrier_killed_ns); FIELD(u64, p->hurt_carrier_ns); FIELD(u64, p->defended_ns);
    FIELD(u64, p->last_kill_ns); FIELD(bool, p->returned); FIELD(bool, p->carrier_killed);
    FIELD(bool, p->hurt_carrier); FIELD(bool, p->defended); return true;
}
static bool player_state(qa_source_save_io *io, qa_mode_player_state *p) {
    FIELD(actor, p->follow_target); FIELD(string, p->observer_team); FIELD(string, p->team);
    FIELD(i32, p->score); FIELD(i32, p->wins); FIELD(i32, p->losses);
    FIELD(bool, p->spectator); FIELD(bool, p->scoreboard); FIELD(bool, p->ready); FIELD(bool, p->leader);
    FIELD(u64, p->spectator_since_ns);
    if (!signed_byte(io, &p->automatic_follow)) return false;
    FIELD(i32, p->ctf_last_team); FIELD(f32, p->ctf_status); FIELD(f32, p->ctf_access); return true;
}
static bool member(qa_source_save_io *io, qa_mode_member_state *p) {
    FIELD(actor, p->actor);
    if (!player_state(io, &p->player) || !statistics(io, &p->stats)) return false;
    FIELD(string, p->external_owner); FIELD(actor, p->relic); FIELD(actor, p->flag);
    FIELD(string, p->last_team); FIELD(u64, p->tech_sound_ns); FIELD(u64, p->regen_ns);
    FIELD(u32, p->rogue_rune);
    for (size_t i = 0; i < 4; ++i) FIELD(u64, p->rune_sound_ns[i]);
    FIELD(u64, p->notice_ns); FIELD(u64, p->respawn_ns); FIELD(u64, p->team_switch_ns);
    FIELD(i32, p->regen_frame); FIELD(i32, p->extra_flags); FIELD(i32, p->location);
    FIELD(i32, p->spawn_state); FIELD(i32, p->suicide_count); FIELD(i32, p->introduction_frames);
    FIELD(u32, p->ghost_code);
    for (size_t i = 0; i < 4; ++i) {
        FIELD(u32, p->vote_calls[i]); if (!signed_byte(io, &p->ballots[i])) return false;
    }
    FIELD(bool, p->joined); FIELD(bool, p->admin); FIELD(bool, p->observer_jump); return true;
}
static bool ghost(qa_source_save_io *io, qa_mode_ghost_state *p) {
    FIELD(actor, p->actor); FIELD(string, p->name); FIELD(string, p->team);
    if (!statistics(io, &p->stats)) return false;
    FIELD(u32, p->code); FIELD(i32, p->score); return true;
}
static bool spawnpoint(qa_source_save_io *io, qa_mode_spawnpoint *p) {
    FIELD(actor, p->actor); FIELD(vec3, p->origin); FIELD(vec3, p->angles);
    FIELD(string, p->team); FIELD(string, p->classname); FIELD(u32, p->flags);
    FIELD(bool, p->no_bots); FIELD(bool, p->no_humans); return true;
}
static bool intent(qa_source_save_io *io, qa_match_intent *p) {
    ENUM(p->kind, QA_MATCH_FRAG_LIMIT);
    if (!mode_id(io, &p->mode)) return false;
    FIELD(actor, p->actor); FIELD(string, p->team); FIELD(string, p->map);
    FIELD(string, p->source_command);
    ENUM(p->game_type, QA_MODE_HORDE); FIELD(f32, p->value); return true;
}
static bool vote(qa_source_save_io *io, qa_mode_vote *p) {
    if (!intent(io, &p->intent)) return false;
    FIELD(actor, p->initiator); FIELD(string, p->team); FIELD(u64, p->deadline_ns);
    FIELD(u64, p->execute_ns); FIELD(i32, p->yes); FIELD(i32, p->no); FIELD(i32, p->needed);
    FIELD(bool, p->active); FIELD(bool, p->passed); return true;
}
static bool horde_point(qa_source_save_io *io, qa_horde_point *p) {
    FIELD(actor, p->actor); ENUM(p->kind, QA_HORDE_KEY);
    FIELD(vec3, p->origin); FIELD(vec3, p->angles); FIELD(string, p->target);
    FIELD(u32, p->flags); FIELD(u64, p->next_ns); FIELD(bool, p->occupied); return true;
}
static bool horde(qa_source_save_io *io, qa_modes *m, qa_horde_checkpoint **out) {
    bool present = *out != NULL;
    FIELD(bool, present);
    if (!present) return true;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        *out = calloc(1, sizeof(**out));
        if (!*out) {
            qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating Horde save");
            return false;
        }
    }
    qa_horde_checkpoint *p = *out;
    FIELD(actor, p->options.manager); FIELD(string, p->options.target);
    FIELD(u32, p->options.starting_campaign_flags); FIELD(i32, p->options.skill);
    FIELD(i32, p->options.world_type); FIELD(bool, p->options.cooperative);
    FIELD(i32, p->value.wave); FIELD(i32, p->value.fodder); FIELD(i32, p->value.elites);
    FIELD(i32, p->value.bosses); FIELD(i32, p->value.countdown); FIELD(i32, p->value.silver_keys);
    FIELD(i32, p->value.gold_keys); FIELD(u64, p->value.next_ns); FIELD(f32, p->value.powerup_chance);
    FIELD(bool, p->value.army); FIELD(bool, p->value.spawning); FIELD(bool, p->value.key_spawned);
    FIELD(bool, p->prepared); FIELD(bool, p->checking); FIELD(bool, p->finishing);
    ARRAY(p->points, p->point_count, SIZE_MAX);
    for (size_t i = 0; i < p->point_count; ++i) if (!horde_point(io, &p->points[i])) return false;
    ARRAY(p->monsters, p->monster_count, m->actor_capacity);
    for (size_t i = 0; i < p->monster_count; ++i) {
        qa_horde_monster_state *v = &p->monsters[i];
        FIELD(actor, v->actor); FIELD(bool, v->zombie); FIELD(bool, v->counted);
        FIELD(bool, v->death_pending); FIELD(u64, v->kill_ns);
    }
    ARRAY(p->loot, p->loot_count, m->actor_capacity);
    for (size_t i = 0; i < p->loot_count; ++i) {
        qa_horde_loot_state *v = &p->loot[i];
        FIELD(actor, v->actor); ENUM(v->kind, HORDE_POWER);
        if (!index_value(io, &v->point)) return false;
        FIELD(string, v->item); FIELD(f32, v->amount); FIELD(f32, v->capacity);
        FIELD(f32, v->alpha); FIELD(u64, v->fade_ns);
    }
    return true;
}
static bool instance(qa_source_save_io *io, qa_modes *m, qa_mode_checkpoint *p) {
    if (!mode_id(io, &p->id) || !rules(io, &p->value.rules)) return false;
    ENUM(p->value.phase, QA_MODE_FINISHED);
    FIELD(u64, p->value.time_ns); FIELD(u64, p->value.started_ns); FIELD(u64, p->value.deadline_ns);
    for (size_t i = 0; i < 3; ++i) {
        FIELD(i32, p->value.team_scores[i]); FIELD(i32, p->value.team_captures[i]);
    }
    if (!qa_source_save_count(io, &p->value.playing, m->actor_capacity) ||
        !qa_source_save_count(io, &p->value.voting, m->actor_capacity) ||
        !qa_source_save_count(io, &p->value.sorted_count, m->actor_capacity)) return false;
    FIELD(bool, p->value.ready_exit); FIELD(bool, p->value.ctf_pregame_over);
    ARRAY(p->members, p->member_count, m->actor_capacity);
    for (size_t i = 0; i < p->member_count; ++i) if (!member(io, &p->members[i])) return false;
    ARRAY(p->ghosts, p->ghost_count, m->actor_capacity);
    for (size_t i = 0; i < p->ghost_count; ++i) if (!ghost(io, &p->ghosts[i])) return false;
    ARRAY(p->spawns, p->spawn_count, SIZE_MAX);
    for (size_t i = 0; i < p->spawn_count; ++i) if (!spawnpoint(io, &p->spawns[i])) return false;
    ARRAY(p->items, p->item_count, SIZE_MAX);
    for (size_t i = 0; i < p->item_count; ++i) {
        FIELD(string, p->items[i].source); FIELD(string, p->items[i].inventory);
    }
    for (size_t i = 0; i < 3; ++i) FIELD(actor, p->bases[i]);
    FIELD(actor, p->ball); FIELD(actor, p->tag); FIELD(actor, p->tag_owner); FIELD(actor, p->last_ball_touch);
    for (size_t i = 0; i < 3; ++i) FIELD(actor, p->last_spawns[i]);
    FIELD(actor, p->rogue_spawn_spot);
    for (size_t i = 0; i < 4; ++i) {
        if (!vote(io, &p->votes[i])) return false;
        FIELD(u64, p->vote_started[i]);
    }
    FIELD(u64, p->ready_since_ns); FIELD(u64, p->next_second_ns);
    for (size_t i = 0; i < 3; ++i) { FIELD(u64, p->flag_sound_ns[i]); FIELD(u64, p->attack_sound_ns[i]); }
    FIELD(u64, p->relic_spawn_ns); FIELD(u64, p->team_location_ns);
    if (!qa_source_save_count(io, &p->rune_cursor, SIZE_MAX)) return false;
    FIELD(i32, p->next_location); FIELD(i32, p->tag_count); FIELD(i32, p->remaining_seconds);
    FIELD(bool, p->countdown_announced); FIELD(bool, p->restart_sent);
    FIELD(bool, p->relics_started); FIELD(bool, p->rune_forward);
    FIELD(bool, p->q3_settings_present);
    if (p->q3_settings_present) {
        FIELD(i32, p->q3_settings.do_warmup); FIELD(i32, p->q3_settings.warmup_seconds);
        FIELD(i32, p->q3_settings.time_limit_minutes); FIELD(i32, p->q3_settings.frag_limit);
        FIELD(i32, p->q3_settings.capture_limit); FIELD(u64, p->q3_settings.warmup_modification_count);
        FIELD(i32, p->q3_started_ms); FIELD(i32, p->q3_warmup_ms); FIELD(u64, p->q3_warmup_seen);
    }
    return horde(io, m, &p->horde);
}
static bool object_spec(qa_source_save_io *io, qa_mode_object_spec *p) {
    ENUM(p->kind, QA_MODE_OBJECT_FLAG_BASE); FIELD(string, p->team); ENUM(p->relic, QA_RELIC_COUNT - 1);
    FIELD(string, p->item); FIELD(actor, p->actor); FIELD(vec3, p->origin); FIELD(vec3, p->angles);
    FIELD(vec3, p->direction); FIELD(vec3, p->bounds.mins); FIELD(vec3, p->bounds.maxs);
    FIELD(string, p->target); FIELD(string, p->id); FIELD(string, p->message);
    FIELD(u32, p->flags); FIELD(i32, p->location); FIELD(f32, p->value);
    FIELD(bool, p->authored); FIELD(bool, p->suspended); FIELD(bool, p->has_bounds);
    FIELD(bool, p->retain_body); FIELD(bool, p->command_created); return true;
}
static bool object(qa_source_save_io *io, qa_mode_object_checkpoint *p) {
    FIELD(actor, p->actor);
    if (!mode_id(io, &p->mode) || !object_spec(io, &p->spec) || !mode_id(io, &p->value.mode)) return false;
    ENUM(p->value.kind, QA_MODE_OBJECT_FLAG_BASE); FIELD(string, p->value.team);
    ENUM(p->value.relic, QA_RELIC_COUNT - 1); ENUM(p->value.phase, QA_OBJECTIVE_COMPLETE);
    FIELD(actor, p->value.carrier); FIELD(actor, p->value.previous_owner); FIELD(string, p->value.model);
    FIELD(u64, p->value.effects); FIELD(u64, p->value.deadline_ns); FIELD(i32, p->value.frame);
    FIELD(i32, p->value.skin); FIELD(i32, p->value.health_fraction); FIELD(bool, p->value.visible);
    FIELD(vec3, p->home); FIELD(actor, p->base); FIELD(actor, p->dropped_actor);
    if (!qa_persistence_collision(io, &p->collision) || !qa_persistence_physics(io, &p->physics)) return false;
    FIELD(u64, p->next_ns); FIELD(u64, p->owner_until_ns); FIELD(u64, p->animation_ns);
    FIELD(u64, p->expire_ns); FIELD(u64, p->born_ns); FIELD(i32, p->tag_stage);
    FIELD(bool, p->targets_used); FIELD(bool, p->has_physics); FIELD(bool, p->dropped);
    FIELD(bool, p->global_animation); return true;
}
static bool checkpoint(qa_source_save_io *io, qa_modes *m, qa_modes_checkpoint *p) {
    FIELD(u32, p->version);
    if (p->version != 8) return save_fail(io, "unsupported typed mode checkpoint version");
    FIELD(u64, p->random); FIELD(u64, p->attack_sequence);
    ARRAY(p->mode_generations, p->generation_count, m->mode_capacity);
    if (p->generation_count != m->mode_capacity) return save_fail(io, "mode save capacity changed");
    for (size_t i = 0; i < p->generation_count; ++i) FIELD(u64, p->mode_generations[i]);
    ARRAY(p->players, p->player_count, m->actor_capacity);
    for (size_t i = 0; i < p->player_count; ++i) {
        qa_match_player *v = &p->players[i].value;
        FIELD(actor, v->actor); FIELD(string, v->name);
        FIELD(bool, v->connected); FIELD(bool, v->connecting); FIELD(bool, v->bot);
    }
    ARRAY(p->modes, p->mode_count, m->mode_capacity);
    for (size_t i = 0; i < p->mode_count; ++i) if (!instance(io, m, &p->modes[i])) return false;
    ARRAY(p->objects, p->object_count, m->actor_capacity);
    for (size_t i = 0; i < p->object_count; ++i) if (!object(io, &p->objects[i])) return false;
    ARRAY(p->external_objectives, p->external_objective_count, m->objective_capacity);
    for (size_t i = 0; i < p->external_objective_count; ++i) {
        qa_mode_objective_checkpoint *v = &p->external_objectives[i];
        if (!mode_id(io, &v->mode)) return false;
        FIELD(string, v->owner); FIELD(string, v->id);
        FIELD(bool, v->campaign_gate); FIELD(bool, v->bot_goal);
    }
    return true;
}
static bool header(qa_source_save_io *io) {
    static const uint8_t expected[8] = {'Q', 'A', 'M', 'O', 'D', 'E', 'S', 0};
    uint8_t signature[8] = {'Q', 'A', 'M', 'O', 'D', 'E', 'S', 0};
    uint32_t version = 6;
    if (!qa_source_save_bytes(io, signature, sizeof(signature)) || memcmp(signature, expected, sizeof(signature)))
        return save_fail(io, "invalid mode save signature");
    FIELD(u32, version);
    return version == 6 || save_fail(io, "unsupported mode save version");
}
static bool boundary(qa_modes *m, qa_error *e) {
    if (!m || m->callback_depth || !qa_session_safe(m->options.services.session) ||
        !qa_world_idle(m->options.services.world) || !qa_combat_idle(m->options.services.combat))
        return mode_fail(e, "mode save requires idle shared owners");
    for (uint32_t i = 0; i < m->actor_capacity; ++i)
        if (m->objects[i].admitting) return mode_fail(e, "mode save conflicts with objective admission");
    for (uint32_t i = 0; i < m->objective_capacity; ++i)
        if (m->objectives[i].reserved) return mode_fail(e, "mode save conflicts with objective binding");
    return true;
}
bool qa_modes_capture(qa_modes *m, qa_buffer *out, qa_error *e) {
    if (!out) return mode_fail(e, "mode capture requires output");
    if (!boundary(m, e)) return false;
    if (m->source_restored) return mode_fail(e, "mode capture requires completed shared reconnection");
    qa_modes_checkpoint saved = {0};
    if (!qa_modes_checkpoint_capture(m, &saved, e)) return false;
    qa_source_save_io storage = {0}, *io = &storage;
    bool okay = qa_source_save_writer(io, m->options.services.session, e) && header(io) && checkpoint(io, m, &saved);
    if (okay) okay = qa_source_save_finish(io, out);
    qa_source_save_dispose(io); qa_modes_checkpoint_free(&saved); return okay;
}
bool qa_modes_restore_bytes(qa_modes *m, qa_bytes input, qa_error *e) {
    if (!boundary(m, e)) return false;
    qa_modes_checkpoint saved = {0};
    qa_source_save_io storage = {0}, *io = &storage;
    bool okay = qa_source_save_reader(io, m->options.services.session, input, e) && header(io) &&
        checkpoint(io, m, &saved) && qa_source_save_finish(io, NULL);
    if (okay) okay = mode_checkpoint_restore_source(m, &saved, e);
    qa_source_save_dispose(io); qa_modes_checkpoint_free(&saved); return okay;
}

#undef FIELD
#undef ENUM
#undef ARRAY
