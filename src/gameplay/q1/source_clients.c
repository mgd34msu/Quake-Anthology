#include "maps/internal.h"
#include "qa/game_q1_bots.h"
#include <stdio.h>
#include <ctype.h>

void q1_source_client_clear(q1_player *player) {
    free(player->source_info);player->source_info=NULL;player->source_info_count=0;
}
static const q1_player *client_const(const qa_q1_game *game,qa_actor_id actor) {
    if(!qa_q1_player_source_present(game,actor)) return NULL;
    const q1_player *player=game->players[actor.slot];return player->source_client?player:NULL;
}
static const char *info(const qa_q1_game *game,const q1_player *player,const char *key) {
    qa_strings *strings=qa_session_strings(game->services.session);
    for(size_t i=0;i<player->source_info_count;++i)
        if(!strcmp(qa_strings_cstr(strings,player->source_info[i].key),key))
            return qa_strings_cstr(strings,player->source_info[i].value);
    return NULL;
}
static size_t number_space(const unsigned char *text,size_t length) {
    if(!length) return 0;
    if(text[0]==' ' || (text[0]>=9 && text[0]<=13)) return 1;
    if(length>=2 && text[0]==0xc2 && text[1]==0xa0) return 2;
    if(length<3) return 0;
    if(text[0]==0xe1 && text[1]==0x9a && text[2]==0x80) return 3;
    if(text[0]==0xe2 && text[1]==0x80 &&
       ((text[2]>=0x80 && text[2]<=0x8a) || text[2]==0xa8 || text[2]==0xa9 || text[2]==0xaf)) return 3;
    if(text[0]==0xe2 && text[1]==0x81 && text[2]==0x9f) return 3;
    if(text[0]==0xe3 && text[1]==0x80 && text[2]==0x80) return 3;
    if(text[0]==0xef && text[1]==0xbb && text[2]==0xbf) return 3;
    return 0;
}
static uint8_t color(const char *text) {
    if(!text || !*text) return 0;
    size_t length=strlen(text);
    size_t width;
    while((width=number_space((const unsigned char *)text,length))!=0) {text+=width;length-=width;}
    size_t end=0;
    for(size_t offset=0;offset<length;) {
        width=number_space((const unsigned char *)text+offset,length-offset);
        if(width) offset+=width;
        else {++offset;end=offset;}
    }
    length=end;
    if(!length) return 0;
    double value=0;
    if(length>2 && text[0]=='0' && (text[1]=='x' || text[1]=='X' || text[1]=='b' || text[1]=='B' || text[1]=='o' || text[1]=='O')) {
        unsigned base=text[1]=='b' || text[1]=='B'?2:text[1]=='o' || text[1]=='O'?8:16;
        for(size_t i=2;i<length;++i) {
            unsigned char byte=(unsigned char)text[i];unsigned digit;
            if(byte>='0' && byte<='9') digit=byte-'0';
            else if(byte>='a' && byte<='f') digit=byte-'a'+10;
            else if(byte>='A' && byte<='F') digit=byte-'A'+10;
            else return 0;
            if(digit>=base) return 0;
            value=value*base+digit;
        }
    } else {
        for(size_t i=0;i<length;++i)
            if(!isdigit((unsigned char)text[i]) && text[i]!='.' && text[i]!='e' && text[i]!='E' && text[i]!='+' && text[i]!='-') return 0;
        char *parsed_end;value=strtod(text,&parsed_end);
        if(parsed_end==text || (size_t)(parsed_end-text)!=length) return 0;
    }
    if(!isfinite(value)) return 0;
    return value<=0?0:value>=13?13:(uint8_t)trunc(value);
}
bool qa_q1_source_client_info(const qa_q1_game *game,qa_actor_id actor,const char *key,const char **out) {
    const q1_player *player=client_const(game,actor);
    if(!player || !key || !out) return false;
    *out=info(game,player,key);return true;
}
bool qa_q1_source_client_read(const qa_q1_game *game,qa_actor_id actor,qa_q1_source_client_view *out) {
    const q1_player *player=client_const(game,actor);if(!player || !out) return false;
    const char *name=info(game,player,"name");
    *out=(qa_q1_source_client_view){.actor=actor,.slot=player->client_slot,.name=name && *name?name:"unconnected",
        .frags=player->source_frags,.team=player->source_team,.shirt=color(info(game,player,"topcolor")),
        .pants=color(info(game,player,"bottomcolor")),.observer=player->source_observer,
        .no_target=player->source_no_target,.god_mode=player->source_god_mode,.impulse=player->source_impulse,
        .use=player->source_use,.death_recorded=player->source_death_recorded,
        .respawn_requested_at=player->source_respawn_requested_at};return true;
}
static bool publish(qa_q1_game *game,q1_player *player,qa_error *error) {
    qa_q1_source_client_view view;
    if(!game->source_client_publish || !qa_q1_source_client_read(game,player->id,&view)) {
        qa_error_set(error,QA_ERROR_NOT_FOUND,0,"Q1 source client publication owner is absent");return false;
    }
    return game->source_client_publish(game->source_client_context,&view,error);
}
bool qa_q1_source_clients_configure(qa_q1_game *game,const qa_q1_source_client_services *services,qa_error *error) {
    if(!game || !services || !services->publish || !services->observer || game->destroy_pending || game->observation_depth) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q1 source clients require their actual publication and observer owners");return false;
    }
    game->source_client_context=services->context;game->source_client_publish=services->publish;
    game->source_client_observer=services->observer;return true;
}
static bool apply(qa_q1_game *game,q1_player *player,qa_error *error) {
    qa_team_id team=0;char number[32];const char *name=NULL;
    if(game->options.program==QA_Q1_CTF) name=player->source_team==5?"red":player->source_team==14?"blue":NULL;
    else if(player->source_team>0) {snprintf(number,sizeof(number),"%.9g",(double)player->source_team);name=number;}
    if(name && !qa_strings_intern_cstr(qa_session_strings(game->services.session),name,&team,error)) return false;
    qa_combat_state combat;
    if(!qa_combat_read_traits(game->services.combat,player->id,&combat,error)) return false;
    combat.team=team;
    if(!qa_combat_set_traits(game->services.combat,player->id,&combat,error)) return false;
    if(!q1_alive(game,player->id)) return true;
    const char *automatic=info(game,player,"qts_weapon_autoswitch");
    if(automatic) player->auto_switch=!strcmp(automatic,"new")?QA_Q1_SWITCH_NEW:
        !strcmp(automatic,"never")?QA_Q1_SWITCH_NEVER:QA_Q1_SWITCH_ALWAYS;
    return true;
}
static bool info_set(qa_q1_game *,q1_player *,const char *,const char *,qa_error *);
static bool userinfo(qa_q1_game *game,qa_actor_id actor,const char *text,const char *name,qa_error *error) {
    qa_q1_game_operation operation={0};
    if(!text || !qa_q1_game_operation_begin(game,&operation,error)) return false;
    q1_player *player=(q1_player *)client_const(game,actor);bool okay=false;
    if(!player) {qa_error_set(error,QA_ERROR_NOT_FOUND,0,"Q1 source userinfo client is absent");goto finish;}
    uint8_t previous=color(info(game,player,"bottomcolor"));
    q1_source_client_clear(player);
    const char *cursor=strchr(text,'\\');
    qa_strings *strings=qa_session_strings(game->services.session);
    while(cursor) {
        const char *key=cursor+1,*separator=strchr(key,'\\');if(!separator) break;
        const char *value=separator+1,*next=strchr(value,'\\');
        size_t key_size=(size_t)(separator-key),value_size=next?(size_t)(next-value):strlen(value);
        qa_string_id key_id,value_id;
        if(!qa_strings_intern(strings,(qa_bytes){(const uint8_t *)key,key_size},&key_id,error) ||
           !qa_strings_intern(strings,(qa_bytes){(const uint8_t *)value,value_size},&value_id,error)) goto finish;
        size_t index=0;while(index<player->source_info_count && player->source_info[index].key!=key_id) ++index;
        if(index==player->source_info_count) {
            if(index>=SIZE_MAX/sizeof(*player->source_info)) {qa_error_set(error,QA_ERROR_MEMORY,0,"Q1 source userinfo map exceeds its extent");goto finish;}
            q1_source_info *entries=realloc(player->source_info,(index+1)*sizeof(*entries));
            if(!entries) {qa_error_set(error,QA_ERROR_MEMORY,0,"growing actual Q1 source userinfo");goto finish;}
            player->source_info=entries;player->source_info_count=index+1;
        }
        player->source_info[index]=(q1_source_info){key_id,value_id};cursor=next;
    }
    if(name && !info(game,player,"name") && !info_set(game,player,"name",name,error)) goto finish;
    if(color(info(game,player,"bottomcolor"))!=previous) player->source_team=(float)color(info(game,player,"bottomcolor"))+1;
    okay=apply(game,player,error) && (!q1_alive(game,actor) || publish(game,player,error));
finish:
    qa_q1_game_operation_end(&operation);return okay;
}
bool qa_q1_source_client_userinfo(qa_q1_game *game,qa_actor_id actor,const char *text,qa_error *error) {
    return userinfo(game,actor,text,NULL,error);
}
bool qa_q1_source_client_userinfo_storage(qa_q1_game *game,qa_actor_id actor,const char *text,qa_error *error) {
    qa_q1_game_operation operation={0};
    if(!text||!qa_q1_game_operation_begin(game,&operation,error)) return false;
    q1_player *player=(q1_player *)client_const(game,actor);
    q1_source_info *entries=NULL;size_t count=0;bool okay=false;
    if(!player) {qa_error_set(error,QA_ERROR_NOT_FOUND,0,"Q1 stored userinfo client is absent");goto finish;}
    qa_strings *strings=qa_session_strings(game->services.session);
    const char *cursor=text+(*text=='\\');
    while(*cursor) {
        const char *separator=strchr(cursor,'\\');if(!separator) break;
        const char *value=separator+1,*next=strchr(value,'\\');
        qa_string_id key_id,value_id;
        if(!qa_strings_intern(strings,(qa_bytes){(const uint8_t *)cursor,(size_t)(separator-cursor)},&key_id,error)||
            !qa_strings_intern(strings,(qa_bytes){(const uint8_t *)value,next?(size_t)(next-value):strlen(value)},&value_id,error)) goto finish;
        size_t index=0;while(index<count&&entries[index].key!=key_id) ++index;
        if(index==count) {
            if(count>=SIZE_MAX/sizeof(*entries)) {qa_error_set(error,QA_ERROR_MEMORY,0,"Q1 stored userinfo extent overflows");goto finish;}
            q1_source_info *grown=realloc(entries,(count+1)*sizeof(*entries));
            if(!grown) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining Q1 stored userinfo dictionary");goto finish;}
            entries=grown;entries[count++]=(q1_source_info){key_id,value_id};
        }
        if(!next) break;
        cursor=next+1;
    }
    q1_source_client_clear(player);player->source_info=entries;player->source_info_count=count;
    entries=NULL;okay=true;
finish:
    free(entries);qa_q1_game_operation_end(&operation);return okay;
}
bool qa_q1_source_client_userinfo_named(qa_q1_game *game,qa_actor_id actor,const char *text,
    const char *name,qa_error *error) {
    if(!name) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q1 source admission lost its actual name");return false;}
    return userinfo(game,actor,text,name,error);
}
bool qa_q1_source_client_add_score(qa_q1_game *game,qa_actor_id actor,double delta,qa_error *error) {
    qa_q1_game_operation operation={0};if(!qa_q1_game_operation_begin(game,&operation,error)) return false;
    q1_player *player=(q1_player *)client_const(game,actor);bool okay=false;
    if(!player) qa_error_set(error,QA_ERROR_NOT_FOUND,0,"Q1 source score client is absent");
    else {volatile float score=(float)((double)player->source_frags+delta);player->source_frags=score;okay=publish(game,player,error);}
    qa_q1_game_operation_end(&operation);return okay;
}
bool qa_q1_source_client_set_score(qa_q1_game *game,qa_actor_id actor,float score,qa_error *error) {
    qa_q1_game_operation operation={0};if(!qa_q1_game_operation_begin(game,&operation,error)) return false;
    q1_player *player=(q1_player *)client_const(game,actor);bool okay=false;
    if(!player) qa_error_set(error,QA_ERROR_NOT_FOUND,0,"Q1 source score client is absent");
    else {player->source_frags=score;okay=publish(game,player,error);}
    qa_q1_game_operation_end(&operation);return okay;
}
bool qa_q1_source_client_observer(qa_q1_game *game,qa_actor_id actor,bool observer,qa_error *error) {
    qa_q1_game_operation operation={0};if(!qa_q1_game_operation_begin(game,&operation,error)) return false;
    q1_player *player=(q1_player *)client_const(game,actor);bool okay=false;
    if(!player || !game->source_client_observer) qa_error_set(error,QA_ERROR_NOT_FOUND,0,"Q1 source observer owner is absent");
    else {player->source_observer=observer;okay=game->source_client_observer(game->source_client_context,actor,observer,error) &&
        (!q1_alive(game,actor) || publish(game,player,error));}
    qa_q1_game_operation_end(&operation);return okay;
}
bool qa_q1_source_client_spawned(qa_q1_game *game,qa_actor_id actor,qa_error *error) {
    qa_q1_game_operation operation={0};if(!qa_q1_game_operation_begin(game,&operation,error)) return false;
    q1_player *player=(q1_player *)client_const(game,actor);bool okay=false;
    if(!player) qa_error_set(error,QA_ERROR_NOT_FOUND,0,"Q1 source spawned client is absent");
    else {
        player->source_death_recorded=false;
        player->source_impulse=0;
        player->source_use=false;
        player->source_respawn_requested_at=-1;
        player->source_god_mode=false;
        if(game->options.edition==QA_Q1_RERELEASE && game->options.program!=QA_Q1_CTF) {
            if(game->options.coop) player->source_team=1;
            else if(game->options.program==QA_Q1_ID1) player->source_team=-1;
        }
        okay=apply(game,player,error) && (!q1_alive(game,actor) || publish(game,player,error));
    }
    qa_q1_game_operation_end(&operation);return okay;
}
bool qa_q1_source_client_consume_impulse(qa_q1_game *game,qa_actor_id actor,qa_error *error) {
    qa_q1_game_operation operation={0};
    if(!qa_q1_game_operation_begin(game,&operation,error)) return false;
    q1_player *player=(q1_player *)client_const(game,actor);
    bool okay=player!=NULL;
    if(okay) {player->source_impulse=0;player->input.impulse=0;}
    else qa_error_set(error,QA_ERROR_NOT_FOUND,actor.slot,"Source impulse client is absent");
    qa_q1_game_operation_end(&operation);return okay;
}
bool qa_q1_source_client_record_death(qa_q1_game *game,qa_actor_id actor,bool *first,qa_error *error) {
    if(!first) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source death needs its first-record output");return false;}
    qa_q1_game_operation operation={0};
    if(!qa_q1_game_operation_begin(game,&operation,error)) return false;
    q1_player *player=(q1_player *)client_const(game,actor);
    uint32_t slot;
    bool okay=player && qa_q1_native_client_slot(game,actor,&slot,error) &&
        slot==player->client_slot;
    if(okay) {*first=!player->source_death_recorded;player->source_death_recorded=true;}
    else if(!player) qa_error_set(error,QA_ERROR_NOT_FOUND,actor.slot,"Source death client is absent");
    else if(!error || error->code==QA_OK) qa_error_set(error,QA_ERROR_ARGUMENT,actor.slot,
        "Source death differs from its real physical client slot");
    qa_q1_game_operation_end(&operation);return okay;
}
bool qa_q1_source_respawn_options_read(const qa_q1_game *game,qa_q1_options *out,
    double *source_seconds,qa_error *error) {
    if(!game || !out || !source_seconds || game->destroy_pending || game->continuation_pending) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q1 respawn has no current native source options");return false;
    }
    *out=game->options;*source_seconds=game->time;return true;
}
bool qa_q1_source_respawn_options_prepared(const qa_q1_game *game,qa_q1_options *out,
    qa_error *error) {
    if(!game || !out || game->destroy_pending || !game->continuation_pending) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q1 selector has no imported pending source options");return false;
    }
    *out=game->options;return true;
}
bool qa_q1_source_client_request_respawn(qa_q1_game *game,qa_actor_id actor,
    bool *force_spawn,qa_error *error) {
    if(!force_spawn) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q1 respawn requires its source decision output");return false;}
    qa_q1_game_operation operation={0};
    if(!qa_q1_game_operation_begin(game,&operation,error)) return false;
    q1_player *player=(q1_player *)client_const(game,actor);bool okay=false;
    if(!player) qa_error_set(error,QA_ERROR_NOT_FOUND,0,"Q1 respawn source client is absent");
    else {
        if(player->source_respawn_requested_at<0) player->source_respawn_requested_at=game->time;
        *force_spawn=game->time>=player->source_respawn_requested_at+5;okay=true;
    }
    qa_q1_game_operation_end(&operation);return okay;
}
bool qa_q1_bot_exit_level(qa_q1_game *game,double seconds,bool same_level,qa_error *error) {
    if(!game || !game->maps || !game->maps->options.level) {qa_error_set(error,QA_ERROR_NOT_FOUND,0,"Q1 bot ExitLevel lacks its actual source level owner");return false;}
    qa_q1_intermission_result result;
    return qa_q1_level_request_exit(game->maps->options.level,seconds,true,same_level,&result,error);
}
static bool info_set(qa_q1_game *game,q1_player *player,const char *key,const char *value,qa_error *error) {
    qa_strings *strings=qa_session_strings(game->services.session);
    qa_string_id key_id,value_id;
    if(!qa_strings_intern_cstr(strings,key,&key_id,error) ||
       !qa_strings_intern_cstr(strings,value,&value_id,error)) return false;
    size_t index=0;
    while(index<player->source_info_count && player->source_info[index].key!=key_id) ++index;
    if(index==player->source_info_count) {
        if(index>=SIZE_MAX/sizeof(*player->source_info)) {
            qa_error_set(error,QA_ERROR_MEMORY,0,"Q1 source userinfo map exceeds its extent");return false;
        }
        q1_source_info *entries=realloc(player->source_info,(index+1)*sizeof(*entries));
        if(!entries) {qa_error_set(error,QA_ERROR_MEMORY,0,"growing actual Q1 source userinfo");return false;}
        player->source_info=entries;player->source_info_count=index+1;
    }
    player->source_info[index]=(q1_source_info){key_id,value_id};return true;
}
bool qa_q1_source_client_userinfo_read(const qa_q1_game *game,qa_actor_id actor,bool include_name,qa_buffer *out,qa_error *error) {
    const q1_player *player=client_const(game,actor);
    if(!player || !out || out->data || out->size) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q1 source userinfo read requires its client and empty output");return false;
    }
    qa_strings *strings=qa_session_strings(game->services.session);size_t length=0;
    for(size_t i=0;i<player->source_info_count;++i) {
        const char *key=qa_strings_cstr(strings,player->source_info[i].key);
        const char *value=qa_strings_cstr(strings,player->source_info[i].value);
        if(!key || !value) {qa_error_set(error,QA_ERROR_FORMAT,0,"Q1 source userinfo lost its actual string");return false;}
        if(!include_name && !strcmp(key,"name")) continue;
        size_t key_size=strlen(key),value_size=strlen(value);
        if(length>SIZE_MAX-2 || key_size>SIZE_MAX-length-2 || value_size>SIZE_MAX-length-key_size-2) {
            qa_error_set(error,QA_ERROR_MEMORY,0,"Q1 source userinfo extent overflows");return false;
        }
        length+=key_size+value_size+2;
    }
    if(length==SIZE_MAX) {qa_error_set(error,QA_ERROR_MEMORY,0,"Q1 source userinfo extent overflows");return false;}
    uint8_t *bytes=malloc(length+1);
    if(!bytes) {qa_error_set(error,QA_ERROR_MEMORY,0,"reading actual Q1 source userinfo");return false;}
    size_t cursor=0;
    for(size_t i=0;i<player->source_info_count;++i) {
        const char *key=qa_strings_cstr(strings,player->source_info[i].key);
        const char *value=qa_strings_cstr(strings,player->source_info[i].value);
        if(!include_name && !strcmp(key,"name")) continue;
        size_t key_size=strlen(key),value_size=strlen(value);
        bytes[cursor++]='\\';memcpy(bytes+cursor,key,key_size);cursor+=key_size;
        bytes[cursor++]='\\';memcpy(bytes+cursor,value,value_size);cursor+=value_size;
    }
    bytes[cursor]=0;*out=(qa_buffer){bytes,length};return true;
}
bool qa_q1_source_client_name(qa_q1_game *game,qa_actor_id actor,const char *name,qa_error *error) {
    qa_q1_game_operation operation={0};
    if(!name || !qa_q1_game_operation_begin(game,&operation,error)) return false;
    q1_player *player=(q1_player *)client_const(game,actor);bool okay=false;
    if(!player) qa_error_set(error,QA_ERROR_NOT_FOUND,0,"Q1 source name client is absent");
    else {
        okay=info_set(game,player,"name",name,error) && publish(game,player,error);
    }
    qa_q1_game_operation_end(&operation);return okay;
}
bool qa_q1_source_client_colors(qa_q1_game *game,qa_actor_id actor,int32_t top,int32_t bottom,qa_error *error) {
    qa_q1_game_operation operation={0};
    if(!qa_q1_game_operation_begin(game,&operation,error)) return false;
    q1_player *player=(q1_player *)client_const(game,actor);bool okay=false;
    if(!player) qa_error_set(error,QA_ERROR_NOT_FOUND,0,"Q1 source colors client is absent");
    else {
        if(top<0) top=0;else if(top>13) top=13;
        if(bottom<0) bottom=0;else if(bottom>13) bottom=13;
        char shirt[4],pants[4];snprintf(shirt,sizeof(shirt),"%d",top);snprintf(pants,sizeof(pants),"%d",bottom);
        if(info_set(game,player,"topcolor",shirt,error) && info_set(game,player,"bottomcolor",pants,error)) {
            player->source_team=(float)bottom+1;
            okay=apply(game,player,error) && (!q1_alive(game,actor) || publish(game,player,error));
        }
    }
    qa_q1_game_operation_end(&operation);return okay;
}
