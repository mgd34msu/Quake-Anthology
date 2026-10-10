#include "unified_player.h"
#include "unified_frame_private.h"
#include "internal.h"
#include "control_frame.h"
#include "guest_q3_private.h"
#include "guest_q3_catalog.h"
#include "guest_q3_weapons.h"
#include "guest_q3_components.h"
#include "guest_projection_private.h"
#include "guest_native_q2_private.h"
#include "native_q2_inventory_scanner.h"
#include "equipment_runtime.h"
#include "map_players_private.h"
#include "qa/application_equipment.h"
#include "qa/application_network.h"
#include "qa/application_native_q3_presentation.h"
#include "qa/game_q1_inventory.h"
#include "qa/game_q1_ui.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q2_wire.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include "qa/native_host_q2_wire.h"
#include "qa/text.h"

#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct player_observation {
    qa_application *app;
    qa_unified_frame_lease *lease;
    const qa_inventory_entry *recipient_inventory;
    size_t recipient_inventory_count;
    qa_inventory_entry *recipient_ui_inventory;
    const application_unified_source *source;
    const qa_unified_session_player *player;
    const application_unified_player_external *external;
    qa_net_client_id client;
    application_provider *primary, *arsenal, *q3_ui_owner;
    qa_actor_id ui_actor;
    uint64_t actors_revision;
    qa_combat_state combat;
    const qa_inventory_entry *inventory;
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
static bool item(char **out, player_observation *o, qa_item_id id, qa_error *e)
{ return application_unified_frame_string(o->lease, out, id ? identity(o, id) : NULL, e); }
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

static void qc_bindings_clear(player_observation *o)
{
    o->qc_bindings = NULL; o->qc_binding_count = 0;
}
static bool qc_bindings_read(player_observation *o, const qa_application_qc_player_ui *view, qa_error *e)
{
    qc_bindings_clear(o);
    if (view->binding_count > SIZE_MAX / sizeof(*o->qc_bindings))
        return application_fail(e, QA_ERROR_MEMORY, "QC UI bindings exceed their host extent");
    o->qc_bindings = view->binding_count ? application_unified_frame_alloc(o->lease, view->binding_count, sizeof(*o->qc_bindings), e) : NULL;
    if (view->binding_count && !o->qc_bindings)
        return application_fail(e, QA_ERROR_MEMORY, "Retaining actual QC UI declarations");
    for (size_t i = 0; i < view->binding_count; ++i) {
        qa_application_qc_weapon_ui_binding binding;
        if (!qa_application_qc_message_player_ui_binding(o->app, view, i, &binding, e) || !current(o, e)) return false;
        if (!binding.item || !binding.label)
            return application_fail(e, QA_ERROR_FORMAT, "QC UI binding lacks its genuine item or label");
        char *label = NULL;
        if (!application_unified_frame_string(o->lease, &label, binding.label, e)) return false;
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
    o->q3_weapons = NULL; o->q3_weapon_count = 0;
    o->q3_ui_owner = NULL; o->q3_active = 0;
}

static bool q3_catalog_read(player_observation *o, application_provider *p,
    struct application_q3_guest *engine, const qa_q3_player *ps, qa_error *e)
{
    const application_q3_catalog_weapon *weapons;
    size_t length;
    if (!engine->game->catalog)
        return application_fail(e, QA_ERROR_NOT_FOUND, "Original Q3 UI lacks its real complete source catalog");
    if (!application_q3_catalog_role_current(engine->game->catalog,engine->game))
        return application_fail(e, QA_ERROR_ARGUMENT, "Original Q3 UI catalog left its actual source role");
    if (!application_q3_catalog_weapons(engine->game->catalog, &weapons, &length, e) || !current(o, e)) return false;
    q3_catalog_clear(o);
    if (length > SIZE_MAX / sizeof(*o->q3_weapons))
        return application_fail(e, QA_ERROR_MEMORY, "Original Q3 catalog exceeds its host extent");
    o->q3_weapons = length ? application_unified_frame_alloc(o->lease, length, sizeof(*o->q3_weapons), e) : NULL;
    if (length && !o->q3_weapons) return application_fail(e, QA_ERROR_MEMORY, "Retaining actual Q3 UI catalog");
    for (size_t i = 0; i < length; ++i) {
        if (weapons[i].weapon <= 0 || (size_t)weapons[i].weapon >= sizeof(ps->ammo) / sizeof(ps->ammo[0]) ||
            weapons[i].weapon >= 32 || !weapons[i].label || !weapons[i].item)
            return application_fail(e, QA_ERROR_FORMAT, "Original Q3 UI catalog leaves its actual PS weapon fields");
        char *label = NULL;
        if (!application_unified_frame_string(o->lease, &label, weapons[i].label, e)) return false;
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
    if (qa_actor_id_equal(o->ui_actor, o->player->actor)) {
        o->inventory = o->recipient_inventory;
        o->inventory_count = o->recipient_inventory_count;
    } else {
        if (!qa_inventory_entries(o->app->inventory, o->ui_actor, NULL, 0, &extent, e) || !current(o, e)) return false;
        qa_inventory_entry *entries = extent ? application_unified_frame_alloc(o->lease, extent, sizeof(*entries), e) : NULL;
        if (extent && !entries) return false;
        if (!qa_inventory_entries(o->app->inventory, o->ui_actor, entries, extent, &actual, e) ||
            actual != extent || !current(o, e)) return false;
        o->inventory = entries;
        o->inventory_count = actual;
    }
    extent = 0;
    if (!qa_inventory_item_definitions(o->app->inventory, o->ui_actor, NULL, 0, &extent, e) || !current(o, e)) return false;
    if (extent > SIZE_MAX / sizeof(*o->definitions))
        return application_fail(e, QA_ERROR_MEMORY, "Unified item definitions exceed their host extent");
    o->definitions = extent ? application_unified_frame_alloc(o->lease, extent, sizeof(*o->definitions), e) : NULL;
    if (extent && !o->definitions) return application_fail(e, QA_ERROR_MEMORY, "Copying actual player item declarations");
    if (!qa_inventory_item_definitions(o->app->inventory, o->ui_actor, o->definitions, extent, &actual, e) ||
        actual != extent || !current(o, e)) return false;
    /* Labels are borrowed by the inventory registry; copy them before any
     * provider observation can close their original declaration group. */
    for (size_t i = 0; i < actual; ++i) {
        const char *label = o->definitions[i].label;
        o->definitions[i].label = NULL;
        if (!label) return application_fail(e, QA_ERROR_FORMAT, "Player item has no actual declared label");
        char *copy = NULL;
        if (!application_unified_frame_string(o->lease, &copy, label, e)) return false;
        o->definitions[i].label = copy;
        o->definition_count = i + 1;
    }
    return true;
}

static bool armor(qa_armor *out, player_observation *o, qa_error *e)
{
    if (o->has_q3 && o->primary->kind != APPLICATION_PROVIDER_Q3 && !o->q3_combat) {
        out->regular.kind = o->q3.stats[3] ? QA_ARMOR_Q3 : QA_ARMOR_NONE;
        out->regular.points = o->q3.stats[3];
        out->regular.protection.q3_protection = .66f;
        return true;
    }
    if (o->has_q2) {
        out->regular.kind = o->q2.stats[5] ? QA_ARMOR_Q2 : QA_ARMOR_NONE;
        out->regular.points = o->q2.stats[5];
        return !o->q2.stats[5] || qa_strings_intern_cstr(qa_session_strings(o->app->session),
            "q2:remote-armor", &out->regular.item, e);
    }
    *out = o->combat.armor;
    return true;
}

static bool inventory(qa_unified_player_ui *out, player_observation *o, qa_error *e)
{
    bool original = o->has_q3 && o->primary->kind != APPLICATION_PROVIDER_Q3 &&
        o->arsenal == o->primary && !o->q3_inventory;
    if (!original && o->lease && qa_actor_id_equal(o->ui_actor, o->player->actor)) {
        out->inventory = o->recipient_ui_inventory;
        out->inventory_count = o->recipient_inventory_count;
        return true;
    }
    size_t extent = original ? o->q3_weapon_count * 2 : o->inventory_count;
    out->inventory = extent ? application_unified_frame_alloc(o->lease, extent, sizeof(*out->inventory), e) : NULL;
    if (extent && !out->inventory) return application_fail(e, QA_ERROR_MEMORY, "Retaining actual UI inventory");
    if (original) {
        for (size_t i = 0; i < o->q3_weapon_count; ++i) {
            const application_q3_catalog_weapon *w = o->q3_weapons + i;
            qa_inventory_entry *row = out->inventory + out->inventory_count++;
            row->count = count(o, w->item); row->capacity = 1;
            row->item=w->item;
            if (w->ammo) {
                row = out->inventory + out->inventory_count++;
                row->count = count(o, w->ammo); row->capacity = 200;
                row->item=w->ammo;
            }
        }
        return true;
    }
    for (size_t i = 0; i < o->inventory_count; ++i) {
        const qa_inventory_entry *v = o->inventory + i;
        qa_inventory_entry *row = out->inventory + out->inventory_count++;
        *row = *v;
    }
    return true;
}

static bool timer(qa_unified_player_ui *out, player_observation *o, const char *id,
    const char *label, double seconds, qa_error *e)
{
    if (!(seconds > 0)) return true;
    qa_unified_powerup_state *row = out->powerups + out->powerup_count++;
    *row = (qa_unified_powerup_state){.seconds = seconds};
    return application_unified_frame_string(o->lease, &row->id, id, e) && application_unified_frame_string(o->lease, &row->label, label, e);
}

static bool timers(qa_unified_player_ui *out, player_observation *o, qa_error *e)
{
    size_t capacity = o->primary->kind == APPLICATION_PROVIDER_QC ? o->qc.timer_count :
        o->primary->kind == APPLICATION_PROVIDER_Q1 ? QA_Q1_POWER_COUNT : 7;
    out->powerups = capacity ? application_unified_frame_alloc(o->lease, capacity, sizeof(*out->powerups), e) : NULL;
    if (capacity && !out->powerups) return false;
    if (o->primary->kind == APPLICATION_PROVIDER_QC) {
        for (size_t i = 0; i < o->qc.timer_count; ++i) {
            const qa_application_qc_power_timer *power = o->qc.timers + i;
            if (!timer(out, o, power->item, power->label, power->expires_seconds - o->qc.now_seconds, e)) return false;
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
            if (!timer(out, o, ids[power.power], labels[power.power], power.expires - powers.seconds, e)) return false;
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
            if (expires[i] > clock.frame.time_ns && !timer(out, o, ids[i], labels[i],
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
            if (!timer(out, o, ids[i], labels[i], ((double)viewed.powerups[i + 1] - o->q3_time) / 1000, e)) return false;
        if (o->primary->kind == APPLICATION_PROVIDER_Q3) {
            qa_q3_player_state state;
            if (!qa_q3_player_read(o->primary->state.q3, actor, &state))
                return application_fail(e, QA_ERROR_NOT_FOUND, "Q3 UI invulnerability lost its viewed source client");
            if (!timer(out, o, "q3:holdable_invulnerability", "Invulnerability",
                ((double)state.invulnerability_until - o->q3_time) / 1000, e)) return false;
        }
    } /* Original Q2's public PS exposes no complete source timer collection. */
    return true;
}



static qa_vec3 from_array(const float v[3]) { return qa_v3(v[0], v[1], v[2]); }

static bool view(qa_unified_player_view *out, player_observation *o, qa_error *e)
{
    qa_player_state control;
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
    qa_vec3 punch = control.state.kind == QA_RULESET_NETQUAKE ? control.state.data.nq.punch_angles : qa_v3(0, 0, 0);
    if (o->arsenal->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view weapon;
        if (!qa_q1_player_read(o->arsenal->state.q1, o->player->actor, &weapon))
            return application_fail(e, QA_ERROR_NOT_FOUND, "Unified camera lost its real Q1 punch owner");
        punch = weapon.punch_angles;
    }
    kick = qa_vec_add(kick, punch);
    out->origin = origin; out->angles = angles;
    if (has_death_yaw) out->angles.y = (float)death_yaw;
    out->view_height = height; out->kick_angles = kick;
    out->has_field_of_view = has_fov; out->field_of_view = fov;
    out->has_client_view_offset_delta = camera.has_client_view_offset;
    out->client_view_offset_delta = client_delta;
    out->has_blend = has_blend; out->has_damage_blend = has_damage;
    memcpy(out->blend, rgba, sizeof(rgba)); memcpy(out->damage_blend, damage_rgba, sizeof(damage_rgba));
    out->foreign_character_death = foreign_death;
    if (control.state.kind == QA_RULESET_NETQUAKE || control.state.kind == QA_RULESET_QUAKEWORLD) {
        bool nq = control.state.kind == QA_RULESET_NETQUAKE;
        out->has_pitch_drift = true; out->grounded = control.ground.hit != QA_TRACE_HIT_NONE;
        out->ideal_pitch = nq ? control.state.data.nq.ideal_pitch : 0;
        out->pitch_drift_disabled = camera.cutscene || o->intermission || view_combat.health <= 0 ||
            (nq ? control.state.data.nq.move_type != 3 : control.state.data.qw.spectator != 0);
    }
    return current(o, e);
}

static bool presentation_owner(qa_unified_source_identity *out,
    player_observation *o, const qa_application_qc_client_presentation *frame, qa_error *e)
{
    out->provider = frame->source.provider;
    return application_unified_frame_string(o->lease, &out->instance, frame->source.descriptor->selection.instance, e);
}
static bool client_presentation(qa_unified_frame_player *out, player_observation *o, qa_error *e)
{
    const application_unified_player_external *external = o->external;
    if (!external || (!external->declared_vitals && !external->declared_camera)) return true;
    qa_unified_client_presentation *p = application_unified_frame_alloc(o->lease, 1, sizeof(*p), e);
    if (!p) return application_fail(e, QA_ERROR_MEMORY, "Retaining actual declared CLIENT presentation");
    out->client_presentation = p; p->recipient = o->player->actor;
    const qa_application_qc_client_presentation *v = external->declared_vitals;
    if (v) {
        if (!qa_actor_id_equal(v->recipient, o->player->actor) || !v->vitals ||
            !qa_application_qc_client_presentation_current(o->app, v) || !presentation_owner(&p->hud_source, o, v, e)) return false;
        p->has_hud = true; p->health = v->health; p->armor = v->armor;
    }
    const qa_application_camera_view *camera = external->declared_camera;
    if (camera) {
        const qa_application_qc_client_presentation *source = external->declared_view;
        if (!source || !source->view || !qa_actor_id_equal(camera->actor, o->player->actor) ||
            !qa_actor_id_equal(source->recipient, o->player->actor) ||
            !qa_application_qc_client_presentation_current(o->app, source) || !presentation_owner(&p->view_source, o, source, e)) return false;
        p->has_view = true; p->origin = qa_vec_add(camera->origin, qa_v3(camera->view_offset.x, camera->view_offset.y, 0));
        p->angles = camera->angles; p->view_height = camera->view_offset.z;
    }
    return current(o, e);
}

static bool native_inventory_presentation(qa_unified_inventory_presentation *out,
    player_observation *o, const application_native_q2_inventory_presentation *p, qa_error *e)
{
    if (p->kind == APPLICATION_NATIVE_INVENTORY_PRESENTATION_NONE) return true;
    application_provider *owner = NULL;
    for (size_t i = 0; i < o->app->provider_count; ++i)
        if (o->app->providers[i]->owner == p->source) { owner = o->app->providers[i]; break; }
    application_q3_component_publication component = {0};
    const qa_product *product = owner ? owner->product : NULL;
    if (owner) { if (!application_unified_provider_state(&out->source, owner, e)) return false; }
    else {
        if (!application_q3_components_event_source_read(o->app, p->source, &component, e) ||
            !component.descriptor || !component.product || !component.content || !current(o, e)) return false;
        product = component.product;
        out->source = (qa_unified_provider_state){component.provider_name, component.content_name};
    }
    out->kind = (qa_unified_inventory_presentation_kind)p->kind;
    if (p->kind == APPLICATION_NATIVE_INVENTORY_PRESENTATION_WEAPON || p->kind == APPLICATION_NATIVE_INVENTORY_PRESENTATION_AMMUNITION)
        return item(&out->weapon, o, p->weapon, e);
    switch (p->icon_kind) {
    case APPLICATION_NATIVE_INVENTORY_ICON_NONE: out->icon_kind = QA_UNIFIED_INVENTORY_ICON_NONE; break;
    case APPLICATION_NATIVE_INVENTORY_ICON_IMAGE: out->icon_kind = QA_UNIFIED_INVENTORY_ICON_IMAGE; break;
    case APPLICATION_NATIVE_INVENTORY_ICON_WAD_PICTURE: out->icon_kind = QA_UNIFIED_INVENTORY_ICON_WAD_PICTURE; break;
    case APPLICATION_NATIVE_INVENTORY_ICON_SHADER: out->icon_kind = QA_UNIFIED_INVENTORY_ICON_SHADER; break;
    }
    if (p->icon_kind == APPLICATION_NATIVE_INVENTORY_ICON_NONE) return true;
    return application_unified_frame_string(o->lease, &out->content, product->identity, e) &&
        application_unified_frame_string(o->lease, &out->path, p->icon, e) &&
        application_unified_frame_string(o->lease, &out->lump, p->lump, e);
}

static bool ui_item(qa_unified_player_ui *out, player_observation *o,
    qa_item_id id, const char *label, bool weapon, int64_t ordinal,
    bool owned, bool has_ammo, bool finite, double value, double warning, qa_error *e)
{
    qa_unified_ui_item *v = out->items + out->item_count++;
    *v = (qa_unified_ui_item){.kind = weapon ? QA_UNIFIED_UI_WEAPON : QA_UNIFIED_UI_POWERUP,
        .source_ordinal = ordinal, .owned = owned, .has_ammo = has_ammo, .has_count = finite,
        .count = value, .warning_count = warning};
    return item(&v->id, o, id, e) && application_unified_frame_string(o->lease, &v->label, label, e);
}
static bool ui_gear(qa_unified_player_ui *out, player_observation *o, size_t ordinal, qa_error *e)
{
    return !o->has_gear || ui_item(out, o, o->gear.item, o->gear.label, true, (int64_t)ordinal,
        count(o, o->gear.item) > 0, true, false, 0, 0, e);
}

static bool ui_items(qa_unified_player_ui *out, player_observation *o, qa_error *e)
{
    size_t capacity = native_original(o) ? 0 : qc_arsenal(o) ? o->qc_binding_count :
        catalog_q3(o) ? o->q3_weapon_count : o->definition_count;
    if (o->has_gear) {
        if (capacity == SIZE_MAX) return application_fail(e, QA_ERROR_MEMORY, "Unified UI item extent overflows");
        ++capacity;
    }
    out->items = capacity ? application_unified_frame_alloc(o->lease, capacity, sizeof(*out->items), e) : NULL;
    if (capacity && !out->items) return false;
    /* Original API3/API2023 PlayerUI exposes inventory separately from its
     * public PS. Selected constituents below supply their complete UI roster. */
    if (native_original(o)) return ui_gear(out, o, 0, e);
    if (qc_arsenal(o)) {
        const qa_application_qc_weapon_ui_binding *active = qc_active(o);
        const char *ammo = qc_ammo_item(o);
        for (size_t i = 0; i < o->qc_binding_count; ++i) {
            const qa_application_qc_weapon_ui_binding *w = o->qc_bindings + i;
            if (o->has_gear && w->item == o->gear.item) continue;
            if (!ui_item(out, o, w->item, w->label, true, w->impulse,
                (qc_weapons(o)->items & w->bit) != 0, w != active || !ammo || o->qc_ammo > 0,
                w == active && ammo, o->qc_ammo, 0, e)) return false;
        }
        return ui_gear(out, o, o->qc_binding_count, e);
    }
    if (catalog_q3(o)) {
        for (size_t i = 0; i < o->q3_weapon_count; ++i) {
            const application_q3_catalog_weapon *w = o->q3_weapons + i;
            if (o->has_gear && w->item == o->gear.item) continue;
            double ammo = w->ammo ? count(o, w->ammo) : 0;
            if (!ui_item(out, o, w->item, w->label, true, w->weapon,
                count(o, w->item) > 0, !w->ammo || (o->q3_ui_owner == o->primary ? ammo != 0 : ammo > 0),
                w->ammo != 0, ammo, 0, e)) return false;
        }
        return ui_gear(out, o, o->q3_weapon_count, e);
    }
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
                    !application_q3_catalog_role_current(engine->game->catalog,engine->game) ||
                    !application_q3_catalog_weapons(engine->game->catalog, &weapons, &length, e) || !current(o, e)) return false;
                bool found = false;
                for (size_t n = 0; n < length; ++n)
                    if (weapons[n].item == d->item) { ordinal = (size_t)weapons[n].weapon; label = weapons[n].label; found = true; break; }
                if (!found) return application_fail(e, QA_ERROR_NOT_FOUND, "Q3 UI weapon lacks its actual source catalog row");
            }
        }
        if (!ui_item(out, o, d->item, label, d->weapon, (int64_t)ordinal, owned > 0,
            has_ammo, finite, ammo, warning, e)) return false;
    }
    return ui_gear(out, o, base_count, e);
}

static bool weapon_status(qa_unified_player_ui *out, player_observation *o, qa_error *e)
{
    qa_unified_weapon_status value = {0};
    application_provider *owner = NULL;
    qa_item_id selected = 0, ammo = 0;
    const char *label = NULL, *direct_item = NULL, *direct_ammo = NULL;
    if (o->has_gear && o->gear.active) {
        for (size_t i = 0; i < o->app->provider_count; ++i)
            if (o->app->providers[i]->owner == o->gear.source.owner) { owner = o->app->providers[i]; break; }
        selected = o->gear.item; label = o->gear.label;
    } else if (catalog_q3(o)) {
        const application_q3_catalog_weapon *w = q3_active(o);
        if (!w) return true;
        owner = o->q3_ui_owner; selected = w->item; label = w->label;
        double amount = w->ammo ? count(o, w->ammo) : -1;
        if (w->ammo && !(o->q3_ui_owner == o->primary && amount == -1)) {
            value.finite = true; ammo = w->ammo; value.count = amount; value.has_ammo_to_start = amount > 0;
        }
    } else if (native_original(o)) {
        const qa_q2_weapon_definition *w = o->native_weapon;
        if (!w) return true;
        owner = o->primary; direct_item = w->item; label = qa_q2_weapon_display_name(w->weapon);
        if (w->ammo) {
            value.finite = true; direct_ammo = w->ammo; value.count = o->q2.stats[3];
            value.has_ammo_to_start = value.count >= w->quantity; value.low = value.count <= w->warning;
        }
    } else if (qc_arsenal(o)) return true;
    else {
        qa_application_equipment_view *v = &o->equipment;
        if (!v->has_weapon_status || !v->item) return true;
        owner = o->arsenal;
        if (v->equipment_slot) {
            owner = NULL;
            for (size_t i = 0; i < o->app->provider_count; ++i)
                if (o->app->providers[i]->owner == v->provider) { owner = o->app->providers[i]; break; }
        }
        selected = v->item; label = o->equipment_label;
        value.finite = v->finite_ammo;
        if (value.finite) {
            ammo = v->ammo; value.count = v->ammo_count;
            value.has_ammo_to_start = v->has_ammo_to_start; value.low = v->low_ammo;
        }
    }
    out->weapon_status = application_unified_frame_alloc(o->lease, 1, sizeof(*out->weapon_status), e);
    if (!out->weapon_status) return application_fail(e, QA_ERROR_MEMORY, "Retaining actual selected weapon status");
    *out->weapon_status = value;
    qa_unified_weapon_status *v = out->weapon_status;
    return application_unified_provider_state(&v->source, owner, e) &&
        application_unified_frame_string(o->lease, &v->item, direct_item ? direct_item : selected ? identity(o, selected) : NULL, e) &&
        application_unified_frame_string(o->lease, &v->label, label, e) &&
        application_unified_frame_string(o->lease, &v->ammo_item, direct_ammo ? direct_ammo : ammo ? identity(o, ammo) : NULL, e);
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
        /* Actual Q3 ammo counters are signed words; the aggregate retains
         * the original integer multiplication and addition widths. */
        uint32_t word = (uint32_t)(int32_t)value;
        bool slow = w->weapon == QA_Q3_W_ROCKET || w->weapon == QA_Q3_W_GRENADE ||
            w->weapon == QA_Q3_W_RAIL || w->weapon == QA_Q3_W_SHOTGUN ||
            (o->q3_product == QA_Q3_TEAM_ARENA && w->weapon == QA_Q3_W_PROX);
        total += word * (slow ? 1000u : 200u);
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
            uint32_t word = (uint32_t)(int32_t)value;
            bool slow = w == QA_Q3_W_ROCKET || w == QA_Q3_W_GRENADE || w == QA_Q3_W_RAIL ||
                w == QA_Q3_W_SHOTGUN || (o->q3_product == QA_Q3_TEAM_ARENA && w == QA_Q3_W_PROX);
            total += word * (slow ? 1000u : 200u);
            if (total <= INT32_MAX && total >= 5000) return "none";
        }
        return total == 0 ? "empty" : "low";
    }
    return "none";
}

static bool q1_team_face(qa_unified_player_ui *out, player_observation *o, qa_error *e)
{
    if (o->source->family != QA_GAME_Q1 || strcmp(o->primary->product->campaign, "rogue")) return true;
    qa_cvars *cvars = qa_application_network_q1_cvars(o->app, o->primary->owner, e);
    if (!cvars) return false;
    const qa_cvar_view *teamplay = qa_cvars_read(cvars, o->primary->teamplay);
    if (!teamplay || !qa_q1_rogue_team_face_active(o->source->max_clients, teamplay->number)) return true;
    qa_application_network_q1_status_player players[255]; size_t count = 0;
    if (!qa_application_network_q1_status(o->app, o->primary->owner, players, &count, e) || !current(o, e)) return false;
    for (size_t i = 0; i < count; ++i) if (qa_actor_id_equal(players[i].actor, o->player->actor)) {
        out->q1_team_face = application_unified_frame_alloc(o->lease, 1, sizeof(*out->q1_team_face), e);
        if (!out->q1_team_face) return application_fail(e, QA_ERROR_MEMORY, "Retaining actual Rogue team face");
        out->q1_team_face->colors = players[i].colors; out->q1_team_face->frags = players[i].frags;
        return application_unified_frame_string(o->lease, &out->q1_team_face->content, o->primary->product->identity, e);
    }
    return application_fail(e, QA_ERROR_ARGUMENT, "Rogue team face lost its physical Source player");
}
static qa_ammo_warning warning_kind(const char *value)
{
    return !strcmp(value, "empty") ? QA_AMMO_EMPTY :
        !strcmp(value, "low") ? QA_AMMO_LOW : QA_AMMO_NONE;
}
static bool native_inventory_item(qa_unified_native_inventory *out, player_observation *o,
    qa_item_id id, const char *label, double amount, qa_error *e)
{
    qa_unified_native_inventory_item *row = out->items + out->item_count++;
    row->count = amount;
    return item(&row->item, o, id, e) && application_unified_frame_string(o->lease, &row->label, label, e);
}
static bool ui(qa_unified_player_ui *out, player_observation *o, qa_error *e)
{
    out->health = o->has_q2 ? (double)o->q2.stats[1] :
        o->has_q3 && o->primary->kind != APPLICATION_PROVIDER_Q3 ? (double)o->q3.stats[0] : (double)o->combat.health;
    if (!armor(&out->armor, o, e) || !inventory(out, o, e) || !timers(out, o, e) ||
        !ui_items(out, o, e) || !weapon_status(out, o, e)) return false;
    const char *active = NULL, *ammo = NULL;
    if (o->has_gear && o->gear.active) {
        out->arsenal_warning = warning_kind(arsenal_warning(o)); active = identity(o, o->gear.item);
    } else if (catalog_q3(o)) {
        const application_q3_catalog_weapon *w = q3_active(o);
        out->arsenal_warning = warning_kind(original_q3_warning(o)); active = o->q3_active ? identity(o, o->q3_active) : NULL;
        if (w && w->ammo) { ammo = identity(o, w->ammo); out->ammo_count = count(o, w->ammo); }
    } else if (native_original(o)) {
        const qa_q2_weapon_definition *w = o->native_weapon;
        if (w) { active = w->item; ammo = w->ammo; out->ammo_count = o->q2.stats[3]; }
    } else if (qc_arsenal(o)) {
        const qa_application_qc_weapon_ui_binding *w = qc_active(o);
        active = w ? identity(o, w->item) : NULL; ammo = qc_ammo_item(o); out->ammo_count = o->qc_ammo;
    } else {
        out->arsenal_warning = o->equipment.warning;
        active = o->equipment.item ? identity(o, o->equipment.item) : NULL;
        ammo = o->equipment.ammo ? identity(o, o->equipment.ammo) : NULL;
        out->ammo_count = o->equipment.ammo_count;
    }
    out->has_ammo = ammo != NULL; out->selected_arsenal = o->arsenal != o->primary;
    if (!application_unified_frame_string(o->lease, &out->active_weapon, active, e) ||
        !application_unified_frame_string(o->lease, &out->ammo_item, ammo, e)) return false;
    if (o->primary->kind == APPLICATION_PROVIDER_NATIVE && o->primary->state.native.q2_engine) {
        struct application_native_q2 *engine = o->primary->state.native.q2_engine;
        if (engine->inventory_scanner) {
            application_native_q2_inventory_readout mixed = {0};
            if (!application_native_q2_inventory_mixed_read(o->primary, o->ui_actor, &mixed, e) || !current(o, e)) {
                application_native_q2_inventory_readout_free(&mixed); return false;
            }
            bool ok = true;
            if (mixed.present) {
                out->native_inventory = application_unified_frame_alloc(o->lease, 1, sizeof(*out->native_inventory), e);
                if (!out->native_inventory) ok = application_fail(e, QA_ERROR_MEMORY, "Retaining native inventory");
                qa_unified_native_inventory *v = out->native_inventory;
                if (ok && mixed.count) {
                    v->items = application_unified_frame_alloc(o->lease, mixed.count, sizeof(*v->items), e);
                    if (!v->items) ok = application_fail(e, QA_ERROR_MEMORY, "Retaining native inventory rows");
                }
                for (size_t i = 0; ok && i < mixed.count; ++i)
                    ok = native_inventory_item(v, o, mixed.rows[i].item, mixed.rows[i].label, mixed.rows[i].count, e);
                if (ok) ok = item(&v->selected, o, mixed.selected, e) &&
                    native_inventory_presentation(&v->presentation, o, &mixed.selected_presentation, e);
            }
            application_native_q2_inventory_readout_free(&mixed);
            if (!ok) return false;
        } else if (engine->primary_inventory) {
            application_native_q2_ui_inventory native = {0};
            if (!application_native_q2_inventory_ui_read(o->primary, o->ui_actor, &native, e) || !current(o, e)) {
                application_native_q2_inventory_ui_free(&native); return false;
            }
            out->native_inventory = application_unified_frame_alloc(o->lease, 1, sizeof(*out->native_inventory), e);
            bool ok = out->native_inventory != NULL;
            if (!ok) application_fail(e, QA_ERROR_MEMORY, "Retaining native inventory");
            qa_unified_native_inventory *v = out->native_inventory;
            if (ok && native.count) {
                v->items = application_unified_frame_alloc(o->lease, native.count, sizeof(*v->items), e);
                if (!v->items) ok = application_fail(e, QA_ERROR_MEMORY, "Retaining native inventory rows");
            }
            for (size_t i = 0; ok && i < native.count; ++i)
                ok = native_inventory_item(v, o, native.items[i].item, native.items[i].label, native.items[i].count, e);
            if (ok) ok = item(&v->selected, o, native.selected, e);
            application_native_q2_inventory_ui_free(&native);
            if (!ok) return false;
        }
    }
    return q1_team_face(out, o, e) && current(o, e);
}

bool application_unified_player_values(qa_application *app, const application_unified_source *source,
    qa_net_client_id client, const qa_unified_session_player *player,
    const application_unified_player_external *external, qa_unified_frame *frame,
    const qa_inventory_entry *entries, size_t entry_count, qa_error *error)
{
    if (!app || !source || !player || !frame || frame->player || !application_unified_source_current(app, source) ||
        !application_unified_player_current(app, client, player))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified player requires its actual returned Source recipient");
    player_observation o = {.app = app, .lease = frame->lease, .recipient_inventory = entries,
        .recipient_inventory_count = entry_count, .recipient_ui_inventory = frame->inventories->entries, .source = source, .player = player, .external = external, .client = client,
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
    qa_unified_frame_player *out = application_unified_frame_alloc(o.lease, 1, sizeof(*out), error);
    if (!out) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Unified player state");
    frame->player = out; out->actor = player->actor; out->ui_actor = o.ui_actor;
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
            else o.native_weapon = qa_q2_base_weapon_view_model(qa_strings_cstr(qa_session_strings(engine->provider->application->session), engine->configstrings[base + (uint32_t)o.q2.gunindex]));
        }
    }
    bool qc = qc_arsenal(&o);
    if (ok && app->equipment) {
        ok = qa_equipment_weapon_view_read(app->equipment, o.ui_actor, &o.gear, &o.has_gear, error) && current(&o, error);
    }
    if (ok && o.arsenal->kind == APPLICATION_PROVIDER_Q3) {
        const qa_q3_model_names *names;
        ok = qa_application_equipment_q3_metadata_read(app, o.arsenal->owner, &names, error) && current(&o, error);
        if (ok) o.q3_product = names->product;
    }
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
        if (o.equipment.label)
            ok = application_unified_frame_string(o.lease, &o.equipment_label, o.equipment.label, error);
    }
    if (ok) ok = view(&out->view, &o, error) && ui(&out->ui, &o, error) &&
        client_presentation(out, &o, error) && current(&o, error);
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
            !application_q3_catalog_role_current(engine->game->catalog,engine->game) ||
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
