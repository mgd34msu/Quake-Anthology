#include "internal.h"
#include "source_storage.h"
#include "source_library.h"
#include <stdio.h>

bool bot_ai_fail(qa_error *e, const char *message) {
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
bool bot_ai_live(const qa_bots *b, qa_actor_id actor) {
    return qa_actors_get(qa_session_actors(b->services.shared.session), actor) != NULL;
}
bot_ai_state *bot_ai_actor(const qa_bots *b, qa_actor_id actor) {
    if (!b || actor.slot >= b->actor_capacity) return NULL;
    uint32_t index = b->actor_clients[actor.slot];
    bot_ai_state *state = index ? b->clients[index - 1] : NULL;
    return state && qa_actor_id_equal(state->view.actor, actor) ? state : NULL;
}
bool bot_ai_mutable(qa_bots *b, qa_error *e) {
    return b && !b->busy && !b->source_match_exit_depth && !b->restore_pending && !b->shutting_down && !qa_bot_runtime_closed(b->runtime) ? true :
        bot_ai_fail(e, "bot population is absent, closed, restoring or executing a callback");
}
bool bot_ai_context_create(qa_bot_runtime *runtime, const qa_bot_services *services,
                    uint32_t client_capacity, qa_bots **out, qa_error *e) {
    if (!runtime || !services || !out || !services->shared.session ||
        !services->shared.world || !services->shared.combat || !services->shared.player_info || !services->player ||
        !services->inventory || !services->entity || !services->entity_extent || !services->entity_list || !services->arsenal || !services->arsenal_end ||
        !services->submit || !services->random || !services->source_client || !services->source_actor ||
        !services->memory.allocate || !services->memory.read || !services->memory.write || !services->memory.borrow_span)
        return bot_ai_fail(e, "native bot population requires live shared gameplay and botlib services");
    if (client_capacity > INT32_MAX || (client_capacity && SIZE_MAX / client_capacity < sizeof(bot_ai_state *)))
        return bot_ai_fail(e, "native bot client capacity exceeds its source memory extent");
    qa_bots *b = calloc(1, sizeof(*b));
    if (!b) { qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating native bot population"); return false; }
    b->runtime = runtime;
    b->services = *services;
    b->client_capacity = client_capacity;
    b->actor_capacity = qa_actors_capacity(qa_session_actors(services->shared.session));
    b->clients = calloc(b->client_capacity, sizeof(*b->clients));
    b->actor_clients = calloc(b->actor_capacity, sizeof(*b->actor_clients));
    if ((b->client_capacity && !b->clients) || (b->actor_capacity && !b->actor_clients)) {
        free(b->clients); free(b->actor_clients); free(b);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating retained bot actor/client lookup");
        return false;
    }
    b->controls = (qa_bot_controls){.think_time_ms = 100, .rocket_jump = true};
    b->scheduled_think_ms = 100;
    b->time = qa_bot_runtime_time(runtime);
    bot_ai_source_chat_globals_init(&b->source_chat);
    bot_ai_source_match_init(&b->source_match);
    *out = b;
    return true;
}
bool bot_ai_source_setup_cvars(qa_bots *b,qa_error *e) {
    if(!b->services.register_cvar) return bot_ai_fail(e,"bot setup requires its actual source cvar registration owner");
    static const struct {const char *name,*value;uint32_t flags;} cvars[]={
        {"bot_thinktime","100",QA_CVAR_CHEAT},{"bot_memorydump","0",QA_CVAR_CHEAT},
        {"bot_saveroutingcache","0",QA_CVAR_CHEAT},{"bot_pause","0",QA_CVAR_CHEAT},
        {"bot_report","0",QA_CVAR_CHEAT},{"bot_testsolid","0",QA_CVAR_CHEAT},
        {"bot_testclusters","0",QA_CVAR_CHEAT},{"bot_developer","0",QA_CVAR_CHEAT},
        {"bot_interbreedchar","",0},{"bot_interbreedbots","10",0},
        {"bot_interbreedcycle","20",0},{"bot_interbreedwrite","",0}
    };
    for(size_t i=0;i<sizeof(cvars)/sizeof(*cvars);++i)
        if(!b->services.register_cvar(b->services.context,cvars[i].name,cvars[i].value,cvars[i].flags,e) ||
           !bot_ai_source_match_register(b,cvars[i].name,e)) return false;
    return true;
}
static bool source_setup(qa_bots *b,qa_error *e) {
    if(!qa_bot_runtime_lease_begin(b->runtime,e)) return false;
    b->busy=true;
    bool okay=bot_ai_source_setup_cvars(b,e) && bot_ai_source_goals_load(b,e) &&
        bot_ai_source_match_setup(b,e);
    b->busy=false;qa_bot_runtime_lease_end(b->runtime);return okay;
}
bool qa_bots_create_round(qa_bot_runtime *runtime, const qa_bot_services *services,
                         qa_bots **out, qa_error *e) {
    if (!runtime || !qa_bot_runtime_initialized(runtime) || !qa_bot_runtime_loaded(runtime) ||
        !out || *out || !qa_bot_runtime_can_destroy(runtime))
        return bot_ai_fail(e, "new round AI requires an idle retained initialized bot library");
    if (!bot_ai_context_create(runtime, services, qa_bot_actions_capacity(qa_bot_runtime_actions(runtime)), out, e)) return false;
    (*out)->time = 0;
    (*out)->scheduled_think_ms = 0;
    if(source_setup(*out,e)) return true;
    qa_bots_destroy(*out,NULL);*out=NULL;return false;
}
bool qa_bots_admission_read(const qa_bots *b, qa_actor_id actor,
                            qa_bot_admission *out, qa_error *e) {
    bot_ai_state *state = bot_ai_actor(b, actor);
    const qa_bot_character_view *character = state ?
        qa_bot_character_read(qa_bot_runtime_character(b->runtime, state->character)) : NULL;
    if (!out || !state || !state->inuse || state->source_setup.progress.kind!=BOT_SOURCE_SETUP_COMPLETE ||
        state->retired || !bot_ai_live(b, actor) || !character ||
        !state->admitted_character || !state->admitted_name || !isfinite(state->admitted_skill) || b->busy || b->restore_pending)
        return bot_ai_fail(e, "bot admission settings require the actual idle live character owner");
    *out = (qa_bot_admission){.actor = actor, .client = state->view.client,
        .entity = state->view.entity, .character_file = state->admitted_character, .name = state->admitted_name,
        .skill = state->admitted_skill, .mode = state->view.mode, .team_arena = state->team_arena,
        .team=state->source_setup.team};
    return true;
}
bool qa_bots_create_restored(qa_bot_runtime *runtime, const qa_bot_services *services,
                            uint32_t client_capacity, qa_bots **out, qa_error *e) {
    if (!runtime || !out || *out || !qa_bot_runtime_can_destroy(runtime))
        return bot_ai_fail(e, "restored native bot population requires idle detached owners and an empty output");
    if (!bot_ai_context_create(runtime, services, client_capacity, out, e))
        return false;
    (*out)->restore_pending = true;
    return true;
}
bool bot_ai_cleanup(qa_bots *b, bot_ai_state *s, qa_error *e) {
    if (qa_bot_runtime_closed(b->runtime)) {
        bot_ai_source_order_clear(b,s);
        free(s->admitted_character);s->admitted_character=NULL;
        free(s->admitted_name);s->admitted_name=NULL;return true;
    }
    if ((s->inuse || s->movement) && !qa_bot_moves_free(qa_bot_runtime_moves(b->runtime), s->movement, e)) return false;
    s->movement = 0;
    if (s->goals && !qa_bot_goals_free(qa_bot_runtime_goals(b->runtime), s->goals, e)) return false;
    s->goals = 0;
    if (s->chat && !qa_bot_runtime_chat_free(b->runtime, s->chat, e)) return false;
    s->chat = 0;
    if (s->weapons && !qa_bot_runtime_weapon_free(b->runtime, s->weapons, e)) return false;
    s->weapons = 0;
    if (s->character && !qa_bot_runtime_character_free(b->runtime, s->character, e)) return false;
    s->character = 0;
    bot_ai_source_order_clear(b,s);
    free(s->admitted_character);s->admitted_character=NULL;
    free(s->admitted_name);s->admitted_name=NULL;
    return true;
}
bool qa_bots_can_destroy(const qa_bots *b) {
    return !b || (!b->busy && !b->source_match_exit_depth && !b->checking_spawn);
}
bool qa_bots_destroy(qa_bots *b, qa_error *e) {
    if (!b) return true;
    if (b->busy || b->source_match_exit_depth || b->checking_spawn) return bot_ai_fail(e, "bot population is executing a callback");
    if (!qa_bot_runtime_lease_begin(b->runtime,e)) return false;
    b->busy=true;
    for (uint32_t i = 0; i < 64; ++i) {
        bot_ai_state *s=b->source_cells[i];if(!s) continue;
        if (!bot_ai_cleanup(b, s, e)) {
            s->retired=true;
            b->busy=false;
            qa_bot_runtime_lease_end(b->runtime);
            return false;
        }
        bot_ai_source_cell_clear(b,s);
        free(s);b->source_cells[i]=NULL;
    }
    qa_builtin_snapshot_free(&b->entities);
    qa_builtin_snapshot_free(&b->players);
    qa_bot_runtime_lease_end(b->runtime);
    free(b->clients); free(b->actor_clients); free(b);
    return true;
}
void bot_ai_source_cell_clear(qa_bots *b,bot_ai_state *s) {
    uint32_t source=s->acquired_source_client;
    qa_bot_source_record record=s->source_record;
    if(s->view.actor.registry && s->view.actor.slot<b->actor_capacity &&
       b->actor_clients[s->view.actor.slot]==s->view.client+1)
        b->actor_clients[s->view.actor.slot]=0;
    if(s->view.client<b->client_capacity && b->clients[s->view.client]==s)
        b->clients[s->view.client]=NULL;
    b->source_clients[source]=0;
    if(s->counted) --b->count;
    *s=(bot_ai_state){.acquired_source_client=source,.source_record=record};
    bot_ai_source_order_init(&s->source_order);
    bot_ai_source_chat_init(&s->source_chat);
}
bool bot_ai_source_shutdown_client(qa_bots *b,bot_ai_state *s,bool restart,qa_error *e) {
    int32_t client;
    if(!s->inuse || s->retired || !bot_ai_live(b,s->view.actor) || !bot_ai_source_client(b,s,&client,e)) return false;
    if(s->shutdown_phase==BOT_SHUTDOWN_FAILED)
        return bot_ai_fail(e,"bot source shutdown failed after consuming its actual source state");
    if(s->shutdown_phase==BOT_SHUTDOWN_RUNNING) {
        s->shutdown_restart=restart;s->shutdown_phase=BOT_SHUTDOWN_SESSION;
    } else if(s->shutdown_restart!=restart)
        return bot_ai_fail(e,"bot source shutdown changed its original restart disposition");
    if(s->shutdown_phase==BOT_SHUTDOWN_SESSION) {
        if(restart && !bot_ai_session_write(b,s,e)) return false;
        s->shutdown_phase=BOT_SHUTDOWN_CHAT;
    }
    if(s->shutdown_phase==BOT_SHUTDOWN_CHAT) {
        s->shutdown_phase=BOT_SHUTDOWN_FAILED;
        if(!bot_ai_source_chat_exit_game(b,s,&s->shutdown_chat_pending,e)) return false;
        s->shutdown_phase=BOT_SHUTDOWN_SEND;
    }
    if(s->shutdown_chat_pending) {
        s->shutdown_phase=BOT_SHUTDOWN_FAILED;
        if(!qa_bot_chat_enter(qa_bot_runtime_chat(b->runtime,s->chat),client,QA_BOT_CHAT_ALL,e)) return false;
        s->shutdown_chat_pending=false;
    }
    s->shutdown_phase=BOT_SHUTDOWN_DONE;
    if(!bot_ai_cleanup(b,s,e) ||
       !qa_bot_source_record_clear(&b->services.memory,s->source_record,false,e)) return false;
    bot_ai_source_cell_clear(b,s);return true;
}
bool qa_bots_shutdown_client(qa_bots *b,qa_actor_id actor,bool restart,qa_error *e) {
    if(!b) return true;
    bot_ai_state *s=bot_ai_actor(b,actor);if(!s || !s->inuse) return true;
    if(b->busy || b->restore_pending || b->checking_spawn || qa_bot_runtime_closed(b->runtime) ||
       (b->shutting_down && (!qa_actor_id_equal(b->shutdown_actor,actor) || b->shutdown_restart!=restart)))
        return bot_ai_fail(e,"bot client shutdown requires its same idle source actor and disposition");
    if(!qa_bot_runtime_lease_begin(b->runtime,e)) return false;
    b->busy=true;bool ok=bot_ai_source_shutdown_client(b,s,restart,e);b->busy=false;
    if(!ok) {b->shutting_down=true;b->shutdown_restart=restart;b->shutdown_actor=actor;}
    else {b->shutting_down=false;b->shutdown_restart=false;b->shutdown_actor=(qa_actor_id){0};}
    qa_bot_runtime_lease_end(b->runtime);return ok;
}
bool qa_bots_shutdown(qa_bots *b,bool restart,qa_error *e) {
    if(!b) return true;
    if(b->busy || b->checking_spawn || b->restore_pending ||
       (restart && qa_bot_runtime_closed(b->runtime)) ||
       b->shutdown_actor.registry || (b->shutting_down && b->shutdown_restart!=restart))
        return bot_ai_fail(e,"bot source shutdown requires the same idle admitted lifetime");
    if(!restart) {
        b->busy=true;b->shutting_down=true;b->shutdown_restart=false;
        bool okay=qa_bot_runtime_shutdown(b->runtime,e);
        b->busy=false;return okay;
    }
    if(!qa_bot_runtime_lease_begin(b->runtime,e)) return false;
    b->busy=true;b->shutting_down=true;b->shutdown_restart=restart;
    bool ok=true;
    for(;;) {
        bot_ai_state *next=NULL;int32_t first=INT32_MAX;
        for(uint32_t i=0;i<b->client_capacity;++i) {
            bot_ai_state *s=b->clients[i];if(!s || !s->inuse) continue;
            int32_t client;
            if(s->retired || !bot_ai_live(b,s->view.actor) || !bot_ai_source_client(b,s,&client,e)) {ok=false;break;}
            if(!next || client<first) {next=s;first=client;}
        }
        if(!ok || !next) break;
        if(!bot_ai_source_shutdown_client(b,next,restart,e)) {ok=false;break;}
    }
    b->busy=false;qa_bot_runtime_lease_end(b->runtime);return ok;
}
bool bot_ai_schedule(qa_bots *b,qa_error *e) {
    if (!b->count) return true;
    uint32_t ordinal = 0;
    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t client=b->source_clients[i];if(!client) continue;
        uint32_t bits = (uint32_t)b->source_match.cvars[BOT_SOURCE_THINK_TIME].integer_value * ordinal++;
        int32_t product;
        memcpy(&product, &bits, sizeof(product));
        int32_t residual = (int32_t)((int64_t)product / b->count);
        if(!bot_ai_storage_i32(b,b->clients[client-1],QA_BOT_SOURCE_RESIDUAL,
                &residual,true,e)) return false;
    }
    return true;
}
static bool character_string(qa_bots *b, bot_ai_state *s, uint32_t characteristic,
                               char text[144], qa_error *e) {
    const char *value; bool written;
    if (!qa_bot_runtime_character_string(b->runtime, s->character, characteristic, &value, &written, e)) return false;
    size_t size = written ? strlen(value) : 0;
    if (size > 143) size = 143;
    if (size) memcpy(text, value, size);
    text[size] = 0;
    return true;
}
bool bot_ai_character_float(qa_bots *b, bot_ai_state *s, uint32_t index,
                              float minimum, float maximum, float *out, qa_error *e) {
    return qa_bot_runtime_character_bounded_float(b->runtime, s->character, index,
                                                   minimum, maximum, out, e);
}
static bool admit_resources(qa_bots *b, bot_ai_state *s, const qa_bot_admission *a, qa_error *e) {
    qa_bot_navigation *navigation = qa_bot_runtime_navigation(b->runtime, (int32_t)a->client);
    const qa_nav_graph_view *graph = navigation ? qa_navigation_graph(qa_bot_navigation_runtime(navigation)) : NULL;
    if (!graph || !graph->node_count || !qa_actor_id_equal(qa_bot_navigation_actor(navigation), a->actor)) {
        bot_ai_source_setup_failed(&s->source_setup,BOT_SOURCE_SETUP_FAILED_AAS,0);
        if(!bot_ai_source_print(b,"^1Fatal: AAS not initialized\n",e)) return false;
        return bot_ai_fail(e, "bot navigation is not bound to the admitted canonical actor");
    }
    if (!qa_bot_runtime_character_load(b->runtime, a->character_file, a->skill, &s->character, e)) return false;
    if(!bot_ai_storage_u32(b,s,QA_BOT_SOURCE_CHARACTER,&s->character,true,e)) return false;
    bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_CHARACTER);
    if (!s->character) {
        bot_ai_source_setup_failed(&s->source_setup,BOT_SOURCE_SETUP_FAILED_CHARACTER,0);
        return bot_ai_fail(e, "native bot character handles are exhausted");
    }
    bot_ai_source_setup_team(&s->source_setup,a->team?a->team:"");
    if(!bot_ai_storage_settings(b,s,a,e)) return false;
    bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_SETTINGS);
    qa_bot_goals *goals = qa_bot_runtime_goals(b->runtime);
    if (!qa_bot_goals_allocate(goals, (int32_t)a->client, &s->goals, e)) return false;
    if(!bot_ai_storage_u32(b,s,QA_BOT_SOURCE_GOALS,&s->goals,true,e)) return false;
    bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_GOAL_STATE);
    char path[144], name[144];
    if (!character_string(b, s, BOT_C_ITEM_WEIGHTS, path, e)) return false;
    int32_t result;
    if (!qa_bot_runtime_goal_weights(b->runtime, s->goals, path, &result, e)) return false;
    if (result) {
        bot_ai_source_setup_failed(&s->source_setup,BOT_SOURCE_SETUP_FAILED_ITEM_WEIGHTS,result);
        if(s->goals && !qa_bot_goals_free(goals,s->goals,e)) return false;
        s->goals=0;
        return bot_ai_fail(e, "native bot item weights could not load");
    }
    bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_ITEM_WEIGHTS);
    if (!qa_bot_runtime_weapon_allocate(b->runtime, &s->weapons, e)) return false;
    if(!bot_ai_storage_u32(b,s,QA_BOT_SOURCE_WEAPONS,&s->weapons,true,e)) return false;
    bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_WEAPON_STATE);
    if (!character_string(b, s, BOT_C_WEAPON_WEIGHTS, path, e) ||
        !qa_bot_runtime_weapon_weights(b->runtime, s->weapons, path, &result, e)) return false;
    if (result) {
        bot_ai_source_setup_failed(&s->source_setup,BOT_SOURCE_SETUP_FAILED_WEAPON_WEIGHTS,result);
        if(s->goals && !qa_bot_goals_free(goals,s->goals,e)) return false;
        s->goals=0;
        if(s->weapons && !qa_bot_runtime_weapon_free(b->runtime,s->weapons,e)) return false;
        s->weapons=0;
        return bot_ai_fail(e, "native bot weapon weights could not load");
    }
    bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_WEAPON_WEIGHTS);
    if (!qa_bot_runtime_chat_allocate(b->runtime, &s->chat, e)) return false;
    if(!bot_ai_storage_u32(b,s,QA_BOT_SOURCE_CHAT,&s->chat,true,e)) return false;
    bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_CHAT_STATE);
    if (!character_string(b, s, BOT_C_CHAT_FILE, path, e) ||
        !character_string(b, s, BOT_C_CHAT_NAME, name, e) ||
        !qa_bot_runtime_chat_load(b->runtime, s->chat, path, name, &result, e)) return false;
    if (result) {
        bot_ai_source_setup_failed(&s->source_setup,BOT_SOURCE_SETUP_FAILED_CHAT_FILE,result);
        if(s->chat && !qa_bot_runtime_chat_free(b->runtime,s->chat,e)) return false;
        s->chat=0;
        if(s->goals && !qa_bot_goals_free(goals,s->goals,e)) return false;
        s->goals=0;
        if(s->weapons && !qa_bot_runtime_weapon_free(b->runtime,s->weapons,e)) return false;
        s->weapons=0;
        return bot_ai_fail(e, "native bot chat file could not load");
    }
    bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_CHAT_FILE);
    return bot_ai_source_setup_gender(b,s,e);
}
static bool admit(qa_bots *b,const qa_bot_admission *a,bool *rejected,qa_error *e) {
    if (!bot_ai_mutable(b, e)) return false;
    if (!a || !a->character_file || !a->name || !isfinite(a->skill) ||
        a->client >= b->client_capacity || a->entity < 0 || !bot_ai_live(b, a->actor) ||
        (a->actor.slot < b->actor_capacity && b->actor_clients[a->actor.slot]!=0 &&
         b->actor_clients[a->actor.slot]!=a->client+1))
        return bot_ai_fail(e, "invalid or duplicate native bot admission");
    int32_t source_client;
    if(!b->services.source_client(b->services.context,a->actor,&source_client,e)) return false;
    if(source_client<0 || source_client>=64)
        return bot_ai_fail(e,"bot admission has no unique actual source client slot");
    bot_ai_state *s=b->source_cells[source_client];
    if(b->clients[a->client] && b->clients[a->client]!=s)
        return bot_ai_fail(e,"bot source or virtual client already has an active setup state");
    if (a->actor.slot >= b->actor_capacity) {
        uint32_t capacity=qa_actors_capacity(qa_session_actors(b->services.shared.session));
        if (capacity && SIZE_MAX/capacity<sizeof(*b->actor_clients)) return bot_ai_fail(e,"bot actor lookup overflow");
        uint32_t *lookup=realloc(b->actor_clients,(size_t)capacity*sizeof(*lookup));
        if (!lookup) {qa_error_set(e,QA_ERROR_MEMORY,capacity,"growing bot actor lookup");return false;}
        memset(lookup+b->actor_capacity,0,(capacity-b->actor_capacity)*sizeof(*lookup));
        b->actor_clients=lookup;b->actor_capacity=capacity;
    }
    bool acquired=s!=NULL;
    if(!s) s=calloc(1,sizeof(*s));
    if (!s) { qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating native bot continuation"); return false; }
    if(!acquired) {
        b->busy=true;
        bool allocated=qa_bot_source_record_allocate(&b->services.memory,&s->source_record,e);
        b->busy=false;
        if(!allocated) {free(s);return false;}
        s->acquired_source_client=(uint32_t)source_client;
        bot_ai_source_order_init(&s->source_order);
        bot_ai_source_chat_init(&s->source_chat);
        bot_ai_source_setup_init(&s->source_setup);
        b->source_cells[source_client]=s;
    }
    bool source_inuse;
    if(!bot_ai_storage_bool(b,s,QA_BOT_SOURCE_INUSE,&source_inuse,false,e)) return false;
    if(source_inuse) {
        char text[144];snprintf(text,sizeof(text),"^1Fatal: BotAISetupClient: client %d already setup\n",source_client);
        b->busy=true;bool printed=bot_ai_source_print(b,text,e);b->busy=false;
        if(!printed) return false;
        if(rejected) *rejected=true;
        return bot_ai_fail(e,"bot actual source allocation is already in use");
    }
    size_t character_size = strlen(a->character_file) + 1;
    char *character_request=malloc(character_size);
    if (!character_request) {
        qa_error_set(e, QA_ERROR_MEMORY, character_size, "retaining original bot character request");return false;
    }
    memcpy(character_request,a->character_file,character_size);
    size_t admitted_name_size = strlen(a->name) + 1;
    char *admission_name=malloc(admitted_name_size);
    if (!admission_name) {
        free(character_request);
        qa_error_set(e, QA_ERROR_MEMORY, admitted_name_size, "retaining original bot admission name");return false;
    }
    memcpy(admission_name,a->name,admitted_name_size);
    if (!qa_bot_runtime_lease_begin(b->runtime,e)) {
        free(admission_name);free(character_request);return false;
    }
    if(acquired && s->view.actor.registry) {
        if(s->view.actor.slot<b->actor_capacity) b->actor_clients[s->view.actor.slot]=0;
        if(s->view.client<b->client_capacity && b->clients[s->view.client]==s) b->clients[s->view.client]=NULL;
    }
    s->acquired_source_client=(uint32_t)source_client;
    s->view=(qa_bot_view){.actor=a->actor,.client=a->client,.entity=a->entity,
        .source_client=source_client,.mode=a->mode,.decision=QA_BOT_SEEK_LONG_TERM};
    s->team_arena=a->team_arena;s->admitted_skill=a->skill;
    free(s->admitted_character);free(s->admitted_name);
    s->admitted_character=character_request;s->admitted_name=admission_name;
    size_t name_size = strlen(a->name);
    if (name_size >= sizeof(s->name)) name_size = sizeof(s->name) - 1;
    memset(s->name,0,sizeof(s->name));memcpy(s->name, a->name, name_size);
    b->clients[a->client]=s;
    b->source_cells[source_client]=s;
    b->actor_clients[a->actor.slot]=a->client+1;
    bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_ALLOCATED);
    b->busy = true;
    bool ok = admit_resources(b, s, a, e);
    if (ok && !bot_ai_live(b, a->actor)) ok = bot_ai_fail(e, "bot actor retired during resource admission");
    if (ok) {
        ok=bot_ai_storage_publish(b,s,e);
    }
    if (ok) {
        s->inuse=true;
        s->view.enter_time=b->time;
        b->source_clients[source_client]=a->client+1;
        bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_PUBLISHED);
        ok=bot_ai_source_setup_published(b,s,a->restart,b->source_match.interbreed,e);
    }
    b->busy = false;
    qa_bot_runtime_lease_end(b->runtime);
    if(!ok && rejected && s->source_setup.progress.kind==BOT_SOURCE_SETUP_FAILED) *rejected=true;
    return ok;
}
bool qa_bots_admit(qa_bots *b,const qa_bot_admission *a,qa_error *e) {
    return admit(b,a,NULL,e);
}
bool qa_bots_admit_source(qa_bots *b,const qa_bot_admission *a,bool *accepted,qa_error *e) {
    if(!accepted) return bot_ai_fail(e,"source bot admission requires its real SetupClient result");
    *accepted=false;bool rejected=false;qa_error source_error={0};
    bool okay=admit(b,a,&rejected,&source_error);
    if(okay) {*accepted=true;return true;}
    if(rejected) return true;
    if(e) *e=source_error;
    return false;
}
bool qa_bots_release(qa_bots *b, qa_actor_id actor, qa_error *e) {
    if (!bot_ai_mutable(b, e)) return false;
    bot_ai_state *s = bot_ai_actor(b, actor);
    if (!s) return true;
    if (!qa_bot_runtime_lease_begin(b->runtime,e)) return false;
    b->busy = true;
    bool published=s->inuse;
    bool ok = bot_ai_cleanup(b, s, e);
    if (ok) {
        ok=!published || qa_bot_source_record_clear(&b->services.memory,s->source_record,false,e);
        if(ok) {bot_ai_source_cell_clear(b,s);ok=bot_ai_schedule(b,e);}
    } else s->retired=true;
    b->busy = false;
    qa_bot_runtime_lease_end(b->runtime);
    return ok;
}
bool qa_bots_actor_released(qa_bots *b, const qa_actor_record *released, qa_error *e) {
    if (!b || !released) return bot_ai_fail(e, "missing bot retirement record");
    bot_ai_state *s = bot_ai_actor(b, released->id);
    if (!s) return true;
    if (b->busy || b->restore_pending || b->shutting_down) { s->retired = true; return true; }
    return qa_bots_release(b, released->id, e);
}
bool qa_bots_read(const qa_bots *b, qa_actor_id actor, qa_bot_view *out, qa_error *e) {
    bot_ai_state *s = bot_ai_actor(b, actor);
    if (!s || !s->inuse || !out || !bot_ai_live(b, actor)) return bot_ai_fail(e, "native bot actor is not live");
    *out = s->view;
    return true;
}
bool qa_bots_setup_failed(const qa_bots *b,qa_actor_id actor) {
    bot_ai_state *s=bot_ai_actor(b,actor);
    return s && s->source_setup.progress.kind==BOT_SOURCE_SETUP_FAILED;
}
bool bot_ai_random(qa_bots *b,float *out,qa_error *e) {
    return b->services.random(b->services.context,out,e);
}
