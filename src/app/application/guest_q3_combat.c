#include "guest_q3_combat.h"
#include "guest_q3_pickups.h"
#include "guest_q3_combat_source.h"
#include "guest_q3_damage_scope.h"
#include "guest_q3_private.h"

typedef struct combat_actor {
    struct combat_actor *next;
    application_q3_combat_actor source;
    struct application_q3_combat *owner;
    uint64_t serial;
    bool bound;
} combat_actor;
typedef struct incoming_damage {
    struct incoming_damage *previous;
    combat_actor *actor;
    const qa_damage_request *request;
    qa_damage_observer *observer;
    qa_damage_result *result;
    bool entered;
} incoming_damage;
struct application_q3_combat {
    q3g_role *role;
    const application_q3_combat_profile *profile;
    application_q3_damage_scopes *scopes;
    combat_actor *actors;
    incoming_damage *incoming;
    qa_qvm_binding bindings[3];
    uint64_t sequence;
    size_t count, calls;
};
static qa_combat *shared(application_q3_combat *owner)
{ return owner->role->engine->provider->application->combat; }
static combat_actor *actor_find(application_q3_combat *owner, qa_actor_id actor)
{
    for (combat_actor *at = owner->actors; at; at = at->next)
        if (qa_actor_id_equal(at->source.actor, actor)) return at;
    return NULL;
}
static bool read(void *context, qa_combat_state *out, qa_error *error)
{ return application_q3_combat_state_read(&((combat_actor *)context)->source, out, error); }
static bool health(void *context, float value, qa_error *error)
{ return application_q3_combat_health_write(&((combat_actor *)context)->source, value, error); }
static bool armor(void *context, const qa_armor *value, qa_error *error)
{ return application_q3_combat_armor_write(&((combat_actor *)context)->source, value, error); }
static bool armor_valid(void *context, const qa_armor *value, qa_error *error)
{ return application_q3_combat_armor_validate(&((combat_actor *)context)->source, value, error); }
static bool empty_armor(void *context, double value, qa_regular_armor *out, bool *present, qa_error *error)
{ return application_q3_combat_empty_armor(&((combat_actor *)context)->source, value, out, present, error); }
static bool invoke_damage(void *context, const int32_t *words, size_t count, qa_error *error)
{
    incoming_damage *incoming = context;
    application_q3_combat *owner = incoming->actor->owner;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    incoming->previous = owner->incoming; owner->incoming = incoming;
    int32_t result;
    bool ok = qa_qvm_invoke(owner->role->vm, d->callbacks.damage, words, count, &result, error);
    owner->incoming = incoming->previous;
    return ok && (incoming->entered || application_fail(error, QA_ERROR_FORMAT,
        "Actual Source damage did not enter its admitted callback"));
}
static bool source_damage(void *context, qa_combat *combat, const qa_damage_request *request,
    qa_damage_observer *observer, qa_damage_result *result, qa_error *error)
{
    combat_actor *actor = context;
    application_q3_combat *owner = actor->owner;
    if (combat != shared(owner) || owner->role->retired || !result ||
        !qa_actor_id_equal(actor->source.actor, request->target) ||
        !application_q3_combat_actor_current(&actor->source, error)) return false;
    *result = (qa_damage_result){0};
    qa_combat_state state;
    if (!read(actor, &state, error)) return false;
    if (!state.can_take_damage) return true;
    incoming_damage incoming = {.actor = actor, .request = request, .observer = observer, .result = result};
    q3g_role *previous = owner->role->engine->entered_role;
    owner->role->engine->entered_role = owner->role;
    ++owner->role->engine->calls; ++owner->calls;
    bool ok = application_q3_combat_source_damage(&actor->source, NULL, request, 0, invoke_damage, &incoming, error);
    --owner->calls; --owner->role->engine->calls; owner->role->engine->entered_role = previous;
    return ok;
}
static qa_combat_binding binding(combat_actor *actor)
{
    return (qa_combat_binding){.context = actor, .read = read, .write_health = health,
        .write_armor = armor, .validate_armor = armor_valid, .empty_regular_armor = empty_armor,
        .source_damage = source_damage, .source_armor_stages = {true, true},
        .has_primary_protection = {true, false},
        .primary_protection = {actor->owner->role->engine->provider->owner, 0}};
}
static bool pointer_actor(application_q3_combat *owner, int32_t pointer,
    combat_actor **out, qa_error *error)
{
    *out = NULL;
    if (!pointer) return true;
    qa_q3_host_game_data table;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    if (!qa_q3_host_game_data_read(owner->role->host, &table) || table.entity_stride != d->entity_stride ||
        table.client_stride != d->client_stride || pointer < 0 || (uint32_t)pointer < table.entities_address ||
        ((uint32_t)pointer - table.entities_address) % table.entity_stride ||
        ((uint32_t)pointer - table.entities_address) / table.entity_stride >= table.entity_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Damage pointer leaves its declared located Source entity");
    uint32_t slot = (uint32_t)(((uint32_t)pointer - table.entities_address) / table.entity_stride);
    if (slot >= 1022) return true;
    uint8_t inuse[4];
    if (!qa_qvm_read(owner->role->vm, (uint32_t)pointer + d->fields.inuse, inuse, sizeof(inuse), error)) return false;
    if (!qa_load_i32le(inuse)) return true;
    qa_actor_id actor;
    if (!qa_q3_host_actor(owner->role->host, slot, true, &actor, error)) return false;
    if (!actor.registry) return true;
    combat_actor *at = actor_find(owner, actor);
    if ((!at || !at->bound) && !application_q3_combat_admit(owner, actor, error)) return false;
    at = actor_find(owner, actor);
    if (!at || !at->bound || !application_q3_combat_actor_current(&at->source, error))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Damage target has no admitted original combat primary");
    bool live;
    if (!application_q3_combat_actor_live(&at->source, &live, error)) return false;
    if (live) *out = at;
    return true;
}
static bool vector(application_q3_combat *owner, int32_t pointer, qa_vec3 *out, qa_error *error)
{
    *out = (qa_vec3){0};
    if (!pointer) return true;
    uint8_t bytes[12];
    if (pointer < 0 || !qa_qvm_read(owner->role->vm, (uint32_t)pointer, bytes, sizeof(bytes), error)) return false;
    *out = qa_v3(qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8));
    return qa_vec_finite(*out) || application_fail(error, QA_ERROR_ARGUMENT, "Original damage has nonfinite source geometry");
}
typedef struct entered_damage {
    application_q3_combat *owner;
    combat_actor *actor;
    const qa_qvm_call *call;
    uint32_t flags;
    const qa_damage_request *effective;
    qa_damage_observer *observer;
    qa_damage_result *result;
    qa_damage_request original;
} entered_damage;
static bool same_vector(qa_vec3 a, qa_vec3 b)
{ return a.x == b.x && a.y == b.y && a.z == b.z; }
static bool proceed_damage(void *context, const int32_t *words, size_t count, qa_error *error)
{
    entered_damage *entered = context;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(entered->owner->profile);
    if (count != d->damage_call.count)
        return application_fail(error, QA_ERROR_FORMAT, "Lowered Source damage omits its declared argument words");
    int32_t prior[Q3_DAMAGE_WORDS];
    size_t saved = 0;
    bool ok = true;
    for (size_t i = 0; ok && i < Q3_DAMAGE_WORDS; ++i) {
        uint32_t position = d->damage_call.positions[i];
        ok = qa_qvm_call_argument(entered->call, position, prior + i, error);
        bool preserve = (i == Q3_DAMAGE_DIRECTION && same_vector(entered->effective->direction, entered->original.direction)) ||
            (i == Q3_DAMAGE_POINT && same_vector(entered->effective->point, entered->original.point)) ||
            (i == Q3_DAMAGE_ATTACKER && qa_actor_id_equal(entered->effective->attack.attacker, entered->original.attack.attacker)) ||
            (i == Q3_DAMAGE_INFLICTOR && qa_actor_id_equal(entered->effective->attack.inflictor, entered->original.attack.inflictor));
        if (ok) { ++saved; if (!preserve) ok = qa_qvm_call_set_argument(entered->call, position, words[position], error); }
    }
    if (ok) ok = application_q3_damage_scope_run(entered->owner->scopes, entered->call,
        &entered->actor->source, entered->effective, entered->observer, entered->result, error);
    qa_error first = error ? *error : (qa_error){0};
    for (size_t i = 0; i < saved; ++i) {
        qa_error restore = {0};
        if (!qa_qvm_call_set_argument(entered->call, d->damage_call.positions[i], prior[i], &restore)) {
            if (ok) first = restore;
            ok = false;
        }
    }
    if (!ok && error) *error = first;
    return ok;
}
static bool entered_source(void *context, qa_combat *combat, const qa_damage_request *request,
    qa_damage_observer *observer, qa_damage_result *result, qa_error *error)
{
    entered_damage *entered = context;
    if (combat != shared(entered->owner) || !qa_actor_id_equal(request->target, entered->actor->source.actor))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Reached Q3 damage cannot change its actual source target");
    entered->effective = request; entered->observer = observer; entered->result = result;
    return application_q3_combat_source_damage(&entered->actor->source, entered->call, request,
        entered->flags, proceed_damage, entered, error);
}
static bool damage(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_combat *owner = context;
    incoming_damage *incoming = owner->incoming;
    if (incoming && !incoming->entered) {
        incoming->entered = true;
        return application_q3_damage_scope_run(owner->scopes, call, &incoming->actor->source,
            incoming->request, incoming->observer, incoming->result, error) && (*result = 0, true);
    }
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    int32_t words[Q3_DAMAGE_WORDS];
    for (size_t i = 0; i < Q3_DAMAGE_WORDS; ++i)
        if (!qa_qvm_call_argument(call, d->damage_call.positions[i], words + i, error)) return false;
    combat_actor *target;
    if (!pointer_actor(owner, words[Q3_DAMAGE_TARGET], &target, error)) return false;
    if (!target) return qa_qvm_proceed(call, result, error);
    qa_damage_request request = {.target = target->source.actor, .amount = (float)words[Q3_DAMAGE_AMOUNT],
        .knockback = (words[Q3_DAMAGE_DIRECTION] && !((uint32_t)words[Q3_DAMAGE_FLAGS] & d->damage_flags.no_knockback)) ?
            (float)words[Q3_DAMAGE_AMOUNT] : 0,
        .radius = ((uint32_t)words[Q3_DAMAGE_FLAGS] & d->damage_flags.radius) != 0};
    if ((double)request.amount != words[Q3_DAMAGE_AMOUNT])
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Reached source damage exceeds exact shared amount representation");
    if (!vector(owner, words[Q3_DAMAGE_DIRECTION], &request.direction, error) ||
        !vector(owner, words[Q3_DAMAGE_POINT], &request.point, error)) return false;
    combat_actor *attacker, *inflictor;
    if (!pointer_actor(owner, words[Q3_DAMAGE_ATTACKER], &attacker, error) ||
        !pointer_actor(owner, words[Q3_DAMAGE_INFLICTOR], &inflictor, error)) return false;
    if (attacker) request.attack.attacker = attacker->source.actor;
    if (inflictor) request.attack.inflictor = inflictor->source.actor;
    qa_application *app = owner->role->engine->provider->application;
    qa_clock_state clock;
    if (!qa_session_clock(app->session, owner->role->engine->provider->owner, &clock) ||
        clock.frame.provider != owner->role->engine->provider->owner || clock.frame.kind != QA_CLOCK_Q3)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Reached Source damage has no actual Q3 clock");
    request.attack.time_ns = clock.frame.time_ns;
    application_provider *combat = application_provider_for(app, request.target, QA_ROLE_COMBAT, "");
    application_provider *movement = application_provider_for(app, request.target, QA_ROLE_MOVEMENT, "");
    application_provider *inventory = application_provider_for(app, request.attack.attacker, QA_ROLE_INVENTORY, "");
    request.attack.combat_provider = combat ? combat->owner : 0;
    request.attack.weapon_provider = owner->role->engine->provider->owner;
    request.attack.movement_provider = movement ? movement->owner : 0;
    request.attack.inventory_provider = inventory ? inventory->owner : 0;
    request.attack.cause = (qa_damage_cause){.kind = QA_CAUSE_Q3,
        .source.q3 = {words[Q3_DAMAGE_METHOD], application_q3_combat_canonical_flags(d, (uint32_t)words[Q3_DAMAGE_FLAGS])}};
    if (!qa_attack_next(&owner->sequence, &request.attack, error)) return false;
    entered_damage entered = {.owner = owner, .actor = target, .call = call,
        .flags = (uint32_t)words[Q3_DAMAGE_FLAGS], .original = request};
    qa_damage_outcome outcome = {0};
    ++owner->calls;
    bool ok = qa_combat_run_source(shared(owner), &request, entered_source, &entered, &outcome, error);
    --owner->calls; qa_damage_outcome_free(&outcome);
    if (ok) *result = 0;
    return ok;
}
static bool freed(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_combat *owner = context;
    int32_t pointer;
    if (!qa_qvm_call_argument(call, 0, &pointer, error)) return false;
    qa_actor_id released = {0};
    for (combat_actor *at = owner->actors; at; at = at->next)
        if (at->source.entity == (uint32_t)pointer && at->bound) { released = at->source.actor; break; }
    if (!qa_qvm_proceed(call, result, error)) return false;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(owner->role->engine->provider->application->session), released);
    combat_actor *actor = actor_find(owner, released);
    if (record && actor && record->owner == owner->role->engine->provider->owner) {
        bool live;
        if (!application_q3_combat_actor_live(&actor->source, &live, error) ||
            (!live && !qa_session_release(owner->role->engine->provider->application->session, released, error))) return false;
    }
    return application_q3_pickups_after_free(owner->role->pickups, call, pointer, error);
}
static bool armor_stage(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_combat *owner = context;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    int32_t target, amount, source_flags;
    if (!qa_qvm_call_argument(call, d->armor.call.positions[Q3_ARMOR_TARGET], &target, error) ||
        !qa_qvm_call_argument(call, d->armor.call.positions[Q3_ARMOR_AMOUNT], &amount, error) ||
        !qa_qvm_call_argument(call, d->armor.call.positions[Q3_ARMOR_FLAGS], &source_flags, error)) return false;
    application_q3_damage_frame *frame = application_q3_damage_scope_current(owner->scopes, (uint32_t)target);
    if (!frame) return qa_qvm_proceed(call, result, error);
    const qa_damage_request *request = application_q3_damage_scope_request(frame);
    qa_actor_owner regular = 0, power = 0;
    uint64_t serial;
    bool foreign_regular = qa_combat_protection_owner(shared(owner), request->target, QA_PROTECTION_REGULAR, &regular, &serial) &&
        regular != owner->role->engine->provider->owner;
    bool foreign_power = qa_combat_protection_owner(shared(owner), request->target, QA_PROTECTION_POWERED, &power, &serial);
    if (!foreign_regular && !foreign_power) return qa_qvm_proceed(call, result, error);
    qa_damage_flags flags = qa_attack_flags(&request->attack);
    flags.no_armor = ((uint32_t)source_flags & d->damage_flags.no_armor) != 0;
    if ((double)(float)amount != amount)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Reached source armor damage exceeds exact shared representation");
    const qa_qvm_call *damage_call = application_q3_damage_scope_call(frame);
    int32_t direction, point;
    qa_damage_geometry geometry = {.normal = request->normal};
    if (!qa_qvm_call_argument(damage_call, d->damage_call.positions[Q3_DAMAGE_DIRECTION], &direction, error) ||
        !qa_qvm_call_argument(damage_call, d->damage_call.positions[Q3_DAMAGE_POINT], &point, error) ||
        !vector(owner, direction, &geometry.direction, error) ||
        !vector(owner, point, &geometry.point, error)) return false;
    qa_armor_context victim = {0};
    float saved = 0;
    if (foreign_power && !qa_combat_absorb(shared(owner), request, QA_PROTECTION_POWERED, &geometry,
        (float)amount, flags, &victim, &saved, NULL, error)) return false;
    const application_q3_combat_actor *source = application_q3_damage_scope_actor(frame);
    bool live = false;
    if (qa_actors_get(qa_session_actors(owner->role->engine->provider->application->session), source->actor) &&
        !application_q3_combat_actor_live(source, &live, error)) return false;
    if (!live) { *result = 0; return application_q3_damage_scope_cancel(frame, error); }
    if (!isfinite(saved) || saved < 0 || saved > fmaxf(0, (float)amount))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected power armor exceeds reached source damage");
    int32_t power_saved = (int32_t)trunc((double)saved), remaining = amount - power_saved;
    if (!qa_qvm_call_set_argument(call, d->armor.call.positions[Q3_ARMOR_AMOUNT], remaining, error)) return false;
    bool ok;
    if (foreign_regular) {
        float regular_saved;
        ok = qa_qvm_call_argument(call, d->armor.call.positions[Q3_ARMOR_FLAGS], &source_flags, error) &&
            qa_qvm_call_argument(damage_call, d->damage_call.positions[Q3_DAMAGE_DIRECTION], &direction, error) &&
            qa_qvm_call_argument(damage_call, d->damage_call.positions[Q3_DAMAGE_POINT], &point, error) &&
            vector(owner, direction, &geometry.direction, error) && vector(owner, point, &geometry.point, error);
        flags.no_armor = ((uint32_t)source_flags & d->damage_flags.no_armor) != 0;
        if (ok) ok = qa_combat_absorb(shared(owner), request, QA_PROTECTION_REGULAR, &geometry,
            (float)remaining, flags, &victim, &regular_saved, NULL, error);
        if (ok && (!isfinite(regular_saved) || regular_saved < 0 || regular_saved > fmaxf(0, (float)remaining)))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Selected regular armor exceeds reached source damage");
        if (ok) *result = power_saved + (int32_t)trunc((double)regular_saved);
    } else {
        ok = qa_qvm_proceed(call, result, error);
        if (ok) *result += power_saved;
    }
    live = false;
    if (ok && qa_actors_get(qa_session_actors(owner->role->engine->provider->application->session), source->actor))
        ok = application_q3_combat_actor_live(source, &live, error);
    if (ok && !live) { *result = 0; ok = application_q3_damage_scope_cancel(frame, error); }
    qa_error restore = {0};
    bool restored = qa_qvm_call_set_argument(call, d->armor.call.positions[Q3_ARMOR_AMOUNT], amount, &restore);
    if (!restored && ok && error) *error = restore;
    return ok && restored;
}
bool application_q3_combat_create(q3g_role *role, const application_q3_combat_profile *profile,
    application_q3_combat **out, qa_error *error)
{
    if (!role || !profile || !out || *out || !role->vm || !role->host || role->kind != QA_QVM_GAME ||
        role->image != application_q3_combat_profile_image(profile) || role->abi != application_q3_combat_profile_abi(profile))
        return application_fail(error, QA_ERROR_ARGUMENT, "Combat hooks require their genuine original GAME constructor");
    application_q3_combat *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Retaining original combat callback owner");
    owner->role = role; owner->profile = profile; *out = owner;
    if (!application_q3_damage_scopes_create(role, profile, &owner->scopes, error)) return false;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(profile);
    uint32_t entries[] = {d->callbacks.damage, d->callbacks.free, d->armor.check};
    qa_qvm_function_hook hooks[] = {damage, freed, armor_stage};
    for (size_t i = 0; i < 3; ++i) {
        if (!qa_qvm_bind_function(role->vm, entries[i], true, hooks[i], owner, &owner->bindings[i], error)) return false;
        ++owner->count;
    }
    return true;
}
bool application_q3_combat_idle(const application_q3_combat *owner)
{ return !owner || (!owner->calls && !owner->incoming && application_q3_damage_scopes_idle(owner->scopes)); }
bool application_q3_combat_sequence_read(const application_q3_combat *owner,
    uint64_t *out, qa_error *error)
{
    if (!out || !application_q3_combat_idle(owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source attack sequence requires its returned combat owner");
    *out = owner ? owner->sequence : 0;
    return true;
}
void application_q3_combat_sequence_adopt(application_q3_combat *owner, uint64_t sequence)
{ if (owner) owner->sequence = sequence; }
bool application_q3_combat_admit(application_q3_combat *owner, qa_actor_id id, qa_error *error)
{
    if (!owner || owner->role->retired || !owner->role->host || !owner->role->vm)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original combat admission requires its retained live Source owner");
    combat_actor *actor = actor_find(owner, id);
    if (actor && actor->bound) return application_q3_combat_actor_current(&actor->source, error);
    if (!actor) {
        actor = calloc(1, sizeof(*actor));
        if (!actor) return application_fail(error, QA_ERROR_MEMORY, "Retaining original combat actor binding");
        actor->owner = owner; actor->source.actor = id; actor->next = owner->actors; owner->actors = actor;
    }
    if (!application_q3_combat_actor_read(owner->role, owner->profile, id, &actor->source, error)) return false;
    qa_combat_binding value = binding(actor);
    if (!qa_combat_bind(shared(owner), id, &value, true, error)) return false;
    actor->serial = qa_combat_storage_serial(shared(owner), id); actor->bound = true; return true;
}
bool application_q3_combat_binding(application_q3_combat *owner, qa_actor_id id, uint64_t serial,
    qa_combat_binding *out, qa_error *error)
{
    if (!owner || !out || !serial || !application_q3_combat_idle(owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Restored combat requires its retained source identity");
    combat_actor *actor = actor_find(owner, id);
    if (!actor) {
        actor = calloc(1, sizeof(*actor));
        if (!actor) return application_fail(error, QA_ERROR_MEMORY, "Retaining restored original combat binding");
        actor->owner = owner; actor->next = owner->actors; owner->actors = actor;
    }
    if (!application_q3_combat_actor_read(owner->role, owner->profile, id, &actor->source, error)) return false;
    if (actor->bound && actor->serial != serial)
        return application_fail(error, QA_ERROR_FORMAT, "Restored combat changed its primary storage serial");
    actor->serial = serial; actor->bound = true; *out = binding(actor); return true;
}
bool application_q3_combat_actor_released(application_q3_combat *owner, qa_actor_record released, qa_error *error)
{
    (void)error;
    combat_actor *actor = owner ? actor_find(owner, released.id) : NULL;
    if (actor) actor->bound = false;
    return true;
}
size_t application_q3_combat_descriptor_count(const application_q3_combat *owner)
{ return owner ? 3 : 0; }
bool application_q3_combat_descriptors(const application_q3_combat *owner,
    qa_qvm_saved_function *out, size_t count, qa_error *error)
{
    if (count != application_q3_combat_descriptor_count(owner) || (count && !out) ||
        !application_q3_combat_idle(owner) || (owner && owner->count != count))
        return application_fail(error, QA_ERROR_FORMAT, "Original combat inventory differs from its actual constructor");
    if (!owner) return true;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    uint32_t entries[] = {d->callbacks.damage, d->callbacks.free, d->armor.check};
    qa_qvm_function_hook hooks[] = {damage, freed, armor_stage};
    for (size_t i = 0; i < count; ++i)
        out[i] = (qa_qvm_saved_function){owner->bindings[i], entries[i], true, hooks[i], (void *)owner};
    return true;
}
void application_q3_combat_adopt(application_q3_combat *owner, const qa_qvm_binding *bindings)
{ if (owner) memcpy(owner->bindings, bindings, sizeof(owner->bindings)); }
bool application_q3_combat_destroy(application_q3_combat **pointer, qa_error *error)
{
    if (!pointer || !*pointer) return true;
    application_q3_combat *owner = *pointer;
    if (owner->calls || owner->incoming ||
        (owner->scopes && !application_q3_damage_scopes_quiescent(owner->scopes)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original combat retains a source action or refused observer cleanup");
    for (combat_actor *actor = owner->actors; actor; actor = actor->next) {
        if (actor->bound && qa_actors_get(qa_session_actors(owner->role->engine->provider->application->session), actor->source.actor) &&
            qa_combat_primary_current(shared(owner), actor->source.actor, actor->serial, actor)) {
            if (!qa_combat_detach_primary(shared(owner), actor->source.actor, actor->serial, actor, error)) return false;
        }
        actor->bound = false;
    }
    for (size_t i = 0; i < owner->count; ++i) {
        if (owner->bindings[i] && !qa_qvm_unbind(owner->role->vm, owner->bindings[i], error)) return false;
        owner->bindings[i] = 0;
    }
    if (!application_q3_damage_scopes_destroy(&owner->scopes, error)) return false;
    while (owner->actors) { combat_actor *next = owner->actors->next; free(owner->actors); owner->actors = next; }
    free(owner); *pointer = NULL; return true;
}
