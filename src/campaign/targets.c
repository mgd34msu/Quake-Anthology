#include "targets_internal.h"
#include "qa/text.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool qa_targets_bind_field_keys(qa_strings *strings, qa_string_id keys[QA_TARGET_KEY_TOTAL], qa_error *error) {
    static const char *const names[] = {
#define QA_TARGET_KEY_TEXT(key, text) text,
        QA_TARGET_KEY_LIST(QA_TARGET_KEY_TEXT)
#undef QA_TARGET_KEY_TEXT
    };
    for (unsigned i = 0; i < QA_TARGET_KEY_TOTAL; ++i)
        if (!qa_strings_intern_cstr(strings, names[i], &keys[i], error)) return false;
    return true;
}
const qa_string_id *qa_targets_field_keys(const qa_targets *targets) { return targets->field_keys; }
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
    /* Source shader fields distinguish an absent pointer from a present empty
     * byte path. AddRemap accepts the latter as a real table key or value. */
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
    if (!qa_targets_bind_field_keys(qa_session_strings(options->session), targets->field_keys, error)) {
        qa_targets_destroy(targets);
        return NULL;
    }
    static const char *const runtime_names[TARGET_NAME_COUNT] = {
        "monster_zombie", "func_areaportal", "func_door", "func_door_rotating"
    };
    for (unsigned i = 0; i < TARGET_NAME_COUNT; ++i)
        if (!qa_strings_intern_cstr(qa_session_strings(options->session), runtime_names[i],
                                    &targets->runtime_names[i], error)) {
            qa_targets_destroy(targets);
            return NULL;
        }
    targets->capacity = qa_actors_capacity(qa_session_actors(options->session));
    targets->bindings = calloc(targets->capacity, sizeof(*targets->bindings));
    targets->monsters = calloc(targets->capacity, sizeof(*targets->monsters));
    targets->binding_serial = calloc(targets->capacity, sizeof(*targets->binding_serial));
    targets->index = calloc(targets->capacity, sizeof(*targets->index));
    targets->indexed = calloc(targets->capacity, sizeof(*targets->indexed));
    targets->authored = calloc(targets->capacity, sizeof(*targets->authored));
    if (!targets->bindings || !targets->monsters || !targets->binding_serial || !targets->index || !targets->indexed || !targets->authored) {
        qa_targets_destroy(targets);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating target index");
        return NULL;
    }
    return targets;
}
void qa_targets_destroy(qa_targets *targets) {
    if (!targets)
        return;
    qa_arena_destroy(&targets->scratch);
    if (targets->monsters)
        for (size_t i = 0; i < targets->capacity; ++i) {
            if (targets->monsters[i]) free(targets->monsters[i]->authored.barriers);
            free(targets->monsters[i]);
        }
    free(targets->monsters);
    free(targets->authored);
    free(targets->index);
    free(targets->indexed);
    free(targets->bindings);
    free(targets->binding_serial);
    free(targets);
}
static bool monster_read(void *opaque, qa_actor_id actor, qa_authored_target *out) {
    target_monster *monster = opaque;
    if (!qa_actor_id_equal(monster->native.actor, actor)) return false;
    *out = monster->authored.fields;
    return true;
}
static bool monster_use(void *opaque, qa_actor_id actor, qa_actor_id other,
    qa_actor_id activator, qa_error *error) {
    target_monster *monster = opaque;
    qa_target_binding native = monster->native;
    return !native.use || native.use(native.context, actor, other, activator, error);
}
static bool monster_field(void *opaque, qa_actor_id actor, qa_string_id key, qa_target_field *out) {
    target_monster *monster = opaque;
    const qa_authored_monster *row = &monster->authored;
    static const qa_target_key keys[] = {QA_TARGET_KEY_CLASSNAME, QA_TARGET_KEY_TARGETNAME, QA_TARGET_KEY_TARGET, QA_TARGET_KEY_KILLTARGET, QA_TARGET_KEY_MESSAGE,
        QA_TARGET_KEY_DEATHTARGET, QA_TARGET_KEY_ITEM, QA_TARGET_KEY_ITEMTARGET, QA_TARGET_KEY_HEALTHTARGET, QA_TARGET_KEY_COMBATTARGET};
    const qa_string_id values[] = {row->fields.classname, row->fields.targetname,
        row->fields.target, row->fields.killtarget, row->fields.message,
        row->death_target, row->drop_item, row->item_target, row->health_target, row->combat_target};
    for (size_t i = 0; i < sizeof(keys) / sizeof(*keys); ++i)
        if ((key == monster->targets->field_keys[keys[i]])) {
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_TEXT, .value.text = values[i]};
            return true;
        }
    if ((key == monster->targets->field_keys[QA_TARGET_KEY_SPAWNFLAGS]) || (key == monster->targets->field_keys[QA_TARGET_KEY_DELAY])) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
            .value.number = (key == monster->targets->field_keys[QA_TARGET_KEY_SPAWNFLAGS]) ? (double)row->spawnflags : (double)row->fields.delay_seconds};
        return true;
    }
    return monster->native.field && monster->native.field(monster->native.context, actor, key, out);
}
static bool monster_targetname(void *opaque, qa_actor_id actor, qa_string_id name, qa_error *error) {
    (void)error;
    target_monster *monster = opaque;
    monster->authored.fields.targetname = name;
    qa_targets_changed(monster->targets, actor);
    return true;
}
static bool monster_target(void *opaque, qa_actor_id actor, qa_string_id name, qa_error *error) {
    (void)actor; (void)error;
    ((target_monster *)opaque)->authored.fields.target = name;
    return true;
}
static bool monster_delay(void *opaque, qa_actor_id actor, float seconds, qa_error *error) {
    (void)actor; (void)error;
    ((target_monster *)opaque)->authored.fields.delay_seconds = seconds;
    return true;
}
static bool monster_before_remove(void *opaque, qa_actor_id actor, qa_error *error) {
    qa_target_binding native = ((target_monster *)opaque)->native;
    return !native.before_remove || native.before_remove(native.context, actor, error);
}
static qa_target_binding monster_binding(target_monster *monster) {
    return (qa_target_binding){.actor = monster->native.actor, .source = monster->authored.source,
        .context = monster, .read = monster_read, .use = monster_use, .field = monster_field,
        .set_targetname = monster_targetname, .set_target = monster_target, .set_delay = monster_delay,
        .before_remove = monster_before_remove};
}
void qa_targets_monsters_configure(qa_targets *targets, void *context,
    bool (*resolve)(void *, qa_actor_owner, qa_monster_mission *, qa_error *)) {
    targets->monster_context = context;
    targets->monster_resolve = resolve;
}
qa_authored_monster *qa_targets_monster(qa_targets *targets, qa_actor_id actor) {
    if (!targets || actor.slot >= targets->capacity || !live(targets, actor)) return NULL;
    target_monster *row = targets->monsters[actor.slot];
    return row && qa_actor_id_equal(row->native.actor, actor) ? &row->authored : NULL;
}
void qa_targets_monster_route(qa_targets *targets, qa_actor_id actor, qa_string_id name, qa_actor_id goal) {
    qa_authored_monster *row = qa_targets_monster(targets, actor);
    if (!row) return;
    row->route = name; row->route_goal = goal; row->route_resolved = true;
}
bool qa_targets_monster_lookup(void *opaque, qa_actor_id actor, qa_monster_mission *out) {
    qa_targets *targets = opaque;
    if (!qa_targets_monster(targets, actor)) return false;
    *out = targets->monsters[actor.slot]->mission;
    return true;
}
bool qa_targets_monster_admit(qa_targets *targets, qa_actor_id actor,
    const qa_authored_monster *authored, qa_error *error) {
    const qa_target_binding *native = targets ? binding(targets, actor) : NULL;
    if (!native || !authored || !authored->owner || !valid_fields(targets, &authored->fields))
        return fail(error, "Selected monster requires its real native binding and authored map fields");
    target_monster *row = calloc(1, sizeof(*row));
    if (!row) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating authored monster target"); return false; }
    *row = (target_monster){.targets = targets,
        .native = targets->monsters[actor.slot] ? targets->monsters[actor.slot]->native : *native,
        .authored = *authored};
    if (!targets->monster_resolve || !targets->monster_resolve(targets->monster_context,
        authored->owner, &row->mission, error)) { free(row); return false; }
    bool zombie = (authored->source == QA_RULESET_NETQUAKE || authored->source == QA_RULESET_QUAKEWORLD) &&
        authored->fields.classname == targets->runtime_names[TARGET_NAME_MONSTER_ZOMBIE];
    row->mission.ambush = (authored->spawnflags & (zombie ? 2u : 1u)) != 0;
    if (authored->barrier_count) {
        row->authored.barriers = malloc(authored->barrier_count * sizeof(*authored->barriers));
        if (!row->authored.barriers) { free(row); qa_error_set(error, QA_ERROR_MEMORY, 0, "retaining authored door encounter"); return false; }
        memcpy(row->authored.barriers, authored->barriers, authored->barrier_count * sizeof(*authored->barriers));
    }
    if (targets->monsters[actor.slot]) free(targets->monsters[actor.slot]->authored.barriers);
    free(targets->monsters[actor.slot]);
    targets->monsters[actor.slot] = row;
    targets->bindings[actor.slot] = monster_binding(row);
    qa_targets_changed(targets, actor);
    return true;
}
bool qa_targets_bind(qa_targets *targets, const qa_target_binding *entry, qa_error *error) {
    qa_authored_target fields;
    if (!targets || !entry || !entry->read || entry->actor.slot >= targets->capacity ||
        !live(targets, entry->actor) || entry->source < QA_RULESET_NETQUAKE ||
        entry->source > QA_RULESET_Q3 || !entry->read(entry->context, entry->actor, &fields) ||
        !valid_fields(targets, &fields))
        return fail(error, "Invalid authored target binding");
    if (targets->next_binding_serial == UINT64_MAX)
        return fail(error, "Authored target binding serial exhausted");
    target_monster *monster = targets->monsters[entry->actor.slot];
    if (monster && !qa_actor_id_equal(monster->native.actor, entry->actor)) {
        free(monster->authored.barriers); free(monster); targets->monsters[entry->actor.slot] = NULL; monster = NULL;
    }
    if (monster) monster->native = *entry;
    targets->bindings[entry->actor.slot] = monster ? monster_binding(monster) : *entry;
    targets->binding_serial[entry->actor.slot] = ++targets->next_binding_serial;
    qa_targets_changed(targets, entry->actor);
    return true;
}
void qa_targets_unbind(qa_targets *targets, qa_actor_id actor) {
    if (actor.slot < targets->capacity &&
        qa_actor_id_equal(targets->bindings[actor.slot].actor, actor)) {
        if (targets->monsters[actor.slot]) free(targets->monsters[actor.slot]->authored.barriers);
        free(targets->monsters[actor.slot]);
        targets->monsters[actor.slot] = NULL;
        targets->bindings[actor.slot] = (qa_target_binding){0};
        targets->binding_serial[actor.slot] = 0;
        qa_targets_changed(targets, actor);
    }
}
void qa_targets_unbind_context(qa_targets *targets, qa_actor_id actor, const void *context) {
    if (actor.slot < targets->capacity && (targets->bindings[actor.slot].context == context ||
        (targets->monsters[actor.slot] && targets->monsters[actor.slot]->native.context == context)))
        qa_targets_unbind(targets, actor);
}
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
    bool (*write)(void *, qa_actor_id, qa_string_id, qa_error *) =
        targetname ? before.set_targetname : before.set_target;
    bool ok = write(before.context, actor, nonempty(targets, name), error);
    if (targetname) qa_targets_changed(targets, actor);
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
bool qa_targets_set_delay(qa_targets *targets, qa_actor_id actor, float seconds, qa_error *error) {
    if (!targets || !isfinite(seconds))
        return fail(error, "Invalid authored target delay");
    const qa_target_binding *entry = binding(targets, actor);
    if (!entry || !entry->set_delay) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Authored target has no delay setter");
        return false;
    }
    qa_target_binding before = *entry;
    uint64_t serial = targets->binding_serial[actor.slot];
    bool ok = before.set_delay(before.context, actor, seconds, error);
    if (!ok)
        return false;
    entry = binding(targets, actor);
    if (!entry || targets->binding_serial[actor.slot] != serial) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Authored target owner changed during delay write");
        return false;
    }
    return true;
}
bool qa_targets_field(const qa_targets *targets, qa_actor_id actor, qa_string_id key,
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
bool qa_targets_number(const qa_targets *targets, qa_actor_id actor, qa_string_id key, double *out) {
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
bool qa_targets_vector(const qa_targets *targets, qa_actor_id actor, qa_string_id key,
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
static int compare(const target_index *a, const target_index *b) {
    if (a->name != b->name)
        return a->name < b->name ? -1 : 1;
    if (a->order != b->order)
        return a->order < b->order ? -1 : 1;
    return a->actor.slot < b->actor.slot ? -1 : a->actor.slot > b->actor.slot;
}
static int compare_authored(const authored_index *a, const authored_index *b) {
    if (a->order != b->order)
        return a->order < b->order ? -1 : 1;
    return a->slot < b->slot ? -1 : a->slot > b->slot;
}
static size_t index_at(const qa_targets *targets, const target_index *key) {
    size_t low = 0, high = targets->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (compare(&targets->index[mid], key) < 0)
            low = mid + 1;
        else
            high = mid;
    }
    return low;
}
static size_t authored_at(const qa_targets *targets, const authored_index *key) {
    size_t low = 0, high = targets->authored_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (compare_authored(&targets->authored[mid], key) < 0)
            low = mid + 1;
        else
            high = mid;
    }
    return low;
}
void qa_targets_changed(qa_targets *targets, qa_actor_id actor) {
    if (!targets || actor.slot >= targets->capacity)
        return;
    target_index before = targets->indexed[actor.slot], after = {0};
    const qa_target_binding *entry = binding(targets, actor);
    if (entry) {
        qa_authored_target fields;
        const qa_actor_record *record = qa_actors_get(qa_session_actors(targets->options.session), actor);
        if (qa_targets_read(targets, actor, &fields)) {
            uint32_t order = targets->monsters[actor.slot] ? targets->monsters[actor.slot]->authored.ordinal :
                record->has_source ? record->source_slot : actor.slot;
            after = (target_index){actor, fields.targetname, order};
        }
    } else if (!qa_actor_id_equal(before.actor, actor)) {
        return;
    }
    bool same_actor = qa_actor_id_equal(before.actor, after.actor);
    bool same_order = same_actor && before.order == after.order;
    bool same_name = same_order && before.name == after.name;
    if (same_name)
        return;
    if (!same_name && before.actor.registry && before.name) {
        size_t at = index_at(targets, &before);
        --targets->count;
        memmove(targets->index + at, targets->index + at + 1,
            (targets->count - at) * sizeof(*targets->index));
    }
    if (!same_order && before.actor.registry) {
        authored_index key = {before.order, actor.slot};
        size_t at = authored_at(targets, &key);
        --targets->authored_count;
        memmove(targets->authored + at, targets->authored + at + 1,
            (targets->authored_count - at) * sizeof(*targets->authored));
    }
    if (!same_name && after.actor.registry && after.name) {
        size_t at = index_at(targets, &after);
        memmove(targets->index + at + 1, targets->index + at,
            (targets->count - at) * sizeof(*targets->index));
        targets->index[at] = after;
        ++targets->count;
    }
    if (!same_order && after.actor.registry) {
        authored_index key = {after.order, actor.slot};
        size_t at = authored_at(targets, &key);
        memmove(targets->authored + at + 1, targets->authored + at,
            (targets->authored_count - at) * sizeof(*targets->authored));
        targets->authored[at] = key;
        ++targets->authored_count;
    }
    targets->indexed[actor.slot] = after;
}
static size_t lower(qa_targets *targets, qa_string_id name) {
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
bool qa_targets_next_authored(qa_targets *targets, qa_string_id classname, qa_target_cursor *cursor,
                              qa_actor_id *out) {
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
        if (classname && fields.classname != classname)
            continue;
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
static bool use_now(qa_targets *targets, qa_target_use request, qa_error *error) {
    bool q1 = request.dialect == QA_RULESET_NETQUAKE || request.dialect == QA_RULESET_QUAKEWORLD;
    bool q3 = request.dialect == QA_RULESET_Q3;
    const qa_target_binding *source_binding = q3 ? binding(targets, request.source) : NULL;
    uint64_t source_serial = source_binding ? targets->binding_serial[request.source.slot] : 0;
    if (q3) {
        if (request.fields.shader_old && request.fields.shader_new) {
            if (source_binding && source_binding->remap_shader) {
                qa_target_binding captured = *source_binding;
                if (!captured.remap_shader(captured.context, request.source,
                                            request.fields.shader_old, request.fields.shader_new,
                                            request.time_ns, error)) return false;
            } else {
                if (!targets->options.remap_shader)
                    return fail(error, "Authored shader target has no remap owner");
                if (!targets->options.remap_shader(targets->options.context,
                                                   request.fields.shader_old,
                                                   request.fields.shader_new,
                                                   request.time_ns, error)) return false;
            }
            if (source_serial && (!binding(targets, request.source) ||
                targets->binding_serial[request.source.slot] != source_serial)) return true;
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
            const qa_target_binding *entry = binding(targets, victims[i]);
            if (entry && entry->before_remove) {
                qa_target_binding current = *entry;
                if (!current.before_remove(current.context, victims[i], error))
                    return false;
            }
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
                                   (destination.classname == targets->runtime_names[TARGET_NAME_FUNC_AREAPORTAL]) &&
                                   ((request.fields.classname == targets->runtime_names[TARGET_NAME_FUNC_DOOR]) ||
                                    (request.fields.classname == targets->runtime_names[TARGET_NAME_FUNC_DOOR_ROTATING]));
                if (!skip_portal &&
                    !qa_targets_invoke(targets, current, request.source, request.activator, error))
                    return false;
            }
            if (!live(targets, request.source) ||
                (q3 && source_serial && (!binding(targets, request.source) ||
                 targets->binding_serial[request.source.slot] != source_serial))) {
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
    if (!request || request->dialect < QA_RULESET_NETQUAKE || request->dialect > QA_RULESET_Q3 ||
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
    if (normalized.dialect != QA_RULESET_Q3 && normalized.fields.delay_seconds != 0) {
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
