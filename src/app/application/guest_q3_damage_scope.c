#include "guest_q3_damage_scope.h"
#include "guest_q3_private.h"
#include "control_frame.h"

typedef struct damage_change { uint32_t address; qa_damage_mutation mutation; } damage_change;
typedef struct damage_delivery {
    struct damage_delivery *next;
    qa_unified_frame_lease *storage;
    uint64_t sequence;
    damage_change changes[3];
    size_t count;
} damage_delivery;
typedef struct reaction_watch {
    struct application_q3_damage_frame *frame;
    qa_qvm_binding binding;
    uint32_t offset, pointer;
    application_q3_reaction_call arguments;
    qa_reaction kind;
} reaction_watch;
struct application_q3_damage_frame {
    struct application_q3_damage_frame *previous, *retained_next;
    struct application_q3_damage_scopes *owner;
    qa_unified_frame_lease *storage;
    application_q3_combat_actor source;
    qa_qvm_call call;
    const qa_damage_request *request;
    qa_damage_observer *observer;
    qa_damage_result result;
    qa_qvm_binding writes;
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
    if (!qa_qvm_cancel(&frame->call, error)) return false;
    frame->cancelled = true; return true;
}
static bool touches(const qa_qvm_committed_write *event, uint32_t at, size_t size)
{
    for (size_t i = 0; i < event->count; ++i) {
        const qa_qvm_committed_range *range = event->ranges + i;
        if ((uint64_t)range->offset < (uint64_t)at + size &&
            (uint64_t)at < (uint64_t)range->offset + range->after.size) return true;
    }
    return false;
}
static bool reaction_enter(void *context, const qa_qvm_call *call, qa_error *error)
{
    reaction_watch *watch = context; application_q3_damage_frame *frame = watch->frame;
    bool report; int32_t pointer, target, amount;
    if (!reporting(frame, &report, error)) return false;
    if (!report) return true;
    if (!application_q3_combat_word_read(&frame->source, frame->source.entity + watch->offset, &pointer, error) ||
        !qa_qvm_call_argument(call, watch->arguments.target, &target, error)) return false;
    if ((uint32_t)pointer != watch->pointer || (uint32_t)target != frame->source.entity) return true;
    if (!qa_qvm_call_argument(call, watch->arguments.amount, &amount, error)) return false;
    frame->reacting = true;
    frame->result = (qa_damage_result){.applied_damage = amount, .reaction = watch->kind};
    if (!qa_damage_before_reaction(frame->observer, &frame->result, error)) return false;
    bool live;
    return actor_live(frame, &live, error) && (live || application_q3_damage_scope_cancel(frame, error));
}
static bool reaction_refresh(reaction_watch *watch, qa_error *error)
{
    application_q3_damage_frame *frame = watch->frame; int32_t pointer;
    if (!application_q3_combat_word_read(&frame->source,
        frame->source.entity + watch->offset, &pointer, error)) return false;
    if ((uint32_t)pointer == watch->pointer) return true;
    if (watch->binding) {
        if (!qa_qvm_unbind(frame->call.vm, watch->binding, error)) return false;
        watch->binding = 0;
    }
    watch->pointer = (uint32_t)pointer;
    return !pointer || qa_qvm_observe_function(frame->call.vm, (uint32_t)pointer,
        reaction_enter, watch, &watch->binding, error);
}
static bool store_publish(void *context, qa_qvm *vm,
    const qa_qvm_committed_write *event, qa_error *error)
{
    application_q3_damage_frame *frame = context;
    if (!frame->active || vm != frame->call.vm) return true;
    bool live;
    if (!actor_live(frame, &live, error)) return false;
    if (!live) return true;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(frame->owner->profile);
    for (size_t i = 0; i < 2; ++i)
        if (touches(event, frame->source.entity + frame->reactions[i].offset, 4) &&
            !reaction_refresh(frame->reactions + i, error)) return false;
    bool report;
    if (!reporting(frame, &report, error)) return false;
    damage_change changes[3]; size_t count = 0;
    uint32_t health_at = frame->source.entity + d->fields.health;
    if (touches(event, health_at, 4)) {
        int32_t word;
        if (!application_q3_combat_word_read(&frame->source, health_at, &word, error)) goto failed;
        double after = word;
        double before = frame->health; frame->health = after;
        if (before != after) changes[count++] = (damage_change){health_at,
            {.kind = QA_MUTATION_HEALTH, .value.health = {before, after}}};
    }
    if (frame->source.player) {
        uint32_t first = UINT32_MAX;
        uint32_t points = frame->source.player + 184 + d->armor.points_stat * 4;
        if (touches(event, points, 4)) first = points;
        if (d->armor.mode_count) {
            uint32_t tier = frame->source.player + 184 + d->armor.tier_stat * 4;
            if (touches(event, tier, 4) && tier < first) first = tier;
            for (size_t i = 0; i < d->armor.mode_count; ++i)
                if (touches(event, d->armor.modes[i].offset, 4) && d->armor.modes[i].offset < first)
                    first = d->armor.modes[i].offset;
        }
        if (first != UINT32_MAX) {
            qa_armor before = frame->armor;
            if (!application_q3_combat_armor_read(&frame->source, &frame->armor, error)) goto failed;
            if (!qa_armor_equal(before, frame->armor)) changes[count++] = (damage_change){first,
                {.kind = QA_MUTATION_ARMOR, .value.armor = {before, frame->armor}}};
        }
    }
    uint32_t velocity_at = frame->source.player ? frame->source.player + 32 : frame->source.entity + 36;
    if (touches(event, velocity_at, 12)) {
        qa_vec3 before = frame->velocity;
        if (!application_q3_combat_velocity(&frame->source, &frame->velocity, error)) goto failed;
        if (!qa_vec_equal(before, frame->velocity)) changes[count++] = (damage_change){velocity_at,
            {.kind = QA_MUTATION_SOURCE_VELOCITY,
                .value.velocity = {before, frame->velocity, frame->request->attack.movement_provider}}};
    }
    if (report && count) {
        qa_unified_frame_lease *storage = application_control_storage_acquire(
            frame->owner->role->engine->provider->application, error);
        if (!storage) return false;
        damage_delivery *delivery = qa_unified_frame_lease_alloc(storage, 1,
            sizeof(*delivery), _Alignof(damage_delivery), error);
        if (!delivery) { qa_unified_frame_lease_release(storage); return false; }
        delivery->storage = storage; delivery->sequence = event->sequence;
        for (size_t i = 1; i < count; ++i) {
            damage_change value = changes[i]; size_t j = i;
            while (j && changes[j - 1].address > value.address) { changes[j] = changes[j - 1]; --j; }
            changes[j] = value;
        }
        memcpy(delivery->changes, changes, count * sizeof(*changes)); delivery->count = count;
        delivery->next = frame->deliveries; frame->deliveries = delivery;
    }
    return true;
failed:
    return false;
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
    for (size_t i = 0; i < delivery.count; ++i) {
        bool report;
        if (!reporting(frame, &report, error)) return false;
        if (!report) break;
        const qa_damage_mutation *change = &delivery.changes[i].mutation;
        if (!qa_damage_observe(frame->observer, change, error)) return false;
        if (change->kind == QA_MUTATION_HEALTH)
            frame->result.applied_damage += change->value.health.before - change->value.health.after;
        bool live;
        if (!actor_live(frame, &live, error)) return false;
        if (!live) return application_q3_damage_scope_cancel(frame, error);
    }
    return true;
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
        if (qa_qvm_unobserve_writes(frame->call.vm, frame->writes, &current)) frame->writes = 0;
        else { first = current; ok = false; }
    }
    for (size_t i = 0; i < 2; ++i) if (frame->reactions[i].binding) {
        qa_error current = {0};
        if (qa_qvm_unbind(frame->call.vm, frame->reactions[i].binding, &current)) frame->reactions[i].binding = 0;
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
    const application_q3_combat_profile *profile, application_q3_damage_scopes **out, qa_error *error)
{
    if (!role || !profile || !out || *out || !role->vm || role->kind != QA_QVM_GAME ||
        role->image != application_q3_combat_profile_image(profile) || role->abi != application_q3_combat_profile_abi(profile))
        return fail(error, QA_ERROR_ARGUMENT, "Original Q3 damage scopes require their actual GAME executor");
    application_q3_damage_scopes *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, "Retaining original Q3 damage scope owner");
    const application_q3_combat_definition *definition = application_q3_combat_profile_definition(profile);
    if (definition->armor.mode_count > SIZE_MAX - 6 ||
        6 + definition->armor.mode_count >
            (SIZE_MAX - sizeof(application_q3_damage_frame)) / sizeof(qa_qvm_write_range)) {
        free(owner);
        return fail(error, QA_ERROR_MEMORY, "Original Q3 damage frame storage size overflow");
    }
    size_t ranges = 6 + definition->armor.mode_count;
    owner->frame_bytes = sizeof(application_q3_damage_frame) + ranges * sizeof(qa_qvm_write_range);
    owner->role = role; owner->profile = profile; *out = owner; return true;
}
bool application_q3_damage_scopes_idle(const application_q3_damage_scopes *owner)
{ return owner && !owner->current && !owner->retained; }
bool application_q3_damage_scopes_quiescent(const application_q3_damage_scopes *owner)
{ return owner && !owner->current; }
bool application_q3_damage_scopes_destroy(application_q3_damage_scopes **out, qa_error *error)
{
    if (!out || !*out) return true;
    application_q3_damage_scopes *owner = *out;
    if (owner->current) return fail(error, QA_ERROR_ARGUMENT, "Original Q3 damage scopes still borrow source frames");
    while (owner->retained) {
        application_q3_damage_frame *frame = owner->retained;
        if (!frame_close(frame, error)) return false;
        owner->retained = frame->retained_next; frame_free(frame);
    }
    free(owner); *out = NULL; return true;
}
bool application_q3_damage_scope_run(application_q3_damage_scopes *owner,
    const qa_qvm_call *call, const application_q3_combat_actor *source,
    const qa_damage_request *request, qa_damage_observer *observer, qa_damage_result *out, qa_error *error)
{
    if (!owner || !call || !source || !request || !observer || !out || owner->retained ||
        source->role != owner->role || source->profile != owner->profile || call->vm != owner->role->vm ||
        !qa_actor_id_equal(source->actor, request->target) || !application_q3_combat_actor_current(source, error))
        return fail(error, QA_ERROR_ARGUMENT, "Original Q3 damage scope lost its entered actor namespace");
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    int32_t target;
    if (call->instruction != d->callbacks.damage ||
        !qa_qvm_call_argument(call, d->damage_call.positions[Q3_DAMAGE_TARGET], &target, error) ||
        (uint32_t)target != source->entity)
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
    frame->owner = owner; frame->source = *source; frame->call = *call;
    frame->request = request; frame->observer = observer; frame->active = true;
    frame->previous = owner->current; owner->current = frame;
    frame->reactions[0] = (reaction_watch){.frame = frame, .offset = d->reactions.pain,
        .arguments = d->reactions.pain_call, .kind = QA_REACTION_PAIN};
    frame->reactions[1] = (reaction_watch){.frame = frame, .offset = d->reactions.die,
        .arguments = d->reactions.die_call, .kind = QA_REACTION_DEATH};
    qa_combat_state state; bool ok = application_q3_combat_state_read(&frame->source, &state, error) &&
        application_q3_combat_velocity(&frame->source, &frame->velocity, error);
    if (ok) { frame->health = state.health; frame->armor = state.armor; }
    qa_qvm_write_range *ranges = (qa_qvm_write_range *)(frame + 1);
    size_t count = 0;
    if (ok) {
        ranges[count++] = (qa_qvm_write_range){source->entity + d->fields.health, 4};
        ranges[count++] = (qa_qvm_write_range){source->player ? source->player + 32 : source->entity + 36, 12};
        ranges[count++] = (qa_qvm_write_range){source->entity + d->reactions.pain, 4};
        ranges[count++] = (qa_qvm_write_range){source->entity + d->reactions.die, 4};
        if (source->player) {
            ranges[count++] = (qa_qvm_write_range){source->player + 184 + d->armor.points_stat * 4, 4};
            if (d->armor.mode_count) {
                ranges[count++] = (qa_qvm_write_range){source->player + 184 + d->armor.tier_stat * 4, 4};
                for (size_t i = 0; i < d->armor.mode_count; ++i)
                    ranges[count++] = (qa_qvm_write_range){d->armor.modes[i].offset, 4};
            }
        }
        ok = reaction_refresh(frame->reactions, error) && reaction_refresh(frame->reactions + 1, error) &&
            qa_qvm_observe_writes_owned(call->vm, ranges, count, store_publish, store_after,
                store_dispose, frame, &frame->writes, error);
    }
    int32_t ignored;
    if (ok) ok = qa_qvm_proceed(call, &ignored, error);
    qa_error original = error ? *error : (qa_error){0}; qa_error cleanup = {0};
    bool closed = frame_close(frame, &cleanup); owner->current = frame->previous;
    if (closed) { if (ok) *out = frame->result; frame_free(frame); }
    else { frame->retained_next = owner->retained; owner->retained = frame; }
    if (error && (!ok || !closed)) *error = ok ? cleanup : original;
    return ok && closed;
}
application_q3_damage_frame *application_q3_damage_scope_current(application_q3_damage_scopes *owner, uint32_t target)
{
    application_q3_damage_frame *frame = owner ? owner->current : NULL;
    return frame && frame->active && !frame->reacting && frame->source.entity == target ? frame : NULL;
}
const qa_damage_request *application_q3_damage_scope_request(const application_q3_damage_frame *frame)
{ return frame && frame->active ? frame->request : NULL; }
const qa_qvm_call *application_q3_damage_scope_call(const application_q3_damage_frame *frame)
{ return frame && frame->active ? &frame->call : NULL; }
const application_q3_combat_actor *application_q3_damage_scope_actor(const application_q3_damage_frame *frame)
{ return frame && frame->active ? &frame->source : NULL; }
