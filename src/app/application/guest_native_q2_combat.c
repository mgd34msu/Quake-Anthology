#include "guest_native_q2_private.h"
#include "guest_native_q2_combat.h"
#include "guest_native_q2_combat_state.h"
#include "guest_native_q2_attack.h"
#include "guest_native_q2_combat_kex.h"
#include "native_q2_callbacks.h"
#include "qa/native_observe.h"
#include "qa/native_host_q2_wire.h"
#include "qa/source_save.h"
#include "control_frame.h"
#include "qa/pool.h"
#include "qa/id_index.h"
#include <math.h>

typedef struct combat_record {
    application_q2_combat_actor source;
    struct combat_record *next, *previous;
    struct application_native_q2_combat *owner;
    size_t storage_slot;
    unsigned calls;
    uint64_t serial;
    bool bound, fuel_bound, prepared, retired;
} combat_record;
typedef struct combat_reaction {
    struct combat_reaction *next;
    struct application_native_q2_combat *owner;
    qa_native_entry_observer *binding;
    qa_native_address address;
    application_q2_call_operation operation;
    size_t storage_slot;
} combat_reaction;
typedef struct combat_reactions {
    combat_reaction *first;
    qa_unified_frame_lease *storage;
    qa_pool pool;
    qa_id_index index;
    size_t chunk;
} combat_reactions;
typedef struct combat_incoming {
    struct combat_incoming *previous;
    qa_native_entry_observer *binding;
    const qa_native_value *arguments;
    size_t count;
    qa_actor_id target;
    combat_record *record;
} combat_incoming;
typedef struct combat_frame {
    struct combat_frame *previous, *retained_next;
    struct application_native_q2_combat *owner;
    qa_unified_frame_lease *storage;
    combat_record *record;
    const qa_damage_request *request;
    qa_damage_observer *observer;
    qa_damage_result result;
    qa_native_write_observer *health_watch, *velocity_watch, *armor_watch, *flags_watch;
    qa_native_write_observer *pain_watch, *die_watch;
    double health;
    qa_armor armor;
    qa_vec3 velocity;
    bool active, reacting, entering;
} combat_frame;
typedef struct application_native_q2_resource_scope {
    combat_frame frame;
    struct application_native_q2_resource_scope *previous;
    qa_pickup_execution *execution;
    qa_protection_channel channel;
    bool (*committed)(void *);
    void *committed_context;
} application_native_q2_resource_scope;
typedef struct combat_armor_hook {
    struct application_native_q2_combat *owner;
    qa_protection_channel channel;
    qa_native_entry_observer *binding;
} combat_armor_hook;
typedef struct combat_deferred {
    struct combat_deferred *previous;
    struct application_native_q2_combat *owner;
    combat_record *record;
    combat_frame *parent_current;
    qa_source_reaction_observer *observer;
    qa_source_reaction_body execute;
    void *context;
    const qa_damage_request *request;
    bool dispatching;
} combat_deferred;
struct application_native_q2_combat {
    struct application_native_q2 *engine;
    application_q2_combat_profile profile;
    combat_record *records;
    qa_actor_index record_index;
    qa_unified_frame_lease *record_storage;
    qa_pool record_pool;
    size_t record_chunk;
    combat_reactions reactions;
    qa_native_entry_observer *damage;
    combat_armor_hook armor[2];
    combat_frame *current, *retained;
    application_native_q2_resource_scope *pickups;
    combat_incoming *incoming;
    combat_deferred *deferred;
    struct application_q2_kex_damage *kex;
    bool active;
};
static qa_native_instance *native(struct application_native_q2_combat *p)
{ return qa_native_host_instance(p->engine->provider->state.native.host); }
static qa_combat *shared(struct application_native_q2_combat *p)
{ return p->engine->provider->application->combat; }
typedef struct native_shared_damage {
    qa_combat *combat;
    const qa_damage_request *request;
    qa_damage_outcome *outcome;
} native_shared_damage;
static bool shared_damage(void *context, qa_error *error)
{
    native_shared_damage *call = context;
    return qa_combat_apply(call->combat, call->request, call->outcome, error);
}
bool application_native_q2_combat_apply(struct application_native_q2 *engine,
    const qa_damage_request *request, qa_damage_outcome *outcome, qa_error *error)
{
    native_shared_damage call = {engine->provider->application->combat, request, outcome};
    return engine->callbacks ? application_native_q2_callbacks_transfer(engine->callbacks,
        shared_damage, &call, error) : shared_damage(&call, error);
}
static void reactions_destroy(combat_reactions *reactions)
{
    qa_id_index_destroy(&reactions->index);
    qa_unified_frame_lease_release(reactions->storage);
    *reactions = (combat_reactions){0};
}
static bool reactions_prepare(struct application_native_q2_combat *p,
    combat_reactions *reactions, size_t count, qa_error *error)
{
    if (p->record_chunk > SIZE_MAX / 2)
        return application_fail(error, QA_ERROR_MEMORY, "Native reaction capacity exceeds storage");
    reactions->chunk = p->record_chunk * 2;
    if (count < reactions->chunk) count = reactions->chunk;
    reactions->storage = application_control_storage_acquire(p->engine->provider->application, error);
    if (!reactions->storage) return false;
    qa_arena *arena = qa_unified_frame_lease_arena(reactions->storage);
    reactions->index.storage = arena;
    if (!qa_pool_prepare(&reactions->pool, arena, count,
        sizeof(combat_reaction), _Alignof(combat_reaction), error) ||
        !qa_id_index_reserve(&reactions->index, count, error)) {
        reactions_destroy(reactions); return false;
    }
    return true;
}
static combat_reaction *reaction_find(combat_reactions *reactions, qa_native_address address)
{
    qa_id_index_entry *entry = qa_id_index_find(&reactions->index, QA_STRING_NONE, address);
    return entry ? qa_pool_at(&reactions->pool, entry->value) : NULL;
}
static combat_reaction *reaction_take(struct application_native_q2_combat *p,
    combat_reactions *reactions, qa_error *error)
{
    if (!qa_id_index_reserve(&reactions->index, 1, error)) return NULL;
    if (reactions->pool.head == SIZE_MAX && !qa_pool_extend(&reactions->pool,
        qa_unified_frame_lease_arena(reactions->storage),
        reactions->chunk ? reactions->chunk : 1, error)) return NULL;
    size_t slot;
    combat_reaction *hook = qa_pool_take(&reactions->pool, &slot);
    if (!hook) {
        application_fail(error, QA_ERROR_MEMORY, "Retaining native reaction entry ownership"); return NULL;
    }
    *hook = (combat_reaction){.owner = p, .storage_slot = slot};
    return hook;
}
static combat_record *record_for(struct application_native_q2_combat *p, qa_actor_id actor)
{
    size_t value;
    return qa_actor_index_find(&p->record_index, actor, &value) ? (combat_record *)(uintptr_t)value : NULL;
}
bool application_native_q2_actor_traits(application_provider *provider, qa_actor_id actor,
    qa_builtin_actor_traits *out)
{
    struct application_native_q2 *engine = provider && provider->kind == APPLICATION_PROVIDER_NATIVE
        ? provider->state.native.q2_engine : NULL;
    if (!engine || !out || !engine->initialized || !engine->map_ready || engine->shutting_down ||
        engine->profile == QA_NATIVE_Q2_CGAME_API2023 || !provider->state.native.host) return false;
    const qa_actor_registry *actors = qa_session_actors(provider->application->session);
    const qa_actor_record *record = qa_actors_get(actors, actor);
    qa_native_host *host = provider->state.native.host;
    uint32_t slot;
    qa_native_host_q2_entity source;
    qa_error ignored = {0};
    if (!record || !qa_native_host_slot_for_actor(host, actor, &slot) ||
        !qa_native_host_q2_bot_entity(host, slot, &source, &ignored) ||
        !source.in_use || !qa_actor_id_equal(source.binding.actor, actor)) return false;
    const qa_actor_player *identity = qa_actors_player(actors, actor);
    bool player = source.bot.player || application_native_q2_client_slot(engine, actor) != 0;
    qa_builtin_actor_traits traits = {.classname = record->definition, .player = player,
        .monster = (source.server_flags & 4u) != 0,
        .spectator = identity && identity->spectator,
        .disguised = identity && identity->disguised};
    if (engine->profile == QA_NATIVE_Q2_GAME_API2023) {
        /* Public API2023 sv_ent_flags_t, distinct from private edict flags. */
        traits.no_target = (source.bot.flags & (UINT64_C(1) << 13)) != 0;
        traits.spectator |= (source.bot.flags & (UINT64_C(1) << 29)) != 0;
        traits.invisible = (source.bot.flags & (UINT64_C(1) << 3)) != 0;
        traits.damageable_target = (source.bot.flags & (UINT64_C(1) << 9)) != 0;
        if (source.bot.registered) {
            traits.dead = (player || traits.monster) && source.bot.health <= 0;
            traits.max_health = source.bot.max_health;
            traits.view_height = (float)source.bot.view_height;
        }
    }
    if (player && !source.bot.registered) {
        qa_q2_player state;
        if (!qa_native_host_q2_player(host, slot, &state, &ignored)) return false;
        traits.view_height = state.viewoffset[2];
    }
    struct application_native_q2_combat *combat = engine->source_combat;
    if (combat && combat->profile.notarget) {
        const application_q2_combat_profile *profile = &combat->profile;
        qa_native_instance *native = qa_native_host_instance(host);
        qa_native_address address;
        uint8_t flags[8];
        if (!qa_native_entity_address(native, slot, &address, &ignored) ||
            !qa_native_read(native, address + profile->flags, flags,
                profile->kex ? 8 : 4, &ignored)) return false;
        uint64_t value = profile->kex ? qa_load_u64le(flags) : qa_load_u32le(flags);
        traits.no_target |= (value & profile->notarget) != 0;
    }
    *out = traits;
    return true;
}
static combat_record *record_create(struct application_native_q2_combat *p, qa_actor_id actor,
    qa_error *error)
{
    if (p->record_pool.head == SIZE_MAX && !qa_pool_extend(&p->record_pool,
        qa_unified_frame_lease_arena(p->record_storage), p->record_chunk, error)) return NULL;
    size_t slot; combat_record *record = qa_pool_take(&p->record_pool, &slot);
    if (!record) { application_fail(error, QA_ERROR_MEMORY, "Retaining native combat actor context"); return NULL; }
    *record = (combat_record){.owner = p, .storage_slot = slot, .next = p->records};
    record->source.actor = actor;
    if (record->next) record->next->previous = record;
    p->records = record;
    qa_actor_index_put(&p->record_index, actor, (size_t)(uintptr_t)record);
    return record;
}
static void record_reclaim(combat_record *record)
{
    if (!record->retired || record->calls) return;
    struct application_native_q2_combat *p = record->owner;
    if (record->previous) record->previous->next = record->next;
    else p->records = record->next;
    if (record->next) record->next->previous = record->previous;
    qa_pool_release(&p->record_pool, record->storage_slot);
}
static void record_leave(combat_record *record)
{
    --record->calls; record_reclaim(record);
}
static bool pointer_read(struct application_native_q2_combat *p, qa_native_address at,
    qa_native_address *out, qa_error *error)
{
    uint8_t bytes[8]; size_t size = p->profile.target.pointer_bytes;
    if (!qa_native_read(native(p), at, bytes, size, error)) return false;
    *out = size == 4 ? qa_load_u32le(bytes) : qa_load_u64le(bytes); return true;
}
static bool vector_read(struct application_native_q2_combat *p, qa_native_address at,
    qa_vec3 *out, qa_error *error)
{
    uint8_t bytes[12];
    if (!at || !qa_native_read(native(p), at, bytes, sizeof(bytes), error)) return false;
    *out = qa_v3(qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8));
    return qa_vec_finite(*out) || application_fail(error, QA_ERROR_FORMAT, "Native damage geometry is not finite");
}
static bool actor_at(struct application_native_q2_combat *p, qa_native_address at,
    qa_actor_id *out, qa_error *error)
{
    if (!at) return application_fail(error, QA_ERROR_ARGUMENT, "Native damage actor pointer is null");
    return qa_native_host_source_actor(p->engine->provider->state.native.host, at, true, out, error) &&
        (out->registry || application_fail(error, QA_ERROR_NOT_FOUND, "Native damage actor is retired"));
}
static bool address_for(struct application_native_q2_combat *p, qa_actor_id actor,
    qa_native_address *out, qa_error *error)
{
    if (!actor.registry) return qa_native_entity_address(native(p), 0, out, error);
    return qa_native_host_source_address(p->engine->provider->state.native.host, actor, out, error);
}
static bool health_write(void *opaque, double value, qa_error *error)
{
    combat_record *record = opaque; ++record->calls; ++record->owner->engine->calls;
    bool ok = application_q2_combat_health_write(&record->source, value, error);
    if (ok) for (combat_frame *frame = record->owner->current; frame; frame = frame->previous)
        if (frame->active && qa_actor_id_equal(frame->record->source.actor, record->source.actor))
            frame->health = value;
    --record->owner->engine->calls; record_leave(record); return ok;
}
static bool state_read(void *opaque, qa_combat_state *out, qa_error *error)
{
    combat_record *record = opaque; ++record->calls; ++record->owner->engine->calls;
    bool ok = application_q2_combat_state_read(&record->source, out, error);
    --record->owner->engine->calls; record_leave(record); return ok;
}
static bool armor_validate(void *opaque, const qa_armor *value, qa_error *error)
{
    combat_record *record = opaque; ++record->calls; ++record->owner->engine->calls;
    bool ok = application_q2_combat_armor_validate(&record->source, value, error);
    --record->owner->engine->calls; record_leave(record); return ok;
}
static bool resource_projection_active(const struct application_native_q2_combat *p, qa_actor_id actor)
{
    for (application_native_q2_resource_scope *pickup = p->pickups; pickup; pickup = pickup->previous)
        if (pickup->frame.active && qa_actor_id_equal(pickup->frame.record->source.actor, actor) &&
            pickup->committed && !pickup->committed(pickup->committed_context)) return true;
    return false;
}
static bool armor_write(void *opaque, const qa_armor *value, qa_error *error)
{
    combat_record *record = opaque; ++record->calls; ++record->owner->engine->calls;
    bool ok = application_q2_combat_armor_write(&record->source, value, error);
    if (ok && !resource_projection_active(record->owner, record->source.actor))
      for (combat_frame *frame = record->owner->current; frame; frame = frame->previous)
        if (frame->active && qa_actor_id_equal(frame->record->source.actor, record->source.actor) &&
            !application_q2_combat_armor_read(&frame->record->source, &frame->armor, error)) { ok = false; break; }
    if (ok && !resource_projection_active(record->owner, record->source.actor))
      for (application_native_q2_resource_scope *pickup = record->owner->pickups; pickup; pickup = pickup->previous)
        if (pickup->frame.active && qa_actor_id_equal(pickup->frame.record->source.actor, record->source.actor) &&
            !application_q2_combat_armor_read(&pickup->frame.record->source, &pickup->frame.armor, error)) { ok = false; break; }
    --record->owner->engine->calls; record_leave(record); return ok;
}
static bool empty_armor(void *opaque, double points, qa_regular_armor *out, bool *selected, qa_error *error)
{
    combat_record *record = opaque; ++record->calls; ++record->owner->engine->calls;
    bool ok = application_q2_combat_empty_armor(&record->source, points, out, selected, error);
    --record->owner->engine->calls; record_leave(record); return ok;
}
static bool normalize_armor(void *opaque, const qa_armor *input, qa_armor *out, qa_error *error)
{
    combat_record *record = opaque; ++record->calls; ++record->owner->engine->calls;
    bool ok = application_q2_combat_normalize_armor(&record->source, input, out, error);
    --record->owner->engine->calls; record_leave(record); return ok;
}
static bool refresh_comparison_state(struct application_native_q2_combat *p, qa_error *error)
{
    /* Nested source requests and shared callbacks have already journaled their
     * stores. Retain only the next source instruction's raw comparison values. */
    for (combat_frame *frame = p->current; frame; frame = frame->previous) {
        if (!frame->active || frame->reacting) continue;
        qa_combat_state state;
        if (!state_read(frame->record, &state, error) ||
            !vector_read(p, frame->record->source.address + p->profile.velocity, &frame->velocity, error)) return false;
        frame->health = state.health;
        if (!resource_projection_active(p, frame->record->source.actor)) frame->armor = state.armor;
    }
    for (application_native_q2_resource_scope *pickup = p->pickups; pickup; pickup = pickup->previous)
        if (pickup->frame.active && !pickup->frame.record->retired &&
            !resource_projection_active(p, pickup->frame.record->source.actor) &&
            !application_q2_combat_armor_read(&pickup->frame.record->source, &pickup->frame.armor, error)) return false;
    return true;
}
static bool committed_health(void *opaque, qa_native_instance *instance,
    const qa_native_write_event *event, qa_error *error)
{
    (void)instance; combat_frame *frame = opaque;
    if (!frame->active || frame->reacting) return true;
    if (!application_q2_combat_actor_valid(&frame->record->source, error)) return false;
    if (event->before.size != 4 || event->after.size != 4)
        return application_fail(error, QA_ERROR_FORMAT, "Native health store has a different original extent");
    int32_t source_before = qa_load_i32le(event->before.data), source_after = qa_load_i32le(event->after.data);
    double before = source_before, after = source_after;
    if (frame->health != before)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native health store does not match exact observed authority");
    frame->health = after;
    if (frame->owner->current != frame) return true;
    if (before == after) return true;
    qa_damage_mutation mutation = {.kind = QA_MUTATION_HEALTH, .value.health = {before, after}};
    if (!qa_damage_observe(frame->observer, &mutation, error)) return false;
    frame->result.applied_damage += before - after; return true;
}
static bool committed_velocity(void *opaque, qa_native_instance *instance,
    const qa_native_write_event *event, qa_error *error)
{
    (void)instance; combat_frame *frame = opaque;
    if (!frame->active || frame->reacting) return true;
    if (!application_q2_combat_actor_valid(&frame->record->source, error)) return false;
    if (event->before.size != 12 || event->after.size != 12)
        return application_fail(error, QA_ERROR_FORMAT, "Native velocity store has a different original extent");
    qa_vec3 before = qa_v3(qa_load_f32le(event->before.data), qa_load_f32le(event->before.data + 4), qa_load_f32le(event->before.data + 8)),
        after = qa_v3(qa_load_f32le(event->after.data), qa_load_f32le(event->after.data + 4), qa_load_f32le(event->after.data + 8));
    if (!qa_vec_finite(before) || !qa_vec_finite(after) ||
        !qa_vec_equal(before, frame->velocity))
        return application_fail(error, QA_ERROR_FORMAT, "Native velocity store differs from its observed source vector");
    frame->velocity = after;
    if (frame->owner->current != frame) return true;
    qa_damage_mutation mutation = {.kind = QA_MUTATION_SOURCE_VELOCITY,
        .value.velocity = {before, after, frame->request->attack.movement_provider}};
    return qa_damage_observe(frame->observer, &mutation, error);
}
static bool committed_armor(void *opaque, qa_native_instance *instance,
    const qa_native_write_event *event, qa_error *error)
{
    (void)instance; (void)event; combat_frame *frame = opaque;
    if (!frame->active || frame->reacting ||
        resource_projection_active(frame->owner, frame->record->source.actor)) return true;
    qa_armor after;
    if (!application_q2_combat_armor_read(&frame->record->source, &after, error)) return false;
    qa_armor before = frame->armor; frame->armor = after;
    if (frame->owner->current != frame) return true;
    if (qa_armor_equal(before, after)) return true;
    qa_damage_mutation mutation = {.kind = QA_MUTATION_ARMOR, .value.armor = {before, after}};
    return qa_damage_observe(frame->observer, &mutation, error);
}
static bool committed_callback(void *opaque, qa_native_instance *instance,
    const qa_native_write_event *event, qa_error *error)
{
    (void)instance; combat_frame *frame = opaque;
    if (!frame->active || frame->reacting) return true;
    if (!application_q2_combat_actor_valid(&frame->record->source, error)) return false;
    size_t bytes = frame->owner->profile.target.pointer_bytes;
    if (event->after.size != bytes) return application_fail(error, QA_ERROR_FORMAT, "Native reaction pointer store has a different original extent");
    qa_native_address value = bytes == 4 ? qa_load_u32le(event->after.data) : qa_load_u64le(event->after.data);
    if (!value) return true;
    application_q2_call_operation operation = event->address == frame->record->source.address + frame->owner->profile.pain
        ? APPLICATION_Q2_PAIN : APPLICATION_Q2_DEATH;
    combat_reaction *hook = reaction_find(&frame->owner->reactions, value);
    if (hook && hook->operation == operation) return true;
    return application_fail(error, QA_ERROR_UNSUPPORTED, "Native reaction store needs a prequalified callback entry before source continuation");
}
static bool remove_watches(combat_frame *frame, qa_error *error)
{
    qa_native_write_observer **bindings[] = {&frame->health_watch, &frame->velocity_watch,
        &frame->armor_watch, &frame->flags_watch, &frame->pain_watch, &frame->die_watch};
    bool ok = true; qa_error first = {0};
    for (size_t i = 0; i < sizeof(bindings) / sizeof(*bindings); ++i) {
        qa_error current = {0};
        if (qa_native_unobserve_writes(*bindings[i], &current)) *bindings[i] = NULL;
        else if (ok) { ok = false; first = current; }
    }
    if (!ok && error) *error = first;
    return ok;
}
static bool committed_resources(void *opaque, qa_native_instance *instance,
    const qa_native_write_event *event, qa_error *error)
{
    (void)instance; (void)event;
    application_native_q2_resource_scope *pickup = opaque;
    combat_frame *frame = &pickup->frame;
    combat_record *record = frame->record;
    if (!frame->active || record->retired || !record->bound ||
        (pickup->execution && !qa_pickup_current(pickup->execution)) ||
        resource_projection_active(frame->owner, record->source.actor)) return true;
    qa_armor after;
    if (!application_q2_combat_armor_read(&record->source, &after, error)) return false;
    qa_armor before = frame->armor; frame->armor = after;
    struct application_native_q2_combat *p = frame->owner;
    qa_actor_id actor = record->source.actor;
    if (qa_combat_protection_owner(shared(p), actor, pickup->channel, NULL, NULL)) return true;
    for (combat_frame *damage = p->current; damage; damage = damage->previous)
        if (damage->active && qa_actor_id_equal(damage->record->source.actor, actor)) return true;
    for (application_native_q2_resource_scope *nested = p->pickups; nested != pickup; nested = nested->previous)
        if (nested->frame.active && nested->channel == pickup->channel &&
            qa_actor_id_equal(nested->frame.record->source.actor, actor)) return true;
    bool regular = pickup->channel == QA_PROTECTION_REGULAR;
    if (regular ? qa_regular_armor_equal(before.regular, after.regular) :
        qa_powered_armor_equal(before.powered, after.powered)) return true;
    qa_protection_store change = {.regular = regular, .powered = !regular,
        .before = before, .after = after};
    return qa_combat_pickup_store(shared(p), actor, p->engine->provider->owner, &change, error);
}
bool application_native_q2_combat_resources_begin(struct application_native_q2 *engine,
    qa_actor_id actor, qa_pickup_execution *execution, qa_protection_channel channel,
    bool (*committed)(void *), void *committed_context,
    struct application_native_q2_resource_scope **out, qa_error *error)
{
    *out = NULL;
    struct application_native_q2_combat *p = engine->source_combat;
    combat_record *record = p ? record_for(p, actor) : NULL;
    if (!record || !record->bound || !p->active) return true;
    ++record->calls; ++engine->calls;
    qa_unified_frame_lease *storage = application_control_storage_acquire(engine->provider->application, error);
    application_native_q2_resource_scope *scope = storage ? qa_unified_frame_lease_alloc(storage, 1,
        sizeof(*scope), _Alignof(application_native_q2_resource_scope), error) : NULL;
    if (!scope) {
        qa_unified_frame_lease_release(storage); --engine->calls; record_leave(record); return false;
    }
    scope->frame = (combat_frame){.owner = p, .storage = storage, .record = record, .active = true};
    scope->previous = p->pickups; scope->execution = execution; scope->channel = channel;
    scope->committed = committed; scope->committed_context = committed_context;
    p->pickups = scope;
    qa_native_address client = 0;
    bool okay = application_q2_combat_armor_read(&record->source, &scope->frame.armor, error) &&
        pointer_read(p, record->source.address + p->profile.client_pointer, &client, error) &&
        qa_native_observe_writes(native(p), record->source.address + p->profile.flags,
            p->profile.kex ? 8 : 4, committed_resources, scope, &scope->frame.flags_watch, error);
    if (okay && client) okay = qa_native_observe_writes(native(p), client + p->profile.inventory,
        (size_t)p->profile.inventory_count * 4, committed_resources, scope, &scope->frame.armor_watch, error);
    if (okay) { *out = scope; return true; }
    qa_error cleanup = {0};
    (void)application_native_q2_combat_resources_end(scope, &cleanup); return false;
}
bool application_native_q2_combat_resources_end(struct application_native_q2_resource_scope *scope,
    qa_error *error)
{
    if (!scope) return true;
    struct application_native_q2_combat *p = scope->frame.owner;
    struct application_native_q2 *engine = p->engine;
    combat_record *record = scope->frame.record;
    scope->frame.active = false; p->pickups = scope->previous;
    bool okay = remove_watches(&scope->frame, error);
    if (okay) qa_unified_frame_lease_release(scope->frame.storage);
    else { scope->frame.retained_next = p->retained; p->retained = &scope->frame; }
    --engine->calls;
    if (okay) record_leave(record);
    return okay;
}
bool application_native_q2_combat_resources(struct application_native_q2 *engine,
    qa_actor_id actor, qa_pickup_execution *execution, qa_protection_channel channel,
    bool (*execute)(void *, qa_error *), void *context,
    bool (*committed)(void *), void *committed_context, qa_error *error)
{
    application_native_q2_resource_scope *scope = NULL;
    if (!application_native_q2_combat_resources_begin(engine, actor, execution, channel,
        committed, committed_context, &scope, error)) return false;
    bool okay = execute(context, error); qa_error cleanup = {0};
    if (!application_native_q2_combat_resources_end(scope, &cleanup)) {
        if (okay && error) *error = cleanup;
        okay = false;
    }
    return okay;
}
typedef struct original_reaction {
    qa_native_entry_observer *binding;
    const qa_native_value *arguments;
    size_t count;
    qa_native_value *result;
} original_reaction;
static bool reaction_original(void *opaque, qa_error *error)
{
    original_reaction *call = opaque;
    return qa_native_invoke_original(call->binding, call->arguments, call->count, call->result, error);
}
static bool reaction_entry(void *opaque, qa_native_instance *instance, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *error)
{
    (void)instance; combat_reaction *hook = opaque; struct application_native_q2_combat *p = hook->owner;
    combat_frame *frame = p->current;
    combat_deferred *deferred = p->deferred;
    if (!p->active || ((!frame || !frame->active) && (!deferred || deferred->dispatching)))
        return qa_native_invoke_original(binding, arguments, count, result, error);
    qa_native_value fields[APPLICATION_Q2_FIELD_COUNT];
    if (!application_q2_call_project(&p->profile.calls[hook->operation], arguments, count, fields, error)) return false;
    qa_native_address target = fields[APPLICATION_Q2_TARGET].as.address;
    bool deferred_call = deferred && target == deferred->record->source.address && frame == deferred->parent_current;
    if (deferred_call && deferred->dispatching)
        return qa_native_invoke_original(binding, arguments, count, result, error);
    if (!deferred_call && frame && frame->active && frame->reacting && target == frame->record->source.address)
        return qa_native_invoke_original(binding, arguments, count, result, error);
    bool synchronous = !deferred_call && frame && frame->active && !frame->reacting && target == frame->record->source.address;
    combat_record *record = synchronous ? frame->record :
        deferred && !deferred->dispatching && target == deferred->record->source.address ? deferred->record : NULL;
    if (!record) return qa_native_invoke_original(binding, arguments, count, result, error);
    if (!application_q2_combat_actor_valid(&record->source, error)) return false;
    qa_damage_result reaction = {.reaction = hook->operation == APPLICATION_Q2_DEATH ? QA_REACTION_DEATH : QA_REACTION_PAIN,
        .applied_damage = fields[APPLICATION_Q2_AMOUNT].as.i32};
    const qa_damage_request *request = synchronous ? frame->request : deferred->request;
    double kick = request->knockback; qa_vec3 point = request->point;
    if (hook->operation == APPLICATION_Q2_PAIN) kick = fields[APPLICATION_Q2_KICK].as.f32;
    else if (!vector_read(p, fields[APPLICATION_Q2_POINT].as.address, &point, error)) return false;
    original_reaction original = {binding, arguments, count, result};
    *result = (qa_native_value){.type = QA_NATIVE_VOID};
    if (synchronous) {
        frame->reacting = true; frame->result = reaction;
        if (!remove_watches(frame, error)) return false;
        return qa_damage_dispatch_source_reaction(frame->observer, &frame->result,
            kick, point, p->engine->provider->owner, reaction_original, &original, error);
    }
    deferred->dispatching = true;
    bool ok = qa_source_reaction_dispatch(deferred->observer, &reaction, kick, point, p->engine->provider->owner,
        reaction_original, &original, error);
    deferred->dispatching = false; return ok;
}
static bool ensure_reaction(struct application_native_q2_combat *p, qa_native_address address,
    application_q2_call_operation operation, qa_error *error)
{
    if (!address) return true;
    combat_reaction *hook = reaction_find(&p->reactions, address);
    if (hook) {
        if (hook->operation != operation)
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Native reaction entry is shared by incompatible original call contracts");
        return hook->binding || qa_native_observe_entry(native(p), address,
            &p->profile.calls[operation].signature, reaction_entry, hook, &hook->binding, error);
    }
    hook = reaction_take(p, &p->reactions, error);
    if (!hook) return false;
    hook->address = address; hook->operation = operation;
    if (!qa_native_observe_entry(native(p), address, &p->profile.calls[operation].signature,
        reaction_entry, hook, &hook->binding, error)) {
        qa_pool_release(&p->reactions.pool, hook->storage_slot); return false;
    }
    hook->next = p->reactions.first; p->reactions.first = hook;
    qa_id_index_insert(&p->reactions.index, QA_STRING_NONE, address, hook->storage_slot);
    return true;
}
static bool execute_damage(combat_record *record, combat_incoming *incoming,
    const qa_damage_request *request, qa_damage_observer *observer,
    qa_damage_result *result, qa_error *error)
{
    struct application_native_q2_combat *p = record->owner;
    if (!p->active || p->retained ||
        !qa_combat_primary_current(shared(p), record->source.actor, record->serial, record) ||
        !qa_actor_id_equal(record->source.actor, request->target))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native damage requires its active qualified synchronous source owner");
    ++record->calls; ++p->engine->calls;
    qa_unified_frame_lease *storage = application_control_storage_acquire(p->engine->provider->application, error);
    size_t argument_count = p->profile.calls[APPLICATION_Q2_DAMAGE].signature.parameter_count;
    combat_frame *frame = storage ? qa_unified_frame_lease_alloc(storage, 1,
        sizeof(*frame) + argument_count * sizeof(qa_native_value), _Alignof(combat_frame), error) : NULL;
    if (!frame) qa_unified_frame_lease_release(storage);
    qa_native_address vectors = 0, pain, die, client; qa_combat_state state;
    bool ok = frame != NULL, retained = false;
    if (ok) {
        frame->storage = storage;
        frame->owner = p; frame->record = record; frame->request = request; frame->observer = observer;
        frame->previous = p->current; p->current = frame; frame->active = true;
        ok = state_read(record, &state, error) &&
            vector_read(p, record->source.address + p->profile.velocity, &frame->velocity, error) &&
            pointer_read(p, record->source.address + p->profile.pain, &pain, error) &&
            pointer_read(p, record->source.address + p->profile.die, &die, error) &&
            ensure_reaction(p, pain, APPLICATION_Q2_PAIN, error) && ensure_reaction(p, die, APPLICATION_Q2_DEATH, error);
        if (ok) { frame->health = state.health; frame->armor = state.armor; }
    }
    if (ok) ok = qa_native_observe_writes(native(p), record->source.address + p->profile.health, 4,
        committed_health, frame, &frame->health_watch, error) &&
        qa_native_observe_writes(native(p), record->source.address + p->profile.velocity, 12,
        committed_velocity, frame, &frame->velocity_watch, error) &&
        pointer_read(p, record->source.address + p->profile.client_pointer, &client, error) &&
        qa_native_observe_writes(native(p), record->source.address + p->profile.pain, p->profile.target.pointer_bytes,
            committed_callback, frame, &frame->pain_watch, error) &&
        qa_native_observe_writes(native(p), record->source.address + p->profile.die, p->profile.target.pointer_bytes,
            committed_callback, frame, &frame->die_watch, error);
    if (ok && client) ok = qa_native_observe_writes(native(p), client + p->profile.inventory,
        (size_t)p->profile.inventory_count * 4, committed_armor, frame, &frame->armor_watch, error);
    if (ok) ok = application_q2_kex_damage_track(p->kex, &record->source, error);
    application_q2_call *call = &p->profile.calls[APPLICATION_Q2_DAMAGE];
    qa_native_value fields[APPLICATION_Q2_FIELD_COUNT] = {0};
    qa_native_value *arguments = frame ? (qa_native_value *)(frame + 1) : NULL;
    uint8_t mod[3];
    if (incoming && !qa_actor_id_equal(incoming->target, request->target))
        ok = application_fail(error, QA_ERROR_UNSUPPORTED, "Native source continuation cannot change its original damage target");
    if (ok) {
        if (!isfinite(request->amount) || !isfinite(request->knockback) ||
            (double)request->amount < INT32_MIN || (double)request->amount > INT32_MAX ||
            (double)request->knockback < INT32_MIN || (double)request->knockback > INT32_MAX)
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Native damage exceeds its original int32 arguments");
    }
    if (ok && !incoming) ok = qa_native_allocate(native(p), 36, INT32_MIN + 12, &vectors, error);
    if (ok) {
        qa_vec3 values[] = {request->direction, request->point, request->normal}; uint8_t bytes[36];
        for (size_t i = 0; i < 3; ++i) {
            float axes[] = {values[i].x, values[i].y, values[i].z};
            for (size_t j = 0; j < 3; ++j) { uint32_t bits; memcpy(&bits, &axes[j], 4); qa_store_u32le(bytes + i * 12 + j * 4, bits); }
        }
        fields[APPLICATION_Q2_TARGET] = (qa_native_value){.type = QA_NATIVE_ADDRESS, .as.address = record->source.address};
        fields[APPLICATION_Q2_INFLICTOR].type = fields[APPLICATION_Q2_ATTACKER].type = QA_NATIVE_ADDRESS;
        ok =
            address_for(p, request->attack.inflictor, &fields[APPLICATION_Q2_INFLICTOR].as.address, error) &&
            address_for(p, request->attack.attacker, &fields[APPLICATION_Q2_ATTACKER].as.address, error) &&
            application_q2_combat_cause_lower(&p->profile, &request->attack.cause, mod, &fields[APPLICATION_Q2_CAUSE], error);
        fields[APPLICATION_Q2_DIRECTION] = (qa_native_value){.type = QA_NATIVE_ADDRESS, .as.address = vectors};
        fields[APPLICATION_Q2_POINT] = (qa_native_value){.type = QA_NATIVE_ADDRESS, .as.address = vectors + 12};
        fields[APPLICATION_Q2_NORMAL] = (qa_native_value){.type = QA_NATIVE_ADDRESS, .as.address = vectors + 24};
        if (ok && incoming) {
            qa_native_value original[APPLICATION_Q2_FIELD_COUNT];
            ok = application_q2_call_project(call, incoming->arguments, incoming->count, original, error);
            const application_q2_call_field geometry[] = {APPLICATION_Q2_DIRECTION, APPLICATION_Q2_POINT, APPLICATION_Q2_NORMAL};
            for (size_t i = 0; ok && i < 3; ++i) {
                fields[geometry[i]] = original[geometry[i]];
                for (size_t j = 0; j < i; ++j)
                    if (fields[geometry[i]].as.address == fields[geometry[j]].as.address &&
                        memcmp(bytes + i * 12, bytes + j * 12, 12))
                        ok = application_fail(error, QA_ERROR_UNSUPPORTED, "Native aliased damage geometry cannot encode different modified vectors");
            }
            for (size_t i = 0; ok && i < 3; ++i)
                ok = qa_native_write(native(p), fields[geometry[i]].as.address, (qa_bytes){bytes + i * 12, 12}, error);
        } else if (ok) ok = qa_native_write(native(p), vectors, (qa_bytes){bytes, sizeof(bytes)}, error);
        fields[APPLICATION_Q2_AMOUNT] = (qa_native_value){.type = QA_NATIVE_I32, .as.i32 = (int32_t)request->amount};
        fields[APPLICATION_Q2_KNOCKBACK] = (qa_native_value){.type = QA_NATIVE_I32, .as.i32 = (int32_t)request->knockback};
        qa_damage_flags flags = qa_attack_flags(&request->attack);
        uint32_t source_flags = request->attack.cause.kind == QA_CAUSE_Q2 ? request->attack.cause.source.q2.flags :
            (request->radius ? 1u : 0u) | (flags.no_armor ? 2u : 0u) | (flags.energy ? 4u : 0u) |
            (flags.no_knockback ? 8u : 0u) | (flags.no_protection ? 32u : 0u);
        fields[APPLICATION_Q2_FLAGS] = (qa_native_value){.type = QA_NATIVE_I32, .as.i32 = (int32_t)source_flags};
    }
    if (ok) ok = application_q2_call_lower(call, native(p), fields,
        incoming ? incoming->arguments : NULL, incoming ? incoming->count : 0, arguments, error);
    if (ok) {
        qa_native_value ignored = {.type = QA_NATIVE_VOID};
        if (incoming) ok = qa_native_invoke_original(incoming->binding, arguments, call->signature.parameter_count, &ignored, error);
        else {
            qa_native_address entry; frame->entering = true;
            ok = qa_native_rva(native(p), p->profile.damage_entry, 1, &entry, error) &&
                qa_native_invoke(native(p), entry, &call->signature, arguments, call->signature.parameter_count, &ignored, error);
            frame->entering = false;
        }
    }
    if (frame) {
        frame->active = false; frame->request = NULL; frame->observer = NULL;
        qa_error cleanup = {0}; bool removed = remove_watches(frame, &cleanup);
        if (!removed && ok) { ok = false; if (error) *error = cleanup; }
        if (ok) *result = frame->result;
        p->current = frame->previous;
        if (removed) qa_unified_frame_lease_release(frame->storage);
        else { frame->retained_next = p->retained; p->retained = frame; retained = true; }
    }
    if (ok) ok = refresh_comparison_state(p, error);
    if (vectors) {
        qa_error cleanup = {0};
        if (!qa_native_free(native(p), vectors, &cleanup) && ok) { ok = false; if (error) *error = cleanup; }
    }
    --p->engine->calls;
    if (!retained) record_leave(record);
    return ok;
}
static bool source_damage(void *opaque, qa_combat *combat, const qa_damage_request *request,
    qa_damage_observer *observer, qa_damage_result *result, qa_error *error)
{
    (void)combat; return execute_damage(opaque, NULL, request, observer, result, error);
}
static bool incoming_damage(void *opaque, qa_combat *combat, const qa_damage_request *request,
    qa_damage_observer *observer, qa_damage_result *result, qa_error *error)
{
    (void)combat; combat_incoming *incoming = opaque;
    return execute_damage(incoming->record, incoming, request, observer, result, error);
}
static qa_combat_binding source_binding(combat_record *record)
{
    qa_actor_owner owner = record->owner->engine->provider->owner;
    return (qa_combat_binding){.context = record, .read = state_read, .write_health = health_write,
        .write_armor = armor_write, .validate_armor = armor_validate, .empty_regular_armor = empty_armor,
        .normalize_legacy_armor = normalize_armor, .source_damage = source_damage,
        .source_armor_stages = {true, true}, .has_primary_protection = {true, true},
        .primary_protection = {owner, owner}};
}
static bool weapon_cause(uint32_t id)
{
    return (id >= 1 && id <= 16) || id == 24 || id == 34 || id == 35 || id == 39 ||
        (id >= 40 && id <= 47) || id == 51 || id == 56 || id == 58;
}
static bool damage_entry(void *opaque, qa_native_instance *instance, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *error)
{
    (void)instance; struct application_native_q2_combat *p = opaque;
    if (!p->active) return qa_native_invoke_original(binding, arguments, count, result, error);
    qa_native_value fields[APPLICATION_Q2_FIELD_COUNT];
    if (!application_q2_call_project(&p->profile.calls[APPLICATION_Q2_DAMAGE], arguments, count, fields, error)) return false;
    if (p->current && p->current->entering &&
        fields[APPLICATION_Q2_TARGET].as.address == p->current->record->source.address) {
        p->current->entering = false;
        return qa_native_invoke_original(binding, arguments, count, result, error);
    }
    qa_native_address target = fields[APPLICATION_Q2_TARGET].as.address;
    qa_actor_id target_actor;
    uint32_t target_slot;
    qa_native_slot_binding target_binding;
    if (!actor_at(p, target, &target_actor, error) ||
        !qa_native_entity_slot(native(p), target, &target_slot, error) ||
        !qa_native_slot(native(p), target_slot, &target_binding, error)) return false;
    bool projected = target_binding.kind == QA_NATIVE_SLOT_BORROWED &&
        application_native_q2_client_slot(p->engine, target_binding.actor) != target_slot;
    qa_application *app = p->engine->provider->application;
    application_provider *combat = application_provider_for(app, target_actor, QA_ROLE_COMBAT, NULL);
    bool original = !projected && combat == p->engine->provider;
    if (original) {
        uint8_t damageable[4]; size_t extent = p->profile.kex ? 1 : 4;
        if (target > UINT64_MAX - p->profile.damageable ||
            !qa_native_read(native(p), target + p->profile.damageable, damageable, extent, error)) return false;
        if (!(p->profile.kex ? damageable[0] : qa_load_u32le(damageable)))
            return qa_native_invoke_original(binding, arguments, count, result, error);
    } else {
        if (!qa_combat_storage_serial(shared(p), target_actor)) {
            *result = (qa_native_value){.type = QA_NATIVE_VOID}; return true;
        }
        qa_combat_state traits;
        if (!qa_combat_read_traits(shared(p), target_actor, &traits, error)) return false;
        if (!traits.can_take_damage) {
            *result = (qa_native_value){.type = QA_NATIVE_VOID}; return true;
        }
    }
    ++p->engine->calls;
    qa_damage_request request = {.target = target_actor}; qa_actor_id attacker, inflictor;
    bool ok = actor_at(p, fields[APPLICATION_Q2_ATTACKER].as.address, &attacker, error) &&
        actor_at(p, fields[APPLICATION_Q2_INFLICTOR].as.address, &inflictor, error);
    uint32_t flags = (uint32_t)fields[APPLICATION_Q2_FLAGS].as.i32;
    qa_damage_cause cause; qa_native_address client;
    if (ok) ok = application_q2_combat_cause_read(&p->profile, &fields[APPLICATION_Q2_CAUSE], flags, &cause, error) &&
        pointer_read(p, fields[APPLICATION_Q2_ATTACKER].as.address + p->profile.client_pointer, &client, error);
    if (ok) ok = application_native_q2_attack_read(p->engine, attacker, inflictor, request.target,
        client && weapon_cause((uint32_t)cause.source.q2.means_of_death & ~UINT32_C(0x08000000)), &request.attack, error);
    if (ok) {
        request.attack.cause = cause; request.radius = (flags & 1) != 0;
        request.amount = fields[APPLICATION_Q2_AMOUNT].as.i32;
        request.knockback = fields[APPLICATION_Q2_KNOCKBACK].as.i32;
        application_provider *movement = application_provider_for(app, request.target, QA_ROLE_MOVEMENT, NULL);
        request.attack.combat_provider = combat ? combat->owner : 0;
        request.attack.movement_provider = movement ? movement->owner : 0;
    }
    if (ok) ok = vector_read(p, fields[APPLICATION_Q2_DIRECTION].as.address, &request.direction, error) &&
        vector_read(p, fields[APPLICATION_Q2_POINT].as.address, &request.point, error) &&
        vector_read(p, fields[APPLICATION_Q2_NORMAL].as.address, &request.normal, error);
    combat_incoming incoming = {.previous = p->incoming, .binding = binding,
        .arguments = arguments, .count = count, .target = request.target};
    if (ok && original) {
        combat_record *record = record_for(p, request.target);
        if (!record || !record->bound) ok = application_fail(error, QA_ERROR_NOT_FOUND, "Incoming native damage has no published source combat primary");
        else { incoming.record = record; ++record->calls; }
    }
    if (ok) {
        qa_damage_outcome outcome = {0};
        if (original) {
            p->incoming = &incoming;
            ok = qa_combat_run_source(shared(p), &request, incoming_damage, &incoming, &outcome, error);
            p->incoming = incoming.previous;
        } else ok = application_native_q2_combat_apply(p->engine, &request, &outcome, error);
        qa_damage_outcome_release(&outcome);
        if (ok) ok = refresh_comparison_state(p, error);
    }
    if (ok) *result = (qa_native_value){.type = QA_NATIVE_VOID};
    --p->engine->calls;
    if (incoming.record) record_leave(incoming.record);
    return ok;
}
static bool absorb_stage(struct application_native_q2_combat *p, combat_frame *frame,
    qa_protection_channel channel, int32_t amount, uint32_t source_flags,
    qa_native_address point, qa_native_address normal, int32_t *out, qa_error *error)
{
    if (!application_q2_combat_actor_valid(&frame->record->source, error)) return false;
    qa_damage_geometry geometry = {.direction = frame->request->direction};
    qa_body_state body; qa_combat_state state;
    if (!vector_read(p, point, &geometry.point, error) ||
        !vector_read(p, normal, &geometry.normal, error) ||
        !qa_world_body_read(p->engine->world, frame->request->target, &body, error) ||
        !state_read(frame->record, &state, error)) return false;
    qa_vec3 forward;
    qa_angle_vectors(body.angles,&forward,NULL,NULL,QA_ANGLE_FLOAT_MATH);
    qa_armor_context context = {.q2_profile = true, .rerelease = p->profile.kex,
        .ctf = qa_json_string_equal(p->profile.document, qa_json_get(p->profile.document, p->profile.world, "game"), "ctf"),
        .alive = state.health > 0, .screen_facing_dot = qa_vec_dot(
            qa_vec_normalize(qa_vec_sub(geometry.point, body.origin)), forward)};
    qa_damage_flags flags = {.no_armor = (source_flags & 2) != 0,
        .no_power_armor = (source_flags & 0x100) != 0, .no_regular_armor = (source_flags & 0x80) != 0,
        .energy = (source_flags & 4) != 0, .regular_scale = 1};
    double saved;
    if (!qa_combat_absorb(shared(p), frame->request, channel, &geometry,
        amount, flags, &context, &saved, NULL, error) ||
        !application_q2_combat_actor_valid(&frame->record->source, error) ||
        !isfinite(saved) || (double)saved < INT32_MIN || (double)saved > INT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native armor replacement did not retain its live original target or int32 result");
    /* Replacement stores use shared authoritative callbacks rather than an
     * application instruction. Their observer has already advanced the journal. */
    if (!refresh_comparison_state(p, error)) return false;
    *out = (int32_t)saved; return true;
}
const qa_damage_request *application_native_q2_combat_request(struct application_native_q2 *engine,
    qa_actor_id actor)
{
    struct application_native_q2_combat *p = engine ? engine->source_combat : NULL;
    for (combat_frame *frame = p ? p->current : NULL; frame; frame = frame->previous)
        if (frame->active && !frame->reacting && qa_actor_id_equal(frame->record->source.actor, actor))
            return frame->request;
    return NULL;
}
bool application_native_q2_combat_inline_armor(struct application_native_q2 *engine,
    qa_native_address target, int32_t amount, uint32_t flags, qa_native_address point,
    qa_native_address normal, bool *handled, int32_t *saved, qa_error *error)
{
    *handled = false;
    struct application_native_q2_combat *p = engine ? engine->source_combat : NULL;
    combat_frame *frame = p ? p->current : NULL;
    if (!p || !p->active || !frame || !frame->active || frame->reacting ||
        frame->record->source.address != target ||
        !qa_combat_protection_owner(shared(p), frame->request->target, QA_PROTECTION_REGULAR, NULL, NULL))
        return true;
    if (!absorb_stage(p, frame, QA_PROTECTION_REGULAR, amount, flags, point, normal, saved, error)) return false;
    *handled = true; return true;
}
static bool armor_entry(void *opaque, qa_native_instance *instance, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *error)
{
    (void)instance; combat_armor_hook *hook = opaque; struct application_native_q2_combat *p = hook->owner;
    combat_frame *frame = p->current;
    if (!p->active || !frame || !frame->active || frame->reacting)
        return qa_native_invoke_original(binding, arguments, count, result, error);
    application_q2_call_operation operation = hook->channel == QA_PROTECTION_REGULAR
        ? APPLICATION_Q2_REGULAR_ARMOR : APPLICATION_Q2_POWER_ARMOR;
    qa_native_value fields[APPLICATION_Q2_FIELD_COUNT];
    if (!application_q2_call_project(&p->profile.calls[operation], arguments, count, fields, error)) return false;
    if (fields[APPLICATION_Q2_TARGET].as.address != frame->record->source.address ||
        !qa_combat_protection_owner(shared(p), frame->request->target, hook->channel, NULL, NULL))
        return qa_native_invoke_original(binding, arguments, count, result, error);
    int32_t saved;
    if (!absorb_stage(p, frame, hook->channel, fields[APPLICATION_Q2_AMOUNT].as.i32,
        (uint32_t)fields[APPLICATION_Q2_FLAGS].as.i32, fields[APPLICATION_Q2_POINT].as.address,
        fields[APPLICATION_Q2_NORMAL].as.address, &saved, error)) return false;
    *result = (qa_native_value){.type = QA_NATIVE_I32, .as.i32 = saved}; return true;
}
bool application_native_q2_combat_prepare(struct application_native_q2 *engine, qa_error *error)
{
    if (!engine->declaration || engine->profile == QA_NATIVE_Q2_CGAME_API2023 ||
        qa_native_declaration_callbacks(engine->declaration).data) return true;
    struct application_native_q2_combat *p = calloc(1, sizeof(*p));
    if (!p) return application_fail(error, QA_ERROR_MEMORY, "Preparing original native combat producer");
    p->engine = engine; engine->source_combat = p;
    const qa_actor_registry *registry = qa_session_actors(engine->provider->application->session);
    if (!qa_actor_index_create(&p->record_index, registry, NULL, error)) return false;
    p->record_chunk = qa_actors_capacity(registry);
    p->record_storage = application_control_storage_acquire(engine->provider->application, error);
    if (!p->record_storage || !qa_pool_prepare(&p->record_pool,
        qa_unified_frame_lease_arena(p->record_storage), p->record_chunk,
        sizeof(combat_record), _Alignof(combat_record), error)) return false;
    if (!reactions_prepare(p, &p->reactions, 0, error)) return false;
    if (!application_q2_combat_profile_read(engine, &p->profile, error)) return false;
    return !p->profile.kex || application_q2_kex_damage_prepare(&p->profile, &p->kex, error);
}
bool application_native_q2_combat_team_refresh(struct application_native_q2 *engine,
    uint32_t slot, qa_error *error)
{
    struct application_native_q2_combat *p = engine->source_combat;
    return !p || application_q2_combat_team_refresh(&p->profile, slot, error);
}
bool application_native_q2_combat_load(struct application_native_q2 *engine, qa_error *error)
{
    struct application_native_q2_combat *p = engine->source_combat;
    return !p || application_q2_kex_damage_bind(p->kex, error);
}
bool application_native_q2_combat_activate(struct application_native_q2 *engine, qa_error *error)
{
    struct application_native_q2_combat *p = engine->source_combat;
    if (!p || p->active) return true;
    if (!application_q2_combat_profile_resolve(&p->profile, error)) return false;
    qa_native_address entry;
    if (!p->damage && (!qa_native_rva(native(p), p->profile.damage_entry, 1, &entry, error) ||
        !qa_native_observe_entry(native(p), entry, &p->profile.calls[APPLICATION_Q2_DAMAGE].signature,
            damage_entry, p, &p->damage, error))) return false;
    for (unsigned i = 0; i < 2; ++i) {
        combat_armor_hook *hook = &p->armor[i]; hook->owner = p; hook->channel = (qa_protection_channel)i;
        if (hook->binding || (p->profile.kex && i == QA_PROTECTION_REGULAR)) continue;
        application_q2_call_operation operation = i == QA_PROTECTION_REGULAR ? APPLICATION_Q2_REGULAR_ARMOR : APPLICATION_Q2_POWER_ARMOR;
        if (!qa_native_rva(native(p), i == QA_PROTECTION_REGULAR ? p->profile.regular_entry : p->profile.power_entry, 1, &entry, error) ||
            !qa_native_observe_entry(native(p), entry, &p->profile.calls[operation].signature,
                armor_entry, hook, &hook->binding, error)) return false;
    }
    if (!application_q2_kex_damage_activate(p->kex, error)) return false;
    for (combat_reaction *hook=p->reactions.first; hook; hook=hook->next)
        if (!ensure_reaction(p,hook->address,hook->operation,error)) return false;
    p->active = true; return true;
}
bool application_native_q2_combat_admit(struct application_native_q2 *engine, uint32_t slot,
    qa_actor_id actor, bool admitting, qa_error *error)
{
    struct application_native_q2_combat *p = engine->source_combat;
    if (application_provider_for(engine->provider->application, actor, QA_ROLE_COMBAT, NULL) != engine->provider) return true;
    if (!p && application_native_q2_whole_source(engine, actor)) return true;
    if (!p || !p->active) return application_fail(error, QA_ERROR_UNSUPPORTED, "Native primary combat has no active original producer");
    combat_record *record = record_for(p, actor);
    if (record && record->bound && !qa_combat_primary_current(shared(p), actor, record->serial, record))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native combat primary differs from its ordinary admitted source lease");
    if (record && record->prepared)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native combat restore claim requires its final shared primary validation");
    if (record && record->bound && record->fuel_bound) return true;
    if (!record) {
        record = record_create(p, actor, error);
        if (!record) return false;
    }
    if (!record->bound && !application_q2_combat_actor_init(&p->profile, slot, actor, admitting, &record->source, error)) return false;
    qa_native_address client; qa_item_id cells = 0;
    if (!pointer_read(p, record->source.address + p->profile.client_pointer, &client, error)) return false;
    if (client && !application_q2_combat_team_refresh(&p->profile, slot, error)) return false;
    if (client) {
        if (application_provider_for(engine->provider->application, actor, QA_ROLE_INVENTORY, NULL) != engine->provider ||
            !slot || slot >= 257 || !engine->clients[slot].inventory_bound ||
            !qa_actor_id_equal(engine->clients[slot].actor, actor))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Native powered combat requires its actual admitted original inventory cell producer");
        if (!application_native_q2_inventory_item(engine, p->profile.cells, &cells, error)) return false;
        qa_inventory *inventory; qa_item_id previous;
        if (qa_combat_power_inventory(shared(p), actor, &inventory, &previous) &&
            (inventory != engine->provider->application->inventory || previous != cells))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Native powered combat cannot replace a different authoritative fuel identity");
    }
    qa_combat_binding binding = source_binding(record);
    ++record->calls;
    bool ok = record->bound || qa_combat_bind(shared(p), actor, &binding, true, error);
    record->source.admitting = false;
    if (!ok) { record_leave(record); return false; }
    record->serial = qa_combat_storage_serial(shared(p), actor); record->bound = true;
    if (client) {
        if (!qa_combat_bind_power_inventory(shared(p), actor, engine->provider->application->inventory, cells, error)) {
            record_leave(record); return false;
        }
    }
    record->fuel_bound = true;
    record_leave(record); return true;
}
bool application_native_q2_combat_detach(struct application_native_q2 *engine, qa_actor_id actor, qa_error *error)
{
    struct application_native_q2_combat *p = engine->source_combat;
    if (!p) return true;
    combat_record *record = record_for(p, actor);
    if (!record || !record->bound) return true;
    if (record->prepared && !qa_combat_primary_current(shared(p), actor, record->serial, record)) {
        record->bound = record->fuel_bound = record->prepared = false; return true;
    }
    if (!qa_actors_get(qa_session_actors(engine->provider->application->session), actor)) {
        record->bound = record->fuel_bound = record->prepared = false; return true;
    }
    ++record->calls;
    bool ok = qa_combat_detach_primary(shared(p), actor, record->serial, record, error);
    if (ok) record->bound = record->fuel_bound = record->prepared = false;
    record_leave(record); return ok;
}
void application_native_q2_combat_released(struct application_native_q2 *engine, qa_actor_id actor)
{
    struct application_native_q2_combat *p = engine->source_combat;
    combat_record *record = p ? record_for(p, actor) : NULL;
    if (record) {
        record->bound = record->fuel_bound = record->prepared = false;
        record->retired = true;
        qa_actor_index_remove(&p->record_index, actor);
        record_reclaim(record);
    }
    if (p) application_q2_kex_damage_released(p->kex, actor);
}
bool application_native_q2_combat_suspend(struct application_native_q2 *engine, qa_error *error)
{
    struct application_native_q2_combat *p = engine->source_combat;
    if (!p) return true;
    if (p->current || p->incoming || p->deferred || p->pickups) return application_fail(error, QA_ERROR_ARGUMENT, "Native combat suspension requires drained source damage, pickup and reaction helpers");
    combat_record *record = p->records;
    while (record) {
        ++record->calls;
        bool ok = application_native_q2_combat_detach(engine, record->source.actor, error);
        combat_record *next = record->next;
        record_leave(record);
        if (!ok) return false;
        record = next;
    }
    p->active = false;
    if (!application_q2_kex_damage_suspend(p->kex, error)) return false;
    combat_frame **frame_cursor = &p->retained;
    while (*frame_cursor) {
        combat_frame *frame = *frame_cursor;
        if (!remove_watches(frame, error)) return false;
        combat_record *frame_record = frame->record;
        *frame_cursor = frame->retained_next; qa_unified_frame_lease_release(frame->storage);
        record_leave(frame_record);
    }
    combat_reaction **cursor = &p->reactions.first;
    while (*cursor) {
        combat_reaction *hook = *cursor;
        if (!qa_native_unobserve_entry(hook->binding, error)) return false;
        *cursor = hook->next;
        qa_id_index_remove(&p->reactions.index, QA_STRING_NONE, hook->address);
        qa_pool_release(&p->reactions.pool, hook->storage_slot);
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (!qa_native_unobserve_entry(p->armor[i].binding, error)) return false;
        p->armor[i].binding = NULL;
    }
    if (!qa_native_unobserve_entry(p->damage, error)) return false;
    p->damage = NULL; p->profile.resolved = false; return true;
}
bool application_native_q2_combat_close(struct application_native_q2 *engine, qa_error *error)
{
    struct application_native_q2_combat *p = engine->source_combat;
    if (!p) return true;
    if (!application_native_q2_combat_suspend(engine, error)) return false;
    if (!application_q2_kex_damage_close(p->kex, error)) return false;
    p->kex = NULL;
    reactions_destroy(&p->reactions);
    qa_actor_index_destroy(&p->record_index);
    qa_unified_frame_lease_release(p->record_storage);
    application_q2_combat_profile_free(&p->profile); free(p); engine->source_combat = NULL; return true;
}
bool application_native_q2_combat_binding(application_provider *provider, qa_actor_id actor,
    uint64_t saved_serial, qa_combat_binding *out, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    struct application_native_q2_combat *p = engine ? engine->source_combat : NULL;
    if (!p || !out || !saved_serial || !provider->state.native.host ||
        !application_native_q2_idle(provider) || p->active || p->current || p->incoming || p->deferred ||
        application_provider_for(provider->application, actor, QA_ROLE_COMBAT, NULL) != provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Restored native combat descriptor requires its idle original module and suspended observers");
    uint32_t source_slot;
    if (!qa_native_host_slot_for_actor(provider->state.native.host, actor, &source_slot))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Restored native combat actor lacks its canonical source slot");
    combat_record *record = record_for(p, actor);
    if (record && record->bound && record->serial != saved_serial)
        return application_fail(error, QA_ERROR_ARGUMENT, "Restored native combat claim changes its retained source serial");
    if (!record) {
        record = record_create(p, actor, error);
        if (!record) return false;
    }
    if (!application_q2_combat_actor_init(&p->profile, source_slot, actor, false, &record->source, error) ||
        !application_q2_combat_profile_resolve(&p->profile, error) ||
        !application_q2_combat_team_refresh(&p->profile, source_slot, error)) return false;
    if (!record->bound) record->prepared = true;
    record->serial = saved_serial; record->bound = true;
    *out = source_binding(record); return true;
}
bool application_native_q2_combat_finish(application_provider *provider, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    struct application_native_q2_combat *p = engine ? engine->source_combat : NULL;
    if (!p) return true;
    if (!application_native_q2_idle(provider) || !p->active || p->current || p->incoming || p->deferred)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native combat finish requires its idle restored observer owner");
    for (combat_record *record = p->records; record; record = record->next) {
        if (!record->prepared) continue;
        if (!record->bound || !qa_combat_primary_current(shared(p), record->source.actor, record->serial, record) ||
            !application_q2_combat_actor_valid(&record->source, error))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native combat prepared claim differs from its exact imported shared primary");
        qa_native_address client;
        if (!pointer_read(p, record->source.address + p->profile.client_pointer, &client, error)) return false;
        if (client) {
            qa_inventory *inventory; qa_item_id item, cells;
            if (!application_native_q2_inventory_item(engine, p->profile.cells, &cells, error) ||
                !qa_combat_power_inventory(shared(p), record->source.actor, &inventory, &item) ||
                inventory != provider->application->inventory || item != cells)
                return application_fail(error, QA_ERROR_FORMAT, "Native combat restored primary lacks its exact authoritative cell identity");
        }
    }
    for (combat_record *record = p->records; record; record = record->next)
        if (record->prepared) { record->prepared = false; record->fuel_bound = true; }
    return true;
}

static bool execute_deferred(void *opaque, qa_source_reaction_observer *observer, qa_error *error)
{
    combat_deferred *frame = opaque;
    if (!application_q2_combat_actor_valid(&frame->record->source, error)) return false;
    frame->observer = observer;
    bool ok = frame->execute(frame->context, error);
    frame->observer = NULL; return ok;
}
bool application_native_q2_combat_deferred(struct application_native_q2 *engine,
    const qa_damage_request *request, const qa_damage_result *result,
    qa_source_reaction_body execute, void *context, qa_error *error)
{
    struct application_native_q2_combat *p = engine ? engine->source_combat : NULL;
    combat_record *record = p && request ? record_for(p, request->target) : NULL;
    if (!p || !p->active || p->retained || !record || !execute ||
        !qa_combat_primary_current(shared(p), record->source.actor, record->serial, record) ||
        !application_q2_combat_actor_valid(&record->source, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native deferred reaction requires its actual active source primary");
    qa_native_address pain, die;
    if (!pointer_read(p, record->source.address + p->profile.pain, &pain, error) ||
        !pointer_read(p, record->source.address + p->profile.die, &die, error) ||
        !ensure_reaction(p, pain, APPLICATION_Q2_PAIN, error) ||
        !ensure_reaction(p, die, APPLICATION_Q2_DEATH, error)) return false;
    combat_deferred frame = {.previous = p->deferred, .owner = p, .record = record,
        .parent_current = p->current, .execute = execute, .context = context, .request = request};
    p->deferred = &frame; ++record->calls; ++engine->calls;
    bool ok = qa_combat_run_source_reaction(shared(p), request, result, execute_deferred, &frame, error);
    --engine->calls; p->deferred = frame.previous; record_leave(record); return ok;
}
struct application_native_q2_combat_restore {
    combat_reactions reactions;
    struct application_q2_kex_restore *kex;
};
static bool combat_fields(qa_source_save_io *io, struct application_native_q2_combat *p,
    combat_reactions *reactions, qa_bytes *kex)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t count=0;
    if (!reading) for (combat_reaction *hook=reactions->first;hook;hook=hook->next) ++count;
    if (!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if (reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/12))
        return application_fail(io->error,QA_ERROR_FORMAT,"Native reaction count exceeds its actual saved fields");
    if (reading && !reactions_prepare(p, reactions, count, io->error)) return false;
    combat_reaction **tail=&reactions->first;
    for (size_t i=0;i<count;++i) {
        if (reading) {
            *tail=reaction_take(p, reactions, io->error);
            if (!*tail) return false;
        }
        combat_reaction *hook=*tail;
        uint32_t operation=(uint32_t)hook->operation;
        if (!qa_source_save_u64(io,&hook->address) || !qa_source_save_u32(io,&operation)) return false;
        if (!hook->address || (operation!=APPLICATION_Q2_PAIN && operation!=APPLICATION_Q2_DEATH) ||
            (!reading && !hook->binding))
            return application_fail(io->error,QA_ERROR_FORMAT,"Native reaction lacks its actual Source entry and call operation");
        hook->operation=(application_q2_call_operation)operation;
        combat_reaction *prior=reaction_find(reactions, hook->address);
        if (reading ? prior!=NULL : prior!=hook)
            return application_fail(io->error,QA_ERROR_FORMAT,"Native saved reaction entry repeats");
        if (reading) {
            uint8_t byte;
            if (!qa_native_read(native(p),hook->address,&byte,1,io->error)) return false;
            qa_id_index_insert(&reactions->index, QA_STRING_NONE, hook->address, hook->storage_slot);
        }
        tail=&hook->next;
    }
    size_t extent=kex->size;
    if (!qa_source_save_count(io,&extent,SIZE_MAX)) return false;
    if (reading) {
        if (io->offset>io->input.size || extent>io->input.size-io->offset)
            return application_fail(io->error,QA_ERROR_FORMAT,"Native combat child leaves its saved extent");
        *kex=(qa_bytes){io->input.data+io->offset,extent}; io->offset+=extent; return true;
    }
    return qa_source_save_bytes(io,(void *)kex->data,extent);
}
bool application_native_q2_combat_capture(struct application_native_q2 *engine, qa_buffer *out, qa_error *error)
{
    struct application_native_q2_combat *p = engine->source_combat;
    if (p && (p->current || p->incoming || p->retained || p->deferred))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native deferred capture requires drained damage frames");
    if (!p) return application_q2_kex_damage_capture(NULL,out,error);
    qa_buffer child={0}; qa_source_save_io io={0};
    bool ok=application_q2_kex_damage_capture(p->kex,&child,error) && qa_source_save_writer(&io,NULL,error);
    qa_bytes bytes={child.data,child.size};
    if (ok) ok=combat_fields(&io,p,&p->reactions,&bytes) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&child); return ok;
}
bool application_native_q2_combat_restore_ready(const struct application_native_q2 *engine, qa_error *error)
{
    const struct application_native_q2_combat *p = engine->source_combat;
    if (!p) return true;
    if (p->active || p->current || p->incoming || p->retained || p->damage || p->deferred)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native private continuation requires suspended combat observers");
    for (const combat_record *record = p->records; record; record = record->next)
        if (record->bound || record->fuel_bound || record->prepared)
            return application_fail(error, QA_ERROR_ARGUMENT, "Native private continuation precedes combat primary preparation");
    return true;
}
void application_native_q2_combat_restore_abort(struct application_native_q2_combat_restore *token)
{
    if (!token) return;
    reactions_destroy(&token->reactions);
    application_q2_kex_damage_restore_abort(token->kex); free(token);
}
bool application_native_q2_combat_restore_prepare(struct application_native_q2 *engine, qa_bytes bytes,
    struct application_native_q2_combat_restore **out, qa_error *error)
{
    struct application_native_q2_combat *p = engine->source_combat;
    if (!out || *out) return application_fail(error,QA_ERROR_ARGUMENT,"Native combat restore token requires empty actual custody");
    if (!p) return !bytes.size || application_fail(error,QA_ERROR_FORMAT,"Native combat continuation has no actual source owner");
    if (p->active || p->current || p->incoming || p->retained || p->damage || p->deferred || p->reactions.first)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native deferred restore requires suspended damage observers");
    struct application_native_q2_combat_restore *token=calloc(1,sizeof(*token));
    if (!token) return application_fail(error,QA_ERROR_MEMORY,"Owning native combat restore preparation");
    qa_source_save_io io={0}; qa_bytes child={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && combat_fields(&io,p,&token->reactions,&child) &&
        qa_source_save_finish(&io,NULL) && application_q2_kex_damage_restore_prepare(p->kex,child,&token->kex,error);
    qa_source_save_dispose(&io);
    if (!ok) { application_native_q2_combat_restore_abort(token); return false; }
    *out=token; return true;
}
void application_native_q2_combat_restore_commit(struct application_native_q2 *engine,
    struct application_native_q2_combat_restore *token)
{
    if (!token) return;
    struct application_native_q2_combat *p = engine->source_combat;
    reactions_destroy(&p->reactions);
    p->reactions=token->reactions; token->reactions=(combat_reactions){0};
    application_q2_kex_damage_restore_commit(p->kex,token->kex); token->kex=NULL;
    free(token);
}
