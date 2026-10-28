/*
 * Port of src/guest/pe/directories.ts: import/export/TLS/unwind/load-config
 * directory readers. Strings are pooled into the caller's pool; result
 * arrays are malloc'd (caller owns). GPL-2.0-or-later.
 */
#ifndef QA_GUEST_PE_DIRECTORIES_H
#define QA_GUEST_PE_DIRECTORIES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/guest/abi/values.h"
#include "qa/guest/pe/image.h"

void qa_pe_read_imports(const qa_pe_image_reader *image, qa_pe_owned_strings *pool,
                        qa_guest_import **out, size_t *out_count);
void qa_pe_read_exports(const qa_pe_image_reader *image, qa_pe_owned_strings *pool,
                        qa_guest_export **out, size_t *out_count);
/* tls.initialized/callbacks are malloc'd when has_tls. */
void qa_pe_read_tls(const qa_pe_image_reader *image, bool *has_tls, qa_guest_tls_template *tls,
                    bool *has_index, qa_guest_core_address *index);
void qa_pe_read_unwind(const qa_pe_image_reader *image, qa_pe_unwind_record **out, size_t *out_count);
bool qa_pe_read_load_configuration(const qa_pe_image_reader *image, qa_pe_load_configuration *out);

#endif
