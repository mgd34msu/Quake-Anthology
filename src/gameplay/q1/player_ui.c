#include "wire_internal.h"
#include "qa/game_q1_ui.h"

bool qa_q1_player_ui_powers_read(const qa_q1_game *game, qa_actor_id actor,
    qa_q1_ui_powers *out, qa_error *error)
{
    q1_player *player = game && !game->continuation_pending ?
        q1_player_get((qa_q1_game *)game, actor) : NULL;
    if (!out || !player) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, actor.slot,
            "Q1 UI timers require their actual live player");
        return false;
    }
    qa_q1_ui_powers value = {.seconds = game->time};
    uint64_t cursor = 0;
    for (;;) {
        uint64_t order = 0;
        unsigned chosen = 0;
        for (unsigned i = 0; i < QA_Q1_POWER_COUNT; ++i)
            if (player->power_order[i] > cursor &&
                (!order || player->power_order[i] < order)) {
                order = player->power_order[i];
                chosen = i;
            }
        if (!order) break;
        cursor = order;
        if (player->power_expires[chosen] > value.seconds)
            value.powers[value.count++] = (qa_q1_ui_power){
                (qa_q1_power)chosen, player->power_expires[chosen]};
    }
    *out = value;
    return true;
}

static bool ui_fail(qa_error *error, const char *message)
{ qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message); return false; }

static bool player_ui_read(const qa_q1_game_operation *operation, qa_actor_id actor,
    qa_q1_wire_player *out, qa_error *error)
{
    const qa_q1_game *g = operation->game;
    const q1_player *player = q1_player_get((qa_q1_game *)g, actor);
    if (!player) return ui_fail(error, "Q1 UI lost its actual player");
    qa_q1_weapon weapon = player->weapon;
    int32_t frame = player->weapon_frame;
    double powers[QA_Q1_POWER_COUNT], seconds = g->time;
    bool superhealth=player->source_superhealth;
    memcpy(powers, player->power_expires, sizeof(powers));
    uint32_t bits[QA_Q1_WEAPON_COUNT] = {0};
    for (unsigned shift = 0; shift < 32; ++shift) {
        qa_q1_weapon declared;
        uint32_t bit = UINT32_C(1) << shift;
        if (qa_q1_weapon_source(g->options.program, bit, &declared))
            bits[declared] = bit;
    }
    if ((unsigned)weapon >= QA_Q1_WEAPON_COUNT || !bits[weapon])
        return ui_fail(error, "Q1 source weapon leaves its actual program table");
    qa_q1_wire_player value = {.weapon_model = q1_weapon_model(g, player),
        .weapon_frame = frame, .weapon = bits[weapon], .ammo = player->current_ammo};
    if (!qa_inventory_count_read(g->services.inventory, actor, g->ammo[QA_Q1_SHELLS], &value.shells, error) ||
        !qa_inventory_count_read(g->services.inventory, actor, g->ammo[QA_Q1_NAILS], &value.nails, error) ||
        !qa_inventory_count_read(g->services.inventory, actor, g->ammo[QA_Q1_ROCKETS], &value.rockets, error) ||
        !qa_inventory_count_read(g->services.inventory, actor, g->ammo[QA_Q1_CELLS], &value.cells, error)) return false;
    for (size_t i = 0; i < sizeof(bits)/sizeof(*bits); ++i) {
        if (!bits[i]) continue;
        double count;
        if (!qa_inventory_count_read(g->services.inventory, actor, g->weapons[i], &count, error)) return false;
        if (count > 0) value.items |= bits[i];
    }
    static const char *const keys[] = {"q1:key/silver", "q1:key/gold"};
    for (unsigned i = 0; i < 2; ++i) {
        qa_string_id key = qa_strings_find(qa_session_strings(g->services.session),
            (qa_bytes){(const uint8_t *)keys[i], strlen(keys[i])});
        double count;
        if (!key) continue;
        if (!qa_inventory_count_read(g->services.inventory, actor, key, &count, error)) return false;
        if (count > 0) value.items |= 131072u << i;
    }
    qa_combat_state combat;
    if (!qa_combat_read(g->services.combat,actor,&combat,error)) return false;
    q1_wire_player_items(g->options.program,weapon,powers,seconds,&combat.armor,
        value.items,superhealth,&value.items,&value.items2);
    value.power_items = value.items & (4194304u | 1048576u | 524288u | 2097152u);
    value.power_items2 = value.items2 & (g->options.program == QA_Q1_HIPNOTIC ? 6u :
        g->options.program == QA_Q1_ROGUE ? 192u : 0u);
    if (!qa_q1_game_operation_live(operation)) return false;
    player = q1_player_get((qa_q1_game *)g, actor);
    if (!player) return ui_fail(error, "Q1 UI player retired during inventory observation");
    if (player->weapon != weapon || player->weapon_frame != frame ||
        player->current_ammo != value.ammo || player->source_superhealth!=superhealth || g->time != seconds ||
        q1_weapon_model(g, player) != value.weapon_model ||
        memcmp(player->power_expires, powers, sizeof(powers)))
        return ui_fail(error, "Q1 source player changed during canonical inventory observation");
    *out = value;
    return true;
}

bool qa_q1_player_ui_read(const qa_q1_game *game, qa_actor_id actor,
    qa_q1_wire_player *out, qa_error *error)
{
    if (!out) return ui_fail(error, "Q1 UI requires its player output");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin((qa_q1_game *)game, &operation, error)) return false;
    bool okay = player_ui_read(&operation, actor, out, error);
    qa_q1_game_operation_end(&operation);
    return okay;
}
