#include "internal.h"
#include <ctype.h>

bool catalog_grow(void **data, size_t *capacity, size_t count, size_t size, qa_error *error)
{
    if (count <= *capacity) return true;
    size_t next = *capacity ? *capacity : 16;
    while (next < count) {
        if (next > SIZE_MAX / 2) { next = count; break; }
        next *= 2;
    }
    if (size && next <= SIZE_MAX / size) {
        void *memory = realloc(*data, next * size);
        if (memory) { *data = memory; *capacity = next; return true; }
    }
    qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot grow content catalog");
    return false;
}

const char *catalog_string(qa_catalog *catalog, const char *value, qa_error *error)
{
    qa_string_id id;
    if (!value || !qa_strings_intern_cstr(catalog->strings, value, &id, error)) return NULL;
    return qa_strings_cstr(catalog->strings, id);
}

const char *catalog_json_string(qa_catalog *catalog, const qa_json_document *doc,
                                qa_json_id id, const char *fallback, qa_error *error)
{
    if (id == QA_JSON_NONE && fallback) return catalog_string(catalog, fallback, error);
    qa_buffer text = {0};
    if (!qa_json_string(doc, id, &text, error)) return NULL;
    if (memchr(text.data, 0, text.size)) {
        qa_buffer_free(&text);
        qa_error_set(error, QA_ERROR_FORMAT, 0, "content metadata contains a NUL byte");
        return NULL;
    }
    const char *result = catalog_string(catalog, (const char *)text.data, error);
    qa_buffer_free(&text);
    return result;
}

static unsigned char fold(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

bool catalog_ascii_equal(const char *a, const char *b)
{
    while (*a && fold((unsigned char)*a) == fold((unsigned char)*b)) { ++a; ++b; }
    return fold((unsigned char)*a) == fold((unsigned char)*b);
}

bool catalog_suffix(const char *path, const char *suffix)
{
    size_t a = strlen(path), b = strlen(suffix);
    return a >= b && catalog_ascii_equal(path + a - b, suffix);
}

bool catalog_safe_name(const char *name)
{
    if (!name || !isalnum((unsigned char)*name) || strstr(name, "..")) return false;
    for (; *name; ++name)
        if (!isalnum((unsigned char)*name) && !strchr("._+-", *name)) return false;
    return true;
}

bool qa_catalog_mod_key(const char *key)
{
    if (!key || !isalnum((unsigned char)*key)) return false;
    const char *slash = strchr(key, '/');
    if (!slash || slash == key || !isalnum((unsigned char)slash[1])) return false;
    for (const char *p = key; p < slash; ++p)
        if (!isalnum((unsigned char)*p) && !strchr("._+-", *p)) return false;
    for (const char *p = slash + 1; *p; ++p)
        if (!isalnum((unsigned char)*p) && !strchr("._+:/-", *p)) return false;
    return true;
}

bool catalog_requirement(qa_catalog *catalog, catalog_product *product,
                          const char *message, qa_error *error)
{
    const char *text = catalog_string(catalog, message, error);
    if (!text) return false;
    size_t count = product->view.requirement_count;
    for (size_t i = 0; i < count; ++i)
        if (strcmp(product->view.requirements[i], text) == 0) return true;
    if (count == SIZE_MAX / sizeof(text)) return false;
    const char **items = realloc((void *)product->view.requirements, (count + 1) * sizeof(*items));
    if (!items) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain missing content requirements"); return false; }
    items[count] = text;
    product->view.requirements = items;
    product->view.requirement_count = count + 1;
    product->view.availability = QA_CONTENT_MISSING;
    return true;
}

bool catalog_add_product(qa_catalog *catalog, const qa_product *view,
                          catalog_product **out, qa_error *error)
{
    if (catalog->product_count == UINT32_MAX ||
        !catalog_grow((void **)&catalog->products, &catalog->product_capacity,
                      catalog->product_count + 1, sizeof(*catalog->products), error)) return false;
    catalog_product *product = &catalog->products[catalog->product_count];
    *product = (catalog_product){ .view = *view };
    product->view.id = (qa_product_id)++catalog->product_count;
    *out = product;
    return true;
}

bool qa_catalog_discover(const qa_catalog_options *options, qa_catalog **out, qa_error *error)
{
    if (!options || !options->resources || !options->content_root || !*options->content_root || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "catalog discovery requires a content root"); return false;
    }
    qa_catalog *catalog = calloc(1, sizeof(*catalog));
    if (!catalog) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate content catalog"); return false; }
    catalog->references = 1;
    catalog->generation = options->generation;
    catalog->resources = options->resources;
    if (!qa_strings_create(&catalog->strings, error)) goto fail;
    catalog->mounts = qa_vfs_create(options->resources, error);
    if (!catalog->mounts) goto fail;
    catalog->root = catalog_string(catalog, options->content_root, error);
    if (options->user_root) catalog->user = catalog_string(catalog, options->user_root, error);
    if (!catalog->root || (options->user_root && !catalog->user) ||
        !catalog_stock(catalog, error) || !catalog_scan(catalog, options->discover_mods, error)) goto fail;
    for (size_t i = 0; i < catalog->product_count; ++i) {
        catalog_product *p = &catalog->products[i];
        size_t first_mod = catalog->mod_count, first_behavior = catalog->behavior_count;
        qa_error issue = {0};
        if (!catalog_index_product(catalog, p, &issue) ||
            !catalog_read_starts(catalog, p, &issue) || !catalog_read_mods(catalog, p, &issue) ||
            !catalog_read_behaviors(catalog, p, &issue)) {
            if (issue.code == QA_ERROR_MEMORY) { if (error) *error = issue; goto fail; }
            if (!catalog_requirement(catalog, p, issue.message, error)) goto fail;
            p->view.availability = QA_CONTENT_INVALID;
            const char *reason = p->view.requirements[p->view.requirement_count - 1];
            for (size_t j = first_mod; j < catalog->mod_count; ++j) catalog->mods[j].unavailable = reason;
            for (size_t j = first_behavior; j < catalog->behavior_count; ++j) catalog->behaviors[j].unavailable = reason;
        }
        qa_resource_pool_trim(catalog->resources);
    }
    *out = catalog;
    return true;
fail:
    qa_catalog_release(catalog);
    return false;
}

void qa_catalog_retain(qa_catalog *catalog) { if (catalog) ++catalog->references; }
void qa_catalog_release(qa_catalog *catalog)
{
    if (!catalog || --catalog->references) return;
    for (size_t i = 0; i < catalog->product_count; ++i) {
        catalog_product *p = &catalog->products[i];
        free((void *)p->view.requirements); free(p->own_mounts); free(p->mounts);
        free(p->maps); free(p->starts);
    }
    for (size_t i = 0; i < catalog->mod_count; ++i) {
        qa_catalog_mod *m = &catalog->mods[i];
        free((void *)m->requires); free((void *)m->conflicts);
        free((void *)m->declaration.data);
    }
    for (size_t i = 0; i < catalog->behavior_count; ++i) {
        free((void *)catalog->behaviors[i].entry.data);
    }
    free(catalog->behaviors);
    for (size_t i = 0; i < catalog->physical_count; ++i) free(catalog->physical[i].members);
    free(catalog->products); free(catalog->physical); free(catalog->mods);
    if (catalog->mounts) qa_resource_pool_trim(catalog->resources);
    qa_vfs_destroy(catalog->mounts); qa_strings_destroy(catalog->strings); free(catalog);
}
uint64_t qa_catalog_generation(const qa_catalog *c) { return c ? c->generation : 0; }
size_t qa_catalog_count(const qa_catalog *c) { return c ? c->product_count : 0; }
const qa_product *qa_catalog_at(const qa_catalog *c, size_t i)
{ return c && i < c->product_count ? &c->products[i].view : NULL; }
const qa_product *qa_catalog_product(const qa_catalog *c, qa_product_id id)
{ return id ? qa_catalog_at(c, (size_t)id - 1) : NULL; }
const qa_product *qa_catalog_find(const qa_catalog *c, const char *key)
{
    if (c && key) for (size_t i = 0; i < c->product_count; ++i) {
        const qa_product *p = &c->products[i].view;
        if (!strcmp(key, p->key) || !strcmp(key, p->identity)) return p;
    }
    return NULL;
}
size_t qa_catalog_mount_count(const qa_catalog *c) { return c ? c->physical_count : 0; }
const qa_catalog_mount *qa_catalog_mount_at(const qa_catalog *c, size_t i)
{ return c && i < c->physical_count ? &c->physical[i].view : NULL; }
const qa_catalog_mount *catalog_mount(const qa_catalog *c, qa_mount_id id)
{
    const catalog_physical *p = catalog_package(c, id);
    return p ? &p->view : NULL;
}
const catalog_physical *catalog_package(const qa_catalog *c, qa_mount_id id)
{ return id && id <= c->physical_count ? &c->physical[id - 1] : NULL; }
bool qa_catalog_product_mounts(const qa_catalog *c, qa_product_id id,
                               const qa_mount_id **mounts, size_t *count)
{
    if (!qa_catalog_product(c, id) || !mounts || !count) return false;
    *mounts = c->products[id - 1].mounts; *count = c->products[id - 1].mount_count;
    return true;
}
const qa_catalog_map *qa_catalog_maps(const qa_catalog *c, qa_product_id id, size_t *count)
{
    if (count) *count = qa_catalog_product(c, id) ? c->products[id - 1].map_count : 0;
    return qa_catalog_product(c, id) ? c->products[id - 1].maps : NULL;
}
const qa_catalog_start *qa_catalog_starts(const qa_catalog *c, qa_product_id id,
                                         const qa_catalog_episode **episode, size_t *count)
{
    const catalog_product *p = qa_catalog_product(c, id) ? &c->products[id - 1] : NULL;
    if (episode) *episode = p && p->has_episode ? &p->episode : NULL;
    if (count) *count = p ? p->start_count : 0;
    return p ? p->starts : NULL;
}
size_t qa_catalog_mod_count(const qa_catalog *c) { return c ? c->mod_count : 0; }
const qa_catalog_mod *qa_catalog_mod_at(const qa_catalog *c, size_t i)
{ return c && i < c->mod_count ? &c->mods[i] : NULL; }
const qa_catalog_mod *qa_catalog_mod_find(const qa_catalog *c, const char *key)
{
    if (c && key) for (size_t i = 0; i < c->mod_count; ++i)
        if (!strcmp(c->mods[i].key, key)) return &c->mods[i];
    return NULL;
}
