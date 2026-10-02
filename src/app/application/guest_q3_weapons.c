#include "guest_q3_weapons_private.h"

bool q3_weapons_read(application_q3_weapons *w, uint32_t address, int32_t *out, qa_error *error)
{
    uint8_t bytes[4];
    if (!qa_qvm_read(w->role->vm, address, bytes, sizeof(bytes), error)) return false;
    *out = qa_load_i32le(bytes); return true;
}
bool q3_weapons_write(application_q3_weapons *w, uint32_t address, int32_t value, qa_error *error)
{
    uint8_t bytes[4]; qa_store_u32le(bytes, (uint32_t)value);
    return qa_qvm_write(w->role->vm, address, (qa_bytes){bytes, sizeof(bytes)}, error);
}
static bool restoration_current(application_q3_weapons *, const q3_weapon_actor *);
static bool projection_end(application_q3_weapons *w, q3_weapon_projection **owner,
    bool restore, qa_error *error)
{
    q3_weapon_projection *projection = owner ? *owner : NULL;
    if (!projection) return true;
    projection->restore = restore;
    bool current = !projection->actor_record || restoration_current(w, &projection->actor);
    bool ok = qa_qvm_source_words_end(&projection->lease, restore && current, error);
    if (!projection->lease) {
        q3_weapon_projection **cursor = &w->projections;
        while (*cursor != projection) cursor = &(*cursor)->next;
        *cursor = projection->next; free(projection);
    }
    /* A refused nested-owner close stays in this runtime's actual lifetime,
     * never in a vanished local variable. Destroy retries after scopes drain. */
    *owner = NULL; return ok;
}
static bool projection_begin(application_q3_weapons *w, const qa_qvm_source_word *words,
    const uint32_t *capture, size_t count, bool observed, const q3_weapon_actor *actor,
    q3_weapon_projection **out, qa_error *error)
{
    q3_weapon_projection *projection = calloc(1, sizeof(*projection));
    if (!projection) return application_fail(error, QA_ERROR_MEMORY, "Retaining original action restoration owner");
    projection->restore = true; projection->next = w->projections; w->projections = projection;
    if (actor) { projection->actor = *actor; projection->actor_record = true; }
    *out = projection;
    bool ok;
    if (capture) ok = observed ? qa_qvm_source_words_capture_observed(w->role->vm, w->role->image,
        capture, count, &projection->lease, error) : qa_qvm_source_words_capture(w->role->vm,
        w->role->image, capture, count, &projection->lease, error);
    else ok = observed ? qa_qvm_source_words_begin_observed(w->role->vm, w->role->image,
        words, count, &projection->lease, error) : qa_qvm_source_words_begin(w->role->vm,
        w->role->image, words, count, &projection->lease, error);
    if (!ok) { qa_error cleanup = {0}; (void)projection_end(w, out, true, &cleanup); }
    return ok;
}
static bool located(application_q3_weapons *w, uint32_t slot, q3_weapon_actor *out, qa_error *error)
{
    qa_q3_host_game_data data;
    if (!qa_q3_host_game_data_read(w->role->host, &data) || slot >= data.client_count || slot >= data.entity_count)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original weapon actor has no located source record");
    if (data.client_stride != w->profile.client_stride || data.entity_stride != w->profile.entity_stride)
        return application_fail(error, QA_ERROR_FORMAT, "Original weapon records changed their admitted layout");
    uint64_t entity = data.entities_address + (uint64_t)slot * data.entity_stride,
        player = data.clients_address + (uint64_t)slot * data.client_stride;
    if (entity + w->profile.entity_stride > qa_qvm_memory_size(w->role->vm) ||
        player + w->profile.client_stride > qa_qvm_memory_size(w->role->vm) || entity > UINT32_MAX || player > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Original weapon player leaves its real source allocation");
    int32_t pointer; qa_actor_id actor;
    if (!q3_weapons_read(w, (uint32_t)entity + w->profile.client_pointer, &pointer, error) ||
        !qa_q3_host_actor(w->role->host, slot, false, &actor, error)) return false;
    if ((uint32_t)pointer != player || !actor.registry ||
        !qa_actors_get(qa_session_actors(w->role->engine->provider->application->session), actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original weapon client lost its actual full actor identity");
    *out = (q3_weapon_actor){actor, data, slot, (uint32_t)entity, (uint32_t)player};
    return true;
}
bool q3_weapons_actor(application_q3_weapons *w, qa_actor_id actor, q3_weapon_actor *out, qa_error *error)
{
    uint32_t slot;
    if (!w || !out || !w->role || w->role->retired || w->role->kind != QA_QVM_GAME ||
        w->role->weapons != w || !w->role->vm || !w->role->host || w->role->image == NULL ||
        !qa_sha256_equal(qa_qvm_image_digest(w->role->image), &w->profile.digest) || w->role->abi != w->profile.abi ||
        qa_qvm_get_role(w->role->vm) != QA_QVM_GAME || qa_qvm_get_abi(w->role->vm) != w->profile.abi ||
        !qa_sha256_equal(qa_qvm_digest(w->role->vm), &w->profile.digest) ||
        !qa_q3_host_actor_slot(w->role->host, actor, &slot, error) || !located(w, slot, out, error))
        return (error && error->code != QA_OK) ? false :
            application_fail(error, QA_ERROR_NOT_FOUND, "Original weapon actor lost its retained GAME runtime");
    return qa_actor_id_equal(out->actor, actor) ||
        application_fail(error, QA_ERROR_NOT_FOUND, "Original weapon source slot now belongs to another actor");
}
bool q3_weapons_current(application_q3_weapons *w, const q3_weapon_actor *actor)
{
    q3_weapon_actor actual;
    return q3_weapons_actor(w, actor->actor, &actual, NULL) && actual.slot == actor->slot &&
        actual.entity == actor->entity && actual.player == actor->player &&
        actual.data.entity_stride == actor->data.entity_stride && actual.data.client_stride == actor->data.client_stride;
}
static bool restoration_current(application_q3_weapons *w, const q3_weapon_actor *actor)
{
    q3_weapon_actor actual;
    q3g_role *role = w ? w->role : NULL;
    return role && role->weapons == w && role->vm && role->host && role->image &&
        role->abi == w->profile.abi && qa_qvm_get_role(role->vm) == QA_QVM_GAME &&
        qa_qvm_get_abi(role->vm) == w->profile.abi &&
        qa_sha256_equal(qa_qvm_image_digest(role->image), &w->profile.digest) &&
        qa_sha256_equal(qa_qvm_digest(role->vm), &w->profile.digest) &&
        located(w, actor->slot, &actual, NULL) && qa_actor_id_equal(actual.actor, actor->actor) &&
        actual.entity == actor->entity && actual.player == actor->player &&
        actual.data.entity_stride == actor->data.entity_stride && actual.data.client_stride == actor->data.client_stride;
}
static bool matches(application_q3_weapons *w, const q3_weapon_actor *actor,
    const application_q3_weapon_test *tests, size_t count, bool *out, qa_error *error)
{
    *out = true;
    for (size_t i = 0; i < count; ++i) {
        int32_t value;
        if (!q3_weapons_read(w, actor->player + tests[i].offset, &value, error)) return false;
        if (tests[i].masked) { uint32_t bits = (uint32_t)value & tests[i].mask; memcpy(&value, &bits, sizeof(value)); }
        if (tests[i].at_most ? value > tests[i].value : value != tests[i].value) { *out = false; break; }
    }
    return true;
}
static bool dispatcher_actor(application_q3_weapons *w, const qa_qvm_call *call,
    q3_weapon_actor *out, bool *present, qa_error *error)
{
    *present = false;
    int32_t word = 0;
    bool ok = w->profile.pointer_global ? q3_weapons_read(w, w->profile.pointer_base, &word, error) :
        qa_qvm_call_argument(call, w->profile.pointer_base, &word, error);
    uint32_t pointer = (uint32_t)word;
    for (size_t i = 0; ok && i < w->profile.indirection_count; ++i) {
        if (pointer > UINT32_MAX - w->profile.indirections[i])
            return application_fail(error, QA_ERROR_FORMAT, "Original weapon pointer path overflowed");
        ok = q3_weapons_read(w, pointer + w->profile.indirections[i], &word, error); pointer = (uint32_t)word;
    }
    if (!ok) return false;
    if (pointer > UINT32_MAX - w->profile.pointer_offset)
        return application_fail(error, QA_ERROR_FORMAT, "Original weapon pointer offset overflowed");
    pointer += w->profile.pointer_offset;
    qa_q3_host_game_data data;
    if (!qa_q3_host_game_data_read(w->role->host, &data) || !data.client_stride ||
        pointer < data.clients_address || (pointer - data.clients_address) % data.client_stride)
        return application_fail(error, QA_ERROR_FORMAT, "Original weapon dispatcher has no located client pointer");
    uint64_t slot = (pointer - data.clients_address) / data.client_stride;
    if (slot >= data.client_count || slot > UINT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Original weapon dispatcher client is outside its source roster");
    qa_actor_id actor;
    if (!qa_q3_host_actor(w->role->host, (uint32_t)slot, false, &actor, error)) return false;
    if (!actor.registry) return true;
    if (!located(w, (uint32_t)slot, out, error)) return false;
    *present = true; return true;
}
typedef struct weapon_branch_context {
    application_q3_weapons *weapons;
    q3_weapon_dispatch *scope;
    bool unselected;
} weapon_branch_context;
static bool branch(void *context, const qa_qvm_call *call, bool original, bool *taken, qa_error *error)
{
    weapon_branch_context *b = context;
    b->scope->reached = true; *taken = original;
    if (!q3_weapons_current(b->weapons, &b->scope->actor)) {
        bool cancelled;
        return qa_qvm_call_cancelled(call, &cancelled, error) &&
            (cancelled || qa_qvm_cancel(b->scope->call, error));
    }
    *taken = b->weapons->services.selected(b->weapons->services.context, b->scope->actor.actor) ? original : b->unselected;
    return true;
}
static bool dispatch(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_weapons *w = context;
    q3_weapon_actor actor; bool present;
    if (!dispatcher_actor(w, call, &actor, &present, error)) return false;
    q3_weapon_evaluation *evaluation = w->evaluation;
    if (evaluation && evaluation->kind == Q3_WEAPON_DELAY && !evaluation->entered) {
        if (!present || !qa_actor_id_equal(evaluation->actor.actor, actor.actor) || !q3_weapons_current(w, &evaluation->actor))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Original weapon delay lost its admitted actor");
        evaluation->entered = true;
        return qa_qvm_evaluate_call_region(call, &w->profile.delay, &evaluation->input, result, error);
    }
    if (!present) return qa_qvm_proceed(call, result, error);
    size_t count = w->profile.predicate_count;
    qa_qvm_branch_binding *bindings = calloc(count, sizeof(*bindings));
    weapon_branch_context *contexts = calloc(count, sizeof(*contexts));
    if (!bindings || !contexts) { free(bindings); free(contexts); return application_fail(error, QA_ERROR_MEMORY, "Retaining invocation-owned original weapon decisions"); }
    q3_weapon_dispatch scope = {.previous = w->dispatch, .actor = actor, .call = call};
    w->dispatch = &scope;
    for (size_t i = 0; i < count; ++i) {
        contexts[i] = (weapon_branch_context){w, &scope, w->profile.predicates[i].unselected};
        bindings[i] = (qa_qvm_branch_binding){w->profile.predicates[i].instruction, branch, contexts + i};
    }
    application_q3_weapon_preparation preparation = {0};
    bool ok = qa_qvm_bind_branches(call, bindings, count, error);
    if (ok && w->services.prepare_weapon) ok = w->services.prepare_weapon(w->services.context, actor.actor, call, &preparation, error);
    if (ok) ok = qa_qvm_proceed(call, result, error);
    qa_error cleanup = {0};
    if (preparation.finish && !preparation.finish(preparation.context, &cleanup)) {
        if (ok && error) *error = cleanup;
        ok = false;
    }
    if (ok && q3_weapons_current(w, &actor)) ok = w->services.completed(w->services.context, actor.actor, scope.reached, error);
    w->dispatch = scope.previous; free(contexts); free(bindings); return ok;
}
static bool request(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_weapons *w = context;
    q3_weapon_dispatch *scope = w->dispatch;
    if (!scope || !q3_weapons_current(w, &scope->actor)) return qa_qvm_proceed(call, result, error);
    bool accepted; int32_t requested;
    if (!matches(w, &scope->actor, w->profile.accepted, w->profile.accepted_count, &accepted, error) ||
        !qa_qvm_call_argument(call, w->profile.request_argument, &requested, error)) return false;
    if (!accepted && !w->services.attempted(w->services.context, scope->actor.actor, requested, error)) return false;
    bool ok = qa_qvm_proceed(call, result, error);
    if (ok && !accepted && q3_weapons_current(w, &scope->actor)) {
        bool after;
        ok = matches(w, &scope->actor, w->profile.accepted, w->profile.accepted_count, &after, error);
        if (ok && after) ok = w->services.accepted(w->services.context, scope->actor.actor, requested, error);
    }
    return ok;
}
static bool entity_actor(application_q3_weapons *w, const qa_qvm_call *call, uint32_t argument,
    q3_weapon_actor *out, bool *present, qa_error *error)
{
    *present = false; int32_t entity; qa_q3_host_game_data data;
    if (!qa_qvm_call_argument(call, argument, &entity, error)) return false;
    uint32_t pointer = (uint32_t)entity;
    if (!qa_q3_host_game_data_read(w->role->host, &data) || !data.entity_stride || pointer < data.entities_address ||
        (pointer - data.entities_address) % data.entity_stride)
        return application_fail(error, QA_ERROR_FORMAT, "Original weapon action has no actual entity pointer");
    uint64_t slot = (pointer - data.entities_address) / data.entity_stride;
    if (slot >= data.client_count) return true;
    qa_actor_id actor;
    if (!qa_q3_host_actor(w->role->host, (uint32_t)slot, false, &actor, error)) return false;
    if (!actor.registry) return true;
    if (!located(w, (uint32_t)slot, out, error)) return false;
    *present = true; return true;
}
typedef struct weapon_grant_scope { application_q3_weapons *weapons; q3_weapon_actor actor; const qa_qvm_call *call; } weapon_grant_scope;
typedef struct weapon_grant_branch { weapon_grant_scope *scope; application_q3_weapon_grant category; } weapon_grant_branch;
static bool give_branch(void *context, const qa_qvm_call *call, bool original, bool *taken, qa_error *error)
{
    (void)call; weapon_grant_branch *binding = context;
    weapon_grant_scope *scope = binding->scope; application_q3_weapons *w = scope->weapons;
    *taken = original;
    return !q3_weapons_current(w, &scope->actor) || w->services.give(w->services.context, scope->actor.actor,
        binding->category, error);
}
static bool named_done(void *context, const qa_qvm_call *call, qa_error *error)
{
    weapon_grant_scope *scope = context; application_q3_weapons *w = scope->weapons;
    int32_t item, name;
    if (!qa_qvm_local_word(call, w->profile.give.item, &item, error)) return false;
    if (item || !q3_weapons_current(w, &scope->actor)) return true;
    qa_bytes text;
    if (!qa_qvm_local_word(call, w->profile.give.name, &name, error) || !qa_qvm_read_string(w->role->vm, name, &text, error)) return false;
    bool handled;
    if (!w->services.give_item(w->services.context, scope->actor.actor, text, &handled, error)) return false;
    bool cancelled;
    return !handled || (qa_qvm_call_cancelled(call, &cancelled, error) && (cancelled || qa_qvm_cancel(scope->call, error)));
}
static bool named_enter(void *context, const qa_qvm_call *call, bool *skip, qa_error *error)
{
    (void)context; (void)call; (void)error;
    *skip = false; return true;
}
static bool give(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_weapons *w = context; weapon_grant_scope scope = {.weapons = w, .call = call}; bool present;
    if (!entity_actor(w, call, w->profile.give.argument, &scope.actor, &present, error)) return false;
    if (!present) return qa_qvm_proceed(call, result, error);
    weapon_grant_branch contexts[] = {{&scope, APPLICATION_Q3_GIVE_WEAPONS}, {&scope, APPLICATION_Q3_GIVE_AMMO}};
    qa_qvm_branch_binding branches[] = {{w->profile.give.weapons, give_branch, contexts}, {w->profile.give.ammo, give_branch, contexts + 1}};
    qa_qvm_region_binding region = {.entry = w->profile.give.named.entry, .join = w->profile.give.named.join,
        .enter = named_enter, .completed = named_done, .context = &scope};
    ++w->calls;
    bool ok = qa_qvm_bind_branches(call, branches, 2, error) && qa_qvm_bind_regions(call, &region, 1, error) && qa_qvm_proceed(call, result, error);
    --w->calls; return ok;
}
typedef struct weapon_drop_scope {
    application_q3_weapons *weapons;
    q3_weapon_actor actor;
    q3_weapon_projection *projection;
} weapon_drop_scope;
static bool drop_done(void *context, const qa_qvm_call *call, qa_error *error)
{
    (void)call; weapon_drop_scope *scope = context;
    return projection_end(scope->weapons, &scope->projection, q3_weapons_current(scope->weapons, &scope->actor), error);
}
static bool drop_enter(void *context, const qa_qvm_call *call, bool *skip, qa_error *error)
{
    (void)call; weapon_drop_scope *scope = context; application_q3_weapons *w = scope->weapons;
    *skip = false;
    application_q3_weapon_drop projected = {0};
    if (!w->services.drop(w->services.context, scope->actor.actor, &projected, error)) return false;
    if (!projected.present) return true;
    if (!q3_weapons_current(w, &scope->actor)) return true;
    bool known = projected.weapon == 0;
    for (size_t i = 0; i < w->profile.catalog_count; ++i) if (projected.weapon == w->profile.catalog[i].weapon) known = true;
    if (!known || (w->profile.drop.inventory && !projected.inventory) ||
        (projected.word_count && !projected.words) || projected.word_count >= SIZE_MAX / sizeof(qa_qvm_source_word))
        return application_fail(error, QA_ERROR_FORMAT, "Selected death drop lacks its original inventory projection");
    size_t count = w->profile.drop.inventory ? projected.word_count + 1 : 2;
    qa_qvm_source_word *words = malloc(count * sizeof(*words));
    if (!words) return application_fail(error, QA_ERROR_MEMORY, "Retaining original selected death-drop words");
    words[0] = (qa_qvm_source_word){scope->actor.entity + w->profile.drop.weapon, projected.weapon};
    if (w->profile.drop.inventory) {
        if (projected.word_count) memcpy(words + 1, projected.words, projected.word_count * sizeof(*words));
    } else words[1] = (qa_qvm_source_word){scope->actor.player + w->profile.drop.ammo + (uint32_t)projected.weapon * 4, projected.ammo};
    bool ok = projection_begin(w, words, NULL, count, false, &scope->actor, &scope->projection, error);
    free(words); return ok;
}
static bool drop(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_weapons *w = context; weapon_drop_scope scope = {.weapons = w}; bool present;
    if (!entity_actor(w, call, w->profile.drop.argument, &scope.actor, &present, error)) return false;
    if (!present) return qa_qvm_proceed(call, result, error);
    qa_qvm_region_binding region = {.entry = w->profile.drop.region.entry, .join = w->profile.drop.region.join,
        .enter = drop_enter, .completed = drop_done, .context = &scope};
    ++w->calls;
    bool ok = qa_qvm_bind_regions(call, &region, 1, error) && qa_qvm_proceed(call, result, error);
    qa_error cleanup = {0};
    if (!drop_done(&scope, call, &cleanup)) { if (ok && error) *error = cleanup; ok = false; }
    --w->calls; return ok;
}
typedef struct weapon_damage_scope { application_q3_weapons *weapons; q3_weapon_evaluation *evaluation; const qa_qvm_call *call; } weapon_damage_scope;
static bool damage_stop(void *context, const qa_qvm_call *call, bool *skip, qa_error *error)
{
    weapon_damage_scope *scope = context; int32_t result;
    *skip = false;
    if (!q3_weapons_read(scope->weapons, scope->weapons->profile.damage.result, &result, error)) return false;
    memcpy(&scope->evaluation->damage, &result, sizeof(result)); scope->evaluation->produced = true;
    bool cancelled;
    return qa_qvm_call_cancelled(call, &cancelled, error) && (cancelled || qa_qvm_cancel(scope->call, error));
}
static bool effect_hook(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_weapons *w = context; q3_weapon_evaluation *evaluation = w->evaluation;
    bool damage = call->instruction == w->profile.damage.entry;
    if (!evaluation || evaluation->entered || (damage ? evaluation->kind != Q3_WEAPON_DAMAGE :
        evaluation->kind != Q3_WEAPON_TELEPORT && evaluation->kind != Q3_WEAPON_OBJECTIVES)) return qa_qvm_proceed(call, result, error);
    q3_weapon_actor actor; bool present;
    if (!entity_actor(w, call, 0, &actor, &present, error)) return false;
    if (!present || !qa_actor_id_equal(actor.actor, evaluation->actor.actor) || !q3_weapons_current(w, &evaluation->actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original player effect lost its entered actor");
    evaluation->entered = true;
    if (!damage) return qa_qvm_evaluate_call_region(call,
        evaluation->kind == Q3_WEAPON_OBJECTIVES ? &w->profile.objectives : &w->profile.teleport, NULL, result, error);
    weapon_damage_scope scope = {w, evaluation, call};
    qa_qvm_region_binding region = {.entry = w->profile.damage.stop.entry, .join = w->profile.damage.stop.join, .enter = damage_stop, .context = &scope};
    return qa_qvm_bind_regions(call, &region, 1, error) && qa_qvm_proceed(call, result, error);
}

bool application_q3_weapons_idle(const application_q3_weapons *w)
{ return !w || (!w->dispatch && !w->evaluation && !w->projections && !w->calls && !qa_qvm_active(w->role->vm)); }
bool application_q3_weapons_cleanup_ready(const application_q3_weapons *w)
{
    return w && !w->dispatch && !w->evaluation && !w->calls && !w->role->engine->calls &&
        qa_qvm_source_returned(w->role->vm) && w->projections &&
        (!w->projections->lease || qa_qvm_source_words_is_last(w->projections->lease));
}
bool application_q3_weapons_cleanup(application_q3_weapons *w, qa_error *error)
{
    if (!w) return true;
    if (w->dispatch || w->evaluation || w->calls || w->role->engine->calls || !qa_qvm_source_returned(w->role->vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon cleanup requires its returned Source boundary");
    while (application_q3_weapons_cleanup_ready(w)) {
        q3_weapon_projection *projection = w->projections;
        if (!projection_end(w, &projection, projection->restore, error)) return false;
    }
    return true;
}
bool application_q3_weapons_destroy(application_q3_weapons **owner, qa_error *error)
{
    application_q3_weapons *w = owner ? *owner : NULL;
    if (!w) return true;
    if (!application_q3_weapons_cleanup(w, error)) return false;
    if (!application_q3_weapons_idle(w)) return application_fail(error, QA_ERROR_ARGUMENT, "Original weapons retain an actual action scope");
    while (w->binding_count) {
        if (!qa_qvm_unbind(w->role->vm, w->bindings[w->binding_count - 1], error)) return false;
        w->bindings[--w->binding_count] = 0;
    }
    application_q3_weapon_profile_free(&w->profile); free(w); *owner = NULL; return true;
}
bool application_q3_weapons_create(q3g_role *role, application_q3_weapon_profile *profile,
    const application_q3_weapon_services *services, application_q3_weapons **out, qa_error *error)
{
    if (!role || !role->vm || !role->image || role->kind != QA_QVM_GAME || !profile || !services || !out || *out ||
        (profile->present && (!qa_sha256_equal(&profile->digest, qa_qvm_image_digest(role->image)) || profile->abi != role->abi ||
         !services->selected || !services->attempted || !services->accepted || !services->completed ||
         !services->give || !services->give_item || !services->drop)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapons require their genuine qualified actions and source owner");
    if (!profile->present) { application_q3_weapon_profile_free(profile); return true; }
    application_q3_weapons *w = calloc(1, sizeof(*w));
    if (!w) return application_fail(error, QA_ERROR_MEMORY, "Retaining original primary weapon owner");
    w->role = role; w->profile = *profile; *profile = (application_q3_weapon_profile){0}; w->services = *services; *out = w;
    const uint32_t entries[] = {w->profile.dispatcher, w->profile.request, w->profile.give.entry,
        w->profile.drop.entry, w->profile.damage.entry, w->profile.teleport_entry};
    const qa_qvm_function_hook hooks[] = {dispatch, request, give, drop, effect_hook, effect_hook};
    for (size_t i = 0; i < 6; ++i) {
        if (!qa_qvm_bind_function(role->vm, entries[i], true, hooks[i], w, &w->bindings[i], error)) {
            qa_error cleanup = {0}; (void)application_q3_weapons_destroy(out, &cleanup); return false;
        }
        ++w->binding_count;
    }
    return true;
}
const application_q3_weapon_profile *application_q3_weapons_profile(const application_q3_weapons *w)
{ return w ? &w->profile : NULL; }
bool application_q3_weapons_catalog_refresh(application_q3_weapons *w,
    const application_q3_weapon_catalog_entry *entries, size_t count, qa_error *error)
{
    if (!w || !w->role->image || !w->role->vm || w->role->abi != w->profile.abi ||
        !qa_sha256_equal(qa_qvm_image_digest(w->role->image), &w->profile.digest))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon catalog lost its retained source owner");
    return application_q3_weapon_profile_catalog(&w->profile, entries, count, error);
}
size_t application_q3_weapons_descriptor_count(const application_q3_weapons *w)
{ return w ? 6 : 0; }
bool application_q3_weapons_descriptors(const application_q3_weapons *w,
    qa_qvm_saved_function *out, size_t count, qa_error *error)
{
    if (count != application_q3_weapons_descriptor_count(w) || (count && !out) ||
        !application_q3_weapons_idle(w) || (w && w->binding_count != count))
        return application_fail(error, QA_ERROR_FORMAT, "Original weapon descriptors differ from their actual idle constructor");
    if (!w) return true;
    const uint32_t entries[] = {w->profile.dispatcher, w->profile.request, w->profile.give.entry,
        w->profile.drop.entry, w->profile.damage.entry, w->profile.teleport_entry};
    const qa_qvm_function_hook hooks[] = {dispatch, request, give, drop, effect_hook, effect_hook};
    for (size_t i = 0; i < count; ++i) {
        if (!w->bindings[i]) return application_fail(error, QA_ERROR_FORMAT, "Original weapon callback identity is missing");
        out[i] = (qa_qvm_saved_function){w->bindings[i], entries[i], true, hooks[i], (void *)w};
    }
    return true;
}
void application_q3_weapons_adopt(application_q3_weapons *w, const qa_qvm_binding bindings[6])
{ if (w) memcpy(w->bindings, bindings, sizeof(w->bindings)); }

bool application_q3_weapons_settled(application_q3_weapons *w, qa_actor_id actor, bool *out, qa_error *error)
{
    q3_weapon_actor source;
    return out && q3_weapons_actor(w, actor, &source, error) && matches(w, &source, w->profile.settled, w->profile.settled_count, out, error);
}
bool application_q3_weapons_active(application_q3_weapons *w, qa_actor_id actor, qa_item_id *out, qa_error *error)
{
    q3_weapon_actor source; int32_t value;
    if (!out || !q3_weapons_actor(w, actor, &source, error) || !q3_weapons_read(w, source.player + w->profile.selection_offset, &value, error)) return false;
    if (!value) { *out = 0; return true; }
    for (size_t i = 0; i < w->profile.catalog_count; ++i) if (value == w->profile.catalog[i].weapon) { *out = w->profile.catalog[i].item; return true; }
    return application_fail(error, QA_ERROR_FORMAT, "Original GAME selected an undeclared source weapon");
}
bool application_q3_weapons_available(application_q3_weapons *w, qa_actor_id actor, bool attacking, bool *out, qa_error *error)
{
    q3_weapon_actor source; int32_t type, health, team, flags;
    if (!out || !q3_weapons_actor(w, actor, &source, error) ||
        !q3_weapons_read(w, source.player + w->profile.availability.movement_type, &type, error) ||
        !q3_weapons_read(w, source.player + w->profile.availability.health, &health, error) ||
        !q3_weapons_read(w, source.player + w->profile.availability.team, &team, error) ||
        !q3_weapons_read(w, source.player + w->profile.availability.flags, &flags, error)) return false;
    bool available = health > 0 && team != w->profile.availability.spectator_team &&
        (!attacking || !((uint32_t)flags & (uint32_t)w->profile.availability.respawn_flag));
    for (size_t i = 0; available && i < w->profile.availability.excluded_count; ++i)
        if (type == w->profile.availability.excluded[i]) available = false;
    *out = available; return true;
}
bool application_q3_weapons_selection_for_item(application_q3_weapons *w, qa_actor_id actor,
    qa_item_id item, int32_t *out, qa_error *error)
{
    q3_weapon_actor source;
    if (!item || !out) return application_fail(error, QA_ERROR_ARGUMENT,
        "Original weapon intent requires its canonical item and output");
    if (!q3_weapons_actor(w, actor, &source, error)) return false;
    for (size_t i = 0; i < w->profile.catalog_count; ++i)
        if (w->profile.catalog[i].item == item) { *out = w->profile.catalog[i].weapon; return true; }
    return application_fail(error, QA_ERROR_NOT_FOUND,
        "Original weapon intent has no declared Source selection value");
}
static bool player_read(application_q3_weapons *w, qa_actor_id actor, uint32_t offset, int32_t *out, qa_error *error)
{ q3_weapon_actor source; return out && q3_weapons_actor(w, actor, &source, error) && q3_weapons_read(w, source.player + offset, out, error); }
bool application_q3_weapons_max_health(application_q3_weapons *w, qa_actor_id actor, int32_t *out, qa_error *error)
{ return w && player_read(w, actor, w->profile.max_health, out, error); }
bool application_q3_weapons_set_max_health(application_q3_weapons *w, qa_actor_id actor, int32_t value, qa_error *error)
{
    q3_weapon_actor source;
    if (value <= 0) return application_fail(error, QA_ERROR_ARGUMENT, "Original maximum health requires a positive int32");
    if (!q3_weapons_actor(w, actor, &source, error) ||
        !q3_weapons_write(w, source.player + w->profile.max_health, value, error)) return false;
    if (!q3_weapons_current(w, &source)) return application_fail(error, QA_ERROR_NOT_FOUND,
        "Original maximum health replaced its Source player");
    return q3_weapons_write(w, source.player + w->profile.persistent_max_health, value, error);
}
bool application_q3_weapons_powerup_until(application_q3_weapons *w, qa_actor_id actor, application_q3_weapon_power power, int32_t *out, qa_error *error)
{ return w && (unsigned)power < 3 && player_read(w, actor, w->profile.powerups[power], out, error); }
bool application_q3_weapons_score(application_q3_weapons *w, qa_actor_id actor, int32_t *out, qa_error *error)
{ return (w && w->profile.has_match) ? player_read(w, actor, w->profile.score, out, error) : application_fail(error, QA_ERROR_ARGUMENT, "Original source score has no declaration"); }
bool application_q3_weapons_set_score(application_q3_weapons *w, qa_actor_id actor, int32_t value, qa_error *error)
{
    q3_weapon_actor source;
    if (!w || !w->profile.has_match) return application_fail(error, QA_ERROR_ARGUMENT, "Original source score has no declaration");
    return q3_weapons_actor(w, actor, &source, error) && q3_weapons_write(w, source.player + w->profile.score, value, error);
}
bool application_q3_weapons_canonical_team(application_q3_weapons *w, qa_actor_id actor,
    qa_team_id original, qa_team_id *out, qa_error *error)
{
    q3_weapon_actor source;
    if (!out || !q3_weapons_actor(w, actor, &source, error)) return false;
    *out = original;
    if (w->profile.has_match)
        for (size_t i = 0; i < w->profile.team_count; ++i)
            if (w->profile.teams[i].source == original) { *out = w->profile.teams[i].team; break; }
    return true;
}
bool application_q3_weapons_team_command(application_q3_weapons *w, qa_actor_id actor,
    qa_team_id team, const application_q3_weapon_team_command **out, qa_error *error)
{
    q3_weapon_actor source;
    if (!out || !q3_weapons_actor(w, actor, &source, error)) return false;
    if (w->profile.has_match)
        for (size_t i = 0; i < w->profile.team_count; ++i)
            if (w->profile.teams[i].team == team) { *out = w->profile.teams + i; return true; }
    return application_fail(error, QA_ERROR_ARGUMENT, "Shared team has no declared original command");
}
bool application_q3_weapons_water_level(application_q3_weapons *w, qa_actor_id actor, int32_t *out, qa_error *error)
{
    q3_weapon_actor source; int32_t movement, player;
    if (!out || !q3_weapons_actor(w, actor, &source, error) || !q3_weapons_read(w, w->profile.delay_global, &movement, error)) return false;
    if (movement > 0 && (uint64_t)(uint32_t)movement + w->profile.delay_player + 4 <= qa_qvm_memory_size(w->role->vm)) {
        if (!q3_weapons_read(w, (uint32_t)movement + w->profile.delay_player, &player, error)) return false;
        if ((uint32_t)player == source.player) return q3_weapons_read(w, (uint32_t)movement + w->profile.water_movement, out, error);
    }
    return q3_weapons_read(w, source.entity + w->profile.water_entity, out, error);
}

bool application_q3_weapons_prediction_read(application_q3_weapons *w, qa_actor_id actor,
    application_q3_weapon_prediction *out, qa_error *error)
{
    q3_weapon_actor source; qa_q3_player player;
    if (!out || !q3_weapons_actor(w, actor, &source, error) ||
        !qa_qvm_read_player(w->role->vm, (int32_t)source.player, true, &player, error)) return false;
    if (!q3_weapons_current(w, &source))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original prediction lost its current Source player");
    *out = (application_q3_weapon_prediction){player.weapon, player.weaponState, player.weaponTime};
    return true;
}

static bool evaluate(application_q3_weapons *w, const q3_weapon_actor *actor,
    q3_weapon_evaluation_kind kind, int32_t input, int32_t *result, float *damage, qa_error *error)
{
    if (!q3_weapons_current(w, actor)) return application_fail(error, QA_ERROR_NOT_FOUND, "Original weapon effect requires its current actor");
    q3_weapon_evaluation frame = {.previous = w->evaluation, .actor = *actor, .kind = kind, .input = input};
    w->evaluation = &frame;
    uint32_t entry = kind == Q3_WEAPON_DELAY ? w->profile.dispatcher :
        kind == Q3_WEAPON_DAMAGE ? w->profile.damage.entry : w->profile.teleport_entry;
    int32_t entity = (int32_t)actor->entity, value;
    bool ok = qa_qvm_invoke(w->role->vm, entry, kind == Q3_WEAPON_DELAY ? NULL : &entity,
        kind == Q3_WEAPON_DELAY ? 0 : 1, &value, error);
    w->evaluation = frame.previous;
    if (ok && !frame.entered) ok = application_fail(error, QA_ERROR_FORMAT, "Original effect did not enter its retained function hook");
    if (ok && kind == Q3_WEAPON_DAMAGE && (!frame.produced || !isfinite(frame.damage) || frame.damage < 0))
        ok = application_fail(error, QA_ERROR_FORMAT, "Original damage factor did not produce a finite nonnegative value");
    if (ok && result) *result = value;
    if (ok && damage) *damage = frame.damage;
    return ok;
}
bool application_q3_weapons_damage_factor(application_q3_weapons *w, qa_actor_id actor, float *out, qa_error *error)
{
    q3_weapon_actor source;
    if (!out || !q3_weapons_actor(w, actor, &source, error)) return false;
    q3_weapon_projection *lease = NULL;
    if (!projection_begin(w, NULL, &w->profile.damage.result, 1, true, NULL, &lease, error)) return false;
    float value;
    bool ok = evaluate(w, &source, Q3_WEAPON_DAMAGE, 0, NULL, &value, error);
    qa_error cleanup = {0};
    if (!projection_end(w, &lease, true, &cleanup)) { if (ok && error) *error = cleanup; ok = false; }
    if (ok) *out = value;
    return ok;
}
static bool movement_player(application_q3_weapons *w, const q3_weapon_actor *actor,
    q3_weapon_projection **out, qa_error *error)
{
    int32_t movement;
    if (!q3_weapons_read(w, w->profile.delay_global, &movement, error)) return false;
    if (movement <= 0 || (uint64_t)(uint32_t)movement + w->profile.delay_player + 4 > qa_qvm_memory_size(w->role->vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon action has no established Pmove context");
    qa_qvm_source_word word = {(uint32_t)movement + w->profile.delay_player, (int32_t)actor->player};
    return projection_begin(w, &word, NULL, 1, true, NULL, out, error);
}
bool application_q3_weapons_delay(application_q3_weapons *w, qa_actor_id actor, int32_t input, int32_t *out, qa_error *error)
{
    q3_weapon_actor source;
    if (input < 0) return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon delay requires a nonnegative int32");
    if (!out || !q3_weapons_actor(w, actor, &source, error)) return false;
    q3_weapon_projection *lease = NULL;
    if (!movement_player(w, &source, &lease, error)) return false;
    int32_t value;
    bool ok = evaluate(w, &source, Q3_WEAPON_DELAY, input, &value, NULL, error);
    qa_error cleanup = {0};
    if (!projection_end(w, &lease, true, &cleanup)) { if (ok && error) *error = cleanup; ok = false; }
    if (ok) *out = value;
    return ok;
}
bool application_q3_weapons_equipment_delay(application_q3_weapons *w, qa_actor_id actor,
    qa_string_id provider, int32_t input, int32_t *out, qa_error *error)
{
    if (!w) return application_fail(error, QA_ERROR_ARGUMENT, "Equipment cadence lost its original weapon profile");
    const application_q3_weapon_context *context = NULL;
    for (size_t i = 0; i < w->profile.context_count; ++i) if (w->profile.contexts[i].provider == provider) { context = w->profile.contexts + i; break; }
    if (!context) return application_fail(error, QA_ERROR_NOT_FOUND, "Equipment source has no declared original cadence context");
    if (!context->item) return application_q3_weapons_delay(w, actor, input, out, error);
    q3_weapon_actor source;
    if (!q3_weapons_actor(w, actor, &source, error)) return false;
    int32_t selected = 0;
    for (size_t i = 0; i < w->profile.catalog_count; ++i) if (w->profile.catalog[i].item == context->item) selected = w->profile.catalog[i].weapon;
    if (!selected) return application_fail(error, QA_ERROR_FORMAT, "Equipment cadence lost its original selection value");
    qa_qvm_source_word word = {source.player + w->profile.selection_offset, selected};
    q3_weapon_projection *lease = NULL;
    if (!projection_begin(w, &word, NULL, 1, true, &source, &lease, error)) return false;
    bool ok = application_q3_weapons_delay(w, actor, input, out, error);
    qa_error cleanup = {0};
    if (!projection_end(w, &lease, q3_weapons_current(w, &source), &cleanup)) { if (ok && error) *error = cleanup; ok = false; }
    return ok;
}
bool application_q3_weapons_animation(application_q3_weapons *w, qa_actor_id actor, bool melee, qa_error *error)
{
    q3_weapon_actor source;
    if (!q3_weapons_actor(w, actor, &source, error)) return false;
    q3_weapon_projection *lease = NULL;
    if (!movement_player(w, &source, &lease, error)) return false;
    int32_t value = melee ? w->profile.animation.melee : w->profile.animation.attack, result;
    bool ok = qa_qvm_invoke(w->role->vm, w->profile.animation.entry, &value, 1, &result, error);
    qa_error cleanup = {0};
    if (!projection_end(w, &lease, true, &cleanup)) { if (ok && error) *error = cleanup; ok = false; }
    return ok;
}
bool application_q3_weapons_drop_objectives(application_q3_weapons *w, qa_actor_id actor, qa_error *error)
{ q3_weapon_actor source; return q3_weapons_actor(w, actor, &source, error) && evaluate(w, &source, Q3_WEAPON_OBJECTIVES, 0, NULL, NULL, error); }
bool application_q3_weapons_teleport(application_q3_weapons *w, qa_actor_id actor, qa_error *error)
{ q3_weapon_actor source; return q3_weapons_actor(w, actor, &source, error) && evaluate(w, &source, Q3_WEAPON_TELEPORT, 0, NULL, NULL, error); }

typedef struct weapon_scratch_action {
    application_q3_weapons *weapons;
    q3_weapon_actor actor;
    qa_vec3 origin, angles;
    bool spawn;
} weapon_scratch_action;
static bool scratch_action(void *context, qa_qvm *vm, uint32_t offset, qa_error *error)
{
    weapon_scratch_action *action = context; application_q3_weapons *w = action->weapons;
    qa_q3_player player;
    qa_vec3 input = action->angles;
    if (action->spawn) {
        if (!qa_qvm_read_player(vm, (int32_t)action->actor.player, true, &player, error)) return false;
        input = qa_v3(player.origin[0], player.origin[1], player.origin[2]);
    }
    uint8_t bytes[12]; const float values[] = {input.x, input.y, input.z};
    for (size_t i = 0; i < 3; ++i) { uint32_t bits; memcpy(&bits, values + i, sizeof(bits)); qa_store_u32le(bytes + i * 4, bits); }
    if (!qa_qvm_write(vm, offset, (qa_bytes){bytes, sizeof(bytes)}, error)) return false;
    int32_t words[3], result;
    if (action->spawn) { words[0] = (int32_t)offset; words[1] = (int32_t)(offset + 12); words[2] = (int32_t)(offset + 24); }
    else { words[0] = (int32_t)action->actor.entity; words[1] = (int32_t)offset; }
    if (!qa_qvm_invoke(vm, action->spawn ? w->profile.spawn : w->profile.view, words, action->spawn ? 3 : 2, &result, error)) return false;
    if (action->spawn) {
        if (!q3_weapons_current(w, &action->actor)) return application_fail(error, QA_ERROR_NOT_FOUND, "Original spawn selector retired its player");
        uint8_t output[24];
        if (!qa_qvm_read(vm, offset + 12, output, sizeof(output), error)) return false;
        float vector[6];
        for (size_t i = 0; i < 6; ++i) { uint32_t bits = qa_load_u32le(output + i * 4); memcpy(vector + i, &bits, sizeof(bits)); }
        action->origin = qa_v3(vector[0], vector[1], vector[2]); action->angles = qa_v3(vector[3], vector[4], vector[5]);
    }
    return true;
}
bool application_q3_weapons_spawn_point(application_q3_weapons *w, qa_actor_id actor,
    qa_vec3 *origin, qa_vec3 *angles, qa_error *error)
{
    weapon_scratch_action action = {.weapons = w, .spawn = true};
    if (!origin || !angles || !q3_weapons_actor(w, actor, &action.actor, error)) return false;
    ++w->calls;
    bool ok = qa_qvm_source_scratch_run(w->role->vm, w->role->image, 36, scratch_action, &action, error);
    --w->calls;
    if (ok) { *origin = action.origin; *angles = action.angles; }
    return ok;
}
bool application_q3_weapons_teleport_state(application_q3_weapons *w, qa_actor_id actor,
    qa_vec3 origin, qa_vec3 velocity, qa_vec3 angles, int32_t hold, qa_error *error)
{
    weapon_scratch_action action = {.weapons = w, .angles = angles}; qa_q3_player state;
    if (!q3_weapons_actor(w, actor, &action.actor, error) || !qa_qvm_read_player(w->role->vm,
        (int32_t)action.actor.player, true, &state, error)) return false;
    const float position[] = {origin.x, origin.y, origin.z}, motion[] = {velocity.x, velocity.y, velocity.z};
    memcpy(state.origin, position, sizeof(position)); memcpy(state.velocity, motion, sizeof(motion));
    state.groundEntityNum = 1023; state.eFlags ^= 4; state.pmFlags |= 64; state.pmTime = hold;
    ++w->calls;
    bool ok = qa_qvm_write_player(w->role->vm, (int32_t)action.actor.player, true, true, &state, error);
    if (ok && !q3_weapons_current(w, &action.actor)) ok = application_fail(error, QA_ERROR_NOT_FOUND, "Original teleport state retired its player before view selection");
    if (ok) ok = qa_qvm_source_scratch_run(w->role->vm, w->role->image, 12, scratch_action, &action, error);
    --w->calls; return ok;
}
