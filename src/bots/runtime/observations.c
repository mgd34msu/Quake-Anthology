#include "internal.h"

static const qa_id_index_entry *module_entry(const qa_bot_runtime *r, int32_t number) {
    return qa_id_index_find((qa_id_index *)&r->observation_index, 0, (uint32_t)number);
}
static size_t module_slot(const qa_bot_runtime *r, int32_t number) {
    const qa_id_index_entry *entry = module_entry(r, number);
    return entry ? entry->value + 1 : 0;
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
        qa_id_index_clear(&r->observation_index);
        qa_pool_reset(&r->observation_slots);
    }
}
void bot_runtime_observations_close(qa_bot_runtime *r) {
    qa_id_index_destroy(&r->observation_index);
    qa_arena_destroy(&r->observation_storage);
    r->observation_slots = (qa_pool){0};
    r->entities = NULL; r->goal_entities = NULL;
    r->entity_capacity = 0;
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
    if (count > SIZE_MAX / sizeof(*r->entities) || count > SIZE_MAX / sizeof(*r->goal_entities))
        return bot_runtime_fail(e, "bot observation capacity overflow");
    qa_arena storage = {0};
    qa_pool slots = {0};
    qa_id_index index = {0};
    if (module) {
        index.storage = &storage;
        if (!qa_id_index_reserve(&index, count, e) ||
            !qa_pool_prepare(&slots, &storage, count, sizeof(*r->entities),
                _Alignof(qa_bot_entity_info), e)) goto failed;
    }
    qa_bot_entity_info *entities = module ? (qa_bot_entity_info *)slots.values : count ?
        qa_arena_alloc(&storage, count * sizeof(*entities), _Alignof(qa_bot_entity_info), e) : NULL;
    qa_bot_goal_entity *goals = count ? qa_arena_alloc(&storage,
        count * sizeof(*goals), _Alignof(qa_bot_goal_entity), e) : NULL;
    if (count && (!entities || !goals)) goto failed;
    if (count) memset(entities, 0, count * sizeof(*entities));
    size_t copy = count < r->entity_capacity ? count : r->entity_capacity;
    if (copy) memcpy(entities, r->entities, copy * sizeof(*entities));
    for (size_t i = copy; i < count; ++i) entities[i].number = (int32_t)i;
    if (module) {
        size_t free_count = 0;
        size_t *free_slots = r->entity_capacity && r->observation_slots.head != SIZE_MAX ?
            qa_arena_alloc(&storage, copy * sizeof(*free_slots), _Alignof(size_t), e) : NULL;
        if (r->entity_capacity && r->observation_slots.head != SIZE_MAX && !free_slots) goto failed;
        for (size_t id = r->entity_capacity ? r->observation_slots.head : SIZE_MAX;
             id != SIZE_MAX; id = r->observation_slots.next[id])
            free_slots[free_count++] = id;
        for (size_t i = 0; i < count; ++i) {
            size_t slot;
            (void)qa_pool_take(&slots, &slot);
        }
        while (free_count) qa_pool_release(&slots, free_slots[--free_count]);
        for (size_t i = count; i-- > copy;) qa_pool_release(&slots, i);
        for (size_t i = 0; i < r->observation_index.count; ++i) {
            const qa_id_index_entry *entry = &r->observation_index.entries[i];
            qa_id_index_insert(&index, entry->key, entry->discriminator, entry->value);
        }
    }
    qa_id_index_destroy(&r->observation_index);
    qa_arena_destroy(&r->observation_storage);
    r->observation_storage = storage; storage = (qa_arena){0};
    r->observation_index = index; index = (qa_id_index){0};
    if (module) r->observation_index.storage = &r->observation_storage;
    r->observation_slots = slots;
    r->entities = entities;
    r->goal_entities = goals;
    r->entity_capacity = count;
    return true;
failed:
    qa_id_index_destroy(&index);
    qa_arena_destroy(&storage);
    return false;
}
bool qa_bot_runtime_update_entity(qa_bot_runtime *r, int32_t number,
                                   const qa_bot_entity_update *update, qa_error *e) {
    if (!r || r->busy || r->restore_pending || r->observation_leases)
        return bot_runtime_fail(e, "bot observations are absent or in use");
    bool module = r->options.observations == QA_BOT_OBSERVATION_MODULE;
    if (!module && (r->closed || !r->initialized || number < 0 || (size_t)number >= r->entity_capacity))
        return bot_runtime_fail(e, "bot entity observation is outside configured source capacity");
    size_t slot = module ? module_slot(r, number) : (size_t)number + 1;
    if (!update) {
        if (module && slot) {
            (void)qa_id_index_remove(&r->observation_index, 0, (uint32_t)number);
            qa_pool_release(&r->observation_slots, slot - 1);
            r->entities[slot - 1] = (qa_bot_entity_info){0};
        }
        return true;
    }
    if (!module && (!qa_vec_finite(update->origin) || !qa_vec_finite(update->angles) ||
        !qa_vec_finite(update->old_origin) || !qa_vec_finite(update->mins) || !qa_vec_finite(update->maxs)))
        return bot_runtime_fail(e, "nonfinite bot entity observation");
    if (module && !slot) {
        if ((!r->entity_capacity || r->observation_slots.head == SIZE_MAX) &&
            !bot_runtime_observations_resize(r, r->entity_capacity + 1, e)) return false;
        size_t physical;
        (void)qa_pool_take(&r->observation_slots, &physical);
        qa_id_index_insert(&r->observation_index, 0, (uint32_t)number, physical);
        slot = physical + 1;
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
    if (!r || !out || !found) {
        (void)bot_runtime_fail(e, "missing bot entity observation output");
        return false;
    }
    const qa_bot_entity_info *info = bot_runtime_observation(r, number);
    *found = info != NULL;
    *out = info ? *info : (qa_bot_entity_info){.number = number};
    return true;
}
int32_t qa_bot_runtime_next_entity(const qa_bot_runtime *r, int32_t after) {
    if (!r || after == INT32_MAX) return 0;
    if (r->options.observations == QA_BOT_OBSERVATION_MODULE) {
        int32_t next = 0;
        for (size_t i = 0; i < r->observation_index.count; ++i) {
            const qa_bot_entity_info *info = &r->entities[r->observation_index.entries[i].value];
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
        const qa_id_index_entry *entry = previous ? module_entry(r, *previous) : NULL;
        size_t ordinal = previous ? entry ? (size_t)(entry - r->observation_index.entries) + 1 :
            r->observation_index.count : 0;
        if (ordinal < r->observation_index.count) slot = r->observation_index.entries[ordinal].value + 1;
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
    const qa_bot_variable *v = r->dropped_weight;
    return v ? v->value : 1000;
}
static bool maximum_level_items(void *context, int32_t *out, qa_error *error) {
    return bot_runtime_integer(context, "max_levelitems", "256", out, error);
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
static bool severity(void *context, qa_script_severity kind, const char *message,qa_error *error) {
    qa_bot_runtime *r = context;
    bool previous = r->busy; r->busy = true;
    bool ok=r->services.goals.report ? r->services.goals.report(r->services.goals.context,kind,message,error) :
        qa_bot_log_print(r->log,kind,message,error);
    r->busy = previous;
    return ok;
}
static bool log_text(void *context, const char *message, qa_error *error) {
    qa_bot_runtime *r = context;
    bool previous = r->busy; r->busy = true;
    bool okay = r->services.goals.log ?
        r->services.goals.log(r->services.goals.context, message, error) :
        qa_bot_log_write(r->log, message, error);
    r->busy = previous;
    return okay;
}
static bool developer(void *context) {
    qa_bot_runtime *r = context;
    const qa_bot_variable *value = r->developer;
    return value && value->value != 0;
}
static bool debug(void *context) { return ((qa_bot_runtime *)context)->options.debug; }
qa_bot_goal_services bot_runtime_goal_services(qa_bot_runtime *r) {
    return (qa_bot_goal_services){.context = r, .strings = r->services.goals.strings,
        .actors = r->services.goals.actors,
        .navigation = navigation,
        .entities = entities, .entities_end = entities_end, .entity = entity,
        .dropped_weight = dropped_weight, .maximum_level_items = maximum_level_items,
        .pickups = r->services.goals.pickups ? pickups : NULL,
        .pickups_end = r->services.goals.pickups_end ? pickups_end : NULL,
        .pickup = r->services.goals.pickup ? pickup : NULL,
        .owns_item = r->services.goals.owns_item ? owns_item : NULL, .diagnostic = report,
        .report = severity, .log = log_text, .developer = developer, .debug = debug};
}
