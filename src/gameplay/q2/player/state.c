#include "internal.h"

static char *copy_text(const char *text) {
    if (!text)
        text = "";
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy)
        memcpy(copy, text, length);
    return copy;
}
void qa_q2_player_rules_default(qa_q2_player_rules *r) {
    if (r)
        *r = (qa_q2_player_rules){.password = "",
                                  .spectator_password = "",
                                  .spawn_point = "",
                                  .map_name = "",
                                  .start_items = "",
                                  .max_spectators = 4,
                                  .max_clients = 1,
                                  .coop_squad_respawn = true,
                                  .coop_instanced_items = true,
                                  .coop_num_lives = 2,
                                  .spawn_farthest = true,
                                  .autosave_minimum_seconds = 60,
                                  .flood_messages = 4,
                                  .flood_seconds = 4,
                                  .flood_wait_seconds = 10,
                                  .roll_speed = 200,
                                  .roll_angle = 2,
                                  .run_pitch = .002f,
                                  .run_roll = .005f,
                                  .bob_up = .005f,
                                  .bob_pitch = .002f,
                                  .bob_roll = .002f};
}
bool q2_players_init(qa_q2_game *g, qa_error *e) {
    g->player_runtime = calloc(1, sizeof(*g->player_runtime));
    if (!g->player_runtime) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 players");
        return false;
    }
    qa_q2_player_rules_default(&g->player_runtime->rules);
    return true;
}
void q2_players_close(qa_q2_game *g) {
    if (!g->player_runtime)
        return;
    for (size_t i = 0; i < 5; i++)
        free(g->player_runtime->rule_strings[i]);
    q2_player_list *list = g->player_runtime->lists;
    while (list) {
        q2_player_list *next = list->next;
        free(list->rows);
        free(list);
        list = next;
    }
    free(g->player_runtime);
    g->player_runtime = NULL;
}
void qa_q2_player_carry_free(qa_q2_player_carry *carry) {
    if (carry) {
        free(carry->inventory);
        *carry = (qa_q2_player_carry){0};
    }
}
void q2_client_release_state(q2_actor *a) {
    if (!a->client)
        return;
    qa_q2_player_carry_free(&a->client->coop);
    free(a->client->spawn_inventory);
    free(a->client->help_points);
    free(a->client);
    a->client = NULL;
}
bool qa_q2_players_configure(qa_q2_game *g, const qa_q2_player_rules *r,
                             const qa_q2_player_services *s, qa_error *e) {
    if (!g || !r || !s || !s->movement || !s->set_movement || !s->emit ||
        (!!s->score != !!s->score_read) || r->coop_num_lives < 0 ||
        !isfinite(r->roll_speed) || r->roll_speed <= 0 || !isfinite(r->force_respawn_seconds) ||
        r->force_respawn_seconds < 0 || !isfinite(r->flood_seconds) || r->flood_seconds < 0 ||
        !isfinite(r->flood_wait_seconds) || r->flood_wait_seconds < 0 || !isfinite(r->roll_angle) ||
        !isfinite(r->run_pitch) || !isfinite(r->run_roll) || !isfinite(r->bob_up) ||
        !isfinite(r->bob_pitch) || !isfinite(r->bob_roll) || !qa_vec_finite(r->gun_offset) ||
        !isfinite(r->autosave_minimum_seconds) || r->autosave_minimum_seconds < 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 player services or rules");
        return false;
    }
    const char *values[] = {r->password, r->spectator_password, r->spawn_point, r->map_name,
                            r->start_items};
    char *copies[5] = {0};
    for (size_t i = 0; i < 5; i++)
        if (!(copies[i] = copy_text(values[i]))) {
            for (size_t j = 0; j < 5; j++)
                free(copies[j]);
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Copying Q2 player rules");
            return false;
        }
    q2_players *p = g->player_runtime;
    for (size_t i = 0; i < 5; i++) {
        free(p->rule_strings[i]);
        p->rule_strings[i] = copies[i];
    }
    p->rules = *r;
    p->services = *s;
    p->rules.password = copies[0];
    p->rules.spectator_password = copies[1];
    p->rules.spawn_point = copies[2];
    p->rules.map_name = copies[3];
    p->rules.start_items = copies[4];
    return true;
}
q2_actor *q2_client(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_actor *a = g ? q2_actor_get(g, id, false, e) : NULL;
    if (!a || !a->client || !a->client->info.connected) {
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Q2 player is not admitted");
        return NULL;
    }
    return a;
}
bool q2_player_clear_powerups(qa_q2_game *g, q2_actor *a, qa_error *e) {
    if (!qa_q2_powerups_clear(g, a->id, e) ||
        !qa_combat_set_powered_armor(g->services.combat, a->id, &(qa_powered_armor){0}, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    qa_combat_state traits;
    if (!qa_combat_read_traits(g->services.combat, a->id, &traits, e))
        return false;
    traits.invulnerable = a->client->info.god;
    return qa_combat_set_traits(g->services.combat, a->id, &traits, e);
}
bool q2_player_emit(qa_q2_game *g, const qa_q2_player_event *event, qa_error *e) {
    qa_q2_player_services *s = &g->player_runtime->services;
    if (!s->emit) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 player event service is not configured");
        return false;
    }
    return s->emit(s->context, event, e);
}
bool q2_player_print(qa_q2_game *g, qa_actor_id id, int level, const char *text, qa_error *e) {
    return q2_player_emit(
        g,
        &(qa_q2_player_event){
            .kind = QA_Q2_PLAYER_PRINT, .actor = id, .level = level, .text = text},
        e);
}
bool q2_player_sound(qa_q2_game *g, qa_actor_id id, const char *path, int channel, qa_error *e) {
    qa_body_state body;
    qa_string_id sound;
    if (!qa_world_body_read(g->services.world, id, &body, e) ||
        !qa_builtin_resource(&g->services, path, &sound, e))
        return false;
    return qa_builtin_emit(&g->services,
                           &(qa_builtin_event){.kind = QA_BUILTIN_SOUND,
                                               .family = QA_GAME_Q2,
                                               .provider = g->options.owner,
                                               .actor = id,
                                               .resource = sound,
                                               .origin = body.origin,
                                               .channel = channel,
                                               .volume = 1,
                                               .attenuation = 1,
                                               .time_ns = g->now_ns},
                           e);
}
bool q2_player_observe(qa_q2_game *g, q2_actor *a, qa_q2_player_movement *m, qa_error *e) {
    qa_q2_player_services *s = &g->player_runtime->services;
    if (!s->movement) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 movement observation service is not configured");
        return false;
    }
    return s->movement(s->context, a->id, m, e);
}
bool q2_player_move(qa_q2_game *g, q2_actor *a, const qa_q2_player_motion *change, qa_error *e) {
    qa_q2_player_services *s = &g->player_runtime->services;
    if (!s->set_movement) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 movement control service is not configured");
        return false;
    }
    return s->set_movement(s->context, a->id, change, e);
}
bool q2_player_inventory_copy(qa_q2_game *g, qa_actor_id id, qa_inventory_entry **out,
                              size_t *count, qa_error *e) {
    size_t n = 0;
    if (!qa_inventory_entries(g->services.inventory, id, NULL, 0, &n, e))
        return false;
    qa_inventory_entry *entries = n ? malloc(n * sizeof(*entries)) : NULL;
    if (n && !entries) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Copying Q2 persistent inventory");
        return false;
    }
    if (!qa_inventory_entries(g->services.inventory, id, entries, n, &n, e)) {
        free(entries);
        return false;
    }
    *out = entries;
    *count = n;
    return true;
}
bool q2_player_inventory_set(qa_q2_game *g, qa_actor_id id, const qa_inventory_entry *entries,
                             size_t count, qa_error *e) {
    qa_inventory_entry *old = NULL;
    size_t n = 0;
    if (!q2_player_inventory_copy(g, id, &old, &n, e))
        return false;
    for (size_t i = 0; i < n && q2_actor_live(g, id); i++) {
        old[i].count = 0;
        if (!qa_inventory_configure(g->services.inventory, id, &old[i], NULL, NULL, e)) {
            free(old);
            return false;
        }
    }
    free(old);
    for (size_t i = 0; i < count && q2_actor_live(g, id); i++)
        if (!qa_inventory_configure(g->services.inventory, id, &entries[i], NULL, NULL, e))
            return false;
    return true;
}
bool qa_q2_player_carry_capture(qa_q2_game *g, qa_actor_id id, qa_q2_player_carry *out,
                                qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    qa_combat_state combat;
    if (!a || !out || !qa_combat_read(g->services.combat, id, &combat, e))
        return false;
    q2_power_state *powers = q2_powers(g, id, e);
    if (!powers)
        return false;
    qa_q2_player_carry c = {.health = combat.health,
                            .maximum_health = powers->maximum_health,
                            .armor = combat.armor,
                            .weapon = a->weapon_bound ? a->weapon.weapon : QA_Q2_WEAPON_NONE,
                            .selected_item = a->client->info.selected_item,
                            .score = a->client->info.score,
                            .power_cubes = powers->power_cubes,
                            .flags = (a->client->info.god ? 16u : 0) |
                                     (a->client->info.notarget ? 32u : 0) |
                                     (combat.armor.powered.kind != QA_POWER_NONE ? 4096u : 0) |
                                     (a->client->info.flashlight ? 0x400000u : 0) |
                                     (a->client->auto_shield_enabled ? 0x40000000u : 0)};
    if (!q2_player_inventory_copy(g, id, &c.inventory, &c.count, e))
        return false;
    *out = c;
    return true;
}
bool qa_q2_player_carry_restore(qa_q2_game *g, qa_actor_id id, const qa_q2_player_carry *c,
                                qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    if (!a || !c || (c->count && !c->inventory) || !isfinite(c->health) ||
        !isfinite(c->maximum_health) || c->maximum_health <= 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 player carry");
        return false;
    }
    q2_power_state *powers = q2_powers(g, id, e);
    if (!powers)
        return false;
    if (!q2_player_inventory_set(g, id, c->inventory, c->count, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (!qa_combat_set_health(g->services.combat, id, c->health, e) ||
        !qa_combat_set_armor(g->services.combat, id, &c->armor, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    powers->maximum_health = c->maximum_health;
    powers->power_cubes = c->power_cubes;
    q2_client_state *s = a->client;
    s->info.score = c->score;
    s->info.selected_item = c->selected_item;
    s->info.god = (c->flags & 16) != 0;
    s->info.notarget = (c->flags & 32) != 0;
    s->info.flashlight = (c->flags & 0x400000) != 0;
    s->auto_shield_enabled = (c->flags & 0x40000000u) != 0;
    if (a->weapon_bound) {
        a->weapon.weapon = c->weapon;
        a->weapon.pending = QA_Q2_WEAPON_NONE;
    }
    qa_combat_state traits;
    if (!qa_combat_read_traits(g->services.combat, id, &traits, e))
        return false;
    traits.invulnerable = s->info.god;
    return qa_combat_set_traits(g->services.combat, id, &traits, e);
}
bool qa_q2_player_read(qa_q2_game *g, qa_actor_id id, qa_q2_player_info *out) {
    q2_actor *a = g ? q2_actor_get(g, id, false, NULL) : NULL;
    if (!a || !a->client || !out)
        return false;
    *out = a->client->info;
    return true;
}
bool qa_q2_player_projection(qa_q2_game *g, qa_actor_id id, qa_builtin_player_info *out) {
    q2_actor *a = g ? q2_actor_get(g, id, false, NULL) : NULL;
    if (!a || !a->client || !a->client->info.connected || !out)
        return false;
    q2_client_state *s = a->client;
    *out = (qa_builtin_player_info){.name = s->info.name,
                                   .skin = s->info.skin,
                                   .slot = s->info.slot,
                                   .ping = s->info.ping,
                                   .entered_ns = s->entered_ns,
                                   .view_height = s->info.view_height,
                                   .killer_yaw = s->killer_yaw,
                                   .connected = true,
                                   .spectator = s->info.spectator,
                                   .dead = s->info.dead};
    return true;
}
bool q2_player_info(qa_q2_game *g, qa_actor_id id, qa_builtin_player_info *out) {
    if (!q2_actor_live(g, id))
        return false;
    if (g->services.player_info)
        return g->services.player_info(g->services.context, id, out) && out->connected;
    return qa_q2_player_projection(g, id, out);
}
bool q2_player_score_read(qa_q2_game *g, qa_actor_id id, int32_t *out, qa_error *e) {
    qa_q2_player_services *services = &g->player_runtime->services;
    if (services->score_read)
        return services->score_read(services->context, id, out, e);
    q2_actor *a = q2_client(g, id, e);
    if (!a)
        return false;
    *out = a->client->info.score;
    return true;
}
q2_player_list *q2_player_list_acquire(qa_q2_game *g, bool scores, qa_error *e) {
    q2_player_list *list = g->player_runtime->lists;
    while (list && list->active)
        list = list->next;
    if (!list) {
        list = calloc(1, sizeof(*list));
        if (!list) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating nested Q2 player listing");
            return NULL;
        }
        list->next = g->player_runtime->lists;
        g->player_runtime->lists = list;
    }
    list->active = true;
    list->count = 0;
    q2_trace_frame *roster = q2_player_roster(g, e);
    if (!roster)
        goto fail;
    if (list->capacity < roster->snapshot.count) {
        size_t capacity = roster->snapshot.count;
        if (capacity > SIZE_MAX / sizeof(*list->rows)) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Q2 player listing is too large");
            goto release;
        }
        q2_player_row *rows = realloc(list->rows, capacity * sizeof(*rows));
        if (!rows) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Growing Q2 player listing");
            goto release;
        }
        list->rows = rows;
        list->capacity = capacity;
    }
    for (size_t i = 0; i < roster->snapshot.count; ++i) {
        q2_player_row row = {.actor = roster->snapshot.ids[i]};
        if (!q2_player_info(g, row.actor, &row.info))
            continue;
        if (scores && !q2_player_score_read(g, row.actor, &row.score, e))
            goto release;
        list->rows[list->count++] = row;
    }
    roster->active = false;
    return list;
release:
    roster->active = false;
fail:
    list->active = false;
    return NULL;
}
bool qa_q2_player_score(qa_q2_game *g, qa_actor_id id, int score, int ping, qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    if (!a)
        return false;
    a->client->info.score = score;
    a->client->info.ping = ping;
    return true;
}
bool q2_client_slot(qa_q2_game *g, qa_actor_id id, uint32_t *slot) {
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (!a || !a->client || !a->client->info.connected)
        return false;
    *slot = a->client->info.slot;
    return true;
}
bool q2_client_traits(qa_q2_game *g, qa_actor_id id, qa_builtin_actor_traits *out) {
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (!a || !a->client)
        return false;
    q2_client_state *s = a->client;
    if (!s->info.connected && !s->corpse)
        return false;
    const qa_actor_record *r = qa_actors_get(qa_session_actors(g->services.session), id);
    *out = (qa_builtin_actor_traits){
        .classname = r ? r->definition : 0,
        .player = !s->corpse,
        .spectator = s->info.spectator,
        .no_target = s->info.notarget,
        .invisible = a->powers && a->powers->values.invisibility_until_ns > g->now_ns,
        .view_height = s->info.view_height,
        .gib_health = -40,
        .max_health = a->powers ? a->powers->maximum_health : 100};
    return true;
}
bool qa_q2_player_consumed_key(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    if (!a)
        return false;
    q2_client_state *s = a->client;
    if (!s->has_coop)
        return true;
    for (size_t i = 0; i < s->coop.count; i++) {
        const qa_q2_item_definition *d = q2_item_by_id(g, s->coop.inventory[i].item);
        if (d && d->kind == QA_Q2_ITEM_KEY &&
            !qa_inventory_entry_read(g->services.inventory, id, d->item, &s->coop.inventory[i], e))
            return false;
    }
    if (a->powers)
        s->coop.power_cubes = a->powers->power_cubes;
    return true;
}
