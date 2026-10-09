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
static bool client_name(const char *text,const char *name)
{
    for (;*text && *name;++text,++name) {
        unsigned char c=(unsigned char)*text;
        if (c>='A' && c<='Z') c=(unsigned char)(c+'a'-'A');
        if (c!=(unsigned char)*name) return false;
    }
    return !*text && !*name;
}
static bool world_command(qa_frontend *f,const qa_application_startup_source *source,
    const qa_command_invocation *call,bool *handled,qa_error *error)
{
    const char *name=call->argv[0];
    *handled=false;
    if (!client_name(name,"map") && !client_name(name,"gamemap") &&
        !client_name(name,"changelevel") && !client_name(name,"killserver")) return true;
    if (call->context.origin==QA_COMMAND_REMOTE) {
        *handled=true;
        return true;
    }
    bool parked=frontend_config_store_parked_current(f->config_store,source);
    bool q2=parked || source->scope.kind==QA_APPLICATION_CONSOLE_Q2_GAME ||
        source->scope.kind==QA_APPLICATION_CONSOLE_NATIVE_Q2;
    bool map=client_name(name,"map"),gamemap=client_name(name,"gamemap");
    bool changelevel=client_name(name,"changelevel");
    if (client_name(name,"killserver")) {
        *handled=true;
        if (!q2) {
            frontend_console_print(f,&call->context,"No Q2 server running.\n");
            return true;
        }
        const qa_launch_snapshot *published=qa_application_launch(f->application);
        const qa_launch_instance *live=published && source->descriptor?
            qa_launch_snapshot_find(published,source->descriptor->selection.instance):NULL;
        if (!live || live->storage!=source->descriptor->storage || live->state!=source->descriptor->state) {
            if (call->context.dialect==QA_RULESET_Q2_RERELEASE)
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
    if (map || gamemap || changelevel) {
        *handled=true;
        if (call->argc!=2) {
            frontend_console_print(f,&call->context,"usage: ");
            frontend_console_print(f,&call->context,name);
            frontend_console_print(f,&call->context," <map>\n");
            return true;
        }
        const char *destination=call->argv[1];
        if (!q2 || (map && !strchr(destination,'.'))) {
            size_t length=strlen(destination);
            bool prefix=!strncmp(destination,"maps/",5);
            bool suffix=length>=4 && !strcmp(destination+length-4,".bsp");
            if (length>SIZE_MAX-10)
                return frontend_fail(error,QA_ERROR_MEMORY,"Map command destination is too long");
            char *expanded=malloc(length+(prefix?0u:5u)+(suffix?0u:4u)+1u);
            if (!expanded) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining map command path");
            snprintf(expanded,length+(prefix?0u:5u)+(suffix?0u:4u)+1u,"%s%s%s",
                prefix?"":"maps/",destination,suffix?"":".bsp");
            qa_vfs *files=source->descriptor->content;
            bool found=false; uint64_t size=0;
            bool ok=files && qa_vfs_probe(files,expanded,&found,&size,error);
            free(expanded);
            if (!files) return frontend_fail(error,QA_ERROR_ARGUMENT,"Map command lost its Source filesystem");
            if (!ok) return false;
            if (!found) {
                frontend_console_print(f,&call->context,"Can't find map ");
                frontend_console_print(f,&call->context,destination);
                frontend_console_print(f,&call->context,"\n");
                return true;
            }
        }
        bool stopping=f->server_stop_owner==source->scope.provider && !f->server_stopped;
        qa_application_travel_request request={
            .provider=source->scope.provider,.cause=call->context.actor,.expression=destination,
            .new_unit=map,.carry_players=!map && !stopping && !parked,
            .complete_campaign=changelevel};
        bool queued=q2?qa_application_queue_travel(f->application,&request,error):
            qa_application_queue_map_travel(f->application,&request,error);
        /* Q2 resumes the remaining command buffer only after ClientBegin in
         * the new world. Travel is asynchronous here, so retain that tail as
         * soon as the actual request has been accepted. */
        qa_application_travel_view travel;
        if (q2 && queued && qa_application_travel_read(f->application,&travel) &&
            travel.target.kind==QA_TRAVEL_MAP && !qa_console_defer(call->console,error)) return false;
        if (queued) frontend_demo_dispatch_manual_game(f->demos);
        if (queued && stopping) f->server_stop_follow_map=true;
        return queued;
    }
    return true;
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
    if ((call->context.dialect==QA_RULESET_NETQUAKE || call->context.dialect==QA_RULESET_QUAKEWORLD) && client_name(name,"bf")) {
        *handled=true;
        uint32_t physical; qa_actor_id actor;
        if (!frontend_command_seat_read(f,&call->context,&physical) ||
            !frontend_seat_actor_read(f,physical,&actor)) return true;
        return frontend_view_q1_local_bonus(f,actor,error);
    }
    if (!world_command(f,source,call,handled,error)) return false;
    if (*handled) return true;
    return true;
}
static qa_input_seat *input_seat(void *context, const qa_command_context *command)
{
    qa_frontend *frontend = context;
    uint32_t ordinal;
    return frontend_command_seat_read(frontend,command,&ordinal)?frontend->seats[ordinal].input:NULL;
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
    if (client_name(name,"map") || client_name(name,"gamemap") ||
        client_name(name,"changelevel") || client_name(name,"killserver")) {
        qa_application_startup_source source; bool present=false,handled=false;
        if (!frontend_config_store_primary_server_read(frontend->config_store,&source,&present,error)) return false;
        if (!present) {
            frontend_console_print(frontend,&invocation->context,"No server running.\n");
            return true;
        }
        return world_command(frontend,&source,invocation,&handled,error) && handled;
    }
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
        "cinematic", "cinematicpause", "stopcinematic", "cd", "music", "sky", "actualimagegrid",
        "map", "gamemap", "changelevel", "killserver"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!qa_console_register_owned(console, names[i], "Native frontend command", 0,
            QA_FRONTEND_COMMAND_OWNER, true, command, frontend, error)) return false;
    if (frontend->options.dedicated) return true;
    qa_input_console_options input = {.console = console, .owner = QA_FRONTEND_COMMAND_OWNER,
        .user = frontend, .seat = input_seat, .print = frontend_print, .center = center, .wheel = frontend_wheel_command};
    frontend->input_commands = qa_input_console_create(&input, error);
    return frontend->input_commands != NULL;
}
