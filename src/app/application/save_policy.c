#include "internal.h"
#include "map_players_private.h"
#include "guest_native_q2_private.h"
#include "guest_q3_private.h"
#include "qa/application_save_policy.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q3_source.h"
#include <stdlib.h>

static bool selected_player(const qa_application *app,const application_player_record *record,bool dedicated)
{
    return !record->retiring && !record->deferred && !record->source_begin_pending &&
        qa_actors_get(qa_session_actors(app->session),record->actor) &&
        (dedicated || (!record->remote && !record->bot));
}
static bool native_q2_player(application_provider *source,qa_actor_id actor,float *health,
    bool *intermission,qa_error *error)
{
    struct application_native_q2 *engine=source->state.native.q2_engine;
    uint32_t slot=0;
    for(uint32_t i=1;engine && i<257;++i)
        if(engine->clients[i].connected && engine->clients[i].begun && !engine->clients[i].disconnect_started &&
           qa_actor_id_equal(engine->clients[i].actor,actor)) {slot=i;break;}
    if(!slot || !engine->initialized || engine->baseline || engine->shutting_down || !source->state.native.host)
        return application_fail(error,QA_ERROR_NOT_FOUND,"Save policy lost its actual original Q2 player state");
    bool classic=engine->profile==QA_NATIVE_Q2_GAME_API3;
    if(!classic && engine->profile!=QA_NATIVE_Q2_GAME_API2023)
        return application_fail(error,QA_ERROR_ARGUMENT,"Save policy requires the actual original Q2 GAME profile");
    qa_buffer player={0};
    bool okay=qa_native_host_q2_player_state(source->state.native.host,slot,&player,error);
    if(okay && player.size!=(classic?184u:296u))
        okay=application_fail(error,QA_ERROR_FORMAT,"Save policy original Q2 player-state extent changed");
    if(okay) {
        /* Public API3/API2023 player_state_t; STAT_HEALTH is index 1. */
        *health=(float)(int16_t)qa_load_u16le(player.data+(classic?122u:168u));
        *intermission=*intermission || (classic?qa_load_i32le(player.data)==4:player.data[0]==6);
    }
    qa_buffer_free(&player);return okay;
}
static bool control_intermission(const qa_application *app,qa_actor_id actor)
{
    qa_application_control_view view;
    if(!qa_application_control_read(app,actor,&view)) return false;
    return application_control_intermission(&view.state);
}
bool qa_application_save_policy(qa_application *app,qa_save_authority authority,bool dedicated,
    bool loading,qa_save_purpose purpose,qa_error *error)
{
    if(!app || (unsigned)authority>QA_SAVE_REMOTE || (unsigned)purpose>QA_SAVE_DEMO_KEYFRAME)
        return application_fail(error,QA_ERROR_ARGUMENT,"Invalid actual application save policy request");
    if(app->operation!=APPLICATION_IDLE || app->frame_preparing || app->q3_round_active || app->q3_world_restart)
        return application_fail(error,QA_ERROR_ARGUMENT,"Save/load requires its actual idle application");
    if(loading) return authority==QA_SAVE_OFFLINE ||
        application_fail(error,QA_ERROR_ARGUMENT,"Save/load unavailable during a network game");
    application_provider *source=application_world_provider(app,QA_ROLE_ENTITIES,"");
    if(app->state!=QA_APPLICATION_RUNNING || !app->world || !app->session || !app->map_resource ||
       !source || !source->product || !source->constructed || !source->attached || source->close_pending)
        return application_fail(error,QA_ERROR_ARGUMENT,"No active world to save");
    qa_save_eligibility eligibility={.family=source->product->family,.authority=authority,.active=true};
    bool native_q2=source->kind==APPLICATION_PROVIDER_NATIVE && source->state.native.q2_engine;
    if(native_q2) {
        qa_cvars *registry=source->state.native.q2_engine->cvars;
        const qa_cvar_view *deathmatch=registry?qa_cvars_find(registry,"deathmatch"):NULL;
        if(!deathmatch) return application_fail(error,QA_ERROR_NOT_FOUND,"Original Q2 save policy has no actual deathmatch setting");
        if(deathmatch->number!=0)
            return application_fail(error,QA_ERROR_ARGUMENT,"Native Quake II deathmatch games cannot be saved");
    }
    if(purpose==QA_SAVE_TRANSITION || purpose==QA_SAVE_RECOVERY || purpose==QA_SAVE_DEMO_KEYFRAME)
        return qa_save_eligible(&eligibility,purpose,error);
    if(authority!=QA_SAVE_OFFLINE) return qa_save_eligible(&eligibility,purpose,error);
    if(!application_match_mode_source_owned(source)) {
        qa_mode_view mode;
        if(!app->modes || !app->primary_mode_ready || !qa_modes_read(app->modes,app->primary_mode,&mode,error))
            return application_fail(error,QA_ERROR_NOT_FOUND,"Save policy has no actual chosen primary match owner");
        eligibility.deathmatch=mode.rules.kind!=QA_MODE_SINGLE_PLAYER && mode.rules.kind!=QA_MODE_COOPERATIVE;
    }
    if(source->kind==APPLICATION_PROVIDER_Q1 || source->kind==APPLICATION_PROVIDER_Q2 ||
       source->kind==APPLICATION_PROVIDER_Q3 ||
       (source->kind==APPLICATION_PROVIDER_QC && source->product->family==QA_GAME_Q1)) {
        if(!application_source_intermission_read(source,&eligibility.intermission,error)) return false;
    } else if(source->product->family==QA_GAME_Q3 &&
              (source->kind==APPLICATION_PROVIDER_QVM || source->kind==APPLICATION_PROVIDER_NATIVE)) {
        struct application_q3_guest *engine=q3g_engine(source);
        if(!engine) return application_fail(error,QA_ERROR_NOT_FOUND,"Save policy lost its actual original Q3 gamestate");
        eligibility.intermission=!strcmp(qa_q3_configstring(&engine->gamestate,22),"1");
    }
    struct application_player_roster *roster=app->players;
    size_t count=0;
    for(size_t i=0;roster && i<roster->count;++i)
        if(selected_player(app,roster->records+i,dedicated)) ++count;
    if(count>SIZE_MAX/sizeof(float)) return application_fail(error,QA_ERROR_MEMORY,"Actual save player health extent exceeded");
    float *health=count?malloc(count*sizeof(*health)):NULL;
    if(count && !health) return application_fail(error,QA_ERROR_MEMORY,"Reading actual admitted save player health");
    bool okay=true;size_t index=0;
    for(size_t i=0;okay && roster && i<roster->count;++i) {
        application_player_record *record=roster->records+i;
        if(!selected_player(app,record,dedicated)) continue;
        eligibility.intermission=eligibility.intermission || control_intermission(app,record->actor);
        if(native_q2) okay=native_q2_player(source,record->actor,health+index,&eligibility.intermission,error);
        else if(eligibility.family==QA_GAME_Q3) health[index]=0;
        else {
            qa_combat_state combat;qa_error local={0};
            if(qa_combat_read(app->combat,record->actor,&combat,&local)) health[index]=combat.health;
            else if(local.code==QA_ERROR_NOT_FOUND) health[index]=0;
            else {if(error) *error=local;okay=false;}
        }
        ++index;
    }
    eligibility.player_health=health;eligibility.player_count=index;
    if(okay) okay=qa_save_eligible(&eligibility,purpose,error);
    free(health);return okay;
}
