#include "qa/q2_sound.h"
#include "reinforcements.h"
#include "../entities/internal.h"
#include "spawn.h"
#include <errno.h>

bool q2m_summon_add(int64_t *counter, int64_t amount, qa_error *error) {
    if ((amount > 0 && *counter > INT64_MAX - amount) ||
        (amount < 0 && *counter < INT64_MIN - amount)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 reinforcement counter overflow");
        return false;
    }
    *counter += amount;
    return true;
}

bool q2m_summon_subtract(int64_t *counter, int64_t amount, qa_error *error) {
    if ((amount > 0 && *counter < INT64_MIN + amount) ||
        (amount < 0 && *counter > INT64_MAX + amount)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 reinforcement counter overflow");
        return false;
    }
    *counter -= amount;
    return true;
}

bool q2m_summon_has_slots(const struct qa_q2_monster *monster, uint64_t required) {
    return monster->monster_slots >= monster->monster_used &&
           (uint64_t)monster->monster_slots - (uint64_t)monster->monster_used >= required;
}

static const struct {
    const char *classname;
    int strength;
} medic_defaults[] = {{"monster_soldier_light", 1}, {"monster_soldier", 2},
                      {"monster_soldier_ss", 2},    {"monster_infantry", 3},
                      {"monster_gunner", 4},        {"monster_medic", 5},
                      {"monster_gladiator", 6}};
static const qa_vec3 medic_positions[] = {
    {80, 0, 0}, {40, 60, 0}, {40, -60, 0}, {0, 80, 0}, {0, -80, 0}};

bool q2m_summon_initialize(q2m_context *context, qa_error *error) {
    q2m_species species = context->monster->definition->species;
    if (species == Q2M_WIDOW || species == Q2M_WIDOW2)
        return q2m_widow_slots(context, error);
    bool source_medic =
        species == Q2M_MEDIC_COMMANDER ||
        (species == Q2M_MEDIC && (context->game->options.edition == QA_Q2_RERELEASE ||
                                  context->game->options.product == QA_Q2_ROGUE));
    if (!source_medic)
        return true;
    context->monster->ignore_shots = true;
    if (context->monster->summons) {
        if (context->combat.mass > 400)
            context->monster->skin = 2;
        return true;
    }
    context->monster->summons = calloc(1, sizeof(*context->monster->summons));
    if (!context->monster->summons) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating medic reinforcement choices");
        return false;
    }
    if (context->combat.mass <= 400)
        return true;
    context->monster->skin = 2;
    return q2m_medic_summon_initialize(context, context->monster->summons, error);
}

void q2m_summon_clear(q2m_summon_state *state) {
    if (!state)
        return;
    free(state->entries);
    *state = (q2m_summon_state){0};
}

static bool configure(q2m_context *context, q2m_summon_state *state, qa_error *error) {
    if (state->configured)
        return true;
    qa_q2_game *game = context->game;
    qa_string_id authored =
        context->actor->entity ? q2_field_id(context->actor->entity, game->field_keys[QA_TARGET_KEY_REINFORCEMENTS]) : 0;
    const char *text =
        authored ? qa_strings_cstr(qa_session_strings(game->services.session), authored) : NULL;
    size_t count = authored ? 0 : sizeof(medic_defaults) / sizeof(*medic_defaults);
    if (text && *text) {
        count = 1;
        for (const char *p = text; *p; ++p)
            count += *p == ';';
    }
    if (count > UINT32_MAX || count > SIZE_MAX / sizeof(q2m_reinforcement)) {
        qa_error_set(error, QA_ERROR_MEMORY, count, "Medic reinforcement list is too large");
        return false;
    }
    q2m_reinforcement *entries = count ? calloc(count, sizeof(*entries)) : NULL;
    if (count && !entries) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating authored medic reinforcements");
        return false;
    }
    const char *cursor = text;
    for (size_t i = 0; i < count; ++i) {
        if (!authored) {
            const q2m_definition *definition =
                q2m_definition_for(game, medic_defaults[i].classname);
            entries[i] = (q2m_reinforcement){definition, medic_defaults[i].strength,
                                            definition->bounds};
            continue;
        }
        const char *end = strchr(cursor, ';');
        if (!end)
            end = cursor + strlen(cursor);
        cursor += strspn(cursor, " \t\r\n\v\f");
        size_t length = strcspn(cursor, " \t\r\n\v\f;");
        char classname[QA_Q2_MONSTER_NAME_CAPACITY];
        if (!length || length >= sizeof(classname) || cursor + length > end)
            goto invalid;
        memcpy(classname, cursor, length);
        classname[length] = 0;
        const q2m_definition *definition = q2m_definition_for(game, classname);
        if (!definition)
            goto invalid;
        cursor += length;
        cursor += strspn(cursor, " \t\r\n\v\f");
        long strength = 0;
        if (cursor < end) {
            char *tail;
            errno = 0;
            strength = strtol(cursor, &tail, 10);
            if (tail == cursor || errno == ERANGE || strength < INT_MIN || strength > INT_MAX)
                goto invalid;
        }
        entries[i] = (q2m_reinforcement){definition, (int)strength, definition->bounds};
        cursor = *end ? end + 1 : end;
    }
    free(state->entries);
    state->entries = entries;
    state->entry_count = count;
    state->authored = authored;
    state->configured = true;
    return true;
invalid:
    free(entries);
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid authored medic reinforcement");
    return false;
}

static size_t entry_count(const q2m_summon_state *state) {
    return state->entry_count;
}

static q2m_reinforcement entry_at(const q2m_summon_state *state, size_t index) {
    return state->entries[index];
}

bool q2m_medic_summon_initialize(q2m_context *context, q2m_summon_state *state, qa_error *error) {
    if (context->game->options.edition == QA_Q2_CLASSIC) {
        context->monster->monster_slots = context->game->options.skill == 0   ? 3
                                          : context->game->options.skill == 1 ? 4
                                                                              : 6;
        return true;
    }
    if (!configure(context, state, error))
        return false;
    double slots = context->actor->entity
                       ? q2_field_float(context->game, context->actor->entity, context->game->field_keys[QA_TARGET_KEY_MONSTER_SLOTS], 3)
                       : 3;
    if (slots != 0 && entry_count(state))
        slots += floor(slots * context->game->options.skill / 2.0);
    if (!isfinite(slots) || slots < -0x1p63 || slots >= 0x1p63 || trunc(slots) != slots) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid medic reinforcement slot count");
        return false;
    }
    context->monster->monster_slots = (int64_t)slots;
    return true;
}

static bool choose_squad(q2m_context *context, q2m_summon_state *state, qa_error *error) {
    qa_q2_game *game = context->game;
    state->chosen_count = 0;
    if (game->options.edition == QA_Q2_CLASSIC) {
        float luck = q2m_random(game);
        int strength = game->options.skill + (luck < .05f   ? -3
                                              : luck < .15f ? -2
                                              : luck < .3f  ? -1
                                              : luck > .95f ? 3
                                              : luck > .85f ? 2
                                              : luck > .7f  ? 1
                                                            : 0);
        if (strength > 6) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Medic reinforcement skill is out of range");
            return false;
        }
        state->classic_strength = strength = strength < 0 ? 0 : strength;
        size_t count = strength ? (size_t)(strength - 1 + strength % 2) : 1;
        for (size_t i = 0; i < count; ++i) {
            int index = strength - (int)i - (int)(i % 2);
            const q2m_definition *definition =
                q2m_definition_for(game, medic_defaults[index].classname);
            qa_bounds bounds = index == 6 ? (qa_bounds){{-32, -32, -24}, {32, 32, 64}}
                                          : (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
            state->chosen[state->chosen_count++] = (q2m_reinforcement){definition, 1, bounds};
        }
        return true;
    }
    if (!configure(context, state, error))
        return false;
    float roll = q2_rerelease_float(game, 0, 32);
    int count = roll >= 2 ? (int)log2((double)roll) : 1;
    if (count > 5)
        count = 5;
    int64_t remaining = context->monster->monster_slots;
    if (!q2m_summon_subtract(&remaining, context->monster->monster_used, error))
        return false;
    size_t entries = entry_count(state);
    for (int n = 0; n < count && remaining; ++n) {
        uint32_t available = 0;
        for (size_t i = 0; i < entries; ++i)
            available += entry_at(state, i).strength <= remaining;
        if (!available)
            break;
        uint32_t selected = q2_random_bounded(game, available);
        for (size_t i = 0; i < entries; ++i) {
            q2m_reinforcement entry = entry_at(state, i);
            if (entry.strength > remaining)
                continue;
            if (selected) {
                --selected;
                continue;
            }
            state->chosen[state->chosen_count++] = entry;
            if (!q2m_summon_subtract(&remaining, entry.strength, error))
                return false;
            break;
        }
    }
    return true;
}

static bool child_context(qa_q2_game *game, qa_actor_id id, q2m_context *child, qa_error *error) {
    q2_actor *actor = q2_actor_get(game, id, false, NULL);
    if (!actor || !actor->monster)
        return true;
    *child = (q2m_context){.game = game, .actor = actor, .monster = actor->monster};
    bool ok = q2m_refresh(child, error);
    return ok || !q2m_alive(child);
}

bool q2m_create_reinforcement(q2m_context *context, const char *classname, qa_vec3 origin,
                              qa_actor_id *out, qa_error *error) {
    *out = (qa_actor_id){0};
    if (!q2m_alive(context))
        return true;
    qa_q2_game *game = context->game;
    qa_actor_definition definition;
    if (!qa_builtin_resource(&game->services, classname, &definition, error))
        return false;
    if (!q2m_alive(context))
        return true;
    qa_builtin_spawn spawn = {.owner = game->options.owner,
                              .definition = definition,
                              .body = {.origin = origin, .angles = context->body.angles}};
    qa_actor_id child;
    if (!qa_builtin_spawn_actor(&game->services, &spawn, &child, error))
        return false;
    qa_q2_monster_spawn_options options = {
        .classname = classname, .health_multiplier = 1, .summoned = true};
    if (!qa_q2_monster_spawn(game, child, &options, error)) {
        qa_error ignored = {0};
        (void)qa_session_release(game->services.session, child, &ignored);
        return false;
    }
    q2_actor *actor = q2_actor_get(game, child, false, NULL);
    if (!actor || !actor->monster)
        return true;
    actor->monster->render_flags |= UINT32_C(32768);
    *out = child;
    return true;
}

static bool start_child(q2m_context *child, qa_error *error) {
    if (!q2m_alive(child))
        return true;
    child->monster->start_due_ns = child->game->now_ns;
    bool handled;
    return q2m_lifecycle_tick(child, &handled, error);
}

static bool coop_target(q2m_context *context, qa_actor_id *out, qa_error *error) {
    *out = (qa_actor_id){0};
    qa_q2_game *game = context->game;
    if (!game->options.cooperative || !q2m_alive(context))
        return true;
    qa_builtin_snapshot_frame *roster = q2_player_roster(game, error);
    if (!roster)
        return false;
    bool ok = true;
    size_t count = 0;
    for (size_t i = 0; i < roster->snapshot.count && q2m_alive(context); ++i) {
        qa_actor_id id = roster->snapshot.ids[i];
        qa_builtin_actor_traits traits;
        if (!q2_actor_live(game, id) || !game->services.actor_traits ||
            !game->services.actor_traits(game->services.context, id, &traits) || !traits.player)
            continue;
        if (!q2m_alive(context) || !q2_actor_live(game, id))
            continue;
        bool visible;
        if (!q2m_visible(context, id, &visible, error)) {
            ok = false;
            break;
        }
        if (q2m_alive(context) && q2_actor_live(game, id) && visible)
            roster->snapshot.ids[count++] = id;
    }
    if (ok && count && q2m_alive(context)) {
        size_t choice = (size_t)(q2m_random(game) * (float)count);
        *out = roster->snapshot.ids[choice < count ? choice : count - 1];
    }
    qa_builtin_snapshot_release(roster);
    return ok;
}

static bool assign_enemy(q2m_context *parent, q2m_context *child, bool widow, qa_error *error) {
    if (!q2m_alive(parent) || !q2m_alive(child))
        return true;
    qa_actor_id enemy =
        !widow && parent->monster->medic ? parent->monster->old_enemy : parent->monster->enemy;
    if (parent->game->options.cooperative) {
        if (!coop_target(child, &enemy, error))
            return false;
        if (!q2m_alive(parent) || !q2m_alive(child))
            return true;
        if (enemy.registry && qa_actor_id_equal(enemy, parent->monster->enemy) &&
            !coop_target(child, &enemy, error))
            return false;
        if (!q2m_alive(parent) || !q2m_alive(child))
            return true;
        if (!enemy.registry)
            enemy = parent->monster->enemy;
    }
    qa_combat_state combat;
    bool living = false;
    if (q2_actor_live(parent->game, enemy)) {
        qa_error observed = {0};
        living = qa_combat_read(parent->game->services.combat, enemy, &combat, &observed);
        if (!q2m_alive(parent) || !q2m_alive(child))
            return true;
        if (!living && q2_actor_live(parent->game, enemy) && observed.code != QA_OK &&
            observed.code != QA_ERROR_NOT_FOUND) {
            if (error)
                *error = observed;
            return false;
        }
    }
    if (!q2m_alive(parent) || !q2m_alive(child))
        return true;
    living = living && q2_actor_live(parent->game, enemy) && combat.health > 0;
    if (!living) {
        if (widow)
            return true;
        child->monster->enemy = qa_actor_reference_resolve(qa_session_actors(parent->game->services.session), child->actor->physics.enemy = (qa_actor_reference){0});
        return q2m_set_move(child, child->monster->definition->stand_move, false, error);
    }
    if (!q2m_found_target(child, enemy, error))
        return false;
    return !widow || !q2m_alive(child) || q2m_source_attack(child, false, error);
}

bool q2m_widow_slots(q2m_context *context, qa_error *error) {
    if (!q2m_alive(context))
        return true;
    qa_q2_game *game = context->game;
    int slots = game->options.skill < 2 ? 3 : game->options.skill == 2 ? 4 : 6;
    if (game->options.cooperative) {
        qa_builtin_snapshot_frame *roster = q2_player_roster(game, error);
        if (!roster)
            return false;
        int64_t players = 0;
        for (size_t i = 0; i < roster->snapshot.count && q2m_alive(context); ++i) {
            qa_actor_id id = roster->snapshot.ids[i];
            qa_builtin_actor_traits traits;
            bool player = q2_actor_live(game, id) && game->services.actor_traits &&
                          game->services.actor_traits(game->services.context, id, &traits) &&
                          traits.player;
            if (q2m_alive(context) && player && q2_actor_live(game, id))
                ++players;
        }
        qa_builtin_snapshot_release(roster);
        int64_t total = slots + game->options.skill * (players - 1);
        slots = (int)(total > 6 ? 6 : total);
    }
    if (q2m_alive(context))
        context->monster->monster_slots = slots;
    return true;
}

bool q2m_widow_attack_move(q2m_context *context, bool second, float distance, q2m_move_id *move,
                           qa_error *error) {
    struct qa_q2_monster *monster = context->monster;
    qa_q2_game *game = context->game;
    bool blocked = monster->source_blocked, anger = monster->target_anger;
    monster->source_blocked = false;
    if (!second) {
        monster->move_target = (qa_actor_id){0};
        monster->target_anger = false;
    }
    *move = Q2M_MOVE_NONE;
    if (!q2_actor_live(game, monster->enemy))
        return true;
    qa_actor_id hazard;
    if (!qa_q2_bad_area(game, context->actor->id, context->body.origin, &hazard, error))
        return false;
    if (!q2m_alive(context))
        return true;
    bool ready = game->now_ns >= (second ? monster->attack_ns : monster->timestamp_ns);
    if (hazard.registry) {
        float chance = second ? .75f : .1f;
        *move = q2m_random(game) < chance || !ready
                    ? (second ? Q2M_MOVE_widow2_move_attack_pre_beam : Q2M_MOVE_widow_move_attack_pre_blaster)
                    : (second ? Q2M_MOVE_widow2_move_attack_disrupt : Q2M_MOVE_widow_move_attack_pre_rail);
    } else {
        if (!q2m_widow_slots(context, error))
            return false;
        if (!q2m_alive(context))
            return true;
        bool slots = q2m_summon_has_slots(monster, 2);
        if ((monster->attack_state == Q2M_BLIND || blocked) && slots)
            *move = second ? Q2M_MOVE_widow2_move_spawn : Q2M_MOVE_widow_move_spawn;
        else if (second) {
            float luck = q2m_random(game);
            if (distance < 600)
                *move = slots ? (luck <= .4f            ? Q2M_MOVE_widow2_move_attack_pre_beam
                                 : luck <= .7f && ready ? Q2M_MOVE_widow2_move_attack_disrupt
                                                        : Q2M_MOVE_widow2_move_spawn)
                              : (luck <= .5f || !ready ? Q2M_MOVE_widow2_move_attack_pre_beam
                                                       : Q2M_MOVE_widow2_move_attack_disrupt);
            else
                *move = slots ? (luck < .3f              ? Q2M_MOVE_widow2_move_attack_pre_beam
                                 : luck < .65f || !ready ? Q2M_MOVE_widow2_move_spawn
                                                         : Q2M_MOVE_widow2_move_attack_disrupt)
                              : (luck < .45f || !ready ? Q2M_MOVE_widow2_move_attack_pre_beam
                                                       : Q2M_MOVE_widow2_move_attack_disrupt);
        } else if (distance > 300 && !anger && q2m_random(game) < .5f && !blocked) {
            *move = Q2M_MOVE_widow_move_run_attack;
        } else {
            bool rail_frame =
                monster->frame == 23 || (monster->frame >= 11 && monster->frame <= 13);
            bool blaster_frame = monster->frame >= 19 && monster->frame <= 22;
            bool blaster_ready = q2m_after(monster->pause_ns, 2) <= game->now_ns;
            if (blaster_frame && slots)
                *move = Q2M_MOVE_widow_move_spawn;
            else if (blaster_frame && blaster_ready)
                *move = Q2M_MOVE_widow_move_attack_pre_blaster;
            else if (rail_frame && ready)
                *move = Q2M_MOVE_widow_move_attack_pre_rail;
            else if (!blaster_frame && !rail_frame) {
                float luck = q2m_random(game);
                if (slots)
                    *move = luck <= .4f && blaster_ready ? Q2M_MOVE_widow_move_attack_pre_blaster
                            : luck <= .7f && ready       ? Q2M_MOVE_widow_move_attack_pre_rail
                                                         : Q2M_MOVE_widow_move_spawn;
                else
                    *move = !ready ? Q2M_MOVE_widow_move_attack_pre_blaster
                            : luck <= .5f || q2m_after(game->now_ns, 2) >= monster->pause_ns
                                ? Q2M_MOVE_widow_move_attack_pre_rail
                                : Q2M_MOVE_widow_move_attack_pre_blaster;
            }
        }
    }
    return !*move || (*move != Q2M_MOVE_widow_move_attack_pre_rail) ||
           q2m_sound(context, QA_Q2_SOUND_GLADIATOR_RAILGUN, 1, 1, error);
}

bool q2m_widow_summon(q2m_context *context, bool second, bool grow, qa_error *error) {
    static const qa_bounds bounds = {.mins = {-28, -28, -18}, .maxs = {28, 28, 18}};
    for (int side = 1; side >= -1 && q2m_alive(context); side -= 2) {
        if (!q2m_refresh(context, error))
            return !q2m_alive(context);
        qa_vec3 forward, right, up;
        qa_builtin_angle_vectors(context->body.angles, &forward, &right, &up);
        qa_vec3 point =
            qa_vec_add(context->body.origin,
                       qa_vec_add(qa_vec_scale(forward, 30),
                                  qa_vec_add(qa_vec_scale(right, (float)side * (second ? 135.0f : 100.0f)),
                                             qa_vec_scale(up, second ? 0 : 16))));
        bool found;
        if (!qa_q2_rogue_find_spawn_point(context->game, point, bounds, 64, &found, &point, error))
            return false;
        if (!q2m_alive(context))
            return true;
        if (!found)
            continue;
        if (grow) {
            if (!q2_spawn_growth(context->game, point, 1, error))
                return false;
            continue;
        }
        bool valid;
        if (!qa_q2_rogue_check_ground_spawn(context->game, point, bounds, 256, -1, &valid, error))
            return false;
        if (!q2m_alive(context))
            return true;
        if (!valid)
            continue;
        qa_actor_id id;
        if (!q2m_create_reinforcement(context, "monster_stalker", point, &id, error))
            return false;
        if (!q2m_alive(context))
            return true;
        q2m_context child = {0};
        if (!child_context(context->game, id, &child, error))
            return false;
        if (!q2m_alive(context) || !q2m_alive(&child))
            continue;
        if (!q2m_summon_add(&context->monster->monster_used, 1, error))
            return false;
        child.monster->commander = context->actor->id;
        if (!start_child(&child, error))
            return false;
        if (!q2m_alive(context) || !q2m_alive(&child))
            continue;
        child.monster->spawned_by = Q2M_SPAWN_WIDOW;
        child.monster->do_not_count = child.monster->ignore_shots = true;
        if (!assign_enemy(context, &child, true, error))
            return false;
    }
    return true;
}

typedef enum medic_spawn_stage { MEDIC_DETERMINE, MEDIC_GROW, MEDIC_FINISH } medic_spawn_stage;

static bool medic_positions_pass(q2m_context *context, q2m_summon_state *state, bool behind,
                                 medic_spawn_stage stage, bool *success, qa_error *error) {
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    *success = false;
    for (size_t i = 0; q2m_alive(context) && i < state->chosen_count; ++i) {
        if (!q2m_refresh(context, error))
            return !q2m_alive(context);
        q2m_reinforcement reinforcement = state->chosen[i];
        qa_vec3 offset = medic_positions[i];
        if (rerelease && context->monster->entity_scale != 0 && stage == MEDIC_DETERMINE)
            offset = qa_vec_scale(offset, context->monster->entity_scale);
        if (behind) {
            offset.x = -offset.x;
            offset.y = -offset.y;
        }
        qa_vec3 point = q2m_project_offset(context, offset);
        point.z += rerelease && !behind ? 10 * (context->monster->entity_scale != 0 ? context->monster->entity_scale : 1) : 10;
        bool found;
        bool ok = rerelease
                      ? q2m_rerelease_find_spawn_point(context->game, point, reinforcement.bounds,
                                                       true, &found, &point, error)
                      : qa_q2_rogue_find_spawn_point(context->game, point, reinforcement.bounds, 32,
                                                     &found, &point, error);
        if (!ok)
            return false;
        if (!q2m_alive(context))
            return true;
        if (!found)
            continue;
        bool valid;
        if (stage == MEDIC_FINISH) {
            if (!q2m_check_spawn_point(context->game, point, reinforcement.bounds, &valid, error))
                return false;
            if (!q2m_alive(context))
                return true;
            if (!valid)
                continue;
        }
        ok = rerelease ? q2m_rerelease_check_ground_spawn(context->game, point,
                                                          reinforcement.bounds, &valid, error)
                       : qa_q2_rogue_check_ground_spawn(context->game, point, reinforcement.bounds,
                                                        256, -1, &valid, error);
        if (!ok)
            return false;
        if (!q2m_alive(context))
            return true;
        if (!valid)
            continue;
        *success = true;
        if (stage == MEDIC_DETERMINE)
            return true;
        if (stage == MEDIC_GROW) {
            if (rerelease) {
                qa_vec3 center = qa_vec_add(
                    point, qa_vec_add(reinforcement.bounds.mins, reinforcement.bounds.maxs));
                float radius = qa_vec_length(qa_vec_sub(reinforcement.bounds.maxs,
                                                        reinforcement.bounds.mins)) *
                               .5f;
                if (!q2m_rerelease_spawn_growth(context->game, center, radius, error))
                    return false;
            } else {
                int index = state->classic_strength - (int)i - (int)(i % 2);
                if (!q2_spawn_growth(context->game, point, index > 3 ? 1 : 0, error))
                    return false;
            }
            continue;
        }
        qa_actor_id id;
        if (!q2m_create_reinforcement(context, reinforcement.definition->classname, point, &id,
                                      error))
            return false;
        if (!q2m_alive(context))
            return true;
        q2m_context child = {0};
        if (!child_context(context->game, id, &child, error))
            return false;
        if (!q2m_alive(context) || !q2m_alive(&child))
            continue;
        if (!start_child(&child, error))
            return false;
        if (!q2m_alive(context) || !q2m_alive(&child))
            continue;
        child.monster->ignore_shots = child.monster->do_not_count = true;
        child.monster->spawned_by = Q2M_SPAWN_MEDIC;
        child.monster->commander = context->actor->id;
        if (rerelease) {
            child.monster->monster_slots = reinforcement.strength;
            if (!q2m_summon_add(&context->monster->monster_used, reinforcement.strength, error))
                return false;
        } else {
            if (!q2m_summon_subtract(&context->monster->monster_slots, 1, error))
                return false;
        }
        if (!assign_enemy(context, &child, false, error))
            return false;
    }
    return true;
}



bool q2m_medic_determine_summons(q2m_context *context, q2m_summon_state *state, qa_error *error) {
    if (!choose_squad(context, state, error))
        return false;
    bool success;
    if (!medic_positions_pass(context, state, false, MEDIC_DETERMINE, &success, error))
        return false;
    if (!q2m_alive(context))
        return true;
    if (!success) {
        if (!medic_positions_pass(context, state, true, MEDIC_DETERMINE, &success, error))
            return false;
        if (!q2m_alive(context))
            return true;
        if (success) {
            context->monster->manual_steering = true;
            context->monster->ideal_yaw = qa_angle_mod(context->body.angles.y) + 180;
            if (context->monster->ideal_yaw > 360)
                context->monster->ideal_yaw -= 360;
        }
    }
    if (!success)
        context->monster->next_frame = 229;
    return true;
}

bool q2m_medic_grow_summons(q2m_context *context, q2m_summon_state *state, qa_error *error) {
    if (context->monster->manual_steering) {
        if (fabsf(qa_angle_mod(context->body.angles.y) - context->monster->ideal_yaw) > .1f) {
            context->monster->hold_frame = true;
            return true;
        }
        context->monster->manual_steering = context->monster->hold_frame = false;
    }
    bool success;
    if (!medic_positions_pass(context, state, false, MEDIC_GROW, &success, error))
        return false;
    if (q2m_alive(context) && !success)
        context->monster->next_frame = 229;
    return true;
}

bool q2m_medic_finish_summons(q2m_context *context, q2m_summon_state *state, qa_error *error) {
    bool success;
    return medic_positions_pass(context, state, false, MEDIC_FINISH, &success, error);
}

bool q2m_summon_callback(q2m_context *context, q2m_callback_id callback, bool *handled,
                         qa_error *error) {
    *handled = true;
    if (callback == Q2M_CALLBACK_medic_start_spawn) {
        if (!q2m_sound(context, QA_Q2_SOUND_MEDIC_COMMANDER_MONSTERSPAWN1, 1, 1, error))
            return false;
        if (q2m_alive(context))
            context->monster->next_frame = 224;
        return true;
    }
    if (callback == Q2M_CALLBACK_widow_start_spawn) {
        context->monster->manual_steering = true;
        return true;
    }
    if ((callback == Q2M_CALLBACK_widow_ready_spawn) || (callback == Q2M_CALLBACK_widow_spawn_check) ||
        (callback == Q2M_CALLBACK_widow2_ready_spawn) || (callback == Q2M_CALLBACK_widow2_spawn_check)) {
        bool second = (callback == Q2M_CALLBACK_widow2_ready_spawn || callback == Q2M_CALLBACK_widow2_spawn_check);
        bool grow = !(callback != (second ? Q2M_CALLBACK_widow2_ready_spawn : Q2M_CALLBACK_widow_ready_spawn));
        return q2m_callback_run(context, second ? Q2M_CALLBACK_Widow2Beam : Q2M_CALLBACK_WidowBlaster, error) &&
               (!q2m_alive(context) || q2m_widow_summon(context, second, grow, error));
    }
    bool determine = (callback == Q2M_CALLBACK_medic_determine_spawn);
    bool grow = (callback == Q2M_CALLBACK_medic_spawngrows);
    bool finish = (callback == Q2M_CALLBACK_medic_finish_spawn);
    if (!determine && !grow && !finish) {
        *handled = false;
        return true;
    }
    q2m_summon_state *state = context->monster->summons;
    if (!state) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Medic summon callback lacks retained choices");
        return false;
    }
    return determine ? q2m_medic_determine_summons(context, state, error)
           : grow    ? q2m_medic_grow_summons(context, state, error)
                     : q2m_medic_finish_summons(context, state, error);
}
