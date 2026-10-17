#include "settings_menu.h"
#include "settings_devices.h"
#include "startup_menus.h"
#include "config_store.h"
#include "neutral_config.h"
#include "accessibility.h"
#include "ui_features_private.h"
#include "music_sources.h"
#include "startup_server_profile.h"
#include "global_settings_storage.h"
#include "qa/ui_library.h"
#include "qa/application_rankings.h"
#include "qa/arena.h"
#include "qa/filesystem.h"
#include "qa/text.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum setting_operation {
    SET_CVAR, SET_MASK, SET_DESTINATION, SET_CLOSE, SET_RENDERER, SET_RENDERER_APPLY,
    SET_WIDTH, SET_HEIGHT, SET_RESOLUTION, SET_SIZE_APPLY, SET_BRIGHTNESS_RESET,
    SET_MOUSE_AXIS, SET_MOUSE_INVERT, SET_GAMEPAD_FLOAT, SET_GAMEPAD_BOOL, SET_CURVE,
    SET_RUMBLE, SET_RUMBLE_STRENGTH, SET_KEYBOARD, SET_CONTROLLER_SEAT,
    SET_CONTROLLER, SET_AUDIO_DEVICE, SET_GYRO_ENABLE, SET_GYRO_CALIBRATE,
    SET_GYRO_CANCEL, SET_GYRO_RESET, SET_GYRO_AXIS, SET_GYRO_SAVE, SET_LANGUAGE, SET_DOPPLER, SET_ENVIRONMENT,
    SET_PACKED_COLOR, SET_PREFERENCE, SET_PREFERENCES_RESET, SET_GAMEPLAY_RESET,
    SET_MATCH_SELECT, SET_MATCH_TEXT, SET_MATCH_COMMAND, SET_LOCAL_JOIN, SET_LOCAL_DROP,
    SET_SERVER_OPEN, SET_SERVER_VALUE, SET_SERVER_APPLY, SET_SERVER_DEFAULT, SET_SERVER_PAGE,
    SET_SERVER_PROFILE_NAME, SET_SERVER_PROFILE_SELECT, SET_SERVER_PROFILE_SAVE,
    SET_SERVER_PROFILE_LOAD, SET_SERVER_PROFILE_PAGE
} setting_operation;
typedef enum match_operation {
    MATCH_JOIN, MATCH_FOLLOW, MATCH_CALL_VOTE, MATCH_YES, MATCH_NO, MATCH_ADD, MATCH_REMOVE
} match_operation;
static const char *const match_teams[]={"red","blue","free","spectator"};
static const char *const match_team_labels[]={"Red","Blue","Free for all","Spectator"};
static const char *const match_votes[]={"map_restart","nextmap","map","kick"};
static const char *const match_vote_labels[]={"Restart map","Next map","Change map","Kick player"};
static const char *const match_skills[]={"1","2","3","4","5"};
#define SETTINGS_CVARS(X) \
    X(UI_CVAR__CL_COLOR,"_cl_color") \
    X(UI_CVAR__CL_NAME,"_cl_name") \
    X(UI_CVAR_AUTOSWITCH,"autoswitch") \
    X(UI_CVAR_BGMVOLUME,"bgmvolume") \
    X(UI_CVAR_BOTTOMCOLOR,"bottomcolor") \
    X(UI_CVAR_CG_AUTOSWITCH,"cg_autoswitch") \
    X(UI_CVAR_CG_DRAWCROSSHAIRNAMES,"cg_drawCrosshairNames") \
    X(UI_CVAR_CG_DRAWGUN,"cg_drawGun") \
    X(UI_CVAR_CG_MARKS,"cg_marks") \
    X(UI_CVAR_CG_SIMPLEITEMS,"cg_simpleItems") \
    X(UI_CVAR_CL_MOUSEACCEL,"cl_mouseAccel") \
    X(UI_CVAR_CL_RUN,"cl_run") \
    X(UI_CVAR_COLOR,"color") \
    X(UI_CVAR_CON_SCALE,"con_scale") \
    X(UI_CVAR_FOV,"fov") \
    X(UI_CVAR_FREELOOK,"freelook") \
    X(UI_CVAR_GL_MD5_LOAD,"gl_md5_load") \
    X(UI_CVAR_GL_MD5_USE,"gl_md5_use") \
    X(UI_CVAR_HAND,"hand") \
    X(UI_CVAR_HEADMODEL,"headmodel") \
    X(UI_CVAR_IN_JOYSTICK,"in_joystick") \
    X(UI_CVAR_IN_JOYSTICKPROFILE,"in_joystickProfile") \
    X(UI_CVAR_IN_JOYSTICKSEAT,"in_joystickSeat") \
    X(UI_CVAR_IN_MIDI,"in_midi") \
    X(UI_CVAR_IN_MIDICHANNEL,"in_midichannel") \
    X(UI_CVAR_IN_MIDIDEVICE,"in_mididevice") \
    X(UI_CVAR_IN_MIDISEAT,"in_midiseat") \
    X(UI_CVAR_JOY_THRESHOLD,"joy_threshold") \
    X(UI_CVAR_LOOKSPRING,"lookspring") \
    X(UI_CVAR_LOOKSTRAFE,"lookstrafe") \
    X(UI_CVAR_M_FILTER,"m_filter") \
    X(UI_CVAR_M_PITCH,"m_pitch") \
    X(UI_CVAR_M_YAW,"m_yaw") \
    X(UI_CVAR_MODEL,"model") \
    X(UI_CVAR_MUSIC_MENU_TRACK,"music_menu_track") \
    X(UI_CVAR_MUSIC_SHUFFLE,"music_shuffle") \
    X(UI_CVAR_NAME,"name") \
    X(UI_CVAR_QTS_WEAPON_AUTOSWITCH,"qts_weapon_autoswitch") \
    X(UI_CVAR_R_ENHANCEDMODELS,"r_enhancedmodels") \
    X(UI_CVAR_R_FULLSCREEN,"r_fullscreen") \
    X(UI_CVAR_R_GAMMA,"r_gamma") \
    X(UI_CVAR_R_MODEL_DISTANCE,"r_model_distance") \
    X(UI_CVAR_R_OVERRIDE_TEXTURES,"r_override_textures") \
    X(UI_CVAR_R_SMP,"r_smp") \
    X(UI_CVAR_R_SWAPINTERVAL,"r_swapInterval") \
    X(UI_CVAR_R_TEXTURE_FORMATS,"r_texture_formats") \
    X(UI_CVAR_R_TEXTURE_OVERRIDES,"r_texture_overrides") \
    X(UI_CVAR_S_GEOMETRYACOUSTICS,"s_geometryAcoustics") \
    X(UI_CVAR_S_OUTPUTBITS,"s_outputBits") \
    X(UI_CVAR_S_OUTPUTCHANNELS,"s_outputChannels") \
    X(UI_CVAR_S_OUTPUTRATE,"s_outputRate") \
    X(UI_CVAR_SENSITIVITY,"sensitivity") \
    X(UI_CVAR_SKIN,"skin") \
    X(UI_CVAR_SV_AUTOSAVE,"sv_autosave") \
    X(UI_CVAR_TOPCOLOR,"topcolor") \
    X(UI_CVAR_VOLUME,"volume")
#define SETTINGS_CVAR_ENUM(id,name) id,
typedef enum setting_cvar { SETTINGS_CVARS(SETTINGS_CVAR_ENUM) UI_CVAR_COUNT } setting_cvar;
#undef SETTINGS_CVAR_ENUM
#define SETTINGS_CVAR_NAME(id,name) [id]=name,
static const char *const setting_names[UI_CVAR_COUNT]={SETTINGS_CVARS(SETTINGS_CVAR_NAME)};
#undef SETTINGS_CVAR_NAME
#undef SETTINGS_CVARS

typedef struct settings_cvar_store {
    qa_cvars *cvars;
    uint64_t identity,declarations;
    qa_cvar_handle handles[UI_CVAR_COUNT];
} settings_cvar_store;
typedef struct setting_binding {
    frontend_seat *seat;
    setting_operation operation;
    qa_cvars *cvars;
    const char *name;
    qa_cvar_handle handle;
    const char *const *values;
    size_t value_count, offset;
    uint32_t mask;
    qa_ui_id destination;
    bool restart_audio, restart_video, restart_input, submit_only;
    const qa_controller_selection *controllers;
    const uint32_t (*sizes)[2];
    qa_ui_library_field library_field;
} setting_binding;
struct frontend_settings_menu {
    qa_arena arena;
    setting_binding bindings[96];
    settings_cvar_store cvar_stores[3];
    size_t next_cvar_store;
    size_t count;
    qa_error allocation;
    qa_display *display;
    qa_display_backend backend;
    char match_target[257],match_bot[257];
    size_t match_team,match_vote,match_skill;
    const char *server_selected;
    char *server_draft;
    size_t server_draft_capacity,server_page,server_profile_page;
    char server_message[256],server_profile_name[129];
    qa_fs_listing server_profiles;
};
static bool server_action(setting_binding *,const qa_ui_action *,qa_error *);
static settings_cvar_store *settings_store_bind(frontend_settings_menu *owner,qa_cvars *cvars)
{
    uint64_t identity=qa_cvars_view_identity(cvars),declarations=qa_cvars_declaration_revision(cvars);
    settings_cvar_store *store=NULL;
    for(size_t i=0;i<3;++i)
        if(owner->cvar_stores[i].cvars==cvars && owner->cvar_stores[i].identity==identity) {
            store=owner->cvar_stores+i;break;
        }
    if(!store) store=owner->cvar_stores+(owner->next_cvar_store++%3);
    if(store->cvars!=cvars || store->identity!=identity || store->declarations!=declarations) {
        *store=(settings_cvar_store){.cvars=cvars,.identity=identity,.declarations=declarations};
        for(size_t i=0;i<UI_CVAR_COUNT;++i) store->handles[i]=qa_cvars_resolve(cvars,setting_names[i]);
    }
    return store;
}
static const qa_cvar_view *setting_value(frontend_seat *seat,qa_cvars *cvars,setting_cvar variable)
{
    if(!cvars) return NULL;
    settings_cvar_store *store=settings_store_bind(seat->settings_menu,cvars);
    return qa_cvars_read(cvars,store->handles[variable]);
}
static double now_ms(const frontend_seat *seat)
{ return (double)seat->frontend->time_ns / 1000000.0; }
static bool in_game(const frontend_seat *seat)
{ return qa_application_launch(seat->frontend->application)!=NULL || frontend_network_remote(seat->frontend); }
static bool q1_pickup_shared(const frontend_seat *seat)
{
    const qa_launch_snapshot *publication=qa_application_launch(seat->frontend->application);
    const qa_launch_choices *selected=qa_launch_snapshot_choices(publication);
    if(!selected || frontend_network_remote(seat->frontend)) return false;
    const qa_launch_binding *binding=qa_launch_binding_for(selected,(qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    for(size_t i=0;binding && i<selected->provider_count;++i) {
        const qa_launch_provider *provider=selected->providers+i;
        if(strcmp(provider->instance,binding->instance)) continue;
        const qa_product *product=qa_catalog_product(qa_application_catalog(seat->frontend->application),provider->product);
        return provider->runtime==QA_PROGRAM_BUILTIN && product && product->family==QA_GAME_Q1;
    }
    return false;
}
static bool gameplay_reset(frontend_seat *,qa_error *);
static char *copy_text(frontend_settings_menu *owner, const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = qa_arena_alloc(&owner->arena, size, 1, &owner->allocation);
    if (copy) memcpy(copy, text, size);
    return copy;
}
static void *cache(frontend_settings_menu *owner, size_t count, size_t size, size_t alignment)
{
    if (count > SIZE_MAX/size) { qa_error_set(&owner->allocation,QA_ERROR_MEMORY,0,"Settings choice cache is too large"); return NULL; }
    return qa_arena_alloc(&owner->arena, count*size, alignment, &owner->allocation);
}
static void begin(frontend_seat *seat)
{
    frontend_settings_menu *owner = seat->settings_menu;
    qa_arena_reset(&owner->arena); owner->count = 0; owner->allocation = (qa_error){0};
}
static bool queue_restart(frontend_seat *seat, const char *text, qa_error *error)
{
    qa_console *console; qa_cvars *cvars; qa_command_context command;
    if (!qa_input_seat_recipient_read(seat->input, &console, &cvars, &command))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Settings lost their actual input recipient");
    (void)cvars;
    return qa_console_append(console, &command, text, error);
}
static bool match_command(frontend_seat *seat,match_operation operation,qa_error *error)
{
    frontend_settings_menu *owner=seat->settings_menu;
    const char *name=NULL,*arguments[3]; size_t count=0;
    switch(operation) {
    case MATCH_JOIN: name="team"; arguments[count++]=match_teams[owner->match_team]; break;
    case MATCH_FOLLOW: name="follow"; arguments[count++]=owner->match_target; break;
    case MATCH_CALL_VOTE:
        name="callvote"; arguments[count++]=match_votes[owner->match_vote];
        if(owner->match_vote>=2) arguments[count++]=owner->match_target;
        break;
    case MATCH_YES: case MATCH_NO: name="vote"; arguments[count++]=operation==MATCH_YES?"yes":"no"; break;
    case MATCH_ADD:
        name="addbot"; arguments[count++]=owner->match_bot; arguments[count++]=match_skills[owner->match_skill];
        arguments[count++]=match_teams[owner->match_team]; break;
    case MATCH_REMOVE: name="kick"; arguments[count++]=owner->match_bot; break;
    }
    qa_console *console; qa_cvars *cvars; qa_command_context command;
    if(!name || !qa_input_seat_recipient_read(seat->input,&console,&cvars,&command) ||
        command.dialect!=QA_RULESET_Q3)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Match controls lost their actual Q3 CLIENT recipient");
    if((operation==MATCH_ADD || operation==MATCH_REMOVE) && !frontend_network_remote(seat->frontend)) {
        qa_application_startup_source source; bool present=false;
        if(!frontend_config_store_primary_server_read(seat->frontend->config_store,&source,&present,error)) return false;
        if(!present || source.scope.kind!=QA_APPLICATION_CONSOLE_Q3_GAME || source.command.dialect!=QA_RULESET_Q3)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Match administration has no actual primary Q3 Source");
        console=source.console; command=source.command;
    }
    char text[1024]; size_t used=strlen(name); memcpy(text,name,used);
    for(size_t i=0;i<count;++i) {
        size_t size=strlen(arguments[i]);
        if(size+3>=sizeof(text)-used) return frontend_fail(error,QA_ERROR_FORMAT,"Match command exceeds its Source text range");
        text[used++]=' '; text[used++]='"'; memcpy(text+used,arguments[i],size); used+=size; text[used++]='"';
    }
    text[used]=0;
    if(qa_command_separator(text,used,command.dialect)!=used)
        return frontend_fail(error,QA_ERROR_FORMAT,"Match text cannot contain a Source command separator");
    qa_arena storage={0}; qa_console_command_arena(console,&storage);
    qa_command_tokens tokens;
    if(!qa_command_tokenize(text,command.dialect,command.console_text,&tokens,
        qa_arena_alloc_callback,&storage,error)) { qa_arena_destroy(&storage); return false; }
    bool exact=tokens.count==count+1 && !strcmp(tokens.values[0],name);
    for(size_t i=0;exact && i<count;++i) exact=!strcmp(tokens.values[i+1],arguments[i]);
    qa_command_tokens_free(&tokens);
    qa_arena_destroy(&storage);
    if(!exact) return frontend_fail(error,QA_ERROR_FORMAT,"Match text cannot be represented by the actual Source command grammar");
    text[used++]='\n'; text[used]=0;
    return qa_console_append(console,&command,text,error);
}
static bool valid_size(const frontend_seat *seat, uint32_t *width, uint32_t *height)
{
    double w,h;
    if (!*seat->menu_width || !*seat->menu_height ||
        !qa_parse_number((qa_bytes){(const uint8_t *)seat->menu_width,strlen(seat->menu_width)},&w,NULL) ||
        !qa_parse_number((qa_bytes){(const uint8_t *)seat->menu_height,strlen(seat->menu_height)},&h,NULL) ||
        !isfinite(w) || !isfinite(h) || floor(w)!=w || floor(h)!=h || w<320 || h<200 || w>8192 || h>8192) return false;
    *width=(uint32_t)w; *height=(uint32_t)h; return true;
}
static bool set_size(frontend_seat *seat, uint32_t width, uint32_t height, qa_error *error)
{
    qa_cvars *cvars=qa_application_cvars(seat->frontend->application);
    char w[12],h[12]; snprintf(w,sizeof(w),"%u",width); snprintf(h,sizeof(h),"%u",height);
    if (!qa_cvars_set_console(cvars,"r_mode","-1",error) ||
        !qa_cvars_set_console(cvars,"r_customwidth",w,error) ||
        !qa_cvars_set_console(cvars,"r_customheight",h,error) ||
        !queue_restart(seat,"vid_restart\n",error)) return false;
    snprintf(seat->menu_width,sizeof(seat->menu_width),"%.4s",w);
    snprintf(seat->menu_height,sizeof(seat->menu_height),"%.4s",h); return true;
}
static bool action(void *context,uint32_t id,qa_ui_id control,const qa_ui_action *event,qa_error *error)
{
    setting_binding *binding=context; frontend_seat *seat=binding->seat;
    (void)id; (void)control;
    const char *choice=NULL;
    if (event->kind==QA_UI_SELECT) {
        if (event->value.row>=binding->value_count) return frontend_fail(error,QA_ERROR_ARGUMENT,"Settings choice was removed");
        choice=binding->values ? binding->values[event->value.row] : NULL;
    }
    double value=event->kind==QA_UI_CHANGE_NUMBER?event->value.number:0;
    qa_gamepad_tuning *live=qa_input_seat_gamepad_tuning(seat->input), tuning=*live;
    switch(binding->operation) {
    case SET_SERVER_OPEN: case SET_SERVER_VALUE: case SET_SERVER_APPLY: case SET_SERVER_DEFAULT:
    case SET_SERVER_PAGE: case SET_SERVER_PROFILE_NAME: case SET_SERVER_PROFILE_SELECT:
    case SET_SERVER_PROFILE_SAVE: case SET_SERVER_PROFILE_LOAD: case SET_SERVER_PROFILE_PAGE:
        return server_action(binding,event,error);
    case SET_LOCAL_JOIN:
        return event->kind!=QA_UI_ACTIVATE || frontend_startup_local_join_stage(seat,error);
    case SET_LOCAL_DROP:
        return event->kind!=QA_UI_ACTIVATE || frontend_startup_local_drop_stage(seat,error);
    case SET_MATCH_SELECT:
        if(event->kind==QA_UI_SELECT) *(size_t *)((char *)seat->settings_menu+binding->offset)=event->value.row;
        return true;
    case SET_MATCH_TEXT:
        if(event->kind==QA_UI_CHANGE_TEXT || event->kind==QA_UI_SUBMIT) {
            const char *text=event->value.text?event->value.text:""; size_t size=strlen(text);
            if(size>=sizeof(seat->settings_menu->match_target))
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Match field exceeds its actual 64-character storage");
            memcpy((char *)seat->settings_menu+binding->offset,text,size+1);
        }
        return true;
    case SET_MATCH_COMMAND:
        return event->kind!=QA_UI_ACTIVATE || match_command(seat,(match_operation)binding->mask,error);
    case SET_DESTINATION:
        return event->kind!=QA_UI_ACTIVATE || frontend_menu_open(seat,binding->destination,error);
    case SET_CLOSE: return event->kind!=QA_UI_ACTIVATE || qa_ui_close(seat->ui,now_ms(seat),error);
    case SET_RENDERER:
        if (choice) seat->menu_renderer=event->value.row?QA_DISPLAY_OPENGL:QA_DISPLAY_CPU;
        return true;
    case SET_RENDERER_APPLY:
        return event->kind!=QA_UI_ACTIVATE || queue_restart(seat,seat->menu_renderer==QA_DISPLAY_OPENGL?"vid_restart gl\n":"vid_restart cpu\n",error);
    case SET_WIDTH: case SET_HEIGHT:
        if (event->kind==QA_UI_CHANGE_TEXT) snprintf(binding->operation==SET_WIDTH?seat->menu_width:seat->menu_height,5,"%s",event->value.text?event->value.text:"");
        return true;
    case SET_RESOLUTION:
        return event->kind!=QA_UI_SELECT || set_size(seat,binding->sizes[event->value.row][0],binding->sizes[event->value.row][1],error);
    case SET_SIZE_APPLY: {
        if (event->kind!=QA_UI_ACTIVATE) return true;
        uint32_t w,h;
        if (!valid_size(seat,&w,&h)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Use a width of 320-8192 and a height of 200-8192 pixels.");
        return set_size(seat,w,h,error);
    }
    case SET_BRIGHTNESS_RESET:
        return event->kind!=QA_UI_ACTIVATE || qa_cvars_set_console(binding->cvars,"r_gamma","1",error);
    case SET_KEYBOARD:
        return event->kind!=QA_UI_SELECT || frontend_settings_keyboard_select(seat->frontend,event->value.row<seat->frontend->options.seats?(int)event->value.row:-1,error);
    case SET_CONTROLLER_SEAT:
        if (event->kind==QA_UI_SELECT) seat->menu_controller_seat=(uint32_t)event->value.row;
        return true;
    case SET_CONTROLLER:
        return event->kind!=QA_UI_SELECT || frontend_settings_controller_select(seat->frontend,seat->menu_controller_seat,binding->controllers+event->value.row,error);
    case SET_AUDIO_DEVICE:
        return event->kind!=QA_UI_SELECT || frontend_settings_audio_select(seat->frontend,event->value.row?choice:NULL,error);
    case SET_PREFERENCE: {
        char text[32];
        if(event->kind==QA_UI_CHANGE_NUMBER) { if(!qa_format_number(value,text,error)) return false; choice=text; }
        return !choice || (qa_ui_preference_set(binding->cvars,seat->id,(qa_ui_preference)binding->mask,choice,error) &&
            frontend_settings_devices_save(seat->frontend,error));
    }
    case SET_PREFERENCES_RESET:
        if(event->kind!=QA_UI_ACTIVATE) return true;
        for(unsigned preference=0;preference<QA_UI_PREF_LANGUAGE;++preference)
            if(!qa_ui_preference_set(binding->cvars,seat->id,(qa_ui_preference)preference,qa_ui_preference_describe((qa_ui_preference)preference)->initial,error)) return false;
        return frontend_settings_devices_save(seat->frontend,error);
    case SET_GAMEPLAY_RESET:
        return event->kind!=QA_UI_ACTIVATE || (gameplay_reset(seat,error) && qa_ui_close(seat->ui,now_ms(seat),error));
    case SET_LANGUAGE:
        return event->kind!=QA_UI_SELECT || (qa_ui_preference_set(binding->cvars,seat->id,QA_UI_PREF_LANGUAGE,choice,error) &&
            frontend_settings_devices_save(seat->frontend,error));
    case SET_DOPPLER: case SET_ENVIRONMENT:
        return event->kind!=QA_UI_ACTIVATE || qa_ui_library_open_selection(seat->library,binding->library_field,error);
    case SET_RUMBLE: case SET_RUMBLE_STRENGTH: {
        if (event->kind!=QA_UI_CHANGE_NUMBER) return true;
        qa_haptic_player *haptics=qa_input_platform_haptics(seat->frontend->input,seat->id);
        return !haptics || ((binding->operation==SET_RUMBLE?
            qa_haptic_enable(haptics,value!=0,haptics->active,error):qa_haptic_strength(haptics,(float)value,now_ms(seat),error)) &&
            frontend_settings_devices_save(seat->frontend,error));
    }
    case SET_GYRO_ENABLE:
        return event->kind!=QA_UI_CHANGE_NUMBER || (qa_input_platform_gyro(seat->frontend->input,seat->id,value!=0,error) &&
            frontend_settings_devices_save(seat->frontend,error));
    case SET_GYRO_CALIBRATE:
        return event->kind!=QA_UI_ACTIVATE || qa_input_platform_calibrate(seat->frontend->input,seat->id,error);
    case SET_GYRO_CANCEL: case SET_GYRO_RESET:
        return event->kind!=QA_UI_ACTIVATE || qa_input_platform_calibration_cancel(seat->frontend->input,seat->id,binding->operation==SET_GYRO_RESET,error);
    case SET_GYRO_AXIS:
        if (event->kind!=QA_UI_SELECT) return true;
        tuning.gyro_yaw_axis=(qa_gyro_yaw_axis)event->value.row;
        break;
    case SET_GYRO_SAVE:
        return event->kind!=QA_UI_ACTIVATE || frontend_settings_devices_save(seat->frontend,error);
    case SET_GAMEPAD_FLOAT:
        if (event->kind!=QA_UI_CHANGE_NUMBER) return true;
        *(float *)((char *)&tuning+binding->offset)=(float)value;
        if (binding->offset==offsetof(qa_gamepad_tuning,move.deadzone)) tuning.move.deadzone=fminf(tuning.move.deadzone,0.999f-tuning.move.outer_threshold);
        if (binding->offset==offsetof(qa_gamepad_tuning,look.deadzone)) tuning.look.deadzone=fminf(tuning.look.deadzone,0.999f-tuning.look.outer_threshold);
        if (binding->offset==offsetof(qa_gamepad_tuning,move.outer_threshold)) tuning.move.outer_threshold=fminf(tuning.move.outer_threshold,0.999f-tuning.move.deadzone);
        if (binding->offset==offsetof(qa_gamepad_tuning,look.outer_threshold)) tuning.look.outer_threshold=fminf(tuning.look.outer_threshold,0.999f-tuning.look.deadzone);
        break;
    case SET_GAMEPAD_BOOL:
        if (event->kind!=QA_UI_CHANGE_NUMBER) return true;
        *(bool *)((char *)&tuning+binding->offset)=value!=0; break;
    case SET_CURVE: {
        if (event->kind!=QA_UI_SELECT) return true;
        qa_stick_curve *curve=(qa_stick_curve *)((char *)&tuning+binding->offset);
        curve->kind=event->value.row?QA_STICK_AXIAL:QA_STICK_RADIAL;
        curve->outer_threshold=curve->kind==QA_STICK_RADIAL?fminf(0.02f,(1-curve->deadzone)/2):0;
        break;
    }
    case SET_MOUSE_AXIS: case SET_MOUSE_INVERT: {
        if (event->kind!=QA_UI_CHANGE_NUMBER) return true;
        const qa_cvar_view *row=qa_cvars_read(binding->cvars,binding->handle);
        double previous=row?row->number:0.022;
        value=binding->operation==SET_MOUSE_INVERT?(value!=0?-fabs(previous):fabs(previous)):
            copysign(0.022*value/100,previous);
        char text[32]; return qa_format_number(value,text,error) && qa_cvars_set_console(binding->cvars,binding->name,text,error);
    }
    case SET_CVAR: case SET_MASK: case SET_PACKED_COLOR: {
        const qa_cvar_view *row=qa_cvars_read(binding->cvars,binding->handle);
        if (!row) return frontend_fail(error,QA_ERROR_ARGUMENT,"Settings cvar was removed");
        const char *text=choice; char number[32];
        if (event->kind==QA_UI_CHANGE_TEXT && binding->submit_only) {
            seat->selected_setting=(size_t)control;
            snprintf(seat->setting_value,sizeof(seat->setting_value),"%s",event->value.text?event->value.text:""); return true;
        }
        if (event->kind==QA_UI_CHANGE_TEXT || event->kind==QA_UI_SUBMIT) text=event->value.text?event->value.text:"";
        if (event->kind==QA_UI_CHANGE_NUMBER) {
            if (binding->operation==SET_MASK) value=(double)(value!=0?(uint32_t)row->integer|binding->mask:(uint32_t)row->integer&~binding->mask);
            if (!qa_format_number(value,number,error)) return false;
            text=number;
        }
        if (binding->operation==SET_PACKED_COLOR && event->kind==QA_UI_SELECT) {
            uint32_t previous=(uint32_t)row->integer, shift=binding->mask;
            snprintf(number,sizeof(number),"%u",(previous&~(15u<<shift))|((uint32_t)event->value.row<<shift)); text=number;
        }
        if (!text) return true;
        if (!qa_cvars_set_console(binding->cvars,binding->name,text,error)) return false;
        if (event->kind==QA_UI_SUBMIT) seat->selected_setting=0;
        if (binding->restart_input) return queue_restart(seat,"in_restart\n",error);
        if (binding->restart_audio) return queue_restart(seat,"snd_restart\n",error);
        if (binding->restart_video) return queue_restart(seat,"vid_restart\n",error);
        return true;
    }
    }
    if (!qa_gamepad_tuning_valid(&tuning)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid controller settings");
    *live=tuning; return frontend_settings_devices_save(seat->frontend,error);
}
static qa_ui_control *control(frontend_seat *seat,const char *label,qa_ui_control_kind kind,setting_operation operation)
{
    frontend_settings_menu *owner=seat->settings_menu;
    if (owner->count>=sizeof(owner->bindings)/sizeof(*owner->bindings)) {
        qa_error_set(&owner->allocation,QA_ERROR_MEMORY,0,"Authored settings controls exceed their cache"); return NULL;
    }
    size_t i=owner->count++;
    setting_binding *binding=owner->bindings+i; *binding=(setting_binding){.seat=seat,.operation=operation};
    seat->controls[i]=(qa_ui_control){.id=1000+i,.kind=kind,.label=label,.rect={64,92+(float)i*28,496,28},
        .enabled=true,.visible=true,.scrolls=true,.context=binding,.action=action};
    return seat->controls+i;
}
static setting_binding *binding_of(qa_ui_control *item) { return item?item->context:NULL; }
static qa_ui_control *button(frontend_seat *seat,const char *label,setting_operation operation)
{ return control(seat,label,QA_UI_BUTTON,operation); }
static qa_ui_control *slider(frontend_seat *seat,const char *label,double value,double minimum,double maximum,double step,setting_operation operation)
{
    qa_ui_control *item=control(seat,label,QA_UI_SLIDER,operation);
    if (item) { item->value.slider.value=value; item->value.slider.minimum=minimum;
        item->value.slider.maximum=maximum; item->value.slider.step=step; }
    return item;
}
static qa_ui_control *toggle(frontend_seat *seat,const char *label,bool value,setting_operation operation)
{
    qa_ui_control *item=control(seat,label,QA_UI_TOGGLE,operation);
    if(item) item->value.checked=value;
    return item;
}
static qa_ui_control *choice(frontend_seat *seat,const char *label,const char *const *labels,const char *const *values,size_t count,size_t selected,setting_operation operation)
{
    qa_ui_control *item=control(seat,label,QA_UI_CHOICE,operation);
    if(item) { item->value.choice.labels=labels; item->value.choice.count=count; item->value.choice.selected=selected;
        setting_binding *binding=binding_of(item); binding->values=values; binding->value_count=count; }
    return item;
}
static bool enabled(const qa_cvars *cvars,const qa_cvar_view *row)
{
    qa_ruleset_id dialect=qa_cvars_dialect(cvars);
    uint32_t flags=dialect==QA_RULESET_Q3?QA_CVAR_READONLY|QA_CVAR_INIT:
        dialect==QA_RULESET_Q2_CLASSIC || dialect==QA_RULESET_Q2_RERELEASE?QA_Q2_CVAR_NOSET:0;
    return !(row->flags&flags);
}
static const char *value_text(const qa_cvar_view *row)
{ return row->latched_value ? row->latched_value : row->value; }
static qa_ui_control *cvar(frontend_seat *seat,qa_cvars *cvars,setting_cvar variable,const char *label,
    qa_ui_control_kind kind,double minimum,double maximum,double step)
{
    const qa_cvar_view *row=setting_value(seat,cvars,variable);
    if(!row) return NULL;
    qa_ui_control *item=control(seat,label,kind,SET_CVAR);
    if(!item) return NULL;
    setting_binding *binding=binding_of(item); binding->cvars=cvars; binding->name=setting_names[variable];
    binding->handle=settings_store_bind(seat->settings_menu,cvars)->handles[variable];
    item->enabled=enabled(cvars,row);
    if(kind==QA_UI_SLIDER) { item->value.slider.value=row->number; item->value.slider.minimum=minimum;
        item->value.slider.maximum=maximum; item->value.slider.step=step; }
    if(kind==QA_UI_TOGGLE) item->value.checked=row->integer!=0;
    if(kind==QA_UI_FIELD) { item->value.field.text=value_text(row); item->value.field.maximum=(size_t)maximum; }
    return item;
}
static qa_ui_control *cvar_choice(frontend_seat *seat,qa_cvars *cvars,setting_cvar variable,const char *label,
    const char *const *labels,const char *const *values,size_t count)
{
    qa_ui_control *item=cvar(seat,cvars,variable,label,QA_UI_CHOICE,0,0,0);
    if(item) {
        const qa_cvar_view *row=setting_value(seat,cvars,variable);
        item->value.choice.labels=labels; item->value.choice.count=count;
        for(size_t i=0;i<count;++i) if(!strcmp(value_text(row),values[i])) item->value.choice.selected=i;
        setting_binding *binding=binding_of(item); binding->values=values; binding->value_count=count;
    }
    return item;
}
static qa_cvars *client_cvars(frontend_seat *seat,qa_cvars **mouse)
{
    qa_console *console=NULL; qa_cvars *cvars=NULL; qa_command_context command;
    *mouse=NULL;
    if(!qa_input_seat_recipient_read(seat->input,&console,&cvars,&command)) return NULL;
    frontend_neutral_config_view view;
    if(frontend_config_store_neutral_read(seat->frontend->config_store,cvars,&view,NULL) && view.ready) *mouse=view.mouse;
    else *mouse=cvars;
    return cvars;
}
static void destination(frontend_seat *seat,const char *label,qa_ui_id id)
{ setting_binding *binding=binding_of(button(seat,label,SET_DESTINATION)); if(binding) binding->destination=id; }
static bool finish(frontend_seat *seat,qa_ui_id id,const char *title,qa_ui_menu *out,qa_error *error,bool sound)
{
    frontend_settings_menu *owner=seat->settings_menu;
    if(owner->allocation.code!=QA_OK) { if(error) *error=owner->allocation; return false; }
    size_t count=owner->count;
    if(sound) for(size_t i=0;i<count;++i) seat->controls[i].rect=(qa_scene_rect_f){64,118+(float)i*38,496,34};
    qa_ui_control *back=button(seat,"Back",SET_CLOSE);
    if(!back) { if(error) *error=owner->allocation; return false; }
    back->rect=(qa_scene_rect_f){64,sound?424:400,512,sound?30:28}; back->scrolls=false;
    *out=(qa_ui_menu){.id=id,.title=title,.controls=seat->controls,.count=owner->count,.scrollable=true,
        .scroll_rect={64,sound?118:92,512,sound?298:300},.content_height=(float)count*(sound?38:28)};
    return true;
}

static void server_message(frontend_settings_menu *owner,const qa_error *error)
{ snprintf(owner->server_message,sizeof(owner->server_message),"%s",error->message); }
static bool server_read(frontend_seat *seat,frontend_server_settings *settings)
{
    qa_error error={0}; bool present=false;
    if(!frontend_server_settings_read(seat->frontend,settings,&present,&error)) {
        server_message(seat->settings_menu,&error); return false;
    }
    if(!present) snprintf(seat->settings_menu->server_message,sizeof(seat->settings_menu->server_message),
        "Server settings require a running local host.");
    return present;
}
static bool server_draft(frontend_settings_menu *owner,const char *text,qa_error *error)
{
    size_t size=strlen(text)+1;
    if(size>owner->server_draft_capacity) {
        char *value=realloc(owner->server_draft,size);
        if(!value) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining requested server setting");
        owner->server_draft=value; owner->server_draft_capacity=size;
    }
    memmove(owner->server_draft,text,size); return true;
}
static qa_settings_store server_store(const frontend_seat *seat)
{ return frontend_global_settings_storage_user_store(seat->frontend->global_settings_storage); }
static bool server_profiles_read(frontend_seat *seat)
{
    frontend_settings_menu *owner=seat->settings_menu;
    qa_fs_listing_free(&owner->server_profiles);
    qa_settings_store store=server_store(seat);
    qa_fs_root *root=qa_vfs_mount_root(store.vfs,store.mount);
    qa_error error={0}; qa_fs_entry_kind kind;
    if(!root) {
        snprintf(owner->server_message,sizeof(owner->server_message),"Server profile storage is unavailable.");
        return false;
    }
    if(!qa_fs_root_status(root,"servers",&kind,NULL,&error) ||
        (kind!=QA_FS_MISSING && !qa_fs_root_list(root,"servers",&owner->server_profiles,&error))) {
        qa_fs_listing_free(&owner->server_profiles);
        server_message(owner,&error); return false;
    }
    size_t count=0;
    for(size_t i=0;i<owner->server_profiles.count;++i) {
        qa_fs_entry entry=owner->server_profiles.entries[i];
        size_t size=strlen(entry.name); char name[33],path[46];
        bool valid=entry.kind==QA_FS_REGULAR && size>5 && size<=37 && !strcmp(entry.name+size-5,".json");
        if(valid) {
            memcpy(name,entry.name,size-5); name[size-5]=0;
            valid=frontend_server_profile_path(name,path,NULL);
        }
        if(valid) owner->server_profiles.entries[count++]=entry;
        else free(entry.name);
    }
    owner->server_profiles.count=count;
    return true;
}
static bool server_profiles_open(void *context,uint32_t id,qa_error *error)
{ (void)id; (void)error; (void)server_profiles_read(context); return true; }
static bool server_apply(frontend_seat *seat)
{
    frontend_settings_menu *owner=seat->settings_menu; frontend_server_settings settings;
    if(!server_read(seat,&settings)) return true;
    const qa_server_setting_definition *definition=owner->server_selected?
        qa_server_setting_find(&settings.definitions,owner->server_selected):NULL;
    qa_error error={0}; qa_server_setting_status status;
    if(!definition) snprintf(owner->server_message,sizeof(owner->server_message),"This setting is no longer available.");
    else if(!qa_server_setting_write(definition,settings.cvars,owner->server_draft?owner->server_draft:"",&error) ||
        !frontend_server_setting_read(seat->frontend,&settings,definition,&status,&error) ||
        !server_draft(owner,status.desired,&error)) server_message(owner,&error);
    else owner->server_message[0]=0;
    return true;
}
static bool server_profile(frontend_seat *seat,bool save)
{
    frontend_settings_menu *owner=seat->settings_menu; frontend_server_settings settings;
    if(!server_read(seat,&settings)) return true;
    char path[46]; qa_error error={0}; qa_server_profile *profile=NULL; qa_buffer bytes={0}; bool found=false;
    bool okay=frontend_server_profile_path(owner->server_profile_name,path,&error);
    if(okay && save) okay=qa_server_profile_capture(settings.selection,settings.cvars,&profile,&error) &&
        qa_server_profile_encode(profile,&bytes,&error) &&
        qa_settings_write(server_store(seat),path,(qa_bytes){bytes.data,bytes.size},&error);
    if(okay && !save) {
        okay=qa_settings_load_server_profile(server_store(seat),path,settings.selection,&profile,&found,&error);
        if(okay && !found) okay=frontend_fail(&error,QA_ERROR_NOT_FOUND,"The selected server profile does not exist.");
        if(okay) okay=qa_server_profile_apply(profile,settings.selection,settings.cvars,&error);
    }
    qa_server_profile_destroy(profile); qa_buffer_free(&bytes);
    if(!okay) server_message(owner,&error);
    else {
        snprintf(owner->server_message,sizeof(owner->server_message),"%s %s.",save?"Saved":"Loaded",owner->server_profile_name);
        if(save) (void)server_profiles_read(seat);
    }
    return true;
}
static bool server_action(setting_binding *binding,const qa_ui_action *event,qa_error *error)
{
    frontend_seat *seat=binding->seat; frontend_settings_menu *owner=seat->settings_menu;
    qa_error failure={0};
    switch(binding->operation) {
    case SET_SERVER_OPEN: {
        if(event->kind!=QA_UI_ACTIVATE) return true;
        frontend_server_settings settings;
        if(!server_read(seat,&settings)) return true;
        const qa_server_setting_definition *definition=qa_server_setting_find(&settings.definitions,binding->name);
        qa_server_setting_status status;
        if(!definition || !frontend_server_setting_read(seat->frontend,&settings,definition,&status,&failure) ||
            !server_draft(owner,status.desired,&failure)) {
            if(!definition) snprintf(owner->server_message,sizeof(owner->server_message),"This setting is no longer available.");
            else server_message(owner,&failure);
            return true;
        }
        owner->server_selected=definition->id; owner->server_message[0]=0;
        return frontend_menu_open(seat,FRONTEND_SERVER_SETTING,error);
    }
    case SET_SERVER_VALUE: {
        const char *text=NULL;
        if(event->kind==QA_UI_CHANGE_TEXT || event->kind==QA_UI_SUBMIT) text=event->value.text?event->value.text:"";
        if(event->kind==QA_UI_CHANGE_NUMBER) text=event->value.number!=0?"1":"0";
        if(event->kind==QA_UI_SELECT && event->value.row<binding->value_count) text=binding->values[event->value.row];
        if(text && !server_draft(owner,text,&failure)) { server_message(owner,&failure); return true; }
        return event->kind!=QA_UI_SUBMIT || server_apply(seat);
    }
    case SET_SERVER_APPLY: return event->kind!=QA_UI_ACTIVATE || server_apply(seat);
    case SET_SERVER_DEFAULT: {
        if(event->kind!=QA_UI_ACTIVATE) return true;
        frontend_server_settings settings;
        if(!server_read(seat,&settings)) return true;
        const qa_server_setting_definition *definition=owner->server_selected?
            qa_server_setting_find(&settings.definitions,owner->server_selected):NULL;
        if(definition && server_draft(owner,definition->default_value,&failure)) owner->server_message[0]=0;
        else if(definition) server_message(owner,&failure);
        return true;
    }
    case SET_SERVER_PAGE: case SET_SERVER_PROFILE_PAGE:
        if(event->kind==QA_UI_ACTIVATE) {
            size_t *page=binding->operation==SET_SERVER_PAGE?&owner->server_page:&owner->server_profile_page;
            if(binding->offset) ++*page; else if(*page) --*page;
        }
        return true;
    case SET_SERVER_PROFILE_NAME:
        if(event->kind==QA_UI_CHANGE_TEXT || event->kind==QA_UI_SUBMIT)
            snprintf(owner->server_profile_name,sizeof(owner->server_profile_name),"%s",event->value.text?event->value.text:"");
        return true;
    case SET_SERVER_PROFILE_SELECT:
        if(event->kind==QA_UI_ACTIVATE) snprintf(owner->server_profile_name,sizeof(owner->server_profile_name),"%s",binding->name);
        return true;
    case SET_SERVER_PROFILE_SAVE: return event->kind!=QA_UI_ACTIVATE || server_profile(seat,true);
    case SET_SERVER_PROFILE_LOAD: return event->kind!=QA_UI_ACTIVATE || server_profile(seat,false);
    default: return true;
    }
}
static void server_info(frontend_seat *seat,const char *label)
{
    qa_ui_control *item=button(seat,label,SET_CLOSE);
    if(item) item->enabled=false;
}
static char *server_label(frontend_settings_menu *owner,const char *prefix,const char *value,const char *suffix)
{
    size_t size=strlen(prefix)+strlen(value)+strlen(suffix)+1;
    char *text=cache(owner,size,1,1);
    if(text) snprintf(text,size,"%s%s%s",prefix,value,suffix);
    return text;
}
static void server_pages(frontend_seat *seat,size_t page,size_t pages,setting_operation operation)
{
    qa_ui_control *item=button(seat,"Previous page",operation);
    if(item) item->enabled=page>0;
    item=button(seat,"Next page",operation);
    if(item) { item->enabled=page+1<pages; binding_of(item)->offset=1; }
}
static bool server_settings_menu(void *context,uint32_t seat_id,qa_ui_menu *out,qa_error *error)
{
    (void)seat_id;
    frontend_seat *seat=context; frontend_settings_menu *owner=seat->settings_menu; begin(seat);
    frontend_server_settings settings; bool ready=server_read(seat,&settings);
    size_t pages=ready?(settings.definitions.count+6)/7:1;
    if(!pages) pages=1;
    if(owner->server_page>=pages) owner->server_page=pages-1;
    for(size_t i=owner->server_page*7;ready && i<settings.definitions.count && i<(owner->server_page+1)*7;++i) {
        const qa_server_setting_definition *definition=settings.definitions.items[i];
        qa_server_setting_status status; qa_error failure={0};
        if(!frontend_server_setting_read(seat->frontend,&settings,definition,&status,&failure)) {
            server_message(owner,&failure); continue;
        }
        char *prefix=server_label(owner,definition->label,": ","");
        qa_ui_control *item=button(seat,prefix?server_label(owner,prefix,status.desired,status.pending?" (pending)":""):NULL,SET_SERVER_OPEN);
        if(item) binding_of(item)->name=definition->id;
    }
    server_pages(seat,owner->server_page,pages,SET_SERVER_PAGE);
    destination(seat,"Save or load server profile",FRONTEND_SERVER_PROFILES);
    seat->controls[owner->count-1].enabled=ready;
    if(owner->server_message[0]) server_info(seat,copy_text(owner,owner->server_message));
    char title[64]; snprintf(title,sizeof(title),"Server settings %zu/%zu",owner->server_page+1,pages);
    return finish(seat,FRONTEND_SERVER_SETTINGS,copy_text(owner,title),out,error,false);
}
static bool server_setting_menu(void *context,uint32_t seat_id,qa_ui_menu *out,qa_error *error)
{
    (void)seat_id;
    frontend_seat *seat=context; frontend_settings_menu *owner=seat->settings_menu; begin(seat);
    frontend_server_settings settings; bool ready=server_read(seat,&settings);
    const qa_server_setting_definition *definition=ready && owner->server_selected?
        qa_server_setting_find(&settings.definitions,owner->server_selected):NULL;
    qa_server_setting_status status; qa_error failure={0};
    if(definition && frontend_server_setting_read(seat->frontend,&settings,definition,&status,&failure)) {
        const char *draft=owner->server_draft?owner->server_draft:"";
        qa_ui_control *item=NULL;
        if(definition->kind==QA_SERVER_SETTING_TOGGLE) item=toggle(seat,"Requested value",!strcmp(draft,"1"),SET_SERVER_VALUE);
        else if(definition->kind==QA_SERVER_SETTING_CHOICE) {
            size_t count=definition->control.choices.count,selected=0;
            const char **labels=cache(owner,count,sizeof(*labels),_Alignof(const char *));
            const char **values=cache(owner,count,sizeof(*values),_Alignof(const char *));
            if(!labels || !values) return finish(seat,FRONTEND_SERVER_SETTING,definition->label,out,error,false);
            for(size_t i=0;i<count;++i) {
                labels[i]=definition->control.choices.items[i].label; values[i]=definition->control.choices.items[i].id;
                if(!strcmp(draft,values[i])) selected=i;
            }
            item=choice(seat,"Requested value",labels,values,count,selected,SET_SERVER_VALUE);
        } else {
            item=control(seat,"Requested value",QA_UI_FIELD,SET_SERVER_VALUE);
            if(item) { item->value.field.text=draft; item->value.field.maximum=
                definition->kind==QA_SERVER_SETTING_TEXT?definition->control.maximum_length:24; }
        }
        server_info(seat,server_label(owner,"Setting ID: ",definition->id,""));
        server_info(seat,server_label(owner,"Desired: ",status.desired,""));
        server_info(seat,server_label(owner,"Effective: ",status.effective,""));
        server_info(seat,server_label(owner,"Default: ",definition->default_value,""));
        server_info(seat,server_label(owner,status.pending?"Pending; applies at ":"Apply timing: ",
            qa_server_setting_apply_name(definition->apply_at),""));
        if(definition->kind==QA_SERVER_SETTING_NUMBER) {
            char range[128]; snprintf(range,sizeof(range),"%s from %.12g to %.12g",definition->control.number.integer?"Whole numbers":"Numbers",
                definition->control.number.minimum,definition->control.number.maximum);
            server_info(seat,copy_text(owner,range));
        }
        button(seat,"Apply",SET_SERVER_APPLY); button(seat,"Use default",SET_SERVER_DEFAULT);
    } else {
        if(definition) server_message(owner,&failure);
        server_info(seat,"This setting is no longer available.");
    }
    if(owner->server_message[0]) server_info(seat,copy_text(owner,owner->server_message));
    return finish(seat,FRONTEND_SERVER_SETTING,definition?definition->label:"Server setting",out,error,false);
}
static bool server_profiles_menu(void *context,uint32_t seat_id,qa_ui_menu *out,qa_error *error)
{
    (void)seat_id;
    frontend_seat *seat=context; frontend_settings_menu *owner=seat->settings_menu; begin(seat);
    frontend_server_settings settings; bool ready=server_read(seat,&settings);
    qa_ui_control *item=control(seat,"Profile name",QA_UI_FIELD,SET_SERVER_PROFILE_NAME);
    if(item) { item->value.field.text=owner->server_profile_name; item->value.field.maximum=32; }
    server_info(seat,"A short name, such as league-night. No file paths.");
    item=button(seat,"Save current desired settings",SET_SERVER_PROFILE_SAVE); if(item) item->enabled=ready;
    item=button(seat,"Load and apply profile",SET_SERVER_PROFILE_LOAD); if(item) item->enabled=ready;
    server_info(seat,"Loaded settings keep their normal apply timing.");
    size_t pages=(owner->server_profiles.count+6)/7;
    if(!pages) pages=1;
    if(owner->server_profile_page>=pages) owner->server_profile_page=pages-1;
    for(size_t i=owner->server_profile_page*7;i<owner->server_profiles.count && i<(owner->server_profile_page+1)*7;++i) {
        const char *file=owner->server_profiles.entries[i].name; size_t size=strlen(file)-5;
        char *name=cache(owner,size+1,1,1);
        if(!name) break;
        memcpy(name,file,size); name[size]=0;
        item=button(seat,name,SET_SERVER_PROFILE_SELECT); if(item) binding_of(item)->name=name;
    }
    if(!owner->server_profiles.count) server_info(seat,"No saved server profiles.");
    server_pages(seat,owner->server_profile_page,pages,SET_SERVER_PROFILE_PAGE);
    if(owner->server_message[0]) server_info(seat,copy_text(owner,owner->server_message));
    char title[64]; snprintf(title,sizeof(title),"Server profiles %zu/%zu",owner->server_profile_page+1,pages);
    return finish(seat,FRONTEND_SERVER_PROFILES,copy_text(owner,title),out,error,false);
}
static int compare_size(const void *a,const void *b)
{
    const uint32_t *left=a,*right=b;
    return left[0]<right[0]?-1:left[0]>right[0]?1:left[1]<right[1]?-1:left[1]>right[1]?1:0;
}
static void renderer_controls(frontend_seat *seat,qa_cvars *cvars,const qa_display_info *info)
{
    qa_ui_control *item=cvar(seat,cvars,UI_CVAR_R_SMP,"Render worker",QA_UI_TOGGLE,0,0,0);
    if(item) binding_of(item)->restart_video=true;
    static const char *const renderers[]={"CPU","OpenGL"};
    static const char *const renderer_values[]={"cpu","gl"};
    choice(seat,"Renderer",renderers,renderer_values,2,(size_t)seat->menu_renderer,SET_RENDERER);
    item=button(seat,"Apply renderer",SET_RENDERER_APPLY);
    if(item) item->enabled=seat->menu_renderer!=info->backend;
}
static bool display(void *context,uint32_t id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; qa_frontend *f=seat->frontend; (void)id; begin(seat);
    qa_cvars *cvars=qa_application_cvars(f->application);
    qa_display_info info;
    if(!qa_display_info_get(f->display,&info,error)) return false;
    frontend_settings_menu *display_cache=seat->settings_menu;
    if(display_cache->display && display_cache->backend!=info.backend) seat->menu_renderer=info.backend;
    if(!seat->menu_display_initialized || (display_cache->display && display_cache->display!=f->display)) {
        seat->menu_renderer=info.backend;
        snprintf(seat->menu_width,sizeof(seat->menu_width),"%u",f->cpu?f->width:info.logical_width);
        snprintf(seat->menu_height,sizeof(seat->menu_height),"%u",f->cpu?f->height:info.logical_height);
        seat->menu_display_initialized=true;
    }
    display_cache->display=f->display; display_cache->backend=info.backend;
    if(in_game(seat)) renderer_controls(seat,cvars,&info);
    qa_ui_control *item=cvar(seat,cvars,UI_CVAR_R_GAMMA,"Brightness",QA_UI_SLIDER,.5,3,.05);
    item=button(seat,"Reset brightness",SET_BRIGHTNESS_RESET);
    binding_of(item)->cvars=cvars; item->enabled=setting_value(seat,cvars,UI_CVAR_R_GAMMA)->number!=1;
    item=cvar(seat,cvars,UI_CVAR_R_FULLSCREEN,"Borderless fullscreen",QA_UI_TOGGLE,0,0,0);
    binding_of(item)->restart_video=true;
    static const uint32_t standard[][2]={{640,480},{800,600},{960,600},{1024,768},{1280,720},{1280,800},{1600,900},{1920,1080},{2560,1440},{3440,1440},{3840,2160}};
    int modes=SDL_GetNumDisplayModes(info.display_index); if(modes<0) modes=0;
    frontend_settings_menu *owner=seat->settings_menu;
    size_t capacity=(size_t)modes+sizeof(standard)/sizeof(*standard)+1;
    uint32_t (*sizes)[2]=cache(owner,capacity,sizeof(*sizes),_Alignof(uint32_t));
    const char **labels=cache(owner,capacity,sizeof(*labels),_Alignof(const char *));
    if(!sizes || !labels) return finish(seat,FRONTEND_DISPLAY,"Display",out,error,false);
    size_t count=0;
    for(int i=0;i<modes;++i) { SDL_DisplayMode mode; if(!SDL_GetDisplayMode(info.display_index,i,&mode) && mode.w>0 && mode.h>0)
        { sizes[count][0]=(uint32_t)mode.w; sizes[count++][1]=(uint32_t)mode.h; } }
    for(size_t i=0;i<sizeof(standard)/sizeof(*standard);++i) { sizes[count][0]=standard[i][0]; sizes[count++][1]=standard[i][1]; }
    sizes[count][0]=info.logical_width; sizes[count++][1]=info.logical_height;
    qsort(sizes,count,sizeof(*sizes),compare_size);
    size_t unique=0,selected=0;
    for(size_t i=0;i<count;++i) {
        if(i && sizes[i][0]==sizes[i-1][0] && sizes[i][1]==sizes[i-1][1]) continue;
        sizes[unique][0]=sizes[i][0]; sizes[unique][1]=sizes[i][1];
        char text[40]; snprintf(text,sizeof(text),"%u x %u",sizes[i][0],sizes[i][1]); labels[unique]=copy_text(owner,text);
        if(sizes[i][0]==info.logical_width && sizes[i][1]==info.logical_height) selected=unique;
        ++unique;
    }
    item=choice(seat,"Window resolution",labels,NULL,unique,selected,SET_RESOLUTION);
    binding_of(item)->sizes=(const uint32_t (*)[2])sizes; item->enabled=info.fullscreen==QA_DISPLAY_WINDOWED;
    item=control(seat,"Custom width (320-8192)",QA_UI_FIELD,SET_WIDTH);
    item->value.field.text=seat->menu_width; item->value.field.maximum=4; item->enabled=info.fullscreen==QA_DISPLAY_WINDOWED;
    item=control(seat,"Custom height (200-8192)",QA_UI_FIELD,SET_HEIGHT);
    item->value.field.text=seat->menu_height; item->value.field.maximum=4; item->enabled=info.fullscreen==QA_DISPLAY_WINDOWED;
    item=button(seat,"Apply custom window size",SET_SIZE_APPLY);
    uint32_t width,height; item->enabled=info.fullscreen==QA_DISPLAY_WINDOWED && valid_size(seat,&width,&height);
    item=cvar(seat,cvars,UI_CVAR_R_SWAPINTERVAL,"Vertical sync",QA_UI_TOGGLE,0,0,0);
    item->enabled=info.backend==QA_DISPLAY_OPENGL; binding_of(item)->restart_video=true;
    if(!in_game(seat)) renderer_controls(seat,cvars,&info);
    return finish(seat,FRONTEND_DISPLAY,"Display",out,error,false);
}
typedef struct audio_names { frontend_settings_menu *owner; const char **names; size_t count,capacity; } audio_names;
static void audio_name(void *context,const char *name)
{
    audio_names *names=context;
    if(names->count==names->capacity) { qa_error_set(&names->owner->allocation,QA_ERROR_MEMORY,0,"Audio device inventory changed during enumeration"); return; }
    names->names[names->count++]=copy_text(names->owner,name);
}
static bool audio_controls(frontend_seat *seat,qa_error *error)
{
    qa_frontend *f=seat->frontend; frontend_settings_menu *owner=seat->settings_menu;
    qa_cvars *cvars=qa_application_cvars(f->application);
    if(f->device) {
        int devices=SDL_GetNumAudioDevices(0); if(devices<0) devices=0;
        size_t capacity=(size_t)devices+2;
        const char **labels=cache(owner,capacity,sizeof(*labels),_Alignof(const char *));
        if(!labels) return false;
        labels[0]="System default";
        audio_names names={owner,labels,1,capacity};
        if(!qa_audio_device_names(audio_name,&names,error)) return false;
        const char *current=qa_audio_device_requested_configuration(f->device).name;
        size_t selected=0;
        for(size_t i=1;current && i<names.count;++i) if(!strcmp(current,labels[i])) selected=i;
        if(current && !selected) { labels[names.count]=copy_text(owner,current); selected=names.count++; }
        choice(seat,"Output device",labels,labels,names.count,selected,SET_AUDIO_DEVICE);
    }
    static const char *const rates[]={"11025","22050","44100","48000"};
    static const char *const rate_labels[]={"11025 Hz","22050 Hz","44100 Hz","48000 Hz"};
    static const char *const bits[]={"8","16"},*const bit_labels[]={"8-bit","16-bit"};
    static const char *const channels[]={"1","2"},*const channel_labels[]={"Mono","Stereo"};
    const char **output_rates=cache(owner,5,sizeof(*output_rates),_Alignof(const char *));
    const char **output_labels=cache(owner,5,sizeof(*output_labels),_Alignof(const char *));
    if(!output_rates || !output_labels) return false;
    memcpy(output_rates,rates,sizeof(rates)); memcpy(output_labels,rate_labels,sizeof(rate_labels));
    size_t rate_count=4; const qa_cvar_view *rate=setting_value(seat,cvars,UI_CVAR_S_OUTPUTRATE);
    double sample_rate=rate ? strtod(value_text(rate),NULL) : 0;
    bool rate_found=false;
    for(size_t i=0;rate && i<4;++i) if(sample_rate==strtod(rates[i],NULL)) {
        output_rates[i]=copy_text(owner,value_text(rate)); rate_found=true; break;
    }
    if(rate && !rate_found) { output_rates[4]=copy_text(owner,value_text(rate));
        char text[48]; snprintf(text,sizeof(text),"%.0f Hz",sample_rate); output_labels[4]=copy_text(owner,text); rate_count=5; }
    qa_ui_control *item=cvar_choice(seat,cvars,UI_CVAR_S_OUTPUTRATE,"Output sample rate",output_labels,output_rates,rate_count);
    if(item) binding_of(item)->restart_audio=true;
    item=cvar_choice(seat,cvars,UI_CVAR_S_OUTPUTBITS,"Output sample bits",bit_labels,bits,2);
    if(item) binding_of(item)->restart_audio=true;
    item=cvar_choice(seat,cvars,UI_CVAR_S_OUTPUTCHANNELS,"Output channels",channel_labels,channels,2);
    if(item) binding_of(item)->restart_audio=true;
    cvar(seat,cvars,UI_CVAR_VOLUME,"Effects volume",QA_UI_SLIDER,0,1,.05);
    cvar(seat,cvars,UI_CVAR_BGMVOLUME,"Music volume",QA_UI_SLIDER,0,1,.05);
    cvar(seat,cvars,UI_CVAR_S_GEOMETRYACOUSTICS,"Geometry sound obstruction",QA_UI_TOGGLE,0,0,0);
    cvar(seat,cvars,UI_CVAR_MUSIC_SHUFFLE,"Shuffle Quake II gameplay music",QA_UI_TOGGLE,0,0,0);
    frontend_music_policy *policy=frontend_music_sources_policy(f->music_sources,FRONTEND_MUSIC_MENU);
    size_t tracks=frontend_music_policy_track_count(policy), capacity=tracks+3;
    const char **labels=cache(owner,capacity,sizeof(*labels),_Alignof(const char *));
    const char **values=cache(owner,capacity,sizeof(*values),_Alignof(const char *));
    if(!labels || !values) return false;
    labels[0]="Automatic"; labels[1]="Off"; values[0]="auto"; values[1]="0";
    const qa_cvar_view *row=setting_value(seat,cvars,UI_CVAR_MUSIC_MENU_TRACK); size_t count=2, selected=0;
    for(size_t i=0;i<tracks;++i) { const char *track=frontend_music_policy_track_at(policy,i);
        bool found=false; for(size_t j=0;j<count;++j) if(!strcmp(values[j],track)) found=true;
        if(!found) labels[count]=values[count]=track,++count; }
    bool found=false;
    for(size_t i=0;row && i<count;++i) if(!strcmp(row->value,values[i])) { selected=i; found=true; }
    if(row && !found) labels[count]=values[count]=row->value,selected=count++;
    item=choice(seat,"Menu music",labels,values,count,selected,SET_CVAR);
    if(item) { binding_of(item)->cvars=cvars; binding_of(item)->name="music_menu_track";
        binding_of(item)->handle=settings_store_bind(owner,cvars)->handles[UI_CVAR_MUSIC_MENU_TRACK]; }
    return owner->allocation.code==QA_OK;
}
static bool sound(void *context,uint32_t id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; (void)id; begin(seat);
    const qa_launch_choices *selected=seat->library?qa_ui_library_choices(seat->library):NULL;
    if(selected) {
        static const qa_ui_library_field fields[]={QA_UI_LIBRARY_ENVIRONMENT,QA_UI_LIBRARY_DOPPLER};
        static const char *const names[]={"Environment","Doppler"};
        for(size_t field=0;field<2;++field) {
            const qa_ui_library_choice *rows=NULL; size_t count=0; const char *current=NULL;
            if(!qa_ui_library_selection_choices(seat->library,fields[field],&rows,&count,&current,error)) return false;
            const char *selected_label="Game default";
            size_t used=0;
            for(size_t i=0;i<count;++i) {
                if(rows[i].unavailable) continue;
                if(current && !strcmp(current,rows[i].id)) selected_label=rows[i].label;
                ++used;
            }
            size_t size=strlen(names[field])+strlen(selected_label)+3;
            char *label=cache(seat->settings_menu,size,1,1);
            if(!label) return false;
            snprintf(label,size,"%s: %s",names[field],selected_label);
            qa_ui_control *item=control(seat,label,QA_UI_BUTTON,field?SET_DOPPLER:SET_ENVIRONMENT);
            binding_of(item)->library_field=fields[field]; item->enabled=used!=0;
        }
    }
    if(!audio_controls(seat,error)) return false;
    return finish(seat,FRONTEND_SOUND,"Sound",out,error,true);
}
static bool audio_menu(void *context,uint32_t id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; (void)id; begin(seat);
    return audio_controls(seat,error) && finish(seat,FRONTEND_AUDIO_OPTIONS,"Audio",out,error,false);
}
static bool primary_input_controls(frontend_seat *seat)
{
    qa_frontend *f=seat->frontend; frontend_settings_menu *owner=seat->settings_menu;
    qa_ui_control *item=NULL;
    qa_haptic_player *haptics=qa_input_platform_haptics(f->input,seat->id);
    if(haptics) { toggle(seat,"Controller vibration",haptics->enabled,SET_RUMBLE);
        slider(seat,"Vibration strength",haptics->strength,0,1,.05,SET_RUMBLE_STRENGTH); }
    qa_cvars *mouse=NULL; (void)client_cvars(seat,&mouse);
    if(mouse) {
        cvar(seat,mouse,UI_CVAR_SENSITIVITY,"Mouse sensitivity",QA_UI_SLIDER,.1,20,.1);
        const setting_cvar axes[]={UI_CVAR_M_YAW,UI_CVAR_M_PITCH};
        const char *const axis_labels[]={"Horizontal sensitivity","Vertical sensitivity"};
        for(size_t i=0;i<2;++i) {
            const qa_cvar_view *row=setting_value(seat,mouse,axes[i]); if(!row) continue;
            item=slider(seat,axis_labels[i],fabs(row->number)/.022*100,0,200,1,SET_MOUSE_AXIS);
            setting_binding *binding=binding_of(item); binding->cvars=mouse; binding->name=setting_names[axes[i]];
            binding->handle=settings_store_bind(owner,mouse)->handles[axes[i]];
            char text[48]; snprintf(text,sizeof(text),"%.2f",item->value.slider.value);
            size_t length=strlen(text);
            while(length && text[length-1]=='0') text[--length]='\0';
            if(length && text[length-1]=='.') text[--length]='\0';
            text[length++]='%'; text[length]='\0'; item->value.slider.label=copy_text(owner,text);
        }
        const qa_cvar_view *pitch=setting_value(seat,mouse,UI_CVAR_M_PITCH);
        if(pitch) { item=toggle(seat,"Invert mouse",signbit(pitch->number)!=0,SET_MOUSE_INVERT); binding_of(item)->cvars=mouse; binding_of(item)->name="m_pitch"; binding_of(item)->handle=settings_store_bind(owner,mouse)->handles[UI_CVAR_M_PITCH]; }
        cvar(seat,mouse,UI_CVAR_CL_RUN,"Always run",QA_UI_TOGGLE,0,0,0);
        cvar(seat,mouse,UI_CVAR_CL_MOUSEACCEL,"Mouse acceleration",QA_UI_SLIDER,0,2,.05);
        cvar(seat,mouse,UI_CVAR_M_FILTER,"Mouse smoothing",QA_UI_TOGGLE,0,0,0);
        item=cvar(seat,mouse,UI_CVAR_LOOKSPRING,"Look spring",QA_UI_TOGGLE,0,0,0);
        if(item) { const qa_cvar_view *free_look=setting_value(seat,mouse,UI_CVAR_FREELOOK);
            qa_ruleset_id dialect=qa_cvars_dialect(mouse);
            item->enabled=(dialect==QA_RULESET_NETQUAKE || dialect==QA_RULESET_QUAKEWORLD) && (!free_look || !free_look->integer); }
        cvar(seat,mouse,UI_CVAR_LOOKSTRAFE,"Look strafe",QA_UI_TOGGLE,0,0,0);
        cvar(seat,mouse,UI_CVAR_FREELOOK,"Free look",QA_UI_TOGGLE,0,0,0);
    }
    return owner->allocation.code==QA_OK;
}
static bool device_input_controls(frontend_seat *seat)
{
    qa_frontend *f=seat->frontend; frontend_settings_menu *owner=seat->settings_menu;
    qa_ui_control *item=NULL;
    qa_cvars *engine=qa_application_cvars(f->application);
    item=cvar(seat,engine,UI_CVAR_IN_MIDI,"MIDI input",QA_UI_TOGGLE,0,0,0); if(item) binding_of(item)->restart_input=true;
    size_t local_seats=f->options.seats;
    const char **seat_labels=cache(owner,local_seats,sizeof(*seat_labels),_Alignof(const char *));
    const char **seat_values=cache(owner,local_seats,sizeof(*seat_values),_Alignof(const char *));
    if(!seat_labels || !seat_values) return false;
    for(size_t i=0;i<local_seats;++i) { char text[32],number[12]; snprintf(text,sizeof(text),"Player %zu",i+1);
        snprintf(number,sizeof(number),"%zu",i+1); seat_labels[i]=copy_text(owner,text); seat_values[i]=copy_text(owner,number); }
    item=cvar_choice(seat,engine,UI_CVAR_IN_MIDISEAT,"MIDI player",seat_labels,seat_values,local_seats); if(item) binding_of(item)->restart_input=true;
    qa_midi_device *midi=NULL; size_t midi_count=0;
    qa_error midi_error={0};
    if(qa_input_midi_devices(&midi,&midi_count,&midi_error)) {
        const char **midi_labels=midi_count?cache(owner,midi_count,sizeof(*midi_labels),_Alignof(const char *)):NULL;
        const char **midi_values=midi_count?cache(owner,midi_count,sizeof(*midi_values),_Alignof(const char *)):NULL;
        for(size_t i=0;midi_labels && midi_values && i<midi_count;++i) { char number[32]; snprintf(number,sizeof(number),"%zu",i);
            midi_labels[i]=copy_text(owner,midi[i].name); midi_values[i]=copy_text(owner,number); }
        item=cvar_choice(seat,engine,UI_CVAR_IN_MIDIDEVICE,"MIDI device",midi_labels,midi_values,midi_count);
        if(item) { item->enabled=midi_count!=0; binding_of(item)->restart_input=true; }
    } else {
        item=cvar_choice(seat,engine,UI_CVAR_IN_MIDIDEVICE,"MIDI device",NULL,NULL,0);
        if(item) item->enabled=false;
    }
    free(midi);
    item=cvar(seat,engine,UI_CVAR_IN_MIDICHANNEL,"MIDI channel",QA_UI_SLIDER,1,16,1); if(item) binding_of(item)->restart_input=true;
    item=cvar(seat,engine,UI_CVAR_IN_JOYSTICK,"Source joystick input",QA_UI_TOGGLE,0,0,0); if(item) binding_of(item)->restart_input=true;
    item=cvar_choice(seat,engine,UI_CVAR_IN_JOYSTICKSEAT,"Joystick player",seat_labels,seat_values,local_seats); if(item) binding_of(item)->restart_input=true;
    static const char *const joystick_labels[]={"Linux axes","Windows POV and ball"},*const joystick_values[]={"linux","windows"};
    item=cvar_choice(seat,engine,UI_CVAR_IN_JOYSTICKPROFILE,"Joystick profile",joystick_labels,joystick_values,2); if(item) binding_of(item)->restart_input=true;
    item=cvar(seat,engine,UI_CVAR_JOY_THRESHOLD,"Joystick axis threshold",QA_UI_SLIDER,.01,1,.01); if(item) binding_of(item)->restart_input=true;
    size_t seats=f->options.seats;
    if(seat->menu_controller_seat>=seats) seat->menu_controller_seat=0;
    const char **labels=cache(owner,seats+1,sizeof(*labels),_Alignof(const char *));
    if(!labels) return false;
    for(size_t i=0;i<seats;++i) { char text[32]; snprintf(text,sizeof(text),"Player %zu",i+1); labels[i]=copy_text(owner,text); }
    labels[seats]="None";
    int keyboard=-1; (void)frontend_settings_keyboard_read(f,&keyboard);
    choice(seat,"Keyboard and mouse player",labels,labels,seats+1,keyboard<0?seats:(size_t)keyboard,SET_KEYBOARD);
    item=choice(seat,"Assign controller to",labels,labels,seats,seat->menu_controller_seat,SET_CONTROLLER_SEAT);
    item->enabled=seats>1;
    size_t devices=qa_input_platform_device_count(f->input),capacity=devices+3;
    labels=cache(owner,capacity,sizeof(*labels),_Alignof(const char *));
    qa_controller_selection *selectors=cache(owner,capacity,sizeof(*selectors),_Alignof(qa_controller_selection));
    if(!labels || !selectors) return false;
    labels[0]="Automatic"; labels[1]="None";
    selectors[0]=(qa_controller_selection){.kind=QA_CONTROLLER_AUTO}; selectors[1]=(qa_controller_selection){.kind=QA_CONTROLLER_NONE};
    qa_controller_selection current={.kind=QA_CONTROLLER_AUTO};
    (void)qa_input_platform_selection(f->input,seat->menu_controller_seat,&current);
    size_t count=2,selected=current.kind==QA_CONTROLLER_NONE?1:0; bool found=current.kind<=QA_CONTROLLER_NONE;
    for(size_t i=0;i<devices;++i) {
        qa_controller_info info; if(!qa_input_platform_device(f->input,i,&info) || !*info.guid) continue;
        qa_controller_selection *target=selectors+count;
        *target=(qa_controller_selection){.kind=info.serial && *info.serial?QA_CONTROLLER_SERIAL:QA_CONTROLLER_GUID,.ordinal=info.ordinal};
        memcpy(target->guid,info.guid,sizeof(target->guid)); target->serial=info.serial && *info.serial?copy_text(owner,info.serial):NULL;
        size_t size=strlen(info.name)+32; char *label=cache(owner,size,1,1);
        if(!label) return false;
        snprintf(label,size,"%s (%u)",info.name,info.ordinal+1); labels[count]=label;
        if(qa_controller_selection_equal(&current,target)) selected=count,found=true;
        ++count;
    }
    if(!found) { selectors[count]=current; selectors[count].serial=current.serial?copy_text(owner,current.serial):NULL;
        labels[count]="Saved controller (disconnected)"; selected=count++; }
    item=choice(seat,"Controller device",labels,labels,count,selected,SET_CONTROLLER);
    binding_of(item)->controllers=selectors;
    return owner->allocation.code==QA_OK;
}
static bool gamepad_controls(frontend_seat *seat)
{
    frontend_settings_menu *owner=seat->settings_menu;
    qa_ui_control *item=NULL;
    qa_gamepad_tuning *pad=qa_input_seat_gamepad_tuning(seat->input);
    item=toggle(seat,"Invert controller",pad->invert_pitch,SET_GAMEPAD_BOOL); binding_of(item)->offset=offsetof(qa_gamepad_tuning,invert_pitch);
    item=toggle(seat,"Swap controller sticks",pad->swap_sticks,SET_GAMEPAD_BOOL); binding_of(item)->offset=offsetof(qa_gamepad_tuning,swap_sticks);
    static const struct { const char *label; size_t offset; double minimum,maximum,step; } specs[]={
        {"Controller turn speed",offsetof(qa_gamepad_tuning,yaw_speed),30,720,10},
        {"Controller look speed",offsetof(qa_gamepad_tuning,pitch_speed),30,720,10},
        {"Move stick deadzone",offsetof(qa_gamepad_tuning,move.deadzone),0,.5,.01},
        {"Look stick deadzone",offsetof(qa_gamepad_tuning,look.deadzone),0,.5,.01},
        {"Look response curve",offsetof(qa_gamepad_tuning,look.exponent),.5,4,.1},
        {"Trigger threshold",offsetof(qa_gamepad_tuning,trigger_threshold),.05,.95,.05},
        {"Move response curve",offsetof(qa_gamepad_tuning,move.exponent),.5,4,.1},
        {"Forward controller sensitivity",offsetof(qa_gamepad_tuning,forward_sensitivity),0,3,.05},
        {"Side controller sensitivity",offsetof(qa_gamepad_tuning,side_sensitivity),0,3,.05}};
    for(size_t i=0;i<sizeof(specs)/sizeof(*specs);++i) { item=slider(seat,specs[i].label,
        *(const float *)((const char *)pad+specs[i].offset),specs[i].minimum,specs[i].maximum,specs[i].step,SET_GAMEPAD_FLOAT);
        binding_of(item)->offset=specs[i].offset; }
    static const char *const shapes[]={"Radial","Axial"};
    qa_gamepad_preview preview; qa_gamepad_preview_read(qa_input_seat_gamepad(seat->input),pad,&preview);
    for(size_t i=0;i<2;++i) {
        qa_stick_curve *curve=i?&pad->look:&pad->move;
        size_t offset=i?offsetof(qa_gamepad_tuning,look):offsetof(qa_gamepad_tuning,move);
        item=choice(seat,i?"Look deadzone shape":"Move deadzone shape",shapes,shapes,2,(size_t)curve->kind,SET_CURVE);
        binding_of(item)->offset=offset;
        item=slider(seat,i?"Look outer threshold":"Move outer threshold",curve->kind==QA_STICK_RADIAL?curve->outer_threshold:0,0,.49,.01,SET_GAMEPAD_FLOAT);
        binding_of(item)->offset=offset+offsetof(qa_stick_curve,outer_threshold); item->enabled=curve->kind==QA_STICK_RADIAL;
        qa_vec2 raw=i?preview.look_raw:preview.move_raw,curved=i?preview.look_curved:preview.move_curved;
        for(size_t axis=0;axis<2;++axis) {
            item=slider(seat,i?(axis?"Look Y live":"Look X live"):(axis?"Move Y live":"Move X live"),axis?curved.y:curved.x,-1,1,.01,SET_GAMEPAD_FLOAT);
            item->enabled=false; char text[80]; snprintf(text,sizeof(text),"Raw %.2f / %.2f",(double)(axis?raw.y:raw.x),(double)(axis?curved.y:curved.x));
            item->value.slider.label=copy_text(owner,text);
        }
    }
    return owner->allocation.code==QA_OK;
}
static void gameplay_input_controls(frontend_seat *seat)
{
    qa_cvars *mouse=NULL,*client=client_cvars(seat,&mouse); (void)mouse;
    if(client) {
        qa_ruleset_id dialect=qa_cvars_dialect(client);
        if(dialect==QA_RULESET_Q3) cvar(seat,client,UI_CVAR_CG_AUTOSWITCH,"Switch to picked-up weapons",QA_UI_TOGGLE,0,0,0);
        if(dialect==QA_RULESET_Q2_RERELEASE) {
            static const char *const labels_auto[]={"Smart","Always","Except consumable weapons","Never"},*const values_auto[]={"0","1","2","3"};
            cvar_choice(seat,client,UI_CVAR_AUTOSWITCH,"Switch to picked-up weapons",labels_auto,values_auto,4);
        }
        if(dialect==QA_RULESET_NETQUAKE || dialect==QA_RULESET_QUAKEWORLD) {
            if(setting_value(seat,client,UI_CVAR_QTS_WEAPON_AUTOSWITCH)) {
                if(q1_pickup_shared(seat)) {
                    static const char *const labels_auto[]={"Always","New weapons","Never"},*const values_auto[]={"always","new","never"};
                    cvar_choice(seat,client,UI_CVAR_QTS_WEAPON_AUTOSWITCH,"Switch to picked-up weapons",labels_auto,values_auto,3);
                } else {
                    qa_ui_control *item=button(seat,"This game controls weapon pickup switching",SET_CLOSE);
                    if(item) item->enabled=false;
                }
            }
        }
    }
}
static bool controls(void *context,uint32_t id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; (void)id; begin(seat);
    bool live=in_game(seat);
    if(live) {
        destination(seat,"Key and controller bindings",FRONTEND_BINDINGS);
        destination(seat,"Gyro controls",FRONTEND_GYRO);
    } else {
        char label[40]; snprintf(label,sizeof(label),"Bindings (Player %u)",seat->id+1);
        destination(seat,copy_text(seat->settings_menu,label),FRONTEND_BINDINGS);
        if(!primary_input_controls(seat)) return finish(seat,FRONTEND_CONTROLS,"Controls",out,error,false);
    }
    if(!device_input_controls(seat)) return finish(seat,FRONTEND_CONTROLS,"Controls",out,error,false);
    if(live) {
        bool join,drop;
        if(!frontend_startup_local_players_read(seat,&join,&drop,error)) return false;
        qa_ui_control *item=button(seat,"Add local player",SET_LOCAL_JOIN); if(item) item->enabled=join;
        item=button(seat,"Remove this player",SET_LOCAL_DROP); if(item) item->enabled=drop;
        if(!primary_input_controls(seat)) return finish(seat,FRONTEND_CONTROLS,"Controls",out,error,false);
    }
    if(!gamepad_controls(seat)) return finish(seat,FRONTEND_CONTROLS,"Controls",out,error,false);
    if(live) gameplay_input_controls(seat);
    else destination(seat,"Gyro controls",FRONTEND_GYRO);
    return finish(seat,FRONTEND_CONTROLS,"Controls",out,error,false);
}
static bool graphics(void *context,uint32_t id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; (void)id; begin(seat);
    qa_cvars *cvars=qa_application_cvars(seat->frontend->application);
    static const char *const labels[]={"Disabled","Replace classic formats","Replace all formats"},*const values[]={"0","1","2"};
    cvar_choice(seat,cvars,UI_CVAR_R_OVERRIDE_TEXTURES,"Replacement images",labels,values,3);
    cvar(seat,cvars,UI_CVAR_R_TEXTURE_FORMATS,"Formats (source = default)",QA_UI_FIELD,0,128,0);
    static const char *const usages[]={"Replace model skins","Replace sprites","Replace wall textures","Replace pictures","Replace sky images"};
    for(size_t i=0;i<5;++i) {
        qa_ui_control *item=cvar(seat,cvars,UI_CVAR_R_TEXTURE_OVERRIDES,usages[i],QA_UI_TOGGLE,0,0,0);
        if(item) { setting_binding *binding=binding_of(item); binding->operation=SET_MASK; binding->mask=1u<<i;
            item->value.checked=((uint32_t)setting_value(seat,cvars,UI_CVAR_R_TEXTURE_OVERRIDES)->integer&binding->mask)!=0; }
    }
    cvar(seat,cvars,UI_CVAR_R_ENHANCEDMODELS,"Q1 enhanced models",QA_UI_TOGGLE,0,0,0);
    cvar(seat,cvars,UI_CVAR_GL_MD5_LOAD,"Load Q2 enhanced models",QA_UI_TOGGLE,0,0,0);
    cvar(seat,cvars,UI_CVAR_GL_MD5_USE,"Draw Q2 enhanced models",QA_UI_TOGGLE,0,0,0);
    qa_ui_control *item=cvar(seat,cvars,UI_CVAR_R_MODEL_DISTANCE,"Model range (map units)",QA_UI_FIELD,0,16,0);
    if(item) { binding_of(item)->submit_only=true; if(seat->selected_setting==item->id) item->value.field.text=seat->setting_value; }
    qa_cvars *mouse=NULL,*client=client_cvars(seat,&mouse); (void)mouse;
    if(client && (qa_cvars_dialect(client)==QA_RULESET_Q2_CLASSIC || qa_cvars_dialect(client)==QA_RULESET_Q2_RERELEASE))
        cvar(seat,client,UI_CVAR_FOV,"Field of view",QA_UI_SLIDER,1,160,1);
    return finish(seat,FRONTEND_GRAPHICS,"Graphics",out,error,false);
}
static bool network(void *context,uint32_t id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; (void)id; begin(seat);
    if(!frontend_network_remote(seat->frontend)) {
        frontend_server_settings settings;
        destination(seat,"Server settings",FRONTEND_SERVER_SETTINGS);
        seat->controls[seat->settings_menu->count-1].enabled=server_read(seat,&settings);
    }
    uint32_t ranking_slot;
    if(seat->rankings && qa_application_rankings(seat->frontend->application)) {
        destination(seat,"Ranking account",FRONTEND_RANKINGS);
        seat->controls[seat->settings_menu->count-1].enabled=
            qa_application_rankings_client_slot(seat->frontend->application,seat->actor,&ranking_slot);
    }
    qa_cvars *mouse=NULL,*client=client_cvars(seat,&mouse); (void)mouse;
    if(client) {
        qa_ruleset_id dialect=qa_cvars_dialect(client);
        setting_cvar variable=dialect==QA_RULESET_NETQUAKE && !setting_value(seat,client,UI_CVAR_NAME)?UI_CVAR__CL_NAME:UI_CVAR_NAME;
        qa_ui_control *item=cvar(seat,client,variable,"Player name",QA_UI_FIELD,0,dialect==QA_RULESET_NETQUAKE?15:63,0);
        if(item) { binding_of(item)->submit_only=true; if(seat->selected_setting==item->id) item->value.field.text=seat->setting_value; }
        const setting_cvar variables[]={UI_CVAR_MODEL,UI_CVAR_HEADMODEL,UI_CVAR_SKIN};
        const char *const labels[]={"Player model / skin","Head model / skin","Player skin (model/skin)"};
        for(size_t i=dialect==QA_RULESET_Q3?0:2;i<(dialect==QA_RULESET_Q3?2:3);++i) {
            if(dialect!=QA_RULESET_Q3 && dialect!=QA_RULESET_Q2_CLASSIC && dialect!=QA_RULESET_Q2_RERELEASE) break;
            item=cvar(seat,client,variables[i],labels[i],QA_UI_FIELD,0,63,0);
            if(item) { binding_of(item)->submit_only=true; if(seat->selected_setting==item->id) item->value.field.text=seat->setting_value; }
        }
        const char **colors=cache(seat->settings_menu,14,sizeof(*colors),_Alignof(const char *));
        if(!colors) return false;
        for(size_t i=0;i<14;++i) { char text[12]; snprintf(text,sizeof(text),"%zu",i); colors[i]=copy_text(seat->settings_menu,text); }
        if(dialect==QA_RULESET_NETQUAKE) {
            setting_cvar color=setting_value(seat,client,UI_CVAR_COLOR)?UI_CVAR_COLOR:UI_CVAR__CL_COLOR;
            const qa_cvar_view *row=setting_value(seat,client,color);
            if(row) for(unsigned i=0;i<2;++i) {
                item=choice(seat,i?"Pants color":"Shirt color",colors,colors,14,((unsigned)row->integer>>(i?0:4))&15u,SET_PACKED_COLOR);
                setting_binding *binding=binding_of(item); binding->cvars=client; binding->name=setting_names[color]; binding->handle=settings_store_bind(seat->settings_menu,client)->handles[color]; binding->mask=i?0:4;
            }
        } else if(dialect==QA_RULESET_QUAKEWORLD) {
            cvar_choice(seat,client,UI_CVAR_TOPCOLOR,"Shirt color",colors,colors,14);
            cvar_choice(seat,client,UI_CVAR_BOTTOMCOLOR,"Pants color",colors,colors,14);
        } else if(dialect==QA_RULESET_Q2_CLASSIC || dialect==QA_RULESET_Q2_RERELEASE) {
            static const char *const hands[]={"Right","Left","Center"},*const hand_values[]={"0","1","2"};
            cvar_choice(seat,client,UI_CVAR_HAND,"Weapon hand",hands,hand_values,3);
        }
    }
    return finish(seat,FRONTEND_NETWORK_OPTIONS,"Network",out,error,false);
}
static bool language(void *context,uint32_t id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; (void)id; begin(seat);
    qa_vfs_listing listing={0};
    if(!qa_vfs_list(seat->frontend->ui_mounts,"localization",".txt",&listing,error)) return false;
    qa_ui_preferences preferences;
    if(!qa_ui_preferences_read(qa_application_cvars(seat->frontend->application),qa_application_ui_preference_handles(seat->frontend->application),seat->id,&preferences,error)) { qa_vfs_listing_free(&listing); return false; }
    const char **languages=cache(seat->settings_menu,listing.count+2,sizeof(*languages),_Alignof(const char *));
    if(!languages) { qa_vfs_listing_free(&listing); return false; }
    size_t count=1,selected=0; languages[0]="english";
    for(size_t i=0;i<listing.count;++i) {
        const char *name=listing.names[i],*base=strrchr(name,'/'); if(base) name=base+1;
        size_t length=strlen(name);
        if(length<=8 || strncmp(name,"loc_",4) || strcmp(name+length-4,".txt")) continue;
        if(length>12 && !strcmp(name+length-8,"_mod.txt")) continue;
        size_t amount=length-8; char *text=cache(seat->settings_menu,amount+1,1,1);
        if(!text) { qa_vfs_listing_free(&listing); return false; }
        memcpy(text,name+4,amount); text[amount]=0;
        bool found=false; for(size_t j=0;j<count;++j) if(!strcmp(languages[j],text)) found=true;
        if(!found && qa_localization_language_valid(text)) languages[count++]=text;
    }
    bool found=false;
    for(size_t i=0;i<count;++i) if(!strcmp(languages[i],preferences.language)) selected=i,found=true;
    if(!found) { languages[count]=copy_text(seat->settings_menu,preferences.language); selected=count++; }
    qa_vfs_listing_free(&listing);
    qa_ui_control *item=choice(seat,"Language",languages,languages,count,selected,SET_LANGUAGE);
    binding_of(item)->cvars=qa_application_cvars(seat->frontend->application);
    return finish(seat,FRONTEND_LANGUAGE,"Language",out,error,false);
}
static bool gyro(void *context,uint32_t id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; (void)id; begin(seat);
    qa_frontend *f=seat->frontend; frontend_settings_menu *owner=seat->settings_menu;
    qa_controller_info device={0}; bool found=frontend_settings_controller_read(f,seat->id,&device);
    bool capable=found && device.sensors[SDL_SENSOR_GYRO - 1], enabled=capable;
    qa_gyro_status status=qa_gamepad_calibration_status(qa_input_seat_gamepad(seat->input));
    qa_gamepad_tuning *pad=qa_input_seat_gamepad_tuning(seat->input);
    qa_ui_control *item=button(seat,found?copy_text(owner,device.name):"Controller",SET_CLOSE); item->enabled=false;
    item=toggle(seat,"Gyro aiming",pad->gyro_enabled,SET_GYRO_ENABLE); item->enabled=enabled;
    item=button(seat,status.state==QA_GYRO_READY?"Recalibrate":"Calibrate",SET_GYRO_CALIBRATE);
    item->enabled=enabled && status.state!=QA_GYRO_CALIBRATING;
    item=button(seat,"Cancel calibration",SET_GYRO_CANCEL); item->enabled=status.state==QA_GYRO_CALIBRATING;
    const char *message=!found?"No controller connected.":!capable?"This controller has no gyroscope.":
        status.state==QA_GYRO_READY?"Calibration complete.":"Place the controller on a steady surface.";
    if(status.state==QA_GYRO_CALIBRATING) { char text[80]; snprintf(text,sizeof(text),"Keep still: %.0f%%",(double)status.progress*100); message=copy_text(owner,text); }
    item=button(seat,message,SET_CLOSE); item->enabled=false;
    item=button(seat,"Reset calibration",SET_GYRO_RESET); item->enabled=enabled;
    item=button(seat,"Recalibrate after reconnecting.",SET_CLOSE); item->enabled=false;
    item=button(seat,"",SET_CLOSE); item->enabled=false;
    item=slider(seat,"Gyro yaw sensitivity",pad->gyro_yaw_sensitivity,0,10,.1,SET_GAMEPAD_FLOAT);
    item->enabled=enabled; binding_of(item)->offset=offsetof(qa_gamepad_tuning,gyro_yaw_sensitivity);
    item=slider(seat,"Gyro pitch sensitivity",pad->gyro_pitch_sensitivity,0,10,.1,SET_GAMEPAD_FLOAT);
    item->enabled=enabled; binding_of(item)->offset=offsetof(qa_gamepad_tuning,gyro_pitch_sensitivity);
    static const char *const axes[]={"Y","Z"};
    item=choice(seat,"Gyro yaw axis",axes,axes,2,pad->gyro_yaw_axis,SET_GYRO_AXIS); item->enabled=enabled;
    item=button(seat,"Save gyro settings",SET_GYRO_SAVE); item->enabled=enabled;
    item=button(seat,copy_text(owner,frontend_settings_gyro_message(f,seat->id)),SET_CLOSE); item->enabled=false;
    return finish(seat,FRONTEND_GYRO,"Gyro controls",out,error,false);
}
static void close_gyro(void *context,uint32_t id)
{
    frontend_seat *seat=context; (void)id;
    (void)qa_input_platform_calibration_cancel(seat->frontend->input,seat->id,false,NULL);
}
static void close_fields(void *context,uint32_t id)
{ frontend_seat *seat=context; (void)id; seat->selected_setting=0; seat->setting_value[0]=0; }
bool frontend_settings_accessibility_menu(void *context,uint32_t id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; (void)id; begin(seat);
    qa_cvars *cvars=qa_application_cvars(seat->frontend->application);
    static const char *const scales[]={"Auto","1x","2x","3x","4x"},*const scale_values[]={"0","1","2","3","4"};
    if(in_game(seat)) cvar_choice(seat,cvars,UI_CVAR_CON_SCALE,"Console text size",scales,scale_values,5);
    static const struct { qa_ui_preference preference; const char *label; } preferences[]={
        {QA_UI_PREF_TYPEFACE,"Typeface"},{QA_UI_PREF_COLOR_MODE,"Interface colors"},
        {QA_UI_PREF_HUD_SCALE,"HUD size"},{QA_UI_PREF_TEXT_SCALE,"Text size"},
        {QA_UI_PREF_MENU_SCALE,"Menu size"},{QA_UI_PREF_CROSSHAIR_SIZE,"Crosshair size"},
        {QA_UI_PREF_HIGH_CONTRAST,"High contrast"},{QA_UI_PREF_REDUCED_FLASHES,"Reduce HUD flashes"},
        {QA_UI_PREF_CAPTIONS,"Captions"},{QA_UI_PREF_CROSSHAIR,"Crosshair"}};
    static const char *const typefaces[]={"Standard","Bold"},*const typeface_values[]={"standard","bold"};
    static const char *const colors[]={"Standard","Blue and yellow","Monochrome"},*const color_values[]={"standard","blue-yellow","monochrome"};
    for(size_t i=0;i<sizeof(preferences)/sizeof(*preferences);++i) {
        qa_ui_preference preference=preferences[i].preference;
        const qa_ui_preference_description *description=qa_ui_preference_describe(preference);
        const qa_ui_preference_handles *handles=qa_application_ui_preference_handles(seat->frontend->application);
        const qa_cvar_view *row=qa_cvars_read(cvars,handles->seats[seat->id][preference]);
        if(!row) return frontend_fail(error,QA_ERROR_ARGUMENT,"Accessibility preferences are not registered");
        qa_ui_control *item=NULL;
        if(description->kind==QA_UI_PREFERENCE_RANGE) item=slider(seat,preferences[i].label,row->number,
            description->minimum,description->maximum,description->step,SET_PREFERENCE);
        if(description->kind==QA_UI_PREFERENCE_TOGGLE) item=toggle(seat,preferences[i].label,row->integer!=0,SET_PREFERENCE);
        if(description->kind==QA_UI_PREFERENCE_CHOICE) {
            const char *const *labels=preference==QA_UI_PREF_TYPEFACE?typefaces:colors;
            const char *const *values=preference==QA_UI_PREF_TYPEFACE?typeface_values:color_values;
            size_t selected=0;
            for(size_t j=0;j<description->choice_count;++j) if(!strcmp(values[j],row->value)) selected=j;
            item=choice(seat,preferences[i].label,labels,values,description->choice_count,selected,SET_PREFERENCE);
        }
        if(item) { binding_of(item)->cvars=cvars; binding_of(item)->mask=(uint32_t)preference; }
    }
    qa_ui_control *item=button(seat,"Reset accessibility settings",SET_PREFERENCES_RESET); binding_of(item)->cvars=cvars;
    if(!in_game(seat)) cvar_choice(seat,cvars,UI_CVAR_CON_SCALE,"Console text size",scales,scale_values,5);
    qa_cvars *mouse=NULL,*client=client_cvars(seat,&mouse); (void)mouse;
    if(in_game(seat) && client) {
        cvar(seat,cvars,UI_CVAR_SV_AUTOSAVE,"Autosave on level load",QA_UI_TOGGLE,0,0,0);
        cvar(seat,client,UI_CVAR_CG_DRAWGUN,"Draw weapon",QA_UI_TOGGLE,0,0,0);
        cvar(seat,client,UI_CVAR_CG_SIMPLEITEMS,"Simple items",QA_UI_TOGGLE,0,0,0);
        cvar(seat,client,UI_CVAR_CG_MARKS,"Wall marks",QA_UI_TOGGLE,0,0,0);
        cvar(seat,client,UI_CVAR_CG_DRAWCROSSHAIRNAMES,"Target names",QA_UI_TOGGLE,0,0,0);
    }
    return finish(seat,FRONTEND_ACCESSIBILITY,"Accessibility",out,error,false);
}
static bool reset_existing(frontend_seat *seat,qa_cvars *cvars,setting_cvar variable,qa_error *error)
{ return !setting_value(seat,cvars,variable) || qa_cvars_reset(cvars,setting_names[variable],false,error); }
static bool gameplay_reset(frontend_seat *seat,qa_error *error)
{
    qa_cvars *mouse=NULL,*client=client_cvars(seat,&mouse); (void)mouse;
    if(!in_game(seat) || !client) return frontend_fail(error,QA_ERROR_ARGUMENT,"Player preferences lost their actual game recipient");
    qa_cvars *engine=qa_application_cvars(seat->frontend->application);
    if(!reset_existing(seat,engine,UI_CVAR_SV_AUTOSAVE,error)) return false;
    static const setting_cvar variables[]={UI_CVAR_CG_DRAWGUN,UI_CVAR_CG_SIMPLEITEMS,UI_CVAR_CG_MARKS,UI_CVAR_CG_DRAWCROSSHAIRNAMES};
    for(size_t i=0;i<sizeof(variables)/sizeof(*variables);++i)
        if(!reset_existing(seat,client,variables[i],error)) return false;
    qa_ruleset_id dialect=qa_cvars_dialect(client);
    setting_cvar variable=dialect==QA_RULESET_NETQUAKE && !setting_value(seat,client,UI_CVAR_NAME)?UI_CVAR__CL_NAME:UI_CVAR_NAME;
    if(!reset_existing(seat,client,variable,error)) return false;
    if(dialect==QA_RULESET_Q3) {
        if(!reset_existing(seat,client,UI_CVAR_CG_AUTOSWITCH,error) || !reset_existing(seat,client,UI_CVAR_MODEL,error) ||
            !reset_existing(seat,client,UI_CVAR_HEADMODEL,error)) return false;
    } else if(dialect==QA_RULESET_Q2_CLASSIC || dialect==QA_RULESET_Q2_RERELEASE) {
        if(!reset_existing(seat,client,UI_CVAR_SKIN,error) || !reset_existing(seat,client,UI_CVAR_HAND,error) ||
            !reset_existing(seat,client,UI_CVAR_FOV,error) ||
            (dialect==QA_RULESET_Q2_RERELEASE && !reset_existing(seat,client,UI_CVAR_AUTOSWITCH,error))) return false;
    } else {
        if(q1_pickup_shared(seat) && !reset_existing(seat,client,UI_CVAR_QTS_WEAPON_AUTOSWITCH,error)) return false;
        if(dialect==QA_RULESET_NETQUAKE) {
            if(!reset_existing(seat,client,setting_value(seat,client,UI_CVAR_COLOR)?UI_CVAR_COLOR:UI_CVAR__CL_COLOR,error)) return false;
        } else if(!reset_existing(seat,client,UI_CVAR_TOPCOLOR,error) || !reset_existing(seat,client,UI_CVAR_BOTTOMCOLOR,error)) return false;
    }
    return frontend_settings_devices_save(seat->frontend,error);
}
static bool reset_menu(void *context,uint32_t id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; (void)id; begin(seat);
    qa_ui_control *item=button(seat,"Keep current settings",SET_CLOSE);
    if(item) { item->rect=(qa_scene_rect_f){64,176,512,28}; item->scrolls=false; }
    item=button(seat,"Restore defaults",SET_GAMEPLAY_RESET);
    if(item) { item->rect=(qa_scene_rect_f){64,232,512,28}; item->scrolls=false; item->enabled=in_game(seat); }
    if(seat->settings_menu->allocation.code!=QA_OK) { if(error) *error=seat->settings_menu->allocation; return false; }
    *out=(qa_ui_menu){.id=FRONTEND_GAMEPLAY_RESET,.title="Reset player preferences...",.fullscreen=true,
        .controls=seat->controls,.count=seat->settings_menu->count}; return true;
}
static bool all_options(void *context,uint32_t id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; (void)id; begin(seat);
    destination(seat,"Display",FRONTEND_DISPLAY);
    bool live=in_game(seat);
    if(live) destination(seat,"Graphics",FRONTEND_GRAPHICS);
    destination(seat,"Audio",FRONTEND_AUDIO_OPTIONS);
    destination(seat,"Controls",FRONTEND_CONTROLS);
    qa_cvars *mouse=NULL,*client=client_cvars(seat,&mouse); (void)mouse;
    if(live && client && (setting_value(seat,client,UI_CVAR_NAME) || setting_value(seat,client,UI_CVAR__CL_NAME))) destination(seat,"Network",FRONTEND_NETWORK_OPTIONS);
    destination(seat,"Accessibility",FRONTEND_ACCESSIBILITY);
    if(live) destination(seat,"Language",FRONTEND_LANGUAGE);
    if(frontend_tools_llm(seat->frontend)) destination(seat,"LLM options",FRONTEND_ASSISTANCE);
    if(live && client) destination(seat,"Reset player preferences...",FRONTEND_GAMEPLAY_RESET);
    if(!finish(seat,FRONTEND_ALL_OPTIONS,"Options",out,error,false)) return false;
    out->scrollable=false;
    for(size_t i=0;i<out->count;++i) { seat->controls[i].rect.width=512; seat->controls[i].scrolls=false; }
    return true;
}
static bool match_text_present(const char *text)
{
    qa_bytes bytes={(const uint8_t *)text,strlen(text)}; size_t offset=0; uint32_t scalar;
    while(qa_utf8_next(bytes,&offset,&scalar)) if(!qa_unicode_whitespace(scalar)) return true;
    return false;
}
static void match_button(frontend_seat *seat,const char *label,match_operation operation,bool enabled)
{
    qa_ui_control *item=button(seat,label,SET_MATCH_COMMAND);
    if(item) { binding_of(item)->mask=(uint32_t)operation; item->enabled=enabled; }
}
static void match_field(frontend_seat *seat,const char *label,size_t offset)
{
    qa_ui_control *item=control(seat,label,QA_UI_FIELD,SET_MATCH_TEXT);
    if(item) { binding_of(item)->offset=offset; item->value.field.text=(char *)seat->settings_menu+offset;
        item->value.field.maximum=64; }
}
bool frontend_settings_match_menu(void *context,uint32_t id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; (void)id; begin(seat);
    frontend_settings_menu *owner=seat->settings_menu;
    qa_ui_control *item=choice(seat,"Team",match_team_labels,match_teams,4,owner->match_team,SET_MATCH_SELECT);
    if(item) binding_of(item)->offset=offsetof(frontend_settings_menu,match_team);
    match_button(seat,"Join team",MATCH_JOIN,true);
    match_field(seat,"Player or map",offsetof(frontend_settings_menu,match_target));
    bool target=match_text_present(owner->match_target),bot=match_text_present(owner->match_bot);
    match_button(seat,"Follow player",MATCH_FOLLOW,target);
    item=choice(seat,"Vote",match_vote_labels,match_votes,4,owner->match_vote,SET_MATCH_SELECT);
    if(item) binding_of(item)->offset=offsetof(frontend_settings_menu,match_vote);
    match_button(seat,"Call vote",MATCH_CALL_VOTE,owner->match_vote<2 || target);
    match_button(seat,"Vote yes",MATCH_YES,true); match_button(seat,"Vote no",MATCH_NO,true);
    match_field(seat,"Bot name",offsetof(frontend_settings_menu,match_bot));
    item=choice(seat,"Bot difficulty",match_skills,match_skills,5,owner->match_skill,SET_MATCH_SELECT);
    if(item) binding_of(item)->offset=offsetof(frontend_settings_menu,match_skill);
    match_button(seat,"Add bot",MATCH_ADD,bot); match_button(seat,"Remove named bot",MATCH_REMOVE,bot);
    if(!finish(seat,FRONTEND_MATCH,"Match controls",out,error,false)) return false;
    out->fullscreen=true; out->scroll_rect=(qa_scene_rect_f){64,92,512,306}; out->content_height=442;
    for(size_t i=0;i+1<out->count;++i) seat->controls[i].rect=(qa_scene_rect_f){64,96+(float)i*34,496,30};
    seat->controls[out->count-1].rect=(qa_scene_rect_f){64,416,496,30};
    return true;
}
bool frontend_settings_create(frontend_seat *seat,qa_error *error)
{
    if(!seat || seat->settings_menu) return frontend_fail(error,QA_ERROR_ARGUMENT,"Settings menus require their empty seat cache");
    frontend_settings_menu *owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining authored settings menu cache");
    qa_arena_init(&owner->arena,4096); seat->settings_menu=owner;
    (void)settings_store_bind(owner,qa_application_cvars(seat->frontend->application));
    memcpy(owner->match_bot,"sarge",6); owner->match_skill=2;
    memcpy(owner->server_profile_name,"default",8);
    const qa_ui_menu_registration registrations[]={
        {.id=FRONTEND_DISPLAY,.context=seat,.factory=display},
        {.id=FRONTEND_SOUND,.context=seat,.factory=sound},
        {.id=FRONTEND_CONTROLS,.context=seat,.factory=controls,.close=close_fields},
        {.id=FRONTEND_ALL_OPTIONS,.context=seat,.factory=all_options},
        {.id=FRONTEND_GRAPHICS,.context=seat,.factory=graphics,.close=close_fields},
        {.id=FRONTEND_NETWORK_OPTIONS,.context=seat,.factory=network,.close=close_fields},
        {.id=FRONTEND_SERVER_SETTINGS,.context=seat,.factory=server_settings_menu},
        {.id=FRONTEND_SERVER_SETTING,.context=seat,.factory=server_setting_menu},
        {.id=FRONTEND_SERVER_PROFILES,.context=seat,.factory=server_profiles_menu,.open=server_profiles_open},
        {.id=FRONTEND_LANGUAGE,.context=seat,.factory=language},
        {.id=FRONTEND_GYRO,.context=seat,.factory=gyro,.close=close_gyro},
        {.id=FRONTEND_AUDIO_OPTIONS,.context=seat,.factory=audio_menu},
        {.id=FRONTEND_GAMEPLAY_RESET,.context=seat,.factory=reset_menu},
        {.id=FRONTEND_MATCH,.context=seat,.factory=frontend_settings_match_menu}};
    for(size_t i=0;i<sizeof(registrations)/sizeof(*registrations);++i)
        if(!qa_ui_register(seat->ui,registrations+i,error)) return false;
    return true;
}
void frontend_settings_destroy(frontend_seat *seat)
{
    if(!seat || !seat->settings_menu) return;
    qa_fs_listing_free(&seat->settings_menu->server_profiles);
    free(seat->settings_menu->server_draft);
    qa_arena_destroy(&seat->settings_menu->arena); free(seat->settings_menu); seat->settings_menu=NULL;
}
