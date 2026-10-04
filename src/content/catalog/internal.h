#ifndef QA_CATALOG_INTERNAL_H
#define QA_CATALOG_INTERNAL_H

#include "qa/catalog.h"
#include "qa/catalog_save.h"
#include "qa/json.h"
#include <stdlib.h>
#include <string.h>

typedef struct catalog_product {
    qa_product view;
    qa_product_id configuration_base;
    qa_mount_id write_mount, loose_mount;
    qa_mount_id family_mount;
    qa_product_id family_product;
    qa_mount_id *own_mounts, *mounts;
    size_t own_count, mount_count;
    qa_catalog_map *maps;
    size_t map_count;
    qa_catalog_start *starts;
    size_t start_count;
    qa_catalog_episode episode;
    bool has_episode;
    /* Discovery-only admission for an explicitly created empty write directory.
     * Its resulting mounts/availability are saved; restore never rescans it. */
    bool remote_directory;
    const char *installed_directory;
    const char *witness;
    const char *required[4];
    size_t required_count;
} catalog_product;
typedef qa_catalog_member_identity catalog_member;
typedef struct catalog_location { const char *logical, *path; } catalog_location;
typedef struct catalog_physical {
    qa_catalog_mount view;
    qa_sha256_digest digest;
    catalog_member *members;
    size_t member_count;
} catalog_physical;
struct qa_catalog {
    size_t references;
    uint64_t generation;
    bool q3_demo_restricted;
    qa_strings *strings;
    qa_strings *restored_literals;
    qa_vfs *mounts;
    qa_resource_pool *resources;
    const char *root, *user;
    const char **install_roots;
    size_t install_root_count;
    catalog_location *locations;
    size_t location_count, location_capacity;
    qa_mount_id q3_install_mount;
    qa_mount_id q3_download_mount, corpus_mount, q2_download_mount[2];
    catalog_product *products;
    size_t product_count, product_capacity;
    catalog_physical *physical;
    size_t physical_count, physical_capacity;
    qa_catalog_mod *mods;
    size_t mod_count, mod_capacity;
    qa_catalog_weapon_behavior *behaviors;
    size_t behavior_count, behavior_capacity;
};
bool catalog_grow(void **data, size_t *capacity, size_t count, size_t size, qa_error *);
const char *catalog_string(qa_catalog *, const char *, qa_error *);
const char *catalog_json_string(qa_catalog *, const qa_json_document *, qa_json_id,
                                const char *fallback, qa_error *);
bool catalog_ascii_equal(const char *, const char *);
bool catalog_suffix(const char *, const char *);
bool catalog_safe_name(const char *);
bool catalog_remote_name(const char *);
bool catalog_requirement(qa_catalog *, catalog_product *, const char *, qa_error *);
bool catalog_stock(qa_catalog *, qa_error *);
bool catalog_add_product(qa_catalog *, const qa_product *, catalog_product **, qa_error *);
bool catalog_scan(qa_catalog *, bool mods, const char *remote_base,
    const char *remote_directory, qa_product_id *selected, qa_error *);
bool catalog_discover_locations(qa_catalog *, qa_error *);
const char *catalog_native_parent(qa_catalog *, const char *, qa_error *);
bool catalog_physical_path(qa_catalog *,const char *,const char *,const char **,qa_error *);
bool catalog_location_matches(const qa_catalog *,const char *,const char *);
bool catalog_product_directory(qa_catalog *,catalog_product *,const char **,qa_error *);
bool catalog_index_product(qa_catalog *, catalog_product *, qa_error *);
bool catalog_index_maps(qa_catalog *, catalog_product *, bool archives_only, qa_error *);
bool catalog_read_mods(qa_catalog *, catalog_product *, qa_error *);
bool catalog_read_starts(qa_catalog *, catalog_product *, qa_error *);
bool catalog_read_behaviors(qa_catalog *, catalog_product *, qa_error *);
bool catalog_view(const qa_catalog *, const qa_mount_id *, size_t, qa_vfs **, qa_error *);
const qa_catalog_mount *catalog_mount(const qa_catalog *, qa_mount_id);
const catalog_physical *catalog_package(const qa_catalog *, qa_mount_id);
bool catalog_path(qa_catalog *, const char *root, const char *relative,
                   const char **out, qa_error *);
bool catalog_has_path(qa_catalog *, const catalog_product *, const char *, bool own,
                       bool *, qa_error *);
bool catalog_copy_metadata(const qa_catalog *, const qa_catalog_checkpoint_refs *, qa_catalog **, qa_error *);
bool catalog_q3_restriction_valid(const qa_catalog *, qa_error *);

#endif
