#include "internal.h"
#include "unified_q3_runtime_services.h"
#include "remote_unified_presentation.h"
#include "remote_unified_save.h"
#include "q3_render_policy.h"
#include "qa/network_q3.h"
#include "qa/console_cvars_prepare.h"
#include "qa/scene_marks.h"
#include "qa/material_source_scratch.h"
#include "../../presentation/q3_native/server_commands_internal.h"
#include <math.h>
#include <limits.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <sys/sysinfo.h>
#endif

struct frontend_unified_q3_runtime_services {
    frontend_unified_q3_runtime_services_options options;
    q3n_compiled_source *source;
    qa_q3_presentation_assets *assets;
    qa_vfs *files;
    qa_font_library *fonts;
    uint32_t physical_seat;
    unsigned calls;
};
static bool fail(qa_error *e,const char *text)
{ qa_error_set(e,QA_ERROR_ARGUMENT,0,"%s",text);return false; }
bool frontend_unified_q3_runtime_services_current(const frontend_unified_q3_runtime_services *o)
{
    if(!o || !o->source || !frontend_unified_q3_client_current(o->options.client) ||
        !frontend_unified_media_current(o->options.media) ||
        frontend_unified_media_recipe(o->options.media)!=frontend_remote_unified_recipe(o->options.replica))return false;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->options.replica);
    q3n_compiled_source_view source;
    return d && d->application==o->options.frontend->application && d->physical_seat==o->physical_seat &&
        q3n_compiled_source_read(o->source,&source,NULL) && source.basis.receiver==o->options.receiver &&
        source.basis.application==d->application && source.basis.assets==o->assets && source.basis.content==o->files &&
        source.basis.physical_seat==o->physical_seat &&
        source.basis.provider==o->options.source.provider->source_owner && q3n_compiled_source_current(&source);
}
static bool cut(frontend_unified_q3_runtime_services *o,const q3n_frame *f,qa_error *e)
{
    return o && f && f->compiled && f->compiled->source.owner==o->source && f->assets==o->assets &&
        f->application==o->options.frontend->application && q3n_frame_current(f) &&
        frontend_unified_q3_runtime_services_current(o) ? true:fail(e,"Compiled CG service lost its actual entered CLIENT");
}
static bool cvar(void *context,const char *name,qa_native_q3_client_cvar *out,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;
    return frontend_unified_q3_runtime_services_current(o) &&
        frontend_unified_q3_client_cvar_read(o->options.client,name,out,e); }
static bool compiled_current(void *context,const q3n_frame *f,qa_cvars *registry,const qa_command_context *origin)
{
    frontend_unified_q3_runtime_services *o=context;
    const qa_command_context *actual=o?frontend_unified_q3_client_context(o->options.client):NULL;
    return actual && origin && registry==frontend_unified_q3_client_cvars(o->options.client) &&
        origin->owner==actual->owner && origin->session==actual->session &&
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
    return cut(o,f,e) && frontend_unified_q3_client_register(o->options.client,e) && cut(o,f,e); }
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
{ frontend_unified_q3_runtime_services *o=context;return (double)o->options.frontend->wall_time_ns*1e-9; }
static int32_t frame_number(void *context)
{ frontend_unified_q3_runtime_services *o=context;uint32_t word=(uint32_t)o->options.frontend->frame_number;
    int32_t result;memcpy(&result,&word,sizeof(result));return result; }
static uint64_t audio_bus(void *context)
{ return ((frontend_unified_q3_runtime_services *)context)->options.audio_owner; }
static bool audio_actor(void *context,int32_t number,uint64_t *out,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;qa_actor_id actor;bool present=false;
    if(number<0 || !out || !frontend_unified_q3_runtime_services_current(o))return false;
    if((uint32_t)number<o->options.source.max_clients &&
        !q3n_compiled_source_client_actor(o->source,(uint32_t)number,&actor,&present,e))return false;
    if(!present){int32_t latest,time;
        if(!frontend_unified_q3_client_latest(o->options.client,&latest,&time,e) ||
            !frontend_unified_q3_client_snapshot_actor(o->options.client,latest,(uint32_t)number,&actor,&present,e))return false;
    }
    return present && o->options.audio_actor(o->options.audio_context,actor,out,e) &&
        frontend_unified_q3_runtime_services_current(o);
}
static int32_t memory_remaining(void *context)
{
    (void)context;uint64_t bytes=0;
#ifdef _WIN32
    MEMORYSTATUSEX state={.dwLength=sizeof(state)};
    if(GlobalMemoryStatusEx(&state))bytes=state.ullAvailPhys;
#elif defined(__APPLE__)
    mach_port_t host=mach_host_self();vm_size_t page=0;
    vm_statistics64_data_t state;mach_msg_type_number_t count=HOST_VM_INFO64_COUNT;
    if(host_page_size(host,&page)==KERN_SUCCESS &&
        host_statistics64(host,HOST_VM_INFO64,(host_info64_t)&state,&count)==KERN_SUCCESS)
        bytes=(uint64_t)state.free_count*(uint64_t)page;
    mach_port_deallocate(mach_task_self(),host);
#else
    struct sysinfo state;if(sysinfo(&state)==0)bytes=(uint64_t)state.freeram*state.mem_unit;
#endif
    return bytes>INT32_MAX?INT32_MAX:(int32_t)bytes;
}
static bool preferences(void *context,qa_ui_preferences *out,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;
    return frontend_unified_q3_runtime_services_current(o) && qa_ui_preferences_read(
        qa_application_cvars(o->options.frontend->application),o->physical_seat,out,e); }
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
static bool info_settings(frontend_unified_q3_runtime_services *o,bool loading,q3n_client_settings *out,qa_error *e)
{
    qa_native_q3_client_cvar value;q3n_client_settings result={.loading=loading,.memory_remaining=(size_t)memory_remaining(o)};
    if(!cvar(o,"cg_forceModel",&value,e))return false;
    result.force_model=value.integer!=0;
    if(!cvar(o,"cg_deferPlayers",&value,e))return false;
    result.defer_players=value.integer!=0;
    if(!cvar(o,"cg_buildScript",&value,e))return false;
    result.build_script=value.integer!=0;
    const qa_cvar_view *model=qa_cvars_find(frontend_unified_q3_client_cvars(o->options.client),"model");
    const qa_cvar_view *head=qa_cvars_find(frontend_unified_q3_client_cvars(o->options.client),"headmodel");
    snprintf(result.model,sizeof(result.model),"%s",model?model->value:"");
    snprintf(result.head_model,sizeof(result.head_model),"%s",head?head->value:"");
    if(o->options.source.product==QA_Q3_TEAM_ARENA){
        if(!cvar(o,"cg_redTeamName",&value,e))return false;
        snprintf(result.red_team_name,sizeof(result.red_team_name),"%s",value.value);
        if(!cvar(o,"cg_blueTeamName",&value,e))return false;
        snprintf(result.blue_team_name,sizeof(result.blue_team_name),"%s",value.value);}
    *out=result;return frontend_unified_q3_runtime_services_current(o);
}
static bool frame_settings(void *context,const q3n_compiled_frame *f,bool loading,uint32_t stereo,
    q3n_native_frame_options *out,qa_error *e)
{
    frontend_unified_q3_runtime_services *o=context;
    if(!out || stereo>2 || !f || f->source.owner!=o->source || !q3n_compiled_frame_current(f) ||
        !frontend_unified_q3_runtime_services_current(o))return fail(e,"CG settings lost their current compiled source");
    q3n_native_frame_options v={.stereo=stereo};qa_native_q3_client_cvar cache;
    v.view.ragepro=v.weapons.ragepro=v.events.ragepro=ragepro(o);
    const char *server;uint64_t revision;char flags[64];
    if(!q3n_compiled_source_configstring(o->source,0,&server,&revision,e) ||
        !qa_q3_info_value(server,"dmflags",flags,sizeof(flags),e))return false;
    v.view.dm_flags=q3nc_integer(flags);
#define INT(field,name) do {if(!cvar(o,#name,&cache,e))return false;v.field=cache.integer;}while(0)
#define BOOL(field,name) do {if(!cvar(o,#name,&cache,e))return false;v.field=cache.integer!=0;}while(0)
#define REAL(field,name) do {if(!cvar(o,#name,&cache,e))return false;v.field=cache.number;}while(0)
    INT(view.view_size,cg_viewsize);INT(view.camera_orbit_integer,cg_cameraOrbit);INT(view.camera_orbit_delay,cg_cameraOrbitDelay);
    REAL(view.camera_orbit_value,cg_cameraOrbit);REAL(view.third_person_range,cg_thirdPersonRange);REAL(view.third_person_angle,cg_thirdPersonAngle);
    REAL(view.error_decay,cg_errorDecay);REAL(view.run_pitch,cg_runpitch);REAL(view.run_roll,cg_runroll);
    REAL(view.bob_pitch,cg_bobpitch);REAL(view.bob_roll,cg_bobroll);REAL(view.bob_up,cg_bobup);REAL(view.fov,cg_fov);REAL(view.zoom_fov,cg_zoomFov);
    REAL(view.gun_x,cg_gun_x);REAL(view.gun_y,cg_gun_y);REAL(view.gun_z,cg_gun_z);BOOL(view.third_person,cg_thirdPerson);BOOL(view.camera_mode,cg_cameraMode);
    BOOL(hud.draw_2d,cg_draw2D);BOOL(hud.draw_status,cg_drawStatus);BOOL(hud.draw_icons,cg_drawIcons);BOOL(hud.draw_3d_icons,cg_draw3dIcons);
    BOOL(hud.draw_rewards,cg_drawRewards);BOOL(hud.crosshair_health,cg_crosshairHealth);BOOL(hud.draw_crosshair_names,cg_drawCrosshairNames);
    BOOL(hud.draw_ammo_warning,cg_drawAmmoWarning);BOOL(hud.paused,cg_paused);BOOL(hud.draw_snapshot,cg_drawSnapshot);BOOL(hud.draw_fps,cg_drawFPS);
    BOOL(hud.draw_timer,cg_drawTimer);BOOL(hud.draw_attacker,cg_drawAttacker);BOOL(hud.lagometer,cg_lagometer);BOOL(hud.no_predict,cg_nopredict);
    BOOL(hud.synchronous_clients,cg_synchronousClients);INT(hud.crosshair,cg_drawCrosshair);INT(hud.crosshair_x,cg_crosshairX);INT(hud.crosshair_y,cg_crosshairY);
    INT(hud.team_overlay,cg_drawTeamOverlay);INT(hud.team_chat_height,cg_teamChatHeight);INT(hud.team_chat_time,cg_teamChatTime);
    REAL(hud.crosshair_size,cg_crosshairSize);REAL(hud.center_time,cg_centertime);
    INT(weapons.brass_time,cg_brassTime);INT(weapons.fov,cg_fov);REAL(weapons.rail_trail_time,cg_railTrailTime);REAL(weapons.true_lightning,cg_trueLightning);
    REAL(weapons.gun_x,cg_gun_x);REAL(weapons.gun_y,cg_gun_y);REAL(weapons.gun_z,cg_gun_z);
    REAL(weapons.tracer_length,cg_tracerLength);REAL(weapons.tracer_width,cg_tracerWidth);REAL(weapons.tracer_chance,cg_tracerChance);
    BOOL(weapons.old_rail,cg_oldRail);BOOL(weapons.no_projectile_trail,cg_noProjectileTrail);BOOL(weapons.old_plasma,cg_oldPlasma);
    BOOL(weapons.old_rocket,cg_oldRocket);BOOL(weapons.draw_gun,cg_drawGun);
    BOOL(events.footsteps,cg_footsteps);BOOL(events.autoswitch,cg_autoswitch);BOOL(events.no_predict,cg_nopredict);
    BOOL(events.synchronous_clients,cg_synchronousClients);BOOL(events.camera_orbit,cg_cameraOrbit);BOOL(events.debug_events,cg_debugEvents);
    BOOL(events.blood,cg_blood);BOOL(events.gibs,cg_gibs);BOOL(events.score_plum,cg_scorePlum);BOOL(events.no_projectile_trail,cg_noProjectileTrail);BOOL(events.add_marks,cg_addMarks);
    BOOL(packet.smooth_clients,cg_smoothClients);BOOL(packet.simple_items,cg_simpleItems);INT(player_fx.shadow_mode,cg_shadows);BOOL(player_fx.draw_friend,cg_drawFriend);
    REAL(swing_speed,cg_swingSpeed);BOOL(no_player_animations,cg_noPlayerAnims);REAL(stereo_separation,cg_stereoSeparation);
    if(o->options.source.product==QA_Q3_TEAM_ARENA){BOOL(events.single_player_active,cg_singlePlayerActive);
        INT(packet.obelisk_respawn_delay,cg_obeliskRespawnDelay);BOOL(player_fx.enable_breath,cg_enableBreath);BOOL(player_fx.enable_dust,cg_enableDust);}
#undef INT
#undef BOOL
#undef REAL
    if(!cvar(o,"cg_animSpeed",&cache,e) || !info_settings(o,loading,&v.clients,e))return false;
    v.animations_disabled=cache.number==0;v.player_fx.animations_disabled=v.animations_disabled;
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
static bool prediction_cursor(void *context,const q3n_compiled_frame *f,int32_t before,int32_t after,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;return f && f->source.owner==o->source && q3n_compiled_frame_current(f) &&
    o->options.operations.prediction_cursor(o->options.operations.context,f,before,after,e); }
static bool trace_number(void *context,const q3n_compiled_frame *f,const qa_trace_result *trace,int32_t *out,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;return f && f->source.owner==o->source && q3n_compiled_frame_current(f) &&
    o->options.operations.trace_number(o->options.operations.context,f,trace,out,e); }
static bool timescale(void *context,int32_t elapsed,qa_error *e)
{ frontend_unified_q3_runtime_services *o=context;return frontend_unified_q3_runtime_services_current(o) &&
    o->options.operations.timescale(o->options.operations.context,elapsed,e); }
bool frontend_unified_q3_runtime_services_create(const frontend_unified_q3_runtime_services_options *options,
    frontend_unified_q3_runtime_services **out,qa_error *e)
{
    if(!options || !out || *out || !options->frontend || !options->replica || !options->media || !options->client ||
        !options->receiver || !options->audio_owner || !options->audio_actor || !options->source.provider ||
        !options->operations.current || !options->operations.command_values || !options->operations.prediction_cursor ||
        !options->operations.trace_number || !options->operations.timescale ||
        !frontend_unified_q3_client_matches(options->client,&options->source))
        return fail(e,"CG services require the genuine CLIENT, media and input/prediction producers");
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(options->replica);
    if(!d || d->application!=options->frontend->application || d->physical_seat>=options->frontend->options.seats ||
        !options->frontend->seats[d->physical_seat].input)
        return fail(e,"CG services lost their actual physical frontend seat");
    frontend_unified_q3_runtime_services *o=calloc(1,sizeof(*o));
    if(!o){qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining compiled CG services");return false;}
    o->options=*options;o->source=frontend_unified_q3_client_source(options->client);
    o->assets=options->source.assets;o->files=options->source.files;o->physical_seat=d->physical_seat;
    qa_scene_resources *images;qa_material_library *materials;qa_audio_bank *sounds;
    if(!frontend_unified_media_bank(options->media,options->source.content,&images,&materials,&o->fonts,&sounds,e) ||
        !frontend_unified_q3_runtime_services_current(o)){free(o);return false;}
    *out=o;return true;
}
bool frontend_unified_q3_runtime_services_read(frontend_unified_q3_runtime_services *o,
    frontend_unified_q3_runtime_options *out,qa_error *e)
{
    q3n_compiled_source_view source;
    if(!out || !frontend_unified_q3_runtime_services_current(o) || !q3n_compiled_source_read(o->source,&source,e))
        return fail(e,"CG options require their actual current service owner");
    frontend_unified_q3_runtime_options v=o->options.operations;
    v.frontend=o->options.frontend;v.replica=o->options.replica;v.client=o->options.client;
    v.context=o;v.current=runtime_current;v.frame_settings=frame_settings;v.trace_number=trace_number;
    v.command_values=command_values;v.prediction_cursor=prediction_cursor;v.timescale=timescale;
    v.preferences=preferences;v.backend_frame=backend_frame;
    v.presentation.assets=o->assets;v.presentation.audio=o->options.frontend->audio;
    v.presentation.owner=o->options.audio_owner;v.presentation.seat=o->physical_seat;
    /* Other presentation callbacks retain the actual factory operations
     * context; clock/audio identity use their dedicated typed contexts. */
    v.presentation.clock=(qa_media_clock){o,clock_time};
    v.view.application=v.player_state.application=v.hud.application=v.loading.application=v.mission.application=
        v.commands.application=o->options.frontend->application;
    v.view.compiled_source=v.player_state.compiled_source=v.hud.compiled_source=v.loading.compiled_source=
        v.mission.compiled_source=v.commands.compiled_source=o->source;
    v.view.assets=v.player_state.assets=v.hud.assets=v.loading.assets=v.mission.assets=v.commands.assets=o->assets;
    v.view.seat=v.player_state.seat=v.hud.seat=v.loading.seat=v.mission.seat=source.basis.seat;
    v.hud.presentation_seat=v.loading.presentation_seat=o->physical_seat;
    v.hud.ui=v.loading.ui=o->options.frontend->seats[o->physical_seat].ui;
    v.view.context=o;v.view.print=print;v.player_state.context=o;v.player_state.print=print;
    v.commands.context=o;v.commands.content=v.mission.content=o->files;
    v.commands.compiled_cvars=v.mission.compiled_cvars=v.loading.compiled_cvars=frontend_unified_q3_client_cvars(o->options.client);
    v.commands.compiled_context=v.mission.compiled_context=*frontend_unified_q3_client_context(o->options.client);
    v.commands.compiled_current=v.mission.compiled_current=compiled_current;
    v.commands.compiled_cvar_read=v.mission.compiled_cvar_read=cvar;
    v.commands.compiled_console=v.mission.compiled_console=console;
    v.commands.compiled_register=registered;v.commands.memory_remaining=memory_remaining;
    v.mission.context=o;v.mission.fonts=o->fonts;v.mission.print=print;v.mission.milliseconds=milliseconds;
    v.weapons.assets=v.events.assets=o->assets;v.weapons.product=v.events.product=v.commands.product=source.basis.product;
    *out=v;return true;
}
bool frontend_unified_q3_runtime_services_destroy(frontend_unified_q3_runtime_services **slot,qa_error *e)
{
    if(!slot || !*slot)return true;
    frontend_unified_q3_runtime_services *o=*slot;
    if(o->calls || !frontend_unified_q3_client_idle(o->options.client))
        return fail(e,"CG service callbacks retain their actual CLIENT parent");
    free(o);*slot=NULL;return true;
}
