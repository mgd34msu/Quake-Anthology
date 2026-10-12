#include "internal.h"
#include "../save_fields.h"
#include "qa/bot_observations_save.h"

static const uint8_t magic[8] = {'Q', 'A', 'B', 'O', 'B', 'S', 'V', 0};

typedef struct bot_observation_link {
    size_t previous, next, hash_next;
} bot_observation_link;
typedef struct saved_observations {
    qa_bot_observation_profile profile;
    size_t entity_capacity, observation_head, observation_tail, observation_free;
    const qa_bot_entity_info *entities;
    const qa_bot_goal_entity *goal_entities;
    bot_observation_link *observation_links;
    size_t *observation_buckets;
} saved_observations;

/* QABOBSV retains the original hash topology only at the save boundary. */
static size_t observation_bucket(int32_t number, size_t capacity) {
    uint32_t value = (uint32_t)number;
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    return (size_t)value & (capacity - 1);
}

static bool entity_fields(qa_source_save_io *io, qa_bot_entity_info *info)
{
    qa_bot_entity_update *state = &info->state;
    return qa_source_save_actor(io, &state->actor) && qa_source_save_i32(io, &state->type) &&
        qa_source_save_i32(io, &state->flags) && qa_source_save_vec3(io, &state->origin) &&
        qa_source_save_vec3(io, &state->angles) && qa_source_save_vec3(io, &state->old_origin) &&
        qa_source_save_vec3(io, &state->mins) && qa_source_save_vec3(io, &state->maxs) &&
        qa_source_save_i32(io, &state->ground_entity) && qa_source_save_i32(io, &state->solid) &&
        qa_source_save_i32(io, &state->model_index) && qa_source_save_i32(io, &state->model_index2) &&
        qa_source_save_i32(io, &state->frame) && qa_source_save_i32(io, &state->event) &&
        qa_source_save_i32(io, &state->event_parameter) && qa_source_save_i32(io, &state->powerups) &&
        qa_source_save_i32(io, &state->weapon) && qa_source_save_i32(io, &state->legs_animation) &&
        qa_source_save_i32(io, &state->torso_animation) && qa_source_save_bool(io, &info->valid) &&
        qa_source_save_i32(io, &info->number) && qa_source_save_vec3(io, &info->last_visible_origin) &&
        qa_source_save_f32(io, &info->last_update_time) && qa_source_save_f32(io, &info->update_interval);
}

static bool topology_valid(const saved_observations *runtime, qa_error *error)
{
    size_t count = runtime->entity_capacity;
    if (count && (!runtime->entities || !runtime->goal_entities))
        goto invalid;
    if (runtime->profile == QA_BOT_OBSERVATION_NATIVE) {
        if (runtime->observation_head || runtime->observation_tail || runtime->observation_free ||
            runtime->observation_links || runtime->observation_buckets || count > INT32_MAX)
            goto invalid;
        for (size_t i = 0; i < count; ++i)
            if (runtime->entities[i].number != (int32_t)i)
                goto invalid;
        return true;
    }
    if (runtime->profile != QA_BOT_OBSERVATION_MODULE ||
        (count && (count < 64 || (count & (count - 1)) ||
            !runtime->observation_links || !runtime->observation_buckets)) ||
        runtime->observation_head > count || runtime->observation_tail > count ||
        runtime->observation_free > count)
        goto invalid;
    uint8_t *marks = count ? calloc(count, 1) : NULL;
    if (count && !marks) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Validating bot observation topology");
        return false;
    }
    bool ok = true;
    size_t previous = 0;
    for (size_t id = runtime->observation_head; ok && id;) {
        if (id > count || marks[id - 1]) { ok = false; break; }
        const bot_observation_link *link = &runtime->observation_links[id - 1];
        if (link->previous != previous || link->next > count || link->hash_next > count) {
            ok = false; break;
        }
        marks[id - 1] = 1;
        previous = id;
        id = link->next;
    }
    if (previous != runtime->observation_tail)
        ok = false;
    for (size_t id = runtime->observation_free; ok && id;) {
        if (id > count || marks[id - 1]) { ok = false; break; }
        const bot_observation_link *link = &runtime->observation_links[id - 1];
        if (link->previous || link->hash_next || link->next > count || runtime->entities[id - 1].valid) {
            ok = false; break;
        }
        marks[id - 1] = 2;
        id = link->next;
    }
    for (size_t i = 0; ok && i < count; ++i) {
        if (!marks[i]) { ok = false; break; }
        size_t id = runtime->observation_buckets[i];
        for (size_t traversed = 0; ok && id; ++traversed) {
            if (id > count || traversed >= count || marks[id - 1] != 1 ||
                observation_bucket(runtime->entities[id - 1].number, count) != i) {
                ok = false; break;
            }
            marks[id - 1] = 3;
            size_t next = runtime->observation_links[id - 1].hash_next;
            for (size_t cursor = next, checked = 0; ok && cursor; ++checked) {
                if (cursor > count || checked >= count ||
                    runtime->entities[cursor - 1].number == runtime->entities[id - 1].number) {
                    ok = false; break;
                }
                cursor = runtime->observation_links[cursor - 1].hash_next;
            }
            id = next;
        }
    }
    for (size_t i = 0; ok && i < count; ++i)
        if (marks[i] == 1)
            ok = false;
    free(marks);
    if (ok)
        return true;
invalid:
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid bot observation slot/hash/free topology");
    return false;
}

bool qa_bot_observations_capture(qa_session *session, const qa_bot_runtime *runtime,
                                 qa_buffer *out, qa_error *error)
{
    if (!session || !runtime || !out || !qa_bot_runtime_can_destroy(runtime)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot observation owner is absent or borrowed");
        return false;
    }
    size_t count = runtime->entity_capacity;
    uint32_t profile = (uint32_t)runtime->options.observations;
    qa_arena storage = {0};
    saved_observations topology = {.profile = runtime->options.observations, .entity_capacity = count,
        .entities = runtime->entities, .goal_entities = runtime->goal_entities};
    bool ok = true;
    if (profile == QA_BOT_OBSERVATION_MODULE && count) {
        topology.observation_links = qa_arena_alloc(&storage, count * sizeof(*topology.observation_links),
            _Alignof(bot_observation_link), error);
        topology.observation_buckets = qa_arena_alloc(&storage, count * sizeof(*topology.observation_buckets),
            _Alignof(size_t), error);
        ok = topology.observation_links && topology.observation_buckets;
        if (ok) {
            memset(topology.observation_links, 0, count * sizeof(*topology.observation_links));
            memset(topology.observation_buckets, 0, count * sizeof(*topology.observation_buckets));
            for (size_t i = 0; i < runtime->observation_index.count; ++i) {
                size_t id = runtime->observation_index.entries[i].value + 1;
                bot_observation_link *link = &topology.observation_links[id - 1];
                link->previous = topology.observation_tail;
                if (link->previous) topology.observation_links[link->previous - 1].next = id;
                else topology.observation_head = id;
                topology.observation_tail = id;
                size_t bucket = observation_bucket(runtime->entities[id - 1].number, count);
                link->hash_next = topology.observation_buckets[bucket];
                topology.observation_buckets[bucket] = id;
            }
            if (runtime->observation_slots.head != SIZE_MAX)
                topology.observation_free = runtime->observation_slots.head + 1;
            for (size_t slot = runtime->observation_slots.head; slot != SIZE_MAX;
                 slot = runtime->observation_slots.next[slot]) {
                size_t next = runtime->observation_slots.next[slot];
                topology.observation_links[slot].next = next == SIZE_MAX ? 0 : next + 1;
            }
        }
    }
    qa_source_save_io io = {0};
    ok = ok && topology_valid(&topology, error) && qa_source_save_writer(&io, session, error) && bot_save_signature(&io, magic) &&
        qa_source_save_u32(&io, &profile) && qa_source_save_count(&io, &count, SIZE_MAX);
    for (size_t i = 0; ok && i < count; ++i) {
        qa_bot_entity_info info = runtime->entities[i];
        const qa_bot_entity_info empty = {.number = (int32_t)i};
        bool retained = memcmp(&info, &empty, sizeof(info)) != 0;
        ok = qa_source_save_bool(&io, &retained) && (!retained || entity_fields(&io, &info));
    }
    if (profile == QA_BOT_OBSERVATION_MODULE) {
        size_t head = topology.observation_head, tail = topology.observation_tail, free_head = topology.observation_free;
        ok = ok && qa_source_save_count(&io, &head, count) && qa_source_save_count(&io, &tail, count) &&
            qa_source_save_count(&io, &free_head, count);
        for (size_t i = 0; ok && i < count; ++i) {
            bot_observation_link link = topology.observation_links[i];
            size_t bucket = topology.observation_buckets[i];
            ok = qa_source_save_count(&io, &link.previous, count) && qa_source_save_count(&io, &link.next, count) &&
                qa_source_save_count(&io, &link.hash_next, count) && qa_source_save_count(&io, &bucket, count);
        }
    }
    if (ok)
        ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    qa_arena_destroy(&storage);
    return ok;
}

bool qa_bot_observations_restore(qa_session *session, qa_bot_runtime *runtime,
                                 qa_bytes bytes, qa_error *error)
{
    if (!session || !runtime || !qa_bot_runtime_can_destroy(runtime)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Detached bot observation owner is absent or borrowed");
        return false;
    }
    qa_bot_runtime scratch = {0};
    qa_arena storage = {0};
    saved_observations topology = {0};
    qa_source_save_io io = {0};
    uint32_t profile = 0;
    bool ok = qa_source_save_reader(&io, session, bytes, error) && bot_save_signature(&io, magic) &&
        qa_source_save_u32(&io, &profile) && profile == (uint32_t)runtime->options.observations &&
        qa_source_save_count(&io, &scratch.entity_capacity, bytes.size);
    scratch.options.observations = (qa_bot_observation_profile)profile;
    size_t count = scratch.entity_capacity;
    topology.profile = scratch.options.observations;
    topology.entity_capacity = count;
    if (ok && (count > SIZE_MAX / sizeof(*scratch.entities) || count > SIZE_MAX / sizeof(*scratch.goal_entities) ||
        count > SIZE_MAX / sizeof(*topology.observation_links) || count > SIZE_MAX / sizeof(*topology.observation_buckets)))
        ok = bot_save_fail(&io, QA_ERROR_FORMAT, "Bot observation extent exceeds memory");
    if (ok && profile == QA_BOT_OBSERVATION_MODULE) {
        scratch.observation_index.storage = &scratch.observation_storage;
        ok = qa_id_index_reserve(&scratch.observation_index, count, error) &&
            qa_pool_prepare(&scratch.observation_slots, &scratch.observation_storage, count,
                sizeof(*scratch.entities), _Alignof(qa_bot_entity_info), error);
        if (ok) scratch.entities = (qa_bot_entity_info *)scratch.observation_slots.values;
    }
    if (ok && count) {
        if (profile != QA_BOT_OBSERVATION_MODULE)
            scratch.entities = qa_arena_alloc(&scratch.observation_storage, count * sizeof(*scratch.entities),
                _Alignof(qa_bot_entity_info), error);
        scratch.goal_entities = qa_arena_alloc(&scratch.observation_storage, count * sizeof(*scratch.goal_entities),
            _Alignof(qa_bot_goal_entity), error);
        if (profile == QA_BOT_OBSERVATION_MODULE) {
            topology.observation_links = qa_arena_alloc(&storage, count * sizeof(*topology.observation_links),
                _Alignof(bot_observation_link), error);
            topology.observation_buckets = qa_arena_alloc(&storage, count * sizeof(*topology.observation_buckets),
                _Alignof(size_t), error);
        }
        if (!scratch.entities || !scratch.goal_entities ||
            (profile == QA_BOT_OBSERVATION_MODULE && (!topology.observation_links || !topology.observation_buckets)))
            ok = bot_save_fail(&io, QA_ERROR_MEMORY, "Restoring bot observation slots");
        if (ok) memset(scratch.entities, 0, count * sizeof(*scratch.entities));
    }
    topology.entities = scratch.entities;
    topology.goal_entities = scratch.goal_entities;
    for (size_t i = 0; ok && i < count; ++i) {
        bool retained = false;
        scratch.entities[i].number = (int32_t)i;
        ok = qa_source_save_bool(&io, &retained) &&
            (!retained || entity_fields(&io, &scratch.entities[i]));
    }
    if (profile == QA_BOT_OBSERVATION_MODULE) {
        ok = ok && qa_source_save_count(&io, &topology.observation_head, count) &&
            qa_source_save_count(&io, &topology.observation_tail, count) &&
            qa_source_save_count(&io, &topology.observation_free, count);
        for (size_t i = 0; ok && i < count; ++i) {
            bot_observation_link *link = &topology.observation_links[i];
            ok = qa_source_save_count(&io, &link->previous, count) && qa_source_save_count(&io, &link->next, count) &&
                qa_source_save_count(&io, &link->hash_next, count) &&
                qa_source_save_count(&io, &topology.observation_buckets[i], count);
        }
    }
    if (ok)
        ok = qa_source_save_finish(&io, NULL) && topology_valid(&topology, error);
    if (ok && profile == QA_BOT_OBSERVATION_MODULE) {
        for (size_t i = 0; i < count; ++i) {
            size_t slot;
            (void)qa_pool_take(&scratch.observation_slots, &slot);
        }
        size_t previous = 0;
        for (size_t id = topology.observation_free; id; id = topology.observation_links[id - 1].next) {
            topology.observation_links[id - 1].previous = previous;
            previous = id;
        }
        while (previous) {
            qa_pool_release(&scratch.observation_slots, previous - 1);
            previous = topology.observation_links[previous - 1].previous;
        }
        for (size_t id = topology.observation_head; id; id = topology.observation_links[id - 1].next)
            qa_id_index_insert(&scratch.observation_index, 0, (uint32_t)scratch.entities[id - 1].number, id - 1);
    }
    if (ok) {
        bot_runtime_observations_close(runtime);
        runtime->observation_storage = scratch.observation_storage; scratch.observation_storage = (qa_arena){0};
        runtime->observation_slots = scratch.observation_slots; scratch.observation_slots = (qa_pool){0};
        runtime->observation_index = scratch.observation_index; scratch.observation_index = (qa_id_index){0};
        if (profile == QA_BOT_OBSERVATION_MODULE) runtime->observation_index.storage = &runtime->observation_storage;
        runtime->entities = scratch.entities; scratch.entities = NULL;
        runtime->goal_entities = scratch.goal_entities; scratch.goal_entities = NULL;
        runtime->entity_capacity = count;
    }
    if (!ok && (!error || error->code == QA_OK))
        qa_error_set(error, QA_ERROR_FORMAT, io.offset, "Invalid bot observation continuation");
    qa_source_save_dispose(&io);
    bot_runtime_observations_close(&scratch);
    qa_arena_destroy(&storage);
    return ok;
}
