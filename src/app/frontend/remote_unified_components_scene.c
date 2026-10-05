#include "remote_unified_components_private.h"
#include "component_scene.h"
#include <math.h>
#include <limits.h>
#include "qa/console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool frontend_unified_components_scene_association_read(const qa_frontend *frontend,const void *context,
    uint64_t identity,frontend_unified_component_scene_association *out)
{
    const remote_component *row=context;
    const frontend_unified_components *owner=row?row->parent:NULL;
    if(!frontend||!identity||!out||!owner||owner->frontend!=frontend||!owner->recipe||owner->failed) return false;
    bool linked=false;
    for(size_t i=0;i<q3remote_component_physical_count(owner);++i) if(q3remote_component_physical_at(owner,i)==row) { linked=true; break; }
    if(!linked||row->frontend_identity!=identity||!row->frontend.owner||!row->assets||(!row->scene&&!row->retired)||!row->frame||!row->state.provider_row)
        return false;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(owner->replica);
    if(!domain) return false;
    *out=(frontend_unified_component_scene_association){.owner=row->owner,.service_owner=row->services,
        .generation=row->state.generation,.frontend_identity=identity,.physical_seat=domain->physical_seat,.time_ms=row->renderer_time,
        .viewer=row->frame->viewer,.recipe=owner->recipe,.provider=row->state.provider_row,
        .frontend_owner=row->frontend.owner,.scene=row->scene,.assets=row->assets};
    return true;
}

static bool retained(void *context)
{
    remote_component *r=context;
    return r&&r->parent&&r->parent->recipe&&r->state.provider_row&&
        qa_executable_recipe_current(r->parent->recipe,qa_executable_recipe_catalog(r->parent->recipe));
}
static bool published(void *context)
{
    remote_component *r=context;
    if(!retained(r)||r->retired||(!r->parent->restoring&&!frontend_unified_components_current(r->parent))) return false;
    for(size_t i=0;i<r->parent->count;++i) if(r->parent->rows[i]==r) return true;
    return false;
}
static bool source_current(void *context,const application_q3_scene_context *view)
{
    remote_component *r=context;
    if(!published(r)||!r->frame||!view||view->generation!=r->state.generation) return false;
    remote_component_frame *frame=view->baseline&&!r->state.player_events?r->baseline:r->frame;
    if(!frame||view->revision!=frame->context.revision||view->game_state_revision!=frame->context.game_state_revision||
        view->client_number!=frame->context.client_number||
        !qa_actors_get(frontend_remote_unified_registry(r->parent->replica),frame->viewer)) return false;
    if(view->baseline&&(view->time_ms!=frame->context.time_ms||view->frame_ms)) return false;
    if(r->acquired&&view->time_ms==r->entered.time_ms&&view->frame_ms==r->entered.frame_ms&&
        view->snapshot==&frame->snapshot&&view->game_state==&frame->source.game_state&&view->actors==frame->actors) return true;
    application_q3_scene_context held;
    return application_q3_scene_retained_context(r->scene,&held)&&view->snapshot==held.snapshot&&
        view->game_state==held.game_state&&view->actors==held.actors&&view->actor_count==held.actor_count&&
        view->revision==held.revision&&view->time_ms==held.time_ms&&view->generation==held.generation;
}
static bool presentation_time(remote_component *r,int32_t *time,int32_t *elapsed,qa_error *e)
{
    frontend_unified_recipient_clock clock;
    if(!r->frame||!frontend_remote_unified_clock_read(r->parent->replica,&clock,e)||!isfinite(clock.milliseconds)) return false;
    int32_t server_time=r->frame->snapshot.server_time;
    if(r->state.player_events) {
        if(r->previous_frame_present&&server_time<r->previous_frame_time)
            return q3remote_component_fail(e,QA_ERROR_FORMAT,"Component player-event source time moved backward");
        *time=server_time; *elapsed=r->previous_frame_present?server_time-r->previous_frame_time:0;
        return true;
    }
    if(!r->scene_time_present) {
        r->scene_time_offset=(double)server_time-clock.milliseconds;
        r->scene_time_present=true;
    }
    double value=trunc(fmax((double)server_time,fmax(r->previous_frame_present?(double)r->previous_frame_time:(double)server_time,
        clock.milliseconds+r->scene_time_offset)));
    if(!isfinite(value)||value<0||value>INT32_MAX)
        return q3remote_component_fail(e,QA_ERROR_FORMAT,"Component presentation time exceeds its real Source word range");
    *time=(int32_t)value;
    *elapsed=r->previous_frame_present?*time-r->previous_frame_time:0;
    return true;
}
static bool acquire(void *context,bool baseline,application_q3_scene_context *out,qa_error *e)
{
    remote_component *r=context; remote_component_frame *f=baseline&&!r->state.player_events?r->baseline:r->frame;
    if(!published(r)||r->acquired||!f||!out)
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component has no entered received Source frame");
    *out=f->context; out->baseline=baseline;
    int32_t time=0,elapsed=0;
    if(!presentation_time(r,&time,&elapsed,e)) return false;
    out->time_ms=baseline?f->snapshot.server_time:time; out->frame_ms=baseline?0:elapsed;
    r->entered=*out; r->acquired=true;
    if(!source_current(r,out)) {
        r->acquired=false; memset(&r->entered,0,sizeof(r->entered));
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component snapshot viewer is stale");
    }
    r->renderer_time=out->time_ms; return true;
}
static void release(void *context,const application_q3_scene_context *view)
{
    remote_component *r=context;
    if(r->acquired&&r->entered.snapshot==view->snapshot) { r->acquired=false; memset(&r->entered,0,sizeof(r->entered)); }
}
static bool source_actor(void *context,uint32_t slot,qa_actor_id *out,bool *owned,bool *found,qa_error *e)
{
    remote_component *r=context; *out=(qa_actor_id){0}; *owned=*found=false;
    if(!published(r)||!r->frame||(r->acquired&&!source_current(r,&r->entered)))
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote actor mapping lacks its actual component frame");
    const application_q3_scene_context *source=r->acquired?&r->entered:&r->frame->context;
    for(size_t i=0;i<source->actor_count;++i) if(source->actors[i].slot==slot) {
        *out=source->actors[i].actor; *owned=source->actors[i].owned;
        *found=qa_actors_get(frontend_remote_unified_registry(r->parent->replica),*out)!=NULL; break;
    }
    return true;
}
static bool source_live(void *context,qa_actor_id actor)
{
    remote_component *r=context;
    if(!published(r)||!r->frame||!qa_actors_get(frontend_remote_unified_registry(r->parent->replica),actor)) return false;
    for(size_t i=0;i<r->frame->context.actor_count;++i) if(qa_actor_id_equal(r->frame->actors[i].actor,actor)) return true;
    return false;
}
static bool actor_encode(void *context,qa_actor_id actor,qa_saved_actor_id *out,qa_error *e)
{
    remote_component *r=context;
    return frontend_remote_unified_wire_actor(r->parent->replica,actor,out)||
        q3remote_component_fail(e,QA_ERROR_FORMAT,"Component actor is absent from its actual replica identity ledger");
}
static bool actor_decode(void *context,qa_saved_actor_id actor,qa_actor_id *out,qa_error *e)
{
    remote_component *r=context;
    return frontend_remote_unified_actor_retained(r->parent->replica,actor.slot,actor.generation,out,e);
}
static bool weapon_presented(void *context,qa_actor_id actor,bool *out,qa_error *e)
{
    remote_component *r=context;
    if(!r->frame||!qa_actor_id_equal(r->frame->viewer,actor)||!source_live(r,actor)) return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote weapon visibility lost its received viewer");
    *out=r->frame->context.weapon_presented; return true;
}
static bool entered(void *context,application_q3_scene_context *out)
{
    remote_component *r=context;
    if(!r->acquired||!source_current(r,&r->entered)) return false;
    *out=r->entered; return true;
}
static bool active(void *context,const qa_command_context *command)
{
    remote_component *r=context;
    return published(r)&&command&&command->owner==r->services&&command->dialect==QA_CONSOLE_Q3;
}
static bool capture(void *context,const qa_command_context *command,qa_command_context *out,qa_error *e)
{
    if(!active(context,command)) return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote CG console lost its admitted component namespace");
    *out=*command; return true;
}
static qa_cvars *cvar_owner(void *context,const qa_command_context *command,const char *name)
{ (void)name; remote_component *r=context; return active(r,command)?r->cvars:NULL; }
static bool cheats(void *context)
{ remote_component *r=context; const qa_cvar_view *view=qa_cvars_find(r->cvars,"sv_cheats"); return view&&view->integer!=0; }
static void print(void *context,const char *text)
{
    remote_component *r=context; qa_command_context command={.owner=r->services,.dialect=QA_CONSOLE_Q3,.origin=QA_COMMAND_REMOTE};
    frontend_console_print(r->parent->frontend,&command,text);
}
static void console_print(void *context,const qa_command_context *command,const char *text)
{ (void)command; print(context,text); }
static qa_command_result console_command(void *context,const qa_command_invocation *command,qa_error *e)
{
    remote_component *r=context;
    if(!published(r)||!r->frame) return QA_COMMAND_UNHANDLED;
    if(!r->initialized) return QA_COMMAND_UNHANDLED;
    qa_command_tokens tokens={.count=command->argc,.values=(char **)command->argv,.args_text=(char *)command->args_text};
    bool handled=false;
    if(!application_q3_scene_console(r->scene,&tokens,&handled,e)) return QA_COMMAND_FAILED;
    if(handled) return QA_COMMAND_HANDLED;
    if (r->state.owner_generation!=r->state.generation) {
        q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component command changed its actual publication generation");
        return QA_COMMAND_FAILED;
    }
    qa_unified_component_owner owner={r->state.provider,r->state.owner_generation};
    return frontend_remote_unified_component_command(r->parent->replica,&owner,
        command->argv,command->argc,e)?QA_COMMAND_HANDLED:QA_COMMAND_FAILED;
}
bool q3remote_component_open(remote_component *r,qa_error *e)
{
    if(r->scene) return r->initialized||r->restore_pending||q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component retains an incomplete CG constructor");
    frontend_unified_components *o=r->parent; const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    const qa_product *product=qa_catalog_product(qa_executable_recipe_catalog(o->recipe),r->state.mod->product);
    qa_vfs *files=NULL;
    if(!domain||!product||!qa_executable_recipe_content(o->recipe,product->identity,&files,&product,e)) return false;
    bool ok=true;
    if(!r->profile) {
    qa_json_document *d=NULL; if(!qa_json_parse(r->state.mod->declaration,&d,e)) return false;
    qa_json_id presentation=qa_json_get(d,qa_json_root(d),"presentation"); qa_buffer path={0};
    ok=qa_json_string_equal(d,qa_json_get(d,presentation,"runtime"),r->state.player_events?"qvm-player-events":"qvm-scene")&&
        qa_json_string(d,qa_json_get(d,qa_json_get(d,presentation,"cgame"),"path"),&path,e)&&!memchr(path.data,0,path.size)&&
        qa_vfs_acquire_receipt(files,(const char *)path.data,&r->artifact,&r->acquisition,e)&&
        qa_vfs_acquire_receipt(files,r->state.mod->program_path,&r->gameplay,&r->gameplay_acquisition,e)&&
        qa_sha256_equal(qa_resource_digest(r->gameplay),&r->state.mod->program_digest)&&
        qa_qvm_image_load(qa_resource_bytes(r->artifact),&r->image,e)&&qa_qvm_image_load(qa_resource_bytes(r->gameplay),&r->gameplay_image,e)&&
        application_q3_scene_profile_create(r->image,r->state.abi,(const char *)path.data,r->gameplay_image,r->state.mod->program_path,
            qa_json_source(d,presentation),&r->profile,e);
    qa_buffer_free(&path); qa_json_destroy(d);
    if(!ok) return e&&e->code!=QA_OK?false:q3remote_component_fail(e,QA_ERROR_FORMAT,"Received component has no qualified held CG program");
    }
    qa_strings *strings=qa_session_strings(qa_application_session(domain->application));
    size_t size=strlen(r->state.provider)+128; char *name=malloc(size);
    if(!name) return q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining actual received CG namespace");
    int count=snprintf(name,size,"remote-component:%u:%s:%llu:%llu",frontend_remote_unified_epoch(o->replica),r->state.provider,
        (unsigned long long)r->state.owner_generation,(unsigned long long)r->state.generation);
    ok=count>=0&&(size_t)count<size&&qa_strings_intern_cstr(strings,name,&r->owner,e);
    if(ok) { size_t length=strlen(name); ok=length+9<size; if(ok) { memcpy(name+length,":services",10); ok=qa_strings_intern_cstr(strings,name,&r->services,e); } }
    free(name); if(!ok) return false;
    qa_cvar_options cvars={.dialect=QA_CONSOLE_Q3,.user=r,.print=print,.cheats_allowed=cheats};
    if(!r->cvars) r->cvars=qa_cvars_create(&cvars,e);
    if(!r->cvars) return false;
    qa_console_options console={.context={.owner=r->services,.dialect=QA_CONSOLE_Q3,.origin=QA_COMMAND_REMOTE},.cvars=r->cvars,.user=r,
        .print=console_print,.cvar_owner=cvar_owner,.source_command=console_command,.capture_context=capture,.context_active=active};
    if(!r->console) r->console=qa_console_create(&console,e);
    if(!r->console) return false;
    r->host=(qa_q3_host_options){.role=QA_QVM_CGAME,.abi=r->state.abi,.session=qa_application_session(domain->application),
        .owner=r->owner,.service_owner=r->services,.mounts=files,.cvars=r->cvars,.console=r->console,.command_context=console.context,
        .common={.context=r,.print=print}};
    application_q3_scene_source source={r,acquire,source_current,source_actor,source_live,release,weapon_presented};
    application_q3_component_scene_preparation request={.origin=APPLICATION_Q3_COMPONENT_SCENE_REMOTE,.component=r->state.mod,
        .restoring=r->restore_pending,.retired=r->retired,.frontend_identity=r->frontend_identity,
        .recipe=o->recipe,.recipe_provider=r->state.provider_row,.catalog=qa_executable_recipe_catalog(o->recipe),
        .owner=r->owner,.generation=r->state.generation,.service_owner=r->services,.physical_seat=domain->physical_seat,.viewer=r->frame->viewer,
        .content=files,.artifact=r->artifact,.acquisition=&r->acquisition,.profile=r->profile,.source=source,.host=&r->host,.assets=&r->assets,
        .frontend=&r->frontend,.context=r,.retained=retained,.published=published,.publication_read=entered};
    if(!frontend_component_scene_prepare(o->frontend,&request,e)) return false;
    if(!r->frontend.identity_read||!r->frontend.identity_read(r->frontend.owner,&r->frontend_identity))
        return q3remote_component_fail(e,QA_ERROR_FORMAT,"Remote CG renderer has no real physical identity");
    if(r->retired) {
        if(r->host.frontend_lifetime&&r->host.release_frontend) {
            r->host.release_frontend(r->host.frontend_lifetime); r->host.frontend_lifetime=NULL;
        }
        return true;
    }
    application_q3_scene_options create={.profile=r->profile,.host=r->host,.assets=r->assets,.viewer=r->frame->viewer,.source=source,
        .output_context=r->frontend.owner,.finish_output=r->frontend.finish,.actor_codec_context=r,.actor_encode=actor_encode,.actor_decode=actor_decode};
    bool created=application_q3_scene_create(&create,r->restore_pending,&r->scene,e); r->host_entered=r->scene!=NULL;
    if(!created) return false;
    if(r->restore_pending) return true;
    if(!r->frontend.begin(r->frontend.owner,0,e)||!application_q3_scene_initialize(r->scene,e)) return false;
    r->initialized=true; return true;
}
bool q3remote_component_retire(remote_component *row,qa_error *error)
{
    if(row->acquired||(row->scene&&!application_q3_scene_idle(row->scene)))
        return q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component retirement retains real execution or body leases");
    if(!application_q3_scene_destroy(&row->scene,error)) return false;
    if(!row->host_entered&&row->host.frontend_lifetime&&row->host.release_frontend) {
        row->host.release_frontend(row->host.frontend_lifetime); row->host.frontend_lifetime=NULL;
    }
    row->retired=true; row->initialized=false; row->host_entered=false;
    if(row->frontend.owner&&(!row->frontend.retire||!row->frontend.retire(row->frontend.owner,error)))
        return error&&error->code!=QA_OK?false:q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component registry lacks its real service retirement owner");
    while(row->events) { remote_component_event *next=row->events->next; free(row->events); row->events=next; }
    q3remote_component_admissions_clear(row); return true;
}
bool q3remote_component_close(remote_component **slot,qa_error *e)
{
    if(!slot||!*slot) return true;
    remote_component *r=*slot;
    if(r->acquired||(r->scene&&!application_q3_scene_idle(r->scene))) return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote CG retirement retains Source output leases");
    if(!application_q3_scene_destroy(&r->scene,e)) return false;
    if(!r->host_entered&&r->host.frontend_lifetime&&r->host.release_frontend) {
        r->host.release_frontend(r->host.frontend_lifetime); r->host.frontend_lifetime=NULL;
    }
    if(r->frontend.owner&&!r->frontend.destroy(&r->frontend.owner,e)) return false;
    qa_console_destroy(r->console); qa_cvars_destroy(r->cvars);
    application_q3_scene_profile_destroy(r->profile); qa_qvm_image_release(r->image); qa_qvm_image_release(r->gameplay_image);
    qa_resource_release(r->artifact); qa_resource_release(r->gameplay); qa_vfs_acquisition_dispose(&r->acquisition); qa_vfs_acquisition_dispose(&r->gameplay_acquisition);
    if(r->frame!=r->baseline) q3remote_component_frame_free(r->frame);
    q3remote_component_frame_free(r->baseline); q3remote_component_state_free(&r->state);
    qa_buffer_free(&r->saved_scene); qa_buffer_free(&r->saved_cvars); qa_buffer_free(&r->saved_console);
    q3remote_component_admissions_clear(r);
    while(r->events) { remote_component_event *next=r->events->next; free(r->events); r->events=next; }
    free(r); *slot=NULL; return true;
}
bool frontend_unified_components_prepare_draw(frontend_unified_components *o,const qa_scene_view *view,uint64_t sequence,
    const frontend_unified_recipient_clock *clock,qa_error *e)
{
    if(!frontend_unified_components_current(o)||!frontend_unified_components_idle(o)||!view||!clock)
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component draw requires its authentic recipient view");
    frontend_unified_recipient_clock actual;
    if(!frontend_remote_unified_clock_read(o->replica,&actual,e)) return false;
    if(actual.milliseconds!=clock->milliseconds||actual.source_elapsed_ms!=clock->source_elapsed_ms||
        actual.physical_frame!=clock->physical_frame||actual.wall_time_ns!=clock->wall_time_ns||actual.wall_elapsed_ns!=clock->wall_elapsed_ns)
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component draw clock changed its actual recipient receipt");
    o->busy=true; bool ok=true;
    for(size_t i=0;ok&&i<o->count;++i) {
        remote_component *r=o->rows[i]; if(!r->frame) continue;
        r->frame->context.origin=view->origin; memcpy(r->frame->context.axis,view->axis,sizeof(view->axis));
        if(r->baseline) { r->baseline->context.origin=view->origin; memcpy(r->baseline->context.axis,view->axis,sizeof(view->axis)); }
        ok=q3remote_component_open(r,e);
        if(ok&&(!r->advanced||r->draw_sequence!=sequence)) {
            ok=r->frontend.begin(r->frontend.owner,sequence,e);
            while(ok&&r->events) {
                remote_component_event *event=r->events;
                ok=application_q3_scene_consume(r->scene,&event->value,event->sequence,e);
                if(ok) { r->events=event->next; free(event); }
            }
            if(ok) ok=application_q3_scene_advance(r->scene,sequence,e);
            if(ok) {
                q3remote_component_admissions_clear(r); r->draw_sequence=sequence; r->advanced=true; r->submitted=false;
                if(!r->profile->has_hud) { r->previous_frame_time=r->renderer_time; r->previous_frame_present=true; }
            }
        }
    }
    o->busy=false; return ok;
}
static bool packets(remote_component *r,size_t *count,qa_error *e)
{
    uint64_t identity=0;
    if(!r->advanced||!r->frontend.identity_read(r->frontend.owner,&identity))
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component packet lost its actual completed renderer");
    return frontend_component_scene_packet_count(r->parent->frontend,identity,r->draw_sequence,count,e);
}
bool q3remote_component_packet_read(remote_component *r,size_t ordinal,frontend_component_scene_packet *out,qa_error *e)
{
    uint64_t identity=0;
    if(!r->frontend.identity_read(r->frontend.owner,&identity))
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component packet lost its actual registry identity");
    return frontend_component_scene_packet_read(r->parent->frontend,identity,r->draw_sequence,ordinal,out,e);
}
bool frontend_unified_components_world(frontend_unified_components *o,const qa_scene_view *view,
    const qa_scene_world_input *world,qa_scene_frame *frame,qa_error *e)
{
    if(!world||!frame) return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component world requires its recipient frame");
    frontend_unified_recipient_clock clock;
    if(!frontend_remote_unified_clock_read(o->replica,&clock,e)||
        !frontend_unified_components_prepare_draw(o,view,frame->sequence,&clock,e)) return false;
    for(size_t i=0;i<o->count;++i) {
        remote_component *r=o->rows[i]; if(!r->frame) continue; size_t count;
        if(!packets(r,&count,e)) return false;
        if(count&&!r->submitted) return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component packets have not entered their real recipient submission");
    }
    return true;
}
bool frontend_unified_components_status_replacement(const frontend_unified_components *o,bool *out,qa_error *e)
{
    if(!out || !frontend_unified_components_current(o) || !frontend_unified_components_idle(o))
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component status requires its actual returned owners");
    *out=false;
    for(size_t i=0;i<o->count;++i) {
        const remote_component *r=o->rows[i];
        if(r->scene && r->frame && r->profile->has_hud && r->profile->replace_status)*out=true;
    }
    return true;
}

bool frontend_unified_components_hud(frontend_unified_components *o,qa_ui *ui,qa_scene_rect viewport,qa_scene_frame *frame,qa_error *e)
{
    (void)ui; (void)viewport;
    if(!frontend_unified_components_current(o)||!frontend_unified_components_idle(o)||!frame)
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component HUD requires its real returned scene owners");
    o->busy=true; bool ok=true;
    for(size_t i=0;ok&&i<o->count;++i) if(o->rows[i]->scene&&o->rows[i]->profile->has_hud) {
        remote_component *r=o->rows[i];
        ok=application_q3_scene_hud(r->scene,frame->sequence,e);
        if(ok) { r->previous_frame_time=r->renderer_time; r->previous_frame_present=true; }
    }
    o->busy=false; return ok;
}
bool frontend_unified_components_pictures(frontend_unified_components *o,qa_q3_presentation *recipient,qa_scene_frame *frame,qa_error *e)
{
    if(!frontend_unified_components_current(o)||!frontend_unified_components_idle(o)||!frame)
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component pictures require their returned recipient frame");
    for(size_t i=0;i<o->count;++i) {
        remote_component *r=o->rows[i]; if(!r->scene||!r->frame||!r->advanced) continue;
        size_t count=0;
        if(!frontend_component_scene_picture_count(o->frontend,r->frontend_identity,r->draw_sequence,&count,e)) return false;
        if(!r->pictures_present||r->picture_sequence!=frame->sequence) {
            r->pictures_present=true; r->picture_sequence=frame->sequence; r->pictures_submitted=0;
        }
        if(r->pictures_submitted>count)
            return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component picture continuation moved backwards");
        while(r->pictures_submitted<count) {
            qa_q3_picture_receipt picture;
            if(!recipient) return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component picture has no actual primary HUD recipient");
            if(!frontend_component_scene_picture_read(o->frontend,r->frontend_identity,r->draw_sequence,r->pictures_submitted,&picture,e)||
                !qa_q3_presentation_completed_picture(recipient,&picture,frame,e)) return false;
            ++r->pictures_submitted;
        }
    }
    return true;
}
