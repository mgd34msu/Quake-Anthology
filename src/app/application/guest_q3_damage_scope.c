#include "guest_q3_damage_scope.h"
#include "guest_q3_private.h"
#include "control_frame.h"
#include "qa/native_observe.h"

typedef struct damage_change { uint64_t address; qa_damage_mutation mutation; } damage_change;
typedef struct damage_delivery {
    struct damage_delivery *next;
    qa_unified_frame_lease *storage;
    uint64_t sequence;
    damage_change changes[3];
    size_t count;
} damage_delivery;
typedef struct reaction_watch {
    struct application_q3_damage_frame *frame;
    uint32_t offset;
    uint64_t pointer;
    application_q3_reaction_call arguments;
    qa_reaction kind;
} reaction_watch;
typedef struct damage_watch {
    uint64_t address;
    size_t bytes;
    qa_native_write_observer *native;
} damage_watch;
struct application_q3_damage_frame {
    struct application_q3_damage_frame *previous, *retained_next;
    struct application_q3_damage_scopes *owner;
    qa_unified_frame_lease *storage;
    application_q3_combat_actor source;
    application_source_call invocation;
    qa_qvm_call qvm_call;
    const qa_damage_request *request;
    qa_damage_observer *observer;
    qa_damage_result result;
    qa_qvm_binding writes;
    size_t watch_count;
    reaction_watch reactions[2];
    damage_delivery *deliveries;
    double health;
    qa_armor armor;
    qa_vec3 velocity;
    bool active, reacting, cancelled;
};
struct application_q3_damage_scopes {
    q3g_role *role;
    const application_q3_combat_profile *profile;
    application_q3_damage_frame *current, *retained;
    qa_qvm_binding resolver;
    qa_qvm_function_resolver fallback;
    void *fallback_context;
    application_q3_damage_reaction_bind native_bind;
    void *native_context;
    size_t entries;
    size_t frame_bytes;
};
static bool fail(qa_error *error, qa_status status, const char *text)
{ return application_fail(error, status, text); }
static bool actor_live(application_q3_damage_frame *frame, bool *out, qa_error *error)
{
    if (!qa_actors_get(qa_session_actors(frame->owner->role->engine->provider->application->session), frame->source.actor)) {
        *out = false; return true;
    }
    return application_q3_combat_actor_live(&frame->source, out, error);
}
static bool reporting(application_q3_damage_frame *frame, bool *out, qa_error *error)
{
    *out = false;
    if (!frame->active || frame->reacting) return true;
    for (application_q3_damage_frame *current = frame->owner->current; current; current = current->previous)
        if (qa_actor_id_equal(current->source.actor, frame->source.actor))
            return current != frame || current->reacting ? true : actor_live(frame, out, error);
    return true;
}
bool application_q3_damage_scope_cancel(application_q3_damage_frame *frame, qa_error *error)
{
    if (!frame || !frame->active)
        return fail(error, QA_ERROR_ARGUMENT, "Original Q3 damage cancellation lost its entered source frame");
    if (frame->cancelled) return true;
    frame->cancelled = true;
    return frame->invocation.cancel(frame->invocation.context, error);
}
static bool touches(const qa_qvm_committed_write *event, uint64_t at, size_t size)
{
    for (size_t i = 0; i < event->count; ++i) {
        const qa_qvm_committed_range *range = event->ranges + i;
        if ((uint64_t)range->offset < (uint64_t)at + size &&
            (uint64_t)at < (uint64_t)range->offset + range->after.size) return true;
    }
    return false;
}
typedef struct original_reaction {
    const qa_qvm_call *call;
    int32_t *result;
} original_reaction;
static bool reaction_original(void *context, qa_error *error)
{
    original_reaction *original = context;
    return qa_qvm_proceed(original->call, original->result, error);
}
bool application_q3_damage_scope_reaction(application_q3_damage_scopes *owner,
    const application_q3_reaction_invocation *call, bool *handled, qa_error *error)
{
    *handled = false;
    if (!owner) return true;
    for (application_q3_damage_frame *frame = owner->current; frame; frame = frame->previous) {
        if (!frame->active || frame->reacting) continue;
        for (size_t i = 0; i < 2; ++i) {
            reaction_watch *watch = frame->reactions + i;
            if (!watch->pointer || call->address != watch->pointer) continue;
            uint64_t target, pointer; int32_t amount;
            if (!call->pointer(call->context, watch->arguments.target, &target, error)) return false;
            if (target != frame->source.entity) continue;
            bool report;
            if (!reporting(frame, &report, error)) return false;
            if (!report) continue;
            if (!qa_q3_host_source_pointer(owner->role->host,
                    frame->source.entity + watch->offset, &pointer, error)) return false;
            if (pointer != call->address) continue;
            if (!call->integer(call->context, watch->arguments.amount, &amount, error)) return false;
            *handled = true;
            frame->reacting = true;
            frame->result = (qa_damage_result){.applied_damage = amount, .reaction = watch->kind};
            if (!qa_damage_dispatch_source_reaction(frame->observer, &frame->result,
                    frame->request->knockback, frame->request->point,
                    owner->role->engine->provider->owner, call->proceed, call->context, error)) return false;
            bool live;
            return actor_live(frame, &live, error) &&
                (live || application_q3_damage_scope_cancel(frame, error));
        }
    }
    return true;
}
static bool reaction_qvm_integer(void *context, uint32_t index, int32_t *out, qa_error *error)
{ return qa_qvm_call_argument(((original_reaction *)context)->call, index, out, error); }
static bool reaction_qvm_pointer(void *context, uint32_t index, uint64_t *out, qa_error *error)
{
    int32_t value;
    if (!reaction_qvm_integer(context, index, &value, error)) return false;
    *out = (uint32_t)value; return true;
}
static bool reaction_enter(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_damage_scopes *owner = context;
    original_reaction original = {call, result};
    application_q3_reaction_invocation invocation = {.address = call->instruction,
        .context = &original, .pointer = reaction_qvm_pointer, .integer = reaction_qvm_integer,
        .proceed = reaction_original};
    bool handled;
    *result = 0;
    if (!application_q3_damage_scope_reaction(owner, &invocation, &handled, error)) return false;
    if (handled) return true;
    void *hook_context = NULL;
    qa_qvm_function_hook hook = owner->fallback ?
        owner->fallback(owner->fallback_context, call, &hook_context) : NULL;
    return hook ? hook(hook_context, call, result, error) : qa_qvm_proceed(call, result, error);
}
static qa_qvm_function_hook reaction_resolve(void *context, const qa_qvm_call *call, void **hook_context)
{
    application_q3_damage_scopes *owner = context;
    for (application_q3_damage_frame *frame = owner->current; frame; frame = frame->previous) {
        if (!frame->active || frame->reacting) continue;
        for (size_t i = 0; i < 2; ++i)
            if (frame->reactions[i].pointer && call->instruction == frame->reactions[i].pointer) {
                *hook_context = owner;
                return reaction_enter;
            }
    }
    return owner->fallback ? owner->fallback(owner->fallback_context, call, hook_context) : NULL;
}
static bool reaction_refresh(reaction_watch *watch, qa_error *error)
{
    application_q3_damage_frame *frame = watch->frame;
    if (!qa_q3_host_source_pointer(frame->owner->role->host,
        frame->source.entity + watch->offset, &watch->pointer, error)) return false;
    return !watch->pointer || !frame->owner->native_bind ||
        frame->owner->native_bind(frame->owner->native_context, watch->pointer, &watch->arguments, error);
}
static bool global_address(application_q3_damage_scopes *owner, uint32_t offset,
    uint64_t *out, qa_error *error)
{
    *out = offset;
    return !owner->role->native || qa_native_rva(qa_native_host_instance(owner->role->native),
        offset, 4, out, error);
}
static bool qvm_changed(const void *event, uint64_t address, size_t bytes)
{ return touches(event, address, bytes); }
static bool store_changes(application_q3_damage_frame *frame,
    bool (*changed)(const void *, uint64_t, size_t), const void *event,
    damage_change changes[3], size_t *out_count, qa_error *error)
{
    *out_count = 0;
    if (!frame->active) return true;
    bool live;
    if (!actor_live(frame, &live, error)) return false;
    if (!live) return true;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(frame->owner->profile);
    size_t pointer_bytes = frame->owner->role->module ?
        qa_native_module_describe(frame->owner->role->module).image.target.pointer_bytes : 4;
    for (size_t i = 0; i < 2; ++i)
        if (changed(event, frame->source.entity + frame->reactions[i].offset, pointer_bytes) &&
            !reaction_refresh(frame->reactions + i, error)) return false;
    bool report;
    if (!reporting(frame, &report, error)) return false;
    size_t count = 0;
    uint64_t health_at = frame->source.entity + d->fields.health;
    if (changed(event, health_at, 4)) {
        int32_t word;
        if (!application_q3_combat_word_read(&frame->source, health_at, &word, error)) goto failed;
        double after = word;
        double before = frame->health; frame->health = after;
        if (before != after) changes[count++] = (damage_change){health_at,
            {.kind = QA_MUTATION_HEALTH, .value.health = {before, after}}};
    }
    if (frame->source.player) {
        uint64_t first = UINT64_MAX;
        uint64_t points = frame->source.player + 184 + d->armor.points_stat * 4;
        if (changed(event, points, 4)) first = points;
        if (d->armor.mode_count) {
            uint64_t tier = frame->source.player + 184 + d->armor.tier_stat * 4;
            if (changed(event, tier, 4) && tier < first) first = tier;
            for (size_t i = 0; i < d->armor.mode_count; ++i) {
                uint64_t at;
                if (!global_address(frame->owner, d->armor.modes[i].offset, &at, error)) return false;
                if (changed(event, at, 4) && at < first) first = at;
            }
        }
        if (first != UINT64_MAX) {
            qa_armor before = frame->armor;
            if (!application_q3_combat_armor_read(&frame->source, &frame->armor, error)) goto failed;
            if (!qa_armor_equal(before, frame->armor)) changes[count++] = (damage_change){first,
                {.kind = QA_MUTATION_ARMOR, .value.armor = {before, frame->armor}}};
        }
    }
    uint64_t velocity_at = frame->source.player ? frame->source.player + 32 : frame->source.entity + 36;
    if (changed(event, velocity_at, 12)) {
        qa_vec3 before = frame->velocity;
        if (!application_q3_combat_velocity(&frame->source, &frame->velocity, error)) goto failed;
        if (!qa_vec_equal(before, frame->velocity)) changes[count++] = (damage_change){velocity_at,
            {.kind = QA_MUTATION_SOURCE_VELOCITY,
                .value.velocity = {before, frame->velocity, frame->request->attack.movement_provider}}};
    }
    if (report) {
        for (size_t i = 1; i < count; ++i) {
            damage_change value = changes[i]; size_t j = i;
            while (j && changes[j - 1].address > value.address) { changes[j] = changes[j - 1]; --j; }
            changes[j] = value;
        }
        *out_count = count;
    }
    return true;
failed:
    return false;
}
static bool store_publish(void *context, qa_qvm *vm,
    const qa_qvm_committed_write *event, qa_error *error)
{
    application_q3_damage_frame *frame = context;
    if (vm != frame->qvm_call.vm) return true;
    damage_change changes[3]; size_t count;
    if (!store_changes(frame, qvm_changed, event, changes, &count, error)) return false;
    if (count) {
        qa_unified_frame_lease *storage = application_control_storage_acquire(
            frame->owner->role->engine->provider->application, error);
        if (!storage) return false;
        damage_delivery *delivery = qa_unified_frame_lease_alloc(storage, 1,
            sizeof(*delivery), _Alignof(damage_delivery), error);
        if (!delivery) { qa_unified_frame_lease_release(storage); return false; }
        delivery->storage = storage; delivery->sequence = event->sequence;
        memcpy(delivery->changes, changes, count * sizeof(*changes)); delivery->count = count;
        delivery->next = frame->deliveries; frame->deliveries = delivery;
    }
    return true;
}
static bool store_deliver(application_q3_damage_frame *frame,
    const damage_change *changes, size_t count, qa_error *error)
{
    for (size_t i = 0; i < count; ++i) {
        bool report;
        if (!reporting(frame, &report, error)) return false;
        if (!report) break;
        const qa_damage_mutation *change = &changes[i].mutation;
        if (!qa_damage_observe(frame->observer, change, error)) return false;
        if (change->kind == QA_MUTATION_HEALTH)
            frame->result.applied_damage += change->value.health.before - change->value.health.after;
        bool live;
        if (!actor_live(frame, &live, error)) return false;
        if (!live) return application_q3_damage_scope_cancel(frame, error);
    }
    return true;
}
static bool store_after(void *context, qa_qvm *vm,
    const qa_qvm_committed_write *event, qa_error *error)
{
    application_q3_damage_frame *frame = context; (void)vm;
    damage_delivery **link = &frame->deliveries;
    while (*link && (*link)->sequence != event->sequence) link = &(*link)->next;
    if (!*link) return true;
    damage_delivery delivery = **link; damage_delivery *owned = *link;
    *link = owned->next; qa_unified_frame_lease_release(owned->storage);
    return store_deliver(frame, delivery.changes, delivery.count, error);
}
static bool native_changed(const void *context, uint64_t address, size_t bytes)
{
    const qa_native_write_event *event = context;
    uint64_t first = event->address + event->offset;
    return first < address + bytes && address < first + event->size;
}
static bool native_store(void *context, qa_native_instance *instance,
    const qa_native_write_event *event, qa_error *error)
{
    application_q3_damage_frame *frame = context;
    if (!frame->active) return true;
    qa_native_write_scope *scope = NULL;
    if (!qa_native_write_scope_open(instance, event, &scope, error)) return false;
    damage_change changes[3]; size_t count;
    bool ok = store_changes(frame, native_changed, event, changes, &count, error) &&
        store_deliver(frame, changes, count, error);
    qa_error cleanup = {0};
    if (!qa_native_write_scope_close(&scope, &cleanup)) {
        if (error) *error = cleanup;
        ok = false;
    }
    return ok;
}
static void store_dispose(void *context, const qa_qvm_committed_write *event)
{
    application_q3_damage_frame *frame = context;
    damage_delivery **link = &frame->deliveries;
    while (*link && (*link)->sequence != event->sequence) link = &(*link)->next;
    if (!*link) return;
    damage_delivery *delivery = *link;
    *link = delivery->next;
    qa_unified_frame_lease_release(delivery->storage);
}
static bool frame_close(application_q3_damage_frame *frame, qa_error *error)
{
    bool ok = true; qa_error first = {0}; frame->active = false;
    if (frame->writes) {
        qa_error current = {0};
        if (qa_qvm_unobserve_writes(frame->qvm_call.vm, frame->writes, &current)) frame->writes = 0;
        else { first = current; ok = false; }
    }
    damage_watch *watches = (damage_watch *)(frame + 1);
    for (size_t i = frame->watch_count; i > 0; --i) if (watches[i - 1].native) {
        qa_error current = {0};
        if (qa_native_unobserve_writes(watches[i - 1].native, &current)) watches[i - 1].native = NULL;
        else { if (ok) first = current; ok = false; }
    }
    if (!frame->previous && !frame->owner->entries && frame->owner->resolver) {
        qa_error current = {0};
        if (qa_qvm_unbind(frame->qvm_call.vm, frame->owner->resolver, &current)) frame->owner->resolver = 0;
        else if (ok) { first = current; ok = false; }
    }
    if (!ok && error) *error = first;
    return ok;
}
static void frame_free(application_q3_damage_frame *frame)
{
    while (frame->deliveries) {
        damage_delivery *next = frame->deliveries->next;
        qa_unified_frame_lease_release(frame->deliveries->storage);
        frame->deliveries = next;
    }
    qa_unified_frame_lease_release(frame->storage);
}
bool application_q3_damage_scopes_create(q3g_role *role,
    const application_q3_combat_profile *profile, application_q3_damage_reaction_bind native_bind,
    void *native_context, application_q3_damage_scopes **out, qa_error *error)
{
    if (!role || !profile || !out || *out || (!role->vm && !role->module) || role->kind != QA_QVM_GAME ||
        role->image != application_q3_combat_profile_image(profile) ||
        role->module != application_q3_combat_profile_module(profile) || role->abi != application_q3_combat_profile_abi(profile))
        return fail(error, QA_ERROR_ARGUMENT, "Original Q3 damage scopes require their actual GAME executor");
    application_q3_damage_scopes *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, "Retaining original Q3 damage scope owner");
    const application_q3_combat_definition *definition = application_q3_combat_profile_definition(profile);
    if (definition->armor.mode_count > SIZE_MAX - 6 ||
        6 + definition->armor.mode_count >
            (SIZE_MAX - sizeof(application_q3_damage_frame)) / (sizeof(damage_watch) + sizeof(qa_qvm_write_range))) {
        free(owner);
        return fail(error, QA_ERROR_MEMORY, "Original Q3 damage frame storage size overflow");
    }
    size_t ranges = 6 + definition->armor.mode_count;
    owner->frame_bytes = sizeof(application_q3_damage_frame) + ranges * (sizeof(damage_watch) + sizeof(qa_qvm_write_range));
    owner->role = role; owner->profile = profile;
    owner->native_bind = native_bind; owner->native_context = native_context;
    *out = owner; return true;
}
bool application_q3_damage_scopes_idle(const application_q3_damage_scopes *owner)
{ return owner && !owner->current && !owner->retained && !owner->resolver; }
bool application_q3_damage_scopes_quiescent(const application_q3_damage_scopes *owner)
{ return owner && !owner->current; }
bool application_q3_damage_scopes_enter(application_q3_damage_scopes *owner,
    qa_qvm_function_resolver fallback, void *context, qa_error *error)
{
    if (owner->role->vm && !owner->entries && !owner->resolver &&
        !qa_qvm_bind_resolver(owner->role->vm, reaction_resolve, owner, &owner->resolver, error)) return false;
    owner->fallback = fallback; owner->fallback_context = context;
    ++owner->entries;
    return true;
}
bool application_q3_damage_scopes_leave(application_q3_damage_scopes *owner, qa_error *error)
{
    if (--owner->entries) return true;
    owner->fallback = NULL; owner->fallback_context = NULL;
    if (owner->current) return true;
    if (owner->resolver && !qa_qvm_unbind(owner->role->vm, owner->resolver, error)) return false;
    owner->resolver = 0;
    return true;
}
void application_q3_damage_scopes_health_projected(application_q3_damage_scopes *owner,
    qa_actor_id actor, double health)
{
    for (application_q3_damage_frame *frame = owner ? owner->current : NULL;
         frame; frame = frame->previous)
        if (frame->active && qa_actor_id_equal(frame->source.actor, actor)) frame->health = health;
}
bool application_q3_damage_scopes_destroy(application_q3_damage_scopes **out, qa_error *error)
{
    if (!out || !*out) return true;
    application_q3_damage_scopes *owner = *out;
    if (owner->current || owner->entries) return fail(error, QA_ERROR_ARGUMENT, "Original Q3 damage scopes still borrow source frames");
    while (owner->retained) {
        application_q3_damage_frame *frame = owner->retained;
        if (!frame_close(frame, error)) return false;
        owner->retained = frame->retained_next; frame_free(frame);
    }
    if (owner->resolver && !qa_qvm_unbind(owner->role->vm, owner->resolver, error)) return false;
    free(owner); *out = NULL; return true;
}
bool application_q3_damage_scope_run(application_q3_damage_scopes *owner,
    const application_source_call *call, const application_q3_combat_actor *source,
    const qa_damage_request *request, qa_damage_observer *observer, qa_damage_result *out, qa_error *error)
{
    if (!owner || !call || !source || !request || !observer || !out || owner->retained ||
        source->role != owner->role || source->profile != owner->profile ||
        (call->qvm && call->qvm->vm != owner->role->vm) || !call->proceed || !call->cancel ||
        !qa_actor_id_equal(source->actor, request->target) || !application_q3_combat_actor_current(source, error))
        return fail(error, QA_ERROR_ARGUMENT, "Original Q3 damage scope lost its entered actor namespace");
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    if (call->qvm && (source->entity > UINT32_MAX - (d->entity_stride - 1) ||
        (source->player && source->player > UINT32_MAX - (d->client_stride - 1))))
        return fail(error, QA_ERROR_ARGUMENT, "Source combat records leave their QVM observer address space");
    uint64_t entity = source->entity, player = source->player, entry = d->callbacks.damage;
    if (owner->role->native && !qa_native_rva(qa_native_host_instance(owner->role->native),
            d->callbacks.damage, 1, &entry, error)) return false;
    if (call->entry != entry || call->count != d->damage_call.count ||
        call->arguments[d->damage_call.positions[Q3_DAMAGE_TARGET]].as.address != source->entity)
        return fail(error, QA_ERROR_ARGUMENT, "Original Q3 damage scope differs from the reached G_Damage frame");
    bool live;
    if (!application_q3_combat_actor_live(source, &live, error)) return false;
    if (!live) { *out = (qa_damage_result){0}; return true; }
    qa_unified_frame_lease *storage = application_control_storage_acquire(
        owner->role->engine->provider->application, error);
    if (!storage) return false;
    application_q3_damage_frame *frame = qa_unified_frame_lease_alloc(storage, 1,
        owner->frame_bytes, _Alignof(application_q3_damage_frame), error);
    if (!frame) { qa_unified_frame_lease_release(storage); return false; }
    frame->storage = storage;
    frame->owner = owner; frame->source = *source; frame->invocation = *call;
    if (call->qvm) { frame->qvm_call = *call->qvm; frame->invocation.qvm = &frame->qvm_call; }
    frame->request = request; frame->observer = observer; frame->active = true;
    frame->previous = owner->current; owner->current = frame;
    frame->reactions[0] = (reaction_watch){.frame = frame, .offset = d->reactions.pain,
        .arguments = d->reactions.pain_call, .kind = QA_REACTION_PAIN};
    frame->reactions[1] = (reaction_watch){.frame = frame, .offset = d->reactions.die,
        .arguments = d->reactions.die_call, .kind = QA_REACTION_DEATH};
    qa_combat_state state; bool ok = application_q3_combat_state_read(&frame->source, &state, error) &&
        application_q3_combat_velocity(&frame->source, &frame->velocity, error);
    if (ok) { frame->health = state.health; frame->armor = state.armor; }
    damage_watch *watches = (damage_watch *)(frame + 1);
    qa_qvm_write_range *ranges = (qa_qvm_write_range *)(watches + 6 + d->armor.mode_count);
    size_t count = 0;
    if (ok) {
        size_t pointer_bytes = owner->role->module ?
            qa_native_module_describe(owner->role->module).image.target.pointer_bytes : 4;
        watches[count++] = (damage_watch){entity + d->fields.health, 4, NULL};
        watches[count++] = (damage_watch){player ? player + 32 : entity + 36, 12, NULL};
        watches[count++] = (damage_watch){entity + d->reactions.pain, pointer_bytes, NULL};
        watches[count++] = (damage_watch){entity + d->reactions.die, pointer_bytes, NULL};
        if (source->player) {
            watches[count++] = (damage_watch){player + 184 + d->armor.points_stat * 4, 4, NULL};
            if (d->armor.mode_count) {
                watches[count++] = (damage_watch){player + 184 + d->armor.tier_stat * 4, 4, NULL};
                for (size_t i = 0; ok && i < d->armor.mode_count; ++i) {
                    uint64_t at;
                    ok = global_address(owner, d->armor.modes[i].offset, &at, error);
                    if (ok) watches[count++] = (damage_watch){at, 4, NULL};
                }
            }
        }
        frame->watch_count = count;
        if (ok) ok = reaction_refresh(frame->reactions, error) && reaction_refresh(frame->reactions + 1, error);
        if (call->qvm) {
            for (size_t i = 0; i < count; ++i)
                ranges[i] = (qa_qvm_write_range){(uint32_t)watches[i].address, watches[i].bytes};
            if (ok) ok = qa_qvm_observe_writes_owned(call->qvm->vm, ranges, count, store_publish, store_after,
                store_dispose, frame, &frame->writes, error);
            if (ok && !frame->previous && !owner->resolver)
                ok = qa_qvm_bind_resolver(call->qvm->vm, reaction_resolve, owner, &owner->resolver, error);
        } else for (size_t i = 0; ok && i < count; ++i)
            ok = qa_native_observe_writes(qa_native_host_instance(owner->role->native),
                &(qa_native_write_watch){.address = watches[i].address, .bytes = watches[i].bytes},
                native_store, frame, &watches[i].native, error);
    }
    if (ok) ok = call->proceed(call->context, call->arguments, call->count, error);
    qa_error original = error ? *error : (qa_error){0}; qa_error cleanup = {0};
    bool closed = frame_close(frame, &cleanup); owner->current = frame->previous;
    if (closed) { if (ok) *out = frame->result; frame_free(frame); }
    else { frame->retained_next = owner->retained; owner->retained = frame; }
    if (error && (!ok || !closed)) *error = ok ? cleanup : original;
    return ok && closed;
}
application_q3_damage_frame *application_q3_damage_scope_current(application_q3_damage_scopes *owner, uint64_t target)
{
    application_q3_damage_frame *frame = owner ? owner->current : NULL;
    return frame && frame->active && !frame->reacting && frame->source.entity == target ? frame : NULL;
}
const qa_damage_request *application_q3_damage_scope_request(const application_q3_damage_frame *frame)
{ return frame && frame->active ? frame->request : NULL; }
const application_source_call *application_q3_damage_scope_call(const application_q3_damage_frame *frame)
{ return frame && frame->active ? &frame->invocation : NULL; }
const application_q3_combat_actor *application_q3_damage_scope_actor(const application_q3_damage_frame *frame)
{ return frame && frame->active ? &frame->source : NULL; }
