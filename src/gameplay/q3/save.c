#include "map/internal.h"
#include "qa/game_q3_save.h"
#include "qa/source_save.h"
#include "qa/persistence_gameplay.h"

typedef struct q3_saved_leases {
    uint64_t weapons, holdables, observation;
    uint32_t selections;
    bool inventory;
} q3_saved_leases;

#define FIELD(kind, value) do { if (!qa_source_save_##kind(io, &(value))) return false; } while (0)
#define ENUM(value, maximum) do { \
    uint32_t encoded = (uint32_t)(value); \
    FIELD(u32, encoded); \
    if (encoded > (uint32_t)(maximum)) return save_fail(io, "invalid Q3 save enum"); \
    if (io->direction == QA_SOURCE_SAVE_READ) (value) = encoded; \
} while (0)

static bool save_fail(qa_source_save_io *io, const char *message)
{
    qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "%s", message);
    return false;
}

static bool trajectory(qa_source_save_io *io, qa_trajectory *p)
{
    ENUM(p->type, QA_TRAJECTORY_GRAVITY);
    FIELD(i32, p->time_ms); FIELD(i32, p->duration_ms);
    FIELD(vec3, p->base); FIELD(vec3, p->delta);
    return true;
}

static bool item_spawn(qa_source_save_io *io, qa_q3_item_spawn *p)
{
    FIELD(u32, p->item_index); FIELD(i32, p->count); FIELD(i32, p->team_restriction);
    FIELD(f32, p->wait_seconds); FIELD(f32, p->random_seconds);
    FIELD(bool, p->dropped); FIELD(bool, p->suspended);
    FIELD(vec3, p->origin); FIELD(vec3, p->velocity); FIELD(string, p->target);
    return true;
}

static bool player(qa_source_save_io *io, qa_q3_player_state *p)
{
    FIELD(u32, p->selections); FIELD(u32, p->flags); FIELD(u32, p->event_sequence);
    FIELD(u32, p->spawn_count);
    for (size_t i = 0; i < 2; ++i) FIELD(i32, p->events[i]);
    for (size_t i = 0; i < 2; ++i) FIELD(i32, p->event_parameters[i]);
    FIELD(u32, p->entity_event_sequence);
    FIELD(i32, p->external_event); FIELD(i32, p->external_event_parameter);
    FIELD(i32, p->external_event_time);
    ENUM(p->weapon, QA_Q3_WEAPON_COUNT - 1); ENUM(p->requested_weapon, QA_Q3_WEAPON_COUNT - 1);
    ENUM(p->weapon_phase, QA_Q3_FIRING); ENUM(p->external_slot, QA_Q3_SLOT_RESUME_REQUESTED);
    FIELD(i32, p->weapon_time_ms); FIELD(i32, p->max_health); FIELD(i32, p->handicap);
    FIELD(bool, p->has_last_fire); FIELD(i32, p->last_fire_ms);
    for (size_t i = 0; i < QA_Q3_POWERUP_COUNT; ++i) FIELD(i32, p->powerups[i]);
    for (size_t i = 0; i < QA_Q3_WEAPON_COUNT; ++i) FIELD(i32, p->ammo_time_ms[i]);
    for (size_t i = 0; i < QA_Q3_WEAPON_COUNT; ++i) FIELD(string, p->ammo_regeneration_items[i]);
    ENUM(p->persistent, QA_Q3_POWERUP_COUNT - 1); ENUM(p->holdable, QA_Q3_H_INVULNERABILITY);
    FIELD(i32, p->invulnerability_until); FIELD(i32, p->respawn_after);
    FIELD(i32, p->time_residual); FIELD(i32, p->air_out_time);
    FIELD(i32, p->drowning_damage); FIELD(i32, p->pain_after); FIELD(i32, p->reward_until);
    FIELD(i32, p->accuracy_shots); FIELD(i32, p->accuracy_hits); FIELD(i32, p->rail_streak);
    FIELD(i32, p->impressive_count); FIELD(i32, p->denied_rewards); FIELD(i32, p->player_events);
    FIELD(i32, p->deaths); FIELD(i32, p->excellent_count); FIELD(i32, p->gauntlet_frag_count);
    FIELD(i32, p->last_kill_ms); FIELD(i32, p->dead_yaw);
    FIELD(i32, p->last_killed_client); FIELD(i32, p->last_hurt_client);
    FIELD(i32, p->last_hurt_mod);
    FIELD(i32, p->legs_animation); FIELD(i32, p->torso_animation);
    FIELD(i32, p->legs_timer_ms); FIELD(i32, p->torso_timer_ms);
    FIELD(i32, p->delta_yaw_word); FIELD(i32, p->ground_entity_number);
    FIELD(i32, p->delta_pitch_word); FIELD(i32, p->delta_roll_word);
    FIELD(i32, p->teleport_lock_ms); FIELD(u64, p->teleport_revision);
    FIELD(u32, p->selected_pm_flags); FIELD(i32, p->selected_pm_time_ms);
    FIELD(i32, p->damage_event); FIELD(i32, p->damage_count);
    FIELD(i32, p->damage_pitch); FIELD(i32, p->damage_yaw); FIELD(i32, p->last_command_ms);
    FIELD(i32, p->command_time_ms);
    FIELD(i32, p->portal_id);
    FIELD(i32, p->rank); FIELD(i32, p->persistent_team); FIELD(i32, p->generic1);
    FIELD(i32, p->defend_count); FIELD(i32, p->assist_count); FIELD(i32, p->captures);
    FIELD(i32, p->client_number);
    FIELD(i32, p->fly_sound_after); FIELD(i32, p->jumppad_entity);
    FIELD(i32, p->jumppad_frame); FIELD(i32, p->pmove_frame_count);
    for (size_t i = 0; i < 3; ++i) FIELD(i32, p->last_command_angles[i]);
    FIELD(f32, p->damage_blood); FIELD(f32, p->damage_armor); FIELD(f32, p->damage_knockback);
    FIELD(vec3, p->damage_from); FIELD(string, p->loop_sound);
    FIELD(f32, p->fractional_weapon_ms); FIELD(f32, p->view_height);
    FIELD(vec3, p->view_angles); FIELD(vec3, p->grapple_point);
    FIELD(vec3, p->cutscene.origin); FIELD(vec3, p->cutscene.angles);
    FIELD(vec3, p->cutscene.view_offset); FIELD(bool, p->cutscene.active);
    FIELD(actor, p->hook); FIELD(actor, p->attached_mine);
    FIELD(actor, p->persistent_item); FIELD(actor, p->portal);
    FIELD(bool, p->spectator); FIELD(bool, p->dead); FIELD(bool, p->gibbed);
    FIELD(bool, p->respawned); FIELD(bool, p->use_item_held); FIELD(bool, p->fire_held);
    FIELD(bool, p->grapple_pull); FIELD(bool, p->damage_from_world); FIELD(bool, p->noclip);
    FIELD(bool, p->invulnerability_expanded); FIELD(bool, p->death_cleanup_done);
    FIELD(bool, p->gauntlet_contact); FIELD(bool, p->no_target);
    return true;
}

static bool missile(qa_source_save_io *io, qa_q3_projectile_state *p)
{
    ENUM(p->weapon, QA_Q3_WEAPON_COUNT - 1);
    if (!trajectory(io, &p->trajectory)) return false;
    FIELD(actor, p->owner); FIELD(actor, p->pass); FIELD(actor, p->attached); FIELD(actor, p->trigger);
    FIELD(string, p->team); FIELD(vec3, p->normal); FIELD(vec3, p->damage_point);
    FIELD(f32, p->damage); FIELD(f32, p->splash); FIELD(f32, p->radius);
    FIELD(i32, p->method); FIELD(i32, p->splash_method); FIELD(i32, p->think_at); FIELD(i32, p->event_at);
    FIELD(u32, p->flags); FIELD(string, p->loop_sound);
    ENUM(p->phase, Q3_MISSILE_PROX_DISCARD); FIELD(bool, p->left_owner);
    return true;
}

static bool mover(qa_source_save_io *io, qa_q3_mover_definition *p)
{
    ENUM(p->state.kind, QA_Q3_MOVER_PROXIMITY_MINE);
    if (!trajectory(io, &p->state.position) || !trajectory(io, &p->state.angular)) return false;
    FIELD(i32, p->state.delta_yaw_word); FIELD(i32, p->state.ground_entity_number);
    FIELD(actor, p->state.team_next); FIELD(actor, p->state.proximity_pusher);
    FIELD(vec3, p->state.proximity_direction); FIELD(bool, p->state.stop); FIELD(bool, p->state.team_slave);
    FIELD(vec3, p->first); FIELD(vec3, p->second); FIELD(i32, p->state_index);
    FIELD(f32, p->wait_ms); ENUM(p->blocked, QA_Q3_MOVER_BLOCKED_DOOR);
    FIELD(i32, p->damage); FIELD(i32, p->next_think_ms);
    FIELD(actor, p->team_leader); FIELD(actor, p->activator);
    FIELD(string, p->target); FIELD(string, p->loop_sound);
    FIELD(bool, p->crusher); FIELD(bool, p->map_controlled);
    return true;
}

static bool wire_trajectory(qa_source_save_io *io, qa_q3_trajectory *p)
{
    FIELD(i32, p->type); FIELD(i32, p->time); FIELD(i32, p->duration);
    for (size_t i = 0; i < 3; ++i) FIELD(f32, p->base[i]);
    for (size_t i = 0; i < 3; ++i) FIELD(f32, p->delta[i]);
    return true;
}
static bool wire_entity(qa_source_save_io *io, qa_q3_entity *p)
{
    FIELD(i32, p->number); FIELD(i32, p->eType); FIELD(i32, p->eFlags);
    if (!wire_trajectory(io, &p->pos) || !wire_trajectory(io, &p->apos)) return false;
    FIELD(i32, p->time); FIELD(i32, p->time2);
    for (size_t i = 0; i < 3; ++i) FIELD(f32, p->origin[i]);
    for (size_t i = 0; i < 3; ++i) FIELD(f32, p->origin2[i]);
    for (size_t i = 0; i < 3; ++i) FIELD(f32, p->angles[i]);
    for (size_t i = 0; i < 3; ++i) FIELD(f32, p->angles2[i]);
    FIELD(i32, p->otherEntityNum); FIELD(i32, p->otherEntityNum2);
    FIELD(i32, p->groundEntityNum); FIELD(i32, p->constantLight);
    FIELD(i32, p->loopSound); FIELD(i32, p->modelindex); FIELD(i32, p->modelindex2);
    FIELD(i32, p->clientNum); FIELD(i32, p->frame); FIELD(i32, p->solid);
    FIELD(i32, p->event); FIELD(i32, p->eventParm); FIELD(i32, p->powerups);
    FIELD(i32, p->weapon); FIELD(i32, p->legsAnim); FIELD(i32, p->torsoAnim);
    FIELD(i32, p->generic1);
    return true;
}

static bool wire_player(qa_source_save_io *io, qa_q3_player *p)
{
    ENUM(p->product, QA_Q3_TEAM_ARENA);
    FIELD(i32, p->commandTime); FIELD(i32, p->pmType); FIELD(i32, p->bobCycle);
    FIELD(i32, p->pmFlags); FIELD(i32, p->pmTime);
    for (size_t i = 0; i < 3; ++i) FIELD(f32, p->origin[i]);
    for (size_t i = 0; i < 3; ++i) FIELD(f32, p->velocity[i]);
    FIELD(i32, p->weaponTime); FIELD(i32, p->gravity); FIELD(i32, p->speed);
    for (size_t i = 0; i < 3; ++i) FIELD(i32, p->deltaAngles[i]);
    FIELD(i32, p->groundEntityNum); FIELD(i32, p->legsTimer); FIELD(i32, p->legsAnim);
    FIELD(i32, p->torsoTimer); FIELD(i32, p->torsoAnim); FIELD(i32, p->movementDir);
    for (size_t i = 0; i < 3; ++i) FIELD(f32, p->grapplePoint[i]);
    FIELD(i32, p->eFlags); FIELD(i32, p->eventSequence);
    for (size_t i = 0; i < 2; ++i) FIELD(i32, p->events[i]);
    for (size_t i = 0; i < 2; ++i) FIELD(i32, p->eventParms[i]);
    FIELD(i32, p->externalEvent); FIELD(i32, p->externalEventParm); FIELD(i32, p->externalEventTime);
    FIELD(i32, p->clientNum); FIELD(i32, p->weapon); FIELD(i32, p->weaponState);
    for (size_t i = 0; i < 3; ++i) FIELD(f32, p->viewangles[i]);
    FIELD(i32, p->viewheight); FIELD(i32, p->damageEvent); FIELD(i32, p->damageYaw);
    FIELD(i32, p->damagePitch); FIELD(i32, p->damageCount);
    for (size_t i = 0; i < 16; ++i) FIELD(i32, p->stats[i]);
    for (size_t i = 0; i < 16; ++i) FIELD(i32, p->persistant[i]);
    for (size_t i = 0; i < 16; ++i) FIELD(i32, p->powerups[i]);
    for (size_t i = 0; i < 16; ++i) FIELD(i32, p->ammo[i]);
    FIELD(i32, p->generic1); FIELD(i32, p->loopSound); FIELD(i32, p->jumppadEnt);
    FIELD(i32, p->ping); FIELD(i32, p->pmoveFramecount); FIELD(i32, p->jumppadFrame);
    FIELD(i32, p->entityEventSequence);
    return true;
}

static bool actor_state(qa_source_save_io *io, qa_q3_actor_state *p)
{
    FIELD(actor, p->actor); ENUM(p->kind, Q3_ACTOR_VICTORY_MODEL);
    FIELD(actor, p->enemy); FIELD(bool, p->enemy_source_present); FIELD(u32, p->enemy_source_slot);
    FIELD(bool, p->force_gesture);
    FIELD(i32, p->spawnflags);
    FIELD(i32, p->water_level); FIELD(i32, p->water_type);
    FIELD(f32, p->alpha);
    switch (p->kind) {
    case Q3_ACTOR_PODIUM: case Q3_ACTOR_VICTORY_MODEL:
        if (!wire_entity(io, &p->state.postgame.entity)) return false;
        FIELD(i32, p->state.postgame.timestamp); FIELD(i32, p->state.postgame.count);
        FIELD(i32, p->state.postgame.event_time_ms);
        FIELD(f32, p->state.postgame.physics_bounce); FIELD(bool, p->state.postgame.physics_object);
        return true;
    case Q3_ACTOR_PLAYER: return player(io, &p->state.player);
    case Q3_ACTOR_MISSILE: return missile(io, &p->state.missile);
    case Q3_ACTOR_MOVER: return mover(io, &p->state.mover);
    case Q3_ACTOR_ITEM:
        if (!item_spawn(io, &p->state.item.spawn) || !trajectory(io, &p->state.item.trajectory)) return false;
        FIELD(i32, p->state.item.respawn_at); FIELD(i32, p->state.item.expire_at);
        FIELD(i32, p->state.item.ground_entity_number); FIELD(f32, p->state.item.bounce);
        FIELD(bool, p->state.item.hidden); FIELD(bool, p->state.item.on_ground);
        return true;
    case Q3_ACTOR_PROX_TRIGGER:
        FIELD(actor, p->state.trigger.parent); return true;
    case Q3_ACTOR_KAMIKAZE:
    case Q3_ACTOR_KAMIKAZE_TIMER:
        FIELD(actor, p->state.kamikaze.attacker); FIELD(i32, p->state.kamikaze.start);
        FIELD(i32, p->state.kamikaze.next); FIELD(i32, p->state.kamikaze.elapsed);
        FIELD(vec3, p->state.kamikaze.angles); return true;
    case Q3_ACTOR_PORTAL:
        FIELD(actor, p->state.portal.destination); FIELD(actor, p->state.portal.owner);
        FIELD(i32, p->state.portal.expire_at); FIELD(i32, p->state.portal.activate_at);
        FIELD(vec3, p->state.portal.angles); FIELD(vec3, p->state.portal.fallback);
        FIELD(bool, p->state.portal.source); FIELD(i32, p->state.portal.sequence);
        FIELD(bool, p->state.portal.enabled); return true;
    case Q3_ACTOR_CORPSE:
        FIELD(actor, p->state.corpse.player);
        if (!trajectory(io, &p->state.corpse.trajectory)) return false;
        FIELD(i32, p->state.corpse.animation); FIELD(i32, p->state.corpse.timestamp);
        FIELD(i32, p->state.corpse.next_sink); FIELD(u32, p->state.corpse.flags);
        FIELD(bool, p->state.corpse.physics_object); return true;
    case Q3_ACTOR_TEMPORARY:
        FIELD(i32, p->state.temporary.event_time_ms);
        return wire_entity(io, &p->state.temporary.entity);
    case Q3_ACTOR_OBELISK:
        FIELD(actor, p->state.obelisk.model);
        FIELD(i32, p->state.obelisk.next_think_ms);
        ENUM(p->state.obelisk.think, QA_Q3_OBELISK_RESPAWN); return true;
    case Q3_ACTOR_NONE: break;
    }
    return save_fail(io, "empty Q3 actor in continuation");
}

static bool rules(qa_source_save_io *io, qa_q3_rules *p)
{
    FIELD(i32, p->dmflags);
    FIELD(i32, p->game_type); FIELD(i32, p->proximity_timeout_ms); FIELD(i32, p->force_respawn_seconds);
    FIELD(f32, p->quad_factor); FIELD(f32, p->knockback);
    FIELD(f32, p->weapon_respawn_seconds); FIELD(f32, p->team_weapon_respawn_seconds);
    FIELD(f32, p->gravity);
    FIELD(bool, p->friendly_fire); FIELD(bool, p->blood); FIELD(bool, p->intermission);
    return true;
}

static void *allocate(qa_source_save_io *io, size_t count, size_t size)
{
    if (count > SIZE_MAX / size || io->offset > io->input.size ||
        count > io->input.size - io->offset) {
        save_fail(io, "Q3 continuation allocation exceeds input");
        return NULL;
    }
    void *out = calloc(count, size);
    if (!out)
        qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating Q3 continuation");
    return out;
}

static bool configstring_text(qa_source_save_io *io, char **text)
{
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE && *text ? strlen(*text) : 0;
    if (io->direction == QA_SOURCE_SAVE_WRITE && !*text)
        return save_fail(io, "missing Q3 stored configstring text");
    if (!qa_source_save_count(io, &length, SIZE_MAX - 1)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE)
        return qa_source_save_bytes(io, *text, length);
    if (io->offset > io->input.size || length > io->input.size - io->offset)
        return save_fail(io, "Q3 configstring exceeds continuation input");
    char *copy = malloc(length + 1);
    if (!copy) {
        qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating Q3 stored configstring");
        return false;
    }
    if (!qa_source_save_bytes(io, copy, length) || memchr(copy, 0, length)) {
        free(copy);
        return save_fail(io, "invalid Q3 stored configstring text");
    }
    copy[length] = 0;
    *text = copy;
    return true;
}

static bool checkpoint(qa_source_save_io *io, qa_q3_game *game, qa_q3_checkpoint *p)
{
    FIELD(u32, p->random_state); FIELD(u32, p->death_animation);
    FIELD(u32, p->body_queue_index); ENUM(p->product, QA_Q3_TEAM_ARENA);
    FIELD(u32, p->max_clients);
    if (!p->max_clients || p->max_clients > 64) return save_fail(io, "invalid Q3 source client capacity");
    FIELD(u32, p->source_count);
    FIELD(u32, p->memory.allocated_bytes);
    if (p->memory.allocated_bytes > QA_Q3_SOURCE_MEMORY_BYTES || p->memory.allocated_bytes % 32)
        return save_fail(io, "invalid Q3 GAME memory allocation point");
    if (!qa_source_save_bytes(io, p->memory.pool, p->memory.allocated_bytes)) return false;
    FIELD(bool, p->new_session); FIELD(i32, p->fry_sound_index);
    FIELD(i32, p->portal_sequence);
    FIELD(i32, p->last_team_location_time);
    FIELD(i32, p->client_counts.num_connected); FIELD(i32, p->client_counts.num_non_spectator);
    FIELD(i32, p->client_counts.num_playing); FIELD(i32, p->client_counts.num_voting);
    for (size_t i = 0; i < 2; ++i) FIELD(i32, p->client_counts.num_team_voting[i]);
    FIELD(i32, p->client_counts.follow1); FIELD(i32, p->client_counts.follow2);
    for (size_t i = 0; i < QA_Q3_NATIVE_CLIENTS; ++i) FIELD(u32, p->client_counts.sorted_clients[i]);
    for (size_t i = 0; i < 4; ++i) FIELD(i32, p->team_state.team_scores[i]);
    FIELD(i32, p->team_state.warmup_time_ms); FIELD(f32, p->team_state.last_flag_capture_ms);
    FIELD(i32, p->team_state.last_capture_team); FIELD(i32, p->team_state.red_status);
    FIELD(i32, p->team_state.blue_status); FIELD(i32, p->team_state.neutral_status);
    FIELD(i32, p->team_state.red_taken_ms); FIELD(i32, p->team_state.blue_taken_ms);
    FIELD(i32, p->team_state.red_obelisk_attacked_ms); FIELD(i32, p->team_state.blue_obelisk_attacked_ms);
    FIELD(bool, p->team_state.initialized); FIELD(actor, p->team_state.neutral_obelisk);
    FIELD(i32, p->match_state.intermission_time_ms); FIELD(i32, p->match_state.intermission_queued_ms);
    FIELD(i32, p->match_state.exit_time_ms);
    FIELD(string, p->match_state.changemap);
    FIELD(bool, p->match_state.ready_to_exit); FIELD(bool, p->match_state.restarted);
    FIELD(vec3, p->match_state.intermission_origin); FIELD(vec3, p->match_state.intermission_angles);
    if (p->fry_sound_index < 0 || p->fry_sound_index > 255)
        return save_fail(io, "invalid Q3 saved fry sound index");
    if (p->source_count < QA_Q3_SOURCE_CLIENTS || p->source_count > QA_Q3_SOURCE_WORLD)
        return save_fail(io, "invalid Q3 physical entity extent");
    if (io->direction == QA_SOURCE_SAVE_READ)
        for (size_t i = 0; i < QA_Q3_SOURCE_ENTITIES; ++i)
            p->source_entities[i].client_slot = i < p->max_clients ? (int32_t)i : -1;
    for (size_t i = 0; i <= p->source_count; ++i) {
        size_t slot = i == p->source_count ? QA_Q3_SOURCE_WORLD : i;
        qa_q3_source_binding *binding = &p->source_entities[slot];
        FIELD(actor, binding->actor); FIELD(string, binding->classname);
        FIELD(i32, binding->free_time_ms);
        FIELD(i32, binding->number); FIELD(i32, binding->owner_number);
        FIELD(i32, binding->client_slot); FIELD(bool, binding->body_attached);
        FIELD(u32, binding->server_flags); FIELD(bool, binding->in_use);
        FIELD(bool, binding->never_free);
    }
    const qa_q3_native_client empty_client = {.source_model_shape = QA_SHAPE_BOX};
    const q3_actor empty_actor = {.kind = Q3_ACTOR_PLAYER, .alpha = 1};
    for (size_t i = 0; i < QA_Q3_NATIVE_CLIENTS; ++i) {
        qa_q3_native_client *client = &p->clients[i];
        bool retained = io->direction == QA_SOURCE_SAVE_WRITE &&
            (memcmp(client, &empty_client, sizeof(*client)) ||
             memcmp(&p->source_clients[i], &empty_actor, sizeof(empty_actor)));
        FIELD(bool, retained);
        if (!retained) {
            if (io->direction == QA_SOURCE_SAVE_READ) {
                *client = empty_client;
                p->source_clients[i] = empty_actor;
            }
            continue;
        }
        ENUM(client->connected, QA_Q3_CLIENT_CONNECTED);
        FIELD(i32, client->command.serverTime);
        FIELD(i32, client->session.team); FIELD(i32, client->session.spectator_time_ms);
        FIELD(i32, client->session.spectator_state); FIELD(i32, client->session.spectator_client);
        FIELD(i32, client->session.wins); FIELD(i32, client->session.losses);
        FIELD(i32, client->session.team_leader);
        for (size_t axis = 0; axis < 3; ++axis) FIELD(i32, client->command.angles[axis]);
        FIELD(i32, client->command.buttons); FIELD(u8, client->command.weapon);
        uint8_t forward = (uint8_t)client->command.forwardmove;
        uint8_t right = (uint8_t)client->command.rightmove;
        uint8_t up = (uint8_t)client->command.upmove;
        FIELD(u8, forward); FIELD(u8, right); FIELD(u8, up);
        if (io->direction == QA_SOURCE_SAVE_READ) {
            memcpy(&client->command.forwardmove, &forward, sizeof(forward));
            memcpy(&client->command.rightmove, &right, sizeof(right));
            memcpy(&client->command.upmove, &up, sizeof(up));
        }
        FIELD(i32, client->max_health); FIELD(i32, client->enter_time_ms);
        FIELD(i32, client->team_state); FIELD(i32, client->team_location);
        FIELD(i32, client->team.captures); FIELD(i32, client->team.base_defense);
        FIELD(i32, client->team.carrier_defense); FIELD(i32, client->team.flag_recovery);
        FIELD(i32, client->team.frag_carrier); FIELD(i32, client->team.assists);
        FIELD(f32, client->team.last_hurt_carrier_ms); FIELD(f32, client->team.last_returned_flag_ms);
        FIELD(f32, client->team.flag_since_ms); FIELD(f32, client->team.last_fragged_carrier_ms);
        FIELD(i32, client->retired_score); ENUM(client->source_model_shape, QA_SHAPE_CAPSULE);
        FIELD(i32, client->inactivity_time_ms); FIELD(bool, client->inactivity_warning);
        FIELD(vec3, client->old_origin);
        FIELD(bool, client->has_followed_player);
        if (!wire_player(io, &client->followed_player)) return false;
        FIELD(i32, client->switch_team_time_ms);
        FIELD(i32, client->ping); FIELD(i32, client->vote_count); FIELD(i32, client->team_vote_count);
        FIELD(u32, client->old_buttons); FIELD(u32, client->buttons); FIELD(u32, client->latched_buttons);
        if (!qa_source_save_bytes(io, client->netname, sizeof(client->netname)) ||
            !memchr(client->netname, 0, sizeof(client->netname))) return save_fail(io, "invalid Q3 retained netname");
        FIELD(bool, client->local_client); FIELD(bool, client->initial_spawn);
        FIELD(bool, client->predict_item_pickup); FIELD(bool, client->pmove_fixed);
        FIELD(bool, client->team_info);
        FIELD(bool, client->ready_to_exit);
        if (!actor_state(io, &p->source_clients[i]) ||
            p->source_clients[i].kind != Q3_ACTOR_PLAYER)
            return save_fail(io, "invalid Q3 retained source client");
    }
    if (!rules(io, &p->rules)) return false;
    FIELD(i32, p->previous_ms); FIELD(i32, p->now_ms); FIELD(u64, p->attack_sequence);
    FIELD(i32, p->ranking_hit.frame); FIELD(i32, p->ranking_hit.self);
    FIELD(i32, p->ranking_hit.attacker); FIELD(i32, p->ranking_hit.method); FIELD(bool, p->ranking_hit.valid);
    for (size_t i = 0; i < 8; ++i) FIELD(actor, p->body_queue[i]);
    for (size_t i = 0; i < 3; ++i) FIELD(u32, p->podium_players[i]);
    if (!qa_source_save_count(io, &p->actor_count, game->capacity)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && p->actor_count) {
        p->actors = allocate(io, p->actor_count, sizeof(*p->actors));
        if (!p->actors) return false;
    }
    for (size_t i = 0; i < p->actor_count; ++i) if (!actor_state(io, &p->actors[i])) return false;
    if (!qa_source_save_count(io, &p->cooldown_count, game->capacity)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && p->cooldown_count) {
        p->kamikaze_cooldowns = allocate(io, p->cooldown_count, sizeof(*p->kamikaze_cooldowns));
        if (!p->kamikaze_cooldowns) return false;
    }
    for (size_t i = 0; i < p->cooldown_count; ++i) {
        FIELD(actor, p->kamikaze_cooldowns[i].actor);
        FIELD(i32, p->kamikaze_cooldowns[i].damage_after);
        FIELD(i32, p->kamikaze_cooldowns[i].shock_after);
    }
    if (!qa_source_save_count(io, &p->configstring_count, QA_Q3_NATIVE_CONFIGSTRINGS)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && p->configstring_count) {
        p->configstrings = allocate(io, p->configstring_count, sizeof(*p->configstrings));
        if (!p->configstrings) return false;
    }
    for (size_t i = 0; i < p->configstring_count; ++i) {
        FIELD(u32, p->configstrings[i].index);
        if (p->configstrings[i].index >= QA_Q3_NATIVE_CONFIGSTRINGS ||
            (i && p->configstrings[i].index <= p->configstrings[i - 1].index))
            return save_fail(io, "invalid Q3 stored configstring order");
        if (!configstring_text(io, &p->configstrings[i].text)) return false;
    }
    if (!q3_shader_remaps_codec(io, &p->shader_remaps)) return false;
    if (!qa_source_save_count(io, &p->wire_state.size, SIZE_MAX)) return false;
    if (!p->wire_state.size) return save_fail(io, "missing Q3 source wire continuation");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (io->offset > io->input.size || p->wire_state.size > io->input.size - io->offset)
            return save_fail(io, "Q3 wire continuation exceeds its input");
        p->wire_state.data = malloc(p->wire_state.size);
        if (!p->wire_state.data) {
            qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating Q3 wire continuation");
            return false;
        }
    }
    if (!qa_source_save_bytes(io, p->wire_state.data, p->wire_state.size)) return false;
    return true;
}

static bool saved_actor(qa_source_save_io *io, qa_saved_actor_id *p)
{
    FIELD(u64, p->generation); FIELD(u32, p->slot);
    return true;
}

static bool map_actor(qa_source_save_io *io, qa_q3_map_actor_checkpoint *p)
{
    if (!saved_actor(io, &p->actor) || !saved_actor(io, &p->activator) ||
        !saved_actor(io, &p->enemy) || !saved_actor(io, &p->team_master) ||
        !saved_actor(io, &p->team_next) || !saved_actor(io, &p->parent) ||
        !saved_actor(io, &p->path_next)) return false;
    qa_q3_map_actor_state *s = &p->state;
    ENUM(s->kind, QA_Q3_MAP_MOVER_PLAT_TRIGGER); ENUM(s->think, QA_Q3_MAP_THINK_MOVER_TRAIN_RESUME);
    FIELD(string, s->classname); FIELD(string, s->model); FIELD(string, s->model2);
    FIELD(string, s->targetname); FIELD(string, s->target); FIELD(string, s->message); FIELD(string, s->team);
    FIELD(string, s->noise); FIELD(string, s->shader_old); FIELD(string, s->shader_new);
    FIELD(vec3, s->origin); FIELD(vec3, s->angles); FIELD(vec3, s->direction); FIELD(vec3, s->launch_velocity);
    FIELD(vec3, s->first); FIELD(vec3, s->second); FIELD(vec3, s->color);
    FIELD(vec3, s->bounds.mins); FIELD(vec3, s->bounds.maxs);
    if (!item_spawn(io, &s->item)) return false;
    FIELD(u32, s->spawnflags); FIELD(u32, s->inline_model); FIELD(u32, s->ordinal);
    FIELD(i32, s->count); FIELD(i32, s->health); FIELD(i32, s->damage); FIELD(i32, s->due_ms);
    FIELD(i32, s->cooldown_ms); FIELD(i32, s->sound_frame); FIELD(i32, s->sound_random);
    FIELD(i32, s->noise_index); FIELD(i32, s->sound_1_to_2); FIELD(i32, s->sound_2_to_1);
    FIELD(i32, s->sound_pos_1); FIELD(i32, s->sound_pos_2);
    FIELD(i32, s->sound_loop);
    FIELD(f32, s->speed); FIELD(f32, s->wait); FIELD(f32, s->random);
    FIELD(f32, s->delay); FIELD(f32, s->roll); FIELD(f32, s->light);
    FIELD(f32, s->alpha);
    FIELD(bool, s->active); FIELD(bool, s->linked); FIELD(bool, s->has_inline_model);
    FIELD(bool, s->touchable); FIELD(bool, s->usable); FIELD(bool, s->team_slave);
    FIELD(bool, s->item_bound); FIELD(bool, s->damageable); FIELD(bool, s->has_delay);
    FIELD(bool, s->sound_looping); FIELD(bool, s->has_color); FIELD(bool, s->has_light);
    FIELD(bool, s->no_bots); FIELD(bool, s->no_humans);
    return true;
}

static bool map_checkpoint(qa_source_save_io *io, qa_q3_game *game, qa_q3_map_checkpoint *p)
{
    FIELD(i32, p->loaded_game_type);
    if (p->loaded_game_type < -1) return save_fail(io, "invalid Q3 loaded game type");
    FIELD(u64, p->registered_items); FIELD(f32, p->gravity);
    FIELD(string, p->motd); FIELD(u32, p->random_seed); FIELD(i32, p->start_time_ms);
    FIELD(i32, p->restarted); FIELD(bool, p->warmup);
    FIELD(bool, p->world_spawned); FIELD(bool, p->post_spawned); FIELD(bool, p->locations_linked);
    if (!saved_actor(io, &p->location_head)) return false;
    if (p->loaded_game_type >= 0 && (!p->world_spawned || !p->post_spawned))
        return save_fail(io, "Q3 loaded game type lacks completed authored admission");
    if (!qa_source_save_count(io, &p->actor_count, game->capacity)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && p->actor_count) {
        p->actors = allocate(io, p->actor_count, sizeof(*p->actors));
        if (!p->actors) return false;
    }
    for (size_t i = 0; i < p->actor_count; ++i) if (!map_actor(io, &p->actors[i])) return false;
    return true;
}

static bool private_lease(qa_source_save_io *io, qa_q3_game *game,
                            const q3_actor *actor, q3_saved_leases *leases)
{
    if (actor->actor.slot >= game->capacity)
        return save_fail(io, "Q3 private lease actor is outside the candidate");
    q3_saved_leases *saved = &leases[actor->actor.slot];
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        const q3_inventory_owner *owner = &game->inventory_owners[actor->actor.slot];
        const qa_pickup_lease *observation = &game->item_observations[actor->actor.slot];
        *saved = (q3_saved_leases){.inventory = owner->actor.registry != 0,
            .selections = owner->selections, .weapons = owner->weapons.serial,
            .holdables = owner->holdables.serial, .observation = observation->serial};
    }
    FIELD(bool, saved->inventory); FIELD(u32, saved->selections);
    FIELD(u64, saved->weapons); FIELD(u64, saved->holdables); FIELD(u64, saved->observation);
    if ((!saved->inventory && (saved->selections || saved->weapons || saved->holdables)) ||
        (saved->inventory && (actor->kind != Q3_ACTOR_PLAYER ||
            (saved->selections & ~actor->state.player.selections))) ||
        (saved->weapons && !(saved->selections & QA_Q3_ARSENAL)) ||
        (saved->holdables && !(saved->selections & QA_Q3_EQUIPMENT)) ||
        (saved->weapons && saved->weapons == saved->holdables) ||
        ((actor->kind == Q3_ACTOR_ITEM) != (saved->observation != 0)))
        return save_fail(io, "Q3 private leases differ from native continuation roles");
    return true;
}

static bool private_leases(qa_source_save_io *io, qa_q3_game *game,
                            const qa_q3_checkpoint *native, q3_saved_leases *leases)
{
    for (size_t i = 0; i < native->actor_count; ++i)
        if (!private_lease(io, game, &native->actors[i], leases)) return false;
    for (size_t i = 0; i < QA_Q3_NATIVE_CLIENTS; ++i)
        if (native->source_clients[i].actor.registry &&
            !private_lease(io, game, &native->source_clients[i], leases)) return false;
    return true;
}

static bool bindings_current(qa_q3_game *game, qa_error *error)
{
    if (!q3_wire_validate(game, game->wire, error)) return false;
    for (uint32_t i = 0; i < game->capacity; ++i) {
        const q3_actor *actor = q3_actor_const(game, q3_actor_at(game, i)->actor);
        const q3_inventory_owner *owner = &game->inventory_owners[i];
        const qa_pickup_lease *observation = &game->item_observations[i];
        if (owner->actor.registry || owner->weapons.serial || owner->holdables.serial) {
            if (!actor || actor->kind != Q3_ACTOR_PLAYER || owner->game != game ||
                !qa_actor_id_equal(owner->actor, actor->actor) ||
                (owner->selections & ~actor->state.player.selections) ||
                (owner->weapons.serial && (!(owner->selections & QA_Q3_ARSENAL) ||
                    !qa_actor_id_equal(owner->weapons.actor, actor->actor) ||
                    !qa_inventory_lease_current(game->options.services.inventory, owner->weapons))) ||
                (owner->holdables.serial && (!(owner->selections & QA_Q3_EQUIPMENT) ||
                    !qa_actor_id_equal(owner->holdables.actor, actor->actor) ||
                    !qa_inventory_lease_current(game->options.services.inventory, owner->holdables))))
                return q3_fail(error, "Q3 native inventory private lease is not current");
        }
        if (observation->serial || (actor && actor->kind == Q3_ACTOR_ITEM)) {
            if (!actor || actor->kind != Q3_ACTOR_ITEM || !observation->serial ||
                !qa_actor_id_equal(observation->actor, actor->actor) ||
                !qa_pickups_observation_current(game->options.services.pickups, *observation))
                return q3_fail(error, "Q3 native item private observation is not current");
        }
    }
    if (game->map)
        for (uint32_t i = 0; i < game->map->capacity; ++i) {
            qa_q3_map_actor_state *state = &game->map->actors[i];
            if (!state->active) continue;
            qa_target_binding expected_target, actual_target;
            if (!q3_map_target_binding(game, state->actor, &expected_target) ||
                !qa_persistence_targets_binding(game->map->options.targets, state->actor, &actual_target) ||
                actual_target.source != expected_target.source ||
                actual_target.context != expected_target.context ||
                actual_target.read != expected_target.read || actual_target.use != expected_target.use ||
                actual_target.field != expected_target.field ||
                actual_target.set_targetname != expected_target.set_targetname ||
                actual_target.set_target != expected_target.set_target ||
                actual_target.remap_shader != expected_target.remap_shader ||
                actual_target.set_delay != expected_target.set_delay)
                return q3_fail(error, "Q3 authored target binding is not current");
            if (!state->damageable) continue;
            qa_combat_admission expected, actual;
            if (!qa_q3_game_damage_admission(game, state->actor, &expected, error) ||
                !qa_persistence_combat_admission(game->options.services.combat, state->actor, &actual) ||
                actual.context != expected.context || actual.admit != expected.admit)
                return q3_fail(error, "Q3 authored mover damage admission is not current");
        }
    return true;
}

static bool continuation(qa_source_save_io *io, qa_q3_game *game,
                          qa_q3_checkpoint *native, qa_q3_map_checkpoint *map,
                          q3_saved_leases *leases)
{
    uint8_t signature[8] = {'Q', 'A', 'Q', '3', 'S', 'A', 'V', 'E'};
    static const uint8_t expected[8] = {'Q', 'A', 'Q', '3', 'S', 'A', 'V', 'E'};
    if (!qa_source_save_bytes(io, signature, sizeof(signature)) ||
        memcmp(signature, expected, sizeof(signature))) return save_fail(io, "invalid Q3 save signature");
    if (!checkpoint(io, game, native)) return false;
    bool has_map = game->map != NULL;
    FIELD(bool, has_map);
    if (has_map != (game->map != NULL)) return save_fail(io, "Q3 authored map owner differs");
    return (!has_map || map_checkpoint(io, game, map)) && private_leases(io, game, native, leases);
}

bool qa_q3_game_capture(qa_q3_game *game, qa_buffer *out, qa_error *error)
{
    if (!game || !out || game->source_restored || !qa_world_idle(game->options.services.world))
        return q3_fail(error, "Q3 portable capture requires an idle world");
    for (uint32_t i = 0; i < game->capacity; ++i)
        if (game->player_binding_tokens[i])
            return q3_fail(error, "Q3 portable capture conflicts with player admission");
    q3_saved_leases *leases = calloc(game->capacity, sizeof(*leases));
    if (!leases) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 private lease capture");
        return false;
    }
    qa_q3_checkpoint native = {0};
    qa_q3_map_checkpoint map = {0};
    qa_source_save_io io = {0};
    bool okay = bindings_current(game, error) && qa_q3_checkpoint_capture(game, &native, error) &&
        (!game->map || qa_q3_map_checkpoint_capture(game, &map, error)) &&
        qa_source_save_writer(&io, game->options.services.session, error) &&
        continuation(&io, game, &native, &map, leases) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    qa_q3_checkpoint_free(&native); qa_q3_map_checkpoint_free(&map);
    free(leases);
    return okay;
}

bool qa_q3_game_restore(qa_q3_game *game, qa_bytes input, qa_error *error)
{
    if (!game || game->source_restored || game->observation_depth || !qa_session_safe(game->options.services.session) ||
        !qa_world_idle(game->options.services.world) || !qa_combat_idle(game->options.services.combat))
        return q3_fail(error, "Q3 portable restore requires an idle candidate");
    for (uint32_t i = 0; i < QA_Q3_SOURCE_ENTITIES; ++i)
        if (game->source_entities[i].actor.registry)
            return q3_fail(error, "Q3 portable restore requires an empty physical source candidate");
    for (uint32_t i = 0; i < game->capacity; ++i)
        if (game->actors[i].kind != Q3_ACTOR_NONE || game->player_binding_tokens[i] ||
            game->inventory_owners[i].actor.registry || game->item_observations[i].serial ||
            (game->map && game->map->actors[i].active))
            return q3_fail(error, "Q3 portable restore requires an empty native candidate");
    q3_saved_leases *leases = calloc(game->capacity, sizeof(*leases));
    if (!leases) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 private lease restore");
        return false;
    }
    qa_q3_checkpoint native = {0};
    qa_q3_map_checkpoint map = {0};
    qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, game->options.services.session, input, error) &&
        continuation(&io, game, &native, &map, leases) && qa_source_save_finish(&io, NULL) &&
        q3_checkpoint_restore_source(game, &native, error);
    if (okay) {
        game->source_restored = true;
        okay = !game->map || q3_map_checkpoint_restore_source(game, &map, error);
    }
    if (okay) {
        for (uint32_t i = 0; i < game->capacity; ++i) {
            const q3_actor *entry = q3_actor_at_const(game, i);
            if (!q3_actor_const(game, entry->actor)) continue;
            qa_actor_id actor = entry->actor;
            const q3_saved_leases *saved = &leases[actor.slot];
            if (saved->inventory)
                game->inventory_owners[actor.slot] = (q3_inventory_owner){.game = game,
                    .actor = actor, .selections = saved->selections,
                    .weapons = {.actor = actor, .serial = saved->weapons},
                    .holdables = {.actor = actor, .serial = saved->holdables}};
            if (saved->observation)
                game->item_observations[actor.slot] = (qa_pickup_lease){.actor = actor,
                    .serial = saved->observation};
        }
    }
    qa_source_save_dispose(&io);
    qa_q3_checkpoint_free(&native); qa_q3_map_checkpoint_free(&map);
    free(leases);
    return okay;
}

bool qa_q3_game_reconnect(qa_q3_game *game, qa_error *error)
{
    if (!game || game->observation_depth || !q3_source_origins_idle(game) ||
        !qa_session_safe(game->options.services.session) ||
        !qa_world_idle(game->options.services.world) || !qa_combat_idle(game->options.services.combat))
        return q3_fail(error, "Q3 reconnect requires an idle restored candidate");
    for (uint32_t i = 0; i < game->capacity; ++i) {
        const q3_actor *entry = q3_actor_const(game, q3_actor_at(game, i)->actor);
        if (entry && entry->kind == Q3_ACTOR_OBELISK &&
            !q3_obelisk_reconnect(game, entry->actor, error)) return false;
    }
    if (!bindings_current(game, error)) return false;
    game->source_restored = false;
    return true;
}

bool qa_q3_game_target_binding(qa_q3_game *game, qa_actor_id actor,
                               qa_target_binding *out, qa_error *error)
{
    if (!game || !out)
        return q3_fail(error, "Q3 target resolution requires a source and output");
    return q3_map_target_binding(game, actor, out);
}

#undef FIELD
#undef ENUM
