#include "internal.h"
#include "demo_dispatch.h"
#include "commands.h"
#include "save_commands.h"
#include "campaign_cinematic.h"
#include "config_store.h"
#include "view_settings.h"
#include "system_cinematic.h"
#include "music_sources.h"
#include "q1_sky.h"
#include "source_renderer_runtime.h"
#include "q1_help.h"
#include <stdio.h>

static const char *const client_menus[]={"toggleconsole","menu","messagemode","messagemode2",
    "menu_anthology","library","mods","settings","rankings","assistance","controls","quit","togglemenu","help"};
static const qa_ui_id menu_destinations[] = {FRONTEND_HOME, FRONTEND_LIBRARY, FRONTEND_MODS,
    FRONTEND_OPTIONS, FRONTEND_RANKINGS, FRONTEND_ASSISTANCE, FRONTEND_CONTROLS};
static bool menu_command(frontend_seat *seat, const qa_command_context *context,
    qa_ui_id destination, bool game, qa_error *error)
{
    qa_command_context target;
    if (!qa_application_capture_command_context(seat->frontend->application,context,&target,error)) return false;
    target.script=NULL;
    seat->command_menu=destination;
    seat->command_game_menu=game;
    seat->command_menu_context=target;
    return true;
}
bool frontend_commands_menus_pump(qa_frontend *frontend, qa_error *error)
{
    for (unsigned i=0;i<frontend->options.seats;++i) {
        frontend_seat *seat=frontend->seats+i;
        qa_ui_id destination=seat->command_menu;
        if (!destination) continue;
        bool game=seat->command_game_menu;
        qa_command_context context=seat->command_menu_context;
        seat->command_menu=0;
        seat->command_game_menu=false;
        seat->command_menu_context=(qa_command_context){0};
        uint32_t physical;
        if (!frontend_command_seat_read(frontend,&context,&physical) || physical!=i) continue;
        bool opened;
        if (game) opened=frontend_game_menu(seat,error);
        else if (destination==FRONTEND_Q1_HELP) opened=frontend_q1_help_open(seat,error);
        else opened=frontend_menu_open(seat,destination,error);
        if (!opened) return false;
    }
    return true;
}
struct frontend_client_commands {
    qa_frontend *frontend;
    qa_console *console;
    qa_cvars *cvars;
    const void *lifetime;
    qa_actor_owner receiver;
    uint64_t command_owner;
    uint32_t seat,physical;
    size_t registered;
};
static bool client_name(const char *text,const char *name)
{
    for (;*text && *name;++text,++name) {
        unsigned char c=(unsigned char)*text;
        if (c>='A' && c<='Z') c=(unsigned char)(c+'a'-'A');
        if (c!=(unsigned char)*name) return false;
    }
    return !*text && !*name;
}
bool frontend_commands_source(qa_frontend *f,const qa_application_startup_source *source,
    const qa_command_invocation *call,bool *handled,qa_error *error)
{
    if (!f || !source || !call || !handled || !call->argc || call->console!=source->console ||
        !qa_console_invocation_current(call->console,call))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Common command requires its entered Source invocation");
    *handled=false;
    const char *name=call->argv[0];
    if (f->demos && !frontend_demo_dispatch_command(f->demos,call,handled,error)) return false;
    if (*handled) return true;
    if (qa_console_find(source->console,&call->context,name) || qa_cvars_find(source->cvars,name)) return true;
    for (size_t i=0;;++i) {
        const qa_console_entry *alias=qa_console_alias_at(source->console,call->context.owner,i);
        if (!alias) break;
        if (client_name(name,alias->name)) return true;
    }
    if ((call->context.dialect==QA_CONSOLE_Q1 || call->context.dialect==QA_CONSOLE_QW) && client_name(name,"bf")) {
        *handled=true;
        uint32_t physical; qa_actor_id actor;
        if (!frontend_command_seat_read(f,&call->context,&physical) ||
            !frontend_seat_actor_read(f,physical,&actor)) return true;
        return frontend_view_q1_local_bonus(f,actor,error);
    }
    bool parked=frontend_config_store_parked_current(f->config_store,source);
    bool q2=parked || source->scope.kind==QA_APPLICATION_CONSOLE_Q2_GAME ||
        source->scope.kind==QA_APPLICATION_CONSOLE_NATIVE_Q2;
    bool map=client_name(name,"map"),gamemap=client_name(name,"gamemap");
    if (q2 && client_name(name,"killserver")) {
        if (!source->scope.provider || source->scope.provider!=call->context.owner ||
            source->command.owner!=source->scope.provider)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 server shutdown lost its actual Source provider");
        *handled=true;
        const qa_launch_snapshot *published=qa_application_launch(f->application);
        const qa_launch_instance *live=published && source->descriptor?
            qa_launch_snapshot_find(published,source->descriptor->selection.instance):NULL;
        if (!live || live->storage!=source->descriptor->storage || live->state!=source->descriptor->state) {
            if (call->context.dialect==QA_CONSOLE_Q2_RERELEASE)
                frontend_console_print(f,&call->context,"No server running.\n");
            return true;
        }
        uint64_t generation=qa_application_configuration_generation(f->application);
        if (f->server_stop_owner && (f->server_stop_owner!=source->scope.provider ||
            f->server_stop_generation!=generation || f->server_stopped))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 shutdown already retains another actual server");
        f->server_stop_owner=source->scope.provider;
        f->server_stop_generation=generation;
        return true;
    }
    if (q2 && (map || gamemap)) {
        if (!source->scope.provider || source->scope.provider!=call->context.owner ||
            source->command.owner!=source->scope.provider)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 map command lost its actual Source provider");
        *handled=true;
        const char *destination=call->argc>1?call->argv[1]:"";
        if (map && !strchr(destination,'.')) {
            char expanded[64];
            snprintf(expanded,sizeof(expanded),"maps/%s.bsp",destination);
            qa_vfs *files=parked?source->descriptor->content:
                qa_application_context_files(f->application,&call->context,NULL);
            bool found=false; uint64_t size=0;
            if (!files) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 map command lost its Source filesystem");
            if (!qa_vfs_probe(files,expanded,&found,&size,error)) return false;
            if (!found) {
                char message[96];
                snprintf(message,sizeof(message),"Can't find %s\n",expanded);
                frontend_console_print(f,&call->context,message);
                return true;
            }
        }
        if (call->argc!=2) {
            frontend_console_print(f,&call->context,"USAGE: gamemap <map>\n");
            return true;
        }
        bool stopping=f->server_stop_owner==source->scope.provider && !f->server_stopped;
        bool queued=qa_application_queue_travel(f->application,&(qa_application_travel_request){
            .provider=source->scope.provider,.cause=call->context.actor,.expression=destination,
            .new_unit=map,.carry_players=!map && !stopping && !parked},error);
        /* Q2 resumes the remaining command buffer only after ClientBegin in
         * the new world. Travel is asynchronous here, so retain that tail as
         * soon as the actual request has been accepted. */
        qa_application_travel_view travel;
        if (queued && qa_application_travel_read(f->application,&travel) &&
            travel.target.kind==QA_TRAVEL_MAP && !qa_console_defer(source->console,error)) return false;
        if (queued) frontend_demo_dispatch_manual_game(f->demos);
        if (queued && stopping) f->server_stop_follow_map=true;
        return queued;
    }
    qa_console *engine=qa_application_console(f->application);
    qa_command_context context={.origin=QA_COMMAND_LOCAL,.dialect=call->context.dialect,.direct=true};
    const qa_console_entry *entry=qa_console_find(engine,&context,name);
    uint64_t lifetime=0; qa_command_handler handler=NULL; void *user=NULL;
    if (!entry || !entry->engine_command ||
        !qa_console_registration_read(engine,entry->name,entry->owner,&lifetime,&handler,&user) || !handler) return true;
    *handled=true;
    return handler(user,call,error);
}
static bool client_menu_command(void *context,const qa_command_invocation *command,qa_error *error)
{
    frontend_client_commands *owner=context;
    qa_frontend *f=owner?owner->frontend:NULL;
    qa_application_client_source source;
    if (!f || !command || !command->argc || command->console!=owner->console ||
        !qa_application_client_physical_read(f->application,owner->receiver,owner->seat,&source,error) ||
        source.context.console!=owner->console || source.context.cvars!=owner->cvars ||
        source.context.lifetime!=owner->lifetime || source.context.physical_seat!=owner->physical ||
        !qa_application_command_context_active(f->application,&command->context) ||
        command->context.owner!=source.context.command.owner || command->context.session!=source.context.command.session ||
        command->context.client!=source.context.command.client || command->context.seat!=source.context.command.seat ||
        command->context.registry!=source.context.command.registry || command->context.generation!=source.context.command.generation ||
        command->context.dialect!=source.context.command.dialect ||
        !qa_actor_id_equal(command->context.actor,source.context.command.actor) || !f->seats ||
        owner->physical>=f->options.seats || f->seats[owner->physical].frontend!=f)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT menu command lost its actual physical namespace");
    frontend_seat *seat=f->seats+owner->physical;
    if (client_name(command->argv[0],"help"))
        return menu_command(seat,&command->context,FRONTEND_Q1_HELP,false,error);
    if (command->context.origin==QA_COMMAND_REMOTE) {
        frontend_console_print(f,&command->context,"Menu commands require the local client.\n"); return true;
    }
    if (client_name(command->argv[0],"quit")) { qa_application_request_stop(f->application); return true; }
    size_t kind=0;
    while (kind<sizeof(client_menus)/sizeof(*client_menus) && !client_name(command->argv[0],client_menus[kind])) ++kind;
    if (kind==0) return qa_seat_console_toggle(seat->console,false,false,error);
    if (kind==1 || client_name(command->argv[0],"togglemenu"))
        return menu_command(seat,&command->context,FRONTEND_HOME,true,error);
    if (kind==2 || kind==3) return qa_seat_console_message(seat->console,kind==3,false,0,error);
    if (kind >= 4 && kind - 4 < sizeof(menu_destinations)/sizeof(*menu_destinations))
        return menu_command(seat,&command->context,menu_destinations[kind-4],false,error);
    return frontend_fail(error,QA_ERROR_ARGUMENT,"Unknown registered CLIENT menu command");
}
bool frontend_commands_client_unbind(frontend_client_commands **slot,qa_error *error)
{
    frontend_client_commands *owner=slot?*slot:NULL;
    if (!owner) return true;
    if (!qa_console_cvar_returned(owner->console))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT menu handler has not returned");
    if (!frontend_demo_dispatch_unregister(owner->frontend->demos,owner->console,
        owner->command_owner,owner->receiver,error)) return false;
    for (size_t i=0;i<owner->registered;++i) qa_console_unregister(owner->console,client_menus[i],owner->receiver);
    free(owner); *slot=NULL; return true;
}
bool frontend_commands_client_bind(qa_frontend *f,const qa_application_client_source *source,
    frontend_client_commands **out,qa_error *error)
{
    if (!f || !source || !out || *out || !source->context.receiver ||
        !qa_console_cvar_returned(source->context.console) ||
        (!qa_application_client_current(f->application,source) &&
            !(f->source_restoring && qa_application_client_retirement_current(f->application,source))))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT menus require their actual returned Source console");
    frontend_client_commands *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Owning CLIENT menu command handlers");
    *owner=(frontend_client_commands){.frontend=f,.console=source->context.console,.cvars=source->context.cvars,
        .lifetime=source->context.lifetime,.receiver=source->context.receiver,
        .command_owner=source->context.command.owner,
        .seat=source->context.seat,.physical=source->context.physical_seat};
    *out=owner;
    for (size_t i=0;i<sizeof(client_menus)/sizeof(*client_menus);++i) {
        if (client_name(client_menus[i],"help") && source->context.command.dialect!=QA_CONSOLE_Q1 &&
            source->context.command.dialect!=QA_CONSOLE_QW)continue;
        if (!qa_console_register_owned(owner->console,client_menus[i],"Native client menu command",0,
            owner->receiver,true,client_menu_command,owner,error)) {
            qa_error original=error?*error:(qa_error){0};
            if (!frontend_commands_client_unbind(out,error)) return false;
            if (error) *error=original;
            return false;
        }
        ++owner->registered;
    }
    if (f->demos && !frontend_demo_dispatch_register(f->demos,owner->console,owner->command_owner,
        owner->receiver,error)) {
        qa_error original=error?*error:(qa_error){0};
        if (!frontend_commands_client_unbind(out,error)) return false;
        if (error) *error=original;
        return false;
    }
    return true;
}
void frontend_commands_client_rebind(frontend_client_commands *owner,qa_frontend *f)
{ if (owner && f) owner->frontend=f; }

static qa_input_seat *input_seat(void *context, const qa_command_context *command)
{
    qa_frontend *frontend = context;
    uint32_t ordinal;
    return frontend_command_seat_read(frontend,command,&ordinal)?frontend->seats[ordinal].input:NULL;
}
static bool scores(void *context, const qa_command_invocation *command)
{
    qa_frontend *frontend = context;
    uint32_t ordinal;
    if (!frontend_command_seat_read(frontend,&command->context,&ordinal)) return false;
    frontend->seats[ordinal].scores = command->argv[0][0] == '+';
    return true;
}
static void center(void *context, qa_input_seat *input)
{
    qa_frontend *frontend = context;
    for (unsigned i = 0; i < frontend->options.seats; ++i)
        if (frontend->seats[i].input == input) qa_input_command_center(&frontend->seats[i].builder, 0);
}
static bool command(void *context, const qa_command_invocation *invocation, qa_error *error)
{
    qa_frontend *frontend = context;
    const char *name = invocation->argv[0];
    if (!strcmp(name,"cinematic") || !strcmp(name,"cinematicpause") || !strcmp(name,"stopcinematic")) {
        bool handled;
        if (!frontend_system_cinematic_command(frontend,invocation,&handled,error)) return false;
        return handled || frontend_cinematic_command(frontend,invocation,error);
    }
    if (!strcmp(name,"save") || !strcmp(name,"load"))
        return frontend_save_commands_queue(frontend,invocation,error);
    if (!strcmp(name,"cd") || !strcmp(name,"music"))
        return frontend_music_sources_command(frontend->music_sources,invocation,error);
    if (!strcmp(name,"sky")) {
        if (invocation->argc>2) { frontend_print(frontend,"usage: sky <skyname>\n"); return true; }
        if (invocation->argc==2) return frontend_q1_sky_command(frontend->q1_sky,invocation->argv[1],error);
        const char *selected;
        if (!frontend_q1_sky_name(frontend->q1_sky,invocation->context.actor,&selected,error)) return false;
        char text[1200]; snprintf(text,sizeof(text),"\"sky\" is \"%s\"\n",selected); frontend_print(frontend,text); return true;
    }
    if (!strcmp(name,"actualimagegrid")) {
        if (invocation->argc>2) { frontend_print(frontend,"usage: actualimagegrid [0|1|2]\n"); return true; }
        int32_t mode=1;
        if (invocation->argc==2) {
            if (!strcmp(invocation->argv[1],"2")) mode=2;
            else if (!strcmp(invocation->argv[1],"0")) mode=0;
            else if (strcmp(invocation->argv[1],"1")) { frontend_print(frontend,"usage: actualimagegrid [0|1|2]\n"); return true; }
        }
        const char *value=mode==2?"2":mode==1?"1":"0";
        return qa_cvars_set(qa_application_cvars(frontend->application),"r_showImages",value,true,error) &&
            (mode==0 || frontend_source_renderer_image_grid(frontend,mode,error));
    }
    if (!strcmp(name, "quit")) { qa_application_request_stop(frontend->application); return true; }
    uint32_t slot;
    if (!frontend_command_seat_read(frontend,&invocation->context,&slot))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "command requires a local player");
    frontend_seat *seat = &frontend->seats[slot];
    if (!strcmp(name,"help"))return menu_command(seat,&invocation->context,FRONTEND_Q1_HELP,false,error);
    if (!strcmp(name, "toggleconsole")) return qa_seat_console_toggle(seat->console, false, false, error);
    if (!strcmp(name, "menu") || !strcmp(name,"togglemenu"))
        return menu_command(seat,&invocation->context,FRONTEND_HOME,true,error);
    const char *menus[] = {"menu_anthology", "library", "mods", "settings", "rankings", "assistance", "controls"};
    for (unsigned i = 0; i < sizeof(menus) / sizeof(*menus); ++i)
        if (!strcmp(name, menus[i])) return menu_command(seat,&invocation->context,menu_destinations[i],false,error);
    if (!strcmp(name, "weapnext") || !strcmp(name, "weapprev"))
        return qa_hud_wheel_cycle(seat->wheel, !strcmp(name, "weapnext") ? 1 : -1, frontend->time_ns, error);
    if (!strcmp(name, "messagemode") || !strcmp(name, "messagemode2"))
        return qa_seat_console_message(seat->console, !strcmp(name, "messagemode2"), false, 0, error);
    return false;
}
bool frontend_commands(qa_frontend *frontend, qa_error *error)
{
    qa_console *console = qa_application_console(frontend->application);
    const char *names[] = {"quit", "help", "toggleconsole", "menu", "togglemenu", "messagemode", "messagemode2", "weapnext", "weapprev",
        "menu_anthology", "library", "mods", "settings", "rankings", "assistance", "controls", "save", "load",
        "cinematic", "cinematicpause", "stopcinematic", "cd", "music", "sky", "actualimagegrid"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!qa_console_register_owned(console, names[i], "Native frontend command", 0,
            QA_FRONTEND_COMMAND_OWNER, true, command, frontend, error)) return false;
    if (frontend->options.dedicated) return true;
    qa_input_console_options input = {.console = console, .owner = QA_FRONTEND_COMMAND_OWNER,
        .user = frontend, .seat = input_seat, .print = frontend_print, .scores = scores, .center = center, .wheel = frontend_wheel_command};
    frontend->input_commands = qa_input_console_create(&input, error);
    return frontend->input_commands != NULL;
}
