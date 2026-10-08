#include "local_lobby.h"
#include "startup_menus.h"
#include "qa/ui_library.h"
#include <inttypes.h>
#include <stdio.h>

typedef enum lobby_operation {
    LOBBY_NONE, LOBBY_HOST, LOBBY_JOIN, LOBBY_READY, LOBBY_START, LOBBY_LEAVE
} lobby_operation;
typedef struct lobby_menu {
    frontend_local_lobby *owner;
    frontend_seat *seat;
    qa_ui *ui;
    qa_ui_id id;
    char name[257], labels[14][512];
    uint32_t capacity;
    size_t page;
    qa_lobby_id joins[4];
    qa_ui_control controls[14];
    bool registered;
} lobby_menu;
struct frontend_local_lobby {
    qa_frontend *frontend;
    qa_lobbies *service;
    qa_lobby_session *session;
    char *account_id;
    char status[256];
    lobby_menu *menus[QA_INPUT_LOCAL_SEATS];
    lobby_operation operation;
    uint32_t physical;
    frontend_host_settings hosting;
    bool match_active;
    qa_error launch_failure;
    bool closing, return_menu;
    union {
        struct { char name[257]; uint32_t capacity, seats; } host;
        struct { qa_lobby_id id; uint32_t seats; } join;
        bool ready;
    } pending;
};

static double now(const frontend_local_lobby *owner)
{ return (double)owner->frontend->time_ns / 1000000.0; }
static const qa_lobby_view *current(const frontend_local_lobby *owner)
{ return qa_lobby_read(qa_lobby_session_current(owner->session)); }
static bool busy(const frontend_local_lobby *owner)
{
    const qa_lobby_view *view = current(owner);
    return owner->operation != LOBBY_NONE || owner->frontend->startup_launch ||
        (view && view->phase == QA_LOBBY_STARTING);
}
static uint32_t seats(const lobby_menu *menu)
{ return frontend_local_seat_count(qa_ui_library_choices(menu->seat->library)); }
void frontend_local_lobby_report(frontend_local_lobby *owner, const char *message)
{ snprintf(owner->status, sizeof(owner->status), "%s", message); }
static const qa_lobby_member *member(const frontend_local_lobby *owner,
    const qa_lobby_view *view)
{
    if (!view) return NULL;
    for (size_t i = 0; i < view->member_count; ++i)
        if (!strcmp(view->members[i].account.id, owner->account_id)) return view->members + i;
    return NULL;
}
static bool action(void *context, uint32_t physical, qa_ui_id control,
    const qa_ui_action *event, qa_error *error)
{
    lobby_menu *menu = context;
    frontend_local_lobby *owner = menu->owner;
    (void)physical;
    if (control == 99 && event->kind == QA_UI_ACTIVATE)
        return qa_ui_close(menu->ui, now(owner), error);
    if (busy(owner)) return true;
    if (control == 1 && event->kind == QA_UI_CHANGE_TEXT) {
        snprintf(menu->name, sizeof(menu->name), "%s", event->value.text);
        return true;
    }
    if (control == 2 && event->kind == QA_UI_CHANGE_NUMBER) {
        menu->capacity = (uint32_t)event->value.number;
        return true;
    }
    if (event->kind != QA_UI_ACTIVATE) return true;
    if (control == 8) { if (menu->page) --menu->page; return true; }
    if (control == 9) { ++menu->page; return true; }
    if (control == 11) {
        const qa_lobby_view *view = current(owner);
        if (view && view->member_count)
            menu->page = (menu->page + 1) % ((view->member_count + 3) / 4);
        return true;
    }
    owner->physical = menu->seat->id;
    owner->status[0] = 0;
    if (control == 3) {
        owner->operation = LOBBY_HOST;
        snprintf(owner->pending.host.name, sizeof(owner->pending.host.name), "%s", menu->name);
        owner->pending.host.capacity = menu->capacity;
        owner->pending.host.seats = seats(menu);
    } else if (control >= 20 && control < 24) {
        owner->operation = LOBBY_JOIN;
        owner->pending.join.id = menu->joins[control - 20];
        owner->pending.join.seats = seats(menu);
    } else if (control == 4) {
        const qa_lobby_member *local = member(owner,current(owner));
        if (!local) return true;
        owner->operation = LOBBY_READY;
        owner->pending.ready = !local->ready;
    } else if (control == 5) owner->operation = LOBBY_START;
    else if (control == 6) owner->operation = LOBBY_LEAVE;
    return true;
}
static qa_ui_control button(lobby_menu *menu, qa_ui_id id, const char *label,
    unsigned row, bool enabled)
{
    return (qa_ui_control){.id = id, .kind = QA_UI_BUTTON, .label = label,
        .rect = {64, 92 + (float)row * 28, 512, 28}, .enabled = enabled,
        .visible = true, .context = menu, .action = action};
}
static uint64_t occupied(const qa_lobby_view *view)
{
    uint64_t count = 0;
    for (size_t i = 0; i < view->member_count; ++i) count += view->members[i].seats;
    return count;
}
static size_t open_count(const frontend_local_lobby *owner)
{
    size_t count = 0;
    for (size_t i = 0; i < qa_lobbies_count(owner->service); ++i)
        count += qa_lobby_read(qa_lobbies_at(owner->service, i))->phase == QA_LOBBY_OPEN;
    return count;
}
static bool factory(void *context, uint32_t physical, qa_ui_menu *out, qa_error *error)
{
    lobby_menu *menu = context;
    frontend_local_lobby *owner = menu->owner;
    const qa_lobby_view *view = current(owner);
    bool enabled = !busy(owner);
    size_t count = 0;
    (void)physical;
    (void)error;
    if (!view) {
        size_t available = open_count(owner), pages = (available + 3) / 4;
        if (!pages) menu->page = 0;
        else if (menu->page >= pages) menu->page = pages - 1;
        menu->controls[count] = button(menu, 1, "Lobby name", 0, enabled);
        menu->controls[count].kind = QA_UI_FIELD;
        menu->controls[count].value.field.text = menu->name;
        menu->controls[count].value.field.maximum = 64;
        ++count;
        menu->controls[count] = button(menu, 2, "Player capacity", 1, enabled);
        menu->controls[count].kind = QA_UI_SLIDER;
        menu->controls[count].value.slider.value = menu->capacity;
        menu->controls[count].value.slider.minimum = 1;
        menu->controls[count].value.slider.maximum = 64;
        menu->controls[count].value.slider.step = 1;
        ++count;
        menu->controls[count++] = button(menu, 3, "Host selected game", 2,
            enabled && menu->name[strspn(menu->name, " \t\r\n")] != 0);
        size_t row = 0, shown = 0;
        for (size_t i = 0; i < qa_lobbies_count(owner->service) && shown < 4; ++i) {
            const qa_lobby_view *candidate = qa_lobby_read(qa_lobbies_at(owner->service, i));
            if (candidate->phase != QA_LOBBY_OPEN) continue;
            if (row++ < menu->page * 4) continue;
            menu->joins[shown] = candidate->id;
            snprintf(menu->labels[count], sizeof(menu->labels[count]),
                "Join %s (%" PRIu64 "/%u)", candidate->name, occupied(candidate), candidate->capacity);
            menu->controls[count] = button(menu, 20 + shown, menu->labels[count],
                3 + (unsigned)shown, enabled);
            ++count; ++shown;
        }
        menu->controls[count++] = button(menu, 8, "Previous lobbies", 7, enabled && menu->page > 0);
        menu->controls[count++] = button(menu, 9, "Next lobbies", 8, enabled && menu->page + 1 < pages);
    } else {
        const qa_lobby_member *local = member(owner, view);
        bool host = !strcmp(view->owner, owner->account_id), ready = true;
        size_t pages = (view->member_count + 3) / 4;
        if (menu->page >= pages) menu->page = pages - 1;
        snprintf(menu->labels[count], sizeof(menu->labels[count]), "%s: %s", view->name,
            view->phase == QA_LOBBY_PLAYING ? "Playing" :
            view->phase == QA_LOBBY_STARTING ? "Starting host" : "Waiting for readiness");
        menu->controls[count] = button(menu, 10, menu->labels[count], 0, false); ++count;
        for (size_t i = 0; i < view->member_count; ++i) ready &= view->members[i].ready;
        for (size_t i = menu->page * 4; i < view->member_count && i < (menu->page + 1) * 4; ++i) {
            const qa_lobby_member *entry = view->members + i;
            snprintf(menu->labels[count], sizeof(menu->labels[count]), "%s: %s (%u seat%s)",
                entry->account.name, entry->ready ? "Ready" : "Not ready", entry->seats,
                entry->seats == 1 ? "" : "s");
            menu->controls[count] = button(menu, 30 + i, menu->labels[count],
                1 + (unsigned)(i % 4), false); ++count;
        }
        menu->controls[count++] = button(menu, 4, local && local->ready ? "Not ready" : "Ready", 5,
            enabled && local && view->phase == QA_LOBBY_OPEN);
        menu->controls[count++] = button(menu, 5,
            view->match_generation ? "Start next match" : "Start match", 6,
            enabled && host && view->phase == QA_LOBBY_OPEN && ready);
        menu->controls[count++] = button(menu, 6, host ? "Close lobby" : "Leave lobby", 7, enabled);
        menu->controls[count++] = button(menu, 11, "More members", 8, enabled && pages > 1);
    }
    menu->controls[count++] = button(menu, 98, owner->status[0] ? owner->status :
        busy(owner) ? "Working..." : view ? "Set Ready before starting the match" :
        open_count(owner) ? "Choose a lobby or host your selected game" : "No open lobbies", 9, false);
    menu->controls[count++] = button(menu, 99, "Back", 11, enabled);
    *out = (qa_ui_menu){.id = menu->id, .title = "Local lobbies",
        .controls = menu->controls, .count = count};
    return true;
}
static frontend_seat *launch_seat(frontend_local_lobby *owner)
{
    return owner->frontend->seats +
        (owner->physical < owner->frontend->options.seats ? owner->physical : 0);
}
static bool host(void *context, const qa_lobby_view *view, qa_net_address *endpoint,
    qa_lobby_wire *wire, qa_error *error)
{
    frontend_local_lobby *owner = context;
    (void)endpoint; (void)wire;
    owner->launch_failure = (qa_error){0};
    return frontend_startup_lobby_launch_stage(launch_seat(owner),view,&owner->hosting,false,error);
}
static bool host_ready(void *context, const qa_lobby_view *view, bool *ready,
    qa_net_address *endpoint, qa_lobby_wire *wire, qa_error *error)
{
    frontend_local_lobby *owner = context;
    (void)view;
    *ready = false;
    if (owner->launch_failure.code != QA_OK) {
        if (error) *error = owner->launch_failure;
        owner->launch_failure = (qa_error){0};
        return false;
    }
    if (owner->frontend->startup_launch ||
        qa_application_startup_pending(owner->frontend->application)) return true;
    return frontend_network_lobby_host_read(owner->frontend,ready,endpoint,wire,error);
}
static bool join(void *context, const qa_lobby_view *view, qa_error *error)
{
    frontend_local_lobby *owner = context;
    owner->launch_failure = (qa_error){0};
    return frontend_startup_lobby_launch_stage(launch_seat(owner),view,NULL,true,error);
}
static bool leave(void *context, const qa_lobby_view *view, qa_error *error)
{
    frontend_local_lobby *owner = context;
    (void)view;
    if (owner->closing) return true;
    bool okay = frontend_startup_lobby_end_stage(owner->frontend,owner->physical,error);
    if (okay) {
        owner->match_active = false;
        owner->return_menu = true;
    }
    return okay;
}
static bool completed(void *context, const qa_lobby_view *view, qa_error *error)
{ return leave(context,view,error); }
bool frontend_local_lobby_init(qa_frontend *frontend, qa_error *error)
{
    static uint64_t serial;
    uint64_t instance = ++serial;
    char id[64];
    snprintf(id,sizeof(id),"local-%" PRIu64,instance);
    qa_local_account account = frontend->options.local_account.id ? frontend->options.local_account :
        (qa_local_account){id,"Local player"};
    frontend->lobbies = frontend->options.local_lobbies;
    if (!frontend->lobbies) {
        if (!qa_lobbies_create(instance,&frontend->lobbies,error)) return false;
        frontend->lobbies_owned = true;
    }
    frontend_local_lobby *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating local lobby menu owner");
    owner->account_id = malloc(strlen(account.id) + 1);
    if (!owner->account_id) {
        free(owner);
        return frontend_fail(error, QA_ERROR_MEMORY, "Retaining local lobby account name");
    }
    strcpy(owner->account_id, account.id);
    owner->frontend = frontend; owner->service = frontend->lobbies;
    frontend->local_lobby = owner;
    return qa_lobby_session_create(frontend->lobbies,account,
        &(qa_lobby_transitions){.context=owner,.host=host,.host_ready=host_ready,.join=join,
            .leave=leave,.completed=completed},&owner->session,error);
}
void frontend_local_lobby_hosting(frontend_local_lobby *owner, uint32_t physical,
    const frontend_host_settings *settings)
{ owner->physical = physical; owner->hosting = *settings; }
void frontend_local_lobby_launch_started(frontend_local_lobby *owner)
{ owner->match_active = true; }
void frontend_local_lobby_launch_failed(frontend_local_lobby *owner, const qa_error *failure)
{
    owner->launch_failure = *failure;
    owner->return_menu = true;
    frontend_local_lobby_report(owner,failure->message);
}
bool frontend_local_lobby_owns_match(const frontend_local_lobby *owner)
{ return owner && owner->match_active; }
void frontend_local_lobby_match_replaced(frontend_local_lobby *owner)
{
    if (!owner || !owner->match_active) return;
    owner->match_active = false;
    const qa_lobby_view *view = current(owner);
    qa_error error = {0};
    if (view) {
        bool okay = !strcmp(view->owner,owner->account_id) ?
            qa_lobby_session_complete(owner->session,&error) :
            qa_lobby_session_leave(owner->session,&error);
        if (!okay) frontend_local_lobby_report(owner,error.message);
    }
    owner->return_menu = false;
}
void frontend_local_lobby_end_complete(frontend_local_lobby *owner)
{
    owner->match_active = false;
    const qa_lobby_view *view = current(owner);
    qa_error error = {0};
    if (view) {
        bool okay = !strcmp(view->owner,owner->account_id) ?
            qa_lobby_session_complete(owner->session,&error) :
            qa_lobby_session_leave(owner->session,&error);
        if (!okay) frontend_local_lobby_report(owner,error.message);
    }
    owner->return_menu = true;
}
bool frontend_local_lobby_menu_create(frontend_local_lobby *owner, frontend_seat *seat,
    qa_ui_id id, qa_error *error)
{
    lobby_menu *menu = calloc(1, sizeof(*menu));
    if (!menu) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating local lobby menu view");
    menu->owner = owner; menu->seat = seat; menu->ui = seat->ui; menu->id = id;
    strcpy(menu->name, "Local lobby"); menu->capacity = 4;
    owner->menus[seat->id] = menu;
    menu->registered = qa_ui_register(menu->ui,
        &(qa_ui_menu_registration){.id = id, .context = menu, .factory = factory}, error);
    return menu->registered;
}
bool frontend_local_lobby_menu_destroy(frontend_local_lobby *owner, uint32_t physical,
    qa_error *error)
{
    lobby_menu *menu = owner->menus[physical];
    if (!menu) return true;
    if (menu->registered && !qa_ui_unregister(menu->ui, menu->id, now(owner), error)) return false;
    if (owner->operation != LOBBY_NONE && owner->physical == physical) owner->operation = LOBBY_NONE;
    owner->menus[physical] = NULL; free(menu); return true;
}
bool frontend_local_lobby_pump(frontend_local_lobby *owner, qa_error *error)
{
    qa_error failure = {0};
    bool okay = true;
    (void)error;
    lobby_operation operation = owner->operation;
    owner->operation = LOBBY_NONE;
    switch (operation) {
    case LOBBY_NONE: break;
    case LOBBY_HOST:
        okay = frontend_startup_lobby_host_stage(owner->menus[owner->physical]->seat,
            owner->session, owner->pending.host.name, owner->pending.host.capacity,
            owner->pending.host.seats, &failure); break;
    case LOBBY_JOIN:
        okay = qa_lobby_session_join(owner->session, owner->pending.join.id,
            owner->pending.join.seats, &failure); break;
    case LOBBY_READY: okay = qa_lobby_session_ready(owner->session, owner->pending.ready, &failure); break;
    case LOBBY_START: okay = qa_lobby_session_start(owner->session, &failure); break;
    case LOBBY_LEAVE: okay = qa_lobby_session_leave(owner->session, &failure); break;
    }
    if (!okay) frontend_local_lobby_report(owner, failure.message);
    failure = (qa_error){0};
    if (!qa_lobby_session_poll(owner->session, &failure))
        frontend_local_lobby_report(owner, failure.message);
    if (owner->return_menu && !owner->frontend->startup_launch &&
        !qa_application_startup_pending(owner->frontend->application) &&
        owner->frontend->options.seats) {
        lobby_menu *menu = owner->menus[launch_seat(owner)->id];
        if (menu) {
            failure = (qa_error){0};
            if (!frontend_menu_open(menu->seat,menu->id,&failure))
                frontend_local_lobby_report(owner,failure.message);
            else owner->return_menu = false;
        }
    }
    return true;
}
bool frontend_local_lobby_destroy(frontend_local_lobby **owner, qa_error *error)
{
    if (!*owner) return true;
    for (uint32_t i = 0; i < QA_INPUT_LOCAL_SEATS; ++i)
        if (!frontend_local_lobby_menu_destroy(*owner, i, error)) return false;
    (*owner)->closing = true;
    bool okay = qa_lobby_session_close((*owner)->session,error);
    free((*owner)->account_id); free(*owner); *owner = NULL; return okay;
}
