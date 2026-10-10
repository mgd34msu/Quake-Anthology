#include "internal.h"
#include "unified_q3_runtime_services.h"
#include "../application/native_q3_client_settings.h"
#include "remote_unified_presentation.h"
#include "remote_unified_save.h"
#include "q3_render_policy.h"
#include "qa/network_q3.h"
#include "qa/console_cvars_prepare.h"
#include "qa/scene_marks.h"
#include "qa/material_source_scratch.h"
#include "../../presentation/q3_native/server_commands_internal.h"
#include "../../presentation/q3/internal.h"
#include <math.h>
#include <limits.h>
#include <stdio.h>

struct frontend_unified_q3_runtime_services {
    frontend_unified_q3_runtime_services_options options;
    q3n_compiled_source *source;
    qa_q3_presentation_assets *assets;
    qa_vfs *files;
    qa_font_library *fonts;
    const qa_product *content_product;
    qa_actor_owner provider;
    qa_q3_product product;
    q3n_media *media;
    q3n_clients *clients;
    uint32_t physical_seat;
};
static bool fail(qa_error *e,const char *text)
{ qa_error_set(e,QA_ERROR_ARGUMENT,0,"%s",text);return false; }
static bool tuple_current(const frontend_unified_q3_runtime_services *o,bool checkpoint)
{
    const frontend_unified_q3_client_frame *stage=checkpoint && o && o->options.checkpoint_frame?
        o->options.checkpoint_frame(o->options.operations.context):NULL;
    if(!o || !o->source || !(checkpoint?(stage?
        frontend_unified_q3_client_checkpoint_stage_current(o->options.client,stage):
        frontend_unified_q3_client_checkpoint_current(o->options.client)):
        frontend_unified_q3_client_current(o->options.client)) ||
        !(checkpoint?frontend_remote_unified_checkpoint_current(o->options.replica,NULL):
        frontend_unified_media_current(o->options.media)) ||
        frontend_unified_media_recipe(o->options.media)!=frontend_remote_unified_recipe(o->options.replica))return false;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->options.replica);
    q3n_compiled_source_view source;qa_q3_presentation_assets *assets=NULL;
    return d && d->application==o->options.frontend->application && d->physical_seat==o->physical_seat &&
        frontend_unified_media_q3_assets_read(o->options.media,o->content_product,&assets) && assets==o->assets &&
        (checkpoint?q3n_compiled_source_checkpoint_read(o->source,&source,NULL):
        q3n_compiled_source_read(o->source,&source,NULL)) && source.basis.receiver==o->options.receiver &&
        source.basis.application==d->application && source.basis.assets==o->assets && source.basis.content==o->files &&
        source.basis.physical_seat==o->physical_seat &&
        source.basis.provider==o->provider && source.basis.product==o->product &&
        (checkpoint?q3n_compiled_source_checkpoint_current(&source):q3n_compiled_source_current(&source));
}
bool frontend_unified_q3_runtime_services_current(const frontend_unified_q3_runtime_services *o)
{ return tuple_current(o,false); }
static bool cut(frontend_unified_q3_runtime_services *o,const q3n_frame *f,qa_error *e)
{
    return o && f && f->compiled && f->compiled->source.owner==o->source && f->assets==o->assets &&
        f->application==o->options.frontend->application && q3n_frame_current(f) &&
        frontend_unified_q3_runtime_services_current(o) ? true:fail(e,"Compiled CG service lost its actual entered CLIENT");
}
static bool cvar(void *context,qa_native_q3_cvar_id id,qa_native_q3_client_cvar *out,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;
    return frontend_unified_q3_runtime_services_current(o) &&
        frontend_unified_q3_client_cvar_read(o->options.client,id,out,e); }
static bool compiled_current(void *context,const q3n_frame *f,qa_cvars *registry,const qa_command_context *origin)
{
    frontend_unified_q3_runtime_services *o=context;
    const qa_command_context *actual=o?frontend_unified_q3_client_context(o->options.client):NULL;
    return actual && origin && registry==frontend_unified_q3_client_cvars(o->options.client) &&
        origin->owner==actual->owner && origin->session==actual->session && origin->client==actual->client &&
        origin->seat==actual->seat && origin->dialect==actual->dialect && origin->origin==actual->origin &&
        origin->direct==actual->direct && origin->console_text==actual->console_text &&
        origin->registry==actual->registry && origin->generation==actual->generation &&
        ((!origin->script && !actual->script) || (origin->script && actual->script && !strcmp(origin->script,actual->script))) &&
        qa_actor_id_equal(origin->actor,actual->actor) && cut(o,f,NULL);
}
static bool console(void *context,const q3n_frame *f,const char *text,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;
    const frontend_remote_unified_domain *d=o?frontend_remote_unified_domain_read(o->options.replica):NULL;
    return d && text && cut(o,f,e) && qa_console_append(d->console,
        frontend_unified_q3_client_context(o->options.client),text,e) && cut(o,f,e);
}
static bool registered(void *context,const q3n_frame *f,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;
    return cut(o,f,e) && o->options.operations.commands.compiled_register(
        o->options.operations.commands.context,f,e) && cut(o,f,e); }
static void print(void *context,const char *text)
{
    frontend_unified_q3_runtime_services *o=context;
    const frontend_remote_unified_domain *d=o?frontend_remote_unified_domain_read(o->options.replica):NULL;
    if(d && text && frontend_unified_q3_runtime_services_current(o))
        qa_console_emit(d->console,frontend_unified_q3_client_context(o->options.client),text);
}
static int32_t milliseconds(void *context)
{
    frontend_unified_q3_runtime_services *o=context;
    uint32_t word=(uint32_t)(o->options.frontend->wall_time_ns/UINT64_C(1000000));
    int32_t result;memcpy(&result,&word,sizeof(result));return result;
}
static double clock_time(void *context)
{ frontend_unified_q3_runtime_services *o=context;return (double)o->options.frontend->wall_time_ns/1e6; }
static int32_t frame_number(void *context)
{ frontend_unified_q3_runtime_services *o=context;uint32_t word=(uint32_t)o->options.frontend->frame_number;
    int32_t result;memcpy(&result,&word,sizeof(result));return result; }
static uint64_t audio_bus(void *context)
{ return ((frontend_unified_q3_runtime_services *)context)->options.audio_owner; }
static bool prepare_view(void *context,const qa_q3_refdef *refdef,qa_q3_scene_options *out,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;
    frontend_unified_media_world_scratch(o->options.media,&out->world);
    const qa_q3_presentation_options *actual=&o->options.operations.presentation;
    qa_native_q3_client_cvar shadows;
    if(!frontend_unified_q3_runtime_services_current(o) ||
        (actual->prepare_view && !actual->prepare_view(actual->context,refdef,out,e)) ||
        !frontend_q3_scene_policy_read(o->options.frontend,out,e) || !cvar(o,QA_NATIVE_Q3_CVAR_cg_shadows,&shadows,e))return false;
    out->shadow_mode=(uint32_t)shadows.integer;
    return frontend_unified_q3_runtime_services_current(o);
}
static bool prepare_picture(void *context,qa_material_context *out,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;
    const qa_q3_presentation_options *actual=&o->options.operations.presentation;
    return frontend_unified_q3_runtime_services_current(o) &&
        (!actual->prepare_picture || actual->prepare_picture(actual->context,out,e)) &&
        frontend_q3_material_diagnostics_read(o->options.frontend,&out->source_diagnostics,e) &&
        frontend_unified_q3_runtime_services_current(o);
}
static bool submit_view(void *context,const qa_q3_scene_options *options,qa_scene_frame *frame,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;
    const qa_q3_presentation_options *actual=&o->options.operations.presentation;
    return frontend_unified_q3_runtime_services_current(o) &&
        actual->submit_view(actual->context,options,frame,e) && frontend_unified_q3_runtime_services_current(o);
}
static qa_material_source_scratch *source_state(void *context,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;
    const qa_q3_presentation_options *actual=&o->options.operations.presentation;
    if(!frontend_unified_q3_runtime_services_current(o))return NULL;
    qa_material_source_scratch *state=actual->source_state(actual->context,e);
    return frontend_unified_q3_runtime_services_current(o)?state:NULL;
}
static bool remap(void *context,const char *from,const char *to,float offset,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;
    const qa_q3_presentation_options *actual=&o->options.operations.presentation;
    return frontend_unified_q3_runtime_services_current(o) && actual->remap(actual->context,from,to,offset,e) &&
        frontend_unified_q3_runtime_services_current(o);
}
static bool scene_completed(void *context,const qa_q3_refdef *refdef,const qa_q3_scene_options *options,
    const qa_q3_ref_entity *entities,size_t entity_count,const qa_q3_scene_polygon *polygons,size_t polygon_count,
    const qa_scene_vertex *vertices,size_t vertex_count,const qa_scene_light *lights,size_t light_count,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;
    const qa_q3_presentation_options *actual=&o->options.operations.presentation;
    return frontend_unified_q3_runtime_services_current(o) && actual->scene_completed(actual->context,refdef,options,
        entities,entity_count,polygons,polygon_count,vertices,vertex_count,lights,light_count,e) &&
        frontend_unified_q3_runtime_services_current(o);
}
static void scene_cleared(void *context)
{
    frontend_unified_q3_runtime_services *o=context;
    const qa_q3_presentation_options *actual=&o->options.operations.presentation;
    actual->scene_cleared(actual->context);
}
static bool hud_command(void *context,const q3n_frame *f,const char *text,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;const q3n_hud_options *h=&o->options.operations.hud;
    return text && cut(o,f,e) && h->client_command(h->context,f,text,e) && cut(o,f,e);
}
static bool oldest_command(void *context,const q3n_frame *f,int32_t *time,bool *available,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;const q3n_hud_options *h=&o->options.operations.hud;
    return cut(o,f,e) && h->compiled_oldest_command(h->context,f,time,available,e) && cut(o,f,e); }
static bool weapon_warning(void *context,const q3n_frame *f,q3n_weapon_hud *out,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;const q3n_player_state_options *p=&o->options.operations.player_state;
    return cut(o,f,e) && p->weapon_warning(p->context,f,out,e) && cut(o,f,e); }
static bool hud_warning(void *context,const q3n_frame *f,q3n_weapon_hud *out,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;const q3n_hud_options *h=&o->options.operations.hud;
    return cut(o,f,e) && h->weapon_warning(h->context,f,out,e) && cut(o,f,e); }
static bool event_replace(void *context,const q3n_frame *f,q3n_entity *cent,const qa_q3_entity *scratch,
    qa_vec3 position,bool *suppressed,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;const q3n_event_options *v=&o->options.operations.events;
    return cut(o,f,e) && v->event_replacement(v->context,f,cent,scratch,position,suppressed,e) && cut(o,f,e); }
static void local_allocated(void *context,int32_t slot)
{ frontend_unified_q3_runtime_services *o=context;const q3n_event_options *v=&o->options.operations.events;
    v->local_allocated(v->context,slot); }
static bool update_loading(void *context,q3n_loading *loading,const q3n_frame *f,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;qa_frontend *frontend=o->options.frontend;
    if(!cut(o,f,e) || frontend->capture || frontend->source_restoring)return false;
    qa_scene_frame_reset(&frontend->frame,frontend->frame_number);frontend->frame.source_backend=true;
    if(!qa_q3_presentation_frame(f->presentation,&frontend->frame,frontend_viewport(frontend,o->physical_seat),e) ||
        !q3n_loading_draw_information(loading,f,e) || !frontend_render_controls_live(frontend,e) || !cut(o,f,e))return false;
    if(frontend->frame.source_skip_backend)return
        (!frontend->frame.source_pending || qa_material_source_frame_end(frontend->frame.source_pending,&frontend->frame,false,e)) && cut(o,f,e);
    bool okay=frontend->cpu?(qa_cpu_execute(frontend->cpu,&frontend->frame,e) && qa_cpu_present_frame(frontend->cpu,e)):
        (frontend->gl && qa_gl_execute(frontend->gl,&frontend->frame,e) && qa_gl_swap(frontend->gl,e));
    return okay && cut(o,f,e);
}
static bool music(void *context,const char *intro,const char *loop,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;const qa_q3_presentation_options *actual=&o->options.operations.presentation;
    return frontend_unified_q3_runtime_services_current(o) && actual->music(actual->context,intro,loop,e) &&
        frontend_unified_q3_runtime_services_current(o); }
static bool listener(void *context,const qa_audio_listener *value,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;const qa_q3_presentation_options *actual=&o->options.operations.presentation;
    return frontend_unified_q3_runtime_services_current(o) && actual->listener(actual->context,value,e) &&
        frontend_unified_q3_runtime_services_current(o); }
static bool system_movie(void *context,const qa_q3_movie_request *request,qa_q3_system_movie *out,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;const qa_q3_presentation_options *actual=&o->options.operations.presentation;
    return frontend_unified_q3_runtime_services_current(o) && actual->system_movie(actual->context,request,out,e) &&
        frontend_unified_q3_runtime_services_current(o); }
static bool set_number(frontend_unified_q3_runtime_services *o,const char *name,float value,qa_error *e)
{
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->options.replica);
    const qa_command_context *context=frontend_unified_q3_client_context(o->options.client);
    qa_cvars_edit_command command={.kind=QA_CVARS_EDIT_SET_NUMBER,.name=name,.number=value};
    return d && context && frontend_unified_q3_runtime_services_current(o) &&
        qa_console_cvar_apply(d->console,context,&command,e) && frontend_unified_q3_runtime_services_current(o);
}
static bool view_size(void *context,int32_t size,qa_error *e)
{ return set_number(context,"cg_viewsize",(float)size,e); }
static bool orbit_angle(void *context,float angle,qa_error *e)
{ return set_number(context,"cg_thirdPersonAngle",angle,e); }
static bool key_catcher(void *context,int32_t mask,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;
    return mask>=0 && frontend_unified_q3_runtime_services_current(o) && qa_input_seat_set_catcher(
        o->options.frontend->seats[o->physical_seat].input,o->options.receiver,(uint32_t)mask,e);
}
static bool trace(void *context,const q3n_frame *f,qa_vec3 start,qa_vec3 end,qa_bounds bounds,
    int32_t skip,uint32_t mask,qa_trace_result *out,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;if(!cut(o,f,e))return false;
    qa_actor_id pass={0};
    if(skip>=0 && skip<1022){q3n_compiled_entity row;
        if(!q3n_compiled_frame_entity(f->compiled,(uint32_t)skip,&row,e))return false;
        if(row.published)pass=row.actor;}
    qa_trace_query query={.start=start,.end=end,.shape={.kind=QA_SHAPE_BOX,.bounds=bounds},
        .policy={.family=QA_COLLISION_Q3,.contents_mask=qa_collision_contents_mask(mask,QA_COLLISION_Q3),.curves=true},.pass_actor=pass};
    return frontend_remote_unified_presentation_trace(o->options.replica,&query,out,e) && cut(o,f,e);
}
static bool world_trace(void *context,const q3n_frame *f,qa_vec3 start,qa_vec3 end,qa_bounds bounds,
    uint32_t mask,qa_trace_result *out,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;if(!cut(o,f,e))return false;
    qa_trace_query query={.start=start,.end=end,.shape={.kind=QA_SHAPE_BOX,.bounds=bounds},
        .policy={.family=QA_COLLISION_Q3,.contents_mask=qa_collision_contents_mask(mask,QA_COLLISION_Q3),.curves=true}};
    qa_collision_geometry *geometry=(qa_collision_geometry *)frontend_remote_unified_geometry(o->options.replica);
    return geometry && qa_collision_trace(geometry,frontend_remote_unified_presentation_trace_scratch(o->options.replica),&query,out,e) && cut(o,f,e);
}
static bool contents(void *context,const q3n_frame *f,qa_vec3 point,int32_t pass,uint32_t *out,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;if(!out || !cut(o,f,e))return false;
    qa_point_query query={.point=point,.policy={.family=QA_COLLISION_Q3,.curves=true}};
    if(pass>=0 && pass<1022){q3n_compiled_entity row;
        if(!q3n_compiled_frame_entity(f->compiled,(uint32_t)pass,&row,e))return false;
        if(row.published)query.pass_actor=row.actor;}
    qa_point_contents result;
    if(!frontend_remote_unified_presentation_point_contents(o->options.replica,&query,&result,e) || !cut(o,f,e))return false;
    *out=(uint32_t)qa_collision_point_contents_export(result.contents,QA_COLLISION_Q3,result.q1_opaque_token);return true;
}
static bool world_contents(void *context,const q3n_frame *f,qa_vec3 point,uint32_t *out,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;if(!out || !cut(o,f,e))return false;
    qa_point_query query={.point=point,.policy={.family=QA_COLLISION_Q3,.curves=true}};
    qa_collision_geometry *geometry=(qa_collision_geometry *)frontend_remote_unified_geometry(o->options.replica);
    qa_point_contents result;
    if(!geometry || !qa_collision_point_contents(geometry,frontend_remote_unified_presentation_trace_scratch(o->options.replica),&query,&result,e) || !cut(o,f,e))return false;
    *out=(uint32_t)qa_collision_point_contents_export(result.contents,QA_COLLISION_Q3,result.q1_opaque_token);return true;
}
static bool fragments(void *context,const q3n_frame *f,const qa_vec3 *points,size_t count,
    qa_vec3 projection,qa_vec3 *vertices,size_t capacity,q3n_mark_fragment *out,size_t fragment_capacity,
    size_t *returned,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;
    if(!returned || fragment_capacity>SIZE_MAX/sizeof(qa_scene_mark_fragment) || !cut(o,f,e))return false;
    qa_scene_mark_fragment *rows=fragment_capacity?calloc(fragment_capacity,sizeof(*rows)):NULL;
    if(fragment_capacity && !rows){qa_error_set(e,QA_ERROR_MEMORY,0,"Preparing compiled Source mark fragments");return false;}
    qa_scene_mark_result result={0};qa_scene_world *world=frontend_unified_media_world(o->options.media);
    bool okay=world && qa_scene_world_mark_fragments(world,points,count,projection,vertices,capacity,
        rows,fragment_capacity,&result,e) && cut(o,f,e);
    for(size_t i=0;okay && i<result.fragment_count;++i){
        okay=rows[i].first_point<=UINT32_MAX && rows[i].point_count<=UINT32_MAX;
        if(okay)out[i]=(q3n_mark_fragment){(uint32_t)rows[i].first_point,(uint32_t)rows[i].point_count};}
    free(rows);if(okay)*returned=result.fragment_count;return okay;
}
static bool body_hidden(void *context,const q3n_frame *f,const q3n_compiled_entity *row,bool *out,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;const q3n_player_fx_compiled_backend *b=&o->options.operations.player_fx;
    return cut(o,f,e) && b->body_hidden(b->context,f,row,out,e) && cut(o,f,e); }
static bool body_submit(void *context,const q3n_frame *f,const q3n_compiled_entity *row,uint32_t part,
    const qa_q3_ref_entity *ref,bool base,bool *consumed,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;const q3n_player_fx_compiled_backend *b=&o->options.operations.player_fx;
    return cut(o,f,e) && b->body_submit(b->context,f,row,part,ref,base,consumed,e) && cut(o,f,e); }
static bool player_weapon(void *context,const q3n_frame *f,const q3n_compiled_entity *row,
    const qa_q3_ref_entity *torso,int32_t team,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;const q3n_player_fx_compiled_backend *b=&o->options.operations.player_fx;
    return cut(o,f,e) && b->player_weapon(b->context,f,row,torso,team,e) && cut(o,f,e); }
static bool command_message(void *context,const q3n_command_message *message,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;
    if(!message || !message->text || !cut(o,message->frame,e))return false;
    print(o,message->text);return cut(o,message->frame,e);
}
static bool initialize_stage(void *context,const q3n_frame *f,q3n_command_init_stage stage,
    const char *map,int32_t physical,uint32_t *extent,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;const q3n_server_command_options *c=&o->options.operations.commands;
    return cut(o,f,e) && c->initialize_stage(c->context,f,stage,map,physical,extent,e) && cut(o,f,e); }

static bool video_shutdown(void *context,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;return o->options.operations.video_shutdown(
    o->options.operations.context,e); }
static bool audio_actor(void *context,int32_t number,uint64_t *out,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;
    if(!out || !frontend_unified_q3_runtime_services_current(o))return false;
    if(number<0 || number==1022 || number==1023){*out=QA_AUDIO_NO_ACTOR;return true;}
    const q3n_compiled_frame *frame=o->options.entered_frame(o->options.operations.context);
    q3n_compiled_entity entity;
    if(!frame || frame->source.owner!=o->source || !q3n_compiled_frame_current(frame) ||
        !q3n_compiled_frame_entity(frame,(uint32_t)number,&entity,e))
        return fail(e,"CG audio actor requires the actual entered snapshot entity");
    uint64_t id=QA_AUDIO_NO_ACTOR;
    if(entity.published && entity.actor.registry &&
        !o->options.audio_actor(o->options.audio_context,entity.actor,&id,e))return false;
    if(o->options.entered_frame(o->options.operations.context)!=frame || !q3n_compiled_frame_current(frame) ||
        !frontend_unified_q3_runtime_services_current(o))return fail(e,"CG audio actor left its entered snapshot");
    *out=id;return true;
}
static int32_t memory_remaining(void *context)
{
    (void)context;
    return qa_memory_available();
}
static bool preferences(void *context,qa_ui_preferences *out,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;
    return frontend_unified_q3_runtime_services_current(o) && qa_ui_preferences_read(
        qa_application_cvars(o->options.frontend->application),qa_application_ui_preference_handles(o->options.frontend->application),o->physical_seat,out,e); }
static bool backend_frame(void *context,qa_q3_presentation *backend,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;
    return frontend_unified_q3_runtime_services_current(o) && qa_q3_presentation_frame(backend,
        &o->options.frontend->frame,frontend_viewport(o->options.frontend,o->physical_seat),e); }
static bool renderer_name(const char *text,const char *name)
{
    for(;*text;++text){const char *a=text,*b=name;
        while(*a && *b){unsigned char c=(unsigned char)*a++;if(c>='A' && c<='Z')c=(unsigned char)(c+32);
            if(c!=(unsigned char)*b)break;
            ++b;}
        if(!*b)return true;}
    return false;
}
static bool ragepro(frontend_unified_q3_runtime_services *o)
{ const qa_gl_capabilities *caps=qa_gl_capabilities_get(o->options.frontend->gl);
    return caps && !renderer_name(caps->renderer,"banshee") && !renderer_name(caps->renderer,"voodoo_graphics") &&
        (renderer_name(caps->renderer,"rage pro") || renderer_name(caps->renderer,"ragepro")); }
static bool settings_cvar(const void *context,qa_native_q3_cvar_id id,qa_native_q3_client_cvar *out,qa_error *e)
{ frontend_unified_q3_runtime_services *o=(frontend_unified_q3_runtime_services *)context;
    return frontend_unified_q3_runtime_services_current(o) && frontend_unified_q3_client_cvar_read(o->options.client,id,out,e); }
static bool frame_settings(void *context,const q3n_compiled_frame *f,bool loading,uint32_t stereo,
    q3n_native_frame_options *out,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;
    if(!out || stereo>2 || !f || f->source.owner!=o->source || !q3n_compiled_frame_current(f) ||
        !frontend_unified_q3_runtime_services_current(o))return fail(e,"CG settings lost their current compiled source");
    const char *server;uint64_t revision;char flags[8192];
    if(!q3n_compiled_source_configstring(o->source,0,&server,&revision,e) ||
        !qa_q3_info_value(server,"dmflags",flags,sizeof(flags),e))return false;
    application_q3_client_settings_source source={.context=o,.read=settings_cvar,
        .cvars=frontend_unified_q3_client_cvars(o->options.client),
        .refs=frontend_unified_q3_client_cvar_refs(o->options.client),.product=o->product};
    q3n_native_frame_options v;
    if(!application_q3_client_frame_settings(&source,q3nc_integer(flags),ragepro(o),
        (size_t)memory_remaining(o),loading,false,stereo,&v,e) ||
        !frontend_unified_q3_client_local_server_read(o->options.client,&v.hud.local_server,e))return false;
    if(o->options.operations.frame_settings &&
        !o->options.operations.frame_settings(o->options.operations.context,f,loading,stereo,&v,e))return false;
    if(!q3n_compiled_frame_current(f) || !frontend_unified_q3_runtime_services_current(o))return false;
    *out=v;return true;
}

static bool runtime_current(void *context,const frontend_unified_q3_runtime_options *v)
{ frontend_unified_q3_runtime_services *o=context;
    return v && v->frontend==o->options.frontend && v->replica==o->options.replica &&
        v->client==o->options.client && v->presentation.assets==o->assets &&
        frontend_unified_q3_runtime_services_current(o) &&
        o->options.operations.current(o->options.operations.context,&o->options.operations); }
static bool command_values(void *context,int32_t weapon,float sensitivity,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;return frontend_unified_q3_runtime_services_current(o) &&
    o->options.operations.command_values(o->options.operations.context,weapon,sensitivity,e); }
static bool trace_number(void *context,const q3n_compiled_frame *f,const qa_trace_result *trace,int32_t *out,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;return f && f->source.owner==o->source && q3n_compiled_frame_current(f) &&
    o->options.operations.trace_number(o->options.operations.context,f,trace,out,e); }
static bool timescale(void *context,int32_t elapsed,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;return frontend_unified_q3_runtime_services_current(o) &&
    o->options.operations.timescale(o->options.operations.context,elapsed,e); }
bool frontend_unified_q3_runtime_services_create(const frontend_unified_q3_runtime_services_options *options,
    frontend_unified_q3_runtime_services **out,qa_error *e)
{
    if(!options || !out || *out || !options->frontend || !options->replica || !options->media || !options->client || !options->events ||
        !options->receiver || !options->audio_owner || !options->audio_actor || !options->entered_frame ||
        !options->source.provider || !options->source.content ||
        !options->operations.current || !options->operations.command_values ||
        !options->operations.trace_number || !options->operations.timescale ||
        !options->operations.presentation.music ||
        !options->operations.presentation.listener ||
        !options->operations.commands.compiled_register || !options->operations.commands.initialize_stage ||
        !options->operations.player_fx.body_hidden || !options->operations.player_fx.body_submit ||
        !options->operations.player_fx.player_weapon ||
        !options->operations.hud.compiled_oldest_command || !options->operations.hud.client_command ||
        !frontend_unified_q3_client_matches(options->client,&options->source))
        return fail(e,"CG services require the genuine CLIENT, media and input/prediction producers");
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(options->replica);
    if(!d || d->application!=options->frontend->application || d->physical_seat>=options->frontend->options.seats ||
        !options->frontend->seats[d->physical_seat].input)
        return fail(e,"CG services lost their actual physical frontend seat");
    frontend_unified_q3_runtime_services *o=calloc(1,sizeof(*o));
    if(!o){qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining compiled CG services");return false;}
    o->options=*options;o->source=frontend_unified_q3_client_source(options->client);
    o->options.operations.frontend=options->frontend;o->options.operations.replica=options->replica;
    o->options.operations.client=options->client;
    o->assets=options->source.assets;o->files=options->source.files;o->physical_seat=d->physical_seat;
    o->provider=options->source.provider->source_owner;o->product=options->source.product;
    bool bank_found=false;
    for(size_t i=0;i<frontend_unified_media_bank_count(options->media);++i){
        frontend_unified_bank_view bank;
        if(!frontend_unified_media_bank_read(options->media,i,&bank))break;
        if(!strcmp(bank.content,options->source.content) && bank.q3_assets==o->assets && bank.files==o->files){
            o->fonts=bank.fonts;o->content_product=bank.product;bank_found=true;break;}
    }
    /* Only the admitted constructor observes the received row. Every later
     * operation reads the real retained CLIENT and bank instead. */
    o->options.source=(frontend_unified_q3_source_view){0};
    if(!bank_found || !o->fonts || !tuple_current(o,false)){
        free(o);return fail(e,"CG services require their already retained content bank");}
    q3n_media_options media={.assets=o->assets,.product=options->source.product,.compiled_source=o->source};
    q3n_client_options clients={.assets=o->assets,.content=o->files,.product=options->source.product,
        .compiled_source=o->source,.context=o,.print=print};
    if(!q3n_media_create(&media,&o->media,e) || !q3n_clients_create_compiled(&clients,&o->clients,e)){
        q3n_clients_destroy(o->clients);q3n_media_destroy(o->media);free(o);return false;}
    *out=o;return true;
}
bool frontend_unified_q3_runtime_services_read(frontend_unified_q3_runtime_services *o,
    frontend_unified_q3_runtime_options *out,qa_error *e)
{
    q3n_compiled_source_view source;
    if(!out || !o || !tuple_current(o,false) || !q3n_compiled_source_read(o->source,&source,e))
        return fail(e,"CG options require their actual current service owner");
    const qa_command_context *origin=frontend_unified_q3_client_context(o->options.client);
    qa_cvars *registry=frontend_unified_q3_client_cvars(o->options.client);
    if(!origin || !registry)return fail(e,"CG options lost the retained CLIENT command namespace");
    frontend_unified_q3_runtime_options v=o->options.operations;
    v.frontend=o->options.frontend;v.replica=o->options.replica;v.client=o->options.client;
    v.media=o->media;v.clients=o->clients;
    v.context=o;v.current=runtime_current;v.frame_settings=frame_settings;v.trace_number=trace_number;
    v.command_values=command_values;v.timescale=timescale;
    v.preferences=preferences;v.backend_frame=backend_frame;
    v.video_shutdown=o->options.operations.video_shutdown?video_shutdown:NULL;
    v.presentation.assets=o->assets;v.presentation.audio=o->options.frontend->audio;
    v.presentation.owner=o->options.audio_owner;v.presentation.seat=o->physical_seat;
    v.presentation.viewport=frontend_viewport(o->options.frontend,o->physical_seat);
    if(!frontend_q3_renderer_options_read(o->options.frontend,&v.presentation,e))return false;
    v.presentation.context=o;v.presentation.frame_number=frame_number;v.presentation.milliseconds=milliseconds;
    v.presentation.audio_bus=audio_bus;v.presentation.audio_actor=audio_actor;
    v.presentation.prepare_view=prepare_view;v.presentation.prepare_picture=prepare_picture;
    v.presentation.submit_view=o->options.operations.presentation.submit_view?submit_view:NULL;
    if(v.presentation.source_state)v.presentation.source_state=source_state;
    if(v.presentation.remap)v.presentation.remap=remap;
    if(v.presentation.scene_completed)v.presentation.scene_completed=scene_completed;
    if(v.presentation.scene_cleared)v.presentation.scene_cleared=scene_cleared;
    v.presentation.music=music;v.presentation.listener=listener;
    v.presentation.system_movie=o->options.operations.presentation.system_movie?system_movie:NULL;v.presentation.print=print;
    v.presentation.clock=(qa_media_clock){o,clock_time};
    v.view.application=v.player_state.application=v.hud.application=v.loading.application=v.mission.application=
        v.commands.application=o->options.frontend->application;
    v.view.compiled_source=v.player_state.compiled_source=v.hud.compiled_source=v.loading.compiled_source=
        v.mission.compiled_source=v.commands.compiled_source=o->source;
    v.view.assets=v.player_state.assets=v.hud.assets=v.loading.assets=v.mission.assets=v.commands.assets=o->assets;
    v.view.seat=v.player_state.seat=v.hud.seat=v.loading.seat=v.mission.seat=source.basis.seat;
    v.hud.presentation_seat=v.loading.presentation_seat=o->physical_seat;
    v.hud.ui=v.loading.ui=o->options.frontend->seats[o->physical_seat].ui;
    v.hud.messages=o->options.frontend->seats[o->physical_seat].hud;
    v.view.context=o;v.view.print=print;v.player_state.context=o;v.player_state.print=print;
    v.view.set_view_size=view_size;v.view.set_third_person_angle_value=orbit_angle;
    if(v.player_state.weapon_warning)v.player_state.weapon_warning=weapon_warning;
    v.hud.context=o;v.hud.milliseconds=milliseconds;v.hud.client_command=hud_command;
    v.hud.oldest_command=NULL;v.hud.compiled_oldest_command=oldest_command;
    if(v.hud.weapon_warning)v.hud.weapon_warning=hud_warning;
    v.loading.context=o;v.loading.update_screen=update_loading;
    v.commands.context=o;v.commands.content=v.mission.content=o->files;
    v.commands.compiled_cvars=v.mission.compiled_cvars=v.loading.compiled_cvars=registry;
    v.commands.compiled_context=v.mission.compiled_context=*origin;
    v.commands.compiled_current=v.mission.compiled_current=compiled_current;
    v.commands.compiled_cvar_read=v.mission.compiled_cvar_read=cvar;
    v.commands.compiled_console=v.mission.compiled_console=console;
    v.commands.compiled_register=registered;v.commands.memory_remaining=memory_remaining;
    v.commands.message=command_message;v.commands.initialize_stage=initialize_stage;
    v.mission.context=o;v.mission.fonts=o->fonts;v.mission.print=print;v.mission.milliseconds=milliseconds;
    v.mission.key_catcher=key_catcher;
    v.weapons.assets=v.events.assets=o->assets;v.weapons.product=v.events.product=v.commands.product=source.basis.product;
    v.events.context=o;v.events.compiled_source=o->source;v.events.trace=trace;v.events.point_contents=contents;
    v.events.mark_fragments=fragments;v.events.print=print;
    if(v.events.event_replacement)v.events.event_replacement=event_replace;
    if(v.events.local_allocated)v.events.local_allocated=local_allocated;
    v.player_fx.context=o;v.player_fx.world_trace=world_trace;v.player_fx.world_point_contents=world_contents;
    v.player_fx.body_hidden=body_hidden;v.player_fx.body_submit=body_submit;v.player_fx.player_weapon=player_weapon;
    if(!tuple_current(o,false))return fail(e,"CG options lost their retained CLIENT during projection");
    *out=v;return true;
}
bool frontend_unified_q3_runtime_services_caches(const frontend_unified_q3_runtime_services *o,
    q3n_media **media,q3n_clients **clients,qa_error *e)
{
    if(!o || !media || !clients || !q3n_media_idle(o->media) || !q3n_clients_idle(o->clients))
        return fail(e,"CG cache inventory requires its returned retained owners");
    *media=o->media;*clients=o->clients;return true;
}
bool frontend_unified_q3_runtime_services_destroy(frontend_unified_q3_runtime_services **slot,qa_error *e)
{
    if(!slot || !*slot)return true;
    frontend_unified_q3_runtime_services *o=*slot;
    if(!frontend_unified_q3_client_idle(o->options.client) ||
        !q3n_media_idle(o->media) || !q3n_clients_idle(o->clients))
        return fail(e,"CG service callbacks retain their actual CLIENT parent");
    q3n_clients_destroy(o->clients);q3n_media_destroy(o->media);
    free(o);*slot=NULL;return true;
}
