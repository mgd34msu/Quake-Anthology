#include "internal.h"
#include <float.h>

bool mode_fail(qa_error *error, const char *text) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", text);
    return false;
}
bool mode_callback_end(qa_modes *m, bool result) {
    --m->callback_depth;
    return result;
}
bool mode_live(const qa_modes *m, qa_actor_id actor) {
    return m && qa_actors_get(qa_session_actors(m->options.services.session), actor);
}
mode_player *mode_player_get(qa_modes *m, qa_actor_id actor) {
    if (!mode_live(m, actor) || actor.slot >= m->actor_capacity)
        return NULL;
    mode_player *p = &m->players[actor.slot];
    return p->active && qa_actor_id_equal(p->value.actor, actor) ? p : NULL;
}
mode_instance *mode_get(qa_modes *m, qa_mode_id id) {
    if (!m || id.slot >= m->mode_capacity)
        return NULL;
    mode_instance *v = &m->instances[id.slot];
    return v->active && v->id.generation == id.generation ? v : NULL;
}
mode_member *mode_member_get(qa_modes *m, mode_instance *v, qa_actor_id actor) {
    if (!v || !mode_player_get(m, actor))
        return NULL;
    mode_member *p = &v->members[actor.slot];
    return p->joined && qa_actor_id_equal(p->actor, actor) ? p : NULL;
}
mode_object *mode_object_get(qa_modes *m, qa_actor_id actor) {
    if (!mode_live(m, actor) || actor.slot >= m->actor_capacity)
        return NULL;
    mode_object *o = &m->objects[actor.slot];
    return o->active && qa_actor_id_equal(o->actor, actor) ? o : NULL;
}
int mode_team_index(const mode_instance *v, qa_team_id team) {
    if (!team)
        return -1;
    for (int i = 0; i < 3; ++i)
        if (v->value.rules.teams[i] == team)
            return i;
    return -1;
}
int32_t mode_add_i32(int32_t a, int32_t b) {
    uint32_t bits = (uint32_t)a + (uint32_t)b;
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}
void mode_stat_add(mode_instance *v, int32_t *stat, int32_t amount) {
    if (v->value.rules.source == QA_MODE_LMCTF &&
        (v->value.phase == QA_MODE_COUNTDOWN || v->value.phase == QA_MODE_FINISHED))
        return;
    *stat = mode_add_i32(*stat, amount);
}
uint32_t mode_random(qa_modes *m) {
    m->random = m->random * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    uint32_t x = (uint32_t)(((m->random >> 18u) ^ m->random) >> 27u);
    unsigned r = (unsigned)(m->random >> 59u);
    return (x >> r) | (x << ((32u - r) & 31u));
}
float mode_random_float(qa_modes *m) { return (float)(mode_random(m) >> 8) / 16777216.0f; }
bool mode_event(qa_modes *m, mode_instance *v, qa_mode_event_kind kind, qa_actor_id actor,
                qa_actor_id other, qa_actor_id object, qa_team_id team, int32_t value,
                int32_t detail, qa_error *e) {
    if (!m->options.hooks.event)
        return true;
    qa_mode_event event = {.kind = kind,
                           .mode = v->id,
                           .actor = actor,
                           .other = other,
                           .object = object,
                           .team = team,
                           .value = value,
                           .detail = detail,
                           .time_ns = v->value.time_ns};
    return MODE_CALLBACK(m, m->options.hooks.event(m->options.hooks.context, &event, e));
}
bool mode_intent(qa_modes *m, mode_instance *v, qa_match_intent_kind kind, qa_actor_id actor,
                 qa_team_id team, qa_string_id map, qa_error *e) {
    if (!m->options.hooks.intent)
        return mode_fail(e, "match transition has no campaign/match coordinator");
    qa_match_intent intent = {
        .kind = kind, .mode = v->id, .actor = actor, .team = team, .map = map};
    if (v->value.rules.source >= QA_MODE_Q3 && kind == QA_MATCH_RESTART_MAP &&
        !MODE_CALLBACK(m, qa_builtin_resource(&m->options.services, "map_restart 0\n",
                                               &intent.source_command, e))) return false;
    return MODE_CALLBACK(m, m->options.hooks.intent(m->options.hooks.context, &intent, e));
}
bool mode_alive(qa_modes *m, qa_actor_id actor) {
    qa_combat_state state;
    return mode_live(m, actor) && qa_combat_read(m->options.services.combat, actor, &state, NULL) &&
           state.health > 0;
}
static bool sound_event(qa_modes *m, mode_instance *v, qa_actor_id actor, const char *sound,
                          int32_t channel, float volume, qa_error *e) {
    qa_builtin_event event = {.kind = QA_BUILTIN_SOUND,
                              .provider = m->options.owner,
                              .actor = actor,
                              .time_ns = v->value.time_ns,
                              .volume = volume,
                              .attenuation = 1};
    qa_body_state body;
    if (!qa_world_body_read(m->options.services.world, actor, &body, e)) return false;
    if (!mode_live(m, actor)) return mode_fail(e, "mode sound actor retired during body read");
    event.origin = body.origin;
    event.channel = channel;
    event.family = v->value.rules.source <= QA_MODE_Q1_HORDE ? QA_GAME_Q1
                   : v->value.rules.source < QA_MODE_Q3      ? QA_GAME_Q2
                                                             : QA_GAME_Q3;
    return qa_builtin_resource(&m->options.services, sound, &event.resource, e) &&
           (m->options.hooks.emit
               ? m->options.hooks.emit(m->options.hooks.context, v->id, &event, e)
               : qa_builtin_emit(&m->options.services, &event, e));
}
bool mode_sound(qa_modes *m, mode_instance *v, qa_actor_id actor, const char *sound, float volume,
                qa_error *e) {
    return MODE_CALLBACK(m, sound_event(m, v, actor, sound, 3, volume, e));
}
bool mode_sound_channel(qa_modes *m, mode_instance *v, qa_actor_id actor, const char *sound,
                        int32_t channel, float volume, qa_error *e) {
    return MODE_CALLBACK(m, sound_event(m, v, actor, sound, channel, volume, e));
}
bool mode_count(qa_modes *m, qa_actor_id actor, qa_item_id item, double *count, qa_error *e) {
    qa_inventory_entry entry;
    qa_error local = {0};
    if (!qa_inventory_entry_read(m->options.services.inventory, actor, item, &entry, &local)) {
        if (local.code == QA_ERROR_NOT_FOUND &&
            qa_inventory_has(m->options.services.inventory, actor)) {
            *count = 0;
            return true;
        }
        if (e)
            *e = local;
        return false;
    }
    *count = entry.count;
    return true;
}
bool mode_set_count(qa_modes *m, qa_actor_id actor, qa_item_id item, double count, qa_error *e) {
    if (!item)
        return true;
    qa_inventory_entry entry;
    qa_error local = {0};
    if (!qa_inventory_entry_read(m->options.services.inventory, actor, item, &entry, &local)) {
        if (local.code != QA_ERROR_NOT_FOUND ||
            !qa_inventory_has(m->options.services.inventory, actor)) {
            if (e)
                *e = local;
            return false;
        }
        entry = (qa_inventory_entry){.item = item, .capacity = 1, .policy = QA_COUNT_SOURCE_INT32};
    }
    entry.count = count;
    if (entry.capacity < count)
        entry.capacity = count;
    return qa_inventory_configure(m->options.services.inventory, actor, &entry, NULL, NULL, e);
}
bool mode_near(qa_modes *m, qa_actor_id a, qa_actor_id b, float distance) {
    qa_body_state x, y;
    if (!qa_world_body_read(m->options.services.world, a, &x, NULL) ||
        !qa_world_body_read(m->options.services.world, b, &y, NULL))
        return false;
    qa_vec3 d = qa_vec_sub(x.origin, y.origin);
    return qa_vec_dot(d, d) < distance * distance;
}
bool mode_visible(qa_modes *m, qa_actor_id a, qa_actor_id b, bool pvs) {
    if (m->options.hooks.visible)
        return m->options.hooks.visible(m->options.hooks.context, a, b, pvs);
    /* PVS is a map capability and cannot be replaced by an unobstructed ray. */
    if (pvs)
        return false;
    qa_body_state from, to;
    if (!qa_world_body_read(m->options.services.world, a, &from, NULL) ||
        !qa_world_body_read(m->options.services.world, b, &to, NULL))
        return false;
    qa_builtin_actor_traits traits = {0};
    if (m->options.services.actor_traits)
        m->options.services.actor_traits(m->options.services.context, a, &traits);
    from.origin.z += traits.view_height;
    qa_trace_query query = {
        .start = from.origin,
        .end = to.origin,
        .pass_actor = a,
        .policy = {.family = QA_COLLISION_Q2, .contents_mask = qa_collision_contents_mask(3, QA_COLLISION_Q2), .q1_hull = -1}};
    qa_trace_result trace;
    return qa_world_trace(m->options.services.world, &query, &trace, NULL) &&
           (trace.fraction == 1 || qa_actor_id_equal(trace.actor, b));
}
qa_mode_rules qa_mode_defaults(qa_mode_source source, qa_mode_kind kind) {
    return (qa_mode_rules){.source = source,
                           .kind = kind,
                           .enabled = true,
                           .teamplay = source == QA_MODE_ROGUE && kind == QA_MODE_TAG ? 3 : 0,
                           .relics = source == QA_MODE_THREEWAVE || source == QA_MODE_Q2_CTF ||
                                     source == QA_MODE_LMCTF,
                           .vote_limit = source >= QA_MODE_Q3 ? 3 : 0,
                           .setup_seconds = 600,
                           .countdown_seconds = source == QA_MODE_LMCTF ? 15 : 20,
                           .match_lock = source == QA_MODE_Q2_CTF,
                           .match_seconds = 1200,
                           .election_percent = 66,
                           .rune_mask = 15,
                           .obelisk_health = 2500,
                           .obelisk_regen = 15,
                           .obelisk_regen_ns = MODE_SECOND,
                           .obelisk_respawn_ns = 10 * MODE_SECOND};
}
bool mode_rules_valid(const qa_mode_rules *r) {
    if (!r || r->source > QA_MODE_TEAM_ARENA || r->kind > QA_MODE_HORDE || r->source < QA_MODE_Q1 ||
        r->kind < QA_MODE_COOPERATIVE || !isfinite(r->time_limit_minutes) ||
        r->time_limit_minutes < 0 || r->time_limit_minutes > 1000000 ||
        !isfinite(r->obelisk_health) || r->obelisk_health <= 0 || !isfinite(r->obelisk_regen) ||
        r->obelisk_regen < 0 || r->setup_seconds < 0 || r->countdown_seconds < 0 ||
        r->match_seconds < 0 || r->warmup_seconds < 0 || r->max_game_players < 0 ||
        r->vote_limit < 0 || r->election_percent < 0 || r->election_percent > 100)
        return false;
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j)
            if (r->teams[i] && r->teams[i] == r->teams[j])
                return false;
    return true;
}
bool qa_modes_create(const qa_modes_options *options, qa_modes **out, qa_error *e) {
    if (!options || !out || !options->owner || !options->services.inventory ||
        !options->services.players || !qa_builtin_services_validate(&options->services, e))
        return mode_fail(e, "invalid mode services");
    qa_modes *m = calloc(1, sizeof(*m));
    if (!m) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating modes");
        return false;
    }
    m->options = *options;
    m->actor_capacity = qa_actors_capacity(qa_session_actors(options->services.session));
    m->mode_capacity = options->mode_capacity ? options->mode_capacity : 16;
    m->objective_capacity = options->objective_capacity ? options->objective_capacity : 256;
    m->random = options->random_seed;
    m->next_serial = 1;
    m->players = calloc(m->actor_capacity, sizeof(*m->players));
    m->objects = calloc(m->actor_capacity, sizeof(*m->objects));
    m->instances = calloc(m->mode_capacity, sizeof(*m->instances));
    m->objectives = calloc(m->objective_capacity, sizeof(*m->objectives));
    if (!m->players || !m->objects || !m->instances || !m->objectives) {
        qa_modes_destroy(m);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating mode stores");
        return false;
    }
    if (!qa_builtin_snapshot_reserve(&m->players_order, m->actor_capacity, e) ||
        !qa_builtin_snapshot_reserve(&m->observations, m->actor_capacity, e)) {
        qa_modes_destroy(m);
        return false;
    }
    *out = m;
    return true;
}
void qa_modes_destroy(qa_modes *m) {
    if (!m)
        return;
    if (m->players)
        for (uint32_t i = 0; i < m->actor_capacity; ++i)
            if (m->players[i].items.serial)
                (void)qa_inventory_close_items(m->options.services.inventory, m->players[i].items,
                                               NULL);
    if (m->instances)
        for (uint32_t i = 0; i < m->mode_capacity; ++i) {
            mode_instance *v = &m->instances[i];
            free(v->members);
            free(v->bindings);
            free(v->ghosts);
            free(v->sorted);
            free(v->ranks);
            free(v->spawns);
            free(v->items);
            mode_horde_free(v);
        }
    qa_builtin_snapshot_free(&m->players_order);
    qa_builtin_snapshot_free(&m->observations);
    qa_builtin_snapshot_pool_free(&m->snapshot_frames);
    free(m->players);
    free(m->objects);
    free(m->instances);
    free(m->objectives);
    free(m->restored_objectives);
    free(m);
}
bool qa_modes_add(qa_modes *m, const qa_mode_rules *rules, qa_mode_id *out, qa_error *e) {
    if (!m || !out || !mode_rules_valid(rules))
        return mode_fail(e, "invalid mode rules");
    for (uint32_t i = 0; i < m->mode_capacity; ++i)
        if (!m->instances[i].active) {
            mode_instance *v = &m->instances[i];
            if (v->id.generation == UINT64_MAX)
                continue;
            mode_member *members = calloc(m->actor_capacity, sizeof(*members));
            mode_match_owner *bindings = calloc(m->actor_capacity, sizeof(*bindings));
            mode_ghost *ghosts = calloc(m->actor_capacity, sizeof(*ghosts));
            qa_actor_id *sorted = calloc(m->actor_capacity, sizeof(*sorted));
            mode_rank_entry *ranks = calloc(m->actor_capacity, sizeof(*ranks));
            if (!members || !bindings || !ghosts || !sorted || !ranks) {
                free(members);
                free(bindings);
                free(ghosts);
                free(sorted);
                free(ranks);
                qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating mode roster");
                return false;
            }
            qa_mode_id id = {i, v->id.generation + 1};
            *v = (mode_instance){.id = id,
                                 .active = true,
                                 .members = members,
                                 .bindings = bindings,
                                 .ghosts = ghosts,
                                 .sorted = sorted,
                                 .ranks = ranks,
                                 .rune_cursor = SIZE_MAX,
                                 .rune_forward = true};
            v->value.rules = *rules;
            v->value.phase =
                rules->source == QA_MODE_LMCTF                              ? QA_MODE_WAITING
                : rules->competition > 1 && rules->source == QA_MODE_Q2_CTF ? QA_MODE_SETUP
                : rules->warmup_seconds > 0 || rules->kind == QA_MODE_DUEL  ? QA_MODE_WAITING
                                                                            : QA_MODE_PLAYING;
            v->value.deadline_ns =
                v->value.phase == QA_MODE_SETUP ? (uint64_t)rules->setup_seconds * MODE_SECOND : 0;
            *out = id;
            return true;
        }
    return mode_fail(e, "mode capacity exhausted");
}
bool qa_modes_remove(qa_modes *m, qa_mode_id id, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v)
        return mode_fail(e, "stale mode");
    if (m->callback_depth)
        return mode_fail(e, "cannot remove a mode during a synchronous callback");
    if (!MODE_CALLBACK(m, mode_horde_retire(m, v, e)))
        return false;
    for (uint32_t i = 0; i < m->actor_capacity; ++i) {
        mode_object *o = &m->objects[i];
        if (!o->active || o->mode.slot != id.slot || o->mode.generation != id.generation ||
            !mode_live(m, o->value.carrier))
            continue;
        if (!mode_object_count(m, v, o, o->value.carrier, 0, e))
            return false;
        o->value.carrier = (qa_actor_id){0};
    }
    v->active = false;
    if (!MODE_CALLBACK(m, mode_horde_reconcile_keys(m, v, e))) {
        v->active = true;
        return false;
    }
    for (uint32_t i = 0; i < m->objective_capacity; ++i) {
        mode_objective *objective = &m->objectives[i];
        if (objective->active && objective->binding.mode.slot == id.slot &&
            objective->binding.mode.generation == id.generation)
            *objective = (mode_objective){0};
    }
    for (uint32_t i = 0; i < m->actor_capacity; ++i) {
        mode_object *o = &m->objects[i];
        if (o->active && o->mode.slot == id.slot && o->mode.generation == id.generation &&
            mode_live(m, o->actor) && !qa_session_release(m->options.services.session, o->actor, e))
            return false;
    }
    free(v->members);
    free(v->bindings);
    free(v->ghosts);
    free(v->sorted);
    free(v->ranks);
    free(v->spawns);
    free(v->items);
    mode_horde_free(v);
    *v = (mode_instance){.id = id};
    for (size_t i = 0; i < m->players_order.count; ++i)
        if (mode_player_get(m, m->players_order.ids[i]) &&
            !qa_modes_publish_items(m, m->players_order.ids[i], e))
            return false;
    return true;
}
bool qa_modes_add_q1_composition(qa_modes *m, const qa_mode_rules *rules,
    qa_actor_owner source, qa_mode_id *out, qa_error *e) {
    if (!m || !source || !rules || !out ||
        (rules->source != QA_MODE_THREEWAVE && rules->source != QA_MODE_ROGUE) ||
        !m->options.hooks.q1_composition_expected ||
        !m->options.hooks.q1_composition_current ||
        !m->options.hooks.q1_composition_player_current ||
        (rules->source == QA_MODE_ROGUE &&
            (!m->options.hooks.q1_rogue_state || !m->options.hooks.q1_rogue_state_current ||
             !m->options.hooks.q1_rogue_number_read || !m->options.hooks.q1_rogue_number_write)) ||
        !qa_strings_text(qa_session_strings(m->options.services.session), source).size)
        return mode_fail(e, "Q1 composition requires its actual native source identity");
    for (uint32_t i = 0; i < m->mode_capacity; ++i)
        if (m->instances[i].active &&
            m->instances[i].value.origin == QA_MODE_NATIVE_Q1_COMPOSITION &&
            m->instances[i].value.source_owner == source)
            return mode_fail(e, "native Q1 composition already exists");
    qa_mode_id id;
    if (!qa_modes_add(m, rules, &id, e)) return false;
    mode_instance *v = mode_get(m, id);
    v->value.origin = QA_MODE_NATIVE_Q1_COMPOSITION;
    v->value.source_owner = source;
    *out = id;
    return true;
}
bool mode_q1_source_current(qa_modes *m, mode_instance *v, qa_actor_id actor,
    bool *observer, qa_error *e) {
    if (!v || v->value.origin != QA_MODE_NATIVE_Q1_COMPOSITION ||
        !mode_player_get(m, actor) || !observer ||
        !m->options.hooks.q1_composition_player_current)
        return mode_fail(e, "Q1 composition player has no genuine source binding");
    qa_mode_id id = v->id;
    qa_actor_owner source = v->value.source_owner;
    qa_mode_source kind = v->value.rules.source;
    if (!MODE_CALLBACK(m, m->options.hooks.q1_composition_player_current(
        m->options.hooks.context, source, kind, actor, observer, e))) return false;
    return (mode_get(m, id) == v && v->value.source_owner == source &&
        v->value.rules.source == kind && mode_player_get(m, actor)) ||
        mode_fail(e, "Q1 composition player changed during source qualification");
}
bool qa_modes_q1_source_admit(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_error *e) {
    if (!m || m->source_restored)
        return mode_fail(e, "Q1 source admission requires completed shared restoration");
    mode_instance *v = mode_get(m, id);
    bool observer;
    if (!mode_q1_source_current(m, v, actor, &observer, e)) return false;
    mode_member *p = &v->members[actor.slot];
    if (p->joined)
        return qa_actor_id_equal(p->actor, actor) ||
            mode_fail(e, "Q1 composition slot retains a different source player");
    *p = (mode_member){.actor = actor, .joined = true};
    return true;
}
static mode_member *q1_number_member(qa_modes *m, qa_mode_id id, qa_actor_id actor,
    qa_mode_q1_number number, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    bool observer;
    if (number < QA_Q1_CTF_LAST_TEAM || number >= QA_Q1_SOURCE_NUMBERS || !v ||
        (v->value.rules.source == QA_MODE_THREEWAVE ? number >= QA_Q1_ROGUE_STEAM
            : v->value.rules.source != QA_MODE_ROGUE || number < QA_Q1_ROGUE_STEAM)) {
        mode_fail(e, "Q1 source number does not belong to the actual source program");
        return NULL;
    }
    if (!mode_q1_source_current(m, v, actor, &observer, e)) return NULL;
    mode_member *p = mode_member_get(m, v, actor);
    if (!p) mode_fail(e, "Q1 source number has no admitted source continuation");
    return p;
}
static bool rogue_current(qa_modes *m, qa_mode_id id, qa_actor_id actor,
    mode_member *member, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    bool observer;
    if (!v || !member || !member->q1.rogue_state.registry ||
        !m->options.hooks.q1_rogue_state_current)
        return mode_fail(e, "Rogue source has no actual retained team-state actor");
    if (!mode_q1_source_current(m, v, actor, &observer, e) ||
        mode_member_get(m, v, actor) != member || !MODE_CALLBACK(m,
            m->options.hooks.q1_rogue_state_current(m->options.hooks.context,
                v->value.source_owner, actor, member->q1.rogue_state, e))) return false;
    return (mode_get(m, id) == v && mode_member_get(m, v, actor) == member) ||
        mode_fail(e, "Rogue source player changed during state qualification");
}
bool qa_modes_q1_source_read(qa_modes *m, qa_mode_id id, qa_actor_id actor,
    qa_mode_q1_number number, double *out, qa_error *e) {
    if (!out) return mode_fail(e, "Q1 source number requires an output");
    mode_member *p = q1_number_member(m, id, actor, number, e);
    if (!p) return false;
    if (number >= QA_Q1_ROGUE_STEAM) {
        mode_instance *v = mode_get(m, id);
        double value;
        if (!rogue_current(m, id, actor, p, e) || !m->options.hooks.q1_rogue_number_read ||
            !MODE_CALLBACK(m, m->options.hooks.q1_rogue_number_read(m->options.hooks.context,
                v->value.source_owner, actor, p->q1.rogue_state,
                (uint32_t)(number - QA_Q1_ROGUE_STEAM), &value, e)) ||
            !rogue_current(m, id, actor, p, e)) return false;
        *out = value;
        return true;
    }
    *out = p->q1.numbers[number];
    return true;
}
bool qa_modes_q1_source_write(qa_modes *m, qa_mode_id id, qa_actor_id actor,
    qa_mode_q1_number number, double value, qa_error *e) {
    if (!m || m->source_restored)
        return mode_fail(e, "Q1 source mutation requires completed restoration");
    mode_member *p = q1_number_member(m, id, actor, number, e);
    if (!p) return false;
    if (number >= QA_Q1_ROGUE_STEAM) {
        mode_instance *v = mode_get(m, id);
        return rogue_current(m, id, actor, p, e) && m->options.hooks.q1_rogue_number_write &&
            MODE_CALLBACK(m, m->options.hooks.q1_rogue_number_write(m->options.hooks.context,
                v->value.source_owner, actor, p->q1.rogue_state,
                (uint32_t)(number - QA_Q1_ROGUE_STEAM), value, e)) &&
            rogue_current(m, id, actor, p, e);
    }
    p->q1.numbers[number] = (float)(value);
    return true;
}
bool qa_modes_q1_rogue_initialize(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_error *e) {
    if (!m || m->source_restored)
        return mode_fail(e, "Rogue source initialization requires completed shared restoration");
    mode_member *p = q1_number_member(m, id, actor, QA_Q1_ROGUE_STEAM, e);
    if (!p) return false;
    if (!p->q1.rogue_state.registry) {
        mode_instance *v = mode_get(m, id);
        qa_actor_id state;
        bool observer;
        if (!m->options.hooks.q1_rogue_state || !MODE_CALLBACK(m,
            m->options.hooks.q1_rogue_state(m->options.hooks.context,
                v->value.source_owner, actor, &state, e)) ||
            !mode_q1_source_current(m, v, actor, &observer, e) ||
            mode_member_get(m, v, actor) != p) return false;
        p->q1.rogue_state = state;
    }
    return rogue_current(m, id, actor, p, e);
}
bool qa_modes_idle(const qa_modes *m) {
    if (!m || m->callback_depth || m->source_restored)
        return false;
    for (uint32_t i = 0; i < m->objective_capacity; ++i)
        if (m->objectives[i].reserved)
            return false;
    return true;
}
bool qa_modes_configure(qa_modes *m, qa_mode_id id, const qa_mode_rules *rules, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !mode_rules_valid(rules))
        return mode_fail(e, "invalid mode configuration");
    if (m->callback_depth)
        return mode_fail(e, "cannot configure a mode during a synchronous callback");
    if (rules->source != v->value.rules.source || rules->kind != v->value.rules.kind)
        return mode_fail(e, "source and mode kind require a new mode instance");
    bool warmup = rules->warmup_seconds != v->value.rules.warmup_seconds;
    int64_t delta = v->value.phase == QA_MODE_SETUP
                        ? (int64_t)rules->setup_seconds - v->value.rules.setup_seconds
                    : v->value.phase == QA_MODE_COUNTDOWN
                        ? (int64_t)rules->countdown_seconds - v->value.rules.countdown_seconds
                        : (int64_t)rules->match_seconds - v->value.rules.match_seconds;
    if (v->value.rules.source == QA_MODE_Q2_CTF && v->value.deadline_ns && delta) {
        uint64_t amount = (uint64_t)(delta < 0 ? -delta : delta) * MODE_SECOND;
        v->value.deadline_ns =
            delta < 0 ? (amount > v->value.deadline_ns ? 0 : v->value.deadline_ns - amount)
                      : v->value.deadline_ns + amount;
    }
    bool enabled_changed = v->value.rules.enabled != rules->enabled;
    v->value.rules = *rules;
    if (enabled_changed && !MODE_CALLBACK(m, mode_horde_reconcile_keys(m, v, e))) {
        v->value.rules.enabled = !rules->enabled;
        return false;
    }
    for (uint32_t i = 0; i < m->actor_capacity; ++i) {
        mode_object *o = &m->objects[i];
        if (o->active && o->mode.slot == id.slot && o->mode.generation == id.generation &&
            !mode_object_sync(m, o, e))
            return false;
    }
    if (warmup && v->value.phase <= QA_MODE_COUNTDOWN)
        return mode_set_phase(m, v, QA_MODE_WAITING, 0, e);
    return true;
}
bool qa_modes_read(qa_modes *m, qa_mode_id id, qa_mode_view *out, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !out)
        return mode_fail(e, "invalid mode read");
    *out = v->value;
    return true;
}
bool qa_modes_at(qa_modes *m, size_t index, qa_mode_id *id, qa_mode_view *out, qa_error *e) {
    if (!m || !id || !out)
        return mode_fail(e, "invalid mode enumeration");
    for (uint32_t i = 0; i < m->mode_capacity; ++i)
        if (m->instances[i].active && !index--) {
            *id = m->instances[i].id;
            *out = m->instances[i].value;
            return true;
        }
    qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "mode index outside active set");
    return false;
}
bool qa_modes_player(qa_modes *m, const qa_match_player *value, qa_error *e) {
    if (!value || !mode_live(m, value->actor))
        return mode_fail(e, "invalid match player");
    qa_actor_id actor = value->actor;
    mode_player *p = &m->players[actor.slot];
    mode_player before = *p;
    if (p->active && qa_actor_id_equal(p->value.actor, value->actor))
        p->value = *value;
    else
        *p = (mode_player){.modes = m, .active = true, .value = *value};
    if (!m->callback_depth && !qa_builtin_players(&m->options.services, &m->players_order, e)) {
        if (mode_live(m, actor) && qa_actor_id_equal(p->value.actor, actor))
            *p = before;
        return false;
    }
    return true;
}
bool qa_modes_player_read(qa_modes *m, qa_mode_id id, qa_actor_id actor,
                          qa_mode_player_view *out, qa_error *e) {
    mode_player *p = mode_player_get(m, actor);
    mode_member *member = mode_member_get(m, mode_get(m, id), actor);
    if (!p || !member || !out)
        return mode_fail(e, "unknown match player");
    qa_mode_player_view view = {.mode = id, .connection = p->value, .state = member->player};
    if (!qa_modes_score(m, id, actor, &view.state.score, e) ||
        !qa_modes_team(m, id, actor, &view.state.team, e)) return false;
    *out = view;
    return true;
}
bool qa_modes_player_read_optional(qa_modes *m, qa_mode_id id, qa_actor_id actor,
    qa_mode_player_view *out, bool *found, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !out || !found || !mode_live(m, actor))
        return mode_fail(e, "Optional match read requires its live mode and full actor");
    if (!mode_member_get(m, v, actor)) {
        *found = false;
        return true;
    }
    qa_mode_player_view value;
    if (!qa_modes_player_read(m, id, actor, &value, e)) return false;
    *out = value;
    *found = true;
    return true;
}
bool qa_modes_ctf_read(qa_modes *m, qa_mode_id id, qa_actor_id actor,
                       qa_mode_ctf_view *out, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!v || !p || !out || v->value.rules.source != QA_MODE_THREEWAVE ||
        !v->value.rules.enabled)
        return mode_fail(e, "unknown ThreeWave continuation");
    if (v->value.origin == QA_MODE_NATIVE_Q1_COMPOSITION) {
        bool observer;
        if (!mode_q1_source_current(m, v, actor, &observer, e)) return false;
        p = mode_member_get(m, v, actor);
        if (!p) return mode_fail(e, "ThreeWave source continuation retired during read");
        double last = p->q1.numbers[QA_Q1_CTF_LAST_TEAM];
        double status = p->q1.numbers[QA_Q1_CTF_STATUS];
        double access = p->q1.numbers[QA_Q1_CTF_ACCESS];
        *out = (qa_mode_ctf_view){.last_team = last,
            .status = status, .access = access,
            .start_map = v->value.rules.start_map, .pregame_over = v->value.ctf_pregame_over,
            .observer = observer, .grapple_disabled = (v->value.rules.teamplay & 2048) != 0};
        return true;
    }
    *out = (qa_mode_ctf_view){.last_team = p->player.ctf_last_team,
        .status = p->player.ctf_status, .access = p->player.ctf_access,
        .start_map = v->value.rules.start_map,
        .pregame_over = v->value.ctf_pregame_over,
        .observer = p->player.spectator,
        .grapple_disabled = (v->value.rules.teamplay & 2048) != 0};
    return true;
}
bool qa_modes_ctf_restore_player(qa_modes *m, qa_mode_id id, qa_actor_id actor,
                                  double last_team, double status, double access, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!v || !p || v->value.rules.source != QA_MODE_THREEWAVE)
        return mode_fail(e, "invalid ThreeWave player continuation");
    if (v->value.origin == QA_MODE_NATIVE_Q1_COMPOSITION) {
        bool observer;
        if (!mode_q1_source_current(m, v, actor, &observer, e)) return false;
        p = mode_member_get(m, v, actor);
        if (!p) return mode_fail(e, "ThreeWave source continuation retired during restore");
        p->q1.numbers[QA_Q1_CTF_LAST_TEAM] = v->value.rules.start_map ? 1 : (float)(last_team);
        p->q1.numbers[QA_Q1_CTF_STATUS] = (float)(status);
        p->q1.numbers[QA_Q1_CTF_ACCESS] = (float)(access);
        return true;
    }
    if (!isfinite(last_team) || last_team < INT32_MIN || last_team > INT32_MAX ||
        trunc(last_team) != last_team || !isfinite(status) || !isfinite(access) ||
        fabs(status) > FLT_MAX || fabs(access) > FLT_MAX)
        return mode_fail(e, "selected ThreeWave continuation exceeds its field representation");
    if (!v->value.rules.start_map && (last_team == 5 || last_team == 14)) {
        qa_team_id team = v->value.rules.teams[last_team == 5 ? 0 : 1];
        if (!qa_modes_set_team(m, id, actor, team, e))
            return false;
        p = mode_member_get(m, v, actor);
        if (!p)
            return true;
        p->last_team = team;
    }
    p->player.ctf_last_team = v->value.rules.start_map ? 1 : (int32_t)last_team;
    p->player.ctf_status = (float)status;
    p->player.ctf_access = (float)access;
    return true;
}
bool qa_modes_ctf_pregame_end(qa_modes *m, qa_mode_id id, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || v->value.rules.source != QA_MODE_THREEWAVE)
        return mode_fail(e, "unknown ThreeWave pregame owner");
    v->value.ctf_pregame_over = true;
    return true;
}
bool qa_modes_bind_player(qa_modes *m, qa_mode_id id, qa_actor_id actor, const qa_match_binding *binding,
                          qa_match_lease *out, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!p || !binding || !binding->owner || !binding->score || !binding->set_score ||
        !binding->team || !binding->set_team || !out || v->bindings[actor.slot].serial ||
        m->next_serial == UINT64_MAX)
        return mode_fail(e, "invalid or already bound match player");
    mode_match_owner *owner = &v->bindings[actor.slot];
    *owner = (mode_match_owner){.binding = *binding, .serial = m->next_serial++};
    p->external_owner = binding->owner;
    *out = (qa_match_lease){.mode = id, .actor = actor, .serial = owner->serial};
    return true;
}
bool qa_modes_unbind_player(qa_modes *m, qa_match_lease lease, qa_error *e) {
    mode_instance *v = mode_get(m, lease.mode);
    mode_member *p = mode_member_get(m, v, lease.actor);
    if (!p || !lease.serial || v->bindings[lease.actor.slot].serial != lease.serial)
        return true;
    int32_t score;
    qa_team_id team;
    mode_match_owner *owner = &v->bindings[lease.actor.slot];
    qa_match_binding binding = owner->binding;
    if (!MODE_CALLBACK(m, binding.score(binding.context, &score, e)))
        return false;
    if (!mode_member_get(m, v, lease.actor) || owner->serial != lease.serial)
        return true;
    if (!MODE_CALLBACK(m, binding.team(binding.context, &team, e)))
        return false;
    if (!mode_member_get(m, v, lease.actor) || owner->serial != lease.serial)
        return true;
    p->player.score = score;
    p->player.team = team;
    p->external_owner = 0;
    *owner = (mode_match_owner){0};
    return true;
}
bool qa_modes_player_lease(const qa_modes *m, qa_mode_id id, qa_actor_id actor,
    qa_actor_owner source, const void *context, qa_match_lease *out, bool *present, qa_error *e) {
    if (!m || !source || !context || !out || !present || !actor.registry ||
        actor.slot >= m->actor_capacity || id.slot >= m->mode_capacity)
        return mode_fail(e, "match lease lookup needs its actual full actor and callback owner");
    *present = false;
    const mode_instance *v = &m->instances[id.slot];
    if (!v->active || v->id.generation != id.generation) return true;
    const mode_player *player = &m->players[actor.slot];
    const mode_member *member = &v->members[actor.slot];
    if (!player->active || !qa_actor_id_equal(player->value.actor, actor) ||
        !member->joined || !qa_actor_id_equal(member->actor, actor)) return true;
    const mode_match_owner *owner = &v->bindings[actor.slot];
    if (!owner->serial) return true;
    if (owner->binding.owner != source || member->external_owner != source ||
        owner->binding.context != context)
        return mode_fail(e, "match lease belongs to another actual Source callback owner");
    *out = (qa_match_lease){.mode = id, .actor = actor, .serial = owner->serial};
    *present = true; return true;
}
bool qa_modes_score(qa_modes *m, qa_mode_id id, qa_actor_id actor, int32_t *out, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!p || !out)
        return mode_fail(e, "unknown score owner");
    mode_match_owner *owner = &v->bindings[actor.slot];
    if (owner->serial) {
        uint64_t serial = owner->serial;
        qa_match_binding binding = owner->binding;
        int32_t score;
        if (!MODE_CALLBACK(m, binding.score(binding.context, &score, e)))
            return false;
        if (!mode_member_get(m, v, actor) || owner->serial != serial)
            return mode_fail(e, "mode score owner changed during read");
        *out = score;
        return true;
    }
    if (m->options.hooks.q1_source_score) {
        bool bound = false;
        int32_t score;
        if (!MODE_CALLBACK(m, m->options.hooks.q1_source_score(
            m->options.hooks.context, id, actor, &bound, &score, e))) return false;
        p = mode_member_get(m, mode_get(m, id), actor);
        if (mode_get(m, id) != v || !p)
            return mode_fail(e, "native Q1 score owner changed during read");
        if (bound) {
            *out = score;
            return true;
        }
    }
    *out = p->player.score;
    return true;
}
bool qa_modes_set_score(qa_modes *m, qa_mode_id id, qa_actor_id actor, int32_t score, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!v || !p)
        return mode_fail(e, "invalid score mutation");
    mode_match_owner *owner = &v->bindings[actor.slot];
    if (owner->serial) {
        uint64_t serial = owner->serial;
        qa_match_binding binding = owner->binding;
        if (!MODE_CALLBACK(m, binding.set_score(binding.context, score, e)))
            return false;
        if (!mode_member_get(m, v, actor))
            return true;
        if (owner->serial != serial)
            return mode_fail(e, "mode score owner changed during mutation");
    } else {
        bool bound = false;
        if (m->options.hooks.q1_source_set_score && !MODE_CALLBACK(m,
            m->options.hooks.q1_source_set_score(m->options.hooks.context,
                id, actor, score, &bound, e))) return false;
        p = mode_member_get(m, mode_get(m, id), actor);
        if (!p || mode_get(m, id) != v)
            return mode_fail(e, "native Q1 score owner changed during mutation");
        if (!bound) p->player.score = score;
    }
    for (uint32_t i = 0; i < m->actor_capacity; ++i)
        if (v->ghosts[i].code && qa_actor_id_equal(v->ghosts[i].actor, actor))
            v->ghosts[i].score = score;
    return mode_event(m, v, QA_MODE_SCORE, actor, (qa_actor_id){0}, (qa_actor_id){0}, 0, score, 0,
                      e);
}
bool qa_modes_add_score(qa_modes *m, qa_mode_id id, qa_actor_id actor, int32_t amount,
                        qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *member = mode_member_get(m, v, actor);
    if (!member) return mode_fail(e, "unknown score recipient");
    bool bound = false;
    if (!v->bindings[actor.slot].serial && m->options.hooks.q1_source_add_score &&
        !MODE_CALLBACK(m, m->options.hooks.q1_source_add_score(
            m->options.hooks.context, id, actor, amount, &bound, e))) return false;
    if (mode_get(m, id) != v || !mode_member_get(m, v, actor))
        return mode_fail(e, "native Q1 score owner changed during addition");
    int32_t score;
    if (!qa_modes_score(m, id, actor, &score, e)) return false;
    if (bound) {
        if (!mode_event(m, v, QA_MODE_SCORE, actor, (qa_actor_id){0},
            (qa_actor_id){0}, 0, score, 0, e)) return false;
    } else if (!qa_modes_set_score(m, id, actor, mode_add_i32(score, amount), e)) return false;
    v = mode_get(m, id);
    member = mode_member_get(m, v, actor);
    if (member)
        mode_stat_add(v, &member->stats.score, amount);
    return true;
}
bool qa_modes_team(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_team_id *out, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!out || !p)
        return mode_fail(e, "unknown mode team actor");
    mode_match_owner *owner = &v->bindings[actor.slot];
    if (owner->serial) {
        uint64_t serial = owner->serial;
        qa_match_binding binding = owner->binding;
        qa_team_id team;
        if (!MODE_CALLBACK(m, binding.team(binding.context, &team, e)))
            return false;
        if (!mode_member_get(m, v, actor) || owner->serial != serial)
            return mode_fail(e, "mode team owner changed during read");
        *out = team;
        return true;
    }
    *out = p->player.team;
    return true;
}
bool qa_modes_set_team(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_team_id team, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!p)
        return mode_fail(e, "unknown team player");
    mode_match_owner *owner = &v->bindings[actor.slot];
    if (owner->serial) {
        uint64_t serial = owner->serial;
        qa_match_binding binding = owner->binding;
        if (!MODE_CALLBACK(m, binding.set_team(binding.context, team, e)))
            return false;
        return !mode_member_get(m, v, actor) || owner->serial == serial ||
            mode_fail(e, "mode team owner changed during mutation");
    }
    p->player.team = team;
    return true;
}
bool qa_modes_team_totals(qa_modes *m, qa_mode_id id, int64_t totals[3], qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !totals)
        return mode_fail(e, "invalid team total query");
    totals[0] = totals[1] = totals[2] = 0;
    for (size_t i = 0; i < m->players_order.count; ++i) {
        qa_actor_id actor = m->players_order.ids[i];
        qa_team_id team;
        int32_t score;
        if (!mode_member_get(m, v, actor))
            continue;
        if (!qa_modes_team(m, id, actor, &team, e) || !qa_modes_score(m, id, actor, &score, e))
            return false;
        int index = mode_team_index(v, team);
        if (index >= 0)
            totals[index] += score;
    }
    return true;
}
bool qa_modes_same_team(qa_modes *m, qa_mode_id id, qa_actor_id a, qa_actor_id b) {
    qa_team_id x, y;
    return qa_modes_team(m, id, a, &x, NULL) && qa_modes_team(m, id, b, &y, NULL) && x && x == y;
}
bool qa_modes_statistics(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_mode_statistics *out,
                         qa_error *e) {
    mode_member *p = mode_member_get(m, mode_get(m, id), actor);
    if (!p || !out)
        return mode_fail(e, "unknown mode participant");
    *out = p->stats;
    return true;
}
bool qa_modes_source_award(qa_modes *m, qa_mode_id id, qa_actor_id actor,
                            int32_t source_award, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!v || !p ||
        (source_award != 11 && source_award != 12))
        return mode_fail(e, "invalid source mode award");
    mode_stat_add(v, source_award == 11 ? &p->stats.q3_defend_count : &p->stats.q3_assist_count, 1);
    return true;
}
bool qa_modes_team_score(qa_modes *m, qa_mode_id id, qa_team_id team, int32_t amount, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    int index = v ? mode_team_index(v, team) : -1;
    if (index < 0)
        return mode_fail(e, "unknown scoring team");
    int32_t before = v->value.team_scores[index];
    v->value.team_scores[index] = mode_add_i32(before, amount);
    int code = 0;
    if (v->value.rules.source >= QA_MODE_Q3 && index < 2) {
        int32_t other = v->value.team_scores[index ^ 1], after = v->value.team_scores[index];
        code = after == other ? 12 : before <= other && after > other ? 10 + index : 8 + index;
    }
    return mode_event(m, v, QA_MODE_TEAM_SCORE, (qa_actor_id){0}, (qa_actor_id){0},
                      (qa_actor_id){0}, team, v->value.team_scores[index], code, e);
}
bool mode_join(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_team_id team, bool observer,
               bool ghost_rejoin, qa_error *e) {
    mode_player *p = mode_player_get(m, actor);
    if (!v || !p || v->value.origin == QA_MODE_NATIVE_Q1_COMPOSITION ||
        (team && v->value.rules.kind > QA_MODE_TEAM_DEATHMATCH && mode_team_index(v, team) < 0))
        return mode_fail(e, "invalid mode join");
    qa_mode_id id = v->id;
    if (!observer && !ghost_rejoin && v->value.rules.forced_team &&
        team != v->value.rules.forced_team)
        return mode_fail(e, "selected team is restricted by match rules");
    if (!observer && !ghost_rejoin && v->value.rules.match_lock &&
        (v->value.phase == QA_MODE_PLAYING || v->value.phase == QA_MODE_COUNTDOWN))
        return mode_fail(e, "match is locked");
    if (v->members[actor.slot].joined && !qa_modes_drop(m, id, actor, false, e))
        return false;
    mode_member *member = &v->members[actor.slot];
    mode_member before = *member;
    uint64_t binding_serial = v->bindings[actor.slot].serial;
    bool fresh = !member->joined || !qa_actor_id_equal(member->actor, actor);
    if (fresh)
        *member = (mode_member){.actor = actor, .joined = true, .extra_flags = 48};
    if (!qa_modes_set_team(m, id, actor, team, e)) {
        if (mode_member_get(m, v, actor) == member &&
            v->bindings[actor.slot].serial == binding_serial)
            *member = before;
        return false;
    }
    member = mode_member_get(m, v, actor);
    if (!member)
        return true;
    if (fresh && !mode_q3_session_initialize(m, v, actor, e))
        return false;
    member = mode_member_get(m, v, actor);
    if (!member)
        return true;
    member->player.spectator = observer;
    member->player.ready = false;
    if (observer)
        member->player.spectator_since_ns = v->value.time_ns;
    member->last_team = team;
    if (v->value.rules.source == QA_MODE_THREEWAVE)
        member->player.ctf_last_team = v->value.rules.start_map || observer ? 1
            : team == v->value.rules.teams[0] ? 5
            : team == v->value.rules.teams[1] ? 14 : -1;
    member->spawn_state = 0;
    if (m->options.hooks.spectator &&
        !MODE_CALLBACK(m, m->options.hooks.spectator(m->options.hooks.context, id, actor, observer, e)))
        return false;
    return qa_modes_rank(m, id, e);
}
bool qa_modes_join(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_team_id team, bool observer,
                   qa_error *e) {
    return mode_join(m, mode_get(m, id), actor, team, observer, false, e);
}
bool qa_modes_choose_team(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_team_id *out,
                          qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !out)
        return mode_fail(e, "invalid automatic team query");
    qa_actor_owner native_owner;
    if (v->value.rules.source >= QA_MODE_Q3 && m->options.hooks.q3_native_source &&
        m->options.hooks.q3_native_source(m->options.hooks.context, id, &native_owner)) {
        if (!m->options.hooks.q3_choose_team)
            return mode_fail(e, "native Q3 team choice has no source client policy");
        return MODE_CALLBACK(m, m->options.hooks.q3_choose_team(m->options.hooks.context,
            id, actor, out, e));
    }
    if (v->value.rules.forced_team) {
        *out = v->value.rules.forced_team;
        return true;
    }
    size_t counts[3] = {0};
    int64_t scores[3] = {0};
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        qa_actor_id other = m->players_order.ids[ordinal];
        mode_member *member = mode_member_get(m, v, other);
        mode_player *p = mode_player_get(m, other);
        if (!member || !p || member->player.spectator || qa_actor_id_equal(actor, other))
            continue;
        qa_team_id team;
        int32_t score;
        if (!qa_modes_team(m, id, other, &team, e) || !qa_modes_score(m, id, other, &score, e))
            return false;
        int index = mode_team_index(v, team);
        if (index >= 0) {
            ++counts[index];
            scores[index] += score;
        }
    }
    int index = counts[0] < counts[1]                      ? 0
                : counts[1] < counts[0]                    ? 1
                : v->value.rules.source == QA_MODE_LMCTF   ? (scores[0] > scores[1] ? 1 : 0)
                : v->value.rules.kind == QA_MODE_DEATHBALL ? 0
                : v->value.rules.source >= QA_MODE_Q3
                    ? (v->value.team_scores[0] > v->value.team_scores[1] ? 1 : 0)
                    : (int)(mode_random(m) & 1u);
    if (v->value.rules.source == QA_MODE_ROGUE && v->value.rules.teamplay == 6 &&
        counts[2] * 2 < counts[index])
        index = 2;
    *out = v->value.rules.teams[index];
    return true;
}
static bool frame(qa_modes *m, qa_mode_id id, uint64_t now, uint64_t elapsed,
                  bool native_q3_tail, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || now < v->value.time_ns)
        return mode_fail(e, "invalid mode time");
    v->value.time_ns = now;
    if (!v->value.rules.enabled)
        return true;
    if (!qa_builtin_players(&m->options.services, &m->players_order, e) ||
        !qa_builtin_observations(&m->options.services, &m->observations, e))
        return false;
    if (!mode_objects_frame(m, v, elapsed, e))
        return false;
    if (v->value.rules.source == QA_MODE_ROGUE && v->value.rules.teamplay >= 4 &&
        v->value.time_ns >= v->team_location_ns) {
        v->team_location_ns = v->value.time_ns + 120 * MODE_SECOND;
        int64_t totals[3];
        if (!qa_modes_team_totals(m, id, totals, e))
            return false;
        int order[] = {0, 1, 2}, count = v->value.rules.teamplay == 6 ? 3 : 2;
        for (int i = 1; i < count; ++i)
            for (int j = i; j > 0 && totals[order[j]] > totals[order[j - 1]]; --j) {
                int swap = order[j];
                order[j] = order[j - 1];
                order[j - 1] = swap;
            }
        int64_t difference = totals[order[0]] - totals[order[1]];
        if (!mode_event(m, v, QA_MODE_TEAM_STANDING, (qa_actor_id){0}, (qa_actor_id){0},
                        (qa_actor_id){0}, v->value.rules.teams[order[0]],
                        difference > INT32_MAX ? INT32_MAX : (int32_t)difference,
                        difference ? order[1] : order[1] | 4, e))
            return false;
    }
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *p = &v->members[i];
        if (!p->joined || !mode_player_get(m, p->actor))
            continue;
        if (!mode_relic_frame(m, v, p->actor, p, e))
            return false;
        if (p->respawn_ns && now >= p->respawn_ns) {
            p->respawn_ns = 0;
            if (m->options.hooks.respawn &&
                !m->options.hooks.respawn(m->options.hooks.context, id, p->actor, true, e))
                return false;
        }
    }
    if (native_q3_tail)
        return qa_modes_rank(m, id, e) && mode_update_ghosts(m, v, e);
    return mode_vote_frame(m, v, e) && mode_horde_frame(m, v, elapsed, e) &&
           mode_match_frame(m, v, elapsed, e) && mode_team_info_frame(m, v, e) &&
           mode_update_ghosts(m, v, e);
}
bool qa_modes_frame(qa_modes *m, qa_mode_id id, uint64_t now, uint64_t elapsed, qa_error *e) {
    if (!m || m->callback_depth)
        return mode_fail(e, "mode frame cannot reenter a synchronous callback");
    return MODE_CALLBACK(m, frame(m, id, now, elapsed, false, e));
}
bool qa_modes_q3_source_frame(qa_modes *m, qa_mode_id id, uint64_t now,
                             uint64_t elapsed, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || m->callback_depth || m->source_restored ||
        v->value.rules.source < QA_MODE_Q3 ||
        v->value.rules.source > QA_MODE_TEAM_ARENA ||
        v->value.rules.kind < QA_MODE_FFA || v->value.rules.kind > QA_MODE_HARVESTER)
        return mode_fail(e, "native Q3 effects require their idle selected rule mode");
    return MODE_CALLBACK(m, frame(m, id, now, elapsed, true, e));
}
bool qa_modes_actor_released(qa_modes *m, qa_actor_record released, qa_error *e) {
    if (!m || released.id.slot >= m->actor_capacity)
        return true;
    mode_object *o = &m->objects[released.id.slot];
    if (o->admitting && qa_actor_id_equal(o->actor, released.id))
        *o = (mode_object){0};
    if (o->active && qa_actor_id_equal(o->actor, released.id)) {
        mode_instance *v = mode_get(m, o->mode);
        mode_member *carrier = mode_member_get(m, v, o->value.carrier);
        if (carrier) {
            if (qa_actor_id_equal(carrier->flag, released.id))
                carrier->flag = (qa_actor_id){0};
            if (qa_actor_id_equal(carrier->relic, released.id))
                carrier->relic = (qa_actor_id){0};
        }
        if (v && mode_live(m, o->value.carrier) &&
            !mode_object_count(m, v, o, o->value.carrier, 0, e))
            return false;
        if (o->objective.serial && !qa_modes_unbind_objective(m, o->objective, e))
            return false;
        *o = (mode_object){0};
    }
    for (uint32_t i = 0; i < m->mode_capacity; ++i) {
        mode_instance *v = &m->instances[i];
        if (!v->active)
            continue;
        for (int j = 0; j < 3; ++j)
            if (qa_actor_id_equal(v->bases[j], released.id))
                v->bases[j] = (qa_actor_id){0};
        if (qa_actor_id_equal(v->ball, released.id))
            v->ball = (qa_actor_id){0};
        if (qa_actor_id_equal(v->tag, released.id))
            v->tag = (qa_actor_id){0};
        if (qa_actor_id_equal(v->tag_owner, released.id))
            v->tag_owner = (qa_actor_id){0};
        if (qa_actor_id_equal(v->rogue_spawn_spot, released.id))
            v->rogue_spawn_spot = (qa_actor_id){0};
        for (size_t j = 0; j < 4; ++j)
            if (qa_actor_id_equal(v->last_spawns[j], released.id))
                v->last_spawns[j] = (qa_actor_id){0};
    }
    mode_player *p = &m->players[released.id.slot];
    if (!p->active || !qa_actor_id_equal(p->value.actor, released.id))
        return true;
    for (uint32_t i = 0; i < m->mode_capacity; ++i) {
        mode_instance *v = &m->instances[i];
        if (!v->active)
            continue;
        for (size_t j = 0; j < m->players_order.count; ++j) {
            mode_member *other = mode_member_get(m, v, m->players_order.ids[j]);
            if (other && qa_actor_id_equal(other->player.follow_target, released.id))
                other->player.follow_target = (qa_actor_id){0};
        }
        mode_member *member = &v->members[released.id.slot];
        if (!member->joined || !qa_actor_id_equal(member->actor, released.id))
            continue;
        for (uint32_t j = 0; j < m->actor_capacity; ++j) {
            mode_object *held = &m->objects[j];
            if (held->active && held->mode.slot == v->id.slot &&
                held->mode.generation == v->id.generation &&
                qa_actor_id_equal(held->value.carrier, released.id)) {
                held->value.carrier = (qa_actor_id){0};
                if (held->q3_source_owned) continue;
                if (held->spec.kind == QA_MODE_OBJECT_FLAG) {
                    if (!mode_flag_reset(m, v, held, true, e))
                        return false;
                } else if (!mode_object_relocate(m, v, held, false, e))
                    return false;
            }
        }
        *member = (mode_member){0};
        v->bindings[released.id.slot] = (mode_match_owner){0};
    }
    *p = (mode_player){0};
    return true;
}
