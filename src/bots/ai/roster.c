#include "internal.h"

bool bot_ai_fail(qa_error *e, const char *message) {
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
bool bot_ai_live(const qa_bots *b, qa_actor_id actor) {
    return qa_actors_get(qa_session_actors(b->services.shared.session), actor) != NULL;
}
bot_ai_state *bot_ai_actor(const qa_bots *b, qa_actor_id actor) {
    if (!b || actor.slot >= b->actor_capacity) return NULL;
    uint32_t index = b->actor_clients[actor.slot];
    bot_ai_state *state = index ? b->clients[index - 1] : NULL;
    return state && qa_actor_id_equal(state->view.actor, actor) ? state : NULL;
}
bool bot_ai_mutable(qa_bots *b, qa_error *e) {
    return b && !b->busy && !b->restore_pending && !qa_bot_runtime_closed(b->runtime) ? true :
        bot_ai_fail(e, "bot population is absent, closed, restoring or executing a callback");
}
static bool create(qa_bot_runtime *runtime, const qa_bot_services *services,
                    uint32_t client_capacity, qa_bots **out, qa_error *e) {
    if (!runtime || !services || !out || !services->shared.session ||
        !services->shared.world || !services->shared.combat || !services->shared.player_info || !services->player ||
        !services->entity || !services->arsenal || !services->arsenal_end || !services->submit)
        return bot_ai_fail(e, "native bot population requires live shared gameplay and botlib services");
    if (client_capacity > INT32_MAX || (uint64_t)client_capacity > SIZE_MAX / sizeof(bot_ai_state *))
        return bot_ai_fail(e, "native bot client capacity exceeds its source memory extent");
    qa_bots *b = calloc(1, sizeof(*b));
    if (!b) { qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating native bot population"); return false; }
    b->runtime = runtime;
    b->services = *services;
    b->client_capacity = client_capacity;
    b->actor_capacity = qa_actors_capacity(qa_session_actors(services->shared.session));
    b->clients = calloc(b->client_capacity, sizeof(*b->clients));
    b->actor_clients = calloc(b->actor_capacity, sizeof(*b->actor_clients));
    if ((b->client_capacity && !b->clients) || (b->actor_capacity && !b->actor_clients)) {
        free(b->clients); free(b->actor_clients); free(b);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating retained bot actor/client lookup");
        return false;
    }
    b->controls = (qa_bot_controls){.think_time_ms = 100, .rocket_jump = true};
    b->scheduled_think_ms = 100;
    b->time = qa_bot_runtime_time(runtime);
    *out = b;
    return true;
}
bool qa_bots_create(qa_bot_runtime *runtime, const qa_bot_services *services,
                      qa_bots **out, qa_error *e) {
    if (!runtime || !qa_bot_runtime_initialized(runtime))
        return bot_ai_fail(e, "native bot population requires live shared gameplay and botlib services");
    return create(runtime, services, qa_bot_actions_capacity(qa_bot_runtime_actions(runtime)), out, e);
}
bool qa_bots_create_restored(qa_bot_runtime *runtime, const qa_bot_services *services,
                            uint32_t client_capacity, qa_bots **out, qa_error *e) {
    if (!runtime || !out || *out || !qa_bot_runtime_can_destroy(runtime))
        return bot_ai_fail(e, "restored native bot population requires idle detached owners and an empty output");
    if (!create(runtime, services, client_capacity, out, e))
        return false;
    (*out)->restore_pending = true;
    return true;
}
bool bot_ai_cleanup(qa_bots *b, bot_ai_state *s, qa_error *e) {
    if (qa_bot_runtime_closed(b->runtime)) return true;
    if (s->movement && !qa_bot_moves_free(qa_bot_runtime_moves(b->runtime), s->movement, e)) return false;
    s->movement = 0;
    if (s->goals && !qa_bot_goals_free(qa_bot_runtime_goals(b->runtime), s->goals, e)) return false;
    s->goals = 0;
    if (s->chat && !qa_bot_runtime_chat_free(b->runtime, s->chat, e)) return false;
    s->chat = 0;
    if (s->weapons && !qa_bot_runtime_weapon_free(b->runtime, s->weapons, e)) return false;
    s->weapons = 0;
    if (s->character && !qa_bot_runtime_character_free(b->runtime, s->character, e)) return false;
    s->character = 0;
    return true;
}
bool qa_bots_can_destroy(const qa_bots *b) {
    return !b || (!b->busy && !b->checking_spawn);
}
bool qa_bots_destroy(qa_bots *b, qa_error *e) {
    if (!b) return true;
    if (b->busy || b->checking_spawn) return bot_ai_fail(e, "bot population is executing a callback");
    if (!qa_bot_runtime_lease_begin(b->runtime,e)) return false;
    b->busy=true;
    for (uint32_t i = 0; i < b->client_capacity; ++i) {
        if (!b->clients[i]) continue;
        if (!bot_ai_cleanup(b, b->clients[i], e)) {
            b->clients[i]->retired=true;
            b->busy=false;
            qa_bot_runtime_lease_end(b->runtime);
            return false;
        }
        b->actor_clients[b->clients[i]->view.actor.slot]=0;
        free(b->clients[i]);
        b->clients[i]=NULL;
        --b->count;
    }
    qa_builtin_snapshot_free(&b->entities);
    qa_builtin_snapshot_free(&b->players);
    qa_bot_runtime_lease_end(b->runtime);
    free(b->clients); free(b->actor_clients); free(b);
    return true;
}
void bot_ai_schedule(qa_bots *b) {
    if (!b->count) return;
    uint32_t ordinal = 0;
    for (uint32_t i = 0; i < b->client_capacity; ++i) {
        if (!b->clients[i]) continue;
        uint32_t bits = (uint32_t)b->controls.think_time_ms * ordinal++;
        int32_t product;
        memcpy(&product, &bits, sizeof(product));
        b->clients[i]->residual_ms = (int32_t)((int64_t)product / b->count);
    }
}
static bool character_string(qa_bots *b, bot_ai_state *s, uint32_t characteristic,
                               char text[144], qa_error *e) {
    const char *value; bool written;
    if (!qa_bot_runtime_character_string(b->runtime, s->character, characteristic, &value, &written, e)) return false;
    size_t size = written ? strlen(value) : 0;
    if (size > 143) size = 143;
    if (size) memcpy(text, value, size);
    text[size] = 0;
    return true;
}
bool bot_ai_character_float(qa_bots *b, bot_ai_state *s, uint32_t index,
                              float minimum, float maximum, float *out, qa_error *e) {
    return qa_bot_runtime_character_bounded_float(b->runtime, s->character, index,
                                                   minimum, maximum, out, e);
}
static bool admit_resources(qa_bots *b, bot_ai_state *s, const qa_bot_admission *a, qa_error *e) {
    qa_bot_navigation *navigation = qa_bot_runtime_navigation(b->runtime, (int32_t)a->client);
    const qa_nav_graph_view *graph = navigation ? qa_navigation_graph(qa_bot_navigation_runtime(navigation)) : NULL;
    if (!graph || !graph->node_count || !qa_actor_id_equal(qa_bot_navigation_actor(navigation), a->actor))
        return bot_ai_fail(e, "bot navigation is not bound to the admitted canonical actor");
    if (!qa_bot_runtime_character_load(b->runtime, a->character_file, a->skill, &s->character, e)) return false;
    if (!s->character) return bot_ai_fail(e, "native bot character handles are exhausted");
    qa_bot_goals *goals = qa_bot_runtime_goals(b->runtime);
    if (!qa_bot_goals_allocate(goals, (int32_t)a->client, &s->goals, e)) return false;
    if (!s->goals) return bot_ai_fail(e, "native bot goal handles are exhausted");
    char path[144], name[144];
    if (!character_string(b, s, BOT_C_ITEM_WEIGHTS, path, e)) return false;
    int32_t result;
    if (!qa_bot_runtime_goal_weights(b->runtime, s->goals, path, &result, e)) return false;
    if (result) return bot_ai_fail(e, "native bot item weights could not load");
    if (!qa_bot_runtime_weapon_allocate(b->runtime, &s->weapons, e)) return false;
    if (!s->weapons) return bot_ai_fail(e, "native bot weapon handles are exhausted");
    if (!character_string(b, s, BOT_C_WEAPON_WEIGHTS, path, e) ||
        !qa_bot_runtime_weapon_weights(b->runtime, s->weapons, path, &result, e)) return false;
    if (result) return bot_ai_fail(e, "native bot weapon weights could not load");
    if (!qa_bot_runtime_chat_allocate(b->runtime, &s->chat, e)) return false;
    if (!s->chat) return bot_ai_fail(e, "native bot chat handles are exhausted");
    if (!character_string(b, s, BOT_C_CHAT_FILE, path, e) ||
        !character_string(b, s, BOT_C_CHAT_NAME, name, e) ||
        !qa_bot_runtime_chat_load(b->runtime, s->chat, path, name, &result, e)) return false;
    if (result) return bot_ai_fail(e, "native bot chat file could not load");
    if (!character_string(b, s, BOT_C_GENDER, name, e)) return false;
    qa_bot_chat *chat = qa_bot_runtime_chat(b->runtime, s->chat);
    qa_bot_chat_set_gender(chat, name[0] == 'f' || name[0] == 'F' ? 1 : name[0] == 'm' || name[0] == 'M' ? 2 : 0);
    qa_bot_chat_set_name(chat, a->name, (int32_t)a->client);
    if (!qa_bot_moves_allocate(qa_bot_runtime_moves(b->runtime), &s->movement, e)) return false;
    if (!s->movement) return bot_ai_fail(e, "native bot movement handles are exhausted");
    return bot_ai_character_float(b, s, BOT_C_WALKER, 0, 1, &s->walker, e);
}
bool qa_bots_admit(qa_bots *b, const qa_bot_admission *a, qa_error *e) {
    if (!bot_ai_mutable(b, e)) return false;
    if (!a || !a->character_file || !a->name || !isfinite(a->skill) ||
        a->client >= b->client_capacity || a->entity < 0 || !bot_ai_live(b, a->actor) ||
        (a->actor.slot < b->actor_capacity && b->actor_clients[a->actor.slot]) || b->clients[a->client])
        return bot_ai_fail(e, "invalid or duplicate native bot admission");
    if (a->actor.slot >= b->actor_capacity) {
        uint32_t capacity=qa_actors_capacity(qa_session_actors(b->services.shared.session));
        if ((size_t)capacity>SIZE_MAX/sizeof(*b->actor_clients)) return bot_ai_fail(e,"bot actor lookup overflow");
        uint32_t *lookup=realloc(b->actor_clients,(size_t)capacity*sizeof(*lookup));
        if (!lookup) {qa_error_set(e,QA_ERROR_MEMORY,capacity,"growing bot actor lookup");return false;}
        memset(lookup+b->actor_capacity,0,(capacity-b->actor_capacity)*sizeof(*lookup));
        b->actor_clients=lookup;b->actor_capacity=capacity;
    }
    bot_ai_state *s = calloc(1, sizeof(*s));
    if (!s) { qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating native bot continuation"); return false; }
    s->view = (qa_bot_view){.actor = a->actor, .client = a->client, .entity = a->entity,
        .mode = a->mode, .decision = QA_BOT_SEEK_LONG_TERM, .enter_time = b->time};
    s->team_arena = a->team_arena;
    s->setup_count = 4;
    size_t name_size = strlen(a->name);
    if (name_size >= sizeof(s->name)) name_size = sizeof(s->name) - 1;
    memcpy(s->name, a->name, name_size);
    if (!qa_bot_runtime_lease_begin(b->runtime,e)) {free(s);return false;}
    b->busy = true;
    bool ok = admit_resources(b, s, a, e);
    if (ok && !bot_ai_live(b, a->actor)) ok = bot_ai_fail(e, "bot actor retired during resource admission");
    if (ok) {
        b->clients[a->client] = s;
        b->actor_clients[a->actor.slot] = a->client + 1;
        ++b->count;
        bot_ai_schedule(b);
    } else {
        qa_error ignored = {0};
        bot_ai_cleanup(b, s, &ignored);
        free(s);
    }
    b->busy = false;
    qa_bot_runtime_lease_end(b->runtime);
    return ok;
}
bool qa_bots_release(qa_bots *b, qa_actor_id actor, qa_error *e) {
    if (!bot_ai_mutable(b, e)) return false;
    bot_ai_state *s = bot_ai_actor(b, actor);
    if (!s) return true;
    if (!qa_bot_runtime_lease_begin(b->runtime,e)) return false;
    b->busy = true;
    bool ok = bot_ai_cleanup(b, s, e);
    if (ok) {
        b->clients[s->view.client] = NULL;b->actor_clients[actor.slot] = 0;
        --b->count;free(s);bot_ai_schedule(b);
    } else s->retired=true;
    b->busy = false;
    qa_bot_runtime_lease_end(b->runtime);
    return ok;
}
bool qa_bots_actor_released(qa_bots *b, const qa_actor_record *released, qa_error *e) {
    if (!b || !released) return bot_ai_fail(e, "missing bot retirement record");
    bot_ai_state *s = bot_ai_actor(b, released->id);
    if (!s) return true;
    if (b->busy || b->restore_pending) { s->retired = true; return true; }
    return qa_bots_release(b, released->id, e);
}
bool qa_bots_read(const qa_bots *b, qa_actor_id actor, qa_bot_view *out, qa_error *e) {
    bot_ai_state *s = bot_ai_actor(b, actor);
    if (!s || !out || !bot_ai_live(b, actor)) return bot_ai_fail(e, "native bot actor is not live");
    *out = s->view;
    return true;
}
float bot_ai_random(qa_bots *b) {
    qa_bot_random_source source = qa_bot_runtime_random_source(b->runtime);
    return (float)(source.next(source.context) & 32767) / 32767.0f;
}
