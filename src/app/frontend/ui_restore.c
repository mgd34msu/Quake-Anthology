#include "ui_restore.h"

static bool actual_ui(const frontend_seat *seat, const qa_ui *ui, qa_ui_input_binding *binding)
{
    return seat && seat->frontend && seat->frontend->application && !seat->frontend->stepping &&
        !seat->frontend->options.dedicated && seat->frontend->seats &&
        seat->id < seat->frontend->options.seats && seat == seat->frontend->seats + seat->id &&
        seat->ui == ui && ui && seat->input && qa_ui_input_binding_read(ui, binding) &&
        binding->seat == seat->input && binding->handler && binding->context == ui;
}
static bool installed_handler(const frontend_seat *seat, const qa_ui_input_binding *binding,
    qa_input_ui_token token)
{
    qa_input_ui_handler handler = NULL; void *context = NULL;
    return token && qa_input_seat_ui_binding_read(seat->input, token, &handler, &context) &&
        handler == binding->handler && context == binding->context;
}
static bool input_encode(void *context, const qa_ui *ui, qa_input_ui_token token,
    uint64_t *out, qa_error *error)
{
    frontend_seat *seat = context; qa_ui_input_binding binding;
    if (!out || !actual_ui(seat, ui, &binding) || binding.token != token ||
        !installed_handler(seat, &binding, token))
        return frontend_fail(error, QA_ERROR_FORMAT, "Controller token differs from its actual installed input binding");
    *out = token; return true;
}
static bool input_decode(void *context, qa_ui *ui, uint64_t key,
    qa_input_ui_token *out, qa_error *error)
{
    frontend_seat *seat = context; qa_ui_input_binding binding;
    if (!out || !actual_ui(seat, ui, &binding) || binding.token ||
        !installed_handler(seat, &binding, key))
        return frontend_fail(error, QA_ERROR_FORMAT, "Saved controller token has no restored actual input binding");
    *out = key; return true;
}
qa_ui_checkpoint_refs frontend_seat_ui_refs(frontend_seat *seat)
{ return (qa_ui_checkpoint_refs){seat, input_encode, input_decode}; }
qa_hud_checkpoint_refs frontend_hud_image_refs(frontend_scene_namespace *space)
{ return (qa_hud_checkpoint_refs){space, frontend_scene_image_encode, frontend_scene_image_decode}; }
