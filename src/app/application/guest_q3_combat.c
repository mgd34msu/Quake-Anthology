#include "guest_q3_combat.h"
#include "guest_q3_pickups.h"
#include "guest_q3_combat_source.h"
#include "guest_q3_damage_scope.h"
#include "guest_q3_private.h"
#include "control_frame.h"
#include "source_projection.h"
#include "qa/pool.h"
#include "qa/text.h"
#include "qa/native_observe.h"

typedef enum native_combat_kind {
    NATIVE_COMBAT_REACTION, NATIVE_COMBAT_DAMAGE, NATIVE_COMBAT_FREE, NATIVE_COMBAT_ARMOR
} native_combat_kind;
typedef struct native_reaction_binding {
    struct application_q3_combat *owner;
    uint64_t address;
    qa_native_entry_observer *binding;
    native_combat_kind kind;
    application_q3_reaction_call reaction;
} native_reaction_binding;

typedef struct combat_actor {
    struct combat_actor *next,*previous,*retired_next;
    application_q3_combat_actor source;
    struct application_q3_combat *owner;
    size_t storage_slot;
    uint64_t serial;
    unsigned calls;
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
    qa_actor_index actor_index;
    qa_unified_frame_lease *actor_storage;
    qa_pool actor_pool;
    qa_id_index reactions;
    size_t actor_chunk;
    combat_actor *actors;
    combat_actor *retired;
    incoming_damage *incoming;
    qa_qvm_binding bindings[3];
    native_reaction_binding *native_bindings;
    size_t native_count, native_capacity;
    uint64_t sequence;
    size_t count, calls, source_entries;
    bool direct_bound;
};
static qa_combat *shared(application_q3_combat *owner)
{ return owner->role->engine->provider->application->combat; }
static combat_actor *actor_find(application_q3_combat *owner, qa_actor_id actor)
{
    size_t slot;
    return qa_actor_index_find(&owner->actor_index, actor, &slot) ? (combat_actor *)(uintptr_t)slot : NULL;
}
static void actors_collect(application_q3_combat *owner)
{
    combat_actor **link=&owner->retired;
    while(*link){
        combat_actor *actor=*link;
        if(actor->calls){link=&actor->retired_next;continue;}
        *link=actor->retired_next;
        if(actor->previous)actor->previous->next=actor->next;else owner->actors=actor->next;
        if(actor->next)actor->next->previous=actor->previous;
        qa_pool_release(&owner->actor_pool,actor->storage_slot);
    }
}
static combat_actor *actor_create(application_q3_combat *owner, qa_actor_id id, qa_error *error)
{
    actors_collect(owner);
    if(owner->actor_pool.head==SIZE_MAX&&!qa_pool_extend(&owner->actor_pool,
        qa_unified_frame_lease_arena(owner->actor_storage),owner->actor_chunk,error))return NULL;
    size_t slot;
    combat_actor *actor=qa_pool_take(&owner->actor_pool,&slot);
    if(!actor)return NULL;
    *actor = (combat_actor){.owner = owner, .storage_slot = slot, .source.actor = id, .next = owner->actors};
    if(actor->next)actor->next->previous=actor;
    owner->actors=actor;
    qa_actor_index_put(&owner->actor_index, id, (size_t)(uintptr_t)actor);
    return actor;
}
static bool read(void *context, qa_combat_state *out, qa_error *error)
{ return application_q3_combat_state_read(&((combat_actor *)context)->source, out, error); }
static bool traits_write(void *context, const qa_combat_state *traits, qa_error *error)
{ return application_q3_combat_traits_write(&((combat_actor *)context)->source, traits, error); }
static bool health_read(void *context, double *out, qa_error *error)
{
    combat_actor *actor = context;
    application_q3_combat *owner = actor->owner;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    ++actor->calls; ++owner->calls; ++owner->role->engine->calls;
    int32_t value;
    bool ok = application_q3_combat_word_read(&actor->source,
        actor->source.entity + d->fields.health, &value, error);
    if (ok) *out = value;
    --actor->calls; --owner->calls; --owner->role->engine->calls;
    actors_collect(owner);
    return ok;
}
static bool health(void *context, double value, qa_error *error)
{
    combat_actor *actor = context;
    application_q3_combat *owner = actor->owner;
    ++actor->calls; ++owner->calls; ++owner->role->engine->calls;
    bool ok = application_q3_combat_health_write(&actor->source, value, error);
    if (ok) application_q3_damage_scopes_health_projected(owner->scopes, actor->source.actor, value);
    --actor->calls; --owner->calls; --owner->role->engine->calls;
    actors_collect(owner);
    return ok;
}
static bool armor(void *context, const qa_armor *value, qa_error *error)
{ return application_q3_combat_armor_write(&((combat_actor *)context)->source, value, error); }
static bool armor_valid(void *context, const qa_armor *value, qa_error *error)
{ return application_q3_combat_armor_validate(&((combat_actor *)context)->source, value, error); }
static bool empty_armor(void *context, double value, qa_regular_armor *out, bool *present, qa_error *error)
{ return application_q3_combat_empty_armor(&((combat_actor *)context)->source, value, out, present, error); }
static bool invoke_damage(void *context, const qa_native_value *values, size_t count, qa_error *error)
{
    incoming_damage *incoming = context;
    application_q3_combat *owner = incoming->actor->owner;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    incoming->previous = owner->incoming; owner->incoming = incoming;
    qa_native_value result = {.type = QA_NATIVE_VOID};
    bool ok = application_q3_combat_source_call(owner->role, NULL,
        d->callbacks.damage, values, count, &result, error);
    owner->incoming = incoming->previous;
    return ok && (incoming->entered || application_fail(error, QA_ERROR_FORMAT,
        "Actual Source damage did not enter its admitted callback"));
}
static bool native_damage_ready(combat_actor *actor, qa_error *error)
{
    application_q3_combat *owner = actor->owner;
    if (owner->role->vm) return true;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    if (!d->callbacks.has_damage || !d->damage_call.count || !d->has_damage_flags)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native damage requires its declared callback, arguments and flags");
    const uint32_t offsets[] = {d->reactions.pain, d->reactions.die};
    const application_q3_reaction_call *calls[] = {&d->reactions.pain_call, &d->reactions.die_call};
    for (size_t i = 0; i < 2; ++i) if (!calls[i]->arguments_present) {
        uint64_t pointer;
        if (!qa_q3_host_source_pointer(owner->role->host, actor->source.entity + offsets[i], &pointer, error)) return false;
        if (pointer) return application_fail(error, QA_ERROR_UNSUPPORTED, "Native damage reaction has no declared argument layout");
    }
    qa_actor_owner regular, power; uint64_t serial;
    bool foreign = (qa_combat_protection_owner(shared(owner), actor->source.actor,
        QA_PROTECTION_REGULAR, &regular, &serial) && regular != owner->role->engine->provider->owner) ||
        qa_combat_protection_owner(shared(owner), actor->source.actor, QA_PROTECTION_POWERED, &power, &serial);
    return !foreign || (d->armor.has_check && d->armor.call.count) ||
        application_fail(error, QA_ERROR_UNSUPPORTED, "Native selected armor requires its declared original armor callback");
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
    if (!owner->scopes)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Native source damage requires its original damage callback scope");
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    if (!d->callbacks.has_damage || !d->damage_call.count)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Source damage requires its declared callback, arguments and flags");
    if (!native_damage_ready(actor, error)) return false;
    incoming_damage incoming = {.actor = actor, .request = request, .observer = observer, .result = result};
    q3g_role *previous = owner->role->engine->entered_role;
    owner->role->engine->entered_role = owner->role;
    ++owner->role->engine->calls; ++owner->calls; ++actor->calls;
    bool entered = application_q3_combat_enter(owner, error);
    bool ok = entered && application_q3_combat_source_damage(&actor->source, NULL, request, 0, invoke_damage, &incoming, error);
    qa_error cleanup = {0};
    if (entered && !application_q3_combat_leave(owner, &cleanup)) {
        if (ok && error) *error = cleanup;
        ok = false;
    }
    --actor->calls; --owner->calls; --owner->role->engine->calls; owner->role->engine->entered_role = previous;
    actors_collect(owner);
    return ok;
}

typedef struct character_reaction {
    application_q3_combat *owner;
    application_q3_combat_actor source;
    const application_q3_reaction_call *arguments;
    bool death;
} character_reaction;

static bool reaction_actor_live(character_reaction *reaction, bool *live, qa_error *error)
{
    q3g_role *role = reaction->owner->role;
    qa_application *app = role->engine->provider->application;
    uint32_t slot;
    *live = false;
    if (!qa_actors_get(qa_session_actors(app->session), reaction->source.actor) ||
        !qa_q3_host_actor_slot(role->host, reaction->source.actor, &slot, NULL) ||
        slot != reaction->source.slot) return true;
    return application_q3_combat_actor_live(&reaction->source, live, error);
}

static bool reaction_project(character_reaction *reaction, qa_actor_id actor, qa_error *error)
{
    q3g_role *role = reaction->owner->role;
    qa_application *app = role->engine->provider->application;
    if (!actor.registry || !qa_actors_get(qa_session_actors(app->session), actor)) return true;
    const qa_actor_player *player = qa_actors_player(qa_session_actors(app->session), actor);
    uint32_t slot;
    if (player && player->present && !qa_q3_host_actor_slot(role->host, actor, &slot, NULL))
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Selected Q3 reaction requires the attacker's admitted source client");
    return q3g_project_actor(role, NULL, actor, &slot, error);
}

static bool reaction_world(character_reaction *reaction, uint64_t *pointer, qa_error *error)
{
    const application_q3_combat_definition *d =
        application_q3_combat_profile_definition(reaction->owner->profile);
    if (d->reactions.world_slot == UINT32_MAX)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Selected Q3 reaction has no declared source world entity");
    uint64_t displacement = (uint64_t)d->reactions.world_slot * d->entity_stride;
    if (displacement > UINT64_MAX - reaction->source.table.entities_address)
        return application_fail(error, QA_ERROR_FORMAT, "Source world entity exceeds its address space");
    uint64_t address = reaction->source.table.entities_address + displacement;
    q3g_role *role = reaction->owner->role;
    if (role->native) {
        if (!qa_native_range_check(qa_native_host_instance(role->native), address,
                d->entity_stride, QA_NATIVE_MEMORY_READ, error)) return false;
    } else {
        qa_bytes span;
        if (address > UINT32_MAX)
            return application_fail(error, QA_ERROR_FORMAT, "Source world entity exceeds its QVM address word");
        if (!qa_qvm_span(role->vm, (int32_t)(uint32_t)address, 0, d->entity_stride, &span, error)) return false;
    }
    *pointer = address;
    return true;
}

static bool invoke_reaction(void *context, const qa_native_value *damage_values, size_t count, qa_error *error)
{
    character_reaction *reaction = context;
    application_q3_combat *owner = reaction->owner;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    const application_q3_reaction_call *call = reaction->arguments;
    (void)count;
    bool live;
    if (!reaction_actor_live(reaction, &live, error)) return false;
    if (!live) return true;
    uint64_t attacker = damage_values[Q3_DAMAGE_ATTACKER].as.address,
        inflictor = damage_values[Q3_DAMAGE_INFLICTOR].as.address;
    if ((!attacker && !reaction_world(reaction, &attacker, error)) ||
        (reaction->death && !inflictor && !reaction_world(reaction, &inflictor, error))) return false;
    if (reaction->death) {
        int32_t flags;
        if (reaction->source.player &&
            (!application_q3_combat_word_read(&reaction->source,
                reaction->source.entity + d->reactions.flags, &flags, error) ||
             !application_q3_combat_word_write(&reaction->source,
                reaction->source.entity + d->reactions.flags,
                (int32_t)((uint32_t)flags | d->state.no_knockback), error))) return false;
        if (!qa_q3_host_source_pointer_write(owner->role->host,
                reaction->source.entity + d->reactions.enemy, attacker, error)) return false;
    }
    uint64_t callback;
    if (!qa_q3_host_source_pointer(owner->role->host, reaction->source.entity +
        (reaction->death ? d->reactions.die : d->reactions.pain), &callback, error)) return false;
    if (!callback) return !reaction->death || application_fail(error, QA_ERROR_UNSUPPORTED,
        "Selected Q3 character has no source death callback");
    qa_native_value values[APPLICATION_Q3_COMBAT_ARGUMENTS] = {0};
    values[call->target] = damage_values[Q3_DAMAGE_TARGET];
    values[call->attacker] = (qa_native_value){.type = QA_NATIVE_ADDRESS, .as.address = attacker};
    values[call->amount] = damage_values[Q3_DAMAGE_AMOUNT];
    if (call->inflictor != UINT32_MAX)
        values[call->inflictor] = (qa_native_value){.type = QA_NATIVE_ADDRESS, .as.address = inflictor};
    if (call->method != UINT32_MAX)
        values[call->method] = damage_values[Q3_DAMAGE_METHOD];
    qa_native_value result = {.type = QA_NATIVE_VOID};
    return qa_q3_host_source_call(owner->role->host, NULL, callback, values, call->count, &result, error);
}

bool application_q3_combat_reaction(application_q3_combat *owner,
    const qa_damage_outcome *outcome, qa_error *error)
{
    if (!outcome) return application_fail(error, QA_ERROR_ARGUMENT, "Source reaction requires its applied outcome");
    if (outcome->stale || outcome->result.reaction == QA_REACTION_NONE) return true;
    if (!owner || owner->role->retired || (!owner->role->vm && !owner->role->native))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Selected Q3 character has no retained source reaction owner");
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    character_reaction reaction = {.owner = owner,
        .death = outcome->result.reaction == QA_REACTION_DEATH};
    reaction.arguments = reaction.death ? &d->reactions.die_call : &d->reactions.pain_call;
    if (!reaction.arguments->arguments_present || (reaction.death && d->reactions.enemy == UINT32_MAX))
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Selected Q3 character reaction lacks declared source arguments or enemy state");
    qa_application *app = owner->role->engine->provider->application;
    if (!qa_actors_get(qa_session_actors(app->session), outcome->request.target)) return true;
    if (!application_q3_combat_actor_read(owner->role, owner->profile,
            outcome->request.target, &reaction.source, error)) return false;
    combat_actor *retained = actor_find(owner, outcome->request.target);
    q3g_role *previous = owner->role->engine->entered_role;
    owner->role->engine->entered_role = owner->role;
    ++owner->role->engine->calls; ++owner->calls;
    if (retained) ++retained->calls;
    bool live;
    bool ok = reaction_project(&reaction, outcome->request.attack.attacker, error) &&
        (qa_actor_id_equal(outcome->request.attack.attacker, outcome->request.attack.inflictor) ||
         reaction_project(&reaction, outcome->request.attack.inflictor, error)) &&
        reaction_actor_live(&reaction, &live, error);
    if (ok && live) {
        qa_combat_state state = {0};
        ok = qa_combat_read_traits(shared(owner), outcome->request.target, &state, error);
        if (ok) ok = application_q3_combat_health_write(&reaction.source, qa_source_to_i32(state.health), error);
        if (ok && reaction.death && state.health < -999)
            ok = application_q3_combat_word_write(&reaction.source,
                reaction.source.entity + d->fields.health, -999, error);
        if (ok && live) {
            qa_damage_request request = outcome->request;
            request.amount = outcome->result.applied_damage;
            ok = application_q3_combat_source_arguments(&reaction.source, NULL, &request, 0,
                invoke_reaction, &reaction, error);
        }
        const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), outcome->request.target);
        if (ok && record) {
            ok = reaction_actor_live(&reaction, &live, error);
            if (ok && !live && record->owner == owner->role->engine->provider->owner)
                ok = qa_session_release(app->session, outcome->request.target, error);
        }
    }
    if (retained) --retained->calls;
    --owner->calls; --owner->role->engine->calls;
    owner->role->engine->entered_role = previous;
    actors_collect(owner);
    return ok;
}

static qa_combat_binding binding(combat_actor *actor)
{
    return (qa_combat_binding){.context = actor, .read = read, .write_traits = traits_write, .write_health = health,
        .write_armor = armor, .validate_armor = armor_valid, .empty_regular_armor = empty_armor,
        .source_damage = source_damage, .source_armor_stages = {true, true},
        .has_primary_protection = {true, false},
        .primary_protection = {actor->owner->role->engine->provider->owner, 0}};
}
static bool pointer_actor(application_q3_combat *owner, uint64_t pointer,
    qa_actor_id *out, qa_error *error)
{
    *out = (qa_actor_id){0};
    if (!pointer) return true;
    qa_q3_host_game_data table;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    if (!qa_q3_host_game_data_read(owner->role->host, &table) || table.entity_stride != d->entity_stride ||
        table.client_stride != d->client_stride || pointer < table.entities_address ||
        (pointer - table.entities_address) % table.entity_stride ||
        (pointer - table.entities_address) / table.entity_stride >= table.entity_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Damage pointer leaves its declared located Source entity");
    uint32_t slot = (uint32_t)((pointer - table.entities_address) / table.entity_stride);
    if (slot >= 1022) return true;
    uint8_t inuse[4];
    if (!qa_q3_host_source_bytes_read(owner->role->host, pointer + d->fields.inuse,
            inuse, sizeof(inuse), error)) return false;
    if (!qa_load_i32le(inuse)) return true;
    return qa_q3_host_actor(owner->role->host, slot, true, out, error);
}
static bool vector(application_q3_combat *owner, uint64_t pointer, qa_vec3 *out, qa_error *error)
{
    *out = (qa_vec3){0};
    if (!pointer) return true;
    uint8_t bytes[12];
    if (!qa_q3_host_source_bytes_read(owner->role->host, pointer, bytes, sizeof(bytes), error)) return false;
    *out = qa_v3(qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8));
    return qa_vec_finite(*out) || application_fail(error, QA_ERROR_ARGUMENT, "Original damage has nonfinite source geometry");
}
static bool attack_source(application_q3_combat *owner, qa_damage_request *request,
    int32_t method, uint32_t flags, qa_error *error)
{
    application_provider *source = owner->role->engine->provider;
    qa_application *app = source->application;
    qa_clock_state clock;
    if (!qa_session_clock(app->session, source->owner, &clock) ||
        clock.frame.provider != source->owner || clock.frame.kind != QA_RULESET_Q3)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Reached Source damage has no actual Q3 clock");
    application_provider *combat = application_provider_for(app, request->target, QA_ROLE_COMBAT, "");
    application_provider *movement = application_provider_for(app, request->target, QA_ROLE_MOVEMENT, "");
    application_provider *inventory = application_provider_for(app, request->attack.attacker, QA_ROLE_INVENTORY, "");
    request->attack.time_ns = clock.frame.time_ns;
    request->attack.combat_provider = combat ? combat->owner : 0;
    request->attack.weapon_provider = source->owner;
    request->attack.movement_provider = movement ? movement->owner : 0;
    request->attack.inventory_provider = inventory ? inventory->owner : 0;
    request->attack.cause = (qa_damage_cause){.kind = QA_CAUSE_Q3,
        .source.q3 = {method, application_q3_combat_canonical_flags(
            application_q3_combat_profile_definition(owner->profile), flags)}};
    return qa_attack_next(&owner->sequence, &request->attack, error);
}
static bool reaction_pointer(const application_q3_combat_actor *source, uint32_t offset,
    uint64_t *out, qa_error *error)
{
    return qa_q3_host_source_pointer(source->role->host, source->entity + offset, out, error);
}
typedef struct direct_reaction {
    application_q3_combat *owner;
    const application_q3_reaction_invocation *call;
    qa_damage_result reaction;
} direct_reaction;
static bool direct_dispatch(void *context, qa_source_reaction_observer *observer, qa_error *error)
{
    direct_reaction *reaction = context;
    return qa_source_reaction_dispatch(observer, &reaction->reaction, 0, (qa_vec3){0},
        reaction->owner->role->engine->provider->owner,
        reaction->call->proceed, reaction->call->context, error);
}
bool application_q3_combat_direct_reaction(application_q3_combat *owner,
    const application_q3_reaction_invocation *call, qa_error *error)
{
    application_provider *source = owner->role->engine->provider;
    qa_application *app = source->application;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    const qa_id_index_entry *entry = qa_id_index_find(&owner->reactions, QA_STRING_NONE, call->address);
    size_t kinds = entry ? entry->value : 0;
    const application_q3_reaction_call *arguments[] = {&d->reactions.pain_call, &d->reactions.die_call};
    const uint32_t offsets[] = {d->reactions.pain, d->reactions.die};
    for (size_t i = 0; i < 2; ++i) {
        if (!(kinds & (1u << i))) continue;
        const application_q3_reaction_call *a = arguments[i];
        uint64_t target, callback, attacker, inflictor = 0;
        int32_t amount, method = 0;
        qa_actor_id actor;
        if (!call->pointer(call->context, a->target, &target, error) ||
            !pointer_actor(owner, target, &actor, error)) return false;
        if (!actor.registry || application_provider_for(app, actor, QA_ROLE_CHARACTER, "") == source) continue;
        application_q3_combat_actor original;
        if (!application_q3_combat_actor_read(owner->role, owner->profile, actor, &original, error) ||
            !reaction_pointer(&original, offsets[i], &callback, error)) return false;
        if (callback != call->address) continue;
        if (!call->integer(call->context, a->amount, &amount, error) ||
            !call->pointer(call->context, a->attacker, &attacker, error) ||
            (a->inflictor != UINT32_MAX && !call->pointer(call->context, a->inflictor, &inflictor, error)) ||
            (a->method != UINT32_MAX && !call->integer(call->context, a->method, &method, error))) return false;
        qa_damage_request request = {.target = actor, .amount = amount};
        if (!pointer_actor(owner, attacker, &request.attack.attacker, error) ||
            !pointer_actor(owner, inflictor, &request.attack.inflictor, error) ||
            !attack_source(owner, &request, method, 0, error)) return false;
        int32_t health;
        if (!application_q3_combat_word_read(&original, original.entity + d->fields.health, &health, error) ||
            !qa_combat_set_health(shared(owner), actor, health, error)) return false;
        direct_reaction reaction = {.owner = owner, .call = call,
            .reaction = {.applied_damage = amount, .reaction = i ? QA_REACTION_DEATH : QA_REACTION_PAIN}};
        return qa_combat_source_reaction(shared(owner), &request, &reaction.reaction, error) &&
            qa_combat_run_source_reaction(shared(owner), &request, &reaction.reaction,
                direct_dispatch, &reaction, error);
    }
    return call->proceed(call->context, error);
}
typedef struct qvm_reaction {
    const qa_qvm_call *call;
    int32_t *result;
} qvm_reaction;
static bool qvm_reaction_integer(void *context, uint32_t index, int32_t *out, qa_error *error)
{
    qvm_reaction *source = context;
    return qa_qvm_call_argument(source->call, index, out, error);
}
static bool qvm_reaction_pointer(void *context, uint32_t index, uint64_t *out, qa_error *error)
{
    int32_t word;
    if (!qvm_reaction_integer(context, index, &word, error)) return false;
    *out = (uint32_t)word; return true;
}
static bool qvm_reaction_proceed(void *context, qa_error *error)
{
    qvm_reaction *source = context;
    return qa_qvm_proceed(source->call, source->result, error);
}
static bool direct_enter(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    qvm_reaction source = {call, result};
    application_q3_reaction_invocation invocation = {.address = call->instruction,
        .context = &source, .pointer = qvm_reaction_pointer,
        .integer = qvm_reaction_integer, .proceed = qvm_reaction_proceed};
    *result = 0;
    return application_q3_combat_direct_reaction(context, &invocation, error);
}
static qa_qvm_function_hook direct_resolve(void *context, const qa_qvm_call *call, void **hook_context)
{
    application_q3_combat *owner = context;
    if (!qa_id_index_find(&owner->reactions, QA_STRING_NONE, call->instruction)) return NULL;
    *hook_context = owner;
    return direct_enter;
}
typedef struct native_reaction_call {
    qa_native_entry_observer *binding;
    const qa_native_value *arguments;
    size_t count;
    qa_native_value *result;
    qa_error cancellation;
    bool cancelling;
} native_reaction_call;
typedef struct qvm_damage_call {
    const qa_qvm_call *call;
    int32_t *result;
} qvm_damage_call;
static bool qvm_values(const qa_qvm_call *call, const qa_native_value_type *types,
    size_t count, qa_native_value *values, qa_error *error)
{
    for (size_t i = 0; i < count; ++i) {
        int32_t word;
        if (!qa_qvm_call_argument(call, i, &word, error)) return false;
        values[i].type = types ? types[i] : QA_NATIVE_ADDRESS;
        if (values[i].type == QA_NATIVE_ADDRESS) values[i].as.address = (uint32_t)word;
        else if (values[i].type == QA_NATIVE_F32) memcpy(&values[i].as.f32, &word, sizeof(word));
        else values[i].as.i32 = word;
    }
    return true;
}
static bool qvm_damage_proceed(void *context, const qa_native_value *values, size_t count, qa_error *error)
{
    qvm_damage_call *source = context;
    int32_t prior[APPLICATION_Q3_COMBAT_ARGUMENTS];
    size_t saved = 0;
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        int32_t word;
        if (values[i].type == QA_NATIVE_ADDRESS) {
            if (values[i].as.address > UINT32_MAX) {
                ok = application_fail(error, QA_ERROR_ARGUMENT, "Source pointer exceeds its QVM argument width");
                break;
            }
            uint32_t pointer = (uint32_t)values[i].as.address;
            memcpy(&word, &pointer, sizeof(word));
        } else if (values[i].type == QA_NATIVE_F32) memcpy(&word, &values[i].as.f32, sizeof(word));
        else word = values[i].as.i32;
        if (!qa_qvm_call_argument(source->call, i, prior + i, error)) { ok = false; break; }
        ++saved;
        if (word != prior[i]) ok = qa_qvm_call_set_argument(source->call, i, word, error);
    }
    if (ok) ok = qa_qvm_proceed(source->call, source->result, error);
    qa_error first = error ? *error : (qa_error){0};
    for (size_t i = 0; i < saved; ++i) {
        qa_error restore = {0};
        if (!qa_qvm_call_set_argument(source->call, i, prior[i], &restore)) {
            if (ok) first = restore;
            ok = false;
        }
    }
    if (!ok && error) *error = first;
    return ok;
}
static bool qvm_damage_cancel(void *context, qa_error *error)
{ return qa_qvm_cancel(((qvm_damage_call *)context)->call, error); }
static bool native_damage_accepts(void *context, const qa_error *error)
{
    native_reaction_call *call = context;
    return call->cancelling && qa_error_same(error, &call->cancellation);
}
static bool native_damage_cancel(void *context, qa_error *error)
{
    native_reaction_call *call = context;
    application_fail(&call->cancellation, QA_ERROR_ARGUMENT, "Original Q3 damage target retired during its source callback");
    call->cancelling = true;
    if (error) *error = call->cancellation;
    return false;
}
static bool native_damage_proceed(void *context, const qa_native_value *values, size_t count, qa_error *error)
{
    native_reaction_call *call = context;
    if (call->result->type != QA_NATIVE_VOID)
        return qa_native_invoke_original(call->binding, values, count, call->result, error);
    bool cancelled;
    return qa_native_invoke_original_cancellable(call->binding, values, count, call->result,
        native_damage_accepts, call, &cancelled, error);
}
static bool damage_enter(application_q3_combat *, const application_q3_damage_call *, qa_error *);
static bool freed_enter(application_q3_combat *, const application_q3_damage_call *, qa_error *);
static bool armor_enter(application_q3_combat *, const application_q3_damage_call *, int32_t *, qa_error *);
static bool native_reaction_pointer(void *context, uint32_t index, uint64_t *out, qa_error *error)
{
    (void)error;
    *out = ((native_reaction_call *)context)->arguments[index].as.address;
    return true;
}
static bool native_reaction_integer(void *context, uint32_t index, int32_t *out, qa_error *error)
{
    (void)error;
    *out = ((native_reaction_call *)context)->arguments[index].as.i32;
    return true;
}
static bool native_reaction_proceed(void *context, qa_error *error)
{
    native_reaction_call *call = context;
    return qa_native_invoke_original(call->binding, call->arguments, call->count, call->result, error);
}
static bool native_reaction_enter(void *context, qa_native_instance *instance,
    qa_native_entry_observer *binding, const qa_native_value *arguments, size_t count,
    qa_native_value *result, qa_error *error)
{
    (void)instance;
    native_reaction_binding *entry = context;
    native_reaction_call source = {.binding = binding, .arguments = arguments, .count = count, .result = result};
    if (entry->kind != NATIVE_COMBAT_REACTION) {
        *result = (qa_native_value){.type = entry->kind == NATIVE_COMBAT_ARMOR ? QA_NATIVE_I32 : QA_NATIVE_VOID};
        application_q3_damage_call call = {.entry = entry->address, .arguments = arguments, .count = count,
            .context = &source, .proceed = native_damage_proceed, .cancel = native_damage_cancel};
        if (entry->kind == NATIVE_COMBAT_DAMAGE) return damage_enter(entry->owner, &call, error);
        if (entry->kind == NATIVE_COMBAT_FREE) return freed_enter(entry->owner, &call, error);
        return armor_enter(entry->owner, &call, &result->as.i32, error);
    }
    application_q3_reaction_invocation call = {.address = entry->address, .context = &source,
        .pointer = native_reaction_pointer, .integer = native_reaction_integer,
        .proceed = native_reaction_proceed};
    *result = (qa_native_value){.type = QA_NATIVE_VOID};
    bool handled;
    if (!application_q3_damage_scope_reaction(entry->owner->scopes, &call, &handled, error)) return false;
    if (handled) return true;
    return application_q3_combat_direct_reaction(entry->owner, &call, error);
}
static bool native_reactions_leave(application_q3_combat *owner, qa_error *error)
{
    while (owner->native_count) {
        native_reaction_binding *entry = owner->native_bindings + owner->native_count - 1;
        if (!qa_native_unobserve_entry(entry->binding, error)) return false;
        *entry = (native_reaction_binding){0};
        --owner->native_count;
    }
    return true;
}
static bool native_binding_install(application_q3_combat *owner, uint64_t address,
    native_combat_kind kind, const qa_native_type *parameters, size_t count,
    qa_native_value_type result, const application_q3_reaction_call *reaction, qa_error *error)
{
    for (size_t i = 0; i < owner->native_count; ++i) {
        native_reaction_binding *entry = owner->native_bindings + i;
        if (entry->address != address) continue;
        if (entry->kind != kind || (reaction &&
            (entry->reaction.count != reaction->count || entry->reaction.target != reaction->target ||
             entry->reaction.attacker != reaction->attacker || entry->reaction.inflictor != reaction->inflictor ||
             entry->reaction.amount != reaction->amount || entry->reaction.method != reaction->method)))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Native combat entry aliases different declared function signatures");
        return true;
    }
    if (owner->native_count == owner->native_capacity)
        return application_fail(error, QA_ERROR_MEMORY, "Native combat callback storage is full");
    qa_native_signature signature = {.abi = qa_native_module_describe(owner->role->module).image.target.abi,
        .parameters = parameters, .parameter_count = count, .result = {.kind = result, .count = 1}};
    native_reaction_binding *binding = owner->native_bindings + owner->native_count;
    *binding = (native_reaction_binding){.owner = owner, .address = address, .kind = kind};
    if (reaction) binding->reaction = *reaction;
    if (!qa_native_observe_entry(qa_native_host_instance(owner->role->native), address, &signature,
        native_reaction_enter, binding, &binding->binding, error)) return false;
    ++owner->native_count;
    return true;
}
static bool native_reaction_bind(void *context, uint64_t address,
    const application_q3_reaction_call *call, qa_error *error)
{
    application_q3_combat *owner = context;
    if (!call->arguments_present)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native damage reaction has no declared argument layout");
    qa_native_type parameters[APPLICATION_Q3_COMBAT_ARGUMENTS];
    for (size_t i = 0; i < call->count; ++i)
        parameters[i] = (qa_native_type){.kind = QA_NATIVE_I32, .count = 1};
    parameters[call->target].kind = parameters[call->attacker].kind = QA_NATIVE_ADDRESS;
    if (call->inflictor != UINT32_MAX) parameters[call->inflictor].kind = QA_NATIVE_ADDRESS;
    return native_binding_install(owner, address, NATIVE_COMBAT_REACTION, parameters, call->count,
        QA_NATIVE_VOID, call, error);
}
static bool native_declared_bind(application_q3_combat *owner, uint32_t entry,
    native_combat_kind kind, const application_q3_combat_call *call, qa_error *error)
{
    uint64_t address;
    if (!qa_native_rva(qa_native_host_instance(owner->role->native), entry, 1, &address, error)) return false;
    qa_native_type parameters[APPLICATION_Q3_COMBAT_ARGUMENTS];
    size_t count = call ? call->count : 1;
    for (size_t i = 0; i < count; ++i)
        parameters[i] = (qa_native_type){.kind = call ? call->types[i] : QA_NATIVE_ADDRESS, .count = 1};
    return native_binding_install(owner, address, kind, parameters, count,
        kind == NATIVE_COMBAT_ARMOR ? QA_NATIVE_I32 : QA_NATIVE_VOID, NULL, error);
}
static bool native_reactions_enter(application_q3_combat *owner, qa_error *error)
{
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    const application_q3_reaction_call *calls[] = {&d->reactions.pain_call, &d->reactions.die_call};
    if ((d->callbacks.has_damage && d->damage_call.count && d->has_damage_flags &&
            !native_declared_bind(owner, d->callbacks.damage, NATIVE_COMBAT_DAMAGE, &d->damage_call, error)) ||
        (d->callbacks.has_free && !native_declared_bind(owner, d->callbacks.free, NATIVE_COMBAT_FREE, NULL, error)) ||
        (d->armor.has_check && d->armor.call.count &&
            !native_declared_bind(owner, d->armor.check, NATIVE_COMBAT_ARMOR, &d->armor.call, error))) return false;
    for (size_t i = 0; i < owner->reactions.count; ++i) {
        const qa_id_index_entry *entry = owner->reactions.entries + i;
        const application_q3_reaction_call *call = calls[(entry->value & 1u) ? 0 : 1];
        if (entry->value == 3 && (calls[0]->count != calls[1]->count ||
            calls[0]->target != calls[1]->target || calls[0]->attacker != calls[1]->attacker ||
            calls[0]->inflictor != calls[1]->inflictor || calls[0]->amount != calls[1]->amount ||
            calls[0]->method != calls[1]->method))
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                "Native reaction aliases different declared function signatures");
        if (!native_reaction_bind(owner, entry->discriminator, call, error)) return false;
    }
    return true;
}
static bool native_reactions_rebind(application_q3_combat *owner, qa_error *error)
{
    struct application_q3_guest *engine = owner->role->engine;
    qa_application *app = owner->role->engine->provider->application;
    const qa_id_index *clients = &engine->client_actors.retained;
    for (size_t i = 0; i < clients->count; ++i) {
        uint32_t slot = (uint32_t)clients->entries[i].value;
        const q3g_client *client = engine->clients + slot;
        qa_actor_id actor = client->actor;
        if (!client->connected || !client->begun || client->pending_retirement ||
            actor_find(owner, actor) || !qa_actors_get(qa_session_actors(app->session), actor) ||
            application_provider_for(app, actor, QA_ROLE_CHARACTER, "") == owner->role->engine->provider) continue;
        if (!application_q3_combat_admit(owner, actor, error)) return false;
    }
    return true;
}
bool application_q3_combat_enter(application_q3_combat *owner, qa_error *error)
{
    if (!owner) return true;
    if (!owner->source_entries) {
        if (!owner->role->vm && !native_reactions_rebind(owner, error)) return false;
        qa_id_index_clear(&owner->reactions);
        const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
        const uint32_t offsets[] = {d->reactions.pain, d->reactions.die};
        const application_q3_reaction_call *arguments[] = {&d->reactions.pain_call, &d->reactions.die_call};
        qa_application *app = owner->role->engine->provider->application;
        for (combat_actor *actor = owner->actors; actor; actor = actor->next) {
            if (!qa_actors_get(qa_session_actors(app->session), actor->source.actor) ||
                application_provider_for(app, actor->source.actor, QA_ROLE_CHARACTER, "") == owner->role->engine->provider) continue;
            bool live;
            if (!application_q3_combat_actor_live(&actor->source, &live, error)) return false;
            if (!live) continue;
            if (actor->bound && qa_combat_primary_current(shared(owner), actor->source.actor,
                    actor->serial, actor)) {
                qa_combat_state current;
                int32_t source_health;
                if (!qa_combat_read_traits(shared(owner), actor->source.actor, &current, error) ||
                    !application_q3_combat_word_read(&actor->source,
                        actor->source.entity + d->fields.health, &source_health, error)) return false;
                if (source_health != current.health &&
                    !application_q3_combat_health_write(&actor->source, current.health, error)) return false;
            }
            for (size_t i = 0; i < 2; ++i) {
                if (!arguments[i]->arguments_present) continue;
                uint64_t pointer;
                if (!reaction_pointer(&actor->source, offsets[i], &pointer, error)) return false;
                if (!pointer) continue;
                qa_id_index_entry *entry = qa_id_index_find(&owner->reactions, QA_STRING_NONE, pointer);
                if (entry) entry->value |= 1u << i;
                else qa_id_index_insert(&owner->reactions, QA_STRING_NONE, pointer, 1u << i);
            }
        }
        owner->direct_bound = owner->reactions.count != 0 || (!owner->role->vm &&
            ((d->callbacks.has_damage && d->damage_call.count && d->has_damage_flags) ||
             d->callbacks.has_free || (d->armor.has_check && d->armor.call.count)));
        bool bound = !owner->direct_bound || (owner->role->vm ?
            application_q3_damage_scopes_enter(owner->scopes, direct_resolve, owner, error) :
            native_reactions_enter(owner, error));
        if (!bound) {
            owner->direct_bound = false;
            qa_error cleanup = {0};
            if (!owner->role->vm) (void)native_reactions_leave(owner, &cleanup);
            return false;
        }
    }
    ++owner->source_entries;
    return true;
}
bool application_q3_combat_leave(application_q3_combat *owner, qa_error *error)
{
    if (!owner) return true;
    if (--owner->source_entries || !owner->direct_bound) return true;
    owner->direct_bound = false;
    return owner->role->vm ? application_q3_damage_scopes_leave(owner->scopes, error) :
        native_reactions_leave(owner, error);
}
typedef struct entered_damage {
    application_q3_combat *owner;
    combat_actor *actor;
    const application_q3_damage_call *call;
    uint32_t flags;
    const qa_damage_request *effective;
    qa_damage_observer *observer;
    qa_damage_result *result;
    qa_damage_request original;
} entered_damage;
static bool proceed_damage(void *context, const qa_native_value *values, size_t count, qa_error *error)
{
    entered_damage *entered = context;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(entered->owner->profile);
    if (count != d->damage_call.count)
        return application_fail(error, QA_ERROR_FORMAT, "Lowered Source damage omits its declared argument words");
    qa_native_value arguments[APPLICATION_Q3_COMBAT_ARGUMENTS];
    memcpy(arguments, entered->call->arguments, count * sizeof(*arguments));
    for (size_t i = 0; i < Q3_DAMAGE_WORDS; ++i) {
        uint32_t position = d->damage_call.positions[i];
        bool preserve = (i == Q3_DAMAGE_DIRECTION && qa_vec_equal(entered->effective->direction, entered->original.direction)) ||
            (i == Q3_DAMAGE_POINT && qa_vec_equal(entered->effective->point, entered->original.point)) ||
            (i == Q3_DAMAGE_ATTACKER && qa_actor_id_equal(entered->effective->attack.attacker, entered->original.attack.attacker)) ||
            (i == Q3_DAMAGE_INFLICTOR && qa_actor_id_equal(entered->effective->attack.inflictor, entered->original.attack.inflictor));
        if (!preserve) arguments[position] = values[position];
    }
    application_q3_damage_call lowered = *entered->call;
    lowered.arguments = arguments;
    return application_q3_damage_scope_run(entered->owner->scopes, &lowered,
        &entered->actor->source, entered->effective, entered->observer, entered->result, error);
}
static bool entered_source(void *context, qa_combat *combat, const qa_damage_request *request,
    qa_damage_observer *observer, qa_damage_result *result, qa_error *error)
{
    entered_damage *entered = context;
    if (combat != shared(entered->owner) || !qa_actor_id_equal(request->target, entered->actor->source.actor))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Reached Q3 damage cannot change its actual source target");
    entered->effective = request; entered->observer = observer; entered->result = result;
    return application_q3_combat_source_damage(&entered->actor->source, entered->call->qvm, request,
        entered->flags, proceed_damage, entered, error);
}
static bool damage_enter(application_q3_combat *owner,
    const application_q3_damage_call *call, qa_error *error)
{
    incoming_damage *incoming = owner->incoming;
    if (incoming && !incoming->entered) {
        incoming->entered = true;
        return application_q3_damage_scope_run(owner->scopes, call, &incoming->actor->source,
            incoming->request, incoming->observer, incoming->result, error);
    }
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    const qa_native_value *words[Q3_DAMAGE_WORDS];
    for (size_t i = 0; i < Q3_DAMAGE_WORDS; ++i) words[i] = call->arguments + d->damage_call.positions[i];
    qa_actor_id target_id;
    if (!pointer_actor(owner, words[Q3_DAMAGE_TARGET]->as.address, &target_id, error)) return false;
    if (!target_id.registry) return call->proceed(call->context, call->arguments, call->count, error);
    qa_damage_request request = {.target = target_id, .amount = words[Q3_DAMAGE_AMOUNT]->as.i32,
        .knockback = (words[Q3_DAMAGE_DIRECTION]->as.address &&
            !((uint32_t)words[Q3_DAMAGE_FLAGS]->as.i32 & d->damage_flags.no_knockback)) ?
            words[Q3_DAMAGE_AMOUNT]->as.i32 : 0,
        .radius = ((uint32_t)words[Q3_DAMAGE_FLAGS]->as.i32 & d->damage_flags.radius) != 0};
    if (!vector(owner, words[Q3_DAMAGE_DIRECTION]->as.address, &request.direction, error) ||
        !vector(owner, words[Q3_DAMAGE_POINT]->as.address, &request.point, error)) return false;
    if (!pointer_actor(owner, words[Q3_DAMAGE_ATTACKER]->as.address, &request.attack.attacker, error) ||
        !pointer_actor(owner, words[Q3_DAMAGE_INFLICTOR]->as.address, &request.attack.inflictor, error)) return false;
    qa_application *app = owner->role->engine->provider->application;
    application_provider *combat = application_provider_for(app, request.target, QA_ROLE_COMBAT, "");
    if (!attack_source(owner, &request, words[Q3_DAMAGE_METHOD]->as.i32,
        (uint32_t)words[Q3_DAMAGE_FLAGS]->as.i32, error)) return false;
    qa_damage_outcome outcome = {0};
    if (combat && combat != owner->role->engine->provider) {
        uint32_t slot;
        if (!q3g_project_actor(owner->role, call->qvm, target_id, &slot, error)) return false;
        ++owner->calls;
        bool ok = !qa_combat_has(shared(owner), target_id) ||
            qa_combat_apply(shared(owner), &request, &outcome, error);
        --owner->calls;
        qa_damage_outcome_release(&outcome);
        if (ok && qa_actors_get(qa_session_actors(app->session), target_id)) {
            ok = q3g_project_actor(owner->role, call->qvm, target_id, &slot, error);
        }
        return ok;
    }
    combat_actor *target = actor_find(owner, target_id);
    if ((!target || !target->bound) && !application_q3_combat_admit(owner, target_id, error)) return false;
    target = actor_find(owner, target_id);
    if (!target || !target->bound || !application_q3_combat_actor_current(&target->source, error))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Damage target has no admitted original combat primary");
    if (!native_damage_ready(target, error)) return false;
    entered_damage entered = {.owner = owner, .actor = target, .call = call,
        .flags = (uint32_t)words[Q3_DAMAGE_FLAGS]->as.i32, .original = request};
    ++owner->calls; ++target->calls;
    bool ok = qa_combat_run_source(shared(owner), &request, entered_source, &entered, &outcome, error);
    --target->calls; --owner->calls; qa_damage_outcome_release(&outcome); actors_collect(owner);
    return ok;
}
static bool damage(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_combat *owner = context;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    qa_native_value values[APPLICATION_Q3_COMBAT_ARGUMENTS];
    if (!qvm_values(call, d->damage_call.types, d->damage_call.count, values, error)) return false;
    qvm_damage_call source = {call, result};
    application_q3_damage_call invocation = {.qvm = call, .entry = call->instruction,
        .arguments = values, .count = d->damage_call.count, .context = &source,
        .proceed = qvm_damage_proceed, .cancel = qvm_damage_cancel};
    *result = 0;
    return damage_enter(owner, &invocation, error);
}
static bool freed_enter(application_q3_combat *owner,
    const application_q3_damage_call *call, qa_error *error)
{
    uint64_t pointer = call->arguments[0].as.address;
    qa_actor_id released = {0}; qa_q3_host_game_data table;
    if (qa_q3_host_game_data_read(owner->role->host, &table) && table.entity_stride &&
        pointer >= table.entities_address && (pointer - table.entities_address) % table.entity_stride == 0) {
        uint64_t slot = (pointer - table.entities_address) / table.entity_stride;
        if (slot < table.entity_count && slot < 1022 &&
            !qa_q3_host_actor(owner->role->host, (uint32_t)slot, false, &released, error)) return false;
    }
    combat_actor *prior = actor_find(owner, released);
    if (!prior || (!prior->bound && !qa_combat_health_current(shared(owner), released, prior)))
        released = (qa_actor_id){0};
    if (!call->proceed(call->context, call->arguments, call->count, error)) return false;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(owner->role->engine->provider->application->session), released);
    combat_actor *actor = actor_find(owner, released);
    if (record && actor) {
        bool owned = record->owner == owner->role->engine->provider->owner;
        bool live;
        if (!application_q3_combat_actor_live(&actor->source, &live, error)) return false;
        if (!live && qa_combat_health_current(shared(owner), released, actor) &&
            !qa_combat_detach_health(shared(owner), released, actor, error)) return false;
        if (!live && owned && qa_actors_get(qa_session_actors(owner->role->engine->provider->application->session), released) &&
            !qa_session_release(owner->role->engine->provider->application->session, released, error)) return false;
    }
    return q3g_projection_after_free(owner->role, pointer, error) &&
        (!call->qvm || application_q3_pickups_after_free(owner->role->pickups,
            call->qvm, (int32_t)(uint32_t)pointer, error));
}
static bool freed(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    qa_native_value value;
    if (!qvm_values(call, NULL, 1, &value, error)) return false;
    qvm_damage_call source = {call, result};
    application_q3_damage_call invocation = {.qvm = call, .entry = call->instruction,
        .arguments = &value, .count = 1, .context = &source,
        .proceed = qvm_damage_proceed, .cancel = qvm_damage_cancel};
    return freed_enter(context, &invocation, error);
}
static bool armor_enter(application_q3_combat *owner,
    const application_q3_damage_call *call, int32_t *result, qa_error *error)
{
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    uint64_t target = call->arguments[d->armor.call.positions[Q3_ARMOR_TARGET]].as.address;
    int32_t amount = call->arguments[d->armor.call.positions[Q3_ARMOR_AMOUNT]].as.i32,
        source_flags = call->arguments[d->armor.call.positions[Q3_ARMOR_FLAGS]].as.i32;
    application_q3_damage_frame *frame = application_q3_damage_scope_current(owner->scopes, target);
    if (!frame) return call->proceed(call->context, call->arguments, call->count, error);
    const qa_damage_request *request = application_q3_damage_scope_request(frame);
    qa_actor_owner regular = 0, power = 0;
    uint64_t serial;
    bool foreign_regular = qa_combat_protection_owner(shared(owner), request->target, QA_PROTECTION_REGULAR, &regular, &serial) &&
        regular != owner->role->engine->provider->owner;
    bool foreign_power = qa_combat_protection_owner(shared(owner), request->target, QA_PROTECTION_POWERED, &power, &serial);
    if (!foreign_regular && !foreign_power) return call->proceed(call->context, call->arguments, call->count, error);
    qa_damage_flags flags = qa_attack_flags(&request->attack);
    flags.no_armor = ((uint32_t)source_flags & d->damage_flags.no_armor) != 0;
    const application_q3_damage_call *damage_call = application_q3_damage_scope_call(frame);
    qa_damage_geometry geometry = {.normal = request->normal};
    if (!vector(owner, damage_call->arguments[d->damage_call.positions[Q3_DAMAGE_DIRECTION]].as.address,
            &geometry.direction, error) ||
        !vector(owner, damage_call->arguments[d->damage_call.positions[Q3_DAMAGE_POINT]].as.address,
            &geometry.point, error)) return false;
    qa_armor_context victim = {0};
    double saved = 0;
    if (foreign_power && !qa_combat_absorb(shared(owner), request, QA_PROTECTION_POWERED, &geometry,
        amount, flags, &victim, &saved, NULL, error)) return false;
    const application_q3_combat_actor *source = application_q3_damage_scope_actor(frame);
    bool live = false;
    if (qa_actors_get(qa_session_actors(owner->role->engine->provider->application->session), source->actor) &&
        !application_q3_combat_actor_live(source, &live, error)) return false;
    if (!live) { *result = 0; return application_q3_damage_scope_cancel(frame, error); }
    if (!isfinite(saved) || saved < 0 || saved > fmax(0, amount))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected power armor exceeds reached source damage");
    int32_t power_saved = (int32_t)trunc(saved), remaining = amount - power_saved;
    qa_native_value arguments[APPLICATION_Q3_COMBAT_ARGUMENTS];
    memcpy(arguments, call->arguments, call->count * sizeof(*arguments));
    arguments[d->armor.call.positions[Q3_ARMOR_AMOUNT]].as.i32 = remaining;
    bool ok;
    if (foreign_regular) {
        double regular_saved;
        ok = vector(owner, damage_call->arguments[d->damage_call.positions[Q3_DAMAGE_DIRECTION]].as.address,
                &geometry.direction, error) &&
            vector(owner, damage_call->arguments[d->damage_call.positions[Q3_DAMAGE_POINT]].as.address,
                &geometry.point, error);
        flags.no_armor = ((uint32_t)source_flags & d->damage_flags.no_armor) != 0;
        if (ok) ok = qa_combat_absorb(shared(owner), request, QA_PROTECTION_REGULAR, &geometry,
            remaining, flags, &victim, &regular_saved, NULL, error);
        if (ok && (!isfinite(regular_saved) || regular_saved < 0 || regular_saved > fmax(0, remaining)))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Selected regular armor exceeds reached source damage");
        if (ok) *result = power_saved + (int32_t)trunc(regular_saved);
    } else {
        ok = call->proceed(call->context, arguments, call->count, error);
        if (ok) *result += power_saved;
    }
    live = false;
    if (ok && qa_actors_get(qa_session_actors(owner->role->engine->provider->application->session), source->actor))
        ok = application_q3_combat_actor_live(source, &live, error);
    if (ok && !live) { *result = 0; ok = application_q3_damage_scope_cancel(frame, error); }
    return ok;
}
static bool armor_stage(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_combat *owner = context;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(owner->profile);
    qa_native_value values[APPLICATION_Q3_COMBAT_ARGUMENTS];
    if (!qvm_values(call, d->armor.call.types, d->armor.call.count, values, error)) return false;
    qvm_damage_call source = {call, result};
    application_q3_damage_call invocation = {.qvm = call, .entry = call->instruction,
        .arguments = values, .count = d->armor.call.count, .context = &source,
        .proceed = qvm_damage_proceed, .cancel = qvm_damage_cancel};
    return armor_enter(owner, &invocation, result, error);
}
bool application_q3_combat_create(q3g_role *role, const application_q3_combat_profile *profile,
    application_q3_combat **out, qa_error *error)
{
    if (!role || !profile || !out || *out || (!role->vm && !role->module) || !role->host || role->kind != QA_QVM_GAME ||
        role->image != application_q3_combat_profile_image(profile) ||
        role->module != application_q3_combat_profile_module(profile) || role->abi != application_q3_combat_profile_abi(profile))
        return application_fail(error, QA_ERROR_ARGUMENT, "Combat hooks require their genuine original GAME constructor");
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(profile);
    if (!role->vm && !d->reactions.pain_call.arguments_present && !d->reactions.die_call.arguments_present &&
        !d->callbacks.has_damage && !d->callbacks.has_free && !d->armor.has_check)
        return true;
    application_q3_combat *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Retaining original combat callback owner");
    owner->role = role; owner->profile = profile; *out = owner;
    const qa_actor_registry *registry = qa_session_actors(role->engine->provider->application->session);
    if (!qa_actor_index_create(&owner->actor_index, registry, NULL, error)) return false;
    owner->actor_chunk=qa_actors_capacity(registry);
    owner->actor_storage=application_control_storage_acquire(role->engine->provider->application,error);
    if(!owner->actor_storage||!qa_pool_prepare(&owner->actor_pool,
        qa_unified_frame_lease_arena(owner->actor_storage),owner->actor_chunk,
        sizeof(combat_actor),_Alignof(combat_actor),error))return false;
    owner->reactions.storage = qa_unified_frame_lease_arena(owner->actor_storage);
    if (owner->actor_chunk > (SIZE_MAX / sizeof(native_reaction_binding) - 3) / 2 ||
        !qa_id_index_reserve(&owner->reactions, owner->actor_chunk * 2, error)) return false;
    if (!role->vm) {
        owner->native_capacity = owner->actor_chunk * 2 + 3;
        owner->native_bindings = qa_arena_alloc(qa_unified_frame_lease_arena(owner->actor_storage),
            owner->native_capacity * sizeof(*owner->native_bindings), _Alignof(native_reaction_binding), error);
        if (!owner->native_bindings) return false;
    }
    if (!application_q3_damage_scopes_create(role, profile, role->vm ? NULL : native_reaction_bind,
        owner, &owner->scopes, error)) return false;
    if (!role->vm) return true;
    uint32_t entries[] = {d->callbacks.damage, d->callbacks.free, d->armor.check};
    qa_qvm_function_hook hooks[] = {damage, freed, armor_stage};
    for (size_t i = 0; i < 3; ++i) {
        if (!qa_qvm_bind_function(role->vm, entries[i], true, hooks[i], owner, &owner->bindings[i], error)) return false;
        ++owner->count;
    }
    return true;
}
bool application_q3_combat_idle(const application_q3_combat *owner)
{ return !owner || (!owner->calls && !owner->source_entries && !owner->incoming && !owner->native_count &&
    (!owner->scopes || application_q3_damage_scopes_idle(owner->scopes))); }
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
    if (!owner || owner->role->retired || !owner->role->host || (!owner->role->vm && !owner->role->module))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original combat admission requires its retained live Source owner");
    combat_actor *actor = actor_find(owner, id);
    if (!actor) { actor = actor_create(owner, id, error); if (!actor) return false; }
    if (!application_q3_combat_actor_read(owner->role, owner->profile, id, &actor->source, error)) return false;
    application_provider *authority = application_provider_for(
        owner->role->engine->provider->application, id, QA_ROLE_COMBAT, "");
    if (authority && authority != owner->role->engine->provider) {
        qa_combat_health_binding value;
        return application_q3_combat_health_binding(owner, id, &value, error) &&
            (!value.context || qa_combat_bind_health(shared(owner), id, &value, error));
    }
    if (actor && qa_combat_health_current(shared(owner), id, actor) &&
        !qa_combat_detach_health(shared(owner), id, actor, error)) return false;
    if (actor && actor->bound) return application_q3_combat_actor_current(&actor->source, error);
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
    if (!actor) { actor = actor_create(owner, id, error); if (!actor) return false; }
    if (!application_q3_combat_actor_read(owner->role, owner->profile, id, &actor->source, error)) return false;
    if (actor->bound && actor->serial != serial)
        return application_fail(error, QA_ERROR_FORMAT, "Restored combat changed its primary storage serial");
    actor->serial = serial; actor->bound = true; *out = binding(actor); return true;
}
bool application_q3_combat_health_binding(application_q3_combat *owner, qa_actor_id id,
    qa_combat_health_binding *out, qa_error *error)
{
    if (!owner || !out || owner->role->retired || !owner->role->host || (!owner->role->vm && !owner->role->module))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original health requires its retained live Source owner");
    *out = (qa_combat_health_binding){0};
    application_provider *provider = owner->role->engine->provider;
    qa_application *app = provider->application;
    const qa_actor_player *player = qa_actors_player(qa_session_actors(app->session), id);
    if (!player || !player->present ||
        application_provider_for(app, id, QA_ROLE_CHARACTER, "") != provider ||
        application_provider_for(app, id, QA_ROLE_COMBAT, "") == provider) return true;
    combat_actor *actor = actor_find(owner, id);
    if (!actor) { actor = actor_create(owner, id, error); if (!actor) return false; }
    if (!application_q3_combat_actor_read(owner->role, owner->profile, id, &actor->source, error)) return false;
    *out = (qa_combat_health_binding){.context = actor, .read = health_read, .write = health};
    return true;
}
bool application_q3_combat_actor_released(application_q3_combat *owner, qa_actor_record released, qa_error *error)
{
    combat_actor *actor = owner ? actor_find(owner, released.id) : NULL;
    if (actor) {
        if (qa_combat_health_current(shared(owner), released.id, actor) &&
            !qa_combat_detach_health(shared(owner), released.id, actor, error)) return false;
        actor->bound = false;
        qa_actor_index_remove(&owner->actor_index, released.id);
        actor->retired_next = owner->retired; owner->retired = actor;
        actors_collect(owner);
    }
    return true;
}
size_t application_q3_combat_descriptor_count(const application_q3_combat *owner)
{ return owner ? owner->count : 0; }
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
{ if (owner && owner->count) memcpy(owner->bindings, bindings, owner->count * sizeof(*bindings)); }
bool application_q3_combat_destroy(application_q3_combat **pointer, qa_error *error)
{
    if (!pointer || !*pointer) return true;
    application_q3_combat *owner = *pointer;
    if (owner->calls || owner->source_entries || owner->incoming ||
        (owner->scopes && !application_q3_damage_scopes_quiescent(owner->scopes)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original combat retains a source action or refused observer cleanup");
    if (!native_reactions_leave(owner, error)) return false;
    for (combat_actor *actor=owner->actors;actor;actor=actor->next) {
        if (qa_combat_health_current(shared(owner), actor->source.actor, actor) &&
            !qa_combat_detach_health(shared(owner), actor->source.actor, actor, error)) return false;
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
    qa_actor_index_destroy(&owner->actor_index);
    qa_id_index_destroy(&owner->reactions);
    qa_unified_frame_lease_release(owner->actor_storage);
    free(owner); *pointer = NULL; return true;
}
