# B13 frozen source acceptance evidence

Snapshot HEAD: 9b1dfaef2d781f1246e244e039b97c01bdc8be1d. Includes dirty source frozen for audit. This packet contains explicit source selections, not a truncated full-source dump. Read scope and findings are recorded in docs/audit/q1-shared.md.

Goal and criteria:
{
  "id": "B13",
  "label": "Campaigns and authored interactions",
  "depends_on": [
    "B10",
    "B11",
    "B12"
  ],
  "goal": "Implement entity target graphs, mission gates, keys, sigils, bosses, hubs, revisits, authored mechanisms, and travel.",
  "criteria": [
    "Campaign code preserves authored obligations when actors or equipment are replaced; transitions have one owner."
  ],
  "status": "pending"
}

Reviewer assessment:
One shared target registry, typed reads, source ordering, deferred use and staged immutable unit storage are implemented. This cannot fulfill authored campaign obligations while required Q1 map classes (func_spawn/small, rotate controllers, Rogue time/plat/hazard controllers, addon brush/trigger modules) are absent from native classification. Q1 full private-state checkpoint/restore is missing, so unit storage of already encoded bytes cannot preserve its continuations. Application replacement-obligation and travel wiring are absent. Spawn endpoint bug was corrected independently in c9bf514 to allow RNG == 1. No runtime qualification was attempted.

Snapshot selected SHA256 hashes:
6b1d901ba8b416faa382912b1b7296a276c7c4d9dbd8c1030e3960da4e5eaa5b  src/gameplay/q1/runtime.c
376d43df9d7812bd88873b2ecbe065902ee8bb24250f9e5bf37aa99a58d57d8d  src/gameplay/q1/queries.c
8c36feb2b149d7c98062f265351167d6c526a1f185a4813ecbb80ec1b73409b5  src/gameplay/q1/maps/runtime.c
eee0e959ad883ab18796a346de634b5d181333dbd3785688f365edaaa26dd17f  src/gameplay/policies.c
1a4be8c0b34459135d887f664dcb5ee5fe61f51d6793d132fb2f680032019517  src/gameplay/armor.c
33909f58875e89f0bfb43c1c851c26f0f3d87a086f169299dcc72777018d69cc  src/movement/common.c
c0b1c679af11906858a9e26211e7e3399cec31849fc29f1050c10f2af1272fc8  src/campaign/targets.c
1e48a455cf33dca65deefb3661ff1c8ae77c33f17b034302de64b63b97a874fa  src/campaign/q1/spawn.c
e5997203de73384986f6ffe9e3b6396651a6ba05cd09846cec5c8aee61c69f7f  src/main.c
915c4c50da8fcb60ed206f09f2d325eb7755f4c6f3f48b1c3d97d83d2eb98bae  docs/dependencies.json


## Source selection: cat src/campaign/targets.c

```text
#include "qa/targets.h"
#include "qa/arena.h"
#include "qa/text.h"
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
    target_index *index;
    authored_index *authored;
    qa_arena scratch;
    size_t count, authored_count, capacity, depth;
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
    targets->index = calloc(targets->capacity, sizeof(*targets->index));
    targets->authored = calloc(targets->capacity, sizeof(*targets->authored));
    if (!targets->bindings || !targets->index || !targets->authored) {
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
```

## Source selection: cat src/campaign/unit.c

```text
#include "qa/campaign.h"
#include <stdlib.h>
#include <string.h>

struct qa_campaign_world {
    size_t references;
    qa_campaign_location location;
    qa_buffer bytes;
};
struct qa_campaign_unit {
    qa_strings *strings;
    qa_campaign_unit_checkpoint state;
    uint64_t revision;
};
struct qa_campaign_visit {
    qa_campaign_unit *unit;
    qa_campaign_unit_checkpoint candidate;
    qa_campaign_world *restore;
    uint64_t revision;
    bool committed;
};
static bool fail(qa_error *error, qa_status code, const char *message) {
    qa_error_set(error, code, 0, "%s", message);
    return false;
}
static bool valid_id(const qa_strings *strings, qa_string_id id) {
    qa_bytes text = qa_strings_text(strings, id);
    return text.size && !memchr(text.data, 0, text.size);
}
static bool valid_location(const qa_strings *strings, qa_campaign_location location) {
    return valid_id(strings, location.content) && valid_id(strings, location.map);
}
bool qa_campaign_location_equal(qa_campaign_location a, qa_campaign_location b) {
    return a.content == b.content && a.map == b.map;
}
bool qa_campaign_location_make(qa_strings *strings, qa_string_id content, qa_bytes map,
                               qa_campaign_location *out, qa_error *error) {
    if (!strings || !out || !valid_id(strings, content) || !map.data || !map.size ||
        memchr(map.data, 0, map.size))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid campaign location");
    if (map.size >= 5 && !memcmp(map.data, "maps/", 5)) {
        map.data += 5;
        map.size -= 5;
    }
    if (map.size >= 4 && !memcmp(map.data + map.size - 4, ".bsp", 4))
        map.size -= 4;
    if (!map.size)
        return fail(error, QA_ERROR_ARGUMENT, "Empty campaign map name");
    qa_string_id name;
    if (!qa_strings_intern(strings, map, &name, error))
        return false;
    *out = (qa_campaign_location){content, name};
    return true;
}
bool qa_campaign_world_create(qa_campaign_location location, qa_bytes bytes,
                              qa_campaign_world_validate validate, void *context,
                              qa_campaign_world **out, qa_error *error) {
    if (!bytes.data || !bytes.size)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid departed campaign world");
    qa_buffer copy = {.data = malloc(bytes.size), .size = bytes.size};
    if (!copy.data)
        return fail(error, QA_ERROR_MEMORY, "Retaining campaign snapshot");
    memcpy(copy.data, bytes.data, bytes.size);
    bool ok = qa_campaign_world_take(location, &copy, validate, context, out, error);
    qa_buffer_free(&copy);
    return ok;
}
void qa_campaign_world_retain(qa_campaign_world *world) {
    if (world)
        ++world->references;
}
bool qa_campaign_world_take(qa_campaign_location location, qa_buffer *bytes,
                            qa_campaign_world_validate validate, void *context,
                            qa_campaign_world **out, qa_error *error) {
    if (!out || !validate || !location.content || !location.map || !bytes || !bytes->data ||
        !bytes->size)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid departed campaign world");
    if (!validate(context, (qa_bytes){bytes->data, bytes->size}, location, error))
        return false;
    qa_campaign_world *world = calloc(1, sizeof(*world));
    if (!world)
        return fail(error, QA_ERROR_MEMORY, "Allocating campaign world");
    world->bytes = *bytes;
    *bytes = (qa_buffer){0};
    world->references = 1;
    world->location = location;
    *out = world;
    return true;
}
void qa_campaign_world_release(qa_campaign_world *world) {
    if (world && !--world->references) {
        qa_buffer_free(&world->bytes);
        free(world);
    }
}
qa_campaign_location qa_campaign_world_location(const qa_campaign_world *world) {
    return world->location;
}
qa_bytes qa_campaign_world_bytes(const qa_campaign_world *world) {
    return (qa_bytes){world->bytes.data, world->bytes.size};
}
qa_campaign_unit *qa_campaign_unit_create(qa_strings *strings, qa_error *error) {
    if (!strings) {
        fail(error, QA_ERROR_ARGUMENT, "Campaign unit requires session strings");
        return NULL;
    }
    qa_campaign_unit *unit = calloc(1, sizeof(*unit));
    if (!unit) {
        fail(error, QA_ERROR_MEMORY, "Allocating campaign unit");
        return NULL;
    }
    unit->strings = strings;
    return unit;
}
void qa_campaign_unit_checkpoint_free(qa_campaign_unit_checkpoint *state) {
    if (!state)
        return;
    for (size_t i = 0; i < state->count; ++i)
        qa_campaign_world_release(state->worlds[i]);
    free(state->worlds);
    *state = (qa_campaign_unit_checkpoint){0};
}
void qa_campaign_unit_destroy(qa_campaign_unit *unit) {
    if (!unit)
        return;
    qa_campaign_unit_checkpoint_free(&unit->state);
    free(unit);
}
bool qa_campaign_unit_current(const qa_campaign_unit *unit, qa_campaign_location *out) {
    if (!unit->state.has_current)
        return false;
    *out = unit->state.current;
    return true;
}
static bool reserve(qa_campaign_unit_checkpoint *out, size_t capacity, qa_error *error) {
    if (capacity > SIZE_MAX / sizeof(*out->worlds))
        return fail(error, QA_ERROR_MEMORY, "Campaign world count overflow");
    out->worlds = capacity ? malloc(capacity * sizeof(*out->worlds)) : NULL;
    return !capacity || out->worlds ||
           fail(error, QA_ERROR_MEMORY, "Allocating campaign world handles");
}
bool qa_campaign_unit_capture(const qa_campaign_unit *unit, qa_campaign_unit_checkpoint *out,
                              qa_error *error) {
    qa_campaign_unit_checkpoint copy = {.has_current = unit->state.has_current,
                                        .current = unit->state.current};
    if (!reserve(&copy, unit->state.count, error))
        return false;
    for (size_t i = 0; i < unit->state.count; ++i) {
        qa_campaign_world *world = unit->state.worlds[i];
        qa_campaign_world_retain(world);
        copy.worlds[copy.count++] = world;
    }
    *out = copy;
    return true;
}
bool qa_campaign_unit_stage(qa_campaign_unit *unit, qa_campaign_location destination, bool new_unit,
                            qa_campaign_world *departure, qa_campaign_visit **out,
                            qa_error *error) {
    if (!unit || !out || !valid_location(unit->strings, destination) ||
        (departure &&
         (!unit->state.has_current || !valid_location(unit->strings, departure->location) ||
          !qa_campaign_location_equal(unit->state.current, departure->location))))
        return fail(error, QA_ERROR_ARGUMENT, "Campaign departure does not match the active world");
    if (unit->revision == UINT64_MAX)
        return fail(error, QA_ERROR_ARGUMENT, "Campaign revision exhausted");
    qa_campaign_visit *visit = calloc(1, sizeof(*visit));
    if (!visit)
        return fail(error, QA_ERROR_MEMORY, "Allocating campaign visit");
    visit->unit = unit;
    visit->revision = unit->revision;
    visit->candidate.has_current = true;
    visit->candidate.current = destination;
    bool reset =
        new_unit || (unit->state.has_current && unit->state.current.content != destination.content);
    size_t capacity = reset ? 0 : unit->state.count + (departure ? 1u : 0u);
    if (capacity < unit->state.count && !reset) {
        free(visit);
        return fail(error, QA_ERROR_MEMORY, "Campaign visit count overflow");
    }
    if (!reserve(&visit->candidate, capacity, error)) {
        free(visit);
        return false;
    }
    if (!reset) {
        for (size_t i = 0; i < unit->state.count; ++i) {
            qa_campaign_world *world = unit->state.worlds[i];
            if (departure && qa_campaign_location_equal(world->location, departure->location))
                continue;
            qa_campaign_world_retain(world);
            if (qa_campaign_location_equal(world->location, destination))
                visit->restore = world;
            else
                visit->candidate.worlds[visit->candidate.count++] = world;
        }
        if (departure) {
            qa_campaign_world_retain(departure);
            if (qa_campaign_location_equal(departure->location, destination)) {
                qa_campaign_world_release(visit->restore);
                visit->restore = departure;
            } else
                visit->candidate.worlds[visit->candidate.count++] = departure;
        }
    }
    *out = visit;
    return true;
}
const qa_campaign_world *qa_campaign_visit_restore(const qa_campaign_visit *visit) {
    return visit->restore;
}
bool qa_campaign_visit_commit(qa_campaign_visit *visit, qa_error *error) {
    if (!visit || visit->committed || visit->revision != visit->unit->revision)
        return fail(error, QA_ERROR_ARGUMENT, "Campaign visit was superseded or already published");
    qa_campaign_unit_checkpoint previous = visit->unit->state;
    visit->unit->state = visit->candidate;
    visit->candidate = (qa_campaign_unit_checkpoint){0};
    ++visit->unit->revision;
    visit->committed = true;
    qa_campaign_unit_checkpoint_free(&previous);
    return true;
}
void qa_campaign_visit_destroy(qa_campaign_visit *visit) {
    if (!visit)
        return;
    qa_campaign_unit_checkpoint_free(&visit->candidate);
    qa_campaign_world_release(visit->restore);
    free(visit);
}
bool qa_campaign_unit_restore(qa_campaign_unit *unit, const qa_campaign_unit_checkpoint *state,
                              qa_error *error) {
    if (!unit || !state || unit->revision == UINT64_MAX ||
        (state->has_current ? !valid_location(unit->strings, state->current) : state->count != 0) ||
        (state->count && !state->worlds))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid campaign unit checkpoint");
    for (size_t i = 0; i < state->count; ++i) {
        const qa_campaign_world *world = state->worlds[i];
        if (!world || !valid_location(unit->strings, world->location) ||
            world->location.content != state->current.content ||
            qa_campaign_location_equal(world->location, state->current))
            return fail(error, QA_ERROR_FORMAT, "Invalid visited campaign world");
        for (size_t j = 0; j < i; ++j)
            if (qa_campaign_location_equal(world->location, state->worlds[j]->location))
                return fail(error, QA_ERROR_FORMAT, "Duplicate visited campaign world");
    }
    qa_campaign_unit_checkpoint candidate = {.has_current = state->has_current,
                                             .current = state->current};
    if (!reserve(&candidate, state->count, error))
        return false;
    for (size_t i = 0; i < state->count; ++i) {
        qa_campaign_world_retain(state->worlds[i]);
        candidate.worlds[candidate.count++] = state->worlds[i];
    }
    qa_campaign_unit_checkpoint_free(&unit->state);
    unit->state = candidate;
    ++unit->revision;
    return true;
}
```

## Source selection: cat src/campaign/travel.c

```text
#include "qa/campaign.h"
#include <stdlib.h>
#include <string.h>

static bool invalid(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message);
    return false;
}
static bool word(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
}
static bool classify(const char *name, qa_travel_kind *kind) {
    if (!*name || *name == '/' || strstr(name, "//"))
        return false;
    const char *at = name;
    while (*at && (word((unsigned char)*at) || *at == '/'))
        ++at;
    if (at == name)
        return false;
    if (!*at)
        *kind = QA_TRAVEL_MAP;
    else if (!strcmp(at, ".cin"))
        *kind = QA_TRAVEL_CINEMATIC;
    else if (!strcmp(at, ".pcx"))
        *kind = QA_TRAVEL_PICTURE;
    else if (!strcmp(at, ".dm2"))
        *kind = QA_TRAVEL_DEMO;
    else
        return false;
    return true;
}
void qa_travel_route_free(qa_travel_route *route) {
    if (!route)
        return;
    free(route->targets);
    free(route->storage);
    *route = (qa_travel_route){0};
}
bool qa_q2_travel_parse(const char *expression, qa_travel_route *out, qa_error *error) {
    if (!expression || !out || !*expression)
        return invalid(error, "Q2 travel has no destination");
    size_t length = strlen(expression), count = 1;
    for (size_t i = 0; i < length; ++i)
        if (expression[i] == '+')
            ++count;
    if (count > SIZE_MAX / sizeof(qa_travel_target))
        return invalid(error, "Q2 travel chain overflow");
    qa_travel_route route = {.count = count};
    route.storage = malloc(length + 1);
    route.targets = calloc(count, sizeof(*route.targets));
    if (!route.storage || !route.targets) {
        qa_travel_route_free(&route);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q2 travel route");
        return false;
    }
    memcpy(route.storage, expression, length + 1);
    char *part = route.storage;
    for (size_t i = 0; i < count; ++i) {
        char *next = strchr(part, '+');
        if (next)
            *next++ = 0;
        if (!*part) {
            invalid(error, "Q2 travel has an empty destination");
            goto fail;
        }
        char *spawn = strchr(part, '$');
        if (spawn)
            *spawn++ = 0;
        qa_travel_target *target = &route.targets[i];
        target->new_unit = *part == '*';
        target->name = part + (target->new_unit ? 1 : 0);
        target->spawn_point = spawn ? spawn : "";
        if (!classify(target->name, &target->kind)) {
            invalid(error, "Invalid Q2 travel destination");
            goto fail;
        }
        for (const unsigned char *at = (const unsigned char *)target->spawn_point; *at; ++at)
            if (!word(*at)) {
                invalid(error, "Invalid Q2 travel spawn point");
                goto fail;
            }
        part = next;
    }
    *out = route;
    return true;
fail:
    qa_travel_route_free(&route);
    return false;
}
bool qa_q2_nextserver(const qa_travel_route *route, size_t current, qa_buffer *out,
                      qa_error *error) {
    if (!route || current >= route->count || !out)
        return invalid(error, "Invalid next-server route");
    size_t size = current + 1 == route->count ? 0 : 10;
    for (size_t i = current + 1; i < route->count; ++i) {
        const qa_travel_target *target = &route->targets[i];
        qa_travel_kind kind;
        if (!target->name || !target->spawn_point || !classify(target->name, &kind) ||
            kind != target->kind)
            return invalid(error, "Invalid next-server target");
        for (const unsigned char *at = (const unsigned char *)target->spawn_point; *at; ++at)
            if (!word(*at))
                return invalid(error, "Invalid next-server spawn point");
        size_t name = strlen(target->name), spawn = strlen(target->spawn_point);
        if (size > SIZE_MAX - 5 || name > SIZE_MAX - size - 5 || spawn > SIZE_MAX - size - name - 5)
            return invalid(error, "Next-server text overflow");
        size += name + spawn + (target->new_unit ? 1u : 0u) + (spawn ? 1u : 0u) +
                (i > current + 1 ? 1u : 0u);
    }
    char *text = malloc(size + 1);
    if (!text) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating next-server command");
        return false;
    }
    size_t at = 0;
    if (size) {
        memcpy(text, "gamemap \"", 9);
        at = 9;
        for (size_t i = current + 1; i < route->count; ++i) {
            const qa_travel_target *target = &route->targets[i];
            if (i > current + 1)
                text[at++] = '+';
            if (target->new_unit)
                text[at++] = '*';
            size_t count = strlen(target->name);
            memcpy(text + at, target->name, count);
            at += count;
            if (*target->spawn_point) {
                text[at++] = '$';
                count = strlen(target->spawn_point);
                memcpy(text + at, target->spawn_point, count);
                at += count;
            }
        }
        text[at++] = '"';
    }
    text[at] = 0;
    *out = (qa_buffer){(uint8_t *)text, at};
    return true;
}
```

## Source selection: sed -n '140,260p' src/campaign/q1/spawn.c

```text
                                .shape.kind = QA_SHAPE_POINT,
                                .pass_actor = point,
                                .policy = {.family = QA_COLLISION_Q1,
                                           .q1_move = QA_Q1_MOVE_NO_MONSTERS,
                                           .q1_hull = -1}};
        qa_trace_result trace;
        if (!qa_world_trace(selector->options.services.world, &query, &trace, error))
            return false;
        if (trace.fraction >= 1) {
            *out = true;
            break;
        }
    }
    return true;
}
static qa_actor_id first(const qa_q1_spawn_selector *selector, const qa_q1_spawn_point *points,
                         size_t count, qa_q1_spawn_kind kind, size_t start) {
    for (size_t i = start; i < count; ++i)
        if (points[i].kind == kind && live(selector, points[i].actor))
            return points[i].actor;
    return (qa_actor_id){0};
}
static bool random_choice(qa_q1_spawn_selector *selector, qa_actor_id *out, qa_error *error) {
    double random = selector->options.random(selector->options.context);
    if (!isfinite(random) || random < 0 || random > 1)
        return fail(error, "Invalid Q1 spawn random fraction");
    size_t count = selector->candidates.count;
    /* The original rounds over count-1, giving endpoints half the interior weight. */
    size_t index = (size_t)floor(random * (double)(count - 1) + 0.5);
    *out = selector->candidates.ids[index];
    return true;
}
static bool select_point(qa_q1_spawn_selector *selector, const qa_q1_spawn_point *points,
                         size_t count, bool force, qa_actor_id *out, qa_error *error) {
    *out = (qa_actor_id){0};
    for (size_t i = 0; i < selector->options.rule_count; ++i) {
        const qa_q1_spawn_rule *rule = &selector->rules[i];
        qa_q1_spawn_decision decision = QA_Q1_SPAWN_DELEGATE;
        qa_actor_id actor = {0};
        if (!rule->select(rule->context, force, &decision, &actor, error))
            return false;
        if (decision == QA_Q1_SPAWN_DEFERRED)
            return true;
        if (decision == QA_Q1_SPAWN_SELECTED) {
            if (!live(selector, actor))
                return fail(error, "Source spawn rule selected stale actor");
            *out = actor;
            return true;
        }
        if (decision != QA_Q1_SPAWN_DELEGATE)
            return fail(error, "Invalid source spawn decision");
    }
    qa_actor_id point = first(selector, points, count, QA_Q1_SPAWN_TEST, 0);
    if (point.registry) {
        *out = point;
        return true;
    }
    if (selector->options.coop) {
        size_t after = 0;
        for (size_t i = 0; i < count; ++i)
            if (qa_actor_id_equal(points[i].actor, selector->last)) {
                after = i + 1;
                break;
            }
        point = first(selector, points, count, QA_Q1_SPAWN_COOP, after);
        if (!point.registry)
            point = first(selector, points, count, QA_Q1_SPAWN_START, 0);
        if (point.registry) {
            selector->last = *out = point;
            return true;
        }
    } else if (selector->options.deathmatch) {
        qa_builtin_actor_snapshot *candidates = &selector->candidates;
        if (!qa_builtin_snapshot_reserve(candidates, count, error) ||
            !qa_builtin_players(&selector->options.services, &selector->players, error))
            return false;
        candidates->count = 0;
        for (size_t i = 0; i < count; ++i)
            if (points[i].kind == QA_Q1_SPAWN_DEATHMATCH && live(selector, points[i].actor))
                candidates->ids[candidates->count++] = points[i].actor;
        if (!candidates->count)
            return fail(error, "No info_player_deathmatch on level");
        if (!selector->options.rerelease) {
            size_t start = candidates->count - 1;
            for (size_t i = 0; i < candidates->count; ++i)
                if (qa_actor_id_equal(candidates->ids[i], selector->last)) {
                    start = i;
                    break;
                }
            for (size_t i = 0; i < candidates->count; ++i) {
                if (++start == candidates->count)
                    start = 0;
                point = candidates->ids[start];
                bool occupied = false;
                if (!qa_actor_id_equal(point, selector->last) &&
                    !nearby(selector, point, 32, false, &occupied, error))
                    return false;
                if (qa_actor_id_equal(point, selector->last) || !occupied) {
                    selector->last = *out = point;
                    return true;
                }
            }
            if (force)
                *out = candidates->ids[0];
            return true;
        }
        size_t all = candidates->count;
        for (unsigned pass = 0; pass < 2; ++pass) {
            candidates->count = 0;
            for (size_t i = count; i > 0; --i) {
                point = points[i - 1].actor;
                if (points[i - 1].kind != QA_Q1_SPAWN_DEATHMATCH || !live(selector, point))
                    continue;
                bool occupied, seen = false;
                if (!nearby(selector, point, pass == 0 ? 384 : 84, true, &occupied, error))
                    return false;
                if (occupied)
                    continue;
                if (pass == 0 && !visible(selector, point, &seen, error))
                    return false;
                if (!seen)
```

## Source selection: sed -n '270,405p' src/gameplay/q1/maps/runtime.c

```text
    state->volume = source->volume;
    state->duration = source->duration;
    state->distance = source->distance;
    state->initial_think = source->next_think_seconds;
    state->sounds = source->sounds;
    state->style = source->style;
    state->color_map = source->color_map;
    state->impulse = source->impulse;
    state->counter_value = source->counter_value;
    state->particle_color = source->particle_color;
    if (source->model && source->model[0] == '*') {
        const char *number = source->model + 1;
        char *end;
        errno = 0;
        unsigned long model = strtoul(number, &end, 10);
        if (*number < '0' || *number > '9' || *end || errno || model > UINT32_MAX)
            return q1_map_fail(error, "invalid Q1 inline model name");
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
            !qa_collision_model_bounds(qa_world_geometry(g->services.world), (uint32_t)model,
                                       &body.bounds, error) ||
            !qa_world_body_write(g->services.world, entity->id, &body, error))
            return false;
        state->has_inline_model = true;
        state->inline_model = (uint32_t)model;
    }
    return true;
}
static q1_map_kind classify(const char *name) {
    static const struct {
        const char *name;
        q1_map_kind kind;
    } classes[] = {{"worldspawn", Q1_MAP_WORLD},
                   {"func_wall", Q1_MAP_WALL},
                   {"func_door", Q1_MAP_DOOR},
                   {"func_button", Q1_MAP_BUTTON},
                   {"func_door_secret", Q1_MAP_SECRET_DOOR},
                   {"func_plat", Q1_MAP_PLAT},
                   {"func_train", Q1_MAP_TRAIN},
                   {"misc_teleporttrain", Q1_MAP_TRAIN},
                   {"func_episodegate", Q1_MAP_GATE},
                   {"func_bossgate", Q1_MAP_GATE},
                   {"func_illusionary", Q1_MAP_STATIC},
                   {"item_sigil", Q1_MAP_SIGIL},
                   {"trap_spikeshooter", Q1_MAP_SHOOTER},
                   {"trap_shooter", Q1_MAP_SHOOTER},
                   {"misc_fireball", Q1_MAP_FIREBALL_SOURCE},
                   {"air_bubbles", Q1_MAP_BUBBLES},
                   {"light_globe", Q1_MAP_STATIC},
                   {"light_torch_small_walltorch", Q1_MAP_STATIC},
                   {"light_flame_large_yellow", Q1_MAP_STATIC},
                   {"light_flame_small_yellow", Q1_MAP_STATIC},
                   {"light_flame_small_white", Q1_MAP_STATIC},
                   {"ambient_suck_wind", Q1_MAP_AMBIENT},
                   {"ambient_flouro_buzz", Q1_MAP_AMBIENT},
                   {"ambient_drip", Q1_MAP_AMBIENT},
                   {"ambient_thunder", Q1_MAP_AMBIENT},
                   {"ambient_light_buzz", Q1_MAP_AMBIENT},
                   {"ambient_swamp1", Q1_MAP_AMBIENT},
                   {"ambient_swamp2", Q1_MAP_AMBIENT},
                   {"viewthing", Q1_MAP_VIEW},
                   {"misc_noisemaker", Q1_MAP_NOISE},
                   {"event_lightning", Q1_MAP_LIGHTNING},
                   {"play_sound", Q1_MAP_SOUND},
                   {"play_sound_triggered", Q1_MAP_SOUND},
                   {"random_thunder", Q1_MAP_SOUND},
                   {"random_thunder_triggered", Q1_MAP_SOUND},
                   {"ambient_humming", Q1_MAP_HIP_AMBIENT},
                   {"ambient_rushing", Q1_MAP_HIP_AMBIENT},
                   {"ambient_running_water", Q1_MAP_HIP_AMBIENT},
                   {"ambient_fan_blowing", Q1_MAP_HIP_AMBIENT},
                   {"ambient_waterfall", Q1_MAP_HIP_AMBIENT},
                   {"ambient_riftpower", Q1_MAP_HIP_AMBIENT},
                   {"info_command", Q1_MAP_COMMAND},
                   {"effect_teleport", Q1_MAP_TELEPORT_EFFECT},
                   {"func_exploder", Q1_MAP_EXPLODER},
                   {"func_multi_exploder", Q1_MAP_EXPLODER},
                   {"func_rubble", Q1_MAP_RUBBLE_SOURCE},
                   {"func_rubble1", Q1_MAP_RUBBLE_SOURCE},
                   {"func_rubble2", Q1_MAP_RUBBLE_SOURCE},
                   {"func_rubble3", Q1_MAP_RUBBLE_SOURCE},
                   {"func_earthquake", Q1_MAP_EARTHQUAKE},
                   {"func_particlefield", Q1_MAP_PARTICLE_FIELD},
                   {"func_togglewall", Q1_MAP_TOGGLE_WALL},
                   {"wallsprite", Q1_MAP_WALL_SPRITE},
                   {"misc_sacrifice", Q1_MAP_SACRIFICE},
                   {"trigger_multiple", Q1_MAP_MULTI},
                   {"trigger_once", Q1_MAP_MULTI},
                   {"trigger_secret", Q1_MAP_MULTI},
                   {"trigger_counter", Q1_MAP_COUNTER},
                   {"trigger_relay", Q1_MAP_RELAY},
                   {"trigger_teleport", Q1_MAP_TELEPORT},
                   {"info_teleport_destination", Q1_MAP_DESTINATION},
                   {"trigger_hurt", Q1_MAP_HURT},
                   {"trigger_push", Q1_MAP_PUSH},
                   {"trigger_changelevel", Q1_MAP_CHANGELEVEL},
                   {"trigger_setskill", Q1_MAP_SETSKILL},
                   {"trigger_onlyregistered", Q1_MAP_REGISTERED},
                   {"trigger_monsterjump", Q1_MAP_MONSTERJUMP},
                   {"path_corner", Q1_MAP_PATH},
                   {"info_player_start", Q1_MAP_POINT},
                   {"info_player_start2", Q1_MAP_POINT},
                   {"info_player_coop", Q1_MAP_POINT},
                   {"info_player_deathmatch", Q1_MAP_POINT},
                   {"info_intermission", Q1_MAP_POINT},
                   {"info_notnull", Q1_MAP_POINT},
                   {"testplayerstart", Q1_MAP_POINT},
                   {"light", Q1_MAP_LIGHT},
                   {"light_fluoro", Q1_MAP_LIGHT},
                   {"light_fluorospark", Q1_MAP_LIGHT},
                   {"misc_explobox", Q1_MAP_BARREL},
                   {"misc_explobox2", Q1_MAP_BARREL}};
    for (size_t i = 0; i < sizeof(classes) / sizeof(*classes); ++i)
        if (!strcmp(name, classes[i].name))
            return classes[i].kind;
    return Q1_MAP_FIELDS;
}
bool q1_map_spawn(qa_q1_game *g, q1_actor *entity, const qa_q1_spawn *spawn, bool *handled,
                  qa_error *error) {
    q1_map_kind kind = classify(spawn->classname);
    *handled = kind != Q1_MAP_FIELDS;
    if (!*handled && !spawn->map_fields)
        return true;
    q1_map_state *state = q1_map_allocate(g, entity, error);
    if (!state || !fields(g, entity, spawn->map_fields, error))
        return false;
    state->kind = kind;
    if (!*handled)
        return true;
    entity->kind = Q1_MAP;
    if (kind == Q1_MAP_SACRIFICE)
        return q1_map_sacrifice_spawn(g, entity, error);
    if (q1_map_is_mover(kind))
        return q1_map_mover_spawn(g, entity, error);
    if (kind >= Q1_MAP_PARTICLE_FIELD)
        return q1_map_hip_particles_spawn(g, entity, error);
```

## Source selection: cat ../quake-typescript/src/content/q1/missionpacks/world/index.ts

```text
/* Official mission pack world entities over the shared Q1 source runtime. */
import type { ActorId } from "../../../../contracts/identity.ts";
import type { Vec3 } from "../../../../contracts/math.ts";
import type { Q1SourceFinale } from "../../base/rules.ts";
import type { Q1Actor } from "../../foundation/entity.ts";
import type { Q1EntityServices } from "../../foundation/entity-services.ts";
import type { Q1MissionPack } from "../types.ts";
import { registerHipnoticTriggers } from "./hipnotic-triggers.ts";
import { registerHipnoticTrain } from "./hipnotic-train.ts";
import { registerHipnoticRotation } from "./hipnotic-rotate.ts";
import { registerHipnoticMisc, earthquakeAfterPhysics } from "./hipnotic-misc.ts";
import { registerHipnoticParticles } from "./hipnotic-particles.ts";
import { registerHipnoticSpawn } from "./hipnotic-spawn.ts";
import { registerHipnoticHazards } from "./hipnotic-hazards.ts";
import { RogueRunes } from "./rogue-runes.ts";
import { registerRogueMisc } from "./rogue-misc.ts";
import { registerRogueTime, crashTimeMachine } from "./rogue-time.ts";
import { registerRoguePendulum } from "./rogue-pendulum.ts";
import { registerRoguePlats } from "./rogue-plats.ts";
import { registerRogueHazards, rogueEarthquake } from "./rogue-hazards.ts";
import { RogueTeams } from "./rogue-teams.ts";
import { registerMissionShooters } from "./shooters.ts";
import { registerMissionCampaign } from "./campaign.ts";
import { registerRogueEnding, startRogueEnding } from "./rogue-ending.ts";
import { RogueTag } from "./rogue-tag.ts";

export interface MissionpackWorldHooks {
  readonly charmer?: () => ActorId | null;
  readonly charm?: (entity: Q1Actor, charmer: ActorId) => undefined;
  readonly becomeDecoy?: (target: string, origin: Vec3) => Q1Actor;
  readonly presentFinale?: (result: Q1SourceFinale) => undefined;
  readonly gamecfg?: () => number;
  readonly teamColor?: (actor: ActorId) => number;
  readonly setTeamColor?: (actor: ActorId, team: number) => undefined;
  readonly addFrags?: (actor: ActorId, delta: number) => undefined;
  readonly frags?: (actor: ActorId) => number;
  readonly disconnect?: (actor: ActorId) => undefined;
  readonly playerFrame?: (actor: ActorId) => number;
  readonly playerName?: (actor: ActorId) => string;
}
export class Q1MissionpackWorld {
  private readonly runes: RogueRunes | null;
  private readonly teams: RogueTeams | null;
  private readonly tag: RogueTag | null;
  constructor(readonly game: Q1EntityServices, readonly pack: Q1MissionPack, readonly hooks: MissionpackWorldHooks = {}) {
    registerMissionShooters(game, pack);
    registerMissionCampaign(game, pack, hooks);
    if (pack === "hipnotic") {
      registerHipnoticTriggers(game); registerHipnoticTrain(game); registerHipnoticRotation(game); registerHipnoticMisc(game); registerHipnoticParticles(game); registerHipnoticSpawn(game, hooks); registerHipnoticHazards(game); this.runes = null; this.teams = null; this.tag = null;
    } else { registerRogueMisc(game); registerRogueTime(game); registerRogueEnding(game); registerRoguePendulum(game); registerRoguePlats(game); registerRogueHazards(game); this.runes = new RogueRunes(game, hooks.gamecfg ?? (() => 0)); this.teams = new RogueTeams(game, hooks); this.tag = new RogueTag(game, hooks); }
  }
  afterPhysics(actor: ActorId, _seconds: number): undefined { if (this.pack === "hipnotic") return earthquakeAfterPhysics(this.game, actor); if (this.game.world?.number("rogue:earthquake_active") === 1) rogueEarthquake(this.game, actor, this.game.world.number("rogue:earthquake_intensity")); this.teams?.frame(actor); this.runes?.frame(actor); return startRogueEnding(this.game, actor, this.hooks); }
  playerSpawned(actor: ActorId): undefined { return this.teams?.playerSpawned(actor); }
  dropCarriedFlag(actor: ActorId): undefined { return this.teams?.dropCarriedFlag(actor); }
  impulse(actor: ActorId, impulse: number): boolean { return this.teams?.impulse(actor, impulse) ?? false; }
  savedTeam(actor: ActorId): number { return this.teams?.team(actor) ?? 0; }
  selectSpawn(actor: ActorId): Q1Actor | undefined { return this.teams?.selectSpawn(actor); }
  tagScore(victim: ActorId, attacker: ActorId): number { return this.tag?.score(victim, attacker) ?? 1; }
  confirmedDamage(target: ActorId, attacker: ActorId | null): undefined { return this.teams?.confirmedDamage(target, attacker); }
  playerDied(actor: ActorId, attacker: ActorId | null = null): undefined { this.teams?.playerDied(actor, attacker); return this.runes?.drop(actor); }
  runeAttackDelay(actor: ActorId, delay: number): number { return this.runes?.attackDelay(actor, delay) ?? delay; }
  runeAttackSound(actor: ActorId): undefined { return this.runes?.attackSound(actor); }
  runeDamage(actor: ActorId, amount: number): number { return this.runes?.damage(actor, amount) ?? amount; }
  runeResistance(actor: ActorId, amount: number): number { return this.runes?.resistance(actor, amount) ?? amount; }
  hasRegenerationRune(actor: ActorId): boolean { return this.runes?.hasRegeneration(actor) ?? false; }
  crashTimeMachine(): undefined { return crashTimeMachine(this.game); }
}
export function registerMissionpackWorld(game: Q1EntityServices, pack: Q1MissionPack, hooks: MissionpackWorldHooks = {}): Q1MissionpackWorld { return new Q1MissionpackWorld(game, pack, hooks); }
```

## Source selection: rg -n 'checkpoint|capture|restore|serialize' include/qa/game_q1.h include/qa/game_q1_maps.h src/gameplay/q1

```text
include/qa/game_q1.h:276:bool qa_q1_mg3_progress_restore(qa_q1_game *, qa_actor_id, const qa_q1_mg3_progress *, qa_error *);
src/gameplay/q1/mg3_progress.c:108:bool qa_q1_mg3_progress_restore(qa_q1_game *g, qa_actor_id actor, const qa_q1_mg3_progress *state,
```

## Source selection: cat src/main.c

```text
#include "qa/archive.h"
#include "qa/bsp.h"

#include <stdio.h>
#include <string.h>

static void usage(FILE *stream)
{
    fputs("Quake Anthology native C engine\n"
          "Usage: quake-anthology --help | --version\n"
          "       quake-anthology --inspect-bsp FILE\n"
          "       quake-anthology --list ARCHIVE\n"
          "       quake-anthology --inspect-bsp ARCHIVE MEMBER\n"
          "The baseline engine is under construction.\n", stream);
}

static int report_error(const char *source, const qa_error *error)
{
    fprintf(stderr, "%s:%zu: %s\n", source, error->offset, error->message);
    return 1;
}

static int inspect_bsp(const char *name, qa_bytes bytes)
{
    qa_error error = {0};
    qa_bsp_view map;
    qa_entities entities = {0};
    if (!qa_bsp_open(bytes, &map, &error))
        return report_error(name, &error);
    qa_entity_syntax syntax = map.family == QA_BSP_Q3 ? QA_ENTITY_Q3 : QA_ENTITY_Q1;
    if (!qa_entities_parse(map.lumps[QA_BSP_ENTITIES].bytes, syntax,
                           &entities, &error))
        return report_error(name, &error);
    printf("format: %s\nentities: %zu\n", qa_bsp_format_name(map.format), entities.count);
    for (int kind = 0; kind < QA_BSP_LUMP_COUNT; ++kind) {
        const qa_bsp_lump *lump = &map.lumps[kind];
        if (lump->present)
            printf("%s: %zu bytes, %zu records\n",
                   qa_bsp_lump_name((qa_bsp_lump_kind)kind), lump->bytes.size,
                   qa_bsp_record_count(&map, (qa_bsp_lump_kind)kind));
    }
    printf("extensions: %u\ndiagnostics: %u\n", map.extension_count, map.diagnostics);
    qa_entities_free(&entities);
    return 0;
}

static int inspect_bsp_file(const char *path)
{
    qa_error error = {0};
    qa_buffer file = {0};
    if (!qa_file_read_all(path, &file, &error))
        return report_error(path, &error);
    int result = inspect_bsp(path, (qa_bytes){file.data, file.size});
    qa_buffer_free(&file);
    return result;
}

static int inspect_archive(const char *path, const char *member)
{
    qa_error error = {0};
    qa_archive *archive = NULL;
    if (!qa_archive_open_file(path, QA_ARCHIVE_AUTO, &archive, &error))
        return report_error(path, &error);
    int result = 0;
    if (member == NULL) {
        for (size_t i = 0; i < qa_archive_count(archive); ++i) {
            const qa_archive_entry *entry = qa_archive_entry_at(archive, i);
            printf("%zu\t%zu\t%s\n", entry->ordinal, entry->size, entry->path);
        }
    } else {
        const qa_archive_entry *entry = NULL;
        if (!qa_archive_find(archive, member, QA_ARCHIVE_EXACT, 0, &entry, &error)) {
            result = report_error(path, &error);
        } else if (entry == NULL) {
            fprintf(stderr, "%s: archive member not found: %s\n", path, member);
            result = 1;
        } else {
            qa_archive_data data = {0};
            if (!qa_archive_read(archive, entry->ordinal, &data, &error))
                result = report_error(path, &error);
            else
                result = inspect_bsp(member, data.bytes);
            qa_archive_data_free(&data);
        }
    }
    qa_archive_close(archive);
    return result;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("Quake Anthology %s (baseline development)\n", QA_VERSION);
        return 0;
    }
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        usage(stdout);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--inspect-bsp") == 0)
        return inspect_bsp_file(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--list") == 0)
        return inspect_archive(argv[2], NULL);
    if (argc == 4 && strcmp(argv[1], "--inspect-bsp") == 0)
        return inspect_archive(argv[2], argv[3]);
    usage(stderr);
    return 2;
}
```

