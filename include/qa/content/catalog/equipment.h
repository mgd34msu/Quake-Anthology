/*
 * Port of src/content/catalog/equipment.ts. List results are
 * malloc'd (free the array; timing entries hold profile values,
 * resource paths for generated grenade/Q2 names are malloc'd in a
 * parallel owned list freed by the matching free function).
 * GrappleStyle.unavailable strings built from product titles are
 * malloc'd; slot titles/mechanics are literals.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_CATALOG_EQUIPMENT_H
#define QA_CONTENT_CATALOG_EQUIPMENT_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"

/* TS EQUIPMENT_PROVIDERS. */
typedef struct qa_equipment_provider_set {
  qa_provider_id threewave;
  qa_provider_id ctf;
  qa_provider_id lmctf;
  qa_provider_id hand_grenades;
} qa_equipment_provider_set;
extern const qa_equipment_provider_set qa_equipment_provider_ids;

/* TS disabledEquipment. */
qa_equipment_selection qa_disabled_equipment(void);

/* TS GrappleStyle. */
typedef struct qa_grapple_style {
  const char *id;
  const char *title;
  qa_grapple_selection selection; /* enabled */
  char *unavailable; /* owned, nullable */
} qa_grapple_style;

/* TS grappleStyles. Malloc'd list; free with
 * qa_grapple_styles_free. NULL only on OOM. */
qa_grapple_style *qa_grapple_styles(const qa_content_catalog *catalog,
                                    const qa_catalog_discovered_product *preferred,
                                    size_t *out_count);
void qa_grapple_styles_free(qa_grapple_style *styles, size_t count);

/* TS offhandGrenadeSource. */
const qa_catalog_discovered_product *qa_offhand_grenade_source(
    const qa_content_catalog *catalog,
    const qa_catalog_discovered_product *preferred);

/* TS nativeEquipment. */
qa_equipment_selection qa_native_equipment(const qa_content_catalog *catalog,
                                           qa_provider_reference map,
                                           qa_provider_reference match);

/* TS equipmentProviders. Malloc'd array (free only). */
qa_provider_reference *qa_equipment_providers(qa_equipment_selection equipment,
                                              size_t *out_count);

/* TS validateEquipment: aborts with exact TS RangeErrors. */
void qa_validate_equipment(qa_equipment_selection equipment,
                           const qa_content_catalog *catalog);

/* TS equipmentTiming. Malloc'd array (free only). */
qa_provider_timing *qa_equipment_timing(qa_equipment_selection equipment,
                                        size_t *out_count);

/* TS equipmentResources. Paths are literals except generated
 * names, which are malloc'd and tracked for the free call. */
typedef struct qa_catalog_resource_list {
  qa_resource_request *values;
  size_t count;
  char **owned_paths;
  size_t owned_count;
} qa_catalog_resource_list;
bool qa_equipment_resources(qa_equipment_selection equipment,
                            qa_catalog_resource_list *out);
void qa_catalog_resource_list_free(qa_catalog_resource_list *list);
/* List append helpers shared within the catalog module. */
bool qa_catalog_resource_push(qa_catalog_resource_list *list,
                              qa_content_id content, const char *path);
bool qa_catalog_resource_push_owned(qa_catalog_resource_list *list,
                                    qa_content_id content, char *path);

#endif
