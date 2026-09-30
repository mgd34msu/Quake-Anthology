#include "bots_private.h"
#include "guest_q3_private.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t random_word(void *opaque) {
    application_bots *bots=opaque;return qa_builtin_random_integer(&bots->application->random);
}
static bool read_file(void *opaque,const qa_script_include *request,qa_script_resource *out,bool *found,qa_error *error) {
    application_bots *bots=opaque;*found=false;
    size_t directory=0;
    if(request->kind==QA_SCRIPT_INCLUDE_QUOTED && request->from_path) {
        const char *slash=strrchr(request->from_path,'/');
        if(slash) directory=(size_t)(slash-request->from_path)+1;
    }
    for(size_t i=0;i<3;++i) {
        if(i==0 && !directory) continue;
        const char *prefix=i==0?request->from_path:i==1?"":"botfiles/";
        size_t prefix_size=i==0?directory:strlen(prefix),requested=strlen(request->requested_path);
        if(prefix_size>SIZE_MAX-requested-1) return application_fail(error,QA_ERROR_MEMORY,"bot script path size overflow");
        size_t length=prefix_size+requested+1;
        char *path=malloc(length);
        if(!path) return application_fail(error,QA_ERROR_MEMORY,"allocating bot script path");
        memcpy(path,prefix,prefix_size);memcpy(path+prefix_size,request->requested_path,requested+1);
        qa_resource *resource=NULL;qa_error local={0};
        bool ok=qa_vfs_acquire(bots->files,path,&resource,NULL,&local);free(path);
        if(!ok) {if(local.code==QA_ERROR_NOT_FOUND) continue;if(error)*error=local;return false;}
        *out=(qa_script_resource){.path=qa_resource_path(resource),.bytes=qa_resource_bytes(resource),.lease=resource};
        *found=true;return true;
    }
    return true;
}
static void release_file(void *,qa_script_resource *);
static bool character_path(application_bots *bots,const char *name,char path[160],qa_error *error) {
    if(name && strlen(name)<=128) {
        size_t length=strlen(name);bool safe=length>0;
        memcpy(path,"bots/",5);
        for(size_t i=0;i<length;++i) {
            unsigned char letter=(unsigned char)name[i];
            if(letter>='A' && letter<='Z') letter=(unsigned char)(letter+'a'-'A');
            if(!((letter>='a' && letter<='z') || (letter>='0' && letter<='9') || letter=='_')) safe=false;
            path[5+i]=(char)letter;
        }
        memcpy(path+5+length,"_c.c",5);
        if(safe) {
            qa_script_include request={.kind=QA_SCRIPT_ROOT,.requested_path=path};
            qa_script_resource resource;bool found;
            if(!read_file(bots,&request,&resource,&found,error)) return false;
            if(found) {release_file(bots,&resource);return true;}
        }
    }
    memcpy(path,"bots/default_c.c",17);return true;
}
static void release_file(void *opaque,qa_script_resource *resource) {
    (void)opaque;qa_resource_release(resource->lease);*resource=(qa_script_resource){0};
}
static int32_t control_value(application_bots *bots,const char *name,int32_t fallback) {
    const qa_cvar_view *value=qa_cvars_find(bots->application->cvars,name);
    return value?value->integer:fallback;
}
static bool register_controls(application_bots *bots,qa_error *error) {
    static const struct {const char *name,*value,*description;} values[]={
        {"bot_thinktime","100","Native bot decision interval in milliseconds"},
        {"bot_pause","0","Pause native bot decisions"},
        {"bot_challenge","0","Enable source challenge aiming"},
        {"bot_fastchat","0","Use fast native bot chat"},
        {"bot_nochat","0","Suppress native bot chat"},
        {"bot_rocketjump","1","Allow native bot weapon jump travel"},
        {"bot_grapple","0","Allow native bot grapple travel"},
        {"bot_report","0","Report native bot decisions"}
    };
    for(size_t i=0;i<sizeof(values)/sizeof(*values);++i)
        if(!qa_cvars_find(bots->application->cvars,values[i].name) &&
           !qa_cvars_register(bots->application->cvars,values[i].name,values[i].value,0,0,values[i].description,error)) return false;
    return true;
}
static bool controls(void *opaque,qa_bot_controls *out,qa_error *error) {
    (void)error;application_bots *bots=opaque;
    bots->controls=(qa_bot_controls){.think_time_ms=control_value(bots,"bot_thinktime",100),
        .paused=control_value(bots,"bot_pause",0)!=0,.challenge=control_value(bots,"bot_challenge",0)!=0,
        .fast_chat=control_value(bots,"bot_fastchat",0)!=0,.no_chat=control_value(bots,"bot_nochat",0)!=0,
        .rocket_jump=control_value(bots,"bot_rocketjump",1)!=0,.grapple=control_value(bots,"bot_grapple",0)!=0,
        .report=control_value(bots,"bot_report",0)!=0};
    *out=bots->controls;return true;
}
static bool think_time(void *opaque,int32_t milliseconds,qa_error *error) {
    application_bots *bots=opaque;
    if(milliseconds<1 || milliseconds>200) return application_fail(error,QA_ERROR_ARGUMENT,"bot think interval is outside 1..200 ms");
    if(!qa_cvars_set_number(bots->application->cvars,"bot_thinktime",(float)milliseconds,error)) return false;
    bots->controls.think_time_ms=milliseconds;return true;
}
static bool pickup_list(void *opaque,int32_t client,const qa_actor_id **out,size_t *count,void **lease,qa_error *error) {
    application_bots *bots=opaque;(void)client;
    if(bots->pickup_borrowed) return application_fail(error,QA_ERROR_ARGUMENT,"bot pickup snapshot is already borrowed");
    qa_builtin_services services=application_builtin_services(bots->application,bots->application->world,bots->application->physics);
    if(!qa_builtin_observations(&services,&bots->pickup_snapshot,error)) return false;
    bots->pickup_borrowed=true;*out=bots->pickup_snapshot.ids;*count=bots->pickup_snapshot.count;*lease=bots;return true;
}
static void pickup_end(void *opaque,void *lease) {
    application_bots *bots=opaque;if(lease==bots) bots->pickup_borrowed=false;
}
static bool pickup(void *opaque,int32_t client,qa_actor_id actor,qa_bot_pickup_goal *out,bool *found,qa_error *error) {
    application_bots *bots=opaque;qa_application *application=bots->application;*found=false;
    qa_actor_id player=application_bot_client_actor(bots,client);
    if(!player.registry) return true;
    qa_pickup_observation observation;bool observed;
    if(!qa_pickups_inspect(application->pickups,actor,player,&observation,&observed,error)) return false;
    if(!observed || !observation.available || observation.utility<=0) return true;
    qa_body_state body;
    if(!qa_world_body_read(application->world,actor,&body,error)) return false;
    const char *name=qa_strings_cstr(qa_session_strings(application->session),observation.item);
    *out=(qa_bot_pickup_goal){.actor=actor,.entity=(int32_t)actor.slot,.origin=body.origin,
        .bounds=body.bounds,.name=name?name:"source pickup",.utility=observation.utility};
    *found=qa_actors_get(qa_session_actors(application->session),actor)!=NULL;return true;
}
static bool owns_item(void *opaque,int32_t client,int32_t entity,bool *out,qa_error *error) {
    (void)opaque;(void)client;(void)entity;(void)error;
    /* Actual source observers own native pickup utility, including Q3 items.
     * Authoring metadata remains available for roam/camp/location goals. */
    *out=true;return true;
}
static int32_t entity_number(void *opaque,qa_actor_id actor) {
    application_bots *bots=opaque;
    return actor.slot<=INT32_MAX && qa_actors_get(qa_session_actors(bots->application->session),actor)?(int32_t)actor.slot:-1;
}
static bool guest_bot_provider(const application_provider *provider) {
    uint64_t presentation=QA_ROLE_BIT(QA_ROLE_HUD)|QA_ROLE_BIT(QA_ROLE_MENU)|
        QA_ROLE_BIT(QA_ROLE_EFFECTS)|QA_ROLE_BIT(QA_ROLE_AUDIO)|QA_ROLE_BIT(QA_ROLE_MUSIC);
    return provider->attached && provider->constructed && provider->product &&
        provider->product->family==QA_GAME_Q3 &&
        (provider->kind==APPLICATION_PROVIDER_QVM || provider->kind==APPLICATION_PROVIDER_NATIVE) &&
        (provider->launch->roles & ~presentation)!=0;
}
qa_actor_id application_bot_client_actor(application_bots *bots,int32_t client) {
    if(client<0) return (qa_actor_id){0};
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next) {
        if((uint32_t)client<guest->client_base || (uint32_t)client-guest->client_base>=64) continue;
        struct application_q3_guest *engine=q3g_engine(guest->provider);
        qa_actor_id actor=engine?engine->clients[(uint32_t)client-guest->client_base].actor:(qa_actor_id){0};
        return qa_actors_get(qa_session_actors(bots->application->session),actor)?actor:(qa_actor_id){0};
    }
    return application_bot_actor(bots,client);
}
static bool bot_command(void *opaque,int32_t client,const char *text,qa_error *error) {
    application_bots *bots=opaque;
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next)
        if(client>=0 && (uint32_t)client>=guest->client_base && (uint32_t)client-guest->client_base<64)
            return application_q3_guest_client_command(guest->provider,(uint32_t)client-guest->client_base,text,error);
    qa_actor_id actor=application_bot_client_actor(bots,client);
    for(uint32_t i=0;i<bots->capacity;++i)
        if(!bots->seats[i].retired && qa_actor_id_equal(bots->seats[i].actor,actor))
            return qa_application_actor_command(bots->application,actor,text,error);
    return application_fail(error,QA_ERROR_NOT_FOUND,"bot command client has no current roster owner");
}
bool application_bots_guest_bind(qa_application *application,application_provider *provider,
                                  qa_q3_host *host,qa_error *error) {
    application_bots *bots=application->bots;
    if(!bots) return application_fail(error,QA_ERROR_NOT_FOUND,"guest shared bot library was not prepared");
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next)
        if(guest->provider==provider)
            return qa_q3_host_attach_bots(host,bots->runtime,guest->client_base,guest->entity_base,true,error);
    return application_fail(error,QA_ERROR_NOT_FOUND,"guest bot library namespace was not admitted");
}
static bool close_bots(application_bots *bots,qa_error *error) {
    if(!bots) return true;
    if(bots->calls || bots->arsenal_leases || bots->pickup_borrowed || bots->mover_borrowed ||
       !qa_bots_can_destroy(bots->population) || !qa_bot_runtime_can_destroy(bots->runtime))
        return application_fail(error,QA_ERROR_ARGUMENT,"application bot owners are executing a callback");
    if(!qa_bots_destroy(bots->population,error)) return false;
    bots->population=NULL;
    if(!qa_bot_runtime_destroy(bots->runtime,error)) return false;
    bots->runtime=NULL;
    qa_bot_navigation_destroy(bots->map_navigation);
    if(bots->seats) for(uint32_t i=0;i<bots->capacity;++i) qa_bot_navigation_destroy(bots->seats[i].navigation);
    while(bots->targets) {application_bot_target *target=bots->targets;bots->targets=target->next;
        qa_bot_navigation_destroy(target->navigation);free(target);}
    while(bots->graphs) {application_bot_graph *graph=bots->graphs;bots->graphs=graph->next;
        qa_navigation_destroy(graph->navigation);qa_nav_graph_release(graph->graph);free(graph);}
    while(bots->guests) {application_bot_guest *guest=bots->guests;bots->guests=guest->next;free(guest);}
    qa_builtin_snapshot_free(&bots->pickup_snapshot);qa_entities_free(&bots->entities);
    qa_resource_release(bots->map_resource);qa_vfs_destroy(bots->files);
    free(bots->train_stops);free(bots->seats);free(bots);return true;
}
bool application_bots_can_destroy(const qa_application *application) {
    application_bots *bots=application?application->bots:NULL;
    return !bots || (!bots->calls && !bots->arsenal_leases && !bots->pickup_borrowed && !bots->mover_borrowed &&
        qa_bots_can_destroy(bots->population) && qa_bot_runtime_can_destroy(bots->runtime));
}
bool application_bots_destroy(qa_application *application,qa_error *error) {
    if(!application || !application->bots) return true;
    if(!close_bots(application->bots,error)) return false;
    application->bots=NULL;return true;
}
bool application_bots_actor_released(qa_application *application,qa_actor_record actor,qa_error *error) {
    application_bots *bots=application->bots;if(!bots) return true;
    for(uint32_t i=0;i<bots->capacity;++i)
        if(qa_actor_id_equal(bots->seats[i].actor,actor.id)) bots->seats[i].retired=true;
    return !bots->population || qa_bots_actor_released(bots->population,&actor,error);
}
bool application_bots_frame(qa_application *application,qa_error *error) {
    application_bots *bots=application->bots;if(!bots) return true;
    if(bots->calls) return application_fail(error,QA_ERROR_ARGUMENT,"application bot frame reentered");
    application_bot_target **link=&bots->targets;
    while(*link) {
        application_bot_target *target=*link;
        if(qa_actors_get(qa_session_actors(application->session),target->actor)) {link=&target->next;continue;}
        *link=target->next;qa_bot_navigation_destroy(target->navigation);free(target);
    }
    ++bots->calls;
    bool ok=!bots->population || qa_bots_frame(bots->population,(int32_t)(uint32_t)(qa_session_elapsed(application->session)/1000000),error);
    --bots->calls;return ok;
}
bool application_bots_prepare(qa_application *application,const qa_launch_choices *choices,
                               const qa_bsp_view *geometry,const qa_entities *entities,qa_error *error) {
    (void)entities;uint32_t capacity=0;size_t count=0;size_t guest_count=0;
    if(choices->seat_count>INT32_MAX)
        return application_fail(error,QA_ERROR_ARGUMENT,"native bot roster exceeds source handle range");
    for(size_t i=0;i<choices->seat_count;++i) if(choices->seats[i].bot) {++count;capacity=(uint32_t)i+1;}
    for(size_t i=0;i<application->provider_count;++i) {
        application_provider *provider=application->providers[i];
        if(guest_bot_provider(provider)) ++guest_count;
    }
    if(!count && !guest_count) return true;
    if(application->bots) return application->bots->map_resource==application->map_resource ||
        application_fail(error,QA_ERROR_ARGUMENT,"retire the previous shared bot library before map preparation");
    if(count>=UINT32_MAX)
        return application_fail(error,QA_ERROR_ARGUMENT,"native bot roster exceeds source handle range");
    application_bots *bots=calloc(1,sizeof(*bots));
    if(!bots) return application_fail(error,QA_ERROR_MEMORY,"allocating application native bot owner");
    bots->application=application;bots->capacity=capacity;bots->geometry=*geometry;
    uint32_t actor_capacity=qa_actors_capacity(qa_session_actors(application->session));
    if(actor_capacity>=INT32_MAX || guest_count>(INT32_MAX-actor_capacity)/1024 ||
       count+guest_count*64>=INT32_MAX) {
        application_fail(error,QA_ERROR_ARGUMENT,"shared bot namespaces exceed signed source range");goto fail;
    }
    uint32_t ordinal=0;
    for(size_t i=0;i<application->provider_count;++i) {
        application_provider *provider=application->providers[i];
        if(!guest_bot_provider(provider)) continue;
        application_bot_guest *guest=calloc(1,sizeof(*guest));
        if(!guest) {application_fail(error,QA_ERROR_MEMORY,"allocating guest bot namespace");goto fail;}
        *guest=(application_bot_guest){.provider=provider,.client_base=actor_capacity+ordinal*64,
            .entity_base=actor_capacity+ordinal*1024,.next=bots->guests};
        bots->guests=guest;++ordinal;
    }
    bots->map_resource=application->map_resource;qa_resource_retain(bots->map_resource);
    bots->controls=(qa_bot_controls){.think_time_ms=100,.rocket_jump=true};
    if(!register_controls(bots,error)) goto fail;
    bots->seats=capacity?calloc(capacity,sizeof(*bots->seats)):NULL;
    if(capacity && !bots->seats) {application_fail(error,QA_ERROR_MEMORY,"allocating bot roster navigation bindings");goto fail;}
    const qa_launch_snapshot *snapshot=application->routing_snapshot?application->routing_snapshot:qa_application_launch(application);
    bots->files=qa_vfs_clone(qa_launch_snapshot_mounts(snapshot),error);
    if(!bots->files) goto fail;
    qa_resource *probe=NULL;qa_error local={0};
    bool have_assets=qa_vfs_acquire(bots->files,"botfiles/bots/default_c.c",&probe,NULL,&local);
    qa_resource_release(probe);
    if(!have_assets) {
        if(local.code!=QA_ERROR_NOT_FOUND) {if(error)*error=local;goto fail;}
        bool opened=false;
        for(size_t i=0;i<qa_catalog_count(application->catalog);++i) {
            const qa_product *product=qa_catalog_at(application->catalog,i);
            if(product->family!=QA_GAME_Q3 || product->availability!=QA_CONTENT_INSTALLED) continue;
            qa_vfs *files=NULL;if(!qa_catalog_open(application->catalog,product->id,&files,error)) goto fail;
            probe=NULL;local=(qa_error){0};have_assets=qa_vfs_acquire(files,"botfiles/bots/default_c.c",&probe,NULL,&local);
            qa_resource_release(probe);
            if(have_assets) {qa_vfs_destroy(bots->files);bots->files=files;opened=true;break;}
            qa_vfs_destroy(files);if(local.code!=QA_ERROR_NOT_FOUND) {if(error)*error=local;goto fail;}
        }
        if(!opened && count) {application_fail(error,QA_ERROR_NOT_FOUND,"native bots require installed bot personality resources");goto fail;}
    }
    if(!qa_entities_parse(geometry->lumps[QA_BSP_ENTITIES].bytes,
            geometry->family==QA_BSP_Q3?QA_ENTITY_Q3:QA_ENTITY_Q1,&bots->entities,error)) goto fail;
    if(!application_bot_navigation_prepare(bots,error)) goto fail;
    qa_bot_runtime_options options={.library={.scripts={.context=bots,.read=read_file,.release=release_file},
        .preprocessor={.include_path="botfiles",.builtins=true}},.maximum_states=(uint32_t)(count+guest_count*64)+1,
        .minimum_clients=actor_capacity+(uint32_t)guest_count*64,
        .observations=guest_count?QA_BOT_OBSERVATION_MODULE:QA_BOT_OBSERVATION_NATIVE};
    qa_bot_random_source random={bots,random_word};
    qa_bot_runtime_services services={.context=bots,.random=random,.navigation=application_bot_navigation,.command=bot_command,
        .goals={.context=bots,.navigation=application_bot_navigation,.pickups=pickup_list,.pickups_end=pickup_end,
            .pickup=pickup,.owns_item=owns_item},
        .movement={.context=bots,.navigation=application_bot_navigation,.actor=application_bot_actor,
            .entity_number=entity_number,.model=application_bot_travel_model,
            .travel_weapon=application_bot_travel_weapon,.grapple_state=application_bot_grapple_state,.random=random}};
    if(!qa_bot_runtime_create(&options,&services,&bots->runtime,error)) goto fail;
    char clients[32];snprintf(clients,sizeof(clients),"%u",actor_capacity);
    if(!qa_bot_library_variable_set(qa_bot_runtime_library(bots->runtime),"maxclients",clients,error) ||
       !qa_bot_library_variable_set(qa_bot_runtime_library(bots->runtime),"maxentities",clients,error)) goto fail;
    const char *name=qa_strings_cstr(qa_session_strings(application->session),application->current_map);
    qa_bot_runtime_map map={.name=name,.entities=&bots->entities,.navigation=application_bot_navigation(bots,-1)};
    if(!qa_bot_runtime_attach_map(bots->runtime,&map,error)) goto fail;
    application->bots=bots;return true;
fail:
    {qa_error cleanup={0};if(!close_bots(bots,&cleanup)) {if(error && !error->code)*error=cleanup;}}
    return false;
}
qa_bot_runtime *application_bots_runtime(qa_application *application) {
    return application && application->bots?application->bots->runtime:NULL;
}
bool application_bots_publish(qa_application *application,const qa_launch_choices *choices,
                               const qa_bsp_view *geometry,const qa_entities *entities,qa_error *error) {
    if(!application_bots_prepare(application,choices,geometry,entities,error)) return false;
    application_bots *bots=application->bots;bool native=false;
    for(size_t i=0;i<choices->seat_count;++i) native=native || choices->seats[i].bot;
    if(!native) return true;
    if(!bots || bots->population) return application_fail(error,QA_ERROR_ARGUMENT,"native bot population was already published");
    if(!qa_bot_runtime_initialized(bots->runtime)) {
        int32_t result;
        if(!qa_bot_runtime_setup(bots->runtime,&result,error)) return false;
        if(result) return application_fail(error,QA_ERROR_NOT_FOUND,"native shared bot library setup rejected required resources");
    }
    const char *name=qa_strings_cstr(qa_session_strings(application->session),application->current_map);
    if(!qa_bot_runtime_loaded(bots->runtime) && !qa_bot_runtime_load_map(bots->runtime,name,error)) return false;
    for(uint32_t i=0;i<bots->capacity;++i) {
        bots->seats[i].seat=choices->seats[i].id;
        if(!choices->seats[i].bot) continue;
        if(!qa_application_player_actor(application,choices->seats[i].id,&bots->seats[i].actor) ||
           !application_bot_navigation_bind(bots,&bots->seats[i],error)) return false;
        bots->seats[i].last_command_ns=qa_session_elapsed(application->session);
    }
    if(!qa_bot_runtime_weapon_allocate(bots->runtime,&bots->metadata_weapon,error) || !bots->metadata_weapon) return false;
    qa_bot_services ai={.context=bots,.shared=application_builtin_services(application,application->world,application->physics),
        .modes=application->modes,.player=application_bot_player,.entity=application_bot_entity,
        .arsenal=application_bot_arsenal,.arsenal_end=application_bot_arsenal_end,.submit=application_bot_submit,
        .controls=controls,.set_think_time=think_time,.activation=application_bot_activation,
        .predict_motion=application_bot_predict_motion};
    if(!qa_bots_create(bots->runtime,&ai,&bots->population,error)) return false;
    for(uint32_t i=0;i<bots->capacity;++i) {
        if(!choices->seats[i].bot) continue;
        char character[160];
        if(!character_path(bots,choices->seats[i].name,character,error)) return false;
        application_provider *arsenal=application_provider_for(application,bots->seats[i].actor,QA_ROLE_ARSENAL,NULL);
        const qa_product *product=arsenal?qa_catalog_product(application->catalog,arsenal->launch->selection.product):NULL;
        qa_bot_admission admission={.actor=bots->seats[i].actor,.client=bots->seats[i].actor.slot,.entity=(int32_t)bots->seats[i].actor.slot,
            .character_file=character,.name=choices->seats[i].name?choices->seats[i].name:"bot",
            .skill=choices->seats[i].bot_skill,.mode=application->primary_mode,
            .team_arena=product && product->family==QA_GAME_Q3 && !strcmp(product->campaign,"missionpack")};
        if(!qa_bots_admit(bots->population,&admission,error)) return false;
    }
    return true;
}
