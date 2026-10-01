#ifndef QA_FRONTEND_EQUIPMENT_NATIVE_H
#define QA_FRONTEND_EQUIPMENT_NATIVE_H

#include "native_q3_client.h"
#include "equipment_source.h"

/* The actual native row invokes this factory after its media/backend exist.
 * Restoration uses the same empty owner after real dictionary adoption. */
bool frontend_equipment_native_compose(void *frontend, frontend_native_q3 *,
    frontend_native_q3_composition *, qa_error *);
bool frontend_equipment_native_weapon(const void *composition_context,
    qa_application_equipment_view *, bool *requested, qa_error *);
/* The entered appearance frame holds the actual selected torso registry.
 * Powerup shaders remain in the genuine primary frame's source registry. */
bool frontend_equipment_native_character_held(void *equipment_composition_context,
    const q3n_frame *, qa_actor_id, const qa_q3_presentation_assets *parent_assets,
    const qa_q3_ref_entity *torso, int32_t source_powerups, bool *suppressed, qa_error *);

#endif
