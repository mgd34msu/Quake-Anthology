#include "source_objectives.h"
#include "internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct source_objective_value {
    double value;
    qa_string_id stage;
    bool complete;
} source_objective_value;

typedef struct source_objective {
    application_source_objectives *owner;
    size_t index;
    qa_string_id id;
    source_objective_value *values;
    size_t count;
    qa_objective_lease lease;
    qa_objective_state projected;
    application_source_objective_location locations[3];
    bool owned, writable, campaign_gate, bot_goal;
    bool has_carrier, has_target, has_change, has_projection;
} source_objective;

struct application_source_objectives {
    source_objective *rows;
    size_t count;
    qa_actor_owner owner;
    qa_modes *modes;
    application_source_objective_services source;
    bool active, busy;
};

static bool text_id(qa_strings *strings, const qa_json_document *d, qa_json_id id,
    bool namespaced, qa_string_id *out, qa_error *e)
{
    qa_buffer text = {0};
    if (!qa_json_string(d, id, &text, e)) return false;
    const uint8_t *colon = namespaced ? memchr(text.data, ':', text.size) : NULL;
    bool ok = text.size && !memchr(text.data, 0, text.size) &&
        (!namespaced || (colon && colon != text.data && colon + 1 < text.data + text.size));
    if (ok) ok = qa_strings_intern(strings, (qa_bytes){text.data, text.size}, out, e);
    else application_fail(e, QA_ERROR_FORMAT, "Source objective requires a nonempty stage and namespaced identity");
    qa_buffer_free(&text);
    return ok;
}

bool application_source_objectives_create(qa_strings *strings, qa_actor_owner owner,
    const qa_json_document *d, qa_json_id rows, const application_source_objective_services *services,
    application_source_objectives **out, qa_error *e)
{
    if (qa_json_type(d, rows) != QA_JSON_ARRAY)
        return application_fail(e, QA_ERROR_FORMAT, "Source objectives require an array");
    application_source_objectives *o = calloc(1, sizeof(*o));
    if (!o) return application_fail(e, QA_ERROR_MEMORY, "Preparing source objectives");
    o->owner = owner; o->source = *services; o->count = qa_json_size(d, rows);
    o->rows = o->count ? calloc(o->count, sizeof(*o->rows)) : NULL;
    bool ok = !o->count || o->rows;
    if (!ok) application_fail(e, QA_ERROR_MEMORY, "Preparing source objective channels");
    for (size_t i = 0; ok && i < o->count; ++i) {
        source_objective *row = o->rows + i;
        qa_json_id object = qa_json_at(d, rows, i), role = qa_json_get(d, object, "role"),
            state = qa_json_get(d, object, "state"), values = qa_json_get(d, state, "values");
        row->owner = o; row->index = i;
        row->owned = qa_json_string_equal(d, role, "owned");
        ok = (row->owned || qa_json_string_equal(d, role, "borrowed")) &&
            text_id(strings, d, qa_json_get(d, object, "id"), true, &row->id, e) &&
            qa_json_type(d, values) == QA_JSON_ARRAY && qa_json_size(d, values);
        for (size_t j = 0; ok && j < i; ++j) ok = o->rows[j].id != row->id;
        if (!ok) { application_fail(e, QA_ERROR_FORMAT, "Source objectives require distinct channels and declared stages"); break; }
        row->count = qa_json_size(d, values);
        row->values = calloc(row->count, sizeof(*row->values));
        if (!row->values) { ok = application_fail(e, QA_ERROR_MEMORY, "Preparing source objective stages"); break; }
        for (size_t j = 0; ok && j < row->count; ++j) {
            qa_json_id value = qa_json_at(d, values, j);
            source_objective_value *entry = row->values + j;
            ok = qa_json_number(d, qa_json_get(d, value, "value"), &entry->value, e) &&
                isfinite(entry->value) && o->source.normalize(o->source.context, i, &entry->value, e) &&
                text_id(strings, d, qa_json_get(d, value, "stage"), false, &entry->stage, e) &&
                qa_json_bool(d, qa_json_get(d, value, "complete"), &entry->complete, e);
            for (size_t k = 0; ok && k < j; ++k)
                ok = row->values[k].value != entry->value && row->values[k].stage != entry->stage;
            if (!ok) application_fail(e, QA_ERROR_FORMAT, "Source objective stages require distinct representable values and names");
        }
        row->has_carrier = qa_json_type(d, qa_json_get(d, object, "carrier")) != QA_JSON_NULL;
        row->has_target = qa_json_type(d, qa_json_get(d, object, "target")) != QA_JSON_NULL;
        if (ok && row->owned) {
            row->has_change = qa_json_type(d, qa_json_get(d, object, "change")) != QA_JSON_NULL;
            ok = qa_json_bool(d, qa_json_get(d, object, "campaignGate"), &row->campaign_gate, e) &&
                qa_json_bool(d, qa_json_get(d, object, "botGoal"), &row->bot_goal, e);
        } else if (ok) ok = qa_json_bool(d, qa_json_get(d, object, "writable"), &row->writable, e);
    }
    if (!ok) { application_source_objectives_destroy(o); return false; }
    *out = o; return true;
}

static const source_objective_value *stage(const source_objective *row, qa_string_id id)
{
    for (size_t i = 0; i < row->count; ++i)
        if (row->values[i].stage == id) return row->values + i;
    return NULL;
}

static bool source_read(source_objective *row, const application_source_objective_location locations[3],
    qa_objective_state *out, qa_error *e)
{
    application_source_objectives *o = row->owner;
    double scalar; qa_objective_state state = {0};
    if (!o->source.read(o->source.context, row->index, locations, &scalar, &state.carrier, &state.target, e)) return false;
    const source_objective_value *value = NULL;
    for (size_t i = 0; i < row->count; ++i)
        if (row->values[i].value == scalar) { value = row->values + i; break; }
    if (!value) return application_fail(e, QA_ERROR_FORMAT, "Source objective has an undeclared original state");
    state.stage = value->stage; state.complete = value->complete;
    state.phase = value->complete ? QA_OBJECTIVE_COMPLETE : QA_OBJECTIVE_HOME;
    *out = state; return true;
}

static bool read(void *context, qa_objective_state *out, qa_error *e)
{
    source_objective *row = context; application_source_objectives *o = row->owner;
    application_source_objective_location locations[3];
    return o->source.current(o->source.context, true, e) &&
        o->source.locate(o->source.context, row->index, locations, e) &&
        source_read(row, locations, out, e) && o->source.current(o->source.context, true, e);
}

static bool change(void *context, const qa_objective_state *request, qa_error *e)
{
    source_objective *row = context; application_source_objectives *o = row->owner;
    const source_objective_value *value = stage(row, request->stage);
    if (!value || !row->has_change || (!row->has_carrier && request->carrier.registry) ||
        (!row->has_target && request->target.registry))
        return application_fail(e, QA_ERROR_ARGUMENT, "Source objective change requires its original call and storage");
    return o->source.current(o->source.context, true, e) &&
        o->source.change(o->source.context, row->index, value->value, request, e) &&
        o->source.current(o->source.context, true, e);
}

static bool project(source_objective *row, const qa_objective_state *state, qa_error *e)
{
    application_source_objectives *o = row->owner;
    const source_objective_value *value = stage(row, state->stage);
    if (!value || value->complete != state->complete || (!row->has_carrier && state->carrier.registry) ||
        (!row->has_target && state->target.registry))
        return application_fail(e, QA_ERROR_FORMAT, "Borrowed source objective cannot represent its canonical state");
    application_source_objective_location locations[3];
    if (!o->source.locate(o->source.context, row->index, locations, e) ||
        !o->source.write(o->source.context, row->index, locations, value->value, state->carrier, state->target, e) ||
        !o->source.locate(o->source.context, row->index, row->locations, e)) return false;
    row->projected = *state; row->has_projection = true;
    return o->source.current(o->source.context, false, e);
}

static bool refresh(application_source_objectives *o, qa_error *e)
{
    for (size_t i = 0; i < o->count; ++i) {
        source_objective *row = o->rows + i;
        if (row->owned) continue;
        qa_objective_state state;
        if (!qa_modes_objective(o->modes, (qa_mode_id){0}, row->id, &state, e) ||
            !project(row, &state, e)) return false;
    }
    return true;
}

static bool same_location(const application_source_objective_location a[3],
    const application_source_objective_location b[3])
{
    for (size_t i = 0; i < 3; ++i)
        if (a[i].address != b[i].address || a[i].selector != b[i].selector ||
            !qa_actor_id_equal(a[i].actor, b[i].actor)) return false;
    return true;
}

static bool flush(application_source_objectives *o, qa_error *e)
{
    for (size_t i = 0; i < o->count; ++i) {
        source_objective *row = o->rows + i;
        if (row->owned || !row->has_projection) continue;
        application_source_objective_location locations[3];
        if (!o->source.locate(o->source.context, row->index, locations, e)) return false;
        if (!same_location(row->locations, locations)) {
            qa_objective_state state;
            if (!qa_modes_objective(o->modes, (qa_mode_id){0}, row->id, &state, e) || !project(row, &state, e)) return false;
            continue;
        }
        qa_objective_state state;
        if (!source_read(row, locations, &state, e)) return false;
        if (state.stage == row->projected.stage && qa_actor_id_equal(state.carrier, row->projected.carrier) &&
            qa_actor_id_equal(state.target, row->projected.target)) continue;
        if (!row->writable) return application_fail(e, QA_ERROR_ARGUMENT, "Source wrote a read-only borrowed objective");
        if (!qa_modes_change_objective(o->modes, (qa_mode_id){0}, row->id, &state, e) ||
            !o->source.current(o->source.context, false, e) ||
            !qa_modes_objective(o->modes, (qa_mode_id){0}, row->id, &state, e) || !project(row, &state, e)) return false;
    }
    return true;
}

static bool update(application_source_objectives *o, qa_modes *modes, bool before, bool after, qa_error *e)
{
    if (!o || o->busy) return true;
    o->modes = modes;
    if (!o->source.current(o->source.context, false, e)) return false;
    o->busy = true;
    bool ok = (!after || flush(o, e)) && (!before || refresh(o, e));
    o->busy = false; return ok;
}

bool application_source_objectives_refresh(application_source_objectives *o, qa_modes *modes, qa_error *e)
{ return update(o, modes, true, false, e); }
bool application_source_objectives_flush(application_source_objectives *o, qa_modes *modes, qa_error *e)
{ return update(o, modes, false, true, e); }
bool application_source_objectives_sync(application_source_objectives *o, qa_modes *modes, qa_error *e)
{ return update(o, modes, true, true, e); }

bool application_source_objectives_suspend(application_source_objectives *o, qa_error *e)
{
    if (!o) return true;
    if (o->busy) return application_fail(e, QA_ERROR_ARGUMENT, "Source objective callback is still active");
    for (size_t i = 0; i < o->count; ++i) {
        source_objective *row = o->rows + i;
        if (row->lease.serial && !qa_modes_unbind_objective(o->modes, row->lease, e)) return false;
        row->lease = (qa_objective_lease){0}; row->has_projection = false;
    }
    o->active = false; o->modes = NULL; return true;
}

bool application_source_objectives_activate(application_source_objectives *o, qa_modes *modes, qa_error *e)
{
    if (!o) return true;
    if (o->active) return application_source_objectives_sync(o, modes, e);
    o->modes = modes;
    if (!o->source.current(o->source.context, true, e)) return false;
    for (size_t i = 0; i < o->count; ++i) {
        source_objective *row = o->rows + i;
        if (!row->owned) continue;
        qa_objective_state state;
        if (!read(row, &state, e)) goto failed;
        qa_objective_binding binding = {.owner = o->owner, .id = row->id,
            .campaign_gate = row->campaign_gate, .bot_goal = row->bot_goal,
            .context = row, .read = read, .change = row->has_change ? change : NULL};
        if (!qa_modes_bind_objective(modes, &binding, &row->lease, e)) goto failed;
    }
    o->active = true;
    if (application_source_objectives_refresh(o, modes, e)) return true;
failed:
    {
        qa_error cleanup = {0};
        if (!application_source_objectives_suspend(o, &cleanup) && e) *e = cleanup;
    }
    return false;
}

bool application_source_objectives_ready(const application_source_objectives *o, qa_error *e)
{
    if (!o) return true;
    if (o->busy) return application_fail(e, QA_ERROR_ARGUMENT, "Source objective checkpoint intersects a callback");
    if (!o->active) return true;
    if (!o->source.current(o->source.context, false, e)) return false;
    for (size_t i = 0; i < o->count; ++i)
        if ((o->rows[i].lease.serial != 0) != o->rows[i].owned)
            return application_fail(e, QA_ERROR_FORMAT, "Source objective differs from its canonical binding");
    return true;
}

void application_source_objectives_destroy(application_source_objectives *o)
{
    if (!o) return;
    if (o->rows) for (size_t i = 0; i < o->count; ++i) free(o->rows[i].values);
    free(o->rows); free(o);
}

bool application_source_objectives_active(const application_source_objectives *o)
{ return o && o->active; }
