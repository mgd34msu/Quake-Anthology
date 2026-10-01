#include "internal.h"
#include "source_report.h"

static int32_t signed_word(uint32_t bits) {
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
bool bot_ai_source_intermission(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;
    if(!s->player.source_state_available || !b->services.source_intermission)
        return bot_ai_fail(e,"bot intermission requires its actual source clock and retained player state");
    if(!b->services.source_intermission(b->services.context,out,e)) return false;
    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
    *out=*out || s->player.source_state.pmType==4 || s->player.source_state.pmType==5;
    return true;
}
bool bot_ai_source_observer(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;
    if(!s->player.source_state_available)
        return bot_ai_fail(e,"bot observer test requires its retained source player state");
    if(s->player.source_state.pmType==2) {*out=true;return true;}
    int32_t client,team;
    if(!bot_ai_source_client(b,s,&client,e) || !bot_ai_source_team(b,client,&team,e)) return false;
    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
    *out=team==3;return true;
}
static bool ready(qa_bots *b, bot_ai_state *s) {
    if (s->retired || !bot_ai_live(b, s->view.actor)) return false;
    qa_bot_navigation *navigation = qa_bot_runtime_navigation(b->runtime, (int32_t)s->view.client);
    const qa_nav_graph_view *graph = navigation ? qa_navigation_graph(qa_bot_navigation_runtime(navigation)) : NULL;
    return graph && graph->node_count &&
        qa_actor_id_equal(qa_bot_navigation_actor(navigation), s->view.actor);
}
static bool player(qa_bots *b, bot_ai_state *s, qa_error *e) {
    if (!bot_ai_live(b, s->view.actor)) { s->retired = true; return true; }
    if (!b->services.player(b->services.context, s->view.actor, &s->player, e)) return false;
    if (!bot_ai_live(b,s->view.actor)) {s->retired=true;return true;}
    if (!bot_ai_carrying(b,s,&s->player.carrying_objective,e)) return false;
    if (!bot_ai_live(b, s->view.actor)) s->retired = true;
    return true;
}
static bool connected(qa_bots *b, bot_ai_state *s) {
    if (s->retired || !bot_ai_live(b, s->view.actor)) return false;
    qa_builtin_player_info info;
    return b->services.shared.player_info(b->services.shared.context, s->view.actor, &info) &&
        bot_ai_live(b, s->view.actor) && info.connected;
}
static bool submit(qa_bots *b, bot_ai_state *s, const qa_bot_input *input, qa_error *e) {
    if (s->retired || !bot_ai_live(b, s->view.actor)) return true;
    if (s->command_sequence == UINT64_MAX) return bot_ai_fail(e, "bot command sequence exhausted");
    s->last_command.sequence = ++s->command_sequence;
    return b->services.submit(b->services.context, s->view.actor, input, &s->last_command, e);
}
bool bot_ai_input(qa_bots *b, bot_ai_state *s, int32_t time, int32_t elapsed, qa_error *e) {
    float factor = .05f, maximum = 360;
    if (s->view.enemy.registry &&
        (!bot_ai_character_float(b, s, BOT_C_VIEW_FACTOR, .01f, 1, &factor, e) ||
         !bot_ai_character_float(b, s, BOT_C_VIEW_MAX, 1, 1800, &maximum, e))) return false;
    qa_bot_view_delta(&s->angles, s->player.delta_angles, true);
    qa_bot_change_view(&s->angles, factor, maximum, (float)elapsed / 1000, b->controls.challenge);
    qa_bot_actions *actions = qa_bot_runtime_actions(b->runtime);
    qa_bot_input input;
    bool ok = qa_bot_actions_view(actions, s->view.client, s->angles.angles, e) &&
        qa_bot_actions_input(actions, s->view.client, (float)time / 1000, &input, e);
    if (ok && (input.action_flags & QA_BOT_RESPAWN) && (s->last_command.buttons & 1))
        input.action_flags &= ~(QA_BOT_RESPAWN | QA_BOT_ATTACK);
    if (ok) ok = qa_bot_input_q3_command(&input, s->player.delta_angles, time, &s->last_command, e);
    qa_bot_view_delta(&s->angles, s->player.delta_angles, false);
    return ok && submit(b, s, &input, e);
}
bool bot_ai_point_area(qa_bots *b, bot_ai_state *s, qa_vec3 origin, uint32_t *area, qa_error *e) {
    qa_bot_navigation *navigation = qa_bot_runtime_navigation(b->runtime, (int32_t)s->view.client);
    if (!navigation) return bot_ai_fail(e, "bot navigation is unavailable");
    if (!qa_bot_navigation_point(navigation, origin, area, e)) return false;
    if (*area) return true;
    qa_vec3 end = origin; end.z += 10;
    qa_aas_crossing crossing[10]; size_t count;
    if (!qa_bot_navigation_trace_areas(navigation, origin, end, crossing, 10, &count, e)) return false;
    *area = count ? crossing[0].area : 0;
    return true;
}
bool bot_ai_think(qa_bots *b, bot_ai_state *s, float elapsed, qa_error *e) {
    qa_bot_actions *actions = qa_bot_runtime_actions(b->runtime);
    int32_t old_inventory[QA_BOT_INVENTORY_SIZE];
    memcpy(old_inventory,s->player.inventory,sizeof(old_inventory));
    if (!qa_bot_actions_reset(actions, s->view.client, e) || !player(b, s, e)) return false;
    if (s->retired) return true;
    memcpy(s->player.inventory,old_inventory,sizeof(old_inventory));
    if (!bot_ai_console(b, s, e)) return false;
    if (s->retired || !bot_ai_live(b, s->view.actor)) return true;
    qa_bot_view_delta(&s->angles, s->player.delta_angles, true);
    s->local_time += elapsed;
    s->view.think_time = elapsed;
    bool ok = bot_ai_point_area(b, s, s->player.origin, &s->area, e);
    bool setup_ready=true;
    if(ok) ok=bot_ai_source_setup_frame(b,s,&setup_ready,e);
    if(ok && (!setup_ready || s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
    if (ok && s->setup_count<=0) {
        bool intermission,observer;
        ok=bot_ai_source_intermission(b,s,&intermission,e);
        if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
        if (ok && !intermission) {
            ok=bot_ai_source_set_teleport_time(b,s,e) &&
                b->services.inventory(b->services.context,s->view.actor,&s->player,s->player.inventory,e);
            if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
            if(ok) ok=bot_ai_source_task_preference(b,s,old_inventory,e);
            if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
            if(ok) ok=bot_ai_source_check_snapshot(b,s,e);
            if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
            qa_bot_navigation *navigation = qa_bot_runtime_navigation(b->runtime, (int32_t)s->view.client);
            int32_t contents;
            if(ok) ok = navigation && qa_bot_navigation_contents(navigation, s->player.eye, &contents, e);
            if (ok && (s->player.inventory[QA_BOT_INV_ENVIRO] > 0 || !(contents & (8 | 16 | 32))))
                s->last_air_time = b->time;
        }
        if (ok) ok = bot_ai_messages(b, s, e);
        if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
        if(ok) ok=bot_ai_source_intermission(b,s,&intermission,e);
        if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
        if(ok && !intermission) ok=bot_ai_source_observer(b,s,&observer,e);
        if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
        if(ok && !intermission && !observer) ok=bot_ai_source_team_policy(b,s,e);
        if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
        if(ok && !s->source_chat.enter_game_chat && s->view.enter_time>b->time-8) {
            bool chat;
            ok=bot_ai_source_chat_enter_game(b,s,&chat,e);
            if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
            if(ok && chat) {
                float duration;ok=bot_ai_source_chat_time(b,s,&duration,e);
                if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
                if(ok) {
                    s->stand_until=b->time+duration;s->stand_enemy_time=b->time+1;
                    s->view.decision=QA_BOT_STANDING;s->state_time=b->time;
                }
            }
            if(ok) s->source_chat.enter_game_chat=true;
        }
        if (ok) ok = bot_ai_decide(b, s, e);
        if(ok && !s->retired && bot_ai_live(b,s->view.actor)) {
            s->source_chat.last_frame_health=s->player.inventory[QA_BOT_INV_HEALTH];
            if(!s->player.source_state_available) ok=bot_ai_fail(e,"bot frame lacks its retained source player state");
            else s->source_chat.last_hit_count=s->player.source_state.persistant[1];
        }
    }
finished:
    if (ok && !s->retired && bot_ai_live(b,s->view.actor))
        ok = qa_bot_actions_weapon(actions, s->view.client, s->view.weapon, e);
    qa_bot_view_delta(&s->angles, s->player.delta_angles, false);
    return ok;
}
static bool observations(qa_bots *b, qa_error *e) {
    uint32_t extent;
    if (!b->services.entity_extent(b->services.context,&extent,e) ||
        !qa_bot_runtime_invalidate_entity_range(b->runtime,0,extent,e) ||
        !b->services.entity_list(b->services.context,&b->entities,e) ||
        !qa_builtin_players(&b->services.shared, &b->players, e)) return false;
    for (size_t i = 0; i < b->entities.count; ++i) {
        qa_actor_id actor = b->entities.ids[i];
        if (!bot_ai_live(b, actor)) continue;
        qa_bot_entity entity;
        if (!b->services.entity(b->services.context, actor, &entity, e)) return false;
        if (!bot_ai_live(b, actor)) continue;
        bool visible = entity.present && entity.linked && !entity.hidden &&
            (!entity.missile || entity.grapple) && !entity.temporary_event && !entity.proximity_trigger;
        if (!qa_bot_runtime_update_entity(b->runtime, entity.number,
                                             visible ? &entity.observation : NULL, e)) return false;
    }
    return true;
}
static bool retire_pending(qa_bots *b, qa_error *e) {
    qa_error first = {0};
    for (uint32_t i = 0; i < b->client_capacity; ++i) {
        bot_ai_state *s = b->clients[i];
        if (!s || (!s->retired && bot_ai_live(b, s->view.actor))) continue;
        qa_error local = {0};
        bool published=s->inuse;
        if (!bot_ai_cleanup(b, s, &local)) {if(!first.code) first=local;continue;}
        if(published && !qa_bot_source_record_clear(&b->services.memory,s->source_record,false,&local)) {
            if(!first.code) first=local;continue;
        }
        bot_ai_source_cell_clear(b,s);
    }
    if (first.code && e) *e = first;
    return !first.code;
}
static bool frame(qa_bots *b, int32_t time, qa_error *e) {
    if (!bot_ai_source_frame_cvars(b,e)) return false;
    if (b->controls.report && !bot_ai_source_report(b,e)) return false;
    if (b->controls.paused) {
        for (uint32_t i = 0; i < 64; ++i) {
            bot_ai_state *s = b->source_clients[i]?b->clients[b->source_clients[i]-1]:NULL;
            if (!s || !connected(b, s)) continue;
            s->last_command.forward_move = s->last_command.side_move = s->last_command.up_move = 0;
            s->last_command.buttons = 0;
            s->last_command.server_time_ms = time;
            qa_bot_input input = {.view_angles = s->angles.angles, .weapon = s->view.weapon};
            if (!submit(b, s, &input, e)) return false;
        }
        return true;
    }
    if(!bot_ai_source_frame_requests(b,e) || !bot_ai_source_interbreeding(b,e)) return false;
    int32_t period = b->controls.think_time_ms;
    if (!bot_ai_source_frame_limit_think(b,e)) return false;
    if (period != b->scheduled_think_ms) {
        b->scheduled_think_ms=period;if(!bot_ai_schedule(b,e)) return false;
    }
    int32_t elapsed = signed_word((uint32_t)time - (uint32_t)b->local_time_ms);
    b->local_time_ms = time;
    b->library_residual_ms = signed_word((uint32_t)b->library_residual_ms + (uint32_t)elapsed);
    int32_t think = elapsed > period ? elapsed : period;
    if (b->library_residual_ms >= think) {
        b->library_residual_ms = signed_word((uint32_t)b->library_residual_ms - (uint32_t)think);
        if (!qa_bot_runtime_start_frame(b->runtime, (float)time / 1000, e) || !observations(b, e)) return false;
        if (b->regular_update_time < b->time) {
            if (!qa_bot_goals_update_items(qa_bot_runtime_goals(b->runtime),
                                              qa_bot_runtime_navigation(b->runtime, -1), e)) return false;
            b->regular_update_time = b->time + .3f;
        }
    }
    b->time = qa_bot_runtime_time(b->runtime);
    for (uint32_t i = 0; i < 64; ++i) {
        bot_ai_state *s = b->source_clients[i]?b->clients[b->source_clients[i]-1]:NULL;
        if (!s || s->retired) continue;
        s->residual_ms = signed_word((uint32_t)s->residual_ms + (uint32_t)elapsed);
        if (s->residual_ms < think) continue;
        s->residual_ms = signed_word((uint32_t)s->residual_ms - (uint32_t)think);
        if (!ready(b, s)) continue;
        if (connected(b, s) && !bot_ai_think(b, s, (float)think / 1000, e)) return false;
    }
    for (uint32_t i = 0; i < 64; ++i) {
        bot_ai_state *s = b->source_clients[i]?b->clients[b->source_clients[i]-1]:NULL;
        if (!s || s->retired) continue;
        if (connected(b, s) && !bot_ai_input(b, s, time, elapsed, e)) return false;
    }
    return true;
}
bool qa_bots_frame(qa_bots *b, int32_t time, qa_error *e) {
    if (!bot_ai_mutable(b, e)) return false;
    if (b->checking_spawn) return bot_ai_fail(e,"bot frame reentered its spawn callback");
    if (!qa_bot_runtime_lease_begin(b->runtime,e)) return false;
    b->checking_spawn=true;
    bool spawn_ok=!b->services.check_spawn || b->services.check_spawn(b->services.context,e);
    b->checking_spawn=false;
    if(!spawn_ok) {qa_bot_runtime_lease_end(b->runtime);return false;}
    b->busy = true;
    bool ok = frame(b, time, e);
    qa_error cleanup = {0};
    if (!retire_pending(b, &cleanup) && ok) { ok = false; if (e) *e = cleanup; }
    b->busy = false;
    qa_bot_runtime_lease_end(b->runtime);
    return ok;
}
