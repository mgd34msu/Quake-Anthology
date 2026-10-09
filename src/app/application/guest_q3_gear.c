#include "guest_q3_gear_private.h"

static bool enter(application_q3_gear *gear, qa_error *error)
{
    if (!q3gear_current(gear, error)) return false;
    if (gear->busy || gear->restoring || !gear->initialized || !qa_qvm_can_destroy(gear->vm))
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear operation requires its initialized idle executor");
    gear->busy = true; return true;
}

bool application_q3_gear_idle(const application_q3_gear *gear)
{
    return gear && !gear->busy && !gear->synchronizing && !gear->draining &&
        (!gear->vm || qa_qvm_can_destroy(gear->vm)) &&
        (!gear->console || qa_console_idle(gear->console)) && qa_world_idle(gear->options.host.world);
}

bool application_q3_gear_create(const application_q3_gear_options *options, bool restoring,
    application_q3_gear **out, qa_error *error)
{
    if (!options || !out || !options->profile || !options->host.session || !options->host.world || !options->host.mounts ||
        !options->host.owner || !options->host.service_owner ||
        !qa_strings_cstr(qa_session_strings(options->host.session), options->host.owner) ||
        options->host.role != QA_QVM_GAME || options->host.abi != QA_QVM_Q3_MODERN ||
        options->host.command_context.owner != options->host.owner ||
        options->host.command_context.dialect != QA_RULESET_Q3 ||
        options->host.frontend_lifetime || options->host.bots || options->host.script_globals ||
        !options->current || !options->target || !options->damage || !options->velocity ||
        !options->configstring ||
        options->services.session != options->host.session || options->services.world != options->host.world)
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear lacks its real profile/world/target services");
    application_q3_gear *gear = calloc(1, sizeof(*gear));
    if (!gear) return q3gear_fail(error, QA_ERROR_MEMORY, "Allocating separate QVM gear owner");
    gear->options = *options; gear->restoring = restoring;
    gear->definition = application_q3_grapple_profile_definition(options->profile);
    gear->image = (qa_qvm_image *)application_q3_grapple_profile_image(options->profile);
    gear->options.profile = NULL;
    qa_qvm_image_retain(gear->image);
    bool okay = q3gear_replace_text(&gear->path, application_q3_grapple_profile_path(options->profile), error);
    if (okay && restoring) {
        qa_bytes text = options->host.entity_text;
        if ((text.size && !text.data) || text.size == SIZE_MAX)
            okay = q3gear_fail(error, QA_ERROR_ARGUMENT, "Saved separate QVM gear entity text is invalid");
        else {
            gear->entities.data = malloc(text.size + 1); gear->entities.size = text.size;
            if (!gear->entities.data) okay = q3gear_fail(error, QA_ERROR_MEMORY, "Retaining saved separate QVM gear entity text");
            else { if (text.size) memcpy(gear->entities.data, text.data, text.size); gear->entities.data[text.size] = 0; }
        }
    } else if (okay) okay = q3gear_filter_entities(options->host.entity_text, &gear->entities, error);
    if (okay) okay = q3gear_services(gear, error);
    if (okay) {
        gear->lower = qa_q3_host_qvm_options(gear->host, QA_QVM_INTERPRETED);
        qa_qvm_options vm = gear->lower;
        vm.context = gear; vm.syscall = q3gear_syscall;
        vm.checkpoint = q3gear_host_checkpoint; vm.restore = q3gear_host_restore;
        okay = qa_qvm_create(gear->image, &vm, &gear->vm, error) &&
            qa_q3_host_attach_qvm(gear->host, gear->vm, error) && q3gear_bind_hooks(gear, error);
    }
    if (!okay) {
        qa_error cleanup = {0};
        if (gear->vm) { qa_qvm_destroy(gear->vm, &cleanup); qa_q3_host_qvm_consumed(gear->host); }
        if (gear->host) qa_q3_host_destroy(gear->host, &cleanup);
        qa_console_unbind_source(gear->console, qa_cvars_view_identity(gear->cvars), &cleanup);
        qa_cvars_detach_callbacks(gear->cvars); qa_cvars_destroy(gear->cvars);
        qa_qvm_image_release(gear->image); qa_buffer_free(&gear->entities); free(gear->path); free(gear);
        return false;
    }
    *out = gear; return true;
}

bool application_q3_gear_initialize(application_q3_gear *gear, int32_t seed, qa_error *error)
{
    if (!q3gear_current(gear, error)) return false;
    if (gear->initialized || gear->restoring || !application_q3_gear_idle(gear))
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear Init requires its fresh physical executor");
    gear->busy = true;
    int32_t words[] = {0, 0, seed, 0}, result;
    qa_q3_host_game_data layout;
    bool okay = q3gear_call(gear, 0, words, 4, &result, error) && q3gear_layout(gear, &layout, error);
    if (okay) {
        gear->initialized = true;
        size_t executed;
        okay = qa_console_drain(gear->console, 0, &executed, error);
    }
    return q3gear_leave(gear, okay, error);
}

bool q3gear_hook(application_q3_gear *gear, qa_actor_id owner, uint32_t *hook,
    uint32_t *client, qa_error *error)
{
    if (owner.slot >= gear->capacity || !qa_actor_id_equal(gear->bindings[owner.slot].actor, owner) ||
        !gear->bindings[owner.slot].player)
        return q3gear_fail(error, QA_ERROR_NOT_FOUND, "Separate QVM gear owner has no real borrowed client");
    uint32_t entity = gear->bindings[owner.slot].pointer; int32_t live, player, value;
    if (!q3gear_word(gear, entity + gear->definition->fields.inuse, &live, error) ||
        !q3gear_word(gear, entity + gear->definition->fields.client, &player, error)) return false;
    qa_q3_host_game_data layout; uint32_t slot;
    if (!q3gear_layout(gear, &layout, error) || !q3gear_slot(gear, entity, &slot, error)) return false;
    if (!live || slot >= 64 || (uint32_t)player != layout.clients_address + slot*layout.client_stride)
        return q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear owner lost its live physical client pointer");
    if (!q3gear_word(gear, (uint32_t)player + gear->definition->fields.hook, &value, error)) return false;
    if (value) {
        int32_t parent;
        if (!q3gear_slot(gear, (uint32_t)value, &slot, error) ||
            !q3gear_word(gear, (uint32_t)value + gear->definition->fields.inuse, &live, error) ||
            !q3gear_word(gear, (uint32_t)value + gear->definition->fields.parent, &parent, error)) return false;
        if (!live || slot < 64 || slot >= 1022 || (uint32_t)parent != entity)
            return q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear hook is inactive or belongs to another owner");
    }
    *client = (uint32_t)player; *hook = (uint32_t)value; return true;
}

bool q3gear_sound(application_q3_gear *gear, qa_actor_id actor, const char *path,
    bool loop, qa_error *error)
{
    if (!path) return true;
    qa_body_state body; qa_string_id resource;
    if (!qa_world_body_read(gear->options.host.world, actor, &body, error) ||
        !qa_builtin_resource(&gear->options.services, path, &resource, error)) return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_SOUND, .family = QA_GAME_Q3,
        .provider = gear->options.host.owner, .actor = actor, .resource = resource,
        .time_ns = (uint64_t)gear->milliseconds*1000000, .origin = body.origin,
        .direction = body.velocity, .volume = 1, .attenuation = 1, .flags = loop ? 1u : 0u};
    return qa_builtin_emit(&gear->options.services, &event, error);
}

bool q3gear_publish(application_q3_gear *gear, q3gear_tether *entry, qa_error *error)
{
    qa_actor_id owner = entry->owner; uint32_t hook, client;
    if (!q3gear_hook(gear, owner, &hook, &client, error)) return false;
    q3gear_tether tether = *entry;
    if (hook && !qa_actors_get(qa_session_actors(gear->options.host.session), owner)) {
        int32_t words[] = {(int32_t)hook}, result;
        if (!q3gear_call(gear, gear->definition->callbacks.force_release, words, 1, &result, error) ||
            !q3gear_hook(gear, owner, &hook, &client, error)) return false;
        if (hook) return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear retained a hook for a retired owner");
    }
    if (!hook) {
        if (tether.actor.registry) {
            const qa_actor_record *record = qa_actors_get(qa_session_actors(gear->options.host.session), tether.actor);
            if (!record) return q3gear_fail(error, QA_ERROR_NOT_FOUND, "Separate QVM gear tether retired without its owner");
            qa_actor_record released = *record;
            if (!qa_session_release(gear->options.host.session, tether.actor, error) ||
                !qa_q3_host_actor_released(gear->host, released, error)) return false;
        }
        gear->tethers[owner.slot] = (q3gear_tether){0};
    } else {
        uint32_t slot; qa_q3_entity source; qa_qvm_entity_shared shared;
        int32_t flags, mover = 0;
        if (!q3gear_slot(gear, hook, &slot, error) || !qa_qvm_read_entity(gear->vm, (int32_t)hook, true, &source, error) ||
            !qa_qvm_read_shared_entity(gear->vm, (int32_t)hook, &shared, error) ||
            !q3gear_word(gear, client + 12, &flags, error) ||
            (gear->definition->fields.mover != UINT32_MAX &&
             !q3gear_word(gear, hook + gear->definition->fields.mover, &mover, error))) return false;
        bool pulling = ((uint32_t)flags & gear->definition->pulling_flag) != 0;
        qa_body_state body = {.origin = shared.origin, .angles = shared.angles, .bounds = shared.local_bounds,
            .velocity = {source.pos.delta[0], source.pos.delta[1], source.pos.delta[2]}, .ground = qa_actor_reference_lifetime(q3gear_actor(gear, mover))};
        if (pulling && !q3gear_vector(gear, client + 92, &body.origin, error)) return false;
        if (tether.actor.registry && tether.hook != hook)
            return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear replaced a live tether source slot");
        if (!tether.actor.registry) {
            qa_string_id definition;
            if (!qa_strings_intern_cstr(qa_session_strings(gear->options.host.session), "q3:equipment/hook", &definition, error) ||
                !qa_session_allocate(gear->options.host.session, gear->options.host.owner, definition, true, slot, &tether.actor, error)) return false;
            tether.hook = hook; gear->tethers[owner.slot] = tether;
            if (!qa_world_body_create(gear->options.host.world, tether.actor, &body, error) ||
                !qa_q3_host_bind_actor(gear->host, slot, tether.actor, true, error)) return false;
        }
        if (!qa_world_body_write(gear->options.host.world, tether.actor, &body, error) ||
            !qa_world_link(gear->options.host.world, tether.actor, NULL, error)) return false;
        if (pulling && !tether.pulling && !q3gear_sound(gear, tether.actor, gear->definition->presentation.attach_sound, false, error)) return false;
        tether.pulling = pulling; gear->tethers[owner.slot] = tether;
    }
    if (!qa_actors_get(qa_session_actors(gear->options.host.session), owner)) return true;
    qa_vec3 velocity;
    return q3gear_vector(gear, client + 32, &velocity, error) &&
        gear->options.velocity(gear->options.context, owner, velocity, error) && q3gear_current(gear, error);
}

bool q3gear_release(application_q3_gear *gear, qa_actor_id actor, bool force, qa_error *error)
{
    if (actor.slot >= gear->capacity || !gear->tethers[actor.slot].tracked) return true;
    if (!qa_actor_id_equal(gear->tethers[actor.slot].owner, actor))
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear release names another owner generation");
    uint32_t hook, client; int32_t result;
    if (!q3gear_hook(gear, actor, &hook, &client, error)) return false;
    if (hook) {
        int32_t words[] = {(int32_t)hook};
        if (!q3gear_call(gear, force ? gear->definition->callbacks.force_release : gear->definition->callbacks.release,
            words, 1, &result, error)) return false;
    }
    return q3gear_publish(gear, &gear->tethers[actor.slot], error);
}

bool application_q3_gear_admit(application_q3_gear *gear, qa_actor_id actor, qa_error *error)
{
    if (!enter(gear, error)) return false;
    application_q3_gear_target target;
    bool okay = q3gear_target(gear, actor, &target, error);
    if (okay && !target.player) okay = q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear requires an admitted player body");
    if (okay) okay = q3gear_mirror(gear, &target, error);
    return q3gear_leave(gear, okay, error);
}

bool application_q3_gear_fire(application_q3_gear *gear, qa_actor_id actor, qa_error *error)
{
    if (!enter(gear, error)) return false;
    uint32_t previous = 0, client, hook; int32_t result;
    bool okay = q3gear_sync(gear, error) && q3gear_hook(gear, actor, &previous, &client, error);
    if (okay) {
        gear->tethers[actor.slot].owner = actor; gear->tethers[actor.slot].tracked = true;
        if (!previous) {
            int32_t words[16] = {(int32_t)gear->bindings[actor.slot].pointer};
            if (gear->definition->fire_argument_count >= sizeof(words)/sizeof(*words))
                okay = q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear fire profile exceeds source argument frame");
            else {
                for (size_t i = 0; i < gear->definition->fire_argument_count; ++i) words[i+1] = gear->definition->fire_arguments[i];
                okay = q3gear_call(gear, gear->definition->callbacks.fire, words,
                    gear->definition->fire_argument_count + 1, &result, error);
            }
        }
    }
    if (okay) okay = q3gear_publish(gear, &gear->tethers[actor.slot], error) &&
        q3gear_hook(gear, actor, &hook, &client, error);
    if (okay && !previous && hook) okay = q3gear_sound(gear, actor, gear->definition->presentation.fire_sound, false, error);
    return q3gear_leave(gear, okay, error);
}

bool application_q3_gear_release(application_q3_gear *gear, qa_actor_id actor, bool force, qa_error *error)
{
    if (!enter(gear, error)) return false;
    uint32_t previous = 0, hook = 0, client;
    bool tracked = actor.slot < gear->capacity && qa_actor_id_equal(gear->tethers[actor.slot].owner, actor) && gear->tethers[actor.slot].tracked;
    bool okay = !tracked || (q3gear_hook(gear, actor, &previous, &client, error) && q3gear_release(gear, actor, force, error) &&
        q3gear_hook(gear, actor, &hook, &client, error));
    if (okay && previous && !hook) okay = q3gear_sound(gear, actor, gear->definition->presentation.release_sound, false, error);
    return q3gear_leave(gear, okay, error);
}

bool application_q3_gear_pull(application_q3_gear *gear, qa_actor_id actor, qa_vec3 forward,
    qa_vec3 *velocity, bool *apply, qa_error *error)
{
    if (!velocity || !apply) return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear pull outputs are absent");
    if (!isfinite(forward.x) || !isfinite(forward.y) || !isfinite(forward.z))
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear pull direction is not finite");
    if (!enter(gear, error)) return false;
    bool okay = q3gear_sync(gear, error), applied = false; qa_vec3 pulled = {0};
    if (okay && actor.slot < gear->capacity && qa_actor_id_equal(gear->tethers[actor.slot].owner, actor) && gear->tethers[actor.slot].pulling) {
        uint32_t hook, client; int32_t result;
        okay = q3gear_hook(gear, actor, &hook, &client, error);
        if (okay && hook) {
            gear->forward = forward; gear->pull_client = client; gear->pull_pending = true;
            okay = q3gear_call(gear, gear->definition->callbacks.pull, NULL, 0, &result, error);
            gear->pull_pending = false; gear->pull_client = 0;
            if (okay) okay = q3gear_publish(gear, &gear->tethers[actor.slot], error) && q3gear_vector(gear, client+32, &pulled, error);
            applied = okay;
        }
    }
    okay = q3gear_leave(gear, okay, error);
    if (okay) { *apply = applied; if (applied) *velocity = pulled; } return okay;
}

bool application_q3_gear_actor_releasing(application_q3_gear *gear, qa_actor_id actor, qa_error *error)
{
    if (!enter(gear, error)) return false;
    bool okay = q3gear_forget(gear, actor, error); return q3gear_leave(gear, okay, error);
}

bool application_q3_gear_actor_released(application_q3_gear *gear, qa_actor_record released, qa_error *error)
{
    if (!gear || !gear->host || qa_actors_get(qa_session_actors(gear->options.host.session), released.id))
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear release notification needs an invalidated full actor");
    if (!qa_q3_host_actor_released(gear->host, released, error)) return false;
    for (uint32_t i = 0; i < gear->capacity; ++i) {
        if (qa_actor_id_equal(gear->bindings[i].actor, released.id)) gear->bindings[i].retired = true;
        if (qa_actor_id_equal(gear->tethers[i].actor, released.id)) {
            gear->tethers[i].actor = (qa_actor_id){0}; gear->tethers[i].orphaned = true;
        }
    }
    return gear->busy || qa_qvm_active(gear->vm) || gear->restoring ? true : q3gear_flush_releases(gear, error);
}

bool application_q3_gear_begin_frame(application_q3_gear *gear, int32_t time, int32_t frame, qa_error *error)
{
    if (!enter(gear, error)) return false;
    int32_t old_time, old_frame, result;
    bool okay = q3gear_word(gear, gear->definition->globals.time, &old_time, error) &&
        q3gear_word(gear, gear->definition->globals.frame, &old_frame, error);
    if (okay && (time < old_time || frame < old_frame || time < 0 || frame < 0))
        okay = q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear frame moved backwards");
    if (okay) {
        gear->milliseconds = time; gear->frame = frame;
        okay = q3gear_store(gear, gear->definition->globals.time, time, error) &&
            q3gear_store(gear, gear->definition->globals.frame, frame, error) && q3gear_sync(gear, error);
    }
    for (uint32_t i = 0; i < gear->capacity && okay; ++i) {
        q3gear_tether tether = gear->tethers[i];
        if (!tether.tracked) continue;
        uint32_t hook, client; int32_t health;
        okay = q3gear_hook(gear, tether.owner, &hook, &client, error) &&
            q3gear_word(gear, gear->bindings[i].pointer + gear->definition->fields.health, &health, error);
        if (!okay) break;
        if (health <= 0) { okay = q3gear_release(gear, tether.owner, true, error); continue; }
        if (hook) {
            qa_q3_entity source; int32_t words[] = {(int32_t)hook};
            okay = qa_qvm_read_entity(gear->vm, (int32_t)hook, true, &source, error);
            if (okay && source.eType == 3) okay = q3gear_call(gear, gear->definition->callbacks.missile, words, 1, &result, error);
            else if (okay) {
                if (gear->definition->callbacks.follow) okay = q3gear_call(gear, gear->definition->callbacks.follow, words, 1, &result, error);
                if (okay) okay = q3gear_hook(gear, tether.owner, &hook, &client, error);
                if (okay && hook) { words[0] = (int32_t)hook; okay = q3gear_call(gear, gear->definition->callbacks.think, words, 1, &result, error); }
            }
        }
        if (okay) okay = q3gear_publish(gear, &gear->tethers[i], error);
    }
    qa_q3_host_game_data layout;
    if (okay) okay = q3gear_layout(gear, &layout, error);
    for (uint32_t slot = 64; okay && slot < layout.entity_count; ++slot) {
        uint32_t pointer = (uint32_t)layout.entities_address + slot*layout.entity_stride;
        int32_t live, expire, event;
        okay = q3gear_word(gear, pointer+gear->definition->fields.inuse, &live, error) &&
            q3gear_word(gear, pointer+gear->definition->fields.free_after_event, &expire, error) &&
            q3gear_word(gear, pointer+gear->definition->fields.event_time, &event, error);
        if (okay && live && expire && (int64_t)time - event > gear->definition->event_lifetime_ms) {
            int32_t words[] = {(int32_t)pointer}; okay = q3gear_call(gear, gear->definition->callbacks.free, words, 1, &result, error);
        }
    }
    for (uint32_t i = 0; i < gear->capacity && okay; ++i) {
        q3gear_tether tether = gear->tethers[i];
        if (!tether.pulling) continue;
        uint32_t hook, client; qa_vec3 point; qa_body_state body;
        okay = q3gear_hook(gear, tether.owner, &hook, &client, error) && q3gear_vector(gear, client+92, &point, error) &&
            qa_world_body_read(gear->options.host.world, tether.owner, &body, error);
        if (okay) {
            double x = point.x-body.origin.x, y = point.y-body.origin.y, z = point.z-body.origin.z-26;
            okay = q3gear_sound(gear, tether.owner, sqrt(x*x+y*y+z*z) <= 64 ?
                gear->definition->presentation.hang_sound : gear->definition->presentation.pull_sound, true, error);
        }
    }
    return q3gear_leave(gear, okay, error);
}

bool application_q3_gear_read(application_q3_gear *gear, qa_actor_id actor,
    application_q3_gear_view *out, qa_error *error)
{
    if (!out) return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear view output is absent");
    if (!enter(gear, error)) return false;
    for (uint32_t i = 0; i < gear->capacity; ++i)
        if (gear->bindings[i].retired || gear->tethers[i].orphaned) {
            gear->busy = false;
            return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear view has unfinished source retirement");
        }
    if (!qa_actors_get(qa_session_actors(gear->options.host.session), actor)) {
        gear->busy = false; return q3gear_fail(error, QA_ERROR_NOT_FOUND, "Separate QVM gear view actor has retired");
    }
    uint32_t hook, client, slot; int32_t flags, target = 0, mover = 0;
    application_q3_gear_view view = {.actor = actor, .definition = gear->definition, .time_ms = gear->milliseconds};
    bool okay = q3gear_hook(gear, actor, &hook, &client, error) &&
        q3gear_slot(gear, gear->bindings[actor.slot].pointer, &slot, error) &&
        qa_q3_host_source_player(gear->host, slot, &view.player, error) && q3gear_word(gear, client+12, &flags, error);
    if (okay && hook) {
        okay = q3gear_word(gear, hook+gear->definition->fields.target, &target, error) &&
            (gear->definition->fields.mover == UINT32_MAX || q3gear_word(gear, hook+gear->definition->fields.mover, &mover, error));
        view.target = q3gear_actor(gear, target); view.mover = q3gear_actor(gear, mover);
        view.tether = gear->tethers[actor.slot].actor; view.pulling = ((uint32_t)flags & gear->definition->pulling_flag) != 0;
    }
    gear->busy = false; if (okay) *out = view; return okay;
}

qa_console *application_q3_gear_console(application_q3_gear *gear, qa_cvars **cvars)
{
    if (!gear) return NULL;
    if (cvars) *cvars = gear->cvars;
    return gear->console;
}

bool application_q3_gear_destroy(application_q3_gear *gear, qa_error *error)
{
    if (!gear) return true;
    if (!application_q3_gear_idle(gear))
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear destruction has active source/world users");
    if (gear->vm && !gear->restoring && gear->initialized) {
        gear->busy = true; bool okay = true;
        for (uint32_t i = 0; i < gear->capacity && okay; ++i)
            if (gear->tethers[i].tracked) okay = q3gear_release(gear, gear->tethers[i].owner, true, error);
        for (uint32_t i = 0; i < gear->capacity && okay; ++i)
            if (gear->bindings[i].actor.registry) okay = q3gear_forget(gear, gear->bindings[i].actor, error);
        gear->busy = false; if (!okay) return false;
        gear->initialized = false;
    } else if (gear->restoring) {
        for (uint32_t i = 0; i < gear->capacity; ++i) {
            qa_actor_id actor = gear->tethers[i].actor;
            if (!actor.registry) continue;
            const qa_actor_record *record = qa_actors_get(qa_session_actors(gear->options.host.session), actor);
            if (record) {
                qa_actor_record released = *record;
                if (!qa_session_release(gear->options.host.session, actor, error) ||
                    !qa_q3_host_actor_released(gear->host, released, error)) return false;
            }
            gear->tethers[i] = (q3gear_tether){0};
        }
    }
    if (!qa_q3_host_destroy_ready(gear->host) || (gear->vm && !qa_qvm_can_destroy(gear->vm)))
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear destruction retains physical source actors");
    if (!qa_q3_host_close_map(gear->host, error) || (gear->vm && !qa_qvm_destroy(gear->vm, error))) return false;
    gear->vm = NULL; qa_q3_host_qvm_consumed(gear->host);
    if (!qa_q3_host_destroy(gear->host, error)) return false;
    if (!qa_console_unbind_source(gear->console, qa_cvars_view_identity(gear->cvars), error)) return false;
    qa_cvars_detach_callbacks(gear->cvars); qa_cvars_destroy(gear->cvars);
    for (size_t i = 0; i < 1024; ++i) free(gear->configstrings[i]);
    for (size_t i = 0; i < 64; ++i) free(gear->userinfo[i]);
    free(gear->bindings); free(gear->tethers); free(gear->path); qa_buffer_free(&gear->entities);
    qa_qvm_image_release(gear->image); free(gear); return true;
}
