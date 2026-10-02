#include "controls_private.h"
#include <stdlib.h>

struct qa_render_controls_ticket {
    qa_render_controls *controls;
    qa_render_controls_values original, candidate;
    bool published;
};
void material_source_release(qa_material_source_scratch *source)
{
    const qa_material *material = source->material;
    source->material = NULL;
    qa_material_release(material);
}
void qa_render_controls_init_cpu(qa_render_controls *controls, qa_cpu_renderer *owner)
{
    *controls = (qa_render_controls){.backend = QA_RENDER_CONTROLS_CPU, .owner.cpu = owner,
        .values.compiled_vertex_arrays = true};
    controls->source.owner = controls;
}
void qa_render_controls_init_gl(qa_render_controls *controls, qa_gl_renderer *owner)
{
    *controls = (qa_render_controls){.backend = QA_RENDER_CONTROLS_GL, .owner.gl = owner,
        .values.compiled_vertex_arrays = true};
    controls->source.owner = controls;
}
static bool current(const qa_render_controls *controls)
{
    if (!controls) return false;
    switch (controls->backend) {
    case QA_RENDER_CONTROLS_CPU: return qa_cpu_render_controls_current(controls);
    case QA_RENDER_CONTROLS_GL: return qa_gl_render_controls_current(controls);
    }
    return false;
}
static bool fail(qa_error *error, const char *message)
{
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static bool source_current(const qa_render_controls *controls)
{
    if (!controls) return false;
    switch (controls->backend) {
    case QA_RENDER_CONTROLS_CPU: return qa_cpu_source_scratch_current(controls);
    case QA_RENDER_CONTROLS_GL: return qa_gl_source_scratch_current(controls);
    }
    return false;
}
bool qa_render_controls_read(const qa_render_controls *controls, qa_render_controls_values *out, qa_error *error)
{
    if (!out || !current(controls)) return fail(error, "Renderer controls lost their actual idle renderer owner");
    *out = controls->values;
    return true;
}
bool qa_render_controls_live_primitives(qa_render_controls *controls, int32_t primitives, qa_error *error)
{
    if (!current(controls) || controls->ticket || controls->source.issuing ||
        controls->source.submitting || controls->source.dispatching)
        return fail(error, "Live primitive setting requires its actual waiting renderer owner");
    controls->values.primitives = primitives;
    return true;
}
qa_material_source_scratch *qa_render_controls_source_scratch(qa_render_controls *controls, qa_error *error)
{
    if (!controls || controls->source.owner != controls || controls->ticket ||
        (controls->source.entered && (controls->source.dispatching || controls->source.submitting)) ||
        (controls->backend == QA_RENDER_CONTROLS_CPU ? !controls->owner.cpu :
         controls->backend == QA_RENDER_CONTROLS_GL ? !controls->owner.gl : true)) {
        fail(error, "Source tess requires its actual renderer allocation"); return NULL;
    }
    return &controls->source;
}
bool material_source_enter(qa_material_source_scratch *source, qa_error *error)
{
    qa_render_controls *owner = source ? source->owner : NULL;
    if (!source_current(owner) || &owner->source != source || owner->ticket || source->submitting ||
        (source->entered && !source->dispatching))
        return fail(error, "Source tess lost its actual available renderer owner");
    source->entered = source->submitting = true; return true;
}
bool qa_material_source_vertices(qa_material_source_scratch *source,
    const qa_scene_vertex **out, size_t *count, qa_error *error)
{
    qa_render_controls *owner = source ? source->owner : NULL;
    if (!out || !count || !source_current(owner) || &owner->source != source || owner->ticket ||
        (source->entered && !source->collecting))
        return fail(error, "Source vertices require their actual idle renderer allocation");
    *out = source->vertices; *count = QA_SOURCE_TESS_VERTICES; return true;
}
bool material_source_current(const qa_material_source_scratch *source, qa_error *error)
{
    const qa_render_controls *owner = source ? source->owner : NULL;
    return (source_current(owner) && &owner->source == source && source->entered && !owner->ticket) ||
        fail(error, "Source tess renderer retired during a reached operation");
}
void material_source_leave(qa_material_source_scratch *source)
{
    qa_render_controls *owner = source->owner;
    source->submitting = false;
    if (source->dispatching) return;
    source->entered = false;
    switch (owner->backend) {
    case QA_RENDER_CONTROLS_CPU: qa_cpu_render_controls_close(owner); break;
    case QA_RENDER_CONTROLS_GL: qa_gl_render_controls_close(owner); break;
    }
}
bool material_source_execute_prefix(qa_material_source_scratch *source, const qa_scene_frame *frame, bool finish, qa_error *error)
{
    if (!source || !source->issuing || source->frame != frame || source->issued_count > frame->command_count ||
        !material_source_current(source, error)) return false;
    bool begin = !source->issue_started;
    source->issue_started = true;
    size_t first = finish ? frame->command_count : source->issued_count;
    bool ok = source->owner->backend == QA_RENDER_CONTROLS_CPU ?
        qa_cpu_source_execute_prefix(source->owner, frame, first, begin, finish, error) :
        qa_gl_source_execute_prefix(source->owner, frame, first, begin, finish, error);
    if (ok) source->issued_count = frame->command_count;
    return ok;
}
bool qa_render_controls_prepare(qa_render_controls *controls, const qa_render_controls_values *values,
    qa_render_controls_ticket **out, qa_error *error)
{
    if (!values || !out || *out || !current(controls) || controls->ticket || controls->source.entered)
        return fail(error, "Renderer controls require their actual idle owner and empty ticket");
    qa_render_controls_ticket *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining renderer controls ticket");
        return false;
    }
    ticket->controls = controls;
    ticket->original = controls->values;
    ticket->candidate = *values;
    controls->ticket = ticket;
    *out = ticket;
    return true;
}
bool qa_render_controls_ready_is(const qa_render_controls_ticket *ticket)
{
    const qa_render_controls *controls = ticket ? ticket->controls : NULL;
    return current(controls) && controls->ticket == ticket && !ticket->published &&
        controls->values.primitives == ticket->original.primitives &&
        controls->values.compiled_vertex_arrays == ticket->original.compiled_vertex_arrays;
}
bool qa_render_controls_ready(const qa_render_controls_ticket *ticket, qa_error *error)
{
    return qa_render_controls_ready_is(ticket) || fail(error, "Renderer controls ticket is no longer ready");
}
void qa_render_controls_publish(qa_render_controls_ticket *ticket)
{
    if (!ticket || ticket->published) return;
    ticket->controls->values = ticket->candidate;
    ticket->published = true;
}
static void release(qa_render_controls_ticket **out)
{
    qa_render_controls_ticket *ticket = *out;
    qa_render_controls *controls = ticket->controls;
    controls->ticket = NULL;
    free(ticket);
    *out = NULL;
    switch (controls->backend) {
    case QA_RENDER_CONTROLS_CPU: qa_cpu_render_controls_close(controls); break;
    case QA_RENDER_CONTROLS_GL: qa_gl_render_controls_close(controls); break;
    }
}
void qa_render_controls_consume(qa_render_controls_ticket **out)
{
    if (!out || !*out) return;
    qa_render_controls_publish(*out);
    release(out);
}
bool qa_render_controls_finish(qa_render_controls_ticket **out, qa_error *error)
{
    if (!out || (*out && (!(*out)->published || (*out)->controls->ticket != *out)))
        return fail(error, "Renderer controls finish requires its published retained ticket");
    if (*out) release(out);
    return true;
}
bool qa_render_controls_abort(qa_render_controls_ticket **out, qa_error *error)
{
    if (!out || (*out && ((*out)->published || (*out)->controls->ticket != *out)))
        return fail(error, "Renderer controls abort requires its unpublished retained ticket");
    if (*out) release(out);
    return true;
}
qa_render_primitive_mode qa_render_primitives_mode(int32_t requested, bool indexed_arrays)
{
    if (!requested) return indexed_arrays ? QA_RENDER_PRIMITIVES_INDEXED : QA_RENDER_PRIMITIVES_ARRAY_STRIPS;
    if (requested == 1) return QA_RENDER_PRIMITIVES_ARRAY_STRIPS;
    if (requested == 2) return QA_RENDER_PRIMITIVES_INDEXED;
    if (requested == 3) return QA_RENDER_PRIMITIVES_DISCRETE_STRIPS;
    return QA_RENDER_PRIMITIVES_NONE;
}
bool qa_render_strip_next(const uint32_t *indices, size_t count, size_t *cursor, qa_render_strip *out)
{
    if (*cursor == count) return false;
    size_t first = *cursor;
    uint32_t a = indices[first], b = indices[first + 1], c = indices[first + 2];
    bool even = false;
    size_t next = first + 3;
    while (next < count) {
        uint32_t na = indices[next], nb = indices[next + 1], nc = indices[next + 2];
        if (even ? na != a || nb != c : na != c || nb != b) break;
        a = na; b = nb; c = nc; even = !even;
        next += 3;
    }
    *out = (qa_render_strip){indices + first, (next - first) / 3};
    *cursor = next;
    return true;
}
uint32_t qa_render_strip_vertex(const qa_render_strip *strip, size_t ordinal)
{
    return strip->indices[ordinal < 3 ? ordinal : (ordinal - 2) * 3 + 2];
}
