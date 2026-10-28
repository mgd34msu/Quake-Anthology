/* Port of src/app/bootstrap/network/unified-frame-values.ts.
 * Arrays/strings in read results are malloc'd; free helpers below.
 * PlayerUi.nativeInventory and model anchor/grapple/held-weapon
 * ride as retained qa_save_value subtrees in the landed void*
 * slots (validated here, re-emitted by the frame codec).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_FRAME_VALUES_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_FRAME_VALUES_H
#include "qa/app/bootstrap/network/unified_types.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/q3/foundation/presentation.h"
#include "qa/contracts/math.h"
#include "qa/persistence/value.h"
#include "qa/text/world.h"
#include "qa/world/session/mod_client_presentation.h"
/* TS wireActor: owned {slot,generation} object value. */
qa_save_value *qa_app_unified_wire_actor(qa_actor_id actor);
qa_actor_id qa_app_unified_read_actor(const qa_save_reader *reader,
    const qa_app_unified_identity_decoder *identity);
qa_vec3 qa_app_unified_read_vector(const qa_save_reader *reader);
qa_vec4 qa_app_unified_read_color(const qa_save_reader *reader);
void qa_app_unified_read_axis(const qa_save_reader *reader, qa_vec3 out[3]);
qa_app_sim_player_view qa_app_unified_read_player_view(const qa_save_reader *reader);
qa_app_sim_player_ui qa_app_unified_read_player_ui(const qa_save_reader *reader);
void qa_app_unified_player_ui_free(qa_app_sim_player_ui *ui);
qa_app_simulation_presentation qa_app_unified_read_model(const qa_save_reader *reader,
    const qa_app_unified_identity_decoder *identity);
void qa_app_unified_model_free(qa_app_simulation_presentation *model);
qa_q3_character_view qa_app_unified_read_character_view(const qa_save_reader *reader,
    const qa_app_unified_identity_decoder *identity);
qa_world_text qa_app_unified_read_world_text(const qa_save_reader *reader);
qa_native_mod_camera_view qa_app_unified_read_native_camera_view(const qa_save_reader *reader);
#endif
