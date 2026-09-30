#include "internal.h"

#define Q2_SAVE_MAGIC UINT32_C(0x32514151)
#define Q2_SAVE_VERSION UINT32_C(1)

typedef struct actor_save {
    qa_q2_saved_reference id;
    qa_q2_actor_checkpoint actor;
    qa_q2_item_checkpoint item;
    qa_q2_entity_checkpoint entity;
    qa_q2_player_checkpoint player;
    qa_q2_monster_checkpoint monster;
    qa_inventory_entry hand_ammo;
    uint64_t definitions_serial, observation_serial;
    bool has_monster, has_targets;
} actor_save;

static void actor_free(actor_save *s) {
    qa_q2_item_checkpoint_free(&s->item);
    qa_q2_entity_checkpoint_free(&s->entity);
    qa_q2_player_checkpoint_free(&s->player);
}
static bool actor_fields(q2_save_io *io, actor_save *s) {
    if (!q2_save_ref(io, &s->id) || !s->id.present ||
        !q2_save_actor(io, &s->actor) || !q2_save_item(io, &s->item) ||
        !q2_save_entity(io, &s->entity) || !q2_save_player(io, &s->player) ||
        !q2_save_bool(io, &s->has_monster)) return false;
    if (s->has_monster && !q2_save_monster(io, &s->monster)) return false;
    Q2T(definitions_serial); Q2T(observation_serial); Q2B(has_targets);
    if (s->actor.hand_grenade_bound) {
        Q2N(hand_ammo.item); Q2S(f64, hand_ammo.count); Q2S(f64, hand_ammo.capacity);
        Q2U(hand_ammo.policy);
    }
    return true;
}
static bool actor_capture(qa_q2_game *g, q2_actor *a, actor_save *s, qa_error *e) {
    s->has_monster = a->monster != NULL;
    if (a->powers && qa_inventory_lease_current(g->services.inventory, a->powers->definitions))
        s->definitions_serial = a->powers->definitions.serial;
    if (a->item && qa_pickups_observation_current(g->services.pickups, a->item->observation))
        s->observation_serial = a->item->observation.serial;
    qa_target_binding binding;
    s->has_targets = (a->entity || a->item) &&
        qa_persistence_targets_binding(g->entity_runtime->services.targets, a->id, &binding) &&
        binding.context == g;
    return q2_save_reference(g, a->id, &s->id, e) &&
           qa_q2_actor_capture(g, a->id, &s->actor, e) &&
           qa_q2_item_capture(g, a->id, &s->item, e) &&
           qa_q2_entity_capture(g, a->id, &s->entity, e) &&
           qa_q2_player_capture(g, a->id, &s->player, e) &&
           (!s->has_monster || qa_q2_monster_capture(g, a->id, &s->monster, e)) &&
           (!s->actor.hand_grenade_bound ||
            qa_inventory_entry_read(g->services.inventory, a->id, g->ammo[QA_Q2_GRENADES],
                                    &s->hand_ammo, e));
}
static bool header(q2_save_io *io) {
    uint32_t magic = Q2_SAVE_MAGIC, version = Q2_SAVE_VERSION;
    if (!q2_save_u32(io, &magic) || !q2_save_u32(io, &version)) return false;
    return (magic == Q2_SAVE_MAGIC && version == Q2_SAVE_VERSION) ||
           q2_save_fail(io, "Unsupported Q2 continuation schema");
}
bool qa_q2_game_capture(qa_q2_game *g, qa_buffer *out, qa_error *e) {
    if (!g || !out || g->continuation_pending || g->continuation_failed || !q2_checkpoint_idle(g, e)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 capture requires a completed provider turn");
        return false;
    }
    q2_save_io io = {.game = g, .error = e};
    qa_q2_runtime_checkpoint runtime = {0};
    qa_q2_items_checkpoint items = {0};
    qa_q2_players_checkpoint players = {0};
    qa_q2_entities_checkpoint entities = {0};
    qa_q2_monsters_checkpoint monsters = {0};
    bool ok = header(&io) && qa_q2_runtime_capture(g, &runtime, e) &&
              q2_save_runtime(&io, &runtime);
    uint32_t count = 0;
    for (q2_actor *a = g->first_actor; a; a = a->live_next)
        if (q2_actor_live(g, a->id)) ++count;
    ok = ok && q2_save_u32(&io, &count);
    for (q2_actor *a = g->first_actor; ok && a; a = a->live_next) {
        if (!q2_actor_live(g, a->id)) continue;
        actor_save saved = {0};
        ok = actor_capture(g, a, &saved, e) && actor_fields(&io, &saved);
        actor_free(&saved);
    }
    ok = ok && qa_q2_items_capture(g, &items, e) &&
         q2_save_u32(&io, &items.version) && q2_save_u32(&io, &items.cubes) &&
         qa_q2_players_capture(g, &players, e) && q2_save_players(&io, &players) &&
         qa_q2_entities_capture(g, &entities, e) && q2_save_entities(&io, &entities) &&
         qa_q2_monsters_capture(g, &monsters, e) && q2_save_monsters(&io, &monsters);
    qa_q2_entities_checkpoint_free(&entities);
    qa_q2_monsters_checkpoint_free(&monsters);
    if (!ok) { free(io.output.data); return false; }
    *out = io.output;
    return true;
}
static bool actor_restore(q2_save_io *io, actor_save *s, uint64_t limit, uint64_t *previous_order) {
    qa_q2_game *g = io->game;
    const qa_actor_record *record =
        qa_actors_resolve_saved(qa_session_actors(g->services.session), s->id.actor);
    if (!record || g->actors[record->id.slot] || s->actor.source_order <= *previous_order ||
        s->actor.source_order > limit)
        return q2_save_fail(io, "Invalid Q2 continuation actor order or generation");
    if (s->actor.hand_grenade_bound) {
        qa_inventory_entry normalized;
        if (s->hand_ammo.item != g->ammo[QA_Q2_GRENADES] ||
            !qa_inventory_validate_entry(&s->hand_ammo, &normalized, io->error) ||
            normalized.count != s->hand_ammo.count)
            return q2_save_fail(io, "Invalid Q2 continuation hand inventory");
    }
    if ((s->definitions_serial != 0) != s->item.definitions_bound ||
        (s->observation_serial && (!s->item.present || s->item.companion.kind)) ||
        (s->has_targets && !s->item.present && !s->entity.present))
        return q2_save_fail(io, "Q2 continuation lease has no private owner");
    if (!qa_q2_actor_restore(g, record->id, &s->actor, io->error) ||
        !qa_q2_item_restore(g, record->id, &s->item, io->error) ||
        !qa_q2_entity_restore(g, record->id, &s->entity, io->error) ||
        !qa_q2_player_restore(g, record->id, &s->player, io->error) ||
        (s->has_monster &&
         (!qa_q2_monster_restore(g, record->id, &s->monster, io->error) ||
          !qa_q2_actor_restore(g, record->id, &s->actor, io->error))))
        return false;
    q2_actor *a = q2_actor_get(g, record->id, false, NULL);
    a->restore_hand_ammo = s->hand_ammo;
    a->restore_targets = s->has_targets;
    if (a->powers) a->powers->definitions = (qa_inventory_lease){record->id, s->definitions_serial};
    if (a->item) a->item->observation = (qa_pickup_lease){record->id, s->observation_serial};
    *previous_order = s->actor.source_order;
    return true;
}
bool qa_q2_game_restore(qa_q2_game *g, qa_bytes data, qa_error *e) {
    if (!g || (data.size && !data.data) || g->first_actor || g->continuation_pending || g->continuation_failed ||
        !q2_checkpoint_idle(g, e)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 restore requires an empty candidate");
        return false;
    }
    q2_save_io io = {.game = g, .input = data, .error = e, .reading = true};
    qa_q2_runtime_checkpoint runtime = {0};
    qa_q2_items_checkpoint items = {0};
    qa_q2_players_checkpoint players = {0};
    qa_q2_entities_checkpoint entities = {0};
    qa_q2_monsters_checkpoint monsters = {0};
    g->restoring_continuation = true;
    bool ok = header(&io) && q2_save_runtime(&io, &runtime) &&
              qa_q2_runtime_restore(g, &runtime, e);
    /* Actor admission assigns temporary order numbers; retain the saved
     * sequence after restoring each actor's authored order. */
    if (ok) g->actor_sequence = 0;
    uint32_t count = 0;
    ok = ok && q2_save_u32(&io, &count);
    if (ok && (count > g->capacity || count > (io.input.size - io.offset) / 64))
        ok = q2_save_fail(&io, "Invalid Q2 continuation actor count");
    uint64_t previous_order = 0;
    for (uint32_t i = 0; ok && i < count; ++i) {
        actor_save saved = {0};
        ok = actor_fields(&io, &saved) &&
             actor_restore(&io, &saved, runtime.actor_sequence, &previous_order);
        actor_free(&saved);
    }
    ok = ok && q2_save_u32(&io, &items.version) && q2_save_u32(&io, &items.cubes) &&
         qa_q2_items_restore(g, &items, e) &&
         q2_save_players(&io, &players) && qa_q2_players_restore(g, &players, e) &&
         q2_save_entities(&io, &entities) && qa_q2_entities_restore(g, &entities, e) &&
         q2_save_monsters(&io, &monsters) && qa_q2_monsters_restore(g, &monsters, e);
    if (ok && io.offset != data.size) ok = q2_save_fail(&io, "Trailing Q2 continuation data");
    if (ok) ok = qa_q2_entities_validate_links(g, e);
    if (ok) g->actor_sequence = runtime.actor_sequence;
    qa_q2_entities_checkpoint_free(&entities);
    qa_q2_monsters_checkpoint_free(&monsters);
    g->restoring_continuation = false;
    /* A partially restored candidate is never publishable, including failures. */
    g->continuation_pending = ok;
    g->continuation_failed = !ok;
    return ok;
}
bool qa_q2_game_restore_finish(qa_q2_game *g, qa_error *e) {
    if (!g || !g->continuation_pending || g->continuation_failed || g->restoring_continuation || !q2_checkpoint_idle(g, e)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 continuation has no pending reconnect");
        return false;
    }
    for (q2_actor *a = g->first_actor; a; a = a->live_next) {
        if (!q2_actor_live(g, a->id)) {
            qa_error_set(e, QA_ERROR_FORMAT, a->id.slot, "Q2 continuation actor disappeared");
            return false;
        }
        if (a->hand_grenade_bound) {
            qa_inventory_entry ammo;
            if (!qa_inventory_entry_read(g->services.inventory, a->id, g->ammo[QA_Q2_GRENADES],
                                          &ammo, e)) return false;
            if (ammo.item != a->restore_hand_ammo.item || ammo.count != a->restore_hand_ammo.count ||
                ammo.capacity != a->restore_hand_ammo.capacity || ammo.policy != a->restore_hand_ammo.policy) {
                qa_error_set(e, QA_ERROR_FORMAT, a->id.slot, "Q2 hand inventory differs from continuation");
                return false;
            }
        }
        if (a->restore_definitions &&
            !qa_inventory_lease_current(g->services.inventory, a->powers->definitions)) {
            qa_error_set(e, QA_ERROR_FORMAT, a->id.slot, "Q2 inventory definitions lease was not restored");
            return false;
        }
        if (a->restore_power_inventory) {
            qa_inventory *inventory;
            qa_item_id cells;
            if (!qa_combat_power_inventory(g->services.combat, a->id, &inventory, &cells) ||
                inventory != g->services.inventory || cells != a->powers->cells) {
                qa_error_set(e, QA_ERROR_FORMAT, a->id.slot, "Q2 power inventory binding was not restored");
                return false;
            }
        }
        if (a->item && a->item->observation.serial) {
            if (!qa_pickups_observation_current(g->services.pickups, a->item->observation)) {
                qa_error_set(e, QA_ERROR_FORMAT, a->id.slot, "Q2 pickup observation lease was not restored");
                return false;
            }
            a->item->observations = g->services.pickups;
        }
        if (a->restore_targets) {
            qa_target_binding actual, expected;
            qa_targets *targets = g->entity_runtime->services.targets;
            if (!qa_persistence_targets_binding(targets, a->id, &actual) ||
                !qa_q2_game_target_binding(g, a->id, &expected, e) ||
                actual.context != expected.context || actual.source != expected.source ||
                actual.read != expected.read || actual.use != expected.use ||
                actual.field != expected.field || actual.set_targetname != expected.set_targetname ||
                actual.set_target != expected.set_target || actual.set_delay != expected.set_delay) {
                qa_error_set(e, QA_ERROR_FORMAT, a->id.slot, "Q2 authored target binding was not restored");
                return false;
            }
            a->entity_game = g;
            a->entity_targets = targets;
        }
    }
    for (q2_actor *a = g->first_actor; a; a = a->live_next) {
        a->restore_definitions = a->restore_power_inventory = a->restore_targets = false;
        a->restore_hand_ammo = (qa_inventory_entry){0};
    }
    g->continuation_pending = false;
    return true;
}
bool qa_q2_game_think_binding(qa_q2_game *g, qa_actor_id id, uint32_t callback,
                              qa_think_fn *think, void **context, qa_error *e) {
    (void)g; (void)id; (void)callback; (void)think; (void)context;
    /* Q2 source callbacks run from actor turns and private due_ns/think enums.
     * This provider never installs shared scheduler entries. */
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Q2 provider has no shared scheduler callback identities");
    return false;
}
