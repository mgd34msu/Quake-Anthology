#include "guest_qc_profile.h"
#include "guest_qc_items.h"
#include "guest_qc_pickups.h"
#include "guest_qc_combat.h"
#include "guest_qc_protection.h"
#include "control_frame.h"
#include "qa/application_network_qw.h"
#include "guest_qc_rerelease.h"
#include "supplies.h"
#include "qa/application_qc_presentation.h"
#include "qa/qc_observation.h"
#include "qa/text.h"
#include <stdio.h>

application_qc_client *application_qc_client_for_actor(const struct application_qc_state *engine,
    qa_actor_id actor)
{
    size_t slot;
    return engine && qa_actor_index_find(&engine->client_index, actor, &slot) ?
        &engine->clients[slot] : NULL;
}

bool application_qc_classic_qw(const struct application_qc_state *engine)
{
    return engine && !engine->provider->state.qc.qualified && engine->profile == QA_QC_QUAKEWORLD &&
        engine->max_clients == 32 && engine->provider->state.qc.instance;
}
static bool source_binding(struct application_qc_state *engine, uint32_t slot,
    qa_actor_id actor, qa_error *error)
{
    qa_qc_slot_binding binding;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(engine->services.session), actor);
    if (!record || !qa_qc_slot(engine->provider->state.qc.instance, slot, &binding) ||
        !qa_actor_id_equal(binding.actor, actor) || binding.owner != record->owner ||
        binding.source_slot != (record->has_source ? record->source_slot : 0) ||
        (binding.kind != QA_QC_SLOT_BORROWED && (binding.kind != QA_QC_SLOT_OWNED ||
         !application_qc_classic_qw(engine) || record->owner != engine->provider->owner ||
         !record->has_source || record->source_slot != slot)))
        return application_fail(error, QA_ERROR_FORMAT, "QC client differs from its actual physical source binding");
    return true;
}
bool application_qc_control_source_client(const application_provider *provider,
    qa_actor_id actor, bool *member, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ?
        provider->state.qc.engine : NULL;
    if (!engine || !provider->state.qc.instance || !engine->clients || !member)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC source membership requires its actual imported client owner");
    *member = false;
    if (!qa_actors_get(qa_session_actors(engine->services.session), actor)) return true;
    const application_qc_client *client = application_qc_client_for_actor(engine, actor);
    if (client && client->connected && client->spawned) {
        uint32_t slot = (uint32_t)(client - engine->clients);
        if (!source_binding(engine, slot, actor, error)) return false;
        *member = true; return true;
    }
    return true;
}
bool application_qc_player_source_actor(application_provider *provider, uint32_t slot,
    qa_actor_id *out, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ?
        provider->state.qc.engine : NULL;
    if (!engine || !out || !slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC player source actor requires its actual source owner");
    *out = (qa_actor_id){0};
    if (!application_qc_classic_qw(engine)) return true;
    qa_qc_slot_binding binding;
    if (slot > engine->max_clients || !qa_qc_slot(provider->state.qc.instance, slot, &binding) ||
        binding.kind != QA_QC_SLOT_OWNED || !source_binding(engine, slot, binding.actor, error))
        return application_fail(error, QA_ERROR_FORMAT, "QC player has no retained reserved source actor");
    *out = binding.actor; return true;
}
bool application_qc_source_clients_initialize(struct application_qc_state *engine, qa_error *error)
{
    if (!application_qc_classic_qw(engine)) return true;
    const qa_actor_registry *actors = qa_session_actors(engine->services.session);
    qa_actor_definition definition;
    if (!qa_strings_intern_cstr(qa_session_strings(engine->services.session),
        "quakec:reserved-client", &definition, error)) return false;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(engine->provider->state.qc.instance, slot, &binding))
            return application_fail(error, QA_ERROR_FORMAT, "QC reserved physical source row is absent");
        if (binding.kind != QA_QC_SLOT_FREE) {
            if (!source_binding(engine, slot, binding.actor, error)) return false;
            continue;
        }
        const qa_actor_record *existing = qa_actors_at_source(actors, engine->provider->owner, slot);
        qa_actor_id actor;
        if (existing) actor = existing->id;
        else {
            if (engine->provider->application->operation == APPLICATION_PERSISTING) continue;
            if (!qa_session_allocate(engine->services.session, engine->provider->owner,
                definition, true, slot, &actor, error)) return false;
        }
        if (!qa_qc_bind_actor(engine->provider->state.qc.instance, slot, actor, QA_QC_SLOT_OWNED, error)) {
            if (!existing) (void)qa_session_release(engine->services.session, actor, NULL);
            return false;
        }
    }
    return true;
}
bool application_qc_source_client_released(struct application_qc_state *engine,
    qa_actor_record released, qa_error *error)
{
    if (!application_qc_classic_qw(engine) || engine->loading || !engine->initialized ||
        !qa_qc_reserved_actor_rebind_ready(engine->provider->state.qc.instance) ||
        engine->provider->application->operation == APPLICATION_PERSISTING ||
        !qa_session_actor_allocation_ready(engine->services.session, engine->provider->owner)) return true;
    const application_qc_client *client = application_qc_client_for_actor(engine, released.id);
    uint32_t slot = client ? (uint32_t)(client - engine->clients) :
        released.owner == engine->provider->owner && released.has_source ? released.source_slot : 0;
    if (slot && slot <= engine->max_clients) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(engine->provider->state.qc.instance, slot, &binding) ||
            (binding.kind != QA_QC_SLOT_OWNED && binding.kind != QA_QC_SLOT_BORROWED) ||
            !qa_actor_id_equal(binding.actor, released.id)) return true;
        qa_actor_definition definition; qa_actor_id actor;
        if (!qa_strings_intern_cstr(qa_session_strings(engine->services.session),
            "quakec:reserved-client", &definition, error) ||
            !qa_session_allocate(engine->services.session, engine->provider->owner,
                definition, true, slot, &actor, error)) return false;
        if (!qa_qc_rebind_reserved_actor(engine->provider->state.qc.instance, slot,
            released.id, actor, QA_QC_SLOT_OWNED, error)) {
            (void)qa_session_release(engine->services.session, actor, NULL);
            return false;
        }
    }
    return true;
}

static bool qw_name_value(struct application_qc_state *engine, uint32_t slot,
    qa_bytes name, int32_t *out, qa_error *error)
{
    char key[32], text[32];
    snprintf(key, sizeof(key), "qw-name:%u", slot);
    size_t length = name.size;
    if (length >= sizeof(text)) length = sizeof(text) - 1;
    memcpy(text, name.data, length); text[length] = 0;
    return qa_qc_engine_string(engine->provider->state.qc.instance, key, text, sizeof(text), out, error);
}
static bool qw_name(struct application_qc_state *engine, uint32_t slot,
    int32_t reference, qa_bytes name, qa_error *error)
{
    const qa_qc_definition *field = application_qc_field(engine->field_bindings->netname, QA_QC_STRING, error);
    int32_t string;
    return field && qw_name_value(engine, slot, name, &string, error) &&
        qa_qc_set_entity_int(engine->provider->state.qc.instance, reference, field->offset, string, error);
}

static bool give_bits(struct application_qc_state *engine, int32_t reference,
    uint32_t add, uint32_t clear, qa_error *error)
{
    float items;
    if (!application_qc_float(engine, reference, engine->field_bindings->items, &items, error)) return false;
    uint32_t bits = ((uint32_t)qa_source_to_i32(items) & ~clear) | add;
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return application_qc_set_float(engine, reference, engine->field_bindings->items, (float)value, error);
}

static bool give_weapons(struct application_qc_state *engine, qa_actor_id actor,
    int32_t reference, const char *name, bool *handled, qa_error *error)
{
    *handled = name == NULL;
    uint32_t bits = 0;
    for (size_t i = 0;; ++i) {
        qa_item_id item;
        uint32_t bit = 0;
        const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
        if (engine->items) {
            if (i >= engine->items->definition_count) break;
            const qa_item_definition *definition = &engine->items->admissions[i].definition;
            if (!definition->weapon) continue;
            item = definition->item;
        } else if (profile) {
            if (i >= profile->weapon_count) break;
            item = profile->weapon_values[i].item;
            bit = profile->weapon_values[i].bit;
        } else {
            qa_application_qc_weapon_ui_binding binding;
            uint32_t via;
            qa_error lookup = {0};
            if (!application_qc_weapon_binding_at(engine, i, &binding, &via, &lookup)) {
                if (lookup.code != QA_OK) {
                    if (error) *error = lookup;
                    return false;
                }
                break;
            }
            item = binding.item; bit = binding.bit;
        }
        const char *identity = qa_strings_cstr(qa_session_strings(engine->services.session), item);
        const char *base = identity ? strrchr(identity, '/') : NULL;
        if (!name || (identity && (application_qc_command_name_equal(name, identity) ||
            application_qc_command_name_equal(name, base ? base + 1 : identity)))) {
            qa_inventory_entry entry;
            bool present;
            if (!qa_inventory_entry_find(engine->services.inventory, actor, item, &entry, &present, error)) return false;
            if (present) {
                entry.count = 1; entry.capacity = fmax(entry.capacity, 1);
                if (!qa_inventory_configure(engine->services.inventory, actor, &entry, NULL, NULL, error)) return false;
            } else if (bit) bits |= bit;
            else return application_fail(error, QA_ERROR_UNSUPPORTED, "QC weapon grant has no admitted Source inventory or ownership bit");
            *handled = true;
            if (name) break;
        }
    }
    return !bits || give_bits(engine, reference, bits, 0, error);
}

static bool give_ammo(struct application_qc_state *engine, int32_t reference,
    char item, float amount, qa_error *error)
{
    const qa_qc_definition *field = NULL, *alternate = NULL;
    bool extra = false;
    switch (item) {
    case 's': field = engine->field_bindings->ammo_shells; alternate = engine->field_bindings->ammo_shells1; break;
    case 'n': field = engine->field_bindings->ammo_nails; alternate = engine->field_bindings->ammo_nails1; break;
    case 'r': field = engine->field_bindings->ammo_rockets; alternate = engine->field_bindings->ammo_rockets1; break;
    case 'c': field = engine->field_bindings->ammo_cells; alternate = engine->field_bindings->ammo_cells1; break;
    case 'l': field = engine->field_bindings->ammo_nails; alternate = engine->field_bindings->ammo_lava_nails; extra = true; break;
    case 'm': field = engine->field_bindings->ammo_rockets; alternate = engine->field_bindings->ammo_multi_rockets; extra = true; break;
    case 'p': field = engine->field_bindings->ammo_cells; alternate = engine->field_bindings->ammo_plasma; extra = true; break;
    default: return true;
    }
    bool rogue = engine->profile != QA_QC_QUAKEWORLD &&
        engine->provider->product->campaign_id == QA_CAMPAIGN_ROGUE;
    if (!rogue) return extra || application_qc_set_float(engine, reference, field, amount, error);
    if (alternate && !application_qc_set_float(engine, reference, alternate, amount, error)) return false;
    /* Host_Give_f always updates shells; other Rogue counters update the
     * selected counter only when their actual alternate source field exists. */
    if (item != 's') {
        if (!alternate) return true;
        float selected;
        if (!application_qc_float(engine, reference, engine->field_bindings->weapon, &selected, error)) return false;
        if ((selected > 64) != extra) return true;
    }
    return application_qc_set_float(engine, reference, field, amount, error);
}

static bool give_all_ammo(struct application_qc_state *engine, int32_t reference,
    qa_error *error)
{
    const qa_qc_definition *fields[] = {
        engine->field_bindings->ammo_shells, engine->field_bindings->ammo_nails,
        engine->field_bindings->ammo_rockets, engine->field_bindings->ammo_cells,
        engine->field_bindings->ammo_shells1, engine->field_bindings->ammo_nails1,
        engine->field_bindings->ammo_rockets1, engine->field_bindings->ammo_cells1,
        engine->field_bindings->ammo_lava_nails, engine->field_bindings->ammo_multi_rockets,
        engine->field_bindings->ammo_plasma};
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i)
        if ((i < 4 || fields[i]) && !application_qc_set_float(engine, reference, fields[i],
            i == 1 || i == 5 || i == 8 ? 200 : 100, error)) return false;
    return true;
}

static bool give_template(struct application_qc_state *engine, qa_actor_id player,
    int32_t player_reference, const char *name, qa_error *error)
{
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    const qa_qc_game_fields *fields = engine->field_bindings;
    const qa_qc_definition *classname = application_qc_field(fields->classname, QA_QC_STRING, error);
    const qa_qc_definition *touch = application_qc_field(fields->touch, QA_QC_FUNCTION, error);
    if (!classname || !touch) return false;
    qa_qc_slot_binding source = {0};
    uint32_t source_slot = 0;
    for (uint32_t slot = engine->max_clients + 1; slot < qa_qc_entity_count(vm); ++slot) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(vm, slot, &binding) || !binding.actor.registry ||
            !qa_actors_get(qa_session_actors(engine->services.session), binding.actor)) continue;
        int32_t text, callback;
        const char *candidate;
        if (!qa_qc_actor_observation_int(vm, slot, binding.actor, classname->offset, &text, error) ||
            !qa_qc_actor_observation_int(vm, slot, binding.actor, touch->offset, &callback, error) ||
            !qa_qc_string(vm, text, &candidate, error)) return false;
        if (!callback || (strncmp(candidate, "item_", 5) && strncmp(candidate, "weapon_", 7)) ||
            !application_qc_command_name_equal(name, candidate)) continue;
        source = binding; source_slot = slot; break;
    }
    if (!source.actor.registry) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Cannot give %s: this map has no live Source item template", name);
        return false;
    }
    qa_unified_frame_lease *storage = application_control_storage_acquire(engine->provider->application, error);
    if (!storage) return false;
    uint32_t count = qa_qc_program_describe(engine->provider->state.qc.program).entity_field_words;
    int32_t *words = qa_unified_frame_lease_alloc(storage, count, sizeof(*words), _Alignof(int32_t), error);
    bool okay = words != NULL;
    for (uint32_t i = 0; okay && i < count; ++i)
        okay = qa_qc_actor_observation_int(vm, source_slot, source.actor, i, words + i, error);
    qa_vec3 origin;
    if (okay) okay = qa_qc_entity_vector(vm, player_reference, fields->origin->offset, &origin, error);
    int32_t reference = 0;
    qa_actor_id item = {0};
    if (okay) okay = qa_qc_spawn_entity(vm, &reference, error) &&
        qa_qc_reference_actor(vm, reference, &item, error);
    for (uint32_t i = 0; okay && i < count; ++i)
        okay = qa_qc_set_entity_int(vm, reference, i, words[i], error);
    if (okay) okay = qa_qc_set_entity_vector(vm, reference, fields->origin->offset, origin, error) &&
        application_qc_set_float(engine, reference, fields->solid, 1, error) &&
        application_qc_set_float(engine, reference, fields->nextthink, 0, error);
    const char *const targets[] = {"target", "targetname", "killtarget"};
    for (size_t i = 0; okay && i < sizeof(targets) / sizeof(*targets); ++i) {
        const qa_qc_definition *field = qa_qc_program_find_field(engine->provider->state.qc.program, targets[i]);
        if (field) okay = qa_qc_set_entity_int(vm, reference, field->offset, 0, error);
    }
    if (okay) {
        qa_qc_game_global globals[] = {
            {"self", {QA_QC_GAME_ACTOR, {.actor = item}}},
            {"other", {QA_QC_GAME_ACTOR, {.actor = player}}},
            {"time", {QA_QC_GAME_FLOAT, {.number = (float)((double)engine->source_time_ns / 1e9)}}}};
        okay = qa_qc_game_call_index(engine->provider->state.qc.game,
            (uint32_t)words[touch->offset], NULL, 0, globals, 3, NULL, error);
    }
    if (item.registry && qa_actors_get(qa_session_actors(engine->services.session), item)) {
        qa_error cleanup = {0};
        bool released = qa_session_release(engine->services.session, item, okay ? error : &cleanup);
        okay = okay && released;
    }
    qa_unified_frame_lease_release(storage);
    return okay;
}

static bool host_give(struct application_qc_state *engine, qa_actor_id actor,
    int32_t reference, size_t count, const char *const *arguments, qa_error *error)
{
    if (!count) return application_source_message(engine->provider, actor,
        "Usage: give <all|weapons|ammo|health|armor|keys|item> [amount]\n", error);
    const char *name = arguments[0];
    bool all = application_qc_command_name_equal(name, "all");
    bool weapons = application_qc_command_name_equal(name, "weapons");
    bool ammunition = application_qc_command_name_equal(name, "ammo");
    bool handled = false;
    if (!all && !weapons && !ammunition) {
        if (!application_supplies_cheat_item(engine->provider, actor, count, arguments, &handled, error)) return false;
        if (handled) return true;
    }
    float amount = count > 1 ? (float)atoi(arguments[1]) : 0;
    if (all || application_qc_command_name_equal(name, "health") || application_qc_command_name_equal(name, "h")) {
        float value = count > 1 ? amount : application_qc_command_name_equal(name, "h") ? 0 : 100;
        if (!application_qc_set_float(engine, reference, engine->field_bindings->health, value, error)) return false;
        if (!all) return true;
    }
    if (all || application_qc_command_name_equal(name, "armor") || application_qc_command_name_equal(name, "a")) {
        float points = count > 1 ? amount : application_qc_command_name_equal(name, "a") ? 0 : 200;
        const qa_qc_definition *type = qa_qc_program_find_field(engine->provider->state.qc.program, "armortype");
        uint32_t bit = points > 150 ? 32768u : points > 100 ? 16384u : points > 0 ? 8192u : 0;
        if (!give_bits(engine, reference, bit, 8192u | 16384u | 32768u, error) ||
            !application_qc_set_float(engine, reference, engine->field_bindings->armorvalue, points, error) ||
            !application_qc_set_float(engine, reference, type, points > 150 ? .8f : points > 100 ? .6f : points > 0 ? .3f : 0, error)) return false;
        if (!all) return true;
    }
    if (all || weapons) {
        if (!application_supplies_cheat_arsenal(engine->provider, actor, QA_Q1_CHEAT_WEAPONS, &handled, error) ||
            (!handled && !give_weapons(engine, actor, reference, NULL, &handled, error))) return false;
        if (!all) return true;
    }
    if (all || ammunition) {
        if (!application_supplies_cheat_arsenal(engine->provider, actor, QA_Q1_CHEAT_AMMO, &handled, error) ||
            (!handled && !give_all_ammo(engine, reference, error))) return false;
        if (!all) return true;
    }
    if (all || application_qc_command_name_equal(name, "keys"))
        return give_bits(engine, reference, 131072u | 262144u, 0, error);
    if (application_qc_command_name_equal(name, "items")) {
        const char *const items[] = {"quad", "pent", "ring", "suit"};
        for (size_t i = 0; i < sizeof(items) / sizeof(*items); ++i)
            if (!host_give(engine, actor, reference, 1, items + i, error)) return false;
        return true;
    }
    if (*name >= '0' && *name <= '9') {
        bool hipnotic = engine->profile != QA_QC_QUAKEWORLD &&
            engine->provider->product->campaign_id == QA_CAMPAIGN_HIPNOTIC;
        uint32_t weapon = 0;
        if (hipnotic && *name == '6') weapon = name[1] == 'a' ? UINT32_C(1) << 16 : 16;
        else if (hipnotic && *name == '9') weapon = UINT32_C(1) << 23;
        else if (hipnotic && *name == '0') weapon = 128;
        else if (*name >= '2') weapon = UINT32_C(1) << (*name - '2');
        return !weapon || give_bits(engine, reference, weapon, 0, error);
    }
    if (!give_weapons(engine, actor, reference, name, &handled, error)) return false;
    if (handled) return true;
    char ammo = application_qc_command_name_equal(name, "s") || application_qc_command_name_equal(name, "shells") ? 's' :
        application_qc_command_name_equal(name, "n") || application_qc_command_name_equal(name, "nails") ? 'n' :
        application_qc_command_name_equal(name, "r") || application_qc_command_name_equal(name, "rockets") ? 'r' :
        application_qc_command_name_equal(name, "c") || application_qc_command_name_equal(name, "cells") ? 'c' :
        application_qc_command_name_equal(name, "l") ? 'l' : application_qc_command_name_equal(name, "m") ? 'm' :
        application_qc_command_name_equal(name, "p") ? 'p' : 0;
    if (ammo) return give_ammo(engine, reference, ammo, amount, error);
    const char *classname = application_qc_command_name_equal(name, "quad") ? "item_artifact_super_damage" :
        application_qc_command_name_equal(name, "pent") ? "item_artifact_invulnerability" :
        application_qc_command_name_equal(name, "ring") ? "item_artifact_invisibility" :
        application_qc_command_name_equal(name, "suit") ? "item_artifact_envirosuit" : name;
    return give_template(engine, actor, reference, classname, error);
}

bool application_qc_game_command(application_provider *provider,
    const qa_command_invocation *command, bool *handled, qa_error *error)
{
    if (!application_qc_console_command(provider, command->context.actor, command->raw,
        command->context.actor.registry != 0, handled, error)) return false;
    return *handled || application_qc_host_command(provider, command, handled, error);
}

bool application_qc_host_command(application_provider *provider,
    const qa_command_invocation *command, bool *handled, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_QC || !command || !handled)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC Host command requires its Source owner and invocation");
    *handled = false;
    if (!command->argc) return true;
    const char *name = command->argv[0];
    bool god = application_qc_command_name_equal(name, "god");
    bool notarget = application_qc_command_name_equal(name, "notarget");
    bool noclip = application_qc_command_name_equal(name, "noclip");
    bool fly = application_qc_command_name_equal(name, "fly");
    bool give = application_qc_command_name_equal(name, "give");
    struct application_qc_state *engine = provider->state.qc.engine;
    if ((!god && !notarget && !noclip && !fly && !give) || !engine ||
        (engine->profile == QA_QC_QUAKEWORLD && (notarget || fly))) return true;
    if (!engine->initialized || !qa_qc_idle(provider->state.qc.instance) ||
        !application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC Host command lost its entered returned Source");
    qa_command_context context;
    if (!qa_application_capture_command_context(provider->application, &command->context, &context, error)) return false;
    if (!context.actor.registry) return true;
    bool member;
    if (!application_qc_control_source_client(provider, context.actor, &member, error)) return false;
    if (!member || (context.owner && context.owner != provider->owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC Host command has no genuine receiving Source client");
    *handled = true;
    bool allowed;
    if (engine->profile == QA_QC_QUAKEWORLD) {
        const qa_cvar_view *cheats = qa_cvars_read(engine->cvars, engine->cvar_handles.cheats);
        allowed = cheats && cheats->number != 0;
    } else {
        const qa_qc_definition *deathmatch = provider->state.qc.engine->global_bindings->deathmatch;
        float value;
        if (!deathmatch || deathmatch->type != QA_QC_FLOAT ||
            !qa_qc_global_float(provider->state.qc.instance, deathmatch->offset, &value, error))
            return application_fail(error, QA_ERROR_FORMAT, "QC Host command lost its actual deathmatch global");
        allowed = value == 0;
    }
    if (!allowed) {
        if (engine->profile == QA_QC_QUAKEWORLD)
            return application_source_message(provider, context.actor,
                "You must run the server with -cheats to enable this command.\n", error);
        return true;
    }
    int32_t reference;
    if (!qa_qc_actor_reference(provider->state.qc.instance, context.actor, false, &reference, error)) return false;
    if (give) {
        if (!host_give(engine, context.actor, reference, command->argc - 1, command->argv + 1, error)) return false;
        qa_application *app = provider->application;
        if (application_provider_for(app, context.actor, QA_ROLE_ARSENAL, "") == provider &&
            (!app->equipment || qa_equipment_primary_selected(app->equipment, context.actor)) &&
            qa_qc_program_find_function(provider->state.qc.program, "W_SetCurrentAmmo", NULL))
            return application_qc_named(engine, "W_SetCurrentAmmo", context.actor, error);
        return application_qc_publish_client_outputs(engine, error);
    }
    bool enabled;
    if (god || notarget) {
        float flags;
        if (!application_qc_float(engine, reference, engine->field_bindings->flags, &flags, error)) return false;
        int32_t bit = god ? 64 : 128, next = qa_source_to_i32(flags) ^ bit;
        enabled = (next & bit) != 0;
        if (!application_qc_set_float(engine, reference, engine->field_bindings->flags, (float)next, error)) return false;
    } else {
        const application_qc_client *client = application_qc_client_for_actor(engine, context.actor);
        bool spectator = client && client->spectator;
        if (!application_control_toggle_motion(provider->application, context.actor,
            fly ? QA_PHYSICS_FLY : QA_PHYSICS_NOCLIP, spectator, &enabled, error) ||
            !application_qc_set_float(engine, reference, engine->field_bindings->movetype, enabled ? fly ? 5 : 8 : 3, error)) return false;
    }
    if (!application_qc_control_source_client(provider, context.actor, &member, error) || !member)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC Host command retired its actual client during a Source store");
    char text[32];
    snprintf(text, sizeof(text), "%s %s\n", god ? "godmode" : notarget ? "notarget" :
        fly ? "flymode" : "noclip", enabled ? "ON" : "OFF");
    return application_source_message(provider, context.actor, text, error);
}

bool application_qc_console_command(application_provider *provider, qa_actor_id actor,
                                      const char *text, bool client_command,
                                      bool *handled, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_QC || !text || !handled ||
        !provider->state.qc.engine || !qa_qc_idle(provider->state.qc.instance) ||
        !application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC console command requires an idle source owner");
    *handled = false;
    struct application_qc_state *engine = provider->state.qc.engine;
    const application_qc_client *row = application_qc_client_for_actor(engine, actor);
    bool client = row && row->connected && row->spawned;
    if (actor.registry && (!client || !qa_actors_get(qa_session_actors(provider->application->session), actor)))
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC command actor has no live source client");
    const application_qc_command_export *export=&engine->command_exports[client_command?1:0];
    uint32_t function_index=export->index;
    const qa_qc_function *function=export->function;
    if (!function) return true;
    if (!function_index || function->parameter_count > 1 ||
        (function->parameter_count && function->parameter_sizes[0] != 1))
        return application_fail(error, QA_ERROR_FORMAT, "QuakeC console export has an incompatible source signature");
    qa_qc_game_global globals[] = {
        {"self", {QA_QC_GAME_ACTOR, {.actor = actor}}},
        {"time", {QA_QC_GAME_FLOAT, {.number = (float)((double)engine->source_time_ns / 1e9)}}}
    };
    uint32_t result[3];
    int32_t prior = 0;
    qa_qc_instance *vm = provider->state.qc.instance;
    bool staged = !function->parameter_count;
    qa_unified_frame_lease *storage=application_control_storage_acquire(provider->application,error);
    if(!storage) return false;
    size_t depth=engine->command_depth++,length=strlen(text),capacity=1;
    while(capacity<=length) {
        if(capacity>(size_t)INT32_MAX/2) {capacity=INT32_MAX;break;}
        capacity*=2;
    }
    int key_length=snprintf(NULL,0,"qc-command-slot:%zu:%zu",depth,capacity);
    char *key=qa_unified_frame_lease_alloc(storage,(size_t)key_length+1,1,1,error);
    int32_t reference=0;
    bool ok=key!=NULL;
    if(ok) {
        snprintf(key,(size_t)key_length+1,"qc-command-slot:%zu:%zu",depth,capacity);
        ok=!staged||qa_qc_global_int(vm,4,&prior,error);
        if(ok) {
            qa_error lookup={0};
            ok=qa_qc_engine_string(vm,key,text,0,&reference,&lookup);
            if(!ok&&lookup.code==QA_ERROR_NOT_FOUND) {
                if(capacity>qa_qc_string_available(vm)&&length<(size_t)INT32_MAX)
                    capacity=length+1;
                snprintf(key,(size_t)key_length+1,"qc-command-slot:%zu:%zu",depth,capacity);
                ok=qa_qc_engine_string(vm,key,text,capacity,&reference,error);
            } else if(!ok&&error) *error=lookup;
        }
    }
    bool parm_staged=false;
    if(ok&&staged) {
        uint32_t word;
        memcpy(&word, &reference, sizeof(word));
        ok=qa_qc_stage_globals(vm,4,&word,1,error);
        parm_staged=ok;
    }
    qa_qc_game_value argument={QA_QC_GAME_INTEGER,{.integer=reference}};
    if(ok) ok=qa_qc_game_call_index(provider->state.qc.game,function_index,&argument,
        function->parameter_count,globals,2,result,error);
    if(parm_staged) {
        uint32_t word;
        memcpy(&word, &prior, sizeof(word));
        qa_error unwind = {0};
        if (!qa_qc_stage_globals(vm, 4, &word, 1, &unwind)) {
            if (ok && error) *error = unwind;
            ok = false;
        }
    }
    --engine->command_depth;
    qa_unified_frame_lease_release(storage);
    if (!ok || !application_qc_publish_client_outputs(engine, error)) return false;
    float value;
    memcpy(&value, result, sizeof(value));
    *handled = client_command || value != 0;
    return true;
}

static bool parms(struct application_qc_state *engine, application_qc_client *client,
                   bool read, qa_error *error)
{
    for (unsigned i = 0; i < 16; ++i) {
        const qa_qc_definition *def = engine->global_bindings->spawn_parameters[i];
        if (def == NULL || def->type != QA_QC_FLOAT)
            return application_fail(error, QA_ERROR_FORMAT, "QuakeC spawn parm global is missing");
        if (read) {
            if (!qa_qc_global_float(engine->provider->state.qc.instance, def->offset, &client->parms[i], error)) return false;
        } else if (!qa_qc_set_global_float(engine->provider->state.qc.instance, def->offset, client->parms[i], error)) return false;
    }
    return true;
}
static bool source_spawn_colors(struct application_qc_state *engine, int32_t reference,
    uint32_t slot, uint8_t colors, qa_error *error)
{
    return application_qc_set_float(engine, reference, engine->field_bindings->colormap, (float)slot, error) &&
        application_qc_set_float(engine, reference, engine->field_bindings->team, (float)((colors & 15u) + 1u), error);
}
static bool bind_player(application_provider *provider, uint32_t slot, uint32_t seat,
                                 qa_actor_id actor, const char *name, bool spectator,
                                 bool new_player, bool primary_character, bool reserve, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    const struct application_qc_profile *qualified = provider->state.qc.qualified;
    if (engine == NULL || slot == 0 || (!qualified && slot > engine->max_clients) || name == NULL ||
        qa_actors_get(qa_session_actors(engine->services.session), actor) == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeC client admission");
    if (reserve && qualified)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "QC component requires its own deferred source admission contract");
    bool source_map_owned = application_provider_for(provider->application, actor, QA_ROLE_ENTITIES, "") == provider;
    if (!application_qc_player_map_ready(provider, primary_character, source_map_owned, error)) return false;
    if (qualified) {
        const application_qc_client *existing = application_qc_client_for_actor(engine, actor);
        slot = existing && existing->connected ? (uint32_t)(existing - engine->clients) : 0;
        if (!slot) for (uint32_t i = 1; i <= engine->max_clients; ++i)
            if (!engine->clients[i].connected) { slot = i; break; }
        if (!slot) return application_fail(error, QA_ERROR_MEMORY, "QC component source client capacity exceeded");
    }
    application_qc_client *client = &engine->clients[slot];
    if (client->connected) {
        if (!qa_actor_id_equal(client->actor, actor) || client->seat != seat ||
            client->spectator != spectator || client->primary_character != primary_character)
            return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC client slot is already occupied");
        if (client->spawned) return true;
    }
    const application_qc_client *existing = application_qc_client_for_actor(engine, actor);
    if (existing && existing != client && existing->connected)
        return application_fail(error, QA_ERROR_ARGUMENT, "Duplicate QuakeC client actor or seat");
    for (uint32_t i = 1; i <= engine->max_clients; ++i)
        if (i != slot && engine->clients[i].connected && engine->clients[i].seat == seat)
            return application_fail(error, QA_ERROR_ARGUMENT, "Duplicate QuakeC client actor or seat");
    const char *connect = spectator ? "SpectatorConnect" : "ClientConnect";
    uint32_t index;
    if (!qualified && spectator && engine->profile != QA_QC_QUAKEWORLD)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "QuakeC spectators require QuakeWorld");
    if (!qualified && !spectator && !qa_qc_program_find_function(provider->state.qc.program, connect, &index))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "QuakeC selected client callback is absent");
    if (application_qc_classic_qw(engine)) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(provider->state.qc.instance, slot, &binding))
            return application_fail(error, QA_ERROR_FORMAT, "QW client physical source row is absent");
        if (qa_actor_id_equal(binding.actor, actor)) {
            if (!source_binding(engine, slot, actor, error)) return false;
        } else {
            if (client->connected || binding.kind != QA_QC_SLOT_OWNED ||
                !source_binding(engine, slot, binding.actor, error))
                return application_fail(error, QA_ERROR_FORMAT, "QW client has no disconnected source reservation");
            qa_actor_id previous = binding.actor;
            if (!qa_qc_rebind_reserved_actor(provider->state.qc.instance, slot, previous,
                actor, QA_QC_SLOT_BORROWED, error) ||
                !qa_session_release(engine->services.session, previous, error)) return false;
        }
    } else if (!qa_qc_game_bind_client(provider->state.qc.game, slot, actor, error)) return false;
    if (!client->connected || !qa_actor_id_equal(client->actor, actor)) {
        client->receipt_seen = false; client->receipt_sequence = client->receipt_ordinal = 0;
    }
    if (!client->connected && new_player) client->colors = 0;
    if (!qa_actor_id_equal(client->actor, actor)) qa_actor_index_remove(&engine->client_index, client->actor);
    client->actor = actor; client->seat = seat; client->connected = true; client->spectator = spectator;
    qa_actor_index_put(&engine->client_index, actor, slot);
    client->primary_character = primary_character;
    if (qualified) {
        application_qc_inputs inputs = {.self = actor, .time_ns = engine->source_time_ns};
        if (!application_qc_seed_fields(engine, actor, error) || !application_qc_prepare_markers(engine, error)) return false;
        client->spawned = true;
        if (!application_qc_protection_reserve(engine,actor,error)) return false;
        if (!application_qc_run_calls(engine, &qualified->admit, &inputs, error)) return false;
        if (!qa_actors_get(qa_session_actors(engine->services.session), actor))
            return application_fail(error, QA_ERROR_NOT_FOUND, "QC component removed its client during admission");
        return application_qc_protection_activate(engine,actor,error) &&
            application_qc_items_admit(engine,actor,error) && application_qc_pickups_admit(engine,actor,error) &&
            application_qc_admit_client_outputs(engine, slot, error);
    }
    qa_qc_instance *vm = provider->state.qc.instance;
    int32_t reference;
    const qa_qc_definition *netname = application_qc_field(engine->field_bindings->netname, QA_QC_STRING, error);
    int32_t string;
    if (netname == NULL || !application_qc_reference(engine, actor, &reference, error)) return false;
    if (application_qc_classic_qw(engine)) {
        qa_qc_entity_access access = {.kind = QA_QC_ENTITY_BIND, .reference = reference};
        if (!qa_qc_slot(vm, slot, &access.binding))
            return application_fail(error, QA_ERROR_FORMAT, "QW client physical source row is absent");
        if (access.binding.kind == QA_QC_SLOT_OWNED &&
            !application_qc_combat_bind(engine, vm, &access, error)) return false;
    }
    if (engine->profile == QA_QC_QUAKEWORLD) {
        if (!qw_name(engine, slot, reference,
            (qa_bytes){(const uint8_t *)name, strlen(name)}, error)) return false;
    } else if (!qa_qc_string_allocate(vm, name, &string, error) ||
        !qa_qc_set_entity_int(vm, reference, netname->offset, string, error)) return false;
    if ((new_player || !client->has_parms) &&
        (!application_qc_named(engine, "SetNewParms", (qa_actor_id){0}, error) || !parms(engine, client, true, error))) return false;
    client->has_parms = true;
    if (reserve) return true;
    if (application_qc_classic_qw(engine))
        return application_qc_prepare_player(provider, actor, error) &&
            application_qc_begin_player(provider, actor, error);
    if (!source_spawn_colors(engine, reference, slot, client->colors, error)) return false;
    if (!parms(engine, client, false, error) || !(spectator ?
        application_qc_spectator_callback(engine, connect, actor, error) : application_qc_named(engine, connect, actor, error))) return false;
    if (!spectator && !application_qc_named(engine, "PutClientInServer", actor, error)) return false;
    if (qa_actors_get(qa_session_actors(engine->services.session), actor) == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND, "QuakeC client was removed during admission");
    client->spawned = true;
    qa_body_state body;
    if ((!spectator && !application_qc_items_admit(engine,actor,error)) ||
        !qa_world_body_read(engine->world, actor, &body, error)) return false;
    /* Source fields remain private, while the shared movement continuation is
     * admitted from the committed source spawn. */
    application_control_record *control;
    return application_control_ensure(provider->application, actor, body.angles, &control, error);
}
bool application_qc_bind_player(application_provider *provider, uint32_t slot, uint32_t seat,
    qa_actor_id actor, const char *name, bool spectator, bool new_player, bool primary_character, qa_error *error)
{ return bind_player(provider, slot, seat, actor, name, spectator, new_player, primary_character, false, error); }
bool application_qc_reserve_player(application_provider *provider, uint32_t slot, uint32_t seat,
    qa_actor_id actor, const char *name, bool spectator, bool new_player, bool primary_character, qa_error *error)
{ return bind_player(provider, slot, seat, actor, name, spectator, new_player, primary_character, true, error); }
bool application_qc_prepare_player(application_provider *provider, qa_actor_id actor, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ?
        provider->state.qc.engine : NULL;
    if (!application_qc_classic_qw(engine) || !qa_qc_idle(provider->state.qc.instance) || !application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QW source Prepare requires its idle actual client owner");
    application_qc_client *client = application_qc_client_for_actor(engine, actor);
    if (client && client->connected) {
        uint32_t slot = (uint32_t)(client - engine->clients);
        if (client->spawned || !client->has_parms || !source_binding(engine, slot, actor, error))
            return application_fail(error, QA_ERROR_ARGUMENT, "QW source Prepare requires an inactive retained client");
        if (client->prepared) return true;
        const char *raw;
        if (!qa_application_network_qw_userinfo_read(provider->application, actor, &raw, error)) return false;
        if (strlen(raw) > UINT16_MAX)
            return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld info string exceeds capacity");
        qa_bytes name;
        if (!qa_info_find(raw, (qa_bytes){(const uint8_t *)"name", 4}, QA_INFO_LAST, &name))
            name = (qa_bytes){(const uint8_t *)"unnamed", 7};
        qa_qc_instance *vm = provider->state.qc.instance;
        int32_t reference, name_string;
        qa_qc_program_info program = qa_qc_program_describe(provider->state.qc.program);
        const qa_qc_definition *colormap = application_qc_field(engine->field_bindings->colormap, QA_QC_FLOAT, error);
        const qa_qc_definition *team = application_qc_field(engine->field_bindings->team, QA_QC_FLOAT, error);
        const qa_qc_definition *netname = application_qc_field(engine->field_bindings->netname, QA_QC_STRING, error);
        const qa_cvar_view *maximum = qa_cvars_read(engine->cvars, engine->cvar_handles.qw_movement[8]);
        bool ok = colormap && team && netname && qa_qc_slot_reference(vm, slot, &reference, error);
        if (ok && (!maximum || !isfinite(maximum->number)))
            ok = application_fail(error, QA_ERROR_FORMAT, "QW source Prepare has no finite source speed cvar");
        const qa_qc_definition *fields[] = {engine->field_bindings->gravity, engine->field_bindings->maxspeed};
        float values[] = {1, maximum ? maximum->number : 0};
        const qa_qc_definition *optional[2];
        for (size_t i = 0; ok && i < sizeof(fields) / sizeof(*fields); ++i) {
            optional[i] = fields[i];
            if (optional[i] && optional[i]->type != QA_QC_FLOAT)
                ok = application_fail(error, QA_ERROR_FORMAT, "QW source Prepare optional movement field is not a float");
        }
        if (ok) ok = qw_name_value(engine, slot, name, &name_string, error);
        for (uint32_t word = 0; ok && word < program.entity_field_words; ++word)
            ok = qa_qc_project_entity_int(vm, reference, word, 0, error);
        ok = ok && qa_qc_project_entity_float(vm, reference, colormap->offset, (float)slot, error) &&
            qa_qc_project_entity_float(vm, reference, team->offset, 0, error) &&
            qa_qc_project_entity_int(vm, reference, netname->offset, name_string, error);
        for (size_t i = 0; ok && i < sizeof(fields) / sizeof(*fields); ++i)
            if (optional[i]) ok = qa_qc_project_entity_float(vm, reference, optional[i]->offset, values[i], error);
        if (!ok) return false;
        const qa_think *pending = qa_scheduler_pending(qa_session_scheduler(engine->services.session), actor);
        if (pending && pending->execution_provider == provider->owner)
            qa_scheduler_cancel(qa_session_scheduler(engine->services.session), actor);
        client->prepared = true;
        return true;
    }
    return application_fail(error, QA_ERROR_NOT_FOUND, "QW source Prepare has no connected physical client");
}
bool application_qc_begin_player(application_provider *provider, qa_actor_id actor, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ? provider->state.qc.engine : NULL;
    if (!engine || provider->state.qc.qualified ||
        !qa_qc_idle(provider->state.qc.instance) || !application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC source begin requires its idle reserved classic client");
    application_qc_client *client = application_qc_client_for_actor(engine, actor);
    if (client && client->connected) {
        uint32_t slot = (uint32_t)(client - engine->clients);
        if (client->spawned) return true;
        if (!client->has_parms || !source_binding(engine, slot, actor, error) ||
            (application_qc_classic_qw(engine) && !client->prepared))
            return application_fail(error, QA_ERROR_ARGUMENT, "QC source begin lacks its retained actor or spawn parameters");
        int32_t reference;
        if (!application_qc_reference(engine, actor, &reference, error)) return false;
        if (!application_qc_classic_qw(engine) && !source_spawn_colors(engine, reference, slot, client->colors, error)) return false;
        if (!parms(engine, client, false, error) || !(client->spectator ?
            application_qc_spectator_callback(engine, "SpectatorConnect", actor, error) :
            application_qc_named(engine, "ClientConnect", actor, error))) return false;
        if (!client->spectator && !application_qc_named(engine, "PutClientInServer", actor, error)) return false;
        if (!qa_actors_get(qa_session_actors(engine->services.session), actor))
            return application_fail(error, QA_ERROR_NOT_FOUND, "QC source removed its reserved client during begin");
        client->spawned = true;
        qa_body_state body; application_control_record *control;
        return (client->spectator || application_qc_items_admit(engine,actor,error)) &&
            qa_world_body_read(engine->world, actor, &body, error) &&
            application_control_ensure(provider->application, actor, body.angles, &control, error);
    }
    return application_fail(error, QA_ERROR_NOT_FOUND, "QC source begin has no reserved connected client");
}
bool application_qc_client_colors(application_provider *provider, qa_actor_id actor,
    int32_t top, int32_t bottom, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ? provider->state.qc.engine : NULL;
    if (!engine || provider->state.qc.qualified || !qa_qc_idle(provider->state.qc.instance) ||
        !application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC client colors require an idle classic source client");
    uint32_t shirt = (uint32_t)top & 15u, pants = (uint32_t)bottom & 15u;
    if (shirt > 13) shirt = 13;
    if (pants > 13) pants = 13;
    application_qc_client *client = application_qc_client_for_actor(engine, actor);
    if (client && client->connected) {
        uint32_t slot = (uint32_t)(client - engine->clients);
        int32_t reference;
        if (!source_binding(engine, slot, actor, error) || !application_qc_reference(engine, actor, &reference, error))
            return application_fail(error, QA_ERROR_ARGUMENT, "QC colors lack the connected physical client binding");
        if (!application_qc_set_float(engine, reference, engine->field_bindings->team, (float)(pants + 1u), error)) return false;
        client->colors = (uint8_t)((shirt << 4) | pants);
        return true;
    }
    return application_fail(error, QA_ERROR_NOT_FOUND, "QC colors have no connected source client");
}
bool application_qc_change_parms(application_provider *provider, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (engine == NULL || !qa_qc_idle(provider->state.qc.instance))
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC travel continuation is not idle");
    if (provider->state.qc.qualified) {
        for (uint32_t i = 1; i <= engine->max_clients; ++i)
            if (engine->clients[i].connected &&
                !application_qc_disconnect_player(provider, engine->clients[i].actor, error)) return false;
        return true;
    }
    for (uint32_t i = 1; i <= engine->max_clients; ++i) {
        application_qc_client *client = &engine->clients[i];
        if (!client->connected || qa_actors_get(qa_session_actors(engine->services.session), client->actor) == NULL) continue;
        if (!application_qc_named(engine, "SetChangeParms", client->actor, error) || !parms(engine, client, true, error)) return false;
    }
    const qa_qc_definition *flags = provider->state.qc.engine->global_bindings->serverflags;
    if (flags != NULL && (flags->type != QA_QC_FLOAT ||
        !qa_qc_global_float(provider->state.qc.instance, flags->offset, &engine->serverflags, error))) return false;
    return true;
}
bool application_qc_player_receive(application_provider *provider, qa_actor_id actor,
                                    uint64_t ordinal, qa_usercmd *command, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ?
        provider->state.qc.engine : NULL;
    if (!engine || !command)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC receipt needs its actual source and mutable command");
    if (engine->profile != QA_QC_RERELEASE) return true;
    application_qc_client *client = application_qc_client_for_actor(engine, actor);
    if (client && client->connected && client->spawned) {
        uint32_t slot = (uint32_t)(client - engine->clients);
        if (!source_binding(engine, slot, actor, error)) return false;
        if (client->receipt_seen && (command->sequence < client->receipt_sequence ||
            (command->sequence == client->receipt_sequence && ordinal <= client->receipt_ordinal)))
            return application_fail(error, QA_ERROR_ARGUMENT, "QC receipt does not advance its physical client sequence");
        int32_t reference;
        if (!application_qc_reference(engine, actor, &reference, error)) return false;
        bool source_impulse = command->kind != QA_RULESET_Q3 && command->kind != QA_RULESET_Q2_RERELEASE;
        if (source_impulse && command->impulse) {
            application_qc_weapon_command(engine,actor);
            if(!application_qc_set_float(engine, reference, engine->field_bindings->impulse, command->impulse, error)) return false;
        }
        if (!application_qc_rerelease_command(provider, actor, command, error)) return false;
        client->receipt_seen = true; client->receipt_sequence = command->sequence; client->receipt_ordinal = ordinal;
        return true;
    }
    return application_fail(error, QA_ERROR_NOT_FOUND, "QC receipt has no admitted physical source client");
}
bool application_qc_player_command(application_provider *provider, qa_actor_id actor,
                                    const qa_usercmd *command, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (engine == NULL || command == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC player command is missing");
    if (provider->state.qc.qualified)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Qualified QC input requires its mutable phase consumer");
    int32_t reference;
    if (!application_qc_reference(engine, actor, &reference, error)) return false;
    const qa_qc_definition *angles = application_qc_field(engine->field_bindings->v_angle, QA_QC_VECTOR, error);
    if (angles == NULL || !qa_qc_set_entity_vector(provider->state.qc.instance, reference, angles->offset, command->angles, error) ||
        !application_qc_set_float(engine, reference, engine->field_bindings->button0, (command->buttons & 1u) ? 1.0f : 0.0f, error) ||
        !application_qc_set_float(engine, reference, engine->field_bindings->button2, (command->buttons & 2u) ? 1.0f : 0.0f, error)) return false;
    const application_qc_client *client = application_qc_client_for_actor(engine, actor);
    bool received = engine->profile == QA_QC_RERELEASE && client && client->connected &&
        client->spawned && client->receipt_seen && command->sequence <= client->receipt_sequence;
    if (!received && command->impulse != 0) {
        application_qc_weapon_command(engine,actor);
        if(!application_qc_set_float(engine, reference, engine->field_bindings->impulse, command->impulse, error)) return false;
    }
    return true;
}
bool application_qc_client_userinfo(application_provider *provider, qa_actor_id actor, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (!engine) return application_fail(error, QA_ERROR_ARGUMENT, "QC userinfo has no engine owner");
    application_qc_client *client = application_qc_client_for_actor(engine, actor);
    if (client && client->connected) {
        const struct application_qc_profile *profile = provider->state.qc.qualified;
        application_qc_inputs inputs = {.self = actor, .time_ns = engine->source_time_ns};
        if (profile) return client->spawned ? application_qc_run_calls(engine, &profile->userinfo, &inputs, error) :
            application_fail(error, QA_ERROR_NOT_FOUND, "QC userinfo client has not completed declared admission");
        if (engine->profile != QA_QC_QUAKEWORLD) {
            if (client->spawned) return true;
        } else {
            const char *raw; int32_t reference;
            if (!qa_application_network_qw_userinfo_read(provider->application, actor, &raw, error)) return false;
            if (strlen(raw) > UINT16_MAX)
                return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld info string exceeds capacity");
            qa_bytes name;
            if (!qa_info_find(raw, (qa_bytes){(const uint8_t *)"name", 4}, QA_INFO_LAST, &name))
                name = (qa_bytes){(const uint8_t *)"unnamed", 7};
            return application_qc_reference(engine, actor, &reference, error) &&
                qw_name(engine, (uint32_t)(client - engine->clients), reference, name, error);
        }
    }
    return application_fail(error, QA_ERROR_NOT_FOUND, "QC userinfo requires a live admitted client");
}
bool application_qc_disconnect_player(application_provider *provider, qa_actor_id actor, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (!engine) return application_fail(error, QA_ERROR_ARGUMENT, "QC disconnect has no engine owner");
    application_qc_client *client = application_qc_client_for_actor(engine, actor);
    if (client && client->connected) {
        if (!client->spawned) return true;
        const struct application_qc_profile *profile = provider->state.qc.qualified;
        application_qc_inputs inputs = {.self = actor, .time_ns = engine->source_time_ns};
        bool ok = profile ? application_qc_run_calls(engine, &profile->disconnect, &inputs, error) :
            client->spectator ? application_qc_spectator_callback(engine, "SpectatorDisconnect", actor, error) :
            application_qc_named(engine, "ClientDisconnect", actor, error);
        if (ok) ok = application_qc_pickups_release(engine,actor,error) &&
            application_qc_items_release(engine,actor,error) && application_qc_protection_release(engine,actor,error) &&
            application_qc_combat_release(engine,actor,error);
        if (ok && qa_actor_id_equal(client->actor, actor)) {
            client->spawned = false;
            client->prepared = false;
            client->output_published = false;
            client->outputs = (application_client_outputs){0};
            client->receipt_seen = false; client->receipt_sequence = client->receipt_ordinal = 0;
            client->pending_weapon=0; client->pending_weapon_following=false;
        }
        return ok;
    }
    return true;
}
bool application_qc_actor_traits(application_provider *provider, qa_actor_id actor, qa_builtin_actor_traits *out)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (engine == NULL || out == NULL) return false;
    qa_error ignored = {0}; int32_t reference; float flags, health, damage; int32_t name;
    const qa_qc_definition *classname = provider->state.qc.engine->field_bindings->classname;
    qa_string_id identity;
    if (classname == NULL || classname->type != QA_QC_STRING ||
        !application_qc_reference(engine, actor, &reference, &ignored) ||
        !application_qc_float(engine, reference, engine->field_bindings->flags, &flags, &ignored) ||
        !application_qc_float(engine, reference, engine->field_bindings->health, &health, &ignored) ||
        !application_qc_float(engine, reference, engine->field_bindings->takedamage, &damage, &ignored) ||
        !qa_qc_entity_int(provider->state.qc.instance, reference, classname->offset, &name, &ignored) ||
        !qa_qc_string_identity(provider->state.qc.instance, name, &identity, &ignored)) return false;
    if (!isfinite(flags) || (double)flags < INT32_MIN || (double)flags > INT32_MAX) return false;
    uint32_t bits = (uint32_t)(int32_t)flags;
    qa_builtin_actor_traits value = {.monster = (bits & 32u) != 0, .no_target = (bits & 128u) != 0,
        .grounded = (bits & 512u) != 0, .in_water = (bits & 16u) != 0,
        .aimed_damage = damage == 2, .damageable_target = damage != 0,
        .max_health = health, .view_height = 22, .classname = identity};
    const application_qc_client *client = application_qc_client_for_actor(engine, actor);
    value.player = client && client->connected;
    const qa_application_ui_names *names = qa_application_ui_names_read(provider->application);
    if (identity == names->trigger_hurt) {
        float solid, hurt;
        int32_t touch;
        const qa_qc_game_fields *fields = engine->field_bindings;
        if (fields->solid && fields->dmg && fields->touch &&
            application_qc_float(engine, reference, fields->solid, &solid, &ignored) &&
            application_qc_float(engine, reference, fields->dmg, &hurt, &ignored) &&
            qa_qc_entity_int(provider->state.qc.instance, reference, fields->touch->offset, &touch, &ignored))
            value.navigation_hazard = solid == 1 && hurt > 0 && touch != 0;
    }
    *out = value; return true;
}
