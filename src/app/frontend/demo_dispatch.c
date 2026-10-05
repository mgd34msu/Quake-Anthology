#include "demo_dispatch.h"
#include "internal.h"
#include "network_menu.h"
#include "content_library_services.h"
#include "config_store.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct demo_registration {
    struct demo_registration *next;
    qa_console *console;
    uint64_t dispatch, lifetime;
    size_t count;
} demo_registration;
struct frontend_demo_dispatch {
    qa_frontend *frontend;
    frontend_demo_service *service;
    demo_registration *registrations;
};
static const char *const demo_commands[]={"playdemo","demo","demomap","timedemo","stopdemo",
    "record","rerecord","stop","stoprecord","serverrecord","serverstop","mvdrecord","mvdstop"};
static bool registered_command(void *,const qa_command_invocation *,qa_error *);
static bool current(void *context,const qa_command_context *source,qa_error *error) {
    frontend_demo_dispatch *d=context;uint32_t seat;
    if(!d||!source||!frontend_command_seat_read(d->frontend,source,&seat)||
        !qa_application_command_context_active(d->frontend->application,source))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Demo request lost its actual local Source seat");
    return true;
}
static bool read_recording(void *context,const qa_command_context *source,const char *path,
    qa_buffer *out,bool *found,qa_error *error) {
    frontend_demo_dispatch *d=context;
    return frontend_content_library_demo_read(d->frontend,source,path,out,found,error);
}
static bool record_source(void *context,const qa_command_context *source,frontend_demo_action action,
    frontend_demo_record_source *out,qa_error *error) {
    frontend_demo_dispatch *d=context;
    return frontend_network_demo_record(d->frontend,source,action,out,error);
}
static bool playback_source(void *context,const qa_command_context *source,frontend_demo_format format,
    uint32_t protocol,frontend_demo_reader *reader,frontend_demo_playback_source *out,qa_error *error) {
    frontend_demo_dispatch *d=context;
    return frontend_network_demo_playback(d->frontend,source,format,protocol,reader,out,error);
}
static void print(void *context,const char *text) {
    frontend_demo_dispatch *d=context;frontend_console_print(d->frontend,NULL,text);
}
bool frontend_demo_dispatch_create(qa_frontend *f,frontend_demo_dispatch **out,qa_error *error) {
    if(!f||!out||*out)return frontend_fail(error,QA_ERROR_ARGUMENT,"Demo dispatcher requires its retained frontend");
    frontend_demo_dispatch *d=calloc(1,sizeof(*d));
    if(!d)return frontend_fail(error,QA_ERROR_MEMORY,"Retaining frontend demo dispatcher");
    *out=d;d->frontend=f;
    frontend_demo_service_options options={.context=d,.current=current,.read=read_recording,
        .record_source=record_source,.playback_source=playback_source,.print=print};
    return frontend_demo_service_create(&options,&d->service,error);
}
frontend_demo_service *frontend_demo_dispatch_service(frontend_demo_dispatch *d,uint32_t seat) {
    return d&&seat<d->frontend->options.seats?d->service:NULL;
}
bool frontend_demo_dispatch_stage(frontend_demo_dispatch *d,const frontend_demo_request *request,qa_error *error) {
    return d&&request&&current(d,&request->source,error)&&frontend_demo_stage(d->service,request,error);
}
static bool named(const char *name,const char *expected) {
    for(;*name&&*expected;++name,++expected) {
        unsigned char c=(unsigned char)*name;
        if(c>='A'&&c<='Z')c=(unsigned char)(c+'a'-'A');
        if(c!=(unsigned char)*expected)return false;
    }
    return !*name&&!*expected;
}
bool frontend_demo_dispatch_command(frontend_demo_dispatch *d,const qa_command_invocation *call,
    bool *handled,qa_error *error) {
    if(!d||!call||!handled||!call->argc||!call->argv||!call->console||
        !qa_console_invocation_current(call->console,call))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Demo command requires its entered actual console invocation");
    *handled=false;size_t kind=0;
    while(kind<sizeof(demo_commands)/sizeof(*demo_commands)&&!named(call->argv[0],demo_commands[kind]))++kind;
    if(kind==sizeof(demo_commands)/sizeof(*demo_commands))return true;
    const qa_console_entry *entry=qa_console_find(call->console,&call->context,call->argv[0]);
    qa_console *registered=call->console;
    if(!entry) {
        registered=qa_application_console(d->frontend->application);
        entry=qa_console_find(registered,&call->context,call->argv[0]);
    }
    uint64_t lifetime;qa_command_handler handler=NULL;void *user=NULL;
    if(!entry||!qa_console_registration_read(registered,entry->name,entry->owner,&lifetime,&handler,&user)||
        handler!=registered_command||user!=d)return true;
    frontend_demo_format format=frontend_network_demo_format(d->frontend,&call->context);
    *handled=true;
    if(kind==3&&format!=FRONTEND_DEMO_NQ&&format!=FRONTEND_DEMO_QW) {
        frontend_console_print(d->frontend,&call->context,"Timedemo is unavailable for this Source profile.\n");return true;
    }
    if(call->context.origin==QA_COMMAND_REMOTE) {
        frontend_console_print(d->frontend,&call->context,"Demo recording and playback are local client commands.\n");return true;
    }
    frontend_demo_action action=kind<=3?FRONTEND_DEMO_PLAY:kind==4?FRONTEND_DEMO_STOP_PLAY:
        kind==5?FRONTEND_DEMO_RECORD:kind==6?FRONTEND_DEMO_RERECORD:
        kind<=8?FRONTEND_DEMO_STOP_RECORD:kind==9?FRONTEND_DEMO_SERVER_RECORD:
        kind==10?FRONTEND_DEMO_SERVER_STOP:kind==11?FRONTEND_DEMO_MVD_RECORD:FRONTEND_DEMO_MVD_STOP;
    bool takes_name=action==FRONTEND_DEMO_PLAY||action==FRONTEND_DEMO_RECORD||
        action==FRONTEND_DEMO_RERECORD||action==FRONTEND_DEMO_SERVER_RECORD||action==FRONTEND_DEMO_MVD_RECORD;
    if((takes_name&&(call->argc>2||(call->argc<2&&action!=FRONTEND_DEMO_RECORD)))||(!takes_name&&call->argc!=1)) {
        char text[96];snprintf(text,sizeof(text),"Usage: %s%s\n",demo_commands[kind],takes_name?" <name>":"");
        frontend_console_print(d->frontend,&call->context,text);return true;
    }
    if(action==FRONTEND_DEMO_PLAY&&d->frontend->options.dedicated&&kind!=2)return true;
    if(action==FRONTEND_DEMO_STOP_PLAY&&d->frontend->options.dedicated)return true;
    if(kind==1)format=FRONTEND_DEMO_Q3;
    if(kind==2)format=FRONTEND_DEMO_Q2;
    frontend_demo_request request={.action=action,.source=call->context,
        .name=call->argc>1?call->argv[1]:NULL,.fallback=format,.timedemo=kind==3};
    return frontend_demo_dispatch_stage(d,&request,error);
}
static bool registered_command(void *context,const qa_command_invocation *call,qa_error *error) {
    bool handled=false;
    return frontend_demo_dispatch_command(context,call,&handled,error)&&handled;
}
bool frontend_demo_dispatch_register(frontend_demo_dispatch *d,qa_console *console,
    uint64_t dispatch,uint64_t lifetime,qa_error *error) {
    if(!d||!console||!qa_console_cvar_returned(console))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Demo registration requires its returned physical console");
    for(demo_registration *r=d->registrations;r;r=r->next)
        if(r->console==console&&r->dispatch==dispatch&&r->lifetime==lifetime)return true;
    demo_registration *r=calloc(1,sizeof(*r));
    if(!r)return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual demo command registrations");
    r->console=console;r->dispatch=dispatch;r->lifetime=lifetime;r->next=d->registrations;d->registrations=r;
    for(;r->count<sizeof(demo_commands)/sizeof(*demo_commands);++r->count) {
        const char *name=demo_commands[r->count];
        uint64_t existing;qa_command_handler handler=NULL;void *user=NULL;
        if(qa_console_registration_read(console,name,dispatch,&existing,&handler,&user)) {
            if(handler==registered_command&&user==d)continue;
            char text[128];snprintf(text,sizeof(text),"Demo command %s is already owned by another command.\n",name);
            frontend_console_print(d->frontend,NULL,text);continue;
        }
        if(!qa_console_register_owned(console,name,"Native recording and playback",dispatch,lifetime,
            true,registered_command,d,error))return false;
    }
    return true;
}
bool frontend_demo_dispatch_unregister(frontend_demo_dispatch *d,qa_console *console,
    uint64_t dispatch,uint64_t lifetime,qa_error *error) {
    if(!d)return true;
    if(!console||!qa_console_cvar_returned(console))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Demo retirement requires its returned physical console");
    demo_registration **at=&d->registrations;
    while(*at&&((*at)->console!=console||(*at)->dispatch!=dispatch||(*at)->lifetime!=lifetime))at=&(*at)->next;
    if(!*at)return true;
    demo_registration *r=*at;
    bool shared=false;
    for(demo_registration *other=d->registrations;other;other=other->next)
        if(other!=r&&other->console==console&&other->dispatch==dispatch)shared=true;
    if(!shared)for(size_t i=0;i<r->count;++i) {
        uint64_t actual;qa_command_handler handler=NULL;void *user=NULL;
        if(qa_console_registration_read(console,demo_commands[i],dispatch,&actual,&handler,&user)&&
            handler==registered_command&&user==d)qa_console_unregister(console,demo_commands[i],dispatch);
    }
    *at=r->next;free(r);return true;
}
bool frontend_demo_dispatch_execute(frontend_demo_dispatch *d,qa_error *error) {
    if(!d)return true;
    qa_error failure={0};
    if(frontend_demo_execute(d->service,&failure))return true;
    if(frontend_demo_service_idle(d->service)&&!frontend_demo_service_active(d->service)&&
        (failure.code==QA_ERROR_ARGUMENT||failure.code==QA_ERROR_NOT_FOUND||
         failure.code==QA_ERROR_UNSUPPORTED||failure.code==QA_ERROR_IO)) {
        char text[sizeof(failure.message)+32];
        snprintf(text,sizeof(text),"Demo: %s\n",failure.message);
        frontend_console_print(d->frontend,NULL,text);return true;
    }
    if(error)*error=failure;
    return false;
}
bool frontend_demo_dispatch_advance(frontend_demo_dispatch *d,uint64_t elapsed,uint64_t frame,qa_error *error) {
    return !d||frontend_demo_advance(d->service,elapsed,frame,error);
}
bool frontend_demo_dispatch_sources_returned(frontend_demo_dispatch *d,qa_error *error) {
    return !d||frontend_demo_sources_returned(d->service,error);
}
bool frontend_demo_dispatch_capture_ready(const frontend_demo_dispatch *d,qa_error *error) {
    if(!d)return true;
    if(!frontend_demo_service_idle(d->service)||frontend_demo_service_pending(d->service)||
        frontend_demo_service_active(d->service))
        return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Active native demo file cursors have no cold adoption recipe");
    return true;
}
bool frontend_demo_dispatch_stop(frontend_demo_dispatch *d,qa_error *error) {
    return !d||frontend_demo_service_stop(d->service,error);
}
bool frontend_demo_dispatch_destroy(frontend_demo_dispatch **owner,qa_error *error) {
    frontend_demo_dispatch *d=owner?*owner:NULL;if(!d)return true;
    for(demo_registration *r=d->registrations;r;r=r->next)
        if(!qa_console_cvar_returned(r->console))return frontend_fail(error,QA_ERROR_ARGUMENT,"Demo command handler is entered during retirement");
    if(!frontend_demo_service_destroy(&d->service,error))return false;
    while(d->registrations) {
        demo_registration *r=d->registrations;
        if(!frontend_demo_dispatch_unregister(d,r->console,r->dispatch,r->lifetime,error))return false;
    }
    free(d);*owner=NULL;return true;
}
