/* Port of src/app/bootstrap/network/unified-native-components.ts.
 * Configstring maps ride as entry arrays. In project(), the landed
 * session frame hides map contents behind void*: identity (same
 * pointer) is the only sameConfigs fast path available, so distinct
 * maps count as changed. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_NATIVE_COMPONENTS_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_NATIVE_COMPONENTS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/unified_components.h"
#include "qa/ui/hud/q2_native.h"
#include "qa/world/session/mod_client_presentation.h"
typedef struct qa_app_unified_native_identity {
  qa_presentation_owner owner; qa_mod_identity identity;
  const qa_save_value *identity_wire; double generation;
} qa_app_unified_native_identity;
typedef struct qa_app_unified_native_publication {
  qa_app_unified_native_identity identity;
  qa_actor_id viewer;
  qa_mod_client_presentation_frame frame;
} qa_app_unified_native_publication;
typedef struct qa_app_unified_native_hud_state {
  const char *mode; /* layout-overlay|replace-status */
  qa_q2_protocol_identity protocol;
  bool has_configstrings;
  qa_native_q2_config_string *configstrings; size_t configstring_count; /* owned after read */
  const char *layout;
  double *inventory; size_t inventory_count; /* owned after read */
  double player_number;
} qa_app_unified_native_hud_state;
typedef struct qa_app_unified_native_state {
  qa_app_unified_native_identity identity;
  bool has_hud; qa_app_unified_native_hud_state hud;
} qa_app_unified_native_state;
typedef struct qa_app_unified_native_frame_hud {
  double *stats; size_t stat_count; /* owned after read */
  double server_frame, time_milliseconds;
  bool has_frame_time; double frame_time_milliseconds;
} qa_app_unified_native_frame_hud;
typedef struct qa_app_unified_native_frame {
  qa_presentation_owner owner; double generation; qa_actor_id viewer;
  bool has_hud; qa_app_unified_native_frame_hud hud;
  bool has_view; qa_native_mod_camera_view view;
} qa_app_unified_native_frame;
typedef struct qa_app_unified_native_projection {
  bool changed;
  qa_app_unified_native_state *states; size_t state_count;   /* owned shells, borrowed payloads */
  qa_app_unified_native_frame *frames; size_t frame_count;
} qa_app_unified_native_projection;
qa_app_unified_native_projection qa_app_unified_project_native_components(
    const qa_app_unified_native_publication *previous, size_t previous_count,
    const qa_app_unified_native_publication *sources, size_t source_count);
void qa_app_unified_native_projection_free(qa_app_unified_native_projection *projection);
qa_save_value *qa_app_unified_write_native_states(
    const qa_app_unified_native_state *sources, size_t count);
size_t qa_app_unified_read_native_states(const qa_save_reader *reader,
    qa_app_unified_native_state **out);
void qa_app_unified_native_states_free(qa_app_unified_native_state *states, size_t count);
qa_save_value *qa_app_unified_write_native_frames(
    const qa_app_unified_native_frame *sources, size_t count);
size_t qa_app_unified_read_native_frames(const qa_save_reader *reader,
    const qa_app_unified_identity_decoder *identity, qa_app_unified_native_frame **out);
void qa_app_unified_native_frames_free(qa_app_unified_native_frame *frames, size_t count);
#endif
