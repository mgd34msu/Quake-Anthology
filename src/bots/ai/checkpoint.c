#include "internal.h"
#include "../checkpoint_internal.h"

typedef struct bot_checkpoint_record {
    bot_ai_state state;
    uint8_t source_bytes[QA_BOT_STATE_SOURCE_BYTES];
    qa_bot_move_state movement;
    qa_bot_chat_state chat;
    qa_bot_input actions;
    bot_weapon_history *weapons;
    qa_bot_character *character;
} bot_checkpoint_record;
struct qa_bots_checkpoint {
    qa_bot_memory_checkpoint *memory;
    bot_fuzzy_history *fuzzy;
    bot_goal_history *goals;
    bot_checkpoint_record *records;
    uint32_t count,bot_count,client_capacity;
    qa_bot_controls controls;
    int32_t local_time_ms,library_residual_ms,scheduled_think_ms;
    float time,regular_update_time;
    bool shutting_down,shutdown_restart;
    qa_actor_id shutdown_actor;
    struct {char name[36];int32_t preference;} team_preferences[64];
    bool not_leader[64];
    bot_source_goals source_goals;
    bot_source_orders_state source_orders;
    bot_source_team_policy_globals source_team_policy;
    bot_source_events_globals source_event_globals;
    bot_source_chat_globals source_chat;
    bot_source_match_globals source_match;
    size_t leases;
    bool destroy_pending;
};
void qa_bots_checkpoint_destroy(qa_bots_checkpoint *checkpoint) {
    if(!checkpoint) return;
    if(checkpoint->leases) {checkpoint->destroy_pending=true;return;}
    for(uint32_t i=0;i<checkpoint->count;++i) {
        bot_checkpoint_record *record=&checkpoint->records[i];
        qa_bot_chat_state_free(&record->chat);
        bot_weapon_history_destroy(record->weapons);
        qa_bot_character_release(record->character);
        free(record->state.admitted_character);
        free(record->state.admitted_name);
    }
    bot_goal_history_destroy(checkpoint->goals);
    bot_fuzzy_history_destroy(checkpoint->fuzzy);
    qa_bot_memory_checkpoint_destroy(checkpoint->memory);
    free(checkpoint->records);free(checkpoint);
}
static bool capture(qa_bots *b,qa_bots_checkpoint **out,qa_error *e) {
    qa_bots_checkpoint *checkpoint=calloc(1,sizeof(*checkpoint));
    if(!checkpoint) goto memory;
    uint32_t acquired=0;
    for(uint32_t i=0;i<64;++i) if(b->source_cells[i]) ++acquired;
    if(acquired && !(checkpoint->records=calloc(acquired,sizeof(*checkpoint->records)))) {
        free(checkpoint);goto memory;
    }
    checkpoint->client_capacity=b->client_capacity;checkpoint->controls=b->controls;
    checkpoint->local_time_ms=b->local_time_ms;checkpoint->library_residual_ms=b->library_residual_ms;
    checkpoint->scheduled_think_ms=b->scheduled_think_ms;checkpoint->time=b->time;
    checkpoint->regular_update_time=b->regular_update_time;
    checkpoint->shutting_down=b->shutting_down;checkpoint->shutdown_restart=b->shutdown_restart;
    checkpoint->shutdown_actor=b->shutdown_actor;
    memcpy(checkpoint->team_preferences,b->team_preferences,sizeof(checkpoint->team_preferences));
    memcpy(checkpoint->not_leader,b->not_leader,sizeof(checkpoint->not_leader));
    checkpoint->source_goals=b->source_goals;
    checkpoint->source_orders=b->source_orders;checkpoint->source_team_policy=b->source_team_policy;
    checkpoint->source_event_globals=b->source_event_globals;
    checkpoint->source_chat=b->source_chat;
    checkpoint->source_match=b->source_match;
    checkpoint->bot_count=b->count;
    if(!qa_bot_memory_checkpoint_capture(qa_bot_runtime_memory(b->runtime),&checkpoint->memory,e) ||
       !bot_fuzzy_history_capture(qa_bot_runtime_library(b->runtime),&checkpoint->fuzzy,e) ||
       !bot_goal_history_capture(qa_bot_runtime_goals(b->runtime),checkpoint->fuzzy,&checkpoint->goals,e)) goto failed;
    for(uint32_t i=0;i<64;++i) {
        bot_ai_state *s=b->source_cells[i];
        if(!s) continue;
        if(s->retired || (s->view.actor.registry && !bot_ai_live(b,s->view.actor))) {
            bot_ai_fail(e,"cannot checkpoint a retired bot continuation");goto failed;
        }
        bot_checkpoint_record *record=&checkpoint->records[checkpoint->count++];
        if(!qa_bot_source_record_read(&b->services.memory,s->source_record,record->source_bytes,e)) goto failed;
        record->state=*s;
        record->state.admitted_character=NULL;
        record->state.admitted_name=NULL;
        if(s->view.actor.registry && (!s->admitted_character || !s->admitted_name)) {bot_ai_fail(e,"bot original admission settings are absent");goto failed;}
        size_t path_size=s->admitted_character?strlen(s->admitted_character)+1:0;
        if(path_size) {
        record->state.admitted_character=malloc(path_size);
        if(!record->state.admitted_character) {
            qa_error_set(e,QA_ERROR_MEMORY,path_size,"retaining bot checkpoint character request");goto failed;
        }
        memcpy(record->state.admitted_character,s->admitted_character,path_size);
        }
        size_t name_size=s->admitted_name?strlen(s->admitted_name)+1:0;
        if(name_size) {
        record->state.admitted_name=malloc(name_size);
        if(!record->state.admitted_name) {
            qa_error_set(e,QA_ERROR_MEMORY,name_size,"retaining bot checkpoint admission name");goto failed;
        }
        memcpy(record->state.admitted_name,s->admitted_name,name_size);
        }
        record->character=(qa_bot_character *)qa_bot_runtime_character(b->runtime,s->character);
        if(s->character && !record->character) {bot_ai_fail(e,"bot checkpoint character is absent");goto failed;}
        if(record->character) qa_bot_character_retain(record->character);
        if((s->movement && !qa_bot_moves_capture(qa_bot_runtime_moves(b->runtime),s->movement,&record->movement,e)) ||
           (s->chat && !qa_bot_chat_capture_state(qa_bot_runtime_chat(b->runtime,s->chat),&record->chat,e)) ||
           (s->view.actor.registry && !qa_bot_actions_read(qa_bot_runtime_actions(b->runtime),s->view.client,&record->actions,e)) ||
           (s->weapons && !bot_weapon_checkpoint_capture(b->runtime,s->weapons,checkpoint->fuzzy,&record->weapons,e))) goto failed;
    }
    *out=checkpoint;return true;
failed:
    qa_bots_checkpoint_destroy(checkpoint);return false;
memory:
    qa_error_set(e,QA_ERROR_MEMORY,0,"allocating native bot checkpoint");return false;
}
bool qa_bots_capture(qa_bots *b,qa_bots_checkpoint **out,qa_error *e) {
    if(!bot_ai_mutable(b,e)) return false;
    if(!out || b->checking_spawn) return bot_ai_fail(e,"bot checkpoint requires an idle population and output");
    if(!qa_bot_runtime_lease_begin(b->runtime,e)) return false;
    b->busy=true;bool ok=capture(b,out,e);b->busy=false;
    qa_bot_runtime_lease_end(b->runtime);return ok;
}
static bool validate(qa_bots *b,const qa_bots_checkpoint *checkpoint,qa_error *e) {
    uint32_t acquired=0;
    for(uint32_t i=0;i<64;++i) if(b->source_cells[i]) ++acquired;
    if(!checkpoint || checkpoint->destroy_pending || b->checking_spawn || b->source_match_exit_depth ||
       checkpoint->count!=acquired || checkpoint->bot_count!=b->count ||
       checkpoint->client_capacity!=b->client_capacity) return bot_ai_fail(e,"bot checkpoint roster differs from live bindings");
    for(uint32_t i=0;i<checkpoint->count;++i) {
        const bot_checkpoint_record *record=&checkpoint->records[i];
        const bot_ai_state *saved=&record->state;
        bot_ai_state *live=saved->acquired_source_client<64?b->source_cells[saved->acquired_source_client]:NULL;
        if(!live || live->retired || !qa_actor_id_equal(live->view.actor,saved->view.actor) ||
           live->source_record.offset!=saved->source_record.offset ||
           live->source_record.length!=saved->source_record.length ||
           (saved->view.actor.registry && !bot_ai_live(b,saved->view.actor)) ||
           live->view.client!=saved->view.client || live->view.source_client!=saved->view.source_client ||
           live->view.entity!=saved->view.entity ||
           live->inuse!=saved->inuse || live->counted!=saved->counted ||
           (saved->view.actor.registry && (saved->view.client>=b->client_capacity ||
            b->clients[saved->view.client]!=live || saved->view.actor.slot>=b->actor_capacity ||
            b->actor_clients[saved->view.actor.slot]!=saved->view.client+1)) ||
           (saved->inuse && b->source_clients[saved->acquired_source_client]!=saved->view.client+1) ||
           (!saved->inuse && b->source_clients[saved->acquired_source_client]) ||
           live->character!=saved->character || live->goals!=saved->goals ||
           live->weapons!=saved->weapons || live->chat!=saved->chat || live->movement!=saved->movement ||
           (live->admitted_character==NULL)!=(saved->admitted_character==NULL) ||
           (live->admitted_name==NULL)!=(saved->admitted_name==NULL) ||
           (live->admitted_name && strcmp(live->admitted_name,saved->admitted_name)) ||
           (live->admitted_character && strcmp(live->admitted_character,saved->admitted_character)) ||
           memcmp(&live->admitted_skill,&saved->admitted_skill,sizeof(live->admitted_skill)) ||
           record->character!=qa_bot_runtime_character(b->runtime,live->character) ||
           (live->goals && !qa_bot_goals_has_handle(qa_bot_runtime_goals(b->runtime),live->goals)) ||
           (live->movement && !qa_bot_moves_has_handle(qa_bot_runtime_moves(b->runtime),live->movement)) ||
           (live->chat && !qa_bot_runtime_chat(b->runtime,live->chat)) ||
           (live->weapons && !qa_bot_runtime_weapon_has_handle(b->runtime,live->weapons)))
            return bot_ai_fail(e,"bot checkpoint resource or actor binding changed");
    }
    return true;
}
bool qa_bots_checkpoint_validate(qa_bots *b,const qa_bots_checkpoint *checkpoint,qa_error *e) {
    return bot_ai_mutable(b,e) && validate(b,checkpoint,e);
}
typedef struct bot_prepared_record {
    bot_weapon_restore *weapons;
    qa_bot_input *actions;
    qa_bot_source_span source_span;
    char *character_request;
    char *admission_name;
} bot_prepared_record;
static bool prepare(qa_bots *b,const qa_bots_checkpoint *checkpoint,bot_prepared_record *prepared,
                    qa_error *e) {
    for(uint32_t i=0;i<checkpoint->count;++i) {
        const bot_checkpoint_record *record=&checkpoint->records[i];
        bot_ai_state *s=b->source_cells[record->state.acquired_source_client];
        if(!qa_bot_source_record_span(&b->services.memory,record->state.source_record,
            &prepared[i].source_span,e)) return false;
        size_t path_size=record->state.admitted_character?strlen(record->state.admitted_character)+1:0;
        if(path_size) {
        prepared[i].character_request=malloc(path_size);
        if(!prepared[i].character_request) {
            qa_error_set(e,QA_ERROR_MEMORY,path_size,"preparing restored bot character request");return false;
        }
        memcpy(prepared[i].character_request,record->state.admitted_character,path_size);
        }
        size_t name_size=record->state.admitted_name?strlen(record->state.admitted_name)+1:0;
        if(name_size) {
        prepared[i].admission_name=malloc(name_size);
        if(!prepared[i].admission_name) {
            qa_error_set(e,QA_ERROR_MEMORY,name_size,"preparing restored bot admission name");return false;
        }
        memcpy(prepared[i].admission_name,record->state.admitted_name,name_size);
        }
        if(s->weapons && !bot_weapon_restore_prepare(b->runtime,s->weapons,record->weapons,&prepared[i].weapons,e)) return false;
    }
    return true;
}
bool qa_bots_restore(qa_bots *b,const qa_bots_checkpoint *checkpoint,qa_error *e) {
    if(!qa_bots_checkpoint_validate(b,checkpoint,e) || !qa_bot_runtime_lease_begin(b->runtime,e)) return false;
    qa_bots_checkpoint *borrowed=(qa_bots_checkpoint *)checkpoint;
    if(borrowed->leases==SIZE_MAX) {
        qa_bot_runtime_lease_end(b->runtime);return bot_ai_fail(e,"bot checkpoint lease limit exceeded");
    }
    if(!bot_runtime_restore_begin(b->runtime,e)) {
        qa_bot_runtime_lease_end(b->runtime);return false;
    }
    ++borrowed->leases;
    b->busy=true;
    uint32_t count=checkpoint->count;
    bot_prepared_record *prepared=count?calloc(count,sizeof(*prepared)):NULL;
    bot_chat_restore_entry *chats=count?calloc(count,sizeof(*chats)):NULL;
    bot_chat_restore *chat=NULL;
    qa_bot_memory_prepared *memory=NULL;
    bot_fuzzy_history_restore *fuzzy=NULL;
    bot_goal_history_restore *goals=NULL;
    bool ok=!count || (prepared && chats);
    size_t chat_count=0;
    if(!ok) qa_error_set(e,QA_ERROR_MEMORY,0,"preparing native bot checkpoint population");
    for(uint32_t i=0;ok && i<count;++i) {
        const bot_checkpoint_record *record=&checkpoint->records[i];
        if(record->state.chat) chats[chat_count++]=(bot_chat_restore_entry){
            .chat=qa_bot_runtime_chat(b->runtime,record->state.chat),.state=&record->chat};
        if(record->state.view.actor.registry) {
            prepared[i].actions=bot_action_restore_input(qa_bot_runtime_actions(b->runtime),record->state.view.client,e);
            ok=prepared[i].actions!=NULL;
        }
    }
    if(ok) ok=bot_chat_restore_prepare(chats,chat_count,&chat,e);
    for(uint32_t i=0;ok && i<count;++i) {
        const bot_checkpoint_record *record=&checkpoint->records[i];
        ok=!record->state.movement || bot_move_restore_validate(qa_bot_runtime_moves(b->runtime),record->state.movement,&record->movement,e);
        if(ok && (checkpoint->destroy_pending ||
           (record->state.view.actor.registry && !bot_ai_live(b,record->state.view.actor))))
            ok=bot_ai_fail(e,"bot checkpoint or actor retired during navigation validation");
    }
    if(ok) ok=validate(b,checkpoint,e) && prepare(b,checkpoint,prepared,e);
    if(ok) ok=qa_bot_memory_checkpoint_prepare(qa_bot_runtime_memory(b->runtime),checkpoint->memory,&memory,e) &&
        bot_fuzzy_history_prepare(qa_bot_runtime_library(b->runtime),checkpoint->fuzzy,memory,&fuzzy,e) &&
        bot_goal_history_prepare(qa_bot_runtime_goals(b->runtime),checkpoint->goals,memory,&goals,e);
    if(ok) ok=validate(b,checkpoint,e);
    /* All aliases are qualified before writing the actual retained GAME bytes.
     * Read/write callbacks have no source setup or allocation effects. */
    for(uint32_t i=0;ok && i<count;++i) {
        qa_bot_source_record probe={checkpoint->records[i].state.source_record.offset,0};
        ok=qa_bot_source_record_write(&b->services.memory,probe,NULL,e);
    }
    for(uint32_t i=0;ok && i<count;++i)
        ok=qa_bot_source_record_write(&b->services.memory,checkpoint->records[i].state.source_record,
            checkpoint->records[i].source_bytes,e);
    qa_bot_memory_checkpoint_finish(memory,ok);
    bot_fuzzy_history_finish(fuzzy,ok);
    bot_goal_history_finish(goals,ok);
    for(uint32_t i=0;prepared && i<count;++i) {
        bot_weapon_restore_finish(prepared[i].weapons,ok);
        if(ok) {
            const bot_checkpoint_record *record=&checkpoint->records[i];
            if(record->state.movement) bot_move_restore_commit(qa_bot_runtime_moves(b->runtime),record->state.movement,&record->movement);
            if(prepared[i].actions) *prepared[i].actions=record->actions;
            bot_ai_state *state=b->source_cells[record->state.acquired_source_client];
            free(state->admitted_character);free(state->admitted_name);*state=record->state;
            state->source_span=prepared[i].source_span;
            state->admitted_character=prepared[i].character_request;prepared[i].character_request=NULL;
            state->admitted_name=prepared[i].admission_name;prepared[i].admission_name=NULL;
        }
        free(prepared[i].character_request);
        free(prepared[i].admission_name);
    }
    bot_chat_restore_finish(chat,ok);
    if(ok) {
        b->controls=checkpoint->controls;b->local_time_ms=checkpoint->local_time_ms;
        b->library_residual_ms=checkpoint->library_residual_ms;b->scheduled_think_ms=checkpoint->scheduled_think_ms;
        b->time=checkpoint->time;b->regular_update_time=checkpoint->regular_update_time;
        b->shutting_down=checkpoint->shutting_down;b->shutdown_restart=checkpoint->shutdown_restart;
        b->shutdown_actor=checkpoint->shutdown_actor;
        memcpy(b->team_preferences,checkpoint->team_preferences,sizeof(b->team_preferences));
        memcpy(b->not_leader,checkpoint->not_leader,sizeof(b->not_leader));b->source_goals=checkpoint->source_goals;
        b->source_orders=checkpoint->source_orders;b->source_team_policy=checkpoint->source_team_policy;
        b->source_event_globals=checkpoint->source_event_globals;
        b->source_chat=checkpoint->source_chat;
        b->source_match=checkpoint->source_match;
    }
    free(chats);free(prepared);
    b->busy=false;bot_runtime_restore_end(b->runtime);qa_bot_runtime_lease_end(b->runtime);
    --borrowed->leases;
    if(borrowed->destroy_pending) qa_bots_checkpoint_destroy(borrowed);
    return ok;
}
