#include "native_q3_client_settings.h"
#include "native_q3_client.h"
#include <stdio.h>
#include <string.h>

bool application_q3_client_view_settings(const application_q3_client_settings_source *source,
    int32_t dm_flags,bool ragepro,q3n_view_settings *out,qa_error *error)
{
    if (!source || !source->read || !out) return native_client_fail(error,QA_ERROR_ARGUMENT,"Q3 view settings require their cache reader and output");
    q3n_view_settings value={.dm_flags=dm_flags,.ragepro=ragepro};
    qa_native_q3_client_cvar cache;
#define READ(field,symbol,member) do { if (!source->read(source->context,QA_NATIVE_Q3_CVAR_##symbol,&cache,error)) return false; value.field=cache.member; } while (0)
    READ(view_size,cg_viewsize,integer);
    READ(camera_orbit_integer,cg_cameraOrbit,integer);
    READ(camera_orbit_delay,cg_cameraOrbitDelay,integer);
    READ(camera_orbit_value,cg_cameraOrbit,number);
    READ(third_person_range,cg_thirdPersonRange,number);
    READ(third_person_angle,cg_thirdPersonAngle,number);
    READ(error_decay,cg_errorDecay,number);
    READ(run_pitch,cg_runpitch,number);
    READ(run_roll,cg_runroll,number);
    READ(bob_pitch,cg_bobpitch,number);
    READ(bob_roll,cg_bobroll,number);
    READ(bob_up,cg_bobup,number);
    READ(fov,cg_fov,number);
    READ(zoom_fov,cg_zoomFov,number);
    READ(gun_x,cg_gun_x,number);
    READ(gun_y,cg_gun_y,number);
    READ(gun_z,cg_gun_z,number);
    READ(third_person,cg_thirdPerson,integer);
    READ(camera_mode,cg_cameraMode,integer);
#undef READ
    *out=value; return true;
}
bool application_q3_client_hud_settings(const application_q3_client_settings_source *source,
    q3n_hud_settings *out,qa_error *error)
{
    if (!source || !source->read || !out) return native_client_fail(error,QA_ERROR_ARGUMENT,"Q3 HUD settings require their cache reader and output");
    q3n_hud_settings value={0}; qa_native_q3_client_cvar cache;
#define READ(field,symbol,member) do { if (!source->read(source->context,QA_NATIVE_Q3_CVAR_##symbol,&cache,error)) return false; value.field=cache.member; } while (0)
    READ(draw_2d,cg_draw2D,integer);
    READ(draw_status,cg_drawStatus,integer);
    READ(draw_icons,cg_drawIcons,integer);
    READ(draw_3d_icons,cg_draw3dIcons,integer);
    READ(draw_rewards,cg_drawRewards,integer);
    READ(crosshair_health,cg_crosshairHealth,integer);
    READ(draw_crosshair_names,cg_drawCrosshairNames,integer);
    READ(draw_ammo_warning,cg_drawAmmoWarning,integer);
    READ(paused,cg_paused,integer);
    READ(draw_snapshot,cg_drawSnapshot,integer);
    READ(draw_fps,cg_drawFPS,integer);
    READ(draw_timer,cg_drawTimer,integer);
    READ(draw_attacker,cg_drawAttacker,integer);
    READ(lagometer,cg_lagometer,integer);
    READ(no_predict,cg_nopredict,integer);
    READ(synchronous_clients,cg_synchronousClients,integer);
    READ(crosshair,cg_drawCrosshair,integer);
    READ(crosshair_x,cg_crosshairX,integer);
    READ(crosshair_y,cg_crosshairY,integer);
    READ(team_overlay,cg_drawTeamOverlay,integer);
    READ(team_chat_height,cg_teamChatHeight,integer);
    READ(team_chat_time,cg_teamChatTime,integer);
    READ(crosshair_size,cg_crosshairSize,number);
    READ(center_time,cg_centertime,number);
#undef READ
    *out=value; return true;
}

static void source_text(char *out,size_t capacity,const char *text)
{
    size_t length=strlen(text); if (length>=capacity) length=capacity-1;
    memcpy(out,text,length); out[length]=0;
}
bool application_q3_client_info_settings(const application_q3_client_settings_source *source,
    size_t memory_remaining,bool loading,q3n_client_settings *out,qa_error *error)
{
    if (!source || !source->read || !source->cvars || !out)
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Q3 client info requires its actual cache reader and registry");
    q3n_client_settings value={.memory_remaining=memory_remaining,.loading=loading};
    qa_native_q3_client_cvar cache;
#define READ(field,symbol,member) do { if (!source->read(source->context,QA_NATIVE_Q3_CVAR_##symbol,&cache,error)) return false; value.field=cache.member; } while (0)
    READ(force_model,cg_forceModel,integer);
    READ(defer_players,cg_deferPlayers,integer);
    READ(build_script,cg_buildScript,integer);
    const qa_cvar_view *engine=qa_cvars_read(source->cvars,source->refs->model);
    source_text(value.model,sizeof(value.model),engine?engine->value:"");
    engine=qa_cvars_read(source->cvars,source->refs->head_model);
    source_text(value.head_model,sizeof(value.head_model),engine?engine->value:"");
    if (source->product==QA_Q3_TEAM_ARENA) {
        if (!source->read(source->context,QA_NATIVE_Q3_CVAR_cg_redTeamName,&cache,error)) return false;
        source_text(value.red_team_name,sizeof(value.red_team_name),cache.value);
        if (!source->read(source->context,QA_NATIVE_Q3_CVAR_cg_blueTeamName,&cache,error)) return false;
        source_text(value.blue_team_name,sizeof(value.blue_team_name),cache.value);
    }
#undef READ
    *out=value; return true;
}
bool application_q3_client_frame_settings(const application_q3_client_settings_source *source,
    int32_t dm_flags,bool ragepro,size_t memory_remaining,bool loading,bool demo,uint32_t stereo,
    q3n_native_frame_options *out,qa_error *error)
{
    if (!source || !source->read || !source->cvars || !out || stereo>2)
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Q3 frame settings require their actual cache reader and registry");
    q3n_native_frame_options value={.weapons={.ragepro=ragepro,.gun_frame=0},.events={.ragepro=ragepro,.demo_playback=demo},.stereo=stereo};
    if (!application_q3_client_view_settings(source,dm_flags,ragepro,&value.view,error) ||
        !application_q3_client_hud_settings(source,&value.hud,error) ||
        !application_q3_client_info_settings(source,memory_remaining,loading,&value.clients,error)) return false;
    qa_native_q3_client_cvar cache;
#define READ(field,symbol,member) do { if (!source->read(source->context,QA_NATIVE_Q3_CVAR_##symbol,&cache,error)) return false; value.field=cache.member; } while (0)
    if (source->product==QA_Q3_TEAM_ARENA) {
        READ(events.single_player_active,cg_singlePlayerActive,integer);
        READ(packet.obelisk_respawn_delay,cg_obeliskRespawnDelay,integer);
        READ(player_fx.enable_breath,cg_enableBreath,integer);
        READ(player_fx.enable_dust,cg_enableDust,integer);
    }
    READ(weapons.brass_time,cg_brassTime,integer);
    READ(weapons.fov,cg_fov,integer);
    READ(weapons.rail_trail_time,cg_railTrailTime,number);
    READ(weapons.true_lightning,cg_trueLightning,number);
    READ(weapons.gun_x,cg_gun_x,number);
    READ(weapons.gun_y,cg_gun_y,number);
    READ(weapons.gun_z,cg_gun_z,number);
    READ(weapons.tracer_length,cg_tracerLength,number);
    READ(weapons.tracer_width,cg_tracerWidth,number);
    READ(weapons.tracer_chance,cg_tracerChance,number);
    READ(weapons.old_rail,cg_oldRail,integer);
    READ(weapons.no_projectile_trail,cg_noProjectileTrail,integer);
    READ(weapons.old_plasma,cg_oldPlasma,integer);
    READ(weapons.old_rocket,cg_oldRocket,integer);
    READ(weapons.draw_gun,cg_drawGun,integer);
    READ(events.footsteps,cg_footsteps,integer);
    READ(events.autoswitch,cg_autoswitch,integer);
    READ(events.no_predict,cg_nopredict,integer);
    READ(events.synchronous_clients,cg_synchronousClients,integer);
    READ(events.camera_orbit,cg_cameraOrbit,integer);
    READ(events.debug_events,cg_debugEvents,integer);
    READ(events.blood,cg_blood,integer);
    READ(events.gibs,cg_gibs,integer);
    READ(events.score_plum,cg_scorePlum,integer);
    READ(events.no_projectile_trail,cg_noProjectileTrail,integer);
    READ(events.add_marks,cg_addMarks,integer);
    READ(packet.smooth_clients,cg_smoothClients,integer);
    READ(packet.simple_items,cg_simpleItems,integer);
    READ(player_fx.shadow_mode,cg_shadows,integer);
    READ(player_fx.draw_friend,cg_drawFriend,integer);
    READ(swing_speed,cg_swingSpeed,number);
    READ(no_player_animations,cg_noPlayerAnims,integer);
    READ(stereo_separation,cg_stereoSeparation,number);
#undef READ
    if (!source->read(source->context,QA_NATIVE_Q3_CVAR_cg_animSpeed,&cache,error)) return false;
    value.animations_disabled=cache.number==0;
    value.player_fx.animations_disabled=value.animations_disabled;
    *out=value; return true;
}

bool application_q3_client_cache_copy(qa_native_q3_client_cvar *value,
    const qa_cvar_view *engine,bool force,const char *oversized,qa_error *error)
{
    if (!force && value->modification_count==engine->modification_count) return true;
    /* Cvar_Update publishes its modification word before the oversized-value
     * error. Retain that actual Source failure prefix for every client. */
    value->modification_count=engine->modification_count;
    size_t length=strlen(engine->value);
    if (length>=sizeof(value->value)) return native_client_fail(error,QA_ERROR_FORMAT,oversized);
    memcpy(value->value,engine->value,length+1);
    value->number=engine->number; value->integer=engine->integer; return true;
}

static bool native_settings_cvar(const void *context,qa_native_q3_cvar_id id,
    qa_native_q3_client_cvar *out,qa_error *error)
{ return qa_native_q3_client_cvar_read(context,id,out,error); }

bool application_native_q3_client_view_settings(const qa_native_q3_client_service *service,
    int32_t dm_flags,bool ragepro,q3n_view_settings *out,qa_error *error)
{
    if (!out) return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME view settings require output");
    application_q3_client_settings_source source={.context=service,.read=native_settings_cvar};
    return application_q3_client_view_settings(&source,dm_flags,ragepro,out,error);
}
bool application_native_q3_client_hud_settings(const qa_native_q3_client_service *service,
    q3n_hud_settings *out,qa_error *error)
{
    if (!out) return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME HUD settings require output");
    application_q3_client_settings_source source={.context=service,.read=native_settings_cvar};
    return application_q3_client_hud_settings(&source,out,error);
}
bool application_native_q3_client_frame_settings(const qa_native_q3_client_service *service,
    int32_t dm_flags,bool ragepro,size_t memory_remaining,bool loading,bool demo,uint32_t stereo,
    q3n_native_frame_options *out,qa_error *error)
{
    if (!out || stereo>2 || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME frame settings lost its actual constructor");
    application_q3_client_settings_source source={.context=service,.read=native_settings_cvar,
        .cvars=service->services.client.cvars,.refs=&service->cvar_refs,.product=service->product};
    return application_q3_client_frame_settings(&source,dm_flags,ragepro,memory_remaining,loading,demo,stereo,out,error);
}

bool application_native_q3_client_set_view_size(void *context,int32_t size,qa_error *error)
{
    qa_native_q3_client_service *service=context;
    if (!qa_native_q3_client_service_current(service) || !qa_native_q3_client_service_idle(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native view clamp lacks its actual seat cvar owner");
    char text[32]; snprintf(text,sizeof(text),"%d",size);
    service->updating=true;
    bool ok=qa_cvars_set(service->services.client.cvars,"cg_viewsize",text,false,error);
    service->updating=false; return ok;
}
bool application_native_q3_client_set_orbit_angle(void *context,float angle,qa_error *error)
{ return qa_native_q3_client_cvar_number(context,QA_NATIVE_Q3_CVAR_cg_thirdPersonAngle,angle,error); }
