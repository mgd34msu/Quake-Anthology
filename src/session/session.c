/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qa/session.h"

#include <stdlib.h>
#include <string.h>

#define NO_TURN UINT32_MAX

typedef struct component_state {
    qa_component component;
    qa_clock_state clock;
    uint64_t order;
    uint32_t steps;
    bool active;
    bool retiring;
    bool in_frame;
} component_state;

typedef struct actor_execution {
    qa_actor_id actor;
    qa_actor_owner provider;
} actor_execution;

typedef struct actor_turn {
    qa_actor_id actor;
    uint64_t provider_order;
    uint32_t source_slot;
} actor_turn;

struct qa_session {
    qa_actor_registry *actors;
    qa_scheduler *scheduler;
    qa_strings *strings;
    component_state *components;
    actor_execution *executions;
    actor_turn *turns;
    uint32_t *turn_positions;
    uint32_t turn_count;
    uint64_t turn_revision;
    qa_session_options options;
    uint64_t elapsed_ns;
    uint64_t next_order;
    uint32_t notification_depth;
    bool stepping;
    bool transitioning;
    bool faulted;
    qa_error error;
    const qa_invocation *invocation;
    void *world;
    qa_cleanup_fn close_world;
};

static bool fail(qa_error *error, qa_status code, const char *message)
{
    qa_error_set(error, code, 0, "%s", message);
    return false;
}

static void fault(qa_session *session, const qa_error *error)
{
    if (session->faulted) return;
    session->faulted = true;
    if (error != NULL && error->code != QA_OK) session->error = *error;
    else qa_error_set(&session->error, QA_ERROR_ARGUMENT, 0, "Session callback failed");
}

static component_state *component(const qa_session *session, qa_actor_owner owner)
{
    for (uint32_t i = 0; i < session->options.component_capacity; ++i)
        if (session->components[i].active && session->components[i].component.owner == owner)
            return &session->components[i];
    return NULL;
}

static int compare_turn(const qa_session *session, const actor_turn *left, const actor_turn *right)
{
    if (session->options.mixed_order && left->provider_order != right->provider_order)
        return left->provider_order < right->provider_order ? -1 : 1;
    if (left->source_slot != right->source_slot) return left->source_slot < right->source_slot ? -1 : 1;
    if (left->provider_order != right->provider_order) return left->provider_order < right->provider_order ? -1 : 1;
    if (left->actor.slot != right->actor.slot) return left->actor.slot < right->actor.slot ? -1 : 1;
    return 0;
}

static uint32_t after_turn(const qa_session *session, const actor_turn *cursor)
{
    uint32_t low = 0, high = session->turn_count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (compare_turn(session, &session->turns[middle], cursor) <= 0) low = middle + 1u;
        else high = middle;
    }
    return low;
}

static void remove_turn(qa_session *session, qa_actor_id actor)
{
    uint32_t index = session->turn_positions[actor.slot];
    if (index == NO_TURN || !qa_actor_id_equal(session->turns[index].actor, actor)) return;
    --session->turn_count;
    memmove(session->turns + index, session->turns + index + 1u,
            (size_t)(session->turn_count - index) * sizeof(*session->turns));
    session->turn_positions[actor.slot] = NO_TURN;
    for (uint32_t i = index; i < session->turn_count; ++i) session->turn_positions[session->turns[i].actor.slot] = i;
}

static void insert_turn(qa_session *session, const qa_actor_record *actor, uint64_t order)
{
    uint32_t existing = session->turn_positions[actor->id.slot];
    if (existing != NO_TURN) {
        if (qa_actor_id_equal(session->turns[existing].actor, actor->id)) return;
        remove_turn(session, session->turns[existing].actor);
    }
    actor_turn turn = {actor->id, order, actor->has_source ? actor->source_slot : actor->id.slot};
    uint32_t index = after_turn(session, &turn);
    memmove(session->turns + index + 1u, session->turns + index,
            (size_t)(session->turn_count - index) * sizeof(*session->turns));
    session->turns[index] = turn;
    ++session->turn_count;
    for (uint32_t i = index; i < session->turn_count; ++i) session->turn_positions[session->turns[i].actor.slot] = i;
}

static void accounted_mutation(qa_session *session)
{
    uint64_t revision = qa_actors_revision(session->actors);
    if (revision - session->turn_revision == 1u) session->turn_revision = revision;
}

/* Mutable engine-service borrows may allocate directly. Release always reaches
 * our hook. Reconcile unaccounted allocations without rebuilding or sorting. */
static bool reconcile_turns(qa_session *session, qa_error *error)
{
    if (session->turn_revision == qa_actors_revision(session->actors)) return true;
    uint32_t i = 0;
    while (i < session->turn_count) {
        qa_actor_id actor = session->turns[i].actor;
        if (qa_actors_get(session->actors, actor) == NULL) remove_turn(session, actor);
        else ++i;
    }
    uint32_t cursor = 0;
    const qa_actor_record *actor;
    while (qa_actors_next(session->actors, &cursor, &actor)) {
        uint32_t position = session->turn_positions[actor->id.slot];
        if (position != NO_TURN && qa_actor_id_equal(session->turns[position].actor, actor->id)) continue;
        component_state *owner = component(session, actor->owner);
        if (owner == NULL) return fail(error, QA_ERROR_NOT_FOUND, "Borrowed registry actor has no registered component");
        insert_turn(session, actor, owner->order);
    }
    session->turn_revision = qa_actors_revision(session->actors);
    return true;
}

qa_clock_config qa_clock_defaults(qa_clock_kind kind)
{
    qa_clock_config result = {.kind = kind};
    switch (kind) {
    case QA_CLOCK_NETQUAKE: case QA_CLOCK_QUAKEWORLD:
        result.minimum_frame_ns = UINT64_C(1000000);
        result.maximum_frame_ns = UINT64_C(100000000);
        break;
    case QA_CLOCK_Q2_CLASSIC:
        result.interval_ns = UINT64_C(100000000);
        result.maximum_steps = 2;
        break;
    case QA_CLOCK_Q2_RERELEASE:
        result.interval_ns = UINT64_C(25000000);
        result.maximum_steps = 8;
        break;
    case QA_CLOCK_Q3:
        result.interval_ns = UINT64_C(50000000);
        break;
    }
    return result;
}

static bool valid_clock(const qa_clock_config *clock)
{
    if (clock->kind < QA_CLOCK_NETQUAKE || clock->kind > QA_CLOCK_Q3) return false;
    if (clock->kind == QA_CLOCK_Q2_CLASSIC && clock->interval_ns != UINT64_C(100000000)) return false;
    if (clock->kind >= QA_CLOCK_Q2_CLASSIC && clock->interval_ns == 0) return false;
    if (clock->interval_ns == 0 && (clock->minimum_frame_ns == 0
        || clock->maximum_frame_ns < clock->minimum_frame_ns || clock->initial_lead_ns != 0)) return false;
    return clock->initial_lead_ns <= clock->interval_ns
        && clock->initial_time_ns <= UINT64_MAX - clock->initial_lead_ns;
}

static qa_clock_state initial_clock(const qa_session *session, const qa_component *value)
{
    qa_clock_state state = {0};
    state.host_origin_ns = session->elapsed_ns;
    state.debt_ns = value->clock.initial_lead_ns;
    state.frame = (qa_source_frame){value->owner, value->clock.kind, QA_FRAME_EXIT, 0,
                                   value->clock.initial_time_ns, 0, value->clock.initial_time_ns};
    return state;
}

static void actor_released(void *context, qa_actor_registry *actors, qa_actor_record released)
{
    qa_session *session = context;
    (void)actors;
    ++session->notification_depth;
    remove_turn(session, released.id);
    accounted_mutation(session);
    qa_scheduler_cancel(session->scheduler, released.id);
    actor_execution *execution = &session->executions[released.id.slot];
    if (qa_actor_id_equal(execution->actor, released.id)) *execution = (actor_execution){0};
    qa_error error = {0};
    if (session->options.actor_released != NULL
        && !session->options.actor_released(session->options.release_context, session, released, &error)) fault(session, &error);
    component_state *owner = component(session, released.owner);
    if (owner != NULL && owner->component.actor_released != NULL)
        owner->component.actor_released(owner->component.state, session, released);
    --session->notification_depth;
}

typedef struct think_invocation {
    qa_think_fn callback;
    void *context;
    qa_actor_id actor;
    const qa_source_frame *frame;
} think_invocation;

static bool invoke_think(void *context, qa_session *session, qa_error *error)
{
    think_invocation *think = context;
    (void)session;
    return think->callback(think->context, think->actor, think->frame, error);
}

static bool dispatch_think(void *context, qa_think_fn callback, void *callback_context,
                            qa_actor_id actor, const qa_source_frame *frame, qa_error *error)
{
    think_invocation think = {callback, callback_context, actor, frame};
    return qa_session_invoke(context, actor, QA_INVOKE_THINK, invoke_think, &think, error);
}

bool qa_session_create(const qa_session_options *options, qa_session **out, qa_error *error)
{
    if (options == NULL || out == NULL || options->actor_capacity == 0 || options->component_capacity == 0)
        return fail(error, QA_ERROR_ARGUMENT, "Session needs actor and component capacity");
    qa_session *session = calloc(1, sizeof(*session));
    if (session == NULL) return fail(error, QA_ERROR_MEMORY, "Cannot allocate session");
    session->options = *options;
    session->components = calloc(options->component_capacity, sizeof(*session->components));
    session->executions = calloc(options->actor_capacity, sizeof(*session->executions));
    session->turns = calloc(options->actor_capacity, sizeof(*session->turns));
    session->turn_positions = calloc(options->actor_capacity, sizeof(*session->turn_positions));
    if (session->components == NULL || session->executions == NULL || session->turns == NULL || session->turn_positions == NULL) {
        fail(error, QA_ERROR_MEMORY, "Cannot allocate session storage");
        goto failed;
    }
    for (uint32_t i = 0; i < options->actor_capacity; ++i) session->turn_positions[i] = NO_TURN;
    if (!qa_strings_create(&session->strings, error)) goto failed;
    if (!qa_actors_create(options->actor_capacity, actor_released, session, &session->actors, error)) goto failed;
    if (!qa_scheduler_create(session->actors, options->component_capacity, options->mixed_order,
                              dispatch_think, session, &session->scheduler, error)) goto failed;
    *out = session;
    return true;
failed:
    (void)qa_actors_destroy(session->actors, NULL);
    qa_strings_destroy(session->strings);
    free(session->components); free(session->executions); free(session->turns); free(session->turn_positions); free(session);
    return false;
}

bool qa_session_safe(const qa_session *session)
{
    return session != NULL && !session->stepping && !session->transitioning
        && session->invocation == NULL && session->notification_depth == 0
        && !qa_scheduler_active(session->scheduler);
}

bool qa_session_faulted(const qa_session *session) { return session != NULL && session->faulted; }
const qa_error *qa_session_error(const qa_session *session) { return session == NULL || !session->faulted ? NULL : &session->error; }
uint64_t qa_session_elapsed(const qa_session *session) { return session == NULL ? 0 : session->elapsed_ns; }

bool qa_session_restore_elapsed(qa_session *session, uint64_t elapsed_ns, qa_error *error)
{
    if (!qa_session_safe(session)) return fail(error, QA_ERROR_ARGUMENT, "Clock restore requires a safe point");
    session->elapsed_ns = elapsed_ns;
    return true;
}

const qa_actor_registry *qa_session_actors(const qa_session *session) { return session == NULL ? NULL : session->actors; }
qa_actor_registry *qa_session_actor_registry(qa_session *session) { return session == NULL ? NULL : session->actors; }
qa_scheduler *qa_session_scheduler(qa_session *session) { return session == NULL ? NULL : session->scheduler; }
qa_strings *qa_session_strings(qa_session *session) { return session == NULL ? NULL : session->strings; }
void *qa_session_world(const qa_session *session) { return session == NULL ? NULL : session->world; }
const qa_invocation *qa_session_current(const qa_session *session) { return session == NULL ? NULL : session->invocation; }

bool qa_session_add(qa_session *session, const qa_component *value, qa_error *error)
{
    if (!qa_session_safe(session) || value == NULL || !valid_clock(&value->clock))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid component or unsafe registration");
    if (qa_strings_text(session->strings, value->owner).data == NULL)
        return fail(error, QA_ERROR_ARGUMENT, "Component owner is not interned in this session");
    if (component(session, value->owner) != NULL) return fail(error, QA_ERROR_ARGUMENT, "Component owner already registered");
    if (session->next_order == UINT64_MAX) return fail(error, QA_ERROR_MEMORY, "Component order exhausted");
    component_state *empty = NULL;
    for (uint32_t i = 0; i < session->options.component_capacity; ++i)
        if (!session->components[i].active) { empty = &session->components[i]; break; }
    if (empty == NULL) return fail(error, QA_ERROR_MEMORY, "Component capacity exhausted");
    if (!qa_scheduler_register(session->scheduler, value->owner, value->clock.kind, error)) return false;
    *empty = (component_state){.component = *value, .clock = initial_clock(session, value),
                               .order = session->next_order++, .active = true};
    return true;
}

bool qa_session_remove(qa_session *session, qa_actor_owner owner, qa_error *error)
{
    if (!qa_session_safe(session)) return fail(error, QA_ERROR_ARGUMENT, "Component removal requires a safe point");
    component_state *entry = component(session, owner);
    if (entry == NULL) return fail(error, QA_ERROR_NOT_FOUND, "Unknown component owner");
    uint32_t cursor = 0;
    const qa_actor_record *actor;
    while (qa_actors_next(session->actors, &cursor, &actor)) {
        actor_execution execution = session->executions[actor->id.slot];
        if (actor->owner != owner && qa_actor_id_equal(execution.actor, actor->id) && execution.provider == owner)
            return fail(error, QA_ERROR_ARGUMENT, "Foreign actor still uses this component");
    }
    session->transitioning = true;
    entry->retiring = true;
    cursor = 0;
    while (qa_actors_next(session->actors, &cursor, &actor))
        if (actor->owner == owner) (void)qa_actors_release(session->actors, actor->id, NULL);
    (void)qa_scheduler_unregister(session->scheduler, owner, NULL);
    qa_component retired = entry->component;
    *entry = (component_state){0};
    if (retired.close != NULL) retired.close(retired.state);
    session->transitioning = false;
    if (session->faulted) { if (error != NULL) *error = session->error; return false; }
    return true;
}

bool qa_session_pause(qa_session *session, qa_actor_owner owner, bool paused, qa_error *error)
{
    if (!qa_session_safe(session)) return fail(error, QA_ERROR_ARGUMENT, "Pause change requires a safe point");
    component_state *entry = component(session, owner);
    if (entry == NULL) return fail(error, QA_ERROR_NOT_FOUND, "Unknown component owner");
    entry->clock.paused = paused;
    return true;
}

bool qa_session_clock(const qa_session *session, qa_actor_owner owner, qa_clock_state *out)
{
    if (session == NULL || out == NULL) return false;
    component_state *entry = component(session, owner);
    if (entry == NULL) return false;
    *out = entry->clock;
    return true;
}

bool qa_session_restore_clock(qa_session *session, qa_actor_owner owner,
                              const qa_clock_state *state, qa_error *error)
{
    if (!qa_session_safe(session) || state == NULL)
        return fail(error, QA_ERROR_ARGUMENT, "Clock restore requires a safe point and state");
    component_state *entry = component(session, owner);
    if (entry == NULL) return fail(error, QA_ERROR_NOT_FOUND, "Unknown component owner");
    if (state->frame.provider != owner || state->frame.kind != entry->component.clock.kind
        || state->frame.number != state->frame_number || state->frame.phase != QA_FRAME_EXIT
        || entry->component.clock.initial_time_ns > UINT64_MAX - state->elapsed_ns
        || state->frame.start_ns > UINT64_MAX - state->frame.elapsed_ns
        || state->frame.time_ns != state->frame.start_ns + state->frame.elapsed_ns
        || state->frame.time_ns != entry->component.clock.initial_time_ns + state->elapsed_ns
        || state->elapsed_ns > UINT64_MAX - state->debt_ns)
        return fail(error, QA_ERROR_FORMAT, "Invalid provider clock checkpoint");
    entry->clock = *state;
    return true;
}

bool qa_session_allocate(qa_session *session, qa_actor_owner owner,
                          qa_actor_definition definition, bool has_source,
                          uint32_t source_slot, qa_actor_id *out, qa_error *error)
{
    if (session == NULL || out == NULL || session->transitioning || session->faulted)
        return fail(error, QA_ERROR_ARGUMENT, "Session cannot allocate actors in this state");
    if (qa_strings_text(session->strings, definition).data == NULL)
        return fail(error, QA_ERROR_ARGUMENT, "Actor definition is not interned in this session");
    component_state *entry = component(session, owner);
    if (entry == NULL || entry->retiring) return fail(error, QA_ERROR_NOT_FOUND, "Actor owner is unavailable");
    qa_actor_id actor;
    bool ok = has_source ? qa_actors_allocate_source(session->actors, owner, source_slot, definition, &actor, error)
                         : qa_actors_allocate(session->actors, owner, definition, &actor, error);
    if (!ok) return false;
    session->executions[actor.slot] = (actor_execution){actor, owner};
    insert_turn(session, qa_actors_get(session->actors, actor), entry->order);
    accounted_mutation(session);
    *out = actor;
    return true;
}

bool qa_session_release(qa_session *session, qa_actor_id actor, qa_error *error)
{
    if (session == NULL) return fail(error, QA_ERROR_ARGUMENT, "Missing session");
    if (!qa_actors_release(session->actors, actor, error)) return false;
    if (session->faulted) { if (error != NULL) *error = session->error; return false; }
    return true;
}

bool qa_session_bind_execution(qa_session *session, qa_actor_id actor,
                                qa_actor_owner provider_owner, qa_error *error)
{
    if (session == NULL || session->transitioning || session->faulted
        || qa_actors_get(session->actors, actor) == NULL)
        return fail(error, QA_ERROR_ARGUMENT, "Cannot bind stale actor execution");
    component_state *entry = component(session, provider_owner);
    if (entry == NULL || entry->retiring) return fail(error, QA_ERROR_NOT_FOUND, "Execution provider unavailable");
    const qa_think *pending = qa_scheduler_pending(session->scheduler, actor);
    if (pending != NULL && pending->execution_provider != provider_owner)
        return fail(error, QA_ERROR_ARGUMENT, "Cancel the previous provider's think before rebinding execution");
    session->executions[actor.slot] = (actor_execution){actor, provider_owner};
    return true;
}

bool qa_session_execution(const qa_session *session, qa_actor_id actor, qa_actor_owner *out)
{
    if (session == NULL || out == NULL) return false;
    const qa_actor_record *record = qa_actors_get(session->actors, actor);
    if (record == NULL) return false;
    actor_execution execution = session->executions[actor.slot];
    *out = qa_actor_id_equal(execution.actor, actor) ? execution.provider : record->owner;
    return true;
}

bool qa_session_schedule(qa_session *session, const qa_think *think, qa_error *error)
{
    qa_actor_owner execution;
    if (session == NULL || think == NULL || session->transitioning || session->faulted
        || !qa_session_execution(session, think->actor, &execution)
        || execution != think->execution_provider)
        return fail(error, QA_ERROR_ARGUMENT, "Think provider disagrees with actor execution");
    return qa_scheduler_schedule(session->scheduler, think, error);
}

bool qa_session_invoke(qa_session *session, qa_actor_id actor, qa_invocation_kind kind,
                        qa_invocation_fn callback, void *context, qa_error *error)
{
    if (session == NULL || callback == NULL || kind < QA_INVOKE_THINK || kind > QA_INVOKE_PHYSICS
        || session->transitioning || session->faulted)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid actor invocation");
    if (qa_actors_get(session->actors, actor) == NULL) return fail(error, QA_ERROR_NOT_FOUND, "Invocation actor is stale");
    qa_invocation invocation = {actor, kind, session->invocation};
    session->invocation = &invocation;
    bool ok = callback(context, session, error);
    session->invocation = invocation.parent;
    if (!ok) fault(session, error);
    if (session->faulted) { if (error != NULL) *error = session->error; return false; }
    return true;
}

typedef struct physics_invocation {
    component_state *provider;
    qa_actor_id actor;
} physics_invocation;

static bool invoke_physics(void *context, qa_session *session, qa_error *error)
{
    physics_invocation *invocation = context;
    qa_component *value = &invocation->provider->component;
    return value->actor_frame(value->state, session, invocation->actor, &invocation->provider->clock.frame, error);
}

static bool actor_frames(qa_session *session, qa_error *error)
{
    actor_turn cursor = {0};
    bool have_cursor = false;
    uint32_t index = 0;
    uint64_t revision = qa_actors_revision(session->actors);
    for (;;) {
        if (!reconcile_turns(session, error)) return false;
        if (have_cursor && revision != qa_actors_revision(session->actors)) index = after_turn(session, &cursor);
        revision = qa_actors_revision(session->actors);
        if (index == session->turn_count) return true;
        actor_turn turn = session->turns[index++];
        cursor = turn;
        have_cursor = true;
        const qa_actor_record *actor = qa_actors_get(session->actors, turn.actor);
        if (actor == NULL) continue;
        actor_execution execution = session->executions[actor->id.slot];
        component_state *provider_state = component(session,
            qa_actor_id_equal(execution.actor, actor->id) ? execution.provider : actor->owner);
        if (provider_state == NULL || !provider_state->in_frame || provider_state->component.actor_frame == NULL) continue;
        provider_state->clock.frame.phase = QA_ENTITY_PHYSICS;
        physics_invocation invocation = {provider_state, actor->id};
        if (!qa_session_invoke(session, actor->id, QA_INVOKE_PHYSICS, invoke_physics, &invocation, error)) return false;
        if (session->options.after_actor != NULL
            && !session->options.after_actor(session->options.release_context, session, turn.actor,
                                             &provider_state->clock.frame, error)) return false;
    }
}

/* A provider's lead changes host eligibility, never source callback time. */
static bool next_frame(const component_state *entry, uint64_t *duration, uint64_t *deadline)
{
    const qa_clock_config *config = &entry->component.clock;
    if (!entry->active || entry->retiring || entry->clock.paused) return false;
    uint64_t step = config->interval_ns;
    if (step == 0) {
        if (entry->clock.debt_ns < config->minimum_frame_ns) return false;
        step = entry->clock.debt_ns < config->maximum_frame_ns ? entry->clock.debt_ns : config->maximum_frame_ns;
    }
    if (entry->clock.debt_ns < step) return false;
    uint64_t elapsed = entry->clock.elapsed_ns + step;
    uint64_t relative = elapsed >= config->initial_lead_ns ? elapsed - config->initial_lead_ns : 0;
    *duration = step;
    *deadline = entry->clock.host_origin_ns + relative;
    return true;
}

static component_state *ordered_component(qa_session *session, uint64_t after, bool first)
{
    component_state *next = NULL;
    for (uint32_t i = 0; i < session->options.component_capacity; ++i) {
        component_state *candidate = &session->components[i];
        if (!candidate->active || (!first && candidate->order <= after)) continue;
        if (next == NULL || candidate->order < next->order) next = candidate;
    }
    return next;
}

bool qa_session_advance(qa_session *session, uint64_t elapsed_ns, qa_error *error)
{
    if (!qa_session_safe(session) || session->faulted)
        return fail(error, QA_ERROR_ARGUMENT, "Session advance requires an idle, healthy session");
    if (session->elapsed_ns > UINT64_MAX - elapsed_ns)
        return fail(error, QA_ERROR_ARGUMENT, "Session clock exhausted");
    for (uint32_t i = 0; i < session->options.component_capacity; ++i) {
        component_state *entry = &session->components[i];
        if (!entry->active) continue;
        if (entry->clock.paused) {
            if (entry->clock.host_origin_ns > UINT64_MAX - elapsed_ns)
                return fail(error, QA_ERROR_ARGUMENT, "Paused provider clock exhausted");
            continue;
        }
        if (entry->clock.debt_ns > UINT64_MAX - elapsed_ns
            || entry->clock.elapsed_ns > UINT64_MAX - entry->clock.debt_ns - elapsed_ns
            || entry->component.clock.initial_time_ns > UINT64_MAX - entry->clock.elapsed_ns - entry->clock.debt_ns - elapsed_ns)
            return fail(error, QA_ERROR_ARGUMENT, "Provider clock exhausted");
        uint64_t admitted = entry->clock.elapsed_ns + entry->clock.debt_ns + elapsed_ns;
        uint64_t relative = admitted >= entry->component.clock.initial_lead_ns
            ? admitted - entry->component.clock.initial_lead_ns : 0;
        if (entry->clock.host_origin_ns > UINT64_MAX - relative)
            return fail(error, QA_ERROR_ARGUMENT, "Provider host deadline exhausted");
    }
    session->elapsed_ns += elapsed_ns;
    for (uint32_t i = 0; i < session->options.component_capacity; ++i) {
        component_state *entry = &session->components[i];
        entry->steps = 0;
        if (!entry->active) continue;
        if (entry->clock.paused) entry->clock.host_origin_ns += elapsed_ns;
        else entry->clock.debt_ns += elapsed_ns;
    }
    session->stepping = true;
    bool ok = true;
    for (;;) {
        bool found = false, limited = false;
        uint64_t deadline = 0;
        for (uint32_t i = 0; i < session->options.component_capacity; ++i) {
            uint64_t step, due;
            component_state *candidate = &session->components[i];
            if (!next_frame(candidate, &step, &due)) continue;
            bool at_limit = candidate->component.clock.maximum_steps != 0
                && candidate->steps >= candidate->component.clock.maximum_steps;
            if (!found || due < deadline) { found = true; deadline = due; limited = at_limit; }
            else if (due == deadline && at_limit) limited = true;
        }
        /* No provider may pass an earlier, deferred boundary in the same world. */
        if (!found || limited) break;
        for (uint32_t i = 0; i < session->options.component_capacity; ++i) {
            component_state *entry = &session->components[i];
            uint64_t step, due;
            entry->in_frame = next_frame(entry, &step, &due) && due == deadline;
            if (!entry->in_frame) continue;
            if (entry->clock.frame_number == UINT64_MAX) { ok = fail(error, QA_ERROR_ARGUMENT, "Provider frame counter exhausted"); break; }
            uint64_t start = entry->component.clock.initial_time_ns + entry->clock.elapsed_ns;
            entry->clock.frame = (qa_source_frame){entry->component.owner, entry->component.clock.kind,
                                                  QA_FRAME_ENTRY, ++entry->clock.frame_number, start, step, start};
            if (entry->component.clock.kind != QA_CLOCK_NETQUAKE && entry->component.clock.kind != QA_CLOCK_QUAKEWORLD)
                entry->clock.frame.time_ns += step;
            entry->clock.elapsed_ns += step;
            entry->clock.debt_ns -= step;
            ++entry->steps;
        }
        if (!ok) break;
        component_state *entry = ordered_component(session, 0, true);
        while (entry != NULL) {
            if (entry->in_frame && entry->component.begin_frame != NULL
                && !entry->component.begin_frame(entry->component.state, session, &entry->clock.frame, error)) { ok = false; break; }
            if (session->faulted) { ok = false; break; }
            entry = ordered_component(session, entry->order, false);
        }
        if (!ok || session->faulted) { ok = false; break; }
        if (!actor_frames(session, error) || session->faulted) { ok = false; break; }
        entry = ordered_component(session, 0, true);
        while (entry != NULL) {
            if (entry->in_frame) {
                entry->clock.frame.phase = QA_CLIENT_END_FRAME;
                if (entry->component.end_frame != NULL
                    && !entry->component.end_frame(entry->component.state, session, &entry->clock.frame, error)) { ok = false; break; }
                entry->clock.frame.phase = QA_FRAME_EXIT;
                entry->clock.frame.time_ns = entry->clock.frame.start_ns + entry->clock.frame.elapsed_ns;
                entry->in_frame = false;
            }
            if (session->faulted) { ok = false; break; }
            entry = ordered_component(session, entry->order, false);
        }
        if (!ok || session->faulted) { ok = false; break; }
    }
    session->stepping = false;
    for (uint32_t i = 0; i < session->options.component_capacity; ++i) session->components[i].in_frame = false;
    if (!ok) fault(session, error);
    if (session->faulted && error != NULL) *error = session->error;
    return ok;
}

bool qa_session_replace_world(qa_session *session, void *candidate, qa_cleanup_fn close, qa_error *error)
{
    if (!qa_session_safe(session) || (candidate != NULL && candidate == session->world))
        return fail(error, QA_ERROR_ARGUMENT, "World publication requires a distinct candidate and a safe point");
    session->transitioning = true;
    session->faulted = false;
    session->error = (qa_error){0};
    (void)qa_actors_clear(session->actors, NULL);
    (void)qa_scheduler_clear(session->scheduler, NULL);
    if (session->faulted) {
        session->transitioning = false;
        if (error != NULL) *error = session->error;
        return false;
    }
    void *old = session->world;
    qa_cleanup_fn old_close = session->close_world;
    session->world = candidate;
    session->close_world = close;
    for (uint32_t i = 0; i < session->options.component_capacity; ++i)
        if (session->components[i].active) session->components[i].clock = initial_clock(session, &session->components[i].component);
    if (old_close != NULL) old_close(old);
    session->transitioning = false;
    return true;
}

bool qa_session_destroy(qa_session *session, qa_error *error)
{
    if (session == NULL) return true;
    if (!qa_session_safe(session)) return fail(error, QA_ERROR_ARGUMENT, "Session destroy requires a safe point");
    session->transitioning = true;
    (void)qa_actors_clear(session->actors, NULL);
    for (;;) {
        component_state *last = NULL;
        for (uint32_t i = 0; i < session->options.component_capacity; ++i)
            if (session->components[i].active && (last == NULL || session->components[i].order > last->order)) last = &session->components[i];
        if (last == NULL) break;
        qa_component retired = last->component;
        last->active = false;
        if (retired.close != NULL) retired.close(retired.state);
    }
    if (session->close_world != NULL) session->close_world(session->world);
    (void)qa_scheduler_destroy(session->scheduler, NULL);
    (void)qa_actors_destroy(session->actors, NULL);
    qa_strings_destroy(session->strings);
    bool ok = !session->faulted;
    if (!ok && error != NULL) *error = session->error;
    free(session->components); free(session->executions); free(session->turns); free(session->turn_positions); free(session);
    return ok;
}
