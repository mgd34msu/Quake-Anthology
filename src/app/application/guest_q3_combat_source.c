#include "guest_q3_combat_source.h"
#include "guest_q3_private.h"

typedef struct damage_lowering {
    const application_q3_combat_actor *target;
    const qa_damage_request *request;
    const qa_qvm_call *parent;
    application_q3_damage_words_fn invoke;
    void *context;
    uint32_t flags;
} damage_lowering;

static void float_store(uint8_t *out, float value)
{ uint32_t bits; memcpy(&bits, &value, sizeof(bits)); qa_store_u32le(out, bits); }

uint32_t application_q3_combat_canonical_flags(const application_q3_combat_definition *d, uint32_t source)
{
    return ((source & d->damage_flags.radius) ? 1u : 0u) |
        ((source & d->damage_flags.no_armor) ? 2u : 0u) |
        ((source & d->damage_flags.no_knockback) ? 4u : 0u) |
        ((source & d->damage_flags.no_protection) ? 8u : 0u) |
        ((source & d->damage_flags.no_team_protection) ? 16u : 0u);
}
uint32_t application_q3_combat_source_flags(const application_q3_combat_definition *d,
    const qa_damage_request *request, uint32_t original)
{
    qa_damage_flags flags = qa_attack_flags(&request->attack);
    uint32_t masks = d->damage_flags.radius | d->damage_flags.no_armor | d->damage_flags.no_knockback |
        d->damage_flags.no_protection | d->damage_flags.no_team_protection;
    return (original & ~masks) | (request->radius ? d->damage_flags.radius : 0) |
        (flags.no_armor ? d->damage_flags.no_armor : 0) |
        (flags.no_knockback ? d->damage_flags.no_knockback : 0) |
        (flags.no_protection ? d->damage_flags.no_protection : 0) |
        (flags.no_team_protection ? d->damage_flags.no_team_protection : 0);
}
static bool pointer(const application_q3_combat_actor *target, qa_actor_id actor,
    uint32_t *out, bool *present, qa_error *error)
{
    *out = 0; *present = false;
    if (!actor.registry) return true;
    q3g_role *role = target->role;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(role->engine->provider->application->session), actor);
    if (!record) return true;
    uint32_t slot;
    qa_error local = {0};
    if (!qa_q3_host_actor_slot(role->host, actor, &slot, &local)) {
        if (record->owner != role->engine->provider->owner) return true;
        if (error) *error = local;
        return false;
    }
    application_q3_combat_actor source;
    bool live;
    if (!application_q3_combat_actor_read(role, target->profile, actor, &source, error) ||
        !application_q3_combat_actor_live(&source, &live, error)) return false;
    if (live) { *out = source.entity; *present = true; }
    return true;
}
static bool source_call(damage_lowering *lowering, uint32_t entry,
    const int32_t *words, size_t count, int32_t *out, qa_error *error)
{
    q3g_role *role = lowering->target->role;
    return lowering->parent ? qa_qvm_invoke_source_callback(lowering->parent,
        role->image, (int32_t)entry, words, count, out, error) :
        qa_qvm_invoke(role->vm, entry, words, count, out, error);
}
static bool temporary_body(damage_lowering *lowering, uint32_t pointer,
    uint32_t attacker, const qa_body_state *body, qa_error *error)
{
    const application_q3_combat_actor *target = lowering->target;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(target->profile);
    qa_q3_host_game_data table;
    if (!qa_q3_host_game_data_read(target->role->host, &table) ||
        table.entities_address != target->table.entities_address || table.entity_stride != d->entity_stride ||
        table.client_stride != d->client_stride || pointer < table.entities_address ||
        (pointer - table.entities_address) % table.entity_stride ||
        (pointer - table.entities_address) / table.entity_stride >= table.entity_count ||
        pointer > qa_qvm_memory_size(target->role->vm) ||
        d->entity_stride > qa_qvm_memory_size(target->role->vm) - pointer)
        return application_fail(error, QA_ERROR_FORMAT, "Temporary inflictor leaves its real located Source table");
    qa_q3_entity entity;
    qa_qvm_entity_shared shared;
    if (!qa_qvm_read_entity(target->role->vm, (int32_t)pointer, true, &entity, error) ||
        !qa_qvm_read_shared_entity(target->role->vm, (int32_t)pointer, &shared, error)) return false;
    shared.origin = body->origin; shared.angles = body->angles; shared.local_bounds = body->bounds;
    entity.pos.base[0] = body->origin.x; entity.pos.base[1] = body->origin.y; entity.pos.base[2] = body->origin.z;
    entity.pos.delta[0] = body->velocity.x; entity.pos.delta[1] = body->velocity.y; entity.pos.delta[2] = body->velocity.z;
    return application_q3_combat_word_write(target, pointer + d->fields.parent, (int32_t)attacker, error) &&
        qa_qvm_write_entity(target->role->vm, (int32_t)pointer, true, &entity, error) &&
        qa_qvm_write_shared_entity(target->role->vm, (int32_t)pointer, &shared, error);
}
static bool scratch_run(void *context, qa_qvm *vm, uint32_t scratch, qa_error *error)
{
    damage_lowering *lowering = context;
    const application_q3_combat_actor *target = lowering->target;
    const application_q3_combat_definition *d = application_q3_combat_profile_definition(target->profile);
    const qa_damage_request *request = lowering->request;
    uint8_t vectors[24];
    float_store(vectors, request->direction.x); float_store(vectors + 4, request->direction.y);
    float_store(vectors + 8, request->direction.z); float_store(vectors + 12, request->point.x);
    float_store(vectors + 16, request->point.y); float_store(vectors + 20, request->point.z);
    uint32_t attacker = 0, inflictor = 0;
    bool present;
    if (!qa_qvm_write(vm, scratch, (qa_bytes){vectors, sizeof(vectors)}, error) ||
        !pointer(target, request->attack.attacker, &attacker, &present, error) ||
        !pointer(target, request->attack.inflictor, &inflictor, &present, error)) return false;
    int32_t temporary = 0;
    bool allocated = false, ok = true;
    if (!present && request->attack.inflictor.registry &&
        qa_actors_get(qa_session_actors(target->role->engine->provider->application->session), request->attack.inflictor)) {
        qa_body_state body;
        ok = qa_world_body_read(target->role->engine->world, request->attack.inflictor, &body, error) &&
            source_call(lowering, d->callbacks.allocate, NULL, 0, &temporary, error);
        allocated = ok;
        if (ok && temporary <= 0)
            ok = application_fail(error, QA_ERROR_FORMAT, "Actual Source allocation returned no temporary inflictor");
        if (ok) ok = temporary_body(lowering, (uint32_t)temporary, attacker, &body, error);
        if (ok) inflictor = (uint32_t)temporary;
    }
    int32_t words[APPLICATION_Q3_COMBAT_ARGUMENTS];
    memcpy(words, d->damage_call.words, d->damage_call.count * sizeof(*words));
    words[d->damage_call.positions[Q3_DAMAGE_TARGET]] = (int32_t)target->entity;
    words[d->damage_call.positions[Q3_DAMAGE_INFLICTOR]] = (int32_t)inflictor;
    words[d->damage_call.positions[Q3_DAMAGE_ATTACKER]] = (int32_t)attacker;
    words[d->damage_call.positions[Q3_DAMAGE_DIRECTION]] = (int32_t)scratch;
    words[d->damage_call.positions[Q3_DAMAGE_POINT]] = (int32_t)scratch + 12;
    double amount = trunc((double)request->amount);
    if (!isfinite(amount) || amount < INT32_MIN || amount > INT32_MAX)
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Source damage amount leaves its signed argument");
    if (ok) words[d->damage_call.positions[Q3_DAMAGE_AMOUNT]] = (int32_t)amount;
    words[d->damage_call.positions[Q3_DAMAGE_FLAGS]] = (int32_t)lowering->flags;
    int32_t method = request->attack.cause.kind == QA_CAUSE_Q3 ? request->attack.cause.source.q3.means_of_death : 0;
    qa_strings *strings = qa_session_strings(target->role->engine->provider->application->session);
    const char *weapon = qa_strings_cstr(strings, request->attack.weapon);
    if ((weapon && (!strcmp(weapon, "q3:weapon_grapplinghook") || !strcmp(weapon, "q2:weapon_grapple") ||
         !strcmp(weapon, "q2:weapon_hook") || !strcmp(weapon, "ctf:grapple"))) ||
        (request->attack.cause.kind == QA_CAUSE_Q2 && request->attack.cause.source.q2.means_of_death == 56))
        method = (int32_t)d->grapple_method;
    words[d->damage_call.positions[Q3_DAMAGE_METHOD]] = method;
    if (ok) ok = application_q3_combat_actor_current(target, error) &&
        lowering->invoke(lowering->context, words, d->damage_call.count, error);
    if (allocated) {
        qa_error cleanup = {0}; int32_t result;
        if (!source_call(lowering, d->callbacks.free, &temporary, 1, &result, &cleanup)) {
            if (ok && error) *error = cleanup;
            ok = false;
        }
    }
    return ok;
}
static bool scratch_parent(void *context, const qa_qvm_call *call, uint32_t scratch, qa_error *error)
{ return scratch_run(context, call->vm, scratch, error); }
bool application_q3_combat_source_damage(const application_q3_combat_actor *target,
    const qa_qvm_call *parent, const qa_damage_request *request, uint32_t original_flags,
    application_q3_damage_words_fn invoke, void *context, qa_error *error)
{
    qa_combat_state state;
    if (!target || !request || !invoke || !qa_actor_id_equal(target->actor, request->target) ||
        (parent && parent->vm != target->role->vm) ||
        !application_q3_combat_state_read((void *)target, &state, error)) return false;
    if (!state.can_take_damage) return true;
    damage_lowering lowering = {.target = target, .request = request, .parent = parent,
        .invoke = invoke, .context = context,
        .flags = application_q3_combat_source_flags(application_q3_combat_profile_definition(target->profile), request, original_flags)};
    return parent ? qa_qvm_source_scratch(parent, target->role->image, 24, scratch_parent, &lowering, error) :
        qa_qvm_source_scratch_run(target->role->vm, target->role->image, 24, scratch_run, &lowering, error);
}
