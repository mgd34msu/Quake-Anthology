#include "original_items.h"
#include "original_edicts.h"
#include "qa/text.h"

static bool item_error(qa_error *error, size_t offset, const char *message)
{
    qa_error_set(error, QA_ERROR_UNSUPPORTED, offset, "%s", message);
    return false;
}

static const char *const item_thinks[] = {
    NULL, "droptofloor", "DoRespawn", "drop_make_touchable", "G_FreeEdict", "MegaHealth_think"
};

/* g_save.cpp writes bit zero first and trims trailing zeroes. Native pickup
 * slots use the same physical-client zero-based index. */
static bool picked_slots(q2_original_record_io *io, q2_item_state *state, uint32_t clients)
{
    if (io->edition != QA_Q2_RERELEASE) return true;
    if (io->reading) {
        qa_json_id id = qa_json_get(io->document, io->object, "item_picked_up_by");
        qa_buffer text = {0};
        if (id != QA_JSON_NONE && !qa_json_string(io->document, id, &text, io->error)) return false;
        if (text.size > clients) {
            qa_buffer_free(&text);
            qa_error_set(io->error, QA_ERROR_FORMAT, id, "Original Q2 pickup bitset exceeds its clients");
            return false;
        }
        size_t count = 0;
        for (size_t i = 0; i < text.size; ++i) {
            if (text.data[i] == '1') ++count;
            else if (text.data[i] != '0') {
                qa_buffer_free(&text);
                qa_error_set(io->error, QA_ERROR_FORMAT, id, "Original Q2 pickup bitset has an invalid bit");
                return false;
            }
        }
        uint32_t *slots = count ? malloc(count * sizeof(*slots)) : NULL;
        if (count && !slots) {
            qa_buffer_free(&text);
            qa_error_set(io->error, QA_ERROR_MEMORY, count, "Restoring original Q2 pickup clients");
            return false;
        }
        size_t next = 0;
        for (size_t i = 0; i < text.size; ++i)
            if (text.data[i] == '1') slots[next++] = (uint32_t)i;
        qa_buffer_free(&text);
        free(state->picked_slots);
        state->picked_slots = slots; state->picked_count = state->picked_capacity = count;
        return true;
    }
    uint32_t extent = 0;
    for (size_t i = 0; i < state->picked_count; ++i) {
        if (state->picked_slots[i] >= clients) {
            qa_error_set(io->error, QA_ERROR_FORMAT, i, "Q2 pickup has no original physical client");
            return false;
        }
        if (state->picked_slots[i] >= extent) extent = state->picked_slots[i] + 1;
    }
    if (!extent) return true;
    char *text = malloc((size_t)extent + 1);
    if (!text) {
        qa_error_set(io->error, QA_ERROR_MEMORY, extent, "Saving original Q2 pickup clients");
        return false;
    }
    memset(text, '0', extent); text[extent] = 0;
    for (size_t i = 0; i < state->picked_count; ++i) text[state->picked_slots[i]] = '1';
    qa_json_writer_key(io->writer, "item_picked_up_by");
    qa_json_writer_string(io->writer, text);
    free(text);
    if (io->writer->failed) { if (io->error) *io->error = io->writer->failure; return false; }
    return true;
}

static bool companion_callback(qa_q2_game *game, q2_original_record_io *io,
    const char *field, uint16_t offset, const char *name)
{
    if (!io->reading) return q2_original_function(game, io, field, offset, name);
    bool matches;
    if (!q2_original_function_matches(game, io, field, offset, name, &matches)) return false;
    return matches || item_error(io->error, offset,
        "Original Q2 companion has a different Source callback");
}

static bool companion_record(qa_q2_game *game, q2_original_record_io *io,
    q2_actor *actor, const char *classname, bool *handled, qa_error *error)
{
    q2_companion_kind kind = actor->item && actor->item->companion ?
        actor->item->companion->kind : Q2_COMPANION_NONE;
    uint32_t flags = 0;
    if (io->reading && kind == Q2_COMPANION_NONE) {
        if (classname && !strcmp(classname, "sphere")) {
            if (!q2_original_scalar(io, "spawnflags", Q2_ORIGINAL_U32, 284, 284, 284, &flags)) return false;
            switch (flags & 255u) {
            case 1: kind = Q2_SPHERE_DEFENDER; break;
            case 2: kind = Q2_SPHERE_HUNTER; break;
            case 4: kind = Q2_SPHERE_VENGEANCE; break;
            default: return item_error(error, 284, "Original Q2 sphere has an invalid Source kind");
            }
        } else if (classname && !strcmp(classname, "doppleganger")) kind = Q2_DOPPLEGANGER;
        else {
            bool body;
            if (!q2_original_function_matches(game, io, "think", 436, "body_think", &body)) return false;
            if (body) kind = Q2_DOPPLEGANGER_BODY;
        }
    }
    *handled = kind != Q2_COMPANION_NONE;
    if (!*handled) return true;
    if (io->edition == QA_Q2_CLASSIC && io->product != QA_Q2_ROGUE)
        return item_error(error, 436, "Original Q2 companion is not part of this Source GAME");
    if (!actor->item) {
        qa_q2_item_checkpoint state = {.present = true,
            .visual = {.alpha = 1, .scale = 1}, .companion = {.kind = (uint32_t)kind}};
        if (!qa_q2_item_restore(game, actor->id, &state, error)) return false;
    }
    q2_companion *companion = actor->item->companion;
    if (!companion || companion->kind != kind)
        return item_error(error, 436, "Original Q2 companion has a different native continuation");
    const char *think = kind == Q2_SPHERE_DEFENDER ? "defender_think" :
        kind == Q2_SPHERE_HUNTER ? "hunter_think" :
        kind == Q2_SPHERE_VENGEANCE ? "vengeance_think" :
        kind == Q2_DOPPLEGANGER ? "doppleganger_timeout" : "body_think";
    if (!io->references_only && !companion_callback(game, io, "think", 436, think)) return false;
    if (kind <= Q2_SPHERE_VENGEANCE) {
        if (!io->reading) flags = (kind == Q2_SPHERE_DEFENDER ? 1u :
            kind == Q2_SPHERE_HUNTER ? 2u : 4u) | (companion->decoy ? 256u : 0);
        if (!io->references_only &&
            !q2_original_scalar(io, "spawnflags", Q2_ORIGINAL_U32, 284, 284, 284, &flags)) return false;
        if (io->reading && !io->references_only) companion->decoy = (flags & 256u) != 0;
        qa_actor_id credit = io->reading || companion->decoy ? companion->credit : (qa_actor_id){0};
        if (!q2_original_reference(game, io, "owner", 256, &companion->owner) ||
            !q2_original_reference(game, io, "teammaster", 564, &credit) ||
            !q2_original_reference(game, io, "enemy", 540, &companion->enemy)) return false;
        if (io->reading) {
            companion->active = kind != Q2_SPHERE_DEFENDER && companion->enemy.registry != 0;
            companion->credit = companion->decoy ? credit : companion->owner;
            actor->item->owner = companion->decoy ? companion->credit : companion->owner;
            q2_actor *owner = q2_actor_get(game, companion->owner, false, NULL);
            if (owner && owner->client) {
                if (owner->powers) owner->powers->sphere = actor->id;
                companion->camera = kind == Q2_SPHERE_HUNTER && owner->client->sphere_vehicle;
                if (companion->camera) owner->client->sphere_camera = actor->id;
            }
        }
        if (io->references_only) return true;
        if (!q2_original_seconds(io, "wait", 592, &companion->expires_ns) ||
            !q2_original_scalar(io, "monsterinfo.attack_finished", Q2_ORIGINAL_TIME,
                832, 832, 832, &companion->attack_ns) ||
            !q2_original_scalar(io, "monsterinfo.saved_goal", Q2_ORIGINAL_VECTOR,
                836, 836, 836, &companion->goal)) return false;
        const char *pain = kind == Q2_SPHERE_DEFENDER ? "defender_pain" :
            kind == Q2_SPHERE_HUNTER ? "hunter_pain" : "vengeance_pain";
        if (!companion_callback(game, io, "pain", 452, pain) ||
            !companion_callback(game, io, "die", 456,
                kind == Q2_SPHERE_DEFENDER ? "sphere_explode" : "sphere_if_idle_die") ||
            !companion_callback(game, io, "touch", 444, companion->active ?
                kind == Q2_SPHERE_HUNTER ? "hunter_touch" : "vengeance_touch" : NULL)) return false;
    } else {
        if (!q2_original_reference(game, io, "teammaster", 564, &companion->owner)) return false;
        if (kind == Q2_DOPPLEGANGER) {
            if (!q2_original_reference(game, io, "enemy", 540, &companion->enemy) ||
                !q2_original_reference(game, io, "teamchain", 560, &companion->child)) return false;
            if (io->reading) { companion->credit = companion->owner; actor->item->owner = companion->owner; }
            if (io->references_only) return true;
            if (!companion_callback(game, io, "pain", 452, "doppleganger_pain") ||
                !companion_callback(game, io, "die", 456, "doppleganger_die")) return false;
        } else {
            qa_actor_id credit = companion->credit;
            if (!io->reading && io->edition == QA_Q2_RERELEASE && !credit.registry) {
                q2_actor *base = q2_actor_get(game, companion->owner, false, NULL);
                if (base && base->item && base->item->companion) credit = base->item->companion->owner;
            }
            if (io->edition == QA_Q2_RERELEASE &&
                !q2_original_reference(game, io, "owner", 256, &credit)) return false;
            if (io->reading) companion->credit = credit;
            if (io->references_only) return true;
            if (!q2_original_scalar(io, "timestamp", Q2_ORIGINAL_TIME,
                288, 288, 288, &companion->turn_ns)) return false;
            if (io->reading) {
                companion->goal.y = actor->physics.ideal_yaw;
                q2_actor *base = q2_actor_get(game, companion->owner, false, NULL);
                companion->expires_ns = base && base->item && base->item->companion ?
                    base->item->companion->expires_ns : UINT64_MAX;
            }
            if (io->edition == QA_Q2_RERELEASE &&
                !q2_original_scalar(io, "teleport_time", Q2_ORIGINAL_TIME,
                    UINT16_MAX, UINT16_MAX, UINT16_MAX, &companion->attack_ns)) return false;
        }
    }
    if (!q2_original_scalar(io, "nextthink", Q2_ORIGINAL_TIME,
        428, 428, 428, &companion->next_ns)) return false;
    if (io->reading && kind == Q2_DOPPLEGANGER) companion->expires_ns = companion->next_ns;
    return true;
}

bool q2_original_item_record(qa_q2_game *game, q2_original_record_io *io,
    q2_actor *actor, const qa_q2_save_level *engine, qa_error *error)
{
    (void)engine;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(game->services.session), actor->id);
    const char *classname = record ? qa_strings_cstr(qa_session_strings(game->services.session), record->definition) : NULL;
    bool companion;
    if (!companion_record(game, io, actor, classname, &companion, error)) return false;
    if (companion) return true;
    const qa_q2_item_definition *definition = actor->item ? actor->item->definition :
        io->reading && classname ? qa_q2_item_lookup(game, classname) : NULL;
    if (!definition) return true;
    qa_item_id item = definition->item;
    if (!io->references_only &&
        (!q2_original_item(game, io, "item", 648, 648, 648, &item) || item != definition->item)) {
        if (error && error->code == QA_OK)
            qa_error_set(error, QA_ERROR_FORMAT, 648, "Original Q2 item disagrees with its actual classname");
        return false;
    }
    if (!actor->item) {
        qa_q2_item_checkpoint saved = {.present = true, .definition = definition->classname_id,
            .visual = {.scale = 1, .alpha = 1}, .retained = true};
        if (!qa_q2_item_restore(game, actor->id, &saved, error)) return false;
    }
    q2_item_state *state = actor->item;
    if (!q2_original_reference(game, io, "owner", 256, &state->owner) ||
        !q2_original_reference(game, io, "teammaster", 564, &state->spawn.team_master) ||
        !q2_original_reference(game, io, "teamchain", 560, &state->spawn.team_next)) return false;
    if (io->references_only) return true;
    if (!picked_slots(io, state, game->wire_clients)) return false;
    uint32_t flags = state->spawn.spawnflags | (state->targets_used ? UINT32_C(0x40000) : 0);
    if (!q2_original_scalar(io, "spawnflags", Q2_ORIGINAL_U32, 284, 284, 284, &flags) ||
        !q2_original_scalar(io, "count", Q2_ORIGINAL_I32, 532, 532, 532, &state->spawn.count) ||
        !q2_original_scalar(io, "delay", Q2_ORIGINAL_F32, 596, 596, 596, &state->spawn.delay) ||
        !q2_original_scalar(io, "nextthink", Q2_ORIGINAL_TIME, 428, 428, 428, &state->due_ns)) return false;
    if (io->reading) {
        state->spawn.spawnflags = flags;
        state->targets_used = (flags & UINT32_C(0x40000)) != 0;
        if (!q2_original_string(game, io, "target", 296, &state->spawn.target) ||
            !q2_original_string(game, io, "killtarget", 304, &state->spawn.killtarget) ||
            !q2_original_string(game, io, "message", 276, &state->spawn.message) ||
            !q2_original_string(game, io, "team", 308, &state->spawn.team)) return false;
        bool known = false;
        for (size_t i = 0; i < sizeof(item_thinks) / sizeof(item_thinks[0]); ++i) {
            bool matches;
            if (!q2_original_function_matches(game, io, "think", 436, item_thinks[i], &matches)) return false;
            if (matches) { state->think = (q2_item_think)i; known = true; break; }
        }
        if (!known) return item_error(error, 436, "Original Q2 item has an unimplemented Source think callback");
        bool touch, temporary, empty;
        if (!q2_original_function_matches(game, io, "touch", 444, "Touch_Item", &touch) ||
            !q2_original_function_matches(game, io, "touch", 444, "drop_temp_touch", &temporary) ||
            !q2_original_function_matches(game, io, "touch", 444, NULL, &empty)) return false;
        if (!touch && !temporary && !empty)
            return item_error(error, 444, "Original Q2 item has an unimplemented Source touch callback");
        state->touchable = touch || temporary;
        state->temporary = temporary;
        state->retained = state->think == Q2_ITEM_RESPAWN || state->think == Q2_ITEM_MEGA;
        if (state->think == Q2_ITEM_EXPIRE) state->expires_ns = state->due_ns;
        return true;
    }
    if ((unsigned)state->think >= sizeof(item_thinks) / sizeof(item_thinks[0]))
        return item_error(error, 436, "Q2 item has no original Source think callback");
    return q2_original_function(game, io, "think", 436, item_thinks[state->think]) &&
        q2_original_function(game, io, "touch", 444,
            state->touchable ? state->temporary ? "drop_temp_touch" : "Touch_Item" : NULL) &&
        q2_original_function(game, io, "use", 448,
            (flags & 1u) && !state->visible ? "Use_Item" : NULL);
}
