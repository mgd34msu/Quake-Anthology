/* Port of src/app/bootstrap/component-scene.ts: selectComponentScene builds
 * the QvmSceneContext a scene cgame reads from one QvmModScenePublication.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_COMPONENT_SCENE_H
#define QA_APP_BOOTSTRAP_COMPONENT_SCENE_H
#include "qa/compat/qvm/mod_presentation_checkpoint.h"
#include "qa/contracts/identity.h"
#include "qa/world/collision/index.h"
#include "qa/world/session/mod_presentations.h"
/* TS selectComponentScene(source, viewer, queries, leafCount, print). The
 * result owns every array, the game-state copy, the player-state slot block
 * and the command arguments (one block per command); free it with
 * qa_qvm_scene_context_free. Raises "Component snapshot has no admitted
 * viewing player". */
void qa_app_select_component_scene(const qa_qvm_mod_scene_publication *source, qa_actor_id viewer,
    qa_shared_scene_queries *queries, double leaf_count, void (*print)(void *ctx, const char *text), void *print_ctx,
    qa_qvm_scene_context *out);
/* Deep copy of a context (as the TS spread shares it); free with
 * qa_qvm_scene_context_free. Command arguments are copied into one block. */
void qa_app_copy_component_scene(const qa_qvm_scene_context *source, qa_qvm_scene_context *out);
#endif
