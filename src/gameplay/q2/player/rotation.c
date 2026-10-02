#include "../entities/internal.h"

static bool same_map(const char *first,const char *second) {
    while(*first && *second) {
        unsigned char a=(unsigned char)*first++,b=(unsigned char)*second++;
        if(a>='A' && a<='Z') a=(unsigned char)(a+'a'-'A');
        if(b>='A' && b<='Z') b=(unsigned char)(b+'a'-'A');
        if(a!=b) return false;
    }
    return !*first && !*second;
}

bool qa_q2_players_end_deathmatch_level(qa_q2_game *game,qa_error *error) {
    if(!game || !game->player_runtime) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 deathmatch level end requires its actual player rules");return false;}
    q2_players *players=game->player_runtime;qa_strings *strings=qa_session_strings(game->services.session);
    const char *current=players->rules.map_name?players->rules.map_name:"",*next=current;
    if(!(game->options.deathmatch_flags&32)) {
        size_t count=players->rules.map_list_count,index=0;
        while(index<count && !same_map(qa_strings_cstr(strings,players->rotation_maps[index]),current)) ++index;
        if(index<count) {
            if(game->options.edition==QA_Q2_RERELEASE && players->rules.map_list_shuffle && count>1 && index==count-1) {
                qa_string_id *shuffled=malloc(count*sizeof(*shuffled));
                if(!shuffled) {qa_error_set(error,QA_ERROR_MEMORY,0,"Copying Q2 source rotation for shuffle");return false;}
                memcpy(shuffled,players->rotation_maps,count*sizeof(*shuffled));
                for(size_t i=count-1;i>0;--i) {
                    size_t selected=q2_random_bounded(game,(uint32_t)(i+1));
                    qa_string_id swap=shuffled[i];shuffled[i]=shuffled[selected];shuffled[selected]=swap;
                }
                if(!strcmp(qa_strings_cstr(strings,shuffled[0]),current)) {
                    qa_string_id swap=shuffled[0];shuffled[0]=shuffled[count-1];shuffled[count-1]=swap;
                }
                qa_string_id first=shuffled[0];
                if(game->hooks.rotation_changed) {
                    bool published=game->hooks.rotation_changed(game->hooks.context,shuffled,count,error);
                    free(shuffled);
                    if(!published) return false;
                } else {
                    free(players->rotation_maps);players->rotation_maps=shuffled;players->rules.map_list=shuffled;
                }
                next=qa_strings_cstr(strings,first);
            } else if(game->options.edition!=QA_Q2_RERELEASE || count!=1)
                next=qa_strings_cstr(strings,players->rotation_maps[(index+1)%count]);
        } else {
            const char *configured=qa_strings_cstr(strings,players->rules.next_map);
            if(configured && *configured) next=configured;
            else {
                qa_actor_id changelevel;
                if(q2_map_find(game,"target_changelevel",UINT32_MAX,0,&changelevel)) {
                    q2_actor *source=q2_actor_get(game,changelevel,false,NULL);
                    const char *authored=source && source->entity?qa_strings_cstr(strings,source->entity->map):NULL;
                    if(authored && *authored) next=authored;
                }
            }
        }
    }
    return qa_q2_players_intermission(game,next,NULL,0,error);
}
