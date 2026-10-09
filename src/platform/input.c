#define _POSIX_C_SOURCE 200809L
#include "input_platform_private.h"
#include "qa/input_platform_save.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void report(qa_input_platform *p, const char *message) {
    if (p->options.print)
        p->options.print(p->options.user, message);
}
static bool failed(qa_error *error, const char *operation) {
    qa_error_set(error, QA_ERROR_IO, 0, "%s: %s", operation, SDL_GetError());
    return false;
}
static bool native_owner(qa_input_platform *p, qa_error *error) {
    if (p && p->native_owned && !p->settings_ticket)
        return input_platform_restore_abort_pending(p, error);
    if (p && p->settings_ticket) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native input is retained by its settings preparation");
        return false;
    }
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Detached platform has no native input ownership");
    return false;
}
static bool initialize_native(qa_input_platform *, double, qa_error *);
static const char *const cvar_names[INPUT_CVAR_COUNT] = {
    "in_mouse", "in_nograb", "in_joystick", "in_joystickProfile", "in_midi",
    "in_joystickSeat", "in_midiseat", "in_mididevice", "in_midichannel",
    "joy_threshold", "in_joyBallScale", "in_subframe", "in_debugjoystick", "in_midiport"
};
static void bind_cvars(qa_input_platform *p) {
    for (unsigned i = 0; i < INPUT_CVAR_COUNT; ++i)
        p->cvars[i] = p->constructor_edit ? qa_cvars_edit_resolve(p->constructor_edit, cvar_names[i]) :
            qa_cvars_resolve(p->options.cvars, cvar_names[i]);
}
static const qa_cvar_view *read_cvar(qa_input_platform *p, input_cvar key) {
    return p->constructor_edit ? qa_cvars_edit_read(p->constructor_edit, p->cvars[key]) :
        qa_cvars_read(p->options.cvars, p->cvars[key]);
}
static float variable(qa_input_platform *p, input_cvar key, float fallback) {
    const qa_cvar_view *v = read_cvar(p, key);
    return v ? v->number : fallback;
}
static int integer(qa_input_platform *p, input_cvar key, int fallback) {
    const qa_cvar_view *v = read_cvar(p, key);
    return v ? v->integer : fallback;
}
static struct device *device(qa_input_platform *p, int32_t instance) {
    for (size_t i = 0; i < p->device_count; ++i)
        if (p->devices[i].info.instance == instance)
            return &p->devices[i];
    return NULL;
}
static struct seat_route *route_for(qa_input_platform *p, int32_t instance) {
    for (unsigned i = 0; i < 4; ++i)
        if (p->seats[i].seat && p->seats[i].instance == instance)
            return &p->seats[i];
    return NULL;
}
static bool seat_event(qa_input_platform *p, int slot, const qa_input_event *event,
                       qa_error *error) {
    return slot < 0 || slot >= 4 || !p->seats[slot].seat ||
           qa_input_seat_event(p->seats[slot].seat, event, NULL, error);
}
static bool source_key(void *user, int key, bool down, double time, qa_error *error) {
    qa_input_platform *p = user;
    qa_input_event event = {.kind = QA_INPUT_EVENT_KEY,
                            .time_ms = time,
                            .input = {.kind = QA_PHYSICAL_KEY, .code = (uint32_t)key},
                            .down = down};
    return seat_event(p, p->source_slot, &event, error);
}
static bool source_mouse(void *user, float x, float y, double time, qa_error *error) {
    qa_input_platform *p = user;
    qa_input_event event = {.kind = QA_INPUT_EVENT_MOUSE, .time_ms = time, .delta = {x, y}};
    return seat_event(p, p->source_slot, &event, error);
}
static bool source_device_release(qa_input_platform *p, double time, qa_error *error) {
    bool ok = qa_source_joystick_release(&p->source, time, source_key, p, error);
    if (p->source_slot >= 0 && p->seats[p->source_slot].seat) {
        struct seat_route *route = &p->seats[p->source_slot];
        if (!qa_input_seat_release_device(route->seat, p->joystick_instance, time, error))
            ok = false;
        if (!qa_haptic_stop(&route->haptic, error))
            ok = false;
        if (route->calibration_sensor &&
            !qa_input_platform_calibration_cancel(p, (unsigned)p->source_slot, true, error))
            ok = false;
    }
    return ok;
}
static bool midi_key(void *user, int key, bool down, double time, qa_error *error) {
    qa_input_platform *p = user;
    qa_input_event event = {.kind = QA_INPUT_EVENT_KEY,
                            .time_ms = time,
                            .input = {.kind = QA_PHYSICAL_KEY, .code = (uint32_t)key},
                            .down = down};
    if (key >= 0 && key < 256)
        p->midi_held[key] = down;
    return seat_event(p, p->midi_slot, &event, error);
}
static bool midi_release(qa_input_platform *p, double time, qa_error *error) {
    bool ok = true;
    for (int i = 0; i < 256; ++i)
        if (p->midi_held[i])
            if (!midi_key(p, i, false, time, error))
                ok = false;
    p->midi = (qa_midi_decoder){0};
    return ok;
}
static bool controller_motor(struct device *d, bool triggers, uint16_t low, uint16_t high,
    uint32_t duration, qa_error *error) {
    input_motor_output next = {.requested = true, .low = low, .high = high,
        .duration = duration, .ticks = SDL_GetTicks64()};
    int result = triggers ? SDL_GameControllerRumbleTriggers(d->handle, low, high, duration) :
        SDL_GameControllerRumble(d->handle, low, high, duration);
    next.applied = result == 0;
    *(triggers ? &d->triggers : &d->rumble) = next;
    return next.applied || failed(error, triggers ? "Controller trigger rumble" : "Controller rumble");
}
static bool source_motor(qa_input_platform *p, uint16_t low, uint16_t high,
    uint32_t duration, qa_error *error) {
    struct device *d = device(p, p->joystick_instance);
    if (d) return controller_motor(d, false, low, high, duration, error);
    input_motor_output next = {.requested = true, .low = low, .high = high,
        .duration = duration, .ticks = SDL_GetTicks64()};
    next.applied = SDL_JoystickRumble(p->joystick, low, high, duration) == 0;
    p->joystick_rumble = next;
    return next.applied || failed(error, "Source joystick rumble");
}
static bool controller_sensor(struct device *d, SDL_SensorType sensor, bool enabled, qa_error *error) {
    input_sensor_output next = {.requested = true, .enabled = enabled};
    next.applied = SDL_GameControllerSetSensorEnabled(d->handle, sensor,
        enabled ? SDL_TRUE : SDL_FALSE) == 0;
    d->sensor_output[(unsigned)sensor - 1] = next;
    return next.applied || failed(error, "Controller sensor");
}
static bool stop_device(qa_input_platform *p, int32_t instance, qa_error *error) {
    struct device *d = device(p, instance);
    if (!d || !SDL_GameControllerGetAttached(d->handle))
        return true;
    bool ok = true;
    if (d->info.rumble && !controller_motor(d, false, 0, 0, 0, error)) ok = false;
    if (d->info.trigger_rumble && !controller_motor(d, true, 0, 0, 0, error)) ok = false;
    for (unsigned i = 0; i < 6; ++i)
        if (d->info.sensors[i] &&
            SDL_GameControllerIsSensorEnabled(d->handle, (SDL_SensorType)(i + 1)) &&
            !controller_sensor(d, (SDL_SensorType)(i + 1), false, error)) ok = false;
    return ok;
}
static void describe(struct device *d) {
    SDL_Joystick *joy = SDL_GameControllerGetJoystick(d->handle);
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joy), d->info.guid, sizeof(d->info.guid));
    d->info.name = SDL_GameControllerName(d->handle);
    d->info.serial = SDL_GameControllerGetSerial(d->handle);
    d->info.rumble = SDL_GameControllerHasRumble(d->handle) == SDL_TRUE;
    d->info.trigger_rumble = SDL_GameControllerHasRumbleTriggers(d->handle) == SDL_TRUE;
    d->info.led = SDL_GameControllerHasLED(d->handle) == SDL_TRUE;
    for (unsigned i = 0; i < QA_AXIS_COUNT; ++i)
        d->info.axes[i] =
            SDL_GameControllerHasAxis(d->handle, (SDL_GameControllerAxis)i) == SDL_TRUE;
    for (unsigned i = 0; i < 21; ++i)
        d->info.buttons[i] =
            SDL_GameControllerHasButton(d->handle, (SDL_GameControllerButton)i) == SDL_TRUE;
    for (unsigned i = 0; i < 6; ++i)
        d->info.sensors[i] =
            SDL_GameControllerHasSensor(d->handle, (SDL_SensorType)(i + 1)) == SDL_TRUE;
    int touchpads = SDL_GameControllerGetNumTouchpads(d->handle);
    d->info.touchpads = touchpads > 0 ? (unsigned)touchpads : 0;
}
static bool discover(qa_input_platform *p, int index, qa_error *error) {
    int count = SDL_NumJoysticks();
    if (count < 0)
        return failed(error, "Enumerating controllers");
    int first = index < 0 ? 0 : index, last = index < 0 ? count : index + 1;
    for (int i = first; i < last && i < count; ++i) {
        if (!SDL_IsGameController(i))
            continue;
        int32_t instance = SDL_JoystickGetDeviceInstanceID(i);
        if (instance < 0 || device(p, instance))
            continue;
        SDL_GameController *handle = SDL_GameControllerOpen(i);
        if (!handle) {
            qa_error opening = {0};
            (void)failed(&opening, "Opening controller");
            SDL_PumpEvents();
            int remaining = SDL_NumJoysticks();
            bool present = false;
            for (int j = 0; j < remaining; ++j)
                if (SDL_JoystickGetDeviceInstanceID(j) == instance) {
                    present = true;
                    break;
                }
            if (remaining < 0 || present) {
                if (error) *error = opening;
                return false;
            }
            count = last = remaining;
            i = -1;
            continue;
        }
        if (!SDL_GameControllerGetAttached(handle)) {
            SDL_GameControllerClose(handle);
            continue;
        }
        instance = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(handle));
        if (instance < 0) {
            SDL_GameControllerClose(handle);
            return failed(error, "Reading opened controller instance");
        }
        if (device(p, instance)) {
            SDL_GameControllerClose(handle);
            continue;
        }
        SDL_LockJoysticks();
        int current = SDL_NumJoysticks(), opened_index = -1;
        for (int j = 0; j < current; ++j)
            if (SDL_JoystickGetDeviceInstanceID(j) == instance) {
                opened_index = j;
                break;
            }
        bool virtual_device = opened_index >= 0 && SDL_JoystickIsVirtual(opened_index) == SDL_TRUE;
        SDL_UnlockJoysticks();
        if (current < 0) {
            SDL_GameControllerClose(handle);
            return failed(error, "Enumerating opened controller");
        }
        if (opened_index < 0) {
            SDL_GameControllerClose(handle);
            continue;
        }
        struct device next = {
            .handle = handle,
            .info = {.instance = instance, .virtual_device = virtual_device}};
        describe(&next);
        for (unsigned ordinal = 0;; ++ordinal) {
            bool used = false;
            for (size_t j = 0; j < p->device_count; ++j)
                if (p->devices[j].info.ordinal == ordinal &&
                    strcmp(p->devices[j].info.guid, next.info.guid) == 0) {
                    used = true;
                    break;
                }
            if (!used) {
                next.info.ordinal = ordinal;
                break;
            }
        }
        if (p->device_count == p->device_capacity) {
            size_t capacity = p->device_capacity ? p->device_capacity * 2 : 8;
            if (capacity > SIZE_MAX / sizeof(*p->devices)) {
                SDL_GameControllerClose(handle);
                qa_error_set(error, QA_ERROR_MEMORY, 0, "Controller storage overflow");
                return false;
            }
            struct device *grown = realloc(p->devices, capacity * sizeof(*grown));
            if (!grown) {
                SDL_GameControllerClose(handle);
                qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating controller registry");
                return false;
            }
            p->devices = grown;
            p->device_capacity = capacity;
        }
        if (p->joystick && p->joystick_instance == instance) {
            next.rumble = p->joystick_rumble;
            p->joystick_rumble = (input_motor_output){0};
        }
        p->devices[p->device_count++] = next;
        if (p->options.device_changed)
            p->options.device_changed(p->options.user, instance, true);
    }
    return true;
}
static bool same_selection(const qa_controller_selection *a,const qa_controller_selection *b) {
    return a->kind == b->kind && a->ordinal == b->ordinal &&
        !memcmp(a->guid, b->guid, sizeof(a->guid)) &&
        ((!a->serial && !b->serial) || (a->serial && b->serial && !strcmp(a->serial,b->serial)));
}
static bool selection_matches(const qa_controller_selection *s,const qa_controller_info *d) {
    return s->kind == QA_CONTROLLER_AUTO ||
        ((s->kind == QA_CONTROLLER_GUID || s->kind == QA_CONTROLLER_SERIAL) &&
         !strcmp(s->guid,d->guid) && (s->kind == QA_CONTROLLER_GUID ? s->ordinal == d->ordinal :
            d->serial && !strcmp(s->serial,d->serial)));
}
static void resolve_routes(const qa_input_platform *p, const struct seat_route routes[4],
                           const int32_t retained[4], int32_t assignments[4]) {
    for (unsigned slot = 0; slot < 4; ++slot) assignments[slot] = -1;
    if (retained)
        for (unsigned slot = 0; slot < 4; ++slot) {
            const struct seat_route *r = &routes[slot];
            if (!r->seat || retained[slot] < 0) continue;
            for (size_t i = 0; i < p->device_count; ++i)
                if (p->devices[i].info.instance == retained[slot] &&
                    selection_matches(&r->selection,&p->devices[i].info)) {
                    assignments[slot] = retained[slot];
                    break;
                }
        }
    for (unsigned pass = 0; pass < 2; ++pass)
        for (unsigned slot = 0; slot < 4; ++slot) {
            const struct seat_route *r = &routes[slot];
            const qa_controller_selection *s = &r->selection;
            if (!r->seat || assignments[slot] >= 0 || s->kind == QA_CONTROLLER_NONE ||
                (s->kind == QA_CONTROLLER_AUTO) != (pass == 1))
                continue;
            const struct device *match = NULL;
            unsigned matches = 0;
            for (size_t i = 0; i < p->device_count; ++i) {
                const struct device *d = &p->devices[i];
                bool used = false;
                for (unsigned j = 0; j < 4; ++j)
                    if (assignments[j] == d->info.instance) {
                        used = true;
                        break;
                    }
                if (used)
                    continue;
                if (!selection_matches(s,&d->info)) continue;
                match = d;
                ++matches;
                if (s->kind == QA_CONTROLLER_AUTO)
                    break;
            }
            if (matches == 1)
                assignments[slot] = match->info.instance;
        }
}
static int source_route(const struct seat_route routes[4],int preferred,int32_t instance) {
    if (instance >= 0)
        for (unsigned slot = 0; slot < 4; ++slot)
            if (routes[slot].seat && routes[slot].instance == instance &&
                (routes[slot].selection.kind == QA_CONTROLLER_GUID ||
                 routes[slot].selection.kind == QA_CONTROLLER_SERIAL))
                return (int)slot;
    return preferred >= 1 && preferred <= 4 && routes[preferred-1].seat &&
        routes[preferred-1].selection.kind == QA_CONTROLLER_AUTO ? preferred-1 : -1;
}
static bool resolve(qa_input_platform *p, double time, const int32_t retained[4], qa_error *error) {
    int32_t assignments[4];
    resolve_routes(p, p->seats, retained, assignments);
    bool ok = true;
    for (unsigned slot = 0; slot < 4; ++slot) {
        struct seat_route *r = &p->seats[slot];
        int32_t previous = r->instance;
        if (previous == assignments[slot])
            continue;
        if (!qa_haptic_stop(&r->haptic, error))
            ok = false;
        if (previous >= 0) {
            if (r->seat && !qa_input_seat_release_device(r->seat, previous, time, error))
                ok = false;
            if (!stop_device(p, previous, error))
                ok = false;
        }
        r->calibration_sensor = false;
        r->instance = assignments[slot];
        if (r->instance >= 0 && r->seat) {
            qa_gamepad_calibration_reset(qa_input_seat_gamepad(r->seat));
            if (!qa_input_seat_remap_controller(r->seat, r->instance, error))
                ok = false;
            if (qa_input_seat_gamepad_tuning(r->seat)->gyro_enabled &&
                !qa_input_platform_sensor(p, r->instance, SDL_SENSOR_GYRO, true, error))
                ok = false;
        }
        if (p->options.assignment_changed)
            p->options.assignment_changed(p->options.user, slot, previous, r->instance);
    }
    return ok;
}
static bool haptic_sink(void *user, float low, float high, uint32_t duration, qa_error *error) {
    struct seat_route *r = user;
    int32_t instance = r->haptic_instance;
    struct device *d = device(r->platform, instance);
    bool connected = d ? SDL_GameControllerGetAttached(d->handle) == SDL_TRUE
                       : instance == r->platform->joystick_instance && r->platform->joystick &&
                             SDL_JoystickGetAttached(r->platform->joystick) == SDL_TRUE;
    if (instance < 0 || !connected)
        return low == 0 && high == 0;
    return qa_input_platform_rumble(r->platform, instance, low, high, duration, error);
}
bool input_platform_haptic_bindings_ready(const qa_input_platform *p) {
    if (!p) return false;
    for (unsigned i = 0; i < 4; ++i)
        if (p->seats[i].platform != p || p->seats[i].slot != i ||
            p->seats[i].haptic.output != haptic_sink || p->seats[i].haptic.user != &p->seats[i])
            return false;
    return true;
}
void input_platform_route_contexts_rebind(qa_input_platform *p) {
    bind_cvars(p);
    for (unsigned i = 0; i < 4; ++i) {
        p->seats[i].platform = p;
        p->seats[i].slot = i;
        p->seats[i].haptic.user = &p->seats[i];
    }
}
static bool haptic_device(struct seat_route *r, qa_error *error) {
    int32_t instance = qa_input_platform_controller(r->platform, r->slot);
    if (instance == r->haptic_instance)
        return true;
    bool ok = qa_haptic_stop(&r->haptic, error);
    r->haptic_instance = instance;
    return ok;
}
typedef struct input_pointer_modes {
    bool relative, grab, text;
} input_pointer_modes;
static input_pointer_modes pointer_modes(Uint32 flags, qa_input_focus focus,
    bool mouse_available, bool no_grab) {
    bool focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
    bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    bool grab = focused && mouse_available && (fullscreen || (focus == QA_INPUT_GAME && !no_grab));
    return (input_pointer_modes){.relative = grab && focus == QA_INPUT_GAME, .grab = grab,
        .text = focused && (focus == QA_INPUT_CONSOLE || focus == QA_INPUT_CHAT || focus == QA_INPUT_UI)};
}
bool qa_input_platform_sync_focus(qa_input_platform *p, qa_error *error) {
    if (!p->window)
        return true;
    SDL_Window *window = SDL_GetWindowFromID(p->window);
    if (!window)
        return true;
    qa_input_seat *s = p->keyboard >= 0 ? p->seats[p->keyboard].seat : NULL;
    input_pointer_modes modes = pointer_modes(SDL_GetWindowFlags(window), s ? qa_input_seat_focus(s) : QA_INPUT_GAME,
        s && p->mouse_available, integer(p, INPUT_CVAR_NO_GRAB, 0) != 0);
    if (!input_platform_modes_apply(window, modes.relative, modes.grab, modes.text, error)) return false;
    p->capture = modes.relative;
    return true;
}
static qa_input_platform *allocate_owner(const qa_input_platform_options *o, qa_error *error) {
    if (!o || !o->cvars) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing input platform settings");
        return NULL;
    }
    qa_input_platform *p = calloc(1, sizeof(*p));
    if (!p) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating input platform");
        return NULL;
    }
    p->options = *o;
    bind_cvars(p);
    p->keyboard = -1;
    p->midi_fd = -1;
    p->midi_channel = -1;
    p->source_slot = p->midi_slot = -1;
    p->joystick_instance = -1;
    for (unsigned i = 0; i < 4; ++i) {
        p->seats[i] = (struct seat_route){.platform = p,
                                          .slot = i,
                                          .instance = -1,
                                          .haptic_instance = -1,
                                          .calibration_instance = -1,
                                          .selection = {.kind = QA_CONTROLLER_NONE}};
        qa_haptic_player_init(&p->seats[i].haptic, haptic_sink, &p->seats[i]);
    }
    return p;
}
qa_input_platform *qa_input_platform_create_detached(const qa_input_platform_options *o, qa_error *error) {
    qa_input_platform *p = allocate_owner(o, error);
    if (p && !(p->haptics = qa_haptic_cache_create(error))) {
        qa_input_platform_destroy(p);
        return NULL;
    }
    return p;
}
qa_input_platform *qa_input_platform_create(const qa_input_platform_options *o, qa_error *error) {
    qa_input_platform *p = allocate_owner(o, error);
    if (!p) return NULL;
    p->native_owned = true;
    p->old_controller_events = SDL_GameControllerEventState(SDL_QUERY);
    p->old_joystick_events = SDL_JoystickEventState(SDL_QUERY);
    SDL_GameControllerEventState(SDL_ENABLE);
    SDL_JoystickEventState(SDL_ENABLE);
    if (!qa_input_device_settings_register(o->cvars, error) ||
        !(p->haptics = qa_haptic_cache_create(error)) || !discover(p, -1, error) ||
        !qa_input_platform_restart(p, 0, error)) {
        qa_input_platform_destroy(p);
        return NULL;
    }
    return p;
}
static bool constructor_settings_is(const qa_input_platform_options *options,
    const qa_cvars_edit *edit,const qa_input_platform_settings *desired,qa_error *error) {
    if (!options || !desired || !qa_cvars_edit_returned_is(edit,options->cvars)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Prepared input constructor lost its returned canonical edit");
        return false;
    }
    const char *const names[]={"in_mouse","in_nograb","in_joystick","in_joystickProfile",
        "in_midi","in_joystickSeat","in_midiseat","in_mididevice","in_midichannel",
        "joy_threshold","in_joyBallScale"};
    const qa_cvar_view *canonical[11], *row[11];
    for (unsigned i=0;i<11;++i) {
        canonical[i]=qa_cvars_edit_canonical_record(edit,names[i]);
        row[i]=qa_cvars_edit_find(edit,names[i]);
        if (!canonical[i] || canonical[i]->console_created || !canonical[i]->value ||
            !row[i] || !row[i]->value) {
            qa_error_set(error,QA_ERROR_ARGUMENT,0,"Prepared input constructor lacks its actual declared setting");
            return false;
        }
    }
    if ((strcmp(row[3]->value,"linux") && strcmp(row[3]->value,"windows")) ||
        canonical[2]->latched_value || canonical[3]->latched_value ||
        !isfinite(row[9]->number) || !isfinite(row[10]->number) ||
        desired->mouse_available!=(row[0]->integer!=0) || desired->no_grab!=(row[1]->integer!=0) ||
        desired->joystick_enabled!=(row[2]->integer!=0) ||
        desired->windows_joystick!=(!strcmp(row[3]->value,"windows")) ||
        desired->midi_enabled!=(row[4]->integer!=0) || desired->joystick_seat!=row[5]->integer ||
        desired->midi_seat!=row[6]->integer || desired->midi_device!=row[7]->integer ||
        desired->midi_channel!=row[8]->integer || desired->joystick_threshold!=row[9]->number ||
        desired->joystick_ball_scale!=row[10]->number) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Prepared input constructor differs from its actual canonical rows");
        return false;
    }
    return true;
}
qa_input_platform *qa_input_platform_create_prepared(const qa_input_platform_options *options,
    const qa_cvars_edit *edit,const qa_input_platform_settings *desired,qa_error *error) {
    if (!constructor_settings_is(options,edit,desired,error)) return NULL;
    qa_input_platform *p=allocate_owner(options,error);
    if (!p) return NULL;
    p->constructor_edit=edit; p->constructor_settings=desired;
    bind_cvars(p);
    p->native_owned=true;
    p->old_controller_events=SDL_GameControllerEventState(SDL_QUERY);
    p->old_joystick_events=SDL_JoystickEventState(SDL_QUERY);
    SDL_GameControllerEventState(SDL_ENABLE); SDL_JoystickEventState(SDL_ENABLE);
    bool ok=(p->haptics=qa_haptic_cache_create(error)) && discover(p,-1,error) &&
        constructor_settings_is(options,edit,desired,error) &&
        qa_input_platform_restart(p,0,error) && constructor_settings_is(options,edit,desired,error);
    p->constructor_edit=NULL; p->constructor_settings=NULL;
    if (!ok) { qa_input_platform_destroy(p); return NULL; }
    return p;
}
static bool release_all(qa_input_platform *p, double time, qa_error *error) {
    bool ok = true;
    for (unsigned i = 0; i < 4; ++i) {
        if (p->seats[i].seat && !qa_input_seat_release(p->seats[i].seat, time, error))
            ok = false;
        if (!qa_haptic_stop(&p->seats[i].haptic, error))
            ok = false;
    }
    if (!qa_source_joystick_release(&p->source, time, source_key, p, error))
        ok = false;
    if (!midi_release(p, time, error))
        ok = false;
    memset(p->keys, 0, sizeof(p->keys));
    return ok;
}
void qa_input_platform_destroy(qa_input_platform *p) {
    /* A failed checked abort retains its real endpoints and their owner. */
    if (!p || p->settings_ticket)
        return;
    qa_error ignored = {0};
    if (!input_platform_restore_abort_pending(p, &ignored)) return;
    if (p->native_owned) {
        (void)qa_input_platform_window(p, NULL, p->now, &ignored);
        (void)release_all(p, p->now, &ignored);
        if (p->midi_fd >= 0)
            close(p->midi_fd);
        if (p->joystick)
            SDL_JoystickClose(p->joystick);
        for (size_t i = 0; i < p->device_count; ++i) {
            (void)stop_device(p, p->devices[i].info.instance, &ignored);
            SDL_GameControllerClose(p->devices[i].handle);
        }
        SDL_GameControllerEventState(p->old_controller_events);
        SDL_JoystickEventState(p->old_joystick_events);
    } else {
        for (unsigned i = 0; i < 4; ++i)
            qa_haptic_pattern_release(p->seats[i].haptic.pattern);
    }
    for (unsigned i = 0; i < 4; ++i)
        free((void *)p->seats[i].selection.serial);
    qa_haptic_cache_destroy(p->haptics);
    free(p->midi_devices);
    free(p->devices);
    free(p);
}
static bool valid_selection(const qa_controller_selection *s) {
    if (s->kind == QA_CONTROLLER_AUTO || s->kind == QA_CONTROLLER_NONE)
        return true;
    if (s->kind != QA_CONTROLLER_GUID && s->kind != QA_CONTROLLER_SERIAL)
        return false;
    for (unsigned i = 0; i < 32; ++i) {
        unsigned c = (unsigned char)s->guid[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return s->guid[32] == 0 && (s->kind != QA_CONTROLLER_SERIAL || (s->serial && *s->serial));
}
static bool copy_routes(qa_input_seat *const seats[4],
                        const qa_controller_selection selections[4], int keyboard,
                        double time, qa_controller_selection copied[4], qa_error *error) {
    if (!seats || !selections || keyboard < -1 || keyboard >= 4 ||
        (keyboard >= 0 && !seats[keyboard]) || !isfinite(time) || time < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid local input routes");
        return false;
    }
    for (unsigned i = 0; i < 4; ++i) {
        if (!valid_selection(&selections[i]))
            goto invalid;
        for (unsigned j = 0; j < i; ++j)
            if (seats[i] && seats[j]) {
                qa_command_context a = qa_input_seat_context(seats[i]),
                                   b = qa_input_seat_context(seats[j]);
                if (seats[i] == seats[j] || (a.session == b.session && a.seat == b.seat))
                    goto invalid;
            }
        copied[i] = selections[i];
        copied[i].serial = NULL;
        if (copied[i].kind == QA_CONTROLLER_GUID || copied[i].kind == QA_CONTROLLER_SERIAL)
            for (unsigned j = 0; j < 32; ++j)
                if (copied[i].guid[j] >= 'A' && copied[i].guid[j] <= 'F')
                    copied[i].guid[j] = (char)(copied[i].guid[j] + 32);
        if (selections[i].kind == QA_CONTROLLER_SERIAL) {
            copied[i].serial = strdup(selections[i].serial);
            if (!copied[i].serial) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "Copying controller selection");
                goto fail;
            }
        }
    }
    return true;
invalid:
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid or duplicate input seat selection");
fail:
    for (unsigned i = 0; i < 4; ++i)
        free((void *)copied[i].serial);
    return false;
}
bool input_platform_fresh_routes(qa_input_platform *p, qa_input_seat *const seats[4],
    const qa_controller_selection selections[4], int keyboard, double time, qa_error *error) {
    qa_controller_selection copied[4] = {0};
    if (!copy_routes(seats, selections, keyboard, time, copied, error)) return false;
    bool success = qa_input_device_settings_register(p->options.cvars, error) &&
        qa_cvars_apply_latched(p->options.cvars, "in_joystick", error) &&
        qa_cvars_apply_latched(p->options.cvars, "in_joystickProfile", error);
    bind_cvars(p);
    const qa_cvar_view *profile = read_cvar(p, INPUT_CVAR_JOYSTICK_PROFILE);
    if (success && (!profile || (strcmp(profile->value, "linux") && strcmp(profile->value, "windows")))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "in_joystickProfile must be linux or windows");
        success = false;
    }
    if (!success) {
        for (unsigned i = 0; i < 4; ++i) free((void *)copied[i].serial);
        return false;
    }
    for (unsigned i = 0; i < 4; ++i) {
        free((void *)p->seats[i].selection.serial);
        p->seats[i].selection = copied[i];
        p->seats[i].seat = seats[i];
    }
    p->keyboard = keyboard;
    int source = integer(p, INPUT_CVAR_JOYSTICK_SEAT, 1), midi = integer(p, INPUT_CVAR_MIDI_SEAT, 1);
    p->source_slot = source >= 1 && source <= 4 && seats[source - 1] ? source - 1 : -1;
    p->midi_slot = midi >= 1 && midi <= 4 && seats[midi - 1] ? midi - 1 : -1;
    p->windows_joystick = !strcmp(profile->value, "windows");
    p->mouse_available = variable(p, INPUT_CVAR_MOUSE, 1) != 0;
    p->joystick_enabled=integer(p,INPUT_CVAR_JOYSTICK,0)!=0;
    p->midi_enabled=variable(p,INPUT_CVAR_MIDI,0)!=0;
    p->requested_midi_device=integer(p,INPUT_CVAR_MIDI_DEVICE,0);
    p->midi_channel = integer(p, INPUT_CVAR_MIDI_CHANNEL, 1);
    p->now = time;
    p->native_startup = INPUT_NATIVE_PENDING;
    return true;
}
bool qa_input_platform_routes(qa_input_platform *p, qa_input_seat *const seats[4],
                              const qa_controller_selection selections[4], int keyboard,
                              double time, qa_error *error) {
    if (!native_owner(p, error)) return false;
    qa_controller_selection copied[4] = {0};
    if (!copy_routes(seats, selections, keyboard, time, copied, error)) return false;
    int32_t retained[4] = {-1,-1,-1,-1};
    for (unsigned i = 0; i < 4; ++i)
        if ((!p->seats[i].seat || p->seats[i].seat == seats[i]) &&
            same_selection(&p->seats[i].selection,&copied[i]))
            retained[i] = p->seats[i].instance;
    bool ok = release_all(p, time, error);
    for (unsigned i = 0; i < 4; ++i) {
        if (!stop_device(p, p->seats[i].instance, error))
            ok = false;
        free((void *)p->seats[i].selection.serial);
        p->seats[i].selection = copied[i];
        p->seats[i].seat = seats[i];
        p->seats[i].instance = -1;
        p->seats[i].calibration_sensor = false;
    }
    p->keyboard = keyboard;
    int source = integer(p, INPUT_CVAR_JOYSTICK_SEAT, 1), midi = integer(p, INPUT_CVAR_MIDI_SEAT, 1);
    p->midi_slot = midi >= 1 && midi <= 4 && seats[midi - 1] ? midi - 1 : -1;
    if (!resolve(p, time, retained, error))
        ok = false;
    p->source_slot = source_route(p->seats, source, p->joystick_instance);
    if (!qa_input_platform_sync_focus(p, error))
        ok = false;
    return ok;
}
bool qa_input_platform_routes_reindex(qa_input_platform *p,const int old_slots[4],
    unsigned count,int keyboard,double time,qa_error *error) {
    if (!native_owner(p,error)) return false;
    if (!old_slots || !count || count>4 || keyboard < -1 || keyboard >= (int)count ||
        !isfinite(time) || time<0) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid dense input route mapping"); return false;
    }
    unsigned retained=0;
    for (unsigned i=0;i<count;++i) {
        int old=old_slots[i];
        if (old < -1 || old >= 4 || (old>=0 &&
            (!p->seats[old].seat || (retained & (1u<<(unsigned)old))))) {
            qa_error_set(error,QA_ERROR_ARGUMENT,0,"Input route mapping repeats or loses an actual player");
            return false;
        }
        if (old>=0) retained |= 1u<<(unsigned)old;
    }
    if (!release_all(p,time,error)) return false;
    for (unsigned i=0;i<4;++i)
        if (!stop_device(p,p->seats[i].instance,error)) return false;
    qa_controller_selection selections[4]; int32_t instances[4]; qa_input_seat *inputs[4];
    for (unsigned i=0;i<4;++i) {
        selections[i]=p->seats[i].selection; instances[i]=p->seats[i].instance;
        inputs[i]=p->seats[i].seat;
    }
    for (unsigned i=0;i<4;++i) {
        int old=i<count?old_slots[i]:-1;
        p->seats[i].selection=old>=0?selections[old]:
            (qa_controller_selection){.kind=i<count?QA_CONTROLLER_AUTO:QA_CONTROLLER_NONE};
        p->seats[i].instance=old>=0?instances[old]:-1;
        p->seats[i].seat=old==(int)i?inputs[i]:NULL;
        p->seats[i].calibration_sensor=false;
    }
    for (unsigned i=0;i<4;++i)
        if (!(retained & (1u<<i))) free((void *)selections[i].serial);
    memset(p->keys,0,sizeof(p->keys)); p->keyboard=keyboard;
    p->source_slot=-1; p->midi_slot=-1;
    return qa_input_platform_sync_focus(p,error);
}
bool qa_input_platform_retain(qa_input_platform *p, unsigned mask, int keyboard, double time,
                              qa_error *error) {
    if (!native_owner(p, error)) return false;
    if (mask > 15 || keyboard < -1 || keyboard >= 4 ||
        (keyboard >= 0 && (!(mask & (1u << (unsigned)keyboard)) || !p->seats[keyboard].seat))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid retained input seats");
        return false;
    }
    bool ok = true;
    for (unsigned i = 0; i < 4; ++i)
        if (!(mask & (1u << i)) && p->seats[i].seat) {
            if (!qa_input_seat_release(p->seats[i].seat, time, error))
                ok = false;
            if (!qa_haptic_stop(&p->seats[i].haptic, error))
                ok = false;
            if (!stop_device(p, p->seats[i].instance, error))
                ok = false;
            p->seats[i].seat = NULL;
            p->seats[i].instance = -1;
            p->seats[i].calibration_sensor = false;
        }
    /* Do not resolve: retained slots must not inherit removed players' pads. */
    if (!qa_input_platform_keyboard(p, keyboard, time, error))
        ok = false;
    return ok;
}
bool qa_input_platform_keyboard(qa_input_platform *p, int slot, double time, qa_error *error) {
    if (!native_owner(p, error)) return false;
    if (slot < -1 || slot >= 4 || (slot >= 0 && !p->seats[slot].seat)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid keyboard seat");
        return false;
    }
    bool ok = true;
    if (p->keyboard != slot) {
        if (p->keyboard >= 0 && p->seats[p->keyboard].seat)
            ok = qa_input_seat_release(p->seats[p->keyboard].seat, time, error);
        memset(p->keys, 0, sizeof(p->keys));
        p->keyboard = slot;
    }
    return qa_input_platform_sync_focus(p, error) && ok;
}
bool qa_input_platform_window(qa_input_platform *p, const qa_display *display, double time,
                              qa_error *error) {
    if (!native_owner(p, error)) return false;
    qa_display_info info = {0};
    if (display && !qa_display_info_get(display, &info, error))
        return false;
    if (p->window == info.window_id)
        return qa_input_platform_sync_focus(p, error);
    bool ok = release_all(p, time, error);
    if (p->window) {
        SDL_Window *old = SDL_GetWindowFromID(p->window);
        if (old)
            SDL_SetWindowGrab(old, p->old_grab ? SDL_TRUE : SDL_FALSE);
        if (SDL_SetRelativeMouseMode(p->old_relative ? SDL_TRUE : SDL_FALSE) < 0)
            ok = failed(error, "Restoring relative mouse mode");
        SDL_ShowCursor(p->old_cursor);
        if (p->old_text)
            SDL_StartTextInput();
        else
            SDL_StopTextInput();
    }
    p->window = info.window_id;
    p->capture = false;
    if (p->window) {
        SDL_Window *window = SDL_GetWindowFromID(p->window);
        p->old_grab = SDL_GetWindowGrab(window) == SDL_TRUE;
        p->old_relative = SDL_GetRelativeMouseMode() == SDL_TRUE;
        p->old_text = SDL_IsTextInputActive() == SDL_TRUE;
        p->old_cursor = SDL_ShowCursor(SDL_QUERY);
        p->capture = p->old_relative;
        for (unsigned i = 0; i < 4; ++i)
            if (p->seats[i].seat) {
                qa_input_event event = {
                    .kind = QA_INPUT_EVENT_FOCUS, .time_ms = time, .down = info.focused};
                if (!qa_input_seat_event(p->seats[i].seat, &event, NULL, error))
                    ok = false;
            }
    }
    return qa_input_platform_sync_focus(p, error) && ok;
}
bool qa_input_platform_routes_prepared(qa_input_platform *p,qa_input_seat *const seats[4],
    const qa_controller_selection selections[4],int keyboard,double time,
    const qa_cvars_edit *edit,const qa_input_platform_settings *desired,qa_error *error) {
    if (!p || p->constructor_edit || p->window || p->keyboard!=-1) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Prepared input routes require their unattached fresh native owner");
        return false;
    }
    if (!constructor_settings_is(&p->options,edit,desired,error)) return false;
    for (unsigned i=0;i<4;++i) if (p->seats[i].seat ||
        (seats && seats[i] && qa_input_seat_has_held(seats[i]))) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Prepared initial input routes already have physical history");
        return false;
    }
    p->constructor_edit=edit; p->constructor_settings=desired;
    bind_cvars(p);
    bool ok=qa_input_platform_routes(p,seats,selections,keyboard,time,error) &&
        constructor_settings_is(&p->options,edit,desired,error);
    p->constructor_edit=NULL; p->constructor_settings=NULL;
    return ok;
}
bool qa_input_platform_window_prepared(qa_input_platform *p,const qa_display *display,double time,
    const qa_cvars_edit *edit,const qa_input_platform_settings *desired,qa_error *error) {
    if (!p || !display || p->constructor_edit || p->window) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Prepared input window requires its unattached fresh native owner");
        return false;
    }
    if (!constructor_settings_is(&p->options,edit,desired,error)) return false;
    for (unsigned i=0;i<4;++i) if (p->seats[i].seat && qa_input_seat_has_held(p->seats[i].seat)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Prepared initial input window already has held physical input");
        return false;
    }
    p->constructor_edit=edit; p->constructor_settings=desired;
    bind_cvars(p);
    bool ok=qa_input_platform_window(p,display,time,error) &&
        constructor_settings_is(&p->options,edit,desired,error);
    p->constructor_edit=NULL; p->constructor_settings=NULL;
    return ok;
}
static bool finish_calibration(qa_input_platform *p, qa_error *error) {
    bool ok = true;
    for (unsigned i = 0; i < 4; ++i) {
        struct seat_route *r = &p->seats[i];
        if (!r->calibration_sensor)
            continue;
        if (r->seat && qa_input_seat_gamepad(r->seat)->calibrating)
            continue;
        r->calibration_sensor = false;
        if (r->seat && qa_input_seat_gamepad_tuning(r->seat)->gyro_enabled)
            continue;
        struct device *calibration = device(p, r->calibration_instance);
        if (calibration && SDL_GameControllerGetAttached(calibration->handle) &&
            !qa_input_platform_sensor(p, r->calibration_instance, SDL_SENSOR_GYRO, false, error))
            ok = false;
        r->calibration_instance = -1;
    }
    return ok;
}
static qa_input_pair position(qa_input_platform *p, int x, int y) {
    SDL_Window *window = SDL_GetWindowFromID(p->window);
    int w = 0, h = 0, dw = 0, dh = 0;
    if (window) {
        SDL_GetWindowSize(window, &w, &h);
        uint32_t render_width=0,render_height=0;
        if (p->options.render_size &&
            p->options.render_size(p->options.user,&render_width,&render_height)) {
            dw=(int)render_width; dh=(int)render_height;
        } else if (SDL_GetWindowFlags(window) & SDL_WINDOW_OPENGL)
            SDL_GL_GetDrawableSize(window, &dw, &dh);
        else {
            SDL_Renderer *renderer = SDL_GetRenderer(window);
            if (!renderer || SDL_GetRendererOutputSize(renderer, &dw, &dh) < 0) {
                dw = w;
                dh = h;
            }
        }
    }
    return (qa_input_pair){w > 0 ? (float)x * (float)dw / (float)w : (float)x,
                           h > 0 ? (float)y * (float)dh / (float)h : (float)y};
}
static bool disconnect(qa_input_platform *p, int32_t instance, double time, qa_error *error) {
    bool ok = true;
    struct seat_route *r = route_for(p, instance);
    if (r) {
        if (!qa_haptic_stop(&r->haptic, error))
            ok = false;
        if (!qa_input_seat_release_device(r->seat, instance, time, error))
            ok = false;
        r->calibration_sensor = false;
    }
    for (size_t i = 0; i < p->device_count; ++i)
        if (p->devices[i].info.instance == instance) {
            SDL_GameControllerClose(p->devices[i].handle);
            memmove(p->devices + i, p->devices + i + 1,
                    (p->device_count - i - 1) * sizeof(*p->devices));
            --p->device_count;
            break;
        }
    if (p->options.device_changed)
        p->options.device_changed(p->options.user, instance, false);
    if (!resolve(p, time, NULL, error))
        ok = false;
    return ok;
}
static bool window_focus(qa_input_platform *p, bool focused, double time, qa_error *error) {
    bool ok = true;
    if (!focused && !release_all(p, time, error)) ok = false;
    for (unsigned i = 0; i < 4; ++i)
        if (p->seats[i].seat) {
            qa_input_event translated = {
                .kind = QA_INPUT_EVENT_FOCUS, .time_ms = time, .down = focused};
            if (!qa_input_seat_event(p->seats[i].seat, &translated, NULL, error)) ok = false;
        }
    return qa_input_platform_sync_focus(p, error) && finish_calibration(p, error) && ok;
}
static bool native_event(qa_input_platform *p, const SDL_Event *event, double time,
    bool *handled, qa_error *error) {
    if (handled) *handled = true;
    switch (event->type) {
    case SDL_CONTROLLERDEVICEADDED:
        return discover(p, event->cdevice.which, error) && resolve(p, time, NULL, error);
    case SDL_CONTROLLERDEVICEREMOVED:
        return disconnect(p, event->cdevice.which, time, error);
    case SDL_CONTROLLERDEVICEREMAPPED: {
        struct device *d = device(p, event->cdevice.which);
        if (d)
            describe(d);
        return resolve(p, time, NULL, error);
    }
    case SDL_CONTROLLERAXISMOTION:
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
    case SDL_CONTROLLERSENSORUPDATE:
    case SDL_CONTROLLERTOUCHPADDOWN:
    case SDL_CONTROLLERTOUCHPADMOTION:
    case SDL_CONTROLLERTOUCHPADUP: {
        int32_t instance = event->caxis.which;
        struct seat_route *r = route_for(p, instance);
        if (instance == p->joystick_instance) {
            if (event->type == SDL_CONTROLLERSENSORUPDATE && p->source_slot >= 0)
                r = &p->seats[p->source_slot];
            else {
                if (handled)
                    *handled = false;
                return true;
            }
        }
        if (!r || !r->seat) {
            if (handled)
                *handled = false;
            return true;
        }
        qa_input_event translated = {.time_ms = time, .input = {.device = instance}};
        if (event->type == SDL_CONTROLLERAXISMOTION) {
            if (event->caxis.axis >= QA_AXIS_COUNT)
                return true;
            translated.kind = QA_INPUT_EVENT_AXIS;
            translated.input.kind = QA_PHYSICAL_AXIS;
            translated.input.code = event->caxis.axis;
            translated.value = qa_controller_axis_normalize((qa_controller_axis)event->caxis.axis,
                                                            event->caxis.value);
        } else if (event->type == SDL_CONTROLLERBUTTONDOWN ||
                   event->type == SDL_CONTROLLERBUTTONUP) {
            translated.kind = QA_INPUT_EVENT_BUTTON;
            translated.input.kind = QA_PHYSICAL_BUTTON;
            translated.input.code = event->cbutton.button;
            translated.down = event->cbutton.state == SDL_PRESSED;
        } else if (event->type == SDL_CONTROLLERSENSORUPDATE) {
            if (event->csensor.sensor != SDL_SENSOR_GYRO) {
                if (handled)
                    *handled = false;
                return true;
            }
            if (!qa_input_seat_focused(r->seat))
                return true;
            double sensor_time = (double)event->csensor.timestamp;
#if SDL_VERSION_ATLEAST(2, 26, 0)
            if (event->csensor.timestamp_us)
                sensor_time = (double)event->csensor.timestamp_us / 1000;
#endif
            if (!qa_gamepad_gyro(
                    qa_input_seat_gamepad(r->seat),
                    qa_v3(event->csensor.data[0], event->csensor.data[1], event->csensor.data[2]),
                    sensor_time, qa_input_seat_focus(r->seat) == QA_INPUT_GAME, error))
                return false;
            return finish_calibration(p, error);
        } else {
            translated.kind = QA_INPUT_EVENT_TOUCH;
            translated.position = (qa_input_pair){event->ctouchpad.x, event->ctouchpad.y};
            translated.value = event->ctouchpad.pressure;
            translated.touchpad = event->ctouchpad.touchpad;
            translated.finger = event->ctouchpad.finger;
            translated.down = event->type != SDL_CONTROLLERTOUCHPADUP;
        }
        return qa_input_seat_event(r->seat, &translated, NULL, error);
    }
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        if (event->jbutton.which == p->joystick_instance)
            return qa_source_joystick_button(&p->source, event->jbutton.button,
                                             event->jbutton.state == SDL_PRESSED,
                                             p->windows_joystick, source_key, p, error);
        if (handled)
            *handled = false;
        return true;
    case SDL_JOYAXISMOTION:
        if (event->jaxis.which == p->joystick_instance) {
            if (event->jaxis.axis < 16)
                p->source.axes[event->jaxis.axis] = event->jaxis.value;
            return true;
        }
        if (handled)
            *handled = false;
        return true;
    case SDL_JOYHATMOTION:
        if (event->jhat.which == p->joystick_instance) {
            if (!event->jhat.hat)
                p->source.hat = event->jhat.value;
            return true;
        }
        if (handled)
            *handled = false;
        return true;
    case SDL_JOYDEVICEREMOVED:
        if (event->jdevice.which == p->joystick_instance) {
            bool ok = source_device_release(p, time, error);
            SDL_JoystickClose(p->joystick);
            p->joystick = NULL;
            p->joystick_instance = -1;
            p->joystick_rumble = (input_motor_output){0};
            report(p, "SDL source joystick disconnected.\n");
            return ok;
        }
        if (handled)
            *handled = false;
        return true;
    default:
        break;
    }
    uint32_t window = 0;
    switch (event->type) {
    case SDL_WINDOWEVENT:
        window = event->window.windowID;
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        window = event->key.windowID;
        break;
    case SDL_TEXTINPUT:
        window = event->text.windowID;
        break;
    case SDL_MOUSEMOTION:
        window = event->motion.windowID;
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        window = event->button.windowID;
        break;
    case SDL_MOUSEWHEEL:
        window = event->wheel.windowID;
        break;
    default:
        if (handled)
            *handled = false;
        return true;
    }
    if (!p->window || window != p->window) {
        if (handled)
            *handled = false;
        return true;
    }
    if (event->type == SDL_WINDOWEVENT) {
        if (event->window.event != SDL_WINDOWEVENT_FOCUS_GAINED &&
            event->window.event != SDL_WINDOWEVENT_FOCUS_LOST) {
            if (handled)
                *handled = false;
            return true;
        }
        return window_focus(p, event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED, time, error);
    }
    if (p->keyboard < 0 || !p->seats[p->keyboard].seat) {
        if (handled)
            *handled = false;
        return true;
    }
    qa_input_seat *seat = p->seats[p->keyboard].seat;
    qa_input_event translated = {.time_ms = time};
    switch (event->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        int scancode = (int)event->key.keysym.scancode;
        if (scancode < 0 || scancode >= SDL_NUM_SCANCODES)
            return true;
        int code = p->keys[scancode]
                       ? p->keys[scancode]
                       : qa_input_sdl_key(event->key.keysym.sym, event->key.keysym.mod);
        if (!code)
            return true;
        translated.kind = QA_INPUT_EVENT_KEY;
        translated.down = event->type == SDL_KEYDOWN;
        translated.repeat = event->key.repeat != 0;
        translated.input = (qa_physical_input){.kind = QA_PHYSICAL_KEY, .code = (uint32_t)code};
        p->keys[scancode] = translated.down ? code : 0;
        break;
    }
    case SDL_TEXTINPUT:
        translated.kind = QA_INPUT_EVENT_TEXT;
        translated.text = event->text.text;
        break;
    case SDL_MOUSEMOTION:
        translated.kind = QA_INPUT_EVENT_MOUSE;
        translated.position = position(p, event->motion.x, event->motion.y);
        translated.delta = (qa_input_pair){(float)event->motion.xrel, (float)event->motion.yrel};
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        if (qa_input_seat_focus(seat) != QA_INPUT_GAME) {
            qa_input_event motion = {.kind = QA_INPUT_EVENT_MOUSE,
                                     .time_ms = time,
                                     .position = position(p, event->button.x, event->button.y)};
            if (!qa_input_seat_event(seat, &motion, NULL, error))
                return false;
        }
        translated.kind = QA_INPUT_EVENT_BUTTON;
        translated.input =
            (qa_physical_input){.kind = QA_PHYSICAL_MOUSE, .code = event->button.button};
        translated.down = event->type == SDL_MOUSEBUTTONDOWN;
        break;
    case SDL_MOUSEWHEEL: {
        float sign = event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
        translated.kind = QA_INPUT_EVENT_WHEEL;
#if SDL_VERSION_ATLEAST(2, 0, 18)
        translated.delta =
            (qa_input_pair){event->wheel.preciseX * sign, event->wheel.preciseY * sign};
#else
        translated.delta =
            (qa_input_pair){(float)event->wheel.x * sign, (float)event->wheel.y * sign};
#endif
        break;
    }
    default:
        return true;
    }
    return qa_input_seat_event(seat, &translated, NULL, error) && qa_input_platform_sync_focus(p, error);
}
static int midi_compare(const void *left, const void *right) {
    const qa_midi_device *a = left, *b = right;
    return a->card != b->card       ? (a->card > b->card ? 1 : -1)
           : a->device != b->device ? (a->device > b->device ? 1 : -1)
                                    : 0;
}
static bool midi_coordinates(const char *name, unsigned *card, unsigned *number) {
    if (strncmp(name, "midiC", 5) != 0)
        return false;
    const unsigned char *p = (const unsigned char *)name + 5;
    unsigned values[2] = {0};
    for (unsigned field = 0; field < 2; ++field) {
        if (*p < '0' || *p > '9')
            return false;
        do {
            unsigned digit = (unsigned)(*p++ - '0');
            if (values[field] > (UINT_MAX - digit) / 10)
                return false;
            values[field] = values[field] * 10 + digit;
        } while (*p >= '0' && *p <= '9');
        if (field == 0 && *p++ != 'D')
            return false;
    }
    if (*p)
        return false;
    *card = values[0];
    *number = values[1];
    return true;
}
bool qa_input_midi_devices(qa_midi_device **out, size_t *count, qa_error *error) {
    if (!out || !count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing MIDI enumeration output");
        return false;
    }
    DIR *directory = opendir("/dev/snd");
    if (!directory) {
        if (errno == ENOENT) {
            *out = NULL;
            *count = 0;
            return true;
        }
        qa_error_set(error, QA_ERROR_IO, 0, "Opening MIDI device directory: %s", strerror(errno));
        return false;
    }
    qa_midi_device *devices = NULL;
    size_t length = 0, capacity = 0;
    struct dirent *entry;
    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (!entry)
            break;
        unsigned card, number;
        if (!midi_coordinates(entry->d_name, &card, &number))
            continue;
        if (length == capacity) {
            size_t next = capacity ? capacity * 2 : 8;
            if (next > SIZE_MAX / sizeof(*devices)) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "MIDI device list overflow");
                goto fail;
            }
            qa_midi_device *grown = realloc(devices, next * sizeof(*grown));
            if (!grown) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating MIDI device list");
                goto fail;
            }
            devices = grown;
            capacity = next;
        }
        qa_midi_device d = {.card = card, .device = number};
        int n = snprintf(d.path, sizeof(d.path), "/dev/snd/midiC%uD%u", card, number);
        if (n < 0 || (size_t)n >= sizeof(d.path))
            continue;
        (void)snprintf(d.name, sizeof(d.name), "ALSA midiC%uD%u", card, number);
        devices[length++] = d;
    }
    if (errno) {
        qa_error_set(error, QA_ERROR_IO, 0, "Reading MIDI device directory: %s", strerror(errno));
        goto fail;
    }
    closedir(directory);
    if (length > 1)
        qsort(devices, length, sizeof(*devices), midi_compare);
    *out = devices;
    *count = length;
    return true;
fail:
    closedir(directory);
    free(devices);
    return false;
}
static bool midi_capture_start(int fd, bool *pending, uint8_t *byte, uint64_t *reads, qa_error *error) {
    *pending = false; *byte = 0;
    ++*reads;
    ssize_t count = read(fd, byte, 1);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return true;
    if (count == 1) { *pending = true; return true; }
    qa_error_set(error, QA_ERROR_IO, 0, "Starting MIDI input capture: %s",
        count == 0 ? "end of stream" : strerror(errno));
    return false;
}
static bool open_midi(qa_input_platform *p, qa_error *error) {
    if (p->midi_fd >= 0) {
        close(p->midi_fd);
        p->midi_fd = -1;
    }
    p->midi_pending = false; p->midi_byte = 0;
    p->midi_reads = 0;
    free(p->midi_devices);
    p->midi_devices = NULL;
    p->midi_count = 0;
    if (p->constructor_settings?!p->constructor_settings->midi_enabled:variable(p, INPUT_CVAR_MIDI, 0) == 0)
        return true;
    if (!qa_input_midi_devices(&p->midi_devices, &p->midi_count, error))
        return false;
    int index = integer(p, INPUT_CVAR_MIDI_DEVICE, 0);
    if (index < 0 || (size_t)index >= p->midi_count) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "MIDI device index %d is outside %zu devices",
                     index, p->midi_count);
        return false;
    }
    int fd = open(p->midi_devices[index].path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        qa_error_set(error, QA_ERROR_IO, 0, "Opening MIDI input: %s", strerror(errno));
        return false;
    }
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISCHR(st.st_mode)) {
        close(fd);
        qa_error_set(error, QA_ERROR_IO, 0, "MIDI input is not a character device");
        return false;
    }
    if (!midi_capture_start(fd, &p->midi_pending, &p->midi_byte, &p->midi_reads, error)) {
        close(fd); p->midi_byte = 0; p->midi_reads = 0; return false;
    }
    p->midi_fd = fd;
    ++p->midi_generation;
    return true;
}
typedef struct input_output_transition {
    struct device *device;
    SDL_GameController *ready_handle;
    int32_t ready_instance;
    input_motor_output rumble, triggers;
    bool motor_attempted[2];
    bool retire_motors;
    bool sensor_enabled[6], sensor_desired[6], sensor_attempted[6];
} input_output_transition;
struct qa_input_platform_settings_ticket {
    qa_input_platform *platform;
    qa_input_platform_settings desired;
    qa_input_platform_settings_requirements requirements;
    bool previous_joystick_enabled,previous_midi_enabled;
    int previous_midi_device;
    unsigned haptic_routes, calibration_routes;
    struct seat_route routes[4];
    struct seat_route next_routes[4];
    SDL_GameController *next_handles[4];
    struct device *route_devices;
    size_t route_device_count;
    bool selected_routes;
    qa_input_seat *configuration[4];
    bool owns_configuration[4];
    bool gyro_enabled[4], previous_gyro_enabled[4];
    SDL_Window *window;
    uint32_t window_id;
    const qa_display_surface_ticket *surface;
    const qa_display *active_display, *candidate_display;
    SDL_Window *candidate_window;
    uint32_t candidate_window_id;
    bool candidate_grab, window_prepared;
    int keyboard;
    bool focused;
    qa_input_focus focus;
    SDL_Joystick *joystick, *previous_joystick;
    int32_t joystick_instance;
    int midi_fd, previous_midi_fd;
    bool midi_pending;
    uint8_t midi_byte;
    uint64_t midi_reads, midi_generation;
    qa_midi_device *midi_devices;
    size_t midi_count;
    double now;
    input_output_transition *outputs;
    size_t output_count;
    input_motor_output source_rumble;
    bool source_motor_attempted;
    bool owns_joystick, owns_midi, owns_midi_devices;
    bool previous_relative, previous_grab, previous_text, relative, grab, text;
    int previous_cursor;
    bool capture_attempted, prepared, aborting, retiring, terminal, published;
    bool midi_deferred, enter_complete, endpoint_entered;
    uint64_t revision, ready_revision;
    const qa_input_release *ready_release[4];
    qa_input_platform_options ready_options;
    struct device *ready_devices;
    size_t ready_device_count;
    qa_midi_device *ready_midi_devices;
    size_t ready_midi_count;
    uint64_t ready_midi_generation;
    qa_gamepad_tuning ready_tuning[4], ready_configuration_tuning[4];
    qa_command_context ready_context[4], ready_configuration_context[4];
    bool ready_focused[4];
    qa_input_focus ready_focus[4];
    char diagnostic[320];
};
static void settings_changed(qa_input_platform_settings_ticket *t) {
    t->ready_revision = 0;
    if (!++t->revision) ++t->revision;
}
static uint32_t motor_remaining(const input_motor_output *v) {
    uint64_t now = SDL_GetTicks64();
    uint64_t elapsed = now >= v->ticks ? now - v->ticks : 0;
    return elapsed < v->duration ? v->duration - (uint32_t)elapsed : 0;
}
static bool motor_retirement_known(const input_motor_output *v, qa_error *error) {
    if (!v->requested || v->applied) return true;
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
        "Native motor retirement requires a retained successful output request");
    return false;
}
static int32_t settings_controller(const qa_input_platform_settings_ticket *t, unsigned slot) {
    const struct seat_route *r = &t->next_routes[slot];
    if (!r->seat) return -1;
    if (t->requirements.next_source_slot == (int)slot && t->joystick_instance >= 0)
        return t->joystick_instance;
    return r->instance == t->joystick_instance ? -1 : r->instance;
}
static bool settings_controller_changed(const qa_input_platform_settings_ticket *t, unsigned slot) {
    return qa_input_platform_controller(t->platform, slot) != settings_controller(t, slot);
}
static bool settings_bindings_is(const qa_input_platform_settings_ticket *t, unsigned slot) {
    int32_t instance = t->next_routes[slot].instance;
    if (!t->selected_routes || !t->configuration[slot] || instance < 0 ||
        instance == t->routes[slot].instance) return true;
    const qa_input_seat *candidate = t->configuration[slot];
    for (size_t row = 0; row < qa_input_seat_binding_count(candidate); ++row) {
        const qa_input_binding *b = qa_input_seat_binding_at(candidate, row);
        if (!b || (b->input.kind >= QA_PHYSICAL_BUTTON && b->input.device != instance)) return false;
    }
    return true;
}
static bool settings_outputs_prepare(qa_input_platform_settings_ticket *t, qa_error *error) {
    qa_input_platform *p = t->platform;
    for (unsigned slot = 0; slot < 4; ++slot) if (t->gyro_enabled[slot] &&
        (!t->previous_gyro_enabled[slot] ||
         (settings_controller_changed(t, slot) && settings_controller(t, slot) >= 0)) &&
        !device(p, settings_controller(t, slot))) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Requested gyro configuration has no native controller sensor owner");
        return false;
    }
    if (p->device_count) {
        t->outputs = calloc(p->device_count, sizeof(*t->outputs));
        if (!t->outputs) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining native input output transitions");
            return false;
        }
    }
    /* Preflight every retirement before issuing the first native output. */
    for (size_t i = 0; i < p->device_count; ++i) {
        struct device *d = &p->devices[i]; bool affected = false;
        for (unsigned slot = 0; slot < 4; ++slot) if (t->haptic_routes & (1u << slot)) {
            const struct seat_route *r = &t->routes[slot];
            if (d->info.instance == r->haptic_instance ||
                ((t->requirements.controller_routes & (1u << slot)) &&
                 (d->info.instance == r->instance || d->info.instance == t->next_routes[slot].instance))) affected = true;
        }
        if (t->requirements.source_changed &&
            (d->info.instance == p->joystick_instance || d->info.instance == t->joystick_instance)) affected = true;
        bool retire_motors = affected;
        for (unsigned slot = 0; slot < 4; ++slot)
            if (((t->calibration_routes & (1u << slot)) && t->routes[slot].calibration_sensor &&
                    t->routes[slot].calibration_instance == d->info.instance) ||
                ((qa_input_platform_controller(p, slot) == d->info.instance || settings_controller(t, slot) == d->info.instance) &&
                (t->gyro_enabled[slot] != t->previous_gyro_enabled[slot] || settings_controller_changed(t, slot)))) affected = true;
        if (!affected) continue;
        if (SDL_GameControllerGetAttached(d->handle) != SDL_TRUE)
            return failed(error, "Retained controller disconnected during input preparation");
        if (retire_motors && (!motor_retirement_known(&d->rumble, error) ||
            !motor_retirement_known(&d->triggers, error))) return false;
        input_output_transition *v = &t->outputs[t->output_count++];
        v->device = d; v->rumble = d->rumble; v->triggers = d->triggers;
        v->retire_motors = retire_motors;
        for (unsigned slot = 0; slot < 4; ++slot)
            if (settings_controller(t, slot) == d->info.instance && t->gyro_enabled[slot] &&
                (!t->previous_gyro_enabled[slot] || settings_controller_changed(t, slot)) &&
                !d->info.sensors[SDL_SENSOR_GYRO - 1]) {
                qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Prepared controller has no requested gyro sensor");
                return false;
            }
        for (unsigned sensor = 0; sensor < 6; ++sensor) if (d->info.sensors[sensor]) {
            v->sensor_enabled[sensor] = SDL_GameControllerIsSensorEnabled(d->handle,
                (SDL_SensorType)(sensor + 1)) == SDL_TRUE;
            v->sensor_desired[sensor] = v->sensor_enabled[sensor];
        }
        if (d->info.sensors[SDL_SENSOR_GYRO - 1]) {
            bool enabled = false, changed = false;
            for (unsigned slot = 0; slot < 4; ++slot) {
                const struct seat_route *r = &t->routes[slot];
                if (r->seat && settings_controller(t, slot) == d->info.instance) {
                    if (t->gyro_enabled[slot]) enabled = true;
                }
                if ((t->gyro_enabled[slot] != t->previous_gyro_enabled[slot] || settings_controller_changed(t, slot)) &&
                    (qa_input_platform_controller(p, slot) == d->info.instance ||
                        settings_controller(t, slot) == d->info.instance)) changed = true;
                if ((t->calibration_routes & (1u << slot)) && r->calibration_sensor &&
                    r->calibration_instance == d->info.instance) changed = true;
            }
            if (changed) v->sensor_desired[SDL_SENSOR_GYRO - 1] = enabled;
        }
    }
    if (t->requirements.source_changed && p->joystick && !device(p, p->joystick_instance)) {
        t->source_rumble = p->joystick_rumble;
        if (!motor_retirement_known(&t->source_rumble, error)) return false;
    }
    for (size_t i = 0; i < t->output_count; ++i) {
        input_output_transition *v = &t->outputs[i];
        input_motor_output *motors[2] = {&v->rumble, &v->triggers};
        for (unsigned motor = 0; motor < 2; ++motor) {
            const input_motor_output *old = motors[motor];
            if (!v->retire_motors || !old->requested || !(old->low || old->high) || !motor_remaining(old)) continue;
            v->motor_attempted[motor] = true;
            if (!controller_motor(v->device, motor != 0, 0, 0, 0, error)) return false;
        }
        for (unsigned sensor = 0; sensor < 6; ++sensor) {
            if (v->sensor_enabled[sensor] == v->sensor_desired[sensor]) continue;
            v->sensor_attempted[sensor] = true;
            if (!controller_sensor(v->device, (SDL_SensorType)(sensor + 1), v->sensor_desired[sensor], error)) return false;
        }
    }
    if (t->source_rumble.requested && (t->source_rumble.low || t->source_rumble.high) &&
        motor_remaining(&t->source_rumble)) {
        t->source_motor_attempted = true;
        if (!source_motor(p, 0, 0, 0, error)) return false;
    }
    return true;
}
static bool settings_outputs_abort(qa_input_platform_settings_ticket *t, qa_error *error) {
    if (t->source_motor_attempted) {
        uint32_t remaining = motor_remaining(&t->source_rumble);
        if (!source_motor(t->platform, remaining ? t->source_rumble.low : 0,
            remaining ? t->source_rumble.high : 0, remaining, error)) return false;
        t->source_motor_attempted = false;
    }
    for (size_t i = t->output_count; i > 0; --i) {
        input_output_transition *v = &t->outputs[i - 1];
        for (unsigned sensor = 6; sensor > 0; --sensor) if (v->sensor_attempted[sensor - 1]) {
            if (!controller_sensor(v->device, (SDL_SensorType)sensor, v->sensor_enabled[sensor - 1], error)) return false;
            if ((SDL_GameControllerIsSensorEnabled(v->device->handle, (SDL_SensorType)sensor) == SDL_TRUE) !=
                v->sensor_enabled[sensor - 1]) {
                qa_error_set(error, QA_ERROR_IO, 0, "Input sensor abort did not restore its retained native mode");
                return false;
            }
            v->sensor_attempted[sensor - 1] = false;
        }
        for (unsigned motor = 2; motor > 0; --motor) if (v->motor_attempted[motor - 1]) {
            const input_motor_output *old = motor == 2 ? &v->triggers : &v->rumble;
            uint32_t remaining = motor_remaining(old);
            if (!controller_motor(v->device, motor == 2, remaining ? old->low : 0,
                remaining ? old->high : 0, remaining, error)) return false;
            v->motor_attempted[motor - 1] = false;
        }
    }
    return true;
}
static bool settings_window_outputs(qa_input_platform_settings_ticket *t, qa_error *error) {
    qa_input_platform *p = t->platform;
    /* Keep original output snapshots when scalar preparation already touched
     * a device. Window release adds only the actual route's rumble channel. */
    for (size_t i = 0; i < p->device_count; ++i) {
        struct device *d = &p->devices[i]; bool affected = false;
        for (unsigned slot = 0; slot < 4; ++slot)
            if (t->routes[slot].seat && t->routes[slot].haptic_instance == d->info.instance)
                affected = true;
        if (!affected) continue;
        if (SDL_GameControllerGetAttached(d->handle) != SDL_TRUE)
            return failed(error, "Retained window haptic controller disconnected");
        if (!motor_retirement_known(&d->rumble, error)) return false;
        input_output_transition *v = NULL;
        for (size_t row = 0; row < t->output_count; ++row)
            if (t->outputs[row].device == d) v = &t->outputs[row];
        if (!v) {
            v = &t->outputs[t->output_count++];
            v->device = d; v->rumble = d->rumble; v->triggers = d->triggers;
            for (unsigned sensor = 0; sensor < 6; ++sensor) if (d->info.sensors[sensor]) {
                v->sensor_enabled[sensor] = SDL_GameControllerIsSensorEnabled(d->handle,
                    (SDL_SensorType)(sensor + 1)) == SDL_TRUE;
                v->sensor_desired[sensor] = v->sensor_enabled[sensor];
            }
        }
    }
    bool source = false;
    for (unsigned slot = 0; slot < 4; ++slot)
        if (t->routes[slot].seat && t->routes[slot].haptic_instance == p->joystick_instance)
            source = true;
    source = source && p->joystick && !device(p, p->joystick_instance);
    if (source && !t->source_motor_attempted) {
        if (!motor_retirement_known(&p->joystick_rumble, error)) return false;
        t->source_rumble = p->joystick_rumble;
    }
    for (size_t i = 0; i < t->output_count; ++i) {
        input_output_transition *v = &t->outputs[i]; bool affected = false;
        for (unsigned slot = 0; slot < 4; ++slot)
            if (t->routes[slot].seat && t->routes[slot].haptic_instance == v->device->info.instance)
                affected = true;
        const input_motor_output *old = &v->rumble;
        if (!affected || v->motor_attempted[0] || !old->requested ||
            !(old->low || old->high) || !motor_remaining(old)) continue;
        v->motor_attempted[0] = true;
        if (!controller_motor(v->device, false, 0, 0, 0, error)) return false;
    }
    if (source && !t->source_motor_attempted && t->source_rumble.requested &&
        (t->source_rumble.low || t->source_rumble.high) && motor_remaining(&t->source_rumble)) {
        t->source_motor_attempted = true;
        if (!source_motor(p, 0, 0, 0, error)) return false;
    }
    for (unsigned slot = 0; slot < 4; ++slot)
        if (t->routes[slot].seat) t->haptic_routes |= 1u << slot;
    return true;
}
static bool settings_current(const qa_input_platform_settings_ticket *t, qa_error *error) {
    qa_input_platform *p = t ? t->platform : NULL;
    if (!p || t->terminal || p->settings_ticket != t || !p->native_owned ||
        p->native_initializing || p->native_startup != INPUT_NATIVE_READY ||
        p->joystick != t->previous_joystick || p->midi_fd != t->previous_midi_fd ||
        p->joystick_enabled!=t->previous_joystick_enabled ||
        p->midi_enabled!=t->previous_midi_enabled || p->requested_midi_device!=t->previous_midi_device ||
        p->devices != t->route_devices || p->device_count != t->route_device_count ||
        p->window != t->window_id || p->keyboard != t->keyboard ||
        (t->window_id && SDL_GetWindowFromID(t->window_id) != t->window)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input settings ticket lost its actual native owner");
        return false;
    }
    if (t->surface && (qa_display_surface_candidate(t->surface) != t->candidate_display ||
        qa_display_surface_active(t->surface) != t->active_display ||
        SDL_GetWindowFromID(t->candidate_window_id) != t->candidate_window)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input window child lost its retained native surface");
        return false;
    }
    for (unsigned i = 0; i < 4; ++i) {
        const struct seat_route *a = &p->seats[i], *b = &t->routes[i];
        if (a->seat != b->seat || a->instance != b->instance ||
            a->selection.kind != b->selection.kind || a->selection.ordinal != b->selection.ordinal ||
            memcmp(a->selection.guid, b->selection.guid, sizeof(a->selection.guid)) ||
            a->selection.serial != b->selection.serial ||
            a->haptic_instance != b->haptic_instance || a->calibration_instance != b->calibration_instance ||
            a->calibration_sensor != b->calibration_sensor || memcmp(&a->haptic, &b->haptic, sizeof(a->haptic))) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input settings ticket lost its retained physical routes");
            return false;
        }
        if (t->configuration[i] &&
            qa_input_seat_gamepad_tuning(t->configuration[i])->gyro_enabled != t->gyro_enabled[i]) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Prepared input gyro configuration changed");
            return false;
        }
    }
    qa_input_seat *s = t->keyboard >= 0 ? p->seats[t->keyboard].seat : NULL;
    if ((s && qa_input_seat_focused(s)) != t->focused || (s && qa_input_seat_focus(s) != t->focus)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input settings capture lost its retained keyboard focus");
        return false;
    }
    return true;
}
bool input_platform_modes_apply(SDL_Window *window, bool relative,
    bool grab, bool text, qa_error *error) {
    bool success = true;
    if (SDL_GetRelativeMouseMode() != (relative ? SDL_TRUE : SDL_FALSE) &&
        SDL_SetRelativeMouseMode(relative ? SDL_TRUE : SDL_FALSE) < 0)
        success = failed(error, "Preparing input relative mouse capture");
    if (window && SDL_GetWindowGrab(window) != (grab ? SDL_TRUE : SDL_FALSE))
        SDL_SetWindowGrab(window, grab ? SDL_TRUE : SDL_FALSE);
    if (text && !SDL_IsTextInputActive()) SDL_StartTextInput();
    else if (!text && SDL_IsTextInputActive()) SDL_StopTextInput();
    bool focused = window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    int cursor = relative || grab || focused ? SDL_DISABLE : SDL_ENABLE;
    if (SDL_ShowCursor(SDL_QUERY) != cursor) SDL_ShowCursor(cursor);
    return success;
}
static bool settings_open_midi(qa_input_platform_settings_ticket *t, bool enumerate, qa_error *error) {
    t->midi_fd = -1;
    t->midi_pending = false; t->midi_byte = 0;
    t->midi_reads = 0;
    if (enumerate) {
        t->owns_midi_devices = true;
        if (!qa_input_midi_devices(&t->midi_devices, &t->midi_count, error)) return false;
    }
    if (t->desired.midi_device < 0 || (size_t)t->desired.midi_device >= t->midi_count) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "MIDI device index %d is outside %zu devices",
            t->desired.midi_device, t->midi_count);
        return false;
    }
    const char *path = t->midi_devices[t->desired.midi_device].path;
    if (enumerate && t->previous_midi_fd >= 0) {
        struct stat previous, selected;
        if (fstat(t->previous_midi_fd, &previous) < 0 || !S_ISCHR(previous.st_mode)) {
            qa_error_set(error, QA_ERROR_IO, 0, "Active MIDI input lost its character endpoint");
            return false;
        }
        if (lstat(path, &selected) == 0 && S_ISCHR(selected.st_mode) && previous.st_rdev == selected.st_rdev) {
            t->midi_deferred = true; return true;
        }
    }
    int fd = open(path,
        O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        qa_error_set(error, QA_ERROR_IO, 0, "Opening MIDI input: %s", strerror(errno));
        return false;
    }
    t->midi_fd = fd; t->owns_midi = true;
    struct stat status;
    if (fstat(fd, &status) < 0 || !S_ISCHR(status.st_mode)) {
        qa_error_set(error, QA_ERROR_IO, 0, "MIDI input is not a character device");
        return false;
    }
    if (!midi_capture_start(fd, &t->midi_pending, &t->midi_byte, &t->midi_reads, error)) return false;
    t->midi_generation = t->platform->midi_generation + 1;
    return true;
}
static bool settings_midi_warning(qa_input_platform_settings_ticket *t, const qa_error *warning, qa_error *error) {
    if (t->owns_midi) {
        int fd = t->midi_fd; t->midi_fd = -1; t->owns_midi = false;
        t->midi_pending = false; t->midi_byte = 0;
        t->midi_reads = 0;
        if (close(fd) < 0) {
            qa_error_set(error, QA_ERROR_IO, 0, "Closing unqualified MIDI input: %s", strerror(errno));
            return false;
        }
    }
    size_t length = strlen(t->diagnostic);
    snprintf(t->diagnostic + length, sizeof(t->diagnostic) - length, "WARNING: %s\n", warning->message);
    return true;
}
static bool settings_prepare(qa_input_platform *p, const qa_input_platform_settings *desired,
    qa_input_seat *const configuration[4], const qa_controller_selection selections[4],
    double now, bool retry_source, bool retry_midi,
    qa_input_platform_settings_ticket **out, qa_error *error) {
    if (!native_owner(p, error)) return false;
    if (!desired || !configuration || !out || *out || !isfinite(now) || now < 0 ||
        !isfinite(desired->joystick_threshold) || !isfinite(desired->joystick_ball_scale) ||
        p->native_startup != INPUT_NATIVE_READY || p->native_initializing) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input settings require an idle native owner and empty ticket");
        return false;
    }
    for (unsigned slot = 0; slot < 4; ++slot) {
        qa_input_seat *active = p->seats[slot].seat, *candidate = configuration[slot];
        if ((active != NULL) != (candidate != NULL) ||
            (active && active != candidate && !qa_input_seat_configuration_ready(active, candidate, error))) {
            if (!error || error->code == QA_OK)
                qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input settings require the actual physical seat configurations");
            return false;
        }
    }
    qa_input_platform_settings_ticket *t = calloc(1, sizeof(*t));
    if (!t) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining input settings preparation"); return false; }
    qa_controller_selection copied[4] = {0};
    if (selections && !copy_routes(configuration, selections, p->keyboard, now, copied, error)) {
        free(t); return false;
    }
    t->platform = p; t->desired = *desired; t->now = now; t->keyboard = p->keyboard;
    t->revision = 1;
    t->previous_joystick = t->joystick = p->joystick;
    t->previous_joystick_enabled=p->joystick_enabled;
    t->previous_midi_enabled=p->midi_enabled;
    t->previous_midi_device=p->requested_midi_device;
    t->joystick_instance = p->joystick_instance;
    t->previous_midi_fd = t->midi_fd = p->midi_fd;
    t->midi_pending = p->midi_pending; t->midi_byte = p->midi_byte;
    t->midi_reads = p->midi_reads;
    t->midi_generation = p->midi_generation;
    t->midi_devices = p->midi_devices; t->midi_count = p->midi_count;
    t->window_id = p->window; t->window = p->window ? SDL_GetWindowFromID(p->window) : NULL;
    memcpy(t->routes, p->seats, sizeof(t->routes));
    memcpy(t->next_routes, p->seats, sizeof(t->next_routes));
    t->selected_routes = selections != NULL;
    t->route_devices = p->devices; t->route_device_count = p->device_count;
    if (selections) {
        int32_t assignments[4];
        for (unsigned slot = 0; slot < 4; ++slot) t->next_routes[slot].selection = copied[slot];
        resolve_routes(p, t->next_routes, NULL, assignments);
        for (unsigned slot = 0; slot < 4; ++slot) t->next_routes[slot].instance = assignments[slot];
    }
    for (unsigned slot = 0; slot < 4; ++slot) {
        struct device *d = device(p, t->next_routes[slot].instance);
        t->next_handles[slot] = d ? d->handle : NULL;
    }
    for (unsigned slot = 0; slot < 4; ++slot) {
        t->configuration[slot] = configuration[slot];
        t->gyro_enabled[slot] = configuration[slot] && qa_input_seat_gamepad_tuning(configuration[slot])->gyro_enabled;
        t->previous_gyro_enabled[slot] = p->seats[slot].seat &&
            qa_input_seat_gamepad_tuning(p->seats[slot].seat)->gyro_enabled;
    }
    qa_input_seat *keyboard = p->keyboard >= 0 ? p->seats[p->keyboard].seat : NULL;
    t->focused = keyboard && qa_input_seat_focused(keyboard);
    t->focus = keyboard ? qa_input_seat_focus(keyboard) : QA_INPUT_GAME;
    t->previous_relative = SDL_GetRelativeMouseMode() == SDL_TRUE;
    t->previous_grab = t->window && SDL_GetWindowGrab(t->window) == SDL_TRUE;
    t->previous_text = SDL_IsTextInputActive() == SDL_TRUE;
    t->previous_cursor = SDL_ShowCursor(SDL_QUERY);
    p->settings_ticket = t; *out = t;
    if (t->window_id && !t->window) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input settings lost the retained native window");
        return false;
    }
    int source, midi = desired->midi_seat;
    midi = midi >= 1 && midi <= 4 && p->seats[midi - 1].seat ? midi - 1 : -1;
    bool enabled = p->joystick_enabled;
    bool acquire_source = desired->joystick_enabled &&
        (retry_source || desired->restart_requested || !enabled || desired->windows_joystick != p->windows_joystick);
    if (acquire_source) {
        int count = SDL_NumJoysticks();
        if (count < 0) return failed(error, "Enumerating prepared source joystick");
        t->joystick = NULL; t->joystick_instance = -1;
        for (int index = 0; index < count; ++index) {
            t->joystick = SDL_JoystickOpen(index);
            if (t->joystick) break;
        }
        if (t->joystick) {
            t->owns_joystick = true; t->joystick_instance = SDL_JoystickInstanceID(t->joystick);
            if (t->joystick_instance < 0 || SDL_JoystickGetAttached(t->joystick) != SDL_TRUE)
                return failed(error, "Qualifying prepared source joystick");
        } else snprintf(t->diagnostic, sizeof(t->diagnostic), "No source joystick found.\n");
    } else if (!desired->joystick_enabled) {
        t->joystick = NULL; t->joystick_instance = -1;
    }
    source = source_route(t->next_routes, desired->joystick_seat, t->joystick_instance);
    bool midi_enabled = p->midi_enabled;
    bool acquire_midi = desired->midi_enabled &&
        (retry_midi || desired->restart_requested || !midi_enabled || desired->midi_device != p->requested_midi_device);
    if (acquire_midi) {
        t->midi_devices = NULL; t->midi_count = 0;
        qa_error warning = {0};
        if (!settings_open_midi(t, true, &warning) && !settings_midi_warning(t, &warning, error)) return false;
    } else if (!desired->midi_enabled) {
        t->midi_fd = -1; t->midi_devices = NULL; t->midi_count = 0;
        t->midi_pending = false; t->midi_byte = 0;
        t->midi_reads = 0;
        t->owns_midi_devices = true;
    }
    qa_input_platform_settings_requirements *r = &t->requirements;
    *r = (qa_input_platform_settings_requirements){
        .source_changed = desired->restart_requested || t->joystick_instance != p->joystick_instance || source != p->source_slot ||
            desired->windows_joystick != p->windows_joystick || desired->joystick_enabled != enabled,
        .midi_changed = desired->restart_requested || t->midi_fd != p->midi_fd || midi != p->midi_slot || desired->midi_channel != p->midi_channel,
        .source_slot = p->source_slot, .next_source_slot = source,
        .midi_slot = p->midi_slot, .next_midi_slot = midi,
        .joystick_instance = p->joystick_instance, .next_joystick_instance = t->joystick_instance};
    if (selections) for (unsigned slot = 0; slot < 4; ++slot) {
        const struct seat_route *a = &t->routes[slot], *b = &t->next_routes[slot];
        if (a->seat && (!same_selection(&a->selection,&b->selection) || a->instance != b->instance))
            r->controller_routes |= 1u << slot;
        if (a->seat && b->instance >= 0 && a->instance != b->instance) {
            if (t->configuration[slot] == a->seat) {
                qa_input_seat *copy = NULL;
                if (!qa_input_seat_configuration_clone(a->seat, &copy, error)) return false;
                t->configuration[slot] = copy; t->owns_configuration[slot] = true;
            }
            if (!qa_input_seat_remap_controller(t->configuration[slot], b->instance, error)) return false;
        }
    }
    if (r->source_changed) for (unsigned i = 0; i < 4; ++i)
        if (p->seats[i].seat && (desired->restart_requested || (p->seats[i].instance >= 0 &&
            (p->seats[i].instance == p->joystick_instance || p->seats[i].instance == t->joystick_instance))))
            r->controller_routes |= 1u << i;
    t->haptic_routes = t->calibration_routes = r->controller_routes;
    if (r->source_changed && p->source_slot >= 0) {
        t->haptic_routes |= 1u << p->source_slot;
        t->calibration_routes |= 1u << p->source_slot;
    }
    if (r->source_changed && source >= 0) t->calibration_routes |= 1u << source;
    if (!settings_outputs_prepare(t, error)) return false;
    if (t->window) {
        input_pointer_modes modes = pointer_modes(SDL_GetWindowFlags(t->window), t->focus,
            keyboard && desired->mouse_available, desired->no_grab);
        t->relative = modes.relative; t->grab = modes.grab; t->text = modes.text;
        t->capture_attempted = true;
        if (!input_platform_modes_apply(t->window, t->relative, t->grab, t->text, error)) return false;
    } else { t->relative = t->previous_relative; t->grab = t->previous_grab; t->text = t->previous_text; }
    t->prepared = true; return settings_current(t, error);
}
bool qa_input_platform_settings_prepare(qa_input_platform *p, const qa_input_platform_settings *desired,
    qa_input_seat *const configuration[4], double now, qa_input_platform_settings_ticket **out, qa_error *error) {
    return settings_prepare(p, desired, configuration, NULL, now, false, false, out, error);
}
bool qa_input_platform_settings_prepare_selected(qa_input_platform *p, const qa_input_platform_settings *desired,
    qa_input_seat *const configuration[4], const qa_controller_selection selections[4], double now,
    qa_input_platform_settings_ticket **out, qa_error *error) {
    if (!selections) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Selected input settings require all physical controller selectors");
        return false;
    }
    return settings_prepare(p, desired, configuration, selections, now, false, false, out, error);
}
bool qa_input_platform_reconnect_prepare(qa_input_platform *p, double now,
    qa_input_platform_settings_ticket **out, qa_error *error) {
    if (!native_owner(p, error)) return false;
    if (!out || *out || !isfinite(now) || now < 0 || !isfinite(now + 1000)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input reconnect requires a finite clock and empty ticket");
        return false;
    }
    if (p->native_startup != INPUT_NATIVE_READY || p->native_initializing || now < p->retry_at) return true;
    p->retry_at = now + 1000;
    bool source = integer(p, INPUT_CVAR_JOYSTICK, 0) != 0 && !p->joystick;
    bool midi = variable(p, INPUT_CVAR_MIDI, 0) != 0 && p->midi_fd < 0;
    if (!source && !midi) return true;
    qa_input_platform_settings desired = {
        .mouse_available = p->mouse_available, .no_grab = variable(p, INPUT_CVAR_NO_GRAB, 0) != 0,
        .joystick_enabled = integer(p, INPUT_CVAR_JOYSTICK, 0) != 0, .windows_joystick = p->windows_joystick,
        .midi_enabled = variable(p, INPUT_CVAR_MIDI, 0) != 0,
        .joystick_seat = integer(p, INPUT_CVAR_JOYSTICK_SEAT, 1), .midi_seat = integer(p, INPUT_CVAR_MIDI_SEAT, 1),
        .midi_device = integer(p, INPUT_CVAR_MIDI_DEVICE, 0), .midi_channel = integer(p, INPUT_CVAR_MIDI_CHANNEL, 1),
        .joystick_threshold = variable(p, INPUT_CVAR_JOYSTICK_THRESHOLD, .15f),
        .joystick_ball_scale = variable(p, INPUT_CVAR_JOYSTICK_BALL_SCALE, .02f)};
    qa_input_seat *configuration[4];
    for (unsigned slot = 0; slot < 4; ++slot) configuration[slot] = p->seats[slot].seat;
    return settings_prepare(p, &desired, configuration, NULL, now, source, midi, out, error);
}
bool qa_input_platform_settings_requirements_read(const qa_input_platform_settings_ticket *t,
    qa_input_platform_settings_requirements *out, qa_error *error) {
    if (!out || !t || !t->prepared || !settings_current(t, error)) return false;
    *out = t->requirements; return true;
}
bool qa_input_platform_settings_idle(const qa_input_platform *p) {
    return p && !p->settings_ticket && !p->constructor_edit && !p->restore_abort;
}
bool qa_input_platform_settings_retained(const qa_input_platform *p,
    const qa_input_platform_settings_ticket *t, qa_error *error) {
    if (!p || !t || t->platform != p || t->terminal || p->settings_ticket != t ||
        !p->native_owned || p->native_initializing || p->keyboard != t->keyboard) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input settings have no actual retained platform lease");
        return false;
    }
    for (unsigned slot = 0; slot < 4; ++slot)
        if (p->seats[slot].seat != t->routes[slot].seat || p->seats[slot].instance != t->routes[slot].instance ||
            p->seats[slot].selection.kind != t->routes[slot].selection.kind ||
            p->seats[slot].selection.ordinal != t->routes[slot].selection.ordinal ||
            memcmp(p->seats[slot].selection.guid, t->routes[slot].selection.guid, sizeof(t->routes[slot].selection.guid)) ||
            p->seats[slot].selection.serial != t->routes[slot].selection.serial) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input settings lost their actual retained physical routes");
            return false;
        }
    return true;
}
const qa_input_platform *qa_input_platform_settings_owner(const qa_input_platform_settings_ticket *t) {
    return t && t->prepared && !t->terminal ? t->platform : NULL;
}
qa_input_seat *qa_input_platform_settings_seat(const qa_input_platform_settings_ticket *t, unsigned slot) {
    return t && t->prepared && !t->terminal && slot < 4 ? t->routes[slot].seat : NULL;
}
static size_t settings_held_keys(const qa_input_platform *p, bool midi, int held[272]) {
    size_t length = 0;
    if (midi) {
        for (int key = 0; key < 256; ++key)
            if (p->midi_held[key]) held[length++] = key;
    } else {
        static const int axes[16] = {QA_KEY_LEFT, QA_KEY_RIGHT, QA_KEY_UP, QA_KEY_DOWN,
            QA_KEY_JOY1 + 15, QA_KEY_JOY1 + 16, QA_KEY_JOY1 + 17, QA_KEY_JOY1 + 18,
            QA_KEY_JOY1 + 19, QA_KEY_JOY1 + 20, QA_KEY_JOY1 + 21, QA_KEY_JOY1 + 22,
            QA_KEY_JOY1 + 23, QA_KEY_JOY1 + 24, QA_KEY_JOY1 + 25, QA_KEY_JOY1 + 26};
        for (unsigned key = 0; key < 256; ++key)
            if (p->source.buttons[key]) held[length++] = QA_KEY_JOY1 + (int)key;
        for (unsigned axis = 0; axis < 16; ++axis) {
            if (!(p->source.old_axes & (1u << axis))) continue;
            bool duplicate = false;
            for (size_t i = 0; i < length; ++i) if (held[i] == axes[axis]) duplicate = true;
            if (!duplicate) held[length++] = axes[axis];
        }
    }
    return length;
}
bool qa_input_platform_settings_keys(const qa_input_platform_settings_ticket *t, bool midi,
    int *keys, size_t capacity, size_t *count, qa_error *error) {
    if (!count || (!keys && capacity) || !t || !t->prepared || !settings_current(t, error)) return false;
    int held[272]; size_t length = settings_held_keys(t->platform, midi, held);
    *count = length;
    if (capacity < length) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input settings pressed-key output is too small");
        return false;
    }
    if (length) memcpy(keys, held, length * sizeof(*keys));
    return true;
}
const char *qa_input_platform_settings_diagnostic(const qa_input_platform_settings_ticket *t) {
    return t && t->prepared && t->published && t->diagnostic[0] ? t->diagnostic : NULL;
}
static qa_input_release_scope settings_scope(const qa_input_platform_settings_ticket *t,
    unsigned slot, int held[528]) {
    int partial[272]; size_t length = 0, count = 0;
    if (t->routes[slot].seat && t->requirements.source_changed && t->requirements.source_slot == (int)slot) {
        length = settings_held_keys(t->platform, false, held);
    }
    if (t->routes[slot].seat && t->requirements.midi_changed && t->requirements.midi_slot == (int)slot) {
        count = settings_held_keys(t->platform, true, partial);
        for (size_t i = 0; i < count; ++i) {
            bool duplicate = false;
            for (size_t j = 0; j < length; ++j) if (held[j] == partial[i]) duplicate = true;
            if (!duplicate) held[length++] = partial[i];
        }
    }
    return (qa_input_release_scope){
        .all = (t->desired.restart_requested || t->surface) && t->routes[slot].seat,
        .clear_gamepad = t->routes[slot].seat && ((t->requirements.controller_routes & (1u << slot)) != 0 ||
            (t->requirements.source_changed && t->requirements.source_slot == (int)slot)),
        .controller = (t->requirements.controller_routes & (1u << slot)) ? t->routes[slot].instance : -1,
        .keys = held, .key_count = length};
}
bool qa_input_platform_settings_release_scope(const qa_input_platform_settings_ticket *t, unsigned slot,
    qa_input_release_scope *out, int *keys, size_t capacity, qa_error *error) {
    if (!out || slot >= 4 || (!keys && capacity) || !t || !t->prepared || !settings_current(t, error)) return false;
    int held[528]; qa_input_release_scope scope = settings_scope(t, slot, held);
    if (capacity < scope.key_count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input release scope pressed-key output is too small");
        return false;
    }
    if (scope.key_count) memcpy(keys, held, scope.key_count * sizeof(*keys));
    scope.keys = keys; *out = scope;
    return true;
}
static bool settings_releases_qualified(const qa_input_platform_settings_ticket *t,
    const qa_input_release *const release[4], qa_console_release_disposition disposition,
    qa_console_release_retirement_fn qualify, void *context, qa_error *error) {
    if (!t || !t->prepared || (t->aborting && !t->retiring) || !settings_current(t, error)) return false;
    for (unsigned slot = 0; slot < 4; ++slot) if (t->routes[slot].seat &&
        t->configuration[slot] != t->routes[slot].seat &&
        !qa_input_seat_configuration_ready(t->routes[slot].seat, t->configuration[slot], error)) return false;
    for (unsigned slot = 0; slot < 4; ++slot) {
        int keys[528]; qa_input_release_scope scope;
        if (!qa_input_platform_settings_release_scope(t, slot, &scope, keys, 528, error)) return false;
        bool source_seat = t->routes[slot].seat &&
            ((t->requirements.source_changed && t->requirements.source_slot == (int)slot) ||
             (t->requirements.midi_changed && t->requirements.midi_slot == (int)slot));
        if (scope.all || scope.clear_gamepad || scope.controller >= 0 || scope.key_count || source_seat) {
            if (!release) {
                qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input endpoint change has no completed source release continuation");
                return false;
            }
            if (qualify) {
                if (!qa_input_release_retirement_scope_ready(release[slot], t->routes[slot].seat,
                    &scope, disposition, qualify, context, error)) return false;
            } else if (!qa_input_release_ready(release[slot], t->routes[slot].seat, &scope, error)) return false;
        }
    }
    return true;
}
static bool settings_releases_ready(const qa_input_platform_settings_ticket *t,
    const qa_input_release *const release[4], qa_error *error) {
    return settings_releases_qualified(t, release, QA_CONSOLE_RELEASE_RETIRED_ACTOR, NULL, NULL, error);
}
qa_input_platform_settings_outcome qa_input_platform_settings_result(const qa_input_platform_settings_ticket *t) {
    if (t && t->published) return QA_INPUT_PLATFORM_SETTINGS_PUBLISHED;
    if (t && t->terminal && t->retiring) return QA_INPUT_PLATFORM_SETTINGS_RETIRED;
    if (t && t->endpoint_entered) return QA_INPUT_PLATFORM_SETTINGS_ENTERED;
    if (t && t->terminal) return QA_INPUT_PLATFORM_SETTINGS_ABORTED;
    return QA_INPUT_PLATFORM_SETTINGS_UNENTERED;
}
static bool settings_endpoints_ready(const qa_input_platform_settings_ticket *, qa_error *);
bool qa_input_platform_settings_enter(qa_input_platform_settings_ticket *t,
    const qa_input_release *const release[4], qa_input_platform_settings_outcome *outcome, qa_error *error) {
    if (t) settings_changed(t);
    if (outcome) *outcome = qa_input_platform_settings_result(t);
    if (t && t->retiring) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input endpoint retirement excludes replacement entry");
        return false;
    }
    if (!outcome || !settings_releases_ready(t, release, error) || !settings_endpoints_ready(t, error)) return false;
    if (t->enter_complete) return true;
    if (t->midi_deferred) {
        if (!t->endpoint_entered) {
            int fd = t->previous_midi_fd;
            t->endpoint_entered = true;
            *outcome = QA_INPUT_PLATFORM_SETTINGS_ENTERED;
            t->platform->midi_fd = t->previous_midi_fd = -1;
            t->platform->midi_pending = false; t->platform->midi_byte = 0;
            t->platform->midi_reads = 0;
            if (close(fd) < 0) {
                qa_error_set(error, QA_ERROR_IO, 0, "Retiring exclusive MIDI input: %s", strerror(errno));
                return false;
            }
        }
        qa_error warning = {0};
        if (!settings_open_midi(t, false, &warning) && !settings_midi_warning(t, &warning, error)) return false;
        t->midi_deferred = false;
    }
    t->enter_complete = true;
    *outcome = qa_input_platform_settings_result(t);
    return settings_current(t, error);
}
static bool settings_devices_ready(const qa_input_platform_settings_ticket *t, qa_error *error) {
    for (unsigned slot = 0; slot < 4; ++slot) if (!settings_bindings_is(t, slot)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Prepared controller dictionary lost its selected physical route");
        return false;
    }
    for (unsigned slot = 0; slot < 4; ++slot) if (t->next_routes[slot].instance >= 0) {
        struct device *d = device(t->platform, t->next_routes[slot].instance);
        if (!d || !t->next_handles[slot] || d->handle != t->next_handles[slot] ||
            SDL_GameControllerGetAttached(d->handle) != SDL_TRUE ||
            SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(d->handle)) != t->next_routes[slot].instance)
            return failed(error, "Prepared controller route lost its retained native device");
    }
    if (t->joystick && SDL_JoystickGetAttached(t->joystick) != SDL_TRUE)
        return failed(error, "Prepared source joystick disconnected");
    if (t->midi_fd >= 0) {
        struct stat status;
        if (fstat(t->midi_fd, &status) < 0 || !S_ISCHR(status.st_mode)) {
            qa_error_set(error, QA_ERROR_IO, 0, "Prepared MIDI input lost its actual character endpoint");
            return false;
        }
    }
    for (size_t i = 0; i < t->output_count; ++i) {
        const input_output_transition *v = &t->outputs[i];
        if (SDL_GameControllerGetAttached(v->device->handle) != SDL_TRUE)
            return failed(error, "Prepared controller output lost its retained native device");
        const input_motor_output *motors[2] = {&v->device->rumble, &v->device->triggers};
        for (unsigned motor = 0; motor < 2; ++motor) if (v->motor_attempted[motor] &&
            (!motors[motor]->requested || !motors[motor]->applied || motors[motor]->low ||
                motors[motor]->high || motors[motor]->duration)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Prepared native motor retirement changed");
            return false;
        }
        for (unsigned sensor = 0; sensor < 6; ++sensor) if (v->device->info.sensors[sensor] &&
            (SDL_GameControllerIsSensorEnabled(v->device->handle, (SDL_SensorType)(sensor + 1)) == SDL_TRUE) !=
                v->sensor_desired[sensor]) return failed(error, "Prepared controller sensor mode changed");
    }
    if (t->source_motor_attempted && (!t->platform->joystick_rumble.requested ||
        !t->platform->joystick_rumble.applied || t->platform->joystick_rumble.low ||
        t->platform->joystick_rumble.high || t->platform->joystick_rumble.duration)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Prepared source motor retirement changed");
        return false;
    }
    return true;
}
static bool settings_requested_grab(SDL_Window *window) {
    return (SDL_GetWindowFlags(window) & (SDL_WINDOW_MOUSE_GRABBED | SDL_WINDOW_KEYBOARD_GRABBED)) != 0;
}
static bool settings_endpoints_ready(const qa_input_platform_settings_ticket *t, qa_error *error) {
    if (t->surface && (!t->window_prepared || !qa_display_surface_ready(t->surface, error))) {
        if (!error || error->code == QA_OK)
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input candidate window has not completed native preparation");
        return false;
    }
    return settings_devices_ready(t, error);
}
bool qa_input_platform_settings_window_stage(qa_input_platform_settings_ticket *t,
    const qa_display_surface_ticket *surface, const qa_input_release *const release[4], qa_error *error) {
    if (t) settings_changed(t);
    if (!surface || !t || t->aborting || t->retiring) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input window preparation requires its retained settings and surface tickets");
        return false;
    }
    if (!settings_releases_ready(t, release, error)) return false;
    qa_input_release_scope all = {.all = true, .controller = -1};
    for (unsigned slot = 0; slot < 4; ++slot) if (t->routes[slot].seat &&
        (!release || !qa_input_release_ready(release[slot], t->routes[slot].seat, &all, error))) {
        if (!error || error->code == QA_OK)
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Window replacement requires every physical seat's completed ALL release");
        return false;
    }
    if (t->surface) {
        if (t->surface != surface || !t->window_prepared) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Retained input window preparation requires checked cleanup");
            return false;
        }
        return settings_endpoints_ready(t, error);
    }
    if (!t->window) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input window replacement has no retained active native window");
        return false;
    }
    /* Surface staging has already moved native focus, and SDL may have
     * transferred the effective grab. Retained device/output proof remains
     * required; the candidate capture is entered and checked below. */
    if (!settings_devices_ready(t, error) || !qa_display_surface_ready(surface, error)) return false;
    const qa_display *active = qa_display_surface_active(surface);
    const qa_display *candidate = qa_display_surface_candidate(surface);
    qa_display_info previous = {0}, next = {0};
    if (!qa_display_info_get(active, &previous, error) ||
        !qa_display_info_get(candidate, &next, error)) return false;
    SDL_Window *window = next.window_id ? SDL_GetWindowFromID(next.window_id) : NULL;
    if (previous.window_id != t->window_id || !window || window == t->window ||
        SDL_GetWindowFromID(previous.window_id) != t->window) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input replacement requires its actual retained active and candidate display windows");
        return false;
    }
    t->surface = surface; t->active_display = active; t->candidate_display = candidate;
    t->candidate_window = window; t->candidate_window_id = next.window_id;
    t->candidate_grab = settings_requested_grab(window);
    if (!settings_window_outputs(t, error)) return false;
    /* Preserve the old external request even while its unfocused window has
     * no effective grab. SDL focus determines the candidate's effective grab. */
    t->capture_attempted = true;
    SDL_SetWindowGrab(t->window, t->platform->old_grab ? SDL_TRUE : SDL_FALSE);
    input_pointer_modes modes = pointer_modes(SDL_GetWindowFlags(window), t->focus,
        t->keyboard >= 0 && t->routes[t->keyboard].seat && t->desired.mouse_available, t->desired.no_grab);
    t->relative = modes.relative; t->grab = modes.grab; t->text = modes.text;
    if (!input_platform_modes_apply(window, t->relative, t->grab, t->text, error)) return false;
    t->window_prepared = true;
    return settings_current(t, error) && settings_endpoints_ready(t, error);
}
bool qa_input_platform_settings_ready(qa_input_platform_settings_ticket *t,
    const qa_input_release *const release[4], qa_error *error) {
    if (t) t->ready_revision = 0;
    uint64_t revision = t ? t->revision : 0;
    if (t && t->retiring) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input endpoint retirement excludes replacement publication");
        return false;
    }
    if (!settings_releases_ready(t, release, error)) return false;
    if (!t->enter_complete || t->midi_deferred) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input settings have not completed their endpoint entry");
        return false;
    }
    if (!settings_endpoints_ready(t, error)) return false;
    if (t->revision != revision) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input settings changed during readiness qualification");
        return false;
    }
    qa_input_platform *p = t->platform;
    t->ready_options = p->options;
    t->ready_devices = p->devices; t->ready_device_count = p->device_count;
    t->ready_midi_devices = p->midi_devices; t->ready_midi_count = p->midi_count;
    t->ready_midi_generation = p->midi_generation;
    for (unsigned slot = 0; slot < 4; ++slot) {
        t->ready_release[slot] = release ? release[slot] : NULL;
        if (!t->routes[slot].seat) continue;
        qa_input_seat *active = t->routes[slot].seat, *candidate = t->configuration[slot];
        memcpy(&t->ready_tuning[slot], qa_input_seat_gamepad_tuning(active), sizeof(t->ready_tuning[slot]));
        memcpy(&t->ready_configuration_tuning[slot], qa_input_seat_gamepad_tuning(candidate),
            sizeof(t->ready_configuration_tuning[slot]));
        t->ready_context[slot] = qa_input_seat_context(active);
        t->ready_configuration_context[slot] = qa_input_seat_context(candidate);
        t->ready_focused[slot] = qa_input_seat_focused(active);
        t->ready_focus[slot] = qa_input_seat_focus(active);
    }
    for (size_t i = 0; i < t->output_count; ++i) {
        t->outputs[i].ready_handle = t->outputs[i].device->handle;
        t->outputs[i].ready_instance = t->outputs[i].device->info.instance;
    }
    t->ready_revision = t->revision;
    if (qa_input_platform_settings_ready_is(t, p, release)) return true;
    t->ready_revision = 0;
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input settings lost their completed retained readiness");
    return false;
}
static bool settings_context_is(qa_command_context a, qa_command_context b) {
    return a.session == b.session && a.owner == b.owner && a.client == b.client &&
        a.seat == b.seat && a.dialect == b.dialect && a.origin == b.origin &&
        a.direct == b.direct && a.console_text == b.console_text && a.script == b.script &&
        a.registry == b.registry && a.generation == b.generation &&
        a.actor.registry == b.actor.registry && a.actor.generation == b.actor.generation &&
        a.actor.slot == b.actor.slot;
}
bool qa_input_platform_settings_ready_is(const qa_input_platform_settings_ticket *t,
    const qa_input_platform *p, const qa_input_release *const release[4]) {
    if (!t || !p || t->platform != p || p->settings_ticket != t || !p->native_owned ||
        p->native_initializing || p->native_startup != INPUT_NATIVE_READY ||
        !t->ready_revision || t->ready_revision != t->revision || !t->prepared ||
        t->aborting || t->retiring || t->terminal || t->published || !t->enter_complete ||
        t->midi_deferred || (t->surface && !t->window_prepared) ||
        p->joystick != t->previous_joystick || p->midi_fd != t->previous_midi_fd ||
        p->joystick_instance != t->requirements.joystick_instance ||
        p->joystick_enabled!=t->previous_joystick_enabled ||
        p->midi_enabled!=t->previous_midi_enabled || p->requested_midi_device!=t->previous_midi_device ||
        p->source_slot != t->requirements.source_slot || p->midi_slot != t->requirements.midi_slot ||
        p->window != t->window_id || p->keyboard != t->keyboard ||
        p->devices != t->ready_devices || p->device_count != t->ready_device_count ||
        p->midi_devices != t->ready_midi_devices || p->midi_count != t->ready_midi_count ||
        p->midi_generation != t->ready_midi_generation ||
        p->options.cvars != t->ready_options.cvars || p->options.user != t->ready_options.user ||
        p->options.render_size != t->ready_options.render_size ||
        p->options.print != t->ready_options.print ||
        p->options.device_changed != t->ready_options.device_changed ||
        p->options.assignment_changed != t->ready_options.assignment_changed) return false;
    for (unsigned slot = 0; slot < 4; ++slot) {
        const struct seat_route *a = &p->seats[slot], *b = &t->routes[slot];
        if (!settings_bindings_is(t, slot)) return false;
        if (t->next_routes[slot].instance >= 0) {
            const struct device *d = NULL;
            for (size_t row = 0; row < p->device_count; ++row)
                if (p->devices[row].info.instance == t->next_routes[slot].instance) d = &p->devices[row];
            if (!d || !t->next_handles[slot] || d->handle != t->next_handles[slot]) return false;
        }
        if ((release ? release[slot] : NULL) != t->ready_release[slot] ||
            a->platform != b->platform || a->slot != b->slot || a->seat != b->seat ||
            a->instance != b->instance || a->selection.kind != b->selection.kind ||
            a->selection.ordinal != b->selection.ordinal ||
            memcmp(a->selection.guid, b->selection.guid, sizeof(a->selection.guid)) ||
            a->selection.serial != b->selection.serial || a->haptic_instance != b->haptic_instance ||
            a->calibration_instance != b->calibration_instance || a->calibration_sensor != b->calibration_sensor ||
            memcmp(&a->haptic, &b->haptic, sizeof(a->haptic))) return false;
        if (!a->seat) {
            if (t->configuration[slot]) return false;
            continue;
        }
        qa_input_seat *candidate = t->configuration[slot];
        if (!candidate || (candidate != a->seat && !qa_input_seat_configuration_owned_is(a->seat, candidate)) ||
            memcmp(qa_input_seat_gamepad_tuning(a->seat), &t->ready_tuning[slot], sizeof(t->ready_tuning[slot])) ||
            memcmp(qa_input_seat_gamepad_tuning(candidate), &t->ready_configuration_tuning[slot],
                sizeof(t->ready_configuration_tuning[slot])) ||
            !settings_context_is(qa_input_seat_context(a->seat), t->ready_context[slot]) ||
            !settings_context_is(qa_input_seat_context(candidate), t->ready_configuration_context[slot]) ||
            qa_input_seat_focused(a->seat) != t->ready_focused[slot] ||
            qa_input_seat_focus(a->seat) != t->ready_focus[slot]) return false;
        int keys[528]; qa_input_release_scope scope = settings_scope(t, slot, keys);
        bool source_seat = (t->requirements.source_changed && t->requirements.source_slot == (int)slot) ||
            (t->requirements.midi_changed && t->requirements.midi_slot == (int)slot);
        if ((scope.all || scope.clear_gamepad || scope.controller >= 0 || scope.key_count || source_seat) &&
            !qa_input_release_completed_is(t->ready_release[slot], a->seat, &scope)) return false;
    }
    for (size_t i = 0; i < t->output_count; ++i) {
        const input_output_transition *v = &t->outputs[i];
        bool retained = false;
        for (size_t row = 0; row < p->device_count; ++row)
            if (v->device == &p->devices[row]) retained = true;
        if (!retained || !v->ready_handle || v->device->handle != v->ready_handle ||
            v->device->info.instance != v->ready_instance) return false;
        const input_motor_output *motors[2] = {&v->device->rumble, &v->device->triggers};
        for (unsigned motor = 0; motor < 2; ++motor) if (v->motor_attempted[motor] &&
            (!motors[motor]->requested || !motors[motor]->applied || motors[motor]->low ||
                motors[motor]->high || motors[motor]->duration)) return false;
        for (unsigned sensor = 0; sensor < 6; ++sensor) if (v->sensor_attempted[sensor] &&
            (!v->device->sensor_output[sensor].requested || !v->device->sensor_output[sensor].applied ||
                v->device->sensor_output[sensor].enabled != v->sensor_desired[sensor])) return false;
    }
    return !t->source_motor_attempted || (p->joystick_rumble.requested && p->joystick_rumble.applied &&
        !p->joystick_rumble.low && !p->joystick_rumble.high && !p->joystick_rumble.duration);
}
static bool settings_dispose(qa_input_platform_settings_ticket *t, qa_error *error) {
    settings_changed(t);
    t->aborting = true;
    if (t->capture_attempted) {
        if (t->surface) SDL_SetWindowGrab(t->candidate_window, t->candidate_grab ? SDL_TRUE : SDL_FALSE);
        if (t->previous_text) SDL_StartTextInput(); else SDL_StopTextInput();
        if (t->window) SDL_SetWindowGrab(t->window, t->previous_grab ? SDL_TRUE : SDL_FALSE);
        bool success = true;
        if (SDL_GetRelativeMouseMode() != (t->previous_relative ? SDL_TRUE : SDL_FALSE) &&
            SDL_SetRelativeMouseMode(t->previous_relative ? SDL_TRUE : SDL_FALSE) < 0)
            success = failed(error, "Restoring input relative mouse capture");
        SDL_ShowCursor(t->previous_cursor);
        if (!success) return false;
        t->capture_attempted = false;
    }
    if (!settings_outputs_abort(t, error)) return false;
    bool success = true;
    if (t->owns_midi) {
        int fd = t->midi_fd; t->midi_fd = -1; t->owns_midi = false;
        if (close(fd) < 0) {
            qa_error_set(error, QA_ERROR_IO, 0, "Closing disposed MIDI input: %s", strerror(errno)); success = false;
        }
    }
    if (t->owns_midi_devices) { free(t->midi_devices); t->midi_devices = NULL; t->owns_midi_devices = false; }
    if (t->owns_joystick) { SDL_JoystickClose(t->joystick); t->joystick = NULL; t->owns_joystick = false; }
    if (t->retiring) {
        t->platform->midi = (qa_midi_decoder){0};
        memset(t->platform->midi_held, 0, sizeof(t->platform->midi_held));
    }
    t->platform->settings_ticket = NULL; t->terminal = true; return success;
}
bool qa_input_platform_settings_abort(qa_input_platform_settings_ticket *t, qa_error *error) {
    if (!t || t->terminal || !settings_current(t, error)) return false;
    if (t->endpoint_entered) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
            "Entered MIDI replacement retains its owners; its retired stream cannot be restored");
        return false;
    }
    return settings_dispose(t, error);
}
bool qa_input_platform_settings_retire_entered(qa_input_platform_settings_ticket *t,
    const qa_input_release *const release[4], qa_error *error) {
    if (!t || !t->endpoint_entered) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input retirement requires its retained entered replacement");
        return false;
    }
    if (!settings_releases_ready(t, release, error)) return false;
    t->retiring = true;
    return settings_dispose(t, error);
}
bool qa_input_platform_settings_retire_entered_empty(qa_input_platform_settings_ticket *t,
    const qa_input_release *const release[4], qa_error *error) {
    if (!t || !t->prepared || !t->endpoint_entered ||
        !settings_current(t, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Empty input retirement requires its actual entered native owner");
        return false;
    }
    for (unsigned slot = 0; slot < 4; ++slot) {
        qa_input_seat *seat = t->routes[slot].seat;
        if (!seat) {
            if ((release && release[slot]) || t->configuration[slot]) {
                qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Empty input retirement has an unrelated physical seat proof");
                return false;
            }
            continue;
        }
        if (!t->configuration[slot] || (t->configuration[slot] != seat &&
            !qa_input_seat_configuration_owned_is(seat, t->configuration[slot]))) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Empty input retirement lost its actual configuration owner");
            return false;
        }
        int keys[528]; qa_input_release_scope scope = settings_scope(t, slot, keys);
        bool required = scope.all || scope.clear_gamepad || scope.controller >= 0 || scope.key_count ||
            (t->requirements.source_changed && t->requirements.source_slot == (int)slot) ||
            (t->requirements.midi_changed && t->requirements.midi_slot == (int)slot);
        const qa_input_release *proof = release ? release[slot] : NULL;
        if ((required || proof) && !qa_input_release_completed_empty_is(proof, seat, &scope)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Empty input retirement cannot discard an authored source history");
            return false;
        }
    }
    t->retiring = true;
    return settings_dispose(t, error);
}
bool qa_input_platform_settings_retire_entered_disposition(qa_input_platform_settings_ticket *t,
    const qa_input_release *const release[4], qa_console_release_disposition disposition,
    qa_console_release_retirement_fn qualify, void *context, qa_error *error) {
    if (!t || !t->endpoint_entered || !qualify ||
        disposition < QA_CONSOLE_RELEASE_RETIRED_ACTOR || disposition > QA_CONSOLE_RELEASE_DETACHED_SOURCE) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input retirement requires its actual entered owner disposition");
        return false;
    }
    if (!settings_releases_qualified(t, release, disposition, qualify, context, error)) return false;
    t->retiring = true;
    return settings_dispose(t, error);
}
void qa_input_platform_settings_publish(qa_input_platform_settings_ticket *t) {
    settings_changed(t);
    qa_input_platform *p = t->platform;
    SDL_Joystick *previous_joystick = p->joystick;
    bool retire_joystick = previous_joystick && (t->owns_joystick || previous_joystick != t->joystick);
    p->joystick = t->joystick; p->joystick_instance = t->joystick_instance;
    if (t->requirements.joystick_instance != t->requirements.next_joystick_instance)
        p->joystick_rumble = (input_motor_output){0};
    t->joystick = previous_joystick; t->owns_joystick = retire_joystick;
    int previous_midi = p->midi_fd;
    bool retire_midi = previous_midi >= 0 && previous_midi != t->midi_fd;
    p->midi_fd = t->midi_fd; t->midi_fd = previous_midi; t->owns_midi = retire_midi;
    p->midi_pending = t->midi_pending; p->midi_byte = t->midi_byte;
    p->midi_reads = t->midi_reads;
    p->midi_generation = t->midi_generation;
    if (t->owns_midi_devices) {
        qa_midi_device *previous_devices = p->midi_devices;
        p->midi_devices = t->midi_devices; p->midi_count = t->midi_count;
        t->midi_devices = previous_devices;
    }
    p->mouse_available = t->desired.mouse_available;
    p->joystick_enabled=t->desired.joystick_enabled;
    p->midi_enabled=t->desired.midi_enabled;
    p->requested_midi_device=t->desired.midi_device;
    p->windows_joystick = t->desired.windows_joystick;
    p->source_slot = t->requirements.next_source_slot; p->midi_slot = t->requirements.next_midi_slot;
    p->midi_channel = t->desired.midi_channel; p->now = t->now;
    if (t->requirements.source_changed || t->surface) p->source = (qa_source_joystick){0};
    if (t->requirements.midi_changed || t->surface) {
        p->midi = (qa_midi_decoder){0};
        memset(p->midi_held, 0, sizeof(p->midi_held));
    }
    for (unsigned slot = 0; slot < 4; ++slot) {
        struct seat_route *r = &p->seats[slot];
        if (t->selected_routes) {
            qa_controller_selection previous = r->selection;
            r->selection = t->next_routes[slot].selection;
            t->next_routes[slot].selection = previous;
            r->instance = t->next_routes[slot].instance;
        }
        if (t->owns_configuration[slot])
            qa_input_seat_configuration_publish(r->seat, t->configuration[slot]);
        if (t->haptic_routes & (1u << slot)) {
            qa_haptic_pattern_release(r->haptic.pattern);
            r->haptic.pattern = NULL; r->haptic.last_index = -1;
            r->haptic_instance = qa_input_platform_controller(p, slot);
        }
        if (r->seat && ((t->calibration_routes & (1u << slot)) ||
            t->gyro_enabled[slot] != t->previous_gyro_enabled[slot])) {
            qa_gamepad_calibration_reset(qa_input_seat_gamepad(r->seat));
            r->calibration_sensor = false; r->calibration_instance = -1;
        }
    }
    if (t->surface) {
        p->window = t->candidate_window_id;
        p->old_grab = t->candidate_grab;
        memset(p->keys, 0, sizeof(p->keys));
    }
    if (t->window) p->capture = t->relative;
    p->settings_ticket = NULL; t->terminal = t->published = true;
}
bool qa_input_platform_settings_ticket_destroy(qa_input_platform_settings_ticket *t, qa_error *error) {
    if (!t) return true;
    if (!t->terminal) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input settings ticket still retains its native preparation");
        return false;
    }
    bool success = true;
    if (t->owns_joystick) SDL_JoystickClose(t->joystick);
    if (t->owns_midi && close(t->midi_fd) < 0) {
        qa_error_set(error, QA_ERROR_IO, 0, "Closing prepared MIDI input: %s", strerror(errno)); success = false;
    }
    if (t->owns_midi_devices) free(t->midi_devices);
    if (t->selected_routes) for (unsigned slot = 0; slot < 4; ++slot)
        free((void *)t->next_routes[slot].selection.serial);
    for (unsigned slot = 0; slot < 4; ++slot) if (t->owns_configuration[slot])
        qa_input_seat_destroy(t->configuration[slot]);
    free(t->outputs);
    free(t); return success;
}
static bool restart_devices(qa_input_platform *p, double time, qa_error *error) {
    p->now = time;
    if (!p->constructor_edit && (!qa_input_device_settings_register(p->options.cvars, error) ||
        !qa_cvars_apply_latched(p->options.cvars, "in_joystick", error) ||
        !qa_cvars_apply_latched(p->options.cvars, "in_joystickProfile", error)))
        return false;
    bind_cvars(p);
    const qa_cvar_view *profile = read_cvar(p, INPUT_CVAR_JOYSTICK_PROFILE);
    if (!profile ||
        (strcmp(profile->value, "linux") != 0 && strcmp(profile->value, "windows") != 0)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "in_joystickProfile must be linux or windows");
        return false;
    }
    p->windows_joystick = strcmp(profile->value, "windows") == 0;
    p->mouse_available = p->constructor_settings?p->constructor_settings->mouse_available:
        variable(p, INPUT_CVAR_MOUSE, 1) != 0;
    p->joystick_enabled=integer(p,INPUT_CVAR_JOYSTICK,0)!=0;
    p->midi_enabled=p->constructor_settings?p->constructor_settings->midi_enabled:variable(p,INPUT_CVAR_MIDI,0)!=0;
    p->requested_midi_device=integer(p,INPUT_CVAR_MIDI_DEVICE,0);
    for (unsigned i = 0; i < 4; ++i)
        if (!qa_haptic_stop(&p->seats[i].haptic, error))
            return false;
    if (p->joystick)
        SDL_JoystickClose(p->joystick);
    p->joystick = NULL;
    p->joystick_instance = -1;
    p->joystick_rumble = (input_motor_output){0};
    if (integer(p, INPUT_CVAR_JOYSTICK, 0) != 0) {
        if (p->windows_joystick) {
            if (!qa_source_joystick_release(&p->source, time, source_key, p, error))
                return false;
        }
        int count = SDL_NumJoysticks();
        if (count < 0)
            return failed(error, "Enumerating source joystick");
        for (int i = 0; i < count; ++i) {
            p->joystick = SDL_JoystickOpen(i);
            if (p->joystick)
                break;
        }
        if (p->joystick) {
            p->joystick_instance = SDL_JoystickInstanceID(p->joystick);
            struct seat_route *r = route_for(p, p->joystick_instance);
            if (r && !qa_input_seat_release_device(r->seat, p->joystick_instance, time, error))
                return false;
        } else
            report(p, "No source joystick found.\n");
    }
    if (!midi_release(p, time, error))
        return false;
    p->midi_channel = integer(p, INPUT_CVAR_MIDI_CHANNEL, 1);
    qa_error warning = {0};
    if (!open_midi(p, &warning)) {
        char message[320];
        (void)snprintf(message, sizeof(message), "WARNING: %s\n", warning.message);
        report(p, message);
    }
    return true;
}
bool qa_input_platform_restart(qa_input_platform *p, double time, qa_error *error) {
    if (!native_owner(p, error)) return false;
    if (!isfinite(time) || time < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid input restart clock");
        return false;
    }
    if (p->native_startup != INPUT_NATIVE_READY && !p->native_initializing)
        return initialize_native(p, time, error);
    return restart_devices(p, time, error) && qa_input_platform_sync_focus(p, error);
}
static bool initialize_native(qa_input_platform *p, double time, qa_error *error) {
    if (p->native_startup == INPUT_NATIVE_READY) return true;
    if (p->native_startup != INPUT_NATIVE_PENDING || p->native_initializing) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Fresh native input startup did not complete");
        return false;
    }
    /* Publication transferred the real lease. Do not replay a failed startup
     * or let callbacks recapture a partially configured native owner. */
    p->native_startup = INPUT_NATIVE_FAILED;
    p->native_initializing = true;
    bool success = true;
    for (size_t i = 0; i < p->device_count; ++i)
        if (!stop_device(p, p->devices[i].info.instance, error)) success = false;
    if (p->joystick && SDL_JoystickHasRumble(p->joystick) &&
        !source_motor(p, 0, 0, 0, error)) success = false;
    if (success) {
        SDL_GameControllerEventState(SDL_ENABLE);
        SDL_JoystickEventState(SDL_ENABLE);
        success = restart_devices(p, time, error) && resolve(p, time, NULL, error);
    }
    SDL_Window *window = p->window ? SDL_GetWindowFromID(p->window) : NULL;
    bool focused = window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    for (unsigned i = 0; success && i < 4; ++i)
        if (p->seats[i].seat) {
            qa_input_event event = {.kind = QA_INPUT_EVENT_FOCUS, .time_ms = time, .down = focused};
            success = qa_input_seat_event(p->seats[i].seat, &event, NULL, error);
        }
    if (success) {
        success = qa_input_platform_sync_focus(p, error);
    }
    p->native_initializing = false;
    if (success) p->native_startup = INPUT_NATIVE_READY;
    return success;
}
typedef struct input_frame_sample {
    bool window, focused, joystick_attached, joystick_hat, midi_stopped;
    struct {
        bool present;
        int32_t instance;
        int16_t axes[QA_AXIS_COUNT];
    } controllers[QA_INPUT_LOCAL_SEATS];
    unsigned joystick_axes, joystick_buttons;
    int16_t source_axes[16];
    uint8_t source_buttons[256], source_hat;
    int midi_error;
    uint32_t midi_bytes;
} input_frame_sample;

enum { INPUT_FRAME_INITIALIZE, INPUT_FRAME_SAMPLE };

void qa_input_platform_collect(qa_input_platform *p, qa_platform_events *events, uint64_t now_ns) {
    if (p && p->native_startup != INPUT_NATIVE_READY)
        qa_platform_events_push(events, QA_PLATFORM_EVENT_INPUT_FRAME,
        now_ns, INPUT_FRAME_INITIALIZE, 0, (qa_bytes){0});
    double now = (double)now_ns / 1000000.0;
    bool subframe = p && integer(p, INPUT_CVAR_SUBFRAME, 1) != 0;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        qa_platform_event_kind kind;
        int32_t value = 0, value2 = 0;
        switch (event.type) {
        case SDL_QUIT:
            qa_platform_events_push(events, QA_PLATFORM_EVENT_QUIT, now_ns, 0, 0, (qa_bytes){0});
            return;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            kind = QA_PLATFORM_EVENT_KEY;
            value = event.key.keysym.sym; value2 = event.type == SDL_KEYDOWN;
            break;
        case SDL_TEXTINPUT:
            kind = QA_PLATFORM_EVENT_CHAR;
            break;
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:
            kind = QA_PLATFORM_EVENT_MOUSE;
            break;
        case SDL_CONTROLLERAXISMOTION:
        case SDL_CONTROLLERSENSORUPDATE:
        case SDL_JOYAXISMOTION:
        case SDL_JOYHATMOTION:
            kind = QA_PLATFORM_EVENT_CONTROLLER_AXIS;
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        case SDL_CONTROLLERTOUCHPADDOWN:
        case SDL_CONTROLLERTOUCHPADMOTION:
        case SDL_CONTROLLERTOUCHPADUP:
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
            kind = QA_PLATFORM_EVENT_CONTROLLER_BUTTON;
            break;
        case SDL_WINDOWEVENT:
            kind = QA_PLATFORM_EVENT_WINDOW;
            value = (int32_t)event.window.windowID;
            value2 = event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED;
            break;
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
        case SDL_CONTROLLERDEVICEREMAPPED:
        case SDL_JOYDEVICEADDED:
        case SDL_JOYDEVICEREMOVED:
            kind = QA_PLATFORM_EVENT_DEVICE;
            break;
        case SDL_DROPFILE:
        case SDL_DROPTEXT:
            SDL_free(event.drop.file);
            continue;
#if SDL_VERSION_ATLEAST(2, 0, 22)
        case SDL_TEXTEDITING_EXT:
            SDL_free(event.editExt.text);
            continue;
#endif
        default:
            continue;
        }
        double time = qa_input_event_time(event.common.timestamp, SDL_GetTicks(), now, subframe);
        qa_platform_events_push(events, kind, (uint64_t)(time * 1000000.0), value, value2,
            (qa_bytes){(const uint8_t *)&event, sizeof(event)});
    }
}

bool qa_input_platform_sample(qa_input_platform *p, qa_platform_events *events,
    uint64_t now_ns, qa_error *error) {
    if (!native_owner(p, error)) return false;
    struct {
        input_frame_sample state;
        uint8_t midi[16 * 4096];
    } sampled;
    sampled.state = (input_frame_sample){0};
    input_frame_sample *sample = &sampled.state;
    SDL_Window *window = p->window ? SDL_GetWindowFromID(p->window) : NULL;
    sample->window = window != NULL;
    sample->focused = window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    SDL_GameControllerUpdate();
    for (unsigned slot = 0; slot < QA_INPUT_LOCAL_SEATS; ++slot) {
        struct seat_route *route = &p->seats[slot];
        struct device *d = device(p, route->instance);
        bool focused = sample->window ? sample->focused :
            route->seat && qa_input_seat_focused(route->seat);
        if (!d || !route->seat || d->info.instance == p->joystick_instance ||
            !focused || qa_input_seat_focus(route->seat) != QA_INPUT_GAME) continue;
        sample->controllers[slot].present = true;
        sample->controllers[slot].instance = d->info.instance;
        for (unsigned axis = 0; axis < QA_AXIS_COUNT; ++axis)
            sample->controllers[slot].axes[axis] =
                SDL_GameControllerGetAxis(d->handle, (SDL_GameControllerAxis)axis);
    }
    sample->joystick_attached = p->joystick && SDL_JoystickGetAttached(p->joystick);
    if (sample->joystick_attached) {
        int axes = SDL_JoystickNumAxes(p->joystick);
        sample->joystick_axes = axes > 0 ? (unsigned)axes : 0;
        if (p->windows_joystick) {
            for (unsigned axis = 0; axis < sample->joystick_axes && axis < 16; ++axis)
                sample->source_axes[axis] = SDL_JoystickGetAxis(p->joystick, (int)axis);
            int buttons = SDL_JoystickNumButtons(p->joystick);
            sample->joystick_buttons = buttons > 0 ? (unsigned)buttons : 0;
            if (sample->joystick_buttons > 256) sample->joystick_buttons = 256;
            for (unsigned button = 0; button < sample->joystick_buttons; ++button)
                sample->source_buttons[button] = SDL_JoystickGetButton(p->joystick, (int)button);
            sample->joystick_hat = SDL_JoystickNumHats(p->joystick) > 0;
            if (sample->joystick_hat) sample->source_hat = SDL_JoystickGetHat(p->joystick, 0);
        }
    }
    for (unsigned reads = 0; reads < 16 && p->midi_fd >= 0; ++reads) {
        uint8_t *bytes = sampled.midi + sample->midi_bytes;
        ssize_t count;
        if (p->midi_pending) {
            bytes[0] = p->midi_byte; count = 1;
            p->midi_pending = false; p->midi_byte = 0;
        } else {
            ++p->midi_reads;
            count = read(p->midi_fd, bytes, 4096);
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) break;
        if (count <= 0) {
            sample->midi_stopped = true;
            sample->midi_error = count < 0 ? errno : 0;
            break;
        }
        sample->midi_bytes += (uint32_t)count;
    }
    qa_platform_events_push(events, QA_PLATFORM_EVENT_INPUT_FRAME, now_ns, INPUT_FRAME_SAMPLE, 0,
        (qa_bytes){(const uint8_t *)&sampled, sizeof(*sample) + sample->midi_bytes});
    return true;
}

static bool sampled_frame(qa_input_platform *p, const input_frame_sample *sample,
    qa_bytes midi_bytes, double now, qa_error *error) {
    p->now = now;
    bool focused = sample->focused;
    for (unsigned slot = 0; sample->window && slot < 4; ++slot)
        if (p->seats[slot].seat && qa_input_seat_focused(p->seats[slot].seat) != focused) {
            if (!window_focus(p, focused, now, error)) return false;
            break;
        }
    int source = integer(p, INPUT_CVAR_JOYSTICK_SEAT, 1), midi = integer(p, INPUT_CVAR_MIDI_SEAT, 1);
    source = source_route(p->seats, source, p->joystick_instance);
    midi = midi >= 1 && midi <= 4 && p->seats[midi - 1].seat ? midi - 1 : -1;
    if (source != p->source_slot) {
        if (!source_device_release(p, now, error))
            return false;
        p->source_slot = source;
        if (source >= 0)
            qa_gamepad_calibration_reset(qa_input_seat_gamepad(p->seats[source].seat));
    }
    if (midi != p->midi_slot) {
        if (!midi_release(p, now, error))
            return false;
        p->midi_slot = midi;
    }
    for (unsigned slot = 0; slot < 4; ++slot) {
        struct seat_route *r = &p->seats[slot];
        struct device *d = device(p, r->instance);
        if (sample->controllers[slot].present && d && r->seat &&
            sample->controllers[slot].instance == d->info.instance &&
            d->info.instance != p->joystick_instance &&
            qa_input_seat_focused(r->seat) && qa_input_seat_focus(r->seat) == QA_INPUT_GAME) {
            for (unsigned axis = 0; axis < QA_AXIS_COUNT; ++axis)
                if (!qa_gamepad_axis(
                        qa_input_seat_gamepad(r->seat), (qa_controller_axis)axis,
                        qa_controller_axis_normalize(
                            (qa_controller_axis)axis,
                            sample->controllers[slot].axes[axis]),
                        true, error))
                    return false;
        }
        if (!haptic_device(r, error))
            return false;
        bool active = r->seat && qa_input_seat_focused(r->seat) &&
                      qa_input_seat_focus(r->seat) == QA_INPUT_GAME;
        if (!qa_haptic_enable(&r->haptic, r->haptic.enabled, active, error) ||
            !qa_haptic_update(&r->haptic, now, error))
            return false;
    }
    if (p->joystick && !sample->joystick_attached) {
        if (!source_device_release(p, now, error))
            return false;
        SDL_JoystickClose(p->joystick);
        p->joystick = NULL;
        p->joystick_instance = -1;
        p->joystick_rumble = (input_motor_output){0};
    }
    unsigned axes = sample->joystick_axes;
    if (p->joystick) {
        if (p->windows_joystick) {
            for (unsigned i = 0; i < axes && i < 16; ++i)
                p->source.axes[i] = sample->source_axes[i];
            for (unsigned i = 0; i < sample->joystick_buttons; ++i)
                if (!qa_source_joystick_button(&p->source, (unsigned)i,
                                               sample->source_buttons[i] != 0, true,
                                               source_key, p, error))
                    return false;
            if (sample->joystick_hat)
                p->source.hat = sample->source_hat;
            if (integer(p, INPUT_CVAR_DEBUG_JOYSTICK, 0) != 0) {
                uint32_t buttons = 0;
                for (unsigned i = 0; i < 32; ++i)
                    if (p->source.buttons[i])
                        buttons |= UINT32_C(1) << i;
                unsigned pov = 65535;
                switch (p->source.hat) {
                case 1:
                    pov = 0;
                    break;
                case 3:
                    pov = 4500;
                    break;
                case 2:
                    pov = 9000;
                    break;
                case 6:
                    pov = 13500;
                    break;
                case 4:
                    pov = 18000;
                    break;
                case 12:
                    pov = 22500;
                    break;
                case 8:
                    pov = 27000;
                    break;
                case 9:
                    pov = 31500;
                    break;
                default:
                    break;
                }
                char line[256];
                (void)snprintf(line, sizeof(line), "%8x %5u %5.2f %5.2f %5.2f %5.2f %6d %6d\n",
                               buttons, pov, (double)p->source.axes[0] / 32768,
                               (double)p->source.axes[1] / 32768, (double)p->source.axes[2] / 32768,
                               (double)p->source.axes[3] / 32768, p->source.axes[4],
                               p->source.axes[5]);
                report(p, line);
            }
        }
    }
    if (!qa_source_joystick_frame(&p->source, p->joystick != NULL, p->windows_joystick,
                                  variable(p, INPUT_CVAR_JOYSTICK_THRESHOLD, 0.15f), axes,
                                  variable(p, INPUT_CVAR_JOYSTICK_BALL_SCALE, 0.02f), source_key, source_mouse,
                                  p, error))
        return false;
    int channel = integer(p, INPUT_CVAR_MIDI_CHANNEL, 1);
    if (channel != p->midi_channel) {
        if (!midi_release(p, now, error)) return false;
        p->midi_channel = channel;
    }
    if (midi_bytes.size && !qa_midi_feed(&p->midi, midi_bytes, channel, now, midi_key, p, error))
        return false;
    if (sample->midi_stopped) {
        char message[320];
        (void)snprintf(message, sizeof(message), "WARNING: MIDI input stopped: %s\n",
            sample->midi_error ? strerror(sample->midi_error) : "end of stream");
        report(p, message);
        close(p->midi_fd);
        p->midi_fd = -1;
        p->midi_pending = false; p->midi_byte = 0;
        p->midi_reads = 0;
        if (!midi_release(p, now, error)) return false;
    }
    return finish_calibration(p, error) && qa_input_platform_sync_focus(p, error);
}

bool qa_input_platform_dispatch(qa_input_platform *p, const qa_platform_event *event,
    qa_bytes payload, bool *handled, qa_error *error) {
    if (!native_owner(p, error)) return false;
    double time = (double)event->time_ns / 1000000.0;
    if (!initialize_native(p, time, error)) return false;
    p->now = time;
    if (event->kind == QA_PLATFORM_EVENT_INPUT_FRAME) {
        if (handled) *handled = true;
        if (event->value == INPUT_FRAME_INITIALIZE) return true;
        input_frame_sample sample;
        memcpy(&sample, payload.data, sizeof(sample));
        qa_bytes midi = {payload.data + sizeof(sample), sample.midi_bytes};
        return sampled_frame(p, &sample, midi, time, error);
    }
    SDL_Event native;
    memcpy(&native, payload.data, sizeof(native));
    return native_event(p, &native, time, handled, error);
}
void qa_input_platform_midi_info(qa_input_platform *p) {
    if (!p || !p->native_owned) return;
    char text[320];
    (void)snprintf(text, sizeof(text),
                   "MIDI control: %s\nport: %d\nchannel: %d\ncurrent device: "
                   "%d\nnumber of devices: %zu\n",
                   integer(p, INPUT_CVAR_MIDI, 0) ? "enabled" : "disabled", integer(p, INPUT_CVAR_MIDI_PORT, 1),
                   integer(p, INPUT_CVAR_MIDI_CHANNEL, 1), integer(p, INPUT_CVAR_MIDI_DEVICE, 0), p->midi_count);
    report(p, text);
    for (size_t i = 0; i < p->midi_count; ++i) {
        (void)snprintf(text, sizeof(text), "%s device %zu: %s\n%s\n",
                       (int64_t)i == integer(p, INPUT_CVAR_MIDI_DEVICE, 0) ? "***" : "...", i,
                       p->midi_devices[i].name, p->midi_devices[i].path);
        report(p, text);
    }
}
size_t qa_input_platform_device_count(const qa_input_platform *p) { return p->device_count; }
bool qa_input_platform_device(const qa_input_platform *p, size_t i, qa_controller_info *out) {
    if (i >= p->device_count || !out)
        return false;
    const struct device *d = &p->devices[i];
    *out = d->info;
    for (unsigned sensor = 0; sensor < 6; ++sensor)
        if (out->sensors[sensor]) {
            out->sensor_enabled[sensor] = SDL_GameControllerIsSensorEnabled(
                                              d->handle, (SDL_SensorType)(sensor + 1)) == SDL_TRUE;
            out->sensor_rate[sensor] =
                SDL_GameControllerGetSensorDataRate(d->handle, (SDL_SensorType)(sensor + 1));
        }
    return true;
}
int32_t qa_input_platform_controller(const qa_input_platform *p, unsigned slot) {
    if (slot >= 4 || !p->seats[slot].seat)
        return -1;
    if (p->source_slot == (int)slot && p->joystick_instance >= 0)
        return p->joystick_instance;
    return p->seats[slot].instance == p->joystick_instance ? -1 : p->seats[slot].instance;
}
bool qa_input_platform_selection(const qa_input_platform *p, unsigned slot, qa_controller_selection *out) {
    if (!p || !out || slot >= QA_INPUT_LOCAL_SEATS)
        return false;
    *out = p->seats[slot].selection;
    return true;
}
bool qa_input_platform_keyboard_read(const qa_input_platform *p, int *slot) {
    if (!p || !slot) return false;
    *slot = p->keyboard;
    return true;
}
bool qa_input_platform_mapping(qa_input_platform *p, const char *mapping, qa_error *error) {
    if (!native_owner(p, error)) return false;
    if (!mapping || SDL_GameControllerAddMapping(mapping) < 0)
        return failed(error, "Adding controller mapping");
    for (size_t i = 0; i < p->device_count; ++i)
        describe(&p->devices[i]);
    return discover(p, -1, error) && resolve(p, p->now, NULL, error);
}
static struct device *required_device(qa_input_platform *p, int32_t instance, qa_error *error) {
    struct device *d = device(p, instance);
    if (!d || !SDL_GameControllerGetAttached(d->handle)) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Controller is disconnected");
        return NULL;
    }
    return d;
}
bool qa_input_platform_snapshot(qa_input_platform *p, int32_t instance, qa_controller_snapshot *out,
                                qa_error *error) {
    struct device *d = required_device(p, instance, error);
    if (!d || !out)
        return false;
    qa_controller_snapshot state = {0};
    for (unsigned i = 0; i < QA_AXIS_COUNT; ++i)
        state.axes[i] = SDL_GameControllerGetAxis(d->handle, (SDL_GameControllerAxis)i);
    for (unsigned i = 0; i < 21; ++i)
        state.buttons[i] = SDL_GameControllerGetButton(d->handle, (SDL_GameControllerButton)i) != 0;
    *out = state;
    return true;
}
static bool amplitudes(float low, float high, qa_error *error) {
    if (isfinite(low) && isfinite(high) && low >= 0 && low <= 1 && high >= 0 && high <= 1)
        return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Controller motor amplitude must be in 0..1");
    return false;
}
bool qa_input_platform_rumble(qa_input_platform *p, int32_t instance, float low, float high,
                              uint32_t duration, qa_error *error) {
    if (!native_owner(p, error)) return false;
    if (!amplitudes(low, high, error))
        return false;
    if (p->joystick && p->joystick_instance == instance && !device(p, instance)) {
        if (!SDL_JoystickHasRumble(p->joystick)) {
            if (low == 0 && high == 0)
                return true;
            qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Joystick does not support vibration");
            return false;
        }
        return source_motor(p, (uint16_t)lroundf(low * 65535),
            (uint16_t)lroundf(high * 65535), duration, error);
    }
    struct device *d = required_device(p, instance, error);
    if (!d)
        return false;
    if (!d->info.rumble) {
        if (low == 0 && high == 0)
            return true;
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Controller does not support vibration");
        return false;
    }
    return controller_motor(d, false, (uint16_t)lroundf(low * 65535),
        (uint16_t)lroundf(high * 65535), duration, error);
}
bool qa_input_platform_trigger_rumble(qa_input_platform *p, int32_t instance, float left,
                                      float right, uint32_t duration, qa_error *error) {
    if (!native_owner(p, error)) return false;
    if (!amplitudes(left, right, error))
        return false;
    struct device *d = required_device(p, instance, error);
    if (!d)
        return false;
    if (!d->info.trigger_rumble) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                     "Controller does not support trigger vibration");
        return false;
    }
    return controller_motor(d, true, (uint16_t)lroundf(left * 65535),
        (uint16_t)lroundf(right * 65535), duration, error);
}
bool qa_input_platform_led(qa_input_platform *p, int32_t instance, uint8_t red, uint8_t green,
                           uint8_t blue, qa_error *error) {
    if (!native_owner(p, error)) return false;
    struct device *d = required_device(p, instance, error);
    if (!d)
        return false;
    if (!d->info.led) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Controller does not support an LED");
        return false;
    }
    return SDL_GameControllerSetLED(d->handle, red, green, blue) == 0 ||
           failed(error, "Controller LED");
}
bool qa_input_platform_sensor(qa_input_platform *p, int32_t instance, SDL_SensorType sensor,
                              bool enabled, qa_error *error) {
    if (!native_owner(p, error)) return false;
    struct device *d = required_device(p, instance, error);
    if (!d)
        return false;
    if (sensor < SDL_SENSOR_ACCEL || (int)sensor > 6 ||
        !SDL_GameControllerHasSensor(d->handle, sensor)) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Controller does not support this sensor");
        return false;
    }
    return controller_sensor(d, sensor, enabled, error);
}
bool qa_input_platform_gyro(qa_input_platform *p, unsigned slot, bool enabled, qa_error *error) {
    if (!native_owner(p, error)) return false;
    if (slot >= 4 || !p->seats[slot].seat) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Unknown gyro seat");
        return false;
    }
    struct seat_route *r = &p->seats[slot];
    if (!qa_input_platform_sensor(p, qa_input_platform_controller(p, slot), SDL_SENSOR_GYRO,
                                  enabled, error))
        return false;
    qa_gamepad_calibration_cancel(qa_input_seat_gamepad(r->seat));
    r->calibration_sensor = false;
    qa_input_seat_gamepad_tuning(r->seat)->gyro_enabled = enabled;
    return true;
}
bool qa_input_platform_calibrate(qa_input_platform *p, unsigned slot, qa_error *error) {
    if (!native_owner(p, error)) return false;
    if (slot >= 4 || !p->seats[slot].seat || !qa_input_seat_focused(p->seats[slot].seat)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Gyro calibration requires a focused local seat");
        return false;
    }
    struct seat_route *r = &p->seats[slot];
    if (!qa_input_platform_sensor(p, qa_input_platform_controller(p, slot), SDL_SENSOR_GYRO, true,
                                  error))
        return false;
    r->calibration_sensor = !qa_input_seat_gamepad_tuning(r->seat)->gyro_enabled;
    r->calibration_instance = qa_input_platform_controller(p, slot);
    qa_gamepad_calibration_begin(qa_input_seat_gamepad(r->seat));
    return true;
}
bool qa_input_platform_calibration_cancel(qa_input_platform *p, unsigned slot, bool reset,
                                          qa_error *error) {
    if (!native_owner(p, error)) return false;
    if (slot >= 4 || !p->seats[slot].seat) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Unknown gyro seat");
        return false;
    }
    if (reset)
        qa_gamepad_calibration_reset(qa_input_seat_gamepad(p->seats[slot].seat));
    else
        qa_gamepad_calibration_cancel(qa_input_seat_gamepad(p->seats[slot].seat));
    return finish_calibration(p, error);
}
qa_haptic_player *qa_input_platform_haptics(qa_input_platform *p, unsigned slot) {
    return slot < 4 ? &p->seats[slot].haptic : NULL;
}
bool qa_input_platform_tactile(qa_input_platform *p, unsigned slot, qa_vfs *vfs, const char *sound,
                               double now, qa_error *error) {
    if (!native_owner(p, error)) return false;
    if (slot >= 4) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Unknown tactile seat");
        return false;
    }
    struct seat_route *r = &p->seats[slot];
    if (!haptic_device(r, error))
        return false;
    if (!r->seat || !r->haptic.enabled || !r->haptic.active ||
        qa_input_platform_controller(p, slot) < 0)
        return true;
    qa_haptic_pattern *pattern = NULL;
    if (!qa_haptic_cache_sound(p->haptics, vfs, sound, &pattern, error))
        return false;
    if (!pattern)
        return true;
    bool ok = qa_haptic_play(&r->haptic, pattern, now, error);
    qa_haptic_pattern_release(pattern);
    return ok;
}
void qa_input_platform_tactile_invalidate(qa_input_platform *p) {
    if (!p || !p->native_owned || p->settings_ticket) return;
    qa_error ignored = {0};
    for (unsigned i = 0; i < 4; ++i)
        (void)qa_haptic_stop(&p->seats[i].haptic, &ignored);
    qa_haptic_cache *next = qa_haptic_cache_create(&ignored);
    if (next) {
        qa_haptic_cache_destroy(p->haptics);
        p->haptics = next;
    }
}
