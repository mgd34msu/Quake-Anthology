#include "guest_native_q2_private.h"
#include "qa/json.h"
#include <math.h>

typedef struct q2_inventory_row {
    qa_item_id item;
    uint32_t index, capacity_offset;
    int32_t fixed_capacity;
    uint8_t capacity_bytes;
} q2_inventory_row;
struct application_native_q2_inventory {
    qa_json_document *document;
    qa_json_id world;
    q2_inventory_row *rows;
    size_t count;
    uint32_t client_pointer, inventory_offset, client_bytes, source_count;
    uint8_t pointer_bytes;
    bool resolved;
};

static bool word(const qa_json_document *doc, qa_json_id object, const char *key,
    uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(doc, qa_json_get(doc, object, key), &value, error)) return false;
    if (value > UINT32_MAX) return application_fail(error, QA_ERROR_FORMAT, "Native Q2 inventory field exceeds its source word");
    *out = (uint32_t)value; return true;
}
static bool item_name(struct application_native_q2 *engine, qa_json_id object,
    const char *key, qa_item_id *out, qa_error *error)
{
    qa_buffer text = {0};
    bool ok = qa_json_string(engine->primary_inventory->document,
        qa_json_get(engine->primary_inventory->document, object, key), &text, error);
    if (ok && (!text.size || memchr(text.data, 0, text.size) || !memchr(text.data, ':', text.size)))
        ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 inventory item is not namespaced");
    if (ok) ok = qa_strings_intern_cstr(qa_session_strings(engine->provider->application->session),
        (const char *)text.data, out, error);
    qa_buffer_free(&text); return ok;
}
static bool layout_field(const qa_json_document *doc, qa_json_id layout, const char *name,
    const char *storage, uint32_t count, uint32_t *offset, qa_error *error)
{
    qa_json_id fields = qa_json_get(doc, layout, "fields");
    if (qa_json_type(doc, fields) != QA_JSON_ARRAY)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 client layout has no source fields");
    bool found = false; uint32_t bytes;
    if (!word(doc, layout, "byteLength", &bytes, error)) return false;
    for (size_t i = 0; i < qa_json_size(doc, fields); ++i) {
        qa_json_id field = qa_json_at(doc, fields, i);
        if (!qa_json_string_equal(doc, qa_json_get(doc, field, "name"), name)) continue;
        uint32_t actual;
        if (found || !qa_json_string_equal(doc, qa_json_get(doc, field, "storage"), storage) ||
            !word(doc, field, "count", &actual, error) || actual != count ||
            !word(doc, field, "byteOffset", offset, error) ||
            *offset > bytes || (uint64_t)count * (!strcmp(storage, "int16") ? 2u : 4u) > bytes - *offset)
            return application_fail(error, QA_ERROR_FORMAT, "Native Q2 client field differs from its declared source layout");
        found = true;
    }
    return found || application_fail(error, QA_ERROR_FORMAT, "Native Q2 client source field is missing");
}

bool application_native_q2_inventory_prepare(struct application_native_q2 *engine, qa_error *error)
{
    if (!engine->declaration || engine->profile == QA_NATIVE_Q2_CGAME_API2023) return true;
    struct application_native_q2_inventory *p = calloc(1, sizeof(*p));
    if (!p) return application_fail(error, QA_ERROR_MEMORY, "Preparing native Q2 source inventory");
    engine->primary_inventory = p;
    if (!qa_json_parse(qa_native_declaration_primary(engine->declaration), &p->document, error)) return false;
    const qa_json_document *doc = p->document;
    qa_json_id root = qa_json_root(doc), inventory = qa_json_get(doc, root, "inventory");
    p->world = qa_json_get(doc, root, "world");
    qa_native_module_info info = qa_native_module_describe(engine->provider->state.native.module);
    p->pointer_bytes = info.image.target.pointer_bytes;
    uint32_t source_count;
    if (!word(doc, inventory, "client", &p->client_pointer, error) ||
        !word(doc, inventory, "inventory", &p->inventory_offset, error) ||
        !word(doc, inventory, "count", &source_count, error)) return false;
    uint32_t expected = engine->profile == QA_NATIVE_Q2_GAME_API2023 ? 120u : p->pointer_bytes == 4 ? 84u : 88u;
    if (p->client_pointer != expected || !source_count || source_count > 65536)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 inventory changes its public client pointer or source extent");
    p->source_count = source_count;
    qa_json_id client = qa_json_get(doc, p->world, "client");
    uint32_t count;
    if (!word(doc, client, "inventoryCount", &count, error)) return false;
    if (count != source_count)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 primary sections disagree about their inventory extent");
    if (engine->profile == QA_NATIVE_Q2_GAME_API3) {
        qa_json_id weapons = qa_json_get(doc, root, "weapons"), table = qa_json_get(doc, p->world, "inventoryTable");
        uint32_t actual;
        if (!word(doc, qa_json_get(doc, weapons, "client"), "byteLength", &p->client_bytes, error) ||
            !word(doc, client, "inventory", &actual, error) ||
            !word(doc, table, "count", &count, error)) return false;
        if (actual != p->inventory_offset)
            return application_fail(error, QA_ERROR_FORMAT, "Native Q2 world and inventory offsets disagree");
    } else {
        qa_json_id layout = qa_json_get(doc, client, "layout"); uint32_t actual;
        if (!word(doc, layout, "byteLength", &p->client_bytes, error) ||
            !layout_field(doc, layout, "pers.inventory", "int32", count, &actual, error)) return false;
        if (actual != p->inventory_offset)
            return application_fail(error, QA_ERROR_FORMAT, "Native KEX world and inventory offsets disagree");
        qa_json_id rows = qa_json_get(doc, p->world, "inventory");
        if (qa_json_type(doc, rows) != QA_JSON_ARRAY || qa_json_size(doc, rows) != count)
            return application_fail(error, QA_ERROR_FORMAT, "Native KEX inventory requires its complete declared roster");
    }
    if (!count || count > source_count || p->inventory_offset > p->client_bytes ||
        (uint64_t)source_count * 4 > p->client_bytes - p->inventory_offset)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 inventory exceeds its declared client record");
    p->count = count; p->rows = calloc(count, sizeof(*p->rows));
    return p->rows != NULL || application_fail(error, QA_ERROR_MEMORY, "Retaining native Q2 inventory roster");
}

static bool pointer_read(struct application_native_q2 *engine, qa_native_address at,
    qa_native_address *out, qa_error *error)
{
    uint8_t bytes[8]; uint8_t size = engine->primary_inventory->pointer_bytes;
    if (!qa_native_read(qa_native_host_instance(engine->provider->state.native.host), at, bytes, size, error)) return false;
    *out = size == 4 ? qa_load_u32le(bytes) : qa_load_u64le(bytes); return true;
}
static bool scalar_read(struct application_native_q2 *engine, qa_native_address at,
    uint8_t size, int32_t *out, qa_error *error)
{
    uint8_t bytes[4];
    if (!qa_native_read(qa_native_host_instance(engine->provider->state.native.host), at, bytes, size, error)) return false;
    *out = size == 2 ? (int16_t)qa_load_u16le(bytes) : qa_load_i32le(bytes); return true;
}
static bool string_read(struct application_native_q2 *engine, qa_native_address at,
    qa_buffer *out, qa_error *error)
{
    if (!at) { out->data = calloc(1, 1); out->size = 0; return out->data != NULL || application_fail(error, QA_ERROR_MEMORY, "Retaining native item name"); }
    return qa_native_read_string(qa_native_host_instance(engine->provider->state.native.host), at, 1024, out, error);
}
static bool descriptor_name(struct application_native_q2 *engine, uint32_t rva,
    uint32_t stride, uint32_t index, uint32_t offset, qa_buffer *out, qa_error *error)
{
    struct application_native_q2_inventory *p = engine->primary_inventory;
    if (!stride || offset > stride || p->pointer_bytes > stride - offset)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 item pointer exceeds its descriptor");
    qa_native_address at, text;
    return qa_native_rva(qa_native_host_instance(engine->provider->state.native.host),
        (uint64_t)rva + (uint64_t)index * stride + offset, p->pointer_bytes, &at, error) &&
        pointer_read(engine, at, &text, error) && string_read(engine, text, out, error);
}

static bool resolve_classic(struct application_native_q2 *engine, qa_error *error)
{
    struct application_native_q2_inventory *p = engine->primary_inventory;
    const qa_json_document *doc = p->document;
    qa_json_id table = qa_json_get(doc, p->world, "inventoryTable"), globals = qa_json_get(doc, p->world, "globals");
    uint32_t rva, stride, classname, label, flags, tag, ammo, empty;
    if (!word(doc, globals, "itemList", &rva, error) || !word(doc, globals, "itemBytes", &stride, error) ||
        !word(doc, table, "className", &classname, error) || !word(doc, table, "label", &label, error) ||
        !word(doc, table, "flags", &flags, error) || !word(doc, table, "tag", &tag, error) ||
        !word(doc, table, "ammoFlag", &ammo, error) || !word(doc, table, "emptyIndex", &empty, error)) return false;
    if (stride < 4 || flags > stride - 4 || tag > stride - 4 || empty >= p->count || !ammo)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 item descriptor is invalid");
    for (uint32_t i = 0; i < p->count; ++i) {
        qa_buffer name = {0}, display = {0}; bool ok = descriptor_name(engine, rva, stride, i, classname, &name, error) &&
            descriptor_name(engine, rva, stride, i, label, &display, error);
        q2_inventory_row *row = &p->rows[i]; row->index = i;
        if (ok && !name.size) {
            qa_json_id unnamed = qa_json_get(doc, table, "unnamed"); bool matched = false;
            for (size_t j = 0; j < qa_json_size(doc, unnamed); ++j) {
                qa_json_id candidate = qa_json_at(doc, unnamed, j); uint32_t index;
                if (!word(doc, candidate, "index", &index, error)) { ok = false; break; }
                if (index != i || !qa_json_string_equal(doc, qa_json_get(doc, candidate, "label"), (char *)display.data)) continue;
                if (matched) { ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 unnamed item repeats its source slot"); break; }
                matched = true; ok = item_name(engine, candidate, "item", &row->item, error);
            }
            if (ok && !matched) ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 item has no qualified identity");
        } else if (ok) {
            for (size_t j = 0; j < name.size; ++j)
                if ((j == 0 && (name.data[j] < 'a' || name.data[j] > 'z')) ||
                    (j && !((name.data[j] >= 'a' && name.data[j] <= 'z') ||
                        (name.data[j] >= '0' && name.data[j] <= '9') || name.data[j] == '_')))
                    ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 classname is not qualified");
            char qualified[1030];
            if (ok) { snprintf(qualified, sizeof(qualified), "q2:%s", (char *)name.data);
                ok = qa_strings_intern_cstr(qa_session_strings(engine->provider->application->session), qualified, &row->item, error); }
        }
        qa_buffer_free(&name); qa_buffer_free(&display); if (!ok) return false;
        qa_native_address at; int32_t bits, source_tag;
        if (!qa_native_rva(qa_native_host_instance(engine->provider->state.native.host), (uint64_t)rva + (uint64_t)i * stride + flags, 4, &at, error) ||
            !scalar_read(engine, at, 4, &bits, error)) return false;
        row->fixed_capacity = i == empty ? 0 : INT32_MAX;
        if ((uint32_t)bits & ammo) {
            if (!qa_native_rva(qa_native_host_instance(engine->provider->state.native.host), (uint64_t)rva + (uint64_t)i * stride + tag, 4, &at, error) ||
                !scalar_read(engine, at, 4, &source_tag, error)) return false;
            qa_json_id capacities = qa_json_get(doc, table, "capacities"); uint64_t offset;
            if (source_tag < 0 || !qa_json_u64(doc, qa_json_at(doc, capacities, (size_t)source_tag), &offset, error) ||
                offset > p->client_bytes || p->client_bytes - offset < 4)
                return application_fail(error, QA_ERROR_FORMAT, "Native Q2 ammo capacity exceeds its declared client");
            row->capacity_offset = (uint32_t)offset; row->capacity_bytes = 4;
        }
    }
    bool sentinel;
    if (!qa_json_bool(doc, qa_json_get(doc, table, "sentinel"), &sentinel, error)) return false;
    if (sentinel) {
        qa_native_address at; qa_buffer bytes = {.data = malloc(stride), .size = stride};
        if (!bytes.data) return application_fail(error, QA_ERROR_MEMORY, "Inspecting native Q2 item sentinel");
        bool ok = qa_native_rva(qa_native_host_instance(engine->provider->state.native.host), (uint64_t)rva + (uint64_t)p->count * stride, stride, &at, error) &&
            qa_native_read(qa_native_host_instance(engine->provider->state.native.host), at, bytes.data, bytes.size, error);
        for (size_t i = 0; ok && i < bytes.size; ++i) if (bytes.data[i]) ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 item table exceeds its qualified roster");
        qa_buffer_free(&bytes); if (!ok) return false;
    }
    return true;
}

static bool resolve_kex(struct application_native_q2 *engine, qa_error *error)
{
    struct application_native_q2_inventory *p = engine->primary_inventory;
    const qa_json_document *doc = p->document;
    qa_json_id rows = qa_json_get(doc, p->world, "inventory"), client = qa_json_get(doc, p->world, "client");
    qa_json_id commands = qa_json_get(doc, qa_json_root(doc), "commands"), items = qa_json_get(doc, commands, "items");
    uint32_t ammo_count, ammo_offset, rva, stride, classname, descriptor_count;
    if (!word(doc, client, "ammoCount", &ammo_count, error) ||
        !layout_field(doc, qa_json_get(doc, client, "layout"), "pers.max_ammo", "int16", ammo_count, &ammo_offset, error) ||
        !word(doc, items, "table", &rva, error) || !word(doc, items, "stride", &stride, error) ||
        !word(doc, items, "classname", &classname, error) || !word(doc, items, "count", &descriptor_count, error)) return false;
    if (!descriptor_count || descriptor_count > p->count)
        return application_fail(error, QA_ERROR_FORMAT, "Native KEX descriptor roster exceeds its source inventory");
    bool *assigned = calloc(p->count, sizeof(*assigned));
    if (!assigned) return application_fail(error, QA_ERROR_MEMORY, "Resolving native KEX source item indices");
    bool ok = true; size_t remaining = SIZE_MAX;
    for (size_t i = 0; ok && i < p->count; ++i) {
        qa_json_id declaration = qa_json_at(doc, rows, i), source = qa_json_get(doc, declaration, "source");
        qa_json_id capacity = qa_json_get(doc, declaration, "capacity");
        q2_inventory_row *row = &p->rows[i];
        ok = item_name(engine, declaration, "item", &row->item, error);
        if (!ok) break;
        if (qa_json_string_equal(doc, qa_json_get(doc, source, "kind"), "remaining")) {
            if (remaining != SIZE_MAX) { ok = application_fail(error, QA_ERROR_FORMAT, "Native KEX inventory repeats its remaining index"); break; }
            remaining = i;
        } else {
            uint32_t index = UINT32_MAX;
            if (qa_json_string_equal(doc, qa_json_get(doc, source, "kind"), "index"))
                ok = word(doc, source, "index", &index, error);
            else if (qa_json_string_equal(doc, qa_json_get(doc, source, "kind"), "classname")) {
                for (uint32_t j = 0; ok && j < descriptor_count; ++j) {
                    qa_buffer name = {0}; ok = descriptor_name(engine, rva, stride, j, classname, &name, error);
                    if (ok && qa_json_string_equal(doc, qa_json_get(doc, source, "name"), (char *)name.data)) {
                        if (index != UINT32_MAX) ok = application_fail(error, QA_ERROR_FORMAT, "Native KEX classname has ambiguous source slots");
                        index = j;
                    }
                    qa_buffer_free(&name);
                }
            } else ok = application_fail(error, QA_ERROR_FORMAT, "Native KEX item has no admitted index producer");
            if (ok && (index >= p->count || assigned[index])) ok = application_fail(error, QA_ERROR_FORMAT, "Native KEX item index is absent or duplicated");
            if (ok) { row->index = index; assigned[index] = true; }
        }
        if (!ok) break;
        if (qa_json_string_equal(doc, qa_json_get(doc, capacity, "kind"), "ammo")) {
            uint32_t index;
            ok = word(doc, capacity, "sourceIndex", &index, error);
            if (ok && index >= ammo_count) ok = application_fail(error, QA_ERROR_FORMAT, "Native KEX ammo index exceeds its original capacities");
            if (ok) { row->capacity_offset = ammo_offset + index * 2; row->capacity_bytes = 2; }
        } else if (qa_json_string_equal(doc, qa_json_get(doc, capacity, "kind"), "fixed")) {
            uint32_t fixed; ok = word(doc, capacity, "count", &fixed, error);
            if (ok && fixed > INT32_MAX) ok = application_fail(error, QA_ERROR_FORMAT, "Native KEX fixed capacity exceeds source int32");
            if (ok) row->fixed_capacity = (int32_t)fixed;
        } else ok = application_fail(error, QA_ERROR_FORMAT, "Native KEX capacity producer is unknown");
    }
    if (ok && remaining != SIZE_MAX) {
        uint32_t index = UINT32_MAX;
        for (uint32_t i = 0; i < p->count; ++i) if (!assigned[i]) {
            if (index != UINT32_MAX) { ok = application_fail(error, QA_ERROR_FORMAT, "Native KEX remaining index is ambiguous"); break; }
            index = i;
        }
        if (ok && index == UINT32_MAX) ok = application_fail(error, QA_ERROR_FORMAT, "Native KEX remaining index is missing");
        if (ok) { assigned[index] = true; p->rows[remaining].index = index; }
    }
    for (size_t i = 0; ok && i < p->count; ++i)
        if (!assigned[i]) ok = application_fail(error, QA_ERROR_FORMAT, "Native KEX inventory does not cover its original storage");
    free(assigned); return ok;
}

static bool resolve(struct application_native_q2 *engine, qa_error *error)
{
    struct application_native_q2_inventory *p = engine->primary_inventory;
    if (p->resolved) return true;
    bool ok = engine->profile == QA_NATIVE_Q2_GAME_API3 ? resolve_classic(engine, error) : resolve_kex(engine, error);
    for (size_t i = 0; ok && i < p->count; ++i) {
        const q2_inventory_row *row = &p->rows[i];
        for (size_t j = 0; j < i; ++j)
            if (row->item == p->rows[j].item) ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 inventory repeats a canonical item");
        if (row->capacity_bytes && row->capacity_offset < p->inventory_offset + p->source_count * 4 &&
            row->capacity_offset + row->capacity_bytes > p->inventory_offset)
            ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 capacity aliases source count storage");
    }
    if (ok) p->resolved = true;
    return ok;
}

bool application_native_q2_inventory_index(struct application_native_q2 *engine,
    qa_item_id item, uint32_t *out, qa_error *error)
{
    if (!engine || !engine->primary_inventory || !item || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native source item lookup is missing its qualified inventory");
    if (!resolve(engine, error)) return false;
    struct application_native_q2_inventory *p = engine->primary_inventory;
    for (size_t i = 0; i < p->count; ++i)
        if (p->rows[i].item == item) { *out = p->rows[i].index; return true; }
    return application_fail(error, QA_ERROR_NOT_FOUND, "Native source item is absent from its declared inventory");
}

bool application_native_q2_inventory_item(struct application_native_q2 *engine,
    uint32_t index, qa_item_id *out, qa_error *error)
{
    if (!engine || !engine->primary_inventory || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native source index lookup is missing its qualified inventory");
    if (!resolve(engine, error)) return false;
    struct application_native_q2_inventory *p = engine->primary_inventory;
    for (size_t i = 0; i < p->count; ++i)
        if (p->rows[i].index == index) { *out = p->rows[i].item; return true; }
    return application_fail(error, QA_ERROR_NOT_FOUND, "Native source index is absent from its declared inventory");
}
static bool client_address(application_native_q2_client *client, qa_native_address *out, qa_error *error)
{
    struct application_native_q2 *engine = client->inventory_engine;
    if (!engine || !engine->provider->state.native.host ||
        !qa_actors_get(qa_session_actors(engine->provider->application->session), client->actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 inventory actor generation retired");
    qa_native_instance *instance = qa_native_host_instance(engine->provider->state.native.host);
    qa_native_slot_binding binding; qa_native_address entity;
    if (!qa_native_slot(instance, client->inventory_slot, &binding, error)) return false;
    if (!qa_actor_id_equal(binding.actor, client->actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 inventory source slot changed its actor generation");
    if (!qa_native_entity_address(instance, client->inventory_slot, &entity, error) ||
        !pointer_read(engine, entity + engine->primary_inventory->client_pointer, out, error)) return false;
    return *out != 0 || application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 inventory client pointer is absent");
}
static size_t inventory_count(void *opaque)
{
    return ((application_native_q2_client *)opaque)->inventory_engine->primary_inventory->count;
}
static bool inventory_at(void *opaque, size_t index, qa_inventory_entry *out, qa_error *error)
{
    application_native_q2_client *client = opaque; struct application_native_q2 *engine = client->inventory_engine;
    struct application_native_q2_inventory *p = engine->primary_inventory;
    if (index >= p->count) return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 inventory index exceeds its admitted roster");
    ++engine->calls; qa_native_address base; int32_t count, capacity = p->rows[index].fixed_capacity;
    const q2_inventory_row *row = &p->rows[index];
    bool ok = client_address(client, &base, error) && scalar_read(engine, base + p->inventory_offset + row->index * 4, 4, &count, error);
    if (ok && row->capacity_bytes) ok = scalar_read(engine, base + row->capacity_offset, row->capacity_bytes, &capacity, error);
    if (ok && capacity < 0) ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 original capacity is negative");
    if (ok) *out = (qa_inventory_entry){row->item, count, capacity, QA_COUNT_SOURCE_INT32};
    --engine->calls; return ok;
}
static bool scalar_write(struct application_native_q2 *engine, qa_native_address at,
    uint8_t size, int32_t value, qa_error *error)
{
    uint8_t bytes[4]; qa_store_u32le(bytes, (uint32_t)value);
    return qa_native_write(qa_native_host_instance(engine->provider->state.native.host), at, (qa_bytes){bytes, size}, error);
}
static bool inventory_write(void *opaque, const qa_inventory_entry *entry, qa_error *error)
{
    application_native_q2_client *client = opaque; struct application_native_q2 *engine = client->inventory_engine;
    struct application_native_q2_inventory *p = engine->primary_inventory; const q2_inventory_row *row = NULL;
    for (size_t i = 0; i < p->count; ++i) if (p->rows[i].item == entry->item) { row = &p->rows[i]; break; }
    if (!row || entry->policy != QA_COUNT_SOURCE_INT32 || !isfinite(entry->count) || floor(entry->count) != entry->count ||
        entry->count < INT32_MIN || entry->count > INT32_MAX || !isfinite(entry->capacity) || floor(entry->capacity) != entry->capacity ||
        entry->capacity < 0 || entry->capacity > (row->capacity_bytes == 2 ? INT16_MAX : INT32_MAX) ||
        (!row->capacity_bytes && entry->capacity != row->fixed_capacity))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 inventory write exceeds its original representation");
    ++engine->calls; qa_native_address base;
    bool ok = client_address(client, &base, error);
    if (ok && row->capacity_bytes) ok = scalar_write(engine, base + row->capacity_offset, row->capacity_bytes, (int32_t)entry->capacity, error);
    if (ok) ok = scalar_write(engine, base + p->inventory_offset + row->index * 4, 4, (int32_t)entry->count, error);
    --engine->calls; return ok;
}
static bool inventory_mutable(void *opaque, qa_item_id item)
{
    struct application_native_q2_inventory *p = ((application_native_q2_client *)opaque)->inventory_engine->primary_inventory;
    for (size_t i = 0; i < p->count; ++i) if (p->rows[i].item == item) return p->rows[i].capacity_bytes != 0;
    return false;
}

bool application_native_q2_inventory_admit(struct application_native_q2 *engine, uint32_t slot, qa_error *error)
{
    application_native_q2_client *client = &engine->clients[slot];
    if (application_provider_for(engine->provider->application, client->actor, QA_ROLE_INVENTORY, NULL) != engine->provider) return true;
    if (!engine->primary_inventory) return application_fail(error, QA_ERROR_UNSUPPORTED, "Native Q2 primary inventory requires its artifact-qualified world profile");
    if (!resolve(engine, error)) return false;
    client->inventory_engine = engine; client->inventory_slot = slot;
    if (client->inventory_bound) {
        if (client->inventory_prepared && !qa_inventory_primary_current(
            engine->provider->application->inventory, client->inventory_lease, client))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 inventory restore claim has not been published");
        client->inventory_prepared = false;
        return true;
    }
    qa_inventory_binding binding = {client, inventory_count, inventory_at, inventory_write, inventory_mutable};
    if (!qa_inventory_adopt_primary(engine->provider->application->inventory, client->actor, &binding, &client->inventory_lease, error)) return false;
    client->inventory_bound = true; client->inventory_prepared = false; return true;
}
bool application_native_q2_inventory_detach(struct application_native_q2 *engine, uint32_t slot, qa_error *error)
{
    application_native_q2_client *client = &engine->clients[slot];
    if (!client->inventory_bound) return true;
    if (client->inventory_prepared && !qa_inventory_primary_current(
        engine->provider->application->inventory, client->inventory_lease, client)) {
        client->inventory_bound = false; client->inventory_prepared = false;
        client->inventory_lease = (qa_inventory_lease){0};
        return true;
    }
    if (qa_actors_get(qa_session_actors(engine->provider->application->session), client->actor) &&
        !qa_inventory_detach_primary(engine->provider->application->inventory, client->inventory_lease, client, error)) return false;
    client->inventory_bound = false; client->inventory_prepared = false;
    client->inventory_lease = (qa_inventory_lease){0}; return true;
}

bool application_native_q2_inventory_binding(application_provider *provider, qa_actor_id actor,
    uint64_t saved_serial, qa_inventory_binding *out, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || !engine->primary_inventory || !provider->state.native.host || !out ||
        !saved_serial || !engine->map_ready ||
        application_provider_for(provider->application, actor, QA_ROLE_INVENTORY, NULL) != provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Restored native Q2 inventory requires its live qualified source owner");
    application_native_q2_client *client = NULL;
    uint32_t slot = 0;
    for (uint32_t i = 1; i < 257; ++i) {
        application_native_q2_client *candidate = &engine->clients[i];
        if (candidate->reserved && candidate->connected && candidate->begun &&
            qa_actor_id_equal(candidate->actor, actor)) { client = candidate; slot = i; break; }
    }
    if (!client) return application_fail(error, QA_ERROR_NOT_FOUND, "Restored native Q2 inventory client is absent");
    if (client->inventory_bound && (client->inventory_lease.serial != saved_serial ||
        !qa_actor_id_equal(client->inventory_lease.actor, actor)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Restored native Q2 inventory lease differs from its live source lease");
    bool claimed = false;
    for (uint32_t i = 1; i < 257; ++i) claimed = claimed || engine->clients[i].inventory_bound;
    if (!claimed) engine->primary_inventory->resolved = false;
    if (!resolve(engine, error)) return false;
    client->inventory_engine = engine; client->inventory_slot = slot;
    qa_native_address address;
    if (!client_address(client, &address, error)) return false;
    *out = (qa_inventory_binding){client, inventory_count, inventory_at, inventory_write, inventory_mutable};
    if (!client->inventory_bound) client->inventory_prepared = true;
    client->inventory_lease = (qa_inventory_lease){actor, saved_serial};
    client->inventory_bound = true;
    return true;
}

bool application_native_q2_inventory_finish(application_provider *provider, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || !application_native_q2_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 inventory restore finish requires an idle source owner");
    for (uint32_t i = 1; i < 257; ++i) {
        const application_native_q2_client *client = &engine->clients[i];
        if (client->inventory_prepared && (!client->inventory_bound ||
            !qa_inventory_primary_current(provider->application->inventory, client->inventory_lease, client)))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 prepared inventory primary was not published");
    }
    for (uint32_t i = 1; i < 257; ++i) engine->clients[i].inventory_prepared = false;
    return true;
}

bool application_native_q2_inventory_close(struct application_native_q2 *engine, qa_error *error)
{
    for (uint32_t i = 1; i < 257; ++i) if (!application_native_q2_inventory_detach(engine, i, error)) return false;
    if (engine->primary_inventory) {
        qa_json_destroy(engine->primary_inventory->document); free(engine->primary_inventory->rows);
        free(engine->primary_inventory); engine->primary_inventory = NULL;
    }
    return true;
}
