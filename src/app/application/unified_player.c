#include "unified_player.h"
#include "unified_output_json.h"
#include "internal.h"
#include "control_frame.h"
#include "guest_q3_private.h"
#include "guest_q3_catalog.h"
#include "guest_q3_weapons.h"
#include "guest_projection_private.h"
#include "guest_native_q2_private.h"
#include "native_q2_inventory_scanner.h"
#include "equipment_runtime.h"
#include "map_players_private.h"
#include "qa/application_equipment.h"
#include "qa/application_native_q3_presentation.h"
#include "qa/game_q1_inventory.h"
#include "qa/game_q1_ui.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q2_wire.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include "qa/native_host_q2_wire.h"

#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct player_observation {
    qa_application *app;
    const application_unified_source *source;
    const qa_unified_session_player *player;
    const application_unified_player_external *external;
    qa_net_client_id client;
    application_provider *primary, *arsenal, *q3_ui_owner;
    qa_actor_id ui_actor;
    uint64_t actors_revision;
    qa_combat_state combat;
    qa_inventory_entry *inventory;
    size_t inventory_count;
    qa_item_definition *definitions;
    size_t definition_count;
    qa_application_equipment_view equipment;
    char *equipment_label;
    const qa_q2_weapon_definition *native_weapon;
    application_q3_catalog_weapon *q3_weapons;
    size_t q3_weapon_count;
    qa_item_id q3_active;
    bool q3_inventory, q3_combat, q3_standard;
    qa_q3_product q3_product;
    qa_application_qc_player_ui qc;
    qa_application_qc_player_ui selected_qc;
    qa_application_qc_weapon_ui_binding *qc_bindings;
    size_t qc_binding_count;
    double qc_ammo;
    bool has_qc, has_selected_qc;
    qa_equipment_weapon_view gear;
    bool has_gear;
    qa_q3_player q3;
    qa_q2_player q2;
    int32_t q3_time;
    bool has_q3, has_q2, has_equipment, intermission;
} player_observation;

static bool text(application_unified_json *j, const char *s, qa_error *e)
{ return application_unified_json_text(j, s, e); }
static bool string(application_unified_json *j, const char *s, qa_error *e)
{ return application_unified_json_string(j, s, e); }
static bool number(application_unified_json *j, double n, qa_error *e)
{ return application_unified_json_number(j, n, e); }
static bool boolean(application_unified_json *j, bool n, qa_error *e)
{ return text(j, n ? "true" : "false", e); }
static bool vector(application_unified_json *j, qa_vec3 v, qa_error *e)
{ return application_unified_json_vector(j, v, e); }

static bool current(player_observation *o, qa_error *error)
{
    return (application_unified_source_current(o->app, o->source) &&
        application_unified_player_current(o->app, o->client, o->player) &&
        qa_actors_revision(qa_session_actors(o->source->session)) == o->actors_revision &&
        qa_actors_get(qa_session_actors(o->source->session), o->ui_actor) &&
        (!o->has_qc || qa_application_qc_message_player_ui_current(o->app, &o->qc)) &&
        (!o->has_selected_qc || qa_application_qc_message_player_ui_current(o->app, &o->selected_qc)) &&
        (!o->has_gear || (qa_equipment_weapon_view_current(o->app->equipment, &o->gear) &&
            application_equipment_runtime_owner_current(o->app->equipment_runtime, o->gear.source.owner))) &&
        (!o->has_equipment || qa_application_equipment_current(o->app, &o->equipment)) &&
        (!o->external || (o->external->current && o->external->current(o->external->context,
            o->app, o->source, o->client, o->player)))) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Unified player changed its actual Source or recipient");
}

static const char *identity(player_observation *o, qa_string_id id)
{ return qa_strings_cstr(qa_session_strings(o->source->session), id); }
static bool item(application_unified_json *j, player_observation *o, qa_item_id id, qa_error *e)
{ return id ? string(j, identity(o, id), e) : text(j, "null", e); }
static double count(player_observation *o, qa_item_id id)
{
    if (o->has_q3 && o->primary->kind != APPLICATION_PROVIDER_Q3 &&
        o->arsenal == o->primary && !o->q3_inventory)
        for (size_t i = 0; i < o->q3_weapon_count; ++i) {
            const application_q3_catalog_weapon *w = o->q3_weapons + i;
            if (w->item == id) return ((uint32_t)o->q3.stats[2] & (UINT32_C(1) << w->weapon)) ? 1 : 0;
            if (w->ammo == id) return o->q3.ammo[w->weapon];
        }
    for (size_t i = 0; i < o->inventory_count; ++i)
        if (o->inventory[i].item == id) return o->inventory[i].count;
    return 0;
}
static bool native_original(const player_observation *o)
{ return o->has_q2 && o->arsenal == o->primary && !o->equipment.equipment_slot; }
static bool catalog_q3(const player_observation *o)
{ return o->q3_ui_owner == o->arsenal && o->q3_ui_owner; }
static const application_q3_catalog_weapon *q3_active(const player_observation *o)
{
    for (size_t i = 0; i < o->q3_weapon_count; ++i)
        if (o->q3_weapons[i].item == o->q3_active) return o->q3_weapons + i;
    return NULL;
}

static bool provider(application_unified_json *j, application_provider *p, qa_error *e)
{
    return p && p->constructed && p->attached && !p->close_pending && p->launch && p->product &&
        text(j, "{\"provider\":", e) && string(j, p->launch->selection.instance, e) &&
        text(j, ",\"content\":", e) && string(j, p->product->identity, e) && text(j, "}", e);
}

static void qc_bindings_clear(player_observation *o)
{
    for (size_t i = 0; i < o->qc_binding_count; ++i) free((char *)o->qc_bindings[i].label);
    free(o->qc_bindings); o->qc_bindings = NULL; o->qc_binding_count = 0;
}
static bool qc_bindings_read(player_observation *o, const qa_application_qc_player_ui *view, qa_error *e)
{
    qc_bindings_clear(o);
    if (view->binding_count > SIZE_MAX / sizeof(*o->qc_bindings))
        return application_fail(e, QA_ERROR_MEMORY, "QC UI bindings exceed their host extent");
    o->qc_bindings = view->binding_count ? calloc(view->binding_count, sizeof(*o->qc_bindings)) : NULL;
    if (view->binding_count && !o->qc_bindings)
        return application_fail(e, QA_ERROR_MEMORY, "Retaining actual QC UI declarations");
    for (size_t i = 0; i < view->binding_count; ++i) {
        qa_application_qc_weapon_ui_binding binding;
        if (!qa_application_qc_message_player_ui_binding(o->app, view, i, &binding, e) || !current(o, e)) return false;
        if (!binding.item || !binding.label)
            return application_fail(e, QA_ERROR_FORMAT, "QC UI binding lacks its genuine item or label");
        size_t length = strlen(binding.label);
        char *label = malloc(length + 1);
        if (!label) return application_fail(e, QA_ERROR_MEMORY, "Copying actual QC weapon label");
        memcpy(label, binding.label, length + 1);
        o->qc_bindings[i] = binding; o->qc_bindings[i].label = label;
        o->qc_binding_count = i + 1;
    }
    return current(o, e);
}
static bool qc_read(player_observation *o, qa_error *e)
{
    if (!qa_application_qc_message_player_ui_read(o->app, &o->external->camera->source,
        o->player->actor, &o->qc, e) || !current(o, e)) return false;
    o->has_qc = true;
    o->qc_ammo = o->external->has_qc_ammo ? o->external->qc_ammo : o->qc.current_ammo;
    return qc_bindings_read(o, &o->qc, e);
}
static const qa_application_qc_player_ui *qc_weapons(const player_observation *o)
{ return o->has_selected_qc ? &o->selected_qc : &o->qc; }
static bool qc_arsenal(const player_observation *o)
{ return (o->has_qc || o->has_selected_qc) && o->arsenal->kind == APPLICATION_PROVIDER_QC &&
    qc_weapons(o)->source.provider == o->arsenal->owner; }

static const qa_application_qc_weapon_ui_binding *qc_active(const player_observation *o)
{
    for (size_t i = 0; i < o->qc_binding_count; ++i)
        if (o->qc_bindings[i].bit == qc_weapons(o)->weapon) return o->qc_bindings + i;
    return NULL;
}
static const char *qc_ammo_item(const player_observation *o)
{
    static const uint32_t bits[] = {256, 512, 1024, 2048};
    static const char *const names[] = {"q1:ammo/shells", "q1:ammo/nails", "q1:ammo/rockets", "q1:ammo/cells"};
    for (size_t i = 0; i < sizeof(bits) / sizeof(bits[0]); ++i)
        if (qc_weapons(o)->items & bits[i]) return names[i];
    return NULL;
}

static void q3_catalog_clear(player_observation *o)
{
    for (size_t i = 0; i < o->q3_weapon_count; ++i) free((char *)o->q3_weapons[i].label);
    free(o->q3_weapons); o->q3_weapons = NULL; o->q3_weapon_count = 0;
    o->q3_ui_owner = NULL; o->q3_active = 0;
}

static bool q3_catalog_read(player_observation *o, application_provider *p,
    struct application_q3_guest *engine, const qa_q3_player *ps, qa_error *e)
{
    const application_q3_catalog_weapon *weapons;
    size_t length;
    if (!engine->game->catalog)
        return application_fail(e, QA_ERROR_NOT_FOUND, "Original Q3 UI lacks its real complete source catalog");
    if (!application_q3_catalog_weapons(engine->game->catalog, &weapons, &length, e) || !current(o, e)) return false;
    q3_catalog_clear(o);
    if (length > SIZE_MAX / sizeof(*o->q3_weapons))
        return application_fail(e, QA_ERROR_MEMORY, "Original Q3 catalog exceeds its host extent");
    o->q3_weapons = length ? calloc(length, sizeof(*o->q3_weapons)) : NULL;
    if (length && !o->q3_weapons) return application_fail(e, QA_ERROR_MEMORY, "Retaining actual Q3 UI catalog");
    for (size_t i = 0; i < length; ++i) {
        if (weapons[i].weapon <= 0 || (size_t)weapons[i].weapon >= sizeof(ps->ammo) / sizeof(ps->ammo[0]) ||
            weapons[i].weapon >= 32 || !weapons[i].label || !weapons[i].item)
            return application_fail(e, QA_ERROR_FORMAT, "Original Q3 UI catalog leaves its actual PS weapon fields");
        size_t bytes = strlen(weapons[i].label);
        char *label = malloc(bytes + 1);
        if (!label) return application_fail(e, QA_ERROR_MEMORY, "Copying real Q3 source item label");
        memcpy(label, weapons[i].label, bytes + 1);
        o->q3_weapons[i] = weapons[i]; o->q3_weapons[i].label = label;
        o->q3_weapon_count = i + 1;
    }
    o->q3_standard = application_q3_catalog_standard(engine->game->catalog);
    o->q3_product = engine->product;
    if (engine->game->weapons) {
        if (!application_q3_weapons_active(engine->game->weapons, o->ui_actor, &o->q3_active, e) || !current(o, e)) return false;
    } else for (size_t i = 0; i < o->q3_weapon_count; ++i)
        if (o->q3_weapons[i].weapon == ps->weapon) { o->q3_active = o->q3_weapons[i].item; break; }
    if ((engine->game->weapons ? o->q3_active != 0 : ps->weapon != 0) && !q3_active(o))
        return application_fail(e, QA_ERROR_FORMAT, "Original Q3 active weapon is absent from its authentic item catalog");
    o->q3_ui_owner = p;
    return current(o, e);
}

static bool q3_read(player_observation *o, qa_error *e)
{
    if (o->primary->kind == APPLICATION_PROVIDER_Q3) {
        qa_application_native_q3_presentation cut;
        qa_application_native_q3_client client;
        if (!qa_application_native_q3_presentation_read(o->app, o->source->owner, &cut, e) ||
            !current(o, e) || !qa_application_native_q3_presentation_client(o->app,
                &cut, o->player->source_slot, &client, e) ||
            !qa_application_native_q3_presentation_current(o->app, &cut) || !current(o, e)) return false;
        if (!client.present)
            return application_fail(e, QA_ERROR_NOT_FOUND, "Q3 UI lost its actual physical source client");
        o->q3 = client.player;
        o->q3_time = cut.source_time_ms;
    } else {
        struct application_q3_guest *engine = q3g_engine(o->primary);
        if (!engine || !engine->game || !engine->game->host ||
            !qa_q3_host_source_player(engine->game->host, o->player->source_slot, &o->q3, e) ||
            !current(o, e)) return false;
        o->q3_time = engine->milliseconds;
        if (!q3_catalog_read(o, o->primary, engine, &o->q3, e)) return false;
        o->q3_inventory = engine->game->projection && engine->game->projection->has_inventory;
        o->q3_combat = engine->game->combat != NULL;
    }
    o->has_q3 = true;
    return true;
}

static bool copy_inventory(player_observation *o, qa_error *e)
{
    size_t extent = 0, actual = 0;
    if (!qa_inventory_entries(o->app->inventory, o->ui_actor, NULL, 0, &extent, e) || !current(o, e)) return false;
    if (extent > SIZE_MAX / sizeof(*o->inventory))
        return application_fail(e, QA_ERROR_MEMORY, "Unified inventory exceeds its host extent");
    o->inventory = extent ? calloc(extent, sizeof(*o->inventory)) : NULL;
    if (extent && !o->inventory) return application_fail(e, QA_ERROR_MEMORY, "Copying actual player inventory");
    if (!qa_inventory_entries(o->app->inventory, o->ui_actor, o->inventory, extent, &actual, e) ||
        actual != extent || !current(o, e)) return false;
    o->inventory_count = actual;
    extent = 0;
    if (!qa_inventory_item_definitions(o->app->inventory, o->ui_actor, NULL, 0, &extent, e) || !current(o, e)) return false;
    if (extent > SIZE_MAX / sizeof(*o->definitions))
        return application_fail(e, QA_ERROR_MEMORY, "Unified item definitions exceed their host extent");
    o->definitions = extent ? calloc(extent, sizeof(*o->definitions)) : NULL;
    if (extent && !o->definitions) return application_fail(e, QA_ERROR_MEMORY, "Copying actual player item declarations");
    if (!qa_inventory_item_definitions(o->app->inventory, o->ui_actor, o->definitions, extent, &actual, e) ||
        actual != extent || !current(o, e)) return false;
    /* Labels are borrowed by the inventory registry; copy them before any
     * provider observation can close their original declaration group. */
    for (size_t i = 0; i < actual; ++i) {
        const char *label = o->definitions[i].label;
        o->definitions[i].label = NULL;
        if (!label) return application_fail(e, QA_ERROR_FORMAT, "Player item has no actual declared label");
        size_t length = strlen(label);
        char *copy = malloc(length + 1);
        if (!copy) return application_fail(e, QA_ERROR_MEMORY, "Retaining player item label");
        memcpy(copy, label, length + 1);
        o->definitions[i].label = copy;
        o->definition_count = i + 1;
    }
    return true;
}

static bool armor(application_unified_json *j, player_observation *o, qa_error *e)
{
    if (o->has_q3 && o->primary->kind != APPLICATION_PROVIDER_Q3 && !o->q3_combat) {
        if (!text(j, "{\"regular\":{\"kind\":", e) || !string(j, o->q3.stats[3] ? "q3" : "none", e)) return false;
        if (o->q3.stats[3] && (!text(j, ",\"points\":", e) || !number(j, o->q3.stats[3], e) ||
            !text(j, ",\"protection\":", e) || !number(j, (float).66, e))) return false;
        return text(j, "},\"powered\":{\"kind\":\"none\"}}", e);
    }
    if (o->has_q2) {
        if (!text(j, "{\"regular\":{\"kind\":", e) ||
            !string(j, o->q2.stats[5] ? "q2" : "none", e)) return false;
        if (o->q2.stats[5] && (!text(j, ",\"points\":", e) || !number(j, o->q2.stats[5], e) ||
            !text(j, ",\"normalProtection\":0,\"energyProtection\":0,\"item\":\"q2:remote-armor\"", e))) return false;
        return text(j, "},\"powered\":{\"kind\":\"none\"}}", e);
    }
    qa_regular_armor r = o->combat.armor.regular;
    static const char *const names[] = {"none", "q1", "q2", "q3", "source"};
    if ((unsigned)r.kind >= sizeof(names) / sizeof(names[0]))
        return application_fail(e, QA_ERROR_FORMAT, "Player armor lost its actual protection kind");
    if (!text(j, "{\"regular\":{\"kind\":", e) || !string(j, names[r.kind], e)) return false;
    if (r.kind != QA_ARMOR_NONE) {
        if (!text(j, ",\"points\":", e) || !number(j, r.points, e) ||
            !text(j, ",\"item\":", e) || !item(j, o, r.item, e)) return false;
        if (r.kind == QA_ARMOR_Q1 && (!text(j, ",\"absorption\":", e) || !number(j, r.protection.q1_absorption, e))) return false;
        if (r.kind == QA_ARMOR_Q2 && (!text(j, ",\"normalProtection\":", e) || !number(j, r.protection.q2.normal, e) ||
            !text(j, ",\"energyProtection\":", e) || !number(j, r.protection.q2.energy, e))) return false;
        if (r.kind == QA_ARMOR_Q3 && (!text(j, ",\"protection\":", e) || !number(j, r.protection.q3_protection, e))) return false;
    }
    qa_powered_armor p = o->combat.armor.powered;
    if (p.kind < QA_POWER_NONE || p.kind > QA_POWER_SHIELD)
        return application_fail(e, QA_ERROR_FORMAT, "Player powered armor lost its actual source kind");
    if (!text(j, "},\"powered\":{\"kind\":", e) ||
        !string(j, p.kind == QA_POWER_NONE ? "none" : p.kind == QA_POWER_SCREEN ? "screen" : "shield", e)) return false;
    return (p.kind == QA_POWER_NONE || (text(j, ",\"cells\":", e) && number(j, p.cells, e))) && text(j, "}}", e);
}

static bool inventory(application_unified_json *j, player_observation *o, qa_error *e)
{
    if (!text(j, "[", e)) return false;
    if (o->has_q3 && o->primary->kind != APPLICATION_PROVIDER_Q3 && o->arsenal == o->primary && !o->q3_inventory) {
        for (size_t i = 0; i < o->q3_weapon_count; ++i) {
            const application_q3_catalog_weapon *w = o->q3_weapons + i;
            if ((i && !text(j, ",", e)) || !text(j, "{\"item\":", e) || !item(j, o, w->item, e) ||
                !text(j, ",\"count\":", e) || !number(j, count(o, w->item), e) || !text(j, ",\"capacity\":1}", e)) return false;
            if (w->ammo && (!text(j, ",{\"item\":", e) || !item(j, o, w->ammo, e) || !text(j, ",\"count\":", e) ||
                !number(j, count(o, w->ammo), e) || !text(j, ",\"capacity\":200}", e))) return false;
        }
        return text(j, "]", e);
    }
    for (size_t i = 0; i < o->inventory_count; ++i) {
        qa_inventory_entry value = o->inventory[i];
        if ((i && !text(j, ",", e)) || !text(j, "{\"item\":", e) || !item(j, o, value.item, e) ||
            !text(j, ",\"count\":", e) || !number(j, value.count, e) ||
            !text(j, ",\"capacity\":", e) || !number(j, value.capacity, e)) return false;
        if (value.policy != QA_COUNT_STACK && (!text(j, ",\"countPolicy\":{\"kind\":\"source-counter\",\"arithmetic\":", e) ||
            !string(j, value.policy == QA_COUNT_SOURCE_INT32 ? "int32" : "binary32", e) || !text(j, "}", e))) return false;
        if (!text(j, "}", e)) return false;
    }
    return text(j, "]", e);
}

static bool timer(application_unified_json *j, bool *first, const char *id,
    const char *label, double seconds, qa_error *e)
{
    if (!(seconds > 0)) return true;
    if ((!*first && !text(j, ",", e)) || !text(j, "{\"item\":", e) || !string(j, id, e) ||
        !text(j, ",\"label\":", e) || !string(j, label, e) ||
        !text(j, ",\"remainingSeconds\":", e) || !number(j, seconds, e) || !text(j, "}", e)) return false;
    *first = false;
    return true;
}

static bool timers(application_unified_json *j, player_observation *o, qa_error *e)
{
    if (!text(j, "[", e)) return false;
    bool first = true;
    if (o->primary->kind == APPLICATION_PROVIDER_QC) {
        for (size_t i = 0; i < o->qc.timer_count; ++i) {
            const qa_application_qc_power_timer *power = o->qc.timers + i;
            if (!timer(j, &first, power->item, power->label, power->expires_seconds - o->qc.now_seconds, e)) return false;
        }
    } else if (o->primary->kind == APPLICATION_PROVIDER_Q1) {
        static const char *const ids[QA_Q1_POWER_COUNT] = {"q1:item_artifact_super_damage", "q1:item_artifact_invulnerability",
            "q1:item_artifact_invisibility", "q1:item_artifact_envirosuit", "q1:item_artifact_wetsuit",
            "q1:item_artifact_empathy_shields", "q1:item_powerup_shield", "q1:item_powerup_belt", "q1:item_artifact_lavasuit"};
        static const char *const labels[QA_Q1_POWER_COUNT] = {"Quad Damage", "Invulnerability", "Invisibility",
            "Environment Suit", "Wetsuit", "Empathy Shields", "Power Shield", "Anti-gravity Belt", "Lava Suit"};
        qa_q1_ui_powers powers;
        if (!qa_q1_player_ui_powers_read(o->primary->state.q1, o->ui_actor, &powers, e) || !current(o, e)) return false;
        for (size_t i = 0; i < powers.count; ++i) {
            qa_q1_ui_power power = powers.powers[i];
            if (!timer(j, &first, ids[power.power], labels[power.power], power.expires - powers.seconds, e)) return false;
        }
    } else if (o->primary->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_powerups powers;
        if (!qa_q2_powerups_read(o->primary->state.q2, o->ui_actor, &powers, e) || !current(o, e)) return false;
        const uint64_t expires[] = {powers.quad_until_ns, powers.quad_fire_until_ns, powers.double_until_ns,
            powers.invulnerability_until_ns, powers.enviro_until_ns, powers.breather_until_ns, powers.ir_until_ns};
        static const char *const ids[] = {"q2:item_quad", "q2:item_quadfire", "q2:item_double", "q2:item_invulnerability",
            "q2:item_enviro", "q2:item_breather", "q2:item_ir_goggles"};
        static const char *const labels[] = {"Quad Damage", "DualFire Damage", "Double Damage", "Invulnerability",
            "Environment Suit", "Rebreather", "IR Goggles"};
        qa_clock_state clock;
        if (!qa_session_clock(o->source->session, o->primary->owner, &clock))
            return application_fail(e, QA_ERROR_NOT_FOUND, "Q2 UI powers have no actual source clock");
        for (size_t i = 0; i < sizeof(expires) / sizeof(expires[0]); ++i)
            if (expires[i] > clock.frame.time_ns && !timer(j, &first, ids[i], labels[i],
                (double)(expires[i] - clock.frame.time_ns) / 1e9, e)) return false;
    } else if (o->has_q3) {
        static const char *const ids[] = {"q3:item_quad", "q3:item_enviro", "q3:item_haste", "q3:item_invis", "q3:item_regen", "q3:item_flight"};
        static const char *const labels[] = {"Quad Damage", "Battle Suit", "Haste", "Invisibility", "Regeneration", "Flight"};
        qa_q3_player viewed = o->q3;
        qa_actor_id actor = o->ui_actor;
        if (o->primary->kind == APPLICATION_PROVIDER_Q3 && (o->q3.pmFlags & 4096)) {
            qa_q3_source_binding binding;
            if (o->q3.clientNum < 0 || !qa_q3_source_binding_read(o->primary->state.q3,
                (uint32_t)o->q3.clientNum, &binding, e) || !current(o, e) ||
                !qa_q3_wire_player_read(o->primary->state.q3, (uint32_t)o->q3.clientNum, &viewed, e) ||
                !current(o, e)) return false;
            actor = binding.actor;
        }
        for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
            if (!timer(j, &first, ids[i], labels[i], ((double)viewed.powerups[i + 1] - o->q3_time) / 1000, e)) return false;
        if (o->primary->kind == APPLICATION_PROVIDER_Q3) {
            qa_q3_player_state state;
            if (!qa_q3_player_read(o->primary->state.q3, actor, &state))
                return application_fail(e, QA_ERROR_NOT_FOUND, "Q3 UI invulnerability lost its viewed source client");
            if (!timer(j, &first, "q3:holdable_invulnerability", "Invulnerability",
                ((double)state.invulnerability_until - o->q3_time) / 1000, e)) return false;
        }
    } /* Original Q2's public PS exposes no complete source timer collection. */
    return text(j, "]", e);
}

static bool blend(application_unified_json *j, const float value[4], qa_error *e)
{
    return text(j, "{\"x\":", e) && number(j, value[0], e) && text(j, ",\"y\":", e) &&
        number(j, value[1], e) && text(j, ",\"z\":", e) && number(j, value[2], e) &&
        text(j, ",\"w\":", e) && number(j, value[3], e) && text(j, "}", e);
}

static qa_vec3 from_array(const float v[3]) { return qa_v3(v[0], v[1], v[2]); }

static bool view(application_unified_json *j, player_observation *o, qa_error *e)
{
    qa_application_control_view control;
    qa_application_camera_view camera;
    if (!qa_application_control_read(o->app, o->player->actor, &control) ||
        !qa_application_control_camera(o->app, o->player->actor, &camera) || !current(o, e))
        return application_fail(e, QA_ERROR_NOT_FOUND, "Unified player lost its selected camera owner");
    qa_vec3 origin = camera.origin, angles = control.view_angles, kick = qa_v3(0, 0, 0);
    qa_vec3 client_delta = qa_v3(0, 0, 0);
    float height = control.view_height, fov = 90;
    float rgba[4] = {0}, damage_rgba[4] = {0};
    bool has_blend = false, has_damage = false, foreign_death = false, has_fov = false;
    bool has_death_yaw = false;
    double death_yaw = 0;
    qa_combat_state view_combat = o->combat;
    if (!qa_actor_id_equal(o->ui_actor, o->player->actor) &&
        (!qa_combat_read(o->app->combat, o->player->actor, &view_combat, e) || !current(o, e))) return false;
    if (o->primary->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_wire_view source;
        if (!qa_q2_wire_view_read(o->primary->state.q2, o->player->actor, &source, e) ||
            !source.present || !current(o, e)) return false;
        fov = source.view.fov; has_fov = true;
    }
    application_provider *character = application_provider_for(o->app, o->player->actor, QA_ROLE_CHARACTER, "");
    if (o->primary->kind == APPLICATION_PROVIDER_QC && !camera.cutscene) {
        const application_unified_player_camera *local = o->external->camera;
        qa_vec3 offset = qa_v3(0, 0, 0);
        if (!local->intermission &&
            (!qa_application_qc_message_view_offset(o->app, &local->source, o->player->actor, &offset, e) ||
             !current(o, e))) return false;
        bool targeted = local->intermission || (local->view_entity.registry &&
            !qa_actor_id_equal(local->view_entity, o->player->actor));
        qa_actor_id target = local->view_entity.registry ? local->view_entity : o->player->actor;
        uint64_t serial = targeted ? qa_world_body_storage_serial(o->source->world, target) : 0;
        if (serial) {
            qa_body_state body;
            if (!qa_world_body_read(o->source->world, target, &body, e) || !current(o, e) ||
                qa_world_body_storage_serial(o->source->world, target) != serial)
                return application_fail(e, QA_ERROR_ARGUMENT, "QC camera target changed its genuine body during observation");
            if (local->intermission) offset = qa_v3(0, 0, 0);
            origin = qa_vec_add(body.origin, qa_v3(offset.x, offset.y, 0));
            height = offset.z;
            angles = local->has_angles ? local->angles : body.angles;
        } else if (!local->intermission) {
            origin = qa_vec_add(origin, qa_v3(offset.x, offset.y, 0));
            height = offset.z;
            if (view_combat.health <= 0) angles.z = 80;
        }
    } else if (o->primary->kind == APPLICATION_PROVIDER_NATIVE && o->primary->state.native.q2_engine) {
        qa_q2_player ps = o->q2;
        bool classic = o->primary->state.native.q2_engine->profile == QA_NATIVE_Q2_GAME_API3;
        qa_vec3 movement = classic ? qa_v3((float)ps.pmove.origin[0] / 8, (float)ps.pmove.origin[1] / 8,
            (float)ps.pmove.origin[2] / 8) : from_array(ps.pmove.origin_f);
        origin = qa_vec_add(movement, from_array(ps.viewoffset));
        if (classic) origin.z = movement.z;
        height = classic ? ps.viewoffset[2] : (float)ps.pmove.viewheight;
        angles = from_array(ps.viewangles);
        kick = from_array(ps.kick_angles); fov = ps.fov; has_fov = true;
        memcpy(rgba, ps.blend, sizeof(rgba)); memcpy(damage_rgba, ps.damage_blend, sizeof(damage_rgba));
        has_blend = true; has_damage = !classic;
    } else if (o->has_q3 && o->primary->kind != APPLICATION_PROVIDER_Q3) {
        origin = from_array(o->q3.origin); angles = from_array(o->q3.viewangles); height = (float)o->q3.viewheight;
    } else if (character && character->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_wire_view source;
        if (!qa_q2_wire_view_read(character->state.q2, o->player->actor, &source, e) ||
            !source.present || !current(o, e)) return false;
        origin = qa_vec_add(camera.origin, qa_v3(source.view.offset.x, source.view.offset.y, 0));
        height = source.view.offset.z; fov = source.view.fov; has_fov = true;
        if (o->primary->kind == APPLICATION_PROVIDER_Q2 && o->primary->product->edition == QA_EDITION_RERELEASE && !o->intermission)
            height += control.view_height;
        angles = o->intermission || view_combat.health <= 0 ? source.view.angles : control.view_angles;
        angles = qa_vec_add(angles, source.view.kick_angles);
        foreign_death = o->source->family == QA_GAME_Q3 && view_combat.health <= 0 && !o->intermission;
    } else if (character && character->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_character_view source;
        if (!qa_q1_character_read(character->state.q1, o->player->actor, &source))
            return application_fail(e, QA_ERROR_NOT_FOUND, "Unified camera lost its real Q1 character");
        if (source.life != QA_Q1_ALIVE && view_combat.health <= 0 && !o->intermission) {
            origin = qa_vec_add(camera.origin, qa_v3(source.view_offset.x, source.view_offset.y, 0));
            height = source.view_offset.z; angles.z = 80; has_fov = true;
            foreign_death = o->source->family == QA_GAME_Q3;
        }
    } else if (character && character->kind == APPLICATION_PROVIDER_Q3 && o->source->family != QA_GAME_Q3) {
        qa_q3_player_state source;
        if (!qa_q3_player_read(character->state.q3, o->player->actor, &source))
            return application_fail(e, QA_ERROR_NOT_FOUND, "Unified camera lost its real Q3 character");
        if (source.dead && view_combat.health <= 0 && !o->intermission) {
            qa_attack attack;
            bool present;
            if (!qa_combat_last_attack_read(o->app->combat, o->player->actor, &attack, &present, e) ||
                !current(o, e)) return false;
            death_yaw = angles.y;
            uint64_t serial = present && !qa_actor_id_equal(attack.attacker, o->player->actor) &&
                qa_actors_get(qa_session_actors(o->source->session), attack.attacker) ?
                qa_world_body_storage_serial(o->source->world, attack.attacker) : 0;
            if (serial) {
                qa_body_state killer;
                if (!qa_world_body_read(o->source->world, attack.attacker, &killer, e) || !current(o, e) ||
                    qa_world_body_storage_serial(o->source->world, attack.attacker) != serial)
                    return application_fail(e, QA_ERROR_ARGUMENT, "Selected Q3 death camera lost its genuine attacker body");
                death_yaw = atan2((double)killer.origin.y - origin.y, (double)killer.origin.x - origin.x) *
                    (180.0 / 3.14159265358979323846);
            }
            height = -16; angles = qa_v3(-15, angles.y, 40); has_fov = true; has_death_yaw = true;
        }
    }
    if (camera.cutscene) {
        origin = qa_vec_add(camera.origin, qa_v3(camera.view_offset.x, camera.view_offset.y, 0));
        height = camera.view_offset.z; angles = camera.angles; has_fov = true; has_death_yaw = false;
    }
    if (o->primary->kind == APPLICATION_PROVIDER_Q2 && !qa_actor_id_equal(o->ui_actor, o->player->actor)) {
        qa_q2_wire_view source;
        if (!qa_q2_wire_view_read(o->primary->state.q2, o->player->actor, &source, e) ||
            !source.present || !current(o, e)) return false;
        /* Source chase has already placed the spectator's real control body. */
        origin = camera.origin; angles = control.view_angles; height = 0; fov = source.view.fov; has_fov = true;
    }
    if (camera.has_client_view_offset) {
        qa_vec3 replacement = qa_vec_add(camera.origin, qa_v3(camera.view_offset.x, camera.view_offset.y, 0));
        client_delta = qa_v3(replacement.x - origin.x, replacement.y - origin.y,
            replacement.z + camera.view_offset.z - origin.z - height);
        origin = replacement; height = camera.view_offset.z;
    }
    qa_vec3 punch = control.state.kind == QA_MOVEMENT_NETQUAKE ? control.state.data.nq.punch_angles : qa_v3(0, 0, 0);
    if (o->arsenal->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view weapon;
        if (!qa_q1_player_read(o->arsenal->state.q1, o->player->actor, &weapon))
            return application_fail(e, QA_ERROR_NOT_FOUND, "Unified camera lost its real Q1 punch owner");
        punch = weapon.punch_angles;
    }
    kick = qa_vec_add(kick, punch);
    if (!text(j, "{\"origin\":", e) || !vector(j, origin, e) || !text(j, ",\"angles\":", e) ||
        !(has_death_yaw ? text(j, "{\"x\":", e) && number(j, angles.x, e) && text(j, ",\"y\":", e) &&
            number(j, death_yaw, e) && text(j, ",\"z\":", e) && number(j, angles.z, e) && text(j, "}", e) :
            vector(j, angles, e)) || !text(j, ",\"viewHeight\":", e) || !number(j, height, e) ||
        !text(j, ",\"kickAngles\":", e) || !vector(j, kick, e)) return false;
    if (has_fov && (!text(j, ",\"fieldOfView\":", e) || !number(j, fov, e))) return false;
    if (camera.has_client_view_offset && (!text(j, ",\"clientViewOffsetDelta\":", e) || !vector(j, client_delta, e))) return false;
    if (has_blend && (!text(j, ",\"blend\":", e) || !blend(j, rgba, e))) return false;
    if (has_damage && (!text(j, ",\"damageBlend\":", e) || !blend(j, damage_rgba, e))) return false;
    if (foreign_death && !text(j, ",\"foreignCharacterDeath\":true", e)) return false;
    if (control.state.kind == QA_MOVEMENT_NETQUAKE || control.state.kind == QA_MOVEMENT_QUAKEWORLD) {
        bool nq = control.state.kind == QA_MOVEMENT_NETQUAKE;
        bool disabled = camera.cutscene || o->intermission || view_combat.health <= 0 ||
            (nq ? control.state.data.nq.move_type != 3 : control.state.data.qw.spectator != 0);
        if (!text(j, ",\"pitchDrift\":{\"grounded\":", e) || !boolean(j, control.ground.hit != QA_TRACE_HIT_NONE, e) ||
            !text(j, ",\"idealPitch\":", e) || !number(j, nq ? control.state.data.nq.ideal_pitch : 0, e) ||
            !text(j, ",\"disabled\":", e) || !boolean(j, disabled, e) || !text(j, "}", e)) return false;
    }
    return text(j, "}", e) && current(o, e);
}

static bool native_inventory_presentation(application_unified_json *j,player_observation *o,
    const application_native_q2_inventory_presentation *p,qa_error *e)
{
    if(p->kind==APPLICATION_NATIVE_INVENTORY_PRESENTATION_NONE) return true;
    application_provider *owner=NULL;
    for(size_t i=0;i<o->app->provider_count;++i) if(o->app->providers[i]->owner==p->source) { owner=o->app->providers[i]; break; }
    if(!text(j,",\"presentation\":{\"source\":",e)||!provider(j,owner,e)||!text(j,",\"kind\":",e)) return false;
    if(p->kind==APPLICATION_NATIVE_INVENTORY_PRESENTATION_WEAPON||p->kind==APPLICATION_NATIVE_INVENTORY_PRESENTATION_AMMUNITION)
        return string(j,p->kind==APPLICATION_NATIVE_INVENTORY_PRESENTATION_WEAPON?"weapon":"ammunition",e)&&
            text(j,",\"weapon\":",e)&&item(j,o,p->weapon,e)&&text(j,"}",e);
    if(p->kind!=APPLICATION_NATIVE_INVENTORY_PRESENTATION_ITEM)
        return application_fail(e,QA_ERROR_FORMAT,"Native inventory presentation lost its declared kind");
    if(!string(j,"item",e)||!text(j,",\"icon\":",e)) return false;
    if(p->icon_kind==APPLICATION_NATIVE_INVENTORY_ICON_NONE) return text(j,"null}",e);
    if(!p->icon||!owner||!owner->product) return application_fail(e,QA_ERROR_FORMAT,"Native inventory icon lost its retained source metadata");
    if(p->icon_kind==APPLICATION_NATIVE_INVENTORY_ICON_SHADER)
        return text(j,"{\"kind\":\"shader\",\"content\":",e)&&string(j,owner->product->identity,e)&&
            text(j,",\"name\":",e)&&string(j,p->icon,e)&&text(j,"}}",e);
    if(p->icon_kind!=APPLICATION_NATIVE_INVENTORY_ICON_IMAGE&&p->icon_kind!=APPLICATION_NATIVE_INVENTORY_ICON_WAD_PICTURE)
        return application_fail(e,QA_ERROR_FORMAT,"Native inventory icon lost its actual resource kind");
    if(!text(j,"{\"kind\":",e)||!string(j,p->icon_kind==APPLICATION_NATIVE_INVENTORY_ICON_IMAGE?"image":"wad-picture",e)||
        !text(j,",\"resource\":{\"content\":",e)||!string(j,owner->product->identity,e)||
        !text(j,",\"path\":",e)||!string(j,p->icon,e)||!text(j,"}",e)) return false;
    if(p->icon_kind==APPLICATION_NATIVE_INVENTORY_ICON_WAD_PICTURE&&
        (!p->lump||!text(j,",\"lump\":",e)||!string(j,p->lump,e))) return false;
    return text(j,"}}",e);
}

static bool ui_gear(application_unified_json *j, player_observation *o,
    bool comma, size_t ordinal, qa_error *e)
{
    if (!o->has_gear) return true;
    return (!comma || text(j, ",", e)) && text(j, "{\"id\":", e) && item(j, o, o->gear.item, e) &&
        text(j, ",\"label\":", e) && string(j, o->gear.label, e) &&
        text(j, ",\"kind\":\"weapon\",\"sourceOrdinal\":", e) && application_unified_json_natural(j, ordinal, e) &&
        text(j, ",\"owned\":", e) && boolean(j, count(o, o->gear.item) > 0, e) &&
        text(j, ",\"hasAmmo\":true,\"count\":null,\"warningCount\":0}", e);
}

static bool ui_items(application_unified_json *j, player_observation *o, qa_error *e)
{
    /* Original API3/API2023 PlayerUI exposes inventory separately from its
     * public PS. Selected constituents below supply their complete UI roster. */
    if (native_original(o)) return text(j, "[", e) && ui_gear(j, o, false, 0, e) && text(j, "]", e);
    if (!text(j, "[", e)) return false;
    if (qc_arsenal(o)) {
        const qa_application_qc_weapon_ui_binding *active = qc_active(o);
        const char *ammo = qc_ammo_item(o);
        bool first = true;
        for (size_t i = 0; i < o->qc_binding_count; ++i) {
            const qa_application_qc_weapon_ui_binding *w = o->qc_bindings + i;
            if (o->has_gear && w->item == o->gear.item) continue;
            if ((!first && !text(j, ",", e)) || !text(j, "{\"id\":", e) || !item(j, o, w->item, e) ||
                !text(j, ",\"label\":", e) || !string(j, w->label, e) || !text(j, ",\"kind\":\"weapon\",\"sourceOrdinal\":", e) ||
                !number(j, w->impulse, e) || !text(j, ",\"owned\":", e) || !boolean(j, (qc_weapons(o)->items & w->bit) != 0, e) ||
                !text(j, ",\"hasAmmo\":", e) || !boolean(j, w != active || !ammo || o->qc_ammo > 0, e) ||
                !text(j, ",\"count\":", e) || !(w == active && ammo ? number(j, o->qc_ammo, e) : text(j, "null", e)) ||
                !text(j, ",\"warningCount\":0}", e)) return false;
            first = false;
        }
        return ui_gear(j, o, !first, o->qc_binding_count, e) && text(j, "]", e);
    }
    if (catalog_q3(o)) {
        bool first = true;
        for (size_t i = 0; i < o->q3_weapon_count; ++i) {
            const application_q3_catalog_weapon *w = o->q3_weapons + i;
            if (o->has_gear && w->item == o->gear.item) continue;
            double ammo = w->ammo ? count(o, w->ammo) : 0;
            if ((!first && !text(j, ",", e)) || !text(j, "{\"id\":", e) || !item(j, o, w->item, e) ||
                !text(j, ",\"label\":", e) || !string(j, w->label, e) || !text(j, ",\"kind\":\"weapon\",\"sourceOrdinal\":", e) ||
                !number(j, w->weapon, e) || !text(j, ",\"owned\":", e) || !boolean(j, count(o, w->item) > 0, e) ||
                !text(j, ",\"hasAmmo\":", e) || !boolean(j, !w->ammo ||
                    (o->q3_ui_owner == o->primary ? ammo != 0 : ammo > 0), e) || !text(j, ",\"count\":", e) ||
                !(w->ammo ? number(j, ammo, e) : text(j, "null", e)) || !text(j, ",\"warningCount\":0}", e)) return false;
            first = false;
        }
        return ui_gear(j, o, !first, o->q3_weapon_count, e) && text(j, "]", e);
    }
    bool first = true;
    size_t base_count = 0;
    for (size_t i = 0; i < o->definition_count; ++i) {
        const qa_item_definition *d = o->definitions + i;
        if (!d->weapon && !(o->arsenal == o->primary &&
            o->primary->kind == APPLICATION_PROVIDER_Q2 &&
            d->owner == o->primary->owner && (d->actions & QA_ITEM_USE))) continue;
        ++base_count;
        if (o->has_gear && d->item == o->gear.item) continue;
        application_provider *owner = NULL;
        for (size_t n = 0; n < o->app->provider_count; ++n)
            if (o->app->providers[n]->owner == d->owner) {
                if (owner) return application_fail(e, QA_ERROR_FORMAT, "Player item has ambiguous published source owners");
                owner = o->app->providers[n];
            }
        double owned = count(o, d->item), ammo = d->ammo ? count(o, d->ammo) : owned;
        double required = 1, warning = 0;
        size_t ordinal = i;
        const char *label = d->label;
        bool has_ammo = d->ammo ? ammo >= required : !d->weapon ? owned >= required : true;
        bool finite = d->ammo || !d->weapon;
        if (owner && owner->kind == APPLICATION_PROVIDER_Q1 && d->weapon) {
            qa_q1_weapon_ui_definition source; bool found;
            if (!qa_q1_game_weapon_ui_definition_read(owner->state.q1, o->ui_actor, d->item, &source, &found, e) || !current(o, e)) return false;
            if (found) {
                required = source.quantity; has_ammo = !source.ammo || ammo >= required;
                ordinal = source.ordinal;
                if (owner == o->primary) {
                    const char *key = qa_q1_weapon_identity(source.weapon);
                    label = source.weapon == QA_Q1_CTF_GRAPPLE ? "ctf:grapple" :
                        key && !strncmp(key, "q1:weapon/", 10) ? key + 10 : d->label;
                } else {
                    ++ordinal;
                    bool available, admitted;
                    if (!qa_q1_game_weapon_item_available(owner->state.q1, o->ui_actor, d->item,
                        &available, &admitted, e) || !admitted || !current(o, e)) return false;
                    has_ammo = available;
                }
            }
        } else if (owner && owner->kind == APPLICATION_PROVIDER_Q2) {
            for (size_t n = 0; n < qa_q2_item_count(owner->state.q2); ++n) {
                const qa_q2_item_definition *source = qa_q2_item_at(owner->state.q2, n);
                if (!source || source->item != d->item) continue;
                ordinal = n; label = source->name;
                if (d->weapon) {
                    const qa_q2_weapon_definition *weapon = qa_q2_weapon_definition_at(owner->state.q2, source->weapon);
                    if (!weapon) return application_fail(e, QA_ERROR_FORMAT, "Q2 UI weapon lost its actual quantity definition");
                    required = weapon->quantity; warning = weapon->warning;
                    if (owner != o->primary) {
                        if (!qa_q2_weapon_definition_ordinal(owner->state.q2, source->weapon, &ordinal))
                            return application_fail(e, QA_ERROR_FORMAT, "Selected Q2 UI weapon lost its registered source order");
                        ++ordinal;
                        const char *display = qa_q2_weapon_display_name(source->weapon);
                        if (display) label = display;
                    }
                } else required = 1;
                has_ammo = finite ? ammo >= required : true;
                break;
            }
        } else if (owner && owner->product && owner->product->family == QA_GAME_Q3 && d->weapon) {
            bool original = owner != NULL && owner == o->primary && owner->kind != APPLICATION_PROVIDER_Q3;
            has_ammo = !d->ammo || (original ? ammo != 0 : ammo > 0);
            for (unsigned w = 1; w < QA_Q3_WEAPON_COUNT; ++w) {
                const char *name = qa_q3_weapon_identity_name((qa_q3_weapon)w);
                const char *key = identity(o, d->item);
                if (name && key && !strncmp(key, "q3:weapon/", 10) && !strcmp(key + 10, name)) {
                    ordinal = w; if (!original && owner == o->primary) label = key + 10; break;
                }
            }
            if (owner->kind != APPLICATION_PROVIDER_Q3 && q3g_engine(owner)) {
                struct application_q3_guest *engine = q3g_engine(owner);
                const application_q3_catalog_weapon *weapons;
                size_t length;
                if (!engine || !engine->game || !engine->game->catalog ||
                    !application_q3_catalog_weapons(engine->game->catalog, &weapons, &length, e) || !current(o, e)) return false;
                bool found = false;
                for (size_t n = 0; n < length; ++n)
                    if (weapons[n].item == d->item) { ordinal = (size_t)weapons[n].weapon; label = weapons[n].label; found = true; break; }
                if (!found) return application_fail(e, QA_ERROR_NOT_FOUND, "Q3 UI weapon lacks its actual source catalog row");
            }
        }
        if ((!first && !text(j, ",", e)) || !text(j, "{\"id\":", e) || !item(j, o, d->item, e) ||
            !text(j, ",\"label\":", e) || !string(j, label, e) || !text(j, ",\"kind\":", e) ||
            !string(j, d->weapon ? "weapon" : "powerup", e) || !text(j, ",\"sourceOrdinal\":", e) ||
            !application_unified_json_natural(j, ordinal, e) || !text(j, ",\"owned\":", e) || !boolean(j, owned > 0, e) ||
            !text(j, ",\"hasAmmo\":", e) || !boolean(j, has_ammo, e) || !text(j, ",\"count\":", e) ||
            !(finite ? number(j, ammo, e) : text(j, "null", e)) || !text(j, ",\"warningCount\":", e) ||
            !number(j, warning, e) || !text(j, "}", e)) return false;
        first = false;
    }
    return ui_gear(j, o, !first, base_count, e) && text(j, "]", e);
}

static bool weapon_status(application_unified_json *j, player_observation *o, qa_error *e)
{
    if (o->has_gear && o->gear.active) {
        application_provider *owner = NULL;
        for (size_t i = 0; i < o->app->provider_count; ++i)
            if (o->app->providers[i]->owner == o->gear.source.owner) { owner = o->app->providers[i]; break; }
        return text(j, "{\"source\":", e) && provider(j, owner, e) && text(j, ",\"item\":", e) &&
            item(j, o, o->gear.item, e) && text(j, ",\"label\":", e) && string(j, o->gear.label, e) &&
            text(j, ",\"ammo\":{\"kind\":\"unmetered\"}}", e);
    }
    if (catalog_q3(o)) {
        const application_q3_catalog_weapon *w = q3_active(o);
        if (!w) return text(j, "null", e);
        double ammo = w->ammo ? count(o, w->ammo) : -1;
        if (!text(j, "{\"source\":", e) || !provider(j, o->q3_ui_owner, e) || !text(j, ",\"item\":", e) ||
            !item(j, o, w->item, e) || !text(j, ",\"label\":", e) || !string(j, w->label, e) || !text(j, ",\"ammo\":", e)) return false;
        if (!w->ammo || (o->q3_ui_owner == o->primary && ammo == -1))
            return text(j, "{\"kind\":\"unmetered\"}}", e);
        return text(j, "{\"kind\":\"finite\",\"item\":", e) && item(j, o, w->ammo, e) && text(j, ",\"count\":", e) &&
            number(j, ammo, e) && text(j, ",\"hasAmmoToStart\":", e) && boolean(j, ammo > 0, e) && text(j, ",\"low\":false}}", e);
    }
    if (native_original(o)) {
        const qa_q2_weapon_definition *w = o->native_weapon;
        if (!w) return text(j, "null", e);
        if (!text(j, "{\"source\":", e) || !provider(j, o->primary, e) ||
            !text(j, ",\"item\":", e) || !string(j, w->item, e) ||
            !text(j, ",\"label\":", e) || !string(j, qa_q2_weapon_display_name(w->weapon), e) ||
            !text(j, ",\"ammo\":", e)) return false;
        if (!w->ammo) return text(j, "{\"kind\":\"unmetered\"}}", e);
        return text(j, "{\"kind\":\"finite\",\"item\":", e) && string(j, w->ammo, e) &&
            text(j, ",\"count\":", e) && number(j, o->q2.stats[3], e) &&
            text(j, ",\"hasAmmoToStart\":", e) && boolean(j, o->q2.stats[3] >= w->quantity, e) &&
            text(j, ",\"low\":", e) && boolean(j, o->q2.stats[3] <= w->warning, e) && text(j, "}}", e);
    }
    if (qc_arsenal(o))
        return text(j, "null", e);
    qa_application_equipment_view *v = &o->equipment;
    if (!v->has_weapon_status || !v->item) return text(j, "null", e);
    application_provider *owner = o->arsenal;
    if (v->equipment_slot) {
        owner = NULL;
        for (size_t i = 0; i < o->app->provider_count; ++i)
            if (o->app->providers[i]->owner == v->provider) { owner = o->app->providers[i]; break; }
    }
    if (!text(j, "{\"source\":", e) || !provider(j, owner, e) ||
        !text(j, ",\"item\":", e) || !item(j, o, v->item, e) || !text(j, ",\"label\":", e) ||
        !string(j, o->equipment_label, e) || !text(j, ",\"ammo\":", e)) return false;
    if (!v->finite_ammo) {
        if (!text(j, "{\"kind\":\"unmetered\"}", e)) return false;
    } else if (!text(j, "{\"kind\":\"finite\",\"item\":", e) || !item(j, o, v->ammo, e) ||
        !text(j, ",\"count\":", e) || !number(j, v->ammo_count, e) ||
        !text(j, ",\"hasAmmoToStart\":", e) || !boolean(j, v->has_ammo_to_start, e) ||
        !text(j, ",\"low\":", e) || !boolean(j, v->low_ammo, e) || !text(j, "}", e)) return false;
    return text(j, "}", e);
}

static const char *original_q3_warning(player_observation *o)
{
    if (!o->q3_standard) return "none";
    uint32_t total = 0;
    for (size_t i = 0; i < o->q3_weapon_count; ++i) {
        const application_q3_catalog_weapon *w = o->q3_weapons + i;
        if (w->weapon < QA_Q3_W_MACHINEGUN ||
            (o->q3_product == QA_Q3_ARENA && w->weapon > QA_Q3_W_GRAPPLE) || count(o, w->item) <= 0) continue;
        double value = w->ammo ? count(o, w->ammo) : -1;
        /* Math.imul lowers the authentic counter to its unsigned word before
         * multiplication; the aggregate wraps after each weapon. */
        double word = isfinite(value) ? fmod(trunc(value), 4294967296.0) : 0;
        if (word < 0) word += 4294967296.0;
        bool slow = w->weapon == QA_Q3_W_ROCKET || w->weapon == QA_Q3_W_GRENADE ||
            w->weapon == QA_Q3_W_RAIL || w->weapon == QA_Q3_W_SHOTGUN ||
            (o->q3_product == QA_Q3_TEAM_ARENA && w->weapon == QA_Q3_W_PROX);
        total += (uint32_t)word * (slow ? 1000u : 200u);
        if (total <= INT32_MAX && total >= 5000) return "none";
    }
    return total == 0 ? "empty" : "low";
}

static const char *arsenal_warning(player_observation *o)
{
    if (catalog_q3(o)) return original_q3_warning(o);
    if (o->arsenal->kind == APPLICATION_PROVIDER_Q3) {
        uint32_t total = 0;
        for (qa_q3_weapon w = QA_Q3_W_MACHINEGUN; w < QA_Q3_WEAPON_COUNT; ++w) {
            if (o->q3_product == QA_Q3_ARENA && w > QA_Q3_W_GRAPPLE) continue;
            qa_item_id weapon = qa_q3_weapon_item(o->arsenal->state.q3, w, false);
            if (count(o, weapon) <= 0) continue;
            qa_item_id ammo = qa_q3_weapon_item(o->arsenal->state.q3, w, true);
            double value = ammo ? count(o, ammo) : -1;
            double word = isfinite(value) ? fmod(trunc(value), 4294967296.0) : 0;
            if (word < 0) word += 4294967296.0;
            bool slow = w == QA_Q3_W_ROCKET || w == QA_Q3_W_GRENADE || w == QA_Q3_W_RAIL ||
                w == QA_Q3_W_SHOTGUN || (o->q3_product == QA_Q3_TEAM_ARENA && w == QA_Q3_W_PROX);
            total += (uint32_t)word * (slow ? 1000u : 200u);
            if (total <= INT32_MAX && total >= 5000) return "none";
        }
        return total == 0 ? "empty" : "low";
    }
    return "none";
}

static bool ui(application_unified_json *j, player_observation *o, qa_error *e)
{
    if (!text(j, "{\"health\":", e) || !number(j, o->has_q2 ? (double)o->q2.stats[1] :
        o->has_q3 && o->primary->kind != APPLICATION_PROVIDER_Q3 ? (double)o->q3.stats[0] :
        (double)o->combat.health, e) ||
        !text(j, ",\"armor\":", e) || !armor(j, o, e) || !text(j, ",\"inventory\":", e) || !inventory(j, o, e) ||
        !text(j, ",\"powerups\":", e) || !timers(j, o, e) || !text(j, ",\"items\":", e) || !ui_items(j, o, e) ||
        !text(j, ",\"weaponStatus\":", e) || !weapon_status(j, o, e) || !text(j, ",\"arsenalWarning\":", e)) return false;
    bool qc = qc_arsenal(o);
    if (o->has_gear && o->gear.active) {
        if (!string(j, arsenal_warning(o), e) || !text(j, ",\"activeWeapon\":", e) ||
            !item(j, o, o->gear.item, e) || !text(j, ",\"ammo\":null", e)) return false;
    } else if (catalog_q3(o)) {
        const application_q3_catalog_weapon *w = q3_active(o);
        if (!string(j, original_q3_warning(o), e) || !text(j, ",\"activeWeapon\":", e) ||
            !item(j, o, o->q3_active, e) || !text(j, ",\"ammo\":", e)) return false;
        if (w && w->ammo) {
            if (!text(j, "{\"item\":", e) || !item(j, o, w->ammo, e) || !text(j, ",\"count\":", e) ||
                !number(j, count(o, w->ammo), e) || !text(j, "}", e)) return false;
        } else if (!text(j, "null", e)) return false;
    } else if (native_original(o)) {
        const qa_q2_weapon_definition *w = o->native_weapon;
        if (!text(j, "\"none\",\"activeWeapon\":", e) ||
            !(w ? string(j, w->item, e) : text(j, "null", e)) || !text(j, ",\"ammo\":", e)) return false;
        if (w && w->ammo) {
            if (!text(j, "{\"item\":", e) || !string(j, w->ammo, e) || !text(j, ",\"count\":", e) ||
                !number(j, o->q2.stats[3], e) || !text(j, "}", e)) return false;
        } else if (!text(j, "null", e)) return false;
    } else if (qc) {
        const qa_application_qc_weapon_ui_binding *active = qc_active(o);
        const char *ammo = qc_ammo_item(o);
        if (!string(j, "none", e) || !text(j, ",\"activeWeapon\":", e) || !item(j, o, active ? active->item : 0, e) ||
            !text(j, ",\"ammo\":", e)) return false;
        if (ammo) {
            if (!text(j, "{\"item\":", e) || !string(j, ammo, e) || !text(j, ",\"count\":", e) ||
                !number(j, o->qc_ammo, e) || !text(j, "}", e)) return false;
        } else if (!text(j, "null", e)) return false;
    } else {
        static const char *const warnings[] = {"none", "low", "empty"};
        if ((unsigned)o->equipment.warning >= sizeof(warnings) / sizeof(warnings[0]))
            return application_fail(e, QA_ERROR_FORMAT, "Unified arsenal lost its genuine warning kind");
        if (!string(j, warnings[o->equipment.warning], e) || !text(j, ",\"activeWeapon\":", e) ||
            !item(j, o, o->equipment.item, e) || !text(j, ",\"ammo\":", e)) return false;
        if (o->equipment.ammo) {
            if (!text(j, "{\"item\":", e) || !item(j, o, o->equipment.ammo, e) ||
                !text(j, ",\"count\":", e) || !number(j, o->equipment.ammo_count, e) || !text(j, "}", e)) return false;
        } else if (!text(j, "null", e)) return false;
    }
    if (o->arsenal != o->primary && !text(j, ",\"selectedArsenal\":true", e)) return false;
    if (o->primary->kind == APPLICATION_PROVIDER_NATIVE && o->primary->state.native.q2_engine) {
        if(o->primary->state.native.q2_engine->inventory_scanner) {
            application_native_q2_inventory_readout mixed={0};
            if(!application_native_q2_inventory_mixed_read(o->primary,o->ui_actor,&mixed,e)||!current(o,e)) {
                application_native_q2_inventory_readout_free(&mixed); return false;
            }
            bool ok=true;
            if(mixed.present) {
                ok=text(j,",\"nativeInventory\":{\"items\":[",e);
                for(size_t i=0;ok&&i<mixed.count;++i) ok=(!i||text(j,",",e))&&text(j,"{\"item\":",e)&&item(j,o,mixed.rows[i].item,e)&&
                    text(j,",\"label\":",e)&&string(j,mixed.rows[i].label,e)&&text(j,",\"count\":",e)&&number(j,mixed.rows[i].count,e)&&text(j,"}",e);
                if(ok) ok=text(j,"],\"selected\":",e)&&item(j,o,mixed.selected,e)&&
                    native_inventory_presentation(j,o,&mixed.selected_presentation,e)&&text(j,"}",e);
            }
            application_native_q2_inventory_readout_free(&mixed);
            if(!ok) return false;
        } else {
        application_native_q2_ui_inventory native = {0};
        if (!application_native_q2_inventory_ui_read(o->primary, o->ui_actor, &native, e) || !current(o, e)) {
            application_native_q2_inventory_ui_free(&native); return false;
        }
        bool ok = text(j, ",\"nativeInventory\":{\"items\":[", e);
        for (size_t i = 0; ok && i < native.count; ++i)
            ok = (!i || text(j, ",", e)) && text(j, "{\"item\":", e) && item(j, o, native.items[i].item, e) &&
                text(j, ",\"label\":", e) && string(j, native.items[i].label, e) &&
                text(j, ",\"count\":", e) && number(j, native.items[i].count, e) && text(j, "}", e);
        if (ok) ok = text(j, "],\"selected\":", e) && item(j, o, native.selected, e) && text(j, "}", e);
        application_native_q2_inventory_ui_free(&native);
        if (!ok) return false;
        }
    }
    return text(j, "}", e) && current(o, e);
}

bool application_unified_player_values(qa_application *app, const application_unified_source *source,
    qa_net_client_id client, const qa_unified_session_player *player,
    const application_unified_player_external *external, qa_unified_document **out, qa_error *error)
{
    if (!app || !source || !player || !out || !application_unified_source_current(app, source) ||
        !application_unified_player_current(app, client, player))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified player requires its actual returned Source recipient");
    player_observation o = {.app = app, .source = source, .player = player, .external = external, .client = client,
        .ui_actor = player->actor, .actors_revision = qa_actors_revision(qa_session_actors(source->session))};
    o.primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!o.primary || o.primary->owner != source->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified player lost its physical Source provider");
    if (o.primary->kind == APPLICATION_PROVIDER_QC &&
        (!external || !external->current || !external->camera ||
         external->camera->source.provider != source->owner ||
         external->camera->source_slot != player->source_slot ||
         !qa_actor_id_equal(external->camera->recipient, player->actor) ||
         !qa_application_qc_message_source_current(app, &external->camera->source)))
        return application_fail(error, QA_ERROR_NOT_FOUND, "QC player requires its genuine recipient-message camera and declared UI receipts");
    if (o.primary->kind == APPLICATION_PROVIDER_QC) o.intermission = external->camera->intermission != 0;
    if (o.primary->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_player_info info;
        if (!qa_q2_player_read(o.primary->state.q2, player->actor, &info) || !current(&o, error))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Q2 UI lost its actual source player");
        if (info.spectator && info.chase_target.registry) {
            qa_q2_player_info viewed;
            bool present = qa_q2_player_read(o.primary->state.q2, info.chase_target, &viewed);
            if (!current(&o, error)) return false;
            if (present && viewed.connected && !viewed.spectator)
                o.ui_actor = info.chase_target;
        }
    }
    o.arsenal = application_provider_for(app, o.ui_actor, QA_ROLE_ARSENAL, "");
    if (!o.arsenal || !o.arsenal->constructed || !o.arsenal->attached || o.arsenal->close_pending || !o.arsenal->product)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Unified UI has no actual selected arsenal owner");
    application_unified_json json = {0};
    bool ok = current(&o, error) && qa_combat_read(app->combat, o.ui_actor, &o.combat, error) &&
        current(&o, error) && copy_inventory(&o, error);
    if (ok && o.primary->kind == APPLICATION_PROVIDER_QC) ok = qc_read(&o, error);
    if (ok && o.arsenal->kind == APPLICATION_PROVIDER_QC && o.arsenal != o.primary) {
        ok = qa_application_qc_selected_player_ui_read(app, o.ui_actor, QA_ROLE_ARSENAL,
            &o.selected_qc, error);
        if (ok) {
            o.has_selected_qc = true; o.qc_ammo = o.selected_qc.current_ammo;
            ok = current(&o, error) && qc_bindings_read(&o, &o.selected_qc, error);
        }
    }
    if (ok && source->family == QA_GAME_Q3) ok = q3_read(&o, error);
    if (ok && o.has_q3) o.intermission = o.q3.pmType == 5 || o.q3.pmType == 6;
    if (ok && o.primary->kind == APPLICATION_PROVIDER_Q1) {
        double now, started;
        ok = qa_q1_bot_clock_read(o.primary->state.q1, &now, &o.intermission, &started, error) && current(&o, error);
    }
    if (ok && o.primary->kind == APPLICATION_PROVIDER_Q2)
        o.intermission = qa_q2_players_in_intermission(o.primary->state.q2);
    if (ok && o.primary->kind == APPLICATION_PROVIDER_NATIVE && o.primary->state.native.q2_engine) {
        ok = qa_native_host_q2_wire_player(o.primary->state.native.host, player->source_slot,
            player->actor, &o.q2, error) && current(&o, error);
        o.has_q2 = ok;
        if (ok && o.q2.gunindex) {
            struct application_native_q2 *engine = o.primary->state.native.q2_engine;
            uint32_t base = engine->resource_base[QA_NATIVE_HOST_MODEL];
            if (o.q2.gunindex >= engine->resource_limit[QA_NATIVE_HOST_MODEL] ||
                base > UINT32_MAX - (uint32_t)o.q2.gunindex || base + (uint32_t)o.q2.gunindex >= engine->configstring_count)
                ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 UI gun model leaves its real source namespace");
            else o.native_weapon = qa_q2_base_weapon_view_model(engine->configstrings[base + (uint32_t)o.q2.gunindex]);
        }
    }
    bool qc = qc_arsenal(&o);
    if (ok && app->equipment) {
        ok = qa_equipment_weapon_view_read(app->equipment, o.ui_actor, &o.gear, &o.has_gear, error) && current(&o, error);
    }
    if (ok && o.arsenal->kind == APPLICATION_PROVIDER_Q3)
        ok = qa_application_equipment_q3_product_read(app, o.arsenal->owner, &o.q3_product, error) && current(&o, error);
    bool gear = o.has_gear && o.gear.active;
    struct application_q3_guest *selected_q3 = o.arsenal->kind != APPLICATION_PROVIDER_Q3 ? q3g_engine(o.arsenal) : NULL;
    if (ok && selected_q3 && o.arsenal != o.primary) {
        uint32_t physical_slot;
        qa_q3_player ps;
        if (!selected_q3->game || !selected_q3->game->host ||
            !application_q3_guest_actor_client(o.arsenal, o.ui_actor, &physical_slot))
            ok = application_fail(error, QA_ERROR_NOT_FOUND, "Selected original Q3 UI lost its real source client");
        else ok = qa_q3_host_source_player(selected_q3->game->host, physical_slot, &ps, error) &&
            current(&o, error) && q3_catalog_read(&o, o.arsenal, selected_q3, &ps, error);
    }
    bool source_q3 = catalog_q3(&o) && !gear;
    if (ok && !qc && !source_q3 && !gear) ok = qa_application_equipment_read(app, o.ui_actor, &o.equipment, error) &&
        current(&o, error) && qa_application_equipment_current(app, &o.equipment);
    if (ok && !qc && !source_q3 && !gear) {
        o.has_equipment = true;
        if (o.equipment.label) {
            size_t length = strlen(o.equipment.label);
            o.equipment_label = malloc(length + 1);
            if (!o.equipment_label) ok = application_fail(error, QA_ERROR_MEMORY, "Copying genuine weapon status label");
            else memcpy(o.equipment_label, o.equipment.label, length + 1);
        }
    }
    if (ok) ok = text(&json, "{\"view\":", error) && view(&json, &o, error) && text(&json, ",\"ui\":", error) &&
        ui(&json, &o, error) && text(&json, "}", error) && current(&o, error);
    qa_unified_document *document = NULL;
    if (ok) ok = qa_unified_document_create(QA_UNIFIED_CHECKPOINT,
        (qa_bytes){json.bytes.data, json.bytes.size}, &document, error) && current(&o, error);
    if (ok) *out = document;
    else qa_unified_document_destroy(document);
    application_unified_json_dispose(&json);
    for (size_t i = 0; i < o.definition_count; ++i) free((char *)o.definitions[i].label);
    free(o.definitions); free(o.inventory); free(o.equipment_label);
    q3_catalog_clear(&o);
    qc_bindings_clear(&o);
    return ok;
}

static bool selection_basis(qa_application *app, qa_actor_id actor,
    application_provider **primary, application_provider **arsenal, qa_error *e)
{
    if (!app || !app->session || !app->world || !app->players || app->destroy_requested ||
        app->state != QA_APPLICATION_RUNNING || !app->map_view_ready || app->routing_snapshot ||
        qa_session_faulted(app->session) ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING) ||
        !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(e, QA_ERROR_ARGUMENT, "Selected UI requires its genuine live Source player");
    size_t found = 0;
    for (size_t i = 0; i < app->players->count; ++i) {
        const application_player_record *row = app->players->records + i;
        if (qa_actor_id_equal(row->actor, actor) && !row->retiring && !row->deferred &&
            !row->source_begin_pending) ++found;
    }
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    application_provider *selected = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    if (found != 1 || !source || source != app->players->map_provider || !selected ||
        !source->constructed || !source->attached || source->close_pending ||
        !selected->constructed || !selected->attached || selected->close_pending ||
        !source->launch || !selected->launch || !source->product || !selected->product)
        return application_fail(e, QA_ERROR_ARGUMENT, "Selected UI lost its actual physical or arsenal owner");
    *primary = source; *arsenal = selected; return true;
}

static qa_item_id selection_identity(qa_application *app, const char *name)
{
    return name ? qa_strings_find(qa_session_strings(app->session),
        (qa_bytes){(const uint8_t *)name, strlen(name)}) : 0;
}

bool application_unified_player_selection_read(qa_application *app, qa_actor_id actor,
    application_unified_player_selection *out, qa_error *e)
{
    application_provider *primary, *arsenal;
    if (!out) return application_fail(e, QA_ERROR_ARGUMENT, "Selected UI requires its typed output");
    if (!selection_basis(app, actor, &primary, &arsenal, e)) return false;
    application_unified_player_selection value = {.actor = actor,
        .primary = primary->owner, .arsenal = arsenal->owner, .visible_source = arsenal->owner,
        .publication = app->publication_generation, .map_revision = app->map_revision,
        .frame_revision = app->frame_revision,
        .actors_revision = qa_actors_revision(qa_session_actors(app->session))};
    qa_equipment_weapon_view gear; bool found = false;
    if (app->equipment && !qa_equipment_weapon_view_read(app->equipment, actor, &gear, &found, e)) return false;
    if (found && !application_equipment_runtime_owner_current(app->equipment_runtime, gear.source.owner))
        return application_fail(e, QA_ERROR_ARGUMENT, "Selected UI gear lost its actual retained source");
    if (found && gear.active) {
        value.item = gear.item; value.visible_source = gear.source.owner;
    } else if (arsenal->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view player;
        if (!qa_q1_player_read(arsenal->state.q1, actor, &player))
            return application_fail(e, QA_ERROR_NOT_FOUND, "Selected Q1 UI lost its actual weapon state");
        value.item = qa_q1_weapon_item(arsenal->state.q1, player.weapon);
        qa_q1_weapon_ui_definition weapon; bool admitted;
        if (!value.item || !qa_q1_game_weapon_ui_definition_read(arsenal->state.q1, actor, value.item,
            &weapon, &admitted, e) || !admitted)
            return application_fail(e, QA_ERROR_NOT_FOUND, "Selected Q1 UI lost its admitted weapon definition");
        value.ammo = weapon.ammo;
    } else if (arsenal->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_state player;
        if (!qa_q2_weapon_read(arsenal->state.q2, actor, &player, e)) return false;
        const qa_q2_weapon_definition *weapon = qa_q2_weapon_definition_at(arsenal->state.q2, player.weapon);
        if (player.weapon != QA_Q2_WEAPON_NONE) {
            if (!weapon) return application_fail(e, QA_ERROR_NOT_FOUND, "Selected Q2 UI lost its registered weapon definition");
            value.item = selection_identity(app, weapon->item);
            value.ammo = selection_identity(app, weapon->ammo);
            if (!value.item || (weapon->ammo && !value.ammo))
                return application_fail(e, QA_ERROR_NOT_FOUND, "Selected Q2 UI lost its registered item namespace");
        }
    } else if (arsenal->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state player;
        if (!qa_q3_player_read(arsenal->state.q3, actor, &player))
            return application_fail(e, QA_ERROR_NOT_FOUND, "Selected Q3 UI lost its actual weapon state");
        value.item = qa_q3_weapon_item(arsenal->state.q3, player.weapon, false);
        value.ammo = qa_q3_weapon_item(arsenal->state.q3, player.weapon, true);
        if (player.weapon != QA_Q3_W_NONE && !value.item)
            return application_fail(e, QA_ERROR_NOT_FOUND, "Selected Q3 UI lost its registered weapon identity");
    } else if (arsenal->kind == APPLICATION_PROVIDER_QC) {
        qa_application_qc_player_ui ui;
        if (!qa_application_qc_selected_player_ui_read(app, actor, QA_ROLE_ARSENAL, &ui, e)) return false;
        for (size_t i = 0; i < ui.binding_count; ++i) {
            qa_application_qc_weapon_ui_binding binding;
            if (!qa_application_qc_message_player_ui_binding(app, &ui, i, &binding, e)) return false;
            if (binding.bit == ui.weapon) { value.item = binding.item; break; }
        }
        static const uint32_t bits[] = {256, 512, 1024, 2048};
        static const char *const ammo[] = {"q1:ammo/shells", "q1:ammo/nails", "q1:ammo/rockets", "q1:ammo/cells"};
        for (size_t i = 0; i < sizeof(bits) / sizeof(bits[0]); ++i) if (ui.items & bits[i]) {
            value.ammo = selection_identity(app, ammo[i]);
            if (!value.ammo) return application_fail(e, QA_ERROR_NOT_FOUND, "QC UI ammunition was not admitted");
            break;
        }
        if (!qa_application_qc_message_player_ui_current(app, &ui))
            return application_fail(e, QA_ERROR_ARGUMENT, "QC selected UI changed its actual source client");
    } else if (arsenal->kind == APPLICATION_PROVIDER_NATIVE && arsenal->state.native.q2_engine) {
        qa_application_equipment_view weapon;
        if (!qa_application_equipment_read(app, actor, &weapon, e) ||
            !qa_application_equipment_current(app, &weapon)) return false;
        value.item = weapon.item; value.ammo = weapon.ammo;
    } else {
        struct application_q3_guest *engine = q3g_engine(arsenal);
        uint32_t slot; qa_q3_player player;
        const application_q3_catalog_weapon *weapons; size_t count;
        if (!engine || !engine->game || !engine->game->host || !engine->game->catalog ||
            !application_q3_guest_actor_client(arsenal, actor, &slot))
            return application_fail(e, QA_ERROR_NOT_FOUND, "Original Q3 selected UI lacks its genuine source catalogue");
        if (!qa_q3_host_source_player(engine->game->host, slot, &player, e) ||
            !application_q3_catalog_weapons(engine->game->catalog, &weapons, &count, e)) return false;
        if (engine->game->weapons && !application_q3_weapons_active(engine->game->weapons, actor, &value.item, e)) return false;
        bool matched = false;
        for (size_t i = 0; i < count; ++i)
            if (engine->game->weapons ? weapons[i].item == value.item : weapons[i].weapon == player.weapon) {
                value.item = weapons[i].item; value.ammo = weapons[i].ammo; matched = true; break;
            }
        if (!matched && (engine->game->weapons ? value.item != 0 : player.weapon != 0))
            return application_fail(e, QA_ERROR_FORMAT, "Original Q3 selected weapon leaves its actual source catalogue");
    }
    application_provider *actual_primary, *actual_arsenal;
    if (!selection_basis(app, actor, &actual_primary, &actual_arsenal, e) ||
        actual_primary != primary || actual_arsenal != arsenal ||
        app->publication_generation != value.publication || app->map_revision != value.map_revision ||
        app->frame_revision != value.frame_revision ||
        qa_actors_revision(qa_session_actors(app->session)) != value.actors_revision ||
        (found && (!qa_equipment_weapon_view_current(app->equipment, &gear) ||
            !application_equipment_runtime_owner_current(app->equipment_runtime, gear.source.owner))))
        return application_fail(e, QA_ERROR_ARGUMENT, "Selected UI changed its actual actor or source during observation");
    *out = value; return true;
}

bool application_unified_player_selection_current(qa_application *app,
    const application_unified_player_selection *view)
{
    application_unified_player_selection actual;
    return view && application_unified_player_selection_read(app, view->actor, &actual, NULL) &&
        actual.item == view->item && actual.ammo == view->ammo && actual.primary == view->primary &&
        actual.arsenal == view->arsenal && actual.visible_source == view->visible_source &&
        actual.publication == view->publication && actual.map_revision == view->map_revision &&
        actual.frame_revision == view->frame_revision && actual.actors_revision == view->actors_revision;
}
