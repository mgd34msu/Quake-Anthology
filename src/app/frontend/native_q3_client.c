#include "source_cinematics.h"
#include "renderer_materials.h"
#include "qa/material_source_scratch.h"
#include "q3_color_policy.h"
#include "native_q3_client_internal.h"
#include "native_composition.h"
#include "native_components.h"
#include "source_renderer_runtime.h"
#include "config_store.h"
#include "view_bindings.h"
#include "shared_resource_policy.h"
#include "shared_register.h"
#include "shared_render_controls.h"
#include "q3_render_policy.h"
#include "music_sources.h"
#include "material_movies.h"
#include "material_movie_bindings.h"
#include "qa/audio_music_prepare.h"
#include "capture.h"
#include "save_private.h"
#include "source_restore.h"
#include "visual_access.h"
#include "selected_effects.h"
#include "qc_messages.h"
#include "qa/application_q3_asset_selection.h"
#include "qa/q3_presentation_save.h"
#include "qa/q3_assets_save.h"
#include "qa/q3_assets_custody.h"
#include "qa/scene_marks.h"
#include "qa/scene_world_save.h"
#include "qa/strings.h"
#include "qa/session.h"
#include "qa/catalog_save.h"
#include "qa/scene_resource_save.h"
#include "qa/font_save.h"
#include "qa/material_library_save.h"
#include "qa/media_library_save.h"
#include "../application/native_q3_client_settings.h"
#include <limits.h>
#include <stdio.h>
static int32_t memory_remaining(void *);
static bool row_idle(const frontend_native_q3 *);
static frontend_native_q3 *row_at(const qa_frontend *,size_t);
static bool has_renderer_name(const char *name,const char *match)
{
    for(;*name;++name) {
        const char *text=name,*part=match;
        while(*text && *part) {
            unsigned char c=(unsigned char)*text++;
            if(c>='A' && c<='Z')c=(unsigned char)(c+32);
            if(c!=(unsigned char)*part)break;
            ++part;
        }
        if(!*part)return true;
    }
    return false;
}
static bool ragepro(const frontend_native_q3 *row)
{
    const qa_gl_capabilities *caps=qa_gl_capabilities_get(row->frontend->gl);
    return caps && !has_renderer_name(caps->renderer,"banshee") &&
        !has_renderer_name(caps->renderer,"voodoo_graphics") &&
        (has_renderer_name(caps->renderer,"rage pro") || has_renderer_name(caps->renderer,"ragepro"));
}

static bool linked(const frontend_native_q3 *row)
{
    if(!row || !row->frontend)return false;
    for(const frontend_native_q3 *p=row->frontend->native_q3;p;p=p->next)if(p==row)return true;
    return false;
}
bool frontend_native_q3_current(const frontend_native_q3 *row)
{
    qa_native_q3_wire_basis b;
    return linked(row) && !row->service_released && row->view.reader &&
        qa_native_q3_wire_reader_basis(row->view.reader,&b,NULL) &&
        b.application==row->frontend->application && b.receiver==row->view.receiver &&
        b.session==qa_application_session(row->frontend->application) && b.product==row->view.product &&
        b.source_owner==row->view.source_owner && b.seat==row->view.launch_seat &&
        b.physical_client==row->view.physical_client && qa_actor_id_equal(b.actor,frontend_native_q3_actor(row)) &&
        row->view.seat<row->frontend->options.seats && row->view.input &&
        frontend_client_registry_matches(row->view.registry,row->view.source_launch,row->view.launch_seat) &&
        row->view.cvars==frontend_client_registry_cvars(row->view.registry);
}
bool frontend_native_q3_cut(frontend_native_q3 *row,const q3n_frame *f,qa_error *e)
{
    return f && frontend_native_q3_current(row) && f->application==row->frontend->application &&
        f->reader==row->view.reader && f->presentation==row->view.presentation &&
        f->seat==row->view.launch_seat && f->physical_presentation_seat==row->view.seat &&
        f->viewing_client==row->view.physical_client && qa_actor_id_equal(f->viewing_actor,frontend_native_q3_actor(row)) &&
        qa_application_native_q3_presentation_current(f->application,&f->source) ? true :
        frontend_fail(e,QA_ERROR_ARGUMENT,"Native frontend callback lost its real source cut or physical seat");
}
static void print_row(void *context,const char *text)
{
    frontend_native_q3 *row=context;
    if(row->console) {
        qa_command_context origin=row->command; origin.actor=frontend_native_q3_actor(row);
        qa_console_emit(row->console,&origin,text);
    }
}
static void print_client(void *context,const char *text)
{
    const qa_native_q3_client_services *services=qa_native_q3_client_services_read(context);
    if(services)print_row(services->context,text);
}
static int32_t milliseconds(void *context)
{ return (int32_t)(uint32_t)(((frontend_native_q3 *)context)->frontend->wall_time_ns/UINT64_C(1000000)); }
static double clock_time(void *context)
{ return (double)((frontend_native_q3 *)context)->frontend->wall_time_ns/1e6; }
static int32_t frame_number(void *context)
{ return (int32_t)(uint32_t)((frontend_native_q3 *)context)->frontend->frame_number; }
static uint64_t audio_bus(void *context)
{ return ((frontend_native_q3 *)context)->view.identity; }
static bool services_current(void *context,const qa_native_q3_client_services *s)
{
    frontend_native_q3 *row=context; qa_native_q3_wire_basis b;
    return frontend_native_q3_current(row) && s && s->wire_reader==row->view.reader &&
        s->input==row->view.input && s->client.frontend_lifetime==row &&
        s->client.console==row->console && s->client.cvars==row->view.cvars &&
        s->client.service_owner==row->view.service_owner &&
        qa_native_q3_wire_reader_basis(row->view.reader,&b,NULL) &&
        s->client.source_cvars==b.source_cvars && s->publication_generation==b.publication_generation &&
        s->map_revision==b.map_revision;
}
static bool services_idle(void *context)
{
    frontend_native_q3 *row=context;
    return !row->callbacks && frontend_seat_callbacks_idle(row->frontend) &&
        qa_native_q3_wire_reader_idle(row->view.reader);
}
static bool reliable(void *context,const qa_command_context *origin,const char *text,qa_error *e)
{
    frontend_native_q3 *row=context;
    if(!frontend_native_q3_current(row) || origin->seat!=row->view.launch_seat ||
        !qa_actor_id_equal(origin->actor,frontend_native_q3_actor(row)))return frontend_fail(e,QA_ERROR_ARGUMENT,"Native reliable command lost its actual viewing actor");
    ++row->callbacks;
    bool ok=qa_native_q3_wire_reader_reliable(row->view.reader,text,e);
    --row->callbacks; return ok && frontend_native_q3_current(row);
}
static bool console_command(void *context,const qa_command_context *origin,const char *text,qa_error *e)
{
    frontend_native_q3 *row=context;
    return frontend_native_q3_current(row) && origin->seat==row->view.launch_seat &&
        qa_actor_id_equal(origin->actor,frontend_native_q3_actor(row)) && qa_console_append(row->console,origin,text,e);
}
static bool command_values(void *context,int32_t weapon,float sensitivity,qa_error *e)
{
    frontend_native_q3 *row=context;
    return frontend_native_q3_current(row) &&
        qa_native_q3_wire_reader_command_values(row->view.reader,weapon,sensitivity,e);
}
static bool reload_client(void *context,uint32_t physical,const char *text,qa_error *e)
{
    frontend_native_q3 *row=context; (void)text;
    qa_application_native_q3_presentation source; q3n_native_frame_options settings;
    return row->view.core && qa_application_native_q3_presentation_read(row->frontend->application,row->view.source_owner,&source,e) &&
        application_native_q3_client_frame_settings(row->view.client,0,ragepro(row),(size_t)memory_remaining(row),false,false,0,&settings,e) &&
        q3n_native_reload_client(row->view.core,physical,&settings.clients,e);
}
static bool status_visible(void *context)
{
    frontend_native_q3 *row=context; qa_application_presentation_view view;
    return qa_application_presentation_read(row->frontend->application,row->view.launch_seat,&view) &&
        view.hud==row->view.source_owner;
}
static void release_services(void *context)
{
    frontend_native_q3 *row=context;
    row->service_released=true; row->view.recipient=NULL;
    /* The real row keeps source registries alive through renderer teardown.
     * Service observers are already detached before this release callback. */
}
bool frontend_native_q3_service_options(frontend_native_q3 *row,qa_native_q3_client_services *out,qa_error *e)
{
    qa_native_q3_wire_basis b;
    if(!out || !frontend_native_q3_current(row) || !qa_native_q3_wire_reader_basis(row->view.reader,&b,e))return false;
    row->no_curves=qa_cvars_resolve(b.source_cvars,"cm_noCurves");
    row->player_curve_clip=qa_cvars_resolve(b.source_cvars,"cm_playerCurveClip");
    *out=(qa_native_q3_client_services){.client={.session=b.session,.receiver=b.receiver,.source_owner=b.source_owner,
        .source_actor=b.actor,.seat=b.seat,.source_client=b.physical_client,.service_owner=row->view.service_owner,
        .frontend_lifetime=row,.console=row->console,.cvars=row->view.cvars,.source_cvars=b.source_cvars,
        .client_time_cvars=b.source_cvars,.client_time_owner=b.source_owner,.command_context=row->command,
        .native_source=true},.publication_generation=b.publication_generation,.map_revision=b.map_revision,
        .input=row->view.input,.wire_reader=row->view.reader,.reliable_origin=row->command,.console_origin=row->command,
        .context=row,.current=services_current,.idle=services_idle,.reliable=reliable,.console=console_command,
        .reload_client_info=reload_client,.status_visible=status_visible,.command_values=command_values,.release=release_services};
    return true;
}
static bool audio_actor(void *context,int32_t number,uint64_t *out,qa_error *e)
{
    frontend_native_q3 *row=context;
    if(!out || !frontend_native_q3_current(row))return false;
    if(number==1022 || number==1023) { *out=QA_AUDIO_NO_ACTOR; return true; }
    if(number<0)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native source audio has no actual physical actor");
    *out=frontend_audio_native_q3_actor(row,(uint32_t)number,e);
    return *out!=QA_AUDIO_NO_ACTOR;
}
static bool listener(void *context,const qa_audio_listener *value,qa_error *e)
{
    frontend_native_q3 *row=context;
    if(!frontend_native_q3_current(row))return frontend_fail(e,QA_ERROR_ARGUMENT,"Native listener lost its actual recipient");
    row->view.listener=*value; row->view.listener.gain=1.0f/(float)row->frontend->options.seats;
    row->view.has_listener=true; return true;
}
static bool music_origin_current(void *context,const frontend_music_origin *origin)
{
    frontend_native_q3 *row=context;
    const frontend_native_q3_view *v=row?&row->view:NULL;
    return linked(row) && row->owns_media && v->source_launch && origin &&
        origin->kind==FRONTEND_MUSIC_NATIVE && origin->context==row && origin->bus==v->identity &&
        origin->physical_seat==v->seat && origin->receiver==v->receiver && origin->music==v->music &&
        origin->descriptor && origin->descriptor->storage==v->source_launch->storage &&
        origin->catalog==qa_launch_instance_catalog(v->source_launch) &&
        origin->product==v->source_launch->selection.product && origin->files==v->source_files &&
        (!qa_audio_engine_bus_music(row->frontend->audio,v->identity) ||
         qa_audio_engine_bus_music(row->frontend->audio,v->identity)==v->music);
}
static bool music_origin_stop(void *context,qa_error *e)
{
    frontend_native_q3 *row=context;
    if (!linked(row) || !row->owns_media || !row->view.music)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Music stop lost its actual native player");
    qa_audio_engine_remove_music(row->frontend->audio,row->view.identity);
    if (qa_audio_engine_bus_music(row->frontend->audio,row->view.identity))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native music stop retained its engine bus");
    qa_audio_music_stop(row->view.music); row->view.music_attached=false;
    free(row->music_intro); free(row->music_loop);
    row->music_intro=row->music_loop=NULL; row->music_looping=false;
    return true;
}
static frontend_music_origin music_origin(frontend_native_q3 *row)
{
    return (frontend_music_origin){.kind=FRONTEND_MUSIC_NATIVE,.bus=row->view.identity,
        .physical_seat=row->view.seat,.receiver=row->view.receiver,.descriptor=row->view.source_launch,
        .catalog=qa_launch_instance_catalog(row->view.source_launch),.product=row->view.source_launch->selection.product,
        .files=row->view.source_files,.music=row->view.music,.context=row,
        .current=music_origin_current,.stop=music_origin_stop};
}
static bool music(void *context,const char *intro,const char *loop,qa_error *e)
{
    frontend_native_q3 *row=context; qa_frontend *f=row->frontend;
    if(!frontend_native_q3_current(row) || !f->audio)return frontend_fail(e,QA_ERROR_UNSUPPORTED,"Native music requires its actual audio owner");
    qa_audio_music *attached=qa_audio_engine_bus_music(f->audio,row->view.identity);
    if (attached && attached!=row->view.music)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native bus contains another retained music player");
    row->view.music_attached=attached!=NULL;
    if(!row->view.music && !qa_audio_music_create(qa_audio_engine_rate(f->audio),QA_AUDIO_Q3,true,&row->view.music,e))return false;
    frontend_music_origin origin=music_origin(row);
    if (!frontend_music_sources_explicit_selected(f->music_sources,&origin)) {
        if (attached || !qa_audio_music_idle(row->view.music))
            return frontend_fail(e,QA_ERROR_ARGUMENT,"Native music selection retains its previous playback");
        qa_audio_music *fresh=NULL;
        if (!qa_audio_music_create(qa_audio_engine_rate(f->audio),QA_AUDIO_Q3,true,&fresh,e)) return false;
        qa_audio_music_release(row->view.music); row->view.music=fresh;
        free(row->music_intro); free(row->music_loop); row->music_intro=row->music_loop=NULL;
        row->music_looping=false; origin=music_origin(row);
    }
    if (!frontend_music_sources_explicit_begin(f->music_sources,&origin,e)) return false;
    bool enabled=false;
    if (!qa_audio_music_controls_enabled(frontend_music_sources_controls(f->music_sources),&enabled)) return false;
    if (intro && *intro && !enabled) return true;
    const char *tail=loop?loop:"";
    if(intro && row->music_intro && row->music_loop && row->music_looping &&
        !strcmp(intro,row->music_intro) && !strcmp(tail,row->music_loop) && qa_audio_music_playing(row->view.music))return true;
    qa_audio_music_stop(row->view.music); free(row->music_intro); free(row->music_loop);
    row->music_intro=row->music_loop=NULL; row->music_looping=false;
    if(!intro || !*intro)return frontend_music_sources_explicit(f->music_sources,&origin,"","",false,e);
    row->music_intro=malloc(strlen(intro)+1); row->music_loop=malloc(strlen(tail)+1);
    if(!row->music_intro || !row->music_loop)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining native source music names");
    strcpy(row->music_intro,intro); strcpy(row->music_loop,tail);
    qa_audio_stream *first=NULL,*last=NULL;
    if(!qa_audio_bank_music_cue(row->view.sounds,intro,QA_AUDIO_Q3,NULL,NULL,&first,e))return false;
    if(!first)return frontend_music_sources_explicit(f->music_sources,&origin,row->music_intro,row->music_loop,false,e);
    last=first;
    if(*tail && strcmp(intro,tail) && !qa_audio_bank_music_cue(row->view.sounds,tail,QA_AUDIO_Q3,NULL,NULL,&last,e)) {
        qa_audio_stream_close(first); return false;
    }
    row->music_looping=last!=NULL; qa_audio_music_start(row->view.music,first,last);
    bool retained=!row->view.music_attached;
    if (retained && !qa_audio_music_retain(row->view.music,e)) return false;
    bool ok=qa_audio_engine_music(f->audio,row->view.identity,row->view.seat,1,row->view.music,e);
    if (!ok && retained) qa_audio_music_release(row->view.music);
    if(ok) {
        row->view.music_attached=true;
        ok=frontend_music_sources_explicit(f->music_sources,&origin,row->music_intro,row->music_loop,row->music_looping,e);
    }
    return ok;
}
static bool prepare_view(void *context,const qa_q3_refdef *definition,qa_q3_scene_options *options,qa_error *e)
{
    frontend_native_q3 *row=context; qa_frontend *f=row->frontend;
    if(!frontend_native_q3_current(row))return false;
    const qa_native_q3_cvar_refs *refs = qa_native_q3_client_cvar_refs(row->view.client);
    qa_cvar_handle shadows = refs ? refs->rows[QA_NATIVE_Q3_CVAR_cg_shadows] : (qa_cvar_handle){0};
    return frontend_source_prepare_scene(f,f->application,row->view.seat,definition,options,e) &&
        (!row->composition.prepare_view || row->composition.prepare_view(row->composition.context,definition,options,e)) &&
        frontend_native_components_scene_prepare(row,options,e) &&
        frontend_native_q3_current(row) &&
        frontend_q3_scene_policy_read(f,options,e) &&
        frontend_q3_shadow_mode_read(row->view.cvars,shadows,&options->shadow_mode,e) && frontend_native_q3_current(row);
}
static bool submit_view(void *context,const qa_q3_scene_options *options,qa_scene_frame *frame,qa_error *e)
{
    frontend_native_q3 *row=context; qa_frontend *f=row->frontend;
    if(!frontend_native_q3_current(row))return false;
    if(row->composition.submit_view && !row->composition.submit_view(row->composition.context,options,frame,e))return false;
    return frontend_native_components_scene_submit(row,options,frame,e) &&
        frontend_source_submit_scene(f,row->view.seat,row->view.source_owner,options,frame,e) && frontend_native_q3_current(row);
}
static void scene_cleared(void *context)
{
    frontend_native_q3 *row=context;
    frontend_native_components_clear(row);
    if(row->composition.scene_cleared)row->composition.scene_cleared(row->composition.context);
}
static bool remap(void *context,const char *from,const char *to,float time,qa_error *e)
{
    frontend_native_q3 *row=context; qa_material_source_remap_status status;
    if(!frontend_native_q3_current(row) || !qa_material_remap_source(row->view.materials,from,to,time,&status,e))return false;
    if(status!=QA_MATERIAL_SOURCE_REMAP_APPLIED) {
        char warning[1200];
        snprintf(warning,sizeof(warning),status==QA_MATERIAL_SOURCE_REMAP_ORIGINAL_DEFAULT?
            "WARNING: R_RemapShader: shader %s not found\n":"WARNING: R_RemapShader: new shader %s not found\n",
            status==QA_MATERIAL_SOURCE_REMAP_ORIGINAL_DEFAULT?from:to);
        print_row(row,warning);
    }
    return frontend_native_q3_current(row);
}
bool frontend_native_q3_remap(qa_frontend *f,const char *from,const char *to,float time,qa_error *e)
{
    if(!f)return false;
    for(frontend_native_q3 *row=f->native_q3;row;row=row->next)
        if(row->view.materials && !qa_material_remap(row->view.materials,from,to,time,e))return false;
    return true;
}
static bool select_asset(void *context,const char *name,qa_q3_asset_kind kind,
    qa_q3_presentation_provider *out,qa_error *e)
{
    frontend_native_q3 *row=context;
    if(!name || !out || !frontend_native_q3_current(row))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native asset selection lost its actual source recipient");
    *out=(qa_q3_presentation_provider){row->view.mounts,row->view.images,row->view.materials,QA_SCENE_Q3};
    if(kind==QA_Q3_ASSET_SHADER)return true;
    qa_launch_role role;
    if(!strncmp(name,"models/players/",15))role=QA_ROLE_SKIN;
    else if(!strncmp(name,"models/weapons2/",16) || !strncmp(name,"models/weaphits/",15) ||
        !strncmp(name,"models/ammo/",12))role=QA_ROLE_ARSENAL;
    else return true;
    qa_application_q3_asset_selection selected; bool found;
    if(!qa_application_q3_asset_selection_read(row->frontend->application,frontend_native_q3_actor(row),role,&selected,&found,e))return false;
    if(!found || selected.family!=QA_GAME_Q3)return true;
    frontend_visual_owner_view media;
    if(!frontend_visual_media_acquire(row->frontend,selected.provider,QA_GAME_Q3,&media,e) ||
        !qa_application_q3_asset_selection_current(row->frontend->application,&selected) ||
        !frontend_native_q3_current(row))return false;
    *out=(qa_q3_presentation_provider){media.mounts,media.images,media.materials,QA_SCENE_Q3};
    return true;
}
static bool model_initialize(void *context,const qa_q3_model_opening *opening,
    const qa_model *native,qa_scene_model *root,qa_error *error)
{
    frontend_native_q3 *row=context;
    if (!frontend_native_q3_current(row))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Model admission lost its actual native CLIENT");
    return frontend_visual_registered_model_initialize(row->frontend,opening,native,root,error) &&
        frontend_native_q3_current(row);
}
bool frontend_native_q3_asset_options(frontend_native_q3 *row,qa_q3_presentation_asset_options *out,qa_error *e)
{
    if(!row || !out || !linked(row) || !row->view.mounts || !row->view.images || !row->view.materials || !row->view.sounds || !row->view.movies)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native assets require their real retained media owners");
    *out=(qa_q3_presentation_asset_options){.provider={row->view.mounts,row->view.images,row->view.materials,QA_SCENE_Q3},
        .sounds=row->view.sounds,.movies=row->view.movies,.context=row,.select=select_asset,.print=print_row,.model_initialize=model_initialize}; return true;
}
static bool prepare_picture(void *context,qa_material_context *material,qa_error *error)
{
    frontend_native_q3 *row=context;
    return frontend_native_q3_current(row) &&
        frontend_q3_material_diagnostics_read(row->frontend,&material->source_diagnostics,error) &&
        frontend_native_q3_current(row);
}
bool frontend_native_q3_backend_options(frontend_native_q3 *row,qa_q3_presentation_options *out,qa_error *e)
{
    if(!row || !out || !linked(row) || !row->view.assets)return frontend_fail(e,QA_ERROR_ARGUMENT,"Native backend requires its real asset registry");
    qa_frontend *f=row->frontend;
    *out=(qa_q3_presentation_options){.assets=row->view.assets,.audio=f->audio,.clock={row,clock_time},
        .seat=row->view.seat,.owner=row->view.identity,.viewport=frontend_viewport(f,row->view.seat),
        .near_clip=4,.far_clip=16384,.identity_light=1,.lod_scale=5,.rail_core_width=6,.rail_ring_width=16,.rail_segment_length=32,
        .context=row,.audio_actor=audio_actor,.listener=listener,.music=music,.frame_number=frame_number,
        .video_frame=frontend_material_movies_frontend_resolve,.video_context=row->frontend,
        .milliseconds=milliseconds,.audio_bus=audio_bus,.prepare_view=prepare_view,.submit_view=submit_view,
        .scene_cleared=scene_cleared,.prepare_picture=prepare_picture,.remap=remap,.print=print_row};
    if (row->shader_movies && !frontend_material_movies_cinematic_read(row->shader_movies,&out->cinematics,e)) return false;
    if (!frontend_q3_renderer_options_read(f,out,e)) return false;
    const qa_native_q3_cvar_refs *refs = qa_native_q3_client_cvar_refs(row->view.client);
    qa_cvar_handle shadows = refs ? refs->rows[QA_NATIVE_Q3_CVAR_cg_shadows] : (qa_cvar_handle){0};
    if(row->view.cvars && !qa_cvars_read(row->view.cvars,shadows)) {
        qa_application_q3_client_context client;
        if(!row->view.client || !qa_native_q3_client_context_read(row->view.client,&client,e) ||
            client.cvars!=row->view.cvars || client.initialized)
            return frontend_fail(e,QA_ERROR_ARGUMENT,"Native shadow constructor lost its actual pre-Init CLIENT");
        return true;
    }
    return frontend_q3_shadow_mode_read(row->view.cvars,shadows,&out->shadow_mode,e);
}
static int32_t memory_remaining(void *context)
{
    (void)context;
    return qa_memory_available();
}
static bool frame_settings(void *context,const q3n_native *core,
    const qa_application_native_q3_presentation *source,q3n_native_frame_options *out,qa_error *e)
{
    frontend_native_q3 *row=context; q3n_native_owners owners;
    if(core!=row->view.core || !q3n_native_owners_read(core,&owners,e) ||
        !qa_application_native_q3_presentation_current(row->frontend->application,source))return false;
    const q3n_command_state *commands=q3n_server_commands_state(owners.commands);
    if(!commands)return frontend_fail(e,QA_ERROR_ARGUMENT,"Native settings lost their actual command owner");
    if(!application_native_q3_client_frame_settings(row->view.client,commands->dm_flags,ragepro(row),
        (size_t)memory_remaining(row),false,false,0,out,e))return false;
    if(!status_visible(row))out->hud.draw_2d=false;
    if(row->frontend->qc_messages) {
        qa_application_qc_client_presentation qc; bool found=false;
        if(!frontend_qc_messages_client_vitals(row->frontend->qc_messages,frontend_native_q3_actor(row),&qc,&found,e))return false;
        if(found)out->hud.draw_status=false;
    }
    return frontend_native_q3_current(row);
}
static bool client_settings(void *context,const q3n_frame *f,bool loading,q3n_client_settings *out,qa_error *e)
{
    frontend_native_q3 *row=context; q3n_native_frame_options settings;
    if(!frontend_native_q3_cut(row,f,e) || !application_native_q3_client_frame_settings(row->view.client,0,ragepro(row),
        (size_t)memory_remaining(row),loading,false,0,&settings,e))return false;
    *out=settings.clients; return true;
}
bool frontend_native_q3_project_settings(frontend_native_q3 *row,q3n_native_frame_options *out,qa_error *e)
{
    qa_application_native_q3_presentation source;
    return row && qa_application_native_q3_presentation_read(row->frontend->application,row->view.source_owner,&source,e) &&
        frame_settings(row,row->view.core,&source,out,e);
}
static bool read_command(void *context,const q3n_frame *f,int32_t sequence,q3n_server_command_receipt *out,qa_error *e)
{
    frontend_native_q3 *row=context; qa_native_q3_wire_receipt receipt; qa_application_q3_client_context recipient;
    if(!frontend_native_q3_cut(row,f,e) || !qa_native_q3_wire_reader_command(row->view.reader,sequence,&receipt,e) ||
        !qa_native_q3_client_context_read(row->view.client,&recipient,e) || !frontend_native_q3_cut(row,f,e))return false;
    *out=(q3n_server_command_receipt){.wire=receipt,.recipient=recipient,
        .publication_generation=receipt.publication_generation,.map_revision=receipt.map_revision,
        .sequence=receipt.sequence,.present=receipt.present,.arguments=receipt.arguments}; return true;
}
static bool recipient_current(void *context,const q3n_frame *f,const qa_application_q3_client_context *recipient)
{
    frontend_native_q3 *row=context; const qa_native_q3_client_services *s=qa_native_q3_client_services_read(row->view.client);
    return frontend_native_q3_cut(row,f,NULL) && s && recipient &&
        recipient->frontend_lifetime==row && recipient->service_owner==row->view.service_owner &&
        recipient->receiver==row->view.receiver && recipient->source_owner==row->view.source_owner &&
        recipient->seat==row->view.launch_seat && recipient->source_client==row->view.physical_client &&
        qa_actor_id_equal(recipient->source_actor,frontend_native_q3_actor(row)) && recipient->cvars==row->view.cvars &&
        recipient->source_cvars==s->client.source_cvars && recipient->source_frame.number==f->source.source_frame.number &&
        recipient->source_milliseconds==f->source.source_time_ms;
}
static bool receipt_current(void *context,const q3n_frame *f,const q3n_server_command_receipt *receipt)
{ return receipt && qa_native_q3_wire_receipt_current(&receipt->wire) && recipient_current(context,f,&receipt->recipient); }
static bool center_print(void *context,const q3n_frame *f,const char *text,int32_t y,int32_t width,qa_error *e)
{
    frontend_native_q3 *row=context; q3n_native_owners owners;
    return frontend_native_q3_cut(row,f,e) && q3n_native_owners_read(row->view.core,&owners,e) &&
        q3n_hud_center_print(owners.hud,f,text,y,width,e);
}
static bool command_center(void *context,const q3n_frame *f,const qa_application_q3_client_context *recipient,
    const char *text,int32_t y,int32_t width,qa_error *e)
{ return recipient_current(context,f,recipient) && center_print(context,f,text,y,width,e); }
static bool voice_chat(void *context,const q3n_frame *f,int32_t mode,bool only,int32_t client,int32_t color,const char *name,qa_error *e)
{
    frontend_native_q3 *row=context; q3n_native_owners owners;
    return frontend_native_q3_cut(row,f,e) && q3n_native_owners_read(row->view.core,&owners,e) &&
        q3n_server_commands_voice(owners.commands,f,mode,only,client,color,name,e);
}
static bool hud_command(void *context,const q3n_frame *f,const char *text,qa_error *e)
{ frontend_native_q3 *row=context; return frontend_native_q3_cut(row,f,e) && qa_native_q3_client_reliable(row->view.client,text,e); }
static bool load_deferred(void *context,const q3n_frame *f,qa_error *e)
{
    frontend_native_q3 *row=context; q3n_client_settings settings;
    return client_settings(row,f,false,&settings,e) && q3n_clients_load_deferred(f->clients,f->application,&f->source,&settings,e);
}
static bool message(void *context,const q3n_command_message *message,qa_error *e)
{
    frontend_native_q3 *row=context;
    if(!message || !recipient_current(row,message->frame,message->recipient))return false;
    print_row(row,message->text); return frontend_native_q3_cut(row,message->frame,e);
}
static bool trace_policy(frontend_native_q3 *row,qa_trace_policy *policy,qa_error *e)
{
    qa_native_q3_wire_basis basis;
    if(!qa_native_q3_wire_reader_basis(row->view.reader,&basis,e))return false;
    *policy=qa_collision_default_policy(QA_COLLISION_Q3);
    const qa_cvar_view *curves=qa_cvars_read(basis.source_cvars,row->no_curves);
    const qa_cvar_view *players=qa_cvars_read(basis.source_cvars,row->player_curve_clip);
    if(!curves || !players)return frontend_fail(e,QA_ERROR_FORMAT,"Native trace lacks actual source collision controls");
    policy->curves=curves->integer==0; policy->player_curve_clip=players->integer!=0; return true;
}
static bool trace(void *context,const q3n_frame *f,qa_vec3 start,qa_vec3 end,qa_bounds bounds,
    int32_t skip,uint32_t mask,qa_trace_result *out,qa_error *e)
{
    frontend_native_q3 *row=context; qa_trace_query query={.start=start,.end=end,.shape={QA_SHAPE_BOX,bounds}};
    if(!frontend_native_q3_cut(row,f,e) || !trace_policy(row,&query.policy,e))return false;
    query.policy.contents_mask=mask;
    if(skip>=0 && skip<1022) { bool present; if(!qa_native_q3_wire_reader_actor(row->view.reader,(uint32_t)skip,&query.pass_actor,&present,e))return false; }
    return qa_world_trace(qa_application_world(f->application),&query,out,e) && frontend_native_q3_cut(row,f,e);
}
static bool point_contents(void *context,const q3n_frame *f,qa_vec3 point,int32_t pass,uint32_t *out,qa_error *e)
{
    frontend_native_q3 *row=context; qa_point_query query={.point=point}; qa_point_contents result;
    if(!frontend_native_q3_cut(row,f,e) || !trace_policy(row,&query.policy,e))return false;
    if(pass>=0 && pass<1022) { bool present; if(!qa_native_q3_wire_reader_actor(row->view.reader,(uint32_t)pass,&query.pass_actor,&present,e))return false; }
    if(!qa_world_point_contents(qa_application_world(f->application),&query,&result,e) || !frontend_native_q3_cut(row,f,e))return false;
    *out=(uint32_t)result.contents; return true;
}
static bool world_trace(void *context,const q3n_frame *f,qa_vec3 start,qa_vec3 end,qa_bounds bounds,
    uint32_t mask,qa_trace_result *out,qa_error *e)
{
    frontend_native_q3 *row=context; qa_q3_presentation_binding binding;
    qa_trace_query query={.start=start,.end=end,.shape={QA_SHAPE_BOX,bounds}};
    if(!frontend_native_q3_cut(row,f,e) || !trace_policy(row,&query.policy,e) ||
        !qa_q3_presentation_binding_read(f->presentation,&binding,e) || !binding.geometry)return false;
    query.policy.contents_mask=mask;
    return qa_collision_trace(binding.geometry,&query,out,e) && frontend_native_q3_cut(row,f,e);
}
static bool world_contents(void *context,const q3n_frame *f,qa_vec3 point,uint32_t *out,qa_error *e)
{
    frontend_native_q3 *row=context; qa_q3_presentation_binding binding;
    qa_point_query query={.point=point}; qa_point_contents result;
    if(!frontend_native_q3_cut(row,f,e) || !trace_policy(row,&query.policy,e) ||
        !qa_q3_presentation_binding_read(f->presentation,&binding,e) || !binding.geometry ||
        !qa_collision_point_contents(binding.geometry,&query,&result,e) || !frontend_native_q3_cut(row,f,e))return false;
    *out=(uint32_t)result.contents; return true;
}
static bool mark_fragments(void *context,const q3n_frame *f,const qa_vec3 *points,size_t count,
    qa_vec3 projection,qa_vec3 *output,size_t point_capacity,q3n_mark_fragment *fragments,
    size_t fragment_capacity,size_t *returned,qa_error *e)
{
    frontend_native_q3 *row=context; qa_q3_presentation_binding binding; qa_scene_mark_fragment native[128]; qa_scene_mark_result result;
    if(fragment_capacity>128 || !returned || !frontend_native_q3_cut(row,f,e) ||
        !qa_q3_presentation_binding_read(f->presentation,&binding,e) || !binding.world ||
        !qa_scene_world_mark_fragments(binding.world,points,count,projection,output,point_capacity,native,fragment_capacity,&result,e) ||
        !frontend_native_q3_cut(row,f,e))return false;
    for(size_t i=0;i<result.fragment_count;++i) {
        if(native[i].first_point>UINT32_MAX || native[i].point_count>UINT32_MAX)return frontend_fail(e,QA_ERROR_FORMAT,"Native mark fragment exceeds its source word");
        fragments[i]=(q3n_mark_fragment){(uint32_t)native[i].first_point,(uint32_t)native[i].point_count};
    }
    *returned=result.fragment_count; return true;
}
static bool body_hidden(void *context,const q3n_frame *f,const qa_application_native_q3_entity *actual,bool *hidden,qa_error *e)
{
    frontend_native_q3 *row=context; *hidden=false;
    return frontend_native_q3_cut(row,f,e) && (!row->composition.body_hidden ||
        row->composition.body_hidden(row->composition.context,f,actual,hidden,e)) && frontend_native_q3_cut(row,f,e);
}
static bool body_submit(void *context,const q3n_frame *f,const qa_application_native_q3_entity *actual,uint32_t part,
    const qa_q3_ref_entity *ref,bool base,bool *consumed,qa_error *e)
{
    frontend_native_q3 *row=context; *consumed=false;
    if(!frontend_native_q3_cut(row,f,e) || !frontend_native_components_body(row,f,actual->binding.actor,
        part,ref,base,consumed,e)) return false;
    return (*consumed || !row->composition.body ||
        row->composition.body(row->composition.context,f,actual,part,ref,base,consumed,e)) && frontend_native_q3_cut(row,f,e);
}
static bool packet(void *context,const q3n_frame *f,const qa_application_native_q3_entity *actual,q3n_entity *cent,
    const qa_q3_ref_entity *ref,bool *consumed,qa_error *e)
{
    frontend_native_q3 *row=context; *consumed=false;
    if(!frontend_native_q3_cut(row,f,e) || !frontend_native_components_body(row,f,actual->binding.actor,
        3,ref,true,consumed,e)) return false;
    return (*consumed || !row->composition.packet ||
        row->composition.packet(row->composition.context,f,actual,cent,ref,consumed,e)) && frontend_native_q3_cut(row,f,e);
}
static bool event(void *context,const q3n_frame *f,q3n_entity *cent,const qa_q3_entity *state,
    qa_vec3 position,bool *suppressed,qa_error *e)
{
    frontend_native_q3 *row=context; *suppressed=false;
    return frontend_native_q3_cut(row,f,e) && (!row->composition.event ||
        row->composition.event(row->composition.context,f,cent,state,position,suppressed,e)) && frontend_native_q3_cut(row,f,e);
}
static bool view_weapon(void *context,const q3n_frame *f,const qa_q3_player *ps,bool *suppressed,qa_error *e)
{
    frontend_native_q3 *row=context; *suppressed=false;
    return frontend_native_q3_cut(row,f,e) && (!row->composition.view_weapon ||
        row->composition.view_weapon(row->composition.context,f,ps,suppressed,e)) && frontend_native_q3_cut(row,f,e);
}
static bool held_weapon(void *context,const q3n_frame *f,const qa_q3_entity *state,const qa_q3_ref_entity *torso,bool *suppressed,qa_error *e)
{
    frontend_native_q3 *row=context; *suppressed=false;
    return frontend_native_q3_cut(row,f,e) && (!row->composition.held_weapon ||
        row->composition.held_weapon(row->composition.context,f,state,torso,suppressed,e)) && frontend_native_q3_cut(row,f,e);
}
static bool weapon_warning(void *context,const q3n_frame *f,q3n_weapon_hud *warning,qa_error *e)
{
    frontend_native_q3 *row=context;
    return frontend_native_q3_cut(row,f,e) && row->composition.weapon_warning &&
        row->composition.weapon_warning(row->composition.context,f,warning,e) && frontend_native_q3_cut(row,f,e);
}
static bool mission_order(void *context,const q3n_frame *f,qa_error *e)
{ frontend_native_q3 *row=context; return frontend_native_q3_cut(row,f,e) && q3n_mission_hud_check_order(row->view.mission,f,e); }
static bool mission_paint(void *context,const q3n_frame *f,bool scores,bool first,qa_error *e)
{ frontend_native_q3 *row=context; return frontend_native_q3_cut(row,f,e) && q3n_mission_hud_paint(row->view.mission,f,scores,first,e); }
static bool mission_timed(void *context,const q3n_frame *f,qa_error *e)
{ frontend_native_q3 *row=context; return frontend_native_q3_cut(row,f,e) && q3n_mission_hud_timed(row->view.mission,f,e); }
static bool mission_text(void *context,const q3n_frame *f,const char *text,float y,float scale,const float color[4],int32_t style,bool half,qa_error *e)
{ frontend_native_q3 *row=context; return frontend_native_q3_cut(row,f,e) && q3n_mission_hud_text(row->view.mission,f,text,y,scale,color,style,half,e); }
static bool mission_center(void *context,const q3n_frame *f,const char *text,float y,const float color[4],float *height,qa_error *e)
{ frontend_native_q3 *row=context; return frontend_native_q3_cut(row,f,e) && q3n_mission_hud_center_line(row->view.mission,f,text,y,color,height,e); }
static bool score_selection(void *context,const q3n_frame *f,const q3n_command_state *state,qa_error *e)
{ frontend_native_q3 *row=context; return frontend_native_q3_cut(row,f,e) && q3n_mission_hud_score_selection(row->view.mission,f,state,e); }
static bool response_head(void *context,const q3n_frame *f,const q3n_command_state *state,qa_error *e)
{ frontend_native_q3 *row=context; return frontend_native_q3_cut(row,f,e) && q3n_mission_hud_response(row->view.mission,f,state,e); }
static bool key_catcher(void *context,int32_t mask,qa_error *e)
{
    frontend_native_q3 *row=context;
    return frontend_native_q3_current(row) && mask>=0 &&
        qa_input_seat_set_catcher(row->view.input,row->view.service_owner,(uint32_t)mask,e);
}
static bool update_loading(void *context,q3n_loading *loading,const q3n_frame *frame,qa_error *e)
{
    frontend_native_q3 *row=context; qa_frontend *f=row->frontend;
    if(loading!=row->view.loading || !frontend_native_q3_cut(row,frame,e) ||
        f->capture)return false;
    qa_scene_frame_reset(&f->frame,f->frame_number);
    if (!frontend_q3_texture_mode_begin_frame(f,e)) return false;
    if (!frontend_q3_source_output(f,row->view.materials,frontend_viewport(f,row->view.seat),e)) return false;
    if(!qa_q3_presentation_frame(row->view.presentation,&f->frame,frontend_viewport(f,row->view.seat),e) ||
        !q3n_loading_draw_information(loading,frame,e))return false;
    if(!frontend_render_controls_live(f,e))return false;
    if (f->frame.source_backend && f->frame.source_skip_backend) {
        if (f->frame.source_pending &&
            !qa_material_source_frame_end(f->frame.source_pending,&f->frame,false,e)) return false;
        return frontend_native_q3_cut(row,frame,e);
    }
    bool ok=f->cpu?(qa_cpu_execute(f->cpu,&f->frame,e) && qa_cpu_present_frame(f->cpu,e)):
        (f->gl && qa_gl_execute(f->gl,&f->frame,e) && qa_gl_swap(f->gl,e));
    return ok && frontend_native_q3_cut(row,frame,e);
}
static bool loading(void *context,const q3n_frame *f,const char *text,int32_t item,qa_error *e)
{
    frontend_native_q3 *row=context;
    return frontend_native_q3_cut(row,f,e) && (item>=0?q3n_loading_item(row->view.loading,f,(uint32_t)item,e):
        q3n_loading_string(row->view.loading,f,text,e));
}
static bool initialize_stage(void *context,const q3n_frame *f,q3n_command_init_stage stage,
    const char *mapname,int32_t physical,uint32_t *inline_models,qa_error *e)
{
    frontend_native_q3 *row=context; qa_frontend *frontend=row->frontend; (void)mapname;
    if(!frontend_native_q3_cut(row,f,e))return false;
    switch(stage) {
    case Q3N_INIT_CONSOLE_COMMANDS:return frontend_native_q3_commands_register(row->commands,f,e);
    case Q3N_INIT_COLLISION_MAP: {
        qa_bsp_view bsp;
        if(!frontend->scene_world || !frontend->map_resource ||
            !qa_bsp_open(qa_resource_bytes(frontend->map_resource),&bsp,e) ||
            !qa_q3_presentation_world(row->view.presentation,frontend->scene_world,
                qa_world_geometry(qa_application_world(f->application)),bsp.lumps[QA_BSP_ENTITIES].bytes,e))return false;
        size_t count=qa_scene_world_model_count(frontend->scene_world);
        if(!count || count>UINT32_MAX)return frontend_fail(e,QA_ERROR_FORMAT,"Native collision stage has no actual bound inline model extent");
        *inline_models=(uint32_t)count; break;
    }
    case Q3N_INIT_PARTICLES:if(!q3n_particles_load(f->particles,f->application,&f->source,f->time,e))return false; break;
    case Q3N_INIT_CLIENT_LOADING:
        if (physical<0) return frontend_fail(e,QA_ERROR_ARGUMENT,"Native loading requires its actual physical client");
        if(!q3n_loading_client(row->view.loading,f,(uint32_t)physical,e))return false;
        break;
    case Q3N_INIT_STRING_TABLE:case Q3N_INIT_MISSION_ASSETS:case Q3N_INIT_HUD_MENU:case Q3N_INIT_TEAM_CHAT:
        if(!row->view.mission || !q3n_mission_hud_initialize(row->view.mission,f,stage,e))return false;
        break;
    default:return frontend_fail(e,QA_ERROR_ARGUMENT,"Unknown genuine native constructor stage");
    }
    return frontend_native_q3_cut(row,f,e);
}
static bool clear_particles(void *context,const q3n_frame *f,qa_error *e)
{
    frontend_native_q3 *row=context;
    if(!frontend_native_q3_cut(row,f,e))return false;
    q3n_particles_round(f->particles,f->time); return true;
}
static bool begin_frame(void *context,const q3n_frame *f,qa_error *e)
{
    frontend_native_q3 *row=context;
    return frontend_native_q3_cut(row,f,e) && row->composition.begin_frame(row->composition.context,f,e) &&
        frontend_native_q3_cut(row,f,e);
}
static void end_frame(void *context)
{
    frontend_native_q3 *row=context;
    frontend_native_components_release(row);
    row->composition.end_frame(row->composition.context);
}
static bool camera_ready(void *context,const q3n_frame *frame,qa_error *error)
{ return frontend_native_components_prepare(context,frame,error); }
static bool camera_override(void *context,const q3n_frame *frame,
    qa_application_camera_view *out,bool *found,qa_error *error)
{
    frontend_native_q3 *row=context;
    if(!frontend_native_q3_cut(row,frame,error))return false;
    *found=false;
    return (!row->frontend->qc_messages || frontend_qc_messages_client_camera(row->frontend->qc_messages,
        frame->viewing_actor,out,found,error)) && frontend_native_q3_cut(row,frame,error);
}
static bool before_render(void *context,const q3n_frame *f,qa_error *e)
{
    frontend_native_q3 *row=context;
    return frontend_native_q3_cut(row,f,e) &&
        row->composition.before_render(row->composition.context,f,e) && frontend_native_q3_cut(row,f,e);
}
static bool composition_ready(const frontend_native_q3_composition *o,qa_error *e)
{
    bool owned=o->idle || o->destroy || o->rebind_ready || o->rebind;
    return (o->begin_frame==NULL)==(o->end_frame==NULL) &&
        (!o->before_render || o->begin_frame) &&
        (!owned || (o->context && o->idle && o->destroy && o->rebind_ready && o->rebind)) ? true :
        frontend_fail(e,QA_ERROR_ARGUMENT,"Native composition requires paired frame callbacks and a complete actual owner lifetime");
}
bool frontend_native_q3_core_options(frontend_native_q3 *row,q3n_native_options *out,qa_error *e)
{
    if(!out || !frontend_native_q3_current(row) || !row->view.client || !row->view.presentation ||
        !composition_ready(&row->composition,e))return false;
    *out=(q3n_native_options){.application=row->frontend->application,.client=row->view.client,.reader=row->view.reader,
        .presentation=row->view.presentation,.physical_presentation_seat=row->view.seat,
        .events={.context=row,.print=print_row,.center_print=center_print,.voice_chat=voice_chat,.trace=trace,
            .point_contents=point_contents,.mark_fragments=mark_fragments,.event_replacement=event},
        .weapons={.context=row,.view_replacement=view_weapon,.held_replacement=held_weapon},
        .view={.context=row->view.client,.set_view_size=application_native_q3_client_set_view_size,
            .set_third_person_angle_value=application_native_q3_client_set_orbit_angle,.print=print_client,
            .camera_context=row,.camera_override=camera_override},
        .player_state={.context=row,.print=print_row},
        .hud={.context=row,.ui=row->frontend->seats[row->view.seat].ui,.presentation_seat=row->view.seat,
            .milliseconds=milliseconds,.load_deferred=load_deferred,.client_command=hud_command},
        .commands={.reader=row->view.reader,.context=row,.current=recipient_current,.read_command=read_command,
            .receipt_current=receipt_current,.message=message,.center_print=command_center,.client_settings=client_settings,
            .loading=loading,.initialize_stage=initialize_stage,.clear_particles=clear_particles,
            .score_selection=score_selection,.response_head=response_head,.memory_remaining=memory_remaining},
        .player_fx={.context=row,.world_trace=world_trace,.world_point_contents=world_contents,
            .body_hidden=body_hidden,.body_submit=body_submit},
        .packet_context=row,.packet_body=packet,.settings_context=row,.frame_settings=frame_settings};
    qa_native_q3_wire_basis basis;
    if(!qa_native_q3_wire_reader_basis(row->view.reader,&basis,e))return false;
    if(basis.product==QA_Q3_TEAM_ARENA) {
        out->hud.mission_order=mission_order; out->hud.mission_paint=mission_paint; out->hud.mission_timed=mission_timed;
        out->hud.mission_text=mission_text; out->hud.mission_center_line=mission_center;
    }
    if(row->composition.weapon_warning) { out->hud.weapon_warning=weapon_warning; out->player_state.weapon_warning=weapon_warning; }
    if(row->composition.begin_frame) {
        if(!row->composition.end_frame)return frontend_fail(e,QA_ERROR_ARGUMENT,"Native composition lacks its actual frame unwind");
        out->frame_context=row; out->begin_frame=begin_frame; out->end_frame=end_frame; out->camera_ready=camera_ready;
        if(row->composition.before_render)out->before_render=before_render;
    } else if(row->composition.end_frame)return frontend_fail(e,QA_ERROR_ARGUMENT,"Native composition unwind lacks its actual frame entry");
    return true;
}
bool frontend_native_q3_effect(qa_frontend *f,qa_application *application,qa_actor_owner receiver,uint32_t seat,
    qa_application_q3_client_effect effect,const char *text,bool *handled,qa_error *e)
{
    if(!f || !handled || !text || f->application!=application)return frontend_fail(e,QA_ERROR_ARGUMENT,"Native effect requires its actual frontend/application");
    *handled=false;
    for(frontend_native_q3 *row=f->native_q3;row;row=row->next) {
        if(row->view.receiver!=receiver || row->view.launch_seat!=seat)continue;
        *handled=true;
        if(!frontend_native_q3_current(row) || row->callbacks==SIZE_MAX)return frontend_fail(e,QA_ERROR_ARGUMENT,"Native effect receiver has retired");
        ++row->callbacks; bool ok=false;
        switch(effect) {
        case QA_APPLICATION_Q3_SYSTEM_INFO:ok=qa_native_q3_client_system_info(row->view.client,e); break;
        case QA_APPLICATION_Q3_MAP_RESTART: {
            frontend_seat *target=f->seats+row->view.seat; qa_movement_kind kind=target->builder.kind; qa_vec3 angles=target->builder.angles;
            qa_input_command_clear(&target->builder); target->builder.kind=kind; target->builder.angles=angles;
            ok=qa_q3_presentation_clear(row->view.presentation,e); break;
        }
        case QA_APPLICATION_Q3_LEVEL_SHOT: {
            qa_command_context origin=row->command; origin.actor=frontend_native_q3_actor(row);
            ok=qa_seat_console_open(f->seats[row->view.seat].console,false,e) &&
                qa_tools_capture_levelshot(frontend_tools_owner(f),&origin,e); break;
        }
        case QA_APPLICATION_Q3_DISCONNECT: {
            char *copy=malloc(strlen(text)+1);
            if(!copy)frontend_fail(e,QA_ERROR_MEMORY,"Retaining actual native disconnect");
            else { strcpy(copy,text); free(row->disconnect); row->disconnect=copy; ok=true; } break;
        }
        default:frontend_fail(e,QA_ERROR_ARGUMENT,"Unknown actual native client effect"); break;
        }
        --row->callbacks; return ok && frontend_native_q3_current(row);
    }
    return true;
}
static bool namespace_name(frontend_native_q3 *row,char **out,qa_error *e)
{
    const char *instance=row->view.source_launch->selection.instance;
    size_t length=instance?strlen(instance):0;
    if(!out || *out || !length || length>SIZE_MAX-64)
        return frontend_fail(e,QA_ERROR_FORMAT,"Native service namespace lacks its actual source instance");
    char *name=malloc(length+64);
    if(!name)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining actual native service namespace");
    int count=snprintf(name,length+64,"native-q3-service:%s:%u:%llu",instance,
        row->view.launch_seat,(unsigned long long)row->view.identity);
    if(count<=0 || (size_t)count>=length+64) { free(name); return frontend_fail(e,QA_ERROR_FORMAT,"Native service namespace exceeds its exact identity buffer"); }
    *out=name; return true;
}
static bool row_identity(frontend_native_q3 *row,qa_error *e)
{
    qa_frontend *f=row->frontend; char *name=NULL; uint32_t ordinal;
    if(!row->view.source_launch || !row->view.source_launch->storage ||
        row->view.source_files!=row->view.source_launch->content ||
        !qa_application_constructor_seat_ordinal(f->application,row->view.source_owner,row->view.launch_seat,&ordinal,e) ||
        ordinal!=row->view.seat || ordinal>=f->options.seats)return frontend_fail(e,QA_ERROR_ARGUMENT,"Native frontend row lacks its true source descriptor and seat route");
    for(frontend_native_q3 *p=f->native_q3;p;p=p->next)if(p!=row &&
        (p->view.identity==row->view.identity || (p->view.source_owner==row->view.source_owner && p->view.launch_seat==row->view.launch_seat)))
        return frontend_fail(e,QA_ERROR_FORMAT,"Native frontend physical row or service recipient is duplicated");
    if(frontend_source_identity_used(f,row->view.identity))
        return frontend_fail(e,QA_ERROR_FORMAT,"Native frontend identity aliases a real source group");
    if(!namespace_name(row,&name,e))return false;
    qa_strings *strings=qa_session_strings(qa_application_session(f->application));
    if(!qa_strings_intern_cstr(strings,name,&row->view.service_owner,e)) { free(name); return false; }
    free(name);
    return true;
}
static bool registry_context_retain(void *context,qa_error *e)
{ return frontend_config_source_registry_retain(context,e); }
static bool registry_context_release(void *context,qa_error *e)
{ return frontend_config_source_registry_release(context,e); }
static bool client_registry_acquire(frontend_native_q3 *row,frontend_config_source *configuration,
    const qa_application_startup_source *source,qa_error *e)
{
    qa_frontend *f=row->frontend;
    if (frontend_client_registry_lookup(f,source->cvars))
        return frontend_client_registry_acquire_view(f,source->descriptor,source->scope.seat,
            source->cvars,&row->view.registry,e);
    if (!qa_cvars_retain(source->cvars,e)) return false;
    qa_cvars *owned=source->cvars;
    frontend_client_registry_context callback={configuration,registry_context_retain,registry_context_release,true};
    if (!frontend_client_registry_create(f,source->descriptor,source->scope.seat,&owned,
            &callback,&row->view.registry,e)) {
        qa_cvars_destroy(owned); return false;
    }
    row->owns_registry=true; return true;
}
static bool shader_movies_current(void *context,const frontend_material_movie_source *view)
{
    frontend_native_q3 *row=context;
    const frontend_native_q3_view *v=row?&row->view:NULL;
    return linked(row) && row->owns_media && v->source_launch && v->source_files==v->source_launch->content &&
        view && view->frontend==row->frontend && view->context==row && view->files==v->mounts &&
        view->images==v->images && view->materials==v->materials && view->media==v->movies;
}
bool frontend_native_q3_movie_source_read(qa_frontend *f,size_t index,frontend_material_movie_source *out,qa_error *e)
{
    frontend_native_q3 *row=row_at(f,index);
    frontend_material_movie_source view={.frontend=f,.files=row?row->view.mounts:NULL,.images=row?row->view.images:NULL,
        .materials=row?row->view.materials:NULL,.media=row?row->view.movies:NULL,.context=row,.current=shader_movies_current};
    if (!out || !shader_movies_current(row,&view))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Movie binding lacks its retained native provider");
    *out=view; return true;
}
static bool make_media(frontend_native_q3 *row,qa_error *e)
{
    qa_frontend *f=row->frontend; frontend_native_q3_view *v=&row->view;
    if (!frontend_q3_source_color_ensure(f,e)) return false;
    frontend_visual_owner_view shared;
    if(!frontend_visual_media_acquire(f,v->source_owner,QA_GAME_Q3,&shared,e))return false;
    if(!qa_vfs_lookup_equal(shared.mounts,v->source_files))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native image bank differs from its actual Source content");
    /* Source's image registry survives CG_Init. Keep the provider's canonical
     * bank while each physical CLIENT rebuilds its own shader/media owners. */
    if(!qa_vfs_retain(shared.mounts,e))return false;
    v->mounts=shared.mounts;
    if(!qa_scene_resources_retain(shared.images,e))return false;
    v->images=shared.images;
    v->materials=v->images?qa_material_library_create(v->images,f->order,e):NULL;
    v->fonts=v->images?qa_font_library_create(v->mounts,v->images,e):NULL;
    v->movies=v->images?qa_media_library_create(v->images,e):NULL;
    if (v->materials) {
        frontend_material_movie_source movie={.frontend=f,.files=v->mounts,.images=v->images,
            .materials=v->materials,.media=v->movies,.context=row,.current=shader_movies_current};
        if (!frontend_q3_material_profile_initialize(f,v->materials,e) ||
            !qa_material_library_set_source_upload(v->materials,frontend_q3_source_upload_read,f,e) ||
            !frontend_material_movies_create(&movie,&row->shader_movies,e) ||
            !frontend_source_cinematics_ensure(f,v->images,e) ||
            !frontend_material_movies_cinematic_attach(row->shader_movies,f->source_cinematics,v->seat,v->identity,e)) return false;
    }
    qa_scene_image_options images={.family=QA_SCENE_Q3,.wrap=QA_SCENE_REPEAT,.filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,
        .mipmap=true,.transparent_index=-1};
    bool ok=v->mounts && v->images && v->materials && v->fonts && v->movies &&
        qa_material_library_load_scripts(v->materials,v->mounts,&images,e) &&
        qa_material_library_source_shaders_initialize(v->materials,&images,e) &&
        frontend_material_remaps(f,v->materials,e) && qa_audio_bank_create(v->mounts,&v->sounds,e);
    qa_q3_presentation_asset_options assets; qa_q3_presentation_options backend;
    if(ok)ok=frontend_native_q3_asset_options(row,&assets,e) && qa_q3_presentation_assets_create(&assets,&v->assets,e) &&
        frontend_native_q3_backend_options(row,&backend,e) && qa_q3_presentation_create(&backend,&v->presentation,e) &&
        qa_q3_presentation_frame(v->presentation,&f->frame,frontend_viewport(f,v->seat),e);
    return ok;
}
static bool compose_row(frontend_native_q3 *row,const frontend_native_q3_factory *factory,qa_error *e)
{
    if(row->callbacks==SIZE_MAX)return frontend_fail(e,QA_ERROR_ARGUMENT,"Native composition constructor callback count exhausted");
    ++row->callbacks;
    bool ok=factory->compose(factory->context,row,&row->composition,e);
    --row->callbacks;
    return ok && frontend_native_q3_current(row) && composition_ready(&row->composition,e);
}
bool frontend_native_q3_make_children(frontend_native_q3 *row,const qa_application_native_q3_presentation *source,qa_error *e)
{
    q3n_native_owners owners;
    if(!q3n_native_owners_read(row->view.core,&owners,e))return false;
    if(source?source->product==QA_Q3_TEAM_ARENA:qa_native_q3_client_services_read(row->view.client)!=NULL) {
        qa_native_q3_client_basis basis;
        if(!qa_native_q3_client_basis_read(row->view.client,&basis,e))return false;
        if(basis.product==QA_Q3_TEAM_ARENA) {
            const qa_native_q3_client_services *services=qa_native_q3_client_services_read(row->view.client);
            q3n_mission_hud_options options={.application=row->frontend->application,.source=source,
                .client=row->view.client,.reader=row->view.reader,.recipient=services->client,.content=row->view.source_files,
                .assets=row->view.assets,.presentation=row->view.presentation,.fonts=row->view.fonts,
                .seat=row->view.launch_seat,.context=row,
                .milliseconds=milliseconds,.print=print_row,.key_catcher=key_catcher};
            if(!q3n_mission_hud_create(&options,&row->view.mission,e) || !q3n_mission_hud_bind(row->view.mission,owners.hud,e))return false;
        }
    }
    q3n_loading_options loading_options={.application=row->frontend->application,.client=row->view.client,
        .reader=row->view.reader,.assets=row->view.assets,.presentation=row->view.presentation,.media=owners.media,
        .ui=row->frontend->seats[row->view.seat].ui,.seat=row->view.launch_seat,.presentation_seat=row->view.seat,
        .context=row,.update_screen=update_loading};
    return q3n_loading_create(&loading_options,&row->view.loading,e) &&
        (row->commands || frontend_native_q3_commands_create(row,&row->commands,e));
}
static bool media_close(frontend_native_q3 *row,qa_error *e)
{
    if(!row->owns_media)return true;
    if(row->view.presentation && !qa_q3_presentation_destroy(row->view.presentation,e))return false;
    row->view.presentation=NULL;
    if(row->frontend->audio) {
        qa_audio_engine_remove_music(row->frontend->audio,row->view.identity);
        row->view.music_attached=false;
        if(!qa_audio_engine_stop_owner(row->frontend->audio,row->view.identity,row->view.seat,e))return false;
    }
    if(row->shader_movies && row->view.movies && !row->frontend->source_restoring) {
        frontend_material_movie_source expected={.frontend=row->frontend,.files=row->view.mounts,.images=row->view.images,
            .materials=row->view.materials,.media=row->view.movies,.context=row,.current=shader_movies_current};
        if(!frontend_renderer_materials_adopt_movies(row->frontend,&expected,&row->shader_movies,&row->view.movies,e))return false;
    }
    if(!frontend_material_movies_destroy(&row->shader_movies,e))return false;
    if(row->view.assets && !qa_q3_assets_services_retire(row->view.assets,e))return false;
    qa_q3_presentation_assets_destroy(row->view.assets); row->view.assets=NULL;
    qa_media_library_destroy(row->view.movies);
    qa_font_library_destroy(row->view.fonts); qa_audio_bank_destroy(row->view.sounds);
    qa_audio_music_destroy(row->view.music);
    qa_material_library_destroy(row->view.materials); qa_scene_resources_destroy(row->view.images); qa_vfs_destroy(row->view.mounts);
    row->view.movies=NULL; row->view.fonts=NULL; row->view.sounds=NULL;
    row->view.music=NULL; row->view.materials=NULL; row->view.images=NULL; row->view.mounts=NULL;
    row->owns_media=false;
    return true;
}
static bool row_destroy(frontend_native_q3 *row,qa_error *e)
{
    if(row->video || !row_idle(row) || (row->view.core && !q3n_native_retire_ready(row->view.core,e)) ||
        (row->view.presentation && !frontend_selected_effects_idle(row->frontend)) ||
        (row->owns_media && row->frontend->audio && !qa_audio_engine_round_ready(row->frontend->audio,e)))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native row teardown requires inactive actual reader, children and media owners");
    if (row->frontend->music_sources &&
        !frontend_music_sources_explicit_retire(row->frontend->music_sources,row,e)) return false;
    if(row->view.presentation && !frontend_selected_effects_retire_parent(row->frontend,row->view.presentation,e))return false;
    if(!frontend_native_q3_commands_destroy(row->commands,e))return false;
    row->commands=NULL;
    if(row->composition.destroy && !row->composition.destroy(row->composition.context,e))return false;
    row->composition=(frontend_native_q3_composition){0};
    q3n_loading_destroy(row->view.loading); row->view.loading=NULL;
    q3n_mission_hud_destroy(row->view.mission); row->view.mission=NULL;
    if(row->view.core) { if(!q3n_native_destroy(row->view.core,e))return false; row->view.core=NULL; row->view.client=NULL; }
    else if(row->view.client) { if(!qa_native_q3_client_service_destroy(row->view.client,e))return false; row->view.client=NULL; }
    if(!media_close(row,e))return false;
    if(row->owns_services && row->view.reader && !qa_native_q3_wire_reader_destroy(&row->view.reader,e))return false;
    if(row->owns_services && !(row->owns_registry?
        frontend_client_registry_retire(&row->view.registry,e):
        frontend_client_registry_release(&row->view.registry,e)))return false;
    row->view.cvars=NULL;
    qa_launch_instance_lease_release(row->source_lease); free(row->music_intro); free(row->music_loop); free(row->disconnect);
    frontend_native_q3 **link=&row->frontend->native_q3; while(*link && *link!=row)link=&(*link)->next;
    if(*link)*link=row->next;
    free(row); return true;
}
bool frontend_native_q3_video_row_close(frontend_native_q3 *row,qa_error *e)
{
    if(!row || !row->video || !frontend_native_q3_current(row) || !row_idle(row) ||
        (row->view.presentation && !frontend_selected_effects_idle(row->frontend)) ||
        (row->owns_media && row->frontend->audio && !qa_audio_engine_round_ready(row->frontend->audio,e)))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native video close retains an entered actual client or media child");
    row->constructed=false;
    if(row->frontend->music_sources && !frontend_music_sources_explicit_retire(row->frontend->music_sources,row,e))return false;
    if(row->view.presentation && !frontend_selected_effects_retire_parent(row->frontend,row->view.presentation,e))return false;
    if(!frontend_native_q3_commands_destroy(row->commands,e))return false;
    row->commands=NULL;
    if(row->composition.destroy && !row->composition.destroy(row->composition.context,e))return false;
    row->composition=(frontend_native_q3_composition){0};
    q3n_loading_destroy(row->view.loading); row->view.loading=NULL;
    q3n_mission_hud_destroy(row->view.mission); row->view.mission=NULL;
    if(!q3n_native_video_close(&row->view.core,e) || !qa_native_q3_client_video_reset(row->view.client,e) ||
        !media_close(row,e))return false;
    free(row->music_intro); free(row->music_loop); row->music_intro=row->music_loop=NULL;
    row->music_looping=false; row->view.has_listener=false;
    return true;
}
bool frontend_native_q3_video_row_reopen(frontend_native_q3 *row,qa_error *e)
{
    if(!row || !row->video || !frontend_native_q3_current(row) || !row_idle(row))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native video reopen requires its retained actual client");
    if(row->constructed)return row->view.core && q3n_native_current(row->view.core);
    if(!frontend_native_q3_video_row_close(row,e))return false;
    qa_application_native_q3_presentation source; qa_native_q3_wire_publication publication;
    q3n_native_options options;
    frontend_native_q3_factory factory={.context=row->frontend,.compose=frontend_native_composition_create};
    if(!qa_application_native_q3_presentation_read(row->frontend->application,row->view.source_owner,&source,e) ||
        !qa_native_q3_wire_reader_publication(row->view.reader,&publication,e))return false;
    row->owns_media=true;
    if(!make_media(row,e) || !compose_row(row,&factory,e) || !frontend_native_q3_core_options(row,&options,e) ||
        !q3n_native_create(&options,&row->view.core,e) || !frontend_native_q3_make_children(row,&source,e) ||
        !q3n_native_initialize_video(row->view.core,publication.reached_command_sequence,e) ||
        !frontend_source_renderer_end_registration(row->frontend,e))return false;
    row->constructed=true;
    return frontend_native_q3_current(row);
}
bool frontend_native_q3_create(qa_frontend *f,const qa_application_native_q3_presentation *source,uint32_t seat,
    const frontend_native_q3_factory *factory,frontend_native_q3 **out,qa_error *e)
{
    if(!f || !out || *out || !source || !factory || !factory->compose || f->capture || f->source_restoring || f->options.dedicated ||
        !qa_application_native_q3_presentation_current(f->application,source))return frontend_fail(e,QA_ERROR_ARGUMENT,"Native frontend creation requires its genuine fresh source cut");
    uint32_t physical,ordinal; qa_actor_id actor; qa_q3_player player; bool found;
    if(!qa_application_constructor_seat_ordinal(f->application,source->source_owner,seat,&ordinal,e) || ordinal>=f->options.seats ||
        !qa_application_native_q3_presentation_local(f->application,source,seat,&physical,&actor,&player,&found,e) || !found)return false;
    frontend_native_q3 *row=calloc(1,sizeof(*row));
    if(!row)return frontend_fail(e,QA_ERROR_MEMORY,"Allocating actual native frontend row");
    row->frontend=f; row->view=(frontend_native_q3_view){.source_owner=source->source_owner,.receiver=source->source_owner,
        .seat=ordinal,.launch_seat=seat,.physical_client=physical,.product=source->product,.source_launch=source->launch,.source_files=source->content};
    if(!frontend_source_identity_allocate(f,&row->view.identity,e)) { free(row); return false; }
    row->next=f->native_q3; f->native_q3=row;
    row->owns_services=true;
    bool ok=row_identity(row,e) && qa_launch_instance_retain_metadata(source->launch,&row->source_lease,e);
    if(ok)row->view.source_launch=qa_launch_instance_lease_view(row->source_lease);
    if(ok)ok=qa_native_q3_wire_reader_acquire(f->application,row->view.receiver,seat,physical,actor,&row->view.reader,e);
    qa_native_q3_wire_basis basis; qa_application_startup_source game,actual;
    if(ok)ok=qa_native_q3_wire_reader_basis(row->view.reader,&basis,e);
    frontend_config_source *configuration=ok?frontend_config_store_source(f->config_store,basis.source_cvars):NULL;
    if(ok && (!configuration || !frontend_config_source_tuple(configuration,&game) ||
        game.scope.provider!=row->view.source_owner || game.scope.kind!=QA_APPLICATION_CONSOLE_Q3_GAME ||
        !(row->view.input=frontend_config_source_input(configuration,seat))))
        ok=frontend_fail(e,QA_ERROR_ARGUMENT,"Native client requires its actual prepared GAME input");
    if(ok)ok=qa_application_q3_client_configuration_read(f->application,row->view.source_owner,
        QA_QVM_CGAME,seat,&actual,e);
    if(ok)ok=qa_application_capture_command_context(f->application,&actual.command,&actual.command,e);
    if(ok && (!actual.descriptor || actual.descriptor->storage!=row->view.source_launch->storage ||
        actual.scope.provider!=row->view.source_owner || actual.scope.kind!=QA_APPLICATION_CONSOLE_Q3_CGAME ||
        actual.scope.seat!=seat || actual.console!=game.console || !actual.cvars ||
        actual.command.cvar_view!=qa_cvars_view_identity(actual.cvars) || actual.command.owner!=row->view.source_owner ||
        actual.command.seat!=seat || actual.command.dialect!=QA_CONSOLE_Q3 ||
        !qa_application_command_context_active(f->application,&actual.command)))
        ok=frontend_fail(e,QA_ERROR_ARGUMENT,"Native client lost its actual prepared CGAME constructor");
    if(ok) {
        row->console=actual.console; row->command=actual.command;
        ok=client_registry_acquire(row,configuration,&actual,e);
    }
    if(ok)row->view.cvars=frontend_client_registry_cvars(row->view.registry);
    qa_native_q3_client_services services={0}; qa_native_q3_character_selection character={0};
    if(ok)ok=qa_application_character_selection_read(f->application,seat,&character,&found,e) && found;
    if(ok)ok=frontend_native_q3_service_options(row,&services,e) &&
        qa_native_q3_client_service_create(f->application,source,&services,&character,&row->view.client,e);
    if(ok) {
        const qa_native_q3_client_services *retained=qa_native_q3_client_services_read(row->view.client);
        if(!retained)ok=frontend_fail(e,QA_ERROR_ARGUMENT,"Native client has no retained recipient");
        else row->view.recipient=&retained->client;
    }
    if(ok) {
        const qa_launch_choices *choices=qa_launch_snapshot_choices(source->publication);
        const char *name=NULL;
        for(size_t i=0;choices && i<choices->seat_count;++i)if(choices->seats[i].id==seat)name=choices->seats[i].name;
        if(!name) {
            const qa_cvar_view *registered=qa_cvars_find(row->view.cvars,"name");
            if(registered)name=registered->value;
        }
        char *retained=name?malloc(strlen(name)+1):NULL;
        if(!name)ok=frontend_fail(e,QA_ERROR_ARGUMENT,"Native client lacks its actual prepared constructor name");
        else if(!retained)ok=frontend_fail(e,QA_ERROR_MEMORY,"Retaining actual native constructor name");
        else {
            strcpy(retained,name);
            ok=qa_native_q3_client_userinfo_initialize(row->view.client,retained,e);
        }
        free(retained);
    }
    row->owns_media=true;
    q3n_native_options options;
    if(ok)ok=make_media(row,e) && compose_row(row,factory,e) &&
        frontend_native_q3_core_options(row,&options,e) &&
        q3n_native_create(&options,&row->view.core,e) && frontend_native_q3_make_children(row,source,e);
    qa_native_q3_wire_publication publication;
    if(ok)ok=qa_native_q3_wire_reader_publication(row->view.reader,&publication,e) &&
        q3n_native_initialize(row->view.core,publication.initial_command_sequence,e) &&
        frontend_source_renderer_end_registration(f,e);
    if(ok) {
        row->constructed=true; *out=row;
        return frontend_view_bindings_apply_restored(f,e);
    }
    if(character.release)character.release(character.lifetime);
    if(!e || e->code==QA_OK)frontend_fail(e,QA_ERROR_ARGUMENT,"Native constructor lacks its actual retained CHARACTER declaration");
    qa_error cleanup={0}; row_destroy(row,&cleanup); return false;
}

static frontend_native_q3 *row_at(const qa_frontend *f,size_t index)
{
    frontend_native_q3 *row=f?f->native_q3:NULL;
    while(row && index--)row=row->next;
    return row;
}
bool frontend_native_q3_round_admit(qa_frontend *f, qa_actor_owner source,
    uint32_t launch_seat, uint32_t physical_client, qa_actor_id previous,
    qa_actor_id admitted, qa_error *e)
{
    frontend_native_q3 *selected = NULL;
    for (frontend_native_q3 *row = f ? f->native_q3 : NULL; row; row = row->next)
        if (row->view.source_owner == source && row->view.launch_seat == launch_seat) {
            if (selected) return frontend_fail(e, QA_ERROR_FORMAT,
                "Native round has duplicate retained local recipients");
            selected = row;
        }
    if (!selected) return true;
    uint32_t ordinal;
    if (!selected->constructed || !row_idle(selected) || !selected->view.recipient ||
        selected->view.physical_client != physical_client ||
        !frontend_seat_ordinal_read(f, launch_seat, &ordinal) || selected->view.seat != ordinal ||
        !qa_actor_id_equal(frontend_native_q3_actor(selected), previous) ||
        !qa_native_q3_client_service_admit(selected->view.client, previous, admitted, e))
        return frontend_fail(e, QA_ERROR_ARGUMENT,
            "Native round lost its retained physical recipient");
    selected->view.has_listener = false;
    return true;
}

bool frontend_native_q3_sync(qa_frontend *f,const frontend_native_q3_factory *factory,qa_error *e)
{
    if(!f || f->capture || f->source_restoring || !factory || !factory->compose)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native recipient synchronization requires its actual published constructor factory");
    if(f->options.dedicated)return true;
    qa_application_native_q3_presentation source; bool found;
    if(!qa_application_native_q3_presentation_selected(f->application,&source,&found,e))return false;
    if(!found)return true;
    const qa_launch_choices *choices=qa_launch_snapshot_choices(source.publication);
    if(!choices || !f->scene_world || !f->map_resource)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native recipient synchronization requires the actual prepared world and launch seats");
    for(size_t i=0;i<choices->seat_count;++i) {
        uint32_t seat=choices->seats[i].id,ordinal,physical; qa_actor_id actor; qa_q3_player player;
        if(!qa_application_constructor_seat_ordinal(f->application,source.source_owner,seat,&ordinal,e))return false;
        if(ordinal>=f->options.seats)continue;
        qa_actor_owner original;
        if(!frontend_source_cgame_recipient(f,ordinal,&original,e))return false;
        if(original)continue;
        if(!qa_application_native_q3_presentation_local(f->application,&source,seat,&physical,&actor,&player,&found,e))return false;
        if(!found)continue;
        frontend_native_q3 *installed=NULL;
        for(frontend_native_q3 *row=f->native_q3;row;row=row->next)
            if(row->view.source_owner==source.source_owner && row->view.launch_seat==seat) {
                if(installed)return frontend_fail(e,QA_ERROR_FORMAT,"Native source has duplicate installed local recipients");
                installed=row;
            }
        if(installed) {
            if(!installed->constructed || installed->view.seat!=ordinal ||
                installed->view.physical_client!=physical || !qa_actor_id_equal(frontend_native_q3_actor(installed),actor) ||
                !frontend_native_q3_current(installed))
                return frontend_fail(e,QA_ERROR_ARGUMENT,"Native recipient no longer owns its published physical actor and seat");
        } else if(!frontend_native_q3_create(f,&source,seat,factory,&installed,e))return false;
        if(!qa_application_native_q3_presentation_current(f->application,&source))
            return frontend_fail(e,QA_ERROR_ARGUMENT,"Native recipient construction changed its published source cut");
    }
    return true;
}
frontend_native_q3 *frontend_native_q3_at(const qa_frontend *f,size_t index)
{ return f && !f->stepping?row_at(f,index):NULL; }
size_t frontend_native_q3_count(const qa_frontend *f)
{
    size_t count=0;
    for(const frontend_native_q3 *row=f?f->native_q3:NULL;row;row=row->next)++count;
    return count;
}
bool frontend_native_q3_read(const qa_frontend *f,size_t index,frontend_native_q3_view *out,qa_error *e)
{
    frontend_native_q3 *row=row_at(f,index);
    if(!f || !out || f->stepping || !row || row->frontend!=f || row->frame_active || row->callbacks ||
        row->service_released || !row->constructed || !row->view.recipient)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native inventory requires its actual inactive physical row");
    *out=row->view;
    if(out->music_attached)out->music=qa_audio_engine_bus_music(f->audio,out->identity);
    return true;
}
bool frontend_native_q3_factory_view(const frontend_native_q3 *row,frontend_native_q3_view *out,qa_error *e)
{
    qa_q3_presentation_binding binding; qa_q3_presentation_asset_options assets;
    qa_scene_world *world; qa_collision_geometry *geometry;
    if(!out || !frontend_native_q3_current(row) || !row->view.recipient || !row->view.assets || !row->view.presentation ||
        !qa_q3_presentation_binding_read(row->view.presentation,&binding,e) ||
        !qa_q3_assets_services(row->view.assets,&assets,&world,&geometry,e) ||
        binding.options.context!=row || binding.options.seat!=row->view.seat ||
        binding.options.owner!=row->view.identity || binding.options.assets!=row->view.assets ||
        assets.context!=row || assets.provider.mounts!=row->view.mounts ||
        assets.provider.images!=row->view.images || assets.provider.materials!=row->view.materials ||
        assets.sounds!=row->view.sounds || assets.movies!=row->view.movies)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native composition factory requires its actual installed constructor heaps and physical seat");
    *out=row->view; return true;
}
static bool row_idle(const frontend_native_q3 *row)
{
    return row && !row->frame_active && !row->callbacks && !row->components &&
        (!row->composition.idle || row->composition.idle(row->composition.context)) &&
        (!row->view.reader || qa_native_q3_wire_reader_idle(row->view.reader)) &&
        (!row->view.client || qa_native_q3_client_service_idle(row->view.client)) &&
        (!row->view.core || q3n_native_idle(row->view.core)) &&
        frontend_native_q3_commands_idle(row->commands) &&
        (!row->view.mission || q3n_mission_hud_idle(row->view.mission)) &&
        (!row->view.loading || q3n_loading_idle(row->view.loading)) &&
        (!row->view.presentation || qa_q3_presentation_idle(row->view.presentation)) &&
        (!row->view.assets || qa_q3_assets_idle(row->view.assets)) &&
        (!row->view.fonts || qa_font_library_idle(row->view.fonts)) &&
        (!row->view.materials || qa_material_library_idle(row->view.materials)) &&
        (!row->view.images || qa_scene_resources_idle(row->view.images));
}
bool frontend_native_q3_actor_admitted(const qa_frontend *f, uint32_t physical_seat, qa_actor_id actor)
{
    for (const frontend_native_q3 *row = f ? f->native_q3 : NULL; row; row = row->next)
        if (row->view.seat == physical_seat && row->composition.actor_admitted &&
            frontend_native_q3_current(row) &&
            row->composition.actor_admitted(row->composition.context, actor)) return true;
    return false;
}
bool frontend_native_q3_weapon_snapshot(const qa_frontend *f, uint32_t physical_seat,
    qa_application_equipment_view *out, bool *requested, qa_error *error)
{
    if (!f || physical_seat >= f->options.seats || !out || !requested)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native weapon snapshot requires its actual physical seat");
    *requested = false;
    for (const frontend_native_q3 *row = f->native_q3; row; row = row->next)
        if (row->view.seat == physical_seat && row->composition.weapon_snapshot && frontend_native_q3_current(row))
            return row->composition.weapon_snapshot(row->composition.context, out, requested, error);
    return true;
}
bool frontend_native_q3_idle(const qa_frontend *f)
{
    if(!f)return false;
    for(const frontend_native_q3 *row=f->native_q3;row;row=row->next)if(!row_idle(row))return false;
    return true;
}
bool frontend_native_q3_retire_ready(const qa_frontend *f,qa_error *e)
{
    if(!f || f->capture || !frontend_native_q3_idle(f) ||
        !frontend_selected_effects_idle(f) ||
        (f->audio && !qa_audio_engine_round_ready(f->audio,e)))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native retirement requires inactive actual children and audio");
    for(const frontend_native_q3 *row=f->native_q3;row;row=row->next)
        if(row->view.core && !q3n_native_retire_ready(row->view.core,e))return false;
    return true;
}
bool frontend_native_q3_destroy(qa_frontend *f,qa_error *e)
{
    if(!frontend_native_q3_retire_ready(f,e))return false;
    while(f->native_q3)if(!row_destroy(f->native_q3,e))return false;
    return true;
}
bool frontend_native_q3_retire_world(qa_frontend *f,qa_error *e)
{ return frontend_native_q3_destroy(f,e); }
bool frontend_native_q3_publish_world(qa_frontend *f,qa_error *e)
{
    if(!f || f->capture || f->source_restoring || !frontend_native_q3_idle(f))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native world publication requires its inactive actual rows");
    if(!f->scene_world || !f->map_resource)return f->native_q3==NULL;
    if(!f->native_q3)return true;
    qa_bsp_view bsp;
    if(!qa_bsp_open(qa_resource_bytes(f->map_resource),&bsp,e))return false;
    qa_collision_geometry *geometry=qa_world_geometry(qa_application_world(f->application));
    for(frontend_native_q3 *row=f->native_q3;row;row=row->next)
        if(!frontend_native_q3_current(row) || !qa_q3_presentation_world(row->view.presentation,
            f->scene_world,geometry,bsp.lumps[QA_BSP_ENTITIES].bytes,e))return false;
    return true;
}
bool frontend_native_q3_retire_source(qa_frontend *f,qa_actor_owner source,const qa_launch_instance *retiring,qa_error *e)
{
    if(!f || !source || !retiring || !retiring->storage || f->capture || f->source_restoring ||
        !frontend_seat_callbacks_idle(f) || !frontend_selected_effects_idle(f) ||
        (f->audio && !qa_audio_engine_round_ready(f->audio,e)))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native source retirement requires returned actual frontend callbacks");
    for(frontend_native_q3 *row=f->native_q3;row;row=row->next)
        if(row->view.source_owner==source && row->view.source_launch->storage==retiring->storage && (!row_idle(row) ||
            (row->view.core && !q3n_native_retire_ready(row->view.core,e))))
            return frontend_fail(e,QA_ERROR_ARGUMENT,"Native source still owns an active client presentation");
    frontend_native_q3 *row=f->native_q3;
    while(row) {
        frontend_native_q3 *next=row->next;
        if(row->view.source_owner==source && row->view.source_launch->storage==retiring->storage && !row_destroy(row,e))return false;
        row=next;
    }
    return true;
}
bool frontend_native_q3_frame(qa_frontend *f,uint32_t seat,qa_scene_rect viewport,bool *rendered,qa_error *e)
{
    if(!f || !rendered || seat>=f->options.seats || f->capture || f->source_restoring)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native frame requires a real published display seat");
    *rendered=false;
    qa_application_native_q3_presentation source; bool found;
    if(!qa_application_native_q3_presentation_selected(f->application,&source,&found,e))return false;
    if(!found)return true;
    frontend_native_q3 *selected=NULL;
    for(frontend_native_q3 *row=f->native_q3;row;row=row->next)
        if(row->constructed && row->view.seat==seat && row->view.source_owner==source.source_owner) {
            if(selected)return frontend_fail(e,QA_ERROR_FORMAT,"Native display recipient is duplicated");
            selected=row;
        }
    if(!selected)return true;
    qa_native_q3_wire_publication publication;
    if(!row_idle(selected) || !frontend_native_q3_current(selected) ||
        !qa_native_q3_wire_reader_publication(selected->view.reader,&publication,e) ||
        !qa_q3_presentation_frame(selected->view.presentation,&f->frame,viewport,e))return false;
    if (!frontend_q3_source_output(f,selected->view.materials,viewport,e)) return false;
    selected->frame_active=true;
    selected->view.has_listener=false;
    bool ok=q3n_native_draw(selected->view.core,publication.latest_command_sequence,rendered,e);
    selected->frame_active=false;
    return ok && (!*rendered || frontend_native_components_hud(selected,e));
}
bool frontend_native_q3_listener(const qa_frontend *f,uint32_t seat,qa_audio_listener *out)
{
    if(!f || !out || seat>=f->options.seats)return false;
    for(const frontend_native_q3 *row=f->native_q3;row;row=row->next)
        if(row->constructed && row->view.seat==seat && row->view.has_listener && frontend_native_q3_current(row)) {
            *out=row->view.listener; return true;
        }
    return false;
}
bool frontend_native_q3_audio_view(const qa_frontend *f,const qa_audio_asset *asset,qa_vfs **out)
{
    if(!f || !asset || !out)return false;
    qa_resource *resource=qa_audio_asset_resource(asset);
    if(!resource)return false;
    for(const frontend_native_q3 *row=f->native_q3;row;row=row->next)
        if(row->view.sounds && qa_audio_bank_get(row->view.sounds,qa_resource_id(resource),qa_audio_asset_family(asset))==asset) {
            *out=row->view.mounts; return *out!=NULL;
        }
    return false;
}
bool frontend_native_q3_recipient(const qa_frontend *f,uint32_t seat,
    qa_application_q3_client_context *out,uint64_t *generation,bool *found,qa_error *e)
{
    if(!f || !out || !generation || !found || seat>=f->options.seats)return false;
    *found=false;
    for(frontend_native_q3 *row=f->native_q3;row;row=row->next)if(row->constructed && row->view.seat==seat) {
        if(*found)return frontend_fail(e,QA_ERROR_FORMAT,"Native seat has multiple installed recipients");
        if(!q3n_native_recipient(row->view.core,out,e))return false;
        *generation=row->view.identity; *found=true;
    }
    return true;
}
bool frontend_native_q3_client_cvars_read(const qa_frontend *f,uint32_t seat,
    qa_cvars **out,qa_actor_owner *receiver,uint32_t *launch_seat,bool *present,qa_error *e)
{
    if(!f || !f->application || seat>=f->options.seats || !out || !receiver ||
        !launch_seat || !present || f->capture || f->source_restoring ||
        !frontend_seat_callbacks_returned(f))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native CLIENT settings require returned actual Source callbacks");
    frontend_native_q3 *selected=NULL;
    for(frontend_native_q3 *row=f->native_q3;row;row=row->next)if(row->view.seat==seat) {
        if(selected)return frontend_fail(e,QA_ERROR_FORMAT,"Native CLIENT settings have duplicate physical recipients");
        if(!row->constructed || row->video || !row_idle(row) ||
            !frontend_native_q3_current(row))
            return frontend_fail(e,QA_ERROR_ARGUMENT,"Native CLIENT settings lost their installed Source row");
        selected=row;
    }
    *out=selected?selected->view.cvars:NULL;
    *receiver=selected?selected->view.receiver:0;
    *launch_seat=selected?selected->view.launch_seat:0;
    *present=selected!=NULL;
    return true;
}
bool frontend_native_q3_animation_holder(const qa_frontend *f,size_t index,uint32_t client,
    const qa_resource **resource,const qa_vfs_acquisition **receipt,qa_error *e)
{
    frontend_native_q3_view view; q3n_native_owners owners;
    return frontend_native_q3_read(f,index,&view,e) && view.core &&
        q3n_native_owners_read(view.core,&owners,e) && q3n_clients_animation_holder(owners.clients,client,resource,receipt,e);
}
bool frontend_native_q3_borrow(frontend_native_q3 *row,qa_application_q3_client_context *out,qa_error *e)
{
    if(!linked(row) || !row->constructed || row->callbacks==SIZE_MAX ||
        !out || !q3n_native_recipient(row->view.core,out,e))return false;
    ++row->callbacks; return true;
}
bool frontend_native_q3_borrow_current(const frontend_native_q3 *row,const qa_application_q3_client_context *view)
{
    return row && row->callbacks && frontend_native_q3_context_current(row,view);
}
bool frontend_native_q3_installed_context(const frontend_native_q3 *row,qa_application_q3_client_context *out,qa_error *e)
{
    const qa_native_q3_client_services *services=row?qa_native_q3_client_services_read(row->view.client):NULL;
    if(!out || !frontend_native_q3_current(row) || !row->view.core || !q3n_native_current(row->view.core) ||
        !services || !services->client.initialized)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native installed context requires its true initialized client/core owner");
    *out=services->client; return true;
}
bool frontend_native_q3_context_current(const frontend_native_q3 *row,const qa_application_q3_client_context *view)
{
    qa_application_q3_client_context actual;
    if(!view || !frontend_native_q3_installed_context(row,&actual,NULL))return false;
    return view->session==actual.session && view->frontend_lifetime==row && view->receiver==actual.receiver && view->source_owner==actual.source_owner &&
        view->service_owner==actual.service_owner && view->seat==actual.seat && view->source_client==actual.source_client &&
        qa_actor_id_equal(view->source_actor,actual.source_actor) && view->console==actual.console &&
        view->cvars==actual.cvars && view->source_cvars==actual.source_cvars &&
        view->client_time_cvars==actual.client_time_cvars && view->client_time_owner==actual.client_time_owner &&
        view->native_source==actual.native_source && view->initialized==actual.initialized;
}
void frontend_native_q3_release(frontend_native_q3 *row)
{ if(row && row->callbacks)--row->callbacks; }
bool frontend_native_q3_rebind_ready(const qa_frontend *owned,const qa_frontend *destination,qa_error *e)
{
    if(!owned || !destination || owned->stepping || destination->stepping || owned->source_restoring ||
        !frontend_native_q3_idle(owned))return frontend_fail(e,QA_ERROR_ARGUMENT,"Native publication requires inactive completed real rows");
    for(const frontend_native_q3 *row=owned->native_q3;row;row=row->next) {
        uint32_t ordinal;
        if(!row->constructed || row->frontend!=owned || !frontend_native_q3_current(row) ||
            (row->composition.rebind_ready && !row->composition.rebind_ready(row->composition.context,owned,e)) ||
            !qa_application_constructor_seat_ordinal(owned->application,row->view.source_owner,row->view.launch_seat,&ordinal,e) ||
            ordinal!=row->view.seat || !qa_q3_presentation_frontend_rebind_ready(row->view.presentation,&owned->frame,
                owned->audio,row->view.identity,e))return false;
    }
    return true;
}
void frontend_native_q3_rebind(qa_frontend *owned,qa_frontend *destination)
{
    for(frontend_native_q3 *row=owned->native_q3;row;row=row->next) {
        qa_q3_presentation_frontend_rebind(row->view.presentation,&owned->frame,&destination->frame,owned->audio,row->view.identity);

        if(row->composition.rebind)row->composition.rebind(row->composition.context,destination);
        frontend_native_q3_commands_rebind(row->commands,destination); row->frontend=destination;
    }
}
