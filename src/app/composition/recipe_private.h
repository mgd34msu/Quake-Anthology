#ifndef QA_RECIPE_PRIVATE_H
#define QA_RECIPE_PRIVATE_H

#include "qa/executable_recipe.h"
#include "qa/arena.h"
#include "qa/catalog_save.h"
#include "qa/json_writer.h"
#include "qa/filesystem.h"
#include <stdlib.h>
#include <string.h>

#define RECIPE_MAX_RECORDS 65536u
#define RECIPE_MAX_BYTES (32u * 1024u * 1024u)
typedef struct recipe_view {
    const char *owner;
    qa_vfs *files;
    bool owns_files;
    const qa_catalog *catalog;
    qa_product_id product;
    qa_vfs *admitted_policy;
} recipe_view;
typedef struct recipe_resource {
    qa_launch_resource value;
    size_t view;
    qa_vfs_acquisition acquisition;
    bool owns_resource;
} recipe_resource;
struct qa_executable_recipe {
    qa_catalog *catalog;
    qa_resource_pool *pool;
    uint64_t catalog_generation;
    qa_arena arena;
    qa_recipe_choices choices;
    recipe_view *views; size_t view_count;
    recipe_resource **resources; size_t resource_count;
    qa_recipe_provider *providers; size_t provider_count;
    size_t *order; size_t order_count;
    bool mixed_order;
    bool visiting;
    qa_recipe_sidecar *sidecars; size_t sidecar_count;
    size_t map_index;
    qa_bsp_view bsp;
    qa_collision_geometry *geometry;
    qa_buffer composition;
    qa_sha256_digest digest;
    uint32_t epoch, max_clients;
    const char *mode;
};
typedef struct recipe_reader {
    const qa_json_document *json;
    qa_json_id array;
    size_t next;
    qa_executable_recipe *recipe;
    qa_error *error;
    bool failed;
} recipe_reader;
bool recipe_fail(qa_error *, const char *);
bool recipe_record(recipe_reader *, qa_json_id, size_t);
qa_json_id recipe_take(recipe_reader *);
const char *recipe_text(recipe_reader *);
const char *recipe_optional_text(recipe_reader *);
uint64_t recipe_word(recipe_reader *);
uint32_t recipe_unsigned(recipe_reader *);
int32_t recipe_signed(recipe_reader *);
bool recipe_boolean(recipe_reader *);
float recipe_float(recipe_reader *);
qa_product_id recipe_product(recipe_reader *);
qa_bytes recipe_binary(recipe_reader *);
bool recipe_choices_read(qa_executable_recipe *, const qa_json_document *, qa_json_id, qa_error *);
bool recipe_copy_json(qa_json_writer *, const qa_json_document *, qa_json_id, qa_error *);
void recipe_digest_write(qa_json_writer *, const qa_sha256_digest *);
bool recipe_digest_read(recipe_reader *, qa_sha256_digest *);
bool recipe_path(const char *, qa_error *);
bool recipe_resource_add(qa_executable_recipe *, qa_product_id, const char *,
    const qa_resource *, size_t *, qa_error *);
bool recipe_resource_add_from(qa_executable_recipe *, qa_product_id, const char *,
    const qa_resource *, size_t preferred_view, size_t *, qa_error *);
bool recipe_view_write(qa_json_writer *, const recipe_view *, qa_error *);
bool recipe_resource_write(qa_json_writer *, const qa_executable_recipe *, size_t, qa_error *);
bool recipe_view_add(qa_executable_recipe *, const char *, const qa_catalog *, qa_vfs *, bool, size_t *, qa_error *);

#endif
