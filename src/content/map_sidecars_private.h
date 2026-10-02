#ifndef QA_MAP_SIDECARS_PRIVATE_H
#define QA_MAP_SIDECARS_PRIVATE_H
#include "qa/map_sidecars.h"
#include "qa/persistence_content.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

#define MAP_SIDECAR_LIMIT 65536u
typedef struct map_sidecar_row {
    qa_map_sidecar value;
    qa_vfs_acquisition acquisition;
} map_sidecar_row;
struct qa_map_sidecars {
    size_t references;
    qa_catalog *catalog;
    qa_product_id product;
    qa_bsp_family family;
    qa_resource_pool *map_pool;
    qa_resource *map;
    char *map_path;
    qa_vfs *view;
    map_sidecar_row *rows;
    size_t count;
};
bool map_sidecars_fail(qa_error *, qa_status, const char *);
bool map_sidecars_inventory(const qa_map_sidecars *, qa_error *);
bool map_sidecars_texture_path(qa_bytes, char [1040], qa_error *);
#endif
