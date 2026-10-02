/* GameBotCatalog and G_ParseInfos, g_bot.c source state. */
#include "internal.h"

bool qa_bot_catalog_create(const qa_bot_catalog_services *s,qa_bot_catalog **out,qa_error *e) {
    if(!s || !out || *out || !s->files || !s->memory.allocate || !s->memory.read || !s->memory.write ||
       !s->print || !s->cvar || !s->register_cvar || !s->set_cvar || !s->server_info || !s->clock ||
       !s->client || !s->allocate_client || !s->choose_team || !s->activate || !s->userinfo ||
       !s->set_userinfo || !s->connect || !s->begin || !s->reset_podium || !s->insert_command ||
       !s->append_command || !s->server_command || !s->random)
        return bot_catalog_fail(e,QA_ERROR_ARGUMENT,"game bot catalogue requires its actual source services");
    qa_bot_catalog *c=calloc(1,sizeof(*c));
    if(!c) return bot_catalog_fail(e,QA_ERROR_MEMORY,"allocating actual game bot catalogue");
    c->services=*s;*out=c;return true;
}
bool qa_bot_catalog_can_destroy(const qa_bot_catalog *c) {return !c || !c->calls;}
bool qa_bot_catalog_destroy(qa_bot_catalog *c,qa_error *e) {
    if(!c) return true;
    if(c->calls) return bot_catalog_fail(e,QA_ERROR_ARGUMENT,"game bot catalogue is executing");
    free(c);return true;
}
uint32_t qa_bot_catalog_bot_count(const qa_bot_catalog *c) {return c?c->bots.count:0;}
uint32_t qa_bot_catalog_arena_count(const qa_bot_catalog *c) {return c?c->arenas.count:0;}
bool bot_catalog_cvar(qa_bot_catalog *c,const char *name,char *out,size_t capacity,int32_t *integer,qa_error *e) {
    qa_cvar_view actual={0};bool found;
    if(!c->services.cvar(c->services.context,name,&actual,&found,e)) return false;
    if(integer) *integer=found?actual.integer:0;
    if(out) {
        if(!capacity) return bot_catalog_fail(e,QA_ERROR_ARGUMENT,"source catalogue cvar output has no capacity");
        const char *text=found?actual.value:"";
        if(!text) return bot_catalog_fail(e,QA_ERROR_FORMAT,"source catalogue cvar has no actual text");
        size_t length=strlen(text);if(length>=capacity) length=capacity-1;
        memcpy(out,text,length);out[length]=0;
    }
    return true;
}
bool bot_catalog_minimum_update(qa_bot_catalog *c,qa_error *e) {
    qa_cvar_view actual={0};bool found;
    if(!c->services.cvar(c->services.context,"bot_minplayers",&actual,&found,e)) return false;
    if(!found || (c->minimum_registered && c->minimum_modification==actual.modification_count)) return true;
    if(!actual.value || strlen(actual.value)>255)
        return bot_catalog_fail(e,QA_ERROR_FORMAT,"Bot cvar value exceeds source storage");
    strcpy(c->minimum_value,actual.value);c->minimum_numeric=actual.number;
    c->minimum_length=strlen(actual.value);
    c->minimum_integer=actual.integer;c->minimum_modification=actual.modification_count;
    c->minimum_registered=true;return true;
}
static bool parse_infos(qa_bot_catalog *c,qa_common_cursor *cursor,bot_catalog_infos *destination,qa_error *e) {
    uint32_t parsed=0,maximum=BOT_CATALOG_INFOS-destination->count;
    for(;;) {
        if(!qa_common_parse(&c->parser,cursor,true,e)) return false;
        if(!c->parser.token_length) break;
        if(strcmp(c->parser.token,"{")) {
            if(!c->services.print(c->services.context,"Missing { in info file\n",e)) return false;
            break;
        }
        if(parsed==maximum) {
            if(!c->services.print(c->services.context,"Max infos exceeded\n",e)) return false;
            break;
        }
        char info[1024]={0};
        for(;;) {
            if(!qa_common_parse(&c->parser,cursor,true,e)) return false;
            if(!c->parser.token_length) {
                if(!c->services.print(c->services.context,"Unexpected end of info file\n",e)) return false;
                break;
            }
            if(!strcmp(c->parser.token,"}")) break;
            char key[1024];strcpy(key,c->parser.token);
            if(!qa_common_parse(&c->parser,cursor,false,e) ||
               !bot_catalog_info_set(c,info,key,c->parser.token_length?c->parser.token:"<NULL>",e)) return false;
        }
        uint32_t length=(uint32_t)strlen(info)+10,offset;
        if(!c->services.memory.allocate(c->services.memory.context,length,&offset,e)) return false;
        qa_bot_source_record record={offset,length};
        uint32_t index=destination->count+parsed;
        destination->records[index]=record;
        if(destination->extent<=index) destination->extent=index+1;
        if(!bot_catalog_write(c,record,info,e)) return false;
        ++parsed;
    }
    destination->count+=parsed;return true;
}
static bool load_file(qa_bot_catalog *c,const char *path,bot_catalog_infos *destination,qa_error *e) {
    qa_resource *resource=NULL;qa_error local={0};qa_buffer native_path={0};
    if(!bot_catalog_utf8(path,&native_path,e)) return false;
    bool acquired=qa_vfs_acquire(c->services.files,(char *)native_path.data,&resource,NULL,&local);
    qa_buffer_free(&native_path);
    if(!acquired) {
        if(local.code!=QA_ERROR_NOT_FOUND) {if(e) *e=local;return false;}
        char message[1200];snprintf(message,sizeof(message),"^1file not found: %s\n",path);
        return c->services.print(c->services.context,message,e);
    }
    qa_bytes bytes=qa_resource_bytes(resource);bool okay=true;
    if(bytes.size>=BOT_CATALOG_TEXT) {
        char message[1200];snprintf(message,sizeof(message),"^1file too large: %s is %zu, max allowed is %d",path,bytes.size,BOT_CATALOG_TEXT);
        okay=c->services.print(c->services.context,message,e);
    } else {
        qa_common_cursor cursor;
        okay=qa_common_cursor_init(&cursor,bytes,QA_COMMON_TERMINATED,e) && parse_infos(c,&cursor,destination,e);
    }
    qa_resource_release(resource);return okay;
}
static bool load_catalog(qa_bot_catalog *c,bool bots,qa_error *e) {
    bot_catalog_infos *destination=bots?&c->bots:&c->arenas;
    destination->count=0;
    const char *variable=bots?"g_botsFile":"g_arenasFile",*kind=bots?"bots":"arenas";
    if(!c->services.register_cvar(c->services.context,variable,"",QA_CVAR_INIT|QA_CVAR_READONLY,e)) return false;
    qa_cvar_view value={0};bool found;
    if(!c->services.cvar(c->services.context,variable,&value,&found,e)) return false;
    if(found && (!value.value || strlen(value.value)>255))
        return bot_catalog_fail(e,QA_ERROR_FORMAT,"Bot cvar value exceeds source storage");
    char filename[256];
    if(found && *value.value) strcpy(filename,value.value);
    else snprintf(filename,sizeof(filename),"scripts/%s.txt",kind);
    if(!load_file(c,filename,destination,e)) return false;
    qa_vfs_listing listing={0};
    if(!qa_vfs_list(c->services.files,"scripts",bots?".bot":".arena",&listing,e)) return false;
    bool okay=true;size_t listed_bytes=0,folded_count=0;
    qa_buffer *folded=listing.count?calloc(listing.count,sizeof(*folded)):NULL;
    if(listing.count && !folded) okay=bot_catalog_fail(e,QA_ERROR_MEMORY,"retaining actual bot asset listing names");
    for(size_t i=0;okay && i<listing.count;++i) {
        /* BotAssetFiles.list admits immediate names and a source 1024-byte
         * aggregate, independently of the engine's larger FS listing. */
        if(strchr(listing.names[i],'/')) continue;
        qa_buffer fold={0};
        okay=qa_utf8_lower((qa_bytes){(uint8_t *)listing.names[i],strlen(listing.names[i])},&fold,e);
        if(!okay) break;
        bool duplicate=false;
        for(size_t j=0;j<folded_count;++j)
            if(folded[j].size==fold.size && !memcmp(folded[j].data,fold.data,fold.size)) {duplicate=true;break;}
        if(duplicate) {qa_buffer_free(&fold);continue;}
        folded[folded_count++]=fold;
        char name[256];
        okay=bot_catalog_latin1(listing.names[i],name,sizeof(name),e);if(!okay) break;
        size_t length=strlen(name);
        if(listed_bytes+length+1>=1024) break;
        listed_bytes+=length+1;
        if(strlen(name)+7>=128) {okay=bot_catalog_fail(e,QA_ERROR_FORMAT,"Game catalog filename exceeds source 128-byte storage");break;}
        snprintf(filename,sizeof(filename),"scripts/%s",name);
        okay=load_file(c,filename,destination,e);
    }
    for(size_t i=0;i<folded_count;++i) qa_buffer_free(&folded[i]);
    free(folded);
    qa_vfs_listing_free(&listing);
    if(!okay) return false;
    char message[80];snprintf(message,sizeof(message),"%u %s parsed\n",destination->count,kind);
    return c->services.print(c->services.context,message,e);
}
bool qa_bot_catalog_initialize(qa_bot_catalog *c,bool restart,qa_error *e) {
    if(!bot_catalog_enter(c,e)) return false;
    int32_t enabled;bool okay=bot_catalog_cvar(c,"bot_enable",NULL,0,&enabled,e);
    if(okay && enabled) okay=load_catalog(c,true,e);
    if(okay) okay=load_catalog(c,false,e);
    for(uint32_t i=0;okay && i<c->arenas.count;++i) {
        qa_buffer original={0};char info[1024],number[32];
        okay=bot_catalog_text(c,c->arenas.records[i],&original,e);
        if(okay && original.size>=sizeof(info)) okay=bot_catalog_fail(e,QA_ERROR_FORMAT,"Info_SetValueForKey: oversize infostring");
        if(okay) {
            memcpy(info,original.data,original.size+1);snprintf(number,sizeof(number),"%u",i);
            okay=bot_catalog_info_set(c,info,"num",number,e) && bot_catalog_write(c,c->arenas.records[i],info,e);
        }
        qa_buffer_free(&original);
    }
    if(okay) okay=c->services.register_cvar(c->services.context,"bot_minplayers","0",QA_CVAR_SERVERINFO,e);
    if(okay) {
        c->minimum_registered=true;c->minimum_modification=UINT64_MAX;
        c->minimum_value[0]=0;c->minimum_length=0;c->minimum_numeric=0;c->minimum_integer=0;
        okay=bot_catalog_minimum_update(c,e);
    }
    qa_bot_catalog_clock clock={0};if(okay) okay=c->services.clock(c->services.context,&clock,e);
    if(okay && clock.game_type==2) {
        char server[8192],map[64];qa_buffer arena={0};bool found=false;
        okay=c->services.server_info(c->services.context,server,sizeof(server),e) &&
            bot_catalog_info_value(server,"mapname",map,sizeof(map),e) &&
            bot_catalog_lookup(c,&c->arenas,"map",map,&arena,&found,e);
        if(okay && found) {
            char frag[8192]={0},time[8192]={0},special[8192]={0},bots[8192]={0};
            okay=bot_catalog_info_value((char *)arena.data,"fraglimit",frag,sizeof(frag),e) &&
                bot_catalog_info_value((char *)arena.data,"timelimit",time,sizeof(time),e) &&
                bot_catalog_info_value((char *)arena.data,"special",special,sizeof(special),e) &&
                bot_catalog_info_value((char *)arena.data,"bots",bots,sizeof(bots),e);
            int32_t f=bot_catalog_atoi(frag),t=bot_catalog_atoi(time);
            if(okay) okay=c->services.set_cvar(c->services.context,"fraglimit",f?frag:"0",e) &&
                c->services.set_cvar(c->services.context,"timelimit",t?time:"0",e);
            if(okay && !f && !t) okay=c->services.set_cvar(c->services.context,"fraglimit","10",e) &&
                c->services.set_cvar(c->services.context,"timelimit","0",e);
            if(okay && !restart) okay=bot_catalog_spawn_list(c,bots,2000+(bot_catalog_equal(special,"training")?10000:0),e);
        }
        qa_buffer_free(&arena);
    }
    --c->calls;return okay;
}
bool qa_bot_catalog_bot_number(qa_bot_catalog *c,int32_t number,qa_buffer *out,bool *found,qa_error *e) {
    if(!out || out->data || !found || !bot_catalog_enter(c,e)) return false;
    *found=false;bool okay;
    if(number<0 || (uint32_t)number>=c->bots.count) {
        char text[80];snprintf(text,sizeof(text),"^1Invalid bot number: %d\n",number);okay=c->services.print(c->services.context,text,e);
    } else {okay=bot_catalog_text(c,c->bots.records[number],out,e);if(okay) *found=true;}
    --c->calls;return okay;
}
static bool lookup(qa_bot_catalog *c,bool bots,const char *name,qa_buffer *out,bool *found,qa_error *e) {
    if(!name || !out || out->data || !found || !bot_catalog_enter(c,e)) return false;
    bool okay=bot_catalog_lookup(c,bots?&c->bots:&c->arenas,bots?"name":"map",name,out,found,e);
    --c->calls;return okay;
}
bool qa_bot_catalog_bot_name(qa_bot_catalog *c,const char *name,qa_buffer *out,bool *found,qa_error *e) {
    return lookup(c,true,name,out,found,e);
}
bool qa_bot_catalog_arena_map(qa_bot_catalog *c,const char *name,qa_buffer *out,bool *found,qa_error *e) {
    return lookup(c,false,name,out,found,e);
}
bool qa_bot_catalog_bot_name_utf8(qa_bot_catalog *c,const char *name,qa_buffer *out,bool *found,qa_error *e) {
    if(!name) return bot_catalog_fail(e,QA_ERROR_ARGUMENT,"public bot name is absent");
    size_t bytes=strlen(name);char *source=malloc(bytes+1);
    if(!source) return bot_catalog_fail(e,QA_ERROR_MEMORY,"decoding public authored bot name");
    bool okay=bot_catalog_latin1(name,source,bytes+1,e);
    if(okay) okay=qa_bot_catalog_bot_name(c,source,out,found,e);
    free(source);return okay;
}
