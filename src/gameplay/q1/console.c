#include "maps/internal.h"
#include "qa/game_q1_bots.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>

static int32_t source_integer(const char *text) {
    if (!text)
        return 0;
    while (*text && isspace((unsigned char)*text))
        ++text;
    bool negative = *text == '-';
    if (*text == '-' || *text == '+')
        ++text;
    uint32_t value = 0, limit = negative ? UINT32_C(2147483648) : INT32_MAX;
    for (; *text >= '0' && *text <= '9'; ++text) {
        unsigned digit = (unsigned)(*text - '0');
        value = value > (limit - digit) / 10 ? limit : value * 10 + digit;
    }
    return negative ? value == UINT32_C(2147483648) ? INT32_MIN : -(int32_t)value
                    : (int32_t)value;
}
static bool normalized(const char *a, const char *b) {
    for (;;) {
        while (*a == ' ' || *a == '_')
            ++a;
        while (*b == ' ' || *b == '_')
            ++b;
        unsigned char x = (unsigned char)*a, y = (unsigned char)*b;
        if (tolower(x) != tolower(y))
            return false;
        if (!x)
            return true;
        ++a;
        ++b;
    }
}
bool q1_developer_message(qa_q1_game *g, const char *text, qa_error *error) {
    qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT, .family = QA_GAME_Q1,
                              .provider = g->options.provider, .time_ns = g->time_ns};
    return qa_builtin_resource(&g->services, "developer-message", &event.resource, error) &&
           qa_builtin_resource(&g->services, text, &event.text, error) &&
           qa_builtin_emit(&g->services, &event, error);
}
static bool world_impulse(const qa_q1_game *g, uint8_t impulse) {
    bool addon = g->options.program == QA_Q1_DOPA || g->options.program == QA_Q1_MG1 ||
                 g->options.program == QA_Q1_MG3;
    return impulse == 11 || impulse == 255 ||
           (g->options.program == QA_Q1_HIPNOTIC && impulse >= 200 && impulse <= 206) ||
           (addon && impulse == 219) ||
           (g->options.program == QA_Q1_MG3 &&
            ((impulse >= 101 && impulse <= 105) || impulse == 116 || impulse == 117 ||
             impulse == 119 || impulse == 121 || impulse == 220 ||
             (impulse >= 222 && impulse <= 224)));
}
bool qa_q1_game_console_operation(const qa_q1_game *g, const qa_command_invocation *command,
                                   qa_q1_console_operation *out) {
    if (!g || !command || !out || !command->argc || !command->argv || !command->argv[0])
        return false;
    const char *name = command->argv[0];
    *out = QA_Q1_CONSOLE_UNKNOWN;
    if (!strcmp(name, "kill") || !strcmp(name, "suicide") || !strcmp(name, "god") ||
        !strcmp(name, "notarget"))
        *out = QA_Q1_CONSOLE_CHARACTER;
    else if (!strcmp(name, "noclip") || !strcmp(name, "fly"))
        *out = QA_Q1_CONSOLE_MOVEMENT;
    else if (!strcmp(name, "give") || !strcmp(name, "giveall") || !strcmp(name, "drop"))
        *out = QA_Q1_CONSOLE_EQUIPMENT;
    else if (!strcmp(name, "team") || !strcmp(name, "observer"))
        *out = QA_Q1_CONSOLE_MODE;
    else if (!strcmp(name, "use") || !strcmp(name, "weapnext") || !strcmp(name, "weapprev"))
        *out = QA_Q1_CONSOLE_ARSENAL;
    else if (!strcmp(name, "impulse")) {
        uint8_t impulse = command->argc > 1 ? (uint8_t)source_integer(command->argv[1]) : 0;
        *out = world_impulse(g, impulse) ? QA_Q1_CONSOLE_WORLD : QA_Q1_CONSOLE_ARSENAL;
    }
    return true;
}
static bool program_weapon(qa_q1_program program, qa_q1_weapon weapon) {
    if ((unsigned)program > QA_Q1_CTF || (unsigned)weapon >= QA_Q1_WEAPON_COUNT)
        return false;
    if (weapon <= QA_Q1_LIGHTNING)
        return true;
    if (weapon >= QA_Q1_LASER && weapon <= QA_Q1_PROXIMITY)
        return program == QA_Q1_HIPNOTIC;
    if (weapon >= QA_Q1_LAVA_NAILGUN && weapon <= QA_Q1_ROGUE_GRAPPLE)
        return program == QA_Q1_ROGUE;
    if (weapon == QA_Q1_MG3_LASER || weapon == QA_Q1_MG3_MJOLNIR)
        return program == QA_Q1_MG3;
    return weapon == QA_Q1_CTF_GRAPPLE && program == QA_Q1_CTF;
}
static const struct { const char *console, *display; } weapon_labels[QA_Q1_WEAPON_COUNT] = {
    {"Axe", "Axe"}, {"Shotgun", "Shotgun"},
    {"Super Shotgun", "Double-barrelled Shotgun"}, {"Nailgun", "Nailgun"},
    {"Super Nailgun", "Super Nailgun"}, {"Grenade Launcher", "Grenade Launcher"},
    {"Rocket Launcher", "Rocket Launcher"}, {"Lightning Gun", "Thunderbolt"},
    {"Laser Cannon", "Laser Cannon"}, {"Mjolnir", "Mjolnir"},
    {"Proximity Gun", "Proximity Gun"}, {"Lava Nailgun", "Lava Nailgun"},
    {"Lava Super Nailgun", "Lava Supernailgun"}, {"Multi Grenade", "Multi Grenade"},
    {"Multi Rocket", "Multi Rocket"}, {"Plasma Gun", "Plasma"},
    {"Grapple", "Grapple"}, {"Laser Cannon", "Laser"},
    {"Mjolnir", "Mjolnir"}, {"Grapple", "Grapple"}};
bool qa_q1_weapon_profile_identity(qa_q1_program program, qa_q1_weapon weapon,
    qa_q1_weapon_profile *out) {
    if (!out)
        return false;
    *out = (qa_q1_weapon_profile){0};
    if (!program_weapon(program, weapon))
        return false;
    int ammo = q1_weapon_declared_ammo(weapon);
    *out = (qa_q1_weapon_profile){.item=qa_q1_weapon_identity(weapon),
        .label=weapon_labels[weapon].display,
        .ammo=ammo<0?NULL:qa_q1_ammo_identity((qa_q1_ammo)ammo)};
    return true;
}
static bool supported(const qa_q1_game *g, qa_q1_weapon weapon) {
    return program_weapon(g->options.program, weapon);
}
static bool grant_finish(qa_q1_game_operation *operation, bool ok, qa_error *error) {
    if (ok && !qa_q1_game_operation_live(operation)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 source retired during item grant");
        ok = false;
    }
    qa_q1_game_operation_end(operation);
    return ok;
}
static bool joined(const qa_command_invocation *command, size_t first, size_t last,
                     char *out, size_t capacity, qa_error *error) {
    size_t length = 0;
    for (size_t i = first; i < last; ++i) {
        size_t size = strlen(command->argv[i]);
        if (size >= capacity - length || (i != first && size + 1 >= capacity - length))
            return q1_map_fail(error, "Q1 source item name is too long");
        if (i != first)
            out[length++] = ' ';
        memcpy(out + length, command->argv[i], size);
        length += size;
    }
    out[length] = 0;
    return true;
}
static qa_q1_weapon named_weapon(const qa_q1_game *g, const char *name, bool numbered) {
    if (numbered) {
        if (g->options.program == QA_Q1_HIPNOTIC) {
            if (!strcmp(name, "6a")) return QA_Q1_PROXIMITY;
            if (!strcmp(name, "9")) return QA_Q1_LASER;
            if (!strcmp(name, "0")) return QA_Q1_MJOLNIR;
        }
        if (name[0] >= '2' && name[0] <= '8' && !name[1])
            return (qa_q1_weapon)(name[0] - '1');
    }
    for (unsigned i = 0; i < QA_Q1_WEAPON_COUNT; ++i) {
        if (!supported(g, (qa_q1_weapon)i))
            continue;
        if (normalized(name, weapon_labels[i].console))
            return (qa_q1_weapon)i;
        const char *item = qa_strings_cstr(qa_session_strings(g->services.session), g->weapons[i]);
        const char *short_name = item ? strchr(item, '/') : NULL;
        char alias[96];
        if (!item || !short_name)
            continue;
        snprintf(alias, sizeof(alias), "weapon_%s", short_name + 1);
        if (normalized(name, item) || normalized(name, short_name + 1) || normalized(name, alias))
            return (qa_q1_weapon)i;
    }
    return QA_Q1_WEAPON_COUNT;
}
static bool configure(qa_q1_game *g, qa_actor_id actor, qa_item_id item, double count,
                        double capacity, qa_error *error) {
    qa_inventory_entry entry;
    qa_error local = {0};
    if (!qa_inventory_entry_read(g->services.inventory, actor, item, &entry, &local)) {
        if (local.code != QA_ERROR_NOT_FOUND) {
            if (error) *error = local;
            return false;
        }
        entry = (qa_inventory_entry){.item = item, .policy = QA_COUNT_SOURCE_FLOAT};
    }
    entry.count = fmax(0, count);
    entry.capacity = fmax(entry.capacity, fmax(capacity, entry.count));
    return qa_inventory_configure(g->services.inventory, actor, &entry, NULL, NULL, error);
}
static bool selected_grant(qa_q1_game *g, qa_actor_id actor, qa_q1_cheat_grant grant,
                            bool *handled, qa_error *error) {
    *handled = false;
    return !g->host.cheat_arsenal ||
           g->host.cheat_arsenal(g->host.context, actor, grant, handled, error);
}
static bool native_grant(qa_q1_game *g, qa_actor_id actor, bool ammo, qa_error *error) {
    bool used[QA_Q1_AMMO_COUNT] = {0};
    for (unsigned i = 0; i < QA_Q1_WEAPON_COUNT && q1_alive(g, actor); ++i) {
        if (!supported(g, (qa_q1_weapon)i))
            continue;
        if (!ammo) {
            if (!configure(g, actor, g->weapons[i], 1, 1, error))
                return false;
            continue;
        }
        int kind = q1_weapon_ammo((qa_q1_weapon)i);
        if (kind < 0 || used[kind])
            continue;
        used[kind] = true;
        const char *item = qa_strings_cstr(qa_session_strings(g->services.session), g->ammo[kind]);
        double count = item && strstr(item, "nails") ? 200 : 100;
        if (!configure(g, actor, g->ammo[kind], count, count, error))
            return false;
    }
    return true;
}
static bool ammo_set(qa_q1_game *g, qa_actor_id actor, qa_item_id item, double amount,
                       qa_error *error) {
    qa_supply *supply = NULL;
    if (g->host.supply && !g->host.supply(g->host.context, actor, &supply, error)) return false;
    if (!q1_alive(g, actor))
        return true;
    if (!supply)
        return configure(g, actor, item, amount, amount, error);
    qa_supply_offer offer = {.kind = QA_SUPPLY_AMMO, .item = item};
    qa_pickup_grant grant = {.item = item, .amount = fmax(1, amount)};
    offer.ammo = &grant;
    offer.ammo_count = 1;
    qa_supply_preview_result preview = {0};
    if (!qa_supply_preview(supply, actor, &offer, false, &preview, error))
        return false;
    bool ok = true;
    for (size_t i = 0; ok && i < preview.ammo_count && q1_alive(g, actor); ++i)
        ok = configure(g, actor, preview.ammo[i].item, amount, amount, error);
    qa_supply_preview_free(&preview);
    return ok;
}
static bool allowed(qa_q1_game *g, qa_actor_id actor, bool *out, qa_error *error) {
    *out = true;
    if (!g->options.deathmatch)
        return true;
    float cheats = 0;
    if (!q1_source_value(g, QA_Q1_SOURCE_SV_CHEATS, 0, &cheats, error))
        return false;
    *out = g->options.edition == QA_Q1_RERELEASE && cheats != 0;
    return *out || !q1_alive(g, actor) ||
           q1_message(g, actor, "Cheats are disabled on this server.\n", error);
}
static bool give_inner(qa_q1_game *g, qa_actor_id actor, const qa_command_invocation *command,
                        bool delegate, bool *recognized, qa_error *error) {
    if (recognized)
        *recognized = true;
    if (command->argc < 2)
        return q1_map_fail(error, "Usage: give <all|weapons|ammo|health|armor|keys|item> [amount]");
    const char *input = command->argv[1];
    bool all = normalized(input, "all"), has_amount = command->argc > 2;
    int32_t amount = has_amount ? source_integer(command->argv[2]) : 0;
    if (all || normalized(input, "health") || normalized(input, "h")) {
        qa_builtin_actor_traits traits = {0};
        if (g->services.actor_traits)
            g->services.actor_traits(g->services.context, actor, &traits);
        if (!q1_alive(g, actor))
            return true;
        q1_player *player = q1_player_get(g, actor);
        float health = has_amount ? (float)amount : normalized(input, "h") ? 0
                       : traits.max_health > 0 ? traits.max_health : player ? player->max_health : 100;
        if (!qa_combat_set_health(g->services.combat, actor, health, error))
            return false;
        if (!all || !q1_alive(g, actor))
            return true;
    }
    if (all || normalized(input, "armor") || normalized(input, "a")) {
        qa_combat_state combat;
        if (!qa_combat_read_traits(g->services.combat, actor, &combat, error))
            return false;
        float points = has_amount ? (float)amount : normalized(input, "a") ? 0 : 200;
        combat.armor.regular = (qa_regular_armor){.kind = points <= 0 ? QA_ARMOR_NONE : QA_ARMOR_Q1,
                                                .points = points};
        if (points > 0) {
            const char *item = points > 150 ? "q1:item_armorInv" : points > 100 ? "q1:item_armor2"
                                                                                 : "q1:item_armor1";
            if (!qa_builtin_resource(&g->services, item, &combat.armor.regular.item, error))
                return false;
            combat.armor.regular.protection.q1_absorption = points > 150 ? .8f : points > 100 ? .6f : .3f;
        }
        if (!q1_alive(g, actor))
            return true;
        if (!qa_combat_set_armor(g->services.combat, actor, &combat.armor, error))
            return false;
        if (!all || !q1_alive(g, actor))
            return true;
    }
    if (all || normalized(input, "weapons")) {
        bool selected = false;
        if (delegate && !selected_grant(g, actor, QA_Q1_CHEAT_WEAPONS, &selected, error))
            return false;
        if (!selected && !native_grant(g, actor, false, error))
            return false;
        if (!all || !q1_alive(g, actor))
            return true;
    }
    if (all || normalized(input, "ammo")) {
        bool selected = false;
        if (delegate && !selected_grant(g, actor, QA_Q1_CHEAT_AMMO, &selected, error))
            return false;
        if (!selected && !native_grant(g, actor, true, error))
            return false;
        if (!all || !q1_alive(g, actor))
            return true;
    }
    if (all || normalized(input, "keys")) {
        const char *keys[] = {"q1:key/silver", "q1:key/gold"};
        for (unsigned i = 0; i < 2 && q1_alive(g, actor); ++i) {
            qa_string_id key;
            if (!qa_builtin_resource(&g->services, keys[i], &key, error) ||
                !configure(g, actor, key, 1, 1, error))
                return false;
        }
        return true;
    }
    if (normalized(input, "items")) {
        const char *items[] = {"quad", "pent", "ring", "suit"};
        for (unsigned i = 0; i < 4 && q1_alive(g, actor); ++i) {
            const char *argv[] = {"give", items[i]};
            qa_command_invocation nested = *command;
            nested.argc = 2;
            nested.argv = argv;
            if (!give_inner(g, actor, &nested, delegate, recognized, error))
                return false;
        }
        return true;
    }
    static const char short_ammo[] = {'s', 'n', 'r', 'c', 'l', 'm', 'p'};
    for (unsigned i = 0; i < QA_Q1_AMMO_COUNT; ++i)
        if (tolower((unsigned char)input[0]) == short_ammo[i] && !input[1]) {
            bool available = false;
            for (unsigned j = 0; j < QA_Q1_WEAPON_COUNT; ++j)
                available |= supported(g, (qa_q1_weapon)j) && q1_weapon_ammo((qa_q1_weapon)j) == (int)i;
            return available ? ammo_set(g, actor, g->ammo[i], has_amount ? amount : 0, error)
                             : q1_map_fail(error, "Ammo is unavailable in this Q1 arsenal");
        }
    bool delegated = false;
    if (delegate && g->host.console_give_item &&
        !g->host.console_give_item(g->host.context, actor, command, &delegated, error))
        return false;
    if (delegated || !q1_alive(g, actor))
        return true;
    char name[256];
    size_t last = command->argc;
    if (last > 2) {
        const char *numeric = command->argv[last - 1];
        if (*numeric == '-') ++numeric;
        if (*numeric) {
            bool digits = true;
            for (const char *p = numeric; *p; ++p)
                digits &= *p >= '0' && *p <= '9';
            if (digits) --last;
        }
    }
    if (!joined(command, 1, last, name, sizeof(name), error))
        return false;
    qa_q1_weapon weapon = named_weapon(g, name, true);
    if (weapon != QA_Q1_WEAPON_COUNT) {
        qa_supply *supply = NULL;
        if (g->host.supply && !g->host.supply(g->host.context, actor, &supply, error)) return false;
        if (!q1_alive(g, actor))
            return true;
        if (!supply)
            return configure(g, actor, g->weapons[weapon], 1, 1, error);
        qa_supply_offer offer = {.kind = QA_SUPPLY_WEAPON, .item = g->weapons[weapon],
                                 .weapon = g->weapons[weapon]};
        qa_supply_options options = {.selection = QA_PICKUP_SWITCH_NEVER};
        bool accepted;
        return qa_supply_apply(supply, actor, &offer, &options, &accepted, error);
    }
    for (unsigned i = 0; i < QA_Q1_AMMO_COUNT; ++i) {
        const char *item = qa_strings_cstr(qa_session_strings(g->services.session), g->ammo[i]);
        const char *suffix = item ? strchr(item, '/') : NULL;
        bool available = false;
        for (unsigned j = 0; j < QA_Q1_WEAPON_COUNT; ++j)
            available |= supported(g, (qa_q1_weapon)j) && q1_weapon_ammo((qa_q1_weapon)j) == (int)i;
        if (available && item && (normalized(input, item) || (suffix && normalized(input, suffix + 1))))
            return ammo_set(g, actor, g->ammo[i], has_amount ? amount
                               : q1_ammo_count(g, actor, (qa_q1_ammo)i) + 20, error);
    }
    const char *classname = normalized(input, "quad") ? "item_artifact_super_damage"
                          : normalized(input, "pent") ? "item_artifact_invulnerability"
                          : normalized(input, "ring") ? "item_artifact_invisibility"
                          : normalized(input, "suit") ? "item_artifact_envirosuit" : input;
    if (strncmp(classname, "item_", 5) && strncmp(classname, "weapon_", 7)) {
        if (recognized) {
            *recognized = false;
            return true;
        }
        return q1_map_fail(error, "Unknown Q1 source item");
    }
    qa_actor_id item;
    qa_body_state body = {0};
    qa_q1_spawn spawn = {.classname = classname};
    if (!qa_q1_pickup_spawn_external(g, &spawn, &body, false, &item, error))
        return false;
    bool accepted;
    bool ok = q1_alive(g, actor) && qa_q1_pickup_grant_external(g, item, actor, &accepted, error);
    if (!q1_alive(g, actor)) ok = true;
    if (q1_alive(g, item)) {
        qa_error cleanup = {0};
        bool released = qa_session_release(g->services.session, item, ok ? error : &cleanup);
        ok = ok && released;
    }
    return ok;
}
static bool give(qa_q1_game *g, qa_actor_id actor, const qa_command_invocation *command,
                   qa_error *error) {
    return give_inner(g, actor, command, true, NULL, error);
}
bool qa_q1_game_grant_arsenal(qa_q1_game *g, qa_actor_id actor, bool ammo,
                               bool *handled, qa_error *error) {
    if (!g || !handled)
        return q1_map_fail(error, "Invalid Q1 arsenal grant");
    *handled = false;
    q1_player *player = q1_player_get(g, actor);
    if (!player || !player->arsenal)
        return true;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    *handled = true;
    bool ok = native_grant(g, actor, ammo, error);
    ok = grant_finish(&operation, ok, error);
    return ok;
}
bool qa_q1_game_give_item(qa_q1_game *g, qa_actor_id actor, size_t argc,
                           const char *const *argv, bool *handled, qa_error *error) {
    if (!g || !handled || (argc && !argv))
        return q1_map_fail(error, "Invalid Q1 item grant");
    *handled = false;
    for (size_t i = 0; i < argc; ++i)
        if (!argv[i])
            return q1_map_fail(error, "Invalid Q1 item argument");
    q1_player *player = q1_player_get(g, actor);
    if (!argc || !player || !player->arsenal)
        return true;
    qa_command_invocation input = {.argc = argc, .argv = argv};
    size_t last = argc;
    if (last > 1) {
        const char *numeric = argv[last - 1];
        if (*numeric == '-') ++numeric;
        bool digits = *numeric != 0;
        for (const char *p = numeric; *p; ++p)
            digits &= *p >= '0' && *p <= '9';
        if (digits) --last;
    }
    char name[256];
    if (!joined(&input, 0, last, name, sizeof(name), error))
        return false;
    const char *arguments[] = {"give", name, last < argc ? argv[argc - 1] : NULL};
    qa_command_invocation command = {.argc = last < argc ? 3 : 2, .argv = arguments};
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = give_inner(g, actor, &command, false, handled, error);
    return grant_finish(&operation, ok, error);
}
static bool dispatch(qa_q1_game *g, qa_actor_id actor, const qa_command_invocation *command,
                        bool *handled, qa_error *error) {
    const char *name = command->argv[0];
    bool cheat = !strcmp(name, "god") || !strcmp(name, "notarget") ||
                 !strcmp(name, "noclip") || !strcmp(name, "fly");
    bool giving = !strcmp(name, "give") || !strcmp(name, "giveall");
    bool killing = !strcmp(name, "kill") || !strcmp(name, "suicide");
    bool impulse = !strcmp(name, "impulse");
    bool selecting = !strcmp(name, "use") || !strcmp(name, "weapnext") || !strcmp(name, "weapprev");
    bool dropping = !strcmp(name, "drop");
    *handled = cheat || giving || killing || impulse || selecting || dropping;
    if (!*handled)
        return true;
    if (!q1_alive(g, actor))
        return q1_map_fail(error, "Q1 command requires a live selected actor");
    if (cheat || giving) {
        bool cheats;
        if (!allowed(g, actor, &cheats, error))
            return false;
        if (!cheats || !q1_alive(g, actor))
            return true;
    }
    if (cheat) {
        bool enabled;
        if (!strcmp(name, "notarget")) {
            if (!qa_q1_source_client_toggle_notarget(g, actor, &enabled, error))
                return false;
        } else {
            if (!g->host.console_cheat)
                return q1_map_fail(error, "Q1 cheat requires the selected state owner");
            if (!g->host.console_cheat(g->host.context, actor, name, &enabled, error))
                return false;
        }
        char message[64];
        snprintf(message, sizeof(message), "%s %s\n", !strcmp(name, "god") ? "godmode" : name,
                 enabled ? "ON" : "OFF");
        return !q1_alive(g, actor) || q1_message(g, actor, message, error);
    }
    if (giving) {
        if (!strcmp(name, "giveall")) {
            const char *argv[] = {"give", "all"};
            qa_command_invocation all = *command;
            all.argc = 2;
            all.argv = argv;
            return give(g, actor, &all, error);
        }
        return give(g, actor, command, error);
    }
    if (killing) {
        if (q1_health(g, actor) <= 0)
            return q1_message(g, actor, "Can't suicide -- already dead!\n", error);
        return g->host.console_suicide ? g->host.console_suicide(g->host.context, actor, error)
             : q1_map_fail(error, "Q1 suicide requires selected mode and respawn owners");
    }
    if (impulse) {
        if (command->argc != 2)
            return q1_map_fail(error, "Usage: impulse <number>");
        qa_q1_console_operation operation;
        if (qa_q1_game_console_operation(g, command, &operation) && operation == QA_Q1_CONSOLE_WORLD) {
            bool recognized;
            if (!q1_source_impulse(g, actor, (uint8_t)source_integer(command->argv[1]),
                                     &recognized, error))
                return false;
            return recognized || q1_map_fail(error, "Q1 world impulse continuation is not installed");
        }
    }
    q1_player *player = q1_player_get(g, actor);
    if (!player || !player->arsenal)
        return q1_map_fail(error, "Q1 weapon command requires selected native arsenal");
    if (impulse) {
        if (command->argc != 2)
            return q1_map_fail(error, "Usage: impulse <number>");
        player->input.impulse = (uint8_t)source_integer(command->argv[1]);
        return true;
    }
    if (selecting) {
        if (strcmp(name, "use"))
            return q1_weapon_impulse(g, player, !strcmp(name, "weapnext") ? 10 : 12, error);
        char requested[256];
        if (!joined(command, 1, command->argc, requested, sizeof(requested), error))
            return false;
        qa_q1_weapon weapon = named_weapon(g, requested, false);
        if (weapon == QA_Q1_WEAPON_COUNT)
            return true;
        qa_inventory_entry entry;
        if (!qa_inventory_entry_read(g->services.inventory, actor, g->weapons[weapon], &entry, NULL) ||
            entry.count <= 0)
            return true;
        return qa_q1_player_select(g, actor, weapon, error);
    }
    if (g->options.program != QA_Q1_CTF && g->options.program != QA_Q1_ROGUE)
        return q1_map_fail(error, "This Q1 source does not support item drops");
    if (g->options.program == QA_Q1_ROGUE)
        return q1_rogue_toss(g, player, command->argc > 1 && strcmp(command->argv[1], "ammo"), error);
    qa_q1_drop_input input = {.selected_weapon = g->weapons[player->weapon],
                               .selected_ammo = q1_weapon_ammo(player->weapon) < 0 ? 0
                                     : g->ammo[q1_weapon_ammo(player->weapon)],
                               .view_angles = player->input.view_angles};
    qa_builtin_actor_traits traits = {0};
    if (g->services.actor_traits)
        g->services.actor_traits(g->services.context, actor, &traits);
    float flags = (float)g->options.teamplay;
    if (!q1_source_value(g, QA_Q1_SOURCE_TEAMPLAY, g->options.teamplay, &flags, error))
        return false;
    if (!q1_alive(g, actor) || traits.spectator || !isfinite(flags) ||
        flags < 0 || flags >= (double)UINT32_MAX || !((uint32_t)flags & 128u))
        return true;
    qa_actor_id dropped;
    return command->argc > 1 && !strcmp(command->argv[1], "ammo")
               ? qa_q1_ctf_toss_ammo(g, actor, &input, &dropped, error)
               : qa_q1_ctf_toss_weapon(g, actor, &input, &dropped, error);
}
bool qa_q1_game_console_command(qa_q1_game *g, qa_actor_id actor,
                                 const qa_command_invocation *command, bool *handled,
                                 qa_error *error) {
    if (!g || !command || !handled || (command->argc && !command->argv))
        return q1_map_fail(error, "Invalid Q1 console command invocation");
    *handled = false;
    if (!command->argc)
        return true;
    for (size_t i = 0; i < command->argc; ++i)
        if (!command->argv[i])
            return q1_map_fail(error, "Invalid Q1 console argument");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = dispatch(g, actor, command, handled, error);
    qa_q1_game_operation_end(&operation);
    return ok;
}
static bool legacy_count(qa_q1_game *g, qa_actor_id actor, qa_item_id item, double count,
                           double capacity, qa_error *error) {
    qa_inventory_entry entry;
    qa_error local = {0};
    if (!qa_inventory_entry_read(g->services.inventory, actor, item, &entry, &local)) {
        if (local.code != QA_ERROR_NOT_FOUND) {
            if (error) *error = local;
            return false;
        }
        entry = (qa_inventory_entry){.item = item, .capacity = capacity,
                                     .policy = QA_COUNT_SOURCE_FLOAT};
    }
    entry.count = count;
    return qa_inventory_configure(g->services.inventory, actor, &entry, NULL, NULL, error);
}
static bool cheats_cvar(qa_q1_game *g, bool *enabled, qa_error *error) {
    float value = 0;
    if (!q1_source_value(g, QA_Q1_SOURCE_SV_CHEATS, 0, &value, error))
        return false;
    *enabled = value != 0;
    return true;
}
static bool source_world_impulse(qa_q1_game *g, qa_actor_id actor, uint8_t impulse, bool *handled,
                                 qa_error *error) {
    *handled = false;
    bool hip = g->options.program == QA_Q1_HIPNOTIC, rogue = g->options.program == QA_Q1_ROGUE;
    bool multiplayer = g->options.deathmatch != 0 || g->options.coop;
    bool addon = g->options.program == QA_Q1_DOPA || g->options.program == QA_Q1_MG1 ||
                 g->options.program == QA_Q1_MG3;
    if (addon && impulse == 219) {
        *handled = true;
        return q1_addon_omnicide(g, actor, error);
    }
    if (!q1_map_mg3_impulse(g, actor, impulse, handled, error))
        return false;
    if (*handled)
        return true;
    if (impulse == 11 || (g->options.program == QA_Q1_MG3 && impulse >= 101 && impulse <= 105)) {
        *handled = true;
        if (!g->maps || !g->maps->options.server_flags)
            return q1_map_fail(error, "Q1 rune impulse requires campaign flags");
        uint32_t *flags = g->maps->options.server_flags;
        if (g->options.program == QA_Q1_MG3) {
            if (impulse != 11) {
                *flags |= impulse == 105 ? QA_Q1_MG3_RUNES : 1u << (impulse - 101);
                return true;
            }
            for (unsigned bit = 1; bit <= 8; bit <<= 1)
                if (!(*flags & bit)) {
                    *flags |= bit;
                    return true;
                }
            return q1_developer_message(g, "already has all runes!\n", error);
        }
        double next = (float)((double)*flags * 2 + 1);
        if (next > UINT32_MAX)
            return q1_map_fail(error, "Q1 rune cheat exceeds native campaign flag range");
        *flags = (uint32_t)next;
        return true;
    }
    if (impulse == 255 || (hip && (impulse == 200 || impulse == 201))) {
        *handled = true;
        bool source_cheats = impulse == 255 && g->options.edition == QA_Q1_RERELEASE &&
                             (g->options.program == QA_Q1_ID1 || g->options.program == QA_Q1_CTF);
        bool enabled = false;
        if (source_cheats && !cheats_cvar(g, &enabled, error))
            return false;
        if (source_cheats ? !enabled : multiplayer)
            return true;
        qa_q1_power power = impulse == 200 ? QA_Q1_WETSUIT : impulse == 201 ? QA_Q1_EMPATHY : QA_Q1_QUAD;
        if (!g->host.console_power)
            return q1_map_fail(error, "Q1 power impulse requires selected effects owner");
        if (!g->host.console_power(g->host.context, actor, power, g->time + 30, error))
            return false;
        const char *text = power == QA_Q1_WETSUIT ? "$qc_wetsuit_cheat" : power == QA_Q1_EMPATHY
                           ? "$qc_empathy_cheat" : hip || rogue ? "$qc_quad_cheat" : "quad cheat\n";
        if ((hip || rogue) && g->options.edition == QA_Q1_CLASSIC)
            text = power == QA_Q1_WETSUIT ? "wetsuit cheat\n"
                   : power == QA_Q1_EMPATHY ? "empathy shields cheat\n" : "quad cheat\n";
        if (!q1_alive(g, actor))
            return true;
        return rogue ? q1_developer_message(g, text, error)
                    : q1_message(g, hip ? (qa_actor_id){0} : actor, text, error);
    }
    if (hip && impulse == 205) {
        *handled = true;
        if (multiplayer)
            return true;
        if (!q1_message(g, (qa_actor_id){0}, g->options.edition == QA_Q1_CLASSIC
                                               ? "Genocide!\n" : "$qc_genocide_cheat", error))
            return false;
        qa_builtin_snapshot_frame *entities;
        if (!q1_snapshot_actors(g, &entities, error))
            return false;
        qa_actor_id world = g->maps ? g->maps->world_actor : (qa_actor_id){0};
        bool ok = true;
        for (size_t i = 0; ok && i < entities->snapshot.count && q1_alive(g, actor); ++i) {
            qa_actor_id target = entities->snapshot.ids[i];
            qa_builtin_actor_traits traits = {0};
            if (g->services.actor_traits)
                g->services.actor_traits(g->services.context, target, &traits);
            float health = q1_health(g, target);
            if (traits.monster && health > 0 && q1_alive(g, target))
                ok = q1_damage(g, target, world, world, health + 10, QA_Q1_WEAPON_COUNT, error);
        }
        qa_builtin_snapshot_release(entities);
        return ok;
    }
    if (hip && impulse == 206) {
        *handled = true;
        if (!g->maps || !q1_alive(g, g->maps->world_actor))
            return true;
        g->maps->dump_coordinates = !g->maps->dump_coordinates;
        return !g->maps->dump_coordinates ||
               q1_message(g, (qa_actor_id){0}, "$qc_dump_player_loc", error);
    }
    if (hip && (impulse == 202 || impulse == 203)) {
        *handled = true;
        qa_builtin_snapshot_frame *entities;
        if (!q1_snapshot_actors(g, &entities, error))
            return false;
        qa_actor_id world = g->maps ? g->maps->world_actor : (qa_actor_id){0};
        bool ok = true;
        size_t ordinal = 0;
        for (size_t i = 0; ok && i < entities->snapshot.count && !g->destroy_pending; ++i) {
            qa_actor_id target = entities->snapshot.ids[i];
            if (qa_actor_id_equal(target, world) || !q1_alive(g, target))
                continue;
            ++ordinal;
            if (impulse == 203 && q1_health(g, target) <= 0)
                continue;
            qa_builtin_actor_traits traits = {0};
            if (g->services.actor_traits)
                g->services.actor_traits(g->services.context, target, &traits);
            if (!q1_alive(g, target))
                continue;
            qa_body_state body;
            if (!qa_world_body_read(g->services.world, target, &body, error)) {
                ok = false;
                break;
            }
            if (!q1_alive(g, target))
                continue;
            const char *classname = qa_strings_cstr(qa_session_strings(g->services.session),
                                                    traits.classname);
            char text[512];
            if (impulse == 202)
                snprintf(text, sizeof(text), "%zu %s\n", ordinal, classname ? classname : "");
            else
                snprintf(text, sizeof(text), "%zu %s '%g %g %g'\n--------------------\n",
                         ordinal, classname ? classname : "", body.origin.x, body.origin.y,
                         body.origin.z);
            ok = q1_developer_message(g, text, error);
        }
        qa_builtin_snapshot_release(entities);
        return ok;
    }
    if (g->options.program == QA_Q1_MG3 && impulse >= 222 && impulse <= 224) {
        *handled = true;
        if (!g->maps || !g->maps->options.level || !g->maps->options.server_flags)
            return q1_map_fail(error, "Q1 addon world command requires campaign ownership");
        if (impulse == 222)
            return qa_q1_level_travel(g->maps->options.level, g->maps->options.current_map,
                                      actor, error);
        uint32_t *flags = g->maps->options.server_flags;
        if (impulse == 224) {
            *flags ^= QA_Q1_BLOODY_NIGHTMARE_NEWGAME;
            return true;
        }
        if (*flags & QA_Q1_BLOODY_NIGHTMARE_ACTIVE) {
            *flags &= ~(uint32_t)QA_Q1_BLOODY_NIGHTMARE_ACTIVE;
            return true;
        }
        *flags |= QA_Q1_BLOODY_NIGHTMARE_ACTIVE | QA_Q1_BLOODY_NIGHTMARE_DISCOVERED;
        float skill = (float)g->options.skill;
        if (!q1_source_value(g, QA_Q1_SOURCE_SKILL, g->options.skill, &skill, error))
            return false;
        if (g->destroy_pending || skill == 3)
            return true;
        if (!g->maps->options.server_command || !g->maps->options.set_skill)
            return q1_map_fail(error, "Q1 bloody nightmare requires skill and cvar owners");
        qa_string_id command;
        if (!qa_builtin_resource(&g->services, "skill 3", &command, error) ||
            !g->maps->options.server_command(g->maps->options.context, command, error))
            return false;
        if (g->destroy_pending)
            return true;
        if (!g->maps->options.set_skill(g->maps->options.context, 3, error))
            return false;
        if (!g->destroy_pending)
            g->options.skill = 3;
        return true;
    }
    return true;
}
bool qa_q1_game_map_impulse(qa_q1_game *g, qa_actor_id actor, uint8_t impulse, bool *handled,
                             qa_error *error) {
    if (!g || !handled)
        return q1_map_fail(error, "invalid Q1 world impulse dispatch");
    *handled = false;
    if (!world_impulse(g, impulse))
        return true;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = true;
    if (g->maps && q1_alive(g, g->maps->world_actor) && q1_alive(g, actor))
        ok = source_world_impulse(g, actor, impulse, handled, error);
    if (ok && !qa_q1_game_operation_live(&operation)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q1 source retired during world impulse");
        ok = false;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool q1_source_impulse(qa_q1_game *g, qa_actor_id actor, uint8_t impulse, bool *handled,
                        qa_error *error) {
    if (world_impulse(g, impulse))
        return source_world_impulse(g, actor, impulse, handled, error);
    *handled = false;
    bool addon = g->options.program == QA_Q1_DOPA || g->options.program == QA_Q1_MG1 ||
                 g->options.program == QA_Q1_MG3;
    bool multiplayer = g->options.deathmatch != 0 || g->options.coop;
    bool rogue = g->options.program == QA_Q1_ROGUE;
    if (impulse != 9 && !(addon && impulse == 99))
        return true;
    *handled = true;
    bool enabled = false;
    if (multiplayer && !cheats_cvar(g, &enabled, error))
        return false;
    if (multiplayer && (!enabled || (!addon && g->options.edition == QA_Q1_CLASSIC)))
        return true;
    bool weapons, ammo;
    if (!selected_grant(g, actor, QA_Q1_CHEAT_WEAPONS, &weapons, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    if (!selected_grant(g, actor, QA_Q1_CHEAT_AMMO, &ammo, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    if (!weapons)
        for (unsigned i = 0; i < QA_Q1_WEAPON_COUNT && q1_alive(g, actor); ++i)
            if (supported(g, (qa_q1_weapon)i) &&
                !legacy_count(g, actor, g->weapons[i], 1, 1, error))
                return false;
    if (!ammo) {
        static const double count[] = {100, 200, 100, 200, 200, 100, 100};
        static const double capacity[] = {100, 200, 100, 100, 200, 100, 100};
        for (unsigned i = 0; i < (rogue ? QA_Q1_AMMO_COUNT : 4) && q1_alive(g, actor); ++i)
            if (!legacy_count(g, actor, g->ammo[i], count[i], capacity[i], error))
                return false;
    }
    if (impulse != 99) {
        const char *keys[] = {"q1:key/silver", "q1:key/gold"};
        for (unsigned i = 0; i < 2 && q1_alive(g, actor); ++i) {
            qa_string_id item;
            if (!qa_builtin_resource(&g->services, keys[i], &item, error) ||
                !legacy_count(g, actor, item, 1, 1, error))
                return false;
        }
    }
    if (g->options.edition == QA_Q1_RERELEASE &&
        (g->options.program == QA_Q1_ID1 || g->options.program == QA_Q1_CTF) && q1_alive(g, actor)) {
        const char *argv[] = {"give", "armor"};
        qa_command_invocation armor = {.argc = 2, .argv = argv};
        if (!give(g, actor, &armor, error))
            return false;
    }
    return weapons || !q1_alive(g, actor) || qa_q1_player_select(g, actor, QA_Q1_ROCKET, error);
}
