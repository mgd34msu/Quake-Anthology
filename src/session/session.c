#include "qa/session.h"
#include "qa/source_frame_time.h"

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
    bool in_command;
    qa_source_command command;
    qa_component_admission *reservation;
} component_state;
static bool next_frame(const component_state *, uint64_t *, uint64_t *, bool *, qa_error *);
static bool pending_clock(const component_state *, uint64_t, uint64_t, component_state *, qa_error *);

struct qa_component_admission {
    qa_session *session;
    component_state *slot;
    qa_component component;
    qa_scheduler_admission *scheduler;
};

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
    qa_source_frame *active_frames;
    actor_execution *executions;
    actor_turn *turns;
    uint32_t *turn_positions;
    uint32_t turn_count;
    uint64_t turn_revision;
    qa_session_options options;
    uint64_t elapsed_ns;
    uint64_t frame_host_ns;
    uint64_t advance_elapsed_ns;
    uint64_t next_order;
    uint32_t admissions;
    uint32_t notification_depth;
    bool stepping;
    bool advancing;
    bool transitioning;
    bool restored_world_pending;
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
static bool observer_actor(const qa_session *session, const qa_actor_record *actor)
{
    return session->options.observer_actor &&
        session->options.observer_actor(session->options.release_context, session, actor);
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
        if (owner == NULL) {
            if (!observer_actor(session, actor))
                return fail(error, QA_ERROR_NOT_FOUND, "Borrowed registry actor has no registered component");
            session->executions[actor->id.slot] = (actor_execution){actor->id, 0};
            continue;
        }
        insert_turn(session, actor, owner->order);
    }
    session->turn_revision = qa_actors_revision(session->actors);
    return true;
}

qa_clock_config qa_clock_defaults(qa_clock_kind kind)
{
    qa_clock_config result = {.kind = kind};
    switch (kind) {
    case QA_CLOCK_NETQUAKE:
        result.minimum_frame_ns = UINT64_C(13888889);
        result.maximum_frame_ns = UINT64_C(100000000);
        break;
    case QA_CLOCK_QUAKEWORLD:
        result.minimum_frame_ns = UINT64_C(30000000);
        result.maximum_frame_ns = UINT64_C(100000000);
        break;
    case QA_CLOCK_Q2_CLASSIC:
        result.interval_ns = UINT64_C(100000000);
        result.maximum_steps = 1;
        break;
    case QA_CLOCK_Q2_RERELEASE:
        result.interval_ns = UINT64_C(25000000);
        result.maximum_steps = 1;
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

static bool qw_server_clock(const component_state *entry)
{
    return entry->component.clock.kind == QA_CLOCK_QUAKEWORLD && !entry->component.clock.interval_ns;
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
    const qa_think_scope *scope;
} think_invocation;

static bool invoke_think(void *context, qa_session *session, qa_error *error)
{
    think_invocation *think = context;
    if (session->options.think_dispatch)
        return session->options.think_dispatch(session->options.release_context,
            think->callback, think->context, think->actor, think->scope, error);
    return think->callback(think->context, think->actor, think->scope, error);
}

static bool dispatch_think(void *context, qa_think_fn callback, void *callback_context,
                            qa_actor_id actor, const qa_think_scope *scope, qa_error *error)
{
    qa_session *session = context;
    if (scope->kind == QA_THINK_SOURCE_COMMAND) {
        qa_source_command active;
        if (!qa_session_active_command(session, scope->source.command.provider, &active) ||
            !qa_actor_id_equal(active.actor, actor) ||
            active.kind != scope->source.command.kind || active.phase != scope->source.command.phase ||
            active.completed_frame_number != scope->source.command.completed_frame_number ||
            active.time_ns != scope->source.command.time_ns || active.elapsed_ns != scope->source.command.elapsed_ns ||
            active.host_elapsed_ns != scope->source.command.host_elapsed_ns)
            return fail(error, QA_ERROR_ARGUMENT, "Think command is not the active source admission");
    } else if (scope->kind != QA_THINK_WORLD_FRAME)
        return fail(error, QA_ERROR_ARGUMENT, "Think has no source execution domain");
    think_invocation think = {callback, callback_context, actor, scope};
    return qa_session_invoke(session, actor, QA_INVOKE_THINK, invoke_think, &think, error);
}

bool qa_session_create(const qa_session_options *options, qa_session **out, qa_error *error)
{
    if (options == NULL || out == NULL || options->actor_capacity == 0 || options->component_capacity == 0)
        return fail(error, QA_ERROR_ARGUMENT, "Session needs actor and component capacity");
    qa_session *session = calloc(1, sizeof(*session));
    if (session == NULL) return fail(error, QA_ERROR_MEMORY, "Cannot allocate session");
    session->options = *options;
    session->components = calloc(options->component_capacity, sizeof(*session->components));
    session->active_frames = calloc(options->component_capacity, sizeof(*session->active_frames));
    session->executions = calloc(options->actor_capacity, sizeof(*session->executions));
    session->turns = calloc(options->actor_capacity, sizeof(*session->turns));
    session->turn_positions = calloc(options->actor_capacity, sizeof(*session->turn_positions));
    if (session->components == NULL || session->active_frames == NULL
        || session->executions == NULL || session->turns == NULL
        || session->turn_positions == NULL) {
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
    free(session->components); free(session->active_frames); free(session->executions);
    free(session->turns); free(session->turn_positions); free(session);
    return false;
}

bool qa_session_create_restored(const qa_session_options *options, const qa_actor_checkpoint *actors,
                                 qa_strings *owned_strings, qa_session **out, qa_error *error)
{
    if (!options || !actors || !owned_strings || !out || actors->capacity != options->actor_capacity ||
        actors->count > actors->capacity || (actors->count && !actors->slots))
        return fail(error, QA_ERROR_ARGUMENT, "Restored session requires matching actors/options/strings");
    for (uint32_t i = 0; i < actors->count; ++i)
        if (actors->slots[i].active &&
            (!qa_strings_text(owned_strings, actors->slots[i].owner).data ||
             !qa_strings_text(owned_strings, actors->slots[i].definition).data))
            return fail(error, QA_ERROR_FORMAT, "Saved actor owner/definition is outside the saved string table");
    qa_session *session = NULL;
    if (!qa_session_create(options, &session, error)) return false;
    qa_actor_registry *restored = NULL;
    /* Until publication this registry has no release observer, so allocation
     * failure cleanup cannot dispatch through half-constructed application data. */
    if (!qa_actors_restore(actors, NULL, NULL, &restored, error)) {
        (void)qa_session_destroy(session, NULL); return false;
    }
    qa_scheduler *scheduler = NULL;
    if (!qa_scheduler_create(restored, options->component_capacity, options->mixed_order,
                              dispatch_think, session, &scheduler, error)) {
        (void)qa_actors_destroy(restored, NULL);
        (void)qa_session_destroy(session, NULL); return false;
    }
    (void)qa_scheduler_destroy(session->scheduler, NULL);
    (void)qa_actors_destroy(session->actors, NULL);
    qa_strings_destroy(session->strings);
    session->actors = restored;
    session->scheduler = scheduler;
    session->strings = owned_strings;
    session->restored_world_pending = true;
    qa_actors_set_release_observer(restored, actor_released, session);
    *out = session;
    return true;
}

void qa_session_checkpoint_free(qa_session_checkpoint *value)
{
    if (!value) return;
    free(value->components); free(value->executions);
    qa_scheduler_checkpoint_free(&value->scheduler);
    *value = (qa_session_checkpoint){0};
}

bool qa_session_checkpoint_capture(qa_session *session, qa_session_checkpoint *out, qa_error *error)
{
    if (!qa_session_safe(session) || !out || session->admissions || session->faulted ||
        qa_scheduler_has_admissions(session->scheduler))
        return fail(error, QA_ERROR_ARGUMENT, "Session capture requires a healthy safe point without admissions");
    if (!reconcile_turns(session, error)) return false;
    qa_session_checkpoint value = {.elapsed_ns = session->elapsed_ns, .next_order = session->next_order,
        .actor_capacity = session->options.actor_capacity, .component_capacity = session->options.component_capacity,
        .mixed_order = session->options.mixed_order, .execution_count = qa_actors_count(session->actors)};
    for (uint32_t i = 0; i < session->options.component_capacity; ++i)
        if (session->components[i].active) ++value.component_count;
    value.components = value.component_count ? calloc(value.component_count, sizeof(*value.components)) : NULL;
    value.executions = value.execution_count ? calloc(value.execution_count, sizeof(*value.executions)) : NULL;
    if ((value.component_count && !value.components) || (value.execution_count && !value.executions)) {
        qa_session_checkpoint_free(&value);
        return fail(error, QA_ERROR_MEMORY, "Allocating session checkpoint");
    }
    size_t index = 0;
    for (uint32_t i = 0; i < session->options.component_capacity; ++i) {
        const component_state *entry = session->components + i;
        if (entry->active) value.components[index++] = (qa_session_component_checkpoint){
            entry->component.owner, entry->component.clock, entry->clock, entry->order};
    }
    uint32_t cursor = 0;
    const qa_actor_record *actor;
    index = 0;
    while (qa_actors_next(session->actors, &cursor, &actor)) {
        qa_session_execution_checkpoint *execution = value.executions + index++;
        if (!qa_actors_save_reference(session->actors, actor->id, &execution->actor, error) ||
            !qa_session_execution(session, actor->id, &execution->provider)) {
            qa_session_checkpoint_free(&value); return false;
        }
    }
    if (!qa_scheduler_checkpoint_capture(session->scheduler, &value.scheduler, error)) {
        qa_session_checkpoint_free(&value); return false;
    }
    *out = value;
    return true;
}

static bool checkpoint_clock_equal(qa_clock_config a, qa_clock_config b)
{
    return a.kind == b.kind && a.initial_time_ns == b.initial_time_ns && a.interval_ns == b.interval_ns &&
        a.minimum_frame_ns == b.minimum_frame_ns && a.maximum_frame_ns == b.maximum_frame_ns &&
        a.initial_lead_ns == b.initial_lead_ns && a.maximum_steps == b.maximum_steps;
}

bool qa_session_checkpoint_restore(qa_session *session, const qa_session_checkpoint *value,
                                    qa_think_resolve_fn resolve, void *context, qa_error *error)
{
    if (!qa_session_safe(session) || session->admissions || session->faulted || !value || !resolve ||
        value->actor_capacity != session->options.actor_capacity ||
        value->component_capacity != session->options.component_capacity ||
        value->mixed_order != session->options.mixed_order ||
        value->component_count > session->options.component_capacity ||
        value->execution_count != qa_actors_count(session->actors) ||
        (value->component_count && !value->components) || (value->execution_count && !value->executions) ||
        value->scheduler.next_order != value->next_order || value->scheduler.provider_count != value->component_count ||
        (value->scheduler.provider_count && !value->scheduler.providers) ||
        (value->scheduler.think_count && !value->scheduler.thinks) ||
        value->scheduler.think_count > value->execution_count || value->scheduler.mixed_order != value->mixed_order)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid isolated session checkpoint");
    size_t active = 0;
    for (uint32_t i = 0; i < session->options.component_capacity; ++i) if (session->components[i].active) ++active;
    if (active != value->component_count) return fail(error, QA_ERROR_FORMAT, "Saved component set differs from candidate");
    for (size_t i = 0; i < value->component_count; ++i) {
        const qa_session_component_checkpoint *saved = value->components + i;
        component_state *entry = component(session, saved->owner);
        if (!entry || !checkpoint_clock_equal(entry->component.clock, saved->config) ||
            saved->order >= value->next_order || saved->state.host_origin_ns > value->elapsed_ns)
            return fail(error, QA_ERROR_FORMAT, "Saved component order or clock configuration differs from candidate");
        bool scheduler_found = false;
        for (size_t j = 0; j < value->scheduler.provider_count; ++j)
            if (value->scheduler.providers[j].owner == saved->owner &&
                value->scheduler.providers[j].order == saved->order &&
                value->scheduler.providers[j].kind == saved->config.kind) scheduler_found = true;
        if (!scheduler_found) return fail(error, QA_ERROR_FORMAT, "Saved scheduler disagrees with component ordering");
        for (size_t j = 0; j < i; ++j)
            if (value->components[j].owner == saved->owner || value->components[j].order == saved->order)
                return fail(error, QA_ERROR_FORMAT, "Duplicate saved component ordering");
        if (!qa_session_restore_clock(session, saved->owner, &saved->state, error)) return false;
        entry->order = saved->order;
    }
    uint8_t *seen = calloc(session->options.actor_capacity, 1);
    if (!seen) return fail(error, QA_ERROR_MEMORY, "Allocating restored execution validation");
    bool ok = true;
    for (size_t i = 0; ok && i < value->execution_count; ++i) {
        const qa_session_execution_checkpoint *saved = value->executions + i;
        const qa_actor_record *actor = qa_actors_resolve_saved(session->actors, saved->actor);
        bool observer = actor && !component(session, actor->owner) && observer_actor(session, actor);
        if (!actor || seen[actor->id.slot] || (saved->provider ? observer ||
            !component(session, saved->provider) : !observer)) {
            ok = fail(error, QA_ERROR_FORMAT, "Invalid or duplicate saved actor execution"); break;
        }
        seen[actor->id.slot] = 1;
        session->executions[actor->id.slot] = (actor_execution){actor->id, saved->provider};
    }
    free(seen);
    if (!ok || !qa_scheduler_checkpoint_restore(session->scheduler, &value->scheduler, resolve, context, error)) return false;
    for (size_t i = 0; i < value->scheduler.think_count; ++i) {
        const qa_scheduler_think_checkpoint *saved = value->scheduler.thinks + i;
        const qa_actor_record *actor = qa_actors_resolve_saved(session->actors, saved->actor);
        qa_actor_owner owner;
        if (!actor || !qa_session_execution(session, actor->id, &owner) || owner != saved->execution_provider)
            return fail(error, QA_ERROR_FORMAT, "Restored scheduled work disagrees with actor execution");
    }
    session->elapsed_ns = value->elapsed_ns;
    session->next_order = value->next_order;
    session->turn_count = 0;
    for (uint32_t i = 0; i < session->options.actor_capacity; ++i) session->turn_positions[i] = NO_TURN;
    session->turn_revision = qa_actors_revision(session->actors) - 1u;
    return reconcile_turns(session, error);
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
    qa_component_admission *token;
    if (!qa_session_prepare_component(session, value, 0, &token, error)) return false;
    if (qa_component_admission_commit(token, error)) return true;
    qa_component_admission_abort(token);
    return false;
}

bool qa_session_prepare_component(qa_session *session, const qa_component *value, qa_actor_owner retiring_owner,
                                   qa_component_admission **out, qa_error *error)
{
    if (!qa_session_safe(session) || value == NULL || out == NULL || !valid_clock(&value->clock))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid component or unsafe registration");
    if (qa_strings_text(session->strings, value->owner).data == NULL)
        return fail(error, QA_ERROR_ARGUMENT, "Component owner is not interned in this session");
    if (component(session, value->owner) != NULL && value->owner != retiring_owner)
        return fail(error, QA_ERROR_ARGUMENT, "Component owner already registered");
    component_state *slot = retiring_owner ? component(session, retiring_owner) : NULL;
    if (retiring_owner && (slot == NULL || slot->reservation != NULL))
        return fail(error, QA_ERROR_ARGUMENT, "Retiring component is absent or reserved");
    if (UINT64_MAX - session->next_order <= session->admissions)
        return fail(error, QA_ERROR_MEMORY, "Component order exhausted");
    for (uint32_t i = 0; i < session->options.component_capacity; ++i) {
        component_state *entry = &session->components[i];
        if (entry->reservation != NULL && entry->reservation->component.owner == value->owner)
            return fail(error, QA_ERROR_ARGUMENT, "Component owner already reserved");
        if (slot == NULL && !entry->active && entry->reservation == NULL) slot = entry;
    }
    if (slot == NULL) return fail(error, QA_ERROR_MEMORY, "Component capacity exhausted");
    qa_component_admission *token = malloc(sizeof(*token));
    if (token == NULL) return fail(error, QA_ERROR_MEMORY, "Cannot allocate component admission");
    *token = (qa_component_admission){.session = session, .slot = slot, .component = *value};
    if (!qa_scheduler_prepare(session->scheduler, value->owner, value->clock.kind, retiring_owner, &token->scheduler, error)) {
        free(token);
        return false;
    }
    slot->reservation = token;
    ++session->admissions;
    *out = token;
    return true;
}

bool qa_component_admission_validate(qa_component_admission *token, qa_error *error)
{
    if (token == NULL || !qa_session_safe(token->session) || token->slot->reservation != token
        || token->slot->active || component(token->session, token->component.owner) != NULL)
        return fail(error, QA_ERROR_ARGUMENT, "Component admission requires an idle unregistered owner");
    return qa_scheduler_admission_validate(token->scheduler, error);
}

bool qa_component_admission_commit(qa_component_admission *token, qa_error *error)
{
    if (!qa_component_admission_validate(token, error)) return false;
    if (!qa_scheduler_admission_commit(token->scheduler, error)) return false;
    qa_session *session = token->session;
    *token->slot = (component_state){.component = token->component,
        .clock = initial_clock(session, &token->component), .order = session->next_order++, .active = true};
    --session->admissions;
    free(token);
    return true;
}

void qa_component_admission_abort(qa_component_admission *token)
{
    if (token == NULL) return;
    qa_scheduler_admission_abort(token->scheduler);
    token->slot->reservation = NULL;
    --token->session->admissions;
    free(token);
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
    *entry = (component_state){.reservation = entry->reservation};
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

bool qa_session_mixed_order(const qa_session *session)
{ return session && session->options.mixed_order; }
bool qa_session_component_recipe(const qa_session *session, qa_actor_owner owner,
    qa_clock_config *clock, uint64_t *order)
{
    if (!session || !clock || !order) return false;
    const component_state *entry = component(session, owner);
    if (!entry) return false;
    *clock = entry->component.clock; *order = entry->order; return true;
}
bool qa_session_clock(const qa_session *session, qa_actor_owner owner, qa_clock_state *out)
{
    if (session == NULL || out == NULL) return false;
    component_state *entry = component(session, owner);
    if (entry == NULL) return false;
    *out = entry->clock;
    return true;
}

bool qa_session_active_frame(const qa_session *session, qa_actor_owner owner, qa_source_frame *out)
{
    if (session == NULL || out == NULL || !session->stepping) return false;
    component_state *entry = component(session, owner);
    if (entry == NULL || !entry->in_frame) return false;
    *out = entry->clock.frame;
    return true;
}

bool qa_session_frame_host_time(const qa_session *session, uint64_t *out)
{
    if (session == NULL || out == NULL || !session->stepping) return false;
    *out = session->frame_host_ns;
    return true;
}

bool qa_session_advance_interval(const qa_session *session, uint64_t *out)
{
    if (!session || !out || !session->stepping || !session->advancing) return false;
    *out = session->advance_elapsed_ns;
    return true;
}

bool qa_session_frame_pending(const qa_session *session, qa_actor_owner owner)
{
    if (!session || !session->stepping || !session->advancing) return false;
    component_state *entry = component(session, owner); uint64_t duration, deadline;
    bool available = false;
    return entry && next_frame(entry, &duration, &deadline, &available, NULL) && available;
}

bool qa_session_pending_frame(const qa_session *session, qa_actor_owner owner, uint64_t host_ns,
    bool *accepted, uint64_t *duration_ns, uint64_t *source_host_ns, qa_error *error)
{
    component_state *entry = session ? component(session, owner) : NULL;
    if (!entry || !accepted || !duration_ns || session->elapsed_ns > UINT64_MAX - host_ns)
        return fail(error, QA_ERROR_ARGUMENT, "Pending frame requires its actual Source clock and host interval");
    component_state projected;
    if (!pending_clock(entry, host_ns, session->elapsed_ns + host_ns, &projected, error)) return false;
    uint64_t deadline;
    *duration_ns = 0;
    if (!next_frame(&projected, duration_ns, &deadline, accepted, error)) return false;
    if (source_host_ns) {
        if (qw_server_clock(entry)) *source_host_ns = entry->clock.paused ? 0 : host_ns;
        else if (!entry->component.clock.interval_ns) *source_host_ns = *accepted ? *duration_ns : 0;
        else {
            uint64_t before = entry->clock.debt_ns, after = projected.clock.debt_ns;
            if (entry->component.clock.kind == QA_CLOCK_Q2_RERELEASE) {
                before -= before % UINT64_C(1000000);
                after -= after % UINT64_C(1000000);
            }
            *source_host_ns = after >= before ? after - before : 0;
        }
    }
    return true;
}

bool qa_session_active_command(const qa_session *session, qa_actor_owner owner, qa_source_command *out)
{
    if (session == NULL || out == NULL || !session->stepping) return false;
    component_state *entry = component(session, owner);
    if (entry == NULL || !entry->in_command) return false;
    *out = entry->command;
    return true;
}

typedef struct command_invocation {
    component_state *entry;
    qa_component_command_fn callback;
    void *state;
} command_invocation;

static bool invoke_command(void *opaque, qa_session *session, qa_error *error)
{
    command_invocation *call = opaque;
    return call->callback(call->state, session, &call->entry->command, error);
}

bool qa_session_command_call(qa_session *session, qa_actor_owner owner, qa_actor_id actor,
                              uint64_t elapsed_ns, qa_component_command_fn callback,
                              void *state, qa_error *error)
{
    if (session == NULL || callback == NULL || session->faulted || session->transitioning ||
        session->notification_depth || session->admissions || qa_scheduler_has_admissions(session->scheduler))
        return fail(error, QA_ERROR_ARGUMENT, "Source command needs a healthy admitted owner");
    component_state *entry = component(session, owner);
    qa_actor_owner execution;
    if (entry == NULL || entry->retiring || entry->clock.paused ||
        !qa_session_execution(session, actor, &execution))
        return fail(error, QA_ERROR_ARGUMENT, "Source command owner or actor is not current");
    bool source_client = entry->component.command_actor &&
        entry->component.command_actor(entry->component.state, session, actor);
    if ((!source_client && execution != owner) || (session->invocation && !source_client))
        return fail(error, QA_ERROR_ARGUMENT, "Source command has no actual actor admission");
    for (uint32_t i = 0; i < session->options.component_capacity; ++i)
        if (session->components[i].in_command)
            return fail(error, QA_ERROR_ARGUMENT, "Source command scopes cannot overlap");
    uint64_t time = entry->in_frame ? entry->clock.frame.time_ns :
        entry->component.clock.initial_time_ns + entry->clock.elapsed_ns;
    uint64_t debt = !entry->in_frame && qw_server_clock(entry) ? entry->clock.debt_ns : 0;
    if ((!entry->in_frame && entry->component.clock.initial_time_ns > UINT64_MAX - entry->clock.elapsed_ns) ||
        time > UINT64_MAX - debt || time + debt > UINT64_MAX - elapsed_ns)
        return fail(error, QA_ERROR_ARGUMENT, "Source command interval exhausted");
    time += debt;
    bool stepping = session->stepping;
    uint64_t previous_host = session->frame_host_ns;
    if (!stepping) session->frame_host_ns = session->elapsed_ns;
    entry->command = (qa_source_command){actor, owner, entry->component.clock.kind, QA_CLIENT_COMMAND,
        entry->clock.frame_number - (entry->in_frame ? 1u : 0u), time, elapsed_ns,
        session->advancing ? session->advance_elapsed_ns : elapsed_ns};
    entry->in_command = true; session->stepping = true;
    command_invocation call = {entry, callback, state};
    bool ok = qa_session_invoke(session, actor, QA_INVOKE_PHYSICS, invoke_command, &call, error);
    entry->in_command = false; entry->command = (qa_source_command){0};
    session->stepping = stepping; session->frame_host_ns = previous_host;
    if (session->faulted && error) *error = session->error;
    return ok && !session->faulted;
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
        || state->frame.time_ns != state->frame.start_ns + (qw_server_clock(entry) ? 0 : state->frame.elapsed_ns)
        || state->frame.time_ns != entry->component.clock.initial_time_ns + state->elapsed_ns
        || (entry->component.clock.interval_ns && state->elapsed_ns > UINT64_MAX - state->debt_ns)
        || (qw_server_clock(entry) && (state->elapsed_ns > UINT64_MAX - state->debt_ns ||
            entry->component.clock.initial_time_ns > UINT64_MAX - state->elapsed_ns - state->debt_ns))
        || (!entry->component.clock.interval_ns && state->host_origin_ns > UINT64_MAX - state->debt_ns))
        return fail(error, QA_ERROR_FORMAT, "Invalid provider clock checkpoint");
    entry->clock = *state;
    return true;
}

bool qa_session_actor_allocation_ready(const qa_session *session, qa_actor_owner owner)
{
    if (!session || session->transitioning || session->faulted) return false;
    const component_state *entry = component((qa_session *)session, owner);
    return entry && !entry->retiring;
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
    const qa_actor_record *record = qa_actors_get(session->actors, actor);
    if (!component(session, record->owner) && observer_actor(session, record))
        return fail(error, QA_ERROR_ARGUMENT, "Decoded CLIENT observer has no simulation execution");
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
    if (qa_actor_id_equal(execution.actor, actor)) {
        if (!execution.provider && (component(session, record->owner) || !observer_actor(session, record)))
            return false;
        *out = execution.provider;
    } else if (component(session, record->owner)) *out = record->owner;
    else {
        if (!observer_actor(session, record)) return false;
        *out = 0;
    }
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

static component_state *ordered_component(qa_session *, uint64_t, bool);

typedef struct source_actor_invocation {
    qa_actor_id actor;
    qa_source_frame frame;
} source_actor_invocation;
typedef struct controlled_actor_invocation {
    qa_actor_id actor;
    const qa_source_frame *frame;
    bool handled;
} controlled_actor_invocation;

static bool invoke_controlled_actor(void *context, qa_session *session, qa_error *error)
{
    controlled_actor_invocation *invocation = context;
    return session->options.controlled_actor(session->options.release_context, session,
        invocation->actor, invocation->frame, &invocation->handled, error);
}

static bool invoke_source_actor(void *context, qa_session *session, qa_error *error)
{
    source_actor_invocation *invocation = context;
    return session->options.source_actor(session->options.release_context, session,
                                         invocation->actor, &invocation->frame, error);
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
        if (session->options.source_actor != NULL) {
            component_state *source = ordered_component(session, 0, true);
            while (source != NULL) {
                if (source->in_frame) {
                    source->clock.frame.phase = QA_ENTITY_PHYSICS;
                    source_actor_invocation invocation = {turn.actor, source->clock.frame};
                    if (!qa_session_invoke(session, turn.actor, QA_INVOKE_PHYSICS,
                                            invoke_source_actor, &invocation, error)) return false;
                    if (qa_actors_get(session->actors, turn.actor) == NULL) break;
                }
                source = ordered_component(session, source->order, false);
            }
            actor = qa_actors_get(session->actors, turn.actor);
            if (actor == NULL) continue;
        }
        actor_execution execution = session->executions[actor->id.slot];
        component_state *provider_state = component(session,
            qa_actor_id_equal(execution.actor, actor->id) ? execution.provider : actor->owner);
        if (provider_state == NULL || !provider_state->in_frame) continue;
        if (qw_server_clock(provider_state) && provider_state->component.command_actor &&
            provider_state->component.command_actor(provider_state->component.state, session, actor->id)) continue;
        provider_state->clock.frame.phase = QA_ENTITY_PHYSICS;
        controlled_actor_invocation controlled = {turn.actor, &provider_state->clock.frame, false};
        if (session->options.controlled_actor != NULL &&
            !qa_session_invoke(session, turn.actor, QA_INVOKE_PHYSICS,
                invoke_controlled_actor, &controlled, error)) return false;
        if (qa_actors_get(session->actors, turn.actor) == NULL) continue;
        if (controlled.handled) {
            if (session->options.after_actor != NULL &&
                !session->options.after_actor(session->options.release_context, session, turn.actor,
                    &provider_state->clock.frame, error)) return false;
            continue;
        }
        if (provider_state->component.actor_frame == NULL) continue;
        physics_invocation invocation = {provider_state, actor->id};
        if (!qa_session_invoke(session, actor->id, QA_INVOKE_PHYSICS, invoke_physics, &invocation, error)) return false;
        if (session->options.after_actor != NULL
            && !session->options.after_actor(session->options.release_context, session, turn.actor,
                                             &provider_state->clock.frame, error)) return false;
    }
}

static bool pending_clock(const component_state *entry, uint64_t host_ns, uint64_t host_boundary,
    component_state *projected, qa_error *error)
{
    *projected = *entry;
    if (entry->clock.paused) {
        if (entry->clock.host_origin_ns > UINT64_MAX - host_ns)
            return fail(error, QA_ERROR_ARGUMENT, "Paused provider clock exhausted");
        projected->clock.host_origin_ns += host_ns;
        return true;
    }
    uint64_t frame = 0;
    if (entry->component.clock_admit) {
        uint64_t source_host_ns = entry->component.clock.interval_ns ?
            qa_source_frame_time_host_delta(host_boundary, host_ns) : host_ns;
        if (!entry->component.clock_admit(entry->component.clock_context, source_host_ns, entry->clock.debt_ns,
            &projected->clock.debt_ns, &frame, error)) return false;
        if (entry->component.clock.interval_ns) projected->clock.host_origin_ns = host_boundary;
    } else {
        if (entry->clock.debt_ns > UINT64_MAX - host_ns)
            return fail(error, QA_ERROR_ARGUMENT, "Provider clock exhausted");
        projected->clock.debt_ns += host_ns;
    }
    if (entry->clock.elapsed_ns > UINT64_MAX - frame ||
        entry->component.clock.initial_time_ns > UINT64_MAX - entry->clock.elapsed_ns - frame ||
        (qw_server_clock(entry) && (entry->clock.elapsed_ns > UINT64_MAX - projected->clock.debt_ns ||
         entry->component.clock.initial_time_ns > UINT64_MAX - entry->clock.elapsed_ns - projected->clock.debt_ns)) ||
        (entry->component.clock.interval_ns && (entry->clock.elapsed_ns > UINT64_MAX - projected->clock.debt_ns ||
         entry->component.clock.initial_time_ns > UINT64_MAX - entry->clock.elapsed_ns - projected->clock.debt_ns)))
        return fail(error, QA_ERROR_ARGUMENT, "Provider Source clock exhausted");
    return true;
}

/* A provider's lead changes host eligibility, never source callback time. */
static bool next_frame(const component_state *entry, uint64_t *duration, uint64_t *deadline,
    bool *available, qa_error *error)
{
    const qa_clock_config *config = &entry->component.clock;
    *available = false;
    if (!entry->active || entry->retiring || entry->clock.paused) return true;
    uint64_t step = config->interval_ns;
    if (step == 0) {
        if (entry->component.clock_admit) {
            uint64_t pending;
            if (!entry->component.clock_admit(entry->component.clock_context, 0, entry->clock.debt_ns,
                &pending, &step, error)) return false;
            if (!step) return true;
        } else {
            if (entry->clock.debt_ns < config->minimum_frame_ns) return true;
            step = entry->clock.debt_ns < config->maximum_frame_ns ? entry->clock.debt_ns : config->maximum_frame_ns;
        }
        if (entry->clock.host_origin_ns > UINT64_MAX - entry->clock.debt_ns)
            return fail(error, QA_ERROR_ARGUMENT, "Provider host deadline exhausted");
        uint64_t elapsed = qw_server_clock(entry) ? entry->clock.debt_ns : step;
        if (entry->clock.elapsed_ns > UINT64_MAX - elapsed ||
            config->initial_time_ns > UINT64_MAX - entry->clock.elapsed_ns - elapsed ||
            (qw_server_clock(entry) && config->initial_time_ns + entry->clock.elapsed_ns + elapsed > UINT64_MAX - step))
            return fail(error, QA_ERROR_ARGUMENT, "Provider Source clock exhausted");
        *duration = step;
        *deadline = entry->clock.host_origin_ns + entry->clock.debt_ns;
        *available = true;
        return true;
    }
    if (entry->clock.debt_ns < step) return true;
    uint64_t elapsed = entry->clock.elapsed_ns + step;
    uint64_t relative = elapsed >= config->initial_lead_ns ? elapsed - config->initial_lead_ns : 0;
    *duration = step;
    *deadline = entry->component.clock_admit ? entry->clock.host_origin_ns : entry->clock.host_origin_ns + relative;
    *available = true;
    return true;
}

static bool q2_clock(const component_state *entry)
{
    return entry->component.clock.kind == QA_CLOCK_Q2_CLASSIC ||
        entry->component.clock.kind == QA_CLOCK_Q2_RERELEASE;
}

static bool frame_limit(const component_state *entry)
{
    if (q2_clock(entry)) return entry->steps != 0;
    return entry->component.clock.maximum_steps &&
        entry->steps >= entry->component.clock.maximum_steps;
}

static void consume_frame(component_state *entry, uint64_t step)
{
    entry->clock.elapsed_ns += qw_server_clock(entry) ? entry->clock.debt_ns : step;
    if (!entry->component.clock.interval_ns) {
        entry->clock.host_origin_ns += entry->clock.debt_ns;
        entry->clock.debt_ns = 0;
    } else {
        entry->clock.debt_ns -= step;
        uint64_t retained = entry->clock.debt_ns;
        if (entry->component.clock.kind == QA_CLOCK_Q2_CLASSIC &&
            retained > entry->component.clock.initial_lead_ns)
            retained = entry->component.clock.initial_lead_ns;
        else if (entry->component.clock.kind == QA_CLOCK_Q2_RERELEASE &&
            retained > UINT64_C(250000000))
            retained = UINT64_C(100000000) + retained % UINT64_C(1000000);
        if (!entry->component.clock_admit) entry->clock.host_origin_ns += entry->clock.debt_ns - retained;
        entry->clock.debt_ns = retained;
    }
    ++entry->steps;
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

static size_t collect_active_frames(qa_session *session)
{
    size_t count = 0;
    component_state *entry = ordered_component(session, 0, true);
    while (entry != NULL) {
        if (entry->in_frame)
            session->active_frames[count++] = entry->clock.frame;
        entry = ordered_component(session, entry->order, false);
    }
    return count;
}

static bool command_boundary(qa_session *session, qa_error *error)
{
    session->frame_host_ns = session->elapsed_ns;
    qa_session_frames_fn callbacks[] = {session->options.prepare_commands,
        session->options.run_commands, session->options.end_commands};
    bool ok = true;
    for (size_t i = 0; ok && !session->faulted && i < sizeof(callbacks) / sizeof(callbacks[0]); ++i)
        if (callbacks[i]) ok = callbacks[i](session->options.release_context, session,
            NULL, 0, session->frame_host_ns, error);
    return ok && !session->faulted;
}

bool qa_session_command_turn(qa_session *session, qa_error *error)
{
    if (!qa_session_safe(session) || session->faulted)
        return fail(error, QA_ERROR_ARGUMENT, "Command turn requires an idle, healthy session");
    session->stepping = true;
    bool ok = command_boundary(session, error);
    session->stepping = false;
    if (!ok) fault(session, error);
    if (session->faulted && error != NULL) *error = session->error;
    return ok;
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
        component_state projected;
        if (!pending_clock(entry, elapsed_ns, session->elapsed_ns + elapsed_ns, &projected, error)) return false;
        if (entry->clock.paused || entry->component.clock.interval_ns == 0 || entry->component.clock_admit) continue;
        uint64_t admitted = projected.clock.elapsed_ns + projected.clock.debt_ns;
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
        component_state projected;
        if (!pending_clock(entry, elapsed_ns, session->elapsed_ns, &projected, error)) return false;
        entry->clock = projected.clock;
    }
    session->advance_elapsed_ns = elapsed_ns;
    session->advancing = true;
    session->stepping = true;
    bool ok = true, completed_boundary = false;
    for (;;) {
        bool found = false, limited = false;
        uint64_t deadline = 0;
        for (uint32_t i = 0; i < session->options.component_capacity; ++i) {
            uint64_t step, due;
            component_state *candidate = &session->components[i];
            bool available;
            if (!next_frame(candidate, &step, &due, &available, error)) { ok = false; break; }
            if (!available || (q2_clock(candidate) && frame_limit(candidate))) continue;
            bool at_limit = frame_limit(candidate);
            if (!found || due < deadline) { found = true; deadline = due; limited = at_limit; }
            else if (due == deadline && at_limit) limited = true;
        }
        /* No provider may pass an earlier, deferred boundary in the same world. */
        if (!ok || !found || limited) break;
        for (uint32_t i = 0; i < session->options.component_capacity; ++i) {
            component_state *entry = &session->components[i];
            uint64_t step, due;
            bool available;
            if (!next_frame(entry, &step, &due, &available, error)) { ok = false; break; }
            available = available && !frame_limit(entry);
            entry->in_frame = available && due == deadline;
            if (!entry->in_frame) continue;
            if (entry->clock.frame_number == UINT64_MAX) { ok = fail(error, QA_ERROR_ARGUMENT, "Provider frame counter exhausted"); break; }
            uint64_t start = entry->component.clock.initial_time_ns + entry->clock.elapsed_ns;
            if (qw_server_clock(entry)) start += entry->clock.debt_ns;
            entry->clock.frame = (qa_source_frame){entry->component.owner, entry->component.clock.kind,
                                                  QA_FRAME_ENTRY, ++entry->clock.frame_number, start, step, start};
            if (entry->component.clock.kind != QA_CLOCK_NETQUAKE && entry->component.clock.kind != QA_CLOCK_QUAKEWORLD)
                entry->clock.frame.time_ns += step;
            consume_frame(entry, step);
        }
        if (!ok) break;
        completed_boundary = true;
        uint64_t host_boundary = deadline;
        session->frame_host_ns = host_boundary;
        component_state *entry = ordered_component(session, 0, true);
        while (entry != NULL) {
            if (entry->in_frame && entry->component.prepare_frame != NULL &&
                !entry->component.prepare_frame(entry->component.state, session, &entry->clock.frame, error)) { ok = false; break; }
            if (session->faulted) { ok = false; break; }
            entry = ordered_component(session, entry->order, false);
        }
        size_t frame_count = collect_active_frames(session);
        if (ok && !session->faulted && session->options.prepare_commands != NULL)
            ok = session->options.prepare_commands(session->options.release_context, session,
                session->active_frames, frame_count, host_boundary, error);
        if (!ok || session->faulted) { ok = false; break; }
        entry = ordered_component(session, 0, true);
        while (entry != NULL) {
            if (entry->in_frame && entry->component.begin_frame != NULL
                && !entry->component.begin_frame(entry->component.state, session, &entry->clock.frame, error)) { ok = false; break; }
            if (session->faulted) { ok = false; break; }
            entry = ordered_component(session, entry->order, false);
        }
        if (!ok || session->faulted) { ok = false; break; }
        frame_count = collect_active_frames(session);
        if (session->options.run_commands != NULL &&
            !session->options.run_commands(session->options.release_context, session,
                session->active_frames, frame_count, host_boundary, error)) { ok = false; break; }
        if (!qa_scheduler_advance(session->scheduler, session->active_frames, frame_count,
                                  QA_THINK_BEFORE_PHYSICS, error)
            || session->faulted) { ok = false; break; }
        if (!actor_frames(session, error) || session->faulted) { ok = false; break; }
        if (!qa_scheduler_advance(session->scheduler, session->active_frames, frame_count,
                                  QA_THINK_AFTER_PHYSICS, error)
            || session->faulted) { ok = false; break; }
        entry = ordered_component(session, 0, true);
        while (entry != NULL) {
            if (entry->in_frame) {
                entry->clock.frame.phase = QA_CLIENT_END_FRAME;
                if (entry->component.end_frame != NULL
                    && !entry->component.end_frame(entry->component.state, session, &entry->clock.frame, error)) { ok = false; break; }
            }
            if (session->faulted) { ok = false; break; }
            entry = ordered_component(session, entry->order, false);
        }
        if (!ok || session->faulted) { ok = false; break; }
        frame_count = collect_active_frames(session);
        if (session->options.end_commands != NULL &&
            !session->options.end_commands(session->options.release_context, session,
                session->active_frames, frame_count, host_boundary, error)) { ok = false; break; }
        if (session->faulted) { ok = false; break; }
        for (uint32_t i = 0; i < session->options.component_capacity; ++i) {
            entry = &session->components[i];
            if (!entry->in_frame) continue;
            entry->clock.frame.phase = QA_FRAME_EXIT;
            entry->clock.frame.time_ns = entry->clock.frame.start_ns +
                (qw_server_clock(entry) ? 0 : entry->clock.frame.elapsed_ns);
        }
        frame_count = collect_active_frames(session);
        if (session->options.frame_exit != NULL)
            ok = session->options.frame_exit(session->options.release_context, session,
                session->active_frames, frame_count, host_boundary, error);
        if (!ok || session->faulted) { ok = false; break; }
        for (uint32_t i = 0; i < session->options.component_capacity; ++i)
            session->components[i].in_frame = false;
    }
    if (ok && !session->faulted && !completed_boundary)
        ok = command_boundary(session, error);
    session->stepping = false;
    session->advancing = false;
    session->advance_elapsed_ns = 0;
    for (uint32_t i = 0; i < session->options.component_capacity; ++i) {
        session->components[i].in_frame = false;
    }
    if (!ok) fault(session, error);
    if (session->faulted && error != NULL) *error = session->error;
    return ok;
}

static bool retire_world(qa_session *session, qa_error *error)
{
    session->restored_world_pending = false;
    session->faulted = false;
    session->error = (qa_error){0};
    qa_error current = {0};
    if (!qa_actors_clear(session->actors, &current)) fault(session, &current);
    if (!qa_scheduler_clear(session->scheduler, &current)) fault(session, &current);
    if (session->faulted) {
        if (error != NULL) *error = session->error;
        return false;
    }
    for (uint32_t i = 0; i < session->options.component_capacity; ++i)
        if (session->components[i].active) session->components[i].clock = initial_clock(session, &session->components[i].component);
    return true;
}

bool qa_session_retire_world(qa_session *session, qa_error *error)
{
    if (!qa_session_safe(session)) return fail(error, QA_ERROR_ARGUMENT, "World retirement requires a safe point");
    session->transitioning = true;
    bool ok = retire_world(session, error);
    session->transitioning = false;
    return ok;
}

bool qa_session_retire_actors(qa_session *session, qa_error *error)
{
    if (!qa_session_safe(session) || session->faulted || session->admissions != 0 ||
        qa_scheduler_has_admissions(session->scheduler))
        return fail(error, QA_ERROR_ARGUMENT, "Retained actor retirement requires an idle healthy session");
    session->transitioning = true;
    qa_error current = {0};
    if (!qa_actors_clear(session->actors, &current)) fault(session, &current);
    if (!qa_scheduler_clear(session->scheduler, &current)) fault(session, &current);
    session->transitioning = false;
    if (session->faulted && error != NULL) *error = session->error;
    return !session->faulted;
}

bool qa_session_round_step(qa_session *session, qa_actor_owner owner, uint64_t elapsed_ns,
                            qa_component_frame_fn callback, void *context, qa_error *error)
{
    if (!qa_session_safe(session) || session->faulted || session->admissions != 0 ||
        qa_scheduler_has_admissions(session->scheduler) || elapsed_ns == 0)
        return fail(error, QA_ERROR_ARGUMENT, "Round frame requires an idle healthy session and interval");
    component_state *entry = component(session, owner);
    if (entry == NULL || entry->component.clock.kind != QA_CLOCK_Q3 || entry->clock.paused)
        return fail(error, QA_ERROR_ARGUMENT, "Round frame requires an active Q3 source");
    if (session->elapsed_ns > UINT64_MAX - elapsed_ns || entry->clock.frame_number == UINT64_MAX ||
        entry->clock.elapsed_ns > UINT64_MAX - elapsed_ns ||
        entry->component.clock.initial_time_ns > UINT64_MAX - entry->clock.elapsed_ns - elapsed_ns ||
        entry->clock.debt_ns > UINT64_MAX - entry->clock.elapsed_ns - elapsed_ns)
        return fail(error, QA_ERROR_ARGUMENT, "Round source clock exhausted");
    uint64_t admitted = entry->clock.elapsed_ns + entry->clock.debt_ns + elapsed_ns;
    uint64_t relative = admitted >= entry->component.clock.initial_lead_ns
        ? admitted - entry->component.clock.initial_lead_ns : 0;
    if (entry->component.clock.initial_time_ns > UINT64_MAX - admitted ||
        entry->clock.host_origin_ns > UINT64_MAX - relative)
        return fail(error, QA_ERROR_ARGUMENT, "Round source deadline exhausted");
    for (uint32_t i = 0; i < session->options.component_capacity; ++i) {
        component_state *other = &session->components[i];
        if (other->active && other != entry && other->clock.host_origin_ns > UINT64_MAX - elapsed_ns)
            return fail(error, QA_ERROR_ARGUMENT, "Round host clock exhausted");
    }
    uint64_t start = entry->component.clock.initial_time_ns + entry->clock.elapsed_ns;
    session->elapsed_ns += elapsed_ns;
    session->frame_host_ns = session->elapsed_ns;
    for (uint32_t i = 0; i < session->options.component_capacity; ++i) {
        component_state *other = &session->components[i];
        if (other->active && other != entry) other->clock.host_origin_ns += elapsed_ns;
    }
    /* Native Q3 components use the same end-of-interval source time as normal
     * admission. A supplied GAME callback preserves SV_MapRestart's entry time. */
    uint64_t source_time = callback ? start : start + elapsed_ns;
    entry->clock.frame = (qa_source_frame){owner, QA_CLOCK_Q3, QA_FRAME_ENTRY,
        ++entry->clock.frame_number, start, elapsed_ns, source_time};
    entry->clock.elapsed_ns += elapsed_ns;
    entry->in_frame = true;
    session->stepping = true;
    bool ok;
    if (callback != NULL) ok = callback(context, session, &entry->clock.frame, error);
    else {
        ok = entry->component.prepare_frame == NULL ||
            entry->component.prepare_frame(entry->component.state, session, &entry->clock.frame, error);
        if (ok && !session->faulted && entry->component.begin_frame != NULL)
            ok = entry->component.begin_frame(entry->component.state, session, &entry->clock.frame, error);
        size_t count = collect_active_frames(session);
        if (ok && !session->faulted)
            ok = qa_scheduler_advance(session->scheduler, session->active_frames, count, QA_THINK_BEFORE_PHYSICS, error);
        if (ok && !session->faulted) ok = actor_frames(session, error);
        if (ok && !session->faulted)
            ok = qa_scheduler_advance(session->scheduler, session->active_frames, count, QA_THINK_AFTER_PHYSICS, error);
        entry->clock.frame.phase = QA_CLIENT_END_FRAME;
        if (ok && !session->faulted && entry->component.end_frame != NULL)
            ok = entry->component.end_frame(entry->component.state, session, &entry->clock.frame, error);
    }
    entry->clock.frame.phase = QA_FRAME_EXIT;
    entry->clock.frame.time_ns = start + elapsed_ns;
    if (ok && !session->faulted && session->options.frame_exit != NULL) {
        size_t count = collect_active_frames(session);
        ok = session->options.frame_exit(session->options.release_context, session,
            session->active_frames, count, session->frame_host_ns, error);
    }
    entry->in_frame = false;
    session->stepping = false;
    if (!ok) fault(session, error);
    if (session->faulted && error != NULL) *error = session->error;
    return ok && !session->faulted;
}

bool qa_session_replace_world(qa_session *session, void *candidate, qa_cleanup_fn close, qa_error *error)
{
    if (!qa_session_safe(session) || (candidate != NULL && candidate == session->world))
        return fail(error, QA_ERROR_ARGUMENT, "World publication requires a distinct candidate and a safe point");
    session->transitioning = true;
    if (!retire_world(session, error)) {
        session->transitioning = false;
        return false;
    }
    void *old = session->world;
    qa_cleanup_fn old_close = session->close_world;
    session->world = candidate;
    session->close_world = close;
    if (old_close != NULL) old_close(old);
    session->transitioning = false;
    return true;
}

bool qa_session_adopt_restored_world(qa_session *session, void *candidate,
                                      qa_cleanup_fn close, qa_error *error)
{
    if (!qa_session_safe(session) || session->faulted ||
        !session->restored_world_pending || session->world != NULL ||
        session->admissions != 0 || qa_scheduler_has_admissions(session->scheduler) ||
        candidate == NULL || close == NULL)
        return fail(error, QA_ERROR_ARGUMENT,
                    "Restored world attachment requires an isolated fresh session");
    session->world = candidate;
    session->close_world = close;
    session->restored_world_pending = false;
    return true;
}

bool qa_session_destroy_ready(const qa_session *session)
{
    return session == NULL || (qa_session_safe(session) && session->admissions == 0
        && !qa_scheduler_has_admissions(session->scheduler));
}

bool qa_session_destroy(qa_session *session, qa_error *error)
{
    if (session == NULL) return true;
    if (!qa_session_destroy_ready(session)) {
        if (!qa_session_safe(session))
            return fail(error, QA_ERROR_ARGUMENT, "Session destroy requires a safe point");
        return fail(error, QA_ERROR_ARGUMENT, "Abort component and scheduler admissions before session destruction");
    }
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
    free(session->components); free(session->active_frames); free(session->executions);
    free(session->turns); free(session->turn_positions); free(session);
    return ok;
}
