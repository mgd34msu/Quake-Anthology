#include "qa/targets.h"
#include "qa/arena.h"
#include "qa/text.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct target_index {
    qa_actor_id actor;
    qa_string_id name;
    uint32_t order;
} target_index;
typedef struct authored_index {
    uint32_t order, slot;
} authored_index;
struct qa_targets {
    qa_target_options options;
    qa_target_binding *bindings;
    uint64_t *binding_serial;
    target_index *index;
    authored_index *authored;
    qa_arena scratch;
    size_t count, authored_count, capacity, depth;
    uint64_t actor_revision;
    uint64_t next_binding_serial;
    bool dirty;
};
static bool fail(qa_error *error, const char *text) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", text);
    return false;
}
static bool live(const qa_targets *targets, qa_actor_id actor) {
    return qa_actors_get(qa_session_actors(targets->options.session), actor) != NULL;
}
static const qa_target_binding *binding(const qa_targets *targets, qa_actor_id actor) {
    if (actor.slot >= targets->capacity || !live(targets, actor))
        return NULL;
    const qa_target_binding *entry = &targets->bindings[actor.slot];
    return qa_actor_id_equal(entry->actor, actor) ? entry : NULL;
}
static bool valid_string(const qa_targets *targets, qa_string_id id) {
    if (!id)
        return true;
    qa_bytes text = qa_strings_text(qa_session_strings(targets->options.session), id);
    return text.data && !memchr(text.data, 0, text.size);
}
static bool valid_fields(const qa_targets *targets, const qa_authored_target *fields) {
    return isfinite(fields->delay_seconds) && isfinite(fields->wait_seconds) &&
           valid_string(targets, fields->classname) && valid_string(targets, fields->targetname) &&
           valid_string(targets, fields->target) && valid_string(targets, fields->killtarget) &&
           valid_string(targets, fields->message) && valid_string(targets, fields->shader_old) &&
           valid_string(targets, fields->shader_new);
}
static qa_string_id nonempty(const qa_targets *targets, qa_string_id id) {
    return qa_strings_text(qa_session_strings(targets->options.session), id).size ? id : 0;
}
static void normalize(const qa_targets *targets, qa_authored_target *fields) {
    fields->classname = nonempty(targets, fields->classname);
    fields->targetname = nonempty(targets, fields->targetname);
    fields->target = nonempty(targets, fields->target);
    fields->killtarget = nonempty(targets, fields->killtarget);
    fields->message = nonempty(targets, fields->message);
    fields->shader_old = nonempty(targets, fields->shader_old);
    fields->shader_new = nonempty(targets, fields->shader_new);
}
qa_targets *qa_targets_create(const qa_target_options *options, qa_error *error) {
    if (!options || !options->session) {
        fail(error, "Target routing requires the shared session");
        return NULL;
    }
    qa_targets *targets = calloc(1, sizeof(*targets));
    if (!targets) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating authored target router");
        return NULL;
    }
    targets->options = *options;
    targets->capacity = qa_actors_capacity(qa_session_actors(options->session));
    targets->bindings = calloc(targets->capacity, sizeof(*targets->bindings));
    targets->binding_serial = calloc(targets->capacity, sizeof(*targets->binding_serial));
    targets->index = calloc(targets->capacity, sizeof(*targets->index));
    targets->authored = calloc(targets->capacity, sizeof(*targets->authored));
    if (!targets->bindings || !targets->binding_serial || !targets->index || !targets->authored) {
        qa_targets_destroy(targets);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating target index");
        return NULL;
    }
    targets->dirty = true;
    return targets;
}
void qa_targets_destroy(qa_targets *targets) {
    if (!targets)
        return;
    qa_arena_destroy(&targets->scratch);
    free(targets->authored);
    free(targets->index);
    free(targets->bindings);
    free(targets->binding_serial);
    free(targets);
}
bool qa_targets_bind(qa_targets *targets, const qa_target_binding *entry, qa_error *error) {
    qa_authored_target fields;
    if (!targets || !entry || !entry->read || entry->actor.slot >= targets->capacity ||
        !live(targets, entry->actor) || entry->source < QA_CLOCK_NETQUAKE ||
        entry->source > QA_CLOCK_Q3 || !entry->read(entry->context, entry->actor, &fields) ||
        !valid_fields(targets, &fields))
        return fail(error, "Invalid authored target binding");
    if (targets->next_binding_serial == UINT64_MAX)
        return fail(error, "Authored target binding serial exhausted");
    targets->bindings[entry->actor.slot] = *entry;
    targets->binding_serial[entry->actor.slot] = ++targets->next_binding_serial;
    targets->dirty = true;
    return true;
}
void qa_targets_unbind(qa_targets *targets, qa_actor_id actor) {
    if (actor.slot < targets->capacity &&
        qa_actor_id_equal(targets->bindings[actor.slot].actor, actor)) {
        targets->bindings[actor.slot] = (qa_target_binding){0};
        targets->binding_serial[actor.slot] = 0;
        targets->dirty = true;
    }
}
void qa_targets_unbind_context(qa_targets *targets, qa_actor_id actor, const void *context) {
    if (actor.slot < targets->capacity && targets->bindings[actor.slot].context == context)
        qa_targets_unbind(targets, actor);
}
void qa_targets_changed(qa_targets *targets) { targets->dirty = true; }
bool qa_targets_read(const qa_targets *targets, qa_actor_id actor, qa_authored_target *out) {
    const qa_target_binding *entry = binding(targets, actor);
    qa_authored_target fields;
    if (!entry || !entry->read(entry->context, actor, &fields))
        return false;
    normalize(targets, &fields);
    *out = fields;
    return true;
}
static bool set_target_field(qa_targets *targets, qa_actor_id actor, qa_string_id name,
                              bool targetname, qa_error *error) {
    if (!targets || !valid_string(targets, name))
        return fail(error, "Invalid authored target name");
    const qa_target_binding *entry = binding(targets, actor);
    if (!entry || !(targetname ? entry->set_targetname : entry->set_target)) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Authored target has no field setter");
        return false;
    }
    qa_target_binding before = *entry;
    uint64_t serial = targets->binding_serial[actor.slot];
    targets->dirty = true;
    bool (*write)(void *, qa_actor_id, qa_string_id, qa_error *) =
        targetname ? before.set_targetname : before.set_target;
    bool ok = write(before.context, actor, nonempty(targets, name), error);
    targets->dirty = true;
    if (!ok)
        return false;
    entry = binding(targets, actor);
    if (!entry || targets->binding_serial[actor.slot] != serial) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Authored target owner changed during field write");
        return false;
    }
    return true;
}
bool qa_targets_set_targetname(qa_targets *targets, qa_actor_id actor, qa_string_id name,
                               qa_error *error) {
    return set_target_field(targets, actor, name, true, error);
}
bool qa_targets_set_target(qa_targets *targets, qa_actor_id actor, qa_string_id name,
                           qa_error *error) {
    return set_target_field(targets, actor, name, false, error);
}
bool qa_targets_field(const qa_targets *targets, qa_actor_id actor, const char *key,
                      qa_target_field *out) {
    const qa_target_binding *entry = binding(targets, actor);
    qa_target_field value = {0};
    if (!entry || !entry->field || !entry->field(entry->context, actor, key, &value))
        return false;
    switch (value.kind) {
    case QA_TARGET_FIELD_TEXT:
        if (!valid_string(targets, value.value.text))
            return false;
        break;
    case QA_TARGET_FIELD_NUMBER:
        if (!isfinite(value.value.number))
            return false;
        break;
    case QA_TARGET_FIELD_VECTOR:
        if (!qa_vec_finite(value.value.vector))
            return false;
        break;
    default:
        return false;
    }
    *out = value;
    return true;
}
bool qa_targets_number(const qa_targets *targets, qa_actor_id actor, const char *key, double *out) {
    qa_target_field value;
    if (!targets || !qa_targets_field(targets, actor, key, &value))
        return false;
    if (value.kind == QA_TARGET_FIELD_NUMBER) {
        *out = value.value.number;
        return true;
    }
    if (value.kind != QA_TARGET_FIELD_TEXT)
        return false;
    qa_bytes text = qa_strings_text(qa_session_strings(targets->options.session), value.value.text);
    double number;
    if (!text.size || !qa_parse_number(text, &number, NULL) || !isfinite(number))
        return false;
    *out = number;
    return true;
}
static bool field_space(uint8_t c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}
bool qa_targets_vector(const qa_targets *targets, qa_actor_id actor, const char *key,
                        qa_vec3 *out) {
    qa_target_field value;
    if (!targets || !key || !out || !qa_targets_field(targets, actor, key, &value))
        return false;
    if (value.kind == QA_TARGET_FIELD_VECTOR) {
        *out = value.value.vector;
        return true;
    }
    if (value.kind != QA_TARGET_FIELD_TEXT)
        return false;
    qa_bytes text = qa_strings_text(qa_session_strings(targets->options.session), value.value.text);
    float components[3];
    size_t cursor = 0;
    for (size_t i = 0; i < 3; ++i) {
        while (cursor < text.size && field_space(text.data[cursor]))
            ++cursor;
        size_t begin = cursor;
        while (cursor < text.size && !field_space(text.data[cursor]))
            ++cursor;
        double number;
        if (begin == cursor ||
            !qa_parse_number((qa_bytes){text.data + begin, cursor - begin}, &number, NULL) ||
            !isfinite(number) || number < -FLT_MAX || number > FLT_MAX)
            return false;
        components[i] = (float)number;
    }
    while (cursor < text.size && field_space(text.data[cursor]))
        ++cursor;
    if (cursor != text.size)
        return false;
    *out = qa_v3(components[0], components[1], components[2]);
    return true;
}
static int compare(const void *left, const void *right) {
    const target_index *a = left, *b = right;
    if (a->name != b->name)
        return a->name < b->name ? -1 : 1;
    if (a->order != b->order)
        return a->order < b->order ? -1 : 1;
    return a->actor.slot < b->actor.slot ? -1 : a->actor.slot > b->actor.slot;
}
static int compare_authored(const void *left, const void *right) {
    const authored_index *a = left, *b = right;
    if (a->order != b->order)
        return a->order < b->order ? -1 : 1;
    return a->slot < b->slot ? -1 : a->slot > b->slot;
}
static void refresh(qa_targets *targets) {
    uint64_t revision = qa_actors_revision(qa_session_actors(targets->options.session));
    if (!targets->dirty && targets->actor_revision == revision)
        return;
    targets->count = 0;
    targets->authored_count = 0;
    for (size_t i = 0; i < targets->capacity; ++i) {
        const qa_target_binding *entry = &targets->bindings[i];
        const qa_actor_record *actor =
            qa_actors_get(qa_session_actors(targets->options.session), entry->actor);
        qa_authored_target fields;
        if (!actor || !qa_targets_read(targets, entry->actor, &fields))
            continue;
        uint32_t order = actor->has_source ? actor->source_slot : actor->id.slot;
        targets->authored[targets->authored_count++] = (authored_index){order, actor->id.slot};
        if (fields.targetname)
            targets->index[targets->count++] =
                (target_index){entry->actor, fields.targetname, order};
    }
    if (targets->count > 1)
        qsort(targets->index, targets->count, sizeof(*targets->index), compare);
    if (targets->authored_count > 1)
        qsort(targets->authored, targets->authored_count, sizeof(*targets->authored),
              compare_authored);
    targets->actor_revision = revision;
    targets->dirty = false;
}
static size_t lower(qa_targets *targets, qa_string_id name) {
    refresh(targets);
    size_t low = 0, high = targets->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (targets->index[mid].name < name)
            low = mid + 1;
        else
            high = mid;
    }
    return low;
}
bool qa_targets_first(qa_targets *targets, qa_string_id name, qa_actor_id *out) {
    if (!name)
        return false;
    size_t at = lower(targets, name);
    if (at == targets->count || targets->index[at].name != name)
        return false;
    *out = targets->index[at].actor;
    return true;
}
bool qa_targets_next(qa_targets *targets, qa_string_id name, qa_target_cursor *cursor,
                     qa_actor_id *out) {
    if (!name)
        return false;
    size_t at = lower(targets, name);
    if (cursor->started) {
        size_t high = targets->count;
        while (at < high) {
            size_t mid = at + (high - at) / 2;
            target_index current = targets->index[mid];
            bool visited = current.name == name && (current.order < cursor->source_order ||
                                                    (current.order == cursor->source_order &&
                                                     current.actor.slot <= cursor->host_slot));
            if (visited)
                at = mid + 1;
            else
                high = mid;
        }
    }
    if (at == targets->count || targets->index[at].name != name)
        return false;
    target_index current = targets->index[at];
    *cursor = (qa_target_cursor){current.order, current.actor.slot, true};
    *out = current.actor;
    return true;
}
bool qa_targets_pick(qa_targets *targets, qa_string_id name, uint32_t random, size_t maximum,
                     qa_actor_id *out) {
    if (!name || !maximum)
        return false;
    size_t at = lower(targets, name), count = 0;
    while (count < maximum && at + count < targets->count &&
           targets->index[at + count].name == name)
        ++count;
    if (!count)
        return false;
    *out = targets->index[at + random % count].actor;
    return true;
}
bool qa_targets_next_authored(qa_targets *targets, const char *classname, qa_target_cursor *cursor,
                              qa_actor_id *out) {
    refresh(targets);
    size_t at = 0;
    if (cursor->started) {
        size_t high = targets->authored_count;
        while (at < high) {
            size_t mid = at + (high - at) / 2;
            authored_index current = targets->authored[mid];
            bool visited =
                current.order < cursor->source_order ||
                (current.order == cursor->source_order && current.slot <= cursor->host_slot);
            if (visited)
                at = mid + 1;
            else
                high = mid;
        }
    }
    for (; at < targets->authored_count; ++at) {
        authored_index current = targets->authored[at];
        qa_actor_id actor = targets->bindings[current.slot].actor;
        qa_authored_target fields;
        if (!qa_targets_read(targets, actor, &fields))
            continue;
        if (classname) {
            const char *name =
                qa_strings_cstr(qa_session_strings(targets->options.session), fields.classname);
            if (!name || strcmp(name, classname))
                continue;
        }
        *cursor = (qa_target_cursor){current.order, current.slot, true};
        *out = actor;
        return true;
    }
    return false;
}
static bool snapshot(qa_targets *targets, qa_string_id name, qa_actor_id **out, size_t *count,
                     qa_error *error) {
    *out = NULL;
    *count = 0;
    if (!name)
        return true;
    size_t at = lower(targets, name), end = at;
    while (end < targets->count && targets->index[end].name == name)
        ++end;
    if (end == at)
        return true;
    qa_actor_id *actors = qa_arena_alloc(&targets->scratch, (end - at) * sizeof(*actors),
                                         _Alignof(qa_actor_id), error);
    if (!actors)
        return false;
    for (size_t i = at; i < end; ++i)
        actors[i - at] = targets->index[i].actor;
    *out = actors;
    *count = end - at;
    return true;
}
typedef struct use_invocation {
    qa_targets *router;
    qa_target_binding target;
    uint64_t serial;
    qa_actor_id source, activator;
} use_invocation;
static bool invoke(void *context, qa_session *session, qa_error *error) {
    (void)session;
    use_invocation *use = context;
    if (!binding(use->router, use->target.actor) ||
        use->router->binding_serial[use->target.actor.slot] != use->serial)
        return true;
    return use->target.use(use->target.context, use->target.actor, use->source, use->activator,
                           error);
}
bool qa_targets_invoke(qa_targets *targets, qa_actor_id actor, qa_actor_id other,
                       qa_actor_id activator, qa_error *error) {
    if (!targets)
        return fail(error, "Target invocation requires the shared router");
    const qa_target_binding *entry = binding(targets, actor);
    if (!entry || !entry->use)
        return true;
    use_invocation use = {targets, *entry, targets->binding_serial[actor.slot], other, activator};
    return qa_session_invoke(targets->options.session, actor, QA_INVOKE_USE, invoke, &use, error);
}
static bool named(const qa_targets *targets, qa_string_id id, const char *text) {
    const char *value = qa_strings_cstr(qa_session_strings(targets->options.session), id);
    return value && !strcmp(value, text);
}
static bool use_now(qa_targets *targets, qa_target_use request, qa_error *error) {
    bool q1 = request.dialect == QA_CLOCK_NETQUAKE || request.dialect == QA_CLOCK_QUAKEWORLD;
    bool q3 = request.dialect == QA_CLOCK_Q3;
    if (q3) {
        if (request.fields.shader_old && request.fields.shader_new) {
            if (!targets->options.remap_shader)
                return fail(error, "Authored shader target has no remap owner");
            if (!targets->options.remap_shader(targets->options.context, request.fields.shader_old,
                                               request.fields.shader_new, request.time_ns, error))
                return false;
        }
    } else if (request.fields.message) {
        if (!targets->options.message)
            return fail(error, "Authored target message has no source presentation owner");
        if (!targets->options.message(targets->options.context, &request, error))
            return false;
    }
    if (request.live_fields)
        (void)qa_targets_read(targets, request.source, &request.fields);
    if (!q3) {
        qa_actor_id *victims;
        size_t count;
        if (!snapshot(targets, request.fields.killtarget, &victims, &count, error))
            return false;
        for (size_t i = 0; i < count; ++i) {
            if (live(targets, victims[i]) &&
                !qa_session_release(targets->options.session, victims[i], error))
                return false;
            if (!q1 && !live(targets, request.source))
                return true;
        }
    }
    if (request.live_fields)
        (void)qa_targets_read(targets, request.source, &request.fields);
    if (q1) {
        qa_actor_id *actors;
        size_t count;
        if (!snapshot(targets, request.fields.target, &actors, &count, error))
            return false;
        for (size_t i = 0; i < count; ++i)
            if (live(targets, actors[i]) &&
                !qa_targets_invoke(targets, actors[i], request.source, request.activator, error))
                return false;
    } else {
        qa_target_cursor cursor = {0};
        qa_actor_id current;
        while (qa_targets_next(targets, request.fields.target, &cursor, &current)) {
            if (qa_actor_id_equal(current, request.source)) {
                if (targets->options.diagnostic)
                    targets->options.diagnostic(targets->options.context, request.source,
                                                "Entity targets itself");
            } else {
                qa_authored_target destination;
                bool skip_portal = !q3 && qa_targets_read(targets, current, &destination) &&
                                   named(targets, destination.classname, "func_areaportal") &&
                                   (named(targets, request.fields.classname, "func_door") ||
                                    named(targets, request.fields.classname, "func_door_rotating"));
                if (!skip_portal &&
                    !qa_targets_invoke(targets, current, request.source, request.activator, error))
                    return false;
            }
            if (!live(targets, request.source)) {
                if (q3 && targets->options.diagnostic)
                    targets->options.diagnostic(targets->options.context, request.source,
                                                "Entity removed while using targets");
                break;
            }
            if (request.live_fields)
                (void)qa_targets_read(targets, request.source, &request.fields);
        }
    }
    return true;
}
static bool prepare_request(const qa_targets *targets, const qa_target_use *request,
                            qa_target_use *normalized, qa_error *error) {
    if (!request || request->dialect < QA_CLOCK_NETQUAKE || request->dialect > QA_CLOCK_Q3 ||
        !valid_fields(targets, &request->fields))
        return fail(error, "Invalid authored target request");
    *normalized = *request;
    normalize(targets, &normalized->fields);
    return true;
}
static bool execute_request(qa_targets *targets, qa_target_use request, qa_error *error) {
    ++targets->depth;
    bool ok = use_now(targets, request, error);
    if (!--targets->depth)
        qa_arena_reset(&targets->scratch);
    return ok;
}
bool qa_targets_use_now(qa_targets *targets, const qa_target_use *request, qa_error *error) {
    qa_target_use normalized;
    return prepare_request(targets, request, &normalized, error) &&
           execute_request(targets, normalized, error);
}
bool qa_targets_use_request(qa_targets *targets, const qa_target_use *request, qa_error *error) {
    qa_target_use normalized;
    if (!prepare_request(targets, request, &normalized, error))
        return false;
    if (normalized.dialect != QA_CLOCK_Q3 && normalized.fields.delay_seconds != 0) {
        if (!targets->options.defer)
            return fail(error, "Authored delayed use has no source scheduler owner");
        normalized.live_fields = false;
        return targets->options.defer(targets->options.context, &normalized, error);
    }
    return execute_request(targets, normalized, error);
}
bool qa_targets_use(qa_targets *targets, qa_actor_id source, qa_actor_id activator,
                    uint64_t time_ns, qa_error *error) {
    const qa_target_binding *entry = binding(targets, source);
    qa_target_use request = {
        .source = source, .activator = activator, .time_ns = time_ns, .live_fields = true};
    if (!entry || !qa_targets_read(targets, source, &request.fields))
        return fail(error, "Source has no authored target fields");
    request.dialect = entry->source;
    return qa_targets_use_request(targets, &request, error);
}
