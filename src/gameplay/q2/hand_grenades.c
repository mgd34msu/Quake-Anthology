#include "internal.h"

typedef enum hand_effect_kind {
    HAND_AMMO,
    HAND_COCK,
    HAND_COOK_START,
    HAND_COOK_STOP,
    HAND_EMIT
} hand_effect_kind;
typedef struct hand_effect {
    hand_effect_kind kind;
    q2_hand_spec projectile;
} hand_effect;
typedef struct hand_step {
    q2_weapon_call call;
    const qa_q2_hand_grenade_input *input;
    qa_q2_hand_grenade_state next;
    qa_q2_hand_lifecycle lifecycle;
    hand_effect effects[6];
    size_t effect_count;
} hand_step;

static bool reserved(qa_q2_hand_action_kind kind) {
    return kind == QA_Q2_HAND_PREPARING || kind == QA_Q2_HAND_COOKING ||
           kind == QA_Q2_HAND_RELEASING;
}
static bool options_valid(const qa_q2_hand_grenade_options *options) {
    return options != NULL && options->initial_ammo >= 0 && options->capacity >= 0 &&
           options->initial_ammo <= options->capacity;
}
bool q2_hand_validate(const qa_q2_hand_grenade_state *state, qa_error *e) {
    if (state == NULL || !options_valid(&state->options) ||
        (unsigned)state->action.kind > QA_Q2_HAND_RECOVERING ||
        (state->action.kind == QA_Q2_HAND_PREPARING &&
         (state->action.state.preparing.frame < 1 || state->action.state.preparing.frame > 11))) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid independent Q2 hand grenade state");
        return false;
    }
    return true;
}
static q2_actor *find(qa_q2_game *g, qa_actor_id id) {
    return id.slot < g->capacity && g->actors[id.slot] != NULL &&
                   qa_actor_id_equal(g->actors[id.slot]->id, id)
               ? g->actors[id.slot]
               : NULL;
}
static bool bump(q2_actor *a, qa_error *e) {
    if (a->hand_revision == UINT64_MAX) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 hand grenade revision exhausted");
        return false;
    }
    ++a->hand_revision;
    return true;
}
bool qa_q2_hand_grenade_configure(qa_q2_game *g, qa_actor_id id,
                                  const qa_q2_hand_grenade_options *options, qa_error *e) {
    if (g == NULL || !options_valid(options)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 hand grenade allowance");
        return false;
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e) ||
        !qa_inventory_has(g->services.inventory, id)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                     "Q2 hand grenades require an existing shared body and inventory");
        return false;
    }
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (a == NULL)
        return false;
    if (a->hand_grenade_bound && reserved(a->hand_grenade.action.kind) &&
        a->hand_grenade.options.infinite_ammo != options->infinite_ammo) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                     "Cannot change Q2 grenade resource policy while reserved");
        return false;
    }
    qa_inventory_entry entry;
    qa_error read_error = {0};
    qa_item_id ammo = g->ammo[QA_Q2_GRENADES];
    if (!qa_inventory_entry_read(g->services.inventory, id, ammo, &entry, &read_error)) {
        if (read_error.code != QA_ERROR_NOT_FOUND) {
            if (e != NULL)
                *e = read_error;
            return false;
        }
        entry = (qa_inventory_entry){
            .item = ammo, .count = options->initial_ammo, .capacity = options->capacity};
        if (!qa_inventory_configure(g->services.inventory, id, &entry, NULL, NULL, e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
    }
    if (!bump(a, e))
        return false;
    a->hand_grenade.options = *options;
    if (!a->hand_grenade_bound)
        a->hand_grenade.action = (qa_q2_hand_action){.kind = QA_Q2_HAND_IDLE};
    a->hand_grenade_bound = true;
    return true;
}
bool qa_q2_hand_grenade_read(qa_q2_game *g, qa_actor_id id, qa_q2_hand_grenade_state *out,
                             bool *bound, qa_error *e) {
    if (g == NULL || out == NULL || bound == NULL || !q2_actor_live(g, id)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 hand grenade state query");
        return false;
    }
    q2_actor *a = find(g, id);
    *bound = a != NULL && a->hand_grenade_bound;
    *out = *bound ? a->hand_grenade : (qa_q2_hand_grenade_state){0};
    return true;
}
bool qa_q2_hand_grenade_restore(qa_q2_game *g, qa_actor_id id,
                                const qa_q2_hand_grenade_state *state, qa_error *e) {
    if (g == NULL || !q2_hand_validate(state, e))
        return false;
    if (g->hand_steps != 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 grenade restore requires an action boundary");
        return false;
    }
    qa_body_state body;
    qa_inventory_entry entry;
    if (!qa_world_body_read(g->services.world, id, &body, e) ||
        !qa_inventory_entry_read(g->services.inventory, id, g->ammo[QA_Q2_GRENADES], &entry, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (a == NULL || !bump(a, e))
        return false;
    a->hand_grenade_bound = true;
    a->hand_grenade = *state;
    return true;
}
static void effect(hand_step *step, hand_effect_kind kind) {
    step->effects[step->effect_count++] = (hand_effect){.kind = kind};
}
static uint64_t round_ms(uint64_t value) {
    uint64_t whole = value / Q2_MS, part = value % Q2_MS;
    if (part >= Q2_MS / 2)
        ++whole;
    return whole > UINT64_MAX / Q2_MS ? UINT64_MAX : whole * Q2_MS;
}
static uint64_t interval(hand_step *step, bool frame) {
    q2_weapon_call *c = &step->call;
    unsigned divisor = c->rerelease ? (c->input.haste ? 2u : 1u) *
                                          (c->input.quad_fire_until_ns > c->now_ns ? 2u : 1u)
                                    : 1;
    uint64_t native = frame ? (100 / divisor) * Q2_MS : Q2_NS / divisor;
    return c->game->hooks.firing_interval == NULL
               ? native
               : c->game->hooks.firing_interval(c->game->hooks.context, c->actor->id, native);
}
static uint64_t frame_deadline(hand_step *step, uint64_t from) {
    return q2_deadline(round_ms(from), round_ms(interval(step, true)));
}
static bool emit(hand_step *step, uint64_t expires, bool held, qa_error *e) {
    effect(step, HAND_COOK_STOP);
    hand_effect *pending = &step->effects[step->effect_count++];
    pending->kind = HAND_EMIT;
    if (step->lifecycle == QA_Q2_HAND_DEAD && !step->call.rerelease)
        expires = step->call.now_ns;
    if (!q2_hand_calculate(
            &step->call, expires,
            step->lifecycle == QA_Q2_HAND_ALIVE || step->lifecycle == QA_Q2_HAND_REMOVING, held,
            step->input->project, step->input->project_context, &pending->projectile, e))
        return false;
    uint64_t duration = interval(step, false), now = step->call.now_ns;
    if (step->call.rerelease) {
        duration = round_ms(duration);
        now = round_ms(now);
    }
    step->next.action =
        (qa_q2_hand_action){.kind = QA_Q2_HAND_RECOVERING,
                            .state.recovering = {.ready_ns = q2_deadline(now, duration),
                                                 .require_release = held && step->input->held}};
    return true;
}
static bool release(hand_step *step, uint64_t expires, uint64_t released_at, qa_error *e) {
    if (step->call.rerelease)
        return emit(step, expires, false, e);
    uint64_t when = frame_deadline(step, released_at);
    if (step->call.now_ns >= when)
        return emit(step, expires, false, e);
    step->next.action = (qa_q2_hand_action){
        .kind = QA_Q2_HAND_RELEASING, .state.releasing = {.expires_ns = expires, .throw_ns = when}};
    return true;
}
static bool cook(hand_step *step, uint64_t expires, qa_error *e) {
    if (step->call.now_ns >= expires)
        return emit(step, expires, true, e);
    if (!step->input->released && step->input->held) {
        step->next.action =
            (qa_q2_hand_action){.kind = QA_Q2_HAND_COOKING, .state.cooking = {expires}};
        return true;
    }
    return release(step, expires, step->call.now_ns, e);
}
static bool advance(hand_step *step, qa_error *e) {
    q2_weapon_call *c = &step->call;
    qa_q2_hand_action action = step->next.action;
    const qa_q2_hand_grenade_input *input = step->input;
    qa_item_id ammo = c->game->ammo[QA_Q2_GRENADES];
    if (step->lifecycle == QA_Q2_HAND_DEAD || step->lifecycle == QA_Q2_HAND_REMOVING ||
        !step->next.options.enabled) {
        if (action.kind == QA_Q2_HAND_PREPARING && !step->next.options.infinite_ammo) {
            qa_inventory_entry entry;
            if (!qa_inventory_entry_read(c->game->services.inventory, c->actor->id, ammo, &entry,
                                         e))
                return false;
            entry.count += 1;
            if (!qa_inventory_configure(c->game->services.inventory, c->actor->id, &entry, NULL,
                                        NULL, e))
                return false;
            effect(step, HAND_AMMO);
        }
        if (action.kind == QA_Q2_HAND_COOKING || action.kind == QA_Q2_HAND_RELEASING) {
            uint64_t expires = action.kind == QA_Q2_HAND_COOKING
                                   ? action.state.cooking.expires_ns
                                   : action.state.releasing.expires_ns;
            if (!emit(step, expires, step->lifecycle == QA_Q2_HAND_DEAD && c->rerelease, e))
                return false;
        }
        step->next.action = (qa_q2_hand_action){.kind = QA_Q2_HAND_DISARMED};
        return true;
    }
    switch (action.kind) {
    case QA_Q2_HAND_DISARMED:
        if (!input->held)
            step->next.action = (qa_q2_hand_action){.kind = QA_Q2_HAND_IDLE};
        return true;
    case QA_Q2_HAND_IDLE: {
        if (!input->pressed)
            return true;
        bool consumed = step->next.options.infinite_ammo;
        if (!consumed) {
            if (!qa_inventory_consume(c->game->services.inventory, c->actor->id, ammo, 1, &consumed,
                                      e))
                return false;
            if (consumed)
                effect(step, HAND_AMMO);
        }
        if (consumed)
            step->next.action = (qa_q2_hand_action){
                .kind = QA_Q2_HAND_PREPARING,
                .state.preparing = {.frame = c->rerelease ? 2 : 1,
                                    .next_ns = frame_deadline(step, c->now_ns),
                                    .release_queued = input->released || !input->held}};
        return true;
    }
    case QA_Q2_HAND_PREPARING: {
        int frame = action.state.preparing.frame;
        uint64_t next = action.state.preparing.next_ns;
        while (c->now_ns >= next && frame < 11) {
            if (frame == 5)
                effect(step, HAND_COCK);
            ++frame;
            next = frame_deadline(step, next);
        }
        if (c->now_ns < next) {
            step->next.action.state.preparing.frame = frame;
            step->next.action.state.preparing.next_ns = next;
            step->next.action.state.preparing.release_queued |= input->released || !input->held;
            return true;
        }
        effect(step, HAND_COOK_START);
        uint64_t expires = q2_deadline(c->rerelease ? round_ms(next) : next, 3200 * Q2_MS);
        return action.state.preparing.release_queued ? release(step, expires, next, e)
                                                     : cook(step, expires, e);
    }
    case QA_Q2_HAND_COOKING:
        return cook(step, action.state.cooking.expires_ns, e);
    case QA_Q2_HAND_RELEASING:
        return c->now_ns < action.state.releasing.throw_ns ||
               emit(step, action.state.releasing.expires_ns, false, e);
    case QA_Q2_HAND_RECOVERING:
        step->next.action.state.recovering.require_release =
            action.state.recovering.require_release && input->held && !input->released;
        if (c->now_ns >= action.state.recovering.ready_ns)
            step->next.action = (qa_q2_hand_action){
                .kind = step->next.action.state.recovering.require_release ? QA_Q2_HAND_DISARMED
                                                                           : QA_Q2_HAND_IDLE};
        return true;
    }
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 hand action");
    return false;
}
static bool dispatch(hand_step *step, uint64_t revision, qa_error *e) {
    q2_weapon_call *c = &step->call;
    qa_q2_game *g = c->game;
    for (size_t i = 0; i < step->effect_count; ++i) {
        if (!q2_actor_live(g, c->actor->id) || c->actor->hand_revision != revision)
            break;
        const hand_effect *pending = &step->effects[i];
        if (pending->kind == HAND_AMMO) {
            if (g->hooks.ammo_changed != NULL &&
                !g->hooks.ammo_changed(g->hooks.context, c->actor->id, g->ammo[QA_Q2_GRENADES], e))
                return false;
        } else if (pending->kind == HAND_EMIT) {
            float damage = 125 * q2_multiplier(c);
            if (!q2_actor_live(g, c->actor->id) || c->actor->hand_revision != revision)
                break;
            const q2_hand_spec *p = &pending->projectile;
            if (!q2_projectile_spawn(c, Q2_GRENADE, p->start, p->direction, damage, 0, p->speed,
                                     165, damage, p->fuse, 15, p->held ? 24 : 16, true, p->held, e))
                return false;
        } else {
            qa_body_state body;
            if (!qa_world_body_read(g->services.world, c->actor->id, &body, e))
                return false;
            qa_builtin_event event = {
                .kind = pending->kind == HAND_COOK_STOP ? QA_BUILTIN_STOP_SOUND : QA_BUILTIN_SOUND,
                .family = QA_GAME_Q2,
                .provider = g->options.owner,
                .actor = c->actor->id,
                .time_ns = c->now_ns,
                .origin = body.origin,
                .channel = 1,
                .volume = 1,
                .attenuation = 1,
                .flags = pending->kind == HAND_COOK_START ? 1u : 0u};
            if (!qa_builtin_resource(&g->services,
                                     pending->kind == HAND_COCK ? "weapons/hgrena1b.wav"
                                                                : "weapons/hgrenc1b.wav",
                                     &event.resource, e) ||
                !qa_builtin_emit(&g->services, &event, e))
                return false;
        }
    }
    return true;
}
static bool step(qa_q2_game *g, q2_actor *a, const qa_q2_hand_grenade_input *input, uint64_t now,
                 uint64_t frame, qa_error *e) {
    uint64_t revision = a->hand_revision;
    qa_q2_weapon_state animation = {.weapon = QA_Q2_GRENADES, .gun_rate = 10};
    hand_step state = {.call = {.game = g,
                                .actor = a,
                                .state = &animation,
                                .input = input->weapon,
                                .definition = &g->definitions[QA_Q2_GRENADES],
                                .now_ns = now,
                                .frame_ns = frame,
                                .rerelease = g->options.edition == QA_Q2_RERELEASE},
                       .input = input,
                       .next = a->hand_grenade,
                       .lifecycle = input->lifecycle};
    if (!q2_weapon_powerups(&state.call, e))
        return false;
    if (state.lifecycle == QA_Q2_HAND_ALIVE) {
        qa_combat_state combat;
        qa_error ignored = {0};
        if (!qa_combat_read(g->services.combat, a->id, &combat, &ignored) || combat.health <= 0)
            state.lifecycle = QA_Q2_HAND_DEAD;
    }
    if (!advance(&state, e))
        return false;
    if (!q2_actor_live(g, a->id) || a->hand_revision != revision)
        return true;
    if (!bump(a, e))
        return false;
    a->hand_grenade = state.next;
    return dispatch(&state, a->hand_revision, e);
}
bool qa_q2_hand_grenade_step(qa_q2_game *g, qa_actor_id id, const qa_q2_hand_grenade_input *input,
                             uint64_t now, uint64_t frame, qa_error *e) {
    if (g == NULL || input == NULL || frame == 0 ||
        (unsigned)input->lifecycle > QA_Q2_HAND_REMOVED || !qa_vec_finite(input->weapon.angles) ||
        !isfinite(input->weapon.gravity) || !isfinite(input->weapon.view_height) ||
        (unsigned)input->weapon.hand > QA_Q2_CENTER_HAND) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 hand grenade input");
        return false;
    }
    q2_actor *a = find(g, id);
    if (a == NULL || !a->hand_grenade_bound)
        return true;
    if (input->lifecycle == QA_Q2_HAND_REMOVED || !q2_actor_live(g, id)) {
        if (!bump(a, e))
            return false;
        a->hand_grenade_bound = false;
        return true;
    }
    if (g->hand_steps == UINT_MAX) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 hand grenade callback nesting exhausted");
        return false;
    }
    g->now_ns = now;
    g->frame_ns = frame;
    ++g->hand_steps;
    bool ok = step(g, a, input, now, frame, e);
    --g->hand_steps;
    return ok;
}
