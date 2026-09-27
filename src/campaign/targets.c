#include "qa/targets.h"
#include "qa/arena.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct target_index {
    qa_actor_id actor;
    qa_string_id name;
    uint32_t order;
} target_index;
struct qa_targets {
    qa_target_options options;
    qa_target_binding *bindings;
    target_index *index;
    qa_arena scratch;
    size_t count, capacity, depth;
    uint64_t actor_revision;
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
    return isfinite(fields->delay_seconds) && valid_string(targets, fields->classname) &&
           valid_string(targets, fields->targetname) && valid_string(targets, fields->target) &&
           valid_string(targets, fields->killtarget) && valid_string(targets, fields->message) &&
           valid_string(targets, fields->shader_old) && valid_string(targets, fields->shader_new);
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
    targets->index = calloc(targets->capacity, sizeof(*targets->index));
    if (!targets->bindings || !targets->index) {
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
    free(targets->index);
    free(targets->bindings);
    free(targets);
}
bool qa_targets_bind(qa_targets *targets, const qa_target_binding *entry, qa_error *error) {
    qa_authored_target fields;
    if (!entry || !entry->read || entry->actor.slot >= targets->capacity ||
        !live(targets, entry->actor) || entry->source < QA_CLOCK_NETQUAKE ||
        entry->source > QA_CLOCK_Q3 || !entry->read(entry->context, entry->actor, &fields) ||
        !valid_fields(targets, &fields))
        return fail(error, "Invalid authored target binding");
    targets->bindings[entry->actor.slot] = *entry;
    targets->dirty = true;
    return true;
}
void qa_targets_unbind(qa_targets *targets, qa_actor_id actor) {
    if (actor.slot < targets->capacity &&
        qa_actor_id_equal(targets->bindings[actor.slot].actor, actor)) {
        targets->bindings[actor.slot] = (qa_target_binding){0};
        targets->dirty = true;
    }
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
static int compare(const void *left, const void *right) {
    const target_index *a = left, *b = right;
    if (a->name != b->name)
        return a->name < b->name ? -1 : 1;
    if (a->order != b->order)
        return a->order < b->order ? -1 : 1;
    return a->actor.slot < b->actor.slot ? -1 : a->actor.slot > b->actor.slot;
}
static void refresh(qa_targets *targets) {
    uint64_t revision = qa_actors_revision(qa_session_actors(targets->options.session));
    if (!targets->dirty && targets->actor_revision == revision)
        return;
    targets->count = 0;
    for (size_t i = 0; i < targets->capacity; ++i) {
        const qa_target_binding *entry = &targets->bindings[i];
        const qa_actor_record *actor =
            qa_actors_get(qa_session_actors(targets->options.session), entry->actor);
        qa_authored_target fields;
        if (!actor || !qa_targets_read(targets, entry->actor, &fields) || !fields.targetname)
            continue;
        targets->index[targets->count++] =
            (target_index){entry->actor, fields.targetname,
                           actor->has_source ? actor->source_slot : actor->id.slot};
    }
    if (targets->count > 1)
        qsort(targets->index, targets->count, sizeof(*targets->index), compare);
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
    qa_target_binding target;
    qa_actor_id source, activator;
} use_invocation;
static bool invoke(void *context, qa_session *session, qa_error *error) {
    (void)session;
    use_invocation *use = context;
    return use->target.use(use->target.context, use->target.actor, use->source, use->activator,
                           error);
}
static bool dispatch(qa_targets *targets, qa_actor_id actor, const qa_target_use *request,
                     qa_error *error) {
    const qa_target_binding *entry = binding(targets, actor);
    if (!entry || !entry->use)
        return true;
    use_invocation use = {*entry, request->source, request->activator};
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
            if (live(targets, actors[i]) && !dispatch(targets, actors[i], &request, error))
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
                if (!skip_portal && !dispatch(targets, current, &request, error))
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
bool qa_targets_use_now(qa_targets *targets, const qa_target_use *request, qa_error *error) {
    if (!request || request->dialect < QA_CLOCK_NETQUAKE || request->dialect > QA_CLOCK_Q3 ||
        !valid_fields(targets, &request->fields))
        return fail(error, "Invalid authored target request");
    qa_target_use normalized = *request;
    normalize(targets, &normalized.fields);
    ++targets->depth;
    bool ok = use_now(targets, normalized, error);
    if (!--targets->depth)
        qa_arena_reset(&targets->scratch);
    return ok;
}
bool qa_targets_use(qa_targets *targets, qa_actor_id source, qa_actor_id activator,
                    uint64_t time_ns, qa_error *error) {
    const qa_target_binding *entry = binding(targets, source);
    qa_target_use request = {
        .source = source, .activator = activator, .time_ns = time_ns, .live_fields = true};
    if (!entry || !qa_targets_read(targets, source, &request.fields))
        return fail(error, "Source has no authored target fields");
    request.dialect = entry->source;
    if (!valid_fields(targets, &request.fields))
        return fail(error, "Invalid authored target fields");
    if (request.dialect != QA_CLOCK_Q3 && request.fields.delay_seconds != 0) {
        if (!targets->options.defer)
            return fail(error, "Authored delayed use has no source scheduler owner");
        request.live_fields = false;
        return targets->options.defer(targets->options.context, &request, error);
    }
    return qa_targets_use_now(targets, &request, error);
}
