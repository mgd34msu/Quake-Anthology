#include "guest_qc_internal.h"
#include "map_players_private.h"
#include "qa/application_q1_save.h"
#include "qa/application_save_policy.h"
#include "native_q1_save.h"
#include "qa/application_startup_prepare.h"
#include "qa/qc_text_save.h"
#include "guest_qc_original_save.h"
#include "control_frame.h"
#include <float.h>

struct application_q1_original_save {
    qa_q1_save_data *save;
    qa_product_id product;
    uint64_t initial_ns;
    bool applying;
};
static const uint8_t original_constructor[4]={'Q','1','O','I'};
bool application_q1_original_clock(const application_provider *provider,uint64_t *out)
{
    if (!provider || !provider->application || !provider->launch || !out ||
        provider->kind!=APPLICATION_PROVIDER_QC || provider->launch->selection.clock.kind!=QA_CLOCK_NETQUAKE ||
        !provider->launch->selection.artifact || strcmp(provider->launch->selection.artifact,"progs.dat")) return false;
    const struct application_q1_original_save *stage=provider->application->q1_original_save;
    if (stage && provider->launch->selection.product==stage->product &&
        !strcmp(provider->launch->selection.instance,"native:primary")) {
        *out=stage->initial_ns; return true;
    }
    qa_bytes options=provider->launch->selection.options;
    if (options.size==sizeof(original_constructor) && options.data &&
        !memcmp(options.data,original_constructor,sizeof(original_constructor))) {
        *out=provider->launch->selection.clock.initial_time_ns; return true;
    }
    return false;
}
void application_q1_original_dispose(qa_application *app)
{
    if (!app) return;
    if (app->q1_original_save) qa_q1_save_destroy(app->q1_original_save->save);
    free(app->q1_original_save); app->q1_original_save=NULL;
}

static struct application_qc_state *source(qa_application *app,qa_error *error)
{
    application_provider *provider=app?application_world_provider(app,QA_ROLE_ENTITIES,""):NULL;
    struct application_qc_state *engine=provider && provider->kind==APPLICATION_PROVIDER_QC?provider->state.qc.engine:NULL;
    if (!app || app->operation!=APPLICATION_IDLE || app->state!=QA_APPLICATION_RUNNING ||
        !app->map_view_ready || app->publication_started || app->client_preparation || !app->world || !app->session ||
        !qa_session_safe(app->session) || !qa_world_idle(app->world) || !application_guests_idle(app) ||
        !engine || engine->provider!=provider || engine->world!=app->world ||
        !provider->constructed || !provider->attached || provider->close_pending || !provider->map_bound ||
        provider->state.qc.qualified || !provider->state.qc.game || !provider->state.qc.instance ||
        engine->profile==QA_QC_QUAKEWORLD || !engine->initialized || engine->loading ||
        engine->max_clients<1 || !engine->clients || engine->has_frame ||
        engine->input_scope || engine->parked_inputs || engine->client_think_time ||
        qa_qc_program_describe(provider->state.qc.program).api!=QA_QC_API_NETQUAKE) {
        application_fail(error,QA_ERROR_ARGUMENT,"Original save requires its actual idle singleplayer NetQuake source"); return NULL;
    }
    qa_mode_view mode;
    const qa_launch_snapshot *snapshot=qa_application_launch(app);
    const qa_launch_choices *choices=snapshot?qa_launch_snapshot_choices(snapshot):NULL;
    if (!choices || !app->primary_mode_ready ||
        !qa_modes_read(app->modes,app->primary_mode,&mode,error)) {
        application_fail(error,QA_ERROR_ARGUMENT,"Original save requires its actual chosen singleplayer mode"); return NULL;
    }
    if (engine->max_clients!=1 || mode.rules.kind!=QA_MODE_SINGLE_PLAYER) {
        application_fail(error,QA_ERROR_UNSUPPORTED,"Original Quake saves represent only singleplayer games"); return NULL;
    }
    application_qc_client *client=engine->clients+1; qa_qc_slot_binding binding;
    const qa_actor_record *actor=qa_actors_get(qa_session_actors(app->session),client->actor);
    if (!client->connected || !client->spawned || !client->has_parms || client->spectator ||
        !actor || !qa_qc_slot(provider->state.qc.instance,1,&binding) ||
        binding.kind!=QA_QC_SLOT_BORROWED || !qa_actor_id_equal(binding.actor,client->actor) ||
        binding.owner!=actor->owner || binding.source_slot!=(actor->has_source?actor->source_slot:0)) {
        application_fail(error,QA_ERROR_ARGUMENT,"Original save requires its fully admitted physical source client"); return NULL;
    }
    size_t count=0; bool matches=false;
    for (size_t i=0;app->players && i<app->players->count;++i) {
        const application_player_record *row=app->players->records+i;
        if (row->retiring || !qa_actors_get(qa_session_actors(app->session),row->actor)) continue;
        ++count;
        matches=qa_actor_id_equal(row->actor,client->actor) && row->seat==client->seat &&
            !row->remote && !row->bot && !row->spectator && !row->deferred && !row->source_begin_pending;
    }
    if (count>1) {
        application_fail(error,QA_ERROR_UNSUPPORTED,"Original Quake saves cannot retain several players"); return NULL;
    }
    if (count!=1 || !matches) {
        application_fail(error,QA_ERROR_ARGUMENT,"Original save requires exactly one local source player"); return NULL;
    }
    return engine;
}
static char *duplicate(const char *text,qa_error *error)
{
    size_t size=strlen(text); char *out=malloc(size+1);
    if (!out) { application_fail(error,QA_ERROR_MEMORY,"Retaining original save header text"); return NULL; }
    memcpy(out,text,size+1); return out;
}
static bool copy_record(const qa_q1_save_record *source,qa_q1_save_record *out,qa_error *error)
{
    if ((source->count && !source->pairs) || source->count>SIZE_MAX/sizeof(*out->pairs))
        return application_fail(error,QA_ERROR_FORMAT,"Original source record exceeds its real pair storage");
    out->pairs=source->count?calloc(source->count,sizeof(*out->pairs)):NULL;
    if (source->count && !out->pairs)
        return application_fail(error,QA_ERROR_MEMORY,"Retaining pending original source pairs");
    for (size_t i=0;i<source->count;++i) {
        qa_q1_save_pair *pair=out->pairs+out->count++;
        if (!source->pairs[i].key || !source->pairs[i].value)
            return application_fail(error,QA_ERROR_FORMAT,"Original source pair lacks its actual byte strings");
        pair->key=duplicate(source->pairs[i].key,error);
        pair->value=duplicate(source->pairs[i].value,error);
        if (!pair->key || !pair->value) return false;
    }
    return true;
}
static qa_q1_save_data *copy_save(const qa_q1_save_data *source,qa_error *error)
{
    qa_q1_save_data *out=calloc(1,sizeof(*out));
    if (!out) { application_fail(error,QA_ERROR_MEMORY,"Retaining pending original source save"); return NULL; }
    out->version=source->version; out->skill=source->skill; out->time=source->time;
    memcpy(out->spawn_parameters,source->spawn_parameters,sizeof(out->spawn_parameters));
    out->map=duplicate(source->map,error);
    if (source->comment) out->comment=duplicate(source->comment,error);
    if (source->game_directories) out->game_directories=duplicate(source->game_directories,error);
    bool ok=out->map && (!source->comment || out->comment) && (!source->game_directories || out->game_directories);
    for (size_t i=0;ok && i<64;++i) { out->lightstyles[i]=duplicate(source->lightstyles[i],error); ok=out->lightstyles[i]!=NULL; }
    if (ok) ok=copy_record(&source->globals,&out->globals,error);
    if (ok && source->entity_count>SIZE_MAX/sizeof(*out->entities))
        ok=application_fail(error,QA_ERROR_MEMORY,"Pending original edict extent exceeds its owner");
    if (ok) {
        out->entities=calloc(source->entity_count,sizeof(*out->entities));
        if (!out->entities) ok=application_fail(error,QA_ERROR_MEMORY,"Retaining pending original edicts");
    }
    for (size_t i=0;ok && i<source->entity_count;++i) {
        ++out->entity_count;
        ok=copy_record(source->entities+i,out->entities+i,error);
    }
    if (ok && source->extension.size) {
        out->extension.data=malloc(source->extension.size);
        if (!out->extension.data) ok=application_fail(error,QA_ERROR_MEMORY,"Retaining pending original extension");
        else { memcpy(out->extension.data,source->extension.data,source->extension.size); out->extension.size=source->extension.size; }
    }
    if (!ok) { qa_q1_save_destroy(out); return NULL; }
    return out;
}
bool qa_application_q1_save_capture(qa_application *app,const qa_q1_save_client *client,
    qa_q1_save_data **out,qa_error *error)
{
    if (!client || !client->level || !out || *out || !app || app->operation!=APPLICATION_IDLE ||
        app->state!=QA_APPLICATION_RUNNING || !app->map_view_ready || app->publication_started ||
        app->client_preparation || !app->world || !app->session || !qa_session_safe(app->session) ||
        !qa_world_idle(app->world) || !application_guests_idle(app))
        return application_fail(error,QA_ERROR_ARGUMENT,"Original save requires its actual completed idle game");
    const qa_product *product=qa_application_save_original_product(app);
    application_provider *provider=application_world_provider(app,QA_ROLE_ENTITIES,"");
    if (!product || product->family!=QA_GAME_Q1 || product->edition==QA_EDITION_QUAKEWORLD ||
        !provider || !provider->constructed || !provider->attached || !provider->map_bound || provider->close_pending)
        return application_fail(error,QA_ERROR_ARGUMENT,"Original Quake save requires its actual stock NetQuake product");
    const char *map=qa_strings_cstr(qa_session_strings(app->session),app->current_map);
    if (!map || !product->directory)
        return application_fail(error,QA_ERROR_FORMAT,"Original source has no genuine map/product directory");
    qa_q1_save_data *save=calloc(1,sizeof(*save));
    if (!save) return application_fail(error,QA_ERROR_MEMORY,"Allocating original source save");
    save->version=product->edition==QA_EDITION_RERELEASE?6:5;
    save->map=duplicate(map,error);
    if (save->version==6) {
        const char *directory=product->directory;
        for (const char *p=directory;*p;++p) if (*p=='/' || *p=='\\') directory=p+1;
        save->game_directories=duplicate(directory,error);
    }
    bool ok=save->map && (save->version!=6 || save->game_directories);
    if (ok && provider->kind==APPLICATION_PROVIDER_Q1)
        ok=application_q1_native_save_capture(app,provider,save,error);
    else if (ok) {
        struct application_qc_state *engine=source(app,error);
        ok=engine!=NULL;
        const qa_cvar_view *skill=ok?qa_cvars_find(engine->cvars,"skill"):NULL;
        if (ok && (!skill || !isfinite(skill->number)))
            ok=application_fail(error,QA_ERROR_FORMAT,"Original source lost its live skill setting");
        if (ok) {
            save->skill=qa_source_float_to_i32(fmaxf(0,fminf(3,floorf(skill->number))));
            save->time=(double)engine->source_time_ns/1e9;
            int32_t reference=0;float health=0;
            ok=qa_qc_slot_reference(provider->state.qc.instance,1,&reference,error) &&
                application_qc_float(engine,reference,"health",&health,error);
            if (ok && !(health>0)) ok=application_fail(error,QA_ERROR_ARGUMENT,"Cannot save a dead original Quake player");
        }
        for (size_t i=0;ok && i<16;++i) {
            save->spawn_parameters[i]=engine->clients[1].parms[i];
            if (!isfinite(save->spawn_parameters[i]))
                ok=application_fail(error,QA_ERROR_FORMAT,"Source spawn parameters are nonfinite");
        }
        for (size_t i=0;ok && i<64;++i) {
            const char *style=engine->lightstyles[i];
            save->lightstyles[i]=duplicate(style && *style?style:"m",error);ok=save->lightstyles[i]!=NULL;
        }
        if (ok) {
            app->operation=APPLICATION_PERSISTING;
            ok=qa_qc_text_capture(provider->state.qc.instance,&save->globals,&save->entities,&save->entity_count,error);
            app->operation=APPLICATION_IDLE;
        }
    }
    if (ok) ok=qa_q1_save_comment(save,client->level,client->killed_monsters,client->total_monsters,error) &&
        qa_q1_save_singleplayer(save,error) && save->entities[1].count!=0;
    if (!ok) {
        if (error && error->code==QA_OK) application_fail(error,QA_ERROR_FORMAT,"Original source save lacks its physical player record");
        qa_q1_save_destroy(save);return false;
    }
    *out=save;return true;
}
static bool restore_body(struct application_qc_state *engine,uint32_t slot,qa_error *error)
{
    qa_qc_instance *vm=engine->provider->state.qc.instance;
    qa_qc_slot_binding binding; qa_body_state body;
    if (!qa_qc_slot(vm,slot,&binding) || !qa_qc_text_body_read(vm,slot,&body,error) ||
        (binding.kind==QA_QC_SLOT_BORROWED &&
         !qa_world_body_write(engine->world,binding.actor,&body,error))) return false;
    int32_t reference;
    if (!qa_qc_slot_reference(vm,slot,&reference,error)) return false;
    qa_qc_entity_access access={.kind=QA_QC_ENTITY_BIND,.binding=binding,.reference=reference};
    return application_qc_prepare_entity(engine,vm,&access,error) &&
        qa_world_link(engine->world,binding.actor,NULL,error);
}
static bool restore_think(struct application_qc_state *engine,uint32_t slot,qa_error *error)
{
    qa_qc_slot_binding binding; int32_t reference; float next;
    if (!qa_qc_slot(engine->provider->state.qc.instance,slot,&binding) ||
        !qa_qc_slot_reference(engine->provider->state.qc.instance,slot,&reference,error) ||
        !application_qc_float(engine,reference,"nextthink",&next,error)) return false;
    qa_scheduler_cancel(qa_session_scheduler(engine->services.session),binding.actor);
    if (!isfinite(next)) return application_fail(error,QA_ERROR_FORMAT,"Saved source think is nonfinite");
    if (!(next>0) || (double)next*1e9>=(double)UINT64_MAX) return true;
    qa_think think={.actor=binding.actor,.execution_provider=engine->provider->owner,
        .callback_id=1,.due_ns=(uint64_t)ceil((double)next*1e9),.boundary=QA_THINK_DURING_PHYSICS};
    return application_qc_think_binding(engine->provider,binding.actor,1,&think.callback,&think.context,error) &&
        qa_session_schedule(engine->services.session,&think,error);
}
static bool restore_raw(struct application_qc_state *engine,const qa_q1_save_data *save,qa_error *error)
{
    qa_application *app=engine->provider->application; qa_qc_instance *vm=engine->provider->state.qc.instance;
    uint32_t previous=qa_qc_entity_count(vm);
    for (uint32_t slot=1;slot<previous;++slot) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(vm,slot,&binding)) return application_fail(error,QA_ERROR_FORMAT,"Candidate source row disappeared");
        if (binding.kind==QA_QC_SLOT_FREE) continue;
        if ((slot==1 && binding.kind!=QA_QC_SLOT_BORROWED) ||
            (slot>1 && binding.kind!=QA_QC_SLOT_OWNED))
            return application_fail(error,QA_ERROR_FORMAT,"Candidate source row has another physical owner");
        if (!qa_world_unlink(engine->world,binding.actor,error)) return false;
        if (slot>1) {
            int32_t reference;
            if (!qa_qc_slot_reference(vm,slot,&reference,error) || !qa_qc_remove_entity(vm,reference,error)) return false;
        }
    }
    qa_actor_definition definition;
    if (!qa_strings_intern_cstr(qa_session_strings(app->session),"quakec:authored",&definition,error)) return false;
    for (uint32_t slot=2;slot<save->entity_count;++slot) {
        if (!save->entities[slot].count) continue;
        qa_actor_id actor;
        if (!qa_session_allocate(app->session,engine->provider->owner,definition,true,slot,&actor,error)) return false;
        if (!qa_qc_bind_actor(vm,slot,actor,QA_QC_SLOT_OWNED,error) ||
            !qa_session_bind_execution(app->session,actor,engine->provider->owner,error)) {
            (void)qa_session_release(app->session,actor,NULL); return false;
        }
    }
    if (!qa_qc_text_import(vm,save,error)) return false;
    for (uint32_t slot=1;slot<save->entity_count;++slot)
        if (save->entities[slot].count && !restore_body(engine,slot,error)) return false;
    for (uint32_t slot=1;slot<save->entity_count;++slot)
        if (save->entities[slot].count && !restore_think(engine,slot,error)) return false;
    engine->source_time_ns=app->q1_original_save->initial_ns;
    if (!qa_qc_game_set_time(engine->provider->state.qc.game,save->time,0,error)) return false;
    for (size_t i=0;i<16;++i) {
        engine->clients[1].parms[i]=(float)save->spawn_parameters[i];
    }
    engine->clients[1].has_parms=true;
    for (size_t i=0;i<64;++i) {
        char *style=duplicate(save->lightstyles[i],error);
        if (!style) return false;
        qa_builtin_event event={.kind=QA_BUILTIN_LIGHT,.family=QA_GAME_Q1,.provider=engine->provider->owner,
            .code=(int32_t)i,.time_ns=engine->source_time_ns};
        if (!qa_builtin_resource(&engine->services,style,&event.resource,error) ||
            !qa_builtin_emit(&engine->services,&event,error)) { free(style); return false; }
        free(engine->lightstyles[i]); engine->lightstyles[i]=style;
    }
    qa_buffer_free(&engine->original_extension);
    if (save->extension.size) {
        engine->original_extension.data=malloc(save->extension.size);
        if (!engine->original_extension.data) return application_fail(error,QA_ERROR_MEMORY,"Retaining original extension owner");
        memcpy(engine->original_extension.data,save->extension.data,save->extension.size);
        engine->original_extension.size=save->extension.size;
    }
    application_control_record *control;
    qa_actor_id actor=engine->clients[1].actor;
    qa_body_state body;
    if (!qa_world_body_read(engine->world,actor,&body,error) ||
        !application_control_ensure(app,actor,body.angles,&control,error) ||
        !application_qc_control_state(engine->provider,actor,&control->state,&control->bounds,NULL,&control->view_angles,error)) return false;
    control->command_angles=control->view_angles; control->standing_bounds=body.bounds;
    control->ground=(qa_movement_ground){0};
    if (control->state.kind==QA_MOVEMENT_NETQUAKE) {
        if (control->state.data.nq.flags&UINT32_C(512))
            control->ground=(qa_movement_ground){
                .hit=body.ground.kind==QA_ACTOR_REFERENCE_SOURCE && body.ground.value.source.slot==0 ? QA_TRACE_HIT_WORLD : QA_TRACE_HIT_ACTOR,
                .actor=qa_actor_reference_resolve(qa_session_actors(app->session),body.ground)};
        control->state.data.nq.ground=control->ground;
        control->water_level=control->state.data.nq.water_level;
        control->water_type=control->state.data.nq.water_type;
    }
    const qa_qc_definition *offset=qa_qc_program_find_field(engine->provider->state.qc.program,"view_ofs");
    int32_t reference;
    if (offset && (offset->type!=QA_QC_VECTOR || !qa_qc_slot_reference(vm,1,&reference,error) ||
        !qa_qc_entity_vector(vm,reference,offset->offset,&control->view_offset,error))) return false;
    control->view_height=control->view_offset.z;
    return (qa_session_safe(app->session) && qa_world_idle(app->world) && application_guests_idle(app)) ||
        application_fail(error,QA_ERROR_ARGUMENT,"Original import did not finish at its actual idle source boundary");
}
bool application_q1_original_admit(const qa_application *app,const qa_q1_save_data *save,
    const char *key,application_q1_original_admission *out,qa_error *error)
{
    if (!app || !app->catalog || !app->session || !key || !*key || !out)
        return application_fail(error,QA_ERROR_ARGUMENT,"Original admission requires its actual application and selected product");
    if (!qa_q1_save_singleplayer(save,error)) return false;
    if ((save->version!=5 && save->version!=6) || !save->entities[1].count ||
        (save->extension.size && (!save->extension.data || memchr(save->extension.data,0,save->extension.size))))
        return application_fail(error,QA_ERROR_FORMAT,"Original import requires a v5/v6 source record and physical player");
    const qa_product *product=qa_catalog_find(app->catalog,key);
    if (!product || product->family!=QA_GAME_Q1 || product->availability!=QA_CONTENT_INSTALLED ||
        product->edition==QA_EDITION_QUAKEWORLD ||
        save->version!=(product->edition==QA_EDITION_RERELEASE?6u:5u) || save->entity_count>qa_actors_capacity(qa_session_actors(app->session)))
        return application_fail(error,QA_ERROR_ARGUMENT,"Original candidate lacks its genuine selected Quake content/capacity");
    long double nanoseconds=(long double)save->time*1000000000.0L;
    if (!isfinite(nanoseconds) || nanoseconds<0 || nanoseconds>=(long double)UINT64_MAX)
        return application_fail(error,QA_ERROR_FORMAT,"Original saved time exceeds the actual source clock");
    for (size_t i=0;i<16;++i)
        if (!isfinite(save->spawn_parameters[i]) || fabs(save->spawn_parameters[i])>FLT_MAX)
            return application_fail(error,QA_ERROR_FORMAT,"Saved spawn parameter exceeds source float");
    for (size_t i=0;i<64;++i)
        if (!save->lightstyles[i]) return application_fail(error,QA_ERROR_FORMAT,"Saved lightstyle is absent");
    *out=(application_q1_original_admission){product,(uint64_t)nanoseconds};
    return true;
}
bool qa_application_q1_save_import_ready(const qa_application *app,const qa_q1_save_data *save,
    const char *key,qa_error *error)
{
    application_q1_original_admission admission;
    return application_q1_original_admit(app,save,key,&admission,error);
}
bool qa_application_q1_save_import(qa_application *app,const qa_q1_save_data *save,const char *key,qa_error *error)
{
    if (!app || app->operation!=APPLICATION_IDLE || app->state!=QA_APPLICATION_READY ||
        app->q1_original_save || qa_application_launch(app) || app->world || app->provider_count ||
        !qa_session_safe(app->session))
        return application_fail(error,QA_ERROR_ARGUMENT,"Original import requires a fresh isolated candidate");
    application_q1_original_admission admission;
    if (!application_q1_original_admit(app,save,key,&admission,error)) return false;
    const qa_product *product=admission.product;
    struct application_q1_original_save *stage=calloc(1,sizeof(*stage));
    if (!stage) return application_fail(error,QA_ERROR_MEMORY,"Retaining original source construction stage");
    stage->save=copy_save(save,error); stage->product=product->id; stage->initial_ns=admission.initial_ns;
    if (!stage->save) { free(stage); return false; }
    save=stage->save;
    qa_launch_draft *draft=NULL;
    size_t length=strlen(save->map); char *map=length<=SIZE_MAX-10?malloc(length+10):NULL;
    bool ok=map!=NULL;
    if (ok) { memcpy(map,"maps/",5); memcpy(map+5,save->map,length); memcpy(map+5+length,".bsp",5); }
    else application_fail(error,QA_ERROR_MEMORY,"Preparing original source map path");
    if (ok) ok=qa_launch_draft_create(app->catalog,product->id,map,&draft,error);
    if (ok) {
        const qa_launch_choices *choices=qa_launch_draft_choices(draft);
        qa_launch_world world=choices->world; world.skill=save->skill; world.start_command=NULL;
        qa_launch_provider primary=choices->providers[0];
        primary.runtime=QA_PROGRAM_QUAKEC; primary.artifact="progs.dat"; primary.implementation=product->key;
        primary.clock=qa_clock_defaults(QA_CLOCK_NETQUAKE); primary.clock.initial_time_ns=stage->initial_ns;
        primary.options=(qa_bytes){original_constructor,sizeof(original_constructor)};
        qa_launch_mode mode=choices->modes[0]; mode.rules=qa_mode_defaults(QA_MODE_Q1,QA_MODE_SINGLE_PLAYER);
        qa_launch_equipment equipment=choices->equipment[0]; equipment.selection.grapple=QA_GRAPPLE_DISABLED;
        equipment.selection.grenades.enabled=false;
        ok=qa_launch_set_world(draft,&world,error) && qa_launch_set_provider(draft,&primary,error) &&
            qa_launch_set_mode(draft,&mode,error) && qa_launch_set_equipment(draft,&equipment,error) &&
            qa_launch_set_seat(draft,&(qa_launch_seat){.id=0,.name="Player 1",.local=true},error);
    }
    free(map);
    if (ok) {
        app->q1_original_save=stage; stage=NULL;
        ok=qa_application_apply(app,draft,error);
    }
    qa_launch_draft_destroy(draft);
    if (stage) { qa_q1_save_destroy(stage->save); free(stage); }
    if (!ok && app->q1_original_save) app->q1_original_save->applying=true;
    return ok;
}
bool qa_application_q1_save_import_advance(qa_application *app,bool *complete,qa_error *error)
{
    struct application_q1_original_save *stage=app?app->q1_original_save:NULL;
    if (!app || !complete || !stage || stage->applying || app->operation!=APPLICATION_IDLE)
        return application_fail(error,QA_ERROR_ARGUMENT,"Original import advance requires its retained idle source stage");
    *complete=false;
    if (qa_application_startup_pending(app)) {
        bool started=false;
        if (!qa_application_startup_advance(app,&started,error)) { stage->applying=true; return false; }
        if (!started) return true;
    }
    stage->applying=true;
    struct application_qc_state *engine=source(app,error);
    if (!engine || !restore_raw(engine,stage->save,error)) return false;
    application_q1_original_dispose(app);
    *complete=true;
    return true;
}
