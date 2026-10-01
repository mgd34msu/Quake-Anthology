#include "internal.h"
#include "../save_fields.h"
#include "qa/bot_goals_save.h"
#include "qa/bots_allocator_save.h"

static const uint8_t magic[8] = {'Q', 'A', 'B', 'G', 'O', 'A', 'L', 0};
static bool signature(qa_source_save_io *io) {
    uint8_t bytes[8];memcpy(bytes,magic,sizeof(bytes));uint32_t version=2;
    return qa_source_save_bytes(io,bytes,sizeof(bytes)) && !memcmp(bytes,magic,sizeof(bytes)) &&
        qa_source_save_u32(io,&version) && version==2 ? true :
        bot_save_fail(io,QA_ERROR_FORMAT,"Unsupported goal allocation continuation schema");
}

static bool goal_fields(qa_source_save_io *io, qa_bot_goal *goal)
{
    return qa_source_save_vec3(io, &goal->origin) && qa_source_save_i32(io, &goal->area) &&
        qa_source_save_vec3(io, &goal->mins) && qa_source_save_vec3(io, &goal->maxs) &&
        qa_source_save_i32(io, &goal->entity) && qa_source_save_i32(io, &goal->number) &&
        qa_source_save_i32(io, &goal->flags) && qa_source_save_i32(io, &goal->item_info);
}
static bool state_fields(qa_source_save_io *io, bot_goal_slot *slot, size_t *weight)
{
    qa_bot_goal_state *state = &slot->state;
    bool ok = qa_source_save_bool(io, &slot->used) && qa_source_save_i32(io, &state->client) &&
        qa_source_save_i32(io, &state->last_reachability_area) && qa_source_save_u32(io, &state->stack_top) &&
        state->stack_top < QA_BOT_GOAL_STACK;
    for (size_t i = 0; ok && i < QA_BOT_GOAL_STACK; ++i) ok = goal_fields(io, &state->stack[i]);
    for (size_t i = 0; ok && i < QA_BOT_AVOID_GOALS; ++i)
        ok = qa_source_save_i32(io, &state->avoid[i].number) && qa_source_save_f32(io, &state->avoid[i].expires);
    return ok && qa_source_save_count(io, weight, SIZE_MAX);
}
static bool level_fields(qa_source_save_io *io, bot_level_item *item)
{
    return qa_source_save_i32(io, &item->number) && qa_source_save_i32(io, &item->entity) &&
        qa_source_save_u32(io, &item->info) && qa_source_save_u32(io, &item->flags) &&
        qa_source_save_u32(io, &item->previous) && qa_source_save_u32(io, &item->next) &&
        qa_source_save_f32(io, &item->weight) && qa_source_save_f32(io, &item->timeout) &&
        qa_source_save_vec3(io, &item->origin) && qa_source_save_vec3(io, &item->goal_origin) &&
        qa_source_save_u32(io, &item->goal_area);
}
static bool map_fields(qa_source_save_io *io, bot_map_goal *goal)
{
    return qa_source_save_bytes(io, goal->name, sizeof(goal->name)) && memchr(goal->name, 0, sizeof(goal->name)) &&
        qa_source_save_vec3(io, &goal->origin) && qa_source_save_u32(io, &goal->area) &&
        qa_source_save_f32(io, &goal->range) && qa_source_save_f32(io, &goal->weight) &&
        qa_source_save_f32(io, &goal->wait) && qa_source_save_f32(io, &goal->random);
}
static bool asset_field(qa_source_save_io *io, const qa_bot_saved_assets *assets,
                        qa_bot_saved_asset_kind kind, void **object)
{
    uint64_t id = 0;
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool ok = reading || qa_bot_saved_asset_id(assets, kind, *object, &id, io->error);
    if (ok) ok = qa_source_save_u64(io, &id);
    if (ok && reading) {
        const void *borrowed = NULL;
        ok = qa_bot_saved_asset_resolve(assets, kind, id, &borrowed, io->error);
        if (ok) {
            *object = (void *)borrowed;
            if (kind == QA_BOT_SAVED_ITEMS) qa_bot_items_retain(*object);
            else qa_bot_weights_retain(*object);
        }
    }
    if (!ok) io->failed = true;
    return ok;
}
static bool array(qa_source_save_io *io, void **out, size_t count, size_t stride, size_t minimum)
{
    if (count > SIZE_MAX / stride || io->offset > io->input.size || count > (io->input.size - io->offset) / minimum)
        return bot_save_fail(io, QA_ERROR_FORMAT, "Truncated bot goal pool extent");
    if (count && !(*out = calloc(count, stride)))
        return bot_save_fail(io, QA_ERROR_MEMORY, "Restoring bot goal pool");
    return true;
}
static void clear(qa_bot_goals *goals)
{
    while (goals->weights) {
        bot_goal_weights *weight = goals->weights; goals->weights = weight->next;
        qa_bot_weights_release(weight->weights); free(weight);
    }
    bot_goal_indexes_clear(goals);
    if (!goals->source) goals->source_count = 0;
    bot_goal_map_clear(goals);
    qa_bot_items_release(goals->items); free(goals->states);
}
static bool topology(const qa_bot_goals *goals, qa_error *error)
{
    size_t capacity = goals->level_capacity;
    if (!goals->items || !goals->states || (capacity && (!goals->level || capacity < 2 || capacity > UINT32_MAX)) ||
        (!capacity && (goals->level || goals->level_head || goals->free_head || goals->initial_count)) ||
        goals->initial_count < 0 || (capacity && (size_t)goals->initial_count >= capacity) ||
        goals->source_count > goals->source_capacity || goals->source_count > UINT32_MAX ||
        goals->source_capacity > SIZE_MAX / sizeof(*goals->source) ||
        (goals->source_capacity && (!goals->source || goals->source_capacity < 64 ||
                                    (goals->source_capacity & (goals->source_capacity - 1)) ||
                                    !goals->source_count || (goals->source_capacity > 64 &&
                                     goals->source_count <= goals->source_capacity / 2))) ||
        goals->next_source < QA_BOT_SOURCE_GOAL_MIN - 1 ||
        (goals->location_count && !goals->locations) || (goals->camp_count && !goals->camps)) goto invalid;
    uint8_t *marks = capacity ? calloc(capacity, 1) : NULL;
    if (capacity && !marks) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Validating bot level pool"); return false; }
    bool ok = true; uint32_t previous = 0;
    size_t item_count = qa_bot_items_read(goals->items)->count;
    for (uint32_t id = goals->level_head; ok && id;) {
        if (id >= capacity || marks[id] || goals->level[id].previous != previous ||
            goals->level[id].info >= item_count) { ok = false; break; }
        marks[id] = 1; previous = id; id = goals->level[id].next;
    }
    for (uint32_t id = goals->free_head; ok && id;) {
        if (id >= capacity || marks[id]) { ok = false; break; }
        marks[id] = 1; id = goals->level[id].next;
    }
    for (size_t i = 1; ok && i < capacity; ++i) if (!marks[i]) ok = false;
    free(marks);
    if(!goals->memory || goals->prepared_indexes || qa_bot_memory_disposed(goals->memory) ||
       !goals->next_pointer || goals->next_pointer>UINT64_C(0x100000000)) goto invalid;
    size_t wrapper_count = 0;
    for (const bot_goal_weights *weight = goals->weights; ok && weight; weight = weight->next) {
        if (!weight->weights || !weight->pointer || weight->pointer>=goals->next_pointer ||
            wrapper_count++ >= UINT32_MAX) { ok = false; break; }
        size_t users = 0;
        for (uint32_t i = 0; i < goals->options.maximum_states; ++i) if (goals->states[i].weights == weight) ++users;
        if (users != weight->users) { ok = false; break; }
        for (const bot_goal_weights *other = weight->next; other; other = other->next)
            if (other->weights == weight->weights || other->pointer==weight->pointer) { ok = false; break; }
    }
    const bot_goal_indexes *last=NULL;
    for(const bot_goal_indexes *row=goals->indexes;ok && row;row=row->next) {
        qa_bot_memory_span bytes;
        if(!row->pointer || row->prepared_users || row->pointer>=goals->next_pointer ||
           !qa_bot_memory_bytes(goals->memory,row->allocation,&bytes,error)) {ok=false;break;}
        for(const bot_goal_indexes *other=row->next;other;other=other->next)
            if(other->pointer==row->pointer) ok=false;
        for(const bot_goal_weights *weight=goals->weights;weight;weight=weight->next)
            if(weight->pointer==row->pointer) ok=false;
        last=row;
    }
    if(last!=goals->last_indexes) ok=false;
    for (uint32_t i = 0; ok && i < goals->options.maximum_states; ++i) {
        const bot_goal_slot *slot = &goals->states[i];
        if ((!slot->used && (slot->weights || slot->index_pointer)) || slot->state.stack_top >= QA_BOT_GOAL_STACK) { ok = false; break; }
        if (slot->weights) {
            const bot_goal_weights *weight = goals->weights;
            while (weight && weight != slot->weights) weight = weight->next;
            if (!weight) ok = false;
        }
        if(slot->index_pointer) {
            const bot_goal_indexes *row=goals->indexes;
            while(row && row->pointer!=slot->index_pointer) row=row->next;
            if(!row) ok=false;
        }
    }
    uint32_t buckets[256] = {0};
    for (size_t i = 0; ok && i < goals->source_count; ++i) {
        const bot_source_goal *source = &goals->source[i];
        if (!source->actor.registry || !source->name || !source->name_capacity ||
            strlen(source->name) >= source->name_capacity || source->goal.number < QA_BOT_SOURCE_GOAL_MIN ||
            (i && (int64_t)goals->source[i - 1].goal.number - 1 != source->goal.number)) { ok = false; break; }
        for (size_t j = 0; j < i; ++j) if (qa_actor_id_equal(source->actor, goals->source[j].actor)) { ok = false; break; }
        uint32_t bucket = bot_goal_source_bucket(source->actor);
        if (source->hash_next != buckets[bucket]) ok = false;
        buckets[bucket] = (uint32_t)i + 1;
    }
    if (ok && memcmp(buckets, goals->source_buckets, sizeof(buckets))) ok = false;
    if (ok && goals->source_count && (int64_t)goals->source[goals->source_count - 1].goal.number - 1 != goals->next_source) ok = false;
    if (ok) return true;
invalid:
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid bot goal handle or map pool topology"); return false;
}
static bool fields(qa_source_save_io *io, qa_bot_goals *goals, const qa_bot_saved_assets *assets,
                    const qa_entities *entities)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool has_entities = !reading && goals->entities != NULL;
    size_t entity_count = has_entities ? goals->entities->count : 0;
    size_t state_count = goals->options.maximum_states, weight_count = 0;
    if (!reading) for (bot_goal_weights *weight = goals->weights; weight; weight = weight->next) ++weight_count;
    void *items = goals->items;
    bool ok = qa_source_save_count(io, &state_count, UINT32_MAX) && state_count == goals->options.maximum_states &&
        qa_source_save_u32(io, &goals->options.maximum_level_items) && goals->options.maximum_level_items &&
        goals->options.maximum_level_items < QA_BOT_SOURCE_GOAL_MIN - 1000000 &&
        qa_source_save_i32(io, &goals->options.game_type) && qa_source_save_f32(io, &goals->options.dropped_weight) &&
        isfinite(goals->options.dropped_weight) && qa_source_save_f32(io, &goals->time) && isfinite(goals->time) &&
        qa_source_save_bool(io, &goals->configured) && qa_source_save_bool(io, &has_entities) &&
        qa_source_save_count(io, &entity_count, INT32_MAX) &&
        (!has_entities || !reading || (entities && entity_count == entities->count));
    if (ok) ok = asset_field(io, assets, QA_BOT_SAVED_ITEMS, &items);
    if (reading) { goals->items = items; goals->entities = has_entities ? entities : NULL; }
    if (ok) ok = qa_source_save_u64(io,&goals->next_pointer) &&
        qa_source_save_count(io, &weight_count, UINT32_MAX);
    if(ok && reading && weight_count>(io->input.size-io->offset)/20)
        ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated retained goal configuration references");
    if (ok && reading) ok = array(io, (void **)&goals->states, state_count, sizeof(*goals->states), 2517);
    bot_goal_weights **tail = &goals->weights;
    for (size_t i = 0; ok && i < weight_count; ++i) {
        bot_goal_weights *weight;
        if (reading) {
            weight = calloc(1, sizeof(*weight));
            if (!weight) { ok = bot_save_fail(io, QA_ERROR_MEMORY, "Restoring shared goal weights"); break; }
            *tail = weight;
        } else weight = *tail;
        void *object = weight->weights;
        ok = asset_field(io, assets, QA_BOT_SAVED_WEIGHTS, &object);
        if (reading) weight->weights = object;
        if (ok) ok = qa_source_save_count(io, &weight->users, state_count) && qa_source_save_u32(io,&weight->pointer);
        tail = &weight->next;
    }
    size_t index_count=0;
    if(!reading) for(bot_goal_indexes *row=goals->indexes;row;row=row->next) ++index_count;
    if(ok) ok=qa_source_save_count(io,&index_count,UINT32_MAX);
    if(ok && reading && index_count>(io->input.size-io->offset)/12)
        ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated retained goal index references");
    bot_goal_indexes **index_tail=&goals->indexes;
    for(size_t i=0;ok && i<index_count;++i) {
        bot_goal_indexes *row;
        if(reading) {
            row=calloc(1,sizeof(*row));
            if(!row) {ok=bot_save_fail(io,QA_ERROR_MEMORY,"Restoring goal index reference");break;}
            *index_tail=row;
        } else row=*index_tail;
        size_t reference=0;
        ok=qa_source_save_u32(io,&row->pointer);
        if(ok && !reading) ok=qa_bot_memory_reference(goals->memory,row->allocation,&reference,io->error);
        if(ok) ok=qa_source_save_count(io,&reference,SIZE_MAX);
        if(ok && reading) ok=qa_bot_memory_resolve(goals->memory,reference,&row->allocation,io->error);
        if(!ok && !io->failed) io->failed=true;
        goals->last_indexes=row;index_tail=&row->next;
    }
    for (size_t i = 0; ok && i < state_count; ++i) {
        size_t key = 0;
        if (!reading && goals->states[i].weights) {
            bot_goal_weights *weight = goals->weights; key = 1;
            while (weight != goals->states[i].weights) { weight = weight->next; ++key; }
        }
        ok = state_fields(io, &goals->states[i], &key) && key <= weight_count;
        if (ok && reading && key) {
            bot_goal_weights *weight = goals->weights;
            for (size_t j = 1; j < key; ++j) weight = weight->next;
            goals->states[i].weights = weight;
        }
        uint32_t pointer=!reading?goals->states[i].index_pointer:0;
        if(ok) ok=qa_source_save_u32(io,&pointer);
        if(ok && reading && pointer) {
            bot_goal_indexes *row=goals->indexes;
            while(row && row->pointer!=pointer) row=row->next;
            if(!row) ok=bot_save_fail(io,QA_ERROR_FORMAT,"Saved goal index pointer is missing");
            else goals->states[i].index_pointer=row->pointer;
        }
    }
    if (ok) ok = qa_source_save_count(io, &goals->level_capacity, UINT32_MAX) &&
        qa_source_save_u32(io, &goals->level_head) && qa_source_save_u32(io, &goals->free_head) &&
        qa_source_save_i32(io, &goals->initial_count) && qa_source_save_i32(io, &goals->next_source);
    if (ok && reading) ok = array(io, (void **)&goals->level, goals->level_capacity, sizeof(*goals->level), 60);
    for (size_t i = 0; ok && i < goals->level_capacity; ++i) ok = level_fields(io, &goals->level[i]);
    if (ok) ok = qa_source_save_count(io, &goals->location_count, SIZE_MAX);
    if (ok && reading) ok = array(io, (void **)&goals->locations, goals->location_count, sizeof(*goals->locations), 160);
    for (size_t i = 0; ok && i < goals->location_count; ++i) ok = map_fields(io, &goals->locations[i]);
    if (ok) ok = qa_source_save_count(io, &goals->camp_count, SIZE_MAX);
    if (ok && reading) ok = array(io, (void **)&goals->camps, goals->camp_count, sizeof(*goals->camps), 160);
    for (size_t i = 0; ok && i < goals->camp_count; ++i) ok = map_fields(io, &goals->camps[i]);
    if (ok) ok = qa_source_save_count(io, &goals->source_capacity, SIZE_MAX / sizeof(*goals->source)) &&
        qa_source_save_count(io, &goals->source_count, goals->source_capacity);
    if (ok && reading) {
        if (goals->source_count > (io->input.size - io->offset) / 86 ||
            (goals->source_capacity && (goals->source_capacity < 64 ||
             (goals->source_capacity & (goals->source_capacity - 1)) || !goals->source_count ||
             (goals->source_capacity > 64 && goals->source_count <= goals->source_capacity / 2))))
            ok = bot_save_fail(io, QA_ERROR_FORMAT, "Truncated retained source goals");
        if (ok && goals->source_capacity && !(goals->source = calloc(goals->source_capacity, sizeof(*goals->source))))
            ok = bot_save_fail(io, QA_ERROR_MEMORY, "Restoring retained source goal capacity");
    }
    for (size_t i = 0; ok && i < goals->source_count; ++i) {
        bot_source_goal *source = &goals->source[i]; const char *name = source->name;
        ok = qa_source_save_actor(io, &source->actor) && bot_save_text(io, &name);
        if (reading) source->name = (char *)name;
        if (ok) ok = name && qa_source_save_count(io, &source->name_capacity, SIZE_MAX) &&
            source->name_capacity > strlen(name) && goal_fields(io, &source->goal);
        if (ok && reading) {
            char *allocated = realloc(source->name, source->name_capacity);
            if (!allocated) ok = bot_save_fail(io, QA_ERROR_MEMORY, "Restoring retained source goal name capacity");
            else source->name = allocated;
            uint32_t bucket = bot_goal_source_bucket(source->actor);
            source->hash_next = goals->source_buckets[bucket]; goals->source_buckets[bucket] = (uint32_t)i + 1;
        }
    }
    if (!ok && !io->failed) return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid bot goal continuation fields");
    return ok;
}
bool qa_bot_goals_save_capture(qa_session *session, const qa_bot_goals *goals, const qa_bot_saved_assets *assets,
                              qa_buffer *out, qa_error *error)
{
    if (!session || !goals || goals->busy || !goals->shared_memory || !assets || !out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Goal capture requires the actual runtime allocation owner and asset registry");return false;
    }
    if(!topology(goals,error)) return false;
    qa_source_save_io io = {0}; qa_bot_goals view = *goals;
    bool ok = qa_source_save_writer(&io, session, error) && signature(&io) &&
        fields(&io, &view, assets, NULL) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_bot_goals_save_restore(qa_session *session, qa_bot_goals *goals, qa_bytes bytes,
                              const qa_bot_saved_assets *assets, const qa_entities *entities, qa_error *error)
{
    if (!session || !goals || goals->busy || !goals->shared_memory || !assets || goals->weights || goals->indexes) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot goal import requires a detached idle actual owner"); return false;
    }
    for(uint32_t i=0;i<goals->options.maximum_states;++i)
        if(goals->states[i].used) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Goal import requires its fresh state store");return false;}
    qa_bot_goals scratch = {.options = goals->options, .services = goals->services, .workspace = goals->workspace,
        .memory=goals->memory,.shared_memory=true};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, session, bytes, error) && signature(&io) &&
        fields(&io, &scratch, assets, entities) && qa_source_save_finish(&io, NULL) && topology(&scratch, error);
    if (ok) { qa_bot_goals old = *goals; *goals = scratch; clear(&old); }
    else clear(&scratch);
    if (!ok && (!error || error->code == QA_OK)) qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid complete bot goal continuation");
    qa_source_save_dispose(&io); return ok;
}
