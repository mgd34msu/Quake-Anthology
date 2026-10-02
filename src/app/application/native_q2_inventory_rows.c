#include "native_q2_inventory_rows.h"
#include "native_q2_inventory_source.h"
#include "guest_native_q2_private.h"
#include "guest_native_q2_attack.h"
#include "map_players_private.h"
#include "supplies.h"
#include "qa/game_q2_items.h"
#include "guest_q3_private.h"
#include "guest_q3_catalog.h"
#include "qa/application_qc_presentation.h"
#include <stdlib.h>
#include <string.h>

typedef struct original_slot {
    qa_item_id item;
    uint32_t index;
    char *icon;
    bool weapon, ammunition;
} original_slot;

struct application_native_q2_inventory_rows {
    struct application_native_q2 *engine;
    const qa_json_document *document;
    original_slot *slots;
    size_t slot_count;
    uint32_t prototypes[4];
    application_native_q2_inventory_readout latest;
    size_t calls;
    bool prepared;
};

static bool word(const qa_json_document *doc, qa_json_id object, const char *key,
    uint32_t *out, qa_error *error) {
    uint64_t value;
    if (!qa_json_u64(doc, qa_json_get(doc, object, key), &value, error)) return false;
    if (value > UINT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Original inventory descriptor exceeds its source word");
    *out = (uint32_t)value;
    return true;
}

static qa_native_instance *instance(application_native_q2_inventory_rows *owner) {
    return qa_native_host_instance(owner->engine->provider->state.native.host);
}

static bool owner_current(application_native_q2_inventory_rows *owner, qa_error *error) {
    struct application_native_q2 *engine = owner ? owner->engine : NULL;
    application_provider *provider = engine ? engine->provider : NULL;
    if (!provider || !provider->application || !provider->constructed || provider->close_pending ||
        provider->state.native.q2_engine != engine || !provider->state.native.host ||
        engine->shutting_down || !engine->map_ready || !engine->primary_inventory ||
        application_world_provider(provider->application, QA_ROLE_ENTITIES, "") != provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Mixed inventory lost its genuine native primary owner");
    return true;
}

static bool descriptor_text(application_native_q2_inventory_rows *owner,
    qa_native_address descriptor, uint32_t offset, uint32_t stride,
    uint8_t pointer_bytes, qa_buffer *out, qa_error *error) {
    uint8_t pointer[8];
    if (offset > stride || pointer_bytes > stride - offset)
        return application_fail(error, QA_ERROR_FORMAT, "Original item string leaves its declared descriptor");
    if (!qa_native_read(instance(owner), descriptor + offset, pointer, pointer_bytes, error)) return false;
    qa_native_address address = pointer_bytes == 4 ? qa_load_u32le(pointer) : qa_load_u64le(pointer);
    if (address) return qa_native_read_string(instance(owner), address, 1024, out, error);
    out->data = calloc(1, 1);
    out->size = 0;
    return out->data != NULL || application_fail(error, QA_ERROR_MEMORY, "Copying empty original item string");
}

static bool classname(qa_bytes name) {
    if (!name.size || name.data[0] < 'a' || name.data[0] > 'z') return false;
    for (size_t i = 1; i < name.size; ++i)
        if (!((name.data[i] >= 'a' && name.data[i] <= 'z') ||
              (name.data[i] >= '0' && name.data[i] <= '9') || name.data[i] == '_')) return false;
    return true;
}

static void slots_free(original_slot *slots, size_t count) {
    for (size_t i = 0; i < count; ++i) free(slots[i].icon);
    free(slots);
}

bool application_native_q2_inventory_rows_prepare(application_native_q2_inventory_rows *owner,
    qa_error *error) {
    if (!owner_current(owner, error)) return false;
    const qa_json_document *doc = application_native_q2_attack_declaration_read(owner->engine);
    if (!doc) return application_fail(error, QA_ERROR_ARGUMENT,
        "Original inventory requires its initialized command item declaration");
    if (owner->prepared)
        return owner->document == doc || application_fail(error, QA_ERROR_ARGUMENT,
            "Original inventory declaration changed after descriptor admission");
    qa_json_id root = qa_json_root(doc);
    qa_json_id items = qa_json_get(doc, qa_json_get(doc, root, "commands"), "items");
    uint32_t table, stride, count, name_offset, icon_offset, flags_offset, weapon_flag, ammo_flag;
    if (!word(doc, items, "table", &table, error) || !word(doc, items, "stride", &stride, error) ||
        !word(doc, items, "count", &count, error) || !word(doc, items, "classname", &name_offset, error) ||
        !word(doc, items, "icon", &icon_offset, error) || !word(doc, items, "flags", &flags_offset, error) ||
        !word(doc, items, "weaponFlag", &weapon_flag, error) ||
        !word(doc, items, "ammunitionFlag", &ammo_flag, error)) return false;
    uint8_t pointer_bytes = qa_native_module_describe(owner->engine->provider->state.native.module).image.target.pointer_bytes;
    if (!count || !stride || flags_offset > stride || stride - flags_offset < 4 ||
        (pointer_bytes != 4 && pointer_bytes != 8) || !weapon_flag || !ammo_flag ||
        sizeof(original_slot) > SIZE_MAX / count)
        return application_fail(error, QA_ERROR_FORMAT, "Original inventory has an invalid command item table");
    original_slot *slots = calloc(count, sizeof(*slots));
    if (!slots) return application_fail(error, QA_ERROR_MEMORY, "Acquiring original inventory descriptors");
    size_t used = 0;
    bool okay = true;
    ++owner->calls;
    ++owner->engine->calls;
    for (uint32_t i = 0; okay && i < count; ++i) {
        qa_native_address at;
        qa_buffer name = {0}, icon = {0};
        uint8_t bytes[4];
        okay = qa_native_rva(instance(owner), (uint64_t)table + (uint64_t)i * stride,
            stride, &at, error) &&
            descriptor_text(owner, at, name_offset, stride, pointer_bytes, &name, error) &&
            descriptor_text(owner, at, icon_offset, stride, pointer_bytes, &icon, error) &&
            qa_native_read(instance(owner), at + flags_offset, bytes, 4, error);
        uint32_t flags = okay ? qa_load_u32le(bytes) : 0;
        bool valid = okay && classname((qa_bytes){name.data, name.size});
        if (okay && !valid && (flags & weapon_flag))
            okay = application_fail(error, QA_ERROR_FORMAT, "Original inventory weapon has no qualified classname");
        if (okay && valid) {
            char qualified[1030];
            memcpy(qualified, "q2:", 3);
            memcpy(qualified + 3, name.data, name.size);
            qualified[name.size + 3] = 0;
            qa_item_id item;
            okay = qa_strings_intern_cstr(qa_session_strings(owner->engine->provider->application->session),
                qualified, &item, error);
            uint32_t source_index;
            if (okay) okay = application_native_q2_inventory_index(owner->engine, item, &source_index, error);
            if (okay && source_index != i)
                okay = application_fail(error, QA_ERROR_FORMAT, "Original command item differs from its actual inventory slot");
            for (size_t j = 0; okay && j < used; ++j)
                if (slots[j].item == item)
                    okay = application_fail(error, QA_ERROR_FORMAT, "Original inventory repeats an actual item classname");
            if (okay) {
                slots[used++] = (original_slot){.item = item, .index = i, .icon = (char *)icon.data,
                    .weapon = (flags & weapon_flag) != 0, .ammunition = (flags & ammo_flag) != 0};
                icon = (qa_buffer){0};
            }
        }
        qa_buffer_free(&name);
        qa_buffer_free(&icon);
    }
    uint32_t prototypes[4] = {0};
    static const char *const keys[] = {"weapon", "ammunition", "usable", "passive"};
    qa_json_id declarations = qa_json_get(doc, qa_json_get(doc, root, "inventory"), "prototypes");
    for (size_t i = 0; okay && i < 4; ++i) {
        qa_buffer name = {0};
        okay = qa_json_string(doc, qa_json_get(doc, declarations, keys[i]), &name, error);
        qa_item_id item = okay ? qa_strings_find(qa_session_strings(owner->engine->provider->application->session),
            (qa_bytes){name.data, name.size}) : 0;
        bool found = false;
        for (size_t j = 0; okay && j < used; ++j) if (slots[j].item == item) {
            prototypes[i] = slots[j].index;
            found = true;
        }
        if (okay && !found)
            okay = application_fail(error, QA_ERROR_FORMAT, "Original inventory lacks an admitted item prototype");
        qa_buffer_free(&name);
    }
    if (okay) okay = owner_current(owner, error) &&
        application_native_q2_attack_declaration_read(owner->engine) == doc;
    --owner->engine->calls;
    --owner->calls;
    if (!okay) { slots_free(slots, used); return false; }
    owner->slots = slots;
    owner->slot_count = used;
    memcpy(owner->prototypes, prototypes, sizeof(prototypes));
    owner->document = doc;
    owner->prepared = true;
    return true;
}

static bool player_current(application_native_q2_inventory_rows *owner,
    const application_native_q2_inventory_source *source, application_provider *arsenal,
    uint64_t map_revision, uint64_t config_revision, qa_error *error) {
    if (!owner_current(owner, error)) return false;
    qa_application *app = owner->engine->provider->application;
    if (app->map_revision != map_revision || owner->engine->config_revision != config_revision ||
        application_provider_for(app, source->actor, QA_ROLE_ARSENAL, "") != arsenal ||
        !arsenal || !arsenal->constructed || !arsenal->attached || arsenal->close_pending ||
        application_native_q2_attack_declaration_read(owner->engine) != owner->document)
        return application_fail(error, QA_ERROR_ARGUMENT, "Mixed inventory changed its actual player or source declarations");
    return application_native_q2_inventory_source_current(owner->engine, source, error);
}

static char *copy_text(const char *text, qa_error *error) {
    if (!text) { application_fail(error, QA_ERROR_ARGUMENT, "Inventory declaration lacks its actual label"); return NULL; }
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (!copy) { application_fail(error, QA_ERROR_MEMORY, "Copying inventory presentation"); return NULL; }
    memcpy(copy, text, length + 1);
    return copy;
}

static bool append(application_native_q2_inventory_readout *out,
    const application_native_q2_inventory_row *row, qa_error *error) {
    application_native_q2_inventory_row value = *row;
    value.label = copy_text(row->label, error);
    value.presentation.icon = row->presentation.icon ? copy_text(row->presentation.icon, error) : NULL;
    value.presentation.lump = row->presentation.lump ? copy_text(row->presentation.lump, error) : NULL;
    if (!value.label || (row->presentation.icon && !value.presentation.icon) ||
        (row->presentation.lump && !value.presentation.lump)) {
        free((char *)value.label); free((char *)value.presentation.icon); free((char *)value.presentation.lump);
        return false;
    }
    size_t index = out->count;
    for (size_t i = 0; i < out->count; ++i) if (out->rows[i].item == row->item) { index = i; break; }
    if (index == out->count) {
        if (out->count == SIZE_MAX / sizeof(*out->rows)) {
            free((char *)value.label); free((char *)value.presentation.icon); free((char *)value.presentation.lump);
            return application_fail(error, QA_ERROR_MEMORY, "Mixed inventory row extent overflows");
        }
        application_native_q2_inventory_row *grown = realloc(out->rows, (out->count + 1) * sizeof(*grown));
        if (!grown) {
            free((char *)value.label); free((char *)value.presentation.icon); free((char *)value.presentation.lump);
            return application_fail(error, QA_ERROR_MEMORY, "Building current mixed inventory rows");
        }
        out->rows = grown;
        ++out->count;
    } else {
        free((char *)out->rows[index].label);
        free((char *)out->rows[index].presentation.icon);
        free((char *)out->rows[index].presentation.lump);
    }
    out->rows[index] = value;
    return true;
}

static bool original_label(application_native_q2_inventory_rows *owner, uint32_t index,
    const char **out, qa_error *error) {
    uint32_t base = owner->engine->profile == QA_NATIVE_Q2_GAME_API2023 ? 11326u : 1056u;
    if (index > UINT32_MAX - base || base + index >= owner->engine->configstring_count)
        return application_fail(error, QA_ERROR_FORMAT, "Original inventory label leaves its source configstring namespace");
    *out = owner->engine->configstrings[base + index];
    if (!*out) *out = "";
    return true;
}

static bool ammo_label(qa_application *app, application_provider *arsenal,
    qa_item_id item, const char **out, qa_error *error) {
    const char *name = qa_strings_cstr(qa_session_strings(app->session), item);
    if (arsenal->product->family == QA_GAME_Q1) {
        static const char *const items[] = {"q1:ammo/shells", "q1:ammo/nails", "q1:ammo/rockets",
            "q1:ammo/cells", "rogue:ammo/lava-nails", "rogue:ammo/multi-rockets", "rogue:ammo/plasma"};
        static const char *const labels[] = {"Shells", "Nails", "Rockets", "Cells", "Lava Nails", "Multi Rockets", "Plasma"};
        for (size_t i = 0; name && i < sizeof(items) / sizeof(items[0]); ++i)
            if (!strcmp(name, items[i])) { *out = labels[i]; return true; }
    } else if (arsenal->kind == APPLICATION_PROVIDER_Q2) {
        for (size_t i = 0; i < qa_q2_item_count(arsenal->state.q2); ++i) {
            const qa_q2_item_definition *definition = qa_q2_item_at(arsenal->state.q2, i);
            if (definition && definition->item == item) { *out = definition->name; return true; }
        }
    } else if (arsenal->kind == APPLICATION_PROVIDER_Q3) {
        size_t count;
        const qa_q3_item *items = qa_q3_game_items(arsenal->state.q3, &count);
        for (size_t i = 0; i < count; ++i)
            if (items[i].kind == QA_Q3_ITEM_AMMO && qa_q3_item_identity(arsenal->state.q3, (uint32_t)i) == item)
                { *out = items[i].name; return true; }
    } else if (arsenal->kind == APPLICATION_PROVIDER_QVM) {
        struct application_q3_guest *engine = q3g_engine(arsenal);
        if (engine && engine->game && engine->game->catalog)
            return application_q3_catalog_ammo_label(engine->game->catalog, item, out, error);
    }
    return application_fail(error, QA_ERROR_NOT_FOUND,
        "Selected ammunition lacks its actual source catalog label");
}

typedef struct selected_catalog { qa_item_definition *items; size_t count; } selected_catalog;
static void catalog_free(selected_catalog *catalog) {
    for (size_t i = 0; i < catalog->count; ++i) free((char *)catalog->items[i].label);
    free(catalog->items);
}
static bool catalog_append(selected_catalog *catalog, const qa_item_definition *definition,
    qa_error *error) {
    for (size_t i = 0; i < catalog->count; ++i)
        if (catalog->items[i].item == definition->item) return true;
    if (catalog->count == SIZE_MAX / sizeof(*catalog->items))
        return application_fail(error, QA_ERROR_MEMORY, "Selected weapon catalog extent overflows");
    char *label = copy_text(definition->label, error);
    if (!label) return false;
    qa_item_definition *grown = realloc(catalog->items, (catalog->count + 1) * sizeof(*grown));
    if (!grown) { free(label); return application_fail(error, QA_ERROR_MEMORY, "Copying selected weapon catalog"); }
    catalog->items = grown;
    catalog->items[catalog->count] = *definition;
    catalog->items[catalog->count++].label = label;
    return true;
}
static bool catalog_read(qa_application *app, application_provider *arsenal, qa_actor_id actor,
    const qa_inventory_source_snapshot *snapshot, selected_catalog *out, qa_error *error) {
    if (arsenal->kind == APPLICATION_PROVIDER_QC) {
        qa_application_qc_player_ui view;
        if (!qa_application_qc_selected_player_ui_read(app, actor, QA_ROLE_ARSENAL, &view, error)) return false;
        for (size_t i = 0; i < view.binding_count; ++i) {
            qa_item_definition definition;
            if (!qa_application_qc_message_player_ui_definition(app, &view, i, &definition, error) ||
                !catalog_append(out, &definition, error)) return false;
        }
        return qa_application_qc_message_player_ui_current(app, &view) ||
            application_fail(error, QA_ERROR_ARGUMENT, "Selected QC catalog changed during inventory observation");
    }
    if (arsenal->kind == APPLICATION_PROVIDER_QVM) {
        struct application_q3_guest *engine = q3g_engine(arsenal);
        uint32_t slot;
        const application_q3_catalog_weapon *weapons; size_t count;
        if (!engine || !engine->game || !engine->game->catalog ||
            !application_q3_guest_actor_client(arsenal, actor, &slot))
            return application_fail(error, QA_ERROR_ARGUMENT, "Selected GAME catalog lacks its actual begun client");
        q3g_role *game = engine->game;
        if (!application_q3_catalog_weapons(game->catalog, &weapons, &count, error)) return false;
        for (size_t i = 0; i < count; ++i) {
            qa_item_definition definition = {.item = weapons[i].item, .ammo = weapons[i].ammo,
                .owner = arsenal->owner, .label = weapons[i].label, .weapon = true};
            if (!catalog_append(out, &definition, error)) return false;
        }
        uint32_t current_slot;
        return engine->game == game && application_q3_guest_actor_client(arsenal, actor, &current_slot) &&
            current_slot == slot && application_q3_catalog_current(game->catalog, game->image, game->vm, game->abi) ? true :
            application_fail(error, QA_ERROR_ARGUMENT, "Selected GAME catalog changed during inventory observation");
    }
    for (size_t g = 0; g < snapshot->group_count; ++g) {
        const qa_inventory_source_group *group = snapshot->groups + g;
        if (group->owner != arsenal->owner) continue;
        for (size_t i = 0; i < group->count; ++i)
            if (group->items[i].definition.weapon && !catalog_append(out, &group->items[i].definition, error)) return false;
    }
    return out->count != 0 || application_fail(error, QA_ERROR_NOT_FOUND,
        "Selected arsenal lacks its actual admitted weapon catalog");
}

static bool row_count(application_native_q2_inventory_rows *owner,
    const application_native_q2_inventory_source *source, application_provider *arsenal,
    uint64_t map_revision, uint64_t config_revision, application_native_q2_inventory_row *row,
    application_native_q2_inventory_readout *out, qa_error *error) {
    qa_application *app = owner->engine->provider->application;
    return qa_inventory_count_read(app->inventory, source->actor, row->item, &row->count, error) &&
        player_current(owner, source, arsenal, map_revision, config_revision, error) && append(out, row, error);
}

static bool rows(void *context, qa_actor_id actor,
    const application_native_q2_inventory_row **out, size_t *count, bool *present, qa_error *error) {
    application_native_q2_inventory_rows *owner = context;
    if (!out || !count || !present)
        return application_fail(error, QA_ERROR_ARGUMENT, "Mixed inventory requires its row admission outputs");
    if (!application_native_q2_inventory_rows_prepare(owner, error)) return false;
    qa_application *app = owner->engine->provider->application;
    application_provider *primary = owner->engine->provider;
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    application_native_q2_inventory_source source;
    if (!application_native_q2_inventory_source_read(owner->engine, actor, &source, error)) return false;
    uint64_t map_revision = app->map_revision, config_revision = owner->engine->config_revision;
    if (!player_current(owner, &source, arsenal, map_revision, config_revision, error)) return false;
    qa_inventory_source_snapshot snapshot = {0};
    selected_catalog catalog = {0};
    application_native_q2_inventory_readout result = {0};
    ++owner->calls; ++owner->engine->calls;
    bool okay = qa_inventory_source_items(app->inventory, actor, &snapshot, error) &&
        player_current(owner, &source, arsenal, map_revision, config_revision, error);
    bool selected = arsenal != primary;
    size_t component_count = 0;
    if (okay && selected) okay = catalog_read(app, arsenal, actor, &snapshot, &catalog, error) &&
        player_current(owner, &source, arsenal, map_revision, config_revision, error);
    for (size_t g = 0; okay && g < snapshot.group_count; ++g) {
        const qa_inventory_source_group *group = snapshot.groups + g;
        if (group->definitions_only) continue;
        for (size_t i = 0; i < group->count; ++i) {
            qa_actor_owner actual;
            if (qa_inventory_item_owner(app->inventory, actor, group->items[i].definition.item, &actual, NULL) &&
                actual == group->owner) ++component_count;
        }
    }
    if (okay && (selected || component_count)) {
        result.present = true;
        for (size_t i = 0; okay && selected && i < catalog.count; ++i) {
            const qa_item_definition *definition = catalog.items + i;
            application_native_q2_inventory_row row = {.item = definition->item,
                .label = definition->label, .source_index = owner->prototypes[0], .selected = true,
                .presentation = {.source = arsenal->owner, .kind = APPLICATION_NATIVE_INVENTORY_PRESENTATION_WEAPON,
                    .weapon = definition->item}};
            okay = row_count(owner, &source, arsenal, map_revision, config_revision, &row, &result, error);
        }
        for (size_t i = 0; okay && selected && i < catalog.count; ++i) {
            const qa_item_definition *definition = catalog.items + i;
            if (!definition->ammo) continue;
            bool duplicate = false;
            for (size_t j = 0; j < result.count; ++j) if (result.rows[j].item == definition->ammo) duplicate = true;
            if (duplicate) continue;
            const char *borrowed;
            if (!ammo_label(app, arsenal, definition->ammo, &borrowed, error)) { okay = false; break; }
            char *label = copy_text(borrowed, error);
            if (!label) { okay = false; break; }
            application_native_q2_inventory_row row = {.item = definition->ammo, .label = label,
                .source_index = owner->prototypes[1], .selected = true,
                .presentation = {.source = arsenal->owner, .kind = APPLICATION_NATIVE_INVENTORY_PRESENTATION_AMMUNITION,
                    .weapon = definition->item}};
            okay = row_count(owner, &source, arsenal, map_revision, config_revision, &row, &result, error);
            free(label);
        }
        for (size_t i = 0; okay && selected && i < owner->slot_count; ++i) {
            const original_slot *slot = owner->slots + i;
            if (!slot->ammunition) continue;
            bool duplicate = false, retained = false;
            for (size_t j = 0; j < result.count; ++j)
                if (result.rows[j].item == slot->item &&
                    result.rows[j].presentation.kind == APPLICATION_NATIVE_INVENTORY_PRESENTATION_AMMUNITION)
                    duplicate = true;
            if (duplicate) continue;
            okay = application_supplies_ammo_destination(app->supplies, primary, actor, slot->item, &retained, error);
            if (!okay || !retained) continue;
            const char *label;
            if (!original_label(owner, slot->index, &label, error)) { okay = false; break; }
            char path[1040];
            const char *icon = NULL;
            if (*slot->icon) {
                if (slot->icon[0] == '/' || slot->icon[0] == '\\') icon = slot->icon + 1;
                else { size_t length = strlen(slot->icon); memcpy(path, "pics/", 5);
                    memcpy(path + 5, slot->icon, length); memcpy(path + 5 + length, ".pcx", 5); icon = path; }
            }
            application_native_q2_inventory_row row = {.item = slot->item, .label = label,
                .source_index = owner->prototypes[1], .selected = true,
                .presentation = {.source = primary->owner, .kind = APPLICATION_NATIVE_INVENTORY_PRESENTATION_ITEM,
                    .icon_kind = icon ? APPLICATION_NATIVE_INVENTORY_ICON_IMAGE : APPLICATION_NATIVE_INVENTORY_ICON_NONE,
                    .icon = icon}};
            okay = row_count(owner, &source, arsenal, map_revision, config_revision, &row, &result, error);
        }
        for (size_t i = 0; okay && i < owner->slot_count; ++i) {
            const original_slot *slot = owner->slots + i;
            if (selected && (slot->weapon || slot->ammunition)) continue;
            const char *label;
            if (!original_label(owner, slot->index, &label, error)) { okay = false; break; }
            application_native_q2_inventory_row row = {.item = slot->item, .label = label, .source_index = slot->index};
            okay = row_count(owner, &source, arsenal, map_revision, config_revision, &row, &result, error);
        }
        /* The Source Map replacement order is equipment, then component definitions. */
        for (unsigned pass = 0; okay && pass < 2; ++pass)
        for (size_t g = 0; okay && g < snapshot.group_count; ++g) {
            const qa_inventory_source_group *group = snapshot.groups + g;
            for (size_t i = 0; okay && i < group->count; ++i) {
                const qa_item_definition *definition = &group->items[i].definition;
                qa_actor_owner actual;
                bool component = !group->definitions_only &&
                    qa_inventory_item_owner(app->inventory, actor, definition->item, &actual, NULL) && actual == group->owner;
                bool equipment = group->definitions_only && !definition->weapon &&
                    group->owner == arsenal->owner && arsenal->kind == APPLICATION_PROVIDER_Q3;
                if ((pass == 0 && !equipment) || (pass == 1 && !component)) continue;
                bool ammunition = false;
                for (size_t other = 0; other < snapshot.group_count; ++other) {
                    const qa_inventory_source_group *weapons = snapshot.groups + other;
                    if (weapons->definitions_only) continue;
                    for (size_t j = 0; j < weapons->count; ++j) {
                        const qa_item_definition *weapon = &weapons->items[j].definition;
                        qa_actor_owner actual_weapon;
                        if (weapon->weapon && weapon->ammo == definition->item &&
                            qa_inventory_item_owner(app->inventory, actor, weapon->item, &actual_weapon, NULL) &&
                            actual_weapon == weapons->owner) ammunition = true;
                    }
                }
                uint32_t prototype = equipment ?
                    (definition->actions & QA_ITEM_USE ? owner->prototypes[2] : owner->prototypes[3]) :
                    definition->weapon ? owner->prototypes[0] :
                    definition->actions & QA_ITEM_USE ? owner->prototypes[2] :
                    ammunition ? owner->prototypes[1] : owner->prototypes[3];
                application_native_q2_inventory_row row = {.item = definition->item, .label = definition->label,
                    .source_index = prototype, .selected = true, .presence_only = component,
                    .presentation = {.source = group->owner, .kind = APPLICATION_NATIVE_INVENTORY_PRESENTATION_ITEM}};
                if (equipment) {
                    size_t item_count;
                    const qa_q3_item *items = qa_q3_game_items(arsenal->state.q3, &item_count);
                    bool declared = false;
                    for (size_t j = 0; j < item_count; ++j)
                        if (qa_q3_item_identity(arsenal->state.q3, (uint32_t)j) == definition->item) {
                            declared = true;
                            row.presentation.icon = items[j].icon;
                            row.presentation.icon_kind = items[j].icon ? APPLICATION_NATIVE_INVENTORY_ICON_SHADER :
                                APPLICATION_NATIVE_INVENTORY_ICON_NONE;
                            break;
                        }
                    if (!declared) { okay = application_fail(error, QA_ERROR_NOT_FOUND,
                        "Selected equipment lacks its real source item declaration"); break; }
                }
                okay = row_count(owner, &source, arsenal, map_revision, config_revision, &row, &result, error);
            }
        }
    }
    if (okay) okay = player_current(owner, &source, arsenal, map_revision, config_revision, error);
    qa_inventory_source_snapshot_free(&snapshot);
    catalog_free(&catalog);
    --owner->engine->calls; --owner->calls;
    if (!okay) { application_native_q2_inventory_readout_free(&result); return false; }
    application_native_q2_inventory_readout_free(&owner->latest);
    owner->latest = result;
    *out = result.rows; *count = result.count; *present = result.present;
    return true;
}

static bool use(void *context, qa_actor_id actor, qa_item_id item, qa_error *error) {
    application_native_q2_inventory_rows *owner = context;
    if (!owner_current(owner, error)) return false;
    application_native_q2_inventory_source source;
    if (!application_native_q2_inventory_source_read(owner->engine, actor, &source, error)) return false;
    ++owner->calls; ++owner->engine->calls;
    bool okay = qa_inventory_item_action(owner->engine->provider->application->inventory,
        actor, item, QA_ITEM_USE, error) && application_native_q2_inventory_source_current(owner->engine, &source, error);
    --owner->engine->calls; --owner->calls;
    return okay;
}

static bool print(void *context, qa_actor_id actor, const char *text, qa_error *error) {
    application_native_q2_inventory_rows *owner = context;
    if (!text) return application_fail(error, QA_ERROR_ARGUMENT, "Inventory source message lacks its actual text");
    if (!owner_current(owner, error)) return false;
    application_native_q2_inventory_source source;
    if (!application_native_q2_inventory_source_read(owner->engine, actor, &source, error)) return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_MESSAGE, .family = QA_GAME_Q2,
        .provider = owner->engine->provider->owner, .actor = actor, .code = 2,
        .time_ns = owner->engine->frame.time_ns};
    ++owner->calls; ++owner->engine->calls;
    bool okay = qa_strings_intern_cstr(qa_session_strings(owner->engine->provider->application->session),
        text, &event.text, error) && application_emit(owner->engine->provider->application, &event, error) &&
        application_native_q2_inventory_source_current(owner->engine, &source, error);
    --owner->engine->calls; --owner->calls;
    return okay;
}

bool application_native_q2_inventory_rows_create(struct application_native_q2 *engine,
    application_native_q2_inventory_rows **out, qa_error *error) {
    if (!engine || !engine->provider || engine->provider->state.native.q2_engine != engine || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Mixed inventory requires its actual retained native engine");
    application_native_q2_inventory_rows *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Creating mixed original inventory owner");
    owner->engine = engine;
    *out = owner;
    return true;
}
bool application_native_q2_inventory_rows_idle(const application_native_q2_inventory_rows *owner) {
    return !owner || !owner->calls;
}
bool application_native_q2_inventory_rows_destroy(application_native_q2_inventory_rows *owner,
    qa_error *error) {
    if (!owner) return true;
    if (!application_native_q2_inventory_rows_idle(owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Mixed inventory is still borrowing its actual source owner");
    application_native_q2_inventory_readout_free(&owner->latest);
    slots_free(owner->slots, owner->slot_count);
    free(owner);
    return true;
}
application_native_q2_inventory_scanner_options application_native_q2_inventory_rows_options(
    application_native_q2_inventory_rows *owner) {
    return (application_native_q2_inventory_scanner_options){.context = owner,
        .rows = rows, .use = use, .print = print};
}
