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
    uint32_t cheat = 0;
    if (dialect == QA_CONSOLE_Q2_RERELEASE) cheat = QA_Q2_CVAR_CHEAT;
    else if (dialect == QA_CONSOLE_Q3) cheat = QA_CVAR_CHEAT;
    if (dialect == QA_CONSOLE_Q1)
        return register_control(cvars, "host_framerate", "0", 0, owner, error);
    if (dialect == QA_CONSOLE_QW)
        return register_control(cvars, "cl_maxfps", "0", QA_CVAR_ARCHIVE, owner, error) &&
            register_control(cvars, "rate", "2500", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO, owner, error);
    if (!register_control(cvars, "timescale", "1",
            cheat | (dialect == QA_CONSOLE_Q3 ? QA_CVAR_SYSTEMINFO : 0), owner, error)) return false;
    return register_control(cvars, "fixedtime", "0", cheat, owner, error) &&
        (dialect != QA_CONSOLE_Q3 || register_control(cvars, "com_cameraMode", "0", cheat, owner, error));
}

bool qa_source_frame_time_controls_read(const qa_cvars *cvars,
    qa_source_frame_time_controls *out, qa_error *error)
{
    if (!cvars || !out || !known_dialect(qa_cvars_dialect(cvars)))
        return frame_time_fail(error, QA_ERROR_ARGUMENT, "frame controls require an actual source registry and output");
    qa_source_frame_time_controls controls = {.timescale = 1, .rate = 2500,
        .server_minimum_seconds = .03f, .server_maximum_seconds = .1f};
    const qa_cvar_view *view;
    if ((view = qa_cvars_find(cvars, "timescale"))) controls.timescale = view->number;
    if ((view = qa_cvars_find(cvars, "fixedtime")))
        controls.fixedtime = qa_cvars_dialect(cvars) == QA_CONSOLE_Q3 ||
            qa_cvars_dialect(cvars) == QA_CONSOLE_Q2_RERELEASE ? (double)view->integer : (double)view->number;
    if ((view = qa_cvars_find(cvars, "host_framerate"))) controls.host_framerate = view->number;
    if ((view = qa_cvars_find(cvars, "com_cameraMode"))) controls.camera_mode = view->integer;
    if ((view = qa_cvars_find(cvars, "cl_maxfps"))) controls.maximum_fps = view->number;
    if ((view = qa_cvars_find(cvars, "rate"))) controls.rate = view->number;
    if ((view = qa_cvars_find(cvars, "sv_mintic"))) controls.server_minimum_seconds = view->number;
    if ((view = qa_cvars_find(cvars, "sv_maxtic"))) controls.server_maximum_seconds = view->number;
    *out = controls;
    return true;
}

bool qa_source_frame_time_transform(qa_console_dialect dialect, double supplied_milliseconds,
    const qa_source_frame_time_controls *controls, bool dedicated, bool local_server,
    double *out, qa_error *error)
{
    if (!controls || !out || !known_dialect(dialect) || !isfinite(supplied_milliseconds) ||
        supplied_milliseconds < 0 ||
        (dialect == QA_CONSOLE_Q1 && !isfinite(controls->host_framerate)) ||
        (!q1_dialect(dialect) && (!isfinite(controls->timescale) || !isfinite(controls->fixedtime))) ||
        (dialect == QA_CONSOLE_Q3 && !isfinite(controls->camera_mode)))
        return frame_time_fail(error, QA_ERROR_ARGUMENT, "frame milliseconds and controls must be finite and the supplied delta nonnegative");
    double milliseconds;
    if (dialect == QA_CONSOLE_Q1) {
        if (controls->host_framerate > 0) milliseconds = controls->host_framerate * 1000;
        else {
            milliseconds = supplied_milliseconds;
            if (milliseconds > 100) milliseconds = 100;
            if (milliseconds < 1) milliseconds = 1;
        }
    } else if (dialect == QA_CONSOLE_QW) {
        milliseconds = !dedicated && supplied_milliseconds > 200 ? 200 : supplied_milliseconds;
    } else if (dialect == QA_CONSOLE_Q2) {
        milliseconds = trunc(supplied_milliseconds);
        if (controls->fixedtime != 0) {
            if (controls->fixedtime < -2147483648.0 || controls->fixedtime >= 2147483648.0)
                return frame_time_fail(error, QA_ERROR_ARGUMENT, "undefined native common float-to-int time conversion");
            milliseconds = trunc(controls->fixedtime);
        }
        else {
            if (controls->timescale != 0) {
                if (fabs(controls->timescale) > FLT_MAX || milliseconds > 2147483647.0)
                    return frame_time_fail(error, QA_ERROR_ARGUMENT, "source frame timing exceeds the native common domain");
                float input = (float)milliseconds, scale = (float)controls->timescale;
                float product = input * scale;
                if (!isfinite(product) || (double)product < -2147483648.0 || (double)product >= 2147483648.0)
                    return frame_time_fail(error, QA_ERROR_ARGUMENT, "undefined native common float-to-int time conversion");
                milliseconds = trunc((double)product);
            }
            if (controls->timescale != 0 && milliseconds < 1) milliseconds = 1;
        }
    } else if (dialect == QA_CONSOLE_Q2_RERELEASE) {
        milliseconds = trunc(supplied_milliseconds);
        if (milliseconds > 250) milliseconds = 100;
        if (controls->fixedtime != 0) {
            milliseconds = trunc(controls->fixedtime);
            if (milliseconds < 1) milliseconds = 1;
            if (milliseconds > 1000) milliseconds = 1000;
        } else if (controls->timescale > 0) {
            if (controls->timescale > FLT_MAX || milliseconds > FLT_MAX)
                return frame_time_fail(error, QA_ERROR_ARGUMENT, "source frame timing exceeds the native float domain");
            float input = (float)milliseconds, scale = (float)controls->timescale;
            float product = input * scale;
            milliseconds = product;
        }
    } else {
        milliseconds = trunc(supplied_milliseconds);
        if (fabs(controls->timescale) > FLT_MAX || milliseconds > FLT_MAX)
            return frame_time_fail(error, QA_ERROR_ARGUMENT, "source frame timing exceeds the native float domain");
        float scale = (float)controls->timescale;
        double fixedtime = trunc(controls->fixedtime), camera_mode = trunc(controls->camera_mode);
        if (fixedtime != 0) milliseconds = fixedtime;
        else if (scale != 0 || camera_mode != 0) {
            float input = (float)milliseconds;
            float product = input * scale;
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

bool qa_source_frame_time_admit(const qa_cvars *cvars, uint64_t pending_ns,
    bool server, bool *accepted, uint64_t *source_ns, qa_error *error)
{
    qa_source_frame_time_controls controls;
    if (!accepted || !source_ns || !qa_source_frame_time_controls_read(cvars, &controls, error)) return false;
    qa_console_dialect dialect = qa_cvars_dialect(cvars);
    if (!q1_dialect(dialect))
        return frame_time_fail(error, QA_ERROR_ARGUMENT, "host admission requires an actual Q1 registry");
    double seconds = (double)pending_ns / 1000000000.0;
    double ns;
    if (dialect == QA_CONSOLE_QW && server) {
        if (!isfinite(controls.server_minimum_seconds) || !isfinite(controls.server_maximum_seconds) ||
            controls.server_maximum_seconds <= 0)
            return frame_time_fail(error, QA_ERROR_ARGUMENT, "QW server physics controls must have a finite positive maximum");
        *accepted = pending_ns != 0 && seconds >= controls.server_minimum_seconds;
        *source_ns = 0;
        if (!*accepted) return true;
        double duration = seconds > controls.server_maximum_seconds ? controls.server_maximum_seconds : seconds;
        ns = duration * 1000000000;
    } else {
        double fps = 72;
        if (dialect == QA_CONSOLE_QW) {
            if (!isfinite(controls.maximum_fps) || !isfinite(controls.rate))
                return frame_time_fail(error, QA_ERROR_ARGUMENT, "QW client frame controls must be finite");
            float rate = (float)controls.rate;
            float rate_fps = rate / 80;
            fps = controls.maximum_fps != 0 ? controls.maximum_fps : rate_fps;
            if (fps < 30) fps = 30;
            if (fps > 72) fps = 72;
        }
        *accepted = pending_ns != 0 && seconds >= 1.0 / fps;
        *source_ns = 0;
        if (!*accepted) return true;
        double milliseconds;
        if (!qa_source_frame_time_transform(dialect, seconds * 1000, &controls, false, true,
            &milliseconds, error)) return false;
        ns = milliseconds * 1000000;
    }
    if (!isfinite(ns) || ns < 0 || ns >= 18446744073709551616.0)
        return frame_time_fail(error, QA_ERROR_ARGUMENT, "Source frame duration exceeds the native elapsed range");
    *source_ns = (uint64_t)ns;
    return true;
}
