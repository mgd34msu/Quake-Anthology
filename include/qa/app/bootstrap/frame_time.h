/* Port of src/app/bootstrap/frame-time.ts. FrameTimeCvarMirror's base
 * (core/cvars/mirror.ts) landed against callback registries, so the
 * mirror class stays a host-built qa_shared_cvar_mirror; its name
 * selection is qa_app_frame_time_mirror_names here. Supersedes
 * qa_app_frame_time_mirror in remote_seats.h. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_FRAME_TIME_H
#define QA_APP_BOOTSTRAP_FRAME_TIME_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/common.h"
#include "qa/core/cvars/index.h"
/* TS FrameTimeControls. */
typedef struct qa_app_frame_time_controls {
  double timescale, fixedtime, host_framerate, camera_mode;
} qa_app_frame_time_controls;
typedef struct qa_app_frame_host { bool dedicated, local_server; } qa_app_frame_host;
bool qa_app_q3_server_paused(qa_cvar_registry *cvars, bool requested, double connected_humans);
/* Static name list; *out_count set. */
const char *const *qa_app_frame_time_cvar_names(qa_command_dialect dialect, size_t *out_count);
/* FrameTimeCvarMirror ctor filter: names present in owner. Malloc'd
 * array of borrowed names; caller frees the array. */
const char **qa_app_frame_time_mirror_names(qa_cvar_registry *owner, size_t *out_count);
/* TS minimumFrameMilliseconds(cvars, dedicated): cvars NULL is TS null. */
double qa_app_minimum_frame_milliseconds(qa_cvar_registry *cvars, bool dedicated);
void qa_app_refresh_frame_time_cvars(qa_cvar_registry *owner, qa_cvar_registry *mirror);
void qa_app_register_frame_time_cvars(qa_cvar_registry *cvars);
qa_app_frame_time_controls qa_app_read_frame_time_controls(qa_cvar_registry *cvars);
double qa_app_source_frame_milliseconds(qa_command_dialect dialect, double raw_milliseconds,
                                        qa_app_frame_time_controls controls, qa_app_frame_host host);
#endif
