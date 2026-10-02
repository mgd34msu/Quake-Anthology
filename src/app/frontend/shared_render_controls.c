#include "shared_render_controls.h"
#include "capture.h"
#include "q3_render_policy.h"

struct frontend_shared_render_controls {
    qa_frontend *frontend;
    qa_application *application;
    qa_cpu_renderer *cpu;
    qa_gl_renderer *gl;
    const qa_cvars_edit *edit;
    qa_render_controls_ticket *ticket;
    char *values[3];
    bool published;
};
static bool fail(qa_error *error, const char *message)
{ return frontend_fail(error, QA_ERROR_ARGUMENT, message); }
static const char *const names[3] = {"r_primitives", "r_allowExtensions", "r_ext_compiled_vertex_array"};
static bool values_current(const frontend_shared_render_controls *owner)
{
    for (size_t i = 0; i < 3; ++i) {
        const qa_cvar_view *row = qa_cvars_edit_canonical_record(owner->edit, names[i]);
        if (!row || !row->value || !owner->values[i] || strcmp(row->value, owner->values[i])) return false;
    }
    return true;
}
static bool current(const frontend_shared_render_controls *owner)
{
    const qa_frontend *f = owner ? owner->frontend : NULL;
    return f && f->application == owner->application && f->cpu == owner->cpu &&
        f->gl == owner->gl && !f->stepping && !f->capture && !f->source_restoring &&
        qa_cvars_edit_registry(owner->edit) == qa_application_cvars(owner->application) &&
        qa_cvars_edit_returned_is(owner->edit, qa_application_cvars(owner->application)) &&
        frontend_seat_callbacks_returned(f);
}
static void release(frontend_shared_render_controls **out)
{
    for (size_t i = 0; i < 3; ++i) free((*out)->values[i]);
    free(*out);
    *out = NULL;
}
bool frontend_shared_render_controls_prepare(qa_frontend *f, const qa_cvars_edit *edit,
    frontend_shared_render_controls **out, qa_error *error)
{
    if (!f || !f->application || !edit || !out || *out || (f->cpu && f->gl) ||
        f->stepping || f->capture || f->source_restoring || !frontend_seat_callbacks_returned(f) ||
        qa_cvars_edit_registry(edit) != qa_application_cvars(f->application) ||
        !qa_cvars_edit_returned_is(edit, qa_application_cvars(f->application)))
        return fail(error, "Renderer settings require the actual returned canonical ENGINE owner");
    const qa_cvar_view *rows[3];
    for (size_t i = 0; i < 3; ++i) {
        rows[i] = qa_cvars_edit_canonical_record(edit, names[i]);
        if (!rows[i] || !rows[i]->value) return fail(error, "Renderer primitive selection lacks its canonical rows");
    }
    qa_render_controls_values values = {.primitives = rows[0]->integer,
        .compiled_vertex_arrays = rows[1]->integer != 0 && rows[2]->number != 0};
    if (!f->cpu && !f->gl) return true;
    frontend_shared_render_controls *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining shared renderer settings");
    owner->frontend = f; owner->application = f->application; owner->cpu = f->cpu; owner->gl = f->gl;
    owner->edit = edit;
    for (size_t i = 0; i < 3; ++i) {
        size_t bytes = strlen(rows[i]->value) + 1;
        owner->values[i] = malloc(bytes);
        if (!owner->values[i]) {
            release(&owner);
            return frontend_fail(error, QA_ERROR_MEMORY, "Retaining canonical renderer setting");
        }
        memcpy(owner->values[i], rows[i]->value, bytes);
    }
    qa_render_controls *controls = f->cpu ? qa_cpu_render_controls(f->cpu) : qa_gl_render_controls(f->gl);
    if (!qa_render_controls_prepare(controls, &values, &owner->ticket, error)) {
        release(&owner); return false;
    }
    *out = owner;
    return true;
}
bool frontend_shared_render_controls_ready(const frontend_shared_render_controls *owner, qa_error *error)
{
    return (current(owner) && !owner->published && values_current(owner) &&
        qa_render_controls_ready(owner->ticket, error)) ||
        fail(error, "Shared renderer settings changed after preparation");
}
bool frontend_shared_render_controls_ready_is(const frontend_shared_render_controls *owner)
{
    return current(owner) && !owner->published && values_current(owner) &&
        qa_cvars_edit_ready_is(owner->edit) && qa_render_controls_ready_is(owner->ticket);
}
void frontend_shared_render_controls_publish(frontend_shared_render_controls *owner)
{
    if (!owner || owner->published) return;
    qa_render_controls_publish(owner->ticket);
    owner->published = true;
}
void frontend_shared_render_controls_consume(frontend_shared_render_controls **out)
{
    if (!out || !*out) return;
    frontend_shared_render_controls_publish(*out);
    qa_render_controls_consume(&(*out)->ticket);
    release(out);
}
bool frontend_shared_render_controls_finish(frontend_shared_render_controls **out, qa_error *error)
{
    if (!out || (*out && !(*out)->published)) return fail(error, "Renderer settings finish requires publication");
    if (!*out) return true;
    if (!qa_render_controls_finish(&(*out)->ticket, error)) return false;
    release(out); return true;
}
bool frontend_shared_render_controls_abort(frontend_shared_render_controls **out, qa_error *error)
{
    if (!out || (*out && (*out)->published)) return fail(error, "Renderer settings abort requires an unpublished owner");
    if (!*out) return true;
    if (!qa_render_controls_abort(&(*out)->ticket, error)) return false;
    release(out); return true;
}
const qa_cvar_view *frontend_render_control_record(const qa_cvars *registry, const char *name)
{
    if (!registry || !name) return NULL;
    bool folded = qa_cvars_dialect(registry) == QA_CONSOLE_Q3;
    for (size_t i = 0; i < qa_cvars_count(registry); ++i) {
        const qa_cvar_view *row = qa_cvars_at(registry, i);
        const unsigned char *a = (const unsigned char *)row->name;
        const unsigned char *b = (const unsigned char *)name;
        while (*a && *b) {
            unsigned char c = *a, d = *b;
            if (folded && c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (folded && d >= 'A' && d <= 'Z') d += 'a' - 'A';
            if (c != d) break;
            ++a; ++b;
        }
        if (!*a && !*b) return row;
    }
    return NULL;
}
bool frontend_render_controls_live(qa_frontend *f, qa_error *error)
{
    if (!f || !f->application || f->capture || f->source_restoring || (f->cpu && f->gl))
        return fail(error, "Live renderer settings require their actual frontend/application");
    const qa_cvar_view *row = frontend_render_control_record(qa_application_cvars(f->application), "r_primitives");
    if (!row || !row->value) return fail(error, "Live renderer has no physical ENGINE r_primitives row");
    if (f->frame.source_backend && !frontend_q3_frame_policy_read(f, error)) return false;
    if (!f->cpu && !f->gl) return true;
    qa_render_controls *controls = f->cpu ? qa_cpu_render_controls(f->cpu) : qa_gl_render_controls(f->gl);
    qa_render_controls_values values;
    if (!qa_render_controls_read(controls, &values, error)) return false;
    if (values.primitives == row->integer) return true;
    return qa_render_controls_live_primitives(controls, row->integer, error);
}
bool frontend_q3_shadow_mode_read(const qa_cvars *registry, uint32_t *out, qa_error *error)
{
    const qa_cvar_view *row = registry ? qa_cvars_find(registry, "cg_shadows") : NULL;
    if (!out || !row || !row->value) return fail(error, "Q3 renderer has no actual CLIENTCG cg_shadows setting");
    *out = (uint32_t)row->integer;
    return true;
}
