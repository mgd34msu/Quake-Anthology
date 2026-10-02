#include "campaign.h"
#include "campaign_ui.h"
#include "campaign_cinematic.h"
#include "qa/settings.h"
#include "qa/player_progress.h"
#include "qa/q3_product_policy.h"
#include "qa/audio_save.h"
#include "qa/application_startup_prepare.h"
#include "save_private.h"
#include "qa/binary.h"
#include <SDL.h>
#include <inttypes.h>
#include <stdio.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <unistd.h>
#include <fcntl.h>
#ifdef __linux__
#include <sys/random.h>
#endif
#endif

typedef enum campaign_kind { CAMPAIGN_BASE, CAMPAIGN_TEAM } campaign_kind;
typedef struct campaign_player { int32_t client,rank,score; } campaign_player;
typedef struct campaign_request {
    struct campaign_request *next;
    qa_command_context context;
    char **arguments;
    size_t count;
} campaign_request;
struct frontend_campaign {
    qa_frontend *frontend;
    const qa_launch_snapshot *publication;
    qa_actor_owner source;
    qa_vfs *content;
    qa_cvars *cvars;
    qa_fs_root *config_root;
    uint64_t map_revision;
    int32_t match_start;
    campaign_kind kind;
    qa_base_arena_catalog *catalog;
    qa_arena_progress progression;
    qa_team_arena_progress *team;
    campaign_request *requests,**tail;
    qa_arena_result base_game;
    qa_arena_postgame base_result;
    campaign_player players[8];
    size_t player_count;
    int32_t player_client;
    qa_team_arena_score_result team_result;
    uint64_t result_counter,result_frequency;
    bool result,announced,movie_played,music_attached,draining,restore_pending;
    bool ui_pending;
    frontend_campaign_ui_action ui_action;
    int32_t ui_arena,ui_skill;
    uint32_t ui_seat;
    qa_actor_owner ui_receiver;
    qa_actor_id ui_actor;
};
static const char *const base_archive_names[8]={"g_spScores1","g_spScores2","g_spScores3","g_spScores4",
    "g_spScores5","g_spAwards","g_spVideos","g_spCompletedRound"};
void frontend_campaign_audio_stopped(qa_frontend *f)
{ if (f && f->campaign) f->campaign->music_attached=false; }
static char *campaign_copy(const char *text,qa_error *error)
{
    size_t size=strlen(text); char *copy=size<SIZE_MAX?malloc(size+1):NULL;
    if (!copy) { frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual source campaign text"); return NULL; }
    memcpy(copy,text,size+1); return copy;
}
static const char *campaign_value(const qa_cvars *cvars,const char *name)
{ const qa_cvar_view *value=qa_cvars_find(cvars,name); return value?value->value:""; }
static int32_t campaign_word(uint32_t value)
{ int32_t result; memcpy(&result,&value,sizeof(result)); return result; }
static int32_t campaign_number(const campaign_request *request,size_t index)
{
    const unsigned char *text=(const unsigned char *)(index+1<request->count?request->arguments[index+1]:"");
    size_t length=strlen((const char *)text),at=0; if (length>1023) length=1023;
    while (at<length && (text[at]<=32 || text[at]>=128)) ++at;
    bool negative=at<length && text[at]=='-';
    if (at<length && (text[at]=='+' || text[at]=='-')) ++at;
    uint32_t value=0;
    while (at<length && text[at]>='0' && text[at]<='9') value=value*10u+(uint32_t)(text[at++]-'0');
    return campaign_word(negative?0u-value:value);
}
static bool campaign_uuid(char out[37],qa_error *error)
{
    uint8_t bytes[16]; bool ready=false;
#ifdef _WIN32
    ready=BCryptGenRandom(NULL,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0;
#else
    size_t count=0;
#ifdef __linux__
    while (count<sizeof(bytes)) {
        ssize_t got=getrandom(bytes+count,sizeof(bytes)-count,0);
        if (got>0) count+=(size_t)got; else if (got<0 && errno==EINTR) continue; else break;
    }
#else
    int descriptor=open("/dev/urandom",O_RDONLY);
    if (descriptor>=0) {
        while (count<sizeof(bytes)) {
            ssize_t got=read(descriptor,bytes+count,sizeof(bytes)-count);
            if (got>0) count+=(size_t)got; else if (got<0 && errno==EINTR) continue; else break;
        }
        close(descriptor);
    }
#endif
    ready=count==sizeof(bytes);
#endif
    if (!ready) return frontend_fail(error,QA_ERROR_IO,"Source round identity requires operating-system entropy");
    bytes[6]=(uint8_t)((bytes[6]&15u)|64u); bytes[8]=(uint8_t)((bytes[8]&63u)|128u);
    static const char hex[]="0123456789abcdef"; size_t at=0;
    for (size_t i=0;i<16;++i) {
        if (i==4 || i==6 || i==8 || i==10) out[at++]='-';
        out[at++]=hex[bytes[i]>>4]; out[at++]=hex[bytes[i]&15u];
    }
    out[at]=0; return true;
}
static void campaign_request_destroy(campaign_request *request)
{
    if (!request) return;
    for (size_t i=0;i<request->count;++i) free(request->arguments[i]);
    free(request->arguments); free(request);
}
bool frontend_campaign_ready(const qa_frontend *f)
{ return f && (!f->campaign || (!f->campaign->draining && !f->campaign->requests && !f->campaign->ui_pending)); }
bool frontend_campaign_destroy(qa_frontend *f,qa_error *error)
{
    if (!f || !f->campaign) return true;
    frontend_campaign *owner=f->campaign;
    if (owner->draining) return frontend_fail(error,QA_ERROR_ARGUMENT,"Source campaign callback still owns its result context");
    while (owner->requests) {
        campaign_request *request=owner->requests; owner->requests=request->next; campaign_request_destroy(request);
    }
    if (owner->music_attached) qa_audio_engine_remove_music(f->audio,QA_FRONTEND_COMMAND_OWNER);
    qa_base_arena_catalog_destroy(owner->catalog); qa_team_arena_progress_destroy(owner->team);
    qa_launch_snapshot_release(owner->publication); free(owner); f->campaign=NULL; return true;
}
static bool campaign_has_q3(const qa_frontend *f)
{
    const qa_launch_snapshot *publication=qa_application_launch(f->application);
    const qa_launch_choices *choices=qa_launch_snapshot_choices(publication);
    const qa_launch_binding *binding=choices?qa_launch_binding_for(choices,(qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,""):NULL;
    const qa_launch_instance *instance=binding?qa_launch_snapshot_find(publication,binding->instance):NULL;
    const qa_product *product=instance?qa_catalog_product(qa_launch_snapshot_catalog(publication),instance->selection.product):NULL;
    return product && product->family==QA_GAME_Q3;
}
bool frontend_campaign_sync(qa_frontend *f,qa_error *error)
{
    if (!f || !f->application || f->capture) return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign setup requires its actual frontend source owner");
    if (qa_application_startup_pending(f->application)) return true;
    if (!campaign_has_q3(f)) return frontend_campaign_destroy(f,error);
    qa_application_q3_campaign view;
    if (!qa_application_q3_campaign_read(f->application,0,&view,error)) return false;
    bool base=view.product==QA_Q3_ARENA && view.game_type==2;
    bool team=view.product==QA_Q3_TEAM_ARENA && !strcmp(campaign_value(view.cvars,"nextmap"),"teamarena-results");
    if (!base && !team) return frontend_campaign_destroy(f,error);
    frontend_campaign *owner=f->campaign;
    if (owner && owner->source==view.source_owner && owner->content==view.content && owner->cvars==view.cvars &&
        owner->config_root==view.config_root && owner->map_revision==view.map_revision && owner->match_start==view.match_start_time &&
        owner->kind==(base?CAMPAIGN_BASE:CAMPAIGN_TEAM)) return true;
    if (!frontend_campaign_ready(f) || !frontend_campaign_destroy(f,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign source changed before its accepted command drained");
    owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating actual source campaign continuation");
    owner->frontend=f; owner->source=view.source_owner; owner->content=view.content; owner->cvars=view.cvars;
    owner->config_root=view.config_root; owner->map_revision=view.map_revision; owner->match_start=view.match_start_time;
    owner->kind=base?CAMPAIGN_BASE:CAMPAIGN_TEAM; owner->publication=view.publication;
    qa_launch_snapshot_retain(owner->publication); owner->tail=&owner->requests;
    bool ok=true;
    if (base) {
        ok=qa_base_arena_catalog_create(view.content,&owner->catalog,error) &&
            qa_arena_progress_init(&owner->progression,view.cvars,qa_base_arena_catalog_levels(owner->catalog),view.source_owner,error) &&
            qa_cvars_register(view.cvars,"g_spRoundIdentity","",QA_CVAR_ARCHIVE,view.source_owner,NULL,error) &&
            qa_cvars_register(view.cvars,"g_spCompletedRound","",QA_CVAR_ARCHIVE,view.source_owner,NULL,error);
        const qa_base_arena *arena=ok?qa_base_arena_catalog_find(owner->catalog,view.map):NULL;
        if (ok && arena && !f->source_restoring) {
            char selection[24]; (void)snprintf(selection,sizeof(selection),"%" PRId32,arena->selection);
            ok=qa_cvars_register(view.cvars,"ui_spSelection","0",QA_CVAR_ARCHIVE,view.source_owner,NULL,error) &&
                qa_cvars_set(view.cvars,"ui_spSelection",selection,true,error);
        }
        if (ok && (!f->source_restoring || !*campaign_value(view.cvars,"g_spRoundIdentity"))) {
            char identity[37]; ok=campaign_uuid(identity,error) && qa_cvars_set(view.cvars,"g_spRoundIdentity",identity,true,error);
        }
    } else ok=view.config_root && qa_team_arena_progress_create(view.config_root,&owner->team,error);
    if (ok) ok=qa_application_q3_campaign_current(f->application,&view);
    f->campaign=owner;
    if (!ok) {
        (void)frontend_campaign_destroy(f,NULL);
        if (!error || error->code==QA_OK) frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign construction lost its actual GAME registry/content/config root");
    }
    return ok;
}
bool frontend_campaign_source_command(void *context,qa_application *application,const qa_command_invocation *invocation,
    bool *handled,qa_error *error)
{
    if (handled) *handled=false;
    if (!handled || !invocation || !invocation->argc || !invocation->argv) return false;
    const char *name=invocation->argv[0];
    if (!strcmp(name,"cinematic") || !strcmp(name,"cinematicpause") || !strcmp(name,"stopcinematic")) {
        qa_frontend *f=context;
        if (!f || application!=f->application || f->source_restoring || f->capture) return false;
        *handled=true;
        return frontend_cinematic_command(f,invocation,error);
    }
    if (strcmp(name,"postgame") && strcmp(name,"spPostgame") && strcmp(name,"arena-reset") &&
        strcmp(name,"arena-unlock") && strcmp(name,"arena-medals") && strcmp(name,"teamarena-results")) return true;
    qa_frontend *f=context;
    if (!f || application!=f->application || f->source_restoring || f->capture || !frontend_campaign_sync(f,error)) return false;
    if (!f->campaign) return true;
    qa_application_q3_campaign view;
    bool postgame=!strcmp(name,"postgame") || !strcmp(name,"spPostgame");
    if (!qa_application_q3_campaign_read(application,f->campaign->source,&view,error) ||
        (postgame && invocation->console!=view.console) ||
        (postgame && !qa_application_q3_campaign_postgame_context(application,&view,&invocation->context,error)) ||
        (!postgame && (!qa_application_command_context_active(application,&invocation->context) ||
            (invocation->context.owner && invocation->context.owner!=view.source_owner)))) return false;
    if (invocation->argc>SIZE_MAX/sizeof(char *)) return frontend_fail(error,QA_ERROR_MEMORY,"Source campaign argument count overflows actual storage");
    campaign_request *request=calloc(1,sizeof(*request));
    if (!request) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining source campaign command");
    request->arguments=calloc(invocation->argc,sizeof(*request->arguments));
    if (!request->arguments) { free(request); return frontend_fail(error,QA_ERROR_MEMORY,"Retaining source campaign arguments"); }
    request->context=invocation->context; request->count=invocation->argc;
    for (size_t i=0;i<request->count;++i) {
        if (!invocation->argv[i] || !(request->arguments[i]=campaign_copy(invocation->argv[i],error))) {
            campaign_request_destroy(request); return false;
        }
    }
    if (!qa_application_q3_campaign_current(application,&view)) { campaign_request_destroy(request); return false; }
    *f->campaign->tail=request; f->campaign->tail=&request->next; *handled=true; return true;
}

static bool campaign_archive(qa_frontend *f,const qa_application_q3_campaign *view,qa_error *error)
{
    const qa_product *product=qa_catalog_product(qa_launch_snapshot_catalog(view->publication),view->content_product);
    if (!product || !product->key || !view->launch->selection.implementation || !view->configuration || !view->write_mount ||
        !qa_application_q3_campaign_current(f->application,view))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source campaign archive lost its actual product/provider writable mount");
    const char *owner[3]={"source",product->key,view->launch->selection.implementation};
    return qa_settings_save_cvars((qa_settings_store){view->configuration,view->write_mount},owner,3,view->cvars,error) &&
        qa_application_q3_campaign_current(f->application,view);
}
static void campaign_restore_values(qa_frontend *f,const qa_application_q3_campaign *view,char *const before[8])
{
    for (size_t i=0;i<8 && qa_application_q3_campaign_current(f->application,view);++i) {
        qa_error ignored={0}; (void)qa_cvars_set(view->cvars,base_archive_names[i],before[i],true,&ignored);
    }
}
static bool campaign_music(qa_frontend *f,const qa_application_q3_campaign *view,const char *cue,qa_error *error)
{
    frontend_campaign *owner=f->campaign;
    if (!f->audio) return true;
    qa_audio_music *music=qa_audio_engine_bus_music(f->audio,QA_FRONTEND_COMMAND_OWNER);
    if (!owner->music_attached) {
        if (music) return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign music would replace another actual frontend music owner");
        if (!qa_audio_music_create(qa_audio_engine_rate(f->audio),QA_AUDIO_Q3,true,&music,error)) return false;
        if (!qa_audio_engine_music(f->audio,QA_FRONTEND_COMMAND_OWNER,QA_AUDIO_WORLD,1,music,error)) {
            qa_audio_music_destroy(music); return false;
        }
        owner->music_attached=true;
    }
    if (!music || !qa_audio_engine_music_ready(f->audio,QA_FRONTEND_COMMAND_OWNER,QA_AUDIO_WORLD,1))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign music lost its actual world bus");
    qa_audio_music_stop(music);
    qa_audio_bank *bank=NULL; qa_audio_stream *intro=NULL;
    bool ok=qa_application_q3_campaign_current(f->application,view) &&
        qa_audio_bank_create(view->content,&bank,error) &&
        qa_audio_bank_music_cue(bank,cue,QA_AUDIO_Q3,NULL,NULL,&intro,error) &&
        qa_application_q3_campaign_current(f->application,view);
    qa_audio_bank_destroy(bank);
    if (!ok) { qa_audio_stream_close(intro); return false; }
    if (intro) qa_audio_music_start(music,intro,NULL);
    return true;
}
static bool campaign_base_complete(qa_frontend *f,const qa_application_q3_campaign *view,
    const campaign_request *request,qa_error *error)
{
    frontend_campaign *owner=f->campaign;
    const qa_base_arena *arena=qa_base_arena_catalog_find(owner->catalog,view->map);
    if (!arena) return frontend_fail(error,QA_ERROR_NOT_FOUND,"Postgame map has no actual authored single-player arena");
    const qa_cvar_view *skill_value=qa_cvars_find(view->cvars,"g_spSkill"); float raw_skill=skill_value?skill_value->number:0;
    int32_t skill=raw_skill>=5?5:raw_skill>=4?4:raw_skill>=3?3:raw_skill>=2?2:1;
    qa_arena_result game={.level=arena->number,.skill=skill,.rank=8,
        .accuracy=campaign_number(request,2),.impressive=campaign_number(request,3),.excellent=campaign_number(request,4),
        .gauntlet=campaign_number(request,5),.frags=campaign_number(request,6),.perfect=campaign_number(request,7)!=0};
    campaign_player players[8]={0}; int32_t count=campaign_number(request,0),player=campaign_number(request,1);
    size_t player_count=count<=0?0:count>=8?8:(size_t)count;
    for (size_t i=0;i<player_count;++i) {
        int32_t rank=campaign_word(((uint32_t)campaign_number(request,9+i*3)&~UINT32_C(64))+1u);
        players[i]=(campaign_player){campaign_number(request,8+i*3),rank,campaign_number(request,10+i*3)};
        if (players[i].client==player) game.rank=rank;
    }
    const char *identity=campaign_value(view->cvars,"g_spRoundIdentity"); size_t length=strlen(identity);
    if (length>SIZE_MAX-32) return frontend_fail(error,QA_ERROR_MEMORY,"Source round identity exceeds addressable text");
    char *round=malloc(length+32);
    if (!round) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual source completed-round identity");
    (void)snprintf(round,length+32,"%s:%" PRId32,identity,view->match_start_time);
    qa_arena_postgame result={.rank=game.rank,.completed_tier=-1,.unlocked_movie=-1,.next_level=-1};
    bool duplicate=!strcmp(campaign_value(view->cvars,"g_spCompletedRound"),round),ok=true;
    if (duplicate) ok=qa_arena_progress_tier(&owner->progression,arena->number,&result.completed_tier,error) &&
        qa_arena_progress_current(&owner->progression,&result.next_level,error);
    bool local=false; uint32_t seat=0; qa_actor_id actor={0};
    if (ok && !duplicate) ok=qa_application_q3_campaign_local_seat(f->application,view,(uint32_t)player,&local,&seat,&actor,error);
    if (ok && !duplicate && local) {
        if (!view->profile) ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Local arena completion requires its actual application profile owner");
        else {
            char participant[64]; (void)snprintf(participant,sizeof(participant),"local-seat:%" PRIu32,seat);
            qa_progress_event event={.kind=QA_PROGRESS_MATCH_COMPLETED,.source=QA_GAME_Q3,
                .participant={(const uint8_t *)participant,strlen(participant)},.event={(const uint8_t *)round,strlen(round)},
                .value.match={.map={(const uint8_t *)arena->map,strlen(arena->map)},.score=game.frags}};
            bool inserted=false; ok=qa_player_progress_record(view->profile,&event,&inserted,error) &&
                qa_application_q3_campaign_current(f->application,view);
        }
    }
    char *before[8]={0}; bool mutated=false;
    if (ok && !duplicate) {
        for (size_t i=0;ok && i<8;++i) ok=(before[i]=campaign_copy(campaign_value(view->cvars,base_archive_names[i]),error))!=NULL;
        if (ok) {
            mutated=true;
            ok=qa_arena_progress_record(&owner->progression,&game,&result,error) &&
                qa_application_q3_campaign_current(f->application,view) &&
                qa_cvars_set(view->cvars,"g_spCompletedRound",round,true,error) && campaign_archive(f,view,error);
        }
        if (!ok && mutated) campaign_restore_values(f,view,before);
    }
    for (size_t i=0;i<8;++i) free(before[i]); free(round);
    if (!ok) return false;
    owner->base_game=game; owner->base_result=result; memcpy(owner->players,players,sizeof(players));
    owner->player_count=player_count; owner->player_client=player; owner->result=true;
    owner->announced=duplicate; owner->movie_played=duplicate;
    if (!duplicate) { owner->result_counter=SDL_GetPerformanceCounter(); owner->result_frequency=SDL_GetPerformanceFrequency(); }
    return duplicate || campaign_music(f,view,result.rank==1?"music/win":"music/loss",error);
}
static bool campaign_base_action(qa_frontend *f,const qa_application_q3_campaign *view,const char *name,qa_error *error)
{
    frontend_campaign *owner=f->campaign;
    if (owner->kind!=CAMPAIGN_BASE) return frontend_fail(error,QA_ERROR_ARGUMENT,"No actual Base Arena progression is active");
    bool ok=!strcmp(name,"arena-reset")?qa_arena_progress_reset(&owner->progression,error):
        !strcmp(name,"arena-unlock")?qa_arena_progress_unlock_levels(&owner->progression,error):
        qa_arena_progress_unlock_medals(&owner->progression,error);
    if (!strcmp(name,"arena-reset") && ok) {
        owner->result=false; owner->announced=owner->movie_played=false;
        owner->result_counter=owner->result_frequency=0;
    }
    return ok && campaign_archive(f,view,error);
}
static bool campaign_team_complete(qa_frontend *f,const qa_application_q3_campaign *view,
    const campaign_request *request,qa_error *error)
{
    frontend_campaign *owner=f->campaign;
    const qa_cvar_view *skill=qa_cvars_find(view->cvars,"g_spSkill"),*beat=qa_cvars_find(view->cvars,"ui_teamArenaTimeToBeat");
    qa_team_arena_score_input input={.match_start_time=view->match_start_time,
        .skill=skill?skill->number:0,.time_to_beat=beat?beat->number:0,
        .stats={campaign_number(request,2),campaign_number(request,3),campaign_number(request,4),
            campaign_number(request,5),campaign_number(request,6),campaign_number(request,7),campaign_number(request,8),
            campaign_number(request,9),campaign_number(request,10),campaign_number(request,11),campaign_number(request,12),campaign_number(request,13)}};
    qa_team_arena_score_result result;
    const char *map=view->map; if (!strncmp(map,"maps/",5)) map+=5;
    char *score_map=campaign_copy(map,error);
    if (!score_map) return false;
    size_t map_length=strlen(score_map);
    if (map_length>=4 && !strcmp(score_map+map_length-4,".bsp")) score_map[map_length-4]=0;
    bool recorded=qa_team_arena_progress_record(owner->team,score_map,view->game_type,&input,&result,error);
    free(score_map);
    if (!recorded ||
        !qa_application_q3_campaign_current(f->application,view)) return false;
    char score[32],time[64],teams[64];
    (void)snprintf(score,sizeof(score),"%" PRId32,result.current.score);
    (void)snprintf(time,sizeof(time),"%02" PRId32 ":%02" PRId32,result.current.time/60,result.current.time%60);
    (void)snprintf(teams,sizeof(teams),"%" PRId32 " to %" PRId32,result.current.red_score,result.current.blue_score);
    if (!qa_cvars_set(view->cvars,"ui_scoreScore",score,true,error) || !qa_application_q3_campaign_current(f->application,view) ||
        !qa_cvars_set(view->cvars,"ui_scoreTime",time,true,error) || !qa_application_q3_campaign_current(f->application,view) ||
        !qa_cvars_set(view->cvars,"ui_scoreTeam",teams,true,error) || !qa_application_q3_campaign_current(f->application,view)) return false;
    static const char *const overrides[6][2]={{"capturelimit","ui_saveCaptureLimit"},{"fraglimit","ui_saveFragLimit"},
        {"g_doWarmup","ui_doWarmup"},{"g_warmup","ui_Warmup"},{"sv_pure","ui_pure"},{"g_friendlyFire","ui_friendlyFire"}};
    for (size_t i=0;i<6;++i) {
        char *saved=campaign_copy(campaign_value(view->cvars,overrides[i][1]),error);
        if (!saved) return false;
        bool ok=qa_cvars_set(view->cvars,overrides[i][0],saved,true,error) && qa_application_q3_campaign_current(f->application,view);
        free(saved); if (!ok) return false;
    }
    owner->team_result=result; owner->result=true;
    owner->result_counter=SDL_GetPerformanceCounter(); owner->result_frequency=SDL_GetPerformanceFrequency();
    if (result.new_high_score) {
        char message[96]; (void)snprintf(message,sizeof(message),"New Team Arena high score: %" PRId32 "\n",result.current.score);
        frontend_print(f,message);
    }
    return true;
}
bool frontend_campaign_drain(qa_frontend *f,qa_error *error)
{
    if (!f || !f->application || f->capture) return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign drain requires its installed frontend owner");
    if (qa_application_startup_pending(f->application)) return true;
    frontend_campaign *owner=f->campaign;
    if (!owner) return frontend_campaign_sync(f,error);
    if (owner->restore_pending) return frontend_fail(error,QA_ERROR_ARGUMENT,"Candidate campaign has not reached its genuine publication boundary");
    if (owner->draining) return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign command drain reentered its source result owner");
    owner->draining=true; bool ok=true;
    while (ok && owner->requests) {
        campaign_request *request=owner->requests; owner->requests=request->next;
        if (!owner->requests) owner->tail=&owner->requests;
        qa_application_q3_campaign view;
        ok=qa_application_command_context_active(f->application,&request->context) &&
            qa_application_q3_campaign_read(f->application,owner->source,&view,error) &&
            view.cvars==owner->cvars && view.content==owner->content && view.config_root==owner->config_root &&
            view.map_revision==owner->map_revision && view.match_start_time==owner->match_start;
        const char *name=request->arguments[0];
        if (ok && (!strcmp(name,"postgame") || !strcmp(name,"spPostgame")))
            ok=owner->kind==CAMPAIGN_BASE?campaign_base_complete(f,&view,request,error):campaign_team_complete(f,&view,request,error);
        else if (ok && !strcmp(name,"teamarena-results"))
            ok=owner->kind==CAMPAIGN_TEAM || frontend_fail(error,QA_ERROR_ARGUMENT,"No actual Team Arena skirmish is active");
        else if (ok) ok=campaign_base_action(f,&view,name,error);
        campaign_request_destroy(request);
    }
    owner->draining=false;
    if (!ok && (!error || error->code==QA_OK)) frontend_fail(error,QA_ERROR_ARGUMENT,"Accepted campaign command lost its actual source round");
    return ok;
}

static bool campaign_catalog_encode(void *context,const qa_resource *resource,const char *path,
    uint64_t *identity,qa_error *error)
{
    const qa_application_q3_campaign *view=context;
    qa_resource_pool *pool=qa_vfs_resources(view->content);
    if (!path || !*path || !resource || !identity ||
        qa_resource_pool_find(pool,qa_resource_id(resource))!=resource)
        return frontend_fail(error,QA_ERROR_FORMAT,"Authored campaign resource leaves its actual GAME content pool");
    *identity=qa_resource_id(resource); return *identity!=0;
}
static bool campaign_catalog_decode(void *context,uint64_t identity,const char *path,
    qa_resource **out,qa_error *error)
{
    const qa_application_q3_campaign *view=context;
    const qa_resource *resource=qa_resource_pool_find(qa_vfs_resources(view->content),identity);
    if (!identity || !path || !*path || !resource || !out)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved campaign resource is absent from its genuine GAME content pool");
    *out=(qa_resource *)resource; return true;
}
bool frontend_campaign_content_visit(const qa_frontend *f,const qa_application_content_visitor *visitor,qa_error *error)
{
    if (!f || !visitor || !visitor->pool || !visitor->view || !frontend_campaign_ready(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign inventory requires its actual idle owner");
    const frontend_campaign *owner=f->campaign;
    if (!owner) return true;
    qa_resource_pool *pool=qa_vfs_resources(owner->content);
    if (!pool || !visitor->pool(visitor->context,pool,error) ||
        !visitor->view(visitor->context,owner->content,error)) return false;
    for (size_t i=0;i<qa_base_arena_catalog_resource_count(owner->catalog);++i) {
        const qa_resource *resource=qa_base_arena_catalog_resource_at(owner->catalog,i,NULL);
        if (!resource || qa_resource_pool_find(pool,qa_resource_id(resource))!=resource)
            return frontend_fail(error,QA_ERROR_FORMAT,"Campaign retained authored bytes have no actual source pool owner");
    }
    return true;
}
typedef struct campaign_saved {
    frontend_campaign state;
    uint64_t content;
    qa_bytes catalog,team;
    bool installed;
} campaign_saved;
static bool campaign_blob(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t size=bytes->size;
    if (!qa_source_save_count(io,&size,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (size>io->input.size-io->offset) return false;
        *bytes=(qa_bytes){io->input.data+io->offset,size}; io->offset+=size; return true;
    }
    return qa_source_save_bytes(io,(void *)bytes->data,size);
}
static bool campaign_game_fields(qa_source_save_io *io,qa_arena_result *game)
{
    return qa_source_save_i32(io,&game->level) && qa_source_save_i32(io,&game->skill) &&
        qa_source_save_i32(io,&game->rank) && qa_source_save_i32(io,&game->accuracy) &&
        qa_source_save_i32(io,&game->impressive) && qa_source_save_i32(io,&game->excellent) &&
        qa_source_save_i32(io,&game->gauntlet) && qa_source_save_i32(io,&game->frags) && qa_source_save_bool(io,&game->perfect);
}
static bool campaign_result_fields(qa_source_save_io *io,qa_arena_postgame *result)
{
    if (!qa_source_save_i32(io,&result->rank) || !qa_source_save_i32(io,&result->completed_tier) ||
        !qa_source_save_i32(io,&result->unlocked_movie) || !qa_source_save_i32(io,&result->next_level) ||
        !qa_source_save_count(io,&result->award_count,6)) return false;
    for (size_t i=0;i<result->award_count;++i)
        if (!qa_source_save_i32(io,&result->awards[i].medal) || !qa_source_save_i32(io,&result->awards[i].amount) ||
            result->awards[i].medal<0 || result->awards[i].medal>=6 || result->awards[i].amount<=0) return false;
    return true;
}
static bool campaign_score_fields(qa_source_save_io *io,qa_team_arena_score *score)
{
    uint8_t bytes[68];
    if (io->direction==QA_SOURCE_SAVE_WRITE) qa_team_arena_score_encode(score,bytes);
    if (!qa_source_save_bytes(io,bytes,sizeof(bytes))) return false;
    if (qa_load_u32le(bytes)!=64) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) qa_team_arena_score_decode((qa_bytes){bytes,sizeof(bytes)},score);
    return true;
}
static bool campaign_fields(qa_source_save_io *io,qa_frontend *f,campaign_saved *saved)
{
    uint8_t magic[4]={'Q','F','C','A'}; uint32_t version=3,kind=saved->state.kind;
    if (!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QFCA",sizeof(magic)) ||
        !qa_source_save_u32(io,&version) || version!=3 || !qa_source_save_bool(io,&saved->installed)) return false;
    if (!saved->installed) return true;
    frontend_campaign *state=&saved->state;
    if (!qa_source_save_u32(io,&kind) || kind>CAMPAIGN_TEAM ||
        !frontend_save_provider(io,f->application,&state->source) || !state->source ||
        !qa_source_save_u64(io,&saved->content) || !saved->content ||
        !qa_source_save_u64(io,&state->map_revision) || !qa_source_save_i32(io,&state->match_start) ||
        !qa_source_save_bool(io,&state->result) || !qa_source_save_bool(io,&state->announced) ||
        !qa_source_save_bool(io,&state->movie_played) || !qa_source_save_bool(io,&state->music_attached) ||
        !qa_source_save_u64(io,&state->result_counter) ||
        !qa_source_save_u64(io,&state->result_frequency) || !campaign_blob(io,&saved->catalog) ||
        !campaign_blob(io,&saved->team)) return false;
    state->kind=(campaign_kind)kind;
    if ((state->kind==CAMPAIGN_BASE && (!saved->catalog.size || saved->team.size)) ||
        (state->kind==CAMPAIGN_TEAM && (!saved->team.size || saved->catalog.size))) return false;
    if (!state->result) return !state->result_counter && !state->result_frequency && !state->announced && !state->movie_played;
    if ((!state->announced || !state->movie_played) && !state->result_frequency) return false;
    if (state->kind==CAMPAIGN_TEAM)
        return campaign_score_fields(io,&state->team_result.current) && campaign_score_fields(io,&state->team_result.previous) &&
            qa_source_save_bool(io,&state->team_result.won) && qa_source_save_bool(io,&state->team_result.new_high_score) &&
            qa_source_save_bool(io,&state->team_result.new_best_time);
    if (!campaign_game_fields(io,&state->base_game) || state->base_game.skill<1 || state->base_game.skill>5 ||
        !campaign_result_fields(io,&state->base_result) || state->base_result.rank!=state->base_game.rank ||
        !qa_source_save_i32(io,&state->player_client) || !qa_source_save_count(io,&state->player_count,8)) return false;
    for (size_t i=0;i<state->player_count;++i)
        if (!qa_source_save_i32(io,&state->players[i].client) || !qa_source_save_i32(io,&state->players[i].rank) ||
            !qa_source_save_i32(io,&state->players[i].score)) return false;
    return true;
}
static bool campaign_source_matches(const frontend_campaign *owner,const qa_application_q3_campaign *view)
{
    return owner->source==view->source_owner && owner->content==view->content && owner->cvars==view->cvars &&
        owner->config_root==view->config_root && owner->publication==view->publication &&
        owner->map_revision==view->map_revision && owner->match_start==view->match_start_time &&
        (owner->kind==CAMPAIGN_BASE?(view->product==QA_Q3_ARENA && view->game_type==2):
            (view->product==QA_Q3_TEAM_ARENA && !strcmp(campaign_value(view->cvars,"nextmap"),"teamarena-results")));
}
bool frontend_campaign_publish_ready(const qa_frontend *f,qa_error *error)
{
    if (!f || !frontend_campaign_ready(f)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign owner still retains an active command");
    const frontend_campaign *owner=f->campaign;
    if (!owner) return true;
    qa_application_q3_campaign view;
    if (owner->music_attached && (!f->audio || !qa_audio_engine_music_ready(f->audio,
        QA_FRONTEND_COMMAND_OWNER,QA_AUDIO_WORLD,1)))
        return frontend_fail(error,QA_ERROR_FORMAT,"Campaign music does not own its actual restored world bus");
    return qa_application_q3_campaign_read(f->application,owner->source,&view,error) && campaign_source_matches(owner,&view) &&
        (owner->kind==CAMPAIGN_BASE?qa_base_arena_catalog_ready(owner->catalog,error):qa_team_arena_progress_ready(owner->team,error)) &&
        (!owner->result_frequency || owner->result_frequency==SDL_GetPerformanceFrequency());
}
bool frontend_campaign_checkpoint(qa_frontend *f,const qa_application_content_graph *graph,qa_buffer *out,qa_error *error)
{
    if (!f || !f->application || f->stepping || f->source_restoring || !graph || !out || out->data || out->size ||
        !frontend_campaign_publish_ready(f,error)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign capture requires its actual idle installed owner");
    campaign_saved saved={.installed=f->campaign!=NULL}; qa_buffer catalog={0},team={0}; bool ok=true;
    if (saved.installed) {
        qa_application_q3_campaign view;
        ok=qa_application_q3_campaign_read(f->application,f->campaign->source,&view,error);
        saved.state=*f->campaign; saved.content=qa_application_content_view_id(graph,saved.state.content);
        qa_base_arena_catalog_refs refs={.context=&view,.resource_encode=campaign_catalog_encode};
        if (ok) ok=saved.state.kind==CAMPAIGN_BASE?qa_base_arena_catalog_checkpoint(saved.state.catalog,&refs,&catalog,error):
            qa_team_arena_progress_checkpoint(saved.state.team,&team,error);
        saved.catalog=(qa_bytes){catalog.data,catalog.size}; saved.team=(qa_bytes){team.data,team.size};
    }
    qa_source_save_io io={0};
    if (ok) ok=qa_source_save_writer(&io,NULL,error) && campaign_fields(&io,f,&saved) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&catalog); qa_buffer_free(&team);
    if (!ok && (!error || error->code==QA_OK)) frontend_fail(error,QA_ERROR_FORMAT,"Invalid actual source campaign continuation");
    return ok;
}
bool frontend_campaign_restore(qa_frontend *f,qa_application_content_graph *graph,qa_bytes bytes,qa_error *error)
{
    if (!f || !f->application || f->campaign || f->stepping || f->source_restoring || !graph)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign import requires its empty finished source candidate");
    campaign_saved saved={0}; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && campaign_fields(&io,f,&saved) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!ok || !saved.installed) {
        if (!ok && (!error || error->code==QA_OK)) frontend_fail(error,QA_ERROR_FORMAT,"Invalid saved campaign envelope");
        return ok;
    }
    frontend_campaign *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Restoring actual campaign result owner");
    *owner=saved.state; owner->frontend=f; owner->tail=&owner->requests; owner->restore_pending=true;
    owner->music_attached=false;
    qa_application_q3_campaign view;
    ok=qa_application_q3_campaign_read(f->application,owner->source,&view,error) &&
        qa_application_content_view(graph,saved.content)==view.content;
    if (ok) {
        owner->content=view.content; owner->cvars=view.cvars; owner->config_root=view.config_root;
        owner->publication=view.publication; qa_launch_snapshot_retain(owner->publication);
        ok=campaign_source_matches(owner,&view);
    }
    if (ok && owner->kind==CAMPAIGN_BASE) {
        qa_base_arena_catalog_refs refs={.context=&view,.resource_decode=campaign_catalog_decode};
        ok=qa_base_arena_catalog_restore(saved.catalog,&refs,&owner->catalog,error);
        if (ok) { owner->progression.cvars=view.cvars; owner->progression.catalog=qa_base_arena_catalog_levels(owner->catalog); }
        if (ok && owner->result) {
            const qa_base_arena *arena=qa_base_arena_catalog_find(owner->catalog,view.map);
            ok=arena && arena->number==owner->base_game.level;
        }
    } else if (ok) ok=view.config_root && qa_team_arena_progress_restore(saved.team,view.config_root,NULL,&owner->team,error);
    if (ok && saved.state.music_attached) {
        ok=f->audio && qa_audio_engine_music_ready(f->audio,QA_FRONTEND_COMMAND_OWNER,QA_AUDIO_WORLD,1);
        if (ok) owner->music_attached=true;
    }
    f->campaign=owner;
    if (ok) ok=frontend_campaign_publish_ready(f,error) && qa_application_q3_campaign_current(f->application,&view);
    if (!ok) {
        (void)frontend_campaign_destroy(f,NULL);
        if (!error || error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Saved campaign leaves its actual GAME round/content owners");
        return false;
    }
    return true;
}
void frontend_campaign_publish_restored(qa_frontend *f)
{
    if (!f || !f->campaign) return;
    if (f->campaign->team) qa_team_arena_progress_publish_restored(f->campaign->team);
    f->campaign->restore_pending=false;
}
bool frontend_campaign_ui_read(qa_frontend *f,uint32_t seat,frontend_campaign_ui_view *out,qa_error *error)
{
    if (!f || !out || seat>=f->options.seats || f->capture || f->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign menu requires its installed physical seat");
    *out=(frontend_campaign_ui_view){0};
    if (qa_application_startup_pending(f->application)) return true;
    frontend_campaign *owner=f->campaign;
    if (!owner || owner->restore_pending || owner->draining || frontend_network_remote(f)) return true;
    qa_actor_owner receiver=0;
    if (!frontend_source_cgame_recipient(f,seat,&receiver,error)) return false;
    if (!receiver) return true;
    uint32_t launch_seat;
    if (!frontend_seat_launch_id_read(f,seat,&launch_seat)) return true;
    if (!qa_application_q3_campaign_menu_read(f->application,receiver,launch_seat,&out->source,&out->client,error) ||
        !campaign_source_matches(owner,&out->source)) return false;
    const qa_cvar_view *skill=qa_cvars_find(owner->cvars,"g_spSkill");
    double raw=skill?skill->number:2;
    out->skill=raw>=5?5:raw>=4?4:raw>=3?3:raw>=2?2:1;
    out->installed=true; out->team=owner->kind==CAMPAIGN_TEAM; out->result=owner->result; out->action_pending=owner->ui_pending;
    if (out->team) {
        out->team_score_recorded=owner->result; out->team_score=owner->team_result;
        if (!qa_application_q3_campaign_result_read(f->application,&out->source,launch_seat,
            &out->team_live,&out->result,error)) return false;
    }
    else {
        out->catalog=owner->catalog; out->progression=&owner->progression;
        out->selection=-1;
        if (!qa_arena_progress_current(&owner->progression,&out->selection,error)) return false;
        const qa_cvar_view *selected=qa_cvars_find(owner->cvars,"ui_spSelection");
        if (selected && *selected->value) {
            char *end=NULL; double value=strtod(selected->value,&end);
            while (end && (*end==' ' || (*end>='\t' && *end<='\r'))) ++end;
            if (end && !*end && isfinite(value)) {
                for (size_t i=0;i<qa_base_arena_catalog_count(owner->catalog);++i) {
                    const qa_base_arena *arena=qa_base_arena_catalog_at(owner->catalog,i);
                    bool available=false;
                    if (arena->selection!=value) continue;
                    if (!qa_arena_progress_available(&owner->progression,arena->number,&available,error)) return false;
                    if (available) out->selection=arena->number;
                    break;
                }
            }
        }
        out->game=owner->base_game; out->postgame=owner->base_result;
        out->player_count=owner->player_count; out->player_client=owner->player_client;
        for (size_t i=0;i<owner->player_count;++i)
            out->players[i]=(frontend_campaign_podium_player){owner->players[i].client,owner->players[i].rank,owner->players[i].score};
    }
    return qa_application_q3_campaign_current(f->application,&out->source) &&
        qa_application_q3_client_context_current(f->application,&out->client);
}
bool frontend_campaign_ui_stage(qa_frontend *f,uint32_t seat,frontend_campaign_ui_action action,
    int32_t arena,int32_t skill,qa_error *error)
{
    frontend_campaign_ui_view view;
    if (action>FRONTEND_CAMPAIGN_QUIT || action<FRONTEND_CAMPAIGN_PLAY ||
        !frontend_campaign_ui_read(f,seat,&view,error) || !view.installed || !frontend_campaign_ready(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign action requires its idle installed local recipient");
    if (view.team && action!=FRONTEND_CAMPAIGN_QUIT)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Base Arena action requires its actual Base Arena campaign");
    if (action==FRONTEND_CAMPAIGN_PLAY || action==FRONTEND_CAMPAIGN_RETRY || action==FRONTEND_CAMPAIGN_NEXT) {
        qa_q3_product_policy policy;
        if (!qa_application_q3_product_policy_read(f->application,&policy) || policy.prerelease_demo)
            return frontend_fail(error,QA_ERROR_UNSUPPORTED,"The retained Q3 product policy does not admit single-player map commands");
    }
    if (action==FRONTEND_CAMPAIGN_PLAY) {
        bool found=false,available=false;
        for (size_t i=0;i<qa_base_arena_catalog_count(view.catalog);++i)
            if (qa_base_arena_catalog_at(view.catalog,i)->number==arena) { found=true; break; }
        if (!found || skill<1 || skill>5 || !qa_arena_progress_available(view.progression,arena,&available,error) || !available)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"That authored arena is not unlocked in the source profile");
    }
    frontend_campaign *owner=f->campaign;
    owner->ui_action=action; owner->ui_arena=arena; owner->ui_skill=skill;
    owner->ui_seat=seat; owner->ui_receiver=view.client.receiver; owner->ui_actor=view.client.source_actor;
    owner->ui_pending=true; return true;
}
bool frontend_campaign_ui_drain(qa_frontend *f,qa_error *error)
{
    if (!f || f->stepping || f->capture || f->preparing || !frontend_owners_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign action must follow the completed frontend callback boundary");
    if (qa_application_startup_pending(f->application)) return true;
    frontend_campaign *owner=f->campaign;
    if (!owner || !owner->ui_pending) return true;
    if (owner->draining || owner->requests || owner->restore_pending)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign action still retains an earlier source command");
    qa_application_q3_campaign source; qa_application_q3_client_context client;
    uint32_t launch_seat;
    if (!frontend_seat_launch_id_read(f,owner->ui_seat,&launch_seat) ||
        !qa_application_q3_campaign_menu_read(f->application,owner->ui_receiver,launch_seat,&source,&client,error) ||
        !campaign_source_matches(owner,&source) || !qa_actor_id_equal(client.source_actor,owner->ui_actor))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Queued campaign action lost its source or local recipient");
    frontend_campaign_ui_action action=owner->ui_action;
    int32_t number=owner->ui_arena,skill=action==FRONTEND_CAMPAIGN_PLAY?owner->ui_skill:0;
    owner->ui_pending=false;
    if (action==FRONTEND_CAMPAIGN_QUIT) { qa_application_request_stop(f->application); return true; }
    if (action==FRONTEND_CAMPAIGN_RESET) return campaign_base_action(f,&source,"arena-reset",error);
    const qa_base_arena *arena=NULL;
    if (action==FRONTEND_CAMPAIGN_RETRY) arena=qa_base_arena_catalog_find(owner->catalog,source.map);
    else {
        if (action==FRONTEND_CAMPAIGN_NEXT && !qa_arena_progress_current(&owner->progression,&number,error)) return false;
        for (size_t i=0;i<qa_base_arena_catalog_count(owner->catalog);++i) {
            const qa_base_arena *row=qa_base_arena_catalog_at(owner->catalog,i);
            if (row->number==number) { arena=row; break; }
        }
    }
    if (!arena) return frontend_fail(error,QA_ERROR_NOT_FOUND,"Campaign action has no authored arena map");
    qa_launch_draft *draft=NULL;
    if (!qa_launch_snapshot_draft_copy(source.publication,&draft,error)) return false;
    qa_launch_world world=qa_launch_draft_choices(draft)->world;
    world.map=arena->map; world.start_command=NULL; world.spawn_point=NULL; world.explicit_spawn_point=false;
    if (action==FRONTEND_CAMPAIGN_PLAY) world.skill=skill;
    char text[16]; (void)snprintf(text,sizeof(text),"%" PRId32,skill);
    qa_application_q3_setting rows[]={ {"g_gametype","2"},{"sv_cheats","0"},{"g_doWarmup","0"},
        {"sv_maxclients","8"},{"g_spSkill",text} };
    bool ok=qa_launch_set_world(draft,&world,error) &&
        qa_application_q3_campaign_launch(f->application,&source,draft,rows,action==FRONTEND_CAMPAIGN_PLAY?5:4,error);
    qa_launch_draft_destroy(draft);
    return ok && (qa_application_startup_pending(f->application) || frontend_campaign_sync(f,error));
}
