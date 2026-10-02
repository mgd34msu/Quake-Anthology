#include "guest_native_q2_private.h"
#include "guest_native_q2_combat.h"
#include "guest_native_q2_combat_state.h"
#include "guest_native_q2_attack.h"
#include "guest_native_q2_combat_kex.h"
#include "qa/native_observe.h"
#include <math.h>

typedef struct combat_record {
    application_q2_combat_actor source;
    struct combat_record *next;
    struct application_native_q2_combat *owner;
    uint64_t serial;
    bool bound, fuel_bound, prepared;
} combat_record;
typedef struct combat_reaction {
    struct combat_reaction *next;
    struct application_native_q2_combat *owner;
    qa_native_entry_observer *binding;
    qa_native_address address;
    application_q2_call_operation operation;
} combat_reaction;
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
    combat_record *record;
    const qa_damage_request *request;
    qa_damage_observer *observer;
    qa_damage_result result;
    qa_native_write_observer *health_watch, *velocity_watch, *armor_watch;
    qa_native_write_observer *pain_watch, *die_watch;
    float health;
    qa_armor armor;
    qa_vec3 velocity;
    bool active, reacting, entering;
} combat_frame;
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
    combat_reaction *reactions;
    qa_native_entry_observer *damage;
    combat_armor_hook armor[2];
    combat_frame *current, *retained;
    combat_incoming *incoming;
    combat_deferred *deferred;
    struct application_q2_kex_damage *kex;
    bool active;
};
static qa_native_instance *native(struct application_native_q2_combat *p)
{ return qa_native_host_instance(p->engine->provider->state.native.host); }
static qa_combat *shared(struct application_native_q2_combat *p)
{ return p->engine->provider->application->combat; }
static combat_record *record_for(struct application_native_q2_combat *p, qa_actor_id actor)
{
    for (combat_record *r = p->records; r; r = r->next)
        if (qa_actor_id_equal(r->source.actor, actor)) return r;
    return NULL;
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
    qa_native_entity_table table;
    if (!qa_native_entity_table_get(native(p), &table, error)) return false;
    for (uint32_t i = 0; i < table.capacity; ++i) {
        qa_native_slot_binding binding;
        if (!qa_native_slot(native(p), i, &binding, error)) return false;
        if (binding.kind != QA_NATIVE_SLOT_FREE && qa_actor_id_equal(binding.actor, actor))
            return qa_native_entity_address(native(p), i, out, error);
    }
    return application_fail(error, QA_ERROR_UNSUPPORTED, "Native damage needs an original body projection for its foreign actor");
}
static bool health_write(void *opaque, float value, qa_error *error)
{
    combat_record *record = opaque; ++record->owner->engine->calls;
    bool ok = application_q2_combat_health_write(&record->source, value, error);
    if (ok) for (combat_frame *frame = record->owner->current; frame; frame = frame->previous)
        if (frame->active && qa_actor_id_equal(frame->record->source.actor, record->source.actor))
            frame->health = value;
    --record->owner->engine->calls; return ok;
}
static bool state_read(void *opaque, qa_combat_state *out, qa_error *error)
{
    combat_record *record = opaque; ++record->owner->engine->calls;
    bool ok = application_q2_combat_state_read(&record->source, out, error);
    --record->owner->engine->calls; return ok;
}
static bool armor_validate(void *opaque, const qa_armor *value, qa_error *error)
{
    combat_record *record = opaque; ++record->owner->engine->calls;
    bool ok = application_q2_combat_armor_validate(&record->source, value, error);
    --record->owner->engine->calls; return ok;
}
static bool armor_write(void *opaque, const qa_armor *value, qa_error *error)
{
    combat_record *record = opaque; ++record->owner->engine->calls;
    bool ok = application_q2_combat_armor_write(&record->source, value, error);
    if (ok) for (combat_frame *frame = record->owner->current; frame; frame = frame->previous)
        if (frame->active && qa_actor_id_equal(frame->record->source.actor, record->source.actor) &&
            !application_q2_combat_armor_read(&frame->record->source, &frame->armor, error)) { ok = false; break; }
    --record->owner->engine->calls; return ok;
}
static bool empty_armor(void *opaque, double points, qa_regular_armor *out, bool *selected, qa_error *error)
{
    combat_record *record = opaque; ++record->owner->engine->calls;
    bool ok = application_q2_combat_empty_armor(&record->source, points, out, selected, error);
    --record->owner->engine->calls; return ok;
}
static bool normalize_armor(void *opaque, const qa_armor *input, qa_armor *out, qa_error *error)
{
    combat_record *record = opaque; ++record->owner->engine->calls;
    bool ok = application_q2_combat_normalize_armor(&record->source, input, out, error);
    --record->owner->engine->calls; return ok;
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
        frame->health = state.health; frame->armor = state.armor;
    }
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
    float before = (float)source_before, after = (float)source_after;
    if ((double)before != source_before || (double)after != source_after || frame->health != before)
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
        before.x != frame->velocity.x || before.y != frame->velocity.y || before.z != frame->velocity.z)
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
    if (!frame->active || frame->reacting) return true;
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
    for (combat_reaction *hook = frame->owner->reactions; hook; hook = hook->next)
        if (hook->address == value && hook->operation == operation) return true;
    return application_fail(error, QA_ERROR_UNSUPPORTED, "Native reaction store needs a prequalified callback entry before source continuation");
}
static bool remove_watches(combat_frame *frame, qa_error *error)
{
    qa_native_write_observer **bindings[] = {&frame->health_watch, &frame->velocity_watch,
        &frame->armor_watch, &frame->pain_watch, &frame->die_watch};
    bool ok = true; qa_error first = {0};
    for (size_t i = 0; i < sizeof(bindings) / sizeof(*bindings); ++i) {
        qa_error current = {0};
        if (qa_native_unobserve_writes(*bindings[i], &current)) *bindings[i] = NULL;
        else if (ok) { ok = false; first = current; }
    }
    if (!ok && error) *error = first;
    return ok;
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
    if ((double)(float)fields[APPLICATION_Q2_AMOUNT].as.i32 != fields[APPLICATION_Q2_AMOUNT].as.i32)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native reaction damage exceeds exact shared result representation");
    qa_damage_result reaction = {.reaction = hook->operation == APPLICATION_Q2_DEATH ? QA_REACTION_DEATH : QA_REACTION_PAIN,
        .applied_damage = (float)fields[APPLICATION_Q2_AMOUNT].as.i32};
    const qa_damage_request *request = synchronous ? frame->request : deferred->request;
    float kick = request->knockback; qa_vec3 point = request->point;
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
    for (combat_reaction *hook = p->reactions; hook; hook = hook->next)
        if (hook->address == address) return hook->operation == operation ||
            application_fail(error, QA_ERROR_UNSUPPORTED, "Native reaction entry is shared by incompatible original call contracts");
    combat_reaction *hook = calloc(1, sizeof(*hook));
    if (!hook) return application_fail(error, QA_ERROR_MEMORY, "Retaining native reaction entry ownership");
    hook->owner = p; hook->address = address; hook->operation = operation;
    if (!qa_native_observe_entry(native(p), address, &p->profile.calls[operation].signature,
        reaction_entry, hook, &hook->binding, error)) { free(hook); return false; }
    hook->next = p->reactions; p->reactions = hook; return true;
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
    ++p->engine->calls;
    combat_frame *frame = calloc(1, sizeof(*frame));
    qa_native_address vectors = 0, pain, die, client; qa_combat_state state;
    bool ok = frame != NULL;
    if (!ok) application_fail(error, QA_ERROR_MEMORY, "Retaining native damage observation frame");
    if (ok) {
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
    qa_native_value fields[APPLICATION_Q2_FIELD_COUNT] = {0}, *arguments = NULL; uint8_t mod[3];
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
    if (ok) {
        arguments = calloc(call->signature.parameter_count, sizeof(*arguments));
        if (!arguments) ok = application_fail(error, QA_ERROR_MEMORY, "Lowering original native damage argument roster");
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
        if (removed) free(frame);
        else { frame->retained_next = p->retained; p->retained = frame; }
    }
    if (ok) ok = refresh_comparison_state(p, error);
    if (vectors) {
        qa_error cleanup = {0};
        if (!qa_native_free(native(p), vectors, &cleanup) && ok) { ok = false; if (error) *error = cleanup; }
    }
    free(arguments); --p->engine->calls; return ok;
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
    uint8_t damageable[4];
    qa_native_address target = fields[APPLICATION_Q2_TARGET].as.address;
    size_t extent = p->profile.kex ? 1 : 4;
    if (!target || target > UINT64_MAX - p->profile.damageable ||
        !qa_native_read(native(p), target + p->profile.damageable, damageable, extent, error)) return false;
    if (!(p->profile.kex ? damageable[0] : qa_load_u32le(damageable)))
        return qa_native_invoke_original(binding, arguments, count, result, error);
    ++p->engine->calls;
    qa_damage_request request = {0}; qa_actor_id attacker, inflictor;
    bool ok = actor_at(p, fields[APPLICATION_Q2_TARGET].as.address, &request.target, error) &&
        actor_at(p, fields[APPLICATION_Q2_ATTACKER].as.address, &attacker, error) &&
        actor_at(p, fields[APPLICATION_Q2_INFLICTOR].as.address, &inflictor, error);
    if (ok && application_provider_for(p->engine->provider->application, request.target,
        QA_ROLE_COMBAT, NULL) != p->engine->provider)
        ok = application_fail(error, QA_ERROR_UNSUPPORTED, "Incoming native damage requires a qualified selected combat projection");
    uint32_t flags = (uint32_t)fields[APPLICATION_Q2_FLAGS].as.i32;
    qa_damage_cause cause; qa_native_address client;
    if (ok) ok = application_q2_combat_cause_read(&p->profile, &fields[APPLICATION_Q2_CAUSE], flags, &cause, error) &&
        pointer_read(p, fields[APPLICATION_Q2_ATTACKER].as.address + p->profile.client_pointer, &client, error);
    if (ok) ok = application_native_q2_attack_read(p->engine, attacker, inflictor, request.target,
        client && weapon_cause((uint32_t)cause.source.q2.means_of_death & ~UINT32_C(0x08000000)), &request.attack, error);
    if (ok) {
        request.attack.cause = cause; request.radius = (flags & 1) != 0;
        request.amount = (float)fields[APPLICATION_Q2_AMOUNT].as.i32;
        request.knockback = (float)fields[APPLICATION_Q2_KNOCKBACK].as.i32;
        if ((double)request.amount != fields[APPLICATION_Q2_AMOUNT].as.i32 ||
            (double)request.knockback != fields[APPLICATION_Q2_KNOCKBACK].as.i32)
            ok = application_fail(error, QA_ERROR_UNSUPPORTED, "Incoming native damage exceeds exact shared argument representation");
    }
    if (ok) ok = vector_read(p, fields[APPLICATION_Q2_DIRECTION].as.address, &request.direction, error) &&
        vector_read(p, fields[APPLICATION_Q2_POINT].as.address, &request.point, error) &&
        vector_read(p, fields[APPLICATION_Q2_NORMAL].as.address, &request.normal, error);
    combat_incoming incoming = {.previous = p->incoming, .binding = binding,
        .arguments = arguments, .count = count, .target = request.target};
    if (ok) {
        combat_record *record = record_for(p, request.target);
        if (!record || !record->bound) ok = application_fail(error, QA_ERROR_NOT_FOUND, "Incoming native damage has no published source combat primary");
        else incoming.record = record;
    }
    if (ok) {
        p->incoming = &incoming; qa_damage_outcome outcome = {0};
        ok = qa_combat_run_source(shared(p), &request, incoming_damage, &incoming, &outcome, error);
        qa_damage_outcome_free(&outcome); p->incoming = incoming.previous;
        if (ok) ok = refresh_comparison_state(p, error);
    }
    if (ok) *result = (qa_native_value){.type = QA_NATIVE_VOID};
    --p->engine->calls; return ok;
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
    float pitch = body.angles.x * (3.14159265358979323846f / 180.f),
        yaw = body.angles.y * (3.14159265358979323846f / 180.f);
    qa_vec3 forward = qa_v3(cosf(pitch) * cosf(yaw), cosf(pitch) * sinf(yaw), -sinf(pitch));
    qa_armor_context context = {.q2_profile = true, .rerelease = p->profile.kex,
        .ctf = qa_json_string_equal(p->profile.document, qa_json_get(p->profile.document, p->profile.world, "game"), "ctf"),
        .alive = state.health > 0, .screen_facing_dot = qa_vec_dot(
            qa_vec_normalize(qa_vec_sub(geometry.point, body.origin)), forward)};
    qa_damage_flags flags = {.no_armor = (source_flags & 2) != 0,
        .no_power_armor = (source_flags & 0x100) != 0, .no_regular_armor = (source_flags & 0x80) != 0,
        .energy = (source_flags & 4) != 0, .regular_scale = 1};
    float saved;
    if (!qa_combat_absorb(shared(p), frame->request, channel, &geometry,
        (float)amount, flags, &context, &saved, NULL, error) ||
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
    return application_q2_combat_profile_read(engine, &p->profile, error) &&
        (!p->profile.kex || application_q2_kex_damage_prepare(&p->profile, &p->kex, error));
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
    p->active = true; return true;
}
bool application_native_q2_combat_admit(struct application_native_q2 *engine, uint32_t slot,
    qa_actor_id actor, bool admitting, qa_error *error)
{
    struct application_native_q2_combat *p = engine->source_combat;
    if (application_provider_for(engine->provider->application, actor, QA_ROLE_COMBAT, NULL) != engine->provider) return true;
    if (!p || !p->active) return application_fail(error, QA_ERROR_UNSUPPORTED, "Native primary combat has no active original producer");
    combat_record *record = record_for(p, actor);
    if (record && record->bound && !qa_combat_primary_current(shared(p), actor, record->serial, record))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native combat primary differs from its ordinary admitted source lease");
    if (record && record->prepared)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native combat restore claim requires its final shared primary validation");
    if (record && record->bound && record->fuel_bound) return true;
    if (!record) {
        record = calloc(1, sizeof(*record));
        if (!record) return application_fail(error, QA_ERROR_MEMORY, "Retaining native combat actor context");
        record->owner = p; record->next = p->records; p->records = record;
    }
    if (!record->bound && !application_q2_combat_actor_init(&p->profile, slot, actor, admitting, &record->source, error)) return false;
    qa_native_address client; qa_item_id cells = 0;
    if (!pointer_read(p, record->source.address + p->profile.client_pointer, &client, error)) return false;
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
    bool ok = record->bound || qa_combat_bind(shared(p), actor, &binding, true, error);
    record->source.admitting = false;
    if (!ok) return false;
    record->serial = qa_combat_storage_serial(shared(p), actor); record->bound = true;
    if (client) {
        if (!qa_combat_bind_power_inventory(shared(p), actor, engine->provider->application->inventory, cells, error)) return false;
    }
    record->fuel_bound = true;
    return true;
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
    if (!qa_combat_detach_primary(shared(p), actor, record->serial, record, error)) return false;
    record->bound = record->fuel_bound = record->prepared = false; return true;
}
void application_native_q2_combat_released(struct application_native_q2 *engine, qa_actor_id actor)
{
    struct application_native_q2_combat *p = engine->source_combat;
    combat_record *record = p ? record_for(p, actor) : NULL;
    if (record) record->bound = record->fuel_bound = record->prepared = false;
    if (p) application_q2_kex_damage_released(p->kex, actor);
}
bool application_native_q2_combat_suspend(struct application_native_q2 *engine, qa_error *error)
{
    struct application_native_q2_combat *p = engine->source_combat;
    if (!p) return true;
    if (p->current || p->incoming || p->deferred) return application_fail(error, QA_ERROR_ARGUMENT, "Native combat suspension requires drained source damage and reaction helpers");
    for (combat_record *record = p->records; record; record = record->next)
        if (!application_native_q2_combat_detach(engine, record->source.actor, error)) return false;
    p->active = false;
    if (!application_q2_kex_damage_suspend(p->kex, error)) return false;
    combat_frame **frame_cursor = &p->retained;
    while (*frame_cursor) {
        combat_frame *frame = *frame_cursor;
        if (!remove_watches(frame, error)) return false;
        *frame_cursor = frame->retained_next; free(frame);
    }
    combat_reaction **cursor = &p->reactions;
    while (*cursor) {
        combat_reaction *hook = *cursor;
        if (!qa_native_unobserve_entry(hook->binding, error)) return false;
        *cursor = hook->next; free(hook);
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
    while (p->records) { combat_record *record = p->records; p->records = record->next; free(record); }
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
    qa_native_entity_table table; uint32_t source_slot = UINT32_MAX;
    if (!qa_native_entity_table_get(native(p), &table, error)) return false;
    for (uint32_t slot = 0; slot < table.capacity; ++slot) {
        qa_native_slot_binding binding;
        if (!qa_native_slot(native(p), slot, &binding, error)) return false;
        if (binding.kind == QA_NATIVE_SLOT_FREE || !qa_actor_id_equal(binding.actor, actor)) continue;
        if (source_slot != UINT32_MAX) return application_fail(error, QA_ERROR_FORMAT, "Restored native combat actor has ambiguous source slots");
        source_slot = slot;
    }
    if (source_slot == UINT32_MAX) return application_fail(error, QA_ERROR_NOT_FOUND, "Restored native combat actor lacks its canonical source slot");
    combat_record *record = record_for(p, actor);
    if (record && record->bound && record->serial != saved_serial)
        return application_fail(error, QA_ERROR_ARGUMENT, "Restored native combat claim changes its retained source serial");
    if (!record) {
        record = calloc(1, sizeof(*record));
        if (!record) return application_fail(error, QA_ERROR_MEMORY, "Preparing native combat primary context");
        record->owner = p; record->next = p->records; p->records = record;
    }
    if (!application_q2_combat_actor_init(&p->profile, source_slot, actor, false, &record->source, error) ||
        !application_q2_combat_profile_resolve(&p->profile, error)) return false;
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
    p->deferred = &frame; ++engine->calls;
    bool ok = qa_combat_run_source_reaction(shared(p), request, result, execute_deferred, &frame, error);
    --engine->calls; p->deferred = frame.previous; return ok;
}
bool application_native_q2_combat_capture(struct application_native_q2 *engine, qa_buffer *out, qa_error *error)
{
    struct application_native_q2_combat *p = engine->source_combat;
    if (p && (p->current || p->incoming || p->retained || p->deferred))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native deferred capture requires drained damage frames");
    return application_q2_kex_damage_capture(p ? p->kex : NULL, out, error);
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
bool application_native_q2_combat_restore_prepare(struct application_native_q2 *engine, qa_bytes bytes,
    struct application_q2_kex_restore **out, qa_error *error)
{
    struct application_native_q2_combat *p = engine->source_combat;
    if (p && (p->active || p->current || p->incoming || p->retained || p->damage || p->deferred))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native deferred restore requires suspended damage observers");
    return application_q2_kex_damage_restore_prepare(p ? p->kex : NULL, bytes, out, error);
}
void application_native_q2_combat_restore_commit(struct application_native_q2 *engine,
    struct application_q2_kex_restore *token)
{
    struct application_native_q2_combat *p = engine->source_combat;
    application_q2_kex_damage_restore_commit(p ? p->kex : NULL, token);
}
void application_native_q2_combat_restore_abort(struct application_q2_kex_restore *token)
{ application_q2_kex_damage_restore_abort(token); }
