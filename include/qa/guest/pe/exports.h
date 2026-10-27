/*
 * Port of src/guest/pe/exports.ts: chained PE export/forwarder resolution.
 * The TS address-space identity check has no C counterpart (addresses
 * carry no space token); the ABI width check is kept. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_PE_EXPORTS_H
#define QA_GUEST_PE_EXPORTS_H

#include "qa/guest/abi/values.h"
#include "qa/guest/pe/image.h"

typedef struct qa_pe_resolved_export {
  const qa_pe_image *image;
  qa_guest_address address;
} qa_pe_resolved_export;

typedef const qa_pe_image *(*qa_pe_library_lookup)(void *ctx, const char *library,
                                                   const qa_pe_image *requesting);

qa_pe_resolved_export qa_resolve_pe_export(const qa_pe_image *image, qa_guest_symbol_name symbol,
                                           qa_pe_library_lookup lookup, void *lookup_ctx);

#endif
