/* Port of src/content/q3/presentation/draw-status.ts. GPL-2.0-or-later.
 * Per-cgame-instance status text and lagometer state from cg_draw.c.
 * TS async drawDisconnect/drawLagometer are synchronous. */
#ifndef QA_CONTENT_Q3_PRESENTATION_DRAW_STATUS_H
#define QA_CONTENT_Q3_PRESENTATION_DRAW_STATUS_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/presentation/draw_tools.h"
#include "qa/content/q3/presentation/prediction.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/core/cvars/index.h"
#include "qa/text/q3_font.h"
typedef enum qa_q3_pres_draw_status_cvar {
  QA_Q3_PRES_STATUS_CVAR_CENTER_TIME, QA_Q3_PRES_STATUS_CVAR_LAGOMETER,
  QA_Q3_PRES_STATUS_CVAR_NO_PREDICT, QA_Q3_PRES_STATUS_CVAR_SYNCHRONOUS_CLIENTS
} qa_q3_pres_draw_status_cvar;
const char *qa_q3_pres_draw_status_cvar_name(qa_q3_pres_draw_status_cvar cvar);
/* TS ClientDrawStatusVariant: fonts non-NULL only for missionpack. */
typedef struct qa_q3_pres_draw_status_variant {
  qa_q3_base_product kind;
  const qa_q3_font_set *fonts;
} qa_q3_pres_draw_status_variant;
/* TS ClientDrawStatusHost + the media picks drawLagometer reads. */
typedef struct qa_q3_pres_draw_status_host {
  qa_q3_pres_command_source commands;
  void *ctx;
  qa_cvar_snapshot (*read_vm_cvar)(void *ctx, qa_q3_pres_draw_status_cvar name);
  bool (*register_shader)(void *ctx, const char *path, const qa_q3_pres_scene_shader **out);
  const qa_q3_pres_scene_shader *lagometer_shader; /* nullable */
} qa_q3_pres_draw_status_host;
typedef struct qa_q3_pres_lagometer_snapshot_sample { double ping, flags; } qa_q3_pres_lagometer_snapshot_sample;
typedef struct qa_q3_pres_client_draw_status {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_client_game_static_state *static_state;
  qa_q3_pres_client_draw_tools *tools;
  qa_q3_pres_draw_status_variant variant;
  qa_q3_pres_draw_status_host host;
  int32_t frame_samples[128], snapshot_flags[128], snapshot_samples[128];
  int32_t frame_count, snapshot_count;
} qa_q3_pres_client_draw_status;
/* Canonical-state/variant mismatches abort with the TS Error texts.
 * tools_static_state is the TS tools.media.staticState identity. */
void qa_q3_pres_client_draw_status_init(qa_q3_pres_client_draw_status *status,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_game_static_state *static_state,
    qa_q3_pres_client_draw_tools *tools, qa_q3_pres_client_game_static_state *tools_static_state,
    qa_q3_pres_draw_status_variant variant, qa_q3_pres_draw_status_host host);
/* False only on OOM storing the text. */
bool qa_q3_pres_client_draw_status_center_print(qa_q3_pres_client_draw_status *status, const char *text,
                                                double y, double char_width);
void qa_q3_pres_client_draw_status_draw_center_string(qa_q3_pres_client_draw_status *status);
void qa_q3_pres_client_draw_status_add_lagometer_frame_info(qa_q3_pres_client_draw_status *status);
/* sample NULL is TS null. */
void qa_q3_pres_client_draw_status_add_lagometer_snapshot_info(qa_q3_pres_client_draw_status *status,
    const qa_q3_pres_lagometer_snapshot_sample *sample);
/* False only on OOM registering the net shader. */
bool qa_q3_pres_client_draw_status_draw_disconnect(qa_q3_pres_client_draw_status *status);
bool qa_q3_pres_client_draw_status_draw_lagometer(qa_q3_pres_client_draw_status *status);
#endif
