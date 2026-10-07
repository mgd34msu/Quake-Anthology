#include "internal.h"

bool qa_q2_player_cheats_allowed(const qa_q2_game *game) {
    const q2_players *players = game ? game->player_runtime : NULL;
    return players && (!(game->options.edition == QA_Q2_RERELEASE
                            ? players->rules.max_clients > 1 : game->options.deathmatch) ||
                       players->rules.cheats);
}

static bool equal_name(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++))
            return false;
    }
    return *a == *b;
}
static void join_arguments(size_t count, const char *const *args, char *out, size_t size) {
    size_t length = 0;
    for (size_t i = 0; i < count && length + 1 < size; i++) {
        if (i)
            out[length++] = ' ';
        size_t n = strlen(args[i]);
        if (n > size - 1 - length)
            n = size - 1 - length;
        memcpy(out + length, args[i], n);
        length += n;
    }
    out[length] = 0;
}
static int argument_integer(const char *text) {
    char *end;
    long value = strtol(text, &end, 10);
    return end == text ? 0 : value > INT_MAX ? INT_MAX : value < INT_MIN ? INT_MIN : (int)value;
}
static bool usable(const qa_q2_item_definition *d) {
    return d->weapon || d->kind == QA_Q2_ITEM_POWER || d->kind == QA_Q2_ITEM_POWER_ARMOR ||
           d->kind == QA_Q2_ITEM_SPHERE || d->kind == QA_Q2_ITEM_DECOY ||
           d->kind == QA_Q2_ITEM_NUKE || d->kind == QA_Q2_ITEM_COMPASS ||
           d->kind == QA_Q2_ITEM_FLASHLIGHT;
}
bool q2_player_publish_inventory(qa_q2_game *g, q2_actor *a, qa_error *e) {
    qa_inventory_entry *entries = NULL;
    size_t count = 0;
    if (!q2_player_inventory_copy(g, a->id, &entries, &count, e))
        return false;
    bool ok = q2_player_emit(g,
                             &(qa_q2_player_event){.kind = QA_Q2_PLAYER_INVENTORY,
                                                   .actor = a->id,
                                                   .inventory = entries,
                                                   .count = count,
                                                   .visible = a->client->show_inventory,
                                                   .selected_item = a->client->info.selected_item},
                             e);
    free(entries);
    return ok;
}
bool q2_player_scoreboard(qa_q2_game *g, q2_actor *a, bool reliable, qa_error *e) {
    q2_player_list *list = q2_player_list_acquire(g, true, e);
    if (!list)
        return false;
    qa_q2_score_row rows[12];
    size_t count = 0;
    for (size_t i = 0; i < list->count; i++) {
        const q2_player_row *other = &list->rows[i];
        const qa_builtin_player_info *s = &other->info;
        if (s->spectator)
            continue;
        uint64_t elapsed = g->now_ns > s->entered_ns ? g->now_ns - s->entered_ns : 0;
        qa_q2_score_row row = {
            .slot = s->slot,
            .name = s->name,
            .score = other->score,
            .ping = s->ping > 999 ? 999 : s->ping,
            .minutes = (int)fmin((double)(elapsed / (60 * Q2_NS)), INT_MAX)};
        size_t index = 0;
        while (index < count && (rows[index].score > row.score ||
                                 (rows[index].score == row.score && rows[index].slot < row.slot)))
            index++;
        if (index >= 12)
            continue;
        if (count < 12)
            count++;
        memmove(rows + index + 1, rows + index, (count - index - 1) * sizeof(*rows));
        rows[index] = row;
    }
    bool okay = q2_player_emit(g,
                          &(qa_q2_player_event){.kind = QA_Q2_PLAYER_SCOREBOARD,
                                                .actor = a->id,
                                                .scores = rows,
                                                .count = count,
                                                .reliable = reliable},
                          e);
    list->active = false;
    return okay;
}
static bool select_item(qa_q2_game *g, q2_actor *a, int direction, int filter, qa_error *e) {
    size_t count = qa_q2_item_count(g), start = count - 1;
    for (size_t i = 0; i < count; i++)
        if (qa_q2_item_at(g, i)->item == a->client->info.selected_item) {
            start = i;
            break;
        }
    for (size_t step = 1; step <= count; step++) {
        size_t index =
            direction > 0 ? (start + step) % count : (start + count - (step % count)) % count;
        const qa_q2_item_definition *d = qa_q2_item_at(g, index);
        if (!usable(d) || (filter == 1 && d->kind != QA_Q2_ITEM_WEAPON) ||
            (filter == 2 && d->kind != QA_Q2_ITEM_POWER))
            continue;
        int owned;
        if (!q2_count(g, a->id, d->item, &owned, e))
            return false;
        if (owned > 0) {
            a->client->info.selected_item = d->item;
            if (g->options.edition == QA_Q2_RERELEASE) {
                if (!qa_builtin_resource(&g->services, d->name, &a->selected_item_name, e))
                    return false;
                a->selected_item_name_until_ns = q2_deadline(g->now_ns, 3 * Q2_NS);
            }
            return true;
        }
    }
    a->client->info.selected_item = 0;
    a->selected_item_name = 0;
    a->selected_item_name_until_ns = 0;
    return true;
}
static bool use_item(qa_q2_game *g, q2_actor *a, const qa_q2_item_definition *d, qa_error *e) {
    if (!d)
        return q2_player_print(g, a->id, 2, "unknown item\n", e);
    if (!usable(d))
        return q2_player_print(g, a->id, 2, "Item is not usable.\n", e);
    int owned;
    if (!q2_count(g, a->id, d->item, &owned, e))
        return false;
    if (!owned) {
        char text[256];
        snprintf(text, sizeof(text), "Out of item: %s\n", d->name);
        return q2_player_print(g, a->id, 2, text, e);
    }
    if (!qa_inventory_item_action(g->services.inventory, a->id, d->item, QA_ITEM_USE, e))
        return false;
    if (q2_actor_live(g, a->id) && a->client)
        a->client->info.selected_item = d->item;
    return true;
}
static bool supplemental_action(qa_q2_game *g, q2_actor *a, const char *name, qa_item_action action,
                                bool *handled, qa_error *e) {
    qa_q2_supplemental_item item;
    *handled = q2_supplemental_find(g, name, false, &item);
    if (!*handled)
        return true;
    if (!(item.definition.actions & (uint32_t)action))
        return q2_player_print(
            g, a->id, 2,
            action == QA_ITEM_USE ? "Item is not usable.\n" : "Item is not dropable.\n", e);
    int count;
    if (!q2_count(g, a->id, item.definition.item, &count, e))
        return false;
    return count ? qa_inventory_item_action(g->services.inventory, a->id, item.definition.item,
                                            action, e)
                 : q2_player_print(g, a->id, 2, "Out of item.\n", e);
}
static bool drop_item(qa_q2_game *g, q2_actor *a, const qa_q2_item_definition *d, qa_error *e) {
    if (!d)
        return q2_player_print(g, a->id, 2, "unknown item\n", e);
    if (!d->droppable || (g->options.cooperative && d->coop_stay &&
                          !(g->options.edition == QA_Q2_RERELEASE &&
                            (g->player_runtime->rules.coop_instanced_items ||
                             g->player_runtime->rules.coop_squad_respawn))))
        return q2_player_print(g, a->id, 2, "Item is not dropable.\n", e);
    int owned;
    if (!q2_count(g, a->id, d->item, &owned, e))
        return false;
    if (!owned)
        return q2_player_print(g, a->id, 2, "Out of item.\n", e);
    return qa_inventory_item_action(g->services.inventory, a->id, d->item, QA_ITEM_DROP, e);
}
static bool item_command(qa_q2_game *g, q2_actor *a, const char *text, bool using, qa_error *e) {
    const qa_q2_item_definition *item = qa_q2_item_lookup(g, text);
    if (!item) {
        bool supplied;
        if (!supplemental_action(g, a, text, using ? QA_ITEM_USE : QA_ITEM_DROP, &supplied, e))
            return false;
        if (supplied || !q2_actor_live(g, a->id))
            return true;
    }
    return using ? use_item(g, a, item, e) : drop_item(g, a, item, e);
}
static bool flood_allowed(qa_q2_game *g, q2_actor *a, bool *allowed, qa_error *e) {
    q2_client_state *s = a->client;
    qa_q2_player_rules *r = &g->player_runtime->rules;
    *allowed = true;
    if (!r->flood_messages)
        return true;
    char text[128];
    if (g->now_ns < s->flood_until_ns) {
        *allowed = false;
        snprintf(text, sizeof(text), "You can't talk for %d more seconds\n",
                 (int)q2_seconds_left(s->flood_until_ns, g->now_ns));
        return q2_player_print(g, a->id, 2, text, e);
    }
    size_t messages = r->flood_messages > 10 ? 10 : r->flood_messages;
    size_t index = s->flood_count > messages ? s->flood_count - messages : 0;
    if (s->flood_count >= r->flood_messages &&
        g->now_ns - s->flood_times[index] < q2_item_seconds(r->flood_seconds)) {
        s->flood_until_ns = q2_deadline(g->now_ns, q2_item_seconds(r->flood_wait_seconds));
        *allowed = false;
        snprintf(text, sizeof(text), "Flood protection:  You can't talk for %d seconds.\n",
                 (int)r->flood_wait_seconds);
        return q2_player_print(g, a->id, 2, text, e);
    }
    if (s->flood_count == 10) {
        memmove(s->flood_times, s->flood_times + 1, 9 * sizeof(*s->flood_times));
        s->flood_count--;
    }
    s->flood_times[s->flood_count++] = g->now_ns;
    return true;
}
static void team_name(const char *skin, bool model, char *out, size_t capacity) {
    const char *slash = strchr(skin, '/');
    if (!slash)
        snprintf(out, capacity, "%s", skin);
    else if (model)
        snprintf(out, capacity, "%.*s", (int)(slash - skin), skin);
    else
        snprintf(out, capacity, "%s", slash + 1);
}
static bool say(qa_q2_game *g, q2_actor *a, const char *text, bool team, qa_error *e) {
    if (!*text)
        return true;
    bool allowed;
    if (!flood_allowed(g, a, &allowed, e))
        return false;
    if (!allowed)
        return true;
    if (!q2_actor_live(g, a->id))
        return true;
    team = team && (g->options.deathmatch_flags & (64 | 128));
    char message[153], own_team[256];
    if (*text == '"')
        text++;
    size_t n = strlen(text);
    if (n && text[n - 1] == '"')
        n--;
    snprintf(message, sizeof(message), team ? "(%s): %.*s" : "%s: %.*s", a->client->info.name,
             (int)fmin((double)n, 150), text);
    size_t length = strlen(message);
    if (length > 150)
        length = 150;
    message[length++] = '\n';
    message[length] = 0;
    team_name(a->client->info.skin, (g->options.deathmatch_flags & 64) != 0, own_team,
              sizeof(own_team));
    qa_builtin_snapshot_frame *players = q2_player_roster(g, e);
    if (!players)
        return false;
    bool okay = true;
    for (size_t i = 0; i < players->snapshot.count; i++) {
        qa_actor_id other = players->snapshot.ids[i];
        qa_builtin_player_info info;
        if (!q2_player_info(g, other, &info))
            continue;
        char recipient[256];
        team_name(info.skin, (g->options.deathmatch_flags & 64) != 0, recipient,
                  sizeof(recipient));
        if (team && strcmp(own_team, recipient))
            continue;
        if (!q2_player_print(g, other, 3, message, e)) {
            okay = false;
            break;
        }
    }
    qa_builtin_snapshot_release(players);
    return okay;
}
static bool write_count(qa_q2_game *g, qa_actor_id actor, qa_item_id item, double capacity,
                        double count, qa_error *e) {
    qa_inventory_entry entry;
    qa_error missing = {0};
    if (!qa_inventory_entry_read(g->services.inventory, actor, item, &entry, &missing)) {
        if (missing.code != QA_ERROR_NOT_FOUND) {
            if (e)
                *e = missing;
            return false;
        }
        entry = (qa_inventory_entry){.item = item, .capacity = capacity};
    }
    entry.count = count;
    entry.policy = QA_COUNT_SOURCE_INT32;
    return qa_inventory_configure(g->services.inventory, actor, &entry, NULL, NULL, e);
}
static bool give_lookup(qa_q2_game *g, const char *requested, const char *first,
                        const qa_q2_item_definition **native, qa_q2_supplemental_item *extra) {
    *native = NULL;
    for (unsigned pass = 0; pass < (g->options.edition == QA_Q2_RERELEASE ? 3u : 2u); ++pass) {
        const char *text = pass == 0 ? requested : first;
        if (!text)
            continue;
        for (size_t i = 0; i < qa_q2_item_count(g); ++i) {
            const qa_q2_item_definition *d = qa_q2_item_at(g, i);
            const char *id = qa_strings_cstr(qa_session_strings(g->services.session), d->item);
            if (pass < 2 ? equal_name(text, d->name)
                         : equal_name(text, d->classname) || (id && equal_name(text, id))) {
                *native = d;
                return true;
            }
        }
        if (q2_supplemental_find(g, text, pass < 2, extra))
            return true;
    }
    return false;
}
static bool give_ammo(qa_q2_game *g, qa_actor_id actor, const qa_q2_item_definition *d, bool exact,
                      int amount, qa_error *e) {
    qa_supply *supply;
    if (!q2_item_supply(g, actor, &supply, e)) return false;
    if (!supply || !qa_supply_maps(supply, d->item, false)) {
        int previous;
        if (!q2_count(g, actor, d->item, &previous, e))
            return false;
        return write_count(g, actor, d->item, d->capacity,
                           exact ? amount : (double)previous + d->quantity, e);
    }
    qa_pickup_grant grant = {.item = d->item};
    qa_supply_preview_result preview = {0};
    if (!qa_supply_preview(
            supply, actor,
            &(qa_supply_offer){.kind = QA_SUPPLY_AMMO, .ammo = &grant, .ammo_count = 1}, false,
            &preview, e))
        return false;
    bool okay = true;
    for (size_t i = 0; i < preview.ammo_count && q2_actor_live(g, actor); ++i) {
        qa_inventory_entry entry;
        if (!qa_inventory_entry_read(g->services.inventory, actor, preview.ammo[i].item, &entry,
                                     e)) {
            okay = false;
            break;
        }
        double next = exact ? amount : entry.count + d->quantity;
        entry.count = entry.policy == QA_COUNT_STACK ? fmax(0, next) : next;
        if (!qa_inventory_configure(g->services.inventory, actor, &entry, NULL, NULL, e)) {
            okay = false;
            break;
        }
    }
    qa_supply_preview_free(&preview);
    return okay;
}
static bool power_after_give(qa_q2_game *g, q2_actor *a, qa_error *e) {
    if (!a->client)
        return true;
    const qa_q2_item_definition *cells = qa_q2_item_lookup(g, "ammo_cells"),
                                *shield = qa_q2_item_lookup(g, "item_power_shield"),
                                *screen = qa_q2_item_lookup(g, "item_power_screen");
    int fuel, owned;
    qa_combat_state combat;
    if (!q2_count(g, a->id, cells->item, &fuel, e) ||
        !q2_count(g, a->id, shield->item, &owned, e) ||
        !qa_combat_read(g->services.combat, a->id, &combat, e))
        return false;
    int automatic = a->client->auto_shield;
    bool enough =
             fuel != 0 && (automatic < 0 || (a->client->auto_shield_enabled && fuel > automatic)),
         active = combat.armor.powered.kind != QA_POWER_NONE;
    bool used;
    return !((active && !enough) || (!active && automatic != -1 && enough)) ||
           qa_q2_item_use(g, a->id, owned ? shield->item : screen->item, &used, e);
}
static bool grant_arsenal(qa_q2_game *g, qa_actor_id actor, bool ammo, qa_error *e) {
    for (size_t i = 0; i < qa_q2_item_count(g) && q2_actor_live(g, actor); ++i) {
        const qa_q2_item_definition *d = qa_q2_item_at(g, i);
        if (ammo ? d->kind != QA_Q2_ITEM_AMMO
                 : !d->weapon || d->console_give == QA_Q2_GIVE_INVENTORY_ONLY)
            continue;
        if (!q2_item_ensure(g, actor, d, e))
            return false;
        qa_inventory_entry entry;
        if (!qa_inventory_entry_read(g->services.inventory, actor, d->item, &entry, e))
            return false;
        entry.count = ammo ? fmin(entry.count + 1000, entry.capacity) : entry.count + 1;
        if (!qa_inventory_configure(g->services.inventory, actor, &entry, NULL, NULL, e))
            return false;
    }
    return true;
}
typedef struct arsenal_grant_call { qa_q2_game *game; bool ammo; } arsenal_grant_call;
static bool arsenal_grant(void *context, qa_actor_id actor, qa_error *error) {
    arsenal_grant_call *call = context;
    return grant_arsenal(call->game, actor, call->ammo, error);
}
bool qa_q2_game_grant_arsenal(qa_q2_game *g, qa_actor_id actor, bool ammo, qa_error *error) {
    if (!g || !q2_actor_get(g, actor, false, NULL)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 arsenal grant requires a native actor");
        return false;
    }
    arsenal_grant_call call = {g, ammo};
    return qa_q2_run_actor(g, actor, arsenal_grant, &call, error);
}
static bool give(qa_q2_game *g, q2_actor *a, size_t count, const char *const *args,
                 bool selected, qa_error *e) {
    char requested[1024];
    join_arguments(count, args, requested, sizeof(requested));
    bool all = equal_name(requested, "all"), rr = g->options.edition == QA_Q2_RERELEASE;
    qa_q2_player_services *services = &g->player_runtime->services;
    if (all || (count && equal_name(args[0], "health"))) {
        float health = count == 2  ? (float)argument_integer(args[1])
                       : a->powers ? a->powers->maximum_health
                                   : 100;
        if (!qa_combat_set_health(g->services.combat, a->id, health, e))
            return false;
        if (!all || !q2_actor_live(g, a->id))
            return true;
    }
    for (int category = 0; category < 2; category++)
        if (all || equal_name(requested, category ? "ammo" : "weapons")) {
            bool handled = false;
            if (selected && services->grant_arsenal &&
                !services->grant_arsenal(services->context, a->id, category != 0, &handled, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            if (!handled) {
                if (category && all && rr) {
                    if (!q2_item_console_pickup(g, a->id, qa_q2_item_lookup(g, "item_pack"), e))
                        return false;
                }
                if (!grant_arsenal(g, a->id, category != 0, e))
                    return false;
            }
            if (!all || !q2_actor_live(g, a->id))
                return true;
        }
    if (all || equal_name(requested, "armor")) {
        const qa_q2_item_definition *d = qa_q2_item_lookup(g, "item_armor_body");
        qa_regular_armor armor = {.kind = QA_ARMOR_Q2,
                                  .item = d ? d->item : 0,
                                  .points = 200,
                                  .protection.q2 = {.8f, .6f}};
        if (!qa_combat_set_regular_armor(g->services.combat, a->id, &armor, e))
            return false;
        if (!all || !q2_actor_live(g, a->id))
            return true;
    }
    if (all || (!rr && equal_name(requested, "power shield"))) {
        if (!q2_item_console_pickup(g, a->id, qa_q2_item_lookup(g, "item_power_shield"), e))
            return false;
        if (!all)
            return true;
    }
    if (all) {
        for (size_t i = 0; i < qa_q2_item_count(g) && q2_actor_live(g, a->id); i++) {
            const qa_q2_item_definition *d = qa_q2_item_at(g, i);
            if (d->weapon || d->kind == QA_Q2_ITEM_AMMO || d->kind == QA_Q2_ITEM_ARMOR ||
                d->kind == QA_Q2_ITEM_SHARD || d->console_give == QA_Q2_GIVE_INVENTORY_ONLY)
                continue;
            if (rr &&
                (d->kind == QA_Q2_ITEM_HEALTH ||
                 (d->kind == QA_Q2_ITEM_MAX_HEALTH && strcmp(d->classname, "item_adrenaline")) ||
                 d->console_give == QA_Q2_GIVE_FORBIDDEN ||
                 d->console_give == QA_Q2_GIVE_INDIVIDUAL_ONLY))
                continue;
            if (!write_count(g, a->id, d->item, d->capacity,
                             rr && d->kind == QA_Q2_ITEM_KEY ? 8 : 1, e))
                return false;
        }
        qa_q2_item_options *options = &g->item_runtime->options;
        size_t extra_count =
            options->supplemental_count ? options->supplemental_count(options->context) : 0;
        for (size_t i = 0; i < extra_count && q2_actor_live(g, a->id); ++i) {
            qa_q2_supplemental_item item;
            if (!options->supplemental_item(options->context, i, &item) || item.definition.weapon ||
                item.console_give == QA_Q2_GIVE_INVENTORY_ONLY ||
                (rr && (item.console_give == QA_Q2_GIVE_INDIVIDUAL_ONLY ||
                        item.console_give == QA_Q2_GIVE_FORBIDDEN)))
                continue;
            if (!write_count(g, a->id, item.definition.item, item.capacity, 1, e))
                return false;
        }
        if (rr && a->powers)
            a->powers->power_cubes = 0xff;
        return !rr || !q2_actor_live(g, a->id) || power_after_give(g, a, e);
    }
    const qa_q2_item_definition *d;
    qa_q2_supplemental_item extra;
    if (!give_lookup(g, requested, count ? args[0] : NULL, &d, &extra)) {
        bool handled = false;
        if (selected && services->give_item &&
            !services->give_item(services->context, a->id, count, args, &handled, e))
            return false;
        return handled || q2_player_print(g, a->id, 2, "unknown item\n", e);
    }
    qa_q2_console_give policy = d ? d->console_give : extra.console_give;
    if (rr && policy == QA_Q2_GIVE_FORBIDDEN)
        return q2_player_print(g, a->id, 2, "Item is not giveable.\n", e);
    if (!d) {
        if (policy == QA_Q2_GIVE_INVENTORY_ONLY)
            return rr ? write_count(g, a->id, extra.definition.item, extra.capacity, 1, e)
                      : q2_player_print(g, a->id, 2, "non-pickup item\n", e);
        bool accepted;
        qa_q2_item_options *options = &g->item_runtime->options;
        return options->supplemental_give(options->context, a->id, extra.definition.item, false, 0,
                                          &accepted, e);
    }
    qa_supply *supply;
    if (!q2_item_supply(g, a->id, &supply, e)) return false;
    if ((d->weapon || d->kind == QA_Q2_ITEM_AMMO) &&
        (!supply || !qa_supply_maps(supply, d->item, d->kind != QA_Q2_ITEM_AMMO)) &&
        selected && services->give_item) {
        const char *selected_args[2] = {
            qa_strings_cstr(qa_session_strings(g->services.session), d->item),
            count == 2 ? args[1] : NULL};
        bool handled = false;
        if (!services->give_item(services->context, a->id,
                                 d->kind == QA_Q2_ITEM_AMMO && count == 2 ? 2 : 1, selected_args,
                                 &handled, e))
            return false;
        if (handled || !q2_actor_live(g, a->id))
            return true;
    }
    if (d->console_give == QA_Q2_GIVE_INVENTORY_ONLY) {
        if (!rr)
            return q2_player_print(g, a->id, 2, "non-pickup item\n", e);
        return write_count(g, a->id, d->item, d->capacity, 1, e);
    }
    if (d->kind == QA_Q2_ITEM_AMMO)
        return give_ammo(g, a->id, d, count == 2, count == 2 ? argument_integer(args[1]) : 0, e);
    return q2_item_console_pickup(g, a->id, d, e);
}
typedef struct item_give_call {
    qa_q2_game *game;
    size_t count;
    const char *const *args;
    bool *handled;
} item_give_call;
static bool item_give(void *context, qa_actor_id actor, qa_error *error) {
    item_give_call *call = context;
    char requested[1024];
    join_arguments(call->count, call->args, requested, sizeof(requested));
    const qa_q2_item_definition *definition;
    qa_q2_supplemental_item extra;
    if (!give_lookup(call->game, requested, call->count ? call->args[0] : NULL,
                     &definition, &extra))
        return true;
    q2_actor *source = q2_actor_get(call->game, actor, false, NULL);
    if (!source)
        return true;
    *call->handled = true;
    return give(call->game, source, call->count, call->args, false, error);
}
bool qa_q2_game_give_item(qa_q2_game *g, qa_actor_id actor, size_t count,
                          const char *const *args, bool *handled, qa_error *error) {
    if (!g || !handled || (count && !args)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 source item grant");
        return false;
    }
    *handled = false;
    for (size_t i = 0; i < count; ++i)
        if (!args[i]) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 source item name");
            return false;
        }
    item_give_call call = {g, count, args, handled};
    return qa_q2_run_actor(g, actor, item_give, &call, error);
}
static bool players_list(qa_q2_game *g, q2_actor *a, bool scores, qa_error *e) {
    q2_player_list *list = q2_player_list_acquire(g, true, e);
    if (!list)
        return false;
    q2_player_row *ordered = list->rows;
    size_t count = list->count;
    for (size_t i = 1; i < count; i++) {
        q2_player_row row = ordered[i];
        size_t at = i;
        while (at && (scores ? ordered[at - 1].score > row.score
                             : ordered[at - 1].info.slot > row.info.slot)) {
            ordered[at] = ordered[at - 1];
            at--;
        }
        ordered[at] = row;
    }
    char text[1400];
    size_t used = 0;
    for (size_t i = 0; i < count; i++) {
        const qa_builtin_player_info *s = &ordered[i].info;
        char line[128];
        uint64_t seconds = g->now_ns > s->entered_ns ? (g->now_ns - s->entered_ns) / Q2_NS : 0;
        if (scores)
            snprintf(line, sizeof(line), "%3d %s\n", ordered[i].score, s->name);
        else
            snprintf(line, sizeof(line), "%02llu:%02llu %4d %3d %s%s\n",
                     (unsigned long long)(seconds / 60), (unsigned long long)(seconds % 60),
                     s->ping, ordered[i].score, s->name, s->spectator ? " (spectator)" : "");
        size_t length = strlen(line);
        if (used + length > 1280) {
            memcpy(text + used, "...\n", 4);
            used += 4;
            break;
        }
        memcpy(text + used, line, length);
        used += length;
    }
    text[used] = 0;
    if (scores)
        snprintf(text + used, sizeof(text) - used, "\n%zu players\n", count);
    list->active = false;
    return q2_player_print(g, a->id, 2, text, e);
}
bool q2_player_command(qa_q2_game *g, qa_actor_id id, const char *command, size_t count,
                        const char *const *args, bool *recognized, qa_error *e) {
    *recognized = true;
    q2_actor *a = g ? q2_actor_get(g, id, false, NULL) : NULL;
    if (!a || !command || (count && !args)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 player command");
        return false;
    }
    for (size_t i = 0; i < count; i++)
        if (!args[i]) {
            qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing Q2 command argument");
            return false;
        }
    q2_client_state *s = a->client;
    q2_players *p = g->player_runtime;
    qa_q2_player_services *services = &p->services;
    bool handled = false;
    if (services->command &&
        !services->command(services->context, id, command, count, args, &handled, e))
        return false;
    if (handled || !q2_actor_live(g, id))
        return true;
    char text[2048];
    join_arguments(count, args, text, sizeof(text));
    if (!s) {
        if (p->intermission)
            return true;
        if (equal_name(command, "use") || equal_name(command, "drop"))
            return item_command(g, a, text, equal_name(command, "use"), e);
        if (equal_name(command, "give")) {
            if (!qa_q2_player_cheats_allowed(g))
                return q2_player_print(g, id, 2,
                                      "You must run the server with '+set cheats 1' to "
                                      "enable this command.\n", e);
            return give(g, a, count, args, true, e);
        }
        *recognized = false;
        return true;
    }
    if (equal_name(command, "say") || equal_name(command, "say_team"))
        return say(g, a, text, equal_name(command, "say_team"), e);
    if (equal_name(command, "players") || equal_name(command, "playerlist"))
        return players_list(g, a, equal_name(command, "players"), e);
    if (equal_name(command, "score") || equal_name(command, "help")) {
        if (g->options.edition == QA_Q2_RERELEASE && !g->options.deathmatch &&
            equal_name(command, "help"))
            return qa_q2_player_help_computer(g, id, e);
        s->show_inventory = false;
        if (equal_name(command, "score")) {
            s->show_help = false;
            s->show_scores = !s->show_scores;
        } else {
            s->show_scores = g->options.deathmatch;
            if (!g->options.deathmatch)
                s->show_help = !s->show_help;
        }
        if (!q2_player_publish_inventory(g, a, e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
        if (s->show_scores && (g->options.deathmatch || g->options.cooperative))
            return q2_player_scoreboard(g, a, true, e);
        return !equal_name(command, "help") || g->options.deathmatch ||
               q2_player_emit(g,
                              &(qa_q2_player_event){
                                  .kind = QA_Q2_PLAYER_HELP, .actor = id, .visible = s->show_help},
                              e);
    }
    if (p->intermission)
        return true;
    if (equal_name(command, "use") || equal_name(command, "drop"))
        return item_command(g, a, text, equal_name(command, "use"), e);
    if (equal_name(command, "inven")) {
        s->show_scores = s->show_help = false;
        s->show_inventory = !s->show_inventory;
        return q2_player_publish_inventory(g, a, e);
    }
    if (equal_name(command, "invnext") || equal_name(command, "invprev") ||
        equal_name(command, "invnextw") || equal_name(command, "invprevw") ||
        equal_name(command, "invnextp") || equal_name(command, "invprevp")) {
        bool next = tolower((unsigned char)command[3]) == 'n';
        size_t length = strlen(command);
        int filter = command[length - 1] == 'w' ? 1 : command[length - 1] == 'p' ? 2 : 0;
        if (s->info.chase_target.registry)
            return qa_q2_player_chase(g, id, next ? 1 : -1, false, e);
        if (!select_item(g, a, next ? 1 : -1, filter, e))
            return false;
        return !s->show_inventory || q2_player_publish_inventory(g, a, e);
    }
    if (equal_name(command, "invuse") || equal_name(command, "invdrop")) {
        int owned = 0;
        if (s->info.selected_item && !q2_count(g, id, s->info.selected_item, &owned, e))
            return false;
        if (!owned && !select_item(g, a, 1, 0, e))
            return false;
        const qa_q2_item_definition *d = q2_item_by_id(g, s->info.selected_item);
        if (!d)
            return q2_player_print(g, id, 2, "No item to use.\n", e);
        return equal_name(command, "invuse") ? use_item(g, a, d, e) : drop_item(g, a, d, e);
    }
    if (equal_name(command, "weapprev") || equal_name(command, "weapnext") ||
        equal_name(command, "weaplast")) {
        if (!a->weapon_bound)
            return true;
        qa_q2_selection selection;
        if (equal_name(command, "weaplast"))
            return !a->weapon.last_weapon ||
                   qa_q2_weapon_select(g, id, a->weapon.last_weapon, false, &selection, e);
        int direction = equal_name(command, "weapprev") ? 1 : -1;
        for (int step = 1; step < QA_Q2_WEAPON_COUNT; step++) {
            int index = ((int)a->weapon.weapon + direction * step + QA_Q2_WEAPON_COUNT) %
                        QA_Q2_WEAPON_COUNT;
            if (!index || !qa_q2_weapon_definition_at(g, (qa_q2_weapon)index))
                continue;
            if (!qa_q2_weapon_select(g, id, (qa_q2_weapon)index, false, &selection, e))
                return false;
            if (selection == QA_Q2_SELECTED)
                break;
        }
        return true;
    }
    if (equal_name(command, "kill")) {
        if ((g->options.edition == QA_Q2_RERELEASE && s->info.spectator) ||
            g->now_ns < q2_deadline(s->respawn_ns, 5 * Q2_NS))
            return true;
        qa_combat_state combat;
        if (!qa_combat_read(g->services.combat, id, &combat, e))
            return false;
        if (!q2_actor_live(g, id)) return true;
        qa_damage_request suicide = {.target = id, .amount = 100000,
            .attack = {.attacker = id, .inflictor = id, .weapon_provider = g->options.owner,
                .combat_provider = g->options.owner,
                .time_ns = g->now_ns,
                .cause = qa_q2_damage_cause(g->options.edition, g->options.product, 23, 0)}};
        if (!qa_attack_next(&g->sequence, &suicide.attack, e)) return false;
        if (g->options.edition == QA_Q2_RERELEASE)
            suicide.attack.cause.source.q2.no_point_loss = p->rules.teamplay;
        s->info.god = false;
        combat.invulnerable = false;
        if (!qa_combat_set_traits(g->services.combat, id, &combat, e))
            return false;
        if (!q2_actor_live(g, id)) return true;
        if (!qa_combat_set_health(g->services.combat, id, 0, e)) return false;
        if (!q2_actor_live(g, id)) return true;
        if (g->options.edition == QA_Q2_RERELEASE || g->options.product == QA_Q2_ROGUE) {
            if (!qa_q2_clear_trackers(g, id, e)) return false;
            if (!q2_actor_live(g, id)) return true;
            if (a->powers && a->powers->sphere.registry) {
                qa_actor_id sphere = a->powers->sphere;
                a->powers->sphere = (qa_actor_id){0};
                if (q2_actor_live(g, sphere) && !qa_session_release(g->services.session, sphere, e))
                    return false;
                if (!q2_actor_live(g, id)) return true;
            }
        }
        if (services->suicide)
            return services->suicide(services->context, &suicide, e);
        qa_damage_outcome death = {.request = suicide,
            .result = {.applied_damage = 100000, .reaction = QA_REACTION_DEATH}};
        return q2_player_death(g, a, &death, e);
    }
    if (equal_name(command, "putaway")) {
        s->show_inventory = s->show_scores = s->show_help = false;
        return true;
    }
    if (equal_name(command, "wave")) {
        qa_q2_player_movement m;
        if (!q2_player_observe(g, a, &m, e))
            return false;
        if (m.ducked || s->animation_priority > 1)
            return true;
        int wave = count ? argument_integer(args[0]) : 0;
        if (wave < 0 || wave > 4)
            wave = 4;
        static const int first[] = {72, 84, 95, 112, 123}, last[] = {83, 94, 111, 122, 134};
        static const char *names[] = {"flipoff\n", "salute\n", "taunt\n", "wave\n", "point\n"};
        s->animation_priority = 1;
        s->visual.frame = first[wave] - 1;
        s->animation_end = last[wave];
        return q2_player_print(g, id, 2, names[wave], e);
    }
    if (equal_name(command, "god") ||
        (g->options.edition == QA_Q2_RERELEASE && equal_name(command, "immortal")) ||
        equal_name(command, "notarget") ||
        equal_name(command, "noclip") || equal_name(command, "give") ||
        equal_name(command, "target")) {
        if (!qa_q2_player_cheats_allowed(g))
            return q2_player_print(g, id, 2,
                                   "You must run the server with '+set cheats 1' to "
                                   "enable this command.\n",
                                   e);
        if (equal_name(command, "give"))
            return give(g, a, count, args, true, e);
        if (equal_name(command, "target")) {
            qa_string_id target;
            if (!qa_builtin_resource(&g->services, text, &target, e))
                return false;
            if (!g->services.use_targets) {
                qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 target service is not configured");
                return false;
            }
            return g->services.use_targets(g->services.context, id, id, target, 0, 0, e);
        }
        bool enabled;
        if (equal_name(command, "immortal")) {
            a->character_immortal = !a->character_immortal;
            enabled = a->character_immortal;
        } else if (equal_name(command, "god")) {
            s->info.god = !s->info.god;
            enabled = s->info.god;
            qa_combat_state combat;
            if (!qa_combat_read_traits(g->services.combat, id, &combat, e))
                return false;
            combat.invulnerable = enabled;
            if (!qa_combat_set_traits(g->services.combat, id, &combat, e))
                return false;
        } else if (equal_name(command, "notarget")) {
            if (!qa_q2_player_notarget(g, id, &enabled, e))
                return false;
        } else {
            s->info.noclip = !s->info.noclip;
            enabled = s->info.noclip;
            if (!q2_player_move(
                    g, a, &(qa_q2_player_motion){.kind = QA_Q2_PLAYER_NOCLIP, .enabled = enabled},
                    e))
                return false;
        }
        snprintf(text, sizeof(text), "%s %s\n", equal_name(command, "god") ? "godmode" : command,
                 enabled ? "ON" : "OFF");
        return !q2_actor_live(g, id) || q2_player_print(g, id, 2, text, e);
    }
    *recognized = false;
    return true;
}
typedef struct player_command_call {
    qa_q2_game *game;
    const char *command;
    size_t count;
    const char *const *args;
} player_command_call;

static bool player_command(void *context, qa_actor_id id, qa_error *e) {
    const player_command_call *call = context;
    qa_q2_game *g = call->game;
    const char *command = call->command;
    size_t count = call->count;
    const char *const *args = call->args;
    bool handled;
    if (!q2_player_command(g, id, command, count, args, &handled, e))
        return false;
    if (handled || !q2_actor_live(g, id))
        return true;
    char text[2048], unknown[2048];
    join_arguments(count, args, text, sizeof(text));
    snprintf(unknown, sizeof(unknown), "%s%s%.1800s", command, count ? " " : "", text);
    q2_actor *a = q2_client(g, id, e);
    return a && say(g, a, unknown, false, e);
}
bool qa_q2_player_command(qa_q2_game *g, qa_actor_id id, const char *command, size_t count,
                          const char *const *args, qa_error *e) {
    player_command_call call = {g, command, count, args};
    return qa_q2_run_actor(g, id, player_command, &call, e);
}
