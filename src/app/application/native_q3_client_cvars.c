#include "native_q3_client.h"
#include "qa/game_q3_configstrings.h"
#include <stdlib.h>
#include <string.h>

#define A QA_CVAR_ARCHIVE
#define C QA_CVAR_CHEAT
#define R QA_CVAR_READONLY
#define U QA_CVAR_USERINFO
#define S QA_CVAR_SERVERINFO
#define CV(symbol,name,value,flags) {#symbol,name,value,flags,false}
#define MP(symbol,name,value,flags) {#symbol,name,value,flags,true}
const native_client_definition native_client_definitions[] = {
    CV(cg_ignore,"cg_ignore","0",0),
    CV(cg_autoswitch,"cg_autoswitch","1",A),
    CV(cg_drawGun,"cg_drawGun","1",A),
    CV(cg_zoomFov,"cg_zoomfov","22.5",A),
    CV(cg_fov,"cg_fov","90",A),
    CV(cg_viewsize,"cg_viewsize","100",A),
    CV(cg_stereoSeparation,"cg_stereoSeparation","0.4",A),
    CV(cg_shadows,"cg_shadows","1",A),
    CV(cg_gibs,"cg_gibs","1",A),
    CV(cg_draw2D,"cg_draw2D","1",A),
    CV(cg_drawStatus,"cg_drawStatus","1",A),
    CV(cg_drawTimer,"cg_drawTimer","0",A),
    CV(cg_drawFPS,"cg_drawFPS","0",A),
    CV(cg_drawSnapshot,"cg_drawSnapshot","0",A),
    CV(cg_draw3dIcons,"cg_draw3dIcons","1",A),
    CV(cg_drawIcons,"cg_drawIcons","1",A),
    CV(cg_drawAmmoWarning,"cg_drawAmmoWarning","1",A),
    CV(cg_drawAttacker,"cg_drawAttacker","1",A),
    CV(cg_drawCrosshair,"cg_drawCrosshair","4",A),
    CV(cg_drawCrosshairNames,"cg_drawCrosshairNames","1",A),
    CV(cg_drawRewards,"cg_drawRewards","1",A),
    CV(cg_crosshairSize,"cg_crosshairSize","24",A),
    CV(cg_crosshairHealth,"cg_crosshairHealth","1",A),
    CV(cg_crosshairX,"cg_crosshairX","0",A),
    CV(cg_crosshairY,"cg_crosshairY","0",A),
    CV(cg_brassTime,"cg_brassTime","2500",A),
    CV(cg_simpleItems,"cg_simpleItems","0",A),
    CV(cg_addMarks,"cg_marks","1",A),
    CV(cg_lagometer,"cg_lagometer","1",A),
    CV(cg_railTrailTime,"cg_railTrailTime","400",A),
    CV(cg_gun_x,"cg_gunX","0",C),
    CV(cg_gun_y,"cg_gunY","0",C),
    CV(cg_gun_z,"cg_gunZ","0",C),
    CV(cg_centertime,"cg_centertime","3",C),
    CV(cg_runpitch,"cg_runpitch","0.002",A),
    CV(cg_runroll,"cg_runroll","0.005",A),
    CV(cg_bobup,"cg_bobup","0.005",C),
    CV(cg_bobpitch,"cg_bobpitch","0.002",A),
    CV(cg_bobroll,"cg_bobroll","0.002",A),
    CV(cg_swingSpeed,"cg_swingSpeed","0.3",C),
    CV(cg_animSpeed,"cg_animspeed","1",C),
    CV(cg_debugAnim,"cg_debuganim","0",C),
    CV(cg_debugPosition,"cg_debugposition","0",C),
    CV(cg_debugEvents,"cg_debugevents","0",C),
    CV(cg_errorDecay,"cg_errordecay","100",0),
    CV(cg_nopredict,"cg_nopredict","0",0),
    CV(cg_noPlayerAnims,"cg_noplayeranims","0",C),
    CV(cg_showmiss,"cg_showmiss","0",0),
    CV(cg_footsteps,"cg_footsteps","1",C),
    CV(cg_tracerChance,"cg_tracerchance","0.4",C),
    CV(cg_tracerWidth,"cg_tracerwidth","1",C),
    CV(cg_tracerLength,"cg_tracerlength","100",C),
    CV(cg_thirdPersonRange,"cg_thirdPersonRange","40",C),
    CV(cg_thirdPersonAngle,"cg_thirdPersonAngle","0",C),
    CV(cg_thirdPerson,"cg_thirdPerson","0",0),
    CV(cg_teamChatTime,"cg_teamChatTime","3000",A),
    CV(cg_teamChatHeight,"cg_teamChatHeight","0",A),
    CV(cg_forceModel,"cg_forceModel","0",A),
    CV(cg_predictItems,"cg_predictItems","1",A),
    CV(cg_deferPlayers,"cg_deferPlayers","1",A),
    CV(cg_drawTeamOverlay,"cg_drawTeamOverlay","0",A),
    CV(cg_teamOverlayUserinfo,"teamoverlay","0",R|U),
    CV(cg_stats,"cg_stats","0",0),
    CV(cg_drawFriend,"cg_drawFriend","1",A),
    CV(cg_teamChatsOnly,"cg_teamChatsOnly","0",A),
    CV(cg_noVoiceChats,"cg_noVoiceChats","0",A),
    CV(cg_noVoiceText,"cg_noVoiceText","0",A),
    CV(cg_buildScript,"com_buildScript","0",0),
    CV(cg_paused,"cl_paused","0",R),
    CV(cg_blood,"com_blood","1",A),
    CV(cg_synchronousClients,"g_synchronousClients","0",0),
    MP(cg_redTeamName,"g_redteam","Stroggs",A|S|U),
    MP(cg_blueTeamName,"g_blueteam","Pagans",A|S|U),
    MP(cg_currentSelectedPlayer,"cg_currentSelectedPlayer","0",A),
    MP(cg_currentSelectedPlayerName,"cg_currentSelectedPlayerName","",A),
    MP(cg_singlePlayer,"ui_singlePlayerActive","0",U),
    MP(cg_enableDust,"g_enableDust","0",S),
    MP(cg_enableBreath,"g_enableBreath","0",S),
    MP(cg_singlePlayerActive,"ui_singlePlayerActive","0",U),
    MP(cg_recordSPDemo,"ui_recordSPDemo","0",A),
    MP(cg_recordSPDemoName,"ui_recordSPDemoName","",A),
    MP(cg_obeliskRespawnDelay,"g_obeliskRespawnDelay","10",S),
    MP(cg_hudFiles,"cg_hudFiles","ui/hud.txt",A),
    CV(cg_cameraOrbit,"cg_cameraOrbit","0",C),
    CV(cg_cameraOrbitDelay,"cg_cameraOrbitDelay","50",A),
    CV(cg_timescaleFadeEnd,"cg_timescaleFadeEnd","1",0),
    CV(cg_timescaleFadeSpeed,"cg_timescaleFadeSpeed","0",0),
    CV(cg_timescale,"timescale","1",0),
    CV(cg_scorePlum,"cg_scorePlums","1",U|A),
    CV(cg_smoothClients,"cg_smoothClients","0",U|A),
    CV(cg_cameraMode,"com_cameraMode","0",C),
    CV(pmove_fixed,"pmove_fixed","0",0),
    CV(pmove_msec,"pmove_msec","8",0),
    CV(cg_noTaunt,"cg_noTaunt","0",A),
    CV(cg_noProjectileTrail,"cg_noProjectileTrail","0",A),
    CV(cg_smallFont,"ui_smallFont","0.25",A),
    CV(cg_bigFont,"ui_bigFont","0.4",A),
    CV(cg_oldRail,"cg_oldRail","1",A),
    CV(cg_oldRocket,"cg_oldRocket","1",A),
    CV(cg_oldPlasma,"cg_oldPlasma","1",A),
    CV(cg_trueLightning,"cg_trueLightning","0.0",A)
};
const size_t native_client_definition_count = sizeof(native_client_definitions)/sizeof(*native_client_definitions);
_Static_assert(sizeof(native_client_definitions)/sizeof(*native_client_definitions)==QA_NATIVE_CLIENT_CVARS,
    "CGAME continuation schema must cover the exact registration table");
#undef CV
#undef MP
#undef A
#undef C
#undef R
#undef U
#undef S

static size_t symbol_index(const qa_native_q3_client_service *service,const char *symbol)
{
    if (!symbol) return SIZE_MAX;
    for (size_t i=0;i<service->count;++i)
        if ((!native_client_definitions[i].missionpack || service->product==QA_Q3_TEAM_ARENA) &&
            !strcmp(symbol,native_client_definitions[i].symbol)) return i;
    return SIZE_MAX;
}
static bool copy_cvar(qa_native_q3_client_service *service,size_t index,const qa_cvar_view *engine,
    bool forced,qa_error *error)
{
    qa_native_q3_client_cvar *value=&service->cache[index];
    if (!forced && value->modification_count==engine->modification_count) return true;
    /* The source updates modificationCount before its oversized string error.
     * Preserve that failure prefix; do not silently truncate the VM cache. */
    value->modification_count=engine->modification_count;
    size_t length=strlen(engine->value);
    if (length>=sizeof(value->value))
        return native_client_fail(error,QA_ERROR_FORMAT,"Cvar_Update source exceeds MAX_CVAR_VALUE_STRING");
    memcpy(value->value,engine->value,length+1);
    value->number=engine->number; value->integer=engine->integer; return true;
}
bool qa_native_q3_client_cvar_read(const qa_native_q3_client_service *service,const char *symbol,
    qa_native_q3_client_cvar *out,qa_error *error)
{
    if (!service || !out || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME cvar cache lost its actual seat owner");
    size_t index=symbol_index(service,symbol);
    if (index==SIZE_MAX) return native_client_fail(error,QA_ERROR_NOT_FOUND,"Native CGAME cvar symbol is absent for this product");
    *out=service->cache[index];
    if (!strcmp(symbol,"cg_drawStatus") && service->services.status_visible &&
        !service->services.status_visible(service->services.context)) {
        strcpy(out->value,"0"); out->number=0; out->integer=0;
    }
    return true;
}
bool qa_native_q3_client_cvar_number(qa_native_q3_client_service *service,const char *symbol,
    float number,qa_error *error)
{
    qa_native_q3_client_cvar value;
    if (!qa_native_q3_client_cvar_read(service,symbol,&value,error)) return false;
    service->cache[symbol_index(service,symbol)].number=number; return true;
}
bool qa_native_q3_client_cvar_integer(qa_native_q3_client_service *service,const char *symbol,
    int32_t integer,qa_error *error)
{
    qa_native_q3_client_cvar value;
    if (!qa_native_q3_client_cvar_read(service,symbol,&value,error)) return false;
    service->cache[symbol_index(service,symbol)].integer=integer; return true;
}
static bool register_body(qa_native_q3_client_service *service,qa_error *error)
{
    qa_cvars *registry=service->services.client.cvars;
    for (size_t i=0;i<service->count;++i) {
        const native_client_definition *definition=&native_client_definitions[i];
        if (definition->missionpack && service->product!=QA_Q3_TEAM_ARENA) continue;
        const char *reset=definition->value;
        if (!strcmp(definition->symbol,"cg_deferPlayers") && service->product==QA_Q3_TEAM_ARENA) reset="0";
        if (!qa_cvars_register(registry,definition->name,reset,definition->flags,
            service->services.client.service_owner,"Native Q3 CGAME",error) ||
            !qa_native_q3_client_service_current(service)) return false;
        const qa_cvar_view *value=qa_cvars_find(registry,definition->name);
        if (!value || !copy_cvar(service,i,value,true,error)) return false;
    }
    const qa_cvar_view *running=qa_cvars_find(registry,"sv_running");
    service->local_server=running?running->integer:0;
    service->force_model_count=service->cache[symbol_index(service,"cg_forceModel")].modification_count;
    /* SDK registration follows seat userinfo initialization. Existing actual
     * selected CHARACTER values survive these reset/default declarations. */
    const char *team_model=service->product==QA_Q3_TEAM_ARENA?"james":"sarge";
    const char *team_head=service->product==QA_Q3_TEAM_ARENA?"*james":"sarge";
    if (!qa_cvars_register(registry,"model","sarge",QA_CVAR_USERINFO|QA_CVAR_ARCHIVE,
        service->services.client.service_owner,"Q3 body model",error) ||
        !qa_cvars_register(registry,"headmodel","sarge",QA_CVAR_USERINFO|QA_CVAR_ARCHIVE,
        service->services.client.service_owner,"Q3 head model",error) ||
        !qa_cvars_register(registry,"team_model",team_model,QA_CVAR_USERINFO|QA_CVAR_ARCHIVE,
        service->services.client.service_owner,"Q3 team model",error) ||
        !qa_cvars_register(registry,"team_headmodel",team_head,QA_CVAR_USERINFO|QA_CVAR_ARCHIVE,
        service->services.client.service_owner,"Q3 team head",error) ||
        !qa_native_q3_client_service_current(service)) return false;
    service->registered=true; return true;
}
bool qa_native_q3_client_register(qa_native_q3_client_service *service,qa_error *error)
{
    if (!service || service->updating || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME registration requires its live constructor");
    service->updating=true;
    bool ok=register_body(service,error);
    service->updating=false; return ok;
}
static bool userinfo_defaults(qa_native_q3_client_service *service,const char *name,qa_error *error)
{
    static const struct {const char *name,*value; uint32_t flags;} definitions[]={
        {"cl_timeNudge","0",QA_CVAR_TEMPORARY},
        {"rate","25000",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
        {"cl_maxpackets","30",QA_CVAR_ARCHIVE},
        {"cl_packetdup","1",QA_CVAR_ARCHIVE},
        {"snaps","20",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
        {"color1","4",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
        {"color2","5",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
        {"sex","male",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
        {"cl_anonymous","0",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
        {"cg_predictItems","1",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
        {"teamtask","0",QA_CVAR_USERINFO},{"password","",QA_CVAR_USERINFO},
        {"handicap","100",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
        {"cl_maxPing","800",QA_CVAR_ARCHIVE},
        {"cl_serverStatusResendTime","750",0},{"sv_master1","master.quake3arena.com",0}
    };
    qa_cvars *registry=service->services.client.cvars; uint64_t owner=service->services.client.service_owner;
    /* Registration order is the actual initializeQ3ClientCvars prefix. */
    for (size_t i=0;i<5;++i)
        if (!qa_cvars_register(registry,definitions[i].name,definitions[i].value,definitions[i].flags,owner,
            "Native Q3 seat userinfo",error) || !qa_native_q3_client_service_current(service)) return false;
    if (!qa_cvars_register(registry,"name",name,QA_CVAR_ARCHIVE|QA_CVAR_USERINFO,owner,
        "Native Q3 seat identity",error) || !qa_native_q3_client_service_current(service)) return false;
    const char *names[]={"model","headmodel","team_model","team_headmodel"};
    for (size_t i=0;i<4;++i) {
        const char *model=i%2 && *service->character.head_model?service->character.head_model:service->character.model;
        const char *skin=i%2?service->character.head_skin:service->character.skin;
        size_t a=strlen(model),b=strlen(skin);
        if (a>SIZE_MAX-b-2) return native_client_fail(error,QA_ERROR_MEMORY,"CHARACTER declaration exceeds userinfo capacity");
        char *value=malloc(a+b+2);
        if (!value) return native_client_fail(error,QA_ERROR_MEMORY,"Formatting actual CHARACTER userinfo declaration");
        memcpy(value,model,a); value[a]='/'; memcpy(value+a+1,skin,b+1);
        bool ok=qa_cvars_register(registry,names[i],value,QA_CVAR_ARCHIVE|QA_CVAR_USERINFO,owner,
            "Selected CHARACTER declaration",error);
        free(value);
        if (!ok || !qa_native_q3_client_service_current(service)) return false;
    }
    for (size_t i=5;i<sizeof(definitions)/sizeof(*definitions);++i)
        if (!qa_cvars_register(registry,definitions[i].name,definitions[i].value,definitions[i].flags,owner,
            "Native Q3 seat userinfo",error) || !qa_native_q3_client_service_current(service)) return false;
    return true;
}
bool qa_native_q3_client_userinfo_initialize(qa_native_q3_client_service *service,const char *name,qa_error *error)
{
    if (!name || !service || service->updating || service->registered || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native Q3 userinfo requires its actual pre-registration constructor");
    service->updating=true; bool ok=userinfo_defaults(service,name,error); service->updating=false; return ok;
}
static bool reload(qa_native_q3_client_service *service,qa_error *error)
{
    for (uint32_t slot=0;slot<64;++slot) {
        const char *text;
        if (!qa_q3_configstring_read(service->source_game,544+slot,&text,error)) return false;
        if (*text && (!service->services.reload_client_info(service->services.context,slot,text,error) ||
            !qa_native_q3_client_service_current(service))) return false;
    }
    return true;
}
bool qa_native_q3_client_force_model_change(qa_native_q3_client_service *service,qa_error *error)
{
    if (!service || !service->registered || service->updating || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME force-model update lacks its idle registered owner");
    service->updating=true; bool ok=reload(service,error); service->updating=false; return ok;
}
bool qa_native_q3_client_update(qa_native_q3_client_service *service,qa_error *error)
{
    if (!service || !service->registered || service->updating || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME update lacks its idle registered owner");
    service->updating=true; bool ok=true;
    for (size_t i=0;ok && i<service->count;++i) {
        const native_client_definition *definition=&native_client_definitions[i];
        if (definition->missionpack && service->product!=QA_Q3_TEAM_ARENA) continue;
        const qa_cvar_view *value=qa_cvars_find(service->services.client.cvars,definition->name);
        if (value) ok=copy_cvar(service,i,value,false,error);
    }
    qa_native_q3_client_cvar *overlay=&service->cache[symbol_index(service,"cg_drawTeamOverlay")];
    if (ok && (service->overlay_initial || service->overlay_count!=overlay->modification_count)) {
        service->overlay_initial=false; service->overlay_count=overlay->modification_count;
        ok=qa_cvars_set(service->services.client.cvars,"teamoverlay",overlay->integer>0?"1":"0",true,error) &&
            qa_native_q3_client_service_current(service) &&
            qa_cvars_set(service->services.client.cvars,"teamoverlay","1",true,error) &&
            qa_native_q3_client_service_current(service);
    }
    qa_native_q3_client_cvar *force=&service->cache[symbol_index(service,"cg_forceModel")];
    if (ok && service->force_model_count!=force->modification_count) {
        service->force_model_count=force->modification_count; ok=reload(service,error);
    }
    service->updating=false; return ok;
}
