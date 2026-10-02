#include "guest_q3_catalog.h"
#include "guest_projection_private.h"
#include "guest_q3_native_catalog.h"
#include "qa/game_q3.h"
#include "qa/json.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct catalog_locator { uint32_t value, maximum; bool global; } catalog_locator;
typedef struct catalog_selection { int32_t value; qa_item_id item; } catalog_selection;
struct application_q3_catalog {
    application_q3_native_catalog *native;
    q3g_role *native_role;
    qa_qvm_image *image;
    qa_qvm *vm;
    qa_qvm_abi abi;
    qa_strings *strings;
    catalog_locator address, count;
    uint32_t stride, fields[4];
    int32_t weapon_type, ammo_type;
    bool table, live, private_inventory, selected, records_full;
    catalog_selection *selection;
    size_t selection_count;
    qa_item_id *stored;
    size_t stored_count;
    application_q3_catalog_record *records;
    size_t record_count;
    uint32_t source_address, source_count;
    application_q3_catalog_record *source_records;
    size_t source_record_count;
    bool source_records_ready;
    application_q3_catalog_weapon *weapons;
    size_t weapon_count;
    guest_inventory_weapon *inventory;
    size_t inventory_count;
};
static bool fail(qa_error *e, qa_status status, const char *text)
{ qa_error_set(e, status, 0, "%s", text); return false; }
static bool word(const qa_json_document *doc, qa_json_id at, uint32_t *out, qa_error *e)
{
    uint64_t value;
    if (!qa_json_u64(doc, at, &value, e)) return false;
    if (value > INT32_MAX) return fail(e, QA_ERROR_FORMAT, "Catalog declaration leaves its signed source address");
    *out = (uint32_t)value; return true;
}
static bool identity(application_q3_catalog *c, const qa_json_document *doc,
    qa_json_id at, qa_item_id *out, qa_error *e)
{
    qa_buffer value = {0};
    if (!qa_json_string(doc, at, &value, e)) return false;
    const uint8_t *colon = memchr(value.data, ':', value.size);
    bool valid = colon && colon != value.data && colon != value.data + value.size - 1 &&
        !memchr(value.data, 0, value.size);
    bool ok = valid ? qa_strings_intern(c->strings, (qa_bytes){value.data, value.size}, out, e) :
        fail(e, QA_ERROR_FORMAT, "Catalog item requires its namespaced identity");
    qa_buffer_free(&value); return ok;
}
static bool locator(application_q3_catalog *c, const qa_json_document *doc,
    qa_json_id at, bool count, catalog_locator *out, qa_error *e)
{
    out->global = qa_json_type(doc, at) == QA_JSON_OBJECT;
    if (!word(doc, out->global ? qa_json_get(doc, at, "global") : at, &out->value, e)) return false;
    if (out->global) {
        size_t extent = qa_qvm_image_memory_size(c->image);
        if (!c->live || out->value % 4 || out->value > extent || extent - out->value < 4)
            return fail(e, QA_ERROR_FORMAT, "Catalog locator leaves its actual live allocation");
        if (count && (!word(doc, qa_json_get(doc, at, "maximum"), &out->maximum, e) || !out->maximum))
            return fail(e, QA_ERROR_FORMAT, "Catalog count has no positive declared maximum");
    } else if ((count && !out->value) || (!count && (!out->value || out->value % 4)))
        return fail(e, QA_ERROR_FORMAT, "Catalog locator is empty or unaligned");
    return true;
}
static bool layout(application_q3_catalog *c, const qa_json_document *doc, qa_json_id at, qa_error *e)
{
    qa_json_id source = qa_json_get(doc, at, "source");
    c->live = source != QA_JSON_NONE;
    if (c->live && !qa_json_string_equal(doc, source, "live"))
        return fail(e, QA_ERROR_FORMAT, "Catalog has no original memory source");
    if (!word(doc, qa_json_get(doc, at, "stride"), &c->stride, e) || c->stride < 4 || c->stride % 4 ||
        !locator(c, doc, qa_json_get(doc, at, "address"), false, &c->address, e) ||
        !locator(c, doc, qa_json_get(doc, at, "count"), true, &c->count, e))
        return fail(e, QA_ERROR_FORMAT, "Catalog record layout is not aligned");
    static const char *const names[] = {"className", "pickupName", "type", "tag"};
    qa_json_id fields = qa_json_get(doc, at, "fields");
    for (size_t i = 0; i < 4; ++i)
        if (!word(doc, qa_json_get(doc, fields, names[i]), c->fields + i, e) ||
            c->fields[i] % 4 || c->fields[i] > c->stride - 4)
            return fail(e, QA_ERROR_FORMAT, "Catalog field leaves its original record");
    uint32_t weapon, ammo;
    if (!word(doc, qa_json_get(doc, at, "weaponType"), &weapon, e) ||
        !word(doc, qa_json_get(doc, at, "ammoType"), &ammo, e) || weapon == ammo)
        return fail(e, QA_ERROR_FORMAT, "Catalog weapon and ammo types are ambiguous");
    c->weapon_type = (int32_t)weapon; c->ammo_type = (int32_t)ammo; c->table = true; return true;
}
static bool primary(application_q3_catalog *c, const qa_json_document *doc, qa_error *e)
{
    qa_json_id root = qa_json_root(doc), items = qa_json_get(doc, root, "items");
    if (!layout(c, doc, items, e)) return false;
    application_q3_catalog pickup = {.image = c->image};
    qa_json_id pickups = qa_json_get(doc, root, "pickups");
    if (!layout(&pickup, doc, qa_json_get(doc, pickups, "items"), e)) return false;
    if (c->address.value != pickup.address.value || c->address.global != pickup.address.global ||
        c->count.value != pickup.count.value || c->count.global != pickup.count.global ||
        c->count.maximum != pickup.count.maximum || c->stride != pickup.stride ||
        c->live != pickup.live || c->weapon_type != pickup.weapon_type || c->ammo_type != pickup.ammo_type ||
        memcmp(c->fields, pickup.fields, sizeof(c->fields)))
        return fail(e, QA_ERROR_FORMAT, "Catalog and pickup declarations name different original tables");
    qa_json_id weapons = qa_json_get(doc, root, "weapons"), stage = qa_json_get(doc, weapons, "stage");
    qa_json_id selection = qa_json_get(doc, stage, "selection"), values = qa_json_get(doc, selection, "values");
    if (qa_json_type(doc, values) != QA_JSON_ARRAY)
        return fail(e, QA_ERROR_FORMAT, "Catalog lacks its original weapon selection");
    c->selected = true; c->selection_count = qa_json_size(doc, values);
    if (c->selection_count > SIZE_MAX / sizeof(*c->selection)) return fail(e, QA_ERROR_MEMORY, "Catalog selection is too large");
    c->selection = c->selection_count ? calloc(c->selection_count, sizeof(*c->selection)) : NULL;
    if (c->selection_count && !c->selection) return fail(e, QA_ERROR_MEMORY, "Retaining catalog selection");
    for (size_t i = 0; i < c->selection_count; ++i) {
        qa_json_id row = qa_json_at(doc, values, i); uint32_t value;
        if (!word(doc, qa_json_get(doc, row, "value"), &value, e) ||
            !identity(c, doc, qa_json_get(doc, row, "item"), &c->selection[i].item, e)) return false;
        if (!value) return fail(e, QA_ERROR_FORMAT, "Catalog selection has no positive original weapon slot");
        c->selection[i].value = (int32_t)value;
        for (size_t j = 0; j < i; ++j) if (c->selection[j].value == (int32_t)value || c->selection[j].item == c->selection[i].item)
            return fail(e, QA_ERROR_FORMAT, "Catalog selection repeats its source value or item");
    }
    qa_json_id inventory = qa_json_get(doc, root, "inventory"), storage = qa_json_get(doc, inventory, "storage");
    c->private_inventory = storage != QA_JSON_NONE;
    if (!c->private_inventory) {
        for (size_t i = 0; i < c->selection_count; ++i) if (c->selection[i].value > 15)
            return fail(e, QA_ERROR_FORMAT, "Catalog selection exceeds public inventory storage");
        return true;
    }
    if (qa_json_type(doc, storage) != QA_JSON_ARRAY) return fail(e, QA_ERROR_FORMAT, "Catalog inventory has no original storage list");
    for (size_t i = 0; i < qa_json_size(doc, storage); ++i) {
        qa_json_id row = qa_json_at(doc, storage, i), kind = qa_json_get(doc, row, "kind"), list = qa_json_get(doc, row, "items");
        bool counter = qa_json_string_equal(doc, kind, "counter");
        if (!counter && !qa_json_string_equal(doc, kind, "bits")) return fail(e, QA_ERROR_FORMAT, "Catalog inventory storage kind is unknown");
        if (!counter && qa_json_type(doc, list) != QA_JSON_ARRAY) return fail(e, QA_ERROR_FORMAT, "Catalog inventory bits lack item list");
        size_t count = counter ? 1 : qa_json_size(doc, list);
        if (count > SIZE_MAX / sizeof(*c->stored) - c->stored_count) return fail(e, QA_ERROR_MEMORY, "Catalog inventory is too large");
        qa_item_id *next = realloc(c->stored, (c->stored_count + count) * sizeof(*next));
        if (count && !next) return fail(e, QA_ERROR_MEMORY, "Retaining catalog inventory identities");
        c->stored = next;
        for (size_t j = 0; j < count; ++j) {
            qa_json_id item = counter ? qa_json_get(doc, row, "item") : qa_json_get(doc, qa_json_at(doc, list, j), "item");
            if (!identity(c, doc, item, c->stored + c->stored_count, e)) return false;
            ++c->stored_count;
        }
    }
    for (size_t i = 0; i < c->selection_count; ++i) {
        bool found = false;
        for (size_t j = 0; j < c->stored_count; ++j) if (c->stored[j] == c->selection[i].item) found = true;
        if (!found) return fail(e, QA_ERROR_FORMAT, "Catalog selection lacks private inventory storage");
    }
    return true;
}
static uint32_t little(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static bool read_bytes(application_q3_catalog *c, uint32_t address, void *out, size_t count, qa_error *e)
{
    if (c->live) return qa_qvm_read(c->vm, address, out, count, e);
    qa_bytes data = qa_qvm_image_initialized_data(c->image);
    if (address > data.size || count > data.size - address) return fail(e, QA_ERROR_FORMAT, "Catalog read leaves initialized artifact data");
    if (count) memcpy(out, data.data + address, count);
    return true;
}
static bool read_word(application_q3_catalog *c, uint32_t address, int32_t *out, qa_error *e)
{
    uint8_t bytes[4]; if (!read_bytes(c, address, bytes, 4, e)) return false;
    uint32_t value = little(bytes); memcpy(out, &value, 4); return true;
}
static bool read_string(application_q3_catalog *c, int32_t pointer, const char **out, qa_error *e)
{
    size_t extent = c->live ? qa_qvm_memory_size(c->vm) : qa_qvm_image_initialized_data(c->image).size;
    if (pointer <= 0 || (uint32_t)pointer >= extent) return fail(e, QA_ERROR_FORMAT, "Catalog string leaves original data");
    size_t length = 0; uint8_t byte;
    do {
        if (length == extent - (uint32_t)pointer || !read_bytes(c, (uint32_t)pointer + (uint32_t)length, &byte, 1, e))
            return fail(e, QA_ERROR_FORMAT, "Catalog string is unterminated");
        ++length;
    } while (byte);
    if (length == 1) return fail(e, QA_ERROR_FORMAT, "Catalog source name is empty");
    char *text = malloc(length);
    if (!text) return fail(e, QA_ERROR_MEMORY, "Retaining original catalog string");
    if (!read_bytes(c, (uint32_t)pointer, text, length, e)) { free(text); return false; }
    *out = text; return true;
}
static void records_free(application_q3_catalog_record *records, size_t count)
{ for (size_t i = 0; i < count; ++i) { free((void *)records[i].class_name); free((void *)records[i].pickup_name); } free(records); }
static const char *const weapon_names[] = {NULL,"gauntlet","machinegun","shotgun","grenadelauncher","rocketlauncher","lightning","railgun","plasmagun","bfg","grapple","nailgun","proxlauncher","chaingun"};
static bool item_id(application_q3_catalog *c, const application_q3_catalog_record *record, bool weapon, qa_item_id *out, qa_error *e)
{
    if (weapon) for (size_t i = 0; i < c->selection_count; ++i)
        if (c->selection[i].value == record->tag) { *out = c->selection[i].item; return true; }
    size_t count; const qa_q3_item *stock = qa_q3_items(QA_Q3_TEAM_ARENA, &count);
    int32_t tag = 0;
    for (size_t i = 1; i < count; ++i)
        if (stock[i].kind == (weapon ? QA_Q3_ITEM_WEAPON : QA_Q3_ITEM_AMMO) && !strcmp(stock[i].classname, record->class_name)) { tag = stock[i].tag; break; }
    char digest[65]; qa_sha256_hex(qa_qvm_image_digest(c->image), digest);
    size_t capacity = strlen(record->class_name) + 96;
    char *text = malloc(capacity);
    if (!text) return fail(e, QA_ERROR_MEMORY, "Retaining original catalog identity");
    if (tag > 0 && (size_t)tag < sizeof(weapon_names) / sizeof(*weapon_names))
        snprintf(text, capacity, "q3:%s/%s", weapon ? "weapon" : "ammo", weapon_names[tag]);
    else snprintf(text, capacity, "q3:guest/sha256:%s/%s", digest, record->class_name);
    bool ok = qa_strings_intern_cstr(c->strings, text, out, e); free(text); return ok;
}
static bool stored(const application_q3_catalog *c, qa_item_id item)
{ for (size_t i = 0; i < c->stored_count; ++i) if (c->stored[i] == item) return true; return false; }
static bool refresh(application_q3_catalog *c, bool full_records, bool roster, qa_error *e)
{
    if (!c->table) return true;
    int32_t address = (int32_t)c->address.value, count = (int32_t)c->count.value;
    if ((c->address.global && !read_word(c, c->address.value, &address, e)) ||
        (c->count.global && !read_word(c, c->count.value, &count, e))) return false;
    size_t extent = c->live ? qa_qvm_memory_size(c->vm) : qa_qvm_image_initialized_data(c->image).size;
    if (address < 0 || address % 4 || count < 0 || (count && !address) ||
        (c->count.global && (uint32_t)count > c->count.maximum) || (uint32_t)address > extent ||
        (uint64_t)(uint32_t)count * c->stride > extent - (uint32_t)address ||
        (uint64_t)(uint32_t)address + (uint64_t)(uint32_t)count * c->stride > UINT32_MAX ||
        (size_t)count > SIZE_MAX / sizeof(application_q3_catalog_record) ||
        (size_t)count > SIZE_MAX / sizeof(application_q3_catalog_weapon))
        return fail(e, QA_ERROR_FORMAT, "Catalog table leaves original source data");
    application_q3_catalog_record *records = count ? calloc((size_t)count, sizeof(*records)) : NULL;
    application_q3_catalog_weapon *weapons = count ? calloc((size_t)count, sizeof(*weapons)) : NULL;
    if (count && (!records || !weapons)) { free(records); free(weapons); return fail(e, QA_ERROR_MEMORY, "Retaining original catalog records"); }
    size_t used = 0, weapon_count = 0; bool ok = true;
    for (int32_t i = 0; ok && i < count; ++i) {
        uint32_t at = (uint32_t)address + (uint32_t)i * c->stride; int32_t words[4];
        ok = read_word(c, at + c->fields[0], words, e);
        if (!ok || !words[0]) continue;
        ok = read_word(c, at + c->fields[2], words + 2, e);
        if (!ok || (!full_records && words[2] != c->weapon_type && words[2] != c->ammo_type)) continue;
        ok = read_word(c, at + c->fields[1], words + 1, e) && read_word(c, at + c->fields[3], words + 3, e);
        if (!ok) continue;
        application_q3_catalog_record *r = records + used++;
        *r = (application_q3_catalog_record){.index=(uint32_t)i,.address=at,.type=words[2],.tag=words[3]};
        ok = read_string(c, words[0], &r->class_name, e) && read_string(c, words[1], &r->pickup_name, e);
    }
    for (size_t i = 0; roster && ok && i < used; ++i) {
        application_q3_catalog_record *r = records + i;
        if (!c->live && (r->type == c->weapon_type || r->type == c->ammo_type) &&
            (r->tag < 1 || (!c->private_inventory && r->tag > 15))) {
            ok = fail(e, QA_ERROR_FORMAT, "Catalog item exceeds its original inventory representation");
            break;
        }
        if (r->type != c->weapon_type) continue;
        bool duplicate = false;
        for (size_t j = 0; j < weapon_count; ++j) if (weapons[j].weapon == r->tag) duplicate = true;
        if (duplicate) continue;
        application_q3_catalog_weapon *w = weapons + weapon_count;
        w->weapon = r->tag; w->label = r->pickup_name;
        ok = r->tag > 0 && (c->private_inventory || r->tag <= 15);
        if (!ok) { fail(e, QA_ERROR_FORMAT, "Catalog weapon exceeds its original inventory representation"); break; }
        ok = item_id(c, r, true, &w->item, e);
        for (size_t j = 0; ok && j < used; ++j) if (records[j].type == c->ammo_type && records[j].tag == r->tag) {
            ok = item_id(c, records + j, false, &w->ammo, e); break;
        }
        bool selected = !c->selected;
        for (size_t j = 0; j < c->selection_count; ++j)
            if (c->selection[j].value == w->weapon && c->selection[j].item == w->item) selected = true;
        if (ok && (!selected || (c->private_inventory && (!stored(c,w->item) || (w->ammo && !stored(c,w->ammo))))))
            ok = fail(e, QA_ERROR_FORMAT, "Catalog weapon lacks its declared selection or inventory storage");
        if (ok) ++weapon_count;
    }
    if (!ok) { records_free(records, used); free(weapons); return false; }
    bool same = full_records == c->records_full && used == c->record_count &&
        (!roster || weapon_count == c->weapon_count);
    for (size_t i = 0; same && i < used; ++i) {
        const application_q3_catalog_record *a = records + i, *b = c->records + i;
        same = a->index == b->index && a->address == b->address && a->type == b->type && a->tag == b->tag &&
            !strcmp(a->class_name, b->class_name) && !strcmp(a->pickup_name, b->pickup_name);
    }
    for (size_t i = 0; roster && same && i < weapon_count; ++i)
        same = weapons[i].weapon == c->weapons[i].weapon && weapons[i].item == c->weapons[i].item &&
            weapons[i].ammo == c->weapons[i].ammo;
    if (same) { records_free(records, used); free(weapons); return true; }
    records_free(c->records, c->record_count);
    c->records = records; c->record_count = used; c->records_full = full_records;
    if (roster) {
        free(c->weapons); c->weapons = weapons; c->weapon_count = weapon_count;
    } else {
        free(weapons);
        /* A subsequent live weapons read always revalidates the roster. The
         * immutable roster keeps names from this same newly admitted table. */
        for (size_t i = 0; i < c->weapon_count; ++i) {
            c->weapons[i].label = NULL;
            for (size_t j = 0; j < used; ++j)
                if (records[j].type == c->weapon_type && records[j].tag == c->weapons[i].weapon) {
                    c->weapons[i].label = records[j].pickup_name; break;
                }
        }
    }
    return true;
}
bool application_q3_catalog_current(const application_q3_catalog *c, const qa_qvm_image *image, const qa_qvm *vm, qa_qvm_abi abi)
{
    return c && image && vm && c->image == image && c->vm == vm && c->abi == abi &&
        qa_qvm_get_role(vm) == QA_QVM_GAME && qa_qvm_get_abi(vm) == abi &&
        qa_sha256_equal(qa_qvm_digest(vm), qa_qvm_image_digest(image)) &&
        qa_qvm_read(vm, 0, NULL, 0, NULL);
}
bool application_q3_catalog_role_current(const application_q3_catalog *c, const q3g_role *role)
{
    return c && role && (c->native ? c->native_role == role &&
        application_q3_native_catalog_current(c->native, role) :
        application_q3_catalog_current(c, role->image, role->vm, role->abi));
}
bool application_q3_catalog_create_native(q3g_role *role, qa_bytes declaration,
    application_q3_catalog **out, qa_error *e)
{
    if (!out || *out || !role || !declaration.size)
        return fail(e, QA_ERROR_ARGUMENT, "Native catalog requires its retained declaration and empty owner");
    application_q3_catalog *c = calloc(1, sizeof(*c));
    if (!c) return fail(e, QA_ERROR_MEMORY, "Retaining native GAME catalog");
    c->native_role = role;
    if (!application_q3_native_catalog_create(role, declaration, &c->native, e)) {
        free(c); return false;
    }
    *out = c; return true;
}
bool application_q3_catalog_create(qa_qvm_image *image, qa_qvm *vm, qa_qvm_abi abi,
    qa_strings *strings, qa_bytes declared, qa_bytes items, bool missionpack,
    application_q3_catalog **out, qa_error *e)
{
    if (!image || !vm || !strings || !out || *out || (declared.size && !declared.data) || (items.size && !items.data) ||
        qa_qvm_get_role(vm) != QA_QVM_GAME || qa_qvm_get_abi(vm) != abi || !qa_sha256_equal(qa_qvm_digest(vm), qa_qvm_image_digest(image)))
        return fail(e, QA_ERROR_ARGUMENT, "Catalog requires its actual GAME image, executor and empty owner");
    if (!qa_qvm_read(vm, 0, NULL, 0, e)) return false;
    application_q3_catalog *c = calloc(1, sizeof(*c));
    if (!c) return fail(e, QA_ERROR_MEMORY, "Retaining original source catalog");
    qa_qvm_image_retain(image); c->image = image; c->vm = vm; c->abi = abi; c->strings = strings;
    qa_json_document *doc = NULL; bool ok = true;
    if (declared.size) ok = qa_json_parse(declared, &doc, e) && primary(c, doc, e);
    else if (items.size) {
        uint32_t version; qa_buffer digest = {0}; qa_sha256_digest expected;
        ok = qa_json_parse(items, &doc, e) &&
            word(doc, qa_json_get(doc, qa_json_root(doc), "version"), &version, e) && version == 1 &&
            qa_json_string(doc, qa_json_get(doc, qa_json_root(doc), "artifactDigest"), &digest, e);
        if (ok) ok = !memchr(digest.data, 0, digest.size) &&
            qa_sha256_parse((char *)digest.data, &expected, e) &&
            qa_sha256_equal(&expected, qa_qvm_image_digest(image));
        qa_buffer_free(&digest);
        if (ok) ok = layout(c, doc, qa_json_get(doc, qa_json_root(doc), "items"), e) && !c->live;
        if (!ok && (!e || !e->code)) fail(e, QA_ERROR_FORMAT,
            "Item declaration is not an immutable matching artifact receipt");
    } else {
        char digest[65]; qa_sha256_hex(qa_qvm_image_digest(image), digest);
        if (!strcmp(digest, "9751bad99a2d138f96a9b0436d2ea2d965b86214175dc33e4cea95e059419337")) {
            c->table = true; c->address.value = 5356; c->count.value = 49; c->stride = 52;
            c->fields[1] = 28; c->fields[2] = 36; c->fields[3] = 40;
            c->weapon_type = 1; c->ammo_type = 2;
        } else {
            c->weapon_count = missionpack ? 13 : 10;
            c->weapons = calloc(c->weapon_count, sizeof(*c->weapons));
            ok = c->weapons != NULL;
            if (!ok) fail(e, QA_ERROR_MEMORY, "Retaining source default weapon roster");
            size_t count; const qa_q3_item *stock = qa_q3_items(QA_Q3_TEAM_ARENA, &count);
            for (size_t i = 0; ok && i < c->weapon_count; ++i) {
                uint32_t slot = (uint32_t)i + 1; char text[64];
                snprintf(text, sizeof(text), "q3:weapon/%s", weapon_names[slot]);
                application_q3_catalog_weapon *w = c->weapons + i; w->weapon = (int32_t)slot;
                ok = qa_strings_intern_cstr(strings, text, &w->item, e);
                if (ok && slot != 1 && slot != 10) {
                    snprintf(text, sizeof(text), "q3:ammo/%s", weapon_names[slot]);
                    ok = qa_strings_intern_cstr(strings, text, &w->ammo, e);
                }
                for (size_t j = 1; j < count; ++j)
                    if (stock[j].kind == QA_Q3_ITEM_WEAPON && stock[j].tag == (int32_t)slot) {
                        w->label = stock[j].name; break;
                    }
            }
        }
    }
    qa_json_destroy(doc);
    if (ok && !declared.size && abi == QA_QVM_Q3_MODERN) {
        char digest[65]; qa_sha256_hex(qa_qvm_image_digest(image), digest);
        if (!strcmp(digest, "57c52bf22e4f528c064f8af1553a7103723bab0a02276bb11eed944bf829b219")) {
            c->source_address = 2552; c->source_count = 36;
        } else if (!strcmp(digest, "9751bad99a2d138f96a9b0436d2ea2d965b86214175dc33e4cea95e059419337")) {
            c->source_address = 5304; c->source_count = 50;
        }
    }
    if (ok && c->table && !c->live) ok = refresh(c, false, true, e);
    if (!ok) { application_q3_catalog_destroy(c); return false; }
    *out = c; return true;
}
void application_q3_catalog_destroy(application_q3_catalog *c)
{
    if (!c) return;
    application_q3_native_catalog_destroy(c->native);
    records_free(c->records, c->record_count);
    records_free(c->source_records, c->source_record_count);
    free(c->weapons); free(c->inventory); free(c->selection); free(c->stored);
    qa_qvm_image_release(c->image); free(c);
}
bool application_q3_catalog_records(application_q3_catalog *c,
    const application_q3_catalog_record **out, size_t *count, qa_error *e)
{
    if (c && c->native) return application_q3_native_catalog_records(c->native, out, count, e);
    if (!out || !count || !c || !application_q3_catalog_current(c, c->image, c->vm, c->abi) ||
        (!c->table && !c->source_count))
        return fail(e, QA_ERROR_ARGUMENT, "Catalog records require their actual retained GAME table");
    if (c->source_count) {
        if (!c->source_records_ready) {
            application_q3_catalog source = {
                .image=c->image, .vm=c->vm, .abi=c->abi,
                .address={.value=c->source_address}, .count={.value=c->source_count},
                .stride=52, .fields={0,28,36,40}, .weapon_type=1, .ammo_type=2, .table=true
            };
            if (!refresh(&source, true, false, e)) return false;
            c->source_records = source.records; c->source_record_count = source.record_count;
            c->source_records_ready = true;
        }
        *out = c->source_records; *count = c->source_record_count; return true;
    }
    if ((c->live || !c->records_full) && !refresh(c, true, false, e)) return false;
    *out = c->records; *count = c->record_count; return true;
}
bool application_q3_catalog_weapons(application_q3_catalog *c,
    const application_q3_catalog_weapon **out, size_t *count, qa_error *e)
{
    if (c && c->native) return application_q3_native_catalog_weapons(c->native, out, count, e);
    if (!out || !count || !c || !application_q3_catalog_current(c, c->image, c->vm, c->abi))
        return fail(e, QA_ERROR_ARGUMENT, "Catalog weapons require their actual retained GAME owner");
    if (c->live && !refresh(c, true, true, e)) return false;
    *out = c->weapons; *count = c->weapon_count; return true;
}
bool application_q3_catalog_standard(const application_q3_catalog *c)
{ return c && !c->native && !c->table; }
bool application_q3_catalog_ammo_label(application_q3_catalog *c, qa_item_id item,
    const char **out, qa_error *e)
{
    if (c && c->native) return application_q3_native_catalog_ammo_label(c->native, item, out, e);
    if (!c || !item || !out || !application_q3_catalog_current(c, c->image, c->vm, c->abi))
        return fail(e, QA_ERROR_ARGUMENT, "Ammo label requires its actual retained GAME catalog");
    const application_q3_catalog_record *records;
    size_t count;
    int32_t ammo_type;
    if (c->source_count) {
        if (!application_q3_catalog_records(c, &records, &count, e)) return false;
        /* source_count is installed only with the qualified SDK item layout. */
        ammo_type = 2;
    } else if (c->table) {
        if ((c->live || !c->record_count) && !refresh(c, false, false, e)) return false;
        records = c->records; count = c->record_count; ammo_type = c->ammo_type;
    } else return fail(e, QA_ERROR_NOT_FOUND, "GAME catalog has no retained Source ammo records");
    for (size_t i = 0; i < count; ++i) {
        if (records[i].type != ammo_type) continue;
        qa_item_id actual;
        if (!item_id(c, records + i, false, &actual, e)) return false;
        if (actual == item) { *out = records[i].pickup_name; return true; }
    }
    return fail(e, QA_ERROR_NOT_FOUND, "Ammo item is absent from the actual Source catalog");
}
bool application_q3_catalog_inventory_read(void *context, q3g_role *role,
    const guest_inventory_weapon **out, size_t *count, qa_error *e)
{
    application_q3_catalog *c = context;
    if (!role || role->kind != QA_QVM_GAME || !out || !count ||
        !application_q3_catalog_current(c, role->image, role->vm, role->abi))
        return fail(e, QA_ERROR_ARGUMENT, "Inventory catalog belongs to another physical GAME executor");
    const application_q3_catalog_weapon *weapons; size_t used;
    if (!application_q3_catalog_weapons(c, &weapons, &used, e)) return false;
    bool same = used == c->inventory_count;
    for (size_t i = 0; same && i < used; ++i)
        same = c->inventory[i].weapon == (uint32_t)weapons[i].weapon &&
            c->inventory[i].item == weapons[i].item && c->inventory[i].ammo == weapons[i].ammo;
    if (!same) {
        if (used > SIZE_MAX / sizeof(*c->inventory)) return fail(e, QA_ERROR_MEMORY, "Inventory catalog is too large");
        guest_inventory_weapon *next = used ? malloc(used * sizeof(*next)) : NULL;
        if (used && !next) return fail(e, QA_ERROR_MEMORY, "Retaining source inventory catalog projection");
        for (size_t i = 0; i < used; ++i)
            next[i] = (guest_inventory_weapon){(uint32_t)weapons[i].weapon, weapons[i].item, weapons[i].ammo};
        free(c->inventory); c->inventory = next; c->inventory_count = used;
    }
    *out = c->inventory; *count = c->inventory_count; return true;
}
