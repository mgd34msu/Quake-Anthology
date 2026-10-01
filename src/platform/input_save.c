#define _POSIX_C_SOURCE 200809L
#include "input_platform_private.h"
#include "qa/input_platform_save.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct qa_input_platform_restore_guard {
    qa_input_platform *candidate;
    const qa_input_platform *active;
    qa_buffer native_cut;
    bool applied;
};
static bool fail(qa_error *error, qa_status status, const char *text)
{ qa_error_set(error, status, 0, "%s", text); return false; }
static bool integer(qa_source_save_io *io, int *value)
{
    int32_t saved = *value;
    if (!qa_source_save_i32(io, &saved)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) *value = saved;
    return true;
}
static bool text(qa_source_save_io *io, char **value)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ, present = *value != NULL;
    size_t length = !reading && present ? strlen(*value) : 0;
    if (!qa_source_save_bool(io, &present) ||
        (present && !qa_source_save_count(io, &length, SIZE_MAX - 1))) return false;
    if (!reading) return !present || qa_source_save_bytes(io, *value, length);
    char *copy = NULL;
    if (present) {
        if (io->offset > io->input.size || length > io->input.size - io->offset)
            return fail(io->error, QA_ERROR_FORMAT, "truncated platform text");
        copy = malloc(length + 1);
        if (!copy) return fail(io->error, QA_ERROR_MEMORY, "retaining platform text");
        if (!qa_source_save_bytes(io, copy, length)) { free(copy); return false; }
        if (memchr(copy, 0, length)) { free(copy); return fail(io->error, QA_ERROR_FORMAT, "platform text contains NUL"); }
        copy[length] = 0;
    }
    free(*value); *value = copy; return true;
}
static bool blob(qa_source_save_io *io, qa_buffer *value)
{
    size_t count = value->size;
    if (!qa_source_save_count(io, &count, SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (io->offset > io->input.size || count > io->input.size - io->offset)
            return fail(io->error, QA_ERROR_FORMAT, "truncated platform owner record");
        value->data = count ? malloc(count) : NULL; value->size = count;
        if (count && !value->data) return fail(io->error, QA_ERROR_MEMORY, "retaining platform owner record");
    }
    return qa_source_save_bytes(io, value->data, count);
}
static bool info_fields(qa_source_save_io *io, qa_controller_info *info,
                        const qa_controller_info *binding)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    char *name = reading ? NULL : (char *)info->name;
    char *serial = reading ? NULL : (char *)info->serial;
    bool success = text(io, &name) && text(io, &serial) &&
        qa_source_save_i32(io, &info->instance) && qa_source_save_bytes(io, info->guid, sizeof(info->guid)) &&
        qa_source_save_u32(io, &info->ordinal) && qa_source_save_bool(io, &info->virtual_device) &&
        qa_source_save_bool(io, &info->rumble) && qa_source_save_bool(io, &info->trigger_rumble) &&
        qa_source_save_bool(io, &info->led);
    for (unsigned i = 0; success && i < QA_AXIS_COUNT; ++i) success = qa_source_save_bool(io, &info->axes[i]);
    for (unsigned i = 0; success && i < 21; ++i) success = qa_source_save_bool(io, &info->buttons[i]);
    for (unsigned i = 0; success && i < 6; ++i) success = qa_source_save_bool(io, &info->sensors[i]) &&
        qa_source_save_bool(io, &info->sensor_enabled[i]) && qa_source_save_f32(io, &info->sensor_rate[i]);
    success = success && qa_source_save_u32(io, &info->touchpads) && memchr(info->guid, 0, sizeof(info->guid));
    if (reading && success) {
        success = binding && info->instance == binding->instance &&
            (name ? binding->name && !strcmp(name, binding->name) : !binding->name) &&
            (serial ? binding->serial && !strcmp(serial, binding->serial) : !binding->serial) &&
            !memcmp(info->guid, binding->guid, sizeof(info->guid)) && info->ordinal == binding->ordinal &&
            info->virtual_device == binding->virtual_device && info->rumble == binding->rumble &&
            info->trigger_rumble == binding->trigger_rumble && info->led == binding->led &&
            !memcmp(info->axes, binding->axes, sizeof(info->axes)) &&
            !memcmp(info->buttons, binding->buttons, sizeof(info->buttons)) &&
            !memcmp(info->sensors, binding->sensors, sizeof(info->sensors)) &&
            !memcmp(info->sensor_enabled, binding->sensor_enabled, sizeof(info->sensor_enabled)) &&
            !memcmp(info->sensor_rate, binding->sensor_rate, sizeof(info->sensor_rate)) &&
            info->touchpads == binding->touchpads;
        if (success) { info->name = binding->name; info->serial = binding->serial; }
    }
    if (reading) { free(name); free(serial); }
    return success;
}
static bool native_capture(const qa_input_platform *p, qa_buffer *out, qa_error *error)
{
    if (!p || !p->native_owned || !out || out->data || out->size)
        return fail(error, QA_ERROR_ARGUMENT, "platform native cut requires its actual live owner");
    qa_source_save_io io = {0};
    size_t count = p->device_count;
    bool success = qa_source_save_writer(&io, NULL, error) && qa_source_save_count(&io, &count, SIZE_MAX);
    for (size_t i = 0; success && i < count; ++i) {
        qa_controller_info info;
        success = p->devices[i].handle && SDL_GameControllerGetAttached(p->devices[i].handle) == SDL_TRUE &&
            SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p->devices[i].handle)) == p->devices[i].info.instance &&
            qa_input_platform_device(p, i, &info) && info_fields(&io, &info, NULL);
    }
    bool joystick = p->joystick != NULL, midi = p->midi_fd >= 0;
    int32_t instance = p->joystick_instance;
    success = success && qa_source_save_bool(&io, &joystick) && qa_source_save_i32(&io, &instance);
    if (success && joystick) {
        char guid[33];
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(p->joystick), guid, sizeof(guid));
        char *name = (char *)SDL_JoystickName(p->joystick);
        success = SDL_JoystickGetAttached(p->joystick) == SDL_TRUE &&
            SDL_JoystickInstanceID(p->joystick) == instance && text(&io, &name) &&
            qa_source_save_bytes(&io, guid, sizeof(guid));
    }
    success = success && qa_source_save_bool(&io, &midi);
    if (success && midi) {
        struct stat status;
        if (fstat(p->midi_fd, &status) || !S_ISCHR(status.st_mode)) success = false;
        else {
            uint64_t device = status.st_dev, file = status.st_ino, node = status.st_rdev;
            success = qa_source_save_u64(&io, &device) && qa_source_save_u64(&io, &file) && qa_source_save_u64(&io, &node);
        }
    }
    uint32_t window = p->window;
    SDL_Window *handle = window ? SDL_GetWindowFromID(window) : NULL;
    bool relative = SDL_GetRelativeMouseMode() == SDL_TRUE, input = SDL_IsTextInputActive() == SDL_TRUE,
        grab = handle && SDL_GetWindowGrab(handle) == SDL_TRUE;
    int controllers = SDL_GameControllerEventState(SDL_QUERY), joysticks = SDL_JoystickEventState(SDL_QUERY);
    success = success && (!window || handle) && qa_source_save_u32(&io, &window) &&
        qa_source_save_bool(&io, &relative) && qa_source_save_bool(&io, &input) && qa_source_save_bool(&io, &grab) &&
        integer(&io, &controllers) && integer(&io, &joysticks) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!success && error && error->code == QA_OK) fail(error, QA_ERROR_UNSUPPORTED, "platform native endpoint is not qualified");
    return success;
}
static bool native_matches(const qa_input_platform *active, qa_bytes expected, qa_error *error)
{
    qa_buffer current = {0};
    if (!native_capture(active, &current, error)) return false;
    bool success = current.size == expected.size &&
        (!current.size || !memcmp(current.data, expected.data, current.size));
    qa_buffer_free(&current);
    return success || fail(error, QA_ERROR_UNSUPPORTED, "saved platform requires the same native endpoint and SDL cut");
}
static bool selection_fields(qa_source_save_io *io, qa_controller_selection *s)
{
    uint32_t kind = s->kind;
    char *serial = (char *)s->serial;
    bool success = qa_source_save_u32(io, &kind) && kind <= QA_CONTROLLER_SERIAL &&
        qa_source_save_bytes(io, s->guid, sizeof(s->guid)) && qa_source_save_u32(io, &s->ordinal) &&
        text(io, &serial);
    if (io->direction == QA_SOURCE_SAVE_READ) { s->kind = (qa_controller_selection_kind)kind; s->serial = serial; }
    if (!success) return false;
    if (kind == QA_CONTROLLER_AUTO || kind == QA_CONTROLLER_NONE) return true;
    for (unsigned i = 0; i < 32; ++i) {
        unsigned c = (unsigned char)s->guid[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return !s->guid[32] && (kind != QA_CONTROLLER_SERIAL || (s->serial && *s->serial));
}
static bool slot_valid(int slot, const qa_input_platform *p)
{ return slot >= -1 && slot < 4 && (slot < 0 || p->seats[slot].seat); }
static bool state_fields(qa_source_save_io *io, qa_input_platform *p, const qa_input_platform *native,
                          const qa_input_platform_checkpoint_refs *refs)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t devices = p->device_count, capacity = p->device_capacity;
    if (!qa_source_save_count(io, &capacity, SIZE_MAX / sizeof(*p->devices)) ||
        !qa_source_save_count(io, &devices, capacity) ||
        (capacity && (capacity < 8 || (capacity & (capacity - 1))))) return false;
    if (reading) {
        if (devices != native->device_count || io->offset > io->input.size ||
            devices > (io->input.size - io->offset) / 33) return false;
        p->devices = capacity ? calloc(capacity, sizeof(*p->devices)) : NULL;
        if (capacity && !p->devices) return fail(io->error, QA_ERROR_MEMORY, "restoring controller inventory");
        p->device_capacity = capacity; p->device_count = devices;
    }
    for (size_t i = 0; i < devices; ++i) {
        qa_controller_info info = p->devices[i].info;
        if (!info_fields(io, &info, reading ? &native->devices[i].info : NULL)) return false;
        if (reading) { p->devices[i].info = info; p->devices[i].handle = native->devices[i].handle; }
    }
    for (unsigned i = 0; i < 4; ++i) {
        struct seat_route *r = &p->seats[i];
        bool present = r->seat != NULL;
        uint64_t id = 0;
        if (!qa_source_save_bool(io, &present)) return false;
        if (present) {
            if ((!reading && !refs->seat_encode(refs->context, r->seat, &id, io->error)) ||
                !qa_source_save_u64(io, &id) ||
                (reading && (!refs->seat_decode(refs->context, id, &r->seat, io->error) || !r->seat))) return false;
        }
        if (reading && !present) r->seat = NULL;
        if (!selection_fields(io, &r->selection) || !qa_source_save_i32(io, &r->instance) ||
            !qa_source_save_i32(io, &r->haptic_instance) || !qa_source_save_i32(io, &r->calibration_instance) ||
            !qa_source_save_bool(io, &r->calibration_sensor)) return false;
        for (unsigned j = 0; j < i; ++j) if (r->seat && p->seats[j].seat) {
            qa_command_context a = qa_input_seat_context(r->seat), b = qa_input_seat_context(p->seats[j].seat);
            if (r->seat == p->seats[j].seat || (a.session == b.session && a.seat == b.seat)) return false;
        }
    }
    if (!integer(io, &p->keyboard)) return false;
    for (unsigned i = 0; i < SDL_NUM_SCANCODES; ++i) if (!qa_source_save_i32(io, &p->keys[i])) return false;
    if (!qa_source_save_u32(io, &p->window) || !qa_source_save_bool(io, &p->old_relative) ||
        !qa_source_save_bool(io, &p->old_text) || !qa_source_save_bool(io, &p->old_grab) ||
        !qa_source_save_bool(io, &p->capture) || !integer(io, &p->old_controller_events) ||
        !integer(io, &p->old_joystick_events)) return false;
    bool joystick = p->joystick != NULL, midi = p->midi_fd >= 0;
    if (!qa_source_save_bool(io, &joystick) || !qa_source_save_i32(io, &p->joystick_instance)) return false;
    if (reading) {
        if (joystick != (native->joystick != NULL) || p->joystick_instance != native->joystick_instance) return false;
        p->joystick = native->joystick;
    }
    for (unsigned i = 0; i < 16; ++i) {
        uint16_t axis; memcpy(&axis, &p->source.axes[i], sizeof(axis));
        if (!qa_source_save_u16(io, &axis)) return false;
        if (reading) memcpy(&p->source.axes[i], &axis, sizeof(axis));
    }
    if (!qa_source_save_u32(io, &p->source.old_axes) || !qa_source_save_u8(io, &p->source.hat)) return false;
    for (unsigned i = 0; i < 256; ++i) if (!qa_source_save_bool(io, &p->source.buttons[i])) return false;
    if (!qa_source_save_bool(io, &p->windows_joystick) || !qa_source_save_bool(io, &p->mouse_available) ||
        !integer(io, &p->source_slot) || !integer(io, &p->midi_slot) ||
        !qa_source_save_bool(io, &midi) || !integer(io, &p->midi_channel)) return false;
    if (reading) { if (midi != (native->midi_fd >= 0)) return false; p->midi_fd = native->midi_fd; }
    if (!qa_source_save_u8(io, &p->midi.status) || !qa_source_save_u8(io, &p->midi.first) ||
        !qa_source_save_bool(io, &p->midi.has_first)) return false;
    for (unsigned i = 0; i < 256; ++i) if (!qa_source_save_bool(io, &p->midi_held[i])) return false;
    size_t count = p->midi_count;
    if (!qa_source_save_count(io, &count, SIZE_MAX / sizeof(*p->midi_devices))) return false;
    if (reading) {
        if (io->offset > io->input.size || count > (io->input.size - io->offset) / 200) return false;
        p->midi_devices = count ? calloc(count, sizeof(*p->midi_devices)) : NULL;
        if (count && !p->midi_devices) return fail(io->error, QA_ERROR_MEMORY, "restoring MIDI inventory");
        p->midi_count = count;
    }
    for (size_t i = 0; i < count; ++i) {
        qa_midi_device *d = &p->midi_devices[i];
        if (!qa_source_save_u32(io, &d->card) || !qa_source_save_u32(io, &d->device) ||
            !qa_source_save_bytes(io, d->name, sizeof(d->name)) || !memchr(d->name, 0, sizeof(d->name)) ||
            !qa_source_save_bytes(io, d->path, sizeof(d->path)) || !memchr(d->path, 0, sizeof(d->path))) return false;
    }
    return qa_source_save_f64(io, &p->now) && isfinite(p->now) && p->now >= 0 &&
        slot_valid(p->keyboard, p) && p->source_slot >= -1 && p->source_slot < 4 &&
        p->midi_slot >= -1 && p->midi_slot < 4 &&
        (!p->midi.status || (p->midi.status >= 0x80 && p->midi.status < 0xf0)) && p->midi.first < 0x80;
}
static uint32_t services(const qa_input_platform_options *o)
{ return (o->print ? 1u : 0u) | (o->device_changed ? 2u : 0u) | (o->assignment_changed ? 4u : 0u); }
static bool envelope(qa_source_save_io *io, qa_input_platform *p, const qa_input_platform *native,
    const qa_input_platform_checkpoint_refs *refs, qa_buffer *native_cut, qa_buffer *haptic)
{
    uint8_t magic[4] = {'Q','I','P','L'};
    uint32_t version = 1, callbacks = services(&p->options);
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "QIPL", sizeof(magic)) &&
        qa_source_save_u32(io, &version) && version == 1 && qa_source_save_u32(io, &callbacks) &&
        callbacks == services(&p->options) && blob(io, native_cut) &&
        state_fields(io, p, native, refs) && blob(io, haptic);
}
bool qa_input_platform_checkpoint(const qa_input_platform *p, const qa_input_platform_checkpoint_refs *refs,
    qa_buffer *out, qa_error *error)
{
    if (!p || !p->native_owned || !refs || !refs->seat_encode || !out || out->data || out->size ||
        !input_platform_haptic_bindings_ready(p))
        return fail(error, QA_ERROR_ARGUMENT, "platform capture requires actual idle holders and empty output");
    qa_buffer native_cut = {0}, haptic = {0};
    qa_haptic_player *players[4];
    for (unsigned i = 0; i < 4; ++i) players[i] = (qa_haptic_player *)&p->seats[i].haptic;
    qa_source_save_io io = {0};
    bool success = native_capture(p, &native_cut, error) &&
        qa_haptic_checkpoint(p->haptics, players, 4, &refs->haptics, &haptic, error) &&
        qa_source_save_writer(&io, NULL, error) && envelope(&io, (qa_input_platform *)p, NULL, refs, &native_cut, &haptic) &&
        qa_source_save_finish(&io, out);
    qa_buffer_free(&native_cut); qa_buffer_free(&haptic); qa_source_save_dispose(&io);
    if (!success && error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "invalid installed platform continuation");
    return success;
}
bool qa_input_platform_restore(qa_input_platform *p, const qa_input_platform *active,
    const qa_input_platform_checkpoint_refs *refs, qa_bytes bytes,
    qa_input_platform_restore_guard **out, qa_error *error)
{
    if (!p || p->native_owned || !active || !active->native_owned || !refs || !refs->seat_decode || !out || *out ||
        !input_platform_haptic_bindings_ready(p) || p->devices || p->joystick || p->midi_fd >= 0 ||
        p->options.print != active->options.print || p->options.device_changed != active->options.device_changed ||
        p->options.assignment_changed != active->options.assignment_changed)
        return fail(error, QA_ERROR_ARGUMENT, "platform restore requires genuine detached and active owners");
    qa_input_platform *candidate = qa_input_platform_create_detached(&p->options, error);
    qa_input_platform_restore_guard *guard = calloc(1, sizeof(*guard));
    qa_buffer haptic = {0};
    qa_source_save_io io = {0};
    bool success = candidate && guard && qa_source_save_reader(&io, NULL, bytes, error) &&
        envelope(&io, candidate, active, refs, &guard->native_cut, &haptic) && qa_source_save_finish(&io, NULL) &&
        native_matches(active, (qa_bytes){guard->native_cut.data, guard->native_cut.size}, error);
    if ((!candidate || !guard) && error && error->code == QA_OK) fail(error, QA_ERROR_MEMORY, "allocating platform candidate");
    qa_haptic_player *players[4];
    if (success) {
        for (unsigned i = 0; i < 4; ++i) players[i] = &candidate->seats[i].haptic;
        success = qa_haptic_restore(candidate->haptics, players, 4, &refs->haptics,
            (qa_bytes){haptic.data, haptic.size}, error);
    }
    if (success) {
        qa_input_platform old = *p;
        *p = *candidate; *candidate = old;
        input_platform_route_contexts_rebind(p);
        guard->candidate = p; guard->active = active; *out = guard; guard = NULL;
    }
    qa_input_platform_destroy(candidate); qa_input_platform_restore_guard_destroy(guard);
    qa_buffer_free(&haptic); qa_source_save_dispose(&io);
    if (!success && error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "invalid platform continuation");
    return success;
}
bool qa_input_platform_handoff_ready(const qa_input_platform_restore_guard *g, qa_error *error)
{
    if (!g || g->applied || !g->candidate || g->candidate->native_owned || !g->active || !g->active->native_owned ||
        !input_platform_haptic_bindings_ready(g->candidate) || !input_platform_haptic_bindings_ready(g->active) ||
        g->candidate->device_count != g->active->device_count || g->candidate->joystick != g->active->joystick ||
        g->candidate->midi_fd != g->active->midi_fd || g->candidate->window != g->active->window)
        return fail(error, QA_ERROR_ARGUMENT, "platform native handoff lost its qualified ownership cut");
    for (size_t i = 0; i < g->candidate->device_count; ++i)
        if (g->candidate->devices[i].handle != g->active->devices[i].handle)
            return fail(error, QA_ERROR_ARGUMENT, "platform native controller owner changed");
    return native_matches(g->active, (qa_bytes){g->native_cut.data, g->native_cut.size}, error);
}
bool qa_input_platform_restore_checkpoint(const qa_input_platform_restore_guard *g,
    const qa_input_platform_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!refs || !refs->seat_encode || !out || out->data || out->size)
        return fail(error, QA_ERROR_ARGUMENT, "platform candidate capture requires actual references and empty output");
    if (!qa_input_platform_handoff_ready(g, error)) return false;
    const qa_input_platform *p = g->candidate;
    qa_buffer native_cut = g->native_cut, haptic = {0};
    qa_haptic_player *players[4];
    for (unsigned i = 0; i < 4; ++i) players[i] = (qa_haptic_player *)&p->seats[i].haptic;
    qa_source_save_io io = {0};
    bool success = qa_haptic_checkpoint(p->haptics, players, 4, &refs->haptics, &haptic, error) &&
        qa_source_save_writer(&io, NULL, error) &&
        envelope(&io, (qa_input_platform *)p, NULL, refs, &native_cut, &haptic) &&
        qa_source_save_finish(&io, out);
    qa_buffer_free(&haptic); qa_source_save_dispose(&io);
    if (!success && error && error->code == QA_OK)
        fail(error, QA_ERROR_FORMAT, "invalid detached platform continuation");
    return success;
}
void qa_input_platform_handoff(qa_input_platform_restore_guard *g)
{
    ((qa_input_platform *)g->active)->native_owned = false;
    g->candidate->native_owned = true; g->applied = true;
}
void qa_input_platform_restore_guard_destroy(qa_input_platform_restore_guard *g)
{ if (g) { qa_buffer_free(&g->native_cut); free(g); } }
bool qa_input_platform_context_rebind_ready(const qa_input_platform *p, const void *current, qa_error *error)
{
    return (p && p->options.user == current && input_platform_haptic_bindings_ready(p)) ||
        fail(error, QA_ERROR_ARGUMENT, "platform callback context changed before publication");
}
void qa_input_platform_context_rebind(qa_input_platform *p, void *destination)
{ p->options.user = destination; }
