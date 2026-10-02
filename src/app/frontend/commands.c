#include "internal.h"
#include "save_commands.h"
#include "campaign_cinematic.h"
#include "system_cinematic.h"
#include "music_sources.h"
#include "q1_sky.h"
#include <stdio.h>

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
    if (!strcmp(name, "quit")) { qa_application_request_stop(frontend->application); return true; }
    uint32_t slot;
    if (!frontend_command_seat_read(frontend,&invocation->context,&slot))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "command requires a local player");
    frontend_seat *seat = &frontend->seats[slot];
    if (!strcmp(name, "toggleconsole")) return qa_seat_console_toggle(seat->console, false, false, error);
    if (!strcmp(name, "menu")) return frontend_game_menu(seat, error);
    const char *menus[] = {"menu_anthology", "library", "mods", "settings", "rankings", "assistance", "controls"};
    for (unsigned i = 0; i < sizeof(menus) / sizeof(*menus); ++i)
        if (!strcmp(name, menus[i])) return frontend_menu_open(seat, FRONTEND_HOME + i, error);
    if (!strcmp(name, "weapnext") || !strcmp(name, "weapprev"))
        return qa_hud_wheel_cycle(seat->wheel, !strcmp(name, "weapnext") ? 1 : -1, frontend->time_ns, error);
    if (!strcmp(name, "messagemode") || !strcmp(name, "messagemode2"))
        return qa_seat_console_message(seat->console, !strcmp(name, "messagemode2"), false, 0, error);
    return false;
}
bool frontend_commands(qa_frontend *frontend, qa_error *error)
{
    qa_console *console = qa_application_console(frontend->application);
    const char *names[] = {"quit", "toggleconsole", "menu", "messagemode", "messagemode2", "weapnext", "weapprev",
        "menu_anthology", "library", "mods", "settings", "rankings", "assistance", "controls", "save", "load",
        "cinematic", "cinematicpause", "stopcinematic", "cd", "music", "sky"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!qa_console_register_owned(console, names[i], "Native frontend command", 0,
            QA_FRONTEND_COMMAND_OWNER, true, command, frontend, error)) return false;
    if (frontend->options.dedicated) return true;
    qa_input_console_options input = {.console = console, .owner = QA_FRONTEND_COMMAND_OWNER,
        .user = frontend, .seat = input_seat, .print = frontend_print, .scores = scores, .center = center, .wheel = frontend_wheel_command};
    frontend->input_commands = qa_input_console_create(&input, error);
    return frontend->input_commands != NULL;
}
