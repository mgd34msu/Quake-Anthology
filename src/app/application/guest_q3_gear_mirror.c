#include "guest_q3_gear_private.h"

bool q3gear_reserve(application_q3_gear *gear, uint32_t slot, qa_error *error)
{
    if (slot < gear->capacity) return true;
    uint32_t capacity = qa_actors_capacity(qa_session_actors(gear->options.host.session));
    if (slot >= capacity)
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear actor exceeds its shared registry");
#if SIZE_MAX <= UINT32_MAX
    if (capacity > SIZE_MAX/sizeof(*gear->bindings) || capacity > SIZE_MAX/sizeof(*gear->tethers))
        return q3gear_fail(error, QA_ERROR_MEMORY, "Separate QVM gear actor bindings exceed the native allocation extent");
#endif
    q3gear_binding *bindings = calloc(capacity, sizeof(*bindings));
    q3gear_tether *tethers = calloc(capacity, sizeof(*tethers));
    if (!bindings || !tethers) { free(bindings); free(tethers); return q3gear_fail(error, QA_ERROR_MEMORY, "Growing separate QVM gear actor bindings"); }
    if (gear->capacity) {
        memcpy(bindings, gear->bindings, gear->capacity*sizeof(*bindings));
        memcpy(tethers, gear->tethers, gear->capacity*sizeof(*tethers));
    }
    free(gear->bindings); free(gear->tethers);
    gear->bindings = bindings; gear->tethers = tethers; gear->capacity = capacity; return true;
}

bool q3gear_mirror(application_q3_gear *gear, const application_q3_gear_target *input, qa_error *error)
{
    application_q3_gear_target target;
    if (!q3gear_target(gear, input->actor, &target, error) || !q3gear_reserve(gear, target.actor.slot, error)) return false;
    qa_actor_id actor = target.actor;
    q3gear_binding binding = gear->bindings[actor.slot];
    if (binding.actor.registry && (!qa_actor_id_equal(binding.actor, actor) || binding.player != target.player))
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear borrowed actor generation/kind changed without retirement");
    qa_q3_host_game_data layout;
    if (!q3gear_layout(gear, &layout, error)) return false;
    uint32_t slot = 0; int32_t result;
    if (!binding.actor.registry) {
        if (target.player) {
            while (slot < 64 && gear->userinfo[slot]) ++slot;
            if (slot == 64) return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear exhausted its 64 borrowed clients");
            binding = (q3gear_binding){.actor = actor, .pointer = (uint32_t)layout.entities_address + slot*layout.entity_stride,
                .origin = target.body.origin, .player = true};
            if (!q3gear_replace_text(&gear->userinfo[slot], target.userinfo, error) ||
                !qa_q3_host_bind_actor(gear->host, slot, actor, true, error)) return false;
            gear->bindings[actor.slot] = binding;
            int32_t connect[] = {2, (int32_t)slot, 1, 0}, begin[] = {3, (int32_t)slot};
            if (!q3gear_call(gear, 0, connect, 4, &result, error)) return false;
            if (result) {
                qa_bytes reason;
                if (!qa_qvm_read_string(gear->vm, result, &reason, error)) return false;
                qa_error_set(error, QA_ERROR_ARGUMENT, slot, "Separate QVM gear rejected borrowed client: %.*s",
                    (int)(reason.size > 180 ? 180 : reason.size), (const char *)reason.data); return false;
            }
            if(!q3gear_current(gear,error) ||
                !qa_actor_id_equal(gear->bindings[actor.slot].actor,actor) || gear->bindings[actor.slot].retired ||
                !qa_actors_get(qa_session_actors(gear->options.host.session),actor)) return false;
            gear->bindings[actor.slot].connected=true;
            if (!q3gear_call(gear, 0, begin, 2, &result, error)) return false;
            if(!q3gear_current(gear,error) ||
                !qa_actor_id_equal(gear->bindings[actor.slot].actor,actor) || gear->bindings[actor.slot].retired ||
                !qa_actors_get(qa_session_actors(gear->options.host.session),actor)) return false;
            gear->bindings[actor.slot].begun=true;
        } else {
            if (!q3gear_call(gear, gear->definition->callbacks.allocate, NULL, 0, &result, error) ||
                !q3gear_slot(gear, (uint32_t)result, &slot, error)) return false;
            if (slot < 64 || slot >= 1022)
                return q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear allocator returned a reserved client/world slot");
            for (uint32_t i = 0; i < gear->capacity; ++i)
                if ((gear->bindings[i].actor.registry && gear->bindings[i].pointer == (uint32_t)result) ||
                    (gear->tethers[i].actor.registry && gear->tethers[i].hook == (uint32_t)result))
                    return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear allocator reused a live source record");
            binding = (q3gear_binding){.actor = actor, .pointer = (uint32_t)result, .origin = target.body.origin};
            if (!qa_q3_host_bind_actor(gear->host, slot, actor, true, error)) return false;
            gear->bindings[actor.slot] = binding;
        }
    }
    if (!q3gear_target(gear, actor, &target, error) ||
        !q3gear_slot(gear, binding.pointer, &slot, error)) return false;
    if (target.player && strcmp(gear->userinfo[slot], target.userinfo)) {
        if (!q3gear_replace_text(&gear->userinfo[slot], target.userinfo, error)) return false;
        int32_t change[] = {4, (int32_t)slot};
        if (!q3gear_call(gear, 0, change, 2, &result, error) || !q3gear_target(gear, actor, &target, error)) return false;
    }
    qa_q3_entity entity; qa_qvm_entity_shared shared;
    if (!qa_qvm_read_entity(gear->vm, (int32_t)binding.pointer, true, &entity, error) ||
        !qa_qvm_read_shared_entity(gear->vm, (int32_t)binding.pointer, &shared, error)) return false;
    const qa_body_state *body = &target.body;
    if (target.player) {
        uint32_t client = (uint32_t)layout.clients_address + slot*layout.client_stride;
        int32_t source_client;
        if (!q3gear_word(gear, binding.pointer + gear->definition->fields.client, &source_client, error)) return false;
        if ((uint32_t)source_client != client)
            return q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear client pointer differs from its physical slot");
        if (!q3gear_vector_store(gear, client + 20, body->origin, error) ||
            !q3gear_vector_store(gear, client + 32, body->velocity, error) ||
            !q3gear_vector_store(gear, client + 152, body->angles, error) ||
            !q3gear_store(gear, client + 184, (int32_t)target.health, error) ||
            !q3gear_store(gear, client + 164, (int32_t)target.view_height, error) ||
            !q3gear_store(gear, client + 260, target.team, error) ||
            !q3gear_store(gear, client + 4, target.team == 3 ? 2 : target.health <= 0 ? 3 : 0, error)) return false;
        shared.contents = target.team == 3 ? 0 : target.health > 0 ? 0x2000000 : 0x4000000;
        entity.eType = 1;
    } else {
        entity.eType = target.mover ? 4 : 0;
        qa_vec3 delta = {body->origin.x-binding.origin.x, body->origin.y-binding.origin.y, body->origin.z-binding.origin.z};
        if (target.mover && gear->definition->callbacks.move_mover_hooks && (delta.x != 0 || delta.y != 0 || delta.z != 0)) {
            gear->translation = delta; gear->mover_pending = true;
            int32_t words[] = {(int32_t)binding.pointer, 0};
            bool okay = q3gear_call(gear, gear->definition->callbacks.move_mover_hooks, words, 2, &result, error);
            gear->mover_pending = false;
            if (!okay) return false;
            for (uint32_t i = 0; i < gear->capacity; ++i)
                if (gear->tethers[i].tracked && !q3gear_publish(gear, &gear->tethers[i], error)) return false;
        }
    }
    shared.origin = body->origin; shared.angles = body->angles; shared.local_bounds = body->bounds;
    float origin[] = {body->origin.x, body->origin.y, body->origin.z};
    float angles[] = {body->angles.x, body->angles.y, body->angles.z};
    float velocity[] = {body->velocity.x, body->velocity.y, body->velocity.z};
    memcpy(entity.origin, origin, sizeof(origin)); memcpy(entity.angles, angles, sizeof(angles));
    memcpy(entity.pos.base, origin, sizeof(origin)); memcpy(entity.pos.delta, velocity, sizeof(velocity)); entity.pos.time = gear->milliseconds;
    if (!qa_qvm_write_entity(gear->vm, (int32_t)binding.pointer, true, &entity, error) ||
        !qa_qvm_write_shared_entity(gear->vm, (int32_t)binding.pointer, &shared, error) ||
        !q3gear_store(gear, binding.pointer + gear->definition->fields.health, (int32_t)target.health, error) ||
        !q3gear_store(gear, binding.pointer + gear->definition->fields.takedamage, target.health > 0, error)) return false;
    gear->bindings[actor.slot].origin = body->origin; return q3gear_current(gear, error);
}

bool application_q3_gear_userinfo_bound(application_q3_gear *gear,qa_actor_id actor,bool *out,qa_error *error)
{
    if(!out || !gear || !q3gear_current(gear,error)) return false;
    *out=false;
    if(actor.slot>=gear->capacity) return true;
    const q3gear_binding *binding=&gear->bindings[actor.slot];
    if(!binding->actor.registry || !qa_actor_id_equal(binding->actor,actor) || !binding->player) return true;
    if(binding->retired || !binding->connected || !binding->begun || !gear->initialized || gear->restoring)
        return q3gear_fail(error,QA_ERROR_ARGUMENT,"Gear userinfo recipient has not completed its actual Connect and Begin");
    uint32_t slot,actual; qa_q3_host_game_data layout; int32_t client,live;
    if(!q3gear_layout(gear,&layout,error) || !q3gear_slot(gear,binding->pointer,&slot,error) || slot>=64 ||
        !gear->userinfo[slot] || !qa_q3_host_actor_slot(gear->host,actor,&actual,error) || actual!=slot ||
        !q3gear_word(gear,binding->pointer+gear->definition->fields.client,&client,error) ||
        (uint32_t)client!=layout.clients_address+slot*layout.client_stride ||
        !q3gear_word(gear,binding->pointer+gear->definition->fields.inuse,&live,error) || !live)
        return q3gear_fail(error,QA_ERROR_FORMAT,"Gear userinfo recipient lost its real physical client");
    *out=true; return true;
}
bool application_q3_gear_userinfo_changed(application_q3_gear *gear,qa_actor_id actor,qa_error *error)
{
    bool bound=false;
    if(!gear || !application_q3_gear_idle(gear) ||
        !application_q3_gear_userinfo_bound(gear,actor,&bound,error) || !bound)
        return error && error->code!=QA_OK?false:
            q3gear_fail(error,QA_ERROR_ARGUMENT,"Gear userinfo event requires its returned admitted client");
    gear->busy=true;
    application_q3_gear_target target; uint32_t slot; int32_t result;
    bool okay=q3gear_target(gear,actor,&target,error) && target.player && target.userinfo &&
        q3gear_slot(gear,gear->bindings[actor.slot].pointer,&slot,error) &&
        q3gear_replace_text(&gear->userinfo[slot],target.userinfo,error);
    int32_t words[]={4,(int32_t)(okay?slot:0)};
    if(okay) okay=q3gear_call(gear,0,words,2,&result,error) &&
        application_q3_gear_userinfo_bound(gear,actor,&bound,error) && bound;
    return q3gear_leave(gear,okay,error);
}

bool q3gear_forget(application_q3_gear *gear, qa_actor_id actor, qa_error *error)
{
    if (actor.slot >= gear->capacity || !qa_actor_id_equal(gear->bindings[actor.slot].actor, actor)) return true;
    for (uint32_t i = 0; i < gear->capacity; ++i) {
        q3gear_tether tether = gear->tethers[i];
        if (!tether.tracked) continue;
        int32_t target = 0, mover = 0;
        if (tether.hook && (!q3gear_word(gear, tether.hook + gear->definition->fields.target, &target, error) ||
            (gear->definition->fields.mover != UINT32_MAX &&
             !q3gear_word(gear, tether.hook + gear->definition->fields.mover, &mover, error)))) return false;
        if ((qa_actor_id_equal(tether.owner, actor) || qa_actor_id_equal(q3gear_actor(gear, target), actor) ||
            qa_actor_id_equal(q3gear_actor(gear, mover), actor)) && !q3gear_release(gear, tether.owner, true, error)) return false;
    }
    q3gear_binding binding = gear->bindings[actor.slot]; uint32_t slot; int32_t result;
    if (!q3gear_slot(gear, binding.pointer, &slot, error)) return false;
    if (binding.player) {
        int32_t words[] = {5, (int32_t)slot};
        if (!q3gear_call(gear, 0, words, 2, &result, error)) return false;
    } else {
        int32_t words[] = {(int32_t)binding.pointer};
        if (!q3gear_call(gear, gear->definition->callbacks.free, words, 1, &result, error)) return false;
    }
    if (!gear->bindings[actor.slot].retired && !qa_q3_host_detach_actor(gear->host, slot, actor, error)) return false;
    if (binding.player) { free(gear->userinfo[slot]); gear->userinfo[slot] = NULL; }
    gear->bindings[actor.slot] = (q3gear_binding){0}; return true;
}

bool q3gear_flush_releases(application_q3_gear *gear, qa_error *error)
{
    if (gear->draining || qa_qvm_active(gear->vm)) return true;
    gear->draining = true; bool okay = true;
    for (uint32_t i = 0; i < gear->capacity && okay; ++i)
        if (gear->bindings[i].actor.registry && gear->bindings[i].retired)
            okay = q3gear_forget(gear, gear->bindings[i].actor, error);
    for (uint32_t i = 0; i < gear->capacity && okay; ++i)
        if (gear->tethers[i].orphaned) okay = q3gear_release(gear, gear->tethers[i].owner, true, error);
    gear->draining = false; return okay;
}

bool q3gear_leave(application_q3_gear *gear, bool success, qa_error *error)
{
    qa_error cleanup = {0};
    bool released = q3gear_flush_releases(gear, success ? error : &cleanup);
    gear->busy = false; return success && released;
}

bool q3gear_sync(application_q3_gear *gear, qa_error *error)
{
    if (gear->synchronizing) return true;
    gear->synchronizing = true; bool okay = true;
    const qa_actor_registry *actors = qa_session_actors(gear->options.host.session);
    for (uint32_t i = 0; i < gear->capacity && okay; ++i) {
        qa_actor_id actor = gear->bindings[i].actor;
        if (!actor.registry) continue;
        bool present = false; application_q3_gear_target target;
        okay = gear->options.target(gear->options.context, actor, &target, &present, error);
        if (okay && present && !qa_actor_id_equal(target.actor, actor))
            okay = q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear target changed its full shared actor");
        if (okay && !present) okay = q3gear_forget(gear, actor, error);
    }
    uint32_t cursor = 0; const qa_actor_record *record;
    while (okay && qa_actors_next(actors, &cursor, &record)) {
        qa_actor_id actor = record->id;
        application_q3_gear_target target; bool present = false;
        okay = gear->options.target(gear->options.context, actor, &target, &present, error);
        if (okay && present) {
            if (!qa_actor_id_equal(target.actor, actor))
                okay = q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear target changed its full shared actor");
            else okay = q3gear_mirror(gear, &target, error);
        }
    }
    gear->synchronizing = false; return okay;
}
