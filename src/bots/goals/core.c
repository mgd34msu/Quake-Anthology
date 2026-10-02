#include "internal.h"
#include "../checkpoint_internal.h"
#include <stdio.h>

bool bot_goal_fail(qa_error *e, const char *message) {
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
bool bot_goal_mutable(qa_bot_goals *g, qa_error *e) {
    return g && !g->busy ? true : bot_goal_fail(e, "goal owner is absent or executing a query");
}
bool qa_bot_goals_active(const qa_bot_goals *g) { return g && g->busy; }
qa_bot_memory *qa_bot_goals_memory(const qa_bot_goals *g) {return g?g->memory:NULL;}
bool qa_bot_goals_has_handle(const qa_bot_goals *g, uint32_t id) {
    return g && id && id <= g->options.maximum_states && g->states[id - 1].used;
}
bot_goal_slot *bot_goal_slot_get(const qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!g || !id || id > g->options.maximum_states || !g->states[id - 1].used) {
        bot_goal_fail(e, "invalid bot goal state handle");
        return NULL;
    }
    return &g->states[id - 1];
}
static bool release_weights(qa_bot_goals *g, bot_goal_slot *s,qa_error *error) {
    bot_goal_weights *w;
    if(!bot_goal_config_get(g,s,&w,error)) return false;
    if(w && !qa_bot_weights_free(w->weights,error)) return false;
    qa_bot_memory_allocation indexes;bool present;
    if(!bot_goal_indexes_get(g,s,&indexes,&present,error) ||
       (present && !qa_bot_memory_free(g->memory,indexes,error)) ||
       !bot_goal_record_word_write(&s->record,BOT_GOAL_CONFIG_POINTER,0,error) ||
       !bot_goal_indexes_drop(g,s,error)) return false;
    if(!w) return true;
    for(uint32_t i=0;i<g->options.maximum_states;++i) if(g->states[i].used) {
        bot_goal_weights *other;
        if(!bot_goal_config_get(g,&g->states[i],&other,error)) return false;
        if(other==w) return true;
    }
    bot_goal_weights **link = &g->weights;
    while (*link != w) link = &(*link)->next;
    *link = w->next;
    qa_bot_weights_release(w->weights);
    free(w);
    return true;
}
bool bot_goal_config_get(const qa_bot_goals *g,const bot_goal_slot *s,bot_goal_weights **out,qa_error *e) {
    if(!out) return bot_goal_fail(e,"Goal configuration read requires its output");
    uint32_t pointer;
    if(!bot_goal_record_word_read(&s->record,BOT_GOAL_CONFIG_POINTER,&pointer,e)) return false;
    if(!pointer) {*out=NULL;return true;}
    bot_goal_weights *row=g->weights;
    while(row && row->pointer!=pointer) row=row->next;
    if(!row) return bot_goal_fail(e,"Invalid goal weight configuration pointer");
    *out=row;return true;
}
bool bot_goal_config_set(qa_bot_goals *g, bot_goal_slot *s, qa_bot_weights *weights, qa_error *e) {
    bot_goal_weights *w = g->weights;
    if (weights) {
        while (w && w->weights != weights) w = w->next;
        if (!w) {
            if(!g->next_pointer || g->next_pointer>UINT32_MAX)
                return bot_goal_fail(e,"Goal weight pointer identities exhausted");
            w = calloc(1, sizeof(*w));
            if (!w) {
                qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining goal weight configuration reference");
                return false;
            }
            w->pointer=(uint32_t)g->next_pointer++;
            w->weights = weights;
            qa_bot_weights_retain(weights);
            bot_goal_weights **tail=&g->weights;
            while(*tail) tail=&(*tail)->next;
            *tail=w;
        }
    }
    return bot_goal_record_word_write(&s->record,BOT_GOAL_CONFIG_POINTER,weights?w->pointer:0,e);
}
static bool bind_weights(qa_bot_goals *g,bot_goal_slot *s,qa_bot_weights *weights,qa_error *e) {
    if(!bot_goal_config_set(g,s,weights,e)) return false;
    if(!weights) return true;
    if(!g->configured) return true;
    const qa_bot_items_view *items=qa_bot_items_read(g->items);
    if(items->count>UINT32_MAX) return bot_goal_fail(e,"Item index count exceeds the source domain");
    qa_bot_memory_allocation indexes;
    if(!bot_goal_indexes_create(g,(uint32_t)items->count,&indexes,e)) return false;
    for(uint32_t i=0;i<(uint32_t)items->count;++i) {
        int32_t index;
        if(!qa_bot_weights_find_value(weights,items->items[i].classname,&index,e) ||
           !bot_goal_indexes_write(g,indexes,i,index,e)) return false;
    }
    return bot_goal_indexes_publish(g,s,indexes,e);
}
bool qa_bot_goals_create(qa_bot_items *items, const qa_bot_goal_options *options,
                         const qa_bot_goal_services *services, qa_bot_goals **out, qa_error *e) {
    if (!items || !options || !services || !out || !options->maximum_states ||
        !options->maximum_level_items || options->maximum_level_items >= QA_BOT_SOURCE_GOAL_MIN - 1000000 ||
        !isfinite(options->dropped_weight) || !options->random.next || !services->navigation ||
        (!!services->entities != !!services->entities_end) ||
        (!!services->pickups != !!services->pickups_end) ||
        (services->pickups && !services->pickup) ||
        options->maximum_states > SIZE_MAX / sizeof(bot_goal_slot))
        return bot_goal_fail(e, "invalid bot goal services or capacities");
    qa_bot_goals *g = calloc(1, sizeof(*g));
    if (!g || !(g->states = calloc(options->maximum_states, sizeof(*g->states)))) {
        free(g);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating bot goal states");
        return false;
    }
    g->options = *options;
    g->services = *services;
    g->items = items;
    g->configured = true;
    g->next_source = INT32_MAX;
    g->next_pointer=1;
    g->next_info=1;
    qa_bot_items_retain(items);
    if (!qa_bot_memory_create(NULL,&g->memory,e) || !qa_bot_weight_workspace_create(&g->workspace, e)) {
        qa_bot_goals_destroy(g);
        return false;
    }
    *out = g;
    return true;
}
void qa_bot_goals_destroy(qa_bot_goals *g) {
    if (!g || g->busy) return;
    g->busy=true;
    while(g->weights) {bot_goal_weights *w=g->weights;g->weights=w->next;qa_bot_weights_release(w->weights);free(w);}
    bot_goal_indexes_clear(g);
    bot_goal_map_clear(g);
    qa_bot_weight_workspace_destroy(g->workspace);
    qa_bot_items_release(g->items);
    free(g->states);
    (void)qa_bot_memory_release(g->memory,NULL);
    free(g);
}
bool qa_bot_goals_shutdown(qa_bot_goals *g, qa_error *error) {
    if (!g) return true;
    if (!bot_goal_mutable(g, error)) return false;
    g->configured = false;
    if (!bot_goal_info_free(g, error)) return false;
    bot_goal_map_clear(g);
    for (uint32_t i = 0; i < g->options.maximum_states; ++i)
        if (g->states[i].used && !qa_bot_goals_free(g, i + 1, error)) return false;
    return true;
}
bool qa_bot_goals_reconfigure(qa_bot_goals *g, qa_bot_items *items, int32_t game_type,
                               uint32_t next_map_capacity, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (!items) { g->configured = false; return true; }
    if (!next_map_capacity || next_map_capacity >= QA_BOT_SOURCE_GOAL_MIN - 1000000)
        return bot_goal_fail(e, "invalid goal reconfiguration");
    const qa_bot_items_view *view = qa_bot_items_read(items);
    for (uint32_t i = g->level_head; i; i = g->level[i].next)
        if (g->level[i].info >= view->count)
            return bot_goal_fail(e, "replacement item config omits retained level item");
    qa_bot_items_retain(items);
    qa_bot_items_release(g->items);
    g->items = items;
    g->configured = true;
    g->options.game_type = game_type;
    g->options.maximum_level_items = next_map_capacity;
    return true;
}
bool qa_bot_goals_time(qa_bot_goals *g, float time, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (!isfinite(time)) return bot_goal_fail(e, "bot goal time must be finite");
    g->time = time;
    return true;
}
bool qa_bot_goals_allocate(qa_bot_goals *g, int32_t client, uint32_t *out, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (!out) return bot_goal_fail(e, "missing goal handle output");
    *out = 0;
    for (uint32_t i = 0; i < g->options.maximum_states; ++i)
        if (!g->states[i].used) {
            bot_goal_record record;
            g->busy=true;
            bool ok=bot_goal_record_allocate(g->memory,client,&record,e);
            g->busy=false;
            if(!ok) return false;
            g->states[i] = (bot_goal_slot){.used = true, .record=record};
            *out = i + 1;
            break;
        }
    return true;
}
bool qa_bot_goals_free(qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    g->busy=true;
    bool ok=release_weights(g,s,e) && qa_bot_memory_free(g->memory,s->record.allocation,e);
    if(ok) *s = (bot_goal_slot){0};
    g->busy=false;
    return ok;
}
bool qa_bot_goals_reset(qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    return bot_goal_record_reset(&s->record,true,e);
}
bool qa_bot_goals_weights(qa_bot_goals *g, uint32_t id, qa_bot_weights *w, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if(!s) return false;
    g->busy=true;
    bool ok=w?bind_weights(g,s,w,e):release_weights(g,s,e);
    g->busy=false;return ok;
}
static bool breed_report(void *context, const char *message,qa_error *error) {
    char line[128];
    (void)snprintf(line, sizeof(line), "%s\n", message);
    return bot_goal_report(context, QA_SCRIPT_ERROR, line,error);
}
bool qa_bot_goals_interbreed(qa_bot_goals *g, uint32_t first, uint32_t second, uint32_t child,
                            bool *matched, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *a = bot_goal_slot_get(g, first, e), *b = bot_goal_slot_get(g, second, e),
                  *c = bot_goal_slot_get(g, child, e);
    if (!a || !b || !c) return false;
    if (!matched) return bot_goal_fail(e, "goal breeding requires matched output");
    *matched = false;
    g->busy = true;
    bot_goal_weights *wa,*wb,*wc;
    bool ok=bot_goal_config_get(g,a,&wa,e) && bot_goal_config_get(g,b,&wb,e) && bot_goal_config_get(g,c,&wc,e);
    if(ok && (!wa || !wb || !wc))
        ok=bot_goal_report(g, QA_SCRIPT_FATAL, "goal fuzzy interbreeding requires loaded item weights",e);
    else if(ok) ok=qa_bot_weights_interbreed_report(wc->weights,wa->weights,wb->weights,g,breed_report,matched,e);
    g->busy = false;
    return ok;
}
bool qa_bot_goals_mutate(qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    g->busy = true;
    bot_goal_weights *weights;
    bool ok=bot_goal_config_get(g,s,&weights,e);
    if(ok && !weights) ok=bot_goal_report(g, QA_SCRIPT_FATAL, "goal fuzzy mutation requires loaded item weights",e);
    else if(ok) ok=qa_bot_weights_evolve(weights->weights,&g->options.random,e);
    g->busy = false;
    return ok;
}
bool qa_bot_goals_save_weights(qa_bot_goals *g, uint32_t id, qa_error *e) {
    return bot_goal_slot_get(g, id, e) != NULL;
}
static bool push(qa_bot_goals *g, bot_goal_slot *s, const qa_bot_goal *goal,
                 void *context, bool (*read)(void *, qa_bot_goal *, qa_error *),
                 bool *out, qa_error *e) {
    int32_t top;
    if(!bot_goal_record_integer_read(&s->record,BOT_GOAL_STACK_TOP,&top,e)) return false;
    *out=top<QA_BOT_GOAL_STACK-1;
    if (*out) {
        if(!bot_goal_record_word_write(&s->record,BOT_GOAL_STACK_TOP,(uint32_t)(top+1),e)) return false;
        qa_bot_goal value;
        if (read) {
            if (!read(context, &value, e)) return false;
            goal = &value;
        }
        return bot_goal_record_integer_read(&s->record,BOT_GOAL_STACK_TOP,&top,e) &&
            bot_goal_record_goal_write(&s->record,top,goal,e);
    } else {
        return bot_goal_report(g, QA_SCRIPT_ERROR, "goal heap overflow\n",e) && bot_goal_dump_stack(g, s, e);
    }
    return true;
}
bool bot_goal_push(qa_bot_goals *g, bot_goal_slot *s, const qa_bot_goal *goal, bool *out, qa_error *e) {
    return push(g, s, goal, NULL, NULL, out, e);
}
bool qa_bot_goals_push(qa_bot_goals *g, uint32_t id, const qa_bot_goal *goal, bool *out, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    if (!goal || !out) return bot_goal_fail(e, "missing goal or push output");
    g->busy = true;
    bool ok = push(g, s, goal, NULL, NULL, out, e);
    g->busy = false;
    return ok;
}
bool qa_bot_goals_push_from(qa_bot_goals *g, uint32_t id, void *context,
                              bool (*read)(void *, qa_bot_goal *, qa_error *), bool *out, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    if (!read || !out) return bot_goal_fail(e, "missing source goal reader/output");
    g->busy = true;
    bool ok = push(g, s, NULL, context, read, out, e);
    g->busy = false;
    return ok;
}
bool qa_bot_goals_push_source_from(qa_bot_goals *g,uint32_t id,void *context,
    bool (*read)(void *,qa_bytes *,qa_error *),bool *out,qa_error *e) {
    if(!bot_goal_mutable(g,e)) return false;
    bot_goal_slot *s=bot_goal_slot_get(g,id,e);
    if(!s || !read || !out) return s?bot_goal_fail(e,"missing source goal bytes reader/output"):false;
    int32_t top;
    if(!bot_goal_record_integer_read(&s->record,BOT_GOAL_STACK_TOP,&top,e)) return false;
    *out=top<QA_BOT_GOAL_STACK-1;g->busy=true;
    bool ok;
    if(*out) {
        qa_bytes bytes={0};
        ok=bot_goal_record_word_write(&s->record,BOT_GOAL_STACK_TOP,(uint32_t)(top+1),e) &&
            read(context,&bytes,e) && bot_goal_record_integer_read(&s->record,BOT_GOAL_STACK_TOP,&top,e) &&
            bot_goal_record_goal_bytes_write(&s->record,top,bytes,e);
    } else {
        ok=bot_goal_report(g,QA_SCRIPT_ERROR,"goal heap overflow\n",e) && bot_goal_dump_stack(g,s,e);
    }
    g->busy=false;return ok;
}
bool qa_bot_goals_pop(qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    int32_t top;
    if(!bot_goal_record_integer_read(&s->record,BOT_GOAL_STACK_TOP,&top,e)) return false;
    return top<=0 || bot_goal_record_word_write(&s->record,BOT_GOAL_STACK_TOP,(uint32_t)(top-1),e);
}
bool qa_bot_goals_empty(qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    return bot_goal_record_word_write(&s->record,BOT_GOAL_STACK_TOP,0,e);
}
bool qa_bot_goals_top(const qa_bot_goals *g, uint32_t id, bool second, qa_bot_goal *out,
                      bool *found, qa_error *e) {
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    if (!out || !found) return bot_goal_fail(e, "missing goal stack output");
    int32_t top;
    if(!bot_goal_record_integer_read(&s->record,BOT_GOAL_STACK_TOP,&top,e)) return false;
    *found=second?top>1:top!=0;
    return !*found || bot_goal_record_goal_read(&s->record,top-(int32_t)second,out,e);
}
bool qa_bot_goals_top_source(const qa_bot_goals *g,uint32_t id,bool second,qa_bytes *out,bool *found,qa_error *e) {
    bot_goal_slot *s=bot_goal_slot_get(g,id,e);
    if(!s) return false;
    if(!out || !found) return bot_goal_fail(e,"missing source goal stack bytes output");
    int32_t top;
    if(!bot_goal_record_integer_read(&s->record,BOT_GOAL_STACK_TOP,&top,e)) return false;
    *found=second?top>1:top!=0;
    if(!*found) {*out=(qa_bytes){0};return true;}
    return bot_goal_record_goal_bytes_read(&s->record,top-(int32_t)second,out,e);
}
bool qa_bot_goals_avoid_clear(qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    return bot_goal_record_reset(&s->record,false,e);
}
float bot_goal_default_avoid(const qa_bot_item_info *item) {
    return fmaxf(10, item->respawn_seconds == 0 ? 30 : item->respawn_seconds);
}
bool bot_goal_avoid(qa_bot_goals *g, bot_goal_slot *s, int32_t number, float duration,qa_error *e) {
    int32_t slot=-1;qa_bot_avoid_goal goal;
    for(int32_t i=0;i<QA_BOT_AVOID_GOALS;++i) {
        if(!bot_goal_record_avoid_read(&s->record,i,&goal,e)) return false;
        if(goal.number==number) {slot=i;break;}
    }
    if(slot<0) for(int32_t i=0;i<QA_BOT_AVOID_GOALS;++i) {
        if(!bot_goal_record_avoid_read(&s->record,i,&goal,e)) return false;
        if(goal.expires<g->time) {slot=i;break;}
    }
    goal=(qa_bot_avoid_goal){number,g->time+duration};
    return slot<0 || bot_goal_record_avoid_write(&s->record,slot,&goal,e);
}
bool qa_bot_goals_avoid_set(qa_bot_goals *g, uint32_t id, int32_t number, float duration, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    if (duration < 0) {
        if (!g->configured) return true;
        bot_level_item *item = bot_goal_find(g, number);
        if (!item) return true;
        duration = bot_goal_default_avoid(&qa_bot_items_read(g->items)->items[item->info]);
    }
    return bot_goal_avoid(g,s,number,duration,e);
}
bool qa_bot_goals_avoid_remove(qa_bot_goals *g, uint32_t id, int32_t number, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    for(int32_t i=0;i<QA_BOT_AVOID_GOALS;++i) {
        qa_bot_avoid_goal goal;
        if(!bot_goal_record_avoid_read(&s->record,i,&goal,e)) return false;
        if(goal.number==number && goal.expires>=g->time) {
            goal.expires=0;return bot_goal_record_avoid_write(&s->record,i,&goal,e);
        }
    }
    return true;
}
bool qa_bot_goals_avoid_time(const qa_bot_goals *g, uint32_t id, int32_t number, float *out, qa_error *e) {
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    if (!out) return bot_goal_fail(e, "missing avoidance time output");
    *out = 0;
    for(int32_t i=0;i<QA_BOT_AVOID_GOALS;++i) {
        qa_bot_avoid_goal goal;
        if(!bot_goal_record_avoid_read(&s->record,i,&goal,e)) return false;
        if(goal.number==number && goal.expires>=g->time) {*out=goal.expires-g->time;break;}
    }
    return true;
}
bool qa_bot_goals_capture(const qa_bot_goals *g, uint32_t id, qa_bot_goal_state *out,
                          qa_bot_weights **weights, qa_error *e) {
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    if (!out || !weights) return bot_goal_fail(e, "missing goal checkpoint output");
    bot_goal_weights *config;
    if(!bot_goal_record_state_read(&s->record,out,e) || !bot_goal_config_get(g,s,&config,e)) return false;
    *weights=config?config->weights:NULL;return true;
}
bool qa_bot_goals_restore(qa_bot_goals *g, uint32_t id, const qa_bot_goal_state *state,
                          qa_bot_weights *weights, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (!id || id > g->options.maximum_states || !state || state->stack_top > QA_BOT_GOAL_STACK)
        return bot_goal_fail(e, "invalid goal state checkpoint");
    bot_goal_slot *s = &g->states[id - 1];
    g->busy=true;
    if(!s->used) {
        bot_goal_record record;
        if(!bot_goal_record_allocate(g->memory,state->client,&record,e)) {g->busy=false;return false;}
        *s=(bot_goal_slot){.used=true,.record=record};
    }
    bool ok=bind_weights(g,s,weights,e) && bot_goal_record_state_write(&s->record,state,e);
    g->busy=false;return ok;
}

void bot_goal_restore_lock(qa_bot_goals *g, bool locked) { g->busy = locked; }
