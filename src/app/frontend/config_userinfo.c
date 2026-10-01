#include "config_userinfo.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool declare(qa_cvars *cvars,const char *name,const char *value,uint32_t flags,qa_error *error)
{
    const qa_cvar_view *previous=qa_cvars_find(cvars,name);
    if (qa_cvars_dialect(cvars)<=QA_CONSOLE_QW && previous && !previous->console_created)
        return (previous->flags&flags)==flags || qa_cvars_add_flags(cvars,name,flags,error);
    return qa_cvars_register(cvars,name,value,flags,0,"Prepared client identity",error);
}
bool frontend_config_userinfo_register(qa_cvars *cvars,uint32_t seat,const char *model,qa_error *error)
{
    if (!cvars || !model || !*model) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Client configuration needs its actual identity declaration"); return false;
    }
    qa_console_dialect dialect=qa_cvars_dialect(cvars);
    const uint32_t identity=QA_CVAR_ARCHIVE|QA_CVAR_USERINFO;
    char name[64]; snprintf(name,sizeof(name),"Player %" PRIu64,(uint64_t)seat+1);
    if (dialect<=QA_CONSOLE_QW && !declare(cvars,"qts_weapon_autoswitch","always",identity,error)) return false;
    if (dialect==QA_CONSOLE_Q2_RERELEASE && !declare(cvars,"autoswitch","0",identity,error)) return false;
    if (dialect==QA_CONSOLE_Q1) {
        const qa_cvar_view *old_name=qa_cvars_find(cvars,"name"),*color=qa_cvars_find(cvars,"color");
        return declare(cvars,"_cl_name",old_name?old_name->value:name,QA_CVAR_ARCHIVE,error) &&
            declare(cvars,"_cl_color",color?color->value:"0",QA_CVAR_ARCHIVE,error);
    }
    if (dialect==QA_CONSOLE_QW) {
        static const char *names[]={"topcolor","bottomcolor","team","skin"};
        if (!declare(cvars,"name",name,identity,error)) return false;
        for (size_t i=0;i<4;++i) if (!declare(cvars,names[i],i<2?"0":"",identity,error)) return false;
        return true;
    }
    if (dialect==QA_CONSOLE_Q3) {
        static const struct {const char *name,*value; uint32_t flags;} prefix[]={
            {"vm_ui","2",QA_CVAR_ARCHIVE},{"vm_cgame","2",QA_CVAR_ARCHIVE},
            {"cl_allowDownload","0",QA_CVAR_ARCHIVE},
            {"cl_timeNudge","0",QA_CVAR_TEMPORARY},{"rate","25000",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
            {"cl_maxpackets","30",QA_CVAR_ARCHIVE},{"cl_packetdup","1",QA_CVAR_ARCHIVE},
            {"snaps","20",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO}};
        for (size_t i=0;i<sizeof(prefix)/sizeof(*prefix);++i)
            if (!declare(cvars,prefix[i].name,prefix[i].value,prefix[i].flags,error)) return false;
        if (!declare(cvars,"name",name,identity,error)) return false;
        size_t length=strlen(model);
        char *body=length<=SIZE_MAX-9?malloc(length+9):NULL;
        if (!body) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining actual prepared client model"); return false; }
        memcpy(body,model,length); memcpy(body+length,"/default",9);
        static const char *names[]={"model","headmodel","team_model","team_headmodel"};
        bool ok=true;
        for (size_t i=0;ok && i<4;++i) ok=declare(cvars,names[i],body,identity,error);
        free(body); if (!ok) return false;
        static const struct {const char *name,*value; uint32_t flags;} suffix[]={
            {"color1","4",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},{"color2","5",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
            {"sex","male",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},{"cl_anonymous","0",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
            {"cg_predictItems","1",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},{"teamtask","0",QA_CVAR_USERINFO},
            {"password","",QA_CVAR_USERINFO},{"handicap","100",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
            {"cl_maxPing","800",QA_CVAR_ARCHIVE},{"cl_serverStatusResendTime","750",0},
            {"sv_master1","master.quake3arena.com",0}};
        for (size_t i=0;i<sizeof(suffix)/sizeof(*suffix);++i)
            if (!declare(cvars,suffix[i].name,suffix[i].value,suffix[i].flags,error)) return false;
        return true;
    }
    if (!declare(cvars,"name",name,identity,error) || !declare(cvars,"spectator","0",QA_CVAR_USERINFO,error) ||
        !declare(cvars,"password","",QA_CVAR_USERINFO,error)) return false;
    const char *skin=!strcmp(model,"female")?"athena":!strcmp(model,"cyborg")?"oni911":"grunt";
    size_t a=strlen(model),b=strlen(skin);
    char *body=a<=SIZE_MAX-b-2?malloc(a+b+2):NULL;
    if (!body) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining actual prepared client skin"); return false; }
    memcpy(body,model,a); body[a]='/'; memcpy(body+a+1,skin,b+1);
    bool ok=declare(cvars,"skin",body,identity,error); free(body);
    static const char *names[]={"rate","msg","hand","fov","gender"};
    const char *values[]={"25000","1","0","90",!strcmp(model,"female")?"female":"male"};
    for (size_t i=0;ok && i<5;++i) ok=declare(cvars,names[i],values[i],identity,error);
    return ok;
}
