#include "bots_round.h"
#include "bots_private.h"
#include "bots_catalog.h"
#include "guest_q3_private.h"
#include "map_players_private.h"
#include "q3_world_restart.h"

typedef struct bot_round_client {
    qa_actor_id previous_actor;
    uint32_t seat,source_slot,seat_index;
    qa_bot_admission admission;
    char *character,*name;
    char team[144];
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
typedef struct bot_original_pair {
    application_provider *previous,*next;
} bot_original_pair;
struct application_bots_original {
    application_bots *bots;
    qa_bot_runtime *runtime;
    application_publication *publication;
    qa_world *world;
    qa_resource *map;
    qa_string_id map_name;
    application_provider *previous_source,*next_source;
    bot_original_pair *pairs;
    size_t pair_count,graph_count;
    bot_round_graph *graphs;
    qa_bot_navigation *map_navigation;
    bool initialized,loaded,rebound;
};

static bool original_text(const char *a,const char *b) {
    return a && b?!strcmp(a,b):a==b;
}
static bool original_resource(const qa_resource *a,const qa_resource *b) {
    return a && b?qa_sha256_equal(qa_resource_digest(a),qa_resource_digest(b)):a==b;
}
static bool original_library_ready(const qa_cvars *configuration,bool initialized,bool loaded,qa_error *error) {
    const qa_cvar_view *enabled=configuration?qa_cvars_find(configuration,"bot_enable"):NULL;
    if(!enabled)
        return application_fail(error,QA_ERROR_ARGUMENT,"original bots require their actual source bot_enable cvar");
    int32_t effective=enabled->integer;
    if(enabled->latched_value) {
        qa_cvar_options options={.dialect=QA_CONSOLE_Q3};
        qa_cvars *resolved=qa_cvars_create(&options,error);
        if(!resolved) return false;
        bool okay=qa_cvars_register(resolved,"bot_enable",enabled->latched_value,0,0,NULL,error);
        if(okay) effective=qa_cvars_find(resolved,"bot_enable")->integer;
        qa_cvars_destroy(resolved);
        if(!okay) return false;
    }
    return !effective || (initialized && loaded)?true:
        application_fail(error,QA_ERROR_ARGUMENT,"enabled original restart bots require their genuinely initialized loaded retained library");
}
static bool original_instance(const application_provider *a,const application_provider *b) {
    const qa_launch_instance *old=a?a->launch:NULL,*next=b?b->launch:NULL;
    if(!old || !next || a==b || a->application!=b->application || a->kind!=b->kind ||
       old->roles!=next->roles || old->selection.runtime!=next->selection.runtime ||
       old->selection.product!=next->selection.product ||
       !original_text(old->selection.instance,next->selection.instance) ||
       !original_text(old->selection.implementation,next->selection.implementation) ||
       !original_text(old->selection.artifact,next->selection.artifact) ||
       !original_text(old->selection.component,next->selection.component) ||
       old->selection.options.size!=next->selection.options.size ||
       (old->selection.options.size && memcmp(old->selection.options.data,next->selection.options.data,old->selection.options.size)) ||
       !original_resource(old->artifact,next->artifact) || !original_resource(old->declaration,next->declaration) ||
       old->interface_count!=next->interface_count || old->behavior_count!=next->behavior_count) return false;
    for(size_t i=0;i<old->interface_count;++i)
        if(old->interfaces[i].product!=next->interfaces[i].product ||
           !original_text(old->interfaces[i].path,next->interfaces[i].path) ||
           !original_resource(old->interfaces[i].resource,next->interfaces[i].resource)) return false;
    for(size_t i=0;i<old->behavior_count;++i)
        if(old->behaviors[i]!=next->behaviors[i]) return false;
    return true;
}
static application_provider *original_next(const application_bots_original *cut,
    const application_provider *previous) {
    if(!previous) return NULL;
    for(size_t i=0;i<cut->pair_count;++i)
        if(cut->pairs[i].previous==previous) return cut->pairs[i].next;
    return NULL;
}
static bool original_idle(const application_bots_original *cut,qa_error *error) {
    application_bots *bots=cut?cut->bots:NULL;
    qa_application *app=bots?bots->application:NULL;
    application_q3_world_startup startup;
    return app && app->bots==bots && bots->original==cut && !cut->rebound &&
        app->operation==APPLICATION_CONFIGURING && cut->publication &&
        application_q3_world_restart_source(app,cut->publication->map_provider,&startup) && startup.restart &&
        app->world==cut->world && app->map_resource==cut->map && bots->map_resource==cut->map && bots->source==cut->previous_source &&
        app->current_map==cut->map_name && !bots->population && !bots->restoring && !bots->round &&
        bots->round_phase==APPLICATION_BOT_ROUND_ACTIVE && !bots->producing &&
        application_bots_can_destroy(app) && qa_session_safe(app->session) &&
        application_bots_guest_runtime(app,cut->previous_source)==cut->runtime &&
        qa_bot_runtime_initialized(cut->runtime)==cut->initialized &&
        qa_bot_runtime_loaded(cut->runtime)==cut->loaded && !qa_bot_runtime_closed(cut->runtime)?true:
        application_fail(error,QA_ERROR_ARGUMENT,"original bot handoff lost its actual retained source/map/library");
}
bool application_bots_original_prepare(qa_application *app,application_publication *publication,
    application_bots_original **out,qa_error *error) {
    if(!app || !publication || !out || *out)
        return application_fail(error,QA_ERROR_ARGUMENT,"original bot handoff requires actual publication and an empty owner");
    application_bots *bots=app->bots;if(!bots) return true;
    application_q3_world_startup startup;
    application_provider *old_source=app->players?app->players->map_provider:NULL;
    struct application_q3_guest *old_engine=old_source?q3g_engine(old_source):NULL;
    qa_bot_runtime *runtime=application_bots_guest_runtime(app,old_source);
    if(app->operation!=APPLICATION_CONFIGURING || !publication->travel || publication->restoring ||
       !application_q3_world_restart_source(app,publication->map_provider,&startup) || !startup.restart ||
       bots->source!=old_source || !old_engine || !old_engine->game || !old_engine->game->initialized || !old_source->constructed ||
       !old_source->attached || old_source->close_pending || publication->removed_count!=publication->next_count ||
       !publication->map_resource || !original_resource(publication->map_resource,bots->map_resource) ||
       bots->map_resource!=app->map_resource || !app->world || !bots->map_navigation || bots->population ||
       bots->original || bots->round || bots->round_phase!=APPLICATION_BOT_ROUND_ACTIVE || bots->restoring ||
       !application_bots_can_destroy(app) || !qa_session_safe(app->session) || !runtime || qa_bot_runtime_closed(runtime))
        return application_fail(error,QA_ERROR_ARGUMENT,"original bot handoff requires its genuine idle same-map replacement");
    qa_cvars *configuration=NULL;qa_q3_host_console(old_engine->game->host,&configuration,NULL);
    bool initialized=qa_bot_runtime_initialized(runtime),loaded=qa_bot_runtime_loaded(runtime);
    if(!original_library_ready(configuration,initialized,loaded,error)) return false;
    application_bots_original *cut=calloc(1,sizeof(*cut));
    if(!cut) return application_fail(error,QA_ERROR_MEMORY,"allocating original bot lifetime handoff");
    *cut=(application_bots_original){.bots=bots,.runtime=runtime,.publication=publication,.world=app->world,
        .map=bots->map_resource,.map_name=app->current_map,.initialized=initialized,.loaded=loaded,
        .previous_source=old_source,.next_source=publication->map_provider,
        .pair_count=publication->removed_count};
    if(cut->pair_count>SIZE_MAX/sizeof(*cut->pairs)) goto invalid;
    cut->pairs=cut->pair_count?calloc(cut->pair_count,sizeof(*cut->pairs)):NULL;
    if(cut->pair_count && !cut->pairs) goto memory;
    for(size_t i=0;i<cut->pair_count;++i) {
        application_provider *previous=publication->removed[i],*next=NULL;
        for(size_t j=0;j<publication->next_count;++j) {
            if(!original_instance(previous,publication->next[j])) continue;
            if(next) goto invalid;next=publication->next[j];
        }
        if(!next || !previous->constructed || !next->constructed || next->attached) goto invalid;
        for(size_t j=0;j<i;++j)
            if(cut->pairs[j].previous==previous || cut->pairs[j].next==next) goto invalid;
        cut->pairs[i]=(bot_original_pair){previous,next};
    }
    if(original_next(cut,old_source)!=publication->map_provider) goto invalid;
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next) {
        application_provider *next=original_next(cut,guest->provider);
        struct application_q3_guest *engine=next?q3g_engine(next):NULL;
        if(!engine || !engine->game || engine->game->initialized) goto invalid;
    }
    for(application_bot_graph *graph=bots->graphs;graph;graph=graph->next) {
        if(graph->movement && !original_next(cut,graph->movement)) goto invalid;
        ++cut->graph_count;
    }
    if(cut->graph_count>SIZE_MAX/sizeof(*cut->graphs)) goto invalid;
    cut->graphs=cut->graph_count?calloc(cut->graph_count,sizeof(*cut->graphs)):NULL;
    if(cut->graph_count && !cut->graphs) goto memory;
    qa_navigation_services services=application_bot_navigation_services(bots);
    qa_navigation *map=qa_bot_navigation_runtime(bots->map_navigation);size_t i=0;
    for(application_bot_graph *graph=bots->graphs;graph;graph=graph->next) {
        bot_round_graph *entry=cut->graphs+i++;entry->owner=graph;entry->previous=graph->navigation;
        if(!qa_navigation_create(graph->graph,&services,&entry->fresh,error)) goto failed;
        if(graph->navigation==map &&
           !application_bot_navigation_restore_binding(bots,entry->fresh,(qa_actor_id){0},&cut->map_navigation,error)) goto failed;
    }
    if(!cut->map_navigation) goto invalid;
    bots->original=cut;*out=cut;return true;
memory:
    application_fail(error,QA_ERROR_MEMORY,"allocating actual original bot replacement bindings");goto failed;
invalid:
    application_fail(error,QA_ERROR_ARGUMENT,"original bot handoff differs from actual retained provider/navigation identities");
failed:
    application_bots_original_dispose(cut);return false;
}
bool application_bots_original_rebind(application_bots_original *cut,qa_error *error) {
    if(!cut) return true;
    if(!original_idle(cut,error)) return false;
    application_bots *bots=cut->bots;application_publication *publication=cut->publication;
    if(publication->removed_count!=cut->pair_count || publication->next_count!=cut->pair_count)
        return application_fail(error,QA_ERROR_ARGUMENT,"original bot replacement inventory changed before retirement");
    for(size_t i=0;i<cut->pair_count;++i) {
        const bot_original_pair *pair=cut->pairs+i;bool old_found=false,next_found=false;
        for(size_t j=0;j<cut->pair_count;++j) {
            old_found=old_found || publication->removed[j]==pair->previous;
            next_found=next_found || publication->next[j]==pair->next;
        }
        if(!old_found || !next_found || !original_instance(pair->previous,pair->next) ||
           pair->previous->constructed || pair->previous->attached || !pair->next->constructed)
            return application_fail(error,QA_ERROR_ARGUMENT,"retire all actual old source executors before original bot rebind");
    }
    for(uint32_t i=0;i<bots->capacity;++i)
        if(bots->seats[i].actor.registry && qa_actors_get(qa_session_actors(bots->application->session),bots->seats[i].actor))
            return application_fail(error,QA_ERROR_ARGUMENT,"original bot handoff still owns an old admitted actor");
    for(application_bot_target *target=bots->targets;target;target=target->next)
        if(qa_actors_get(qa_session_actors(bots->application->session),target->actor))
            return application_fail(error,QA_ERROR_ARGUMENT,"original bot handoff still owns an old navigation actor");
    for(size_t i=0;i<cut->graph_count;++i)
        if(cut->graphs[i].owner->navigation!=cut->graphs[i].previous)
            return application_fail(error,QA_ERROR_ARGUMENT,"original bot graph owner changed during source retirement");
    const char *name=qa_strings_cstr(qa_session_strings(bots->application->session),cut->map_name);
    qa_bot_runtime_map map={.name=name,.entities=&bots->entities,.navigation=cut->map_navigation};
    if(!qa_bot_runtime_rebind_round(bots->runtime,&map,error)) return false;
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next)
        if(!qa_bot_runtime_rebind_round(guest->runtime,&map,error)) return false;
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next)
        guest->provider=original_next(cut,guest->provider);
    bots->source=cut->next_source;
    for(uint32_t i=0;i<bots->capacity;++i) {
        qa_bot_navigation_destroy(bots->seats[i].navigation);bots->seats[i].navigation=NULL;
        bots->seats[i].actor=(qa_actor_id){0};bots->seats[i].retired=true;
    }
    while(bots->targets) {application_bot_target *target=bots->targets;bots->targets=target->next;
        qa_bot_navigation_destroy(target->navigation);free(target);}
    qa_bot_navigation_destroy(bots->map_navigation);bots->map_navigation=cut->map_navigation;cut->map_navigation=NULL;
    for(size_t i=0;i<cut->graph_count;++i) {
        bot_round_graph *entry=cut->graphs+i;
        if(entry->owner->movement) entry->owner->movement=original_next(cut,entry->owner->movement);
        qa_navigation_destroy(entry->previous);entry->previous=NULL;
        entry->owner->navigation=entry->fresh;entry->fresh=NULL;
    }
    qa_builtin_snapshot_free(&bots->pickup_snapshot);
    if(bots->map_resource!=publication->map_resource) {
        qa_resource *previous=bots->map_resource;
        qa_resource_retain(publication->map_resource);bots->map_resource=publication->map_resource;
        bots->geometry=publication->map;
        if(!bots->source_map_resource) bots->source_map_resource=previous;
        else qa_resource_release(previous);
    }
    cut->rebound=true;return true;
}
bool application_bots_original_validate_source(application_bots_original *cut,
    application_provider *source,const qa_cvars *configuration,qa_error *error) {
    if(!cut) return true;
    application_bots *bots=cut->bots;qa_application *app=bots?bots->application:NULL;
    bool previous=source==cut->previous_source,next=source==cut->next_source;
    struct application_q3_guest *engine=source?q3g_engine(source):NULL;
    qa_cvars *actual=NULL;
    if(engine && engine->game) qa_q3_host_console(engine->game->host,&actual,NULL);
    if(!app || app->bots!=bots || bots->original!=cut || app->operation!=APPLICATION_CONFIGURING ||
       (!previous && !next) || (previous && cut->rebound) || (next && !cut->rebound) ||
       !source->constructed || source->application!=app || !engine || !engine->game ||
       actual!=configuration || !configuration || (next && engine->game->initialized) ||
       bots->population || bots->restoring || bots->source!=(cut->rebound?cut->next_source:cut->previous_source) ||
       application_bots_guest_runtime(app,source)!=cut->runtime || qa_bot_runtime_closed(cut->runtime) ||
       qa_bot_runtime_initialized(cut->runtime)!=cut->initialized ||
       qa_bot_runtime_loaded(cut->runtime)!=cut->loaded)
        return application_fail(error,QA_ERROR_ARGUMENT,"original bot restart readiness lost its actual carried source/library");
    return original_library_ready(configuration,cut->initialized,cut->loaded,error);
}
void application_bots_original_detach(application_bots_original *cut) {
    if(cut) cut->bots=NULL;
}
void application_bots_original_dispose(application_bots_original *cut) {
    if(!cut) return;
    if(cut->bots && cut->bots->original==cut) cut->bots->original=NULL;
    qa_bot_navigation_destroy(cut->map_navigation);
    if(cut->graphs) for(size_t i=0;i<cut->graph_count;++i) qa_navigation_destroy(cut->graphs[i].fresh);
    free(cut->graphs);free(cut->pairs);free(cut);
}

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
                size_t team_length=strlen(saved->admission.team);
                if(team_length>=sizeof(saved->team)) goto invalid;
                memcpy(saved->team,saved->admission.team,team_length+1);
                saved->admission.team=saved->team;
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
               strcmp(actual.character_file,saved->character) || strcmp(actual.name,saved->name) ||
               strcmp(actual.team,saved->team))
                return application_fail(error,QA_ERROR_ARGUMENT,"actual bot settings changed after round preparation");
        }
    }
    cut->begun=true;bots->round_phase=APPLICATION_BOT_ROUND_DETACHED;
    ++bots->calls;bool ok=qa_bots_shutdown(bots->population,true,error) &&
        qa_bots_destroy(bots->population,error);--bots->calls;
    if(!ok) {bots->round_phase=APPLICATION_BOT_ROUND_FAILED;return false;}
    bots->population=NULL;
    if(!qa_bot_catalog_destroy(bots->catalogue,error)) {bots->round_phase=APPLICATION_BOT_ROUND_FAILED;return false;}
    bots->catalogue=NULL;bots->catalogue_ready=false;
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
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next)
        if(!qa_bot_runtime_rebind_round(guest->runtime,&map,error)) goto failed;
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
    if(cut->had_population && !application_bots_catalog_initialize(bots,true,error)) goto failed;
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
        qa_bot_admission admission;
        ok=!seat->retired && qa_actor_id_equal(seat->actor,actor) && seat->navigation &&
            seat->seat==saved->seat && seat->library_client==saved->admission.client &&
            qa_bots_admission_read(bots->population,actor,&admission,error) &&
            admission.client==saved->admission.client && admission.entity==(int32_t)row->source_slot;
        if(!ok && (!error || !error->code))
            application_fail(error,QA_ERROR_ARGUMENT,"round bot was not set up by its real source G_BotConnect");
    } else ok=application_bots_guest_admit(bots->application,actor,error);
    --bots->calls;
    if(!ok) {bots->round_phase=APPLICATION_BOT_ROUND_FAILED;return false;}
    saved->completed=true;return true;
}
bool application_bots_round_client_binding(application_bots *bots,uint32_t source_slot,
    uint32_t *seat_index,uint32_t *library_client,bool *found,qa_error *error) {
    if(!bots || !seat_index || !library_client || !found)
        return application_fail(error,QA_ERROR_ARGUMENT,"bot round source binding requires bounded outputs");
    *found=false;
    application_bots_round *cut=bots->round;if(!cut) return true;
    if(!idle(cut,error) || !cut->bound || cut->resumed || !settled(cut,3,error)) return false;
    for(size_t i=0;i<cut->count;++i) {
        const bot_round_client *saved=cut->clients+i;
        if(saved->source_slot!=source_slot) continue;
        if(!saved->native || saved->completed || saved->seat_index>=bots->capacity)
            return application_fail(error,QA_ERROR_ARGUMENT,"source bot Connect has no pending native round binding");
        *seat_index=saved->seat_index;*library_client=saved->admission.client;*found=true;return true;
    }
    return application_fail(error,QA_ERROR_ARGUMENT,"source bot Connect is absent from the retained round roster");
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
