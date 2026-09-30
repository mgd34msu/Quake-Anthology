#include "internal.h"

typedef struct item_row {
    const char *classname, *name, *icon;
    qa_mode_object_kind kind;
    qa_relic_kind relic;
    unsigned team;
    int32_t capacity;
    uint32_t actions;
    qa_mode_console_give policy;
} item_row;
static const item_row ctf_items[] = {
    {"item_flag_team1", "Red Flag", "i_ctf1", QA_MODE_OBJECT_FLAG, 0, 0, 1, 0, QA_MODE_GIVE_INDIVIDUAL_ONLY},
    {"item_flag_team2", "Blue Flag", "i_ctf2", QA_MODE_OBJECT_FLAG, 0, 1, 1, 0, QA_MODE_GIVE_INDIVIDUAL_ONLY},
    {"item_tech1", "Disruptor Shield", "tech1", QA_MODE_OBJECT_RELIC, QA_RELIC_RESISTANCE, 0, 1, QA_ITEM_DROP, QA_MODE_GIVE_INDIVIDUAL_ONLY},
    {"item_tech2", "Power Amplifier", "tech2", QA_MODE_OBJECT_RELIC, QA_RELIC_STRENGTH, 0, 1, QA_ITEM_DROP, QA_MODE_GIVE_INDIVIDUAL_ONLY},
    {"item_tech3", "Time Accel", "tech3", QA_MODE_OBJECT_RELIC, QA_RELIC_HASTE, 0, 1, QA_ITEM_DROP, QA_MODE_GIVE_INDIVIDUAL_ONLY},
    {"item_tech4", "AutoDoc", "tech4", QA_MODE_OBJECT_RELIC, QA_RELIC_REGENERATION, 0, 1, QA_ITEM_DROP, QA_MODE_GIVE_INDIVIDUAL_ONLY}
};
static const item_row lm_items[] = {
    {"flag", "Enemy Flag", "a_redflag", QA_MODE_OBJECT_FLAG, 0, 0, 32767, QA_ITEM_USE, QA_MODE_GIVE_PICKUP},
    {"damage_rune", "Damage Artifact", "a_strength", QA_MODE_OBJECT_RELIC, QA_RELIC_STRENGTH, 0, 1, QA_ITEM_USE, QA_MODE_GIVE_PICKUP},
    {"haste_rune", "Haste Artifact", "a_haste", QA_MODE_OBJECT_RELIC, QA_RELIC_HASTE, 0, 1, QA_ITEM_USE, QA_MODE_GIVE_PICKUP},
    {"resist_rune", "Resist Artifact", "a_resist", QA_MODE_OBJECT_RELIC, QA_RELIC_RESISTANCE, 0, 1, QA_ITEM_USE, QA_MODE_GIVE_PICKUP},
    {"regen_rune", "Regen Artifact", "a_regen", QA_MODE_OBJECT_RELIC, QA_RELIC_REGENERATION, 0, 1, QA_ITEM_USE, QA_MODE_GIVE_PICKUP},
    {"vampire_rune", "Vampire Artifact", "k_redkey", QA_MODE_OBJECT_RELIC, QA_RELIC_VAMPIRE, 0, 1, QA_ITEM_USE, QA_MODE_GIVE_PICKUP}
};
static const item_row tag_item = {
    "dm_tag_token", "Tag Token", "i_tagtoken", QA_MODE_OBJECT_TAG, 0, 0, 32767, 0, QA_MODE_GIVE_FORBIDDEN
};
static const item_row *rows(mode_instance *v, size_t *count) {
    *count = 0;
    if (!v || !v->value.rules.enabled)
        return NULL;
    if (v->value.rules.source == QA_MODE_Q2_CTF) {
        *count = sizeof(ctf_items) / sizeof(ctf_items[0]);
        return ctf_items;
    }
    if (v->value.rules.source == QA_MODE_LMCTF) {
        *count = sizeof(lm_items) / sizeof(lm_items[0]);
        return lm_items;
    }
    if ((v->value.rules.source == QA_MODE_Q2 || v->value.rules.source == QA_MODE_Q2_TAG) &&
        v->value.rules.kind == QA_MODE_TAG) {
        *count = 1;
        return &tag_item;
    }
    return NULL;
}
size_t qa_modes_item_count(qa_modes *m, qa_mode_id id) {
    size_t count;
    rows(mode_get(m, id), &count);
    return count;
}
static bool row_id(qa_modes *m, const item_row *row, qa_item_id *out, qa_error *e) {
    char text[64] = "q2:";
    size_t length = strlen(row->classname);
    memcpy(text + 3, row->classname, length + 1);
    return qa_builtin_resource(&m->options.services, text, out, e);
}
bool qa_modes_item_at(qa_modes *m, qa_mode_id id, size_t index, qa_mode_item *out, qa_error *e) {
    size_t count;
    const item_row *list = rows(mode_get(m, id), &count);
    if (!out || index >= count)
        return mode_fail(e, "mode item index outside selected catalog");
    const item_row *row = &list[index];
    qa_item_id item, source;
    if (!row_id(m, row, &source, e) ||
        !mode_inventory_item(m, mode_get(m, id), source, &item, e))
        return false;
    *out = (qa_mode_item){.mode = id, .source_item = source,
        .definition = {.item = item, .owner = m->options.owner,
        .label = row->name, .actions = row->actions}, .classname = row->classname,
        .capacity = row->capacity, .console_give = row->policy};
    return true;
}

typedef struct give_call {
    qa_modes *modes;
    qa_mode_id mode;
    qa_actor_id object, actor;
    const item_row *row;
} give_call;
static bool give_eligible(void *context, const qa_pickup_offer *offer, bool *yes, qa_error *e) {
    give_call *call = context;
    qa_modes *m = call->modes;
    mode_player *p = mode_player_get(m, offer->recipient);
    mode_instance *v = mode_get(m, call->mode);
    mode_member *member = mode_member_get(m, v, offer->recipient);
    *yes = p && member && !member->player.spectator && v->value.rules.enabled &&
        mode_alive(m, offer->recipient);
    (void)e;
    return true;
}
static bool give_original(void *context, const qa_pickup_offer *offer, bool *accepted, qa_error *e) {
    give_call *call = context;
    qa_modes *m = call->modes;
    mode_object *o = mode_object_get(m, call->object);
    mode_instance *v = mode_get(m, call->mode);
    *accepted = false;
    if (!o || !v || !mode_member_get(m, v, offer->recipient))
        return true;
    switch (o->spec.kind) {
    case QA_MODE_OBJECT_FLAG: return mode_flag_touch(m, v, o, offer->recipient, accepted, e);
    case QA_MODE_OBJECT_RELIC: return mode_relic_touch(m, v, o, offer->recipient, accepted, e);
    case QA_MODE_OBJECT_TAG: return mode_tag_touch(m, v, o, offer->recipient, accepted, e);
    default: return mode_fail(e, "command item has no source pickup");
    }
}
static bool give_complete(void *context, const qa_pickup_offer *offer, bool accepted, qa_error *e) {
    give_call *call = context;
    qa_modes *m = call->modes;
    if (!accepted || !mode_live(m, offer->recipient))
        return true;
    qa_string_id icon, name;
    if (!qa_builtin_resource(&m->options.services, call->row->icon, &icon, e) ||
        !qa_builtin_resource(&m->options.services, call->row->name, &name, e))
        return false;
    mode_instance *v = mode_get(m, call->mode);
    if (!v) return true;
    qa_builtin_event event = {.kind = QA_BUILTIN_ITEM,
        .family = QA_GAME_Q2, .provider = m->options.owner, .actor = offer->recipient,
        .other = offer->pickup, .resource = icon, .text = name, .time_ns = offer->time_ns};
    if (!MODE_CALLBACK(m, m->options.hooks.emit
            ? m->options.hooks.emit(m->options.hooks.context, v->id, &event, e)
            : qa_builtin_emit(&m->options.services, &event, e)))
        return false;
    return !v || !mode_live(m, offer->recipient) || mode_sound(m, v, offer->recipient,
        call->row->kind == QA_MODE_OBJECT_FLAG ? "misc/am_pkup.wav" : "items/pkup.wav", 1, e);
}
static bool give_item(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_item_id item,
                      bool direct, int32_t count, bool *accepted, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !accepted || !mode_member_get(m, v, actor))
        return mode_fail(e, "mode command grant needs an admitted player");
    *accepted = false;
    size_t length;
    const item_row *list = rows(v, &length), *row = NULL;
    qa_item_id source = 0;
    for (size_t i = 0; i < length; ++i) {
        qa_item_id candidate, inventory;
        if (!row_id(m, &list[i], &candidate, e) ||
            !mode_inventory_item(m, v, candidate, &inventory, e))
            return false;
        if (inventory == item) { row = &list[i]; source = candidate; break; }
    }
    if (!row)
        return mode_fail(e, "unknown selected mode command item");
    qa_actor_id object = {0};
    bool temporary = true;
    if (row->kind == QA_MODE_OBJECT_TAG && mode_live(m, v->tag)) {
        mode_object *tag = mode_object_get(m, v->tag);
        if (!tag || tag->value.phase == QA_OBJECTIVE_CARRIED)
            return true;
        object = v->tag;
        temporary = false;
    } else {
        qa_body_state body;
        if (!qa_world_body_read(m->options.services.world, actor, &body, e))
            return false;
        qa_team_id team = v->value.rules.teams[row->team];
        if (v->value.rules.source == QA_MODE_LMCTF && row->kind == QA_MODE_OBJECT_FLAG) {
            qa_team_id player_team;
            if (!qa_modes_team(m, v->id, actor, &player_team, e))
                return false;
            int own = mode_team_index(v, player_team);
            if (own < 0 || own > 1)
                return true;
            team = v->value.rules.teams[count == 1 || count == 2 ? count - 1 : 1 - own];
        }
        qa_mode_object_spec spec = {.kind = row->kind, .relic = row->relic,
            .team = row->kind == QA_MODE_OBJECT_FLAG ? team : 0, .item = source,
            .origin = body.origin, .command_created = true, .suspended = true};
        if (!qa_modes_spawn_object(m, id, &spec, &object, e))
            return false;
        mode_object *o = mode_object_get(m, object);
        o->expire_ns = o->next_ns = 0;
        o->dropped = true;
    }
    give_call call = {m, id, object, actor, row};
    qa_pickup_offer offer = {.recipient = actor, .pickup = object, .source = m->options.owner,
        .item = item, .override_count = count != 0, .count = count, .dropped = true,
        .time_ns = v->value.time_ns, .grant = QA_PICKUP_MAP_COUPLED};
    bool ok;
    if (direct)
        ok = give_original(&call, &offer, accepted, e);
    else if (!m->options.services.pickups)
        ok = mode_fail(e, "mode command pickup requires shared pickup admission");
    else {
        qa_pickup_continuation continuation = {&call, give_eligible, give_original, give_complete};
        qa_pickup_outcome outcome;
        ok = qa_pickups_touch(m->options.services.pickups, &offer, &continuation, &outcome, e);
        if (ok) *accepted = outcome == QA_PICKUP_ACCEPTED;
    }
    mode_object *o = mode_object_get(m, object);
    if (o && o->value.phase == QA_OBJECTIVE_CARRIED) {
        if (row->kind == QA_MODE_OBJECT_TAG)
            v->tag = object;
    } else if (temporary && mode_live(m, object)) {
        qa_error cleanup = {0};
        if (!qa_session_release(m->options.services.session, object, &cleanup) && ok) {
            if (e) *e = cleanup;
            ok = false;
        }
    }
    return ok && (!*accepted || !mode_live(m, actor) || qa_modes_publish_items(m, actor, e));
}
bool qa_modes_give_item(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_item_id item,
                        bool direct, int32_t count, bool *accepted, qa_error *e) {
    if (!m)
        return mode_fail(e, "invalid mode command item service");
    return MODE_CALLBACK(m, give_item(m, id, actor, item, direct, count, accepted, e));
}
