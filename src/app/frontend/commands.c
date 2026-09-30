#include "internal.h"
#include <stdio.h>

static qa_input_seat *input_seat(void *context, const qa_command_context *command)
{
    qa_frontend *frontend = context;
    return command->seat < frontend->options.seats ? frontend->seats[command->seat].input : NULL;
}
static bool scores(void *context, const qa_command_invocation *command)
{
    qa_frontend *frontend = context;
    if (command->context.seat >= frontend->options.seats) return false;
    frontend->seats[command->context.seat].scores = command->argv[0][0] == '+';
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
    if (!strcmp(name, "quit")) { qa_application_request_stop(frontend->application); return true; }
    unsigned slot = invocation->context.seat;
    if (slot >= frontend->options.seats || frontend->options.dedicated)
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
        "menu_anthology", "library", "mods", "settings", "rankings", "assistance", "controls"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!qa_console_register_owned(console, names[i], "Native frontend command", 0,
            QA_FRONTEND_COMMAND_OWNER, true, command, frontend, error)) return false;
    qa_cvars *cvars = qa_application_cvars(frontend->application);
    if (!qa_cvars_register(cvars, "r_gamma", "1", QA_CVAR_ARCHIVE, QA_FRONTEND_COMMAND_OWNER,
        "Output brightness, 0.5 through 3", error) ||
        !qa_cvars_set_number(cvars, "r_gamma", frontend->options.gamma, error) ||
        !qa_cvars_register(cvars, "s_volume", "0.7", QA_CVAR_ARCHIVE, QA_FRONTEND_COMMAND_OWNER, "Master audio gain", error)) return false;
    if (frontend->options.dedicated) return true;
    qa_input_console_options input = {.console = console, .owner = QA_FRONTEND_COMMAND_OWNER,
        .user = frontend, .seat = input_seat, .print = frontend_print, .scores = scores, .center = center, .wheel = frontend_wheel_command};
    frontend->input_commands = qa_input_console_create(&input, error);
    return frontend->input_commands != NULL;
}
