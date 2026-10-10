#include "map/internal.h"
#include "qa/game_type.h"
#include "source_wire.h"
#include "qa/game_q3_clients.h"

typedef struct q3_wire_link_state {
    int32_t area, area2, last_cluster, clusters[16];
    uint32_t cluster_count;
    uint64_t link_count;
    bool written;
} q3_wire_link_state;

typedef struct q3_wire_row {
    qa_actor_id actor;
    q3_wire_entity_source source;
    /* Native s.pos/s.apos are publication state, distinct from r.currentOrigin
     * and from authoritative player movement. BG conversion writes them. */
    qa_q3_trajectory player_position, player_angles;
    int32_t event_time_ms;
    q3_wire_link_state link;
    bool player_published, initialized;
} q3_wire_row;

typedef struct q3_wire_client {
    qa_q3_wire_policy foreign_policy;
    int32_t hits, attacker, attackee_armor, clients_ready, loop_sound;
    int32_t special_ammo[16];
    bool foreign_policy_written, movement_detached;
} q3_wire_client;

struct q3_wire_state {
    qa_q3_wire_services services;
    q3_wire_row rows[QA_Q3_SOURCE_ENTITIES];
    q3_wire_client clients[QA_Q3_SOURCE_CLIENTS];
    qa_q3_player_motion foreign_motions[QA_Q3_SOURCE_CLIENTS];
};

static bool fields_valid(const q3_wire_row *, uint32_t);

static int32_t word(uint32_t bits) {
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void vector(float out[3], qa_vec3 value) {
    out[0] = value.x; out[1] = value.y; out[2] = value.z;
}

static bool current(const qa_q3_game *game, uint32_t slot, qa_actor_id actor) {
    return slot < QA_Q3_SOURCE_ENTITIES &&
        qa_actor_id_equal(game->source_entities[slot].actor, actor) &&
        qa_actor_id_equal(game->wire->rows[slot].actor, actor) &&
        qa_actors_get(qa_session_actors(game->options.services.session), actor);
}

static q3_wire_row *row(qa_q3_game *game, qa_actor_id actor) {
    uint32_t slot;
    qa_error ignored = {0};
    return game && game->wire &&
        qa_q3_source_actor_slot(game, actor, &slot, &ignored) && current(game, slot, actor)
        ? &game->wire->rows[slot] : NULL;
}

static qa_q3_player_motion *retired_motion(qa_q3_game *game, qa_actor_id actor) {
    uint32_t slot;
    return game->wire && q3_source_client_pointer(game, actor, &slot)
        ? &game->wire->foreign_motions[slot] : NULL;
}
static qa_q3_foreign_movement *foreign_movement(qa_q3_game *game, qa_actor_id actor) {
    q3_actor *entry = q3_actor_get(game, actor);
    return entry && entry->kind == Q3_ACTOR_PLAYER ? &entry->state.player.foreign_movement : NULL;
}
static bool movement_control(const qa_q3_game *game, qa_actor_id actor,
                              qa_builtin_player_control *control, bool *selected,
                              qa_error *error) {
    if (!q3_player_control(game, actor, control, error)) return false;
    uint32_t slot;
    bool native = q3_source_client_pointer(game, actor, &slot);
    *selected = control->state->kind == QA_RULESET_Q3 &&
        (!native || (control->source_movement && !game->wire->clients[slot].movement_detached));
    return true;
}
bool q3_player_motion_read(const qa_q3_game *game, qa_actor_id actor,
                           qa_q3_player_motion *out, qa_error *error) {
    uint32_t slot;
    bool native = q3_source_client_pointer(game, actor, &slot);
    if (native && game->wire->clients[slot].movement_detached) {
        *out = game->wire->foreign_motions[slot];
        return true;
    }
    qa_builtin_player_control control;
    bool selected = false;
    if (!movement_control(game, actor, &control, &selected, error)) return false;
    *out = (qa_q3_player_motion){0};
    if (selected) {
        const qa_q3_movement_state *state = &control.state->data.q3;
        out->command_time_ms = state->command_time_ms;
        out->delta_pitch_word = state->delta_angle_words[0];
        out->delta_yaw_word = state->delta_angle_words[1];
        out->delta_roll_word = state->delta_angle_words[2];
        out->pmove_frame_count = state->movement_frame;
        out->jumppad_frame = state->jump_pad_frame;
        out->jumppad_entity = state->jump_pad.registry ? q3_entity_number(game, state->jump_pad) : 0;
    } else {
        const qa_q3_foreign_movement *foreign = foreign_movement((qa_q3_game *)game, actor);
        if (foreign) {
            out->command_time_ms = foreign->command_time_ms;
            out->delta_pitch_word = foreign->delta_angle_words[0];
            out->delta_yaw_word = foreign->delta_angle_words[1];
            out->delta_roll_word = foreign->delta_angle_words[2];
            out->pmove_frame_count = foreign->movement_frame;
            out->jumppad_frame = foreign->jump_pad_frame;
            out->jumppad_entity = foreign->jump_pad_entity;
        }
    }
    out->view_angles = *control.view_angles;
    out->view_height = *control.view_height;
    out->ground_entity_number = control.ground->hit == QA_TRACE_HIT_WORLD ? (int32_t)QA_Q3_SOURCE_WORLD :
        control.ground->hit == QA_TRACE_HIT_ACTOR ? q3_entity_number(game, control.ground->actor) : (int32_t)QA_Q3_SOURCE_NONE;
    return true;
}
bool q3_player_motion_slot_read(const qa_q3_game *game, uint32_t slot,
                                qa_q3_player_motion *out, qa_error *error) {
    qa_actor_id actor = game->source_entities[slot].actor;
    if (!actor.registry || !game->source_entities[slot].body_attached ||
        game->wire->clients[slot].movement_detached) {
        *out = game->wire->foreign_motions[slot];
        return true;
    }
    return q3_player_motion_read(game, actor, out, error);
}
void q3_player_motion_slots_restore(qa_q3_game *game, const qa_q3_player_motion *motions) {
    for (uint32_t i = 0; i < QA_Q3_SOURCE_CLIENTS; ++i) {
        qa_actor_id actor = game->source_entities[i].actor;
        if (actor.registry && game->source_entities[i].body_attached &&
            !game->wire->clients[i].movement_detached)
            q3_player_motion_restore(game, actor, &motions[i]);
        else game->wire->foreign_motions[i] = motions[i];
    }
}
void q3_player_command_time_write(qa_q3_game *game, qa_actor_id actor, int32_t time) {
    qa_builtin_player_control control;
    bool selected = false;
    if (movement_control(game, actor, &control, &selected, NULL) && selected) control.state->data.q3.command_time_ms = time;
    else {
        qa_q3_foreign_movement *foreign = foreign_movement(game, actor);
        if (foreign) foreign->command_time_ms = time;
    }
}
void q3_player_jumppad_write(qa_q3_game *game, qa_actor_id actor, qa_actor_id pad, int32_t frame) {
    qa_builtin_player_control control;
    bool selected = false;
    if (movement_control(game, actor, &control, &selected, NULL) && selected) {
        control.state->data.q3.jump_pad = pad;
        control.state->data.q3.jump_pad_frame = frame;
    } else {
        qa_q3_foreign_movement *foreign = foreign_movement(game, actor);
        if (foreign) { foreign->jump_pad_entity = pad.registry ? q3_entity_number(game, pad) : 0;
            foreign->jump_pad_frame = frame; }
    }
}
void q3_player_delta_write(qa_q3_game *game, qa_actor_id actor, unsigned axis, int32_t word_value) {
    qa_builtin_player_control control;
    bool selected = false;
    if (movement_control(game, actor, &control, &selected, NULL) && selected) control.state->data.q3.delta_angle_words[axis] = word_value;
    else {
        qa_q3_foreign_movement *foreign = foreign_movement(game, actor);
        if (foreign) foreign->delta_angle_words[axis] = word_value;
    }
}
void q3_player_view_write(qa_q3_game *game, qa_actor_id actor, qa_vec3 view) {
    qa_builtin_player_control control;
    if (q3_player_control(game, actor, &control, NULL)) {
        *control.view_angles = view;
        control.player->view_angles = view;
    } else {
        qa_q3_player_motion *retired = retired_motion(game, actor);
        if (retired) retired->view_angles = view;
    }
}
void q3_player_height_write(qa_q3_game *game, qa_actor_id actor, float height) {
    qa_builtin_player_control control;
    if (q3_player_control(game, actor, &control, NULL)) {
        *control.view_height = height;
        control.player->view_height = height;
    } else {
        qa_q3_player_motion *retired = retired_motion(game, actor);
        if (retired) retired->view_height = height;
    }
}
void q3_player_motion_restore(qa_q3_game *game, qa_actor_id actor, const qa_q3_player_motion *motion) {
    qa_builtin_player_control control;
    bool selected = false;
    if (movement_control(game, actor, &control, &selected, NULL) && selected) {
        qa_q3_movement_state *state = &control.state->data.q3;
        state->command_time_ms = motion->command_time_ms;
        state->movement_frame = motion->pmove_frame_count;
        state->jump_pad_frame = motion->jumppad_frame;
        state->delta_angle_words[0] = motion->delta_pitch_word;
        state->delta_angle_words[1] = motion->delta_yaw_word;
        state->delta_angle_words[2] = motion->delta_roll_word;
        qa_q3_source_binding binding;
        state->jump_pad = motion->jumppad_entity > 0 &&
            qa_q3_source_binding_read(game, (uint32_t)motion->jumppad_entity, &binding, NULL)
            ? binding.actor : (qa_actor_id){0};
    } else {
        qa_q3_foreign_movement *foreign = foreign_movement(game, actor);
        if (foreign) *foreign = (qa_q3_foreign_movement){
            .command_time_ms = motion->command_time_ms,
            .delta_angle_words = {motion->delta_pitch_word, motion->delta_yaw_word, motion->delta_roll_word},
            .movement_frame = motion->pmove_frame_count, .jump_pad_entity = motion->jumppad_entity,
            .jump_pad_frame = motion->jumppad_frame};
    }
    q3_player_view_write(game, actor, motion->view_angles);
    q3_player_height_write(game, actor, motion->view_height);
}
void q3_player_motion_clear(qa_q3_game *game, qa_actor_id actor) {
    q3_player_motion_restore(game, actor, &(qa_q3_player_motion){0});
}

bool q3_wire_create(qa_q3_game *game, qa_error *error) {
    if (!game || game->wire) return q3_fail(error, "Q3 source wire owner is already present");
    game->wire = calloc(1, sizeof(*game->wire));
    if (!game->wire) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 source wire records");
        return false;
    }
    /* Fixed gentities and GameClient PS exist before actor admission. Their
     * source constructors author zero s and PM fields independently. */
    for (uint32_t slot = 0; slot < QA_Q3_SOURCE_CLIENTS; ++slot) {
        game->wire->rows[slot].initialized = true;
        game->wire->clients[slot].foreign_policy_written = true;
    }
    return true;
}

void q3_wire_destroy(qa_q3_game *game) {
    if (!game) return;
    free(game->wire);
    game->wire = NULL;
}

void q3_wire_reset(qa_q3_game *game) {
    if (!game || !game->wire) return;
    qa_q3_wire_services services = game->wire->services;
    memset(game->wire, 0, sizeof(*game->wire));
    game->wire->services = services;
    for (uint32_t slot = 0; slot < QA_Q3_SOURCE_CLIENTS; ++slot) {
        game->wire->rows[slot].initialized = true;
        game->wire->clients[slot].foreign_policy_written = true;
    }
}

void q3_wire_bind(qa_q3_game *game, uint32_t slot, qa_actor_id actor) {
    if (!game || !game->wire || slot >= QA_Q3_SOURCE_ENTITIES) return;
    q3_wire_row *record = &game->wire->rows[slot];
    if (qa_actor_id_equal(record->actor, actor)) return;
    if (slot < QA_Q3_SOURCE_CLIENTS) record->actor = actor;
    else {
        q3_wire_entity_source source = record->source;
        *record = (q3_wire_row){.actor = actor, .source = source};
    }
}

void q3_wire_release(qa_q3_game *game, uint32_t slot, qa_actor_id actor) {
    if (!game || !game->wire || slot >= QA_Q3_SOURCE_ENTITIES ||
        !qa_actor_id_equal(game->wire->rows[slot].actor, actor)) return;
    if (slot < QA_Q3_SOURCE_CLIENTS) game->wire->rows[slot].actor = (qa_actor_id){0};
    else game->wire->rows[slot] = (q3_wire_row){0};
}

q3_wire_entity_source *q3_wire_entity(qa_q3_game *game, qa_actor_id actor) {
    q3_wire_row *record = row(game, actor);
    return record ? &record->source : NULL;
}
q3_wire_entity_source *q3_wire_entity_slot(qa_q3_game *game, uint32_t slot) {
    return game && game->wire && slot < QA_Q3_SOURCE_NONE && !game->source_restored
        ? &game->wire->rows[slot].source : NULL;
}

bool q3_wire_entity_ready(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_wire_row *record = row(game, actor);
    uint32_t slot;
    if (!record || game->source_restored ||
        !qa_q3_source_actor_slot(game, actor, &slot, error) || !fields_valid(record, slot))
        return q3_fail(error, "Q3 source constructor has not committed valid native entity fields");
    record->initialized = true;
    return true;
}

static qa_trajectory native_trajectory(const qa_q3_trajectory *source) {
    return (qa_trajectory){.type = (qa_trajectory_type)source->type,
        .time_ms = source->time, .duration_ms = source->duration,
        .base = qa_v3(source->base[0], source->base[1], source->base[2]),
        .delta = qa_v3(source->delta[0], source->delta[1], source->delta[2])};
}

bool q3_wire_copy_body(qa_q3_game *game, qa_actor_id player, qa_actor_id corpse,
                       qa_trajectory *position, uint32_t *source_flags, qa_error *error) {
    q3_wire_row *from = row(game, player), *to = row(game, corpse);
    uint32_t source_slot;
    const q3_actor *client = game ? q3_actor_const(game, player) : NULL;
    if (!from || !to || !position || !source_flags || game->source_restored || !from->initialized ||
        !from->player_published || !client || client->kind != Q3_ACTOR_PLAYER ||
        !qa_q3_source_actor_slot(game, corpse, &source_slot, error) ||
        source_slot < QA_Q3_SOURCE_CLIENTS || source_slot >= QA_Q3_SOURCE_WORLD)
        return q3_fail(error, "Q3 body copy lacks the actual published source player or corpse row");
    int32_t single_client = to->source.single_client;
    bool free_after_event = to->source.free_after_event;
    bool unlink_after_event = to->source.unlink_after_event;
    to->source = from->source;
    to->source.single_client = single_client;
    to->source.free_after_event = free_after_event;
    to->source.unlink_after_event = unlink_after_event;
    *source_flags = (uint32_t)from->source.flags;
    to->source.flags = 0;
    to->source.angular = native_trajectory(&from->player_angles);
    *position = native_trajectory(&from->player_position);
    game->source_entities[source_slot].number = (int32_t)source_slot;
    to->player_published = false;
    to->initialized = false;
    return true;
}

void q3_wire_client_clear(qa_q3_game *game, uint32_t slot) {
    if (game && game->wire && slot < QA_Q3_SOURCE_CLIENTS) {
        game->wire->clients[slot] = (q3_wire_client){.foreign_policy_written = true};
        game->wire->foreign_motions[slot] = (qa_q3_player_motion){0};
    }
}

void q3_wire_client_spawn_clear(qa_q3_game *game, uint32_t slot) {
    if (!game || !game->wire || slot >= QA_Q3_SOURCE_CLIENTS) return;
    q3_wire_client *client = &game->wire->clients[slot];
    client->clients_ready = 0;
    client->loop_sound = 0;
    memset(client->special_ammo, 0, sizeof(client->special_ammo));
    client->foreign_policy = (qa_q3_wire_policy){0};
    client->foreign_policy_written = true;
    client->movement_detached = false;
}

void q3_wire_selected_client_clear(qa_q3_game *game, uint32_t slot) {
    if (!game || !game->wire || slot >= QA_Q3_SOURCE_CLIENTS) return;
    q3_wire_client *client = &game->wire->clients[slot];
    client->clients_ready = 0;
    client->loop_sound = 0;
    client->foreign_policy = (qa_q3_wire_policy){0};
    client->foreign_policy_written = true;
    client->movement_detached = false;
}

void q3_wire_client_follow_copy(qa_q3_game *game, uint32_t slot, const qa_q3_player *source) {
    q3_wire_client *client = &game->wire->clients[slot];
    client->hits = source->persistant[1];
    client->attacker = source->persistant[6];
    client->attackee_armor = source->persistant[7];
    client->clients_ready = source->stats[5 + (game->options.product == QA_Q3_TEAM_ARENA)];
    client->loop_sound = source->loopSound;
}

bool qa_q3_wire_client_ready(qa_q3_game *game, uint32_t slot,
                             int32_t ready_mask, qa_error *error) {
    if (!game || !game->wire || game->source_restored || slot >= game->options.max_clients ||
        game->clients[slot].connected != QA_Q3_CLIENT_CONNECTED ||
        !row(game, game->source_entities[slot].actor) || ready_mask < 0 || ready_mask > 65535)
        return q3_fail(error, "Q3 readiness publication requires a CONNECTED source PS row");
    game->wire->clients[slot].clients_ready = ready_mask;
    qa_q3_player *followed = q3_client_follow_player(game, slot);
    if (followed) followed->stats[5 + (game->options.product == QA_Q3_TEAM_ARENA)] = ready_mask;
    return true;
}

bool qa_q3_wire_player_special_ammo(qa_q3_game *game, qa_actor_id actor,
                                     uint32_t ammo_slot, int32_t value, qa_error *error) {
    uint32_t slot;
    if (!game || !game->wire || game->source_restored || ammo_slot >= 16 ||
        (ammo_slot < QA_Q3_WEAPON_COUNT && game->ammo_items[ammo_slot]) ||
        !qa_q3_native_client_slot(game, actor, &slot, error) || !row(game, actor))
        return q3_fail(error, "Q3 special ammo mutation has a shared item or no source client");
    game->wire->clients[slot].special_ammo[ammo_slot] = value;
    return true;
}

bool qa_q3_wire_player_loop_sound(qa_q3_game *game, qa_actor_id actor,
                                   int32_t sound_index, qa_error *error) {
    uint32_t slot;
    if (!game || !game->wire || game->source_restored || sound_index < 0 || sound_index > 255 ||
        !qa_q3_native_client_slot(game, actor, &slot, error) || !row(game, actor))
        return q3_fail(error, "Q3 loop sound mutation has no source client or sound index");
    game->wire->clients[slot].loop_sound = sound_index;
    qa_q3_player *followed = q3_client_follow_player(game, slot);
    if (followed) followed->loopSound = sound_index;
    return true;
}

bool qa_q3_wire_bind_services(qa_q3_game *game, const qa_q3_wire_services *services,
                             qa_error *error) {
    if (!game || !game->wire || !services || !services->mode ||
        game->observation_depth || game->source_restored)
        return q3_fail(error, "Q3 wire services require idle actual movement and mode owners");
    game->wire->services = *services;
    return true;
}

static void followed_policy_update(qa_q3_game *game, uint32_t slot, uint8_t fields,
                                     const qa_q3_wire_policy *policy) {
    qa_q3_player *followed = q3_client_follow_player(game, slot);
    if (!followed) return;
    if (fields & QA_Q3_WIRE_PM_TYPE) followed->pmType = policy->pm_type;
    if (fields & QA_Q3_WIRE_PM_BOB_CYCLE) followed->bobCycle = policy->bob_cycle;
    if (fields & QA_Q3_WIRE_PM_FLAGS) followed->pmFlags = policy->pm_flags;
    if (fields & QA_Q3_WIRE_PM_TIME) followed->pmTime = policy->pm_time;
    if (fields & QA_Q3_WIRE_PM_GRAVITY) followed->gravity = policy->gravity;
    if (fields & QA_Q3_WIRE_PM_SPEED) followed->speed = policy->speed;
    if (fields & QA_Q3_WIRE_PM_DIRECTION) followed->movementDir = policy->movement_dir;
}

bool qa_q3_wire_player_policy(qa_q3_game *game, qa_actor_id actor,
                              const qa_q3_wire_policy *policy, qa_error *error) {
    uint32_t slot;
    if (!game || !game->wire || !policy || game->source_restored ||
        !qa_q3_native_client_slot(game, actor, &slot, error) || !row(game, actor))
        return q3_fail(error, "Q3 native PM policy has no admitted source client");
    if (policy->pm_type < 0 || policy->pm_type > 6 || policy->bob_cycle < 0 ||
        policy->bob_cycle > 255 || policy->movement_dir < 0 || policy->movement_dir > 7)
        return q3_fail(error, "Q3 native PM policy contains invalid source fields");
    game->wire->clients[slot].foreign_policy = *policy;
    game->wire->clients[slot].foreign_policy_written = true;
    followed_policy_update(game, slot, QA_Q3_WIRE_PM_ALL, policy);
    return true;
}

bool qa_q3_wire_player_policy_read(const qa_q3_game *game, qa_actor_id actor,
                                    qa_q3_wire_policy *out, qa_error *error) {
    uint32_t slot;
    if (!game || !game->wire || !out || game->source_restored ||
        game->observation_depth == SIZE_MAX ||
        !qa_q3_native_client_slot(game, actor, &slot, error) || !row((qa_q3_game *)game, actor))
        return q3_fail(error, "Q3 native PM policy read lacks its actual source client");
    qa_q3_game *retained = (qa_q3_game *)game;
    ++retained->observation_depth;
    qa_builtin_player_control control;
    bool selected = false;
    bool ok = movement_control(game, actor, &control, &selected, error) && current(game, slot, actor);
    const qa_q3_movement_state *movement = selected ? &control.state->data.q3 : NULL;
    if (ok && !selected && !game->wire->clients[slot].foreign_policy_written)
        ok = q3_fail(error, "Q3 native PM policy read requires its authored foreign movement state");
    if (ok) *out = selected
        ? (qa_q3_wire_policy){movement->movement_type, movement->bob_cycle,
            word(movement->movement_flags), movement->movement_time_ms,
            movement->gravity, movement->speed, movement->movement_direction}
        : game->wire->clients[slot].foreign_policy;
    if (!ok && (!error || !error->code))
        q3_fail(error, "Q3 native client retired during its PM policy read");
    --retained->observation_depth;
    return ok;
}

static void policy_update(qa_q3_wire_policy *target, uint8_t fields,
                           const qa_q3_wire_policy *source) {
    if (fields & QA_Q3_WIRE_PM_TYPE) target->pm_type = source->pm_type;
    if (fields & QA_Q3_WIRE_PM_BOB_CYCLE) target->bob_cycle = source->bob_cycle;
    if (fields & QA_Q3_WIRE_PM_FLAGS) target->pm_flags = source->pm_flags;
    if (fields & QA_Q3_WIRE_PM_TIME) target->pm_time = source->pm_time;
    if (fields & QA_Q3_WIRE_PM_GRAVITY) target->gravity = source->gravity;
    if (fields & QA_Q3_WIRE_PM_SPEED) target->speed = source->speed;
    if (fields & QA_Q3_WIRE_PM_DIRECTION) target->movement_dir = source->movement_dir;
}

bool qa_q3_wire_player_policy_update(qa_q3_game *game, qa_actor_id actor, uint8_t fields,
                                      const qa_q3_wire_policy *policy, qa_error *error) {
    uint32_t slot;
    if (!game || !game->wire || !policy || !fields || (fields & 128u) || game->source_restored ||
        game->observation_depth == SIZE_MAX ||
        !qa_q3_native_client_slot(game, actor, &slot, error) || !row(game, actor) ||
        ((fields & QA_Q3_WIRE_PM_TYPE) && (policy->pm_type < 0 || policy->pm_type > 6)) ||
        ((fields & QA_Q3_WIRE_PM_BOB_CYCLE) && (policy->bob_cycle < 0 || policy->bob_cycle > 255)) ||
        ((fields & QA_Q3_WIRE_PM_DIRECTION) && (policy->movement_dir < 0 || policy->movement_dir > 7)))
        return q3_fail(error, "Q3 source PM assignment lacks its actual client or valid field mask");
    ++game->observation_depth;
    qa_builtin_player_control control;
    bool selected = false;
    bool ok = movement_control(game, actor, &control, &selected, error) && current(game, slot, actor);
    if (ok && selected) {
        if (!game->wire->services.movement_policy)
            ok = q3_fail(error, "Q3 source PM assignment lacks its actual selected writer");
        else ok = game->wire->services.movement_policy(game->wire->services.context,
                actor, fields, policy, error) && current(game, slot, actor);
    } else if (ok) {
        q3_wire_client *client = &game->wire->clients[slot];
        if (!client->foreign_policy_written)
            ok = q3_fail(error, "Q3 source PM assignment has no authored native constructor state");
        else policy_update(&client->foreign_policy, fields, policy);
    }
    if (ok) followed_policy_update(game, slot, fields, policy);
    if (!ok && (!error || !error->code))
        q3_fail(error, "Q3 source client retired during its PM assignment");
    --game->observation_depth;
    return ok;
}

bool qa_q3_wire_player_view_command(qa_q3_game *game, qa_actor_id actor,
                                     const qa_q3_usercmd *command, qa_vec3 *out,
                                     qa_error *error) {
    uint32_t slot;
    if (!game || !game->wire || !command || !out || game->source_restored ||
        game->observation_depth == SIZE_MAX ||
        !qa_q3_native_client_slot(game, actor, &slot, error) || !row(game, actor))
        return q3_fail(error, "Q3 source view command lacks its actual native client");
    ++game->observation_depth;
    qa_builtin_player_control control;
    bool selected = false;
    qa_combat_state combat;
    bool ok = movement_control(game, actor, &control, &selected, error) && current(game, slot, actor);
    if (ok && (selected || !game->wire->clients[slot].foreign_policy_written))
        ok = q3_fail(error, "Q3 native view producer requires its authored foreign movement policy");
    if (ok) ok = qa_combat_read(game->options.services.combat, actor, &combat, error) &&
        current(game, slot, actor);
    q3_actor *entry = ok ? q3_actor_get(game, actor) : NULL;
    if (ok && (!entry || entry->kind != Q3_ACTOR_PLAYER))
        ok = q3_fail(error, "Q3 native view producer lost its source player state");
    if (ok) {
        qa_q3_player_motion motion;
        if (!q3_player_motion_read(game, actor, &motion, error)) { --game->observation_depth; return false; }
        int32_t type = game->wire->clients[slot].foreign_policy.pm_type;
        if (type != 5 && type != 6 &&
            (type == 2 || q3_source_float_to_int(combat.health) > 0)) {
            int32_t delta[3] = {motion.delta_pitch_word, motion.delta_yaw_word,
                                motion.delta_roll_word};
            int32_t angles[3];
            for (size_t i = 0; i < 3; ++i) {
                uint32_t value = ((uint32_t)command->angles[i] + (uint32_t)delta[i]) & 65535u;
                angles[i] = value >= 32768u ? (int32_t)value - 65536 : (int32_t)value;
            }
            if (angles[0] > 16000 || angles[0] < -16000) {
                angles[0] = angles[0] > 0 ? 16000 : -16000;
                motion.delta_pitch_word = word((uint32_t)angles[0] - (uint32_t)command->angles[0]);
            }
            motion.view_angles = qa_v3((float)angles[0] * (360.0f / 65536.0f),
                (float)angles[1] * (360.0f / 65536.0f),
                (float)angles[2] * (360.0f / 65536.0f));
        }
        qa_q3_player *followed = q3_client_follow_player(game, slot);
        if (followed) {
            vector(followed->viewangles, motion.view_angles);
            followed->deltaAngles[0] = motion.delta_pitch_word;
            followed->deltaAngles[1] = motion.delta_yaw_word;
            followed->deltaAngles[2] = motion.delta_roll_word;
        }
        q3_player_delta_write(game, actor, 0, motion.delta_pitch_word);
        q3_player_view_write(game, actor, motion.view_angles);
        *out = motion.view_angles;
    }
    if (!ok && (!error || !error->code))
        q3_fail(error, "Q3 native client retired during view command production");
    --game->observation_depth;
    return ok;
}

bool qa_q3_wire_client_stop_following(qa_q3_game *game, uint32_t slot, qa_error *error) {
    if (!game || !game->wire || slot >= QA_Q3_SOURCE_CLIENTS || game->source_restored ||
        game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 StopFollowing requires its actual fixed source client");
    qa_actor_id actor = game->source_entities[slot].actor;
    ++game->observation_depth;
    bool ok = true;
    if (!game->wire->clients[slot].movement_detached && actor.registry &&
        qa_actors_get(qa_session_actors(game->options.services.session), actor)) {
        qa_builtin_player_control control;
        bool selected = false;
        ok = movement_control(game, actor, &control, &selected, error);
        if (ok && selected) {
            if (!game->wire->services.movement_flags)
                ok = q3_fail(error, "Q3 StopFollowing lacks its actual PMF_FOLLOW writer");
            else ok = game->wire->services.movement_flags(game->wire->services.context,
                                                          actor, 4096u, 0, error);
        }
        if (ok && !current(game, slot, actor))
            ok = q3_fail(error, "Q3 StopFollowing source client retired during movement mutation");
    }
    if (ok) {
        game->wire->clients[slot].foreign_policy.pm_flags &= ~4096;
        qa_q3_player *followed = q3_client_follow_player(game, slot);
        if (followed) followed->pmFlags &= ~4096;
    }
    --game->observation_depth;
    return ok;
}

bool qa_q3_wire_client_detach(qa_q3_game *game, uint32_t slot, qa_error *error) {
    if (!game || !game->wire || slot >= game->options.max_clients || game->source_restored ||
        game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 PM retirement requires its current movement source capability");
    q3_wire_client *client = &game->wire->clients[slot];
    if (client->movement_detached) return true;
    qa_actor_id actor = game->source_entities[slot].actor;
    if (!current(game, slot, actor))
        return q3_fail(error, "Q3 PM retirement has no current native source client generation");
    ++game->observation_depth;
    qa_builtin_player_control control;
    bool selected = false;
    bool ok = movement_control(game, actor, &control, &selected, error);
    const qa_q3_movement_state *movement = selected ? &control.state->data.q3 : NULL;
    if (ok && !current(game, slot, actor))
        ok = q3_fail(error, "Q3 PM source client changed while retiring its movement authority");
    if (ok && selected) {
        client->foreign_policy = (qa_q3_wire_policy){movement->movement_type, movement->bob_cycle,
            word(movement->movement_flags), movement->movement_time_ms,
            movement->gravity, movement->speed, movement->movement_direction};
        client->foreign_policy_written = true;
    }
    if (ok) ok = q3_player_motion_read(game, actor, &game->wire->foreign_motions[slot], error);
    if (ok) {
        int32_t score;
        qa_q3_wire_client_body body;
        ok = qa_q3_wire_client_source_score_read(game, slot, &score, error) &&
            qa_q3_wire_client_source_body_read(game, slot, &body, error) && current(game, slot, actor);
        if (ok) {
            game->clients[slot].retired_score = score;
            game->clients[slot].source_model_shape = body.model_shape;
            client->movement_detached = true;
        }
    }
    --game->observation_depth;
    return ok;
}

static int32_t item_index(const qa_q3_game *game, qa_q3_item_kind kind, int32_t tag) {
    if (!tag) return 0;
    size_t count;
    const qa_q3_item *items = qa_q3_items(game->options.product, &count);
    for (size_t i = 1; i < count; ++i)
        if (items[i].kind == kind && items[i].tag == tag) return (int32_t)i;
    return 0;
}

static bool player_authority_read(const qa_q3_game *game, uint32_t slot, qa_actor_id actor,
                                   qa_q3_player *value, qa_error *error) {
    qa_body_state body;
    qa_combat_state combat;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error) ||
        !current(game, slot, actor) ||
        !qa_combat_read(game->options.services.combat, actor, &combat, error) ||
        !current(game, slot, actor)) return false;
    vector(value->origin, body.origin); vector(value->velocity, body.velocity);
    unsigned shift = game->options.product == QA_Q3_TEAM_ARENA ? 1u : 0u;
    value->stats[0] = q3_source_float_to_int(combat.health);
    value->stats[3 + shift] = combat.armor.regular.kind == QA_ARMOR_NONE ? 0
        : q3_source_float_to_int((float)combat.armor.regular.points);
    value->stats[2 + shift] = 0;
    for (unsigned i = 1; i < QA_Q3_WEAPON_COUNT; ++i) {
        double owned;
        if (!qa_inventory_count_read(game->options.services.inventory, actor,
                                      game->weapon_items[i], &owned, error) ||
            !current(game, slot, actor)) return false;
        if (owned > 0) value->stats[2 + shift] |= 1 << i;
    }
    for (unsigned i = 0; i < 16; ++i) {
        if (i < QA_Q3_WEAPON_COUNT && game->ammo_items[i]) {
            double ammo;
            if (!qa_inventory_count_read(game->options.services.inventory, actor,
                                          game->ammo_items[i], &ammo, error) ||
                !current(game, slot, actor)) return false;
            if (ammo < INT32_MIN || ammo > INT32_MAX)
                return q3_fail(error, "Q3 source ammunition exceeds its integer domain");
            value->ammo[i] = (int32_t)ammo;
        } else value->ammo[i] = game->wire->clients[slot].special_ammo[i];
    }
    return true;
}

static bool player_read(const qa_q3_game *game, uint32_t slot, qa_q3_player *out,
                         qa_error *error) {
    qa_q3_player followed;
    bool has_followed;
    if (!qa_q3_client_follow_read(game, slot, &followed, &has_followed, error)) return false;
    qa_actor_id actor = game->source_entities[slot].actor;
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!current(game, slot, actor) || !entry || entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "Q3 PS observation lacks its full source client generation");
    if (has_followed) {
        /* Native copyFrom preserves this record's body and inventory binding.
         * Only source fields came from the followed player at END. */
        if (!player_authority_read(game, slot, actor, &followed, error)) return false;
        *out = followed;
        return true;
    }
    if (!game->wire->services.mode)
        return q3_fail(error, "Q3 PS observation lacks its actual movement or match binding");
    qa_q3_player_state source = entry->state.player;
    qa_q3_player_motion motion;
    if (!q3_player_motion_read(game, actor, &motion, error)) return false;
    qa_q3_wire_mode mode;
    qa_builtin_player_control control;
    bool selected = false;
    if (!movement_control(game, actor, &control, &selected, error) || !current(game, slot, actor) ||
        !game->wire->services.mode(game->wire->services.context, actor, &mode, error) ||
        !current(game, slot, actor)) return false;
    if (!selected && !game->wire->clients[slot].foreign_policy_written)
        return q3_fail(error, "Q3 source ClientThink has not produced its native PM policy");
    const qa_q3_movement_state *movement = selected ? &control.state->data.q3 : NULL;
    qa_q3_wire_policy policy = selected
        ? (qa_q3_wire_policy){movement->movement_type, movement->bob_cycle,
              word(movement->movement_flags), movement->movement_time_ms,
              movement->gravity, movement->speed, movement->movement_direction}
        : game->wire->clients[slot].foreign_policy;
    qa_q3_player value = {.product = game->options.product,
        .commandTime = motion.command_time_ms, .pmType = policy.pm_type,
        .bobCycle = policy.bob_cycle, .pmFlags = policy.pm_flags, .pmTime = policy.pm_time,
        .weaponTime = source.weapon_time_ms, .gravity = policy.gravity, .speed = policy.speed,
        .groundEntityNum = motion.ground_entity_number,
        .legsTimer = source.legs_timer_ms, .legsAnim = source.legs_animation,
        .torsoTimer = source.torso_timer_ms, .torsoAnim = source.torso_animation,
        .movementDir = policy.movement_dir, .eFlags = word(source.flags),
        .eventSequence = word(source.event_sequence), .externalEvent = source.external_event,
        .externalEventParm = source.external_event_parameter,
        .externalEventTime = source.external_event_time, .clientNum = source.client_number,
        .weapon = (int32_t)source.weapon, .weaponState = (int32_t)source.weapon_phase,
        .viewheight = q3_source_float_to_int(motion.view_height),
        .damageEvent = source.damage_event, .damageYaw = source.damage_yaw,
        .damagePitch = source.damage_pitch, .damageCount = source.damage_count,
        .generic1 = source.generic1, .jumppadEnt = motion.jumppad_entity,
        .ping = game->clients[slot].ping, .pmoveFramecount = motion.pmove_frame_count,
        .jumppadFrame = motion.jumppad_frame,
        .entityEventSequence = word(source.entity_event_sequence)};
    vector(value.viewangles, motion.view_angles); vector(value.grapplePoint, source.grapple_point);
    value.deltaAngles[0] = motion.delta_pitch_word;
    value.deltaAngles[1] = motion.delta_yaw_word;
    value.deltaAngles[2] = motion.delta_roll_word;
    memcpy(value.events, source.events, sizeof(value.events));
    memcpy(value.eventParms, source.event_parameters, sizeof(value.eventParms));
    unsigned shift = game->options.product == QA_Q3_TEAM_ARENA ? 1u : 0u;
    value.stats[1] = item_index(game, QA_Q3_ITEM_HOLDABLE, (int32_t)source.holdable);
    if (shift) value.stats[2] = item_index(game, QA_Q3_ITEM_PERSISTENT, (int32_t)source.persistent);
    value.stats[4 + shift] = source.dead_yaw;
    value.stats[5 + shift] = game->wire->clients[slot].clients_ready;
    value.stats[6 + shift] = source.max_health;
    memcpy(value.powerups, source.powerups, sizeof(source.powerups));
    const q3_wire_client *client = &game->wire->clients[slot];
    const int32_t persistant[16] = {mode.score, client->hits, source.rank, source.persistent_team,
        word(source.spawn_count), source.player_events, client->attacker, client->attackee_armor,
        source.deaths, source.impressive_count, source.excellent_count, source.defend_count,
        source.assist_count, source.gauntlet_frag_count, source.captures, 0};
    memcpy(value.persistant, persistant, sizeof(value.persistant));
    value.loopSound = client->loop_sound;
    if (!player_authority_read(game, slot, actor, &value, error)) return false;
    if (!current(game, slot, actor))
        return q3_fail(error, "Q3 source client changed during PS observation");
    *out = value;
    return true;
}

bool qa_q3_wire_player_read(const qa_q3_game *game, uint32_t slot,
                            qa_q3_player *out, qa_error *error) {
    if (!game || !game->wire || !out || slot >= game->options.max_clients ||
        game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 physical PS observation boundary");
    qa_q3_game *retained = (qa_q3_game *)game;
    ++retained->observation_depth;
    bool ok = player_read(game, slot, out, error);
    --retained->observation_depth;
    return ok;
}

static float snap_component(float value) {
    return (float)q3_source_float_to_int(value);
}

static void convert_player(qa_q3_player player, qa_q3_player_state *native,
                            q3_wire_entity_source *s, qa_q3_trajectory *position,
                            qa_q3_trajectory *angles, bool snap, bool extrapolate, int32_t time_ms) {
    s->type = player.pmType == 5 || player.pmType == 2 || player.stats[0] <= -40 ? 10 : 1;
    position->type = extrapolate ? 3 : 1;
    if (extrapolate) {
        position->time = time_ms;
        position->duration = 50;
    }
    angles->type = 1;
    for (size_t i = 0; i < 3; ++i) {
        position->base[i] = snap ? snap_component(player.origin[i]) : player.origin[i];
        position->delta[i] = player.velocity[i];
        angles->base[i] = snap ? snap_component(player.viewangles[i]) : player.viewangles[i];
    }
    s->angles2.y = (float)player.movementDir;
    s->legs = player.legsAnim; s->torso = player.torsoAnim; s->client = player.clientNum;
    s->flags = player.stats[0] <= 0 ? player.eFlags | 1 : player.eFlags & ~1;
    if (player.externalEvent) {
        s->event = player.externalEvent;
        s->event_parameter = player.externalEventParm;
    } else if (player.entityEventSequence < player.eventSequence) {
        int32_t oldest = q3_sub_time(player.eventSequence, 2);
        if (player.entityEventSequence < oldest) player.entityEventSequence = oldest;
        uint32_t sequence = (uint32_t)player.entityEventSequence;
        s->event = player.events[sequence & 1u] | (int32_t)((sequence & 3u) << 8);
        s->event_parameter = player.eventParms[sequence & 1u];
        native->entity_event_sequence = sequence + 1u;
    }
    s->weapon = player.weapon; s->ground_entity = player.groundEntityNum;
    s->powerups = 0;
    for (unsigned i = 0; i < 16; ++i)
        if (player.powerups[i]) s->powerups |= 1 << i;
    s->loop_sound = player.loopSound; s->generic1 = player.generic1;
}

bool qa_q3_wire_player_publish(qa_q3_game *game, qa_actor_id actor, bool snap,
                               bool extrapolate, int32_t time_ms, qa_error *error) {
    uint32_t slot;
    qa_q3_player player;
    if (!game || !game->wire || game->source_restored ||
        !qa_q3_native_client_slot(game, actor, &slot, error) ||
        !qa_q3_wire_player_read(game, slot, &player, error)) return false;
    q3_wire_row *record = row(game, actor);
    q3_actor *entry = q3_actor_get(game, actor);
    if (!record || !entry || entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "Q3 BG conversion lost its actual client generation");
    convert_player(player, &entry->state.player, &record->source, &record->player_position,
                    &record->player_angles, snap, extrapolate, time_ms);
    qa_q3_player *followed = q3_client_follow_player(game, slot);
    if (followed) followed->entityEventSequence = word(entry->state.player.entity_event_sequence);
    game->source_entities[slot].number = player.clientNum;
    record->player_published = true;
    record->initialized = true;
    return true;
}

bool qa_q3_wire_player_publication_read(const qa_q3_game *game, qa_actor_id actor,
                                         qa_q3_wire_player_publication *out, qa_error *error) {
    uint32_t slot;
    if (!game || !game->wire || game->source_restored || !out)
        return q3_fail(error, "Q3 player publication lacks its actual source owner or output");
    if (!qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    const q3_wire_row *record = &game->wire->rows[slot];
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!current(game, slot, actor) || !record->initialized || !record->player_published ||
        !entry || entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "Q3 player publication lost its actual BG conversion or client generation");
    *out = (qa_q3_wire_player_publication){
        .position = qa_v3(record->player_position.base[0], record->player_position.base[1],
                         record->player_position.base[2]),
        .weapon = record->source.weapon, .client_number = record->source.client};
    return true;
}

static void trajectory(qa_q3_trajectory *out, const qa_trajectory *source) {
    *out = (qa_q3_trajectory){.type = (int32_t)source->type, .time = source->time_ms,
                             .duration = source->duration_ms};
    vector(out->base, source->base); vector(out->delta, source->delta);
}

static bool link_membership(const qa_q3_game *game, qa_actor_id actor, const qa_bounds *bounds,
                             qa_q3_wire_visibility *out, bool *has_leaves, qa_error *error) {
    qa_collision_geometry *geometry = qa_world_geometry(game->options.services.world);
    if (!geometry) return q3_fail(error, "Q3 linked entity lacks actual collision geometry");
    qa_q3_visibility_entity visibility={0};
    if(!qa_q3_leaf_visibility(game->options.services.world,actor,bounds,
        qa_world_trace_scratch(game->options.services.world,geometry),&visibility,out->clusters,has_leaves,error)) return false;
    out->area=visibility.area;out->area2=visibility.area2;
    out->last_cluster=visibility.last_cluster;out->cluster_count=visibility.cluster_count;
    return true;
}

static uint32_t solid_byte(float value) {
    int32_t integer = q3_source_float_to_int(value);
    return integer < 1 ? 1u : integer > 255 ? 255u : (uint32_t)integer;
}

static void link_write(qa_q3_game *game, uint32_t slot, const qa_body_state *body,
                        const qa_actor_collision *collision,
                        const qa_q3_wire_visibility *membership, uint64_t link_count) {
    q3_wire_row *record = &game->wire->rows[slot];
    uint32_t solid = collision->inline_model ? UINT32_C(0xffffff)
        : !qa_collision_bits_overlap(collision->contents, qa_collision_contents_mask(UINT32_C(0x02000001), QA_COLLISION_Q3)) ? 0u
        : (solid_byte((body->bounds.maxs.z + 32)) << 16) |
          (solid_byte(-body->bounds.mins.z) << 8) | solid_byte(body->bounds.maxs.x);
    qa_q3_entity *temporary = q3_wire_temporary(game, record->actor);
    if (temporary) temporary->solid = word(solid);
    else {
        q3_actor *entry = q3_actor_get(game, record->actor);
        if (entry && (entry->kind == Q3_ACTOR_PODIUM || entry->kind == Q3_ACTOR_VICTORY_MODEL))
            entry->state.postgame.entity.solid = word(solid);
        else record->source.solid = word(solid);
    }
    if (slot < QA_Q3_SOURCE_CLIENTS)
        game->clients[slot].source_model_shape = collision->shape == QA_SHAPE_CAPSULE
            ? QA_SHAPE_CAPSULE : QA_SHAPE_BOX;
    record->link = (q3_wire_link_state){.written = true,
        .area = membership->area, .area2 = membership->area2,
        .last_cluster = membership->last_cluster,
        .cluster_count = (uint32_t)membership->cluster_count, .link_count = link_count};
    memcpy(record->link.clusters, membership->clusters, sizeof(record->link.clusters));
}

static bool linked_geometry_matches(const qa_body_state *published,
                                      const qa_body_state *source, qa_vec3 origin) {
    for (unsigned axis = 0; axis < 3; ++axis)
        if (q3_source_vec_component(published->origin, axis) != q3_source_vec_component(origin, axis) ||
            q3_source_vec_component(published->angles, axis) != q3_source_vec_component(source->angles, axis) ||
            q3_source_vec_component(published->bounds.mins, axis) != q3_source_vec_component(source->bounds.mins, axis) ||
            q3_source_vec_component(published->bounds.maxs, axis) != q3_source_vec_component(source->bounds.maxs, axis))
            return false;
    return true;
}

bool qa_q3_wire_link(qa_q3_game *game, qa_actor_id actor,
                      const qa_vec3 *origin_override, qa_error *error) {
    uint32_t slot;
    if (!game || !game->wire || game->source_restored || game->observation_depth == SIZE_MAX ||
        (origin_override && !qa_vec_finite(*origin_override)) ||
        !qa_q3_source_actor_slot(game, actor, &slot, error) || !current(game, slot, actor))
        return q3_fail(error, "Q3 source link lacks its current physical actor or origin");
    ++game->observation_depth;
    qa_world *world = game->options.services.world;
    qa_body_state body;
    bool ok = qa_world_unlink(world, actor, error) && current(game, slot, actor) &&
        q3_source_body_read(game, actor, &body, error) && current(game, slot, actor);
    uint64_t body_storage = ok ? qa_world_body_storage_serial(world, actor) : 0;
    qa_actor_collision collision = {0};
    if (ok) {
        qa_error observed = {0};
        bool colliding = qa_world_get_collision(world, actor, &collision, &observed);
        if (!colliding && observed.code != QA_OK) { if (error) *error = observed; ok = false; }
        if (!body_storage || body_storage != qa_world_body_storage_serial(world, actor) ||
            !current(game, slot, actor)) ok = false;
    }
    if (ok) {
        q3_wire_row *record = &game->wire->rows[slot];
        qa_bounds local = body.bounds;
        if (collision.inline_model && (body.angles.x != 0 || body.angles.y != 0 || body.angles.z != 0)) {
            qa_vec3 extent = qa_v3(fmaxf(fabsf(local.mins.x), fabsf(local.maxs.x)),
                fmaxf(fabsf(local.mins.y), fabsf(local.maxs.y)),
                fmaxf(fabsf(local.mins.z), fabsf(local.maxs.z)));
            float radius = sqrtf(qa_vec_dot(extent, extent));
            local = (qa_bounds){qa_v3(-radius, -radius, -radius), qa_v3(radius, radius, radius)};
        }
        qa_vec3 origin = origin_override ? *origin_override : body.origin;
        qa_bounds bounds = {qa_vec_sub(qa_vec_add(origin, local.mins), qa_v3(1, 1, 1)),
                            qa_vec_add(qa_vec_add(origin, local.maxs), qa_v3(1, 1, 1))};
        qa_q3_wire_visibility membership = {0};
        bool has_leaves = false;
        qa_body_link_state previous;
        ok = link_membership(game, actor, &bounds, &membership, &has_leaves, error) &&
            qa_world_link_state(world, actor, &previous) && current(game, slot, actor);
        if (ok) {
            if (has_leaves && previous.link_count == UINT64_MAX)
                ok = q3_fail(error, "Q3 source link count exhausted");
            else {
                link_write(game, slot, &body, &collision, &membership,
                    previous.link_count + (has_leaves ? 1u : 0u));
                if (has_leaves) ok = qa_world_link_bounds_at(world, actor, &bounds, &origin, error);
                qa_body_link_state published;
                if (ok && (!current(game, slot, actor) || !qa_world_link_state(world, actor, &published) ||
                    body_storage != qa_world_body_storage_serial(world, actor) ||
                    published.link_count != record->link.link_count || published.linked != has_leaves ||
                    (has_leaves && !linked_geometry_matches(&published.state, &body, origin))))
                    ok = q3_fail(error, "Q3 source link changed during world publication");
            }
        }
    }
    if (!ok && (!error || !error->code)) q3_fail(error, "Q3 source body retired during link production");
    --game->observation_depth;
    return ok;
}

bool qa_q3_wire_linked(qa_q3_game *game, const qa_linked_body *linked, qa_error *error) {
    if (!game || !game->wire || !linked || game->source_restored ||
        game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 linked observation lacks its actual source owner");
    q3_wire_row *record = row(game, linked->actor);
    if (!record) return true;
    uint32_t slot = (uint32_t)(record - game->wire->rows);
    if (record->link.written && record->link.link_count == linked->link_count) return true;
    ++game->observation_depth;
    qa_world *world = game->options.services.world;
    uint64_t body_storage = qa_world_body_storage_serial(world, linked->actor);
    qa_actor_collision collision = {0};
    qa_error observed = {0};
    bool colliding = qa_world_get_collision(world, linked->actor, &collision, &observed);
    bool ok = colliding || observed.code == QA_OK;
    if (!ok && error) *error = observed;
    qa_q3_wire_visibility membership = {0};
    bool has_leaves = false;
    qa_body_link_state published;
    if (ok) ok = body_storage && current(game, slot, linked->actor) &&
        body_storage == qa_world_body_storage_serial(world, linked->actor) &&
        link_membership(game, linked->actor, &linked->absolute_bounds, &membership, &has_leaves, error) &&
        current(game, slot, linked->actor) &&
        body_storage == qa_world_body_storage_serial(world, linked->actor) &&
        qa_world_link_state(world, linked->actor, &published) && published.linked &&
        published.link_count == linked->link_count;
    if (ok) link_write(game, slot, &linked->state, &collision, &membership, linked->link_count);
    if (!ok && (!error || !error->code))
        q3_fail(error, "Q3 source body retired during linked observation");
    --game->observation_depth;
    return ok;
}

static bool visibility(const qa_q3_game *game, qa_actor_id actor,
                        qa_q3_wire_visibility *out, qa_error *error) {
    q3_wire_row *record = row((qa_q3_game *)game, actor);
    qa_linked_body linked;
    uint32_t slot;
    if (!qa_q3_source_actor_slot(game, actor, &slot, error)) return false;
    out->linked = game->source_entities[slot].body_attached &&
        qa_world_linked(game->options.services.world, actor, &linked);
    if (!record || (out->linked && (!record->link.written || record->link.link_count != linked.link_count)))
        return q3_fail(error, "Q3 current world link bypassed its genuine source visibility producer");
    if (!record->link.written) return true;
    out->area = record->link.area; out->area2 = record->link.area2;
    out->last_cluster = record->link.last_cluster; out->cluster_count = record->link.cluster_count;
    memcpy(out->clusters, record->link.clusters, sizeof(out->clusters));
    return true;
}

bool qa_q3_wire_native_visibility_read(const qa_q3_game *game, uint32_t slot,
                                        qa_q3_wire_native_visibility *out, qa_error *error) {
    if (!game || !game->wire || !out || slot >= QA_Q3_SOURCE_NONE ||
        game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid native Q3 visibility observation boundary");
    const qa_q3_source_binding *binding = &game->source_entities[slot];
    const q3_wire_row *record = &game->wire->rows[slot];
    qa_actor_id actor = binding->actor;
    if (!qa_actor_id_equal(record->actor, actor) ||
        (actor.registry && !current(game, slot, actor)) ||
        (!actor.registry && binding->in_use) || (binding->in_use && !record->initialized))
        return q3_fail(error, "native Q3 visibility lacks its completed physical source row");
    qa_q3_wire_native_visibility value = {.present = binding->in_use,
        .server_flags = binding->server_flags, .single_client = record->source.single_client,
        .area = -1, .area2 = -1};
    if (!actor.registry || !binding->body_attached) { *out = value; return true; }
    qa_q3_game *retained = (qa_q3_game *)game;
    ++retained->observation_depth;
    qa_world *world = game->options.services.world;
    qa_collision_geometry *geometry = qa_world_geometry(world);
    qa_body_link_state state;
    uint64_t storage = qa_world_body_storage_serial(world, actor);
    bool ok = storage && qa_world_link_state(world, actor, &state);
    if (!ok) q3_fail(error, "native Q3 visibility has no actual body link owner");
    if (ok && state.linked) {
        qa_linked_body linked;
        if (!geometry || !qa_world_linked(world, actor, &linked) ||
            linked.link_count != state.link_count)
            ok = q3_fail(error, "native Q3 visibility lost its current published body");
        const qa_world_leaf_visibility_result *r;
        if(ok) ok=qa_world_leaf_visibility(world,actor,NULL,QA_WORLD_LEAVES_BOX,
            qa_world_trace_scratch(world,geometry),&r,error);
        if(ok && r->prefix_invalid) ok=q3_fail(error,"native Q3 visibility exceeds its source index domain");
        if(ok) {
            value.linked=true;
            if(r->unique_areas.count) value.area=r->unique_areas.area;
            if(r->unique_areas.count>1) value.area2=r->unique_areas.area2;
            value.cluster_count=r->distinct_count;
            memcpy(value.clusters,r->distinct,value.cluster_count*sizeof(*value.clusters));
        }
    }
    qa_body_link_state final;
    if (ok && (!current(game, slot, actor) || storage != qa_world_body_storage_serial(world, actor) ||
        geometry != qa_world_geometry(world) || !qa_world_link_state(world, actor, &final) ||
        final.linked != state.linked || final.link_count != state.link_count))
        ok = q3_fail(error, "native Q3 published body changed during visibility observation");
    if (ok) *out = value;
    --retained->observation_depth;
    return ok;
}

static bool entity_read(const qa_q3_game *game, uint32_t slot, qa_q3_entity *out,
                         qa_q3_wire_visibility *visible, qa_error *error) {
    const q3_wire_row *record = &game->wire->rows[slot];
    qa_actor_id actor = game->source_entities[slot].actor;
    if (!qa_actor_id_equal(record->actor, actor) ||
        (actor.registry && !current(game, slot, actor)) ||
        (!actor.registry && game->source_entities[slot].in_use))
        return q3_fail(error, "Q3 source wire entity generation is stale");
    if (actor.registry && !record->initialized)
        return q3_fail(error, "Q3 source entity constructor has not completed its native state producers");
    const q3_wire_entity_source *s = &record->source;
    qa_q3_entity value = {.number = game->source_entities[slot].number,
        .eType = s->type, .eFlags = s->flags,
        .time = s->time, .time2 = s->time2, .otherEntityNum = s->other_entity,
        .otherEntityNum2 = s->other_entity2, .groundEntityNum = s->ground_entity,
        .constantLight = s->constant_light, .loopSound = s->loop_sound,
        .modelindex = s->model, .modelindex2 = s->model2, .clientNum = s->client,
        .frame = s->frame, .solid = s->solid, .event = s->event,
        .eventParm = s->event_parameter, .powerups = s->powerups, .weapon = s->weapon,
        .legsAnim = s->legs, .torsoAnim = s->torso, .generic1 = s->generic1};
    vector(value.origin, s->authored_origin); vector(value.origin2, s->origin2);
    vector(value.angles, s->authored_angles); vector(value.angles2, s->angles2);
    trajectory(&value.pos, &s->position); trajectory(&value.apos, &s->angular);
    const q3_actor *entry = actor.registry ? q3_actor_const(game, actor)
        : slot < QA_Q3_SOURCE_CLIENTS ? &game->client_actors[slot] : NULL;
    if (entry) switch (entry->kind) {
    case Q3_ACTOR_PLAYER:
        if (record->player_published) {
            value.pos = record->player_position; value.apos = record->player_angles;
        }
        break;
    case Q3_ACTOR_MISSILE:
        trajectory(&value.pos, &entry->state.missile.trajectory);
        value.eFlags = word(entry->state.missile.flags);
        value.weapon = (int32_t)entry->state.missile.weapon;
        value.generic1 = word(entry->state.missile.team);
        break;
    case Q3_ACTOR_ITEM:
        trajectory(&value.pos, &entry->state.item.trajectory);
        value.groundEntityNum = entry->state.item.ground_entity_number;
        break;
    case Q3_ACTOR_MOVER:
        trajectory(&value.pos, &entry->state.mover.state.position);
        trajectory(&value.apos, &entry->state.mover.state.angular); break;
    case Q3_ACTOR_CORPSE:
        trajectory(&value.pos, &entry->state.corpse.trajectory);
        value.eFlags = word(entry->state.corpse.flags);
        break;
    case Q3_ACTOR_TEMPORARY: value = entry->state.temporary.entity; break;
    case Q3_ACTOR_PODIUM: case Q3_ACTOR_VICTORY_MODEL:
        value = entry->state.postgame.entity; break;
    case Q3_ACTOR_NONE: case Q3_ACTOR_PROX_TRIGGER: case Q3_ACTOR_KAMIKAZE:
    case Q3_ACTOR_KAMIKAZE_TIMER: case Q3_ACTOR_PORTAL: case Q3_ACTOR_OBELISK: break;
    }
    qa_q3_wire_visibility membership = {.present = game->source_entities[slot].in_use,
                                        .server_flags = game->source_entities[slot].server_flags,
                                        .single_client = s->single_client};
    if (actor.registry) {
        if (!visibility(game, actor, &membership, error) || !current(game, slot, actor)) return false;
    } else if (record->link.written) {
        membership.area = record->link.area; membership.area2 = record->link.area2;
        membership.last_cluster = record->link.last_cluster;
        membership.cluster_count = record->link.cluster_count;
        memcpy(membership.clusters, record->link.clusters, sizeof(membership.clusters));
    }
    *out = value; *visible = membership;
    return true;
}

bool qa_q3_wire_entity_read(const qa_q3_game *game, uint32_t slot, qa_q3_entity *out,
                            qa_q3_wire_visibility *visible, qa_error *error) {
    if (!game || !game->wire || !out || !visible || slot >= QA_Q3_SOURCE_NONE ||
        game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 physical entity observation boundary");
    qa_q3_game *retained = (qa_q3_game *)game;
    ++retained->observation_depth;
    bool ok = entity_read(game, slot, out, visible, error);
    --retained->observation_depth;
    return ok;
}

bool qa_q3_source_model_read(const qa_q3_game *game, uint32_t slot,
                              qa_q3_source_model *out, qa_error *error) {
    if (!game || !game->wire || !out || slot >= QA_Q3_SOURCE_NONE || game->source_restored ||
        !game->source_entities[slot].in_use || !game->wire->rows[slot].initialized)
        return q3_fail(error, "Q3 model observation requires a completed physical Source row");
    qa_actor_id actor = game->source_entities[slot].actor;
    if (!current(game, slot, actor))
        return q3_fail(error, "Q3 model observation has a stale Source actor generation");
    const q3_wire_entity_source *source = &game->wire->rows[slot].source;
    qa_q3_source_model value = {.type = source->type, .model = source->model};
    const q3_actor *entry = q3_actor_const(game, actor);
    if (entry && entry->kind == Q3_ACTOR_TEMPORARY) {
        value.type = entry->state.temporary.entity.eType;
        value.model = entry->state.temporary.entity.modelindex;
    } else if (entry && (entry->kind == Q3_ACTOR_PODIUM || entry->kind == Q3_ACTOR_VICTORY_MODEL)) {
        value.type = entry->state.postgame.entity.eType;
        value.model = entry->state.postgame.entity.modelindex;
    }
    *out = value;
    return true;
}

bool qa_q3_source_activator_frame_read(const qa_q3_game *game, uint32_t slot,
                                        int32_t *frame, bool *present, qa_error *error) {
    if (!game || !game->wire || !frame || !present || slot >= QA_Q3_SOURCE_NONE ||
        game->source_restored || !game->source_entities[slot].in_use ||
        !game->wire->rows[slot].initialized)
        return q3_fail(error, "Q3 activator observation requires an initialized physical Source row");
    qa_actor_id actor = game->source_entities[slot].actor;
    if (!current(game, slot, actor))
        return q3_fail(error, "Q3 activator observation has a stale Source actor generation");
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_OBELISK || !entry->state.obelisk.model.registry) {
        *present = false;
        return true;
    }
    qa_actor_id model = entry->state.obelisk.model;
    uint32_t model_slot;
    if (!qa_q3_source_actor_slot(game, model, &model_slot, error)) return false;
    if (model_slot >= QA_Q3_SOURCE_NONE || !game->source_entities[model_slot].in_use ||
        !game->wire->rows[model_slot].initialized || !current(game, model_slot, model) ||
        !current(game, slot, actor) || !qa_actor_id_equal(entry->state.obelisk.model, model))
        return q3_fail(error, "Q3 obelisk activator lost its actual current Source model");
    *frame = game->wire->rows[model_slot].source.frame;
    *present = true;
    return true;
}

bool qa_q3_source_contents_read(const qa_q3_game *game, uint32_t slot,
                                 int32_t *out, qa_error *error) {
    if (!game || !game->wire || !out || slot >= QA_Q3_SOURCE_NONE || game->source_restored ||
        !game->source_entities[slot].in_use || !game->source_entities[slot].body_attached ||
        !game->wire->rows[slot].initialized || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 contents observation requires a completed physical Source body");
    qa_actor_id actor = game->source_entities[slot].actor;
    if (!current(game, slot, actor))
        return q3_fail(error, "Q3 contents observation has a stale Source actor generation");
    qa_world *world = game->options.services.world;
    uint64_t storage = qa_world_body_storage_serial(world, actor);
    if (!storage) return q3_fail(error, "Q3 contents observation lost its actual Source body");
    qa_q3_game *retained = (qa_q3_game *)game;
    const q3_wire_state *wire = game->wire;
    ++retained->observation_depth;
    qa_actor_collision collision;
    qa_error observed = {0};
    bool present = qa_world_get_collision(world, actor, &collision, &observed);
    bool okay = present || observed.code == QA_OK;
    if (!okay && error) *error = observed;
    if (okay && (game->source_restored || game->wire != wire ||
        game->options.services.world != world || !current(game, slot, actor) ||
        !game->source_entities[slot].in_use || !game->source_entities[slot].body_attached ||
        !game->wire->rows[slot].initialized || storage != qa_world_body_storage_serial(world, actor)))
        okay = q3_fail(error, "Q3 contents owner changed during its actual collision observation");
    if (okay) *out = present ? qa_collision_contents_export(collision.contents, QA_COLLISION_Q3, 0) : 0;
    --retained->observation_depth;
    return okay;
}

bool qa_q3_source_model_bounds_read(const qa_q3_game *game, uint32_t slot,
                                     qa_vec3 *mins, qa_vec3 *maxs, qa_error *error) {
    if (!game || !game->wire || (!mins && !maxs) || slot >= QA_Q3_SOURCE_NONE ||
        game->source_restored || !game->source_entities[slot].in_use ||
        !game->source_entities[slot].body_attached || !game->wire->rows[slot].initialized)
        return q3_fail(error, "Q3 model bounds require a completed physical Source row and output");
    qa_actor_id actor = game->source_entities[slot].actor;
    if (!current(game, slot, actor))
        return q3_fail(error, "Q3 model bounds have a stale Source actor generation");
    qa_body_state body;
    if (!q3_source_body_read((qa_q3_game *)game, actor, &body, error)) return false;
    if (!current(game, slot, actor) || !game->source_entities[slot].in_use ||
        !game->source_entities[slot].body_attached || !game->wire->rows[slot].initialized)
        return q3_fail(error, "Q3 model bounds lost their physical Source row during observation");
    if (mins) *mins = qa_vec_add(body.origin, body.bounds.mins);
    if (maxs) *maxs = qa_vec_add(body.origin, body.bounds.maxs);
    return true;
}

bool qa_q3_wire_entity_motion_write(qa_q3_game *game, qa_actor_id actor,
                                     const qa_trajectory *position, const qa_trajectory *angular,
                                     int32_t ground, qa_error *error) {
    uint32_t slot;
    if (!game || game->source_restored || !position || !angular ||
        position->type < 0 || position->type > QA_TRAJECTORY_GRAVITY ||
        angular->type < 0 || angular->type > QA_TRAJECTORY_GRAVITY ||
        !qa_vec_finite(position->base) || !qa_vec_finite(position->delta) ||
        !qa_vec_finite(angular->base) || !qa_vec_finite(angular->delta) ||
        ground < -1 || ground > (int32_t)QA_Q3_SOURCE_NONE ||
        !qa_q3_source_actor_slot(game, actor, &slot, error))
        return q3_fail(error, "Q3 source motion assignment needs its genuine entity state");
    q3_wire_row *record = row(game, actor);
    q3_actor *entry = q3_actor_get(game, actor);
    if (!record) return q3_fail(error, "Q3 source motion assignment lost its source owner");
    if (entry && (entry->kind == Q3_ACTOR_PODIUM || entry->kind == Q3_ACTOR_VICTORY_MODEL)) {
        trajectory(&entry->state.postgame.entity.pos, position);
        trajectory(&entry->state.postgame.entity.apos, angular);
        entry->state.postgame.entity.groundEntityNum = ground;
    } else if (entry && entry->kind == Q3_ACTOR_TEMPORARY) {
        trajectory(&entry->state.temporary.entity.pos, position);
        trajectory(&entry->state.temporary.entity.apos, angular);
        entry->state.temporary.entity.groundEntityNum = ground;
    } else if (entry && entry->kind == Q3_ACTOR_MOVER) {
        entry->state.mover.state.position = *position;
        entry->state.mover.state.angular = *angular;
        record->source.ground_entity = ground;
    } else {
        record->source.position = *position; record->source.angular = *angular;
        record->source.ground_entity = ground;
        if (entry && entry->kind == Q3_ACTOR_PLAYER && record->player_published) {
            trajectory(&record->player_position, position); trajectory(&record->player_angles, angular);
        } else if (entry && entry->kind == Q3_ACTOR_ITEM) {
            entry->state.item.trajectory = *position; entry->state.item.ground_entity_number = ground;
        } else if (entry && entry->kind == Q3_ACTOR_CORPSE) entry->state.corpse.trajectory = *position;
        else if (entry && entry->kind == Q3_ACTOR_MISSILE) entry->state.missile.trajectory = *position;
    }
    return true;
}

bool qa_q3_wire_body_read(const qa_q3_game *game, uint32_t slot,
                          qa_q3_wire_body *out, qa_error *error) {
    if (!game || !game->wire || !out || slot >= QA_Q3_SOURCE_NONE ||
        game->source_restored || game->observation_depth == SIZE_MAX ||
        !game->source_entities[slot].in_use || !game->wire->rows[slot].initialized)
        return q3_fail(error, "Q3 source body observation requires a current physical row");
    qa_actor_id actor = game->source_entities[slot].actor;
    if (!current(game, slot, actor))
        return q3_fail(error, "Q3 source body observation has a stale actor generation");
    qa_q3_game *retained = (qa_q3_game *)game;
    ++retained->observation_depth;
    qa_q3_wire_body value = {.actor = actor};
    const q3_actor *entry = q3_actor_const(game, actor);
    value.proximity_trigger = entry && entry->kind == Q3_ACTOR_PROX_TRIGGER;
    bool ok = q3_source_body_read(retained, actor, &value.current, error) &&
        current(game, slot, actor) && qa_world_link_state(game->options.services.world,
            actor, &value.last_link) && current(game, slot, actor);
    if (ok) {
        qa_error collision_error = {0};
        uint64_t storage = qa_world_body_storage_serial(game->options.services.world, actor);
        value.colliding = qa_world_get_collision(game->options.services.world, actor,
                                                 &value.collision, &collision_error);
        if (!value.colliding && collision_error.code != QA_OK) {
            if (error) *error = collision_error;
            ok = false;
        }
        if (!storage || storage != qa_world_body_storage_serial(game->options.services.world, actor) ||
            !current(game, slot, actor)) ok = false;
    }
    if (!ok && (!error || !error->code))
        q3_fail(error, "Q3 source body changed during its retained observation");
    if (ok) *out = value;
    --retained->observation_depth;
    return ok;
}

bool qa_q3_wire_client_read(const qa_q3_game *game, uint32_t slot,
                            qa_q3_wire_client_view *out, qa_error *error) {
    if (!game || !game->wire || !out || slot >= QA_Q3_SOURCE_CLIENTS ||
        game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 fixed client read exceeds its actual source records");
    const qa_q3_source_binding *binding = &game->source_entities[slot];
    qa_q3_wire_client_view value = {.present = binding->in_use, .bot = (binding->server_flags & 8u) != 0};
    qa_actor_id actor = binding->actor;
    if (!actor.registry) {
        if (binding->in_use) return q3_fail(error, "Active Q3 source client has no body actor");
        *out = value;
        return true;
    }
    if (!current(game, slot, actor))
        return q3_fail(error, "Q3 fixed client body has a stale source generation");
    qa_q3_game *retained = (qa_q3_game *)game;
    ++retained->observation_depth;
    qa_body_state body;
    bool ok = q3_source_body_read(retained, actor, &body, error);
    if (ok && !current(game, slot, actor))
        ok = q3_fail(error, "Q3 fixed client body retired during its source read");
    if (ok) { value.origin = body.origin; *out = value; }
    --retained->observation_depth;
    return ok;
}

bool qa_q3_wire_client_source_body_read(const qa_q3_game *game, uint32_t slot,
                                        qa_q3_wire_client_body *out, qa_error *error) {
    if (!game || !game->wire || !out || slot >= QA_Q3_SOURCE_CLIENTS ||
        game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 fixed source body read exceeds its physical clients");
    qa_q3_wire_client_body value = {.model_shape = game->clients[slot].source_model_shape};
    const qa_q3_source_binding *binding = &game->source_entities[slot];
    if (!binding->body_attached) { *out = value; return true; }
    qa_actor_id actor = binding->actor;
    if (!current(game, slot, actor)) return q3_fail(error, "Q3 fixed source body is stale");
    qa_q3_game *retained = (qa_q3_game *)game;
    ++retained->observation_depth;
    bool ok = q3_source_body_read(retained, actor, &value.current, error) && current(game, slot, actor);
    if (ok) {
        qa_actor_collision collision;
        qa_error observed = {0};
        bool colliding = qa_world_get_collision(game->options.services.world, actor, &collision, &observed);
        if (!colliding && observed.code != QA_OK) { if (error) *error = observed; ok = false; }
        if (colliding) value.model_shape = collision.shape == QA_SHAPE_CAPSULE ? QA_SHAPE_CAPSULE : QA_SHAPE_BOX;
        if (!current(game, slot, actor) || !game->source_entities[slot].body_attached) ok = false;
    }
    if (!ok && (!error || !error->code)) q3_fail(error, "Q3 fixed source body retired during observation");
    if (ok) *out = value;
    --retained->observation_depth;
    return ok;
}
bool qa_q3_wire_client_source_pm_read(const qa_q3_game *game, uint32_t slot,
                                      int32_t *out, qa_error *error) {
    if (!game || !game->wire || !out || slot >= QA_Q3_SOURCE_CLIENTS || game->source_restored)
        return q3_fail(error, "Q3 fixed source PM read exceeds its physical clients");
    if (game->clients[slot].has_followed_player) {
        *out = game->clients[slot].followed_player.pmType;
        return true;
    }
    if (!game->source_entities[slot].body_attached) {
        if (!game->wire->clients[slot].foreign_policy_written)
            return q3_fail(error, "Q3 fixed source PM has no genuine retained producer");
        *out = game->wire->clients[slot].foreign_policy.pm_type;
        return true;
    }
    qa_q3_wire_policy policy;
    if (!qa_q3_wire_player_policy_read(game, game->source_entities[slot].actor, &policy, error)) return false;
    *out = policy.pm_type;
    return true;
}
bool qa_q3_wire_client_source_score_read(const qa_q3_game *game, uint32_t slot,
                                         int32_t *out, qa_error *error) {
    if (!game || !game->wire || !out || slot >= QA_Q3_SOURCE_CLIENTS ||
        game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 fixed source score read exceeds its physical clients");
    if (game->clients[slot].has_followed_player) {
        *out = game->clients[slot].followed_player.persistant[0];
        return true;
    }
    if (!game->source_entities[slot].body_attached || game->wire->clients[slot].movement_detached ||
        (!game->source_entities[slot].in_use &&
         game->clients[slot].connected == QA_Q3_CLIENT_DISCONNECTED)) {
        *out = game->clients[slot].retired_score;
        return true;
    }
    qa_actor_id actor = game->source_entities[slot].actor;
    if (!current(game, slot, actor) || !game->wire->services.mode)
        return q3_fail(error, "Q3 fixed source score lacks its genuine selected owner");
    qa_q3_game *retained = (qa_q3_game *)game;
    ++retained->observation_depth;
    qa_q3_wire_mode mode;
    bool ok = game->wire->services.mode(game->wire->services.context, actor, &mode, error) &&
        current(game, slot, actor) && game->source_entities[slot].body_attached;
    if (ok) *out = mode.score;
    else if (!error || !error->code) q3_fail(error, "Q3 fixed source score retired during observation");
    --retained->observation_depth;
    return ok;
}

bool qa_q3_wire_borrowed_client_motion_read(const qa_q3_game *game, qa_actor_id actor,
                                            qa_q3_source_client_motion *out, qa_error *error) {
    uint32_t client;
    if (!game || !out || game->source_restored || game->observation_depth == SIZE_MAX ||
        !q3_source_client_pointer(game, actor, &client))
        return q3_fail(error, "Q3 borrowed PS motion requires its real client pointer");
    qa_q3_game *retained = (qa_q3_game *)game;
    ++retained->observation_depth;
    qa_actor_id original = game->source_entities[client].actor;
    qa_body_state body = {0};
    bool ok = !game->source_entities[client].body_attached ||
        (qa_world_body_read(game->options.services.world, original, &body, error) &&
         current(game, client, original) && game->source_entities[client].body_attached);
    uint32_t actual;
    if (ok) ok = q3_source_client_pointer(game, actor, &actual) && actual == client;
    qa_q3_player_motion motion;
    if (ok && !game->clients[client].has_followed_player)
        ok = q3_player_motion_slot_read(game, client, &motion, error);
    if (ok) *out = (qa_q3_source_client_motion){.origin = body.origin,
        .delta_yaw_word = game->clients[client].has_followed_player
            ? game->clients[client].followed_player.deltaAngles[1] : motion.delta_yaw_word};
    --retained->observation_depth;
    return ok || q3_fail(error, "Q3 borrowed PS motion changed during observation");
}
bool qa_q3_wire_borrowed_client_motion_write(qa_q3_game *game, qa_actor_id actor,
                                             const qa_q3_source_client_motion *value,
                                             qa_error *error) {
    uint32_t client;
    if (!game || !value || !qa_vec_finite(value->origin) || game->source_restored ||
        game->observation_depth == SIZE_MAX || !q3_source_client_pointer(game, actor, &client))
        return q3_fail(error, "Q3 borrowed PS motion write requires its real client pointer");
    ++game->observation_depth;
    qa_actor_id original;
    bool ok = q3_source_client_body_ensure(game, client, &original, error);
    qa_body_state body;
    if (ok) ok = qa_world_body_read(game->options.services.world, original, &body, error) &&
        current(game, client, original);
    uint32_t actual;
    if (ok) ok = q3_source_client_pointer(game, actor, &actual) && actual == client;
    if (ok) {
        body.origin = value->origin;
        ok = qa_world_body_write(game->options.services.world, original, &body, error) &&
            current(game, client, original) && q3_source_client_pointer(game, actor, &actual) &&
            actual == client;
    }
    if (ok) {
        if (game->wire->clients[client].movement_detached)
            game->wire->foreign_motions[client].delta_yaw_word = value->delta_yaw_word;
        else q3_player_delta_write(game, original, 1, value->delta_yaw_word);
        if (game->clients[client].has_followed_player)
            game->clients[client].followed_player.deltaAngles[1] = value->delta_yaw_word;
        q3_source_origin_written(game, original, value->origin);
    }
    --game->observation_depth;
    if (!ok && (!error || error->code == QA_OK))
        q3_fail(error, "Q3 borrowed PS motion changed during its write");
    return ok;
}

bool qa_q3_wire_entity_event_time(const qa_q3_game *game, uint32_t slot,
                                   int32_t *out, qa_error *error) {
    if (!game || !game->wire || !out || slot >= QA_Q3_SOURCE_ENTITIES || game->source_restored)
        return q3_fail(error, "Q3 event timestamp read exceeds its actual fixed source rows");
    const q3_wire_row *record = &game->wire->rows[slot];
    qa_actor_id actor = game->source_entities[slot].actor;
    if (!actor.registry) { *out = record->event_time_ms; return true; }
    if (!current(game, slot, actor))
        return q3_fail(error, "Q3 event timestamp has a stale physical source generation");
    const q3_actor *entry = q3_actor_const(game, actor);
    *out = entry && entry->kind == Q3_ACTOR_MISSILE ? entry->state.missile.event_at
        : entry && entry->kind == Q3_ACTOR_TEMPORARY ? entry->state.temporary.event_time_ms
        : entry && (entry->kind == Q3_ACTOR_PODIUM || entry->kind == Q3_ACTOR_VICTORY_MODEL)
            ? entry->state.postgame.event_time_ms
        : record->event_time_ms;
    return true;
}

static void damage_attacker(qa_q3_game *game, qa_actor_id target, qa_actor_id attacker) {
    uint32_t slot, owner_slot;
    qa_error ignored = {0};
    if (!qa_q3_native_client_slot(game, target, &slot, &ignored)) return;
    int32_t number = qa_q3_source_actor_slot(game, attacker, &owner_slot, &ignored)
        ? game->source_entities[owner_slot].number : (int32_t)QA_Q3_SOURCE_NONE;
    if (current(game, slot, target)) {
        game->wire->clients[slot].attacker = number;
        qa_q3_player *followed = q3_client_follow_player(game, slot);
        if (followed) followed->persistant[6] = number;
    }
}

bool q3_wire_damage(qa_q3_game *game, const qa_damage_outcome *outcome, qa_error *error) {
    if (!game || !game->wire || !outcome)
        return q3_fail(error, "Q3 damage source wire owner is absent");
    if (outcome->stale) return true;
    bool armor_mutation;
    float armor_saved = q3_damage_regular_delta(outcome, &armor_mutation);
    if (outcome->result.applied_damage == 0 && !armor_mutation) return true;
    uint32_t target_slot, attacker_slot;
    qa_error ignored = {0};
    qa_actor_id target = outcome->request.target, attacker = outcome->request.attack.attacker;
    if (qa_actor_id_equal(target, attacker) ||
        !qa_q3_native_client_slot(game, attacker, &attacker_slot, &ignored)) {
        damage_attacker(game, target, attacker);
        return true;
    }
    q3_wire_row *target_row = row(game, target);
    if (!target_row || target_row->source.type == 0 || target_row->source.type == 3) {
        damage_attacker(game, target, attacker);
        return true;
    }
    qa_combat_state combat;
    if (!qa_combat_read(game->options.services.combat, target, &combat, error)) return false;
    float previous_health = combat.health;
    for (size_t i = 0; i < outcome->mutation_count; ++i)
        if (outcome->mutations[i].kind == QA_MUTATION_HEALTH) {
            previous_health = outcome->mutations[i].value.health.before;
            break;
        }
    if (previous_health <= 0) {
        damage_attacker(game, target, attacker);
        return true;
    }
    bool target_client = qa_q3_native_client_slot(game, target, &target_slot, &ignored);
    bool same_team = false;
    if (target_client && qa_game_type_has_allies(game->options.rules.game_type)) {
        int32_t target_team = q3_source_team(game, target);
        int32_t attacker_team = q3_source_team(game, attacker);
        same_team = target_team == attacker_team;
    }
    if (!row(game, target) || !current(game, attacker_slot, attacker)) return true;
    q3_wire_client *client = &game->wire->clients[attacker_slot];
    qa_q3_player *followed = q3_client_follow_player(game, attacker_slot);
    client->hits = q3_add_time(followed ? followed->persistant[1] : client->hits, same_team ? -1 : 1);
    int32_t previous_armor = q3_source_float_to_int(
        (target_client ? (float)combat.armor.regular.points : 0) + armor_saved);
    client->attackee_armor = word(((uint32_t)q3_source_float_to_int(previous_health) << 8) |
                                  (uint32_t)previous_armor);
    if (followed) {
        followed->persistant[1] = client->hits;
        followed->persistant[7] = client->attackee_armor;
    }
    damage_attacker(game, target, attacker);
    return true;
}

qa_q3_entity *q3_wire_temporary(qa_q3_game *game, qa_actor_id actor) {
    q3_actor *entry = game ? q3_actor_get(game, actor) : NULL;
    return entry && entry->kind == Q3_ACTOR_TEMPORARY ? &entry->state.temporary.entity : NULL;
}

bool q3_wire_temp_entity(qa_q3_game *game, qa_vec3 origin, int32_t event,
                         qa_actor_id *out, qa_error *error) {
    if (!game || !game->wire || !out || !qa_vec_finite(origin) || event < 0 || event > 1023 ||
        game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid actual Q3 temporary-entity producer");
    *out = (qa_actor_id){0};
    ++game->observation_depth;
    qa_string_id name;
    bool ok = qa_builtin_resource(&game->options.services, "tempEntity", &name, error);
    qa_actor_id actor = {0};
    qa_vec3 snapped = qa_v3(snap_component(origin.x), snap_component(origin.y), snap_component(origin.z));
    if (ok) ok = q3_spawn_raw_actor(game, name, &actor, error);
    if (ok) {
        uint32_t slot;
        ok = qa_q3_source_actor_slot(game, actor, &slot, error);
        q3_actor *entry = ok ? q3_actor_storage(game, actor) : NULL;
        if (ok && (!entry || !row(game, actor)))
            ok = q3_fail(error, "Q3 temporary entity lost its physical source binding");
        if (ok) {
            qa_q3_entity original; qa_q3_wire_visibility visibility;
            qa_body_state body;
            ok = qa_q3_wire_entity_read(game, slot, &original, &visibility, error) &&
                qa_world_body_read(game->options.services.world, actor, &body, error);
            if (ok && (!current(game, slot, actor) || !row(game, actor)))
                ok = q3_fail(error, "Q3 temporary entity changed during its raw state observation");
            if (!ok) goto temp_done;
            game->source_entities[slot].classname = name;
            original.eType = 13 + event;
            original.pos = (qa_q3_trajectory){.base = {snapped.x, snapped.y, snapped.z}};
            *entry = (q3_actor){.actor = actor, .kind = Q3_ACTOR_TEMPORARY, .alpha = 1,
                .state.temporary = {.entity = original, .event_time_ms = game->now_ms}};
            row(game, actor)->source.free_after_event = true;
            body.origin = snapped;
            ok = qa_world_body_write(game->options.services.world, actor, &body, error) &&
                qa_q3_wire_link(game, actor, NULL, error);
            if (ok && row(game, actor)) ok = q3_wire_entity_ready(game, actor, error);
        }
    }
temp_done:
    if (!ok && actor.registry && row(game, actor))
        q3_rollback_spawn(game, actor, NULL);
    if (ok && row(game, actor)) *out = actor;
    --game->observation_depth;
    return ok;
}

static bool player_pending(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    uint32_t slot;
    if (!game || !game->wire || game->source_restored ||
        !qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !current(game, slot, actor))
        return q3_fail(error, "Q3 predictable publication lacks its actual source client");
    qa_q3_player_state *native = &entry->state.player;
    if (word(native->entity_event_sequence) >= word(native->event_sequence)) return true;
    uint32_t sequence = native->entity_event_sequence;
    int32_t event = native->events[sequence & 1u] | (int32_t)((sequence & 3u) << 8);
    int32_t external = native->external_event;
    native->external_event = 0;
    qa_q3_player *followed = q3_client_follow_player(game, slot);
    if (followed) followed->externalEvent = 0;
    qa_body_state body;
    qa_actor_id temporary = {0};
    bool ok = qa_world_body_read(game->options.services.world, actor, &body, error) &&
        current(game, slot, actor) && q3_wire_temp_entity(game, body.origin, event, &temporary, error);
    qa_q3_player player;
    if (ok) ok = qa_q3_wire_player_read(game, slot, &player, error);
    entry = q3_actor_get(game, actor);
    qa_q3_entity *s = ok ? q3_wire_temporary(game, temporary) : NULL;
    q3_wire_row *record = ok ? row(game, temporary) : NULL;
    uint32_t temporary_slot;
    if (ok && (!entry || entry->kind != Q3_ACTOR_PLAYER || !current(game, slot, actor) ||
        !s || !record || !qa_q3_source_actor_slot(game, temporary, &temporary_slot, error)))
        ok = q3_fail(error, "Q3 predictable event lost its actual temporary or client generation");
    if (ok) {
        q3_wire_entity_source converted = {0};
        convert_player(player, &entry->state.player, &converted, &s->pos, &s->apos, true, false, 0);
        followed = q3_client_follow_player(game, slot);
        if (followed) followed->entityEventSequence = word(entry->state.player.entity_event_sequence);
        s->eType = 13 + event; s->eFlags = converted.flags | 16;
        s->angles2[1] = converted.angles2.y;
        s->legsAnim = converted.legs; s->torsoAnim = converted.torso;
        s->clientNum = converted.client; s->event = converted.event;
        s->eventParm = converted.event_parameter; s->weapon = converted.weapon;
        s->groundEntityNum = converted.ground_entity; s->powerups = converted.powerups;
        s->loopSound = converted.loop_sound; s->generic1 = converted.generic1;
        s->otherEntityNum = player.clientNum;
        record->source.single_client = player.clientNum;
        game->source_entities[temporary_slot].server_flags |= 2048u;
    }
    entry = q3_actor_get(game, actor);
    if (entry && entry->kind == Q3_ACTOR_PLAYER && current(game, slot, actor)) {
        entry->state.player.external_event = external;
        followed = q3_client_follow_player(game, slot);
        if (followed) followed->externalEvent = external;
    }
    if (!ok && temporary.registry && row(game, temporary))
        q3_rollback_spawn(game, temporary, NULL);
    return ok;
}

bool qa_q3_wire_player_pending(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!game || !game->wire || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 predictable event publication requires its retained source owner");
    ++game->observation_depth;
    bool ok = player_pending(game, actor, error);
    --game->observation_depth;
    return ok;
}

bool q3_wire_add_event(qa_q3_game *game, qa_actor_id actor, int32_t event,
                       int32_t parameter, qa_error *error) {
    q3_wire_row *record = row(game, actor);
    if (!record || game->source_restored || event < 0 || event > 255)
        return q3_fail(error, "Q3 G_AddEvent lacks its actual source row");
    if (!event) {
        if (!game->options.hooks.console_print) return true;
        char warning[96];
        snprintf(warning, sizeof(warning), "G_AddEvent: zero event added for entity %i\n",
                 q3_entity_number(game, actor));
        if (game->observation_depth == SIZE_MAX)
            return q3_fail(error, "Q3 G_AddEvent callback nesting exhausted");
        ++game->observation_depth;
        bool ok = game->options.hooks.console_print(game->options.hooks.context, warning, error);
        --game->observation_depth;
        return ok;
    }
    q3_actor *entry = q3_actor_get(game, actor);
    uint32_t client;
    if (q3_source_client_pointer(game, actor, &client)) {
        qa_q3_player_state *p = &game->client_actors[client].state.player;
        p->external_event = event | (((p->external_event & 0x300) + 0x100) & 0x300);
        p->external_event_parameter = parameter;
        p->external_event_time = game->now_ms;
        qa_q3_player *followed = q3_client_follow_player(game, client);
        if (followed) {
            followed->externalEvent = p->external_event;
            followed->externalEventParm = parameter;
            followed->externalEventTime = game->now_ms;
        }
        if (entry && (entry->kind == Q3_ACTOR_PODIUM || entry->kind == Q3_ACTOR_VICTORY_MODEL))
            entry->state.postgame.event_time_ms = game->now_ms;
        else record->event_time_ms = game->now_ms;
    } else if (entry && entry->kind == Q3_ACTOR_TEMPORARY) {
        qa_q3_entity *s = &entry->state.temporary.entity;
        s->event = event | (((s->event & 0x300) + 0x100) & 0x300);
        s->eventParm = parameter;
        entry->state.temporary.event_time_ms = game->now_ms;
    } else if (entry && (entry->kind == Q3_ACTOR_PODIUM || entry->kind == Q3_ACTOR_VICTORY_MODEL)) {
        qa_q3_entity *s = &entry->state.postgame.entity;
        s->event = event | (((s->event & 0x300) + 0x100) & 0x300);
        s->eventParm = parameter;
        entry->state.postgame.event_time_ms = game->now_ms;
    } else {
        record->source.event = event | (((record->source.event & 0x300) + 0x100) & 0x300);
        record->source.event_parameter = parameter;
        if (entry && entry->kind == Q3_ACTOR_MISSILE)
            entry->state.missile.event_at = game->now_ms;
        else record->event_time_ms = game->now_ms;
    }
    return true;
}

bool q3_wire_event_time(qa_q3_game *game, qa_actor_id actor, int32_t time_ms, qa_error *error) {
    q3_wire_row *record = row(game, actor);
    if (!record || game->source_restored)
        return q3_fail(error, "Q3 event timestamp requires its actual source row");
    q3_actor *entry = q3_actor_get(game, actor);
    if (entry && entry->kind == Q3_ACTOR_MISSILE) entry->state.missile.event_at = time_ms;
    else if (entry && entry->kind == Q3_ACTOR_TEMPORARY)
        entry->state.temporary.event_time_ms = time_ms;
    else if (entry && (entry->kind == Q3_ACTOR_PODIUM || entry->kind == Q3_ACTOR_VICTORY_MODEL))
        entry->state.postgame.event_time_ms = time_ms;
    else record->event_time_ms = time_ms;
    return true;
}

static bool expire_events(qa_q3_game *game, qa_actor_id actor,
                           q3_wire_event_status *status, qa_error *error) {
    q3_wire_row *record = row(game, actor);
    uint32_t slot;
    if (!record || !status || game->source_restored ||
        !qa_q3_source_actor_slot(game, actor, &slot, error))
        return q3_fail(error, "Q3 event expiry lacks its actual source row");
    *status = Q3_WIRE_EVENT_INACTIVE;
    if (!game->source_entities[slot].in_use) return true;
    *status = record->source.free_after_event ? Q3_WIRE_EVENT_WAITING : Q3_WIRE_EVENT_ACTIVE;
    q3_actor *entry = q3_actor_get(game, actor);
    int32_t timestamp = entry && entry->kind == Q3_ACTOR_MISSILE ? entry->state.missile.event_at
        : entry && entry->kind == Q3_ACTOR_TEMPORARY ? entry->state.temporary.event_time_ms
        : entry && (entry->kind == Q3_ACTOR_PODIUM || entry->kind == Q3_ACTOR_VICTORY_MODEL)
            ? entry->state.postgame.event_time_ms
        : record->event_time_ms;
    if (q3_sub_time(game->now_ms, timestamp) > 300) {
        if (entry && entry->kind == Q3_ACTOR_TEMPORARY)
            entry->state.temporary.entity.event = 0;
        int32_t *source_event = entry && (entry->kind == Q3_ACTOR_PODIUM ||
            entry->kind == Q3_ACTOR_VICTORY_MODEL) ? &entry->state.postgame.entity.event : &record->source.event;
        if (*source_event) {
            *source_event = 0;
            uint32_t client;
            if (q3_source_client_pointer(game, actor, &client)) {
                game->client_actors[client].state.player.external_event = 0;
                qa_q3_player *followed = q3_client_follow_player(game, client);
                if (followed) followed->externalEvent = 0;
            }
        }
        if (record->source.free_after_event) {
            if (!qa_world_unlink(game->options.services.world, actor, error)) return false;
            if (!current(game, slot, actor)) { *status = Q3_WIRE_EVENT_FREED; return true; }
            if (game->source_entities[slot].never_free) return true;
            if (!qa_session_release(game->options.services.session, actor, error)) return false;
            *status = Q3_WIRE_EVENT_FREED;
        } else if (record->source.unlink_after_event) {
            record->source.unlink_after_event = false;
            if (!qa_world_unlink(game->options.services.world, actor, error)) return false;
            if (!current(game, slot, actor)) *status = Q3_WIRE_EVENT_FREED;
        }
    }
    return true;
}

bool q3_wire_expire_events(qa_q3_game *game, qa_actor_id actor,
                           q3_wire_event_status *status, qa_error *error) {
    if (!game || !game->wire || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 event expiry requires its retained source owner");
    ++game->observation_depth;
    bool ok = expire_events(game, actor, status, error);
    --game->observation_depth;
    return ok;
}

#define WIRE_FIELD(kind, value) do { \
    if (!qa_source_save_##kind(io, &(value))) return false; \
} while (0)

static bool save_trajectory(qa_source_save_io *io, qa_q3_trajectory *p) {
    WIRE_FIELD(i32, p->type); WIRE_FIELD(i32, p->time); WIRE_FIELD(i32, p->duration);
    for (size_t i = 0; i < 3; ++i) WIRE_FIELD(f32, p->base[i]);
    for (size_t i = 0; i < 3; ++i) WIRE_FIELD(f32, p->delta[i]);
    return true;
}

static bool save_entity(qa_source_save_io *io, q3_wire_entity_source *p) {
    qa_trajectory *trajectories[2] = {&p->position, &p->angular};
    for (size_t i = 0; i < 2; ++i) {
        qa_trajectory *t = trajectories[i];
        uint32_t type = (uint32_t)t->type;
        WIRE_FIELD(u32, type);
        if (type > QA_TRAJECTORY_GRAVITY) return q3_fail(io->error, "invalid Q3 source trajectory kind");
        if (io->direction == QA_SOURCE_SAVE_READ) t->type = (qa_trajectory_type)type;
        WIRE_FIELD(i32, t->time_ms); WIRE_FIELD(i32, t->duration_ms);
        WIRE_FIELD(vec3, t->base); WIRE_FIELD(vec3, t->delta);
    }
    WIRE_FIELD(i32, p->type); WIRE_FIELD(i32, p->flags);
    WIRE_FIELD(i32, p->time); WIRE_FIELD(i32, p->time2);
    WIRE_FIELD(vec3, p->authored_origin); WIRE_FIELD(vec3, p->origin2);
    WIRE_FIELD(vec3, p->authored_angles); WIRE_FIELD(vec3, p->angles2);
    WIRE_FIELD(i32, p->other_entity); WIRE_FIELD(i32, p->other_entity2);
    WIRE_FIELD(i32, p->ground_entity); WIRE_FIELD(i32, p->constant_light);
    WIRE_FIELD(i32, p->loop_sound); WIRE_FIELD(i32, p->model); WIRE_FIELD(i32, p->model2);
    WIRE_FIELD(i32, p->client); WIRE_FIELD(i32, p->frame); WIRE_FIELD(i32, p->solid);
    WIRE_FIELD(i32, p->event); WIRE_FIELD(i32, p->event_parameter);
    WIRE_FIELD(i32, p->powerups); WIRE_FIELD(i32, p->weapon);
    WIRE_FIELD(i32, p->legs); WIRE_FIELD(i32, p->torso); WIRE_FIELD(i32, p->generic1);
    WIRE_FIELD(i32, p->single_client);
    WIRE_FIELD(bool, p->free_after_event); WIRE_FIELD(bool, p->unlink_after_event);
    WIRE_FIELD(i32, p->arena_think); WIRE_FIELD(i32, p->arena_nextthink);
    return true;
}

static bool save_policy(qa_source_save_io *io, qa_q3_wire_policy *p) {
    WIRE_FIELD(i32, p->pm_type); WIRE_FIELD(i32, p->bob_cycle);
    WIRE_FIELD(i32, p->pm_flags); WIRE_FIELD(i32, p->pm_time);
    WIRE_FIELD(i32, p->gravity); WIRE_FIELD(i32, p->speed);
    WIRE_FIELD(i32, p->movement_dir);
    return true;
}

static bool save_link(qa_source_save_io *io, q3_wire_link_state *p) {
    WIRE_FIELD(bool, p->written);
    if (!p->written) return true;
    WIRE_FIELD(u64, p->link_count);
    WIRE_FIELD(i32, p->area); WIRE_FIELD(i32, p->area2); WIRE_FIELD(i32, p->last_cluster);
    WIRE_FIELD(u32, p->cluster_count);
    if (p->cluster_count > 16) return q3_fail(io->error, "Q3 source link has too many clusters");
    for (size_t i = 0; i < p->cluster_count; ++i) WIRE_FIELD(i32, p->clusters[i]);
    return true;
}

static bool save_owner(qa_source_save_io *io, q3_wire_state *state) {
    char magic[8] = "Q3WIRE2";
    if (!qa_source_save_bytes(io, magic, sizeof(magic))) return false;
    if (memcmp(magic, "Q3WIRE2", sizeof(magic)))
        return q3_fail(io->error, "invalid Q3 source wire continuation schema");
    for (uint32_t i = 0; i < QA_Q3_SOURCE_ENTITIES; ++i) {
        q3_wire_row *p = &state->rows[i];
        const q3_wire_row empty = {.initialized = i < QA_Q3_SOURCE_CLIENTS};
        bool retained = io->direction == QA_SOURCE_SAVE_WRITE && memcmp(p, &empty, sizeof(*p));
        WIRE_FIELD(bool, retained);
        if (!retained) {
            if (io->direction == QA_SOURCE_SAVE_READ) *p = empty;
            continue;
        }
        WIRE_FIELD(actor, p->actor);
        if (!save_entity(io, &p->source)) return false;
        WIRE_FIELD(i32, p->event_time_ms);
        if (!save_link(io, &p->link)) return false;
        WIRE_FIELD(bool, p->initialized);
        WIRE_FIELD(bool, p->player_published);
        if (p->player_published &&
            (!save_trajectory(io, &p->player_position) ||
             !save_trajectory(io, &p->player_angles))) return false;
    }
    const q3_wire_client empty_client = {.foreign_policy_written = true};
    for (uint32_t i = 0; i < QA_Q3_SOURCE_CLIENTS; ++i) {
        q3_wire_client *p = &state->clients[i];
        bool retained = io->direction == QA_SOURCE_SAVE_WRITE &&
            memcmp(p, &empty_client, sizeof(*p));
        WIRE_FIELD(bool, retained);
        if (!retained) {
            if (io->direction == QA_SOURCE_SAVE_READ) *p = empty_client;
            continue;
        }
        if (!save_policy(io, &p->foreign_policy)) return false;
        WIRE_FIELD(bool, p->foreign_policy_written);
        WIRE_FIELD(bool, p->movement_detached);
        WIRE_FIELD(i32, p->hits); WIRE_FIELD(i32, p->attacker);
        WIRE_FIELD(i32, p->attackee_armor);
        WIRE_FIELD(i32, p->clients_ready);
        WIRE_FIELD(i32, p->loop_sound);
        for (size_t slot = 0; slot < 16; ++slot) WIRE_FIELD(i32, p->special_ammo[slot]);
    }
    return true;
}

#undef WIRE_FIELD

static bool policy_valid(const qa_q3_wire_policy *p) {
    return p->pm_type >= 0 && p->pm_type <= 6 && p->bob_cycle >= 0 && p->bob_cycle <= 255 &&
        p->movement_dir >= 0 && p->movement_dir <= 7;
}

static bool fields_valid(const q3_wire_row *row, uint32_t slot) {
    const q3_wire_entity_source *s = &row->source;
    if (s->arena_think < 0 || s->arena_think > 3 ||
        (!s->arena_think && s->arena_nextthink) ||
        s->position.type < QA_TRAJECTORY_STATIONARY || s->position.type > QA_TRAJECTORY_GRAVITY ||
        s->angular.type < QA_TRAJECTORY_STATIONARY || s->angular.type > QA_TRAJECTORY_GRAVITY ||
        !qa_vec_finite(s->position.base) || !qa_vec_finite(s->position.delta) ||
        !qa_vec_finite(s->angular.base) || !qa_vec_finite(s->angular.delta) ||
        !qa_vec_finite(s->authored_origin) || !qa_vec_finite(s->origin2) ||
        !qa_vec_finite(s->authored_angles) || !qa_vec_finite(s->angles2) ||
        s->other_entity < 0 || s->other_entity >= (int32_t)QA_Q3_SOURCE_ENTITIES ||
        s->other_entity2 < 0 || s->other_entity2 >= (int32_t)QA_Q3_SOURCE_ENTITIES ||
        s->ground_entity < -1 || s->ground_entity >= (int32_t)QA_Q3_SOURCE_ENTITIES ||
        s->single_client < 0 || s->single_client >= (int32_t)QA_Q3_SOURCE_CLIENTS ||
        s->model < 0 || s->model > 255 || s->model2 < 0 || s->model2 > 255 ||
        s->loop_sound < 0 || s->loop_sound > 255 ||
        row->link.cluster_count > 16 || row->link.area < -1 ||
        row->link.area2 < -1 || row->link.last_cluster < -1) return false;
    for (size_t i = 0; i < row->link.cluster_count; ++i)
        if (row->link.clusters[i] < 0) return false;
    if (!row->player_published) return true;
    if (slot >= QA_Q3_SOURCE_CLIENTS ||
        (row->player_position.type != 1 && row->player_position.type != 3) ||
        row->player_angles.type != 1 ||
        (row->player_position.type == 3 && row->player_position.duration != 50)) return false;
    for (size_t i = 0; i < 3; ++i)
        if (!isfinite(row->player_position.base[i]) || !isfinite(row->player_position.delta[i]) ||
            !isfinite(row->player_angles.base[i]) || !isfinite(row->player_angles.delta[i])) return false;
    return true;
}

static bool owner_valid(const q3_wire_state *state, qa_error *error) {
    for (uint32_t i = 0; i < QA_Q3_SOURCE_ENTITIES; ++i)
        if (!fields_valid(&state->rows[i], i))
            return q3_fail(error, "Q3 source wire continuation contains invalid entity fields");
    if (state->rows[QA_Q3_SOURCE_NONE].actor.registry)
        return q3_fail(error, "Q3 source NONE row cannot own an entity");
    for (uint32_t i = 0; i < QA_Q3_SOURCE_CLIENTS; ++i)
        if (!policy_valid(&state->clients[i].foreign_policy) ||
            state->clients[i].clients_ready < 0 || state->clients[i].clients_ready > 65535 ||
            state->clients[i].loop_sound < 0 || state->clients[i].loop_sound > 255)
            return q3_fail(error, "Q3 source wire continuation contains invalid native PM policy");
    return true;
}

static bool link_valid(const qa_q3_game *game, const q3_wire_row *record,
                         qa_error *error) {
    if (!record->actor.registry) return true;
    qa_body_link_state linked;
    if (!qa_world_link_state(game->options.services.world, record->actor, &linked))
        return q3_fail(error, "Q3 source continuation has no actual body link owner");
    if (linked.linked &&
        (!record->link.written || record->link.link_count != linked.link_count))
        return q3_fail(error, "Q3 source continuation disagrees with its actual authored world link");
    return true;
}

static bool special_ammo_valid(const qa_q3_game *game, const q3_wire_state *state,
                                 qa_error *error) {
    for (uint32_t client = 0; client < QA_Q3_SOURCE_CLIENTS; ++client)
        for (uint32_t slot = 0; slot < QA_Q3_WEAPON_COUNT; ++slot)
            if (game->ammo_items[slot] && state->clients[client].special_ammo[slot])
                return q3_fail(error, "Q3 source continuation duplicates shared inventory ammunition");
    return true;
}

bool q3_wire_validate(const qa_q3_game *game, const q3_wire_state *state, qa_error *error) {
    if (!game || !state || !owner_valid(state, error) ||
        !special_ammo_valid(game, state, error)) return false;
    for (uint32_t i = 0; i < QA_Q3_SOURCE_ENTITIES; ++i) {
        if (state->rows[i].source.arena_think &&
            (i < QA_Q3_SOURCE_CLIENTS || i >= game->source_count))
            return q3_fail(error, "Q3 arena continuation exceeds actual dynamic source rows");
        qa_actor_id actor = state->rows[i].actor;
        if (!qa_actor_id_equal(actor, game->source_entities[i].actor) ||
            (actor.registry && !qa_actors_get(
                qa_session_actors(game->options.services.session), actor)))
            return q3_fail(error, "Q3 source wire continuation disagrees with physical GAME bindings");
        if (game->source_entities[i].in_use && !state->rows[i].initialized)
            return q3_fail(error, "Q3 source wire capture contains an unfinished native constructor");
        if (!link_valid(game, &state->rows[i], error)) return false;
        if (state->rows[i].player_published) {
            const q3_actor *entry = i < QA_Q3_SOURCE_CLIENTS ? &game->client_actors[i] : NULL;
            if (!entry || entry->kind != Q3_ACTOR_PLAYER)
                return q3_fail(error, "Q3 BG continuation has no actual source player");
        }
    }
    return true;
}

bool q3_wire_validate_saved(const qa_q3_game *game, const q3_wire_state *state,
                            const qa_q3_checkpoint *saved, bool world_links,
                            qa_error *error) {
    if (!game || !state || !saved || !owner_valid(state, error) ||
        !special_ammo_valid(game, state, error)) return false;
    for (uint32_t i = 0; i < QA_Q3_SOURCE_ENTITIES; ++i) {
        if (state->rows[i].source.arena_think &&
            (i < QA_Q3_SOURCE_CLIENTS || i >= saved->source_count))
            return q3_fail(error, "Q3 saved arena continuation exceeds actual dynamic source rows");
        qa_actor_id actor = state->rows[i].actor;
        if (!qa_actor_id_equal(actor, saved->source_entities[i].actor) ||
            (saved->source_entities[i].in_use && (!actor.registry ||
             !qa_actors_get(qa_session_actors(game->options.services.session), actor))))
            return q3_fail(error, "Q3 source wire continuation disagrees with saved physical GAME rows");
        if (saved->source_entities[i].in_use && !state->rows[i].initialized)
            return q3_fail(error, "Q3 source wire restore contains an unfinished native constructor");
        if (world_links && !link_valid(game, &state->rows[i], error)) return false;
        if (state->rows[i].player_published &&
            (i >= QA_Q3_SOURCE_CLIENTS || saved->source_clients[i].kind != Q3_ACTOR_PLAYER ||
             !qa_actor_id_equal(saved->source_clients[i].actor, actor)))
            return q3_fail(error, "Q3 saved BG conversion has no actual source client state");
    }
    return true;
}

bool q3_wire_capture(const qa_q3_game *game, qa_buffer *out, qa_error *error) {
    if (!game || !game->wire || !out || out->data || game->observation_depth ||
        !q3_source_origins_idle(game) ||
        !q3_wire_validate(game, game->wire, error))
        return q3_fail(error, "Q3 source wire capture requires its idle qualified owner");
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, game->options.services.session, error)) return false;
    bool ok = save_owner(&io, game->wire) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool q3_wire_prepare(qa_q3_game *game, qa_bytes bytes, q3_wire_state **out, qa_error *error) {
    if (!game || !game->wire || !out || *out || !bytes.data)
        return q3_fail(error, "Q3 source wire restore requires an empty candidate");
    q3_wire_state *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "preparing Q3 source wire continuation");
        return false;
    }
    candidate->services = game->wire->services;
    qa_source_save_io io;
    if (!qa_source_save_reader(&io, game->options.services.session, bytes, error)) {
        free(candidate);
        return false;
    }
    bool ok = save_owner(&io, candidate) && io.offset == bytes.size &&
        owner_valid(candidate, error);
    if (!ok && (!error || !error->code))
        q3_fail(error, "Q3 source wire continuation contains trailing or invalid fields");
    qa_source_save_dispose(&io);
    if (!ok) { free(candidate); return false; }
    *out = candidate;
    return true;
}

void q3_wire_discard(q3_wire_state *state) { free(state); }

void q3_wire_commit(qa_q3_game *game, q3_wire_state *state) {
    free(game->wire);
    game->wire = state;
}
