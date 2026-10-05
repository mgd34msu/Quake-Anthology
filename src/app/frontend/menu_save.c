#include "menu_save.h"
#include "save_private.h"
#include "qa/ui_save.h"
#include "qa/text.h"
#include "host_menu.h"
#include "startup_server_profile.h"

typedef struct menu_state {
    char *command, *selected, *server_profile;
    char query[321];
    bool reassign;
    qa_physical_input previous;
    qa_display_backend renderer;
    char width[5], height[5];
    bool display_initialized;
    uint32_t controller_seat;
    size_t binding_capacity, label_capacity, selected_binding;
    qa_physical_input pending;
    bool conflict;
    char status[256];
    size_t selected_setting;
    char value[1024];
    qa_bytes hosting;
} menu_state;
static bool qualified(const frontend_seat *seat)
{
    qa_ui_input_binding binding;
    return seat && seat->frontend && !seat->frontend->stepping &&
        seat->id < seat->frontend->options.seats && seat->frontend->seats &&
        seat == &seat->frontend->seats[seat->id] && seat->ui &&
        qa_ui_input_binding_read(seat->ui, &binding) && binding.seat == seat->input;
}
static bool reservation(qa_source_save_io *io,size_t capacity,size_t stride)
{
    if (!stride || capacity>SIZE_MAX/stride) return false;
    /* One portable wire cell per reserved slot; stride only bounds allocation. */
    size_t size=capacity;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (size>io->input.size-io->offset) return false;
        for (size_t i=0;i<size;++i) if (io->input.data[io->offset+i]) return false;
        io->offset+=size; return true;
    }
    uint8_t zero[128]={0};
    while (size) {
        size_t part=size<sizeof(zero)?size:sizeof(zero);
        if (!qa_source_save_bytes(io,zero,part)) return false;
        size-=part;
    }
    return true;
}
static bool fields(qa_source_save_io *io, uint32_t seat, menu_state *state)
{
    uint8_t magic[4] = {'Q','F','M','U'};
    uint32_t id = seat, kind = state->pending.kind, previous_kind = state->previous.kind, renderer = state->renderer;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QFMU", sizeof(magic)) ||
        !qa_source_save_u32(io, &id) || id != seat ||
        !qa_source_save_owned_text(io, &state->command) ||
        !qa_source_save_owned_text(io, &state->selected) ||
        !qa_source_save_owned_text(io, &state->server_profile) ||
        !qa_source_save_bytes(io, state->query, sizeof(state->query)) || !memchr(state->query, 0, sizeof(state->query)) ||
        !qa_utf8_valid((qa_bytes){(const uint8_t *)state->query, strlen(state->query)}) ||
        !qa_source_save_bool(io, &state->reassign) ||
        !qa_source_save_u32(io, &previous_kind) || previous_kind > QA_PHYSICAL_AXIS ||
        !qa_source_save_i32(io, &state->previous.device) || !qa_source_save_u32(io, &state->previous.code) ||
        !qa_source_save_bool(io, &state->previous.positive) ||
        !qa_source_save_u32(io, &renderer) || renderer > QA_DISPLAY_OPENGL ||
        !qa_source_save_bytes(io, state->width, sizeof(state->width)) || !memchr(state->width, 0, sizeof(state->width)) ||
        !qa_source_save_bytes(io, state->height, sizeof(state->height)) || !memchr(state->height, 0, sizeof(state->height)) ||
        !qa_source_save_bool(io, &state->display_initialized) ||
        !qa_source_save_u32(io, &state->controller_seat) || state->controller_seat >= QA_INPUT_LOCAL_SEATS ||
        !qa_source_save_count(io, &state->binding_capacity, SIZE_MAX / sizeof(qa_ui_row)) ||
        !reservation(io,state->binding_capacity,sizeof(qa_ui_row)) ||
        !qa_source_save_count(io, &state->label_capacity, SIZE_MAX) ||
        !reservation(io,state->label_capacity,1) ||
        !qa_source_save_count(io, &state->selected_binding, SIZE_MAX) ||
        !qa_source_save_u32(io, &kind) || kind > QA_PHYSICAL_AXIS ||
        !qa_source_save_i32(io, &state->pending.device) || !qa_source_save_u32(io, &state->pending.code) ||
        !qa_source_save_bool(io, &state->pending.positive) || !qa_source_save_bool(io, &state->conflict) ||
        !qa_source_save_bytes(io, state->status, sizeof(state->status)) || !memchr(state->status, 0, sizeof(state->status)) ||
        !qa_source_save_count(io, &state->selected_setting, SIZE_MAX) ||
        !qa_source_save_bytes(io, state->value, sizeof(state->value)) || !memchr(state->value, 0, sizeof(state->value))) return false;
    size_t host_size = state->hosting.size;
    if (!qa_source_save_count(io, &host_size, SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (!qa_source_save_span(io, host_size, &state->hosting)) return false;
    } else if (!qa_source_save_bytes(io, (void *)state->hosting.data, host_size)) return false;
    state->pending.kind = (qa_physical_kind)kind;
    state->previous.kind = (qa_physical_kind)previous_kind;
    state->renderer = (qa_display_backend)renderer;
    if (state->reassign && !qa_input_physical_valid(state->previous)) return false;
    return !state->conflict || (state->command && *state->command && qa_input_physical_valid(state->pending));
}
bool frontend_menu_checkpoint(const frontend_seat *seat, qa_buffer *out, qa_error *error)
{
    if (!qualified(seat) || !out || out->data || out->size ||
        (seat->binding_capacity && !seat->binding_rows) ||
        (seat->binding_label_capacity && !seat->binding_labels))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "menu capture requires idle actual private owners and empty output");
    menu_state state = {.command = seat->binding_command, .binding_capacity = seat->binding_capacity,
        .label_capacity = seat->binding_label_capacity, .selected_binding = seat->selected_binding,
        .pending = seat->pending_binding, .conflict = seat->binding_conflict,
        .selected_setting = seat->selected_setting, .selected = seat->binding_selected,
        .reassign = seat->binding_reassign, .previous = seat->binding_previous,
        .renderer = seat->menu_renderer, .display_initialized = seat->menu_display_initialized,
        .controller_seat = seat->menu_controller_seat, .server_profile = seat->server_profile_path};
    memcpy(state.query, seat->binding_query, sizeof(state.query));
    memcpy(state.width, seat->menu_width, sizeof(state.width));
    memcpy(state.height, seat->menu_height, sizeof(state.height));
    memcpy(state.status, seat->binding_status, sizeof(state.status));
    memcpy(state.value, seat->setting_value, sizeof(state.value));
    qa_buffer hosting = {0};
    qa_source_save_io io = {0};
    bool success = frontend_host_menu_checkpoint(seat->host_menu, &hosting, error);
    state.hosting = (qa_bytes){hosting.data, hosting.size};
    success = success && qa_source_save_writer(&io, NULL, error) && fields(&io, seat->id, &state) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    qa_buffer_free(&hosting);
    if (!success && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "invalid retained menu draft");
    return success;
}
bool frontend_menu_restore(frontend_seat *seat, qa_bytes bytes, qa_error *error)
{
    if (!qualified(seat)) return frontend_fail(error, QA_ERROR_ARGUMENT, "menu restore requires its idle stable seat");
    menu_state state = {0};
    qa_ui_row *binding_rows = NULL;
    char *labels = NULL;
    qa_source_save_io io = {0};
    bool success = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, seat->id, &state) &&
        qa_source_save_finish(&io, NULL);
    if (success) success = frontend_host_menu_restore(seat->host_menu, state.hosting, error);
    if (success) success = frontend_startup_server_profile_select(state.server_profile,&seat->server_profile_path,error);
    if (success) {
        binding_rows = state.binding_capacity ? calloc(state.binding_capacity, sizeof(*binding_rows)) : NULL;
        labels = state.label_capacity ? malloc(state.label_capacity) : NULL;
        if ((state.binding_capacity && !binding_rows) ||
            (state.label_capacity && !labels))
            success = frontend_fail(error, QA_ERROR_MEMORY, "restoring retained menu allocation extents");
    }
    if (success) {
        free(seat->binding_command); free(seat->binding_selected); free(seat->binding_rows); free(seat->binding_labels);
        seat->binding_command = state.command; state.command = NULL;
        seat->binding_selected = state.selected; state.selected = NULL;
        seat->binding_reassign = state.reassign; seat->binding_previous = state.previous;
        memcpy(seat->binding_query, state.query, sizeof(state.query));
        seat->menu_renderer = state.renderer; seat->menu_display_initialized = state.display_initialized;
        memcpy(seat->menu_width, state.width, sizeof(state.width));
        memcpy(seat->menu_height, state.height, sizeof(state.height));
        seat->menu_controller_seat = state.controller_seat;
        seat->binding_rows = binding_rows; binding_rows = NULL;
        seat->binding_labels = labels; labels = NULL;
        seat->binding_capacity = state.binding_capacity; seat->binding_label_capacity = state.label_capacity;
        seat->selected_binding = state.selected_binding; seat->pending_binding = state.pending;
        seat->binding_conflict = state.conflict;
        memcpy(seat->binding_status, state.status, sizeof(state.status));
        seat->selected_setting = state.selected_setting;
        memcpy(seat->setting_value, state.value, sizeof(state.value));
    }
    free(state.command); free(state.selected); free(state.server_profile); free(binding_rows); free(labels); qa_source_save_dispose(&io);
    if (!success && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "invalid saved menu draft");
    return success;
}
