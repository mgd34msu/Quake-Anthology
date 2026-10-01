#ifndef QA_FRONTEND_MATERIAL_INVENTORY_H
#define QA_FRONTEND_MATERIAL_INVENTORY_H
#include "internal.h"
#include "scene_identity.h"

/* Owner ordinals follow the actual root library, physical source groups, then
 * physical appearance owners. Distinct destructor owners never merge by
 * content. The aggregate holds its frontend/content/scene leases throughout
 * collection and the complete codecs, and uses these ordinals for Q3 assets. */
bool frontend_materials_capture_namespace(qa_frontend *, frontend_scene_namespace *, qa_error *);
bool frontend_materials_checkpoint(qa_frontend *, frontend_scene_namespace *, qa_buffer *, qa_error *);
/* Candidate construction provides genuine empty detached library placeholders
 * at their stable borrowed addresses and an empty renderer order. The complete
 * image/content namespace precedes this call. QAMC catalogs precede QAML
 * adoption, shared namespace binding and QAMO installation. Candidate cleanup
 * owns partially imported state on failure. Actual media services bind later,
 * before activation; this codec dispatches no video or source callback. */
bool frontend_materials_restore(qa_frontend *, frontend_scene_namespace *, qa_bytes, qa_error *);
/* Pure actual QFMA roster lookup, also valid after library import. The key is
 * its physical library ordinal plus one; no detached-state check or allocation
 * occurs inside a dependent Q3 codec callback. */
bool frontend_material_provider_encode(const qa_frontend *, const qa_q3_presentation_provider *, uint64_t *, qa_error *);
bool frontend_material_provider_decode(const qa_frontend *, uint64_t, qa_q3_presentation_provider *, qa_error *);

#endif
