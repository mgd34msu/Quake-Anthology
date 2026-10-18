#include "guest_native_q2_targets.h"
#include "guest_native_q2_private.h"
#include "guest_native_q2_combat_state.h"
#include "control_frame.h"
#include "qa/native_observe.h"
#include "qa/pool.h"
#include <math.h>

enum { TARGET_CLASSNAME, TARGET_NAME, TARGET_TARGET, TARGET_KILLTARGET,
    TARGET_TEXT_COUNT };

typedef struct native_target_row {
    struct native_target_row *next, **previous;
    struct native_target_row *retired_next, **retired_previous;
    struct application_native_q2_targets *owner;
    application_q2_combat_actor source;
    qa_native_write_observer *watch;
    qa_native_address watched;
    qa_string_id names[TARGET_TEXT_COUNT];
    size_t storage_slot;
    unsigned calls;
    bool retired;
} native_target_row;

struct application_native_q2_targets {
    struct application_native_q2 *engine;
    application_q2_combat_profile *profile;
    qa_unified_frame_lease *storage;
    qa_pool rows;
    native_target_row **actors, *first, *retired;
    uint32_t capacity, watch_start, watch_bytes;
};

static qa_native_instance *instance(struct application_native_q2_targets *owner)
{
    return qa_native_host_instance(owner->engine->provider->state.native.host);
}

static qa_targets *targets(struct application_native_q2_targets *owner)
{
    return owner->engine->provider->application->targets;
}

static uint32_t text_offset(const application_q2_combat_profile *profile, unsigned field)
{
    switch (field) {
    case TARGET_CLASSNAME: return profile->classname;
    case TARGET_NAME: return profile->authored.targetname;
    case TARGET_TARGET: return profile->authored.target;
    default: return profile->authored.killtarget;
    }
}

static native_target_row *find(struct application_native_q2_targets *owner, qa_actor_id actor)
{
    native_target_row *row = owner && actor.slot < owner->capacity ? owner->actors[actor.slot] : NULL;
    return row && qa_actor_id_equal(row->source.actor, actor) ? row : NULL;
}

static bool collect(native_target_row *row, qa_error *error)
{
    if (!row->retired || row->calls) return true;
    if (!qa_native_unobserve_writes(row->watch, error)) return false;
    *row->previous = row->next;
    if (row->next) row->next->previous = row->previous;
    *row->retired_previous = row->retired_next;
    if (row->retired_next) row->retired_next->retired_previous = row->retired_previous;
    qa_pool_release(&row->owner->rows, row->storage_slot);
    return true;
}

static bool collect_retired(struct application_native_q2_targets *owner, qa_error *error)
{
    native_target_row *row = owner->retired;
    while (row) {
        native_target_row *next = row->retired_next;
        if (!collect(row, error)) return false;
        row = next;
    }
    return true;
}

static void retire(native_target_row *row)
{
    if (row->retired) return;
    struct application_native_q2_targets *owner = row->owner;
    qa_targets_unbind_context(targets(owner), row->source.actor, row);
    if (owner->actors[row->source.actor.slot] == row)
        owner->actors[row->source.actor.slot] = NULL;
    row->retired = true;
    row->retired_next = owner->retired;
    row->retired_previous = &owner->retired;
    if (row->retired_next) row->retired_next->retired_previous = &row->retired_next;
    owner->retired = row;
}

static void enter(native_target_row *row)
{
    ++row->calls;
    ++row->owner->engine->calls;
}

static bool leave(native_target_row *row, bool ok, qa_error *error)
{
    --row->owner->engine->calls;
    --row->calls;
    qa_error cleanup = {0};
    if (!collect(row, &cleanup)) {
        if (ok && error) *error = cleanup;
        return false;
    }
    return ok;
}

static bool current(native_target_row *row, qa_error *error)
{
    struct application_native_q2_targets *owner = row->owner;
    return !row->retired && owner->engine->source_targets == owner &&
        find(owner, row->source.actor) == row &&
        application_q2_combat_actor_valid(&row->source, error);
}

static bool pointer_read(native_target_row *row, uint32_t offset,
    qa_native_address *out, qa_error *error)
{
    uint8_t bytes[8];
    uint8_t width = row->owner->profile->target.pointer_bytes;
    if (!qa_native_read(instance(row->owner), row->source.address + offset,
            bytes, width, error)) return false;
    *out = width == 4 ? qa_load_u32le(bytes) : qa_load_u64le(bytes);
    return true;
}

static bool text_at(native_target_row *row, uint32_t offset, qa_string_id *out,
    qa_error *error)
{
    qa_native_address address;
    if (!pointer_read(row, offset, &address, error)) return false;
    if (!address) { *out = QA_STRING_NONE; return true; }
    const char *text;
    return qa_native_host_string_span(row->owner->engine->provider->state.native.host,
        address, &text, error) && qa_strings_intern_cstr(
            qa_session_strings(row->owner->engine->provider->application->session),
            text, out, error);
}

static bool text_read(native_target_row *row, unsigned field, qa_error *error)
{
    return text_at(row, text_offset(row->owner->profile, field), &row->names[field], error);
}

static bool changed(void *opaque, qa_native_instance *native,
    const qa_native_write_event *event, qa_error *error)
{
    native_target_row *row = opaque;
    if (row->retired) return true;
    enter(row);
    qa_error observed = {0};
    bool ok = current(row, &observed);
    if (!ok && observed.code == QA_ERROR_NOT_FOUND) {
        retire(row);
        ok = true;
    } else if (!ok && error) *error = observed;
    bool reindex = false;
    if (ok && !row->retired) {
        const application_q2_combat_profile *profile = row->owner->profile;
        for (unsigned i = 0; ok && i < TARGET_TEXT_COUNT; ++i) {
            size_t offset = text_offset(profile, i) - row->owner->watch_start;
            if (!memcmp(event->before.data + offset, event->after.data + offset,
                    profile->target.pointer_bytes)) continue;
            ok = text_read(row, i, error);
            reindex |= i == TARGET_CLASSNAME || i == TARGET_NAME;
        }
        if (ok && reindex) qa_targets_changed(targets(row->owner), row->source.actor);
    }
    (void)native;
    --row->owner->engine->calls;
    --row->calls;
    /* The current write observer cannot be removed until its callback returns. */
    return ok;
}

static bool watch(native_target_row *row, qa_error *error)
{
    struct application_native_q2_targets *owner = row->owner;
    qa_native_address address = row->source.address + owner->watch_start;
    if (row->watch && row->watched == address) return true;
    if (!qa_native_unobserve_writes(row->watch, error)) return false;
    row->watch = NULL;
    qa_native_write_span spans[TARGET_TEXT_COUNT];
    for (unsigned i = 0; i < TARGET_TEXT_COUNT; ++i) {
        if (!text_read(row, i, error)) return false;
        spans[i] = (qa_native_write_span){
            .offset = text_offset(owner->profile, i) - owner->watch_start,
            .bytes = owner->profile->target.pointer_bytes};
    }
    if (!qa_native_observe_writes(instance(owner), &(qa_native_write_watch){
            .address = address, .bytes = owner->watch_bytes,
            .spans = spans, .span_count = TARGET_TEXT_COUNT},
            changed, row, &row->watch, error)) return false;
    row->watched = address;
    return true;
}

static bool row_prepare(struct application_native_q2_targets *owner, uint32_t slot,
    qa_actor_id actor, bool admitting, native_target_row **out, qa_error *error)
{
    if (!collect_retired(owner, error)) return false;
    application_q2_combat_actor source;
    if (!application_q2_combat_actor_init(owner->profile, slot, actor, admitting,
            &source, error)) return false;
    native_target_row *occupied = owner->actors[actor.slot];
    if (occupied && !qa_actor_id_equal(occupied->source.actor, actor)) {
        retire(occupied);
        if (!collect(occupied, error)) return false;
    }
    native_target_row *row = find(owner, actor);
    if (row && row->source.generation != source.generation) {
        retire(row);
        if (!collect(row, error)) return false;
        return application_fail(error, QA_ERROR_NOT_FOUND,
            "Native target source generation changed before actor reconciliation");
    }
    if (!row) {
        if (owner->rows.head == SIZE_MAX && !qa_pool_extend(&owner->rows,
                qa_unified_frame_lease_arena(owner->storage), 1, error)) return false;
        size_t storage_slot;
        row = qa_pool_take(&owner->rows, &storage_slot);
        if (!row) return application_fail(error, QA_ERROR_CAPACITY,
            "Native target watcher storage is exhausted");
        *row = (native_target_row){.owner = owner, .storage_slot = storage_slot,
            .next = owner->first, .previous = &owner->first};
        if (row->next) row->next->previous = &row->next;
        owner->first = row;
        owner->actors[actor.slot] = row;
    }
    row->source = source;
    enter(row);
    bool ok = watch(row, error);
    if (!ok) retire(row);
    if (!leave(row, ok, error)) return false;
    *out = row;
    return true;
}

static bool read(void *opaque, qa_actor_id actor, qa_authored_target *out)
{
    native_target_row *row = opaque;
    if (!qa_actor_id_equal(row->source.actor, actor)) return false;
    enter(row);
    qa_error error = {0};
    uint8_t delay[4], wait[4];
    qa_string_id message;
    const application_q2_combat_profile *profile = row->owner->profile;
    bool ok = current(row, &error) &&
        qa_native_read(instance(row->owner), row->source.address + profile->authored.delay,
            delay, sizeof(delay), &error) &&
        qa_native_read(instance(row->owner), row->source.address + profile->authored.wait,
            wait, sizeof(wait), &error) &&
        text_at(row, profile->authored.message, &message, &error);
    if (ok) {
        *out = (qa_authored_target){.classname = row->names[TARGET_CLASSNAME],
            .targetname = row->names[TARGET_NAME], .target = row->names[TARGET_TARGET],
            .killtarget = row->names[TARGET_KILLTARGET], .message = message,
            .delay_seconds = qa_load_f32le(delay), .wait_seconds = qa_load_f32le(wait)};
        ok = isfinite(out->delay_seconds) && isfinite(out->wait_seconds);
    }
    return leave(row, ok, &error);
}

static bool address_for(struct application_native_q2_targets *owner, qa_actor_id actor,
    qa_native_address *out, qa_error *error)
{
    if (!actor.registry) return qa_native_entity_address(instance(owner), 0, out, error);
    return qa_native_host_source_address(owner->engine->provider->state.native.host,
        actor, out, error);
}

static bool projected_address(struct application_native_q2_targets *owner,
    qa_actor_id actor, qa_native_address *out, bool *present, qa_error *error)
{
    *present = false;
    if (!actor.registry) {
        *present = true;
        return qa_native_entity_address(instance(owner), 0, out, error);
    }
    if (!qa_actors_get(qa_session_actors(owner->engine->provider->application->session), actor))
        return true;
    uint32_t slot;
    if (!qa_native_host_slot_for_actor(owner->engine->provider->state.native.host, actor, &slot))
        return true;
    qa_native_slot_binding source;
    uint8_t inuse[4];
    if (!qa_native_slot(instance(owner), slot, &source, error)) return false;
    if ((source.kind != QA_NATIVE_SLOT_OWNED && source.kind != QA_NATIVE_SLOT_BORROWED &&
            source.kind != QA_NATIVE_SLOT_WORLD) || !qa_actor_id_equal(source.actor, actor))
        return true;
    if (!qa_native_entity_address(instance(owner), slot, out, error) ||
        !qa_native_read(instance(owner), *out + owner->profile->inuse,
            inuse, owner->profile->kex ? 1 : 4, error)) return false;
    *present = owner->profile->kex ? inuse[0] != 0 : qa_load_u32le(inuse) != 0;
    return true;
}

static bool source_call(native_target_row *row, qa_actor_id other, qa_actor_id activator,
    bool removing, qa_error *error)
{
    struct application_native_q2_targets *owner = row->owner;
    enter(row);
    qa_native_address other_address = 0, activator_address = 0, entry = 0;
    if (row->retired) return leave(row, true, error);
    qa_error initial = {0};
    if (!current(row, &initial)) {
        if (initial.code == QA_ERROR_NOT_FOUND) {
            retire(row);
            return leave(row, true, error);
        }
        if (error) *error = initial;
        return leave(row, false, error);
    }
    if (!removing) {
        if (!pointer_read(row, owner->profile->authored.use, &entry, error))
            return leave(row, false, error);
        if (!entry) return leave(row, true, error);
    }
    bool ok = removing || (address_for(owner, other, &other_address, error) &&
        address_for(owner, activator, &activator_address, error));
    bool other_present = true, activator_present = true;
    if (ok && !removing) ok = projected_address(owner, other, &other_address,
            &other_present, error) &&
        projected_address(owner, activator, &activator_address, &activator_present, error);
    if (ok && !row->retired) {
        /* Projecting a foreign argument can enter G_Spawn and retire this target. */
        qa_error observed = {0};
        if (!current(row, &observed)) {
            if (observed.code == QA_ERROR_NOT_FOUND) retire(row);
            else { if (error) *error = observed; ok = false; }
        }
    }
    if (ok && !row->retired && other_present && activator_present) {
        if (removing) ok = qa_native_rva(instance(owner), owner->profile->authored.free_entry,
            1, &entry, error);
        else ok = pointer_read(row, owner->profile->authored.use, &entry, error);
        if (ok && entry) {
            qa_native_type parameters[] = {
                {.kind = QA_NATIVE_ADDRESS, .count = 1},
                {.kind = QA_NATIVE_ADDRESS, .count = 1},
                {.kind = QA_NATIVE_ADDRESS, .count = 1}};
            size_t count = removing ? 1 : 3;
            qa_native_signature signature = {.abi = owner->profile->target.abi,
                .parameters = parameters, .parameter_count = count,
                .result = {.kind = QA_NATIVE_VOID, .count = 1}};
            qa_native_value arguments[] = {
                {.type = QA_NATIVE_ADDRESS, .as.address = row->source.address},
                {.type = QA_NATIVE_ADDRESS, .as.address = other_address},
                {.type = QA_NATIVE_ADDRESS, .as.address = activator_address}}, ignored;
            ok = qa_native_invoke(instance(owner), entry, &signature, arguments, count,
                &ignored, error);
            if (ok) ok = qa_native_host_source_reconcile(
                owner->engine->provider->state.native.host, error);
        }
    }
    return leave(row, ok, error);
}

static bool use(void *opaque, qa_actor_id actor, qa_actor_id other,
    qa_actor_id activator, qa_error *error)
{
    native_target_row *row = opaque;
    return !qa_actor_id_equal(row->source.actor, actor) ||
        source_call(row, other, activator, false, error);
}

static bool remove_actor(void *opaque, qa_actor_id actor, qa_error *error)
{
    native_target_row *row = opaque;
    return !qa_actor_id_equal(row->source.actor, actor) ||
        source_call(row, (qa_actor_id){0}, (qa_actor_id){0}, true, error);
}

static bool set_text(native_target_row *row, unsigned field, qa_string_id name,
    qa_error *error)
{
    enter(row);
    qa_native_address address = 0;
    bool ok = current(row, error) && (!name || qa_native_host_retain_string(
        row->owner->engine->provider->state.native.host, name, &address, error)) &&
        current(row, error);
    if (ok) {
        uint8_t bytes[8];
        uint8_t width = row->owner->profile->target.pointer_bytes;
        if (width == 4) qa_store_u32le(bytes, (uint32_t)address);
        else qa_store_u64le(bytes, address);
        ok = qa_native_write(instance(row->owner), row->source.address +
            text_offset(row->owner->profile, field), (qa_bytes){bytes, width}, error);
        if (ok && !row->retired) {
            row->names[field] = name;
            if (field == TARGET_NAME) qa_targets_changed(targets(row->owner), row->source.actor);
        }
    }
    return leave(row, ok, error);
}

static bool set_targetname(void *opaque, qa_actor_id actor, qa_string_id name,
    qa_error *error)
{
    native_target_row *row = opaque;
    return qa_actor_id_equal(row->source.actor, actor) && set_text(row, TARGET_NAME, name, error);
}

static bool set_target(void *opaque, qa_actor_id actor, qa_string_id name, qa_error *error)
{
    native_target_row *row = opaque;
    return qa_actor_id_equal(row->source.actor, actor) && set_text(row, TARGET_TARGET, name, error);
}

static bool set_delay(void *opaque, qa_actor_id actor, float seconds, qa_error *error)
{
    native_target_row *row = opaque;
    if (!qa_actor_id_equal(row->source.actor, actor)) return false;
    enter(row);
    uint8_t bytes[4]; qa_store_f32le(bytes, seconds);
    bool ok = current(row, error) && qa_native_write(instance(row->owner),
        row->source.address + row->owner->profile->authored.delay,
        (qa_bytes){bytes, sizeof(bytes)}, error);
    return leave(row, ok, error);
}

static bool field(void *opaque, qa_actor_id actor, qa_string_id key, qa_target_field *out)
{
    native_target_row *row = opaque;
    const qa_string_id *keys = qa_targets_field_keys(targets(row->owner));
    qa_authored_target value;
    if (!read(opaque, actor, &value)) return false;
    if (key == keys[QA_TARGET_KEY_CLASSNAME])
        *out = (qa_target_field){QA_TARGET_FIELD_TEXT, {.text = value.classname}};
    else if (key == keys[QA_TARGET_KEY_TARGETNAME])
        *out = (qa_target_field){QA_TARGET_FIELD_TEXT, {.text = value.targetname}};
    else if (key == keys[QA_TARGET_KEY_TARGET])
        *out = (qa_target_field){QA_TARGET_FIELD_TEXT, {.text = value.target}};
    else if (key == keys[QA_TARGET_KEY_KILLTARGET])
        *out = (qa_target_field){QA_TARGET_FIELD_TEXT, {.text = value.killtarget}};
    else if (key == keys[QA_TARGET_KEY_MESSAGE])
        *out = (qa_target_field){QA_TARGET_FIELD_TEXT, {.text = value.message}};
    else if (key == keys[QA_TARGET_KEY_DELAY])
        *out = (qa_target_field){QA_TARGET_FIELD_NUMBER, {.number = value.delay_seconds}};
    else if (key == keys[QA_TARGET_KEY_WAIT])
        *out = (qa_target_field){QA_TARGET_FIELD_NUMBER, {.number = value.wait_seconds}};
    else return false;
    return true;
}

static qa_target_binding binding(native_target_row *row)
{
    return (qa_target_binding){.actor = row->source.actor,
        .source = row->owner->engine->provider->component.clock.kind, .context = row,
        .read = read, .use = use, .remove = remove_actor, .field = field,
        .set_targetname = set_targetname, .set_target = set_target, .set_delay = set_delay};
}

bool application_native_q2_targets_prepare(struct application_native_q2 *engine,
    qa_error *error)
{
    if (engine->source_targets) return true;
    application_q2_combat_profile *profile = application_native_q2_combat_profile(engine);
    if (!profile || !profile->authored.present) return true;
    struct application_native_q2_targets *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Preparing original native targets");
    owner->engine = engine; owner->profile = profile;
    owner->capacity = qa_actors_capacity(qa_session_actors(engine->provider->application->session));
    owner->storage = application_control_storage_acquire(engine->provider->application, error);
    qa_arena *arena = owner->storage ? qa_unified_frame_lease_arena(owner->storage) : NULL;
    owner->actors = arena ? qa_arena_alloc(arena, owner->capacity * sizeof(*owner->actors),
        _Alignof(native_target_row *), error) : NULL;
    if (!owner->actors || !qa_pool_prepare(&owner->rows, arena, owner->capacity,
            sizeof(native_target_row), _Alignof(native_target_row), error)) {
        qa_unified_frame_lease_release(owner->storage); free(owner); return false;
    }
    memset(owner->actors, 0, owner->capacity * sizeof(*owner->actors));
    owner->watch_start = UINT32_MAX;
    uint32_t end = 0;
    for (unsigned i = 0; i < TARGET_TEXT_COUNT; ++i) {
        uint32_t offset = text_offset(profile, i);
        if (offset < owner->watch_start) owner->watch_start = offset;
        if (offset + profile->target.pointer_bytes > end) end = offset + profile->target.pointer_bytes;
    }
    owner->watch_bytes = end - owner->watch_start;
    engine->source_targets = owner;
    return true;
}

bool application_native_q2_targets_admit(struct application_native_q2 *engine,
    uint32_t source_slot, qa_actor_id actor, bool admitting, qa_error *error)
{
    if (!application_native_q2_targets_prepare(engine, error)) return false;
    struct application_native_q2_targets *owner = engine->source_targets;
    if (!owner) return true;
    const qa_actor_record *record = qa_actors_get(
        qa_session_actors(engine->provider->application->session), actor);
    qa_native_slot_binding source;
    if (!record || record->owner != engine->provider->owner || !record->has_source ||
        record->source_slot != source_slot) return true;
    if (!qa_native_slot(instance(owner), source_slot, &source, error)) return false;
    if (source.kind == QA_NATIVE_SLOT_BORROWED || source.kind == QA_NATIVE_SLOT_WORLD ||
        (source.kind == QA_NATIVE_SLOT_FREE && !admitting)) return true;
    native_target_row *row;
    if (!row_prepare(owner, source_slot, actor, admitting, &row, error)) return false;
    qa_target_binding published;
    bool ok = qa_targets_binding(targets(owner), actor, &published) &&
        published.context == row;
    if (!ok) {
        qa_target_binding value = binding(row);
        enter(row);
        ok = qa_targets_bind(targets(owner), &value, error);
        row->source.admitting = false;
        return leave(row, ok, error);
    }
    row->source.admitting = false;
    return true;
}

bool application_native_q2_targets_refresh(struct application_native_q2 *engine,
    qa_error *error)
{
    if (!application_native_q2_targets_prepare(engine, error)) return false;
    struct application_native_q2_targets *owner = engine->source_targets;
    if (!owner) return true;
    if (!collect_retired(owner, error)) return false;
    qa_native_entity_table table;
    if (!qa_native_entity_table_get(instance(owner), &table, error)) return false;
    for (uint32_t i = 1; i < table.count; ++i) {
        qa_native_slot_binding source;
        if (!qa_native_slot(instance(owner), i, &source, error)) return false;
        if (source.kind == QA_NATIVE_SLOT_OWNED &&
            !application_native_q2_targets_admit(engine, i, source.actor, false, error))
            return false;
    }
    return true;
}

bool application_native_q2_targets_released(struct application_native_q2 *engine,
    qa_actor_id actor, qa_error *error)
{
    struct application_native_q2_targets *owner = engine->source_targets;
    native_target_row *row = find(owner, actor);
    if (row) retire(row);
    return !owner || collect_retired(owner, error);
}

bool application_native_q2_targets_close(struct application_native_q2 *engine,
    qa_error *error)
{
    struct application_native_q2_targets *owner = engine->source_targets;
    if (!owner) return true;
    for (native_target_row *row = owner->first; row; row = row->next)
        if (row->calls) return application_fail(error, QA_ERROR_ARGUMENT,
            "Native targets still own an entered source callback");
    while (owner->first) {
        native_target_row *row = owner->first;
        retire(row);
        if (!collect(row, error)) return false;
    }
    qa_unified_frame_lease_release(owner->storage);
    free(owner);
    engine->source_targets = NULL;
    return true;
}

bool application_native_q2_targets_binding(application_provider *provider,
    qa_actor_id actor, qa_target_binding *out, qa_error *error)
{
    struct application_native_q2 *engine = provider->state.native.q2_engine;
    if (!engine || !application_native_q2_targets_prepare(engine, error)) return false;
    struct application_native_q2_targets *owner = engine->source_targets;
    uint32_t slot;
    qa_native_slot_binding source;
    if (!owner || !qa_native_host_slot_for_actor(provider->state.native.host, actor, &slot) ||
        !qa_native_slot(instance(owner), slot, &source, error) ||
        source.kind != QA_NATIVE_SLOT_OWNED || !qa_actor_id_equal(source.actor, actor))
        return false;
    native_target_row *row;
    if (!row_prepare(owner, slot, actor, false, &row, error)) return false;
    *out = binding(row);
    return true;
}
