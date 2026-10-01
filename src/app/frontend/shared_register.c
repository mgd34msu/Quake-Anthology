#include "shared_register.h"
#include "qa/cvars_alias.h"
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
    {"fov","90","Field of view",QA_CVAR_ARCHIVE,ANY},
    {"con_scale","0","Console text size",QA_CVAR_ARCHIVE,ANY},
    {"r_gamma","1","Display brightness, 0.5 through 3",QA_CVAR_ARCHIVE,GAMMA},
    {"r_shadows","0","Shared model shadows; zero disables",QA_CVAR_ARCHIVE,FINITE},
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
    {"r_swapInterval","1","GL vertical synchronization",QA_CVAR_ARCHIVE,ANY},
    {"r_smp","0","Render worker selection applied by video restart",QA_CVAR_ARCHIVE,TOGGLE},
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
static bool menu_track(const char *value)
{
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
        return menu_track(value) || frontend_fail(error,QA_ERROR_ARGUMENT,"Use auto, 0, track 1..255 or a mounted OGG/WAV path");
    double number;
    qa_bytes input={(const uint8_t *)value,strlen(value)};
    size_t cursor=0; uint32_t scalar; bool present=false;
    while (qa_utf8_next(input,&cursor,&scalar))
        if (!qa_unicode_whitespace(scalar)) { present=true; break; }
    if (!present || !qa_parse_ecmascript_number(input,&number,error) || !isfinite(number))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Expected a finite shared setting number");
    bool valid=row->validation==FINITE ||
        (row->validation==GAMMA && number>=.5 && number<=3) ||
        (row->validation==RATE && floor(number)==number && number>=8000 && number<=192000) ||
        (row->validation==BITS && (number==8 || number==16)) ||
        (row->validation==CHANNELS && (number==1 || number==2));
    return valid || frontend_fail(error,QA_ERROR_ARGUMENT,row->description);
}
bool frontend_shared_register(qa_cvars *cvars,qa_console_dialect source,
    qa_audio_output_format output,float gamma,qa_error *error)
{
    if (!cvars || (unsigned)source>QA_CONSOLE_Q3 || !isfinite(gamma) || gamma<.5f || gamma>3 ||
        output.sample_rate<8000 || output.sample_rate>192000 ||
        (output.channels!=1 && output.channels!=2) || (output.sample_bits!=8 && output.sample_bits!=16))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Shared settings require actual factory defaults");
    for (size_t i=0;i<sizeof(declarations)/sizeof(*declarations);++i) {
        const shared_declaration *row=declarations+i; const char *initial=row->initial; char text[32];
        if (!strcmp(row->name,"r_gamma")) { if (!qa_format_ecmascript_number(gamma,text,error)) return false; initial=text; }
        else if (!strcmp(row->name,"volume") && source==QA_CONSOLE_Q3) initial="0.8";
        else if (!strcmp(row->name,"bgmvolume") && source==QA_CONSOLE_Q3) initial="0.25";
        else if (!strcmp(row->name,"s_outputRate")) { snprintf(text,sizeof(text),"%u",output.sample_rate); initial=text; }
        else if (!strcmp(row->name,"s_outputBits")) { snprintf(text,sizeof(text),"%u",output.sample_bits); initial=text; }
        else if (!strcmp(row->name,"s_outputChannels")) { snprintf(text,sizeof(text),"%u",output.channels); initial=text; }
        if (!qa_cvars_register(cvars,row->name,initial,row->flags,QA_FRONTEND_COMMAND_OWNER,row->description,error)) return false;
        if (row->validation!=ANY && !qa_cvars_bind(cvars,row->name,&(qa_cvar_binding){
            .owner=QA_FRONTEND_COMMAND_OWNER,.user=(void *)row,.validate=validate},error)) return false;
    }
    static const struct { const char *name,*target; qa_cvar_alias_conversion conversion; const char *summary,*usage; } aliases[]={
        {"gamma","r_gamma",QA_CVAR_ALIAS_RECIPROCAL_GAMMA,"Quake brightness convention: gamma = 1 / r_gamma","gamma <1/3..2>"},
        {"vid_gamma","r_gamma",QA_CVAR_ALIAS_RECIPROCAL_GAMMA,"Quake II brightness convention: gamma = 1 / r_gamma","vid_gamma <1/3..2>"},
        {"s_volume","volume",QA_CVAR_ALIAS_IDENTITY,"Alias of the shared effects gain","s_volume <0..1>"},
        {"s_musicvolume","bgmvolume",QA_CVAR_ALIAS_IDENTITY,"Alias of the shared music gain","s_musicvolume <0..1>"},
        {"ogg_volume","bgmvolume",QA_CVAR_ALIAS_IDENTITY,"Quake II shared music gain","ogg_volume <0..1>"},
        {"s_khz","s_outputRate",QA_CVAR_ALIAS_KILOHERTZ,"Source sample-rate convention; apply with snd_restart","s_khz <11|22|44|48>"},
        {"ogg_shuffle","music_shuffle",QA_CVAR_ALIAS_IDENTITY,"Shuffle mounted Quake II gameplay music","ogg_shuffle <0|1>"},
        {"ogg_menu_track","music_menu_track",QA_CVAR_ALIAS_IDENTITY,"Mounted Quake II menu music selection","ogg_menu_track <auto|0|1..255|path>"}
    };
    for (size_t i=0;i<sizeof(aliases)/sizeof(*aliases);++i)
        if (!qa_cvars_alias_register(cvars,aliases[i].name,aliases[i].target,aliases[i].conversion,
            aliases[i].summary,&(qa_console_documentation){.summary=aliases[i].summary,.usage=aliases[i].usage},error)) return false;
    return qa_input_device_settings_register(cvars,error) &&
        qa_ui_preferences_register(cvars,QA_FRONTEND_COMMAND_OWNER,error);
}
