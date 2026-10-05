#include "internal.h"
#include "../save_fields.h"
#include "qa/bot_goals_save.h"
#include "qa/binary.h"

static const uint8_t magic[8] = {'Q', 'A', 'B', 'G', 'O', 'A', 'L', 0};
static bool signature(qa_source_save_io *io) {
    uint8_t bytes[8];memcpy(bytes,magic,sizeof(bytes));
    return qa_source_save_bytes(io,bytes,sizeof(bytes)) && !memcmp(bytes,magic,sizeof(bytes)) ? true :
        bot_save_fail(io,QA_ERROR_FORMAT,"Invalid goal allocation continuation signature");
}

static bool goal_fields(qa_source_save_io *io, qa_bot_goal *goal)
{
    return qa_source_save_vec3(io, &goal->origin) && qa_source_save_i32(io, &goal->area) &&
        qa_source_save_vec3(io, &goal->mins) && qa_source_save_vec3(io, &goal->maxs) &&
        qa_source_save_i32(io, &goal->entity) && qa_source_save_i32(io, &goal->number) &&
        qa_source_save_i32(io, &goal->flags) && qa_source_save_i32(io, &goal->item_info);
}
static bool state_fields(qa_source_save_io *io,qa_bot_memory *memory,bot_goal_slot *slot) {
    if(!qa_source_save_bool(io,&slot->used) || !slot->used) return !io->failed;
    bool reading=io->direction==QA_SOURCE_SAVE_READ;qa_bot_goal_state state={0};uint32_t config=0,index=0;
    if(!reading && (!bot_goal_record_state_read(&slot->record,&state,io->error) ||
       !bot_goal_record_word_read(&slot->record,BOT_GOAL_CONFIG_POINTER,&config,io->error) ||
       !bot_goal_record_word_read(&slot->record,BOT_GOAL_INDEX_POINTER,&index,io->error))) return false;
    bool ok=qa_source_save_u32(io,&config) && qa_source_save_u32(io,&index) &&
        qa_source_save_i32(io,&state.client) && qa_source_save_i32(io,&state.last_reachability_area) &&
        qa_source_save_u32(io,&state.stack_top) && state.stack_top<=QA_BOT_GOAL_STACK;
    for(size_t i=0;ok && i<QA_BOT_GOAL_STACK;++i) ok=goal_fields(io,&state.stack[i]);
    for(size_t i=0;ok && i<QA_BOT_AVOID_GOALS;++i)
        ok=qa_source_save_i32(io,&state.avoid[i].number) && qa_source_save_f32(io,&state.avoid[i].expires);
    if(ok && reading) ok=bot_goal_record_allocate(memory,state.client,&slot->record,io->error) &&
        bot_goal_record_state_write(&slot->record,&state,io->error) &&
        bot_goal_record_word_write(&slot->record,BOT_GOAL_CONFIG_POINTER,config,io->error) &&
        bot_goal_record_word_write(&slot->record,BOT_GOAL_INDEX_POINTER,index,io->error);
    return ok;
}
static bool indexes_fields(qa_source_save_io *io,qa_bot_goals *goals,qa_bot_memory_allocation *allocation) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;size_t count=0;qa_bot_memory_span bytes={0};
    if(!reading) {
        if(!qa_bot_memory_bytes(goals->memory,*allocation,&bytes,io->error) || bytes.size%4) return false;
        count=bytes.size/4;
    }
    if(!qa_source_save_count(io,&count,(INT32_MAX-4)/4)) return false;
    if(reading && count>(io->input.size-io->offset)/4)
        return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated saved bot selector indexes");
    if(reading && (!bot_goal_indexes_create(goals,(uint32_t)count,allocation,io->error) ||
       !qa_bot_memory_bytes(goals->memory,*allocation,&bytes,io->error))) return false;
    for(size_t i=0;i<count;++i) {
        uint32_t word=reading?0:qa_load_u32le(bytes.data+i*4);
        if(!qa_source_save_u32(io,&word)) return false;
        if(reading) qa_store_u32le(bytes.data+i*4,word);
    }
    return true;
}
static bool level_fields(qa_source_save_io *io,qa_bot_goals *goals,uint32_t id) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;bot_level_item item={0};
    if(!reading && !bot_goal_level_read(goals,id,&item,io->error)) return false;
    bool ok=qa_source_save_i32(io,&item.number) && qa_source_save_i32(io,&item.entity) &&
        qa_source_save_u32(io,&item.info) && qa_source_save_u32(io,&item.flags) &&
        qa_source_save_u32(io,&item.previous) && qa_source_save_u32(io,&item.next) &&
        qa_source_save_f32(io,&item.weight) && qa_source_save_f32(io,&item.timeout) &&
        qa_source_save_vec3(io,&item.origin) && qa_source_save_vec3(io,&item.goal_origin) &&
        qa_source_save_u32(io,&item.goal_area);
    if(ok && reading) {
        uint32_t weight,timeout;memcpy(&weight,&item.weight,4);memcpy(&timeout,&item.timeout,4);
        ok=bot_goal_level_word(goals,id,0,(uint32_t)item.number,io->error) &&
            bot_goal_level_word(goals,id,4,item.info,io->error) && bot_goal_level_word(goals,id,8,item.flags,io->error) &&
            bot_goal_level_word(goals,id,12,weight,io->error) && bot_goal_level_vector(goals,id,16,item.origin,io->error) &&
            bot_goal_level_word(goals,id,28,item.goal_area,io->error) && bot_goal_level_vector(goals,id,32,item.goal_origin,io->error) &&
            bot_goal_level_word(goals,id,44,(uint32_t)item.entity,io->error) && bot_goal_level_word(goals,id,48,timeout,io->error) &&
            bot_goal_level_word(goals,id,52,item.previous,io->error) && bot_goal_level_word(goals,id,56,item.next,io->error);
    }
    return ok;
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
    if (!goals->items || !goals->states || (capacity && (!goals->level_allocation.owner || capacity > UINT32_MAX)) ||
        (!capacity && (goals->level_allocation.owner || goals->level_head || goals->free_head || goals->initial_count)) ||
        goals->initial_count < 0 || (capacity && (size_t)goals->initial_count > capacity) ||
        goals->source_count > goals->source_capacity || goals->source_count > UINT32_MAX ||
        goals->source_capacity > SIZE_MAX / sizeof(*goals->source) ||
        (goals->source_capacity && (!goals->source || goals->source_capacity < 64 ||
                                    (goals->source_capacity & (goals->source_capacity - 1)) ||
                                    !goals->source_count || (goals->source_capacity > 64 &&
                                     goals->source_count <= goals->source_capacity / 2))) ||
        goals->next_source < QA_BOT_SOURCE_GOAL_MIN - 1) goto invalid;
    if (!bot_goal_info_topology(goals, error) || !bot_goal_level_topology(goals, error)) return false;
    bool ok = true;
    if(!goals->memory || goals->prepared_indexes || qa_bot_memory_disposed(goals->memory) ||
       !goals->next_pointer || goals->next_pointer>UINT64_C(0x100000000)) goto invalid;
    size_t wrapper_count = 0;
    for (const bot_goal_weights *weight = goals->weights; ok && weight; weight = weight->next) {
        if (!weight->weights || !weight->pointer || weight->pointer>=goals->next_pointer ||
            wrapper_count++ >= UINT32_MAX) { ok = false; break; }
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
        if(!slot->used) {
            if(slot->record.memory || slot->record.allocation.owner || slot->record.allocation.generation ||
               slot->record.allocation.slot) ok=false;
            continue;
        }
        int32_t top;bot_goal_weights *weight;qa_bot_memory_allocation allocation;bool present;
        if(slot->record.memory!=goals->memory ||
           !bot_goal_record_integer_read(&slot->record,BOT_GOAL_STACK_TOP,&top,error) ||
           top<0 || top>QA_BOT_GOAL_STACK ||
           !bot_goal_config_get(goals,slot,&weight,error) ||
           !bot_goal_indexes_get(goals,slot,&allocation,&present,error)) {ok=false;break;}
        for(uint32_t j=0;j<i;++j) if(goals->states[j].used &&
            goals->states[j].record.allocation.owner==slot->record.allocation.owner &&
            goals->states[j].record.allocation.generation==slot->record.allocation.generation &&
            goals->states[j].record.allocation.slot==slot->record.allocation.slot) ok=false;
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
                    const qa_entities *entities,qa_bot_navigation *navigation)
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
    if (reading) {
        goals->items=items;
        if(ok && has_entities) {
            goals->entities=entities;goals->busy=true;
            ok=navigation && bot_goal_map_info_load(goals,navigation,io->error);
            goals->busy=false;
        }
    }
    if (ok) ok = qa_source_save_u64(io,&goals->next_pointer) &&
        qa_source_save_count(io, &weight_count, UINT32_MAX);
    if(ok && reading && weight_count>(io->input.size-io->offset)/12)
        ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated retained goal configuration references");
    if (ok && reading) ok = array(io, (void **)&goals->states, state_count, sizeof(*goals->states), 1);
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
        if (ok) ok = qa_source_save_u32(io,&weight->pointer);
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
        ok=qa_source_save_u32(io,&row->pointer) && indexes_fields(io,goals,&row->allocation);
        goals->last_indexes=row;index_tail=&row->next;
    }
    for (size_t i = 0; ok && i < state_count; ++i)
        ok = state_fields(io,goals->memory,&goals->states[i]);
    if (ok) ok = qa_source_save_count(io, &goals->level_capacity, UINT32_MAX) &&
        qa_source_save_u32(io, &goals->level_head) && qa_source_save_u32(io, &goals->free_head) &&
        qa_source_save_i32(io, &goals->initial_count) && qa_source_save_i32(io, &goals->next_source);
    bool has_level = goals->level_allocation.owner != 0;
    if (ok) ok = qa_source_save_bool(io, &has_level);
    if(ok && reading && has_level && goals->level_capacity>(io->input.size-io->offset)/60)
        ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated saved mutable level items");
    if (ok && has_level) {
        if(reading && !goals->level_allocation.owner)
            ok=goals->level_capacity<=(INT32_MAX-4u)/60u &&
                qa_bot_memory_allocate(goals->memory,(uint32_t)goals->level_capacity*60u,QA_BOT_MEMORY_HEAP,true,NULL,&goals->level_allocation,io->error);
        qa_bot_memory_span bytes;
        if(ok) ok=qa_bot_memory_bytes(goals->memory,goals->level_allocation,&bytes,io->error) && bytes.size/60==goals->level_capacity && !(bytes.size%60);
        for(uint32_t id=1;ok && id<=goals->level_capacity;++id) ok=level_fields(io,goals,id);
    }
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
    if (!session || !goals || goals->busy || !assets || !out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Goal capture requires the actual runtime allocation owner and asset registry");return false;
    }
    if(!topology(goals,error)) return false;
    qa_source_save_io io = {0}; qa_bot_goals view = *goals;
    bool ok = qa_source_save_writer(&io, session, error) && signature(&io) &&
        fields(&io, &view, assets, NULL,NULL) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_bot_goals_save_restore(qa_session *session, qa_bot_goals *goals, qa_bytes bytes,
                              const qa_bot_saved_assets *assets, const qa_entities *entities,qa_bot_navigation *navigation, qa_error *error)
{
    if (!session || !goals || goals->busy || !assets || goals->weights || goals->indexes) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot goal import requires a detached idle actual owner"); return false;
    }
    for(uint32_t i=0;i<goals->options.maximum_states;++i)
        if(goals->states[i].used) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Goal import requires its fresh state store");return false;}
    qa_bot_goals scratch = {.options = goals->options, .services = goals->services, .workspace = goals->workspace,
        .memory=goals->memory,.shared_memory=goals->shared_memory,.next_info=1};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, session, bytes, error) && signature(&io) &&
        fields(&io, &scratch, assets, entities,navigation) && qa_source_save_finish(&io, NULL) && topology(&scratch, error);
    if (ok) { qa_bot_goals old = *goals; *goals = scratch; clear(&old); }
    else clear(&scratch);
    if (!ok && (!error || error->code == QA_OK)) qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid complete bot goal continuation");
    qa_source_save_dispose(&io); return ok;
}
