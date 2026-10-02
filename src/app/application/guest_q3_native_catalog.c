#include "guest_q3_native_catalog.h"
#include "guest_q3_private.h"
#include "qa/json.h"

typedef struct native_catalog_locator {
    uint64_t value;
    uint32_t maximum;
    bool global;
} native_catalog_locator;

struct application_q3_native_catalog {
    q3g_role *role;
    qa_native_module *module;
    qa_native_module_info info;
    qa_sha256_digest declaration_digest;
    native_catalog_locator address, count;
    uint32_t stride, fields[4], string_limit;
    int32_t weapon_type, ammo_type;
    application_q3_catalog_record *records;
    size_t record_count;
    application_q3_catalog_weapon *weapons;
    size_t weapon_count;
};

static bool opening_current(const q3g_role *role)
{
    const q3g_artifact *a = role ? role->artifact : NULL;
    const qa_launch_instance *descriptor = a ? qa_launch_instance_lease_view(a->descriptor) : NULL;
    if (!a || !a->view || !a->descriptor || !a->items_resource ||
        !a->items_acquisition.path || strcmp(a->items_acquisition.path, "native-q3-items.json") ||
        !descriptor || descriptor->content != a->view ||
        qa_resource_id(a->items_resource) != a->items_acquisition.resource_id ||
        qa_resource_pool_find(qa_vfs_resources(a->view), a->items_acquisition.resource_id) != a->items_resource ||
        !qa_vfs_acquisition_retained(a->view, &a->items_acquisition, NULL)) return false;
    return true;
}

static bool fail(qa_error *error, qa_status status, const char *message)
{ return application_fail(error, status, message); }

static bool unsigned_field(const qa_json_document *doc, qa_json_id at,
    uint64_t limit, uint64_t *out, qa_error *error)
{
    if (!qa_json_u64(doc, at, out, error)) return false;
    return *out <= limit || fail(error, QA_ERROR_FORMAT, "Native catalog field exceeds its declared source domain");
}

static bool u32(const qa_json_document *doc, qa_json_id at, uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!unsigned_field(doc, at, UINT32_MAX, &value, error)) return false;
    *out = (uint32_t)value; return true;
}

static bool locator(application_q3_native_catalog *c, const qa_json_document *doc,
    qa_json_id at, bool count, native_catalog_locator *out, qa_error *error)
{
    if (count && qa_json_type(doc, at) != QA_JSON_OBJECT) {
        if (!unsigned_field(doc, at, INT32_MAX, &out->value, error) || !out->value)
            return fail(error, QA_ERROR_FORMAT, "Native catalog has no positive declared item count");
        out->maximum = (uint32_t)out->value; return true;
    }
    if (qa_json_type(doc, at) != QA_JSON_OBJECT)
        return fail(error, QA_ERROR_FORMAT, "Native catalog address must name an actual module RVA");
    qa_json_id global = qa_json_get(doc, at, "globalRva"), direct = qa_json_get(doc, at, "rva");
    out->global = global != QA_JSON_NONE;
    if ((count && !out->global) || (out->global && direct != QA_JSON_NONE) ||
        !unsigned_field(doc, out->global ? global : direct, UINT64_MAX, &out->value, error))
        return fail(error, QA_ERROR_FORMAT, "Native catalog locator has no unique authored RVA");
    size_t bytes = count ? 4 : c->info.image.target.pointer_bytes;
    if (out->value > c->info.image.image_bytes || bytes > c->info.image.image_bytes - out->value)
        return fail(error, QA_ERROR_FORMAT, "Native catalog locator leaves its actual artifact image");
    if (count && (!u32(doc, qa_json_get(doc, at, "maximum"), &out->maximum, error) ||
        !out->maximum || out->maximum > INT32_MAX))
        return fail(error, QA_ERROR_FORMAT, "Native catalog has no bounded original count global");
    return true;
}

static void records_free(application_q3_catalog_record *records, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        free((void *)records[i].class_name); free((void *)records[i].pickup_name);
    }
    free(records);
}

void application_q3_native_catalog_destroy(application_q3_native_catalog *c)
{
    if (!c) return;
    records_free(c->records, c->record_count); free(c->weapons);
    qa_native_module_release(c->module); free(c);
}

bool application_q3_native_catalog_create(q3g_role *role, qa_bytes bytes,
    application_q3_native_catalog **out, qa_error *error)
{
    if (!role || !out || *out || role->kind != QA_QVM_GAME || role->vm || !role->module ||
        !role->artifact || role->artifact->module != role->module || !bytes.size || !bytes.data ||
        !opening_current(role))
        return fail(error, QA_ERROR_ARGUMENT, "Native catalog requires its actual GAME module and retained declaration");
    application_q3_native_catalog *c = calloc(1, sizeof(*c));
    if (!c) return fail(error, QA_ERROR_MEMORY, "Retaining declared native Q3 catalog");
    c->role = role; c->module = role->module; qa_native_module_retain(c->module);
    c->info = qa_native_module_describe(c->module); qa_sha256(bytes, &c->declaration_digest);
    if (!qa_sha256_equal(&c->declaration_digest, qa_resource_digest(role->artifact->items_resource)) ||
        !qa_sha256_equal(&c->info.image.digest, qa_resource_digest(role->artifact->resource))) {
        application_q3_native_catalog_destroy(c);
        return fail(error, QA_ERROR_FORMAT, "Native catalog bytes differ from their actual source openings");
    }
    if (c->info.profile != QA_NATIVE_Q3_VMMAIN ||
        (c->info.image.target.pointer_bytes != 4 && c->info.image.target.pointer_bytes != 8)) {
        application_q3_native_catalog_destroy(c);
        return fail(error, QA_ERROR_UNSUPPORTED, "Native item catalog needs its actual Q3 vmMain pointer ABI");
    }
    qa_json_document *doc = NULL; qa_buffer path = {0}, digest = {0};
    uint32_t version = 0, pointer_bytes = 0, source_abi = 0;
    qa_sha256_digest expected;
    bool ok = qa_json_parse(bytes, &doc, error);
    qa_json_id root = doc ? qa_json_root(doc) : QA_JSON_NONE;
    ok = ok && u32(doc, qa_json_get(doc, root, "version"), &version, error) && version == 1 &&
        qa_json_string_equal(doc, qa_json_get(doc, root, "profile"), "q3-vmMain") &&
        u32(doc, qa_json_get(doc, root, "pointerBytes"), &pointer_bytes, error) &&
        pointer_bytes == c->info.image.target.pointer_bytes &&
        u32(doc, qa_json_get(doc, root, "abi"), &source_abi, error) && source_abi == (uint32_t)c->info.image.target.abi &&
        qa_json_string(doc, qa_json_get(doc, root, "artifactPath"), &path, error) &&
        !memchr(path.data, 0, path.size) && path.size == strlen(role->path) && !memcmp(path.data, role->path, path.size) &&
        qa_json_string(doc, qa_json_get(doc, root, "artifactDigest"), &digest, error) &&
        !memchr(digest.data, 0, digest.size) && qa_sha256_parse((const char *)digest.data, &expected, error) &&
        qa_sha256_equal(&expected, &c->info.image.digest);
    qa_json_id items = qa_json_get(doc, root, "items"), fields = qa_json_get(doc, items, "fields");
    if (ok) ok = qa_json_string_equal(doc, qa_json_get(doc, items, "source"), "live") &&
        locator(c, doc, qa_json_get(doc, items, "address"), false, &c->address, error) &&
        locator(c, doc, qa_json_get(doc, items, "count"), true, &c->count, error) &&
        u32(doc, qa_json_get(doc, items, "stride"), &c->stride, error) && c->stride &&
        u32(doc, qa_json_get(doc, items, "maximumStringBytes"), &c->string_limit, error) && c->string_limit;
    const char *names[4] = {"className", "pickupName", "type", "tag"};
    for (size_t i = 0; ok && i < 4; ++i) {
        uint32_t width = i < 2 ? pointer_bytes : 4;
        ok = u32(doc, qa_json_get(doc, fields, names[i]), c->fields + i, error) &&
            c->fields[i] <= c->stride && width <= c->stride - c->fields[i];
    }
    uint32_t weapon_type = 0, ammo_type = 0;
    if (ok) ok = u32(doc, qa_json_get(doc, items, "weaponType"), &weapon_type, error) && weapon_type <= INT32_MAX &&
        u32(doc, qa_json_get(doc, items, "ammoType"), &ammo_type, error) && ammo_type <= INT32_MAX && weapon_type != ammo_type;
    c->weapon_type = (int32_t)weapon_type; c->ammo_type = (int32_t)ammo_type;
    if (ok && !c->address.global && !c->count.global) {
        uint64_t extent = c->count.value * c->stride;
        ok = c->address.value <= c->info.image.image_bytes &&
            extent <= c->info.image.image_bytes - c->address.value;
    }
    qa_buffer_free(&path); qa_buffer_free(&digest); qa_json_destroy(doc);
    if (!ok) {
        if (!error || !error->code) fail(error, QA_ERROR_FORMAT, "Native item declaration differs from its actual artifact and pointer layout");
        application_q3_native_catalog_destroy(c); return false;
    }
    *out = c; return true;
}

static bool retained_current(const application_q3_native_catalog *c, const q3g_role *role, bool restoring)
{
    if (!c || !role || c->role != role || role->kind != QA_QVM_GAME || role->vm ||
        role->retired || !role->ready ||
        !role->host || !role->native || role->source_cleared ||
        !role->engine || role->engine->game != role ||
        role->module != c->module || !role->artifact || role->artifact->module != c->module ||
        !opening_current(role) ||
        !qa_sha256_equal(qa_resource_digest(role->artifact->items_resource), &c->declaration_digest)) return false;
    application_provider *provider = role->engine->provider;
    qa_native_instance *instance = qa_native_host_instance(role->native);
    if (restoring) {
        if (!role->engine->restore_pending || !role->engine->restoration || !provider ||
            provider->application->operation != APPLICATION_PERSISTING ||
            qa_native_process_restore_pending(instance)) return false;
    } else if (role->engine->restore_pending || !role->initialized || !role->init_succeeded) return false;
    return provider && provider->constructed && provider->attached && !provider->close_pending &&
        instance && qa_native_get_module(instance) == c->module && !qa_native_terminal(instance) &&
        !qa_native_active(instance) && qa_native_get_lifecycle(instance) == QA_NATIVE_INITIALIZED;
}

bool application_q3_native_catalog_current(const application_q3_native_catalog *c, const q3g_role *role)
{ return retained_current(c, role, false); }

static uint64_t little(const uint8_t *p, size_t count)
{
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static bool scalar(qa_native_instance *instance, uint64_t address, size_t width,
    uint64_t *out, qa_error *error)
{
    uint8_t bytes[8];
    if (!qa_native_read(instance, address, bytes, width, error)) return false;
    *out = little(bytes, width); return true;
}

static bool table(application_q3_native_catalog *c, qa_native_instance *instance,
    uint64_t *address, uint32_t *count, qa_error *error)
{
    uint64_t count64 = c->count.value;
    if (c->count.global) {
        qa_native_address global;
        if (!qa_native_rva(instance, c->count.value, 4, &global, error) ||
            !scalar(instance, global, 4, &count64, error)) return false;
    }
    if (count64 > c->count.maximum || count64 > INT32_MAX)
        return fail(error, QA_ERROR_FORMAT, "Native catalog count exceeds its actual declaration");
    *count = (uint32_t)count64;
    uint64_t extent = count64 * c->stride;
    if (extent > SIZE_MAX) return fail(error, QA_ERROR_FORMAT, "Native catalog table exceeds the host address domain");
    if (c->address.global) {
        qa_native_address global;
        if (!qa_native_rva(instance, c->address.value, c->info.image.target.pointer_bytes, &global, error) ||
            !scalar(instance, global, c->info.image.target.pointer_bytes, address, error)) return false;
    } else if (!qa_native_rva(instance, c->address.value, (size_t)extent, address, error)) return false;
    if ((!*address && *count) || extent > UINT64_MAX - *address)
        return fail(error, QA_ERROR_FORMAT, "Native catalog table has an invalid source pointer span");
    return !extent || qa_native_range_check(instance, *address, (size_t)extent, QA_NATIVE_MEMORY_READ, error);
}

static bool string(qa_native_instance *instance, uint64_t pointer, uint32_t maximum,
    const char **out, qa_error *error)
{
    qa_buffer text = {0};
    if (!qa_native_read_string(instance, pointer, maximum, &text, error)) return false;
    if (!text.size) { qa_buffer_free(&text); return fail(error, QA_ERROR_FORMAT, "Native item source name is empty"); }
    *out = (const char *)text.data; return true;
}

static bool identity(application_q3_native_catalog *c, const application_q3_catalog_record *record,
    bool weapon, qa_item_id *out, qa_error *error)
{
    static const char *const names[] = {NULL,"gauntlet","machinegun","shotgun","grenadelauncher","rocketlauncher","lightning","railgun","plasmagun","bfg","grapple","nailgun","proxlauncher","chaingun"};
    size_t count;
    const qa_q3_item *known = qa_q3_items(QA_Q3_TEAM_ARENA, &count);
    const char *canonical = NULL;
    for (size_t i = 1; i < count; ++i)
        if (known[i].kind == (weapon ? QA_Q3_ITEM_WEAPON : QA_Q3_ITEM_AMMO) &&
            !strcmp(known[i].classname, record->class_name) && known[i].tag > 0 &&
            (size_t)known[i].tag < sizeof(names) / sizeof(*names)) canonical = names[known[i].tag];
    char digest[65]; qa_sha256_hex(&c->info.image.digest, digest);
    size_t length = strlen(record->class_name);
    if (length > SIZE_MAX - 96) return fail(error, QA_ERROR_MEMORY, "Native item identity exceeds storage");
    char *text = malloc(length + 96);
    if (!text) return fail(error, QA_ERROR_MEMORY, "Retaining native Source item identity");
    if (canonical) snprintf(text, length + 96, "q3:%s/%s", weapon ? "weapon" : "ammo", canonical);
    else snprintf(text, length + 96, "q3:guest/sha256:%s/%s", digest, record->class_name);
    bool ok = qa_strings_intern_cstr(qa_session_strings(c->role->engine->provider->application->session), text, out, error);
    free(text); return ok;
}

static bool refresh_held(application_q3_native_catalog *c, bool restoring, qa_error *error)
{
    if (!retained_current(c, c ? c->role : NULL, restoring))
        return fail(error, QA_ERROR_ARGUMENT, "Native catalog lost its actual initialized returned GAME module");
    qa_native_instance *instance = qa_native_host_instance(c->role->native);
    uint64_t address; uint32_t count;
    if (!table(c, instance, &address, &count, error)) return false;
    size_t rows = count;
    if (rows > SIZE_MAX / sizeof(application_q3_catalog_record) ||
        rows > SIZE_MAX / sizeof(application_q3_catalog_weapon))
        return fail(error, QA_ERROR_MEMORY, "Native item table exceeds owned catalog storage");
    application_q3_catalog_record *records = count ? calloc(count, sizeof(*records)) : NULL;
    application_q3_catalog_weapon *weapons = count ? calloc(count, sizeof(*weapons)) : NULL;
    if (count && (!records || !weapons)) { free(records); free(weapons); return fail(error, QA_ERROR_MEMORY, "Reading native item table"); }
    size_t used = 0, weapon_count = 0; bool ok = true;
    for (uint32_t i = 0; ok && i < count; ++i) {
        uint64_t at = address + (uint64_t)i * c->stride, values[4];
        ok = scalar(instance, at + c->fields[0], c->info.image.target.pointer_bytes, values, error);
        if (!ok || !values[0]) continue;
        for (size_t j = 1; ok && j < 4; ++j)
            ok = scalar(instance, at + c->fields[j], j == 1 ? c->info.image.target.pointer_bytes : 4, values + j, error);
        if (!ok) break;
        application_q3_catalog_record *record = records + used++;
        uint32_t type = (uint32_t)values[2], tag = (uint32_t)values[3];
        *record = (application_q3_catalog_record){.index = i, .address = at};
        memcpy(&record->type, &type, sizeof(type)); memcpy(&record->tag, &tag, sizeof(tag));
        ok = string(instance, values[0], c->string_limit, &record->class_name, error);
        if (ok && values[1]) ok = string(instance, values[1], c->string_limit, &record->pickup_name, error);
        if (ok && !record->pickup_name && (record->type == c->weapon_type || record->type == c->ammo_type))
            ok = fail(error, QA_ERROR_FORMAT, "Native weapon or ammo has no actual Source label");
    }
    for (size_t i = 0; ok && i < used; ++i) {
        application_q3_catalog_record *record = records + i;
        if (record->type != c->weapon_type) continue;
        bool duplicate = false;
        for (size_t j = 0; j < weapon_count; ++j) if (weapons[j].weapon == record->tag) duplicate = true;
        if (duplicate) continue;
        if (record->tag <= 0) { ok = fail(error, QA_ERROR_FORMAT, "Native weapon has no positive Source selection"); break; }
        application_q3_catalog_weapon *weapon = weapons + weapon_count;
        *weapon = (application_q3_catalog_weapon){.weapon = record->tag, .label = record->pickup_name};
        ok = identity(c, record, true, &weapon->item, error);
        for (size_t j = 0; ok && j < used; ++j)
            if (records[j].type == c->ammo_type && records[j].tag == record->tag) {
                ok = identity(c, records + j, false, &weapon->ammo, error); break;
            }
        for (size_t j = 0; ok && j < weapon_count; ++j)
            if (weapons[j].item == weapon->item)
                ok = fail(error, QA_ERROR_FORMAT, "Native Source selection repeats its item identity");
        if (ok) ++weapon_count;
    }
    uint64_t current_address = 0; uint32_t current_count = 0;
    if (ok) ok = retained_current(c, c->role, restoring) &&
        table(c, instance, &current_address, &current_count, error) &&
        current_address == address && current_count == count;
    if (!ok) {
        records_free(records, used); free(weapons);
        if (!error || !error->code) fail(error, QA_ERROR_ARGUMENT, "Native catalog changed its actual source table during read");
        return false;
    }
    bool same = used == c->record_count && weapon_count == c->weapon_count;
    for (size_t i = 0; same && i < used; ++i) {
        const application_q3_catalog_record *a = records + i, *b = c->records + i;
        same = a->index == b->index && a->address == b->address && a->type == b->type &&
            a->tag == b->tag && !strcmp(a->class_name, b->class_name) &&
            ((a->pickup_name && b->pickup_name && !strcmp(a->pickup_name, b->pickup_name)) ||
             (!a->pickup_name && !b->pickup_name));
    }
    for (size_t i = 0; same && i < weapon_count; ++i)
        same = weapons[i].weapon == c->weapons[i].weapon && weapons[i].item == c->weapons[i].item &&
            weapons[i].ammo == c->weapons[i].ammo;
    if (same) { records_free(records, used); free(weapons); return true; }
    records_free(c->records, c->record_count); free(c->weapons);
    c->records = records; c->record_count = used; c->weapons = weapons; c->weapon_count = weapon_count;
    return true;
}

bool application_q3_native_catalog_restore_validate(application_q3_native_catalog *c,
    const q3g_role *role, qa_error *error)
{
    if (!retained_current(c, role, true))
        return fail(error, QA_ERROR_ARGUMENT, "Native catalog validation requires its real imported process and unpublished role");
    return refresh_held(c, true, error);
}

static bool refresh(application_q3_native_catalog *c, qa_error *error)
{ return refresh_held(c, false, error); }

bool application_q3_native_catalog_records(application_q3_native_catalog *c,
    const application_q3_catalog_record **out, size_t *count, qa_error *error)
{
    if (!out || !count) return fail(error, QA_ERROR_ARGUMENT, "Native catalog records require their output slots");
    if (!refresh(c, error)) return false;
    *out = c->records; *count = c->record_count; return true;
}

bool application_q3_native_catalog_weapons(application_q3_native_catalog *c,
    const application_q3_catalog_weapon **out, size_t *count, qa_error *error)
{
    if (!out || !count) return fail(error, QA_ERROR_ARGUMENT, "Native catalog weapons require their output slots");
    if (!refresh(c, error)) return false;
    *out = c->weapons; *count = c->weapon_count; return true;
}

bool application_q3_native_catalog_ammo_label(application_q3_native_catalog *c,
    qa_item_id item, const char **out, qa_error *error)
{
    if (!item || !out) return fail(error, QA_ERROR_ARGUMENT, "Native ammo label requires its actual item and output");
    if (!refresh(c, error)) return false;
    for (size_t i = 0; i < c->record_count; ++i) if (c->records[i].type == c->ammo_type) {
        qa_item_id actual;
        if (!identity(c, c->records + i, false, &actual, error)) return false;
        if (actual == item) { *out = c->records[i].pickup_name; return true; }
    }
    return fail(error, QA_ERROR_NOT_FOUND, "Ammo is absent from the actual native GAME table");
}
