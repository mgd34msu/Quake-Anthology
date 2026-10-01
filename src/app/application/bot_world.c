#include "bot_world.h"
#include "../../bots/save_fields.h"
#include <limits.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct bot_world_row { qa_actor_id actor; uint64_t generation; } bot_world_row;
typedef struct bot_world_text { uint32_t id; char *text; } bot_world_text;
struct application_bot_world {
    application_bot_world_services services;
    bot_world_row rows[APPLICATION_BOT_WORLD_CAPACITY];
    uint32_t order[APPLICATION_BOT_WORLD_CAPACITY],free_ids[APPLICATION_BOT_WORLD_CAPACITY];
    uint32_t order_count,free_count,next_entity;
    uint64_t next_generation;
    bool *begun;
    bot_world_text *userinfos,*strings,*models;
    size_t userinfo_count,string_count,model_count;
    uint8_t memory[APPLICATION_BOT_MEMORY_BYTES];
    uint32_t alloc_point;
    size_t calls,drop_depth;
    bool restoring;
};
static bool fail(qa_error *error,qa_status code,const char *text) {
    qa_error_set(error,code,0,"%s",text);return false;
}
static bool idle(application_bot_world *world,qa_error *error) {
    return world && world->calls<SIZE_MAX && !world->restoring ? true:
        fail(error,QA_ERROR_ARGUMENT,"shared bot sensory owner is absent, restoring or executing");
}
static bool live(const application_bot_world *world,qa_actor_id actor) {
    return qa_actors_get(qa_session_actors(world->services.session),actor)!=NULL;
}
static bot_world_row *row(application_bot_world *world,uint32_t id) {
    return id>=64 && id<1022?world->rows+id-64:NULL;
}
static void retire(application_bot_world *world) {
    uint32_t retained=0;
    for(uint32_t i=0;i<world->order_count;++i) {
        uint32_t id=world->order[i];bot_world_row *entry=row(world,id);
        if(live(world,entry->actor)) world->order[retained++]=id;
        else {entry->actor=(qa_actor_id){0};world->free_ids[world->free_count++]=id;}
    }
    world->order_count=retained;
}
static bool entity_id(application_bot_world *world,qa_actor_id actor,int32_t *out,qa_error *error) {
    if(!live(world,actor)) return fail(error,QA_ERROR_NOT_FOUND,"shared bot entity identity requires its full live actor");
    application_bot_world_movement movement;bool found;
    if(!world->services.movement(world->services.context,actor,&movement,&found,error)) return false;
    if(found) {
        if(movement.source_client<0 || !live(world,actor))
            return fail(error,QA_ERROR_FORMAT,"shared bot movement client lost its source identity");
        *out=movement.source_client;return true;
    }
    application_bot_world_metadata metadata;
    if(!world->services.metadata(world->services.context,actor,&metadata,&found,error)) return false;
    if(found && metadata.worldspawn) {*out=1022;return true;}
    for(uint32_t i=0;i<world->order_count;++i) {
        uint32_t id=world->order[i];
        if(qa_actor_id_equal(row(world,id)->actor,actor)) {*out=(int32_t)id;return true;}
    }
    retire(world);
    uint32_t id=world->free_count?world->free_ids[world->free_count-1]:world->next_entity;
    if(id>=1022) return fail(error,QA_ERROR_ARGUMENT,"live bot observations exceed the simultaneous sensory capacity");
    if(world->next_generation>=UINT64_C(9007199254740991))
        return fail(error,QA_ERROR_ARGUMENT,"shared bot observation generation exceeds its source integer domain");
    if(!live(world,actor)) return fail(error,QA_ERROR_NOT_FOUND,"shared bot actor retired during namespace admission");
    if(world->free_count) --world->free_count;
    else ++world->next_entity;
    *row(world,id)=(bot_world_row){.actor=actor,.generation=world->next_generation++};
    world->order[world->order_count++]=id;*out=(int32_t)id;return true;
}
static bool actor_for_id(application_bot_world *world,int32_t number,qa_actor_id *out,qa_error *error) {
    *out=(qa_actor_id){0};
    if(number<0) return true;
    if(number<64) {
        uint32_t cursor=0;const qa_actor_record *record;
        while(qa_actors_next(qa_session_actors(world->services.session),&cursor,&record)) {
            application_bot_world_movement movement;application_bot_world_client client;
            bool moving,has_client;qa_actor_id actor=record->id;
            if(!world->services.client(world->services.context,actor,&client,&has_client,error)) return false;
            if(!has_client) continue;
            if(!world->services.movement(world->services.context,actor,&movement,&moving,error)) return false;
            if(moving && movement.source_client==number && live(world,actor)) {*out=actor;return true;}
        }
    } else if(number==1022) *out=world->services.world_actor(world->services.context);
    else if(number<1022) *out=row(world,(uint32_t)number)->actor;
    if(out->registry && !live(world,*out)) *out=(qa_actor_id){0};
    return true;
}
static char *copy(const char *text,qa_error *error) {
    if(!text) {fail(error,QA_ERROR_ARGUMENT,"shared bot private text is absent");return NULL;}
    size_t size=strlen(text)+1;char *out=malloc(size);
    if(!out) {fail(error,QA_ERROR_MEMORY,"allocating private shared bot text");return NULL;}
    memcpy(out,text,size);return out;
}
static const char *text_find(const bot_world_text *table,size_t count,uint32_t id) {
    for(size_t i=0;i<count;++i) if(table[i].id==id) return table[i].text;
    return "";
}
static bool text_set(bot_world_text **table,size_t *count,uint32_t id,const char *value,qa_error *error) {
    char *owned=copy(value,error);if(!owned) return false;
    for(size_t i=0;i<*count;++i) if((*table)[i].id==id) {
        free((*table)[i].text);(*table)[i].text=owned;return true;
    }
    if(*count>=SIZE_MAX/sizeof(**table)) {free(owned);return fail(error,QA_ERROR_MEMORY,"shared bot text table exceeds its extent");}
    bot_world_text *next=realloc(*table,(*count+1)*sizeof(*next));
    if(!next) {free(owned);return fail(error,QA_ERROR_MEMORY,"growing shared bot private text table");}
    *table=next;next[(*count)++]=(bot_world_text){.id=id,.text=owned};return true;
}
static void text_free(bot_world_text *table,size_t count) {
    for(size_t i=0;i<count;++i) free(table[i].text);
    free(table);
}
static size_t source_space(const unsigned char *text,size_t length) {
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
static double inline_index(const char *name) {
    const char *start=name+1;size_t length=strlen(start),width;
    while((width=source_space((const unsigned char *)start,length))!=0) {start+=width;length-=width;}
    size_t end_offset=0;
    for(size_t offset=0;offset<length;) {
        width=source_space((const unsigned char *)start+offset,length-offset);
        if(width) offset+=width;
        else {++offset;end_offset=offset;}
    }
    length=end_offset;
    if(!length) return 0;
    if((length==8 && !memcmp(start,"Infinity",8)) || (length==9 && !memcmp(start,"+Infinity",9))) return INFINITY;
    if(length==9 && !memcmp(start,"-Infinity",9)) return -INFINITY;
    if(length>2 && start[0]=='0' && (start[1]=='x' || start[1]=='X' || start[1]=='b' || start[1]=='B' || start[1]=='o' || start[1]=='O')) {
        unsigned base=start[1]=='b' || start[1]=='B'?2:start[1]=='o' || start[1]=='O'?8:16;
        double value=0;
        for(size_t i=2;i<length;++i) {
            unsigned char c=(unsigned char)start[i];unsigned digit;
            if(c>='0' && c<='9') digit=c-'0';
            else if(c>='a' && c<='f') digit=c-'a'+10;
            else if(c>='A' && c<='F') digit=c-'A'+10;
            else return NAN;
            if(digit>=base) return NAN;
            value=value*base+digit;
        }
        return value;
    }
    /* strtod's inf/nan and hexadecimal extensions are absent from Number. */
    size_t at=start[0]=='+' || start[0]=='-'?1:0;
    if(at>=length || (!isdigit((unsigned char)start[at]) && start[at]!='.')) return NAN;
    for(size_t i=at;i<length;++i)
        if(!isdigit((unsigned char)start[i]) && start[i]!='.' && start[i]!='e' && start[i]!='E' && start[i]!='+' && start[i]!='-') return NAN;
    char *end;double value=strtod(start,&end);
    return (size_t)(end-start)==length?value:NAN;
}
static int32_t source_i32(double value) {
    if(!isfinite(value) || !value) return 0;
    double reduced=fmod(trunc(value),4294967296.0);if(reduced<0) reduced+=4294967296.0;
    uint32_t bits=(uint32_t)reduced;int32_t result;memcpy(&result,&bits,sizeof(result));return result;
}
static bool model_index(application_bot_world *world,const char *name,int32_t *out,qa_error *error) {
    if(!name || !out) return fail(error,QA_ERROR_ARGUMENT,"shared bot model query requires actual source text");
    if(!*name) {*out=0;return true;}
    if(*name=='*') {*out=source_i32(inline_index(name));return true;}
    for(size_t i=0;i<world->model_count;++i) if(!strcmp(world->models[i].text,name)) {
        *out=(int32_t)world->models[i].id;return true;
    }
    uint64_t id=(uint64_t)world->services.base_model_count+world->model_count;
    if(id>INT32_MAX) return fail(error,QA_ERROR_ARGUMENT,"shared bot model registry exceeds its source index domain");
    if(!text_set(&world->models,&world->model_count,(uint32_t)id,name,error)) return false;
    *out=(int32_t)id;return true;
}
bool application_bot_world_create(const application_bot_world_services *services,bool restoring,
    application_bot_world **out,qa_error *error) {
    if(!services || !out || *out || !services->session || !services->world ||
       services->source>APPLICATION_BOT_WORLD_Q2 || !services->metadata || !services->movement ||
       !services->client || !services->combat || !services->brush || !services->weapon ||
       !services->world_actor || !services->bot_actor || !services->clock || services->max_clients>INT32_MAX)
        return fail(error,QA_ERROR_ARGUMENT,"shared bot world requires its actual Q1 or Q2 sensory owners");
    if(!services->print || !services->memory_debug || !services->userinfo_changed || !services->bot_connect ||
       !services->bot_begin || !services->bot_drop || !services->exit_level || !services->console ||
       !services->message || !services->random ||
       (services->source==APPLICATION_BOT_WORLD_Q2 && (!services->q2_connect || !services->q2_activate)))
        return fail(error,QA_ERROR_ARGUMENT,"shared bot world requires actual source admission and effect owners");
    application_bot_world *world=calloc(1,sizeof(*world));
    if(!world) return fail(error,QA_ERROR_MEMORY,"allocating independent shared bot sensory owner");
    world->services=*services;world->restoring=restoring;world->next_entity=64;world->next_generation=1;
    world->begun=services->max_clients?calloc(services->max_clients,sizeof(*world->begun)):NULL;
    if(services->max_clients && !world->begun) {free(world);return fail(error,QA_ERROR_MEMORY,"allocating shared bot begun-client set");}
    *out=world;
    if(restoring || application_bot_world_refresh(world,error)) return true;
    application_bot_world_destroy(world,NULL);*out=NULL;return false;
}
bool application_bot_world_can_destroy(const application_bot_world *world) {return !world || !world->calls;}
bool application_bot_world_destroy(application_bot_world *world,qa_error *error) {
    if(!world) return true;
    if(world->calls) return fail(error,QA_ERROR_ARGUMENT,"shared bot world is executing a source observation");
    text_free(world->userinfos,world->userinfo_count);text_free(world->strings,world->string_count);
    text_free(world->models,world->model_count);free(world->begun);free(world);return true;
}
bool application_bot_world_refresh(application_bot_world *world,qa_error *error) {
    if(!idle(world,error)) return false;
    ++world->calls;retire(world);
    uint32_t cursor=0;const qa_actor_record *record;bool okay=true;
    while(okay && qa_actors_next(qa_session_actors(world->services.session),&cursor,&record)) {
        qa_actor_id actor=record->id;qa_body_state body;qa_error local={0};
        if(!qa_world_body_storage_serial(world->services.world,actor)) continue;
        if(!qa_world_body_read(world->services.world,actor,&body,&local)) {
            if(local.code) {if(error) *error=local;okay=false;}
            continue;
        }
        if(!live(world,actor)) continue;
        int32_t number;okay=entity_id(world,actor,&number,error);
    }
    --world->calls;return okay;
}
bool application_bot_world_entity_id(application_bot_world *world,qa_actor_id actor,int32_t *out,qa_error *error) {
    if(!idle(world,error) || !out) return false;
    ++world->calls;bool okay=entity_id(world,actor,out,error);--world->calls;return okay;
}
bool application_bot_world_actor(application_bot_world *world,int32_t number,qa_actor_id *out,qa_error *error) {
    if(!idle(world,error) || !out) return false;
    ++world->calls;bool okay=actor_for_id(world,number,out,error);--world->calls;return okay;
}
static void vector(float out[3],qa_vec3 value) {out[0]=value.x;out[1]=value.y;out[2]=value.z;}
bool application_bot_world_read(application_bot_world *world,int32_t number,
    application_bot_world_entity *out,qa_error *error) {
    if(!idle(world,error) || !out) return false;
    ++world->calls;
    application_bot_world_entity observed={.state.number=number};
    qa_actor_id actor;
    bool okay=actor_for_id(world,number,&actor,error);
    application_bot_world_metadata metadata={0};application_bot_world_movement movement={0};
    application_bot_world_client client={0};application_bot_world_combat combat={0};
    bool has_metadata=false,has_movement=false,has_client=false,has_combat=false,brush=false,has_body=false;
    qa_body_state body={0};
    if(okay && actor.registry) {
        observed.actor=actor;
        okay=world->services.metadata(world->services.context,actor,&metadata,&has_metadata,error);
        if(okay && qa_world_body_storage_serial(world->services.world,actor)) {
            okay=qa_world_body_read(world->services.world,actor,&body,error);has_body=okay;
        }
        if(okay) okay=world->services.movement(world->services.context,actor,&movement,&has_movement,error);
        if(okay) okay=world->services.client(world->services.context,actor,&client,&has_client,error);
        if(okay) okay=world->services.brush(world->services.context,actor,&brush,error);
        if(okay && has_body && has_movement && has_client) {
            okay=world->services.combat(world->services.context,actor,&combat,&has_combat,error);
            if(okay && (!client.name || !client.skin)) okay=fail(error,QA_ERROR_FORMAT,"shared bot player has no source client metadata");
            qa_q3_player *ps=&observed.player;
            ps->product=QA_Q3_ARENA;ps->clientNum=movement.source_client;
            vector(ps->origin,body.origin);vector(ps->velocity,body.velocity);vector(ps->viewangles,movement.view_angles);
            ps->viewheight=movement.view_height;ps->groundEntityNum=1023;
            if(okay && body.ground.registry) okay=entity_id(world,body.ground,&ps->groundEntityNum,error);
            ps->pmType=client.spectator?2:(!has_combat || combat.health<=0)?3:0;
            if(okay) okay=world->services.weapon(world->services.context,actor,&ps->weapon,&ps->weaponState,error);
            ps->stats[0]=has_combat?combat.health:0;ps->stats[3]=has_combat?combat.armor:0;
            ps->stats[6]=has_metadata?metadata.max_health:100;
            ps->persistant[0]=client.score;ps->persistant[3]=client.spectator?3:0;
            observed.has_player=true;observed.team=ps->persistant[3];observed.name=client.name;
            qa_actor_id bot=world->services.bot_actor(world->services.context,number);
            observed.connected=!bot.registry || (number>=0 && (uint32_t)number<world->services.max_clients && world->begun[number]);
            observed.state.eType=1;observed.state.weapon=ps->weapon;
            vector(observed.state.pos.base,body.origin);vector(observed.state.apos.base,movement.view_angles);
            observed.state.groundEntityNum=ps->groundEntityNum;
        } else observed.state.eType=brush?4:0;
        observed.origin=body.origin;observed.angles=body.angles;observed.bounds=body.bounds;
        vector(observed.state.origin,body.origin);
        if(okay && has_metadata) {
            okay=model_index(world,metadata.model,&observed.state.modelindex,error);
            observed.state.frame=metadata.frame;observed.classname=metadata.classname;observed.hidden=metadata.hidden;
            if(okay && metadata.model[0]=='*') {observed.inline_model=inline_index(metadata.model);observed.has_inline_model=true;}
        }
        observed.present=live(world,actor) && has_body;
        qa_linked_body linked;
        observed.linked=qa_world_linked(world->services.world,actor,&linked);
        observed.bot=qa_actor_id_equal(world->services.bot_actor(world->services.context,number),actor);
        observed.contents=brush?1:observed.has_player?0x2000000:0;
        if(number<64) observed.generation=actor.generation;
        else if(number>=64 && number<1022) observed.generation=row(world,(uint32_t)number)->generation;
    } else if(okay && number>=64 && number<1022) observed.generation=row(world,(uint32_t)number)->generation;
    if(okay) *out=observed;
    --world->calls;return okay;
}
uint32_t application_bot_world_entity_count(const application_bot_world *world) {return world?world->next_entity:0;}
uint32_t application_bot_world_max_clients(const application_bot_world *world) {return world?world->services.max_clients:0;}
bool application_bot_world_clock(application_bot_world *world,int32_t *time,int32_t *intermission,qa_error *error) {
    if(!idle(world,error) || !time || !intermission) return false;
    ++world->calls;bool okay=world->services.clock(world->services.context,time,intermission,error);--world->calls;return okay;
}
bool application_bot_world_begin(application_bot_world *world,uint32_t client,qa_error *error) {
    if(!idle(world,error)) return false;
    if(client>=world->services.max_clients) return fail(error,QA_ERROR_ARGUMENT,"shared bot begun client is outside actual maxclients");
    world->begun[client]=true;++world->calls;
    bool okay=world->services.bot_begin(world->services.context,client,error);
    --world->calls;return okay;
}
static bool userinfo_changed(application_bot_world *world,uint32_t client,qa_error *error) {
    qa_actor_id actor=world->services.bot_actor(world->services.context,(int32_t)client);
    return !actor.registry || world->services.userinfo_changed(world->services.context,actor,
        text_find(world->userinfos,world->userinfo_count,client),error);
}
bool application_bot_world_userinfo_changed(application_bot_world *world,uint32_t client,qa_error *error) {
    if(!idle(world,error) || client>INT32_MAX) return false;
    ++world->calls;bool okay=userinfo_changed(world,client,error);--world->calls;return okay;
}
bool application_bot_world_connect_client(application_bot_world *world,uint32_t client,bool first_time,bool bot,
    const char **rejection,qa_error *error) {
    if(!idle(world,error) || !rejection || client>INT32_MAX) return false;
    ++world->calls;bool okay=true;const char *reason=NULL;
    if(world->services.source==APPLICATION_BOT_WORLD_Q2) {
        application_bot_world_connect result={0};
        okay=world->services.q2_connect(world->services.context,
            text_find(world->userinfos,world->userinfo_count,client),bot,&result,error);
        if(okay && !result.allowed) {
            if(!result.reason) okay=fail(error,QA_ERROR_FORMAT,"Q2 source rejection has no actual reason");
            else reason=result.reason;
        } else if(okay) okay=text_set(&world->userinfos,&world->userinfo_count,client,result.userinfo,error);
    }
    if(okay && !reason) okay=userinfo_changed(world,client,error);
    if(okay && !reason) {
        bool accepted=false;okay=world->services.bot_connect(world->services.context,client,!first_time,&accepted,error);
        if(okay && !accepted) reason="Bot setup failed";
    }
    if(okay) *rejection=reason;
    --world->calls;return okay;
}
bool application_bot_world_activate(application_bot_world *world,uint32_t client,qa_error *error) {
    if(!idle(world,error) || client>INT32_MAX) return false;
    if(world->services.source==APPLICATION_BOT_WORLD_Q1) return true;
    ++world->calls;qa_actor_id actor=world->services.bot_actor(world->services.context,(int32_t)client);
    bool okay=!actor.registry || world->services.q2_activate(world->services.context,actor,error);
    --world->calls;return okay;
}
bool application_bot_world_drop(application_bot_world *world,uint32_t client,const char *reason,qa_error *error) {
    if(!idle(world,error) || !reason || world->drop_depth==SIZE_MAX) return false;
    ++world->calls;++world->drop_depth;bool okay=world->services.print(world->services.context,reason,error) &&
        world->services.bot_drop(world->services.context,client,error);
    --world->drop_depth;--world->calls;return okay;
}
bool application_bot_world_drop_admitted(const application_bot_world *world) {return world && world->drop_depth!=0;}
bool application_bot_world_exit_level(application_bot_world *world,qa_error *error) {
    if(!idle(world,error)) return false;
    ++world->calls;bool okay=world->services.exit_level(world->services.context,error);--world->calls;return okay;
}
bool application_bot_world_console(application_bot_world *world,const char *text,qa_error *error) {
    if(!idle(world,error) || !text) return false;
    ++world->calls;bool okay=world->services.console(world->services.context,text,error);--world->calls;return okay;
}
bool application_bot_world_message(application_bot_world *world,uint32_t client,const char *text,qa_error *error) {
    if(!idle(world,error) || !text) return false;
    ++world->calls;bool okay=world->services.message(world->services.context,client,text,error);--world->calls;return okay;
}
bool application_bot_world_random(application_bot_world *world,bool centered,float *out,qa_error *error) {
    if(!idle(world,error) || !out) return false;
    ++world->calls;float value;bool okay=world->services.random(world->services.context,&value,error);
    if(okay) *out=centered?value*2.0f-1.0f:value;
    --world->calls;return okay;
}
const char *application_bot_world_userinfo(const application_bot_world *world,uint32_t client) {
    return world?text_find(world->userinfos,world->userinfo_count,client):NULL;
}
bool application_bot_world_userinfo_set(application_bot_world *world,uint32_t client,const char *text,qa_error *error) {
    return idle(world,error) && text_set(&world->userinfos,&world->userinfo_count,client,text,error);
}
bool application_bot_world_configstring_set(application_bot_world *world,uint32_t index,const char *text,qa_error *error) {
    return idle(world,error) && text_set(&world->strings,&world->string_count,index,text,error);
}
bool application_bot_world_model_index(application_bot_world *world,const char *name,int32_t *out,qa_error *error) {
    return idle(world,error) && model_index(world,name,out,error);
}
bool application_bot_world_configstring(application_bot_world *world,uint32_t index,char *out,size_t capacity,qa_error *error) {
    if(!idle(world,error) || !out || !capacity) return false;
    if(index<544 || index>=608) {
        const char *value=text_find(world->strings,world->string_count,index);
        size_t size=strlen(value);if(size>=capacity) size=capacity-1;
        memcpy(out,value,size);out[size]=0;return true;
    }
    ++world->calls;qa_actor_id actor;application_bot_world_client info;bool found=false;
    bool okay=actor_for_id(world,(int32_t)(index-544),&actor,error);
    if(okay && actor.registry) okay=world->services.client(world->services.context,actor,&info,&found,error);
    if(okay && found) {
        if(!info.name || !info.skin) okay=fail(error,QA_ERROR_FORMAT,"shared bot client info has no actual name or appearance");
        else {
            int size=snprintf(out,capacity,"\\n\\%s\\t\\%d\\model\\%s",info.name,info.spectator?3:0,info.skin);
            if(size<0) okay=fail(error,QA_ERROR_FORMAT,"formatting shared bot client configstring");
        }
    } else if(okay) out[0]=0;
    --world->calls;return okay;
}
bool application_bot_world_memory_alias(application_bot_world *world,application_bot_memory_alias alias,
    application_bot_memory_view *out,qa_error *error) {
    if(!world || !out || alias.offset>APPLICATION_BOT_MEMORY_BYTES ||
       alias.length>APPLICATION_BOT_MEMORY_BYTES-alias.offset)
        return fail(error,QA_ERROR_ARGUMENT,"shared bot memory alias is outside its actual fixed source pool");
    *out=(application_bot_memory_view){world->memory+alias.offset,alias.length};return true;
}
static bool memory_allocate(application_bot_world *world,uint32_t size,
    application_bot_memory_alias *out,qa_error *error) {
    if(!idle(world,error) || !out) return false;
    if(size>INT32_MAX) return fail(error,QA_ERROR_ARGUMENT,"G_Alloc requires a nonnegative source int size");
    int32_t debug;
    if(!world->services.memory_debug(world->services.context,&debug,error)) return false;
    uint32_t aligned=(size+31u)&~31u;
    if(debug) {
        uint32_t remaining=APPLICATION_BOT_MEMORY_BYTES-world->alloc_point-aligned;int32_t signed_remaining;
        memcpy(&signed_remaining,&remaining,sizeof(signed_remaining));char text[144];
        snprintf(text,sizeof(text),"G_Alloc of %u bytes (%d left)\n",size,signed_remaining);
        if(!world->services.print(world->services.context,text,error)) return false;
    }
    if(size>APPLICATION_BOT_MEMORY_BYTES-world->alloc_point) {
        char text[144];snprintf(text,sizeof(text),"G_Alloc: failed on allocation of %u bytes\n",size);
        return fail(error,QA_ERROR_MEMORY,text);
    }
    *out=(application_bot_memory_alias){world->alloc_point,size};
    world->alloc_point+=(size+31u)&~31u;return true;
}
bool application_bot_world_memory_allocate(application_bot_world *world,uint32_t size,
    application_bot_memory_alias *out,qa_error *error) {
    if(!idle(world,error) || !out) return false;
    ++world->calls;bool okay=memory_allocate(world,size,out,error);--world->calls;return okay;
}
bool application_bot_world_memory_string(application_bot_world *world,application_bot_memory_alias alias,
    const char **out,qa_error *error) {
    application_bot_memory_view view;
    if(!out || !application_bot_world_memory_alias(world,alias,&view,error)) return false;
    if(!memchr(view.data,0,APPLICATION_BOT_MEMORY_BYTES-alias.offset))
        return fail(error,QA_ERROR_FORMAT,"shared bot string reads beyond the retained source memory pool");
    *out=(const char *)view.data;return true;
}
bool application_bot_world_memory_write_string(application_bot_world *world,application_bot_memory_alias alias,
    const char *text,qa_error *error) {
    application_bot_memory_view view;
    if(!idle(world,error) || !text || !application_bot_world_memory_alias(world,alias,&view,error)) return false;
    size_t size=strlen(text)+1;
    if(size>view.size) return fail(error,QA_ERROR_ARGUMENT,"shared bot string exceeds its actual allocation view");
    memcpy(view.data,text,size);return true;
}
bool application_bot_world_memory_rewind(application_bot_world *world,qa_error *error) {
    if(!idle(world,error)) return false;
    world->alloc_point=0;return true;
}
bool application_bot_world_memory_status(application_bot_world *world,qa_error *error) {
    if(!idle(world,error)) return false;
    char text[144];snprintf(text,sizeof(text),"Game memory status: %u out of %u bytes allocated\n",
        world->alloc_point,APPLICATION_BOT_MEMORY_BYTES);
    ++world->calls;bool okay=world->services.print(world->services.context,text,error);--world->calls;return okay;
}
static bool topology(const application_bot_world *world,qa_error *error) {
    if(world->next_entity<64 || world->next_entity>1022 || !world->next_generation ||
       world->next_generation>UINT64_C(9007199254740991) ||
       world->order_count>APPLICATION_BOT_WORLD_CAPACITY || world->free_count>APPLICATION_BOT_WORLD_CAPACITY ||
       world->order_count+world->free_count!=world->next_entity-64 ||
       world->alloc_point>APPLICATION_BOT_MEMORY_BYTES || world->alloc_point%32 ||
       (world->services.max_clients && !world->begun)) goto invalid;
    bool seen[APPLICATION_BOT_WORLD_CAPACITY]={0};
    for(uint32_t i=0;i<world->order_count;++i) {
        uint32_t id=world->order[i];
        if(id<64 || id>=world->next_entity || seen[id-64] || !world->rows[id-64].actor.registry) goto invalid;
        seen[id-64]=true;
        for(uint32_t j=0;j<i;++j)
            if(qa_actor_id_equal(world->rows[id-64].actor,world->rows[world->order[j]-64].actor)) goto invalid;
    }
    for(uint32_t i=0;i<world->free_count;++i) {
        uint32_t id=world->free_ids[i];
        if(id<64 || id>=world->next_entity || seen[id-64] || world->rows[id-64].actor.registry) goto invalid;
        seen[id-64]=true;
    }
    for(uint32_t i=0;i<world->next_entity-64;++i) {
        uint64_t generation=world->rows[i].generation;
        if(!seen[i] || !generation || generation>=world->next_generation) goto invalid;
        for(uint32_t j=0;j<i;++j) if(generation==world->rows[j].generation) goto invalid;
    }
    const bot_world_text *tables[]={world->userinfos,world->strings,world->models};
    const size_t counts[]={world->userinfo_count,world->string_count,world->model_count};
    for(size_t table=0;table<3;++table) {
        if(counts[table] && !tables[table]) goto invalid;
        for(size_t i=0;i<counts[table];++i) {
            const bot_world_text *entry=&tables[table][i];
            if(!entry->text) goto invalid;
            for(size_t j=0;j<i;++j) if(entry->id==tables[table][j].id ||
               (table==2 && !strcmp(entry->text,tables[table][j].text))) goto invalid;
            if(table==2 && (!entry->text[0] || entry->text[0]=='*' ||
               entry->id<world->services.base_model_count ||
               (uint64_t)entry->id>=(uint64_t)world->services.base_model_count+world->model_count || entry->id>INT32_MAX)) goto invalid;
        }
    }
    return true;
invalid:
    return fail(error,QA_ERROR_FORMAT,"invalid actual shared bot namespace, memory or private string topology");
}
static bool text_fields(qa_source_save_io *io,bot_world_text **table,size_t *count) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(!qa_source_save_count(io,count,SIZE_MAX/sizeof(**table))) return false;
    if(reading && *count) {
        if(io->offset>io->input.size || *count>(io->input.size-io->offset)/13)
            return bot_save_fail(io,QA_ERROR_FORMAT,"truncated shared bot private string table");
        *table=calloc(*count,sizeof(**table));
        if(!*table) {*count=0;return bot_save_fail(io,QA_ERROR_MEMORY,"restoring shared bot private strings");}
    }
    for(size_t i=0;i<*count;++i) {
        if(!qa_source_save_u32(io,&(*table)[i].id)) return false;
        const char *text=(*table)[i].text;
        if(!bot_save_text(io,&text)) return false;
        if(reading) (*table)[i].text=(char *)text;
        if(!text) return bot_save_fail(io,QA_ERROR_FORMAT,"absent shared bot private string entry");
    }
    return true;
}
static bool fields(qa_source_save_io *io,application_bot_world *world) {
    static const uint8_t magic[8]={'Q','A','B','W','O','R','L','D'};
    if(!bot_save_signature(io,magic)) return false;
    uint32_t source=world->services.source,max_clients=world->services.max_clients,base_models=world->services.base_model_count;
    if(!qa_source_save_u32(io,&source) || !qa_source_save_u32(io,&max_clients) || !qa_source_save_u32(io,&base_models) ||
       source!=(uint32_t)world->services.source || max_clients!=world->services.max_clients || base_models!=world->services.base_model_count)
        return bot_save_fail(io,QA_ERROR_FORMAT,"shared bot continuation source family or source limits changed");
    if(!qa_source_save_u32(io,&world->next_entity) || !qa_source_save_u64(io,&world->next_generation) ||
       !qa_source_save_u32(io,&world->order_count) || !qa_source_save_u32(io,&world->free_count) ||
       world->next_entity<64 || world->next_entity>1022 || world->order_count>APPLICATION_BOT_WORLD_CAPACITY ||
       world->free_count>APPLICATION_BOT_WORLD_CAPACITY || world->order_count+world->free_count!=world->next_entity-64)
        return bot_save_fail(io,QA_ERROR_FORMAT,"invalid shared bot namespace continuation extent");
    for(uint32_t i=0;i<world->order_count;++i) if(!qa_source_save_u32(io,&world->order[i])) return false;
    for(uint32_t i=0;i<world->free_count;++i) if(!qa_source_save_u32(io,&world->free_ids[i])) return false;
    for(uint32_t i=0;i<world->next_entity-64;++i)
        if(!qa_source_save_actor(io,&world->rows[i].actor) || !qa_source_save_u64(io,&world->rows[i].generation)) return false;
    for(uint32_t i=0;i<world->services.max_clients;++i) if(!qa_source_save_bool(io,&world->begun[i])) return false;
    return text_fields(io,&world->userinfos,&world->userinfo_count) && text_fields(io,&world->strings,&world->string_count) &&
        text_fields(io,&world->models,&world->model_count) && qa_source_save_u32(io,&world->alloc_point) &&
        qa_source_save_bytes(io,world->memory,sizeof(world->memory));
}
bool application_bot_world_capture(application_bot_world *world,qa_buffer *out,qa_error *error) {
    if(!idle(world,error) || world->calls || !out || !topology(world,error)) return false;
    ++world->calls;qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,world->services.session,error) && fields(&io,world) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);--world->calls;return okay;
}
bool application_bot_world_restore(application_bot_world *world,qa_bytes bytes,qa_error *error) {
    if(!world || world->calls || !world->restoring)
        return fail(error,QA_ERROR_ARGUMENT,"shared bot restore requires its detached empty source holder");
    application_bot_world *candidate=NULL;
    if(!application_bot_world_create(&world->services,true,&candidate,error)) return false;
    ++world->calls;qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,world->services.session,bytes,error) && fields(&io,candidate) &&
        qa_source_save_finish(&io,NULL) && topology(candidate,error);
    if(okay) {
        application_bot_world old=*world;*world=*candidate;*candidate=old;
        world->restoring=false;candidate->calls=0;
    } else --world->calls;
    application_bot_world_destroy(candidate,NULL);qa_source_save_dispose(&io);return okay;
}
