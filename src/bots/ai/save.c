#include "internal.h"
#include "../runtime/internal.h"
#include "../save_fields.h"
#include "qa/bots_population_save.h"

static const uint8_t magic[8] = {'Q', 'A', 'B', 'P', 'O', 'P', 'U', 0};
#define FIELD(kind, value) do { if (!qa_source_save_##kind(io, &(value))) return false; } while (0)
#define A(value) FIELD(actor, value)
#define V(value) FIELD(vec3, value)
#define F(value) FIELD(f32, value)
#define I(value) FIELD(i32, value)
#define U(value) FIELD(u32, value)
#define B(value) FIELD(bool, value)
#define L(value) FIELD(u64, value)

static bool goal_fields(qa_source_save_io *io, qa_bot_goal *goal)
{
    V(goal->origin); I(goal->area); V(goal->mins); V(goal->maxs);
    I(goal->entity); I(goal->number); I(goal->flags); I(goal->item_info); return true;
}
static bool command_fields(qa_source_save_io *io, qa_movement_command *command)
{
    uint32_t kind = command->kind;
    U(kind); if (kind > QA_MOVEMENT_Q3) return false;
    command->kind = (qa_movement_kind)kind;
    L(command->sequence); U(command->milliseconds); I(command->server_time_ms); I(command->server_frame);
    FIELD(f64, command->acknowledged_server_seconds); V(command->angles);
    for (size_t i = 0; i < 3; ++i) I(command->angle_words[i]);
    F(command->forward_move); F(command->side_move); F(command->up_move); U(command->buttons);
    FIELD(u8, command->impulse); FIELD(u8, command->light_level); FIELD(u8, command->weapon); return true;
}
static bool view_fields(qa_source_save_io *io, qa_bot_view *view)
{
    uint32_t decision = view->decision, order = view->order.kind, status = view->order.status;
    A(view->actor); A(view->enemy); U(view->client); I(view->entity); I(view->weapon);
    U(view->mode.slot); L(view->mode.generation);
    U(decision); U(order); U(status);
    if (decision > QA_BOT_BATTLE_NEARBY || order > QA_BOT_ORDER_FOLLOW || status > QA_BOT_ORDER_ACTIVE) return false;
    view->decision = (qa_bot_decision)decision; view->order.kind = (qa_bot_order_kind)order;
    view->order.status = (qa_bot_order_status)status;
    V(view->order.point); A(view->order.target); F(view->enter_time); F(view->think_time); return true;
}
static bool player_fields(qa_source_save_io *io, qa_bot_player *player)
{
    B(player->connected); B(player->observer); B(player->intermission); B(player->dead);
    B(player->grounded); B(player->crouched); B(player->teleported); B(player->water_jump);
    B(player->grapple_pull); B(player->firing); B(player->invisible); B(player->chatting); B(player->carrying_objective);
    V(player->origin); V(player->velocity); V(player->eye); V(player->view_angles);
    for (size_t i = 0; i < 3; ++i) I(player->delta_angles[i]);
    U(player->presence); I(player->current_weapon); I(player->weapon_state); I(player->weapon_time_ms);
    for (size_t i = 0; i < QA_BOT_INVENTORY_SIZE; ++i) I(player->inventory[i]);
    A(player->last_attacker); A(player->last_victim); I(player->deaths); I(player->kills); I(player->last_damage_cause);
    F(player->air_time); F(player->teleport_time); L(player->spawn_sequence); L(player->teleport_sequence); return true;
}
static bool state_fields(qa_source_save_io *io, bot_ai_state *state)
{
    if (!view_fields(io, &state->view) || !player_fields(io, &state->player)) return false;
    V(state->angles.angles); V(state->angles.ideal); V(state->angles.velocity);
    if (!command_fields(io, &state->last_command)) return false;
    U(state->character); U(state->goals); U(state->weapons); U(state->chat); U(state->movement);
    U(state->area); U(state->travel_flags); U(state->setup_count); I(state->residual_ms); I(state->last_health);
    F(state->local_time); F(state->walker); F(state->long_term_until); F(state->nearby_until);
    F(state->stand_until); F(state->stand_enemy_time); F(state->respawn_time); F(state->respawn_chat_time);
    F(state->chase_time); F(state->enemy_visible_time); F(state->enemy_sight_time); F(state->check_time);
    F(state->attack_crouch_time); F(state->attack_jump_time); F(state->attack_strafe_time); F(state->fire_wait_time);
    F(state->fire_until); F(state->weapon_change_time); F(state->enemy_death_time); F(state->state_time); F(state->chase_until);
    F(state->teleport_time); L(state->teleport_sequence); F(state->last_air_time); F(state->last_chat_time);
    F(state->blocked_time); F(state->not_blocked_time);
    V(state->enemy_origin); V(state->enemy_velocity); V(state->last_enemy_origin); V(state->aim_target);
    U(state->last_enemy_area); B(state->respawn_wait); B(state->suicidal); B(state->strafe_right);
    B(state->team_arena); B(state->retired); B(state->attacked); B(state->chat_pending);
    L(state->command_sequence); U(state->activation_count);
    if (state->activation_count > 8) return false;
    for (size_t i = 0; i < 8; ++i) {
        qa_bot_activation *activation = &state->activations[i].activation;
        uint32_t resume = state->activations[i].resume;
        A(activation->blocker); A(activation->target);
        if (!goal_fields(io, &activation->goal)) return false;
        V(activation->blocker_origin); V(activation->target_origin); V(activation->aim); B(activation->shoot);
        U(resume); if (resume > QA_BOT_BATTLE_NEARBY) return false;
        state->activations[i].resume = (qa_bot_decision)resume; F(state->activations[i].until);
    }
    uint32_t team = state->team_task;
    U(team); if (team > BOT_TEAM_CAMP) return false;
    state->team_task = (bot_team_task)team;
    A(state->team_requester); A(state->team_leader); F(state->team_task_until); V(state->camp_origin);
    return qa_source_save_bytes(io, state->name, sizeof(state->name)) && memchr(state->name, 0, sizeof(state->name));
}

static bool controls_fields(qa_source_save_io *io, qa_bot_controls *controls)
{
    I(controls->think_time_ms); B(controls->paused); B(controls->challenge); B(controls->fast_chat);
    B(controls->no_chat); B(controls->rocket_jump); B(controls->grapple); B(controls->report); return true;
}
static bool snapshot_fields(qa_source_save_io *io, qa_builtin_actor_snapshot *snapshot)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (!qa_source_save_count(io, &snapshot->capacity, SIZE_MAX / (2 * sizeof(*snapshot->ids))) ||
        snapshot->capacity > qa_actors_capacity(qa_session_actors(io->session)) ||
        !qa_source_save_count(io, &snapshot->count, snapshot->capacity)) return false;
    if (reading) {
        if (snapshot->count > (io->input.size - io->offset) / 13)
            return bot_save_fail(io, QA_ERROR_FORMAT, "Truncated retained bot population observations");
        if (snapshot->capacity) {
            snapshot->ids = calloc(snapshot->capacity, 2 * sizeof(*snapshot->ids));
            if (!snapshot->ids)
                return bot_save_fail(io, QA_ERROR_MEMORY, "Restoring retained bot observation capacity");
            snapshot->sort = snapshot->ids + snapshot->capacity;
        }
    } else if (snapshot->capacity && (!snapshot->ids || snapshot->sort != snapshot->ids + snapshot->capacity))
        return bot_save_fail(io, QA_ERROR_FORMAT, "Incomplete actual bot observation buffers");
    for (size_t i = 0; i < snapshot->count; ++i) A(snapshot->ids[i]);
    return true;
}
static bool topology(const qa_bots *bots, qa_error *error)
{
    if (bots->client_capacity > INT32_MAX || bots->count > bots->client_capacity ||
        (bots->client_capacity && !bots->clients) || (bots->actor_capacity && !bots->actor_clients) ||
        bots->actor_capacity > qa_actors_capacity(qa_session_actors(bots->services.shared.session))) goto invalid;
    uint32_t count = 0;
    for (uint32_t i = 0; i < bots->client_capacity; ++i) {
        const bot_ai_state *state = bots->clients[i]; if (!state) continue;
        if (!state->view.actor.registry || state->view.client != i || state->view.actor.slot >= bots->actor_capacity ||
            bots->actor_clients[state->view.actor.slot] != i + 1 || state->activation_count > 8 ||
            state->view.entity < 0 || state->setup_count > 4 ||
            state->view.decision > QA_BOT_BATTLE_NEARBY || state->view.order.kind > QA_BOT_ORDER_FOLLOW ||
            state->view.order.status > QA_BOT_ORDER_ACTIVE || state->team_task > BOT_TEAM_CAMP ||
            !memchr(state->name, 0, sizeof(state->name)) || (!state->retired && !bot_ai_live(bots, state->view.actor))) goto invalid;
        ++count;
        if (!qa_bot_runtime_closed(bots->runtime)) {
            if ((!state->retired && (!state->character || !state->goals || !state->weapons || !state->chat || !state->movement)) ||
                (state->character && !qa_bot_runtime_character(bots->runtime, state->character)) ||
                (state->goals && !qa_bot_goals_has_handle(qa_bot_runtime_goals(bots->runtime), state->goals)) ||
                (state->weapons && !qa_bot_runtime_weapon_has_handle(bots->runtime, state->weapons)) ||
                (state->chat && !qa_bot_runtime_chat(bots->runtime, state->chat)) ||
                (state->movement && !qa_bot_moves_has_handle(qa_bot_runtime_moves(bots->runtime), state->movement))) goto invalid;
            if (state->goals) {
                qa_bot_goal_state goal; qa_bot_weights *borrowed;
                if (!qa_bot_goals_capture(qa_bot_runtime_goals(bots->runtime), state->goals, &goal, &borrowed, error) ||
                    goal.client != (int32_t)state->view.client) goto invalid;
            }
            for (uint32_t j = 0; j < i; ++j) {
                const bot_ai_state *other = bots->clients[j]; if (!other) continue;
                if ((state->goals && state->goals == other->goals) || (state->weapons && state->weapons == other->weapons) ||
                    (state->chat && state->chat == other->chat) || (state->movement && state->movement == other->movement)) goto invalid;
            }
        }
    }
    if (count != bots->count) goto invalid;
    for (uint32_t i = 0; i < bots->actor_capacity; ++i) {
        uint32_t client = bots->actor_clients[i]; if (!client) continue;
        if (client > bots->client_capacity || !bots->clients[client - 1] || bots->clients[client - 1]->view.actor.slot != i) goto invalid;
    }
    return true;
invalid:
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid native bot population handle/actor lookup topology"); return false;
}
static void clear(qa_bots *bots)
{
    if (bots->clients) for (uint32_t i = 0; i < bots->client_capacity; ++i) free(bots->clients[i]);
    free(bots->clients); free(bots->actor_clients);
    qa_builtin_snapshot_free(&bots->entities); qa_builtin_snapshot_free(&bots->players);
}
static bool fields(qa_source_save_io *io, qa_bots *bots)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    U(bots->client_capacity); U(bots->actor_capacity); U(bots->count);
    if (bots->client_capacity > INT32_MAX || bots->client_capacity > SIZE_MAX / sizeof(*bots->clients) ||
        bots->actor_capacity > SIZE_MAX / sizeof(*bots->actor_clients) || bots->count > bots->client_capacity) return false;
    if (!controls_fields(io, &bots->controls)) return false;
    I(bots->local_time_ms); I(bots->library_residual_ms); I(bots->scheduled_think_ms);
    F(bots->time); F(bots->regular_update_time); L(bots->command_sequence);
    for (size_t i = 0; i < QA_BOT_INVENTORY_SIZE; ++i) I(bots->inventory_scratch[i]);
    if (reading) {
        size_t remaining = io->input.size - io->offset;
        if (bots->client_capacity > remaining || bots->count > (remaining - bots->client_capacity) / 1280 ||
            bots->actor_capacity > qa_actors_capacity(qa_session_actors(bots->services.shared.session)))
            return bot_save_fail(io, QA_ERROR_FORMAT, "Truncated native bot population storage");
        if (bots->client_capacity) bots->clients = calloc(bots->client_capacity, sizeof(*bots->clients));
        if (bots->actor_capacity) bots->actor_clients = calloc(bots->actor_capacity, sizeof(*bots->actor_clients));
        if ((bots->client_capacity && !bots->clients) || (bots->actor_capacity && !bots->actor_clients))
            return bot_save_fail(io, QA_ERROR_MEMORY, "Restoring actual bot client/actor tables");
    }
    for (uint32_t i = 0; i < bots->client_capacity; ++i) {
        bool occupied = !reading && bots->clients[i] != NULL;
        B(occupied);
        if (!occupied) continue;
        if (reading && !(bots->clients[i] = calloc(1, sizeof(*bots->clients[i]))))
            return bot_save_fail(io, QA_ERROR_MEMORY, "Restoring native bot continuation");
        if (!state_fields(io, bots->clients[i])) return false;
    }
    for (uint32_t i = 0; i < bots->actor_capacity; ++i) U(bots->actor_clients[i]);
    return snapshot_fields(io, &bots->entities) && snapshot_fields(io, &bots->players);
}
bool qa_bots_population_capture(const qa_bots *bots, qa_buffer *out, qa_error *error)
{
    if (!bots || !out || bots->restore_pending || !qa_bots_can_destroy(bots) ||
        !qa_bot_runtime_can_destroy(bots->runtime) || bots->runtime->restore_pending || !topology(bots, error)) return false;
    qa_source_save_io io = {0}; qa_bots view = *bots;
    bool ok = qa_source_save_writer(&io, bots->services.shared.session, error) && bot_save_signature(&io, magic) &&
        fields(&io, &view) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_bots_population_restore(qa_bots *bots, qa_bytes bytes, qa_error *error)
{
    if (!bots || !bots->restore_pending || !qa_bots_can_destroy(bots) || bots->count ||
        !qa_bot_runtime_can_destroy(bots->runtime) || bots->runtime->restore_pending) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native bot population restore requires its detached prepared owner"); return false;
    }
    for (uint32_t i = 0; i < bots->client_capacity; ++i) if (bots->clients[i]) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Prepared bot population already owns continuation records"); return false;
    }
    qa_bots scratch = {.runtime = bots->runtime, .services = bots->services}; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, bots->services.shared.session, bytes, error) && bot_save_signature(&io, magic) &&
        fields(&io, &scratch) && scratch.client_capacity == bots->client_capacity &&
        qa_source_save_finish(&io, NULL) && topology(&scratch, error);
    if (ok) { qa_bots old = *bots; *bots = scratch; clear(&old); }
    else clear(&scratch);
    if (!ok && (!error || error->code == QA_OK)) qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid complete bot population continuation");
    qa_source_save_dispose(&io); return ok;
}
