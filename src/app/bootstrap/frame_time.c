/* Port of src/app/bootstrap/frame-time.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/frame_time.h"
#include <math.h>
#include <stdlib.h>
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

static bool reg_cvar(qa_cvar_registry *r, const char *name, const char *value, double flags, void *unused) {
  (void)unused;
  qa_cvar_snapshot snapshot;
  bool ok = qa_cvar_registry_register(r, name, value, flags, &snapshot);
  qa_cvar_read_free(&snapshot);
  return ok;
}
static bool set_cvar(qa_cvar_registry *r, const char *name, const char *value, bool force, void *unused) {
  (void)unused;
  qa_cvar_snapshot snapshot;
  bool ok = qa_cvar_registry_set(r, name, value, force, &snapshot);
  qa_cvar_read_free(&snapshot);
  return ok;
}

static bool find_cvar(qa_cvar_registry *cvars, const char *name, qa_cvar_read *out) {
  return qa_cvar_registry_find(cvars, name, out);
}
bool qa_app_q3_server_paused(qa_cvar_registry *cvars, bool requested, double connected_humans) {
  qa_cvar_read read;
  if (!find_cvar(cvars, "sv_paused", &read)) reg_cvar(cvars, "sv_paused", "0", QA_CVAR_FLAG_READ_ONLY, NULL);
  else qa_cvar_read_free(&read);
  if (!requested) return false;
  bool paused = connected_humans <= 1;
  set_cvar(cvars, "sv_paused", paused ? "1" : "0", true, NULL);
  return paused;
}
static const char *const names_q1[] = {"timescale", "host_framerate"};
static const char *const names_q2[] = {"timescale", "fixedtime"};
static const char *const names_q3[] = {"timescale", "fixedtime", "com_cameraMode"};
const char *const *qa_app_frame_time_cvar_names(qa_command_dialect dialect, size_t *out_count) {
  if (qa_command_dialect_is_q1(dialect)) { *out_count = 2; return names_q1; }
  if (qa_command_dialect_is_q2(dialect)) { *out_count = 2; return names_q2; }
  *out_count = 3; return names_q3;
}
const char **qa_app_frame_time_mirror_names(qa_cvar_registry *owner, size_t *out_count) {
  size_t count = 0;
  const char *const *all = qa_app_frame_time_cvar_names(qa_cvar_registry_dialect(owner), &count);
  const char **out = malloc((count ? count : 1) * sizeof *out);
  if (!out) { *out_count = 0; return NULL; }
  size_t kept = 0;
  for (size_t i = 0; i < count; i++) {
    qa_cvar_read read;
    if (find_cvar(owner, all[i], &read)) { qa_cvar_read_free(&read); out[kept++] = all[i]; }
  }
  *out_count = kept;
  return out;
}
void qa_app_refresh_frame_time_cvars(qa_cvar_registry *owner, qa_cvar_registry *mirror) {
  size_t count = 0;
  const char *const *names = qa_app_frame_time_cvar_names(qa_cvar_registry_dialect(owner), &count);
  for (size_t i = 0; i < count; i++) {
    qa_cvar_read read;
    if (find_cvar(owner, names[i], &read)) {
      set_cvar(mirror, names[i], read.value, true, NULL);
      qa_cvar_read_free(&read);
    }
  }
}
void qa_app_register_frame_time_cvars(qa_cvar_registry *cvars) {
  qa_command_dialect dialect = qa_cvar_registry_dialect(cvars);
  bool q1 = qa_command_dialect_is_q1(dialect), q2 = qa_command_dialect_is_q2(dialect);
  /* TS registers unconditionally except q1 skips existing names. */
  typedef struct { const char *name, *value; double flags; } row_t;
  row_t rows[3];
  size_t n = 0;
  rows[n++] = (row_t){"timescale", "1", q1 ? 0 : q2 ? QA_Q2_CVAR_FLAG_CHEAT : QA_CVAR_FLAG_CHEAT | QA_CVAR_FLAG_SYSTEM_INFO};
  if (q1) {
    rows[n++] = (row_t){"host_framerate", "0", 0};
    rows[n++] = (row_t){"sys_ticrate", "0.05", 0};
  } else {
    rows[n++] = (row_t){"fixedtime", "0", q2 ? QA_Q2_CVAR_FLAG_CHEAT : QA_CVAR_FLAG_CHEAT};
    if (!q2) rows[n++] = (row_t){"com_cameraMode", "0", QA_CVAR_FLAG_CHEAT};
  }
  for (size_t i = 0; i < n; i++) {
    qa_cvar_read read;
    bool exists = find_cvar(cvars, rows[i].name, &read);
    if (exists) qa_cvar_read_free(&read);
    if (!q1 || !exists) reg_cvar(cvars, rows[i].name, rows[i].value, rows[i].flags, NULL);
  }
}
static double numeric_or(qa_cvar_registry *cvars, const char *name, double fallback, bool integer) {
  qa_cvar_read read;
  if (!find_cvar(cvars, name, &read)) return fallback;
  double value = integer ? read.integer_value : read.numeric_value;
  qa_cvar_read_free(&read);
  return value;
}
qa_app_frame_time_controls qa_app_read_frame_time_controls(qa_cvar_registry *cvars) {
  qa_app_frame_time_controls controls;
  controls.timescale = numeric_or(cvars, "timescale", 1, false);
  controls.fixedtime = qa_cvar_registry_dialect(cvars) == QA_COMMAND_DIALECT_Q3
      ? numeric_or(cvars, "fixedtime", 0, true) : qa_cvar_registry_variable_value(cvars, "fixedtime");
  controls.host_framerate = qa_cvar_registry_variable_value(cvars, "host_framerate");
  controls.camera_mode = numeric_or(cvars, "com_cameraMode", 0, true);
  return controls;
}
double qa_app_source_frame_milliseconds(qa_command_dialect dialect, double raw_milliseconds,
                                        qa_app_frame_time_controls controls, qa_app_frame_host host) {
  if (!isfinite(raw_milliseconds) || raw_milliseconds < 0)
    qa_range_error("Frame milliseconds must be finite and nonnegative");
  if (!isfinite(controls.timescale) || !isfinite(controls.fixedtime) || !isfinite(controls.host_framerate) || !isfinite(controls.camera_mode))
    qa_range_error("Frame time controls must be finite");
  if (dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE || dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD) {
    if (controls.host_framerate > 0) return controls.host_framerate * 1000;
    double scaled = controls.timescale == 0 ? raw_milliseconds : raw_milliseconds * controls.timescale;
    return qa_js_max(1, qa_js_min(100, scaled));
  }
  if (dialect == QA_COMMAND_DIALECT_Q2_CLASSIC || dialect == QA_COMMAND_DIALECT_Q2_RERELEASE) {
    if (controls.fixedtime != 0) return controls.fixedtime;
    double scaled = controls.timescale == 0 ? raw_milliseconds : qa_js_max(1, raw_milliseconds * controls.timescale);
    /* sv_main.c never lets the server fall more than one tic behind; this bounds a stall such as a map load. */
    return qa_js_min(scaled, host.dedicated || !host.local_server ? 5000 : 200);
  }
  double milliseconds = trunc(raw_milliseconds);
  float scale = (float)controls.timescale; /* Math.fround */
  double fixedtime = trunc(controls.fixedtime), camera_mode = trunc(controls.camera_mode);
  if (fixedtime != 0) milliseconds = fixedtime;
  else if (scale != 0 || camera_mode != 0) {
    float product = (float)((float)milliseconds * scale); /* Math.fround staged */
    /* timescale is a console cvar; a value that overflows the source's int cast just stops the clock for this frame. */
    milliseconds = !isfinite(product) || product < -2147483648.0f || product >= 2147483648.0f ? 0 : trunc(product) + 0;
  }
  if (milliseconds < 1 && scale != 0) milliseconds = 1;
  return qa_js_min(milliseconds, host.dedicated || !host.local_server ? 5000 : 200);
}

/* TS minimumFrameMilliseconds: Host_FilterTime, Q1 hosts never run above 72 Hz,
 * and dedicated servers pace to sys_ticrate. */
double qa_app_minimum_frame_milliseconds(qa_cvar_registry *cvars, bool dedicated) {
  if (cvars == NULL) return 4;
  qa_command_dialect dialect = qa_cvar_registry_dialect(cvars);
  if (dialect != QA_COMMAND_DIALECT_Q1_NETQUAKE && dialect != QA_COMMAND_DIALECT_Q1_QUAKEWORLD) return 4;
  qa_cvar_read found;
  double ticrate = 0.05;
  if (qa_cvar_registry_find(cvars, "sys_ticrate", &found)) { ticrate = found.numeric_value; qa_cvar_read_free(&found); }
  return dedicated && isfinite(ticrate) && ticrate > 0 ? ticrate * 1000 : 1000.0 / 72;
}
