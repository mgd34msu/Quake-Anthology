#include "bots_round.h"
#include "bots_private.h"
#include "guest_q3_private.h"
#include "map_players_private.h"

typedef struct bot_round_client {
    qa_actor_id previous_actor;
    uint32_t seat,source_slot,seat_index;
    qa_bot_admission admission;
    char *character,*name;
    bool native,completed;
} bot_round_client;
typedef struct bot_round_graph {
    application_bot_graph *owner;
    qa_navigation *previous,*fresh;
} bot_round_graph;
struct application_bots_round {
    application_bots *bots;
    application_provider *provider;
    qa_resource *map;
    qa_world *world;
    bot_round_client *clients;
    bot_round_graph *graphs;
    size_t count,graph_count;
    qa_bot_navigation *map_navigation;
    uint64_t bound_frame,bound_elapsed;
    bool had_population,begun,bound,resumed;
};

static char *copy_text(const char *value,qa_error *error) {
    size_t size=strlen(value)+1;char *copy=malloc(size);
    if(!copy) {application_fail(error,QA_ERROR_MEMORY,"retaining bot round settings");return NULL;}
    memcpy(copy,value,size);return copy;
}
static bool idle(application_bots_round *cut,qa_error *error) {
    application_bots *bots=cut?cut->bots:NULL;
    return bots && bots->application && bots->application->bots==bots &&
        bots->map_resource==cut->map && bots->application->map_resource==cut->map &&
        bots->application->world==cut->world && bots->application->players &&
        bots->application->players->map_provider==cut->provider &&
        !bots->restoring && !bots->producing && application_bots_can_destroy(bots->application) &&
        qa_session_safe(bots->application->session)?true:
        application_fail(error,QA_ERROR_ARGUMENT,"bot round cut no longer owns its idle source and map");
}
static bool settled(application_bots_round *cut,uint64_t frames,qa_error *error) {
    qa_clock_state clock;
    if(!qa_session_clock(cut->bots->application->session,cut->provider->owner,&clock) ||
       cut->bound_frame>UINT64_MAX-frames || cut->bound_elapsed>UINT64_MAX-frames*UINT64_C(100000000) ||
       clock.frame_number!=cut->bound_frame+frames ||
       clock.elapsed_ns!=cut->bound_elapsed+frames*UINT64_C(100000000) ||
       clock.frame.phase!=QA_FRAME_EXIT)
        return application_fail(error,QA_ERROR_ARGUMENT,"bot round requires the actual completed source settlement frames");
    return true;
}
static bot_round_client *client(application_bots_round *cut,const qa_application_q3_round_client *row) {
    if(!row || !row->bot) return NULL;
    for(size_t i=0;i<cut->count;++i)
        if(cut->clients[i].source_slot==row->source_slot && cut->clients[i].seat==row->seat &&
           qa_actor_id_equal(cut->clients[i].previous_actor,row->previous_actor)) return cut->clients+i;
    return NULL;
}
bool application_bots_round_prepare(qa_application *app,application_provider *provider,
    const qa_application_q3_round_client *rows,size_t count,application_bots_round **out,qa_error *error) {
    if(!app || !out || *out || (!rows && count) || !provider || !app->players ||
       app->players->map_provider!=provider)
        return application_fail(error,QA_ERROR_ARGUMENT,"bot round requires an empty cut and actual source roster");
    application_bots *bots=app->bots;if(!bots) return true;
    if(bots->round || bots->round_phase!=APPLICATION_BOT_ROUND_ACTIVE || bots->restoring ||
       !application_bots_can_destroy(app) || !qa_session_safe(app->session) || qa_bot_runtime_closed(bots->runtime))
        return application_fail(error,QA_ERROR_ARGUMENT,"bot round requires its actual idle retained library");
    application_bots_round *cut=calloc(1,sizeof(*cut));
    if(!cut) return application_fail(error,QA_ERROR_MEMORY,"allocating bot round cut");
    cut->bots=bots;cut->provider=provider;cut->map=bots->map_resource;cut->world=app->world;
    cut->had_population=bots->population!=NULL;
    size_t bot_count=0;for(size_t i=0;i<count;++i) if(rows[i].bot) ++bot_count;
    if((bot_count || cut->had_population) &&
       (!qa_bot_runtime_initialized(bots->runtime) || !qa_bot_runtime_loaded(bots->runtime))) goto invalid;
    cut->clients=bot_count?calloc(bot_count,sizeof(*cut->clients)):NULL;
    if(bot_count && !cut->clients) goto memory;
    for(size_t i=0;i<count;++i) {
        const qa_application_q3_round_client *row=rows+i;if(!row->bot) continue;
        if(!qa_actors_get(qa_session_actors(app->session),row->previous_actor)) goto invalid;
        bot_round_client *saved=cut->clients+cut->count++;
        saved->previous_actor=row->previous_actor;saved->seat=row->seat;saved->source_slot=row->source_slot;
        saved->seat_index=UINT32_MAX;
        for(uint32_t j=0;j<bots->capacity;++j) {
            const application_bot_seat *seat=bots->seats+j;
            if(!seat->retired && qa_actor_id_equal(seat->actor,row->previous_actor)) {
                if(seat->seat!=row->seat || !bots->population ||
                   !qa_bots_admission_read(bots->population,seat->actor,&saved->admission,error) ||
                   saved->admission.client!=seat->library_client) goto failed;
                saved->native=true;saved->seat_index=j;
                saved->character=copy_text(saved->admission.character_file,error);
                saved->name=copy_text(saved->admission.name,error);
                if(!saved->character || !saved->name) goto failed;
                saved->admission.character_file=saved->character;saved->admission.name=saved->name;
                break;
            }
        }
        if(!saved->native) {
            struct application_q3_guest *engine=q3g_engine(provider);
            bool namespace=false;for(application_bot_guest *guest=bots->guests;guest;guest=guest->next)
                namespace=namespace || guest->provider==provider;
            if(!namespace || !engine || row->source_slot>=64 ||
               !engine->clients[row->source_slot].bot ||
               !qa_actor_id_equal(engine->clients[row->source_slot].actor,row->previous_actor)) goto invalid;
        }
        for(size_t j=0;j+1<cut->count;++j)
            if(cut->clients[j].source_slot==saved->source_slot ||
               qa_actor_id_equal(cut->clients[j].previous_actor,saved->previous_actor)) goto invalid;
    }
    for(uint32_t i=0;i<bots->capacity;++i) {
        const application_bot_seat *seat=bots->seats+i;if(seat->retired || !seat->actor.registry) continue;
        bool found=false;for(size_t j=0;j<cut->count;++j)
            found=found || (cut->clients[j].native && cut->clients[j].seat_index==i);
        if(!found) goto invalid;
    }
    for(application_bot_graph *graph=bots->graphs;graph;graph=graph->next) ++cut->graph_count;
    cut->graphs=cut->graph_count?calloc(cut->graph_count,sizeof(*cut->graphs)):NULL;
    if(cut->graph_count && !cut->graphs) goto memory;
    size_t ordinal=0;qa_navigation_services services=application_bot_navigation_services(bots);
    qa_navigation *map_runtime=qa_bot_navigation_runtime(bots->map_navigation);
    for(application_bot_graph *graph=bots->graphs;graph;graph=graph->next) {
        bot_round_graph *entry=cut->graphs+ordinal++;
        entry->owner=graph;entry->previous=graph->navigation;
        if(!qa_navigation_create(graph->graph,&services,&entry->fresh,error)) goto failed;
        if(graph->navigation==map_runtime &&
           !application_bot_navigation_restore_binding(bots,entry->fresh,(qa_actor_id){0},&cut->map_navigation,error)) goto failed;
    }
    if(!cut->map_navigation) goto invalid;
    bots->round=cut;*out=cut;return true;
memory:
    application_fail(error,QA_ERROR_MEMORY,"allocating retained bot round inventory");goto failed;
invalid:
    application_fail(error,QA_ERROR_ARGUMENT,"bot round roster differs from its actual retained clients");
failed:
    if(!error || error->code==QA_OK)
        application_fail(error,QA_ERROR_ARGUMENT,"bot round preparation lost an actual retained owner");
    application_bots_round_dispose(cut);return false;
}
bool application_bots_round_begin(application_bots_round *cut,qa_error *error) {
    if(!cut) return true;
    if(!idle(cut,error) || cut->begun || cut->bots->round!=cut) return false;
    application_bots *bots=cut->bots;
    for(size_t i=0;i<cut->graph_count;++i)
        if(cut->graphs[i].owner->navigation!=cut->graphs[i].previous)
            return application_fail(error,QA_ERROR_ARGUMENT,"bot navigation changed after round preparation");
    for(size_t i=0;i<cut->count;++i) {
        if(!qa_actors_get(qa_session_actors(bots->application->session),cut->clients[i].previous_actor))
            return application_fail(error,QA_ERROR_ARGUMENT,"bot client retired before round mutation");
        const bot_round_client *saved=cut->clients+i;
        if(saved->native) {
            qa_bot_admission actual;
            if(!qa_bots_admission_read(bots->population,saved->previous_actor,&actual,error)) return false;
            if(actual.client!=saved->admission.client || actual.entity!=saved->admission.entity ||
               actual.mode.slot!=saved->admission.mode.slot || actual.mode.generation!=saved->admission.mode.generation ||
               actual.team_arena!=saved->admission.team_arena ||
               memcmp(&actual.skill,&saved->admission.skill,sizeof(actual.skill)) ||
               strcmp(actual.character_file,saved->character) || strcmp(actual.name,saved->name))
                return application_fail(error,QA_ERROR_ARGUMENT,"actual bot settings changed after round preparation");
        }
    }
    cut->begun=true;bots->round_phase=APPLICATION_BOT_ROUND_DETACHED;
    ++bots->calls;bool ok=qa_bots_destroy(bots->population,error);--bots->calls;
    if(!ok) {bots->round_phase=APPLICATION_BOT_ROUND_FAILED;return false;}
    bots->population=NULL;
    return true;
}
bool application_bots_round_bind(application_bots_round *cut,qa_error *error) {
    if(!cut) return true;
    if(!idle(cut,error) || !cut->begun || cut->bound ||
       cut->bots->round_phase!=APPLICATION_BOT_ROUND_DETACHED) return false;
    application_bots *bots=cut->bots;
    for(size_t i=0;i<cut->count;++i)
        if(qa_actors_get(qa_session_actors(bots->application->session),cut->clients[i].previous_actor))
            return application_fail(error,QA_ERROR_ARGUMENT,"retire old bot generations before binding the new source round");
    const char *name=qa_strings_cstr(qa_session_strings(bots->application->session),bots->application->current_map);
    qa_bot_runtime_map map={.name=name,.entities=&bots->entities,.navigation=cut->map_navigation};
    if(!qa_bot_runtime_rebind_round(bots->runtime,&map,error)) goto failed;
    for(uint32_t i=0;i<bots->capacity;++i) {
        qa_bot_navigation_destroy(bots->seats[i].navigation);bots->seats[i].navigation=NULL;
        bots->seats[i].actor=(qa_actor_id){0};bots->seats[i].retired=true;
    }
    while(bots->targets) {application_bot_target *target=bots->targets;bots->targets=target->next;
        qa_bot_navigation_destroy(target->navigation);free(target);}
    qa_bot_navigation_destroy(bots->map_navigation);
    bots->map_navigation=cut->map_navigation;cut->map_navigation=NULL;
    for(size_t i=0;i<cut->graph_count;++i) {
        bot_round_graph *entry=cut->graphs+i;qa_navigation_destroy(entry->previous);
        entry->previous=NULL;entry->owner->navigation=entry->fresh;entry->fresh=NULL;
    }
    qa_builtin_snapshot_free(&bots->pickup_snapshot);
    qa_bot_services services=application_bots_services(bots);
    if(cut->had_population && !qa_bots_create_round(bots->runtime,&services,&bots->population,error)) goto failed;
    qa_clock_state clock;
    if(!qa_session_clock(bots->application->session,cut->provider->owner,&clock)) goto failed;
    cut->bound_frame=clock.frame_number;cut->bound_elapsed=clock.elapsed_ns;
    cut->bound=true;bots->round_phase=APPLICATION_BOT_ROUND_BOUND;
    return true;
failed:
    bots->round_phase=APPLICATION_BOT_ROUND_FAILED;return false;
}
bool application_bots_round_reconnect(application_bots_round *cut,
    const qa_application_q3_round_client *row,qa_actor_id actor,qa_error *error) {
    if(!cut || !row || !row->bot) return true;
    if(!idle(cut,error) || !cut->bound || cut->resumed || !settled(cut,3,error)) return false;
    bot_round_client *saved=client(cut,row);
    if(!saved || saved->completed || !qa_actors_get(qa_session_actors(cut->bots->application->session),actor))
        return application_fail(error,QA_ERROR_ARGUMENT,"bot reconnect has no pending actual new source actor");
    application_bots *bots=cut->bots;
    application_player_record *record=NULL;
    for(size_t i=0;bots->application->players && i<bots->application->players->count;++i) {
        application_player_record *candidate=bots->application->players->records+i;
        if(!candidate->retiring && candidate->bot && candidate->source_slot==row->source_slot &&
           qa_actor_id_equal(candidate->actor,actor)) {record=candidate;break;}
    }
    if(!record || record->character!=cut->provider)
        return application_fail(error,QA_ERROR_ARGUMENT,"bot reconnect differs from the actual admitted source roster");
    ++bots->calls;bool ok;
    if(saved->native) {
        application_bot_seat *seat=bots->seats+saved->seat_index;
        seat->actor=actor;seat->seat=saved->seat;seat->library_client=saved->admission.client;seat->retired=false;
        ok=application_bot_navigation_bind(bots,seat,error);
        qa_bot_admission admission=saved->admission;admission.actor=actor;admission.entity=(int32_t)actor.slot;
        if(ok) ok=qa_bots_admit(bots->population,&admission,error);
    } else ok=application_bots_guest_admit(bots->application,actor,error);
    --bots->calls;
    if(!ok) {bots->round_phase=APPLICATION_BOT_ROUND_FAILED;return false;}
    saved->completed=true;return true;
}
bool application_bots_round_reject(application_bots_round *cut,
    const qa_application_q3_round_client *row,qa_error *error) {
    if(!cut || !row || !row->bot) return true;
    if(!idle(cut,error) || !cut->bound || !settled(cut,3,error)) return false;
    bot_round_client *saved=client(cut,row);
    if(!saved || saved->completed) return application_fail(error,QA_ERROR_ARGUMENT,"bot rejection has no pending retained client");
    saved->completed=true;return true;
}
bool application_bots_round_resume(application_bots_round *cut,qa_error *error) {
    if(!cut) return true;
    if(!idle(cut,error) || !cut->bound || cut->resumed || !settled(cut,4,error)) return false;
    for(size_t i=0;i<cut->count;++i)
        if(!cut->clients[i].completed) return application_fail(error,QA_ERROR_ARGUMENT,"reconnect every retained bot before resuming AI");
    cut->resumed=true;cut->bots->round_phase=APPLICATION_BOT_ROUND_ACTIVE;return true;
}
void application_bots_round_detach(application_bots_round *cut) {
    if(cut && cut->bots) {cut->bots->round=NULL;cut->bots=NULL;}
}
void application_bots_round_dispose(application_bots_round *cut) {
    if(!cut) return;
    if(cut->bots && cut->bots->round==cut) {
        cut->bots->round=NULL;
        if(cut->begun && !cut->resumed) cut->bots->round_phase=APPLICATION_BOT_ROUND_FAILED;
    }
    qa_bot_navigation_destroy(cut->map_navigation);
    if(cut->graphs) for(size_t i=0;i<cut->graph_count;++i) qa_navigation_destroy(cut->graphs[i].fresh);
    for(size_t i=0;i<cut->count;++i) {free(cut->clients[i].character);free(cut->clients[i].name);}
    free(cut->graphs);free(cut->clients);free(cut);
}
