#include "qa/source_frame_time.h"

#include <math.h>
#include <float.h>

static bool frame_time_fail(qa_error *error, qa_status code, const char *message)
{
    qa_error_set(error, code, 0, "%s", message);
    return false;
}

static bool q1_dialect(qa_console_dialect dialect)
{
    return dialect == QA_CONSOLE_Q1 || dialect == QA_CONSOLE_QW;
}
static bool q2_dialect(qa_console_dialect dialect)
{
    return dialect == QA_CONSOLE_Q2 || dialect == QA_CONSOLE_Q2_RERELEASE;
}
static bool known_dialect(qa_console_dialect dialect)
{
    return q1_dialect(dialect) || q2_dialect(dialect) || dialect == QA_CONSOLE_Q3;
}

static bool register_control(qa_cvars *cvars, const char *name, const char *value,
    uint32_t flags, uint64_t owner, qa_error *error)
{
    if (q1_dialect(qa_cvars_dialect(cvars)) && qa_cvars_find(cvars, name)) return true;
    return qa_cvars_register(cvars, name, value, flags, owner, "Source frame timing", error);
}

bool qa_source_frame_time_register(qa_cvars *cvars, uint64_t owner, qa_error *error)
{
    if (!cvars || !known_dialect(qa_cvars_dialect(cvars)))
        return frame_time_fail(error, QA_ERROR_ARGUMENT, "frame time requires an actual source registry");
    qa_console_dialect dialect = qa_cvars_dialect(cvars);
    uint32_t cheat = q1_dialect(dialect) ? 0 : q2_dialect(dialect) ? QA_Q2_CVAR_CHEAT : QA_CVAR_CHEAT;
    if (!register_control(cvars, "timescale", "1",
            cheat | (dialect == QA_CONSOLE_Q3 ? QA_CVAR_SYSTEMINFO : 0), owner, error)) return false;
    if (q1_dialect(dialect)) return register_control(cvars, "host_framerate", "0", 0, owner, error);
    return register_control(cvars, "fixedtime", "0", cheat, owner, error) &&
        (dialect != QA_CONSOLE_Q3 || register_control(cvars, "com_cameraMode", "0", cheat, owner, error));
}

bool qa_source_frame_time_controls_read(const qa_cvars *cvars,
    qa_source_frame_time_controls *out, qa_error *error)
{
    if (!cvars || !out || !known_dialect(qa_cvars_dialect(cvars)))
        return frame_time_fail(error, QA_ERROR_ARGUMENT, "frame controls require an actual source registry and output");
    qa_source_frame_time_controls controls = {.timescale = 1};
    const qa_cvar_view *view;
    if ((view = qa_cvars_find(cvars, "timescale"))) controls.timescale = view->number;
    if ((view = qa_cvars_find(cvars, "fixedtime")))
        controls.fixedtime = qa_cvars_dialect(cvars) == QA_CONSOLE_Q3 ? (double)view->integer : (double)view->number;
    if ((view = qa_cvars_find(cvars, "host_framerate"))) controls.host_framerate = view->number;
    if ((view = qa_cvars_find(cvars, "com_cameraMode"))) controls.camera_mode = view->integer;
    *out = controls;
    return true;
}

bool qa_source_frame_time_transform(qa_console_dialect dialect, double supplied_milliseconds,
    const qa_source_frame_time_controls *controls, bool dedicated, bool local_server,
    double *out, qa_error *error)
{
    if (!controls || !out || !known_dialect(dialect) || !isfinite(supplied_milliseconds) ||
        supplied_milliseconds < 0 || !isfinite(controls->timescale) || !isfinite(controls->fixedtime) ||
        !isfinite(controls->host_framerate) || !isfinite(controls->camera_mode))
        return frame_time_fail(error, QA_ERROR_ARGUMENT, "frame milliseconds and controls must be finite and the supplied delta nonnegative");
    double milliseconds;
    if (q1_dialect(dialect)) {
        if (controls->host_framerate > 0) milliseconds = controls->host_framerate * 1000;
        else {
            milliseconds = controls->timescale == 0 ? supplied_milliseconds : supplied_milliseconds * controls->timescale;
            if (milliseconds > 100) milliseconds = 100;
            if (milliseconds < 1) milliseconds = 1;
        }
    } else if (q2_dialect(dialect)) {
        if (controls->fixedtime != 0) milliseconds = controls->fixedtime;
        else {
            milliseconds = controls->timescale == 0 ? supplied_milliseconds : supplied_milliseconds * controls->timescale;
            if (controls->timescale != 0 && milliseconds < 1) milliseconds = 1;
        }
    } else {
        milliseconds = trunc(supplied_milliseconds);
        if (fabs(controls->timescale) > FLT_MAX || milliseconds > FLT_MAX)
            return frame_time_fail(error, QA_ERROR_ARGUMENT, "source frame timing exceeds the native float domain");
        volatile float scale = (float)controls->timescale;
        double fixedtime = trunc(controls->fixedtime), camera_mode = trunc(controls->camera_mode);
        if (fixedtime != 0) milliseconds = fixedtime;
        else if (scale != 0 || camera_mode != 0) {
            volatile float input = (float)milliseconds;
            volatile float product = input * scale;
            if (!isfinite(product) || (double)product < -2147483648.0 || (double)product >= 2147483648.0)
                return frame_time_fail(error, QA_ERROR_ARGUMENT, "undefined native common float-to-int time conversion");
            milliseconds = trunc((double)product) + 0.0;
        }
        if (milliseconds < 1 && scale != 0) milliseconds = 1;
        double maximum = dedicated || !local_server ? 5000 : 200;
        if (milliseconds > maximum) milliseconds = maximum;
    }
    if (!isfinite(milliseconds))
        return frame_time_fail(error, QA_ERROR_ARGUMENT, "source frame timing result is not finite");
    *out = milliseconds;
    return true;
}

bool qa_source_frame_time_sample(const qa_cvars *cvars, double supplied_milliseconds,
    bool dedicated, bool local_server, double *out, qa_error *error)
{
    qa_source_frame_time_controls controls;
    return qa_source_frame_time_controls_read(cvars, &controls, error) &&
        qa_source_frame_time_transform(qa_cvars_dialect(cvars), supplied_milliseconds,
            &controls, dedicated, local_server, out, error);
}
