#include "maps/internal.h"
#include "qa/game_q1_travel.h"
#include "qa/source_save.h"
#include "qa/game_q1_inventory.h"

typedef enum travel_extension { TRAVEL_NONE, TRAVEL_MG3, TRAVEL_CTF } travel_extension;
struct qa_q1_travel_state {
    size_t references;
    qa_strings *strings;
    float health, max_health;
    qa_armor armor;
    qa_inventory_entry *inventory;
    size_t count;
    qa_q1_weapon weapon;
    travel_extension extension;
    union {
        qa_q1_mg3_progress mg3;
        struct { double last_team, status, access; } ctf;
    } source;
};
typedef struct travel_call {
    qa_q1_game_operation operation;
    qa_actor_id actor;
    q1_player *player;
} travel_call;

static bool fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static bool current(travel_call *call, qa_error *error) {
    qa_q1_game *game = call->operation.game;
    uint32_t slot;
    if (!qa_q1_game_operation_live(&call->operation) || !call->player ||
        q1_player_get(game, call->actor) != call->player)
        return fail(error, "Q1 source travel lost its actual physical client");
    return qa_q1_native_client_slot(game, call->actor, &slot, error);
}
static bool begin(qa_q1_game *game, qa_actor_id actor, travel_call *call, qa_error *error) {
    if (!qa_q1_game_operation_begin(game, &call->operation, error)) return false;
    call->actor = actor;
    call->player = q1_player_get(game, actor);
    return current(call, error);
}
bool qa_q1_travel_retain(qa_q1_travel_state *state, qa_error *error) {
    if (!state || !state->references || state->references == SIZE_MAX)
        return fail(error, "Q1 travel state cannot retain another owner");
    ++state->references;
    return true;
}
void qa_q1_travel_destroy(qa_q1_travel_state *state) {
    if (state && --state->references == 0) { free(state->inventory); free(state); }
}
static bool ctf_read(travel_call *call, const qa_q1_travel_services *services,
    qa_q1_travel_ctf *out, qa_error *error) {
    if (!services || !services->ctf_read)
        return fail(error, "Q1 CTF travel has no actual source controller");
    return services->ctf_read(services->context, call->actor, out, error) &&
        current(call, error);
}
static bool item(travel_call *call, const char *name, qa_item_id *out, qa_error *error) {
    return qa_builtin_resource(&call->operation.game->services, name, out, error) &&
        current(call, error);
}
static bool append(travel_call *call, qa_q1_travel_state *state, qa_item_id id,
    double count, double capacity, qa_error *error) {
    if (!current(call, error)) return false;
    if (!id || state->count == SIZE_MAX / sizeof(*state->inventory))
        return fail(error, "Q1 source travel inventory exceeds its native extent");
    qa_inventory_entry *entries = realloc(state->inventory,
        (state->count + 1) * sizeof(*entries));
    if (!entries) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining Q1 source travel inventory");
        return false;
    }
    state->inventory = entries;
    entries[state->count++] = (qa_inventory_entry){.item = id, .count = count,
        .capacity = capacity, .policy = QA_COUNT_SOURCE_FLOAT};
    return true;
}
static bool new_base(travel_call *call, qa_q1_travel_state *state, qa_error *error) {
    qa_q1_game *game = call->operation.game;
    state->health = state->max_health = game->options.edition == QA_Q1_RERELEASE &&
        game->options.skill == 3 && !game->options.deathmatch ? 50 : 100;
    state->weapon = QA_Q1_SHOTGUN;
    for (int weapon = QA_Q1_AXE; weapon <= QA_Q1_LIGHTNING; ++weapon)
        if (!append(call, state, game->weapons[weapon],
                weapon == QA_Q1_AXE || weapon == QA_Q1_SHOTGUN ? 1 : 0, 1, error)) return false;
    for (int ammo = QA_Q1_SHELLS; ammo <= QA_Q1_CELLS; ++ammo)
        if (!append(call, state, game->ammo[ammo], ammo == QA_Q1_SHELLS ? 25 : 0,
                ammo == QA_Q1_NAILS ? 200 : 100, error)) return false;
    const char *keys[] = {"q1:key/silver", "q1:key/gold"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(*keys); ++i) {
        qa_item_id id;
        if (!item(call, keys[i], &id, error) || !append(call, state, id, 0, 1, error)) return false;
    }
    return true;
}
static bool new_state(travel_call *call, const qa_q1_travel_services *services,
    qa_q1_travel_state *state, qa_error *error) {
    qa_q1_game *game = call->operation.game;
    if (!new_base(call, state, error)) return false;
    if (game->options.program == QA_Q1_HIPNOTIC || game->options.program == QA_Q1_ROGUE) {
        int first = game->options.program == QA_Q1_HIPNOTIC ? QA_Q1_LASER : QA_Q1_LAVA_NAILGUN;
        int last = game->options.program == QA_Q1_HIPNOTIC ? QA_Q1_PROXIMITY : QA_Q1_PLASMA;
        for (int weapon = first; weapon <= last; ++weapon)
            if (!append(call, state, game->weapons[weapon], 0, 1, error)) return false;
    }
    if (game->options.program == QA_Q1_ROGUE) {
        for (int ammo = QA_Q1_LAVA_NAILS; ammo <= QA_Q1_PLASMA_CELLS; ++ammo)
            if (!append(call, state, game->ammo[ammo], 0,
                    ammo == QA_Q1_LAVA_NAILS ? 200 : 100, error)) return false;
        bool grapple = game->options.deathmatch && game->options.teamplay >= 4;
        if (!append(call, state, game->vengeance_item, 0, 1, error) ||
            !append(call, state, game->weapons[QA_Q1_ROGUE_GRAPPLE], grapple ? 1 : 0, 1, error)) return false;
        if (grapple) {
            qa_item_id armor;
            if (!item(call, "q1:armor/green", &armor, error)) return false;
            state->armor.regular = (qa_regular_armor){.kind = QA_ARMOR_Q1,
                .item = armor, .points = 50, .protection.q1_absorption = 0.3f};
        }
    }
    if (game->options.program == QA_Q1_MG3 && !game->options.deathmatch) {
        state->health = state->max_health = 50;
        const double capacities[] = {50, 100, 20, 100};
        for (size_t i = 0; i < state->count; ++i)
            for (size_t ammo = 0; ammo < sizeof(capacities) / sizeof(*capacities); ++ammo)
                if (state->inventory[i].item == game->ammo[ammo])
                    state->inventory[i].capacity = capacities[ammo];
    }
    if (game->options.program == QA_Q1_CTF) {
        qa_q1_travel_ctf source;
        if (!ctf_read(call, services, &source, error)) return false;
        bool lobby = source.start_map && !source.pregame_over;
        state->health = state->max_health = 100;
        state->weapon = lobby ? QA_Q1_AXE : QA_Q1_SHOTGUN;
        for (size_t i = 0; i < state->count; ++i) {
            if (state->inventory[i].item == game->weapons[QA_Q1_SHOTGUN])
                state->inventory[i].count = lobby ? 0 : 1;
            if (state->inventory[i].item == game->ammo[QA_Q1_SHELLS])
                state->inventory[i].count = lobby ? 0 : 40;
        }
        if (!append(call, state, game->weapons[QA_Q1_CTF_GRAPPLE],
                source.grapple_enabled && !lobby && !source.grapple_disabled ? 1 : 0, 1, error)) return false;
        if (!lobby) {
            qa_item_id armor;
            if (!item(call, "q1:item_armor1", &armor, error)) return false;
            state->armor.regular = (qa_regular_armor){.kind = QA_ARMOR_Q1,
                .item = armor, .points = 50, .protection.q1_absorption = 0.3f};
        }
    }
    return true;
}
static qa_q1_travel_state *allocate(qa_session *session, qa_error *error) {
    qa_q1_travel_state *state = calloc(1, sizeof(*state));
    if (!state) qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining Q1 source travel state");
    else { state->references = 1; state->strings = qa_session_strings(session); }
    return state;
}
bool qa_q1_travel_new(qa_q1_game *game, qa_actor_id actor,
    const qa_q1_travel_services *services, qa_q1_travel_state **out, qa_error *error) {
    if (!out) return fail(error, "Q1 new travel requires an owned output");
    *out = NULL;
    travel_call call = {0};
    bool ok = begin(game, actor, &call, error);
    qa_q1_travel_state *state = ok ? allocate(game->services.session, error) : NULL;
    if (ok) ok = state && new_state(&call, services, state, error) && current(&call, error);
    qa_q1_game_operation_end(&call.operation);
    if (ok) *out = state;
    else qa_q1_travel_destroy(state);
    return ok;
}
static bool temporary(qa_q1_game *game, qa_item_id id) {
    const char *names[] = {"q1:key/silver", "q1:key/gold", "q1:powerup/quad",
        "q1:powerup/invulnerability", "q1:powerup/invisibility", "q1:powerup/suit"};
    qa_strings *strings = qa_session_strings(game->services.session);
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (id == qa_strings_find(strings, (qa_bytes){(const uint8_t *)names[i], strlen(names[i])})) return true;
    return false;
}
bool qa_q1_travel_capture(qa_q1_game *game, qa_actor_id actor, qa_item_id selected_weapon,
    const qa_q1_travel_services *services, qa_q1_travel_state **out, qa_error *error) {
    if (!out) return fail(error, "Q1 travel capture requires an owned output");
    *out = NULL;
    travel_call call = {0};
    bool ok = begin(game, actor, &call, error);
    qa_q1_travel_state *state = ok ? allocate(game->services.session, error) : NULL;
    qa_combat_state combat;
    qa_q1_travel_ctf ctf = {0};
    if (ok) ok = state && qa_combat_read(game->services.combat, actor, &combat, error) &&
        current(&call, error);
    bool mission = ok && (game->options.program == QA_Q1_HIPNOTIC || game->options.program == QA_Q1_ROGUE);
    bool addon = ok && (game->options.program == QA_Q1_DOPA || game->options.program == QA_Q1_MG1 ||
        game->options.program == QA_Q1_MG3);
    bool mg3_new_game = ok && game->options.program == QA_Q1_MG3 &&
        qa_q1_game_map_new_game_travel(game);
    bool reset = ok && (combat.health <= 0 || game->options.program == QA_Q1_CTF ||
        (game->options.deathmatch && (!mission || game->options.edition == QA_Q1_RERELEASE)) ||
        (game->options.program == QA_Q1_ROGUE && game->options.teamplay >= 4) ||
        (addon && !mg3_new_game && game->options.world_type == 3));
    if (ok && game->options.program == QA_Q1_MG3) {
        state->extension = TRAVEL_MG3;
        state->source.mg3 = call.player->mg3_progress;
    } else if (ok && game->options.program == QA_Q1_CTF) {
        ok = ctf_read(&call, services, &ctf, error);
        if (ok) {
            state->extension = TRAVEL_CTF;
            state->source.ctf.last_team = combat.health > 0 && (ctf.start_map || ctf.observer) ? -1 : ctf.last_team;
            state->source.ctf.status = ctf.status;
            state->source.ctf.access = ctf.access;
        }
    }
    if (ok && reset) ok = mg3_new_game ? new_base(&call, state, error) :
        new_state(&call, services, state, error);
    else if (ok) {
        state->max_health = mission && game->options.edition == QA_Q1_CLASSIC ? 100 : call.player->max_health;
        state->health = fmaxf(state->max_health / 2, fminf(state->max_health, combat.health));
        state->armor = combat.armor;
        state->weapon = call.player->weapon;
        if (!mission && !addon && selected_weapon) {
            for (int weapon = QA_Q1_AXE; weapon <= QA_Q1_LIGHTNING; ++weapon)
                if (game->weapons[weapon] == selected_weapon) { state->weapon = (qa_q1_weapon)weapon; break; }
        }
        ok = qa_inventory_entries(game->services.inventory, actor, NULL, 0, &state->count, error) &&
            current(&call, error);
        if (ok && state->count > SIZE_MAX / sizeof(*state->inventory))
            ok = fail(error, "Q1 captured inventory exceeds its native extent");
        if (ok && state->count) {
            state->inventory = malloc(state->count * sizeof(*state->inventory));
            if (!state->inventory) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "Capturing Q1 source inventory");
                ok = false;
            }
        }
        size_t count = 0;
        if (ok) ok = qa_inventory_entries(game->services.inventory, actor, state->inventory,
            state->count, &count, error) && current(&call, error);
        if (ok && count > state->count)
            ok = fail(error, "Q1 captured inventory changed its retained extent");
        if (ok) {
            state->count = count;
            for (size_t i = 0; i < count; ++i) {
                if (temporary(game, state->inventory[i].item)) state->inventory[i].count = 0;
                else if (state->inventory[i].item == game->ammo[QA_Q1_SHELLS])
                    state->inventory[i].count = fmax(25, state->inventory[i].count);
            }
        }
    }
    if (ok && mg3_new_game) state->health = state->max_health = 50;
    if (ok) ok = current(&call, error);
    qa_q1_game_operation_end(&call.operation);
    if (ok) *out = state;
    else qa_q1_travel_destroy(state);
    return ok;
}

static bool valid(qa_session *session, const qa_q1_travel_state *state, qa_error *error) {
    if (!session || !state || !isfinite(state->health) || !isfinite(state->max_health) ||
        state->health <= 0 || state->max_health <= 0 || state->weapon < QA_Q1_AXE ||
        state->weapon >= QA_Q1_WEAPON_COUNT || state->extension < TRAVEL_NONE || state->extension > TRAVEL_CTF ||
        (state->count && !state->inventory) || !qa_armor_validate(&state->armor, error))
        return fail(error, "Invalid Q1 source travel state");
    qa_strings *strings = qa_session_strings(session);
    if (state->strings != strings)
        return fail(error, "Q1 travel state belongs to another session string table");
    if ((state->armor.regular.item && !qa_strings_cstr(strings, state->armor.regular.item)) ||
        (state->armor.powered.source_owner &&
         !qa_strings_cstr(strings, state->armor.powered.source_owner)))
        return fail(error, "Q1 travel armor lost its session identity");
    for (size_t i = 0; i < state->count; ++i) {
        qa_inventory_entry normalized;
        if (!state->inventory[i].item || !qa_strings_cstr(strings, state->inventory[i].item))
            return fail(error, "Q1 travel inventory lost its session identity");
        if (!qa_inventory_validate_entry(&state->inventory[i], &normalized, error)) return false;
        if (normalized.count != state->inventory[i].count)
            return fail(error, "Q1 travel inventory has an unnormalized source counter");
        for (size_t j = 0; j < i; ++j)
            if (state->inventory[i].item == state->inventory[j].item)
                return fail(error, "Q1 travel inventory repeats an actual item");
    }
    if (state->extension == TRAVEL_MG3) {
        const qa_q1_mg3_progress *p = &state->source.mg3;
        if ((p->health | p->shells | p->nails | p->rockets | p->cells | p->bloody) > 8388607u)
            return fail(error, "Q1 MG3 travel exceeds its source flag words");
    }
    return true;
}
bool qa_q1_travel_source_valid(const qa_q1_game *game,
    const qa_q1_travel_state *state, qa_error *error) {
    if (!game) return fail(error, "Q1 travel has no actual receiving source owner");
    if (!valid(game->services.session, state, error)) return false;
    if ((state->extension == TRAVEL_MG3 && game->options.program != QA_Q1_MG3) ||
        (state->extension == TRAVEL_CTF && game->options.program != QA_Q1_CTF))
        return fail(error, "Q1 travel extension has no actual receiving source owner");
    return true;
}
static bool cvar(travel_call *call, const char *name, float *out, qa_error *error) {
    qa_q1_game *game = call->operation.game;
    qa_string_id id;
    if (!game->services.cvar)
        return fail(error, "Q1 source travel requires its actual cvar reader");
    return item(call, name, &id, error) &&
        game->services.cvar(q1_cvar_context(game), id, out, error) && current(call, error) &&
        (isfinite(*out) || fail(error, "Q1 source travel read a nonfinite cvar"));
}
static bool configure(travel_call *call, const qa_inventory_entry *entry, qa_error *error) {
    return current(call, error) && qa_inventory_configure(call->operation.game->services.inventory,
        call->actor, entry, NULL, NULL, error) && current(call, error);
}
static bool select_weapon(travel_call *call, qa_q1_weapon weapon,
    const qa_q1_travel_services *services, qa_error *error) {
    qa_q1_game *game = call->operation.game;
    qa_q1_weapon_profile profile;
    if (!qa_q1_weapon_profile_identity(game->options.program, weapon, &profile)) return true;
    double owned;
    if (!qa_inventory_count_read(game->services.inventory, call->actor, game->weapons[weapon],
        &owned, error) || !current(call, error)) return false;
    if (owned == 0) return true;
    if (weapon == QA_Q1_CTF_GRAPPLE) {
        qa_q1_travel_ctf ctf;
        if (!ctf_read(call, services, &ctf, error)) return false;
        if (!ctf.grapple_enabled || ctf.grapple_disabled) return true;
    }
    call->player->weapon = weapon;
    call->player->weapon_frame = 0;
    call->player->continuous = false;
    call->player->animation_at = -1;
    return q1_current_ammo_select(game, call->player, error) &&
        q1_weapon_event(game, call->player, 0, 0, error) && current(call, error);
}
static bool mg3_restore(travel_call *call, const qa_q1_mg3_progress *progress,
    const qa_q1_travel_services *services, qa_error *error) {
    qa_q1_game *game = call->operation.game;
    call->player->mg3_progress = *progress;
    uint32_t *flags = game->maps->options.server_flags;
    if (*flags & 64u) {
        float skill;
        if (!cvar(call, "skill", &skill, error)) return false;
        if (skill != 3) *flags &= ~64u;
        else {
            double hammer;
            if (!qa_inventory_count_read(game->services.inventory, call->actor,
                game->weapons[QA_Q1_MG3_MJOLNIR], &hammer, error) ||
                !current(call, error)) return false;
            qa_inventory_entry *entries = NULL;
            size_t count = 0, written = 0;
            if (!qa_inventory_entries(game->services.inventory, call->actor, NULL, 0,
                &count, error) || !current(call, error)) return false;
            if (count > SIZE_MAX / sizeof(*entries))
                return fail(error, "Q1 MG3 inventory exceeds its native extent");
            if (count) {
                entries = malloc(count * sizeof(*entries));
                if (!entries) {
                    qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring Q1 MG3 inventory");
                    return false;
                }
            }
            bool ok = qa_inventory_entries(game->services.inventory, call->actor, entries,
                count, &written, error) && current(call, error);
            if (ok && written > count)
                ok = fail(error, "Q1 MG3 inventory changed its retained extent");
            for (size_t i = 0; ok && i < written; ++i) {
                const char *name = qa_strings_cstr(qa_session_strings(game->services.session),
                    entries[i].item);
                if (!name) { ok = fail(error, "Q1 MG3 inventory lost its item identity"); break; }
                if (strncmp(name, "q1:weapon/", 10)) continue;
                entries[i].count = entries[i].item == game->weapons[QA_Q1_AXE] ||
                    entries[i].item == game->weapons[QA_Q1_SHOTGUN] ||
                    (hammer != 0 && entries[i].item == game->weapons[QA_Q1_MG3_MJOLNIR]) ||
                    ((progress->bloody & 2u) && entries[i].item == game->weapons[QA_Q1_SUPER_SHOTGUN]);
                ok = configure(call, &entries[i], error);
            }
            free(entries);
            if (!ok) return false;
            if (progress->bloody & 2u) {
                qa_inventory_entry entry = {.item = game->weapons[QA_Q1_SUPER_SHOTGUN],
                    .count = 1, .capacity = 1, .policy = QA_COUNT_SOURCE_FLOAT};
                if (!configure(call, &entry, error)) return false;
            }
            if (call->player->weapon != QA_Q1_AXE &&
                call->player->weapon != QA_Q1_MG3_MJOLNIR &&
                call->player->weapon != QA_Q1_SHOTGUN) call->player->weapon = QA_Q1_SHOTGUN;
        }
    }
    return q1_mg3_capacities(game, call->player, error) && current(call, error) &&
        select_weapon(call, call->player->weapon, services, error);
}
bool qa_q1_travel_admit(qa_q1_game *game, qa_actor_id actor, qa_q1_travel_state *state,
    const qa_q1_travel_services *services, qa_error *error) {
    if (!qa_q1_travel_source_valid(game, state, error) ||
        !qa_q1_travel_retain(state, error)) return false;
    travel_call call = {0};
    bool ok = begin(game, actor, &call, error);
    if (ok && (!game->maps || !game->maps->options.server_flags))
        ok = fail(error, "Q1 travel admission requires its actual map campaign owner");
    const qa_q1_travel_state *decoded = state;
    qa_q1_travel_state reset = {0};
    bool mission = game->options.program == QA_Q1_HIPNOTIC || game->options.program == QA_Q1_ROGUE;
    bool addon = game->options.program == QA_Q1_DOPA || game->options.program == QA_Q1_MG1 ||
        game->options.program == QA_Q1_MG3;
    const char *map = ok ? qa_strings_cstr(qa_session_strings(game->services.session),
        game->maps->options.current_map) : NULL;
    if (ok && !map) ok = fail(error, "Q1 travel admission has no actual source map");
    bool replace = false;
    if (ok) {
        bool start = !strcmp(map, "start");
        uint32_t flags = *game->maps->options.server_flags;
        if (game->options.program == QA_Q1_HIPNOTIC)
            replace = start || !strcmp(map, "hip1m1") || !strcmp(map, "hip2m1") || !strcmp(map, "hip3m1");
        else if (game->options.program == QA_Q1_ROGUE)
            replace = (flags != 0 && start) || (!game->options.deathmatch && !strcmp(map, "r2m1"));
        else if (addon) {
            if (game->options.program == QA_Q1_MG3 && !strcmp(map, "boss2") && game->options.skill == 3)
                *game->maps->options.server_flags |= 64u | 128u;
            float horde = 0;
            if (game->options.program != QA_Q1_MG3) ok = cvar(&call, "horde", &horde, error);
            replace = start || game->options.world_type == 3 || horde != 0;
        } else if (game->options.program == QA_Q1_CTF) {
            qa_q1_travel_ctf ctf;
            ok = ctf_read(&call, services, &ctf, error);
            if (ok) replace = ctf.start_map;
        } else replace = flags != 0 && start;
    }
    if (ok && replace) {
        ok = new_state(&call, services, &reset, error);
        if (ok) { reset.extension = state->extension; reset.source = state->source; decoded = &reset; }
    }
    if (ok) ok = qa_combat_set_health(game->services.combat, actor, decoded->health, error) &&
        current(&call, error) && qa_combat_set_armor(game->services.combat, actor,
            &decoded->armor, error) && current(&call, error);
    if (ok && !qa_inventory_has(game->services.inventory, actor))
        ok = qa_inventory_create_actor(game->services.inventory, actor, NULL, 0, error) && current(&call, error);
    if (ok && mission) {
        qa_q1_travel_state defaults = {0};
        ok = new_state(&call, services, &defaults, error);
        const size_t base_count = (size_t)(QA_Q1_LIGHTNING - QA_Q1_AXE + 1) +
            (size_t)(QA_Q1_CELLS - QA_Q1_SHELLS + 1) + 2;
        for (size_t i = base_count; ok && i < defaults.count; ++i) {
            bool supplied = false;
            for (size_t j = 0; j < decoded->count; ++j)
                if (decoded->inventory[j].item == defaults.inventory[i].item) { supplied = true; break; }
            if (!supplied) ok = configure(&call, &defaults.inventory[i], error);
        }
        free(defaults.inventory);
    }
    for (size_t i = 0; ok && i < decoded->count; ++i) ok = configure(&call, &decoded->inventory[i], error);
    if (ok) {
        call.player->max_health = decoded->max_health;
        ok = qa_q1_player_powers_clear(game, actor, error) && current(&call, error);
        if (ok) call.player->mega_rot_at = -1;
    }
    qa_q1_weapon weapon = decoded->weapon;
    if (game->options.program == QA_Q1_ROGUE && weapon == QA_Q1_ROGUE_GRAPPLE &&
        game->options.teamplay < 4) weapon = QA_Q1_AXE;
    if (ok) ok = select_weapon(&call, weapon, services, error);
    if (ok && decoded->extension == TRAVEL_MG3)
        ok = mg3_restore(&call, &decoded->source.mg3, services, error);
    else if (ok && decoded->extension == TRAVEL_CTF)
        ok = (services && services->ctf_restore ? services->ctf_restore(services->context, actor,
            decoded->source.ctf.last_team, decoded->source.ctf.status, decoded->source.ctf.access, error) :
            fail(error, "Q1 CTF travel has no actual restoring controller")) && current(&call, error);
    if (ok && mission)
        ok = (game->host.set_gravity ? game->host.set_gravity(game->host.context, actor, 1, error) :
            fail(error, "Q1 mission travel requires its selected gravity owner")) && current(&call, error);
    free(reset.inventory);
    qa_q1_game_operation_end(&call.operation);
    qa_q1_travel_destroy(state);
    return ok;
}
static bool armor_number(qa_source_save_io *io, double *value, uint8_t schema) {
    if (schema >= 4) return qa_source_save_f64(io, value);
    float legacy = (float)*value;
    if (!qa_source_save_f32(io, &legacy)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) *value = legacy;
    return true;
}
static bool armor_fields(qa_source_save_io *io, qa_armor *value, uint8_t schema) {
    uint32_t regular = value->regular.kind, powered = value->powered.kind,
        edition = value->powered.source_edition, source_kind = value->powered.source_kind;
    if (!qa_source_save_u32(io, &regular) || regular > QA_ARMOR_SOURCE ||
        !armor_number(io, &value->regular.points, schema) ||
        !qa_source_save_string(io, &value->regular.item)) return false;
    value->regular.kind = (qa_regular_armor_kind)regular;
    switch (value->regular.kind) {
    case QA_ARMOR_Q1:
        if (!qa_source_save_f32(io, &value->regular.protection.q1_absorption)) return false;
        break;
    case QA_ARMOR_Q2:
        if (!armor_number(io, &value->regular.protection.q2.normal, schema) ||
            !armor_number(io, &value->regular.protection.q2.energy, schema)) return false;
        break;
    case QA_ARMOR_Q3:
        if (!qa_source_save_f32(io, &value->regular.protection.q3_protection)) return false;
        break;
    case QA_ARMOR_NONE: case QA_ARMOR_SOURCE: break;
    }
    if (!qa_source_save_u32(io, &powered) || powered > QA_POWER_SHIELD ||
        !armor_number(io, &value->powered.cells, schema) ||
        !qa_source_save_string(io, &value->powered.source_owner) ||
        !qa_source_save_u32(io, &edition) || edition > QA_Q2_POWER_ARMOR_RERELEASE ||
        !qa_source_save_u32(io, &source_kind) || source_kind > QA_POWER_SOURCE_GENERIC) return false;
    value->powered.kind = (qa_power_kind)powered;
    value->powered.source_edition = (qa_q2_power_armor_edition)edition;
    value->powered.source_kind = (qa_power_armor_source)source_kind;
    return true;
}
static bool state_fields(qa_source_save_io *io, qa_q1_travel_state *state) {
    const uint8_t expected[] = {'Q','1','T','R',4};
    uint8_t magic[sizeof(expected)];
    memcpy(magic, expected, sizeof(magic));
    uint32_t weapon = state->weapon, extension = state->extension;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, expected, 4) || magic[4] < 3 || magic[4] > 4 ||
        !qa_source_save_f32(io, &state->health) || !qa_source_save_f32(io, &state->max_health) ||
        !armor_fields(io, &state->armor, magic[4]) || !qa_source_save_u32(io, &weapon) ||
        weapon >= QA_Q1_WEAPON_COUNT || !qa_source_save_u32(io, &extension) || extension > TRAVEL_CTF ||
        !qa_source_save_count(io, &state->count, SIZE_MAX / sizeof(*state->inventory))) return false;
    state->weapon = (qa_q1_weapon)weapon;
    state->extension = (travel_extension)extension;
    if (io->direction == QA_SOURCE_SAVE_READ && state->count) {
        if (state->count > (io->input.size - io->offset) / 25) return false;
        state->inventory = calloc(state->count, sizeof(*state->inventory));
        if (!state->inventory) {
            qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Decoding Q1 source travel inventory");
            return false;
        }
    }
    for (size_t i = 0; i < state->count; ++i) {
        qa_inventory_entry *entry = &state->inventory[i];
        uint32_t policy = entry->policy;
        if (!qa_source_save_string(io, &entry->item) || !qa_source_save_f64(io, &entry->count) ||
            !qa_source_save_f64(io, &entry->capacity) || !qa_source_save_u32(io, &policy) ||
            policy > QA_COUNT_SOURCE_DOUBLE) return false;
        entry->policy = (qa_inventory_count_policy)policy;
    }
    if (state->extension == TRAVEL_MG3) {
        qa_q1_mg3_progress *p = &state->source.mg3;
        return qa_source_save_u32(io, &p->health) && qa_source_save_u32(io, &p->shells) &&
            qa_source_save_u32(io, &p->nails) && qa_source_save_u32(io, &p->rockets) &&
            qa_source_save_u32(io, &p->cells) && qa_source_save_u32(io, &p->bloody);
    }
    return state->extension != TRAVEL_CTF ||
        (qa_source_save_f64(io, &state->source.ctf.last_team) &&
         qa_source_save_f64(io, &state->source.ctf.status) &&
         qa_source_save_f64(io, &state->source.ctf.access));
}
bool qa_q1_travel_encode(qa_session *session, const qa_q1_travel_state *state,
    qa_buffer *out, qa_error *error) {
    if (!out || !valid(session, state, error)) return false;
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, session, error)) return false;
    qa_q1_travel_state copy = *state;
    bool ok = state_fields(&io, &copy) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}
bool qa_q1_travel_decode(qa_session *session, qa_bytes bytes,
    qa_q1_travel_state **out, qa_error *error) {
    if (!session || !out) return fail(error, "Q1 source travel decode requires its actual session");
    *out = NULL;
    qa_q1_travel_state *state = allocate(session, error);
    if (!state) return false;
    qa_source_save_io io;
    bool ok = qa_source_save_reader(&io, session, bytes, error);
    if (ok) ok = state_fields(&io, state) && qa_source_save_finish(&io, NULL) && valid(session, state, error);
    if (!ok && (!error || error->code == QA_OK))
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid Q1 source travel continuation");
    qa_source_save_dispose(&io);
    if (ok) *out = state;
    else qa_q1_travel_destroy(state);
    return ok;
}
