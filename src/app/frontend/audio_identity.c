#include "internal.h"
#include "round.h"
#include "native_q3_client_internal.h"
#include "qa/ruleset.h"
#include "qa/application_equipment_events.h"
#include "qa/application_native_q2_delivery.h"
#include "remote_unified_private.h"
#include "remote_unified_presentation.h"
#include "../../network/unified/frame_internal.h"
static bool marks_reserve(qa_frontend *frontend, qa_error *error)
{
    size_t capacity = frontend->audio_ids.capacity;
    if (capacity <= frontend->audio_marks.count) return true;
    uint32_t *marks = qa_arena_alloc(&frontend->audio_identity_storage,
        capacity * sizeof(*marks), _Alignof(uint32_t), error);
    if (!marks) return false;
    qa_stamp_set_init(&frontend->audio_marks, marks, capacity);
    return true;
}
static frontend_audio_registry *registry_find(const qa_frontend *frontend, uint64_t registry)
{
    qa_id_index_cursor cursor = qa_id_index_begin(&frontend->audio_registry_index, QA_STRING_NONE, registry);
    size_t slot;
    return qa_id_index_next(&frontend->audio_registry_index, &cursor, &slot) ?
        qa_pool_at(&frontend->audio_registry_records, slot) : NULL;
}
bool frontend_audio_domain_prepare(qa_frontend *frontend, frontend_audio_domain *domain,
    const qa_actor_registry *actors, qa_error *error)
{
    uint64_t registry = qa_actors_identity(actors);
    if (domain->registry && domain->registry->index.registry == registry) return true;
    frontend_audio_domain_close(frontend, domain);
    size_t capacity = qa_actors_capacity(actors);
    for (const frontend_remote_unified *replica = frontend->remote_unified;
         replica; replica = replica->next)
        if (replica->actors == actors && replica->identity_records.capacity > capacity)
            capacity = replica->identity_records.capacity;
    if (!frontend->audio_ids.stride) {
        if (!qa_pool_prepare(&frontend->audio_ids, &frontend->audio_identity_storage,
                capacity, sizeof(frontend_audio_identity), _Alignof(frontend_audio_identity), error)) return false;
    } else {
        size_t available = frontend->audio_ids.capacity - frontend->audio_ids.active;
        if (capacity > available && !qa_pool_extend(&frontend->audio_ids,
                &frontend->audio_identity_storage, capacity - available, error)) return false;
    }
    if (!marks_reserve(frontend, error) ||
        !qa_id_index_reserve(&frontend->audio_reverse, capacity, error)) return false;
    frontend_audio_registry *binding = registry_find(frontend, registry);
    if (!binding) {
        if (!frontend->audio_registry_records.stride) {
            if (!qa_pool_prepare(&frontend->audio_registry_records, &frontend->audio_identity_storage,
                    1, sizeof(*binding), _Alignof(frontend_audio_registry), error)) return false;
        } else if (frontend->audio_registry_records.active == frontend->audio_registry_records.capacity &&
            !qa_pool_extend(&frontend->audio_registry_records, &frontend->audio_identity_storage,
                frontend->audio_registry_records.capacity, error)) return false;
        if (!qa_id_index_reserve(&frontend->audio_registry_index, 1, error)) return false;
        qa_actor_index prepared = {0};
        if (!qa_actor_index_create(&prepared, actors, NULL, error)) return false;
        if (!qa_id_index_reserve(&prepared.retained, capacity, error)) {
            qa_actor_index_destroy(&prepared); return false;
        }
        size_t slot;
        binding = qa_pool_take(&frontend->audio_registry_records, &slot);
        *binding = (frontend_audio_registry){.actors = actors, .index = prepared,
            .slot = slot, .next = frontend->audio_registries};
        frontend->audio_registries = binding;
        qa_id_index_insert(&frontend->audio_registry_index, QA_STRING_NONE, registry, slot);
    } else if (!qa_id_index_reserve(&binding->index.retained, capacity, error)) return false;
    binding->actors = actors;
    *domain = (frontend_audio_domain){.registry = binding, .next = frontend->audio_domains};
    frontend->audio_domains = domain;
    for (frontend_audio_identity *row = frontend->audio_first; row; row = row->next) {
        if (row->registry != binding) continue;
        row->retired = qa_actors_get(actors, row->actor) == NULL;
        if (!row->retired) qa_actor_index_put(&binding->index, row->actor, row->slot);
    }
    return true;
}
void frontend_audio_domain_close(qa_frontend *frontend, frontend_audio_domain *domain)
{
    frontend_audio_registry *binding = domain->registry;
    frontend_audio_domain **linked = &frontend->audio_domains;
    while (*linked && *linked != domain) linked = &(*linked)->next;
    if (*linked) *linked = domain->next;
    *domain = (frontend_audio_domain){0};
    if (!binding) return;
    for (const frontend_audio_domain *other = frontend->audio_domains; other; other = other->next)
        if (other->registry == binding) return;
    binding->actors = NULL;
    for (frontend_audio_identity *row = frontend->audio_first; row; row = row->next)
        if (row->registry == binding) {
            row->retired = true;
            qa_actor_index_remove(&binding->index, row->actor);
        }
}
bool frontend_audio_identities_prepare(qa_frontend *frontend, qa_error *error)
{
    return frontend_audio_domain_prepare(frontend, &frontend->audio_local,
        qa_session_actors(qa_application_session(frontend->application)), error);
}
void frontend_audio_identities_destroy(qa_frontend *frontend)
{
    while (frontend->audio_domains)
        frontend_audio_domain_close(frontend, frontend->audio_domains);
    for (frontend_audio_registry *row = frontend->audio_registries; row; row = row->next)
        qa_actor_index_destroy(&row->index);
    qa_id_index_destroy(&frontend->audio_registry_index);
    qa_id_index_destroy(&frontend->audio_reverse);
    qa_arena_destroy(&frontend->audio_identity_storage);
    frontend->audio_ids = frontend->audio_registry_records = (qa_pool){0};
    frontend->audio_marks = (qa_stamp_set){0};
    frontend->audio_first = frontend->audio_last = NULL;
    frontend->audio_registries = NULL;
}
static bool retire_dead_aliases(qa_frontend *frontend)
{
    bool retired = false;
    for (frontend_audio_identity *row = frontend->audio_first; row; row = row->next) {
        if (!row->retired && (!row->registry->actors ||
            !qa_actors_get(row->registry->actors, row->actor))) {
            row->retired = true;
            qa_actor_index_remove(&row->registry->index, row->actor);
        }
        retired |= row->retired;
    }
    return retired;
}
void frontend_audio_retire_dead_aliases(qa_frontend *frontend)
{
    (void)retire_dead_aliases(frontend);
}
void frontend_audio_retire_round_aliases(qa_frontend *frontend)
{
    /* Retained event leases and Source histories can outlive the mixer reset. */
    frontend_audio_retire_dead_aliases(frontend);
}
uint64_t frontend_audio_bind(qa_frontend *frontend, frontend_audio_domain *domain,
    qa_actor_id actor, bool retained, qa_error *error)
{
    frontend_audio_registry *binding = domain->registry;
    if (!actor.registry || !binding || actor.registry != binding->index.registry) return QA_AUDIO_NO_ACTOR;
    size_t slot;
    if (qa_actor_index_find(&binding->index, actor, &slot) ||
        qa_actor_index_find_retained(&binding->index, actor, &slot))
        return ((const frontend_audio_identity *)qa_pool_at(&frontend->audio_ids, slot))->id;
    bool present = qa_actors_get(binding->actors, actor) != NULL;
    if (!present && !retained) return QA_AUDIO_NO_ACTOR;
    if (frontend->next_audio_id >= UINT64_MAX - 1) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "audio actor identity exhausted"); return QA_AUDIO_NO_ACTOR;
    }
    if (frontend->audio_ids.active == frontend->audio_ids.capacity &&
        !qa_pool_extend(&frontend->audio_ids, &frontend->audio_identity_storage,
            frontend->audio_ids.capacity, error)) return QA_AUDIO_NO_ACTOR;
    if (!marks_reserve(frontend, error) || !qa_id_index_reserve(&frontend->audio_reverse, 1, error) ||
        !qa_id_index_reserve(&binding->index.retained, 1, error)) return QA_AUDIO_NO_ACTOR;
    frontend_audio_identity *row = qa_pool_take(&frontend->audio_ids, &slot);
    if (!row) {
        frontend_fail(error, QA_ERROR_MEMORY, "allocating audio actor identities"); return QA_AUDIO_NO_ACTOR;
    }
    if (!qa_actor_index_remember(&binding->index, actor, slot, error)) {
        qa_pool_release(&frontend->audio_ids, slot); return QA_AUDIO_NO_ACTOR;
    }
    uint64_t id = ++frontend->next_audio_id;
    *row = (frontend_audio_identity){.actor = actor, .id = id, .slot = slot,
        .registry = binding, .retired = !present};
    qa_id_index_insert(&frontend->audio_reverse, QA_STRING_NONE, id, slot);
    if (present) qa_actor_index_put(&binding->index, actor, slot);
    if (frontend->audio_last) frontend->audio_last->next = row;
    else frontend->audio_first = row;
    frontend->audio_last = row;
    return id;
}
static void mark_actor(void *opaque, qa_actor_id actor)
{
    qa_frontend *frontend = opaque;
    frontend_audio_registry *registry = registry_find(frontend, actor.registry);
    size_t slot;
    if (registry && qa_actor_index_find_retained(&registry->index, actor, &slot))
        (void)qa_stamp_set_mark(&frontend->audio_marks, slot);
}
static bool bus_slot(const qa_frontend *frontend, uint64_t bus, size_t *slot)
{
    qa_id_index_cursor cursor = qa_id_index_begin(&frontend->audio_reverse, QA_STRING_NONE, bus);
    return qa_id_index_next(&frontend->audio_reverse, &cursor, slot);
}
static void mark_bus(void *opaque, uint64_t bus)
{
    qa_frontend *frontend = opaque;
    size_t slot;
    if (bus_slot(frontend, bus, &slot)) (void)qa_stamp_set_mark(&frontend->audio_marks, slot);
}
static void audience_actors(qa_frontend *frontend, const qa_application_q2_audience *audience)
{
    if (audience)
        for (size_t i = 0; i < audience->count; ++i)
            mark_actor(frontend, audience->recipients[i].actor);
}
static void record_actors(void *opaque, const qa_application_event_view *view)
{
    qa_frontend *frontend = opaque;
    mark_actor(frontend, view->presentation_recipient);
    mark_actor(frontend, view->simulation_recipient);
    mark_actor(frontend, view->sound_recipient);
    qa_unified_record_actors_visit(&qa_unified_presentation_payload_layout, view->presentation,
        mark_actor, frontend);
    qa_unified_record_actors_visit(&qa_unified_simulation_payload_layout, view->simulation,
        mark_actor, frontend);
    if (view->protocol) {
        mark_actor(frontend, view->protocol->recipient);
        for (size_t i = 0; i < view->protocol->reference_count; ++i)
            mark_actor(frontend, view->protocol->references[i].actor);
    }
    if (view->equipment) mark_actor(frontend, view->equipment->recipient);
    audience_actors(frontend, view->q2_audience);
    if (view->q2_delivery) audience_actors(frontend, &view->q2_delivery->audience);
    switch (view->kind) {
    case QA_APPLICATION_EVENT_AUDIO:
        mark_bus(frontend, view->value.audio->sound.actor);
        if (view->value.audio->sound.origin_kind == QA_AUDIO_ACTOR)
            mark_bus(frontend, view->value.audio->sound.origin_actor);
        break;
    case QA_APPLICATION_EVENT_BUILTIN: case QA_APPLICATION_EVENT_CLIENT_SOUND:
        mark_actor(frontend, view->value.builtin->actor);
        mark_actor(frontend, view->value.builtin->other);
        break;
    case QA_APPLICATION_EVENT_Q2_MAP:
        mark_actor(frontend, view->value.q2_map->event.actor);
        mark_actor(frontend, view->value.q2_map->event.recipient);
        mark_actor(frontend, view->value.q2_map->event.target);
        break;
    case QA_APPLICATION_EVENT_Q3_MAP:
        mark_actor(frontend, view->value.q3_map->event.actor);
        mark_actor(frontend, view->value.q3_map->event.other);
        break;
    case QA_APPLICATION_EVENT_Q2_PLAYER:
        mark_actor(frontend, view->value.q2_player->event.actor);
        mark_actor(frontend, view->value.q2_player->event.target);
        break;
    case QA_APPLICATION_EVENT_Q3_EFFECT: mark_actor(frontend, view->value.q3_effect->actor); break;
    case QA_APPLICATION_EVENT_QW_COMMAND: mark_actor(frontend, view->value.qw_command->actor); break;
    default: break;
    }
}
static bool alias_keep(void *opaque, const qa_id_index_entry *entry)
{
    const qa_frontend *frontend = opaque;
    return qa_stamp_set_test(&frontend->audio_marks, entry->value);
}
static bool position_keep(void *opaque, uint64_t bus)
{
    const qa_frontend *frontend = opaque;
    size_t slot;
    /* The engine can also hold non-frontend actor namespaces. */
    return !bus_slot(frontend, bus, &slot) || qa_stamp_set_test(&frontend->audio_marks, slot);
}
static bool registry_keep(void *opaque, const qa_id_index_entry *entry)
{
    const qa_frontend *frontend = opaque;
    const frontend_audio_registry *row = qa_pool_at(&frontend->audio_registry_records, entry->value);
    return row->actors || qa_actor_index_retained_count(&row->index);
}
static void empty_registries_release(qa_frontend *frontend)
{
    (void)qa_id_index_retain(&frontend->audio_registry_index, registry_keep, frontend);
    frontend_audio_registry **binding = &frontend->audio_registries;
    while (*binding) {
        frontend_audio_registry *row = *binding;
        if (!row->actors && !qa_actor_index_retained_count(&row->index)) {
            *binding = row->next;
            qa_actor_index_destroy(&row->index);
            qa_pool_release(&frontend->audio_registry_records, row->slot);
        } else binding = &row->next;
    }
}
void frontend_audio_collect(qa_frontend *frontend)
{
    if (!frontend || frontend->stepping || frontend->preparing || frontend->capture ||
        frontend->resource_inventory || frontend->source_restoring ||
        frontend->audio_marks.count < frontend->audio_ids.capacity) return;
    if (!retire_dead_aliases(frontend)) {
        empty_registries_release(frontend);
        return;
    }
    qa_stamp_set_begin(&frontend->audio_marks);
    for (const frontend_audio_identity *row = frontend->audio_first; row; row = row->next)
        if (!row->retired) (void)qa_stamp_set_mark(&frontend->audio_marks, row->slot);
    qa_audio_engine_actors_visit(frontend->audio, mark_bus, frontend);
    qa_application_events_visit_retained(frontend->application, record_actors, frontend);
    qa_application_audio_source_actors_visit(frontend->application, mark_actor, frontend);
    for (const frontend_remote_unified *replica = frontend->remote_unified; replica; replica = replica->next)
        frontend_remote_unified_presentation_actors_visit(replica, mark_actor, frontend);
    qa_audio_engine_positions_retain(frontend->audio, position_keep, frontend);
    (void)qa_id_index_retain(&frontend->audio_reverse, alias_keep, frontend);
    for (frontend_audio_registry *row = frontend->audio_registries; row; row = row->next)
        (void)qa_id_index_retain(&row->index.retained, alias_keep, frontend);
    frontend_audio_identity **next = &frontend->audio_first;
    frontend->audio_last = NULL;
    while (*next) {
        frontend_audio_identity *row = *next;
        if (row->retired && !qa_stamp_set_test(&frontend->audio_marks, row->slot)) {
            *next = row->next;
            qa_pool_release(&frontend->audio_ids, row->slot);
        } else {
            frontend->audio_last = row;
            next = &row->next;
        }
    }
    empty_registries_release(frontend);
}
uint64_t frontend_audio_actor(qa_frontend *frontend, qa_actor_id actor, qa_error *error)
{
    return frontend_audio_bind(frontend, &frontend->audio_local, actor, false, error);
}
bool frontend_audio_actor_for_id(const qa_frontend *frontend, uint64_t id, qa_actor_id *actor)
{
    qa_id_index_cursor cursor = qa_id_index_begin(&frontend->audio_reverse, QA_STRING_NONE, id);
    size_t slot;
    if (!qa_id_index_next(&frontend->audio_reverse, &cursor, &slot)) return false;
    const frontend_audio_identity *row = qa_pool_at(&frontend->audio_ids, slot);
    if (row->retired) return false;
    *actor = row->actor; return true;
}
static uint64_t retained_actor(qa_frontend *frontend, qa_actor_id actor, qa_error *error)
{
    return frontend_audio_bind(frontend, &frontend->audio_local, actor, true, error);
}
uint64_t frontend_audio_native_q3_actor(frontend_native_q3 *row,
    uint32_t source_number, qa_error *error)
{
    if (!row || !frontend_native_q3_current(row) || !row->frontend->audio ||
        row->frontend->capture || row->frontend->resource_inventory || row->frontend->source_restoring ||
        source_number >= QA_Q3_ENTITY_WORLD) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Native Q3 sound requires its actual installed CLIENT receipt");
        return QA_AUDIO_NO_ACTOR;
    }
    qa_actor_id actor;
    bool present;
    if (!qa_native_q3_wire_reader_actor(row->view.reader, source_number, &actor, &present, error))
        return QA_AUDIO_NO_ACTOR;
    if (!actor.registry) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Native Q3 sound has no mapped Source actor receipt");
        return QA_AUDIO_NO_ACTOR;
    }
    /* CG can first position a received entity after GAME has freed it. Its
     * retained full generation owns the sound; its decoded position follows
     * through S_UpdateEntityPosition, without reading a replacement body. */
    return retained_actor(row->frontend, actor, error);
}
static bool retained_event_source(qa_frontend *frontend, qa_actor_owner owner, qa_game_family family,
    qa_error *error)
{
    qa_clock_state clock;
    if (!qa_session_clock(qa_application_session(frontend->application),
        owner, &clock)) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Retained sound lost its actual Source clock");
        return false;
    }
    if ((unsigned)clock.frame.kind > QA_RULESET_Q3) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Retained sound has no gameplay Source clock");
        return false;
    }
    bool matches = qa_ruleset_descriptors[clock.frame.kind].family == family;
    return matches || frontend_fail(error, QA_ERROR_ARGUMENT, "Retained sound changed its Source family");
}
uint64_t frontend_audio_retained_event_actor(qa_frontend *frontend,
    const qa_builtin_event *event, qa_error *error)
{
    if (!frontend || !frontend->application || !frontend->stepping || !frontend->audio ||
        frontend->capture || frontend->resource_inventory || frontend->source_restoring ||
        !event || (event->kind != QA_BUILTIN_SOUND && event->kind != QA_BUILTIN_MUZZLE) ||
        (event->kind == QA_BUILTIN_SOUND && (event->flags & 1u)) || !event->actor.registry || !event->provider) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Retained sound requires its actual event delivery boundary");
        return QA_AUDIO_NO_ACTOR;
    }
    if (!retained_event_source(frontend,event->provider,event->family,error)) return QA_AUDIO_NO_ACTOR;
    return retained_actor(frontend, event->actor, error);
}
uint64_t frontend_audio_q2_protocol_actor(qa_frontend *frontend,
    const qa_application_protocol_event *message, qa_actor_id actor, qa_error *error)
{
    if (!frontend || !frontend->application || !frontend->stepping || !frontend->audio ||
        frontend->capture || frontend->resource_inventory || frontend->source_restoring ||
        !message || !actor.registry || !message->provider) {
        frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 protocol sound requires its actual delivery boundary");
        return QA_AUDIO_NO_ACTOR;
    }
    qa_application_event_view output;
    const qa_application_protocol_event *retained=message;
    if (message->event_id) {
        if (!qa_application_event_read(frontend->application, &(qa_application_event_cursor){.id = message->event_id, .projection = 0}, &output) || !output.protocol) {
            frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 protocol sound lost its retained event ID");
            return QA_AUDIO_NO_ACTOR;
        }
        retained=output.protocol;
    }
    bool referenced=false;
    for (size_t i=0;i<retained->reference_count;++i)
        if (qa_actor_id_equal(retained->references[i].actor,actor)) { referenced=true; break; }
    if (!referenced || !retained_event_source(frontend,retained->provider,QA_GAME_Q2,error)) {
        if (!referenced)
            frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 protocol sound lost its captured actor reference");
        return QA_AUDIO_NO_ACTOR;
    }
    return retained_actor(frontend,actor,error);
}
