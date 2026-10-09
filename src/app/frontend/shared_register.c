#include "shared_register.h"
#include "q3_render_policy.h"
#include "shared_resource_policy.h"
#include "q3_color_policy.h"
#include "legacy_render_policy.h"
#include "shared_settings.h"
#include "shared_video.h"
#include "view_settings.h"
#include "qa/cvars_alias.h"
#include "qa/console_cvar_observer.h"
#include "qa/ui_preferences.h"
#include "qa/text.h"
#include <stdio.h>

typedef enum setting_validation { ANY,FINITE,GAMMA,TOGGLE,RATE,BITS,CHANNELS,MENU_TRACK } setting_validation;
typedef struct shared_declaration {
    const char *name,*initial,*description;
    uint32_t flags;
    setting_validation validation;
} shared_declaration;
static const shared_declaration declarations[]={
    {"sv_autosave","1","Autosave on level entry",QA_CVAR_ARCHIVE,TOGGLE},
    {"fov","90","Field of view",QA_CVAR_ARCHIVE,ANY},
    {"viewsize","100","Quake view size, 30 through 120",QA_CVAR_ARCHIVE,FINITE},
    {"cl_sbar","0","QuakeWorld status display placement",QA_CVAR_ARCHIVE,FINITE},
    {"chase_active","0","Enable the NetQuake chase camera",0,FINITE},
    {"chase_back","100","NetQuake chase distance",0,FINITE},
    {"chase_up","16","NetQuake chase height",0,FINITE},
    {"chase_right","0","NetQuake chase lateral offset",0,FINITE},
    {"con_scale","0","Console text size",QA_CVAR_ARCHIVE,ANY},
    {"con_notifytime","3","Console notification lifetime in seconds",0,FINITE},
    {"r_gamma","1","Display brightness, 0.5 through 3",QA_CVAR_ARCHIVE,GAMMA},
    {"r_shadows","0","Shared model shadows; zero disables",QA_CVAR_ARCHIVE,FINITE},
    {"cl_shadowlights","1","Quake II rerelease shadow lights",0,ANY},
    {"cl_rerelease_effects","1","Quake II rerelease effects",0,ANY},
    {"cl_dlight_hacks","0","Quake II dynamic light compatibility mode",0,ANY},
    {"cl_muzzlelight_time","100","Quake II muzzle light duration",0,ANY},
    {"cl_muzzleflashes","1","Quake II muzzle flash models",0,ANY},
    {"cl_disable_particles","0","Quake II disabled particle recipe mask",0,ANY},
    {"cl_disable_explosions","0","Quake II disabled explosion recipe mask",0,ANY},
    {"cl_hit_markers","2","Quake II hit marker mode",0,ANY},
    {"scr_hit_marker_time","500","Quake II hit marker duration",0,ANY},
    {"r_finish","0","Synchronize Source drawing at view begin",QA_CVAR_ARCHIVE,ANY},
    {"r_showImages","0","Source image residency grid",QA_CVAR_TEMPORARY,ANY},
    {"r_measureOverdraw","0","Measure Source stencil overdraw",QA_CVAR_CHEAT,ANY},
    {"r_speeds","0","Source renderer performance counters",QA_CVAR_CHEAT,ANY},
    {"r_primitives","0","Renderer primitive submission mode",QA_CVAR_ARCHIVE,ANY},
    {"r_allowExtensions","1","Enable renderer extensions at initialization",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
    {"r_ext_compiled_vertex_array","1","Enable compiled vertex arrays at initialization",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
    {"r_detailtextures","1","Detail textures",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
    {"r_vertexLight","0","Vertex lighting",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
    {"r_uifullscreen","0","Fullscreen source UI",0,ANY},
    {"r_ignoreFastPath","1","Disable the source fast rendering path",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
    {"r_ext_multitexture","1","Enable multitexture at initialization",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
#ifdef __linux__
    {"r_ext_texture_env_add","0","Texture environment addition",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
#else
    {"r_ext_texture_env_add","1","Texture environment addition",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
#endif
    {"r_lodCurveError","250","Curve level of detail error",QA_CVAR_ARCHIVE|QA_CVAR_CHEAT,ANY},
    {"r_lodbias","0","Model level of detail bias",QA_CVAR_ARCHIVE,ANY},
    {"r_lodscale","5","Model level of detail scale",QA_CVAR_CHEAT,ANY},
    {"r_railWidth","16","Rail trail width",QA_CVAR_ARCHIVE,ANY},
    {"r_railCoreWidth","6","Rail core width",QA_CVAR_ARCHIVE,ANY},
    {"r_railSegmentLength","32","Rail ring spacing",QA_CVAR_ARCHIVE,ANY},
    {"r_drawworld","1","Draw the source world",QA_CVAR_CHEAT,ANY},
    {"r_drawentities","1","Draw source entities",QA_CVAR_CHEAT,ANY},
    {"r_drawviewmodel","1","Draw the Quake first-person weapon",0,FINITE},
    {"cg_gun_frame","0","Selected weapon animation frame override",0,ANY},
    {"r_nocull","0","Disable source frustum culling",QA_CVAR_CHEAT,ANY},
    {"r_novis","0","Disable source visibility culling",QA_CVAR_CHEAT,ANY},
    {"r_nocurves","0","Disable source curves",QA_CVAR_CHEAT,ANY},
    {"r_lockpvs","0","Retain the source visibility set",QA_CVAR_CHEAT,ANY},
    {"r_noportals","0","Disable source portals",QA_CVAR_CHEAT,ANY},
    {"r_portalOnly","0","Draw only source portals",QA_CVAR_CHEAT,ANY},
    {"r_fastsky","0","Use the source fast sky path",QA_CVAR_ARCHIVE,ANY},
    {"r_facePlaneCull","1","Cull source face planes",QA_CVAR_ARCHIVE,ANY},
    {"r_dynamiclight","1","Source dynamic lighting",QA_CVAR_ARCHIVE,ANY},
    {"r_dlightBacks","1","Light back facing source surfaces",QA_CVAR_ARCHIVE,ANY},
    {"r_ambientScale","0.6","Source ambient light scale",QA_CVAR_CHEAT,ANY},
    {"r_directedScale","1","Source directed light scale",QA_CVAR_CHEAT,ANY},
    {"r_znear","4","Source near clip distance",QA_CVAR_CHEAT,ANY},
    {"r_subdivisions","4","Source curve subdivision size",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
    {"r_mapOverBrightBits","2","Source map overbright bits",QA_CVAR_LATCH,ANY},
    {"r_overBrightBits","1","Source display overbright bits",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
    {"r_intensity","1","Source texture intensity",QA_CVAR_LATCH,ANY},
    {"r_ignorehwgamma","0","Disable source hardware gamma",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
    {"r_roundImagesDown","1","Round source image dimensions down",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
    {"r_simpleMipMaps","1","Source simple mipmap sampling",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
    {"r_colorMipLevels","0","Source mipmap color diagnostics",QA_CVAR_LATCH,ANY},
    {"r_picmip","1","Source texture mip level",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
    {"r_texturebits","0","Source texture storage precision",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
    {"r_ext_compressed_textures","0","Source compressed texture selection",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
    {"r_textureMode","GL_LINEAR_MIPMAP_NEAREST","Source texture sampling filter",QA_CVAR_ARCHIVE,ANY},
    {"r_fullbright","0","Source full bright lighting",QA_CVAR_LATCH|QA_CVAR_CHEAT,ANY},
    {"r_drawBuffer","GL_BACK","Source framebuffer selection",QA_CVAR_CHEAT,ANY},
    {"r_stereo","0","Source stereo rendering",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,ANY},
    {"r_maxpolys","600","Source submitted polygon capacity",0,ANY},
    {"r_maxpolyverts","3000","Source submitted polygon vertex capacity",0,ANY},
    {"r_norefresh","0","Disable source scene refresh",QA_CVAR_CHEAT,ANY},
    {"r_showcluster","0","Source visibility cluster diagnostic",QA_CVAR_CHEAT,ANY},
    {"r_skipBackEnd","0","Skip source backend drawing",QA_CVAR_CHEAT,ANY},
    {"r_debugSort","0","Limit source surface sort",QA_CVAR_CHEAT,ANY},
    {"r_showtris","0","Source wireframe diagnostics",QA_CVAR_CHEAT,ANY},
    {"r_shownormals","0","Source normal diagnostics",QA_CVAR_CHEAT,ANY},
    {"r_showsky","0","Source sky diagnostics",QA_CVAR_CHEAT,ANY},
    {"r_clear","0","Clear the source color buffer",QA_CVAR_CHEAT,ANY},
    {"r_nobind","0","Source texture binding diagnostic",QA_CVAR_CHEAT,ANY},
    {"r_offsetfactor","-1","Source wireframe depth factor",QA_CVAR_CHEAT,ANY},
    {"r_offsetunits","-2","Source wireframe depth units",QA_CVAR_CHEAT,ANY},
    {"r_sky_quality","12","Quake sky subdivision quality",0,ANY},
    {"r_skyalpha","1","Quake sky layer opacity",0,ANY},
    {"r_skyfog","0.5","Quake sky fog factor",0,ANY},
    {"gl_flashblend","0","Legacy source dynamic light blending",0,ANY},
    {"gl_doubleeys","1","Quake eye model scale",0,ANY},
    {"gl_shadows","0","Quake II source model shadows",0,ANY},
    {"r_lightmap","0","Quake lightmap diagnostic",0,ANY},
    {"r_dynamic","1","Quake dynamic lightmaps",0,ANY},
    {"gl_lightmap","0","Quake II lightmap diagnostic",0,ANY},
    {"gl_dynamic","1","Quake II dynamic lightmaps",0,ANY},
    {"gl_modulate","1","Quake II light modulation",QA_CVAR_ARCHIVE,ANY},
    {"gl_monolightmap","0","Quake II lightmap encoding",0,ANY},
    {"gl_saturatelighting","0","Quake II additive lightmaps",0,ANY},
    {"gl_polyblend","1","Legacy fullscreen blend",0,ANY},
    {"gl_cull","1","Legacy triangle face culling",0,ANY},
    {"gl_clear","0","Legacy color buffer clear",0,ANY},
    {"cl_flares","1","Quake II rerelease flare entities",0,ANY},
    {"r_mirroralpha","1","Quake mirror opacity",0,ANY},
    {"gl_texsort","1","Quake texture sorted world drawing",0,ANY},
    {"gl_farclip","65536","Quake sky and world far clip distance",QA_CVAR_ARCHIVE,ANY},
    {"r_saveFontData","0","Export generated Q3 font atlases and DAT records",0,ANY},
    {"volume","0.7","Effects gain; output clamps to zero through one",QA_CVAR_ARCHIVE,FINITE},
    {"bgmvolume","1","Music gain; output clamps to zero through one",QA_CVAR_ARCHIVE,FINITE},
    {"s_geometryAcoustics","0","Shared geometry sound obstruction",QA_CVAR_ARCHIVE,TOGGLE},
    {"s_outputRate","48000","Physical audio output sample rate",QA_CVAR_ARCHIVE,RATE},
    {"s_outputBits","16","Physical audio output sample bits",QA_CVAR_ARCHIVE,BITS},
    {"s_outputChannels","2","Physical audio output channel count",QA_CVAR_ARCHIVE,CHANNELS},
    {"music_shuffle","0","Shuffle mounted Quake II gameplay music",QA_CVAR_ARCHIVE,TOGGLE},
    {"music_menu_track","auto","Menu music: auto, 0, track 1..255 or mounted OGG/WAV path",QA_CVAR_ARCHIVE,MENU_TRACK},
    {"r_customwidth","0","Window width; zero uses current width",QA_CVAR_ARCHIVE,ANY},
    {"r_customheight","0","Window height; zero uses current height",QA_CVAR_ARCHIVE,ANY},
    {"r_fullscreen","0","Borderless desktop fullscreen",QA_CVAR_ARCHIVE,ANY},
    {"r_swapInterval","0","GL vertical synchronization",QA_CVAR_ARCHIVE,ANY},
    {"r_smp","0","Render worker selection applied by video restart",QA_CVAR_ARCHIVE,TOGGLE},
#ifdef __APPLE__
    {"r_inGameVideo","0","Original in-world cinematic playback",QA_CVAR_ARCHIVE,ANY},
#else
    {"r_inGameVideo","1","Original in-world cinematic playback",QA_CVAR_ARCHIVE,ANY},
#endif
    {"gl_debug_linewidth","2","Shared debug line width",0,ANY},
    {"gl_debug_distfrac","0.004","World text distance culling factor",0,ANY},
    {"r_override_textures","1","Replacement image priority",QA_CVAR_ARCHIVE,ANY},
    {"r_texture_overrides","-1","Replacement image usage bitmask",QA_CVAR_ARCHIVE,ANY},
    {"r_texture_formats","source","Replacement image search order",QA_CVAR_ARCHIVE,ANY},
    {"r_enhancedmodels","1","Quake I enhanced model replacements",QA_CVAR_ARCHIVE,ANY},
    {"gl_md5_load","1","Quake II MD5 replacement loading",QA_CVAR_ARCHIVE,ANY},
    {"gl_md5_use","1","Quake II MD5 replacement drawing",QA_CVAR_ARCHIVE,ANY},
    {"gl_md5_distance","2048","Quake II replacement distance",QA_CVAR_ARCHIVE,ANY},
    {"r_model_distance","source","Shared replacement model distance",QA_CVAR_ARCHIVE,ANY}
};
bool frontend_shared_menu_track_valid(const char *value)
{
    if (!value) return false;
    if (!strcmp(value,"auto") || !strcmp(value,"0")) return true;
    bool digits=*value!=0;
    for (const unsigned char *p=(const unsigned char *)value;*p;++p) digits&=*p>='0' && *p<='9';
    if (digits) {
        double number; qa_error ignored={0};
        return qa_parse_number((qa_bytes){(const uint8_t *)value,strlen(value)},&number,&ignored) && number>=1 && number<=255;
    }
    size_t at=0,units=0; uint32_t point=0,first=0,last=0;
    qa_bytes text={(const uint8_t *)value,strlen(value)};
    while (qa_utf8_next(text,&at,&point)) {
        if (!units) first=point;
        last=point; units+=point>0xffff?2:1;
        if (units>255 || point<32 || point==127 || point=='"' || point=='\\') return false;
    }
    if (!units || qa_unicode_whitespace(first) || qa_unicode_whitespace(last)) return false;
    if (((value[0]>='a' && value[0]<='z') || (value[0]>='A' && value[0]<='Z')) && value[1]==':') return false;
    const char *part=value;
    for (const char *p=value;;++p) if (*p=='/' || !*p) {
        size_t length=(size_t)(p-part);
        if (!length || (length==1 && part[0]=='.') || (length==2 && part[0]=='.' && part[1]=='.')) return false;
        if (!*p) break;
        part=p+1;
    }
    const char *name=strrchr(value,'/'); name=name?name+1:value;
    if (!strchr(name,'.')) return true;
    size_t length=strlen(name);
    if (length<4 || name[length-4]!='.') return false;
    char ending[4];
    for (size_t i=0;i<3;++i) { unsigned char c=(unsigned char)name[length-3+i]; ending[i]=(char)(c>='A' && c<='Z'?c+'a'-'A':c); }
    ending[3]=0; return !strcmp(ending,"ogg") || !strcmp(ending,"wav");
}
static bool validate(void *user,const char *value,qa_error *error)
{
    const shared_declaration *row=user;
    if (!value) return frontend_fail(error,QA_ERROR_ARGUMENT,"Missing shared setting value");
    if (row->validation==TOGGLE)
        return !strcmp(value,"0") || !strcmp(value,"1") || frontend_fail(error,QA_ERROR_ARGUMENT,"Use 0 or 1");
    if (row->validation==MENU_TRACK)
        return frontend_shared_menu_track_valid(value) || frontend_fail(error,QA_ERROR_ARGUMENT,"Use auto, 0, track 1..255 or a mounted OGG/WAV path");
    double number;
    qa_bytes input={(const uint8_t *)value,strlen(value)};
    if (!qa_parse_number(input,&number,error) || !isfinite(number))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Expected a finite shared setting number");
    bool valid=row->validation==FINITE ||
        (row->validation==GAMMA && number>=.5 && number<=3) ||
        (row->validation==RATE && floor(number)==number && number>=8000 && number<=192000) ||
        (row->validation==BITS && (number==8 || number==16)) ||
        (row->validation==CHANNELS && (number==1 || number==2));
    return valid || frontend_fail(error,QA_ERROR_ARGUMENT,row->description);
}
static const qa_cvar_view *initialization_row(qa_cvars *registry,qa_cvars_edit *edit,const char *name)
{ return edit?qa_cvars_edit_find(edit,name):qa_cvars_find(registry,name); }
static bool initialization_set(qa_cvars *registry,qa_cvars_edit *edit,
    const char *name,const char *value,qa_error *error)
{
    return edit?qa_cvars_edit_apply(edit,&(qa_cvars_edit_command){
        .kind=QA_CVARS_EDIT_SET,.name=name,.value=value,.force=true},error):
        qa_cvars_set(registry,name,value,true,error);
}
static bool initialization_latches(qa_cvars *registry,qa_cvars_edit *edit,
    const char *const *names,size_t count,const char *message,qa_error *error)
{
    for (size_t i=0;i<count;++i) {
        const qa_cvar_view *row=edit?qa_cvars_edit_canonical_record(edit,names[i]):
            initialization_row(registry,edit,names[i]);
        if (!row || row->console_created) return frontend_fail(error,QA_ERROR_ARGUMENT,message);
        if (!(edit?qa_cvars_edit_apply(edit,&(qa_cvars_edit_command){
                .kind=QA_CVARS_EDIT_APPLY_LATCHED,.name=names[i]},error):
                qa_cvars_apply_latched(registry,names[i],error))) return false;
    }
    return true;
}
bool frontend_source_renderer_values_initialize(qa_cvars *registry,qa_cvars_edit *edit,qa_error *error)
{
    const char *const latched[]={"r_allowExtensions","r_ext_compiled_vertex_array","r_detailtextures",
        "r_vertexLight","r_fullbright","r_stereo","r_ignoreFastPath","r_ext_multitexture","r_ext_texture_env_add","r_subdivisions"};
    if (!initialization_latches(registry,edit,latched,sizeof(latched)/sizeof(*latched),
            "Source renderer lost its physical initialization declaration",error)) return false;
    const qa_cvar_view *row=initialization_row(registry,edit,"r_znear");
    if ((!edit && !qa_cvars_observer_idle(registry)) || !row || !isfinite(row->number))
        return frontend_fail(error,QA_ERROR_ARGUMENT,edit?"Source near clip requires its finite canonical scalar":
            "Source near clip requires its returned finite canonical row");
    if ((double)row->number>=INT32_MIN && (double)row->number<=INT32_MAX &&
        (int32_t)row->number!=row->integer) {
        char text[32]; snprintf(text,sizeof(text),"%d",row->integer);
        if (!initialization_set(registry,edit,"r_znear",text,error)) return false;
        row=initialization_row(registry,edit,"r_znear");
        if (!row) return frontend_fail(error,QA_ERROR_ARGUMENT,"Source initialization lost its actual near clip record");
    }
    const char *bounded=row->number<0.001f?"0.001000":row->number>200?"200.000000":NULL;
    return !bounded || initialization_set(registry,edit,"r_znear",bounded,error);
}
bool frontend_source_color_values_register(qa_cvars *registry,qa_cvars_edit *edit,qa_error *error)
{
    const char *const names[]={"r_intensity","r_ignorehwgamma","r_roundImagesDown",
        "r_simpleMipMaps","r_colorMipLevels","r_picmip","r_texturebits","r_ext_compressed_textures",
        "r_overBrightBits","r_mapOverBrightBits"};
    return initialization_latches(registry,edit,names,sizeof(names)/sizeof(*names),
        "Source color registration lost its physical declaration",error);
}
bool frontend_source_color_values_initialize(qa_cvars *registry,qa_cvars_edit *edit,qa_error *error)
{
    const char *const names[]={"r_intensity","r_gamma","r_picmip"};
    for (unsigned i=0;i<3;++i) {
        const qa_cvar_view *row=initialization_row(registry,edit,names[i]);
        if (!row) return frontend_fail(error,QA_ERROR_ARGUMENT,edit?"Source color requires its canonical initialization row":
            "Source color requires a canonical initialization row");
        float number=(float)row->number;
        if (!isfinite(number)) return frontend_fail(error,QA_ERROR_ARGUMENT,edit?
            "Source color requires its finite binary32 initialization value":
            "Source color requires a finite binary32 initialization value");
        char integer[32]; const char *value=NULL;
        if (!i) value=number<=1?"1":NULL;
        else if (i==1) value=number<.5f?"0.500000":number>3?"3.000000":NULL;
        else if (number<0) value="0.000000";
        else if (number>16) value="16.000000";
        else if ((int32_t)number!=row->integer) {
            snprintf(integer,sizeof(integer),"%d",row->integer); value=integer;
        }
        if (value && !initialization_set(registry,edit,names[i],value,error)) return false;
    }
    return true;
}
bool frontend_shared_q3_renderer_initialize(qa_frontend *f,qa_error *error)
{
    if (!f || !f->application || f->capture || f->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source renderer initialization requires its actual fresh ENGINE owner");
    frontend_shared_settings *shared=frontend_config_store_shared(f->config_store,f->application,
        qa_application_startup_candidate(f->application));
    if (shared) return frontend_shared_values_q3_renderer_initialize(frontend_shared_settings_values(shared),error);
    if (frontend_config_store_shared_pending(f->config_store))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source renderer initialization cannot replace unavailable candidate values");
    qa_cvars *registry=qa_application_cvars(f->application);
    if (!qa_cvars_observer_idle(registry))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source renderer registration requires its returned canonical owner");
    return frontend_source_renderer_values_initialize(registry,NULL,error);
}
bool frontend_source_color_register(qa_frontend *f,const qa_cvars_edit *edit,qa_error *error)
{
    if (!f || !f->application || f->capture || f->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source color registration requires its physical initialization owner");
    frontend_shared_settings *shared=frontend_config_store_shared(f->config_store,f->application,
        qa_application_startup_candidate(f->application));
    if (shared) {
        frontend_shared_values *values=frontend_shared_settings_values(shared);
        return edit && edit==frontend_shared_values_prepared(values)?
            frontend_shared_values_source_color_register(values,error):
            frontend_fail(error,QA_ERROR_ARGUMENT,"Source color registration selected another canonical edit");
    }
    qa_cvars *registry=qa_application_cvars(f->application);
    if (edit || frontend_config_store_shared_pending(f->config_store) || !qa_cvars_observer_idle(registry))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source color registration cannot replace a pending canonical owner");
    return frontend_source_color_values_register(registry,NULL,error);
}
bool frontend_source_color_clamp(qa_frontend *f,const qa_cvars_edit *edit,qa_error *error)
{
    if (!f || !f->application || f->capture || f->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source color initialization requires its fresh physical ENGINE owner");
    frontend_shared_settings *shared=frontend_config_store_shared(f->config_store,f->application,
        qa_application_startup_candidate(f->application));
    if (shared) {
        frontend_shared_values *values=frontend_shared_settings_values(shared);
        return (edit && edit==frontend_shared_values_prepared(values))?
            frontend_shared_values_source_color_initialize(values,error):
            frontend_fail(error,QA_ERROR_ARGUMENT,"Source color initialization selected another actual canonical edit");
    }
    qa_cvars *registry=qa_application_cvars(f->application);
    if (edit || frontend_config_store_shared_pending(f->config_store) || !qa_cvars_observer_idle(registry))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source color initialization cannot replace a pending canonical owner");
    return frontend_source_color_values_initialize(registry,NULL,error);
}
typedef enum q2_client_group {
    Q2_CLIENT_LAYOUT, Q2_CLIENT_EFFECTS, Q2_CLIENT_FOOTSTEPS, Q2_CLIENT_HAND
} q2_client_group;
static const struct {
    const char *name, *value;
    uint32_t flags;
    q2_client_group group;
} q2_client_declarations[] = {
    {"ch_alpha", "1", 0, Q2_CLIENT_LAYOUT}, {"ch_scale", "1", 0, Q2_CLIENT_LAYOUT},
    {"ch_x", "0", 0, Q2_CLIENT_LAYOUT}, {"ch_y", "0", 0, Q2_CLIENT_LAYOUT},
    {"cl_smooth_explosions", "1", 0, Q2_CLIENT_EFFECTS},
    {"cl_disable_particles", "0", 0, Q2_CLIENT_EFFECTS},
    {"cl_disable_explosions", "0", 0, Q2_CLIENT_EFFECTS},
    {"cl_dlight_hacks", "0", 0, Q2_CLIENT_EFFECTS},
    {"cl_rerelease_effects", "1", 0, Q2_CLIENT_EFFECTS},
    {"cl_muzzlelight_time", "100", 0, Q2_CLIENT_EFFECTS},
    {"cl_muzzleflashes", "1", 0, Q2_CLIENT_EFFECTS},
    {"cl_gun", "1", 0, Q2_CLIENT_EFFECTS}, {"cl_gunfov", "90", 0, Q2_CLIENT_EFFECTS},
    {"cl_railtrail_type", "0", 0, Q2_CLIENT_EFFECTS},
    {"cl_railtrail_time", "1.0", 0, Q2_CLIENT_EFFECTS},
    {"cl_railcore_color", "red", 0, Q2_CLIENT_EFFECTS},
    {"cl_railcore_width", "2", 0, Q2_CLIENT_EFFECTS},
    {"cl_railspiral_color", "blue", 0, Q2_CLIENT_EFFECTS},
    {"cl_railspiral_radius", "3", 0, Q2_CLIENT_EFFECTS},
    {"cl_footsteps", "1", 0, Q2_CLIENT_FOOTSTEPS},
    {"hand", "0", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO, Q2_CLIENT_HAND}
};
static bool q2_client_register(qa_cvars *registry, const qa_command_context *command,
    q2_client_group first, q2_client_group last, bool preserve_authored, qa_error *error)
{
    for (size_t i = 0; i < sizeof(q2_client_declarations) / sizeof(*q2_client_declarations); ++i) {
        if (q2_client_declarations[i].group < first || q2_client_declarations[i].group > last) continue;
        const qa_cvar_view *existing = qa_cvars_find(registry, q2_client_declarations[i].name);
        if (preserve_authored && existing && !existing->console_created) continue;
        if (!qa_cvars_register(registry, q2_client_declarations[i].name, q2_client_declarations[i].value,
            q2_client_declarations[i].flags, command->owner, "", error)) return false;
    }
    return true;
}
bool frontend_source_q2_effects_register(qa_cvars *registry, const qa_command_context *command,
    frontend_remote_q2_effects_profile profile, qa_error *error)
{
    if (!registry || !command || !command->owner || command->origin != QA_COMMAND_SEAT ||
        qa_cvars_dialect(registry) != command->dialect || !qa_cvars_observer_idle(registry) ||
        (profile != FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC && profile != FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 effects declarations require their reached Source profile and physical CLIENT heap");
    return q2_client_register(registry, command, Q2_CLIENT_EFFECTS, Q2_CLIENT_HAND, true, error);
}
bool frontend_source_q2_settings_register(const qa_launch_instance *descriptor,qa_cvars *registry,
    const qa_command_context *command,qa_error *error)
{
    qa_catalog *catalog=descriptor?qa_launch_instance_catalog(descriptor):NULL;
    const qa_product *profile=descriptor?qa_catalog_product(catalog,descriptor->selection.product):NULL;
    if (!profile || profile->family!=QA_GAME_Q2 || !profile->builtin ||
        profile->program_kind!=QA_PROGRAM_BUILTIN ||
        (descriptor->selection.runtime!=QA_PROGRAM_BUILTIN &&
         (descriptor->selection.runtime!=QA_PROGRAM_NATIVE || !descriptor->artifact)) ||
        !descriptor->storage || !descriptor->content ||
        !registry || !command || !command->owner || command->origin!=QA_COMMAND_SEAT ||
        (profile->edition!=QA_EDITION_CLASSIC && profile->edition!=QA_EDITION_RERELEASE) ||
        command->dialect!=(profile->edition==QA_EDITION_RERELEASE?QA_RULESET_Q2_RERELEASE:QA_RULESET_Q2_CLASSIC) ||
        qa_cvars_dialect(registry)!=command->dialect || !qa_cvars_observer_idle(registry))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 CLIENT declarations require their actual normalized profile and new private heap");
    return q2_client_register(registry,command,Q2_CLIENT_LAYOUT,Q2_CLIENT_EFFECTS,false,error) &&
        frontend_legacy_source_register(registry,command->dialect,command->owner,error) &&
        q2_client_register(registry,command,Q2_CLIENT_HAND,Q2_CLIENT_HAND,false,error) &&
        q2_client_register(registry,command,Q2_CLIENT_FOOTSTEPS,Q2_CLIENT_FOOTSTEPS,false,error) &&
        qa_cvars_register(registry,"crosshair",profile->edition==QA_EDITION_RERELEASE?"3":"0",
        QA_CVAR_ARCHIVE,command->owner,"",error);
}
void frontend_engine_cvars_bind(qa_frontend *frontend)
{
    const qa_cvars *registry=qa_application_cvars(frontend->application);
    uint64_t identity=qa_cvars_view_identity(registry);
    if (frontend->engine_cvars.view_identity==identity) return;
    frontend->engine_cvars=(frontend_engine_cvar_handles){.view_identity=identity,
        .timedemo=qa_cvars_resolve(registry,"timedemo"),
        .cl_avidemo=qa_cvars_resolve(registry,"cl_avidemo"),
        .cl_forceavidemo=qa_cvars_resolve(registry,"cl_forceavidemo"),
        .timescale=qa_cvars_resolve(registry,"timescale"),
        .com_maxfps=qa_cvars_resolve(registry,"com_maxfps"),
        .r_maxfps=qa_cvars_resolve(registry,"r_maxfps"),
        .s_volume=qa_cvars_resolve(registry,"s_volume"),
        .cl_rerelease_effects=qa_cvars_resolve(registry,"cl_rerelease_effects"),
        .scr_centertime=qa_cvars_resolve(registry,"scr_centertime"),
        .s_geometry_acoustics=qa_cvars_resolve(registry,"s_geometryAcoustics"),
        .gl_farclip=qa_cvars_resolve(registry,"gl_farclip"),
        .r_gamma=qa_cvars_resolve(registry,"r_gamma"),
        .con_notifytime=qa_cvars_resolve(registry,"con_notifytime"),
        .r_drawentities=qa_cvars_resolve(registry,"r_drawentities"),
        .filterban=qa_cvars_resolve(registry,"filterban"),
        .public_server=qa_cvars_resolve(registry,"public"),
        .dedicated=qa_cvars_resolve(registry,"dedicated"),
        .cg_gunX=qa_cvars_resolve(registry,"cg_gunX"),
        .cg_gunY=qa_cvars_resolve(registry,"cg_gunY"),
        .cg_gunZ=qa_cvars_resolve(registry,"cg_gunZ"),
        .cg_gun_frame=qa_cvars_resolve(registry,"cg_gun_frame")};
    qa_hud_cvars_bind(registry,QA_HUD_CVAR_DRAW_GUN,&frontend->engine_cvars.hud);
    frontend_legacy_cvars_bind(registry,&frontend->engine_cvars.legacy);
    frontend_render_cvars_bind(frontend);
    frontend_shared_resource_policy_cvars_bind(frontend);
    frontend_q3_color_cvars_bind(frontend);
    frontend_render_controls_cvars_bind(frontend);
}
bool frontend_shared_register(qa_cvars *cvars,const qa_ruleset_id *source,
    qa_audio_output_format output,float gamma,qa_error *error)
{
    if (!cvars || (source && (unsigned)*source>QA_RULESET_Q3) || !isfinite(gamma) || gamma<.5f || gamma>3 ||
        output.sample_rate<8000 || output.sample_rate>192000 ||
        (output.channels!=1 && output.channels!=2) || (output.sample_bits!=8 && output.sample_bits!=16))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Shared settings require actual factory defaults");
    if (!qa_cvars_set_video_resolver(cvars,frontend_shared_video_resolve,NULL,error)) return false;
    for (size_t i=0;i<sizeof(declarations)/sizeof(*declarations);++i) {
        const shared_declaration *row=declarations+i; const char *initial=row->initial; char text[32];
        if (!strcmp(row->name,"r_gamma")) { if (!qa_format_number(gamma,text,error)) return false; initial=text; }
        else if (!strcmp(row->name,"volume") && source && *source==QA_RULESET_Q3) initial="0.8";
        else if (!strcmp(row->name,"bgmvolume") && source && *source==QA_RULESET_Q3) initial="0.25";
        else if (!strcmp(row->name,"gl_flashblend") && source &&
            (*source==QA_RULESET_NETQUAKE || *source==QA_RULESET_QUAKEWORLD)) initial="1";
        else if (!strcmp(row->name,"s_outputRate")) { snprintf(text,sizeof(text),"%u",output.sample_rate); initial=text; }
        else if (!strcmp(row->name,"s_outputBits")) { snprintf(text,sizeof(text),"%u",output.sample_bits); initial=text; }
        else if (!strcmp(row->name,"s_outputChannels")) { snprintf(text,sizeof(text),"%u",output.channels); initial=text; }
        if (!qa_cvars_register(cvars,row->name,initial,row->flags,QA_FRONTEND_COMMAND_OWNER,row->description,error)) return false;
        if (row->validation!=ANY && !qa_cvars_bind(cvars,row->name,&(qa_cvar_binding){
            .owner=QA_FRONTEND_COMMAND_OWNER,.user=(void *)row,.validate=validate},error)) return false;
    }
    return frontend_view_settings_q1_motion_register(cvars,QA_FRONTEND_COMMAND_OWNER,false,error) &&
        qa_input_settings_register(cvars,QA_RULESET_NETQUAKE,error) &&
        qa_input_device_settings_register(cvars,error) &&
        qa_ui_preferences_register(cvars,QA_FRONTEND_COMMAND_OWNER,error);
}
