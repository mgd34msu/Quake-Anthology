#include "guest_native_q2_private.h"
#include "guest_native_q2_combat_state.h"
#include "qa/game_q2.h"
#include "qa/native_host_q2_wire.h"
#include <math.h>

static bool word(const qa_json_document *doc, qa_json_id object, const char *name,
    uint32_t *out, qa_error *error)
{
    uint64_t number;
    if (!qa_json_u64(doc, qa_json_get(doc, object, name), &number, error)) return false;
    if (number > UINT32_MAX) {
        application_fail(error, QA_ERROR_FORMAT, "Native combat field exceeds its source word");
        return false;
    }
    *out = (uint32_t)number; return true;
}
static bool range(uint32_t offset, uint64_t bytes, uint32_t extent, qa_error *error)
{
    return (offset <= extent && bytes <= extent - offset) ||
        application_fail(error, QA_ERROR_FORMAT, "Native combat field exceeds its qualified source record");
}
static bool mask(const qa_json_document *doc, qa_json_id object, const char *name,
    bool kex, uint64_t *out, qa_error *error)
{
    if (!qa_json_u64(doc, qa_json_get(doc, object, name), out, error)) return false;
    return (*out && *out <= (kex ? UINT64_C(9007199254740991) : UINT32_MAX)) ||
        application_fail(error, QA_ERROR_FORMAT, "Native combat mask exceeds its original declared source range");
}
static uint32_t storage_bytes(const qa_json_document *doc, qa_json_id id, uint8_t pointers)
{
    static const char *const names[] = {"int8", "uint8", "int16", "uint16", "int32", "uint32", "float32", "int64", "uint64", "float64", "pointer"};
    static const uint8_t sizes[] = {1, 1, 2, 2, 4, 4, 4, 8, 8, 8, 0};
    for (size_t i = 0; i < sizeof(sizes); ++i)
        if (qa_json_string_equal(doc, id, names[i])) return sizes[i] ? sizes[i] : pointers;
    return 0;
}
static bool layout_validate(const qa_json_document *doc, qa_json_id layout,
    uint8_t pointers, uint32_t *extent, qa_error *error)
{
    uint32_t actual, alignment;
    if (!word(doc, layout, "pointerBytes", &actual, error) ||
        !word(doc, layout, "byteLength", extent, error) ||
        !word(doc, layout, "alignment", &alignment, error)) return false;
    qa_json_id fields = qa_json_get(doc, layout, "fields");
    if (actual != pointers || !*extent || *extent > 1048576 || !alignment ||
        alignment > 64 || (alignment & (alignment - 1)) ||
        !qa_json_string_equal(doc, qa_json_get(doc, layout, "byteOrder"), "little-endian") ||
        qa_json_type(doc, fields) != QA_JSON_ARRAY || qa_json_size(doc, fields) > 4096)
        return application_fail(error, QA_ERROR_FORMAT, "Native combat layout differs from its original source ABI");
    for (size_t i = 0; i < qa_json_size(doc, fields); ++i) {
        qa_json_id row = qa_json_at(doc, fields, i); uint32_t offset, count;
        uint32_t bytes = storage_bytes(doc, qa_json_get(doc, row, "storage"), pointers);
        if (!bytes || !word(doc, row, "byteOffset", &offset, error) ||
            !word(doc, row, "count", &count, error) || !count ||
            !range(offset, (uint64_t)bytes * count, *extent, error)) return false;
        qa_buffer name = {0};
        if (!qa_json_string(doc, qa_json_get(doc, row, "name"), &name, error)) return false;
        bool ok = name.size && !memchr(name.data, 0, name.size);
        for (size_t j = 0; ok && j < i; ++j) {
            qa_json_id prior = qa_json_at(doc, fields, j); uint32_t prior_offset, prior_count;
            uint32_t prior_bytes = storage_bytes(doc, qa_json_get(doc, prior, "storage"), pointers);
            ok = word(doc, prior, "byteOffset", &prior_offset, error) &&
                word(doc, prior, "count", &prior_count, error);
            if (ok && (qa_json_string_equal(doc, qa_json_get(doc, prior, "name"), (char *)name.data) ||
                ((uint64_t)offset < (uint64_t)prior_offset + (uint64_t)prior_count * prior_bytes &&
                 (uint64_t)prior_offset < (uint64_t)offset + (uint64_t)count * bytes))) ok = false;
        }
        qa_buffer_free(&name);
        if (!ok) return application_fail(error, QA_ERROR_FORMAT, "Native combat layout repeats or overlaps source fields");
    }
    return true;
}
static bool field(const qa_json_document *doc, qa_json_id layout, const char *name,
    const char *storage, uint32_t count, uint32_t *out, bool optional, qa_error *error)
{
    qa_json_id fields = qa_json_get(doc, layout, "fields");
    for (size_t i = 0; i < qa_json_size(doc, fields); ++i) {
        qa_json_id row = qa_json_at(doc, fields, i); uint32_t actual;
        if (!qa_json_string_equal(doc, qa_json_get(doc, row, "name"), name)) continue;
        if (!qa_json_string_equal(doc, qa_json_get(doc, row, "storage"), storage) ||
            !word(doc, row, "count", &actual, error) || actual != count ||
            !word(doc, row, "byteOffset", out, error))
            return application_fail(error, QA_ERROR_FORMAT, "Native combat field changes its original source type");
        return true;
    }
    if (optional) { *out = UINT32_MAX; return true; }
    return application_fail(error, QA_ERROR_FORMAT, "Native combat source field is absent");
}
static bool item(const application_q2_combat_profile *p, qa_json_id object,
    const char *name, qa_item_id *out, qa_error *error)
{
    qa_buffer text = {0}; bool ok = qa_json_string(p->document,
        qa_json_get(p->document, object, name), &text, error);
    if (ok && (!text.size || memchr(text.data, 0, text.size) || !memchr(text.data, ':', text.size)))
        ok = application_fail(error, QA_ERROR_FORMAT, "Native armor identity is not namespaced");
    if (ok) ok = qa_strings_intern_cstr(qa_session_strings(p->engine->provider->application->session),
        (char *)text.data, out, error);
    qa_buffer_free(&text); return ok;
}
bool application_q2_combat_profile_read(struct application_native_q2 *engine,
    application_q2_combat_profile *p, qa_error *error)
{
    if (!engine || !p || !engine->declaration || engine->profile == QA_NATIVE_Q2_CGAME_API2023)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native primary combat requires its original game declaration");
    memset(p, 0, sizeof(*p)); p->engine = engine;
    p->kex = engine->profile == QA_NATIVE_Q2_GAME_API2023;
    p->target = qa_native_module_describe(engine->provider->state.native.module).image.target;
    if ((p->kex && p->target.pointer_bytes != 8) || (!p->kex && p->target.pointer_bytes != 4))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native primary combat requires its qualified classic32 or KEX64 artifact");
    if (!qa_json_parse(qa_native_declaration_primary(engine->declaration), &p->document, error)) return false;
    const qa_json_document *doc = p->document;
    qa_json_id root = qa_json_root(doc); p->world = qa_json_get(doc, root, "world");
    qa_json_id client = qa_json_get(doc, p->world, "client"),
        entries = qa_json_get(doc, p->world, "entries"), flags = qa_json_get(doc, p->world, "flags"),
        armor = qa_json_get(doc, p->world, "armor"), calls = qa_json_get(doc, p->world, "calls");
    if (!word(doc, client, "inventoryCount", &p->inventory_count, error) ||
        !p->inventory_count || p->inventory_count > 65536 ||
        !word(doc, entries, "damage", &p->damage_entry, error) ||
        !word(doc, entries, "powerArmor", &p->power_entry, error) ||
        !mask(doc, flags, p->kex ? "godmode" : "invulnerable", p->kex, &p->godmode, error) ||
        !mask(doc, flags, "noKnockback", p->kex, &p->no_knockback, error) ||
        !mask(doc, flags, "powerArmor", p->kex, &p->power_armor, error)) return false;
    static const char *const call_names[] = {"damage", "regularArmor", "powerArmor", "pain", "death", "processPain"};
    for (size_t i = 0; i < 6; ++i) {
        if ((p->kex && i == APPLICATION_Q2_REGULAR_ARMOR) || (!p->kex && i == APPLICATION_Q2_PROCESS_PAIN)) continue;
        if (!application_q2_call_read(doc, qa_json_get(doc, calls, call_names[i]),
            (application_q2_call_operation)i, p->target, &p->calls[i], error)) return false;
    }
    qa_json_id regular = qa_json_get(doc, armor, "regular");
    p->regular_count = qa_json_size(doc, regular);
    if (qa_json_type(doc, regular) != QA_JSON_ARRAY || !p->regular_count || p->regular_count > p->inventory_count)
        return application_fail(error, QA_ERROR_FORMAT, "Native regular armor requires its original ordered roster");
    p->regular = calloc(p->regular_count, sizeof(*p->regular));
    if (!p->regular) return application_fail(error, QA_ERROR_MEMORY, "Retaining native armor priorities");
    if (!p->kex) {
        qa_json_id fields = qa_json_get(doc, p->world, "fields"), globals = qa_json_get(doc, p->world, "globals"),
            items = qa_json_get(doc, p->world, "items"), item_fields = qa_json_get(doc, p->world, "itemFields"),
            info = qa_json_get(doc, p->world, "armorInfo"), teams = qa_json_get(doc, p->world, "teams");
        p->client_pointer = 84; p->inuse = 88; p->svflags = 184; p->team = UINT32_MAX;
#define READ(object, name, member) if (!word(doc, object, name, &p->member, error)) return false
        READ(p->world, "entityBytes", entity_bytes);
        READ(qa_json_get(doc, qa_json_get(doc, root, "weapons"), "client"), "byteLength", client_bytes);
        READ(fields, "health", health); READ(fields, "damageable", damageable); READ(fields, "flags", flags);
        READ(fields, "mass", mass); READ(fields, "velocity", velocity); READ(fields, "pain", pain); READ(fields, "die", die);
        READ(client, "inventory", inventory); READ(client, "invincibleFrame", invincible);
        READ(client, "userinfo", userinfo); READ(client, "userinfoBytes", userinfo_bytes); READ(client, "viewAngles", view_angles);
        READ(entries, "regularArmor", regular_entry); READ(globals, "levelFrame", time);
        READ(globals, "itemList", item_table); READ(globals, "itemBytes", item_stride);
        READ(item_fields, "className", item_classname); READ(item_fields, "armorInfo", item_info);
        READ(info, "normalProtection", normal); READ(info, "energyProtection", energy);
        READ(items, "screen", screen); READ(items, "shield", shield); READ(items, "cells", cells);
        READ(armor, "empty", empty); READ(teams, "model", model_team); READ(teams, "skin", skin_team);
        for (size_t i = 0; i < p->regular_count; ++i) {
            uint64_t index;
            if (!qa_json_u64(doc, qa_json_at(doc, regular, i), &index, error) || index >= p->inventory_count)
                return application_fail(error, QA_ERROR_FORMAT, "Native armor tier exceeds source inventory");
            p->regular[i].index = (uint32_t)index;
        }
        uint32_t offsets[] = {p->health, p->damageable, p->flags, p->mass, p->pain, p->die};
        for (size_t i = 0; i < sizeof(offsets) / sizeof(*offsets); ++i) {
            if (offsets[i] % 4 || !range(offsets[i], 4, p->entity_bytes, error)) return false;
            if (offsets[i] < 260 || ((uint64_t)offsets[i] < (uint64_t)p->velocity + 12 &&
                (uint64_t)p->velocity < (uint64_t)offsets[i] + 4))
                return application_fail(error, QA_ERROR_FORMAT, "Native classic combat aliases its public prefix or source velocity");
            for (size_t j = 0; j < i; ++j)
                if (offsets[i] == offsets[j]) return application_fail(error, QA_ERROR_FORMAT, "Native classic combat fields overlap");
        }
        qa_json_id game = qa_json_get(doc, p->world, "game");
        if (!qa_json_string_equal(doc, game, "base") && !qa_json_string_equal(doc, game, "xatrix") &&
            !qa_json_string_equal(doc, game, "rogue") && !qa_json_string_equal(doc, game, "ctf"))
            return application_fail(error, QA_ERROR_FORMAT, "Native classic combat requires its exact original product cause roster");
        if (p->entity_bytes < 260 || !range(p->velocity, 12, p->entity_bytes, error) ||
            !range(p->invincible, 4, p->client_bytes, error) || !p->userinfo_bytes ||
            !range(p->userinfo, p->userinfo_bytes, p->client_bytes, error) ||
            !range(p->view_angles, 12, p->client_bytes, error) ||
            !range(p->item_classname, 4, p->item_stride, error) ||
            !range(p->item_info, 4, p->item_stride, error) ||
            p->screen >= p->inventory_count || p->shield >= p->inventory_count || p->cells >= p->inventory_count)
            return application_fail(error, QA_ERROR_FORMAT, "Native classic combat metadata exceeds its records");
    } else {
        qa_json_id edict = qa_json_get(doc, p->world, "edict"), layout = qa_json_get(doc, client, "layout"),
            monster = qa_json_get(doc, p->world, "monster");
        if (!layout_validate(doc, edict, 8, &p->entity_bytes, error) ||
            !layout_validate(doc, layout, 8, &p->client_bytes, error)) return false;
#define FIELD(layout, name, storage, count, member) if (!field(doc, layout, name, storage, count, &p->member, false, error)) return false
        FIELD(edict, "shared.client", "pointer", 1, client_pointer);
        FIELD(edict, "shared.inuse", "uint8", 1, inuse); FIELD(edict, "shared.svflags", "uint32", 1, svflags);
        FIELD(edict, "health", "int32", 1, health); FIELD(edict, "takedamage", "uint8", 1, damageable);
        FIELD(edict, "flags", "uint64", 1, flags); FIELD(edict, "mass", "int32", 1, mass);
        FIELD(edict, "velocity", "float32", 3, velocity); FIELD(edict, "pain.value", "pointer", 1, pain);
        FIELD(edict, "die.value", "pointer", 1, die); FIELD(edict, "spawn_count", "int32", 1, generation);
        FIELD(layout, "pers.inventory", "int32", p->inventory_count, inventory);
        FIELD(layout, "invincible_time", "int64", 1, invincible); FIELD(layout, "v_angle", "float32", 3, view_angles);
#undef FIELD
        if (!field(doc, layout, "resp.ctf_team", "int32", 1, &p->team, true, error)) return false;
        if (p->client_pointer != 120 || p->inuse != 1376 || p->svflags != 1392 || p->entity_bytes < 1472)
            return application_fail(error, QA_ERROR_FORMAT, "Native combat changes its API2023 public entity prefix");
        READ(entries, "time", time); READ(entries, "processPain", process_entry);
        READ(armor, "table", item_table); READ(armor, "stride", item_stride);
        READ(armor, "normal", normal); READ(armor, "energy", energy); READ(armor, "cellsIndex", cells);
        READ(monster, "attacker", monster_attacker); READ(monster, "inflictor", monster_inflictor);
        READ(monster, "blood", monster_blood); READ(monster, "knockback", monster_knockback);
        READ(monster, "point", monster_point); READ(monster, "mod", monster_mod); READ(monster, "invincibleTime", monster_invincible);
        uint32_t offsets[] = {p->monster_attacker, p->monster_inflictor, p->monster_blood,
            p->monster_knockback, p->monster_point, p->monster_mod, p->monster_invincible};
        uint32_t sizes[] = {8, 8, 4, 4, 12, 3, 8};
        for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i)
            if (!range(offsets[i], sizes[i], p->entity_bytes, error)) return false;
        for (size_t i = 0; i < p->regular_count; ++i) {
            qa_buffer text = {0};
            bool ok = qa_json_string(doc, qa_json_at(doc, regular, i), &text, error);
            if (ok && (!text.size || memchr(text.data, 0, text.size) || !memchr(text.data, ':', text.size)))
                ok = application_fail(error, QA_ERROR_FORMAT, "Native armor tier is not namespaced");
            if (ok) ok = qa_strings_intern_cstr(qa_session_strings(engine->provider->application->session),
                (char *)text.data, &p->regular[i].item, error);
            qa_buffer_free(&text); if (!ok) return false;
        }
        qa_item_id unused;
        if (!item(p, armor, "empty", &unused, error) || !item(p, armor, "screen", &unused, error) ||
            !item(p, armor, "shield", &unused, error) || !item(p, armor, "cells", &unused, error)) return false;
    }
#undef READ
    qa_json_id inventory = qa_json_get(doc, root, "inventory"); uint32_t actual_pointer, actual_offset, actual_count;
    if (!word(doc, inventory, "client", &actual_pointer, error) ||
        !word(doc, inventory, "inventory", &actual_offset, error) ||
        !word(doc, inventory, "count", &actual_count, error) ||
        actual_pointer != p->client_pointer || actual_offset != p->inventory || actual_count != p->inventory_count)
        return application_fail(error, QA_ERROR_FORMAT, "Native combat and inventory disagree about their original shared counters");
    if (!range(p->inventory, (uint64_t)p->inventory_count * 4, p->client_bytes, error) ||
        p->item_stride < p->target.pointer_bytes || p->item_stride > 65536 ||
        p->normal > 65532 || p->energy > 65532 || p->normal == p->energy ||
        !p->damage_entry || !p->power_entry || !p->time)
        return application_fail(error, QA_ERROR_FORMAT, "Native combat metadata lacks bounded source storage");
    return true;
}
bool application_q2_combat_profile_resolve(application_q2_combat_profile *p, qa_error *error)
{
    if (p->resolved) return true;
    qa_json_id armor = qa_json_get(p->document, p->world, "armor");
    if (p->kex) {
        qa_item_id identity;
        if (!item(p, armor, "empty", &identity, error) ||
            !application_native_q2_inventory_index(p->engine, identity, &p->empty, error) ||
            !item(p, armor, "screen", &identity, error) ||
            !application_native_q2_inventory_index(p->engine, identity, &p->screen, error) ||
            !item(p, armor, "shield", &identity, error) ||
            !application_native_q2_inventory_index(p->engine, identity, &p->shield, error) ||
            !item(p, armor, "cells", &identity, error)) return false;
        uint32_t actual;
        if (!application_native_q2_inventory_index(p->engine, identity, &actual, error) || actual != p->cells)
            return application_fail(error, QA_ERROR_FORMAT, "Native source power armor and ammunition disagree about their cell counter");
    }
    bool found_empty = false;
    for (size_t i = 0; i < p->regular_count; ++i) {
        application_q2_armor_row *row = &p->regular[i];
        if (p->kex) {
            if (!application_native_q2_inventory_index(p->engine, row->item, &row->index, error)) return false;
        } else if (!application_native_q2_inventory_item(p->engine, row->index, &row->item, error)) return false;
        if (row->index == p->empty) found_empty = true;
        for (size_t j = 0; j < i; ++j)
            if (p->regular[j].index == row->index || p->regular[j].item == row->item)
                return application_fail(error, QA_ERROR_FORMAT, "Native armor tiers repeat an original source counter");
    }
    if (!found_empty) return application_fail(error, QA_ERROR_FORMAT, "Native empty armor tier is absent from its source priorities");
    p->resolved = true; return true;
}
void application_q2_combat_profile_free(application_q2_combat_profile *p)
{
    if (!p) return;
    for (size_t i = 0; i < 6; ++i) application_q2_call_free(&p->calls[i]);
    qa_json_destroy(p->document); free(p->regular); memset(p, 0, sizeof(*p));
}
static qa_native_instance *native(const application_q2_combat_profile *p)
{ return qa_native_host_instance(p->engine->provider->state.native.host); }
static bool integer_read(const application_q2_combat_profile *p, qa_native_address at,
    size_t bytes, int64_t *out, qa_error *error)
{
    uint8_t value[8];
    if (!qa_native_read(native(p), at, value, bytes, error)) return false;
    *out = bytes == 1 ? value[0] : bytes == 4 ? qa_load_i32le(value) : (int64_t)qa_load_u64le(value);
    return true;
}
static bool pointer_read(const application_q2_combat_profile *p, qa_native_address at,
    qa_native_address *out, qa_error *error)
{
    uint8_t bytes[8];
    if (!qa_native_read(native(p), at, bytes, p->target.pointer_bytes, error)) return false;
    *out = p->target.pointer_bytes == 4 ? qa_load_u32le(bytes) : qa_load_u64le(bytes); return true;
}
static bool integer_write(const application_q2_combat_profile *p, qa_native_address at,
    double value, qa_error *error)
{
    if (!isfinite(value) || trunc(value) != value || value < INT32_MIN || value > INT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native combat store requires an exact int32 source value");
    uint8_t bytes[4]; qa_store_u32le(bytes, (uint32_t)(int32_t)value);
    return qa_native_write(native(p), at, (qa_bytes){bytes, sizeof(bytes)}, error);
}
bool application_q2_combat_actor_init(application_q2_combat_profile *p, uint32_t slot,
    qa_actor_id actor, bool admitting, application_q2_combat_actor *out, qa_error *error)
{
    if (!p || !out || !qa_actors_get(qa_session_actors(p->engine->provider->application->session), actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native combat actor has no live full canonical generation");
    qa_native_entity_table table; qa_native_address address; int64_t generation = 0;
    if (!qa_native_entity_table_get(native(p), &table, error) ||
        (p->kex ? table.stride < p->entity_bytes : table.stride != p->entity_bytes) ||
        !qa_native_entity_address(native(p), slot, &address, error) ||
        (p->kex && !integer_read(p, address + p->generation, 4, &generation, error)))
        return application_fail(error, QA_ERROR_FORMAT, "Native combat source table differs from its qualified edict extent");
    *out = (application_q2_combat_actor){p, actor, address, slot, (int32_t)generation, admitting};
    return application_q2_combat_actor_valid(out, error);
}
bool application_q2_combat_actor_valid(application_q2_combat_actor *a, qa_error *error)
{
    application_q2_combat_profile *p = a->profile;
    if (!qa_actors_get(qa_session_actors(p->engine->provider->application->session), a->actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native combat full actor generation retired");
    qa_native_address address; qa_native_slot_binding binding; int64_t inuse, generation;
    if (!qa_native_entity_address(native(p), a->slot, &address, error) || address != a->address ||
        !qa_native_slot(native(p), a->slot, &binding, error) ||
        (!a->admitting && (binding.kind == QA_NATIVE_SLOT_FREE || !qa_actor_id_equal(binding.actor, a->actor))))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native combat source address or generation changed");
    struct application_native_q2 *engine = p->engine;
    bool final_disconnect = !a->admitting && a->slot && a->slot < 257 &&
        engine->disconnect_client == a->slot && engine->clients[a->slot].disconnect_started &&
        engine->clients[a->slot].connected && qa_actor_id_equal(engine->clients[a->slot].actor, a->actor) &&
        binding.kind == QA_NATIVE_SLOT_BORROWED && binding.owner == engine->provider->owner &&
        binding.source_slot == a->slot && qa_native_host_destroy_ready(engine->provider->state.native.host);
    if (!integer_read(p, address + p->inuse, p->kex ? 1 : 4, &inuse, error) || (!inuse && !final_disconnect) ||
        (p->kex && (!integer_read(p, address + p->generation, 4, &generation, error) || generation != a->generation)))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native combat source address or generation changed");
    return true;
}
static bool count(const application_q2_combat_profile *p, qa_native_address client,
    uint32_t index, double *out, qa_error *error)
{
    int64_t value;
    if (index >= p->inventory_count || !integer_read(p, client + p->inventory + (uint64_t)index * 4, 4, &value, error)) return false;
    *out = (double)value; return true;
}
static bool classic_skin(const char *info, char *out, size_t capacity, qa_error *error)
{
    out[0] = 0;
    const char *cursor = info + (*info == '\\');
    while (*cursor) {
        const char *separator = strchr(cursor, '\\');
        if (!separator) return true;
        const char *value = separator + 1, *end = strchr(value, '\\');
        size_t bytes = end ? (size_t)(end - value) : strlen(value);
        if ((size_t)(separator - cursor) == 4 && !memcmp(cursor, "skin", 4)) {
            if (bytes >= capacity) return application_fail(error, QA_ERROR_FORMAT, "Native classic skin exceeds its bounded source team value");
            memcpy(out, value, bytes); out[bytes] = 0; return true;
        }
        if (!end) return true;
        cursor = end + 1;
    }
    return true;
}
static bool definition(application_q2_combat_profile *p, uint32_t index,
    qa_regular_armor *out, qa_error *error)
{
    qa_native_address record, info; uint8_t bytes[4];
    if (!qa_native_rva(native(p), (uint64_t)p->item_table + (uint64_t)index * p->item_stride +
        (p->kex ? 0 : p->item_info), p->target.pointer_bytes, &record, error) ||
        !pointer_read(p, record, &info, error) ||
        !application_native_q2_inventory_item(p->engine, index, &out->item, error)) return false;
    out->kind = QA_ARMOR_Q2;
    if (!info) {
        if (!p->kex) return application_fail(error, QA_ERROR_FORMAT, "Native classic armor lacks its original protection record");
        out->protection.q2.normal = out->protection.q2.energy = 0; return true;
    }
    if (!qa_native_read(native(p), info + p->normal, bytes, 4, error)) return false;
    out->protection.q2.normal = qa_load_f32le(bytes);
    if (!qa_native_read(native(p), info + p->energy, bytes, 4, error)) return false;
    out->protection.q2.energy = qa_load_f32le(bytes);
    return qa_armor_validate(&(qa_armor){.regular = *out}, error);
}
bool application_q2_combat_armor_read(application_q2_combat_actor *a, qa_armor *out, qa_error *error)
{
    if (!application_q2_combat_actor_valid(a, error) || !application_q2_combat_profile_resolve(a->profile, error)) return false;
    application_q2_combat_profile *p = a->profile; qa_native_address client; int64_t flags;
    *out = (qa_armor){0};
    if (!pointer_read(p, a->address + p->client_pointer, &client, error)) return false;
    if (!client) return true;
    for (size_t i = 0; i < p->regular_count; ++i) {
        double points;
        if (!count(p, client, p->regular[i].index, &points, error)) return false;
        if (points <= 0) continue;
        if (!definition(p, p->regular[i].index, &out->regular, error)) return false;
        out->regular.points = points; break;
    }
    if (!integer_read(p, a->address + p->flags, p->kex ? 8 : 4, &flags, error)) return false;
    if ((uint64_t)flags & p->power_armor) {
        double shield, screen;
        if (!count(p, client, p->shield, &shield, error) || !count(p, client, p->screen, &screen, error)) return false;
        out->powered.kind = shield > 0 ? QA_POWER_SHIELD : screen > 0 ? QA_POWER_SCREEN : QA_POWER_NONE;
        if (out->powered.kind != QA_POWER_NONE && !count(p, client, p->cells, &out->powered.cells, error)) return false;
    }
    if (out->powered.kind != QA_POWER_NONE) {
        out->powered.source_kind = QA_POWER_SOURCE_Q2;
        out->powered.source_owner = p->engine->provider->owner;
        out->powered.source_edition = p->kex ? QA_Q2_POWER_ARMOR_RERELEASE : QA_Q2_POWER_ARMOR_CLASSIC;
    }
    return qa_armor_validate(out, error);
}
bool application_q2_combat_armor_validate(void *opaque, const qa_armor *armor, qa_error *error)
{
    application_q2_combat_actor *a = opaque; qa_armor actual;
    if (!armor || !qa_armor_validate(armor, error) || !application_q2_combat_armor_read(a, &actual, error)) return false;
    if ((armor->regular.kind != QA_ARMOR_NONE && armor->regular.kind != QA_ARMOR_Q2) ||
        armor->powered.kind != actual.powered.kind ||
        armor->powered.source_kind != actual.powered.source_kind ||
        armor->powered.source_owner != actual.powered.source_owner ||
        armor->powered.source_edition != actual.powered.source_edition)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native armor requires its original tier and equipment activation");
    if (armor->regular.kind == QA_ARMOR_Q2) {
        bool found = false;
        for (size_t i = 0; i < a->profile->regular_count; ++i) if (a->profile->regular[i].item == armor->regular.item) {
            qa_regular_armor original = {0};
            if (!definition(a->profile, a->profile->regular[i].index, &original, error)) return false;
            found = original.protection.q2.normal == armor->regular.protection.q2.normal &&
                original.protection.q2.energy == armor->regular.protection.q2.energy;
        }
        if (!found) return application_fail(error, QA_ERROR_UNSUPPORTED, "Native armor changes its original tier protection");
    }
    double numbers[] = {armor->regular.points, armor->powered.cells};
    for (size_t i = 0; i < sizeof(numbers) / sizeof(*numbers); ++i)
        if (trunc(numbers[i]) != numbers[i] || numbers[i] < INT32_MIN || numbers[i] > INT32_MAX)
            return application_fail(error, QA_ERROR_ARGUMENT, "Native armor cannot encode fractional or overflowing counters");
    return true;
}
bool application_q2_combat_armor_write(void *opaque, const qa_armor *armor, qa_error *error)
{
    application_q2_combat_actor *a = opaque; application_q2_combat_profile *p = a->profile;
    qa_armor actual; qa_native_address client;
    if (!application_q2_combat_armor_validate(a, armor, error) ||
        !application_q2_combat_armor_read(a, &actual, error) ||
        !pointer_read(p, a->address + p->client_pointer, &client, error)) return false;
    if (!client) return (armor->regular.kind == QA_ARMOR_NONE && armor->powered.kind == QA_POWER_NONE) ||
        application_fail(error, QA_ERROR_UNSUPPORTED, "Native non-client has no original armor inventory");
    for (size_t i = 0; i < p->regular_count; ++i) {
        application_q2_armor_row row = p->regular[i];
        if (armor->regular.kind == QA_ARMOR_Q2 && actual.regular.kind == QA_ARMOR_Q2 &&
            armor->regular.item == actual.regular.item && row.item != actual.regular.item) continue;
        double value = armor->regular.kind == QA_ARMOR_Q2 && armor->regular.item == row.item ? armor->regular.points : 0;
        double before;
        if (!count(p, client, row.index, &before, error) ||
            (before != value && !integer_write(p, client + p->inventory + (uint64_t)row.index * 4, value, error))) return false;
    }
    return armor->powered.kind == QA_POWER_NONE ||
        integer_write(p, client + p->inventory + (uint64_t)p->cells * 4, armor->powered.cells, error);
}
bool application_q2_combat_empty_armor(void *opaque, double points, qa_regular_armor *out,
    bool *selected, qa_error *error)
{
    application_q2_combat_actor *a = opaque;
    if (!application_q2_combat_actor_valid(a, error) || !application_q2_combat_profile_resolve(a->profile, error)) return false;
    *out = (qa_regular_armor){.points = points};
    if (!definition(a->profile, a->profile->empty, out, error)) return false;
    *selected = true; return true;
}
bool application_q2_combat_normalize_armor(void *opaque, const qa_armor *input,
    qa_armor *out, qa_error *error)
{
    qa_armor actual;
    if (!input || !out || !application_q2_combat_armor_read(opaque, &actual, error)) return false;
    *out = *input;
    if (actual.regular.kind == QA_ARMOR_NONE && input->regular.kind == QA_ARMOR_Q2 &&
        input->regular.points == 0 && input->regular.protection.q2.normal == 0 &&
        input->regular.protection.q2.energy == 0 && input->powered.kind != QA_POWER_NONE &&
        qa_powered_armor_equal(input->powered, actual.powered)) {
        const char *name = qa_strings_cstr(qa_session_strings(((application_q2_combat_actor *)opaque)->profile->engine->provider->application->session), input->regular.item);
        if (name && !strcmp(name, "q2:none")) out->regular = (qa_regular_armor){0};
    }
    return true;
}
bool application_q2_combat_state_read(void *opaque, qa_combat_state *out, qa_error *error)
{
    application_q2_combat_actor *a = opaque; application_q2_combat_profile *p = a->profile;
    int64_t health, mass, damageable, flags, now, until = 0; qa_native_address client, time;
    if (!application_q2_combat_armor_read(a, &out->armor, error) ||
        !integer_read(p, a->address + p->health, 4, &health, error) ||
        !integer_read(p, a->address + p->mass, 4, &mass, error) ||
        !integer_read(p, a->address + p->damageable, p->kex ? 1 : 4, &damageable, error) ||
        !integer_read(p, a->address + p->flags, p->kex ? 8 : 4, &flags, error) ||
        !pointer_read(p, a->address + p->client_pointer, &client, error) ||
        !qa_native_rva(native(p), p->time, p->kex ? 8 : 4, &time, error) ||
        !integer_read(p, time, p->kex ? 8 : 4, &now, error)) return false;
    out->health = (float)health; out->mass = (float)mass; out->team = 0;
    if ((double)out->health != (double)health || (double)out->mass != (double)mass)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native health or mass exceeds exact shared combat representation");
    if (client) {
        if (p->kex) {
            if (!integer_read(p, client + p->invincible, 8, &until, error)) return false;
            int64_t team;
            if (p->team != UINT32_MAX) {
                if (!integer_read(p, client + p->team, 4, &team, error)) return false;
            } else {
                qa_q2_player player;
                if (!qa_native_host_q2_player(p->engine->provider->state.native.host, a->slot, &player, error)) return false;
                team = player.team_id;
            }
            if (team > 0) {
                char text[48]; snprintf(text, sizeof(text), "q2:%lld", (long long)team);
                if (!qa_strings_intern_cstr(qa_session_strings(p->engine->provider->application->session), text, &out->team, error)) return false;
            }
        } else {
            uint8_t bytes[4]; qa_buffer info = {0}; char skin[1024];
            if (!qa_native_read(native(p), client + p->invincible, bytes, 4, error)) return false;
            float expiration = qa_load_f32le(bytes);
            if (!isfinite(expiration)) return application_fail(error, QA_ERROR_FORMAT, "Native invulnerability expiration is invalid");
            until = expiration > (float)now ? now + 1 : 0;
            const qa_cvar_view *coop = qa_cvars_find(p->engine->cvars, "coop"),
                *rules = qa_cvars_find(p->engine->cvars, "dmflags");
            if (coop && coop->number != 0) {
                if (!qa_strings_intern_cstr(qa_session_strings(p->engine->provider->application->session), "q2:coop", &out->team, error)) return false;
            } else if (rules && ((uint32_t)rules->integer & (p->model_team | p->skin_team))) {
                bool ok = qa_native_read_string(native(p), client + p->userinfo, p->userinfo_bytes, &info, error) &&
                    classic_skin((char *)info.data, skin, sizeof(skin), error);
                qa_buffer_free(&info); if (!ok) return false;
                char *slash = strchr(skin, '/'); const char *value = skin; const char *prefix = "q2:model:";
                if ((uint32_t)rules->integer & p->model_team) { if (slash) *slash = 0; }
                else { value = slash ? slash + 1 : skin; prefix = "q2:skin:"; }
                char text[1040]; snprintf(text, sizeof(text), "%s%s", prefix, value);
                if (!qa_strings_intern_cstr(qa_session_strings(p->engine->provider->application->session), text, &out->team, error)) return false;
            }
        }
    } else if (p->kex) {
        int64_t svflags;
        if (!integer_read(p, a->address + p->svflags, 4, &svflags, error) ||
            (((uint32_t)svflags & 4) && !integer_read(p, a->address + p->monster_invincible, 8, &until, error))) return false;
    }
    out->can_take_damage = damageable != 0;
    out->no_knockback = ((uint64_t)flags & p->no_knockback) != 0;
    out->invulnerable = ((uint64_t)flags & p->godmode) != 0 || until > now;
    return true;
}
bool application_q2_combat_health_write(void *opaque, float value, qa_error *error)
{
    application_q2_combat_actor *a = opaque;
    return application_q2_combat_actor_valid(a, error) &&
        integer_write(a->profile, a->address + a->profile->health, value, error);
}
static qa_q2_classic_cause_profile classic_product(const application_q2_combat_profile *p)
{
    qa_json_id id = qa_json_get(p->document, p->world, "game");
    if (qa_json_string_equal(p->document, id, "xatrix")) return QA_Q2_NATIVE_XATRIX;
    if (qa_json_string_equal(p->document, id, "rogue")) return QA_Q2_NATIVE_ROGUE;
    if (qa_json_string_equal(p->document, id, "ctf")) return QA_Q2_NATIVE_CTF;
    return QA_Q2_NATIVE_BASE;
}
bool application_q2_native_cause_read(bool rerelease,qa_q2_classic_cause_profile product,
    const qa_native_value *value, uint32_t flags, qa_damage_cause *out, qa_error *error)
{
    *out = (qa_damage_cause){.kind = QA_CAUSE_Q2}; out->source.q2.flags = flags;
    uint32_t id;
    if (rerelease) {
        if (value->type != QA_NATIVE_BYTES || value->as.bytes.size != 3 || !value->as.bytes.data)
            return application_fail(error, QA_ERROR_FORMAT, "Native KEX damage lacks its exact source mod_t");
        const uint8_t *mod = value->as.bytes.data;
        if (mod[0] > 58 || mod[1] > 1 || mod[2] > 1)
            return application_fail(error, QA_ERROR_FORMAT, "Native KEX damage lacks its exact source mod_t");
        id = mod[0];
        out->source.q2.native = QA_Q2_CAUSE_RERELEASE; out->source.q2.native_value = (int32_t)id;
        out->source.q2.friendly_fire = mod[1] != 0;
        out->source.q2.no_point_loss = mod[2] != 0;
        out->source.q2.means_of_death = (int32_t)((id < 22 ? id : id == 22 ? 57 : id <= 56 ? id - 1 : id == 57 ? 56 : 58) |
            (out->source.q2.friendly_fire ? UINT32_C(0x08000000) : 0));
    } else {
        if (value->type != QA_NATIVE_I32 || value->as.i32 < 0)
            return application_fail(error, QA_ERROR_FORMAT, "Native classic damage lacks its exact source MOD word");
        uint32_t raw = (uint32_t)value->as.i32; id = raw & ~UINT32_C(0x08000000);
        if (!(id <= 33 || (product == QA_Q2_NATIVE_XATRIX && id <= 39) ||
            (product == QA_Q2_NATIVE_ROGUE && id >= 40 && id <= 55) || (product == QA_Q2_NATIVE_CTF && id == 34)))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Native classic damage cause is outside its qualified product roster");
        out->source.q2.native = QA_Q2_CAUSE_CLASSIC; out->source.q2.native_value = value->as.i32;
        out->source.q2.classic_product = (uint32_t)product;
        out->source.q2.friendly_fire = (raw & UINT32_C(0x08000000)) != 0;
        out->source.q2.means_of_death = (int32_t)((product == QA_Q2_NATIVE_CTF && id == 34 ? 56 : id) |
            (raw & UINT32_C(0x08000000)));
    }
    return true;
}
bool application_q2_native_cause_lower(bool rerelease,qa_q2_classic_cause_profile classic,
    const qa_damage_cause *cause, uint8_t mod[3], qa_native_value *out, qa_error *error)
{
    /* The original foreign-cause contract lowers an unclassified other-game
     * hit to MOD_UNKNOWN. The shared request retains its original cause. */
    qa_damage_cause lowered = {.kind = QA_CAUSE_Q2};
    if (cause->kind == QA_CAUSE_Q2) {
        if ((rerelease && cause->source.q2.native == QA_Q2_CAUSE_RERELEASE) ||
            (!rerelease && cause->source.q2.native == QA_Q2_CAUSE_CLASSIC &&
             cause->source.q2.classic_product == (uint32_t)classic)) {
            qa_native_value original = rerelease
                ? (qa_native_value){.type = QA_NATIVE_BYTES, .as.bytes = {mod, 3}}
                : (qa_native_value){.type = QA_NATIVE_I32, .as.i32 = cause->source.q2.native_value};
            if (rerelease) {
                if (cause->source.q2.native_value < 0 || cause->source.q2.native_value > 58)
                    return application_fail(error, QA_ERROR_FORMAT, "Native KEX cause exceeds its source mod_t");
                mod[0] = (uint8_t)cause->source.q2.native_value;
                mod[1] = cause->source.q2.friendly_fire; mod[2] = cause->source.q2.no_point_loss;
            }
            qa_damage_cause classified;
            if (!application_q2_native_cause_read(rerelease,classic, &original, cause->source.q2.flags, &classified, error)) return false;
            if (classified.source.q2.means_of_death == cause->source.q2.means_of_death) lowered = *cause;
            else return application_fail(error, QA_ERROR_FORMAT, "Native saved cause differs from its canonical classification");
        }
        else {
            qa_q2_product product = classic == QA_Q2_NATIVE_XATRIX ? QA_Q2_XATRIX :
                classic == QA_Q2_NATIVE_ROGUE ? QA_Q2_ROGUE : QA_Q2_BASE;
            lowered = qa_q2_damage_cause(rerelease ? QA_Q2_RERELEASE : QA_Q2_CLASSIC,
                product, cause->source.q2.means_of_death, cause->source.q2.flags);
            if (!rerelease && classic == QA_Q2_NATIVE_CTF &&
                lowered.source.q2.native == QA_Q2_CAUSE_CLASSIC &&
                (((uint32_t)lowered.source.q2.native_value & ~UINT32_C(0x08000000)) <= 33))
                lowered.source.q2.classic_product = QA_Q2_NATIVE_CTF;
            if (lowered.source.q2.native == QA_Q2_CAUSE_NONE ||
                (!rerelease && lowered.source.q2.classic_product != (uint32_t)classic))
                lowered = (qa_damage_cause){.kind = QA_CAUSE_Q2};
        }
    }
    if (rerelease) {
        if (lowered.source.q2.native_value < 0 || lowered.source.q2.native_value > 58)
            return application_fail(error, QA_ERROR_FORMAT, "Native KEX cause exceeds its source mod_t");
        mod[0] = (uint8_t)lowered.source.q2.native_value;
        mod[1] = lowered.source.q2.friendly_fire; mod[2] = lowered.source.q2.no_point_loss;
        *out = (qa_native_value){.type = QA_NATIVE_BYTES, .as.bytes = {mod, 3}};
    } else *out = (qa_native_value){.type = QA_NATIVE_I32, .as.i32 = lowered.source.q2.native_value};
    return true;
}
bool application_q2_combat_cause_read(const application_q2_combat_profile *p,
    const qa_native_value *value,uint32_t flags,qa_damage_cause *out,qa_error *error)
{
    return application_q2_native_cause_read(p->kex,classic_product(p),value,flags,out,error);
}
bool application_q2_combat_cause_lower(const application_q2_combat_profile *p,
    const qa_damage_cause *cause,uint8_t mod[3],qa_native_value *out,qa_error *error)
{
    return application_q2_native_cause_lower(p->kex,classic_product(p),cause,mod,out,error);
}
