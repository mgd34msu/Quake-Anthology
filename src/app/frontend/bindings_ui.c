#include "internal.h"
#include "config_store.h"
#include <stdio.h>

static const char *binding_text(const qa_input_binding *binding)
{
    return binding->kind == QA_BIND_COMMAND ? binding->command : qa_input_action_command(binding->action);
}
static bool set_command(frontend_seat *seat, const char *text, qa_error *error)
{
    if (!text) text = "";
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (!copy) return frontend_fail(error, QA_ERROR_MEMORY, "editing binding command");
    memcpy(copy, text, size); free(seat->binding_command); seat->binding_command = copy;
    return true;
}
static bool bind_pending(frontend_seat *seat, qa_error *error)
{
    qa_input_binding binding = {.input = seat->pending_binding, .kind = QA_BIND_COMMAND,
        .command = seat->binding_command};
    if (!qa_input_seat_bind(seat->input, &binding, error)) return false;
    seat->binding_conflict = false;
    snprintf(seat->binding_status, sizeof(seat->binding_status), "Binding saved");
    return true;
}
bool frontend_binding_capture(void *context, uint32_t id, qa_physical_input input, qa_error *error)
{
    frontend_seat *seat = context; (void)id;
    if (!seat->binding_command || !*seat->binding_command)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "enter a command before capturing a key");
    seat->pending_binding = input;
    const qa_input_binding *old = qa_input_seat_binding(seat->input, input);
    const char *command = old ? binding_text(old) : NULL;
    if (command && strcmp(command, seat->binding_command)) {
        char physical[128];
        if (!qa_input_physical_name(input, physical, sizeof(physical)))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "captured key has no physical name");
        seat->binding_conflict = true;
        snprintf(seat->binding_status, sizeof(seat->binding_status), "%s is bound to %.96s; replace or cancel", physical, command);
        return true;
    }
    return bind_pending(seat, error);
}
void frontend_binding_cancel(void *context, uint32_t id)
{
    frontend_seat *seat = context; (void)id;
    seat->binding_conflict = false; seat->binding_status[0] = 0;
}
static bool action(void *context, uint32_t id, qa_ui_id control, const qa_ui_action *event, qa_error *error)
{
    frontend_seat *seat = context; (void)id;
    if (control == 1 && (event->kind == QA_UI_SELECT || event->kind == QA_UI_ROW_ACTIVATE)) {
        seat->selected_binding = event->value.row;
        const qa_input_binding *binding = qa_input_seat_binding_at(seat->input, seat->selected_binding);
        seat->binding_conflict = false;
        return !binding || set_command(seat, binding_text(binding), error);
    }
    if (control == 2 && event->kind == QA_UI_CHANGE_TEXT) return set_command(seat, event->value.text, error);
    if (event->kind != QA_UI_ACTIVATE) return true;
    switch (control) {
    case 3:
        if (!seat->binding_command || !*seat->binding_command) return frontend_fail(error, QA_ERROR_ARGUMENT, "enter a binding command");
        seat->binding_conflict = false;
        snprintf(seat->binding_status, sizeof(seat->binding_status), "Press a key, controller button or axis; Escape cancels");
        return qa_ui_capture_binding(seat->ui, true, error);
    case 4: {
        const qa_input_binding *binding = qa_input_seat_binding_at(seat->input, seat->selected_binding);
        if (binding) (void)qa_input_seat_unbind(seat->input, binding->input);
        return true;
    }
    case 5:
        return seat->binding_conflict ? bind_pending(seat, error) : true;
    case 6:
        frontend_binding_cancel(seat, seat->id); return true;
    case 7: {
        uint32_t logical;
        if (!frontend_seat_launch_id_read(seat->frontend,seat->id,&logical))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Binding Reset lost its published launch seat");
        int32_t controller = qa_input_platform_controller(seat->frontend->input, seat->id);
        return frontend_config_store_reset_bindings(seat->frontend->config_store,logical,controller,error);
    }
    default: return true;
    }
}
static bool menu(void *context, uint32_t id, qa_ui_menu *out, qa_error *error)
{
    frontend_seat *seat = context; (void)id;
    size_t count = qa_input_seat_binding_count(seat->input), bytes = 0;
    if (count > SIZE_MAX / sizeof(*seat->binding_rows)) return frontend_fail(error, QA_ERROR_MEMORY, "bindings list overflow");
    for (size_t i = 0; i < count; ++i) {
        const qa_input_binding *binding = qa_input_seat_binding_at(seat->input, i);
        const char *text = binding_text(binding);
        size_t amount = 129 + (text ? strlen(text) : 0);
        if (amount > SIZE_MAX - bytes) return frontend_fail(error, QA_ERROR_MEMORY, "binding text overflow");
        bytes += amount;
    }
    if (count > seat->binding_capacity) {
        qa_ui_row *rows = realloc(seat->binding_rows, count * sizeof(*rows));
        if (!rows) return frontend_fail(error, QA_ERROR_MEMORY, "allocating binding rows");
        seat->binding_rows = rows; seat->binding_capacity = count;
    }
    if (bytes > seat->binding_label_capacity) {
        char *labels = realloc(seat->binding_labels, bytes);
        if (!labels) return frontend_fail(error, QA_ERROR_MEMORY, "allocating binding labels");
        seat->binding_labels = labels; seat->binding_label_capacity = bytes;
    }
    size_t offset = 0; uint64_t revision = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < count; ++i) {
        const qa_input_binding *binding = qa_input_seat_binding_at(seat->input, i);
        char *key = seat->binding_labels + offset;
        if (!qa_input_physical_name(binding->input, key, 128)) return frontend_fail(error, QA_ERROR_ARGUMENT, "binding lacks a physical name");
        offset += strlen(key) + 1;
        const char *text = binding_text(binding); if (!text) text = "";
        char *command = seat->binding_labels + offset; size_t size = strlen(text) + 1;
        memcpy(command, text, size); offset += size;
        seat->binding_rows[i] = (qa_ui_row){.key = key, .label = key, .detail = command, .enabled = true};
        for (const unsigned char *p = (const unsigned char *)key; *p; ++p) revision = (revision ^ *p) * UINT64_C(1099511628211);
        revision = (revision ^ (uint32_t)binding->input.device) * UINT64_C(1099511628211);
        for (const unsigned char *p = (const unsigned char *)command; *p; ++p) revision = (revision ^ *p) * UINT64_C(1099511628211);
    }
    const char *labels[] = {"", "Command", "Add key", "Remove key", "Replace conflict", "Cancel conflict", "Restore defaults", seat->binding_status};
    for (unsigned i = 0; i < 8; ++i)
        seat->controls[i] = (qa_ui_control){.id = i + 1, .kind = QA_UI_BUTTON, .label = labels[i],
            .rect = {40 + (float)(i % 2) * 280, 296 + (float)(i / 2) * 36, 270, 28},
            .visible = true, .enabled = true, .context = seat, .action = action};
    seat->controls[0].kind = QA_UI_LIST; seat->controls[0].rect = (qa_scene_rect_f){40, 84, 560, 200};
    seat->controls[0].enabled = count != 0;
    seat->controls[0].value.list.rows = seat->binding_rows; seat->controls[0].value.list.count = count;
    seat->controls[0].value.list.selected = seat->selected_binding; seat->controls[0].value.list.row_height = 24;
    seat->controls[0].value.list.revision = revision;
    seat->controls[1].kind = QA_UI_FIELD; seat->controls[1].rect = (qa_scene_rect_f){40, 288, 560, 28};
    seat->controls[1].value.field.text = seat->binding_command ? seat->binding_command : "";
    seat->controls[1].value.field.maximum = 4096; seat->controls[1].enabled = !seat->binding_conflict;
    seat->controls[4].enabled = seat->controls[5].enabled = seat->binding_conflict;
    seat->controls[7].enabled = false; seat->controls[7].rect = (qa_scene_rect_f){40, 430, 560, 42};
    *out = (qa_ui_menu){.id = FRONTEND_BINDINGS, .title = "Controls", .controls = seat->controls, .count = 8};
    return true;
}
bool frontend_bindings_create(frontend_seat *seat, qa_error *error)
{
    return qa_ui_register(seat->ui, &(qa_ui_menu_registration){.id = FRONTEND_BINDINGS,
        .context = seat, .factory = menu, .close = frontend_binding_cancel}, error);
}
void frontend_bindings_destroy(frontend_seat *seat)
{
    free(seat->binding_rows); free(seat->binding_labels); free(seat->binding_command);
    seat->binding_rows = NULL; seat->binding_labels = seat->binding_command = NULL;
}
