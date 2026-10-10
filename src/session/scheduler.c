#include "qa/scheduler.h"

#include <stdlib.h>
#include <string.h>

#define NO_HEAP_SLOT UINT32_MAX

typedef struct provider_clock {
    qa_actor_owner owner;
    qa_ruleset_id kind;
    uint64_t order;
    bool active;
    qa_scheduler_admission *reservation;
} provider_clock;

struct qa_scheduler_admission {
    qa_scheduler *scheduler;
    provider_clock *slot;
    qa_actor_owner owner;
    qa_ruleset_id kind;
};

typedef struct pending_think {
    qa_think think;
    qa_actor_owner owner;
    uint64_t provider_order;
    uint32_t source_slot;
    uint32_t heap_slot;
    bool pending;
} pending_think;

struct qa_scheduler {
    qa_actor_registry *actors;
    pending_think *pending;
    provider_clock *providers;
    uint32_t *heap;
    uint32_t capacity;
    uint32_t provider_capacity;
    uint32_t heap_count;
    uint32_t dispatch_depth;
    uint64_t next_order;
    uint32_t admissions;
    pending_think cursor;
    bool mixed;
    bool advancing;
    bool has_cursor;
    bool checkpointing;
    qa_think_dispatch_fn dispatch;
    void *context;
};

static bool fail(qa_error *error, qa_status code, const char *message)
{
    qa_error_set(error, code, 0, "%s", message);
    return false;
}

static provider_clock *provider(const qa_scheduler *scheduler, qa_actor_owner owner)
{
    for (uint32_t i = 0; i < scheduler->provider_capacity; ++i)
        if (scheduler->providers[i].active && scheduler->providers[i].owner == owner)
            return &scheduler->providers[i];
    return NULL;
}

static int compare_position(const qa_scheduler *scheduler, const pending_think *left,
                            const pending_think *right)
{
    if (scheduler->mixed && left->provider_order != right->provider_order)
        return left->provider_order < right->provider_order ? -1 : 1;
    if (left->source_slot != right->source_slot)
        return left->source_slot < right->source_slot ? -1 : 1;
    return 0;
}

static bool before_invocation(const qa_scheduler *scheduler, const pending_think *left,
                              const pending_think *right)
{
    int position = compare_position(scheduler, left, right);
    if (position != 0)
        return position < 0;
    if (left->think.sequence != right->think.sequence)
        return left->think.sequence < right->think.sequence;
    if (left->provider_order != right->provider_order)
        return left->provider_order < right->provider_order;
    return left->think.actor.slot < right->think.actor.slot;
}

static void heap_swap(qa_scheduler *scheduler, uint32_t a, uint32_t b)
{
    uint32_t temporary = scheduler->heap[a];
    scheduler->heap[a] = scheduler->heap[b];
    scheduler->heap[b] = temporary;
    scheduler->pending[scheduler->heap[a]].heap_slot = a;
    scheduler->pending[scheduler->heap[b]].heap_slot = b;
}

static void heap_up(qa_scheduler *scheduler, uint32_t index)
{
    while (index != 0) {
        uint32_t parent = (index - 1u) / 2u;
        if (!before_invocation(scheduler, &scheduler->pending[scheduler->heap[index]],
                               &scheduler->pending[scheduler->heap[parent]]))
            break;
        heap_swap(scheduler, index, parent);
        index = parent;
    }
}

static void heap_down(qa_scheduler *scheduler, uint32_t index)
{
    while (index < scheduler->heap_count / 2u) {
        uint32_t child = index * 2u + 1u;
        if (child + 1u < scheduler->heap_count
            && before_invocation(scheduler, &scheduler->pending[scheduler->heap[child + 1u]],
                                 &scheduler->pending[scheduler->heap[child]]))
            ++child;
        if (!before_invocation(scheduler, &scheduler->pending[scheduler->heap[child]],
                               &scheduler->pending[scheduler->heap[index]]))
            break;
        heap_swap(scheduler, index, child);
        index = child;
    }
}

static void heap_remove(qa_scheduler *scheduler, pending_think *pending)
{
    uint32_t index = pending->heap_slot;
    if (index == NO_HEAP_SLOT) return;
    --scheduler->heap_count;
    pending->heap_slot = NO_HEAP_SLOT;
    if (index == scheduler->heap_count) return;
    scheduler->heap[index] = scheduler->heap[scheduler->heap_count];
    scheduler->pending[scheduler->heap[index]].heap_slot = index;
    if (index != 0
        && before_invocation(scheduler, &scheduler->pending[scheduler->heap[index]],
                             &scheduler->pending[scheduler->heap[(index - 1u) / 2u]]))
        heap_up(scheduler, index);
    else
        heap_down(scheduler, index);
}

static void heap_insert(qa_scheduler *scheduler, uint32_t actor_slot)
{
    pending_think *pending = &scheduler->pending[actor_slot];
    pending->heap_slot = scheduler->heap_count;
    scheduler->heap[scheduler->heap_count++] = actor_slot;
    heap_up(scheduler, pending->heap_slot);
}

bool qa_scheduler_create(qa_actor_registry *actors, uint32_t provider_capacity,
                          bool mixed_order, qa_think_dispatch_fn dispatch,
                          void *context, qa_scheduler **out, qa_error *error)
{
    if (actors == NULL || provider_capacity == 0 || out == NULL)
        return fail(error, QA_ERROR_ARGUMENT, "Scheduler needs actors, providers, and output");
    qa_scheduler *scheduler = calloc(1, sizeof(*scheduler));
    if (scheduler == NULL) return fail(error, QA_ERROR_MEMORY, "Cannot allocate scheduler");
    scheduler->capacity = qa_actors_capacity(actors);
    scheduler->pending = calloc(scheduler->capacity, sizeof(*scheduler->pending));
    scheduler->heap = calloc(scheduler->capacity, sizeof(*scheduler->heap));
    scheduler->providers = calloc(provider_capacity, sizeof(*scheduler->providers));
    if (scheduler->pending == NULL || scheduler->heap == NULL || scheduler->providers == NULL) {
        free(scheduler->pending); free(scheduler->heap); free(scheduler->providers); free(scheduler);
        return fail(error, QA_ERROR_MEMORY, "Cannot allocate scheduler storage");
    }
    scheduler->actors = actors;
    scheduler->provider_capacity = provider_capacity;
    scheduler->mixed = mixed_order;
    scheduler->dispatch = dispatch;
    scheduler->context = context;
    for (uint32_t i = 0; i < scheduler->capacity; ++i) scheduler->pending[i].heap_slot = NO_HEAP_SLOT;
    *out = scheduler;
    return true;
}

bool qa_scheduler_active(const qa_scheduler *scheduler)
{
    return scheduler != NULL && (scheduler->advancing || scheduler->dispatch_depth != 0 || scheduler->checkpointing);
}

bool qa_scheduler_has_admissions(const qa_scheduler *scheduler)
{
    return scheduler != NULL && scheduler->admissions != 0;
}

bool qa_scheduler_clear(qa_scheduler *scheduler, qa_error *error)
{
    if (scheduler == NULL || qa_scheduler_active(scheduler))
        return fail(error, QA_ERROR_ARGUMENT, "Scheduler clear requires an idle scheduler");
    for (uint32_t i = 0; i < scheduler->capacity; ++i) {
        scheduler->pending[i].pending = false;
        scheduler->pending[i].heap_slot = NO_HEAP_SLOT;
    }
    scheduler->heap_count = 0;
    return true;
}

bool qa_scheduler_destroy(qa_scheduler *scheduler, qa_error *error)
{
    if (scheduler == NULL) return true;
    if (scheduler->admissions != 0)
        return fail(error, QA_ERROR_ARGUMENT, "Abort scheduler admissions before destruction");
    if (!qa_scheduler_clear(scheduler, error)) return false;
    free(scheduler->pending); free(scheduler->heap); free(scheduler->providers); free(scheduler);
    return true;
}

bool qa_scheduler_register(qa_scheduler *scheduler, qa_actor_owner owner,
                            qa_ruleset_id kind, qa_error *error)
{
    qa_scheduler_admission *token;
    if (!qa_scheduler_prepare(scheduler, owner, kind, 0, &token, error)) return false;
    if (qa_scheduler_admission_commit(token, error)) return true;
    qa_scheduler_admission_abort(token);
    return false;
}

bool qa_scheduler_prepare(qa_scheduler *scheduler, qa_actor_owner owner, qa_ruleset_id kind,
                           qa_actor_owner retiring_owner, qa_scheduler_admission **out, qa_error *error)
{
    if (scheduler == NULL || out == NULL || qa_scheduler_active(scheduler) || kind < QA_RULESET_NETQUAKE || kind > QA_RULESET_Q3)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid scheduler provider registration");
    if (provider(scheduler, owner) != NULL && owner != retiring_owner)
        return fail(error, QA_ERROR_ARGUMENT, "Duplicate scheduler provider");
    provider_clock *slot = retiring_owner ? provider(scheduler, retiring_owner) : NULL;
    if (retiring_owner && (slot == NULL || slot->reservation != NULL))
        return fail(error, QA_ERROR_ARGUMENT, "Retiring scheduler provider is absent or reserved");
    if (UINT64_MAX - scheduler->next_order <= scheduler->admissions)
        return fail(error, QA_ERROR_MEMORY, "Provider ordering exhausted");
    for (uint32_t i = 0; i < scheduler->provider_capacity; ++i) {
        provider_clock *entry = &scheduler->providers[i];
        if (entry->reservation != NULL && entry->reservation->owner == owner)
            return fail(error, QA_ERROR_ARGUMENT, "Scheduler provider already reserved");
        if (slot == NULL && !entry->active && entry->reservation == NULL) slot = entry;
    }
    if (slot == NULL) return fail(error, QA_ERROR_MEMORY, "Scheduler provider capacity exhausted");
    qa_scheduler_admission *token = malloc(sizeof(*token));
    if (token == NULL) return fail(error, QA_ERROR_MEMORY, "Cannot allocate scheduler admission");
    *token = (qa_scheduler_admission){scheduler, slot, owner, kind};
    slot->reservation = token;
    ++scheduler->admissions;
    *out = token;
    return true;
}

bool qa_scheduler_admission_validate(qa_scheduler_admission *token, qa_error *error)
{
    if (token == NULL || qa_scheduler_active(token->scheduler) || token->slot->reservation != token
        || token->slot->active || provider(token->scheduler, token->owner) != NULL)
        return fail(error, QA_ERROR_ARGUMENT, "Scheduler admission requires an idle unregistered provider");
    return true;
}

bool qa_scheduler_admission_commit(qa_scheduler_admission *token, qa_error *error)
{
    if (!qa_scheduler_admission_validate(token, error)) return false;
    qa_scheduler *scheduler = token->scheduler;
    *token->slot = (provider_clock){.owner = token->owner, .kind = token->kind,
        .order = scheduler->next_order++, .active = true};
    --scheduler->admissions;
    free(token);
    return true;
}

void qa_scheduler_admission_abort(qa_scheduler_admission *token)
{
    if (token == NULL) return;
    token->slot->reservation = NULL;
    --token->scheduler->admissions;
    free(token);
}

bool qa_scheduler_unregister(qa_scheduler *scheduler, qa_actor_owner owner, qa_error *error)
{
    if (scheduler == NULL || qa_scheduler_active(scheduler))
        return fail(error, QA_ERROR_ARGUMENT, "Provider removal requires an idle scheduler");
    provider_clock *clock = provider(scheduler, owner);
    if (clock == NULL) return fail(error, QA_ERROR_NOT_FOUND, "Unknown scheduler provider");
    for (uint32_t i = 0; i < scheduler->capacity; ++i) {
        pending_think *pending = &scheduler->pending[i];
        if (pending->pending && (pending->owner == owner || pending->think.execution_provider == owner)) {
            heap_remove(scheduler, pending);
            pending->pending = false;
        }
    }
    clock->active = false;
    return true;
}

bool qa_scheduler_schedule(qa_scheduler *scheduler, const qa_think *think, qa_error *error)
{
    if (scheduler == NULL || scheduler->checkpointing || think == NULL || think->callback == NULL
        || think->boundary < QA_THINK_BEFORE_PHYSICS || think->boundary > QA_THINK_AFTER_PHYSICS)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid actor think");
    const qa_actor_record *actor = qa_actors_get(scheduler->actors, think->actor);
    if (actor == NULL) return fail(error, QA_ERROR_NOT_FOUND, "Cannot schedule stale actor");
    provider_clock *owner = provider(scheduler, actor->owner);
    if (owner == NULL || provider(scheduler, think->execution_provider) == NULL)
        return fail(error, QA_ERROR_NOT_FOUND, "Think provider is unregistered");
    pending_think *pending = &scheduler->pending[think->actor.slot];
    heap_remove(scheduler, pending);
    *pending = (pending_think){*think, actor->owner, owner->order,
                              actor->has_source ? actor->source_slot : actor->id.slot, NO_HEAP_SLOT, true};
    bool ahead = !scheduler->has_cursor
        || compare_position(scheduler, &scheduler->cursor, pending) < 0;
    if (!scheduler->advancing || ahead) heap_insert(scheduler, think->actor.slot);
    return true;
}

void qa_scheduler_cancel(qa_scheduler *scheduler, qa_actor_id actor)
{
    if (scheduler == NULL || scheduler->checkpointing || actor.slot >= scheduler->capacity) return;
    pending_think *pending = &scheduler->pending[actor.slot];
    if (pending->pending && qa_actor_id_equal(pending->think.actor, actor)) {
        heap_remove(scheduler, pending);
        pending->pending = false;
    }
}

const qa_think *qa_scheduler_pending(qa_scheduler *scheduler, qa_actor_id actor)
{
    if (scheduler == NULL || actor.slot >= scheduler->capacity) return NULL;
    pending_think *pending = &scheduler->pending[actor.slot];
    if (!pending->pending || !qa_actor_id_equal(pending->think.actor, actor)) return NULL;
    if (qa_actors_get(scheduler->actors, actor) == NULL) { qa_scheduler_cancel(scheduler, actor); return NULL; }
    return &pending->think;
}

bool qa_frame_project(const qa_source_frame *frame, qa_actor_owner owner,
                       qa_ruleset_id kind, qa_source_frame *out, qa_error *error)
{
    if (frame == NULL || out == NULL || kind < QA_RULESET_NETQUAKE || kind > QA_RULESET_Q3
        || frame->start_ns > UINT64_MAX - frame->elapsed_ns)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid source frame projection");
    qa_source_frame projected = *frame;
    projected.provider = owner;
    projected.kind = kind;
    projected.time_ns = frame->start_ns;
    if (kind != QA_RULESET_NETQUAKE && kind != QA_RULESET_QUAKEWORLD) projected.time_ns += frame->elapsed_ns;
    *out = projected;
    return true;
}

static bool due_time(qa_ruleset_id kind, uint64_t due, uint64_t start,
                       uint64_t elapsed, uint64_t *time)
{
    if (due == 0) return false;
    uint64_t end = start + elapsed;
    if (kind == QA_RULESET_NETQUAKE || kind == QA_RULESET_QUAKEWORLD) {
        if (due > end) return false;
        *time = due > start ? due : start;
    } else {
        if (kind == QA_RULESET_Q2_CLASSIC) {
            if (due > end && due - end > UINT64_C(1000000)) return false;
        } else if (kind == QA_RULESET_Q3) {
            if ((double)(float)((double)due / 1000000.0) > (double)end / 1000000.0) return false;
        } else if (due > end) return false;
        *time = end;
    }
    return true;
}

static bool scheduler_run(qa_scheduler *scheduler, qa_actor_id actor,
                      const qa_source_frame *frame, qa_think_boundary boundary,
                      bool once, qa_think_result *out, qa_error *error)
{
    if (scheduler == NULL || frame == NULL || out == NULL
        || boundary < QA_THINK_BEFORE_PHYSICS || boundary > QA_THINK_AFTER_PHYSICS
        || frame->start_ns > UINT64_MAX - frame->elapsed_ns)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid think dispatch frame");
    qa_think_result result = {0, qa_actors_get(scheduler->actors, actor) != NULL};
    for (;;) {
        const qa_think *pending = qa_scheduler_pending(scheduler, actor);
        if (pending == NULL || pending->boundary != boundary) break;
        provider_clock *clock = provider(scheduler, pending->execution_provider);
        if (clock == NULL || clock->kind != frame->kind || frame->provider != pending->execution_provider)
            return fail(error, QA_ERROR_ARGUMENT, "Think execution clock mismatch");
        uint64_t time;
        if (!due_time(clock->kind, pending->due_ns, frame->start_ns, frame->elapsed_ns, &time)) break;
        qa_think think = *pending;
        qa_ruleset_id kind = clock->kind;
        qa_scheduler_cancel(scheduler, actor);
        qa_think_scope scope = {.kind = QA_THINK_WORLD_FRAME,
            .source.frame = *frame, .time_ns = time,
            .interval_start_ns = frame->start_ns, .interval_elapsed_ns = frame->elapsed_ns};
        ++scheduler->dispatch_depth;
        bool ok = scheduler->dispatch == NULL ? think.callback(think.context, actor, &scope, error)
            : scheduler->dispatch(scheduler->context, think.callback, think.context, actor, &scope, error);
        --scheduler->dispatch_depth;
        if (!ok) return false;
        ++result.invocations;
        result.alive = qa_actors_get(scheduler->actors, actor) != NULL;
        if (!result.alive || once || kind != QA_RULESET_QUAKEWORLD) break;
    }
    *out = result;
    return true;
}

bool qa_scheduler_run(qa_scheduler *scheduler, qa_actor_id actor,
    const qa_source_frame *frame, qa_think_boundary boundary,
    qa_think_result *out, qa_error *error)
{ return scheduler_run(scheduler, actor, frame, boundary, false, out, error); }

bool qa_scheduler_run_once(qa_scheduler *scheduler, qa_actor_id actor,
    const qa_source_frame *frame, qa_think_boundary boundary,
    qa_think_result *out, qa_error *error)
{ return scheduler_run(scheduler, actor, frame, boundary, true, out, error); }

bool qa_scheduler_run_command_once(qa_scheduler *scheduler, qa_actor_id actor,
    const qa_usercmd *command, uint64_t source_time_ns, uint64_t source_elapsed_ns,
    qa_think_boundary boundary,
    qa_think_result *out, qa_error *error)
{
    if (!scheduler || !command || !out || !qa_actor_id_equal(command->actor, actor) ||
        command->phase != QA_CLIENT_COMMAND ||
        boundary < QA_THINK_BEFORE_PHYSICS || boundary > QA_THINK_AFTER_PHYSICS ||
        source_time_ns > UINT64_MAX - source_elapsed_ns)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid source command think admission");
    qa_think_result result = {0, qa_actors_get(scheduler->actors, actor) != NULL};
    const qa_think *pending = qa_scheduler_pending(scheduler, actor);
    if (pending && pending->boundary == boundary) {
        provider_clock *clock = provider(scheduler, pending->execution_provider);
        if (!clock || clock->kind != command->kind || command->provider != pending->execution_provider)
            return fail(error, QA_ERROR_ARGUMENT, "Think command execution clock mismatch");
        uint64_t time;
        if (due_time(clock->kind, pending->due_ns, source_time_ns, source_elapsed_ns, &time)) {
            qa_think think = *pending;
            qa_scheduler_cancel(scheduler, actor);
            qa_think_scope scope = {.kind = QA_THINK_SOURCE_COMMAND,
                .source.command = *command, .time_ns = time,
                .interval_start_ns = source_time_ns, .interval_elapsed_ns = source_elapsed_ns};
            ++scheduler->dispatch_depth;
            bool ok = scheduler->dispatch == NULL ? think.callback(think.context, actor, &scope, error)
                : scheduler->dispatch(scheduler->context, think.callback, think.context, actor, &scope, error);
            --scheduler->dispatch_depth;
            if (!ok) return false;
            result.invocations = 1;
            result.alive = qa_actors_get(scheduler->actors, actor) != NULL;
        }
    }
    *out = result;
    return true;
}

bool qa_scheduler_advance(qa_scheduler *scheduler, const qa_source_frame *frames,
                          size_t count, qa_think_boundary boundary, qa_error *error)
{
    if (scheduler == NULL || (count != 0 && frames == NULL) || qa_scheduler_active(scheduler)
        || boundary < QA_THINK_BEFORE_PHYSICS || boundary > QA_THINK_AFTER_PHYSICS)
        return fail(error, QA_ERROR_ARGUMENT, "Scheduler traversal requires an idle scheduler");
    for (size_t i = 0; i < count; ++i) {
        provider_clock *clock = provider(scheduler, frames[i].provider);
        if (clock == NULL || clock->kind != frames[i].kind
            || frames[i].start_ns > UINT64_MAX - frames[i].elapsed_ns)
            return fail(error, QA_ERROR_ARGUMENT, "Invalid scheduler source frame");
        for (size_t j = 0; j < i; ++j)
            if (frames[j].provider == frames[i].provider) return fail(error, QA_ERROR_ARGUMENT, "Duplicate scheduler source frame");
    }
    scheduler->heap_count = 0;
    for (uint32_t i = 0; i < scheduler->capacity; ++i) scheduler->pending[i].heap_slot = NO_HEAP_SLOT;
    for (uint32_t i = 0; i < scheduler->capacity; ++i) {
        pending_think *pending = &scheduler->pending[i];
        if (!pending->pending) continue;
        if (qa_actors_get(scheduler->actors, pending->think.actor) == NULL) pending->pending = false;
        else heap_insert(scheduler, i);
    }
    scheduler->advancing = true;
    scheduler->has_cursor = false;
    bool ok = true;
    while (scheduler->heap_count != 0) {
        pending_think next = scheduler->pending[scheduler->heap[0]];
        heap_remove(scheduler, &scheduler->pending[next.think.actor.slot]);
        const qa_source_frame *frame = NULL;
        for (size_t i = 0; i < count; ++i)
            if (frames[i].provider == next.think.execution_provider) { frame = &frames[i]; break; }
        if (frame == NULL) continue;
        if (scheduler->has_cursor
            && compare_position(scheduler, &next, &scheduler->cursor) <= 0)
            continue;
        scheduler->cursor = next;
        scheduler->has_cursor = true;
        qa_think_result result;
        if (!qa_scheduler_run(scheduler, next.think.actor, frame, boundary, &result, error)) { ok = false; break; }
    }
    scheduler->advancing = false;
    scheduler->has_cursor = false;
    return ok;
}

void qa_scheduler_checkpoint_free(qa_scheduler_checkpoint *value)
{
    if (!value) return;
    free(value->providers); free(value->thinks);
    *value = (qa_scheduler_checkpoint){0};
}

bool qa_scheduler_checkpoint_capture(const qa_scheduler *scheduler,
                                      qa_scheduler_checkpoint *out, qa_error *error)
{
    if (!scheduler || !out || qa_scheduler_active(scheduler) || scheduler->admissions)
        return fail(error, QA_ERROR_ARGUMENT, "Scheduler capture requires a safe point without admissions");
    qa_scheduler_checkpoint value = {.next_order = scheduler->next_order, .mixed_order = scheduler->mixed};
    for (uint32_t i = 0; i < scheduler->provider_capacity; ++i)
        if (scheduler->providers[i].active) ++value.provider_count;
    for (uint32_t i = 0; i < scheduler->capacity; ++i)
        if (scheduler->pending[i].pending && qa_actors_get(scheduler->actors, scheduler->pending[i].think.actor))
            ++value.think_count;
    value.providers = value.provider_count ? calloc(value.provider_count, sizeof(*value.providers)) : NULL;
    value.thinks = value.think_count ? calloc(value.think_count, sizeof(*value.thinks)) : NULL;
    if ((value.provider_count && !value.providers) || (value.think_count && !value.thinks)) {
        qa_scheduler_checkpoint_free(&value);
        return fail(error, QA_ERROR_MEMORY, "Allocating scheduler checkpoint");
    }
    size_t index = 0;
    for (uint32_t i = 0; i < scheduler->provider_capacity; ++i) {
        const provider_clock *clock = scheduler->providers + i;
        if (clock->active) value.providers[index++] = (qa_scheduler_provider_checkpoint){clock->owner, clock->kind, clock->order};
    }
    index = 0;
    for (uint32_t i = 0; i < scheduler->capacity; ++i) {
        const qa_think *think = &scheduler->pending[i].think;
        if (!scheduler->pending[i].pending || !qa_actors_get(scheduler->actors, think->actor)) continue;
        qa_scheduler_think_checkpoint *saved = value.thinks + index++;
        if (!qa_actors_save_reference(scheduler->actors, think->actor, &saved->actor, error)) {
            qa_scheduler_checkpoint_free(&value); return false;
        }
        saved->execution_provider = think->execution_provider;
        saved->callback_id = think->callback_id;
        saved->due_ns = think->due_ns; saved->sequence = think->sequence; saved->boundary = think->boundary;
    }
    *out = value;
    return true;
}

bool qa_scheduler_checkpoint_restore(qa_scheduler *scheduler, const qa_scheduler_checkpoint *value,
                                      qa_think_resolve_fn resolve, void *context, qa_error *error)
{
    if (!scheduler || !value || !resolve || qa_scheduler_active(scheduler) || scheduler->admissions ||
        value->mixed_order != scheduler->mixed || value->provider_count > scheduler->provider_capacity ||
        value->think_count > scheduler->capacity || (value->provider_count && !value->providers) ||
        (value->think_count && !value->thinks))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid candidate scheduler checkpoint");
    size_t active = 0;
    for (uint32_t i = 0; i < scheduler->provider_capacity; ++i) if (scheduler->providers[i].active) ++active;
    if (active != value->provider_count) return fail(error, QA_ERROR_FORMAT, "Scheduler saved provider set differs from candidate");
    for (size_t i = 0; i < value->provider_count; ++i) {
        const qa_scheduler_provider_checkpoint *saved = value->providers + i;
        provider_clock *clock = provider(scheduler, saved->owner);
        if (!clock || clock->kind != saved->kind || saved->order >= value->next_order)
            return fail(error, QA_ERROR_FORMAT, "Invalid saved scheduler provider order or dialect");
        for (size_t j = 0; j < i; ++j)
            if (value->providers[j].owner == saved->owner || value->providers[j].order == saved->order)
                return fail(error, QA_ERROR_FORMAT, "Duplicate scheduler provider identity/order");
    }
    pending_think *prepared = calloc(scheduler->capacity, sizeof(*prepared));
    if (!prepared) return fail(error, QA_ERROR_MEMORY, "Allocating restored scheduler work");
    for (uint32_t i = 0; i < scheduler->capacity; ++i) prepared[i].heap_slot = NO_HEAP_SLOT;
    uint64_t revision = qa_actors_revision(scheduler->actors);
    scheduler->checkpointing = true;
    bool ok = true;
    for (size_t i = 0; ok && i < value->think_count; ++i) {
        const qa_scheduler_think_checkpoint *saved = value->thinks + i;
        const qa_actor_record *actor = qa_actors_resolve_saved(scheduler->actors, saved->actor);
        if (!actor || prepared[actor->id.slot].pending || !provider(scheduler, saved->execution_provider) ||
            !provider(scheduler, actor->owner) || (unsigned)saved->boundary > QA_THINK_AFTER_PHYSICS) {
            ok = fail(error, QA_ERROR_FORMAT, "Invalid saved scheduled actor/provider"); break;
        }
        pending_think *pending = prepared + actor->id.slot;
        pending->think = (qa_think){.actor = actor->id, .execution_provider = saved->execution_provider,
            .callback_id = saved->callback_id, .due_ns = saved->due_ns, .sequence = saved->sequence,
            .boundary = saved->boundary};
        ok = resolve(context, saved->execution_provider, actor->id, saved->callback_id,
                     &pending->think.callback, &pending->think.context, error);
        if (!ok) break;
        if (!pending->think.callback || qa_actors_revision(scheduler->actors) != revision ||
            scheduler->advancing || scheduler->dispatch_depth || scheduler->admissions) {
            ok = fail(error, QA_ERROR_FORMAT, "Scheduled callback resolver changed candidate ownership"); break;
        }
        pending->owner = actor->owner;
        pending->source_slot = actor->has_source ? actor->source_slot : actor->id.slot;
        for (size_t j = 0; j < value->provider_count; ++j)
            if (value->providers[j].owner == actor->owner) pending->provider_order = value->providers[j].order;
        pending->pending = true;
    }
    scheduler->checkpointing = false;
    if (ok) {
        for (size_t i = 0; i < value->provider_count; ++i)
            provider(scheduler, value->providers[i].owner)->order = value->providers[i].order;
        free(scheduler->pending);
        scheduler->pending = prepared;
        scheduler->next_order = value->next_order;
        scheduler->heap_count = 0;
        for (uint32_t i = 0; i < scheduler->capacity; ++i)
            if (prepared[i].pending) heap_insert(scheduler, i);
    } else free(prepared);
    return ok;
}
