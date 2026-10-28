/* Port of src/bots/navigation/load.ts. TS async becomes synchronous
 * (PORTING.md). GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_LOAD_H
#define QA_BOTS_NAVIGATION_LOAD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/navigation/aas.h"
#include "qa/bots/navigation/construct.h"
#include "qa/bots/navigation/nav.h"
#include "qa/bots/navigation/runtime.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"

typedef struct qa_navigation_load_options {
  qa_navigation_construction construction;
  qa_content_mounted *resources;
  const uint8_t *map_bytes; size_t map_byte_length;
  bool has_navigation_content; qa_content_id navigation_content;
} qa_navigation_load_options;
typedef struct qa_prepared_navigation {
  qa_navigation_map_identity map;
  qa_aas_asset *aas;              /* owned, at most one non-NULL */
  qa_kex_navigation_asset *kex;   /* owned */
  const qa_resolved_resource_reference *resource; /* borrowed, NULL = TS null */
} qa_prepared_navigation;
void qa_prepared_navigation_destroy(qa_prepared_navigation *prepared);
typedef struct qa_loaded_navigation {
  qa_navigation_graph graph; /* owned; borrows owned_aas/owned_kex when set */
  qa_aas_asset *owned_aas;
  qa_kex_navigation_asset *owned_kex;
  qa_navigation_runtime runtime;
  const qa_resolved_resource_reference *resource; /* borrowed */
} qa_loaded_navigation;
void qa_loaded_navigation_destroy(qa_loaded_navigation *loaded);
/* False only on OOM; invalid map paths abort (TS RangeError). A present
 * but unparsable asset counts as absent (TS try/catch): the aborting C
 * parsers are probed in a forked child first. */
bool qa_preload_navigation(qa_prepared_navigation *out, qa_navigation_map_identity map,
                           qa_content_mounted *resources, const uint8_t *map_bytes, size_t map_byte_length,
                           bool has_navigation_content, qa_content_id navigation_content);
bool qa_load_prepared_navigation(qa_loaded_navigation *out, qa_navigation_construction options,
                                 const qa_prepared_navigation *prepared);
bool qa_load_navigation(qa_loaded_navigation *out, qa_navigation_load_options options);
#endif
