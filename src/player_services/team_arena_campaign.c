/* Authored Team Arena metadata and UI_StartSkirmish progression. */
#include "qa/team_arena_campaign_progress.h"
#include "qa/common_parse.h"
#include "qa/text.h"
#include "save_io.h"
#include <inttypes.h>
#include <stdio.h>

typedef struct tokens { char **fields; size_t count; } tokens;
typedef struct section { char *name; tokens *rows; size_t count; } section;
typedef struct sections { section *rows; size_t count; } sections;
typedef struct game_time { int64_t type, time; } game_time;
typedef struct campaign_map {
    qa_team_arena_campaign_map view;
    game_time *times; size_t count;
} campaign_map;
typedef struct campaign_team { char *name, *members[5]; } campaign_team;
typedef struct campaign_alias { char *name, *ai; } campaign_alias;
typedef struct campaign_resource { qa_resource *resource; char *path; } campaign_resource;
struct qa_team_arena_campaign {
    qa_team_arena_catalog_policy policy;
    int64_t *game_types; size_t game_type_count;
    campaign_map *maps; size_t map_count;
    campaign_team *teams; size_t team_count;
    campaign_alias *aliases; size_t alias_count;
    campaign_resource *resources; size_t resource_count;
    size_t references;
    bool busy;
};
struct qa_team_arena_skirmish {
    qa_team_arena_campaign *campaign;
    qa_team_arena_skirmish_view view;
    qa_team_arena_setting source[20], client[3];
    qa_team_arena_campaign_bot bots[9];
    char numbers[7][32];
    char *player, *opponent;
};
static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error,code,0,"%s",message); return false; }
static char *copy(const char *text,qa_error *error)
{
    size_t size=strlen(text); char *out=malloc(size+1);
    if (!out) { fail(error,QA_ERROR_MEMORY,"Retaining authored Team Arena text"); return NULL; }
    memcpy(out,text,size+1); return out;
}
static char *latin1(const char *text,qa_error *error)
{
    size_t size=strlen(text),length=size;
    for (size_t i=0;i<size;++i) if ((unsigned char)text[i]>=128) ++length;
    char *out=malloc(length+1);
    if (!out) { fail(error,QA_ERROR_MEMORY,"Publishing authored Team Arena UTF-8 text"); return NULL; }
    size_t n=0;
    for (size_t i=0;i<size;++i) {
        unsigned char c=(unsigned char)text[i];
        if (c<128) out[n++]=(char)c;
        else { out[n++]=(char)(0xc0u|(c>>6)); out[n++]=(char)(0x80u|(c&63u)); }
    }
    out[n]=0; return out;
}
static char *lower(const char *text,qa_error *error)
{
    qa_buffer value={0};
    if (!qa_utf8_lower((qa_bytes){(const uint8_t *)text,strlen(text)},&value,error)) return NULL;
    return (char *)value.data;
}
static void tokens_destroy(tokens *row)
{
    for (size_t i=0;i<row->count;++i) free(row->fields[i]);
    free(row->fields); *row=(tokens){0};
}
static void section_destroy(section *part)
{
    free(part->name);
    for (size_t i=0;i<part->count;++i) tokens_destroy(part->rows+i);
    free(part->rows); *part=(section){0};
}
static void sections_destroy(sections *all)
{
    for (size_t i=0;i<all->count;++i) section_destroy(all->rows+i);
    free(all->rows); *all=(sections){0};
}
static bool grow(void **data,size_t count,size_t stride,qa_error *error)
{
    if (count==SIZE_MAX || count+1>SIZE_MAX/stride)
        return fail(error,QA_ERROR_MEMORY,"Team Arena metadata exceeds native storage");
    void *next=realloc(*data,(count+1)*stride);
    if (!next) return fail(error,QA_ERROR_MEMORY,"Allocating authored Team Arena metadata rows");
    *data=next; return true;
}
static bool parse_sections(qa_resource *resource,sections *out,qa_error *error)
{
    qa_common_cursor cursor; qa_common_parser parser={0};
    if (!qa_common_cursor_init(&cursor,qa_resource_bytes(resource),QA_COMMON_TERMINATED,error)) return false;
    for (;;) {
        if (!qa_common_parse(&parser,&cursor,true,error)) return false;
        if (!parser.token[0]) return true;
        char *text=latin1(parser.token,error),*name=text?lower(text,error):NULL; free(text);
        if (!name) return false;
        size_t at=0;
        while (at<out->count && strcmp(out->rows[at].name,name)) ++at;
        if (at==out->count) {
            if (!grow((void **)&out->rows,out->count,sizeof(section),error)) { free(name); return false; }
            out->rows[out->count++]=(section){0};
        } else section_destroy(out->rows+at);
        section *part=out->rows+at; part->name=name;
        if (!qa_common_parse(&parser,&cursor,true,error) || strcmp(parser.token,"{"))
            return fail(error,QA_ERROR_FORMAT,"Invalid Team Arena metadata section");
        for (;;) {
            if (!qa_common_parse(&parser,&cursor,true,error)) return false;
            if (!strcmp(parser.token,"}")) break;
            if (strcmp(parser.token,"{")) return fail(error,QA_ERROR_FORMAT,"Invalid Team Arena metadata row");
            if (!grow((void **)&part->rows,part->count,sizeof(tokens),error)) return false;
            tokens *row=part->rows+part->count++; *row=(tokens){0};
            for (;;) {
                if (!qa_common_parse(&parser,&cursor,true,error)) return false;
                if (!strcmp(parser.token,"}")) break;
                if (!parser.token[0] || !strcmp(parser.token,"{"))
                    return fail(error,QA_ERROR_FORMAT,"Invalid Team Arena metadata field");
                if (!grow((void **)&row->fields,row->count,sizeof(char *),error)) return false;
                char *field=latin1(parser.token,error); if (!field) return false;
                row->fields[row->count++]=field;
            }
        }
    }
}
static const section *find_section(const sections *all,const char *name)
{
    for (size_t i=0;i<all->count;++i) if (!strcmp(all->rows[i].name,name)) return all->rows+i;
    return NULL;
}
static bool integer(const tokens *row,size_t index,int64_t *out,qa_error *error)
{
    if (index>=row->count) return fail(error,QA_ERROR_FORMAT,"Incomplete Team Arena metadata row");
    const unsigned char *p=(const unsigned char *)row->fields[index]; bool negative=*p=='-';
    if (negative) ++p;
    if (!*p) return fail(error,QA_ERROR_FORMAT,"Invalid Team Arena metadata integer");
    uint64_t number=0,maximum=UINT64_C(9007199254740991);
    for (;*p;++p) {
        if (*p<'0' || *p>'9' || number>(maximum-(uint32_t)(*p-'0'))/10)
            return fail(error,QA_ERROR_FORMAT,"Team Arena metadata integer exceeds its source range");
        number=number*10+(uint32_t)(*p-'0');
    }
    *out=negative?-(int64_t)number:(int64_t)number; return true;
}
static void map_destroy(campaign_map *map)
{
    free((char *)map->view.map); free((char *)map->view.title); free((char *)map->view.opponent);
    free(map->times); *map=(campaign_map){0};
}
static void team_destroy(campaign_team *team)
{
    free(team->name); for (size_t i=0;i<5;++i) free(team->members[i]); *team=(campaign_team){0};
}
void qa_team_arena_campaign_destroy(qa_team_arena_campaign *campaign)
{
    if (!campaign || --campaign->references) return;
    for (size_t i=0;i<campaign->map_count;++i) map_destroy(campaign->maps+i);
    for (size_t i=0;i<campaign->team_count;++i) team_destroy(campaign->teams+i);
    for (size_t i=0;i<campaign->alias_count;++i) { free(campaign->aliases[i].name); free(campaign->aliases[i].ai); }
    for (size_t i=0;i<campaign->resource_count;++i) {
        qa_resource_release(campaign->resources[i].resource); free(campaign->resources[i].path);
    }
    free(campaign->game_types); free(campaign->maps); free(campaign->teams);
    free(campaign->aliases); free(campaign->resources); free(campaign);
}
static bool read_game(qa_team_arena_campaign *campaign,const sections *all,qa_error *error)
{
    const section *types=find_section(all,"gametypes"),*maps=find_section(all,"maps");
    for (size_t i=0;types && i<types->count;++i) {
        int64_t type=0;
        if (!integer(types->rows+i,1,&type,error) ||
            !grow((void **)&campaign->game_types,campaign->game_type_count,sizeof(int64_t),error)) return false;
        campaign->game_types[campaign->game_type_count++]=type;
    }
    for (size_t i=0;maps && i<maps->count;++i) {
        const tokens *row=maps->rows+i; int64_t members=0;
        if (row->count<4 || !integer(row,2,&members,error) || members<1 || members>5)
            return fail(error,QA_ERROR_FORMAT,"Invalid Team Arena campaign map");
        const char *name=row->fields[1];
        if (!*name) return fail(error,QA_ERROR_FORMAT,"Empty Team Arena campaign map");
        for (const unsigned char *p=(const unsigned char *)name;*p;++p)
            if (!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || *p=='_' || *p=='/' || *p=='-'))
                return fail(error,QA_ERROR_FORMAT,"Invalid Team Arena campaign map path");
        if (!grow((void **)&campaign->maps,campaign->map_count,sizeof(campaign_map),error)) return false;
        campaign_map *map=campaign->maps+campaign->map_count++; *map=(campaign_map){0};
        size_t size=strlen(name)+10; char *path=malloc(size);
        if (!path) return fail(error,QA_ERROR_MEMORY,"Retaining authored Team Arena map path");
        snprintf(path,size,"maps/%s.bsp",name); map->view.map=path;
        map->view.title=copy(row->fields[0],error); map->view.opponent=copy(row->fields[3],error);
        map->view.team_members=(uint32_t)members;
        if (!map->view.title || !map->view.opponent) return false;
        for (size_t j=4;j<row->count;j+=2) {
            int64_t type=0,time=0;
            if (!integer(row,j,&type,error) || !integer(row,j+1,&time,error)) return false;
            size_t at=0; while (at<map->count && map->times[at].type!=type) ++at;
            if (at==map->count) {
                if (!grow((void **)&map->times,map->count,sizeof(game_time),error)) return false;
                ++map->count;
            }
            map->times[at]=(game_time){type,time};
        }
    }
    return true;
}
static bool read_teams(qa_team_arena_campaign *campaign,const sections *all,qa_error *error)
{
    const section *teams=find_section(all,"teams"),*aliases=find_section(all,"aliases");
    for (size_t i=0;teams && i<teams->count;++i) {
        const tokens *row=teams->rows+i;
        if (row->count<7) return fail(error,QA_ERROR_FORMAT,"Incomplete Team Arena team row");
        campaign_team team={0}; team.name=lower(row->fields[0],error);
        if (!team.name) return false;
        bool ok=true;
        for (size_t j=0;ok && j<5;++j) ok=(team.members[j]=copy(row->fields[j+2],error))!=NULL;
        if (!ok) { team_destroy(&team); return false; }
        size_t at=0; while (at<campaign->team_count && strcmp(campaign->teams[at].name,team.name)) ++at;
        if (at==campaign->team_count) {
            if (!grow((void **)&campaign->teams,campaign->team_count,sizeof(campaign_team),error)) { team_destroy(&team); return false; }
            campaign->teams[campaign->team_count++]=(campaign_team){0};
        }
        team_destroy(campaign->teams+at); campaign->teams[at]=team;
    }
    for (size_t i=0;aliases && i<aliases->count;++i) {
        const tokens *row=aliases->rows+i;
        if (row->count<2) return fail(error,QA_ERROR_FORMAT,"Incomplete Team Arena alias row");
        char *name=lower(row->fields[0],error),*ai=name?copy(row->fields[1],error):NULL;
        if (!name || !ai) { free(name); free(ai); return false; }
        size_t at=0; while (at<campaign->alias_count && strcmp(campaign->aliases[at].name,name)) ++at;
        if (at==campaign->alias_count) {
            if (!grow((void **)&campaign->aliases,campaign->alias_count,sizeof(campaign_alias),error)) { free(name); free(ai); return false; }
            campaign->aliases[campaign->alias_count++]=(campaign_alias){0};
        }
        free(campaign->aliases[at].name); free(campaign->aliases[at].ai);
        campaign->aliases[at]=(campaign_alias){name,ai};
    }
    return true;
}
static bool add_resource(qa_team_arena_campaign *campaign,qa_resource *resource,const char *path,qa_error *error)
{
    sections all={0};
    bool ok=parse_sections(resource,&all,error) &&
        (campaign->resource_count?read_teams(campaign,&all,error):read_game(campaign,&all,error));
    sections_destroy(&all);
    if (!ok || !grow((void **)&campaign->resources,campaign->resource_count,sizeof(campaign_resource),error)) return false;
    char *requested=copy(path,error); if (!requested) return false;
    qa_resource_retain(resource);
    campaign->resources[campaign->resource_count++]=(campaign_resource){resource,requested}; return true;
}
static bool acquire(qa_team_arena_campaign *campaign,qa_vfs *files,const char *path,qa_error *error)
{
    qa_resource *resource=NULL;
    if (!qa_vfs_acquire(files,path,&resource,NULL,error)) return false;
    bool ok=add_resource(campaign,resource,path,error); qa_resource_release(resource); return ok;
}
static const char *game_path(qa_team_arena_catalog_policy policy)
{ return policy==QA_TEAM_ARENA_CATALOG_DEMO?"demogameinfo.txt":"gameinfo.txt"; }
static const char *team_path(qa_team_arena_catalog_policy policy)
{ return policy==QA_TEAM_ARENA_CATALOG_DEMO?"demoteaminfo.txt":"teaminfo.txt"; }
bool qa_team_arena_campaign_create(qa_vfs *files,qa_team_arena_catalog_policy policy,
    qa_team_arena_campaign **out,qa_error *error)
{
    if (!files || !out || *out || (unsigned)policy>QA_TEAM_ARENA_CATALOG_DEMO)
        return fail(error,QA_ERROR_ARGUMENT,"Team Arena catalog requires its genuine source VFS and product policy");
    qa_team_arena_campaign *campaign=calloc(1,sizeof(*campaign));
    if (!campaign) return fail(error,QA_ERROR_MEMORY,"Allocating authored Team Arena campaign");
    campaign->references=1; campaign->policy=policy;
    bool ok=acquire(campaign,files,game_path(policy),error) && acquire(campaign,files,team_path(policy),error);
    qa_vfs_listing listing={0};
    if (ok && policy==QA_TEAM_ARENA_CATALOG_RETAIL) ok=qa_vfs_list(files,"scripts",".team",&listing,error);
    for (size_t i=0;ok && i<listing.count;++i) {
        size_t size=strlen(listing.names[i])+9; char *path=malloc(size);
        if (!path) { ok=fail(error,QA_ERROR_MEMORY,"Retaining authored Team Arena team resource path"); break; }
        snprintf(path,size,"scripts/%s",listing.names[i]); ok=acquire(campaign,files,path,error); free(path);
    }
    qa_vfs_listing_free(&listing);
    if (!ok) { qa_team_arena_campaign_destroy(campaign); return false; }
    *out=campaign; return true;
}
bool qa_team_arena_campaign_ready(const qa_team_arena_campaign *campaign,qa_error *error)
{ return (campaign && !campaign->busy) || fail(error,QA_ERROR_ARGUMENT,"Team Arena campaign is absent or executing a resource callback"); }
bool qa_team_arena_campaign_policy(const qa_team_arena_campaign *campaign,qa_team_arena_catalog_policy *out)
{ if (!campaign || !out) return false; *out=campaign->policy; return true; }
size_t qa_team_arena_campaign_map_count(const qa_team_arena_campaign *campaign)
{ return campaign?campaign->map_count:0; }
const qa_team_arena_campaign_map *qa_team_arena_campaign_map_at(const qa_team_arena_campaign *campaign,size_t index)
{ return campaign && index<campaign->map_count?&campaign->maps[index].view:NULL; }
size_t qa_team_arena_campaign_game_type_count(const qa_team_arena_campaign *campaign)
{ return campaign?campaign->game_type_count:0; }
bool qa_team_arena_campaign_game_type_at(const qa_team_arena_campaign *campaign,size_t index,int64_t *out)
{ if (!campaign || !out || index>=campaign->game_type_count) return false; *out=campaign->game_types[index]; return true; }
size_t qa_team_arena_campaign_team_count(const qa_team_arena_campaign *campaign)
{ return campaign?campaign->team_count:0; }
const char *qa_team_arena_campaign_team_at(const qa_team_arena_campaign *campaign,size_t index)
{ return campaign && index<campaign->team_count?campaign->teams[index].name:NULL; }
static bool has_type(const campaign_map *map,int64_t type)
{ for (size_t i=0;i<map->count;++i) if (map->times[i].type==type) return true; return false; }
static bool active(const campaign_map *map,int64_t type)
{ return has_type(map,2) && has_type(map,type==2?3:type==3?0:type); }
bool qa_team_arena_campaign_current(const qa_team_arena_campaign *campaign,const char *map,
    uint32_t game_type,qa_team_arena_cursor *out,qa_error *error)
{
    if (!qa_team_arena_campaign_ready(campaign,error) || !map || !out) return false;
    size_t type=0; while (type<campaign->game_type_count && campaign->game_types[type]!=game_type) ++type;
    char *name=lower(map,error); if (!name) return false;
    size_t index=0;
    for (;index<campaign->map_count;++index) {
        char *candidate=lower(campaign->maps[index].view.map,error);
        if (!candidate) { free(name); return false; }
        bool same=!strcmp(name,candidate); free(candidate); if (same) break;
    }
    free(name);
    if (type==campaign->game_type_count || index==campaign->map_count)
        return fail(error,QA_ERROR_FORMAT,"Current Team Arena match has no authored campaign entry");
    *out=(qa_team_arena_cursor){type,index}; return true;
}
bool qa_team_arena_campaign_next(const qa_team_arena_campaign *campaign,qa_team_arena_cursor current,
    qa_team_arena_cursor *out,qa_error *error)
{
    if (!qa_team_arena_campaign_ready(campaign,error) || !out ||
        current.game_type_index>=campaign->game_type_count || current.map_index>=campaign->map_count)
        return fail(error,QA_ERROR_ARGUMENT,"Invalid Team Arena campaign cursor");
    int64_t type=campaign->game_types[current.game_type_index];
    for (size_t i=current.map_index+1;i<campaign->map_count;++i) if (active(campaign->maps+i,type)) {
        *out=(qa_team_arena_cursor){current.game_type_index,i}; return true;
    }
    size_t next=current.game_type_index+1;
    next=next>=campaign->game_type_count?1:next==2?3:next;
    if (next<campaign->game_type_count) for (size_t i=0;i<campaign->map_count;++i)
        if (active(campaign->maps+i,campaign->game_types[next])) { *out=(qa_team_arena_cursor){next,i}; return true; }
    return fail(error,QA_ERROR_FORMAT,"Next Team Arena game type has no authored single-player maps");
}
static bool append_team(qa_team_arena_skirmish *plan,const char *name,const char *team,size_t count,qa_error *error)
{
    qa_team_arena_campaign *campaign=plan->campaign; char *key=lower(name,error); if (!key) return false;
    size_t at=0; while (at<campaign->team_count && strcmp(campaign->teams[at].name,key)) ++at;
    free(key);
    if (at==campaign->team_count) return fail(error,QA_ERROR_NOT_FOUND,"Authored Team Arena team is missing");
    for (size_t i=0;i<count;++i) {
        const char *member=campaign->teams[at].members[i],*ai="James";
        key=lower(member,error); if (!key) return false;
        for (size_t j=0;j<campaign->alias_count;++j) if (!strcmp(key,campaign->aliases[j].name)) { ai=campaign->aliases[j].ai; break; }
        free(key); size_t bot=plan->view.bot_count++;
        plan->bots[bot]=(qa_team_arena_campaign_bot){ai,member,team,(uint32_t)(bot+1)*500};
    }
    return true;
}
void qa_team_arena_skirmish_destroy(qa_team_arena_skirmish *plan)
{ if (plan) { qa_team_arena_campaign_destroy(plan->campaign); free(plan->player); free(plan->opponent); free(plan); } }
const qa_team_arena_skirmish_view *qa_team_arena_skirmish_read(const qa_team_arena_skirmish *plan)
{ return plan?&plan->view:NULL; }
bool qa_team_arena_source_baseline(const qa_cvars *source,qa_team_arena_setting out[7],qa_error *error)
{
    if (!source || !out) return fail(error,QA_ERROR_ARGUMENT,"Team Arena baseline requires the actual GAME cvar owner");
    static const char *const names[7]={"sv_maxclients","capturelimit","fraglimit","g_doWarmup","g_warmup","sv_pure","g_friendlyFire"};
    static const char *const saved[7]={"ui_maxClients","ui_saveCaptureLimit","ui_saveFragLimit","ui_doWarmup","ui_Warmup","ui_pure","ui_friendlyFire"};
    static const char *const defaults[7]={"0","8","20","0","20","0","0"};
    for (size_t i=0;i<7;++i) {
        const qa_cvar_view *value=qa_cvars_find(source,names[i]);
        out[i]=(qa_team_arena_setting){saved[i],value?value->value:defaults[i]};
    }
    return true;
}
bool qa_team_arena_client_baseline(const qa_cvars *source,qa_team_arena_setting *out,qa_error *error)
{
    if (!source || !out) return fail(error,QA_ERROR_ARGUMENT,"Team Arena timer baseline requires the actual local CGAME cvar owner");
    const qa_cvar_view *value=qa_cvars_find(source,"cg_drawTimer");
    *out=(qa_team_arena_setting){"ui_drawTimer",value?value->value:"0"}; return true;
}
bool qa_team_arena_campaign_plan(qa_team_arena_campaign *campaign,const qa_team_arena_cursor *cursor,
    uint32_t skill,const char *player,const char *opponent,qa_team_arena_skirmish **out,qa_error *error)
{
    if (!qa_team_arena_campaign_ready(campaign,error) || !out || *out || skill<1 || skill>5 || campaign->references==SIZE_MAX)
        return fail(error,QA_ERROR_ARGUMENT,"Team Arena planning requires a valid skill and empty plan output");
    qa_team_arena_cursor current=cursor?*cursor:(qa_team_arena_cursor){3,0};
    if (current.game_type_index>=campaign->game_type_count || current.map_index>=campaign->map_count)
        return fail(error,QA_ERROR_FORMAT,"Invalid Team Arena campaign cursor");
    int64_t type=campaign->game_types[current.game_type_index]; campaign_map *map=campaign->maps+current.map_index;
    if (!(type==1 || type==4 || type==5 || type==6 || type==7) || !active(map,type))
        return fail(error,QA_ERROR_FORMAT,"Selected Team Arena map is not authored for this single-player game type");
    qa_team_arena_skirmish *plan=calloc(1,sizeof(*plan));
    if (!plan) return fail(error,QA_ERROR_MEMORY,"Allocating authored Team Arena launch plan");
    plan->campaign=campaign; ++campaign->references;
    plan->player=copy(player?player:"Pagans",error); plan->opponent=copy(opponent?opponent:"Stroggs",error);
    qa_team_arena_skirmish_view *view=&plan->view;
    *view=(qa_team_arena_skirmish_view){.cursor=current,.map=map->view.map,.title=map->view.title,
        .game_type=(uint32_t)type,.skill=skill,.max_clients=type==1?2:map->view.team_members*2,
        .player_team=type==1?"free":"Red",.player_model=type==1?"sarge":"james",
        .player_head_model=type==1?"sarge":"*james",.source_cvars=plan->source,.source_cvar_count=20,
        .client_cvars=plan->client,.client_cvar_count=3,.bots=plan->bots};
    for (size_t i=0;i<map->count;++i) if (map->times[i].type==type) view->time_to_beat=map->times[i].time;
    bool ok=plan->player && plan->opponent;
    if (ok && type==1) { plan->bots[0]=(qa_team_arena_campaign_bot){map->view.opponent,map->view.opponent,"",500}; view->bot_count=1; }
    else if (ok) ok=append_team(plan,plan->opponent,"Blue",map->view.team_members,error) &&
        append_team(plan,plan->player,"Red",map->view.team_members-1,error);
    if (!ok) { qa_team_arena_skirmish_destroy(plan); return false; }
    size_t preceding=0;
    for (size_t i=0;i<current.map_index;++i) if (active(campaign->maps+i,type)) ++preceding;
    snprintf(plan->numbers[0],32,"%" PRId64,view->time_to_beat);
    snprintf(plan->numbers[1],32,"%u",view->game_type); snprintf(plan->numbers[2],32,"%u",skill);
    snprintf(plan->numbers[3],32,"%u",view->max_clients);
    snprintf(plan->numbers[4],32,"%zu",current.game_type_index); snprintf(plan->numbers[5],32,"%zu",current.map_index);
    snprintf(plan->numbers[6],32,"%zu",preceding);
    const qa_team_arena_setting source[20]={{"nextmap","teamarena-results"},{"ui_teamArenaTimeToBeat",plan->numbers[0]},
        {"g_gametype",plan->numbers[1]},{"ui_singlePlayerActive","1"},{"g_spSkill",plan->numbers[2]},
        {"sv_maxclients",plan->numbers[3]},{"g_doWarmup","1"},{"g_warmup","15"},{"sv_pure","0"},
        {"g_friendlyFire","0"},{"g_redTeam",plan->player},{"g_blueTeam",plan->opponent},
        {"capturelimit",type==6?"4":type==7?"15":"5"},{"fraglimit","10"},{"ui_scoreMap",map->view.title},
        {"ui_gameType",plan->numbers[4]},{"ui_currentMap",plan->numbers[5]},{"ui_mapIndex",plan->numbers[6]},
        {"ui_teamName",plan->player},{"ui_opponentName",plan->opponent}};
    memcpy(plan->source,source,sizeof(source));
    plan->client[0]=(qa_team_arena_setting){"cg_cameraOrbit","0"};
    plan->client[1]=(qa_team_arena_setting){"cg_thirdPerson","0"};
    plan->client[2]=(qa_team_arena_setting){"cg_drawTimer","1"};
    *out=plan; return true;
}
size_t qa_team_arena_campaign_resource_count(const qa_team_arena_campaign *campaign)
{ return campaign?campaign->resource_count:0; }
const qa_resource *qa_team_arena_campaign_resource_at(const qa_team_arena_campaign *campaign,size_t index,const char **path)
{
    if (!campaign || index>=campaign->resource_count) return NULL;
    if (path) *path=campaign->resources[index].path;
    return campaign->resources[index].resource;
}
static const uint8_t magic[8]={'Q','A','T','C',1,0,0,0};
static bool authored_path(const char *path)
{
    if (!path || strncmp(path,"scripts/",8)) return false;
    size_t size=strlen(path); if (size<13) return false;
    const char *suffix=path+size-5;
    for (size_t i=0;i<5;++i) {
        unsigned char c=(unsigned char)suffix[i];
        if (c>='A' && c<='Z') c=(unsigned char)(c+'a'-'A');
        if (c!=(unsigned char)".team"[i]) return false;
    }
    const char *first=path+8;
    for (const char *p=first;;++p) {
        if (*p=='\\') return false;
        if (*p && *p!='/') continue;
        size_t count=(size_t)(p-first);
        if (!count || (count==1 && *first=='.') || (count==2 && first[0]=='.' && first[1]=='.')) return false;
        if (!*p) return true;
        first=p+1;
    }
}
bool qa_team_arena_campaign_checkpoint(const qa_team_arena_campaign *source,
    const qa_base_arena_catalog_refs *refs,qa_buffer *out,qa_error *error)
{
    if (!qa_team_arena_campaign_ready(source,error) || !refs || !refs->resource_encode || !out || out->data || out->size)
        return fail(error,QA_ERROR_ARGUMENT,"Team Arena capture needs actual resource bindings and empty output");
    qa_team_arena_campaign *campaign=(qa_team_arena_campaign *)source; campaign->busy=true;
    qa_source_save_io io={0}; uint32_t policy=campaign->policy; size_t count=campaign->resource_count;
    bool ok=qa_source_save_writer(&io,NULL,error) && ps_magic(&io,magic) && qa_source_save_u32(&io,&policy) &&
        qa_source_save_count(&io,&count,SIZE_MAX);
    for (size_t i=0;ok && i<count;++i) {
        uint64_t reference=0; char *path=campaign->resources[i].path;
        ok=refs->resource_encode(refs->context,campaign->resources[i].resource,path,&reference,error) &&
            qa_source_save_owned_text(&io,&path) && qa_source_save_u64(&io,&reference);
    }
    if (ok) ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); campaign->busy=false; return ok;
}
bool qa_team_arena_campaign_restore(qa_bytes bytes,const qa_base_arena_catalog_refs *refs,
    qa_team_arena_campaign **out,qa_error *error)
{
    if (!refs || !refs->resource_decode || !out || *out)
        return fail(error,QA_ERROR_ARGUMENT,"Team Arena restore needs actual retained source resource bindings");
    qa_team_arena_campaign *campaign=calloc(1,sizeof(*campaign));
    if (!campaign) return fail(error,QA_ERROR_MEMORY,"Allocating detached authored Team Arena campaign");
    campaign->references=1; qa_source_save_io io={0}; uint32_t policy=0; size_t count=0;
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && ps_magic(&io,magic) &&
        qa_source_save_u32(&io,&policy) && policy<=QA_TEAM_ARENA_CATALOG_DEMO &&
        ps_count(&io,&count,17,sizeof(campaign_resource)) && count>=2 &&
        (policy!=QA_TEAM_ARENA_CATALOG_DEMO || count==2);
    campaign->policy=(qa_team_arena_catalog_policy)policy;
    for (size_t i=0;ok && i<count;++i) {
        char *path=NULL; uint64_t reference=0; qa_resource *resource=NULL;
        ok=qa_source_save_owned_text(&io,&path) && path &&
            (i==0?!strcmp(path,game_path(campaign->policy)):i==1?!strcmp(path,team_path(campaign->policy)):authored_path(path)) &&
            qa_source_save_u64(&io,&reference) && refs->resource_decode(refs->context,reference,path,&resource,error) && resource;
        if (ok) ok=add_resource(campaign,resource,path,error);
        free(path);
    }
    if (ok) ok=qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!ok) { qa_team_arena_campaign_destroy(campaign); if (!error || error->code==QA_OK) fail(error,QA_ERROR_FORMAT,"Saved Team Arena authored resources are inconsistent"); return false; }
    *out=campaign; return true;
}
