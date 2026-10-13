#include "internal.h"
#include "qa/text.h"
#include <stdio.h>

enum { MODE_CHAT_BYTES = 1024 };
typedef struct chat_text {
    char bytes[MODE_CHAT_BYTES];
    size_t used;
} chat_text;
typedef struct chat_location_rule {
    mode_name classname;
    int priority;
} chat_location_rule;
static const chat_location_rule location_rules[] = {
    {MODE_NAME_ITEM_FLAG_TEAM1, 1}, {MODE_NAME_ITEM_FLAG_TEAM2, 1},
    {MODE_NAME_ITEM_QUAD, 2}, {MODE_NAME_ITEM_INVULNERABILITY, 2},
    {MODE_NAME_WEAPON_BFG, 3}, {MODE_NAME_WEAPON_RAILGUN, 4},
    {MODE_NAME_WEAPON_ROCKETLAUNCHER, 4}, {MODE_NAME_WEAPON_HYPERBLASTER, 4},
    {MODE_NAME_WEAPON_CHAINGUN, 4}, {MODE_NAME_WEAPON_GRENADELAUNCHER, 4},
    {MODE_NAME_WEAPON_MACHINEGUN, 4}, {MODE_NAME_WEAPON_SUPERSHOTGUN, 4},
    {MODE_NAME_WEAPON_SHOTGUN, 4}, {MODE_NAME_ITEM_POWER_SCREEN, 5},
    {MODE_NAME_ITEM_POWER_SHIELD, 5}, {MODE_NAME_ITEM_ARMOR_BODY, 6},
    {MODE_NAME_ITEM_ARMOR_COMBAT, 6}, {MODE_NAME_ITEM_ARMOR_JACKET, 6},
    {MODE_NAME_ITEM_SILENCER, 7}, {MODE_NAME_ITEM_BREATHER, 7},
    {MODE_NAME_ITEM_ENVIRO, 7}, {MODE_NAME_ITEM_ADRENALINE, 7},
    {MODE_NAME_ITEM_BANDOLIER, 8}, {MODE_NAME_ITEM_PACK, 8}
};

static void text_append(chat_text *out, const char *text) {
    size_t length = strlen(text);
    if (length > sizeof(out->bytes) - 1 - out->used)
        length = sizeof(out->bytes) - 1 - out->used;
    memcpy(out->bytes + out->used, text, length);
    out->used += length;
    out->bytes[out->used] = 0;
}
static bool chat_item(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_item_id item,
                      qa_string_id *classname, const char **label) {
    *classname = 0;
    *label = NULL;
    mode_object *object = item ? NULL : mode_object_get(m, actor);
    if (object && object->spec.kind == QA_MODE_OBJECT_FLAG &&
        object->mode.slot == v->id.slot && object->mode.generation == v->id.generation) {
        int team = mode_team_index(v, object->spec.team);
        if (team == 0 || team == 1) {
            *classname = m->runtime_names[team == 0 ? MODE_NAME_ITEM_FLAG_TEAM1 :
                MODE_NAME_ITEM_FLAG_TEAM2];
            *label = qa_strings_cstr(qa_session_strings(m->options.services.session),
                m->runtime_names[team == 0 ? MODE_NAME_LABEL_RED_FLAG : MODE_NAME_LABEL_BLUE_FLAG]);
            return true;
        }
    }
    return m->options.hooks.chat_item && MODE_CALLBACK(m, m->options.hooks.chat_item(
        m->options.hooks.context, v->id, actor, item, classname, label));
}
static size_t location_rule(qa_modes *m, qa_string_id classname) {
    for (size_t i = 0; i < sizeof(location_rules) / sizeof(*location_rules); ++i)
        if (m->runtime_names[location_rules[i].classname] == classname)
            return i;
    return SIZE_MAX;
}
static bool chat_location(qa_modes *m, mode_instance *v, qa_actor_id actor,
                          chat_text *out, qa_error *e) {
    qa_body_state who;
    if (!qa_world_body_read(m->options.services.world, actor, &who, NULL)) {
        text_append(out, "nowhere");
        return true;
    }
    qa_world_actor_snapshot nearby = {0};
    qa_world_radius_query query = {.origin = who.origin, .radius = 1024,
        .metric = QA_WORLD_RADIUS_BOUNDS_CENTER, .read_policy = QA_WORLD_RADIUS_SKIP_MISSING};
    if (!qa_world_radius_snapshot_capture(m->options.services.world, &query, &nearby, e))
        return false;
    qa_builtin_sort_observations(&m->options.services, &nearby);
    qa_actor_id hot = {0};
    qa_string_id hotclass = 0;
    float hotdist = 999999;
    int hotindex = 999;
    bool hotsee = false;
    for (size_t i = 0; i < nearby.count; ++i) {
        qa_actor_id candidate = nearby.actors[i];
        qa_string_id classname;
        const char *label;
        qa_body_state body;
        if (!chat_item(m, v, candidate, 0, &classname, &label) || !label ||
            !qa_world_body_read(m->options.services.world, candidate, &body, NULL))
            continue;
        size_t rule = location_rule(m, classname);
        if (rule == SIZE_MAX)
            continue;
        bool visible = mode_visible(m, actor, candidate, false);
        if (visible && !hotsee) {
            hotsee = true;
            hotindex = location_rules[rule].priority;
            hot = candidate;
            hotclass = classname;
            hotdist = qa_vec_length(qa_vec_sub(body.origin, who.origin));
            continue;
        }
        if ((hotsee && !visible) || (hotsee && hotindex < location_rules[rule].priority))
            continue;
        float distance = qa_vec_length(qa_vec_sub(body.origin, who.origin));
        if (distance < hotdist || (visible && location_rules[rule].priority < hotindex)) {
            hot = candidate;
            hotclass = classname;
            hotdist = distance;
            hotindex = (int)rule;
            hotsee = mode_visible(m, actor, hot, false);
        }
    }
    qa_world_snapshot_release(&nearby);
    qa_body_state body;
    qa_string_id classname;
    const char *label;
    if (!hot.registry || !chat_item(m, v, hot, 0, &classname, &label) || !label ||
        !qa_world_body_read(m->options.services.world, hot, &body, NULL)) {
        text_append(out, "nowhere");
        return true;
    }
    qa_world_actor_snapshot all = {0};
    if (!qa_builtin_observations(&m->options.services, &all, e))
        return false;
    qa_builtin_sort_observations(&m->options.services, &all);
    qa_actor_id flag[2] = {{0}};
    bool duplicate = false;
    for (size_t i = 0; i < all.count; ++i) {
        qa_actor_id candidate = all.actors[i];
        const char *candidate_label;
        if (!chat_item(m, v, candidate, 0, &classname, &candidate_label))
            continue;
        if (!qa_actor_id_equal(candidate, hot) && classname == hotclass)
            duplicate = true;
        if (!flag[0].registry && classname == m->runtime_names[MODE_NAME_ITEM_FLAG_TEAM1])
            flag[0] = candidate;
        if (!flag[1].registry && classname == m->runtime_names[MODE_NAME_ITEM_FLAG_TEAM2])
            flag[1] = candidate;
    }
    qa_world_snapshot_release(&all);
    int nearteam = -1;
    qa_body_state red, blue;
    if (duplicate && flag[0].registry && flag[1].registry &&
        qa_world_body_read(m->options.services.world, flag[0], &red, NULL) &&
        qa_world_body_read(m->options.services.world, flag[1], &blue, NULL)) {
        float red_distance = qa_vec_length(qa_vec_sub(body.origin, red.origin));
        float blue_distance = qa_vec_length(qa_vec_sub(body.origin, blue.origin));
        if (red_distance < blue_distance) nearteam = 0;
        else if (red_distance > blue_distance) nearteam = 1;
    }
    qa_physics *physics = m->options.services.physics;
    qa_physics_properties properties;
    if (physics && physics->services.read &&
        physics->services.read(physics->services.context, actor, &properties) &&
        properties.water_level)
        text_append(out, "in the water ");
    qa_vec3 delta = qa_vec_sub(who.origin, body.origin);
    if (fabsf(delta.z) > fabsf(delta.x) && fabsf(delta.z) > fabsf(delta.y))
        text_append(out, delta.z > 0 ? "above " : "below ");
    else
        text_append(out, "near ");
    text_append(out, nearteam == 0 ? "the red " : nearteam == 1 ? "the blue " : "the ");
    text_append(out, label);
    return true;
}
static void chat_armor(qa_modes *m, mode_instance *v, qa_actor_id actor, chat_text *out) {
    qa_combat_state state;
    if (!qa_combat_read(m->options.services.combat, actor, &state, NULL)) {
        text_append(out, "no armor");
        return;
    }
    char number[80];
    qa_powered_armor power = state.armor.powered;
    if (power.kind != QA_POWER_NONE && power.cells) {
        snprintf(number, sizeof(number), "%s with %i cells ",
            power.kind == QA_POWER_SCREEN ? "Power Screen" : "Power Shield",
            qa_source_to_i32(power.cells));
        text_append(out, number);
    }
    qa_regular_armor armor = state.armor.regular;
    if (armor.item && armor.points) {
        qa_item_definition definition;
        bool found = false;
        const char *label = NULL;
        qa_string_id classname;
        if (qa_inventory_item_definition_find(m->options.services.inventory, actor, NULL,
                armor.item, &definition, &found, NULL) && found)
            label = definition.label;
        if (!label)
            chat_item(m, v, actor, armor.item, &classname, &label);
        if (label) {
            if (out->used) text_append(out, "and ");
            snprintf(number, sizeof(number), "%i units of ", qa_source_to_i32(armor.points));
            text_append(out, number);
            text_append(out, label);
        }
    }
    if (!out->used)
        text_append(out, "no armor");
}
static void chat_health(qa_modes *m, qa_actor_id actor, chat_text *out) {
    qa_combat_state state;
    if (!qa_combat_read(m->options.services.combat, actor, &state, NULL) || state.health <= 0)
        text_append(out, "dead");
    else {
        char number[32];
        snprintf(number, sizeof(number), "%i health", qa_source_to_i32(state.health));
        text_append(out, number);
    }
}
static void chat_tech(qa_modes *m, mode_instance *v, qa_actor_id actor, chat_text *out) {
    static const mode_name labels[] = {MODE_NAME_LABEL_DISRUPTOR_SHIELD,
        MODE_NAME_LABEL_POWER_AMPLIFIER, MODE_NAME_LABEL_TIME_ACCEL, MODE_NAME_LABEL_AUTODOC};
    for (size_t i = 0; i < sizeof(labels) / sizeof(*labels); ++i)
        if (qa_modes_has_relic(m, v->id, actor, (qa_relic_kind)i)) {
            text_append(out, "the ");
            text_append(out, qa_strings_cstr(qa_session_strings(m->options.services.session),
                m->runtime_names[labels[i]]));
            return;
        }
    text_append(out, "no powerup");
}
static bool chat_weapon(qa_modes *m, mode_instance *v, qa_actor_id actor, chat_text *out,
                        qa_error *e) {
    qa_item_id item = 0;
    uint64_t powerups;
    if (m->options.hooks.team_equipment && !MODE_CALLBACK(m,
            m->options.hooks.team_equipment(m->options.hooks.context, actor, &item, &powerups, e)))
        return false;
    qa_item_definition definition;
    bool found = false;
    const char *label = NULL;
    qa_string_id classname;
    if (item && qa_inventory_item_definition_find(m->options.services.inventory, actor, NULL,
            item, &definition, &found, NULL) && found)
        label = definition.label;
    if (item && !label)
        chat_item(m, v, actor, item, &classname, &label);
    text_append(out, label ? label : "none");
    return true;
}
static bool chat_sight(qa_modes *m, qa_actor_id actor, chat_text *out, qa_error *e) {
    qa_world_actor_snapshot players = {0};
    if (!qa_builtin_players(&m->options.services, &players, e))
        return false;
    qa_string_id previous = 0;
    size_t count = 0;
    qa_strings *strings = qa_session_strings(m->options.services.session);
    for (size_t i = 0; i < players.count; ++i) {
        qa_actor_id target = players.actors[i];
        mode_player *player = mode_player_get(m, target);
        if (!player || !player->connected || qa_actor_id_equal(actor, target) ||
            !mode_visible(m, actor, target, false))
            continue;
        if (previous) {
            const char *name = qa_strings_cstr(strings, previous);
            if (out->used + strlen(name) + 3 < sizeof(out->bytes)) {
                if (count) text_append(out, ", ");
                text_append(out, name);
            }
            count++;
        }
        previous = player->identity->name;
    }
    qa_world_snapshot_release(&players);
    if (previous) {
        const char *name = qa_strings_cstr(strings, previous);
        if (out->used + strlen(name) + 6 < sizeof(out->bytes)) {
            if (count) text_append(out, " and ");
            text_append(out, name);
        }
    } else
        text_append(out, "no one");
    return true;
}
bool qa_modes_team_chat(qa_modes *m, qa_mode_id id, qa_actor_id actor, const char *text,
                        bool *handled, qa_error *e) {
    *handled = false;
    mode_instance *v = mode_get(m, id);
    mode_player *sender = mode_player_get(m, actor);
    if (!v || !sender || !mode_member_get(m, v, actor) || !v->value.rules.enabled ||
        v->value.rules.source != QA_MODE_Q2_CTF)
        return true;
    *handled = true;
    bool allowed = true;
    if (m->options.hooks.chat_allowed && !MODE_CALLBACK(m,
            m->options.hooks.chat_allowed(m->options.hooks.context, id, actor, &allowed, e)))
        return false;
    if (!allowed || !mode_live(m, actor))
        return true;
    qa_team_id team;
    if (!qa_modes_team(m, id, actor, &team, NULL))
        return true;
    size_t length = strlen(text);
    if (length && *text == '"') {
        text++;
        length--;
        if (length && text[length - 1] == '"') length--;
    }
    chat_text expanded = {0};
    for (size_t i = 0; i < length && expanded.used < sizeof(expanded.bytes) - 1; ++i) {
        if (text[i] != '%') {
            char byte[] = {text[i], 0};
            text_append(&expanded, byte);
            continue;
        }
        if (i + 1 == length)
            break;
        chat_text macro = {0};
        unsigned char code = (unsigned char)text[++i];
        if (code >= 'A' && code <= 'Z') code += 'a' - 'A';
        switch (code) {
        case 'l': if (!chat_location(m, v, actor, &macro, e)) return false; break;
        case 'a': chat_armor(m, v, actor, &macro); break;
        case 'h': chat_health(m, actor, &macro); break;
        case 't': chat_tech(m, v, actor, &macro); break;
        case 'w': if (!chat_weapon(m, v, actor, &macro, e)) return false; break;
        case 'n': if (!chat_sight(m, actor, &macro, e)) return false; break;
        default: macro.bytes[0] = text[i]; macro.used = 1; break;
        }
        text_append(&expanded, macro.bytes);
    }
    qa_world_actor_snapshot players = {0};
    if (!qa_builtin_players(&m->options.services, &players, e))
        return false;
    qa_builtin_message_arg arguments[] = {
        {.kind = QA_BUILTIN_MESSAGE_STRING, .value.text = sender->identity->name},
        {.kind = QA_BUILTIN_MESSAGE_TEXT, .value.literal = expanded.bytes}
    };
    bool okay = true;
    for (size_t i = 0; i < players.count; ++i) {
        qa_actor_id recipient = players.actors[i];
        mode_player *player = mode_player_get(m, recipient);
        qa_team_id current;
        if (!player || !player->connected || !mode_member_get(m, v, recipient) ||
            !qa_modes_team(m, id, recipient, &current, NULL) || current != team)
            continue;
        qa_builtin_event message = {.kind = QA_BUILTIN_MESSAGE, .actor = recipient, .code = 3,
            .text = m->runtime_names[MODE_NAME_CHAT_TEAM], .arguments = arguments,
            .argument_count = sizeof(arguments) / sizeof(*arguments)};
        if (!mode_builtin_emit(m, v, &message, e)) {
            okay = false;
            break;
        }
    }
    qa_world_snapshot_release(&players);
    return okay;
}
