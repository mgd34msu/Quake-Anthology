#include "demo_dispatch.h"
#include "internal.h"
#include "network_menu.h"
#include "content_library_services.h"
#include "config_store.h"
#include "startup_menus.h"
#include "content_library_menu.h"
#include "qa/application_client.h"
#include "qa/application_q3_client.h"
#include "qa/console_cvars_prepare.h"
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
    char names[8][16];
    size_t name_count;
    int next;
    uint32_t cycle_seat, completion_seat;
    bool cycle_bound, schedule_next, completion_ready, return_menu;
    char *completion;
};
static const char *const demo_commands[]={"playdemo","demo","demomap","timedemo","stopdemo",
    "record","rerecord","stop","stoprecord","serverrecord","serverstop","mvdrecord","mvdstop","startdemos","demos"};
static bool registered_command(void *,const qa_command_invocation *,qa_error *);
static bool completing(void *,const qa_command_context *,frontend_demo_format,frontend_demo_end,bool,qa_error *);
static bool completed(void *,const qa_command_context *,frontend_demo_format,frontend_demo_end,bool,qa_error *);
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
    if(!frontend_command_seat_read(d->frontend,source,&d->cycle_seat))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Playback lost its captured physical seat");
    d->cycle_bound=true;
    if(!frontend_demo_playback_attract(d->service))frontend_demo_dispatch_manual_game(d);
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
        .record_source=record_source,.playback_source=playback_source,.print=print,
        .completing=completing,.completed=completed};
    return frontend_demo_service_create(&options,&d->service,error);
}
frontend_demo_service *frontend_demo_dispatch_service(frontend_demo_dispatch *d,uint32_t seat) {
    return d&&seat<d->frontend->options.seats?d->service:NULL;
}
bool frontend_demo_dispatch_stage(frontend_demo_dispatch *d,const frontend_demo_request *request,qa_error *error) {
    if(!d||!request||!current(d,&request->source,error))return false;
    if((request->action==FRONTEND_DEMO_PLAY&&!request->attract)||request->action==FRONTEND_DEMO_STOP_PLAY)
        frontend_demo_dispatch_manual_game(d);
    return frontend_demo_stage(d->service,request,error);
}
void frontend_demo_dispatch_manual_game(frontend_demo_dispatch *d) {
    if(!d)return;
    d->next=-1;d->schedule_next=false;d->return_menu=false;
    frontend_demo_cancel_attract(d->service);
    free(d->completion);d->completion=NULL;d->completion_ready=false;
}
static bool explicit_launch(const frontend_demo_dispatch *d) {
    const qa_frontend *f=d->frontend;
    return f->startup_launch||f->options.game||f->options.map||f->options.network_connect;
}
static bool idle_world(const frontend_demo_dispatch *d) {
    return !qa_application_launch(d->frontend->application)&&
        !frontend_demo_playback_path(d->service)&&!frontend_network_client_only(d->frontend);
}
static bool attract_command(frontend_demo_dispatch *d,const qa_command_invocation *call,bool start,qa_error *error) {
    if(start) {
        if(d->frontend->options.dedicated) {
            if(idle_world(d)&&!explicit_launch(d))return qa_console_append(call->console,&call->context,"map start\n",error);
            return true;
        }
        size_t count=call->argc-1;
        if(count>8){frontend_console_print(d->frontend,&call->context,"Max 8 demos in demoloop\n");count=8;}
        memset(d->names,0,sizeof(d->names));d->name_count=count;
        for(size_t i=0;i<count;++i) {
            size_t length=strlen(call->argv[i+1]);if(length>15)length=15;
            memcpy(d->names[i],call->argv[i+1],length);
        }
        char line[48];snprintf(line,sizeof(line),"%zu demo(s) in loop\n",count);
        frontend_console_print(d->frontend,&call->context,line);
        if(!idle_world(d)||explicit_launch(d)||d->next==-1) {
            d->next=-1;d->schedule_next=false;frontend_demo_cancel_attract(d->service);return true;
        }
        d->next=0;
    } else {
        if(d->frontend->options.dedicated)return true;
        if(d->next<0)d->next=1;
    }
    if(!frontend_command_seat_read(d->frontend,&call->context,&d->cycle_seat))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Attract list lost its actual Source seat");
    d->cycle_bound=true;d->schedule_next=true;return true;
}
static bool seat_source(frontend_demo_dispatch *d,uint32_t ordinal,qa_command_context *source,
    qa_console **console,qa_error *error) {
    qa_frontend *f=d->frontend;
    if(ordinal>=f->options.seats||!f->seats[ordinal].input)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Demo continuation lost its retained physical seat");
    qa_command_context input=qa_input_seat_context(f->seats[ordinal].input);
    if(!qa_application_capture_command_context(f->application,&input,source,error))return false;
    *console=qa_seat_console_recipient_read(f->seats[ordinal].console);
    qa_application_client_source client;
    if(source->owner&&source->owner<=UINT32_MAX&&
        qa_application_client_physical_read(f->application,(qa_actor_owner)source->owner,source->seat,&client,NULL)&&
        client.context.physical_seat==ordinal&&qa_application_client_current(f->application,&client))
        *console=client.context.console;
    qa_application_q3_client_context q3;
    if(source->owner&&source->owner<=UINT32_MAX&&
        qa_application_q3_remote_context_read(f->application,(qa_actor_owner)source->owner,source->seat,&q3,NULL)&&
        qa_application_q3_remote_context_current(f->application,&q3))
        *console=q3.console;
    if(!*console)return frontend_fail(error,QA_ERROR_ARGUMENT,"Demo continuation lost its actual console recipient");
    return true;
}
static bool next_demo(frontend_demo_dispatch *d,qa_error *error) {
    if(d->next<0||!d->cycle_bound){d->schedule_next=false;return true;}
    if((size_t)d->next>=d->name_count||!d->names[d->next][0])d->next=0;
    if(!d->name_count||!d->names[d->next][0]) {
        d->next=-1;d->schedule_next=false;
        frontend_console_print(d->frontend,NULL,"No demos listed with startdemos\n");return true;
    }
    qa_command_context source;qa_console *console;
    if(!seat_source(d,d->cycle_seat,&source,&console,error))return false;
    (void)console;
    frontend_demo_request request={.action=FRONTEND_DEMO_PLAY,.source=source,
        .name=d->names[d->next],.fallback=FRONTEND_DEMO_NQ,.attract=true};
    if(!frontend_demo_stage(d->service,&request,error))return false;
    ++d->next;d->schedule_next=false;return true;
}
static bool completing(void *context,const qa_command_context *request,frontend_demo_format format,
    frontend_demo_end end,bool attract,qa_error *error) {
    frontend_demo_dispatch *d=context;(void)attract;
    free(d->completion);d->completion=NULL;d->completion_ready=false;
    if((format!=FRONTEND_DEMO_Q2&&format!=FRONTEND_DEMO_Q3)||
        end==FRONTEND_DEMO_CLOSED||end==FRONTEND_DEMO_TRUNCATED||end==FRONTEND_DEMO_DISCONNECTED)return true;
    if(!frontend_command_seat_read(d->frontend,request,&d->completion_seat)) {
        /* Playback can replace its request's GAME; the genuine input publication
         * retains the same physical seat established when construction began. */
        if(!d->cycle_bound)return frontend_fail(error,QA_ERROR_ARGUMENT,"Demo completion lost its original physical seat");
        d->completion_seat=d->cycle_seat;
    }
    qa_command_context source;qa_console *console;
    if(!seat_source(d,d->completion_seat,&source,&console,error))return false;
    const char *name=format==FRONTEND_DEMO_Q2?"nextserver":"nextdemo";
    const qa_cvar_view *view;
    if(!qa_console_cvar_read(console,&source,name,&view,error))return false;
    if(!view||!*view->value)return true;
    size_t length=strlen(view->value);d->completion=malloc(length+2);
    if(!d->completion)return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual demo completion command");
    memcpy(d->completion,view->value,length);d->completion[length]='\n';d->completion[length+1]=0;
    qa_cvars *registry;qa_cvars_edit *edit;
    if(!qa_console_cvar_access(console,&source,name,&registry,&edit,error))return false;
    qa_cvars_edit_command clear={.kind=QA_CVARS_EDIT_SET,.name=name,.value="",.force=true};
    return edit?qa_cvars_edit_apply(edit,&clear,error):qa_cvars_apply(registry,&clear,error);
}
static bool completed(void *context,const qa_command_context *source,frontend_demo_format format,
    frontend_demo_end end,bool attract,qa_error *error) {
    frontend_demo_dispatch *d=context;(void)source;(void)error;
    if(end==FRONTEND_DEMO_CLOSED||end==FRONTEND_DEMO_TRUNCATED) {
        frontend_demo_dispatch_manual_game(d);
        if(end==FRONTEND_DEMO_CLOSED)d->return_menu=true;
        return true;
    }
    if(format==FRONTEND_DEMO_NQ||format==FRONTEND_DEMO_QW) {
        if(attract&&d->next>=0)d->schedule_next=true;
    } else d->completion_ready=d->completion!=NULL;
    return true;
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
    if(kind>=13)return attract_command(d,call,kind==13,error);
    frontend_demo_action action=kind<=3?FRONTEND_DEMO_PLAY:kind==4?FRONTEND_DEMO_STOP_PLAY:
        kind==5?FRONTEND_DEMO_RECORD:kind==6?FRONTEND_DEMO_RERECORD:
        kind<=8?FRONTEND_DEMO_STOP_RECORD:kind==9?FRONTEND_DEMO_SERVER_RECORD:
        kind==10?FRONTEND_DEMO_SERVER_STOP:kind==11?FRONTEND_DEMO_MVD_RECORD:FRONTEND_DEMO_MVD_STOP;
    bool takes_name=action==FRONTEND_DEMO_PLAY||action==FRONTEND_DEMO_RECORD||
        action==FRONTEND_DEMO_RERECORD||action==FRONTEND_DEMO_SERVER_RECORD||action==FRONTEND_DEMO_MVD_RECORD;
    if((takes_name&&(call->argc>2||(call->argc<2&&action!=FRONTEND_DEMO_RECORD)))||(!takes_name&&kind!=4&&call->argc!=1)) {
        char text[96];snprintf(text,sizeof(text),"Usage: %s%s\n",demo_commands[kind],takes_name?" <name>":"");
        frontend_console_print(d->frontend,&call->context,text);return true;
    }
    if(action==FRONTEND_DEMO_PLAY&&d->frontend->options.dedicated&&kind!=2)return true;
    if(action==FRONTEND_DEMO_STOP_PLAY&&(d->frontend->options.dedicated||
        !frontend_demo_playback_path(d->service)))return true;
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
    if(!qa_application_startup_pending(d->frontend->application)) {
        if(d->completion_ready) {
            qa_command_context source;qa_console *console;
            if(!seat_source(d,d->completion_seat,&source,&console,error)||
                !qa_console_append(console,&source,d->completion,error))return false;
            free(d->completion);d->completion=NULL;d->completion_ready=false;
        }
        if(d->schedule_next&&!next_demo(d,error))return false;
    }
    qa_error failure={0};
    if(frontend_demo_execute(d->service,&failure)) {
        if(d->return_menu) {
            if(!d->frontend->startup_launch) {
                if(!d->cycle_bound||d->cycle_seat>=d->frontend->options.seats)
                    return frontend_fail(error,QA_ERROR_ARGUMENT,"Stopped demo lost its retained physical menu seat");
                if(!frontend_startup_end_stage(d->frontend->seats+d->cycle_seat,error))return false;
            }
            d->return_menu=false;
        }
        return true;
    }
    frontend_demo_dispatch_manual_game(d);
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
        frontend_demo_service_active(d->service)||d->schedule_next||d->completion_ready||d->return_menu||
        (d->cycle_bound&&d->next>=0))
        return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Active native demo file cursors have no cold adoption recipe");
    return true;
}
bool frontend_demo_dispatch_stop(frontend_demo_dispatch *d,qa_error *error) {
    if(!d)return true;
    frontend_demo_dispatch_manual_game(d);
    return frontend_demo_service_stop(d->service,error);
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
    free(d->completion);free(d);*owner=NULL;return true;
}
