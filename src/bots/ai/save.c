#include "internal.h"
#include "source_event_state.h"
#include "source_activation.h"
#include "../runtime/internal.h"
#include "../save_fields.h"
#include "qa/bots_population_save.h"

static const uint8_t magic[8] = {'Q', 'A', 'B', 'P', 'O', 'P', 'U', 0};
static bool signature(qa_source_save_io *io)
{
    uint8_t actual[8];memcpy(actual,magic,sizeof(actual));
    return qa_source_save_bytes(io,actual,sizeof(actual)) &&
        (!memcmp(actual,magic,sizeof(actual))?true:
            bot_save_fail(io,QA_ERROR_FORMAT,"Invalid native bot population continuation signature"));
}
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
static bool view_fields(qa_source_save_io *io, bot_ai_view *view)
{
    uint32_t decision = view->decision, order = view->order.kind, status = view->order.status;
    A(view->actor); A(view->enemy); U(view->client); I(view->source_client); I(view->entity);
    U(view->mode.slot); L(view->mode.generation);
    U(decision); U(order); U(status);
    if (decision > QA_BOT_BATTLE_NEARBY || order > QA_BOT_ORDER_FOLLOW || status > QA_BOT_ORDER_ACTIVE) return false;
    view->decision = (qa_bot_decision)decision; view->order.kind = (qa_bot_order_kind)order;
    view->order.status = (qa_bot_order_status)status;
    V(view->order.point); A(view->order.target); return true;
}
static bool player_fields(qa_source_save_io *io, bot_ai_player *player)
{
    B(player->connected); B(player->observer); B(player->intermission); B(player->dead);
    B(player->grounded); B(player->crouched); B(player->teleported); B(player->water_jump);
    B(player->grapple_pull); B(player->firing); B(player->invisible); B(player->chatting); B(player->carrying_objective);
    A(player->last_attacker); A(player->last_victim); I(player->deaths); I(player->kills); I(player->last_damage_cause);
    F(player->air_time); F(player->teleport_time); L(player->spawn_sequence); L(player->teleport_sequence);
    return true;
}
static bool waypoint_fields(qa_source_save_io *io,const bot_source_orders_state *owner,
                             bot_source_orders_state *target,bot_source_waypoint **point) {
    int32_t index=-1;
    if(io->direction==QA_SOURCE_SAVE_WRITE && !bot_ai_source_waypoint_index(owner,*point,&index))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Waypoint reference escapes its actual AI bank");
    I(index);
    if(index<-1 || index>=BOT_SOURCE_WAYPOINTS)
        return bot_save_fail(io,QA_ERROR_FORMAT,"Waypoint continuation reference is outside its AI bank");
    if(io->direction==QA_SOURCE_SAVE_READ) *point=index<0?NULL:&target->points[index];
    return true;
}
static bool source_order_fields(qa_source_save_io *io,const bot_source_orders_state *owner,
                                 bot_source_orders_state *target,bot_source_order_state *order) {
    return waypoint_fields(io,owner,target,&order->checkpoints) &&
        waypoint_fields(io,owner,target,&order->patrol_points) &&
        waypoint_fields(io,owner,target,&order->current_patrol_point);
}
static bool source_orders_fields(qa_source_save_io *io,bot_source_orders_state *orders,
                                  const bot_source_orders_state *owner) {
    if(!waypoint_fields(io,owner,orders,&orders->free_point)) return false;
    I(orders->find_client_maxclients);I(orders->find_enemy_maxclients);
    I(orders->same_team_maxclients);I(orders->client_name_maxclients);I(orders->team_name_maxclients);
    for(size_t i=0;i<BOT_SOURCE_WAYPOINTS;++i) {
        bot_source_waypoint *point=&orders->points[i];B(point->inuse);
        if(!qa_source_save_bytes(io,point->name,sizeof(point->name)) ||
           !memchr(point->name,0,sizeof(point->name)) || !goal_fields(io,&point->goal)) return false;
        if(!waypoint_fields(io,owner,orders,&point->next) || !waypoint_fields(io,owner,orders,&point->prev)) return false;
    }
    return true;
}
static bool source_policy_globals_fields(qa_source_save_io *io,bot_source_team_policy_globals *policy) {
    I(policy->num_team_mates_maxclients);I(policy->sort_team_mates_maxclients);I(policy->team_orders_maxclients);
    B(policy->routes_setup);
    if(!qa_source_save_count(io,&policy->red_route_count,BOT_SOURCE_ALTERNATE_ROUTES) ||
       !qa_source_save_count(io,&policy->blue_route_count,BOT_SOURCE_ALTERNATE_ROUTES)) return false;
    for(size_t i=0;i<BOT_SOURCE_ALTERNATE_ROUTES;++i) {
        qa_bot_alternative_goal *routes[]={&policy->red_routes[i],&policy->blue_routes[i]};
        for(size_t j=0;j<2;++j) {
            qa_bot_alternative_goal *route=routes[j];V(route->origin);U(route->area);U(route->start_time);U(route->goal_time);
            uint32_t extra=route->extra_time;U(extra);if(extra>UINT16_MAX) return false;route->extra_time=(uint16_t)extra;
        }
    }
    return true;
}
static bool state_fields(qa_source_save_io *io, bot_ai_state *state,const bot_source_orders_state *owner,
                          bot_source_orders_state *target)
{
    B(state->inuse);B(state->counted);
    bot_source_setup_state *setup=&state->source_setup;
    if(!qa_source_save_bytes(io,setup->team,sizeof(setup->team)) || !memchr(setup->team,0,sizeof(setup->team))) return false;
    B(setup->map_restart);
    uint32_t setup_kind=setup->progress.kind;U(setup_kind);
    if(setup_kind>BOT_SOURCE_SETUP_COMPLETE) return false;
    setup->progress.kind=(bot_source_setup_kind)setup_kind;
    if(setup_kind==BOT_SOURCE_SETUP_SETTING_UP) {
        uint32_t stage=setup->progress.value.stage;U(stage);
        if(stage>BOT_SOURCE_SETUP_SESSION) return false;
        setup->progress.value.stage=(bot_source_setup_stage)stage;
    } else if(setup_kind==BOT_SOURCE_SETUP_FAILED) {
        uint32_t failure=setup->progress.value.failure.stage;U(failure);
        if(failure>BOT_SOURCE_SETUP_FAILED_CHAT_FILE) return false;
        setup->progress.value.failure.stage=(bot_source_setup_failure)failure;
        I(setup->progress.value.failure.error);
    }
    if (!view_fields(io, &state->view) || !player_fields(io, &state->player)) return false;
    U(state->character); U(state->goals); U(state->weapons); U(state->chat); U(state->movement);
    F(state->admitted_skill);
    const char *character=state->admitted_character,*name=state->admitted_name;
    if (!bot_save_text(io,&character)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) state->admitted_character=(char *)character;
    if (!bot_save_text(io,&name)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) state->admitted_name=(char *)name;
    if (state->view.actor.registry && (!character || !name)) return false;

    F(state->state_time);
    B(state->team_arena); B(state->retired);
    L(state->command_sequence);

    if(!source_order_fields(io,owner,target,&state->source_order)) return false;
    uint32_t phase=state->shutdown_phase;U(phase);if(phase>BOT_SHUTDOWN_FAILED) return false;
    state->shutdown_phase=(bot_shutdown_phase)phase;
    B(state->shutdown_restart); B(state->shutdown_chat_pending);
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
static bool waypoint_chain(const bot_source_orders_state *orders,const bot_source_waypoint *point,
                            bool seen[BOT_SOURCE_WAYPOINTS]) {
    while(point) {
        int32_t index=-1;
        if(!bot_ai_source_waypoint_index(orders,point,&index) || index<0 || seen[index]) return false;
        seen[index]=true;point=point->next;
    }
    return true;
}
static bool topology(const qa_bots *bots, qa_error *error)
{
    if (bots->client_capacity > INT32_MAX || bots->count > bots->client_capacity ||
        (bots->client_capacity && !bots->clients) || (bots->actor_capacity && !bots->actor_clients) ||
        bots->actor_capacity > qa_actors_capacity(qa_session_actors(bots->services.shared.session))) goto invalid;
    for (size_t i = 0; i < BOT_SOURCE_MATCH_CVARS; ++i)
        if (!bots->source_match.cvars[i].registered) goto invalid;
    if(bots->shutdown_actor.registry && (!bots->shutting_down ||
       !bot_ai_actor(bots,bots->shutdown_actor) || !bot_ai_live(bots,bots->shutdown_actor))) goto invalid;
    bool waypoint_seen[BOT_SOURCE_WAYPOINTS]={0};
    int32_t waypoint=-1;
    for(size_t i=0;i<BOT_SOURCE_WAYPOINTS;++i) {
        const bot_source_waypoint *point=&bots->source_orders.points[i];
        if(!bot_ai_source_waypoint_index(&bots->source_orders,point->next,&waypoint) ||
           !bot_ai_source_waypoint_index(&bots->source_orders,point->prev,&waypoint) ||
           !memchr(point->name,0,sizeof(point->name))) goto invalid;
    }
    if(!waypoint_chain(&bots->source_orders,bots->source_orders.free_point,waypoint_seen) ||
       bots->source_team_policy.red_route_count>BOT_SOURCE_ALTERNATE_ROUTES ||
       bots->source_team_policy.blue_route_count>BOT_SOURCE_ALTERNATE_ROUTES) goto invalid;
    uint32_t count = 0;
    for (uint32_t source = 0; source < 64; ++source) {
        const bot_ai_state *state = bots->source_cells[source]; if (!state) continue;
        if(state->acquired_source_client!=source) goto invalid;
        bot_ai_state actual=*state;
        if(!actual.source_span.data && !qa_bot_source_record_span(&bots->services.memory,
            actual.source_record,&actual.source_span,error)) goto invalid;
        if(!bot_ai_activation_validate(&actual,error)) goto invalid;
        if(!bot_ai_source_waypoint_index(&bots->source_orders,state->source_order.current_patrol_point,&waypoint) ||
           !waypoint_chain(&bots->source_orders,state->source_order.checkpoints,waypoint_seen) ||
           !waypoint_chain(&bots->source_orders,state->source_order.patrol_points,waypoint_seen)) goto invalid;
        if(!state->view.actor.registry) {
            if(state->view.actor.slot || state->view.actor.generation || state->inuse || state->counted ||
               state->character || state->goals || state->weapons || state->chat || state->movement ||
               state->admitted_character || state->admitted_name ||
               state->source_setup.progress.kind!=BOT_SOURCE_SETUP_EMPTY || bots->source_clients[source]) goto invalid;
            continue;
        }
        uint32_t i=state->view.client;
        if (i>=bots->client_capacity || bots->clients[i]!=state || state->view.actor.slot >= bots->actor_capacity ||
            bots->actor_clients[state->view.actor.slot] != i + 1 ||
            state->view.entity < 0 ||
            state->view.source_client!=(int32_t)source ||
            (state->inuse && bots->source_clients[state->view.source_client]!=i+1) ||
            (!state->inuse && bots->source_clients[state->view.source_client]==i+1) ||
            !isfinite(state->admitted_skill) ||
            !state->admitted_character || !state->admitted_name ||
            state->view.decision > QA_BOT_BATTLE_NEARBY || state->view.order.kind > QA_BOT_ORDER_FOLLOW ||
            state->view.order.status > QA_BOT_ORDER_ACTIVE ||
            !memchr(state->name, 0, sizeof(state->name)) || (!state->retired && !bot_ai_live(bots, state->view.actor))) goto invalid;
        if(state->shutdown_phase>BOT_SHUTDOWN_FAILED ||
           (state->shutdown_phase!=BOT_SHUTDOWN_RUNNING && (!bots->shutting_down ||
            state->shutdown_restart!=bots->shutdown_restart ||
            (bots->shutdown_actor.registry && !qa_actor_id_equal(bots->shutdown_actor,state->view.actor))))) goto invalid;
        if(bot_ai_num_prox_mines(state)<0 || bot_ai_num_prox_mines(state)>BOT_SOURCE_PROX_MINES) goto invalid;
        if(!memchr(state->source_setup.team,0,sizeof(state->source_setup.team))) goto invalid;
        const bot_source_setup_progress *setup=&state->source_setup.progress;
        if(setup->kind>BOT_SOURCE_SETUP_COMPLETE || setup->kind==BOT_SOURCE_SETUP_EMPTY) goto invalid;
        bool published=setup->kind==BOT_SOURCE_SETUP_COMPLETE ||
            (setup->kind==BOT_SOURCE_SETUP_SETTING_UP && setup->value.stage>=BOT_SOURCE_SETUP_PUBLISHED);
        bool counted=setup->kind==BOT_SOURCE_SETUP_COMPLETE ||
            (setup->kind==BOT_SOURCE_SETUP_SETTING_UP && setup->value.stage>=BOT_SOURCE_SETUP_COUNTED);
        if(state->inuse!=published || state->counted!=counted ||
           (setup->kind==BOT_SOURCE_SETUP_SETTING_UP && setup->value.stage>BOT_SOURCE_SETUP_SESSION) ||
           (setup->kind==BOT_SOURCE_SETUP_FAILED && setup->value.failure.stage>BOT_SOURCE_SETUP_FAILED_CHAT_FILE)) goto invalid;
        if(state->counted) ++count;
        if (!qa_bot_runtime_closed(bots->runtime)) {
            if ((!state->retired && state->inuse && state->shutdown_phase!=BOT_SHUTDOWN_DONE &&
                 (!state->character || !state->goals || !state->weapons || !state->chat)) ||
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
            for (uint32_t j = 0; j < source; ++j) {
                const bot_ai_state *other = bots->source_cells[j]; if (!other) continue;
                if ((state->goals && state->goals == other->goals) || (state->weapons && state->weapons == other->weapons) ||
                    (state->chat && state->chat == other->chat) || (state->movement && state->movement == other->movement)) goto invalid;
            }
        }
    }
    if (count != bots->count) goto invalid;
    for(uint32_t i=0;i<bots->client_capacity;++i) {
        const bot_ai_state *state=bots->clients[i];if(!state) continue;
        if(!state->view.actor.registry || state->view.client!=i || state->acquired_source_client>=64 ||
           bots->source_cells[state->acquired_source_client]!=state) goto invalid;
    }
    for(size_t i=0;i<64;++i) {
        uint32_t client=bots->source_clients[i];if(!client) continue;
        if(client>bots->client_capacity || !bots->clients[client-1] ||
           !bots->clients[client-1]->inuse || bots->clients[client-1]->view.source_client!=(int32_t)i) goto invalid;
    }
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
    for (uint32_t i = 0; i < 64; ++i) {
        if (bots->source_cells[i]) {
            free(bots->source_cells[i]->admitted_character);free(bots->source_cells[i]->admitted_name);
        }
        free(bots->source_cells[i]);
    }
    free(bots->clients); free(bots->actor_clients);
    qa_builtin_snapshot_free(&bots->entities); qa_builtin_snapshot_free(&bots->players);
}
static bool fields(qa_source_save_io *io, qa_bots *bots,const bot_source_orders_state *waypoints)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    U(bots->client_capacity); U(bots->actor_capacity); U(bots->count);
    for(size_t i=0;i<64;++i) U(bots->source_clients[i]);
    if (bots->client_capacity > INT32_MAX || (bots->client_capacity && SIZE_MAX / bots->client_capacity < sizeof(*bots->clients)) ||
        (bots->actor_capacity && SIZE_MAX / bots->actor_capacity < sizeof(*bots->actor_clients)) || bots->count > bots->client_capacity) return false;
    if (!controls_fields(io, &bots->controls)) return false;
    I(bots->local_time_ms); I(bots->library_residual_ms); I(bots->scheduled_think_ms);
    F(bots->time); F(bots->regular_update_time); L(bots->command_sequence);
    for(size_t i=0;i<BOT_SOURCE_MATCH_CVARS;++i) {
        bot_source_match_cvar *cell=&bots->source_match.cvars[i];
        if(!qa_source_save_bytes(io,cell->value,sizeof(cell->value)) || !memchr(cell->value,0,sizeof(cell->value))) return false;
        F(cell->numeric_value);I(cell->integer_value);L(cell->modification_count);B(cell->registered);
    }
    I(bots->source_match.interbreed_match_count);B(bots->source_match.interbreed);
    I(bots->source_chat.active_maxclients);I(bots->source_chat.first_maxclients);I(bots->source_chat.last_maxclients);
    I(bots->source_chat.first_name_maxclients);I(bots->source_chat.last_name_maxclients);I(bots->source_chat.opponent_maxclients);
    B(bots->shutting_down); B(bots->shutdown_restart);A(bots->shutdown_actor);
    for(size_t i=0;i<64;++i) {
        if(!qa_source_save_bytes(io,bots->team_preferences[i].name,sizeof(bots->team_preferences[i].name)) ||
           !memchr(bots->team_preferences[i].name,0,sizeof(bots->team_preferences[i].name))) return false;
        I(bots->team_preferences[i].preference);B(bots->not_leader[i]);
    }
    bot_source_goals *source=&bots->source_goals;
    if(!goal_fields(io,&source->red_flag) || !goal_fields(io,&source->blue_flag) ||
       !goal_fields(io,&source->neutral_flag) || !goal_fields(io,&source->red_obelisk) ||
       !goal_fields(io,&source->blue_obelisk) || !goal_fields(io,&source->neutral_obelisk)) return false;
    I(source->game_type);I(source->max_clients);I(source->max_bsp_model_index);
    if(!source_orders_fields(io,&bots->source_orders,waypoints) || !source_policy_globals_fields(io,&bots->source_team_policy)) return false;
    V(bots->source_event_globals.last_teleport_origin);F(bots->source_event_globals.last_teleport_time);
    for (size_t i = 0; i < QA_BOT_INVENTORY_SIZE; ++i) I(bots->inventory_scratch[i]);
    if (reading) {
        size_t remaining = io->input.size - io->offset;
        if (remaining < 64 || bots->count > (remaining - 64) / 1280 ||
            bots->actor_capacity > qa_actors_capacity(qa_session_actors(bots->services.shared.session)))
            return bot_save_fail(io, QA_ERROR_FORMAT, "Truncated native bot population storage");
        if (bots->client_capacity) bots->clients = calloc(bots->client_capacity, sizeof(*bots->clients));
        if (bots->actor_capacity) bots->actor_clients = calloc(bots->actor_capacity, sizeof(*bots->actor_clients));
        if ((bots->client_capacity && !bots->clients) || (bots->actor_capacity && !bots->actor_clients))
            return bot_save_fail(io, QA_ERROR_MEMORY, "Restoring actual bot client/actor tables");
    }
    for (uint32_t i = 0; i < 64; ++i) {
        bool occupied = !reading && bots->source_cells[i] != NULL;
        B(occupied);
        if (!occupied) continue;
        if (reading && !(bots->source_cells[i] = calloc(1, sizeof(*bots->source_cells[i]))))
            return bot_save_fail(io, QA_ERROR_MEMORY, "Restoring native bot continuation");
        bot_ai_state *state=bots->source_cells[i];
        if(reading) state->acquired_source_client=i;
        if(!qa_bot_source_record_fields(io,&bots->services.memory,&state->source_record) ||
           !bot_ai_source_alias_bind(bots,state,io->error)) return false;
        if (!state_fields(io,state,waypoints,&bots->source_orders)) return false;
        if(reading && state->view.actor.registry) {
            if(state->view.client>=bots->client_capacity || bots->clients[state->view.client]) return false;
            bots->clients[state->view.client]=state;
        }
    }
    for (uint32_t i = 0; i < bots->actor_capacity; ++i) U(bots->actor_clients[i]);
    return snapshot_fields(io, &bots->entities) && snapshot_fields(io, &bots->players);
}
bool qa_bots_population_capture(const qa_bots *bots, qa_buffer *out, qa_error *error)
{
    if (!bots || !out || bots->restore_pending || !qa_bots_can_destroy(bots) ||
        !qa_bot_runtime_can_destroy(bots->runtime) || bots->runtime->restore_pending || !topology(bots, error)) return false;
    qa_source_save_io io = {0}; qa_bots view = *bots;
    bool ok = qa_source_save_writer(&io, bots->services.shared.session, error) && signature(&io) &&
        fields(&io,&view,&bots->source_orders) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_bots_population_restore(qa_bots *bots, qa_bytes bytes, qa_error *error)
{
    if (!bots || !bots->restore_pending || !qa_bots_can_destroy(bots) || bots->count ||
        !qa_bot_runtime_can_destroy(bots->runtime) || bots->runtime->restore_pending) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native bot population restore requires its detached prepared owner"); return false;
    }
    for (uint32_t i = 0; i < 64; ++i) if (bots->source_cells[i]) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Prepared bot population already owns continuation records"); return false;
    }
    qa_bots scratch = {.runtime = bots->runtime, .services = bots->services}; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, bots->services.shared.session, bytes, error) && signature(&io) &&
        fields(&io,&scratch,&scratch.source_orders) && scratch.client_capacity == bots->client_capacity &&
        qa_source_save_finish(&io, NULL) && topology(&scratch, error);
    for(uint32_t i=0;ok && i<64;++i) if(scratch.source_cells[i])
        ok=bot_ai_source_order_rebase(&scratch.source_orders,&scratch.source_cells[i]->source_order,
            &bots->source_orders,error);
    if(ok) ok=bot_ai_source_orders_rebase(&scratch.source_orders,&scratch.source_orders,&bots->source_orders,error);
    if (ok) { bot_ai_source_match_bind(&scratch); qa_bots old = *bots; *bots = scratch; clear(&old); }
    else clear(&scratch);
    if (!ok && (!error || error->code == QA_OK)) qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid complete bot population continuation");
    qa_source_save_dispose(&io); return ok;
}
