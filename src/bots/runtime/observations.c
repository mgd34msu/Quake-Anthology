#include "internal.h"

size_t bot_runtime_observation_bucket(int32_t number, size_t capacity) {
    uint32_t value = (uint32_t)number;
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    return (size_t)value & (capacity - 1);
}
static size_t module_slot(const qa_bot_runtime *r, int32_t number) {
    if (!r->entity_capacity) return 0;
    for (size_t id = r->observation_buckets[bot_runtime_observation_bucket(number, r->entity_capacity)]; id;
         id = r->observation_links[id - 1].hash_next)
        if (r->entities[id - 1].number == number) return id;
    return 0;
}
const qa_bot_entity_info *bot_runtime_observation(const qa_bot_runtime *r, int32_t number) {
    if (r->options.observations == QA_BOT_OBSERVATION_MODULE) {
        size_t id = module_slot(r, number);
        return id ? &r->entities[id - 1] : NULL;
    }
    return number >= 0 && (size_t)number < r->entity_capacity ? &r->entities[number] : NULL;
}
void bot_runtime_observations_clear(qa_bot_runtime *r) {
    for (size_t i = 0; i < r->entity_capacity; ++i)
        r->entities[i] = (qa_bot_entity_info){.number = (int32_t)i};
    if (r->options.observations == QA_BOT_OBSERVATION_MODULE) {
        if (r->entity_capacity)
            memset(r->observation_buckets, 0, r->entity_capacity * sizeof(*r->observation_buckets));
        for (size_t i = 0; i < r->entity_capacity; ++i)
            r->observation_links[i] = (bot_observation_link){
                .next = i + 1 < r->entity_capacity ? i + 2 : 0};
        r->observation_head = r->observation_tail = 0;
        r->observation_free = r->entity_capacity ? 1 : 0;
    }
}
void bot_runtime_observations_close(qa_bot_runtime *r) {
    free(r->entities); r->entities = NULL;
    free(r->goal_entities); r->goal_entities = NULL;
    free(r->observation_links); r->observation_links = NULL;
    free(r->observation_buckets); r->observation_buckets = NULL;
    r->entity_capacity = r->observation_head = r->observation_tail = r->observation_free = 0;
}
bool bot_runtime_observations_resize(qa_bot_runtime *r, size_t count, qa_error *e) {
    bool module = r->options.observations == QA_BOT_OBSERVATION_MODULE;
    if (module) {
        if (count <= r->entity_capacity) return true;
        size_t rounded = r->entity_capacity ? r->entity_capacity : 64;
        while (rounded < count) {
            if (rounded > SIZE_MAX / 2) return bot_runtime_fail(e, "bot observation capacity overflow");
            rounded *= 2;
        }
        count = rounded;
    }
    if (count > SIZE_MAX / sizeof(*r->entities) || count > SIZE_MAX / sizeof(*r->goal_entities) ||
        count > SIZE_MAX / sizeof(*r->observation_links) ||
        count > SIZE_MAX / sizeof(*r->observation_buckets))
        return bot_runtime_fail(e, "bot observation capacity overflow");
    qa_bot_entity_info *entities = count ? calloc(count, sizeof(*entities)) : NULL;
    qa_bot_goal_entity *goals = count ? malloc(count * sizeof(*goals)) : NULL;
    bot_observation_link *links = module && count ? calloc(count, sizeof(*links)) : NULL;
    size_t *buckets = module && count ? calloc(count, sizeof(*buckets)) : NULL;
    if (count && (!entities || !goals || (module && (!links || !buckets)))) {
        free(entities); free(goals); free(links); free(buckets);
        qa_error_set(e, QA_ERROR_MEMORY, count, "allocating retained bot observations");
        return false;
    }
    size_t copy = count < r->entity_capacity ? count : r->entity_capacity;
    if (copy) memcpy(entities, r->entities, copy * sizeof(*entities));
    for (size_t i = copy; i < count; ++i) entities[i].number = (int32_t)i;
    if (module) {
        if (copy) memcpy(links, r->observation_links, copy * sizeof(*links));
        for (size_t id = r->observation_head; id; id = links[id - 1].next) {
            size_t index = bot_runtime_observation_bucket(entities[id - 1].number, count);
            links[id - 1].hash_next = buckets[index];
            buckets[index] = id;
        }
        for (size_t i = count; i-- > copy;) {
            links[i].next = r->observation_free;
            r->observation_free = i + 1;
        }
    }
    free(r->entities); free(r->goal_entities);
    free(r->observation_links); free(r->observation_buckets);
    r->entities = entities;
    r->goal_entities = goals;
    r->observation_links = links;
    r->observation_buckets = buckets;
    r->entity_capacity = count;
    return true;
}
bool qa_bot_runtime_update_entity(qa_bot_runtime *r, int32_t number,
                                   const qa_bot_entity_update *update, qa_error *e) {
    if (!r || r->busy || r->observation_leases)
        return bot_runtime_fail(e, "bot observations are absent or in use");
    bool module = r->options.observations == QA_BOT_OBSERVATION_MODULE;
    if (!module && (r->closed || !r->initialized || number < 0 || (size_t)number >= r->entity_capacity))
        return bot_runtime_fail(e, "bot entity observation is outside configured source capacity");
    size_t slot = module ? module_slot(r, number) : (size_t)number + 1;
    if (!update) {
        if (module && slot) {
            bot_observation_link *link = &r->observation_links[slot - 1];
            size_t *hash = &r->observation_buckets[bot_runtime_observation_bucket(number, r->entity_capacity)];
            while (*hash != slot) hash = &r->observation_links[*hash - 1].hash_next;
            *hash = link->hash_next;
            if (link->previous) r->observation_links[link->previous - 1].next = link->next;
            else r->observation_head = link->next;
            if (link->next) r->observation_links[link->next - 1].previous = link->previous;
            else r->observation_tail = link->previous;
            *link = (bot_observation_link){.next = r->observation_free};
            r->observation_free = slot;
            r->entities[slot - 1] = (qa_bot_entity_info){0};
        }
        return true;
    }
    if (!module && (!qa_vec_finite(update->origin) || !qa_vec_finite(update->angles) ||
        !qa_vec_finite(update->old_origin) || !qa_vec_finite(update->mins) || !qa_vec_finite(update->maxs)))
        return bot_runtime_fail(e, "nonfinite bot entity observation");
    if (module && !slot) {
        if (!r->observation_free &&
            !bot_runtime_observations_resize(r, r->entity_capacity + 1, e)) return false;
        slot = r->observation_free;
        bot_observation_link *link = &r->observation_links[slot - 1];
        r->observation_free = link->next;
        size_t hash = bot_runtime_observation_bucket(number, r->entity_capacity);
        *link = (bot_observation_link){.previous = r->observation_tail,
            .hash_next = r->observation_buckets[hash]};
        r->observation_buckets[hash] = slot;
        if (r->observation_tail) r->observation_links[r->observation_tail - 1].next = slot;
        else r->observation_head = slot;
        r->observation_tail = slot;
        r->entities[slot - 1] = (qa_bot_entity_info){.number = number};
    }
    qa_bot_entity_info *info = &r->entities[slot - 1];
    bool same = qa_actor_id_equal(info->state.actor, update->actor);
    qa_vec3 previous = module || same ? info->state.origin : (qa_vec3){0};
    float previous_time = module || same ? info->last_update_time : 0;
    if (module && !info->valid) { previous = update->origin; previous_time = r->time; }
    *info = (qa_bot_entity_info){.state = *update, .number = number, .valid = true,
        .last_visible_origin = previous, .last_update_time = r->time,
        .update_interval = r->time - previous_time};
    return true;
}
bool qa_bot_runtime_entity(const qa_bot_runtime *r, int32_t number,
                            qa_bot_entity_info *out, bool *found, qa_error *e) {
    if (!r || !out || !found) return bot_runtime_fail(e, "missing bot entity observation output");
    const qa_bot_entity_info *info = bot_runtime_observation(r, number);
    *found = info != NULL;
    *out = info ? *info : (qa_bot_entity_info){.number = number};
    return true;
}
int32_t qa_bot_runtime_next_entity(const qa_bot_runtime *r, int32_t after) {
    if (!r || after == INT32_MAX) return 0;
    if (r->options.observations == QA_BOT_OBSERVATION_MODULE) {
        int32_t next = 0;
        for (size_t id = r->observation_head; id; id = r->observation_links[id - 1].next) {
            const qa_bot_entity_info *info = &r->entities[id - 1];
            if (info->valid && info->number > after && (!next || info->number < next)) next = info->number;
        }
        return next;
    }
    size_t first = after < 0 ? 0 : (size_t)after + 1;
    for (size_t i = first; i < r->entity_capacity; ++i)
        if (r->entities[i].valid) return (int32_t)i;
    return 0;
}
bool qa_bot_runtime_entity_after(const qa_bot_runtime *r, const int32_t *previous,
                                  qa_bot_entity_info *out, bool *found, qa_error *e) {
    if (!r || !out || !found) return bot_runtime_fail(e, "missing ordered bot observation output");
    size_t slot = 0;
    if (r->options.observations == QA_BOT_OBSERVATION_MODULE) {
        slot = previous ? module_slot(r, *previous) : 0;
        slot = previous ? slot ? r->observation_links[slot - 1].next : 0 : r->observation_head;
    } else {
        size_t start = previous && *previous >= 0 ? (size_t)*previous + 1 : 0;
        for (size_t i = start; i < r->entity_capacity; ++i)
            if (r->entities[i].valid) { slot = i + 1; break; }
    }
    *found = slot != 0;
    *out = slot ? r->entities[slot - 1] : (qa_bot_entity_info){0};
    return true;
}
bool qa_bot_runtime_invalidate_entities(qa_bot_runtime *r, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    for (size_t i = 0; i < r->entity_capacity; ++i) r->entities[i].valid = false;
    return true;
}
bool qa_bot_runtime_invalidate_entity_range(qa_bot_runtime *r,int32_t first,uint32_t count,qa_error *e) {
    if(!bot_runtime_mutable(r,e)) return false;
    if(first<0 || count>(uint32_t)INT32_MAX-(uint32_t)first)
        return bot_runtime_fail(e,"bot observation invalidation range exceeds signed identity");
    for(size_t i=0;i<r->entity_capacity;++i)
        if(r->entities[i].number>=first && (uint32_t)(r->entities[i].number-first)<count)
            r->entities[i].valid=false;
    return true;
}
static qa_bot_navigation *navigation(void *context, int32_t client) {
    return qa_bot_runtime_navigation(context, client);
}
static bool entity(void *context, int32_t number, qa_bot_goal_entity *out, bool *found, qa_error *e) {
    qa_bot_entity_info info;
    if (!qa_bot_runtime_entity(context, number, &info, found, e)) return false;
    if (*found) *out = (qa_bot_goal_entity){.actor = info.state.actor, .number = number,
        .type = info.state.type, .model_index = info.state.model_index, .origin = info.state.origin,
        .last_visible_origin = info.last_visible_origin, .last_update_time = info.last_update_time};
    return true;
}
static float dropped_weight(void *context) {
    qa_bot_runtime *r = context;
    const qa_bot_variable *v = qa_bot_library_variable(r->library, "droppedweight");
    return v ? v->value : 1000;
}
static int goal_order(const void *a, const void *b) {
    int32_t x = ((const qa_bot_goal_entity *)a)->number;
    int32_t y = ((const qa_bot_goal_entity *)b)->number;
    return (x > y) - (x < y);
}
static bool entities(void *context, const qa_bot_goal_entity **out, size_t *count,
                      void **lease, qa_error *e) {
    qa_bot_runtime *r = context;
    if (r->observation_leases) return bot_runtime_fail(e, "nested bot entity snapshot is unsupported");
    size_t length = 0;
    for (size_t i = 0; i < r->entity_capacity; ++i) {
        const qa_bot_entity_info *info = &r->entities[i];
        if (!info->valid || info->number <= 0) continue;
        r->goal_entities[length++] = (qa_bot_goal_entity){.actor = info->state.actor,
            .number = info->number, .type = info->state.type, .model_index = info->state.model_index,
            .origin = info->state.origin, .last_visible_origin = info->last_visible_origin,
            .last_update_time = info->last_update_time};
    }
    if (r->options.observations == QA_BOT_OBSERVATION_MODULE && length > 1)
        qsort(r->goal_entities, length, sizeof(*r->goal_entities), goal_order);
    ++r->observation_leases;
    *lease = r;
    *out = r->goal_entities;
    *count = length;
    return true;
}
static void entities_end(void *context, void *lease) {
    qa_bot_runtime *r = context;
    if (lease == r && r->observation_leases) --r->observation_leases;
}
static bool pickups(void *context, int32_t client, const qa_actor_id **out, size_t *count,
                     void **lease, qa_error *e) {
    qa_bot_runtime *r = context;
    bool previous = r->busy; r->busy = true;
    bool ok = r->services.goals.pickups(r->services.goals.context, client, out, count, lease, e);
    r->busy = previous;
    return ok;
}
static void pickups_end(void *context, void *lease) {
    qa_bot_runtime *r = context;
    bool previous = r->busy; r->busy = true;
    r->services.goals.pickups_end(r->services.goals.context, lease);
    r->busy = previous;
}
static bool pickup(void *context, int32_t client, qa_actor_id id, qa_bot_pickup_goal *out,
                    bool *found, qa_error *e) {
    qa_bot_runtime *r = context;
    bool previous = r->busy; r->busy = true;
    bool ok = r->services.goals.pickup(r->services.goals.context, client, id, out, found, e);
    r->busy = previous;
    return ok;
}
static bool owns_item(void *context, int32_t client, int32_t entity, bool *out, qa_error *e) {
    qa_bot_runtime *r = context;
    bool previous = r->busy; r->busy = true;
    bool ok = r->services.goals.owns_item(r->services.goals.context, client, entity, out, e);
    r->busy = previous;
    return ok;
}
static void report(void *context, const char *message) {
    qa_bot_runtime *r = context;
    bool previous = r->busy; r->busy = true;
    if (r->services.goals.diagnostic) r->services.goals.diagnostic(r->services.goals.context, message);
    else if (r->services.diagnostic) r->services.diagnostic(r->services.context, QA_SCRIPT_WARNING, message);
    r->busy = previous;
}
static void severity(void *context, qa_script_severity kind, const char *message) {
    qa_bot_runtime *r = context;
    bool previous = r->busy; r->busy = true;
    if (r->services.goals.report) r->services.goals.report(r->services.goals.context, kind, message);
    else if (r->services.diagnostic) r->services.diagnostic(r->services.context, kind, message);
    r->busy = previous;
}
static void log_text(void *context, const char *message) {
    qa_bot_runtime *r = context;
    bool previous = r->busy; r->busy = true;
    if (r->services.goals.log) r->services.goals.log(r->services.goals.context, message);
    r->busy = previous;
}
static bool developer(void *context) {
    qa_bot_runtime *r = context;
    const qa_bot_variable *value = qa_bot_library_variable(r->library, "bot_developer");
    return value && value->value != 0;
}
static bool debug(void *context) { return ((qa_bot_runtime *)context)->options.debug; }
qa_bot_goal_services bot_runtime_goal_services(qa_bot_runtime *r) {
    return (qa_bot_goal_services){.context = r, .navigation = navigation,
        .entities = entities, .entities_end = entities_end, .entity = entity,
        .dropped_weight = dropped_weight,
        .pickups = r->services.goals.pickups ? pickups : NULL,
        .pickups_end = r->services.goals.pickups_end ? pickups_end : NULL,
        .pickup = r->services.goals.pickup ? pickup : NULL,
        .owns_item = r->services.goals.owns_item ? owns_item : NULL, .diagnostic = report,
        .report = severity, .log = log_text, .developer = developer, .debug = debug};
}
