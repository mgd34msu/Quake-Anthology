#include "qa/platform_services.h"
#include "campaign.h"
#include "capture.h"
#include "campaign_ui.h"
#include "campaign_cinematic.h"
#include "ui_features.h"
#include "content_library_services.h"
#include "startup_menus.h"
#include "network_recipient.h"
#include "../application/control_frame.h"
#include "qa/settings.h"
#include "qa/player_progress.h"
#include "qa/q3_product_policy.h"
#include "qa/audio_save.h"
#include "qa/application_startup_prepare.h"
#include "qa/application_network_q2.h"
#include "qa/game_q2_bots.h"
#include "qa/text.h"
#include <SDL.h>
#include <inttypes.h>
#include <stdio.h>

typedef enum campaign_kind { CAMPAIGN_BASE, CAMPAIGN_TEAM, CAMPAIGN_Q2, CAMPAIGN_NONE } campaign_kind;
typedef struct campaign_player { int32_t client,rank,score; } campaign_player;
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
    qa_cvar_handle nextmap, skill, selection, deathmatch, protocol;
    qa_cvar_handle archive[8], round_identity, time_to_beat, saved_rules[6], result_fields[3];
    uint64_t selection_modification, progression_revision, result_declarations;
    int32_t menu_selection;
    bool selection_valid;
    uint64_t cvar_revision, configuration_generation;
    qa_base_arena_catalog *catalog;
    qa_arena_progress progression;
    qa_team_arena_progress *team;
    qa_arena_result base_game;
    qa_arena_postgame base_result;
    uint64_t result_at_ns;
    campaign_player players[8];
    size_t player_count;
    int32_t player_client;
    qa_team_arena_score_result team_result;
    qa_string_id demo_path;
    int32_t demo_game_type, demo_protocol;
    bool demo_checked;
    bool result,announced,movie_played,music_attached,draining,match_prepared;

};
static const char *const base_archive_names[8]={"g_spScores1","g_spScores2","g_spScores3","g_spScores4",
    "g_spScores5","g_spAwards","g_spVideos","g_spCompletedRound"};
static const char *const team_overrides[6][2]={{"capturelimit","ui_saveCaptureLimit"},{"fraglimit","ui_saveFragLimit"},
    {"g_doWarmup","ui_doWarmup"},{"g_warmup","ui_Warmup"},{"sv_pure","ui_pure"},{"g_friendlyFire","ui_friendlyFire"}};
static void campaign_result_bind(frontend_campaign *owner)
{
    static const char *const names[]={"ui_scoreMap","ui_scoreScore","ui_scoreTime"};
    uint64_t revision=qa_cvars_declaration_revision(owner->cvars);
    if(owner->result_declarations==revision) return;
    for(size_t i=0;i<3;++i) owner->result_fields[i]=qa_cvars_resolve(owner->cvars,names[i]);
    owner->result_declarations=revision;
}
void frontend_campaign_audio_stopped(qa_frontend *f)
{ if (f && f->campaign) f->campaign->music_attached=false; }
static char *campaign_copy(const char *text,qa_error *error)
{
    size_t size=strlen(text); char *copy=size<SIZE_MAX?malloc(size+1):NULL;
    if (!copy) { frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual source campaign text"); return NULL; }
    memcpy(copy,text,size+1); return copy;
}
static const char *campaign_value(const qa_cvars *cvars,qa_cvar_handle handle)
{ const qa_cvar_view *value=qa_cvars_read(cvars,handle); return value?value->value:""; }
static bool campaign_match_record(qa_player_progress *profile,qa_game_family source,uint32_t seat,
    const char *round,const char *map,double score,qa_error *error)
{
    char participant[64]; (void)snprintf(participant,sizeof(participant),"local-seat:%" PRIu32,seat);
    qa_progress_event event={.kind=QA_PROGRESS_MATCH_COMPLETED,.source=source,
        .participant={(const uint8_t *)participant,strlen(participant)},
        .event={(const uint8_t *)round,strlen(round)},
        .value.match={.map={(const uint8_t *)map,strlen(map)},.score=score}};
    bool inserted=false;
    return qa_player_progress_record(profile,&event,&inserted,error);
}
static int32_t campaign_word(uint32_t value)
{ int32_t result; memcpy(&result,&value,sizeof(result)); return result; }
static int32_t campaign_number(const qa_command_invocation *request,size_t index)
{
    const unsigned char *text=(const unsigned char *)(index+1<request->argc?request->argv[index+1]:"");
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
    uint8_t bytes[16];
    if (!qa_platform_entropy(bytes,sizeof(bytes),error)) return false;
    bytes[6]=(uint8_t)((bytes[6]&15u)|64u); bytes[8]=(uint8_t)((bytes[8]&63u)|128u);
    static const char hex[]="0123456789abcdef"; size_t at=0;
    for (size_t i=0;i<16;++i) {
        if (i==4 || i==6 || i==8 || i==10) out[at++]='-';
        out[at++]=hex[bytes[i]>>4]; out[at++]=hex[bytes[i]&15u];
    }
    out[at]=0; return true;
}
bool frontend_campaign_ready(const qa_frontend *f)
{ return f && (!f->campaign || !f->campaign->draining); }
bool frontend_campaign_destroy(qa_frontend *f,qa_error *error)
{
    if (!f || !f->campaign) return true;
    frontend_campaign *owner=f->campaign;
    if (owner->draining) return frontend_fail(error,QA_ERROR_ARGUMENT,"Source campaign callback still owns its result context");
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
static campaign_kind campaign_kind_of(const qa_application_q3_campaign *view, qa_cvar_handle nextmap)
{
    if (view->product == QA_Q3_ARENA && view->game_type == 2) return CAMPAIGN_BASE;
    const qa_cvar_view *next = qa_cvars_read(view->cvars, nextmap);
    if (view->product == QA_Q3_TEAM_ARENA && next && !strcmp(next->value, "teamarena-results")) return CAMPAIGN_TEAM;
    return CAMPAIGN_NONE;
}
static bool campaign_source_matches(const frontend_campaign *, const qa_application_q3_campaign *);
static bool campaign_q2_bind(qa_frontend *f,const qa_application_native_q2_presentation *view,qa_error *error)
{
    frontend_campaign *owner=f->campaign;
    if(owner && owner->kind==CAMPAIGN_Q2 && owner->source==view->source_owner &&
        owner->map_revision==view->map_revision) {
        if(owner->publication!=view->publication) {
            qa_launch_snapshot_retain(view->publication);
            qa_launch_snapshot_release(owner->publication);owner->publication=view->publication;
        }
        return true;
    }
    if(!frontend_campaign_destroy(f,error)) return false;
    qa_application_network_q2_host host;
    if(!qa_application_network_q2_host_source(f->application,
        qa_q2_default_protocol(view->edition==QA_RULESET_Q2_RERELEASE),&host,error)) return false;
    owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining Q2 match progress");
    owner->frontend=f;owner->source=view->source_owner;owner->content=view->content;
    owner->cvars=host.cvars;owner->publication=view->publication;owner->map_revision=view->map_revision;
    owner->kind=CAMPAIGN_Q2;
    qa_launch_snapshot_retain(owner->publication);f->campaign=owner;
    owner->deathmatch=qa_cvars_resolve(owner->cvars,"deathmatch");
    owner->round_identity=qa_cvars_resolve(owner->cvars,"qts_matchRoundIdentity");
    if(qa_cvars_declare_save_policy(owner->cvars,"qts_matchRoundIdentity",QA_CVAR_SAVE_GAMEPLAY,error)) return true;
    (void)frontend_campaign_destroy(f,NULL);return false;
}
static bool campaign_q2_deathmatch(const frontend_campaign *owner,qa_ruleset_id edition)
{
    const qa_cvar_view *value=qa_cvars_read(owner->cvars,owner->deathmatch);
    return value && (edition==QA_RULESET_Q2_RERELEASE?value->integer!=0:value->number!=0);
}
static bool campaign_q2_prepare(qa_frontend *f,const qa_application_native_q2_presentation *view,
    const char *map,qa_error *error)
{
    frontend_campaign *owner=f->campaign;
    if(owner->match_prepared) return true;
    const char *previous=campaign_value(owner->cvars,owner->round_identity);
    const char *tail=!strncmp(previous,"q2:",3)?strchr(previous+3,':'):NULL;
    char generation[32];
    int generation_bytes=snprintf(generation,sizeof(generation),":%" PRIu64 ":",view->map_revision);
    bool retained=tail && !strncmp(tail,generation,(size_t)generation_bytes) &&
        !strcmp(tail+generation_bytes,map);
    if(!retained) {
        qa_player_progress *profile=qa_application_player_progress(f->application);
        char prefix[64];
        int prefix_bytes=snprintf(prefix,sizeof(prefix),"q2:%zu:%" PRIu64 ":",
            qa_player_progress_count(profile),view->map_revision);
        size_t map_bytes=strlen(map);
        char *identity=qa_arena_alloc(&f->frame.storage,(size_t)prefix_bytes+map_bytes+1,_Alignof(char),error);
        if(!identity) return false;
        memcpy(identity,prefix,(size_t)prefix_bytes);memcpy(identity+prefix_bytes,map,map_bytes+1);
        if(!qa_cvars_set(owner->cvars,"qts_matchRoundIdentity",identity,true,error)) return false;
    }
    owner->match_prepared=true;return true;
}
static bool campaign_q2_player(qa_frontend *f,const qa_application_native_q2_presentation *view,
    uint32_t ordinal,uint32_t *seat,int32_t *score,bool *playing,bool *intermission,qa_error *error)
{
    *playing=false;*intermission=false;
    qa_actor_id actor;
    if(!frontend_seat_launch_id_read(f,ordinal,seat) || !frontend_seat_actor_read(f,ordinal,&actor)) return true;
    if(view->kind==QA_APPLICATION_NATIVE_Q2_BUILTIN) {
        qa_q2_player_info player;
        if(!qa_q2_player_read((qa_q2_game *)view->source.game,actor,&player) || !player.connected || player.spectator)
            return true;
        *score=player.score;
    } else {
        qa_native_host *host=(qa_native_host *)view->source.original.host;
        uint32_t slot;
        if(!qa_native_host_slot_for_actor(host,actor,&slot)) return true;
        qa_q2_player player;
        if(!qa_native_host_q2_wire_player(host,slot,actor,&player,error)) return false;
        if(player.stats[17]!=0) return true;
        *score=player.stats[14];
        *intermission=view->edition==QA_RULESET_Q2_RERELEASE && (player.stats[13]&8)!=0;
    }
    *playing=true;return true;
}
bool frontend_campaign_match_complete(qa_frontend *f,qa_actor_owner source,qa_error *error)
{
    if(!f || f->options.dedicated || frontend_network_remote(f) || f->capture) return true;
    qa_application_native_q2_presentation view;bool found;
    if(!qa_application_native_q2_presentation_selected(f->application,&view,&found,error)) return false;
    if(!found || view.source_owner!=source) return true;
    if(!campaign_q2_bind(f,&view,error)) return false;
    frontend_campaign *owner=f->campaign;
    if(owner->result || !campaign_q2_deathmatch(owner,view.edition)) return true;
    qa_application_map_view map;
    if(!qa_application_map_read(f->application,&map)) return true;
    if(!campaign_q2_prepare(f,&view,map.name,error)) return false;
    qa_player_progress *profile=qa_application_player_progress(f->application);
    if(!profile) {owner->result=true;return true;}
    const char *identity=campaign_value(owner->cvars,owner->round_identity);
    size_t length=strlen(identity);
    char *round=qa_arena_alloc(&f->frame.storage,length+sizeof("match:"),_Alignof(char),error);
    if(!round) return false;
    memcpy(round,"match:",sizeof("match:")-1);memcpy(round+sizeof("match:")-1,identity,length+1);
    for(uint32_t ordinal=0;ordinal<f->options.seats;++ordinal) {
        uint32_t seat;int32_t score;bool playing,intermission;
        if(!campaign_q2_player(f,&view,ordinal,&seat,&score,&playing,&intermission,error)) return false;
        if(playing && !campaign_match_record(profile,QA_GAME_Q2,seat,round,map.name,score,error)) return false;
    }
    owner->result=true;return true;
}
static bool campaign_q2_sync(qa_frontend *f,qa_error *error)
{
    qa_application_map_view map;
    if(f->options.dedicated || frontend_network_remote(f) || !qa_application_map_read(f->application,&map))
        return frontend_campaign_destroy(f,error);
    qa_application_native_q2_presentation view;bool found;
    if(!qa_application_native_q2_presentation_selected(f->application,&view,&found,error)) return false;
    if(!found) return frontend_campaign_destroy(f,error);
    if(!campaign_q2_bind(f,&view,error)) return false;
    if(!campaign_q2_deathmatch(f->campaign,view.edition) || f->campaign->result) return true;
    if(!campaign_q2_prepare(f,&view,map.name,error)) return false;
    bool completed=false;
    if(view.kind==QA_APPLICATION_NATIVE_Q2_BUILTIN) {
        uint64_t time,started;
        if(!qa_q2_bot_clock_read(view.source.game,&time,&completed,&started,error)) return false;
    } else if(view.edition==QA_RULESET_Q2_RERELEASE) {
        for(uint32_t ordinal=0;!completed && ordinal<f->options.seats;++ordinal) {
            uint32_t seat;int32_t score;bool playing;
            if(!campaign_q2_player(f,&view,ordinal,&seat,&score,&playing,&completed,error)) return false;
        }
    }
    return !completed || frontend_campaign_match_complete(f,view.source_owner,error);
}
bool frontend_campaign_sync(qa_frontend *f,qa_error *error)
{
    if (!f || !f->application || f->capture) return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign setup requires its actual frontend source owner");
    if (qa_application_startup_pending(f->application)) return true;
    if (!campaign_has_q3(f)) {
        qa_error progress={0};
        if (!campaign_q2_sync(f,&progress))
            qa_application_feature_report(f->application,"match progress",&progress);
        return true;
    }
    frontend_campaign *owner = f->campaign;
    qa_application_map_view map;
    if (owner && owner->publication == qa_application_launch(f->application) &&
        owner->configuration_generation == qa_application_configuration_generation(f->application) &&
        qa_application_map_read(f->application, &map) && owner->map_revision == map.revision &&
        owner->cvar_revision == qa_cvars_revision(owner->cvars)) return true;
    qa_application_q3_campaign view;
    if (!qa_application_q3_campaign_read(f->application,0,&view,error)) return false;
    qa_cvar_handle nextmap = view.nextmap;
    campaign_kind kind = campaign_kind_of(&view, nextmap);
    if (kind == CAMPAIGN_NONE) return frontend_campaign_destroy(f,error);
    bool base = kind == CAMPAIGN_BASE;
    if (owner && campaign_source_matches(owner, &view)) {
        owner->cvar_revision = qa_cvars_revision(owner->cvars);
        return true;
    }
    if (!frontend_campaign_ready(f) || !frontend_campaign_destroy(f,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign source changed before its accepted command drained");
    owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating actual source campaign continuation");
    owner->frontend=f; owner->source=view.source_owner; owner->content=view.content; owner->cvars=view.cvars;
    owner->config_root=view.config_root; owner->map_revision=view.map_revision; owner->match_start=view.match_start_time;
    owner->kind=kind; owner->publication=view.publication; owner->nextmap=nextmap;
    owner->configuration_generation=qa_application_configuration_generation(f->application);
    qa_launch_snapshot_retain(owner->publication);
    bool ok=true;
    if (base) {
        ok=qa_base_arena_catalog_create(view.content,&owner->catalog,error) &&
            qa_arena_progress_init(&owner->progression,view.cvars,qa_base_arena_catalog_levels(owner->catalog),view.source_owner,error) &&
            qa_cvars_register(view.cvars,"g_spRoundIdentity","",QA_CVAR_ARCHIVE,view.source_owner,NULL,error) &&
            qa_cvars_register(view.cvars,"g_spCompletedRound","",QA_CVAR_ARCHIVE,view.source_owner,NULL,error);
        owner->round_identity=qa_cvars_resolve(view.cvars,"g_spRoundIdentity");
        for(size_t i=0;i<8;++i) owner->archive[i]=qa_cvars_resolve(view.cvars,base_archive_names[i]);
        const qa_base_arena *arena=ok?qa_base_arena_catalog_find(owner->catalog,view.map):NULL;
        if (ok && arena && !f->source_restoring) {
            char selection[24]; (void)snprintf(selection,sizeof(selection),"%" PRId32,arena->selection);
            ok=qa_cvars_register(view.cvars,"ui_spSelection","0",QA_CVAR_ARCHIVE,view.source_owner,NULL,error) &&
                qa_cvars_set(view.cvars,"ui_spSelection",selection,true,error);
        }
        if (ok && (!f->source_restoring || !*campaign_value(view.cvars,owner->round_identity))) {
            char identity[37]; ok=campaign_uuid(identity,error) && qa_cvars_set(view.cvars,"g_spRoundIdentity",identity,true,error);
        }
    } else ok=view.config_root && qa_team_arena_progress_create(view.config_root,&owner->team,error);
    owner->skill=qa_cvars_resolve(view.cvars,"g_spSkill");
    owner->selection=qa_cvars_resolve(view.cvars,"ui_spSelection");
    owner->time_to_beat=qa_cvars_resolve(view.cvars,"ui_teamArenaTimeToBeat");
    owner->protocol=qa_cvars_resolve(view.cvars,"protocol");
    campaign_result_bind(owner);
    for(size_t i=0;i<6;++i) owner->saved_rules[i]=qa_cvars_resolve(view.cvars,team_overrides[i][1]);
    owner->cvar_revision=qa_cvars_revision(view.cvars);
    if (ok) ok=qa_application_q3_campaign_current(f->application,&view);
    f->campaign=owner;
    if (!ok) {
        (void)frontend_campaign_destroy(f,NULL);
        if (!error || error->code==QA_OK) frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign construction lost its actual GAME registry/content/config root");
    }
    return ok;
}
static bool campaign_base_complete(qa_frontend *, const qa_application_q3_campaign *, const qa_command_invocation *, qa_error *);
static bool campaign_team_complete(qa_frontend *, const qa_application_q3_campaign *, const qa_command_invocation *, qa_error *);
static bool campaign_base_action(qa_frontend *, const qa_application_q3_campaign *, const char *, qa_error *);
static bool campaign_launch_action(qa_frontend *, const qa_application_q3_campaign *, const qa_command_invocation *, qa_error *);
bool frontend_campaign_source_command(void *context,qa_application *application,const qa_command_invocation *invocation,
    bool *handled,qa_error *error)
{
    if (handled) *handled=false;
    if (!handled || !invocation || !invocation->argc || !invocation->argv) return false;
    const char *name=invocation->argv[0];
    static const struct { const char *name, *action, *notice; } aliases[]={
        {"iamacheater","arena-unlock","All levels unlocked at skill level 1\n"},
        {"iamamonkey","arena-medals","All levels unlocked at 100\n"}};
    const char *notice=NULL;
    for (size_t i=0;i<sizeof(aliases)/sizeof(aliases[0]);++i)
        if (qa_text_ascii_equal(name,aliases[i].name)) {
            name=aliases[i].action; notice=aliases[i].notice; break;
        }
    if (!strcmp(name,"cinematic") || !strcmp(name,"cinematicpause") || !strcmp(name,"stopcinematic")) {
        qa_frontend *f=context;
        if (!f || application!=f->application || f->source_restoring || f->capture) return false;
        *handled=true;
        return frontend_cinematic_command(f,invocation,error);
    }
    if (strcmp(name,"postgame") && strcmp(name,"spPostgame") && strcmp(name,"arena-reset") &&
        strcmp(name,"arena-unlock") && strcmp(name,"arena-medals") && strcmp(name,"teamarena-results") &&
        strcmp(name,"arena-play") && strcmp(name,"arena-retry") && strcmp(name,"arena-next")) return true;
    qa_frontend *f=context;
    if (!f || application!=f->application || f->source_restoring || f->capture || !frontend_campaign_sync(f,error)) return false;
    if (!f->campaign || f->campaign->kind==CAMPAIGN_Q2) return true;
    qa_application_q3_campaign view;
    bool postgame=!strcmp(name,"postgame") || !strcmp(name,"spPostgame");
    if (!qa_application_q3_campaign_read(application,f->campaign->source,&view,error)) return false;
    if (!postgame && invocation->context.owner && invocation->context.owner!=view.source_owner) {
        uint32_t physical;
        if (!frontend_command_seat_read(f,&invocation->context,&physical)) return true;
        frontend_campaign_ui_view recipient;
        if (!frontend_campaign_ui_read(f,physical,&recipient,error)) return false;
        if (!recipient.installed || recipient.source.source_owner!=view.source_owner ||
            invocation->console!=recipient.client.context.console ||
            invocation->context.owner!=recipient.client.context.receiver) return true;
    }
    if ((postgame && invocation->console!=view.console) ||
        (postgame && !qa_application_q3_campaign_postgame_context(application,&view,&invocation->context,error)) ||
        (!postgame && !qa_application_command_context_active(application,&invocation->context))) return false;
    /* The common console owns and orders the invocation; campaign has no private queue. */
    *handled = true;
    if (!strcmp(name,"arena-play") || !strcmp(name,"arena-retry") || !strcmp(name,"arena-next"))
        return campaign_launch_action(f, &view, invocation, error);
    f->campaign->draining = true;
    bool ok;
    if (postgame) ok = f->campaign->kind == CAMPAIGN_BASE ?
        campaign_base_complete(f, &view, invocation, error) : campaign_team_complete(f, &view, invocation, error);
    else if (!strcmp(name, "teamarena-results")) ok = f->campaign->kind == CAMPAIGN_TEAM ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "No actual Team Arena skirmish is active");
    else ok = campaign_base_action(f, &view, name, error);
    f->campaign->draining = false;
    if (ok && notice) qa_console_emit(invocation->console,&invocation->context,notice);
    return ok;
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
        if (!qa_audio_music_create(qa_audio_engine_rate(f->audio),QA_GAME_Q3,true,&music,error)) return false;
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
        qa_audio_bank_create(view->content, qa_session_strings(qa_application_session(f->application)),&bank,error) &&
        qa_audio_bank_music_cue(bank,cue,QA_GAME_Q3,NULL,NULL,&intro,error) &&
        qa_application_q3_campaign_current(f->application,view);
    qa_audio_bank_destroy(bank);
    if (!ok) { qa_audio_stream_close(intro); return false; }
    if (intro) qa_audio_music_start(music,intro,NULL);
    return true;
}
static bool campaign_base_complete(qa_frontend *f,const qa_application_q3_campaign *view,
    const qa_command_invocation *request,qa_error *error)
{
    frontend_campaign *owner=f->campaign;
    const qa_base_arena *arena=qa_base_arena_catalog_find(owner->catalog,view->map);
    if (!arena) return frontend_fail(error,QA_ERROR_NOT_FOUND,"Postgame map has no actual authored single-player arena");
    const qa_cvar_view *skill_value=qa_cvars_read(view->cvars,owner->skill); float raw_skill=skill_value?skill_value->number:0;
    int32_t skill=raw_skill>=5?5:raw_skill>=4?4:raw_skill>=3?3:raw_skill>=2?2:1;
    qa_arena_result game={.level=arena->number,.skill=skill,.rank=8,
        .accuracy=campaign_number(request,2),.impressive=campaign_number(request,3),.excellent=campaign_number(request,4),
        .gauntlet=campaign_number(request,5),.frags=campaign_number(request,6),.perfect=campaign_number(request,7)!=0};
    campaign_player players[8]={0}; int32_t count=campaign_number(request,0),player=campaign_number(request,1);
    size_t player_count=count<=0?0:count>=8?8:(size_t)count;
    for (size_t i=0;i<player_count;++i) {
        int32_t rank=campaign_word(((uint32_t)campaign_number(request,9+i*3)&~UINT32_C(0x4000))+1u);
        players[i]=(campaign_player){campaign_number(request,8+i*3),rank,campaign_number(request,10+i*3)};
        if (players[i].client==player) game.rank=rank;
    }
    const char *identity=campaign_value(view->cvars,owner->round_identity); size_t length=strlen(identity);
    if (length>SIZE_MAX-32) return frontend_fail(error,QA_ERROR_MEMORY,"Source round identity exceeds addressable text");
    char *round=malloc(length+32);
    if (!round) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual source completed-round identity");
    (void)snprintf(round,length+32,"%s:%" PRId32,identity,view->match_start_time);
    qa_arena_postgame result={.rank=game.rank,.completed_tier=-1,.unlocked_movie=-1,.next_level=-1};
    bool duplicate=!strcmp(campaign_value(view->cvars,owner->archive[7]),round),ok=true;
    if (duplicate && owner->result) { free(round); return true; }
    if (duplicate) ok=qa_arena_progress_tier(&owner->progression,arena->number,&result.completed_tier,error) &&
        qa_arena_progress_current(&owner->progression,&result.next_level,error);
    bool local=false; uint32_t seat=0; qa_actor_id actor={0};
    if (ok && !duplicate) ok=qa_application_q3_campaign_local_seat(f->application,view,(uint32_t)player,&local,&seat,&actor,error);
    if (ok && !duplicate && local) {
        if (!view->profile) ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Local arena completion requires its actual application profile owner");
        else {
            ok=campaign_match_record(view->profile,QA_GAME_Q3,seat,round,arena->map,game.frags,error) &&
                qa_application_q3_campaign_current(f->application,view);
        }
    }
    char *before[8]={0}; bool mutated=false;
    if (ok && !duplicate) {
        for (size_t i=0;ok && i<8;++i) ok=(before[i]=campaign_copy(campaign_value(view->cvars,owner->archive[i]),error))!=NULL;
        if (ok) {
            mutated=true;
            ok=qa_arena_progress_record(&owner->progression,&game,&result,error) &&
                qa_application_q3_campaign_current(f->application,view) &&
                qa_cvars_set(view->cvars,"g_spCompletedRound",round,true,error) && campaign_archive(f,view,error);
        }
        if (!ok && mutated) campaign_restore_values(f,view,before);
    }
    for (size_t i=0;i<8;++i) free(before[i]);
    free(round);
    if (!ok) return false;
    owner->base_game=game; owner->base_result=result; memcpy(owner->players,players,sizeof(players));
    owner->player_count=player_count; owner->player_client=player; owner->result=true;
    owner->result_at_ns=f->wall_time_ns;
    owner->announced=duplicate; owner->movie_played=duplicate;
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
    }
    return ok && campaign_archive(f,view,error);
}
static bool campaign_team_complete(qa_frontend *f,const qa_application_q3_campaign *view,
    const qa_command_invocation *request,qa_error *error)
{
    frontend_campaign *owner=f->campaign;
    const qa_cvar_view *skill=qa_cvars_read(view->cvars,owner->skill),*beat=qa_cvars_read(view->cvars,owner->time_to_beat);
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
    for (size_t i=0;i<6;++i) {
        char *saved=campaign_copy(campaign_value(view->cvars,owner->saved_rules[i]),error);
        if (!saved) return false;
        bool ok=qa_cvars_set(view->cvars,team_overrides[i][0],saved,true,error) && qa_application_q3_campaign_current(f->application,view);
        free(saved); if (!ok) return false;
    }
    owner->team_result=result; owner->result=true;
    owner->demo_checked=false; owner->demo_path=QA_STRING_NONE;
    if (result.new_high_score) {
        char message[96]; (void)snprintf(message,sizeof(message),"New Team Arena high score: %" PRId32 "\n",result.current.score);
        frontend_print(f,message);
    }
    return true;
}
static bool campaign_source_matches(const frontend_campaign *owner,const qa_application_q3_campaign *view)
{
    return owner->source==view->source_owner && owner->content==view->content && owner->cvars==view->cvars &&
        owner->config_root==view->config_root && owner->publication==view->publication &&
        owner->map_revision==view->map_revision && owner->match_start==view->match_start_time &&
        owner->kind == campaign_kind_of(view, owner->nextmap);
}
static bool campaign_team_demo(frontend_campaign *owner,const qa_application_q3_campaign *source,
    const qa_command_context *command,qa_error *error)
{
    const qa_cvar_view *value=qa_cvars_read(owner->cvars,owner->protocol);
    int32_t protocol=value?value->integer:68;
    if (owner->demo_checked && owner->demo_game_type==source->game_type && owner->demo_protocol==protocol) return true;
    owner->demo_checked=true; owner->demo_game_type=source->game_type; owner->demo_protocol=protocol;
    owner->demo_path=QA_STRING_NONE;
    const char *map=source->map; if (!strncmp(map,"maps/",5)) map+=5;
    size_t length=strlen(map);
    if (length>=4 && !strcmp(map+length-4,".bsp")) length-=4;
    if (!length || source->game_type<0 || protocol<0)
        return frontend_fail(error,QA_ERROR_FORMAT,"Invalid Team Arena postgame demo selection");
    for (size_t i=0;i<length;++i) {
        unsigned char c=(unsigned char)map[i];
        bool separator=c=='/';
        if (!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-' || separator) ||
            (separator && (!i || i+1==length || map[i-1]=='/')))
            return frontend_fail(error,QA_ERROR_FORMAT,"Invalid Team Arena postgame demo map");
    }
    char ending[40];
    int ending_length=snprintf(ending,sizeof(ending),"_%" PRId32 ".dm_%" PRId32,source->game_type,protocol);
    if (ending_length<0 || (size_t)ending_length>=sizeof(ending) || length>SIZE_MAX-(size_t)ending_length-7)
        return frontend_fail(error,QA_ERROR_MEMORY,"Team Arena demo path exceeds memory extent");
    qa_frontend *f=owner->frontend;
    qa_unified_frame_lease *storage=application_control_storage_acquire(f->application,error);
    if (!storage) return false;
    char *path=qa_arena_alloc(qa_unified_frame_lease_arena(storage),length+(size_t)ending_length+7,1,error);
    bool found=false,okay=path!=NULL;
    if (okay) {
        memcpy(path,"demos/",6); memcpy(path+6,map,length);
        memcpy(path+6+length,ending,(size_t)ending_length+1);
        okay=frontend_content_library_demo_probe(f,command,path,&found,error);
    }
    qa_string_id selected=QA_STRING_NONE;
    if (okay && found) okay=qa_strings_intern_cstr(qa_session_strings(source->session),path,&selected,error);
    if (okay) okay=qa_application_q3_campaign_current(f->application,source);
    if (okay) owner->demo_path=selected;
    qa_unified_frame_lease_release(storage);
    return okay;
}
bool frontend_campaign_ui_read(qa_frontend *f,uint32_t seat,frontend_campaign_ui_view *out,qa_error *error)
{
    if (!f || !out || seat>=f->options.seats || f->capture || f->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign menu requires its installed physical seat");
    *out=(frontend_campaign_ui_view){0};
    if (qa_application_startup_pending(f->application)) return true;
    frontend_campaign *owner=f->campaign;
    if (!owner || owner->kind==CAMPAIGN_Q2 || owner->draining || frontend_network_remote(f)) return true;
    frontend_network_client_recipient recipient;
    bool present=false;
    if (!frontend_network_client_recipient_read(f,seat,&recipient,&present,error)) return false;
    if (!present || !recipient.ready) return true;
    uint32_t launch_seat;
    if (!frontend_seat_launch_id_read(f,seat,&launch_seat)) return true;
    out->client=recipient.source;
    if (!qa_application_q3_campaign_menu_read(f->application,&out->client,&out->source,&out->source_client,error) ||
        !campaign_source_matches(owner,&out->source)) return false;
    const qa_cvar_view *skill=qa_cvars_read(owner->cvars,owner->skill);
    double raw=skill?skill->number:2;
    out->skill=raw>=5?5:raw>=4?4:raw>=3?3:raw>=2?2:1;
    out->installed=true; out->team=owner->kind==CAMPAIGN_TEAM; out->result=owner->result;
    out->action_pending=qa_console_pending(out->source.console);
    if (out->team) {
        out->team_score_recorded=owner->result; out->team_score=owner->team_result;
        campaign_result_bind(owner);
        for(size_t i=0;i<3;++i) out->result_fields[i]=qa_cvars_read(owner->cvars,owner->result_fields[i]);
        if (!qa_application_q3_campaign_result_read(f->application,&out->source,launch_seat,
            &out->team_live,&out->result,error)) return false;
        if (owner->result && out->result && owner->demo_path!=QA_STRING_NONE)
            out->demo_path=qa_strings_cstr(qa_session_strings(out->source.session),owner->demo_path);
    }
    else {
        out->catalog=owner->catalog; out->progression=&owner->progression;
        int32_t current;
        if (!qa_arena_progress_current(&owner->progression,&current,error)) return false;
        const qa_cvar_view *selected=qa_cvars_read(owner->cvars,owner->selection);
        uint64_t modification=selected?selected->modification_count:0;
        if(!owner->selection_valid || owner->selection_modification!=modification ||
            owner->progression_revision!=owner->progression.current_revision) {
            owner->menu_selection=current;
            if (selected && *selected->value) {
                char *end=NULL; double value=strtod(selected->value,&end);
                while (end && (*end==' ' || (*end>='\t' && *end<='\r'))) ++end;
                if (end && !*end && isfinite(value)) {
                    for (size_t i=0;i<qa_base_arena_catalog_count(owner->catalog);++i) {
                        const qa_base_arena *arena=qa_base_arena_catalog_at(owner->catalog,i);
                        bool available=false;
                        if (arena->selection!=value) continue;
                        if (!qa_arena_progress_available(&owner->progression,arena->number,&available,error)) return false;
                        if (available) owner->menu_selection=arena->number;
                        break;
                    }
                }
            }
            owner->selection_modification=modification;
            owner->progression_revision=owner->progression.current_revision;
            owner->selection_valid=true;
        }
        out->selection=owner->menu_selection;
        out->game=owner->base_game; out->postgame=owner->base_result;
        out->player_count=owner->player_count; out->player_client=owner->player_client;
        for (size_t i=0;i<owner->player_count;++i)
            out->players[i]=(frontend_campaign_podium_player){owner->players[i].client,owner->players[i].rank,owner->players[i].score};
    }
    return qa_application_q3_campaign_current(f->application,&out->source) &&
        qa_application_client_current(f->application,&out->client);
}
bool frontend_campaign_postgame_frame(qa_frontend *f,uint32_t seat,
    frontend_campaign_ui_view *view,qa_error *error)
{
    frontend_campaign *owner=f->campaign;
    if (!owner || !view->installed || !view->result) return true;
    if (view->team) {
        if (owner->result) {
            qa_error lookup={0};
            if (!campaign_team_demo(owner,&view->source,&view->client.context.command,&lookup) && lookup.code!=QA_OK)
                qa_application_feature_report(f->application,"postgame demo",&lookup);
            if (owner->demo_path!=QA_STRING_NONE)
                view->demo_path=qa_strings_cstr(qa_session_strings(view->source.session),owner->demo_path);
        }
        return true;
    }
    if (owner->player_client<0 || view->source_client!=(uint32_t)owner->player_client) return true;
    uint64_t elapsed=f->wall_time_ns>=owner->result_at_ns?
        (f->wall_time_ns-owner->result_at_ns)/UINT64_C(1000000):0;
    if (!owner->announced && elapsed>=1000) {
        char path[128]="sound/player/announce/youwin.wav";
        bool available=owner->base_result.rank==1;
        if (!available && owner->player_count && owner->players[0].client>=0) {
            char name[80];
            if (!qa_application_q3_campaign_player_name(f->application,&view->source,
                    (uint32_t)owner->players[0].client,name,error)) return false;
            size_t size=0;
            for (size_t at=0;name[at];++at) {
                uint8_t color=0;
                if (qa_text_q3_color_escape((uint8_t)name[at],(uint8_t)name[at+1],&color)) { ++at; continue; }
                name[size++]=(char)qa_text_ascii_lower((uint8_t)name[at]);
            }
            name[size]=0;
            available=size!=0;
            snprintf(path,sizeof(path),"sound/player/announce/%s_wins.wav",name);
        }
        if (available && f->audio) {
            qa_audio_bank *bank;
            if (!frontend_event_sounds(f,view->source.source_owner,QA_GAME_Q3,&bank,error) ||
                !frontend_ui_local_sound(f->seats+seat,bank,QA_GAME_Q3,path,7,NULL,error)) return false;
        }
        owner->announced=true;
    }
    uint64_t awards=(uint64_t)owner->base_result.award_count*2000;
    if (!owner->movie_played && owner->base_result.unlocked_movie>=0 &&
        elapsed>=5000+(awards>5000?awards:5000)) {
        const qa_arena_catalog *catalog=&owner->progression.catalog;
        char text[64];
        if (catalog->final>=0 && owner->base_result.completed_tier==catalog->regular_levels/4+1)
            strcpy(text,"cinematic end.RoQ\n");
        else snprintf(text,sizeof(text),"cinematic tier%" PRId32 ".RoQ\n",owner->base_result.unlocked_movie);
        if (!qa_console_append(view->client.context.console,&view->client.context.command,text,error)) return false;
        owner->movie_played=true;
    }
    return true;
}
bool frontend_campaign_ui_stage(qa_frontend *f,uint32_t seat,frontend_campaign_ui_action action,
    int32_t arena,int32_t skill,qa_error *error)
{
    frontend_campaign_ui_view view;
    if (action>FRONTEND_CAMPAIGN_QUIT || action<FRONTEND_CAMPAIGN_PLAY ||
        !frontend_campaign_ui_read(f,seat,&view,error) || !view.installed || !frontend_campaign_ready(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign action requires its idle installed local recipient");
    if (view.team && action!=FRONTEND_CAMPAIGN_QUIT && action!=FRONTEND_CAMPAIGN_RETRY &&
        action!=FRONTEND_CAMPAIGN_NEXT && action!=FRONTEND_CAMPAIGN_DEMO)
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
    qa_console *console; qa_cvars *cvars; qa_command_context command;
    if (!qa_input_seat_recipient_read(f->seats[seat].input, &console, &cvars, &command)) return false;
    (void)cvars;
    if (action==FRONTEND_CAMPAIGN_DEMO) {
        if (!view.team || !view.result || !view.demo_path)
            return frontend_fail(error,QA_ERROR_NOT_FOUND,"No demo recording is available for this Team Arena result");
        size_t length=strlen(view.demo_path);
        if (length>SIZE_MAX-9) return frontend_fail(error,QA_ERROR_MEMORY,"Demo command exceeds memory extent");
        qa_unified_frame_lease *storage=application_control_storage_acquire(f->application,error);
        if (!storage) return false;
        char *text=qa_arena_alloc(qa_unified_frame_lease_arena(storage),length+9,1,error);
        bool okay=text!=NULL;
        if (okay) {
            snprintf(text,length+9,"demo \"%s\"\n",view.demo_path);
            okay=qa_console_append(console,&command,text,error);
        }
        qa_unified_frame_lease_release(storage);
        return okay;
    }
    char text[96];
    if (action == FRONTEND_CAMPAIGN_RESET) strcpy(text, "arena-reset\n");
    else if (action == FRONTEND_CAMPAIGN_QUIT) strcpy(text, "quit\n");
    else if (action == FRONTEND_CAMPAIGN_RETRY) strcpy(text, "arena-retry\n");
    else if (action == FRONTEND_CAMPAIGN_NEXT) strcpy(text, "arena-next\n");
    else snprintf(text, sizeof(text), "arena-play %" PRId32 " %" PRId32 "\n", arena, skill);
    return qa_console_append(console, &command, text, error);
}
static bool campaign_launch_action(qa_frontend *f, const qa_application_q3_campaign *view,
    const qa_command_invocation *command, qa_error *error)
{
    frontend_campaign *owner = f->campaign;
    bool play = !strcmp(command->argv[0], "arena-play");
    bool retry = !strcmp(command->argv[0], "arena-retry");
    if(owner->kind==CAMPAIGN_TEAM) {
        if(play)return true;
        if(!owner->result)return frontend_fail(error,QA_ERROR_ARGUMENT,"Team Arena match has not finished");
        uint32_t physical;
        if(!frontend_seat_ordinal_read(f,command->context.seat,&physical))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Team Arena continuation requires its local recipient");
        return frontend_startup_campaign_stage(f->seats+physical,view,!retry,error);
    }
    if (owner->kind != CAMPAIGN_BASE) return true;
    int32_t number = play ? campaign_number(command, 0) : 0;
    int32_t skill = play ? campaign_number(command, 1) : 0;
    const qa_base_arena *arena=NULL;
    if (retry) arena=qa_base_arena_catalog_find(owner->catalog,view->map);
    else {
        if (!play && !qa_arena_progress_current(&owner->progression,&number,error)) return false;
        for (size_t i=0;i<qa_base_arena_catalog_count(owner->catalog);++i) {
            const qa_base_arena *row=qa_base_arena_catalog_at(owner->catalog,i);
            if (row->number==number) { arena=row; break; }
        }
    }
    if (!arena) return frontend_fail(error,QA_ERROR_NOT_FOUND,"Campaign action has no authored arena map");
    qa_launch_draft *draft=NULL;
    if (!qa_launch_snapshot_draft_copy(view->publication,&draft,error)) return false;
    qa_launch_world world=qa_launch_draft_choices(draft)->world;
    world.map=arena->map; world.start_command=NULL; world.spawn_point=NULL; world.explicit_spawn_point=false;
    if (play) world.skill=skill;
    char text[16]; (void)snprintf(text,sizeof(text),"%" PRId32,skill);
    qa_application_q3_setting rows[]={ {"g_gametype","2"},{"sv_cheats","0"},{"g_doWarmup","0"},
        {"sv_maxclients","8"},{"g_spSkill",text} };
    bool ok=qa_launch_set_world(draft,&world,error) &&
        qa_application_q3_campaign_launch(f->application,view,draft,rows,play?5:4,error);
    qa_launch_draft_destroy(draft);
    return ok && (qa_application_startup_pending(f->application) || frontend_campaign_sync(f,error));
}
