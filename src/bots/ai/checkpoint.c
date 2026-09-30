#include "internal.h"
#include "../checkpoint_internal.h"

typedef struct bot_checkpoint_record {
    bot_ai_state state;
    qa_bot_goal_state goals;
    qa_bot_move_state movement;
    qa_bot_chat_state chat;
    qa_bot_input actions;
    qa_bot_weights *goal_weights,*weapon_weights;
    qa_bot_character *character;
} bot_checkpoint_record;
struct qa_bots_checkpoint {
    bot_checkpoint_record *records;
    uint32_t count,client_capacity;
    qa_bot_controls controls;
    int32_t local_time_ms,library_residual_ms,scheduled_think_ms;
    float time,regular_update_time;
    size_t leases;
    bool destroy_pending;
};
void qa_bots_checkpoint_destroy(qa_bots_checkpoint *checkpoint) {
    if(!checkpoint) return;
    if(checkpoint->leases) {checkpoint->destroy_pending=true;return;}
    for(uint32_t i=0;i<checkpoint->count;++i) {
        bot_checkpoint_record *record=&checkpoint->records[i];
        qa_bot_chat_state_free(&record->chat);
        qa_bot_weights_release(record->goal_weights);qa_bot_weights_release(record->weapon_weights);
        qa_bot_character_release(record->character);
    }
    free(checkpoint->records);free(checkpoint);
}
static bool capture(qa_bots *b,qa_bots_checkpoint **out,qa_error *e) {
    qa_bots_checkpoint *checkpoint=calloc(1,sizeof(*checkpoint));
    if(!checkpoint) goto memory;
    if(b->count && !(checkpoint->records=calloc(b->count,sizeof(*checkpoint->records)))) {
        free(checkpoint);goto memory;
    }
    checkpoint->client_capacity=b->client_capacity;checkpoint->controls=b->controls;
    checkpoint->local_time_ms=b->local_time_ms;checkpoint->library_residual_ms=b->library_residual_ms;
    checkpoint->scheduled_think_ms=b->scheduled_think_ms;checkpoint->time=b->time;
    checkpoint->regular_update_time=b->regular_update_time;
    for(uint32_t i=0;i<b->client_capacity;++i) {
        bot_ai_state *s=b->clients[i];
        if(!s) continue;
        if(s->retired || !bot_ai_live(b,s->view.actor)) {
            bot_ai_fail(e,"cannot checkpoint a retired bot continuation");goto failed;
        }
        bot_checkpoint_record *record=&checkpoint->records[checkpoint->count++];
        record->state=*s;
        record->character=(qa_bot_character *)qa_bot_runtime_character(b->runtime,s->character);
        if(!record->character) {bot_ai_fail(e,"bot checkpoint character is absent");goto failed;}
        qa_bot_character_retain(record->character);
        qa_bot_weights *borrowed=NULL;
        if(!qa_bot_goals_capture(qa_bot_runtime_goals(b->runtime),s->goals,&record->goals,&borrowed,e) ||
           (borrowed && !qa_bot_weights_clone(borrowed,&record->goal_weights,e)) ||
           !qa_bot_moves_capture(qa_bot_runtime_moves(b->runtime),s->movement,&record->movement,e) ||
           !qa_bot_chat_capture_state(qa_bot_runtime_chat(b->runtime,s->chat),&record->chat,e) ||
           !qa_bot_actions_read(qa_bot_runtime_actions(b->runtime),s->view.client,&record->actions,e) ||
           !qa_bot_runtime_weapon_capture(b->runtime,s->weapons,&record->weapon_weights,e)) goto failed;
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
    if(!checkpoint || checkpoint->destroy_pending || b->checking_spawn || checkpoint->count!=b->count ||
       checkpoint->client_capacity!=b->client_capacity) return bot_ai_fail(e,"bot checkpoint roster differs from live bindings");
    for(uint32_t i=0;i<checkpoint->count;++i) {
        const bot_checkpoint_record *record=&checkpoint->records[i];
        const bot_ai_state *saved=&record->state;
        bot_ai_state *live=bot_ai_actor(b,saved->view.actor);
        if(!live || live->retired || !bot_ai_live(b,saved->view.actor) ||
           live->view.client!=saved->view.client || live->view.entity!=saved->view.entity ||
           live->character!=saved->character || live->goals!=saved->goals ||
           live->weapons!=saved->weapons || live->chat!=saved->chat || live->movement!=saved->movement ||
           record->character!=qa_bot_runtime_character(b->runtime,live->character) ||
           !qa_bot_goals_has_handle(qa_bot_runtime_goals(b->runtime),live->goals) ||
           !qa_bot_moves_has_handle(qa_bot_runtime_moves(b->runtime),live->movement) ||
           !qa_bot_runtime_chat(b->runtime,live->chat) || !qa_bot_runtime_weapon_has_handle(b->runtime,live->weapons))
            return bot_ai_fail(e,"bot checkpoint resource or actor binding changed");
    }
    return true;
}
bool qa_bots_checkpoint_validate(qa_bots *b,const qa_bots_checkpoint *checkpoint,qa_error *e) {
    return bot_ai_mutable(b,e) && validate(b,checkpoint,e);
}
typedef struct bot_prepared_record {
    bot_goal_restore *goals;
    bot_weapon_restore *weapons;
    qa_bot_input *actions;
} bot_prepared_record;
static bool prepare(qa_bots *b,const qa_bots_checkpoint *checkpoint,bot_prepared_record *prepared,
                    qa_error *e) {
    for(uint32_t i=0;i<checkpoint->count;++i) {
        const bot_checkpoint_record *record=&checkpoint->records[i];
        bot_ai_state *s=bot_ai_actor(b,record->state.view.actor);
        qa_bot_weights *goal_weights=NULL,*weapon_weights=NULL;
        if((record->goal_weights && !qa_bot_weights_clone(record->goal_weights,&goal_weights,e)) ||
           (record->weapon_weights && !qa_bot_weights_clone(record->weapon_weights,&weapon_weights,e))) {
            qa_bot_weights_release(goal_weights);return false;
        }
        bool ok=bot_goal_restore_prepare(qa_bot_runtime_goals(b->runtime),s->goals,&record->goals,
                                         goal_weights,&prepared[i].goals,e) &&
            bot_weapon_restore_prepare(b->runtime,s->weapons,weapon_weights,&prepared[i].weapons,e);
        qa_bot_weights_release(goal_weights);qa_bot_weights_release(weapon_weights);
        if(!ok) return false;
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
    bool ok=!count || (prepared && chats);
    if(!ok) qa_error_set(e,QA_ERROR_MEMORY,0,"preparing native bot checkpoint population");
    for(uint32_t i=0;ok && i<count;++i) {
        const bot_checkpoint_record *record=&checkpoint->records[i];
        chats[i]=(bot_chat_restore_entry){.chat=qa_bot_runtime_chat(b->runtime,record->state.chat),
                                         .state=&record->chat};
        prepared[i].actions=bot_action_restore_input(qa_bot_runtime_actions(b->runtime),record->state.view.client,e);
        ok=prepared[i].actions!=NULL;
    }
    if(ok) ok=bot_chat_restore_prepare(chats,count,&chat,e);
    for(uint32_t i=0;ok && i<count;++i) {
        const bot_checkpoint_record *record=&checkpoint->records[i];
        ok=bot_move_restore_validate(qa_bot_runtime_moves(b->runtime),record->state.movement,&record->movement,e);
        if(ok && (checkpoint->destroy_pending || !bot_ai_live(b,record->state.view.actor)))
            ok=bot_ai_fail(e,"bot checkpoint or actor retired during navigation validation");
    }
    if(ok) ok=validate(b,checkpoint,e) && prepare(b,checkpoint,prepared,e);
    for(uint32_t i=0;prepared && i<count;++i) {
        bot_goal_restore_finish(prepared[i].goals,ok);
        bot_weapon_restore_finish(prepared[i].weapons,ok);
        if(ok) {
            const bot_checkpoint_record *record=&checkpoint->records[i];
            bot_move_restore_commit(qa_bot_runtime_moves(b->runtime),record->state.movement,&record->movement);
            *prepared[i].actions=record->actions;
            *bot_ai_actor(b,record->state.view.actor)=record->state;
        }
    }
    bot_chat_restore_finish(chat,ok);
    if(ok) {
        b->controls=checkpoint->controls;b->local_time_ms=checkpoint->local_time_ms;
        b->library_residual_ms=checkpoint->library_residual_ms;b->scheduled_think_ms=checkpoint->scheduled_think_ms;
        b->time=checkpoint->time;b->regular_update_time=checkpoint->regular_update_time;
    }
    free(chats);free(prepared);
    b->busy=false;bot_runtime_restore_end(b->runtime);qa_bot_runtime_lease_end(b->runtime);
    --borrowed->leases;
    if(borrowed->destroy_pending) qa_bots_checkpoint_destroy(borrowed);
    return ok;
}
