#include "weapon_trajectories.h"
#include "internal.h"
#include "source_trajectory.h"
#include "guest_qc_internal.h"
#include "qa/application_players.h"
#include "qa/pool.h"
#include "qa/source_save.h"

#include <stdlib.h>
#include <string.h>

typedef struct trajectory_source {
    struct trajectory_source *next;
    qa_launch_instance_lease *descriptor;
    const qa_catalog_weapon_behavior *behavior;
    application_trajectory_source runtime;
    qa_string_id instance, name;
    uint64_t identity;
    size_t bindings, flights;
} trajectory_source;

typedef struct trajectory_binding {
    qa_launch_scope scope;
    qa_item_id weapon;
    qa_builtin_projectile_role role;
    trajectory_source *source;
} trajectory_binding;

typedef struct trajectory_flight {
    application_trajectory_attachment attachment;
    trajectory_source *source;
    bool active;
} trajectory_flight;

struct application_trajectories {
    trajectory_source *sources;
    trajectory_binding *bindings;
    size_t binding_count;
    qa_actor_index actors;
    qa_arena storage;
    qa_pool flights;
};

static bool create(qa_application *app, qa_error *error)
{
    if (app->trajectories) return true;
    struct application_trajectories *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Allocating projectile behavior owner");
    const qa_actor_registry *actors = qa_session_actors(app->session);
    if (!qa_actor_index_create(&owner->actors, actors, NULL, error) ||
        !qa_pool_reserve_pages(&owner->flights, &owner->storage, qa_actors_capacity(actors),
            sizeof(trajectory_flight), _Alignof(trajectory_flight), error)) {
        qa_actor_index_destroy(&owner->actors);
        qa_arena_destroy(&owner->storage);
        free(owner);
        return false;
    }
    memset(owner->flights.values, 0, owner->flights.capacity * owner->flights.stride);
    app->trajectories = owner;
    return true;
}

static bool source_destroy(trajectory_source *source, qa_error *error)
{
    if (source->runtime.destroy && !source->runtime.destroy(source->runtime.context, error)) return false;
    qa_launch_instance_lease_release(source->descriptor);
    free(source);
    return true;
}

static trajectory_source *source_find(struct application_trajectories *owner,
    qa_string_id instance, qa_string_id name, uint64_t identity)
{
    for (trajectory_source *source = owner->sources; source; source = source->next)
        if (source->instance == instance && source->name == name &&
            (!identity || source->identity == identity)) return source;
    return NULL;
}

static trajectory_source *source_prepare(qa_application *app, const qa_launch_instance *instance,
    const qa_catalog_weapon_behavior *behavior, qa_error *error)
{
    qa_strings *strings = qa_session_strings(app->session);
    qa_string_id instance_name, behavior_name;
    if (!qa_strings_intern_cstr(strings, instance->selection.instance, &instance_name, error) ||
        !qa_strings_intern_cstr(strings, behavior->id, &behavior_name, error)) return NULL;
    trajectory_source *existing = source_find(app->trajectories, instance_name, behavior_name, instance->identity);
    if (existing && existing->behavior == behavior) return existing;
    trajectory_source *source = calloc(1, sizeof(*source));
    if (!source) { application_fail(error, QA_ERROR_MEMORY, "Allocating projectile donor state"); return NULL; }
    bool okay = qa_launch_instance_retain_metadata(instance, &source->descriptor, error);
    const qa_launch_instance *descriptor = qa_launch_instance_lease_view(source->descriptor);
    application_provider *provider = instance->state;
    if (okay && provider->kind == APPLICATION_PROVIDER_QC)
        okay = application_trajectory_qc_create(app, descriptor, provider->state.qc.program,
            behavior, &source->runtime, error);
    else if (okay)
        okay = application_fail(error, QA_ERROR_UNSUPPORTED,
            "Selected projectile donor requires a native or QVM trajectory adapter");
    if (!okay) {
        (void)source_destroy(source, NULL);
        return NULL;
    }
    source->instance = instance_name;
    source->name = behavior_name;
    source->identity = instance->identity;
    source->behavior = behavior;
    source->next = app->trajectories->sources;
    app->trajectories->sources = source;
    return source;
}

static bool unused_sources(struct application_trajectories *owner, qa_error *error)
{
    trajectory_source **next = &owner->sources;
    while (*next) {
        trajectory_source *source = *next;
        if (source->bindings || source->flights) { next = &source->next; continue; }
        trajectory_source *following = source->next;
        if (!source_destroy(source, error)) return false;
        *next = following;
    }
    return true;
}

bool application_trajectories_configure(qa_application *app, const qa_launch_snapshot *snapshot,
    qa_error *error)
{
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    for (size_t i = 0; i < qa_launch_snapshot_instance_count(snapshot); ++i) {
        const qa_launch_instance *instance = qa_launch_snapshot_instance(snapshot, i);
        application_provider *provider = instance->state;
        if (provider && provider->kind == APPLICATION_PROVIDER_QC && provider->state.qc.engine)
            provider->state.qc.engine->trajectory_only = instance->roles == QA_ROLE_BIT(QA_ROLE_TRAJECTORY);
    }
    size_t count = 0;
    for (size_t i = 0; choices && i < choices->behavior_count; ++i) count += choices->behaviors[i].enabled;
    if (!count && !app->trajectories) return true;
    if (!create(app, error)) return false;
    trajectory_binding *bindings = count ? calloc(count, sizeof(*bindings)) : NULL;
    if (count && !bindings) return application_fail(error, QA_ERROR_MEMORY, "Preparing projectile selections");
    size_t used = 0;
    for (size_t i = 0; choices && i < choices->behavior_count; ++i) {
        const qa_launch_weapon_behavior *selected = choices->behaviors + i;
        if (!selected->enabled) continue;
        const qa_launch_instance *instance = qa_launch_snapshot_find(snapshot, selected->instance);
        const qa_catalog_weapon_behavior *behavior = NULL;
        for (size_t j = 0; instance && j < instance->behavior_count; ++j)
            if (!strcmp(instance->behaviors[j]->id, selected->behavior)) { behavior = instance->behaviors[j]; break; }
        if (!behavior) {
            free(bindings);
            return application_fail(error, QA_ERROR_NOT_FOUND, "Selected projectile behavior is not admitted");
        }
        trajectory_binding *binding = bindings + used++;
        binding->source = source_prepare(app, instance, behavior, error);
        binding->scope = selected->scope;
        binding->role = selected->role;
        if (!binding->source || (selected->weapon && *selected->weapon &&
            !qa_strings_intern_cstr(qa_session_strings(app->session), selected->weapon, &binding->weapon, error))) {
            free(bindings);
            return false;
        }
    }
    struct application_trajectories *owner = app->trajectories;
    for (trajectory_source *source = owner->sources; source; source = source->next) source->bindings = 0;
    for (size_t i = 0; i < used; ++i) ++bindings[i].source->bindings;
    free(owner->bindings);
    owner->bindings = bindings;
    owner->binding_count = used;
    return unused_sources(owner, error);
}

static trajectory_binding *selection(qa_application *app, const qa_builtin_weapon_launch *launch)
{
    struct application_trajectories *owner = app->trajectories;
    if (!owner) return NULL;
    uint32_t seat = 0;
    bool player = qa_application_player_seat(app, launch->shooter, &seat);
    unsigned best = 0;
    trajectory_binding *selected = NULL;
    for (size_t i = 0; i < owner->binding_count; ++i) {
        trajectory_binding *binding = owner->bindings + i;
        if (binding->role != launch->role || (binding->weapon && binding->weapon != launch->weapon)) continue;
        unsigned rank = 0;
        switch (binding->scope.kind) {
        case QA_SCOPE_WORLD: rank = 1; break;
        case QA_SCOPE_DEFAULT_PLAYER: rank = player ? 2 : 0; break;
        case QA_SCOPE_SEAT: rank = player && binding->scope.seat == seat ? 3 : 0; break;
        case QA_SCOPE_ACTOR: rank = qa_actor_id_equal(binding->scope.actor, launch->shooter) ? 4 : 0; break;
        }
        if (!rank) continue;
        rank = rank * 2 + (binding->weapon != 0);
        if (rank > best) { best = rank; selected = binding; }
    }
    return selected;
}

static trajectory_flight *flight_find(qa_application *app, qa_actor_id actor, size_t *slot)
{
    struct application_trajectories *owner = app->trajectories;
    size_t index;
    if (!owner || !qa_actor_index_find(&owner->actors, actor, &index)) return NULL;
    if (slot) *slot = index;
    return qa_pool_at(&owner->flights, index);
}

bool application_trajectory_launch(void *context, const qa_builtin_weapon_launch *launch,
    qa_builtin_trajectory_update *update, bool *changed, qa_error *error)
{
    qa_application *app = context;
    *changed = false;
    trajectory_binding *binding = selection(app, launch);
    if (!binding) return true;
    struct application_trajectories *owner = app->trajectories;
    size_t slot;
    trajectory_flight *flight = qa_pool_take(&owner->flights, &slot);
    if (!flight) return application_fail(error, QA_ERROR_CAPACITY, "Projectile behavior capacity is exhausted");
    *flight = (trajectory_flight){.attachment.projectile = launch->projectile, .source = binding->source};
    bool okay = flight->source->runtime.launch(flight->source->runtime.context, launch,
        &flight->attachment, update, changed, error);
    if (!okay || !*changed) {
        qa_error released = {0};
        bool closed = flight->source->runtime.release(flight->source->runtime.context, &flight->attachment, &released);
        *flight = (trajectory_flight){0};
        qa_pool_release(&owner->flights, slot);
        if (okay && !closed && error) *error = released;
        return okay && closed;
    }
    flight->active = true;
    ++flight->source->flights;
    qa_actor_index_put(&owner->actors, launch->projectile, slot);
    return true;
}

bool application_trajectory_controls(void *context, qa_actor_id actor)
{
    return flight_find(context, actor, NULL) != NULL;
}

bool application_trajectory_step(void *context, qa_actor_id actor, const qa_body_state *body,
    uint64_t time, qa_builtin_trajectory_update *update, bool *changed, qa_error *error)
{
    trajectory_flight *flight = flight_find(context, actor, NULL);
    *changed = false;
    return !flight || flight->source->runtime.step(flight->source->runtime.context,
        &flight->attachment, body, time, update, changed, error);
}

bool application_trajectories_actor_released(qa_application *app, qa_actor_record actor, qa_error *error)
{
    struct application_trajectories *owner = app->trajectories;
    if (!owner) return true;
    size_t slot;
    trajectory_flight *flight = flight_find(app, actor.id, &slot);
    if (flight) {
        if (!flight->source->runtime.release(flight->source->runtime.context, &flight->attachment, error)) return false;
        --flight->source->flights;
        *flight = (trajectory_flight){0};
        qa_actor_index_remove(&owner->actors, actor.id);
        qa_pool_release(&owner->flights, slot);
    }
    for (trajectory_source *source = owner->sources; source; source = source->next)
        if (source->runtime.actor_released &&
            !source->runtime.actor_released(source->runtime.context, actor, error)) return false;
    return true;
}

bool application_trajectories_destroy(qa_application *app, qa_error *error)
{
    struct application_trajectories *owner = app->trajectories;
    if (owner) {
        while (owner->sources) {
            trajectory_source *source = owner->sources;
            trajectory_source *next = source->next;
            if (!source_destroy(source, error)) return false;
            owner->sources = next;
        }
        qa_actor_index_destroy(&owner->actors);
        qa_arena_destroy(&owner->storage);
        free(owner->bindings);
        free(owner);
        app->trajectories = NULL;
    }
    qa_buffer_free(&app->trajectories_saved);
    return true;
}

static bool blob(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t size = bytes->size;
    if (!qa_source_save_count(io, &size, SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) return qa_source_save_span(io, size, bytes);
    return qa_source_save_bytes(io, (void *)bytes->data, size);
}

bool application_trajectories_capture(qa_application *app, qa_buffer *out, qa_error *error)
{
    struct application_trajectories *owner = app->trajectories;
    *out = (qa_buffer){0};
    if (!owner || !owner->sources) return true;
    size_t count = 0;
    for (trajectory_source *source = owner->sources; source; source = source->next)
        count += source->bindings || source->flights;
    if (!count) return true;
    qa_source_save_io io = {0};
    if (!qa_source_save_writer(&io, app->session, error)) return false;
    bool okay = qa_source_save_bytes(&io, "QATR", 4) && qa_source_save_count(&io, &count, UINT32_MAX);
    for (trajectory_source *source = owner->sources; okay && source; source = source->next) {
        if (!source->bindings && !source->flights) continue;
        qa_buffer state = {0};
        qa_bytes bytes = {0};
        okay = source->runtime.capture(source->runtime.context, &state, error);
        bytes = (qa_bytes){state.data, state.size};
        if (okay) okay = qa_source_save_string(&io, &source->instance) &&
            qa_source_save_string(&io, &source->name) && blob(&io, &bytes) &&
            qa_source_save_count(&io, &source->flights, owner->flights.capacity);
        for (size_t i = 0; okay && i < owner->flights.capacity; ++i) {
            trajectory_flight *flight = qa_pool_at(&owner->flights, i);
            if (!flight->active || flight->source != source) continue;
            application_trajectory_attachment saved = flight->attachment;
            okay = qa_source_save_actor(&io, &saved.projectile) &&
                qa_source_save_u64(&io, &saved.handle) && qa_source_save_bool(&io, &saved.retired);
        }
        qa_buffer_free(&state);
    }
    if (okay) okay = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return okay;
}

bool application_trajectories_restore_saved(qa_application *app, qa_bytes bytes, qa_error *error)
{
    qa_buffer pending = {0};
    if (bytes.size) {
        if (bytes.size < 4 || memcmp(bytes.data, "QATR", 4))
            return application_fail(error, QA_ERROR_FORMAT, "Unknown application checkpoint tail");
        pending.data = malloc(bytes.size);
        if (!pending.data) return application_fail(error, QA_ERROR_MEMORY, "Retaining projectile checkpoint");
        memcpy(pending.data, bytes.data, bytes.size);
        pending.size = bytes.size;
    }
    qa_buffer_free(&app->trajectories_saved);
    app->trajectories_saved = pending;
    return true;
}

bool application_trajectories_restore_finish(qa_application *app, qa_error *error)
{
    qa_bytes saved = {app->trajectories_saved.data, app->trajectories_saved.size};
    if (!saved.size) return true;
    if (!create(app, error)) return false;
    struct application_trajectories *owner = app->trajectories;
    qa_source_save_io io = {0};
    size_t count = 0;
    uint8_t tag[4];
    bool okay = qa_source_save_reader(&io, app->session, saved, error) &&
        qa_source_save_bytes(&io, tag, sizeof(tag)) && !memcmp(tag, "QATR", 4) &&
        qa_source_save_count(&io, &count, saved.size);
    for (size_t i = 0; okay && i < count; ++i) {
        qa_string_id instance = 0, name = 0;
        qa_bytes state = {0}; size_t flights = 0;
        okay = qa_source_save_string(&io, &instance) && qa_source_save_string(&io, &name) &&
            blob(&io, &state) && qa_source_save_count(&io, &flights, owner->flights.capacity);
        trajectory_source *source = okay ? source_find(owner, instance, name, 0) : NULL;
        if (okay && !source) okay = application_fail(error, QA_ERROR_NOT_FOUND,
            "Saved projectile donor is not selected in the restored session");
        if (okay) okay = source->runtime.restore(source->runtime.context, state, error);
        for (size_t j = 0; okay && j < flights; ++j) {
            application_trajectory_attachment attachment = {0};
            okay = qa_source_save_actor(&io, &attachment.projectile) &&
                qa_source_save_u64(&io, &attachment.handle) && qa_source_save_bool(&io, &attachment.retired);
            if (!okay) break;
            if (!qa_actors_get(qa_session_actors(app->session), attachment.projectile) ||
                flight_find(app, attachment.projectile, NULL)) {
                okay = application_fail(error, QA_ERROR_FORMAT, "Saved projectile attachment has no unique live actor");
                break;
            }
            size_t slot;
            trajectory_flight *flight = qa_pool_take(&owner->flights, &slot);
            if (!flight) { okay = application_fail(error, QA_ERROR_CAPACITY, "Saved projectile attachments exceed actor capacity"); break; }
            *flight = (trajectory_flight){.attachment = attachment, .source = source, .active = true};
            ++source->flights;
            qa_actor_index_put(&owner->actors, attachment.projectile, slot);
        }
    }
    if (okay) okay = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (okay) qa_buffer_free(&app->trajectories_saved);
    return okay;
}
