#include "internal.h"

bool q2_actor_live(qa_q2_game *g, qa_actor_id id) {
    return qa_actors_get(qa_session_actors(g->services.session), id) != NULL;
}
qa_actor_id qa_q2_current_actor(const qa_q2_game *g) {
    return g == NULL ? (qa_actor_id){0} : g->current_actor;
}
bool qa_q2_run_actor(qa_q2_game *g, qa_actor_id id, qa_q2_actor_fn callback, void *context,
                     qa_error *e) {
    if (g == NULL || callback == NULL || g->continuation_pending || g->continuation_failed ||
        !q2_actor_live(g, id)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 source actor invocation");
        return false;
    }
    qa_actor_id previous = g->current_actor;
    g->current_actor = id;
    bool ok = callback(context, id, e);
    g->current_actor = previous;
    return ok;
}
void q2_actor_publish_prepared(qa_q2_game *g, q2_actor *a, qa_actor_id id, bool new_storage) {
    if (new_storage) {
        a->all_next = g->all_actors;
        g->all_actors = a;
    }
    a->id = id;
    a->alpha = 1;
    g->actors[id.slot] = a;
    a->source_order = ++g->actor_sequence;
    a->live_previous = g->last_actor;
    if (g->last_actor != NULL)
        g->last_actor->live_next = a;
    else
        g->first_actor = a;
    g->last_actor = a;
}
q2_actor *q2_actor_get(qa_q2_game *g, qa_actor_id id, bool create, qa_error *e) {
    if (g == NULL || id.slot >= g->capacity || !q2_actor_live(g, id)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 actor is not live in this session");
        return NULL;
    }
    q2_actor *a = g->actors[id.slot];
    if (a == NULL || !qa_actor_id_equal(a->id, id)) {
        if (!create) {
            qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Q2 actor has no provider state");
            return NULL;
        }
        if (g->actor_sequence == UINT64_MAX) {
            qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 actor source order exhausted");
            return NULL;
        }
        bool new_storage = g->spare_actors == NULL;
        if (!new_storage) {
            a = g->spare_actors;
            g->spare_actors = a->free_next;
            q2_monster_release_state(a);
            q2_items_release_state(a);
            q2_client_release_state(a);
            q2_entity_release_state(a);
            q2_actor *next = a->all_next;
            memset(a, 0, sizeof(*a));
            a->all_next = next;
        } else {
            a = calloc(1, sizeof(*a));
            if (a == NULL) {
                qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 actor extension");
                return NULL;
            }
        }
        q2_actor_publish_prepared(g, a, id, new_storage);
    }
    return a;
}
void q2_actor_order(qa_q2_game *g, q2_actor *a, uint64_t order) {
    if (a->live_previous != NULL)
        a->live_previous->live_next = a->live_next;
    else
        g->first_actor = a->live_next;
    if (a->live_next != NULL)
        a->live_next->live_previous = a->live_previous;
    else
        g->last_actor = a->live_previous;
    q2_actor *previous = g->last_actor;
    while (previous != NULL && previous->source_order > order)
        previous = previous->live_previous;
    a->source_order = order;
    a->live_previous = previous;
    a->live_next = previous == NULL ? g->first_actor : previous->live_next;
    if (a->live_next != NULL)
        a->live_next->live_previous = a;
    else
        g->last_actor = a;
    if (previous != NULL)
        previous->live_next = a;
    else
        g->first_actor = a;
    if (g->actor_sequence < order)
        g->actor_sequence = order;
}
float q2_random(qa_q2_game *g) { return qa_builtin_random_unit(&g->random); }
bool qa_q2_game_random(qa_q2_game *g,float *out,qa_error *e) {
    if(!g || !out || g->continuation_pending || g->continuation_failed || g->restoring_continuation) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Q2 source random requires its actual constructed GAME owner");
        return false;
    }
    *out=q2_random(g);return true;
}
float q2_crandom(qa_q2_game *g) { return q2_random(g) * 2.0f - 1.0f; }
bool q2_noise_for_actor(qa_q2_game *g, qa_actor_id id, qa_vec3 origin, bool secondary,
                        qa_error *e) {
    if (!q2_actor_live(g, id))
        return true;
    return q2_player_noise(g, id, origin, secondary, e) &&
           (!q2_actor_live(g, id) || g->hooks.noise == NULL ||
            g->hooks.noise(g->hooks.context, id, origin, secondary, e));
}
bool qa_q2_actor_released(qa_q2_game *g, qa_actor_record record, qa_error *e) {
    if (g)
        q2_monsters_release_actor(g, record.id);
    if (g != NULL && record.id.slot < g->capacity && g->actors[record.id.slot] != NULL &&
        qa_actor_id_equal(g->actors[record.id.slot]->id, record.id)) {
        q2_actor *a = g->actors[record.id.slot];
        q2_entity_unbind(g, a);
        g->actors[record.id.slot] = NULL;
        if (a->live_previous != NULL)
            a->live_previous->live_next = a->live_next;
        else
            g->first_actor = a->live_next;
        if (a->live_next != NULL)
            a->live_next->live_previous = a->live_previous;
        else
            g->last_actor = a->live_previous;
        bool ok = q2_grapple_released(g, a, e);
        a->free_next = g->retired_actors;
        g->retired_actors = a;
        return ok;
    }
    return true;
}
bool qa_q2_clear_trackers(qa_q2_game *g, qa_actor_id target, qa_error *e) {
    if (!g || !target.registry) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 tracker target");
        return false;
    }
    q2_actor *a = g->first_actor;
    while (a) {
        /* Retired nodes keep their successors until the next source frame. */
        q2_actor *next = a->live_next;
        if (q2_actor_live(g, a->id) && a->projectile.kind == Q2_TRACKER_DAEMON &&
            qa_actor_id_equal(a->projectile.enemy, target) &&
            !qa_session_release(g->services.session, a->id, e))
            return false;
        a = next;
    }
    return true;
}
static void released(void *context, qa_session *session, qa_actor_record record) {
    (void)session;
    qa_q2_game *g = context;
    qa_error error = {0};
    if (!qa_q2_actor_released(g, record, &error) && !g->release_failed) {
        g->release_failed = true;
        g->release_error = error;
    }
}
static bool begin_frame(void *context, qa_session *session, const qa_source_frame *frame,
                        qa_error *e) {
    (void)session;
    qa_q2_game *g = context;
    if (g->current_actor.registry) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 source frame cannot reenter an actor invocation");
        return false;
    }
    if (g->release_failed) {
        if (e != NULL)
            *e = g->release_error;
        return false;
    }
    /* Source frame entry occurs outside every prior gameplay invocation. Until
     * then retired generations remain address-stable across nested callbacks. */
    q2_monsters_reclaim(g);
    while (g->retired_actors != NULL) {
        q2_actor *a = g->retired_actors;
        g->retired_actors = a->free_next;
        a->free_next = g->spare_actors;
        g->spare_actors = a;
    }
    g->now_ns = frame->time_ns;
    g->frame_ns = frame->elapsed_ns;
    return true;
}
static bool actor_frame(void *context, qa_session *session, qa_actor_id id,
                        const qa_source_frame *frame, qa_error *e) {
    (void)session;
    return qa_q2_actor_tick(context, id, frame->time_ns, frame->elapsed_ns, e);
}
static bool end_frame(void *context, qa_session *session, const qa_source_frame *frame,
                       qa_error *e) {
    (void)session;
    (void)frame;
    return qa_q2_monsters_end_frame(context, e);
}
bool q2_actor_think(qa_q2_game *g, q2_actor *a, qa_error *e) {
    qa_actor_id id = a->id;
    if (!q2_actor_live(g, id)) return true;
    if ((a->item != NULL || a->powers != NULL) && !q2_item_tick(g, a, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (a->client != NULL && !q2_client_tick(g, a, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (a->entity != NULL && !q2_entity_tick(g, a, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (a->projectile.kind != Q2_PROJECTILE_NONE && !q2_projectile_tick(g, a, e))
        return false;
    return !q2_actor_live(g, id) || a->projectile.kind != Q2_PROJECTILE_NONE ||
           a->monster == NULL || q2_monster_tick(g, a, e);
}
static bool tick_actor(void *context, qa_actor_id id, qa_error *e) {
    qa_q2_game *g = context;
    q2_actor *a = g->actors[id.slot];
    return q2_actor_physics(g, a, e);
}
bool qa_q2_actor_tick(qa_q2_game *g, qa_actor_id id, uint64_t now, uint64_t elapsed, qa_error *e) {
    if (g == NULL || elapsed == 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 source actor turn");
        return false;
    }
    if (g->release_failed) {
        if (e != NULL)
            *e = g->release_error;
        return false;
    }
    if (id.slot >= g->capacity || g->actors[id.slot] == NULL ||
        !qa_actor_id_equal(g->actors[id.slot]->id, id))
        return true;
    g->now_ns = now;
    g->frame_ns = elapsed;
    return qa_q2_run_actor(g, id, tick_actor, g, e);
}
static void close_game(void *context) {
    qa_q2_game *g = context;
    while (g->trace_frames != NULL) {
        q2_trace_frame *next = g->trace_frames->next;
        qa_builtin_snapshot_free(&g->trace_frames->snapshot);
        free(g->trace_frames);
        g->trace_frames = next;
    }
    while (g->all_actors != NULL) {
        q2_actor *next = g->all_actors->all_next;
        q2_monster_release_state(g->all_actors);
        q2_items_release_state(g->all_actors);
        q2_client_release_state(g->all_actors);
        q2_entity_release_state(g->all_actors);
        free(g->all_actors);
        g->all_actors = next;
    }
    q2_entities_close(g);
    q2_monsters_close(g);
    q2_players_close(g);
    q2_items_close(g);
    free(g->actors);
    free(g);
}
bool qa_q2_create(const qa_builtin_services *services, const qa_q2_options *options,
                  const qa_q2_hooks *hooks, qa_q2_game **out, qa_error *e) {
    if (options == NULL || out == NULL || options->owner == 0 ||
        (unsigned)options->edition > QA_Q2_RERELEASE || (unsigned)options->product > QA_Q2_N64 ||
        (hooks != NULL && ((hooks->lag_begin == NULL) != (hooks->lag_end == NULL))) ||
        !qa_builtin_services_validate(services, e)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid native Q2 provider options");
        return false;
    }
    qa_q2_game *g = calloc(1, sizeof(*g));
    if (g == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 provider");
        return false;
    }
    g->services = *services;
    g->options = *options;
    g->widow_damage_multiplier = 1;
    g->grapple_options = (qa_q2_grapple_options){
        .fly_speed = 650, .pull_speed = 650, .damage = 10, .players_collide = true};
    if (hooks != NULL)
        g->hooks = *hooks;
    g->capacity = qa_actors_capacity(qa_session_actors(services->session));
    g->actors = calloc(g->capacity, sizeof(*g->actors));
    qa_builtin_random_seed(&g->random, (uint32_t)options->seed);
    q2_rerelease_seed(g, (uint32_t)options->seed);
    if (g->actors == NULL) {
        close_game(g);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 actor state");
        return false;
    }
    g->frame_ns = qa_q2_component(g).clock.interval_ns;
    if (!q2_definitions(g, e) || !q2_items_init(g, e) || !q2_players_init(g, e) ||
        !q2_entities_init(g, e) || !q2_monsters_init(g, e)) {
        close_game(g);
        return false;
    }
    *out = g;
    return true;
}
qa_component qa_q2_component(qa_q2_game *g) {
    qa_component component = {.owner = g->options.owner,
                              .state = g,
                              .begin_frame = begin_frame,
                              .end_frame = end_frame,
                              .actor_frame = actor_frame,
                              .actor_released = released};
    component.clock = qa_clock_defaults(g->options.edition == QA_Q2_CLASSIC
                                            ? QA_CLOCK_Q2_CLASSIC
                                            : QA_CLOCK_Q2_RERELEASE);
    if (g->options.frame_ns != 0)
        component.clock.interval_ns = g->options.frame_ns;
    return component;
}
bool qa_q2_destroy(qa_q2_game *g, qa_error *e) {
    if (g == NULL)
        return true;
    if (!qa_session_safe(g->services.session) || !qa_world_idle(g->services.world) ||
        !qa_combat_idle(g->services.combat)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 provider destruction requires a safe point");
        return false;
    }
    if (!q2_checkpoint_idle(g, e))
        return false;
    close_game(g);
    return true;
}
typedef struct damage_reaction_call {
    qa_q2_game *game;
    qa_damage_outcome outcome;
    bool item_only;
} damage_reaction_call;

static bool damage_reaction(void *context, qa_actor_id id, qa_error *e) {
    (void)id;
    damage_reaction_call *call = context;
    qa_q2_game *g = call->game;
    const qa_damage_outcome *outcome = &call->outcome;
    if (call->item_only)
        return q2_item_reaction(g, outcome, e);
    return q2_item_reaction(g, outcome, e) && qa_q2_projectile_reaction(g, outcome, e) &&
           q2_monster_reaction(g, outcome, e) && q2_client_reaction(g, outcome, e) &&
           q2_entity_reaction(g, outcome, e);
}
bool qa_q2_damage_reaction(qa_q2_game *g, const qa_damage_outcome *outcome, qa_error *e) {
    if (!g || !outcome) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 damage reaction");
        return false;
    }
    if (!q2_actor_live(g, outcome->request.target))
        return true;
    damage_reaction_call call = {.game = g, .outcome = *outcome};
    return qa_q2_run_actor(g, call.outcome.request.target, damage_reaction, &call, e);
}
bool qa_q2_item_reaction(qa_q2_game *g, const qa_damage_outcome *outcome, qa_error *e) {
    if (!g || !outcome) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 item reaction");
        return false;
    }
    if (!q2_actor_live(g, outcome->request.target))
        return true;
    damage_reaction_call call = {.game = g, .outcome = *outcome, .item_only = true};
    return qa_q2_run_actor(g, call.outcome.request.target, damage_reaction, &call, e);
}
bool qa_q2_actor_traits(qa_q2_game *g, qa_actor_id id, qa_builtin_actor_traits *out) {
    if (g == NULL || out == NULL || !q2_actor_live(g, id) || id.slot >= g->capacity ||
        g->actors[id.slot] == NULL || !qa_actor_id_equal(g->actors[id.slot]->id, id))
        return false;
    q2_actor *a = g->actors[id.slot];
    *out = (qa_builtin_actor_traits){0};
    if (a->projectile.kind == Q2_PROJECTILE_NONE) {
        bool known = a->monster ? q2_monster_traits(g, id, out)
                   : a->client ? q2_client_traits(g, id, out)
                   : a->item ? q2_item_traits(g, id, out)
                   : a->entity ? q2_entity_traits(g, id, out) : false;
        if (a->physics_bound && (a->physics.flags & QA_PHYSICS_MONSTER)) {
            out->monster = true;
            known = true;
        }
        return known;
    }
    const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), id);
    *out = (qa_builtin_actor_traits){
        .classname = a->projectile.classname != 0 ? a->projectile.classname : record->definition,
        .owner = a->projectile.owner};
    out->damageable_target =
        a->projectile.kind == Q2_PROX || a->projectile.kind == Q2_TESLA ||
        a->projectile.kind == Q2_NUKE || a->projectile.kind == Q2_LMCTF_HOOK ||
        ((a->projectile.kind == Q2_TRAP || a->projectile.kind == Q2_CTF_HOOK) &&
         g->options.edition == QA_Q2_RERELEASE);
    out->no_source_friendly_fire =
        g->options.edition == QA_Q2_RERELEASE &&
        (a->projectile.kind == Q2_TESLA || a->projectile.kind == Q2_TRAP);
    out->monster = a->physics_bound && (a->physics.flags & QA_PHYSICS_MONSTER);
    return true;
}
bool qa_q2_projectile_read(qa_q2_game *g, qa_actor_id id, qa_q2_projectile_view *out) {
    if (g == NULL || out == NULL || !q2_actor_live(g, id) || id.slot >= g->capacity ||
        g->actors[id.slot] == NULL || !qa_actor_id_equal(g->actors[id.slot]->id, id))
        return false;
    const q2_projectile *p = &g->actors[id.slot]->projectile;
    if (p->kind == Q2_PROJECTILE_NONE)
        return false;
    *out = (qa_q2_projectile_view){.model = p->model,
                                   .loop_sound = p->loop_sound,
                                   .effects = p->effects | g->actors[id.slot]->extra_effects,
                                   .render_flags = p->render_flags,
                                   .frame = p->frame,
                                   .skin = p->skin,
                                   .scale = p->scale,
                                   .alpha = p->kind == Q2_RERELEASE_SPAWN_GROWTH
                                                ? p->alpha : g->actors[id.slot]->alpha,
                                   .visible = p->visible,
                                   .beam = p->kind == Q2_PROBOSCIS_SEGMENT ||
                                           p->kind == Q2_RERELEASE_SPAWN_BEAM,
                                   .beam_end = p->movedir};
    return true;
}
uint64_t qa_q2_actor_extra_effects(qa_q2_game *g, qa_actor_id id) {
    return g != NULL && q2_actor_live(g, id) && id.slot < g->capacity &&
                   g->actors[id.slot] != NULL && qa_actor_id_equal(g->actors[id.slot]->id, id)
               ? g->actors[id.slot]->extra_effects
               : 0;
}
bool q2_count(qa_q2_game *g, qa_actor_id actor, qa_item_id item, int *out, qa_error *e) {
    if (item == 0) {
        *out = INT_MAX;
        return true;
    }
    qa_inventory_entry entry;
    qa_error read_error = {0};
    if (!qa_inventory_entry_read(g->services.inventory, actor, item, &entry, &read_error)) {
        if (read_error.code == QA_ERROR_NOT_FOUND && q2_actor_live(g, actor)) {
            *out = 0;
            return true;
        }
        if (e != NULL)
            *e = read_error;
        return false;
    }
    if (entry.count < (entry.policy == QA_COUNT_SOURCE_INT32 ? INT32_MIN : 0) ||
        entry.count > INT_MAX || !isfinite(entry.count)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Q2 ammunition is outside the native count domain");
        return false;
    }
    *out = (int)entry.count;
    return true;
}
bool q2_ammo(q2_weapon_call *c, int *out, qa_error *e) {
    return q2_count(c->game, c->actor->id, c->game->ammo[c->definition->weapon], out, e);
}
bool q2_consume(q2_weapon_call *c, int quantity, bool honor_infinite, qa_error *e) {
    qa_q2_game *g = c->game;
    if (!q2_actor_live(g, c->actor->id))
        return true;
    qa_item_id ammo = g->ammo[c->definition->weapon];
    if (ammo == 0 ||
        (honor_infinite &&
         (c->rerelease ? c->input.infinite_ammo : (g->options.deathmatch_flags & 8192u) != 0)))
        return true;
    int before, after;
    bool consumed;
    if (!q2_ammo(c, &before, e) ||
        !qa_inventory_consume(g->services.inventory, c->actor->id, ammo, quantity, &consumed, e))
        return false;
    if (!q2_actor_live(g, c->actor->id))
        return true;
    if (!consumed) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 firing lost its admitted ammunition");
        return false;
    }
    if (!q2_ammo(c, &after, e))
        return false;
    if (c->rerelease && before > c->definition->warning && after <= c->definition->warning &&
        !q2_sound(c, "weapons/lowammo.wav", 0, 1, e))
        return false;
    return !q2_actor_live(g, c->actor->id) || g->hooks.ammo_changed == NULL ||
           g->hooks.ammo_changed(g->hooks.context, c->actor->id, ammo, e);
}
qa_attack q2_attack(q2_weapon_call *c, int mod, uint32_t flags) {
    qa_attack attack = {.time_ns = c->now_ns,
                        .attacker = c->has_attack_owner ? c->attack_owner : c->actor->id,
                        .inflictor = c->actor->id,
                        .weapon = c->definition == NULL ? 0 : c->game->items[c->definition->weapon],
                        .weapon_provider = c->game->options.owner,
                        .powerup_applied = true,
                        .powerup_owner = c->game->options.owner};
    attack.cause = qa_q2_damage_cause(c->rerelease ? QA_Q2_RERELEASE : QA_Q2_CLASSIC,
                                      c->game->options.product, mod, flags);
    return attack;
}
qa_damage_cause qa_q2_damage_cause(qa_q2_edition edition, qa_q2_product product, int canonical,
                                   uint32_t flags) {
    qa_damage_cause cause = {.kind = QA_CAUSE_Q2,
                             .source.q2 = {.means_of_death = canonical, .flags = flags}};
    uint32_t raw = (uint32_t)canonical;
    bool friendly = (raw & UINT32_C(0x08000000)) != 0;
    uint32_t id = raw & ~UINT32_C(0x08000000);
    if (canonical < 0 || id > 58)
        return cause;
    cause.source.q2.friendly_fire = friendly;
    if (edition == QA_Q2_RERELEASE) {
        cause.source.q2.native = QA_Q2_CAUSE_RERELEASE;
        cause.source.q2.native_value = (int32_t)(id < 22    ? id
                                                 : id <= 55 ? id + 1
                                                 : id == 56 ? 57
                                                 : id == 57 ? 22
                                                            : 58);
    } else {
        qa_q2_classic_cause_profile profile = product == QA_Q2_XATRIX  ? QA_Q2_NATIVE_XATRIX
                                              : product == QA_Q2_ROGUE ? QA_Q2_NATIVE_ROGUE
                                                                       : QA_Q2_NATIVE_BASE;
        if (id == 56) {
            profile = QA_Q2_NATIVE_CTF;
            id = 34;
        }
        if (!(id <= 33 || (profile == QA_Q2_NATIVE_XATRIX && id <= 39) ||
              (profile == QA_Q2_NATIVE_ROGUE && id >= 40 && id <= 55) ||
              (profile == QA_Q2_NATIVE_CTF && id == 34)))
            return cause;
        cause.source.q2.native = QA_Q2_CAUSE_CLASSIC;
        cause.source.q2.native_value = (int32_t)(id + (friendly ? UINT32_C(0x08000000) : 0));
        cause.source.q2.classic_product = (uint32_t)profile;
    }
    return cause;
}
