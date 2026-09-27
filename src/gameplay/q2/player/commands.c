#include "internal.h"

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
    qa_q2_score_row rows[12];
    size_t count = 0;
    for (size_t i = 0; i < g->capacity; i++) {
        q2_actor *other = g->actors[i];
        if (!other || !other->client || !other->client->info.connected ||
            other->client->info.spectator)
            continue;
        q2_client_state *s = other->client;
        qa_q2_score_row row = {
            .slot = s->info.slot,
            .name = s->info.name,
            .score = s->info.score,
            .ping = s->info.ping > 999 ? 999 : s->info.ping,
            .minutes = (int)fmin((double)((g->now_ns - s->entered_ns) / (60 * Q2_NS)), INT_MAX)};
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
    return q2_player_emit(g,
                          &(qa_q2_player_event){.kind = QA_Q2_PLAYER_SCOREBOARD,
                                                .actor = a->id,
                                                .scores = rows,
                                                .count = count,
                                                .reliable = reliable},
                          e);
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
            return true;
        }
    }
    a->client->info.selected_item = 0;
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
    bool used;
    if (d->weapon) {
        qa_q2_selection selection;
        if (!qa_q2_weapon_select(g, a->id, d->weapon, false, &selection, e))
            return false;
        if (selection == QA_Q2_NO_AMMO || selection == QA_Q2_INSUFFICIENT_AMMO)
            return q2_player_print(g, a->id, 2, "Not enough ammo.\n", e);
    } else if (!qa_q2_item_use(g, a->id, d->item, &used, e))
        return false;
    if (q2_actor_live(g, a->id))
        a->client->info.selected_item = d->item;
    return true;
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
    qa_actor_id drop;
    bool accepted;
    if (!qa_q2_item_drop(g, a->id, d->item, &(qa_q2_drop_options){0}, &drop, &accepted, e))
        return false;
    return accepted || !q2_actor_live(g, a->id) ||
           q2_player_print(g, a->id, 2, "Can't drop current weapon\n", e);
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
    for (size_t i = 0; i < g->capacity; i++) {
        q2_actor *other = g->actors[i];
        if (!other || !other->client || !other->client->info.connected)
            continue;
        char recipient[256];
        team_name(other->client->info.skin, (g->options.deathmatch_flags & 64) != 0, recipient,
                  sizeof(recipient));
        if (team && strcmp(own_team, recipient))
            continue;
        if (!q2_player_print(g, other->id, 3, message, e))
            return false;
    }
    return true;
}
static bool give(qa_q2_game *g, q2_actor *a, size_t count, const char *const *args, qa_error *e) {
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
            if (services->grant_arsenal &&
                !services->grant_arsenal(services->context, a->id, category != 0, &handled, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            if (!handled) {
                if (category && all && rr) {
                    bool accepted;
                    if (!qa_q2_item_give(g, a->id, "item_pack", 0, &accepted, e))
                        return false;
                }
                for (size_t i = 0; i < qa_q2_item_count(g) && q2_actor_live(g, a->id); i++) {
                    const qa_q2_item_definition *d = qa_q2_item_at(g, i);
                    if (category ? d->kind != QA_Q2_ITEM_AMMO : !d->weapon || d->inventory_only)
                        continue;
                    if (!q2_item_ensure(g, a->id, d, e))
                        return false;
                    qa_inventory_entry entry;
                    if (!qa_inventory_entry_read(g->services.inventory, a->id, d->item, &entry, e))
                        return false;
                    entry.count =
                        category ? fmin(entry.count + 1000, entry.capacity) : entry.count + 1;
                    if (!qa_inventory_configure(g->services.inventory, a->id, &entry, NULL, NULL,
                                                e))
                        return false;
                }
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
        bool accepted;
        if (!qa_q2_item_give(g, a->id, "item_power_shield", 0, &accepted, e))
            return false;
        if (!all)
            return true;
    }
    if (all) {
        for (size_t i = 0; i < qa_q2_item_count(g) && q2_actor_live(g, a->id); i++) {
            const qa_q2_item_definition *d = qa_q2_item_at(g, i);
            if (d->weapon || d->kind == QA_Q2_ITEM_AMMO || d->kind == QA_Q2_ITEM_ARMOR ||
                d->kind == QA_Q2_ITEM_SHARD || d->inventory_only)
                continue;
            if (rr &&
                (d->kind == QA_Q2_ITEM_HEALTH ||
                 (d->kind == QA_Q2_ITEM_MAX_HEALTH && strcmp(d->classname, "item_adrenaline")) ||
                 d->kind == QA_Q2_ITEM_FOOD || d->kind == QA_Q2_ITEM_PACK))
                continue;
            qa_inventory_entry entry = {.item = d->item,
                                        .count = rr && d->kind == QA_Q2_ITEM_KEY ? 8 : 1,
                                        .capacity = d->capacity,
                                        .policy = QA_COUNT_SOURCE_INT32};
            if (!qa_inventory_configure(g->services.inventory, a->id, &entry, NULL, NULL, e))
                return false;
        }
        if (rr && a->powers)
            a->powers->power_cubes = 0xff;
        return true;
    }
    const qa_q2_item_definition *d = qa_q2_item_lookup(g, requested);
    if (!d && count)
        d = qa_q2_item_lookup(g, args[0]);
    if (!d) {
        bool handled = false;
        if (services->give_item &&
            !services->give_item(services->context, a->id, count, args, &handled, e))
            return false;
        return handled || q2_player_print(g, a->id, 2, "unknown item\n", e);
    }
    if (d->inventory_only) {
        if (!rr)
            return q2_player_print(g, a->id, 2, "non-pickup item\n", e);
        return qa_inventory_configure(
            g->services.inventory, a->id,
            &(qa_inventory_entry){d->item, 1, d->capacity, QA_COUNT_SOURCE_INT32}, NULL, NULL, e);
    }
    if (d->kind == QA_Q2_ITEM_AMMO && count == 2) {
        if (!q2_item_ensure(g, a->id, d, e))
            return false;
        qa_inventory_entry entry;
        if (!qa_inventory_entry_read(g->services.inventory, a->id, d->item, &entry, e))
            return false;
        entry.count = argument_integer(args[1]);
        return qa_inventory_configure(g->services.inventory, a->id, &entry, NULL, NULL, e);
    }
    bool accepted;
    return qa_q2_item_give(g, a->id, d->classname, 0, &accepted, e);
}
static bool players_list(qa_q2_game *g, q2_actor *a, bool scores, qa_error *e) {
    size_t n = 0;
    for (size_t i = 0; i < g->capacity; i++)
        if (g->actors[i] && g->actors[i]->client && g->actors[i]->client->info.connected)
            n++;
    q2_actor **ordered = n ? malloc(n * sizeof(*ordered)) : NULL;
    if (n && !ordered) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Listing Q2 players");
        return false;
    }
    size_t count = 0;
    for (size_t i = 0; i < g->capacity; i++) {
        q2_actor *other = g->actors[i];
        if (!other || !other->client || !other->client->info.connected)
            continue;
        size_t at = count;
        while (at && (scores ? ordered[at - 1]->client->info.score > other->client->info.score
                             : ordered[at - 1]->client->info.slot > other->client->info.slot)) {
            ordered[at] = ordered[at - 1];
            at--;
        }
        ordered[at] = other;
        count++;
    }
    char text[1400];
    size_t used = 0;
    for (size_t i = 0; i < count; i++) {
        q2_client_state *s = ordered[i]->client;
        char line[128];
        uint64_t seconds = (g->now_ns - s->entered_ns) / Q2_NS;
        if (scores)
            snprintf(line, sizeof(line), "%3d %s\n", s->info.score, s->info.name);
        else
            snprintf(line, sizeof(line), "%02llu:%02llu %4d %3d %s%s\n",
                     (unsigned long long)(seconds / 60), (unsigned long long)(seconds % 60),
                     s->info.ping, s->info.score, s->info.name,
                     s->info.spectator ? " (spectator)" : "");
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
    free(ordered);
    return q2_player_print(g, a->id, 2, text, e);
}
bool qa_q2_player_command(qa_q2_game *g, qa_actor_id id, const char *command, size_t count,
                          const char *const *args, qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
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
    if (equal_name(command, "use"))
        return use_item(g, a, qa_q2_item_lookup(g, text), e);
    if (equal_name(command, "drop"))
        return drop_item(g, a, qa_q2_item_lookup(g, text), e);
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
        s->info.god = false;
        combat.invulnerable = false;
        if (!qa_combat_set_traits(g->services.combat, id, &combat, e))
            return false;
        return !q2_actor_live(g, id) ||
               q2_player_environment_damage(g, a, fmaxf(1, combat.health) + 1, 23, 32, e);
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
    if (equal_name(command, "god") || equal_name(command, "notarget") ||
        equal_name(command, "noclip") || equal_name(command, "give") ||
        equal_name(command, "target")) {
        if ((g->options.edition == QA_Q2_RERELEASE ? p->rules.max_clients > 1
                                                   : g->options.deathmatch) &&
            !p->rules.cheats)
            return q2_player_print(g, id, 2,
                                   "You must run the server with '+set cheats 1' to "
                                   "enable this command.\n",
                                   e);
        if (equal_name(command, "give"))
            return give(g, a, count, args, e);
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
        if (equal_name(command, "god")) {
            s->info.god = !s->info.god;
            enabled = s->info.god;
            qa_combat_state combat;
            if (!qa_combat_read_traits(g->services.combat, id, &combat, e))
                return false;
            combat.invulnerable = enabled;
            if (!qa_combat_set_traits(g->services.combat, id, &combat, e))
                return false;
        } else if (equal_name(command, "notarget")) {
            s->info.notarget = !s->info.notarget;
            enabled = s->info.notarget;
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
    char unknown[2048];
    snprintf(unknown, sizeof(unknown), "%s%s%.1800s", command, count ? " " : "", text);
    return say(g, a, unknown, false, e);
}
