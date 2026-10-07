#include "guest_projection_private.h"
#include "guest_qc_items.h"
#include "guest_qc_pickups.h"
#include "guest_qc_combat.h"
#include "guest_qc_protection.h"
#include "guest_native_q2_private.h"
#include "guest_q3_combat_state.h"

static bool current(guest_projection_actor *context, qa_q3_host_game_data *data,
                    qa_error *error)
{
    application_guest_projection *p = context->projection;
    q3g_role *role = p->role;
    uint32_t slot;
    if (!qa_actors_get(qa_session_actors(role->engine->provider->application->session), context->actor) ||
        !qa_q3_host_actor_slot(role->host, context->actor, &slot, error) ||
        slot != context->slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest projection actor generation retired");
    if (!qa_q3_host_game_data_read(role->host, data) ||
        (!p->located_inventory && (data->entity_stride != p->entity_stride || data->client_stride != p->client_stride)) ||
        context->slot >= data->entity_count || context->slot >= data->client_count)
        return application_fail(error, QA_ERROR_FORMAT, "Guest projection source records changed");
    return true;
}

static bool read_word(q3g_role *role, uint32_t address, uint32_t *out, qa_error *error)
{
    uint8_t bytes[4];
    if (!qa_qvm_read(role->vm, address, bytes, sizeof(bytes), error)) return false;
    *out = qa_load_u32le(bytes); return true;
}

static bool write_word(q3g_role *role, uint32_t address, uint32_t word, qa_error *error)
{
    uint8_t bytes[4]; qa_store_u32le(bytes, word);
    return qa_qvm_write(role->vm, address, (qa_bytes){bytes, sizeof(bytes)}, error);
}

static bool inventory_refresh(application_guest_projection *p, qa_error *error)
{
    if (!p->inventory_public) return true;
    const guest_inventory_weapon *weapons = NULL; size_t count = 0;
    if (!p->inventory_catalog.read || !p->inventory_catalog.context)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original public inventory has no retained source catalog");
    if (!p->inventory_catalog.read(p->inventory_catalog.context, p->role, &weapons, &count, error)) return false;
    if (count > 15 || (count && !weapons))
        return application_fail(error, QA_ERROR_FORMAT, "Original catalog exceeds distinct public weapon slots");
    guest_inventory_field fields[30] = {0}; size_t used = 0;
    uint32_t slots = 0;
    for (size_t i = 0; i < count; ++i) {
        guest_inventory_weapon weapon = weapons[i];
        if (weapon.weapon < 1 || weapon.weapon > 15 || !weapon.item || (slots & (UINT32_C(1) << weapon.weapon)))
            return application_fail(error, QA_ERROR_FORMAT, "Original catalog weapon lacks a distinct actual public slot");
        slots |= UINT32_C(1) << weapon.weapon;
        qa_item_id items[2] = {weapon.item, weapon.ammo};
        for (size_t j = 0; j < 2; ++j) {
            if (!items[j]) continue;
            for (size_t k = 0; k < used; ++k)
                if (fields[k].item == items[j])
                    return application_fail(error, QA_ERROR_FORMAT, "Original catalog item aliases another public inventory field");
            fields[used++] = (guest_inventory_field){.item = items[j],
                .field = {GUEST_CLIENT_RECORD, j ? p->public_inventory.ammo_offset + weapon.weapon * 4 : p->public_inventory.weapons_offset},
                .mask = j ? 0 : UINT32_C(1) << weapon.weapon, .allowed_mask = UINT32_MAX,
                .capacity = {.kind = GUEST_CAPACITY_CONSTANT, .value.constant = j ? 0 : 1},
                .public_weapon = weapon.weapon};
        }
    }
    bool changed = used != p->inventory_count;
    for (size_t i = 0; !changed && i < used; ++i)
        changed = fields[i].item != p->inventory[i].item || fields[i].mask != p->inventory[i].mask ||
            fields[i].field.offset != p->inventory[i].field.offset || fields[i].public_weapon != p->inventory[i].public_weapon;
    if (!changed) return true;
    guest_inventory_field *owned = used ? malloc(used * sizeof(*owned)) : NULL;
    if (used && !owned) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual public inventory catalog fields");
    if (used) memcpy(owned, fields, used * sizeof(*owned));
    free(p->inventory); p->inventory = owned; p->inventory_count = used;
    return true;
}

bool application_guest_inventory_catalog_bind(q3g_role *role,
    const guest_inventory_catalog_services *services, qa_error *error)
{
    application_guest_projection *p = role ? role->projection : NULL;
    if (!p || !p->inventory_public || !services || !services->context || !services->read || p->inventory_catalog.read)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original public inventory catalog requires one genuine constructor owner");
    for (guest_projection_actor *actor = p->actors; actor; actor = actor->next)
        if (actor->inventory_bound)
            return application_fail(error, QA_ERROR_ARGUMENT, "Original inventory catalog cannot change after binding source actors");
    p->inventory_catalog = *services; return true;
}

static bool address(guest_projection_actor *context, guest_field field, uint32_t *out,
                    qa_error *error)
{
    qa_q3_host_game_data data;
    if (!current(context, &data, error)) return false;
    application_guest_projection *p = context->projection;
    uint64_t entity = data.entities_address + (uint64_t)context->slot * data.entity_stride;
    uint64_t client = data.clients_address + (uint64_t)context->slot * data.client_stride;
    if (p->located_inventory) {
        uint64_t base = field.record == GUEST_CLIENT_RECORD ? client : entity;
        if (!data.entities_address || !data.clients_address || base > INT32_MAX || base > UINT32_MAX - field.offset)
            return application_fail(error, QA_ERROR_FORMAT, "Original public inventory lost its located source record");
        *out = (uint32_t)base + field.offset; return true;
    }
    if (entity > UINT32_MAX - p->client_pointer || client > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Guest source client pointer exceeds QVM memory");
    uint32_t pointer;
    if (!read_word(p->role, (uint32_t)entity + p->client_pointer, &pointer, error)) return false;
    if (pointer != client)
        return application_fail(error, QA_ERROR_FORMAT, "Guest entity does not own its located client record");
    uint64_t base = field.record == GUEST_CLIENT_RECORD ? client : entity;
    if (base > UINT32_MAX - field.offset)
        return application_fail(error, QA_ERROR_FORMAT, "Guest projection address exceeds QVM memory");
    *out = (uint32_t)base + field.offset; return true;
}

static bool capacity_read(guest_projection_actor *context, const guest_inventory_field *field,
                          int32_t *out, qa_error *error)
{
    const guest_capacity *capacity = &field->capacity;
    q3g_role *role = context->projection->role;
    if (context->projection->inventory_public && !field->mask) {
        uint32_t client, entity;
        if (!address(context, (guest_field){GUEST_CLIENT_RECORD, 0}, &client, error) ||
            !address(context, (guest_field){GUEST_ENTITY_RECORD, 0}, &entity, error)) return false;
        guest_inventory_source source = {client, entity, context->slot, field->public_weapon};
        if (!application_guest_public_inventory_capacity(&context->projection->public_inventory,
            role->vm, &source, out, error)) return false;
        uint32_t after_client, after_entity;
        return address(context, (guest_field){GUEST_CLIENT_RECORD, 0}, &after_client, error) &&
            address(context, (guest_field){GUEST_ENTITY_RECORD, 0}, &after_entity, error) &&
            ((client == after_client && entity == after_entity) ||
             application_fail(error, QA_ERROR_ARGUMENT, "Original inventory query changed its actual source records"));
    }
    if (capacity->kind == GUEST_CAPACITY_CONSTANT) *out = capacity->value.constant;
    else if (capacity->kind == GUEST_CAPACITY_FIELD) {
        uint32_t at, word;
        if (!address(context, capacity->value.field, &at, error) || !read_word(role, at, &word, error)) return false;
        *out = (int32_t)word;
    } else if (capacity->kind == GUEST_CAPACITY_SOURCE) {
        *out = capacity->value.constant;
        for (size_t i = 0; i < capacity->override_count; ++i) {
            const guest_capacity_override *value = &capacity->overrides[i];
            uint32_t word;
            if (!read_word(role, value->condition.address, &word, error)) return false;
            bool match = (int32_t)word == value->condition.value;
            if (match == value->condition.equal) { *out = value->value; break; }
        }
    } else return application_fail(error, QA_ERROR_UNSUPPORTED, "Guest public source capacity query is not admitted");
    return *out >= 0 || application_fail(error, QA_ERROR_FORMAT, "Guest source capacity is negative");
}

static size_t inventory_count(void *context)
{
    return ((guest_projection_actor *)context)->projection->inventory_count;
}

static bool inventory_checked_count(void *opaque, size_t *out, qa_error *error)
{
    guest_projection_actor *context = opaque;
    q3g_role *role = context->projection->role;
    ++role->engine->calls;
    qa_q3_host_game_data data;
    bool ok = current(context, &data, error) && inventory_refresh(context->projection, error) &&
        current(context, &data, error);
    if (ok) *out = context->projection->inventory_count;
    --role->engine->calls;
    return ok;
}

static bool inventory_at_inner(void *opaque, size_t index, qa_inventory_entry *out, qa_error *error)
{
    guest_projection_actor *context = opaque;
    application_guest_projection *p = context->projection;
    if (!inventory_refresh(p, error)) return false;
    if (index >= p->inventory_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest inventory index exceeds its declaration");
    guest_inventory_field copied = p->inventory[index];
    const guest_inventory_field *field = &copied;
    uint32_t at, word; int32_t capacity;
    if (!address(context, field->field, &at, error) || !read_word(p->role, at, &word, error) ||
        !capacity_read(context, field, &capacity, error)) return false;
    if (field->mask && (word & ~field->allowed_mask))
        return application_fail(error, QA_ERROR_FORMAT, "Guest inventory contains undeclared source bits");
    *out = (qa_inventory_entry){field->item,
        field->mask ? (word & field->mask ? 1 : 0) : (double)(int32_t)word,
        (double)capacity, field->mask ? QA_COUNT_STACK : QA_COUNT_SOURCE_INT32};
    return true;
}

static bool inventory_write_inner(void *opaque, const qa_inventory_entry *entry, qa_error *error)
{
    guest_projection_actor *context = opaque;
    application_guest_projection *p = context->projection;
    if (!inventory_refresh(p, error)) return false;
    const guest_inventory_field *field = NULL;
    guest_inventory_field copied;
    for (size_t i = 0; i < p->inventory_count; ++i)
        if (p->inventory[i].item == entry->item) { copied = p->inventory[i]; field = &copied; break; }
    if (!field) return application_fail(error, QA_ERROR_ARGUMENT, "Guest inventory item has no source storage");
    if (!isfinite(entry->count) || floor(entry->count) != entry->count ||
        entry->count < INT32_MIN || entry->count > INT32_MAX ||
        !isfinite(entry->capacity) || floor(entry->capacity) != entry->capacity ||
        entry->capacity < 0 || entry->capacity > INT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest inventory value exceeds source representation");
    uint32_t at, capacity_at = 0; int32_t capacity;
    if (!address(context, field->field, &at, error) || !capacity_read(context, field, &capacity, error)) return false;
    if (field->mask) {
        if (entry->policy != QA_COUNT_STACK || (entry->count != 0 && entry->count != 1) || entry->capacity != 1)
            return application_fail(error, QA_ERROR_ARGUMENT, "Guest packed item requires zero or one ownership");
        uint32_t previous;
        if (!read_word(p->role, at, &previous, error)) return false;
        if (previous & ~field->allowed_mask)
            return application_fail(error, QA_ERROR_FORMAT, "Guest inventory contains undeclared source bits");
        return write_word(p->role, at, entry->count != 0 ? previous | field->mask : previous & ~field->mask, error);
    }
    if (entry->policy != QA_COUNT_SOURCE_INT32)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest counter requires its signed source policy");
    if (field->capacity.kind == GUEST_CAPACITY_FIELD) {
        if (!address(context, field->capacity.value.field, &capacity_at, error)) return false;
    } else if (entry->capacity != capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest inventory capacity is owned by original source");
    if (!write_word(p->role, at, (uint32_t)(int32_t)entry->count, error)) return false;
    if (field->capacity.kind != GUEST_CAPACITY_FIELD) return true;
    uint32_t current_capacity;
    if (!address(context, field->capacity.value.field, &current_capacity, error)) return false;
    if (current_capacity != capacity_at)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest capacity record changed during count publication");
    return write_word(p->role, capacity_at, (uint32_t)(int32_t)entry->capacity, error);
}

static bool inventory_at(void *opaque, size_t index, qa_inventory_entry *out, qa_error *error)
{
    guest_projection_actor *context = opaque;
    q3g_role *role = context->projection->role;
    ++role->engine->calls;
    qa_inventory_entry entry;
    qa_q3_host_game_data data;
    bool ok = inventory_at_inner(opaque, index, &entry, error) && current(context, &data, error);
    if (ok) *out = entry;
    --role->engine->calls; return ok;
}

static bool inventory_write(void *opaque, const qa_inventory_entry *entry, qa_error *error)
{
    guest_projection_actor *context = opaque;
    q3g_role *role = context->projection->role;
    ++role->engine->calls;
    qa_q3_host_game_data data;
    bool ok = inventory_write_inner(opaque, entry, error) && current(context, &data, error);
    --role->engine->calls; return ok;
}

static bool inventory_acquire(void *opaque, const qa_inventory_entry *before,
    double amount, bool publish, qa_inventory_entry *after, bool *handled, bool *writes, qa_error *error)
{
    guest_projection_actor *context = opaque;
    application_guest_projection *p = context->projection;
    q3g_role *role = p->role;
    *handled = false; *writes = false;
    if (!p->inventory_public || !p->public_inventory.acquisition_entry) return true;
    ++role->engine->calls;
    bool ok = inventory_refresh(p, error);
    guest_inventory_field field = {0};
    bool found = false;
    for (size_t i = 0; ok && i < p->inventory_count; ++i)
        if (p->inventory[i].item == before->item) { field = p->inventory[i]; found = true; break; }
    if (!ok || !found || field.mask) { --role->engine->calls; return ok; }
    *handled = true;
    qa_inventory_entry actual, normalized;
    uint32_t client = 0, entity = 0, pointer = 0;
    qa_q3_host_game_data data;
    size_t index = 0;
    while (ok && index < p->inventory_count && p->inventory[index].item != before->item) ++index;
    if (ok) ok = inventory_at_inner(context, index, &actual, error);
    if (ok && (actual.policy != before->policy || actual.capacity != before->capacity ||
        (publish && actual.count != before->count)))
        ok = application_fail(error, QA_ERROR_NOT_FOUND, "Original acquisition entry changed before its Source grant");
    normalized = *before; normalized.count = amount;
    if (ok) ok = qa_inventory_validate_entry(&normalized, &normalized, error) && current(context, &data, error);
    if (ok && (data.entity_stride < 4 || p->public_inventory.acquisition_client > data.entity_stride - 4 ||
        data.client_stride < 4 || p->public_inventory.acquisition_mirror > data.client_stride - 4))
        ok = application_fail(error, QA_ERROR_FORMAT, "Original acquisition fields leave their actual Source records");
    if (ok) ok = address(context, (guest_field){GUEST_CLIENT_RECORD, 0}, &client, error) &&
        address(context, (guest_field){GUEST_ENTITY_RECORD, 0}, &entity, error) &&
        read_word(role, entity + p->public_inventory.acquisition_client, &pointer, error);
    if (ok && pointer != client)
        ok = application_fail(error, QA_ERROR_FORMAT, "Original acquisition entity does not own its located client");
    qa_qvm_source_word words[2], original[2]; size_t count = 0;
    guest_inventory_source source = {client, entity, context->slot, field.public_weapon};
    if (ok) ok = application_guest_public_inventory_acquire(&p->public_inventory,
        role->vm, &source, (int32_t)before->count, (int32_t)normalized.count, words, &count, error);
    for (size_t i = 0; ok && i < count; ++i) {
        uint32_t value;
        ok = read_word(role, words[i].offset, &value, error);
        if (ok) original[i] = (qa_qvm_source_word){words[i].offset, (int32_t)value};
    }
    if (ok) ok = current(context, &data, error);
    uint32_t after_client, after_entity;
    if (ok) ok = address(context, (guest_field){GUEST_CLIENT_RECORD, 0}, &after_client, error) &&
        address(context, (guest_field){GUEST_ENTITY_RECORD, 0}, &after_entity, error);
    if (ok && (client != after_client || entity != after_entity))
        ok = application_fail(error, QA_ERROR_NOT_FOUND, "Original acquisition Source records changed during evaluation");
    if (ok) {
        original[0].value = (int32_t)before->count;
        for (size_t i = 0; i < count; ++i) *writes |= original[i].value != words[i].value;
        if (publish && *writes) ok = qa_qvm_write_words(role->vm, words, count, error);
        if (ok && publish) ok = current(context, &data, error);
    }
    if (ok) { *after = *before; after->count = words[0].value; }
    --role->engine->calls;
    return ok;
}

static bool mutable_capacity(void *opaque, qa_item_id item)
{
    application_guest_projection *p = ((guest_projection_actor *)opaque)->projection;
    for (size_t i = 0; i < p->inventory_count; ++i)
        if (p->inventory[i].item == item) return p->inventory[i].capacity.kind == GUEST_CAPACITY_FIELD;
    return false;
}

static qa_inventory_binding inventory_binding(guest_projection_actor *context)
{
    return (qa_inventory_binding){.context = context, .count = inventory_count,
        .at = inventory_at, .write = inventory_write, .mutable_capacity = mutable_capacity,
        .checked_count = inventory_checked_count,
        .acquire = context->projection->public_inventory.acquisition_entry ? inventory_acquire : NULL};
}

bool application_guest_inventory_project(q3g_role *role, qa_actor_id actor,
    qa_item_id item, int32_t count, guest_inventory_projection_word out[2], size_t *out_count,
    qa_error *error)
{
    application_guest_projection *p = role ? role->projection : NULL;
    if (!p || !p->has_inventory || !out || !out_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original inventory projection lacks its actual source owner");
    uint32_t slot;
    if (!qa_q3_host_actor_slot(role->host, actor, &slot, error)) return false;
    guest_projection_actor context = {.projection = p, .actor = actor, .slot = slot};
    ++role->engine->calls;
    guest_inventory_projection_word words[2]; size_t written = 0;
    qa_actor_id source_actor;
    bool ok = qa_q3_host_actor(role->host, slot, false, &source_actor, error) &&
        (qa_actor_id_equal(source_actor, actor) ||
         application_fail(error, QA_ERROR_ARGUMENT, "Projected original inventory client is no longer admitted")) &&
        inventory_refresh(p, error), found = false;
    guest_inventory_field field = {0};
    for (size_t i = 0; i < p->inventory_count; ++i)
        if (p->inventory[i].item == item) { field = p->inventory[i]; found = true; break; }
    if (ok && !found) ok = application_fail(error, QA_ERROR_ARGUMENT, "Projected original item has no actual inventory storage");
    uint32_t at = 0, before = 0; int32_t capacity;
    if (ok) ok = address(&context, field.field, &at, error) && read_word(role, at, &before, error);
    if (ok && !p->inventory_public) ok = capacity_read(&context, &field, &capacity, error);
    if (ok && field.mask) {
        if (count != 0 && count != 1)
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Projected original ownership requires zero or one");
        else if (before & ~field.allowed_mask)
            ok = application_fail(error, QA_ERROR_FORMAT, "Projected original ownership contains undeclared bits");
        else {
            uint32_t after = count ? before | field.mask : before & ~field.mask;
            if (p->inventory_public || after != before)
                words[written++] = (guest_inventory_projection_word){at, (int32_t)after};
        }
    } else if (ok && (p->inventory_public || (int32_t)before != count))
        words[written++] = (guest_inventory_projection_word){at, count};
    if (ok && !p->inventory_public && field.capacity.kind == GUEST_CAPACITY_FIELD) {
        uint32_t capacity_at, capacity_before;
        ok = address(&context, field.capacity.value.field, &capacity_at, error) &&
            read_word(role, capacity_at, &capacity_before, error);
        if (ok && (int32_t)capacity_before != capacity)
            words[written++] = (guest_inventory_projection_word){capacity_at, capacity};
    }
    qa_q3_host_game_data data;
    if (ok) ok = current(&context, &data, error);
    if (ok) { memcpy(out, words, written * sizeof(*out)); *out_count = written; }
    --role->engine->calls;
    return ok;
}

bool application_guest_projection_prepare(q3g_role *role, qa_bytes primary, qa_error *error)
{
    if (role->projection) return true;
    application_guest_projection *p = calloc(1, sizeof(*p));
    if (!p) return application_fail(error, QA_ERROR_MEMORY, "Preparing qualified guest projection");
    if (!application_guest_projection_profile_read(role, primary, p, error)) {
        application_guest_projection_profile_free(p); free(p); return false;
    }
    role->projection = p; return true;
}

static bool player_state(q3g_role *role, qa_actor_id actor, qa_combat_state *out, qa_error *error)
{
    application_guest_projection *p = role->projection;
    if (p && p->located_inventory && !p->state) {
        uint32_t slot;
        qa_q3_player player;
        if (!qa_q3_host_actor_slot(role->host, actor, &slot, error) ||
            !qa_q3_host_source_player(role->host, slot, &player, error)) return false;
        qa_combat_state state = {.health = (float)player.stats[0], .mass = 200,
            .can_take_damage = player.pmType != 2,
            .armor.regular = {.kind = QA_ARMOR_Q3,
                .points = player.stats[role->engine->product == QA_Q3_TEAM_ARENA ? 4 : 3],
                .protection.q3_protection = 0.66f}};
        int32_t team = player.persistant[3];
        if (team == 1 || team == 2) {
            qa_strings *strings = qa_session_strings(role->engine->provider->application->session);
            if (!qa_strings_intern_cstr(strings, team == 1 ? "q3:1" : "q3:2", &state.team, error))
                return false;
        }
        *out = state;
        return true;
    }
    if (!p || !p->state || !role->artifact || p->state != role->artifact->combat_profile)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Guest player state requires a qualified original combat interface");
    application_q3_combat_actor source;
    if (!application_q3_combat_actor_read(role, p->state, actor, &source, error)) return false;
    guest_projection_actor context = {.projection = p, .actor = actor, .slot = source.slot};
    uint32_t entity;
    if (!address(&context, (guest_field){GUEST_ENTITY_RECORD, 0}, &entity, error)) return false;
    if (entity != source.entity)
        return application_fail(error, QA_ERROR_FORMAT, "Guest player state changed its physical source entity");
    bool live;
    if (!application_q3_combat_actor_live(&source, &live, error)) return false;
    if (!live) return application_fail(error, QA_ERROR_ARGUMENT, "Guest player source entity has not been admitted");
    qa_combat_state state;
    if (!application_q3_combat_state_read(&source, &state, error)) return false;
    if (state.armor.regular.points < 0)
        return application_fail(error, QA_ERROR_FORMAT, "Guest source armor points are negative");
    qa_q3_host_game_data data;
    if (!current(&context, &data, error)) return false;
    *out = state;
    return true;
}

bool application_guest_player_state(application_provider *provider, qa_actor_id actor,
                                    qa_combat_state *out, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !engine->game || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest player observation requires a game owner");
    ++engine->calls;
    bool ok = player_state(engine->game, actor, out, error);
    --engine->calls; return ok;
}

bool application_guest_projection_admit(q3g_role *role, qa_actor_id actor, qa_error *error)
{
    application_guest_projection *p = role->projection;
    qa_application *app = role->engine->provider->application;
    uint32_t slot;
    if (!qa_q3_host_actor_slot(role->host, actor, &slot, error)) return false;
    qa_q3_host_game_data data;
    if (!qa_q3_host_game_data_read(role->host, &data))
        return application_fail(error, QA_ERROR_FORMAT, "Guest admission requires located source records");
    if (slot >= data.client_count || application_provider_for(app, actor, QA_ROLE_INVENTORY, NULL) != role->engine->provider)
        return true;
    if (!p || !p->has_inventory)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Guest inventory requires a qualified original storage interface");
    if (!inventory_refresh(p, error)) return false;
    guest_projection_actor *context;
    for (context = p->actors; context; context = context->next)
        if (qa_actor_id_equal(context->actor, actor)) break;
    if (!context) {
        context = calloc(1, sizeof(*context));
        if (!context) return application_fail(error, QA_ERROR_MEMORY, "Retaining guest actor projection ownership");
        *context = (guest_projection_actor){.next = p->actors, .projection = p, .actor = actor, .slot = slot};
        p->actors = context;
    }
    if (!context->inventory_bound) {
        qa_inventory_binding binding = inventory_binding(context);
        if (!qa_inventory_adopt_primary(app->inventory, actor, &binding, &context->inventory_lease, error)) return false;
        context->inventory_bound = true;
        context->inventory_prepared = false;
    }
    return true;
}

bool application_guest_projection_detach(q3g_role *role, qa_actor_id actor, qa_error *error)
{
    application_guest_projection *p = role->projection;
    if (!p) return true;
    qa_application *app = role->engine->provider->application;
    for (guest_projection_actor *context = p->actors; context; context = context->next) {
        if (!qa_actor_id_equal(context->actor, actor) || !context->inventory_bound) continue;
        bool unpublished = context->inventory_prepared &&
            !qa_inventory_primary_current(app->inventory, context->inventory_lease, context);
        if (!unpublished && qa_actors_get(qa_session_actors(app->session), actor) &&
            !qa_inventory_detach_primary(app->inventory, context->inventory_lease, context, error)) return false;
        context->inventory_bound = false;
        context->inventory_prepared = false;
    }
    return true;
}

bool application_guest_projection_inventory_binding(q3g_role *role,
    qa_actor_id actor, uint64_t serial, qa_inventory_binding *out, qa_error *error)
{
    application_guest_projection *projection = role ? role->projection : NULL;
    if (!projection || !projection->has_inventory || !out || !serial)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Restored guest primary inventory requires its actual source declaration");
    if (!inventory_refresh(projection, error)) return false;
    uint32_t slot;
    if (!qa_q3_host_actor_slot(role->host, actor, &slot, error))
        return false;
    guest_projection_actor *context = projection->actors;
    while (context && !qa_actor_id_equal(context->actor, actor))
        context = context->next;
    guest_projection_actor temporary = {.projection = projection, .actor = actor, .slot = slot};
    qa_q3_host_game_data data;
    if (!current(&temporary, &data, error))
        return false;
    if (context && (context->slot != slot ||
        (context->inventory_bound && context->inventory_lease.serial != serial)))
        return application_fail(error, QA_ERROR_FORMAT,
                                "Restored guest inventory differs from its source lease");
    if (!context) {
        context = calloc(1, sizeof(*context));
        if (!context)
            return application_fail(error, QA_ERROR_MEMORY,
                                    "Retaining restored guest inventory callback context");
        *context = temporary;
        context->next = projection->actors;
        projection->actors = context;
    }
    context->inventory_lease = (qa_inventory_lease){actor, serial};
    if (!context->inventory_bound)
        context->inventory_prepared = true;
    context->inventory_bound = true;
    *out = inventory_binding(context);
    return true;
}

bool application_guest_inventory_restore_finish(application_provider *provider, qa_error *error)
{
    if(provider->kind==APPLICATION_PROVIDER_QC)
        return application_qc_combat_restore_attach(provider->state.qc.engine,error) &&
            application_qc_protection_restore_attach(provider->state.qc.engine,error) &&
            application_qc_items_restore_finish(provider,error) &&
            application_qc_pickups_ready(provider->state.qc.engine,error);
    if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine)
        return application_native_q2_restore_finish(provider, error);
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine)
        return true;
    if (engine->calls || !qa_session_safe(provider->application->session) ||
        !qa_world_idle(engine->world))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Guest inventory finish requires idle source owners");
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->projection)
            for (guest_projection_actor *context = role->projection->actors; context; context = context->next)
                if (context->inventory_prepared && (!context->inventory_bound ||
                    !qa_inventory_primary_current(provider->application->inventory,
                                                   context->inventory_lease, context)))
                    return application_fail(error, QA_ERROR_FORMAT,
                                            "Restored guest primary inventory was not published");
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->projection)
            for (guest_projection_actor *context = role->projection->actors; context; context = context->next)
                context->inventory_prepared = false;
    return true;
}

bool application_guest_projection_close(q3g_role *role, qa_error *error)
{
    application_guest_projection *p = role->projection;
    if (!p) return true;
    for (guest_projection_actor *context = p->actors; context; context = context->next)
        if (!application_guest_projection_detach(role, context->actor, error)) return false;
    while (p->actors) { guest_projection_actor *next = p->actors->next; free(p->actors); p->actors = next; }
    application_guest_projection_profile_free(p); free(p); role->projection = NULL; return true;
}
