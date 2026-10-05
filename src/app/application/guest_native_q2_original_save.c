#include "guest_native_q2_original_save.h"
#include "guest_native_q2_private.h"
#include "native_q2_console.h"
#include "map_travel_private.h"
#include "portals.h"
#include "network_q2_private.h"
#include "qa/application_q2_save.h"
#include "qa/application_startup_prepare.h"
#include "qa/game_q2_original_save.h"
#include "qa/game_q2_source.h"
#include "qa/application_save_policy.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

struct application_q2_original_save {
    qa_q2_save_data *save;
    qa_travel_route route;
    qa_product_id product;
    bool prepared, game_read, level_read, failed;
};

static bool copy_bytes(qa_bytes source, qa_buffer *out, qa_error *error)
{
    if (source.size && !source.data)
        return application_fail(error,QA_ERROR_FORMAT,"Original Q2 file has no byte owner");
    out->data=source.size?malloc(source.size):NULL; out->size=source.size;
    if (source.size && !out->data)
        return application_fail(error,QA_ERROR_MEMORY,"Retaining original Q2 file");
    if (source.size) memcpy(out->data,source.data,source.size);
    return true;
}

static qa_q2_save_level *copy_level(const qa_q2_save_level *source, qa_error *error)
{
    qa_q2_save_level *out=malloc(sizeof(*out));
    if (!out) { application_fail(error,QA_ERROR_MEMORY,"Retaining original Q2 level"); return NULL; }
    *out=*source; out->game=(qa_buffer){0};
    if (!copy_bytes((qa_bytes){source->game.data,source->game.size},&out->game,error)) { free(out); return NULL; }
    return out;
}

static qa_q2_save_data *copy_save(const qa_q2_save_data *source, qa_error *error)
{
    qa_q2_save_data *out=calloc(1,sizeof(*out));
    if (!out) { application_fail(error,QA_ERROR_MEMORY,"Retaining original Q2 import"); return NULL; }
    out->server=source->server; out->server.cvars=NULL;
    bool ok=source->server.cvar_count<=SIZE_MAX/sizeof(*out->server.cvars) &&
        source->level_count<=SIZE_MAX/sizeof(*out->levels);
    if (!ok) application_fail(error,QA_ERROR_FORMAT,"Original Q2 roster exceeds memory");
    if (ok && source->server.cvar_count) {
        out->server.cvars=malloc(source->server.cvar_count*sizeof(*out->server.cvars));
        ok=out->server.cvars!=NULL;
        if (ok) memcpy(out->server.cvars,source->server.cvars,source->server.cvar_count*sizeof(*out->server.cvars));
    }
    if (ok) ok=copy_bytes((qa_bytes){source->game.data,source->game.size},&out->game,error);
    if (ok && source->level_count) { out->levels=calloc(source->level_count,sizeof(*out->levels)); ok=out->levels!=NULL; }
    for (size_t i=0;ok && i<source->level_count;++i) {
        out->levels[i]=source->levels[i]; out->levels[i].game=(qa_buffer){0}; ++out->level_count;
        ok=copy_bytes((qa_bytes){source->levels[i].game.data,source->levels[i].game.size},&out->levels[i].game,error);
    }
    if (!ok) {
        if (error && error->code==QA_OK) application_fail(error,QA_ERROR_MEMORY,"Retaining original Q2 save owners");
        qa_q2_save_destroy(out); return NULL;
    }
    return out;
}

static bool fixed_text(char *out,size_t size,const char *text,qa_error *error)
{
    size_t length=strlen(text?text:"");
    if (length>=size) return application_fail(error,QA_ERROR_UNSUPPORTED,
        "Q2 original engine field exceeds its Source extent");
    memset(out,0,size); memcpy(out,text?text:"",length);
    return true;
}

static bool capture_configs(qa_q2_save_level *level,const qa_q2_config_entry *configs,
    size_t count,qa_error *error)
{
    if (count && !configs) return application_fail(error,QA_ERROR_ARGUMENT,
        "Original Q2 capture requires the actual configstring owner");
    char *table=(char *)level->configstrings;
    for (size_t i=0;i<count;++i) {
        uint32_t index=configs[i].index;
        const char *text=configs[i].value;
        if (index>=QA_Q2_SAVE_CONFIGSTRINGS || !text ||
            (i && configs[i-1].index>=index))
            return application_fail(error,QA_ERROR_FORMAT,"Original Q2 config rows leave their Source table");
        size_t size=strlen(text)+1, offset=(size_t)index*QA_Q2_SAVE_CONFIGSTRING_BYTES;
        if (size>sizeof(level->configstrings)-offset)
            return application_fail(error,QA_ERROR_UNSUPPORTED,"Original Q2 configstring exceeds its physical table");
        /* Source strcpy permits statusbar to occupy following reserved rows.
         * Empty independently owned rows must not erase that actual span. */
        if (*text) memcpy(table+offset,text,size);
    }
    return true;
}

static bool capture_portals(qa_application *app,qa_q2_save_level *level,qa_error *error)
{
    qa_collision_portal_checkpoint portals={0};
    if (!qa_collision_capture_portals(app->geometry,&portals,error)) return false;
    bool ok=portals.family==QA_COLLISION_Q2 ||
        application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 capture requires actual Q2 area portals");
    for (size_t i=0;ok && i<portals.portal_count;++i) {
        const qa_collision_saved_portal *row=portals.portals+i;
        if (row->portal>=QA_Q2_SAVE_AREA_PORTALS)
            ok=application_fail(error,QA_ERROR_UNSUPPORTED,"Q2 area portal exceeds the original engine file");
        else level->portal_open[row->portal]=row->primary || row->contributions!=0;
    }
    qa_collision_portal_checkpoint_free(&portals); return ok;
}

static bool capture_files(application_provider *source,qa_save_purpose purpose,
    qa_q2_save_level *engine_level,qa_buffer *game,qa_buffer *level,qa_error *error)
{
    bool autosave=purpose==QA_SAVE_LEVEL_ENTRY, transition=purpose==QA_SAVE_TRANSITION;
    if (source->kind==APPLICATION_PROVIDER_Q2)
        return qa_q2_game_original_capture(source->state.q2,autosave,transition,engine_level,game,level,error);
    qa_native_checkpoint snapshot={0};
    bool ok=qa_native_checkpoint_capture(qa_native_host_instance(source->state.native.host),
        (qa_native_checkpoint_request){.game=!transition,.level=!autosave,
            .autosave=autosave,.transition=transition},&snapshot,error);
    if (ok) {
        *game=snapshot.game; snapshot.game=(qa_buffer){0};
        *level=snapshot.level; snapshot.level=(qa_buffer){0};
    }
    qa_native_checkpoint_free(&snapshot); return ok;
}

static bool capture_server(qa_application *app,application_provider *source,
    qa_save_purpose purpose,const qa_q2_config_entry *configs,size_t count,
    qa_q2_save_server *out,qa_error *error)
{
    qa_cvars *cvars=source->kind==APPLICATION_PROVIDER_Q2?
        application_native_q2_console_registry(source):source->state.native.q2_engine->cvars;
    const qa_launch_choices *choices=qa_launch_snapshot_choices(qa_application_launch(app));
    const char *map=qa_strings_cstr(qa_session_strings(app->session),app->current_map);
    const char *spawn=choices->world.spawn_point?choices->world.spawn_point:"";
    const char *start=choices->world.start_command;
    const char *next=qa_strings_cstr(qa_session_strings(app->session),qa_application_nextserver(app));
    size_t tail=next?strlen(next):0;
    bool ok=map!=NULL;
    if (ok && tail && (tail<10 || strncmp(next,"gamemap \"",9) || next[tail-1]!='\"'))
        ok=application_fail(error,QA_ERROR_UNSUPPORTED,"Original Q2 save has a non-Source nextserver command");
    if (ok) {
        int length=snprintf(out->map_command,sizeof(out->map_command),"%s%s%s%s%s%.*s",
            start && *start=='*'?"*":"",map,*spawn?"$":"",spawn,tail?"+":"",
            (int)(tail?tail-10:0),tail?next+9:"");
        if (length<0 || (size_t)length>=sizeof(out->map_command))
            ok=application_fail(error,QA_ERROR_UNSUPPORTED,"Q2 original map chain exceeds its Source extent");
    }
    const char *name=map;
    for (size_t i=0;i<count;++i) if (!configs[i].index) {name=configs[i].value;break;}
    if (ok && purpose==QA_SAVE_LEVEL_ENTRY)
        (void)snprintf(out->comment,sizeof(out->comment),"ENTERING %s",name?name:"");
    else if (ok) {
        time_t now=time(NULL); struct tm *local=localtime(&now);
        if (!local) ok=application_fail(error,QA_ERROR_IO,"Reading original Q2 save time");
        else (void)snprintf(out->comment,sizeof(out->comment),"%2i:%02i %2i/%2i  %s",
            local->tm_hour,local->tm_min,local->tm_mon+1,local->tm_mday,name?name:"");
    }
    for (const qa_cvar_view *row=ok?qa_cvars_next(cvars,NULL):NULL;row;row=qa_cvars_next(cvars,row))
        if ((row->flags&QA_Q2_CVAR_LATCH) && strlen(row->name)<QA_Q2_SAVE_CVAR_BYTES-1 &&
            strlen(row->value)<QA_Q2_SAVE_CVAR_BYTES-1) ++out->cvar_count;
    if (ok && out->cvar_count) {
        out->cvars=calloc(out->cvar_count,sizeof(*out->cvars));
        if (!out->cvars) ok=application_fail(error,QA_ERROR_MEMORY,"Retaining original Q2 latched cvars");
    }
    size_t at=0;
    for (const qa_cvar_view *row=ok?qa_cvars_next(cvars,NULL):NULL;row;row=qa_cvars_next(cvars,row))
        if ((row->flags&QA_Q2_CVAR_LATCH) && strlen(row->name)<QA_Q2_SAVE_CVAR_BYTES-1 &&
            strlen(row->value)<QA_Q2_SAVE_CVAR_BYTES-1) {
            memcpy(out->cvars[at].name,row->name,strlen(row->name));
            memcpy(out->cvars[at++].value,row->value,strlen(row->value));
        }
    return ok;
}

bool qa_application_q2_save_capture(qa_application *app,qa_save_purpose purpose,
    const qa_q2_config_entry *configs,size_t count,qa_q2_save_data **out,qa_error *error)
{
    const qa_product *product=qa_application_save_original_product(app);
    application_provider *source=app?application_world_provider(app,QA_ROLE_ENTITIES,""):NULL;
    if (!out || (count && !configs) || !product || product->family!=QA_GAME_Q2 || product->edition!=QA_EDITION_CLASSIC ||
        !source || (source->kind!=APPLICATION_PROVIDER_Q2 &&
            !(source->kind==APPLICATION_PROVIDER_NATIVE && source->state.native.q2_engine &&
                source->state.native.q2_engine->profile==QA_NATIVE_Q2_GAME_API3)) ||
        app->operation!=APPLICATION_IDLE || app->state!=QA_APPLICATION_RUNNING ||
        !app->world || !app->geometry || !application_guests_idle(app) ||
        !qa_world_idle(app->world) || !qa_session_safe(app->session) ||
        (purpose!=QA_SAVE_MANUAL && purpose!=QA_SAVE_LEVEL_ENTRY && purpose!=QA_SAVE_TRANSITION))
        return application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 capture requires its actual idle classic GAME");
    qa_q2_save_data *save=calloc(1,sizeof(*save));
    if (!save) return application_fail(error,QA_ERROR_MEMORY,"Retaining original Q2 save files");
    qa_campaign_unit_checkpoint visits={0}; qa_buffer level={0};
    bool ok=purpose==QA_SAVE_TRANSITION ||
        (qa_campaign_unit_capture(app->campaign_unit,&visits,error) &&
         capture_server(app,source,purpose,configs,count,&save->server,error));
    size_t capacity=0;
    if (ok && visits.count>=SIZE_MAX/sizeof(*save->levels))
        ok=application_fail(error,QA_ERROR_MEMORY,"Q2 original visited level count exceeds memory");
    if (ok) capacity=visits.count+1;
    if (ok && capacity) {
        save->levels=calloc(capacity,sizeof(*save->levels));
        if (!save->levels) ok=application_fail(error,QA_ERROR_MEMORY,"Retaining original Q2 visited levels");
    }
    if (ok) {
        qa_q2_save_level *current=save->levels;
        const char *name=qa_strings_cstr(qa_session_strings(app->session),app->current_map);
        ok=name && fixed_text(current->name,sizeof(current->name),name,error) &&
            capture_configs(current,configs,count,error) &&
            (purpose==QA_SAVE_LEVEL_ENTRY || capture_portals(app,current,error)) &&
            capture_files(source,purpose,current,&save->game,&level,error);
        if (ok && ((purpose!=QA_SAVE_TRANSITION && (!save->game.data || !save->game.size)) ||
            (purpose!=QA_SAVE_LEVEL_ENTRY && (!level.data || !level.size))))
            ok=application_fail(error,QA_ERROR_FORMAT,"Original Q2 GAME did not produce its requested save file");
        if (ok && purpose!=QA_SAVE_LEVEL_ENTRY) {
            current->game=level; level=(qa_buffer){0}; ++save->level_count;
        }
    }
    for (size_t i=0;ok && i<visits.count;++i) {
        const qa_q2_save_level *value=qa_campaign_world_q2(visits.worlds[i]);
        if (!value) {ok=application_fail(error,QA_ERROR_UNSUPPORTED,
            "Original Q2 directory requires original departed LEVEL files");break;}
        qa_q2_save_level *target=save->levels+save->level_count;
        *target=*value; target->game=(qa_buffer){0}; ++save->level_count;
        ok=copy_bytes((qa_bytes){value->game.data,value->game.size},&target->game,error);
    }
    qa_buffer_free(&level); qa_campaign_unit_checkpoint_free(&visits);
    if (!ok) {qa_q2_save_destroy(save);return false;}
    *out=save; return true;
}

void application_q2_original_dispose(qa_application *app)
{
    struct application_q2_original_save *stage=app?app->q2_original_save:NULL;
    if (!stage) return;
    qa_q2_save_destroy(stage->save); qa_travel_route_free(&stage->route); free(stage);
    app->q2_original_save=NULL;
}

bool application_q2_original_source(const application_provider *provider)
{
    const struct application_q2_original_save *stage=provider && provider->application?
        provider->application->q2_original_save:NULL;
    return provider && provider->application && provider->launch && provider->product &&
        provider->product->family==QA_GAME_Q2 && provider->product->edition==QA_EDITION_CLASSIC &&
        application_world_provider(provider->application,QA_ROLE_ENTITIES,"")==provider &&
        ((stage && stage->product==provider->product->id) ||
         application_campaign_q2_level(provider->application)) &&
        !strcmp(provider->launch->selection.instance,"native:primary");
}

static const qa_q2_save_level *current_level(const struct application_q2_original_save *stage)
{
    const char *map=stage->route.targets[0].name;
    for (size_t i=0;i<stage->save->level_count;++i)
        if (!strcmp(map,stage->save->levels[i].name)) return stage->save->levels+i;
    return NULL;
}

bool application_q2_original_prepare(application_provider *provider, qa_error *error)
{
    if (!application_q2_original_source(provider)) return true;
    struct application_q2_original_save *stage=provider->application->q2_original_save;
    if (!stage) return true;
    if (stage->failed) return application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 construction has failed");
    if (stage->prepared) return true;
    qa_cvars *cvars=provider->kind==APPLICATION_PROVIDER_Q2?application_native_q2_console_registry(provider):
        provider->kind==APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine?
            provider->state.native.q2_engine->cvars:NULL;
    if (!cvars) return application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 import has no actual GAME registry");
    for (size_t i=0;i<stage->save->server.cvar_count;++i) {
        const qa_q2_save_cvar *row=stage->save->server.cvars+i;
        if (!qa_cvars_set(cvars,row->name,row->value,true,error)) { stage->failed=true; return false; }
    }
    stage->prepared=true;
    return true;
}

static bool native_file(application_provider *provider, qa_bytes bytes,
    qa_native_restore_part part, qa_error *error)
{
    struct application_native_q2 *engine=provider->state.native.q2_engine;
    qa_native_module_info module=qa_native_module_describe(provider->state.native.module);
    qa_native_checkpoint value={.kind=QA_NATIVE_CHECKPOINT_Q2_CLASSIC,
        .profile=module.profile,.q3_role=QA_QVM_GAME,.image=module.image,
        .has_declaration=engine->declaration!=NULL};
    if (engine->declaration) value.declaration=*qa_native_declaration_digest(engine->declaration);
    qa_buffer file={(uint8_t *)bytes.data,bytes.size};
    if (part==QA_NATIVE_RESTORE_GAME) { value.game=file; value.has_game=true; }
    else { value.level=file; value.has_level=true; }
    return qa_native_checkpoint_restore(qa_native_host_instance(provider->state.native.host),&value,part,error);
}

bool application_q2_original_game(application_provider *provider, qa_error *error)
{
    if (!application_q2_original_source(provider)) return true;
    struct application_q2_original_save *stage=provider->application->q2_original_save;
    if (!stage) return true;
    if (!stage->prepared || stage->game_read || stage->failed)
        return application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 ReadGame lost its constructor boundary");
    qa_bytes bytes={stage->save->game.data,stage->save->game.size};
    bool ok=provider->kind==APPLICATION_PROVIDER_Q2?
        qa_q2_game_original_read_game(provider->state.q2,bytes,error):native_file(provider,bytes,QA_NATIVE_RESTORE_GAME,error);
    stage->game_read=ok; stage->failed=!ok;
    return ok;
}

static bool restore_engine_level(application_provider *provider,const qa_q2_save_level *level,qa_error *error)
{
    qa_application *app=provider->application;
    if (provider->kind==APPLICATION_PROVIDER_NATIVE) {
        struct application_native_q2 *engine=provider->state.native.q2_engine;
        if (!engine || engine->profile!=QA_NATIVE_Q2_GAME_API3 || engine->configstring_count!=QA_Q2_SAVE_CONFIGSTRINGS)
            return application_fail(error,QA_ERROR_FORMAT,"Original Q2 level differs from its real engine table");
        const char *table=(const char *)level->configstrings;
        for (size_t i=0;i<QA_Q2_SAVE_CONFIGSTRINGS;++i) {
            const char *text=table+i*QA_Q2_SAVE_CONFIGSTRING_BYTES;
            size_t remaining=sizeof(level->configstrings)-i*QA_Q2_SAVE_CONFIGSTRING_BYTES;
            const char *end=memchr(text,0,remaining);
            if (!end) return application_fail(error,QA_ERROR_FORMAT,"Original Q2 configstring has no table terminator");
            size_t length=(size_t)(end-text);
            char *copy=malloc(length+1);
            if (!copy) return application_fail(error,QA_ERROR_MEMORY,"Restoring original Q2 configstring");
            memcpy(copy,text,length+1); free(engine->configstrings[i]); engine->configstrings[i]=copy;
        }
    }
    qa_collision_portal_checkpoint portals={0};
    if (provider->kind==APPLICATION_PROVIDER_Q2 &&
        !application_portals_close(app,provider->owner,error)) return false;
    if (!qa_collision_capture_portals(app->geometry,&portals,error)) return false;
    bool ok=portals.family==QA_COLLISION_Q2 ||
        application_fail(error,QA_ERROR_FORMAT,"Original Q2 level has no Q2 collision owner");
    for (size_t i=0;ok && i<portals.portal_count;++i) {
        uint32_t index=portals.portals[i].portal;
        if (index>=QA_Q2_SAVE_AREA_PORTALS) ok=application_fail(error,QA_ERROR_FORMAT,"Original Q2 area portal exceeds its Source table");
        else { portals.portals[i].primary=provider->kind==APPLICATION_PROVIDER_NATIVE && level->portal_open[index]!=0;
            portals.portals[i].contributions=0; }
    }
    if (ok) ok=qa_collision_restore_portals(app->geometry,&portals,error);
    if (provider->kind==APPLICATION_PROVIDER_Q2)
        for (size_t i=0;ok && i<portals.portal_count;++i)
            ok=application_portal_q2(provider,portals.portals[i].portal,
                level->portal_open[portals.portals[i].portal]!=0,error);
    qa_collision_portal_checkpoint_free(&portals);
    return ok;
}

bool application_q2_original_level(application_provider *provider, qa_error *error)
{
    if (!application_q2_original_source(provider)) return true;
    struct application_q2_original_save *stage=provider->application->q2_original_save;
    if (stage && (!stage->game_read || stage->level_read || stage->failed))
        return application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 ReadLevel lost its real Spawn boundary");
    const qa_q2_save_level *level=stage?current_level(stage):application_campaign_q2_level(provider->application);
    bool ok=true;
    if (level) {
        qa_bytes bytes={level->game.data,level->game.size};
        ok=restore_engine_level(provider,level,error) && (provider->kind==APPLICATION_PROVIDER_Q2?
            qa_q2_game_original_read_level(provider->state.q2,bytes,level,error):native_file(provider,bytes,QA_NATIVE_RESTORE_LEVEL,error));
    }
    if (stage) { stage->level_read=ok; stage->failed=!ok; }
    return ok;
}

bool application_q2_original_player(application_provider *provider,uint32_t slot,
    qa_actor_id actor,bool *restored,qa_error *error)
{
    *restored=false;
    if (!application_q2_original_source(provider)) return true;
    struct application_q2_original_save *stage=provider->application->q2_original_save;
    if (!stage) return true;
    if (provider->kind!=APPLICATION_PROVIDER_Q2 || !stage->level_read || stage->failed)
        return application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 client lost its actual LEVEL/owner boundary");
    const qa_q2_save_level *level=current_level(stage);
    bool ok=qa_q2_game_original_read_client(provider->state.q2,slot,actor,
        (qa_bytes){stage->save->game.data,stage->save->game.size},
        level?(qa_bytes){level->game.data,level->game.size}:(qa_bytes){0},level,restored,error);
    if (!ok) stage->failed=true;
    return ok;
}

bool application_q2_original_configure(qa_application_network_q2 *owner,qa_error *error)
{
    qa_application *app=owner?owner->app:NULL;
    struct application_q2_original_save *stage=app?app->q2_original_save:NULL;
    if (!stage) return true;
    if (!stage->game_read || !stage->level_read || stage->failed ||
        app->state!=QA_APPLICATION_RUNNING || owner->config_count!=QA_Q2_SAVE_CONFIGSTRINGS)
        return application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 namespace lost its actual completed GAME/LEVEL");
    const qa_q2_save_level *level=current_level(stage);
    if (level && owner->host.source.kind==QA_APPLICATION_NATIVE_Q2_BUILTIN) {
        const char *table=(const char *)level->configstrings;
        for (uint32_t i=0;i<owner->config_count;++i) {
            const char *text=table+(size_t)i*QA_Q2_SAVE_CONFIGSTRING_BYTES;
            size_t remaining=sizeof(level->configstrings)-(size_t)i*QA_Q2_SAVE_CONFIGSTRING_BYTES;
            if (!memchr(text,0,remaining)) {
                stage->failed=true;
                return application_fail(error,QA_ERROR_FORMAT,"Original Q2 configstring has no physical table terminator");
            }
            if (!application_network_q2_config(owner,i,text,error)) {
                stage->failed=true; return false;
            }
        }
        for (unsigned kind=0;kind<3;++kind) {
            application_q2_resource_table *resources=owner->resources+kind;
            for (uint32_t index=1;index<resources->maximum;++index) {
                const char *path=owner->configs[resources->base+index];
                if (!path || !*path) continue;
                resources->paths[index]=application_network_q2_copy(path,error);
                if (!resources->paths[index]) {stage->failed=true;return false;}
                resources->count=index;
            }
        }
        owner->initialized=true;
    }
    qa_travel_route_free(&app->map_state->route);
    app->map_state->route=stage->route; stage->route=(qa_travel_route){0};
    app->map_state->cursor=0; app->map_state->pending=false;
    application_q2_original_dispose(app); return true;
}

static const qa_product *admit(const qa_application *app,const qa_q2_save_data *save,
    const char *key,qa_error *error)
{
    const qa_product *product=app && app->catalog && key?qa_catalog_find(app->catalog,key):NULL;
    if (!product || product->family!=QA_GAME_Q2 || product->availability!=QA_CONTENT_INSTALLED ||
        product->edition!=QA_EDITION_CLASSIC || !save || !save->game.data || !save->game.size ||
        (save->level_count && !save->levels) || (save->server.cvar_count && !save->server.cvars)) {
        application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 save requires its selected installed classic GAME"); return NULL;
    }
    qa_buffer header={0};
    bool ok=qa_q2_save_server_encode(&save->server,&header,error); qa_buffer_free(&header);
    qa_travel_route route={0};
    if (ok) ok=qa_q2_travel_parse(save->server.map_command,&route,error);
    if (ok && (!route.count || route.targets[0].kind!=QA_TRAVEL_MAP))
        ok=application_fail(error,QA_ERROR_FORMAT,"Original Q2 server save has no active BSP map");
    for (size_t i=0;ok && i<save->level_count;++i) {
        const qa_q2_save_level *level=save->levels+i;
        if (!level->name[0] || !memchr(level->name,0,sizeof(level->name)) || !level->game.data || !level->game.size)
            ok=application_fail(error,QA_ERROR_FORMAT,"Original Q2 visited level lacks its actual file values");
    }
    qa_travel_route_free(&route);
    return ok?product:NULL;
}

bool qa_application_q2_save_import_ready(const qa_application *app,const qa_q2_save_data *save,
    const char *key,qa_error *error)
{ return admit(app,save,key,error)!=NULL; }

bool qa_application_q2_save_import(qa_application *app,const qa_q2_save_data *save,
    const char *key,qa_error *error)
{
    if (!app || app->operation!=APPLICATION_IDLE || app->state!=QA_APPLICATION_READY ||
        app->q1_original_save || app->q2_original_save || qa_application_launch(app) ||
        app->world || app->provider_count || !qa_session_safe(app->session))
        return application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 import requires a fresh isolated application");
    const qa_product *product=admit(app,save,key,error);
    if (!product) return false;
    struct application_q2_original_save *stage=calloc(1,sizeof(*stage));
    if (!stage) return application_fail(error,QA_ERROR_MEMORY,"Retaining original Q2 constructor");
    stage->product=product->id; stage->save=copy_save(save,error);
    bool ok=stage->save && qa_q2_travel_parse(save->server.map_command,&stage->route,error);
    qa_launch_draft *draft=NULL;
    if (ok) {
        const char *name=stage->route.targets[0].name;
        size_t size=strlen(name); char *path=malloc(size+10);
        if (!path) ok=application_fail(error,QA_ERROR_MEMORY,"Retaining original Q2 BSP path");
        else {
            memcpy(path,"maps/",5); memcpy(path+5,name,size); memcpy(path+5+size,".bsp",5);
            ok=qa_launch_draft_create(app->catalog,product->id,path,&draft,error); free(path);
        }
    }
    if (ok) {
        const qa_launch_choices *choices=qa_launch_draft_choices(draft);
        qa_launch_world world=choices->world; world.start_command=NULL;
        world.spawn_point=stage->route.targets[0].spawn_point;
        world.explicit_spawn_point=true;
        ok=qa_launch_set_world(draft,&world,error) &&
            qa_launch_set_seat(draft,&(qa_launch_seat){.id=0,.name="Player 1",.local=true},error);
        if (ok && !product->builtin && product->program && *product->program) ok=qa_launch_select_original(draft,"native:primary",error);
    }
    if (ok) { app->q2_original_save=stage; stage=NULL; ok=qa_application_apply(app,draft,error); }
    qa_launch_draft_destroy(draft);
    if (stage) { qa_q2_save_destroy(stage->save); qa_travel_route_free(&stage->route); free(stage); }
    if (!ok && app->q2_original_save) app->q2_original_save->failed=true;
    return ok;
}

static bool import_campaign(qa_application *app,struct application_q2_original_save *stage,qa_error *error)
{
    qa_campaign_unit_checkpoint state={.has_current=true};
    bool ok=application_campaign_location(app,stage->product,stage->route.targets[0].name,&state.current,error);
    if (ok && stage->save->level_count) {
        state.worlds=calloc(stage->save->level_count,sizeof(*state.worlds));
        if (!state.worlds) ok=application_fail(error,QA_ERROR_MEMORY,"Retaining original Q2 visited unit");
    }
    for (size_t i=0;ok && i<stage->save->level_count;++i) {
        const qa_q2_save_level *source=stage->save->levels+i;
        if (!strcmp(source->name,stage->route.targets[0].name)) continue;
        qa_campaign_location location;
        qa_q2_save_level *level=copy_level(source,error);
        ok=level && application_campaign_location(app,stage->product,source->name,&location,error) &&
            qa_campaign_world_q2_take(location,&level,state.worlds+state.count,error);
        if (ok) ++state.count;
        if (level) { qa_buffer_free(&level->game); free(level); }
    }
    if (ok) ok=qa_campaign_unit_restore(app->campaign_unit,&state,error);
    qa_campaign_unit_checkpoint_free(&state);
    return ok;
}

bool qa_application_q2_save_import_advance(qa_application *app,bool *complete,qa_error *error)
{
    struct application_q2_original_save *stage=app?app->q2_original_save:NULL;
    if (!stage || !complete || stage->failed || app->operation!=APPLICATION_IDLE)
        return application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 advance requires its retained idle construction");
    *complete=false;
    if (qa_application_startup_pending(app)) {
        bool started=false;
        if (!qa_application_startup_advance(app,&started,error)) { stage->failed=true; return false; }
        if (!started) return true;
    }
    if (!stage->game_read || !stage->level_read || app->state!=QA_APPLICATION_RUNNING ||
        !application_guests_idle(app) || !qa_world_idle(app->world) || !qa_session_safe(app->session))
        return application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 import has not completed its actual GAME/LEVEL admission");
    if (!import_campaign(app,stage,error)) { stage->failed=true; return false; }
    if (!app->map_state) return application_fail(error,QA_ERROR_ARGUMENT,
        "Original Q2 import lost its real published map continuation");
    qa_buffer next={0};
    bool ok=qa_q2_nextserver(&stage->route,0,&next,error) &&
        qa_strings_intern(qa_session_strings(app->session),(qa_bytes){next.data,next.size},
            &app->map_state->nextserver,error);
    qa_buffer_free(&next);
    if (!ok) {stage->failed=true;return false;}
    /* The ordinary Q2 namespace constructor consumes the same retained engine
     * table and releases this stage after its real resource rows exist. */
    *complete=true;
    return true;
}
