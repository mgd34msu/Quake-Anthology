/*
 * Port of src/guest/pe/loader.ts: mapPeImage (map + base-relocate +
 * directory reads) and bindPeImports. The mapped image buffer is retained
 * in the PeImage. TS failure rollback around protect/write is unobservable
 * under abort-on-error and omitted; IAT bytes are still all resolved
 * before any write. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_PE_LOADER_H
#define QA_GUEST_PE_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/guest/abi/values.h"
#include "qa/guest/pe/image.h"

typedef struct qa_map_pe_image_options {
  const uint8_t *bytes;
  size_t bytes_len;
  qa_mapped_guest_memory *memory;
  qa_mapped_memory_ext *memory_ext;
  qa_guest_module_identity memory_module; /* TS memory.module default */
  bool has_module;
  qa_guest_module_identity module;
  bool has_base;
  uint64_t base;
  bool has_maximum_image_bytes;
  int64_t maximum_image_bytes;
} qa_map_pe_image_options;

void qa_map_pe_image(qa_pe_image *out, const qa_map_pe_image_options *options);
/* Resolutions array is malloc'd; caller frees the array itself. */
void qa_bind_pe_imports(const qa_pe_image *image, qa_mapped_guest_memory *memory,
                        qa_mapped_memory_ext *memory_ext, qa_guest_import_resolver resolver,
                        qa_guest_import_resolution **out, size_t *out_count);

#endif
